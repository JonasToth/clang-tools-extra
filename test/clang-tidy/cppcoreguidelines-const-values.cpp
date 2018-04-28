// RUN: %check_clang_tidy %s cppcoreguidelines-const %t

// ------- Provide test samples for primitive builtins ---------
// - every 'p_*' variable is a 'potential_const_*' variable
// - every 'np_*' variable is a 'non_potential_const_*' variable

bool global;
char np_global = 0; // globals can't be known to be const

namespace foo {
int scoped;
float np_scoped = 1; // namespace variables are like globals
} // namespace foo

void some_function(double, wchar_t);

void some_function(double np_arg0, wchar_t np_arg1) {
  int p_local0 = 2;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local0' of type 'int' can be declared const

  int np_local0;
  const int np_local1 = 42;

  unsigned int np_local2 = 3;
  np_local2 <<= 4;

  int np_local3 = 4;
  ++np_local3;
  int np_local4 = 4;
  np_local4++;

  int np_local5 = 4;
  --np_local5;
  int np_local6 = 4;
  np_local6--;
}

void nested_scopes() {
  int p_local0 = 2;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local0' of type 'int' can be declared const
  int np_local0 = 42;

  {
    int p_local1 = 42;
    // CHECK-MESSAGES: [[@LINE-1]]:5: warning: variable 'p_local1' of type 'int' can be declared const
    np_local0 *= 2;
  }
}

void some_lambda_environment_capture_all_by_reference(double np_arg0) {
  int np_local0 = 0;
  int p_local0 = 1;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local0' of type 'int' can be declared const

  int np_local2;
  const int np_local3 = 2;

  // Capturing all variables by reference prohibits making them const.
  [&]() { ++np_local0; };

  int p_local1 = 0;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local1' of type 'int' can be declared const
}

void some_lambda_environment_capture_all_by_value(double np_arg0) {
  int np_local0 = 0;
  int p_local0 = 1;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local0' of type 'int' can be declared const

  int np_local1;
  const int np_local2 = 2;

  // Capturing by value has no influence on them.
  [=]() { (void)p_local0; };

  np_local0 += 10;
}

void function_inout_pointer(int *inout);
void function_in_pointer(const int *in);

void some_pointer_taking(int *out) {
  int p_local0 = 42;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local0' of type 'int' can be declared const
  const int *const p0_p_local0 = &p_local0;
  int *const p1_p_local0 = &p_local0;

  int np_local0 = 42;
  function_inout_pointer(&np_local0);

  // Prevents const.
  int np_local1 = 42;
  out = &np_local1; // This returns and invalid address, its just about the AST

  int p_local1 = 42;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local1' of type 'int' can be declared const
  const int *const p0_p_local1 = &p_local1;

  int p_local2 = 42;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local2' of type 'int' can be declared const
  function_in_pointer(&p_local2);
}

void function_inout_ref(int &inout);
void function_in_ref(const int &in);

void some_reference_taking() {
  // FIXME False positive
  int np_local0 = 42;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'np_local0' of type 'int' can be declared const
  const int &r0_np_local0 = np_local0;
  int &r1_np_local0 = np_local0;
  r1_np_local0 = 43;
  const int &r2_np_local0 = r1_np_local0;

  int np_local1 = 42;
  function_inout_ref(np_local1);

  int p_local0 = 42;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local0' of type 'int' can be declared const
  const int &r0_p_local0 = p_local0;

  int p_local1 = 42;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local1' of type 'int' can be declared const
  function_in_ref(p_local1);
}

double *non_const_pointer_return() {
  double p_local0 = 0.0;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local0' of type 'double' can be declared const
  double np_local0 = 24.4;

  return &np_local0;
}

const double *const_pointer_return() {
  double p_local0 = 0.0;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local0' of type 'double' can be declared const
  double p_local1 = 24.4;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local1' of type 'double' can be declared const
  return &p_local1;
}

double &non_const_ref_return() {
  double p_local0 = 0.0;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local0' of type 'double' can be declared const
  double np_local0 = 42.42;
  return np_local0;
}

const double &const_ref_return() {
  double p_local0 = 0.0;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local0' of type 'double' can be declared const
  double p_local1 = 24.4;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local1' of type 'double' can be declared const
  return p_local1;
}

double *&return_non_const_pointer_ref() {
  double *np_local0 = nullptr;
  return np_local0;
}

void overloaded_arguments(const int &in);
void overloaded_arguments(int &inout);
void overloaded_arguments(const int *in);
void overloaded_arguments(int *inout);

