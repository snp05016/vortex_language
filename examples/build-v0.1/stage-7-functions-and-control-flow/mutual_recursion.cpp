// Two functions that call each other, tracing every call and return so the
// call stack's shape becomes visible. Each call gets a "depth" argument
// purely for the trace; nothing here shares storage between calls, which is
// the point: every call gets its own frame, so a deeper call cannot disturb
// the caller's still-pending one.
//
// Follows: cppreference, "Function declaration", for the declarations
// without bodies that let is_even call is_odd before is_odd is defined, and
// "Standard format specification", for the nested {} that sets the indent.

#include <print>

bool is_even(unsigned n, int depth);
bool is_odd(unsigned n, int depth);

bool is_even(unsigned n, int depth) {
  std::println("{:>{}}is_even({}) called", "", depth * 2, n);
  bool result = (n == 0) ? true : is_odd(n - 1, depth + 1);
  std::println("{:>{}}is_even({}) returns {}", "", depth * 2, n, result);
  return result;
}

bool is_odd(unsigned n, int depth) {
  std::println("{:>{}}is_odd({}) called", "", depth * 2, n);
  bool result = (n == 0) ? false : is_even(n - 1, depth + 1);
  std::println("{:>{}}is_odd({}) returns {}", "", depth * 2, n, result);
  return result;
}

int main() {
  std::println("is_even(4) = {}", is_even(4, 0));
}
