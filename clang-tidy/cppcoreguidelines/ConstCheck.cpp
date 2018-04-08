//===--- ConstCheck.cpp - clang-tidy---------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ConstCheck.h"
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
 * Handle = either a pointer or reference
 * Value  = everything else (Type variable_name;)
 *
 * Value Semantic
 * --------------
 *  - modifications of itself prohibit constness
 *
 * Handle Semantic
 * ---------------
 *  - modification of the pointee prohibit constness
 *
 * All types
 * ---------
 *  - it is neither global nor namespace level                        + CHECK
 *  - it never gets assigned to after initialization                  + CHECK
 *    -> every uninitialized variable can not be const                + CHECK
 *  - no non-const handle is created with it
 *    - no non-const pointer from it
 *    - no non-const reference from it
 *    - no non-const capture by reference in a lambda                 + CHECK
 *  - it is not used as argument to a non-const handle (pointer/ref)
 *  - it is not returned as non-const handle from a function
 *  - it address is not assigned to an out pointer parameter
 *
 * primitive Builtins
 * ----------------
 *  - it is not modified with an operator (++i,i++,--i,i--)           + CHECK
 *  - it is not modified with an operator-assignment                  + CHECK
 *
 * objects
 * -------
 *  - there is no call to a non-const method
 *  - there is no call to an non-const overloaded operator
 *  - there is no non-const iterator created from this type
 *    (std::begin and friends)
 *
 * arrays
 * ------
 *  - there is no non-const operator[] access
 *  - there is no non-const handle creation of one of the elements
 *  - there is no non-const iterator created from this type
 *    (std::begin and friends)
 *
 * pointers
 * --------
 *  - match both for value and handle semantic
 *
 * references
 * ----------
 *  - only handle semantic applies
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
 *  - once the translation unit is finished, determine what can be const
 *    - values that can be const -> emit warning for their type and name
 *    - handles that can be const -> emit warning for the pointee type and name
 *
 * Caveats
 * =======
 *
 *  - values of templated types are tricky, because with an other
 *    instantiation the const-properties might not exist anymore
 *    -> remove const if one of this happens: operator call, method call
 *
 * Open Questions
 * ==============
 *
 *  - what is a type does not provide a const overload for some operation
 *    -> is every usage of this operation non-const? (operator[]) (Yes?!)
 *  - how to deal with range-for? -> the iterator var is just another variable
 */

void ConstCheck::registerMatchers(MatchFinder *Finder) {
  // Ensure that all intersting variables are registered in our mapping.
  variableRegistering(Finder);

  valueTypeMatchers(Finder);
  // ----------------- matchers for pointer semantic ---------------------------
}

void ConstCheck::check(const MatchFinder::MatchResult &Result) {
  handleRegistration(Result);
  checkValueType(Result);
  // checkHandleType(Result);
}

// The decision which variable might be made const can only be made at the
// end of each translation unit.
void ConstCheck::onEndOfTranslationUnit() { diagnosePotentialConst(); }

void ConstCheck::variableRegistering(MatchFinder *Finder) {
  const auto IsHandleType =
      anyOf(hasType(referenceType()), hasType(pointerType()));
  const auto IsValueType = unless(hasType(referenceType()));

  // Match value and pointer access.
  // Pointer do have value and reference semantic.
  const auto IsValueDeclRefExpr = declRefExpr(
      allOf(hasDeclaration(varDecl().bind("var-decl")), IsValueType));
  // Match pointer and reference access.
  const auto IsHandleDeclRefExpr = declRefExpr(
      allOf(hasDeclaration(varDecl().bind("handle-decl")), IsHandleType));

  const auto ConstType = hasType(isConstQualified());
  const auto LocalVariable =
      anyOf(hasAncestor(functionDecl()), hasAncestor(lambdaExpr()));

  // Match global and namespace wide variables that will never be diagnosed.
  // Global handles are not relevant, because this check does not analyze that
  // for now.
  /* Why is this necessary again?
  Finder->addMatcher(varDecl(allOf(noneOf(hasAncestor(functionDecl()),
                                          hasAncestor(lambdaExpr())),
                                   hasInitializer(anything()), IsValueType))
                         .bind("new-global-value"),
                     this);
  */

  // Match local variables, that could be const.
  // Example: `int i = 10`, `int i` (will be used if program is correct)
  Finder->addMatcher(varDecl(allOf(LocalVariable, hasInitializer(anything()),
                                   unless(ConstType), IsValueType))
                         .bind("new-local-value"),
                     this);
  // Match local constants.
  // Example: `const int ri = 1`
  /* Not necessary?!
  Finder->addMatcher(varDecl(allOf(LocalVariable, ConstType, IsValueType))
                         .bind("new-local-const-value"),
                     this);
  */

  // Match local handle types, that are not const.
  // Example: `int &ri`, `int * pi`.
  Finder->addMatcher(
      varDecl(allOf(LocalVariable, IsHandleType, unless(ConstType)))
          .bind("new-local-handle"),
      this);
}

