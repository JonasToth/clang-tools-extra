// RUN: %check_clang_tidy %s modernize-use-ranges %t -- -- \
// RUN:    -std=c++2a -I %S/Inputs/modernize-use-ranges

#include "array.h"
#include "algorithm.h"

using SomePolicy = int;

int *Begin(std::array<int, 4> &a);
int *End(std::array<int, 4> &a);

void all_of_all_possibilities() {
  std::array<int, 4> foo, bar;

  std::all_of(foo.begin(), foo.end(), [](int x) { return x == 0; });
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: algorithm could be rewritten with std::ranges
  std::all_of(std::begin(foo), std::end(foo), [](int x) { return x == 0; });
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: algorithm could be rewritten with std::ranges

  std::all_of(std::cbegin(foo), std::cend(foo), [](int x) { return x == 0; });
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: algorithm could be rewritten with std::ranges
  std::all_of(foo.cbegin(), foo.cend(), [](int x) { return x == 0; });
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: algorithm could be rewritten with std::ranges
  
  std::all_of((foo.cbegin()), foo.cend(), [](int x) { return x == 0; });
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: algorithm could be rewritten with std::ranges

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
  std::all_of(static_cast<int*>(foo.cbegin()), foo.cend(), [](int x) { return x == 0; });
  std::all_of(SomePolicy(42), std::begin(foo), std::end(foo), [](int x) { return x == 0; });

  // container-names missmatch and it's not safe to do the transformation.
  std::all_of(std::begin(foo), std::end(bar), [](int x) { return x == 0; });
  // TODO: use index-expr as container argument
}
