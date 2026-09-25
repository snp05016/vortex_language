# M6. Loops: affine and scf

<p class="page-intro">MLIR writes loops in two dialects that make different promises. The affine dialect restricts bounds and subscripts to affine functions, so that passes can work out dependences and transform loops on their own; the scf dialect lifts the restriction and leaves more to whoever writes the IR. This chapter reads both, runs MLIR's loop passes and checks their work by hand, and shows which choices keep Vortex's floating-point rules and which quietly break them.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 35 minutes · Builds on: [M5. Structured ops: linalg, tensor and memref](m5-structured-ops.md), [P7. Loop transformations](../optimize/p7-loop-transformations.md)</p>

???+ remember "Before you start, remember"

    ??? question "How does an `scf.for` hand a running total from one iteration to the next?"

        Through a loop-carried value. The loop's region takes the running value as a block argument after the loop variable, the body passes the next value to `scf.yield`, and after the last iteration the loop's result holds the final value.

        Introduced in [M2. Reading MLIR](m2-reading-mlir.md).

    ??? question "When is a permutation of a loop nest legal?"

        When every dependence's distance vector, rearranged to match the new loop order, is still lexicographically positive: its first nonzero entry is positive.

        Introduced in [P6. Dependence analysis](../optimize/p6-dependence-analysis.md#when-a-loop-permutation-is-legal).

    ??? question "What does tiling do to a loop nest, and what must hold for it to be legal?"

        It strip-mines several loops and moves the strip loops outside, so the nest visits one tile after another. The loops being tiled must form a fully permutable band: no dependence may have a negative entry along them, unless an outer loop already carries it.

        Introduced in [P7. Loop transformations](../optimize/p7-loop-transformations.md#strip-mining-and-tiling).

    ??? question "What does `--convert-linalg-to-affine-loops` make of a `linalg.matmul` on memrefs?"

        Three nested `affine.for` loops, one per dimension, around loads of `a`, `b` and `c`, an `arith.mulf`, an `arith.addf` and a store back into `c`.

        Introduced in [M5. Structured ops: linalg, tensor and memref](m5-structured-ops.md#the-transformations-this-shape-is-built-for).

    ??? question "May a Vortex compiler add the terms of an `f32` sum in a different order?"

        No. Each `f32` operation is one IEEE 754 operation, rounded to nearest with ties to even, and a compiler must not contract, reassociate or reorder them.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Read `affine.for`, `affine.load`, `affine.store` and affine maps, and decide whether a value may serve as a dim or a symbol.
    - Explain what the affine restriction buys: dependence questions that a pass can answer from the IR alone.
    - Run `-affine-loop-tile`, `-affine-loop-fusion` and `-affine-parallelize`, and predict their output by hand.
    - Test a transformation's legality yourself instead of trusting the pass, using a nest that MLIR 18.1.8 tiles wrongly.
    - Choose between `affine.for`, `scf.for`, `scf.parallel` and `scf.forall` for a loop, and say which choices put decision 56 at risk.

## One loop nest, four levels

[M5](m5-structured-ops.md) ended with a matrix product written as one operation, `linalg.matmul`, and lowered it to loops with `--convert-linalg-to-affine-loops`. Here is that lowering's result for a 4 × 3 matrix times a 3 × 2 one, written out by hand as the first function of this chapter's fourth example:

```mlir
affine.for %i = 0 to 4 {
  affine.for %j = 0 to 2 {
    affine.for %k = 0 to 3 {
      %x = affine.load %a[%i, %k] : memref<4x3xf32>
      ...
```

The nest runs its body once for each triple `(i, j, k)` with `0 ≤ i < 4`, `0 ≤ j < 2` and `0 ≤ k < 3`: 24 triples, in the order the loops count. That set of points, together with the order the nest visits them in, is the nest's **iteration space**. [P9](../optimize/p9-polyhedral-model.md#a-loop-nest-as-a-set-of-points) treats it as one geometric object; this chapter asks what an IR has to keep for a pass to reason about it that way.

The same nest can sit at four levels of MLIR, and each level keeps less. Figure 1 lines them up.

<figure class="vx-figure">
<svg viewBox="0 0 760 430" role="img" aria-label="The matrix product at four levels of MLIR, from linalg down to cf, with what each level lets a pass see" aria-describedby="m6-f1-desc">
<title id="m6-f1-title">One loop nest at four levels</title>
<desc id="m6-f1-desc">Four boxes stacked top to bottom, joined by arrows labelled with the pass that lowers one level to the next. Top: linalg.matmul ins(%a, %b) outs(%c), one operation whose indexing maps name the data each point touches. Arrow labelled --convert-linalg-to-affine-loops. Second: affine.for %i = 0 to 4 and affine.load %a[%i, %k], loops whose bounds and subscripts are affine maps, so a pass can compute dependences from the IR. Arrow labelled --lower-affine. Third: scf.for %i = %c0 to %c4 step %c1 and memref.load %a[%i, %k], loops whose bounds and subscripts are ordinary index values, so the loop structure remains but the subscripts are opaque. Arrow labelled --convert-scf-to-cf. Bottom: cf.cond_br %cmp, ^body, ^exit, blocks and branches, where the loop must be found again as a cycle in the control-flow graph. The affine box is highlighted.</desc>
<rect class="vx-box" x="20" y="16" width="430" height="70" rx="4"/>
<text class="vx-mono" x="36" y="44">linalg.matmul ins(%a, %b) outs(%c)</text>
<text class="vx-text-muted" x="36" y="68">one operation; maps name the data</text>
<text class="vx-text" x="470" y="44">sees: which data each point</text>
<text class="vx-text" x="470" y="64">touches, without any analysis</text>
<line class="vx-line" x1="120" y1="86" x2="120" y2="118"/>
<polygon class="vx-arrowhead" points="115,118 120,126 125,118"/>
<text class="vx-mono" x="132" y="108">--convert-linalg-to-affine-loops</text>
<rect class="vx-box-accent vx-pulse" x="20" y="126" width="430" height="70" rx="4"/>
<text class="vx-mono" x="36" y="154">affine.for %i = 0 to 4</text>
<text class="vx-mono" x="36" y="176">affine.load %a[%i, %k]</text>
<text class="vx-text" x="470" y="154">sees: dependences, computed</text>
<text class="vx-text" x="470" y="174">from affine bounds and subscripts</text>
<line class="vx-line" x1="120" y1="196" x2="120" y2="228"/>
<polygon class="vx-arrowhead" points="115,228 120,236 125,228"/>
<text class="vx-mono" x="132" y="218">--lower-affine</text>
<rect class="vx-box" x="20" y="236" width="430" height="70" rx="4"/>
<text class="vx-mono" x="36" y="264">scf.for %i = %c0 to %c4 step %c1</text>
<text class="vx-mono" x="36" y="286">memref.load %a[%i, %k]</text>
<text class="vx-text" x="470" y="264">sees: the loops, but bounds and</text>
<text class="vx-text" x="470" y="284">subscripts are plain values</text>
<line class="vx-line" x1="120" y1="306" x2="120" y2="338"/>
<polygon class="vx-arrowhead" points="115,338 120,346 125,338"/>
<text class="vx-mono" x="132" y="328">--convert-scf-to-cf</text>
<rect class="vx-box" x="20" y="346" width="430" height="70" rx="4"/>
<text class="vx-mono" x="36" y="374">cf.cond_br %cmp, ^body, ^exit</text>
<text class="vx-text-muted" x="36" y="398">blocks and branches</text>
<text class="vx-text" x="470" y="374">sees: a control-flow graph; the</text>
<text class="vx-text" x="470" y="394">loop must be found again (O8)</text>
</svg>
<figcaption>Figure 1. The matrix product at four levels, with the pass that takes each level to the next. Going down is always possible; each step turns facts that were part of the IR's grammar into ordinary computation. The affine level is the lowest one at which a pass can still read the whole iteration space and every subscript as affine maps.</figcaption>
</figure>

The steps down are one-way in practice. `--lower-affine` turns `affine.for` into `scf.for`, which the Passes documentation describes as free of certain structural restrictions on bounds and step, and turns each `affine.apply` into `arith` operations with the same effect.[^passes] After that, a bound such as `min(d0 + 3, 4)` is an `arith.addi`, an `arith.cmpi` and an `arith.select`, and nothing marks it as special. The current documentation lists passes that raise `memref` and `scf` operations back to affine form, on a best-effort basis;[^passes] `mlir-opt` 18.1.8 has neither (checked with `mlir-opt --help` on 2026-09-24). A front end that wants affine loops should emit them, not hope to recover them.

## Affine maps, dims and symbols

The affine dialect is built on one kind of function. An **affine function** of some integer inputs is, informally, a linear function plus a constant: each input multiplied by a constant, summed, plus a constant.[^affine] `2i + 1` and `i + j - 1` are affine in `i` and `j`; `i * j`, `i * i` and `a[i]` used as a subscript are not. MLIR also allows `floordiv`, `ceildiv` and `mod` by a positive constant, which the polyhedral community calls **quasi-affine**, and it calls these functions **affine maps**.[^affine]

An affine map is written with its inputs in two lists:

```mlir
affine_map<(d0, d1)[s0] -> (d0, d1 + s0)>
```

The names in parentheses are **dims**, and the names in square brackets are **symbols**. A dim corresponds to a dimension of the structure the map describes, such as one loop of a nest; a symbol stands for an unknown quantity that can be treated as a constant over the region the map is used in.[^affine] Both are always of type `index`. The distinction matters because a symbol does not vary while the loops run: a pass can reason about `i < n` for every `i` in the loop, with `n` fixed, but not about `i < f(i)` for an arbitrary `f`.

The loop that uses these maps is `affine.for`. Its lower and upper bounds are affine maps applied to dims and symbols, the range is half-open, and its step is a positive integer constant.[^affine] Its memory operations, `affine.load` and `affine.store`, take one affine expression of loop variables and symbols per dimension of the memref.[^affine] Here is the smallest useful loop: it sums a fixed eight-element row from a starting point chosen at run time.

--8<-- "includes/examples/mlir/m6-affine-and-scf/suffix_sum.mlir.md"

`%start` is a function argument, so its value is known only when the function runs, but it is the same on every iteration of the loop. That makes it a valid symbol, and `affine.for %i = %start to 8` is legal: the custom form's bare `%start` is shorthand for the map `()[s0] -> (s0)` applied to it.[^affine] The running total `%acc` is a loop-carried value, exactly as in M2's `scf.for`: `affine.for` takes `iter_args`, and `affine.yield` passes the next value on.[^affine]

**Which values may be symbols.** The dialect gives a precise list.[^affine] A value may be bound to a symbol if it is an argument of a region whose operation has the `AffineScope` trait (`func.func` has it), a value defined at the top level of such a region, a value that dominates the `AffineScope` operation around the use, a constant, the result of a pure operation (one with no side effects) whose operands are all valid symbols, or the size of a memref dimension under certain conditions. A dim may be bound to anything a symbol may, and also to the loop variable of an enclosing `affine.for` or `affine.parallel`, or to the result of an `affine.apply`.

Two consequences follow. Symbol validity depends on where the value is used, as the documentation points out,[^affine] and on how it was computed. An `arith.addi` of two function arguments is a valid symbol even inside a loop, because it is pure and its operands are symbols. A `memref.load` inside a loop is not, because a load is not pure and it is not at the top level of the function.

Beyond `affine.for`, the dialect has `affine.if`, whose condition is an **integer set**, a conjunction of affine equalities and inequalities over dims and symbols; `affine.apply`, which evaluates a map to one `index` value; `affine.min` and `affine.max`; and `affine.parallel`, which this chapter meets later.[^affine] A subscript that is not affine, such as `x[i * j]`, is not an error in a program: it is written with `memref.load` instead, and every pass that reasons about affine accesses must treat that load as something it cannot see into.

??? check "A function loads a row count `%n` from a memref. In version A the load sits at the top of the function, before any loop, and an inner `affine.for` runs from 0 to `%n`. In version B the load sits inside an outer `affine.for`, and the inner loop's bound is the same `%n`. Which does `mlir-opt` accept, and why?"

    Version A. A value defined at the top level of the function, which is an `AffineScope`, is a valid symbol whatever operation defined it. In version B, `%n` is defined inside the outer loop by a load, which is not pure, so none of the rules applies. MLIR 18.1.8 rejects version B with `'affine.for' op operand cannot be used as a symbol` (checked on 2026-09-24). This chapter's last example writes that loop in `scf`.

## What the restriction buys

The restriction exists for the analyses. The design note that introduced this form argues that a general control-flow IR can express anything but is awkward for loop transformations, while affine bounds and subscripts "define a closed algebra" in which dependence analysis runs "more efficiently and more reliably".[^simplified] The Affine dialect page makes the same claim for its own techniques, borrowed from polyhedral compilation.[^affine]

Here is what that looks like on a concrete pair of accesses. Take a nest over `1 ≤ i < 5` and `0 ≤ j < 4` whose body stores to `a[i, j]` and loads `a[i - 1, j + 1]`. The store in iteration `(i₁, j₁)` and the load in iteration `(i₂, j₂)` touch the same element exactly when

$$i_1 = i_2 - 1, \qquad j_1 = j_2 + 1,$$

with both iterations inside the bounds. Every term is an affine function of loop variables, so the question "can they touch the same element, and in which order" is a system of integer equalities and inequalities over the loop bounds: the question that [P6](../optimize/p6-dependence-analysis.md#bounds-inequalities-and-exact-tests)'s exact tests answer. Its solutions have `i₂ - i₁ = 1` and `j₂ - j₁ = -1`: the distance vector `(1, -1)` of P6's stencil. No pattern matching on source code was needed, and no guess about aliasing: an `affine.load` names its memref and its subscripts in the operation itself.

The same design note records a second choice. Classic polyhedral compilers, the kind [P9](../optimize/p9-polyhedral-model.md#a-schedule-is-an-affine-map) describes, store each statement's domain and a schedule, and a code generator rebuilds loops at the end. MLIR's affine dialect keeps the loops explicit, as a tree of `affine.for` and `affine.if`; the note calls this a simplified polyhedral form, and observes that a transformation must then generate its new loops itself instead of editing a schedule.[^simplified] So each affine transformation in MLIR is a separate pass that rewrites loops. The Passes page lists them:[^passes] tiling (`-affine-loop-tile`), unrolling and unroll-and-jam (`-affine-loop-unroll`, `-affine-loop-unroll-jam`), fusion (`-affine-loop-fusion`), conversion to parallel loops (`-affine-parallelize`), store-to-load forwarding (`-affine-scalrep`), loop-invariant code motion and others. The rest of this chapter runs three of them.

## Tiling, traced by hand

Tiling, as [P7](../optimize/p7-loop-transformations.md#strip-mining-and-tiling) described it, strip-mines several loops and moves the strip loops outside. Start with a nest where every order is legal: an element-wise add of two 4 × 4 arrays. Each iteration writes a different `c[i, j]` and reads only `a[i, j]` and `b[i, j]`, so no iteration depends on another.

--8<-- "includes/examples/mlir/m6-affine-and-scf/tile.mlir.md"

The option `tile-size=2` asks for tiles of 2 in every loop of the band.[^passes] Read the output from the outside in.

1. `affine.for %arg3 = 0 to 4 step 2` and `affine.for %arg4 = 0 to 4 step 2` are the **tile loops**. They take the values 0 and 2, so together they pick the top-left corner of each of four tiles: `(0, 0)`, `(0, 2)`, `(2, 0)` and `(2, 2)`.
2. `affine.for %arg5 = #map(%arg3) to #map1(%arg3)` is a **point loop**, one that walks within a tile. `#map` is `(d0) -> (d0)` and `#map1` is `(d0) -> (d0 + 2)`, so for the corner `%arg3 = 2` it runs from 2 to 4, exclusive. The other point loop does the same for `j`.
3. The body is unchanged apart from its variable names: the loads and the store now use the point loops' variables.

Tracing the first two tiles by hand gives `(0,0)`, `(0,1)`, `(1,0)`, `(1,1)`, then `(0,2)`, `(0,3)`, `(1,2)`, `(1,3)`. Figure 2 numbers all sixteen points.

<figure class="vx-figure">
<svg viewBox="0 0 400 320" role="img" aria-label="A 4 by 4 iteration space tiled into four 2 by 2 tiles, visited tile by tile" aria-describedby="m6-f2-desc">
<title id="m6-f2-title">Tiling a 4x4 iteration space with tile size 2</title>
<desc id="m6-f2-desc">A 4 by 4 grid of cells, row i from 0 to 3 top to bottom, column j from 0 to 3 left to right. A heavier border divides the grid into four 2 by 2 tiles: top-left, top-right, bottom-left, bottom-right. Each cell is labelled with the order the tiled loop visits it, 0 through 15, and with its i,j pair. The top-left tile holds cells 0 to 3, the top-right tile 4 to 7, the bottom-left tile 8 to 11, the bottom-right tile 12 to 15; inside each tile the order is again row by row. A highlight moves from cell 0 to cell 15 in that order, looping.</desc>
<text class="vx-text" x="20" y="25">i</text>
<text class="vx-text" x="195" y="25">j</text>
<text class="vx-text-muted" x="117" y="42" text-anchor="middle">0</text>
<text class="vx-text-muted" x="177" y="42" text-anchor="middle">1</text>
<text class="vx-text-muted" x="237" y="42" text-anchor="middle">2</text>
<text class="vx-text-muted" x="297" y="42" text-anchor="middle">3</text>
<text class="vx-text-muted" x="72" y="82">0</text>
<text class="vx-text-muted" x="72" y="142">1</text>
<text class="vx-text-muted" x="72" y="202">2</text>
<text class="vx-text-muted" x="72" y="262">3</text>
<rect class="vx-box-strong" x="84" y="44" width="126" height="126" rx="4"/>
<rect class="vx-box-strong" x="204" y="44" width="126" height="126" rx="4"/>
<rect class="vx-box-strong" x="84" y="164" width="126" height="126" rx="4"/>
<rect class="vx-box-strong" x="204" y="164" width="126" height="126" rx="4"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 16">
<rect class="vx-box" x="90" y="50" width="54" height="54" rx="3"/>
<text class="vx-mono" x="117" y="73" text-anchor="middle">0</text>
<text class="vx-text-muted" x="117" y="89" text-anchor="middle">0,0</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 16">
<rect class="vx-box" x="150" y="50" width="54" height="54" rx="3"/>
<text class="vx-mono" x="177" y="73" text-anchor="middle">1</text>
<text class="vx-text-muted" x="177" y="89" text-anchor="middle">0,1</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 16">
<rect class="vx-box" x="210" y="50" width="54" height="54" rx="3"/>
<text class="vx-mono" x="237" y="73" text-anchor="middle">4</text>
<text class="vx-text-muted" x="237" y="89" text-anchor="middle">0,2</text>
</g>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 16">
<rect class="vx-box" x="270" y="50" width="54" height="54" rx="3"/>
<text class="vx-mono" x="297" y="73" text-anchor="middle">5</text>
<text class="vx-text-muted" x="297" y="89" text-anchor="middle">0,3</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 16">
<rect class="vx-box" x="90" y="110" width="54" height="54" rx="3"/>
<text class="vx-mono" x="117" y="133" text-anchor="middle">2</text>
<text class="vx-text-muted" x="117" y="149" text-anchor="middle">1,0</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 16">
<rect class="vx-box" x="150" y="110" width="54" height="54" rx="3"/>
<text class="vx-mono" x="177" y="133" text-anchor="middle">3</text>
<text class="vx-text-muted" x="177" y="149" text-anchor="middle">1,1</text>
</g>
<g class="vx-seq" style="--vx-i: 6; --vx-n: 16">
<rect class="vx-box" x="210" y="110" width="54" height="54" rx="3"/>
<text class="vx-mono" x="237" y="133" text-anchor="middle">6</text>
<text class="vx-text-muted" x="237" y="149" text-anchor="middle">1,2</text>
</g>
<g class="vx-seq" style="--vx-i: 7; --vx-n: 16">
<rect class="vx-box" x="270" y="110" width="54" height="54" rx="3"/>
<text class="vx-mono" x="297" y="133" text-anchor="middle">7</text>
<text class="vx-text-muted" x="297" y="149" text-anchor="middle">1,3</text>
</g>
<g class="vx-seq" style="--vx-i: 8; --vx-n: 16">
<rect class="vx-box" x="90" y="170" width="54" height="54" rx="3"/>
<text class="vx-mono" x="117" y="193" text-anchor="middle">8</text>
<text class="vx-text-muted" x="117" y="209" text-anchor="middle">2,0</text>
</g>
<g class="vx-seq" style="--vx-i: 9; --vx-n: 16">
<rect class="vx-box" x="150" y="170" width="54" height="54" rx="3"/>
<text class="vx-mono" x="177" y="193" text-anchor="middle">9</text>
<text class="vx-text-muted" x="177" y="209" text-anchor="middle">2,1</text>
</g>
<g class="vx-seq" style="--vx-i: 12; --vx-n: 16">
<rect class="vx-box" x="210" y="170" width="54" height="54" rx="3"/>
<text class="vx-mono" x="237" y="193" text-anchor="middle">12</text>
<text class="vx-text-muted" x="237" y="209" text-anchor="middle">2,2</text>
</g>
<g class="vx-seq" style="--vx-i: 13; --vx-n: 16">
<rect class="vx-box" x="270" y="170" width="54" height="54" rx="3"/>
<text class="vx-mono" x="297" y="193" text-anchor="middle">13</text>
<text class="vx-text-muted" x="297" y="209" text-anchor="middle">2,3</text>
</g>
<g class="vx-seq" style="--vx-i: 10; --vx-n: 16">
<rect class="vx-box" x="90" y="230" width="54" height="54" rx="3"/>
<text class="vx-mono" x="117" y="253" text-anchor="middle">10</text>
<text class="vx-text-muted" x="117" y="269" text-anchor="middle">3,0</text>
</g>
<g class="vx-seq" style="--vx-i: 11; --vx-n: 16">
<rect class="vx-box" x="150" y="230" width="54" height="54" rx="3"/>
<text class="vx-mono" x="177" y="253" text-anchor="middle">11</text>
<text class="vx-text-muted" x="177" y="269" text-anchor="middle">3,1</text>
</g>
<g class="vx-seq" style="--vx-i: 14; --vx-n: 16">
<rect class="vx-box" x="210" y="230" width="54" height="54" rx="3"/>
<text class="vx-mono" x="237" y="253" text-anchor="middle">14</text>
<text class="vx-text-muted" x="237" y="269" text-anchor="middle">3,2</text>
</g>
<g class="vx-seq" style="--vx-i: 15; --vx-n: 16">
<rect class="vx-box" x="270" y="230" width="54" height="54" rx="3"/>
<text class="vx-mono" x="297" y="253" text-anchor="middle">15</text>
<text class="vx-text-muted" x="297" y="269" text-anchor="middle">3,3</text>
</g>
</svg>
<figcaption>Figure 2. The iteration space of <code>add2d</code> after <code>-affine-loop-tile</code> with tile size 2, traced by hand from the pass's output. The number in each cell is the visiting order; heavy borders mark the four tiles. Untiled, the order would run 0 to 3 across row 0, then row 1, and so on.</figcaption>
</figure>

No value changes: every `c[i, j]` still receives `a[i, j] + b[i, j]`. Only the order does, and for this nest no order matters. Without `tile-size` or `tile-sizes`, the pass picks sizes itself from a simple memory-footprint model, which its `-cache-size` option sizes in KiB;[^passes] the source calls the model simple and marks it as one to evolve.[^looptiling] [P8](../optimize/p8-cache-blocking.md) is where sizes are chosen properly.

Four does not divide by every tile size. Run the same file with `tile-size=3` and the output changes in three places, left blank here:

```mlir
#map = affine_map<(d0) -> (d0)>
#map1 = affine_map<(d0) -> (____, ____)>
...
    affine.for %arg3 = 0 to 4 step ____ {
      affine.for %arg4 = 0 to 4 step 3 {
        affine.for %arg5 = #map(%arg3) to ____ #map1(%arg3) {
          affine.for %arg6 = #map(%arg4) to min #map1(%arg4) {
```

??? check "Fill the blanks. How many points does the tile whose corner is `(3, 3)` hold, and how many does the tile at `(0, 3)` hold?"

    `#map1` is `(d0) -> (d0 + 3, 4)`, the step is 3, and the missing keyword is `min`: a point loop runs to the smaller of three past its corner and the array's end. An upper bound with two results needs `min`, as `affine.for`'s syntax requires.[^affine] The tile at `(3, 3)` holds one point, `(3, 3)`; the tile at `(0, 3)` holds three, `(0, 3)`, `(1, 3)` and `(2, 3)`. This is the output of MLIR 18.1.8 (checked on 2026-09-24). The `-separate` option asks the pass to split full tiles from partial ones.[^passes]

## When tiling is illegal, and who notices

The add had no dependences, so any tile order was safe. P6's stencil is the standard nest where tiling is not: each iteration reads the element that the previous row wrote one column to the right. Its distance vector is `(1, -1)`, and P7's condition for tiling fails because the band has a negative entry that no outer loop carries.

`-affine-loop-tile` does check. Its source, at the LLVM 18.1.8 release, has a function that looks for any negative dependence component along the loops being tiled; a comment attributes the condition to Irigoin and Triolet and says tiles are run in lexicographic order.[^looptiling] When the check fails, the pass leaves the band alone and emits a remark. The next example holds two nests that break the condition, and runs the pass with `--verify-diagnostics`, so the expected remark is part of the test.

--8<-- "includes/examples/mlir/m6-affine-and-scf/tile_legality.mlir.md"

`@spread` reads `a[i - 1, 2j + 1]`, so the distance along `j` is `j - (2j + 1) = -j - 1`: negative, and different for each `j`. The pass refuses with `tiling code is illegal due to dependences`, and `--verify-diagnostics` confirms the remark appears on the outer loop.

`@skew` is the plain stencil, and the pass tiles it without a word. Figure 3 shows the damage.

<figure class="vx-figure">
<svg viewBox="0 0 560 390" role="img" aria-label="The skew stencil's 4 by 4 iteration space cut into 2 by 2 tiles, with two dependences running backward from a later tile to an earlier one" aria-describedby="m6-f3-desc">
<title id="m6-f3-title">Why the skew stencil cannot be tiled as written</title>
<desc id="m6-f3-desc">A 4 by 4 grid of points, rows i from 1 to 4 top to bottom and columns j from 0 to 3 left to right, cut into four 2 by 2 tiles numbered 1 to 4 in the order the tiled loops run them: 1 top-left, 2 top-right, 3 bottom-left, 4 bottom-right. Nine arrows show the stencil's dependence, each from the point that writes a[i - 1, j + 1] to the point (i, j) that reads it, one row down and one column left. Seven arrows stay within a tile or go to a later tile and are drawn as plain lines. Two arrows, from (1, 2) to (2, 1) and from (3, 2) to (4, 1), leave tile 2 for tile 1 and tile 4 for tile 3: the reader's tile has already finished before the writer's tile starts. These two are drawn as animated dashed lines.</desc>
<defs>
<marker id="m6-f3-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker>
</defs>
<text class="vx-text-muted" x="140" y="28" text-anchor="middle">j = 0</text>
<text class="vx-text-muted" x="240" y="28" text-anchor="middle">j = 1</text>
<text class="vx-text-muted" x="340" y="28" text-anchor="middle">j = 2</text>
<text class="vx-text-muted" x="440" y="28" text-anchor="middle">j = 3</text>
<text class="vx-text-muted" x="50" y="74" text-anchor="middle">i = 1</text>
<text class="vx-text-muted" x="50" y="154" text-anchor="middle">i = 2</text>
<text class="vx-text-muted" x="50" y="234" text-anchor="middle">i = 3</text>
<text class="vx-text-muted" x="50" y="314" text-anchor="middle">i = 4</text>
<rect class="vx-box" x="95" y="40" width="190" height="150" rx="6"/>
<rect class="vx-box" x="295" y="40" width="190" height="150" rx="6"/>
<rect class="vx-box" x="95" y="200" width="190" height="150" rx="6"/>
<rect class="vx-box" x="295" y="200" width="190" height="150" rx="6"/>
<text class="vx-text-accent" x="275" y="54" text-anchor="end">tile 1</text>
<text class="vx-text-accent" x="475" y="54" text-anchor="end">tile 2</text>
<text class="vx-text-accent" x="275" y="214" text-anchor="end">tile 3</text>
<text class="vx-text-accent" x="475" y="214" text-anchor="end">tile 4</text>
<path class="vx-line" d="M232 76 L148 144" marker-end="url(#m6-f3-head)"/>
<path class="vx-flow" d="M332 76 L248 144" marker-end="url(#m6-f3-head)"/>
<path class="vx-line" d="M432 76 L348 144" marker-end="url(#m6-f3-head)"/>
<path class="vx-line" d="M232 156 L148 224" marker-end="url(#m6-f3-head)"/>
<path class="vx-line" d="M332 156 L248 224" marker-end="url(#m6-f3-head)"/>
<path class="vx-line" d="M432 156 L348 224" marker-end="url(#m6-f3-head)"/>
<path class="vx-line" d="M232 236 L148 304" marker-end="url(#m6-f3-head)"/>
<path class="vx-flow" d="M332 236 L248 304" marker-end="url(#m6-f3-head)"/>
<path class="vx-line" d="M432 236 L348 304" marker-end="url(#m6-f3-head)"/>
<circle class="vx-dot" cx="140" cy="70" r="7"/>
<circle class="vx-dot" cx="240" cy="70" r="7"/>
<circle class="vx-dot" cx="340" cy="70" r="7"/>
<circle class="vx-dot" cx="440" cy="70" r="7"/>
<circle class="vx-dot" cx="140" cy="150" r="7"/>
<circle class="vx-dot" cx="240" cy="150" r="7"/>
<circle class="vx-dot" cx="340" cy="150" r="7"/>
<circle class="vx-dot" cx="440" cy="150" r="7"/>
<circle class="vx-dot" cx="140" cy="230" r="7"/>
<circle class="vx-dot" cx="240" cy="230" r="7"/>
<circle class="vx-dot" cx="340" cy="230" r="7"/>
<circle class="vx-dot" cx="440" cy="230" r="7"/>
<circle class="vx-dot" cx="140" cy="310" r="7"/>
<circle class="vx-dot" cx="240" cy="310" r="7"/>
<circle class="vx-dot" cx="340" cy="310" r="7"/>
<circle class="vx-dot" cx="440" cy="310" r="7"/>
<text class="vx-text-accent" x="95" y="378">dashed: the reading tile runs before the writing tile</text>
</svg>
<figcaption>Figure 3. <code>@skew</code> tiled 2 × 2. Each arrow runs from the iteration that writes an element to the one that reads it. Two arrows leave a tile for the tile to its left, which the tiled nest has already finished: iteration <code>(2, 1)</code> in tile 1 reads <code>a[1, 2]</code> before tile 2 writes it. The tiled function computes different values from the original.</figcaption>
</figure>

The difference lies in one comparison. In the 18.1.8 source, a dependence component counts as negative only if its lower bound is strictly less than its upper bound and the upper bound is below zero.[^looptiling] A component that is one fixed value, such as the `-1` of `@skew`, has equal bounds and slips through; the ranged `-j - 1` of `@spread` does not. The dependence analysis itself sees `@skew`'s dependence: `-affine-parallelize` on the same function keeps the `i` loop sequential and makes only the `j` loop parallel (checked on 2026-09-24).

Later LLVM versions moved this code, so the lesson is not about one line of one release. It is about where confidence comes from. A pass's legality check is code like any other, and a compiler for a language with Vortex's rules has to test the result of every transformation it asks for.

??? check "Which transformation from P7 and P9 makes the stencil safe to tile, and what does its distance vector become?"

    Skewing: rewrite the inner index as `i + j`, so the distance `(1, -1)` becomes `(1, 0)`. Both entries are non-negative, the band is fully permutable, and rectangular tiles are legal. [P9](../optimize/p9-polyhedral-model.md#tiling-asks-for-more-than-legality) draws the skewed tiles.

## Fusion

**Fusion** merges two loops over the same range into one, so a value produced by the first is consumed in the same iteration instead of a pass later ([P7](../optimize/p7-loop-transformations.md#fusion-and-fission)). `-affine-loop-fusion` combines two strategies, according to its documentation: **producer-consumer fusion**, for a loop that writes a memref the next one reads, and **sibling fusion**, for loops with no dependence between them that read the same memref.[^passes] Where it can, it also shrinks the temporary that carried values between the loops.

Try it on a two-loop function: the first loop stores `x[i] * k` into a temporary `t` from `memref.alloc`, and the second stores `t[i] + y[i]` into `out`. With MLIR 18.1.8, `-affine-loop-fusion` produced one loop, and the temporary shrank to `memref<1xf32>`, written and read in the same iteration; adding `-affine-scalrep`, which forwards stores to loads, removed the temporary entirely (both checked on 2026-09-24). The same input with a `memref.dealloc` of `t` at the end fused the loops but kept all eight elements of `t`. The documentation says fusion shrinks buffers when possible, and whether it was possible depended on something outside the loops. Read the output.

Fusion and tiling never change a floating-point result when they are legal, because a legal transformation runs every dependent pair in the original order. Parallelization is different, as the next section shows.

## Parallel loops that a pass proves

`affine.parallel` is the dialect's parallel loop: a band of loops, each with affine bounds and a constant step, whose iterations may run in parallel.[^affine] It can also produce a result by **reduction**: each iteration yields a value, and the values are combined with a named operation such as `"addf"`. The documentation is explicit about the order: "The order of reduction is unspecified, and lowering may produce any valid ordering."[^affine]

`-affine-parallelize` converts an `affine.for` into a one-dimensional `affine.parallel` when dependence analysis shows its iterations are independent. It has an option, `parallel-reductions`, that also lets it convert loops that carry a reduction; the option is off by default.[^passes] The next example turns it on.

--8<-- "includes/examples/mlir/m6-affine-and-scf/parallelize.mlir.md"

Read `@matmul` first. The `i` and `j` loops became `affine.parallel`: iterations with different `(i, j)` touch different elements of `c`, and the analysis proved it. The `k` loop stayed an `affine.for`. Its iterations all load and store the same `c[i, j]`, a dependence through memory with distance 1 along `k`, so no option could make it parallel. That is decision 56's order kept: each element's sum is still added in increasing `k`, one rounding at a time.

`@total` is the case the option was made for. Its running sum lives in an `iter_args` value, not in memory, and the pass recognized the `arith.addf` feeding `affine.yield` as a reduction. The output is `affine.parallel ... reduce ("addf")`, whose combining order is unspecified, followed by one more `arith.addf` that adds the loop's initial value, `0.0`, to the reduced result. Figure 4 shows what the change allows.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Two ways to add four numbers: the chain that affine.for with iter_args fixes, and a pairwise tree that a parallel reduction permits" aria-describedby="m6-f4-desc">
<title id="m6-f4-title">The order a loop fixes and the order a reduction leaves open</title>
<desc id="m6-f4-desc">Left panel, titled affine.for with iter_args: a chain of four addition nodes. The first adds 0.0 and x0, the second adds that result and x1, the third adds x2, the fourth adds x3, so the sum is ((((0 + x0) + x1) + x2) + x3), one order only. Right panel, titled affine.parallel reduce addf: one ordering a lowering may choose, a tree that adds x0 and x1, adds x2 and x3, adds the two partial sums, and finally adds 0.0 to the result. Each addition node is one rounding. A caption line under the right panel reads: any order the lowering picks; f32 results may differ.</desc>
<text class="vx-text" x="30" y="28">affine.for with iter_args: one order</text>
<text class="vx-text" x="420" y="28">reduce ("addf"): one order a lowering may pick</text>
<line class="vx-line" x1="390" y1="16" x2="390" y2="290"/>
<text class="vx-mono" x="40" y="70">0.0</text>
<text class="vx-mono" x="110" y="70">x0</text>
<rect class="vx-box" x="60" y="86" width="56" height="30" rx="15"/>
<text class="vx-mono" x="88" y="106" text-anchor="middle">+</text>
<line class="vx-line" x1="55" y1="76" x2="80" y2="86"/>
<line class="vx-line" x1="115" y1="76" x2="96" y2="86"/>
<text class="vx-mono" x="160" y="118">x1</text>
<rect class="vx-box" x="110" y="136" width="56" height="30" rx="15"/>
<text class="vx-mono" x="138" y="156" text-anchor="middle">+</text>
<line class="vx-line" x1="96" y1="116" x2="130" y2="136"/>
<line class="vx-line" x1="165" y1="124" x2="146" y2="136"/>
<text class="vx-mono" x="210" y="168">x2</text>
<rect class="vx-box" x="160" y="186" width="56" height="30" rx="15"/>
<text class="vx-mono" x="188" y="206" text-anchor="middle">+</text>
<line class="vx-line" x1="146" y1="166" x2="180" y2="186"/>
<line class="vx-line" x1="215" y1="174" x2="196" y2="186"/>
<text class="vx-mono" x="260" y="218">x3</text>
<rect class="vx-box-accent" x="210" y="236" width="56" height="30" rx="15"/>
<text class="vx-mono" x="238" y="256" text-anchor="middle">+</text>
<line class="vx-line" x1="196" y1="216" x2="230" y2="236"/>
<line class="vx-line" x1="265" y1="224" x2="246" y2="236"/>
<text class="vx-mono" x="440" y="70">x0</text>
<text class="vx-mono" x="500" y="70">x1</text>
<text class="vx-mono" x="580" y="70">x2</text>
<text class="vx-mono" x="640" y="70">x3</text>
<rect class="vx-box" x="452" y="96" width="56" height="30" rx="15"/>
<text class="vx-mono" x="480" y="116" text-anchor="middle">+</text>
<line class="vx-line" x1="450" y1="76" x2="472" y2="96"/>
<line class="vx-line" x1="508" y1="76" x2="488" y2="96"/>
<rect class="vx-box" x="592" y="96" width="56" height="30" rx="15"/>
<text class="vx-mono" x="620" y="116" text-anchor="middle">+</text>
<line class="vx-line" x1="590" y1="76" x2="612" y2="96"/>
<line class="vx-line" x1="648" y1="76" x2="628" y2="96"/>
<rect class="vx-box" x="522" y="156" width="56" height="30" rx="15"/>
<text class="vx-mono" x="550" y="176" text-anchor="middle">+</text>
<line class="vx-line" x1="480" y1="126" x2="540" y2="156"/>
<line class="vx-line" x1="620" y1="126" x2="560" y2="156"/>
<text class="vx-mono" x="640" y="200">0.0</text>
<rect class="vx-box-accent" x="572" y="226" width="56" height="30" rx="15"/>
<text class="vx-mono" x="600" y="246" text-anchor="middle">+</text>
<line class="vx-line" x1="556" y1="186" x2="592" y2="226"/>
<line class="vx-line" x1="650" y1="206" x2="612" y2="226"/>
<text class="vx-text-muted" x="420" y="286">any order the lowering picks; f32 results may differ</text>
</svg>
<figcaption>Figure 4. Four terms of <code>@total</code>, added two ways. Each <code>+</code> is one rounding. The loop on the left fixes one order. The reduction on the right fixes only the set of terms; the tree drawn is one order a lowering may choose, and in <code>f32</code> it can round to a different result.</figcaption>
</figure>

For integers the two orders agree. For `f32`, rounding after each addition makes the result depend on the grouping, and decision 56 forbids the change. That is why the default setting of `parallel-reductions`, off, is the only one a Vortex pipeline can use: with the default, MLIR 18.1.8 leaves `@total` unchanged (checked on 2026-09-24). `--lower-affine` turns the parallel form into an `scf.parallel` with an `scf.reduce` (checked on 2026-09-24), which, as the next sections show, leaves the order open in the same way.

??? check "The same file is run with `-affine-parallelize` and no options. Which loops become `affine.parallel`, and could any pass option make `@matmul`'s `k` loop parallel?"

    In `@matmul`, `i` and `j` become parallel, exactly as with the option; in `@total` nothing changes, because its loop carries a reduction and the option is off. No option parallelizes `k`: its dependence goes through memory, on `c[i, j]`, and `parallel-reductions` covers only reductions carried in `iter_args` values. To reorder `k`, a transformation would first have to turn the memory accumulation into a value, and then the same decision 56 question returns.

## Loops without the restriction: scf

Not every loop is affine. The **scf dialect**, for structured control flow, keeps loops and conditionals as operations with regions, like affine, but takes ordinary `index` values as bounds and subscripts.[^scf] Its sequential loop is `scf.for`: a lower bound, an upper bound and a positive step, all SSA values; a half-open range; one region whose first argument is the loop variable, followed by one argument per loop-carried value; and a body ending in `scf.yield`.[^scf] Its other operations include `scf.if` and `scf.while`, a general loop whose condition may be any computation, including an early exit.[^scf]

The first function of the next example is the loop that the check question above ruled out: each row's length is loaded from memory inside the outer loop.

--8<-- "includes/examples/mlir/m6-affine-and-scf/scf_loops.mlir.md"

`@ragged_sum` is valid `scf`: `%n` is a plain `index` value, and `scf.for` accepts it as a bound. The price is the one Figure 1 showed. An `scf.for` still has visible structure, a loop variable and a trip count, so passes such as `--loop-invariant-code-motion` can work on it (checked on 2026-09-24). But a subscript such as `%rows[%i, %j]` is now two SSA values, and a pass that wants a dependence has to rediscover that each is an affine function of loop variables, the recovery work that [P9](../optimize/p9-polyhedral-model.md#polyhedral-compilers-in-practice) describes for LLVM's Polly. Nothing in the loop's syntax rules out a subscript read from another array.

When a loop is affine, lowering to `scf` loses information and gains nothing. When it is not, `scf` is the level at which it starts. A compiler for a language with data-dependent loops will have both in one module.

## Parallel loops that the writer promises: scf.parallel and scf.forall

The scf dialect has two parallel loops, and neither is proved by a pass: the IR's writer asserts them.

**`scf.parallel`** is a band of loops whose iteration space, the documentation says, "can be iterated in any order", with a body that may run in parallel; "If there are data races, the behavior is undefined."[^scf] Its terminator, `scf.reduce`, can combine a value from each iteration with a reduction region, and because the iteration order is unspecified, so is the order of combining.[^scf] This is what `affine.parallel` lowers to.

**`scf.forall`** is a parallel loop designed for mapping to hardware. The documentation calls each point of its iteration space a **thread**, and lets an optional mapping attribute assign its dimensions to processing units, such as the dimensions of a GPU grid.[^scf] When its body has side effects, their order across threads is unspecified.[^scf] Its results come from **shared outputs**, `shared_outs`: tensors that all threads write into, through a terminator, `scf.forall.in_parallel`, that says how each thread's piece is inserted.[^scf] The second function in the example, `@scale_rows`, uses the simpler form with memrefs and no results: four threads, one per row, each scaling its row's three elements with a sequential `scf.for`.

`@scale_rows` is correct because row `i` touches only row `i`. Nothing checked that. A function whose thread `i` reads `a[i + 1]` and writes `a[i]`, so that one thread may overwrite what its neighbour reads, passes `mlir-opt` 18.1.8 as readily (checked on 2026-09-24). The promise has the shape of [decision 25](../decisions/references.md#d25)'s rule that a `&mut` argument appears in no other argument of the call: stated where the compiler cannot work it out, and relied on afterwards. A Vortex compiler that emits `scf.forall` must be sure of the independence it asserts, from its own dependence analysis.

[M7](m7-bufferization.md) returns to `shared_outs`, which are destination-passing style inside a loop, and [M10](m10-mlir-for-gpus.md) maps `scf.forall` and `scf.parallel` onto GPU threads.

## Choosing a loop form for Vortex

The chapter's loops, side by side:

| Loop | Bounds and subscripts | Order of iterations | Who guarantees a parallel claim | Risk to decision 56 |
| --- | --- | --- | --- | --- |
| `affine.for` | Affine maps of dims and symbols | Fixed, increasing | Not parallel | None from the loop; passes must keep dependences |
| `affine.parallel` | Affine maps | Any | `-affine-parallelize`, from dependence analysis, or the writer | A `reduce` on `f32` has an unspecified order |
| `scf.for` | Any `index` values | Fixed, increasing | Not parallel | None from the loop |
| `scf.parallel` | Any `index` values | Any | The writer; a race is undefined behavior | `scf.reduce` on `f32` has an unspecified order |
| `scf.forall` | Any `index` values | Any, one thread per point | The writer | Side effects across threads in unspecified order |

Vortex v0.1 sits at the affine end of this table. Every array extent is a constant expression ([decision 11](../decisions/arrays.md#d11)), so a loop that runs over an array's own extents has constant bounds, and a subscript built from loop variables with constant coefficients is an affine map with no symbols at all. The [stage 10 kernel](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for) is such a nest. Its three loops can be emitted as `affine.for`, its loads and stores as `affine.load` and `affine.store`, and its `sum` as an `iter_args` value on the `k` loop.

That choice brings two duties, both from this chapter. The compiler, not the pass, must decide which bands may be tiled, because the pass's check has a gap. And it must keep every pass and option that reorders an `f32` reduction out of its pipeline, because MLIR treats that reordering as a legal choice and Vortex does not. Figure 1's lesson runs the other way too: whatever the compiler cannot express as affine, such as a subscript read from another array, drops to `scf`, and the analyses drop with it.

## For Vortex

!!! vortex "Exercise"

    **Build** an affine path in the MLIR-emitting tool from [M2's exercise](m2-reading-mlir.md#for-vortex), extended in [M5's](m5-structured-ops.md#for-vortex): for loop nests whose bounds are constants and whose subscripts are affine in the loop variables, emit `affine.for`, `affine.load` and `affine.store`, and let the tool ask `mlir-opt` to tile them, but only where your own analysis says tiling is legal.

    1. **On paper, before any code:** for the stage 10 kernel, each loop, its bounds, each subscript written as an affine map, and where `sum` lives. Decide whether you emit affine loops directly from your IR or through M5's linalg path and `--convert-linalg-to-affine-loops`, and write down what each choice gives you and costs.
    2. **An affine gate.** Before emitting an affine operation, the tool checks that every bound and subscript is an affine function of loop variables and constants. A nest that fails falls back to the `scf` path from M2, and the tool writes a remark naming the construct that failed.
    3. **A tiling gate.** The tool decides which bands to tile from the dependence analysis you built in [P6](../optimize/p6-dependence-analysis.md#for-vortex), using P7's condition for a fully permutable band, and passes tile sizes to `-affine-loop-tile` only for those bands. It never relies on the pass's own check. Tile sizes come from the command line.
    4. **A decision 56 gate.** Your tool's pipeline may not contain `-affine-parallelize` with `parallel-reductions=true`, or any other pass you have not checked for reordering of floating-point reductions. Keep the list of passes you allow, and the reason for each, in a file beside the tool.
    5. **Not yet:** lowering to LLVM or running the code ([M4](m4-dialect-conversion.md)), choosing tile sizes by a model or by search ([P8](../optimize/p8-cache-blocking.md), [P15](../optimize/p15-choosing-parameters.md)), fusion across functions, transform-dialect schedules ([M9](m9-transform-dialect.md)), and parallel or GPU loops ([M10](m10-mlir-for-gpus.md)).

    **Proof that it works:**

    - The affine output for the stage 10 kernel and one other shape passes `mlir-opt` with no options, and so does each tiled version at two tile sizes that divide the extents and one that does not.
    - An order test: a small script, outside the compiler, that interprets the untiled and tiled affine nests for a 4 × 4 × 4 kernel and records the sequence of `k` values each `(row, column)` element's accumulation sees. The sequences must match exactly.
    - A refusal test: P7's [`wave`](../optimize/p7-loop-transformations.md#skewing-and-the-unimodular-view) nest, or P6's stencil written in Vortex, produces a remark naming the dependence and its distance, and no tiled output. Keep a note beside it that `-affine-loop-tile` in MLIR 18.1.8 alone would have tiled it.
    - A canary for the decision 56 gate: add `parallel-reductions=true` to the pipeline for a program with an `f32` row sum, and confirm that a test reading the output fails because it finds a `reduce ("addf")`.
    - A fallback test: a nest with a subscript loaded from another array is emitted as `scf`, with a remark.

## Key ideas

!!! recap "Questions you can now answer"

    - **What makes a bound or subscript affine?** It is a sum of dims and symbols times constants, plus a constant, with `floordiv`, `ceildiv` and `mod` by positive constants allowed.
    - **What decides whether a value may be a symbol?** Where it is used and how it was computed: arguments and top-level values of the function, constants, and pure operations on symbols qualify; a load inside a loop does not.
    - **What does the affine restriction buy?** Dependence questions become systems of affine constraints that passes can solve from the IR, so tiling, fusion and parallelization can run as passes.
    - **Does `-affine-loop-tile` in MLIR 18.1.8 refuse every illegal tiling?** No. Its check misses a negative dependence component that is one fixed value, so it tiles the stencil `a[i, j] = a[i - 1, j + 1]` wrongly.
    - **Which option of `-affine-parallelize` breaks decision 56, and why?** `parallel-reductions=true`, which turns an `f32` sum into a `reduce ("addf")` whose order is unspecified.
    - **When does a loop belong in `scf`?** When a bound or subscript is not affine, for example read from memory inside the nest, or when control flow such as a data-dependent exit has no affine form.
    - **Who guarantees that an `scf.forall` or `scf.parallel` is safe to run in parallel?** The writer of the IR; the verifier accepts a racy loop.

## Where this comes back

!!! next "You will use this again in"

    - [M7. Bufferization](m7-bufferization.md): *scf.forall*, *shared outputs*, *destination-passing style*
    - [M8. Vectorization in MLIR](m8-vectorization.md): *affine loops*, *reduction order*
    - [M9. Schedules as IR: the transform dialect](m9-transform-dialect.md): *tile*, *fuse*, *legality*
    - [M10. MLIR for GPUs](m10-mlir-for-gpus.md): *scf.parallel*, *scf.forall*, *mapping*
    - [P8. Cache blocking](../optimize/p8-cache-blocking.md): *tile size*, *partial tile*
    - [P9. The polyhedral model](../optimize/p9-polyhedral-model.md): *affine map*, *dims and symbols*, *tiling legality*
    - [G10. The GPU matmul ladder](../gpu/g10-matmul-ladder.md): *tile*, *iteration space*, *reduction dimension*
    - [O12. Testing an optimizer](../optimize/o12-testing-optimizers.md): *legality check*, *differential test*

## Sources and further reading

Read the Affine dialect page's sections on dims, symbols and their restrictions with `suffix_sum.mlir` open, then the entries for `affine.for` and `affine.parallel`.[^affine] The design note on the simplified polyhedral form explains why MLIR keeps loops explicit instead of storing schedules.[^simplified] The SCF page's entries for `scf.parallel` and `scf.forall` are worth reading in full before M7 and M10.[^scf] For how a real pass checks tiling legality, and where it can go wrong, read `checkTilingLegality` in the 18.1.8 source.[^looptiling]

[^affine]: MLIR Project, "'affine' Dialect", sections "Dimensions and Symbols", "Restrictions on Dimensions and Symbols", "Affine Expressions", "Affine Maps" and "Integer Sets", and the entries `affine.for`, `affine.if`, `affine.load` and `affine.parallel`. <https://mlir.llvm.org/docs/Dialects/Affine/>
[^scf]: MLIR Project, "'scf' Dialect", entries `scf.for`, `scf.while`, `scf.parallel`, `scf.reduce` and `scf.forall`. <https://mlir.llvm.org/docs/Dialects/SCFDialect/>
[^passes]: MLIR Project, "Passes", entries `-affine-loop-tile`, `-affine-loop-fusion`, `-affine-loop-unroll-jam`, `-affine-parallelize`, `-affine-scalrep`, `-affine-raise-from-memref`, `-raise-scf-to-affine` and `-lower-affine`. <https://mlir.llvm.org/docs/Passes/>
[^simplified]: Chris Lattner, "MLIR: The case for a simplified polyhedral form", MLIR Rationale, sections on the goals of the representation and "Proposal: Simplified Polyhedral Form". <https://mlir.llvm.org/docs/Rationale/RationaleSimplifiedPolyhedralForm/>
[^looptiling]: LLVM Project, `mlir/lib/Dialect/Affine/Transforms/LoopTiling.cpp` at tag `llvmorg-18.1.8`, functions `checkTilingLegality`, `getTileSizes` and `LoopTiling::runOnOperation`. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/mlir/lib/Dialect/Affine/Transforms/LoopTiling.cpp>
