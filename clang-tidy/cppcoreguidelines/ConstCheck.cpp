//===--- ConstCheck.cpp - clang-tidy---------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ConstCheck.h"
// #include "../utils/ASTUtils.h"
// #include "../utils/DeclRefExprUtils.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

#include <iostream>

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace cppcoreguidelines {

/*
 * General Thoughts
 * ================
 *
 * For now: Only local variables are considered. Globals/namespace variables,
 * paramters and class members are not analyzed.
 * Parameters have a check already: readability-non-const-parameter
 *
 *
 * Handle = either a pointer or reference
 * Value  = everything else (Type variable_name;)
 *
 * Value Semantic
 * ==============
 *  - it is neither global nor namespace level                        + CHECK
 *  - it never gets assigned to after initialization                  + CHECK
 *    -> every uninitialized variable can not be const                + CHECK
 *  - no non-const handle is created with it                          + CHECK
 *    - no non-const pointer from it                                  + CHECK
 *    - no non-const pointer argument                                 + CHECK
 *    - no non-const reference from it                                + CHECK
 *    - no non-const reference argument                               + CHECK
 *    - no non-const capture by reference in a lambda                 + CHECK
 *  - it is not returned as non-const handle from a function          + CHECK
 *  - it address is not assigned to an out pointer parameter          + CHECK
 *
 * primitive Builtins
 * ----------------
 *  - it is not modified with an operator (++i,i++,--i,i--)           + CHECK
 *  - it is not modified with an operator-assignment                  + CHECK
 *
 * objects
 * -------
 *  - there is no non-const access to a member
 *  - there is no call to a non-const method                          + CHECK
 *  - there is no call to an non-const overloaded operator            + CHECK
 *  - there is no non-const iterator created from this type           + CHECK
 *    (std::begin and friends)
 *
 * arrays
 * ------
 *  - there is no non-const operator[] access                         + CHECK
 *  - there is no non-const handle creation of one of the elements    + CHECK
 *  - there is no non-const iterator created from this type           + CHECK
 *    (std::begin and friends)
 *
 * templated variables
 * -------------------
 *  - one can not reason about templated variables, because every sensible
 *    operation is overloadable and different instantiations will result
 *    in types with different const-properties.
 *  - Example: operator+(T& lhs, T& rhs) -> modification might occur for this
 * type
 *    -> this fordbids `val = val1 + val2` val1 and val2 to be const
 *  - only concepts give possibility to infer constness of templated variables
 *
 * Handle Semantic
 * ===============
 *  - modification of the pointee prohibit constness
 *  - Handles follow the typ of the pointee
 *
 *  - no assignment to the target of the handle
 *
 * pointers
 * --------
 *  - match both for value and handle semantic
 *
 * references
 * ----------
 *  - only handle semantic applies
 *  - references to templated types suffer from the same problems as templated
 *    values
 *
 * forwarding reference
 * --------------------
 *  - same as references?
 *
 * Implementation strategy
 * =======================
 *
 *  - Register every declared local variable/constant with value semantic.
 *    (pointers, values)
 *    Store if they can be made const.
 *    (const int i = 10 : no,
 *     int *const = &i  : no,
 *     int i = 10       : yes, -> const int i = 10
 *     int *p_i = &i    : yes, -> int *const p_i = &i)
 *  - Register every declared local variable/constant with handle semantic.
 *    (pointers, references)
 *    Store if they can be made const, meaning if they can be a const target
 *    (const int *cp_i = &i : no,
 *     const int &cr_i = i  : no,
 *     int *p_i = &i        : yes, -> const int *p_i = &i
 *     int &r_i = i         : yes, -> const int &r_i = i)
 *  - Keep 2 dictionaries for values and handles
 *
 *  - Match operations/events that forbid values to be const -> mark then 'no'
 *  - Match operations/events that forbid handles to be const -> mark then 'no'
 *
 *  - once the translation unit is finished, determine what can be const, by
 *    just iterating over all keys and check if they map to 'true'.
 *    - values that can be const -> emit warning for their type and name
 *    - handles that can be const -> emit warning for the pointee type and name
 *    - ignore the rest
 *
 */

namespace {
AST_MATCHER(VarDecl, isLocal) { return Node.isLocalVarDecl(); }
} // namespace

void ConstCheck::storeOptions(ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "AnalyzeValues", AnalyzeValues);
  Options.store(Opts, "AnalyzeReferences", AnalyzeReferences);
  Options.store(Opts, "WarnPointersAsValues", WarnPointersAsValues);
}

