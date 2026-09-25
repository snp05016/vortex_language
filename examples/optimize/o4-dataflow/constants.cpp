// Constant propagation on a function with one if and no loop:
//   x = 2; y = 3; if swap { x = 3; y = 2; } return x + y;
// Blocks: A (x = 2; y = 3; branch on swap), B (x = 3; y = 2) and C (return
// x + y), with edges A -> B, A -> C and B -> C. Both paths return 5.
//
// The fixed-point solution joins the states that reach C, then evaluates
// x + y once. (With no loop, one pass in reverse postorder is already the
// fixed point.) The all-paths solution evaluates x + y along each path, then
// joins the answers. They differ because evaluating x + y does not distribute
// over the join: the join keeps each variable's values, but forgets that x and
// y changed together.
//
// Each variable holds a value from the flat lattice: bottom (no value seen
// yet), one integer, or top (not a constant).
//
// Follows: Møller and Schwartzbach, Static Program Analysis, sections 5.2 and
// 9.5; Kam and Ullman, "Monotone Data Flow Analysis Frameworks" (1977).

#include <print>
#include <string>

struct Value {
  enum Kind { bottom, constant, top } kind = bottom;
  int n = 0;
};

Value join(Value a, Value b) {
  if (a.kind == Value::bottom) return b;
  if (b.kind == Value::bottom) return a;
  if (a.kind == Value::constant && b.kind == Value::constant && a.n == b.n) return a;
  return {Value::top, 0};
}

Value add(Value a, Value b) {
  if (a.kind == Value::bottom || b.kind == Value::bottom) return {Value::bottom, 0};
  if (a.kind == Value::top || b.kind == Value::top) return {Value::top, 0};
  return {Value::constant, a.n + b.n};
}

std::string show(Value v) {
  if (v.kind == Value::bottom) return "bottom";
  return v.kind == Value::top ? "top" : std::to_string(v.n);
}

struct State {
  Value x, y;
};

State join(State a, State b) { return {join(a.x, b.x), join(a.y, b.y)}; }

// The transfer functions of the three blocks.
State after_a() { return {{Value::constant, 2}, {Value::constant, 3}}; }
State after_b(State) { return {{Value::constant, 3}, {Value::constant, 2}}; }
Value returned_by_c(State in) { return add(in.x, in.y); }

int main() {
  const State from_a = after_a(), from_b = after_b(from_a);
  const State at_c = join(from_a, from_b);  // what reaches C along both edges
  std::println("fixed point at C:  x = {}, y = {}, x + y = {}", show(at_c.x), show(at_c.y), show(returned_by_c(at_c)));
  std::println("path A, C:         x = {}, y = {}, x + y = {}", show(from_a.x), show(from_a.y), show(returned_by_c(from_a)));
  std::println("path A, B, C:      x = {}, y = {}, x + y = {}", show(from_b.x), show(from_b.y), show(returned_by_c(from_b)));
  std::println("all paths joined:  x = {}, y = {}, x + y = {}", show(join(from_a.x, from_b.x)), show(join(from_a.y, from_b.y)),
               show(join(returned_by_c(from_a), returned_by_c(from_b))));
}
