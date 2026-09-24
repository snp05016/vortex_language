# M6. Loops: affine and scf

<p class="page-intro">MLIR splits loops into two dialects with two different promises: affine restricts bounds and subscripts so a compiler can prove things about them exactly, and scf drops that restriction to say whatever needs saying. This chapter reads both, tiles a loop nest, and asks where Vortex's own fixed-shape, sequential-sum kernel would sit.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 15 minutes · Builds on: [M5. Structured ops: linalg, tensor and memref](m5-structured-ops.md), [P7. Loop transformations](../optimize/p7-loop-transformations.md)</p>

???+ remember "Before you start, remember"

    ??? question "What replaces a phi in MLIR?"

        A block argument. Where two control paths meet, each branch passes a value to the block it jumps to, instead of a phi listing one incoming value per predecessor.

        Introduced in [M2. Reading MLIR](m2-reading-mlir.md).

    ??? question "What does a dependence's direction vector record, and when may loops be reordered?"

        For two accesses to one location, loop by loop from the outermost: whether the second runs in a later iteration (`<`), the same one (`=`) or an earlier one (`>`). A reordering of the loops is legal when no dependence, rewritten for the new order, has `>` as its first entry that is not `=`.

        Introduced in [P6. Dependence analysis](../optimize/p6-dependence-analysis.md).

    ??? question "In what order are the elements of a `[f32; 4, 3]` array stored?"

        Row after row, the last index varying fastest: `[0,0]`, `[0,1]`, `[0,2]`, `[1,0]`, `[1,1]`, `[1,2]`, and so on.

        Introduced in [Arrays and shapes, decision 43](../decisions/arrays.md#d43).

    ??? question "May a Vortex compiler turn `sum += a[row, k] * b[k, column]` into one fused multiply-add, or run the additions out of order?"

        No. Each `f32` operation is one IEEE 754 operation, rounded to nearest with ties to even, and a compiler must not contract, reassociate or reorder them.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Explain why the affine dialect restricts loop bounds and array subscripts to affine functions of dims and symbols, and what exact analysis that restriction buys.
    - Read and write `affine.for` loops with symbols and loop-carried values, and say why a value's position in the loop nest, not what computed it, decides whether it may be a symbol.
    - Use `mlir-opt` to tile an affine loop nest, and predict which reorderings a tiling pass can and cannot introduce.
    - Recognize when a loop belongs in `scf` instead of `affine`, and distinguish the sequential `scf.for` from the parallel `scf.forall`.

## A loop nest is a set of points

Take a function that adds two fixed-size arrays element by element, `c[i, j] = a[i, j] + b[i, j]`, over a 4-by-4 shape. Written as two nested loops, `i` from 0 to 3 and `j` from 0 to 3 inside it, the function visits sixteen `(i, j)` pairs, once each, in the order `(0,0)`, `(0,1)`, `(0,2)`, `(0,3)`, `(1,0)`, and so on to `(3,3)`. That set of pairs, together with the order the loops visit them in, is the **iteration space**: every point a loop nest touches, seen as one object instead of as a sequence of individual passes through the code.

Two questions about an iteration space come up constantly when transforming loops. First, does a transformation change which points exist, or only the order they are visited in? Second, for two accesses that touch the same memory, does the transformation put one before the other where it used to come after, or the reverse? [P7](../optimize/p7-loop-transformations.md) answers the second question with direction vectors, computed by a dependence test. MLIR's two loop dialects differ in how willing they are to let that test be exact.

## The affine dialect: bounds and subscripts as linear functions

The **affine dialect** is MLIR's structured-loop dialect for the case where every loop bound and every array subscript is an **affine function**: a sum of the loop's own induction variables and a fixed set of other values, each multiplied only by a compile-time constant, plus a constant. No multiplying two induction variables together, no calling a function inside a bound, no indexing through a second array to find an index.

The "other values" an affine expression may use split into two kinds. A **dim** is a value that changes as the affine computation runs: an enclosing loop's own induction variable is the usual example. A **symbol** is a value that does not change for the whole affine computation, even though its value is not known until the program runs. Here is a function that sums a fixed 8-element row from a runtime-chosen starting point to the end:

--8<-- "includes/examples/mlir/m6-affine-and-scf/suffix_sum.mlir.md"

`%start` is a function argument, so `affine.for %i = %start to 8` is legal: the loop's lower bound is the symbol `%start`, its upper bound the constant `8`. Inside the loop, `%acc` is a **loop-carried value**: each turn of the loop receives the running sum as an operand and produces the next one with `affine.yield`, the same mechanism `scf.for`'s `iter_args` uses later in this chapter, and the one [M2](m2-reading-mlir.md) introduced for block arguments in general. No memory cell holds the sum between iterations; the SSA value itself is threaded through.

Why bother with a dialect this restricted? Because the restriction is what makes the questions from the previous section answerable exactly, not approximately. [P6](../optimize/p6-dependence-analysis.md) builds a dependence test for arbitrary array code, which has to fall back on conservative answers whenever a subscript is too complicated to pin down. Affine expressions never get that complicated: an affine function of a loop's induction variables and symbols is exactly the kind of object that integer linear arithmetic can reason about precisely, so an affine dependence test can say "these two accesses never touch the same element" and be certain, not merely unable to find a counterexample. That certainty is what lets a compiler apply transformations like tiling, fusion and unroll-and-jam automatically, instead of only under a programmer's promise. [P9](../optimize/p9-polyhedral-model.md) covers the underlying machinery, known in the literature as polyhedral compilation, in depth.

??? question "Two functions each compute a bound from a value read out of a memref. One reads it before any `affine.for`; the other reads it inside an outer `affine.for` and uses the result as an inner loop's bound. Which is legal?"

    The first. A symbol only has to be defined outside every `affine.for` and `affine.if` in the nest around it; nothing requires it to be a compile-time constant or even a pure computation, because a value defined before the nest starts runs exactly once, before any iteration, so it is invariant by construction. A value read inside the outer loop is computed once per outer iteration, not once total, so it fails that test regardless of what operation produced it. Running the second version through `mlir-opt` 18.1.8 (checked locally) rejects it: `'affine.for' op operand cannot be used as a symbol`.

## Transformations that exactness buys: tiling

**Tiling** a loop nest splits each loop into two: an outer loop that steps by a fixed tile size, and an inner loop that walks across one tile. It changes the order points are visited in, from row by row across the whole array to tile by tile, each tile finished before the next starts. Because affine's dependence analysis is exact, `mlir-opt` can apply this as a pass instead of asking a programmer to rewrite the loops by hand. Take the element-wise add from the first section, expressed with `affine.for`:

--8<-- "includes/examples/mlir/m6-affine-and-scf/tile.mlir.md"

Every iteration of this loop writes a distinct `c[i, j]` and reads only `a[i, j]` and `b[i, j]`, so no iteration depends on another: reordering them changes nothing about what gets computed. `mlir-opt`'s `-affine-loop-tile` pass, given a tile size of 2, turns the two loops into four: an outer `i` loop and outer `j` loop that each step by 2, and an inner `i` loop and inner `j` loop, each bounded by an affine map (`d0` to `d0 + 2`), that walk across one 2-by-2 tile. Figure 1 shows the sixteen points in the order the tiled version visits them.

<figure class="vx-figure">
<svg viewBox="0 0 400 320" role="img" aria-label="A 4 by 4 iteration space tiled into four 2 by 2 tiles, visited tile by tile" aria-describedby="m6-f1-desc">
<title id="m6-f1-title">Tiling a 4x4 iteration space with tile size 2</title>
<desc id="m6-f1-desc">A 4 by 4 grid of cells, row i from 0 to 3 top to bottom, column j from 0 to 3 left to right. A heavier border divides the grid into four 2 by 2 tiles: top-left, top-right, bottom-left, bottom-right. Each cell is labelled with the order the tiled loop visits it, 0 through 15, and with its i,j pair. The top-left tile holds cells 0 to 3, the top-right tile 4 to 7, the bottom-left tile 8 to 11, the bottom-right tile 12 to 15; inside each tile the order is again row by row. A highlight moves from cell 0 to cell 15 in that order, looping.</desc>
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
<figcaption>Figure 1. The 4-by-4 iteration space of <code>add2d</code>, tiled at tile size 2 by <code>mlir-opt -affine-loop-tile</code> and traced by hand from the pass's own output. The number in each cell is the visiting order; the heavy borders mark the four tiles. Untiled, the order would be 0, 1, 2, 3 across row 0, then row 1, and so on.</figcaption>
</figure>

??? question "Does the tiled `add2d` compute a different value for any `c[i, j]` than the untiled version? What does change?"

    No value changes: every `c[i, j]` still receives `a[i, j] + b[i, j]`, because tiling never removes, adds or aliases an access, only reorders independent ones. What changes is the order the sixteen points are visited in: row by row, versus tile by tile as Figure 1 shows.

??? question "If `-affine-loop-tile` tiled only a reduction loop's dimension, with no interchange with another loop, could the sum's floating-point result change from decision 56's point of view?"

    Not from tiling alone. Tiling a single dimension splits its index into an outer stride and an inner offset; for any one fixed value of the surrounding indices, that dimension is still visited in increasing order, 0, 1, 2, and so on, just grouped into runs. The additions still happen in the same sequence. What would change the order is combining tiling with an interchange that moves the reduction dimension past another loop, or replacing it with a parallel loop such as `scf.forall`, which makes no promise about visiting order at all. That is one reason a tiled matmul kernel usually tiles its two independent output dimensions and leaves the reduction dimension both innermost and sequential.

## scf: giving up exactness for generality

Not every loop's bound is invariant in the way affine requires. Consider a ragged table: for each of four rows, sum the first `counts[i]` elements, where `counts` is itself an array read at runtime. The inner loop's bound depends on a value loaded inside the outer loop, so it fails the symbol test from the check question above; `affine.for` rejects it. The **scf dialect** (structured control flow) has no such restriction:

--8<-- "includes/examples/mlir/m6-affine-and-scf/ragged_sum.mlir.md"

`scf.for`'s bound, step and `iter_args` work exactly like `affine.for`'s, because they are the same mechanism; only the restriction on what the bound may be is gone. `%n`, loaded fresh on every outer iteration, is a perfectly ordinary index value here, not a symbol at all. The cost of that freedom is what the previous section bought: `scf`'s dependence questions fall back to the general, sometimes-approximate analysis [P6](../optimize/p6-dependence-analysis.md) builds, because nothing in the loop's own syntax rules out a subscript the compiler cannot pin down.

## Parallel loops: scf.parallel and scf.forall

A sequential `scf.for` says nothing about whether its iterations could run out of order or at once; it only says what order they do run in. **`scf.forall`** says more: it declares that its iterations are independent, so nothing may read or write another iteration's data. Scaling every row of a fixed array by a constant, in place, is a natural fit:

--8<-- "includes/examples/mlir/m6-affine-and-scf/scale_rows.mlir.md"

The outer `scf.forall (%i) in (4)` promises row `%i`'s body touches nothing that another row's body touches; the inner `scf.for` over the three columns is ordinary sequential iteration, since nothing here needs it to be otherwise. `mlir-opt`'s verifier does not, and in general cannot, check that promise: nothing stops a badly generated `scf.forall` body from reading a neighboring row, and the pass would still accept it. The promise is the writer's, the same shape as [decision 25](../decisions/references.md#d25)'s promise that a `&mut` argument shares storage with none of the call's other arguments: stated once, at the point where the compiler cannot otherwise know it, and relied on afterward.

`scf.parallel` is the dialect's older parallel loop; `scf.forall` is newer and pairs each iteration with a slot in a **shared output** it writes into, a structure close to Vortex's own habit of writing results through a `&mut` parameter instead of returning them. [M7](m7-bufferization.md) is where that structure, called destination-passing style, becomes the chapter's subject.

??? question "What does `scf.forall` promise that `scf.for` does not, and who is responsible for that promise being true?"

    That its iterations are independent: nothing threads a value between them the way `iter_args` threads one through `scf.for`, and nothing may alias across them. The promise belongs to whoever produces the IR, not to `mlir-opt`'s verifier, which accepts a `scf.forall` whose body secretly violates it just as readily as one that does not.

## Choosing affine or scf

Prefer `affine` whenever every bound and subscript is genuinely invariant in the sense this chapter defined: built from induction variables, constants and values fixed outside the loop nest. That single grammar decision is what enables `-affine-loop-tile`, `-affine-loop-fusion` and `-affine-loop-unroll-jam` as passes a compiler can run on its own authority, and what a dependence test can answer exactly instead of conservatively. Reach for `scf` once a bound is truly data-dependent, once the loop needs a data-dependent step or an early exit (`scf.while` has no affine counterpart at all), or once the computation is parallel in a way `affine.parallel` does not cover.

Vortex's arrays are fixed-shape: every extent is a compile-time constant, fixed by [the array decisions](../decisions/arrays.md), never a value computed at runtime. A loop nest whose bounds are exactly an array's own extents, and whose subscripts are exactly its own loop variables, is affine by construction, with no symbols needed at all. That is not an accident this chapter is pointing out for its own sake: it is what a future MLIR lowering for Vortex would get for free, for every kernel that indexes arrays with nothing more than its own loop variables, from a language decision that had nothing to do with MLIR when it was made.

## For Vortex

!!! vortex "Exercise"

    **Build** an affine-emitting mode for the MLIR tool [M2](m2-reading-mlir.md#for-vortex)'s exercise asked you to build: given the same checked stage 10 kernel, emit its three nested loops as `affine.for`, its array reads and writes as `affine.load`/`affine.store`, and its running sum as an `iter_args` value threaded through the innermost loop, exactly as `suffix_sum.mlir` threads one through a single loop.

    1. A one-page note, not code, mapping the kernel's three loops and their bounds (each one of the array's own fixed extents), its two `&` inputs and one `&mut` output, and each `f32` operation to the affine construct that will represent it.
    2. A rule your tool checks before emitting any affine op: every value used in a bound or a subscript must be a loop's own induction variable, a compile-time constant, or a value defined at the top level of the function, never inside another loop, matching the symbol rule this chapter's first check question worked through. Decide, and write down, what your tool does when a construct fails that rule (refuse the file, or fall back to the `scf`/`cf` form from M2's exercise) and why.
    3. A way to run the emitted file through `mlir-opt`'s `-affine-loop-tile` pass at a few tile sizes you choose by hand, and confirm each result still passes `mlir-opt` with no options.
    4. A method for choosing a tile size, not an invented number: read your own machine's cache sizes (for example `sysctl hw.perflevel0.l1dcachesize` and `hw.perflevel0.l2cachesize` on macOS, or the files under `/sys/devices/system/cpu/cpu0/cache/` on Linux), and use the reasoning [P8](../optimize/p8-cache-blocking.md) builds to propose a tile size that keeps one tile of each array resident. Write down the machine, the date and the numbers you read.
    5. A decision, justified in your note, about whether to tile the reduction dimension at all, using this chapter's reasoning about a fixed set of outer indices to say whether tiling it alone could touch the sum decision 56 governs.

    **Not yet:** lowering the affine (or tiled affine) output to `scf` or to LLVM IR ([M4](m4-dialect-conversion.md), [M7](m7-bufferization.md)), choosing a tile size automatically or by search instead of by hand ([P15](../optimize/p15-choosing-parameters.md)), fusing this kernel with another operation ([M9](m9-transform-dialect.md) is where that becomes a tool instead of a hand edit), and any change to `src/`.

    **Proof that it works:**

    - The kernel's affine output passes `mlir-opt` with no options.
    - The same file, tiled at two different tile sizes you chose, still passes `mlir-opt`.
    - A hand trace, for a small shape such as 4x4x4, of the untiled and one tiled version visiting the same 64 `(row, k, column)` triples, in the same order for each fixed pair of output indices: the check this chapter's reasoning predicts, confirmed on your own output instead of taken on faith.
    - A written record of the cache sizes you read, the machine, the date, and the tile size you chose from them.
    - A canary: change one array's extent in the emitted file without changing your tool, and confirm `mlir-opt` rejects the now-inconsistent file, so you know the shape checking is real.

## Key ideas

!!! recap "Questions you can now answer"

    - **What must a loop bound or array subscript look like to belong in the affine dialect?** An affine function: a sum of the loop's induction variables and symbols, each multiplied only by a compile-time constant, plus a constant.
    - **What is a symbol, and what decides whether a value may be one?** A value fixed for the whole affine computation. What decides it is where the value is defined, never inside any `affine.for` or `affine.if` in the nest, not what operation produced it.
    - **What does affine's restricted grammar buy that general loop code does not get?** Exact, decidable dependence and containment analysis, so transformations such as tiling, fusion and unroll-and-jam can be checked legal by the compiler instead of only assumed legal by a programmer.
    - **Does tiling a loop, by itself, reorder its iterations?** It changes which group of points is visited when, but for a fixed value of the surrounding indices, one tiled dimension is still visited in the same increasing order it always was.
    - **When does a loop belong in `scf` instead of `affine`?** When a bound or a subscript depends on a value computed inside the loop nest, or when the computation needs control flow, such as an early exit, that affine has no equivalent for.
    - **What does `scf.forall` promise that `scf.for` does not, and who guarantees it?** That its iterations are independent. The writer of the IR guarantees it; the verifier accepts a `scf.forall` whether the promise holds or not.

## Where this comes back

!!! next "You will use this again in"

    - [M7. Bufferization](m7-bufferization.md): *scf.forall*, *shared output*, *destination-passing style*
    - [M9. Schedules as IR: the transform dialect](m9-transform-dialect.md): *tile*, *fuse*, *payload IR*
    - [M10. MLIR for GPUs](m10-mlir-for-gpus.md): *scf.parallel*, *iteration space*, *kernel outlining*
    - [P9. The polyhedral model](../optimize/p9-polyhedral-model.md): *affine function*, *dims and symbols*, *exact dependence*
    - [G10. The GPU matmul ladder](../gpu/g10-matmul-ladder.md): *tile*, *iteration space*, *reduction dimension*
    - [O8. Loops: structure, induction variables and bounds checks](../optimize/o8-loops.md): *induction variable*, *loop-carried value*, *loop bound*

## Sources and further reading

The Affine dialect's own page is the primary reference for the grammar this chapter builds on: read its sections on affine maps, dims and symbols with `suffix_sum.mlir` open.[^affine] The SCF dialect page documents `scf.for`, `scf.parallel` and `scf.forall` directly; its entry for `scf.forall` is worth reading in full once M7 makes destination-passing style concrete.[^scf] The Passes page documents `-affine-loop-tile`, `-affine-loop-fusion` and `-affine-loop-unroll-jam`, including the options this chapter's example used.[^passes]

[^affine]: MLIR Project, "'affine' Dialect". <https://mlir.llvm.org/docs/Dialects/Affine/>
[^scf]: MLIR Project, "'scf' Dialect", entries `scf.for`, `scf.parallel`, `scf.forall`. <https://mlir.llvm.org/docs/Dialects/SCFDialect/>
[^passes]: MLIR Project, "Passes", entries `-affine-loop-tile`, `-affine-loop-fusion`, `-affine-loop-unroll-jam`. <https://mlir.llvm.org/docs/Passes/>
