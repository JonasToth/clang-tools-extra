//===--- MixedIntArithmeticCheck.cpp - clang-tidy--------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MixedIntArithmeticCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace cppcoreguidelines {

void MixedIntArithmeticCheck::registerMatchers(MatchFinder *Finder) {
  const auto UnsignedIntegerOperand =
      expr(ignoringImpCasts(hasType(isUnsignedInteger())))
          .bind("unsigned-operand");

  // Match binary bitwise operations on signed integer arguments.
  Finder->addMatcher(
      binaryOperator(allOf(anyOf(hasOperatorName("+"), hasOperatorName("-"),
                                 hasOperatorName("*"), hasOperatorName("/")),
                           hasEitherOperand(UnsignedIntegerOperand),
                           hasRHS(hasType(isInteger())),
                           hasLHS(hasType(isInteger()))))
          .bind("mixed-arithmetic"),
      this);
}

void MixedIntArithmeticCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *UnsignedOperand =
      Result.Nodes.getNodeAs<Expr>("unsigned-operand");
  const auto *MixedArithmetic =
      Result.Nodes.getNodeAs<BinaryOperator>("mixed-arithmetic");

  diag(MixedArithmetic->getLocStart(), "use of signed and unsigned integers in "
                                       "arithmetic expression; use signed "
                                       "integers only")
      << UnsignedOperand->getSourceRange();
}

} // namespace cppcoreguidelines
} // namespace tidy
} // namespace clang
