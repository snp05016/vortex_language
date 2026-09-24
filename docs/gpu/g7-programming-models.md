# G7. Programming models tour

<p class="page-intro">CUDA, HIP, OpenCL, SYCL, Vulkan compute, WebGPU and Metal all launch the same shape of computation under different names. This chapter reads one calculation through six of them, builds the vocabulary table you will use for the rest of the GPU book, and asks what a compiler, rather than a programmer, needs from any of them.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 30 minutes · Builds on: [G2. The SIMT execution model](g2-simt.md)</p>

???+ remember "Before you start, remember"

    ??? question "Which threads of a 32 × 32 thread block form one warp?"

        Thirty-two threads with the same `y` and consecutive `x`: one row of the block. Threads are numbered with `x` varying fastest, and each run of 32 consecutive threads is a warp, which executes one instruction for all of its threads together.

        Introduced in [G2. The SIMT execution model](g2-simt.md).

    ??? question "What is shared memory, and when may a thread read a value that another thread stored there?"

        A small on-chip scratchpad that every thread of one thread block can read and write, much faster than global memory. A thread may read another thread's value only after both have passed a barrier that orders the store before the load.

        Introduced in [G3. The GPU memory hierarchy](g3-memory-hierarchy.md).

    ??? question "In what order are the elements of a `[f32; 64, 64]` array stored?"

        Row after row, the last index varying fastest: `m[i, j + 1]` sits next to `m[i, j]`, and `m[i + 1, j]` is a row, 256 bytes, away.

        Introduced in [Arrays and shapes, decision 43](../decisions/arrays.md#d43).

    ??? question "May a Vortex compiler regroup the additions in `sum += a[row, k] * b[k, column]`?"

        No. Each floating-point operation is one IEEE 754 operation, rounded once, and must not be contracted, reassociated or reordered.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Read one kernel across CUDA, HIP, OpenCL, SYCL, Vulkan compute, WebGPU and Metal, and name what stays the same underneath every spelling.
    - Translate between each model's vocabulary for a thread, a group of threads, the group's on-chip memory and the whole launch.
    - Explain why some models put device code in the same file as host code and others keep the two apart, and what that choice costs a compiler.
    - Place each model on the map of IRs that G8 and G9 cover next.
    - State which parts of a kernel a Vortex compiler may choose freely, and which parts the language's own rules fix no matter which host API it eventually targets.

## One calculation, six texts

Take the smallest kernel worth writing: SAXPY, `y = a*x + y`, applied elementwise to two vectors. Every model in this chapter can express it, and every one of them writes a different-looking program for the same fifteen or so floating-point operations.

In CUDA, the kernel and the code that launches it live in the same `.cu` file. The kernel is an ordinary-looking function marked `__global__`; the launch is a function call with an extra `<<<...>>>` between the name and the parentheses, read by `nvcc` and nobody else.

--8<-- "includes/examples/gpu/g7-programming-models/saxpy.cu.md"

HIP's version of this file has the same shape: a `__global__` kernel, the same `<<<...>>>` launch syntax, and `hip*` functions where CUDA has `cuda*` ones. HIP's own documentation describes itself as a C++ runtime API and kernel language that lets a single source file build for AMD GPUs (through ROCm) or, unmodified, for NVIDIA ones (through CUDA), which is why it reads as CUDA with the vendor prefix changed.[^hip-docs]

Metal keeps the two sides apart. A `.metal` file holds only the kernel, written against Apple's own C++-based Metal Shading Language; there is no launch syntax inside it, because launching is a host-side job done from Swift, Objective-C or C++ (through Apple's header-only `metal-cpp`), several function calls away from the kernel text.[^metal-calc][^metal-cpp]

--8<-- "includes/examples/gpu/g7-programming-models/saxpy_msl.metal.md"

