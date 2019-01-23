//===--- ExceptionEscapeCheck.h - clang-tidy---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_BUGPRONE_EXCEPTION_ESCAPE_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_BUGPRONE_EXCEPTION_ESCAPE_H

#include "../ClangTidy.h"

#include "llvm/ADT/StringSet.h"

namespace clang {

/// Check if 'DerivedType' has any base class 'BaseType'.
bool isBaseOf(const Type *DerivedType, const Type *BaseType);

namespace tidy {
namespace bugprone {

class ExceptionTracer {
  using TypeVec = llvm::SmallVector<const Type *, 8>;

public:
  ExceptionTracer() = default;

  TypeVec throwsException(const FunctionDecl *Func);

private:
  TypeVec throwsException(const Stmt *St, const TypeVec &Caught,
                          llvm::SmallSet<const FunctionDecl *, 32> &CallStack);
  TypeVec throwsException(const FunctionDecl *Func,
                          llvm::SmallSet<const FunctionDecl *, 32> &CallStack);
  TypeVec throwsException(const Stmt *St, const TypeVec &Caught,
                          llvm::SmallSet<const FunctionDecl *, 32> &CallStack);
};

/// Finds functions which should not throw exceptions: Destructors, move
/// constructors, move assignment operators, the main() function,
/// swap() functions, functions marked with throw() or noexcept and functions
/// given as option to the checker.
///
/// For the user-facing documentation see:
/// http://clang.llvm.org/extra/clang-tidy/checks/bugprone-exception-escape.html
class ExceptionEscapeCheck : public ClangTidyCheck {
public:
  ExceptionEscapeCheck(StringRef Name, ClangTidyContext *Context);
  void storeOptions(ClangTidyOptions::OptionMap &Opts) override;
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;

private:
  std::string RawFunctionsThatShouldNotThrow;
  std::string RawIgnoredExceptions;

  llvm::StringSet<> FunctionsThatShouldNotThrow;
  llvm::StringSet<> IgnoredExceptions;
};

} // namespace bugprone
} // namespace tidy
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_BUGPRONE_EXCEPTION_ESCAPE_H
