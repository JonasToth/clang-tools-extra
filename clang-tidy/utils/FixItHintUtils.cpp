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

static bool isValueType(const Type *T) {
  return !(isa<PointerType>(T) || isa<ReferenceType>(T) || isa<ArrayType>(T));
}
static bool isValueType(QualType QT) { return isValueType(QT.getTypePtr()); }
static bool isArrayType(QualType QT) { return isa<ArrayType>(QT.getTypePtr()); }

static bool isReferenceType(QualType QT) {
  return isa<ReferenceType>(QT.getTypePtr());
}

static bool isPointerType(const Type *T) { return isa<PointerType>(T); }
static bool isPointerType(QualType QT) {
  return isPointerType(QT.getTypePtr());
}

static bool locDangerous(SourceLocation S) {
  return S.isInvalid() || S.isMacroID();
}
static Optional<SourceLocation> skipLParensBackwards(SourceLocation Start,
                                                     ASTContext &Context) {
  Token T;
  auto PreviousTokenLParen = [&]() {
    T = lexer::getPreviousToken(Start, Context.getSourceManager(),
                                Context.getLangOpts());
    return T.is(tok::l_paren);
  };
  while (PreviousTokenLParen()) {
    if (locDangerous(Start))
      return None;

    Start = lexer::findPreviousTokenStart(Start, Context.getSourceManager(),
                                          Context.getLangOpts());
  }
  return Start;
}

static Optional<FixItHint> changeValue(const VarDecl &Var, ConstTarget CT,
                                       ConstPolicy CP) {
  llvm::dbgs() << "Change Value for " << Var.getName() << "\n";
  switch (CP) {
  case ConstPolicy::Left:
    if (locDangerous(Var.getTypeSpecStartLoc()))
      return None;
    return FixItHint::CreateInsertion(Var.getTypeSpecStartLoc(), "const ");
  case ConstPolicy::Right:
    if (locDangerous(Var.getLocation()))
      return None;
    return FixItHint::CreateInsertion(Var.getLocation(), "const ");
  }
}

static Optional<FixItHint> changePointer(const VarDecl &Var,
                                         const Type *Pointee, ConstTarget CT,
                                         ConstPolicy CP, ASTContext *Context) {
  llvm::dbgs() << "Change Pointer for " << Var.getName() << "\n";
  // The pointer itself shall be marked as `const`. This is always right
  // of the '*' or in front of the identifier.
  if (CT == ConstTarget::Value) {
    llvm::dbgs() << "Pointer itself const - policy irrelevant\n";
    if (locDangerous(Var.getLocation()))
      return None;
    return FixItHint::CreateInsertion(Var.getLocation(), "const ");
  }

  // Mark the pointee `const` that is a normal value (`int* p = nullptr;`).
  if (CT == ConstTarget::Pointee && isValueType(Pointee)) {
    llvm::dbgs() << "Pointee - ValueType\n";
    // Adding the `const` on the left side is just the beginning of the type
    // specification. (`const int* p = nullptr;`)
    if (CP == ConstPolicy::Left) {
      llvm::dbgs() << "Policy: Left\n";
      if (locDangerous(Var.getTypeSpecStartLoc()))
        return None;
      return FixItHint::CreateInsertion(Var.getTypeSpecStartLoc(), "const ");
    }

    // Adding the `const` on the right side of the value type requires finding
    // the `*` token and placing the `const` left of it.
    // (`int const* p = nullptr;`)
    if (CP == ConstPolicy::Right) {
      llvm::dbgs() << "Policy: Right\n";
      assert(Context && "Require ASTContext!");

      SourceLocation BeforeStar = lexer::findPreviousTokenKind(
          Var.getLocation(), Context->getSourceManager(),
          Context->getLangOpts(), tok::star);
      if (locDangerous(BeforeStar))
        return None;

      Optional<SourceLocation> IgnoredParens =
          skipLParensBackwards(BeforeStar, *Context);
      if (IgnoredParens)
        return FixItHint::CreateInsertion(*IgnoredParens, " const");
      return None;
    }
  }

  if (CT == ConstTarget::Pointee && isPointerType(Pointee)) {
    llvm::dbgs() << "Pointee - PointerType - Policy doesn't matter\n";
    // Adding the `const` to the pointee if the pointee is a pointer
    // is the same as 'CP == Right && isValueType(Pointee)'.
    // The `const` must be left of the last `*` token.
    // (`int * const* p = nullptr;`)
    assert(Context && "Require ASTContext!");

    SourceLocation BeforeStar = lexer::findPreviousTokenKind(
        Var.getLocation(), Context->getSourceManager(), Context->getLangOpts(),
        tok::star);
    return FixItHint::CreateInsertion(BeforeStar, " const");
  }

  llvm_unreachable("All paths should have been handled");
} // namespace fixit

static Optional<FixItHint> changeReferencee(const VarDecl &Var,
                                            QualType Pointee, ConstTarget CT,
                                            ConstPolicy CP,
                                            ASTContext *Context) {
  llvm::dbgs() << "Change Referencee for " << Var.getName() << "\n";
  if (CP == ConstPolicy::Left && isValueType(Pointee)) {
    if (locDangerous(Var.getTypeSpecStartLoc()))
      return None;
    llvm::dbgs() << "Policy: Left\n";
    return FixItHint::CreateInsertion(Var.getTypeSpecStartLoc(), "const ");
  }

  llvm::dbgs() << "Policy: Right || Pointee not Value\n";

  assert(Context && "Require ASTContext!");
  SourceLocation BeforeRef = lexer::findPreviousAnyTokenKind(
      Var.getLocation(), Context->getSourceManager(), Context->getLangOpts(),
      tok::amp, tok::ampamp);
  if (locDangerous(BeforeRef))
    return None;

  Optional<SourceLocation> IgnoredParens =
      skipLParensBackwards(BeforeRef, *Context);
  if (IgnoredParens)
    return FixItHint::CreateInsertion(*IgnoredParens, " const");

  return None;
}

Optional<FixItHint> changeVarDeclToConst(const VarDecl &Var, ConstTarget CT,
                                         ConstPolicy CP, ASTContext *Context) {
  assert((CP == ConstPolicy::Left || CP == ConstPolicy::Right) &&
         "Unexpected Insertion Policy");
  assert((CT == ConstTarget::Pointee || CT == ConstTarget::Value) &&
         "Unexpected Target");

  if (isValueType(Var.getType()))
    return changeValue(Var, CT, CP);

  if (isReferenceType(Var.getType()))
    return changeReferencee(Var, Var.getType()->getPointeeType(), CT, CP,
                            Context);

  if (isPointerType(Var.getType()))
    return changePointer(Var, Var.getType()->getPointeeType().getTypePtr(), CT,
                         CP, Context);

  if (isArrayType(Var.getType())) {
    llvm::dbgs() << "Found Array - dispatch\n";
    const Type *AT = Var.getType()->getBaseElementTypeUnsafe();
    assert(AT && "Did not retrieve array element type for an array.");

    if (isValueType(AT))
      return changeValue(Var, CT, CP);

    if (isPointerType(AT))
      return changePointer(Var, AT->getPointeeType().getTypePtr(), CT, CP,
                           Context);
  }

  llvm_unreachable(
      "All possible combinations should have been handled already");
}

} // namespace fixit
} // namespace utils
} // namespace tidy
} // namespace clang
