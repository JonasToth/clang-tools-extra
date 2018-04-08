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
 *  - if the pointer is not reseated it can be marked as const
 *  - the type (->const<- int*) can be const if:
 *    - no non-const operation is executed on the dereferenced pointer
 *
 * references
 * ----------
 *  - no special semantics here, everything from the underlying type applies
 *
 * Implementation strategy
 * =======================
 *
 *  - Match for every type category every possible way a variable would loose
 *    its constness.
 *  - Keep a dictionary of all variables, as default constness is assumed.
 *  - Once there is a construct applied to a variable to prohibit constness,
 *    this variable is marked not const.
 *  - Only look inside functionDecl implementations.
 *
 * Caveats
 * =======
 *
 *  -  values of templated types are tricky, because with an other
 *    instantiation the const-properties might not exist anymore once
 *    -> remove const if one of this happens: operator call, method call
 *
 * Open Questions
 * ==============
 *
 *  - what is a type does not provide a const overload for some operation
 *    -> is every usage of this operation non-const? (operator[]) (Yes?!)
 *  - how is dereferencing of the handles dealt with?
 *  - when do implicit come to live
 *  - how to deal with range-for?
 */

void ConstCheck::registerMatchers(MatchFinder *Finder) {
  // If a variable is declared as non-const within a function or lambda it is
  // a candidate to declare it const.
  Finder->addMatcher(varDecl(allOf(anyOf(hasAncestor(functionDecl()),
                                         hasAncestor(lambdaExpr())),
                                   hasInitializer(anything()),
                                   unless(hasType(isConstQualified()))))
                         .bind("new-local"),
                     this);

  // Classical assignment of any form (=, +=, <<=, ...) modifies the LHS
  // and prohibts it from being const.
  Finder->addMatcher(binaryOperator(allOf(isAssignmentOperator(),
                                          hasLHS(declRefExpr(hasDeclaration(
                                              varDecl().bind("var-decl"))))))
                         .bind("binary-assignment"),
                     this);

  // Usage of the '++' or '--' operator modifies a variable.
  Finder->addMatcher(
      unaryOperator(allOf(anyOf(hasOperatorName("++"), hasOperatorName("--")),
                          hasUnaryOperand(declRefExpr(
                              hasDeclaration(varDecl().bind("var-decl"))))))
          .bind("unary-modification"),
      this);

  // Lambda expression can capture variables by reference which invalidates
  // the captured variables. Lambdas capture only the variables they actually
  // use!
  Finder->addMatcher(lambdaExpr().bind("lambda"), this);
}

void ConstCheck::check(const MatchFinder::MatchResult &Result) {
  // Added a new local variable to check for.
  if (const auto *Variable = Result.Nodes.getNodeAs<VarDecl>("new-local")) {
    if (CanBeConst.find(Variable) == CanBeConst.end()) {
      // diag(Variable->getLocStart(), "registering %0") << Variable;
      CanBeConst[Variable] = true;
      return;
    }
  }

  // All matchers will match for 'var-decl'.
  const auto *Variable = Result.Nodes.getNodeAs<VarDecl>("var-decl");

  // Assignment of any form prohibits the LHS to be const.
  if (Result.Nodes.getNodeAs<BinaryOperator>("binary-assignment")) {
    // diag(Variable->getLocStart(), "invalidating %0") << Variable;
    CanBeConst[Variable] = false;
  }

  // Usage of the '++' or '--' operator modifies a variable.
  if (Result.Nodes.getNodeAs<UnaryOperator>("unary-modification")) {
    // diag(Variable->getLocStart(), "invalidating %0") << Variable;
    CanBeConst[Variable] = false;
  }

  // Anylsis of the lambda is more difficult.
  // Offloaded into a separate function.
  if (const auto *Lambda = Result.Nodes.getNodeAs<LambdaExpr>("lambda")) {
    invalidate_ref_captured(Lambda);
  }
}

void ConstCheck::onEndOfTranslationUnit() { diagnosePotentialConst(); }

void ConstCheck::invalidate_ref_captured(const LambdaExpr *Lambda) {
  for (const auto capture : Lambda->captures()) {
    if (capture.capturesVariable() &&
        capture.getCaptureKind() == LambdaCaptureKind::LCK_ByRef) {
      // diag(capture.getCapturedVar()->getLocStart(), "invalidating %0")
          // << capture.getCapturedVar();
      CanBeConst[capture.getCapturedVar()] = false;
    }
  }
}

void ConstCheck::diagnosePotentialConst() {
  for (const auto it : CanBeConst) {
    bool VarCanBeConst = it.second;

    if (VarCanBeConst) {
      const VarDecl *Variable = it.first;

      diag(Variable->getLocStart(),
           "variable %0 of type %1 could be declared const")
          << Variable << Variable->getType();
    }
  }
}

} // namespace cppcoreguidelines
} // namespace tidy
} // namespace clang
