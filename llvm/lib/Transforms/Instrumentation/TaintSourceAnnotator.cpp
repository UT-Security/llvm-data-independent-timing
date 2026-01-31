//===----- TaintSourceAnnotator.cpp - a pass annotates taint arguments ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/TaintSourceAnnotator.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace llvm;

#define DEBUG_TYPE "taint-annotate"

static cl::opt<std::string> TaintSourcesFile("taint-src", cl::desc("A file specifying taint sources"), cl::value_desc("file"));

struct TaintSource {
  std::string FuncName;
  llvm::SmallSet<unsigned, 4> TaintedArgs;
};

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

PreservedAnalyses TaintSourceAnnotatorPass::run(Module &M,
                                                ModuleAnalysisManager &MAM) {
  if (TaintSourcesFile.empty())
    return PreservedAnalyses::all();

  auto Parsed = parseTaintSourcesFile(TaintSourcesFile);
  if (!Parsed) {
    logAllUnhandledErrors(Parsed.takeError(), errs(),
                          "Error parsing taint source file: ");
    report_fatal_error("Failed to parse taint source file");
  }

  StringMap<TaintSource> &TaintSources = *Parsed;

  bool Changed = false;

  for (Function &F : M) {
      if (F.isDeclaration())
        continue;

      auto It = TaintSources.find(F.getName());
      if (It == TaintSources.end())
        continue;

      const auto &TaintedArgs = It->second.TaintedArgs;

      for (Argument &Arg : F.args()) {
        if (!TaintedArgs.contains(Arg.getArgNo()))
          continue;
        if (!Arg.hasAttribute("tainted")) {
          Arg.addAttr(llvm::Attribute::get(Arg.getContext(), "tainted"));
          Changed = true;
        }
      }
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
