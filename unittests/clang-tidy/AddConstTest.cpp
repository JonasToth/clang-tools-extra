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
    using utils::fixit::changeVarDeclToConst;
    Optional<FixItHint> Fix = changeVarDeclToConst(*D, CT, CP, Result.Context);
    auto Diag = diag(D->getBeginLoc(), "doing const transformation");
    if (Fix)
      Diag << *Fix;
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

// TODO: Template-code
// TODO: Macros

TEST(Values, Builtin) {
  StringRef Snippet = "int target = 0;";

  EXPECT_EQ("const int target = 0;", runCheckOnCode<ValueLTransform>(Snippet));
  EXPECT_EQ("const int target = 0;",
            runCheckOnCode<PointeeLTransform>(Snippet));

  EXPECT_EQ("int const target = 0;", runCheckOnCode<ValueRTransform>(Snippet));
  EXPECT_EQ("int const target = 0;",
            runCheckOnCode<PointeeRTransform>(Snippet));
}
TEST(Values, TypedefBuiltin) {
  StringRef T = "typedef int MyInt;";
  StringRef S = "MyInt target = 0;";
  auto Cat = [&T](StringRef S) { return (T + S).str(); };

  EXPECT_EQ(Cat("const MyInt target = 0;"),
            runCheckOnCode<ValueLTransform>(Cat(S)));
  EXPECT_EQ(Cat("const MyInt target = 0;"),
            runCheckOnCode<PointeeLTransform>(Cat(S)));

  EXPECT_EQ(Cat("MyInt const target = 0;"),
            runCheckOnCode<ValueRTransform>(Cat(S)));
  EXPECT_EQ(Cat("MyInt const target = 0;"),
            runCheckOnCode<PointeeRTransform>(Cat(S)));
}
TEST(Values, TypedefBuiltinPointer) {
  StringRef T = "typedef int* MyInt;";
  StringRef S = "MyInt target = nullptr;";
  auto Cat = [&T](StringRef S) { return (T + S).str(); };

  EXPECT_EQ(Cat("const MyInt target = nullptr;"),
            runCheckOnCode<ValueLTransform>(Cat(S)));
  EXPECT_EQ(Cat("const MyInt target = nullptr;"),
            runCheckOnCode<PointeeLTransform>(Cat(S)));

  EXPECT_EQ(Cat("MyInt const target = nullptr;"),
            runCheckOnCode<ValueRTransform>(Cat(S)));
  EXPECT_EQ(Cat("MyInt const target = nullptr;"),
            runCheckOnCode<PointeeRTransform>(Cat(S)));
}
TEST(Values, AutoValue) {
  StringRef T = "int f() { return 42; }\n";
  StringRef S = "auto target = f();";
  auto Cat = [&T](StringRef S) { return (T + S).str(); };

  EXPECT_EQ(Cat("const auto target = f();"),
            runCheckOnCode<ValueLTransform>(Cat(S)));
  EXPECT_EQ(Cat("const auto target = f();"),
            runCheckOnCode<PointeeLTransform>(Cat(S)));

  EXPECT_EQ(Cat("auto const target = f();"),
            runCheckOnCode<ValueRTransform>(Cat(S)));
  EXPECT_EQ(Cat("auto const target = f();"),
            runCheckOnCode<PointeeRTransform>(Cat(S)));
}
TEST(Values, AutoPointer) {
  StringRef T = "int* f() { return nullptr; }\n";
  StringRef S = "auto target = f();";
  auto Cat = [&T](StringRef S) { return (T + S).str(); };

  EXPECT_EQ(Cat("const auto target = f();"),
            runCheckOnCode<ValueLTransform>(Cat(S)));
  EXPECT_EQ(Cat("const auto target = f();"),
            runCheckOnCode<PointeeLTransform>(Cat(S)));

  EXPECT_EQ(Cat("auto const target = f();"),
            runCheckOnCode<ValueRTransform>(Cat(S)));
  EXPECT_EQ(Cat("auto const target = f();"),
            runCheckOnCode<PointeeRTransform>(Cat(S)));
}
TEST(Values, AutoReference) {
  StringRef T = "static int global = 42; int& f() { return global; }\n";
  StringRef S = "auto target = f();";
  auto Cat = [&T](StringRef S) { return (T + S).str(); };

  EXPECT_EQ(Cat("const auto target = f();"),
            runCheckOnCode<ValueLTransform>(Cat(S)));
  EXPECT_EQ(Cat("const auto target = f();"),
            runCheckOnCode<PointeeLTransform>(Cat(S)));

  EXPECT_EQ(Cat("auto const target = f();"),
            runCheckOnCode<ValueRTransform>(Cat(S)));
  EXPECT_EQ(Cat("auto const target = f();"),
            runCheckOnCode<PointeeRTransform>(Cat(S)));
}

