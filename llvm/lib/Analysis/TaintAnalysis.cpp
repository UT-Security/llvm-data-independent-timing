//===- TaintAnalysis.cpp - Interprocedural Taint Analysis ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements an interprocedural taint analysis using a PRUNING
// approach: conservatively assume ALL instructions could leak sensitive data,
// then use taint analysis to PROVE which instructions are safe (don't use
// tainted data). Instructions that cannot be proven safe are reported.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/TaintAnalysis.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/InitializePasses.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <map>

using namespace llvm;

#define DEBUG_TYPE "taint-analysis"

// Command-line option for specifying the CSV file with taint sources
static cl::opt<std::string> TaintSourcesFile(
    "taint-sources-file",
    cl::desc("CSV file specifying taint sources (FunctionName,ArgumentIndex)"),
    cl::value_desc("filename"));

// Command-line option for specifying output file for sensitive lines
// Not static so it can be accessed from TaintFenceInsertion.cpp
cl::opt<std::string> TaintOutputFile(
    "taint-output-file",
    cl::desc("Output file for sensitive source lines report"),
    cl::value_desc("filename"));

// Command-line option for function summaries output file
// Not static so it can be accessed from TaintFenceInsertion.cpp
cl::opt<std::string> TaintSummaryFile(
    "taint-summary-file",
    cl::desc("Output file for function taint summaries (for MIR pass)"),
    cl::value_desc("filename"));

//===----------------------------------------------------------------------===//
// PotentiallyLeakingInfo Implementation
//===----------------------------------------------------------------------===//

TaintInfo::PotentiallyLeakingInfo::PotentiallyLeakingInfo(const Value *V,
                                                           const Argument *SourceArg)
    : V(V), SourceArg(SourceArg), Inst(dyn_cast<Instruction>(V)),
      LineNumber(0) {
  // Try to get debug line number
  if (Inst) {
    if (const DebugLoc &DL = Inst->getDebugLoc()) {
      LineNumber = DL.getLine();
    }
  }
}

//===----------------------------------------------------------------------===//
// SensitiveInstInfo Implementation
//===----------------------------------------------------------------------===//

TaintInfo::SensitiveInstInfo::SensitiveInstInfo(const Instruction *I,
                                                 SensitiveReason R)
    : Inst(I), Reason(R), LineNumber(0) {
  if (const DebugLoc &DL = I->getDebugLoc()) {
    LineNumber = DL.getLine();
  }
}

//===----------------------------------------------------------------------===//
// TaintInfo Implementation
//===----------------------------------------------------------------------===//

TaintInfo::TaintInfo(Module &M, CallGraph &CG) : M(M), CG(CG) {
  // Load sources from CSV file if specified
  if (!TaintSourcesFile.empty()) {
    if (!loadSourcesFromCSV(TaintSourcesFile)) {
      errs() << "Warning: Failed to load taint sources from "
             << TaintSourcesFile << "\n";
    }
  }

  runAnalysis();
}

bool TaintInfo::isTainted(const Value *V) const {
  return TaintedValues.count(V) > 0;
}

TaintLattice::Value TaintInfo::getTaint(const llvm::Value *V) const {
  // Constants are never tainted
  if (isa<Constant>(V))
    return TaintLattice::Notaint;
  return TaintedValues.count(V) > 0 ? TaintLattice::Taint : TaintLattice::Notaint;
}

//===----------------------------------------------------------------------===//
// BAP-style Taint Computation
//===----------------------------------------------------------------------===//

