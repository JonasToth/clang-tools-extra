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

#include <iostream>

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace cppcoreguidelines {

void MixedIntArithmeticCheck::registerMatchers(MatchFinder *Finder) {
  const auto UnsignedIntegerOperand =
      expr(ignoringImpCasts(hasType(isUnsignedInteger())))
          .bind("unsigned-binary-operand");
  const auto SignedIntegerOperand =
      expr(ignoringImpCasts(hasType(isSignedInteger())))
          .bind("signed-binary-operand");

  // Match binary bitwise operations on signed integer arguments.
  // FIXME: Add assignment operations to the matcher and tests.
  Finder->addMatcher(
      binaryOperator(allOf(anyOf(hasOperatorName("+"), hasOperatorName("-"),
                                 hasOperatorName("*"), hasOperatorName("/")),
                           hasEitherOperand(UnsignedIntegerOperand),
                           hasEitherOperand(SignedIntegerOperand),
                           hasRHS(hasType(isInteger())),
                           hasLHS(hasType(isInteger()))))
          .bind("mixed-binary-arithmetic"),
      this);
}

void MixedIntArithmeticCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *UnsignedOperand =
      Result.Nodes.getNodeAs<Expr>("unsigned-binary-operand");
  const auto *SignedOperand =
      Result.Nodes.getNodeAs<Expr>("signed-binary-operand");
  const auto *MixedArithmetic =
      Result.Nodes.getNodeAs<BinaryOperator>("mixed-binary-arithmetic");

  assert(UnsignedOperand && MixedArithmetic &&
         "Matcher did not match operand and operation");

  diag(MixedArithmetic->getLocStart(),
       "mixed signed and unsigned arithmetic; "
       "prefer signed integers and use unsigned types only for modulo "
       "arithmetic")
      << MixedArithmetic->getSourceRange();

  /*
  std::cout << "Debug: signed expr = '"
            << std::string(Lexer::getSourceText(
                   CharSourceRange::getTokenRange(
                       SignedOperand->getSourceRange()),
                   *Result.SourceManager, getLangOpts(), 0))
            << "'\n";
  */

  diag(SignedOperand->getLocStart(), "signed operand", DiagnosticIDs::Note)
      << SignedOperand->getSourceRange();
  diag(UnsignedOperand->getLocStart(), "unsigned operand", DiagnosticIDs::Note)
      << UnsignedOperand->getSourceRange();
}

} // namespace cppcoreguidelines
} // namespace tidy
} // namespace clang
