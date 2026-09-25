# P6. Dependence analysis

<p class="page-intro">Before a compiler swaps, splits or reorders a loop's iterations, it has to know which of them depend on which. This chapter builds that knowledge: flow, anti and output dependence, distance and direction vectors, the rule that says when loops may be reordered, and the subscript tests that compute it all. Vortex keeps an array's shape in its type, which lets its compiler skip a step that LLVM's analysis still has to take.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 45 minutes · Builds on: [O8. Loops: structure, induction variables and bounds checks](o8-loops.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is an add recurrence, and what did O8 write for the address of `a[row, k]` in the kernel's `k` loop?"

        {start,+,step}&lt;L&gt;: a value that is `start` in the first iteration of loop L and grows by `step` in each later one. O8 wrote the address of `a[row, k]` as {A + 256·row,+,4}&lt;k&gt;.

        Introduced in [O8. Loops: structure, induction variables and bounds checks](o8-loops.md#add-recurrences).

    ??? question "What is a loop's backedge-taken count, and how does it relate to the range of the loop's counter?"

        The number of times a back edge is taken, one less than the number of times the header runs. For a counter that starts at L and steps by 1, it is U − L when the last value is U: the width of the range a dependence test compares distances against.

        Introduced in [O8. Loops: structure, induction variables and bounds checks](o8-loops.md#counting-iterations).

    ??? question "In LLVM's definition, how can two loops of one function relate to each other?"

        They share no block, or one contains the other. A function's loops therefore form a forest, and the outer loop's header dominates the inner loop's header.

        Introduced in [O2. Control-flow graphs and dominance](o2-cfg-and-dominance.md#loops-from-dominance).

    ??? question "Why do a Vortex program's runtime checks fence in any transformation that reorders loop iterations?"

        A failed check is observable: it writes one error line and stops the program. Reordering iterations can make a different check fail first, or run a check the original order never reached. O8 and P7 therefore transform only nests whose checks are proven unable to fail.

        Introduced in [O8. Loops: structure, induction variables and bounds checks](o8-loops.md#why-the-checks-matter-for-the-vectorizer).

!!! goals "In this chapter"

    - Classify a dependence between two array accesses as flow, anti or output, and name the loop that carries it.
    - Compute distance and direction vectors for an affine loop nest, by hand and by brute force.
    - Decide from its dependence vectors whether a permutation of a loop nest is legal.
    - Apply the ZIV, SIV and MIV classification, the strong SIV test and the GCD test, and say what each proves and what it leaves open.
    - Explain why LLVM's dependence analysis must recover subscripts that a Vortex compiler can keep, and what makes testing one dimension at a time sound.

## Why reordering iterations needs a proof

Here is a loop that turns an array of zeros into a count. `a` holds five zeros, and for `i` from 1 to 4:

```text
a[i] = a[i - 1] + 1
```

Run in order, it leaves 0, 1, 2, 3, 4: each element is one more than the one before it.

Now suppose a compiler ran the iterations all at once, the way four vector lanes or four threads might: every iteration reads its right-hand side from the array as it was before the loop, then every iteration writes. `a[1]` still comes out right, 0 + 1, because nothing writes `a[0]`. But `a[2]` reads the old `a[1]`, which is 0, not the 1 that iteration 1 produces, and becomes 1. Every later element makes the same mistake, and the array ends 0, 1, 1, 1, 1.

Iteration `i` needs the value that iteration `i − 1` wrote. Goff, Kennedy and Tseng define the relation this way: a **dependence** runs from one statement to another when execution can reach the second from the first and both touch the same memory location.[^gkt91] It constrains a compiler when at least one of the two accesses writes, because then swapping them changes what is read or what is left behind.

Before a compiler interchanges two loops, cuts one into tiles, or runs iterations on several threads or vector lanes, it must know every such pair, because those are exactly the pairs whose order it must keep. Everything else is free to move. [O8](o8-loops.md) gave a compiler the facts it needs about one loop: its shape, its induction variables, how many times it runs. This chapter adds what the loop's body does to memory across iterations. [O9](o9-alias-analysis.md) asks whether two different references can reach the same array; here both accesses are to one array, so the only question is which elements their subscripts reach. [P7](p7-loop-transformations.md) turns "legal" into transformations.

## Naming what changes: flow, anti and output dependence

The running count reads a location that an earlier iteration wrote: iteration `i` reads `a[i − 1]`, which iteration `i − 1` wrote. That is a **flow dependence**, also called a true dependence or read after write: a later access reads what an earlier one wrote.[^gkt91]

A dependence can run the other way. This loop shifts an array one place to the left, in place:

```text
for i in 0..n - 1 {
    a[i] = a[i + 1]
}
```

Iteration `i` reads `a[i + 1]`, and iteration `i + 1` then writes it. Run iteration `i + 1` first, and the read picks up the new value instead of the old one the shift is supposed to move. This is an **anti dependence**, or write after read: a later access writes what an earlier one read.

The third kind involves no read. In this loop both statements write, and the second writes one place ahead:

```text
for i in 0..n - 1 {
    a[i] = f(i)
    a[i + 1] = g(i)
}
```

The second statement of iteration 2 writes `a[3]`, and the first statement of iteration 3 overwrites it. Nothing reads between the two writes, yet their order decides which value survives: run in order, `a[3]` ends as `f(3)`. This is an **output dependence**, or write after write. Goff, Kennedy and Tseng list a fourth kind, the **input dependence**, where both accesses read.[^gkt91] It imposes no order; Bacon, Graham and Sharp observe that a compiler may still record it when deciding where data should live.[^bgs94]

<figure class="vx-figure">
<svg viewBox="0 0 720 260" role="img" aria-label="Three kinds of dependence, each drawn as two iterations one above the other, time running downward. Flow, for a[i] = a[i - 1] + 1: iteration 2 writes a[2], and iteration 3 reads it; write, then read. Anti, for a[i] = a[i + 1]: iteration 2 reads a[3], and iteration 3 writes it; read, then write. Output, for a[i] = f(i) followed by a[i + 1] = g(i): the second statement of iteration 2 writes a[3], and the first statement of iteration 3 writes it again; write, then write. Each arrow runs from the access that must happen first to the one that must happen second.">
<defs><marker id="p6-f1-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text-accent" x="120" y="24" text-anchor="middle">Flow</text>
<text class="vx-text-muted" x="120" y="46" text-anchor="middle">a[i] = a[i - 1] + 1</text>
<rect class="vx-box" x="15" y="62" width="210" height="50" rx="6"/>
<text class="vx-text" x="120" y="82" text-anchor="middle">iteration 2</text>
<text class="vx-text-muted" x="120" y="102" text-anchor="middle">writes a[2]</text>
<line class="vx-line" x1="120" y1="112" x2="120" y2="164" marker-end="url(#p6-f1-head)"/>
<text class="vx-text-muted" x="128" y="143">write, then read</text>
<rect class="vx-box" x="15" y="168" width="210" height="50" rx="6"/>
<text class="vx-text" x="120" y="188" text-anchor="middle">iteration 3</text>
<text class="vx-text-muted" x="120" y="208" text-anchor="middle">reads a[2]</text>
<text class="vx-text-accent" x="360" y="24" text-anchor="middle">Anti</text>
<text class="vx-text-muted" x="360" y="46" text-anchor="middle">a[i] = a[i + 1]</text>
<rect class="vx-box" x="255" y="62" width="210" height="50" rx="6"/>
<text class="vx-text" x="360" y="82" text-anchor="middle">iteration 2</text>
<text class="vx-text-muted" x="360" y="102" text-anchor="middle">reads a[3]</text>
<line class="vx-line" x1="360" y1="112" x2="360" y2="164" marker-end="url(#p6-f1-head)"/>
<text class="vx-text-muted" x="368" y="143">read, then write</text>
<rect class="vx-box" x="255" y="168" width="210" height="50" rx="6"/>
<text class="vx-text" x="360" y="188" text-anchor="middle">iteration 3</text>
<text class="vx-text-muted" x="360" y="208" text-anchor="middle">writes a[3]</text>
<text class="vx-text-accent" x="600" y="24" text-anchor="middle">Output</text>
<text class="vx-text-muted" x="600" y="46" text-anchor="middle">a[i] = f(i); a[i + 1] = g(i)</text>
<rect class="vx-box" x="495" y="62" width="210" height="50" rx="6"/>
<text class="vx-text" x="600" y="82" text-anchor="middle">iteration 2</text>
<text class="vx-text-muted" x="600" y="102" text-anchor="middle">second line writes a[3]</text>
<line class="vx-line" x1="600" y1="112" x2="600" y2="164" marker-end="url(#p6-f1-head)"/>
<text class="vx-text-muted" x="608" y="143">write, then write</text>
<rect class="vx-box" x="495" y="168" width="210" height="50" rx="6"/>
<text class="vx-text" x="600" y="188" text-anchor="middle">iteration 3</text>
<text class="vx-text-muted" x="600" y="208" text-anchor="middle">first line writes a[3]</text>
<text class="vx-text-muted" x="20" y="245">time runs downward; the arrow is the order a transformation must keep</text>
</svg>
<figcaption>Figure 1. The three kinds of dependence that fix an order, each drawn as the pair of iterations that creates it, with time running downward. Only the flow dependence passes a value from one access to the other; the anti and output dependences share a location without passing a value.</figcaption>
</figure>

Only a flow dependence carries a value. Anti and output dependences come from reusing a location, and Bacon, Graham and Sharp point out that giving the later write its own storage can let the two accesses run concurrently.[^bgs94] A flow dependence cannot be removed that way, because the reader needs the value. As long as a dependence of any kind exists, it fixes an order.

??? check "In `for i in 1..n { b[i] = a[i - 1]; a[i] = c[i] }`, what kind of dependence joins the two statements, and which is its source?"

    A flow dependence whose source is the second statement: iteration `i − 1` writes `a[i − 1]` there, and iteration `i` reads it in the first statement. The dependence runs backward in the text but forward in time. Kinds are defined by the order in which the accesses execute, not by the order of the statements on the page.

## Loop-carried and loop-independent dependence

A dependence between two accesses in the same iteration, such as a value computed by one statement and used by the next, is **loop-independent**. One that joins different iterations, like every example so far, is **loop-carried**.[^bgs94] A transformation that only reorders whole iterations cannot break a loop-independent dependence: both ends stay in one iteration, in their original order. It can break a loop-carried one.

In a nest, a loop-carried dependence joins iterations that differ in some loop indices and agree in others. Goff, Kennedy and Tseng say the dependence is carried by the outermost loop whose index differs between the two ends; call it the **carrying loop**.[^gkt91] The two ends run in different iterations of that loop and in the same iteration of every loop outside it, so the carrying loop is what orders them. They add that carried dependences decide which loops cannot run in parallel without synchronization.[^gkt91] Once the loops outside it are fixed, a loop that carries no dependence may run its iterations in any order, or all at once.

Scalars have dependences too. Here is the kernel from [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for), as written:

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

Every trip around the `k` loop reads `sum` and writes it: a flow, an anti and an output dependence on one variable, carried by `k`. In SSA form ([O3](o3-ssa.md#your-turn-the-kernel-in-ssa-form)) that is the phi at the head of the `k` loop, and a compiler finds it from the phi with no subscript analysis at all. The array accesses have no dependences: `a` and `b` are only read, and each element of `c` is written once. What follows is about array accesses, where finding the dependences takes work.

## The iteration space and its dependences

A nest `d` loops deep runs its body once for each tuple of index values $(i_1, \dots, i_d)$. The set of those tuples is the nest's **iteration space**. For loops over fixed ranges, as every loop in this chapter is, it is a box of integer points: a nest whose two loops each run over four values has sixteen.

Sequential execution visits the points in **lexicographic order**: the outermost loop changes slowest, so $(i_1, \dots, i_d)$ runs before $(i'_1, \dots, i'_d)$ exactly when, at the first position where the tuples differ, the first tuple has the smaller value. $(0, 3)$ runs before $(1, 0)$: they first differ in position one, and $0 < 1$.

O8 wrote an address in one loop as an add recurrence. A subscript in a nest generalizes it. An **affine subscript** is an integer constant plus a constant multiple of each loop index:

$$
f(\vec i) = a_0 + a_1 i_1 + a_2 i_2 + \cdots + a_d i_d
$$

`row`, `k + 1` and `2i − j` are affine; `i * j`, `i % 2` and `idx[i]` are not. Goff, Kennedy and Tseng call these subscripts linear, and let the constant term contain loop-invariant symbols such as `n`.[^gkt91]

Take two accesses to one array, one in iteration $\vec\alpha$ and one in iteration $\vec\beta$. There is a dependence from the first to the second exactly when some $\vec\alpha$ and $\vec\beta$ inside the loop bounds, with $\vec\alpha$ no later than $\vec\beta$, make the subscripts agree in every dimension $k$:[^gkt91]

$$
f_k(\vec\alpha) = g_k(\vec\beta)
$$

These are the **dependence equations**. The rest of this chapter is about deciding whether they have an integer solution inside the box, and describing the solutions when they do.

## Distance and direction vectors

When iterations $\vec\alpha$ and $\vec\beta$ solve the equations, the **distance vector** is $\vec\beta - \vec\alpha$, one entry per loop from the outermost in. The **direction vector** keeps only each entry's sign: `<` when the second iteration's index is larger (a positive distance), `=` when it is the same, and `>` when it is smaller.[^gkt91] A **stencil** computes each element from its neighbours; the stencil `g[i, j] = g[i − 1, j + 1] + 1` has distance $(1, −1)$ and direction $(<, >)$: the reading iteration is one row later and one column earlier than the writing one.

Because $\vec\alpha$ runs first, the first nonzero entry of a distance vector is positive. A vector with that property is **lexicographically positive**; later entries may be negative, as the stencil's is. Bacon, Graham and Sharp explain why the first entry cannot be negative: it would describe a dependence on an iteration that has not run yet.[^bgs94] The carrying loop is the position of the first nonzero entry, and a vector of zeros is a loop-independent dependence.

A dependence vector relates iterations, not array elements.[^bgs94] Its distance need not be constant either. A write to `a[2i]` and a read of `a[i]` meet whenever the read's iteration is twice the write's, so the distance grows with `i`. A direction vector still describes such a dependence. When a test cannot pin an entry down, it reports the set of directions that remain possible, and `*` stands for all three.[^bgs94]

The first example computes all of this by brute force, for two nests small enough to enumerate. It runs every iteration, records the element each access touches, compares every earlier iteration with every later one, and prints each distinct kind and distance with its carrying loop. The first nest is the kernel's multiply-accumulate with `sum` replaced by `c[row, column]` itself, the form [P7](p7-loop-transformations.md#making-the-stage-10-nest-perfect) arrives at; the second is the stencil.

--8<-- "includes/examples/optimize/p6-dependence-analysis/brute_force_dependence.cpp.md"

The reads of `a` and `b` produce nothing, because two reads never conflict. Every dependence of the matrix product is on `c`, with distance $(0, 0, 1)$ or $(0, 0, 2)$ when each loop runs three times, and every one is carried by `k`. The longer distance is the shorter one followed twice, which is why real analyses summarize instead of listing pairs. The stencil's nine instances all have distance $(1, −1)$, carried by `i`. The lines headed `order` belong to the next section.

### The matrix product by hand

A test does by algebra what the program did by enumeration. Take `c[row, column] += a[row, k] * b[k, column]` over loops `(row, column, k)`. Write $r$ for `row` and $s$ for `column`. The write to `c` in iteration $(r, s, k)$ and the read of `c` in iteration $(r', s', k')$ touch the same element when

$$
r = r', \qquad s = s'
$$

Nothing constrains $k$ and $k'$, so every two iterations of one `(row, column)` pair's `k` loop are related, at distance $(0, 0, k' - k)$. With the earlier iteration as the source, $k' > k$, so the direction is $(=, =, <)$ and `k` carries the dependence. The read against a later write, and the write against a later write, give the same equations: the anti and output dependences. `a` and `b` are only read.

So `row` and `column` carry nothing, and their iterations may run in parallel, one of the facts [P13](p13-multithreading.md) uses. `k` carries every dependence: its iterations must run in order, unless the compiler may change the order of the additions into `c[row, column]` ([P10](p10-vectorization.md)).

??? check "The first example reports that `i` carries the stencil's dependence. May the iterations of either loop run in parallel, and why?"

    Only `j`'s. The vector (1, −1) has its first nonzero entry on `i`, so the iterations of `i` must stay in order. `j` carries nothing: within one iteration of `i`, the iterations of `j` may run at once, because each reads only the row above, which the previous iteration of `i` has finished.

## When a loop permutation is legal

Interchanging a nest's loops, tiling it and running its iterations at once all ask one question: does the new order still run the source of every dependence before its sink? For a permutation of the loops, the answer is mechanical. The new nest still runs its iterations in lexicographic order, now of the index tuple rearranged to match the new nesting, so rearrange each distance vector the same way. The permutation is legal exactly when every rearranged vector is still lexicographically positive. Bacon, Graham and Sharp state the rule for swapping two loops, and add that a nest of two loops can be interchanged unless some dependence has direction $(<, >)$.[^bgs94]

The matrix product passes in every order. Its only dependence vector has the form $(0, 0, +)$, one nonzero entry, and moving that entry never changes its sign. All six orders of `row`, `column` and `k` are legal, and the `order` lines of the first example try each one. That freedom is what [P7](p7-loop-transformations.md) and [P8](p8-cache-blocking.md) spend on locality. It also respects strict floating point ([decision 56](../decisions/numbers.md#d56)): the dependence on `c[row, column]` is what fixes the order of that element's additions, so an order that keeps the dependence keeps each element's sum bit for bit.

The stencil fails. Its vector $(1, −1)$ in `(i, j)` order becomes $(−1, 1)$ in `(j, i)` order, and a negative first entry means the reading iteration would now run before the writing one. Figure 2 shows it on the grid.

<figure class="vx-figure">
<div class="vx-stepper">
<div class="vx-step">
<p><strong>Step 1. As written: i outside, j inside.</strong> The numbers give the order in which the iterations run, row by row. Every arrow runs from the iteration that writes an element to the one that reads it, and every arrow goes from a smaller number to a larger one.</p>
<svg viewBox="0 0 700 330" role="img" aria-label="A 3 by 3 grid of iterations with i from 0 to 2 downward and j from 0 to 2 rightward, numbered 0 to 8 row by row. Four arrows show the stencil's flow dependence, each from an iteration to the one a row down and a column to the left: from 1 to 3, from 2 to 4, from 4 to 6 and from 5 to 7. Every arrow runs from a smaller number to a larger one, so every write happens before its read.">
<defs>
<marker id="p6-f2a-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker>
</defs>
<text class="vx-text-muted" x="140" y="30" text-anchor="middle">j = 0</text>
<text class="vx-text-muted" x="340" y="30" text-anchor="middle">j = 1</text>
<text class="vx-text-muted" x="540" y="30" text-anchor="middle">j = 2</text>
<text class="vx-text-muted" x="40" y="75" text-anchor="middle">i = 0</text>
<text class="vx-text-muted" x="40" y="170" text-anchor="middle">i = 1</text>
<text class="vx-text-muted" x="40" y="265" text-anchor="middle">i = 2</text>
<path class="vx-flow" d="M328 82 L164 152" marker-end="url(#p6-f2a-head)"/>
<path class="vx-flow" d="M528 82 L364 152" marker-end="url(#p6-f2a-head)"/>
<path class="vx-flow" d="M328 177 L164 247" marker-end="url(#p6-f2a-head)"/>
<path class="vx-flow" d="M528 177 L364 247" marker-end="url(#p6-f2a-head)"/>
<circle class="vx-dot" cx="140" cy="70" r="8"/><text class="vx-text" x="140" y="52" text-anchor="middle">0</text>
<circle class="vx-dot" cx="340" cy="70" r="8"/><text class="vx-text" x="340" y="52" text-anchor="middle">1</text>
<circle class="vx-dot" cx="540" cy="70" r="8"/><text class="vx-text" x="540" y="52" text-anchor="middle">2</text>
<circle class="vx-dot" cx="140" cy="165" r="8"/><text class="vx-text" x="140" y="147" text-anchor="middle">3</text>
<circle class="vx-dot" cx="340" cy="165" r="8"/><text class="vx-text" x="340" y="147" text-anchor="middle">4</text>
<circle class="vx-dot" cx="540" cy="165" r="8"/><text class="vx-text" x="540" y="147" text-anchor="middle">5</text>
<circle class="vx-dot" cx="140" cy="260" r="8"/><text class="vx-text" x="140" y="242" text-anchor="middle">6</text>
<circle class="vx-dot" cx="340" cy="260" r="8"/><text class="vx-text" x="340" y="242" text-anchor="middle">7</text>
<circle class="vx-dot" cx="540" cy="260" r="8"/><text class="vx-text" x="540" y="242" text-anchor="middle">8</text>
<text class="vx-text-accent" x="40" y="305">every arrow runs from a smaller number to a larger one: legal</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 2. Interchanged: j outside, i inside.</strong> The same iterations and the same four dependences, numbered column by column. Every arrow now runs from a larger number to a smaller one: each read would run before the write it depends on, so the interchange is illegal.</p>
<svg viewBox="0 0 700 330" role="img" aria-label="The same 3 by 3 grid, numbered column by column: column j = 0 holds 0, 1, 2 from top to bottom, column j = 1 holds 3, 4, 5, and column j = 2 holds 6, 7, 8. The same four dependence arrows are drawn dashed: from 3 to 1, from 6 to 4, from 4 to 2 and from 7 to 5. Every arrow runs from a larger number to a smaller one, so every read would happen before its write.">
<defs>
<marker id="p6-f2b-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker>
</defs>
<text class="vx-text-muted" x="140" y="30" text-anchor="middle">j = 0</text>
<text class="vx-text-muted" x="340" y="30" text-anchor="middle">j = 1</text>
<text class="vx-text-muted" x="540" y="30" text-anchor="middle">j = 2</text>
<text class="vx-text-muted" x="40" y="75" text-anchor="middle">i = 0</text>
<text class="vx-text-muted" x="40" y="170" text-anchor="middle">i = 1</text>
<text class="vx-text-muted" x="40" y="265" text-anchor="middle">i = 2</text>
<path class="vx-box-bad" d="M328 82 L164 152" marker-end="url(#p6-f2b-head)"/>
<path class="vx-box-bad" d="M528 82 L364 152" marker-end="url(#p6-f2b-head)"/>
<path class="vx-box-bad" d="M328 177 L164 247" marker-end="url(#p6-f2b-head)"/>
<path class="vx-box-bad" d="M528 177 L364 247" marker-end="url(#p6-f2b-head)"/>
<circle class="vx-dot" cx="140" cy="70" r="8"/><text class="vx-text" x="140" y="52" text-anchor="middle">0</text>
<circle class="vx-dot" cx="340" cy="70" r="8"/><text class="vx-text" x="340" y="52" text-anchor="middle">3</text>
<circle class="vx-dot" cx="540" cy="70" r="8"/><text class="vx-text" x="540" y="52" text-anchor="middle">6</text>
<circle class="vx-dot" cx="140" cy="165" r="8"/><text class="vx-text" x="140" y="147" text-anchor="middle">1</text>
<circle class="vx-dot" cx="340" cy="165" r="8"/><text class="vx-text" x="340" y="147" text-anchor="middle">4</text>
<circle class="vx-dot" cx="540" cy="165" r="8"/><text class="vx-text" x="540" y="147" text-anchor="middle">7</text>
<circle class="vx-dot" cx="140" cy="260" r="8"/><text class="vx-text" x="140" y="242" text-anchor="middle">2</text>
<circle class="vx-dot" cx="340" cy="260" r="8"/><text class="vx-text" x="340" y="242" text-anchor="middle">5</text>
<circle class="vx-dot" cx="540" cy="260" r="8"/><text class="vx-text" x="540" y="242" text-anchor="middle">8</text>
<text class="vx-text-accent" x="40" y="305">every arrow runs from a larger number to a smaller one: illegal</text>
</svg>
</div>
</div>
<figcaption>Figure 2. The stencil's iteration space, 3 by 3, in its original loop order and interchanged. The points and the four dependences are the same in both panels. Only the numbering changes, and with it which end of each arrow runs first.</figcaption>
</figure>

## Deciding whether a dependence exists

Distance vectors describe a dependence once it is known to exist. Deciding whether the dependence equations have a solution inside the loop bounds is the hard part. For affine subscripts it means finding integer solutions to a system of linear equations, which is NP-complete in general.[^gkt91] [^bgs94] Compilers therefore chain cheap tests that settle the common cases. When none settles a case, they assume the dependence exists: a missed optimization costs speed, while reordering across a real dependence costs a wrong answer. Goff, Kennedy and Tseng require exactly this: a test must be **conservative** and assume any dependence it cannot disprove. They call a test **exact** when it reports a dependence if and only if one exists.[^gkt91]

Their scheme sorts each **subscript**, meaning one dimension of a pair of accesses, by how many loop indices it mentions.[^gkt91] A **ZIV** subscript (zero index variables) mentions none, as in `x[5]` against `x[n]`. An **SIV** subscript (single index variable) mentions one, as in `x[i + 1]` against `x[i]`. An **MIV** subscript (multiple index variables) mentions more, as in `x[i + j]` against `x[i]`. The classification is per dimension: `x[2, i, i + j]` against `x[m, i - 1, k]` has one of each.

The ZIV test compares two loop-invariant values. If they are provably different, no dependence exists; if not, that dimension adds no constraint.[^gkt91] LLVM's version returns one of three answers: provably equal, provably different, or possibly equal, in which case it assumes a dependence.[^llvm-da]

A second property decides how the per-dimension answers combine. A subscript is **separable** when its indices appear in no other dimension, and two dimensions that share an index are **coupled**.[^gkt91] Separable dimensions can be tested one at a time and their answers merged without losing precision. Coupled ones cannot.

Take `x[i + 1, i + 3] = x[i, i]`. Alone, the first dimension says the read runs one iteration after the write, and the second says three; each reports a dependence with direction `<`. Together they need a pair of iterations that is one apart and three apart, and there is none. Goff, Kennedy and Tseng partition subscripts into separable ones and minimal coupled groups, and test a coupled group with their Delta test, which carries what one dimension proves into the next.[^gkt91]

## The strong SIV test

Goff, Kennedy and Tseng report that most array references in the scientific Fortran programs they studied are simple.[^gkt91] The commonest SIV shape has the same coefficient on both sides, $a i + c_1$ against $a i' + c_2$. They call it **strong SIV**, and its exact test also yields the distance:[^gkt91]

$$
d = i' - i = \frac{c_1 - c_2}{a}
$$

A dependence exists if and only if $d$ is an integer and $|d| \le U - L$, where $L$ and $U$ are the loop's lower and upper bounds. The sign of $d$ gives the direction. LLVM's `strongSIVtest` implements it, with a comment that cites section 4.2.1 of the paper.[^llvm-da]

Work three pairs by hand, with the write first and `i` running from 0 to 9, so that U − L = 9:

| Write | Read | d = (c₁ − c₂) / a | Verdict |
| --- | --- | --- | --- |
| `x[3i + 6]` | `x[3i]` | (6 − 0) / 3 = 2 | flow, distance 2 |
| `x[2i]` | `x[2i + 1]` | (0 − 1) / 2 = −1/2 | none: not an integer |
| `x[i + 20]` | `x[i]` | (20 − 0) / 1 = 20 | none: 20 > 9 |

A negative integer $d$ would mean the read runs first: an anti dependence from the read to the write.

When the two coefficients differ, the subscript is **weak SIV**. Goff, Kennedy and Tseng single out two cases.[^gkt91] In **weak-zero SIV** one side is constant, as in `x[i]` against `x[0]`, and every dependence involves one particular iteration, often the first or last, which peeling that iteration removes. In **weak-crossing SIV** the coefficients are opposite, as in `x[i]` against `x[n − i]`, and every dependence crosses one middle iteration, where splitting the loop removes it. The general case falls back on an exact single-index test from earlier work.[^gkt91]

## The GCD test, and its limits

For subscripts with different coefficients, or with several indices, the oldest tool is the **GCD test**, which Bacon, Graham and Sharp list with Banerjee's inequalities as the two general approximate tests.[^bgs94] Two subscripts $a_1 i + c_1$ and $a_2 i' + c_2$ are equal when

$$
a_1 i - a_2 i' = c_2 - c_1
$$

An equation $ax + by = c$ in integers has a solution exactly when $\gcd(a, b)$ divides $c$. So if $\gcd(a_1, a_2)$ does not divide $c_2 - c_1$, no two iterations anywhere touch one element, and there is no dependence. With more indices, the gcd of all their coefficients plays the same role.

The test ignores the loop bounds, and that is its weakness. When the gcd divides, a solution exists somewhere among the integers, perhaps far outside the loop, and the test proves nothing. Figure 3 shows one pair the GCD test separates and one it cannot.

<figure class="vx-figure">
<svg viewBox="0 0 720 300" role="img" aria-label="Two strips of array elements numbered 0 to 29. Top panel: x[2i] writes the even elements 0 to 18 and x[2i + 1] reads the odd elements 1 to 19; the two sets interleave but never share an element, and the GCD test proves it because 2 does not divide 1. Bottom panel: with i from 0 to 9, x[i + 20] writes elements 20 to 29 and x[i] reads elements 0 to 9; the two ranges never meet, but the GCD test cannot tell, because gcd(1, 1) = 1 divides 20. Only a test that uses the loop bounds proves independence.">
<text class="vx-text-accent" x="20" y="22">x[2i] and x[2i + 1]: parity keeps them apart (GCD test: independent)</text>
<text class="vx-text" x="162" y="51" text-anchor="end">x[2i] writes</text>
<rect class="vx-box-accent" x="170" y="36" width="16" height="18"/>
<rect class="vx-box" x="188" y="36" width="16" height="18"/>
<rect class="vx-box-accent" x="206" y="36" width="16" height="18"/>
<rect class="vx-box" x="224" y="36" width="16" height="18"/>
<rect class="vx-box-accent" x="242" y="36" width="16" height="18"/>
<rect class="vx-box" x="260" y="36" width="16" height="18"/>
<rect class="vx-box-accent" x="278" y="36" width="16" height="18"/>
<rect class="vx-box" x="296" y="36" width="16" height="18"/>
<rect class="vx-box-accent" x="314" y="36" width="16" height="18"/>
<rect class="vx-box" x="332" y="36" width="16" height="18"/>
<rect class="vx-box-accent" x="350" y="36" width="16" height="18"/>
<rect class="vx-box" x="368" y="36" width="16" height="18"/>
<rect class="vx-box-accent" x="386" y="36" width="16" height="18"/>
<rect class="vx-box" x="404" y="36" width="16" height="18"/>
<rect class="vx-box-accent" x="422" y="36" width="16" height="18"/>
<rect class="vx-box" x="440" y="36" width="16" height="18"/>
<rect class="vx-box-accent" x="458" y="36" width="16" height="18"/>
<rect class="vx-box" x="476" y="36" width="16" height="18"/>
<rect class="vx-box-accent" x="494" y="36" width="16" height="18"/>
<rect class="vx-box" x="512" y="36" width="16" height="18"/>
<rect class="vx-box" x="530" y="36" width="16" height="18"/>
<rect class="vx-box" x="548" y="36" width="16" height="18"/>
<rect class="vx-box" x="566" y="36" width="16" height="18"/>
<rect class="vx-box" x="584" y="36" width="16" height="18"/>
<rect class="vx-box" x="602" y="36" width="16" height="18"/>
<rect class="vx-box" x="620" y="36" width="16" height="18"/>
<rect class="vx-box" x="638" y="36" width="16" height="18"/>
<rect class="vx-box" x="656" y="36" width="16" height="18"/>
<rect class="vx-box" x="674" y="36" width="16" height="18"/>
<rect class="vx-box" x="692" y="36" width="16" height="18"/>
<text class="vx-text" x="162" y="75" text-anchor="end">x[2i + 1] reads</text>
<rect class="vx-box" x="170" y="60" width="16" height="18"/>
<rect class="vx-box-strong" x="188" y="60" width="16" height="18"/>
<rect class="vx-box" x="206" y="60" width="16" height="18"/>
<rect class="vx-box-strong" x="224" y="60" width="16" height="18"/>
<rect class="vx-box" x="242" y="60" width="16" height="18"/>
<rect class="vx-box-strong" x="260" y="60" width="16" height="18"/>
<rect class="vx-box" x="278" y="60" width="16" height="18"/>
<rect class="vx-box-strong" x="296" y="60" width="16" height="18"/>
<rect class="vx-box" x="314" y="60" width="16" height="18"/>
<rect class="vx-box-strong" x="332" y="60" width="16" height="18"/>
<rect class="vx-box" x="350" y="60" width="16" height="18"/>
<rect class="vx-box-strong" x="368" y="60" width="16" height="18"/>
<rect class="vx-box" x="386" y="60" width="16" height="18"/>
<rect class="vx-box-strong" x="404" y="60" width="16" height="18"/>
<rect class="vx-box" x="422" y="60" width="16" height="18"/>
<rect class="vx-box-strong" x="440" y="60" width="16" height="18"/>
<rect class="vx-box" x="458" y="60" width="16" height="18"/>
<rect class="vx-box-strong" x="476" y="60" width="16" height="18"/>
<rect class="vx-box" x="494" y="60" width="16" height="18"/>
<rect class="vx-box-strong" x="512" y="60" width="16" height="18"/>
<rect class="vx-box" x="530" y="60" width="16" height="18"/>
<rect class="vx-box" x="548" y="60" width="16" height="18"/>
<rect class="vx-box" x="566" y="60" width="16" height="18"/>
<rect class="vx-box" x="584" y="60" width="16" height="18"/>
<rect class="vx-box" x="602" y="60" width="16" height="18"/>
<rect class="vx-box" x="620" y="60" width="16" height="18"/>
<rect class="vx-box" x="638" y="60" width="16" height="18"/>
<rect class="vx-box" x="656" y="60" width="16" height="18"/>
<rect class="vx-box" x="674" y="60" width="16" height="18"/>
<rect class="vx-box" x="692" y="60" width="16" height="18"/>
<text class="vx-text-muted" x="179" y="96" text-anchor="middle">0</text>
<text class="vx-text-muted" x="269" y="96" text-anchor="middle">5</text>
<text class="vx-text-muted" x="359" y="96" text-anchor="middle">10</text>
<text class="vx-text-muted" x="449" y="96" text-anchor="middle">15</text>
<text class="vx-text-muted" x="539" y="96" text-anchor="middle">20</text>
<text class="vx-text-muted" x="629" y="96" text-anchor="middle">25</text>
<text class="vx-text-accent" x="20" y="152">x[i + 20] and x[i], 0 &lt;= i &lt; 10: the bounds keep them apart (GCD test: maybe)</text>
<text class="vx-text" x="162" y="181" text-anchor="end">x[i + 20] writes</text>
<rect class="vx-box" x="170" y="166" width="16" height="18"/>
<rect class="vx-box" x="188" y="166" width="16" height="18"/>
<rect class="vx-box" x="206" y="166" width="16" height="18"/>
<rect class="vx-box" x="224" y="166" width="16" height="18"/>
<rect class="vx-box" x="242" y="166" width="16" height="18"/>
<rect class="vx-box" x="260" y="166" width="16" height="18"/>
<rect class="vx-box" x="278" y="166" width="16" height="18"/>
<rect class="vx-box" x="296" y="166" width="16" height="18"/>
<rect class="vx-box" x="314" y="166" width="16" height="18"/>
<rect class="vx-box" x="332" y="166" width="16" height="18"/>
<rect class="vx-box" x="350" y="166" width="16" height="18"/>
<rect class="vx-box" x="368" y="166" width="16" height="18"/>
<rect class="vx-box" x="386" y="166" width="16" height="18"/>
<rect class="vx-box" x="404" y="166" width="16" height="18"/>
<rect class="vx-box" x="422" y="166" width="16" height="18"/>
<rect class="vx-box" x="440" y="166" width="16" height="18"/>
<rect class="vx-box" x="458" y="166" width="16" height="18"/>
<rect class="vx-box" x="476" y="166" width="16" height="18"/>
<rect class="vx-box" x="494" y="166" width="16" height="18"/>
<rect class="vx-box" x="512" y="166" width="16" height="18"/>
<rect class="vx-box-accent" x="530" y="166" width="16" height="18"/>
<rect class="vx-box-accent" x="548" y="166" width="16" height="18"/>
<rect class="vx-box-accent" x="566" y="166" width="16" height="18"/>
<rect class="vx-box-accent" x="584" y="166" width="16" height="18"/>
<rect class="vx-box-accent" x="602" y="166" width="16" height="18"/>
<rect class="vx-box-accent" x="620" y="166" width="16" height="18"/>
<rect class="vx-box-accent" x="638" y="166" width="16" height="18"/>
<rect class="vx-box-accent" x="656" y="166" width="16" height="18"/>
<rect class="vx-box-accent" x="674" y="166" width="16" height="18"/>
<rect class="vx-box-accent" x="692" y="166" width="16" height="18"/>
<text class="vx-text" x="162" y="205" text-anchor="end">x[i] reads</text>
<rect class="vx-box-strong" x="170" y="190" width="16" height="18"/>
<rect class="vx-box-strong" x="188" y="190" width="16" height="18"/>
<rect class="vx-box-strong" x="206" y="190" width="16" height="18"/>
<rect class="vx-box-strong" x="224" y="190" width="16" height="18"/>
<rect class="vx-box-strong" x="242" y="190" width="16" height="18"/>
<rect class="vx-box-strong" x="260" y="190" width="16" height="18"/>
<rect class="vx-box-strong" x="278" y="190" width="16" height="18"/>
<rect class="vx-box-strong" x="296" y="190" width="16" height="18"/>
<rect class="vx-box-strong" x="314" y="190" width="16" height="18"/>
<rect class="vx-box-strong" x="332" y="190" width="16" height="18"/>
<rect class="vx-box" x="350" y="190" width="16" height="18"/>
<rect class="vx-box" x="368" y="190" width="16" height="18"/>
<rect class="vx-box" x="386" y="190" width="16" height="18"/>
<rect class="vx-box" x="404" y="190" width="16" height="18"/>
<rect class="vx-box" x="422" y="190" width="16" height="18"/>
<rect class="vx-box" x="440" y="190" width="16" height="18"/>
<rect class="vx-box" x="458" y="190" width="16" height="18"/>
<rect class="vx-box" x="476" y="190" width="16" height="18"/>
<rect class="vx-box" x="494" y="190" width="16" height="18"/>
<rect class="vx-box" x="512" y="190" width="16" height="18"/>
<rect class="vx-box" x="530" y="190" width="16" height="18"/>
<rect class="vx-box" x="548" y="190" width="16" height="18"/>
<rect class="vx-box" x="566" y="190" width="16" height="18"/>
<rect class="vx-box" x="584" y="190" width="16" height="18"/>
<rect class="vx-box" x="602" y="190" width="16" height="18"/>
<rect class="vx-box" x="620" y="190" width="16" height="18"/>
<rect class="vx-box" x="638" y="190" width="16" height="18"/>
<rect class="vx-box" x="656" y="190" width="16" height="18"/>
<rect class="vx-box" x="674" y="190" width="16" height="18"/>
<rect class="vx-box" x="692" y="190" width="16" height="18"/>
<text class="vx-text-muted" x="179" y="226" text-anchor="middle">0</text>
<text class="vx-text-muted" x="269" y="226" text-anchor="middle">5</text>
<text class="vx-text-muted" x="359" y="226" text-anchor="middle">10</text>
<text class="vx-text-muted" x="449" y="226" text-anchor="middle">15</text>
<text class="vx-text-muted" x="539" y="226" text-anchor="middle">20</text>
<text class="vx-text-muted" x="629" y="226" text-anchor="middle">25</text>
<text class="vx-text-muted" x="20" y="270">array element index; outlined cells are written, dark-edged cells are read, plain cells are untouched</text>
</svg>
<figcaption>Figure 3. Two subscript pairs that never touch the same element, for different reasons. Top: every write lands on an even element and every read on an odd one, a divisibility fact the GCD test finds. Bottom: writes and reads fall in ranges twenty apart, a fact about the loop bounds that the GCD test cannot see, because gcd(1, 1) = 1 divides every difference.</figcaption>
</figure>

The second example runs three tests on six pairs, with the loop running from 0 to 9, and compares each verdict with brute force.

--8<-- "includes/examples/optimize/p6-dependence-analysis/subscript_tests.cpp.md"

Every `none` is right, and every real dependence was reported as possible: both tests are conservative. They differ in how often they say `maybe` when the truth is `no`. The GCD test says it for `x[i + 20]` against `x[i]`, where the strong SIV test uses the bounds and proves independence. It says it again for `x[2i]` against `x[i + 20]`, where the strong SIV test does not apply and brute force shows the two never meet.

??? check "Neither test settles `x[2i]` against `x[i + 20]`. What must a compiler with only these two tests do, and what fact would settle the question?"

    Assume the dependence, and give up any transformation it would block. The bounds settle it: with `i` from 0 to 9, `2i` covers 0 to 18 and `i + 20` covers 20 to 29, so the two ranges never overlap. Comparing the ranges each side can reach is what Banerjee's inequalities do in general; an exact integer test such as the Omega test decides the question outright.

## Bounds, inequalities and exact tests

That last case needs the bounds. For `x[2i]` against `x[i' + 20]`, the equation is $2i - i' = 20$, and with both indices between 0 and 9 the left side can only reach values from −9 to 18. Twenty lies outside that range, so there is no solution.

Goff, Kennedy and Tseng give tests of this form, comparing the constant difference with the smallest and largest values the other side can take within the bounds, and describe them as special cases of **Banerjee's inequality**.[^gkt91] Bacon, Graham and Sharp classify Banerjee's inequalities, like the GCD test, as approximate: a range that contains the constant does not guarantee an integer solution.[^bgs94] For MIV subscripts, Goff, Kennedy and Tseng use a combined Banerjee and GCD test to build every possible direction vector.[^gkt91]

Pugh's **Omega test** is exact. It extends Fourier-Motzkin elimination, a method for systems of linear inequalities over the reals, to integers, and its worst case takes exponential time. Pugh presented evidence that on real programs it is competitive with the approximate tests in use, and showed that it can project a problem onto chosen variables, which is how it computes distance and direction vectors.[^pugh91] [P9](p9-polyhedral-model.md) treats dependences as integer sets in the same spirit.

## What LLVM does

LLVM 18's `DependenceAnalysis` describes itself, in its file header, as an incomplete implementation of Goff, Kennedy and Tseng's approach.[^llvm-da] It answers a query about one pair of memory instructions: no dependence, or a description with a kind and one entry per common loop, printed as a distance where it knows one and as a set of directions where it does not. The header names two gaps: it cannot propagate constraints between coupled RDIV subscripts (a form of MIV in which each side has one index, a different one on each side) and lacks a multi-subscript MIV test. It calls both conservative weaknesses, not sources of wrong answers.[^llvm-da]

The header adds a sentence that matters for Vortex. Because Clang linearizes some array subscripts, the analysis uses ScalarEvolution's delinearization to recover the separate subscripts, and so avoid the more expensive and less precise MIV tests.[^llvm-da] A **linearized** subscript is a two-dimensional access already turned into one offset, such as `i * 64 + j`. **Delinearization** tries to split that offset back into `[i, j]`.

The third example gives LLVM 18's loop-interchange pass the same nest twice. Both functions copy each element of a 64 by 64 array one row down, `grid[i + 1, j] = grid[i, j]`, with the column loop outside. The flow dependence has distance $(0, 1)$ in `(j, i)` order and $(1, 0)$ after interchange, so the swap is legal. `@down_2d` indexes rows of type `[64 x i32]`; `@down_flat` computes `i * 64 + j` by hand, as C code over a plain pointer does.

--8<-- "includes/examples/optimize/p6-dependence-analysis/llvm_subscripts.ll.md"

The pass interchanges the first nest and refuses the second because of its dependences. The analysis shows why. On the owner's M4 Pro with LLVM 18.1.8 (2026-09-24), `opt -passes='print<da>'` reported the load-to-store pair of `@down_2d` as `consistent anti [0 -1]`, and that of `@down_flat` as `anti [<= >]`.

The first answer is exact. The printer names the pair in program order, load first, and the `-1` says the store runs one `i` iteration earlier than the load that reads its element. Turned around, it is the flow dependence $(0, 1)$. Bacon, Graham and Sharp cite a framework, due to Burke and Cytron, built on the same symmetry: an anti dependence with an impossible vector is a flow dependence.[^bgs94] "Consistent" means, in LLVM's words, that the dependence occurs every time both instructions run.[^llvm-dah] The second answer has no distances, only directions: the analysis did not separate the rows. It allows `<` or `=` for `j` and `>` for `i`, and so cannot exclude the direction $(<, >)$ that forbids interchange.

Delinearization is only sound when subscripts stay inside their dimensions. LLVM checks this: it keeps the recovered subscripts only if it can prove that each one after the first is non-negative and below its dimension's size. A hidden option turns the check off, and its description warns that doing so can produce wrong dependence vectors for languages that let one dimension's subscript overflow into another.[^llvm-da]

??? check "In `@down_flat`, the element read at `(j, i)` is `grid[i * 64 + j]`. Why can't an analysis that sees only this offset conclude that `j` and `i` are separate subscripts?"

    Because the same offset has other readings. With nothing proving `0 <= j < 64`, the offset `i * 64 + j` for `i = 1, j = 0` equals the offset for `i = 0, j = 64`. Only a proof that `j` stays below 64 makes the split into row and column unique, which is the range check LLVM performs before it trusts a delinearization.

## Vortex's shapes remove a whole step

A Vortex array carries its shape in its type, `[f32; 64, 64]`, and every access gives one index per dimension; linear indexing is not part of v0.1 ([Arrays 7.6](../specification/arrays.md#76-indexing)). A Vortex compiler can run dependence analysis on an IR that still says `a[row, k]`, before it lowers addresses to `row * 64 + k` under [decision 43](../decisions/arrays.md#d43). The analysis then gets the per-dimension subscripts that Goff, Kennedy and Tseng's tests expect, without recovering them. Lower first, and the compiler is back where `@down_flat` left LLVM.

Keeping the dimensions apart is sound only if no subscript leaves its extent. Figure 4 shows what goes wrong otherwise.

<figure class="vx-figure">
<svg viewBox="0 0 720 320" role="img" aria-label="A 3 by 4 array drawn as a grid, rows 0 to 2 and columns 0 to 3, above the same twelve elements laid out in one row of memory, numbered 0 to 11, row after row. A dashed cell to the right of row 0 stands for the out-of-range subscript a[0, 4]. An arrow from it and an arrow from grid cell a[1, 0] both land on memory element 4. Two subscripts that differ in both dimensions address the same element once one of them leaves its extent, which is why a per-dimension dependence test is sound only when every subscript is within its extent.">
<defs><marker id="p6-f4-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text-accent" x="20" y="26">a: [i32; 3, 4], rows stored one after another</text>
<rect class="vx-box" x="60" y="50" width="56" height="30"/>
<text class="vx-text" x="88" y="71" text-anchor="middle">0, 0</text>
<rect class="vx-box" x="120" y="50" width="56" height="30"/>
<text class="vx-text" x="148" y="71" text-anchor="middle">0, 1</text>
<rect class="vx-box" x="180" y="50" width="56" height="30"/>
<text class="vx-text" x="208" y="71" text-anchor="middle">0, 2</text>
<rect class="vx-box" x="240" y="50" width="56" height="30"/>
<text class="vx-text" x="268" y="71" text-anchor="middle">0, 3</text>
<rect class="vx-box-accent" x="60" y="84" width="56" height="30"/>
<text class="vx-text" x="88" y="105" text-anchor="middle">1, 0</text>
<rect class="vx-box" x="120" y="84" width="56" height="30"/>
<text class="vx-text" x="148" y="105" text-anchor="middle">1, 1</text>
<rect class="vx-box" x="180" y="84" width="56" height="30"/>
<text class="vx-text" x="208" y="105" text-anchor="middle">1, 2</text>
<rect class="vx-box" x="240" y="84" width="56" height="30"/>
<text class="vx-text" x="268" y="105" text-anchor="middle">1, 3</text>
<rect class="vx-box" x="60" y="118" width="56" height="30"/>
<text class="vx-text" x="88" y="139" text-anchor="middle">2, 0</text>
<rect class="vx-box" x="120" y="118" width="56" height="30"/>
<text class="vx-text" x="148" y="139" text-anchor="middle">2, 1</text>
<rect class="vx-box" x="180" y="118" width="56" height="30"/>
<text class="vx-text" x="208" y="139" text-anchor="middle">2, 2</text>
<rect class="vx-box" x="240" y="118" width="56" height="30"/>
<text class="vx-text" x="268" y="139" text-anchor="middle">2, 3</text>
<rect class="vx-box-bad" x="300" y="50" width="56" height="30"/>
<text class="vx-text" x="328" y="71" text-anchor="middle">0, 4</text>
<text class="vx-text-muted" x="364" y="71">a[0, 4]: column 4 is outside the extent 4</text>
<rect class="vx-box" x="60" y="250" width="46" height="30"/>
<text class="vx-text" x="83" y="270" text-anchor="middle">0</text>
<rect class="vx-box" x="110" y="250" width="46" height="30"/>
<text class="vx-text" x="133" y="270" text-anchor="middle">1</text>
<rect class="vx-box" x="160" y="250" width="46" height="30"/>
<text class="vx-text" x="183" y="270" text-anchor="middle">2</text>
<rect class="vx-box" x="210" y="250" width="46" height="30"/>
<text class="vx-text" x="233" y="270" text-anchor="middle">3</text>
<rect class="vx-box-accent" x="260" y="250" width="46" height="30"/>
<text class="vx-text" x="283" y="270" text-anchor="middle">4</text>
<rect class="vx-box" x="310" y="250" width="46" height="30"/>
<text class="vx-text" x="333" y="270" text-anchor="middle">5</text>
<rect class="vx-box" x="360" y="250" width="46" height="30"/>
<text class="vx-text" x="383" y="270" text-anchor="middle">6</text>
<rect class="vx-box" x="410" y="250" width="46" height="30"/>
<text class="vx-text" x="433" y="270" text-anchor="middle">7</text>
<rect class="vx-box" x="460" y="250" width="46" height="30"/>
<text class="vx-text" x="483" y="270" text-anchor="middle">8</text>
<rect class="vx-box" x="510" y="250" width="46" height="30"/>
<text class="vx-text" x="533" y="270" text-anchor="middle">9</text>
<rect class="vx-box" x="560" y="250" width="46" height="30"/>
<text class="vx-text" x="583" y="270" text-anchor="middle">10</text>
<rect class="vx-box" x="610" y="250" width="46" height="30"/>
<text class="vx-text" x="633" y="270" text-anchor="middle">11</text>
<text class="vx-text-muted" x="60" y="300">memory: element r * 4 + c</text>
<line class="vx-line" x1="328" y1="80" x2="289" y2="246" marker-end="url(#p6-f4-head)"/>
<line class="vx-line" x1="88" y1="114" x2="277" y2="246" marker-end="url(#p6-f4-head)"/>
<text class="vx-text-muted" x="323" y="210">both land on element 4</text>
</svg>
<figcaption>Figure 4. In a row-major array of type [i32; 3, 4], the subscript a[0, 4] would be memory element 4, the same element as a[1, 0]. Tested one dimension at a time, the two accesses look independent because their rows differ; in memory they collide. Testing dimension by dimension is sound only when every subscript stays within its extent.</figcaption>
</figure>

Vortex supplies that guarantee. Every index is checked against its extent, during compilation when it is a constant and at run time otherwise, and an index outside its extent is an error ([Arrays 7.6](../specification/arrays.md#76-indexing)) that stops the program ([decision 14](../decisions/program.md#d14)). So every access that completes has every subscript in range, and a Vortex compiler may test each dimension separately, provided it keeps the checks themselves in order, which is the subject of the next section. Extents are constants as well, so they bound a subscript even where a loop's own bounds are known only at run time.

## Checks and prints are ordered too

Dependences between array elements are not the only order a Vortex loop has. A bounds check that may fail, an overflow check, and a call to `print` each produce something the user sees, and [O1](o1-optimizer-contract.md#vortexs-list) requires the same output, the same error line and the same exit status. Reordering iterations can make a different check fail first, or print lines in another order, even when no array element is touched out of order. [P7](p7-loop-transformations.md#what-a-vortex-transformation-must-also-keep) adopts a policy: transform only nests whose checks are proven unable to fail, and treat `print` as a dependence no transformation may reorder.

A dependence analysis can state that policy in its own terms. Treat every `print`, and every check that may still fail, as a write to one shared location, the program's output. Two such events in different iterations are then an output dependence, every loop around them carries one, and no permutation of those loops passes the legality test. A nest whose checks [O8](o8-loops.md#removing-a-check-with-a-proof) has proven away has no such events, which is one more reason to run check elimination before dependence analysis.

## Your turn: a nest with two dependences

This loop updates `a` in place, writing one column to the right of an element it reads:

```text
for i in 0..n - 1 {
    for j in 0..n - 1 {
        a[i, j + 1] = a[i, j] + a[i + 1, j]
    }
}
```

It has two dependences on `a`. The first row is filled in; fill in the second, then decide whether interchanging `i` and `j` is legal.

| Read | Written by iteration | Kind | Distance (Δi, Δj) |
| --- | --- | --- | --- |
| `a[i, j]` | (i, j − 1), earlier | flow | (0, 1) |
| `a[i + 1, j]` | ? | ? | ? |

??? check "Answer: the second dependence, and whether the interchange is legal"

    `a[i + 1, j]`, read in iteration `(i, j)`, is written by iteration `(i + 1, j − 1)`, whose target is `a[i + 1, (j − 1) + 1]`. That iteration comes later, since its `i` is larger, so the read runs first: an anti dependence from `(i, j)` to `(i + 1, j − 1)`, distance (1, −1).

    The flow dependence (0, 1) would allow either order, like the matrix product. The anti dependence has the stencil's shape: interchange turns it into (−1, 1), whose first nonzero entry is negative. One illegal dependence is enough to block the permutation, so the nest cannot be interchanged as written.

## For Vortex

!!! vortex "Exercise"

    **Build** a dependence analysis for the loop nests in your compiler's IR, on the loop forest and induction variables from [O8](o8-loops.md#for-vortex), running before array addresses are lowered to offsets.

    1. **Collect accesses.** For each nest of `for` loops whose subscripts are affine in the loop variables, list every read and write of each array with one affine form per dimension. Mark a nest you cannot describe this way, such as one with a subscript read from another array, as having every dependence.
    2. **Model the events.** Treat every `print`, and every check your earlier passes could not prove, as a write to one shared location.
    3. **Test each pair** of accesses to one array, at least one of them a write. Classify each dimension as ZIV, SIV or MIV, and as separable or coupled. Implement the ZIV test, the strong SIV test with the loop bounds, and the GCD test. Whatever they leave open is an assumed dependence, with direction `*` in every loop the subscripts mention.
    4. **Describe each dependence** by its kind, its distance or direction vector, and its carrying loop.
    5. **Answer two queries:** does a given loop carry any dependence, and is a given permutation of a perfect nest legal?
    6. **Remarks**, in the stream you built for [O1](o1-optimizer-contract.md#for-vortex): one for each query answered, naming the dependence that decided it, such as `interchange (i, j) refused: flow on g, distance (1, -1) becomes (-1, 1)`, and naming an assumed dependence as assumed.

    **Not yet:** weak SIV tests beyond the two special cases (a stretch goal once the rest passes); Banerjee's inequalities, the Delta test and the Omega test; loops whose bounds depend on an outer loop's variable; dependences between two different references, which wait for [O9](o9-alias-analysis.md); performing any transformation, which is [P7](p7-loop-transformations.md)'s job.

    **Proof that it works:**

    - A brute-force oracle, kept as a test outside the compiler, that enumerates each test nest with its extents reduced to 4, as the first example does. On every nest in your suite, every dependence the oracle finds must appear in your analysis's report; each extra one the analysis reports must be marked as assumed.
    - Golden remark files for five programs: the stage 10 kernel as written (the only dependence is on `sum`, carried by `k`; none on arrays), the multiply-accumulate form (all six loop orders legal), the stencil (interchange refused), this chapter's two-dependence nest (refused, naming the anti dependence), and a loop that writes `x[2 * i]` and reads `x[i + 20]` (a dependence reported as assumed).
    - Two nests that must be reported as carrying a dependence on the shared output location: one with a `print` in its body, and one with an index that may be out of range.
    - A table filled in from your compiler's remarks, with its version and the date:

    | Program | Access pairs tested | Settled by ZIV | Settled by strong SIV | Settled by GCD | Assumed | Loops carrying no dependence |
    | --- | --- | --- | --- | --- | --- | --- |
    | stage 10 `multiply` | | | | | | |
    | the stencil | | | | | | |
    | your largest test program | | | | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **What stops a compiler from reordering a loop's iterations freely?** A dependence: two accesses to one location, at least one a write, whose order the original program fixes.
    - **What are the kinds of dependence?** Flow (read after write), anti (write after read) and output (write after write); two reads form an input dependence, which fixes no order.
    - **Which loop carries a dependence?** The outermost loop whose distance entry is nonzero; a loop that carries none may run its iterations in any order once the outer loops are fixed.
    - **When is a permutation of a loop nest legal?** When every dependence's distance vector, rearranged to the new loop order, is still lexicographically positive.
    - **What does the GCD test prove, and what does it miss?** It proves independence when the gcd of the coefficients does not divide the constant difference; it ignores the loop bounds, so it cannot prove pairs like `x[i + 20]` and `x[i]` independent.
    - **Why does LLVM delinearize, and when is it sound?** Clang linearizes some subscripts, and per-dimension tests are more precise than MIV tests on one offset; splitting an offset is sound only when every subscript stays within its dimension.
    - **Why may a Vortex compiler test each dimension separately?** Its arrays keep their shape and every index is checked against its extent, so every access that completes has every subscript in range.

## Where this comes back

!!! next "You will use this again in"

    - [P7. Loop transformations](p7-loop-transformations.md): *distance and direction vectors*, *the carrying loop*, *the legality rule for interchange*
    - [P8. Cache blocking](p8-cache-blocking.md): *every order of the kernel's loops is legal*
    - [P9. The polyhedral model](p9-polyhedral-model.md): *dependence equations*, *exact integer tests*
    - [P10. Vectorization](p10-vectorization.md): *dependence distance*, *loops that carry no dependence*
    - [P13. Multithreading](p13-multithreading.md): *loops that carry no dependence*
    - [C6. Instruction scheduling](../backend/c6-scheduling.md): *flow, anti and output dependence*
    - [G13. Tile languages](../gpu/g13-tile-languages.md): *parallel and reduction loops*

## Sources and further reading

Start with Goff, Kennedy and Tseng: the paper is short, and its first five sections give the definitions, the classification and the SIV tests in the order a compiler applies them. Bacon, Graham and Sharp's survey, sections 5 and 6, covers dependence vectors and the legality of each loop transformation. Read Pugh's paper once the limits of the approximate tests are clear. The header of LLVM's `DependenceAnalysis.cpp` is a short, frank account of what a production implementation does and does not do.

[^gkt91]: Gina Goff, Ken Kennedy and Chau-Wen Tseng, "Practical Dependence Testing", *Proceedings of the ACM SIGPLAN 1991 Conference on Programming Language Design and Implementation (PLDI)*, 1991: the abstract, sections 1.1 to 1.5, 2.1, 2.2, 3.1, 4.1, 4.2.1, 4.2.2, 4.4 and 4.5, and the opening of section 5. <https://doi.org/10.1145/113445.113448>
[^bgs94]: David F. Bacon, Susan L. Graham and Oliver J. Sharp, "Compiler Transformations for High-Performance Computing", *ACM Computing Surveys* 26(4), 1994: sections 5.1 to 5.4 and 6.2.1. <https://doi.org/10.1145/197405.197406>
[^pugh91]: William Pugh, "The Omega Test: A Fast and Practical Integer Programming Algorithm for Dependence Analysis", *Proceedings of Supercomputing '91*, 1991, pages 4 to 13; the abstract, as given in the expanded version the author hosts (*Communications of the ACM*, August 1992). <https://doi.org/10.1145/125826.125848>, <https://www.cs.umd.edu/~pugh/papers/omega.pdf>
[^llvm-da]: LLVM Project, `DependenceAnalysis.cpp`, release/18.x branch: the file header, the description of the option `da-disable-delinearization-checks`, the comments on `testZIV` and `strongSIVtest`, and the range checks in `tryDelinearizeFixedSize` and `tryDelinearizeParametricSize`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Analysis/DependenceAnalysis.cpp>
[^llvm-dah]: LLVM Project, `DependenceAnalysis.h`, release/18.x branch: the comments on `Dependence::isConsistent` and `Dependence::isConfused`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/include/llvm/Analysis/DependenceAnalysis.h>
