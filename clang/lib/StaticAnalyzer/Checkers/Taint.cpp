//=== Taint.cpp - Taint tracking and basic propagation rules. ------*- C++ -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines basic, non-domain-specific mechanisms for tracking tainted values.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/Taint.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include <optional>

using namespace clang;
using namespace ento;
using namespace taint;

// Fully tainted symbols.
REGISTER_MAP_WITH_PROGRAMSTATE(TaintMap, SymbolRef, TaintTagType)

// Partially tainted symbols.
REGISTER_MAP_FACTORY_WITH_PROGRAMSTATE(TaintedSubRegions, const SubRegion *,
                                       TaintTagType)
REGISTER_MAP_WITH_PROGRAMSTATE(DerivedSymTaint, SymbolRef, TaintedSubRegions)

namespace {
/// Taint can only ever be observed through these two maps, so while both are
/// empty nothing in the state is tainted and a query can be answered without
/// walking the symbol and region graphs at all. This is the case that
/// dominates: taint enters the state only through a taint source, and most
/// translation units never call one.
bool isTaintFree(ProgramStateRef State) {
  return State->get<TaintMap>().isEmpty() &&
         State->get<DerivedSymTaint>().isEmpty();
}

/// A taint query either needs every tainted symbol (to mark them interesting in
/// a report) or just whether there is one (to decide control flow), so the walk
/// takes an accumulator and a flag saying when it may stop early.
using TaintedSymbols = SmallVectorImpl<SymbolRef>;

/// Append to \p Out every symbol carrying taint of kind \p Kind that the
/// argument depends on. Returns true when the walk should stop, that is, when
/// \p FirstOnly was requested and something has been found.
bool findTainted(ProgramStateRef State, SVal V, TaintTagType Kind,
                 bool FirstOnly, TaintedSymbols &Out);
bool findTainted(ProgramStateRef State, SymbolRef Sym, TaintTagType Kind,
                 bool FirstOnly, TaintedSymbols &Out);
bool findTainted(ProgramStateRef State, const MemRegion *Reg, TaintTagType Kind,
                 bool FirstOnly, TaintedSymbols &Out);

bool findTainted(ProgramStateRef State, SVal V, TaintTagType Kind,
                 bool FirstOnly, TaintedSymbols &Out) {
  if (SymbolRef Sym = V.getAsSymbol())
    return findTainted(State, Sym, Kind, FirstOnly, Out);
  if (const MemRegion *Reg = V.getAsRegion())
    return findTainted(State, Reg, Kind, FirstOnly, Out);

  if (auto LCV = V.getAs<nonloc::LazyCompoundVal>()) {
    StoreManager &StoreMgr = State->getStateManager().getStoreManager();
    if (auto DefaultVal = StoreMgr.getDefaultBinding(*LCV))
      return findTainted(State, *DefaultVal, Kind, FirstOnly, Out);
  }

  return false;
}

bool findTainted(ProgramStateRef State, const MemRegion *Reg, TaintTagType Kind,
                 bool FirstOnly, TaintedSymbols &Out) {
  if (!Reg)
    return false;

  // Element region (array element) is tainted if the offset is tainted.
  if (const auto *ER = dyn_cast<ElementRegion>(Reg))
    if (findTainted(State, ER->getIndex(), Kind, FirstOnly, Out))
      return true;

  // Symbolic region is tainted if the corresponding symbol is tainted.
  if (const auto *SR = dyn_cast<SymbolicRegion>(Reg))
    if (findTainted(State, SR->getSymbol(), Kind, FirstOnly, Out))
      return true;

  // Any subregion (including Element and Symbolic regions) is tainted if its
  // super-region is tainted.
  if (const auto *SubReg = dyn_cast<SubRegion>(Reg))
    return findTainted(State, SubReg->getSuperRegion(), Kind, FirstOnly, Out);

  return false;
}

bool findTainted(ProgramStateRef State, SymbolRef Sym, TaintTagType Kind,
                 bool FirstOnly, TaintedSymbols &Out) {
  if (!Sym)
    return false;

  // HACK:https://discourse.llvm.org/t/rfc-make-istainted-and-complex-symbols-friends/79570
  if (const auto &Opts = State->getAnalysisManager().getAnalyzerOptions();
      Sym->computeComplexity() > Opts.MaxTaintedSymbolComplexity)
    return false;

  // Traverse all the symbols this symbol depends on to see if any are tainted.
  // Note that `symbols()` already descends through SymbolCasts, so casts need
  // no handling of their own here.
  for (SymbolRef SubSym : Sym->symbols()) {
    if (!isa<SymbolData>(SubSym))
      continue;

    if (const TaintTagType *Tag = State->get<TaintMap>(SubSym))
      if (*Tag == Kind) {
        Out.push_back(SubSym);
        if (FirstOnly)
          return true;
      }

    if (const auto *SD = dyn_cast<SymbolDerived>(SubSym)) {
      // If this is a SymbolDerived with a tainted parent, it's also tainted.
      if (findTainted(State, SD->getParentSymbol(), Kind, FirstOnly, Out))
        return true;

      // If this is a SymbolDerived with the same parent symbol as another
      // tainted SymbolDerived and a region that's a sub-region of that
      // tainted symbol, it's also tainted.
      if (const TaintedSubRegions *Regs =
              State->get<DerivedSymTaint>(SD->getParentSymbol())) {
        const TypedValueRegion *R = SD->getRegion();
        for (auto I : *Regs) {
          // FIXME: The logic to identify tainted regions could be more
          // complete. For example, this would not currently identify
          // overlapping fields in a union as tainted. To identify this we can
          // check for overlapping/nested byte offsets.
          if (Kind == I.second && R->isSubRegionOf(I.first)) {
            Out.push_back(SD->getParentSymbol());
            if (FirstOnly)
              return true;
          }
        }
      }
    }

    // If memory region is tainted, data is also tainted.
    if (const auto *SRV = dyn_cast<SymbolRegionValue>(SubSym))
      if (findTainted(State, SRV->getRegion(), Kind, FirstOnly, Out))
        return true;
  }
  return false;
}

/// The public entry points differ only in what they start the walk from, so
/// they all funnel through these two.
template <typename T>
bool hasAnyTaint(ProgramStateRef State, T Start, TaintTagType Kind) {
  if (isTaintFree(State))
    return false;
  SmallVector<SymbolRef, 1> Syms;
  findTainted(State, Start, Kind, /*FirstOnly=*/true, Syms);
  return !Syms.empty();
}

template <typename T>
std::vector<SymbolRef> collectTaint(ProgramStateRef State, T Start,
                                    TaintTagType Kind) {
  if (isTaintFree(State))
    return {};
  SmallVector<SymbolRef, 4> Syms;
  findTainted(State, Start, Kind, /*FirstOnly=*/false, Syms);
  return {Syms.begin(), Syms.end()};
}
} // namespace