OpenCL keeps the same separation and adds a step: a host program builds a `program` object from kernel source text (or a precompiled binary) at run time, asks it for a named `kernel`, sets each argument individually, and enqueues the work on a command queue. SYCL, a later Khronos standard, puts the host and device code back in one C++ file, written in standard C++ with lambdas and no special syntax, and still compiles to the same execution model underneath, which its specification calls "single-source" programming.[^sycl-reg] Vulkan's compute shaders are written in GLSL or HLSL, compiled ahead of time to SPIR-V, a binary instruction set that Vulkan loads and runs; nothing about it depends on a specific vendor's hardware.[^spirv-spec] WebGPU goes further than any of the others: its shading language, WGSL, is the only language in this chapter with no C or C++ ancestry at all, and the host code that builds a pipeline from WGSL text and dispatches it can be written in JavaScript, Rust or several other languages that have nothing to do with the device language.[^webgpu][^wgsl]

Six texts, six sets of keywords, and (mostly) the same arithmetic. The rest of this chapter is about what does not change.

## The shape every model shares

Underneath the syntax, every model in this chapter launches the same three-level shape: a large **grid** of equally sized **groups**, each holding a fixed number of **threads**. Every thread runs the same kernel function on its own data, found through its coordinates in the grid; threads in the same group can hold values in a small, fast **scratchpad memory** that other groups cannot see, and can wait for each other at a **barrier** before reading what a groupmate wrote. The MLIR project's `gpu` dialect names this shape directly, independent of any vendor's syntax, with a `gpu.module` holding kernel functions and built-in operations for a thread's position within its block and a block's position within the grid.[^gpu-dialect] The fourth example writes the SAXPY kernel a third way, in that dialect, and `mlir-opt` parses and verifies it without picking a vendor.

--8<-- "includes/examples/gpu/g7-programming-models/grid_of_groups.mlir.md"

<figure class="vx-figure">
<svg viewBox="0 0 760 400" role="img" aria-labelledby="g7-f1-title g7-f1-desc">
<title id="g7-f1-title">The shape every programming model launches, under whatever names it uses</title>
<desc id="g7-f1-desc">A host issues a launch that starts a grid on the device. The grid holds several groups, arranged in a row. One group is expanded to show that it holds several threads, each running the same kernel function, plus one shared box of scratchpad memory that every thread in the group can read and write, and a barrier that threads pass through together before reading what a groupmate stored.</desc>
<rect class="vx-box-strong" x="20" y="30" width="120" height="50" rx="4"/>
<text class="vx-text" x="80" y="60" text-anchor="middle">Host</text>
<line class="vx-flow" x1="140" y1="55" x2="185" y2="55"/>
<polygon class="vx-arrowhead" points="185,50 193,55 185,60"/>
<text class="vx-text-muted" x="163" y="42" text-anchor="middle">launch</text>
<rect class="vx-box" x="195" y="20" width="545" height="70" rx="4"/>
<text class="vx-text-muted" x="215" y="40">device: one grid</text>
<rect class="vx-box-accent" x="215" y="50" width="95" height="30" rx="3"/>
<text class="vx-text" x="262" y="70" text-anchor="middle">group</text>
<rect class="vx-box-accent" x="320" y="50" width="95" height="30" rx="3"/>
<text class="vx-text" x="367" y="70" text-anchor="middle">group</text>
<rect class="vx-box-accent" x="425" y="50" width="95" height="30" rx="3"/>
<text class="vx-text" x="472" y="70" text-anchor="middle">group</text>
<text class="vx-text-muted" x="580" y="70">...</text>
<line class="vx-line" x1="262" y1="80" x2="262" y2="120"/>
<polygon class="vx-arrowhead" points="257,120 262,128 267,120"/>
<rect class="vx-box-strong" x="60" y="130" width="640" height="250" rx="4"/>
<text class="vx-text-muted" x="80" y="152">one group, expanded</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 4">
<rect class="vx-box" x="80" y="170" width="80" height="60" rx="3"/>
<text class="vx-text" x="120" y="196" text-anchor="middle">thread</text>
<text class="vx-text-muted" x="120" y="214" text-anchor="middle">runs kernel</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4">
<rect class="vx-box" x="180" y="170" width="80" height="60" rx="3"/>
<text class="vx-text" x="220" y="196" text-anchor="middle">thread</text>
<text class="vx-text-muted" x="220" y="214" text-anchor="middle">runs kernel</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4">
<rect class="vx-box" x="280" y="170" width="80" height="60" rx="3"/>
<text class="vx-text" x="320" y="196" text-anchor="middle">thread</text>
<text class="vx-text-muted" x="320" y="214" text-anchor="middle">runs kernel</text>
</g>
<text class="vx-text-muted" x="410" y="200" text-anchor="middle">...</text>
<line class="vx-line" x1="120" y1="230" x2="120" y2="260"/>
<line class="vx-line" x1="220" y1="230" x2="220" y2="260"/>
<line class="vx-line" x1="320" y1="230" x2="320" y2="260"/>
<rect class="vx-box-accent" x="80" y="260" width="280" height="40" rx="3"/>
<text class="vx-text" x="220" y="284" text-anchor="middle">scratchpad memory (shared by this group only)</text>
<line class="vx-line" x1="120" y1="300" x2="120" y2="330"/>
<line class="vx-line" x1="220" y1="300" x2="220" y2="330"/>
<line class="vx-line" x1="320" y1="300" x2="320" y2="330"/>
<rect class="vx-box" x="80" y="330" width="280" height="34" rx="3" stroke-dasharray="4 3"/>
<text class="vx-text-muted" x="220" y="352" text-anchor="middle">barrier: every thread waits here before reading a groupmate's write</text>
<text class="vx-text-muted" x="450" y="185">CUDA/HIP: thread, block, __shared__, __syncthreads()</text>
<text class="vx-text-muted" x="450" y="205">OpenCL/SYCL: work-item, work-group, local memory, barrier()</text>
<text class="vx-text-muted" x="450" y="225">Vulkan/WGSL: invocation, workgroup, workgroup memory, barrier</text>
<text class="vx-text-muted" x="450" y="245">Metal: thread, threadgroup, threadgroup memory, threadgroup_barrier</text>
</svg>
<figcaption>Figure 1. Every model in this chapter launches this same three-level shape from the host: a grid of groups of threads, with scratchpad memory and a barrier shared inside each group. The names on the right are what six models call the four boxed ideas on the left.</figcaption>
</figure>

