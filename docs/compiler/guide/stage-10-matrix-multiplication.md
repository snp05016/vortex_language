# 10. Matrix multiplication

<p class="page-intro">The first real Vortex program. Everything built so far comes together in one small numerical routine that must compile, run, and give exactly the right answer.</p>

Stages 0 to 9 each added one kind of ability. This stage adds no new language
feature at all. Instead it asks the compiler to handle a complete program of
the kind Vortex was designed for, using functions, nested loops, fixed-size
two-dimensional arrays, references and floating-point arithmetic, all at once.

The [philosophy page](../../philosophy.md) calls matrix multiplication "the
first flagship workload", small enough to understand completely but rich
enough to exercise the whole compiler. It is also the program the roadmap's
[goal](../../roadmap.md#goal) names as the final demonstration for v0.1. When
it works, the whole of the [compiler mountain](index.md#the-shape-of-the-whole-thing),
up one side and down the other, has carried a real program.

## What this stage is for

The roadmap's
[Milestone 10](../../roadmap.md#milestone-10-matrix-multiplication) asks for
matrix multiplication written with ordinary Vortex functions and loops, using
fixed-size multidimensional `f32` arrays, with inputs passed through shared
references and the output through a mutable reference. The result must be
compared with a known correct answer, square and rectangular shapes must both
be tested, and shape or type mistakes must be reported during compilation.

The milestone is complete "when Vortex compiles and correctly runs the matrix
multiplication example on the CPU without optimization". The last two words
are part of the requirement, not an apology. Speed is explicitly not the goal
yet. The section [Why speed can wait](#why-speed-can-wait) explains why.

## Words for this stage

matrix
: A rectangle of numbers arranged in rows and columns. In Vortex v0.1 a
  matrix is simply a two-dimensional array such as `[f32; 2, 3]`.

row
: One horizontal line of a matrix. A 2 by 3 matrix has 2 rows.

column
: One vertical line of a matrix. A 2 by 3 matrix has 3 columns.

square matrix
: A matrix with as many rows as columns, such as 2 by 2 or 4 by 4.

rectangular matrix
: A matrix whose number of rows and number of columns differ, such as 2 by 3.

matrix multiplication
: An operation that combines a matrix `A` and a matrix `B` into a new matrix
  `C`, where each number in `C` is built from one row of `A` and one column of
  `B`.

dot product
: Multiply two equally long lists of numbers position by position and add up
  the results. Each cell of a matrix product is one dot product.

inner dimension
: The size that must match for two matrices to be multiplied: the number of
  columns of `A` and the number of rows of `B`.

known answer
: A result worked out independently, by hand or by trusted software, that a
  test compares the program's output against.

tolerance
: How far apart two floating-point results may be and still count as equal.

naive
: Written in the most direct way, with no attempt to make it fast. The
  roadmap's goal calls the v0.1 demonstration "straightforward, unoptimized".

loop nest
: Loops placed inside other loops. Matrix multiplication is usually three
  loops deep.

## What matrix multiplication is

Start with two small square matrices.

```text
A = | 1  2 |        B = | 5  6 |
    | 3  4 |            | 7  8 |
```

The product `C = A × B` is another 2 by 2 matrix. To get the number in row
`i`, column `j` of `C`, take row `i` of `A` and column `j` of `B`, multiply
their numbers in pairs, and add the products.

```text
C[0, 0] = row 0 of A · column 0 of B = 1×5 + 2×7 = 19
C[0, 1] = row 0 of A · column 1 of B = 1×6 + 2×8 = 22
C[1, 0] = row 1 of A · column 0 of B = 3×5 + 4×7 = 43
C[1, 1] = row 1 of A · column 1 of B = 3×6 + 4×8 = 50
```

So `C` is `| 19 22 |` over `| 43 50 |`. Each cell is one **dot product**: a
row and a column, multiplied pair by pair and summed.

That is all matrix multiplication is. It shows up everywhere numerical code
runs, from graphics to machine learning, which is why Vortex chose it as the
first workload. The same rule works for rectangles, as long as the row of `A`
and the column of `B` have the same length.

<figure class="vx-figure">
<svg viewBox="0 0 760 280" role="img" aria-labelledby="s10-mm-title s10-mm-desc">
<title id="s10-mm-title">A two-by-three matrix times a three-by-two matrix, filled one output cell at a time</title>
<desc id="s10-mm-desc">Matrix A has rows 1 2 3 and 4 5 6. Matrix B has rows 7 8, 9 10 and 11 12. The product C has rows 58 64 and 139 154. In turn, row 0 of A and column 0 of B light up with cell C[0, 0], then row 0 with column 1, then row 1 with column 0, then row 1 with column 1. Beside the matrices, the four sums are written out.</desc>
<text class="vx-text" x="30" y="32">Each cell of C is one row of A times one column of B</text>
<rect class="vx-box" x="30" y="80" width="44" height="36"/>
<rect class="vx-box" x="74" y="80" width="44" height="36"/>
<rect class="vx-box" x="118" y="80" width="44" height="36"/>
<rect class="vx-box" x="30" y="116" width="44" height="36"/>
<rect class="vx-box" x="74" y="116" width="44" height="36"/>
<rect class="vx-box" x="118" y="116" width="44" height="36"/>
<rect class="vx-box" x="200" y="62" width="44" height="36"/>
<rect class="vx-box" x="244" y="62" width="44" height="36"/>
<rect class="vx-box" x="200" y="98" width="44" height="36"/>
<rect class="vx-box" x="244" y="98" width="44" height="36"/>
<rect class="vx-box" x="200" y="134" width="44" height="36"/>
<rect class="vx-box" x="244" y="134" width="44" height="36"/>
<rect class="vx-box" x="325" y="80" width="44" height="36"/>
<rect class="vx-box" x="369" y="80" width="44" height="36"/>
<rect class="vx-box" x="325" y="116" width="44" height="36"/>
<rect class="vx-box" x="369" y="116" width="44" height="36"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 4">
<rect class="vx-box-accent" x="30" y="80" width="132" height="36"/>
<rect class="vx-box-accent" x="200" y="62" width="44" height="108"/>
<rect class="vx-box-accent" x="325" y="80" width="44" height="36"/>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4">
<rect class="vx-box-accent" x="30" y="80" width="132" height="36"/>
<rect class="vx-box-accent" x="244" y="62" width="44" height="108"/>
<rect class="vx-box-accent" x="369" y="80" width="44" height="36"/>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4">
<rect class="vx-box-accent" x="30" y="116" width="132" height="36"/>
<rect class="vx-box-accent" x="200" y="62" width="44" height="108"/>
<rect class="vx-box-accent" x="325" y="116" width="44" height="36"/>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 4">
<rect class="vx-box-accent" x="30" y="116" width="132" height="36"/>
<rect class="vx-box-accent" x="244" y="62" width="44" height="108"/>
<rect class="vx-box-accent" x="369" y="116" width="44" height="36"/>
</g>
<text class="vx-mono" x="52" y="103" text-anchor="middle">1</text>
<text class="vx-mono" x="96" y="103" text-anchor="middle">2</text>
<text class="vx-mono" x="140" y="103" text-anchor="middle">3</text>
<text class="vx-mono" x="52" y="139" text-anchor="middle">4</text>
<text class="vx-mono" x="96" y="139" text-anchor="middle">5</text>
<text class="vx-mono" x="140" y="139" text-anchor="middle">6</text>
<text class="vx-text" x="181" y="121" text-anchor="middle">×</text>
<text class="vx-mono" x="222" y="85" text-anchor="middle">7</text>
<text class="vx-mono" x="266" y="85" text-anchor="middle">8</text>
<text class="vx-mono" x="222" y="121" text-anchor="middle">9</text>
<text class="vx-mono" x="266" y="121" text-anchor="middle">10</text>
<text class="vx-mono" x="222" y="157" text-anchor="middle">11</text>
<text class="vx-mono" x="266" y="157" text-anchor="middle">12</text>
<text class="vx-text" x="306" y="121" text-anchor="middle">=</text>
<text class="vx-mono" x="347" y="103" text-anchor="middle">58</text>
<text class="vx-mono" x="391" y="103" text-anchor="middle">64</text>
<text class="vx-mono" x="347" y="139" text-anchor="middle">139</text>
<text class="vx-mono" x="391" y="139" text-anchor="middle">154</text>
<text class="vx-mono" x="96" y="200" text-anchor="middle">A: [f32; 2, 3]</text>
<text class="vx-mono" x="244" y="200" text-anchor="middle">B: [f32; 3, 2]</text>
<text class="vx-mono" x="369" y="200" text-anchor="middle">C: [f32; 2, 2]</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 4">
<text class="vx-mono" x="450" y="90">c[0, 0] = 1×7 + 2×9 + 3×11 = 58</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4">
<text class="vx-mono" x="450" y="120">c[0, 1] = 1×8 + 2×10 + 3×12 = 64</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4">
<text class="vx-mono" x="450" y="150">c[1, 0] = 4×7 + 5×9 + 6×11 = 139</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 4">
<text class="vx-mono" x="450" y="180">c[1, 1] = 4×8 + 5×10 + 6×12 = 154</text>
</g>
<text class="vx-text-muted" x="30" y="240">A row has 3 numbers and a column of B has 3 numbers, so every pair lines up.</text>
<text class="vx-text-muted" x="30" y="260">The output has A's number of rows and B's number of columns.</text>
</svg>
<figcaption>Figure 1. A rectangular example. A 2 by 3 matrix times a 3 by 2 matrix gives a 2 by 2 matrix. Each highlight pairs one row of A with one column of B and fills one cell of C. The four sums on the right are the known answer used in the program below.</figcaption>
</figure>

Two facts about shapes fall straight out of the definition. First, the
**inner dimension** must match: `A` needs exactly as many columns as `B` has
rows, otherwise the row and column cannot be paired off. Second, the result
has as many rows as `A` and as many columns as `B`. A 2 by 3 times a 3 by 2
gives a 2 by 2. A 3 by 2 times a 2 by 3 gives a 3 by 3. Order matters.

## The program the milestone asks for

The roadmap describes the shape of the Vortex program precisely:

- ordinary functions and `for` loops, no special matrix syntax (the
  [arrays chapter](../../specification/arrays.md#79-excluded-array-behavior)
  says v0.1 has no "built-in matrix operators");
- fixed-size, two-dimensional `f32` arrays;
- the two inputs passed as shared references, `&[f32; ...]`, because the
  function only reads them;
- the output passed as a mutable reference, `&mut [f32; ...]`, because the
  function writes into the caller's storage.

Here is one way to write it. It is illustrative: it is not an official Vortex
example, but every construct in it was checked against the
[grammar](../../specification/grammar.md) and against examples in the
specification and the language tour.

```vortex
// program: valid
fn multiply(a: &[f32; 2, 3], b: &[f32; 3, 2], c: &mut [f32; 2, 2]) {
    for row in 0..2 {
        for column in 0..2 {
            let mut sum: f32 = 0.0;
            for k in 0..3 {
                sum += a[row, k] * b[k, column];
            }
            c[row, column] = sum;
        }
    }
}

fn main() {
    let mut a = [0.0; 2, 3];
    a[0, 0] = 1.0;
    a[0, 1] = 2.0;
    a[0, 2] = 3.0;
    a[1, 0] = 4.0;
    a[1, 1] = 5.0;
    a[1, 2] = 6.0;

    let mut b = [0.0; 3, 2];
    b[0, 0] = 7.0;
    b[0, 1] = 8.0;
    b[1, 0] = 9.0;
    b[1, 1] = 10.0;
    b[2, 0] = 11.0;
    b[2, 1] = 12.0;

    let mut c = [0.0; 2, 2];
    multiply(&a, &b, &mut c);

    if c[0, 0] == 58.0 && c[0, 1] == 64.0 && c[1, 0] == 139.0 && c[1, 1] == 154.0 {
        print("ok");
    } else {
        print("wrong answer");
    }
}
```

Read it once as a compiler would. The parameter types carry the whole shape:
`multiply` accepts exactly a 2 by 3 input, a 3 by 2 input, and a 2 by 2
output, and nothing else. The loops are the three-deep **loop nest** from
Figure 1: pick a row, pick a column, then walk `k` along the shared inner
dimension. `sum` is a local `f32` that starts at `0.0` and collects one dot
product. The arrays in `main` start as repeat arrays filled with `0.0` and are
then set element by element, which exercises indexed stores; a nested literal
with the written type would also work (see [Traps](#traps)).

Every part of this program exercises something from an earlier stage. The
indexing `a[row, k]` needs the row-major layout from
[stage 8](stage-8-data-in-memory.md) and a bounds check from
[stage 9](stage-9-runtime-safety.md), or a proof that `row` and `k` stay in
range. The call passes references with no copying. `+=` on `f32` needs no
overflow check, because the required overflow checks are for integers, but it
must not be regrouped or reordered, and `sum += a[row, k] * b[k, column]` must
round the product and then the sum: the back end must not fuse them into one
fused multiply-add ([decision](../../decisions/numbers.md#d56)).

### One function per shape

V0.1 has no generics and no runtime-sized arrays, so a function's parameter
types fix its shapes. To test square matrices as well as rectangular ones,
you write a second function, say `multiply_square` with parameters of type
`&[f32; 2, 2]`, and a third for any other shape you want. This is not a
workaround. It is what fixed-shape arrays mean, and it is exactly why the
compiler knows every shape at compile time. Slices, tensors with shape
parameters and generics are all on the roadmap's
[after v0.1](../../roadmap.md#after-v01) list.

## Shape mistakes are compile-time errors

The milestone asks that "shape or type mistakes" be reported during
compilation. With shapes in the types, most of this comes free from
[stage 5](stage-5-types-and-rules.md). Suppose someone tries to multiply by a
matrix whose inner dimension does not match:

```vortex
// fragment
let mut wrong = [0.0; 2, 2];
multiply(&a, &wrong, &mut c);
// type error: multiply expects &[f32; 3, 2] for b, not &[f32; 2, 2]
```

<figure class="vx-figure">
<svg viewBox="0 0 760 290" role="img" aria-labelledby="s10-shape-title s10-shape-desc">
<title id="s10-shape-title">A shape that fits and a shape that is rejected while compiling</title>
<desc id="s10-shape-desc">The top row shows a two by three array times a three by two array giving a two by two array; the inner sizes, 3 and 3, match. The bottom row shows a two by three array times a two by two array. The second box is dashed and marked as rejected, because 3 columns cannot pair with 2 rows.</desc>
<text class="vx-text-muted" x="40" y="36">accepted</text>
<rect class="vx-box" x="40" y="48" width="170" height="50"/>
<text class="vx-mono" x="125" y="78" text-anchor="middle">[f32; 2, 3]</text>
<text class="vx-text" x="235" y="79" text-anchor="middle">×</text>
<rect class="vx-box" x="260" y="48" width="170" height="50"/>
<text class="vx-mono" x="345" y="78" text-anchor="middle">[f32; 3, 2]</text>
<line class="vx-line" x1="440" y1="73" x2="512" y2="73"/>
<polygon class="vx-arrowhead" points="522,73 510,67 510,79"/>
<rect class="vx-box-accent" x="530" y="48" width="190" height="50"/>
<text class="vx-mono" x="625" y="78" text-anchor="middle">[f32; 2, 2]</text>
<text class="vx-text-muted" x="125" y="120" text-anchor="middle">3 columns</text>
<text class="vx-text-muted" x="345" y="120" text-anchor="middle">3 rows</text>
<text class="vx-text-accent" x="235" y="120" text-anchor="middle">match</text>
<text class="vx-text-muted" x="40" y="166">rejected</text>
<rect class="vx-box" x="40" y="178" width="170" height="50"/>
<text class="vx-mono" x="125" y="208" text-anchor="middle">[f32; 2, 3]</text>
<text class="vx-text" x="235" y="209" text-anchor="middle">×</text>
<rect class="vx-box-bad vx-pulse" x="260" y="178" width="170" height="50"/>
<text class="vx-mono" x="345" y="208" text-anchor="middle">[f32; 2, 2]</text>
<rect class="vx-box-bad" x="530" y="178" width="190" height="50"/>
<text class="vx-text" x="625" y="208" text-anchor="middle">Type error, no program</text>
<text class="vx-text-muted" x="125" y="250" text-anchor="middle">3 columns</text>
<text class="vx-text-muted" x="345" y="250" text-anchor="middle">2 rows</text>
<text class="vx-text-accent" x="235" y="250" text-anchor="middle">no match</text>
<text class="vx-text-muted" x="530" y="250">the call's argument type does not</text>
<text class="vx-text-muted" x="530" y="268">equal the parameter type</text>
</svg>
<figcaption>Figure 2. The top pair can be multiplied: the inner sizes agree. The bottom pair cannot, and the compiler stops before producing any code. Note how it finds out. It knows nothing about matrix multiplication; it only sees that <code>&amp;[f32; 2, 2]</code> is not the parameter type <code>&amp;[f32; 3, 2]</code>.</figcaption>
</figure>

The caption's last point is worth saying again. The compiler has no rule
about matrices. The error comes from ordinary
[type equality](../../specification/types-and-values.md#411-type-equality),
which compares element type, rank and every evaluated dimension. The shapes
live in the types, so a shape mistake is a type mistake.

Other shape mistakes the front end should catch in this program:

- indexing with the wrong number of indices, such as `a[row]` on a rank-2
  array, because the number of indices must match the rank;
- a constant index past the edge, such as `a[2, 0]` on a 2 by 3 array, which
  is a constant-evaluation error because the index and the extent are both
  constants;
- a result array of the wrong shape passed as `c`;
- passing `&c` where `&mut c` is required, or `&mut a` where `a` was declared
  without `mut`.

One mistake the compiler must not reject is a loop bound that disagrees with
the array, such as `for k in 0..4` over an array with 3 columns. `k` is a loop
variable, not a constant expression, so the program compiles and keeps the
bounds check from [stage 9](stage-9-runtime-safety.md); it stops with a
runtime error the first time `k` reaches 3. A compiler may warn about the
loop, but only a constant index is checked while compiling
([decision 12](../../decisions/arrays.md#d12),
[record 39](../../decisions/diagnostics.md#d39)). A wrong answer is never
acceptable.

## Comparing with a known answer

A matrix multiplication test is only as good as the answer it checks against.
The numbers in Figure 1 were worked out by hand, and anyone can check them in
a minute. That is deliberate. A **known answer** should come from somewhere
other than the compiler under test: hand calculation for small cases, or a
trusted tool for larger ones.

The example program compares results with `==`. That is safe here and only
here. Every input is a small whole number, every product and sum is a whole
number well inside the range an `f32` stores exactly, so no rounding happens
anywhere. In general, floating-point results are rounded at every step, as
the tour warns: they "are not exact decimal arithmetic". For inputs such as
`0.1` or `1.0 / 3.0`, two correct programs can produce results that differ in
their last bits, and an exact comparison would call one of them wrong.
Within Vortex this cannot happen for one program: every operation rounds
once, in a fixed order, so the same program gives the same bits with every
conforming compiler. The difference appears between different calculations,
such as a Vortex result and a known answer computed in another order or by
another tool.

The usual answer is a **tolerance**: accept a result if it is within some
small distance of the expected value. In v0.1 you can write that with
ordinary comparisons, for example by checking that the difference lies
between a small negative and a small positive number. How small is a real
decision that depends on the sizes and values involved; Goldberg's paper is
the place to learn how to reason about it.[^goldberg] For the milestone
itself, the simplest honest plan is to choose inputs whose products are exact,
compare exactly, and add one test with non-exact inputs and a tolerance.

The [types chapter](../../specification/types-and-values.md#44-floating-point-values)
makes that fixed order a rule: no reordering, no fused multiply-add and no
extra precision. That rule is what makes the known answer stable. If your compiler
regrouped the sum inside the `k` loop, or fused each multiplication with its
addition, the program might still be "roughly right" and yet print a different
last digit on a different machine.

## Why speed can wait

The roadmap is blunt: "The first release is about correctness, useful errors,
and a complete compiler pipeline. Performance optimization begins after
v0.1." Its [after v0.1](../../roadmap.md#after-v01) list names the techniques
that make matrix multiplication fast: loop transformations, tiling and fusion,
SIMD vectorization, multicore execution, and later GPU code generation. None
of them belong in this stage.

There are three reasons, and they build on each other.

The first is that fast matrix multiplication is a large subject on its own,
and an unfinished compiler is a poor place to learn it. Every optimization
adds code that can be wrong in new ways.

The second is that an optimization needs something to be measured against.
The philosophy page says the first optimization milestone after v0.1 is to
turn "a straightforward matrix multiplication into a faster CPU
implementation while preserving its semantics". The naive program you finish
in this stage *is* that straightforward version. Its output becomes the known
answer every later, faster version must reproduce.

The third is that Vortex's principles forbid speed that changes answers. "Do
not surprise the programmer" is one of its design principles, and it says an
optimization that could change a floating-point result may only be used when
the programmer allowed it. A correct, slow baseline is how you will later
prove that a fast version kept that promise.

## What you need to have

<div class="vx-split" markdown="1">
<div markdown="1">

#### Need to have

- A matrix multiplication written in plain Vortex functions and loops: the milestone forbids special-casing it in the compiler.
- Inputs as shared references and the output as a mutable reference: required by the milestone and exercised by stage 8.
- At least one rectangular case and one square case, each with its own function: shapes are fixed by parameter types.
- Known answers worked out independently of the compiler.
- A test that feeds a wrongly shaped argument and expects a compile-time type error.
- A test with inputs that are not exact in `f32`, compared with a tolerance.
- The compiled program built with no optimization, as the milestone states.

</div>
<div class="vx-not-yet" markdown="1">

#### Not yet

- Any optimization of the loops, including tiling, reordering, vectorization and parallel execution: listed under after v0.1.
- A built-in matrix operator or library function: excluded by the arrays chapter.
- Shape-generic functions or runtime-sized matrices: generics, slices and tensors are after v0.1.
- Timing or performance tests: correctness is the whole requirement.
- GPU execution: kernels and GPU code generation are after v0.1.
- Relaxed floating-point modes: the tour defers them.

</div>
</div>

## What you do not need yet

Everything in the right-hand column is about making the program faster or more
general. Both are good goals, and both have their own place on the roadmap.
The only thing this stage has to prove is that the answer is right.

## How you know it is finished

[Milestone 10](../../roadmap.md#milestone-10-matrix-multiplication) is done
"when Vortex compiles and correctly runs the matrix multiplication example on
the CPU without optimization". The evidence for that:

- The rectangular program compiles, runs, and prints the success message, with
  every cell of `C` matching the known answer.
- A square version does the same, including a case where `A × B` and `B × A`
  give different answers, so a swapped argument would be caught.
- A version with non-exact inputs passes when compared with a tolerance.
- Passing an input of the wrong shape, or an output of the wrong shape, is
  rejected at compile time with a type error that points at the argument.
- A loop bound larger than the array's dimension compiles and stops at run
  time with a bounds error, never producing a wrong answer.
- All earlier milestone tests still pass.

## Traps

**Testing only square matrices.** A 2 by 2 multiply can pass while row and
column are swapped somewhere, because the shapes line up either way. The
rectangular test is the one that catches it. The same applies to the
row-major offsets from stage 8 ([decision](../../decisions/arrays.md#d43)).

**Choosing inputs that hide mistakes.** If every input is 1.0, many wrong
programs print the right answer. Use distinct values in every cell, as in
Figure 1.

**Forgetting to reset the sum.** A dot product must start at zero for every
output cell. Starting it once outside the row and column loops adds every
previous cell's total into the next one.

**Comparing floating-point results exactly by habit.** It works for the
whole-number example and fails, sometimes, for everything else. Know which
of your tests are exact and why.

**Filling arrays with nested list literals.** The
[tour](../../language-tour/04-variables-and-types.md) initializes a
`[f32; 2, 2]` from a list of two lists. That works because the written type
shapes the literal: each inner list is checked as one row
([decision](../../decisions/arrays.md#d21)). Without the annotation, the same
literal has the nested type `[[f32; 2]; 2]`, which does not convert. Keep the
annotation whenever you use a nested literal.

**Letting the compiler recognize the pattern.** It may be tempting to spot
three nested loops and swap in something clever. The milestone says ordinary
functions and loops, compiled without optimization. Anything else hides bugs
in the parts of the compiler this program is meant to test.

## How others teach this stage

**Kaleidoscope.** The LLVM tutorial generates plain, unoptimized code in
chapter 3 and only adds optimization passes in chapter 4, once code generation
works.[^kal4] Even then it presents each pass as a choice: LLVM "allows a
compiler implementer to make complete decisions" about which optimizations to
use and in what order.[^kal4] Vortex takes the same order, but further apart:
optimization waits not for the next chapter but for the next release.

**MLIR Toy.** The MLIR tutorial builds a small language whose values are
tensors, with shapes that can be inferred from the literals that create them
and code generation limited to tensors of rank 2 or less.[^toy] Across its chapters it adds shape inference and lowers the
program step by step until it reaches LLVM.[^toy] It is a preview of where
Vortex's "higher-level tensor types" might go after v0.1. Read it for the
direction of travel, not as a model for v0.1, which has no tensor type and no
shape inference beyond constant dimensions.

**Ghuloum.** The incremental compiler tests every step the same way: a sample
program paired with its expected output, compiled, linked with a small
runtime, run, and compared.[^ghuloum] The matrix program here is exactly that
kind of test, only larger.

**Goldberg.** The background reading on why floating-point results are
rounded, and why algebraically equal expressions can give different
results.[^goldberg] It is the reason the tolerance section above exists.

[^kal4]: LLVM Project, "My First Language Frontend with LLVM Tutorial", chapter 4, "Adding JIT and Optimizer Support". <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl04.html>
[^toy]: LLVM Project, "Toy Tutorial", MLIR documentation, including chapter 1, "Toy Language and AST". <https://mlir.llvm.org/docs/Tutorials/Toy/>
[^ghuloum]: Abdulaziz Ghuloum, "An Incremental Approach to Compiler Construction", *Proceedings of the 2006 Scheme and Functional Programming Workshop*, University of Chicago Technical Report TR-2006-06, section 2.7, "Testing Infrastructure". <http://scheme2006.cs.uchicago.edu/11-ghuloum.pdf>
[^goldberg]: David Goldberg, "What Every Computer Scientist Should Know About Floating-Point Arithmetic", *ACM Computing Surveys* 23(1), 1991. <https://doi.org/10.1145/103162.103163>
