//===- TaintAnalysis.cpp - Taint Analysis Pass in Backend ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/TaintAnalysis.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace llvm;

#define DEBUG_TYPE "taint-analysis"

static cl::opt<std::string> TaintSourcesFile("taint-src-file", cl::desc("A file specifying taint sources"), cl::value_desc("file"));

AnalysisKey TaintAnalysis::Key;

static Expected<StringMap<TaintSource>>
parseTaintSourcesFile(StringRef Filename) {
  auto BufferOrErr = MemoryBuffer::getFile(Filename);
  if (!BufferOrErr)
    return errorCodeToError(BufferOrErr.getError());

  StringMap<TaintSource> Result;

  for (line_iterator I(**BufferOrErr, /*SkipBlanks=*/true);
       !I.is_at_eof(); ++I) {

    StringRef Line = *I;

    // Skip comments
    if (Line.ltrim().starts_with("#"))
      continue;

    SmallVector<StringRef, 4> Fields;
    Line.split(Fields, ',');

    if (Fields.size() != 2) {
      return createStringError(
          inconvertibleErrorCode(),
          "Invalid format (expected func,argN) at line %u",
          I.line_number());
    }

    StringRef FuncName = Fields[0].trim();
    StringRef Spec = Fields[1].trim();

    if (FuncName.empty())
      return createStringError(
          inconvertibleErrorCode(),
          "Empty function name at line %u",
          I.line_number());

    auto &TS = Result[FuncName];
    TS.FuncName = FuncName.str();

    unsigned ArgNo;
    if (Spec.getAsInteger(10, ArgNo))
      return createStringError(
          inconvertibleErrorCode(),
          "Invalid argument index '%s' at line %u",
          Spec.str().c_str(), I.line_number());

    TS.TaintedArgs.insert(ArgNo);
  }

  return Result;
}

TaintAnalysis::TaintAnalysis() {
  if (TaintSourcesFile.getNumOccurrences() == 0)
    return;

  auto Parsed = parseTaintSourcesFile(TaintSourcesFile);
  if (!Parsed) {
    logAllUnhandledErrors(Parsed.takeError(), errs(),
                          "Error parsing taint source file: ");
    report_fatal_error("Failed to parse taint source file");
  }

  TaintSources = std::move(*Parsed);
}

TaintInfo TaintAnalysis::run(MachineFunction &MF,
                             MachineFunctionAnalysisManager &MFAM) {
  LLVM_DEBUG(dbgs() << "TaintAnalysis run on: " << MF.getName() << "\n");

  if (TaintSources.empty())
    return {};

  LLVM_DEBUG(dbgs() << "Start Taint Analysis\n");

  auto It = TaintSources.find(MF.getName());
  if (It == TaintSources.end()) {
    LLVM_DEBUG(dbgs() << "This MF doesn't have tainted arguments\n");
    return {};
  }

  const TaintSource TS = It->second;

  for (auto &arg: TS.TaintedArgs) {
    LLVM_DEBUG(dbgs() << arg << ' ');
  } LLVM_DEBUG(dbgs() << '\n');

  return TaintInfo {};
}

PreservedAnalyses TaintAnalysisPass::run(MachineFunction &MF,
                                         MachineFunctionAnalysisManager &MFAM) {
  auto PA = getMachineFunctionPassPreservedAnalyses();

  auto TI = MFAM.getResult<TaintAnalysis>(MF);

  return PA;
}
