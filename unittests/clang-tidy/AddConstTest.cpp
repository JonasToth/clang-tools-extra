#include "../clang-tidy/utils/FixItHintUtils.h"
#include "ClangTidyTest.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Tooling/Tooling.h"
#include "gtest/gtest.h"

namespace {
std::unique_ptr<ASTUnit>
buildASTFromCodeWithArgs(const Twine &Code,
                         const std::vector<std::string> &Args) {
  auto AST = tooling::buildASTFromCodeWithArgs(Code, Args);
  EXPECT_FALSE(AST->getDiagnostics().hasErrorOccurred());
  return AST;
}

std::unique_ptr<ASTUnit> buildASTFromCode(const Twine &Code) {
  return buildASTFromCodeWithArgs(Code, {});
}

} // namespace