## Reading your own coordinates

A kernel function takes no argument that says which element it owns. Instead, every model gives it a way to ask the hardware "where am I in the grid", and every model answers with the same two-level coordinate: a group index and a position inside the group. `thread_grid_model.cpp`, the first example, has no GPU or vendor API in it at all: it is the arithmetic every one of these built-ins computes, given a total thread count split into equally sized groups.

--8<-- "includes/examples/gpu/g7-programming-models/thread_grid_model.cpp.md"

Reading a coordinate back from a global id and reconstructing the global id from a coordinate are exact inverses, because the split is a fixed group size chosen when the kernel launches, not something the kernel discovers at run time. CUDA and HIP hand a thread its coordinates in two built-in structs, `blockIdx` and `threadIdx`, each with up to three components; a one-dimensional global id is `blockIdx.x * blockDim.x + threadIdx.x`, exactly the `coord_of` and `global_id_of` functions of the example run in reverse. OpenCL and SYCL wrap the same numbers behind functions, `get_global_id`/`get_group_id`/`get_local_id` in OpenCL and the `item` and `nd_item` classes in SYCL.[^sycl-reg] Vulkan's shaders and WGSL read theirs from **built-in variables** attached to the entry point, such as GLSL's `gl_GlobalInvocationID` or WGSL's `@builtin(global_invocation_id)`.[^wgsl] Metal attaches the same information as an ordinary function parameter carrying a `[[thread_position_in_grid]]` attribute, as `saxpy.metal` shows above.

The table below is this chapter's Rosetta stone: the same four ideas from Figure 1, in the words each model's own specification uses for them.

| Idea | CUDA / HIP | OpenCL | SYCL | Vulkan (SPIR-V) | WebGPU (WGSL) | Metal |
| --- | --- | --- | --- | --- | --- | --- |
| one thread of execution | thread | work-item[^opencl-terms] | work-item[^sycl-reg] | invocation | invocation[^wgsl] | thread |
| a group that shares memory and a barrier | (thread) block | work-group[^opencl-terms] | work-group[^sycl-reg] | workgroup | workgroup[^wgsl] | threadgroup |
| the group's on-chip scratchpad | shared memory / LDS[^amd-lds] | local memory[^opencl-terms] | local memory[^sycl-reg] | workgroup memory[^glsl-shared] | workgroup address space[^wgsl] | threadgroup memory[^msl-spec] |
| the whole launch | grid | NDRange[^opencl-terms] | `nd_range`[^sycl-reg] | dispatch (workgroup count) | dispatch[^wgsl] | grid |