TaintLattice::Value TaintInfo::computeInstructionTaint(const Instruction *I) const {
  using TL = TaintLattice;

  // Binary operators: join taint of both operands
  if (const auto *BinOp = dyn_cast<BinaryOperator>(I)) {
    return TL::binop(getTaint(BinOp->getOperand(0)),
                     getTaint(BinOp->getOperand(1)));
  }

  // Unary operators / casts: preserve taint
  if (const auto *Cast = dyn_cast<CastInst>(I)) {
    return TL::unop(getTaint(Cast->getOperand(0)));
  }

  if (const auto *Unary = dyn_cast<UnaryOperator>(I)) {
    return TL::unop(getTaint(Unary->getOperand(0)));
  }

  // Load: taint from pointer operand (conservative - no memory tracking)
  if (const auto *LI = dyn_cast<LoadInst>(I)) {
    return getTaint(LI->getPointerOperand());
  }

  // GEP: join base pointer + all indices
  if (const auto *GEP = dyn_cast<GetElementPtrInst>(I)) {
    TL::Value Result = getTaint(GEP->getPointerOperand());
    for (const Use &Idx : GEP->indices()) {
      Result = TL::join(Result, getTaint(Idx.get()));
    }
    return Result;
  }

  // PHI: join taint of all incoming values
  if (const auto *PHI = dyn_cast<PHINode>(I)) {
    TL::Value Result = TL::Notaint;
    for (unsigned i = 0; i < PHI->getNumIncomingValues(); ++i) {
      Result = TL::join(Result, getTaint(PHI->getIncomingValue(i)));
    }
    return Result;
  }

  // Select: join condition + both values
  if (const auto *Sel = dyn_cast<SelectInst>(I)) {
    return TL::join(getTaint(Sel->getCondition()),
             TL::join(getTaint(Sel->getTrueValue()),
                      getTaint(Sel->getFalseValue())));
  }

  // Compare: join both operands
  if (const auto *Cmp = dyn_cast<CmpInst>(I)) {
    return TL::binop(getTaint(Cmp->getOperand(0)),
                     getTaint(Cmp->getOperand(1)));
  }

  // Call: conservative - if any tainted arg, result may be tainted
  if (const auto *CI = dyn_cast<CallInst>(I)) {
    TL::Value Result = TL::Notaint;
    for (unsigned i = 0; i < CI->arg_size(); ++i) {
      Result = TL::join(Result, getTaint(CI->getArgOperand(i)));
    }
    return Result;
  }

  // Default: check all operands
  TL::Value Result = TL::Notaint;
  for (const Use &Op : I->operands()) {
    Result = TL::join(Result, getTaint(Op.get()));
  }
  return Result;
}

//===----------------------------------------------------------------------===//
// Sensitive Instruction Detection (BAP-style)
//===----------------------------------------------------------------------===//

void TaintInfo::checkSensitiveInstruction(const Instruction *I) {
  // Branch on tainted condition -> timing leak
  if (const auto *BI = dyn_cast<BranchInst>(I)) {
    if (BI->isConditional() && isTainted(BI->getCondition())) {
      SensitiveInsts.emplace_back(I, SensitiveReason::TaintedBranchCondition);
      LLVM_DEBUG(dbgs() << "  [SENSITIVE] Branch on tainted condition: ";
                 I->print(dbgs()); dbgs() << "\n");
    }
    return;
  }

  // Switch on tainted condition -> timing leak
  if (const auto *SI = dyn_cast<SwitchInst>(I)) {
    if (isTainted(SI->getCondition())) {
      SensitiveInsts.emplace_back(I, SensitiveReason::TaintedBranchCondition);
      LLVM_DEBUG(dbgs() << "  [SENSITIVE] Switch on tainted condition: ";
                 I->print(dbgs()); dbgs() << "\n");
    }
    return;
  }

  // Load from tainted address -> cache timing leak
  if (const auto *LI = dyn_cast<LoadInst>(I)) {
    if (isTainted(LI->getPointerOperand())) {
      SensitiveInsts.emplace_back(I, SensitiveReason::TaintedLoadAddress);
      LLVM_DEBUG(dbgs() << "  [SENSITIVE] Load from tainted address: ";
                 I->print(dbgs()); dbgs() << "\n");
    }
    return;
  }

  // Store to tainted address -> cache timing leak
  if (const auto *SI = dyn_cast<StoreInst>(I)) {
    if (isTainted(SI->getPointerOperand())) {
      SensitiveInsts.emplace_back(I, SensitiveReason::TaintedStoreAddress);
      LLVM_DEBUG(dbgs() << "  [SENSITIVE] Store to tainted address: ";
                 I->print(dbgs()); dbgs() << "\n");
    }
    return;
  }

  // Variable-time operations on tainted data (div/rem)
  if (const auto *BinOp = dyn_cast<BinaryOperator>(I)) {
    unsigned Opcode = BinOp->getOpcode();
    if (Opcode == Instruction::UDiv || Opcode == Instruction::SDiv ||
        Opcode == Instruction::URem || Opcode == Instruction::SRem) {
      if (isTainted(I)) {
        SensitiveInsts.emplace_back(I, SensitiveReason::TaintedDivision);
        LLVM_DEBUG(dbgs() << "  [SENSITIVE] Variable-time op on tainted data: ";
                   I->print(dbgs()); dbgs() << "\n");
      }
    }
  }
}