void ConstCheck::handleRegistration(const MatchFinder::MatchResult &Result) {
  // Local variables can be declared as consts.
  if (const auto *Variable =
          Result.Nodes.getNodeAs<VarDecl>("new-local-value")) {
    if (ValueCanBeConst.find(Variable) == ValueCanBeConst.end()) {
      ValueCanBeConst[Variable] = true;
      return;
    }
  }

  if (const auto *Variable =
          Result.Nodes.getNodeAs<VarDecl>("new-local-handle")) {
    if (HandleCanBeConst.find(Variable) == HandleCanBeConst.end()) {
      HandleCanBeConst[Variable] = true;
      return;
    }
  }
}

void ConstCheck::valueTypeMatchers(MatchFinder *Finder) {
  const auto IsHandleType =
      anyOf(hasType(referenceType()), hasType(pointerType()));
  const auto IsValueType = unless(hasType(referenceType()));

  // Match value and pointer access.
  // Pointer do have value and reference semantic.
  const auto IsValueDeclRefExpr = declRefExpr(
      allOf(hasDeclaration(varDecl().bind("var-decl")), IsValueType));

  // Classical assignment of any form (=, +=, <<=, ...) modifies the LHS
  // and prohibts it from being const.
  Finder->addMatcher(
      binaryOperator(allOf(isAssignmentOperator(), hasLHS(IsValueDeclRefExpr)))
          .bind("value-assignment"),
      this);

  // Usage of the '++' or '--' operator modifies a variable.
  Finder->addMatcher(
      unaryOperator(allOf(anyOf(hasOperatorName("++"), hasOperatorName("--")),
                          hasUnaryOperand(IsValueDeclRefExpr)))
          .bind("value-unary-modification"),
      this);

  // Lambda expression can capture variables by reference which invalidates
  // the captured variables. Lambdas capture only the variables they actually
  // use!
  Finder->addMatcher(lambdaExpr().bind("value-lambda"), this);
}

void ConstCheck::checkValueType(const MatchFinder::MatchResult &Result) {

  // Assignment of any form prohibits the LHS to be const.
  if (Result.Nodes.getNodeAs<BinaryOperator>("value-assignment")) {
    const auto *Variable = Result.Nodes.getNodeAs<VarDecl>("var-decl");
    ValueCanBeConst[Variable] = false;
  }

  // Usage of the '++' or '--' operator modifies a variable.
  if (Result.Nodes.getNodeAs<UnaryOperator>("value-unary-modification")) {
    const auto *Variable = Result.Nodes.getNodeAs<VarDecl>("var-decl");
    ValueCanBeConst[Variable] = false;
  }

  // Anylsis of the lambda is more difficult.
  // Offloaded into a separate function.
  if (const auto *Lambda = Result.Nodes.getNodeAs<LambdaExpr>("value-lambda")) {
    invalidateRefCaptured(Lambda);
  }
}

void ConstCheck::invalidateRefCaptured(const LambdaExpr *Lambda) {
  for (const auto capture : Lambda->captures()) {
    if (capture.capturesVariable() &&
        capture.getCaptureKind() == LambdaCaptureKind::LCK_ByRef) {
      ValueCanBeConst[capture.getCapturedVar()] = false;
    }
  }
}

void ConstCheck::diagnosePotentialConst() {
  // Diagnose all potential values.
  for (const auto it : ValueCanBeConst) {
    bool VarCanBeConst = it.second;

    if (VarCanBeConst) {
      const VarDecl *Variable = it.first;

      diag(Variable->getLocStart(),
           "variable %0 of type %1 can be declared const")
          << Variable << Variable->getType();
    }
  }

  for (const auto it : HandleCanBeConst) {
    bool HandleCanBeConst = it.second;

    // Example: `int& ri` could be `const int& ri`.
    if (HandleCanBeConst) {
      const VarDecl *Variable = it.first;

      // Differentiate between pointers and references.
      QualType HandleType = Variable->getType();
      if (HandleType->isReferenceType()) {

        diag(Variable->getLocStart(),
             "reference type %0 of variable %1 can be declared const")
            << HandleType << Variable;
      } else if (HandleType->isPointerType()) {

        diag(Variable->getLocStart(),
             "pointee type %0 of variable %1 can be declared const")
            << HandleType << Variable;
      } else
        llvm_unreachable("Expected handle type");
    }
  }
}

} // namespace cppcoreguidelines
} // namespace tidy
} // namespace clang