??? check "A kernel author writes `get_local_id(0)` in one file and `[[thread_position_in_threadgroup]]` in another. Are these two different ideas, or the same idea in two models?"

    The same idea: a thread's position inside its own group, counted from zero. The first spelling is OpenCL's; the second is Metal's. Neither tells the thread its position in the whole grid, which is a separate built-in in both models (`get_global_id` and `[[thread_position_in_grid]]`).

Not every model names every row. Some Khronos APIs also expose a **subgroup**, a hardware-scheduled group smaller than a work-group whose members can share values without going through the scratchpad or a barrier at all; SYCL's specification defines a `sub_group` class for it.[^sycl-reg] CUDA and HIP call the same kind of group a **warp** or **wavefront**, and Metal calls it a **SIMD-group**; G2 introduced why hardware groups threads this way, and G6 covers what a program can do with the grouping once it has a name for it. WGSL, at the revision this chapter cites, defines invocations and workgroups but no single collective term for every invocation in a dispatch: the specification talks about "all invocations in the shader stage" rather than naming the whole launch, which is why the table above lists "dispatch" for that row as what a program *does*, not a term the specification defines for what results.[^wgsl]

## Same source, or two?

The six models split into two families by one design choice: does host code and device code live in the same file, compiled by the same call to one compiler?

CUDA, HIP and SYCL answer yes. Nvidia's guide calls this compiling "the entire source file", host and device parts together, and generating a fat binary that carries device code for possibly several architectures alongside ordinary host object code.[^cuda-pg] SYCL keeps the same single-source promise using plain, standard C++, with kernels written as lambdas or function objects rather than a separate language.[^sycl-reg] The gain is that the compiler sees both sides at once: it can check that a value captured into a kernel has a type the device supports, and a programmer writes one file, not two kept in sync by hand.

OpenCL, Vulkan and Metal answer no. Device code is a separate text (or a separate precompiled binary) that the host loads, builds, and only then can launch. Apple's Metal documentation shows this in the API itself: a host program asks a `MTLDevice` to `makeLibrary(source:options:)`, gets back a compiled library object, then asks that library for the named function before it can build a pipeline and dispatch anything.[^metal-source] Precompiling that same source ahead of time, with an offline `.metal -> .ir -> .metallib` build, is also documented and avoids paying the compile cost at every launch.[^metal-precompile] The gain is decoupling: the device compiler, the device binary format, and even the device vendor can change without touching the host program's source, which is exactly what lets Vulkan's SPIR-V run unmodified on GPUs from more than one vendor.[^spirv-spec]

WebGPU sits at the far end of the second family. Its host language is not fixed at all: WGSL text is handed to whatever host language's WebGPU binding is in use, at run time, the same way Metal's `makeLibrary(source:)` builds a library from text without needing a system compiler installed ahead of time.[^webgpu] A build tool exists precisely because SPIR-V and MSL are different binary and text formats behind the same idea: SPIRV-Cross translates a SPIR-V module into GLSL, HLSL or MSL source, and Apple's own MoltenVK uses it to run Vulkan applications, unmodified, as Metal calls.[^spirv-cross][^moltenvk] A device language is a text or binary format with a compiler behind it; which compiler, and when it runs, is a separable decision from what the kernel says.

??? check "SYCL and OpenCL both descend from the same Khronos lineage and share most of their vocabulary (work-item, work-group, NDRange). What is the one design choice that puts them in different families in this chapter's grouping?"

    Whether host and device code live in one source file compiled together. SYCL is single-source, standard C++; OpenCL keeps host code and device kernel source (or a precompiled binary) separate, built at run time through an explicit program-and-kernel API.

## Underneath: a handful of IRs

