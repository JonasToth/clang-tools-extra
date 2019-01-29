// RUN: %check_clang_tidy %s modernize-use-ranges %t -- -- \
// RUN:    -std=c++2a -I %S/Inputs/modernize-use-ranges

#include "array.h"
#include "algorithm.h"

using SomePolicy = int;

int *Begin(std::array<int, 4> &a);
int *End(std::array<int, 4> &a);

void single_range_all_of_everything() {
  std::array<int, 4> foo, bar;

  std::all_of(foo.begin(), foo.end(), [](int x) { return x == 0; });
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: algorithm could be rewritten with std::ranges
  // CHECK-FIXES: std::all_of(foo, [](int x) { return x == 0; });
  std::all_of(std::begin(foo), std::end(foo), [](int x) { return x == 0; });
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: algorithm could be rewritten with std::ranges
  // CHECK-FIXES: std::all_of(foo, [](int x) { return x == 0; });

  std::all_of(std::cbegin(foo), std::cend(foo), [](int x) { return x == 0; });
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: algorithm could be rewritten with std::ranges
  // CHECK-FIXES: std::all_of(foo, [](int x) { return x == 0; });
  std::all_of(foo.cbegin(), foo.cend(), [](int x) { return x == 0; });
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: algorithm could be rewritten with std::ranges
  // CHECK-FIXES: std::all_of(foo, [](int x) { return x == 0; });

  std::all_of((foo.cbegin()), foo.cend(), [](int x) { return x == 0; });
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: algorithm could be rewritten with std::ranges
  // CHECK-FIXES: std::all_of(foo, [](int x) { return x == 0; });

  // Variations that are not diagnosed yet, because either advanced feature
  // or it would be incorrect, because not the full range of the container
  // is used.
  std::all_of(std::rbegin(foo), std::rend(foo), [](int x) { return x == 0; });
  std::all_of(std::crbegin(foo), std::crend(foo), [](int x) { return x == 0; });
  std::all_of(foo.rbegin(), foo.rend(), [](int x) { return x == 0; });
  std::all_of(foo.crbegin(), foo.crend(), [](int x) { return x == 0; });

  std::all_of(foo.begin() + 2, foo.end() - 2, [](int x) { return x == 0; });
  std::all_of(foo.begin(), foo.end() - 2, [](int x) { return x == 0; });
  std::all_of(foo.begin() + 2, foo.end(), [](int x) { return x == 0; });

  std::all_of(foo.cbegin() + 2, foo.cend() - 2, [](int x) { return x == 0; });
  std::all_of(foo.cbegin(), foo.cend() - 2, [](int x) { return x == 0; });
  std::all_of(foo.cbegin() + 2, foo.cend(), [](int x) { return x == 0; });

  std::all_of(std::begin(foo) + 2, std::end(foo) - 2, [](int x) { return x == 0; });
  std::all_of(std::begin(foo), std::end(foo) - 2, [](int x) { return x == 0; });
  std::all_of(std::begin(foo) + 2, std::end(foo), [](int x) { return x == 0; });

  std::all_of(std::cbegin(foo) + 2, std::cend(foo) - 2, [](int x) { return x == 0; });
  std::all_of(std::cbegin(foo), std::cend(foo) - 2, [](int x) { return x == 0; });
  std::all_of(std::cbegin(foo) + 2, std::cend(foo), [](int x) { return x == 0; });

  std::all_of(std::cend(foo) - 1, std::cbegin(foo) - 1, [](int x) { return x == 0; });
  std::all_of(Begin(foo), End(foo), [](int x) { return x == 0; });
  std::all_of(static_cast<int *>(foo.cbegin()), foo.cend(), [](int x) { return x == 0; });
  std::all_of(SomePolicy(42), std::begin(foo), std::end(foo), [](int x) { return x == 0; });

  // container-names missmatch and it's not safe to do the transformation.
  std::all_of(std::begin(foo), std::end(bar), [](int x) { return x == 0; });
  // TODO: use index-expr as container argument
}

void single_range_others() {
  std::array<int, 4> foo, bar;

  std::any_of(foo.begin(), foo.end(), [](int x) { return x == 0; });
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: algorithm could be rewritten with std::ranges
  // CHECK-FIXES: std::any_of(foo, [](int x) { return x == 0; });
  std::any_of(SomePolicy(42), std::begin(foo), std::end(foo), [](int x) { return x == 0; });

  std::none_of(foo.begin(), foo.end(), [](int x) { return x == 0; });
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: algorithm could be rewritten with std::ranges
  // CHECK-FIXES: std::none_of(foo, [](int x) { return x == 0; });
  std::none_of(SomePolicy(42), std::begin(foo), std::end(foo), [](int x) { return x == 0; });

  std::for_each(foo.begin(), foo.end(), [](int x) { return x == 0; });
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: algorithm could be rewritten with std::ranges
  // CHECK-FIXES: std::for_each(foo, [](int x) { return x == 0; });
  std::for_each(SomePolicy(42), std::begin(foo), std::end(foo), [](int x) { return x == 0; });

  std::count(foo.begin(), foo.end(), 42);
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: algorithm could be rewritten with std::ranges
  // CHECK-FIXES: std::count(foo, 42);
  std::count(SomePolicy(42), std::begin(foo), std::end(foo), 42);

  std::count_if(foo.begin(), foo.end(), [](int x) { return x % 2 == 0; });
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: algorithm could be rewritten with std::ranges
  // CHECK-FIXES: std::count_if(foo, [](int x) { return x % 2 == 0; });
  std::count_if(SomePolicy(42), std::begin(foo), std::end(foo), [](int x) { return x % 2 == 0; });

  std::find(foo.begin(), foo.end(), 42);
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: algorithm could be rewritten with std::ranges
  // CHECK-FIXES: std::find(foo, 42);
  std::find(SomePolicy(42), std::begin(foo), std::end(foo), 42);

  std::find_if(foo.begin(), foo.end(), [](int x) { return x % 2 == 0; });
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: algorithm could be rewritten with std::ranges
  // CHECK-FIXES: std::find_if(foo, [](int x) { return x % 2 == 0; });
  std::find_if(SomePolicy(42), std::begin(foo), std::end(foo), [](int x) { return x % 2 == 0; });

  std::find_if_not(foo.begin(), foo.end(), [](int x) { return x % 2 == 0; });
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: algorithm could be rewritten with std::ranges
  // CHECK-FIXES: std::find_if_not(foo, [](int x) { return x % 2 == 0; });
  std::find_if_not(SomePolicy(42), std::begin(foo), std::end(foo), [](int x) { return x % 2 == 0; });
}

void missmatch_algorithm() {
}