/// Helper to convert SensitiveReason to string
static const char *sensitiveReasonToString(SensitiveReason R) {
  switch (R) {
  case SensitiveReason::TaintedBranchCondition:
    return "Branch on tainted condition (timing leak)";
  case SensitiveReason::TaintedLoadAddress:
    return "Load from tainted address (cache timing)";
  case SensitiveReason::TaintedStoreAddress:
    return "Store to tainted address (cache timing)";
  case SensitiveReason::TaintedDivision:
    return "Variable-time division on tainted data";
  case SensitiveReason::TaintedValue:
    return "Tainted value";
  }
  return "Unknown";
}

/// Helper to read a specific line from a source file
static std::string getSourceLine(StringRef Filename, unsigned LineNo) {
  auto FileOrErr = MemoryBuffer::getFile(Filename);
  if (!FileOrErr)
    return "";

  StringRef Content = (*FileOrErr)->getBuffer();
  SmallVector<StringRef, 128> Lines;
  Content.split(Lines, '\n', /*MaxSplit=*/-1, /*KeepEmpty=*/true);

  if (LineNo == 0 || LineNo > Lines.size())
    return "";

  return Lines[LineNo - 1].str();  // LineNo is 1-indexed
}

void TaintInfo::print(raw_ostream &OS) const {
  OS << "===== Taint Analysis Results =====\n";
  OS << "Taint Source Arguments: " << SourceArguments.size() << "\n";
  for (const Argument *Arg : SourceArguments) {
    OS << "  " << Arg->getParent()->getName() << "::arg" << Arg->getArgNo()
       << "\n";
  }

  // Print ALL tainted instructions (everything that operates on secrets)
  OS << "\n======================================================\n";
  OS << "ALL TAINTED INSTRUCTIONS: " << TaintedValues.size() << "\n";
  OS << "======================================================\n";

  // Group by function for readability
  std::map<const Function *, SmallVector<const Instruction *, 16>> FuncToTaintedInsts;

  for (const Value *V : TaintedValues) {
    if (const auto *I = dyn_cast<Instruction>(V)) {
      FuncToTaintedInsts[I->getFunction()].push_back(I);
    }
  }

  for (const auto &Entry : FuncToTaintedInsts) {
    const Function *F = Entry.first;
    const auto &Insts = Entry.second;

    OS << "\nFunction: " << F->getName() << " (" << Insts.size() << " tainted instructions)\n";
    OS << "------------------------------------------------------\n";

    for (const Instruction *I : Insts) {
      OS << "  ";
      if (const DebugLoc &DL = I->getDebugLoc()) {
        OS << "[Line " << DL.getLine() << "] ";
        // Print the actual source code line
        std::string SrcLine = getSourceLine(DL->getFilename(), DL.getLine());
        if (!SrcLine.empty()) {
          // Trim leading whitespace for cleaner output
          StringRef Trimmed = StringRef(SrcLine).ltrim();
          OS << "src: " << Trimmed << "\n       ";
        }
      }
      OS << "IR: ";
      I->print(OS);
      OS << "\n";
    }
  }

  // Print sensitive instructions (timing channel specific)
  OS << "\n======================================================\n";
  OS << "SENSITIVE INSTRUCTIONS (potential timing leaks): "
     << SensitiveInsts.size() << "\n";
  OS << "======================================================\n";

  for (const auto &Info : SensitiveInsts) {
    OS << "  [";
    if (Info.LineNumber > 0) {
      OS << "Line " << Info.LineNumber;
    } else {
      OS << "no debug info";
    }
    OS << "] " << sensitiveReasonToString(Info.Reason) << "\n";
    // Print source line
    if (const DebugLoc &DL = Info.Inst->getDebugLoc()) {
      std::string SrcLine = getSourceLine(DL->getFilename(), DL.getLine());
      if (!SrcLine.empty()) {
        StringRef Trimmed = StringRef(SrcLine).ltrim();
        OS << "    src: " << Trimmed << "\n";
      }
    }
    OS << "    in " << Info.Inst->getFunction()->getName() << ": ";
    Info.Inst->print(OS);
    OS << "\n";
  }

  // Print summary of unique source lines that are tainted
  OS << "\n======================================================\n";
  OS << "Summary: Tainted Source Lines\n";
  OS << "======================================================\n";

  // Collect unique (filename, line) pairs
  std::map<std::pair<std::string, unsigned>, SmallVector<const Instruction *, 4>> LineToInsts;

  for (const Value *V : TaintedValues) {
    const auto *I = dyn_cast<Instruction>(V);
    if (!I)
      continue;

    // Get source file info
    if (const DebugLoc &DL = I->getDebugLoc()) {
      std::string Filename = DL->getFilename().str();
      unsigned Line = DL.getLine();
      LineToInsts[{Filename, Line}].push_back(I);
    }
  }

  if (LineToInsts.empty()) {
    OS << "(No debug information available - compile with -g)\n";
  } else {
    for (const auto &Entry : LineToInsts) {
      const auto &FileLine = Entry.first;
      const auto &Insts = Entry.second;
      OS << FileLine.first << ":" << FileLine.second;
      // Print the actual source line
      std::string SrcLine = getSourceLine(FileLine.first, FileLine.second);
      if (!SrcLine.empty()) {
        StringRef Trimmed = StringRef(SrcLine).ltrim();
        OS << "  |  " << Trimmed;
      }
      OS << " (" << Insts.size() << " instructions)\n";
    }
  }

  OS << "======================================================\n";
}