void function_calling() {
  int np_local0 = 42;
  overloaded_arguments(np_local0);

  const int np_local1 = 42;
  overloaded_arguments(np_local1);

  int np_local2 = 42;
  overloaded_arguments(&np_local2);

  const int np_local3 = 42;
  overloaded_arguments(&np_local3);
}

template <typename T>
void define_locals(T np_arg0, T &np_arg1, int np_arg2) {
  T np_local0 = 0;
  np_local0 += np_arg0 * np_arg1;

  T np_local1 = 42;
  np_local0 += np_local1;

  // Used as argument to an overloaded function with const and non-const.
  T np_local2 = 42;
  overloaded_arguments(np_local2);

  int np_local4 = 42;
  // non-template values are ok still.
  int p_local0 = 42;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local0' of type 'int' can be declared const
  np_local4 += p_local0;
}

void template_instantiation() {
  const int np_local0 = 42;
  int np_local1 = 42;

  define_locals(np_local0, np_local1, np_local0);
  define_locals(np_local1, np_local1, np_local1);
}

struct ConstNonConstClass {
  ConstNonConstClass();
  ConstNonConstClass(double &np_local0);
  double nonConstMethod() {}
  double constMethod() const {}
  double modifyingMethod(double &np_arg0) const;

  double NonConstMember;
  const double ConstMember;

  double &NonConstMemberRef;
  const double &ConstMemberRef;

  double *NonConstMemberPtr;
  const double *ConstMemberPtr;
};

void direct_class_access() {
  // FIXME False positive
  ConstNonConstClass np_local0;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'np_local0' of type 'ConstNonConstClass' can be declared const

  np_local0.constMethod();
  np_local0.nonConstMethod();

  ConstNonConstClass p_local0;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local0' of type 'ConstNonConstClass' can be declared const
  p_local0.constMethod();

  ConstNonConstClass p_local1;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local1' of type 'ConstNonConstClass' can be declared const
  double np_local1;
  p_local1.modifyingMethod(np_local1);

  double np_local2;
  ConstNonConstClass p_local2(np_local2);
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local2' of type 'ConstNonConstClass' can be declared const

  ConstNonConstClass np_local3;
  np_local3.NonConstMember = 42.;

  ConstNonConstClass np_local4;
  np_local4.NonConstMemberRef = 42.;

  ConstNonConstClass p_local3;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local3' of type 'ConstNonConstClass' can be declared const
  const double val0 = p_local3.NonConstMember;
  const double val1 = p_local3.NonConstMemberRef;
  const double val2 = *p_local3.NonConstMemberPtr;

  ConstNonConstClass p_local4;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local4' of type 'ConstNonConstClass' can be declared const
  *np_local4.NonConstMemberPtr = 42.;
}

struct OperatorsAsConstAsPossible {
  OperatorsAsConstAsPossible &operator+=(const OperatorsAsConstAsPossible &rhs);
  OperatorsAsConstAsPossible operator+(const OperatorsAsConstAsPossible &rhs) const;
};

struct NonConstOperators {
};
NonConstOperators operator+(NonConstOperators &lhs, NonConstOperators &rhs);
NonConstOperators operator-(NonConstOperators lhs, NonConstOperators rhs);

void internal_operator_calls() {
  OperatorsAsConstAsPossible np_local0;
  OperatorsAsConstAsPossible np_local1;
  OperatorsAsConstAsPossible p_local0;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local0' of type 'OperatorsAsConstAsPossible' can be declared const
  OperatorsAsConstAsPossible p_local1;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local1' of type 'OperatorsAsConstAsPossible' can be declared const

  np_local0 += p_local0;
  np_local1 = p_local0 + p_local1;

  NonConstOperators np_local2;
  NonConstOperators np_local3;
  NonConstOperators np_local4;

  np_local2 = np_local3 + np_local4;

  NonConstOperators p_local2;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local2' of type 'NonConstOperators' can be declared const
  NonConstOperators p_local3 = p_local2 - p_local2;
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local3' of type 'NonConstOperators' can be declared const
}

struct MyVector {
  double *begin();
  const double *begin() const;

  double *end();
  const double *end() const;

  double &operator[](int index);
  double operator[](int index) const;

  double values[100];
};