However a kernel's text is written, a compiler still has to turn it into something a GPU executes, and there are far fewer of *those* than there are source languages. NVIDIA's `nvcc` translates CUDA (and, through the same LLVM-based front end, HIP source built for NVIDIA) down to PTX, a virtual instruction set that the driver finishes translating to the actual machine ISA at load time; LLVM's own NVPTX back end targets the same PTX.[^nvptx] AMD's HIP path and OpenCL implementations that target AMD hardware go through LLVM's AMDGPU back end to AMD's published ISAs.[^amdgpu-backend] OpenCL, Vulkan and newer SYCL back ends can all produce SPIR-V, the one binary IR in this chapter with a public specification that more than one vendor implements, including LLVM's own SPIR-V back end.[^spirv-backend] Apple's toolchain is the outlier: its documentation describes building a `.metallib` from MSL source through an intermediate `.ir` file, but does not publish the format of that intermediate representation, so MSL text is the lowest level a programmer, or a compiler author outside Apple, can target directly.[^metal-precompile]

This is also where a compiler-neutral path exists: MLIR's `gpu` dialect (used above for the SAXPY kernel) is not a fourth vendor IR competing with PTX, AMDGPU or SPIR-V, but a layer that sits above them and lowers toward NVIDIA's `nvvm` dialect, AMD's `rocdl` dialect, or a `spirv` dialect for Vulkan, from one representation of "grid of groups of threads" written once.[^gpu-dialect] The IREE project builds exactly this kind of multi-target compiler, and for Apple GPUs it takes the route this section already named: it embeds MSL source text and compiles it at run time through the same documented API `saxpy.metal`'s host side would use, because that is the only entry point Apple documents.[^iree-metal] G8 opens the vendor ISAs this section only named (PTX, AMDGPU, SPIR-V) and reads real disassembly; G9 covers what LLVM's back ends do with the parts of this shape that are hardest to get right, such as a warp that takes both sides of a branch; M10 returns to the `gpu` dialect itself and takes its lowerings further than a parse-and-verify check.

