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
 * Open Questions
 * ==============
 *
 *  - type conversions:
 *    - one can overload the type conversion operation and modify a value of a
 *      class -> implications?
 *  - what about the 'mutable' keyword -> not considered now, because it applies
 *    only to class members
 */

void ConstCheck::storeOptions(ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "AnalyzeValues", AnalyzeValues);
  Options.store(Opts, "AnalyzeHandles", AnalyzeHandles);
  Options.store(Opts, "WarnPointersAsValues", WarnPointersAsValues);
}

void ConstCheck::registerMatchers(MatchFinder *Finder) {
  if (!getLangOpts().CPlusPlus)
    return;

  if (AnalyzeValues || AnalyzeHandles) {
    // Ensure that all intersting variables are registered in our mapping.
    variableRegistering(Finder);
    modificationMatchers(Finder);
  }
}

void ConstCheck::check(const MatchFinder::MatchResult &Result) {
  if (!getLangOpts().CPlusPlus)
    return;

  handleRegistration(Result);
  checkModification(Result);
}

// The decision which variable might be made const can only be made at the
// end of each translation unit.
void ConstCheck::onEndOfTranslationUnit() { diagnosePotentialConst(); }

void ConstCheck::variableRegistering(MatchFinder *Finder) {
  const auto HandleType =
      anyOf(hasType(referenceType()), hasType(pointerType()));
  const auto ValueType = unless(hasType(referenceType()));
  const auto ConstType = hasType(isConstQualified());
  const auto TemplateType = anyOf(hasType(templateTypeParmType()),
                                  hasType(substTemplateTypeParmType()));
  const auto LocalVariable =
      anyOf(hasAncestor(functionDecl(hasBody(compoundStmt()))),
            hasAncestor(lambdaExpr()));

  if (AnalyzeValues) {
    // Match local variables, that could be const.
    // Example: `int i = 10`, `int i` (will be used if program is correct)
    Finder->addMatcher(varDecl(allOf(LocalVariable, hasInitializer(anything()),
                                     unless(ConstType), unless(TemplateType),
                                     unless(isImplicit()), ValueType))
                           .bind("new-local-value"),
                       this);
  }

  if (AnalyzeHandles) {
    // Match local handle types, that are not const.
    // Example: `int &ri`, `int * pi`.
    Finder->addMatcher(
        varDecl(allOf(LocalVariable, hasInitializer(anything()),
                      unless(hasType(references(isConstQualified()))),
                      unless(hasType(pointsTo(isConstQualified()))),
                      unless(TemplateType), unless(isImplicit()), HandleType))
            .bind("new-local-handle"),
        this);
  }
}

void ConstCheck::handleRegistration(const MatchFinder::MatchResult &Result) {
  // Local variables can be declared as consts.
  if (const auto *Variable =
          Result.Nodes.getNodeAs<VarDecl>("new-local-value")) {
    // std::cout << "Added new local value" << std::endl;
    assert(AnalyzeValues && "Matched local value without analyzing them");
    if (ValueCanBeConst.find(Variable) == ValueCanBeConst.end()) {
      ValueCanBeConst[Variable] = true;
      return;
    }
  }

  if (const auto *Variable =
          Result.Nodes.getNodeAs<VarDecl>("new-local-handle")) {
    // std::cout << "Added new local handle" << std::endl;
    assert(AnalyzeHandles && "Matched local handle without analyzing them");
    if (HandleCanBeConst.find(Variable) == HandleCanBeConst.end()) {
      HandleCanBeConst[Variable] = true;
      return;
    }
  }
}

