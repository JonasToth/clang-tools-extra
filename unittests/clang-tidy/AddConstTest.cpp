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

template <ConstTarget CT = ConstTarget::Pointee,
          ConstPolicy CP = ConstPolicy::Left>
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
        << utils::fixit::changeVarDeclToConst(*D, CT, CP);
  }
};
} // namespace

namespace test {
using PointeeLTransform =
    ConstTransform<ConstTarget::Pointee, ConstPolicy::Left>;
using PointeeRTransform =
    ConstTransform<ConstTarget::Pointee, ConstPolicy::Right>;

using ValueLTransform = ConstTransform<ConstTarget::Value, ConstPolicy::Left>;
using ValueRTransform = ConstTransform<ConstTarget::Value, ConstPolicy::Right>;

TEST(ConstBuiltins, BuiltinValueLeft) {
  EXPECT_EQ("const int target = 0;",
            runCheckOnCode<PointeeLTransform>("int target = 0;"));
}
TEST(ConstBuiltins, BuiltinValueRight) {
  EXPECT_EQ("int const target = 0;",
            runCheckOnCode<PointeeRTransform>("int target = 0;"));
}

TEST(ConstBuiltins, PointerLeft) {
  EXPECT_EQ("int* const target = nullptr;",
            runCheckOnCode<ValueLTransform>("int* target = nullptr;"));
}

TEST(ConstBuiltins, PointerRight) {
  EXPECT_EQ("int* const target = nullptr;",
            runCheckOnCode<ValueRTransform>("int* target = nullptr;"));
}

TEST(ConstBuiltins, LValueReferenceLeft) {
  EXPECT_EQ("int x = 42; const int& target = x;",
            runCheckOnCode<PointeeLTransform>("int x = 42; int& target = x;"));
}
TEST(ConstBuiltins, LValueReferenceRight) {
  EXPECT_EQ("int x = 42; int& const target = x;",
            runCheckOnCode<PointeeRTransform>("int x = 42; int& target = x;"));
}

TEST(ConstBuiltins, RValueReferenceLeft) {
  EXPECT_EQ("const int&& target = 42;",
            runCheckOnCode<PointeeLTransform>("int&& target = 42;"));
}
TEST(ConstBuiltins, RValueReferenceRight) {
  EXPECT_EQ("int&& const target = 42;",
            runCheckOnCode<PointeeRTransform>("int&& target = 42;"));
}
} // namespace test
} // namespace tidy
} // namespace clang
