#include "../clang-tidy/utils/FixItHintUtils.h"
#include "ClangTidyTest.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Tooling/Tooling.h"
#include "gtest/gtest.h"

namespace clang {
namespace tidy {

namespace {
using namespace clang::ast_matchers;
using namespace utils::fixit;

template <ConstPolicy CP = ConstPolicy::Left>
class ConstTransform : public ClangTidyCheck {
public:
  ConstTransform(StringRef CheckName, ClangTidyContext *Context)
      : ClangTidyCheck(CheckName, Context) {}

  void registerMatchers(MatchFinder *Finder) override {
    Finder->addMatcher(varDecl(hasName("target")).bind("var"), this);
  }

  void check(const MatchFinder::MatchResult &Result) override {
    const auto *D = Result.Nodes.getNodeAs<VarDecl>("var");
    diag(D->getBeginLoc(), "doing const transformation")
        << utils::fixit::changeVarDeclToConst(*D, CP);
  }
};
} // namespace

namespace test {
using LTransform = ConstTransform<ConstPolicy::Left>;
using RTransform = ConstTransform<ConstPolicy::Right>;
TEST(ConstBuiltins, BuiltinValueLeft) {
  EXPECT_EQ("const int target = 0;",
            runCheckOnCode<LTransform>("int target = 0;"));
}
TEST(ConstBuiltins, BuiltinValueRight) {
  EXPECT_EQ("int  const target = 0;",
            runCheckOnCode<RTransform>("int target = 0;"));
}

TEST(ConstBuiltins, PointeeLeft) {
  EXPECT_EQ("int*  const target = nullptr;",
            runCheckOnCode<LTransform>("int* target = nullptr;"));
}

TEST(ConstBuiltins, PointeeRight) {
  EXPECT_EQ("int*  const target = nullptr;",
            runCheckOnCode<RTransform>("int* target = nullptr;"));
}

TEST(ConstBuiltins, ReferenceeLeft) {
  EXPECT_EQ("int x = 42; const int& target = x;",
            runCheckOnCode<LTransform>("int x = 42; int& target = x;"));
}
TEST(ConstBuiltins, ReferenceeRight) {
  EXPECT_EQ("int x = 42; int&  const target = x;",
            runCheckOnCode<RTransform>("int x = 42; int& target = x;"));
}

} // namespace test
} // namespace tidy
} // namespace clang
