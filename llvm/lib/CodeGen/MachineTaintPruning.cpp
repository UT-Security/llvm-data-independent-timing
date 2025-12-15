//===- MachineTaintPruning.cpp - MIR-level Taint Analysis -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a MachineFunction pass that performs taint-based pruning
// at the machine instruction level.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineTaintPruning.h"
#include "llvm/Analysis/TaintAnalysis.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

#define DEBUG_TYPE "machine-taint-pruning"

// Defined in TaintAnalysis.cpp - shared between IR and MIR passes
extern cl::opt<std::string> TaintSummaryFile;

// Output file for leaky instructions
static cl::opt<std::string> LeakyInstsFile(
    "taint-leaky-insts-file",
    cl::desc("Output CSV file for potentially leaking instructions"),
    cl::value_desc("filename"));

// Map from function name to vector of tainted argument indices
static StringMap<SmallVector<unsigned, 8>> FunctionTaintSummaries;
static bool SummariesLoaded = false;

// Collected leaky instructions: (filename, line, function, leak_type, instruction)
struct LeakyInst {
  std::string Filename;
  unsigned Line;
  std::string Function;
  std::string LeakType;
  std::string Instruction;
};
static std::vector<LeakyInst> LeakyInstructions;
static bool OutputWritten = false;

/// Load function taint summaries from CSV file.
/// Format: function_name,arg0;arg1;arg2,returns_taint
static void loadTaintSummaries() {
  if (SummariesLoaded || TaintSummaryFile.empty())
    return;

  SummariesLoaded = true;

  auto BufOrErr = MemoryBuffer::getFile(TaintSummaryFile);
  if (!BufOrErr) {
    LLVM_DEBUG(dbgs() << "MachineTaintPruning: Could not open summary file: "
                      << TaintSummaryFile << "\n");
    return;
  }

  StringRef Content = (*BufOrErr)->getBuffer();
  SmallVector<StringRef, 0> Lines;
  Content.split(Lines, '\n');

  for (StringRef Line : Lines) {
    Line = Line.trim();
    if (Line.empty() || Line.starts_with("#"))
      continue;

    // Parse: function_name,arg_indices,returns_taint
    SmallVector<StringRef, 3> Parts;
    Line.split(Parts, ',');
    if (Parts.size() < 2)
      continue;

    StringRef FuncName = Parts[0].trim();
    StringRef ArgIndices = Parts[1].trim();

    SmallVector<unsigned, 8> TaintedArgs;
    if (!ArgIndices.empty()) {
      SmallVector<StringRef, 8> Indices;
      ArgIndices.split(Indices, ';');
      for (StringRef Idx : Indices) {
        unsigned ArgIdx;
        if (!Idx.trim().getAsInteger(10, ArgIdx)) {
          TaintedArgs.push_back(ArgIdx);
        }
      }
    }

    if (!TaintedArgs.empty()) {
      size_t NumArgs = TaintedArgs.size();
      FunctionTaintSummaries[FuncName] = std::move(TaintedArgs);
      LLVM_DEBUG(dbgs() << "MachineTaintPruning: Loaded summary for "
                        << FuncName << " with " << NumArgs
                        << " tainted args\n");
    }
  }

  LLVM_DEBUG(dbgs() << "MachineTaintPruning: Loaded "
                    << FunctionTaintSummaries.size()
                    << " function summaries\n");
}

/// Write collected leaky instructions to CSV file.
static void writeLeakyInstructions() {
  if (OutputWritten || LeakyInstsFile.empty() || LeakyInstructions.empty())
    return;

  OutputWritten = true;

  std::error_code EC;
  raw_fd_ostream OutFile(LeakyInstsFile, EC, sys::fs::OF_None);
  if (EC) {
    errs() << "MachineTaintPruning: Could not open output file: "
           << LeakyInstsFile << ": " << EC.message() << "\n";
    return;
  }

  // Write CSV header
  OutFile << "filename,line,function,leak_type,instruction\n";

  // Write each leaky instruction
  for (const LeakyInst &LI : LeakyInstructions) {
    // Escape any commas or quotes in the instruction string
    std::string EscapedInst = LI.Instruction;
    // Replace newlines with spaces
    for (char &C : EscapedInst) {
      if (C == '\n' || C == '\r')
        C = ' ';
    }

    OutFile << LI.Filename << "," << LI.Line << "," << LI.Function << ","
            << LI.LeakType << ",\"" << EscapedInst << "\"\n";
  }

  LLVM_DEBUG(dbgs() << "MachineTaintPruning: Wrote " << LeakyInstructions.size()
                    << " leaky instructions to " << LeakyInstsFile << "\n");
}

char MachineTaintPruning::ID = 0;

