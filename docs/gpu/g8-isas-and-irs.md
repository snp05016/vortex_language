# G8. ISAs and IRs

<p class="page-intro">One tiny kernel, written once, read back out as four vendors' own representations: NVIDIA's virtual and real instruction sets, AMD's fully published ISA, Khronos's structured binary IR, and the one rung of Apple's stack that Apple documents.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 25 minutes · Builds on: [G7. Programming models tour](g7-programming-models.md)</p>

???+ remember "Before you start, remember"

    ??? question "Why do all 32 lanes of a warp execute the same instruction at the same moment?"

        The hardware does not, in fact, give each thread its own instruction stream. It gathers threads into a fixed-size group (NVIDIA's warp, AMD's wavefront, Apple's SIMD-group) and issues one instruction to the whole group at once; a lane whose branch disagrees just sits out the instructions the others take.

        Introduced in [G2. The SIMT execution model](g2-simt.md).

    ??? question "What must be true before one thread may safely read a value that another thread in its group stored?"

        Both threads must have passed a barrier that orders the store before the load. A barrier moves no data of its own; it adds one fact, that everything written before it is visible to everything reading after it.

        Introduced in [G6. Synchronization, atomics and reductions](g6-synchronization.md).

    ??? question "Which of CUDA and Metal keeps kernel code and the code that launches it in one source file, compiled together?"

        CUDA (and HIP): a `__global__` function and its `<<<...>>>` launch live side by side and are compiled by one call to `nvcc`. Metal keeps the two apart: a `.metal` file holds only the kernel, and launching it is a separate, host-side job.

        Introduced in [G7. Programming models tour](g7-programming-models.md).

    ??? question "May a Vortex compiler regroup the additions in `sum += a[row, k] * b[k, column]`, or fuse the multiply and the add into one rounding?"

        No. Each floating-point operation is one IEEE 754 operation, rounded once, and must not be contracted, reassociated or reordered, on any target.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Explain what an instruction set architecture is, and what makes some of them *virtual*: a documented format no chip executes directly.
    - Read the same kernel's NVIDIA, AMD, Khronos and Apple representations far enough to say, for each, which rung a programmer can actually target and read a written specification for.
    - Trace one kernel through the two ladders this chapter's own examples compile and check: MLIR's `gpu` dialect lowered toward NVVM and toward SPIR-V.
    - Explain why SPIR-V demands structured control flow, and show what that costs a lowering pass that did not ask for it.
    - State which representation a Vortex back end would have to emit for each vendor family, without deciding which family to target.

## One kernel, four ladders

Take the smallest kernel worth naming: a one-sided clamp, `y[i] = max(x[i], 0)`, one comparison and one store per element. This chapter uses it, unchanged in shape, as the through-line, written three times.

First, in the one representation Apple documents end to end: Metal Shading Language source text.

--8<-- "includes/examples/gpu/g8-isas-and-irs/relu.metal.md"

Second, in MLIR's vendor-neutral `gpu` dialect, lowered toward NVIDIA's `nvvm` dialect, the LLVM-level representation NVIDIA's own back end reads:

--8<-- "includes/examples/gpu/g8-isas-and-irs/relu_nvvm.mlir.md"

Third, the same source kernel lowered toward `spirv`, the Khronos dialect that produces real SPIR-V:

--8<-- "includes/examples/gpu/g8-isas-and-irs/relu_spirv.mlir.md"

The MSL file is never compiled by this book's harness: Apple's offline compiler needs a toolchain component this machine does not have installed, so the file is checked as text only, read but not built. The other two are real `mlir-opt` runs, and their expected output is exactly what the pass produced, byte for byte, the last time this page was checked.

Three representations, one kernel, and already a question worth asking: which of these three could a programmer outside NVIDIA, Khronos or Apple read a written specification for, and which are only readable because a tool happened to print them? That question is this chapter's subject.

## ISAs, and the virtual kind

An **instruction set architecture (ISA)** is the set of instructions, registers and encodings a processor executes: the contract between software and the chip that runs it, precise enough that two different chips implementing the same ISA run the same binary. A GPU's real ISA is the actual bit pattern its cores decode.

Some vendors interpose a second, published format between source code and that real ISA: a **virtual instruction set**, an ISA-shaped text or binary that describes the computation completely but that no chip decodes directly. A further translation step, run by a driver or a separate tool, turns it into whatever the installed GPU actually needs. NVIDIA's PTX is the clearest example in this chapter: its own reference calls it "a low-level parallel thread execution virtual machine and instruction set architecture", one level above the machine ISA, and describes the driver finishing the translation to the real ISA for whichever GPU is installed.[^ptx-isa] The benefit is portability of the *published* format: an application can ship PTX once, and a new GPU generation only needs a new driver that knows how to finish the job, not a recompile of the application.

That pattern, a documented virtual layer sitting above an opaque real one, does not hold for every vendor. The rest of this chapter reads four vendors' ladders far enough to see which ones use it, and which do not need to.

## NVIDIA: PTX is public, SASS is inferred

PTX is the rung NVIDIA writes a specification for. The current reference (PTX ISA 9.4) documents every instruction's operands, address spaces and rounding modifiers in prose, the way an ordinary ISA manual would, and states explicitly that it changes across CUDA releases while staying a stable target for compilers to emit.[^ptx-isa] Ordinary mnemonics look almost readable on sight: `ld.global` loads through a pointer in global memory, `st.shared` stores into the group-local scratchpad, `bar.sync` is the barrier instruction behind the `__syncthreads()` a CUDA kernel calls. LLVM's own NVPTX back end targets this same virtual ISA, which is why an NVVM-dialect MLIR module such as this chapter's second example is one recognizable step above PTX rather than a different design.[^nvptx]

**SASS** is the name for the real, per-architecture machine ISA NVIDIA's GPUs actually execute; it has no equivalent prose reference. What exists instead is `cuobjdump` and `nvdisasm`, two tools that read a compiled binary back out as assembly-like text, and a reference (the CUDA Binary Utilities documentation) that lists SASS's opcodes architecture by architecture rather than describing the instruction set as a whole the way the PTX manual does.[^cuda-binutils] SASS mnemonics differ from PTX's (a load through global memory becomes `LDG`, not `ld.global`) and from one GPU generation to the next, which is exactly the freedom the virtual-ISA layer buys NVIDIA: PTX can stay stable while SASS keeps changing underneath it.

This machine has no NVIDIA GPU and no CUDA toolkit: local `llc` only registers AArch64, and `nvcc` is not on `PATH`. Nothing in this section is disassembly this book produced; it is what NVIDIA's own manuals document. A reader who wants real PTX or SASS for a small kernel can compile it on Compiler Explorer, whose `nvcc133` target is the compiler id this repository already uses for CUDA examples.

??? check "An application ships only PTX, not SASS, for a kernel it wants to run on GPUs that do not exist yet. Why does that work?"

    Because PTX is the documented, stable target: the driver installed alongside a future GPU knows how to finish translating PTX to that GPU's own SASS. If the application shipped SASS instead, it would be tied to whichever architecture's opcode encoding it was compiled for.

## AMD: no virtual layer, because the real one is already public

AMD's ladder skips the middle rung entirely. AMD publishes ISA manuals for every RDNA and CDNA generation it has shipped (RDNA 1 through 4, CDNA 1 through 5 at the time this chapter was written), each one a full instruction-set reference for the real hardware, plus a machine-readable version of the same data.[^amd-isa] There is no separate, stable virtual ISA that HIP or an AMD-targeting OpenCL implementation compiles to first and that a driver then finishes translating; the compiler goes from source through LLVM IR to the AMDGPU back end and straight to that same real, documented ISA.[^amdgpu-backend]

LLVM IR does appear in the middle of that path, but it is not a published target the way PTX is: nothing AMD ships treats an LLVM IR file as a stable interchange format a third party is meant to read, write or archive. It is the compiler's own working representation, present in this pipeline for the same reason it is present in every other LLVM-based back end this book's [backend book](../backend/index.md) covers, not because AMD chose to expose one more documented rung.

??? check "AMD documents the real AMDGPU ISA in full. Why would a compiler author still prefer to reach it through LLVM's AMDGPU back end rather than emit that ISA directly from their own compiler?"

    Because everything between a typed IR and a working binary on a real machine, instruction selection, register allocation across a wavefront's worth of state, and scheduling, is already solved there, tested against real hardware, and shared with every other LLVM-based front end. [G9](g9-gpu-compilers-in-llvm.md) opens exactly that back end.

## Khronos: one binary IR, and a rule about branches

SPIR-V is Khronos's own answer to the same question: a single binary intermediate representation, with one public specification, that more than one vendor's driver consumes for Vulkan and OpenCL.[^spirv-spec] Where PTX is NVIDIA's private virtual ISA, SPIR-V is a shared one: any front end that can emit it (a Vulkan GLSL or HLSL compiler, an OpenCL implementation, MLIR's own SPIR-V dialect) hands the same binary format to any vendor's driver, and what happens beneath SPIR-V is each vendor's own business, undocumented outside that vendor the way SASS is.

