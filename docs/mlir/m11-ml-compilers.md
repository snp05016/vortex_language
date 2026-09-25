# M11. End-to-end ML compilers

<p class="page-intro">XLA, IREE, TVM, PyTorch's compiler, Triton and Mojo each take a machine-learning model, or one kernel of it, down to machine code through a staircase of intermediate forms. This chapter walks one small layer down that staircase with mlir-opt, shows where each system decides what to fuse and how to schedule it, and places Vortex among them: a language for one kernel at a time, not a compiler for whole models.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 35 minutes · Builds on: [M5. Structured ops: linalg, tensor and memref](m5-structured-ops.md), [M9. Schedules as IR: the transform dialect](m9-transform-dialect.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is a named op such as `linalg.matmul`, underneath?"

        A `linalg.generic` whose indexing maps, iterator types and region are already fixed and given a name. It lowers exactly like the equivalent generic op.

        Introduced in [M5. Structured ops: linalg, tensor and memref](m5-structured-ops.md#named-ops-the-same-interface-with-the-maps-already-chosen).

    ??? question "What changes between the tensor form and the memref form of the same structured op?"

        The memref form writes through its `outs` operand and returns nothing. The tensor form cannot change `outs`, so it returns a new tensor instead: destination-passing style.

        Introduced in [M5. Structured ops: linalg, tensor and memref](m5-structured-ops.md#values-or-buffers-tensor-against-memref).

    ??? question "Does every lowering step keep every fact the level above stated?"

        No. The "parallel" marker on a `linalg.generic` survives `--convert-linalg-to-parallel-loops` but not `--convert-linalg-to-affine-loops`, although both start from the same file.

        Introduced in [M1. Why MLIR](m1-why-mlir.md#what-survives-a-step-and-what-does-not).

    ??? question "What does `transform.structured.fuse_into_containing_op` do to a producer?"

        It moves the producer inside a loop that already contains its consumer, so each iteration computes only the slice of the producer's result that the consumer needs, instead of the producer finishing first and leaving its whole result in memory.

        Introduced in [M9. Schedules as IR: the transform dialect](m9-transform-dialect.md#fusing-a-producer-into-a-consumers-loop).

    ??? question "May a Vortex compiler turn `sum += a[row, k] * b[k, column]` into one fused multiply-add?"

        No. Each `f32` operation is one IEEE 754 operation, rounded to nearest with ties to even, and a compiler must not contract, reassociate or reorder them. The product is rounded, then the sum.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Trace one matrix product followed by a ReLU from a whole-tensor graph through structured operations to loops over memory, reading each level's checked `mlir-opt` output.
    - Explain operator fusion as a change in when each result is used, and predict which operators a set of fusion rules will put in one kernel.
    - Compare XLA, IREE, TVM, PyTorch's TorchInductor, Triton and Mojo by what each reads, what it is built on, who decides fusion and schedule, and how it reaches an Apple GPU.
    - Describe how a hand-written kernel plugs into a framework as a custom operator, and what the framework's compiler can no longer do to it.
    - Place Vortex among these systems, including the cost of its strict floating-point rule to any fusion or search it might borrow.

## One layer, four programs

A **machine-learning framework** such as PyTorch, JAX or TensorFlow lets a person write a model as a program over whole arrays, which frameworks call **tensors**. An **ML compiler** turns that program, or the graph of operations recorded from it, into kernels for a CPU, GPU or accelerator. This chapter follows one small piece of a model the whole way down: one linear layer without a bias, followed by a ReLU, which replaces every negative number by zero.

Written as a formula, the layer computes $C = \max(0, A B)$, element by element, for $A$ of shape 8 × 16 and $B$ of shape 16 × 4. It is Vortex's [stage 10](../compiler/guide/stage-10-matrix-multiplication.md) kernel at the same sizes, with one more step at the end.

Every system in this chapter starts from something like the following file. It has one node per whole-tensor operation, no loops and no addresses. That is the **graph level**: the program as a graph whose nodes are operations on tensors and whose edges are the tensors flowing between them.

--8<-- "includes/examples/mlir/m11-ml-compilers/graph_level.mlir.md"

The file uses **tosa**, the Tensor Operator Set Architecture, an operator set whose MLIR dialect ships with MLIR itself. The dialect's documentation describes TOSA as the result of reconciling what many frameworks need with what CPU, GPU and NPU hardware can do.[^tosa] XLA and IREE read a different operator set, **StableHLO**, described by its project as a portability layer between frameworks and compilers; the same page mentions conversions from StableHLO to MLIR's `linalg` and `tosa` dialects.[^stablehlo] StableHLO is not part of the MLIR 18 install this book uses, so `tosa` plays its role here: whole tensors, one operation per node.

Two details in the file matter later. `tosa.matmul` works on 3-D tensors, a batch of matrices, so a single product is written as a batch of one: shapes `1x8x16` and `1x16x4`.[^tosa] And the ReLU is a `tosa.clamp` whose lower bound is zero and whose upper bound is the largest `f32`. The bounds are attributes, constants stored on the operation, in the sense [M2](m2-reading-mlir.md#attributes-and-properties-hold-the-constants) described. MLIR 18 spells them `min_fp` and `max_fp`; the current dialect page has renamed them `min_val` and `max_val`,[^tosa] a reminder that upstream MLIR changes between versions.

With no pass given, `mlir-opt` parses and verifies the file and prints it back. Nothing has been decided yet: not the order of any arithmetic, not where any value lives.

A graph with a stable format is also a contract between two programs written by different teams. StableHLO's compatibility page states it precisely. A serialized program written by an older version of the StableHLO library must keep its meaning when read by a newer one, if the two versions are less than five years apart. A program written by a newer version must keep its meaning in an older reader less than two years older, provided it uses no new features.[^shlo-compat] The same page lists what is excluded from those guarantees, and numerical accuracy is on the list.[^shlo-compat] For Vortex, whose [decision 56](../decisions/numbers.md#d56) fixes every rounding, that exclusion is the first sign of a difference in priorities.

## From graph to structured operations

The first lowering turns each `tosa` operation into a `linalg` structured operation, still on tensors and still with no loop order chosen. Two passes do it: `--tosa-to-linalg-named` for operations with a named `linalg` counterpart, then `--tosa-to-linalg` for the rest.[^passes]

--8<-- "includes/examples/mlir/m11-ml-compilers/op_level.mlir.md"

Read the output from the top.

1. `tensor.empty()` makes a tensor with a shape and no defined contents, a placeholder for a destination.
2. `linalg.fill` fills it with `%cst`, the constant 0.0.
3. `linalg.batch_matmul` takes that zero-filled tensor as its `outs` and accumulates the products into it. This is the destination-passing style of [M5](m5-structured-ops.md#values-or-buffers-tensor-against-memref): the zeros are the starting value of every output element.
4. A second `tensor.empty()` supplies the clamp's destination.
5. The clamp became a `linalg.generic` over all three dimensions, all parallel. Its body takes `arith.minimumf` against the largest `f32`, then `arith.maximumf` against zero, and yields the result.

Two things are worth checking against Vortex's kernel. The zero fill plays the part of `let mut sum: f32 = 0.0` in the stage 10 kernel, but for all 32 outputs at once, before any product is computed. And inside `linalg.batch_matmul`'s region, the multiply and the add are separate `arith` operations, as they are in the named op [M2](m2-reading-mlir.md#a-named-operation-hides-a-region) printed in generic form: one rounding each, as decision 56 requires.

The clamp's first indexing map is not the identity. It reads `(d0, d1, d2) -> (0, d1, d2)`: whatever the batch index `d0`, the clamp reads batch element 0 of its input.

??? check "Why does the clamp's input map pin the batch index to 0, and what would `op_level.mlir` print for that map if the batch had two matrices?"

    The lowering of an elementwise `tosa` operation treats a dimension of size 1 as one that may be broadcast, and a broadcast dimension always reads index 0. With a batch of 1, reading index 0 and reading index `d0` are the same, so the map is correct but not the identity. With a batch of 2 the dimension cannot be broadcast, and the map is the identity, `(d0, d1, d2) -> (d0, d1, d2)`; running the same passes on a `2x8x16` version prints exactly that (MLIR 18.1.8, checked on 2026-09-24).

Every system in this chapter has a level in this position, under its own name: one computation per node, tensors still whole, no order chosen for the arithmetic inside a node. None of them is `linalg`, and none of their documentation claims that they share an intermediate form. What they share is the position.

## Loops over memory: where the machine appears

One more step reaches loops. `--one-shot-bufferize` gives every tensor a **buffer**, a `memref` with an address ([M7](m7-bufferization.md)), and `--convert-linalg-to-loops` writes each structured operation as its own nest of `scf.for` loops with explicit `memref.load` and `memref.store`.[^passes]

--8<-- "includes/examples/mlir/m11-ml-compilers/loop_level.mlir.md"

Walk the output by hand. It has three loop nests and two buffers.

- `%alloc`, a `1x8x4` buffer, is the product. The first nest stores 0.0 into each of its 32 elements.
- The second nest is the matrix product: batch, row, column, then `k` innermost, 16 iterations. Each innermost iteration loads one element of `a`, one of `b` and the current partial sum from `%alloc`, multiplies, adds, and stores the sum back.
- `%alloc_1` is a second `1x8x4` buffer, the clamped result. The third nest loads each element of `%alloc`, clamps it, and stores it in `%alloc_1`. Its load uses `%c0` for the batch index: the broadcast map from the previous section, now visible as a constant.

Count the memory operations on `%alloc`. The product nest runs 1 × 8 × 4 × 16 = 512 innermost iterations, each with one load of the partial sum and one store: 512 loads and 512 stores. The fill adds 32 stores and the clamp 32 loads. The stage 10 kernel keeps its running sum in a local, `sum`, and writes each output once. The named op's body reads and writes the output element on every step, and nothing at this level has moved the partial sum into a register-like value yet.

The more important point is the order. Every one of the 32 products is finished before the clamp nest reads the first one. The product is written in full to memory and read back by a separate pass.

??? check "The loop nest for the product reads `%alloc` 512 times, but a straightforward compilation of the Vortex kernel reads its output zero times. Where does that difference come from, and which of the two follows the order of additions that decision 56 requires?"

    From the named op's semantics. `linalg.batch_matmul` accumulates into its `outs` operand, so the lowered body loads the output element, adds one product and stores it back on every `k` step. The Vortex kernel starts a local `sum` at 0.0 and stores it once, after the `k` loop. Both follow decision 56's order: for each output element, the 16 products are added one at a time in increasing `k`, starting from 0.0, each multiply and add rounded separately. They differ in where the partial sum lives, not in the arithmetic.

## Fusion: deciding what shares a loop

**Operator fusion** combines several operators into one kernel so that an intermediate result is used without being saved to memory first; that is how the TVM paper defines it.[^tvm] Here the candidate is plain: clamp each product while it is still at hand, instead of storing all 32 and reading them back. When the operation fused onto the end of a kernel is elementwise, like this clamp, it is often called an **epilogue**, and the whole move **epilogue fusion**; the Triton matrix multiplication tutorial does this in its kernel, applying an activation function to the accumulator before the store.[^triton-tut]

The next file makes that decision with a schedule. It uses the transform dialect from [M9](m9-transform-dialect.md): it matches the clamp, tiles it one output row at a time, and fuses its producers, the product and its zero fill, into the same loop.

--8<-- "includes/examples/mlir/m11-ml-compilers/fused_rows.mlir.md"

`transform.structured.fuse` does in one step what M9 did in two: it tiles the consumer and pulls each producer into the new loop. The tile sizes `[1, 1, 0]` mean one batch element, one row, and all columns (a 0 leaves that dimension untiled). That makes two loops, over the batch and over the rows; `canonicalize` removed the batch loop because it runs once.

What is left is one `scf.for` over the 8 rows. Each iteration takes a `1x1x16` slice of `a`, fills a `1x1x4` slice with zeros, computes one row of the product against the whole of `b`, clamps that row, and inserts it into the result. The schedule is printed at the end, because it is part of the module.

Nothing in the arithmetic moved. For each output element, the zero start, the 16 products and their order of addition are the same as in `loop_level.mlir`, and the clamp still comes after the last addition. What moved is **reuse distance**: how much other work happens between producing a value and using it. Figure 1 draws the difference.

<figure class="vx-figure">
<svg viewBox="0 0 760 270" role="img" aria-label="Two timelines of the same work, separate loop nests against rows fused, with the distance between producing row 0 and clamping it" aria-describedby="m11-f1-desc">
<title id="m11-f1-title">Reuse distance before and after fusing the clamp</title>
<desc id="m11-f1-desc">Two horizontal timelines of sixteen blocks each. Top timeline, separate loop nests: product rows P0 to P7, then clamp rows C0 to C7. The blocks P0 and C0 are highlighted, and a bracket between them notes that the seven other product rows are computed in between. Bottom timeline, fused by rows: P0, C0, P1, C1 and so on to P7, C7. P0 and C0 are highlighted and adjacent: nothing happens in between. Both timelines contain the same sixteen blocks of work.</desc>
<text class="vx-text" x="20" y="30">Separate loop nests (loop_level.mlir)</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 2">
<rect class="vx-box-accent" x="20" y="44" width="40" height="34" rx="3"/><text class="vx-mono" x="40" y="66" text-anchor="middle">P0</text>
<rect class="vx-box" x="64" y="44" width="40" height="34" rx="3"/><text class="vx-mono" x="84" y="66" text-anchor="middle">P1</text>
<rect class="vx-box" x="108" y="44" width="40" height="34" rx="3"/><text class="vx-mono" x="128" y="66" text-anchor="middle">P2</text>
<rect class="vx-box" x="152" y="44" width="40" height="34" rx="3"/><text class="vx-mono" x="172" y="66" text-anchor="middle">P3</text>
<rect class="vx-box" x="196" y="44" width="40" height="34" rx="3"/><text class="vx-mono" x="216" y="66" text-anchor="middle">P4</text>
<rect class="vx-box" x="240" y="44" width="40" height="34" rx="3"/><text class="vx-mono" x="260" y="66" text-anchor="middle">P5</text>
<rect class="vx-box" x="284" y="44" width="40" height="34" rx="3"/><text class="vx-mono" x="304" y="66" text-anchor="middle">P6</text>
<rect class="vx-box" x="328" y="44" width="40" height="34" rx="3"/><text class="vx-mono" x="348" y="66" text-anchor="middle">P7</text>
<rect class="vx-box-accent" x="372" y="44" width="40" height="34" rx="3"/><text class="vx-mono" x="392" y="66" text-anchor="middle">C0</text>
<rect class="vx-box" x="416" y="44" width="40" height="34" rx="3"/><text class="vx-mono" x="436" y="66" text-anchor="middle">C1</text>
<rect class="vx-box" x="460" y="44" width="40" height="34" rx="3"/><text class="vx-mono" x="480" y="66" text-anchor="middle">C2</text>
<rect class="vx-box" x="504" y="44" width="40" height="34" rx="3"/><text class="vx-mono" x="524" y="66" text-anchor="middle">C3</text>
<rect class="vx-box" x="548" y="44" width="40" height="34" rx="3"/><text class="vx-mono" x="568" y="66" text-anchor="middle">C4</text>
<rect class="vx-box" x="592" y="44" width="40" height="34" rx="3"/><text class="vx-mono" x="612" y="66" text-anchor="middle">C5</text>
<rect class="vx-box" x="636" y="44" width="40" height="34" rx="3"/><text class="vx-mono" x="656" y="66" text-anchor="middle">C6</text>
<rect class="vx-box" x="680" y="44" width="40" height="34" rx="3"/><text class="vx-mono" x="700" y="66" text-anchor="middle">C7</text>
<path class="vx-line" d="M60,90 L60,100 L372,100 L372,90"/>
<text class="vx-text-muted" x="216" y="118" text-anchor="middle">row 0 waits while P1 to P7 are computed</text>
</g>
<text class="vx-text" x="20" y="160">Fused by rows (fused_rows.mlir)</text>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 2">
<rect class="vx-box-accent" x="20" y="174" width="40" height="34" rx="3"/><text class="vx-mono" x="40" y="196" text-anchor="middle">P0</text>
<rect class="vx-box-accent" x="64" y="174" width="40" height="34" rx="3"/><text class="vx-mono" x="84" y="196" text-anchor="middle">C0</text>
<rect class="vx-box" x="108" y="174" width="40" height="34" rx="3"/><text class="vx-mono" x="128" y="196" text-anchor="middle">P1</text>
<rect class="vx-box" x="152" y="174" width="40" height="34" rx="3"/><text class="vx-mono" x="172" y="196" text-anchor="middle">C1</text>
<rect class="vx-box" x="196" y="174" width="40" height="34" rx="3"/><text class="vx-mono" x="216" y="196" text-anchor="middle">P2</text>
<rect class="vx-box" x="240" y="174" width="40" height="34" rx="3"/><text class="vx-mono" x="260" y="196" text-anchor="middle">C2</text>
<rect class="vx-box" x="284" y="174" width="40" height="34" rx="3"/><text class="vx-mono" x="304" y="196" text-anchor="middle">P3</text>
<rect class="vx-box" x="328" y="174" width="40" height="34" rx="3"/><text class="vx-mono" x="348" y="196" text-anchor="middle">C3</text>
<rect class="vx-box" x="372" y="174" width="40" height="34" rx="3"/><text class="vx-mono" x="392" y="196" text-anchor="middle">P4</text>
<rect class="vx-box" x="416" y="174" width="40" height="34" rx="3"/><text class="vx-mono" x="436" y="196" text-anchor="middle">C4</text>
<rect class="vx-box" x="460" y="174" width="40" height="34" rx="3"/><text class="vx-mono" x="480" y="196" text-anchor="middle">P5</text>
<rect class="vx-box" x="504" y="174" width="40" height="34" rx="3"/><text class="vx-mono" x="524" y="196" text-anchor="middle">C5</text>
<rect class="vx-box" x="548" y="174" width="40" height="34" rx="3"/><text class="vx-mono" x="568" y="196" text-anchor="middle">P6</text>
<rect class="vx-box" x="592" y="174" width="40" height="34" rx="3"/><text class="vx-mono" x="612" y="196" text-anchor="middle">C6</text>
<rect class="vx-box" x="636" y="174" width="40" height="34" rx="3"/><text class="vx-mono" x="656" y="196" text-anchor="middle">P7</text>
<rect class="vx-box" x="680" y="174" width="40" height="34" rx="3"/><text class="vx-mono" x="700" y="196" text-anchor="middle">C7</text>
<text class="vx-text-muted" x="20" y="232">row 0 is clamped right after it is produced: nothing in between</text>
</g>
<text class="vx-text-muted" x="20" y="258">P = one row of the product (4 outputs, 16 products each); C = the clamp of that row</text>
</svg>
<figcaption>Figure 1. The same sixteen blocks of work in two orders. Separately, the clamp of row 0 waits for seven more product rows; fused by rows, it follows at once. Fusion changes when each value is used, not which floating-point operations run or in what order within an output element.</figcaption>
</figure>

On 32 outputs the difference is invisible. It becomes the whole point as tensors grow. A 1024 × 1024 `f32` product is 4 MiB. Unfused, the clamp reads its first element after the other million or so have been written; fused by rows, it reads a row written a moment ago. Whether a value is still in a cache after that much other traffic depends on the machine's cache sizes, which [P2](../optimize/p2-memory-hierarchy.md) shows how to find. [P8](../optimize/p8-cache-blocking.md) measures that kind of reuse distance on a CPU, and [G12](../gpu/g12-flashattention.md#fusion-as-something-a-compiler-decides) follows a fusion that removes a whole intermediate matrix on a GPU.

The schedule changed the order of the work but not yet the storage. Bufferizing `fused_rows.mlir`'s output with the options `loop_level.mlir` uses still allocated two full `1x8x4` buffers (MLIR 18.1.8, checked on 2026-09-24). Shrinking the intermediate to one row is a further decision, for bufferization and buffer planning, not a free consequence of fusing.

??? check "In `fused_rows.mlir`, each loop iteration computes one row of the product. Is any product `a[row, k] * b[k, column]` computed more than once in the whole fused loop?"

    No. Row `r` of the output needs only row `r` of `a` and all of `b`, and each iteration takes exactly its own row of `a`. The eight iterations partition the output rows without overlap, so each of the 512 products is computed once, as in `loop_level.mlir`. Fusion would repeat work only if two tiles of the consumer needed the same part of the producer, as neighbouring tiles of a stencil do.

## How each system decides what to fuse

Every system in this chapter makes the decision `fused_rows.mlir` made by hand. They differ in who makes it and by what rule.

**TVM.** The TVM paper sorts graph operators into four categories and gives fusion rules between them.[^tvm]

| Category | Meaning | Paper's example |
| --- | --- | --- |
| Injective | Each output element depends on one input element | `add` |
| Reduction | Many input elements combine into one output | `sum` |
| Complex-out-fusable | Elementwise operations can be fused onto its output | `conv2d` |
| Opaque | Cannot be fused | `sort` |

The rules: several injective operators can fuse into one; a reduction can fuse with the injective operators that feed it, such as a scale before a sum; and a complex-out-fusable operator can fuse the elementwise operators applied to its output.[^tvm] A matrix product with a clamp on its output is the third case. The paper reports speedups of up to 1.2 to 2 times from fused operators on its workloads, measured on an NVIDIA Titan X, and attributes them to fewer memory accesses.[^tvm]

Try the rules on a slightly larger graph before reading on. A layer computes `y = relu(matmul(a, b) + bias)`, then `s = sum(y * 2.0)` over each row, then `t = sort(s)`. That is six operators: matmul, add, relu, multiply, sum, sort.

??? check "Using only the TVM paper's four categories and three rules, what is the smallest number of kernels this six-operator graph can become, and which operator must stay alone whatever else happens?"

    Three. The matmul is complex-out-fusable, so add, relu and possibly the multiply (all injective) can fuse onto its output. The sum is a reduction, which may fuse with injective operators that feed it, so the multiply can join either group, but the sum itself cannot join the matmul's kernel: the rules let a complex-out-fusable operator absorb elementwise operators on its output, not a reduction. The sort is opaque and always stays alone. So: {matmul, add, relu, and perhaps the multiply}, {the multiply if not taken, sum}, {sort}. The rules decide what may fuse; they do not say which of the two legal places the multiply goes.

**XLA.** XLA's architecture page describes a pipeline in three parts. It converts StableHLO into its internal HLO dialect and runs target-independent passes, including common subexpression elimination, operation fusion and buffer analysis. Then a back end runs its own passes; the GPU back end may perform fusions that suit the GPU programming model and decides how to split the work into streams. Finally the CPU and GPU back ends use LLVM to generate code.[^xla-arch]

**IREE.** IREE's `flow` dialect models how a program is partitioned into **dispatch regions**, groups of operations that will run as one dispatch. The dialect page presents dispatch regions as the lightweight form in which fusion heuristics are written, and each group is later outlined into an executable.[^iree-flow] A dispatch region is IREE's name for a fused kernel.

**PyTorch 2.** `torch.compile` captures Python code with TorchDynamo and hands it to a back end, `inductor` by default.[^torch-compile] The PyTorch 2 paper describes TorchInductor's scheduler. It turns every buffer into a scheduler node, derives dependency edges from each kernel's memory reads and writes, and fuses greedily: it finds every legal fusion, scores each by category (pointwise, reduction or template), by estimated memory traffic saved and by closeness in the original graph, and applies the best that are still legal, repeating until none remain.[^pt2] For GPUs it writes the fused kernels in Triton, and for CPUs in C++ with OpenMP.[^pt2]

**Triton.** A Triton program is one kernel already. The programmer decides what goes into it: the tutorial's matrix product applies its activation inside the kernel, while the accumulator is still in 32-bit floating point.[^triton-tut] Fusion is done by hand, in the source.

**Mojo and MAX.** Modular's vision page says MAX has a graph compiler that performs graph-level optimizations such as kernel fusion, and that Mojo code can extend MAX with custom graph operations.[^mojo-vision]

## Five systems, one staircase

The systems use different vocabularies for the same few levels. Figure 2 lines them up, with this chapter's own four files as the last column.

<figure class="vx-figure">
<svg viewBox="0 0 960 400" role="img" aria-label="Seven columns, one per system, each showing its graph level, fusion level, kernel code and targets" aria-describedby="m11-f2-desc">
<title id="m11-f2-title">The same staircase under seven sets of names</title>
<desc id="m11-f2-desc">A grid of four rows and seven columns. Rows, top to bottom: graph, fusion, kernel code, targets. Columns: XLA, IREE, TVM as described in its 2018 paper, PyTorch 2, Triton, Mojo and MAX, and this chapter. XLA: StableHLO from JAX, PyTorch and TensorFlow; HLO with target-independent then GPU-specific fusion; LLVM IR; CPUs and NVIDIA GPUs through NVPTX. IREE: StableHLO, tosa and other inputs from JAX, ONNX, PyTorch and TensorFlow; flow dispatch regions; stream and hal dialects, then code generation per target; CPU, Vulkan, CUDA, ROCm and Metal. TVM: a computational graph; four operator categories with fusion rules; tensor expressions with a searched schedule; LLVM IR, CUDA, Metal, OpenCL and accelerators. PyTorch 2: an FX graph from TorchDynamo; the Inductor scheduler's greedy fusion; a loop-level IR, then Triton or C++; GPUs through Triton and CPUs through C++. Triton: no graph level, drawn dashed, because a Triton program is one kernel; fusion by hand in the kernel source; the tt and ttg dialects, then ttng or amdg, then LLVM; NVIDIA and AMD GPUs on Linux. Mojo and MAX: a MAX graph; kernel fusion by the MAX graph compiler; Mojo kernels compiled by KGEN, built on MLIR; NVIDIA, AMD and Apple silicon GPUs. This chapter, highlighted: tosa in graph_level.mlir; linalg and a fusion schedule in op_level.mlir and fused_rows.mlir; scf and memref in loop_level.mlir; targets left to M10.</desc>
<text class="vx-text" x="10" y="92">Graph</text>
<text class="vx-text" x="10" y="172">Fusion</text>
<text class="vx-text" x="10" y="244">Kernel</text>
<text class="vx-text" x="10" y="262">code</text>
<text class="vx-text" x="10" y="332">Targets</text>
<text class="vx-text" x="159" y="36" text-anchor="middle">XLA</text>
<text class="vx-text" x="281" y="36" text-anchor="middle">IREE</text>
<text class="vx-text" x="403" y="36" text-anchor="middle">TVM (2018)</text>
<text class="vx-text" x="525" y="36" text-anchor="middle">PyTorch 2</text>
<text class="vx-text" x="647" y="36" text-anchor="middle">Triton</text>
<text class="vx-text" x="769" y="36" text-anchor="middle">Mojo / MAX</text>
<text class="vx-text-accent" x="891" y="36" text-anchor="middle">This chapter</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 4">
<rect class="vx-box" x="102" y="54" width="114" height="68" rx="4"/><text class="vx-text-muted" x="108" y="76">StableHLO, from</text><text class="vx-text-muted" x="108" y="92">JAX, PyTorch,</text><text class="vx-text-muted" x="108" y="108">TensorFlow</text>
<rect class="vx-box" x="224" y="54" width="114" height="68" rx="4"/><text class="vx-text-muted" x="230" y="76">StableHLO, tosa;</text><text class="vx-text-muted" x="230" y="92">from JAX, ONNX,</text><text class="vx-text-muted" x="230" y="108">PyTorch, TF</text>
<rect class="vx-box" x="346" y="54" width="114" height="68" rx="4"/><text class="vx-text-muted" x="352" y="76">computational</text><text class="vx-text-muted" x="352" y="92">graph</text>
<rect class="vx-box" x="468" y="54" width="114" height="68" rx="4"/><text class="vx-text-muted" x="474" y="76">FX graph, from</text><text class="vx-text-muted" x="474" y="92">TorchDynamo</text>
<rect class="vx-box-bad" x="590" y="54" width="114" height="68" rx="4"/><text class="vx-text-muted" x="596" y="76">none: a program</text><text class="vx-text-muted" x="596" y="92">is one kernel</text>
<rect class="vx-box" x="712" y="54" width="114" height="68" rx="4"/><text class="vx-text-muted" x="718" y="76">MAX graph</text>
<rect class="vx-box-accent" x="834" y="54" width="114" height="68" rx="4"/><text class="vx-mono" x="840" y="76">tosa</text><text class="vx-text-muted" x="840" y="94">graph_level</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4">
<rect class="vx-box" x="102" y="134" width="114" height="68" rx="4"/><text class="vx-text-muted" x="108" y="156">HLO: fusion,</text><text class="vx-text-muted" x="108" y="172">generic, then</text><text class="vx-text-muted" x="108" y="188">GPU-specific</text>
<rect class="vx-box" x="224" y="134" width="114" height="68" rx="4"/><text class="vx-text-muted" x="230" y="156">flow dialect:</text><text class="vx-text-muted" x="230" y="172">dispatch regions</text>
<rect class="vx-box" x="346" y="134" width="114" height="68" rx="4"/><text class="vx-text-muted" x="352" y="156">4 operator kinds,</text><text class="vx-text-muted" x="352" y="172">fusion rules</text>
<rect class="vx-box" x="468" y="134" width="114" height="68" rx="4"/><text class="vx-text-muted" x="474" y="156">Inductor's</text><text class="vx-text-muted" x="474" y="172">greedy, scored</text><text class="vx-text-muted" x="474" y="188">fusion</text>
<rect class="vx-box" x="590" y="134" width="114" height="68" rx="4"/><text class="vx-text-muted" x="596" y="156">by hand, in the</text><text class="vx-text-muted" x="596" y="172">kernel source</text>
<rect class="vx-box" x="712" y="134" width="114" height="68" rx="4"/><text class="vx-text-muted" x="718" y="156">MAX graph</text><text class="vx-text-muted" x="718" y="172">compiler:</text><text class="vx-text-muted" x="718" y="188">kernel fusion</text>
<rect class="vx-box-accent" x="834" y="134" width="114" height="68" rx="4"/><text class="vx-mono" x="840" y="156">linalg</text><text class="vx-text-muted" x="840" y="174">op_level,</text><text class="vx-text-muted" x="840" y="190">fused_rows</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4">
<rect class="vx-box" x="102" y="214" width="114" height="68" rx="4"/><text class="vx-text-muted" x="108" y="236">LLVM IR</text>
<rect class="vx-box" x="224" y="214" width="114" height="68" rx="4"/><text class="vx-text-muted" x="230" y="236">stream, hal;</text><text class="vx-text-muted" x="230" y="252">codegen per</text><text class="vx-text-muted" x="230" y="268">target</text>
<rect class="vx-box" x="346" y="214" width="114" height="68" rx="4"/><text class="vx-text-muted" x="352" y="236">tensor expression</text><text class="vx-text-muted" x="352" y="252">+ searched</text><text class="vx-text-muted" x="352" y="268">schedule</text>
<rect class="vx-box" x="468" y="214" width="114" height="68" rx="4"/><text class="vx-text-muted" x="474" y="236">loop-level IR,</text><text class="vx-text-muted" x="474" y="252">then Triton</text><text class="vx-text-muted" x="474" y="268">or C++</text>
<rect class="vx-box" x="590" y="214" width="114" height="68" rx="4"/><text class="vx-mono" x="596" y="236">tt, ttg,</text><text class="vx-text-muted" x="596" y="254">ttng or amdg,</text><text class="vx-text-muted" x="596" y="270">then LLVM</text>
<rect class="vx-box" x="712" y="214" width="114" height="68" rx="4"/><text class="vx-text-muted" x="718" y="236">Mojo kernels,</text><text class="vx-text-muted" x="718" y="252">KGEN, built</text><text class="vx-text-muted" x="718" y="268">on MLIR</text>
<rect class="vx-box-accent" x="834" y="214" width="114" height="68" rx="4"/><text class="vx-mono" x="840" y="236">scf, memref</text><text class="vx-text-muted" x="840" y="254">loop_level</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 4">
<rect class="vx-box" x="102" y="294" width="114" height="68" rx="4"/><text class="vx-text-muted" x="108" y="316">CPUs; NVIDIA</text><text class="vx-text-muted" x="108" y="332">GPUs through</text><text class="vx-text-muted" x="108" y="348">NVPTX</text>
<rect class="vx-box" x="224" y="294" width="114" height="68" rx="4"/><text class="vx-text-muted" x="230" y="316">CPU, Vulkan,</text><text class="vx-text-muted" x="230" y="332">CUDA, ROCm,</text><text class="vx-text-muted" x="230" y="348">Metal</text>
<rect class="vx-box" x="346" y="294" width="114" height="68" rx="4"/><text class="vx-text-muted" x="352" y="316">LLVM IR, CUDA,</text><text class="vx-text-muted" x="352" y="332">Metal, OpenCL,</text><text class="vx-text-muted" x="352" y="348">accelerators</text>
<rect class="vx-box" x="468" y="294" width="114" height="68" rx="4"/><text class="vx-text-muted" x="474" y="316">GPUs through</text><text class="vx-text-muted" x="474" y="332">Triton; CPUs</text><text class="vx-text-muted" x="474" y="348">through C++</text>
<rect class="vx-box" x="590" y="294" width="114" height="68" rx="4"/><text class="vx-text-muted" x="596" y="316">NVIDIA, AMD;</text><text class="vx-text-muted" x="596" y="332">Linux</text>
<rect class="vx-box" x="712" y="294" width="114" height="68" rx="4"/><text class="vx-text-muted" x="718" y="316">NVIDIA, AMD,</text><text class="vx-text-muted" x="718" y="332">Apple silicon</text><text class="vx-text-muted" x="718" y="348">GPUs</text>
<rect class="vx-box-accent" x="834" y="294" width="114" height="68" rx="4"/><text class="vx-text-muted" x="840" y="316">left to M10:</text><text class="vx-text-muted" x="840" y="332">gpu, nvvm,</text><text class="vx-text-muted" x="840" y="348">spirv</text>
</g>
<text class="vx-text-muted" x="10" y="390">Each cell names the position a system's own documentation describes, not a form shared between systems.</text>
</svg>
<figcaption>Figure 2. Seven systems' own names for four levels, from each one's documentation or paper. Triton has no graph level (dashed): its programs start at the kernel. The highlighted column is this chapter's four files, which show the same staircase with MLIR's upstream dialects.</figcaption>
</figure>

The table below adds what each system is built on, and the one question that matters most on the owner's Mac: whether its documentation gives a route to an Apple GPU.

| System | Reads | Built on | Who decides fusion | Apple GPU, per its documentation |
| --- | --- | --- | --- | --- |
| XLA | StableHLO, from PyTorch, TensorFlow and JAX[^xla][^xla-arch] | MLIR, with HLO inside and LLVM for code generation[^xla][^xla-arch] | Compiler passes, target-independent then per back end[^xla-arch] | Not on the architecture page, which names NVIDIA GPUs through LLVM's NVPTX back end[^xla-arch] |
| IREE | MLIR input from JAX, ONNX, PyTorch and TensorFlow[^iree] | MLIR: a compiler and a runtime[^iree] | The compiler, as dispatch regions[^iree-flow] | Yes: the Metal HAL driver[^iree-metal] |
| TVM (2018 paper) | Models from many frameworks and exchange formats[^tvm] | Its own graph and tensor-expression IRs[^tvm] | Graph rules over four operator categories[^tvm] | Metal is among the paper's code-generation outputs[^tvm] |
| PyTorch 2 | Python code, captured as FX graphs[^pt2] | TorchDynamo and TorchInductor; Triton for GPU code[^pt2] | Inductor's scored, greedy scheduler[^pt2] | Not covered by the sources this chapter used |
| Triton | A tile-level kernel in a Python-embedded language[^triton-tut] | Its own MLIR dialects, on an LLVM commit the project pins[^triton-dialects][^triton-repo] | The kernel's author[^triton-tut] | No: the README lists Linux, NVIDIA and AMD[^triton-repo] |
| Mojo / MAX | Mojo programs and MAX graphs[^mojo-vision] | KGEN, a compiler built on MLIR[^mojo-vision] | MAX's graph compiler[^mojo-vision] | Yes: Apple silicon M1 to M5 listed as known compatible[^mojo-req] |

Two entries need a caution. The TVM column describes the 2018 paper, which predates MLIR's public introduction in 2020;[^mlir-paper] this chapter makes no claim about what TVM's current IRs are. And Mojo's requirements page says Apple silicon GPU programming may need the Metal toolchain installed,[^mojo-req] an Xcode component that was missing on the owner's Mac when this book's research checked it (2026-09-23).

## Schedules: rules, search or the programmer

Fusion decides which operations share a kernel. A **schedule** then decides how that kernel runs: loop order, tile sizes, which loops map to GPU threads. [P14](../optimize/p14-algorithms-and-schedules.md#tensor-programs-tvm) develops the split between an algorithm and its schedule in full; here the question is only who writes the schedule in each system.

TVM searches for it. The paper's explorer walks the space of schedule parameters by parallel simulated annealing, guided by a gradient tree boosting model (XGBoost) that predicts, from features of the lowered loop program, which candidates will run faster; it is trained on measured running times with a ranking objective.[^tvm] [P15](../optimize/p15-choosing-parameters.md#search-when-there-is-no-formula) compares that kind of search with analytic models.

Triton splits the work. The programmer writes a kernel over tiles and lists candidate configurations: block sizes, `num_warps` and `num_stages`.[^triton-tut] The compiler handles what the paper calls machine-dependent passes, including hierarchical tiling, memory coalescing, shared memory allocation and shared memory synchronization.[^triton-paper] [G13](../gpu/g13-tile-languages.md) follows the tutorial's choice of launch order. PyTorch's `max-autotune` mode uses Triton or template-based matrix products,[^torch-compile] and Inductor generates matrix products from templates that mix hand-written and generated Triton.[^pt2]

`fused_rows.mlir` sits at the third point: the schedule is a program a person wrote, stored beside the payload and printed with it. [M9](m9-transform-dialect.md#heritage-halides-schedules-tvms-search-and-what-ir-adds) traces that design back to Halide and TVM.

## IREE's route to an Apple GPU

IREE is the one system here whose documentation describes a route to Apple GPUs from MLIR input, so it is worth following stage by stage. Figure 3 draws it.

The compiler's structure comes from IREE's developer overview. Input conversions bring framework formats into MLIR. The `flow` dialect partitions the program into dispatches, `stream` models asynchronous scheduling and ordering, `hal` is the hardware abstraction layer that the runtime implements for each target, and `vm` is a bytecode virtual machine used to run the compiled modules; a code generation stage produces the kernels themselves.[^iree-dev]

For Metal, the Metal HAL driver design document takes over. IREE reuses its SPIR-V code generation, then cross-compiles the SPIR-V into Metal Shading Language source with SPIRV-Cross, as MoltenVK does for graphics.[^iree-metal] The document gives two ways to package the result: embed the MSL source and compile it when the program runs, or embed a compiled library.[^iree-metal] It targets Metal 3, which it describes as supported from macOS Ventura and iOS 16, on Apple GPUs including A13 and M1 or later chips.[^iree-metal]

<figure class="vx-figure">
<svg viewBox="0 0 760 290" role="img" aria-label="IREE's documented route from MLIR input to a running Metal kernel, in seven stages" aria-describedby="m11-f3-desc">
<title id="m11-f3-title">IREE's route from MLIR input to Metal</title>
<desc id="m11-f3-desc">Seven boxes connected by arrows, four on the top row and three on the bottom. Top row: MLIR input such as StableHLO or tosa, phase input; flow dispatch regions, phase flow; stream scheduling, phase stream; hal executables, phase hal. The arrow continues to the bottom row: SPIR-V code generation; SPIRV-Cross, producing MSL source; Metal, which either compiles the embedded source at run time or loads an embedded library. The first four boxes carry the name of the iree-compile phase that stops there.</desc>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 7"><rect class="vx-box" x="20" y="30" width="150" height="64" rx="5"/><text class="vx-text" x="95" y="56" text-anchor="middle">MLIR input</text><text class="vx-text-muted" x="95" y="76" text-anchor="middle">StableHLO, tosa, ...</text></g>
<path class="vx-line" d="M170,62 L200,62"/><polygon class="vx-arrowhead" points="200,56 210,62 200,68"/>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 7"><rect class="vx-box" x="210" y="30" width="150" height="64" rx="5"/><text class="vx-text" x="285" y="56" text-anchor="middle">flow</text><text class="vx-text-muted" x="285" y="76" text-anchor="middle">dispatch regions</text></g>
<path class="vx-line" d="M360,62 L390,62"/><polygon class="vx-arrowhead" points="390,56 400,62 390,68"/>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 7"><rect class="vx-box" x="400" y="30" width="150" height="64" rx="5"/><text class="vx-text" x="475" y="56" text-anchor="middle">stream</text><text class="vx-text-muted" x="475" y="76" text-anchor="middle">async scheduling</text></g>
<path class="vx-line" d="M550,62 L580,62"/><polygon class="vx-arrowhead" points="580,56 590,62 580,68"/>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 7"><rect class="vx-box" x="590" y="30" width="150" height="64" rx="5"/><text class="vx-text" x="665" y="56" text-anchor="middle">hal</text><text class="vx-text-muted" x="665" y="76" text-anchor="middle">executables</text></g>
<text class="vx-text-muted vx-mono" x="95" y="114" text-anchor="middle">--compile-to=input</text>
<text class="vx-text-muted vx-mono" x="285" y="114" text-anchor="middle">=flow</text>
<text class="vx-text-muted vx-mono" x="475" y="114" text-anchor="middle">=stream</text>
<text class="vx-text-muted vx-mono" x="665" y="114" text-anchor="middle">=hal</text>
<path class="vx-flow" d="M665,124 L665,150 L135,150 L135,172"/><polygon class="vx-arrowhead" points="129,172 135,182 141,172"/>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 7"><rect class="vx-box" x="40" y="182" width="190" height="64" rx="5"/><text class="vx-text" x="135" y="208" text-anchor="middle">SPIR-V codegen</text><text class="vx-text-muted" x="135" y="228" text-anchor="middle">one kernel per dispatch</text></g>
<path class="vx-line" d="M230,214 L270,214"/><polygon class="vx-arrowhead" points="270,208 280,214 270,220"/>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 7"><rect class="vx-box" x="280" y="182" width="190" height="64" rx="5"/><text class="vx-text" x="375" y="208" text-anchor="middle">SPIRV-Cross</text><text class="vx-text-muted" x="375" y="228" text-anchor="middle">SPIR-V to MSL source</text></g>
<path class="vx-line" d="M470,214 L510,214"/><polygon class="vx-arrowhead" points="510,208 520,214 510,220"/>
<g class="vx-seq" style="--vx-i: 6; --vx-n: 7"><rect class="vx-box-strong" x="520" y="182" width="220" height="64" rx="5"/><text class="vx-text" x="630" y="208" text-anchor="middle">Metal</text><text class="vx-text-muted" x="630" y="228" text-anchor="middle">source compiled at run time,</text><text class="vx-text-muted" x="630" y="242" text-anchor="middle">or an embedded library</text></g>
<text class="vx-text-muted" x="20" y="278">Top row: IREE's own dialects, each also an iree-compile phase. Bottom row: the Metal HAL driver's path.</text>
</svg>
<figcaption>Figure 3. IREE's route to an Apple GPU, from its developer overview and its Metal HAL driver design document. The four top-row stages are phases at which <code>iree-compile --compile-to</code> can stop and print the program; the bottom row is the Metal-specific part.</figcaption>
</figure>

The route can be inspected, stage by stage, without a GPU. IREE's developer tips document `iree-compile --compile-to=<phase>`, which stops at a named phase and prints the program there; the phase names include `input`, `dispatch-creation`, `flow`, `stream`, `executable-sources`, `executable-targets`, `hal` and `vm`. The same page documents `--dump-compilation-phases-to`, which writes the IR after every phase, and `--iree-hal-dump-executable-sources-to`, which writes each executable's MLIR before HAL compilation.[^iree-tips] IREE is not installed on the owner's machine (checked on 2026-09-24), so this chapter has not run these commands.

Two facts about this route matter for Vortex. The Metal code IREE runs is MSL source that IREE generated, the same kind of text a Vortex compiler could emit directly ([M12](m12-vortex-gpu-path.md) weighs that option). And every fact a Vortex kernel carries has to survive six stages, owned by three projects, to reach the GPU.

## Where a kernel language plugs in: custom operators

A framework can also call a kernel that its compiler did not write, registered as a **custom operator**: a function the framework calls like one of its built-in operations. PyTorch's tutorial on custom C++ and CUDA operators lists what that takes.[^pt-custom-ops]

1. A schema, declared with `TORCH_LIBRARY`: the operator's name, argument types and result types.
2. An implementation for each device, registered with `TORCH_LIBRARY_IMPL`.
3. A **fake** implementation, registered with `torch.library.register_fake`, which computes only the metadata of the result (shape, data type, device) and never touches data. `torch.compile` traces programs with such metadata-only tensors, so an operator without one cannot be traced.
4. A test with `torch.library.opcheck`, which checks that the registration is correct.
5. Optionally, a backward formula with `torch.library.register_autograd`.

The compiler then treats the operator as a closed box. The PyTorch 2 paper's scheduler has one kind of node for kernels Inductor generates and another, `ExternKernelSchedulerNode`, for calls to library code or user-defined kernels.[^pt2] Its fused nodes are made of the first kind. A custom operator is a call Inductor emits but a body it never writes, so there is no body into which an epilogue could be fused. In TVM's terms, a custom operator is opaque.

??? check "A Vortex matrix product is registered as a PyTorch custom operator, and a model compiled with `torch.compile` applies a ReLU to its output. Where does the ReLU run, and what would it take to get Figure 1's fused order?"

    In a separate kernel. The custom operator becomes an external kernel call whose body Inductor does not generate, so Inductor can fuse the ReLU with other operators it generates but not into the Vortex kernel; the ReLU reads the whole product back from memory, the unfused order of Figure 1. To get the fused order, the kernel author has to fuse by hand: register a second operator whose Vortex kernel applies the clamp after its `k` loop, as a Triton programmer puts the activation into the kernel's own source.

## Where Vortex sits

Put the stage 10 kernel beside these systems:

```vortex
// items: valid
fn multiply(a: &[f32; 8, 16], b: &[f32; 16, 4], c: &mut [f32; 8, 4]) {
    for row in 0..8 {
        for column in 0..4 {
            let mut sum: f32 = 0.0;
            for k in 0..16 {
                sum += a[row, k] * b[k, column];
            }
            c[row, column] = sum;
        }
    }
}
```

It is one kernel at one fixed shape ([stage 10](../compiler/guide/stage-10-matrix-multiplication.md#one-function-per-shape)), written by hand. That makes it closer to one node inside any of these systems, after its graph compiler has decided what belongs together, than to one of the systems. Vortex has no graph level: nothing in a v0.1 compiler decides that two functions should become one kernel. In Figure 2 it would sit in the Triton column, below the dashed box, with the programmer doing the fusion.

Its floating-point rule is where it parts company with the rest. [Decision 56](../decisions/numbers.md#d56) forbids contracting a multiply and an add into one fused multiply-add and forbids reassociating a sum. MLIR's `arith` operations carry a `fastmath` flag whose values include `contract` and `reassoc`, permissions that a lowering can attach and the verifier does not reject.[^arith] StableHLO's compatibility promise leaves numerical accuracy out.[^shlo-compat] And a schedule search such as TVM's rewards whatever runs fastest.[^tvm] Some fusions are harmless under decision 56: `fused_rows.mlir` reorders no floating-point operation within an output element. Others are not: splitting a reduction into partial sums, a common way to expose parallelism, changes the order of the additions.

So Vortex could borrow the machinery of these systems, their staircases, fusion and schedule languages, but not their defaults. Every transformation it took would need the same test: same operations, same order, same bits. That is the test the exercise below builds.

The comparison also marks a gap. Each system in the table has a documented route to at least one GPU family. A v0.1 Vortex compiler has no GPU back end. [M12](m12-vortex-gpu-path.md) weighs the routes for building one, including emitting MSL directly, as IREE's Metal route ends up doing.

## For Vortex

!!! vortex "Exercise"

    **Build** a differential test between your Vortex compiler and an ML compiler's pipeline: the same computation, compiled both ways, must produce the same bits.

    1. A Vortex program (a program, not a change to your compiler): the stage 10 product at 8 × 16 by 16 × 4, with every negative result replaced by 0.0 after its `k` loop, on fixed inputs you choose, printing all 32 results with enough significant digits to tell any two `f32` values apart.
    2. The same computation in MLIR: this chapter's `tosa` graph with your inputs as constants, lowered with `loop_level.mlir`'s pipeline, then on to the `llvm` dialect ([M4](m4-dialect-conversion.md)), and run with `mlir-cpu-runner`, which MLIR 18 installs, printing the same 32 values the same way.
    3. A script that runs both and compares the outputs byte for byte.
    4. A second MLIR variant that applies `fused_rows.mlir`'s schedule before lowering.

    **Not yet:** calling Vortex kernels from PyTorch or any framework, a graph level or a fusion pass in your own compiler, IREE, and any GPU ([M10](m10-mlir-for-gpus.md), [M12](m12-vortex-gpu-path.md)).

    **Proof that it works:**

    - The Vortex output and both MLIR outputs, unfused and fused, are identical.
    - A reordering canary: a third MLIR variant whose schedule splits the `k` reduction into two partial sums (`transform.structured.split_reduction`) makes the comparison fail on your inputs. If it passes, your inputs are too tame: small integers, for example, make every sum exact. Choose inputs where rounding happens, and write down why they work.
    - A wiring canary: change one input in one program only, and confirm that the comparison fails.
    - One paragraph explaining why the fused variant matched and the split one did not, in terms of decision 56.

## Key ideas

!!! recap "Questions you can now answer"

    - **What staircase do these compilers share?** A graph of whole-tensor operations, then one structured computation per node with no loop order chosen, then loops over memory, then target code, under different names in each system.
    - **What does operator fusion change?** When an intermediate result is used, and so whether it must go to memory and come back; it need not change which floating-point operations run or their order within an output element.
    - **Who decides fusion in XLA, IREE, TVM, PyTorch 2, Triton and MAX?** Compiler passes in XLA, dispatch-region formation in IREE, rules over four operator categories in TVM, a scored greedy scheduler in Inductor, the kernel author in Triton, and MAX's graph compiler.
    - **What does StableHLO promise, and what does it leave out?** Five years of backward and two of forward compatibility for serialized programs, and no guarantee of numerical accuracy.
    - **How does IREE reach an Apple GPU?** Through its Metal HAL driver: SPIR-V code generation, SPIRV-Cross to MSL source, then Metal compiles that source at run time or loads an embedded library.
    - **What does a framework's compiler lose when a kernel arrives as a custom operator?** The kernel's body: it can call the kernel but cannot fuse anything into it.
    - **Where does Vortex sit?** Beside Triton, as one hand-written kernel with no graph level, and stricter than all of them about floating-point order.

## Where this comes back

!!! next "You will use this again in"

    - [M12. Designing Vortex's GPU path](m12-vortex-gpu-path.md): *IREE's Metal route*, *MSL source compiled at run time*, *the graph-level gap*
    - [G12. Fusion case study: FlashAttention](../gpu/g12-flashattention.md): *operator fusion*, *reuse distance*
    - [G13. Tile languages](../gpu/g13-tile-languages.md): *who chooses the schedule*, *epilogue in the kernel source*
    - [P15. Choosing parameters: models or search](../optimize/p15-choosing-parameters.md): *learned cost model*, *search over schedules*
    - [O12. Testing an optimizer](../optimize/o12-testing-optimizers.md): *differential test*, *canary*

## Sources and further reading

To see the staircase for yourself, run this chapter's four files with the `tosa`, `linalg` and transform dialect pages open.[^tosa][^linalg][^transform] For the systems, each project's own documentation is the primary source; the TVM, Triton and PyTorch 2 papers explain why each design took its shape, not only what it is.[^tvm][^triton-paper][^pt2]

[^tosa]: MLIR Project, "'tosa' Dialect", introduction and entries `tosa.matmul` and `tosa.clamp`. <https://mlir.llvm.org/docs/Dialects/TOSA/>
[^stablehlo]: OpenXLA Project, "StableHLO". <https://openxla.org/stablehlo>
[^shlo-compat]: OpenXLA Project, "StableHLO Compatibility", sections on backward and forward compatibility and on what is out of scope. <https://openxla.org/stablehlo/compatibility>
[^xla]: OpenXLA Project, "XLA". <https://openxla.org/xla>
[^xla-arch]: OpenXLA Project, "XLA architecture", section "How it works". <https://openxla.org/xla/architecture>
[^iree]: IREE Project, "IREE" (home page: input frameworks, hardware targets, ahead-of-time compilation). <https://iree.dev/>
[^iree-dev]: IREE Project, "Developer overview". <https://iree.dev/developers/general/developer-overview/>
[^iree-flow]: IREE Project, "'flow' Dialect", entries `flow.dispatch.region` and `flow.dispatch.workgroups`. <https://iree.dev/reference/mlir-dialects/Flow/>
[^iree-tips]: IREE Project, "Developer tips and tricks", sections on `--compile-to` and on dumping executables. <https://iree.dev/developers/general/developer-tips/>
[^iree-metal]: IREE Project, "Metal HAL driver" design document, sections "Overall Design Choices", "Metal Versions" and "Shader/kernel compilation". <https://iree.dev/developers/design-docs/metal-hal-driver/>
[^tvm]: Tianqi Chen, Thierry Moreau, Ziheng Jiang, Lianmin Zheng, Eddie Yan, Haichen Shen, Meghan Cowan, Leyuan Wang, Yuwei Hu, Luis Ceze, Carlos Guestrin and Arvind Krishnamurthy, "TVM: An Automated End-to-End Optimizing Compiler for Deep Learning", *OSDI 2018*, pp. 578-594, sections 2, 3 and 5 and Figures 2 and 4. <https://www.usenix.org/conference/osdi18/presentation/chen> and <https://arxiv.org/abs/1802.04799>
[^pt2]: Jason Ansel, Edward Yang, Horace He, Natalia Gimelshein and others, "PyTorch 2: Faster Machine Learning Through Dynamic Python Bytecode Transformation and Graph Compilation", *ASPLOS 2024*, doi:10.1145/3620665.3640366, abstract and section 4.4 "Scheduling". <https://docs.pytorch.org/assets/pytorch2-2.pdf>
[^torch-compile]: PyTorch, "torch.compile", PyTorch 2.14 documentation. <https://docs.pytorch.org/docs/2.14/generated/torch.compile.html>
[^pt-custom-ops]: PyTorch, "Custom C++ and CUDA Operators", PyTorch tutorials. <https://docs.pytorch.org/tutorials/advanced/cpp_custom_ops.html>
[^triton-paper]: Philippe Tillet, H. T. Kung and David Cox, "Triton: An Intermediate Language and Compiler for Tiled Neural Network Computations", *MAPL 2019*, pp. 10-19, doi:10.1145/3315508.3329973, section 5.2. <https://www.eecs.harvard.edu/~htk/publication/2019-mapl-tillet-kung-cox.pdf>
[^triton-tut]: Triton Project, "Matrix Multiplication", Triton tutorials. <https://triton-lang.org/main/getting-started/tutorials/03-matrix-multiplication.html>
[^triton-dialects]: Triton Project, "MLIR Dialects". <https://triton-lang.org/main/dialects/dialects.html>
[^triton-repo]: Triton Project, repository README. <https://github.com/triton-lang/triton>
[^mojo-vision]: Modular, "Mojo vision". <https://mojolang.org/docs/vision/>
[^mojo-req]: Modular, "System requirements", Mojo documentation, section on GPU compatibility. <https://mojolang.org/docs/requirements>
[^passes]: MLIR Project, "Passes", entries `-tosa-to-linalg-named`, `-tosa-to-linalg`, `-one-shot-bufferize` and `-convert-linalg-to-loops`. <https://mlir.llvm.org/docs/Passes/>
[^linalg]: MLIR Project, "'linalg' Dialect". <https://mlir.llvm.org/docs/Dialects/Linalg/>
[^transform]: MLIR Project, "Transform Dialect", entry `transform.structured.fuse`. <https://mlir.llvm.org/docs/Dialects/Transform/>
[^arith]: MLIR Project, "'arith' Dialect", entries `FastMathFlagsAttr` and `FastMathFlags`. <https://mlir.llvm.org/docs/Dialects/ArithOps/>
[^mlir-paper]: Chris Lattner, Mehdi Amini, Uday Bondhugula, Albert Cohen, Andy Davis, Jacques Pienaar, River Riddle, Tatiana Shpeisman, Nicolas Vasilache and Oleksandr Zinenko, "MLIR: A Compiler Infrastructure for the End of Moore's Law", arXiv:2002.11054, 2020. <https://arxiv.org/abs/2002.11054>