INITIALIZE_PASS(MachineTaintPruning, "machine-taint-pruning",
                "Machine Taint Pruning Pass", false, false)

MachineTaintPruning::MachineTaintPruning() : MachineFunctionPass(ID) {
  initializeMachineTaintPruningPass(*PassRegistry::getPassRegistry());
}

void MachineTaintPruning::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool MachineTaintPruning::runOnMachineFunction(MachineFunction &MF) {
  CurMF = &MF;
  TaintedRegs.clear();
  TaintedFrameIndices.clear();

  // Load summaries on first invocation
  loadTaintSummaries();

  // Check if this function has tainted args - skip if not
  StringRef FuncName = MF.getName();
  auto It = FunctionTaintSummaries.find(FuncName);
  if (It == FunctionTaintSummaries.end()) {
    // No taint summary for this function, skip it
    return false;
  }

  LLVM_DEBUG(dbgs() << "MachineTaintPruning: Processing function "
                    << FuncName << "\n");

  // Initialize taint from function summaries
  initializeTaintFromSummary(MF);

  // Propagate taint through instructions
  propagateTaint(MF);

  // Report potentially leaking instructions
  unsigned LeakCount = 0;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (couldLeak(MI)) {
        reportLeakingInstr(MI);
        LeakCount++;
      }
    }
  }

  LLVM_DEBUG(if (LeakCount > 0) {
    dbgs() << "MachineTaintPruning: Found " << LeakCount
           << " potentially leaking instructions in " << FuncName << "\n";
  });

  // This pass doesn't modify the code (analysis only)
  return false;
}

void MachineTaintPruning::initializeTaintFromSummary(MachineFunction &MF) {
  StringRef FuncName = MF.getName();

  auto It = FunctionTaintSummaries.find(FuncName);
  if (It == FunctionTaintSummaries.end())
    return;

  const SmallVector<unsigned, 8> &TaintedArgs = It->second;
  LLVM_DEBUG(dbgs() << "  " << TaintedArgs.size() << " tainted args\n");

  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  // Log which arguments are tainted
  LLVM_DEBUG({
    for (unsigned ArgIdx : TaintedArgs) {
      dbgs() << "    Arg " << ArgIdx << " is tainted\n";
    }
  });

  // Map argument indices to physical registers based on calling convention
  // For x86-64 System V ABI (Linux): RDI, RSI, RDX, RCX, R8, R9 for first 6 int args
  // For other targets, we fall back to marking all live-ins (conservative)

  Triple TT(MF.getFunction().getParent()->getTargetTriple());

  if (TT.isX86() && TT.isArch64Bit()) {
    // x86-64 System V ABI argument registers
    // Note: Using register names that TRI can match against live-ins
    static const char *X86_64ArgRegNames[] = {
      "RDI", "RSI", "RDX", "RCX", "R8", "R9"
    };
    const unsigned NumArgRegs = 6;

    for (unsigned ArgIdx : TaintedArgs) {
      if (ArgIdx < NumArgRegs) {
        // Find the register by name in live-ins
        for (const auto &LI : MF.getRegInfo().liveins()) {
          Register Reg = LI.first;
          StringRef RegName = TRI->getName(Reg);
          if (RegName == X86_64ArgRegNames[ArgIdx]) {
            TaintedRegs.insert(Reg);
            LLVM_DEBUG(dbgs() << "    Marked " << RegName
                              << " (arg " << ArgIdx << ") as tainted\n");
            break;
          }
        }
      } else {
        // Args 6+ are passed on stack - mark as tainted frame indices
        // The exact frame index depends on the function's stack layout
        // For now, we note this limitation
        LLVM_DEBUG(dbgs() << "    Arg " << ArgIdx
                          << " passed on stack (not tracked precisely)\n");
      }
    }
  } else {
    // Fallback for non-x86-64: mark all live-ins as tainted (conservative)
    if (!TaintedArgs.empty()) {
      for (const auto &LI : MF.getRegInfo().liveins()) {
        Register Reg = LI.first;
        TaintedRegs.insert(Reg);
        LLVM_DEBUG(dbgs() << "    Marked live-in " << printReg(Reg, TRI)
                          << " as tainted (conservative)\n");
      }
    }
  }
}

