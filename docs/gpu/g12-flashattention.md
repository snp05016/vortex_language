# G12. Fusion case study: FlashAttention

<p class="page-intro">Attention is one formula, softmax(QKᵀ/√d)V, and a direct implementation of it spends most of its time writing two large matrices to device memory and reading them back. FlashAttention computes the same formula without ever storing them. This chapter counts the bytes that makes a difference, derives the algebra that makes it possible, and separates the parts a Vortex compiler could do on its own from the part that changes a program's rounding.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 45 minutes · Builds on: [G10. The GPU matmul ladder](g10-matmul-ladder.md), [G11. Matrix units](g11-matrix-units.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does operational intensity count, and what does a low value say about a kernel?"

        Flops performed per byte of traffic between the caches and main memory. A kernel whose intensity sits below the machine's ridge point runs at the speed of memory, not of arithmetic, however many flops the machine could do.

        Introduced in [P3. The roofline model](../optimize/p3-roofline.md).

    ??? question "How much threadgroup (shared) memory can one threadgroup use on the owner's M4 Pro?"

        32,768 bytes, as `maxThreadgroupMemoryLength` reports, matching the 32 KB that Apple documents for the Apple4 GPU family onward. Every tile a kernel keeps on chip has to fit in that budget.

        Introduced in [G3. The GPU memory hierarchy](g3-memory-hierarchy.md).

    ??? question "When may two adjacent loops be fused into one?"

        When they run the same number of iterations, one runs exactly when the other does, and no dependence between them would point backwards (a negative distance) in the fused loop. Those are the conditions LLVM's fusion pass checks.

        Introduced in [P7. Loop transformations](../optimize/p7-loop-transformations.md).

    ??? question "Does a shared-memory tiled matrix product print the same bits as the naive one, with strict `f32` arithmetic?"

        Yes. Tiling changes which thread holds a value and when it is read, but each output still adds its products in the same order, so every rounding step is the same.

        Introduced in [G10. The GPU matmul ladder](g10-matmul-ladder.md).

    ??? question "May a Vortex compiler regroup the additions in a floating-point sum, or fuse a multiply and an add into one rounding step?"

        No. Each `f32` or `f64` operation is one IEEE 754 operation, rounded once, and an implementation must not contract, reassociate or reorder them.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Count the device-memory traffic of standard attention and of a fused, tiled version, and explain why the difference depends on the size of on-chip memory.
    - Recognize the reduction barrier that stops ordinary loop fusion inside softmax, and the rescale that removes it.
    - Trace the online softmax recurrence and FlashAttention's block loop by hand, and check the result against the three-step recipe.
    - Explain what FlashAttention-2 and FlashAttention-3 changed (work partitioning, then overlap on Hopper) and why neither changes the arithmetic.
    - Decide which of these transformations a Vortex compiler may apply by itself under decision 56, and which only a programmer may write.

## Attention, by hand

**Attention** is the operation at the center of a transformer layer. Each position in a sequence has a **query** vector, which says what it is looking for, a **key** vector, which says what it offers, and a **value** vector, which holds what it passes on. The output for one query is a weighted average of all the values, weighted by how well the query matches each key. The length of each vector is the **head dimension**, `d`.

Take one query and four keys and values, all of length 2:

| | vector | score `q · k` | value |
| --- | --- | --- | --- |
| query `q` | (1, 0) | | |
| key 0 | (0, 1) | 0 | (1, 2) |
| key 1 | (−1, 0) | −1 | (3, 4) |
| key 2 | (1, 0) | 1 | (5, 6) |
| key 3 | (1, 1) | 1 | (7, 8) |

The recipe has three steps.

1. **Score** each key: `s[j] = q · k[j]`, a dot product. The scores are (0, −1, 1, 1).
2. Turn the scores into weights that are positive and sum to 1 with **softmax**: `p[j] = exp(s[j]) / Σ exp(s[i])`.
3. **Mix** the values: `out = Σ p[j] · v[j]`.

The full formula also divides every score by √d before the softmax. Multiplying the query by 1/√d first has the same effect, so this chapter treats that scale as already folded into `q` and leaves it out.

Softmax as written can overflow: `exp(90)` is already too large for an `f32`. Implementations therefore subtract the largest score first, as FlashAttention does for numerical stability; this changes nothing mathematically, because the factor `exp(−max)` appears in both the numerator and the denominator and cancels.[^fa1] This is **safe softmax**. Here the largest score is 1, so the four terms are `exp(0 − 1)`, `exp(−1 − 1)`, `exp(0)` and `exp(0)`: 0.3679, 0.1353, 1 and 1. Their sum is 2.5032, and the weights are 0.1470, 0.0541, 0.3995 and 0.3995.

The output is 0.1470 · (1, 2) + 0.0541 · (3, 4) + 0.3995 · (5, 6) + 0.3995 · (7, 8) = (5.1030, 6.1030). Keys 2 and 3 point the same way as the query and take most of the weight; key 1 points away and takes little. Keep this number: every version of the algorithm in this chapter must reproduce it.

## Where the time goes: the matrices in between

A real layer has `N` queries, not one, and `N` keys, where `N` is the sequence length. Stack the vectors as rows and the three steps become matrix operations on `Q`, `K` and `V`, each `N` × `d`:[^fa1]

- `S = QKᵀ`, an `N` × `N` matrix of scores;
- `P = softmax(S)`, applied to each row, another `N` × `N` matrix;
- `O = PV`, the `N` × `d` output.

