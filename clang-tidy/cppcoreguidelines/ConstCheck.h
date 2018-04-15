//===--- ConstCheck.h - clang-tidy-------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_CPPCOREGUIDELINES_CONSTCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_CPPCOREGUIDELINES_CONSTCHECK_H

#include "../ClangTidy.h"
#include <iostream>
#include <unordered_map>

namespace clang {
namespace tidy {
namespace cppcoreguidelines {

/// This check warns for every variable, that could be declared as const, but
/// isn't.
///
/// For the user-facing documentation see:
/// http://clang.llvm.org/extra/clang-tidy/checks/cppcoreguidelines-const.html
class ConstCheck : public ClangTidyCheck {
public:
  ConstCheck(StringRef Name, ClangTidyContext *Context)
      : ClangTidyCheck(Name, Context),
        AnalyzeValues(Options.get("AnalyzeValues", 1)),
        AnalyzeHandles(Options.get("AnalyzeHandles", 1)),
        WarnPointersAsValues(Options.get("WarnPointersAsValues", 0)) {}

  void storeOptions(ClangTidyOptions::OptionMap &Opts) override;
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onEndOfTranslationUnit() override;

private:
  void variableRegistering(ast_matchers::MatchFinder *Finder);
  void handleRegistration(const ast_matchers::MatchFinder::MatchResult &Result);

  void modificationMatchers(ast_matchers::MatchFinder *Finder);
  void checkModification(const ast_matchers::MatchFinder::MatchResult &Result);
  void invalidateRefCaptured(const LambdaExpr *Lambda);

  template <typename ASTElement>
  bool notConst(const ast_matchers::MatchFinder::MatchResult &Result,
                StringRef matcher_bind) {
    if (Result.Nodes.getNodeAs<ASTElement>(matcher_bind)) {
      // std::cout << "Prohibting through " << std::string(matcher_bind)
      // << std::endl;
      const auto *Variable = Result.Nodes.getNodeAs<VarDecl>("value-decl");
      ValueCanBeConst[Variable] = false;
      HandleCanBeConst[Variable] = false;
      return true;
    }
    return false;
  }

  void diagnosePotentialConst();

  llvm::DenseMap<const VarDecl *, bool> ValueCanBeConst;
  llvm::DenseMap<const VarDecl *, bool> HandleCanBeConst;

  const bool AnalyzeValues;
  const bool AnalyzeHandles;
  const bool WarnPointersAsValues;
};

} // namespace cppcoreguidelines
} // namespace tidy
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_CPPCOREGUIDELINES_CONSTCHECK_H
