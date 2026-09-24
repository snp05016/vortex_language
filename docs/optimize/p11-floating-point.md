# P11. Floating point under optimization

<p class="page-intro">Which transformations to the matmul kernel keep every bit of its answer, and which quietly change it: the one question every earlier chapter's passes have to answer before they touch a floating-point value.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 40 minutes · Builds on: [O6. Redundancy: CSE, GVN, PRE and LICM](o6-redundancy.md), [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md), [Build v0.1, stage 10](../compiler/guide/stage-10-matrix-multiplication.md), [P10. Vectorization](p10-vectorization.md)</p>

???+ remember "Before you start, remember"

    ??? question "What must a v0.1 Vortex compiler never do to `sum += a[row, k] * b[k, column]` on its own?"

        Reorder the additions, fuse the multiply and the add into one rounding step, evaluate with extra precision, or flush a subnormal result to zero. Each `f32` or `f64` operation rounds once, in the order the program wrote it.

        Introduced in [the language tour's floating-point rules](../language-tour/06-runtime-and-numerical-rules.md#floating-point-behavior).

    ??? question "When may GVN reuse an earlier-computed value instead of recomputing it?"

        Only when the two computations are provably the same operation on the same operands, with nothing in between that could have changed either operand. GVN's proof says nothing about which order two *different* operations ran in.

        Introduced in [O6. Redundancy: CSE, GVN, PRE and LICM](o6-redundancy.md#value-numbering-in-one-block).

    ??? question "Why can the compiler treat `c[row, column]` in the kernel as not aliasing `a` or `b`?"

        `&mut` is exclusive: while `c` is borrowed mutably, no other access to the same memory can exist at the same time, so the compiler may mark the pointer `noalias`.

        Introduced in [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md#for-vortex).

    ??? question "In the stage 10 kernel, in what order does the inner loop add its products into `sum`?"

        One at a time, for `k` from 0 to 63 in increasing order, into a running total that starts at `0.0`.

        Introduced in [Build v0.1, stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for).

!!! goals "In this chapter"

    - Explain why floating-point addition and multiplication are not associative, and connect that to what rounding does at every step.
    - Classify a transformation on the matmul kernel as bitwise-identical or bits-differing, from one question: does it reorder the additions that build one output element?
    - Read LLVM's fast-math flags (`reassoc`, `contract`, `nsz`, and the rest) and explain what each one permits that strict IEEE 754 forbids.
    - State Clang's and GCC's default `-ffp-contract` behavior, and explain why a reference build must turn contraction off before its output means anything to compare against.
    - Measure, with a small program, how far three summation strategies drift from the exact answer, and explain why the sequential one drifts most.

## Why `(a + b) + c` is not always `a + (b + c)`

Real addition is associative: for any three real numbers, `(a + b) + c` and `a + (b + c)` are the same number. A **floating-point** value is not a real number; it is one of a finite set of numbers a computer can represent exactly, spaced unevenly across the number line. Every `f32` operation computes the exact real result and then **rounds** it: replaces it with the nearest value the format can hold, breaking ties by picking the one whose stored bits end in zero (round to nearest, ties to even).[^ieee754] Associativity is a law about exact real arithmetic. Rounding is applied once per operation, not once at the end, and there is nothing that guarantees two different rounding schedules land on the same final value.

Here is a case where they do not:

--8<-- "includes/examples/optimize/p11-floating-point/associativity.cpp.md"

`f32`'s mantissa holds 24 bits: 23 stored, plus one implicit leading bit.[^ieee754] That is enough to represent every integer up to 2^24 = 16,777,216 exactly, one apart. Past that point the representable integers are two apart, because the format has run out of bits to say which odd integer is meant. The figure below shows the gap widening at exactly that boundary.

<figure class="vx-figure">
<svg viewBox="0 0 760 260" role="img" aria-label="A number line of f32 values near 2 to the 24, showing the gap between representable values doubling from 1 to 2 at that point" aria-describedby="p11-f1-desc">
<title id="p11-f1-title">The gap between representable f32 values doubles at 2^24</title>
<desc id="p11-f1-desc">A horizontal number line with eight tick marks. The three leftmost ticks, at 16,777,213, 16,777,214 and 16,777,215, are solid and evenly spaced one apart: every integer here is representable. The fourth tick, at 16,777,216 (2 to the 24), is drawn bold and labelled 2^24. From there the remaining ticks, at 16,777,217, 16,777,218, 16,777,219 and 16,777,220, are spaced twice as far apart as before; the odd ones, 217 and 219, are dashed and labelled not representable, while the even ones, 218 and 220, are solid. An arrow labelled "16,777,216 + 1 = 16,777,217, exact" starts above the dashed 217 tick and curves down to the solid 216 tick, labelled "rounds to: a tie, broken to the even choice". A legend at the bottom explains the solid and dashed ticks.</desc>
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
<text class="vx-text-accent" x="380" y="112" text-anchor="middle">2^24</text>
<text class="vx-text" x="380" y="182" text-anchor="middle">…216</text>
<text class="vx-text-muted" x="460" y="182" text-anchor="middle">…217</text>
<text class="vx-text-muted" x="540" y="182" text-anchor="middle">…218</text>
<text class="vx-text-muted" x="620" y="182" text-anchor="middle">…219</text>
<text class="vx-text-muted" x="700" y="182" text-anchor="middle">…220</text>
<text class="vx-text" x="460" y="42" text-anchor="middle">16,777,216 + 1 = 16,777,217, exact</text>
<path class="vx-flow" d="M460 50 C 460 80, 400 90, 384 128" marker-end="url(#p11-f1-head)"/>
<text class="vx-text-muted" x="330" y="222" text-anchor="middle">rounds to: a tie, broken to the even choice</text>
<text class="vx-text-muted" x="60" y="252">— solid: representable f32</text>
<text class="vx-text-muted" x="300" y="252">╌╌ dashed: not representable, rounds to a neighbor</text>
</svg>
<figcaption>Below 2^24 every f32 integer is one apart; from 2^24 on, only the even ones are representable, so the exact sum 16,777,217 has nowhere to land and rounds down.</figcaption>
</figure>

In the example, `a + b` computes the exact value 16,777,217, which sits exactly halfway between the representable values 16,777,216 and 16,777,218. Round to nearest, ties to even picks 16,777,216, because it is the one whose least significant stored bit is 0. Adding `c` afterward gives 0. The other grouping computes `b + c` first: −16,777,215, which is below the 2^24 boundary and therefore exact. Adding `a` afterward gives 1. Both paths follow IEEE 754 exactly; they simply round at a different point, and rounding is not associative even when every individual step is correct.

This is the fact Goldberg's survey opens with:[^goldberg91] floating-point arithmetic is not the arithmetic learned in school, and algebraic laws that hold for real numbers do not automatically hold once every operation rounds. The next section turns that fact into a rule an optimizer can use.

??? check "Why does the tie in the example break toward 16,777,216 and not 16,777,218?"

    Round to nearest, ties to even picks the candidate whose stored significand is an even number. 16,777,216 is 2^24, whose significand is all zero bits (even); 16,777,218 is 2^24 plus one representable step, an odd significand. The rule exists so that repeated rounding does not drift in one direction on average.

## The rule every reordering pass needs

[O6](o6-redundancy.md) and [O8](o8-loops.md) treat a computation as something that can be hoisted, deduplicated or reordered once the compiler proves two instances compute the same value, or that no dependence forces one before the other. That proof is about *which* value each instruction computes, not about *how many times a real number gets rounded along the way*. For an integer add, reordering never changes the answer, because integer addition is associative in the arithmetic the machine actually performs (modulo wraparound, and Vortex checks that separately). For a floating-point add, reordering can change the answer, because each addition is its own independent rounding event.

That gives one rule, and it needs only one question: **does the transformation change the sequence of roundings that produce one particular output value, or only the order in which independent output values are produced?**

- Changing which *output* is computed first, second, and so on: safe. Each output's own chain of roundings is untouched.
- Changing the *order of the additions inside one output's chain*, or *collapsing two roundings into one*: unsafe. The chain itself is different, so its rounding is different.

Everything else in this chapter is that question, applied to specific transformations.

## The matmul kernel, sorted by that question

The stage 10 kernel computes `c[row, column] = sum(a[row, k] * b[k, column] for k in 0..64)`, one running total per output element, one term at a time, in order. Every optimization this book has discussed for that loop nest falls into one of the two branches above.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="A decision tree splitting matmul transformations into ones that keep every bit and ones that do not" aria-describedby="p11-f2-desc">
<title id="p11-f2-title">Which reorderings of the kernel keep every bit</title>
<desc id="p11-f2-desc">A root box reading: naive kernel, sum += a of row k times b of k column, k from 0 to 63, in order. Two arrows lead down from it. The left arrow leads to a box labelled identical: change which output element runs when — loop interchange, tiling, vectorizing across row or column, parallelizing across row or column blocks. Below it, a note reads: every c of row column still adds its 64 products in the same order. The right arrow leads to a box labelled differs: change the order of the 64 additions inside one output's sum — vectorizing across k as a reduction, splitting k across threads, fusing the multiply and the add. Below it, a note reads: the same 64 products are added in a different order, or rounded a different number of times.</desc>
<defs><marker id="p11-f2-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box-strong" x="180" y="16" width="400" height="46" rx="4"/>
<text class="vx-mono" x="380" y="36" text-anchor="middle">naive kernel</text>
<text class="vx-mono" x="380" y="54" text-anchor="middle" font-size="12">sum += a[row,k]*b[k,column], k = 0..63, in order</text>
<path class="vx-flow" d="M300 62 L120 118" marker-end="url(#p11-f2-head)"/>
<path class="vx-flow" d="M460 62 L640 118" marker-end="url(#p11-f2-head)"/>
<rect class="vx-box-accent" x="20" y="122" width="340" height="78" rx="4"/>
<text class="vx-text-accent" x="190" y="142" text-anchor="middle">bits: identical</text>
<text class="vx-text" x="190" y="162" text-anchor="middle" font-size="12">change which output runs when: interchange,</text>
<text class="vx-text" x="190" y="178" text-anchor="middle" font-size="12">tiling, vectorize across row/column, parallel</text>
<text class="vx-text" x="190" y="194" text-anchor="middle" font-size="12">over row/column blocks</text>
<rect class="vx-box-bad" x="400" y="122" width="340" height="78" rx="4"/>
<text class="vx-text-accent" x="570" y="142" text-anchor="middle">bits: differs</text>
<text class="vx-text" x="570" y="162" text-anchor="middle" font-size="12">change the order inside one sum: vectorize</text>
<text class="vx-text" x="570" y="178" text-anchor="middle" font-size="12">across k, split k across threads, fuse the</text>
<text class="vx-text" x="570" y="194" text-anchor="middle" font-size="12">multiply and add</text>
<text class="vx-text-muted" x="190" y="226" text-anchor="middle" font-size="12">every c[row,column] still adds its 64</text>
<text class="vx-text-muted" x="190" y="242" text-anchor="middle" font-size="12">products in the same order</text>
<text class="vx-text-muted" x="570" y="226" text-anchor="middle" font-size="12">the same 64 products, added in a</text>
<text class="vx-text-muted" x="570" y="242" text-anchor="middle" font-size="12">different order or rounded fewer times</text>
</svg>
<figcaption>Every ladder rung this book discusses sorts into one of two branches: does it touch the order of additions that build one output element, or only the order across output elements?</figcaption>
</figure>

| Transformation | Branch | Why |
| --- | --- | --- |
| Loop interchange (`ijk` to `ikj`) | identical | Only the order across `c` elements changes; each element's own sum is untouched. |
| Tiling `i`, `j`, `k`, with each tile's `c` accumulated in place | identical | The `k` tiles still run in increasing order for each element. |
| Vectorizing across `row` or `column` (each SIMD lane a different output) | identical | No reduction is involved; each lane runs its own independent chain. |
| Parallelizing over `row` or `column` blocks | identical | Each thread owns disjoint output elements and their whole chain.[^smith14] |
| Vectorizing across `k` as a reduction (a dot product done with SIMD lanes) | differs | The 64 terms are added pairwise across lanes, not one at a time; LLVM only does this under `reassoc`, `nsz` and related flags.[^llvm-vec] |
| Parallelizing across `k` ("split-K"): each thread sums part of the range, then the partial sums are combined | differs | The combination step is extra additions in an order the naive loop never performs.[^smith14] |
| A packing-and-microkernel design that adds a panel's whole product to `c` at a `kc` boundary, as BLIS does | differs | The contract at that boundary is "add this panel's already-summed product," not "add these terms one at a time."[^blis-k] |
| Packing `a` and `b` into contiguous panels before the kernel runs | identical | Packing only copies data; it computes nothing.[^goto08] |
| Fusing a multiply and an add (FMA contraction) | differs | One rounding replaces two. The next two sections work through why. |

Because `c` is `&mut`, the compiler already knows no other access to the same element exists while a thread or a lane owns it ([O9](o9-alias-analysis.md#for-vortex)); the question this table answers is never about aliasing, only about arithmetic order. That is also why the table is short: every transformation in the left column is one this book has already justified on other grounds (a proof about *control flow* or *pointers*), and it stays justified here for free. Every transformation in the right column needs a second, numerical justification that no earlier chapter supplies, because none of them are about control flow or pointers at all.

??? check "A future Vortex pass fuses `a[row,k]*b[k,column]` with the running `sum` into one FMA instruction, inside the existing loop, with no other change. Which branch is it in, and why?"

    Differs. The loop order and which output runs when are untouched, but each term's contribution to `sum` now goes through one rounding instead of two (multiply, then add). That is a change to the chain inside a single output's sum, which is exactly the right-hand branch.

## Fused multiply-add: one rounding instead of two

IEEE 754 defines a **fused multiply-add** (FMA) as a single operation that computes `a * b + c` as if the product had infinite range and precision, and rounds only the final result once.[^ieee754] Computed the ordinary way, `a * b` rounds to the nearest `f32` on its own, and then that rounded product is added to `c`, rounding a second time. Two roundings can differ from one:

--8<-- "includes/examples/optimize/p11-floating-point/fma_rounding.cpp.md"

The two results are one ULP (unit in the last place: the gap between one representable value and its neighbor) apart. Neither is "wrong": both are the correctly rounded answer to a well-defined operation, but they are different operations. `sum += a[row, k] * b[k, column]` in the stage 10 kernel is exactly this shape, once per iteration of the inner loop, so whether a back end may lower it to one `fmadd` instruction or must keep it as separate `fmul` and `fadd` instructions is not a detail: it decides which of two different numbers the program prints. [The v0.1 numerical rules](../decisions/numbers.md#d56) chose separate roundings, so that a golden output is the same on every conforming target; a future opt-in mode could choose fusion instead, but only as an explicit choice, never as a silent default.

## The `contract` flag decides, not the optimization level

Nothing about `-O2` or `-O3` decides whether a multiply and an add fuse. LLVM decides it from one **fast-math flag**, `contract`, attached to the individual `fadd` instruction.[^langref] A flag on the IR, not a global setting, is why the same source line can fuse in one function and not in another: a front end (or, in Vortex's case, the compiler itself) chooses where to attach it.

--8<-- "includes/examples/optimize/p11-floating-point/contract_flag.ll.md"

Running this file's second function through `llc -O2` on AArch64 lowers it to one `fmadd` instruction; the first, unmarked function keeps its separate `fmul` and `fadd`. `contract` is the smallest of LLVM's fast-math flags. The rest relax more: `reassoc` permits reassociating a chain of additions or multiplications (the transformation that made the earlier vectorize-across-`k` case unsafe without it); `nsz` permits treating `-0.0` as equal to `0.0`; `ninf` and `nnan` permit assuming no infinities or NaNs occur; `arcp` permits computing `a / b` as `a * (1/b)`; `afn` permits standard approximations for functions like reciprocal square root.[^langref] Clang's `-ffast-math` sets all of them at once, plus a few compiler-specific ones; it is a bundle, not one switch.[^clang-um]

Clang and GCC do not start from the same default for `contract` alone. Clang fuses within one source expression by default (`-ffp-contract=on`); GCC fuses across a whole statement by default (`-ffp-contract=fast`) unless a strict ISO C mode is requested.[^clang-um][^gcc-opt] Both are already relaxing IEEE 754 strictness before `-ffast-math` is ever mentioned, on two different compilers' idea of "default."

??? check "A benchmark compiles its C++ reference implementation with plain `-O2` on Clang and compares its output, bit for bit, against a hand-written FMA kernel. The comparison fails. Does that prove the FMA kernel is wrong?"

    No. Clang's default already fuses multiply-add pairs within an expression. The "reference" may itself be using FMA in places the benchmark author did not intend, so a mismatch could come from the reference, the kernel, or both. The next section is what to do instead.

## Choosing a reference honestly

A bitwise comparison is only meaningful against a reference whose own floating-point behavior is known. Building the reference implementation with `-ffp-contract=off` removes the one variable a plain `-O2` build leaves unstated: whether *it*, not the code under test, silently fused an operation.[^clang-um] Without that flag, a passing comparison and a failing one can both be accidents. This is not specific to Vortex or to matmul; it is the same discipline [P1](p1-measure-first.md) asks for when a measurement's own tooling might be adding noise, applied to correctness instead of to time.

Vortex's own kernel never has this ambiguity to begin with, because [decision 56](../decisions/numbers.md#d56) forbids contraction unconditionally in v0.1: every conforming build produces the same digits. The discipline matters once Vortex, or a benchmark comparing Vortex against a C++ or BLAS reference, has to trust an *external* compiler's floating-point defaults.

## Measuring how far order can drift

The associativity example changed one grouping and got a different answer. A longer reduction, like a matmul row's inner product or a sum over many elements, gives rounding many more chances to drift, and different summation strategies drift by different amounts:

--8<-- "includes/examples/optimize/p11-floating-point/summation_orders.cpp.md"

The sequential sum starts past the 2^24 boundary from the first section and, from then on, every `+ 1.0` is a tie broken toward the value already held: the total stops moving almost immediately. Pairwise summation (splitting the range in half recursively and adding the two halves) keeps each partial sum small until near the end, so it drifts far less, though it is still ordinary `f32` arithmetic and still rounds. Kahan summation carries the rounding error from each step forward as a correction to the next one; on this input it lands within one ULP of the exact answer, the best any `f32` result can do once the exact sum itself is not representable.

None of these strategies change *which* answer is correct: they change how much rounding error the chosen order accumulates. A parallel reduction that must give the same answer regardless of how many threads it runs on needs more than a good summation order; it needs an order that does not depend on the thread count, which is what dedicated reproducible-summation libraries provide.[^reproblas] That is a harder problem than this chapter covers, and a paper cross-references it directly with the summation-order tradeoffs above.[^dn13]

??? check "Why does pairwise summation drift less than sequential summation, even though both are ordinary f32 arithmetic with no compensation?"

    Sequential summation adds each new term to one running total that grows throughout the whole loop, so once that total is large, small terms are absorbed with no effect (as in the 2^24 example). Pairwise summation keeps every partial sum roughly the size of the terms being combined for as long as possible, so no intermediate sum is disproportionately large relative to what is being added to it, and each addition loses less precision.

## For Vortex

!!! vortex "Exercise"

    **Build** a written classification, not a compiler pass: for every optimization your compiler already performs, or plans to perform, on the stage 10 kernel or on any loop over `f32`/`f64` values, decide which branch of [this chapter's question](#the-matmul-kernel-sorted-by-that-question) it falls into, and write that decision down next to the pass, the way a remark would state it. For a transformation you have not built yet, write the decision before you build it, not after.

    1. For each pass already built ([O5](o5-constants-and-dead-code.md) through [O9](o9-alias-analysis.md)), confirm in writing that it never reorders a floating-point reduction and never inserts, removes or reorders a floating-point operation. Most of these passes only touch control flow, addresses and integer arithmetic; state explicitly, for each one, why that is true, rather than assuming it.
    2. Draft the specification paragraph for a future opt-in relaxed floating-point mode: which flag or attribute enables it, whether it is per-function or per-expression, which of `reassoc`, `contract` and `nsz` (or an equivalent Vortex vocabulary) it turns on, and what stays fixed even when it is enabled (no silent default; NaN and infinity behavior; which builds are still required to match bit for bit). Write it as you would write a real decision record, with a "before this decision," options considered, and a "why."
    3. Draft the remark wording a compiler with this mode would print for a rung that changes results, for example "fused multiply-add: sum += a[row,k]*b[k,column], one rounding instead of two; enabled by --relaxed-fp" and for one that does not, for example "vectorized across row: 4-wide, results unchanged." A remark that could describe either case is not specific enough.

    **Not yet:** implementing the relaxed mode itself; deciding what performance it would buy on your machine (that is measurement, not specification); reproducible parallel summation ([^reproblas]), which is a harder problem than an opt-in flag; touching the kernel's inner-loop code at all. This chapter is about the rule, not the pass.

    **Proof that it works:** a short document (or a decision record in your own project's style) that a reviewer who has never seen your compiler could read and correctly predict, for five transformations you did not use as examples here, which branch each one falls into and why. Test it on a real one: pick a transformation from [P7](p7-loop-transformations.md), [P10](p10-vectorization.md) or [P13](p13-multithreading.md) once those chapters exist, and classify it before you read what they say.

## Key ideas

!!! recap "Questions you can now answer"

    - **Why is floating-point addition not associative?** Each operation rounds its result once; `(a + b) + c` and `a + (b + c)` round at different points and can round to different representable values, even though every step individually follows IEEE 754.
    - **What is the one question that sorts a transformation into "identical" or "differs"?** Does it change the order of additions that build one output element's own sum, or only the order across different output elements?
    - **What does a fused multiply-add change?** It replaces two roundings (multiply, then add) with one, computed as if the product had infinite precision before that single rounding.
    - **What controls whether LLVM fuses a multiply and an add?** The `contract` fast-math flag on the instruction, not the optimization level; a front end or compiler decides where to attach it.
    - **What does `-ffast-math` actually do?** It sets a bundle of separate fast-math flags at once (`reassoc`, `contract`, `nsz`, `ninf`, `nnan`, `arcp`, `afn`, and more), each permitting a specific relaxation of IEEE 754.
    - **Why must a reference build turn off `-ffp-contract`?** Clang and GCC both fuse by default; a comparison against a reference that silently fuses cannot tell you whether the difference came from the code under test or from the reference itself.
    - **Why does Kahan summation drift less than sequential summation?** It carries each step's rounding error forward as a correction added into the next term, instead of letting the running total simply absorb small terms once it grows large.

## Where this comes back

!!! next "You will use this again in"

    - [P10. Vectorization](p10-vectorization.md): *reduction vectorization needs `reassoc`*, *SIMD lanes as independent chains*
    - [P12. Anatomy of a fast GEMM](p12-fast-gemm.md): *packing changes nothing, kc-boundary accumulation does*
    - [P13. Multithreading](p13-multithreading.md): *splitting `i`/`j` is free, splitting `k` needs a reduction*
    - [P14. Algorithms and schedules](p14-algorithms-and-schedules.md): *a schedule may not change results unless the FP mode allows it*
    - [P15. Choosing parameters: models or search](p15-choosing-parameters.md): *the search space is pre-filtered to FP-preserving variants*
    - [P16. Capstone: the ladder, measured](p16-capstone.md): *the identical/differs column, next to every rung's measurement*
    - [A3. Floats and vectors in registers](../backend/a3-floats-and-vectors.md): *the FMA instruction encoding this chapter's flag lowers to*

## Sources and further reading

Read Goldberg's survey first: it is the standard reference for how rounding breaks familiar algebraic laws, with the associativity failure as an early example. Then read IEEE 754-2019's definitions of rounding and fused multiply-add directly, and LLVM's Language Reference section on fast-math flags for the vocabulary this chapter's examples use. The Clang and GCC manual pages cited below state each compiler's own default for `-ffp-contract` in one paragraph each. Smith et al. and the BLIS kernel documentation explain the packing and micro-kernel design this chapter's table classifies; van de Geijn's TOMS paper on Goto's algorithm is the origin of the packing step itself.

[^ieee754]: IEEE, "IEEE Standard for Floating-Point Arithmetic (IEEE 754-2019)", 2019: the definitions of round to nearest ties to even, the binary32 format, and `fusedMultiplyAdd` (clause 5.4.1). <https://standards.ieee.org/ieee/754/6210/>
[^goldberg91]: David Goldberg, "What Every Computer Scientist Should Know About Floating-Point Arithmetic", *ACM Computing Surveys* 23(1), 1991: the sections on rounding error and on the failure of algebraic laws under rounding. <https://doi.org/10.1145/103162.103163>
[^langref]: LLVM Project, "LLVM Language Reference Manual", section "Fast-Math Flags", read on 2026-09-24. <https://llvm.org/docs/LangRef.html#fast-math-flags>
[^clang-um]: Clang, "Clang Compiler User's Manual", sections on `-ffast-math` and `-ffp-contract`, read on 2026-09-24. <https://clang.llvm.org/docs/UsersManual.html>
[^gcc-opt]: GCC, "Options That Control Optimization", entry for `-ffp-contract`, read on 2026-09-24. <https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html>
[^llvm-vec]: LLVM Project, "Auto-Vectorization in LLVM", the notes on floating-point reductions needing `reassoc`, `nsz` and related flags, read on 2026-09-24. <https://llvm.org/docs/Vectorizers.html>
[^smith14]: Tyler M. Smith, Robert van de Geijn, Mikhail Smelyanskiy, Jeff R. Hammond and Field G. Van Zee, "Anatomy of High-Performance Many-Threaded Matrix Multiplication", *IPDPS 2014*: the discussion of which of the five GEMM loops parallelize without a reduction. <https://doi.org/10.1109/IPDPS.2014.110>
[^blis-k]: BLIS, "KernelsHowTo", the micro-kernel contract at a `kc` panel boundary. <https://github.com/flame/blis/blob/master/docs/KernelsHowTo.md>
[^goto08]: Kazushige Goto and Robert A. van de Geijn, "Anatomy of High-Performance Matrix Multiplication", *ACM Transactions on Mathematical Software* 34(3), 2008: the packing step. <https://doi.org/10.1145/1356052.1356053>
[^reproblas]: ReproBLAS, a library for reproducible summation and BLAS operations regardless of order or thread count. <https://bebop.cs.berkeley.edu/reproblas/>
[^dn13]: James Demmel and Hong Diep Nguyen, "Fast Reproducible Floating-Point Summation", *21st IEEE Symposium on Computer Arithmetic (ARITH)*, 2013. <https://doi.org/10.1109/ARITH.2013.9>