The FlashAttention paper spells out the **standard implementation**: one kernel computes `S` and writes it to the GPU's high-bandwidth memory (HBM), a second reads `S` and writes `P`, and a third reads `P` and `V` and writes `O`.[^fa1] To **materialize** an intermediate is to store the whole of it in memory like this, so that the next operation can read it back. Figure 1 shows where the bytes go.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Standard attention as three kernels that write S and P to device memory and read them back, beside one fused kernel that reads Q, K and V and writes only O.">
<defs><marker id="g12-f1-h" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text-muted" x="245" y="22" text-anchor="middle">Standard: three kernels</text>
<text class="vx-text-muted" x="630" y="22" text-anchor="middle">Fused: one kernel</text>
<rect class="vx-box" x="20" y="40" width="130" height="60" rx="4"/>
<text class="vx-text-muted" x="85" y="62" text-anchor="middle">kernel 1</text>
<text class="vx-mono" x="85" y="86" text-anchor="middle">S = QKᵀ</text>
<rect class="vx-box" x="180" y="40" width="130" height="60" rx="4"/>
<text class="vx-text-muted" x="245" y="62" text-anchor="middle">kernel 2</text>
<text class="vx-mono" x="245" y="86" text-anchor="middle">P = softmax(S)</text>
<rect class="vx-box" x="340" y="40" width="130" height="60" rx="4"/>
<text class="vx-text-muted" x="405" y="62" text-anchor="middle">kernel 3</text>
<text class="vx-mono" x="405" y="86" text-anchor="middle">O = PV</text>
<line class="vx-line" x1="45" y1="220" x2="45" y2="102" marker-end="url(#g12-f1-h)"/>
<text class="vx-mono" x="50" y="165">Q, K</text>
<line class="vx-flow" x1="125" y1="100" x2="125" y2="218" marker-end="url(#g12-f1-h)"/>
<text class="vx-text-accent" x="130" y="150">S</text>
<text class="vx-text-muted" x="130" y="168">N×N</text>
<line class="vx-flow" x1="205" y1="220" x2="205" y2="102" marker-end="url(#g12-f1-h)"/>
<text class="vx-text-accent" x="210" y="150">S</text>
<line class="vx-flow" x1="285" y1="100" x2="285" y2="218" marker-end="url(#g12-f1-h)"/>
<text class="vx-text-accent" x="290" y="150">P</text>
<text class="vx-text-muted" x="290" y="168">N×N</text>
<line class="vx-flow" x1="365" y1="220" x2="365" y2="102" marker-end="url(#g12-f1-h)"/>
<text class="vx-text-accent" x="370" y="150">P</text>
<text class="vx-mono" x="370" y="168">V</text>
<line class="vx-line" x1="445" y1="100" x2="445" y2="218" marker-end="url(#g12-f1-h)"/>
<text class="vx-mono" x="450" y="165">O</text>
<line class="vx-line" x1="495" y1="30" x2="495" y2="205"/>
<rect class="vx-box-strong" x="520" y="40" width="220" height="60" rx="4"/>
<text class="vx-text" x="630" y="62" text-anchor="middle">one fused kernel</text>
<text class="vx-text-muted" x="630" y="86" text-anchor="middle">tiles of S and P stay on chip</text>
<line class="vx-line" x1="560" y1="220" x2="560" y2="102" marker-end="url(#g12-f1-h)"/>
<text class="vx-mono" x="566" y="165">Q, K, V</text>
<line class="vx-line" x1="700" y1="100" x2="700" y2="218" marker-end="url(#g12-f1-h)"/>
<text class="vx-mono" x="706" y="165">O</text>
<rect class="vx-box-accent" x="20" y="222" width="720" height="50" rx="4"/>
<text class="vx-text" x="380" y="252" text-anchor="middle">device memory (HBM on a discrete GPU, unified DRAM on Apple silicon)</text>
</svg>
<figcaption>Figure 1. Left: standard attention writes the N × N matrices S and P to device memory and reads each back once (the animated arrows). Right: a fused kernel reads Q, K and V and writes O; S and P exist only a tile at a time, on chip.</figcaption>
</figure>

Count the traffic of the standard version in elements, per attention head. Kernel 1 reads `Q` and `K` (2`N·d`) and writes `S` (`N²`). Kernel 2 reads `S` and writes `P` (2`N²`). Kernel 3 reads `P` and `V` (`N²` + `N·d`) and writes `O` (`N·d`). The total is 4`N²` + 4`N·d`. The paper's Theorem 2 states the same order, Θ(`N·d` + `N²`) accesses.[^fa1] The inputs and output grow with `N`; the intermediates grow with `N²`.

The paper's example shape is GPT-2's, `N` = 1024 and `d` = 64.[^fa1] There `N²` is 1,048,576 and `N·d` is 65,536, so the standard version moves 4,456,448 elements, and 94 percent of them are `S` and `P`. In 16-bit floats that is 8,912,896 bytes per head. The two matrix products perform 2`N²d` flops each, 268,435,456 in all, which gives an operational intensity of about 30 flops per byte.

That is low for the GPU the paper measures on. An A100 moves 1.5 to 2.0 TB/s from HBM[^fa1] and peaks at 312 TFLOPs/s of 16-bit matrix arithmetic,[^fa2] so its ridge point lies between about 156 and 208 flops per byte. Attention written this way runs at the speed of memory. The softmax kernel is the extreme case: a few flops per element against a read and a write of every element, the paper's own example of a memory-bound operation.[^fa1]

The paper measured the difference. For GPT-2 medium (sequence length 1024, head dimension 64, 16 heads, batch 64) on an A100, forward plus backward:[^fa1]

| | Standard | FlashAttention |
| --- | --- | --- |
| GFLOPs | 66.6 | 75.2 |
| HBM reads and writes (GB) | 40.3 | 4.4 |
| Runtime (ms) | 41.7 | 7.3 |

FlashAttention does more arithmetic, because its backward pass recomputes work (a later section explains why), and still runs several times faster, because it moves about a ninth of the bytes. The paper's conclusion is that HBM accesses, not flops, decide attention's runtime.[^fa1] The standard version also needs `N²` elements of memory per head to hold `S` alone, which limits the sequence lengths it can run at all.

??? check "For `N` = 4096 and `d` = 64 in 16-bit floats, how many bytes of one head's standard-attention traffic are `S` and `P`, and how many are `Q`, `K`, `V` and `O`?"

    `S` and `P` are each written once and read once: 4`N²` = 67,108,864 elements, or 134,217,728 bytes (128 MiB). The inputs and output account for 4`N·d` = 1,048,576 elements, 2 MiB. Quadrupling `N` from 1024 multiplied the intermediate traffic by 16 and the rest by 4.

## What a compiler sees

To a compiler, the formula is a chain of operations. MLIR's Linalg dialect ([M5](../mlir/m5-structured-ops.md)) has named operations for all three steps, including `linalg.softmax`, documented as a numerically stable softmax.[^mlir-softmax] The example below writes attention for four queries and four keys as three named operations, then applies one transform-dialect step, `decompose_interface`, which replaces the softmax with the loop nests that implement it.[^mlir-decompose]

--8<-- "includes/examples/gpu/g12-flashattention/attention_chain.mlir.md"

After decomposition there are six operations, each a loop nest of its own: the first matrix product; a reduction to the maximum of each row (`%6`); an element-wise `exp(s − max)` (`%7`); a reduction to the sum of each row (`%9`); an element-wise division (`%10`); and the second matrix product. Three results are full 4 × 4 tensors (`%2`, `%7` and `%10`). The decomposition starts the maximum at the most negative finite `f32`, not at minus infinity, a detail a hand-written kernel would also have to choose. If each operation runs as its own kernel, each 4 × 4 result is materialized, which is the standard implementation again.

## Where ordinary fusion stops

