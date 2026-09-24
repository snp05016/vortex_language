# 9. Runtime safety

<p class="page-intro">Some mistakes depend on values that exist only while the program runs. This stage adds the checks that catch those mistakes at the moment they happen and stop the program with a clear message.</p>

The front end already rejects a great many bad programs. It cannot reject all
of them, because some mistakes depend on numbers nobody knows until the
program runs. A function that divides by its argument is fine when the
argument is 3 and wrong when it is 0. An index that comes from a loop, a
calculation or a caller may be inside the array or outside it.

Vortex makes a strong promise about those cases. The
[conformance chapter](../../specification/conformance.md#15-undefined-behavior)
says v0.1 "does not intentionally expose undefined behavior" to a well-formed
program. Every invalid operation must either be rejected before the program
runs or stop the program through a documented runtime error. Stage 9 is where
that second half of the promise is kept.

The work happens on the right-hand slope of the
[compiler mountain](index.md#the-shape-of-the-whole-thing): in the code the
compiler generates, and in the small runtime library that reports the failure.

## What this stage is for

The roadmap's [Milestone 9](../../roadmap.md#milestone-9-runtime-safety) has
eight items: check array bounds unless an access is proven safe, detect
integer division and remainder by zero, detect integer overflow, detect
invalid shift counts, detect invalid numeric casts, stop the program with a
clear runtime error when a check fails, stop it with a `stack` report when it
runs out of stack, and test every failure and every successful boundary case.

The milestone is complete "when the runtime behavior matches the
specification's runtime rules": the required checks in
[Diagnostics](../../specification/diagnostics.md#runtime-error) and the rules
in [Runtime reporting](../../specification/diagnostics.md#106-runtime-reporting)
([decision](../../decisions/documentation.md#d49)). The [tour's chapter on
runtime and numerical rules](../../language-tour/06-runtime-and-numerical-rules.md)
explains the same rules with examples, so read it before this page, and again
after.

## Words for this stage

compile time
: While the compiler is working on the source, before any executable exists.

run time
: While the finished executable is running.

static rule
: A rule the compiler can check by reading the source, such as a type or a
  mutability rule.

dynamic rule
: A rule about values that exist only at run time, such as "this index is
  inside the array".

runtime check
: A small test the compiler places in the generated code, just before an
  operation that might be invalid, to decide whether it is safe to go ahead.

runtime error
: What happens when a runtime check fails. The program stops and reports what
  went wrong. The diagnostics chapter lists it as its own category.

bounds check
: A runtime check that an array index is at least zero and less than the size
  of its dimension.

integer overflow
: An integer calculation whose true answer does not fit in its type. For
  `i32`, anything above 2,147,483,647 or below -2,147,483,648.

wrapping
: Handling overflow by silently throwing away the high bits, so a number that
  is too big comes back as a small or negative one. Vortex v0.1 does not do
  this.

saturating
: Handling overflow by clamping to the largest or smallest value. Vortex v0.1
  does not do this either.

cast
: An explicit conversion from one numeric type to another, written as the
  target type's keyword and one value in parentheses: `f32(count)`,
  `i32(temperature)`, `u32(value)`. It looks like a call but is not one
  ([decision](../../decisions/numbers.md#d1)).

undefined behavior
: A situation where the rules of a language or machine say nothing at all
  about what happens next. The program may crash, give a wrong answer, or
  appear to work. Vortex v0.1 forbids exposing it.

proven safe
: Known by the compiler, from the source alone, to be unable to fail. Only a
  proof allows a check to be left out.

exit status
: The number a finished program hands back to whatever started it. By common
  convention zero means success. A Vortex program exits with 0 when `main`
  returns and with 101 after a runtime error.

IEEE 754
: The international standard for floating-point arithmetic. Vortex's `f32`
  and `f64` follow it.

NaN
: Short for "not a number". A special IEEE 754 value produced by operations
  such as zero divided by zero.

infinity
: A special IEEE 754 value produced, for example, by dividing a nonzero number
  by zero.

## One rule, two moments

The diagnostics chapter makes a point worth reading twice. An index that is an
integer constant expression "is checked during compilation", while any other
index "is checked at run time". "Both violate the same bounds rule"
([10.3](../../specification/diagnostics.md#103-error-phase-versus-category)).
The rule is the same. What changes is the moment it is checked.

For every operation that could fail, the operands decide when it is checked,
not the compiler's cleverness ([record 39](../../decisions/diagnostics.md#d39)):

1. Every operand that decides the check is an integer constant expression,
   built only from integer literals. The check is then made while compiling,
   in [stage 5](stage-5-types-and-rules.md), and a failure is a
   constant-evaluation error. `values[3]` on a three-element array and
   `u32(-1)` are examples.
2. Otherwise the generated code must keep the check. Even if the compiler can
   prove the operation will fail, it must accept the program; it may warn.
3. The compiler may leave a check out only when it proves the operation will
   succeed.

The second case is the normal one, and the one this stage is about.
Figure 1 shows four kinds of failure against the two moments; an invalid shift
count behaves the same way.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-labelledby="s9-when-title s9-when-desc">
<title id="s9-when-title">Which failures are caught while compiling and which at run time</title>
<desc id="s9-when-desc">A table with four rows: out-of-bounds index, integer division by zero, integer overflow and invalid cast. For each, the middle column shows an example whose deciding operands are constant, which is a constant-evaluation error, and the right column shows an example whose operands are variables, which keeps a runtime check. The rows light up one after another.</desc>
<rect class="vx-box-strong" x="20" y="20" width="200" height="44"/>
<rect class="vx-box-strong" x="220" y="20" width="260" height="44"/>
<rect class="vx-box-strong" x="480" y="20" width="260" height="44"/>
<text class="vx-text" x="34" y="47">Failure</text>
<text class="vx-text" x="234" y="47">Operands are constants</text>
<text class="vx-text" x="494" y="47">Operands are variables</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 4">
<rect class="vx-box" x="20" y="64" width="200" height="62"/>
<rect class="vx-box" x="220" y="64" width="260" height="62"/>
<rect class="vx-box" x="480" y="64" width="260" height="62"/>
<text class="vx-text" x="34" y="100">Index out of bounds</text>
<text class="vx-mono" x="234" y="90">values[3]</text>
<text class="vx-text-accent" x="234" y="112">constant-evaluation error</text>
<text class="vx-mono" x="494" y="90">values[index]</text>
<text class="vx-text-muted" x="494" y="112">bounds check stays in the code</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4">
<rect class="vx-box" x="20" y="126" width="200" height="62"/>
<rect class="vx-box" x="220" y="126" width="260" height="62"/>
<rect class="vx-box" x="480" y="126" width="260" height="62"/>
<text class="vx-text" x="34" y="162">Division by zero</text>
<text class="vx-mono" x="234" y="152">10 / 0</text>
<text class="vx-text-accent" x="234" y="174">constant-evaluation error</text>
<text class="vx-mono" x="494" y="152">value / divisor</text>
<text class="vx-text-muted" x="494" y="174">zero check stays in the code</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4">
<rect class="vx-box" x="20" y="188" width="200" height="62"/>
<rect class="vx-box" x="220" y="188" width="260" height="62"/>
<rect class="vx-box" x="480" y="188" width="260" height="62"/>
<text class="vx-text" x="34" y="224">Integer overflow</text>
<text class="vx-mono" x="234" y="214">2147483647 + 1</text>
<text class="vx-text-accent" x="234" y="236">constant-evaluation error</text>
<text class="vx-mono" x="494" y="214">left + right</text>
<text class="vx-text-muted" x="494" y="236">overflow check stays in the code</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 4">
<rect class="vx-box" x="20" y="250" width="200" height="62"/>
<rect class="vx-box" x="220" y="250" width="260" height="62"/>
<rect class="vx-box" x="480" y="250" width="260" height="62"/>
<text class="vx-text" x="34" y="286">Invalid cast</text>
<text class="vx-mono" x="234" y="276">u32(-1)</text>
<text class="vx-text-accent" x="234" y="298">constant-evaluation error</text>
<text class="vx-mono" x="494" y="276">u32(offset)</text>
<text class="vx-text-muted" x="494" y="298">range check stays in the code</text>
</g>
</svg>
<figcaption>Figure 1. Four of the failures Milestone 9 names, each at the two moments it can be found. In the middle column every operand that decides the check is an integer constant expression, so the compiler must check it and report a constant-evaluation error. In the right column the operands are variables or parameters, so the check runs when the program runs, even where a compiler could work out the answer. The <code>values</code> array is assumed to hold three elements.</figcaption>
</figure>

The middle column is not new work for this stage: checking constant operands
is the job of constant evaluation in [stage 5](stage-5-types-and-rules.md).
The right column is new.

## The checks Vortex requires

The [expressions chapter](../../specification/expressions.md#checked-integer-operations)
lists every checked integer operation, one row each: `+`, `-`, `*`, negation,
`/`, `%`, the compound assignments, casts to integer types, and shift counts.
The [arrays chapter](../../specification/arrays.md#76-indexing) adds a bounds
check on every index. The tour adds what is *not* allowed instead: "Unchecked
overflow, silent wrapping, saturating casts, and a user-controlled `unsafe`
escape hatch are not part of v0.1."

Each check runs when the operation it guards runs, after that operation's
operands have been evaluated; for an assignment target, that is immediately
before the store, after the assigned value
([decision 38](../../decisions/statements.md#d38)).

### Array bounds

Every index expression that cannot be proven safe needs a **bounds check**. The
check must hold in every dimension separately. For a `[f32; 2, 3]`, the row
index must be below 2 and the column index below 3.

<figure class="vx-figure">
<svg viewBox="0 0 760 320" role="img" aria-labelledby="s9-guard-title s9-guard-desc">
<title id="s9-guard-title">A bounds check standing guard in front of an array read</title>
<desc id="s9-guard-desc">An index read, values[index], on a four-element array flows into a check that asks whether index is less than 4. The yes path leads up to reading the element. The no path leads down to a dashed box that stops the program with a runtime error. One dot carries index 2 along the yes path; another carries index 5 along the no path.</desc>
<rect class="vx-box-strong" x="30" y="130" width="160" height="60"/>
<text class="vx-mono" x="110" y="157" text-anchor="middle">values[index]</text>
<text class="vx-text-muted" x="110" y="177" text-anchor="middle">values: [i32; 4]</text>
<line class="vx-line" x1="190" y1="160" x2="242" y2="160"/>
<polygon class="vx-arrowhead" points="250,160 238,154 238,166"/>
<rect class="vx-box-accent vx-pulse" x="250" y="130" width="180" height="60"/>
<text class="vx-text" x="340" y="157" text-anchor="middle">Is index below 4?</text>
<text class="vx-text-muted" x="340" y="177" text-anchor="middle">the bounds check</text>
<path class="vx-line" d="M430 160 L470 160 L470 70 L502 70"/>
<polygon class="vx-arrowhead" points="510,70 498,64 498,76"/>
<path class="vx-line" d="M470 160 L470 255 L502 255"/>
<polygon class="vx-arrowhead" points="510,255 498,249 498,261"/>
<text class="vx-text-muted" x="480" y="112">yes</text>
<text class="vx-text-muted" x="480" y="216">no</text>
<rect class="vx-box" x="510" y="40" width="220" height="60"/>
<text class="vx-text" x="620" y="67" text-anchor="middle">Read the element</text>
<text class="vx-text-muted" x="620" y="87" text-anchor="middle">index 2: the program carries on</text>
<rect class="vx-box-bad" x="510" y="220" width="220" height="70"/>
<text class="vx-text" x="620" y="248" text-anchor="middle">Stop with a runtime error</text>
<text class="vx-text-muted" x="620" y="270" text-anchor="middle">index 5: out of bounds for 4</text>
<circle class="vx-dot" r="6">
<animateMotion dur="6s" repeatCount="indefinite" path="M190 160 L430 160 L470 160 L470 70 L510 70" keyPoints="0;0;1;1" keyTimes="0;0.1;0.45;1" calcMode="linear"/>
</circle>
<circle class="vx-dot" r="6">
<animateMotion dur="6s" repeatCount="indefinite" path="M190 160 L430 160 L470 160 L470 255 L510 255" keyPoints="0;0;0;1;1" keyTimes="0;0.5;0.55;0.9;1" calcMode="linear"/>
</circle>
</svg>
<figcaption>Figure 2. A bounds check guarding one array read. The check runs first. Only if it passes does the read happen. The two dots are two runs of the same code: with index 2 the read goes ahead, and with index 5 the program stops before touching memory it does not own.</figcaption>
</figure>

The index types matter too. Any integer type may index (`i32`, `u32` or
`usize`), and an index is in bounds when `0 <= index < extent`, compared as
mathematical integers ([decision](../../decisions/arrays.md#d12)). A negative
`i32` index is as out of bounds as one that is too large, and the check must
catch both. Constant indexes never reach this check: stage 5 has already
rejected the ones outside their extent.

### Integer division and remainder by zero

`/` and `%` on integers need a check that the right-hand side is not zero.
The tour's example is short:

```vortex
// items: valid
fn divide(value: i32, divisor: i32) -> i32 {
    return value / divisor;
    // valid source, but execution fails if divisor is zero
}
```

There is a second failure hiding in signed division. The smallest `i32`,
-2,147,483,648, divided by -1 should give 2,147,483,648, which does not fit in
an `i32`. That is an overflow, and Vortex requires overflow to be caught, so
signed division needs both checks.

The remainder is different. The
[division decision](../../decisions/operators.md#d33) makes
`-2147483648 % -1` equal 0, its true value, so `%` fails only for a zero
divisor. A divide instruction that computes quotient and remainder together
can fault on this case, so the generated code must produce the 0 without that
fault. The same decision fixes the rounding: `/` truncates toward zero and `%`
takes the sign of the left operand.

### Integer overflow

Every integer operation whose result can fall outside its type needs an
overflow check: addition, subtraction, multiplication, negation, and division
as just described. The compound assignments `+=`, `-=`, `*=`, `/=` and `%=`
are the same operations and need the same checks.

Unsigned types overflow in the downward direction as well. In `u32` or
`usize`, `0 - 1` has no answer, because the true result is negative. Under
Vortex rules that is a runtime error, not a very large number.

### Shift counts

A shift needs a check as well. The count must be at least 0 and less than the
bit width of the value being shifted, so `value << count` fails for an `i32`
when `count` is negative or 32 or more. A constant count was already checked in
stage 5. Bits that `<<` pushes out of the top are dropped; that is not
overflow, so a left shift needs no other check. `>>` must copy the sign bit for
`i32` and shift in zeros for `u32` and `usize`. The rule is in
[Expressions 5.5](../../specification/expressions.md#checked-integer-operations);
records [22](../../decisions/operators.md#d22) and
[34](../../decisions/diagnostics.md#d34) give the reasons.

### Invalid numeric casts

A cast converts a value to another numeric type. The
[types chapter](../../specification/types-and-values.md#412-casts-and-conversions)
fixes the full table ([decision](../../decisions/numbers.md#d27)). Two kinds
of cast can fail at run time, and each needs a check before the conversion
instruction:

- an integer-to-integer cast whose value does not fit the target, such as a
  negative `i32` cast to `u32`, or a `usize` above `2^32 - 1` cast to `u32`;
- a floating-point-to-integer cast, which truncates toward zero and fails when
  the value is NaN, an infinity, or out of the target's range after
  truncation.

Integer-to-float and `f64`-to-`f32` casts never fail: they round to nearest,
ties to even, and an `f64` too large for `f32` becomes an infinity. A cast to
the same type does nothing.

```vortex
// statements: valid
let temperature: f32 = 21.8;
let whole_degrees = i32(temperature); // 21, rounded toward zero
```

```vortex
// statements: constant-evaluation error
let impossible = u32(-1); // constant-evaluation error: -1 does not fit in u32
```

The second program never reaches this stage's checks: its operand is a
constant expression, so [stage 5](stage-5-types-and-rules.md) evaluates the
cast and reports the failure.

### Running out of stack

Every call takes a frame on the call stack, and the stack has a fixed size.
Recursion that goes too deep, or one call whose local arrays are larger than
the stack, runs out of it. The
[conformance chapter](../../specification/conformance.md#15-undefined-behavior)
requires the program to stop at that point, reported like a failed check with
the kind `stack`, and never to carry on or die silently. How you detect it is
your choice. Document the stack size a compiled program gets
([record 46](../../decisions/diagnostics.md#d46)).

## Stopping with a clear runtime error

When a check fails, the program must stop. No further statement runs, and a
Vortex program cannot catch the failure, because v0.1 has no exception
handling. Four details of the stop are visible to a test: the layout of the
message, where it is written, the exit status, and whether output printed
before the failure appears.

[Diagnostics 10.6](../../specification/diagnostics.md#106-runtime-reporting)
settles all four ([decision](../../decisions/program.md#d14)). The program
first writes out everything it has already printed, then writes one line to
standard error in the form
`runtime error[<kind>]: <message> at <file>:<line>:<column>`, then exits with
status 101. The kind names the failed check (`bounds`, `divide-by-zero`,
`overflow`, `shift`, `cast` or `stack`), and the position is where the failing
operation's span starts, so every check must know its operation's source
position. Only the message text is yours to choose;
[I7](../../decisions/implementation.md#i7) suggests a short lowercase phrase
that names the values involved, such as
`index 3 is out of bounds for extent 3`.

The first step is the one most often overlooked. A test that prints three
lines and then
divides by zero should see the three lines and then the error. If the output
was sitting in a buffer when the program stopped, the test sees only the
error, and the person debugging sees a program that seems to have died on its
first line.

There is also a choice about where the checking code lives. Ghuloum describes
two approaches in his incremental Scheme compiler: insert an explicit check
in the generated code at every primitive operation, or have the generated code
call safe versions of those operations in the runtime, which do the checks
themselves. He judges the second slower but simpler and less likely to
contain mistakes.[^ghuloum] Either satisfies Vortex. The spec cares that the
check happens and the report is clear, not where the checking code sits.
[I8](../../decisions/implementation.md#i8) suggests the first: a comparison
and a branch before each operation, with every failure calling one reporting
function in the runtime, the only code that writes the line and exits.

## When a check can be skipped

The tour's rule is short: "An optimizer may remove a check only after proving
the operation safe." Nothing weaker than a proof will do. Here is a loop where
the proof is easy:

```vortex
// items: valid
fn total(values: &[f32; 4]) -> f32 {
    let mut sum: f32 = 0.0;
    for index in 0..4 {
        sum += values[index];
    }
    return sum;
}
```

The loop variable only ever takes the values 0, 1, 2 and 3, and the array has
four elements, so every access is in bounds. A compiler that works this out
may drop the check. A compiler that does not bother is still correct.

That second point matters for v0.1. The roadmap puts optimization after the
first release, so the simplest honest plan is to keep every check. Proving
things safe is optimization work, and it can come later without changing any
program's meaning.

Do not assume your backend will supply safety for you. If you use LLVM, its
tutorial states plainly that "LLVM IR does not itself guarantee
safety".[^kal10] The LLVM reference says its plain signed division by zero is
undefined behavior, as is the overflow case of the smallest value divided by
-1, and that its array type does not stop an index from running past the end
of the array.[^langref] Every check Vortex needs
must be put into the generated code on purpose, before the operation it
guards.

The opposite shortcut is not allowed. A compiler that proves a check on
non-constant operands will fail must still accept the program and keep the
check; it may print a warning.

## Floating point

Floating-point numbers are not on the list of runtime checks. The
[types chapter](../../specification/types-and-values.md#44-floating-point-values)
requires every `f32` and `f64` operation to be one IEEE 754 operation rounded
to nearest, ties to even, with no fused multiply-add, no reordering, no wider
intermediate results and no flush-to-zero
([decision](../../decisions/numbers.md#d56)). The tour adds that there is no
fast-math flag and that floating-point results "are not exact decimal
arithmetic".

The no-reordering rule has a real reason behind it. Because every
floating-point result is rounded, the ordinary laws of algebra do not always
hold: David Goldberg shows that `(x + y) + z` and `x + (y + z)` can give quite
different answers.[^goldberg] A compiler that regroups a sum to save time can
therefore change what a program prints.

What about dividing a floating-point number by zero? It is not an error. The
[types chapter](../../specification/types-and-values.md#44-floating-point-values)
follows IEEE 754 exactly: a nonzero number divided by zero gives an infinity
and zero divided by zero gives NaN, as Goldberg explains.[^goldberg] Nothing
needs a check there. The special values matter in two other places: a cast of
NaN or an infinity to an integer type is a runtime error (see
[Invalid numeric casts](#invalid-numeric-casts)), and `print` writes them as
`NaN`, `inf` and `-inf`
([Programs and declarations 3.9](../../specification/declarations.md#39-built-in-functions)).
Records [24](../../decisions/numbers.md#d24) and
[4](../../decisions/program.md#d4) explain both rules. Goldberg's paper is
long, but its opening sections on rounding and its section on NaN and infinity
are worth an evening.

## What you need to have

<div class="vx-split" markdown="1">
<div markdown="1">

#### Need to have

- A bounds check on every index that is not proven safe, in every dimension: required by Milestone 9 and the arrays chapter.
- Division and remainder checks for zero, an overflow check for the smallest signed value divided by -1, and a result of 0, not an error, for that value `%` -1.
- Overflow checks on integer addition, subtraction, multiplication and negation, including compound assignment and unsigned subtraction.
- A check on every shift count that is not a constant, on `<<` and `>>`: at least 0 and below the bit width of the shifted type, as Expressions 5.5 requires.
- Range checks on every cast whose value can fall outside its destination type.
- The runtime error line from Diagnostics 10.6, with the failed check's kind and the failing operation's source position.
- Exit status 101 for a program stopped by a runtime error.
- Stack exhaustion that stops the program with the `stack` report, never a silent crash.

</div>
<div class="vx-not-yet" markdown="1">

#### Not yet

- Removing checks by proof: optimization comes after v0.1, and keeping every check is correct.
- Any way for a program to catch or recover from a runtime error: v0.1 has no exception handling.
- Wrapping or saturating arithmetic: the tour says explicit forms "may be added later".
- Fast or relaxed floating-point modes: the specification allows them only in a later version, as an explicit opt-in.
- Runtime checks for references: every v0.1 reference rule is static and exact, so no reference needs a runtime check ([decision](../../decisions/references.md#d41)).
- Stack traces or detailed crash reports: one line with a kind and a position is the requirement.

</div>
</div>

## What you do not need yet

The right-hand column is short on purpose. Almost everything in this stage
is required, because the conformance chapter leaves no room for "we will make
it safe later". The only thing that truly waits is making the checks cheaper.

## How you know it is finished

[Milestone 9](../../roadmap.md#milestone-9-runtime-safety) ends with "Test
each failure and each successful boundary case." A **boundary case** is the
value right at the edge: the last one that works and the first one that
fails. For each check, you want a compiled program on both sides of the edge:

- Index 0 and the last index of every dimension succeed; one past the last
  index fails, and so does a negative `i32` index.
- A two-dimensional access with a valid row and an out-of-range column fails,
  even though the flattened position would still be inside the array.
- Division by 1 and by -1 succeed on ordinary values; division by zero fails;
  the smallest `i32` divided by -1 fails; the smallest `i32` `%` -1 gives 0;
  `-7 / 2` gives -3 and `-7 % 2` gives -1.
- `2147483646 + 1` succeeds at run time and `2147483647 + 1` fails, with the
  values held in variables or parameters; written as literals, the failing sum
  is a constant-evaluation error instead.
- `0 - 1` in `u32` fails, with both values in variables.
- `+=` and the other compound assignments fail at the same boundaries as their
  operators.
- A shift by the bit width minus one succeeds, so `1 << 31` works in `i32` and
  `u32`; a shift by the bit width fails, and so does a negative count, with the
  count held in a variable; `-8 >> 1` gives -4.
- The largest value that fits a cast's destination succeeds; the next one
  fails.
- A NaN or an infinity cast to an integer type fails, while an `f64` too large
  for `f32` becomes an infinity and does not fail.
- A function that calls itself with no way out stops with the `stack` report
  and the runtime-error exit status.
- Every failing program writes exactly one line to standard error, with the
  right kind and, except after stack exhaustion, the right source position,
  and exits with status 101.
- Output printed before the failure appears before the error message.
- In `values[i] = next();` with `i` out of bounds, `next` runs, and its output
  appears, before the program stops.
- The same failures written with integer constant operands are
  constant-evaluation errors ([record 39](../../decisions/diagnostics.md#d39)).

When the behavior of every one of these matches the specification's runtime
rules, the milestone is done. The
[tour chapter](../../language-tour/06-runtime-and-numerical-rules.md) shows
the same rules with examples.

## Traps

**Checking only the flattened position.** In a `[f32; 2, 3]`, the access
`a[0, 5]` would land on the sixth cell, which exists. It is still out of
bounds, because column 5 does not exist. Check each index against its own
dimension.

**Forgetting the smallest value divided by -1.** It is the one division that
overflows, it has a nonzero divisor, and a zero check will not catch it. Its
remainder, `% -1`, must still give 0.

**Forgetting that unsigned numbers can go below zero.** `u32` and `usize`
subtraction is the most common place for this. A size minus one, when the
size is zero, must fail rather than come out enormous.

**Letting the compiler itself overflow.** The constant evaluator calculates
values like `2147483647 + 1` while compiling. If it does that arithmetic
carelessly in the implementation language, the compiler may produce a wrong
constant, or crash, instead of reporting a clean constant-evaluation error.

**Trusting the backend to trap.** Some processors stop on integer division by
zero and some do not. Some backends treat these cases as undefined, which lets
them assume the case never happens. Write the check yourself.

**Skipping a check because it looks obvious.** If the proof is not written
into the compiler, the check stays. "Nobody would index past the end here" is
not a proof.

**Losing the location.** A runtime error that says only "overflow" makes the
programmer search the whole program. The runtime error line must name the
position, so carry the source location of each checked operation into the
report.

## How others teach this stage

**Crafting Interpreters.** The chapter "Evaluating Expressions" defines
runtime errors as "failures that the language semantics demand we detect and
report while the program is running".[^ci-eval] In Nystrom's language most
runtime errors are type errors, such as subtracting a string, because types
are only known at run time. In Vortex every type error is caught while
compiling, so the runtime errors that remain are about values, not types.
Nystrom's interpreter reports an error and keeps its interactive session
alive; a compiled Vortex program stops. The chapter also leaves division by
zero as an exercise for the reader, which is a good prompt to think through
the floating-point section above.

**Ghuloum.** Section 3.16, "Error Checking and Safe Primitives", explains why
checks matter at the machine level: an out-of-range vector write quietly
damages other parts of the running system, producing bugs far from their
cause.[^ghuloum] It then describes the two placements of checking code
mentioned above. The Vortex runtime is much smaller than a Scheme system's,
but the argument is the same.

**Kaleidoscope.** The final chapter of the LLVM tutorial lists unsafe things
LLVM IR permits, buffer overruns among them, and says safety has to be built
as a separate layer on top of LLVM.[^kal10] For Vortex, that layer is this
stage.

**The LLVM Language Reference.** If you pick LLVM, read the entries for its
integer division, addition and float-to-integer conversion instructions. They
spell out exactly which inputs are undefined or produce an unusable result,
which is a precise list of the cases Vortex must check before the
instruction runs.[^langref] The reference also describes a family of
arithmetic operations that report whether an overflow occurred, one
well-known way to build overflow checks.[^langref]

**Goldberg.** "What Every Computer Scientist Should Know About Floating-Point
Arithmetic" is the standard background reading on rounding, IEEE 754 special
values, and why floating-point algebra is not school algebra.[^goldberg] Read
it before you write any test that compares floating-point results, which is
exactly what [stage 10](stage-10-matrix-multiplication.md) asks for.

[^ci-eval]: Robert Nystrom, *Crafting Interpreters*, chapter "Evaluating Expressions". <https://craftinginterpreters.com/evaluating-expressions.html>
[^ghuloum]: Abdulaziz Ghuloum, "An Incremental Approach to Compiler Construction", *Proceedings of the 2006 Scheme and Functional Programming Workshop*, University of Chicago Technical Report TR-2006-06, section 3.16, "Error Checking and Safe Primitives". <http://scheme2006.cs.uchicago.edu/11-ghuloum.pdf>
[^kal10]: LLVM Project, "My First Language Frontend with LLVM Tutorial", chapter 10, "Conclusion and other useful LLVM tidbits". <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl10.html>
[^langref]: LLVM Project, *LLVM Language Reference Manual*, sections on the "sdiv", "add" and "fptosi" instructions, the "Array Type", and the "llvm.sadd.with.overflow" intrinsics. <https://llvm.org/docs/LangRef.html>
[^goldberg]: David Goldberg, "What Every Computer Scientist Should Know About Floating-Point Arithmetic", *ACM Computing Surveys* 23(1), 1991. <https://doi.org/10.1145/103162.103163>