void ConstCheck::modificationMatchers(MatchFinder *Finder) {
  // Matchers for the basic differntiation groups of types.
  const auto IsHandleType =
      anyOf(hasType(referenceType()), hasType(pointerType()));
  const auto IsValueType = unless(hasType(referenceType()));

  // Matchers for non-const handles.
  const auto IsRefenceToNonConstType =
      hasType(references(qualType(unless(isConstQualified()))));
  const auto IsPointerToNonConstType =
      hasType(pointerType(pointee(unless(isConstQualified()))));
  const auto IsNonConstHandleType =
      anyOf(IsRefenceToNonConstType, IsPointerToNonConstType);

  // Match value, array and pointer access.
  // Pointer do have value and reference semantic.
  const auto VarDeclRef = declRefExpr(
      hasDeclaration(varDecl(unless(isImplicit())).bind("value-decl")));
  const auto ArrayAccess =
      arraySubscriptExpr(hasBase(ignoringImpCasts(VarDeclRef)));
  const auto PointerDeref = unaryOperator(allOf(
      hasOperatorName("*"), hasUnaryOperand(ignoringImpCasts(VarDeclRef))));
  const auto IsVarDeclRefExpr = anyOf(ArrayAccess, VarDeclRef, PointerDeref);

  // Classical assignment of any form (=, +=, <<=, ...) modifies the LHS
  // and prohibts it from being const.
  Finder->addMatcher(
      binaryOperator(allOf(isAssignmentOperator(), hasLHS(IsVarDeclRefExpr)))
          .bind("value-assignment"),
      this);

  // Usage of the '++' or '--' operator modifies a variable.
  Finder->addMatcher(
      unaryOperator(allOf(anyOf(hasOperatorName("++"), hasOperatorName("--")),
                          hasUnaryOperand(IsVarDeclRefExpr)))
          .bind("value-unary-modification"),
      this);

  // Check the address operator.
  Finder->addMatcher(
      unaryOperator(allOf(hasOperatorName("&"),
                          // Checking for the ImplicitCastExpr is enough,
                          // because a pointer can get casted only in the 'add
                          // const' direction implicitly.
                          unless(hasParent(implicitCastExpr())),
                          hasUnaryOperand(IsVarDeclRefExpr)))
          .bind("value-address-to-non-const"),
      this);

  // Check creation of references to this value.
  Finder->addMatcher(
      varDecl(allOf(IsNonConstHandleType, hasInitializer(IsVarDeclRefExpr),
                    unless(isImplicit())))
          .bind("value-non-const-reference"),
      this);

  // Check function calls that bind by reference.
  Finder->addMatcher(
      callExpr(forEachArgumentWithParam(
          IsVarDeclRefExpr, parmVarDecl(IsNonConstHandleType)
                                .bind("value-non-const-ref-call-param"))),
      this);

  // Check return values that reference a value.
  Finder->addMatcher(
      functionDecl(
          allOf(hasDescendant(returnStmt(hasReturnValue(IsVarDeclRefExpr))),
                returns(qualType(
                    references(qualType(unless(isConstQualified())))))))
          .bind("returns-non-const-ref"),
      this);

  // Check for direct method calls, that modify its object by declaration.
  Finder->addMatcher(
      cxxMemberCallExpr(
          allOf(on(IsVarDeclRefExpr), unless(callee(cxxMethodDecl(isConst())))))
          .bind("non-const-member-call"),
      this);

  // Check for operator calls, that are non-const. E.g. operator=
  Finder->addMatcher(
      cxxOperatorCallExpr(allOf(hasArgument(0, IsVarDeclRefExpr),
                                unless(callee(cxxMethodDecl(isConst())))))
          .bind("non-const-operator-call"),
      this);

  // Check for range-for loops that declare non-const handles as loop variable.
  Finder->addMatcher(
      cxxForRangeStmt(allOf(hasLoopVariable(IsNonConstHandleType),
                            hasRangeInit(IsVarDeclRefExpr)))
          .bind("non-const-range-for"),
      this);

  // Lambda expression can capture variables by reference which invalidates
  // the captured variables. Lambdas capture only the variables they actually
  // use!
  Finder->addMatcher(lambdaExpr().bind("value-lambda"), this);
}