void TaintInfo::writeSensitiveLinesToFile(raw_ostream &OS) const {
  // Collect unique entries keyed by (filename, line, function)
  // Value is a struct containing instruction details
  struct LineInfo {
    std::string FunctionName;
    std::string TaintedArgs;  // For call instructions: which args are tainted
    unsigned Count = 0;
  };

  // Key: (filename, line, function) -> LineInfo
  std::map<std::tuple<std::string, unsigned, std::string>, LineInfo> TaintedLines;

  for (const auto &Info : SensitiveInsts) {
    const Instruction *Inst = Info.Inst;
    if (!Inst)
      continue;

    const DebugLoc &DL = Inst->getDebugLoc();
    if (!DL)
      continue;

    std::string Filename = DL->getFilename().str();
    unsigned Line = DL.getLine();

    // Get the function containing this instruction
    std::string FuncName = Inst->getFunction()->getName().str();

    auto Key = std::make_tuple(Filename, Line, FuncName);
    auto &LI = TaintedLines[Key];
    LI.FunctionName = FuncName;
    LI.Count++;

    // For call instructions, identify which arguments are tainted
    if (const CallInst *CI = dyn_cast<CallInst>(Inst)) {
      SmallVector<unsigned, 4> TaintedArgIndices;
      for (unsigned i = 0; i < CI->arg_size(); ++i) {
        if (isTainted(CI->getArgOperand(i))) {
          TaintedArgIndices.push_back(i);
        }
      }

      if (!TaintedArgIndices.empty()) {
        std::string ArgStr;
        raw_string_ostream ArgOS(ArgStr);

        // Get callee name if available
        Function *Callee = CI->getCalledFunction();
        if (Callee) {
          ArgOS << Callee->getName().str() << "(";
        } else {
          ArgOS << "<indirect>(";
        }

        for (size_t i = 0; i < TaintedArgIndices.size(); ++i) {
          if (i > 0) ArgOS << ",";
          ArgOS << "arg" << TaintedArgIndices[i];
        }
        ArgOS << ")";

        // Append to existing if there are multiple calls on same line
        if (LI.TaintedArgs.empty()) {
          LI.TaintedArgs = ArgStr;
        } else if (LI.TaintedArgs.find(ArgStr) == std::string::npos) {
          LI.TaintedArgs += "; " + ArgStr;
        }
      }
    }
  }

  // Don't write anything if there are no tainted lines
  if (TaintedLines.empty())
    return;

  for (const auto &Entry : TaintedLines) {
    const std::string &Filename = std::get<0>(Entry.first);
    unsigned Line = std::get<1>(Entry.first);
    const LineInfo &LI = Entry.second;

    OS << Filename << ":" << Line << " [" << LI.FunctionName << "]";

    // Add tainted args info if present
    if (!LI.TaintedArgs.empty()) {
      OS << " tainted: " << LI.TaintedArgs;
    }

    // Print source line
    std::string SrcLine = getSourceLine(Filename, Line);
    if (!SrcLine.empty()) {
      StringRef Trimmed = StringRef(SrcLine).ltrim();
      OS << " | " << Trimmed;
    }

    OS << "\n";
  }
}