// ----------------------------------------------------------------------------
// Test builtin-arrays
// ----------------------------------------------------------------------------

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
TEST(Reference, LValueToPointer) {
  StringRef Snippet = "int* p; int *& target = p;";
  EXPECT_EQ("int* p; int * const& target = p;",
            runCheckOnCode<ValueLTransform>(Snippet));
  EXPECT_EQ("int* p; int * const& target = p;",
            runCheckOnCode<PointeeLTransform>(Snippet));

  EXPECT_EQ("int* p; int * const& target = p;",
            runCheckOnCode<ValueRTransform>(Snippet));
  EXPECT_EQ("int* p; int * const& target = p;",
            runCheckOnCode<PointeeRTransform>(Snippet));
}
TEST(Reference, LValueParens) {
  StringRef Snippet = "int x = 42; int ((& target)) = x;";

  EXPECT_EQ("int x = 42; const int ((& target)) = x;",
            runCheckOnCode<ValueLTransform>(Snippet));
  EXPECT_EQ("int x = 42; const int ((& target)) = x;",
            runCheckOnCode<PointeeLTransform>(Snippet));

  EXPECT_EQ("int x = 42; int  const((& target)) = x;",
            runCheckOnCode<ValueRTransform>(Snippet));
  EXPECT_EQ("int x = 42; int  const((& target)) = x;",
            runCheckOnCode<PointeeRTransform>(Snippet));
}
TEST(Reference, ToArray) {
  StringRef ArraySnippet = "int a[4] = {1, 2, 3, 4};";
  StringRef Snippet = "int (&target)[4] = a;";
  auto Cat = [&ArraySnippet](StringRef S) { return (ArraySnippet + S).str(); };

  EXPECT_EQ(Cat("const int (&target)[4] = a;"),
            runCheckOnCode<ValueLTransform>(Cat(Snippet)));
  EXPECT_EQ(Cat("const int (&target)[4] = a;"),
            runCheckOnCode<PointeeLTransform>(Cat(Snippet)));

  EXPECT_EQ(Cat("int  const(&target)[4] = a;"),
            runCheckOnCode<ValueRTransform>(Cat(Snippet)));
  EXPECT_EQ(Cat("int  const(&target)[4] = a;"),
            runCheckOnCode<PointeeRTransform>(Cat(Snippet)));
}
TEST(Reference, Auto) {
  StringRef T = "static int global = 42; int& f() { return global; }\n";
  StringRef S = "auto& target = f();";
  auto Cat = [&T](StringRef S) { return (T + S).str(); };

  EXPECT_EQ(Cat("const auto& target = f();"),
            runCheckOnCode<ValueLTransform>(Cat(S)));
  EXPECT_EQ(Cat("auto const& target = f();"),
            runCheckOnCode<ValueRTransform>(Cat(S)));

  EXPECT_EQ(Cat("const auto& target = f();"),
            runCheckOnCode<PointeeLTransform>(Cat(S)));
  EXPECT_EQ(Cat("auto const& target = f();"),
            runCheckOnCode<PointeeRTransform>(Cat(S)));
}

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
TEST(Pointers, ToArray) {
  StringRef ArraySnippet = "int a[4] = {1, 2, 3, 4};";
  StringRef Snippet = "int (*target)[4] = &a;";
  auto Cat = [&ArraySnippet](StringRef S) { return (ArraySnippet + S).str(); };

  EXPECT_EQ(Cat("int (*const target)[4] = &a;"),
            runCheckOnCode<ValueLTransform>(Cat(Snippet)));
  EXPECT_EQ(Cat("const int (*target)[4] = &a;"),
            runCheckOnCode<PointeeLTransform>(Cat(Snippet)));

  EXPECT_EQ(Cat("int (*const target)[4] = &a;"),
            runCheckOnCode<ValueRTransform>(Cat(Snippet)));
  EXPECT_EQ(Cat("int  const(*target)[4] = &a;"),
            runCheckOnCode<PointeeRTransform>(Cat(Snippet)));
}
TEST(Pointers, Parens) {
  StringRef Snippet = "int ((**target)) = nullptr;";

  EXPECT_EQ("int ((**const target)) = nullptr;",
            runCheckOnCode<ValueLTransform>(Snippet));
  EXPECT_EQ("int ((**const target)) = nullptr;",
            runCheckOnCode<ValueRTransform>(Snippet));

  EXPECT_EQ("int ((* const*target)) = nullptr;",
            runCheckOnCode<PointeeLTransform>(Snippet));
  EXPECT_EQ("int ((* const*target)) = nullptr;",
            runCheckOnCode<PointeeRTransform>(Snippet));
}
TEST(Pointers, Auto) {
  StringRef T = "int* f() { return nullptr; }\n";
  StringRef S = "auto* target = f();";
  auto Cat = [&T](StringRef S) { return (T + S).str(); };

  EXPECT_EQ(Cat("auto* const target = f();"),
            runCheckOnCode<ValueLTransform>(Cat(S)));
  EXPECT_EQ(Cat("auto* const target = f();"),
            runCheckOnCode<ValueRTransform>(Cat(S)));

  EXPECT_EQ(Cat("const auto* target = f();"),
            runCheckOnCode<PointeeLTransform>(Cat(S)));
  EXPECT_EQ(Cat("auto const* target = f();"),
            runCheckOnCode<PointeeRTransform>(Cat(S)));
}

// TODO: Function pointers
// TODO: Member-data pointers
// TODO: Member-function pointers

} // namespace test
} // namespace tidy
} // namespace clang