void ConstCheck::registerMatchers(MatchFinder *Finder) {
  if (!getLangOpts().CPlusPlus)
    return;
  const auto ConstType = hasType(isConstQualified());
  const auto ConstReference = hasType(references(isConstQualified()));
  const auto TemplateType = anyOf(hasType(templateTypeParmType()),
                                  hasType(substTemplateTypeParmType()));

  // Match local variables, that could be const.
  // Example: `int i = 10`, `int i` (will be used if program is correct)
  const auto LocalValDecl = varDecl(allOf(
      isLocal(), hasInitializer(anything()), unless(ConstType),
      unless(ConstReference), unless(TemplateType), unless(isImplicit())));

  // Match the function scope for which the analysis of all local variables
  // shall be run.
  const auto FunctionScope =
      functionDecl(allOf(hasBody(compoundStmt().bind("scope")),
                         findAll(LocalValDecl.bind("new-local-value"))));
  Finder->addMatcher(FunctionScope, this);
}

void ConstCheck::check(const MatchFinder::MatchResult &Result) {
  if (!getLangOpts().CPlusPlus)
    return;

  const auto *Scope = Result.Nodes.getNodeAs<CompoundStmt>("scope");
  registerScope(Scope, Result.Context);

  const auto *Variable = Result.Nodes.getNodeAs<VarDecl>("new-local-value");
  assert(Variable && "Did not match local variable definition");

  if (Variable->getType()->isPointerType() && !WarnPointersAsValues)
    return;

  if (Variable->getType()->isReferenceType() && !AnalyzeReferences)
    return;

  if (usedNonConst(Variable, Scope, Result) || !AnalyzeValues)
    return;

  diag(Variable->getLocStart(), "variable %0 of type %1 can be declared const")
      << Variable << Variable->getType();
#if 0
  if (const auto *Variable =
          Result.Nodes.getNodeAs<VarDecl>("new-local-handle")) {
    assert(AnalyzeHandles && "Matched local handle without analyzing them");

    if (usedNonConst(Variable, Scope, Result))
      return;

    // if (!utils::decl_ref_expr::isOnlyUsedAsConst(*Variable, *Scope,
    // *Result.Context))
    // return;

    // Differentiate between pointers and references.
    QualType HandleType = Variable->getType();
    if (HandleType->isReferenceType())
      diag(Variable->getLocStart(),
           "reference variable %0 of type %1 can be declared const")
          << Variable << HandleType;
    else if (HandleType->isPointerType())
      diag(Variable->getLocStart(),
           "pointer variable %0 of type %1 can be declared const")
          << Variable << HandleType;
    else
      llvm_unreachable("Expected handle type");
  }
#endif
}

void ConstCheck::registerScope(const CompoundStmt *Scope, ASTContext *Context) {
  if (Scopes.find(Scope) == Scopes.end()) {
    Scopes.insert(std::make_pair(
        Scope, llvm::make_unique<utils::ExprMutationAnalyzer>(Scope, Context)));
  }
}

bool ConstCheck::usedNonConst(const VarDecl *Variable,
                              const CompoundStmt *Scope,
                              const MatchFinder::MatchResult &Result) {

  // find usage of that variable
  // if a declRefExpr is found -> analyze
  // else -> constant
  const auto Usage = match(
      findAll(declRefExpr(hasDeclaration(equalsNode(Variable))).bind("use")),
      *Scope, *Result.Context);
  const auto *UseExpr = selectFirst<Expr>("use", Usage);

  // The declared variables was used in non-const conserving way and can not
  // be declared as const.
  if (UseExpr && Scopes[Scope]->isMutated(UseExpr)) {
    // diag(UseExpr->getLocStart(), "Investigating starting with this use",
    // DiagnosticIDs::Note);
    return true;
  }
  return false;
}

} // namespace cppcoreguidelines
} // namespace tidy
} // namespace clang
