# P14. Algorithms and schedules

<p class="page-intro">Every rung of the matrix-multiplication ladder computes the same product and differs only in the order, the grouping and the place in which the work runs. This chapter gives that difference a name, the schedule, shows how Halide, TVM and Exo keep it apart from the algorithm, and works out which schedules a Vortex compiler may accept without changing a single printed bit.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 45 minutes · Builds on: [P7. Loop transformations](p7-loop-transformations.md), [P12. Anatomy of a fast GEMM](p12-fast-gemm.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does strip-mining do to a loop, and when is it legal?"

        It splits one loop into an outer loop over strips and an inner loop within a strip. It reorders nothing, so it is always legal, with a remainder when the strip length does not divide the trip count. Tiling is strip-mining followed by moving the strip loops outward.

        Introduced in [P7. Loop transformations](p7-loop-transformations.md#strip-mining-and-tiling).

    ??? question "Which question decides whether a reordering changes a floating-point result?"

        Whether it changes the sequence of roundings that produce one particular output value, or only the order in which independent output values are produced. The second is safe; the first is not.

        Introduced in [P11. Floating point under optimization](p11-floating-point.md#the-rule-every-reordering-pass-needs).

    ??? question "Name the five loops BLIS puts around its micro-kernel, and the two that pack."

        `jc`, `pc`, `ic`, `jr` and `ir`, from the outside in. `pc` packs a `kc` × `nc` slab of `b` into `B̃`, and `ic` packs an `mc` × `kc` block of `a` into `Ã`.

        Introduced in [P12. Anatomy of a fast GEMM](p12-fast-gemm.md#the-five-loops-around-one-micro-kernel).

    ??? question "Why may the kernel's row loop run on several threads while its k loop may not?"

        Each row of `c` is computed start to finish by one thread, in the source order. Splitting k makes two threads add into the same element, which needs a reduction, and combining partial sums regroups the additions.

        Introduced in [P13. Multithreading](p13-multithreading.md#which-loop-in-the-kernel-to-split).

!!! goals "In this chapter"

    - Separate a computation into an algorithm, which fixes every value, and a schedule, which fixes the order, grouping and placement of the work.
    - Apply split, reorder, tile, vectorize, unroll and parallel by hand, and write out the loop nest each one produces, including the leftover iterations of a split.
    - Explain the three-way trade between parallelism, locality and recomputation when a producer feeds a consumer, using Halide's two-stage blur.
    - Compare how Halide, TVM and Exo keep a schedule from changing the answer, and who chooses the schedule in each.
    - Decide whether a given schedule for the stage 10 kernel is acceptable to a Vortex compiler, and say which rule rejects it when it is not.

## One product, five loop nests

Here is the stage 10 kernel, at the size the later chapters use:

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

The function says two different things at once. It says what each element of `c` is: zero, plus `a[row, 0] * b[0, column]`, plus the next product, and so on up to `k = 63`, added in that order. It also says how to get there: rows in the outer loop, columns next, one element finished before the next begins, all on one thread. [P7](p7-loop-transformations.md), [P8](p8-cache-blocking.md), [P12](p12-fast-gemm.md) and [P13](p13-multithreading.md) each changed the second part and kept the first.

The first example makes the split explicit. The arithmetic is written once, as two functions: `init` sets an element to zero, and `update` adds one product into it. Five **schedules**, each nothing but loops around those two calls, then run it in different orders:

--8<-- "includes/examples/optimize/p14-algorithms-and-schedules/one_algorithm.cpp.md"

The first four schedules produce the same 2,304 bit patterns, so the hash of their bits agrees. `k_outer` runs the whole k loop outermost, `tiled` blocks all three loops by 16, and `threads` hands whole rows to four threads; none of them changes the order in which one element receives its 48 products. `split_k` does: it adds the first 24 products into one sum and the last 24 into another, then adds the two. That regroups the additions, and 1,607 of the 2,304 elements come out different. Figure 1 shows the arrangement.

<figure class="vx-figure">
<svg viewBox="0 0 760 370" role="img" aria-label="One algorithm feeding five schedules; four produce the same bit hash, the fifth, which splits the k loop into two partial sums, produces a different one" aria-describedby="p14-f1-desc">
<title id="p14-f1-title">One algorithm, five schedules</title>
<desc id="p14-f1-desc">At the top, a strongly outlined box labelled algorithm holds two lines: c[i][j] = 0, and c[i][j] += a[i][k] * b[k][j] for k = 0, 1, and so on up to N − 1, in order. Five arrows with moving dashes lead down from it to five schedule boxes in a row: rows, with loop order i, j, k; k_outer, with loop order k, i, j; tiled, with i, j and k blocked by 16; threads, with four threads over rows; and split_k, drawn as a bad box, with two partial sums per element. Below each schedule box, a hash box: the first four read ff3a0646 and are accent-coloured; the fifth reads 5b7bfaa4, is drawn as bad, and is labelled 1,607 of 2,304 elements differ. A line of text under the first four says: every element keeps its chain of additions.</desc>
<defs><marker id="p14-f1-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box-strong" x="170" y="16" width="420" height="70" rx="4"/>
<text class="vx-text" x="186" y="40">algorithm</text>
<text class="vx-mono" x="186" y="60">c[i][j] = 0</text>
<text class="vx-mono" x="186" y="78">c[i][j] += a[i][k] * b[k][j], k = 0, 1, …, N−1</text>
<path class="vx-flow" d="M380 86 L80 149" marker-end="url(#p14-f1-head)"/>
<path class="vx-flow" d="M380 86 L230 149" marker-end="url(#p14-f1-head)"/>
<path class="vx-flow" d="M380 86 L380 149" marker-end="url(#p14-f1-head)"/>
<path class="vx-flow" d="M380 86 L530 149" marker-end="url(#p14-f1-head)"/>
<path class="vx-flow" d="M380 86 L680 149" marker-end="url(#p14-f1-head)"/>
<rect class="vx-box" x="10" y="150" width="140" height="56" rx="4"/>
<text class="vx-text" x="80" y="172" text-anchor="middle">rows</text>
<text class="vx-mono" x="80" y="194" text-anchor="middle">i, j, k</text>
<rect class="vx-box" x="160" y="150" width="140" height="56" rx="4"/>
<text class="vx-text" x="230" y="172" text-anchor="middle">k_outer</text>
<text class="vx-mono" x="230" y="194" text-anchor="middle">k, i, j</text>
<rect class="vx-box" x="310" y="150" width="140" height="56" rx="4"/>
<text class="vx-text" x="380" y="172" text-anchor="middle">tiled</text>
<text class="vx-mono" x="380" y="194" text-anchor="middle">16 × 16 × 16</text>
<rect class="vx-box" x="460" y="150" width="140" height="56" rx="4"/>
<text class="vx-text" x="530" y="172" text-anchor="middle">threads</text>
<text class="vx-mono" x="530" y="194" text-anchor="middle">4 × rows</text>
<rect class="vx-box-bad" x="610" y="150" width="140" height="56" rx="4"/>
<text class="vx-text" x="680" y="172" text-anchor="middle">split_k</text>
<text class="vx-mono" x="680" y="194" text-anchor="middle">two half sums</text>
<path class="vx-line" d="M80 206 L80 249" marker-end="url(#p14-f1-head)"/>
<path class="vx-line" d="M230 206 L230 249" marker-end="url(#p14-f1-head)"/>
<path class="vx-line" d="M380 206 L380 249" marker-end="url(#p14-f1-head)"/>
<path class="vx-line" d="M530 206 L530 249" marker-end="url(#p14-f1-head)"/>
<path class="vx-line" d="M680 206 L680 249" marker-end="url(#p14-f1-head)"/>
<rect class="vx-box-accent" x="10" y="250" width="140" height="32" rx="4"/>
<text class="vx-mono" x="80" y="271" text-anchor="middle">ff3a0646</text>
<rect class="vx-box-accent" x="160" y="250" width="140" height="32" rx="4"/>
<text class="vx-mono" x="230" y="271" text-anchor="middle">ff3a0646</text>
<rect class="vx-box-accent" x="310" y="250" width="140" height="32" rx="4"/>
<text class="vx-mono" x="380" y="271" text-anchor="middle">ff3a0646</text>
<rect class="vx-box-accent" x="460" y="250" width="140" height="32" rx="4"/>
<text class="vx-mono" x="530" y="271" text-anchor="middle">ff3a0646</text>
<rect class="vx-box-bad" x="610" y="250" width="140" height="32" rx="4"/>
<text class="vx-mono" x="680" y="271" text-anchor="middle">5b7bfaa4</text>
<text class="vx-text-muted" x="10" y="314">every element keeps its chain of additions: same bits</text>
<text class="vx-text-muted" x="610" y="314">1,607 of 2,304</text>
<text class="vx-text-muted" x="610" y="332">elements differ</text>
</svg>
<figcaption>Figure 1. The first example as a picture. The algorithm is written once; each schedule is a loop order, a blocking or a thread split wrapped around it. The four schedules that leave each element's additions in order produce the same bits, and so the same hash. The fifth regroups them and does not.</figcaption>
</figure>

## Algorithm and schedule

The names come from Halide, a language for image-processing pipelines. Its **algorithm** defines each stage as a **pure** function from integer coordinates to a value, one whose result depends only on its arguments and which has no other effect, written with no loops and no storage: the blur at a point is the sum of three inputs, and nothing more. Its **schedule** answers the questions the algorithm leaves open: when and where each value is computed, where it is stored, and whether a value used twice is kept or recomputed. Ragan-Kelley and his coauthors state that these choices cannot change the meaning or results of the algorithm, only its performance.[^halide13]

The compiler that joins the two makes no heuristic choices of its own. For every question of which loop transformation to apply, it defers to the schedule; it infers the bounds of every loop and allocation itself, so that every allocation covers the region the program uses.[^halide13] The split puts the decisions a C compiler hides inside its optimizer into a separate text that a person, or a search program, can write and change without touching the arithmetic.

That is [principle 2](../philosophy.md#2-say-what-to-calculate-then-decide-how-to-run-it) of Vortex's design in another vocabulary: say what to calculate, then decide how to run it, and let the programmer "guide or replace the compiler's choices" when needed. It is also a way to read the ladder of this book. Interchange, tiling, register blocking and threading are four schedules for one algorithm, and the capstone, [P16](p16-capstone.md), measures them rung by rung.

### Pure definitions and ordered updates

A pure function can be evaluated at its points in any order, any number of times, on any thread, and each point still gets the same value. Most of an image pipeline is like that. A sum, a histogram or a scan is not, so Halide adds **update definitions**: an initial value, then a rule applied over a **reduction domain**, a bounded box of indices visited in a fixed order. The paper states the consequence directly: the meaning of a reduction depends on the order in which its rule is applied, and a schedule may reorder or parallelize the reduction domain's dimensions only if the update is associative.[^halide13]

The first example has exactly that shape. `init` is the initial value, `update` is the rule, and k is the reduction domain. Floating-point addition is not associative, so under Vortex's rules ([decision 56](../decisions/numbers.md#d56)) the k dimension of `update` may be split and blocked but never reordered or divided between threads for one element. Everything in this chapter is built on that one distinction: the pure dimensions `row` and `column` are free, and the reduction dimension `k` is ordered.

??? check "A schedule runs the k loop outermost, as in `k_outer`. The k loop is the reduction dimension. Why does this schedule still keep every bit?"

    Because it moves the k loop without reordering it. Every element of `c` still receives its products for k = 0, 1, 2 and so on, one at a time and in that order, so each element's chain of roundings is unchanged. What changed is how the chains of different elements are interleaved, and no element can tell. Reordering the k loop itself, or splitting one element's chain into partial sums, is what the rule forbids.

## Splitting a loop, and what to do with the leftovers

A schedule is built from a handful of primitives. Halide's paper names the **domain order** of a stage, the order in which the points it must compute are visited, and builds it from four kinds of choice:[^halide13]

- A dimension runs **serially** or in **parallel**.
- A dimension of constant size can be **unrolled** or **vectorized**.
- Dimensions can be **reordered**, for example from column-major to row-major.
- A dimension can be **split** by a factor into an outer and an inner dimension, after which every use of the old index becomes `outer × factor + inner`.

Split is the one the others lean on. Vectorizing by 4 is splitting by 4 and marking the inner loop as vector lanes; unrolling by 4 is splitting by 4 and marking the inner loop to be copied out; **tiling** is splitting two dimensions and reordering so that both outer loops come first.[^halide13] The Halide tutorial also gives names to the combinations, `tile` and `fuse`, the second joining two loops into one.[^halide-l5]

Here is a split worked by hand. A stage computes `f(x, y)` for x from 0 to 7 and y from 0 to 3. The schedule is: split x by 4 into `xo` and `xi`, order the loops as `y`, `xo`, `xi`, vectorize `xi`, and run `y` in parallel. Reading the schedule from the outside in gives the loop nest:

```text
parallel for y in 0..4:
    for xo in 0..2:
        vector for xi in 0..4:      ; four lanes: x = 4*xo + 0 .. 4*xo + 3
            f(4*xo + xi, y)
```

Each point of the 8 × 4 box is visited once, and the order within each row is the same as before the split. The parallel `y` loop is safe because `f` is pure: no two points write the same place.

### When the factor does not divide the extent

Split x by 3 instead, over 7 points, and the last block has only one point to cover. There are three ways to write the leftovers, the loop's **tail**, and the second example runs all three with a body that records each point it evaluates:

--8<-- "includes/examples/optimize/p14-algorithms-and-schedules/split_tails.cpp.md"

- **Guard.** Round the outer loop up to 3 blocks and test every inner iteration against the extent. Each point runs once, at the price of a test inside the innermost loop, which vector code has to carry out lane by lane or with a mask.
- **Shift inward.** Slide the last block left until it ends at the extent, so it covers 4, 5 and 6. Every inner loop runs exactly three times with no test, but points 4 and 5 are evaluated twice. This is what Halide does for a pure function: its tutorial prints the same order, 0 to 5, then 4, 5, 6, and says the repeats are safe because pure functions have no side effects.[^halide-l5]
- **Cut.** Run the full blocks, then a separate loop for what is left. Each point runs once and no test sits in the main loop; the price is a second copy of the body. Exo's `divide_loop` offers `cut`, `guard` and a combination of the two, and has a flag that asserts there is no tail, which it rejects when it cannot verify it.[^exo-loops]

Halide itself refuses to shift inward when the function has an update definition, because evaluating the same point twice would apply the update twice; it rounds the loop up instead.[^halide-l5] Figure 2 compares the three.

<figure class="vx-figure">
<svg viewBox="0 0 760 340" role="img" aria-label="Seven points split by three under three tail strategies: guard skips points 7 and 8 with a test, shift inward evaluates points 4 and 5 twice, and cut runs point 6 in a separate tail loop" aria-describedby="p14-f2-desc">
<title id="p14-f2-title">Three ways to handle the tail of a split</title>
<desc id="p14-f2-desc">Three rows, one per strategy, each drawing the points a loop evaluates as small boxes numbered by x. Row one, guard: three blocks of three, xo = 0 covering 0, 1, 2, xo = 1 covering 3, 4, 5, and xo = 2 covering 6, 7 and 8, where 7 and 8 are drawn as bad boxes labelled skipped by the test. Row two, shift inward: blocks for xo = 0 and xo = 1 as before, and the block for xo = 2 drawn one level lower, slid left to cover 4, 5 and 6; its boxes for 4 and 5 are bad and labelled evaluated twice. Row three, cut: a main loop covering 0 to 5 in two blocks, then point 6 alone in an accent box labelled tail loop. On the right of each row, the number of evaluations: 7, 9 and 7.</desc>
<text class="vx-text" x="16" y="38">guard</text>
<text class="vx-text-muted" x="16" y="56">test in the loop</text>
<text class="vx-text-muted" x="190" y="24">xo = 0</text>
<text class="vx-text-muted" x="322" y="24">xo = 1</text>
<text class="vx-text-muted" x="454" y="24">xo = 2</text>
<rect class="vx-box" x="190" y="32" width="40" height="28" rx="3"/><text class="vx-mono" x="210" y="51" text-anchor="middle">0</text>
<rect class="vx-box" x="234" y="32" width="40" height="28" rx="3"/><text class="vx-mono" x="254" y="51" text-anchor="middle">1</text>
<rect class="vx-box" x="278" y="32" width="40" height="28" rx="3"/><text class="vx-mono" x="298" y="51" text-anchor="middle">2</text>
<rect class="vx-box" x="322" y="32" width="40" height="28" rx="3"/><text class="vx-mono" x="342" y="51" text-anchor="middle">3</text>
<rect class="vx-box" x="366" y="32" width="40" height="28" rx="3"/><text class="vx-mono" x="386" y="51" text-anchor="middle">4</text>
<rect class="vx-box" x="410" y="32" width="40" height="28" rx="3"/><text class="vx-mono" x="430" y="51" text-anchor="middle">5</text>
<rect class="vx-box" x="454" y="32" width="40" height="28" rx="3"/><text class="vx-mono" x="474" y="51" text-anchor="middle">6</text>
<rect class="vx-box-bad" x="498" y="32" width="40" height="28" rx="3"/><text class="vx-text-muted" x="518" y="51" text-anchor="middle">7</text>
<rect class="vx-box-bad" x="542" y="32" width="40" height="28" rx="3"/><text class="vx-text-muted" x="562" y="51" text-anchor="middle">8</text>
<text class="vx-text-muted" x="498" y="80">skipped by the test</text>
<text class="vx-text" x="660" y="51">7 evals</text>
<text class="vx-text" x="16" y="148">shift inward</text>
<text class="vx-text-muted" x="16" y="166">no test, repeats</text>
<text class="vx-text-muted" x="190" y="124">xo = 0</text>
<text class="vx-text-muted" x="322" y="124">xo = 1</text>
<rect class="vx-box" x="190" y="132" width="40" height="28" rx="3"/><text class="vx-mono" x="210" y="151" text-anchor="middle">0</text>
<rect class="vx-box" x="234" y="132" width="40" height="28" rx="3"/><text class="vx-mono" x="254" y="151" text-anchor="middle">1</text>
<rect class="vx-box" x="278" y="132" width="40" height="28" rx="3"/><text class="vx-mono" x="298" y="151" text-anchor="middle">2</text>
<rect class="vx-box" x="322" y="132" width="40" height="28" rx="3"/><text class="vx-mono" x="342" y="151" text-anchor="middle">3</text>
<rect class="vx-box" x="366" y="132" width="40" height="28" rx="3"/><text class="vx-mono" x="386" y="151" text-anchor="middle">4</text>
<rect class="vx-box" x="410" y="132" width="40" height="28" rx="3"/><text class="vx-mono" x="430" y="151" text-anchor="middle">5</text>
<text class="vx-text-muted" x="496" y="190">xo = 2, slid left</text>
<rect class="vx-box-bad" x="366" y="170" width="40" height="28" rx="3"/><text class="vx-mono" x="386" y="189" text-anchor="middle">4</text>
<rect class="vx-box-bad" x="410" y="170" width="40" height="28" rx="3"/><text class="vx-mono" x="430" y="189" text-anchor="middle">5</text>
<rect class="vx-box" x="454" y="170" width="40" height="28" rx="3"/><text class="vx-mono" x="474" y="189" text-anchor="middle">6</text>
<text class="vx-text-muted" x="366" y="216">4 and 5 evaluated twice</text>
<text class="vx-text" x="660" y="170">9 evals</text>
<text class="vx-text" x="16" y="278">cut</text>
<text class="vx-text-muted" x="16" y="296">second loop</text>
<text class="vx-text-muted" x="190" y="254">main loop, xo = 0 and 1</text>
<text class="vx-text-muted" x="454" y="254">tail loop</text>
<rect class="vx-box" x="190" y="262" width="40" height="28" rx="3"/><text class="vx-mono" x="210" y="281" text-anchor="middle">0</text>
<rect class="vx-box" x="234" y="262" width="40" height="28" rx="3"/><text class="vx-mono" x="254" y="281" text-anchor="middle">1</text>
<rect class="vx-box" x="278" y="262" width="40" height="28" rx="3"/><text class="vx-mono" x="298" y="281" text-anchor="middle">2</text>
<rect class="vx-box" x="322" y="262" width="40" height="28" rx="3"/><text class="vx-mono" x="342" y="281" text-anchor="middle">3</text>
<rect class="vx-box" x="366" y="262" width="40" height="28" rx="3"/><text class="vx-mono" x="386" y="281" text-anchor="middle">4</text>
<rect class="vx-box" x="410" y="262" width="40" height="28" rx="3"/><text class="vx-mono" x="430" y="281" text-anchor="middle">5</text>
<rect class="vx-box-accent" x="464" y="262" width="40" height="28" rx="3"/><text class="vx-mono" x="484" y="281" text-anchor="middle">6</text>
<text class="vx-text" x="660" y="281">7 evals</text>
</svg>
<figcaption>Figure 2. The tails of the second example: seven points split by three. A guard keeps one loop and tests every point; shifting the last block inward removes the test but evaluates points 4 and 5 twice; cutting runs the leftover point in a loop of its own. Only the first and last evaluate every point exactly once.</figcaption>
</figure>

??? check "A loop body prints one line per point. A compiler splits the loop by 3 over 7 points to unroll it. Which tail strategies keep the program's output, and why is the third one out?"

    Guard and cut. Both run each point once, in the original order, so the same lines come out in the same order. Shifting inward runs the body for points 4 and 5 twice, so two lines would be printed twice, and the output changes. Recomputing a point is only free when the point has no effect other than its value.

## Where a producer is computed, and where it is kept

The primitives so far reorder one stage. Most programs have several, each consuming what the previous one produced, and the harder choice is how the stages interleave. Halide's paper works it through on a two-stage blur:[^halide13]

```text
blurx(x, y) = in(x - 1, y) + in(x, y) + in(x + 1, y)
out(x, y)   = blurx(x, y - 1) + blurx(x, y) + blurx(x, y + 1)
```

Each point of `out` reads three rows of `blurx`, and each row of `blurx` is read by three rows of `out`. The schedule decides, for `blurx`, at which loop of `out` it is computed and at which loop its results are stored. Halide calls this the stage's **call schedule**, and its tutorial spells the choices `compute_root`, `compute_at`, `store_root` and `store_at`, with full inlining as the default when no choice is given.[^halide13][^halide-l8] The third example implements four points of that space by hand, on a 32 × 32 output with 8 × 8 tiles, and counts what each costs:

--8<-- "includes/examples/optimize/p14-algorithms-and-schedules/blur_schedules.cpp.md"

- **Root** computes all of `blurx` first: 34 rows of 32, one row above and one below the output. No point is computed twice, and every point of both stages is independent, so both loops can run in parallel. But every value of `blurx` is written to a buffer as large as the image and read back only after the whole stage finishes, so on a large image it comes back from memory, not from cache. This is **breadth-first** execution, what composing library calls produces.
- **Inline** computes the three `blurx` values each point of `out` needs, on the spot. Nothing is stored and every point is still independent, but 3,072 evaluations do the work of 1,088: every value of `blurx` is computed three times.
- **Sliding** keeps the last three rows of `blurx` in a ring of 96 values and computes each row once, immediately before the first row of `out` that needs it. It does the minimum work with almost no storage, but row y of `out` now depends on rows computed during earlier iterations, so the row loop must run in order. The Halide tutorial notes that when a parallel loop sits between the storage level and the compute level, Halide stops skipping the rows already computed and stops folding the storage into a ring.[^halide-l8]
- **Tiles** compute, for each 8 × 8 tile of `out`, the 10 × 8 block of `blurx` it needs. The 16 tiles are independent and each one's `blurx` fits in 80 values, but the rows along each horizontal tile edge are computed by both tiles that touch it: 1,280 evaluations instead of 1,088.

Every schedule prints `yes` under "same out": the four compute the same image. They differ in three quantities that the paper names as the tension of the whole space: **parallelism**, the work that can run at once; **locality**, how soon a value is read after it is produced; and **redundant recomputation**, work done more than once.[^halide13] Each extreme gives up one of the three. Root loses locality, inline recomputes, sliding serializes. Figure 3 shows where the tiled schedule's extra work comes from.

<figure class="vx-figure">
<svg viewBox="0 0 760 360" role="img" aria-label="Two vertically adjacent tiles each compute two extra rows of blurx, and the two rows on their shared edge are computed by both; beside it, bar charts of the four schedules' evaluation counts and storage" aria-describedby="p14-f3-desc">
<title id="p14-f3-title">Where the tiled schedule recomputes</title>
<desc id="p14-f3-desc">Left: a vertical strip of 18 thin rows, the rows of blurx numbered from −1 at the top to 16 at the bottom. A bracket labelled tile A spans rows −1 to 8, the ten rows the first 8 × 8 tile of out needs; a second bracket labelled tile B spans rows 7 to 16, the ten rows the second tile needs. Rows 7 and 8, where the brackets overlap, are drawn as bad and pulse, labelled computed by both tiles. Right: two bar charts from the third example. Blurx evaluations: root 1,088, inline 3,072, sliding 1,088, tiles 1,280. Blurx storage in values: root 1,088, inline 0, sliding 96, tiles 80.</desc>
<text class="vx-text" x="16" y="24">blurx rows needed by two tiles</text>
<text class="vx-text-muted" x="96" y="50">−1</text>
<rect class="vx-box" x="120" y="40" width="80" height="14" rx="2"/>
<rect class="vx-box" x="120" y="56" width="80" height="14" rx="2"/>
<rect class="vx-box" x="120" y="72" width="80" height="14" rx="2"/>
<rect class="vx-box" x="120" y="88" width="80" height="14" rx="2"/>
<rect class="vx-box" x="120" y="104" width="80" height="14" rx="2"/>
<rect class="vx-box" x="120" y="120" width="80" height="14" rx="2"/>
<rect class="vx-box" x="120" y="136" width="80" height="14" rx="2"/>
<rect class="vx-box" x="120" y="152" width="80" height="14" rx="2"/>
<rect class="vx-box-bad vx-pulse" x="120" y="168" width="80" height="14" rx="2"/>
<text class="vx-text-muted" x="104" y="179">7</text>
<rect class="vx-box-bad vx-pulse" x="120" y="184" width="80" height="14" rx="2"/>
<text class="vx-text-muted" x="104" y="195">8</text>
<rect class="vx-box" x="120" y="200" width="80" height="14" rx="2"/>
<rect class="vx-box" x="120" y="216" width="80" height="14" rx="2"/>
<rect class="vx-box" x="120" y="232" width="80" height="14" rx="2"/>
<rect class="vx-box" x="120" y="248" width="80" height="14" rx="2"/>
<rect class="vx-box" x="120" y="264" width="80" height="14" rx="2"/>
<rect class="vx-box" x="120" y="280" width="80" height="14" rx="2"/>
<rect class="vx-box" x="120" y="296" width="80" height="14" rx="2"/>
<rect class="vx-box" x="120" y="312" width="80" height="14" rx="2"/>
<text class="vx-text-muted" x="96" y="323">16</text>
<path class="vx-line" d="M210 40 L222 40 L222 198 L210 198"/>
<text class="vx-text-accent" x="230" y="100">tile A</text>
<text class="vx-text-muted" x="230" y="118">rows −1 to 8</text>
<path class="vx-line" d="M210 168 L240 168 L240 326 L210 326"/>
<text class="vx-text-accent" x="248" y="260">tile B</text>
<text class="vx-text-muted" x="248" y="278">rows 7 to 16</text>
<text class="vx-text-muted" x="248" y="180">7 and 8: computed</text>
<text class="vx-text-muted" x="248" y="196">by both tiles</text>
<text class="vx-text" x="420" y="24">blurx evaluations</text>
<text class="vx-text-muted" x="420" y="50">root</text>
<rect class="vx-box-accent" x="490" y="40" width="85" height="14" rx="2"/>
<text class="vx-mono" x="583" y="52">1,088</text>
<text class="vx-text-muted" x="420" y="72">inline</text>
<rect class="vx-box-bad" x="490" y="62" width="240" height="14" rx="2"/>
<text class="vx-mono" x="680" y="92">3,072</text>
<text class="vx-text-muted" x="420" y="110">sliding</text>
<rect class="vx-box-accent" x="490" y="100" width="85" height="14" rx="2"/>
<text class="vx-mono" x="583" y="112">1,088</text>
<text class="vx-text-muted" x="420" y="132">tiles</text>
<rect class="vx-box-accent" x="490" y="122" width="100" height="14" rx="2"/>
<text class="vx-mono" x="598" y="134">1,280</text>
<text class="vx-text" x="420" y="190">blurx storage, values</text>
<text class="vx-text-muted" x="420" y="216">root</text>
<rect class="vx-box-bad" x="490" y="206" width="240" height="14" rx="2"/>
<text class="vx-mono" x="680" y="236">1,088</text>
<text class="vx-text-muted" x="420" y="256">inline</text>
<text class="vx-mono" x="490" y="256">0</text>
<text class="vx-text-muted" x="420" y="278">sliding</text>
<rect class="vx-box-accent" x="490" y="268" width="21" height="14" rx="2"/>
<text class="vx-mono" x="519" y="280">96</text>
<text class="vx-text-muted" x="420" y="300">tiles</text>
<rect class="vx-box-accent" x="490" y="290" width="18" height="14" rx="2"/>
<text class="vx-mono" x="516" y="302">80</text>
<text class="vx-text-muted" x="420" y="334">sliding: rows must run in order</text>
</svg>
<figcaption>Figure 3. Left: each 8 × 8 tile of <code>out</code> needs ten rows of <code>blurx</code>, so two tiles that touch share two rows, and the tiled schedule computes those rows twice. Right: the counts printed by the third example. Only sliding and root avoid recomputation, sliding by giving up parallelism across rows and root by keeping a buffer as large as the image.</figcaption>
</figure>

The best point in this space depends on the machine. The paper reports that on a contemporary x86 machine, the tiled schedule of its blur ran ten times faster than the breadth-first one using the same threads and vector width, because breadth-first was limited by memory bandwidth. A further variant, sliding windows inside independent strips of rows, was 10% faster than tiles on one of the authors' machines and 10% slower on another.[^halide13] The tutorial adds the practical rule: vector units and more cores raise the arithmetic a machine can do per second without raising its memory bandwidth, which tips the balance toward recomputing.[^halide-l8]

??? check "Rerun the third example in your head with 16 × 16 tiles. How many evaluations of `blurx` does the tiled schedule make, and which rows are computed twice?"

    1,152. There are four tiles, each needing 18 rows of 16 values, 288 evaluations each. Only one horizontal tile edge remains, between rows 15 and 16 of `out`, and the rows of `blurx` on either side of it, 15 and 16, are computed by the tile above and the tile below: 2 rows of 32 values, 64 extra evaluations over root's 1,088. Larger tiles recompute less and need more storage per tile.

### A schedule space is large

Halide's paper estimates that its local Laplacian filter pipeline, 99 stages, has a space of schedules far too large to enumerate, and it searches that space with a genetic algorithm: a population of 128 schedules per generation, mutated and crossed, with mutation rules that encode what tends to work for images, such as tiling a stage and computing its producers inside the tile. Tuning took between 2 hours and 2 days per program on the authors' machines.[^halide13]

Mutation and crossover can produce schedules that are invalid, for example computing a stage inside a loop its consumer does not have, and the tuner rejects those; it also compares each candidate's output with a reference, which the authors describe as a sanity check, since every valid schedule should give correct code.[^halide13] [P15](p15-choosing-parameters.md) takes up the search itself.

## Tensor programs: TVM

TVM brings the split to the operators of deep-learning models. Its **tensor expression** language describes each output element by an index formula, for a matrix product a sum over a reduction axis, and leaves the loop structure unspecified. A schedule is then built by applying **schedule primitives** one at a time, each of which the paper describes as preserving the program's logical equivalence.[^tvm18] TVM reuses Halide's loop transformations, placement and thread binding, and adds three kinds of primitive:[^tvm18]

- **Memory scope.** A stage can be marked as living in a named memory, such as GPU shared memory, so that a group of threads fetches a tile cooperatively and the compiler inserts the barriers the sharing needs. [G3](../gpu/g3-memory-hierarchy.md) describes that memory.
- **Tensorization.** A unit of the computation is replaced by a hardware intrinsic whose behavior is declared in the same tensor expression language, for example an 8 × 8 matrix product, together with a rule for lowering it to the instruction. The paper compares the result to high-performance practice: a large operation broken into calls to a micro-kernel, the structure [P12](p12-fast-gemm.md) built by hand.
- **Latency hiding.** For accelerators that separate memory access from computation, the schedule arranges for loads and arithmetic to overlap.

TVM also changes who writes the schedule. A **schedule template** declares **knobs**, such as tile sizes and loop orders, and for the models in the paper's experiments the optimizer had to search billions of configurations. An explorer walks that space by parallel simulated annealing, guided by a cost model trained on the running times of earlier candidates measured on the real device: a gradient-boosted tree model over features of the lowered loop program, trained to rank candidates rather than predict their time.[^tvm18] The programmer writes the algorithm and a template; the machine chooses the schedule.

??? check "A tensorize step replaces the innermost 8 × 8 × 8 block of the stage 10 kernel with one matrix instruction. What must be true of that instruction for a Vortex compiler to accept the step?"

    Its arithmetic must match the algorithm's, rounding included: for each element, the eight products added one at a time, in increasing k, each multiplication and each addition rounded separately. An instruction that fuses a multiply with an add, sums the eight products in a tree, or accumulates in a wider type computes a different chain of roundings, which decision 56 forbids. So the declaration of the instruction's behavior has to state its rounding, not only its mathematical result. [G11](../gpu/g11-matrix-units.md) looks at what real matrix units do.

## Schedules as checked rewrites: Exo

Halide and TVM take the algorithm and the finished schedule together and lower them to loops in one pass; a schedule is a list of directives that the lowering interprets, not a sequence of programs. Exo's design document calls this the **lowering-based** approach and contrasts it with its own **rewrite-based** one.[^exo-design]

In Exo a program is a procedure with ordinary loops, and each scheduling operation is a function from a procedure to a new procedure: `divide_loop` splits a loop, `reorder_loops` swaps two nested loops, `fission` splits a loop body in two.[^exo-loops] Every intermediate state is a procedure, and the design document counts it as an advantage that the state of the scheduling process can be printed and inspected at any point.[^exo-design]

The PLDI paper names the principle **exocompilation**: target-specific code generation and optimization policy move out of the compiler and into code the user writes.[^exo22] Two consequences follow.

- **Hardware lives in libraries.** Custom instructions, special memories and accelerator configuration state are defined in user libraries, not in the compiler.[^exo22] The `replace` operation matches a block of statements against a procedure that describes an instruction and, if the two can be unified, replaces the block with a call to it: TVM's tensorization, reached by rewriting.[^exo-subproc]
- **The compiler checks, the user decides.** Schedules are composable rewrites, and a set of effect analyses guarantees that each preserves the program's meaning and its memory safety.[^exo22] The project's web page puts the division of labor plainly: the performance engineer chooses which optimizations to apply and in what order, and Exo takes responsibility for their being correct.[^exo-site] Its design document lists performance transparency first among its principles: no optimizations that would surprise the user.[^exo-design]

Figure 4 sets the two designs side by side.

<figure class="vx-figure">
<svg viewBox="0 0 760 360" role="img" aria-label="Lowering-based scheduling joins an algorithm and a whole schedule in one lowering step; rewrite-based scheduling applies one checked rewrite at a time, and every intermediate program can be printed" aria-describedby="p14-f4-desc">
<title id="p14-f4-title">Lowering a schedule, or rewriting a program</title>
<desc id="p14-f4-desc">Left panel, lowering-based, as in Halide and TVM: an algorithm box and a schedule box, side by side, each with an arrow into one lowering box, whose arrow leads to a single loop nest box at the bottom. Nothing in between is shown. Right panel, rewrite-based, as in Exo: a vertical chain of four program boxes. The first holds naive loops. An arrow with moving dashes labelled divide_loop, checked, leads to the second, split loops; an arrow labelled reorder_loops, checked, leads to the third, tiled loops; an arrow labelled replace, checked, leads to the fourth, a call to an instruction. A note says that every box is a program.</desc>
<defs><marker id="p14-f4-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="24">Lowering-based</text>
<text class="vx-text-muted" x="20" y="42">Halide, TVM</text>
<rect class="vx-box" x="20" y="64" width="150" height="40" rx="4"/>
<text class="vx-text" x="95" y="89" text-anchor="middle">algorithm</text>
<rect class="vx-box" x="200" y="64" width="150" height="40" rx="4"/>
<text class="vx-text" x="275" y="89" text-anchor="middle">whole schedule</text>
<path class="vx-line" d="M95 104 L165 159" marker-end="url(#p14-f4-head)"/>
<path class="vx-line" d="M275 104 L205 159" marker-end="url(#p14-f4-head)"/>
<rect class="vx-box-strong" x="110" y="160" width="150" height="40" rx="4"/>
<text class="vx-text" x="185" y="185" text-anchor="middle">lowering</text>
<path class="vx-line" d="M185 200 L185 259" marker-end="url(#p14-f4-head)"/>
<rect class="vx-box-accent" x="110" y="260" width="150" height="40" rx="4"/>
<text class="vx-text" x="185" y="285" text-anchor="middle">loop nest</text>
<text class="vx-text-muted" x="20" y="334">one step; no program in between</text>
<text class="vx-text" x="420" y="24">Rewrite-based</text>
<text class="vx-text-muted" x="420" y="42">Exo</text>
<rect class="vx-box" x="420" y="56" width="170" height="36" rx="4"/>
<text class="vx-mono" x="505" y="79" text-anchor="middle">naive loops</text>
<path class="vx-flow" d="M505 92 L505 135" marker-end="url(#p14-f4-head)"/>
<text class="vx-mono" x="514" y="114">divide_loop</text>
<text class="vx-text-accent" x="640" y="114">checked</text>
<rect class="vx-box" x="420" y="136" width="170" height="36" rx="4"/>
<text class="vx-mono" x="505" y="159" text-anchor="middle">split loops</text>
<path class="vx-flow" d="M505 172 L505 215" marker-end="url(#p14-f4-head)"/>
<text class="vx-mono" x="514" y="194">reorder_loops</text>
<text class="vx-text-accent" x="640" y="194">checked</text>
<rect class="vx-box" x="420" y="216" width="170" height="36" rx="4"/>
<text class="vx-mono" x="505" y="239" text-anchor="middle">tiled loops</text>
<path class="vx-flow" d="M505 252 L505 295" marker-end="url(#p14-f4-head)"/>
<text class="vx-mono" x="514" y="274">replace</text>
<text class="vx-text-accent" x="640" y="274">checked</text>
<rect class="vx-box-accent" x="420" y="296" width="170" height="36" rx="4"/>
<text class="vx-mono" x="505" y="319" text-anchor="middle">instruction call</text>
<text class="vx-text-muted" x="604" y="346">every box: a program</text>
</svg>
<figcaption>Figure 4. Two ways to apply a schedule. A lowering-based compiler reads the algorithm and the whole schedule together and produces loops in one step. A rewrite-based one applies one operation at a time, checks it, and yields a complete program after each, so a schedule can be inspected, timed or debugged step by step.</figcaption>
</figure>

For Vortex the rewrite view has a practical merit beyond checking. [O1](o1-optimizer-contract.md#remarks-the-optimizers-report) asked every transformation to report what it did and why; a schedule applied step by step can report, for each step, the program before and after and the rule that allowed it, which is [principle 6](../philosophy.md#6-explain-performance-decisions) applied to a schedule. MLIR's transform dialect, in [M9](../mlir/m9-transform-dialect.md), writes the steps as IR of their own.

## What a schedule may not change in Vortex

The three systems agree that a schedule must not change the answer, and each defines "the answer" for its own domain. Halide's pure functions may be evaluated any number of times in any order; TVM and Exo check equivalence of the computation. A Vortex program has a stricter answer, the contract of [O1](o1-optimizer-contract.md#vortexs-list): every byte printed, in order; whether and where the program stops with a runtime error; the exit status; and every bit of every floating-point result. A Vortex schedule is acceptable only if it keeps all four, which gives four rules.

1. **Dependences.** A step that reorders loops must pass the dependence test of [P6](p6-dependence-analysis.md) and [P7](p7-loop-transformations.md#when-a-swap-is-legal), as any loop transformation must.
2. **Reduction order.** No step may reorder, regroup or divide among threads the updates to one floating-point accumulator. Splitting and blocking the reduction loop are allowed when its blocks still run in increasing order; `split_k`, vectorizing the k loop and splitting it across threads are not. This is decision 56 in schedule form, and it is the rule [P11](p11-floating-point.md) and [P13](p13-multithreading.md) already applied to single transformations. Until Vortex offers an explicit opt-in to reassociation, no schedule can buy that speed.
3. **Evaluate once.** Recomputation is a schedule choice in Halide; in Vortex it is allowed only for work that cannot fail and does not print. An expression with a check that might fail may not be inlined into several consumers, computed in overlapping tiles, or covered by a shifted-inward tail, because evaluating it again can report an error the original never reached, or report it at a different point. Pure integer and floating-point arithmetic may be recomputed freely: the same operation on the same operands gives the same bits.
4. **Failures and output stay in order.** Running a loop in parallel, or reordering it, changes which of two failing checks runs first. A schedule may do either only for a loop that contains no `print` and whose checks are proven never to fail ([O8](o8-loops.md) supplies the proofs for the kernel's constant bounds).

Rules 1 and 2 are about values; rules 3 and 4 are about effects, and they are what distinguish a Vortex schedule from a Halide one. They also decide who may write a schedule. A schedule chosen by the compiler, by a search as in [P15](p15-choosing-parameters.md), or by the programmer through an override channel must all pass the same checks, because principle 3 says a faster program must not quietly change its answer, whoever asked for the speed.

## Your turn: scheduling the stage 10 kernel

Take the kernel from the start of the chapter. Each row below is one scheduling step applied to it on its own. Decide whether a Vortex compiler may accept the step, and which of the four rules decides. Three rows are filled in.

| Step | Accepted? | Rule that decides |
| --- | --- | --- |
| Reorder `row` and `column` | yes | 1: no dependence between elements; each chain unchanged |
| Run `k` on four threads, one quarter each | no | 2: four partial sums per element |
| Tile `row` and `column` by 8 × 8 | yes | 1: each tile holds whole elements, each chain unchanged; 64 is a multiple of 8, so no tail |
| Split `k` by 16 into `ko` and `ki`, `ko` outside, one running `sum` | ? | ? |
| Vectorize `column` by 4 | ? | ? |
| Vectorize `k` by 4 | ? | ? |
| Unroll `k` by 4, keeping one `sum` | ? | ? |
| Move `ko` outside `row` and `column`, accumulating in `c[row, column]` | ? | ? |
| Split `row` by 3 with a shifted-inward tail | ? | ? |
| Run `row` on four threads in a copy of the kernel whose inner loop calls `print` | ? | ? |

??? check "Answers for the kernel"

    - **Split `k` by 16, one running sum:** accepted. Splitting reorders nothing, and 64 is a multiple of 16, so no tail is needed. Every element still receives its 64 products in increasing k.
    - **Vectorize `column` by 4:** accepted. The four lanes compute four different elements of `c`, each with its own unchanged chain; this is the vectorization [P10](p10-vectorization.md) aims at.
    - **Vectorize `k` by 4:** rejected by rule 2. Four lanes would hold four partial sums of one element, added together at the end, the same regrouping as `split_k`.
    - **Unroll `k` by 4, one `sum`:** accepted. Unrolling copies the body four times, but the copies still add into the same `sum` one after another. Unrolling with four separate sums would be rejected, as [P7](p7-loop-transformations.md#putting-them-in-order) notes.
    - **Move `ko` outermost, accumulating in `c`:** accepted, with a preparation. The running `sum` must first return to its element and its zeroing be split off, the step P7 builds; then each element's chain is 0.0 plus the products in increasing k, as before, because the `ko` blocks still run in increasing order. The loads of `a` and `b` cannot see the stores to `c`, since `c` is a `&mut` parameter. This is the `pc` loop of [P12](p12-fast-gemm.md).
    - **Split `row` by 3, shifted inward:** 64 rows make 22 blocks, and the last one slides back to cover rows 61 to 63, so rows 61 and 62 run twice. Accepted only under two conditions. The checks on `a[row, k]` and `b[k, column]` must be proven never to fail ([O8](o8-loops.md)); otherwise rule 3 rejects it. And the body must assign `c[row, column] = sum`, so that a repeated row stores the same values again. In the version that accumulates into `c`, the previous row of this table, a repeated row would add its products a second time: the reason Halide refuses to shift inward for an update definition.
    - **Threads over `row` with a `print` in the loop:** rejected by rule 4. Four threads would interleave the printed lines in an order that depends on timing, and the bytes a program prints, in order, are part of its observable behavior.

## Measuring the schedules

The first example proves that its first four schedules agree bit for bit; it says nothing about their speed, and no number is given here. Measure it yourself, following [P1](p1-measure-first.md):

1. Copy the five schedules into a timing harness of your own, outside the examples folder, with a larger N, such as 512, and build with `-O2 -ffp-contract=off` so that each operation rounds once, as in Vortex.
2. Before timing anything, check that the four valid schedules produce identical bits, as the example does.
3. Time many separate runs of each schedule and report the median with a confidence interval.
4. Compute GFLOP/s as $2N^3 / t$, with $t$ in nanoseconds.
5. Repeat at a second N whose three matrices do not fit in the L2 cache ([P2](p2-memory-hierarchy.md)).

| Schedule | N | Median time | 95% interval | GFLOP/s | Bits equal to `rows`? |
| --- | --- | --- | --- | --- | --- |
| rows | | | | | yes |
| k_outer | | | | | yes |
| tiled, 16 × 16 × 16 | | | | | yes |
| threads, 4 | | | | | yes |
| split_k | | | | | no |

Record the machine, compiler version, flags and date with the table. The last row is there to show what the forbidden schedule would buy; a Vortex compiler may not use it.

## For Vortex

!!! vortex "Exercise"

    **Build** schedules as an input to your compiler: a way to apply a list of scheduling steps to one function's loop nest, check each step against this chapter's four rules, and report what happened. The schedule comes from a command-line option or a side file, not from Vortex syntax: a schedule written inside the language would be a new language feature, which v0.1 does not have ([decision 50](../decisions/documentation.md#d50)).

    1. **Names for loops.** Give every loop in a function a name a schedule can use, built from its induction variable and its source position, and print the nest with those names (extend the loop-nest view from [P7](p7-loop-transformations.md#for-vortex)).
    2. **Steps.** Split with a guard tail or a cut tail, reorder two loops, unroll, and parallel, each applied as a rewrite that produces a complete, printable loop nest. Parallel reuses the pass and runtime from [P13](p13-multithreading.md#for-vortex). A shifted-inward tail is allowed only when rule 3 holds.
    3. **Checks.** Reject a step that breaks a dependence, that reorders, regroups or divides the updates to one floating-point accumulator, that would evaluate a check that may fail or a `print` more than once, or that reorders or parallelizes a loop containing a `print` or a check that may fail. Rejection stops the schedule at that step.
    4. **Remarks**, in the stream from [O1](o1-optimizer-contract.md#remarks-the-optimizers-report): a passed remark for each accepted step, naming the step and the loops; a missed remark for each rejected step, naming the rule and the source position of the accumulator, check or `print` that caused it.

    **Not yet:** schedule syntax in the language; choosing a schedule automatically, by model or search ([P15](p15-choosing-parameters.md)); computing one function inside another's loops, Halide's `compute_at`, which needs inlining first ([O7](o7-inlining-and-sroa.md)); tensorization and vectorization steps ([P10](p10-vectorization.md)); GPU mappings ([M12](../mlir/m12-vortex-gpu-path.md)); any opt-in that relaxes decision 56.

    **Proof that it works:**

    - For the stage 10 kernel, every accepted schedule from the Your turn table, and at least four more of your own, produces standard output byte-identical to the unscheduled build, including a printed checksum of every element's bits.
    - Golden rejections, each with its remark checked against a file: vectorize `k` (or its equivalent split with separate sums), `k` on several threads, a shifted-inward tail around a division whose divisor may be zero, and threads over a loop that prints.
    - A program with an out-of-bounds index inside a scheduled loop reports the same error line at the same position with the schedule on and off.
    - A measurement, following [P1](p1-measure-first.md), of the naive kernel and three accepted schedules, with the machine, the compiler version and the date:

    | Schedule | Median time | 95% interval | Output identical? | Passed remarks | Missed remarks |
    | --- | --- | --- | --- | --- | --- |
    | none | | | | | |
    | | | | | | |
    | | | | | | |
    | | | | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **What is the difference between an algorithm and a schedule?** The algorithm fixes every value the program computes; the schedule fixes the order, grouping, placement and storage of the work, and must not change the values.
    - **Which part of an algorithm constrains a schedule most?** Its update definitions: a reduction's order is part of its meaning, so its dimension may be split but not reordered or parallelized unless the update is associative, which floating-point addition is not.
    - **What are the three ways to handle a split's leftover iterations?** A guard test in the loop, shifting the last block inward, which evaluates some points twice, or a separate tail loop.
    - **What does a producer's schedule trade?** Parallelism, locality and recomputation: breadth-first loses locality, full inlining recomputes, a sliding window serializes, and tiles recompute a little along their edges.
    - **What did TVM add to Halide's split?** Primitives for memory scopes, tensorization and latency hiding, and a search over schedule templates guided by a cost model trained on measurements.
    - **How does Exo differ from both?** Each scheduling step is a checked rewrite from one complete program to another, and hardware instructions and memories are defined in user libraries, outside the compiler.
    - **What must a Vortex schedule keep that a Halide one need not?** The order of every floating-point accumulation, and the effects: no check or `print` evaluated twice or out of order.

## Where this comes back

!!! next "You will use this again in"

    - [P15. Choosing parameters: models or search](p15-choosing-parameters.md): *schedule knobs*, *searching a schedule space*, *what a tuner may not do*
    - [P16. Capstone: the ladder, measured](p16-capstone.md): *each rung as a schedule of one algorithm*
    - [M9. Schedules as IR: the transform dialect](../mlir/m9-transform-dialect.md): *schedules as programs*, *naming the loop to transform*
    - [M11. End-to-end ML compilers](../mlir/m11-ml-compilers.md): *a searched schedule against a fixed one*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *a schedule as the programmer's override*
    - [G11. Matrix units](../gpu/g11-matrix-units.md): *tensorization*, *an instruction's exact arithmetic*
    - [G12. Fusion case study: FlashAttention](../gpu/g12-flashattention.md): *recomputation traded for memory traffic*
    - [G13. Tile languages](../gpu/g13-tile-languages.md): *who chooses the schedule*

## Sources and further reading

Read sections 3 and 4 of the Halide paper first: the two-stage blur, the model of domain order and call schedule, and how lowering turns them into loops. Then work through Halide's tutorial lessons 5 and 8, which print the loop nest of every schedule they show. For search and tensorization, read sections 4 and 5 of the TVM paper. Exo's design document is two pages and states the rewrite-based alternative directly.

[^halide13]: Jonathan Ragan-Kelley, Connelly Barnes, Andrew Adams, Sylvain Paris, Frédo Durand and Saman Amarasinghe, "Halide: A Language and Compiler for Optimizing Parallelism, Locality, and Recomputation in Image Processing Pipelines", *Proceedings of the 34th ACM SIGPLAN Conference on Programming Language Design and Implementation (PLDI)*, 2013, pages 519 to 530: the abstract and sections 1.1, 1.2, 2, 3, 3.1, 3.2, 4 (introduction), 5 and 6.1. <https://doi.org/10.1145/2491956.2462176> (authors' copy: <https://people.csail.mit.edu/jrk/halide-pldi13.pdf>)
[^halide-l5]: Halide Project, "Tutorial lesson 5: Vectorize, parallelize, unroll and tile your code", the sections on `split`, `fuse` and `tile`, and on splitting by a factor that does not divide the extent. <https://halide-lang.org/docs/tutorial/lesson_05_scheduling_1.html>
[^halide-l8]: Halide Project, "Tutorial lesson 8: Scheduling multi-stage pipelines", the default inline schedule, `compute_root`, `compute_at`, `store_root`, and the comments on trade-offs and on parallel loops between the storage and compute levels. <https://halide-lang.org/docs/tutorial/lesson_08_scheduling_2.html>
[^tvm18]: Tianqi Chen, Thierry Moreau, Ziheng Jiang, Lianmin Zheng, Eddie Yan, Haichen Shen, Meghan Cowan, Leyuan Wang, Yuwei Hu, Luis Ceze, Carlos Guestrin and Arvind Krishnamurthy, "TVM: An Automated End-to-End Optimizing Compiler for Deep Learning", *13th USENIX Symposium on Operating Systems Design and Implementation (OSDI)*, 2018, pages 578 to 594: sections 4.1 to 4.4 and 5.1 to 5.4, and Figure 6. <https://www.usenix.org/conference/osdi18/presentation/chen>
[^exo22]: Yuka Ikarashi, Gilbert Louis Bernstein, Alex Reinking, Hasan Genc and Jonathan Ragan-Kelley, "Exocompilation for Productive Programming of Hardware Accelerators", *Proceedings of the 43rd ACM SIGPLAN International Conference on Programming Language Design and Implementation (PLDI)*, 2022, pages 703 to 718: the abstract. <https://doi.org/10.1145/3519939.3523446> (abstract also at <https://pldi22.sigplan.org/details/pldi-2022-pldi/21/Exocompilation-for-Productive-Programming-of-Hardware-Accelerators>)
[^exo-design]: Exo Project, "Design Document for Exo", sections on design principles, exocompilation and the rewrite-based scheduling language. <https://github.com/exo-lang/exo/blob/main/docs/Design.md>
[^exo-loops]: Exo Project, "Loop and scope related primitives": `divide_loop`, `reorder_loops` and `fission`. <https://github.com/exo-lang/exo/blob/main/docs/primitives/loop_ops.md>
[^exo-subproc]: Exo Project, "Subprocedure operations": `replace`. <https://github.com/exo-lang/exo/blob/main/docs/primitives/subproc_ops.md>
[^exo-site]: Exo Project, "The Exo Language", sections "Background & Motivation" and "Exocompilation". <https://exo-lang.dev/>
