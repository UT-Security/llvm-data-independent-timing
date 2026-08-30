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
#include "llvm/ADT/StringSet.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/IR/ValueMap.h"
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

// DIT clone list (docs/design/dit-callee-ownership.md §5, "function cloning").
//
// A helper shared between secret and public call sites can never qualify for
// AlwaysEnteredWithDIT, so it sets DIT at entry and (without relaxed ownership)
// clears it at exit, on EVERY call. Cloning gives the secret-context callers
// their own copy that assumes DIT is already on and therefore emits no switches
// at all -- the only way to remove the entry enable without the fail-dangerous
// static assumption, and the only way to reach oracle parity on a direct-call
// library like libsecp256k1.
//
// The list is a file of function names, one per line; the callee column of
// -taint-dit-reassert-report is exactly the right input. Taking a list rather
// than cloning everything keeps dead clones (and their code-size cost) out of
// the binary. A production version would compute this set internally instead of
// requiring the two-phase build.
static cl::opt<std::string> TaintDitCloneList(
    "taint-dit-clone-list",
    cl::desc("File of function names to clone for DIT-on call sites. Each gets "
             "a <name>.dit copy that assumes DIT is already set and emits no "
             "mode switches; the MIR pass redirects calls made from inside a "
             "DIT-on region to it."),
    cl::value_desc("file"), cl::Hidden);

static cl::opt<std::string> TaintSourcesFile("taint-src", cl::desc("A file specifying taint sources"), cl::value_desc("file"));

struct TaintSource {
  std::string FuncName;
  llvm::SmallSet<unsigned, 4> TaintedArgs;
  llvm::SmallSet<unsigned, 4> PointeeTaintedArgs;
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

    if (Fields.size() != 2 && Fields.size() != 3) {
      return createStringError(
          inconvertibleErrorCode(),
          "Invalid format (expected func,argN or func,argN,pointee) at line %u",
          I.line_number());
    }

    StringRef FuncName = Fields[0].trim();
    StringRef Spec = Fields[1].trim();
    StringRef Kind = Fields.size() == 3 ? Fields[2].trim() : "data";

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

    if (Kind == "data" || Kind.empty()) {
      TS.TaintedArgs.insert(ArgNo);
    } else if (Kind == "pointee") {
      TS.PointeeTaintedArgs.insert(ArgNo);
    } else {
      return createStringError(
          inconvertibleErrorCode(),
          "Invalid taint kind '%s' at line %u (expected data or pointee)",
          Kind.str().c_str(), I.line_number());
    }
  }

  return Result;
}

PreservedAnalyses TaintSourceAnnotatorPass::run(Module &M,
                                                ModuleAnalysisManager &MAM) {
  // An explicit path (e.g. from clang's -ftaint-harden) takes precedence over
  // the -taint-src command-line option.
  StringRef SourcesPath =
      !TaintSourcesPath.empty() ? StringRef(TaintSourcesPath)
                                : StringRef(TaintSourcesFile);
  if (SourcesPath.empty())
    return PreservedAnalyses::all();

  auto Parsed = parseTaintSourcesFile(SourcesPath);
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
      const auto &PointeeTaintedArgs = It->second.PointeeTaintedArgs;

      // A taint source must survive to codegen as a function.
      //
      // The seed is expressed as an attribute on a PARAMETER, so it lives
      // exactly as long as the function does. If the inliner folds the function
      // into its callers, the parameter -- and with it the only record that a
      // secret enters here -- ceases to exist, and the MIR pass later finds
      // nothing to protect. The build still succeeds and the binary is silently
      // unhardened, which is the worst way for this to fail.
      //
      // Measured: a seeded `static` function at -O2 produces ZERO DIT switches
      // without this, and 2 with it. Under LTO it is worse, because
      // internalization makes even externally-visible seeds inlinable, so a
      // whole-program link erases every seed in the module.
      //
      // Externally-visible seeds in a separately-compiled library (Bitcoin's
      // nine secp256k1 entry points, say) happen to survive without this, which
      // is why the hole went unnoticed -- it is luck about linkage, not a
      // property of the mechanism.
      if (F.hasFnAttribute(Attribute::AlwaysInline))
        F.removeFnAttr(Attribute::AlwaysInline);
      if (!F.hasFnAttribute(Attribute::NoInline)) {
        F.addFnAttr(Attribute::NoInline);
        Changed = true;
      }

      // Early run: keeping the function alive is the whole job. Argument
      // numbering is still subject to the middle-end here, so stamping is left
      // to the OptimizerLast run.
      if (PreserveFunctionsOnly)
        continue;

      for (Argument &Arg : F.args()) {
        if (TaintedArgs.contains(Arg.getArgNo()) &&
            !Arg.hasAttribute("tainted")) {
          Arg.addAttr(llvm::Attribute::get(Arg.getContext(), "tainted"));
          Changed = true;
        }
        if (PointeeTaintedArgs.contains(Arg.getArgNo()) &&
            !Arg.hasAttribute("tainted-pointee")) {
          Arg.addAttr(
              llvm::Attribute::get(Arg.getContext(), "tainted-pointee"));
          Changed = true;
        }
      }
  }

  // ---- DIT cloning -------------------------------------------------------
  // Create a <name>.dit copy of each listed function. The copy is marked with
  // the "taint-dit-clone" attribute; the MIR pass reads that and (a) emits NO
  // mode switches inside it -- it is entered with DIT already on, by
  // construction of the call-site rewriting -- and (b) redirects calls made
  // from inside a DIT-on region to it.
  //
  // Only local-linkage, address-not-taken functions are eligible. Local linkage
  // means every caller is in this TU and therefore visible to the MIR pass;
  // address-not-taken means no call can reach the original through a pointer
  // and bypass the rewrite. Both are the same conditions AlwaysEnteredWithDIT
  // uses, for the same reason.
  //
  // appendToUsed keeps the clone alive: it has no callers until the MIR pass
  // creates them, and any intervening DCE would otherwise delete it.
  if (!TaintDitCloneList.empty()) {
    auto Buf = MemoryBuffer::getFile(TaintDitCloneList);
    if (!Buf) {
      errs() << "taint: cannot read DIT clone list " << TaintDitCloneList
             << ": " << Buf.getError().message() << "\n";
      report_fatal_error("Failed to read DIT clone list");
    }
    StringSet<> Wanted;
    for (line_iterator LI(*Buf->get(), /*SkipBlanks=*/true, '#'); !LI.is_at_eof();
         ++LI)
      Wanted.insert(LI->trim());

    SmallVector<GlobalValue *, 8> KeepAlive;
    SmallVector<Function *, 8> ToClone;
    for (Function &F : M) {
      if (F.isDeclaration() || !Wanted.contains(F.getName()))
        continue;
      if (!F.hasLocalLinkage() || F.hasAddressTaken()) {
        errs() << "taint: skipping DIT clone of " << F.getName() << " ("
               << (F.hasLocalLinkage() ? "address taken" : "not local linkage")
               << ")\n";
        continue;
      }
      ToClone.push_back(&F);
    }
    for (Function *F : ToClone) {
      ValueToValueMapTy VMap;
      Function *Clone = CloneFunction(F, VMap);
      Clone->setName(F->getName() + ".dit");
      Clone->setLinkage(GlobalValue::InternalLinkage);
      Clone->addFnAttr("taint-dit-clone");
      KeepAlive.push_back(Clone);
      Changed = true;
    }
    if (!KeepAlive.empty())
      appendToUsed(M, KeepAlive);
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