bool TaintInfo::loadSourcesFromCSV(StringRef FilePath) {
  // Read the CSV file
  auto FileOrErr = MemoryBuffer::getFile(FilePath);
  if (!FileOrErr) {
    errs() << "Error reading file: " << FilePath << "\n";
    return false;
  }

  StringRef Content = (*FileOrErr)->getBuffer();
  SmallVector<StringRef, 32> Lines;
  Content.split(Lines, '\n', /*MaxSplit=*/-1, /*KeepEmpty=*/false);

  for (StringRef Line : Lines) {
    Line = Line.trim();
    if (Line.empty() || Line.starts_with("#"))
      continue; // Skip empty lines and comments

    // Parse: FunctionName,ArgumentIndex
    auto CommaPos = Line.find(',');
    if (CommaPos == StringRef::npos) {
      errs() << "Warning: Malformed line in CSV (missing comma): " << Line
             << "\n";
      continue;
    }

    StringRef FuncName = Line.substr(0, CommaPos).trim();
    StringRef ArgIndexStr = Line.substr(CommaPos + 1).trim();

    unsigned ArgIndex;
    if (ArgIndexStr.getAsInteger(10, ArgIndex)) {
      errs() << "Warning: Invalid argument index: " << ArgIndexStr << "\n";
      continue;
    }

    // Add to map
    FunctionArgSources[FuncName].push_back(ArgIndex);
  }

  return true;
}

void TaintInfo::runAnalysis() {
  // PRUNING APPROACH (function-level with deduplication):
  // PHASE 1: Forward dataflow for each secret
  //   - Track tainted instructions per function
  //   - Deduplicate across secrets (don't re-taint same instruction)
  // PHASE 2: Add conservative instructions (loads/stores) in touched functions

  // Step 1: Identify source arguments from CSV
  identifySources();

  if (SourceArguments.empty()) {
    errs() << "Warning: No taint sources identified. "
           << "Make sure to specify -taint-sources-file=<file.csv>\n";
    return;
  }

  // PHASE 1: Run forward dataflow for each secret
  for (const Argument *SourceArg : SourceArguments) {
    SmallVector<const Value *, 64> Worklist;

    // Debug: print which secret we're processing
    const Function *F = SourceArg->getParent();
    LLVM_DEBUG(dbgs() << "\n[PHASE1 DEBUG] Processing secret: " << F->getName()
                      << " arg" << SourceArg->getArgNo() << "\n");

    // Initialize: mark source argument as tainted
    TaintedValues.insert(SourceArg);
    ValueToSource[SourceArg] = SourceArg;
    Worklist.push_back(SourceArg);

    // Mark the function containing this source as touched
    FunctionTaintedInsts[F];  // Ensure function entry exists

    // Propagate taint until fixpoint
    while (!Worklist.empty()) {
      const Value *V = Worklist.pop_back_val();
      propagateTaint(V, SourceArg, Worklist);
    }

    LLVM_DEBUG(dbgs() << "[PHASE1 DEBUG] After processing " << F->getName()
                      << " arg" << SourceArg->getArgNo() << ": "
                      << TaintedValues.size() << " tainted values\n");
  }

  // PHASE 2: Report all tainted instructions
  LLVM_DEBUG(dbgs() << "\n=== PHASE 2: Collecting tainted instructions ===\n");
  for (const auto &FuncEntry : FunctionTaintedInsts) {
    const Function *F = FuncEntry.first;
    const auto &TaintedSet = FuncEntry.second;

    LLVM_DEBUG(dbgs() << "Touched function: " << F->getName()
                      << " (tainted instructions: " << TaintedSet.size() << ")\n");

    // Report all tainted instructions in this function
    for (const Instruction *I : TaintedSet) {
      const Argument *SrcArg = ValueToSource[I];
      PotentiallyLeakingInsts.emplace_back(I, SrcArg);

      // Also add to SensitiveInsts for file output
      SensitiveInsts.emplace_back(I, SensitiveReason::TaintedValue);
    }
  }
  LLVM_DEBUG(dbgs() << "=== PHASE 2 Complete ===\n\n");

  // PHASE 3: Compute function summaries for MIR pass
  computeFunctionSummaries();
}

