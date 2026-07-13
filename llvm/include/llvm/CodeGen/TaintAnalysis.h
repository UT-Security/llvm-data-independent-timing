//===- llvm/CodeGen/TaintAnalysis.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the TaintAnalysis pass which identifies tainted registers
// in MIR based on "tainted" attributes set on IR function arguments.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TAINTANALYSIS_H
#define LLVM_CODEGEN_TAINTANALYSIS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>

namespace llvm {

// Forward declarations
class TaintSummaryInfo;
class Module;
class TargetRegisterInfo;
class MachineInstr;
class GlobalVariable;
class AAResults;
class Value;

/// Cell keys: (base, (offset, size_in_bytes))
using StackCell = std::pair<int, std::pair<int64_t, uint64_t>>;
using GlobalCell =
    std::pair<const GlobalVariable *, std::pair<int64_t, uint64_t>>;

/// Command-line option for taint output file (shared across passes).
extern cl::opt<std::string> TaintOutputFile;

/// Command-line option for barrier protected-region output file.
extern cl::opt<std::string> TaintRegionsOutputFile;

/// Command-line option for source-line protected-region output file.
extern cl::opt<std::string> TaintSourceRegionsOutputFile;

/// Command-line option for inserting target instruction barriers around
/// tainted instructions.
extern cl::opt<bool> TaintInsertISB;

/// Kind of protection inserted around tainted code (selected with
/// -taint-barrier-mode; -taint-insert-isb remains the master switch).
enum class TaintBarrierKind {
  ISB, ///< Per-region ISB/DSB speculation barriers.
  DIT, ///< Function-granularity PSTATE.DIT data-independent-timing mode.
};

/// Command-line option selecting the protection mechanism.
extern cl::opt<TaintBarrierKind> TaintBarrierMode;

/// Command-line option for the call-site residual (escape) report: call sites
/// passing tainted/pointee-tainted arguments to callees the analysis cannot
/// instrument (external declarations, indirect calls).
extern cl::opt<std::string> TaintCallsiteReportFile;

/// Selects which of TaintState's register bitvectors an operation applies to.
enum class TaintKind {
  Data,    ///< The value itself is secret.
  Pointee, ///< The value is a pointer to secret memory.
  Address, ///< The value may be used as a secret-dependent memory address.
};

/// TaintState holds the result of taint analysis for a MachineFunction.
/// TaintedRegs tracks secret data values. PointeeTaintedRegs tracks pointer
/// values whose pointee memory is secret. AddressTaintedRegs tracks values that
/// may be used as secret-dependent memory addresses.
struct TaintState {
  SparseBitVector<> TaintedRegs;
  SparseBitVector<> PointeeTaintedRegs;
  SparseBitVector<> AddressTaintedRegs;
  DenseSet<StackCell> TaintedStackCells;
  DenseSet<StackCell> PointeeTaintedStackCells;
  DenseSet<GlobalCell> TaintedGlobalCells;
  DenseSet<const Value *> TaintedUnknownMemValues;
  DenseSet<const Value *> PointeeTaintedUnknownMemValues;
  bool UnknownMemTainted = false;

public:
  bool operator==(const TaintState &O) const {
    return TaintedRegs == O.TaintedRegs &&
           PointeeTaintedRegs == O.PointeeTaintedRegs &&
           AddressTaintedRegs == O.AddressTaintedRegs &&
           TaintedStackCells == O.TaintedStackCells &&
           PointeeTaintedStackCells == O.PointeeTaintedStackCells &&
           TaintedGlobalCells == O.TaintedGlobalCells &&
           TaintedUnknownMemValues == O.TaintedUnknownMemValues &&
           PointeeTaintedUnknownMemValues == O.PointeeTaintedUnknownMemValues &&
           UnknownMemTainted == O.UnknownMemTainted;
  }

  bool operator!=(const TaintState &O) const { return !(*this == O); }