void vector_usage() {
  double np_local0[10];
  np_local0[5] = 42.;

  MyVector np_local1;
  np_local1[5] = 42.;

  double p_local0[10] = {0., 1., 2., 3., 4., 5., 6., 7., 8., 9.};
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local0' of type 'double [10]' can be declared const
  double p_local1 = p_local0[5];
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local1' of type 'double' can be declared const

  // The following subscript calls suprisingly choose the non-const operator
  // version.
  MyVector np_local2;
  double p_local2 = np_local2[42];
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local2' of type 'double' can be declared const

  MyVector np_local3;
  const double np_local4 = np_local3[42];

  // This subscript results in const overloaded operator.
  const MyVector np_local5{};
  double p_local3 = np_local5[42];
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local3' of type 'double' can be declared const
}

void const_handle(const double &np_local0);
void const_handle(const double *np_local0);

void non_const_handle(double &np_local0);
void non_const_handle(double *np_local0);

void handle_from_array() {
  // Non-const handle from non-const array forbids declaring the array as const
  double np_local0[10] = {0., 1., 2., 3., 4., 5., 6., 7., 8., 9.};
  double *p_local0 = &np_local0[1]; // Could be `double *const`, but warning deactivated by default

  double np_local1[10] = {0., 1., 2., 3., 4., 5., 6., 7., 8., 9.};
  double &non_const_ref = np_local1[1];
  non_const_ref = 42.;

  double np_local2[10] = {0., 1., 2., 3., 4., 5., 6., 7., 8., 9.};
  double *np_local3;
  np_local3 = &np_local2[5];

  double np_local4[10] = {0., 1., 2., 3., 4., 5., 6., 7., 8., 9.};
  non_const_handle(np_local4[2]);
  double np_local5[10] = {0., 1., 2., 3., 4., 5., 6., 7., 8., 9.};
  non_const_handle(&np_local5[2]);

  // Constant handles are ok
  double p_local1[10] = {0., 1., 2., 3., 4., 5., 6., 7., 8., 9.};
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local1' of type 'double [10]' can be declared const
  const double *p_local2 = &p_local1[2]; // Could be `const double *const`, but warning deactivated by default

  double p_local3[10] = {0., 1., 2., 3., 4., 5., 6., 7., 8., 9.};
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local3' of type 'double [10]' can be declared const
  const double &const_ref = p_local3[2];

  double p_local4[10] = {0., 1., 2., 3., 4., 5., 6., 7., 8., 9.};
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local4' of type 'double [10]' can be declared const
  const double *const_ptr;
  const_ptr = &p_local4[2];

  double p_local5[10] = {0., 1., 2., 3., 4., 5., 6., 7., 8., 9.};
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local5' of type 'double [10]' can be declared const
  const_handle(p_local5[2]);
  double p_local6[10] = {0., 1., 2., 3., 4., 5., 6., 7., 8., 9.};
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local6' of type 'double [10]' can be declared const
  const_handle(&p_local6[2]);
}

void range_for() {
  int np_local0[2] = {1, 2};
  for (int &non_const_ref : np_local0) {
    non_const_ref = 42;
  }

  int np_local1[2] = {1, 2};
  for (auto &non_const_ref : np_local1) {
    non_const_ref = 43;
  }

  int np_local2[2] = {1, 2};
  for (auto &&non_const_ref : np_local2) {
    non_const_ref = 44;
  }

  // FIXME false positive
  int *np_local3[2] = {&np_local0[0], &np_local0[1]};
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'np_local3' of type 'int *[2]' can be declared const
  for (int *non_const_ptr : np_local3) {
    *non_const_ptr = 45;
  }

  // FIXME false positive
  int *np_local4[2] = {&np_local0[0], &np_local0[1]};
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'np_local4' of type 'int *[2]' can be declared const
  for (auto *non_const_ptr : np_local4) {
    *non_const_ptr = 46;
  }

  int p_local0[2] = {1, 2};
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local0' of type 'int [2]' can be declared const
  for (int value : p_local0) {
    // CHECK-MESSAGES: [[@LINE-1]]:8: warning: variable 'value' of type 'int' can be declared const
  }

  int p_local1[2] = {1, 2};
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local1' of type 'int [2]' can be declared const
  for (const int &const_ref : p_local1) {
  }

  int *p_local2[2] = {&np_local0[0], &np_local0[1]};
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local2' of type 'int *[2]' can be declared const
  for (const int *con_ptr : p_local2) {
  }

  int *p_local3[2] = {nullptr, nullptr};
  // CHECK-MESSAGES: [[@LINE-1]]:3: warning: variable 'p_local3' of type 'int *[2]' can be declared const
  for (const auto *con_ptr : p_local3) {
  }
}
