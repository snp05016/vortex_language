# The CPU matmul ladder

<p class="page-intro">One computation, the naive matrix multiplication from stage 10, made faster one transformation at a time. This page is the map of that climb: every rung, what it changes, whether it keeps the naive loop's exact bits, the chapters that teach it, the Vortex compiler feature it becomes, and the remark the compiler prints when it applies it.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 20 minutes · Builds on: [Stage 10. Matrix multiplication](../compiler/guide/stage-10-matrix-multiplication.md), [P1. Measure first](p1-measure-first.md), [P11. Floating point under optimization](p11-floating-point.md)</p>

## The kernel that never changes

Every rung starts from this function, the matrix product from [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for) at the 64 by 64 shape the book uses throughout:

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

The source stays exactly as written. What changes from rung to rung is the machine code the compiler produces from it. A **rung** is one version of that machine code: the previous rung plus one transformation. The **ladder** is the whole sequence, from the naive triple loop at rung 0 to a tuned, threaded kernel near the top. This is [principle 2](../philosophy.md#2-say-what-to-calculate-then-decide-how-to-run-it) at work: the program says what to calculate, and each rung is a different decision about how to run it.

The ladder is the thread that ties the performance half of this book together. Each P chapter teaches the idea behind one or two rungs; [P16](p16-capstone.md) puts them in order and measures them; the [CPU matmul case study](../project/case-studies/cpu-matmul-ladder.md) is where the owner's own results will be reported. This page is the index to all of them.

## How to climb

Climbing well is a procedure, not a burst of optimization. Each rung goes through the same five steps, in the same order, before the next one starts.

<figure class="vx-figure">
<svg viewBox="0 0 760 210" role="img" aria-label="Five boxes in a row, joined by arrows: change one thing, read the remark, bits gate, measure with P1's protocol, record the row. An arrow from the last box loops back over the top to the first, labelled next rung. Below the bits gate, a red box reads: differs and is not a named opt-in, so fix it and do not time it.">
<defs><marker id="ladder-f1-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text-muted" x="375" y="18" text-anchor="middle">next rung</text>
<line class="vx-line" x1="675" y1="60" x2="675" y2="28"/>
<line class="vx-line" x1="675" y1="28" x2="75" y2="28"/>
<line class="vx-line" x1="75" y1="28" x2="75" y2="58" marker-end="url(#ladder-f1-head)"/>
<rect class="vx-box" x="10" y="60" width="130" height="54" rx="5"/>
<text class="vx-text" x="75" y="83" text-anchor="middle">1. Change</text>
<text class="vx-text-muted" x="75" y="101" text-anchor="middle">one thing</text>
<rect class="vx-box" x="160" y="60" width="130" height="54" rx="5"/>
<text class="vx-text" x="225" y="83" text-anchor="middle">2. Read</text>
<text class="vx-text-muted" x="225" y="101" text-anchor="middle">the remark</text>
<rect class="vx-box" x="310" y="60" width="130" height="54" rx="5"/>
<text class="vx-text" x="375" y="83" text-anchor="middle">3. Bits gate</text>
<text class="vx-text-muted" x="375" y="101" text-anchor="middle">same as rung 0?</text>
<rect class="vx-box" x="460" y="60" width="130" height="54" rx="5"/>
<text class="vx-text" x="525" y="83" text-anchor="middle">4. Measure</text>
<text class="vx-text-muted" x="525" y="101" text-anchor="middle">P1's protocol</text>
<rect class="vx-box" x="610" y="60" width="130" height="54" rx="5"/>
<text class="vx-text" x="675" y="83" text-anchor="middle">5. Record</text>
<text class="vx-text-muted" x="675" y="101" text-anchor="middle">one table row</text>
<line class="vx-line" x1="140" y1="87" x2="158" y2="87" marker-end="url(#ladder-f1-head)"/>
<line class="vx-line" x1="290" y1="87" x2="308" y2="87" marker-end="url(#ladder-f1-head)"/>
<line class="vx-line" x1="440" y1="87" x2="458" y2="87" marker-end="url(#ladder-f1-head)"/>
<line class="vx-line" x1="590" y1="87" x2="608" y2="87" marker-end="url(#ladder-f1-head)"/>
<line class="vx-line" x1="375" y1="114" x2="375" y2="144" marker-end="url(#ladder-f1-head)"/>
<rect class="vx-box-bad" x="250" y="146" width="250" height="54" rx="5"/>
<text class="vx-text" x="375" y="169" text-anchor="middle">differs, not a named opt-in</text>
<text class="vx-text-muted" x="375" y="187" text-anchor="middle">a bug: fix it, do not time it</text>
</svg>
<figcaption>Figure 1. The climb, one rung at a time. The bits gate comes before the stopwatch: a rung that changes the answer without permission is a bug, and its timing means nothing.</figcaption>
</figure>

1. **Change one thing.** Turn on one more transformation in the compiler, and nothing else. If two changes go in together and the time moves, you cannot say which one moved it. The rung is compiled from the unchanged source above, so the ladder measures the compiler and not a hand-edited copy.
2. **Read the remark.** An **optimization remark** is a message in which the compiler reports a decision: a transformation it made (a *passed* remark), one it tried and refused (a *missed* remark), or a fact it worked out along the way (an *analysis* remark). [O1](o1-optimizer-contract.md#what-a-good-remark-says) sets out what a good one contains. If the rung's remark does not say the transformation happened, stop here: the time you are about to measure is the previous rung's.
3. **Check the bits.** The **bits gate** runs the new rung and rung 0 on the same inputs and compares every output element as a 32-bit pattern, not with `==`. The inputs must be able to show a difference: exact small whole numbers add up the same in any order, so the gate also uses values that round, values of widely different sizes, and inputs that produce signed zeros. [P16](p16-capstone.md#the-bits-gate) builds the gate and the [measuring page](../project/measuring.md#bitwise-identity-where-the-ladder-promises-it) fixes its rules for the project.
4. **Measure.** Time the rung and the rung below it under [P1's protocol](p1-measure-first.md#comparing-two-versions): many independent runs, interleaved so that neither version gets the warmer machine, summarized by a median and a 95% confidence interval. The number that matters is the **speedup over the previous rung**, the ratio of the old median time to the new one, with its own interval. An interval that contains 1.0 means "no difference shown", and the row says so.
5. **Record the row.** Write down the time, the rate, the speedup, the bits result and the remark, with the machine, compiler commit and date. Then start the next rung from this one.

The order of the steps matters as much as the order of the rungs. Checking the bits before timing means you never spend an afternoon explaining a speedup that came from computing something else. Reading the remark before timing means you never credit a transformation that did not run.

## Which rungs keep the bits

Floating-point addition rounds after every step, so $(a + b) + c$ and $a + (b + c)$ can differ in the last bits ([P11](p11-floating-point.md#why-a-b-c-is-not-always-a-b-c)). The whole bits column below follows from one observation about this kernel. Rung 0 builds each element `c[row, column]` by adding its 64 products one at a time, in increasing `k`, into a running sum that starts at +0.0, rounding once per multiply and once per add. **A rung keeps the bits exactly when it keeps that sequence for every element**, whatever order it visits the elements in.

Most of the climb changes only the order *between* elements: which element is worked on first, which lane of a vector register holds it, which core computes it. None of that touches the sequence *within* an element, so none of it can change a bit. Three things do touch it, and they are the three rows marked "differs" below:

- **FMA contraction** changes the number of roundings: one per multiply-add instead of two.
- **BLIS-style accumulation** and a **split-`k` parallel reduction** change the grouping: each element's sum is cut into partial sums that are added together at the end.

[P11](p11-floating-point.md#the-rule-every-reordering-pass-needs) states the rule for any reordering pass; this is that rule, specialized to one kernel.

## The rungs

The remarks in the last column are examples of what a Vortex compiler could print, not the output of any existing tool. They use a short form, the decision, then the pass, then the message; your compiler adds the source span that [O1](o1-optimizer-contract.md#what-a-good-remark-says) asks for. Angle brackets mark values your compiler fills in from its model or its measurements. The three rungs that change the answer carry an `[opt-in: ...]` label, because they run only when the program asks for them.

| Rung | What changes | Bits vs rung 0 | Taught in | Vortex feature | Example remark |
| --- | --- | --- | --- | --- | --- |
| 0. Naive | Nothing: the stage 10 loop, `row`, `column`, `k`, with bounds and overflow checks in the inner loop | Baseline | [Stage 10](../compiler/guide/stage-10-matrix-multiplication.md), [P2](p2-memory-hierarchy.md#locality-in-the-stage-10-kernel-walked-by-hand) | v0.1, [milestone 10](../roadmap.md#milestone-10-matrix-multiplication) | `analysis checks: multiply keeps its bounds and overflow checks inside the k loop` |
| 1. Scalar cleanup | Locals in registers, the constant shapes propagated, address arithmetic hoisted out of the loop, checks deleted where a range proof shows they cannot fail | Identical: only integer work and checks change, never the `f32` operations | [O5](o5-constants-and-dead-code.md), [O6](o6-redundancy.md), [O8](o8-loops.md#removing-a-check-with-a-proof) | Dead-code elimination and constant folding ([after v0.1](../roadmap.md#after-v01)), plus range-based check removal | `passed check-elim: removed 6 bounds checks in multiply: proved 0 <= row, column, k < 64` |
| 2. Loop interchange | `row`, `column`, `k` becomes `row`, `k`, `column`, with `c` zeroed first and updated in place, so `b` and `c` are walked along rows | Identical: each element still receives its products in increasing `k` | [P6](p6-dependence-analysis.md), [P7](p7-loop-transformations.md#interchange) | Loop transformations, tiling and fusion | `passed interchange: swapped column and k: b and c now unit-stride` |
| 3. Vectorize | The `column` loop runs four `f32` lanes per NEON instruction | Identical, as long as the vector multiply and add stay separate: each lane is a different element | [O9](o9-alias-analysis.md), [P10](p10-vectorization.md#the-kernels-column-loop-lane-by-lane) | SIMD vectorization | `passed vectorize: column loop, 4 lanes; no runtime alias check: c is &mut` |
| 4. Tile | The loops are cut into blocks sized from the cache facts, so the data a block reuses stays in cache | Identical, if each element's `k` tiles run in increasing order and `c` is updated in place | [P2](p2-memory-hierarchy.md), [P8](p8-cache-blocking.md#keeping-the-order-keeping-the-bits) | Loop transformations, tiling and fusion; cost models | `passed tile: row, k, column tiled <mc> x <kc> x <nc> for L1d <size> (model estimate)` |
| 5. Pack | Blocks of `a` and panels of `b` are copied into contiguous buffers in the order the kernel reads them | Identical: packing only copies values | [P2](p2-memory-hierarchy.md#the-tlb-caching-translations-not-data), [P12](p12-fast-gemm.md#packing-copy-once-read-many-times) | Layout selection ([purpose](../philosophy.md#purpose)) | `passed pack: a into <mc> x <kc> blocks, b into <kc> x <nr> panels: copies only` |
| 6. Register micro-kernel | Unroll-and-jam: a small `mr` by `nr` block of `c` lives in registers as accumulators, loaded from `c` before each `k` panel | Identical: each accumulator continues the element's running sum | [P5](p5-microarchitecture.md#latency-throughput-and-the-number-of-chains), [P7](p7-loop-transformations.md#unroll-and-jam-and-register-blocks), [P12](p12-fast-gemm.md#the-register-blocked-micro-kernel) | Loop transformations (unroll-and-jam) | `passed unroll-and-jam: micro-kernel <mr> x <nr>, accumulators loaded from c` |
| 6b. BLIS-style accumulation | The same micro-kernel with its accumulators started at zero for every `k` panel, and each panel's total added to `c` | **Differs**: each element's sum is regrouped at every panel boundary | [P12](p12-fast-gemm.md#two-ways-to-accumulate), [P11](p11-floating-point.md) | A future opt-in floating-point mode ([§4.4](../specification/types-and-values.md#44-floating-point-values)) | `passed accumulate: zero-started accumulators, added to c per k panel [opt-in: regroup]` |
| 7. FMA contraction | Each multiply and the add that follows it become one fused multiply-add instruction | **Differs**: one rounding per step instead of two | [P5](p5-microarchitecture.md), [P11](p11-floating-point.md#fused-multiply-add-one-rounding-instead-of-two) | A future opt-in floating-point mode ([decision 56](../decisions/numbers.md#d56)) | `passed contract: fused a[row, k] * b[k, column] + c into fma [opt-in: contract]` |
| 8. Threads | Blocks of rows (or columns) of `c` go to different cores, each thread with its own packing buffer | Identical: each element is computed by one thread, in rung 6's order | [P13](p13-multithreading.md#which-loop-in-the-kernel-to-split) | Multicore CPU execution | `passed parallel: over row blocks, <t> threads, no reduction` |
| 8b. Split-`k` reduction | Threads take different ranges of `k` for the same elements; their partial sums are added at the end | **Differs**: each element's sum is regrouped into per-thread partial sums | [P13](p13-multithreading.md#splitting-k-needs-a-reduction) | Multicore CPU execution, under the opt-in mode | `passed parallel: over k, <t> threads, partial sums reduced [opt-in: regroup]` |
| 9. Tuning | The block sizes `mc`, `kc`, `nc`, `mr` and `nr` are chosen by a model or by search | Identical, if the search space holds only bit-preserving variants | [P15](p15-choosing-parameters.md#what-a-tuner-may-not-do) | Optimization diagnostics and cost models; auto-tuning | `passed tune: <values> from <model or search>; <n> scored, <m> removed by the bits filter` |
| 10. SME (stretch) | The micro-kernel uses the Arm matrix extension's outer-product instructions, on chips that have them | Depends on the instructions and mode used; it must pass the gate like any rung | [P10](p10-vectorization.md#neon-sve-and-sme), [G11](../gpu/g11-matrix-units.md) | Hardware-specific code generation ([purpose](../philosophy.md#purpose)) | `passed isel: micro-kernel on SME outer products: target has FEAT_SME` |

Rungs 4 to 6 rebuild, one transformation at a time, the layered design of Goto and van de Geijn: a packed block of one input kept in the L2 cache, packed panels of the other streamed through L1, and a small block of `c` held in registers.[^goto08] BLIS reorganizes the same design as loops around one small micro-kernel, the only part written for a particular processor.[^blis15] [P12](p12-fast-gemm.md#the-five-loops-around-one-micro-kernel) takes that structure apart.

Rungs 9 and 10 are not transformations of the same kind as the others. Tuning changes no loop; it picks the numbers the earlier rungs left open. SME changes which instructions the micro-kernel uses, which is the back end's job. Both still pass through the same five steps.

??? check "A pass vectorizes the `k` loop of rung 0 instead of the `column` loop. Which row of the table does it belong to?"

    None of the strict rows. Across `k`, the lanes hold products of the *same* element, and summing the lanes at the end regroups that element's sum, the same kind of change as rung 8b. LLVM's loop vectorizer refuses to reassociate a floating-point reduction unless at least `-fassociative-math -fno-signed-zeros -fno-trapping-math` is in effect; on AArch64 it can emit an ordered reduction instead, which keeps the exact result.[^llvm-vec] The `column` loop needs neither, which is why rung 3 vectorizes it and not `k`.

## The three rungs that change the answer

Each of the three "differs" rows breaks the rule in a different place, and each has a reason to exist anyway: it is how fast libraries and fast hardware prefer to work.

**Rung 6b, BLIS-style accumulation.** BLIS documents its micro-kernel as computing `C11 := beta * C11 + alpha * A1 * B1`:[^blis-k] the product of one `k` panel is formed in registers starting from zero, then added to `c`. For one element with `k` panels of length `kc`, the naive loop computes one long chain of additions, while 6b computes a chain per panel and then adds the panel totals to `c`. Those are different groupings of the same terms, so they can round differently. Rung 6 avoids this by loading the accumulators from `c`, so each panel continues the element's running sum instead of starting a new one. [P16](p16-capstone.md#a-worked-example-by-hand) works through four products where the two forms end at different values. Rung 4 can slide into this shape by accident: summing each `k` tile into a fresh zero-started temporary makes it 6b.

**Rung 7, FMA contraction.** A **fused multiply-add** computes `x * y + z` with a single rounding at the end; the separate multiply and add round twice, once after the product and once after the sum. The fused result is often closer to the exact value, but it is a different value, and decision 56 promises the programmer the two-rounding one.[^decisions-numbers] This rung is also the easiest to take by accident. Clang's default, `-ffp-contract=on`, fuses a multiply and an add written in the same statement,[^clang-um] and in LLVM IR the permission is a `contract` flag on each instruction.[^langref] A Vortex back end that emits LLVM IR must leave that flag off in strict mode, and every C++ reference you build must use `-ffp-contract=off`, or the reference is rung 7.

**Rung 8b, split-`k` reduction.** Splitting rows of `c` across threads gives each thread its own elements, with nothing to combine. Splitting `k` puts several threads on the same element, so each needs a private partial sum, and the partial sums are added at the end: a regrouping, like 6b. Smith and coauthors treat parallelizing this loop as a special case that needs zeroed copies of `c` and a reduction, and suggest it only when `c` is small;[^smith14] BLIS's threading documentation does not parallelize that loop at all, for the same reason.[^blis-mt] The order in which the partial sums are added must also be fixed, or two runs of the same program can print different bits, which the [safety philosophy](../philosophy.md#safety-philosophy) requires to be documented.

??? check "Your C++ reference, built with Apple clang and no special flags, disagrees with rung 0 in a few elements, and rung 1 agrees with rung 0 everywhere. Which result is wrong?"

    Neither may be wrong, but the reference is not rung 0. With clang's default `-ffp-contract=on`, `sum += a * b` compiles to a fused multiply-add, so the reference is rung 7. Rebuild it with `-ffp-contract=off` and compare again. The disagreement was a difference between two rungs, not a bug in yours.

## Strict mode and the price of strictness

**Strict mode** is the compiler's default: every `f32` operation in the program is one IEEE 754 operation, rounded once, never contracted, reassociated, reordered or evaluated in a wider format. That is [decision 56](../decisions/numbers.md#d56), and the specification allows relaxed modes only as an explicit opt-in ([§4.4](../specification/types-and-values.md#44-floating-point-values)).[^decisions-numbers] In strict mode the compiler applies every rung marked "identical" without being asked, and refuses the three marked "differs". A refusal is not silent. It is a missed remark that names the rule:

```text
missed accumulate: not zero-started: would regroup the k sum of each c element (decision 56)
missed contract: not fused: strict mode keeps one rounding per f32 operation (decision 56)
missed parallel: not split over k: would regroup the k sum; parallel over row blocks instead
```

The last line shows the useful habit: a missed remark that also says what the compiler did instead. [Principle 6](../philosophy.md#6-explain-performance-decisions) asks for exactly this, so that the programmer never has to guess why a kernel is slower than it could be.

The opt-in rungs are faster on many machines, which is why libraries use them. The **price of strictness** is how much speed strict mode leaves on the table: the speedup each opt-in rung gains over the strict rung it starts from. It has two parts, and both are measured, never assumed.

- **The arithmetic ceiling.** A strict build cannot use fused multiply-adds, so its peak arithmetic rate is the rate of separate multiplies and adds. A contracted build's peak is the rate of fused ones. Measure both ceilings the way [P3](p3-roofline.md#measuring-your-own-machine) measures a peak, and hold each rung to the ceiling it is allowed to reach. Hoefler and Belli ask for such an upper bound next to every measured result;[^hb15] the roofline model is the usual form of that bound.[^roofline09]
- **The kernel's own gap.** The speedup of 6b over 6, of 7 over 6, and of 8b over 8, each with its confidence interval, at the shapes you report. At a small shape like 64 by 64, `c` is small, which is the one case in which Smith and coauthors suggest splitting `k` at all.[^smith14]

Other people's numbers do not transfer. Boehm measured two fused multiply-adds per cycle, 32 flops per cycle, on an Intel i7-6700, and built his whole CPU ladder with `-ffast-math`, which enables every opt-in on this page at once.[^boehm] That shows the shape of a relaxed climb on one Intel chip. It says nothing about a strict climb on an Apple M4 Pro, and the price of strictness on your machine is a number only your own measurements can give.

The price is worth knowing because it is the case a future relaxed floating-point option in Vortex would have to make. If the gap is small on the machines Vortex targets, strict mode costs little and the option can wait. If it is large, the option has a measured reason to exist, and the remarks above tell the programmer exactly which rungs it would allow.

The same rule governs rung 9. [The performance philosophy](../philosophy.md#performance-philosophy) says auto-tuning must not change the observable meaning of a program, so the tuner in strict mode filters its candidates through the bits gate before scoring any of them ([P15](p15-choosing-parameters.md#what-a-tuner-may-not-do)). A faster candidate that regroups the sum is not a candidate.

## Your results

The table below is empty on purpose. Every number in it must come from your own runs on your own machine, measured with P1's protocol, after the rung has passed the bits gate. No number from this book, from another chapter, or from someone else's blog belongs in it.

Before the first row, record the machine as the harness queries it (the chip, the core counts, and the cache sizes from `sysctl` on macOS), the operating system, the compiler commit, every flag, the shape, the repetition counts and the date. [P16's report](p16-capstone.md#the-report-you-fill-in) and the [measuring page](../project/measuring.md#a-results-table-ready-to-fill) show the full form, with confidence intervals and comparisons against Accelerate, OpenBLAS and BLIS; this is the short version for following the climb.

For an `M` by `K` matrix times a `K` by `N` matrix, the rate is

$$
\text{GFLOP/s} = \frac{2MNK}{t \times 10^{9}}
$$

where $t$ is the median time of one multiplication in seconds, counting each multiply-add as two operations ([measuring](../project/measuring.md#flops-for-matrix-multiplication)). "Previous rung" means the row directly above, except for the opt-in rows, which compare with the strict rung they start from: 6b and 7 with rung 6, and 8b with rung 8.

| Rung | Time (median) | GFLOP/s | Speedup over the previous rung | Bits identical? |
| --- | --- | --- | --- | --- |
| 0. Naive | | | baseline | baseline |
| 1. Scalar cleanup | | | | |
| 2. Loop interchange | | | | |
| 3. Vectorize | | | | |
| 4. Tile | | | | |
| 5. Pack | | | | |
| 6. Register micro-kernel | | | | |
| 6b. BLIS-style accumulation (opt-in) | | | | |
| 7. FMA contraction (opt-in) | | | | |
| 8. Threads | | | | |
| 8b. Split-`k` reduction (opt-in) | | | | |
| 9. Tuning | | | | |
| 10. SME (stretch) | | | | |

Two shapes are the minimum. At 64 by 64 the three matrices take 48 KiB, which fits in the 128 KiB L1 data cache of the owner's M4 Pro performance cores,[^local] so the tiling and packing rungs have little to do there; add a shape whose matrices do not fit in the last cache. And add one shape that no tile or unroll factor divides evenly, so that the edge code runs and the bits gate sees it.

A rung that does not help is still a result. If rung 5's interval against rung 4 contains 1.0, write that down; packing exists to make rung 6 possible, and the useful measurement may be one rung later ([P16](p16-capstone.md#did-the-rung-help-comparing-two-measurements)).

## Where the ladder goes next

- [P16. Capstone: the ladder, measured](p16-capstone.md) builds the bits gate, the speedup intervals and the full report against Accelerate, OpenBLAS and BLIS.
- [A1. CPU matmul ladder generated by the compiler](../project/case-studies/cpu-matmul-ladder.md) is the case study where the owner's results for this ladder will be published, with the setup, ceilings, evidence and threats to validity.
- [G10. The GPU matmul ladder](../gpu/g10-matmul-ladder.md) climbs the same computation on a GPU, where the rungs answer different hardware facts but the rule about splitting `k` is the same.
- [Stage 10](../compiler/guide/stage-10-matrix-multiplication.md#why-speed-can-wait) explains why rung 0 came first, and why speed could wait until the answer was right.

## Sources and further reading

[^decisions-numbers]: Vortex documentation, [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56): each `f32` and `f64` operation is one IEEE 754 operation, rounded to nearest; no contraction, reassociation, reordering or wider evaluation.
[^goto08]: Kazushige Goto and Robert A. van de Geijn, "Anatomy of High-Performance Matrix Multiplication", *ACM Transactions on Mathematical Software* 34(3), 2008. <https://doi.org/10.1145/1356052.1356053> (authors' copy: <https://www.cs.utexas.edu/~flame/pubs/GotoTOMS_revision.pdf>)
[^blis15]: Field G. Van Zee and Robert A. van de Geijn, "BLIS: A Framework for Rapidly Instantiating BLAS Functionality", *ACM Transactions on Mathematical Software* 41(3), 2015. <https://doi.org/10.1145/2764454> (authors' copy: <https://www.cs.utexas.edu/~flame/pubs/blis1_toms_rev3.pdf>)
[^llvm-vec]: LLVM Project, "Auto-Vectorization in LLVM", section "Reductions": floating-point reductions are vectorized only with at least `-fassociative-math -fno-signed-zeros -fno-trapping-math` on most targets; on AArch64 and RISC-V, ordered reductions that keep the exact result. <https://llvm.org/docs/Vectorizers.html>
[^blis-k]: BLIS project, "Kernels HowTo", the gemm micro-kernel: `C11 := beta * C11 + alpha * A1 * B1`. <https://github.com/flame/blis/blob/master/docs/KernelsHowTo.md>
[^clang-um]: LLVM Project, "Clang Compiler User's Manual", option `-ffp-contract`: `on` fuses within one statement and is the default for languages other than CUDA and HIP. <https://clang.llvm.org/docs/UsersManual.html>
[^langref]: LLVM Project, "LLVM Language Reference Manual", section "Fast-Math Flags", the `contract` flag. <https://llvm.org/docs/LangRef.html#fast-math-flags>
[^smith14]: Tyler M. Smith, Robert van de Geijn, Mikhail Smelyanskiy, Jeff R. Hammond and Field G. Van Zee, "Anatomy of High-Performance Many-Threaded Matrix Multiplication", *IPDPS* 2014, the subsection on parallelizing the fourth loop (indexed by `pc`): threads update the same block of `c`, so they need zeroed copies and a reduction, and only when `c` is small. <https://doi.org/10.1109/IPDPS.2014.110> (free copy: <https://www.cs.utexas.edu/~flame/pubs/blis3_ipdps14.pdf>)
[^blis-mt]: BLIS project, "Multithreading": why the fourth loop is not parallelized. <https://github.com/flame/blis/blob/master/docs/Multithreading.md>
[^hb15]: Torsten Hoefler and Roberto Belli, "Scientific Benchmarking of Parallel Computing Systems: Twelve Ways to Tell the Masses When Reporting Performance Results", *SC* 2015: rule 11 (upper performance bounds, with the roofline as an example). <https://doi.org/10.1145/2807591.2807644> (free copy: <https://htor.inf.ethz.ch/publications/img/hoefler-scientific-benchmarking.pdf>)
[^roofline09]: Samuel Williams, Andrew Waterman and David Patterson, "Roofline: An Insightful Visual Performance Model for Multicore Architectures", *Communications of the ACM* 52(4), 2009. <https://doi.org/10.1145/1498765.1498785> (free copy: <https://people.eecs.berkeley.edu/~kubitron/cs252/handouts/papers/RooflineVyNoYellow.pdf>)
[^boehm]: Simon Boehm, "Fast Multidimensional Matrix Multiplication on CPU from Scratch", August 2022: builds with `-O3 -march=native -ffast-math` on an Intel i7-6700, and the bound of two FMAs per cycle, 32 flops per cycle. <https://siboehm.com/articles/22/Fast-MMM-on-CPU>
[^local]: Owner's machine, Apple M4 Pro, macOS 27: `sysctl hw.perflevel0.l1dcachesize` reports 128 KiB (checked 2026-09-23).
