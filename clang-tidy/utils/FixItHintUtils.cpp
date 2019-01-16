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
  return !(isa<PointerType>(T) || isa<ReferenceType>(T) || isa<ArrayType>(T) ||
           isa<MemberPointerType>(T));
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
static bool isMemberOrFunctionPointer(QualType QT) {
  return (isPointerType(QT) && QT->isFunctionPointerType()) ||
         isa<MemberPointerType>(QT.getTypePtr());
}
static bool locDangerous(SourceLocation S) {
  return S.isInvalid() || S.isMacroID();
}

static Optional<SourceLocation>
skipLParensBackwards(SourceLocation Start, const ASTContext &Context) {
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
  if (locDangerous(Start))
    return None;
  return Start;
}

static Optional<FixItHint> fixIfNotDangerous(SourceLocation Loc,
                                             StringRef Text) {
  if (locDangerous(Loc))
    return None;
  return FixItHint::CreateInsertion(Loc, Text);
}

// Build a string that can be emitted as FixIt with either a space in before
// or after the qualifier, either ' const' or 'const '.
static std::string buildQualifier(DeclSpec::TQ Qualifier,
                                  bool WhitespaceBefore = false) {
  if (WhitespaceBefore)
    return (llvm::Twine(' ') +
            llvm::Twine(DeclSpec::getSpecifierName(Qualifier)))
        .str();
  return (llvm::Twine(DeclSpec::getSpecifierName(Qualifier)) + llvm::Twine(' '))
      .str();
}

static Optional<FixItHint> changeValue(const VarDecl &Var,
                                       DeclSpec::TQ Qualifier,
                                       QualifierTarget QualTarget,
                                       QualifierPolicy QualPolicy,
                                       const ASTContext &Context) {
  switch (QualPolicy) {
  case QualifierPolicy::Left:
    return fixIfNotDangerous(Var.getTypeSpecStartLoc(),
                             buildQualifier(Qualifier));
  case QualifierPolicy::Right:
    Optional<SourceLocation> IgnoredParens =
        skipLParensBackwards(Var.getLocation(), Context);

    if (IgnoredParens)
      return fixIfNotDangerous(*IgnoredParens, buildQualifier(Qualifier));
    return None;
  }
}

static Optional<FixItHint> changePointerItself(const VarDecl &Var,
                                               DeclSpec::TQ Qualifier,
                                               const ASTContext &Context) {
  if (locDangerous(Var.getLocation()))
    return None;

  Optional<SourceLocation> IgnoredParens =
      skipLParensBackwards(Var.getLocation(), Context);
  if (IgnoredParens)
    return fixIfNotDangerous(*IgnoredParens, buildQualifier(Qualifier));
  return None;
}

static Optional<FixItHint>
changePointer(const VarDecl &Var, DeclSpec::TQ Qualifier, const Type *Pointee,
              QualifierTarget QualTarget, QualifierPolicy QualPolicy,
              const ASTContext &Context) {
  // The pointer itself shall be marked as `const`. This is always to the right
  // of the '*' or in front of the identifier.
  if (QualTarget == QualifierTarget::Value)
    return changePointerItself(Var, Qualifier, Context);

  // Mark the pointee `const` that is a normal value (`int* p = nullptr;`).
  if (QualTarget == QualifierTarget::Pointee && isValueType(Pointee)) {
    // Adding the `const` on the left side is just the beginning of the type
    // specification. (`const int* p = nullptr;`)
    if (QualPolicy == QualifierPolicy::Left)
      return fixIfNotDangerous(Var.getTypeSpecStartLoc(),
                               buildQualifier(Qualifier));

    // Adding the `const` on the right side of the value type requires finding
    // the `*` token and placing the `const` left of it.
    // (`int const* p = nullptr;`)
    if (QualPolicy == QualifierPolicy::Right) {
      SourceLocation BeforeStar = lexer::findPreviousTokenKind(
          Var.getLocation(), Context.getSourceManager(), Context.getLangOpts(),
          tok::star);
      if (locDangerous(BeforeStar))
        return None;

      Optional<SourceLocation> IgnoredParens =
          skipLParensBackwards(BeforeStar, Context);

      if (IgnoredParens)
        return fixIfNotDangerous(*IgnoredParens,
                                 buildQualifier(Qualifier, true));
      return None;
    }
  }

  if (QualTarget == QualifierTarget::Pointee && isPointerType(Pointee)) {
    // Adding the `const` to the pointee if the pointee is a pointer
    // is the same as 'QualPolicy == Right && isValueType(Pointee)'.
    // The `const` must be left of the last `*` token.
    // (`int * const* p = nullptr;`)
    SourceLocation BeforeStar = lexer::findPreviousTokenKind(
        Var.getLocation(), Context.getSourceManager(), Context.getLangOpts(),
        tok::star);
    return fixIfNotDangerous(BeforeStar, buildQualifier(Qualifier, true));
  }

  llvm_unreachable("All paths should have been handled");
}

