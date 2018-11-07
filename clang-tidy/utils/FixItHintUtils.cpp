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

FixItHint changeVarDeclToConst(const VarDecl &Var, ConstPolicy CP) {
  if (Var.getType()->isPointerType())
    CP = ConstPolicy::Right;

  switch (CP) {
  case ConstPolicy::Left:
    return FixItHint::CreateInsertion(Var.getTypeSpecStartLoc(), "const ");
  case ConstPolicy::Right: {
    return FixItHint::CreateInsertion(Var.getLocation(), " const ");
  }
  }
}

} // namespace fixit
} // namespace utils
} // namespace tidy
} // namespace clang