  void join(const TaintState &O) {
    TaintedRegs |= O.TaintedRegs;
    PointeeTaintedRegs |= O.PointeeTaintedRegs;
    AddressTaintedRegs |= O.AddressTaintedRegs;
    for (const auto &C : O.TaintedStackCells)
      TaintedStackCells.insert(C);
    for (const auto &C : O.PointeeTaintedStackCells)
      PointeeTaintedStackCells.insert(C);
    for (const auto &C : O.TaintedGlobalCells)
      TaintedGlobalCells.insert(C);
    for (const Value *V : O.TaintedUnknownMemValues)
      TaintedUnknownMemValues.insert(V);
    for (const Value *V : O.PointeeTaintedUnknownMemValues)
      PointeeTaintedUnknownMemValues.insert(V);
    UnknownMemTainted |= O.UnknownMemTainted;
  }

  /// The register bitvector holding taint of kind K.
  SparseBitVector<> &regs(TaintKind K) {
    switch (K) {
    case TaintKind::Data:
      return TaintedRegs;
    case TaintKind::Pointee:
      return PointeeTaintedRegs;
    case TaintKind::Address:
      return AddressTaintedRegs;
    }
    llvm_unreachable("unhandled TaintKind");
  }
  const SparseBitVector<> &regs(TaintKind K) const {
    return const_cast<TaintState *>(this)->regs(K);
  }

  /// Check whether R carries taint of kind K.
  bool test(TaintKind K, Register R) const {
    return R.isValid() && regs(K).test(R.id());
  }

  /// Set or clear taint of kind K on R.
  void update(TaintKind K, Register R, bool Set) {
    if (!R.isValid())
      return;
    if (Set)
      regs(K).set(R.id());
    else
      regs(K).reset(R.id());
  }

  /// Check if a register is tainted.
  bool isTainted(Register R) const { return test(TaintKind::Data, R); }

  /// Check if a register points to tainted memory.
  bool isPointeeTainted(Register R) const {
    return test(TaintKind::Pointee, R);
  }

  /// Check if a register carries secret-dependent address taint.
  bool isAddressTainted(Register R) const {
    return test(TaintKind::Address, R);
  }

  // Stack cell methods
  void setTaintedStackCell(int FI, int64_t Off, uint64_t Sz) {
    TaintedStackCells.insert({FI, {Off, Sz}});
  }
  void clearTaintedStackCell(int FI, int64_t Off, uint64_t Sz) {
    TaintedStackCells.erase({FI, {Off, Sz}});
  }
  bool isTaintedStackCell(int FI, int64_t Off, uint64_t Sz) const {
    return TaintedStackCells.contains({FI, {Off, Sz}});
  }
  /// Fallback: any cell for this FI tainted? O(n) scan.
  bool anyTaintedStackCellForFI(int FI) const {
    for (const auto &C : TaintedStackCells)
      if (C.first == FI)
        return true;
    return false;
  }
  void setPointeeTaintedStackCell(int FI, int64_t Off, uint64_t Sz) {
    PointeeTaintedStackCells.insert({FI, {Off, Sz}});
  }
  void clearPointeeTaintedStackCell(int FI, int64_t Off, uint64_t Sz) {
    PointeeTaintedStackCells.erase({FI, {Off, Sz}});
  }
  bool isPointeeTaintedStackCell(int FI, int64_t Off, uint64_t Sz) const {
    return PointeeTaintedStackCells.contains({FI, {Off, Sz}});
  }
  bool anyPointeeTaintedStackCellForFI(int FI) const {
    for (const auto &C : PointeeTaintedStackCells)
      if (C.first == FI)
        return true;
    return false;
  }