void TaintInfo::identifySources() {
  LLVM_DEBUG(dbgs() << "[SOURCE DEBUG] Looking for taint sources from CSV:\n");
  LLVM_DEBUG(for (const auto &Entry : FunctionArgSources) {
    dbgs() << "  CSV entry: " << Entry.first() << " args: ";
    for (unsigned Idx : Entry.second) {
      dbgs() << Idx << " ";
    }
    dbgs() << "\n";
  });

  // Find all functions in the module that match CSV entries
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;

    StringRef FuncName = F.getName();
    auto It = FunctionArgSources.find(FuncName);
    if (It == FunctionArgSources.end())
      continue;

    LLVM_DEBUG(dbgs() << "[SOURCE DEBUG] Found function: " << FuncName
                      << " with " << F.arg_size() << " args\n");

    // Mark specified arguments as sources
    const auto &ArgIndices = It->second;
    for (unsigned ArgIdx : ArgIndices) {
      if (ArgIdx < F.arg_size()) {
        Argument *Arg = F.getArg(ArgIdx);
        SourceArguments.push_back(Arg);
        LLVM_DEBUG(dbgs() << "  -> Marked arg" << ArgIdx << " as taint source\n");
      } else {
        errs() << "Warning: Argument index " << ArgIdx << " out of range for "
               << FuncName << " (has " << F.arg_size() << " arguments)\n";
      }
    }
  }

  LLVM_DEBUG(dbgs() << "[SOURCE DEBUG] Total source arguments identified: "
                    << SourceArguments.size() << "\n");
}
 
void TaintInfo::propagateTaint(const Value *V, const Argument *SourceArg,
                                SmallVectorImpl<const Value *> &Worklist) {
  // Propagate to all users (forward dataflow)
  // Note: We use simple forward propagation here - if a value is tainted,
  // all its users become tainted. This is sound but may over-approximate.
  // The computeInstructionTaint function can be used for more precise
  // checking in specific contexts (e.g., sensitivity detection).

  for (const User *U : V->users()) {
    bool AlreadyTainted = TaintedValues.count(U) > 0;

    if (!AlreadyTainted) {
      // Mark as tainted
      TaintedValues.insert(U);
      ValueToSource[U] = SourceArg;
      Worklist.push_back(U);

      // If this is an instruction, add to per-function tracking
      if (const auto *I = dyn_cast<Instruction>(U)) {
        const Function *F = I->getFunction();
        FunctionTaintedInsts[F].insert(I);
      }
    }

    // Special handling for calls (interprocedural)
    // We ALWAYS check calls even if already tainted, because we may need
    // to propagate newly tainted arguments to the callee
    if (const auto *CI = dyn_cast<CallInst>(U)) {
      handleCall(CI, SourceArg, Worklist);
    }
  }

  // Note: Backward propagation removed for simplicity
  // In pruning approach, untouched functions remain fully conservative
}

