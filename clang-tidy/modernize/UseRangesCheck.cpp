//===--- UseRangesCheck.cpp - clang-tidy ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "UseRangesCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace modernize {

static const NamedDecl *isValidBeginOn(const Expr *E, ASTContext &C) {
  return selectFirst<NamedDecl>(
      "container",
      match(expr(anyOf(
                callExpr(allOf(callee(functionDecl(hasAnyName(
                                   "::std::begin", "::std::cbegin"))),
                               hasArgument(0, declRefExpr(to(namedDecl().bind(
                                                  "container")))))),
                cxxMemberCallExpr(allOf(
                    callee(cxxMethodDecl(hasAnyName("begin", "cbegin"))),
                    on(declRefExpr(to(namedDecl().bind("container")))))))),
            *(E->IgnoreParenImpCasts()), C));
}

static const NamedDecl *isValidEndOn(const Expr *E, ASTContext &C) {
  return selectFirst<NamedDecl>(
      "container",
      match(
          expr(anyOf(
              callExpr(allOf(
                  callee(functionDecl(hasAnyName("::std::end", "::std::cend"))),
                  hasArgument(0,
                              declRefExpr(to(namedDecl().bind("container")))))),
              cxxMemberCallExpr(
                  allOf(callee(cxxMethodDecl(hasAnyName("end", "cend"))),
                        on(declRefExpr(to(namedDecl().bind("container")))))))),
          *(E->IgnoreParenImpCasts()), C));
}

static const NamedDecl *isValidRangeOn(const Expr *Arg1, const Expr *Arg2,
                                       ASTContext &C) {
  const NamedDecl *ReferencedDecl1 = isValidBeginOn(Arg1, C);
  const NamedDecl *ReferencedDecl2 = isValidEndOn(Arg2, C);

  if (ReferencedDecl1 != nullptr && ReferencedDecl1 == ReferencedDecl2)
    return ReferencedDecl1;
  return nullptr;
}

/// Tag-types to choose the algorithm version and have static polymorphism
/// to treat each version differently.
///
/// At the moment all algorithms the Ranges-TS specifies are considered.
namespace algo {
/// Catch algorithms statically and group similar algorithm together.
/// The simplest algorithms are of the form `std::algo(range, functor)`.
/// C++17 standarized a parallel version for each of these algorithms
/// that takes 4 arguments, as a execution policy is added.
struct single_range {
  single_range(const CallExpr *Call, ASTContext &C)
      : IsRangeable(Call->getNumArgs() == 3),
        Container(isValidRangeOn(Call->getArg(0), Call->getArg(1), C)),
        Call(Call) {}

  bool isRangeable() const { return IsRangeable && Container != nullptr; }

  llvm::Optional<FixItHint> getTransformation() {
    SourceRange OldRange(Call->getArg(0)->getBeginLoc(),
                         Call->getArg(1)->getEndLoc());
    std::string ContainerName = Container->getName();
    return FixItHint::CreateReplacement(OldRange, ContainerName);
  }

private:
  const bool IsRangeable;
  const NamedDecl *const Container;
  const CallExpr *const Call;
};

/// Missmatch algorithm has many variations and changes between the standards.
struct missmatch {
  /// The call with 3 arguments can be transformed into a range-based
  /// two argument version.
  static bool isRangeable(const CallExpr *Call) {
    if (Call->getNumArgs() == 3)
      return true;
    return Call->getNumArgs() == 3;
  }
  static const Decl *getRanges(const CallExpr *Call, ASTContext &C) {
    return isValidRangeOn(Call->getArg(0), Call->getArg(1), C);
  }
};
} // namespace algo

/// Helper function to determine if the matcher matched on `Name` as a
/// `FunctionDecl`. This mechanism is used to avoid checking the function name
/// twice.
static bool isAlgorithm(const MatchFinder::MatchResult &R,
                        llvm::StringRef Name) {
  return R.Nodes.getNodeAs<FunctionDecl>(Name) != nullptr;
}

void UseRangesCheck::registerMatchers(MatchFinder *Finder) {
  // 1. Match all call-exprs that are known STL algorithms that have a range
  //    version.
  // 2. require either (std::begin(), or Object.begin()) form
  // 3. Extract the container that is used as range
  // 4. Rewrite the container ranges in the call.
  Finder->addMatcher(
      callExpr(anyOf(callee(functionDecl(
                                hasAnyName("::std::any_of", "::std::all_of",
                                           "::std::none_of", "::std::for_each",
                                           "::std::count", "::std::count_if",
                                           "::std::find", "::std::find_if",
                                           "::std::find_if_not"))
                                .bind("single-range")),
                     callee(functionDecl(hasName("::std::missmatch"))
                                .bind("missmatch"))))
          .bind("algo-call"),
      this);
}

void UseRangesCheck::check(const MatchFinder::MatchResult &Result) {
  ASTContext &C = *Result.Context;
  const auto *AlgoCall = Result.Nodes.getNodeAs<CallExpr>("algo-call");
  assert(AlgoCall != nullptr &&
         "Unexpected match for algorithm without bound callexpr");

  llvm::Optional<FixItHint> RangeTransform;

  // Bail-out if the function-call can not be transformed to a range-based
  // call. That is currently the case for the parrallel versions of the
  // algorithms.
  if (isAlgorithm(Result, "single-range")) {
    auto Algorithm = algo::single_range(AlgoCall, C);
    if (!Algorithm.isRangeable())
      return;
    RangeTransform = Algorithm.getTransformation();
  } else if (isAlgorithm(Result, "missmatch")) {
  }

  auto Diag = diag(AlgoCall->getBeginLoc(),
                   "algorithm could be rewritten with std::ranges");

  if (RangeTransform)
    Diag << *RangeTransform;
}

} // namespace modernize
} // namespace tidy
} // namespace clang
