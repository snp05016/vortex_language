# Statements

## Learning goals

After this chapter, you should be able to recognize every v0.1 statement,
identify where semicolons are required, and explain the contextual checks for
assignment, loops, and return.

A statement tells Vortex to do something. Unlike an expression, a statement is
usually used for its effect: creating a variable, changing a value, choosing a
path, repeating work, or leaving a function.

Simple statements (declarations, assignments, expression statements, `return`,
`break` and `continue`) always end with a semicolon, whether they fit on one
line or span several: line breaks have no meaning in Vortex. Statements that
end with a block, such as `if`, `while` and `for`, take no semicolon after the
closing brace; a semicolon there is a syntax error
([decision 54](../decisions/documentation.md#d54)).

--8<-- "includes/remember/language-tour__09-statements.md"

## Statement quick reference

| Statement | Example | Semicolon after it? |
| --- | --- | --- |
| Variable declaration | `let count = 0;` | Yes |
| Assignment | `count += 1;` | Yes |
| Expression statement | `print(count);` | Yes |
| Return | `return count;` | Yes |
| Break/continue | `break;` | Yes |
| Block | `{ ... }` | No |
| `if`/`else` | `if ready { ... }` | No |
| `while` | `while ready { ... }` | No |
| `for` | `for i in 0..4 { ... }` | No |

Vortex v0.1 does not include declaration-only locals, `switch`, `match`, `do while`,
exceptions, `defer`, or parallel-loop statements.

## Variable declarations

Use `let` to create a variable:

```vortex
// statements: valid
let width = 128;
let mut total: f32 = 0.0;
```

The first variable cannot change. The second can change because it uses `mut`.

Every variable declaration needs an initializer. The optional type annotation
must agree with that expression. [Parsing](../compiler/guide/index.md) (the compiler stage that reads the structure of the code)
checks the syntax; type checking and
local-scope creation validate the declaration.

## Assignment statements

An assignment changes a mutable variable or a mutable array element:

```vortex
// statements: valid
let mut total = 0;
total = 10;

let mut values = [1.0, 2.0, 3.0];
values[0] = 10.0;
```

Vortex does not allow assignment to a variable created without `mut`, or to a
function parameter: parameters are immutable.

??? check "Can a function change one of its own parameters, such as `value = 0;` where `value` is a plain `i32` parameter?"

    No. Every parameter is immutable unless its type is `&mut T`; assigning to
    a plain parameter is a semantic error, even though the parameter is only a
    local name inside the function.

The target may be a mutable variable, or a field or element of one. It may
also be reached through a `&mut` reference, as in `values[index] *= factor`
inside a function whose `values` parameter has type `&mut [f32; 4]`; that
assignment changes the caller's array
([the decision](../decisions/references.md#d23)).
A literal, calculation, call result, or immutable location is not assignable:

```vortex
// statements: syntax error
let value = 1;
let left = 2;
let right = 3;
10 = value;            // invalid: literal is not a location
(left + right) = 0;    // invalid: calculation is not a location
```

```vortex
// statements: semantic error
let fixed = 1;
fixed = 2;             // invalid: fixed is immutable
```

The parser recognizes the restricted assignment-target grammar. [Semantic
analysis](../specification/glossary.md) (the checks on meaning that run after parsing) checks that
the resolved storage is mutable and the value has the target's
type.

## Compound assignment statements

Compound assignments combine an operation with assignment:

| Statement | Same as |
| --- | --- |
| `total += value;` | `total = total + value;` |
| `total -= value;` | `total = total - value;` |
| `total *= value;` | `total = total * value;` |
| `total /= value;` | `total = total / value;` |
| `index %= size;` | `index = index % size;` |

For example:

```vortex
// statements: valid
let mut total: f32 = 0.0;
total += 2.5;
```

The underlying operator must be valid for the target type. For example,
`name %= 2;` is invalid when `name` is a `String`. The target is evaluated once;
compound assignment is not permission to duplicate side effects. Vortex first
works out which element the target names, then the value on the right, and
only then checks the index and stores the result, so in `values[i] = next();`
the call to `next` runs even when `i` is out of bounds
([decision 38](../decisions/statements.md#d38)).

??? check "In `values[index] = make();`, where `make` prints a message and `index` is out of bounds, does `make` run before Vortex reports the error?"

    Yes. Vortex works out the target's index and the value on the right
    before it checks that the index is in bounds, so `make` runs and prints
    its message, and only then does the program stop.

On integers, a compound assignment has the same checks as its operator:
`total += value` stops the program with a runtime error if the sum overflows,
and `index %= size` does so if `size` is zero
([Expressions 5.5](../specification/expressions.md#checked-integer-operations)).

## Expression statements

An expression can be used as a statement when you only care about its effect.
Function calls that print information are the most common example:

```vortex
// fragment
print("starting calculation");
save_results(values);
```

Any grammatical expression can appear before `;`, but using a pure value and
discarding it has no useful effect. A compiler may warn about it, but the
program is still valid. A call returning `void` is valid only as an expression
statement. It cannot be stored, passed as an argument, or returned, even from
another `void` function ([why](../decisions/operators.md#d44)).

## Blocks and scope

Braces create a block. A variable created inside a block exists only inside that
block:

```vortex
// statements: valid
{
    let temporary = 42;
    print(temporary);
}

// `temporary` cannot be used here.
```

Name resolution opens a scope at `{` and closes it at `}`. A block cannot
declare a name that is already visible from outside it: Vortex has no
shadowing ([decision record](../decisions/names.md#d2)). A block may contain
zero or more statements. Vortex v0.1 does not use a block itself as a value.

## If and else statements

Use `if` to run code only when a condition is `true`. Use `else` when the
condition is `false`:

```vortex
// statements: valid
let score = 72;
if score >= 50 {
    print("passed");
} else {
    print("try again");
}
```

You can chain conditions with `else if`:

```vortex
// statements: valid
let temperature = 21;
if temperature < 0 {
    print("freezing");
} else if temperature > 30 {
    print("hot");
} else {
    print("comfortable");
}
```

In v0.1, `if` is a statement. It chooses which code runs but does not directly
produce a value. Value-producing `if` expressions can be added later.

Every condition must have type `bool`; Vortex does not treat numbers or strings
as truth values. Each branch is a block, while `else if` is an `else` followed
by another `if` statement.

```vortex
// statements: type error
if 1 {
    print("invalid");
}
// invalid: condition is i32, not bool
```

## While loops

A `while` loop repeats as long as its condition stays `true`:

```vortex
// statements: valid
let mut index = 0;

while index < 4 {
    print(index);
    index += 1;
}
```

The condition must be `bool`. The body may execute zero times. A C-style loop
such as `for (let i = 0; i < 4; i += 1)` is not part of Vortex.

## For loops

A `for` loop repeats over a range. It is the normal choice when you know the
set of indexes you want to visit:

```vortex
// statements: valid
for index in 0..4 {
    print(index);
}
```

This loop visits `0`, `1`, `2`, and `3`. The end value is not included. Use
`..=` when you want to include the end value:

```vortex
// statements: valid
for index in 0..=4 {
    print(index);
}
```

For loops are especially important in Vortex because matrix and tensor code is
mostly made from clear, nested loops.

Vortex works out both ends of the range once, before the loop starts, so
changing a variable such as `count` inside `for i in 0..count` does not change
how many times the loop runs ([decision 13](../decisions/statements.md#d13)).
The loop variable has the same integer type as the two ends, and the body
cannot assign to it. A `..` range whose start is not below its end runs zero
times, and a `..=` range that ends at the largest value of its type stops
normally.

??? check "If `count` changes inside the body of `for i in 0..count`, does the loop run a different number of times?"

    No. `0` and `count` are evaluated once, before the loop starts. Changing
    `count` inside the body has no effect on how many iterations remain.

The loop variable is introduced by the loop and is visible only inside its
body. Its name must not match a name that is already visible, so nested loops
need different variable names, such as `row`, `column` and `k`. The expression
after `in` must be a range. Vortex v0.1 cannot loop over an array directly, and
a range cannot be stored in a variable or used anywhere outside a `for` loop
([decision 36](../decisions/statements.md#d36)).
`for index = 0..4` is invalid because the required keyword is `in`.

## Break and continue

Use `break` to leave a loop immediately:

```vortex
// statements: valid
let values = [0; 100];
let target = 7;
for index in 0..100 {
    if values[index] == target {
        break;
    }
}
```

Use `continue` to skip the rest of the current loop iteration:

```vortex
// statements: valid
for value in 0..10 {
    if value % 2 == 0 {
        continue;
    }

    print(value);
}
```

`break` and `continue` are valid only inside the nearest enclosing loop. They
are invalid in an ordinary block or directly inside a function with no loop.
This is a semantic context check rather than a parsing decision.

## Return statements

Use `return` to end a function and provide its result:

```vortex
// items: valid
fn larger(left: i32, right: i32) -> i32 {
    if left > right {
        return left;
    }

    return right;
}
```

A `void` function can use `return;` when it needs to finish early:

```vortex
// items: valid
fn print_positive(value: i32) -> void {
    if value <= 0 {
        return;
    }

    print(value);
}
```

- A non-`void` function must end with a statement that always returns: a
  `return`, or an `if` with an `else` whose branches all end that way. A loop
  never counts, not even `while true`, so write a `return` after it
  ([decision 9](../decisions/statements.md#d9)). Code after a `return` is
  allowed, but it never runs.
- A `void` function may use `return;` but cannot return a value, not even the
  result of another `void` call.
- A non-`void` function cannot use an empty `return;`.

??? check "Does ending a function's body with `while true { ... }` and a `return` inside it satisfy the rule that a non-void function must always return?"

    No. `while` and `for` never count as terminating statements, not even
    `while true`. Vortex looks only at the shape of the statements, so a
    `return` must follow the loop, even though such a loop can never finish
    normally.

Leaving out the value in a function that promises one, returning a value from a
`void` function, and returning a value of the wrong type are all type errors; a
non-`void` function whose body can reach its end is a semantic error
([decision 8](../decisions/statements.md#d8)).

```vortex
// items: type error
fn bad() -> i32 {
    return; // type error: i32 result is missing
}
```

## Statements that can wait

The first version does not need these yet:

- `match` statements and pattern matching;
- `switch` statements;
- `do while` loops;
- exception handling;
- `defer` cleanup statements;
- `unsafe` blocks;
- parallel-loop statements;
- GPU-kernel launch statements.

## Compiler handling summary

<details markdown="1">
<summary>Which compiler stage enforces each rule (optional reading)</summary>

The parser identifies statement boundaries and builds statement [AST](../specification/glossary.md) (abstract syntax tree) nodes.
Name resolution manages block and loop-variable scopes. Type checking validates
conditions, assignments, expressions, and returned values.

Control-flow analysis checks loop-only statements and verifies required return paths. Code
generation emits the selected branches, loop edges, and early exits.

</details>

## Practice and self-check

Identify the invalid lines and explain each failure:

```vortex
// fragment
let value;
let fixed = 1;
fixed += 1;
break;
if 42 { print("answer"); }
```

Answers:

1. A local declaration requires an initializer.
2. `let fixed = 1;` is valid.
3. `fixed` is immutable.
4. `break` is outside a loop.
5. An `if` condition must be `bool`.

## Key ideas

!!! recap

    - **When does a statement take a trailing semicolon?** Simple statements
      (declarations, assignments, expression statements, `return`, `break`
      and `continue`) always end with `;`. A statement that ends with a
      block, such as `if`, `while` or `for`, takes no semicolon after the
      closing brace.
    - **What makes an assignment target valid?** Its root must be a variable
      declared with `let mut`, or a name of type `&mut T`. A parameter, a
      `for` loop variable, a `let` binding without `mut`, a literal, and a
      calculation result are never assignable.
    - **In `place = value;`, what runs first: the value or the target's
      indices?** The target's base and indices, left to right, then the
      value on the right, then the bounds check of each index, then the
      store.
    - **Does changing a variable used in a `for` loop's range change how many
      times the loop runs?** No. Both endpoints are evaluated once, before
      the loop starts.
    - **Does a `while true` loop at the end of a function body count as a
      guaranteed return path?** No. `while` and `for` never terminate, so a
      `return` must follow the loop even when the loop can never finish
      normally.
    - **Where can a call to a `void` function appear?** Only as the whole
      expression of an expression statement, optionally in parentheses;
      never stored, passed as an argument, or returned.
    - **Can two nested `for` loops reuse the same loop-variable name?** No.
      Vortex has no shadowing, so nested loops need distinct names, such as
      `row` and `column`.

## Where this comes back

--8<-- "includes/next/language-tour__09-statements.md"
