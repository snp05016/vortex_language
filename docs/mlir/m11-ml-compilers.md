# M11. End-to-end ML compilers

<p class="page-intro">XLA and StableHLO, TVM, IREE, Triton and Mojo each turn a machine-learning framework's whole-tensor graph into hardware instructions, through a stack of intermediate forms of their own. This chapter reads that stack as one shape, shared by all five under different names, and places Vortex among them: a language for one kernel at a time, not a framework for a whole model.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 30 minutes · Builds on: [M1. Why MLIR](m1-why-mlir.md), [M5. Structured ops: linalg, tensor and memref](m5-structured-ops.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is progressive lowering, and does every step keep every fact the level above stated?"

        Moving from a high-level dialect to a low-level one through a sequence of small, separately testable passes, rather than one large translation. No: a lowering step can drop a fact a higher level stated, the way one path out of `linalg` keeps a loop's "safe to reorder" marker and another throws it away.

        Introduced in [M1. Why MLIR](m1-why-mlir.md#what-survives-a-step-and-what-does-not).

    ??? question "What is a named operation, and what does `outs` mean on `linalg.matmul`?"

        A named operation is a common computation with a name of its own, whose region follows the same interface as `linalg.generic`, the dialect's general form. `outs` lists the operand it writes into rather than a value it returns: the storage the caller provided, the shape of a Vortex `&mut` argument.

        Introduced in [M2. Reading MLIR](m2-reading-mlir.md#a-named-operation-hides-a-region).

    ??? question "In what order are the elements of a `[f32; 2, 3]` array stored?"

        Row after row, the last index varying fastest: `[0, 0]`, `[0, 1]`, `[0, 2]`, `[1, 0]`, `[1, 1]`, `[1, 2]`.

        Introduced in [Arrays and shapes, decision 43](../decisions/arrays.md#d43).

    ??? question "May a Vortex compiler turn `sum += a[row, k] * b[k, column]` into one fused multiply-add?"

        No. Each `f32` operation is one IEEE 754 operation, rounded to nearest with ties to even, and a compiler must not contract, reassociate or reorder them. The product is rounded, then the sum.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Explain the shape every one of these systems shares: whole-tensor graph ops, then structured per-operation ops, then explicit loops over memory, whatever each one calls its own levels.
    - Compare XLA and StableHLO, TVM, IREE, Triton and Mojo/MAX by what each consumes, what it is built on, and how it picks a schedule, not by name alone.
    - Trace one matmul-and-activation graph through this chapter's own checked examples, from a whole-tensor operation to a nested loop, and say which named system's own step each transition resembles.
    - Place Vortex among these five: what it already resembles, what layer of theirs it has no equivalent of yet, and what its stricter floating-point rule would cost if it adopted one of their fusion steps.

## One graph, one shape, five names

A matrix multiply followed by clamping the result between zero and a large ceiling is a small, ordinary piece of a neural network: the shape of one linear layer's output going through a ReLU-like activation, with no bias term to keep the example small. Hand that computation to XLA, TVM, IREE, Triton or Mojo/MAX and each one starts from a graph of whole-tensor operations: one node for the multiply, one for the clamp, no loops written anywhere yet, and no memory addresses. XLA and IREE both consume this as **StableHLO**, a portability opset for exactly this level, whose own documentation promises five years of backward compatibility and two years of forward compatibility, so a graph compiled today keeps working against tomorrow's consumers.[^x2] TVM starts from a model graph of its own, then narrows to one operator at a time.[^tvm] Mojo and its MAX graph API describe the same computation as a MAX graph, consumed by Mojo's KGEN compiler.[^mojo] Triton is the outlier: a Triton program is already written at kernel granularity, closer to the second level below, so there is no separate whole-model graph to speak of.[^triton-paper]

This chapter's own examples use a fourth spelling for that first level: **tosa**, Tensor Operator Set Architecture, a real, versioned opset that ships inside MLIR itself.[^tosa] StableHLO is the more direct analogue of what XLA and IREE actually consume, but it is a separate project from the MLIR this book's toolchain already has installed, and this book's [examples policy](https://github.com/snp05016/vortex_language/blob/main/examples/README.md) is that every checked example runs through a tool already on the machine. tosa plays the same structural role: whole tensors, no loops, one operation per node.

--8<-- "includes/examples/mlir/m11-ml-compilers/graph_level.mlir.md"

`tosa.matmul` takes 3-D tensors, a leading batch dimension in front of the two matrix dimensions, because the operation is defined for batches of matrices, not for a single one.[^tosa] `tosa.clamp` carries its bounds as attributes, `min_fp` and `max_fp`, constant data attached to the operation rather than a value computed at run time: the same distinction [M2](m2-reading-mlir.md#attributes-and-properties-hold-the-constants) drew for `arith.cmpf`'s comparison kind. Checked as written, `mlir-opt` only verifies this file and prints it back with its values renumbered; nothing has lowered yet.

??? check "Why does `tosa.matmul` require a batch dimension even for a single matrix multiply, and what does that force the very next level to produce?"

    Because the operation's definition is for a batch of matrix multiplies, not one; a single multiply is the batch-of-one case. The next section's lowering therefore produces `linalg.batch_matmul`, not `linalg.matmul`, even though this chapter's example only ever uses one matrix.

## The kernel level: one computation, no loop order chosen yet

The pass `--tosa-to-linalg-named` turns each `tosa` operation into the `linalg` operation for the same computation, still over tensors, still with no loop written.[^tosa-to-linalg] `tosa.matmul` becomes `linalg.batch_matmul`, a named operation in the sense [M2](m2-reading-mlir.md#a-named-operation-hides-a-region) already read: a common computation with a name of its own, following the interface of `linalg.generic` underneath. `tosa.clamp` has no single named counterpart, so `--tosa-to-linalg` expands it into `linalg.generic` directly, with a two-line body that takes the minimum against the upper bound and then the maximum against the lower one. Run on this chapter's graph, stopping there, the tool produces:

```mlir
%0 = tensor.empty() : tensor<1x8x4xf32>
%1 = linalg.fill ins(%cst : f32) outs(%0 : tensor<1x8x4xf32>) -> tensor<1x8x4xf32>
%2 = linalg.batch_matmul ins(%arg0, %arg1 : tensor<1x8x16xf32>, tensor<1x16x4xf32>)
                         outs(%1 : tensor<1x8x4xf32>) -> tensor<1x8x4xf32>
%3 = tensor.empty() : tensor<1x8x4xf32>
%4 = linalg.generic {indexing_maps = [#map, #map1], iterator_types = ["parallel", "parallel", "parallel"]}
     ins(%2 : tensor<1x8x4xf32>) outs(%3 : tensor<1x8x4xf32>) {
  ^bb0(%in: f32, %out: f32):
    %5 = arith.minimumf %in, %cst_1 : f32
    %6 = arith.maximumf %5, %cst_0 : f32
    linalg.yield %6 : f32
} -> tensor<1x8x4xf32>
```

(run by hand for this section, stopping one step short of `kernel_level.mlir`'s full checked pipeline, shown in full below). `linalg.fill` writes the zero that `linalg.batch_matmul` accumulates into; that fill-then-accumulate shape, and not Vortex's own `let mut sum: f32 = 0.0` inside the innermost loop, is what `linalg.batch_matmul`'s own definition performs, an example of the difference [M2](m2-reading-mlir.md#a-named-operation-hides-a-region) already found between the named operation's body and the Vortex kernel it stands in for.

Every one of the five systems has a level that plays this role, under its own name. It is where XLA's fusion decides which operations end up in one kernel; where TVM narrows from its graph to one operator at a time before searching for that operator's schedule;[^tvm] where Triton's own MLIR dialects, `tt` for the tile-shaped program and `ttg` and `ttng` for its GPU-specific lowerings, represent a tile-level computation directly;[^triton-dialects] and where Mojo's KGEN dialects sit between its MAX graph and machine code.[^mojo] None of them is `linalg`, and none of their sources this chapter cites says two of them share an intermediate form; what they share is the position in the stack, one computation per node, tensors still whole, no order chosen for the arithmetic inside any one node.

## The loop level: memory, order, and where these systems disagree the most

One more step, `--tosa-to-linalg` for the parts `--tosa-to-linalg-named` leaves as arith, `--one-shot-bufferize` to turn each tensor into a memref, and `--convert-linalg-to-loops` to turn each structured operation into `scf.for` loops with explicit `memref.load` and `memref.store`, reaches the level where a back end like the ones [M10](m10-mlir-for-gpus.md) and the [backend book](../backend/index.md) cover can take over:

--8<-- "includes/examples/mlir/m11-ml-compilers/kernel_level.mlir.md"

The batch dimension, still 1 throughout, produces an outer loop that always runs once; a real batch would make it do work. The multiply-accumulate loop nest is the familiar three-loop matmul shape [stage 10](../compiler/guide/stage-10-matrix-multiplication.md) already builds by hand, and the clamp is a fourth, separate loop nest over the same shape, reading `%alloc` and writing `%alloc_1`: nothing at this level has fused the two into one pass over the output yet. That fusion, folding an activation into the same loop that just produced the value it acts on, is exactly what the [GPU book's matmul ladder](../gpu/g10-matmul-ladder.md) calls **epilogue fusion**, and it is a real optimization, not a free one: it means one extra branch or clamp per output element lives inside the hot loop instead of in a separate pass, at the cost of writing the two computations as one kernel instead of two. [G12](../gpu/g12-flashattention.md) is a full case study of a fusion built this way.

This is the level where the five systems disagree the most about mechanism, because it is where "how" gets decided rather than "what". TVM's schedule for each operator is chosen by a learned cost model that searches the space of candidate schedules, rather than a fixed heuristic or a human picking one by hand.[^tvm] Triton's compiler chooses the per-thread mapping, the shared-memory staging and the pipelining for a tile-shaped kernel; the programmer writes tile shapes, not thread indices.[^triton-tut] IREE compiles ahead of time into a bundle its own runtime loads, and on the owner's machine its documented route to an Apple GPU is through its **Metal HAL driver**: MLIR input lowered to the `spirv` dialect, converted to Metal Shading Language source text, and compiled at run time through the same `makeLibrary(source:options:)` call this book's other GPU chapters use.[^x3] Mojo's KGEN compiler documents GPU support for NVIDIA, AMD and Apple-silicon GPUs from the one MLIR-based compiler.[^mojo]

??? check "IREE's documented Metal route lowers through the spirv dialect before it ever becomes Metal Shading Language text. At which of these three points, MLIR input, spirv dialect, or MSL source, would a static array shape most obviously stop being a type the tool checks, and become a comment or a number a human wrote down?"

    Not at the `spirv` dialect: SPIR-V's own types still carry static array lengths, so the shape survives that step as a type. MSL source text is where it would most plausibly stop being checked as a type: unless the emitted `.metal` file uses MSL's own array types with matching literal bounds, a shape becomes just the numbers baked into loop bounds and index arithmetic, no longer something the language's type system enforces for you.

<figure class="vx-figure">
<svg viewBox="0 0 860 560" role="img" aria-label="This chapter's own graph compiled through three levels, with the role each level plays in the five named systems" aria-describedby="m11-f1-desc">
<title id="m11-f1-title">One graph, three levels, five systems' own names for each</title>
<desc id="m11-f1-desc">Three rows, each a box holding this chapter's own checked example at one level, connected top to bottom by arrows labeled with the mlir-opt pass that produces the next row. Row one, graph level: tosa.matmul and tosa.clamp, whole tensors, no loops. To its right, a note: every framework hands its compiler something at this level, StableHLO for XLA and IREE, a model graph for TVM, a MAX graph for Mojo. Row two, kernel level, reached by the pass tosa-to-linalg-named: linalg.batch_matmul and linalg.generic, one computation each, tensors, no loop order chosen. To its right: this is the position of TVM's operator level, XLA's fusion output, and Triton's own tt and ttg dialects. Row three, loop level, reached by bufferize and convert-linalg-to-loops: three nested scf.for loops over memref, explicit addresses and order. To its right: from here a back end takes over, the level IREE's Metal HAL driver, Triton's compiled kernel, and MLIR's own gpu dialect in M10 all lower past.</desc>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 3">
<rect class="vx-box-strong" x="20" y="20" width="280" height="56" rx="6"/>
<text class="vx-mono" x="160" y="45" text-anchor="middle">tosa.matmul</text>
<text class="vx-mono" x="160" y="63" text-anchor="middle">tosa.clamp</text>
<text class="vx-text" x="20" y="96">1. Graph level</text>
<text class="vx-text-muted" x="20" y="114">whole tensors, no loops, no memory</text>
<text class="vx-text-muted" x="330" y="38">Elsewhere: StableHLO, for XLA and IREE.</text>
<text class="vx-text-muted" x="330" y="56">A model graph, for TVM. A MAX graph,</text>
<text class="vx-text-muted" x="330" y="74">for Mojo/MAX.</text>
</g>
<path class="vx-flow" d="M160,140 L160,182"/>
<polygon class="vx-arrowhead" points="154,182 160,192 166,182"/>
<text class="vx-text-muted vx-mono" x="172" y="164">tosa-to-linalg-named</text>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 3">
<rect class="vx-box-strong" x="20" y="192" width="280" height="56" rx="6"/>
<text class="vx-mono" x="160" y="217" text-anchor="middle">linalg.batch_matmul</text>
<text class="vx-mono" x="160" y="235" text-anchor="middle">linalg.generic</text>
<text class="vx-text" x="20" y="268">2. Kernel level</text>
<text class="vx-text-muted" x="20" y="286">one computation each, still no order</text>
<text class="vx-text-muted" x="330" y="210">Elsewhere: TVM's operator level. XLA's</text>
<text class="vx-text-muted" x="330" y="228">fusion output. Triton's own tt and ttg</text>
<text class="vx-text-muted" x="330" y="246">dialects.</text>
</g>
<path class="vx-flow" d="M160,312 L160,354"/>
<polygon class="vx-arrowhead" points="154,354 160,364 166,354"/>
<text class="vx-text-muted vx-mono" x="172" y="326">bufferize +</text>
<text class="vx-text-muted vx-mono" x="172" y="340">convert-linalg-to-loops</text>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 3">
<rect class="vx-box-strong" x="20" y="364" width="280" height="66" rx="6"/>
<text class="vx-mono" x="160" y="389" text-anchor="middle">scf.for × 3, nested</text>
<text class="vx-mono" x="160" y="407" text-anchor="middle">memref.load / memref.store</text>
<text class="vx-text" x="20" y="450">3. Loop level</text>
<text class="vx-text-muted" x="20" y="468">explicit addresses, explicit order</text>
<text class="vx-text-muted" x="330" y="382">Elsewhere: where a back end takes over.</text>
<text class="vx-text-muted" x="330" y="400">IREE's Metal HAL driver, a compiled</text>
<text class="vx-text-muted" x="330" y="418">Triton kernel, and the gpu dialect (M10)</text>
<text class="vx-text-muted" x="330" y="436">each lower past this level.</text>
</g>
</svg>
<figcaption>Figure 1. <code>graph_level.mlir</code> and <code>kernel_level.mlir</code>, this chapter's own checked examples, drawn as the staircase <a href="m1-why-mlir.md#the-lowering-staircase">M1</a> introduced: each row is a level, each arrow a real pass this chapter runs. The notes beside each row name the position, not the exact form, that XLA, TVM, IREE, Triton and Mojo/MAX each have at that level, under their own vocabulary.</figcaption>
</figure>

## Five systems, one shape

| System | What it consumes | Built on | One fact about scheduling or targets |
| --- | --- | --- | --- |
| XLA (OpenXLA) | JAX, PyTorch or TensorFlow programs, through StableHLO | MLIR[^x1] | StableHLO's own compatibility window: 5 years backward, 2 years forward[^x2] |
| TVM | A model graph, narrowed to one operator at a time | Its own graph and operator-level IR, not MLIR[^tvm] | A learned cost model searches the schedule space for each operator, rather than a fixed heuristic choosing one[^tvm] |
| IREE | MLIR input dialects, including StableHLO and tosa | MLIR, compiled ahead of time into a bundle its own runtime loads[^x3] | On Apple GPUs, its Metal HAL driver embeds MSL source and compiles it at run time[^x3] |
| Triton | A tile-shaped kernel, written against a Python-embedded language | Its own MLIR dialects (`tt`, `ttg`, `ttng`), on an LLVM version the project pins by commit hash rather than by release[^triton-repo] | The compiler chooses per-thread mapping, shared-memory staging and pipelining; the kernel author writes tile shapes[^triton-tut] |
| Mojo / MAX | A Mojo program or a MAX graph | KGEN, a compiler built on MLIR[^mojo] | Documented GPU support: NVIDIA, AMD and Apple-silicon GPUs[^mojo] |

Four of the five are built on MLIR in some documented way; TVM, whose OSDI paper predates MLIR's public introduction, is the one system here with its own IR stack from the graph level down.[^tvm][^mlir-paper] That does not make TVM's stack a different shape, only a different implementation of the same shape: a graph level, an operator level with a chosen schedule, and generated code below it.

??? check "Which of these five systems is not stated, by the sources this chapter cites, to be built on MLIR, and does that change the number of levels its compiler has?"

    TVM. Its sources describe a graph level and an operator level with a searched schedule, the same three-part shape (graph, per-operation kernel, generated code) every other system in the table has; it is built with its own data structures for each level instead of MLIR's shared one, the trade [M1](m1-why-mlir.md#why-build-this-instead-of-a-two-level-scheme) already weighed for a project with two levels.

## Where Vortex sits

None of these five systems is a good match for what Vortex is. Each of them compiles a whole model, a graph of many operations chosen and connected by a user working in Python or a similar host language, and decides on its own which operations to fuse into one kernel, which schedule to give each one, and which target to generate code for. Vortex compiles one function at a time, at one fixed shape ([stage 10](../compiler/guide/stage-10-matrix-multiplication.md#one-function-per-shape)), written by hand:

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

This is closer to one fused kernel that sits *inside* any of the five systems, after their graph-level compiler has already decided what belongs together, than it is to one of the systems itself. It has no graph level: there is no step in a v0.1 Vortex compiler that decides two functions should become one kernel, because a v0.1 program is a fixed set of functions the programmer already wrote separately. And its floating-point rule is stricter than what these systems generally expose as a default. [Decision 56](../decisions/numbers.md#d56) forbids contracting a multiply and an add into one fused multiply-add and forbids reassociating a sum; MLIR's own `arith` operations carry a `fastmath` flag whose values include `contract` and `reassoc`, permissions a lowering pass is free to attach unless something stops it.[^arith] A system built to search for the fastest schedule has every incentive to reach for exactly those permissions once decision 56 is not in its way; Vortex's kernel promises the reader it will not, at the cost of leaving that search space unexplored.

The comparison also marks a real gap. Every one of these five systems already has a documented, working route from a whole-tensor graph to at least one GPU family. Vortex's compiler, as of the v0.1 stages this book assumes, has none: no graph level to have a route from, and no GPU back end for the route to end at. [M12](m12-vortex-gpu-path.md) weighs the options for building one, once [M9](m9-transform-dialect.md) and [M10](m10-mlir-for-gpus.md) have covered the two ideas, schedules as inspectable IR and MLIR's own path to GPU dialects, that this chapter's five systems each use some version of.

## For Vortex

!!! vortex "Exercise"

    **Build** a short written trace, not code: follow a Vortex-shaped kernel, the `multiply` function above, through IREE's documented Metal route, and say at each stage whether the kernel's fixed shapes and decision 56's rounding rule are still something the stage's own form states, or have already become the back end's problem.

    1. Read IREE's Metal HAL driver design doc.[^x3] List the stages it names between MLIR input and a running GPU kernel (MLIR input dialects, IREE's own intermediate forms, the `spirv` dialect, the emitted Metal Shading Language text, the run-time compile call). If `iree-compile` is installed on your machine, compile this chapter's `graph_level.mlir` (or a StableHLO version of the same graph) for a Metal target and read its intermediate output at whichever stages the tool will print; if it is not installed, work from the design doc's own description of each stage.
    2. For each stage from step 1, write one line the way [M1](m1-why-mlir.md#what-survives-a-step-and-what-does-not)'s exercise did for decision 25 and decision 43: does that stage's own form still state that `a` and `b` are `8x16` and `16x4`, or state anything about how `sum`'s additions must be ordered? Name the first stage where each fact disappears, or write "nowhere, so it is the back end's job" if it never appears as a checked fact at all.
    3. Two to four sentences: given what step 2 found, would you trust IREE's fusion and scheduling to keep decision 56 unread, the way [M2](m2-reading-mlir.md#attributes-and-properties-hold-the-constants) found nothing in MLIR's verifier that would catch a `fastmath` flag Vortex's own rule forbids, or would you insist on reading the emitted MSL by hand first.

    **Not yet:** writing any part of a Vortex-to-IREE bridge, choosing IREE, or any of these five systems, for Vortex ([M12](m12-vortex-gpu-path.md)), and any GPU route beyond this one traced path ([M10](m10-mlir-for-gpus.md)).

    **Proof that it works:** the per-stage table from step 2, each row citing the design doc's section or, if you compiled it, the actual command and file you read; and the two-to-four-sentence answer to step 3, naming the specific stage (or "nowhere") your table found for decision 56.

## Key ideas

!!! recap "Questions you can now answer"

    - **What shape do XLA, TVM, IREE, Triton and Mojo/MAX all share?** A graph level of whole-tensor operations, a kernel level with one computation per operation and no loop order chosen, and a loop level with explicit memory and order, whatever each system calls its own three levels.
    - **Which of the five is not built on MLIR, by the sources this chapter cites?** TVM: a graph and operator-level IR of its own, from a paper that predates MLIR's public introduction.
    - **What does StableHLO's compatibility promise buy XLA and IREE?** A graph compiled today keeps working against a consumer built up to five years later, or built up to two years before it: the two systems can each move independently of the format.
    - **What does a Triton programmer write, and what does the compiler choose instead?** The programmer writes tile shapes; the compiler chooses the per-thread mapping, the shared-memory staging and the pipelining.
    - **Where does IREE's documented route to an Apple GPU go?** Through its Metal HAL driver: MLIR input lowered through `spirv`, converted to Metal Shading Language source, compiled at run time.
    - **Where does Vortex sit among these five?** Closer to one fused kernel inside any of them than to a whole system: one function, one fixed shape, a stricter floating-point rule than any of the five expose as a default, and no graph level or GPU route of its own yet.

## Where this comes back

!!! next "You will use this again in"

    - [M9. Schedules as IR: the transform dialect](m9-transform-dialect.md): *schedule*, *payload IR*, *a schedule as inspectable data instead of a search*
    - [M10. MLIR for GPUs](m10-mlir-for-gpus.md): *the loop level these systems each lower past*, *the spirv dialect*
    - [M12. Designing Vortex's GPU path](m12-vortex-gpu-path.md): *IREE's Metal route*, *the graph-level gap*, *the adoption trade*
    - [G10. The GPU matmul ladder](../gpu/g10-matmul-ladder.md): *epilogue fusion*
    - [G12. Fusion case study: FlashAttention](../gpu/g12-flashattention.md): *fusing an activation into a kernel's own loop*
    - [P14. Algorithms and schedules](../optimize/p14-algorithms-and-schedules.md): *a searched schedule against a fixed one*

## Sources and further reading

For the graph-to-kernel boundary this chapter builds its own examples around, read the `tosa` and `linalg` dialect pages together with `graph_level.mlir` and `kernel_level.mlir` open.[^tosa][^linalg] For the five systems themselves, each one's own documentation is the primary source; the TVM and Triton papers explain why each project chose the design it did, not only what the design is.[^tvm][^triton-paper]

[^x1]: OpenXLA Project, "XLA". <https://openxla.org/xla>
[^x2]: OpenXLA Project, "StableHLO". <https://openxla.org/stablehlo>
[^x3]: IREE Project, "IREE", and "Metal HAL Driver" design document. <https://iree.dev/> and <https://iree.dev/developers/design-docs/metal-hal-driver/>
[^tvm]: Tianqi Chen, Thierry Moreau, Ziheng Jiang, Lianmin Zheng, Eddie Yan, Haichen Shen, Meghan Cowan, Leyuan Wang, Yuwei Hu, Luis Ceze, Carlos Guestrin and Arvind Krishnamurthy, "TVM: An Automated End-to-End Optimizing Compiler for Deep Learning", *OSDI 2018*, pp. 578-594. <https://www.usenix.org/conference/osdi18/presentation/chen>
[^triton-paper]: Philippe Tillet, H. T. Kung and David Cox, "Triton: An Intermediate Language and Compiler for Tiled Neural Network Computations", *MAPL 2019*, pp. 10-19, doi:10.1145/3315508.3329973. <https://www.eecs.harvard.edu/~htk/publication/2019-mapl-tillet-kung-cox.pdf>
[^triton-tut]: Triton Project, "Tutorials: Matrix Multiplication". <https://triton-lang.org/main/getting-started/tutorials/03-matrix-multiplication.html>
[^triton-dialects]: Triton Project, "MLIR Dialects". <https://triton-lang.org/main/dialects/dialects.html>
[^triton-repo]: Triton Project, repository README. <https://github.com/triton-lang/triton>
[^mojo]: Modular, "Get started with GPU programming", MAX/Mojo documentation, and "Mojo🔥 vision". <https://max.modular.com/gpu/intro-tutorial> and <https://mojolang.org/docs/vision/>
[^tosa]: MLIR Project, "'tosa' Dialect", entries `tosa.matmul` and `tosa.clamp`. <https://mlir.llvm.org/docs/Dialects/TOSA/>
[^tosa-to-linalg]: MLIR Project, "Passes", entries `-tosa-to-linalg-named` and `-tosa-to-linalg`. <https://mlir.llvm.org/docs/Passes/>
[^linalg]: MLIR Project, "'linalg' Dialect", section "Named Payload-Carrying Ops". <https://mlir.llvm.org/docs/Dialects/Linalg/>
[^arith]: MLIR Project, "'arith' Dialect", entries `FastMathFlagsAttr` and `FastMathFlags`. <https://mlir.llvm.org/docs/Dialects/ArithOps/>
[^mlir-paper]: Chris Lattner, Mehdi Amini, Uday Bondhugula, Albert Cohen, Andy Davis, Jacques Pienaar, River Riddle, Tatiana Shpeisman, Nicolas Vasilache and Oleksandr Zinenko, "MLIR: A Compiler Infrastructure for the End of Moore's Law", arXiv:2002.11054, 2020. <https://arxiv.org/abs/2002.11054>
