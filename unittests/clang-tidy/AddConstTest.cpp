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
        << utils::fixit::changeVarDeclToConst(*D, CT, CP, Result.Context);
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

// ----------------------------------------------------------------------------
// Test Value-like types. Everything with indirection is done later.
// ----------------------------------------------------------------------------

TEST(Values, Builtin) {
  StringRef Snippet = "int target = 0;";

  EXPECT_EQ("const int target = 0;", runCheckOnCode<ValueLTransform>(Snippet));
  EXPECT_EQ("const int target = 0;",
            runCheckOnCode<PointeeLTransform>(Snippet));

  EXPECT_EQ("int const target = 0;", runCheckOnCode<ValueRTransform>(Snippet));
  EXPECT_EQ("int const target = 0;",
            runCheckOnCode<PointeeRTransform>(Snippet));
}

TEST(Arrays, Builtin) {
  StringRef Snippet = "int target[][1] = {{1}, {2}, {3}};";

  EXPECT_EQ("const int target[][1] = {{1}, {2}, {3}};",
            runCheckOnCode<PointeeLTransform>(Snippet));
  EXPECT_EQ("const int target[][1] = {{1}, {2}, {3}};",
            runCheckOnCode<ValueLTransform>(Snippet));

  EXPECT_EQ("int const target[][1] = {{1}, {2}, {3}};",
            runCheckOnCode<PointeeRTransform>(Snippet));
  EXPECT_EQ("int const target[][1] = {{1}, {2}, {3}};",
            runCheckOnCode<ValueRTransform>(Snippet));
}
TEST(Arrays, Pointers) {
  StringRef Snippet = "int x; int* target[] = {&x, &x, &x};";

  EXPECT_EQ("int x; const int* target[] = {&x, &x, &x};",
            runCheckOnCode<PointeeLTransform>(Snippet));
  EXPECT_EQ("int x; int const* target[] = {&x, &x, &x};",
            runCheckOnCode<PointeeRTransform>(Snippet));

  EXPECT_EQ("int x; int* const target[] = {&x, &x, &x};",
            runCheckOnCode<ValueLTransform>(Snippet));
  EXPECT_EQ("int x; int* const target[] = {&x, &x, &x};",
            runCheckOnCode<ValueRTransform>(Snippet));
}
TEST(Arrays, PointerPointers) {
  StringRef Snippet = "int* x = nullptr; int** target[] = {&x, &x, &x};";

  EXPECT_EQ("int* x = nullptr; int* const* target[] = {&x, &x, &x};",
            runCheckOnCode<PointeeLTransform>(Snippet));
  EXPECT_EQ("int* x = nullptr; int** const target[] = {&x, &x, &x};",
            runCheckOnCode<ValueLTransform>(Snippet));

  EXPECT_EQ("int* x = nullptr; int* const* target[] = {&x, &x, &x};",
            runCheckOnCode<PointeeRTransform>(Snippet));
  EXPECT_EQ("int* x = nullptr; int** const target[] = {&x, &x, &x};",
            runCheckOnCode<ValueRTransform>(Snippet));
}

// ----------------------------------------------------------------------------
// Test reference types. This does not include pointers and arrays.
// ----------------------------------------------------------------------------

TEST(Reference, LValueBuiltin) {
  StringRef Snippet = "int x = 42; int& target = x;";

  EXPECT_EQ("int x = 42; const int& target = x;",
            runCheckOnCode<ValueLTransform>(Snippet));
  EXPECT_EQ("int x = 42; const int& target = x;",
            runCheckOnCode<PointeeLTransform>(Snippet));

  EXPECT_EQ("int x = 42; int const& target = x;",
            runCheckOnCode<ValueRTransform>(Snippet));
  EXPECT_EQ("int x = 42; int const& target = x;",
            runCheckOnCode<PointeeRTransform>(Snippet));
}
TEST(Reference, RValueBuiltin) {
  StringRef Snippet = "int&& target = 42;";
  EXPECT_EQ("const int&& target = 42;",
            runCheckOnCode<ValueLTransform>(Snippet));
  EXPECT_EQ("const int&& target = 42;",
            runCheckOnCode<PointeeLTransform>(Snippet));

  EXPECT_EQ("int const&& target = 42;",
            runCheckOnCode<ValueRTransform>(Snippet));
  EXPECT_EQ("int const&& target = 42;",
            runCheckOnCode<PointeeRTransform>(Snippet));
}

// TODO: Reference to pointer.
// TODO: Reference to array.

// ----------------------------------------------------------------------------
// Test pointers types.
// ----------------------------------------------------------------------------

TEST(Pointers, SingleBuiltin) {
  StringRef Snippet = "int* target = nullptr;";

  EXPECT_EQ("int* const target = nullptr;",
            runCheckOnCode<ValueLTransform>(Snippet));
  EXPECT_EQ("int* const target = nullptr;",
            runCheckOnCode<ValueRTransform>(Snippet));

  EXPECT_EQ("const int* target = nullptr;",
            runCheckOnCode<PointeeLTransform>(Snippet));
  EXPECT_EQ("int const* target = nullptr;",
            runCheckOnCode<PointeeRTransform>(Snippet));
}
TEST(Pointers, MultiBuiltin) {
  StringRef Snippet = "int** target = nullptr;";

  EXPECT_EQ("int** const target = nullptr;",
            runCheckOnCode<ValueLTransform>(Snippet));
  EXPECT_EQ("int** const target = nullptr;",
            runCheckOnCode<ValueRTransform>(Snippet));

  EXPECT_EQ("int* const* target = nullptr;",
            runCheckOnCode<PointeeLTransform>(Snippet));
  EXPECT_EQ("int* const* target = nullptr;",
            runCheckOnCode<PointeeRTransform>(Snippet));
}
// TODO: Pointer to array `int array[4]; int (*ap)[4] = &array;`


} // namespace test
} // namespace tidy
} // namespace clang