static Optional<FixItHint>
changeReferencee(const VarDecl &Var, DeclSpec::TQ Qualifier, QualType Pointee,
                 QualifierTarget QualTarget, QualifierPolicy QualPolicy,
                 const ASTContext &Context) {
  if (QualPolicy == QualifierPolicy::Left && isValueType(Pointee))
    return fixIfNotDangerous(Var.getTypeSpecStartLoc(),
                             buildQualifier(Qualifier));

  SourceLocation BeforeRef = lexer::findPreviousAnyTokenKind(
      Var.getLocation(), Context.getSourceManager(), Context.getLangOpts(),
      tok::amp, tok::ampamp);
  Optional<SourceLocation> IgnoredParens =
      skipLParensBackwards(BeforeRef, Context);
  if (IgnoredParens)
    return fixIfNotDangerous(*IgnoredParens, buildQualifier(Qualifier, true));

  return None;
}

Optional<FixItHint> addQualifierToVarDecl(const VarDecl &Var,
                                          DeclSpec::TQ Qualifier,
                                          QualifierTarget QualTarget,
                                          QualifierPolicy QualPolicy,
                                          const ASTContext *Context) {
  assert((QualPolicy == QualifierPolicy::Left ||
          QualPolicy == QualifierPolicy::Right) &&
         "Unexpected Insertion Policy");
  assert((QualTarget == QualifierTarget::Pointee ||
          QualTarget == QualifierTarget::Value) &&
         "Unexpected Target");

  QualType ParenStrippedType = Var.getType().IgnoreParens();
  if (isValueType(ParenStrippedType))
    return changeValue(Var, Qualifier, QualTarget, QualPolicy, *Context);

  if (isReferenceType(ParenStrippedType))
    return changeReferencee(Var, Qualifier, Var.getType()->getPointeeType(),
                            QualTarget, QualPolicy, *Context);

  if (isMemberOrFunctionPointer(ParenStrippedType))
    return changePointerItself(Var, Qualifier, *Context);

  if (isPointerType(ParenStrippedType))
    return changePointer(Var, Qualifier,
                         ParenStrippedType->getPointeeType().getTypePtr(),
                         QualTarget, QualPolicy, *Context);

  if (isArrayType(ParenStrippedType)) {
    const Type *AT = ParenStrippedType->getBaseElementTypeUnsafe();
    assert(AT && "Did not retrieve array element type for an array.");

    if (isValueType(AT))
      return changeValue(Var, Qualifier, QualTarget, QualPolicy, *Context);

    if (isPointerType(AT))
      return changePointer(Var, Qualifier, AT->getPointeeType().getTypePtr(),
                           QualTarget, QualPolicy, *Context);
  }

  llvm_unreachable(
      "All possible combinations should have been handled already");
}
} // namespace fixit
} // namespace utils
} // namespace tidy
} // namespace clang