void taint::printTaint(ProgramStateRef State, raw_ostream &Out, const char *NL,
                       const char *Sep) {
  TaintMapTy TM = State->get<TaintMap>();

  if (!TM.isEmpty())
    Out << "Tainted symbols:" << NL;

  for (const auto &I : TM)
    Out << I.first << " : " << I.second << NL;
}

void taint::dumpTaint(ProgramStateRef State) {
  printTaint(State, llvm::errs());
}

ProgramStateRef taint::addTaint(ProgramStateRef State, const Stmt *S,
                                const LocationContext *LCtx,
                                TaintTagType Kind) {
  return addTaint(State, State->getSVal(S, LCtx), Kind);
}

ProgramStateRef taint::addTaint(ProgramStateRef State, SVal V,
                                TaintTagType Kind) {
  SymbolRef Sym = V.getAsSymbol();
  if (Sym)
    return addTaint(State, Sym, Kind);

  // If the SVal represents a structure, try to mass-taint all values within the
  // structure. For now it only works efficiently on lazy compound values that
  // were conjured during a conservative evaluation of a function - either as
  // return values of functions that return structures or arrays by value, or as
  // values of structures or arrays passed into the function by reference,
  // directly or through pointer aliasing. Such lazy compound values are
  // characterized by having exactly one binding in their captured store within
  // their parent region, which is a conjured symbol default-bound to the base
  // region of the parent region.
  if (auto LCV = V.getAs<nonloc::LazyCompoundVal>()) {
    if (std::optional<SVal> binding =
            State->getStateManager().getStoreManager().getDefaultBinding(
                *LCV)) {
      if (SymbolRef Sym = binding->getAsSymbol())
        return addPartialTaint(State, Sym, LCV->getRegion(), Kind);
    }
  }

  const MemRegion *R = V.getAsRegion();
  return addTaint(State, R, Kind);
}

