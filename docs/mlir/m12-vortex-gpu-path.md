# M12. Designing Vortex's GPU path

<p class="page-intro">Five ways a compiler's own IR can reach a GPU, what each one costs on the machine this book was written on, and the questions any of them still has to answer before its output could be trusted against Vortex's CPU reference: this chapter weighs the options and makes no decision yet.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 22 minutes · Builds on: [M10. MLIR for GPUs](m10-mlir-for-gpus.md), [M11. End-to-end ML compilers](m11-ml-compilers.md)</p>

???+ remember "Before you start, remember"

    ??? question "Why does Vortex's `multiply` kernel never need the boundary guard a generic GPU kernel carries at the top of every thread?"

        A generic kernel does not know its array's extent until run time, so it protects the extra threads a rounded-up block count creates with `if (idx < n) { ... }`. Vortex fixes an array's shape as part of its type, a compile-time constant identical at every call site, so a compiler that already knows `multiply` runs over exactly 64 × 64 elements can choose a block size that divides 64 with no remainder in both directions and never needs the guard at all.

        Introduced in [G2. The SIMT execution model](../gpu/g2-simt.md#bringing-this-back-to-vortex-fixed-shapes-and-the-boundary-guard).

    ??? question "What is kernel outlining, and what does it leave behind at the call site?"

        The step that takes code written as ordinary functions calling other functions and lifts the one piece that must run on the device out into its own compilation unit, with its own calling convention. MLIR's `gpu` dialect does this as a dedicated pass: it moves the body of a parallel region into a new function inside a separate `gpu.module`, and leaves a `gpu.launch_func` call behind where the region used to be.

        Introduced in [G9. GPU compilers inside LLVM](../gpu/g9-gpu-compilers-in-llvm.md#address-spaces-which-memory-a-pointer-means).

    ??? question "May a Vortex compiler turn `sum += a[row, k] * b[k, column]` into one fused multiply-add, or add the products in a different order than the source wrote them?"

        No. Every `f32` operation is one IEEE 754 operation, rounded to nearest with ties to even, and a compiler must not contract, reassociate or reorder them. The product is rounded, then the sum.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

    ??? question "Can two correct parallel reductions of the same numbers produce different bit patterns?"

        Yes. Floating-point addition rounds after every step, so two different groupings of the same values, produced by two different thread mappings, can round to two different representable numbers, even though every individual addition follows IEEE 754 exactly. A language that promises the same bits on every run has to fix the grouping, not only require that each step is correct.

        Introduced in [G6. Synchronization, atomics and reductions](../gpu/g6-synchronization.md#choosing-a-reduction-vortex-can-promise).

!!! goals "In this chapter"

    - Explain the one starting point every GPU path shares, and the point where they diverge.
    - Trace Vortex's `multiply` kernel through two lowering routes this machine can run today, and picture a third route the machine cannot check.
    - Weigh five ways a compiler's own IR can reach a GPU against a fixed set of costs: how many device families a route reaches, how much of the work an existing stack does, and what an implementer still has to write and maintain.
    - Recognize which parts of this chapter's plan are documented and checkable on this machine now, and which need hardware or tooling this machine does not have.
    - List the questions any one of the five options must still answer before its output could be trusted against Vortex's CPU reference.

## One IR, one fork in the road

Every option this chapter compares starts in the same place. By the time Vortex's front end is done, a function like `multiply` is no longer source text: it is Vortex's own IR, a form [stage 6](../compiler/guide/stage-6-first-machine-code.md#words-for-this-stage) already named lowering's destination, carrying the facts the front end has already settled and that a later phase must not re-decide. For `multiply`, that means: every array's shape is a compile-time constant, `a` and `b` are read-only, `c` is a `&mut` output the callee may write but never alias against an input, and every arithmetic operation is exactly the IEEE 754 operation the source wrote, in the order it wrote it. Nothing downstream, on a CPU target or a GPU one, gets to loosen any of that.

What happens next is where this chapter's subject begins. A CPU back end takes that IR to a register allocator and an instruction scheduler, the machinery [the backend book](../backend/index.md) builds stage by stage. A GPU back end has a harder first problem: before any of that, something has to decide which of the many pieces of software between Vortex and a real device does the work of turning the `row` and `column` loops into a grid of threads, deciding which memories the kernel is allowed to name, and drawing the line between the code that runs on the host and the code that runs on the device. Five different shapes that "something" can take are surveyed below. All five start from the same IR; none of the differences between them are visible until after this fork.

## Two routes this machine can run today

Before comparing five options in the abstract, it helps to have run two of them. Both start from the same small stand-in for `multiply`, a 4 × 4 matrix multiply written with `linalg.matmul` over `memref`s, small enough that `mlir-opt`'s output stays readable end to end. Running it through `--convert-linalg-to-parallel-loops`, `--gpu-map-parallel-loops`, `--convert-parallel-loops-to-gpu` and `--gpu-kernel-outlining` produces the `gpu.launch_func` and outlined `gpu.module` the remember box above described in the abstract:

--8<-- "includes/examples/mlir/m12-vortex-gpu-path/matmul-naive-launch.mlir.md"

Look at the launch configuration this default pipeline chose: `blocks in (4, 4, 1) threads in (1, 1, 1)`. One thread per block, sixteen blocks for sixteen output elements. Nothing about the pipeline is wrong; it is doing exactly what its name says, mapping each parallel-loop iteration to one GPU thread, with no opinion about whether that mapping is a good one. If this ran unmodified on the 64 × 64 `multiply` kernel, it would ask for 4,096 blocks of one thread each, and every one of G10's later rungs, shared-memory reuse, register tiling, coalesced access, would still be sitting on the table.[^g10] Tiling before the GPU-mapping pass runs changes that, and changes nothing about the arithmetic:

--8<-- "includes/examples/mlir/m12-vortex-gpu-path/matmul-tiled-launch.mlir.md"

`--scf-parallel-loop-tiling=parallel-loop-tile-sizes=2,2`, inserted before the same GPU-mapping passes, turns the launch into `4x1` blocks of `2x2` threads: asking for a 2 × 2 tile is asking for a 2 × 2 block of threads at each grid point, and the kernel body that results still reads and writes exactly the same sixteen elements it read and wrote before. This is [G10](../gpu/g10-matmul-ladder.md#tiling-is-a-schedule-change-not-new-arithmetic)'s point again, one level lower: tiling is a schedule change, and a pass that only changes the schedule cannot be the thing that decides whether the schedule is any good. Something above these passes, a cost model, a target description, or a programmer's choice, has to say which tile sizes to try.

The route from `gpu.module` toward real NVIDIA hardware needs one more step, and this machine can check part of it without owning a GPU at all. `--convert-gpu-to-nvvm` rewrites the block and thread ID reads inside a kernel into NVVM's special-register intrinsics:

--8<-- "includes/examples/mlir/m12-vortex-gpu-path/gpu-to-nvvm-ids.mlir.md"

The result carries `nvvm.read.ptx.sreg.ctaid.x` and `nvvm.read.ptx.sreg.tid.x` calls, an `nvvm.kernel` attribute, and a five-field struct, a pointer, a pointer, and three `i64`s, standing in for the `memref<2xi32>` argument. `mlir-opt` alone gets a kernel this far toward NVIDIA hardware with nothing more than the `mlir` dialect passes it ships with; on this machine, `llc` registers only the AArch64 target, so nothing here can go on to become PTX text. That last step needs a build of LLVM with `NVPTX` in its target list, which this machine does not have.[^l8] The two facts sit side by side deliberately: MLIR's lowering *into* the `nvvm` dialect is available and checkable without any GPU-specific back end at all, because it is MLIR rewriting MLIR; only the final translation to a real ISA, PTX text an NVIDIA driver can load, needs a target-specific code generator this machine's toolchain does not carry.

That struct is worth a second look, because it is not free. A `memref` argument crossing a function boundary in the `llvm` dialect becomes exactly this shape, a pointer, an offset and, for each dimension, a size and a stride, the **memref descriptor** [M4](m4-dialect-conversion.md#types-change-too-the-type-converter-and-the-memref-descriptor) already introduced.[^m20] For a general `memref`, whose shape and strides might not be known until run time, every one of those fields earns its place: the callee genuinely does not know how big the array is or how its rows are laid out without being told. Vortex's arrays are never in that position. `multiply`'s `a`, `b` and `c` all have their shape fixed in the type, `[f32; 64, 64]`, checked once, at compile time, the same fact [decision 43](../decisions/arrays.md#d43) uses to fix row-major order as the storage layout. A calling convention built for Vortex specifically would not need to pass the size and stride fields of that descriptor at all: they are the same known constant on every call, and carrying them at run time, in registers or in a struct a kernel loads from, is pure overhead a generic memref-based ABI pays and a Vortex-shaped one would not have to.

Picture, without writing it, what the corresponding step looks like on the option this machine cannot check by running anything: compiling `multiply` straight to Metal Shading Language (MSL) source text. There is no pass to run here, because there is no MLIR involved; a Vortex back end would walk its own IR once and print MSL source, the way any of this book's earlier stages print C or assembly. The kernel's two outer loops become `thread_position_in_grid.x` and `.y` reads at the top of an MSL function marked `[[kernel]]`; the innermost `k` loop stays an ordinary loop, unrolled or not by MSL's own compiler once it runs; and the function signature takes `a`, `b` and `c` as `device float*` buffer arguments, bound by index, with no descriptor struct at all, because MSL's calling convention is buffer-and-index, not pointer-offset-size. Everything about that last sentence, whether Vortex would emit one buffer index per array or pack all three behind one argument table, whether the fixed 64 × 64 shape lets the kernel skip a bounds check the way [G2](../gpu/g2-simt.md#bringing-this-back-to-vortex-fixed-shapes-and-the-boundary-guard) already showed it can, is left for the reader to work out by hand before the next section names the option this sketch belongs to.

## Five roads out of one IR

<figure class="vx-figure">
<svg viewBox="0 0 920 580" role="img" aria-labelledby="m12-f1-title m12-f1-desc">
<title id="m12-f1-title">One IR, five roads toward a GPU</title>
<desc id="m12-f1-desc">A single box, Vortex's own IR, sits at the left. Five arrows leave it, one per option. Option A goes to MSL source text, then to Apple GPUs, the only route this book's owner can run without renting hardware. Option B goes to LLVM IR, then splits into NVPTX, AMDGPU and SPIR-V back ends, each reaching one hardware family and, for SPIR-V, Apple GPUs indirectly through SPIRV-Cross. Option C goes to MLIR's structured dialects, then to the gpu dialect, then fans out to the same three back ends through nvvm, rocdl and spirv, plus the same indirect Apple route. Option D goes to an existing tile language or library, skipping most compiler work. Option E is a hybrid: MSL directly for Apple, and either the LLVM or MLIR route for NVIDIA and AMD, drawn as a box that reuses both of the arrows above it.</desc>
<rect class="vx-box-strong" x="20" y="255" width="150" height="70" rx="4"/>
<text class="vx-text" x="95" y="285" text-anchor="middle">Vortex's</text>
<text class="vx-text" x="95" y="303" text-anchor="middle">own IR</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5">
<path class="vx-flow" d="M170 270 L340 60"/>
<polygon class="vx-arrowhead" points="340,60 328,62 332,70"/>
<rect class="vx-box" x="340" y="20" width="180" height="60" rx="4"/>
<text class="vx-text" x="430" y="42" text-anchor="middle">A. Direct MSL</text>
<text class="vx-mono vx-text-muted" x="430" y="60" text-anchor="middle">source text, run-time build</text>
<path class="vx-flow" d="M520 50 L700 50"/>
<polygon class="vx-arrowhead" points="700,50 690,45 690,55"/>
<rect class="vx-box-strong" x="700" y="20" width="180" height="60" rx="4"/>
<text class="vx-text" x="790" y="42" text-anchor="middle">Apple GPU</text>
<text class="vx-mono vx-text-muted" x="790" y="60" text-anchor="middle">runs on this machine</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5">
<path class="vx-flow" d="M170 275 L340 175"/>
<polygon class="vx-arrowhead" points="340,175 328,174 332,183"/>
<rect class="vx-box" x="340" y="145" width="180" height="60" rx="4"/>
<text class="vx-text" x="430" y="167" text-anchor="middle">B. LLVM IR</text>
<text class="vx-mono vx-text-muted" x="430" y="185" text-anchor="middle">NVPTX / AMDGPU / SPIR-V</text>
<path class="vx-flow" d="M520 175 L700 130"/>
<polygon class="vx-arrowhead" points="700,130 690,128 693,137"/>
<path class="vx-flow" d="M520 178 L700 178"/>
<polygon class="vx-arrowhead" points="700,178 690,173 690,183"/>
<path class="vx-flow" d="M520 181 L700 226"/>
<polygon class="vx-arrowhead" points="700,226 693,219 690,228"/>
<rect class="vx-box" x="700" y="100" width="180" height="34" rx="4"/>
<text class="vx-mono" x="790" y="122" text-anchor="middle">NVIDIA (PTX)</text>
<rect class="vx-box" x="700" y="161" width="180" height="34" rx="4"/>
<text class="vx-mono" x="790" y="183" text-anchor="middle">AMD (AMDGPU)</text>
<rect class="vx-box" x="700" y="222" width="180" height="34" rx="4"/>
<text class="vx-mono" x="790" y="244" text-anchor="middle">SPIR-V (Vulkan, OpenCL)</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5">
<rect class="vx-box-strong" x="340" y="240" width="180" height="60" rx="4"/>
<text class="vx-text" x="430" y="262" text-anchor="middle">C. MLIR structured</text>
<text class="vx-mono vx-text-muted" x="430" y="280" text-anchor="middle">linalg &#8594; gpu &#8594; nvvm/rocdl/spirv</text>
<path class="vx-flow" d="M170 280 L340 270"/>
<polygon class="vx-arrowhead" points="340,270 330,265 331,275"/>
<path class="vx-flow" d="M520 250 L695 118"/>
<polygon class="vx-arrowhead" points="695,118 686,122 692,130"/>
<path class="vx-flow" d="M520 270 L700 178"/>
<polygon class="vx-arrowhead" points="700,178 690,177 692,187"/>
<path class="vx-flow" d="M520 290 L695 234"/>
<polygon class="vx-arrowhead" points="695,234 687,228 691,237"/>
<path class="vx-flow" d="M480 300 L750 700 750 720"/>
</g>
<path class="vx-flow vx-flow-dashed" d="M520 275 C 610 300, 660 320, 700 335"/>
<polygon class="vx-arrowhead" points="700,335 690,332 692,342"/>
<text class="vx-text-muted" x="620" y="320" font-size="12">SPIR-V &#8594; SPIRV-Cross &#8594; MSL</text>
<rect class="vx-box" x="700" y="315" width="180" height="34" rx="4"/>
<text class="vx-mono" x="790" y="337" text-anchor="middle">Apple GPU (indirect)</text>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5">
<path class="vx-flow" d="M170 290 L340 400"/>
<polygon class="vx-arrowhead" points="340,400 330,397 335,406"/>
<rect class="vx-box" x="340" y="375" width="180" height="60" rx="4"/>
<text class="vx-text" x="430" y="397" text-anchor="middle">D. Tile DSL / library</text>
<text class="vx-mono vx-text-muted" x="430" y="415" text-anchor="middle">Triton, MPS, cuBLAS calls</text>
<path class="vx-flow" d="M520 405 L700 385"/>
<polygon class="vx-arrowhead" points="700,385 690,381 692,391"/>
<rect class="vx-box" x="700" y="368" width="180" height="34" rx="4"/>
<text class="vx-mono" x="790" y="390" text-anchor="middle">whatever the DSL targets</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 5">
<path class="vx-flow" d="M170 300 L340 500"/>
<polygon class="vx-arrowhead" points="340,500 328,499 332,508"/>
<rect class="vx-box" x="340" y="470" width="180" height="70" rx="4"/>
<text class="vx-text" x="430" y="492" text-anchor="middle">E. Hybrid</text>
<text class="vx-mono vx-text-muted" x="430" y="510" text-anchor="middle">A for Apple, B or C for the rest</text>
<text class="vx-text-muted" x="340" y="528" font-size="11">reuses the arrows above</text>
</g>
</svg>
<figcaption>Figure 1. One IR, five roads toward a GPU. Vortex's own IR (left) forks into five options. A reaches only Apple GPUs, but is the only route this book's owner can compile and run without renting hardware. B and C both fan out to NVIDIA, AMD and SPIR-V back ends and reach Apple only indirectly, through SPIR-V and SPIRV-Cross. D skips most of the compiler work by calling an existing tile language or library. E keeps A for Apple and reuses B or C's fan-out for everything else.</figcaption>
</figure>

[The GPU-and-tensor-compiler research this chapter draws on](#sources-and-further-reading) names five shapes a Vortex GPU path could take. Every one of them starts at the same box in the figure above; what follows compares where each arrow goes and what it costs to build.

**A. Direct MSL.** Vortex's IR becomes Metal Shading Language source text, compiled at run time through `MTLDevice.makeLibrary(source:options:)`, or ahead of time with the `metal` command-line tool.[^ap4] This is the only *documented* way onto an Apple GPU: Apple publishes the MSL language and the run-time compilation API, but not an intermediate representation between them, so any route that does not end in MSL source or a `.metallib` built from it is undocumented territory on this hardware.[^ap1] It is also the one option that runs, today, on the machine this book was written on: a small Swift program that compiles an MSL string and dispatches a kernel over it worked without installing anything beyond what ships with the OS, and the device it ran on reports a `threadExecutionWidth` of 32 (Apple's warp-equivalent, the **SIMD-group**[^ap1]), 1,024 threads per threadgroup, and unified memory. The cost is narrowness: this path reaches Apple silicon and nothing else, and every kernel is generated as text, which means tracking MSL's own language versions the way any text-based code generator tracks its target's syntax.

**B. LLVM GPU back ends.** Vortex's IR becomes LLVM IR, lowered by NVPTX, AMDGPU or LLVM's SPIR-V target, the same three back ends [G9](../gpu/g9-gpu-compilers-in-llvm.md) already opened up.[^l1] [^l2] [^l3] This reuses LLVM if Vortex's CPU back end already uses it, and the PTX route in particular is thoroughly documented, sitting directly underneath the CUDA toolchain's own compiler.[^n1] The cost is everything G9 spent a full chapter on: address spaces the IR itself treats as opaque numbers a back end chooses to interpret, a kernel calling convention distinct from an ordinary function's, uniformity and convergence rules that constrain which optimizations are safe, and, for the SPIR-V target, structured control flow a divergent branch does not produce for free. None of that is optional bookkeeping; it is the price of using an IR designed for CPUs to describe a machine whose execution model CPUs do not have. Apple GPUs are reachable only indirectly here, through the SPIR-V target and a separate tool, not through NVPTX or AMDGPU at all.

**C. MLIR structured.** Vortex's IR becomes MLIR, most naturally `linalg` on statically shaped `memref`s or `affine`/`scf` loops, then the `gpu` dialect, then `nvvm`, `rocdl` or `spirv`, ending at the same three targets option B reaches.[^m9] [^m16] [^m18] [^m19] This is the route the section above walked in part: tiling, bufferization and, eventually, the transform dialect's schedules ([M9](m9-transform-dialect.md)) come largely for free, because MLIR's structured dialects were built to carry exactly this kind of reasoning, and a schedule expressed as `transform.structured.tile_using_for` is inspectable IR, not a string of compiler flags, which fits [the second design principle](../philosophy.md#2-say-what-to-calculate-then-decide-how-to-run-it) directly. The cost is a large, fast-moving C++ dependency: [M1](m1-why-mlir.md#the-lowering-staircase) already weighed what adopting MLIR buys a project against what building an equivalent scheme by hand would cost, and this option is that same trade, specifically for GPUs. Triton pins its LLVM dependency to an exact commit hash for a reason; whichever revision Vortex builds against, upstream MLIR moves fast enough that a pass name checked today is not a promise about next year.[^p10]

**D. Emit a tile DSL or call a library.** Vortex's IR becomes a call into an existing system: a Triton kernel, an MPS matrix routine, a cuBLAS call.[^ap11] This is the fastest route to competitive numbers, because someone else has already done the tuning. It is also the route that teaches the least about building a compiler, which matters for a project whose stated purpose is to teach compiler and performance engineering to the level a CPU, GPU or accelerator company would expect, not to produce the fastest matmul with the least new code. A route that hands the interesting part of the problem to an external runtime answers a different question than the one this book's reader is asking.

**E. Hybrid.** Vortex's own IR does the scheduling and the diagnostics, and only the last step differs by target: MSL text for Apple (option A), MLIR's `gpu` dialect or LLVM IR for NVIDIA and AMD (option B or C). This keeps Vortex's own story, and its own diagnostics, central for every target, while borrowing a proven lowering only where borrowing one buys something a hand-written back end would have to reinvent. The cost is real: two lowering paths to build, test and keep in sync, and a schedule's meaning, what a tile size means, what a reduction's grouping means, has to be defined once, above both paths, so that a program does not mean something subtly different depending on which back end happened to run it.

??? check "Options B and C both end at NVPTX, AMDGPU and LLVM's SPIR-V target. What is the actual difference between them, if the destination is the same?"

    What arrives at those back ends. Option B hand-writes LLVM IR directly, with every address space, kernel attribute and loop already chosen by the time the IR exists. Option C arrives at the same back ends through MLIR's structured dialects, which is where tiling, bufferization and fusion decisions get made, in dialects built to represent them; only after those decisions are final does a lowering pass turn the result into `nvvm`, `rocdl` or `spirv`, LLVM-IR-shaped MLIR that reaches the same back ends option B targets directly. The back ends do not know or care which route their input came from.

??? check "Why can this machine's `mlir-opt` lower a kernel into the `nvvm` dialect, complete with `nvvm.read.ptx.sreg.ctaid.x` calls, but cannot turn that same kernel into PTX text?"

    Because those two steps use different tools. Lowering into `nvvm` is MLIR rewriting MLIR: a dialect-conversion pass that only needs to know the target vocabulary, not generate real machine code. Turning `nvvm`-dialect MLIR into PTX text needs `mlir-translate` to emit LLVM IR and then a code generator, `llc`, that has the NVPTX target registered and knows how to encode PTX instructions. This machine's `llc` build registers only AArch64, so the second step has no target to run against, even though the first step already succeeded.

## What every option still has to answer

Naming a route is not the same as having a compiler. Whichever of the five options a Vortex GPU path eventually takes, it still owes concrete answers to questions none of the passes above settle on their own:

- **Thread mapping.** How does a `kernel` turn `multiply`'s `row` and `column` loops into a grid of blocks and threads? [G4](../gpu/g4-memory-performance.md#for-vortex) and [G5](../gpu/g5-occupancy.md#for-vortex) both built exercises around a written **target description**, a record of a specific chip's warp size, shared-memory budget and register budget; every one of the five options needs that same description before it can choose a mapping, because the mapping this section's naive pipeline chose by default, one thread per block, is only ever a starting point, never a schedule on its own.
- **Address spaces and unified memory.** Which memories does the language expose, and which does only the compiler know about? [G3](../gpu/g3-memory-hierarchy.md#unified-memory-on-apple-silicon) already noted that Apple silicon's unified memory collapses a distinction NVIDIA and AMD hardware still make explicit; a route that borrows NVPTX's or AMDGPU's address-space numbering carries assumptions that do not hold on the one GPU this machine can actually run a kernel on today.
- **Barrier rules under divergence.** [G9](../gpu/g9-gpu-compilers-in-llvm.md#convergent-operations-what-an-optimizer-must-not-move) already showed that a barrier or a shuffle cannot be moved or duplicated across a divergent branch without changing what the program computes. Any lowering Vortex writes, by hand or through an existing pass, has to preserve that rule for every barrier its own IR introduces, not only for the ones borrowed passes already protect.
- **Determinism of reductions.** `multiply`'s `sum` accumulation is a reduction, and [G6](../gpu/g6-synchronization.md#choosing-a-reduction-vortex-can-promise) already showed that two correct groupings of the same values can produce different bits. Splitting the `k` loop across a block tile, a warp tile and a thread tile, the way [G10](../gpu/g10-matmul-ladder.md) does rung by rung, changes the grouping every time a tile size changes; a Vortex compiler needs a documented answer for whether that grouping is fixed or is allowed to vary with the schedule, before it can promise anything about a GPU-mapped reduction's output.
- **Explicit precision, never a default.** [G11](../gpu/g11-matrix-units.md) covers tensor cores and their SIMD-group equivalents on Apple hardware in full; whichever path eventually carries a tile operation that far, the same rule this chapter's remember box opened with still applies: a reduced-precision, fixed-order instruction changes a result's bits, and needs the programmer's explicit opt-in, never an automatic substitution the compiler makes on its own.
- **Fastmath flags, never attached.** MLIR's `arith` dialect can carry `fastmath` attributes such as `contract` and `reassoc` on floating-point operations, and [M2](m2-reading-mlir.md) already showed how to read them on the page.[^m10] Whichever MLIR-based route a Vortex lowering takes, decision 56 means its own pass may never attach one of those flags to an `arith.mulf` or `arith.addf` it emits, no matter what a downstream optimization pass might otherwise be tempted to do with the permission such a flag grants.
- **How the decision gets reported.** [The sixth design principle](../philosophy.md#6-explain-performance-decisions) asks a Vortex compiler to tell the programmer about an important optimization it made, or explain why it could not apply one the programmer expected. [G14](../gpu/g14-measuring-gpu-code.md) covers measuring a kernel once it runs; a GPU path additionally owes a report at compile time, in the same spirit: which tile sizes it chose, which target it built for, and why, in words a programmer without a profiler open can still read.

None of these six questions has a settled answer yet, on purpose: the five options above change how expensive each answer is to build, not whether it has to be answered.

## A path that fits a Mac-only compiler project

Weighing five options in the abstract is one exercise; deciding what to build first, with one Apple-silicon laptop and no rented GPU, is a narrower one, and it does not require picking a final answer to answer. Three facts about this machine constrain it directly. Metal is free, local and already working: a kernel compiled from MSL source text and dispatched from a small host program runs today, with no additional install. MLIR's structured dialects are also free and local, and checkable in full without a GPU at all: every pass this chapter ran, `linalg` to parallel loops, parallel loops to `gpu`, `gpu` to `nvvm`, runs on `mlir-opt` alone, and only the final step, real machine code for a real device, needs hardware or a differently configured toolchain this machine does not have. NVIDIA work does not: the CUDA toolkit stopped shipping for macOS after version 10.2, so anything past reading the documentation needs a rented machine, and AMD's stack is cloud-only for the same reason.[^n13] A learning path shaped by those three facts starts with Metal, hand-written, to build intuition for what a GPU kernel is; moves to MLIR's GPU stack next, because every step through `linalg`, `affine`, `scf` and `gpu` is checkable locally, with the eventual PTX or AMDGPU step held as future work rather than blocking anything before it; and treats NVIDIA and AMD hardware access as a later, separate decision, made once there is a specific reason to rent it. Whether GPU-launching tests could run in this project's existing continuous integration is a fourth, open question: the CI provider's own documentation of its Apple-silicon runners says nothing about GPU or Metal access one way or the other, so this chapter records the question as unresolved rather than guessing at an answer neither side of that document settles.[^c2] Text-level checks, the kind this chapter's own three examples already are, comparing MLIR or PTX output byte for byte, do not have that problem and can run anywhere `mlir-opt` runs.

??? check "Both the direct-MSL route and the MLIR route can be checked in full on this machine, but for different reasons. What is the difference?"

    Direct MSL can be checked because it needs nothing this machine lacks: Apple documents run-time compilation from MSL source, and a small program that calls it runs the actual kernel on the actual GPU, not merely a text transformation. The MLIR route can be checked up to, but not including, a real device: every pass this chapter ran rewrites MLIR into more MLIR, which `mlir-opt` verifies without needing a GPU or even a GPU-capable code generator; only the final step, from `nvvm`-dialect MLIR to PTX text a driver could load, needs a piece of tooling (an NVPTX-enabled `llc`) this machine's default LLVM build does not carry.

## For Vortex

!!! vortex "Exercise"

    **Build** a written design, not code: a worksheet, using [the feature decision worksheet](../philosophy.md#feature-decision-worksheet), for one specific choice among options A through E, applied to `multiply`.

    1. **Problem:** which of Vortex's stated target workloads would actually benefit from a GPU path first, and why does that workload, not a generic "GPU support" goal, favor the option you pick?
    2. **Example:** state precisely what your chosen option would emit for `multiply`, at the level this chapter's worked and half-finished traces did: the kernel's signature, how `row` and `column` become a thread's identity, and how (or whether) the fixed 64 × 64 shape lets it skip a bounds check.
    3. **Boundary:** name one Vortex program your option would handle badly, or not at all, and say which of the other four options would handle it better, and why.
    4. **Compiler knowledge:** what does the compiler have to know about a kernel before it can emit anything under your option, that it does not already know from ordinary type checking? A target description, as [G1](../gpu/g1-throughput-machines.md), [G4](../gpu/g4-memory-performance.md) and [G5](../gpu/g5-occupancy.md) each build a piece of, is one likely answer; say what else belongs in it for your option specifically.
    5. **Targets:** using this chapter's [six open questions](#what-every-option-still-has-to-answer), answer each one for your chosen option: thread mapping, address spaces and unified memory, barrier rules, reduction determinism, explicit precision, and fastmath flags. An answer of "inherited from the underlying stack, unchanged" is acceptable where it is true, but say so explicitly rather than leaving the question blank.
    6. **Diagnostics:** for one wrong or slow choice your option could make (a tile size that does not fit the target's shared memory, a mapping that leaves a warp partly idle), what should the compiler tell the programmer, and where does that report fit against [the sixth design principle](../philosophy.md#6-explain-performance-decisions)?
    7. **Testing:** describe, without writing one, a test that would show your chosen option produces the same result as Vortex's CPU reference for `multiply`, within whatever tolerance decision 56's no-reassociation rule allows, and a second test that would catch the launch-configuration mistake from question 6 before it reached a device.

    **Not yet:** picking a final option for Vortex; writing an MSL, LLVM IR or MLIR emitter, or any other GPU-facing compiler code; choosing tile sizes automatically ([P15](../optimize/p15-choosing-parameters.md) covers that question once there is code to tune); building the actual GPU-launching test harness question 7 describes.

    **Proof that it works:** your worksheet's answer to question 5 addresses every one of the six questions by name, with no question left implicitly unanswered; your answer to question 3 names a real Vortex construct, not a hypothetical one; and your answer to question 2 is specific enough that a reader who has not seen this chapter could tell, from your worksheet alone, which of the five options you chose.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is the one thing every GPU path shares, no matter which of the five options a compiler picks?** The starting point: Vortex's own IR, already carrying fixed shapes, `&`/`&mut` effects and IEEE-754-exact arithmetic, before any of the five roads diverge.
    - **Why does this machine's `mlir-opt` reach the `nvvm` dialect but not PTX text?** Lowering into `nvvm` is MLIR rewriting MLIR, checkable with no GPU-specific target at all; turning that into PTX needs `llc` built with the NVPTX target registered, which this machine's default LLVM build does not have.
    - **Why is a memref descriptor's size and stride fields dead weight for a Vortex array specifically?** Because Vortex's array shapes are compile-time constants, the same on every call, while a general memref descriptor carries those fields to support shapes that are not known until run time, a case Vortex's fixed-shape arrays never produce.
    - **What must every one of the five options still decide about `multiply`'s `sum` accumulation, regardless of which hardware it targets?** Whether the reduction's grouping is fixed (reproducible bit for bit, at the cost of more synchronization) or left to whatever order the schedule produces (cheaper, but not reproducible across schedules).
    - **Why is "the option with the least new compiler code" not the same question as "the right option for Vortex"?** Because the project's purpose is to teach compiler and performance engineering, not only to produce fast code; the option that calls an existing library answers a different question than the one the book's reader is asking.
    - **What did the tiled-launch example change about `matmul4`'s arithmetic, compared to the naive-launch example?** Nothing. Both examples compute the identical product; only the launch configuration, and which thread owns which output element, differs between them.

## Where this comes back

!!! next "You will use this again in"

    - [Guide stage 11. Release](../compiler/guide/stage-11-release.md): *the target and back end, once one is chosen and has to be documented*

## Sources and further reading

This chapter's account of Vortex's GPU path options, the ladder shape it cites, and the facts observed on the machine it was written on all come from the project's GPU and tensor-compiler research notes, section 4 ("How a Vortex GPU path could look") and section 0 (local experiments), verified against the sources below.

[^g10]: Vortex documentation, [G10. The GPU matmul ladder](../gpu/g10-matmul-ladder.md): the naive kernel's 309 GFLOP/s (1.3% of cuBLAS) figure and the shape of rungs 1 through 9, from Simon Boehm, "How to Optimize a CUDA Matmul Kernel for cuBLAS-like Performance: a Worklog", 2022, measured on one RTX A6000 multiplying two 4092 × 4092 `f32` matrices without tensor cores, December 2022. <https://siboehm.com/articles/22/CUDA-MMM>
[^ap4]: Apple, "makeLibrary(source:options:)". <https://developer.apple.com/documentation/metal/mtldevice/makelibrary(source:options:)>
[^ap1]: Apple, "Metal Shading Language Specification", version 4.1 (PDF), section defining SIMD-groups and their execution width. <https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf>
[^l1]: LLVM Project, "User Guide for NVPTX Back-end". <https://llvm.org/docs/NVPTXUsage.html>
[^l2]: LLVM Project, "User Guide for AMDGPU Backend". <https://llvm.org/docs/AMDGPUUsage.html>
[^l3]: LLVM Project, "User Guide for SPIR-V Target". <https://llvm.org/docs/SPIRVUsage.html>
[^n1]: NVIDIA, "CUDA Programming Guide", version 13.4. <https://docs.nvidia.com/cuda/cuda-programming-guide/index.html>
[^m9]: MLIR, "'linalg' Dialect". <https://mlir.llvm.org/docs/Dialects/Linalg/>
[^m16]: MLIR, "'gpu' Dialect". <https://mlir.llvm.org/docs/Dialects/GPU/>
[^m18]: MLIR, "'NVVM' Dialect" and "'ROCDL' Dialect". <https://mlir.llvm.org/docs/Dialects/NVVMDialect/> ; <https://mlir.llvm.org/docs/Dialects/ROCDLDialect/>
[^m19]: MLIR, "'SPIR-V' Dialect". <https://mlir.llvm.org/docs/Dialects/SPIR-V/>
[^m20]: MLIR, "'llvm' Dialect" and "Target LLVM IR". <https://mlir.llvm.org/docs/Dialects/LLVM/> ; <https://mlir.llvm.org/docs/TargetLLVMIR/>
[^m10]: MLIR, "'arith' Dialect". <https://mlir.llvm.org/docs/Dialects/ArithOps/>
[^p10]: Triton repository README, noting the project pins its LLVM dependency to a specific commit. <https://github.com/triton-lang/triton>
[^ap11]: Apple, "MPSMatrixMultiplication" (title only; not used for a detailed claim). <https://developer.apple.com/documentation/metalperformanceshaders/mpsmatrixmultiplication>
[^n13]: NVIDIA, "CUDA 10.2 Release Notes", the last CUDA toolkit release to support macOS. <https://docs.nvidia.com/cuda/archive/10.2/cuda-toolkit-release-notes/index.html>
[^c2]: GitHub, "GitHub-hosted runners reference", documenting Apple-silicon macOS runners without stating whether GPU or Metal access is available on them. <https://docs.github.com/en/actions/reference/runners/github-hosted-runners>
[^l8]: Homebrew, `llvm` formula, which builds MLIR with every target, including NVPTX, when `LLVM_TARGETS_TO_BUILD=all` is set; the LLVM this book's examples run against was not built with that target list. <https://formulae.brew.sh/formula/llvm>
