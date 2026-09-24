# G12. Fusion case study: FlashAttention

<p class="page-intro">Attention is one formula, softmax(QKᵀ/√d)V, but a literal implementation of that formula spends most of its time writing and re-reading a matrix that never needed to touch memory at all. FlashAttention is a worked example of turning that observation into a faster, still exact, kernel, and it shows what a Vortex compiler would have to reason about to do the same.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 30 minutes · Builds on: [G10. The GPU matmul ladder](g10-matmul-ladder.md), [G11. Matrix units](g11-matrix-units.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is a warp (or SIMD-group), and why does it matter that its threads run one instruction together?"

        A warp is a fixed-size group of threads, 32 on current NVIDIA GPUs and on the M4 Pro's SIMD-groups, that the hardware issues one instruction to at once. What one thread's load or store costs often depends on where all 32 threads' addresses land together, not on any thread alone.

        Introduced in [G2. The SIMT execution model](g2-simt.md).

    ??? question "Why does staging a tile through shared memory cut the traffic a kernel sends to global memory?"

        Shared memory (LDS, threadgroup memory) is on-chip and belongs to one thread block. Data read into it once and reused by many threads of that block is read from global memory only once, instead of once per reuse.

        Introduced in [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md).

    ??? question "May a Vortex compiler regroup the additions in a floating-point sum, or fuse a multiply and an add into one rounding step?"

        No. Each `f32` or `f64` operation is one IEEE 754 operation, rounded once, and an implementation must not contract, reassociate or reorder them.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

    ??? question "Within one call, may the variable borrowed as `&mut` also appear as another argument?"

        No. A variable borrowed as `&mut` must not appear in any other argument of that same call, so a callee's output can never alias its inputs.

        Introduced in [Value semantics, decision 25](../decisions/references.md#d25).

!!! goals "In this chapter"

    - Explain why a direct implementation of attention is bound by memory traffic, not by arithmetic, as the sequence gets longer.
    - Derive the online-softmax recurrence and show that it reproduces ordinary two-pass softmax exactly.
    - Recognize how tiling generalizes that recurrence to fuse two matrix multiplications and a softmax into one pass that never writes the full score matrix.
    - Explain why FlashAttention recomputes score blocks in its backward pass instead of storing them, and connect that choice to rematerialization.
    - Connect FlashAttention's later versions, work partitioning and hardware asynchrony, to occupancy and matrix-unit ideas already covered.

## A worked example: attention on four keys

Take one query vector `q` and four key and value vectors, `k0` to `k3` and `v0` to `v3`, all length 2. Ordinary (scaled) dot-product attention runs three steps:

1. Score each key against the query: `s[j] = q · k[j]`, giving one number per key.
2. Turn the scores into weights that sum to 1: `p = softmax(s)`.
3. Mix the values by those weights: `out = Σ p[j] · v[j]`.

With `q = (1, 0)`, `k0 = (1, 0)`, `k1 = (0, 1)`, `k2 = (1, 1)`, `k3 = (-1, 0)`, the scores are `s = (1, 0, 1, -1)`. Softmax turns those into weights that favor `k0` and `k2` (the keys most aligned with `q`) and a small weight on `k3` (pointing away from `q`). The output is a weighted blend of `v0` through `v3`.

Four keys is nothing: `s` is a 4-element array that lives comfortably in a register. The chapter's second example computes exactly this, and its tiled version, over these numbers; --8<-- gives its full output below.

## Why the score matrix is the problem

Now let sequence length be `N` instead of 4, as it is in a real transformer layer: `N` queries, each scored against `N` keys, gives an `N` by `N` score matrix, often called `S`. Step 1 of the recipe above builds the whole of `S`. Step 2 needs a whole row of `S` at a time to normalize it. Step 3 reads all of `S` again to weight the values.

An implementation that follows the three steps as separate operations, the way a sequence of ordinary matrix-library calls would, has nowhere to put `S` but device memory (HBM on a discrete GPU, the unified DRAM pool on Apple silicon, [G3](g3-memory-hierarchy.md)): it is written once by step 1 and read at least once more by steps 2 and 3. For `N` in the thousands, `S` is many megabytes, far past the on-chip capacity ([G3](g3-memory-hierarchy.md)) that vanished in the four-key example above. Every byte of `S` that round-trips to device memory is a byte the query, key and value vectors did not need to move at all: `Q`, `K` and `V` together hold `3 * N * d` numbers for a head dimension `d`, while `S` holds `N²`. Past a few hundred tokens, `S` dominates.

This is the same shape of problem [G1](g1-throughput-machines.md) opens with: an operation's cost is set by whichever of arithmetic or memory traffic is larger, and attention's arithmetic (each output needs a handful of multiply-adds per score) is small next to `N²` scores moved twice. FlashAttention's own description of the problem is exactly this: standard attention implementations spend their time moving the score matrix between GPU high-bandwidth memory and on-chip SRAM, not computing it.[^fa1] The fix is not a cleverer formula. `softmax(QKᵀ/√d)V` stays the formula. The fix is never writing `S` down.

## Softmax without keeping the whole row

Step 2 above looks like it needs the whole row of `S` at once: softmax's first pass finds the row's maximum (subtracted for numerical stability, so no exponential overflows), and a second pass exponentiates and sums. That is exactly what the first function in this chapter's opening example does, and it reads every input value twice.

The second function computes the same result from one pass. It keeps a running maximum `m` and a running sum `l`, and it keeps every output value it has written so far. Each new input `x[i]` can only do one of two things to what came before: leave the maximum alone, or raise it. If it raises the maximum from `m` to `x[i]`, every earlier term was computed against the wrong maximum, and

$$
\exp(x_j - m) = \exp(x_j - x_i) \cdot \exp(x_i - m)
$$

says exactly how to fix them: multiply the running sum, and every value already written, by `exp(m - x[i])` before folding in the new term. That factor is the online-softmax **rescale**. It is at most 1 (the new maximum can only be at least as large as the old one, so the exponent is at most 0), so it never causes overflow, and it is applied to whatever has already been accumulated, however far the sweep has gotten.

--8<-- "includes/examples/gpu/g12-flashattention/online_softmax.cpp.md"

Both functions produce the same six weights, within floating-point tolerance, and the second one reads the six-element input array once instead of twice. On six elements that difference does not matter. On the row of an attention score matrix, where each element also had to be computed from a dot product and will be reused to weight a value vector, reading it exactly once, instead of once per pass over the row, is the saving that matters.

??? check "The running max rises from 2.0 to 3.5 when a new element arrives. By what factor must the running sum and every value already written be rescaled before the new element is added in?"

    `exp(2.0 - 3.5) = exp(-1.5)`. Every earlier term was computed as if the maximum were 2.0; multiplying by `exp(2.0 - 3.5)` restates each of them as if the maximum had been 3.5 all along, which is what the algebra above shows.

## Tiling: fusing both matmuls and the softmax into one pass

Online softmax removes one array (the normalized weights) from having to be revisited. FlashAttention applies the identical rescale to two more running quantities and gets rid of `S` entirely.

Split the keys and values into blocks of `B` rows. Sweep the blocks in order. For each block, compute only that block's scores (`B` dot products, not `N`), and update three running values instead of one:

- `m`, the running maximum score, exactly as before;
- `l`, the running sum of `exp(score - m)`, exactly as before;
- `out`, the running weighted sum of value rows, `Σ exp(score - m) · v`, rescaled by the same factor as `l` whenever `m` grows.

After the last block, dividing `out` by `l` gives exactly the same result step 3 would have produced, because every term in the sum was rescaled to a common maximum along the way, the same algebra the check above used. At no point does a full row of scores exist: a block of `B` scores is the most that is ever resident, and it is consumed and discarded before the next block is loaded. `Q`, `K` and `V` are still read from device memory (there is no avoiding that: the inputs have to arrive somehow), but `S` is never written there at all. The three separate matrix operations, `QKᵀ`, softmax, and `× V`, become one fused loop, and the fusion is what removes the traffic, not any change to the arithmetic each operation performs.

<figure class="vx-figure">
<div class="vx-stepper">
<div class="vx-step">
<p><strong>Step 1. First block.</strong> A block of <code>B</code> keys and values arrives. Its scores are computed, its own maximum becomes the running maximum <code>m</code> (nothing came before it to rescale), and <code>l</code> and <code>out</code> take their first values.</p>
<svg viewBox="0 0 760 230" role="img" aria-label="Step 1: the first key/value block sets the running max, sum and output; no rescale is needed yet.">
<defs><marker id="g12-f1-h1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="20" y="20" width="140" height="50" rx="4"/>
<text class="vx-text" x="90" y="50" text-anchor="middle">Q row</text>
<rect class="vx-box-accent" x="20" y="90" width="140" height="50" rx="4"/>
<text class="vx-text" x="90" y="120" text-anchor="middle">block 0 of K, V</text>
<line class="vx-flow" x1="160" y1="115" x2="260" y2="115" marker-end="url(#g12-f1-h1)"/>
<rect class="vx-box-strong" x="280" y="60" width="220" height="110" rx="4"/>
<text class="vx-text" x="292" y="82">score block: B values</text>
<text class="vx-mono" x="292" y="106">m = max(block 0)</text>
<text class="vx-mono" x="292" y="128">l = &#931; exp(score - m)</text>
<text class="vx-mono" x="292" y="150">out = &#931; exp(score - m)&#183;v</text>
<line class="vx-line" x1="500" y1="115" x2="600" y2="115" marker-end="url(#g12-f1-h1)"/>
<rect class="vx-box" x="610" y="30" width="130" height="42" rx="4"/>
<text class="vx-mono" x="622" y="56">m, l, out</text>
<text class="vx-text-muted" x="622" y="100">kept on chip;</text>
<text class="vx-text-muted" x="622" y="118">block 0's scores</text>
<text class="vx-text-muted" x="622" y="136">are discarded</text>
<text class="vx-text-muted" x="622" y="154">once folded in</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 2. Next block, larger maximum.</strong> A new block arrives whose own maximum is larger than the running <code>m</code>. Before anything from this block is added, <code>l</code> and <code>out</code> are rescaled by <code>exp(old m − new m)</code>, restating them as if the new maximum had held all along; only then are this block's terms folded in.</p>
<svg viewBox="0 0 760 250" role="img" aria-label="Step 2: a later block raises the running max, so the running sum and output are rescaled before the new block's terms are added.">
<defs><marker id="g12-f1-h2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box-accent" x="20" y="20" width="140" height="50" rx="4"/>
<text class="vx-text" x="90" y="50" text-anchor="middle">block 1 of K, V</text>
<rect class="vx-box" x="20" y="150" width="140" height="60" rx="4"/>
<text class="vx-mono" x="32" y="172">m, l, out</text>
<text class="vx-text-muted" x="32" y="196">(from block 0)</text>
<line class="vx-flow" x1="160" y1="180" x2="300" y2="100" marker-end="url(#g12-f1-h2)"/>
<text class="vx-mono" x="185" y="150">&#215; exp(m&#8320; - m&#8321;)</text>
<rect class="vx-box-strong" x="300" y="60" width="240" height="110" rx="4"/>
<text class="vx-text" x="312" y="82">new block's scores: B values</text>
<text class="vx-mono" x="312" y="106">m &#8592; max(m, block 1)</text>
<text class="vx-mono" x="312" y="128">l &#8592; l&#183;scale + &#931; exp(score - m)</text>
<text class="vx-mono" x="312" y="150">out &#8592; out&#183;scale + &#931; exp(&#183;)&#183;v</text>
<line class="vx-line" x1="540" y1="115" x2="620" y2="115" marker-end="url(#g12-f1-h2)"/>
<rect class="vx-box" x="630" y="70" width="110" height="90" rx="4"/>
<text class="vx-text-muted" x="642" y="94">updated</text>
<text class="vx-mono" x="642" y="116">m, l, out</text>
<text class="vx-text-muted" x="642" y="140">still one block</text>
<text class="vx-text-muted" x="642" y="156">of scores held</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 3. After the last block: normalize.</strong> No more blocks remain. Dividing the running output by the running sum gives the same result the three-step recipe would have, and the full <code>N</code>-long score row, crossed out here, was never assembled.</p>
<svg viewBox="0 0 760 200" role="img" aria-label="Step 3: after the last block, dividing the running output by the running sum gives the final attention output; the full score row was never materialized.">
<defs><marker id="g12-f1-h3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="20" y="30" width="160" height="60" rx="4"/>
<text class="vx-mono" x="32" y="54">m, l, out</text>
<text class="vx-text-muted" x="32" y="76">after the last block</text>
<line class="vx-flow" x1="180" y1="60" x2="280" y2="60" marker-end="url(#g12-f1-h3)"/>
<rect class="vx-box-strong" x="290" y="30" width="200" height="60" rx="4"/>
<text class="vx-mono" x="302" y="66">out &#247; l</text>
<line class="vx-line" x1="490" y1="60" x2="570" y2="60" marker-end="url(#g12-f1-h3)"/>
<rect class="vx-box-accent" x="580" y="30" width="140" height="60" rx="4"/>
<text class="vx-text" x="650" y="66" text-anchor="middle">attention output</text>
<rect class="vx-box-bad" x="20" y="120" width="700" height="40" rx="4" opacity="0.4"/>
<text class="vx-text-muted" x="30" y="145" text-decoration="line-through">the full N &#215; N score row: never written to device memory</text>
</svg>
</div>
</div>
<figcaption>Figure 1. One query row swept across key/value blocks. Step 1 sets the running max, sum and output from the first block. Step 2 shows the rescale that keeps them correct when a later block's maximum is larger. Step 3 normalizes once, after the sweep, without ever having held a full row of scores.</figcaption>
</figure>

The second example in this chapter runs both the naive, whole-row version and this tiled version, on the same worked numbers from the opening section, and checks that they agree.

--8<-- "includes/examples/gpu/g12-flashattention/flash_tile.cpp.md"

The naive version's score buffer is the whole row, `N` elements; the tiled version's is one block, `B` elements, however large `N` grows. That gap is the whole idea: the score matrix shrinks from something that must live in device memory to something that never leaves on-chip memory at all.

??? check "The algorithm above still computes every one of the N² entries of S eventually, one block at a time. Why does that not cost the same device-memory traffic as materializing the whole matrix?"

    Because a block of scores is produced, folded into the running max, sum and output, and discarded before the next block is even loaded. At most one block's worth of scores exists at any moment, on chip, and none of it is ever written to device memory as a stored array; only Q, K and V (read once) and the final output (written once) cross that boundary.

## Recomputation instead of storage: the backward pass

Training needs a backward pass, which needs the same softmax weights the forward pass computed, to weight gradients the same way the forward pass weighted values. Storing the whole `S` for the backward pass to reuse would spend, on the way out, exactly the device-memory traffic tiling avoided on the way in.

FlashAttention's answer is to store almost nothing extra: only `m` and `l` for each row, two numbers instead of `N`. The backward pass then sweeps the same key/value blocks again and **recomputes** each score block from `Q`, `K` and the two saved numbers, rather than reading a stored block.[^fa1] This spends more arithmetic (every score is computed twice, once in each pass) to save device-memory traffic, which is the resource this whole chapter has been arguing is the scarce one. [C5](../backend/c5-spilling.md) meets the identical trade-off from the opposite direction: a register allocator facing too many live values chooses, for each one, whether to spill it to memory and reload it or to recompute it from other still-live values, **rematerialization**. FlashAttention's backward pass is the same choice, made at the scale of a whole score matrix instead of one register.

??? check "Recomputing a score block costs real floating-point operations that a version storing the block would not repeat. Why is that a good trade here?"

    Because arithmetic is cheap relative to device-memory traffic for this operation ("why the score matrix is the problem", above, and the roofline argument in G1): paying extra FLOPs, which the hardware has to spare, to avoid HBM bytes, which is the bottleneck, lowers the actual running time even though it raises the operation count.

## What changed from v1 to v3

The tiling and recomputation above are FlashAttention's original contribution.[^fa1] Two later papers keep that same fused, tiled shape and change how the work inside it is scheduled across the hardware.

**FlashAttention-2** repartitions the work: more of the loop runs in parallel across thread blocks (parallelizing over one attention head alone was not enough to fill a GPU), and work is split differently between the warps of a thread block, to cut the shared-memory traffic between them and reduce operations that are not matrix multiplies.[^fa2] Both changes are about **occupancy**, keeping the hardware's schedulers fed with independent work, which [G5](g5-occupancy.md) covers on its own. The paper reports about a 2× speedup over the original kernel and 50 to 73 percent of an A100's peak FLOPs/s, against roughly 25 to 40 percent for the original.[^fa2]

**FlashAttention-3** targets Hopper GPUs specifically, and layers on hardware features this book's matmul chapters describe in general: it overlaps the tensor-core matrix multiplies, the softmax arithmetic and the asynchronous copies that bring the next block in, using **warp specialization** (different warps of one thread block running different roles at once), and it adds an FP8 path.[^fa3] [G11](g11-matrix-units.md) is where those asynchronous copies and tensor-core instructions come from, and low-precision matrix units are exactly its subject. The paper reports FlashAttention-2 reaching about 35 percent utilization on an H100; the FP16 path of FlashAttention-3 reaches 1.5 to 2.0 times its speed, up to about 740 TFLOPs/s (75 percent utilization), and the FP8 path reaches close to 1.2 PFLOPs/s.[^fa3]

Read as a sequence, the three papers separate three questions a compiler for this kind of fused kernel eventually has to answer on its own: does the fusion avoid the memory traffic (v1), is the resulting work spread well enough across the machine's parallel units (v2), and does it overlap with the machine's asynchronous copy and compute paths instead of waiting on them in turn (v3). None of the three changes what attention computes.

## Fusion as something a compiler decides

Everything above was invented and hand-written by people, in CUDA. But the recipe, given `S = A op1 B` then `op2` row-wise then `S' op3 C`, choose a block size that keeps one block of the intermediate on chip, and rescale a running accumulator whenever a new block changes the reduction's running statistic, is not specific to attention or to softmax. It is a fusion and tiling decision over a short chain of structured operations, the same shape [O6](../optimize/o6-redundancy.md) describes for **loop fusion**: avoid writing an intermediate array that the next loop would only read straight back.

MLIR's structured-op dialects give that chain a name a compiler can act on. A matrix multiply is `linalg.matmul`; a row-wise reduction like softmax's max and sum is `linalg.generic` with a reduction iterator; [M5](../mlir/m5-structured-ops.md) is where both come from. [M9](../mlir/m9-transform-dialect.md)'s transform dialect lets a schedule (which loops to fuse, which to tile, and at what size) be written as data the compiler interprets, rather than as another hand-written pass, and [M3](../mlir/m3-passes-and-rewriting.md) covers the rewriting machinery underneath it. None of this is FlashAttention-specific: it is the general question of when a chain of operations should share one loop nest instead of each writing its result out and letting the next one read it back, applied to a chain that happens to be attention.

Whether Apple's own software stack takes this path is not settled by anything verified for this chapter: `metal-flash-attention` is an independent, open-source Metal implementation of the same idea,[^mfa] not a documented Apple framework, and this book's research into Apple's matrix facilities ([G11](g11-matrix-units.md)) found SIMD-group matrix operations and, from Metal 4, tensor types, but no first-party fused-attention kernel to cite.

## What this means for Vortex

Vortex already has two of the properties this fusion needs. Its arrays carry a fixed shape, known at compile time,[^vx-arrays] so a tile size chosen for a block of `K` and `V` is a compile-time constant, not a runtime guess a library has to make. And [decision 25](../decisions/references.md#d25), which forbids a `&mut` argument from aliasing any other argument of the same call, is exactly the guarantee a fused kernel needs to accumulate into its output tile across many blocks: nothing written into `out` can be read back as if it were still part of `Q`, `K` or `V`. The [stage 10](../compiler/guide/stage-10-matrix-multiplication.md) kernel's signature, inputs by shared reference and the output by `&mut`, is the shape an attention function would take too.

The other property, strict floating-point order ([decision 56](../decisions/numbers.md#d56)), is a constraint on the fusion, not a help. The rescale trick changes *when* a term is added to a running sum, not what IEEE 754 operations are performed or in what order relative to each other; the two functions in this chapter's first example compute the same six weights, within tolerance, precisely because online softmax reorders no addition, it only defers some multiplications by a factor of 1 until the moment a rescale makes them not 1. A fusion pass that changed the order in which a Vortex program's floating-point operations execute, rather than only where their operands live, would be a different program, and decision 56 rules that out.

## For Vortex

!!! vortex "Exercise"

    **Build** a device-memory traffic estimator for a short chain of operations shaped like attention: a matrix multiply, a row-wise reduction, and a second matrix multiply, over arrays whose shapes are known (Vortex's arrays always are). Given a tile size `B` for the reduction's input dimension, the estimator reports, for two strategies:

    1. **Unfused:** each operation writes its full result to device memory and the next operation reads it back. Report the bytes written and read for the intermediate that sits between the first matmul and the reduction, and for the one between the reduction and the second matmul, as a function of the sequence length `N`, the head dimension `d` and the element size.
    2. **Fused and tiled:** the three operations share one loop nest over blocks of size `B`, keeping a running accumulator on chip and never writing either intermediate to device memory. Report the bytes moved for the inputs and the final output only.

    **Not yet:** writing the fused kernel itself, in any language ([M9](../mlir/m9-transform-dialect.md) is where a schedule for it would eventually live), the backward pass, or anything about how thread blocks or warps divide the work ([G5](g5-occupancy.md), [G6](g6-synchronization.md)).

    **Proof that it works:**

    - For `N = 4096`, `d = 64`, `f32` elements, the unfused estimate's two intermediates should each be `N²` elements, tens of megabytes, and the fused estimate should report none.
    - For the four-key, two-dimension numbers this chapter worked by hand, the unfused estimate's intermediate should be 4 elements per query row and the fused estimate, with `B = 2`, should report a resident buffer of 2 elements, matching what the chapter's second example measured directly.
    - A differential check: for a handful of small, made-up shapes and tile sizes, the fused estimate's reported bytes for `Q`, `K` and `V` should equal the unfused estimate's, exactly (fusion changes what is written between the operations, never what the operations read to begin with).
    - A remark accompanying each estimate, as the [sixth principle](../philosophy.md#6-explain-performance-decisions) asks, stating which intermediate the fused version avoided and how many bytes that saved at the given `N`.

## Key ideas

!!! recap "Questions you can now answer"

    - **Why is a direct implementation of attention memory-bound?** Its arithmetic per output is small, but it must write and re-read an N × N score matrix, which grows faster than the inputs that produced it.
    - **What does online softmax avoid?** Ever holding a whole row of scores at once: a running maximum and sum, rescaled whenever the maximum grows, reproduce the two-pass result from a single sweep.
    - **What does tiling add on top of online softmax?** The same rescale applied to a running weighted-output accumulator, so the whole attention output is built block by block and the full score matrix is never written to device memory.
    - **Why recompute scores in the backward pass instead of storing them?** Storing them would spend, on the way out, the same device-memory traffic tiling avoided on the way in; recomputing trades cheap arithmetic for that traffic, the same choice a register allocator makes when it rematerializes instead of spilling.
    - **What did FlashAttention-2 change?** How the work is partitioned across thread blocks and warps, to raise occupancy and reduce non-matmul operations, not the fused, tiled shape itself.
    - **What did FlashAttention-3 add?** Overlap between tensor-core matrix multiplies, softmax arithmetic and asynchronous data movement via warp specialization on Hopper, plus a lower-precision path.
    - **Why can a Vortex compiler fuse this chain without changing a program's answer?** Fusion only changes where a partial result lives and when a deferred multiplication by 1 happens; it never reorders or contracts a floating-point operation, and the `&mut` no-alias rule guarantees the output tile cannot be read back as an input.

## Where this comes back

!!! next "You will use this again in"

    - [G13. Tile languages](g13-tile-languages.md): *tile size*, *block*
    - [G14. Measuring GPU code](g14-measuring-gpu-code.md): *occupancy*, *device-memory traffic*
    - [M9. Schedules as IR: the transform dialect](../mlir/m9-transform-dialect.md): *tiling*, *fusion*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *fusion as a compiler decision*
    - [C5. Spilling, splitting and rematerialization](../backend/c5-spilling.md): *rematerialization*

## Sources and further reading

Read the FlashAttention paper first, for the rescale identity and the algorithm; FlashAttention-2 and FlashAttention-3 are shorter and build directly on it.

[^fa1]: Tri Dao et al., "FlashAttention: Fast and Memory-Efficient Exact Attention with IO-Awareness", NeurIPS 2022. <https://arxiv.org/abs/2205.14135>
[^fa2]: Tri Dao, "FlashAttention-2: Faster Attention with Better Parallelism and Work Partitioning", 2023. <https://arxiv.org/abs/2307.08691>
[^fa3]: Jay Shah et al., "FlashAttention-3: Fast and Accurate Attention with Asynchrony and Low-precision", 2024. <https://arxiv.org/abs/2407.08608>
[^mfa]: philipturner, "metal-flash-attention", GitHub repository. <https://github.com/philipturner/metal-flash-attention>
[^vx-arrays]: [Arrays and shapes 7.2](../specification/arrays.md#72-dimension-rules) and [decision 11](../decisions/arrays.md#d11): an array's rank and every dimension are fixed at compile time.
