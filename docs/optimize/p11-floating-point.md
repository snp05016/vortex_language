# P11. Floating point under optimization

<p class="page-intro">Every floating-point operation rounds, so two programs that are equal in real arithmetic can print different digits. This chapter gives a Vortex compiler one question that sorts every transformation of the matmul kernel into those that keep each bit of the answer and those that change it, and shows where C compilers, LLVM's flags and the hardware quietly make that choice for you.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 55 minutes · Builds on: [O1. The optimizer's contract](o1-optimizer-contract.md), [Build v0.1, stage 10](../compiler/guide/stage-10-matrix-multiplication.md), [P10. Vectorization](p10-vectorization.md)</p>

???+ remember "Before you start, remember"

    ??? question "Which four things may a v0.1 Vortex compiler never do to an `f32` or `f64` operation, even while folding constants?"

        Contract it with another operation, reassociate or reorder it, evaluate it in a wider format, or flush a subnormal value to zero. Each operation is one IEEE 754 operation, rounded to nearest with ties to even ([decision 56](../decisions/numbers.md#d56)).

        Introduced in [the language tour's floating-point rules](../language-tour/06-runtime-and-numerical-rules.md#floating-point-behavior).

    ??? question "Why is rewriting `x + 0.0` to `x` wrong for `f32`, while rewriting `x + (-0.0)` to `x` is right?"

        At `x = -0.0`, `-0.0 + 0.0` is `+0.0`, so the first rewrite changes the sign of a zero. Adding `-0.0` returns every input unchanged.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#floating-point-identities-that-are-false).

    ??? question "What separates an ordered vectorized reduction from a reassociated one?"

        An ordered reduction folds each lane into one running total, lane 0 first, so it adds in the scalar loop's order and keeps its bits. A reassociated one keeps a partial sum per lane and combines the partial sums at the end, which regroups the additions.

        Introduced in [P10. Vectorization](p10-vectorization.md#reductions-ordered-or-reassociated).

    ??? question "In the stage 10 kernel, in what order does the inner loop add its products into `sum`?"

        One at a time, for `k` from 0 to 63 in increasing order, into a running total that starts at `0.0`.

        Introduced in [Build v0.1, stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for).

!!! goals "In this chapter"

    - Explain why rounding at every operation breaks associativity, and measure a rounding error in ulps.
    - Classify any transformation of the matmul kernel as bit-preserving or not, from one question about the order of each output's additions.
    - Work a fused multiply-add through by hand, and explain why Clang's default contraction can give different bits in two builds of the same loop.
    - Read LLVM's fast-math flags, and say which rewrite each one permits and which inputs that rewrite gets wrong.
    - Choose a reference and a comparison for a floating-point test, and compare summation orders by their error bounds and their measured error.

## Why `(a + b) + c` is not always `a + (b + c)`

Real addition is associative: for any three real numbers, `(a + b) + c` and `a + (b + c)` are the same number. A **floating-point** value is not a real number. It is one of a finite set of numbers, spaced unevenly along the number line, and every `f32` operation computes the exact real result and then **rounds** it: it replaces the result with the nearest value the format can hold. When the exact result lies exactly halfway between two neighbours, the rule is **round to nearest, ties to even**: pick the neighbour whose last significand bit is 0.[^ieee754] Rounding happens once per operation, so two groupings round at different points, and nothing forces them to land on the same float.

Here is a case where they do not:

--8<-- "includes/examples/optimize/p11-floating-point/associativity.cpp.md"

An `f32` has a 24-bit **significand**, the digits of the number: 23 bits are stored and a leading 1 is implied.[^ieee754] Between $2^{23}$ and $2^{24}$ the representable values are therefore exactly the integers, one apart. From $2^{24}$ = 16,777,216 up to $2^{25}$ they are two apart, because the format has no bit left to say which odd integer is meant. Figure 1 shows the gap doubling at that boundary.

<figure class="vx-figure">
<svg viewBox="0 0 760 260" role="img" aria-label="A number line of f32 values near 2 to the 24, where the gap between representable values doubles from 1 to 2, and the exact sum 16,777,217 rounds down to 16,777,216" aria-describedby="p11-f1-desc">
<title id="p11-f1-title">The gap between representable f32 values doubles at 2^24</title>
<desc id="p11-f1-desc">A horizontal number line with eight tick marks. The three leftmost ticks, at 16,777,213, 16,777,214 and 16,777,215, are solid and one apart: every integer here is representable. The fourth tick, at 16,777,216, is taller and labelled 2 to the 24. To its right the ticks at 16,777,218 and 16,777,220 are solid, and the ticks at 16,777,217 and 16,777,219 are dashed, because those odd integers are not representable. An arrow labelled "16,777,216 + 1 = 16,777,217, exact" starts above the dashed 217 tick and curves down to the 216 tick; the text below says the exact sum is a tie and rounds to the even neighbour. A legend at the bottom explains solid and dashed ticks.</desc>
<defs><marker id="p11-f1-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<line class="vx-line" x1="60" y1="150" x2="700" y2="150"/>
<line class="vx-line" x1="140" y1="142" x2="140" y2="158"/>
<line class="vx-line" x1="220" y1="142" x2="220" y2="158"/>
<line class="vx-line" x1="300" y1="142" x2="300" y2="158"/>
<line class="vx-line" x1="380" y1="132" x2="380" y2="168"/>
<line class="vx-line" x1="460" y1="142" x2="460" y2="158" stroke-dasharray="4 4"/>
<line class="vx-line" x1="540" y1="142" x2="540" y2="158"/>
<line class="vx-line" x1="620" y1="142" x2="620" y2="158" stroke-dasharray="4 4"/>
<line class="vx-line" x1="700" y1="142" x2="700" y2="158"/>
<text class="vx-text-muted" x="140" y="182" text-anchor="middle">…213</text>
<text class="vx-text-muted" x="220" y="182" text-anchor="middle">…214</text>
<text class="vx-text-muted" x="300" y="182" text-anchor="middle">…215</text>
<text class="vx-text-accent" x="380" y="118" text-anchor="middle">2^24</text>
<text class="vx-text" x="380" y="182" text-anchor="middle">…216</text>
<text class="vx-text-muted" x="460" y="182" text-anchor="middle">…217</text>
<text class="vx-text-muted" x="540" y="182" text-anchor="middle">…218</text>
<text class="vx-text-muted" x="620" y="182" text-anchor="middle">…219</text>
<text class="vx-text-muted" x="700" y="182" text-anchor="middle">…220</text>
<text class="vx-text-muted" x="220" y="110" text-anchor="middle">gap 1</text>
<text class="vx-text-muted" x="620" y="110" text-anchor="middle">gap 2</text>
<text class="vx-text" x="500" y="42" text-anchor="middle">16,777,216 + 1 = 16,777,217, exact</text>
<path class="vx-flow" d="M460 50 C 460 80, 400 90, 384 128" marker-end="url(#p11-f1-head)"/>
<text class="vx-text-muted" x="380" y="214" text-anchor="middle">a tie between …216 and …218: rounds to the even one, …216</text>
<line class="vx-line" x1="60" y1="244" x2="90" y2="244"/>
<text class="vx-text-muted" x="98" y="248">representable f32</text>
<line class="vx-line" x1="300" y1="244" x2="330" y2="244" stroke-dasharray="4 4"/>
<text class="vx-text-muted" x="338" y="248">not representable: rounds to a neighbour</text>
</svg>
<figcaption>Figure 1. Below 2<sup>24</sup> every integer is an <code>f32</code>; from 2<sup>24</sup> on, only the even ones are. The exact sum 16,777,217 lies halfway between two floats, and ties go to the even significand.</figcaption>
</figure>

In the example, `a + b` is exactly 16,777,217, halfway between 16,777,216 and 16,777,218. Ties go to the even significand, so the sum becomes 16,777,216, and adding `c` gives 0. The other grouping computes `b + c` first: −16,777,215 lies below the boundary, so it is exact, and adding `a` gives 1. Every step followed IEEE 754. The two answers differ because the rounding happened at different points.

Two measures of rounding error come up throughout this chapter. An **ulp**, a unit in the last place, is the gap between a float and its neighbour, so it grows with the magnitude: 1 between $2^{23}$ and $2^{24}$, 2 from $2^{24}$ to $2^{25}$. **Machine epsilon**, written ε, bounds the relative error of one correctly rounded operation. Goldberg defines it as half the gap between 1 and the next float, which for `f32` is $2^{-24}$.[^goldberg91] The first measure says how far apart two results are; the second says how much one operation can lose.

Goldberg's survey also explains why ties go to even. Reiser and Knuth showed that with round-half-up a repeated add-then-subtract can drift upward step by step, while with round to even it cannot.[^goldberg91]

??? check "The tie in the example went to 16,777,216. Which way would a tie between 16,777,218 and 16,777,220 go?"

    To 16,777,220. In this range the last significand bit counts steps of 2 above $2^{24}$: 16,777,218 is one step (odd), 16,777,220 is two steps (even). The exact sum 16,777,219 is halfway between them, and the even neighbour wins.

## The rule every reordering pass needs

The redundancy passes of [O6](o6-redundancy.md) merge two computations only when they are the same operation on the same operands. Merging two identical `f32` multiplications never changes a bit, because the surviving operation rounds the same exact value. Reordering is different. The loop passes of [P7](p7-loop-transformations.md) and the vectorizer of [P10](p10-vectorization.md) reorder work once a dependence test says nothing forces one order. That test is about memory and values, not about how often each real number gets rounded. For wrapping integer addition, order never matters. For floating-point addition every step is its own rounding, so a new order is a new computation.

That gives one rule and one question. Think of each output value as produced by a **chain**: the sequence of roundings, in order, that turns its inputs into its final bits. Then ask of any transformation: **does it change the chain that produces some output, or only the order in which independent chains run?**

- Running different outputs' chains in a new order, side by side or on different cores: safe. Each chain is untouched.
- Changing the order of the additions inside one chain, regrouping them, removing a rounding (fusion) or changing the precision of one (a wider format, a flushed subnormal): unsafe. The chain is different, so its bits may be.

The rule is conservative. A transformation in the second group may keep the bits for your test inputs, and still change them for others. Every other section of this chapter applies the question to one family of transformations.

## The matmul kernel, sorted by that question

The stage 10 kernel computes each `c[row, column]` as `sum += a[row, k] * b[k, column]` for `k` from 0 to 63, one product at a time, into a running total that starts at `0.0`. Each output element has its own chain of 64 multiplications and 64 additions. Figure 2 sorts the rungs of the book's optimization ladder by the question.

<figure class="vx-figure">
<svg viewBox="0 0 760 320" role="img" aria-label="A decision tree splitting matmul transformations into ones that keep every bit and ones that may not" aria-describedby="p11-f2-desc">
<title id="p11-f2-title">Which transformations of the kernel keep every bit</title>
<desc id="p11-f2-desc">A root box reads: naive kernel, sum plus-equals a of row k times b of k column, k from 0 to 63, in order. Two arrows lead down. The left one reaches a box labelled bits identical: change which output runs when. It lists loop interchange, tiling with k blocks in order, vectorizing across row or column, an ordered reduction across k, micro-kernel accumulators loaded from c, packing, and threads over row or column blocks. A note below it says every c element still adds its 64 products in the same order. The right arrow reaches a box labelled bits may differ: change one output's chain. It lists fused multiply-add, per-lane partial sums across k, split-K across threads, accumulators that start at zero for each kc panel, and a wider accumulator. A note below it says the same 64 products are grouped differently or rounded a different number of times.</desc>
<defs><marker id="p11-f2-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box-strong" x="180" y="16" width="400" height="46" rx="4"/>
<text class="vx-text" x="380" y="35" text-anchor="middle">naive kernel</text>
<text class="vx-mono" x="380" y="54" text-anchor="middle">sum += a[row,k] * b[k,column], k = 0..63</text>
<path class="vx-flow" d="M300 62 L190 110" marker-end="url(#p11-f2-head)"/>
<path class="vx-flow" d="M460 62 L570 110" marker-end="url(#p11-f2-head)"/>
<rect class="vx-box-accent" x="20" y="114" width="340" height="140" rx="4"/>
<text class="vx-text-accent" x="190" y="134" text-anchor="middle">bits identical: which output runs when</text>
<text class="vx-text" x="34" y="158">interchange; in-order k tiling</text>
<text class="vx-text" x="34" y="178">vectorize across row or column</text>
<text class="vx-text" x="34" y="198">ordered reduction across k</text>
<text class="vx-text" x="34" y="218">accumulators loaded from c; packing</text>
<text class="vx-text" x="34" y="238">threads over row or column blocks</text>
<rect class="vx-box-bad" x="400" y="114" width="340" height="140" rx="4"/>
<text class="vx-text-accent" x="570" y="134" text-anchor="middle">bits may differ: one output's chain</text>
<text class="vx-text" x="414" y="158">fused multiply-add (contraction)</text>
<text class="vx-text" x="414" y="178">per-lane partial sums across k</text>
<text class="vx-text" x="414" y="198">split-K across threads</text>
<text class="vx-text" x="414" y="218">accumulators zeroed per kc panel</text>
<text class="vx-text" x="414" y="238">a wider accumulator (f64 for f32)</text>
<text class="vx-text-muted" x="190" y="282" text-anchor="middle">every c element still adds its</text>
<text class="vx-text-muted" x="190" y="300" text-anchor="middle">64 products in the same order</text>
<text class="vx-text-muted" x="570" y="282" text-anchor="middle">the same 64 products, grouped differently</text>
<text class="vx-text-muted" x="570" y="300" text-anchor="middle">or rounded a different number of times</text>
</svg>
<figcaption>Figure 2. The ladder's rungs, sorted by one question: does the rung change the chain of roundings that builds one output element, or only the order across elements?</figcaption>
</figure>

| Transformation | Bits | Why |
| --- | --- | --- |
| Loop interchange (`ijk` to `ikj`) | identical | Only the order across `c` elements changes. |
| Tiling `i`, `j` and `k`, with `c` accumulated in place | identical | For each element the `k` tiles still run in increasing order. |
| Vectorizing across `row` or `column` | identical | Each lane holds a different output element; no lanes share a sum. |
| Vectorizing across `k` as an ordered reduction | identical | The lanes are folded into one running total in index order ([P10](p10-vectorization.md#reductions-ordered-or-reassociated)).[^llvm-vec][^langref] |
| Vectorizing across `k` with per-lane partial sums | differs | The partial sums regroup the additions; LLVM builds them only with reassociation allowed.[^llvm-vec] |
| Threads over `row` or `column` blocks | identical | Each thread owns whole chains.[^smith14] |
| Threads over `k` ("split-K"), then a final sum | differs | Each thread sums a part of the range into its own copy of `c`, and the copies are added afterwards.[^smith14] |
| Packing `a` and `b` into contiguous panels | identical | Packing copies values; it computes nothing.[^goto08] |
| Micro-kernel whose accumulators are loaded from `c` | identical | The running sum continues from one panel to the next ([P12](p12-fast-gemm.md#two-ways-to-accumulate)). |
| Micro-kernel whose accumulators start at zero for each `kc` panel | differs when `k` is longer than `kc` | BLIS's contract is `C11 := beta * C11 + alpha * A1 * B1`: each panel's product is summed first, then added to `c`.[^blis-k] |
| Fusing each multiply and add (FMA contraction) | differs | One rounding replaces two; the next section works it through. |
| Accumulating in `f64` and rounding once at the end | differs | The chain rounds to a different format at every step. |

Because `c` is reached through `&mut`, no lane or thread shares an element with another ([O9](o9-alias-analysis.md#for-vortex)), so the table never turns on aliasing. It turns only on arithmetic order. Every rung in the identical column is already justified by an argument about control flow or memory, and that argument carries over for free. Every rung in the other column needs a numerical permission that no earlier chapter granted, and [decision 56](../decisions/numbers.md#d56) withholds it in v0.1.

??? check "A future Vortex pass unrolls the `k` loop by 4 and keeps the single accumulator `sum`, adding the four products in order in each unrolled trip. Another unrolls by 4 and gives each of the four products its own accumulator, adding the four accumulators after the loop. Which column does each belong in?"

    The first is identical: each element's chain is the same 64 additions in the same order, with fewer loop tests. The second differs: it is the per-lane partial-sum shape without vectors, four chains of 16 additions joined at the end, which regroups the sum.

## Fused multiply-add: one rounding instead of two

IEEE 754 defines a **fused multiply-add** (FMA) as one operation that computes `a * b + c` as if with unbounded range and precision, and rounds once, at the end.[^ieee754][^cppref-fma] Computed the ordinary way, `a * b` is rounded to the nearest `f32` first, and the rounded product is then added to `c` and rounded again. The second example builds a case where the two differ:

--8<-- "includes/examples/optimize/p11-floating-point/fma_rounding.cpp.md"

### Following the bits by hand

Every number in the example is a short sum of powers of two, so it can be followed exactly. The spacing of floats between 1 and 2 is $2^{-23}$.

1. `a` is the float after 1, so $a = 1 + 2^{-23}$. `b` is the float after that, $b = 1 + 2^{-22}$, and $c = -a$.
2. The exact product is $ab = 1 + 2^{-22} + 2^{-23} + 2^{-45} = 1 + 3 \cdot 2^{-23} + 2^{-45}$.
3. Two roundings. $2^{-45}$ is far below half the spacing ($2^{-24}$), so the product rounds to $1 + 3 \cdot 2^{-23}$ and the $2^{-45}$ is gone. Adding $c$ gives $3 \cdot 2^{-23} - 2^{-23} = 2^{-22}$, and that subtraction is exact. The result prints as `0x1p-22`.
4. One rounding. The fused operation keeps the exact product, so the exact result is $2^{-22} + 2^{-45} = 2^{-22}(1 + 2^{-23})$. That is a float: the one after $2^{-22}$. It is exact, and prints as `0x1.000002p-22`.

The two results are one ulp apart, and the ulp is exactly the $2^{-45}$ that step 3 threw away. Figure 3 shows why a loss too small to matter in the product decides the last bit of the result: the subtraction cancels the leading bits, and the result's spacing is $2^{-45}$, not $2^{-23}$.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Two number lines. Near the product, the tiny term 2 to the minus 45 is rounded away. Near the result, after cancellation, the same term is a whole unit in the last place" aria-describedby="p11-f3-desc">
<title id="p11-f3-title">The same lost bit at two scales</title>
<desc id="p11-f3-desc">Top: a number line near the product a times b, with floats at 1 plus 2 times 2 to the minus 23, 1 plus 3 times 2 to the minus 23 and 1 plus 4 times 2 to the minus 23, spaced 2 to the minus 23 apart. The exact product sits a hair to the right of the middle float, offset by 2 to the minus 45, far too little to reach the next float, so it rounds to the middle float. Bottom: a number line near the result, with floats at 2 to the minus 22 and at 2 to the minus 22 plus 2 to the minus 45, spaced 2 to the minus 45 apart. The two-rounding result lands on 2 to the minus 22. The fused result lands on the next float, 2 to the minus 22 plus 2 to the minus 45. A brace between the two lines says: subtracting c cancels the leading bits, so the spacing shrinks from 2 to the minus 23 to 2 to the minus 45.</desc>
<defs><marker id="p11-f3-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="28">Near the product: spacing 2^-23</text>
<line class="vx-line" x1="80" y1="80" x2="680" y2="80"/>
<line class="vx-line" x1="140" y1="70" x2="140" y2="90"/>
<line class="vx-line" x1="380" y1="66" x2="380" y2="94"/>
<line class="vx-line" x1="620" y1="70" x2="620" y2="90"/>
<text class="vx-mono" x="140" y="112" text-anchor="middle">1 + 2·2^-23</text>
<text class="vx-mono" x="380" y="112" text-anchor="middle">1 + 3·2^-23</text>
<text class="vx-mono" x="620" y="112" text-anchor="middle">1 + 4·2^-23</text>
<circle class="vx-dot" cx="383" cy="80" r="5"/>
<text class="vx-text-accent" x="392" y="58">exact ab = 1 + 3·2^-23 + 2^-45</text>
<text class="vx-text-muted" x="380" y="134" text-anchor="middle">the extra 2^-45 is far less than half a gap: rounds to 1 + 3·2^-23</text>
<text class="vx-text-muted" x="20" y="170">subtract c = 1 + 2^-23: the leading bits cancel, and the spacing shrinks to 2^-45</text>
<text class="vx-text" x="20" y="204">Near the result: spacing 2^-45</text>
<line class="vx-line" x1="80" y1="250" x2="680" y2="250"/>
<line class="vx-line" x1="260" y1="238" x2="260" y2="262"/>
<line class="vx-line" x1="500" y1="238" x2="500" y2="262"/>
<text class="vx-mono" x="260" y="284" text-anchor="middle">2^-22</text>
<text class="vx-mono" x="500" y="284" text-anchor="middle">2^-22 + 2^-45</text>
<circle class="vx-dot" cx="260" cy="250" r="5"/>
<circle class="vx-dot" cx="500" cy="250" r="5"/>
<text class="vx-text-muted" x="260" y="228" text-anchor="middle">two roundings</text>
<text class="vx-text-accent" x="500" y="228" text-anchor="middle">fused: one rounding, exact</text>
</svg>
<figcaption>Figure 3. The fused multiply-add of the second example at two scales. The <code>2<sup>-45</sup></code> lost when the product is rounded is invisible next to 1, but after the subtraction cancels the leading bits it is a whole ulp of the result.</figcaption>
</figure>

Neither result is wrong. Each is the correctly rounded answer to a well-defined operation; they are different operations. The fused one is more accurate here, and that is typical. But `sum += a[row, k] * b[k, column]` in the stage 10 kernel has this shape on every trip, so whether a back end emits one `fmadd` or a separate `fmul` and `fadd` decides which of two numbers the program prints. [Decision 56](../decisions/numbers.md#d56) chose separate roundings, so that a golden output is the same on every conforming target. [A3](../backend/a3-floats-and-vectors.md#fused-multiply-add) shows the AArch64 instructions for both.

??? check "Finish this one yourself: `a = b = 1 + 2^-12` and `c = -(1 + 2^-11)`, all `f32`. What do `a * b + c` with two roundings and `fma(a, b, c)` return?"

    The exact product is $1 + 2^{-11} + 2^{-24}$. Near 1 the spacing is $2^{-23}$, so $2^{-24}$ is exactly half a gap: a tie. $1 + 2^{-11}$ is $1 + 4096 \cdot 2^{-23}$, an even number of steps, so the tie rounds down to it, and adding `c` gives 0. The fused operation keeps the $2^{-24}$ and returns $2^{-24}$, about $5.96 \times 10^{-8}$. It is the same kind of case as the fused multiply-add rule in [O1](o1-optimizer-contract.md#floating-point-identities-that-are-false), which turned 0 into $2^{-24}$.

## Contraction: who decides

Fusing a separate multiply and add into one operation is called **contraction**. In C and C++ it is a compiler option, and the defaults differ by compiler and by language mode.

- Clang's `-ffp-contract` takes `off`, `on`, `fast` and `fast-honor-pragmas`. For C and C++ the default is `on`, which permits fusion within one statement, as the C and C++ standards allow; `fast` fuses across statements too.[^clang-um]
- GCC's default is `off` for C in a standards-compliant mode such as `-std=c11`, and `fast` otherwise, which includes C++.[^gcc-opt]
- Clang's umbrella option `-ffp-model` names the combinations. `precise` is the default and keeps contraction `on`; `strict` turns contraction off and also assumes that the program may change the rounding mode and read the exception flags.[^clang-um]

So neither compiler's default build of a C++ reference is strict. Both may fuse the kernel's multiply-add before `-ffast-math` is ever mentioned.

### The flag is on the instruction

In LLVM IR the permission travels on each instruction, not in a global setting. The **fast-math flag** `contract` on an `fmul` and the `fadd` that uses it allows the code generator to fuse them.[^langref] Clang's `on` setting uses a different tool: it emits a call to the intrinsic `llvm.fmuladd`, which means `a * b + c` with the rounding between the multiply and the add left unspecified. The code generator fuses it when the target has a fused instruction and it is cheaper, and the Language Reference adds that fusion is not guaranteed.[^langref] "Unspecified" is the important word. It means that other decisions in the pipeline can settle the question, as the third example shows:

--8<-- "includes/examples/optimize/p11-floating-point/contract_depends.cpp.md"

The three functions are the same loop, compiled together by Apple clang 21 at `-O2` with the default `-ffp-contract=on`. In the first, a loop pragma stops the vectorizer, the multiply-add survives as `llvm.fmuladd`, and the code generator emits one `fmadd` per trip.[^clang-le] In the second, the vectorizer took the loop and built an ordered reduction, which on this target splits the multiply from the addition, so every product is rounded before it is added. Its bits match the third function, where `#pragma clang fp contract(off)` forbids fusion.[^clang-le] The same source line gave two answers, one ulp apart, in one build.

Building the default loop at `-O0` instead of `-O2` gave the fused bits again on the owner's M4 Pro (Apple clang 21, 2026-09-24): no vectorizer ran, so `llvm.fmuladd` reached the code generator intact. Under `-ffp-contract=on` the answer depends on which passes ran, and a change to an unrelated loop pragma or to the optimization level can change the output of a numerical program. `-ffp-contract=off` gave the same bits at every level tried.

### What `-ffast-math` turns on

`-ffast-math` is not one permission but a bundle. In Clang it assumes that `+` and `*` obey the algebra of real numbers, that no value is NaN or infinite, and that `+0` and `-0` are interchangeable, and it sets `-ffp-contract=fast`.[^clang-um] At the IR level each part becomes a separate flag on each instruction, and the Language Reference defines them one at a time:[^langref]

| Flag | Permits | Wrong for |
| --- | --- | --- |
| `nnan` | assuming no argument or result is NaN; if one is, the result is poison | any computation that meets a NaN |
| `ninf` | the same for plus and minus infinity | any computation that overflows or meets an infinity |
| `nsz` | treating the sign of a zero as insignificant | results whose sign of zero is observable, such as `1 / x` |
| `arcp` | replacing a division by multiplication with the reciprocal | divisors whose reciprocal is not exact, such as 10 |
| `contract` | fusing a multiply and an add | any value that the fused rounding changes |
| `afn` | substituting approximations for functions such as `sin`, `log` and `sqrt` | callers that expect the library's results |
| `reassoc` | regrouping chains of additions and multiplications | nearly every long sum |
| `fast` | all of the above | |

**Poison**, the result of breaking an `nnan` or `ninf` promise, is not a particular wrong number: it is a value that later instructions may treat as anything, which [O11](o11-undefined-behavior.md#poison-a-value-that-remembers-nothing-went-wrong) explains. A program built with `-ffast-math` that meets a NaN has lost its meaning, not only its last bit. The Language Reference adds one limit on `contract` alone: it does not permit regrouping `(a*b) + (c*d) + e` so that two fusions become possible.[^langref]

The fourth example asks LLVM's instruction combiner for four textbook rewrites, each tried once without its flag and once with it:

--8<-- "includes/examples/optimize/p11-floating-point/fmf_folds.ll.md"

Without flags InstCombine changes nothing, because each rewrite is wrong for some input. With them, `x + 0.0` becomes `x` (`nsz`), `x * 0.0` becomes `0.0` (`nnan nsz`: the product is NaN for an infinite or NaN `x` and `-0.0` for a negative one), `x / 10.0` becomes `x * 0.1` with 0.1 rounded to `f32` (`arcp`), and `(x + 1) + 2` becomes `x + 3`. That last fold needed `reassoc` and `nsz` together; with `reassoc` alone this LLVM 18.1.8 left both additions in place. `@madd_contract` comes out unchanged, because fusion happens later, in instruction selection: `llc -O2` turns it into one `fmadd`.

`-ffast-math` also reaches outside the compiled code. Without `-shared`, Clang links `crtfastmath.o`, whose static constructor sets the flush-to-zero bits of the x86 MXCSR register for the whole process, including libraries compiled without the flag.[^clang-um] GCC's `-ffast-math` additionally sets `-fexcess-precision=fast`.[^gcc-opt] A Vortex program linked into such a process would no longer get the subnormals decision 56 promises.

??? check "A C++ benchmark harness is compiled with `-O2 -ffast-math`. It calls a separately compiled strict Vortex kernel through a C interface, and prints the kernel's results. Can the flag change what the kernel computes?"

    Yes, on x86-64. The harness's link pulls in `crtfastmath.o`, which sets flush-to-zero in MXCSR for the whole process at startup, so the kernel's subnormal results and inputs become zero although the kernel was compiled strictly. The per-instruction flags cannot cross into the kernel, but the floating-point environment is shared by everything in the process. [A3](../backend/a3-floats-and-vectors.md#the-floating-point-control-and-status-registers) describes the matching control register on AArch64.

## Hidden precision: wider formats, constants and the environment

Some changes to a chain come not from reordering but from rounding to the wrong format, or at the wrong time.

**Wider formats.** A compiler may keep an intermediate result in a format wider than its type. GCC's documentation gives x87 floating point, whose registers hold 80 bits, as the example, and says that under its default, `-fexcess-precision=fast`, a program cannot predict when a value is rounded back to its declared type.[^gcc-opt] An addendum to the Oracle reprint of Goldberg's paper describes a program that one x86 compiler made print "Equal" when optimized and "Not Equal" when built for debugging: one copy of `3.0/7.0` stayed in a wide register, the other was stored.[^priest] AArch64 and SSE compute `f32` in `f32`, so this trap now appears mostly as a deliberate rewrite, such as summing `f32` values into an `f64` accumulator.

**Constants.** A value computed during compilation must be the value the target would compute. Goldberg points out that converting a constant such as `1.0E-40` at compile time can change a program's meaning, because the conversion is inexact and depends on the rounding mode.[^goldberg91] For Vortex the rule is plainer: decision 56 applies to values computed during compilation too, so a constant folder must perform one `f32` operation with one `f32` rounding, never fold in `double` and round at the end. [O5](o5-constants-and-dead-code.md#folding-by-the-programs-rules) builds such a folder.

**The environment.** LLVM assumes by default that floating-point traps are disabled, that the status flags are not observed, that the rounding mode is round to nearest and that subnormals are preserved; code that runs where these assumptions fail has undefined behavior.[^langref] Leaving them means saying so explicitly: the `denormal-fp-math` attribute states that subnormals may be flushed, and the **constrained floating-point intrinsics** carry the rounding mode and exception behavior on each operation.[^langref] On the owner's machine, `clang -ffp-model=strict` turned the kernel's multiply and add into `llvm.experimental.constrained.fmul` and `constrained.fadd` calls (Apple clang 21, 2026-09-24). Vortex needs none of this: its environment is the default one, and it must keep it that way.

## How far order can drift

A longer chain gives rounding more chances to add up. The fifth example sums two lists four ways each: in order; in four interleaved partial sums, the shape a vectorizer builds under `reassoc`; pairwise; and with Kahan's compensated summation. It prints each error in ulps of the exact sum:

--8<-- "includes/examples/optimize/p11-floating-point/summation_orders.cpp.md"

The first list is built to fail. The sequential sum reaches $2^{24}$ on its first element, and from then on every `+ 1.0` is the tie of Figure 1, broken back to the value already held: the total never moves, and all 99,999 ones are lost. The four-lane sum loses only the ones that share a partial sum with $2^{24}$, a quarter of them. The second list is ordinary, and still the sequential sum is 739 ulps off, while every other order stays within about 17.

**Pairwise summation** splits the list in half, sums each half the same way, and adds the two results. Figure 4 compares its shape with the sequential chain.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Eight terms summed two ways: a sequential chain seven additions deep, in which the first terms pass through seven roundings, and a balanced pairwise tree three additions deep, in which every term passes through three" aria-describedby="p11-f4-desc">
<title id="p11-f4-title">A chain and a tree over the same eight terms</title>
<desc id="p11-f4-desc">Left: a sequential sum of eight terms x1 to x8 drawn as a chain of seven addition nodes leaning to one side. x1 and x2 enter the first node, and each later term joins one node further along. A label says x1 passes through 7 roundings and x8 through 1. Right: a pairwise sum of the same terms drawn as a balanced binary tree: four additions of neighbouring pairs, then two, then one. A label says every term passes through 3 roundings, log base 2 of 8. A dot travels along the path from x1 to the result in each shape, taking seven steps on the left and three on the right.</desc>
<text class="vx-text" x="20" y="24">Sequential: a chain</text>
<text class="vx-mono" x="40" y="270" text-anchor="middle">x1</text>
<text class="vx-mono" x="80" y="270" text-anchor="middle">x2</text>
<text class="vx-mono" x="120" y="270" text-anchor="middle">x3</text>
<text class="vx-mono" x="160" y="270" text-anchor="middle">x4</text>
<text class="vx-mono" x="200" y="270" text-anchor="middle">x5</text>
<text class="vx-mono" x="240" y="270" text-anchor="middle">x6</text>
<text class="vx-mono" x="280" y="270" text-anchor="middle">x7</text>
<text class="vx-mono" x="320" y="270" text-anchor="middle">x8</text>
<path class="vx-line" d="M40 256 L60 232 L80 256 M60 232 L100 208 L120 256 M100 208 L140 184 L160 256 M140 184 L180 160 L200 256 M180 160 L220 136 L240 256 M220 136 L260 112 L280 256 M260 112 L300 88 L320 256"/>
<circle class="vx-box" cx="60" cy="232" r="7"/>
<circle class="vx-box" cx="100" cy="208" r="7"/>
<circle class="vx-box" cx="140" cy="184" r="7"/>
<circle class="vx-box" cx="180" cy="160" r="7"/>
<circle class="vx-box" cx="220" cy="136" r="7"/>
<circle class="vx-box" cx="260" cy="112" r="7"/>
<circle class="vx-box-accent" cx="300" cy="88" r="7"/>
<circle class="vx-dot" r="5"><animateMotion dur="7s" repeatCount="indefinite" path="M40 256 L60 232 L100 208 L140 184 L180 160 L220 136 L260 112 L300 88"/></circle>
<text class="vx-text-muted" x="20" y="54">x1 and x2 pass through 7 roundings;</text>
<text class="vx-text-muted" x="20" y="72">x8 through 1</text>
<text class="vx-text" x="420" y="24">Pairwise: a tree</text>
<text class="vx-mono" x="440" y="270" text-anchor="middle">x1</text>
<text class="vx-mono" x="480" y="270" text-anchor="middle">x2</text>
<text class="vx-mono" x="520" y="270" text-anchor="middle">x3</text>
<text class="vx-mono" x="560" y="270" text-anchor="middle">x4</text>
<text class="vx-mono" x="600" y="270" text-anchor="middle">x5</text>
<text class="vx-mono" x="640" y="270" text-anchor="middle">x6</text>
<text class="vx-mono" x="680" y="270" text-anchor="middle">x7</text>
<text class="vx-mono" x="720" y="270" text-anchor="middle">x8</text>
<path class="vx-line" d="M440 256 L460 222 L480 256 M520 256 L540 222 L560 256 M600 256 L620 222 L640 256 M680 256 L700 222 L720 256 M460 222 L500 172 L540 222 M620 222 L660 172 L700 222 M500 172 L580 112 L660 172"/>
<circle class="vx-box" cx="460" cy="222" r="7"/>
<circle class="vx-box" cx="540" cy="222" r="7"/>
<circle class="vx-box" cx="620" cy="222" r="7"/>
<circle class="vx-box" cx="700" cy="222" r="7"/>
<circle class="vx-box" cx="500" cy="172" r="7"/>
<circle class="vx-box" cx="660" cy="172" r="7"/>
<circle class="vx-box-accent" cx="580" cy="112" r="7"/>
<circle class="vx-dot" r="5"><animateMotion dur="7s" repeatCount="indefinite" path="M440 256 L460 222 L500 172 L580 112" keyPoints="0;1;1" keyTimes="0;0.43;1" calcMode="linear"/></circle>
<text class="vx-text-muted" x="420" y="54">every term passes through 3 roundings,</text>
<text class="vx-text-muted" x="420" y="72">log2 8</text>
</svg>
<figcaption>Figure 4. The same eight terms summed as a chain and as a tree. Each circle is one rounded addition; the moving dot follows <code>x1</code> to the result. In a chain the first terms are rounded at every step; in a tree each term is rounded once per level.</figcaption>
</figure>

The figure turns into an error bound. Each rounded addition multiplies the running value by a factor $(1 + \delta)$ with $|\delta| \le \varepsilon$. Goldberg shows that the sequential sum therefore equals the exact sum of slightly perturbed terms, $\sum_j x_j (1 + \delta_j)$, where a term that passes through $m$ additions can be perturbed by about $m\varepsilon$: up to $n\varepsilon$ for the first terms.[^goldberg91] In the pairwise tree every term passes through about $\log_2 n$ additions, so the same argument bounds its perturbation by about $\varepsilon \log_2 n$. For 100,000 terms that is 17 roundings per term instead of up to 99,999.

**Kahan summation** keeps a second variable, `carry`, which holds the part of the last addition that rounding threw away, and subtracts it from the next term. Goldberg's Theorem 8 bounds each term's perturbation by $2\varepsilon$, whatever $n$ is, plus a term of order $n\varepsilon^2$ that is negligible for any realistic sum.[^goldberg91] On the first list its result is the exact sum correctly rounded: the exact answer, 16,877,215, is odd, so no `f32` can hold it, and half an ulp is the best any `f32` result can do. Kahan's method is also the algorithm an algebra-minded optimizer destroys, since in real arithmetic `carry` is always zero, as [O1](o1-optimizer-contract.md#floating-point-identities-that-are-false) showed.

Two lessons follow for a compiler. First, reassociation often makes a sum more accurate: the four-lane order beat the sequential one on both lists. That is why it is tempting, and why "more accurate" is not a defense in Vortex: the program's order defines the answer, and a more accurate answer that depends on the vector width a compiler chose is a different answer. Second, a programmer who wants pairwise or compensated summation can write it in the source, where the order is part of the program and every conforming compiler must keep it.

??? check "The four-lane sum of the harmonic list is more accurate than the sequential one. Why is its error still larger than the pairwise error?"

    Each lane is itself a sequential chain of 25,000 additions, so its early terms can be perturbed by up to about $25{,}000\varepsilon$, a quarter of the sequential bound. Pairwise summation cuts every term's path to about 17 additions. Four lanes shorten the chain by a factor of four; a tree shortens it to a logarithm.

## Choosing a reference honestly

A test of a floating-point program compares its output with a **reference**, a result believed correct. The comparison can be exact, bit for bit, or within a tolerance. Each has one condition under which it means something.

A **bitwise comparison** is meaningful only when both sides promise the same chain. A Vortex program and a C++ reference agree bit for bit only if the C++ build performs the same operations in the same order with the same roundings, which means `-ffp-contract=off` and no fast-math options: decision 56 names exactly that flag for a C back end.[^clang-um] Without it, as the third example showed, a plain `-O2` reference may or may not fuse, so a mismatch could come from either side and a match could be luck. Bitwise comparison is what Vortex's golden tests use, and what [P16's bits gate](p16-capstone.md#the-bits-gate) checks for every rung.

A **tolerance comparison** is the right test when the two sides are not supposed to compute the same chain: a relaxed build against a strict one, or Vortex against a tuned BLAS that uses fused multiply-adds and its own blocking. The tolerance must come from an error bound, not a guess. For one dot product of length $n$ in the sequential order, the reasoning above bounds the error by about $n\varepsilon \sum_k |a_k b_k|$. A test that allows exactly that much, per element, fails when something real is wrong and passes when only the order changed. A fixed tolerance such as "six significant digits" does neither reliably.

Reproducibility across thread counts is a third requirement and a harder one. A parallel sum whose partitioning depends on the number of threads has a different chain for every thread count. ReproBLAS, from Berkeley, gives bitwise identical sums regardless of the number of processors, the data partitioning or the order of the reduction, at a price the project states itself: about $9n$ floating-point operations to sum $n$ values with its default settings, and a dot product 4 times slower than an optimized non-reproducible one on a single Intel Sandy Bridge core.[^reproblas] Vortex avoids the problem in v0.1 by never splitting one chain across threads ([P13](p13-multithreading.md#which-loop-in-the-kernel-to-split)).

## Your turn: classify these rungs

Each row is a change a later chapter, a tuner or a build script might make to the stage 10 kernel or its build. Decide whether it keeps every bit of every `c` element, and why. Three rows are filled in.

| Change | Bits | Why |
| --- | --- | --- |
| After interchange, load `a[row, k]` once per `k` and keep it in a register | identical | Moves a load; every addition is unchanged. |
| Split `k` into two halves, one per thread, and add the halves | differs | Two chains of 32 joined at the end. |
| Build the C++ reference with plain `-O2` | may differ | The reference itself may fuse, depending on vectorization. |
| Unroll the column loop by 4 and interleave the four elements' updates | ? | |
| Replace `sum = 0.0; ... c[row, column] = sum` with direct updates of `c[row, column]`, which starts at `0.0` | ? | |
| A tile size chosen by an autotuner, with `k` tiles run in increasing order | ? | |
| Link the Vortex program with a library built with `-ffast-math` on x86-64 | ? | |
| Constant-fold `0.1 * 3.0` by computing it in `double` and rounding once | ? | |
| Vectorize the `k` loop as an ordered reduction | ? | |
| Accumulate each `c` element in `f64`, then round | ? | |

??? check "Answers"

    - **Unrolling and interleaving the column loop**: identical. Four different outputs' chains run side by side; each chain is untouched.
    - **Updating `c` directly**: identical, as long as `c` starts at `+0.0`, the same starting value as `sum`, and no other write reaches it in between. This is scalar promotion run backwards ([O6](o6-redundancy.md#loop-invariant-code-motion)).
    - **An autotuned tile size**: identical, if the tuner's space contains only tilings that run each element's `k` tiles in order. A space that includes split-K or zero-initialized panels is not safe, which is why [P15](p15-choosing-parameters.md#what-a-tuner-may-not-do) filters it.
    - **A `-ffast-math` library on x86-64**: may differ. `crtfastmath.o` turns on flush-to-zero for the whole process, so subnormal products and sums become zero.
    - **Folding in `double`**: may differ. `0.1` must first be rounded to `f32`, and the product rounded to `f32`; one `double` computation rounded once is a different chain. Decision 56 covers values computed during compilation.
    - **An ordered reduction over `k`**: identical. The lanes are added into the running total in index order.
    - **An `f64` accumulator**: differs. Every step rounds to a different format.

## For Vortex

!!! vortex "Exercise"

    **Build** the numerical side of your compiler's optimizer contract: a written classification, a back-end configuration you can prove, and tests that fail if either slips.

    1. **Classify.** For every pass your compiler has ([O5](o5-constants-and-dead-code.md) through [O9](o9-alias-analysis.md)) and every rung you plan ([P7](p7-loop-transformations.md), [P10](p10-vectorization.md), [P12](p12-fast-gemm.md), [P13](p13-multithreading.md)), state which side of this chapter's question it falls on, and why, next to the pass. For a pass you have not built, write the classification before you build it.
    2. **Pin the back end.** Whatever your back end is, make its floating-point settings explicit rather than inherited: the contraction setting and the absence of fast-math options for a C back end ([stage 6](../compiler/guide/stage-6-first-machine-code.md#generating-c)), or the absence of every fast-math flag and of `llvm.fmuladd` in emitted LLVM IR. Write down where each setting lives.
    3. **Specify the opt-in.** Draft the specification paragraph for a future relaxed floating-point mode as a decision record in the style of [decision 56](../decisions/numbers.md#d56): which permissions it grants (contraction, reassociation), at what scope (program, function, expression), what stays fixed even when it is on (NaN and infinity behavior, subnormals, the rounding mode), and which tests must still match bit for bit.
    4. **Word the remarks.** Draft the remark a compiler with that mode would print for a rung that changes bits and for one that does not. Each must say which side it is on; a remark that could describe either is not specific enough.

    **Not yet:** implementing the relaxed mode; reproducible parallel summation; tolerance-based tests for relaxed builds; any rung in the "differs" column.

    **Proof that it works:**

    - A golden test whose printed result differs between one and two roundings: a program that computes `a * b + c` with the values of this chapter's second example and prints the result's bits. It must print `0x1p-22`'s bits with every optimization setting you support.
    - A golden test that prints `(a + b) + c` and `a + (b + c)` with the first example's values; the two lines must differ.
    - A check on generated code: compile the stage 10 program and search the output for fused instructions (`fmadd`, `fmla`, `vfmadd`) or `llvm.fmuladd`. The search must find none.
    - A C++ reference for stage 10 built with `-ffp-contract=off` whose output matches your compiler's byte for byte, and a note of what happens without the flag.
    - The price of strictness, measured with [P1](p1-measure-first.md)'s protocol on the stage 10 kernel written in C++, with your machine, compiler version and date:

    | Build of the C++ kernel | Median time | Spread | Bits equal to strict? |
    | --- | --- | --- | --- |
    | `-O2 -ffp-contract=off` | | | yes |
    | `-O2` (default contraction) | | | |
    | `-O2 -ffp-contract=fast` | | | |
    | `-O2 -ffast-math` | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **Why is floating-point addition not associative?** Each operation rounds once, so two groupings round at different points and can land on different floats, although every step follows IEEE 754.
    - **What one question sorts a transformation?** Does it change the chain of roundings that builds some output, or only the order in which independent chains run?
    - **What does a fused multiply-add change?** Two roundings become one; a bit lost in the product can become a whole ulp of the result after cancellation.
    - **Who decides whether a multiply and an add fuse?** The `contract` flag or an `llvm.fmuladd` call in the IR, set by the front end's contraction setting; under `llvm.fmuladd` other passes, such as the vectorizer, can settle it.
    - **What does `-ffast-math` do?** It grants several separate permissions (`nnan`, `ninf`, `nsz`, `arcp`, `contract`, `afn`, `reassoc`), and on x86-64 it can set flush-to-zero for the whole process.
    - **When is a bitwise comparison meaningful?** When both sides promise the same chain, which for a C or C++ reference means `-ffp-contract=off` and no fast-math options; otherwise compare within a tolerance derived from an error bound.
    - **Why do pairwise and Kahan summation beat the sequential order?** Pairwise cuts each term's path to about $\log_2 n$ roundings; Kahan carries each step's lost low part into the next, bounding each term's perturbation by about $2\varepsilon$.

## Where this comes back

!!! next "You will use this again in"

    - [P12. Anatomy of a fast GEMM](p12-fast-gemm.md): *accumulators loaded from `c` or zeroed per panel*, *fused multiply-add in the micro-kernel*
    - [P13. Multithreading](p13-multithreading.md): *threads over rows and columns keep the bits*, *split-K needs a reduction*
    - [P14. Algorithms and schedules](p14-algorithms-and-schedules.md): *a schedule may not reorder one accumulator's updates*
    - [P15. Choosing parameters: models or search](p15-choosing-parameters.md): *a search space limited to bit-preserving variants*
    - [P16. Capstone: the ladder, measured](p16-capstone.md): *the bits gate*, *the price of strictness*
    - [A3. Floats and vectors in registers](../backend/a3-floats-and-vectors.md): *`fmadd` and `fmla`*, *the control register's flush-to-zero bit*
    - [G6. Synchronization, atomics and reductions](../gpu/g6-synchronization.md): *three reduction orders, three bit patterns*
    - [G11. Matrix units](../gpu/g11-matrix-units.md): *what the unit computes is not what `f32` code says*
    - [M8. Vectorization in MLIR](../mlir/m8-vectorization.md): *fast-math permissions on vector operations*

## Sources and further reading

Read Goldberg's survey first, at least the sections on rounding error, the IEEE standard and optimizers; Oracle's free reprint carries a useful addendum on extended precision. Then read the "Fast-Math Flags" and "Floating-Point Environment" sections of the LLVM Language Reference, and the floating-point part of the Clang User's Manual, which lists what each option implies. The BLIS kernel guide and Smith and colleagues explain the GEMM rungs this chapter classifies.

[^ieee754]: IEEE, "IEEE Standard for Floating-Point Arithmetic (IEEE 754-2019)", 2019: the binary32 format, rounding to nearest with ties to even, and the `fusedMultiplyAdd` operation. <https://standards.ieee.org/ieee/754/6210/>
[^goldberg91]: David Goldberg, "What Every Computer Scientist Should Know About Floating-Point Arithmetic", *ACM Computing Surveys* 23(1), 1991: the sections "Relative Error and Ulps" (machine epsilon), "Exactly Rounded Operations" (round to even, after Reiser and Knuth), "Optimizers" (Kahan's formula and compile-time constants), Theorem 8 and "Errors In Summation". <https://doi.org/10.1145/103162.103163> (reprint: <https://docs.oracle.com/cd/E19957-01/806-3568/ncg_goldberg.html>)
[^priest]: "Differences Among IEEE 754 Implementations", an addendum not written by Goldberg, in Oracle's reprint of his paper in the *Numerical Computation Guide*: the extended-precision example. <https://docs.oracle.com/cd/E19957-01/806-3568/ncg_goldberg.html>
[^langref]: LLVM Project, "LLVM Language Reference Manual", sections "Fast-Math Flags", "Floating-Point Environment", "'llvm.fmuladd.*' Intrinsic", "'llvm.vector.reduce.fadd.*' Intrinsic", the `denormal-fp-math` function attribute and "Constrained Floating-Point Intrinsics", read on 2026-09-24. <https://llvm.org/docs/LangRef.html#fast-math-flags>
[^clang-um]: Clang Project, "Clang Compiler User's Manual", entries `-ffast-math`, `-ffp-contract` and `-ffp-model`, and "A note about crtfastmath.o", read on 2026-09-24. <https://clang.llvm.org/docs/UsersManual.html#cmdoption-ffp-contract>
[^clang-le]: Clang Project, "Clang Language Extensions", sections "Extensions for loop hint optimizations" (`#pragma clang loop vectorize`) and "Extensions to specify floating-point flags" (`#pragma clang fp contract`), read on 2026-09-24. <https://clang.llvm.org/docs/LanguageExtensions.html>
[^gcc-opt]: GCC Project, "Options That Control Optimization", entries `-ffp-contract`, `-fexcess-precision` and `-ffast-math`, read on 2026-09-24. <https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html>
[^cppref-fma]: cppreference.com, "std::fma, std::fmaf, std::fmal", read on 2026-09-24. <https://en.cppreference.com/w/cpp/numeric/math/fma>
[^llvm-vec]: LLVM Project, "Auto-Vectorization in LLVM", section "Reductions", read on 2026-09-24. <https://llvm.org/docs/Vectorizers.html#reductions>
[^smith14]: Tyler M. Smith, Robert van de Geijn, Mikhail Smelyanskiy, Jeff R. Hammond and Field G. Van Zee, "Anatomy of High-Performance Many-Threaded Matrix Multiplication", *IPDPS 2014*: the discussion of the loop indexed by `pc`, whose parallelization needs copies of `C` and a reduction. <https://doi.org/10.1109/IPDPS.2014.110>
[^blis-k]: BLIS Project, "KernelsHowTo", the `gemm` micro-kernel and its contract `C11 := beta * C11 + alpha * A1 * B1`, read on 2026-09-24. <https://github.com/flame/blis/blob/master/docs/KernelsHowTo.md>
[^goto08]: Kazushige Goto and Robert A. van de Geijn, "Anatomy of High-Performance Matrix Multiplication", *ACM Transactions on Mathematical Software* 34(3), 2008: the packing step. <https://doi.org/10.1145/1356052.1356053>
[^reproblas]: ReproBLAS project (James Demmel, Hong Diep Nguyen and colleagues, UC Berkeley), "ReproBLAS: Reproducible Basic Linear Algebra Sub-programs", the project page: its definition of reproducibility and its "Performance" section, read on 2026-09-24. <https://bebop.cs.berkeley.edu/reproblas/>
