# P9. The polyhedral model

<p class="page-intro">A loop nest's iteration space is a set of integer points, and a schedule is an affine map from that set to logical time: one representation for interchange, skewing, tiling and fusion, and one test that decides whether any of them is legal.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 30 minutes · Builds on: [P6. Dependence analysis](p6-dependence-analysis.md), [P7. Loop transformations](p7-loop-transformations.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does a dependence's distance vector record, and when may loops be reordered?"

        For two accesses to one location, loop by loop from the outermost: whether the second runs in a later iteration (`<`), the same one (`=`) or an earlier one (`>`). A reordering of the loops is legal when no dependence, rewritten for the new order, has `>` as its first entry that is not `=`.

        Introduced in [P6. Dependence analysis](p6-dependence-analysis.md).

    ??? question "What single test decided whether a unimodular matrix transformation was legal?"

        A matrix $T$ is legal exactly when $T\,d$ is lexicographically positive for every dependence distance $d$: the transformed nest still runs every dependence forward in time.

        Introduced in [P7. Loop transformations](p7-loop-transformations.md).

    ??? question "In what order are the elements of a `[f32; 64, 64]` array stored?"

        Row after row, the last index varying fastest: `m[i, j + 1]` sits next to `m[i, j]`, and `m[i + 1, j]` is a row away.

        Introduced in [Arrays and shapes, decision 43](../decisions/arrays.md#d43).

    ??? question "May a Vortex compiler regroup the additions in a floating-point accumulation?"

        No. Each floating-point operation is one IEEE 754 operation, rounded once, and must not be contracted, reassociated or reordered.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

    ??? question "What does passing `&mut c` promise about the other arguments of the same call?"

        That none of them is `c`: inside the callee, `c` shares storage with no other parameter.

        Introduced in [References and mutability, decision 25](../decisions/references.md#d25).

!!! goals "In this chapter"

    - Explain why a loop nest's iteration domain is a set of integer points bounded by affine inequalities, a description separate from the loop that happens to enumerate it.
    - Generalize P7's unimodular legality test to an arbitrary affine schedule, and see what a schedule buys: interchange, skewing and tiling become one choice of map instead of separate named transformations.
    - Recognize why Vortex's fixed array shapes and affine subscripts already put its matmul kernel inside the polyhedral fragment, without the recovery step a general-purpose compiler needs first.
    - Separate what the model represents from what a scheduler must still search for: legality is a yes-or-no test, and a good schedule is a search problem.

## A loop nest as a set of points

Take a small nest that is not the matmul kernel yet:

```cpp
for (int i = 0; i < N; ++i)
  for (int j = 0; j <= i; ++j)
    body(i, j);
```

Row `i` visits one more column than the row before it, so this is not a rectangle: it is a **triangle**. `body` runs once for every pair `(i, j)` with `0 <= j <= i < N`, and nothing else. That set of pairs, independent of the two `for` statements that happen to produce it, is the nest's **iteration domain**: the set of integer points that satisfy a list of **affine inequalities** (each one a sum of variables and constants, compared with `<=`, `<` or `>=`, never multiplied together). Here there are three: `i >= 0`, `i < N`, and `0 <= j <= i`.

A rectangular nest, every bound a constant independent of the other loop variables, is a **box**: the matmul kernel's domain is `0 <= i, j, k < 64`, three inequalities that do not mention each other's variable. P7's unimodular matrices reorder a box's axes, and that is all they need to, because every transformation that chapter covers, interchange, tiling, unroll-and-jam, keeps the domain a box: only the order of visiting its points changes. The triangle above cannot be written that way. No renaming of `i` and `j` turns `0 <= j <= i` into two bounds that ignore each other, because the second loop's own bound depends on the first loop's variable. The domain's shape, not only the order the loop visits it in, is where the polyhedral model starts: describe the shape once, as a set, and then ask separately in what order to visit it.

`domain.cpp` checks that description against the loop that is supposed to match it, for `N = 6`:

--8<-- "includes/examples/optimize/p9-polyhedral-model/domain.cpp.md"

A domain need not be a triangle. It can be any set the affine inequalities can carve out of the integer grid for a nest of any depth: a box, a triangle, a trapezoid from a loop whose bound is `min(i + 4, N)`, or the intersection of several such shapes for a nest with more than one array access. Feautrier's dataflow analysis works over exactly these sets, computing, for every read, the single write that produced the value it reads, as an affine function of the read's own indices.[^feau91]

??? check "Is `for (int i = 0; i < N; ++i) for (int j = i; j < N; ++j)` a box or a triangle, and why?"

    A triangle again: `j`'s lower bound depends on `i`, so the two bounds cannot be separated into one inequality per variable that ignores the other. Swap the roles from the text: `0 <= i <= j < N`.

## A schedule is an affine map

Now order. The nest above already gives one order, `i` outer, `j` inner, but nothing about the domain fixes that. A **schedule** is a function from the domain to a **logical time**, a vector compared the same way P7 compared distance vectors: lexicographically, entry by entry from the first. The loop as written computes one schedule, $\theta_0(i, j) = (i, j)$: visit points in increasing order of $i$, and within one $i$, increasing order of $j$. That is a choice, not a law. $\theta_1(i, j) = (i + j, j)$ is a different, equally affine, choice.

To see what changes, give the triangle something to depend on. Let `t(i, j)` follow Pascal's triangle: `t(i, 0) = t(i, i) = 1`, and `t(i, j) = t(i - 1, j) + t(i - 1, j - 1)` for `0 < j < i`. Every interior point reads two points from the row above it. `schedule.cpp` applies $\theta_0$ and $\theta_1$ to every point of a five-row triangle and sorts by the result:

--8<-- "includes/examples/optimize/p9-polyhedral-model/schedule.cpp.md"

$\theta_0$ reproduces the order the nest is already written in, no surprise, since a lexicographic sort by `(i, j)` is what nested loops already do. $\theta_1$ groups the same fifteen points differently. Point `(2, 2)` and point `(3, 1)` land at the same first component, time `4`: both sit on the anti-diagonal where `i + j = 4`. Figure 1 draws both orders for a smaller, three-row triangle, with the dependences of Pascal's triangle drawn as lines.

<figure class="vx-figure">
<svg viewBox="0 0 340 430" role="img" aria-label="A six-point triangular domain and the same domain sheared by an affine schedule" aria-describedby="p9-f1-desc">
<title id="p9-f1-title">A triangular domain sheared by an affine schedule</title>
<desc id="p9-f1-desc">Two panels share six points of the domain 0 less than or equal to j less than or equal to i less than 3. The top panel arranges them as a left-aligned triangle by row i and column j, with six lines showing the dependences of Pascal's triangle: each point below the top row is connected to the one or two points above it that it reads. The bottom panel places the same six points and the same six lines using the schedule w equals i plus j, c equals j: the triangle becomes a slanted parallelogram. Two of the six points, (1, 1) and (2, 0), are highlighted in both panels: in the top panel they sit in different rows with no line between them; in the bottom panel they land in the same column, w equals 2, confirming that a schedule's column groups independent points together.</desc>
<text class="vx-text" x="20" y="30">Domain 0 ≤ j ≤ i &lt; 3, in the order the loop writes it</text>
<text class="vx-text-muted" x="90" y="52" text-anchor="middle">j=0</text>
<text class="vx-text-muted" x="138" y="52" text-anchor="middle">j=1</text>
<text class="vx-text-muted" x="186" y="52" text-anchor="middle">j=2</text>
<text class="vx-text-muted" x="46" y="74" text-anchor="end">i=0</text>
<text class="vx-text-muted" x="46" y="122" text-anchor="end">i=1</text>
<text class="vx-text-muted" x="46" y="170" text-anchor="end">i=2</text>
<line class="vx-line" x1="90" y1="70" x2="90" y2="118"/>
<line class="vx-line" x1="90" y1="70" x2="138" y2="118"/>
<line class="vx-line" x1="90" y1="118" x2="90" y2="166"/>
<line class="vx-line" x1="138" y1="118" x2="138" y2="166"/>
<line class="vx-line" x1="90" y1="118" x2="138" y2="166"/>
<line class="vx-line" x1="138" y1="118" x2="186" y2="166"/>
<circle class="vx-box" cx="90" cy="70" r="7"/>
<circle class="vx-box" cx="90" cy="118" r="7"/>
<circle class="vx-box-accent" cx="138" cy="118" r="7"/>
<circle class="vx-box-accent" cx="90" cy="166" r="7"/>
<circle class="vx-box" cx="138" cy="166" r="7"/>
<circle class="vx-box" cx="186" cy="166" r="7"/>
<text class="vx-mono" x="90" y="93" text-anchor="middle">(0,0)</text>
<text class="vx-mono" x="90" y="141" text-anchor="middle">(1,0)</text>
<text class="vx-mono" x="138" y="141" text-anchor="middle">(1,1)</text>
<text class="vx-mono" x="90" y="189" text-anchor="middle">(2,0)</text>
<text class="vx-mono" x="138" y="189" text-anchor="middle">(2,1)</text>
<text class="vx-mono" x="186" y="189" text-anchor="middle">(2,2)</text>
<text class="vx-text" x="20" y="270">Schedule θ1(i, j) = (i + j, j): same points, same lines, sheared</text>
<text class="vx-text-muted" x="90" y="292" text-anchor="middle">w=0</text>
<text class="vx-text-muted" x="138" y="292" text-anchor="middle">w=1</text>
<text class="vx-text-muted" x="186" y="292" text-anchor="middle">w=2</text>
<text class="vx-text-muted" x="234" y="292" text-anchor="middle">w=3</text>
<text class="vx-text-muted" x="282" y="292" text-anchor="middle">w=4</text>
<line class="vx-line" x1="90" y1="300" x2="138" y2="300"/>
<line class="vx-line" x1="90" y1="300" x2="186" y2="348"/>
<line class="vx-line" x1="138" y1="300" x2="186" y2="300"/>
<line class="vx-line" x1="186" y1="348" x2="234" y2="348"/>
<line class="vx-line" x1="138" y1="300" x2="234" y2="348"/>
<line class="vx-line" x1="186" y1="348" x2="282" y2="396"/>
<circle class="vx-box" cx="90" cy="300" r="7"/>
<circle class="vx-box" cx="138" cy="300" r="7"/>
<circle class="vx-box-accent" cx="186" cy="300" r="7"/>
<circle class="vx-box-accent" cx="186" cy="348" r="7"/>
<circle class="vx-box" cx="234" cy="348" r="7"/>
<circle class="vx-box" cx="282" cy="396" r="7"/>
<text class="vx-mono" x="90" y="323" text-anchor="middle">(0,0)</text>
<text class="vx-mono" x="138" y="323" text-anchor="middle">(1,0)</text>
<text class="vx-mono" x="186" y="323" text-anchor="middle">(2,0)</text>
<text class="vx-mono" x="186" y="371" text-anchor="middle">(1,1)</text>
<text class="vx-mono" x="234" y="371" text-anchor="middle">(2,1)</text>
<text class="vx-mono" x="282" y="419" text-anchor="middle">(2,2)</text>
</svg>
<figcaption>Figure 1. Six points of the triangular domain 0 ≤ j ≤ i &lt; 3, with the six dependences of Pascal's triangle drawn as lines. In the top panel the schedule is θ0(i, j) = (i, j): rows in source order. In the bottom panel the schedule is θ1(i, j) = (i + j, j): the same points and the same lines, but every point now sits at column w = i + j. The two accent-colored points, (1, 1) and (2, 0), sit in different rows in the top panel with no line between them, and land in the same column in the bottom panel: every dependence crosses at least one column, so two points that share a column, like these two, never depend on each other.</figcaption>
</figure>

Every point on one anti-diagonal of the triangle shares the schedule's first coordinate. That coordinate is exactly the **wave** from P7's wavefront example: the triangle is a different shape from that chapter's square stencil, but the same idea, an affine map that turns a diagonal band of independent work into one schedule dimension, applies to it without change.[^wl91]

??? check "Why does theta1 have two components instead of one?"

    One component is not enough to pick a full order: two points can share `i + j` and still need placing relative to each other, since only some pairs on an anti-diagonal are independent, not the whole domain in general (a domain with more accesses might carry a dependence within one diagonal). The second component, `j`, breaks that tie: within one wave, points still visit in a fixed order, even one that, in this domain, happens to be free to run in parallel.

## Legality, generalized

P7's legality test was one sentence: a unimodular matrix $T$ is legal exactly when $T\,d$ is lexicographically positive for every dependence distance $d$.[^wl91] That test used two things a schedule does not need: that the transformation is linear (a matrix, not any affine map) and that the domain is a box, so one distance vector describes every dependence of a given kind, the same everywhere in the nest. Neither restriction survives once the domain can be a triangle or the schedule can carry a constant offset. The generalized test keeps the same shape and drops both restrictions: a schedule $\theta$ is legal exactly when, for every dependence from iteration $s$ to iteration $t$ (meaning $t$ reads or writes something $s$ wrote, with $s$ required to run first), $\theta(t) - \theta(s)$ is lexicographically positive. $\theta_0$ satisfies this trivially, since it is the order the dependence already runs in. $\theta_1$ satisfies it because every dependence in Pascal's triangle crosses at least one anti-diagonal: `(i - 1, j)` to `(i, j)` moves the wave number by exactly `1`, and `(i - 1, j - 1)` to `(i, j)` moves it by `2`. Neither ever stays on one wave, which is exactly why points that share a wave have no dependence between them.

`legality.cpp` finds every dependence in the triangle by brute force, the same style of check as P6, and puts three schedules through the test: $\theta_0$, $\theta_1$, and a third, $\theta_2(i, j) = (-i, j)$, built to fail.

--8<-- "includes/examples/optimize/p9-polyhedral-model/legality.cpp.md"

$\theta_2$ negates the one schedule dimension every dependence relies on, so it is caught on the first dependence checked. That is the benefit of stating legality this way: the checker does not need to know it is looking at a triangle, or at Pascal's triangle specifically, or that the bad schedule happens to be a sign flip. It needs the domain, the dependences and a candidate $\theta$, and it answers yes or no.

??? check "theta0 and theta1 are both legal for the Pascal's-triangle dependences. Are they legal for every recurrence over the same triangular domain?"

    No. Legality depends on the actual dependences, not the domain's shape alone. A recurrence that also read `t(i - 1, j + 1)`, one column to the right, would add a dependence with distance `(1, -1)`: theta1's first component, `i + j`, would move by `1 + (-1) = 0`, and its second component, `j`, would move by `-1`. That delta is not lexicographically positive, so theta1 would be illegal for that recurrence, even on the same triangle.

## One representation, many transformations

P7 built a separate argument for each transformation: interchange swaps two matrix rows, skewing adds one row to another, tiling strip-mines and then interchanges, fusion and fission merge or split whole loops. Each has its own legality story. In the polyhedral model, all four are the same kind of move: pick a different affine $\theta$ over the same domain. Interchange is $\theta(i, j) = (j, i)$. Skewing by a factor $f$ is $\theta(i, j) = (i, f \cdot i + j)$. A rectangular tile of size $T$ is a schedule with one extra pair of dimensions per tiled loop, tile index outer and position within the tile inner. Fusing two loops that used to run one after another is giving their two statements the same value in the schedule dimension that used to separate them. None of these needs a new theorem: the one legality test from the previous section covers all of them, because it never looked at which named transformation produced $\theta$.

That uniformity is also the field's chosen trade. Feautrier posed finding a schedule as an optimization problem over the affine coefficients of $\theta$, subject to the legality inequalities, rather than writing a heuristic and a proof for each named transformation and an order to try them in, the way P7's chapter ends.[^feau92] PLUTO builds on this to search for **tiling hyperplanes**: schedule dimensions chosen so that tiling along them is legal and improves locality and parallelism together, reporting success on kernels no single named transformation handled well alone.[^pluto] isl, the **integer set library**, gives this search a shared data structure: iteration domains, dependences and schedules are all sets or maps of integer points described by Presburger formulas (affine inequalities combined with "and", "or" and quantifiers), with operations such as intersection and lexicographic minimum implemented once for the library rather than once per compiler.[^isl10] Polly is isl wired into LLVM as a pass: it lifts a loop nest's IR into isl's representation when the nest is provably affine, searches, and lowers the chosen schedule back to IR.[^polly]

Two things this section does not claim. First, none of these tools' own reported results transfer to Vortex or to the owner's machine: they are evidence that the representation scales to real kernels, not a number this book can reuse. Second, the model finding a schedule legal says nothing about whether it is fast; the next section is about that gap.

??? check "Why does a tiled schedule need two dimensions per tiled loop instead of one?"

    One dimension can order points, but a tile also groups them: the tile index says which tile a point belongs to, and the position within the tile says where inside it. Dropping either loses information the schedule needs: without the tile index, points from different tiles interleave; without the position, points inside one tile have no order.

## What the model does not give for free

Legality is a yes-or-no test; a good schedule is a search. For a domain of any size, most legal schedules are not fast: $\theta_0$ unchanged is always legal and is not what P7 spent a whole chapter building past. The search space is also unbounded in principle, since nothing above stops a schedule from adding an unused extra dimension or picking coefficients that scan the domain in an order no cache would want, so a real scheduler adds a bound and an objective (PLUTO, for instance, restricts coefficients to a small range and prefers schedules built from fewer, simpler terms[^pluto]) and then still has to solve an optimization problem to fill it in, more machinery than any single P7 transformation needed on its own.

The model also proves nothing about a schedule's cost: two legal schedules can differ enormously in cache behavior, and nothing in $\theta$ alone says which is better. P7's own ordering, fix access order for the line size, then tile for the cache sizes, then register-block, is exactly a set of preferences a scheduler could encode as an objective, and it needs the same machine facts, cache sizes, line size and register count, that P7 and [P8](p8-cache-blocking.md) do.

None of this is part of Vortex's v0.1 roadmap. v0.1 builds the named transformations, each with a legality check a reader can verify by eye against a dependence distance. A schedule-based representation is a later, larger and separable piece of work, useful once the named transformations it would replace are numerous enough that checking their combined legality by hand, one at a time, stops scaling.

## Vortex and the polyhedral fragment

The kernel from stage 10 that P6 and P7 build on has a domain that is a box, `0 <= i, j, k < 64`, and every subscript in it, `a[row, k]`, `b[k, column]`, `c[row, column]`, is an affine function of the loop variables with no multiplication between them: exactly the fragment the polyhedral model, and PLUTO's and Polly's search, are built for. A general-purpose C or C++ compiler does not get this for free. LLVM's own dependence analysis describes itself, in its source, as an incomplete implementation of the classic subscript tests, and it depends on `ScalarEvolution` to first recover an affine subscript from index arithmetic that Clang has already linearized into one flat offset.[^llvm-da-src] Vortex's fixed shapes ([decision 43](../decisions/arrays.md#d43)) mean the front end never has to throw that structure away, and a later pass never has to guess it back: an array's declared extents and an index expression's own multiply-free shape are enough to write down the domain and every access's affine map directly from the source, the same information `domain.cpp`'s `in_domain` checks by hand.

This is also where Vortex's stricter rules bite harder than they do for P7's named transformations. A schedule change may reorder the additions into `c[row, column]` exactly as freely as an interchange or a skew, no more: decision 56 forbids regrouping a floating-point accumulation regardless of which representation proposed the reordering, so a schedule's legality test here must include one more question, is this dimension the accumulator's own reduction, and if so does the schedule keep its iterations in their original relative order, not only does this schedule run every dependence forward. And a scheduler that fuses two statements, or tiles across a boundary where one loop's `&mut` output feeds another's input, inherits [decision 25](../decisions/references.md#d25)'s promise, that `c` shares storage with no other argument, as the fact that makes such a fusion's aliasing question decidable at all instead of a runtime check.

## For Vortex

!!! vortex "Exercise"

    **Build**, as a standalone tool outside the compiler proper, an affine schedule checker for the rectangular kernels [P6](p6-dependence-analysis.md) and [P7](p7-loop-transformations.md) already cover.

    1. A representation of a schedule as one affine map per statement: for a loop nest with `d` dimensions, a matrix of integer coefficients and a constant vector, `d` or more rows.
    2. A legality check that takes a schedule and a list of dependences (from [P6](p6-dependence-analysis.md), as distance vectors, since v0.1's kernels stay inside a box) and tests, for each one, whether the schedule keeps it lexicographically positive: `legality.cpp`'s check, generalized from a hand-written lambda to a matrix read from input.
    3. Two named schedules to check it against: the nest as written, and P7's interchange, confirmed legal or illegal by the same test P7 already trusts by hand.
    4. A remark for every check, naming the schedule's coefficients and the dependence, if any, that made it illegal.

    **Not yet:** searching for a schedule (PLUTO's problem, not this exercise's), a triangular or trapezoidal domain (Vortex's v0.1 kernels are rectangular), and wiring the checker into the compiler's own passes; this stays a standalone tool that takes the same dependence output P6 already produces.

    **Proof that it works:** the checker agrees with P7's unimodular test on every schedule that is also a unimodular matrix (interchange, skewing), for the stage 10 kernel and for a stencil with a genuine cross-iteration dependence; and it rejects a schedule built, as $\theta_2$ was, to violate one dependence on purpose, naming that dependence in its remark.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is an iteration domain?** The set of integer points a loop nest visits, described by affine inequalities, independent of the order any particular loop writes them in.
    - **What is a schedule?** An affine map from the domain to a vector of logical time, compared lexicographically; the loop as written is one schedule among many legal ones.
    - **How does the legality test generalize P7's `T d > 0`?** Drop the requirement that the map be linear and that the domain be a box: a schedule theta is legal when theta(t) minus theta(s) is lexicographically positive for every dependence from s to t.
    - **What do interchange, skewing, tiling and fusion have in common here?** Each is one choice of affine schedule over the same domain, checked by the same test, instead of a separate named transformation with its own proof.
    - **What does the model not decide?** Which legal schedule is fast. That is a search problem, with its own objective and machine facts, not a yes-or-no test.
    - **Why is Vortex's matmul kernel already in the polyhedral fragment?** Its domain is a box and every subscript is affine with no recovery step, unlike a general compiler's IR after linearization has erased that structure.

## Where this comes back

!!! next "You will use this again in"

    - [P10. Vectorization](p10-vectorization.md): *affine schedule*, *legal reordering*
    - [P13. Multithreading](p13-multithreading.md): *parallel schedule dimension*
    - [P15. Choosing parameters: models or search](p15-choosing-parameters.md): *schedule search*, *objective*
    - [M6. Loops: affine and scf](../mlir/m6-affine-and-scf.md): *affine map*, *iteration domain*
    - [M9. Schedules as IR: the transform dialect](../mlir/m9-transform-dialect.md): *schedule as a first-class value*

## Sources and further reading

For the founding papers, read Feautrier on exact dataflow analysis and on posing scheduling as an optimization problem. For a tool built on the same ideas with reported results on real kernels, read the PLUTO paper. For the shared data structure much of the field now builds on, read the isl paper or its manual. For how this fits inside a mainstream compiler, read Polly's own documentation.

[^feau91]: Paul Feautrier, "Dataflow Analysis of Array and Scalar References", *International Journal of Parallel Programming* 20(1), 1991. <https://doi.org/10.1007/BF01407931>
[^feau92]: Paul Feautrier, "Some Efficient Solutions to the Affine Scheduling Problem, Part I: One-Dimensional Time", *International Journal of Parallel Programming* 21(5), 1992. <https://doi.org/10.1007/BF01407835>
[^pluto]: Uday Bondhugula, Albert Hartono, J. Ramanujam and P. Sadayappan, "A Practical Automatic Polyhedral Parallelizer and Locality Optimizer", *Proceedings of the ACM SIGPLAN 2008 Conference on Programming Language Design and Implementation (PLDI)*, 2008. <https://doi.org/10.1145/1375581.1375595> (also at <https://pluto-compiler.sourceforge.net/>)
[^isl10]: Sven Verdoolaege, "isl: An Integer Set Library for the Polyhedral Model", *Mathematical Software (ICMS 2010)*, Lecture Notes in Computer Science, 2010. <https://doi.org/10.1007/978-3-642-15582-6_49> (isl site: <https://libisl.sourceforge.io/>)
[^polly]: The Polly Project. <https://polly.llvm.org/> ; Tobias Grosser, Armin Größlinger and Christian Lengauer, "Polly: Performing Polyhedral Optimizations on a Low-Level Intermediate Representation", *Parallel Processing Letters* 22(4), 2012. <https://doi.org/10.1142/S0129626412500107>
[^wl91]: Michael E. Wolf and Monica S. Lam, "A Data Locality Optimizing Algorithm", *Proceedings of the ACM SIGPLAN 1991 Conference on Programming Language Design and Implementation (PLDI)*, 1991 (also cited in [P7](p7-loop-transformations.md)). <https://doi.org/10.1145/113445.113449> (free copy: <https://suif.stanford.edu/papers/wolf91a.pdf>)
[^llvm-da-src]: LLVM Project, `DependenceAnalysis.cpp` and `ScalarEvolution.cpp`. <https://github.com/llvm/llvm-project/blob/main/llvm/lib/Analysis/DependenceAnalysis.cpp>
