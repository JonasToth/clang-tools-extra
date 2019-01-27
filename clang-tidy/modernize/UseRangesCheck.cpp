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

/// Tag-types to choose the algorithm version and have static polymorphism
/// to treat each version differently.
///
/// At the moment all algorithms the Ranges-TS specifies are considered.
namespace algo {
/// These tags are related to the cppreference description of the algorithms.
struct v1 {};
struct v2 {};
struct v3 {};
struct v4 {};
struct v5 {};
struct v6 {};
struct v7 {};
struct v8 {};
struct v9 {};
struct v10 {};
struct v11 {};

/// Catch algorithms statically and group similar algorithm together
/// (e.g. 'any_of', 'all_of', 'none_of').
struct any_of_like {
  /// The call with 3 arguments can be transformed into a range-based
  /// two argument version.
  static bool isRangeable(const CallExpr *Call) {
    return Call->getNumArgs() == 3;
  }
};
} // namespace algo

static const Decl *isValidBeginOn(const Expr *E, ASTContext &C) {
  return selectFirst<Decl>(
      "container",
      match(expr(anyOf(callExpr(allOf(callee(functionDecl(hasAnyName(
                                          "::std::begin", "::std::cbegin"))),
                                      hasArgument(0, declRefExpr(to(decl().bind(
                                                         "container")))))),
                       cxxMemberCallExpr(allOf(
                           callee(cxxMethodDecl(hasAnyName("begin", "cbegin"))),
                           on(declRefExpr(to(decl().bind("container")))))))),
            *(E->IgnoreParenImpCasts()), C));
}
static const Decl *isValidEndOn(const Expr *E, ASTContext &C) {
  return selectFirst<Decl>(
      "container",
      match(
          expr(anyOf(
              callExpr(allOf(
                  callee(functionDecl(hasAnyName("::std::end", "::std::cend"))),
                  hasArgument(0, declRefExpr(to(decl().bind("container")))))),
              cxxMemberCallExpr(
                  allOf(callee(cxxMethodDecl(hasAnyName("end", "cend"))),
                        on(declRefExpr(to(decl().bind("container")))))))),
          *(E->IgnoreParenImpCasts()), C));
}

static const Decl *isValidRangeOn(const Expr *Arg1, const Expr *Arg2,
                                  ASTContext &C) {
  const Decl *ReferencedDecl1 = isValidBeginOn(Arg1, C);
  const Decl *ReferencedDecl2 = isValidEndOn(Arg2, C);

  if (ReferencedDecl1 != nullptr && ReferencedDecl1 == ReferencedDecl2)
    return ReferencedDecl1;
  return nullptr;
}

static bool isAlgorithm(const MatchFinder::MatchResult &R,
                        llvm::StringRef Name) {
  return R.Nodes.getNodeAs<FunctionDecl>(Name) != nullptr;
}

void UseRangesCheck::registerMatchers(MatchFinder *Finder) {
  // 1. Match all call-exprs that are known STL algorithms
  // 2. require either (std::begin(), or Object.begin()) form
  // 3. find a way on how to specify easily which argument of STL calls
  //    belongs to which range.
  Finder->addMatcher(
      callExpr(
          anyOf(callee(functionDecl(hasAnyName("::std::any_of", "::std::all_of",
                                               "::std::none_of"))
                           .bind("any-of-like")),
                callee(
                    functionDecl(hasName("::std::for_each")).bind("for-each"))))
          .bind("algo-call"),
      this);
}

void UseRangesCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *AlgoCall = Result.Nodes.getNodeAs<CallExpr>("algo-call");
  assert(AlgoCall != nullptr &&
         "Unexpected match for algorithm without bound callexpr");

  // Bail-out if the function-call can not be transformed to a range-based
  // call. That is currently the case for the parrallel versions of the
  // algorithms.
  if (isAlgorithm(Result, "any-of-like")) {
    llvm::dbgs() << "IsRangeable?: " << algo::any_of_like::isRangeable(AlgoCall)
                 << "\n";
    llvm::dbgs() << "IsValidBegin?: "
                 << isValidBeginOn(AlgoCall->getArg(0), *Result.Context)
                 << "\n";
    llvm::dbgs() << "IsValidEnd?: "
                 << isValidEndOn(AlgoCall->getArg(1), *Result.Context) << "\n";
    if (!algo::any_of_like::isRangeable(AlgoCall) ||
        isValidRangeOn(AlgoCall->getArg(0), AlgoCall->getArg(1),
                       *Result.Context) == nullptr)
      return;
  }

  diag(AlgoCall->getBeginLoc(),
       "algorithm could be rewritten with std::ranges");
}

} // namespace modernize
} // namespace tidy
} // namespace clang