void ConstCheck::checkModification(const MatchFinder::MatchResult &Res) {
  bool Prohibited = false;

  // Assignment of any form prohibits the LHS to be const.
  Prohibited = notConst<BinaryOperator>(Res, "value-assignment");
  // Usage of the '++' or '--' operator modifies a value.
  Prohibited |= notConst<UnaryOperator>(Res, "value-unary-modification");
  // The address of the values has been taken and did not result in a
  // pointer to const.
  Prohibited |= notConst<UnaryOperator>(Res, "value-address-to-non-const");
  // A reference is initialized with a value.
  Prohibited |= notConst<VarDecl>(Res, "value-non-const-reference");
  // A call where a parameter is a non-const reference.
  Prohibited |= notConst<ParmVarDecl>(Res, "value-non-const-ref-call-param");
  // A function returning a non-const reference prohibts its return value
  // to be const.
  Prohibited |= notConst<FunctionDecl>(Res, "returns-non-const-ref");
  // Calling a member function, that is not declared as const prohibts
  // constness of the value.
  Prohibited |= notConst<CXXMemberCallExpr>(Res, "non-const-member-call");
  // Calling an overloaded operator that is not declared as const probits
  // constness similar to member calls.
  Prohibited |= notConst<CXXOperatorCallExpr>(Res, "non-const-operator-call");
  // Range-For can loop in a modifying way over the range. This is equivalent
  // to taking a reference/pointer to one of the elements of the range.
  Prohibited |= notConst<CXXForRangeStmt>(Res, "non-const-range-for");

  // If the matchable targets did result in prohibiton, we can stop.
  // Otherwise analysis that does not function with the same pattern must be
  // applied.
  if (Prohibited)
    return;

  // Analysis of the lambda is more difficult.
  // Offloaded into a separate function.
  if (const auto *Lambda = Res.Nodes.getNodeAs<LambdaExpr>("value-lambda")) {
    invalidateRefCaptured(Lambda);
    return;
  }
}

void ConstCheck::invalidateRefCaptured(const LambdaExpr *Lambda) {
  for (const auto capture : Lambda->captures()) {
    if (capture.capturesVariable() &&
        capture.getCaptureKind() == LambdaCaptureKind::LCK_ByRef) {
      ValueCanBeConst[capture.getCapturedVar()] = false;
      HandleCanBeConst[capture.getCapturedVar()] = false;
    }
  }
}

void ConstCheck::diagnosePotentialConst() {
  if (AnalyzeValues) {
    for (const auto it : ValueCanBeConst) {
      const VarDecl *Variable = it.first;
      bool VarCanBeConst = it.second;
      bool VarIsPointer = Variable->getType()->isPointerType();

      // Skip pointer warning for potential `const int * ->const<- value`.
      if (VarIsPointer && !WarnPointersAsValues)
        continue;

      if (VarCanBeConst) {
        diag(Variable->getLocStart(),
             "variable %0 of type %1 can be declared const")
            << Variable << Variable->getType();
      }
    }
  }

  if (AnalyzeHandles) {
    for (const auto it : HandleCanBeConst) {
      bool HandleCanBeConst = it.second;

      // Example: `int& ri` could be `const int& ri`.
      if (HandleCanBeConst) {
        const VarDecl *Variable = it.first;

        // Differentiate between pointers and references.
        QualType HandleType = Variable->getType();
        if (HandleType->isReferenceType()) {
          diag(Variable->getLocStart(),
               "reference variable %0 of type %1 can be declared const")
              << Variable << HandleType;
        } else if (HandleType->isPointerType()) {
          diag(Variable->getLocStart(),
               "pointer variable %0 of type %1 can be declared const")
              << Variable << HandleType;
        } else
          llvm_unreachable("Expected handle type");
      }
    }
  }
}

} // namespace cppcoreguidelines
} // namespace tidy
} // namespace clang