void TaintInfo::handleCall(const CallInst *CI, const Argument *SourceArg,
                            SmallVectorImpl<const Value *> &Worklist) {
  Function *Callee = CI->getCalledFunction();

  // Debug: Show call being analyzed
  LLVM_DEBUG({
    const Function *Caller = CI->getFunction();
    dbgs() << "[CALL DEBUG] " << Caller->getName() << " -> "
           << (Callee ? Callee->getName() : "<indirect>") << "\n";
    dbgs() << "  Call instruction: ";
    CI->print(dbgs());
    dbgs() << "\n";
  });

  // First pass: check if ANY argument is tainted
  bool HasTaintedInput = false;
  for (unsigned i = 0; i < CI->arg_size(); ++i) {
    if (isTainted(CI->getArgOperand(i))) {
      HasTaintedInput = true;
      LLVM_DEBUG(dbgs() << "  Found tainted input at arg" << i << "\n");
      break;
    }
  }

  if (!HasTaintedInput) {
    LLVM_DEBUG(dbgs() << "  No tainted inputs, skipping\n");
    return;
  }

  // Second pass: if any input is tainted, mark all pointer args as tainted
  // (they might be output buffers that receive tainted data)
  // This is the CONSERVATIVE OUTPUT TAINTING approach
  LLVM_DEBUG(dbgs() << "  Applying conservative output tainting:\n");

  for (unsigned i = 0; i < CI->arg_size(); ++i) {
    Value *ArgVal = CI->getArgOperand(i);
    bool ArgTainted = isTainted(ArgVal);

    // Mark pointer arguments as tainted (could be output buffers)
    if (ArgVal->getType()->isPointerTy() && !ArgTainted) {
      LLVM_DEBUG({
        dbgs() << "    arg" << i << " (ptr): ";
        ArgVal->printAsOperand(dbgs(), false);
        dbgs() << " -> marking as tainted (potential output)\n";
      });

      TaintedValues.insert(ArgVal);
      ValueToSource[ArgVal] = SourceArg;
      Worklist.push_back(ArgVal);

      // Track in caller function
      if (const auto *I = dyn_cast<Instruction>(ArgVal)) {
        FunctionTaintedInsts[I->getFunction()].insert(I);
      }
    }

    // Also propagate to callee parameters (if we have the callee body)
    if (ArgTainted && Callee && !Callee->isDeclaration() &&
        i < Callee->arg_size()) {
      Argument *Param = Callee->getArg(i);
      if (!TaintedValues.count(Param)) {
        LLVM_DEBUG({
          dbgs() << "    arg" << i << ": propagating to " << Callee->getName()
                 << " param" << i << "\n";
        });
        TaintedValues.insert(Param);
        ValueToSource[Param] = SourceArg;
        Worklist.push_back(Param);

        // Mark callee function as touched
        FunctionTaintedInsts[Callee];  // Ensure entry exists
      }
    }
  }

  // If function returns tainted data, the call itself is tainted
  if (Callee && !Callee->isDeclaration() && !CI->getType()->isVoidTy()) {
    for (Instruction &I : instructions(*Callee)) {
      if (auto *RI = dyn_cast<ReturnInst>(&I)) {
        if (Value *RetVal = RI->getReturnValue()) {
          if (isTainted(RetVal)) {
            // Mark call as tainted (already done by propagateTaint, but ensure it)
            break;
          }
        }
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// Function Summary Methods (for MIR pass)
//===----------------------------------------------------------------------===//

void TaintInfo::computeFunctionSummaries() {
  LLVM_DEBUG(dbgs() << "\n=== PHASE 3: Computing function summaries ===\n");

  // For each function in the module
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;

    // Compute which arguments are tainted
    BitVector TaintedArgs(F.arg_size(), false);
    for (unsigned i = 0; i < F.arg_size(); ++i) {
      Argument *Arg = F.getArg(i);
      if (isTainted(Arg)) {
        TaintedArgs.set(i);
      }
    }

    // Only store if at least one arg is tainted
    if (TaintedArgs.any()) {
      FunctionTaintedArgs[&F] = std::move(TaintedArgs);
      LLVM_DEBUG(dbgs() << "  " << F.getName() << ": ");
      LLVM_DEBUG(for (unsigned i = 0; i < F.arg_size(); ++i) {
        if (FunctionTaintedArgs[&F].test(i)) dbgs() << "arg" << i << " ";
      });
      LLVM_DEBUG(dbgs() << "\n");
    }

    // Check if function returns tainted value
    bool ReturnsTaint = false;
    for (BasicBlock &BB : F) {
      if (auto *RI = dyn_cast<ReturnInst>(BB.getTerminator())) {
        if (Value *RetVal = RI->getReturnValue()) {
          if (isTainted(RetVal)) {
            ReturnsTaint = true;
            break;
          }
        }
      }
    }

    if (ReturnsTaint) {
      FunctionReturnsTaint[&F] = true;
      LLVM_DEBUG(dbgs() << "  " << F.getName() << " returns taint\n");
    }
  }

  LLVM_DEBUG(dbgs() << "=== PHASE 3 Complete: " << FunctionTaintedArgs.size()
                    << " functions with tainted args ===\n\n");
}

bool TaintInfo::isArgTainted(const Function *F, unsigned ArgIdx) const {
  auto It = FunctionTaintedArgs.find(F);
  if (It == FunctionTaintedArgs.end())
    return false;
  if (ArgIdx >= It->second.size())
    return false;
  return It->second.test(ArgIdx);
}

bool TaintInfo::doesReturnTaint(const Function *F) const {
  auto It = FunctionReturnsTaint.find(F);
  if (It == FunctionReturnsTaint.end())
    return false;
  return It->second;
}

BitVector TaintInfo::getTaintedArgs(const Function *F) const {
  auto It = FunctionTaintedArgs.find(F);
  if (It == FunctionTaintedArgs.end())
    return BitVector(F->arg_size(), false);
  return It->second;
}

SmallVector<const Function *, 16> TaintInfo::getFunctionsWithTaintedArgs() const {
  SmallVector<const Function *, 16> Result;
  for (const auto &Entry : FunctionTaintedArgs) {
    Result.push_back(Entry.first);
  }
  return Result;
}

void TaintInfo::writeFunctionSummaries(raw_ostream &OS) const {
  OS << "# Function Taint Summaries\n";
  OS << "# Format: function_name,tainted_arg_indices,returns_taint\n";

  for (const auto &Entry : FunctionTaintedArgs) {
    const Function *F = Entry.first;
    const BitVector &TaintedArgs = Entry.second;

    OS << F->getName() << ",";

    // Write tainted arg indices
    bool First = true;
    for (unsigned i = 0; i < TaintedArgs.size(); ++i) {
      if (TaintedArgs.test(i)) {
        if (!First) OS << ";";
        OS << i;
        First = false;
      }
    }

    // Write return taint status
    OS << "," << (doesReturnTaint(F) ? "true" : "false") << "\n";
  }
}

//===----------------------------------------------------------------------===//
// TaintAnalysis Implementation
//===----------------------------------------------------------------------===//

AnalysisKey TaintAnalysis::Key;

TaintInfo TaintAnalysis::run(Module &M, ModuleAnalysisManager &MAM) {
  // Get CallGraph analysis
  CallGraph &CG = MAM.getResult<CallGraphAnalysis>(M);

  // Construct and return TaintInfo (performs analysis in constructor)
  return TaintInfo(M, CG);
}

//===----------------------------------------------------------------------===//
// TaintAnalysisPrinterPass Implementation
//===----------------------------------------------------------------------===//

PreservedAnalyses TaintAnalysisPrinterPass::run(Module &M,
                                                  ModuleAnalysisManager &MAM) {
  TaintInfo &TI = MAM.getResult<TaintAnalysis>(M);
  TI.print(OS);

  // Write sensitive lines to file if output file is specified (append mode)
  if (!TaintOutputFile.empty()) {
    std::error_code EC;
    raw_fd_ostream OutFile(TaintOutputFile, EC, sys::fs::OF_Append);
    if (EC) {
      errs() << "Error opening taint output file: " << EC.message() << "\n";
    } else {
      TI.writeSensitiveLinesToFile(OutFile);
    }
  }

  // Write function taint summaries to file if specified
  if (!TaintSummaryFile.empty()) {
    std::error_code EC;
    raw_fd_ostream SummaryFile(TaintSummaryFile, EC, sys::fs::OF_None);
    if (EC) {
      errs() << "Error opening taint summary file: " << EC.message() << "\n";
    } else {
      TI.writeFunctionSummaries(SummaryFile);
    }
  }

  return PreservedAnalyses::all();
}

//===----------------------------------------------------------------------===//
// TaintAnalysisWrapperPass Implementation
//===----------------------------------------------------------------------===//

char TaintAnalysisWrapperPass::ID = 0;

TaintAnalysisWrapperPass::TaintAnalysisWrapperPass() : ImmutablePass(ID) {
  // Initialize pass (no registration needed for ImmutablePass used manually)
}

INITIALIZE_PASS(TaintAnalysisWrapperPass, "taint-analysis-wrapper",
                "Taint Analysis Wrapper Pass", false, true)
