# P16. Capstone: the ladder, measured

<p class="page-intro">The earlier chapters each proved one transformation of the stage 10 kernel legal on its own. This chapter puts them in one order, the matmul ladder, and gives every rung two gates: a bits test that decides whether the rung may run by default, and a measurement protocol that decides whether it made anything faster. It ends with a report against Accelerate, OpenBLAS and BLIS that a reader on another machine can check.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 60 minutes · Builds on: [P1. Measure first](p1-measure-first.md), [P3. The roofline model](p3-roofline.md), [P11. Floating point under optimization](p11-floating-point.md), [P12. Anatomy of a fast GEMM](p12-fast-gemm.md), [P13. Multithreading](p13-multithreading.md), [P15. Choosing parameters: models or search](p15-choosing-parameters.md)</p>

???+ remember "Before you start, remember"

    ??? question "Which question decides whether a transformation can change a floating-point result?"

        Whether it changes the sequence of roundings that produces one output value, or only the order in which independent output values are produced. The first can change bits; the second cannot.

        Introduced in [P11. Floating point under optimization](p11-floating-point.md#the-rule-every-reordering-pass-needs).

    ??? question "Why is 'best of ten runs' a worse summary than a median with a confidence interval?"

        The minimum only ever reports the luckiest run. A median with an interval says where most readings sit and how sure you can be of it, and two intervals can be compared.

        Introduced in [P1. Measure first](p1-measure-first.md#what-to-report-instead-of-one-number).

    ??? question "What does the roofline bound say, and which two numbers does it need?"

        Attainable performance is at most the smaller of the machine's peak flop rate and its peak memory bandwidth times the kernel's operational intensity. It needs a measured peak flop rate and a measured peak bandwidth for the machine in question.

        Introduced in [P3. The roofline model](p3-roofline.md#the-roofline-two-lines-and-a-ridge).

    ??? question "Why does splitting the `k` loop across threads need a reduction, when splitting rows of `c` does not?"

        Every trip around `k` updates the same `c` element, so two threads working on different `k` ranges would race on it. Each needs a private partial sum, added to the others at the end. Splitting rows gives each thread its own elements, with nothing to combine.

        Introduced in [P13. Multithreading](p13-multithreading.md#which-loop-in-the-kernel-to-split).

    ??? question "What may a tuner never do while it searches tile sizes?"

        Try a candidate that changes the program's observable meaning. The search space has to be restricted to variants that keep the result before any of them is timed.

        Introduced in [P15. Choosing parameters: models or search](p15-choosing-parameters.md#what-a-tuner-may-not-do).

!!! goals "In this chapter"

    - List the rungs of the CPU matmul ladder in order, with the legality argument and the chapter behind each, and sort them into rungs that keep rung 0's bits and rungs that do not.
    - Build a bits gate that compares results as bit patterns, and explain why `==` is the wrong test.
    - Decide from two sets of timings whether one rung beats another, using a confidence interval for the ratio of their medians.
    - Set up a fair comparison against Accelerate, OpenBLAS and BLIS: same precision, shape and thread count, each library's thread control pinned, and a tolerance check in place of the bits gate.
    - Report a ladder so that someone else can check it: machine, compiler, flags, date, repetitions, intervals and an upper bound.

## One kernel, eleven rungs

Every chapter of this book has worked on the same computation: the matrix product from [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for), here at the 64 by 64 shape the book has used throughout.

```vortex
// items: valid
fn multiply(a: &[f32; 64, 64], b: &[f32; 64, 64], c: &mut [f32; 64, 64]) {
    for row in 0..64 {
        for column in 0..64 {
            let mut sum: f32 = 0.0;
            for k in 0..64 {
                sum += a[row, k] * b[k, column];
            }
            c[row, column] = sum;
        }
    }
}
```

The chapters took this loop apart one idea at a time. [O8](o8-loops.md#removing-a-check-with-a-proof) removed its bounds checks with a range proof, [P7](p7-loop-transformations.md#interchange) swapped its loops, [P8](p8-cache-blocking.md) tiled it, [P12](p12-fast-gemm.md) packed its operands and built a register micro-kernel, and [P13](p13-multithreading.md) spread it across cores. Each chapter measured, or asked you to measure, its own step against the step before.

A **ladder** is those steps put in one fixed order, each **rung** a version of the kernel that starts from the previous rung and changes one thing. Fixing the order matters for two reasons. A transformation's benefit depends on what came before it: packing pays only once the loops are tiled, and threading a kernel that is still bound by memory bandwidth mostly adds threads waiting on memory. And a fixed order gives every rung one neighbor to be compared with, so that "rung 6 is faster" has a definite meaning: faster than rung 5, on this machine, measured the same way.

The order this book uses follows the one [P7](p7-loop-transformations.md#putting-them-in-order) takes from McKinley, Carr and Tseng: fix the order of memory accesses first, then tile for the caches, then block for registers. Threading and parameter tuning come last, because both need a fast single-core kernel to be worth anything.

## The ladder, rung by rung

The table lists every rung. "Bits" says whether the rung, done correctly, produces exactly the bits rung 0 produces for every input. "Must prove" is the fact a compiler needs before applying the rung without being told to. "Evidence" gives a number only where a cited source measured one, on its own machine: read those as facts about someone else's hardware, not as predictions for yours.

| Rung | Change | Bits | Must prove | Chapter | Evidence |
| --- | --- | --- | --- | --- | --- |
| 0 | Naive `ijk` loop with its bounds and overflow checks | baseline | nothing | [stage 10](../compiler/guide/stage-10-matrix-multiplication.md) | measure |
| 1 | Scalar cleanup: registers for locals, constant shapes propagated, address arithmetic hoisted, checks removed by range proof | identical | every index stays in range; no-wrap only where proven | [O5](o5-constants-and-dead-code.md), [O6](o6-redundancy.md), [O8](o8-loops.md) | measure |
| 2 | Interchange `ijk` to `ikj`: `b` and `c` walked with unit stride | identical | every dependence stays lexicographically positive | [P6](p6-dependence-analysis.md), [P7](p7-loop-transformations.md#interchange) | loop reorder, 1512 ms to 89 ms, fast-math build on an i7-6700[^boehm] |
| 3 | Vectorize across `column` | identical, without FMA | `c` shares no memory with `a` or `b` | [O9](o9-alias-analysis.md), [P10](p10-vectorization.md) | measure |
| 4 | Tile for L1 and L2 | identical, if `k` tiles run in order and `c` is updated in place | the tiled band is fully permutable | [P8](p8-cache-blocking.md#keeping-the-order-keeping-the-bits) | blocked version at 17.3% of the original's cycles, Core 2[^drepper]; L1 tiling, 89 ms to 70 ms, i7-6700[^boehm] |
| 5 | Pack `a` and `b` into contiguous panels | identical | nothing: packing only copies | [P12](p12-fast-gemm.md) | copying `b` transposed first: 23.4% of the original's cycles, Core 2[^drepper] |
| 6 | Register-blocked micro-kernel whose accumulators start from `c` | identical | the accumulators fit in the registers | [P7](p7-loop-transformations.md#unroll-and-jam-and-register-blocks), [P12](p12-fast-gemm.md) | measure |
| 6b | The same micro-kernel with accumulators started at zero, added to `c` once per `k` panel | **differs** | permission to regroup | [P12](p12-fast-gemm.md) | measure |
| 7 | Fused multiply-add | **differs**: one rounding instead of two | permission to contract | [P11](p11-floating-point.md) | two FMAs per cycle, 32 flops per cycle, on the i7-6700[^boehm] |
| 8 | Threads over blocks of rows or columns of `c`, private packing buffers | identical | the threads' parts of `c` do not overlap | [P13](p13-multithreading.md) | 70 ms to 16 ms, OpenMP, eight threads on the four-core i7-6700[^boehm] |
| 8b | Threads over `k` panels, partial sums added at the end | **differs** | permission to regroup | [P13](p13-multithreading.md#which-loop-in-the-kernel-to-split) | measure |
| 9 | Tune `mc`, `kc`, `nc`, `mr`, `nr` | identical, if the search space holds only identical variants | every candidate keeps the bits | [P15](p15-choosing-parameters.md) | measure |
| 10 | Stretch: SME matrix instructions | depends on the instructions and mode used | the target has the feature | [G11](../gpu/g11-matrix-units.md) | measure |

Four notes keep the evidence column honest. All of Boehm's timings come from a build with `-ffast-math`, which [decision 56](../decisions/numbers.md#d56) forbids, so they show the shape of a climb, not the cost of a strict one. His multithreaded step used OpenMP with eight threads on a four-core chip.[^boehm] Drepper's "transposed" version copies `b` into a transposed temporary before multiplying, which makes it a layout change like rung 5, not an interchange like rung 2, and his own footnote says he ignores rounding, treating the order of each element's additions as irrelevant.[^drepper] And the two sources used different machines, shapes and compilers, so their rows cannot be chained into one speedup.

<figure class="vx-figure">
<svg viewBox="0 0 860 400" role="img" aria-label="The ladder drawn as a staircase of rungs 0 to 9 rising left to right. Rungs 0 to 6, 8 and 9 are plain boxes on the main line, meaning they keep rung 0's bits. Rung 7, fused multiply-add, sits on the stair but is marked as changing bits. Two side branches, 6b above rung 6 and 8b above rung 8, are also marked as changing bits. Rung 10, SME, is a dashed box past the top step.">
<defs><marker id="p16-f0-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="20" y="16" width="26" height="16" rx="3"/>
<text class="vx-text-muted" x="54" y="29">keeps rung 0's bits: may run by default</text>
<rect class="vx-box-bad" x="330" y="16" width="26" height="16" rx="3"/>
<text class="vx-text-muted" x="364" y="29">changes bits: opt-in only</text>
<rect class="vx-box" x="560" y="16" width="26" height="16" rx="3" stroke-dasharray="4 3"/>
<text class="vx-text-muted" x="594" y="29">stretch</text>
<rect class="vx-box" x="10" y="330" width="72" height="44" rx="5"/>
<text class="vx-text" x="46" y="349" text-anchor="middle">0</text>
<text class="vx-text-muted" x="46" y="366" text-anchor="middle">naive</text>
<rect class="vx-box" x="86" y="306" width="72" height="44" rx="5"/>
<text class="vx-text" x="122" y="325" text-anchor="middle">1</text>
<text class="vx-text-muted" x="122" y="342" text-anchor="middle">cleanup</text>
<rect class="vx-box" x="162" y="282" width="72" height="44" rx="5"/>
<text class="vx-text" x="198" y="301" text-anchor="middle">2</text>
<text class="vx-text-muted" x="198" y="318" text-anchor="middle">interchange</text>
<rect class="vx-box" x="238" y="258" width="72" height="44" rx="5"/>
<text class="vx-text" x="274" y="277" text-anchor="middle">3</text>
<text class="vx-text-muted" x="274" y="294" text-anchor="middle">vectorize</text>
<rect class="vx-box" x="314" y="234" width="72" height="44" rx="5"/>
<text class="vx-text" x="350" y="253" text-anchor="middle">4</text>
<text class="vx-text-muted" x="350" y="270" text-anchor="middle">tile</text>
<rect class="vx-box" x="390" y="210" width="72" height="44" rx="5"/>
<text class="vx-text" x="426" y="229" text-anchor="middle">5</text>
<text class="vx-text-muted" x="426" y="246" text-anchor="middle">pack</text>
<rect class="vx-box" x="466" y="186" width="72" height="44" rx="5"/>
<text class="vx-text" x="502" y="205" text-anchor="middle">6</text>
<text class="vx-text-muted" x="502" y="222" text-anchor="middle">registers</text>
<rect class="vx-box-bad" x="542" y="162" width="72" height="44" rx="5"/>
<text class="vx-text" x="578" y="181" text-anchor="middle">7</text>
<text class="vx-text-muted" x="578" y="198" text-anchor="middle">FMA</text>
<rect class="vx-box" x="618" y="138" width="72" height="44" rx="5"/>
<text class="vx-text" x="654" y="157" text-anchor="middle">8</text>
<text class="vx-text-muted" x="654" y="174" text-anchor="middle">threads</text>
<rect class="vx-box" x="694" y="114" width="72" height="44" rx="5"/>
<text class="vx-text" x="730" y="133" text-anchor="middle">9</text>
<text class="vx-text-muted" x="730" y="150" text-anchor="middle">tune</text>
<rect class="vx-box" x="770" y="58" width="72" height="44" rx="5" stroke-dasharray="4 3"/>
<text class="vx-text" x="806" y="77" text-anchor="middle">10</text>
<text class="vx-text-muted" x="806" y="94" text-anchor="middle">SME</text>
<line class="vx-line" x1="766" y1="114" x2="788" y2="104" marker-end="url(#p16-f0-head)"/>
<line class="vx-line" x1="502" y1="186" x2="502" y2="128" marker-end="url(#p16-f0-head)"/>
<rect class="vx-box-bad" x="440" y="84" width="124" height="44" rx="5"/>
<text class="vx-text" x="502" y="103" text-anchor="middle">6b</text>
<text class="vx-text-muted" x="502" y="120" text-anchor="middle">zero-started</text>
<line class="vx-line" x1="654" y1="138" x2="654" y2="80" marker-end="url(#p16-f0-head)"/>
<rect class="vx-box-bad" x="600" y="36" width="108" height="44" rx="5"/>
<text class="vx-text" x="654" y="55" text-anchor="middle">8b</text>
<text class="vx-text-muted" x="654" y="72" text-anchor="middle">split k</text>
<text class="vx-text-muted" x="20" y="394">each step: one change, one legality proof, one measured comparison with the step below</text>
</svg>
<figcaption>Figure 1. The ladder. Nine of the rungs keep rung 0's bits, and a strict compiler may apply them without asking. Three variants (6b, 7 and 8b) change the bits and stay off until the programmer opts in. Rung 10 is a stretch goal whose bits depend on how it is built.</figcaption>
</figure>

Three rows need a closer look.

**Rung 3 vectorizes across `column`, never across `k`.** Across `column`, each SIMD lane holds a different element of `c`, and each lane's own sum still runs `k` = 0, 1, 2 and onward in order. Across `k`, one element's products would be split among the lanes and summed as a tree, which regroups them. LLVM's loop vectorizer does not do that for floating point unless at least `-fassociative-math -fno-signed-zeros -fno-trapping-math` is in effect. On AArch64 and RISC-V it can instead emit an **ordered reduction**, which adds the lanes one after another and keeps the exact result; the LLVM documentation calls the vectorization this allows limited.[^llvm-vec] [P10](p10-vectorization.md#reductions-ordered-or-reassociated) covers both.

**Rung 7 is not a flag a C++ comparator gets for free, or avoids for free.** Clang's default, `-ffp-contract=on`, fuses a multiply and an add written in the same statement, so `sum += a * b` compiled with no flags is already rung 7.[^clang-um] Every C++ rung you write as a reference has to be built with `-ffp-contract=off`, as the examples in this book are, or the reference itself is not rung 0.

**Rungs 6b and 8b are the shape real libraries use.** BLIS documents its micro-kernel as computing `C11 := beta * C11 + alpha * A1 * B1`: the product of one `k` panel is formed in registers and then added to `c`, which is rung 6b.[^blis-k] Smith and his coauthors parallelize the other loops and treat the `k` loop as a special case: threads there update the same block of `c`, so each needs a zeroed copy followed by a reduction, and they recommend it only when `c` is small.[^smith14] BLIS's threading documentation says the same loop is not parallelized at all, for the same reason.[^blis-mt] 

The stage 10 kernel, at 64 by 64, is a small `c`, so this is not a remote case. Rung 4 can slide into the same shape by accident: tiling keeps the bits only while each element's `k` tiles run in increasing order and update `c` in place. Sum each `k` tile into a fresh zero-started temporary instead, and the tiled loop has become rung 6b.

## The bits gate

A rung is **bit-preserving** when, for every input, every element of its result has exactly the same 32-bit pattern as rung 0's. For this kernel there is a simple way to see which rungs can be: rung 0 builds each `c` element by adding its products one at a time, in increasing `k`, into a running sum that starts at +0.0. A rung that keeps that sequence for every element keeps the bits, whatever order it visits the elements in. A rung that changes the sequence can change them. That is P11's rule, specialized to this kernel, and [decision 56](../decisions/numbers.md#d56) is why it matters: each `f32` operation must be one IEEE 754 operation, rounded once, never contracted or reordered.[^decisions-numbers]

The **bits gate** is the test that goes with the rule: run a rung and rung 0 on the same inputs and compare every element's bit pattern. It is not a proof. A rung can agree with rung 0 on the inputs you chose and differ on others, and the second half of the example below shows one. The proof is the argument in the last paragraph, made once for the rung's loop structure; the gate is what catches a rung whose code does not match its argument.

### A worked example by hand

Take one element of `c` whose four products, in `k` order, are 100,000,000, 1, −100,000,000 and 1. The exact sum is 2. In `f32`, the gap between neighboring values near 100,000,000 is 8, so 100,000,001 is not representable and rounds back to 100,000,000.

Rung 0 adds in order, starting from +0.0:

| Step | Running sum before | Add | Exact result | Rounded to `f32` |
| --- | --- | --- | --- | --- |
| `k` = 0 | 0 | 100,000,000 | 100,000,000 | 100,000,000 |
| `k` = 1 | 100,000,000 | 1 | 100,000,001 | 100,000,000 |
| `k` = 2 | 100,000,000 | −100,000,000 | 0 | 0 |
| `k` = 3 | 0 | 1 | 1 | 1 |

Rung 6b, with `k` panels of two, starts a new sum at zero for each panel and adds each panel's total to `c`. The first panel gives 100,000,000 + 1, which rounds to 100,000,000. The second gives −100,000,000 + 1 = −99,999,999, which also rounds, to −100,000,000. Adding the two panels to `c` gives 0. Rung 8b, splitting `k` between two threads that each take one half, forms exactly the same two partial sums and also ends at 0.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Two ways of summing the same four products. Left, rung 0: a chain 0, then 100,000,000, then 100,000,000 again because the added 1 is lost to rounding, then 0, then 1. Right, rung 6b or 8b: two panels each start at zero; the first gives 100,000,000 and the second gives minus 100,000,000, because each panel's added 1 is lost; their sum is 0. The exact answer is 2.">
<defs><marker id="p16-f2-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="170" y="24" text-anchor="middle">Rung 0: one chain</text>
<rect class="vx-box" x="110" y="40" width="120" height="30" rx="4"/>
<text class="vx-mono" x="170" y="60" text-anchor="middle">0</text>
<line class="vx-line" x1="170" y1="70" x2="170" y2="94" marker-end="url(#p16-f2-head)"/>
<text class="vx-text-muted" x="182" y="87">+ 1e8</text>
<rect class="vx-box" x="110" y="94" width="120" height="30" rx="4"/>
<text class="vx-mono" x="170" y="114" text-anchor="middle">1e8</text>
<line class="vx-line" x1="170" y1="124" x2="170" y2="148" marker-end="url(#p16-f2-head)"/>
<text class="vx-text-muted" x="182" y="141">+ 1</text>
<rect class="vx-box-accent" x="110" y="148" width="120" height="30" rx="4"/>
<text class="vx-mono" x="170" y="168" text-anchor="middle">1e8</text>
<text class="vx-text-accent" x="240" y="168">1 lost</text>
<line class="vx-line" x1="170" y1="178" x2="170" y2="202" marker-end="url(#p16-f2-head)"/>
<text class="vx-text-muted" x="182" y="195">+ (−1e8)</text>
<rect class="vx-box" x="110" y="202" width="120" height="30" rx="4"/>
<text class="vx-mono" x="170" y="222" text-anchor="middle">0</text>
<line class="vx-line" x1="170" y1="232" x2="170" y2="256" marker-end="url(#p16-f2-head)"/>
<text class="vx-text-muted" x="182" y="249">+ 1</text>
<rect class="vx-box-strong" x="110" y="256" width="120" height="30" rx="4"/>
<text class="vx-mono" x="170" y="276" text-anchor="middle">1</text>
<text class="vx-text" x="540" y="24" text-anchor="middle">Rungs 6b and 8b: two panels</text>
<rect class="vx-box" x="400" y="40" width="130" height="30" rx="4"/>
<text class="vx-mono" x="465" y="60" text-anchor="middle">0 + 1e8 + 1</text>
<rect class="vx-box" x="560" y="40" width="150" height="30" rx="4"/>
<text class="vx-mono" x="635" y="60" text-anchor="middle">0 + (−1e8) + 1</text>
<line class="vx-line" x1="465" y1="70" x2="465" y2="104" marker-end="url(#p16-f2-head)"/>
<line class="vx-line" x1="635" y1="70" x2="635" y2="104" marker-end="url(#p16-f2-head)"/>
<rect class="vx-box-accent" x="400" y="104" width="130" height="30" rx="4"/>
<text class="vx-mono" x="465" y="124" text-anchor="middle">1e8</text>
<rect class="vx-box-accent" x="560" y="104" width="150" height="30" rx="4"/>
<text class="vx-mono" x="635" y="124" text-anchor="middle">−1e8</text>
<text class="vx-text-accent" x="545" y="160" text-anchor="middle">each panel loses its 1</text>
<line class="vx-line" x1="465" y1="134" x2="530" y2="194" marker-end="url(#p16-f2-head)"/>
<line class="vx-line" x1="635" y1="134" x2="570" y2="194" marker-end="url(#p16-f2-head)"/>
<rect class="vx-box-bad" x="490" y="194" width="120" height="30" rx="4"/>
<text class="vx-mono" x="550" y="214" text-anchor="middle">0</text>
<text class="vx-text-muted" x="550" y="256" text-anchor="middle">exact sum: 2</text>
</svg>
<figcaption>Figure 2. One element's four products, added in rung 0's order (left) and in two zero-started panels (right). Both lose information to rounding, in different places, so they end at different values. Neither is the exact answer; the gate asks only whether a rung matches rung 0.</figcaption>
</figure>

Two lessons come out of this small case. First, rung 0 is not more accurate than rung 6b here; both are wrong, differently. The bits gate is not a test of accuracy. It tests that a rung computes the same thing rung 0 computes, which is what decision 56 promises the programmer and what golden outputs rely on. Second, the difference came from one element with a wide range of magnitudes. On inputs of similar sizes, rungs 6b and 8b often agree with rung 0 on many elements and differ on some, which is why the gate compares every element.

### Why the gate compares bits, not values

The obvious way to compare two results is `==` on each element. It is the wrong test, for two reasons. `==` says +0.0 equals −0.0, although their bit patterns differ and a program that prints them can print different text. And `==` says a NaN is not equal to itself, so two rungs that both produce the same NaN would fail. The gate compares bit patterns instead, which in C++26 is one `std::bit_cast` to a 32-bit integer per element.

The example runs a small ladder on an 8 by 8 product, twice: once on mixed inputs, and once on a matrix `a` filled with −0.0 against a matrix `b` of ones. The last rung, "start at first term", is a tempting scalar cleanup: begin each sum at the first product instead of at 0.0 and skip one addition.

--8<-- "includes/examples/optimize/p16-capstone/bits_gate.cpp.md"

On mixed inputs, interchange passes, and the zero-started panels and the fused multiply-add each change a few dozen of the 64 elements.

The "first term" rung passes on mixed inputs and fails on every element of the second input, while `==` sees no difference at all. The reason is IEEE 754's rule for signed zeros: rung 0 computes +0.0 + (−0.0), which rounds to +0.0, while the cleanup starts from the first product, −0.0, and never adds +0.0 to it. The "cleanup" is the fold `0.0 + x` → `x`, and LLVM refuses it for exactly this reason unless the instruction carries the `nsz` flag, which lets the optimizer "treat the sign of a zero argument or zero result as insignificant". Adding −0.0 is a true identity, so that fold needs no flag:[^langref]

--8<-- "includes/examples/optimize/p16-capstone/signed_zero.ll.md"

This is the second half of the gate's lesson: a gate needs inputs chosen to break rungs, not only typical ones. Signed zeros, values of widely different sizes, and shapes that tile and unroll factors do not divide evenly each catch a different class of mistake.

<figure class="vx-figure">
<svg viewBox="0 0 760 440" role="img" aria-label="A flow diagram. Implement rung N leads to the bits gate: same bit patterns as rung 0 on every test input? If identical, the rung goes to measurement under P1's protocol, then to the record. If it differs, the next question is whether the rung is a listed opt-in (6b, 7 or 8b). If yes, it is measured and recorded with an opt-in label. If no, it is rejected and fixed before any timing.">
<defs><marker id="p16-f1-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="290" y="20" width="180" height="50" rx="6"/>
<text class="vx-text" x="380" y="50" text-anchor="middle">Implement rung N</text>
<line class="vx-line" x1="380" y1="70" x2="380" y2="106" marker-end="url(#p16-f1-head)"/>
<rect class="vx-box-accent" x="230" y="106" width="300" height="66" rx="6"/>
<text class="vx-text" x="380" y="132" text-anchor="middle">Bits gate</text>
<text class="vx-text-muted" x="380" y="152" text-anchor="middle">same bit patterns as rung 0, every input?</text>
<path class="vx-line" d="M260 172 C 180 200, 120 200, 120 236" marker-end="url(#p16-f1-head)"/>
<text class="vx-text-accent" x="160" y="200" text-anchor="middle">identical</text>
<path class="vx-line" d="M500 172 C 580 200, 625 200, 625 236" marker-end="url(#p16-f1-head)"/>
<text class="vx-text-accent" x="610" y="200" text-anchor="middle">differs</text>
<rect class="vx-box" x="10" y="236" width="220" height="66" rx="6"/>
<text class="vx-text" x="120" y="262" text-anchor="middle">Measure: P1 protocol</text>
<text class="vx-text-muted" x="120" y="282" text-anchor="middle">median, 95% interval</text>
<rect class="vx-box" x="500" y="236" width="250" height="66" rx="6"/>
<text class="vx-text" x="625" y="262" text-anchor="middle">A listed opt-in?</text>
<text class="vx-text-muted" x="625" y="282" text-anchor="middle">6b, 7 or 8b</text>
<path class="vx-line" d="M120 302 C 120 340, 260 350, 300 358" marker-end="url(#p16-f1-head)"/>
<path class="vx-line" d="M560 302 C 500 340, 440 350, 420 358" marker-end="url(#p16-f1-head)"/>
<text class="vx-text-accent" x="470" y="330" text-anchor="middle">yes: measure, label</text>
<rect class="vx-box" x="200" y="358" width="340" height="66" rx="6"/>
<text class="vx-text" x="370" y="384" text-anchor="middle">Record</text>
<text class="vx-text-muted" x="370" y="404" text-anchor="middle">remark, median, interval, machine, date</text>
<path class="vx-line" d="M690 302 C 695 330, 675 345, 665 358" marker-end="url(#p16-f1-head)"/>
<text class="vx-text-accent" x="712" y="332" text-anchor="middle">no</text>
<rect class="vx-box-bad" x="590" y="358" width="150" height="66" rx="6"/>
<text class="vx-text" x="665" y="384" text-anchor="middle">Reject</text>
<text class="vx-text-muted" x="665" y="404" text-anchor="middle">fix before timing</text>
</svg>
<figcaption>Figure 3. Every rung meets the bits gate before the stopwatch. An identical rung, or a named opt-in carrying its label, goes on to be measured; any other difference is a bug in the rung, not a performance result.</figcaption>
</figure>

??? check "A rung passes the bits gate on 200 random input pairs. Has it been shown to be bit-preserving?"

    No. The gate shows that it matched rung 0 on those inputs. Random inputs rarely contain signed zeros, NaNs or values of wildly different sizes, which is where regrouped or folded sums differ; the "start at first term" rung passes every mixed input in the example and fails every element of the signed-zero one. The claim that a rung is bit-preserving comes from the argument about its loop structure: every element's additions happen in rung 0's order, starting from +0.0. The gate catches code that does not match that argument, so it needs adversarial inputs as well as random ones.

## Your turn: sort these rungs

Here are seven variants a compiler or a programmer might try on the kernel. For each, decide whether it keeps rung 0's bits, and what it would have to prove or be permitted. Two are done.

| Variant | Bits | Why |
| --- | --- | --- |
| Tile with the `k` tile loop outermost, `c` updated in place | identical | each element's `k` tiles still run in increasing order, and nothing regroups them |
| Unroll `k` by two with two running sums, added at the end | differs | even and odd products go into separate sums: a regrouping |
| Pack `b` into column panels before rung 6 | ? | ? |
| Run the `k` loop from 63 down to 0 | ? | ? |
| Vectorize across `column` on a target where the vector multiply-add is fused | ? | ? |
| Threads over row blocks, each with its own packing buffer | ? | ? |
| A tuner's candidate with `kc` = 24, zero-started panels | ? | ? |

??? check "Answers"

    - **Pack `b` into column panels:** identical. Packing copies values into a new layout; the same products are added in the same order.
    - **Reverse the `k` loop:** differs. Each element's additions run in the opposite order, which regroups them; [P7](p7-loop-transformations.md#putting-them-in-order) lists reversing an accumulation among the shortcuts that change Vortex's bits.
    - **Vectorize with a fused vector multiply-add:** differs. The lanes are still separate elements, but each lane's step is now one rounding instead of two, which is rung 7 inside rung 3. The vectorizer must emit a separate multiply and add.
    - **Threads over row blocks:** identical. Each thread owns its rows of `c`; the private buffers only hold copies. What it must prove is that the row blocks do not overlap, [P13](p13-multithreading.md#the-proof-a-compiler-needs-and-the-one-it-does-not-have-yet)'s partition argument.
    - **`kc` = 24 with zero-started panels:** differs. The zero-started panels regroup each element's sum at every panel boundary, whatever `kc` is (below 64). That 24 does not divide 64 only adds a short last panel. A search restricted to identical variants excludes this candidate before it is ever timed.

## Did the rung help? Comparing two measurements

Once a rung passes the gate, the second gate asks whether it is faster than the rung below it. [P1](p1-measure-first.md#what-to-report-instead-of-one-number) built the tool: many independent runs, a median, and a bootstrap confidence interval. Comparing two rungs adds one question: how do you decide from two intervals that one rung beats the other?

Hoefler and Belli give the simplest sound rule: if two 95% confidence intervals do not overlap, you can be 95% confident the difference is real. The converse does not hold: overlapping intervals do not show that there is no difference.[^hb15] A direct alternative is to compute an interval for the quantity you want to report, the **speedup**, here the ratio of the old rung's median time to the new rung's. The bootstrap from P1 does this with one change: in each round, resample both sets of timings, take each resample's median, and record their ratio. The middle 95% of those ratios is the interval.

--8<-- "includes/examples/optimize/p16-capstone/speedup_ci.cpp.md"

The samples are made-up ticks, chosen to show the two outcomes. From rung `a` to rung `b`, the whole interval lies above 1.0: the new rung is faster, by a ratio between 1.32 and 1.38. From `b` to `c`, the medians differ by one tick and the interval, from 0.98 to 1.03, contains 1.0. The honest report for that step is "no difference shown", not a 0.3% speedup. Both steps contain one slow outlier per sample, and neither decision moved because of it, which is the reason to summarize with medians.

<figure class="vx-figure">
<svg viewBox="0 0 760 210" role="img" aria-label="A number line of speedup ratios from 0.9 to 1.5, with a vertical line at 1.0 marking no change. The interval for a to b runs from 1.32 to 1.38 with its point at 1.35, entirely right of 1.0. The interval for b to c runs from 0.98 to 1.03 with its point at 1.00, straddling 1.0.">
<line class="vx-line" x1="80" y1="170" x2="700" y2="170"/>
<text class="vx-text-muted" x="80" y="192" text-anchor="middle">0.9</text>
<text class="vx-text-muted" x="180" y="192" text-anchor="middle">1.0</text>
<text class="vx-text-muted" x="280" y="192" text-anchor="middle">1.1</text>
<text class="vx-text-muted" x="380" y="192" text-anchor="middle">1.2</text>
<text class="vx-text-muted" x="480" y="192" text-anchor="middle">1.3</text>
<text class="vx-text-muted" x="580" y="192" text-anchor="middle">1.4</text>
<text class="vx-text-muted" x="680" y="192" text-anchor="middle">1.5</text>
<line class="vx-line" x1="180" y1="30" x2="180" y2="170" stroke-dasharray="4 3"/>
<text class="vx-text-muted" x="186" y="42">no change</text>
<text class="vx-text" x="90" y="84" text-anchor="start">a → b</text>
<rect class="vx-box-strong" x="500" y="72" width="60" height="16" rx="3"/>
<circle class="vx-dot" cx="530" cy="80" r="5"/>
<text class="vx-text-accent" x="570" y="85">faster: interval above 1.0</text>
<text class="vx-text" x="90" y="134" text-anchor="start">b → c</text>
<rect class="vx-box-accent" x="160" y="122" width="50" height="16" rx="3"/>
<circle class="vx-dot" cx="180" cy="130" r="5"/>
<text class="vx-text-accent" x="220" y="135">no difference shown: interval contains 1.0</text>
</svg>
<figcaption>Figure 4. The two speedup intervals from the example, on one axis. The decision reads straight off the picture: a step whose interval lies wholly right of 1.0 is a speedup; a step whose interval crosses 1.0 has not shown one. The ticks behind these intervals are made up.</figcaption>
</figure>

Three details decide whether such an interval means anything. The runs have to be independent: the same binary, launched fresh, with the rungs interleaved rather than all of rung `a` first and all of rung `b` later, so that a machine that warms up or throttles does not favor one of them. The repetitions have to be counted at every level that has them, processes launched and iterations inside each, as Kalibera and Jones insist.[^kj13] And the comparison should be planned before the numbers are seen. Georges, Buytaert and Eeckhout showed that the reporting habits common in their field, including taking the best run, can lead to the wrong conclusion about which system is faster.[^georges07]

??? check "Rung 5's interval against rung 4 is [0.97, 1.09]. A colleague says packing did nothing and should be dropped. What do you answer?"

    The interval contains 1.0, so this measurement has not shown a speedup, and it has not shown the absence of one either: the true ratio could be anywhere from a 3% loss to a 9% gain. It also answers a narrow question, rung 5 against rung 4 at this shape. Packing is there to make the micro-kernel of rung 6 possible, so the useful measurement is rung 6 with packing against rung 6 without it, and at a shape large enough that the operands no longer fit in the caches, which the 64 by 64 kernel's do.

## The finish line: three libraries, fairly

The top of the ladder is compared with libraries whose authors have spent years on this one routine. Three are widely used and run on the owner's machine: Apple's **Accelerate**, **OpenBLAS** and **BLIS**.[^accelerate][^openblas][^blis-gh] A comparison with them is fair when both sides compute the same product, `f32` in, `f32` out, at the same shapes, with the same number of threads, measured by the same protocol.

The thread count is where comparisons most often go wrong, because each library controls it differently:

- **Accelerate** offers two settings through `BLASSetThreading`, available from macOS 15: `BLAS_THREADING_SINGLE_THREADED`, and `BLAS_THREADING_MULTI_THREADED`, in which Accelerate decides how many threads to use. The setting is stored per thread, so it has to be made on the thread that calls the BLAS routine.[^accelerate-sdk] There is no setting for "exactly four", so a multithreaded comparison against Accelerate compares your chosen thread count with Accelerate's choice, and the report has to say so.
- **OpenBLAS** reads `OPENBLAS_NUM_THREADS` first, then `GOTO_NUM_THREADS`, then `OMP_NUM_THREADS`, except in builds made with `USE_OPENMP=1`, which use `OMP_NUM_THREADS`.[^openblas]
- **BLIS** reads `BLIS_NUM_THREADS` for a total, or per-loop variables such as `BLIS_JC_NT` and `BLIS_IC_NT` for a manual split.[^blis-mt]

A report should set every one of these variables explicitly, for every run, and print them. An unset variable is a value chosen by whoever set up the shell.

The bits gate does not apply to the libraries. None of them promises rung 0's order of additions, and BLIS's documented micro-kernel contract is rung 6b's shape, so bit differences are expected and say nothing about correctness. A comparator is checked against rung 0 with a **tolerance**: the largest difference allowed between two answers that are both acceptable. Choose it, justify it in the report, and print it. This is the only place in the ladder where a tolerance belongs; a Vortex rung that needs one to pass has failed the gate.

One published comparison is worth knowing and worth distrusting in equal measure. Boehm reports that the NumPy code his post measures ran in 1 ms with Accelerate on a 2021 MacBook Pro with an M1 Pro, against about 8 ms with OpenBLAS on the same laptop, and attributes the gap to undocumented matrix instructions only Apple's software could use.[^boehm] That is one author's attribution on one chip.

The owner's machine is an M4 Pro, whose CPU reports `FEAT_SME` and `FEAT_SME2`, the Arm matrix extension.[^local] Remke and Breuer, generating SME kernels on an M4, report an achievable throughput of over 2.3 `f32` TFLOPS.[^hellosme] That number belongs to their M4 and their kernels; on the M4 Pro, the only way to know is to measure. It is also the reason rung 10 exists: if a library uses the matrix unit and your ladder does not, a large gap at the top says little about rungs 0 to 9.

??? check "Your rung 8 runs on four threads, and your Accelerate run calls `BLASSetThreading(BLAS_THREADING_MULTI_THREADED)`. Accelerate wins by a wide margin. What can the report conclude?"

    Only that Accelerate's own threading choice beat your four threads at that shape. The thread counts were not matched, so the result mixes the quality of the kernel with the number of cores used. For a like-for-like kernel comparison, compare single-threaded runs (rung 6 or 9 against Accelerate with `BLAS_THREADING_SINGLE_THREADED`), and report the multithreaded row separately, labelled with the fact that Accelerate chose its own thread count. Also check that the `BLASSetThreading` call was made on the thread that calls `cblas_sgemm`, since the setting is per thread.

## An upper bound for every row

A table of GFLOP/s says how fast each rung ran, not how fast it could have run. Hoefler and Belli's eleventh rule asks for exactly that context: show upper performance bounds where possible, and they point to the roofline model's bandwidth and flop-rate bounds as an example.[^hb15] [P3](p3-roofline.md#measuring-your-own-machine) explains how to measure the two numbers a roofline needs, a peak flop rate and a peak DRAM bandwidth, on your own machine; the roofline itself comes from Williams, Waterman and Patterson.[^roofline09]

For the ladder, two refinements matter. The first is which peak to use. A strict build never fuses, so its arithmetic ceiling is the rate of separate multiplies and adds, and a rung-7 build's ceiling is the rate of fused ones. Measure both ceilings the way P3 measures a peak, and report each rung against the ceiling it is allowed to reach; the gap between the two ceilings is part of the price of strictness.

The second is which shape to report. At 64 by 64, the three `f32` matrices take 48 KiB, which fits in the M4 Pro's 128 KiB performance-core L1 data cache[^local], so the DRAM roofline says little about that shape; [P3](p3-roofline.md#which-roof) discusses which roof applies to a kernel that fits in a cache. Report the ladder at a shape whose operands do not fit in the last-level cache as well, where the tiling rungs have something to do. Finally, a row's share of its ceiling depends on which side of the ridge point the kernel sits at that shape: left of it, the bandwidth line is the ceiling, and quoting the arithmetic peak instead makes a memory-bound rung look slow. The share never says whether a rung is good; the comparator rows do.

The throughput for a product of an `M` by `K` matrix and a `K` by `N` matrix is `2·M·N·K / t` flops per second, counting each multiply-add as two operations, the convention [P3](p3-roofline.md#a-question-the-flop-count-alone-cannot-answer) uses. With `t` in nanoseconds, the same expression gives GFLOP/s.

## The report you fill in

Here is the report this chapter asks for. The tables are empty because no run on this machine has produced their numbers yet; every row must come from your own runs, under P1's protocol.

Record beside the tables: the CPU model as the harness queried it, the operating system version, the compiler and its version, every flag, the thread-control settings of every library, the library versions, the shapes, the repetition counts at each level, the tolerance used for comparators, and the date.

| Kernel | Bits | Threads | Median GFLOP/s | 95% interval | Speedup over the rung below, 95% interval | Share of its ceiling |
| --- | --- | --- | --- | --- | --- | --- |
| Rung 0, naive | baseline | 1 | | | | |
| Rung 1, scalar cleanup | identical | 1 | | | | |
| Rung 2, interchanged | identical | 1 | | | | |
| Rung 3, vectorized | identical | 1 | | | | |
| Rung 4, tiled | identical | 1 | | | | |
| Rung 5, packed | identical | 1 | | | | |
| Rung 6, register-blocked | identical | 1 | | | | |
| Rung 9, tuned | identical | 1 | | | | |
| Rung 8, threads | identical | | | | | |
| Accelerate, single-threaded | tolerance | 1 | | | | |
| Accelerate, its own threading | tolerance | chosen by Accelerate | | | | |
| OpenBLAS | tolerance | | | | | |
| BLIS | tolerance | | | | | |

The second table is the **price of strictness**: what the opt-in rungs would buy if the programmer allowed them. It is the number a future relaxed floating-point option in Vortex would have to justify.

| Opt-in variant | Starts from | Median GFLOP/s | 95% interval | Speedup over its strict rung, 95% interval | Elements differing from rung 0 on the gate's inputs |
| --- | --- | --- | --- | --- | --- |
| 6b, zero-started accumulators | rung 6 | | | | |
| 7, fused multiply-add | rung 6 | | | | |
| 8b, split `k` | rung 8 | | | | |

## For Vortex

!!! vortex "Exercise"

    **Build** the ladder report for your own compiler, on top of `vortex-bench` from [P1](p1-measure-first.md#for-vortex) and the remark stream from [O1](o1-optimizer-contract.md#for-vortex).

    1. **A way to produce each rung.** Each rung your compiler supports must be selectable by name from the command line (for example `--ladder=rung4`) and compiled from the one unchanged stage 10 source, so the ladder measures the compiler and not hand-edited copies. Rungs you have not built yet are reported as missing, not skipped silently.
    2. **The bits gate**, run before any timing. Compare every rung's output with rung 0's as bit patterns, element by element, on at least three input sets: random values, values of widely different magnitudes, and inputs that produce signed zeros. Run it at two shapes, one of which no tile or unroll factor divides evenly. A rung that differs and is not a listed opt-in is not timed, and the harness prints the first differing element, its index and both bit patterns.
    3. **Opt-in labels.** A rung that changes bits on purpose (6b, 7, 8b) runs only when the command line asks for it, carries the label in every output, and is never printed in the same table as the strict rungs without that label.
    4. **Remarks per rung.** Each rung's build emits the remarks that justify it, in O1's format: what was done and why it was legal, or what was not done and why. A rung whose remark stream is empty fails the report.
    5. **Comparators.** A small C or C++ driver that calls `cblas_sgemm` from Accelerate, and from OpenBLAS or BLIS if installed, on the same inputs, checked against rung 0 with a tolerance you choose and print, with each library's thread control set explicitly as this chapter describes.
    6. **The report.** For every rung and comparator: the median, the 95% interval, the speedup over the rung below with its own interval, and the machine facts, as JSON and as the two tables of this chapter.

    **Not yet:** the tuner's search ([P15](p15-choosing-parameters.md)); SME code generation; GPU comparators ([G10](../gpu/g10-matmul-ladder.md)); automatic outlier removal; any number in the report that this run did not measure.

    **Proof that it works:**

    - A deliberately broken rung, one with two indices swapped, is rejected by the gate, and so is a rung that starts each sum at its first product; the second is caught only by the signed-zero input.
    - The report run twice, back to back, on an idle machine gives speedup intervals that overlap for every rung. A third run with a heavy background load gives wider intervals, not a quietly shifted median.
    - An opt-in rung appears in the JSON and in both tables with its label, and in no strict row.
    - A test run with `OPENBLAS_NUM_THREADS` unset and `OMP_NUM_THREADS=2` is refused by the harness until the comparator's thread count is set explicitly.

## Key ideas

!!! recap "Questions you can now answer"

    - **What decides whether a rung may run by default?** Whether it keeps rung 0's bits: every element's additions in rung 0's order, from +0.0. Rungs 6b, 7 and 8b do not, so they are opt-in.
    - **Why does the bits gate compare bit patterns instead of using `==`?** Because `==` calls +0.0 and −0.0 equal and a NaN unequal to itself, so it hides some differences and invents others.
    - **Why is passing the gate on random inputs not enough?** Random inputs rarely contain signed zeros or widely different magnitudes, where regrouped or folded sums differ; the gate needs adversarial inputs, and the proof comes from the rung's loop structure.
    - **How do you decide that one rung beats another?** With a confidence interval for the ratio of their medians, or two non-overlapping intervals; an interval that contains 1.0 shows no difference.
    - **What makes a comparison with Accelerate, OpenBLAS or BLIS fair?** The same precision, shapes and thread count, each library's thread control set explicitly, and a tolerance check in place of the bits gate.
    - **What does a row's "share of its ceiling" depend on?** A measured ceiling for the operations the rung is allowed to use, and which side of the ridge point the kernel sits at that shape.

## Where this comes back

!!! next "You will use this again in"

    - [G10. The GPU matmul ladder](../gpu/g10-matmul-ladder.md): *ladder*, *bits gate*, *comparison with a vendor library*
    - [G11. Matrix units](../gpu/g11-matrix-units.md): *SME*, *a matrix unit at the top of the ladder*
    - [G14. Measuring GPU code](../gpu/g14-measuring-gpu-code.md): *median and interval*, *speedup interval*
    - [M9. Schedules as IR: the transform dialect](../mlir/m9-transform-dialect.md): *one computation, many schedules*, *legality before profitability*
    - [E4. Testing back ends](../backend/e4-testing-backends.md): *comparison against a reference*, *adversarial inputs*

## Sources and further reading

Read Hoefler and Belli's twelve rules in full before writing any performance report; they cover mistakes this chapter only touches. For the libraries' structure, read Smith and coauthors' paper next to BLIS's kernel documentation.

[^decisions-numbers]: Vortex documentation, [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56): each `f32` and `f64` operation is one IEEE 754 operation, rounded to nearest; no contraction, reassociation, reordering or wider evaluation.
[^llvm-vec]: LLVM Project, "Auto-Vectorization in LLVM", section "Reductions": floating-point reductions are vectorized only with at least `-fassociative-math -fno-signed-zeros -fno-trapping-math` on most targets; on AArch64 and RISC-V, ordered reductions that keep the exact result. <https://llvm.org/docs/Vectorizers.html>
[^clang-um]: LLVM Project, "Clang Compiler User's Manual", option `-ffp-contract`: `on` fuses within one statement and is the default for languages other than CUDA and HIP. <https://clang.llvm.org/docs/UsersManual.html>
[^langref]: LLVM Project, "LLVM Language Reference Manual", section "Fast-Math Flags", the `nsz` and `contract` flags. <https://llvm.org/docs/LangRef.html#fast-math-flags>
[^drepper]: Ulrich Drepper, "What Every Programmer Should Know About Memory", 2007, section 6.2.1 and its table of matrix multiplication results on a 2,666 MHz Intel Core 2: the transposed version at 23.4% and the sub-matrix version at 17.3% of the original's cycles; a footnote sets rounding aside. <https://lwn.net/Articles/255364/> (full text: <https://www.akkadia.org/drepper/cpumemory.pdf>)
[^boehm]: Simon Boehm, "Fast Multidimensional Matrix Multiplication on CPU from Scratch", August 2022: the timing table on an Intel i7-6700 built with `-O3 -march=native -ffast-math` (1512 ms, 89 ms, 70 ms, 16 ms), OpenMP with eight threads, the 32 flops per cycle FMA bound, and the M1 Pro timings of 1 ms with Accelerate and about 8 ms with OpenBLAS. <https://siboehm.com/articles/22/Fast-MMM-on-CPU>
[^smith14]: Tyler M. Smith, Robert van de Geijn, Mikhail Smelyanskiy, Jeff R. Hammond and Field G. Van Zee, "Anatomy of High-Performance Many-Threaded Matrix Multiplication", *IPDPS* 2014, the subsection on parallelizing the fourth loop (indexed by `pc`): threads update the same block of `c`, so they need zeroed copies and a reduction, and only when `c` is small. <https://doi.org/10.1109/IPDPS.2014.110> (free copy: <https://www.cs.utexas.edu/~flame/pubs/blis3_ipdps14.pdf>)
[^blis-k]: BLIS project, "Kernels HowTo", the gemm micro-kernel: `C11 := beta * C11 + alpha * A1 * B1`. <https://github.com/flame/blis/blob/master/docs/KernelsHowTo.md>
[^blis-mt]: BLIS project, "Multithreading": `BLIS_NUM_THREADS`, the per-loop variables, and why the fourth loop is not parallelized. <https://github.com/flame/blis/blob/master/docs/Multithreading.md>
[^blis-gh]: BLIS project repository. <https://github.com/flame/blis>
[^openblas]: OpenMathLib, OpenBLAS README: "The priorities are `OPENBLAS_NUM_THREADS` > `GOTO_NUM_THREADS` > `OMP_NUM_THREADS`", and the exception for `USE_OPENMP=1` builds. <https://github.com/OpenMathLib/OpenBLAS>
[^accelerate]: Apple, Accelerate framework documentation and its BLAS pages. <https://developer.apple.com/documentation/accelerate> ; <https://developer.apple.com/documentation/accelerate/blas>
[^accelerate-sdk]: Apple, macOS SDK, `vecLib.framework/Headers/thread_api.h` (read 2026-09-24 in the Xcode SDK on the owner's machine): `BLASSetThreading`, its two settings, availability from macOS 15.0, and "This setting is per thread".
[^hellosme]: Stefan Remke and Alexander Breuer, "Hello SME! Generating Fast Matrix Multiplication Kernels Using the Scalable Matrix Extension", arXiv:2409.18779, September 2024: the abstract's achievable throughput of over 2.3 FP32 TFLOPS on M4. <https://arxiv.org/abs/2409.18779>
[^local]: Owner's machine, Apple M4 Pro, macOS 27: `sysctl hw.optional.arm.FEAT_SME hw.optional.arm.FEAT_SME2` both report 1 (checked 2026-09-24); `sysctl hw.perflevel0.l1dcachesize` reports 128 KiB (checked 2026-09-23).
[^roofline09]: Samuel Williams, Andrew Waterman and David Patterson, "Roofline: An Insightful Visual Performance Model for Multicore Architectures", *Communications of the ACM* 52(4), 2009. <https://doi.org/10.1145/1498765.1498785> (free copy: <https://people.eecs.berkeley.edu/~kubitron/cs252/handouts/papers/RooflineVyNoYellow.pdf>)
[^hb15]: Torsten Hoefler and Roberto Belli, "Scientific Benchmarking of Parallel Computing Systems: Twelve Ways to Tell the Masses When Reporting Performance Results", *SC* 2015: rule 7 and the section "Comparing Statistical Data" (non-overlapping intervals), rule 11 (upper performance bounds, with the roofline as an example). <https://doi.org/10.1145/2807591.2807644> (free copy: <https://htor.inf.ethz.ch/publications/img/hoefler-scientific-benchmarking.pdf>)
[^georges07]: Andy Georges, Dries Buytaert and Lieven Eeckhout, "Statistically Rigorous Java Performance Evaluation", *OOPSLA* 2007: the abstract. <https://doi.org/10.1145/1297027.1297033> (free copy: <https://dri.es/files/oopsla07-georges.pdf>)
[^kj13]: Tomas Kalibera and Richard Jones, "Rigorous Benchmarking in Reasonable Time", *ISMM* 2013. <https://doi.org/10.1145/2464157.2464160> (free copy: <https://kar.kent.ac.uk/33611/>)
