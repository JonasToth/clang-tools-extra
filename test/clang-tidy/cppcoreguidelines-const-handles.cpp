// RUN: %check_clang_tidy %s cppcoreguidelines-const %t \
// RUN: -config="{CheckOptions: \
// RUN:  [{key: cppcoreguidelines-const.AnalyzeValues, value: 0}, \
// RUN:   {key: cppcoreguidelines-const.AnalyzeHandles, value: 1}]}" \
// RUN: --

struct SomeClass {
  void nonConst();
  void constMethod() const;
};

void call_through_handle() {
  SomeClass np_local0;
  SomeClass *np_local1 = &np_local0;
  np_local1->nonConst();

  SomeClass np_local2;
  SomeClass *p_local0 = &np_local2;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: pointer variable 'p_local0' of type 'SomeClass *' can be declared const
  np_local1->constMethod();

  SomeClass np_local3;
  SomeClass &np_local4 = np_local3;
  np_local4.nonConst();

  SomeClass np_local5;
  SomeClass &p_local1 = np_local5;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: reference variable 'p_local1' of type 'SomeClass &' can be declared const
  p_local1.constMethod();
}

void function() {
  double np_local0 = 42.;
  np_local0 = 49.;
  double &p_local0 = np_local0;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: reference variable 'p_local0' of type 'double &' can be declared const

  const double np_local1 = np_local0 + p_local0;
}
