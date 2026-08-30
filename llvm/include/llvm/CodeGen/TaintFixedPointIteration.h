//===- TaintFixedPointIteration.h - Interprocedural Taint -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Module-level pass that runs interprocedural taint analysis via fixed-point
// iteration. This is the main entry point: it orchestrates intraprocedural
// analysis, summary collection, caller-to-callee argument propagation,
// callee-to-caller return propagation, and result export.
//
// Usage:
//   llc -passes='taint-interproc' -taint-output=out.txt input.pe.mir
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TAINTFIXEDPOINTITERATION_H
#define LLVM_CODEGEN_TAINTFIXEDPOINTITERATION_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;
class MachineFunction;
class MachineFunctionPass;
class MachineModuleInfo;
class ModulePass;

/// How the analysis finds the MachineFunction for an IR Function.
///
/// The two pipelines keep MachineFunctions in different places: the new PM
/// caches them per-function in the FunctionAnalysisManager (via
/// MachineFunctionAnalysis), while the legacy codegen pipeline owns them in
/// MachineModuleInfo. The analysis does not care which, so it takes a lookup
/// rather than a manager. Returns null when \p F has no MachineFunction, which
/// callers treat as "skip this function".
using MachineFunctionLookup = function_ref<MachineFunction *(Function &)>;

/// Everything the interprocedural taint analysis needs to reach per-function
/// state. \c GetMF supplies MachineFunctions (see above); \c FAM supplies alias
/// analysis, which both pipelines want identically and neither may drop --
/// losing AA silently coarsens taint and changes how many DIT switches land.
struct TaintMFContext {
  MachineFunctionLookup GetMF;
  FunctionAnalysisManager &FAM;
};

/// Run interprocedural taint analysis over every MachineFunction of \p M that
/// \p Ctx can resolve, applying whatever the -taint-* options ask for (DIT
/// switch insertion, region export, reports).
///
/// Shared by the new-PM TaintInterprocPass below and by the legacy codegen
/// pipeline, which runs it between the MachineFunction passes and the
/// AsmPrinter -- the one point where every MachineFunction of the module is
/// simultaneously resident without serializing the module to MIR text and back.
void runTaintInterproc(Module &M, TaintMFContext Ctx);

class TaintInterprocPass : public PassInfoMixin<TaintInterprocPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

/// True if any function in \p M carries a taint-source attribute.
///
/// The attributes are stamped on the IR by the taint-annotate pass at compile
/// time and travel inside the bitcode, so this is how a *link-time* codegen
/// backend discovers that a module wants hardening -- it needs no flag and no
/// access to the original seed file.
bool moduleHasTaintSources(const Module &M);

/// Legacy module pass running runTaintInterproc over the MachineFunctions held
/// by \p MMI, using \p FAM for alias analysis. Both must outlive the pass.
///
/// Add via addPassesToEmitFileWithPostPrologEpilogModulePasses, which is the
/// point where every MachineFunction of the module is resident at once.
/// Reserve, before PrologEpilogInserter lays the frame out, the 8-byte slot that
/// holds a function's incoming PSTATE.DIT for the callee-saved DIT ABI. Must be
/// scheduled pre-PEI; see TargetPassConfig::setPrePrologEpilogCallback.
MachineFunctionPass *createTaintDITSlotReservePass();

ModulePass *createTaintInterprocLegacyPass(MachineModuleInfo &MMI,
                                           FunctionAnalysisManager &FAM);

} // namespace llvm

#endif // LLVM_CODEGEN_TAINTFIXEDPOINTITERATION_H