ProgramStateRef taint::addTaint(ProgramStateRef State, const MemRegion *R,
                                TaintTagType Kind) {
  if (const SymbolicRegion *SR = dyn_cast_or_null<SymbolicRegion>(R))
    return addTaint(State, SR->getSymbol(), Kind);
  return State;
}

ProgramStateRef taint::addTaint(ProgramStateRef State, SymbolRef Sym,
                                TaintTagType Kind) {
  // If this is a symbol cast, remove the cast before adding the taint. Taint
  // is cast agnostic.
  while (const SymbolCast *SC = dyn_cast<SymbolCast>(Sym))
    Sym = SC->getOperand();

  ProgramStateRef NewState = State->set<TaintMap>(Sym, Kind);
  assert(NewState);
  return NewState;
}

ProgramStateRef taint::removeTaint(ProgramStateRef State, SVal V) {
  SymbolRef Sym = V.getAsSymbol();
  if (Sym)
    return removeTaint(State, Sym);

  const MemRegion *R = V.getAsRegion();
  return removeTaint(State, R);
}

ProgramStateRef taint::removeTaint(ProgramStateRef State, const MemRegion *R) {
  if (const SymbolicRegion *SR = dyn_cast_or_null<SymbolicRegion>(R))
    return removeTaint(State, SR->getSymbol());
  return State;
}

ProgramStateRef taint::removeTaint(ProgramStateRef State, SymbolRef Sym) {
  // If this is a symbol cast, remove the cast before adding the taint. Taint
  // is cast agnostic.
  while (const SymbolCast *SC = dyn_cast<SymbolCast>(Sym))
    Sym = SC->getOperand();

  ProgramStateRef NewState = State->remove<TaintMap>(Sym);
  assert(NewState);
  return NewState;
}

ProgramStateRef taint::addPartialTaint(ProgramStateRef State,
                                       SymbolRef ParentSym,
                                       const SubRegion *SubRegion,
                                       TaintTagType Kind) {
  // Ignore partial taint if the entire parent symbol is already tainted.
  if (const TaintTagType *T = State->get<TaintMap>(ParentSym))
    if (*T == Kind)
      return State;

  // Partial taint applies if only a portion of the symbol is tainted.
  if (SubRegion == SubRegion->getBaseRegion())
    return addTaint(State, ParentSym, Kind);

  const TaintedSubRegions *SavedRegs = State->get<DerivedSymTaint>(ParentSym);
  TaintedSubRegions::Factory &F = State->get_context<TaintedSubRegions>();
  TaintedSubRegions Regs = SavedRegs ? *SavedRegs : F.getEmptyMap();

  Regs = F.add(Regs, SubRegion, Kind);
  ProgramStateRef NewState = State->set<DerivedSymTaint>(ParentSym, Regs);
  assert(NewState);
  return NewState;
}

bool taint::isTainted(ProgramStateRef State, const Stmt *S,
                      const LocationContext *LCtx, TaintTagType Kind) {
  return !isTaintFree(State) &&
         hasAnyTaint(State, State->getSVal(S, LCtx), Kind);
}

bool taint::isTainted(ProgramStateRef State, SVal V, TaintTagType Kind) {
  return hasAnyTaint(State, V, Kind);
}

bool taint::isTainted(ProgramStateRef State, const MemRegion *Reg,
                      TaintTagType K) {
  return hasAnyTaint(State, Reg, K);
}

bool taint::isTainted(ProgramStateRef State, SymbolRef Sym, TaintTagType Kind) {
  return hasAnyTaint(State, Sym, Kind);
}

std::vector<SymbolRef> taint::getTaintedSymbols(ProgramStateRef State,
                                                const Stmt *S,
                                                const LocationContext *LCtx,
                                                TaintTagType Kind) {
  if (isTaintFree(State))
    return {};
  return collectTaint(State, State->getSVal(S, LCtx), Kind);
}

std::vector<SymbolRef> taint::getTaintedSymbols(ProgramStateRef State, SVal V,
                                                TaintTagType Kind) {
  return collectTaint(State, V, Kind);
}

std::vector<SymbolRef> taint::getTaintedSymbols(ProgramStateRef State,
                                                SymbolRef Sym,
                                                TaintTagType Kind) {
  return collectTaint(State, Sym, Kind);
}

std::vector<SymbolRef> taint::getTaintedSymbols(ProgramStateRef State,
                                                const MemRegion *Reg,
                                                TaintTagType Kind) {
  return collectTaint(State, Reg, Kind);
}