SPIR-V's specification adds a rule neither PTX nor ordinary LLVM IR imposes: every function must have **structured control flow**, meaning every selection and every loop is wrapped in an explicit region that names the one block where its branches reconverge.[^spirv-scf] An arbitrary control-flow graph, branches and merges wherever a compiler happened to put them, is not legal SPIR-V; it has to be structured first.

This chapter's two `gpu`-dialect lowerings make that rule concrete, because they start from the same `scf.if` and end up looking different for no reason but the target's own requirement. Lowered toward NVVM, the conditional becomes an ordinary pair of basic blocks joined by a plain branch, with the value each side computed carried in a block argument, the shape a hand-written LLVM IR function would have. Lowered toward SPIR-V, the identical `scf.if` is wrapped in a `spirv.mlir.selection` region that ends in a `spirv.mlir.merge`, naming the block where both sides rejoin. Nothing in the source MLIR asked for that region; the `--convert-gpu-to-spirv` pass reaches for it because the specification requires it of every conditional, not only complicated ones.

??? check "In this chapter's two lowerings, only one of them wraps its branch in an explicit merge region. Where did that region come from, if not the source `scf.if`?"

    From the target's own specification, not the source. SPIR-V requires every selection to name the block where control reconverges; NVVM (and ordinary LLVM IR) has no such requirement, so the equivalent lowering leaves the branch as a plain pair of blocks.

