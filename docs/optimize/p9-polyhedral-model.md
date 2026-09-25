# P9. The polyhedral model

<p class="page-intro">P7 proved each loop transformation legal with its own argument. The polyhedral model replaces those arguments with one: a loop nest becomes a set of integer points, its dependences become relations between points, and every reordering becomes an affine schedule checked by the same test. This chapter builds that model by hand on small nests and on the stage 10 kernel, whose fixed shapes and affine subscripts put it inside the model with no recovery step.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 50 minutes · Builds on: [P6. Dependence analysis](p6-dependence-analysis.md), [P7. Loop transformations](p7-loop-transformations.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is a dependence's distance vector, and when is a permutation of loops legal?"

        The later iteration minus the earlier one, loop by loop. A permutation is legal when every distance vector, with its entries reordered to match the new loop order, is still lexicographically positive: its first nonzero entry is positive.

        Introduced in [P6. Dependence analysis](p6-dependence-analysis.md#when-a-loop-permutation-is-legal).

    ??? question "What single test decides whether a unimodular transformation is legal?"

        A unimodular matrix $T$ is legal exactly when $T\,d$ is lexicographically positive for every dependence distance $d$: the new nest still runs every dependence forward.

        Introduced in [P7. Loop transformations](p7-loop-transformations.md#skewing-and-the-unimodular-view).

    ??? question "When may a band of loops be tiled?"

        When the band is fully permutable: every dependence is lexicographically positive and, inside the band, either carried by an outer loop or free of negative entries.

        Introduced in [P7. Loop transformations](p7-loop-transformations.md#strip-mining-and-tiling).

    ??? question "May a Vortex compiler regroup the additions in `sum += a[row, k] * b[k, column]`?"

        No. Each floating-point operation is one IEEE 754 operation, rounded once, and must not be contracted, reassociated or reordered.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

    ??? question "What does passing `&mut c` promise about the other arguments of the same call?"

        That none of them is `c`. A variable lent as `&mut` may appear in no other argument of the call.

        Introduced in [References and mutability, decision 25](../decisions/references.md#d25).

!!! goals "In this chapter"

    - Describe a loop nest's iteration domain as a set of integer points bounded by affine inequalities, separate from the loops that happen to visit it.
    - Write dependences as relations between points, and tell memory-based dependences from value-based ones.
    - Generalize P7's unimodular test to affine schedules, and tell the test for legality from the stronger test for tiling.
    - Generate loops that visit a domain in a schedule's order, and explain what a polyhedral scheduler searches for.
    - Model the stage 10 kernel, three statements at two depths, and say which of Vortex's rules the model relies on.

## A loop nest as a set of points

Start with a nest that is not a rectangle:

```cpp
for (int i = 0; i < N; ++i)
  for (int j = 0; j <= i; ++j)
    body(i, j);
```

Row `i` visits one more column than the row before it. `body` runs once for every pair `(i, j)` with `0 <= j <= i < N`, and for nothing else. That set of pairs is the nest's **iteration domain**: the set of integer points the nest visits, taken as a set, with no order attached. P6 called the same thing the iteration space ([P6](p6-dependence-analysis.md#the-iteration-space-and-its-dependences)); the new word marks the shift in view, from loops that produce points to a set that exists before any loop is chosen.

This domain is cut out of the integer grid by four **affine inequalities**: each compares a sum of variables times constants, plus a constant, with zero. Here they are `i >= 0`, `N - 1 - i >= 0`, `j >= 0` and `i - j >= 0`. Written as rows of an integer matrix, the domain is the set of integer vectors $x$ with $A\,x + b \ge 0$, which is how the PLUTO paper defines a **polyhedron**.[^pluto] `N` appears in the inequalities but is never a loop variable: it is a **parameter**, a value fixed for the whole nest but unknown at compile time, so one description covers every size. (MLIR's affine dialect calls such values symbols, [M6](../mlir/m6-affine-and-scf.md#affine-maps-dims-and-symbols).)

`domain.cpp` writes the four rows down and checks them against the loop for six values of `N`, over a box larger than the triangle, so that a missing or wrong row would show up as a disagreement:

--8<-- "includes/examples/optimize/p9-polyhedral-model/domain.cpp.md"

Compare this with the matrix multiplication domain, `0 <= row, column, k < 64`. Each of its inequalities mentions one variable, so the domain is a **box**, and P7's matrices only ever reorder a box's axes. The triangle's row `i - j >= 0` mentions two variables at once. No renaming of `i` and `j` separates it into one bound per variable, because the inner loop's bound depends on the outer loop's variable. P7 had no words for that shape; the polyhedral model starts from it.

Not every loop has such a description. Verdoolaege's tutorial on the model lists what a program fragment needs: **static control flow**, meaning control that does not depend on input data, with every condition and loop bound affine in the outer loop variables and the parameters, and every loop step an integer constant.[^tut] A `while` loop that runs until an error is small enough fails the first test. A loop `for (i = 1; i <= n; i += i)` fails the last. A region of code that passes is a **static control part**, or SCoP, the unit Polly's documentation says it detects inside LLVM IR.[^polly]

??? check "Is `for (int i = 0; i < N; ++i) for (int j = i; j < N; ++j)` a box? Write its domain."

    Not a box: `j`'s lower bound depends on `i`. Its domain is `0 <= i <= j < N`, four inequalities, one of which, `j - i >= 0`, relates the two variables. It is the triangle above the diagonal instead of below it.

## Dependences as relations

P6 described a dependence by one distance vector. That works when every instance of a dependence has the same distance. In the model, a dependence is a **relation**: the set of all pairs (source point, target point) such that the source writes a location, the target reads or writes the same location, and the source runs first. The tutorial builds the flow dependences from three pieces: the write accesses, the read accesses, and the order the original program runs in, as the pairs of a write and a read that touch the same element, intersected with that order.[^tut]

Take a recurrence over the triangle, Pascal's triangle with missing neighbours read as zero: `t(0, 0) = 1`, and every other point adds `t(i - 1, j)` and `t(i - 1, j - 1)`, each only when that point lies in the triangle. Point `(i, j)` writes `t(i, j)` and reads the one or two points above it. The flow dependence from the first read is the relation

$$
\{\, (i - 1, j) \to (i, j) \;:\; 0 \le j \le i - 1,\; i \le N - 1 \,\}
$$

and the second read gives $\{(i - 1, j - 1) \to (i, j) : 1 \le j \le i \le N - 1\}$. Both are sets of integer points again, now in four dimensions, bounded by affine inequalities. Here every pair has the same distance, `(1, 0)` or `(1, 1)`, so P6's vectors would have sufficed. They stop sufficing when the distance varies from point to point, as in `a[i][j] = a[j][i] + 1` over a square. The read at `(i, j)` uses the element written at `(j, i)`; when `(j, i)` runs first, the distance is `(i - j, j - i)`, different for almost every pair, and no single vector describes the relation.

Deciding whether such a relation is empty is an integer programming question: does a system of affine equalities and inequalities have an integer solution? The GCD test of [P6](p6-dependence-analysis.md) answers a weaker question and gives up when it cannot decide. Pugh's Omega test answers it exactly, and fast enough to use in a compiler.[^pugh91]

isl, the **integer set library**, is the shared implementation much of the field now builds on: it manipulates sets and relations of integer points bounded by affine constraints, with operations that include intersection, emptiness checks and the lexicographic minimum.[^isl10] Its descriptions are **Presburger formulas**: affine comparisons of integers, with integer division by constants, joined by "and", "or", "not" and quantifiers.[^tut] Multiplying two variables is outside that language, which is the formal version of this book's affine-subscript rule.

### Memory-based and value-based

The relation above counts a pair whenever two accesses touch the same location. That is a **memory-based** dependence. Feautrier's array dataflow analysis computes something sharper: for each read, the one write whose value it receives, the last write to that location before the read.[^feau91][^tut] The tutorial calls these **value-based** dependences, because the value is preserved along them: a pair is dropped when another write to the same element lies in between.[^tut]

The difference matters wherever storage is reused. In Pascal's triangle every location is written once, so the two kinds agree. The stage 10 kernel's scalar `sum` is written again for every output element, and the two kinds disagree sharply there, as the kernel's walk-through later in this chapter shows.

??? check "A loop writes `x = f(i)` and then reads `x` in the same iteration, for `i` from 0 to 9. What memory-based dependences cross iterations, and what value-based ones?"

    Memory-based: an output dependence from every write to every later write, an anti dependence from each read to every later write, and a flow dependence from each write to every later read. Value-based: only the flow from the write of iteration `i` to the read of iteration `i`, since any later write in between kills the value. The value-based view shows that the iterations are independent once each gets its own copy of `x`.

## A schedule is an affine map

The nest's two `for` statements pick one order for the domain. The model separates that choice out. A **schedule** assigns each point a **logical time**, a vector of integers, and points run in lexicographic order of their times, compared entry by entry from the first, as P6 compared distance vectors. The loop as written is the schedule $\theta_0(i, j) = (i, j)$: increasing `i`, and within one `i`, increasing `j`. It is one choice. $\theta_1(i, j) = (i + j, j)$ is another, and it is also **affine**: each entry is a sum of the point's coordinates times integer constants, plus a constant.

`schedule.cpp` applies both schedules to a five-row triangle and sorts its fifteen points by their times:

--8<-- "includes/examples/optimize/p9-polyhedral-model/schedule.cpp.md"

$\theta_0$ reproduces the loop's order, since sorting by `(i, j)` is what the nested loops do. $\theta_1$ groups points by their **anti-diagonal**, the line where `i + j` is constant: `(2, 2)`, `(3, 1)` and `(4, 0)` all get first entry 4. Figure 1 draws both orders for a three-row triangle, with the recurrence's dependences as lines.

<figure class="vx-figure">
<svg viewBox="0 0 360 430" role="img" aria-label="A six-point triangular domain in loop order, and the same domain sheared by the schedule theta1" aria-describedby="p9-f1-desc">
<title id="p9-f1-title">A triangular domain sheared by an affine schedule</title>
<desc id="p9-f1-desc">Two panels show the six points of the domain 0 less than or equal to j less than or equal to i less than 3. The top panel arranges them as a triangle by row i and column j, with six lines for the recurrence's dependences: each point below the top row is joined to the one or two points above it that it reads. The bottom panel places the same six points and lines at column w equals i plus j and row c equals j, which shears the triangle into a slanted shape. Points (1, 1) and (2, 0) are highlighted: in the top panel they sit in different rows with no line between them; in the bottom panel they share the column w equals 2.</desc>
<text class="vx-text" x="20" y="30">Loop order, θ0(i, j) = (i, j)</text>
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
<text class="vx-text" x="20" y="262">Sheared, θ1(i, j) = (i + j, j)</text>
<text class="vx-text-muted" x="90" y="288" text-anchor="middle">w=0</text>
<text class="vx-text-muted" x="138" y="288" text-anchor="middle">w=1</text>
<text class="vx-text-muted" x="186" y="288" text-anchor="middle">w=2</text>
<text class="vx-text-muted" x="234" y="288" text-anchor="middle">w=3</text>
<text class="vx-text-muted" x="282" y="288" text-anchor="middle">w=4</text>
<text class="vx-text-muted" x="46" y="304" text-anchor="end">c=0</text>
<text class="vx-text-muted" x="46" y="352" text-anchor="end">c=1</text>
<text class="vx-text-muted" x="46" y="400" text-anchor="end">c=2</text>
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
<figcaption>Figure 1. The triangle 0 ≤ j ≤ i &lt; 3 with the recurrence's six dependences drawn as lines. Top: the loop's own order, row by row. Bottom: the same points placed by θ1, at column w = i + j. The two highlighted points, (1, 1) and (2, 0), sit in different rows at the top with no line between them, and share column w = 2 at the bottom. Every line crosses at least one column, so no two points in one column depend on each other.</figcaption>
</figure>

The first entry of $\theta_1$ is the **wave** of P7's wavefront example, where skewing turned anti-diagonals into columns ([P7](p7-loop-transformations.md#skewing-and-the-unimodular-view)). The domain here is a triangle, not P7's rectangle, and nothing in the schedule needed to change for that.

$\theta_1$ still has a second entry, `j`, because one entry does not fix a full order: two points with the same `i + j` would tie. The second entry breaks the tie. A schedule whose entries leave ties is allowed too; tied points may then run in any order, including at the same time, which is how the model expresses parallelism.

## Legality, generalized

P7's test asked for $T\,d$ to be lexicographically positive for every distance $d$.[^wl91] It assumed two things a schedule does without: that the transformation is a matrix, with no constant term, and that one distance vector describes each dependence everywhere. The general test keeps the shape and drops both. A schedule $\theta$ is **legal**, or valid, when every dependence runs forward in time:

$$
\theta(t) - \theta(s) \succ 0 \quad \text{for every pair } s \to t \text{ in every dependence relation,}
$$

where $\succ 0$ means lexicographically positive. In the tutorial's words, a valid schedule respects the dependences: every dependent pair is ordered by the schedule.[^tut] For a matrix $T$ and a uniform distance $d = t - s$, $\theta(t) - \theta(s) = T\,d$, so P7's test is the special case.

Walk through $\theta_1$ by hand on the Pascal recurrence. The first relation pairs $s = (i - 1, j)$ with $t = (i, j)$:

$$
\theta_1(t) - \theta_1(s) = \big(i + j - (i - 1 + j),\; j - j\big) = (1, 0).
$$

The second pairs $s = (i - 1, j - 1)$ with $t = (i, j)$, giving $(i + j - (i + j - 2),\; j - (j - 1)) = (2, 1)$. Both differences are the same at every pair, and both start with a positive entry, so $\theta_1$ is legal. Both first entries are positive, so every dependence crosses at least one wave: points in the same wave never depend on each other, which is what Figure 1 showed.

Now try $\theta_2(i, j) = (-i, j)$, which runs the rows bottom to top. The first relation gives $(-i - (-(i - 1)),\; 0) = (-1, 0)$: negative first entry, so $\theta_2$ is illegal, and the first pair checked, $(0, 0) \to (1, 0)$, is a witness. `legality.cpp` does the same by brute force: it lists all twenty dependent pairs of the five-row triangle and puts each schedule through the test.

--8<-- "includes/examples/optimize/p9-polyhedral-model/legality.cpp.md"

The checker does not know it is looking at a triangle, at Pascal's triangle, or at a sign flip. It needs the dependences and a candidate $\theta$, and it answers yes or no, naming a pair when the answer is no. A real tool cannot list the pairs, since $N$ is a parameter; it proves the inequality for all $N$ at once, with the technique in the section on finding a schedule.

??? check "Would $\theta_1$ still be legal if the recurrence also read `t(i - 1, j + 1)`, the point above and to the right?"

    No. That read adds pairs $s = (i - 1, j + 1)$, $t = (i, j)$, and $\theta_1(t) - \theta_1(s) = (i + j - (i + j),\; j - (j + 1)) = (0, -1)$. The first entry is zero and the second is negative, so the difference is lexicographically negative. Legality depends on the dependences, not on the domain's shape alone.

## Tiling asks for more than legality

A legal schedule runs every dependence forward in lexicographic order. Tiling asks for more. Cutting the schedule's dimensions into blocks and running one block after another reorders points across whole blocks, and that is safe only if no dependence points backward along any tiled dimension. The PLUTO paper states the condition per dimension: a schedule dimension $\phi$ is a legal **tiling hyperplane** when $\phi(t) - \phi(s) \ge 0$ for every dependent pair, a generalization of Irigoin and Triolet's classic condition for tiling one domain.[^pluto] A group of consecutive dimensions that all satisfy it is P7's fully permutable band.

P6's skewed stencil, `a[i][j] = a[i - 1][j + 1] + 1`, shows the gap. Its only dependence has distance `(1, -1)`. The schedule as written, $(i, j)$, gives difference $(1, -1)$: lexicographically positive, so legal, but the second entry is negative, so the band is not permutable. Skewing to $(i, i + j)$ gives $(1, 0)$: both entries non-negative. `tiling.cpp` checks three schedules on a 4 × 4 square, and then tiles each schedule's two dimensions by 2 and checks the tiled schedule directly:

--8<-- "includes/examples/optimize/p9-polyhedral-model/tiling.cpp.md"

The two tests agree on all three: the permutable schedule tiles, the others do not. Figure 2 shows why the schedule as written fails. Tiles run in the order numbered. The point `(0, 2)` lies in tile 2 and writes a value that `(1, 1)`, in tile 1, reads; but tile 1 has already finished. After skewing, every dependence points straight down its column, into the same tile or a later one.

<figure class="vx-figure">
<svg viewBox="0 0 500 612" role="img" aria-label="A 4 by 4 stencil domain cut into 2 by 2 tiles before and after skewing, with its dependences as arrows" aria-describedby="p9-f2-desc">
<title id="p9-f2-title">Why the stencil must be skewed before it is tiled</title>
<desc id="p9-f2-desc">Top panel: sixteen points of a 4 by 4 square, rows i from 0 to 3 and columns j from 0 to 3, cut into four 2 by 2 tiles numbered 1 to 4 in the order they run, left to right and then down. Nine arrows show the dependence from each point to the point one row down and one column left. Two arrows are dashed: from (0, 2) to (1, 1) and from (2, 2) to (3, 1). Each leaves a tile numbered 2 or 4 and enters the tile numbered one less, which has already run. Bottom panel: the same sixteen points placed at column w equals i plus j, so each row is shifted right by its row number. The space is cut into 2 by 2 tiles in rows i and columns w, numbered 1 to 6 in the order they run. Every arrow now points straight down, from a tile to itself or to a later tile.</desc>
<defs>
<marker id="p9-f2-head" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker>
</defs>
<text class="vx-text" x="20" y="22">As written, θ(i, j) = (i, j), in 2 × 2 tiles</text>
<text class="vx-text-muted" x="110" y="46" text-anchor="middle">j=0</text>
<text class="vx-text-muted" x="170" y="46" text-anchor="middle">j=1</text>
<text class="vx-text-muted" x="230" y="46" text-anchor="middle">j=2</text>
<text class="vx-text-muted" x="290" y="46" text-anchor="middle">j=3</text>
<rect class="vx-box" x="80" y="54" width="120" height="120"/>
<rect class="vx-box" x="200" y="54" width="120" height="120"/>
<rect class="vx-box" x="80" y="174" width="120" height="120"/>
<rect class="vx-box" x="200" y="174" width="120" height="120"/>
<text class="vx-text-muted" x="196" y="168" text-anchor="end">tile 1</text>
<text class="vx-text-muted" x="316" y="168" text-anchor="end">tile 2</text>
<text class="vx-text-muted" x="196" y="288" text-anchor="end">tile 3</text>
<text class="vx-text-muted" x="316" y="288" text-anchor="end">tile 4</text>
<text class="vx-text-muted" x="66" y="88" text-anchor="end">i=0</text>
<text class="vx-text-muted" x="66" y="148" text-anchor="end">i=1</text>
<text class="vx-text-muted" x="66" y="208" text-anchor="end">i=2</text>
<text class="vx-text-muted" x="66" y="268" text-anchor="end">i=3</text>
<line class="vx-line" x1="164" y1="90" x2="116" y2="138" marker-end="url(#p9-f2-head)"/>
<line class="vx-box-bad" x1="224" y1="90" x2="176" y2="138" marker-end="url(#p9-f2-head)"/>
<line class="vx-line" x1="284" y1="90" x2="236" y2="138" marker-end="url(#p9-f2-head)"/>
<line class="vx-line" x1="164" y1="150" x2="116" y2="198" marker-end="url(#p9-f2-head)"/>
<line class="vx-line" x1="224" y1="150" x2="176" y2="198" marker-end="url(#p9-f2-head)"/>
<line class="vx-line" x1="284" y1="150" x2="236" y2="198" marker-end="url(#p9-f2-head)"/>
<line class="vx-line" x1="164" y1="210" x2="116" y2="258" marker-end="url(#p9-f2-head)"/>
<line class="vx-box-bad" x1="224" y1="210" x2="176" y2="258" marker-end="url(#p9-f2-head)"/>
<line class="vx-line" x1="284" y1="210" x2="236" y2="258" marker-end="url(#p9-f2-head)"/>
<circle class="vx-box-strong" cx="110" cy="84" r="5"/>
<circle class="vx-box-strong" cx="170" cy="84" r="5"/>
<circle class="vx-box-strong" cx="230" cy="84" r="5"/>
<circle class="vx-box-strong" cx="290" cy="84" r="5"/>
<circle class="vx-box-strong" cx="110" cy="144" r="5"/>
<circle class="vx-box-strong" cx="170" cy="144" r="5"/>
<circle class="vx-box-strong" cx="230" cy="144" r="5"/>
<circle class="vx-box-strong" cx="290" cy="144" r="5"/>
<circle class="vx-box-strong" cx="110" cy="204" r="5"/>
<circle class="vx-box-strong" cx="170" cy="204" r="5"/>
<circle class="vx-box-strong" cx="230" cy="204" r="5"/>
<circle class="vx-box-strong" cx="290" cy="204" r="5"/>
<circle class="vx-box-strong" cx="110" cy="264" r="5"/>
<circle class="vx-box-strong" cx="170" cy="264" r="5"/>
<circle class="vx-box-strong" cx="230" cy="264" r="5"/>
<circle class="vx-box-strong" cx="290" cy="264" r="5"/>
<text class="vx-text-muted" x="340" y="110">dashed: leaves a</text>
<text class="vx-text-muted" x="340" y="128">tile that runs later</text>
<text class="vx-text" x="20" y="332">Skewed, θ(i, j) = (i, i + j), in 2 × 2 tiles</text>
<text class="vx-text-muted" x="110" y="356" text-anchor="middle">w=0</text>
<text class="vx-text-muted" x="160" y="356" text-anchor="middle">w=1</text>
<text class="vx-text-muted" x="210" y="356" text-anchor="middle">w=2</text>
<text class="vx-text-muted" x="260" y="356" text-anchor="middle">w=3</text>
<text class="vx-text-muted" x="310" y="356" text-anchor="middle">w=4</text>
<text class="vx-text-muted" x="360" y="356" text-anchor="middle">w=5</text>
<text class="vx-text-muted" x="410" y="356" text-anchor="middle">w=6</text>
<rect class="vx-box" x="85" y="364" width="100" height="120"/>
<rect class="vx-box" x="185" y="364" width="100" height="120"/>
<rect class="vx-box" x="285" y="364" width="100" height="120"/>
<rect class="vx-box" x="185" y="484" width="100" height="120"/>
<rect class="vx-box" x="285" y="484" width="100" height="120"/>
<rect class="vx-box" x="385" y="484" width="100" height="120"/>
<text class="vx-text-muted" x="181" y="478" text-anchor="end">tile 1</text>
<text class="vx-text-muted" x="281" y="478" text-anchor="end">tile 2</text>
<text class="vx-text-muted" x="381" y="478" text-anchor="end">tile 3</text>
<text class="vx-text-muted" x="281" y="598" text-anchor="end">tile 4</text>
<text class="vx-text-muted" x="381" y="598" text-anchor="end">tile 5</text>
<text class="vx-text-muted" x="481" y="598" text-anchor="end">tile 6</text>
<text class="vx-text-muted" x="66" y="398" text-anchor="end">i=0</text>
<text class="vx-text-muted" x="66" y="458" text-anchor="end">i=1</text>
<text class="vx-text-muted" x="66" y="518" text-anchor="end">i=2</text>
<text class="vx-text-muted" x="66" y="578" text-anchor="end">i=3</text>
<line class="vx-line" x1="160" y1="401" x2="160" y2="446" marker-end="url(#p9-f2-head)"/>
<line class="vx-line" x1="210" y1="401" x2="210" y2="446" marker-end="url(#p9-f2-head)"/>
<line class="vx-line" x1="260" y1="401" x2="260" y2="446" marker-end="url(#p9-f2-head)"/>
<line class="vx-line" x1="210" y1="461" x2="210" y2="506" marker-end="url(#p9-f2-head)"/>
<line class="vx-line" x1="260" y1="461" x2="260" y2="506" marker-end="url(#p9-f2-head)"/>
<line class="vx-line" x1="310" y1="461" x2="310" y2="506" marker-end="url(#p9-f2-head)"/>
<line class="vx-line" x1="260" y1="521" x2="260" y2="566" marker-end="url(#p9-f2-head)"/>
<line class="vx-line" x1="310" y1="521" x2="310" y2="566" marker-end="url(#p9-f2-head)"/>
<line class="vx-line" x1="360" y1="521" x2="360" y2="566" marker-end="url(#p9-f2-head)"/>
<circle class="vx-box-strong" cx="110" cy="394" r="5"/>
<circle class="vx-box-strong" cx="160" cy="394" r="5"/>
<circle class="vx-box-strong" cx="210" cy="394" r="5"/>
<circle class="vx-box-strong" cx="260" cy="394" r="5"/>
<circle class="vx-box-strong" cx="160" cy="454" r="5"/>
<circle class="vx-box-strong" cx="210" cy="454" r="5"/>
<circle class="vx-box-strong" cx="260" cy="454" r="5"/>
<circle class="vx-box-strong" cx="310" cy="454" r="5"/>
<circle class="vx-box-strong" cx="210" cy="514" r="5"/>
<circle class="vx-box-strong" cx="260" cy="514" r="5"/>
<circle class="vx-box-strong" cx="310" cy="514" r="5"/>
<circle class="vx-box-strong" cx="360" cy="514" r="5"/>
<circle class="vx-box-strong" cx="260" cy="574" r="5"/>
<circle class="vx-box-strong" cx="310" cy="574" r="5"/>
<circle class="vx-box-strong" cx="360" cy="574" r="5"/>
<circle class="vx-box-strong" cx="410" cy="574" r="5"/>
</svg>
<figcaption>Figure 2. P6's stencil, whose single dependence has distance (1, −1), on a 4 × 4 square. Top: tiled as written. The dashed arrows, (0, 2) to (1, 1) and (2, 2) to (3, 1), leave a tile and enter one that ran before it, so tiling breaks them. Bottom: the same points at column w = i + j. Every dependence now points straight down, and the tiles, run in the order numbered, respect all nine.</figcaption>
</figure>

??? check "A tiled schedule has two entries for each tiled dimension. Why can neither be dropped?"

    The outer entry, the tile number, keeps each tile together: without it, points of different tiles would interleave, which is the original order again. The inner entry orders the points inside one tile: without it, points of one tile would tie and could run in any order, which is legal only if no dependence stays inside a tile.

## From a schedule back to loops

A schedule is not code. To run the triangle in $\theta_1$'s order, a compiler has to write loops over the new time coordinates, `w = i + j` and `c = j`, with bounds that visit exactly the domain's points. Finding those bounds is **polyhedral scanning**, or **code generation**. Tools such as CLooG take domains and schedules, which PLUTO calls scattering functions, and produce loops that visit each domain in the schedules' order. CLooG knows nothing about dependences: whoever supplies the schedule must already have checked it.[^pluto]

Derive the bounds by hand. Substitute `i = w - c` and `j = c` into the four inequalities of the triangle:

- `i >= 0` becomes `w - c >= 0`, so `c <= w`.
- `i <= N - 1` becomes `c >= w - N + 1`.
- `j >= 0` becomes `c >= 0`.
- `i - j >= 0` becomes `w - 2c >= 0`, so `c <= w / 2`, rounded down.

The inner loop over `c` takes the largest lower bound and the smallest upper bound: `max(0, w - N + 1) <= c <= floor(w / 2)` (the bound `c <= w` is implied by `c <= w / 2` when `w >= 0`).

The outer loop's bounds come from eliminating `c`: every lower bound of `c` must be at most every upper bound. `0 <= w / 2` gives `w >= 0`, and `w - N + 1 <= w / 2` gives `w <= 2N - 2`. Pairing each lower bound with each upper bound in this way is **Fourier-Motzkin elimination**; a real generator also rounds each pair to integers carefully, which the example checks by brute force instead. `scan.cpp` runs the generated loops and compares them with a sort by $\theta_1$:

--8<-- "includes/examples/optimize/p9-polyhedral-model/scan.cpp.md"

Now a half-finished one. Interchange the triangle's loops, $\theta(i, j) = (j, i)$, so that `j` runs outside. The outer loop over `j` takes `0 <= j <= N - 1`. Work out the inner loop's bounds on `i` before opening the answer.

??? check "What loop nest visits `0 <= j <= i < N` with `j` outermost?"

    `for j in 0..N { for i in j..N { body(i, j) } }`. The inequalities that mention `i` are `i >= 0`, `i >= j` and `i <= N - 1`; for `j >= 0`, the largest lower bound is `j`. Eliminating `i` from `j <= i <= N - 1` leaves `j <= N - 1`, the outer bound. The interchange is legal only if the dependences allow it; for the Pascal recurrence, the pair $(i - 1, j - 1) \to (i, j)$ gives difference $(1, 1)$ and the pair $(i - 1, j) \to (i, j)$ gives $(0, 1)$, both positive, so it is.

## One representation for many transformations

Each transformation P7 named is a particular choice of schedule. For a two-deep nest over `(i, j)`:

| Transformation in P7 | Schedule $\theta(i, j)$ |
| --- | --- |
| None | $(i, j)$ |
| Interchange | $(j, i)$ |
| Reversal of the inner loop | $(i, -j)$ |
| Skewing the inner loop by the outer | $(i, f\,i + j)$ |
| Wavefront (skew, then interchange) | $(i + j, i)$ |
| Tiling by $T$ | $(\lfloor i/T \rfloor, \lfloor j/T \rfloor, i, j)$ |

Tiling needs integer division by a constant, which Presburger formulas allow.[^tut]

Transformations across statements need one more idea: each statement gets its own schedule, over its own domain. Two loops that run one after the other become two statements whose schedules start with a constant, 0 for the first loop and 1 for the second. **Fusion** gives them the same constant, so that their iterations interleave; **fission**, also called distribution, does the opposite. **Shifting** adds a constant to one statement's schedule so that its iteration `i` runs alongside the other's iteration `i + 1`. None of these needs a new legality proof: the test from the previous section applies to any $\theta$, however it was built. PLUTO's paper reports transformations found this way that combine shifting, fusion and skewing in one schedule.[^pluto]

The tutorial describes a common structure for such multi-statement schedules, the **schedule tree**: a **sequence** node runs its children one after another, and a **band** node orders instances by an affine partial schedule, compared lexicographically.[^tut] A sequence node is what the constant entries above encode, and a band is a group of loop dimensions. isl's scheduler marks the bands its Pluto-like algorithm builds as permutable,[^isl-man] the property Figure 2 turned on.

## Finding a schedule

Checking a schedule is the smaller half of the work. The model's second promise is that a compiler can search for one: treat the schedule's coefficients as unknowns, and ask for coefficients that make every dependence difference positive. The trouble is the phrase "for every pair". For the stencil the difference is a constant, but in general it depends on the point, and "non-negative at every point of a polyhedron" is not a linear constraint on the coefficients.

The **affine form of Farkas' lemma** turns it into one. An affine function is non-negative at every point of a non-empty polyhedron $\{x : a_k \cdot x + b_k \ge 0\}$ exactly when it can be written as $\lambda_0 + \sum_k \lambda_k (a_k \cdot x + b_k)$ with every $\lambda \ge 0$.

A tiny case: asking that $c\,i + d \ge 0$ for every `i` with $0 \le i \le N - 1$ is the same as asking for $\lambda_0, \lambda_1, \lambda_2 \ge 0$ with $c\,i + d = \lambda_0 + \lambda_1 i + \lambda_2 (N - 1 - i)$ for all `i`. Matching the coefficients of `i` and of the constants gives $c = \lambda_1 - \lambda_2$ and $d = \lambda_0 + \lambda_2 (N - 1)$: linear conditions on $c$, $d$ and the $\lambda$s, with `i` gone.

Feautrier's work on affine scheduling, one of the earliest polyhedral scheduling algorithms,[^tut] posed the search this way, with Farkas' lemma, and looked for schedules of minimum latency.[^feau92][^pluto] PLUTO keeps the machinery and changes the goal.[^pluto]

For each dependence it bounds $\phi(t) - \phi(s)$, the number of hyperplanes the dependence crosses, by an affine function of the parameters, and applies Farkas' lemma to that bound. It then asks for the lexicographically smallest solution, minimizing the bound first. A small bound means a short reuse distance when the dimension runs sequentially, and little communication when it runs in parallel. To keep the search tractable, PLUTO allows only non-negative coefficients, which, the paper notes, mainly excludes loop reversal. It finds one dimension at a time, each linearly independent of those already found, and each one satisfying the tiling condition, so the result is made of permutable bands ready to tile.

isl contains a scheduler of the same kind. Its manual calls the default "Pluto-like" and warns that it may fall back to a Feautrier-style step.[^isl-man] Polly's news page reported in 2011 that Polly could use the isl scheduler, "similar to the one in Pluto".[^polly]

Two things the model does not decide. It says nothing about which legal schedule is fast: PLUTO's objective is a proxy for locality and communication, not a measure of time on any machine, and choosing tile sizes still needs the cache facts of [P8](p8-cache-blocking.md). And a search over coefficients is a heavier piece of machinery than any single transformation in P7, which a compiler must then trust to produce loops as good as the ones it replaced. PLUTO's reported results are evidence that the approach works on real kernels, on the paper's machines; they are not numbers this book can reuse.

Figure 3 puts the stages together.

<figure class="vx-figure">
<svg viewBox="0 0 580 470" role="img" aria-label="The stages of a polyhedral compiler, from loop nest to reordered loop nest, with tools that implement each stage" aria-describedby="p9-f3-desc">
<title id="p9-f3-title">The stages of a polyhedral compiler</title>
<desc id="p9-f3-desc">Six boxes stacked from top to bottom and joined by arrows. First, a loop nest in source or IR. Second, extraction: domains, access relations and the original schedule, which needs static control flow; Polly detects such regions in LLVM IR. Third, dependence analysis: which pairs of instances must stay ordered, computed exactly; isl provides it. Fourth, scheduling: choosing a new affine schedule that is legal and good by some objective, as PLUTO and isl's scheduler do with integer programming and Farkas' lemma. Fifth, code generation: loops that scan each domain in the schedule's order, as CLooG and isl's AST generator do. Sixth, the loop nest again, reordered.</desc>
<defs>
<marker id="p9-f3-head" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker>
</defs>
<rect class="vx-box" x="20" y="16" width="280" height="50"/>
<text class="vx-text" x="36" y="46">Loop nest (source or IR)</text>
<rect class="vx-box-strong" x="20" y="92" width="280" height="56"/>
<text class="vx-text" x="36" y="114">Extract</text>
<text class="vx-text-muted" x="36" y="136">domains, accesses, original schedule</text>
<rect class="vx-box-strong" x="20" y="174" width="280" height="56"/>
<text class="vx-text" x="36" y="196">Dependence analysis</text>
<text class="vx-text-muted" x="36" y="218">pairs of instances that must stay ordered</text>
<rect class="vx-box-accent" x="20" y="256" width="280" height="56"/>
<text class="vx-text" x="36" y="278">Scheduling</text>
<text class="vx-text-muted" x="36" y="300">a new θ: legal, and good by an objective</text>
<rect class="vx-box-strong" x="20" y="338" width="280" height="56"/>
<text class="vx-text" x="36" y="360">Code generation</text>
<text class="vx-text-muted" x="36" y="382">loops that scan each domain in θ's order</text>
<rect class="vx-box" x="20" y="420" width="280" height="40"/>
<text class="vx-text" x="36" y="445">Loop nest, reordered</text>
<line class="vx-line" x1="160" y1="66" x2="160" y2="90" marker-end="url(#p9-f3-head)"/>
<line class="vx-line" x1="160" y1="148" x2="160" y2="172" marker-end="url(#p9-f3-head)"/>
<line class="vx-line" x1="160" y1="230" x2="160" y2="254" marker-end="url(#p9-f3-head)"/>
<line class="vx-line" x1="160" y1="312" x2="160" y2="336" marker-end="url(#p9-f3-head)"/>
<line class="vx-line" x1="160" y1="394" x2="160" y2="418" marker-end="url(#p9-f3-head)"/>
<text class="vx-text-muted" x="320" y="114">needs static control flow;</text>
<text class="vx-text-muted" x="320" y="132">Polly detects such regions</text>
<text class="vx-text-muted" x="320" y="196">exact, as integer sets;</text>
<text class="vx-text-muted" x="320" y="214">isl computes it</text>
<text class="vx-text-muted" x="320" y="278">PLUTO, isl's scheduler:</text>
<text class="vx-text-muted" x="320" y="296">integer programming, Farkas</text>
<text class="vx-text-muted" x="320" y="360">CLooG, isl's AST generator;</text>
<text class="vx-text-muted" x="320" y="378">no dependence knowledge</text>
</svg>
<figcaption>Figure 3. The stages of a polyhedral compiler. Scheduling is where the reordering is chosen; the stages around it translate between loops and sets. The notes on the right name tools this chapter cites for each stage.</figcaption>
</figure>

## Polyhedral compilers in practice

Polly is LLVM's polyhedral optimizer. Its documentation describes the flow of Figure 3 on LLVM IR: detect the loop kernels that fit, derive a mathematical model of their computations and memory accesses, optimize that model, and regenerate LLVM IR.[^polly]

It can run early in the pass pipeline, where the IR is still close to the source, or immediately before the vectorizer, after inlining; its documentation notes that by then earlier passes have often added scalar dependences that make Polly less effective.[^polly] Its home page says it performs classical loop transformations, especially tiling and fusion, and since 2017 detects generalized matrix multiplication and optimizes it into a form like expert-written GEMM code.[^polly] It is not part of every LLVM build: `opt --print-passes` and `opt --help-hidden` from LLVM 18.1.8 on the owner's M4 Pro list no Polly pass (checked on 2026-09-24).

A general-purpose front end makes extraction the hard stage. LLVM's own dependence analysis describes itself in its source as an incomplete implementation of Goff, Kennedy and Tseng's tests, and notes that because Clang linearizes some array subscripts, it relies on delinearization through `ScalarEvolution` to recover separate subscripts.[^llvm-da]

Aliasing is the other obstacle. If two arrays may overlap, every write to one may be a write to the other; in the worst case, the tutorial observes, every statement instance that writes may have to be assumed to write every element of every array, and the model stops being useful. It lists three approaches: assume there is no aliasing, require its absence (for example by extracting the model from a source language that does not permit it), or check for it at run time and fall back to the original code when it occurs.[^tut]

MLIR takes a different route: its affine dialect ([M6](../mlir/m6-affine-and-scf.md)) keeps loop bounds and subscripts affine by construction, so nothing has to be recovered.

## The stage 10 kernel in the model

Vortex is in MLIR's position, not Clang's. A Vortex array keeps its shape in its type and is indexed one dimension at a time ([P6](p6-dependence-analysis.md#vortexs-shapes-remove-a-whole-step)), so every subscript in the kernel is already an affine function of the loop variables. Build the model by hand for the kernel as P7 first showed it:

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

**Statements and domains.** There are three statements, at two depths. Call them S1, `sum = 0.0`; S2, `sum += a[row, k] * b[k, column]`; and S3, `c[row, column] = sum`. Writing `r` for `row` and `q` for `column`:

- S1 and S3 run once per pair: domain $\{(r, q) : 0 \le r, q < 64\}$.
- S2 runs once per triple: domain $\{(r, q, k) : 0 \le r, q, k < 64\}$.

**Accesses.** S2 reads `a` at `(r, k)` and `b` at `(k, q)`. S3 writes `c` at `(r, q)`. All three touch `sum`, a scalar: a single location, the same for every instance.

**Original schedule.** Inside one `(r, q)`, S1 runs first, then the `k` loop of S2, then S3. Constant entries encode that order:

$$
\theta(\mathrm{S1}(r, q)) = (r, q, 0, 0), \quad
\theta(\mathrm{S2}(r, q, k)) = (r, q, 1, k), \quad
\theta(\mathrm{S3}(r, q)) = (r, q, 2, 0).
$$

**Dependences.** Neither `a` nor `b` is written, so they add none. `c[r, q]` is written once per pair and never read, so it adds none either. Everything runs through `sum`.

Counted memory-based, `sum` links every instance to every later one: S3 of pair `(r, q)` reads `sum`, and S1 of every later pair overwrites it. Take S3 at `(0, 63)` and S1 at `(1, 0)`: the difference in the first two entries is $(1, -63)$. Interchange `r` and `q`, and it becomes $(-63, 1)$: illegal. Counted value-based, almost all of those pairs vanish, because each new `sum = 0.0` kills the old value. What remains stays inside one pair `(r, q)`: S1 to the first S2, each S2 to the next, $(r, q, k) \to (r, q, k + 1)$, and the last S2 to S3. Their differences in `r` and `q` are zero, so any order of `r` and `q` respects them.

The two views disagree about interchange because `sum` is one location reused for all 4,096 output elements. Giving each output element its own storage makes the memory-based dependences match the value-based ones, which is exactly what P7's preparation steps did by hand, returning the running sum to `c[row, column]` ([P7](p7-loop-transformations.md#making-the-stage-10-nest-perfect)).

The dependence that remains, $(r, q, k) \to (r, q, k + 1)$ through S2, is the floating-point accumulation. A schedule that respects it keeps the additions into one `sum` in increasing `k`, so decision 56 ([Numbers](../decisions/numbers.md#d56)) holds as long as the dependence is kept. A scheduler allowed to treat the accumulation as a reduction whose order does not matter, and so to drop this dependence, could regroup the `f32` sum and change its bits. A Vortex scheduler must never relax it.

The model also needs the three arrays not to overlap. Decision 25 ([References](../decisions/references.md#d25)) guarantees it for the array that is written: `c` is lent as `&mut`, so it appears in no other argument. `a` and `b` may be the same array, but both are only read, and two reads never make a dependence. Vortex fits the tutorial's second approach in its source-language form: the language's own rules exclude the aliasing that matters.[^tut]

??? check "S2 is scheduled with `k` outermost, $(k, r, q)$, with every S1 before all of it and every S3 after all of it. Is that legal?"

    It respects every value-based dependence: S2's own chain $(r, q, k) \to (r, q, k + 1)$ now has difference $(1, 0, 0)$, still positive, so the `k` order of each sum is kept, and each S1 still precedes and each S3 still follows its pair's S2 instances. It does not respect the memory-based ones. With a single `sum`, the 4,096 running sums would share one location and overwrite each other. A schedule must respect the dependences of the storage the program uses, so this one becomes legal only after each pair gets its own storage, as P7's preparation steps give it.

## For Vortex

!!! vortex "Exercise"

    **Build**, as a standalone tool alongside your compiler, an exporter and a schedule checker for the loop nests that [P6](p6-dependence-analysis.md#for-vortex) and [P7](p7-loop-transformations.md#for-vortex) already handle: constant bounds, affine subscripts, one or more statements per nest.

    1. **Export.** For each nest, print every statement's domain, every access as a relation from the statement's points to array elements (reads and writes separately), and the original schedule, with constant entries for statement order as in this chapter's kernel walk-through. Use isl's text notation, so that the output can be read by isl's `iscc` calculator (distributed with the barvinok package, per the isl site) if you install it.
    2. **Dependences.** Compute memory-based dependences between statement instances from the exported accesses and the original schedule, by enumeration over the constant bounds. Report each one as a relation, and where its distance is uniform, as the distance vector P6's pass reports.
    3. **Checker.** Read a candidate schedule, one affine map per statement given as integer coefficients and a constant, and report two verdicts: legal (every difference lexicographically positive) and, for each group of consecutive dimensions, fully permutable (every difference non-negative in each).
    4. **Remarks.** Name the schedule, the verdicts and, for each failure, the first dependent pair that fails, with its two statement instances and the difference.

    **Not yet:** value-based dependences, a schedule search, code generation from a schedule, and any use of the checker's verdict to transform your IR. Keep floating-point accumulations as ordinary dependences; there is no reduction relaxation to add.

    **Proof that it works:**

    - A golden export for the stage 10 kernel of this chapter, written by hand from the walk-through before you run the tool, and compared with the tool's output.
    - Agreement with P6: for every single-statement nest in P6's test suite, the uniform distances your tool reports match P6's pass.
    - Agreement with P7: for the interchange of a perfect nest, your checker's legality verdict matches the decision P7's pass makes, on the prepared kernel, where both call the interchange legal.
    - Four hand-derived verdicts: on the stage 10 kernel as written, the interchange of `row` and `column` is illegal under memory-based dependences, with the pair `S3(0, 63)`, `S1(1, 0)` or another pair of the same kind named in the remark; on P6's skewed stencil, $(i, j)$ is legal but not permutable, $(j, i)$ is illegal, and $(i, i + j)$ is legal and permutable.
    - If you install `iscc`: the dependences it computes from your export, restricted to the same bounds, equal your tool's.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is an iteration domain?** The set of integer points a statement runs at, bounded by affine inequalities in the loop variables and parameters, with no order attached.
    - **How does the model describe a dependence?** As a relation: every pair of instances that touch one location, at least one writing, with the source running first. Value-based dependences keep only pairs with no intervening write.
    - **When is a schedule legal?** When $\theta(t) - \theta(s)$ is lexicographically positive for every dependent pair; P7's $T\,d$ test is the case of a matrix and uniform distances.
    - **Why is legality not enough for tiling?** Tiles reorder whole blocks, so each tiled dimension needs $\phi(t) - \phi(s) \ge 0$ for every pair: a fully permutable band.
    - **What does code generation do?** Writes loops whose bounds, found by eliminating variables from the transformed inequalities, visit each domain in the schedule's order; it trusts the schedule to be legal.
    - **How does a scheduler turn "for every point" into linear constraints?** With the affine form of Farkas' lemma, which replaces the points by non-negative multipliers.
    - **Why does the stage 10 kernel fit the model without a recovery step?** Its bounds are constants, its subscripts affine and per dimension, and decision 25 rules out overlap with the one array it writes.

## Where this comes back

!!! next "You will use this again in"

    - [P10. Vectorization](p10-vectorization.md): *legal reordering*, *dimension with no dependence*
    - [P13. Multithreading](p13-multithreading.md): *parallel schedule dimension*, *wavefront*
    - [P14. Algorithms and schedules](p14-algorithms-and-schedules.md): *schedule separate from computation*
    - [P15. Choosing parameters: models or search](p15-choosing-parameters.md): *schedule search*, *objective*
    - [M6. Loops: affine and scf](../mlir/m6-affine-and-scf.md): *iteration domain*, *parameters as symbols*, *exact dependence*
    - [M9. Schedules as IR: the transform dialect](../mlir/m9-transform-dialect.md): *schedule as a value*, *band*

## Sources and further reading

Read Verdoolaege's tutorial first: it is free, defines every concept in this chapter precisely, and uses isl throughout. Then read the PLUTO paper for a complete scheduler, from Farkas' lemma to tiled code. Feautrier's two papers are the origin of exact dataflow analysis and affine scheduling. For the model inside a production compiler, read Polly's architecture page and paper.

[^tut]: Sven Verdoolaege, "Presburger Formulas and Polyhedral Compilation", version 0.02, 2021: Definition 3.14; sections 5.1, 5.2.2, 5.3.2, 5.4 (Definition 5.35 and note 5.9), 5.6.1 and 6.1; notes 5.25 and 6.1. <https://libisl.sourceforge.io/tutorial.pdf>
[^pluto]: Uday Bondhugula, Albert Hartono, J. Ramanujam and P. Sadayappan, "A Practical Automatic Polyhedral Parallelizer and Locality Optimizer", *Proceedings of the ACM SIGPLAN 2008 Conference on Programming Language Design and Implementation (PLDI)*, 2008: sections 1, 2.1, 3.1 (Lemma 1), 3.2, 4 and 5. <https://doi.org/10.1145/1375581.1375595> (author's copy: <https://www.csa.iisc.ac.in/~udayb/publications/uday-pldi08.pdf>)
[^feau91]: Paul Feautrier, "Dataflow Analysis of Array and Scalar References", *International Journal of Parallel Programming* 20(1), 1991. <https://doi.org/10.1007/BF01407931>
[^feau92]: Paul Feautrier, "Some Efficient Solutions to the Affine Scheduling Problem, Part I: One-Dimensional Time", *International Journal of Parallel Programming* 21(5), 1992. <https://doi.org/10.1007/BF01407835>
[^pugh91]: William Pugh, "The Omega Test: A Fast and Practical Integer Programming Algorithm for Dependence Analysis", *Proceedings of Supercomputing '91*, 1991. <https://doi.org/10.1145/125826.125848>
[^isl10]: Sven Verdoolaege, "isl: An Integer Set Library for the Polyhedral Model", *Mathematical Software (ICMS 2010)*, Lecture Notes in Computer Science, 2010. <https://doi.org/10.1007/978-3-642-15582-6_49> (isl site, with the list of supported operations: <https://libisl.sourceforge.io/>)
[^isl-man]: isl user manual, sections 1.5.2 (dependence analysis) and 1.5.3 (scheduling), and the description of band nodes' permutable property. <https://libisl.sourceforge.io/manual.pdf>
[^polly]: The Polly Project: home page (overview and news, 2011 to 2017) <https://polly.llvm.org/> and "The Architecture" <https://polly.llvm.org/docs/Architecture.html>; Tobias Grosser, Armin Größlinger and Christian Lengauer, "Polly: Performing Polyhedral Optimizations on a Low-Level Intermediate Representation", *Parallel Processing Letters* 22(4), 2012. <https://doi.org/10.1142/S0129626412500107>
[^wl91]: Michael E. Wolf and Monica S. Lam, "A Data Locality Optimizing Algorithm", *Proceedings of the ACM SIGPLAN 1991 Conference on Programming Language Design and Implementation (PLDI)*, 1991. <https://doi.org/10.1145/113445.113449> (free copy: <https://suif.stanford.edu/papers/wolf91a.pdf>)
[^llvm-da]: LLVM Project, `DependenceAnalysis.cpp`, release/18.x branch: the file header. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Analysis/DependenceAnalysis.cpp>
