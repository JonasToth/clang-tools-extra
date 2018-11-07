//===--- FixItHintUtils.cpp - clang-tidy-----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "FixItHintUtils.h"
#include "LexerUtils.h"
#include "clang/AST/ASTContext.h"

namespace clang {
namespace tidy {
namespace utils {
namespace fixit {

FixItHint changeVarDeclToReference(const VarDecl &Var, ASTContext &Context) {
  SourceLocation AmpLocation = Var.getLocation();
  auto Token = utils::lexer::getPreviousToken(
      AmpLocation, Context.getSourceManager(), Context.getLangOpts());
  if (!Token.is(tok::unknown))
    AmpLocation = Lexer::getLocForEndOfToken(Token.getLocation(), 0,
                                             Context.getSourceManager(),
                                             Context.getLangOpts());
  return FixItHint::CreateInsertion(AmpLocation, "&");
}

FixItHint changeVarDeclToConst(const VarDecl &Var, ConstTarget CT,
                               ConstPolicy CP) {
  /// Whether to add whitespace before or after the emitted `const`.
  bool PrefixSpace = false;
  bool PostfixSpace = false;

  // Adding `const` to a pointer (treated as value) requires it to be on the
  // right side of the pointer.
  // Correct transformation is:
  // 'int * foo = nullptr; -> int * const = nullptr;'
  // The left hand transformation is incorrect, as it qualifies the pointee.
  // 'int * foo = nullptr; -> const int * = nullptr;'
  if (CT == ConstTarget::Value && Var.getType()->isPointerType()) {
    CP = ConstPolicy::Right;
  }

  SourceLocation InsertPoint;
  switch (CP) {
  case ConstPolicy::Left:
    InsertPoint = Var.getTypeSpecStartLoc();
    PostfixSpace = true;
    break;
  case ConstPolicy::Right:
    InsertPoint = Var.getLocation();
    PostfixSpace = true;
    break;
  }
  return FixItHint::CreateInsertion(
      InsertPoint,
      (Twine(PrefixSpace ? " " : "") + "const" + (PostfixSpace ? " " : ""))
          .str());
}

} // namespace fixit
} // namespace utils
} // namespace tidy
} // namespace clang