**Kernel fusion** runs several operations in one kernel, so that an intermediate passes from one to the next in registers or shared memory instead of through device memory.[^fa1] On a CPU the same move is loop fusion ([P7](../optimize/p7-loop-transformations.md#fusion-and-fission)), and the transform dialect has an operation for it, `fuse_into_containing_op` ([M9](../mlir/m9-transform-dialect.md#fusing-a-producer-into-a-consumers-loop)). The paper notes that compilers already fuse many element-wise operations automatically.[^fa1] Attention does not fuse the same way, and the reason is in the shape of the chain.

Look at the edges of the chain in Figure 2. Some edges pass one element at a time: the consumer's iteration `j` needs only what the producer's iteration `j` made. `exp(s − max)` feeding the row sum is such an edge. Fused, each exponential is added to the sum as soon as it exists. Other edges pass a **reduction** result, one value computed from a whole row: the consumer cannot start its first iteration until the producer has finished its last. The exponential needs the finished maximum, and the division needs the finished sum.

<figure class="vx-figure">
<svg viewBox="0 0 760 250" role="img" aria-label="The six operations of decomposed attention in a row. Edges from a row reduction to its consumer are marked as barriers that wait for the whole row; the other edges pass one element at a time.">
<defs><marker id="g12-f2-h" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="10" y="90" width="100" height="60" rx="4"/>
<text class="vx-mono" x="60" y="116" text-anchor="middle">S = QKᵀ</text>
<text class="vx-text-muted" x="60" y="138" text-anchor="middle">matmul</text>
<rect class="vx-box-strong" x="138" y="90" width="100" height="60" rx="4"/>
<text class="vx-mono" x="188" y="116" text-anchor="middle">m = max</text>
<text class="vx-text-muted" x="188" y="138" text-anchor="middle">row reduction</text>
<rect class="vx-box" x="266" y="90" width="100" height="60" rx="4"/>
<text class="vx-mono" x="316" y="116" text-anchor="middle">E = exp(S−m)</text>
<text class="vx-text-muted" x="316" y="138" text-anchor="middle">element-wise</text>
<rect class="vx-box-strong" x="394" y="90" width="100" height="60" rx="4"/>
<text class="vx-mono" x="444" y="116" text-anchor="middle">l = sum</text>
<text class="vx-text-muted" x="444" y="138" text-anchor="middle">row reduction</text>
<rect class="vx-box" x="522" y="90" width="100" height="60" rx="4"/>
<text class="vx-mono" x="572" y="116" text-anchor="middle">P = E / l</text>
<text class="vx-text-muted" x="572" y="138" text-anchor="middle">element-wise</text>
<rect class="vx-box" x="650" y="90" width="100" height="60" rx="4"/>
<text class="vx-mono" x="700" y="116" text-anchor="middle">O = PV</text>
<text class="vx-text-muted" x="700" y="138" text-anchor="middle">matmul</text>
<line class="vx-line" x1="110" y1="120" x2="136" y2="120" marker-end="url(#g12-f2-h)"/>
<line class="vx-flow" x1="238" y1="120" x2="264" y2="120" marker-end="url(#g12-f2-h)"/>
<line class="vx-line" x1="366" y1="120" x2="392" y2="120" marker-end="url(#g12-f2-h)"/>
<line class="vx-flow" x1="494" y1="120" x2="520" y2="120" marker-end="url(#g12-f2-h)"/>
<line class="vx-line" x1="622" y1="120" x2="648" y2="120" marker-end="url(#g12-f2-h)"/>
<path class="vx-line" d="M60 90 C 60 40, 300 40, 300 88" fill="none" marker-end="url(#g12-f2-h)"/>
<path class="vx-line" d="M330 90 C 330 40, 560 40, 560 88" fill="none" marker-end="url(#g12-f2-h)"/>
<text class="vx-text-muted" x="180" y="46" text-anchor="middle">S again</text>
<text class="vx-text-muted" x="445" y="46" text-anchor="middle">E again</text>
<rect class="vx-box-bad" x="206" y="165" width="90" height="26" rx="4"/>
<text class="vx-text" x="251" y="183" text-anchor="middle">barrier</text>
<rect class="vx-box-bad" x="462" y="165" width="90" height="26" rx="4"/>
<text class="vx-text" x="507" y="183" text-anchor="middle">barrier</text>
<line class="vx-line" x1="20" y1="222" x2="60" y2="222"/>
<text class="vx-text-muted" x="68" y="226">passes one element at a time: fuses at any granularity</text>
<line class="vx-flow" x1="420" y1="222" x2="460" y2="222"/>
<text class="vx-text-muted" x="468" y="226">waits for a whole row: fuses per row at best</text>
</svg>
<figcaption>Figure 2. The dependences of decomposed attention for one row of queries. Element-wise edges (solid) fuse freely. The two edges leaving a row reduction (animated, marked "barrier") cannot start until the reduction has seen the whole row, and the arcs show that S and E are each needed twice, before and after a barrier.</figcaption>
</figure>

A barrier does not forbid all fusion. A compiler can fuse the whole chain one row at a time: for query row `i`, compute that row of `S`, its maximum, its exponentials and sum, its weights, and its share of `O`, keeping one row of scores in a local buffer. Every floating-point operation is the same, in the same order, so the results are bit-identical, and the `N` × `N` matrices never reach device memory.

But the buffer holds `N` scores. On the M4 Pro, whose threadgroups have 32,768 bytes ([G3](g3-memory-hierarchy.md)), one row of `f32` scores fits only while `N` is at most 8,192, and a kernel needs room for several rows and for tiles of `K` and `V` besides. Per-row fusion stops working exactly where attention is most expensive, at long sequences.

To go further, the barrier itself has to go. That takes algebra, not a loop transformation.

## Online softmax: a running maximum that can be corrected

The barrier comes from the maximum: every term `exp(x[j] − m)` needs the final `m`. Suppose instead a loop keeps a **running maximum** `m`, the largest value seen so far, and a **running sum** `l` of `exp(x[j] − m)` over the values seen so far. When a new value `x` arrives that is larger than `m`, every term already in `l` was computed against the wrong maximum. The identity

$$
e^{x_j - m_{\text{new}}} = e^{x_j - m_{\text{old}}} \cdot e^{m_{\text{old}} - m_{\text{new}}}
$$

says how to repair them all at once: multiply `l` by `exp(m_old − m_new)`, then add the new term. That factor is the **rescale**. Because the maximum never falls, its exponent is never positive, so the factor is at most 1 and cannot overflow.

Milakov and Gimelshein published this as **online softmax** in 2018.[^online] Safe softmax makes three passes over its input: one for the maximum, one for the sum, one to write the outputs, which is three loads and one store per element. The online version computes the maximum and the sum together in one pass and writes the outputs in a second, three accesses per element instead of four.[^online] They also define a way to merge the (maximum, sum) pairs of two halves of a vector, the same rescale applied to both sides, so separate workers can each reduce part of a row and combine their results afterwards.[^online] The example counts the accesses and checks the merge.

--8<-- "includes/examples/gpu/g12-flashattention/online_softmax.cpp.md"

Eighteen loads against twelve: on six elements it is a curiosity, and on a memory-bound kernel it is a quarter of the traffic. The merge is what makes online softmax parallel. The paper states that the merge is associative and commutative, so in exact arithmetic any grouping of the parts gives the same pair, and the reductions of [G6](g6-synchronization.md) apply. In floating point each grouping rounds differently, which matters later in this chapter.

??? check "The running maximum rises from 2.0 to 3.5 when a new element arrives. By what factor must the running sum be multiplied before the new element's term is added?"

    `exp(2.0 − 3.5) = exp(−1.5)`, about 0.2231. Every term in the sum was computed as `exp(x − 2.0)`; multiplying by `exp(2.0 − 3.5)` turns each into `exp(x − 3.5)`, as if the maximum had been 3.5 from the start. The new element then contributes `exp(3.5 − 3.5) = 1`.

## FlashAttention's forward pass: one block at a time

Online softmax still produces the weights `p[j]` in a second pass. Attention does not need the weights: it needs `Σ p[j] · v[j]`. FlashAttention carries a third running quantity, an **accumulator** holding the weighted sum of value rows so far, and rescales it by the same factor as `l`.[^fa1] Nothing then needs the finished maximum, and the barrier is gone.

It also works a **block** at a time, `B` keys and their values together, so that a block of `K` and `V` can sit in on-chip memory and each block of scores is computed with a matrix product ([G11](g11-matrix-units.md) is where those products run). For one query row, each block does four things:

1. compute the block's `B` scores and their maximum;
2. raise the running maximum if the block's maximum is larger, and compute the rescale factor;
3. multiply `l` and the accumulator by that factor;
4. add the block's terms `exp(s − m)` to `l` and `exp(s − m) · v` to the accumulator.

After the last block, one division, accumulator over `l`, gives the output. The paper decomposes the softmax of two concatenated blocks exactly this way and proves the algorithm returns `softmax(QKᵀ)V` with O(`N`) extra memory.[^fa1]

Walk it through on the worked example with blocks of `B` = 2: block 0 holds keys 0 and 1, block 1 holds keys 2 and 3.

| step | scores | running `m` | rescale | `l` | accumulator |
| --- | --- | --- | --- | --- | --- |
| block 0 | 0, −1 | 0 | none (first block) | 1 + 0.3679 = 1.3679 | (1, 2) + 0.3679 · (3, 4) = (2.1036, 3.4715) |
| block 1 | 1, 1 | 1 | exp(0 − 1) = 0.3679 | 1.3679 · 0.3679 + 1 + 1 = 2.5032 | (2.1036, 3.4715) · 0.3679 + (5, 6) + (7, 8) = (12.7739, 15.2771) |
| divide | | | | | (12.7739, 15.2771) / 2.5032 = (5.1030, 6.1030) |

The result matches the three-step recipe, and at no moment did more than two scores exist. Figure 3 steps through the same sweep.

<figure class="vx-figure">
<div class="vx-stepper">
<div class="vx-step">
<p><strong>Step 1. Block 0.</strong> Keys 0 and 1 arrive with their values. Their scores are 0 and −1, so the running maximum becomes 0. Nothing came before, so nothing is rescaled; <code>l</code> and the accumulator take their first values.</p>
<svg viewBox="0 0 760 200" role="img" aria-label="Step 1: block 0 has scores 0 and minus 1; the running maximum becomes 0, the running sum 1.3679 and the accumulator (2.1036, 3.4715).">
<defs><marker id="g12-f3-h1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="20" y="20" width="150" height="46" rx="4"/>
<text class="vx-mono" x="95" y="48" text-anchor="middle">q = (1, 0)</text>
<rect class="vx-box-accent" x="20" y="90" width="150" height="60" rx="4"/>
<text class="vx-text" x="95" y="114" text-anchor="middle">block 0</text>
<text class="vx-mono" x="95" y="136" text-anchor="middle">keys 0, 1</text>
<line class="vx-flow" x1="170" y1="120" x2="268" y2="120" marker-end="url(#g12-f3-h1)"/>
<rect class="vx-box-strong" x="280" y="40" width="250" height="130" rx="4"/>
<text class="vx-text" x="292" y="64">scores: 0, −1</text>
<text class="vx-mono" x="292" y="90">m = 0</text>
<text class="vx-mono" x="292" y="114">l = 1 + 0.3679 = 1.3679</text>
<text class="vx-mono" x="292" y="138">acc = (2.1036, 3.4715)</text>
<text class="vx-text-muted" x="292" y="160">two scores held, then discarded</text>
<line class="vx-line" x1="530" y1="105" x2="598" y2="105" marker-end="url(#g12-f3-h1)"/>
<rect class="vx-box" x="610" y="70" width="130" height="70" rx="4"/>
<text class="vx-mono" x="675" y="98" text-anchor="middle">m, l, acc</text>
<text class="vx-text-muted" x="675" y="122" text-anchor="middle">kept on chip</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 2. Block 1 raises the maximum.</strong> Its scores are 1 and 1, larger than the running maximum 0. Before its terms are added, <code>l</code> and the accumulator are multiplied by <code>exp(0 − 1) = 0.3679</code>, which restates them as if the maximum had been 1 all along.</p>
<svg viewBox="0 0 760 220" role="img" aria-label="Step 2: block 1 has scores 1 and 1; the maximum rises from 0 to 1, so the running sum and accumulator are multiplied by 0.3679 before the block's terms are added, giving l = 2.5032 and accumulator (12.7739, 15.2771).">
<defs><marker id="g12-f3-h2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box-accent" x="20" y="20" width="150" height="60" rx="4"/>
<text class="vx-text" x="95" y="44" text-anchor="middle">block 1</text>
<text class="vx-mono" x="95" y="66" text-anchor="middle">keys 2, 3</text>
<rect class="vx-box" x="20" y="130" width="150" height="60" rx="4"/>
<text class="vx-mono" x="95" y="154" text-anchor="middle">m, l, acc</text>
<text class="vx-text-muted" x="95" y="176" text-anchor="middle">from block 0</text>
<line class="vx-line" x1="170" y1="50" x2="278" y2="80" marker-end="url(#g12-f3-h2)"/>
<line class="vx-flow" x1="170" y1="160" x2="278" y2="140" marker-end="url(#g12-f3-h2)"/>
<text class="vx-mono" x="180" y="190">× 0.3679</text>
<rect class="vx-box-strong" x="290" y="30" width="280" height="150" rx="4"/>
<text class="vx-text" x="302" y="54">scores: 1, 1</text>
<text class="vx-mono" x="302" y="80">m: 0 → 1, scale = exp(0 − 1)</text>
<text class="vx-mono" x="302" y="106">l = 1.3679 × 0.3679 + 1 + 1</text>
<text class="vx-mono" x="302" y="126">  = 2.5032</text>
<text class="vx-mono" x="302" y="152">acc = (12.7739, 15.2771)</text>
<line class="vx-line" x1="570" y1="105" x2="628" y2="105" marker-end="url(#g12-f3-h2)"/>
<rect class="vx-box" x="640" y="70" width="100" height="70" rx="4"/>
<text class="vx-mono" x="690" y="98" text-anchor="middle">m, l, acc</text>
<text class="vx-text-muted" x="690" y="122" text-anchor="middle">updated</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 3. Divide once.</strong> No blocks remain. The accumulator divided by <code>l</code> is the attention output, the same (5.1030, 6.1030) the three-step recipe gave. The full row of four scores never existed at once.</p>
<svg viewBox="0 0 760 180" role="img" aria-label="Step 3: after the last block, the accumulator (12.7739, 15.2771) divided by l = 2.5032 gives the output (5.1030, 6.1030); the full score row was never assembled.">
<defs><marker id="g12-f3-h3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="20" y="30" width="190" height="60" rx="4"/>
<text class="vx-mono" x="115" y="56" text-anchor="middle">acc = (12.7739, 15.2771)</text>
<text class="vx-mono" x="115" y="78" text-anchor="middle">l = 2.5032</text>
<line class="vx-flow" x1="210" y1="60" x2="288" y2="60" marker-end="url(#g12-f3-h3)"/>
<rect class="vx-box-strong" x="300" y="30" width="150" height="60" rx="4"/>
<text class="vx-mono" x="375" y="66" text-anchor="middle">acc ÷ l</text>
<line class="vx-line" x1="450" y1="60" x2="528" y2="60" marker-end="url(#g12-f3-h3)"/>
<rect class="vx-box-accent" x="540" y="30" width="200" height="60" rx="4"/>
<text class="vx-mono" x="640" y="66" text-anchor="middle">out = (5.1030, 6.1030)</text>
<rect class="vx-box-bad" x="20" y="120" width="720" height="40" rx="4"/>
<text class="vx-text-muted" x="380" y="145" text-anchor="middle">the whole row of scores (0, −1, 1, 1) was never held at once</text>
</svg>
</div>
</div>
<figcaption>Figure 3. The worked example swept in blocks of two keys. Step 1 sets the running maximum, sum and accumulator from block 0. Step 2 rescales them when block 1 raises the maximum, then adds block 1's terms. Step 3 divides once. At most two scores exist at any moment.</figcaption>
</figure>

The example program runs the same sweep and prints each block's state beside the three-step result.

--8<-- "includes/examples/gpu/g12-flashattention/flash_tile.cpp.md"

This version divides by `l` only at the end. The first paper's Algorithm 1 instead normalizes the output at every block, and it orders the loops the other way round: the outer loop walks blocks of `K` and `V`, the inner loop walks blocks of `Q`, and each step reads the output block and its `m` and `l` from device memory and writes them back.[^fa1] FlashAttention-2 swapped the loops, so that one query block's statistics stay on chip for the whole sweep, and deferred the division to the end, the form shown here.[^fa2]

??? check "Swap the order of the blocks: block 0 now holds keys 2 and 3 (scores 1, 1), block 1 holds keys 0 and 1 (scores 0, −1). Work out `m`, the rescale factor, `l` and the accumulator after each block. Does the output change?"

    After block 0: `m` = 1, `l` = 2, accumulator (5, 6) + (7, 8) = (12, 14). Block 1's maximum is 0, below 1, so the factor is `exp(1 − 1)` = 1 and nothing changes before its terms are added: `l` = 2 + 0.3679 + 0.1353 = 2.5032, accumulator (12 + 0.3679 + 0.4060, 14 + 0.7358 + 0.5413) = (12.7739, 15.2771). The output is (5.1030, 6.1030) again. The order of blocks changes which rescales happen, not the result in exact arithmetic.

## How big a block, and how much traffic

The first paper sets the block sizes from `M`, the size of on-chip memory: blocks of `K` and `V` hold ⌈`M`/4`d`⌉ rows, and blocks of `Q` hold the smaller of that and `d`.[^fa1] Counting `M` in elements, as the paper's bounds do, the M4 Pro's 32,768 bytes hold 8,192 `f32` values. For `d` = 64 that gives ⌈8192/256⌉ = 32 rows for each block. This is the paper's starting rule, not a tuned value: FlashAttention-2 notes that larger blocks cut shared-memory traffic but need more registers and more shared memory, and past some size the registers spill.[^fa2]

The block size sets the traffic. In the FlashAttention-2 loop order, each block of `B` query rows reads the whole of `K` and `V` once, so a head reads `K` and `V` ⌈`N`/`B`⌉ times, plus `Q` once and `O` once. In elements, that is 2`N·d`·⌈`N`/`B`⌉ + 2`N·d`, against 4`N²` + 4`N·d` for the standard version. For the GPT-2 shape (`N` = 1024, `d` = 64):

| version | elements moved per head | compared with standard |
| --- | --- | --- |
| standard (three kernels) | 4,456,448 | 1.00 |
| fused, `B` = 32 | 4,325,376 | 0.97 |
| fused, `B` = 64 | 2,228,224 | 0.50 |
| fused, `B` = 128 | 1,179,648 | 0.26 |

For large `N`, the standard version's traffic divided by the fused version's tends to 2`B`/`d`. Two lessons follow. First, fusion removed the `N²` intermediates but introduced repeated reads of `K` and `V`, and with small blocks those cost almost as much as the intermediates did. Second, the saving is a factor set by on-chip memory, not by `N`. This is the paper's Theorem 2: FlashAttention needs Θ(`N²d²`/`M`) HBM accesses, and for typical `d` (64 to 128) and `M` (around 100 KB), `d²` is many times smaller than `M`.[^fa1] Proposition 3 adds that no exact attention algorithm can do asymptotically better for every `M` in the range the theorem covers.[^fa1]

This model counts every read of `K` and `V` as a device-memory access, as the paper's analysis does. On real hardware, blocks running at the same time read the same `K` and `V` and some of those reads are served by the L2 cache, so the counts are an upper bound; [G14](g14-measuring-gpu-code.md) covers measuring the real figure. The paper also measured the effect of block size on an A100: runtime fell as blocks grew, until beyond 256 the kernel was limited by other factors, and larger blocks no longer fit in shared memory.[^fa1]

Rabe and Staats had shown in 2021 that attention need not use memory quadratic in sequence length; their implementation for accelerators needs O(√`n`) memory.[^rabe] FlashAttention, which cites them for the softmax decomposition, organized the same algebra around the traffic between memory levels, proved the bounds above, and ran the whole computation as one CUDA kernel.[^fa1]

## The backward pass: recompute instead of store

Training runs a backward pass, which computes gradients with respect to `Q`, `K` and `V`. It needs `S` and `P` again. Storing them from the forward pass would bring back the `N²` memory and traffic the forward pass avoided.

FlashAttention stores only the output `O` and the two statistics `m` and `l` for each row, then **recomputes** each block of `S` and `P` from blocks of `Q`, `K` and `V` during the backward pass.[^fa1] The paper calls this a form of selective gradient checkpointing. It does more flops, which is why the table above shows 75.2 GFLOPs against 66.6, and it still makes the backward pass faster, because the recomputation is cheaper than the HBM traffic it replaces.[^fa1] FlashAttention-2 shrinks the saved statistics to one number per row, the **logsumexp** `m + log(l)`, from which both can be recovered.[^fa2]

A register allocator makes the same trade at a smaller scale. When a value is cheap to compute again, [C5](../backend/c5-spilling.md#rematerializing-instead-of-reloading) recomputes it before its next use instead of storing it to a stack slot and reloading it: **rematerialization**. The allocator's candidates are values like constants, each one instruction to recompute. FlashAttention recomputes a whole block of scores, many flops, and still wins, because the alternative is traffic to device memory, which is far slower than arithmetic on this machine.

## FlashAttention-2: the same algorithm, partitioned differently

The first FlashAttention reached 25 to 40 percent of the A100's peak flops, well short of a good matrix product; its successor traced the gap to how the work is divided among thread blocks and warps.[^fa2] The paper made three changes.[^fa2]

**Fewer non-matmul flops.** On an A100, 16-bit matrix arithmetic peaks at 312 TFLOPs/s, but other `f32` arithmetic at 19.5 TFLOPs/s, so each non-matmul flop costs about 16 times as much.[^fa2] Rescaling the output at every block is non-matmul work. Keeping the accumulator unscaled and dividing once at the end, as the walk-through above does, removes most of it.

**More thread blocks.** The first version gave each (batch, head) pair its own thread block. With long sequences, batches and head counts tend to be small, which can leave too few thread blocks to fill the A100's 108 streaming multiprocessors. FlashAttention-2 also splits the sequence into query blocks, each its own thread block with no communication between them, which raises **occupancy**, the fraction of the machine that has work ([G5](g5-occupancy.md)).[^fa2] The paper credits this loop swap and the extra parallelism to Phil Tillet's implementation in Triton, a tile language that [G13](g13-tile-languages.md) covers.[^fa2]

**A different split between warps.** Inside a thread block, four warps share the work, and Figure 4 shows the two choices.

<figure class="vx-figure">
<svg viewBox="0 0 760 280" role="img" aria-label="Two ways to split one thread block's attention work across four warps. Left, the first FlashAttention splits K and V across the warps, so each warp holds a partial output that must be added through shared memory. Right, FlashAttention-2 splits Q across the warps, so each warp owns whole output rows and no exchange is needed.">
<defs><marker id="g12-f4-h" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text-muted" x="185" y="20" text-anchor="middle">FlashAttention: split K and V</text>
<text class="vx-text-muted" x="575" y="20" text-anchor="middle">FlashAttention-2: split Q</text>
<rect class="vx-box" x="20" y="32" width="330" height="34" rx="4"/>
<text class="vx-mono" x="185" y="54" text-anchor="middle">Q block (every warp reads all of it)</text>
<rect class="vx-box-accent" x="20" y="84" width="75" height="44" rx="4"/>
<text class="vx-mono" x="57" y="111" text-anchor="middle">K,V ¼</text>
<rect class="vx-box-accent" x="105" y="84" width="75" height="44" rx="4"/>
<text class="vx-mono" x="142" y="111" text-anchor="middle">K,V ¼</text>
<rect class="vx-box-accent" x="190" y="84" width="75" height="44" rx="4"/>
<text class="vx-mono" x="227" y="111" text-anchor="middle">K,V ¼</text>
<rect class="vx-box-accent" x="275" y="84" width="75" height="44" rx="4"/>
<text class="vx-mono" x="312" y="111" text-anchor="middle">K,V ¼</text>
<text class="vx-text-muted" x="57" y="146" text-anchor="middle">warp 0</text>
<text class="vx-text-muted" x="142" y="146" text-anchor="middle">warp 1</text>
<text class="vx-text-muted" x="227" y="146" text-anchor="middle">warp 2</text>
<text class="vx-text-muted" x="312" y="146" text-anchor="middle">warp 3</text>
<line class="vx-flow" x1="57" y1="152" x2="150" y2="192" marker-end="url(#g12-f4-h)"/>
<line class="vx-flow" x1="142" y1="152" x2="175" y2="192" marker-end="url(#g12-f4-h)"/>
<line class="vx-flow" x1="227" y1="152" x2="195" y2="192" marker-end="url(#g12-f4-h)"/>
<line class="vx-flow" x1="312" y1="152" x2="220" y2="192" marker-end="url(#g12-f4-h)"/>
<rect class="vx-box-bad" x="60" y="194" width="250" height="56" rx="4"/>
<text class="vx-text" x="185" y="216" text-anchor="middle">shared memory: add 4 partial</text>
<text class="vx-text" x="185" y="238" text-anchor="middle">outputs, after a barrier</text>
<line class="vx-line" x1="380" y1="30" x2="380" y2="260"/>
<rect class="vx-box" x="410" y="32" width="330" height="34" rx="4"/>
<text class="vx-mono" x="575" y="54" text-anchor="middle">K, V block (every warp reads all of it)</text>
<rect class="vx-box-accent" x="410" y="84" width="75" height="44" rx="4"/>
<text class="vx-mono" x="447" y="111" text-anchor="middle">Q ¼</text>
<rect class="vx-box-accent" x="495" y="84" width="75" height="44" rx="4"/>
<text class="vx-mono" x="532" y="111" text-anchor="middle">Q ¼</text>
<rect class="vx-box-accent" x="580" y="84" width="75" height="44" rx="4"/>
<text class="vx-mono" x="617" y="111" text-anchor="middle">Q ¼</text>
<rect class="vx-box-accent" x="665" y="84" width="75" height="44" rx="4"/>
<text class="vx-mono" x="702" y="111" text-anchor="middle">Q ¼</text>
<text class="vx-text-muted" x="447" y="146" text-anchor="middle">warp 0</text>
<text class="vx-text-muted" x="532" y="146" text-anchor="middle">warp 1</text>
<text class="vx-text-muted" x="617" y="146" text-anchor="middle">warp 2</text>
<text class="vx-text-muted" x="702" y="146" text-anchor="middle">warp 3</text>
<line class="vx-line" x1="447" y1="152" x2="447" y2="192" marker-end="url(#g12-f4-h)"/>
<line class="vx-line" x1="532" y1="152" x2="532" y2="192" marker-end="url(#g12-f4-h)"/>
<line class="vx-line" x1="617" y1="152" x2="617" y2="192" marker-end="url(#g12-f4-h)"/>
<line class="vx-line" x1="702" y1="152" x2="702" y2="192" marker-end="url(#g12-f4-h)"/>
<rect class="vx-box-strong" x="410" y="194" width="75" height="56" rx="4"/>
<text class="vx-mono" x="447" y="227" text-anchor="middle">O ¼</text>
<rect class="vx-box-strong" x="495" y="194" width="75" height="56" rx="4"/>
<text class="vx-mono" x="532" y="227" text-anchor="middle">O ¼</text>
<rect class="vx-box-strong" x="580" y="194" width="75" height="56" rx="4"/>
<text class="vx-mono" x="617" y="227" text-anchor="middle">O ¼</text>
<rect class="vx-box-strong" x="665" y="194" width="75" height="56" rx="4"/>
<text class="vx-mono" x="702" y="227" text-anchor="middle">O ¼</text>
</svg>
<figcaption>Figure 4. One thread block's forward-pass work split across four warps. Left: each warp takes a quarter of K and V, so it ends with a partial sum for every output row, and the four partials meet in shared memory after a barrier. Right: each warp takes a quarter of the query rows and owns those output rows outright; no exchange is needed.</figcaption>
</figure>

The first version split `K` and `V` across the warps. Each warp then held part of the sum for every output row, and the warps had to write their partial results to shared memory, synchronize and add them: the paper calls this the "split-K" scheme and names its shared-memory traffic as a cost. FlashAttention-2 splits `Q` instead, so each warp produces its own output rows with no communication.[^fa2] The same choice appears in [G6](g6-synchronization.md): a reduction split across workers needs a combining step, and a split along an independent dimension does not.

Together the changes gave about twice the speed of the first version, 50 to 73 percent of the A100's peak.[^fa2] None of them changed the formula. They changed which hardware unit does which part of it, and when.

## FlashAttention-3: overlap on Hopper

On an H100, FlashAttention-2 reaches only 35 percent utilization.[^fa3] FlashAttention-3 targets the Hopper generation's asynchronous hardware: TMA copies that move tiles from global to shared memory, and WGMMA matrix instructions issued by a warpgroup of four warps, both described in [G11](g11-matrix-units.md#moving-tiles-without-the-lanes).[^fa3]

The first technique is **warp specialization**: some warps of a thread block act as **producers**, which only issue TMA loads of the next `K` and `V` blocks, and the rest as **consumers**, which only compute. The two sides hand buffers back and forth through a pipeline, so loading the next block overlaps computing the current one.[^fa3]

The second technique attacks the exponential. An H100 SXM5 delivers 989 TFLOPS of 16-bit matrix arithmetic but only 3.9 TFLOPS of special functions such as the exponential, which runs on a separate multi-function unit.[^fa3] With head dimension 128 there are 512 times more matrix flops than exponentials, but the exponential's throughput is 256 times lower, so the exponentials alone can take half as long as the matrix products.[^fa3] FlashAttention-3 uses barriers to make two warpgroups take turns, one doing its softmax while the other runs its matrix products, which the paper calls **pingpong scheduling**. For the 16-bit forward pass with head dimension 128 and sequence length 8192, it raised throughput from 570 to 620-640 TFLOPS.[^fa3]

The third technique is an FP8 path. It quantizes each block of `Q`, `K` and `V` with its own scale, and it uses **incoherent processing**: multiplying `Q` and `K` by the same random orthogonal matrix before quantizing, which leaves `QKᵀ` unchanged in exact arithmetic but spreads a few large outlier values across many entries. The paper reports 2.6 times lower numerical error than a baseline FP8 attention.[^fa3] Overall the 16-bit version runs 1.5 to 2.0 times faster than FlashAttention-2 on an H100, up to 740 TFLOPs/s (75 percent utilization), and the FP8 version reaches close to 1.2 PFLOPs/s.[^fa3] Narrower inputs change the arithmetic itself, which [G11](g11-matrix-units.md#precision-what-the-unit-computes-is-not-what-f32-code-says) discusses.

Read in order, the three papers answer three separate questions about a fused kernel. Does it avoid the traffic (version 1)? Is its work spread well across the machine (version 2)? Does it overlap copies, matrix units and other arithmetic instead of waiting for each in turn (version 3)?

On Apple silicon, the independent `metal-flash-attention` project ports the algorithm to Metal. Its README reports that Apple GPUs lack native `f32` atomics, which the FlashAttention-2 backward pass relies on, and describes a different backward pass that performs seven matrix products instead of five in exchange for parallelizing across both dimensions of the attention matrix.[^mfa] The same algorithm meets a different machine and needs a different partition.

## Fusion as something a compiler decides

Split FlashAttention into its parts and ask, for each, whether a compiler could find it.

- **Element-wise and per-row fusion.** Yes. These are loop fusion with the conditions [P7](../optimize/p7-loop-transformations.md#fusion-and-fission) lists, plus a buffer sized from known shapes. [M9](../mlir/m9-transform-dialect.md#fusing-a-producer-into-a-consumers-loop) shows the transform-dialect operation that does it for tensors.
- **Tiling and block sizes.** Yes, as a schedule. The block loop is tiling ([G10](g10-matmul-ladder.md#tiling-is-a-schedule-change-not-new-arithmetic)), and the block size can come from a cost model like the traffic count above or from search.
- **Recomputation.** Yes in principle: it is the same cost comparison a rematerializing allocator makes, with memory traffic as the cost.
- **The online rescale.** Not as a loop transformation. It replaces one computation with a different one that is equal only in exact arithmetic. A compiler finds it only by recognizing softmax followed by a matrix product as a known pattern, or by being given it, as a named operation such as `linalg.softmax` or as a kernel in a library.

The last point matters for Vortex because of rounding. The online sum computes `l · scale + e`, where the three-pass sum computes each term against the final maximum directly. In exact arithmetic these are equal. In floating point, scaling a sum and summing scaled terms round at different places, and `exp(a) · exp(b)` is two rounded results multiplied and rounded again, not `exp(a + b)` rounded once. The example below shows the first effect with nothing but additions and multiplications.

--8<-- "includes/examples/gpu/g12-flashattention/rescale_order.cpp.md"

Almost two pairs in five give a different double depending on whether the rescale comes before or after the addition. The example builds with `-ffp-contract=off` because otherwise GCC, and Clang within one expression, may fuse `a * s + b * s` into a fused multiply-add, which would change the result a third way; [decision 56](../decisions/numbers.md#d56) forbids that contraction in Vortex for the same reason.

??? check "A Vortex optimization pass fuses the loop that computes `exp(s − m)` into the loop that sums those values. A second pass rewrites a three-pass softmax into the online form. Which of the two may run without the programmer asking?"

    The first. Fusing those loops computes the same operations on the same values in the same order, so every rounding step and the final bits are unchanged. The second replaces the computation with one that is equal only in exact arithmetic: it adds rescale multiplications and changes where each rounding happens, so its results can differ in the last bits. Decision 56 forbids an implementation from changing a program's floating-point operations, and allows relaxed modes only as an explicit opt-in.

## What this means for Vortex

Three Vortex rules shape what its compiler can do with a chain like attention.

Array shapes are fixed at compile time.[^vx-arrays] Every size in this chapter's traffic count, `N`, `d`, the element size, is a constant the compiler knows, so it can compute the bytes each fusion choice saves and the on-chip footprint it needs, and report them, as the [sixth principle](../philosophy.md#6-explain-performance-decisions) asks, before generating any code.

A `&mut` argument cannot alias another argument of the same call ([decision 25](../decisions/references.md#d25)). The [stage 10](../compiler/guide/stage-10-matrix-multiplication.md) kernel takes its inputs by shared reference and its output by `&mut`, and an attention function would take the same shape. Accumulating into the output across many blocks is then safe: nothing written to the output can be read back through `Q`, `K` or `V`.

Floating-point operations keep their order ([decision 56](../decisions/numbers.md#d56)). This allows every fusion that moves values without changing operations, including per-row fusion of the whole chain, and rules out the online rescale as something the compiler introduces. A programmer who writes the online form in Vortex gets exactly that computation, rounded as written. The compiler's part is to recognize where the per-row buffer no longer fits on chip and say so.

## For Vortex

!!! vortex "Exercise"

    **Build** a fusion report for chains of loop nests in your compiler's IR, where an earlier nest writes a local array that the caller never sees and a later nest reads it.

    1. For each producer and consumer pair, classify the edge: **element-wise**, where the consumer's iteration needs only elements the producer has already written in the same traversal order, or **barrier**, where the consumer's first iteration needs a value the producer finishes only after the last iteration of a row or of the whole nest.
    2. Choose the finest legal granularity for each pair: fuse the whole nests (the conditions of [P7](../optimize/p7-loop-transformations.md#fusion-and-fission)), fuse per row by sharing the outer loop and keeping one row of the intermediate in a buffer, or do not fuse. A fusion must keep every floating-point operation and its order; it must never introduce a rescale.
    3. For each choice, compute from the fixed shapes the intermediate's footprint after fusion and the bytes of memory traffic saved, counting one write and one read of every materialized intermediate as this chapter did.
    4. Compare each per-row buffer with an on-chip budget taken from a target description (32,768 bytes for an M4 Pro threadgroup), and flag the ones that do not fit.
    5. A remark for every decision, as the [sixth principle](../philosophy.md#6-explain-performance-decisions) asks, naming the barrier that stopped a finer fusion and saying that removing it would need a rescaled running sum, which decision 56 does not let the compiler introduce.

    **Not yet:** generating GPU code ([M12](../mlir/m12-vortex-gpu-path.md) weighs the paths), tiling below one row, the online rescale itself (never automatic under decision 56), backward passes, and splitting work across warps ([G6](g6-synchronization.md)).

    **Proof that it works:**

    - An element-wise chain: a loop that writes `t[i] = 2.0 * x[i]` into a local `[f32; 1024]`, then a loop that writes `out[i] = t[i] + 1.0`. The report fuses the whole nests, the intermediate needs no buffer, and it saves 8,192 bytes (one 4,096-byte write and one read).
    - A row-normalized product, the attention chain without `exp`: for `[f32; 64, 64]` inputs, `s = a · b`, `l[i]` the sum of row `i` of `s`, `p[i, j] = s[i, j] / l[i]`, then `o = p · c`. The report fuses the product that makes `s` with the row sum (element-wise), marks the division as a barrier, and keeps at most one row of each intermediate, 256 bytes instead of 16,384. Neither 64 × 64 intermediate reaches memory any more, a saving of 65,536 bytes.
    - The same chain with rows of 16,384 `f32` values: the per-row buffer is 65,536 bytes, and the report flags that it exceeds the 32,768-byte budget.
    - A differential test: for a few dozen small shapes and random inputs, run the fused and unfused versions of each chain and compare every output bit for bit. They must match exactly; one differing bit means a fusion changed an operation.

## Key ideas

!!! recap "Questions you can now answer"

    - **Why is standard attention memory-bound?** It writes and reads back two `N` × `N` matrices, `S` and `P`, whose traffic grows with `N²` while its flops per byte stay far below the GPU's ridge point.
    - **What stops ordinary loop fusion inside softmax?** Two reductions, the row maximum and the row sum, whose consumers need the finished value; fusion can go no finer than one row without changing the arithmetic.
    - **What does online softmax add?** A running maximum and sum, rescaled by `exp(m_old − m_new)` whenever the maximum rises, which computes both in one pass and lets partial results from separate blocks be merged.
    - **How does FlashAttention's traffic compare with the standard version's?** Θ(`N²d²`/`M`) accesses against Θ(`N·d` + `N²`): the saving is a factor set by on-chip memory size and block size, and small blocks save little.
    - **Why recompute scores in the backward pass?** Storing `S` and `P` would bring back the `N²` traffic; recomputing blocks from `Q`, `K`, `V` and the saved statistics costs more flops and less time, the same trade as rematerialization.
    - **What did FlashAttention-2 and FlashAttention-3 change?** Version 2 cut non-matmul work, parallelized over query blocks and split `Q` rather than `K` and `V` across warps; version 3 overlapped TMA loads, matrix instructions and exponentials on Hopper and added FP8. Neither changed the formula.
    - **Which parts may a Vortex compiler apply by itself?** Fusion, tiling and recomputation that keep every floating-point operation and its order. The online rescale changes rounding, so under decision 56 only the programmer can choose it.

## Where this comes back

!!! next "You will use this again in"

    - [G13. Tile languages](g13-tile-languages.md): *block size*, *fused attention kernel*
    - [G14. Measuring GPU code](g14-measuring-gpu-code.md): *device-memory traffic*, *utilization*
    - [M9. Schedules as IR: the transform dialect](../mlir/m9-transform-dialect.md): *fusion*, *tiling*
    - [M11. End-to-end ML compilers](../mlir/m11-ml-compilers.md): *operator fusion*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *fusion as a compiler decision*

## Sources and further reading

Read the FlashAttention paper first, sections 2 and 3, for the standard implementation, the algorithm and the traffic bounds. Milakov and Gimelshein's short paper is the clearest derivation of online softmax. FlashAttention-2 and FlashAttention-3 build directly on the first paper and are mostly about GPU scheduling.

[^fa1]: Tri Dao et al., "FlashAttention: Fast and Memory-Efficient Exact Attention with IO-Awareness", NeurIPS 2022. Sections 2.1 to 3.2, Algorithms 0 and 1, Theorems 1 and 2, Proposition 3 and Figure 2. <https://arxiv.org/abs/2205.14135>
[^fa2]: Tri Dao, "FlashAttention-2: Faster Attention with Better Parallelism and Work Partitioning", 2023. Abstract and sections 3.1 to 3.3. <https://arxiv.org/abs/2307.08691>
[^fa3]: Jay Shah et al., "FlashAttention-3: Fast and Accurate Attention with Asynchrony and Low-precision", 2024. Abstract and section 3.1. <https://arxiv.org/abs/2407.08608>
[^online]: Maxim Milakov and Natalia Gimelshein, "Online normalizer calculation for softmax", 2018. Sections 2 and 3. <https://arxiv.org/abs/1805.02867>
[^rabe]: Markus N. Rabe and Charles Staats, "Self-attention Does Not Need O(n²) Memory", 2021. <https://arxiv.org/abs/2112.05682>
[^mlir-softmax]: MLIR Linalg dialect, "linalg.softmax (linalg::SoftmaxOp)". <https://mlir.llvm.org/docs/Dialects/Linalg/#linalgsoftmax-linalgsoftmaxop>
[^mlir-decompose]: MLIR Transform dialect, "transform.structured.decompose_interface". <https://mlir.llvm.org/docs/Dialects/Transform/#transformstructureddecompose_interface-transformdecomposeinterfaceop>
[^mfa]: Philip Turner, "metal-flash-attention", GitHub repository, README (read 2026-09-24). <https://github.com/philipturner/metal-flash-attention>
[^vx-arrays]: [Arrays and shapes 7.2](../specification/arrays.md#72-dimension-rules) and [decision 11](../decisions/arrays.md#d11): every dimension of an array is an integer constant expression, fixed at compile time.
