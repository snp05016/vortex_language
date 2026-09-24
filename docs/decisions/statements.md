# Statements and control flow

These records settle how `return` mistakes are classified, when a function
body counts as ending in a `return`, how `for` loops and ranges behave, and the
order in which expressions and assignments are evaluated. The
[Statements](../specification/statements.md) and
[Expressions](../specification/expressions.md) chapters now state the rules;
each record below explains why the rule was chosen and what it changed.

## 8. Return errors {#d8}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 5.

**Question.** Which diagnostic category reports a `return` that does not fit
its function, and which reports a function that can finish without returning a
value?

**Before this decision.** [Statements](../specification/statements.md#610-return)
section 6.10 called "return legality and path coverage" semantic and
control-flow checks, while
[Diagnostics](../specification/diagnostics.md#type-error) section 10.2 listed a
mismatched return value as a type error. The
[stage 5 guide](../compiler/guide/stage-5-types-and-rules.md) said "type or
semantic" for a bare `return;` in an `i32` function.

**Options.**

- All return mistakes are type errors.
- All return mistakes are semantic errors.
- A `return` that disagrees with the return type is a type error; a missing
  return path is a semantic error.

**Elsewhere.** Rust has a dedicated error, [E0069][rs-e0069], for `return;` in
a function that returns a value. Go requires a function with results to end in
a [terminating statement][go-term]. In C++,
[flowing off the end][cpp-return] of a value-returning function is undefined
behavior.

**Decision.** The third option.

- In a non-`void` function, `return;` is a type error.
- In a `void` function, `return expression;` is a type error, even when the
  expression is a call to a `void` function ([record 44](operators.md#d44)).
- In a non-`void` function, `return expression;` is a type error unless the
  expression is compatible with the return type: the same type after literal
  typing ([record 30](numbers.md#d30)).
- A non-`void` function whose body does not end in a terminating statement
  ([record 9](#d9)) is a semantic error.

**Why.** The category follows the information the check needs. Comparing a
`return` with the declared type needs only types. Deciding whether a body can
reach its end needs the shape of the control flow, like the rule for `break`
outside a loop.

**Consequences.** Programs rejected before are still rejected; only the
category is now fixed.

```vortex
// items: type error
fn count() -> i32 {
    return;    // type error: an i32 function must return a value
}
```

Stage 5 reports the three mismatches as type errors and the missing path as a
semantic error. Pages changed: Statements 6.10, Diagnostics 10.2, the grammar,
the statements tour and the stage 5 guide.

## 9. Terminating statements and reachability {#d9}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 5 and 7.

**Question.** A non-`void` function had to return a value "on every reachable
path". What makes a path reachable, and must the compiler reason about
conditions such as `while true`?

**Before this decision.** [Statements](../specification/statements.md#610-return)
section 6.10 used "reachable" without defining it, and the
[stage 5 guide](../compiler/guide/stage-5-types-and-rules.md) left `while true`
as an open decision.

**Options.**

- A structural rule that looks only at the form of the statements.
- The structural rule plus constant conditions (`while true` never ends).
- Full flow analysis.

**Elsewhere.** Go defines a list of [terminating statements][go-term] that
includes a `for` loop with no condition and no `break`. Rust gives a
[`loop` without `break`][rs-loop] the type `!` ("never"), so such a loop can
end a function.

**Decision.** The structural rule. A **terminating statement** is one after
which execution cannot continue with the next statement:

- a `return` terminates;
- a block terminates when its last statement terminates (an empty block does
  not);
- an `if` terminates when it has an `else` and both branches terminate (an
  `else if` branch is an `if`, so the rule repeats);
- nothing else terminates: `while` and `for` never do, not even `while true`.

The body of a non-`void` function must terminate, or the program has a
semantic error ([record 8](#d8)). Statements after a `return`, `break` or
`continue` are valid, never run, may draw a warning, and still count as the
last statement of their block.

**Why.** The rule reads only the syntax tree, needs no constant evaluation
([record 11](arrays.md#d11)), and gives the same answer in every
implementation. The price is an occasional `return` that never runs.

**Consequences.**

```vortex
// items: semantic error
fn root(target: i32) -> i32 {
    let mut guess = 0;
    while true {
        if guess * guess >= target {
            return guess;
        }
        guess += 1;
    }
}   // semantic error: the body ends with a loop
```

A `return guess;` after the loop makes it valid. Stage 5 checks the rule;
stage 7 relies on it, since no accepted non-`void` function reaches its
closing brace. Pages changed: Statements 6.10, the glossaries, the statements
tour, the roadmap, and guide stages 5 and 7.

## 13. `for` loop semantics {#d13}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 5 and 7.

**Question.** What type does the loop variable have, may the body assign it,
when are the endpoints evaluated, and how often does the body run, including
at a type's largest value?

**Before this decision.** [Statements](../specification/statements.md#68-for-loops)
section 6.8 said the expression after `in` "must be iterable", and the
[grammar](../specification/grammar.md#for-loops) said the loop variable
"receives the element type"; neither defined its term. The
[stage 7 guide](../compiler/guide/stage-7-functions-and-control-flow.md)
listed these questions as open.

**Options.**

- Evaluate the endpoints once, or before every iteration.
- Make the loop variable immutable, or let the body assign it.
- Run a range whose start is past its end zero times, or reject it.

**Elsewhere.** Go evaluates the [range expression once][go-range], before the
loop. Swift requires the lower bound of a [closed range][sw-range] to
be no greater than the upper bound. Rust's [`for` loop][rs-iterloop] accepts
any iterator.

**Decision.** For `for i in a..b` and `for i in a..=b`:

- the iterable must be a range ([record 36](#d36));
- `a`, then `b`, are evaluated once, before the loop;
- after literal typing, `a` and `b` must have the same integer type (a type
  error otherwise); a literal endpoint takes the other's type, and two
  literals are `i32` ([record 31](numbers.md#d31));
- `i` has that type and is immutable, so assigning it is a semantic error;
  each iteration binds a new `i`;
- `a..b` runs the body for `a` up to `b - 1`, and zero times when `a >= b`;
- `a..=b` runs it for `a` up to `b`, and zero times when `a > b`; it never
  computes `b + 1`, so the loop itself cannot overflow.

**Why.** No hidden allocation or iterator protocol, a count fixed when the
loop starts, and rules that `print` can test.

**Consequences.**

```vortex
// statements: valid
let mut end = 3;
for i in 0..end {
    end = 10;    // does not change this loop
    print(i);    // prints 0, then 1, then 2
}
```

Stage 5 checks types and immutability; stage 7 generates the loops.
Pages changed: Statements 6.8, Declarations 3.6, the grammar, parser design,
the tour, the cheat sheet, and guide stages 5 and 7.

## 36. Ranges outside `for` {#d36}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N9.
**Guide stage:** 3 and 5.

**Question.** The grammar lets a range such as `0..10` appear wherever an
expression may, but v0.1 has no range type. What does `let span = 0..10;`
mean?

**Before this decision.** [Expressions](../specification/expressions.md#58-ranges)
section 5.8 gave endpoint rules only "when used by a `for` loop", the
[grammar](../specification/grammar.md#expressions-and-precedence) placed ranges
at the lowest precedence level of every expression, and no page gave a range a
type.

**Options.**

- Make the range part of the `for` statement's syntax, so any other use is a
  syntax error.
- Keep the grammar and add a static rule that allows a range only as a `for`
  iterable.
- Add a range type, so that ranges become values.

**Elsewhere.** In Go, [`range`][go-range] is a clause of the `for` statement,
not an expression. In Rust, [ranges][rs-range] are ordinary values.

**Decision.** The static rule. The grammar does not change: `range_expression`
stays the lowest precedence level, and ranges still do not chain.

- A range expression may appear only as the iterable of a `for` statement,
  possibly enclosed in parentheses.
- A range anywhere else is a type error: a range is not a value in v0.1.
- A `for` iterable that is not a range is a type error.
- Arrays as iterables are [Planned](../specification/conformance.md#18-specification-examples).

**Why.** The grammar and the [parser design](../compiler/parser-design.md)
already treat a range and a `for` iterable as expressions, so neither
changes. No type is invented, the message can say what is wrong, and array
iteration can come later without new syntax.

**Consequences.** Every existing example uses ranges only in `for` headers,
so none changes its result.

```vortex
// statements: type error
let span = 0..10;    // type error: a range is not a value in v0.1
```

`for i in (0..10) { }` is valid; `values[0..2]` and `for v in values { }` are
type errors. Stage 3 parses ranges as before; stage 5 reports the errors.
Pages changed: Expressions 5.1, 5.2 and 5.8, Statements 6.8, Diagnostics 10.2,
the grammar, the glossary, both tours, the cheat sheet, the home page, the
roadmap, parser design, and guide stages 3 and 5.

## 38. Evaluation order {#d38}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N15.
**Guide stage:** 6, 7, 8 and 9.

**Question.** In what order are the parts of an expression and of an
assignment evaluated, and when does each runtime check happen?

**Before this decision.** [Expressions](../specification/expressions.md#510-evaluation-order)
section 5.10 required left-to-right order only for "ordered child lists" such
as call arguments, and [Statements](../specification/statements.md#64-assignment)
section 6.4 said only that an assignment target "is evaluated once". The order
of binary operands, of a target and its value, and of runtime checks was open,
although `print` and runtime errors make it visible.

**Options.**

- Left to right, with an assignment's target before its value.
- Left to right, with an assignment's value before its target.
- Leave part of the order unspecified.

**Elsewhere.** Rust evaluates [operands left to right][rs-evalorder] but an
[assignment's value first][rs-assign]. Go fixes the [order of calls][go-order]
only and [assigns in two phases][go-assign]: operands, then stores. C++ leaves
many [operands unsequenced][cpp-eval].

**Decision.** The first option.

- Operands, call arguments, array elements, struct field values (in the order
  written) and indices are evaluated left to right; `&&` and `||` keep their
  short-circuit rule.
- For `place = value;` and `place op= value;` ([record 23](references.md#d23)
  defines places), the order is: the place's base and indices, left to right; then `value`;
  then the bounds check of each place index; for compound assignment, the
  load, the operation and its check; then the store.
- Every other check happens when its own operation runs, after its operands.

**Why.** One rule covers every construct, and `print` can test each part. A
check belongs to the operation it guards; for a place, that is the store,
which needs the value first.

**Consequences.**

```vortex
// program: runtime error
fn make() -> i32 {
    print("value");
    return 7;
}

fn main() {
    let mut values = [0, 0, 0];
    let index = 5;
    values[index] = make();    // prints value, then stops: 5 is out of bounds
}
```

Stages 6 and 7 emit operands and calls in this order, stage 8 emits element
stores, and stage 9 places the checks. Pages changed: Expressions 5.10,
Statements 6.4, Arrays 7.6 and 7.7, both tours, and the stage 6 to 9 guides.

[rs-e0069]: https://doc.rust-lang.org/error_codes/E0069.html
[go-term]: https://go.dev/ref/spec#Terminating_statements
[cpp-return]: https://en.cppreference.com/cpp/language/return
[rs-loop]: https://doc.rust-lang.org/reference/expressions/loop-expr.html#infinite-loops
[go-range]: https://go.dev/ref/spec#For_range
[sw-range]: https://docs.swift.org/swift-book/documentation/the-swift-programming-language/basicoperators#Closed-Range-Operator
[rs-iterloop]: https://doc.rust-lang.org/reference/expressions/loop-expr.html#iterator-loops
[rs-range]: https://doc.rust-lang.org/reference/expressions/range-expr.html
[rs-evalorder]: https://doc.rust-lang.org/reference/expressions.html#evaluation-order-of-operands
[rs-assign]: https://doc.rust-lang.org/reference/expressions/operator-expr.html#assignment-expressions
[go-order]: https://go.dev/ref/spec#Order_of_evaluation
[go-assign]: https://go.dev/ref/spec#Assignment_statements
[cpp-eval]: https://en.cppreference.com/cpp/language/eval_order
