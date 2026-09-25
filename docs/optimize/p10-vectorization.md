# P10. Vectorization

<p class="page-intro">One instruction, four numbers: what LLVM's loop vectorizer and SLP vectorizer each widen, what they must prove before they touch a loop, and why the kernel's <code>column</code> loop can run four lanes at a time with the same bits and no runtime check, while its <code>k</code> loop cannot.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 55 minutes · Builds on: [P6. Dependence analysis](p6-dependence-analysis.md), [A3. Floats and vectors in registers](../backend/a3-floats-and-vectors.md)</p>

???+ remember "Before you start, remember"

    ??? question "Why is every reordering of the kernel's `row`, `column` and `k` loops legal?"

        The only loop-carried dependence is on `c`, with distance vector `(0, 0, 1)`: one nonzero component, on `k`. Reordering the levels moves that component but never makes it negative, so the reordered vector stays lexicographically positive.

        Introduced in [P6. Dependence analysis](p6-dependence-analysis.md#when-a-loop-permutation-is-legal).

    ??? question "After interchange, which loop of the kernel is innermost, and why does each element of `c` keep its bits?"

        In ikj order the innermost loop runs over `column`, updating one row of `c` from one row of `b` scaled by `a[row, k]`. Each element still starts at `0.0` and adds its products in increasing `k`, one rounding per operation.

        Introduced in [P7. Loop transformations](p7-loop-transformations.md#making-the-stage-10-nest-perfect).

    ??? question "What is a loop's backedge-taken count, and how does it relate to the trip count?"

        The number of times a back edge is taken. For a run that enters the header, it is one less than the trip count, the number of times the header executes.

        Introduced in [O8. Loops: structure, induction variables and bounds checks](o8-loops.md#counting-iterations).

    ??? question "Why does the vectorizer need no runtime overlap check in front of the kernel's loop over `c`, `a` and `b`?"

        [References 9.8](../specification/references.md#98-aliasing) makes storage reached through a `&mut` parameter unreachable through the call's other parameters, so the compiler may mark `c` `noalias` on a rule, not a guess. A fact the language guarantees needs no check at run time.

        Introduced in [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md#promises-the-front-end-writes-down).

    ??? question "Which kind of loop hint can change what an optimizer is allowed to do, not only how it does it?"

        In LLVM 18, a hint that enables vectorization or sets a vector width also permits the vectorizer to reorder floating-point operations. A request for speed becomes a permission to change bits.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#legal-then-worth-doing).

!!! goals "In this chapter"

    - Explain what the loop vectorizer and the SLP vectorizer each widen, and why one needs a loop and the other does not.
    - Decide from a dependence distance which vector widths a loop allows, and list the other facts a vectorizer needs: a countable loop, no opaque calls, and proof that its arrays do not overlap.
    - Distinguish an ordered reduction from a reassociated one, predict which one changes the bits of a floating-point sum, and say why Vortex's checked integers are no easier.
    - Read a vector loop, its middle block and its scalar epilogue off the loop vectorizer's output, and count the iterations each one runs.
    - Connect NEON, SVE and SME to Vortex's fixed shapes and to its strict floating-point rule, including why the kernel's vector loop must not fuse its multiply and add.

## Four lanes, one instruction

[A2](../backend/a2-aarch64-assembly.md#branches-and-loops) compiled a small function, `max_reference`, that finds the largest element of an `int` array. With vectorization and unrolling turned off, Apple clang 21 produced a scalar loop: one load, one `cmp` and one `csel` per element. At the default `-O2` the same source produced `smax.4s` instructions instead, each of which compares four pairs of numbers at once. This chapter is about the transformation that made that change, and about when a compiler is allowed to make it.

A **SIMD** instruction (single instruction, multiple data) applies one operation to several values packed side by side in one wide register. AArch64 has 32 such registers, `v0` to `v31`, each 128 bits wide; the name the assembler uses (`q1`, `d1`, `s1`) says how much of the register an instruction touches.[^aapcs64] The suffix `.4s` reads the 128 bits as four 32-bit **lanes**, numbered 0 to 3. `smax.4s v0, v1, v2` puts the larger of lane 0 of `v1` and lane 0 of `v2` in lane 0 of `v0`, and does the same for lanes 1, 2 and 3. No lane looks at another lane.

That last sentence is the whole subject. An instruction that works lane by lane is only a correct replacement for four scalar instructions if the four scalar instructions did not depend on each other. A **vectorizer** is a transformation that finds such groups of independent scalar operations and replaces each group with vector instructions. LLVM has two, and they look for independence in different shapes of code.[^llvm-vec]

The `max_reference` loop is also a first example of a harder case. Its four lanes each keep a running maximum of every fourth element, and the four maxima are combined after the loop. That changes the order in which elements are compared, and it is correct only because the maximum of integers does not depend on the order it is taken in. The section on reductions below shows why a floating-point sum is different.

## Two vectorizers

The **loop vectorizer** widens a loop so that one trip through its body does the work of several consecutive iterations.[^llvm-vec] A loop that computes `y[i] = a * x[i] + y[i]` one element per trip becomes a loop that loads four elements of `x` with one instruction, four of `y` with another, computes four results and stores them with one more, then advances `i` by four. The number of iterations folded into one trip is the **vectorization factor**, often called the vector width. It needs a loop, and it needs the iterations it groups to be independent in the sense of the next section.

The **SLP vectorizer** needs no loop. SLP stands for **superword-level parallelism**, the name Larsen and Amarasinghe gave to parallelism among the statements of straight-line code.[^slp00] They look inside a basic block for **isomorphic** statements, statements that perform the same operations in the same order, and replace a group of them with one SIMD operation, a step they call statement packing. Their algorithm starts from pairs of loads or stores to adjacent addresses, because those operands arrive already packed in memory, and grows groups from there.[^slp00] LLVM's SLP vectorizer follows the same idea, working bottom-up and across basic blocks.[^llvm-vec]

The first example is four isomorphic statements with no loop anywhere:

--8<-- "includes/examples/optimize/p10-vectorization/slp_pack.ll.md"

The input has four loads from `a`, four from `b`, four `fadd` instructions and four stores, each group touching offsets 0 to 3. The output has one `<4 x float>` load from each array, one vector `fadd` and one vector store. The result is the same instruction the loop vectorizer would have produced for a four-element loop, reached from straight-line code.

Packing is not free. When the values a statement needs are not already side by side, the compiler must move them into lanes first and perhaps move results back out afterwards; Larsen and Amarasinghe point out that this can make the packed code slower than the scalar code it replaced.[^slp00] Both vectorizers therefore ask a **cost model**, a table of estimated instruction costs for the target, whether the vector version is cheaper, and a legal rewrite can still be declined as not worth doing.

??? check "A function computes `r[0] = a[0] + b[0]` through `r[3] = a[3] + b[3]` with no loop; a second function computes `s[i] = t[2 * i] + u[i]` in a loop over 64 values of `i`. Which vectorizer is in a position to widen each, and what might stop the second?"

    The first is straight-line code, so only the SLP vectorizer can see it. The second is a loop whose iterations do not depend on each other, so the loop vectorizer can widen it, but `t[2 * i]` reads every other element: the four values one vector operation needs are not adjacent in memory and must be gathered into lanes. The cost model may decide that gathering costs more than the vector add saves.

## When lanes may run together

Take a loop that reads an element and writes the element two places further on:

```c
for (int i = 0; i < 64; ++i) a[i + 2] = a[i] * 0.5f;
```

Iteration 0 writes `a[2]`, which iteration 2 reads; iteration 1 writes `a[3]`, which iteration 3 reads. In the terms of [P6](p6-dependence-analysis.md#distance-and-direction-vectors), this is a flow dependence with distance 2. Now group the iterations into lanes and follow the values by hand.

With two lanes, one vector trip runs iterations 0 and 1 together: it loads `a[0]` and `a[1]`, which nothing in the loop has written, and stores `a[2]` and `a[3]`. The next trip, iterations 2 and 3, loads `a[2]` and `a[3]` after the previous store, exactly as the scalar loop would. With four lanes, one trip runs iterations 0 to 3 together and loads `a[0]` to `a[3]` at the start. Iteration 2 should have read the `a[2]` that iteration 0 writes, but the load happened first. The vector loop computes different numbers.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Iterations 0 to 7 of a loop that writes a of i plus 2 and reads a of i, grouped into lanes of two and of four. Arrows run from each writing iteration to the iteration two later that reads its value. With groups of two, every arrow leaves one group and enters a later one. With groups of four, arrows from 0 to 2, 1 to 3, 4 to 6 and 5 to 7 stay inside a group, and iterations 2, 3, 6 and 7 are marked as reading a value their own group has not yet written.">
<defs><marker id="p10-f1-head" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="16" y="112">width 2</text>
<text class="vx-text-muted" x="16" y="130">legal</text>
<rect class="vx-line" x="102" y="82" width="156" height="52" rx="6"/>
<rect class="vx-line" x="262" y="82" width="156" height="52" rx="6"/>
<rect class="vx-line" x="422" y="82" width="156" height="52" rx="6"/>
<rect class="vx-line" x="582" y="82" width="156" height="52" rx="6"/>
<rect class="vx-box" x="110" y="90" width="60" height="36" rx="4"/>
<text class="vx-mono" x="140" y="113" text-anchor="middle">i=0</text>
<rect class="vx-box" x="190" y="90" width="60" height="36" rx="4"/>
<text class="vx-mono" x="220" y="113" text-anchor="middle">i=1</text>
<rect class="vx-box" x="270" y="90" width="60" height="36" rx="4"/>
<text class="vx-mono" x="300" y="113" text-anchor="middle">i=2</text>
<rect class="vx-box" x="350" y="90" width="60" height="36" rx="4"/>
<text class="vx-mono" x="380" y="113" text-anchor="middle">i=3</text>
<rect class="vx-box" x="430" y="90" width="60" height="36" rx="4"/>
<text class="vx-mono" x="460" y="113" text-anchor="middle">i=4</text>
<rect class="vx-box" x="510" y="90" width="60" height="36" rx="4"/>
<text class="vx-mono" x="540" y="113" text-anchor="middle">i=5</text>
<rect class="vx-box" x="590" y="90" width="60" height="36" rx="4"/>
<text class="vx-mono" x="620" y="113" text-anchor="middle">i=6</text>
<rect class="vx-box" x="670" y="90" width="60" height="36" rx="4"/>
<text class="vx-mono" x="700" y="113" text-anchor="middle">i=7</text>
<path class="vx-line" d="M140 90 C 140 52, 300 52, 300 88" marker-end="url(#p10-f1-head)"/>
<path class="vx-line" d="M220 90 C 220 52, 380 52, 380 88" marker-end="url(#p10-f1-head)"/>
<path class="vx-line" d="M300 90 C 300 52, 460 52, 460 88" marker-end="url(#p10-f1-head)"/>
<path class="vx-line" d="M380 90 C 380 52, 540 52, 540 88" marker-end="url(#p10-f1-head)"/>
<path class="vx-line" d="M460 90 C 460 52, 620 52, 620 88" marker-end="url(#p10-f1-head)"/>
<path class="vx-line" d="M540 90 C 540 52, 700 52, 700 88" marker-end="url(#p10-f1-head)"/>
<text class="vx-text" x="16" y="247">width 4</text>
<text class="vx-text-muted" x="16" y="265">wrong</text>
<rect class="vx-line" x="102" y="217" width="316" height="52" rx="6"/>
<rect class="vx-line" x="422" y="217" width="316" height="52" rx="6"/>
<rect class="vx-box" x="110" y="225" width="60" height="36" rx="4"/>
<text class="vx-mono" x="140" y="248" text-anchor="middle">i=0</text>
<rect class="vx-box" x="190" y="225" width="60" height="36" rx="4"/>
<text class="vx-mono" x="220" y="248" text-anchor="middle">i=1</text>
<rect class="vx-box-bad" x="270" y="225" width="60" height="36" rx="4"/>
<text class="vx-mono" x="300" y="248" text-anchor="middle">i=2</text>
<rect class="vx-box-bad" x="350" y="225" width="60" height="36" rx="4"/>
<text class="vx-mono" x="380" y="248" text-anchor="middle">i=3</text>
<rect class="vx-box" x="430" y="225" width="60" height="36" rx="4"/>
<text class="vx-mono" x="460" y="248" text-anchor="middle">i=4</text>
<rect class="vx-box" x="510" y="225" width="60" height="36" rx="4"/>
<text class="vx-mono" x="540" y="248" text-anchor="middle">i=5</text>
<rect class="vx-box-bad" x="590" y="225" width="60" height="36" rx="4"/>
<text class="vx-mono" x="620" y="248" text-anchor="middle">i=6</text>
<rect class="vx-box-bad" x="670" y="225" width="60" height="36" rx="4"/>
<text class="vx-mono" x="700" y="248" text-anchor="middle">i=7</text>
<path class="vx-flow" d="M140 225 C 140 187, 300 187, 300 223" marker-end="url(#p10-f1-head)"/>
<path class="vx-flow" d="M220 225 C 220 187, 380 187, 380 223" marker-end="url(#p10-f1-head)"/>
<path class="vx-line" d="M300 225 C 300 187, 460 187, 460 223" marker-end="url(#p10-f1-head)"/>
<path class="vx-line" d="M380 225 C 380 187, 540 187, 540 223" marker-end="url(#p10-f1-head)"/>
<path class="vx-flow" d="M460 225 C 460 187, 620 187, 620 223" marker-end="url(#p10-f1-head)"/>
<path class="vx-flow" d="M540 225 C 540 187, 700 187, 700 223" marker-end="url(#p10-f1-head)"/>
</svg>
<figcaption>Figure 1. The loop <code>a[i + 2] = a[i] * 0.5f</code>, with each arrow running from the iteration that writes a value to the iteration that reads it. A rounded frame is one vector trip, whose loads all happen before its stores. With two lanes every arrow crosses into a later trip. With four lanes the highlighted arrows stay inside a trip, and the dashed iterations read their values too early.</figcaption>
</figure>

The rule the figure shows: a loop-carried dependence of distance $d$ between iterations allows up to $d$ consecutive iterations in one vector trip, and no more. A loop with no loop-carried dependence at all allows any width. Apple clang 21 draws the same line. For this loop in a C file with a `restrict` parameter, at `-O2` on the owner's M4 Pro, it chose width 2 on its own; forced to width 4 with `-mllvm -force-vector-width=4`, it refused, with the remark "unsafe dependent memory operations in loop" (checked 2026-09-24).

Now the kernel. In the stage 10 order, ijk, the innermost loop runs over `k` and adds every product into the same `sum`: each iteration depends on the one before it, distance 1. In ikj order, from [P7](p7-loop-transformations.md#making-the-stage-10-nest-perfect), the innermost loop runs over `column`:

```vortex
// items: valid
fn multiply(a: &[f32; 64, 64], b: &[f32; 64, 64], c: &mut [f32; 64, 64]) {
    for row in 0..64 {
        for column in 0..64 {
            c[row, column] = 0.0;
        }
        for k in 0..64 {
            for column in 0..64 {
                c[row, column] += a[row, k] * b[k, column];
            }
        }
    }
}
```

For fixed `row` and `k`, iteration `column` reads and writes only `c[row, column]` and reads `b[k, column]` and `a[row, k]`. No two values of `column` touch the same element of `c`, so the `column` loop carries no dependence: any width is legal, and four lanes means four different elements of `c`. The dependence on `c` is carried by the `k` loop outside it, which still runs in order.

??? check "A loop computes `a[i] = a[i + 3] + 1.0f` for `i` from 0 to 63. Which way does its dependence run, and would a width of 4 be safe?"

    Iteration `i` reads `a[i + 3]` before iteration `i + 3` overwrites it, an anti-dependence of distance 3. In a vector trip all loads happen before all stores, so every lane reads the old value, which is what the scalar loop reads too. A width of 4 is safe here even though the distance is less than 4. The limit of the rule above applies to a value that flows forward from a store to a later load; an anti-dependence is respected by any width as long as the trip loads before it stores. Apple clang 21 vectorized this loop even when forced to width 8 (checked 2026-09-24). This is why compilers classify dependences, not only measure them.

## What else a vectorizer needs

Independence between iterations is necessary, not sufficient. A loop vectorizer needs several more facts, and in LLVM each comes from an earlier analysis rather than from the vectorizer itself ([O10](o10-pass-pipelines.md) is about that ordering).

**A countable loop.** The vectorizer must know, before the loop starts, how many iterations it will run, or at least have a formula for that number, so that it can decide how many full vector trips fit. LLVM 18 asks SCEV for the backedge-taken count and gives up when there is none; [O8](o8-loops.md#why-the-checks-matter-for-the-vectorizer) showed a bounds check that the vectorizer tolerates and an overflow check on loaded data that it does not.

**No call it cannot see through.** A call to an unknown function might do anything, so four of them cannot be merged into one. LLVM widens calls only to a list of math functions that have vector instructions or a vector math library behind them;[^llvm-vec] [O7](o7-inlining-and-sroa.md) is how most calls disappear before the vectorizer runs. Branches inside the body are less of an obstacle: the vectorizer can flatten an `if` into straight-line code that computes both sides and selects per lane, which LLVM calls **if-conversion**.[^llvm-vec]

**No overlap it cannot rule out.** If `y` and `x` might overlap, storing four elements of `y` may change elements of `x` that later lanes should have read. When LLVM cannot prove the arrays disjoint, it emits a test on the addresses at run time and keeps the scalar loop for the case where they overlap;[^llvm-vec] [O9](o9-alias-analysis.md#the-price-of-not-knowing-runtime-checks) measured that check. The kernel pays nothing: `c` is a `&mut` parameter, so `noalias` rests on [References 9.8](../specification/references.md#98-aliasing) and no check is needed.

A C programmer can steer all this with a **loop hint**, a pragma placed before a loop. `#pragma clang loop vectorize(disable)` forbids widening, and `vectorize_width(4)` asks for a width; Clang documents that hints are ignored when the transformation is not safe.[^clang-le] [O1](o1-optimizer-contract.md#legal-then-worth-doing) found the exception: a width hint also grants permission to reorder floating-point additions, so for a sum it changes what "safe" means. The next section shows what that permission buys.

## Reductions: ordered or reassociated

The stage 10 kernel, in its original order, spends its innermost loop on `sum += a[row, k] * b[k, column]`. Every iteration updates the same variable from its previous value. LLVM's documentation calls `sum` a **reduction variable**, and a loop built around one a **reduction**: many values collapsed into one.[^llvm-vec-red] [O3](o3-ssa.md#your-turn-the-kernel-in-ssa-form) met it as a phi.

A reduction has distance 1, so by the rule above it cannot be widened as written. It can be widened after a rewrite: give each lane its own partial sum, let lane 0 add elements 0, 4, 8 and so on, lane 1 add elements 1, 5, 9, and add the four partial sums together after the loop. For integers under wrapping arithmetic, and for integer maximum as in `max_reference`, the result is the same. For floating point it usually is not, because every addition rounds, and `(p + q) + r` can round differently from `p + (q + r)`. The rewrite is a **reassociation**, a regrouping of the additions.

LLVM therefore vectorizes floating-point reductions on most targets only when at least `-fassociative-math -fno-signed-zeros -fno-trapping-math` is in effect.[^llvm-vec-red] On some targets, AArch64 and RISC-V among them, it can instead build an **ordered reduction**, which adds each lane into the running total one at a time, lane 0 first, so the result keeps its exact value; the documentation adds that ordered reductions are typically less efficient.[^llvm-vec-red] In LLVM IR, an ordered reduction is a call to `llvm.vector.reduce.fadd` without the `reassoc` flag, which the Language Reference defines as sequential: start from the given value and add the elements in increasing index order.[^langref]

The second example gives the loop vectorizer the same loop twice, once plain and once with `reassoc` on the addition:

--8<-- "includes/examples/optimize/p10-vectorization/ordered_reduction.ll.md"

`@sum_strict` keeps one scalar running total, `%vec.phi`, and folds each loaded group of four into it with the ordered intrinsic. `@sum_reassoc` keeps a vector of four partial sums, adds each loaded group to it lane by lane, and reduces the four lanes once, in `middle.block`, with a `reassoc` call that may add them in any grouping. Figure 2 draws the two shapes.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="Two ways to add up x0, x1, x2 and so on. Left, ordered: four values are loaded together, but one running total s adds them one at a time, x0 first, then x1, x2 and x3, and the next load continues the same chain. Right, reassociated: four partial sums p0 to p3 each collect every fourth value, p0 gets x0, x4, x8 and so on, and after the loop the four partial sums are added in pairs and then together.">
<defs><marker id="p10-f2-head" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="24">Ordered (no reassoc)</text>
<text class="vx-text-muted" x="20" y="44">one load of four, one chain of adds</text>
<rect class="vx-box" x="20" y="60" width="54" height="30" rx="3"/>
<text class="vx-mono" x="47" y="80" text-anchor="middle">x0</text>
<rect class="vx-box" x="82" y="60" width="54" height="30" rx="3"/>
<text class="vx-mono" x="109" y="80" text-anchor="middle">x1</text>
<rect class="vx-box" x="144" y="60" width="54" height="30" rx="3"/>
<text class="vx-mono" x="171" y="80" text-anchor="middle">x2</text>
<rect class="vx-box" x="206" y="60" width="54" height="30" rx="3"/>
<text class="vx-mono" x="233" y="80" text-anchor="middle">x3</text>
<rect class="vx-box-strong" x="270" y="118" width="100" height="30" rx="4"/>
<text class="vx-mono" x="320" y="138" text-anchor="middle">s += x0</text>
<path class="vx-line" d="M47 90 L47 133 L266 133" marker-end="url(#p10-f2-head)"/>
<path class="vx-flow" d="M320 148 L320 160" marker-end="url(#p10-f2-head)"/>
<rect class="vx-box-strong" x="270" y="162" width="100" height="30" rx="4"/>
<text class="vx-mono" x="320" y="182" text-anchor="middle">s += x1</text>
<path class="vx-line" d="M109 90 L109 177 L266 177" marker-end="url(#p10-f2-head)"/>
<path class="vx-flow" d="M320 192 L320 204" marker-end="url(#p10-f2-head)"/>
<rect class="vx-box-strong" x="270" y="206" width="100" height="30" rx="4"/>
<text class="vx-mono" x="320" y="226" text-anchor="middle">s += x2</text>
<path class="vx-line" d="M171 90 L171 221 L266 221" marker-end="url(#p10-f2-head)"/>
<path class="vx-flow" d="M320 236 L320 248" marker-end="url(#p10-f2-head)"/>
<rect class="vx-box-strong" x="270" y="250" width="100" height="30" rx="4"/>
<text class="vx-mono" x="320" y="270" text-anchor="middle">s += x3</text>
<path class="vx-line" d="M233 90 L233 265 L266 265" marker-end="url(#p10-f2-head)"/>
<text class="vx-text-muted" x="20" y="316">x4 to x7 continue the same chain</text>
<text class="vx-text" x="420" y="24">Reassociated (reassoc)</text>
<text class="vx-text-muted" x="420" y="44">four partial sums, combined once</text>
<rect class="vx-box" x="420" y="60" width="72" height="30" rx="3"/>
<text class="vx-mono" x="456" y="80" text-anchor="middle">x0, x4</text>
<path class="vx-flow" d="M456 90 L456 116" marker-end="url(#p10-f2-head)"/>
<rect class="vx-box-accent" x="420" y="118" width="72" height="30" rx="4"/>
<text class="vx-mono" x="456" y="138" text-anchor="middle">p0</text>
<rect class="vx-box" x="502" y="60" width="72" height="30" rx="3"/>
<text class="vx-mono" x="538" y="80" text-anchor="middle">x1, x5</text>
<path class="vx-flow" d="M538 90 L538 116" marker-end="url(#p10-f2-head)"/>
<rect class="vx-box-accent" x="502" y="118" width="72" height="30" rx="4"/>
<text class="vx-mono" x="538" y="138" text-anchor="middle">p1</text>
<rect class="vx-box" x="584" y="60" width="72" height="30" rx="3"/>
<text class="vx-mono" x="620" y="80" text-anchor="middle">x2, x6</text>
<path class="vx-flow" d="M620 90 L620 116" marker-end="url(#p10-f2-head)"/>
<rect class="vx-box-accent" x="584" y="118" width="72" height="30" rx="4"/>
<text class="vx-mono" x="620" y="138" text-anchor="middle">p2</text>
<rect class="vx-box" x="666" y="60" width="72" height="30" rx="3"/>
<text class="vx-mono" x="702" y="80" text-anchor="middle">x3, x7</text>
<path class="vx-flow" d="M702 90 L702 116" marker-end="url(#p10-f2-head)"/>
<rect class="vx-box-accent" x="666" y="118" width="72" height="30" rx="4"/>
<text class="vx-mono" x="702" y="138" text-anchor="middle">p3</text>
<path class="vx-line" d="M456 148 L497.0 196" marker-end="url(#p10-f2-head)"/>
<path class="vx-line" d="M538 148 L497.0 196" marker-end="url(#p10-f2-head)"/>
<rect class="vx-box-strong" x="452.0" y="198" width="90" height="30" rx="4"/>
<text class="vx-mono" x="497.0" y="218" text-anchor="middle">p0 + p1</text>
<path class="vx-line" d="M620 148 L661.0 196" marker-end="url(#p10-f2-head)"/>
<path class="vx-line" d="M702 148 L661.0 196" marker-end="url(#p10-f2-head)"/>
<rect class="vx-box-strong" x="616.0" y="198" width="90" height="30" rx="4"/>
<text class="vx-mono" x="661.0" y="218" text-anchor="middle">p2 + p3</text>
<path class="vx-line" d="M497.0 228 L579.0 268" marker-end="url(#p10-f2-head)"/>
<path class="vx-line" d="M661.0 228 L579.0 268" marker-end="url(#p10-f2-head)"/>
<rect class="vx-box-strong" x="534.0" y="270" width="90" height="30" rx="4"/>
<text class="vx-mono" x="579.0" y="290" text-anchor="middle">sum</text>
<text class="vx-text-muted" x="420" y="316">after the loop</text>
</svg>
<figcaption>Figure 2. The two shapes of a vectorized sum. Left, the ordered reduction of <code>@sum_strict</code>: loads are four wide, but the additions form one chain in the original order, so the result has the scalar loop's bits. Right, the reassociated reduction of <code>@sum_reassoc</code>: each lane collects every fourth value and the partial sums meet after the loop, which groups the additions differently.</figcaption>
</figure>

Two more facts came from running this file on the owner's machine (LLVM 18.1.8, 2026-09-24). With the triple changed to `x86_64-apple-macosx`, `@sum_strict` is refused with "cannot prove it is safe to reorder floating-point operations": that target does not offer ordered reductions. And with `-force-vector-width=4` added, `@sum_strict` came out in the reassociated shape, with no `reassoc` flag anywhere in the input. A forced width is a hint, and the hint granted the permission. Anyone who tests a vectorizer with forced widths should know this before trusting a reduction's bits.

### Following the bits by hand

The third example sums 64 floats that alternate between $10^7$ and 1, compiled three ways by Apple clang 21 at `-O2`: with vectorization disabled, with no hint, and with `#pragma clang fp reassociate(on)`, which grants the regrouping permission for the enclosing block.[^clang-le]

--8<-- "includes/examples/optimize/p10-vectorization/reduction_reassoc.cpp.md"

Work through the scalar order. A `float` has a 24-bit significand, so between $2^{23}$ and $2^{24}$ (about 8.4 and 16.8 million) neighbouring floats are 1 apart, and between $2^{24}$ and $2^{25}$ they are 2 apart. The running total starts at 0, becomes $10^7$, then $10^7 + 1$ exactly. Adding the next $10^7$ gives 20,000,001, which lies halfway between the floats 20,000,000 and 20,000,002; round-to-nearest-even picks 20,000,000, and the 1 is gone. Every later 1 meets a total at least that large and disappears the same way, so the scalar sum is $32 \times 10^7$ = 320,000,000.

The reassociated build keeps 16 partial sums (its remark reported width 4 and interleave count 4, a term the section on cost below explains). Element `i` goes to partial sum `i` mod 16, so the 1s, at odd indices, never share a partial sum with a $10^7$. They are added only to each other and reach 32 in total, and 32 is exactly the spacing of floats near $3.2 \times 10^8$, so in this build they survive the final combination: 320,000,032, which is the exact sum. The reassociated answer is more accurate here and still wrong for Vortex, because it is not the answer the program's order specifies.

The unhinted build was vectorized too, with the same width and interleave count, and it kept the scalar bits: an ordered reduction. Its assembly shows what "less efficient" means on NEON. The loop loads 16 floats with two `ldp` instructions of two `q` registers each, moves every lane into its own scalar register, and then runs 16 dependent scalar `fadd` instructions, each waiting for the one before. The loads are wider; the chain of additions is exactly as long as in the scalar loop. Whether that is any faster is a question for [P1](p1-measure-first.md)'s protocol, not for inspection.

Vortex's rule settles which shapes it may use. [Decision 56](../decisions/numbers.md#d56) forbids reassociating or reordering `f32` and `f64` operations, so the right-hand shape of Figure 2 is out; an ordered reduction keeps every bit and is allowed, though it widens only the loads. Integers are no easier: Vortex's `i32` addition is checked ([Expressions 5.5](../specification/expressions.md#checked-integer-operations)), and regrouping a checked sum can make an overflow appear, disappear or move to a different operation, which changes the program's runtime error. The kernel avoids the question entirely by vectorizing `column`, where no lane shares a sum with another.

??? check "The third example's reassociated sum is closer to the true sum than the scalar one. Why may a Vortex compiler still not produce it?"

    Because Vortex's contract is to compute what the program's order specifies, not the closest float to the mathematical sum. Decision 56 makes every `f32` operation one IEEE 754 operation in program order, so golden outputs are the same on every conforming compiler. A more accurate answer that depends on the vector width chosen by one compiler for one target is exactly what that rule excludes. A programmer who wants a better sum can write one, such as pairwise or compensated summation, in the source, where it is part of the program.

## Trip counts and the scalar epilogue

A vector trip covers a fixed number of iterations, and a loop's trip count need not be a multiple of it. The vectorizer computes the trip count from the backedge-taken count ([O8](o8-loops.md#counting-iterations)), runs as many full vector trips as fit, and hands the remaining iterations to a **scalar epilogue**: a copy of the original loop that starts where the vector loop stopped.[^llvm-vec-ep]

The fourth example is an axpy loop over 17 elements, `y[i] = a * x[i] + y[i]`, vectorized with the width forced to 4. No reduction is involved, so forcing the width grants nothing that matters here.

--8<-- "includes/examples/optimize/p10-vectorization/scalar_epilogue.ll.md"

Read the output from the top. `entry` would jump straight to the scalar loop if the trip count were too small for one vector trip; its condition is the constant `false`, because 17 is at least 4. `vector.ph` builds `%broadcast.splat`, the scalar `a` copied into all four lanes. `vector.body` loads four elements of `x` and four of `y`, multiplies, adds and stores, and advances `%index` by 4 until it reaches 16. `middle.block` asks whether all iterations are done; the answer is known to be no, so it branches on `false` to `scalar.ph`. There `%bc.resume.val` is 16, and the original loop runs once more, for index 16.

<figure class="vx-figure">
<svg viewBox="0 0 760 250" role="img" aria-label="The seventeen indices 0 to 16 of the third example in a row. Four brackets cover indices 0 to 3, 4 to 7, 8 to 11 and 12 to 15, one per trip of the vector loop. Index 16 stands alone and is handled by the scalar epilogue. Below, the blocks the vectorizer created, in order: vector.ph, vector.body run four times, middle.block, scalar.ph starting at 16, the original loop run once, and exit.">
<defs><marker id="p10-f3-head" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="24" y="50" width="36" height="32" rx="3"/>
<text class="vx-mono" x="42" y="71" text-anchor="middle">0</text>
<rect class="vx-box" x="66" y="50" width="36" height="32" rx="3"/>
<text class="vx-mono" x="84" y="71" text-anchor="middle">1</text>
<rect class="vx-box" x="108" y="50" width="36" height="32" rx="3"/>
<text class="vx-mono" x="126" y="71" text-anchor="middle">2</text>
<rect class="vx-box" x="150" y="50" width="36" height="32" rx="3"/>
<text class="vx-mono" x="168" y="71" text-anchor="middle">3</text>
<rect class="vx-box" x="192" y="50" width="36" height="32" rx="3"/>
<text class="vx-mono" x="210" y="71" text-anchor="middle">4</text>
<rect class="vx-box" x="234" y="50" width="36" height="32" rx="3"/>
<text class="vx-mono" x="252" y="71" text-anchor="middle">5</text>
<rect class="vx-box" x="276" y="50" width="36" height="32" rx="3"/>
<text class="vx-mono" x="294" y="71" text-anchor="middle">6</text>
<rect class="vx-box" x="318" y="50" width="36" height="32" rx="3"/>
<text class="vx-mono" x="336" y="71" text-anchor="middle">7</text>
<rect class="vx-box" x="360" y="50" width="36" height="32" rx="3"/>
<text class="vx-mono" x="378" y="71" text-anchor="middle">8</text>
<rect class="vx-box" x="402" y="50" width="36" height="32" rx="3"/>
<text class="vx-mono" x="420" y="71" text-anchor="middle">9</text>
<rect class="vx-box" x="444" y="50" width="36" height="32" rx="3"/>
<text class="vx-mono" x="462" y="71" text-anchor="middle">10</text>
<rect class="vx-box" x="486" y="50" width="36" height="32" rx="3"/>
<text class="vx-mono" x="504" y="71" text-anchor="middle">11</text>
<rect class="vx-box" x="528" y="50" width="36" height="32" rx="3"/>
<text class="vx-mono" x="546" y="71" text-anchor="middle">12</text>
<rect class="vx-box" x="570" y="50" width="36" height="32" rx="3"/>
<text class="vx-mono" x="588" y="71" text-anchor="middle">13</text>
<rect class="vx-box" x="612" y="50" width="36" height="32" rx="3"/>
<text class="vx-mono" x="630" y="71" text-anchor="middle">14</text>
<rect class="vx-box" x="654" y="50" width="36" height="32" rx="3"/>
<text class="vx-mono" x="672" y="71" text-anchor="middle">15</text>
<rect class="vx-box-accent" x="696" y="50" width="36" height="32" rx="3"/>
<text class="vx-mono" x="714" y="71" text-anchor="middle">16</text>
<rect class="vx-line" x="21" y="44" width="168" height="44" rx="5"/>
<text class="vx-text-muted" x="105.0" y="34" text-anchor="middle">vector trip 1</text>
<rect class="vx-line" x="189" y="44" width="168" height="44" rx="5"/>
<text class="vx-text-muted" x="273.0" y="34" text-anchor="middle">vector trip 2</text>
<rect class="vx-line" x="357" y="44" width="168" height="44" rx="5"/>
<text class="vx-text-muted" x="441.0" y="34" text-anchor="middle">vector trip 3</text>
<rect class="vx-line" x="525" y="44" width="168" height="44" rx="5"/>
<text class="vx-text-muted" x="609.0" y="34" text-anchor="middle">vector trip 4</text>
<text class="vx-text-accent" x="714" y="110" text-anchor="middle">epilogue</text>
<rect class="vx-box-strong" x="24" y="150" width="104" height="36" rx="4"/>
<text class="vx-mono" x="76" y="173" text-anchor="middle">vector.ph</text>
<path class="vx-flow" d="M128 168 L146 168" marker-end="url(#p10-f3-head)"/>
<rect class="vx-box-strong" x="148" y="150" width="104" height="36" rx="4"/>
<text class="vx-mono" x="200" y="173" text-anchor="middle">vector.body</text>
<text class="vx-text-muted" x="200" y="206" text-anchor="middle">4 trips</text>
<path class="vx-flow" d="M252 168 L270 168" marker-end="url(#p10-f3-head)"/>
<rect class="vx-box-strong" x="272" y="150" width="104" height="36" rx="4"/>
<text class="vx-mono" x="324" y="173" text-anchor="middle">middle.block</text>
<text class="vx-text-muted" x="324" y="206" text-anchor="middle">16 of 17</text>
<path class="vx-flow" d="M376 168 L394 168" marker-end="url(#p10-f3-head)"/>
<rect class="vx-box-strong" x="396" y="150" width="104" height="36" rx="4"/>
<text class="vx-mono" x="448" y="173" text-anchor="middle">scalar.ph</text>
<text class="vx-text-muted" x="448" y="206" text-anchor="middle">i = 16</text>
<path class="vx-flow" d="M500 168 L518 168" marker-end="url(#p10-f3-head)"/>
<rect class="vx-box-accent" x="520" y="150" width="104" height="36" rx="4"/>
<text class="vx-mono" x="572" y="173" text-anchor="middle">loop</text>
<text class="vx-text-muted" x="572" y="206" text-anchor="middle">1 trip</text>
<path class="vx-flow" d="M624 168 L642 168" marker-end="url(#p10-f3-head)"/>
<rect class="vx-box-strong" x="644" y="150" width="104" height="36" rx="4"/>
<text class="vx-mono" x="696" y="173" text-anchor="middle">exit</text>
</svg>
<figcaption>Figure 3. The fourth example's 17 iterations. Four vector trips cover indices 0 to 15, four at a time; the original loop, entered through <code>scalar.ph</code> with its index set to 16, runs the one iteration that remains.</figcaption>
</figure>

The vector loop and the epilogue write disjoint elements in increasing index order, and each element is computed by exactly one of them with the same operations, so the split changes no result.

With 64 iterations and the same width, `middle.block` would branch on `true` straight to `exit`, as `@sum_strict` in the second example does: the kernel's 64-element rows need no epilogue at widths 4, 8 or 16. With interleaving, the unit that must divide the trip count is the width times the interleave count, the number of iterations one pass of the vector loop covers. A loop with a small trip count and a large vector trip can spend most of its time in the epilogue; LLVM can vectorize the epilogue itself at a narrower width,[^llvm-vec-ep] and [M8](../mlir/m8-vectorization.md#padding-instead-of-a-scalar-epilogue) shows padding as another answer.

## The kernel's column loop, lane by lane

The ikj kernel's inner loop is an axpy on one row: `c[row, column] += a[row, k] * b[k, column]`, with `a[row, k]` fixed for the whole loop. Widened by 4, one trip loads `b[k, column .. column + 3]` and `c[row, column .. column + 3]`, multiplies the four `b` values by `a[row, k]` copied into every lane, adds the products to the four `c` values and stores them back. Each lane does exactly what one scalar iteration did, for its own `column`.

The fifth example writes that shape by hand with NEON **intrinsics**, C functions that each stand for one vector instruction.[^arm-intr] It runs the loop three ways: a scalar loop with contraction switched off by `#pragma clang fp contract(off)`,[^clang-le] a NEON loop that multiplies with `vmulq_f32` and adds with `vaddq_f32`, and a NEON loop that uses `vfmaq_f32`, a **fused multiply-add** that computes `a * x + y` with a single rounding.

--8<-- "includes/examples/optimize/p10-vectorization/neon_saxpy.cpp.md"

The multiply-then-add loop matches the scalar loop on every element, bit for bit: putting iterations in lanes changed nothing about what each element computes. The fused loop differs on 3 of 16 elements, because it skips the rounding of the product. That is a different computation, whether it runs in lanes or not, and it is what [decision 56](../decisions/numbers.md#d56) forbids by default. Published kernels do not always share that rule. Arm's own worked 4 × 4 matrix multiplication builds each tile from `vld1q_f32`, `vfmaq_laneq_f32` and `vst1q_f32`, a fused step;[^arm-mm] read it for the load, broadcast and store pattern, and replace its fused step before borrowing the pattern for Vortex.

<figure class="vx-figure">
<svg viewBox="0 0 700 360" role="img" aria-label="A single box labelled a, at the left, sends four lines to four multiply units, one per lane, numbered 0 to 3 from top to bottom. Each lane multiplies a by its own element of x, adds its own element of y, and produces its own element of y prime. No line crosses between lanes.">
<defs>
<marker id="p10-f4-head" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker>
</defs>
<rect class="vx-box-strong" x="10" y="140" width="90" height="80" rx="4"/>
<text class="vx-text" x="55" y="175" text-anchor="middle">a</text>
<text class="vx-text-muted" x="55" y="195" text-anchor="middle">broadcast</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 4">
<text class="vx-text-muted" x="150" y="34">lane 0</text>
<rect class="vx-box" x="150" y="6" width="70" height="24" rx="3"/>
<text class="vx-mono" x="185" y="22" text-anchor="middle">x[0]</text>
<path class="vx-flow" d="M100 170 C 130 170, 130 42, 165 42" marker-end="url(#p10-f4-head)"/>
<path class="vx-flow" d="M185 30 L185 50" marker-end="url(#p10-f4-head)"/>
<rect class="vx-box-accent" x="150" y="50" width="70" height="40" rx="4"/>
<text class="vx-text" x="185" y="75" text-anchor="middle">×</text>
<rect class="vx-box" x="330" y="6" width="70" height="24" rx="3"/>
<text class="vx-mono" x="365" y="22" text-anchor="middle">y[0]</text>
<path class="vx-flow" d="M220 70 L310 70" marker-end="url(#p10-f4-head)"/>
<path class="vx-flow" d="M365 30 L365 50" marker-end="url(#p10-f4-head)"/>
<rect class="vx-box-accent" x="330" y="50" width="70" height="40" rx="4"/>
<text class="vx-text" x="365" y="75" text-anchor="middle">+</text>
<path class="vx-flow" d="M400 70 L500 70" marker-end="url(#p10-f4-head)"/>
<rect class="vx-box" x="500" y="58" width="110" height="24" rx="3"/>
<text class="vx-mono" x="555" y="74" text-anchor="middle">y'[0]</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4">
<text class="vx-text-muted" x="150" y="119">lane 1</text>
<rect class="vx-box" x="150" y="91" width="70" height="24" rx="3"/>
<text class="vx-mono" x="185" y="107" text-anchor="middle">x[1]</text>
<path class="vx-flow" d="M100 178 C 130 178, 130 127, 165 127" marker-end="url(#p10-f4-head)"/>
<path class="vx-flow" d="M185 115 L185 135" marker-end="url(#p10-f4-head)"/>
<rect class="vx-box-accent" x="150" y="135" width="70" height="40" rx="4"/>
<text class="vx-text" x="185" y="160" text-anchor="middle">×</text>
<rect class="vx-box" x="330" y="91" width="70" height="24" rx="3"/>
<text class="vx-mono" x="365" y="107" text-anchor="middle">y[1]</text>
<path class="vx-flow" d="M220 155 L310 155" marker-end="url(#p10-f4-head)"/>
<path class="vx-flow" d="M365 115 L365 135" marker-end="url(#p10-f4-head)"/>
<rect class="vx-box-accent" x="330" y="135" width="70" height="40" rx="4"/>
<text class="vx-text" x="365" y="160" text-anchor="middle">+</text>
<path class="vx-flow" d="M400 155 L500 155" marker-end="url(#p10-f4-head)"/>
<rect class="vx-box" x="500" y="143" width="110" height="24" rx="3"/>
<text class="vx-mono" x="555" y="159" text-anchor="middle">y'[1]</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4">
<text class="vx-text-muted" x="150" y="204">lane 2</text>
<rect class="vx-box" x="150" y="176" width="70" height="24" rx="3"/>
<text class="vx-mono" x="185" y="192" text-anchor="middle">x[2]</text>
<path class="vx-flow" d="M100 186 C 130 186, 130 212, 165 212" marker-end="url(#p10-f4-head)"/>
<path class="vx-flow" d="M185 200 L185 220" marker-end="url(#p10-f4-head)"/>
<rect class="vx-box-accent" x="150" y="220" width="70" height="40" rx="4"/>
<text class="vx-text" x="185" y="245" text-anchor="middle">×</text>
<rect class="vx-box" x="330" y="176" width="70" height="24" rx="3"/>
<text class="vx-mono" x="365" y="192" text-anchor="middle">y[2]</text>
<path class="vx-flow" d="M220 240 L310 240" marker-end="url(#p10-f4-head)"/>
<path class="vx-flow" d="M365 200 L365 220" marker-end="url(#p10-f4-head)"/>
<rect class="vx-box-accent" x="330" y="220" width="70" height="40" rx="4"/>
<text class="vx-text" x="365" y="245" text-anchor="middle">+</text>
<path class="vx-flow" d="M400 240 L500 240" marker-end="url(#p10-f4-head)"/>
<rect class="vx-box" x="500" y="228" width="110" height="24" rx="3"/>
<text class="vx-mono" x="555" y="244" text-anchor="middle">y'[2]</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 4">
<text class="vx-text-muted" x="150" y="289">lane 3</text>
<rect class="vx-box" x="150" y="261" width="70" height="24" rx="3"/>
<text class="vx-mono" x="185" y="277" text-anchor="middle">x[3]</text>
<path class="vx-flow" d="M100 194 C 130 194, 130 297, 165 297" marker-end="url(#p10-f4-head)"/>
<path class="vx-flow" d="M185 285 L185 305" marker-end="url(#p10-f4-head)"/>
<rect class="vx-box-accent" x="150" y="305" width="70" height="40" rx="4"/>
<text class="vx-text" x="185" y="330" text-anchor="middle">×</text>
<rect class="vx-box" x="330" y="261" width="70" height="24" rx="3"/>
<text class="vx-mono" x="365" y="277" text-anchor="middle">y[3]</text>
<path class="vx-flow" d="M220 325 L310 325" marker-end="url(#p10-f4-head)"/>
<path class="vx-flow" d="M365 285 L365 305" marker-end="url(#p10-f4-head)"/>
<rect class="vx-box-accent" x="330" y="305" width="70" height="40" rx="4"/>
<text class="vx-text" x="365" y="330" text-anchor="middle">+</text>
<path class="vx-flow" d="M400 325 L500 325" marker-end="url(#p10-f4-head)"/>
<rect class="vx-box" x="500" y="313" width="110" height="24" rx="3"/>
<text class="vx-mono" x="555" y="329" text-anchor="middle">y'[3]</text>
</g>
</svg>
<figcaption>Figure 4. The fifth example's vector loop, one trip. <code>vdupq_n_f32</code> copies <code>a</code> into every lane; each lane then multiplies (one rounding) and adds (a second rounding) using only its own <code>x[i]</code> and <code>y[i]</code>. No value crosses between lanes, so the four results are the four numbers the scalar loop computes. For the kernel, <code>a</code> is <code>a[row, k]</code>, <code>x</code> is a row of <code>b</code> and <code>y</code> is a row of <code>c</code>.</figcaption>
</figure>

### Your turn: one row of the kernel

Take the ikj kernel with the `column` loop widened by 4 and no interleaving, as above. Work out, without running anything:

1. How many vector trips the `column` loop makes for one pair `(row, k)`, and how many times the whole function executes the vector body.
2. Which lane of which trip computes `c[row, 5]`, and which element of `b` it multiplies by `a[row, k]`.
3. Which values stay the same for every trip of one `column` loop, so that a compiler can compute them once in front of it.

??? check "Your turn: the answers"

    1. 64 / 4 = 16 trips per `(row, k)`, and 64 × 64 × 16 = 65,536 executions of the vector body, a quarter of the kernel's 262,144 scalar iterations.
    2. Trip 2, which covers columns 4 to 7, in lane 1. It multiplies `b[k, 5]`.
    3. The copy of `a[row, k]` in all four lanes, and the starting addresses of row `row` of `c` and row `k` of `b`. This is why the fourth example builds `%broadcast.splat` in `vector.ph`, outside the loop: loop-invariant code motion from [O6](o6-redundancy.md), reappearing inside the vectorizer.

## Choosing a width: cost and interleaving

Legality says which widths are allowed; the cost model picks among them. LLVM's loop vectorizer uses a cost model of the target's vector instructions to choose the vector width, and the scalar loop remains the alternative when no width pays.[^clang-le] It also picks an **interleave count**: how many vector trips to unroll into one pass of the loop body, so that independent vector instructions are in flight at once, chosen from register pressure and code size.[^clang-le] LLVM's documentation motivates this with execution ports: a single chain of additions can use only one.[^llvm-vec] [P5](p5-microarchitecture.md#latency-throughput-and-the-number-of-chains) explains why a core needs several independent chains in flight to reach its throughput.

Both choices appear in the remark `-Rpass=loop-vectorize` prints, as in "vectorized loop (vectorization width: 4, interleaved count: 4)" for the third example's loops. They are estimates, not measurements. The `max_reference` loop and the kernel's `column` loop are each a few instructions of arithmetic around loads and stores, and whether four lanes make them four times faster depends on whether the loads keep up. [P3](p3-roofline.md#ceilings-below-the-roof) draws the lower ceiling for code without SIMD instructions, the one vectorization lifts, and shows when memory, not arithmetic, sets the limit instead. A measurement of a vectorized loop in isolation also needs [P1](p1-measure-first.md#keeping-the-compiler-from-helping-too-much)'s barrier, or the optimizer may delete a loop whose result is never used.

## NEON, SVE and SME

Every example so far uses **NEON**, AArch64's baseline SIMD instructions (the `AdvSIMD` feature below): fixed 128-bit registers and a fixed number of lanes per element type.[^arm-intr] Two newer extensions extend the idea in different directions.

**SVE**, the Scalable Vector Extension, lets each implementation choose its vector register length, between 128 and 2,048 bits, and lets one binary run correctly at every length without recompilation, an approach its designers call vector-length agnostic.[^sve17] Its 32 registers, `z0` to `z31`, extend the NEON registers `v0` to `v31`.[^aapcs64]

A **predicate** is a register of per-lane on and off bits, and SVE's `while` instructions set one from a loop counter and its limit, so the final, partial trip of a loop can run with the missing lanes switched off rather than as scalar code.[^sve17] That is valuable when nobody knows the trip count or the machine in advance. Vortex's array extents are constants in the type, so a compiler for a known target can pick a width that divides them and emit no epilogue at all.

The same paper names the cost for floating point. A vectorized reduction's order depends on the vector length, so under SVE the same binary could round differently on two machines; SVE's answer is `fadda`, a strictly ordered floating-point add reduction for code that needs the original order.[^sve17] That is the hardware form of the ordered reduction above, and the only reduction shape decision 56 allows.

**SME**, the Scalable Matrix Extension, adds a streaming SVE mode and an array of storage called `ZA` that instructions address either as vectors or as two-dimensional tiles.[^hellosme] Its core instructions take two vector registers and add their outer product into a tile; `FMOPA` is the floating-point one. On the M4 the streaming vector length is 512 bits, so one FP32 `FMOPA` adds a 16 × 16 tile of products, 512 floating-point operations, in one instruction. Remke and Breuer measured more than 2.3 FP32 TFLOPS from M4's SME, and their generated small matrix-multiplication kernels outperformed the vendor-optimized BLAS in almost all configurations they tested.[^hellosme]

On the owner's M4 Pro, `sysctl hw.optional.arm` reports `AdvSIMD`, `FEAT_SME` and `FEAT_SME2` as 1, `sme_max_svl_b` as 64 bytes (512 bits), and no `FEAT_SVE` key (checked 2026-09-24). SVE instructions are therefore available only inside SME's streaming mode. That is one machine: a back end must ask the target what it has rather than assume it, and [P4](p4-counters-and-tools.md) returns to the tools for asking. Before Vortex uses an outer-product instruction for `f32`, it also has to check against decision 56 whether each accumulation rounds once or twice; that is a question for the instruction's definition in Arm's architecture reference, not for a benchmark.

## For Vortex

!!! vortex "Exercise"

    **Build** loop vectorization for innermost loops over fixed-size arrays, on the loop structure and trip counts from [O8](o8-loops.md#for-vortex), the dependence distances from [P6](p6-dependence-analysis.md#for-vortex), the `noalias` facts from [O9](o9-alias-analysis.md#for-vortex) and the interchange from [P7](p7-loop-transformations.md#for-vortex), targeting AArch64 NEON.

    1. **Legality.** For each innermost loop, collect the loop-carried dependences it carries and decide the largest width they allow. Reject a loop that still contains a call or a runtime check, or an access whose overlap with a store is not ruled out by `&mut`.
    2. **Widening.** For `f32` loops, emit a vector body at width 4 with a separate multiply and add (never a fused multiply-add), plus a scalar epilogue that resumes the original loop when the trip count is not a multiple of the width.
    3. **Reductions.** Refuse to vectorize a loop whose body updates an `f32`, `f64` or checked integer reduction variable, and say so in a remark. As a stretch, once everything else passes, add ordered `f32` reductions and measure whether they help.
    4. **Remarks**, in the stream from [O1](o1-optimizer-contract.md#for-vortex): a passed remark naming the loop, the width and the reason no runtime check was needed; a missed remark naming the blocking fact, such as the reduction variable or the dependence and its distance.

    **Not yet:** SVE and SME; the SLP vectorizer; interleaving; loops that keep a runtime check; runtime alias checks and loop versioning; fused multiply-add or any reassociation, which wait for an explicit opt-in ([P11](p11-floating-point.md)); vectorizing an outer loop.

    **Proof that it works:**

    - The contract test from [O1](o1-optimizer-contract.md#for-vortex) passes on the whole suite with vectorization on: standard output, the error line and the exit status match the unvectorized build byte for byte.
    - The bits gate from [P16](p16-capstone.md#the-bits-gate): the ikj kernel with vectorization on and off prints identical `c` for the stage 10 inputs.
    - Golden remark files for five small programs: a loop with distance-2 flow dependence (width 2 at most, or refused if you support only 4), a loop with distance-3 anti-dependence (vectorized), an `f32` sum (refused, naming the reduction), an `i32` sum whose third addition overflows (refused, and the runtime error line unchanged), and element-wise loops of 64, 65 and 67 elements (0, 1 and 3 epilogue iterations).
    - A measurement under [P1](p1-measure-first.md)'s protocol on your machine, with the date and your compiler's version:

    | Loop | Width | Vectorized? | Epilogue iterations | Time, scalar | Time, vectorized |
    | --- | --- | --- | --- | --- | --- |
    | ikj kernel, `column` loop | | | | | |
    | element-wise, 65 elements | | | | | |
    | `f32` sum, 64 elements | | | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **What does each LLVM vectorizer widen?** The loop vectorizer folds consecutive iterations of one loop into one vector trip; the SLP vectorizer packs isomorphic statements of straight-line code, loop or no loop.
    - **How does a dependence limit the width?** A value that flows from one iteration to another $d$ iterations later allows at most $d$ iterations per vector trip; a loop that carries no dependence allows any width.
    - **What else must a vectorizer know?** A trip count it can compute, no calls it cannot widen, and either proof that arrays do not overlap or a runtime check with a scalar fallback.
    - **Why is vectorizing the kernel's `column` loop exact and its `k` loop not?** Lanes of the `column` loop are different elements of `c`; widening the `k` loop regroups one element's additions, and floating-point addition rounds differently in a different grouping.
    - **What is an ordered reduction?** A vectorized reduction that adds lanes into one running total in the original order, keeping the scalar bits; LLVM offers it on AArch64 and RISC-V, and on NEON it widens the loads but not the chain of adds.
    - **What is a scalar epilogue?** The original loop, resumed where the vector loop stopped, for the iterations left over when the trip count is not a multiple of width times interleave count.
    - **Why must Vortex's vector loop not use `vfmaq_f32`?** A fused multiply-add rounds once instead of twice, so it changes results, and decision 56 forbids contraction by default.

## Where this comes back

!!! next "You will use this again in"

    - [P11. Floating point under optimization](p11-floating-point.md): *reduction vectorization needs `reassoc`*, *fused multiply-add*, *SIMD lanes as independent chains*
    - [P12. Anatomy of a fast GEMM](p12-fast-gemm.md): *the number and width of vector registers*, *fused multiply-add latency*
    - [P13. Multithreading](p13-multithreading.md): *independent iterations*, *the k-split reduction*
    - [P16. Capstone: the ladder, measured](p16-capstone.md): *rung 3, vectorize across `column`*
    - [M8. Vectorization in MLIR](../mlir/m8-vectorization.md): *vector width*, *padding instead of a scalar epilogue*
    - [G2. The SIMT execution model](../gpu/g2-simt.md): *one iteration per lane*

## Sources and further reading

Read LLVM's "Auto-Vectorization in LLVM" first: it is short, and every feature this chapter uses has a section there. Then read sections 2 and 3 of Larsen and Amarasinghe for SLP, and the section of the Language Reference on `llvm.vector.reduce.fadd`, which is four paragraphs. Remke and Breuer's paper on SME is the best short introduction to what the M4's matrix unit can do.

[^llvm-vec]: LLVM Project, "Auto-Vectorization in LLVM", sections "The Loop Vectorizer" (with "Runtime Checks of Pointers", "If Conversion", "Vectorization of function calls" and "Partial unrolling during vectorization") and "The SLP Vectorizer", read on 2026-09-24. <https://llvm.org/docs/Vectorizers.html>
[^llvm-vec-red]: LLVM Project, "Auto-Vectorization in LLVM", section "Reductions", read on 2026-09-24. <https://llvm.org/docs/Vectorizers.html#reductions>
[^llvm-vec-ep]: LLVM Project, "Auto-Vectorization in LLVM", sections "Loops with unknown trip count" and "Epilogue Vectorization", read on 2026-09-24. <https://llvm.org/docs/Vectorizers.html#epilogue-vectorization>
[^slp00]: Samuel Larsen and Saman Amarasinghe, "Exploiting Superword Level Parallelism with Multimedia Instruction Sets", *Proceedings of the ACM SIGPLAN 2000 Conference on Programming Language Design and Implementation (PLDI)*, 2000: the abstract, sections 2.1 and 3. <https://doi.org/10.1145/349299.349320>
[^langref]: LLVM Project, "LLVM Language Reference Manual", section "'llvm.vector.reduce.fadd.*' Intrinsic", read on 2026-09-24. <https://llvm.org/docs/LangRef.html>
[^clang-le]: Clang Project, "Clang Language Extensions", sections "Extensions for loop hint optimizations" (including "Vectorization, Interleaving, and Predication") and "Extensions to specify floating-point flags" (`#pragma clang fp reassociate` and `#pragma clang fp contract`), read on 2026-09-24. <https://clang.llvm.org/docs/LanguageExtensions.html>
[^aapcs64]: Arm, "Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)", release 2025Q4, sections "SIMD and Floating-Point registers" and "Scalable vector registers". <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
[^arm-intr]: Arm, "Intrinsics", the reference for Neon, SVE, SVE2, SME and Helium intrinsics. <https://developer.arm.com/architectures/instruction-sets/intrinsics/>
[^arm-mm]: Arm, "Optimizing C code with Neon intrinsics", version 0201, "Example - matrix multiplication". <https://developer.arm.com/documentation/102467/0201/Example---matrix-multiplication>
[^sve17]: Nigel Stephens et al., "The ARM Scalable Vector Extension", *IEEE Micro* 37(2), 2017: the abstract and introduction, section 2.3.2 "Predicate-driven loop control", and the section "Floating Point" on `fadda`. <https://doi.org/10.1109/MM.2017.35> (open copy: <https://arxiv.org/abs/1803.06185>)
[^hellosme]: Stefan Remke and Alexander Breuer, "Hello SME! Generating Fast Matrix Multiplication Kernels Using the Scalable Matrix Extension", 2024: the abstract and section II-B "Scalable Matrix Extension" (streaming SVE mode, the `ZA` array, `FMOPA`, the M4's 512-bit streaming vector length), read on 2026-09-24. <https://arxiv.org/abs/2409.18779>