void MachineTaintPruning::propagateTaint(MachineFunction &MF) {
  // Forward dataflow: process instructions in program order
  // If any source register is tainted, mark destination registers as tainted

  bool Changed = true;
  unsigned Iterations = 0;
  const unsigned MaxIterations = 100; // Prevent infinite loops

  while (Changed && Iterations < MaxIterations) {
    Changed = false;
    Iterations++;

    for (MachineBasicBlock &MBB : MF) {
      for (MachineInstr &MI : MBB) {
        // Skip debug instructions
        if (MI.isDebugInstr())
          continue;

        // Check if any source operand is tainted
        bool HasTaintedSource = false;
        for (const MachineOperand &MO : MI.operands()) {
          if (MO.isReg() && MO.isUse() && MO.getReg().isValid()) {
            if (TaintedRegs.count(MO.getReg())) {
              HasTaintedSource = true;
              break;
            }
          }
          if (MO.isFI() && TaintedFrameIndices.count(MO.getIndex())) {
            HasTaintedSource = true;
            break;
          }
        }

        // If tainted source, mark all destinations as tainted
        if (HasTaintedSource) {
          for (const MachineOperand &MO : MI.operands()) {
            if (MO.isReg() && MO.isDef() && MO.getReg().isValid()) {
              if (!TaintedRegs.count(MO.getReg())) {
                TaintedRegs.insert(MO.getReg());
                Changed = true;
                LLVM_DEBUG(dbgs() << "  Tainted: " << printReg(MO.getReg())
                                  << " from " << MI);
              }
            }
          }

          // Handle stores to stack
          if (MI.mayStore()) {
            for (const MachineOperand &MO : MI.operands()) {
              if (MO.isFI()) {
                if (!TaintedFrameIndices.count(MO.getIndex())) {
                  TaintedFrameIndices.insert(MO.getIndex());
                  Changed = true;
                  LLVM_DEBUG(dbgs() << "  Tainted stack slot: " << MO.getIndex()
                                    << " from " << MI);
                }
              }
            }
          }
        }

        // Handle loads from tainted stack locations
        if (MI.mayLoad()) {
          for (const MachineOperand &MO : MI.operands()) {
            if (MO.isFI() && TaintedFrameIndices.count(MO.getIndex())) {
              // Mark destination register as tainted
              for (const MachineOperand &DefMO : MI.operands()) {
                if (DefMO.isReg() && DefMO.isDef() && DefMO.getReg().isValid()) {
                  if (!TaintedRegs.count(DefMO.getReg())) {
                    TaintedRegs.insert(DefMO.getReg());
                    Changed = true;
                    LLVM_DEBUG(dbgs() << "  Tainted from stack load: "
                                      << printReg(DefMO.getReg()) << " from "
                                      << MI);
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  LLVM_DEBUG(dbgs() << "  Propagation: " << Iterations << " iters, "
                    << TaintedRegs.size() << " regs, "
                    << TaintedFrameIndices.size() << " stack slots\n");
}

bool MachineTaintPruning::couldLeak(const MachineInstr &MI) const {
  // Check if any operand is tainted
  bool HasTaintedOperand = false;
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg() && MO.getReg().isValid() &&
        TaintedRegs.count(MO.getReg())) {
      HasTaintedOperand = true;
      break;
    }
    if (MO.isFI() && TaintedFrameIndices.count(MO.getIndex())) {
      HasTaintedOperand = true;
      break;
    }
  }

  // Report any instruction that has a tainted operand
  if (HasTaintedOperand)
    return true;

  // Also report loads - they might be loading tainted data from memory
  // (we can't always track memory taint precisely)
  if (MI.mayLoad())
    return true;

  return false;
}

void MachineTaintPruning::reportLeakingInstr(const MachineInstr &MI) const {
  LLVM_DEBUG({
    dbgs() << "LEAK: ";
    MI.print(dbgs());
  });

  // Determine leak type
  std::string LeakType;
  if (MI.mayStore())
    LeakType = "store";
  else if (MI.isConditionalBranch())
    LeakType = "branch";
  else if (MI.mayLoad())
    LeakType = "load";
  else
    LeakType = "other";

  // Get source location from debug info
  std::string Filename = "unknown";
  unsigned Line = 0;
  const DebugLoc &DL = MI.getDebugLoc();
  if (DL) {
    if (auto *Scope = DL.get()) {
      if (auto *File = Scope->getFile()) {
        Filename = File->getFilename().str();
      }
      Line = DL.getLine();
    }
  }

  // Get function name
  std::string FuncName = CurMF ? CurMF->getName().str() : "unknown";

  // Get instruction as string
  std::string InstStr;
  raw_string_ostream OS(InstStr);
  MI.print(OS, /*IsStandalone=*/true, /*SkipOpers=*/false,
           /*SkipDebugLoc=*/true, /*AddNewLine=*/false);

  // Add to collection
  LeakyInstructions.push_back({Filename, Line, FuncName, LeakType, InstStr});
}

FunctionPass *llvm::createMachineTaintPruningPass() {
  return new MachineTaintPruning();
}

// Register a destructor to write output when the pass manager is done
namespace {
struct LeakyInstsWriter {
  ~LeakyInstsWriter() {
    writeLeakyInstructions();
  }
};
static LeakyInstsWriter Writer;
} // anonymous namespace
