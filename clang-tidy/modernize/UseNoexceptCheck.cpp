//===--- UseNoexceptCheck.cpp - clang-tidy---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "UseNoexceptCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/Lex/Lexer.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace modernize {

UseNoexceptCheck::UseNoexceptCheck(StringRef Name, ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context),
      NoexceptMacro(Options.get("ReplacementString", "")),
      UseNoexceptFalse(Options.get("UseNoexceptFalse", true)),
      AddMissingNoexcept(Options.get("AddMissingNoexcept", false)) {}

void UseNoexceptCheck::storeOptions(ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "ReplacementString", NoexceptMacro);
  Options.store(Opts, "UseNoexceptFalse", UseNoexceptFalse);
  Options.store(Opts, "AddMissingNoexcept", AddMissingNoexcept);
}

void UseNoexceptCheck::registerMatchers(MatchFinder *Finder) {
  if (!getLangOpts().CPlusPlus11)
    return;

  Finder->addMatcher(
      functionDecl(
          cxxMethodDecl(
              hasTypeLoc(loc(functionProtoType(hasDynamicExceptionSpec()))),
              anyOf(hasOverloadedOperatorName("delete[]"),
                    hasOverloadedOperatorName("delete"), cxxDestructorDecl()))
              .bind("del-dtor"))
          .bind("funcDecl"),
      this);

  Finder->addMatcher(
      functionDecl(
          hasTypeLoc(loc(functionProtoType(hasDynamicExceptionSpec()))),
          unless(anyOf(hasOverloadedOperatorName("delete[]"),
                       hasOverloadedOperatorName("delete"),
                       cxxDestructorDecl())))
          .bind("funcDecl"),
      this);

  Finder->addMatcher(
      parmVarDecl(anyOf(hasType(pointerType(pointee(parenType(innerType(
                            functionProtoType(hasDynamicExceptionSpec())))))),
                        hasType(memberPointerType(pointee(parenType(innerType(
                            functionProtoType(hasDynamicExceptionSpec()))))))))
          .bind("parmVarDecl"),
      this);

  if (AddMissingNoexcept)
    Finder->addMatcher(
        functionDecl(allOf(hasBody(stmt()),
                           unless(anyOf(isNoThrow(), hasDynamicExceptionSpec(),
                                        isImplicit()))))
            .bind("potentialNoexcept"),
        this);
}

void UseNoexceptCheck::check(const MatchFinder::MatchResult &Result) {
  const FunctionProtoType *FnTy = nullptr;
  bool DtorOrOperatorDel = false;
  SourceRange Range;

  if (const auto *FuncDecl = Result.Nodes.getNodeAs<FunctionDecl>("funcDecl")) {
    DtorOrOperatorDel = Result.Nodes.getNodeAs<FunctionDecl>("del-dtor");
    FnTy = FuncDecl->getType()->getAs<FunctionProtoType>();
    if (const auto *TSI = FuncDecl->getTypeSourceInfo())
      Range =
          TSI->getTypeLoc().castAs<FunctionTypeLoc>().getExceptionSpecRange();
  } else if (const auto *ParmDecl =
                 Result.Nodes.getNodeAs<ParmVarDecl>("parmVarDecl")) {
    FnTy = ParmDecl->getType()
               ->getAs<Type>()
               ->getPointeeType()
               ->getAs<FunctionProtoType>();

    if (const auto *TSI = ParmDecl->getTypeSourceInfo())
      Range = TSI->getTypeLoc()
                  .getNextTypeLoc()
                  .IgnoreParens()
                  .castAs<FunctionProtoTypeLoc>()
                  .getExceptionSpecRange();
  } else if (const auto *PotentialNoexcept =
                 Result.Nodes.getNodeAs<FunctionDecl>("potentialNoexcept")) {
    // `virtual` methods should not add `noexcept`, even if they could as
    // on of its overriders could throw an exception. Having `noexcept`
    // in these cases is a design decision.
    if (const auto *MDecl = dyn_cast<CXXMethodDecl>(PotentialNoexcept))
      if (MDecl->isVirtual())
        return;

    if (!Analyzer.throwsException(PotentialNoexcept) &&
        PotentialNoexcept->getBeginLoc().isValid())
      diag(PotentialNoexcept->getBeginLoc(),
           "this function can not throw an exception, consider adding "
           "'noexcept'");
    return;
  }
  CharSourceRange CRange = Lexer::makeFileCharRange(
      CharSourceRange::getTokenRange(Range), *Result.SourceManager,
      Result.Context->getLangOpts());

  assert(FnTy && "FunctionProtoType is null.");
  bool IsNoThrow = FnTy->isNothrow();
  StringRef ReplacementStr =
      IsNoThrow
          ? NoexceptMacro.empty() ? "noexcept" : NoexceptMacro.c_str()
          : NoexceptMacro.empty() ? (DtorOrOperatorDel || UseNoexceptFalse)
                                        ? "noexcept(false)"
                                        : ""
                                  : "";

  FixItHint FixIt;
  if ((IsNoThrow || NoexceptMacro.empty()) && CRange.isValid())
    FixIt = FixItHint::CreateReplacement(CRange, ReplacementStr);

  diag(Range.getBegin(), "dynamic exception specification '%0' is deprecated; "
                         "consider %select{using '%2'|removing it}1 instead")
      << Lexer::getSourceText(CRange, *Result.SourceManager,
                              Result.Context->getLangOpts())
      << ReplacementStr.empty() << ReplacementStr << FixIt;
}

} // namespace modernize
} // namespace tidy
} // namespace clang
