//===--- FixItHintUtils.h - clang-tidy---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_UTILS_FIXITHINTUTILS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_UTILS_FIXITHINTUTILS_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"

namespace clang {
namespace tidy {
namespace utils {
namespace fixit {

/// \brief Creates fix to make ``VarDecl`` a reference by adding ``&``.
FixItHint changeVarDeclToReference(const VarDecl &Var, ASTContext &Context);

enum class ConstPolicy {
  Left,  // Add the `const` always to the left side, if that is possible.
  Right, // Add the `const` always to the right side.
};

enum class ConstTarget {
  Pointee, /// Transforming a pointer goes for the pointee and not the pointer
           /// itself. For references and normal values this option has no
           /// effect.
           /// `int * p = &i;` -> `const int * p = &i` or `int const * p = &i`.
  Value,   /// Transforming pointers will consider the pointer itself.
           /// `int * p = &i;` -> `int * const = &i`
};

/// \brief Creates fix to make ``VarDecl`` const qualified. Only valid if
/// `Var` is isolated in written code. `int foo = 42;`
Optional<FixItHint> changeVarDeclToConst(const VarDecl &Var,
                                         ConstTarget CT = ConstTarget::Pointee,
                                         ConstPolicy CP = ConstPolicy::Left,
                                         ASTContext *Context = nullptr);

} // namespace fixit
} // namespace utils
} // namespace tidy
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_UTILS_FIXITHINTUTILS_H
