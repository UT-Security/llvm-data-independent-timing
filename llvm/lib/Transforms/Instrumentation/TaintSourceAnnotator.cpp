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
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

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

// The general form of the above: every SEEDED function gets the twin, with its
// original linkage, so a caller in another TU can name it. The MIR pass
// (ditCloneFor) redirects a DIT-on call to `f` to `f.dit` when `f` is defined
// here, or when `f` is a seeded declaration the -taint-owned-symbols list says
// this build defines - so every TU of the build must be compiled with this flag,
// and the module flag it sets is how the pass knows this one was. The original
// stays as it is for the tables and pointers that name it; only direct calls
// from DIT-on code are redirected, which is why the eligibility rules of the
// listed form (local linkage, address not taken) are not needed here.
static cl::opt<bool> TaintDitCloneSeeded(
    "taint-dit-clone-seeded", cl::Hidden, cl::init(false),
    cl::desc("Give every seeded function, and every function it reaches by "
             "direct call in its TU, a <name>.dit twin that is entered with "
             "PSTATE.DIT already set and emits no switch of its own; the MIR "
             "pass redirects calls made from DIT-on code to it, in this TU and "
             "across TUs for seeded callees the -taint-owned-symbols list "
             "covers. Compile every TU of the build with it."));

static cl::opt<std::string> TaintSourcesFile("taint-src", cl::desc("A file specifying taint sources"), cl::value_desc("file"));

// Seed validation. A seed is a parameter attribute stamped on a DEFINITION,
// so a seed line that names a function this TU only declares, or does not
// contain at all, or an argument index the function does not have, applies to
// nothing - and until now did so silently. That is how a seed set can look
// complete while protecting three percent of the secret work (mbedTLS TLS 1.3
// resumption, 2026-09-02: every ticket-path seed was live, the ECDHE scalar
// had no seed, and nothing said so). CIO ships the identical hole: its ABI
// table maps argument indices 0-5, so four of its 64 libsodium seed lines
// (index 8) are never applied.
//
// Two mechanisms. An argument index a DEFINED function does not have is a hard
// error, because it can never apply anywhere. Whether a seed applied in SOME
// TU is only knowable after the whole build, so every TU appends one record
// per seed line to this report - APPLIED / DECLARED / ABSENT - and
// utils/taint_seed_check.py flags any seed with no APPLIED record. It appends
// for the same reason -taint-info-loss-report does: truncating per invocation
// would keep only the last TU's verdict.
static cl::opt<std::string> TaintSeedReport(
    "taint-seed-report",
    cl::desc("Append one APPLIED/DECLARED/ABSENT record per seed line per TU, "
             "for utils/taint_seed_check.py to find seeds that applied nowhere"),
    cl::value_desc("file"));

