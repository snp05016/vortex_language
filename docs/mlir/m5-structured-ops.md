# M5. Structured ops: linalg, tensor and memref

<p class="page-intro">A structured operation such as linalg.generic states what a computation reads, what it writes and how its iteration space maps onto both, and leaves the loop order to whoever lowers it. This chapter reads that form until you can write one by hand, shows which of its claims MLIR checks and which it takes on trust, and asks what it would capture of Vortex's fixed shapes, strict rounding and non-aliasing `&mut` output.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 35 minutes · Builds on: [M2. Reading MLIR](m2-reading-mlir.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is a named operation like linalg.matmul, and what does its generic printout reveal?"

        A common computation with a name of its own. Its generic form shows a region whose block receives one element from each operand, and three affine maps that say which element of each operand one step of the computation touches.

        Introduced in [M2. Reading MLIR](m2-reading-mlir.md#a-named-operation-hides-a-region).

    ??? question "What can a memref type record about Vortex's &mut output, and what can't it?"

        It can record the shape and element type of the buffer a function writes into. It cannot record that the buffer overlaps none of the function's other arguments: two memref parameters may alias, and nothing in their types says otherwise.

        Introduced in [M2. Reading MLIR](m2-reading-mlir.md#types-say-what-a-value-is).

    ??? question "What must be true of every f32 operation under decision 56?"

        Each one must produce the IEEE 754 result, rounded to nearest with ties to even. An implementation must not contract, reassociate or reorder them, and that includes values computed during compilation.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

    ??? question "Does the MLIR verifier reject arith.addf with fastmath<contract>?"

        No. The flag is legal MLIR, so the verifier accepts it. Whether a module keeps decision 56 is something your own test has to check.

        Introduced in [M2. Reading MLIR](m2-reading-mlir.md#attributes-and-properties-hold-the-constants).

!!! goals "In this chapter"

    - Read a `linalg.generic` operation by its three parts (indexing maps, iterator types and region) and run it by hand.
    - Predict which mistakes in a structured op the verifier rejects, and which ones, such as a false `parallel` claim or an output that overlaps an input, it accepts.
    - Explain why `linalg.matmul` is a `linalg.generic` with its maps already chosen, and use a pass to show it.
    - Distinguish a computation on `tensor` values from the same computation on `memref` buffers, and say what `outs` means in each.
    - Choose indexing maps and iterator types for a small array computation this chapter does not show.

## Why not hand the optimizer loops

Vortex's [stage 10 kernel](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for) multiplies matrices with three nested `for` loops. Suppose a later pass wants to split the work across threads, or cut it into tiles that fit in a cache. It first has to rediscover facts the programmer knew all along: that different `(row, column)` pairs never write the same element of `c`, that the `k` loop folds many products into one, and which elements of `a` and `b` each piece of work reads. From loops, those facts come out of **dependence analysis**, which asks whether two iterations touch the same memory ([P6](../optimize/p6-dependence-analysis.md) builds it). That analysis can be slow, and for code that is less regular than a matrix product it can fail.

The linalg dialect's rationale names this problem. Rebuilding high-level structure from lower-level code is called **raising**, and the rationale calls pattern detection after raising fragile, because other transformations break the patterns it looks for.[^rationale] Its answer is two principles: say what a computation means in the IR, so nothing has to be raised later, and lower in small steps, so that structure such as "these loops are parallel" survives until a pass can use it.[^rationale]

A **structured operation** is the result. It carries the facts a loop nest hides: the set of steps it performs, which element of each operand each step reads or writes, and which steps are independent. This chapter reads the linalg dialect's general structured operation, `linalg.generic`, starting from something smaller than matrix multiplication.

## A row sum, read as one operation

This function adds each row of a 3-by-4 matrix into the matching element of a 3-element vector, writing through its second argument the way a Vortex `&mut` parameter would. The example lowers it to loops with `--convert-linalg-to-affine-loops`, so the output shows what the operation means:

--8<-- "includes/examples/mlir/m5-structured-ops/row_sum_memref.mlir.md"

The source contains one operation, `linalg.generic`, and no loop. Its **iteration space** is the set of steps it performs: here one step for each pair `(i, j)` with `i` from 0 to 2 and `j` from 0 to 3, twelve steps in all. Three parts of the operation describe a single step, and the output loop nest is what you get by repeating that step over the whole space.

- `indexing_maps = [#map_in, #map_out]` gives one **affine map** per operand: a function from a point of the iteration space to indices of that operand, built from sums of indices and constant multiples of them. `#map_in`, `(i, j) -> (i, j)`, says step `(i, j)` reads element `[i, j]` of the input. `#map_out`, `(i, j) -> (i)`, says the same step works on element `[i]` of the output, and drops `j`.
- `iterator_types = ["parallel", "reduction"]` gives one entry per dimension. A **parallel** dimension is one whose steps are independent: different values of `i` touch different output elements. A **reduction** dimension is one whose steps all combine into the same output element: every `j` of one row folds into `out[i]`.
- The region, `^bb0(%elem: f32, %acc: f32)`, is the **payload**: the computation of one step. It receives one element from each operand at the current point, in operand order, and yields the new value of the output element. For the output, the block argument is that element's current value.

The documentation's fourth property of `linalg.generic` is this region: it takes one scalar element of each operand as an argument, `ins` operands first, then `outs`.[^linalg]

**Run it by hand.** Suppose row 1 of `%in` holds `1.0, 2.0, 3.0, 4.0` and `%out[1]` holds `10.0` before the call. The four steps with `i = 1` run as follows.

| Step `(i, j)` | `%elem` is `in[1, j]` | `%acc` is `out[1]` before | Yield, stored to `out[1]` |
| --- | --- | --- | --- |
| (1, 0) | 1.0 | 10.0 | 11.0 |
| (1, 1) | 2.0 | 11.0 | 13.0 |
| (1, 2) | 3.0 | 13.0 | 16.0 |
| (1, 3) | 4.0 | 16.0 | 20.0 |

The result is 20.0, not 10.0: the operation adds into whatever `out` held. That is the same behavior M2 found in `linalg.matmul`, and the lowered loops show why. The innermost body loads `%arg1[%arg2]`, adds, and stores back to the same place, once per step. Figure 1 draws the iteration space and the two maps.

<figure class="vx-figure">
<svg viewBox="0 0 760 400" role="img" aria-label="The row sum's iteration space, projected by two indexing maps onto its input and output" aria-describedby="m5-f1-desc">
<title id="m5-f1-title">Row sum's iteration space, projected onto its input and output</title>
<desc id="m5-f1-desc">A three by four grid of cells, one per step of the iteration space, labelled with the input element each step reads, in[i, j], for i from 0 to 2 and j from 0 to 3. Columns are marked j, reduction, along the top; rows are marked i, parallel, down the left side. To the right, a column of three cells labelled out[0], out[1] and out[2]. The four cells of row 1 are highlighted one after another from j equals 0 to j equals 3, and an arrow runs from each of them into out[1], showing four steps folding into one output element. Rows 0 and 2 work the same way, independently. Below the grid, the two indexing maps and the iterator types are written out with one line of explanation each.</desc>
<text class="vx-text" x="70" y="24">j (reduction) →</text>
<text class="vx-text" x="30" y="122" transform="rotate(90 30 122)" text-anchor="middle">i (parallel) →</text>
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
<rect class="vx-box-accent" x="70" y="100" width="80" height="44" rx="4"/>
<text class="vx-mono" x="110" y="126" text-anchor="middle">in[1,0]</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4">
<rect class="vx-box-accent" x="158" y="100" width="80" height="44" rx="4"/>
<text class="vx-mono" x="198" y="126" text-anchor="middle">in[1,1]</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4">
<rect class="vx-box-accent" x="246" y="100" width="80" height="44" rx="4"/>
<text class="vx-mono" x="286" y="126" text-anchor="middle">in[1,2]</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 4">
<rect class="vx-box-accent" x="334" y="100" width="80" height="44" rx="4"/>
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
<rect class="vx-box" x="560" y="40" width="100" height="44" rx="4"/>
<text class="vx-mono" x="610" y="66" text-anchor="middle">out[0]</text>
<rect class="vx-box-strong" x="560" y="100" width="100" height="44" rx="4"/>
<text class="vx-mono" x="610" y="126" text-anchor="middle">out[1]</text>
<rect class="vx-box" x="560" y="160" width="100" height="44" rx="4"/>
<text class="vx-mono" x="610" y="186" text-anchor="middle">out[2]</text>
<path class="vx-flow" style="--vx-i: 0" d="M110,100 C 110,86 440,86 552,114"/>
<path class="vx-flow" style="--vx-i: 1" d="M198,100 C 198,90 440,92 552,117"/>
<path class="vx-flow" style="--vx-i: 2" d="M286,100 C 286,94 440,98 552,120"/>
<path class="vx-flow" style="--vx-i: 3" d="M414,122 L 552,122"/>
<polygon class="vx-arrowhead" points="552,116 560,122 552,128"/>
<text class="vx-mono" x="70" y="246">#map_in = (i, j) -&gt; (i, j)</text>
<text class="vx-text-muted" x="70" y="266">each step reads its own input element</text>
<text class="vx-mono" x="70" y="300">#map_out = (i, j) -&gt; (i)</text>
<text class="vx-text-muted" x="70" y="320">j drops out: the four steps of a row share one output element</text>
<text class="vx-mono" x="70" y="354">iterator_types = ["parallel", "reduction"]</text>
<text class="vx-text-muted" x="70" y="374">rows are independent; each row's steps combine into one value</text>
</svg>
<figcaption>Figure 1. The row sum's iteration space, one cell per step <code>(i, j)</code>, labelled with the input element it reads. Row 1 is highlighted: its four steps each read one input element and all update <code>out[1]</code>. Rows 0 and 2 do the same, independently. The operation says which steps combine, not in what order.</figcaption>
</figure>

One thing is missing from the source: the loop bounds. Nothing in the operation says 3 or 4, yet the lowered loops run `0 to 3` and `0 to 4`. The documentation lists this as the first property of `linalg.generic`: the operands define the iteration space.[^linalg] Each map ties dimensions to operand shapes. `#map_in` sends `i` to the first index of a `3x4` memref and `j` to the second, so `i` ranges over 3 values and `j` over 4. The lowering reads the bounds off the types, and so does the verifier, which the next section tests.

Several operations of the linalg dialect work this way. The documentation calls `linalg.generic` a **payload-carrying operation** that implements the **structured op** abstraction on tensors and buffers, and it lists six properties that together define its meaning.[^linalg] This chapter meets five of them: operands define the iteration space (property 1), the maps from iteration space to data are explicit (2), iterator types are declared (3), the payload is a region (4), and the loop nest is perfectly nested and writes the whole output (6). Property 5, mapping an operation to an external library call, appears in the list of transformations near the end of the chapter.

??? check "Row sum's lowered loops run `i` from 0 to 3 and `j` from 0 to 4. If `%out` were `memref<5xf32>` instead, what would the extent of `i` be, and what should happen?"

    There would be two answers. `#map_in` ties `i` to the input's first dimension, 3, and `#map_out` ties it to the output's only dimension, 5. An operation whose operands disagree about a dimension's extent has no single iteration space, so it should be rejected, and it is: the next section's first broken function is this mistake with a 2 in place of the 5.

## What the verifier checks

M2 used `--verify-diagnostics` to turn broken files into tests: each `expected-error` comment asserts that exactly that error appears on the next line.[^testing] This file holds three structured ops the verifier rejects and two it accepts:

--8<-- "includes/examples/mlir/m5-structured-ops/verifier.mlir.md"

Only the two valid chunks print. Read the rejected ones first.

1. `@short_output` writes row sums into a 2-element vector. From the input, the verifier infers that `i` ranges over 3 values, so the output, which `#row` indexes by `i`, must have 3 elements. The message names the operand (`#1`, the output), the dimension and both numbers.
2. `@one_iterator` lists one iterator type for maps with two dimensions. Every map's domain must have exactly as many dimensions as there are loops, and there is one loop per iterator type.
3. `@window_without_taps` tries a sliding window: `y[i]` should sum `x[i]`, `x[i + 1]` and `x[i + 2]`. The map `(i, k) -> (i + k)` is legal, but no operand has a dimension indexed by `k` alone, so nothing says how many values `k` takes. The verifier's **shape-to-loops map**, the function it builds to compute each loop's extent from the operand shapes, does not exist, and it says so.

The first valid chunk repairs the window. A third operand, `%w`, indexed by `(i, k) -> (k)`, supplies three weights, which gives `k` its extent, 3, and makes the operation a 3-tap filter: `y[i]` is the sum of `w[k] * x[i + k]`. The output extent, 4, gives `i`. The input's map is not a plain selection of dimensions, so the verifier only checks that `x` is long enough: with `x` shortened to 5 elements, MLIR 18.1.8 reports that it expected a dimension "greater than or equal to 6" (checked on 2026-09-24). This is the shape of a convolution, and linalg has named operations for convolutions of several ranks.[^linalg]

The second valid chunk is the warning. `@output_overlaps_input` uses `memref.subview`, an operation that makes a **view**: a new memref that describes part of an existing buffer, with its own sizes and strides, without copying.[^memref] Here the view is column 0 of the input, three elements four apart (`strided<[4]>`, since a row-major `3x4` buffer has strides `[4, 1]`[^builtin]). The row sum then writes each row's total into that row's first element while still reading the row. The verifier accepts it, because nothing in its rules concerns overlap. The shape rules checked here are real, but they are all it checks about memory.

??? check "Here is a fourth broken operation, not in the file. Which error do you expect, and why is it a mistake even though the arithmetic is sensible?"

    ```mlir
    linalg.generic {indexing_maps = [#id, #row], iterator_types = ["parallel", "reduction"]}
        ins(%in : memref<3x4xf32>) outs(%out : memref<3xf32>) {
    ^bb0(%e: f64, %acc: f32):
      linalg.yield %acc : f32
    }
    ```

    The payload's first block argument receives an element of `%in`, which is `f32`, but it is declared `f64`. MLIR 18.1.8 reports that the type of block argument #0 must match the element type of the corresponding operand (checked on 2026-09-24). The payload receives elements, not converted values; a conversion would have to be an operation in the region.

## Iterator types are a promise

The verifier checked every shape. It did not check the other claim a structured op makes: which dimensions are parallel. This file holds two row sums that differ only in `iterator_types`. The second marks `j` parallel, which is false, since four steps of a row update the same `out[i]`. The example lowers both with `--convert-linalg-to-parallel-loops`, which turns parallel dimensions into a parallel loop:

--8<-- "includes/examples/mlir/m5-structured-ops/parallel_claim.mlir.md"

Both functions pass the verifier. The lowerings differ. `scf.parallel` is a loop whose iterations may run in any order, or at the same time; the scf documentation says that if its iterations race, "the behavior is undefined".[^scf]

For `@honest`, only `i` becomes a parallel dimension, and each row's four steps stay in an ordinary `scf.for`, one after another. For `@false_claim`, all twelve steps become iterations of one `scf.parallel`. Four of them load `out[1]`, add, and store it back. Run at the same time, they can all load `10.0`, and the last store wins: a **lost update**, where one write replaces another that it should have included. The final value depends on timing, and 20 is only one of the possible outcomes. Figure 2 draws both schedules.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="The honest and the false iterator claim, lowered: one row's steps in sequence, or all twelve steps at once" aria-describedby="m5-f2-desc">
<title id="m5-f2-title">What the lowering does with each iterator claim</title>
<desc id="m5-f2-desc">Two panels. Left, honest, parallel and reduction: three rows of four steps. Each row is one parallel lane, and within a row arrows chain the steps j equals 0 to 3 one after another, so out[1] receives 11, then 13, then 16, then 20. Right, false claim, parallel and parallel: the same twelve steps with no arrows between them, all marked as independent. The four steps of row 1 all read out[1] as 10 at the same time and each write back their own sum, 11, 12, 13 or 14, into one cell marked as a conflict; whichever store lands last wins.</desc>
<text class="vx-text" x="20" y="24">honest: ["parallel", "reduction"]</text>
<text class="vx-text" x="400" y="24">false claim: ["parallel", "parallel"]</text>
<g>
<rect class="vx-box" x="20" y="44" width="60" height="36" rx="4"/><rect class="vx-box" x="100" y="44" width="60" height="36" rx="4"/><rect class="vx-box" x="180" y="44" width="60" height="36" rx="4"/><rect class="vx-box" x="260" y="44" width="60" height="36" rx="4"/>
<rect class="vx-box-accent" x="20" y="100" width="60" height="36" rx="4"/><rect class="vx-box-accent" x="100" y="100" width="60" height="36" rx="4"/><rect class="vx-box-accent" x="180" y="100" width="60" height="36" rx="4"/><rect class="vx-box-accent" x="260" y="100" width="60" height="36" rx="4"/>
<rect class="vx-box" x="20" y="156" width="60" height="36" rx="4"/><rect class="vx-box" x="100" y="156" width="60" height="36" rx="4"/><rect class="vx-box" x="180" y="156" width="60" height="36" rx="4"/><rect class="vx-box" x="260" y="156" width="60" height="36" rx="4"/>
<text class="vx-mono" x="50" y="123" text-anchor="middle">11</text><text class="vx-mono" x="130" y="123" text-anchor="middle">13</text><text class="vx-mono" x="210" y="123" text-anchor="middle">16</text><text class="vx-mono" x="290" y="123" text-anchor="middle">20</text>
</g>
<line class="vx-line" x1="80" y1="62" x2="96" y2="62"/><line class="vx-line" x1="160" y1="62" x2="176" y2="62"/><line class="vx-line" x1="240" y1="62" x2="256" y2="62"/>
<path class="vx-flow" d="M80,118 L96,118"/><path class="vx-flow" d="M160,118 L176,118"/><path class="vx-flow" d="M240,118 L256,118"/>
<line class="vx-line" x1="80" y1="174" x2="96" y2="174"/><line class="vx-line" x1="160" y1="174" x2="176" y2="174"/><line class="vx-line" x1="240" y1="174" x2="256" y2="174"/>
<text class="vx-text-muted" x="20" y="220">one lane per row; a row's steps run in order</text>
<text class="vx-text-muted" x="20" y="240">out[1]: 10 → 11 → 13 → 16 → 20</text>
<line class="vx-line" x1="370" y1="36" x2="370" y2="310"/>
<g>
<rect class="vx-box" x="400" y="44" width="60" height="36" rx="4"/><rect class="vx-box" x="480" y="44" width="60" height="36" rx="4"/><rect class="vx-box" x="560" y="44" width="60" height="36" rx="4"/><rect class="vx-box" x="640" y="44" width="60" height="36" rx="4"/>
<rect class="vx-box-accent vx-pulse" x="400" y="100" width="60" height="36" rx="4"/><rect class="vx-box-accent vx-pulse" x="480" y="100" width="60" height="36" rx="4"/><rect class="vx-box-accent vx-pulse" x="560" y="100" width="60" height="36" rx="4"/><rect class="vx-box-accent vx-pulse" x="640" y="100" width="60" height="36" rx="4"/>
<rect class="vx-box" x="400" y="156" width="60" height="36" rx="4"/><rect class="vx-box" x="480" y="156" width="60" height="36" rx="4"/><rect class="vx-box" x="560" y="156" width="60" height="36" rx="4"/><rect class="vx-box" x="640" y="156" width="60" height="36" rx="4"/>
<text class="vx-mono" x="430" y="123" text-anchor="middle">11</text><text class="vx-mono" x="510" y="123" text-anchor="middle">12</text><text class="vx-mono" x="590" y="123" text-anchor="middle">13</text><text class="vx-mono" x="670" y="123" text-anchor="middle">14</text>
</g>
<rect class="vx-box-bad" x="505" y="244" width="90" height="36" rx="4"/>
<text class="vx-mono" x="550" y="267" text-anchor="middle">out[1]</text>
<path class="vx-flow" d="M430,136 L535,240"/><path class="vx-flow" d="M510,136 L545,240"/><path class="vx-flow" d="M590,136 L555,240"/><path class="vx-flow" d="M670,136 L565,240"/>
<text class="vx-text-muted" x="400" y="216">all twelve steps may run at once;</text>
<text class="vx-text-muted" x="620" y="266">each read 10;</text>
<text class="vx-text-muted" x="620" y="284">the last store wins</text>
</svg>
<figcaption>Figure 2. The same payload under two iterator claims, lowered by <code>--convert-linalg-to-parallel-loops</code>. Numbers are the value each step of row 1 stores into <code>out[1]</code>, starting from 10 with row 1 holding 1, 2, 3 and 4. With the honest claim the steps of a row chain. With the false claim they are independent iterations of one <code>scf.parallel</code>, so all four may read 10, and then the final value is 11, 12, 13 or 14 instead of 20. Other timings give other results.</figcaption>
</figure>

The linalg documentation says this is by design. Iterator types record facts that are "traditionally the result of complex dependence analyses", and the documentation describes them as a contract that the front end or user guarantees and the compiler may take advantage of.[^linalg] It also says the front end is responsible for making the region's behavior agree with the iterator types, and that a conflict with a parallel iterator is undefined behavior.[^linalg] The same passage gives a case where the "wrong" claim is deliberate: a histogram, where several steps add into the same bin, may be marked parallel if the region uses atomic operations to do the adding.[^linalg]

So iterator types are information that flows in one direction, from whoever builds the operation to every pass that reads it. The verifier checks that there is one per dimension; it does not check that they are true. [O11](../optimize/o11-undefined-behavior.md) shows how LLVM's optimizer uses such promises, and why breaking one can produce any result. For a compiler that emits linalg, the rule is short. Mark a dimension parallel only when you know that no two of its steps write the same output element, and let the indexing maps tell you: a dimension missing from an output's map is shared by several steps that write that output element, and must be a reduction unless you have made the combining safe yourself.

??? check "A matrix-vector product `y[i] += m[i, j] * x[j]` has two dimensions. Which output map, and so which iterator types, follow from the rule above?"

    The output map is `(i, j) -> (i)`: `j` is missing from it, so all the steps with the same `i` and different `j` update one `y[i]`, and `j` must be a reduction. `i` appears in the output map, and different `i` write different elements, so `i` may be parallel: `["parallel", "reduction"]`. The next section asks for the other two maps.

## Named ops: the same interface, with the maps already chosen

Matrix multiplication is one more dimension. The stage 10 kernel computes, for every `row` and `column`, the sum over `k` of `a[row, k] * b[k, column]`, so its iteration space is the set of triples `(row, column, k)`. M2 read the same computation as `linalg.matmul`. Written as `linalg.generic` and as the named op in one file, with the example running `--linalg-generalize-named-ops`, a pass that replaces each named operation with the equivalent `linalg.generic`:[^passes]

--8<-- "includes/examples/mlir/m5-structured-ops/matmul_generalize.mlir.md"

The two functions print identically apart from their names. They even share the three map aliases at the top, because the printer defines one alias per distinct map and both functions use the same three. That is what the documentation means when it says the named operations "adhere to the linalg.generic op interface": a named op is a `linalg.generic` whose maps, iterator types and payload are fixed by its name.[^linalg] The dialect generates these operations from a declarative description rather than writing each one by hand.[^linalg] MLIR 18.1.8 has only this direction as a pass; newer MLIR also lists `-linalg-specialize-generic-ops`, which turns a matching `linalg.generic` back into a named op.[^passes]

The maps for matrix multiplication are worth reading as geometry. The iteration space is a box of `4 × 5 × 3` points. Each map projects the box onto one operand by dropping one dimension, as Figure 3 shows.

<figure class="vx-figure">
<svg viewBox="0 0 760 350" role="img" aria-label="The matrix multiplication iteration space as a box of points, projected onto a, b and c by dropping one dimension each" aria-describedby="m5-f3-desc">
<title id="m5-f3-title">Matrix multiplication's iteration space and its three projections</title>
<desc id="m5-f3-desc">On the left, a box drawn in perspective with edges labelled i, 4 values, parallel; j, 5 values, parallel; and k, 3 values, reduction. A line of three points along k, at i equals 1 and j equals 2, is highlighted. On the right, three grids. The a grid, 4 rows by 3 columns, labelled map (i, j, k) to (i, k), drops j: the highlighted line covers row 1, all three columns. The b grid, 3 rows by 5 columns, labelled map (i, j, k) to (k, j), drops i: the line covers column 2, all three rows. The c grid, 4 rows by 5 columns, labelled map (i, j, k) to (i, j), drops k: the whole line lands on the single cell c[1, 2], because k is the reduction dimension.</desc>
<polygon class="vx-box" points="40,120 200,120 200,280 40,280"/>
<polygon class="vx-box" points="40,120 110,60 270,60 200,120"/>
<polygon class="vx-box" points="200,120 270,60 270,220 200,280"/>
<text class="vx-text" x="80" y="302">j: 5, parallel</text>
<text class="vx-text" x="20" y="200" transform="rotate(-90 20 200)" text-anchor="middle">i: 4, parallel</text>
<text class="vx-text" x="244" y="82" transform="rotate(-40 244 82)">k: 3, reduction</text>
<line class="vx-line" x1="136" y1="156" x2="172" y2="126"/>
<circle class="vx-dot vx-pulse" cx="136" cy="156" r="5"/>
<circle class="vx-dot vx-pulse" cx="154" cy="141" r="5"/>
<circle class="vx-dot vx-pulse" cx="172" cy="126" r="5"/>
<text class="vx-text-muted" x="40" y="330">highlighted: i = 1, j = 2, every k</text>
<text class="vx-mono" x="330" y="30">a  (i, j, k) -&gt; (i, k)</text>
<g>
<rect class="vx-box" x="330" y="42" width="30" height="20"/><rect class="vx-box" x="360" y="42" width="30" height="20"/><rect class="vx-box" x="390" y="42" width="30" height="20"/>
<rect class="vx-box-accent" x="330" y="62" width="30" height="20"/><rect class="vx-box-accent" x="360" y="62" width="30" height="20"/><rect class="vx-box-accent" x="390" y="62" width="30" height="20"/>
<rect class="vx-box" x="330" y="82" width="30" height="20"/><rect class="vx-box" x="360" y="82" width="30" height="20"/><rect class="vx-box" x="390" y="82" width="30" height="20"/>
<rect class="vx-box" x="330" y="102" width="30" height="20"/><rect class="vx-box" x="360" y="102" width="30" height="20"/><rect class="vx-box" x="390" y="102" width="30" height="20"/>
</g>
<text class="vx-text-muted" x="440" y="76">drops j: the line reads row 1</text>
<text class="vx-mono" x="330" y="152">b  (i, j, k) -&gt; (k, j)</text>
<g>
<rect class="vx-box" x="330" y="164" width="30" height="20"/><rect class="vx-box" x="360" y="164" width="30" height="20"/><rect class="vx-box-accent" x="390" y="164" width="30" height="20"/><rect class="vx-box" x="420" y="164" width="30" height="20"/><rect class="vx-box" x="450" y="164" width="30" height="20"/>
<rect class="vx-box" x="330" y="184" width="30" height="20"/><rect class="vx-box" x="360" y="184" width="30" height="20"/><rect class="vx-box-accent" x="390" y="184" width="30" height="20"/><rect class="vx-box" x="420" y="184" width="30" height="20"/><rect class="vx-box" x="450" y="184" width="30" height="20"/>
<rect class="vx-box" x="330" y="204" width="30" height="20"/><rect class="vx-box" x="360" y="204" width="30" height="20"/><rect class="vx-box-accent" x="390" y="204" width="30" height="20"/><rect class="vx-box" x="420" y="204" width="30" height="20"/><rect class="vx-box" x="450" y="204" width="30" height="20"/>
</g>
<text class="vx-text-muted" x="500" y="198">drops i: the line reads column 2</text>
<text class="vx-mono" x="330" y="252">c  (i, j, k) -&gt; (i, j)</text>
<g>
<rect class="vx-box" x="330" y="262" width="24" height="16"/><rect class="vx-box" x="354" y="262" width="24" height="16"/><rect class="vx-box" x="378" y="262" width="24" height="16"/><rect class="vx-box" x="402" y="262" width="24" height="16"/><rect class="vx-box" x="426" y="262" width="24" height="16"/>
<rect class="vx-box" x="330" y="278" width="24" height="16"/><rect class="vx-box" x="354" y="278" width="24" height="16"/><rect class="vx-box-strong vx-pulse" x="378" y="278" width="24" height="16"/><rect class="vx-box" x="402" y="278" width="24" height="16"/><rect class="vx-box" x="426" y="278" width="24" height="16"/>
<rect class="vx-box" x="330" y="294" width="24" height="16"/><rect class="vx-box" x="354" y="294" width="24" height="16"/><rect class="vx-box" x="378" y="294" width="24" height="16"/><rect class="vx-box" x="402" y="294" width="24" height="16"/><rect class="vx-box" x="426" y="294" width="24" height="16"/>
<rect class="vx-box" x="330" y="310" width="24" height="16"/><rect class="vx-box" x="354" y="310" width="24" height="16"/><rect class="vx-box" x="378" y="310" width="24" height="16"/><rect class="vx-box" x="402" y="310" width="24" height="16"/><rect class="vx-box" x="426" y="310" width="24" height="16"/>
</g>
<text class="vx-text-muted" x="470" y="292">drops k: the whole line lands on c[1, 2]</text>
</svg>
<figcaption>Figure 3. Each indexing map of matrix multiplication projects the <code>4 × 5 × 3</code> box of steps onto one operand by dropping a dimension. The highlighted line of three steps reads a row of <code>a</code> and a column of <code>b</code>, and all three land on one element of <code>c</code>. The dimension that <code>c</code>'s map drops, <code>k</code>, is the reduction.</figcaption>
</figure>

The picture explains the iterator types without any loop. `row` and `column` survive in `c`'s map, so steps that differ in either write different elements: parallel. `k` is dropped from `c`'s map, so a whole line of steps writes one element: reduction. The same reading gives the bounds: `a` fixes `row` at 4 and `k` at 3, `b` fixes `column` at 5.

The payload is the part M2 already read: `arith.mulf`, then `arith.addf`, each with `fastmath<none>`. Two roundings per step, as [decision 56](../decisions/numbers.md#d56) requires, provided nothing downstream fuses them. And like the row sum, `linalg.matmul` adds into what `c` already holds, which M2 found does not match the stage 10 kernel's `let mut sum: f32 = 0.0`.[^m2-matmul]

Now finish a structured op yourself. This is the matrix-vector product from the last check question, with three blanks:

```mlir
func.func @matvec(%m: memref<3x4xf32>, %x: memref<4xf32>, %y: memref<3xf32>) {
  linalg.generic {
    indexing_maps = [affine_map<(i, j) -> ____>,
                     affine_map<(i, j) -> ____>,
                     affine_map<(i, j) -> (i)>],
    iterator_types = ____
  } ins(%m, %x : memref<3x4xf32>, memref<4xf32>) outs(%y : memref<3xf32>) {
  ^bb0(%m_elem: f32, %x_elem: f32, %acc: f32):
    %p = arith.mulf %m_elem, %x_elem : f32
    %s = arith.addf %acc, %p : f32
    linalg.yield %s : f32
  }
  return
}
```

??? check "What fills the three blanks, and how could you confirm your answer with a pass instead of by eye?"

    `(i, j)` for `%m`, `(j)` for `%x`, and `["parallel", "reduction"]`. The completed function passes `mlir-opt` 18.1.8. To confirm it, write `linalg.matvec ins(%m, %x : ...) outs(%y : ...)` in the same file and run `--linalg-generalize-named-ops`: the named op becomes a `linalg.generic` with the same three maps and the same iterator types as yours (both checked on 2026-09-24).

## Values or buffers: tensor against memref

Every example so far has worked on memrefs, which name memory. Structured ops also work on the builtin **tensor** type: a value with a shape and an element type, and no address. The tensor dialect's documentation states the key fact: "a tensor is an immutable object."[^tensor] Nothing can write into a tensor. An operation that "changes" one produces a new tensor instead.

Here is the row sum again as a function of values. It also fixes the adds-into-the-output behavior, by starting from zeros:

--8<-- "includes/examples/mlir/m5-structured-ops/row_sum_tensor.mlir.md"

Three things are new.

- `tensor.empty() : tensor<3xf32>` makes a tensor whose contents are unspecified. Its only purpose, in the documentation's words, is to make a shape available to other operations.[^tensor]
- `linalg.fill` is a named structured op that sets every element of its output to one value, here `0.0`.[^linalg] On tensors it returns the filled tensor as `%init`.
- The `linalg.generic` now has a result, `-> tensor<3xf32>`, and the function returns it.

The generic op still has an `outs` operand. On tensors, `outs` supplies the starting value of each output element, the value `%acc` receives on the first step of each row, and the operation returns the final values as a new tensor. The Bufferization documentation calls this **destination-passing style**: an operation that produces a tensor takes an extra operand, its "destination", that gives the result's shape and initial contents, and `linalg.generic` on tensors has one `outs` operand for each result.[^bufferization] The documentation puts "destination" in quotes for a reason: the operand is not modified in place.[^bufferization] `%init` still holds zeros after the operation, and `%sums` is a different value.

The linalg documentation distinguishes two uses of an output tensor.[^linalg] An **init tensor** provides values the computation starts from and updates, as `%init` does here. A **shape-only** output provides a shape and nothing else, because the payload never reads its elements: a structured op that writes every element without reading it, such as `linalg.fill`, can take `tensor.empty` directly.

<figure class="vx-figure">
<svg viewBox="0 0 760 270" role="img" aria-label="The row sum on tensors produces a new value from its destination; on memrefs it writes into the destination's memory" aria-describedby="m5-f4-desc">
<title id="m5-f4-title">Destination as a value, or as memory</title>
<desc id="m5-f4-desc">Two panels. Left, tensor form: a box %in of type tensor 3 by 4 and a box %init holding zeros both feed into a box labelled linalg.generic, which produces a new box %sums. An arrow from %init continues past the operation to a note saying %init still holds zeros and may be used again. Right, memref form: a box %in of type memref 3 by 4 feeds into linalg.generic, and a box labelled %out, the caller's memory, has arrows both into and out of the operation, marked read and write in place. The operation has no result.</desc>
<text class="vx-text" x="20" y="24">tensor: a new value</text>
<text class="vx-text" x="400" y="24">memref: the same memory, updated</text>
<rect class="vx-box" x="20" y="44" width="150" height="40" rx="4"/>
<text class="vx-mono" x="95" y="69" text-anchor="middle">%in : 3x4</text>
<rect class="vx-box" x="20" y="120" width="150" height="40" rx="4"/>
<text class="vx-mono" x="95" y="145" text-anchor="middle">%init = 0, 0, 0</text>
<rect class="vx-box-strong" x="210" y="82" width="140" height="40" rx="4"/>
<text class="vx-mono" x="280" y="107" text-anchor="middle">linalg.generic</text>
<line class="vx-line" x1="170" y1="64" x2="206" y2="94"/>
<line class="vx-line" x1="170" y1="140" x2="206" y2="112"/>
<rect class="vx-box-accent" x="210" y="170" width="140" height="40" rx="4"/>
<text class="vx-mono" x="280" y="195" text-anchor="middle">%sums (new)</text>
<path class="vx-flow" d="M280,122 L280,164"/>
<polygon class="vx-arrowhead" points="275,164 280,170 285,164"/>
<text class="vx-text-muted" x="20" y="238">%init is unchanged and may be used again;</text>
<text class="vx-text-muted" x="20" y="256">bufferization decides later whether they share memory</text>
<line class="vx-line" x1="375" y1="36" x2="375" y2="260"/>
<rect class="vx-box" x="400" y="44" width="150" height="40" rx="4"/>
<text class="vx-mono" x="475" y="69" text-anchor="middle">%in : 3x4</text>
<rect class="vx-box-strong" x="590" y="82" width="140" height="40" rx="4"/>
<text class="vx-mono" x="660" y="107" text-anchor="middle">linalg.generic</text>
<line class="vx-line" x1="550" y1="64" x2="586" y2="94"/>
<rect class="vx-box-accent" x="590" y="170" width="140" height="40" rx="4"/>
<text class="vx-mono" x="660" y="195" text-anchor="middle">%out (caller's)</text>
<path class="vx-flow" d="M640,170 L640,128"/>
<polygon class="vx-arrowhead" points="635,128 640,122 645,128"/>
<path class="vx-flow" d="M680,122 L680,164"/>
<polygon class="vx-arrowhead" points="675,164 680,170 685,164"/>
<text class="vx-text-muted" x="560" y="150">read</text>
<text class="vx-text-muted" x="690" y="150">write</text>
<text class="vx-text-muted" x="400" y="238">no result: the effect is the store;</text>
<text class="vx-text-muted" x="400" y="256">the caller sees the new contents of %out</text>
</svg>
<figcaption>Figure 4. The same operation in destination-passing style on values (left) and on buffers (right). On tensors, <code>outs</code> is an ordinary input that gives the starting values, and the result is a new tensor. On memrefs, <code>outs</code> names memory the operation reads and overwrites, and there is no result.</figcaption>
</figure>

**Tensors cannot become loops yet.** Running `--convert-linalg-to-affine-loops` on the tensor example leaves it unchanged (checked on 2026-09-24), and the Passes documentation states why: the loop lowerings require operands with buffer semantics.[^passes] A loop stores to memory, and a tensor has no memory. Turning tensors into memrefs, and deciding when `%sums` may reuse `%init`'s storage instead of copying it, is **bufferization**, the subject of [M7](m7-bufferization.md).

**Memrefs are memory.** A memref's buffer "can be allocated, aliased and deallocated", in the words of its type's documentation.[^builtin] The memref dialect provides the operations: `memref.alloc` allocates a buffer of a memref type, and `memref.subview` makes the kind of view the verifier example used.[^memref] The linalg documentation names the strided memref that views produce as its data representation, and calls it a **View**.[^linalg]

The two types line up with two of Vortex's own rules. A tensor behaves like a Vortex array passed by value: [decision 25](../decisions/references.md#d25) says that initializing, assigning, passing to a parameter that is not a reference, and returning each copy the whole value, and that the source stays unchanged. That is what immutability gives a tensor. A memref behaves like Vortex's `&mut` parameter: `row_sum_into`'s `%out` is the caller's storage, filled in place, the relationship M2 found between `linalg.matmul`'s `outs` and `c: &mut [f32; 8, 4]`.[^m2-matmul]

The gap M2 found stays open. Decision 25 also forbids passing a variable both as `&mut` and as another argument of the same call, so a Vortex callee knows its output overlaps no input. The verifier example showed that a memref-form `linalg.generic` whose output overlaps its input passes the verifier. Tensor operands avoid the question rather than answer it: nothing can write through a tensor, so there is nothing to overlap until bufferization gives each tensor memory, and at that point avoiding harmful overlap becomes the bufferization pass's job.

??? check "`outs` appears in both row sums, but one operation returns a value and the other returns nothing. What does `outs` mean in each, and why does the tensor form need a result?"

    In both, `outs` supplies the value each output element starts from, the first `%acc`. On memrefs it also names the memory the results are stored to, so the operation needs no result: the stores are its effect. A tensor cannot be written to, so the only way for the operation to deliver the sums is to return a new tensor, and the function returns that.

## The transformations this shape is built for

The linalg documentation lists nine transformations that shaped the dialect's design, all implemented through the properties of the `linalg.generic` interface rather than knowledge of particular operations:[^linalg] progressive buffer allocation; tiling with sizes chosen as parameters; promotion of an operand to a temporary buffer in fast memory; tiled producer-consumer fusion; mapping iterators to parallel and reduction loops and to hardware; rewriting in vector form; lowering to loops; lowering to library calls, special instructions or intrinsics; and partial lowering to a finer-grained linalg operation. This chapter used two members of the loop-lowering item, `--convert-linalg-to-affine-loops` and `--convert-linalg-to-parallel-loops`, and a relative of the last one, generalization.

What makes tiling and fusion cheap here is the second property on the documentation's list: the mapping between iteration space and data is explicit, so a pass can answer two questions without analysis.[^linalg] Given a set of steps, which data do they read and write? And given some data, which steps touch it?

Take the `4 × 5 × 3` matrix product and cut the iteration space into two tiles along `row`: rows 0 and 1, and rows 2 and 3, each with all `column` and `k` values. Push the first tile through the maps. `(i, k)` gives rows 0 to 1 of `a`, all columns. `(k, j)` gives all of `b`. `(i, j)` gives rows 0 to 1 of `c`.

The second tile writes rows 2 to 3 of `c`. The tiles write disjoint parts of `c`, which is what `row` being parallel promised, so they may run in either order or at once. Cut along `k` instead and both tiles write all of `c`: a reduction split in two, whose partial sums must be combined.

The sixth property keeps these answers simple. A `linalg.generic` stands for a **perfectly nested** loop nest, one where all the work happens in the innermost loop, that writes the whole of each output.[^linalg] The price, the documentation says, is that code which is not naturally in this form must be rearranged into it, and that `linalg.generic` cannot model arbitrary code with side effects; such code stays in lower-level dialects.[^linalg]

Splitting a reduction is a transformation linalg supports on purpose. The transform dialect's `split_reduction` operation rewrites one reduction dimension into a parallel dimension and a smaller reduction, with a second `linalg.generic` to finish the sum.[^transform] For integers that is only a change of schedule. For `f32` it changes which partial sums are added together, and so, in general, the rounded result: the reassociation that [decision 56](../decisions/numbers.md#d56) forbids. [M6](m6-affine-and-scf.md) tiles loops, [M8](m8-vectorization.md) rewrites payloads in vector form, and [M9](m9-transform-dialect.md) writes these transformations as IR; each has to be checked against that rule.

??? check "A pass tiles the row sum into two tiles along `j`: columns 0 to 1 and columns 2 to 3. Do the tiles write disjoint data? What must be true of how their results combine for the answer to match the untiled operation bit for bit?"

    No. `#map_out` drops `j`, so both tiles write all three elements of `out`. For the result to match exactly, each row's four additions must still happen in the order `j = 0, 1, 2, 3`, each adding into the running value: the second tile must run after the first and start from the first tile's result. If each tile instead summed its two columns separately and the two partial sums were added at the end, `f32` rounding could give a different answer.

## What the type system checks, and what it leaves to you

M2 ended with a table of where Vortex's promises would live in MLIR's basic dialects.[^m2-table] Structured ops sharpen several rows and add two.

| Vortex fact | Where a structured op records it | What checks it |
| --- | --- | --- |
| Fixed shapes ([decision 11](../decisions/arrays.md#d11)) | The static shapes of the operands, from which the iteration space is inferred | The verifier: every operand's shape must agree with the inferred extents |
| Which steps are independent | `iterator_types` | Nothing: the verifier checks the count, not the truth; a false `parallel` is undefined behavior |
| Row-major layout ([decision 43](../decisions/arrays.md#d43)) | A memref's default layout; a tensor has none | The memref type, as in M2 |
| An `&mut` output overlaps no argument ([decision 25](../decisions/references.md#d25)) | Nowhere for memrefs; not applicable to tensors until bufferization | Nothing in MLIR for memrefs |
| One rounding per operation ([decision 56](../decisions/numbers.md#d56)) | The payload's `arith` operations, each with `fastmath<none>` | Nothing in MLIR: your own test |
| The order a reduction adds its terms ([decision 56](../decisions/numbers.md#d56)) | Nowhere: `reduction` says steps combine, not in what order | Nothing in the operation; each lowering or transformation decides |

The last row needs a precise statement of what was observed. In MLIR 18.1.8, `--convert-linalg-to-affine-loops` produced loops nested in dimension order, with each reduction running from its lower bound upward, for every example in this chapter (checked on 2026-09-24). That is an observation about one pass on these inputs. The documentation gives no such promise, and `split_reduction` exists precisely to change the order. A Vortex compiler that lowers through linalg and wants reproducible `f32` sums has to fix the order itself, by choosing its passes and testing their output, as it already has to enforce `fastmath<none>`.

## For Vortex

!!! vortex "Exercise"

    **Extend** the MLIR-emitting tool from [M2's exercise](m2-reading-mlir.md#for-vortex) so that it can emit the stage 10 kernel's matrix multiplication as structured ops instead of three nested `scf.for` loops, using only the `func`, `arith`, `memref` and `linalg` dialects. Keep the `scf` path; the new one is an option.

    1. **On paper, before any code:** the three indexing maps and the iterator types for the kernel, and a paragraph that derives each iterator type from the output's map, as this chapter did, rather than from memory of `linalg.matmul`.
    2. **A decision about `c`:** emit it as a memref operand written in place, matching `&mut`, or rewrite the function to take and return tensors. Argue from [decision 25](../decisions/references.md#d25) and Figure 4. Write down what would change in the function's signature and its callers under the option you did not choose.
    3. **A decision about zeroing:** the kernel overwrites `c`, while a structured op adds into its `outs`. Decide how your emitted code makes the result independent of `c`'s old contents, and write down why your choice keeps [decision 56](../decisions/numbers.md#d56) (the order and number of roundings for each element must match the `scf` path's).
    4. **Not yet:** tiling, fusion, vectorization or library calls ([M6](m6-affine-and-scf.md), [M8](m8-vectorization.md), [M9](m9-transform-dialect.md)); bufferization of a tensor version ([M7](m7-bufferization.md)); lowering to machine code ([M4](m4-dialect-conversion.md)); any change to the Vortex front end.

    **Proof that it works:**

    - The emitted file for the stage 10 kernel, and for one other shape, passes `mlir-opt` with no options, and a golden test pins its text.
    - After `--linalg-generalize-named-ops --convert-linalg-to-affine-loops`, the structured path and your existing `scf` path give loop nests that load and store the same elements and perform the same `arith` operations in the same order for each element of `c`. Write the comparison as a test, not as a visual check.
    - After `--convert-linalg-to-parallel-loops`, a test confirms that `k` is not a dimension of any `scf.parallel`, so your iterator claim is honest.
    - A canary, as in M2: change one shape in the emitted file, for example `b`'s column count in the signature only, and confirm that `mlir-opt` rejects it with a `linalg` shape error.
    - The M2 test that every floating-point `arith` operation carries `fastmath` `none` also passes on the structured path, payloads included.

## Key ideas

!!! recap "Questions you can now answer"

    - **What three parts describe a `linalg.generic`?** Indexing maps (one per operand), iterator types (one per dimension) and a payload region that computes one step.
    - **Where do a structured op's loop bounds come from?** From the operand shapes, through the indexing maps; the verifier rejects operands that disagree.
    - **Does the verifier check that a dimension marked `parallel` is?** No. Iterator types are a promise from whoever built the op; a false one is undefined behavior that a parallel lowering acts on.
    - **How do you tell from the maps which dimensions must be reductions?** A dimension missing from an output's map is shared by several steps that write the same output element.
    - **What is `linalg.matmul`, underneath?** A `linalg.generic` with fixed maps, iterator types and payload; `--linalg-generalize-named-ops` prints it that way.
    - **What does `outs` mean on tensors and on memrefs?** On both it gives each output element's starting value; on memrefs it is also the memory written, while on tensors the op returns a new value.
    - **Which Vortex promises does a structured op not record?** That an `&mut` output overlaps no input, one rounding per operation, and the order a reduction adds its terms.

## Where this comes back

!!! next "You will use this again in"

    - [M6. Loops: affine and scf](m6-affine-and-scf.md): *iteration space*, *lowering to loops*, *scf.parallel*
    - [M7. Bufferization](m7-bufferization.md): *tensor*, *memref*, *destination-passing style*, *tensor.empty*
    - [M8. Vectorization in MLIR](m8-vectorization.md): *payload*, *iterator types*
    - [M9. Schedules as IR: the transform dialect](m9-transform-dialect.md): *named op*, *tiling*, *split reduction*
    - [M10. MLIR for GPUs](m10-mlir-for-gpus.md): *parallel dimensions mapped to hardware*
    - [M11. End-to-end ML compilers](m11-ml-compilers.md): *structured ops*, *named ops*
    - [M12. Designing Vortex's GPU path](m12-vortex-gpu-path.md): *iterator types as a promise*, *reduction order*
    - [P7. Loop transformations](../optimize/p7-loop-transformations.md): *tiling*, *parallel and reduction dimensions*
    - [P9. The polyhedral model](../optimize/p9-polyhedral-model.md): *affine maps*, *iteration space*

## Sources and further reading

The linalg dialect page is the primary source: read its sections on the six properties of payload-carrying ops with this chapter's examples open, then the rationale document for why the dialect avoids raising.[^linalg][^rationale] Vasilache and colleagues describe the whole structured code generation path in MLIR, from linalg on tensors through tiling, fusion, bufferization and vectorization.[^vasilache] The Bufferization page's section on destination-passing style is the bridge to [M7](m7-bufferization.md).[^bufferization]

[^linalg]: MLIR Project, "'linalg' Dialect", sections "Set of Key Transformations", "High-Level Description of Linalg Ops", "Payload-Carrying Ops" (properties 1 to 6), "Data Representation: Views" and "Named Payload-Carrying Ops", and the entries `linalg.generic` and `linalg.fill`. <https://mlir.llvm.org/docs/Dialects/Linalg/>
[^rationale]: MLIR Project, "Linalg Dialect Rationale: The Case For Compiler-Friendly Custom Operations", sections "Preservation of Information", "Declarative Specification: Avoid Raising" and "Progressive Lowering: Don't Lose Information too Quickly". <https://mlir.llvm.org/docs/Rationale/RationaleLinalgDialect/>
[^tensor]: MLIR Project, "'tensor' Dialect", introduction and entry `tensor.empty`. <https://mlir.llvm.org/docs/Dialects/TensorOps/>
[^memref]: MLIR Project, "'memref' Dialect", entries `memref.alloc` and `memref.subview`. <https://mlir.llvm.org/docs/Dialects/MemRef/>
[^builtin]: MLIR Project, "Builtin Dialect", entries `MemRefType` and `StridedLayoutAttr`. <https://mlir.llvm.org/docs/Dialects/Builtin/>
[^bufferization]: MLIR Project, "Bufferization", section "Destination-Passing Style". <https://mlir.llvm.org/docs/Bufferization/>
[^passes]: MLIR Project, "Passes", entries `-convert-linalg-to-loops`, `-convert-linalg-to-affine-loops`, `-convert-linalg-to-parallel-loops`, `-linalg-generalize-named-ops` and `-linalg-specialize-generic-ops`. <https://mlir.llvm.org/docs/Passes/>
[^scf]: MLIR Project, "'scf' Dialect", entry `scf.parallel`. <https://mlir.llvm.org/docs/Dialects/SCFDialect/>
[^transform]: MLIR Project, "Transform Dialect", entry `transform.structured.split_reduction`. <https://mlir.llvm.org/docs/Dialects/Transform/>
[^testing]: MLIR Project, "Testing Guide", section on diagnostic tests. <https://mlir.llvm.org/getting_started/TestingGuide/>
[^vasilache]: Nicolas Vasilache, Oleksandr Zinenko, Aart J.C. Bik, Mahesh Ravishankar, Thomas Raoux, Alexander Belyaev, Matthias Springer, Tobias Gysi, Diego Caballero, Stephan Herhut, Stella Laurenzo and Albert Cohen, "Composable and Modular Code Generation in MLIR: A Structured and Retargetable Approach to Tensor Compiler Construction", arXiv:2202.03293, 2022. <https://arxiv.org/abs/2202.03293>
[^m2-matmul]: [M2. Reading MLIR, "A named operation hides a region"](m2-reading-mlir.md#a-named-operation-hides-a-region).
[^m2-table]: [M2. Reading MLIR, "Where Vortex's facts would live"](m2-reading-mlir.md#where-vortexs-facts-would-live).