One model this chapter has not mentioned belongs to a different generation entirely. Apple deprecated OpenCL on its own platforms starting with macOS 10.14, in favor of Metal, and its developer page for OpenCL says so directly.[^opencl-deprecated] A working GPU story on the owner's machine runs through Metal or through MLIR's local passes (both checked in this chapter's examples), not through OpenCL. NVIDIA's own toolkit has not shipped for macOS since CUDA 10.2, so CUDA and HIP kernels in this book are written and read, not built, on this machine; they still compile-check as ordinary CUDA source wherever `nvcc` is installed.[^cuda-macos]

## What none of this changes

Whichever model a kernel is written against, the arithmetic it performs is the programmer's to get right, and the language's own rules about that arithmetic do not bend for any host API. [Decision 56](../decisions/numbers.md#d56) already fixed how Vortex's floating-point operations round, in [G4](g4-memory-performance.md#what-does-not-change-the-bits)'s words: no contraction, no reassociation, no reordering. That rule is about what one `f32` or `f64` operation means in Vortex; it says nothing about CUDA, HIP, OpenCL, SYCL, Vulkan, WGSL or Metal, because none of those languages is Vortex. A Vortex compiler that lowers a `+` to any of these models' own multiply-add or fast-math intrinsics has changed what the Vortex program means, independent of which model it targeted; the check belongs at the lowering step, once, not once per back end.

The same is true for aliasing. [Decision 25](../decisions/references.md#d25) forbids the variable lent as `&mut` in a Vortex call from appearing in any other argument of that call, which is exactly the promise every kernel in this chapter's underlying model needs to run many threads over the same buffer safely: nothing one thread's write touches is something another argument might also read through a different name. A Vortex compiler that eventually emits CUDA, Metal or WGSL is handing each device kernel that same promise for free, already checked, in a form none of these host languages checks on their own; C and C++-based device languages ask the *programmer* to promise no aliasing (CUDA's `__restrict__`, for example), where Vortex's rule makes the promise part of the language.

Shape is the third constant. Every access this chapter's coordinate arithmetic touches assumes a fixed group size and a fixed grid size, known before the kernel starts. [Decision 43](../decisions/arrays.md#d43) fixes a Vortex array's shape and storage order the same way: known at compile time, not discovered by walking a length field at run time. That is precisely the fact a thread mapping needs, as [G4](g4-memory-performance.md) used it to compute strides directly from a Vortex array's shape rather than from a pointer whose stride is anyone's guess.

## For Vortex

!!! vortex "Exercise"

    **Build** a small, host-API-neutral description of a kernel launch inside your compiler: not a back end, and not code that emits CUDA, Metal or any other model's text, but a data structure and a report that could feed one later.

    1. A launch shape: grid size and group size, each as a small fixed-size tuple (one, two or three components), computed from your compiler's IR for a parallelizable loop nest with constant bounds.
    2. A coordinate function, in the style of `thread_grid_model.cpp`'s `coord_of`, that maps a global thread id to a (group, local) pair for that shape, and its inverse. Test that they round-trip for every id in a few shapes.
    3. A vocabulary table like this chapter's, but for the two or three host APIs you consider plausible Vortex targets: for each, what a thread, a group, the group's scratchpad and the whole launch are called, and whether that API is single-source or separate-source. [M12](../mlir/m12-vortex-gpu-path.md) is where the real choice among them gets made; this exercise only prepares the vocabulary for that decision.
    4. A one-paragraph note, attached to your launch-shape data structure, stating which of this chapter's "what none of this changes" rules a lowering step must enforce no matter which host API it targets: no reassociated or contracted floating-point arithmetic, no aliasing between a `&mut` output and any other argument, and a shape fixed before the kernel starts.

    **Not yet:** emitting any device source (CUDA, Metal, WGSL or otherwise), choosing a thread mapping for a real kernel ([G4](g4-memory-performance.md) already built that report; this exercise's coordinate function is simpler and unopinionated about which loop variable maps to lanes), synchronization and barriers ([G6](g6-synchronization.md)), and picking an IR to lower to ([G8](g8-isas-and-irs.md), [G9](g9-gpu-compilers-in-llvm.md), [M12](../mlir/m12-vortex-gpu-path.md)).

    **Proof that it works:**

    - For the stage 10 matrix kernel's row/column loops at a small size, your launch shape reports a grid and group size, and the coordinate function's round-trip test passes for every global id in range.
    - Your vocabulary table has an entry for at least one single-source model and at least one separate-source model, and states the difference in one sentence each.
    - The floating-point and aliasing note names the exact Vortex decisions it is quoting (d56, d25, d43) rather than restating them from memory, so a later reader can check it against the source of truth.
    - A negative test: a loop nest whose bounds are not compile-time constants produces no launch shape and a clear reason, not a guess.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does every model in this chapter launch, whatever it calls the pieces?** A grid of equally sized groups of threads, each thread running the same kernel, with scratchpad memory and a barrier shared inside each group.
    - **What two numbers does a kernel read to find its own data, and how are they related to a global thread id?** A group index and a local index inside the group; the global id is `group * group_size + local`, and the pair is recovered from it by integer division and remainder.
    - **What is the one design choice that splits these models into two families?** Whether host and device code live in one file compiled together (CUDA, HIP, SYCL) or are built and connected separately at run time (OpenCL, Vulkan, WebGPU, Metal).
    - **Name the handful of IRs most of this chapter's source languages eventually reduce to.** PTX, an AMDGPU ISA, SPIR-V, and Apple's undocumented `.ir`; MLIR's `gpu` dialect is a compiler-neutral layer above them, not a fifth vendor IR.
    - **Which of this chapter's ideas does a Vortex compiler get to choose per target, and which does it not?** The host API, the launch shape's exact syntax, and the IR it lowers to are choices. Floating-point rounding, no-alias between `&mut` outputs and other arguments, and array shape being fixed before the kernel starts are Vortex's own rules and hold for every target.

## Where this comes back

!!! next "You will use this again in"

    - [G8. ISAs and IRs](g8-isas-and-irs.md): *PTX*, *AMDGPU ISA*, *SPIR-V*, *Apple's undocumented IR*
    - [G9. GPU compilers inside LLVM](g9-gpu-compilers-in-llvm.md): *kernel outlining*, *NVPTX and AMDGPU back ends*
    - [G11. Matrix units](g11-matrix-units.md): *SIMD-group*, *warp*, per-model matrix intrinsic naming
    - [G13. Tile languages](g13-tile-languages.md): *single-source compilation*, *thread mapping*
    - [M10. MLIR for GPUs](../mlir/m10-mlir-for-gpus.md): *the `gpu` dialect*, *lowering to `nvvm`/`rocdl`/`spirv`*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *host API choice*, *launch shape*, *single-source vs. separate-source*

## Sources and further reading

Read the CUDA Programming Guide's kernel-writing chapter first, then the Metal "Performing calculations on a GPU" sample, then skim the OpenCL, SYCL, Vulkan/SPIR-V and WebGPU/WGSL specifications for the same four terms this chapter's table lists.

[^hip-docs]: AMD, "HIP documentation", ROCm 7.15.0. <https://rocm.docs.amd.com/projects/HIP/en/latest/>
[^metal-calc]: Apple, "Performing calculations on a GPU". <https://developer.apple.com/documentation/metal/performing-calculations-on-a-gpu>
[^metal-cpp]: Apple, "metal-cpp". <https://developer.apple.com/metal/cpp/>
[^sycl-reg]: Khronos Group, "SYCL 2020 Specification, revision 12": sections defining single-source programming, `item`, `nd_item`, `nd_range` and `sub_group`. <https://registry.khronos.org/SYCL/specs/sycl-2020/html/sycl-2020.html>
[^spirv-spec]: Khronos Group, "SPIR-V Specification", unified1. <https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html>
[^glsl-shared]: Khronos Group, "Vulkan Registry": Vulkan compute shaders are written in a shading language (commonly GLSL) and compiled to SPIR-V; GLSL's `shared` qualifier is the compute shader's group-local memory. <https://registry.khronos.org/vulkan/>
[^webgpu]: W3C, "WebGPU". <https://www.w3.org/TR/webgpu/>
[^wgsl]: W3C, "WGSL": the definitions of invocation, workgroup and the workgroup address space, and the absence of a single collective term for a dispatch's invocations. <https://www.w3.org/TR/WGSL/>
[^gpu-dialect]: MLIR Project, "'gpu' Dialect": `gpu.module`, `gpu.func`, `gpu.block_id`, `gpu.thread_id`. <https://mlir.llvm.org/docs/Dialects/GPU/>
[^opencl-terms]: Khronos Group, "OpenCL API Specification", version 3.0, unified: the definitions of work-item, work-group, local memory and NDRange. <https://registry.khronos.org/OpenCL/specs/3.0-unified/html/OpenCL_API.html>
[^amd-lds]: AMD, "Hardware implementation", HIP 7.15.0 documentation, section "Local data share (LDS)". <https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html#local-data-share-lds>
[^msl-spec]: Apple, "Metal Shading Language Specification", version 4.1: threadgroup memory. <https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf>
[^cuda-pg]: NVIDIA, "CUDA Programming Guide", v13.4, section 2.3, "Writing SIMT Kernels": single-file compilation of host and device code. <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html>
[^metal-source]: Apple, "`makeLibrary(source:options:)`". <https://developer.apple.com/documentation/metal/mtldevice/makelibrary(source:options:)>
[^metal-precompile]: Apple, "Building a shader library by precompiling source files". <https://developer.apple.com/documentation/metal/building-a-shader-library-by-precompiling-source-files>
[^spirv-cross]: Khronos Group, "SPIRV-Cross". <https://github.com/KhronosGroup/SPIRV-Cross>
[^moltenvk]: Khronos Group, "MoltenVK". <https://github.com/KhronosGroup/MoltenVK>
[^nvptx]: LLVM Project, "User Guide for NVPTX Back-end". <https://llvm.org/docs/NVPTXUsage.html>
[^amdgpu-backend]: LLVM Project, "User Guide for AMDGPU Backend". <https://llvm.org/docs/AMDGPUUsage.html>
[^spirv-backend]: LLVM Project, "User Guide for SPIR-V Target". <https://llvm.org/docs/SPIRVUsage.html>
[^iree-metal]: IREE Project, "Metal HAL driver" design document: embedding and compiling MSL source at run time. <https://iree.dev/developers/design-docs/metal-hal-driver/>
[^opencl-deprecated]: Apple, "OpenCL": deprecated on Apple platforms starting with macOS 10.14, in favor of Metal. <https://developer.apple.com/opencl/>
[^cuda-macos]: NVIDIA, "CUDA 10.2 Release Notes": the last CUDA Toolkit release to support macOS. <https://docs.nvidia.com/cuda/archive/10.2/cuda-toolkit-release-notes/index.html>
