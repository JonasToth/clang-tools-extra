#ifndef ALGORITHM_H
#define ALGORITHM_H

namespace std {
//  Non-modifying sequence operations.

template <class InputIt, class UnaryPredicate>
bool all_of(InputIt first, InputIt last, UnaryPredicate p) { return true; }
template <class ExecutionPolicy, class ForwardIt, class UnaryPredicate>
bool all_of(ExecutionPolicy &&policy, ForwardIt first, ForwardIt last, UnaryPredicate p) { return true; }
} // namespace std

#endif /* end of include guard: ALGORITHM_H */
