# M5. Structured ops: linalg, tensor and memref

<p class="page-intro">A structured operation such as linalg.generic states what a computation reads, what it writes and how its iteration space maps onto both, and leaves the loop order to whoever lowers it. This chapter reads that declarative form until you can write one by hand, and asks what it captures of Vortex's fixed shapes, row-major layout, strict rounding and non-aliasing mut output, and what it does not.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 28 minutes · Builds on: [M2. Reading MLIR](m2-reading-mlir.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is a named operation like linalg.matmul, and what does it hide?"

        A common computation with a name of its own. Printing its generic form reveals a region: a block that does the arithmetic, driven by indexing maps that say which element of each operand one step of the computation touches.

        Introduced in [M2. Reading MLIR](m2-reading-mlir.md#a-named-operation-hides-a-region).

    ??? question "What can a memref type record about Vortex's &mut output, and what can't it?"

        It can record the shape and element type of the buffer a function writes into. It cannot record that the buffer overlaps none of the function's other arguments: two memref parameters may alias, and nothing in their types says otherwise.

        Introduced in [M2. Reading MLIR](m2-reading-mlir.md#types-say-what-a-value-is).

    ??? question "What must be true of every f32 operation under decision 56?"

        Each one must be exactly one IEEE 754 operation, rounded to nearest with ties to even. An implementation must not fuse, reassociate or reorder them, even during compilation.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

    ??? question "In what order are the elements of a [f32; 2, 3] array stored?"

        Row after row, the last index varying fastest: [0, 0], [0, 1], [0, 2], [1, 0], [1, 1], [1, 2].

        Introduced in [Arrays and shapes, decision 43](../decisions/arrays.md#d43).

!!! goals "In this chapter"

    - Explain why a `linalg.generic` operation is completely described by its indexing maps, iterator types and region, with no loop order attached.
    - Read a named op such as `linalg.matmul` as a `linalg.generic` with its maps already chosen, and predict that both lower to the same loops.
    - Distinguish a computation on `tensor` values from the same computation on `memref` buffers, and say what each one's `outs` operand means.
    - Recognize which of Vortex's promises a structured op's type and verifier capture, and which ones a Vortex compiler still has to carry itself.
    - Choose indexing maps and iterator types for a small array computation this chapter does not show.

## A row sum, read as one declarative operation

Start smaller than matrix multiplication. This function sums each row of a 3-by-4 matrix into a 3-element vector, writing through its second argument the way a Vortex `&mut` parameter would:

--8<-- "includes/examples/mlir/m5-structured-ops/row_sum_memref.mlir.md"

One operation, `linalg.generic`, does the whole computation. There is no loop in the source: `%in` and `%out` are memrefs, `ins` and `outs` name which is which, and everything else is data describing a single step of the work.

- `indexing_maps = [#map_in, #map_out]` gives one **affine map** per operand: a function from the operation's iteration space to that operand's indices. `#map_in`, `(i, j) -> (i, j)`, says step `(i, j)` touches element `(i, j)` of the input. `#map_out`, `(i, j) -> (i)`, says the same step touches element `i` of the output, dropping `j`.
- `iterator_types = ["parallel", "reduction"]` names what each dimension of the iteration space means: `i` is **parallel**, one independent row at a time, and `j` is **reduction**, folded into a single output element.
- The region, `^bb0(%elem: f32, %acc: f32): ...`, is the step's body. It receives one element from each operand at the current point (`%elem` from the input, `%acc` from the output) and yields the value the output should hold there.

Read together, the three pieces fix the operation's meaning completely: for every `i`, visit every `j`, load `%in[i, j]` and the output's running value, add them, and store the result back at `%out[i]`. The [linalg dialect documentation](https://mlir.llvm.org/docs/Dialects/Linalg/) calls this family **structured operations**: the iteration space and the mapping between it and every operand are explicit parts of the operation, rather than something a later pass has to reconstruct from a loop nest.[^linalg] Figure 1 draws the row sum's iteration space and both projections.

<figure class="vx-figure">
<svg viewBox="0 0 760 420" role="img" aria-label="The row sum's iteration space, projected by two indexing maps onto its input and output" aria-describedby="m5-f1-desc">
<title id="m5-f1-title">Row sum's iteration space, projected onto its input and output</title>
<desc id="m5-f1-desc">A three by four grid of cells labelled in at row i, column j, for i from 0 to 2 and j from 0 to 3. Columns are marked j, reduction, along the top; rows are marked i, parallel, down the left side. To the right, a column of three cells labelled out at 0, out at 1 and out at 2. The four cells of row 1 are highlighted and animate in order from j equals 0 to j equals 3, each sending an arrow into out at 1, showing the reduction folding four input elements into one output element while row 0 and row 2 work the same way independently. The map labelled i comma j to i comma j points at the grid itself; the map labelled i comma j to i points at the arrows converging on the output column.</desc>
<text class="vx-text" x="20" y="24">j (reduction) →</text>
<text class="vx-text" x="20" y="200" transform="rotate(-90 20 200)" text-anchor="middle">i (parallel) ↓</text>
<g>
<rect class="vx-box" x="70" y="40" width="80" height="44" rx="4"/>
<text class="vx-mono" x="110" y="66" text-anchor="middle">in[0,0]</text>
<rect class="vx-box" x="158" y="40" width="80" height="44" rx="4"/>
<text class="vx-mono" x="198" y="66" text-anchor="middle">in[0,1]</text>
<rect class="vx-box" x="246" y="40" width="80" height="44" rx="4"/>
<text class="vx-mono" x="286" y="66" text-anchor="middle">in[0,2]</text>
<rect class="vx-box" x="334" y="40" width="80" height="44" rx="4"/>
<text class="vx-mono" x="374" y="66" text-anchor="middle">in[0,3]</text>
</g>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 4">
<rect class="vx-box-accent vx-pulse" x="70" y="100" width="80" height="44" rx="4"/>
<text class="vx-mono" x="110" y="126" text-anchor="middle">in[1,0]</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4">
<rect class="vx-box-accent vx-pulse" x="158" y="100" width="80" height="44" rx="4"/>
<text class="vx-mono" x="198" y="126" text-anchor="middle">in[1,1]</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4">
<rect class="vx-box-accent vx-pulse" x="246" y="100" width="80" height="44" rx="4"/>
<text class="vx-mono" x="286" y="126" text-anchor="middle">in[1,2]</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 4">
<rect class="vx-box-accent vx-pulse" x="334" y="100" width="80" height="44" rx="4"/>
<text class="vx-mono" x="374" y="126" text-anchor="middle">in[1,3]</text>
</g>
<g>
<rect class="vx-box" x="70" y="160" width="80" height="44" rx="4"/>
<text class="vx-mono" x="110" y="186" text-anchor="middle">in[2,0]</text>
<rect class="vx-box" x="158" y="160" width="80" height="44" rx="4"/>
<text class="vx-mono" x="198" y="186" text-anchor="middle">in[2,1]</text>
<rect class="vx-box" x="246" y="160" width="80" height="44" rx="4"/>
<text class="vx-mono" x="286" y="186" text-anchor="middle">in[2,2]</text>
<rect class="vx-box" x="334" y="160" width="80" height="44" rx="4"/>
<text class="vx-mono" x="374" y="186" text-anchor="middle">in[2,3]</text>
</g>
<rect class="vx-box-strong" x="520" y="100" width="100" height="44" rx="4"/>
<text class="vx-mono" x="570" y="126" text-anchor="middle">out[1]</text>
<rect class="vx-box" x="520" y="40" width="100" height="44" rx="4"/>
<text class="vx-mono" x="570" y="66" text-anchor="middle">out[0]</text>
<rect class="vx-box" x="520" y="160" width="100" height="44" rx="4"/>
<text class="vx-mono" x="570" y="186" text-anchor="middle">out[2]</text>
<path class="vx-flow" style="--vx-i: 0" d="M150,122 C 300,80 420,122 520,122"/>
<path class="vx-flow" style="--vx-i: 1" d="M238,122 C 340,105 440,122 520,122"/>
<path class="vx-flow" style="--vx-i: 2" d="M326,122 C 400,118 460,122 520,122"/>
<path class="vx-flow" style="--vx-i: 3" d="M414,122 L 520,122"/>
<polygon class="vx-arrowhead" points="514,116 520,122 514,128"/>
<text class="vx-text-muted" x="70" y="246">#map_in = affine_map&lt;(i, j) -&gt; (i, j)&gt;</text>
<text class="vx-text-muted" x="70" y="266">every cell maps to itself: the input is read once per step</text>
<text class="vx-text-muted" x="70" y="300">#map_out = affine_map&lt;(i, j) -&gt; (i)&gt;</text>
<text class="vx-text-muted" x="70" y="320">j drops out: every step of a row folds into the same output cell</text>
<text class="vx-text-muted" x="70" y="360">iterator_types = ["parallel", "reduction"]: rows run independently,</text>
<text class="vx-text-muted" x="70" y="378">each row's four steps must combine into one value</text>
</svg>
<figcaption>Figure 1. The row sum's iteration space, one point per (i, j) pair, projected by two indexing maps. Row 1 is highlighted: its four points visit the input once each and all write toward out[1]. Rows 0 and 2 do the same independently, which is what the parallel iterator type promises. Nothing here fixes the order the four points combine in.</figcaption>
</figure>

??? check "Row sum's iterator types are [\"parallel\", \"reduction\"], one entry per dimension of (i, j). Why must j be reduction rather than parallel?"

    Because `#map_out` drops `j`: several steps, one for each `j`, write to the same output element `out[i]`. A dimension that several steps share at the output must be reduction, so the operation's own verifier and every pass that reads it knows those steps combine rather than run independently. If `j` were parallel, the operation would claim four steps could write `out[i]` at once with no rule for combining them, which `linalg.generic`'s verifier rejects.

## Named ops: the same interface, with the maps already chosen

Matrix multiplication is the same idea at one more dimension. Vortex's stage 10 kernel, and M2's reading of it as `linalg.matmul`, computes `c[row, column] += a[row, k] * b[k, column]`[^stage10] over three indices: `row` and `column` are parallel, `k` is reduction. Written as `linalg.generic`, with the three indexing maps M2 already introduced, the file is longer than `linalg.matmul` alone:

--8<-- "includes/examples/mlir/m5-structured-ops/matmul_generic.mlir.md"

Compare it with the named form:

--8<-- "includes/examples/mlir/m5-structured-ops/matmul_named.mlir.md"

`linalg.matmul` is one of the dialect's **named payload-carrying ops**: an operation such as `linalg.matmul` or `linalg.conv_2d` that "adheres to the `linalg.generic` op interface", carrying the same indexing maps, iterator types and region, generated once for the whole family rather than written out at every call site.[^linalg] The examples above lower with the same flag, `--convert-linalg-to-affine-loops`, and produce identical `affine.for` nests apart from the function's own name: `matmul_generic.expected` and `matmul_named.expected` match line for line. That is the point of the interface. A pass that tiles, fuses or vectorizes `linalg.generic` operations, working only from indexing maps, iterator types and a region, handles `linalg.matmul` for free, because underneath it is one.

The dialect names nine transformations this shape is meant to support: progressive buffer allocation, parametric tiling, promotion of an operand to a temporary buffer in fast memory, tiled producer-consumer fusion, mapping iterator types onto parallel and reduction loops, rewriting the region in vector form, lowering to ordinary loops, lowering to a call into a library such as BLAS, and lowering part of an operation into smaller `linalg` operations.[^linalg] None of them run in this chapter: `--convert-linalg-to-affine-loops`, the only pass these examples use, is the seventh item, chosen because a fixed loop nest is the easiest lowering to read against the source. [M6](m6-affine-and-scf.md) reads the loops these operations become, [M8](m8-vectorization.md) reads vectorization, and [M9](m9-transform-dialect.md) reads tiling and fusion, written as IR that schedules other IR.

??? check "matmul_generic.mlir and matmul_named.mlir lower to byte-identical loops apart from the function name. What does that tell you about what linalg.matmul is, and what it is not?"

    It confirms that `linalg.matmul` is sugar: a shorthand for one specific `linalg.generic` (these three indexing maps, this iterator list, this region), not a separate operation with its own lowering. It is not a hint to the compiler to use a particular algorithm or loop order; nothing about the named form makes the lowering faster or different, only shorter to write and easier for a reader to recognize by name.

## Values or buffers: tensor against memref

Every example so far has used `memref`, a reference to a region of memory that a `linalg.generic` op writes into. Structured ops also work on `tensor`, MLIR's value type: unshaped-by-reference data with no address you can take. The tensor documentation states the difference plainly: "a `tensor` is an immutable object."[^tensor] Written on tensors, the row sum becomes:

--8<-- "includes/examples/mlir/m5-structured-ops/row_sum_tensor.mlir.md"

The op still has an `outs` operand, `%init`, but now it also has a result, `-> tensor<3xf32>`, and the function returns that result instead of writing through an argument. This is **destination-passing style**: `outs` still names the starting value every output element accumulates from, the same role `%out` played on memrefs, but because a tensor cannot be mutated, the operation produces a new tensor rather than changing `%init` in place.[^tensor] Whether a later pass can avoid an actual copy, reusing `%init`'s storage when nothing else needs it, is a question for bufferization, which [M7](m7-bufferization.md) covers; at this level the two forms mean different things about ownership.

The choice lines up with two of Vortex's own rules. A `tensor` operand behaves like a Vortex array passed by value: [decision 25](../decisions/references.md#d25) says initializing, assigning, passing and returning an array or struct always copies it, and a tensor's immutability is exactly that promise, carried into the type. A `memref` operand behaves like Vortex's `&mut`: `row_sum_memref`'s `%out` is the caller's storage, filled in place, the same relationship M2 found between `linalg.matmul`'s `outs` operand and a `c: &mut [f32; 8, 4]` parameter.[^m2-matmul] The gap M2 found there persists here. A `memref` type does not say that its buffer overlaps no other argument; two `memref` parameters may alias, and `linalg.generic`'s verifier checks shapes and element types, never aliasing.[^linalg] Two `tensor` operands sidestep the question rather than answering it: since neither can be written through, there is nothing for them to alias in the sense decision 25 cares about, whether or not the same buffer sits behind them once bufferization assigns one.

??? check "outs appears in both row_sum_tensor.mlir and row_sum_memref.mlir, but the operations differ: one returns a value, one returns nothing. Why does outs appear in both?"

    In both forms `outs` supplies the destination each output element starts from and accumulates into: `%init` for the tensor version, `%out` for the memref version. The difference is what happens to that destination. A memref's `outs` is mutated in place and the operation has no result. A tensor's `outs` cannot be mutated, so the operation produces a new tensor holding the result, and the function returns it instead of relying on a side effect.

## What the type system checks, and what it leaves to you

M2 closed with a table of where Vortex's promises would live in MLIR's basic dialects.[^m2-table] Structured ops sharpen two entries and leave the rest unchanged.

| Vortex fact | Where a structured op can record it | What checks it |
| --- | --- | --- |
| Fixed shapes ([decision 11](../decisions/arrays.md#d11)) | The static shapes of the `tensor` or `memref` operands, from which `linalg.generic`'s verifier derives the bounds of the iteration space | The verifier: every indexing map's domain must match that iteration space, and its range must match the operand it projects onto |
| Row-major layout ([decision 43](../decisions/arrays.md#d43)) | Nowhere in `linalg`: an indexing map is a function from loop indices to array indices, and says nothing about how those indices are laid out in memory | The `memref` type itself, unaffected by which `linalg` operation reads it |
| An `&mut` output overlaps no argument ([decision 25](../decisions/references.md#d25)) | Nowhere for a `memref` operand; sidestepped, not recorded, for a `tensor` operand, since a value cannot alias | Nothing in MLIR for memrefs; nothing to check for tensors |
| One rounding per operation, no reassociation ([decision 56](../decisions/numbers.md#d56)) | The region's own `arith` operations, exactly as in M2: `arith.mulf` then `arith.addf`, each with `fastmath = #arith.fastmath<none>` | Nothing in MLIR: your own test, reading every region the same way M2 read a function |
| The order a reduction combines its elements | Nowhere: `iterator_types` marks a dimension `reduction`, not the order its steps run in or associate | Nothing in the operation. `--convert-linalg-to-affine-loops` in MLIR 18.1.8 produced loops nested in dimension order for every example in this chapter (checked on 2026-09-24), but the documentation does not promise that order, and a different lowering pass is free to choose another one |

The last row matters together with decision 56. Fixing that each floating-point operation is exactly one rounding, with no fusion, does not by itself fix which order a chain of additions runs in, and a different order can produce a different rounded sum. `iterator_types` tells a reader and a pass which dimensions may run in any order, but it makes no promise about which order a lowering picks for the ones marked reduction. A Vortex compiler that wants a reduction's result to be reproducible has to fix that order itself, downstream of `linalg`, the way it already has to enforce `fastmath<none>` downstream of `arith`.

## For Vortex

!!! vortex "Exercise"

    **Extend** the MLIR-emitting tool from [M2's exercise](m2-reading-mlir.md#for-vortex): give it the option to emit the stage 10 kernel's matrix multiplication as one `linalg` operation instead of three nested `scf.for` loops, using only the `linalg`, `memref`, `arith` and `func` dialects.

    1. On paper, before any code: the three indexing maps and the iterator types for `c[row, column] += a[row, k] * b[k, column]`, and a one-paragraph argument for why `row` and `column` must be parallel and `k` must be reduction, referring to what a wrong choice would mean for the verifier.
    2. A decision, argued from [decision 25](../decisions/references.md#d25) and this chapter's table, between emitting `c` as a `memref` operand (mutated in place, matching `&mut`) and as a `tensor` operand (a new value returned, matching a by-value result). Write down which you chose and why, and what would have to change in the surrounding function if you chose the other one.
    3. A check, run before you emit `linalg.matmul`, that the kernel's own semantics match the named op's: M2 found that `linalg.matmul` adds into whatever `c` already holds, while the stage 10 kernel starts each `sum` at `0.0`. Decide how your tool guarantees `c` is zeroed first, or that it never uses `linalg.matmul` when it is not.
    4. **Not yet:** any of the nine transformations (tiling, fusion, vectorization, lowering to a library call), which stay with [M6](m6-affine-and-scf.md), [M8](m8-vectorization.md) and [M9](m9-transform-dialect.md); bufferization of a tensor-form kernel ([M7](m7-bufferization.md)); fixing the order a reduction runs in, beyond writing down that your tool does not yet guarantee one.

    **Proof that it works:**

    - The file your tool emits for the stage 10 kernel's shapes passes `mlir-opt` with no options, and `--convert-linalg-to-affine-loops` on it produces a loop nest whose `.expected` output your test pins down.
    - A test that changes one shape in the emitted file, for example `b`'s columns, and confirms `mlir-opt` rejects it, the same canary M2's exercise asked for.
    - A test that reads the region of your emitted operation and fails if any floating-point `arith` operation there carries a `fastmath` value other than `none`.
    - A short note, in your own words, on which choice you made in step 2 and whether it changed the shape of the function around the kernel.

## Key ideas

!!! recap "Questions you can now answer"

    - **What three things completely describe a linalg.generic operation?** Its indexing maps (one affine map per operand), its iterator types (parallel or reduction, one per dimension) and its region (the per-step computation).
    - **What does an indexing map do?** It maps a point of the operation's iteration space to the indices of one operand, so `(i, j) -> (i)` says every step sharing a row writes the same output element.
    - **Why must a dimension that several steps share at the output be marked reduction?** Because the verifier needs to know those steps combine into one value rather than write independently; marking it parallel would claim they do not conflict, which is false.
    - **What is a named op such as linalg.matmul, underneath?** A linalg.generic with its indexing maps, iterator types and region already fixed and given a name; it lowers exactly like the equivalent generic op.
    - **What changes between a tensor form and a memref form of the same structured op?** The memref form writes through its outs operand and returns nothing; the tensor form cannot mutate outs, so it returns a new value instead.
    - **Does iterator_types = "reduction" fix the order a reduction's elements combine?** No. It says those steps must combine into one value, not in what order or with what associativity; that is left to whichever pass lowers the operation.
    - **Which of Vortex's promises does a memref type capture, and which does it not?** It captures the shape and element type. It does not capture that an &mut output overlaps no other argument: nothing in MLIR checks that for memrefs, so a Vortex compiler still has to enforce it itself.

## Where this comes back

!!! next "You will use this again in"

    - [M6. Loops: affine and scf](m6-affine-and-scf.md): *iteration space*, *lowering a structured op to loops*
    - [M7. Bufferization](m7-bufferization.md): *tensor*, *memref*, *destination-passing style*
    - [M8. Vectorization in MLIR](m8-vectorization.md): *rewriting a region in vector form*, *iterator types*
    - [M9. Schedules as IR: the transform dialect](m9-transform-dialect.md): *named op*, *tiling*, *producer-consumer fusion*
    - [M10. MLIR for GPUs](m10-mlir-for-gpus.md): *mapping parallel and reduction iterator types onto GPU loops*
    - [M12. Designing Vortex's GPU path](m12-vortex-gpu-path.md): *structured ops as a lowering path*

## Sources and further reading

For depth, read the linalg dialect's own pages on payload-carrying ops and named ops with this chapter's four files open, then the tensor dialect's introduction for the destination-passing style vocabulary the rest of the MLIR book keeps using.[^linalg][^tensor] M2's citations for `memref`, `arith` and the builtin types apply unchanged here; this chapter repeats only the ones its own claims depend on.[^m2-table]

[^linalg]: MLIR Project, "'linalg' Dialect", sections "Payload-Carrying Ops", "Named Payload-Carrying Ops" and "Set of Key Transformations". <https://mlir.llvm.org/docs/Dialects/Linalg/>
[^tensor]: MLIR Project, "'tensor' Dialect", introduction. <https://mlir.llvm.org/docs/Dialects/TensorOps/>
[^stage10]: [Stage 10. Matrix multiplication](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for).
[^m2-matmul]: [M2. Reading MLIR, "A named operation hides a region"](m2-reading-mlir.md#a-named-operation-hides-a-region).
[^m2-table]: [M2. Reading MLIR, "Where Vortex's facts would live"](m2-reading-mlir.md#where-vortexs-facts-would-live), citing MLIR Project, "Builtin Dialect", entries `MemRefType` and `RankedTensorType`, <https://mlir.llvm.org/docs/Dialects/Builtin/>, and "'arith' Dialect", entries `FastMathFlagsAttr` and `FastMathFlags`, <https://mlir.llvm.org/docs/Dialects/ArithOps/>.