  // Global cell methods
  void setTaintedGlobalCell(const GlobalVariable *GV, int64_t Off,
                            uint64_t Sz) {
    TaintedGlobalCells.insert({GV, {Off, Sz}});
  }
  void clearTaintedGlobalCell(const GlobalVariable *GV, int64_t Off,
                              uint64_t Sz) {
    TaintedGlobalCells.erase({GV, {Off, Sz}});
  }
  bool isTaintedGlobalCell(const GlobalVariable *GV, int64_t Off,
                           uint64_t Sz) const {
    return TaintedGlobalCells.contains({GV, {Off, Sz}});
  }
  bool anyTaintedGlobalCellForGV(const GlobalVariable *GV) const {
    for (const auto &C : TaintedGlobalCells)
      if (C.first == GV)
        return true;
    return false;
  }

  void setTaintedUnknownMemValue(const Value *V) {
    if (V)
      TaintedUnknownMemValues.insert(V);
  }
  void setPointeeTaintedUnknownMemValue(const Value *V) {
    if (V)
      PointeeTaintedUnknownMemValues.insert(V);
  }
  void clearPointeeTaintedUnknownMemValue(const Value *V) {
    if (V)
      PointeeTaintedUnknownMemValues.erase(V);
  }

  bool emptyRegs() const {
    return TaintedRegs.empty() && PointeeTaintedRegs.empty() &&
           AddressTaintedRegs.empty();
  }
  bool empty() const {
    return emptyRegs() && TaintedStackCells.empty() &&
           PointeeTaintedStackCells.empty() &&
           TaintedGlobalCells.empty() && TaintedUnknownMemValues.empty() &&
           PointeeTaintedUnknownMemValues.empty() &&
           !UnknownMemTainted;
  }

  unsigned countRegs() const {
    return TaintedRegs.count() + PointeeTaintedRegs.count() +
           AddressTaintedRegs.count();
  }
  unsigned countDataRegs() const { return TaintedRegs.count(); }
  unsigned countPointeeRegs() const { return PointeeTaintedRegs.count(); }
  unsigned countAddressRegs() const { return AddressTaintedRegs.count(); }
  unsigned countCells() const {
    return (unsigned)(TaintedStackCells.size() +
                      PointeeTaintedStackCells.size() +
                      TaintedGlobalCells.size() +
                      TaintedUnknownMemValues.size() +
                      PointeeTaintedUnknownMemValues.size());
  }
  unsigned count() const { return countRegs() + countCells(); }
};

/// TaintResult holds both the merged taint state and per-BB entry states.
/// The per-BB IN map is needed by the export pass to replay taint propagation
/// instruction-by-instruction and determine which specific instructions are
/// tainted.
struct TaintResult {
  TaintState Merged;
  DenseMap<const MachineBasicBlock *, TaintState> IN;
};

/// TaintAnalysis is a MachineFunction analysis that computes which registers
/// are tainted based on IR function argument attributes.
class TaintAnalysis : public AnalysisInfoMixin<TaintAnalysis> {
  friend AnalysisInfoMixin<TaintAnalysis>;
  static AnalysisKey Key;

public:
  using Result = TaintResult;

  /// Standard pass-manager entry point. Extracts TSI from MFAM proxy.
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);

  /// Direct entry point for interprocedural analysis.
  /// Bypasses pass manager; TSI is provided directly by the module pass.
  LLVM_ABI Result run(MachineFunction &MF, const TaintSummaryInfo *TSI,
                      AAResults *AA = nullptr);
};

/// TaintAnalysisPass is a MachineFunction pass that runs TaintAnalysis
/// and can be used to trigger the analysis in the pass pipeline.
class TaintAnalysisPass : public PassInfoMixin<TaintAnalysisPass> {
public:
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);

  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties();
  }
};

//===----------------------------------------------------------------------===//
// Shared helpers used by both TaintAnalysisPass and TaintInterprocPass
//===----------------------------------------------------------------------===//

