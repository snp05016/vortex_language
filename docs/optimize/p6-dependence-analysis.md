# P6. Dependence analysis

<p class="page-intro">Before a compiler swaps, splits or reorders a loop's iterations, it has to know which of them depend on which. This chapter builds that knowledge: flow, anti and output dependence, distance and direction vectors, and the tests that compute them from a subscript.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 35 minutes · Builds on: [O8. Loops: structure, induction variables and bounds checks](o8-loops.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is an add recurrence, and what did O8 write for the address of a[row, k] in the kernel's k loop?"

        {start,+,step}&lt;L&gt;: a value that starts at `start` in loop L and grows by `step` in every later iteration. O8 wrote the address of `a[row, k]` as {A + 256·row,+,4}&lt;k&gt;.

        Introduced in [O8. Loops: structure, induction variables and bounds checks](o8-loops.md#add-recurrences).

    ??? question "What is a loop's backedge-taken count, and why do compilers prefer it to the trip count?"

        The number of times a back edge is taken. Compilers prefer it to the trip count, one more, because a top-tested loop's header still runs once even when its body never does, and because the trip count can overflow the counter's type where the backedge-taken count still fits.

        Introduced in [O8. Loops: structure, induction variables and bounds checks](o8-loops.md#counting-iterations).

    ??? question "In O2's terms, what makes one loop nested inside another?"

        The inner loop's header is reachable only through the outer loop's header, which dominates it, and every block the inner loop's back edge can reach without leaving the outer loop lies inside the outer loop's own natural loop.

        Introduced in [O2. Control-flow graphs and dominance](o2-cfg-and-dominance.md#loops-from-dominance).

    ??? question "Why did O8 say a Vortex program's runtime checks fence in any transformation that reorders loop iterations?"

        A check can fail, and a failure is an observable event whose order the specification fixes. A transformation that reorders iterations can change which check fails first, or run one that the original order would never have reached, so it needs a proof that no check's outcome, and no other observable order, changes.

        Introduced in [O8. Loops: structure, induction variables and bounds checks](o8-loops.md#why-the-checks-matter-for-the-vectorizer).

!!! goals "In this chapter"

    - Explain why a compiler must prove that reordering a loop's iterations does not change which value a memory operation reads or writes.
    - Classify a dependence between two array accesses as flow, anti or output, and tell a loop-carried dependence from a loop-independent one.
    - Compute a distance vector and a direction vector for a dependence in an affine loop nest, and use them to decide whether a loop permutation is legal.
    - Apply the ZIV, SIV and MIV tests, including the GCD test, to decide whether two subscripts can ever address the same element.
    - Explain why Vortex's fixed, multi-dimensional array shapes remove a step that a compiler working from linearized subscripts still has to do.

## Why a compiler cannot just swap two loops

Here is a loop that turns a running count into a sequence. `a[0]` starts at 0, and for `i` from 1 to `n - 1`:

```text
a[i] = a[i - 1] + 1
```

Run it for `n` = 5 and the array holds 0, 1, 2, 3, 4: each element is one more than the element before it.

Now suppose a compiler tried to run this loop's iterations out of order, the way a vectorized or parallel version might: read every right-hand side first, using whatever `a` held before the loop touched it, then write every result. `a[1]` still comes out right, 0 + 1 = 1, because `a[0]` never changes. But `a[2]` reads the old `a[1]`, not the 1 that iteration 1 just produced, and gets 0 + 1 = 1 instead of 2. The same mistake repeats for every later element, and the array ends up 1, 1, 1, 1 instead of 1, 2, 3, 4.

The loop's iterations are not independent: iteration `i` needs the value iteration `i - 1` produces. A **dependence** is exactly this: two memory operations in the program, at least one of them a write, that touch the same location, where changing their relative order changes what the program computes. Before a compiler reorders a loop's iterations, however it plans to do so, interchanging two loops, splitting one into tiles, running iterations on several threads or several vector lanes at once, it has to know which pairs of operations in the loop are dependences, because those are exactly the pairs whose order it may not change. Everything else is free to move.

O8 built the tools a compiler uses to reason about a single loop: its shape, its induction variables, how many times it runs. This chapter adds the piece those tools were missing, a way to reason about what a loop's body does to memory across iterations. [O9](o9-alias-analysis.md) extends the same question from array subscripts to arbitrary pointers, and [P7](p7-loop-transformations.md) is where "legal" turns into an actual transformation.

## Naming what changes: flow, anti and output dependence

The example above reads a location that an earlier iteration wrote: iteration `i` reads `a[i - 1]`, which iteration `i - 1` wrote. That is a **flow dependence** (also called a true dependence, or read-after-write): a later access reads what an earlier one wrote.

A dependence can run the other way. Consider a loop that shifts an array's elements one place to the left, in place:

```text
for i in 0..n - 1 {
    a[i] = a[i + 1]
}
```

Iteration `i` reads `a[i + 1]`, and iteration `i + 1` (later) writes `a[i + 1]`. If a compiler ran iteration `i + 1` before iteration `i`, the write would land before the read, and the read would pick up the new, overwritten value instead of the old one the shift is supposed to move. This is an **anti-dependence** (write-after-read): a later access writes what an earlier one read.

A third kind involves no read at all. Consider two statements, in the same loop, that both write the same location:

```text
a[i % 2] = f(i)
a[i % 2] = g(i)
```

Every iteration writes `a[0]` or `a[1]` twice, once from each statement. Nothing reads the first value before the second overwrites it, so no value flows between the two writes, yet their order still decides the array's final contents: reversing them changes which of `f(i)` and `g(i)` survives. This is an **output dependence** (write-after-write).

These three names classify every dependence a compiler needs to track. They matter for different reasons: a flow dependence carries a value forward, so getting its order wrong computes the wrong answer, exactly as in the running-count example. An anti or output dependence carries no value, only a location; getting its order wrong overwrites something before or after it should be overwritten. All three still have to be preserved for a transformation to be correct.

??? check "Is the array shift's dependence loop-carried in both directions, flow and anti?"

    No. `a[i] = a[i + 1]` only reads `a[i + 1]` and only writes `a[i]`; a location written at iteration `i` (namely `a[i]`) is never read again by a later iteration, because every later iteration reads only `a[i' + 1]` for `i' > i`, which is strictly ahead of anything already written. So the shift has exactly one family of dependences, the anti-dependences from each read to the write, one iteration later, that will overwrite what it read.

## Loop-carried and loop-independent dependence

A dependence between two accesses in the *same* iteration, such as a value computed by one statement and used by the next, is **loop-independent**: reordering the loop's iterations relative to each other cannot affect it, because it never crosses an iteration boundary. A dependence between accesses in *different* iterations, such as every example above, is **loop-carried**: it is exactly the kind that a loop transformation can break. This chapter is about loop-carried dependences; the same three kinds, flow, anti and output, describe both, but only a loop-carried one constrains how the compiler may run the loop.

## The iteration space and its dependences

A loop nest `d` levels deep, with each level a `for` loop over a range, visits a set of index tuples $(i_1, \dots, i_d)$, one per combination of the loops' values. That set is the nest's **iteration space**. For loops over constant, non-negative-step ranges, as every `for` loop in this chapter is, the space is a `d`-dimensional lattice of points, one per iteration.

Sequential execution visits the iteration space in **lexicographic order**: the outermost loop's value changes slowest, and $(i_1, \dots, i_d)$ runs before $(i_1', \dots, i_d')$ exactly when, reading the two tuples left to right, the first coordinate where they differ is smaller in the first tuple. O8 wrote an address a loop computes as an add recurrence in a single induction variable; a subscript in a `d`-deep nest is the same idea generalized to several. Writing $\vec i$ for an iteration's index tuple, an **affine subscript** reads or writes the array element

$$
F(\vec i) = a_0 + a_1 i_1 + a_2 i_2 + \cdots + a_d i_d
$$

for integer constants $a_0, \dots, a_d$. Every subscript in this chapter, and every subscript O8's add recurrences described, has this shape.

Two iterations $\vec i$ and $\vec j$, with $\vec i$ running first, carry a dependence exactly when some access at $\vec i$ and some access at $\vec j$ touch the same array element under this rule, and at least one of the two is a write. Finding every dependence a loop nest carries means checking every pair of accesses to the same array for exactly this condition.

## Distance and direction vectors

Once a dependence is found between an earlier iteration $\vec i$ and a later one $\vec j$, the **distance vector** is $\vec j - \vec i$, computed one loop level at a time. The **direction vector** keeps only each component's sign: `<` for a positive distance, `=` for zero, `>` for negative. Because $\vec i$ was chosen to run first, the pair's vector, read left to right, always has its first nonzero component positive; a vector with that property is **lexicographically positive**. A negative component later in the vector is allowed and common; only the first nonzero one is pinned down by the choice of which iteration runs first.

The first example's C++ program computes these vectors by brute force for two small nests. The first models one row of a matrix-vector product, an accumulation that stands in for one `(row, column)` pair of a matmul: `c[row] += a[row][k] * b[k]`, over a `row` loop and a `k` loop. `a` and `b` are read-only, and each iteration touches a distinct element of each, so they carry no dependence at all. `c[row]` is read and written by every iteration of the `k` loop, at the same address for a fixed `row`: every pair of iterations in that row's `k` loop shares an output, a flow and an anti-dependence, all with the same distance, `(0, k₂ − k₁)`. The second nest is a **skewed stencil**, `a[i][j] = a[i - 1][j + 1] + 1`, whose single flow dependence has distance `(1, −1)`: an iteration one row down and one column back wrote the value this iteration reads.

--8<-- "includes/examples/optimize/p6-dependence-analysis/brute_force_dependence.cpp.md"

Read the output row by row. Every pair of the matvec's `k`-loop iterations produces one instance of each kind, always with a positive second component; every instance of the stencil's dependence has distance `(1, −1)`, first component positive, second negative. Both are lexicographically positive, as they must be: the program only ever compares an iteration to a later one.

??? check "For the running-count loop, a[i] = a[i - 1] + 1, what is its distance vector, and what does it forbid?"

    The write to `a[i]` and the later read of it, at iteration `i + 1`, give a flow dependence with distance `(1)`, a one-level nest's only component. It forbids exactly what the opening example did wrong: computing all the reads before any of the writes, which uses the old value of `a[i - 1]` at every iteration but the first. A transformation is legal here only if it still produces, for each `i`, the same value that a sequential run would have written to `a[i - 1]` before reading it.

## When a loop permutation is legal

Interchanging a nest's loop levels, tiling it, or running several of its iterations at once are all instances of the same question: does the new order still respect every dependence the nest carries? For a permutation of the loop levels, the answer has a clean form.[^bgs94] Take each dependence's distance vector in the nest's *original* nesting order, then reorder its components to match the *new* order the loop levels would run in. The permutation is legal exactly when every reordered vector is still lexicographically positive. A vector that comes out with its first nonzero component negative describes a dependence that the new order would run backward: the operation that is supposed to happen first would now happen after the one that depends on it.

The matvec nest's dependence, distance `(0, 1)` in `(row, k)` order, has only one nonzero component. Wherever `k` lands after a permutation, positive or not, that component is still positive, because reordering the components of a vector with one nonzero entry can only move it, never flip its sign. So every permutation of `row` and `k` is legal, including the interchange to `(k, row)` that the brute-force program checked. This is the fact the research behind this book calls Vortex's matmul advantage: because `a` and `b` carry no dependence, and `c`'s only dependence has a single positive component, the whole loop nest, not only one pair of levels, can be reordered however cache locality prefers, and [P7](p7-loop-transformations.md) and [P8](p8-cache-blocking.md) build on exactly that freedom.

The stencil's dependence does not have that shape. Its distance is `(1, −1)` in the original `(i, j)` order. Swapping the loop levels reorders the vector's components to `(−1, 1)`: the first component is now negative, so the interchange is illegal. Figure 1 draws why, over the small grid the brute-force program checked.

<figure class="vx-figure">
<div class="vx-stepper">
<div class="vx-step">
<p><strong>Step 1. Original nest: i outer, j inner.</strong> Numbers show the order iterations run in, row by row. Every dependence arrow points from a smaller number to a larger one: each one runs forward, so the nest is correct as written.</p>
<svg viewBox="0 0 700 330" role="img" aria-label="A 3 by 3 grid of iterations, numbered 0 to 8 in row-major order: row i=0 holds 0, 1, 2; row i=1 holds 3, 4, 5; row i=2 holds 6, 7, 8. Four arrows show the stencil's flow dependence, each from an iteration to the one one row down and one column to the left: from 1 to 3, from 2 to 4, from 4 to 6, and from 5 to 7. Every arrow points from a smaller number to a larger one, so every dependence runs forward and the nest is legal as written.">
<defs>
<marker id="p6-f1a-head" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z" style="fill: var(--vortex-accent)"/></marker>
</defs>
<text class="vx-text-muted" x="140" y="30" text-anchor="middle">j = 0</text>
<text class="vx-text-muted" x="340" y="30" text-anchor="middle">j = 1</text>
<text class="vx-text-muted" x="540" y="30" text-anchor="middle">j = 2</text>
<text class="vx-text-muted" x="40" y="75" text-anchor="middle">i = 0</text>
<text class="vx-text-muted" x="40" y="170" text-anchor="middle">i = 1</text>
<text class="vx-text-muted" x="40" y="265" text-anchor="middle">i = 2</text>
<path class="vx-flow" d="M328 82 L164 152" marker-end="url(#p6-f1a-head)" style="stroke: var(--vortex-accent)"/>
<path class="vx-flow" d="M528 82 L364 152" marker-end="url(#p6-f1a-head)" style="stroke: var(--vortex-accent)"/>
<path class="vx-flow" d="M328 177 L164 247" marker-end="url(#p6-f1a-head)" style="stroke: var(--vortex-accent)"/>
<path class="vx-flow" d="M528 177 L364 247" marker-end="url(#p6-f1a-head)" style="stroke: var(--vortex-accent)"/>
<circle class="vx-dot" cx="140" cy="70" r="8"/><text class="vx-text" x="140" y="52" text-anchor="middle">0</text>
<circle class="vx-dot" cx="340" cy="70" r="8"/><text class="vx-text" x="340" y="52" text-anchor="middle">1</text>
<circle class="vx-dot" cx="540" cy="70" r="8"/><text class="vx-text" x="540" y="52" text-anchor="middle">2</text>
<circle class="vx-dot" cx="140" cy="165" r="8"/><text class="vx-text" x="140" y="147" text-anchor="middle">3</text>
<circle class="vx-dot" cx="340" cy="165" r="8"/><text class="vx-text" x="340" y="147" text-anchor="middle">4</text>
<circle class="vx-dot" cx="540" cy="165" r="8"/><text class="vx-text" x="540" y="147" text-anchor="middle">5</text>
<circle class="vx-dot" cx="140" cy="260" r="8"/><text class="vx-text" x="140" y="242" text-anchor="middle">6</text>
<circle class="vx-dot" cx="340" cy="260" r="8"/><text class="vx-text" x="340" y="242" text-anchor="middle">7</text>
<circle class="vx-dot" cx="540" cy="260" r="8"/><text class="vx-text" x="540" y="242" text-anchor="middle">8</text>
<text class="vx-text-accent" x="40" y="305">every arrow: smaller number to larger, legal</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 2. Interchanged: j outer, i inner.</strong> Same grid, same four physical dependences, renumbered column by column. Every arrow now points from a larger number to a smaller one: the interchanged order would run each dependence's read before its write, so the interchange is illegal.</p>
<svg viewBox="0 0 700 330" role="img" aria-label="The same 3 by 3 grid, renumbered in column-major order: column j=0 holds 0, 1, 2 from i=0 to i=2; column j=1 holds 3, 4, 5; column j=2 holds 6, 7, 8. The same four dependence arrows are drawn in red, dashed: from 3 to 1, from 6 to 4, from 4 to 2, and from 7 to 5. Every arrow now points from a larger number to a smaller one, so every dependence would run backward and the interchange is illegal.">
<text class="vx-text-muted" x="140" y="30" text-anchor="middle">j = 0</text>
<text class="vx-text-muted" x="340" y="30" text-anchor="middle">j = 1</text>
<text class="vx-text-muted" x="540" y="30" text-anchor="middle">j = 2</text>
<text class="vx-text-muted" x="40" y="75" text-anchor="middle">i = 0</text>
<text class="vx-text-muted" x="40" y="170" text-anchor="middle">i = 1</text>
<text class="vx-text-muted" x="40" y="265" text-anchor="middle">i = 2</text>
<path class="vx-box-bad" d="M328 82 L164 152" marker-end="url(#p6-f1b-head)"/>
<path class="vx-box-bad" d="M528 82 L364 152" marker-end="url(#p6-f1b-head)"/>
<path class="vx-box-bad" d="M328 177 L164 247" marker-end="url(#p6-f1b-head)"/>
<path class="vx-box-bad" d="M528 177 L364 247" marker-end="url(#p6-f1b-head)"/>
<defs>
<marker id="p6-f1b-head" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" style="fill: var(--vortex-code-number)"/></marker>
</defs>
<circle class="vx-dot" cx="140" cy="70" r="8"/><text class="vx-text" x="140" y="52" text-anchor="middle">0</text>
<circle class="vx-dot" cx="340" cy="70" r="8"/><text class="vx-text" x="340" y="52" text-anchor="middle">3</text>
<circle class="vx-dot" cx="540" cy="70" r="8"/><text class="vx-text" x="540" y="52" text-anchor="middle">6</text>
<circle class="vx-dot" cx="140" cy="165" r="8"/><text class="vx-text" x="140" y="147" text-anchor="middle">1</text>
<circle class="vx-dot" cx="340" cy="165" r="8"/><text class="vx-text" x="340" y="147" text-anchor="middle">4</text>
<circle class="vx-dot" cx="540" cy="165" r="8"/><text class="vx-text" x="540" y="147" text-anchor="middle">7</text>
<circle class="vx-dot" cx="140" cy="260" r="8"/><text class="vx-text" x="140" y="242" text-anchor="middle">2</text>
<circle class="vx-dot" cx="340" cy="260" r="8"/><text class="vx-text" x="340" y="242" text-anchor="middle">5</text>
<circle class="vx-dot" cx="540" cy="260" r="8"/><text class="vx-text" x="540" y="242" text-anchor="middle">8</text>
<text class="vx-text-accent" x="40" y="305" style="fill: var(--vortex-code-number)">every arrow: larger number to smaller, illegal</text>
</svg>
</div>
</div>
<figcaption>Figure 1. The skewed stencil's iteration space, 3 by 3, under its original loop order and under the interchanged order. The four dots and four dependence arrows are the same points and the same physical relationship in both panels; only the numbering, which marks each panel's execution order, and therefore which end of each arrow runs first, changes.</figcaption>
</figure>

??? check "Why is interchange legal for the matvec nest even though c[row] accumulates across the whole k loop, but illegal for the stencil?"

    Legality depends only on the sign pattern of each dependence's vector after reordering, not on how many dependences share a distance or what kind they are. The matvec's dependences all have distance `(0, k₂ − k₁)`, one nonzero component, which stays positive in either loop order. The stencil's single dependence has distance `(1, −1)`, two nonzero components, and interchange moves the negative one in front of the positive one, making the reordered vector lexicographically negative.

## Deciding whether a dependence exists: ZIV, SIV, MIV

Distance vectors describe a dependence once it is known to exist. Finding out whether it exists at all is a separate question, decided one pair of subscripts at a time. Goff, Kennedy and Tseng's practical dependence tester classifies a pair of subscripts to the same array by how many of the nest's loop indices they mention, and picks a test accordingly, cheapest first.[^gkt91]

A subscript pair is **ZIV** (zero index variables) when neither expression mentions any loop index: both are constants, or expressions in variables the loop never changes. The test is trivial: the two subscripts collide exactly when they are provably equal, a fact a constant-folding or value-range pass already knows.

A pair is **SIV** (single index variable) when exactly one loop index, shared by both subscripts, appears in either one. This is the common case for a single loop's own induction variable, and it has direct algebraic tests, the simplest being the GCD test below.

A pair is **MIV** (multiple index variables) when more than one loop index appears across the two subscripts, as in a two-dimensional array indexed by two different loop counters in a nest that has more than two levels. MIV subscripts need more general reasoning: Banerjee's inequalities bound where a real-valued solution could lie, and when even that does not decide the question, an exact integer test such as the Omega test settles it precisely, by treating the subscripts' equality as a small integer program and asking whether it has a solution inside the loops' bounds.[^pugh91]

When no test decides, and one always terminates the search, a compiler assumes a dependence exists. This is a conservative default: missing an optimization costs performance, but reordering across a dependence that does exist costs correctness, and correctness comes first.

## The GCD test, and its limits

The GCD test is the cheapest SIV test, and the reasoning behind it generalizes to the harder cases. Two subscripts $a_1 i + c_1$ and $a_2 i + c_2$, sharing a loop index $i$ that two different occurrences of the loop, $i_1$ and $i_2$, might take different values of, touch the same array element exactly when

$$
a_1 i_1 - a_2 i_2 = c_2 - c_1
$$

has an integer solution. A linear Diophantine equation $ax + by = c$ has an integer solution in $x$ and $y$ exactly when $\gcd(a, b)$ divides $c$; applying that fact here, the subscripts can only collide if $\gcd(a_1, a_2)$ divides $c_2 - c_1$.[^gkt91] When it does not, the test proves no collision is possible for *any* integers $i_1$ and $i_2$, not merely the ones the loop's bounds allow, so there is no dependence, full stop. When it does divide, an integer solution to the equation exists, but that solution might fall outside the loop's actual bounds, or a dependence might exist for other reasons the equation does not model, so the test has not proven a dependence, only failed to rule one out.

The second example applies the test to five subscript pairs, the kind that show up in unrolled or strided code.

--8<-- "includes/examples/optimize/p6-dependence-analysis/gcd_test.cpp.md"

`a[i] vs a[i - 1]` is the running-count loop's own subscript pair, and the test cannot rule it out, correctly: the dependence is real. `a[2*i] vs a[2*i + 1]` compares every even-indexed element against every odd-indexed one; no integer solution exists, because the left side of $2i_1 - 2i_2 = 1$ is always even and the right side is odd, and the GCD test catches exactly this, since $\gcd(2, 2) = 2$ does not divide 1. `a[3*i] vs a[3*i + 6]` does divide, $3 \mid 6$, and here the test is right to hold back: $i_1 = i_2 + 2$ really does make the two subscripts equal.

??? check "The GCD test says a[3*i] and a[3*i + 6] cannot be ruled out. Does that prove a dependence, and what must a compiler that stops here do?"

    It proves only that the equation has an integer solution somewhere, not that the solution lands inside the loop's bounds or that both accesses actually run. A compiler that has no stronger test must treat this pair as an assumed dependence: safe, because it never permits an illegal reorder, but conservative, because it may block a transformation that a more precise test, such as Banerjee's inequalities or the Omega test, would have allowed.

## Vortex's shapes remove a whole step

Every subscript test above assumes the subscript is already an affine function of the loop's own indices, one dimension at a time, `a[row, k]`, not `a[row * 64 + k]`. That assumption costs LLVM's DependenceAnalysis a step Vortex does not need. C, and the C-like IR Clang emits for it, has no multi-dimensional array type at the level dependence analysis runs; a two-dimensional access is already lowered to pointer arithmetic over a flat buffer, `row * 64 + k`, before any dependence pass sees it. LLVM's DependenceAnalysis pass says so in its own source: it is described there as an incomplete implementation of Goff, Kennedy and Tseng's algorithm, and it depends on ScalarEvolution's delinearization to recover the separate `row` and `k` subscripts from that single flattened expression before its ZIV, SIV and MIV tests can even start.[^llvm-da]

A Vortex array carries its shape in its type, `[f32; 64, 64]`, and indexing it takes one index per dimension ([Arrays 7.6](../specification/arrays.md#76-indexing)); nothing in a Vortex front end ever flattens `a[row, k]` into a single offset the way Clang lowers a C array. A compiler working from Vortex's IR can read `a`'s access as the pair of per-dimension subscripts `(row, k)` directly, exactly the input Goff, Kennedy and Tseng's tests expect, with no delinearization pass standing between the source language and the analysis.

??? check "Why can a Vortex dependence pass skip the delinearization step LLVM's DependenceAnalysis relies on?"

    Because Vortex never linearizes a multi-dimensional subscript into one flat offset in the first place. Its arrays keep their declared shape and are indexed one dimension at a time, so the per-dimension subscripts a dependence test needs are already separate in the IR, instead of needing to be reverse-engineered out of a single flattened expression the way Clang's lowering requires.

## Your turn: a nest with two dependences

This loop updates `a` in place, one column to the right of where it reads:

```text
for i in 0..n - 1 {
    for j in 0..n - 1 {
        a[i][j + 1] = a[i][j] + a[i + 1][j]
    }
}
```

It has two dependences on `a`, not one. Fill in the table, then decide whether interchanging `i` and `j` is legal.

| Read | Matches a write from iteration | Kind | Distance vector (Δi, Δj) |
| --- | --- | --- | --- |
| `a[i][j]` | (i, j − 1) | flow | (0, 1) |
| `a[i + 1][j]` | ? | ? | ? |

??? check "Answer: the second dependence, and whether the interchange is legal"

    `a[i + 1][j]`, read at iteration `(i, j)`, is written by the loop itself at iteration `(i + 1, j - 1)`, since that iteration's write target is `a[i + 1][(j - 1) + 1]`, which is `a[i + 1][j]`. Comparing `(i, j)` to `(i + 1, j - 1)` lexicographically, the first is earlier, so the reading iteration is earlier and the writing one is later: an anti-dependence, distance `(1, −1)`.

    The first dependence alone, distance `(0, 1)`, would permit any loop order, exactly like the matvec nest above. But the second has the skewed stencil's shape, and interchanging `i` and `j` turns its distance into `(−1, 1)`, lexicographically negative. One illegal dependence is enough to block the whole permutation, even though the other is fine with it: the nest cannot be interchanged as written.

## For Vortex

!!! vortex "Exercise"

    **Build** dependence testing over Vortex's multi-dimensional array subscripts, on top of the loop structure and add recurrences from [O8](o8-loops.md#for-vortex) and the control-flow toolkit from [O2](o2-cfg-and-dominance.md#for-vortex).

    1. For every pair of accesses to the same array within a loop nest, classify each dimension's subscript pair as ZIV, SIV or MIV, using the loop indices that dimension's subscript mentions.
    2. ZIV: decide by constant equality. SIV: the GCD test, and, where it does not decide, a bounded search over the loop's actual extent (small in Vortex's fixed-shape arrays, since every extent is known at compile time). MIV: the GCD test per dimension, conservatively assuming a dependence when it does not rule one out.
    3. For each confirmed dependence, record which access is earlier, classify it as flow, anti or output from whether each end is a read or a write, and compute its distance vector across every level of the nest.
    4. A legality check for a proposed permutation of the nest's loop levels (interchange, or the loop reordering a tiling introduces): reorder every dependence's distance vector to match the proposed order, and accept only if every one stays lexicographically positive.
    5. Emit one remark per legality decision, naming the dependence, its distance vector, and the verdict, in the style O8's check-elimination remarks used, for example "interchange (row, k) legal: only dependence is on c, distance (0, 1)" or "interchange (i, j) illegal: dependence on a, distance (1, -1) becomes (-1, 1)".

    **Not yet:** the Omega test or Banerjee's inequalities for MIV subscripts the GCD test cannot decide (a stretch goal; assuming a dependence is always safe); dependence through pointers or through parameters that might alias, which waits for [O9](o9-alias-analysis.md); non-affine subscripts, such as one array indexed by a value read from another; actually performing any transformation the legality check permits, which is [P7](p7-loop-transformations.md)'s job.

    **Proof that it works:**

    - A hand-derived test suite: the matvec-style accumulation (legal under any permutation), the skewed stencil (illegal after interchange), a fully elementwise nest with no shared subscripts (legal, and no dependence reported at all), and the two-dependence nest from this chapter's exercise (illegal, because of one dependence out of two).
    - For each case, a remark log showing every dependence found, its kind and its distance vector, checked by hand against the derivation.
    - A case where the GCD test cannot decide: confirm the pass reports an assumed dependence rather than either a proof or silence.
    - Once [P7](p7-loop-transformations.md) exists, a golden test that runs an interchange only when this chapter's check calls it legal, and shows, by disabling the check, that forcing an illegal one changes the output.

## Key ideas

!!! recap "Questions you can now answer"

    - **What makes a compiler unable to freely reorder a loop's iterations?** A dependence: two accesses to the same location, at least one a write, whose relative order the original program fixes.
    - **What are the three kinds of dependence?** Flow (read after write), anti (write after read) and output (write after write).
    - **What is a distance vector?** The later dependent iteration minus the earlier one, one loop level at a time; its direction vector keeps only each component's sign.
    - **When is a permutation of a loop nest's levels legal?** When every dependence's distance vector, reordered to match the new nesting, is still lexicographically positive: its first nonzero component stays positive.
    - **What does the GCD test decide, and what does it leave open?** It proves no dependence when a coefficient's GCD does not divide the subscripts' constant difference; when it does divide, it has not proven a dependence, only failed to rule one out.
    - **Why is the matmul kernel's only loop-carried dependence on c, and why does that make every loop order legal?** `a` and `b` are read-only with subscripts that never repeat inside the nest; `c[row, column]` is read and written on every value of `k`, but that single dependence has one nonzero component, which stays positive wherever the permutation puts it.
    - **Why does a Vortex dependence pass skip SCEV's delinearization step?** Because Vortex arrays keep their declared shape and are indexed one dimension at a time, so the per-dimension subscripts a dependence test needs are already separate, unlike a subscript already flattened to one pointer offset.

## Where this comes back

!!! next "You will use this again in"

    - [P7. Loop transformations](p7-loop-transformations.md): *distance vectors*, *the legality check for interchange, fusion and unroll-and-jam*
    - [P8. Cache blocking](p8-cache-blocking.md): *legal tiling orders*
    - [P9. The polyhedral model](p9-polyhedral-model.md): *the Omega test*, *exact dependence as an integer program*
    - [P10. Vectorization](p10-vectorization.md): *no loop-carried dependence as a legality precondition*
    - [P13. Multithreading](p13-multithreading.md): *which loop levels carry no dependence, and so may run on separate threads*

## Sources and further reading

Start with Goff, Kennedy and Tseng's paper: it is short, practical, and gives the ZIV, SIV, MIV classification and the GCD test in the order a compiler should try them. Bacon, Graham and Sharp's survey covers the general legality rule for loop permutations, with the wider family of transformations it enables. Pugh's Omega test paper is worth reading once the limits of the GCD test are clear, for what an exact integer test buys over a conservative one. LLVM's `DependenceAnalysis.cpp` header is short and is the direct source for this chapter's claim about delinearization.

[^gkt91]: Gina Goff, Ken Kennedy and Chau-Wen Tseng, "Practical Dependence Testing", *Proceedings of the ACM SIGPLAN Conference on Programming Language Design and Implementation (PLDI 1991)*: the abstract, and the sections on the ZIV, SIV and MIV classification and the GCD test. <https://doi.org/10.1145/113445.113448>
[^pugh91]: William Pugh, "The Omega Test: A Fast and Practical Integer Programming Algorithm for Dependence Analysis", *Proceedings of Supercomputing '91*: the abstract and the introduction's comparison with the GCD and Banerjee tests. <https://doi.org/10.1145/125826.125848>
[^bgs94]: David F. Bacon, Susan L. Graham and Oliver J. Sharp, "Compiler Transformations for High-Performance Computing", *ACM Computing Surveys* 26(4), 1994: the sections on data dependence and on the legality of loop reordering transformations. <https://doi.org/10.1145/197405.197406>
[^llvm-da]: LLVM Project, `DependenceAnalysis.cpp`, release/18.x branch: the file header's description of the algorithm as based on, but not a complete implementation of, Goff, Kennedy and Tseng, and its reliance on `ScalarEvolution`'s delinearization. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Analysis/DependenceAnalysis.cpp>
