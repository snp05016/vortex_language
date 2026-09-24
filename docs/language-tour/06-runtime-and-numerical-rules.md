# Runtime and numerical rules

## Learning goals

After this chapter, you should be able to distinguish compile-time errors from
runtime checks and explain Vortex's default integer and floating-point behavior.

## Checked operations

Vortex v0.1 uses checked behavior for common mistakes. A runtime error stops the
program at once. Everything the program printed before the error still appears.
Then one line on standard error (the output stream kept for error messages)
names the failed check and where it happened, such as
`runtime error[bounds]: ... at main.vx:3:12`, and the program ends with exit
status 101 (the number a program hands back when it finishes) instead of the
usual 0
([Diagnostics 10.6](../specification/diagnostics.md#106-runtime-reporting),
[decision](../decisions/program.md#d14)).

- Accessing an array outside its bounds is a runtime error.
- Dividing an integer by zero, or taking its remainder by zero, is a runtime
  error. Integer division rounds toward zero (`-7 / 2` is -3), and a remainder
  has the sign of the number divided (`-7 % 2` is -1).
- Integer overflow is a runtime error. This covers `+`, `-`, `*`, negation, `/`
  and the compound assignments such as `+=`; subtracting 1 from a `u32` that
  holds 0, for example, stops the program. This includes dividing the smallest
  `i32`, -2147483648, by -1; the remainder of that division is 0
  ([why](../decisions/operators.md#d33)).
- Shifting by a negative count, or by at least the number of bits in the
  shifted value's type (32 for `i32`), is a runtime error. Bits that `<<`
  pushes out are dropped, which is not an error
  ([why](../decisions/operators.md#d22)).
- A cast whose value does not fit the destination type is a runtime error, and
  so is casting a floating-point NaN (not a number) or infinity to an integer
  type ([cast rules](../decisions/numbers.md#d27)).
- When the values that decide a check are written as integer literals, such as
  `10 / 0`, `values[3]` or `u32(-1)`, the compiler checks the operation while
  compiling and reports a constant-evaluation error. A value that comes from a
  variable, a parameter or a call is checked while the program runs, even when
  you can work it out by reading the code
  ([record 39](../decisions/diagnostics.md#d39)).

The [expressions chapter](../specification/expressions.md#checked-integer-operations)
lists every checked operation, and [record 34](../decisions/diagnostics.md#d34)
explains the list.

## Allowed examples

```vortex
// statements: valid
let values = [10, 20, 30];
let index: usize = 1;
let selected = values[index];

let numerator = 12;
let denominator = 3;
let result = numerator / denominator;
```

These operations are valid. `index` and `denominator` are variables, and a
variable is never a constant expression, not even an immutable one, so both
operations keep their runtime checks; here both checks pass.

## Rejected or checked examples

```vortex
// statements: constant-evaluation error
let values = [10, 20, 30];
let selected = values[3];
// constant-evaluation error: index 3 is past the end of a three-element array
```

```vortex
// items: valid
fn divide(value: i32, divisor: i32) -> i32 {
    return value / divisor;
    // valid source, but execution fails if divisor is zero
}
```

```vortex
// statements: constant-evaluation error
let impossible = u32(-1);
// constant-evaluation error: -1 does not fit in u32
```

Unchecked overflow, silent wrapping, saturating casts, and a user-controlled
`unsafe` escape hatch are not part of v0.1.

## Floating-point behavior

`f32` and `f64` follow IEEE 754, the standard for floating-point arithmetic.
Each operation rounds its result once, to the nearest value its type can hold.
The compiler must not reorder operations, merge a multiplication and an
addition into one fused step, or compute with extra precision, because each of
those can change the last digits of a result. The same program therefore
prints the same digits with every conforming compiler. Faster relaxed modes may
be added later as an explicit opt-in.
([The rule](../specification/types-and-values.md#44-floating-point-values),
[why](../decisions/numbers.md#d56).)

Dividing a floating-point number by zero is not an error. It produces a special
value: a nonzero number divided by zero gives infinity (`inf` or `-inf`), and
`0.0 / 0.0` gives NaN, short for not a number. NaN is not equal to anything,
itself included. `print` shows these values as `inf`, `-inf` and `NaN`. The one
operation that rejects them is a cast to an integer type, which stops the
program with a runtime error. ([Why](../decisions/numbers.md#d24).)

Floating-point results are not exact decimal arithmetic. Programs should not
assume that every decimal calculation can be represented perfectly. Vortex v0.1 also
does not provide a fast-math flag, implicit integer-to-float conversion, or a
special decimal-money type.

## Compiler handling

<details markdown="1">
<summary>Which compiler stage enforces each rule (optional reading)</summary>

Type checking (the [compiler stage](../compiler/guide/index.md) that checks every value's type)
first verifies that an operation is defined for its operand types. Constant
evaluation checks every operation whose deciding values are integer literals
and reports failures during compilation. For every other operation, code
generation emits the overflow, division, shift, cast and bounds checks that the
specification requires. An optimizer may remove a check only after proving the
operation safe.

</details>

## Practice and self-check

For each case, decide whether the compiler can reject it immediately or must
keep a runtime check:

```vortex
// fragment
let values = [1, 2, 3, 4];
let a = 10 / 0;
let b = values[100];
```

```vortex
// fragment
fn read(values: &[i32; 4], index: usize) -> i32 {
    return values[index];
}
```

The lines that declare `a` and `b` are constant-evaluation errors: `0` is a
constant divisor, and `100` is a constant index past the end of the
four-element array, whose length is part of its type
([decision](../decisions/arrays.md#d12)). The function compiles and keeps a
runtime bounds check, because `index` is a parameter.