## Apple: MSL is the floor

Apple's own documentation describes an offline build that carries a `.metal` file through an intermediate `.ir` file to a `.metallib`, and a run-time path that hands MSL source text straight to `MTLDevice.makeLibrary(source:options:)` and gets back a compiled library, without ever naming an intermediate format at all.[^metal-precompile][^metal-source] Both paths are documented starting points and stop at the same place: nowhere does Apple publish what the `.ir` file, or the bytes inside a `.metallib`, actually contain. The Metal Shading Language specification itself mentions "AIR" exactly once, inside the description of a compiler option, and never defines it.[^msl-spec] A separate tool, the Metal shader converter, translates a different input (DXIL, from Direct3D shaders) into a `.metallib`, which is useful for porting work but tells a reader nothing about the format it produces, since it is not documented there either.[^metal-converter]

That makes MSL text the one rung of Apple's ladder anyone outside Apple can target, read a specification for, or test a compiler's output against. A small experiment on the machine these chapters are written on (Apple M4 Pro, macOS 27, 2026-09-23) confirms that the run-time path works without the separate Metal Toolchain component the offline compiler needs, and that the device answers ordinary Metal API queries with concrete numbers: a thread-execution width of 32 (this book's warp width for Apple GPUs, from [G2](g2-simt.md#warps-wavefronts-and-simd-groups)), a limit of 1024 threads per threadgroup, and 32,768 bytes of threadgroup memory, which matches the documented Feature Set Tables' figure for this device family once the exact table is read rather than summarized.[^feature-tables] None of that reaches below MSL text; it is what the documented rung reports about itself, not a look at the rung underneath it.

??? check "Precompiling a `.metal` file ahead of time, and calling `makeLibrary(source:options:)` at run time, are both documented Apple APIs. Does either one let a reader inspect the format between MSL text and the GPU's own instructions?"

    No. Both stop at MSL text going in and a working pipeline coming out; the `.ir` and `.metallib` formats in between are not described by either path, or by the MSL specification, or by the Metal shader converter's own documentation.

## The ladder, side by side

Four vendors, four different heights at which the documentation stops. NVIDIA documents a virtual ISA one rung above the real one. AMD documents the real ISA directly, because it never adds a virtual rung. Khronos documents a binary IR shared across vendors, one rung above whatever each vendor's own driver does with it. Apple documents only the source language, one rung above everything else in its own stack.

<figure class="vx-figure">
<svg viewBox="0 0 780 410" role="img" aria-labelledby="g8-f1-title g8-f1-desc">
<title id="g8-f1-title">The same three-rung ladder for four vendors, marking which rungs are documented</title>
<desc id="g8-f1-desc">Four columns, NVIDIA, AMD, Khronos and Apple, each with a source-language box at top, a middle box, and a real-instructions box at bottom, connected by arrows pointing down. NVIDIA documents its top two rungs, CUDA C++ and PTX, and leaves SASS undocumented. AMD documents its top rung and its bottom rung, HIP or OpenCL C++ and the AMDGPU ISA, and its middle rung, LLVM IR, is compiler-internal and not a published target. Khronos documents its top two rungs, a shading language and SPIR-V, and leaves each vendor's own driver ISA undocumented. Apple documents only its top rung, MSL, and leaves both AIR and the GPU's own instructions undocumented.</desc>

<g style="--vx-i: 0; --vx-n: 4">
<text class="vx-text-accent" x="105" y="24" text-anchor="middle">NVIDIA</text>
<rect class="vx-box-strong" x="20" y="34" width="170" height="46" rx="4"/>
<text class="vx-text" x="105" y="62" text-anchor="middle">CUDA C++ (source)</text>
<line class="vx-line" x1="105" y1="80" x2="105" y2="112"/>
<polygon class="vx-arrowhead" points="100,112 105,120 110,112"/>
<rect class="vx-box-strong" x="20" y="122" width="170" height="46" rx="4"/>
<text class="vx-text" x="105" y="150" text-anchor="middle">PTX (virtual ISA)</text>
<line class="vx-line" x1="105" y1="168" x2="105" y2="200"/>
<polygon class="vx-arrowhead" points="100,200 105,208 110,200"/>
<rect class="vx-box" x="20" y="210" width="170" height="46" rx="4" fill="none" stroke-dasharray="4 3"/>
<text class="vx-text-muted" x="105" y="238" text-anchor="middle">SASS (real ISA)</text>
</g>

<g style="--vx-i: 1; --vx-n: 4">
<text class="vx-text-accent" x="295" y="24" text-anchor="middle">AMD</text>
<rect class="vx-box-strong" x="210" y="34" width="170" height="46" rx="4"/>
<text class="vx-text" x="295" y="55" text-anchor="middle">HIP / OpenCL C++</text>
<text class="vx-text" x="295" y="70" text-anchor="middle">(source)</text>
<line class="vx-line" x1="295" y1="80" x2="295" y2="112"/>
<polygon class="vx-arrowhead" points="290,112 295,120 300,112"/>
<rect class="vx-box" x="210" y="122" width="170" height="46" rx="4" fill="none" stroke-dasharray="4 3"/>
<text class="vx-text-muted" x="295" y="150" text-anchor="middle">LLVM IR (internal)</text>
<line class="vx-line" x1="295" y1="168" x2="295" y2="200"/>
<polygon class="vx-arrowhead" points="290,200 295,208 300,200"/>
<rect class="vx-box-strong" x="210" y="210" width="170" height="46" rx="4"/>
<text class="vx-text" x="295" y="238" text-anchor="middle">AMDGPU ISA (real)</text>
</g>

<g style="--vx-i: 2; --vx-n: 4">
<text class="vx-text-accent" x="485" y="24" text-anchor="middle">Khronos</text>
<rect class="vx-box-strong" x="400" y="34" width="170" height="46" rx="4"/>
<text class="vx-text" x="485" y="55" text-anchor="middle">GLSL / HLSL / WGSL</text>
<text class="vx-text" x="485" y="70" text-anchor="middle">(source)</text>
<line class="vx-line" x1="485" y1="80" x2="485" y2="112"/>
<polygon class="vx-arrowhead" points="480,112 485,120 490,112"/>
<rect class="vx-box-strong" x="400" y="122" width="170" height="46" rx="4"/>
<text class="vx-text" x="485" y="150" text-anchor="middle">SPIR-V (binary IR)</text>
<line class="vx-line" x1="485" y1="168" x2="485" y2="200"/>
<polygon class="vx-arrowhead" points="480,200 485,208 490,200"/>
<rect class="vx-box" x="400" y="210" width="170" height="46" rx="4" fill="none" stroke-dasharray="4 3"/>
<text class="vx-text-muted" x="485" y="238" text-anchor="middle">vendor's own driver ISA</text>
</g>

<g style="--vx-i: 3; --vx-n: 4">
<text class="vx-text-accent" x="675" y="24" text-anchor="middle">Apple</text>
<rect class="vx-box-strong" x="590" y="34" width="170" height="46" rx="4"/>
<text class="vx-text" x="675" y="62" text-anchor="middle">MSL (source)</text>
<line class="vx-line" x1="675" y1="80" x2="675" y2="112"/>
<polygon class="vx-arrowhead" points="670,112 675,120 680,112"/>
<rect class="vx-box" x="590" y="122" width="170" height="46" rx="4" fill="none" stroke-dasharray="4 3"/>
<text class="vx-text-muted" x="675" y="150" text-anchor="middle">AIR / .ir (undocumented)</text>
<line class="vx-line" x1="675" y1="168" x2="675" y2="200"/>
<polygon class="vx-arrowhead" points="670,200 675,208 680,200"/>
<rect class="vx-box" x="590" y="210" width="170" height="46" rx="4" fill="none" stroke-dasharray="4 3"/>
<text class="vx-text-muted" x="675" y="238" text-anchor="middle">metallib / GPU ISA</text>
</g>

<rect class="vx-box-strong" x="20" y="300" width="26" height="16" rx="2"/>
<text class="vx-text" x="54" y="313">documented: the vendor publishes a written specification for this rung</text>
<rect class="vx-box" x="20" y="330" width="26" height="16" rx="2" fill="none" stroke-dasharray="4 3"/>
<text class="vx-text" x="54" y="343">not specified in prose: readable only by disassembling a build's output, if at all</text>
</svg>
<figcaption>Every vendor's ladder has the same three rungs, source, an intermediate form, and the real instructions, but each vendor stops documenting at a different height. NVIDIA and Khronos both add one documented rung above the opaque floor; AMD skips the middle rung and documents the floor directly; Apple documents only the top.</figcaption>
</figure>

The pattern is not about how many rungs exist, every vendor here has three. It is about which single rung, in each column, is the last one a compiler author outside that vendor can target with a written contract instead of a guess checked against whatever a particular build happens to accept.

## What this means for choosing where Vortex lands

Nothing in this chapter picks a target for Vortex; [M12](../mlir/m12-vortex-gpu-path.md) is where that choice gets made, once [G9](g9-gpu-compilers-in-llvm.md) has covered the back-end problems (kernel outlining, uniformity, convergence) that sit between any of these IRs and a working kernel. What this chapter does fix is a fact the choice has to respect: whichever rung Vortex eventually emits, the rules [G7](g7-programming-models.md#what-none-of-this-changes) already named do not loosen because one target's documentation runs deeper than another's. [Decision 56](../decisions/numbers.md#d56) forbids reassociating or fusing the multiply and the add in the [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#comparing-with-a-known-answer) kernel's `sum += a[row, k] * b[k, column]` on any target; [decision 43](../decisions/arrays.md#d43) fixes that kernel's array shapes at compile time on any target. This chapter's own two lowerings already show that survival happening one level up: the NVVM and SPIR-V versions of the clamp kernel both keep its single `arith.cmpf ogt` and its plain load and store exactly as written, because nothing in `--convert-gpu-to-nvvm` or `--convert-gpu-to-spirv` touches arithmetic, only control flow and memory addressing. A pass that only restructures control flow cannot, by itself, break a floating-point rule; the rule has to be enforced at the step, wherever it sits, that is allowed to touch arithmetic at all, once, before any of these lowerings run.

One more detail in the NVVM output is worth noticing for what it is not: the two memref arguments arrive as a ten-field struct of pointers, an offset and per-dimension sizes, not as two bare pointers. That shape is MLIR's own convention for describing a memref's layout to LLVM, not a rule PTX, SASS or any GPU vendor imposes; a hand-written NVVM kernel could take two plain pointers instead. It is an ABI decision, made one layer above any of this chapter's ISAs, of exactly the kind [A4](../backend/a4-calling-conventions.md) examines for CPU targets: how a value crosses a function boundary is a question a compiler answers once, independent of what instructions the function body ends up using.

## For Vortex

!!! vortex "Exercise"

    **Do**, on paper or in a short note, not in Vortex: pick one vendor family this chapter did not compile locally, NVIDIA (PTX and SASS) or AMD (the AMDGPU ISA), and find one real, disassembled or documented example of it: a PTX listing from Compiler Explorer's `nvcc133` target for any small kernel, or a short excerpt from AMD's published ISA manual for one instruction class.

    1. In that example, find the equivalent of three things this chapter named for NVIDIA's rung: an instruction that reads through a global or generic pointer, one that touches the group-local scratchpad, and the barrier instruction. Write down their mnemonics.
    2. Compare them to `relu_nvvm.mlir`'s `nvvm.read.ptx.sreg.tid.x` and `relu_spirv.mlir`'s `spirv.mlir.addressof @__builtin__LocalInvocationId__`, this chapter's two working examples: both read a thread's position, in a different vocabulary, for a different rung of a different ladder. State, in one sentence each, which rung each of your three mnemonics belongs to (source, virtual ISA, real ISA, or an MLIR dialect standing in for one).
    3. Write one paragraph naming which single rung, for your chosen vendor family, a Vortex back end would have to emit to reach it: not by deciding Vortex's target, but by stating the fact this chapter established, that only one or two rungs of that family's ladder carry a written specification at all.

    **Not yet:** writing any part of a Vortex back end, choosing which vendor family Vortex targets (that decision belongs to [M12](../mlir/m12-vortex-gpu-path.md)), and the back-end machinery, kernel outlining, uniformity, convergence, that turns a chosen IR into a correct lowering ([G9](g9-gpu-compilers-in-llvm.md)).

    **Proof that it works:** your three mnemonics are real, found in a source this chapter's table lists or a Compiler Explorer run you can point to, not invented; your paragraph names a specific rung by the vocabulary this chapter used (virtual ISA, real ISA, binary IR, source language) rather than a vague "somewhere in the middle"; and it explains, in the terms [decision 56](../decisions/numbers.md#d56) and [decision 43](../decisions/arrays.md#d43) already fixed, why the choice of rung does not change what the [stage 10](../compiler/guide/stage-10-matrix-multiplication.md) kernel is allowed to compute.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is a virtual instruction set, and which vendor in this chapter publishes one as a full specification?** An ISA-shaped format no chip executes directly, translated to the real ISA by a further step (a driver, usually). NVIDIA's PTX is the one this chapter's vendors document in full.
    - **Why does AMD not need a virtual ISA the way NVIDIA does?** Because AMD publishes the real, per-generation AMDGPU ISA directly; there is no separate stable format sitting above it for a driver to finish translating.
    - **What must every SPIR-V module do to a conditional branch that an ordinary LLVM IR function is never required to do?** Wrap it in an explicit structured region (`spirv.mlir.selection`, ending in `spirv.mlir.merge`) that names the block where both sides reconverge.
    - **How many rungs of its own stack does Apple document, from MSL source down to the GPU's own instructions?** One: MSL text. Both the offline `.ir` intermediate and the running `.metallib` are undocumented.
    - **In this chapter's own two lowerings of the same `scf.if`, which one left the branch as a plain pair of basic blocks, and which wrapped it in an explicit merge region?** NVVM: a plain pair of blocks joined by an ordinary branch. SPIR-V: an explicit `spirv.mlir.selection`/`spirv.mlir.merge` region, required by the target's own specification.
    - **Which two Vortex rules must survive the trip no matter which of this chapter's representations a Vortex compiler eventually targets?** [Decision 56](../decisions/numbers.md#d56)'s ban on reassociating or fusing floating-point operations, and [decision 43](../decisions/arrays.md#d43)'s fixed, compile-time array shape.

## Where this comes back

!!! next "You will use this again in"

    - [G9. GPU compilers inside LLVM](g9-gpu-compilers-in-llvm.md): *kernel outlining*, *uniformity*, *convergent operations*, *StructurizeCFG*
    - [G10. The GPU matmul ladder](g10-matmul-ladder.md): *PTX*, *SASS*, *MSL*, measured across a real kernel's rungs
    - [G13. Tile languages](g13-tile-languages.md): which of this chapter's IRs a tile compiler targets and why
    - [M10. MLIR for GPUs](../mlir/m10-mlir-for-gpus.md): the `nvvm`, `rocdl` and `spirv` dialects, taken past a parse-and-verify check
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): choosing which of this chapter's rungs Vortex actually emits

## Sources and further reading

Read the PTX ISA's introduction first, for the clearest short statement of what a virtual ISA is and why one exists; then AMD's ISA documentation index, to see the alternative of publishing the real thing directly; then the SPIR-V specification's structured-control-flow section, short and precise; then Apple's "Building a shader library by precompiling source files" page, for what it does and does not say about the format in between.

[^ptx-isa]: NVIDIA, "Parallel Thread Execution ISA", version 9.4. <https://docs.nvidia.com/cuda/parallel-thread-execution/index.html>
[^cuda-binutils]: NVIDIA, "CUDA Binary Utilities": `cuobjdump`, `nvdisasm`, and per-architecture SASS opcode tables. <https://docs.nvidia.com/cuda/cuda-binary-utilities/index.html>
[^nvptx]: LLVM Project, "User Guide for NVPTX Back-end". <https://llvm.org/docs/NVPTXUsage.html>
[^amd-isa]: AMD, "AMD ISA documentation" and "Machine-readable ISA". <https://gpuopen.com/amd-isa-documentation/> ; <https://gpuopen.com/machine-readable-isa/>
[^amdgpu-backend]: LLVM Project, "User Guide for AMDGPU Backend". <https://llvm.org/docs/AMDGPUUsage.html>
[^spirv-spec]: Khronos Group, "SPIR-V Specification", unified1. <https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html>
[^spirv-scf]: Khronos Group, "SPIR-V Specification", unified1, section "Structured Control Flow". <https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#StructuredControlFlow>
[^metal-precompile]: Apple, "Building a shader library by precompiling source files". <https://developer.apple.com/documentation/metal/building-a-shader-library-by-precompiling-source-files>
[^metal-source]: Apple, "`makeLibrary(source:options:)`". <https://developer.apple.com/documentation/metal/mtldevice/makelibrary(source:options:)>
[^msl-spec]: Apple, "Metal Shading Language Specification", version 4.1. <https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf>
[^metal-converter]: Apple, "Metal shader converter". <https://developer.apple.com/metal/shader-converter/>
[^feature-tables]: Apple, "Metal Feature Set Tables". <https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf>