struct TaintSource {
  std::string FuncName;
  llvm::SmallSet<unsigned, 4> TaintedArgs;
  llvm::SmallSet<unsigned, 4> PointeeTaintedArgs;
  llvm::SmallSet<unsigned, 4> DeclassifiedArgs;
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
    } else if (Kind == "declassify") {
      // DECLASSIFICATION (CryptoMPK's `sinktaint`): taint arriving on this
      // parameter is dropped. The caller may hand it a secret-derived value;
      // this function is asserting that the value is public by then.
      //
      // Scoped to the PARAMETER, deliberately not to the object it points at.
      // Object granularity is what makes CryptoMPK's version unsound, and
      // libhydrogen shows exactly why: `hydro_sign_prehash` keeps the ephemeral
      // SECRET at `&csig[32]` while `csig` is the public signature buffer, so
      // declassifying that object would strip protection from the secret it
      // temporarily holds. A parameter tag cannot do that - it stops taint at
      // one call boundary and says nothing about the storage.
      TS.DeclassifiedArgs.insert(ArgNo);
    } else {
      return createStringError(
          inconvertibleErrorCode(),
          "Invalid taint kind '%s' at line %u (expected data, pointee or "
          "declassify)",
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
      auto It = TaintSources.find(F.getName());

      if (F.isDeclaration()) {
        // A seed naming a function this TU only DECLARES leaves no other trace.
        // The seed is a PARAMETER attribute and parameters live with the body,
        // which is in a different translation unit compiled by a different
        // process. So the interprocedural report had no way to tell "you never
        // annotated this callee" from "you annotated it, in the TU that defines
        // it" - and kept emitting the same repair suggestion for a callee the
        // user had already seeded, on every rebuild, forever.
        //
        // Read ONLY by the information-loss report, never by the analysis, so
        // it cannot move a switch. Verified: the hardened library is
        // byte-identical with and without it.
        if (It != TaintSources.end()) {
          // Carry the seeded ARGUMENT INDICES, not just the name. A name-only
          // marker would suppress the report for a callee that is seeded
          // PARTIALLY - key seeded, message not - which is exactly the case
          // worth still hearing about. The consumer compares these against the
          // arguments actually carrying taint at the call site and stays quiet
          // only when the seed covers all of them.
          auto mask = [](const llvm::SmallSet<unsigned, 4> &S) {
            unsigned M = 0;
            for (unsigned I : S)
              if (I < 32)
                M |= 1u << I;
            return M;
          };
          F.addFnAttr("taint-seeded-elsewhere",
                      (Twine(mask(It->second.TaintedArgs)) + "," +
                       Twine(mask(It->second.PointeeTaintedArgs)))
                          .str());
        }
        continue;
      }

      if (It == TaintSources.end())
        continue;

      const auto &TaintedArgs = It->second.TaintedArgs;
      const auto &PointeeTaintedArgs = It->second.PointeeTaintedArgs;

      // An index this function does not have can never be stamped, in this TU
      // or any other: the definition is right here and it has arg_size()
      // parameters. Silently skipping it is how a seed set lies about its
      // coverage, so this is fatal rather than a warning.
      {
        const unsigned NArgs = F.arg_size();
        auto CheckRange = [&](const llvm::SmallSet<unsigned, 4> &Idx,
                              StringRef Kind) {
          for (unsigned A : Idx)
            if (A >= NArgs) {
              errs() << "taint: seed " << F.getName() << "," << A
                     << (Kind == "data" ? "" : ",")
                     << (Kind == "data" ? "" : Kind) << " names argument "
                     << A << " but " << F.getName() << " has only " << NArgs
                     << " parameter(s); the seed would apply to nothing\n";
              report_fatal_error("taint seed argument index out of range");
            }
        };
        CheckRange(TaintedArgs, "data");
        CheckRange(PointeeTaintedArgs, "pointee");
        CheckRange(It->second.DeclassifiedArgs, "declassify");
      }

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
        if (It->second.DeclassifiedArgs.contains(Arg.getArgNo()) &&
            !Arg.hasAttribute("declassified")) {
          Arg.addAttr(llvm::Attribute::get(Arg.getContext(), "declassified"));
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

  // ---- Seeded-function cloning, cross-TU -----------------------------------
  // Stamping run only: the clone must copy the parameter attributes the loop
  // above just applied, and the early run's argument numbering is not final.
  if (TaintDitCloneSeeded && !PreserveFunctionsOnly) {
    // The MODULE carries the fact. The MIR pass reads this flag to decide that
    // a seeded declaration has a `.dit` twin in the TU that defines it - a TU
    // it never sees - and names the twin on that strength alone.
    if (!M.getModuleFlag("taint-dit-clone-seeded"))
      M.addModuleFlag(Module::Override, "taint-dit-clone-seeded", 1);
    // Every seeded definition, and every definition a seeded one reaches
    // through direct calls inside this TU. The MIR pass instruments a callee
    // that receives the secret by propagation exactly as it does a seeded one
    // (its own enable and clear), and a DIT-on caller can skip those toggles
    // and its own re-assert only if the callee has a twin - so the twin set
    // must cover what propagation reaches, which at this point is the direct
    // call graph under the seeds. Over-approximate on purpose: a reached
    // function that turns out to hold no secret costs its twin's code size and
    // nothing else, because a twin never emits a switch that the whole-function
    // path would not emit for the original. Across TUs the set stays the
    // seeded one (ditCloneFor), since a caller cannot see what a declaration
    // reaches.
    auto cloneable = [](const Function &F) {
      return !F.isDeclaration() && !F.isIntrinsic() &&
             !F.hasFnAttribute("taint-dit-clone");
    };
    SmallPtrSet<Function *, 32> Wanted;
    SmallVector<Function *, 32> Worklist;
    for (Function &F : M)
      if (cloneable(F) && TaintSources.count(F.getName()) &&
          Wanted.insert(&F).second)
        Worklist.push_back(&F);
    while (!Worklist.empty()) {
      Function *F = Worklist.pop_back_val();
      for (Instruction &I : instructions(*F))
        if (auto *CB = dyn_cast<CallBase>(&I))
          if (Function *Callee = CB->getCalledFunction())
            if (cloneable(*Callee) && Wanted.insert(Callee).second)
              Worklist.push_back(Callee);
    }
    SmallVector<Function *, 16> ToClone;
    for (Function &F : M) { // module order: deterministic output
      if (!Wanted.contains(&F))
        continue;
      if (M.getFunction((F.getName() + ".dit").str()))
        continue; // the listed form already made it
      ToClone.push_back(&F);
    }
    SmallVector<GlobalValue *, 16> KeepAlive;
    for (Function *F : ToClone) {
      ValueToValueMapTy VMap;
      Function *Clone = CloneFunction(F, VMap);
      Clone->setName(F->getName() + ".dit");
      // Linkage and visibility stay the original's: an external `f` has an
      // external `f.dit` for other TUs to name, a static one an internal twin.
      Clone->addFnAttr("taint-dit-clone");
      // A discardable twin (internal, linkonce) has no caller until the MIR
      // pass makes one; keep it from any DCE in between.
      if (Clone->isDiscardableIfUnused())
        KeepAlive.push_back(Clone);
      Changed = true;
    }
    if (!KeepAlive.empty())
      appendToUsed(M, KeepAlive);
  }

  // Per-seed verdict for this TU. Only in the stamping run - the early
  // preserve-only run sees the same module and would double every record.
  if (!TaintSeedReport.empty() && !PreserveFunctionsOnly) {
    std::error_code EC;
    raw_fd_ostream OS(TaintSeedReport, EC,
                      sys::fs::OF_Append | sys::fs::OF_Text);
    if (EC) {
      errs() << "taint: cannot open seed report " << TaintSeedReport << ": "
             << EC.message() << "\n";
    } else {
      StringRef Src = M.getSourceFileName();
      for (const auto &KV : TaintSources) {
        const Function *F = M.getFunction(KV.first());
        const char *St = !F ? "ABSENT"
                         : F->isDeclaration() ? "DECLARED"
                                              : "APPLIED";
        auto Emit = [&](const llvm::SmallSet<unsigned, 4> &Idx,
                        const char *Kind) {
          for (unsigned A : Idx)
            OS << St << " func=" << KV.first() << " arg=" << A
               << " kind=" << Kind << " src=" << Src << "\n";
        };
        Emit(KV.second.TaintedArgs, "data");
        Emit(KV.second.PointeeTaintedArgs, "pointee");
        Emit(KV.second.DeclassifiedArgs, "declassify");
      }
    }
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