/// Facts about one instruction, gathered during a taint replay. These are the
/// only questions consumers ask about the state surrounding an instruction, so
/// carrying them lets the replay avoid copying a whole TaintState per
/// instruction.
struct TaintFacts {
  bool UsesData = false;    ///< Reads a secret value (state entering MI).
  bool UsesPointee = false; ///< Reads a pointer to secret memory.
  bool UsesAddress = false; ///< Reads a secret-dependent address.
  bool DefsData = false;    ///< Defines a secret value (state leaving MI).
};

/// Replay the converged taint state through MF instruction by instruction,
/// seeding each block from TR.IN exactly as the analysis did.
///
/// Post, if given, sees the state leaving each instruction; Pre, if given, sees
/// the state entering it. Either may return false to stop the walk early.
void replayTaint(
    MachineFunction &MF, const TaintResult &TR, const TaintSummaryInfo *TSI,
    AAResults *AA,
    function_ref<bool(MachineInstr &, const TaintFacts &, const TaintState &)>
        Post,
    function_ref<bool(MachineInstr &, const TaintState &)> Pre = {});

/// True if MI is secret-dependent and therefore belongs inside a barrier region.
bool isTaintedInstruction(const MachineInstr &MI, const TaintFacts &F);

/// Find the called function from a call instruction.
/// Returns nullptr for indirect calls.
const Function *findCalledFunction(Module &M, const MachineInstr &MI);

/// Derive a sibling report path: ("out.txt", "_src") -> "out_src.txt".
SmallString<256> deriveReportPath(StringRef Base, StringRef Suffix);

/// Open a taint report file, truncating it unless Append is set. Returns null
/// both when Path is empty (the report was not requested) and when the file
/// cannot be opened, so callers can simply test the result; open failures are
/// reported on stderr.
std::unique_ptr<raw_fd_ostream> openTaintReport(StringRef Path, StringRef What,
                                                bool Append = false);

/// True if the function contains at least one coalesced tainted run, i.e.
/// insertTaintBarriers would instrument it. Used to compute the PreservesDIT
/// summary bit before barrier insertion.
bool functionHasTaintedRuns(MachineFunction &MF, const TaintResult &TR,
                            const TaintSummaryInfo *TSI,
                            AAResults *AA = nullptr);

/// Holds buffered per-function taint statistics for sorting before output.
struct FunctionTaintStats {
  std::string Output; // Formatted stats text
  double TaintRatio = 0.0;
};

/// Export tainted instructions for a single MachineFunction to the output
/// streams. OS receives the TSV instruction dump; SrcOS (if non-null)
/// receives the source-line summary; Stats (if non-null) receives
/// buffered per-function taint composition statistics.
void exportTaintedInstructions(MachineFunction &MF, const TaintResult &TR,
                               const TaintSummaryInfo *TSI, raw_ostream &OS,
                               raw_ostream *SrcOS,
                               FunctionTaintStats *Stats = nullptr,
                               AAResults *AA = nullptr);

/// Export the coalesced tainted instruction regions that would be protected by
/// hardening barriers. Returns the number of tainted instructions in those
/// regions.
unsigned exportTaintBarrierRegions(MachineFunction &MF, const TaintResult &TR,
                                   const TaintSummaryInfo *TSI,
                                   raw_ostream &OS, AAResults *AA = nullptr);

/// Export source-line ranges corresponding to coalesced tainted instruction
/// regions. Returns the number of source regions emitted.
unsigned exportTaintSourceRegions(MachineFunction &MF, const TaintResult &TR,
                                  const TaintSummaryInfo *TSI, raw_ostream &OS,
                                  AAResults *AA = nullptr);

/// Insert target instruction barriers before and after every coalesced region
/// of tainted instructions. If RegionsOS is non-null, also prints those
/// protected regions. Returns the number of tainted instructions protected.
unsigned insertTaintBarriers(MachineFunction &MF, const TaintResult &TR,
                             const TaintSummaryInfo *TSI,
                             raw_ostream *RegionsOS = nullptr,
                             AAResults *AA = nullptr);

} // namespace llvm

#endif // LLVM_CODEGEN_TAINTANALYSIS_H
