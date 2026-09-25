# G7. Programming models tour

<p class="page-intro">CUDA, HIP, OpenCL, SYCL, Vulkan compute, WebGPU and Metal all launch the same shape of computation. They differ in what they call its parts, in what a launch counts, in where the group size and the arguments are fixed, and in who compiles the kernel and when. This chapter reads one kernel through all seven, builds the vocabulary table the rest of the GPU book uses, and collects what a compiler that targets any of them must produce.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 40 minutes · Builds on: [G2. The SIMT execution model](g2-simt.md)</p>

???+ remember "Before you start, remember"

    ??? question "Which threads of a 32 × 32 thread block form one warp?"

        Thirty-two threads with the same `y` and consecutive `x`: one row of the block. Threads are numbered with `x` varying fastest, and each run of 32 consecutive threads is a warp, which executes one instruction for all of its threads together.

        Introduced in [G2. The SIMT execution model](g2-simt.md).

    ??? question "A kernel runs one thread per element of a 70-element array, in blocks of 32 threads. Why does its first line test the thread's index?"

        The launch must create whole blocks: 3 blocks, 96 threads, for 70 elements. The 26 threads with an index of 70 or more have no element, so a guard such as `if (idx < 70)` stops them from reading or writing past the end.

        Introduced in [G2. The SIMT execution model](g2-simt.md).

    ??? question "What is shared memory, and when may a thread read a value that another thread stored there?"

        A small on-chip scratchpad that every thread of one thread block can read and write, much faster than global memory. A thread may read another thread's value only after both have passed a barrier that orders the store before the load.

        Introduced in [G3. The GPU memory hierarchy](g3-memory-hierarchy.md).

    ??? question "May a Vortex compiler turn `a * x[i] + y[i]` into one fused multiply-add?"

        No. Each floating-point operation is one IEEE 754 operation, rounded once, and must not be contracted, reassociated or reordered.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Translate between each model's words for a thread, a subgroup, a group, the group's memory, its barrier and the whole launch.
    - Compute a launch in the units each model counts, groups or threads, and decide when the kernel needs a bounds guard.
    - Explain where each model fixes the group size and how arguments reach a kernel, and what a compiler must record about both.
    - Distinguish single-source from separate-source models by who compiles the kernel and when, and follow the outlining step that splits a single source.
    - Name the intermediate forms these models hand to a driver, and the compiler defaults that would break Vortex's floating-point rule.

## One kernel, seven texts

Take the smallest kernel worth writing, SAXPY: `y[i] = a * x[i] + y[i]` for every `i`. In Vortex it is a loop over a fixed-size array:

```vortex
// items: valid
fn saxpy(a: f32, x: &[f32; 1000], y: &mut [f32; 1000]) {
    for i in 0..1000 {
        y[i] = a * x[i] + y[i];
    }
}
```

A GPU version runs one thread per iteration. Every programming model in this chapter can say that, and each says it differently.

In CUDA, the kernel and the code that launches it live in one `.cu` file. The kernel is a function marked `__global__`; the launch is a call with an **execution configuration**, the number of blocks and the threads per block, written between `<<<` and `>>>`.[^cuda-intro] The kernel learns which element is its own from built-in variables, not from an argument.

--8<-- "includes/examples/gpu/g7-programming-models/saxpy.cu.md"

**HIP** is AMD's C++ runtime API and kernel language for its GPUs, part of the ROCm platform. Its kernels are written the same way, with `__global__`, `blockIdx` and `threadIdx`, and they launch with the same triple-chevron syntax or with a `hipLaunchKernelGGL` macro.[^hip-what][^hip-ext] AMD aims it at porting CUDA programs but warns that it is not a drop-in replacement: a port needs some manual work.[^hip-what] From here on, "CUDA" in this chapter covers HIP too, unless the text says otherwise.

Metal keeps the two sides apart. A `.metal` file holds only kernels, written in Apple's C++-based Metal Shading Language (MSL). There is no launch syntax in it. A host program in Swift, Objective-C or C++ gets the kernel from a library, builds a compute pipeline from it, binds the buffers and dispatches the work; for C++, Apple publishes metal-cpp, a header-only interface to the same API.[^metal-calc][^metal-cpp]

--8<-- "includes/examples/gpu/g7-programming-models/saxpy_msl.metal.md"

**OpenCL** also keeps kernels apart from the host program, and adds steps. The kernel is an OpenCL C function marked `__kernel` that reads its index with `get_global_id(0)`.[^opencl-c] The host creates a **program object** from kernel source text, from a precompiled binary, or from SPIR-V; builds it; asks it for a **kernel object** by name; sets each argument by position with `clSetKernelArg`; and enqueues the work with `clEnqueueNDRangeKernel`.[^opencl-api]

**SYCL**, a later Khronos standard, puts both sides back into one file of standard C++: the kernel is a lambda or function object passed to `parallel_for`, next to the host code that submits it. The specification calls this "single-source" programming and says it gives compilers the chance to analyze across the host-device boundary.[^sycl-intro]

**Vulkan** runs compute shaders, which Vulkan receives as **SPIR-V**, a binary intermediate language that Khronos defines for shaders and compute kernels.[^vk-module][^spirv-intro] Programmers write the shader in a shading language such as GLSL and compile it ahead of time; the SPIR-V specification's own example is a GLSL shader compiled by Khronos's Glslang front end.[^spirv-intro] The host creates a pipeline from the module, binds buffers and records a dispatch.

**WebGPU** takes its kernels as text in **WGSL**, the WebGPU Shading Language, handed to `createShaderModule` at run time. The host code can be JavaScript in a browser or any language with a WebGPU binding, and it launches with `dispatchWorkgroups`.[^webgpu]

Seven host APIs, and kernels written in C++ dialects (CUDA, HIP, MSL), standard C++ (SYCL), OpenCL C, GLSL and WGSL. The arithmetic is the same in all of them: one multiply and one add per element. The rest of this chapter separates what changes from what does not.

## The shape every model shares

Underneath the syntax, every model launches the same three-level shape (Figure 1). A launch creates a **grid** of equally sized **groups**, and each group holds a fixed number of **threads**. Every thread runs the same kernel function and finds its data through its coordinates. Threads in one group share a small on-chip **group memory**, which other groups cannot see, and can wait for each other at a **barrier** before reading what a groupmate wrote. Inside a group, the hardware schedules threads in fixed-size **subgroups**, the warps of [G2](g2-simt.md), each of which issues one instruction for all its members at once.

<figure class="vx-figure">
<svg viewBox="0 0 760 335" role="img" aria-label="A host box issues one launch to a device box holding a grid of equally sized groups. One group is expanded below: several threads that each run the same kernel, a bracket marking a subgroup of threads that the hardware schedules together, a box of group memory shared by the threads of this group only, and a dashed barrier line that every thread of the group reaches before any continues.">
<rect class="vx-box-strong" x="20" y="30" width="95" height="50" rx="4"/>
<text class="vx-text" x="67" y="60" text-anchor="middle">Host</text>
<line class="vx-flow" x1="115" y1="55" x2="167" y2="55"/>
<polygon class="vx-arrowhead" points="167,50 175,55 167,60"/>
<text class="vx-text-muted" x="145" y="44" text-anchor="middle">launch</text>
<rect class="vx-box" x="175" y="20" width="565" height="70" rx="4"/>
<text class="vx-text-muted" x="190" y="38">the grid: equally sized groups</text>
<rect class="vx-box-accent" x="190" y="48" width="100" height="30" rx="3"/>
<text class="vx-text" x="240" y="68" text-anchor="middle">group</text>
<rect class="vx-box-accent" x="305" y="48" width="100" height="30" rx="3"/>
<text class="vx-text" x="355" y="68" text-anchor="middle">group</text>
<rect class="vx-box-accent" x="420" y="48" width="100" height="30" rx="3"/>
<text class="vx-text" x="470" y="68" text-anchor="middle">group</text>
<rect class="vx-box-accent" x="535" y="48" width="100" height="30" rx="3"/>
<text class="vx-text" x="585" y="68" text-anchor="middle">group</text>
<text class="vx-text-muted" x="680" y="68" text-anchor="middle">...</text>
<line class="vx-line" x1="240" y1="78" x2="240" y2="108"/>
<polygon class="vx-arrowhead" points="235,108 240,116 245,108"/>
<rect class="vx-box-strong" x="175" y="118" width="565" height="205" rx="4"/>
<text class="vx-text-muted" x="190" y="138">one group, expanded</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 4">
<rect class="vx-box" x="195" y="150" width="100" height="46" rx="3"/>
<text class="vx-text" x="245" y="170" text-anchor="middle">thread</text>
<text class="vx-text-muted" x="245" y="187" text-anchor="middle">same kernel</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4">
<rect class="vx-box" x="310" y="150" width="100" height="46" rx="3"/>
<text class="vx-text" x="360" y="170" text-anchor="middle">thread</text>
<text class="vx-text-muted" x="360" y="187" text-anchor="middle">same kernel</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4">
<rect class="vx-box" x="425" y="150" width="100" height="46" rx="3"/>
<text class="vx-text" x="475" y="170" text-anchor="middle">thread</text>
<text class="vx-text-muted" x="475" y="187" text-anchor="middle">same kernel</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 4">
<rect class="vx-box" x="540" y="150" width="100" height="46" rx="3"/>
<text class="vx-text" x="590" y="170" text-anchor="middle">thread</text>
<text class="vx-text-muted" x="590" y="187" text-anchor="middle">same kernel</text>
</g>
<text class="vx-text-muted" x="690" y="177" text-anchor="middle">...</text>
<line class="vx-line" x1="195" y1="204" x2="410" y2="204"/>
<line class="vx-line" x1="195" y1="198" x2="195" y2="204"/>
<line class="vx-line" x1="410" y1="198" x2="410" y2="204"/>
<text class="vx-text-muted" x="302" y="219" text-anchor="middle">a subgroup: scheduled together</text>
<rect class="vx-box-accent" x="195" y="230" width="445" height="36" rx="3"/>
<text class="vx-text" x="417" y="253" text-anchor="middle">group memory: seen by this group only</text>
<line class="vx-line" x1="195" y1="288" x2="640" y2="288" stroke-dasharray="6 4"/>
<text class="vx-text-muted" x="417" y="308" text-anchor="middle">barrier: no thread continues until the whole group arrives</text>
<text class="vx-text-muted" x="20" y="140">Seven models,</text>
<text class="vx-text-muted" x="20" y="156">one shape;</text>
<text class="vx-text-muted" x="20" y="172">only the names</text>
<text class="vx-text-muted" x="20" y="188">change (see the</text>
<text class="vx-text-muted" x="20" y="204">table below).</text>
</svg>
<figcaption>Figure 1. The shape every model in this chapter launches. The host starts a grid of equally sized groups; each group holds threads that run the same kernel, scheduled in subgroups, and shares a group memory and a barrier that no other group can use.</figcaption>
</figure>

The table below is this chapter's Rosetta stone: the six ideas of Figure 1 in the words each model's own specification or guide uses.

| Idea | CUDA / HIP[^cuda-simt][^hip-ext] | OpenCL[^opencl-api][^opencl-c] | SYCL[^sycl-terms] | Vulkan (SPIR-V)[^spirv-terms][^vk-dispatch] | WebGPU (WGSL)[^wgsl-compute] | Metal[^msl-kernel] |
| --- | --- | --- | --- | --- | --- | --- |
| one thread | thread | work-item | work-item | invocation | invocation | thread |
| subgroup | warp (AMD: wavefront) | sub-group | sub-group | subgroup | subgroup | SIMD-group |
| group | thread block | work-group | work-group | workgroup | workgroup | threadgroup |
| group memory | shared memory (AMD: LDS[^amd-lds]) | local memory | local memory | `Workgroup` storage class | `workgroup` address space | threadgroup memory |
| group barrier | `__syncthreads()` | `work_group_barrier` | `group_barrier` | `OpControlBarrier` | `workgroupBarrier` | `threadgroup_barrier` |
| whole launch | grid | ND-range | nd-range | global workgroup | compute shader grid | grid |

Two entries deserve a word. OpenCL and SYCL call the group memory "local memory", while CUDA uses "local memory" for something else: per-thread storage that the compiler places in global memory, for example when registers run out.[^cuda-simt] The same words can name different things in two models, so a compiler's internal names should be its own. And Vulkan's name for the whole launch comes from its dispatch command, which assembles a "global workgroup" out of the requested count of local workgroups.[^vk-dispatch]

??? check "A kernel author writes `get_local_id(0)` in one file and `[[thread_position_in_threadgroup]]` in another. Are these two different ideas, or the same idea in two models?"

    The same idea: a thread's position inside its own group, counted from zero. The first spelling is OpenCL C's, the second MSL's. Neither gives the position in the whole launch, which both models provide separately (`get_global_id` and `[[thread_position_in_grid]]`).

## Reading your own coordinates

A kernel takes no argument that says which element it owns. Each model instead gives a thread its coordinates: which group it is in, where it sits inside that group, and, in most models, its position in the whole grid. Each coordinate has one, two or three components, named `x`, `y` and `z`.

- CUDA: `blockIdx` and `threadIdx`, with the sizes in `gridDim` and `blockDim`. There is no built-in global position; the kernel computes it, as in `blockIdx.x * blockDim.x + threadIdx.x`.[^cuda-intro]
- OpenCL C: the functions `get_group_id`, `get_local_id` and `get_global_id`, each taking a dimension number.[^opencl-c] SYCL wraps the same numbers in an `nd_item` object passed to the kernel.[^sycl-terms]
- SPIR-V: built-in variables `WorkgroupId`, `LocalInvocationId` and `GlobalInvocationId`.[^spirv-terms] WGSL: entry-point parameters marked `@builtin(workgroup_id)`, `@builtin(local_invocation_id)` and `@builtin(global_invocation_id)`.[^wgsl-compute]
- MSL: kernel parameters with the attributes `[[threadgroup_position_in_grid]]`, `[[thread_position_in_threadgroup]]` and `[[thread_position_in_grid]]`.[^msl-kernel]

The arithmetic behind all of them is the same, and WGSL and MSL write it out in their specifications. A thread's grid position is its group's position times the group size, plus its position in the group, in every dimension. Going the other way, the group position is the grid position divided by the group size, and the position in the group is the remainder.[^wgsl-compute][^msl-kernel]

Work one case by hand. The multiply kernel of [stage 10](../compiler/guide/stage-10-matrix-multiplication.md) computes `c[row, column]` for 64 × 64 matrices. Give it one thread per element, in groups of 16 × 16, with `x` running along `column`, because consecutive columns are neighbours in memory and consecutive `x` values share a warp ([G4](g4-memory-performance.md)). The grid is 4 × 4 groups. Which thread computes `c[37, 21]`?

1. Its grid position is `(x, y) = (21, 37)`: `x` is the column, `y` the row.
2. The group position is `(21 / 16, 37 / 16) = (1, 2)`, and the position inside the group is `(21 % 16, 37 % 16) = (5, 5)`.
3. Its index inside the group, counting `x` fastest, is `5 + 5 × 16 = 85`. WGSL calls this the local invocation index and MSL `thread_index_in_threadgroup`; both define it by this formula.[^wgsl-compute][^msl-kernel]
4. With 32-thread warps numbered by that index ([G2](g2-simt.md)), thread 85 is lane `85 % 32 = 21` of warp `85 / 32 = 2` in its group.

In CUDA this thread sees `blockIdx = (1, 2)` and `threadIdx = (5, 5)`. In WGSL it sees `workgroup_id = (1, 2, 0)`, `local_invocation_id = (5, 5, 0)` and `global_invocation_id = (21, 37, 0)`. In MSL it sees the same three pairs under the three attribute names. One thread, three spellings, the same numbers.

??? check "In the same launch, which group and which position in the group compute `c[50, 33]`, and what is the thread's index inside its group?"

    The grid position is `(33, 50)`. The group is `(33 / 16, 50 / 16) = (2, 3)` and the position inside it is `(33 % 16, 50 % 16) = (1, 2)`. The index inside the group is `1 + 2 × 16 = 33`, lane 1 of the group's warp 1.

## What a launch counts

The coordinates agree across models; the launch call does not. Some models ask the host for a number of groups, others for a number of threads, and the difference decides whether a kernel needs the guard from [G2](g2-simt.md).

- **Counting groups.** CUDA's execution configuration gives the number of blocks and the threads per block.[^cuda-intro] Vulkan's `vkCmdDispatch` takes a count of workgroups in each dimension,[^vk-dispatch] and so does WebGPU's `dispatchWorkgroups`.[^webgpu] Metal's `dispatchThreadgroups` takes a count of threadgroups and their size.[^metal-grid]
- **Counting threads.** OpenCL's `clEnqueueNDRangeKernel` takes the global number of work-items and, optionally, the work-group size.[^opencl-api] SYCL's `nd_range` holds a global range of work-items and a local range.[^sycl-terms] Metal's `dispatchThreads` takes the number of threads in the grid and the threadgroup size.[^metal-grid]

When the number of elements is a multiple of the group size, both kinds of launch create the same grid. When it is not, they part ways. A model that counts groups must round the count up, and the CUDA guide gives the usual formula, `(n + threads - 1) / threads`, an integer division that rounds up.[^cuda-bounds] For SAXPY over 1,000 elements in groups of 256, that is 4 groups and 1,024 threads, and the 24 threads past the end must do nothing, so the kernel carries the guard `if (i < n)`.[^cuda-bounds]

A model that counts threads can instead end the grid with a smaller group. OpenCL calls these **remainder work-groups**: when the global size is not divisible by the local size, the groups at the edge of the ND-range are smaller. It allows them only on devices and programs that support non-uniform work-groups, and otherwise requires the global size to be a multiple of the local size.[^opencl-api] Metal's `dispatchThreads` does the same on GPUs that support nonuniform threadgroup sizes, and Apple's guide says a kernel launched this way need not check its position against the grid.[^metal-grid] That is why `saxpy_msl.metal` has no guard.

SYCL counts threads but does not allow a remainder: a global range that the local range does not divide raises an `nd_range` error, so the host rounds up and the kernel needs the guard again.[^sycl-invoke]

<figure class="vx-figure">
<svg viewBox="0 0 760 290" role="img" aria-label="Ten elements launched in groups of four, two ways. Top row, a launch that counts groups: three full groups of four make twelve threads; threads 0 to 9 have elements and threads 10 and 11 are idle, so the kernel needs a guard. Bottom row, a launch that counts threads: ten threads in groups of four, four and two; every thread has an element and no guard is needed.">
<text class="vx-text" x="40" y="30">Counting groups: 3 groups × 4 = 12 threads for 10 elements</text>
<text class="vx-text-muted" x="40" y="48">CUDA, HIP, Vulkan, WebGPU, Metal dispatchThreadgroups; SYCL after rounding up</text>
<rect class="vx-box" x="40" y="58" width="194" height="60" rx="4"/>
<rect class="vx-box" x="254" y="58" width="194" height="60" rx="4"/>
<rect class="vx-box" x="468" y="58" width="194" height="60" rx="4"/>
<rect class="vx-box-accent" x="48" y="68" width="40" height="40" rx="3"/><text class="vx-mono" x="68" y="93" text-anchor="middle">0</text>
<rect class="vx-box-accent" x="94" y="68" width="40" height="40" rx="3"/><text class="vx-mono" x="114" y="93" text-anchor="middle">1</text>
<rect class="vx-box-accent" x="140" y="68" width="40" height="40" rx="3"/><text class="vx-mono" x="160" y="93" text-anchor="middle">2</text>
<rect class="vx-box-accent" x="186" y="68" width="40" height="40" rx="3"/><text class="vx-mono" x="206" y="93" text-anchor="middle">3</text>
<rect class="vx-box-accent" x="262" y="68" width="40" height="40" rx="3"/><text class="vx-mono" x="282" y="93" text-anchor="middle">4</text>
<rect class="vx-box-accent" x="308" y="68" width="40" height="40" rx="3"/><text class="vx-mono" x="328" y="93" text-anchor="middle">5</text>
<rect class="vx-box-accent" x="354" y="68" width="40" height="40" rx="3"/><text class="vx-mono" x="374" y="93" text-anchor="middle">6</text>
<rect class="vx-box-accent" x="400" y="68" width="40" height="40" rx="3"/><text class="vx-mono" x="420" y="93" text-anchor="middle">7</text>
<rect class="vx-box-accent" x="476" y="68" width="40" height="40" rx="3"/><text class="vx-mono" x="496" y="93" text-anchor="middle">8</text>
<rect class="vx-box-accent" x="522" y="68" width="40" height="40" rx="3"/><text class="vx-mono" x="542" y="93" text-anchor="middle">9</text>
<g class="vx-pulse">
<rect class="vx-box-bad" x="568" y="68" width="40" height="40" rx="3"/><text class="vx-mono" x="588" y="93" text-anchor="middle">10</text>
<rect class="vx-box-bad" x="614" y="68" width="40" height="40" rx="3"/><text class="vx-mono" x="634" y="93" text-anchor="middle">11</text>
</g>
<text class="vx-text-muted" x="672" y="84">idle:</text>
<text class="vx-text-muted" x="672" y="100">guard needed</text>
<text class="vx-text" x="40" y="160">Counting threads: 10 threads, the last group holds 2</text>
<text class="vx-text-muted" x="40" y="178">OpenCL with non-uniform work-groups, Metal dispatchThreads</text>
<rect class="vx-box" x="40" y="188" width="194" height="60" rx="4"/>
<rect class="vx-box" x="254" y="188" width="194" height="60" rx="4"/>
<rect class="vx-box" x="468" y="188" width="102" height="60" rx="4"/>
<rect class="vx-box-accent" x="48" y="198" width="40" height="40" rx="3"/><text class="vx-mono" x="68" y="223" text-anchor="middle">0</text>
<rect class="vx-box-accent" x="94" y="198" width="40" height="40" rx="3"/><text class="vx-mono" x="114" y="223" text-anchor="middle">1</text>
<rect class="vx-box-accent" x="140" y="198" width="40" height="40" rx="3"/><text class="vx-mono" x="160" y="223" text-anchor="middle">2</text>
<rect class="vx-box-accent" x="186" y="198" width="40" height="40" rx="3"/><text class="vx-mono" x="206" y="223" text-anchor="middle">3</text>
<rect class="vx-box-accent" x="262" y="198" width="40" height="40" rx="3"/><text class="vx-mono" x="282" y="223" text-anchor="middle">4</text>
<rect class="vx-box-accent" x="308" y="198" width="40" height="40" rx="3"/><text class="vx-mono" x="328" y="223" text-anchor="middle">5</text>
<rect class="vx-box-accent" x="354" y="198" width="40" height="40" rx="3"/><text class="vx-mono" x="374" y="223" text-anchor="middle">6</text>
<rect class="vx-box-accent" x="400" y="198" width="40" height="40" rx="3"/><text class="vx-mono" x="420" y="223" text-anchor="middle">7</text>
<rect class="vx-box-accent" x="476" y="198" width="40" height="40" rx="3"/><text class="vx-mono" x="496" y="223" text-anchor="middle">8</text>
<rect class="vx-box-accent" x="522" y="198" width="40" height="40" rx="3"/><text class="vx-mono" x="542" y="223" text-anchor="middle">9</text>
<text class="vx-text-muted" x="590" y="214">remainder group:</text>
<text class="vx-text-muted" x="590" y="230">no guard needed</text>
<text class="vx-text-muted" x="40" y="276">Each outlined box is one group; each square is one thread, numbered by its global index.</text>
</svg>
<figcaption>Figure 2. Ten elements in groups of four. A launch that counts groups creates whole groups, so two threads have no element and the kernel needs a guard. A launch that counts threads, where the device allows it, ends with a smaller group and every thread has work.</figcaption>
</figure>

The next example runs this arithmetic for SAXPY in every style, at 1,000 elements and at 1,024. At 1,024 every style creates the same 4 groups of 256 and nobody idles.

--8<-- "includes/examples/gpu/g7-programming-models/launch_shapes.cpp.md"

For a compiler, the lesson is that the grid is not one number. It is a group shape plus either a group count or a thread count, and the kernel's body depends on which: the guard belongs to the kernel exactly when the launch can create threads that have no element.

??? check "SAXPY over 1,000 elements in groups of 128 threads. How many groups does a CUDA launch need, and how many of its threads are idle? What does Metal's `dispatchThreads` create instead?"

    CUDA: `(1000 + 127) / 128 = 8` blocks, 1,024 threads, 24 of them idle, so the kernel needs its guard. `dispatchThreads(1000, 128)` also creates 8 threadgroups, but the last holds `1000 - 7 × 128 = 104` threads, so all 1,000 threads have an element.

## Where the group size lives

The group size is fixed in different places, and the place decides when a compiler can know it.

In CUDA it is part of each launch: two launches of the same kernel may use different execution configurations.[^cuda-intro] OpenCL takes it at enqueue time, or lets the runtime choose one when the host passes none; a kernel may also fix it in its source with `__attribute__((reqd_work_group_size(X, Y, Z)))`, which the OpenCL C specification says lets the compiler optimize the generated code for that size.[^opencl-api][^opencl-c] Metal takes it at every dispatch, as `threadsPerThreadgroup`.[^metal-grid]

Vulkan and WebGPU put it inside the kernel. A SPIR-V entry point declares its workgroup size with the `LocalSize` execution mode (or a variant of it), so the size is compiled into the module,[^spirv-terms] and a WGSL compute entry point must carry a `@workgroup_size` attribute, which defines the workgroup grid.[^wgsl-attr] The dispatch then supplies only the count of groups. The IREE project, which compiles machine-learning models for Vulkan and Metal from one code generator, describes exactly this mismatch: Vulkan splits the count and the size between the dispatch call and the SPIR-V module, while Metal passes both in one API call.[^iree-metal]

A compiler that knows the group size when it compiles the kernel can use it: to fold `blockDim.x` into a constant, to bound loops, and to size group memory. When MLIR outlines a kernel from a launch with constant sizes, it records them on the kernel as `gpu.known_block_size` and `gpu.known_grid_size`, as the MLIR example later in this chapter shows. Vortex's arrays carry their shapes in their types, so a Vortex compiler can know both sizes at compile time for every model, whether that model wants the group size in the kernel or at the launch.

## How arguments reach a kernel

The kernel also needs its data. Here the models differ in how an argument is matched with the value the host supplies.

- **By call.** A CUDA kernel is called like a function; its arguments follow the execution configuration.[^cuda-intro] A SYCL kernel receives values captured by its lambda and buffers through accessor objects.[^sycl-intro]
- **By position.** OpenCL sets each argument by its index with `clSetKernelArg`.[^opencl-api] An MSL kernel marks each buffer parameter with `[[buffer(n)]]`, and the host binds a buffer to index `n` with `setBuffer:offset:atIndex:`, as `saxpy_msl.metal` and Apple's sample do.[^msl-kernel][^metal-calc]
- **By binding.** A SPIR-V variable carries `DescriptorSet` and `Binding` decorations that link it to buffers the client API supplies,[^spirv-terms] and a WGSL variable carries `@group(g)` and `@binding(b)` for the same purpose.[^wgsl-compute]

Whatever the mechanism, a compiler that generates both the kernel and its launch must produce the same thing: a **kernel interface**, the ordered list of the kernel's parameters, each with its type, whether the kernel reads or writes it, and the slot the host must bind it to. The host side and the device side are two views of that one list, and they agree only if they are generated from it.

Vortex already states most of it. A parameter declared `&[f32; 1000]` is read-only, `&mut [f32; 1000]` is written, and `a: f32` arrives by value. The shape and element type are in the type. What the compiler adds is the slot numbering and the per-model spelling.

## Same source, or two?

Every model must at some point compile the kernel for the device and connect it to the host code that launches it. The difference is who does that and when (Figure 3).

CUDA, HIP and SYCL are **single-source**: one compiler reads a file holding both sides. The CUDA guide describes the steps. In its first phase `nvcc` separates the device code from the host code and hands each to its own compiler; the device code becomes PTX, then machine code for each requested GPU, and both can be embedded in one **fatbin** container inside the executable.[^nvcc-flow][^cuda-fatbin] When the program runs, the driver loads the best binary for the GPU it finds, or compiles embedded PTX at load time for a GPU newer than any binary.[^cuda-fatbin]

The SYCL specification names two ways to build such a compiler. With **single source, multiple compiler passes**, a device compiler reads the file first and emits the kernels and some glue, then an ordinary host compiler reads it again. With **single source, single compiler pass**, one custom compiler reads it once and produces both.[^sycl-impl] Either way, one compiler sees the kernel and its launch together, so it can check that the values passed to a kernel have types the device supports and generate both sides of the kernel interface from one list.

<figure class="vx-figure">
<svg viewBox="0 0 760 335" role="img" aria-label="Two rows, split by a dashed line into build time on the left and run time on the right. Single source: one file holding host code and kernel goes to one compiler, which splits it and produces one executable with the device code embedded; at run time the driver loads that code and the launch names the kernel. Separate sources: a host program and a kernel text are built apart, with an optional offline compile of the kernel to a Metal library or SPIR-V; at run time the host asks the device API to compile or load the kernel, then builds a pipeline, binds arguments and dispatches.">
<line class="vx-line" x1="548" y1="20" x2="548" y2="325" stroke-dasharray="6 4"/>
<text class="vx-text-muted" x="280" y="20" text-anchor="middle">build time</text>
<text class="vx-text-muted" x="652" y="20" text-anchor="middle">run time</text>
<text class="vx-text-accent" x="20" y="50">Single source: CUDA, HIP, SYCL</text>
<rect class="vx-box" x="20" y="62" width="150" height="58" rx="4"/>
<text class="vx-mono" x="95" y="86" text-anchor="middle">saxpy.cu</text>
<text class="vx-text-muted" x="95" y="106" text-anchor="middle">host code + kernel</text>
<line class="vx-line" x1="170" y1="91" x2="192" y2="91"/>
<polygon class="vx-arrowhead" points="192,86 200,91 192,96"/>
<rect class="vx-box-strong" x="200" y="62" width="155" height="58" rx="4"/>
<text class="vx-text" x="277" y="86" text-anchor="middle">one compiler</text>
<text class="vx-text-muted" x="277" y="106" text-anchor="middle">splits host and device</text>
<line class="vx-line" x1="355" y1="91" x2="377" y2="91"/>
<polygon class="vx-arrowhead" points="377,86 385,91 377,96"/>
<rect class="vx-box" x="385" y="62" width="150" height="58" rx="4"/>
<text class="vx-text" x="460" y="86" text-anchor="middle">one executable</text>
<text class="vx-text-muted" x="460" y="106" text-anchor="middle">device code inside</text>
<line class="vx-flow" x1="535" y1="91" x2="562" y2="91"/>
<polygon class="vx-arrowhead" points="562,86 570,91 562,96"/>
<rect class="vx-box-accent" x="570" y="62" width="170" height="58" rx="4"/>
<text class="vx-text" x="655" y="86" text-anchor="middle">driver loads it</text>
<text class="vx-text-muted" x="655" y="106" text-anchor="middle">launch names the kernel</text>
<text class="vx-text-accent" x="20" y="165">Separate sources: OpenCL, Vulkan, WebGPU, Metal</text>
<rect class="vx-box" x="20" y="177" width="150" height="58" rx="4"/>
<text class="vx-text" x="95" y="201" text-anchor="middle">host program</text>
<text class="vx-text-muted" x="95" y="221" text-anchor="middle">Swift, C++, JavaScript</text>
<rect class="vx-box" x="20" y="257" width="150" height="58" rx="4"/>
<text class="vx-text" x="95" y="281" text-anchor="middle">kernel text</text>
<text class="vx-text-muted" x="95" y="301" text-anchor="middle">MSL, OpenCL C, WGSL</text>
<line class="vx-line" x1="170" y1="286" x2="192" y2="286"/>
<polygon class="vx-arrowhead" points="192,281 200,286 192,291"/>
<rect class="vx-box" x="200" y="257" width="155" height="58" rx="4" stroke-dasharray="5 4"/>
<text class="vx-text" x="277" y="281" text-anchor="middle">optional offline</text>
<text class="vx-text-muted" x="277" y="301" text-anchor="middle">.metallib, SPIR-V</text>
<line class="vx-flow" x1="170" y1="206" x2="562" y2="206"/>
<polygon class="vx-arrowhead" points="562,201 570,206 562,211"/>
<line class="vx-flow" x1="355" y1="286" x2="562" y2="286"/>
<polygon class="vx-arrowhead" points="562,281 570,286 562,291"/>
<rect class="vx-box-accent" x="570" y="177" width="170" height="58" rx="4"/>
<text class="vx-text" x="655" y="201" text-anchor="middle">pipeline, bind,</text>
<text class="vx-text-muted" x="655" y="221" text-anchor="middle">dispatch by name</text>
<rect class="vx-box-accent" x="570" y="257" width="170" height="58" rx="4"/>
<text class="vx-text" x="655" y="281" text-anchor="middle">compile or load</text>
<text class="vx-text-muted" x="655" y="301" text-anchor="middle">the kernel at run time</text>
<line class="vx-line" x1="655" y1="257" x2="655" y2="243"/>
<polygon class="vx-arrowhead" points="650,243 655,235 660,243"/>
</svg>
<figcaption>Figure 3. Who connects the kernel to its launch. In a single-source model one compiler splits the file and embeds the device code in the executable. In a separate-source model the host and the kernel are built apart and meet at run time, through a kernel name and the argument slots of the kernel interface.</figcaption>
</figure>

OpenCL, Vulkan, WebGPU and Metal are **separate-source**: the kernel is its own text or binary, and the host program meets it only at run time, by name. A Metal host can compile MSL text at run time with `makeLibrary(source:options:)`,[^metal-source] or load a library that Xcode built from the app's `.metal` files, as Apple's sample does,[^metal-calc] or one built by hand with the command-line tools, which turn each source file into an intermediate `.ir` file and then into a `.metallib`.[^metal-precompile] OpenCL builds a program object from source, SPIR-V or a binary;[^opencl-api] Vulkan takes SPIR-V compiled ahead of time;[^vk-module] WebGPU takes WGSL text at run time.[^webgpu]

The split is not absolute. CUDA also offers NVRTC, a library that compiles CUDA C++ device code to PTX at run time,[^cuda-fatbin] and HIP's documentation covers a runtime compiler of its own.[^hip-what] The families describe the usual workflow. What matters for a compiler is the connection: in a separate-source model nothing checks that the host binds the buffer the kernel expects at slot 2, unless the same compiler generated both.

A single-source compiler has to do the separation itself, and MLIR shows the step as a pass. The next example writes SAXPY with the thread code inline in the host function, inside a `gpu.launch` region; `--gpu-kernel-outlining` moves the region into a kernel in its own `gpu.module` and leaves a `gpu.launch_func` behind.[^mlir-gpu][^mlir-passes]

--8<-- "includes/examples/gpu/g7-programming-models/outline_launch.mlir.md"

Read the output as a compiler engineer. Every value the region used from outside, the bound `n`, the scalar `a` and the two arrays, became a kernel parameter, so the pass built the kernel interface from the region's free values. The constant group and grid sizes became the `gpu.known_block_size` and `gpu.known_grid_size` attributes. And the launch counts blocks, so the guard stayed in the kernel.

??? check "SYCL and OpenCL come from the same standards body and share most of their vocabulary. Which question about the compiler puts them in different families here?"

    Whether one compiler sees the kernel and the code that launches it together. A SYCL implementation compiles one C++ file into both host code and device images; an OpenCL host program receives the kernel as separate source, SPIR-V or a binary and connects to it at run time through a kernel name and argument indices.

## Underneath: a handful of targets

However a kernel is written, a compiler must turn it into something a driver accepts, and there are fewer of those than there are languages.

| Kernel language | What the program hands over | Documented by |
| --- | --- | --- |
| CUDA C++ (NVIDIA) | PTX and machine code (cubin) in a fatbin | NVIDIA[^cuda-fatbin] |
| HIP, OpenCL C (AMD) | AMD GPU code objects from LLVM's AMDGPU back end | LLVM[^llvm-amdgpu] |
| OpenCL C, SYCL, GLSL | SPIR-V, or native code for SYCL | Khronos, LLVM[^opencl-api][^sycl-impl][^llvm-spirv] |
| WGSL | WGSL text, at run time | W3C[^webgpu] |
| MSL | MSL text, or a `.metallib` built through `.ir` files | Apple[^metal-source][^metal-precompile] |

PTX is NVIDIA's virtual instruction set, which other compilers can also generate; `ptxas` ahead of time, or the driver at run time, turns it into machine code for a particular GPU.[^cuda-fatbin] LLVM has back ends that emit PTX, AMD GPU code and SPIR-V.[^llvm-nvptx][^llvm-amdgpu][^llvm-spirv] Apple documents MSL and the `.metal` to `.ir` to `.metallib` build but not the format of the intermediate file, and IREE's design notes put it plainly: Metal provides no open intermediate language.[^metal-precompile][^iree-metal]

That gap is why translators exist. SPIRV-Cross converts SPIR-V into GLSL, HLSL or MSL source,[^spirv-cross] and MoltenVK, a Khronos project that implements Vulkan on top of Metal, converts an application's SPIR-V shaders to MSL.[^moltenvk] IREE's Metal back end takes the same road: it generates SPIR-V, converts it to MSL with SPIRV-Cross, and embeds either the MSL text, compiled when the program runs, or a compiled library.[^iree-metal] MLIR's `gpu` dialect sits one level higher: a program written once with `gpu.launch` or `gpu.func` can be lowered toward NVIDIA, AMD or SPIR-V targets.[^mlir-gpu] [G8](g8-isas-and-irs.md) opens these targets and [G9](g9-gpu-compilers-in-llvm.md) the LLVM back ends that produce them.

On the owner's M4 Pro, the choice is narrow. Apple deprecated OpenCL in macOS 10.14 and recommends Metal instead,[^apple-opencl] and CUDA 10.2 was the last CUDA release to support macOS.[^cuda-102] The LLVM 18 on the machine registers only AArch64 targets, so it has no NVPTX, AMDGPU or SPIR-V back end, while a Swift program that compiles MSL text at run time works without Apple's offline Metal toolchain (both checked on 2026-09-23). This chapter's CUDA file is therefore compile-checked only where `nvcc` is installed, its Metal file is not checked by the examples harness at all, and its MLIR example runs here.

## What none of this changes

A model decides how a kernel is launched. It does not decide what a Vortex program means, and three Vortex rules hold for every target.

The first is arithmetic. [Decision 56](../decisions/numbers.md#d56) makes every floating-point operation one IEEE 754 operation, rounded once, with no contraction. The compilers behind these models do not start there. `nvcc` contracts multiplies and adds into fused multiply-add instructions unless it is given `--fmad=false`,[^nvcc-fmad] and Metal's `fastMathEnabled` compile option, which allows optimizations that may violate IEEE 754, defaults to true (newer SDKs replace it with a `mathMode` setting).[^metal-fastmath]

SAXPY's `a * x[i] + y[i]` is exactly the shape these defaults fuse. A Vortex compiler that emits CUDA C++ or MSL text must switch those defaults off; one that emits LLVM IR must not give its operations the `contract` fast-math flag, which allows exactly this fusion.[^llvm-contract] [G4](g4-memory-performance.md#what-does-not-change-the-bits) showed that moving work between threads leaves the bits alone; changing the compiler's floating-point mode does not.

The second is aliasing. [Decision 25](../decisions/references.md#d25) forbids a variable lent as `&mut` in a call from appearing in any other argument of that call. In SAXPY that means `y` cannot overlap `x`, so every thread can load `x[i]` without fearing another thread's store to `y`. C and C++ kernel languages cannot assume that about two pointer parameters without a promise from the programmer; a Vortex compiler can record it in the kernel interface, already checked.

The third is shape. Every size this chapter computed, the group count, the idle threads, the guard, is known at compile time when the array's shape is part of its type, as it is for `[f32; 1000]`. [Decision 43](../decisions/arrays.md#d43) adds the row-major order that the worked example used to put `column` on `x`. So a Vortex compiler can produce a complete launch description, and the kernel interface next to it, before it ever chooses a model.

## For Vortex

!!! vortex "Exercise"

    **Build** a model-neutral description of a GPU kernel in your compiler: for a function whose outer loops are parallel, a **kernel interface** and a **launch description**, plus a report that renders both in the vocabulary of two models. No device code.

    1. The kernel interface: every parameter in declaration order, with its type, its access (read-only from `&`, read and written from `&mut`, by value for a scalar) and a binding slot. Choose one rule for numbering slots and print it with the report.
    2. The launch description: for a given group shape (take 16 × 16 for two-dimensional nests; [G5](g5-occupancy.md) is where you learn to choose one), the grid in groups and in threads, the number of idle threads, and whether the kernel needs a guard. Put `x` on the loop variable that is the last subscript, as in this chapter's worked example.
    3. A target description with each target's limit on threads per group. On the owner's M4 Pro, a Metal compute pipeline reported a maximum of 1,024 threads per threadgroup (checked on 2026-09-23); record your own machine's value and the date.
    4. Two renderings of the same description: one for Metal, which counts threads with `dispatchThreads` and binds with `[[buffer(n)]]`, and one for WebGPU, which fixes `@workgroup_size` in the shader, counts groups with `dispatchWorkgroups` and binds with `@group` and `@binding`. Each rendering states its own guard requirement.
    5. A note on the description naming decisions 56, 25 and 43 and what each obliges a lowering to do.

    **Not yet:** emitting MSL, WGSL, CUDA or SPIR-V; choosing the group shape (G5); choosing which loop variable maps to lanes beyond the rule above (G4's report does that); group memory and barriers ([G6](g6-synchronization.md)); picking an IR ([G8](g8-isas-and-irs.md), [M12](../mlir/m12-vortex-gpu-path.md)).

    **Proof that it works:**

    - Golden test for the stage 10 `multiply` at `[f32; 64, 64]` with 16 × 16 groups: parameters `a` and `b` read-only, `c` written; 4 × 4 groups, 64 × 64 threads, none idle, no guard in either rendering.
    - The same kernel at `[f32; 70, 70]`: the WebGPU rendering reports 5 × 5 workgroups, 80 × 80 invocations, 1,500 of them idle, guard required. The Metal rendering reports 70 × 70 threads, no idle threads, and no guard on GPUs that support nonuniform threadgroups, and says that condition.
    - This chapter's `saxpy` with groups of 256: `a` by value, `x` read-only, `y` written; 4 groups; 24 idle threads in the WebGPU rendering and none in the Metal one.
    - Rejections with a reason, not a guess: a group shape of 32 × 64 on a target whose limit is 1,024 threads; a loop whose bound is a run-time value rather than a constant.
    - The note cites the three decisions by number and link, so a reader can check it against them.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does every model in this chapter launch, whatever it calls the parts?** A grid of equally sized groups of threads, scheduled in subgroups, with a group memory and a barrier shared inside each group.
    - **How does a thread find its element?** From its group position and its position in the group: global position = group position × group size + position in the group, in each dimension.
    - **When does a kernel need a bounds guard?** When the launch counts groups and the element count is not a multiple of the group size, so the rounded-up grid has threads without elements; a launch that counts threads with non-uniform groups avoids it.
    - **Where does the group size live?** At the launch in CUDA, OpenCL and Metal; inside the compiled kernel in Vulkan (`LocalSize`) and WebGPU (`@workgroup_size`).
    - **What must a compiler produce so that host and kernel agree?** A kernel interface: the ordered parameters with types, access and binding slots, generated once and used for both sides.
    - **What separates single-source from separate-source models?** Whether one compiler reads the kernel and its launch together and splits them, as outlining does, or the two are built apart and meet at run time by name.
    - **Which compiler defaults would break decision 56?** `nvcc`'s default contraction into fused multiply-adds and Metal's default fast math; a Vortex lowering must turn both off.

## Where this comes back

!!! next "You will use this again in"

    - [G8. ISAs and IRs](g8-isas-and-irs.md): *PTX*, *SPIR-V*, *Metal libraries*
    - [G9. GPU compilers inside LLVM](g9-gpu-compilers-in-llvm.md): *kernel outlining*, *NVPTX and AMDGPU back ends*
    - [G10. The GPU matmul ladder](g10-matmul-ladder.md): *group shape*, *bounds guard*
    - [G11. Matrix units](g11-matrix-units.md): *subgroup*, *SIMD-group*
    - [G13. Tile languages](g13-tile-languages.md): *single-source*
    - [M10. MLIR for GPUs](../mlir/m10-mlir-for-gpus.md): *`gpu.launch`*, *kernel outlining*, *known block size*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *kernel interface*, *launch description*, *host API choice*

## Sources and further reading

Read the CUDA guide's introduction to CUDA C++ first, then Apple's "Performing calculations on a GPU" and "Calculating threadgroup and grid sizes", then WGSL's section on compute shaders and workgroups, which states the coordinate arithmetic most exactly. The IREE Metal design note shows the same differences from a compiler's side.

[^cuda-intro]: NVIDIA, "CUDA Programming Guide", v13.4.2, sections 2.1.2.1, "Specifying Kernels", 2.1.2.2, "Launching Kernels", and 2.1.2.3, "Thread and Grid Index Intrinsics". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/intro-to-cuda-cpp.html#intro-cpp-launching-kernels>
[^cuda-bounds]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.1.2.3.1, "Bounds Checking". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/intro-to-cuda-cpp.html#intro-cpp-bounds-checking>
[^cuda-simt]: NVIDIA, "CUDA Programming Guide", v13.4.2, sections 2.3.2, "Thread Hierarchy", 2.3.3.2, "Shared Memory", and 2.3.3.4, "Local Memory". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html>
[^cuda-fatbin]: NVIDIA, "CUDA Programming Guide", v13.4.2, sections 1.3.3, "Parallel Thread Execution (PTX)", and 1.3.4, "Cubins and Fatbins", including 1.3.4.3, "Just-in-Time Compilation". <https://docs.nvidia.com/cuda/cuda-programming-guide/01-introduction/cuda-platform.html#cuda-platform-cubins-fatbins>
[^nvcc-flow]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.7.2, "NVCC Compilation Workflow". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/nvcc.html#nvcc-compilation-workflow>
[^nvcc-fmad]: NVIDIA, "NVIDIA CUDA Compiler Driver NVCC", section 4.2.7.11, "--fmad". <https://docs.nvidia.com/cuda/cuda-compiler-driver-nvcc/index.html>
[^hip-what]: AMD, "What is HIP?", HIP 7.15.0 documentation. <https://rocm.docs.amd.com/projects/HIP/en/latest/what_is_hip.html>
[^hip-ext]: AMD, "HIP C++ language extensions", HIP 7.15.0 documentation, sections "Calling `__global__` functions" and "Index built-ins". <https://rocm.docs.amd.com/projects/HIP/en/latest/how-to/hip_cpp_language_extensions.html>
[^amd-lds]: AMD, "Hardware implementation", HIP 7.15.0 documentation, section "Local data share (LDS)". <https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html#local-data-share-lds>
[^opencl-api]: Khronos Group, "The OpenCL Specification", version 3.0, unified: the glossary entries for work-item, work-group, local memory, sub-group, ND-range and remainder work-groups; `clCreateProgramWithSource`, `clCreateProgramWithIL`, `clSetKernelArg` and `clEnqueueNDRangeKernel`. <https://registry.khronos.org/OpenCL/specs/3.0-unified/html/OpenCL_API.html#clEnqueueNDRangeKernel>
[^opencl-c]: Khronos Group, "The OpenCL C Specification", version 3.0, unified: the work-item functions, the synchronization functions and the `reqd_work_group_size` attribute. <https://registry.khronos.org/OpenCL/specs/3.0-unified/html/OpenCL_C.html>
[^sycl-intro]: Khronos Group, "SYCL 2020 Specification", revision 12, section 2, "Introduction", and section 3.13, "Language restrictions in kernels". <https://registry.khronos.org/SYCL/specs/sycl-2020/html/sycl-2020.html>
[^sycl-impl]: Khronos Group, "SYCL 2020 Specification", revision 12, section 3.12, "Implementation options": 3.12.1, "Single source multiple compiler passes", and 3.12.2, "Single source single compiler pass". <https://registry.khronos.org/SYCL/specs/sycl-2020/html/sycl-2020.html#subsec:smcp>
[^sycl-terms]: Khronos Group, "SYCL 2020 Specification", revision 12, section 4.9.1, including 4.9.1.2, "nd_range class", 4.9.1.5, "nd_item class", and 4.9.1.8, "sub_group class"; and the glossary entries for local memory and sub-group. <https://registry.khronos.org/SYCL/specs/sycl-2020/html/sycl-2020.html#subsubsec:nd-range-class>
[^sycl-invoke]: Khronos Group, "SYCL 2020 Specification", revision 12, section 4.9.4.2, "SYCL functions for invoking kernels": the `errc::nd_range` exception for a global size not divisible by the local size. <https://registry.khronos.org/SYCL/specs/sycl-2020/html/sycl-2020.html>
[^spirv-intro]: Khronos Group, "SPIR-V Specification", version 1.6, section 1, "Introduction", including its example GLSL shader. <https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html>
[^spirv-terms]: Khronos Group, "SPIR-V Specification", version 1.6: the terms Invocation, Subgroup and Workgroup; the `LocalSize` execution mode; the `Workgroup` storage class; the `Binding` and `DescriptorSet` decorations; the compute built-ins; `OpControlBarrier`. <https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html>
[^vk-dispatch]: Khronos Group, "vkCmdDispatch", Vulkan reference page. <https://registry.khronos.org/vulkan/specs/latest/man/html/vkCmdDispatch.html>
[^vk-module]: Khronos Group, "VkShaderModuleCreateInfo", Vulkan reference page. <https://registry.khronos.org/vulkan/specs/latest/man/html/VkShaderModuleCreateInfo.html>
[^webgpu]: W3C, "WebGPU", Candidate Recommendation Draft, 15 September 2026: `createShaderModule` and `dispatchWorkgroups`. <https://www.w3.org/TR/webgpu/#dom-gpucomputepassencoder-dispatchworkgroups>
[^wgsl-compute]: W3C, "WebGPU Shading Language", Candidate Recommendation Draft, 21 September 2026, section 15.3, "Compute Shaders and Workgroups", and the sections on built-in values, address spaces, subgroups, `workgroupBarrier` and the `group` and `binding` attributes. <https://www.w3.org/TR/WGSL/#compute-shader-workgroups>
[^wgsl-attr]: W3C, "WebGPU Shading Language", Candidate Recommendation Draft, 21 September 2026, section 12.15, "workgroup_size". <https://www.w3.org/TR/WGSL/#attribute-workgroup_size>
[^msl-kernel]: Apple, "Metal Shading Language Specification", version 4.1, section 4.4, "Threadgroup Address Space", and section 5.2.3.6, "Kernel Function Input Attributes". <https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf>
[^metal-calc]: Apple, "Performing calculations on a GPU", Metal sample code. <https://developer.apple.com/documentation/metal/performing-calculations-on-a-gpu>
[^metal-cpp]: Apple, "Metal-cpp". <https://developer.apple.com/metal/cpp/>
[^metal-grid]: Apple, "Calculating threadgroup and grid sizes", Metal documentation. <https://developer.apple.com/documentation/metal/calculating-threadgroup-and-grid-sizes>
[^metal-source]: Apple, "`makeLibrary(source:options:)`", Metal documentation. <https://developer.apple.com/documentation/metal/mtldevice/makelibrary(source:options:)>
[^metal-precompile]: Apple, "Building a shader library by precompiling source files", Metal documentation. <https://developer.apple.com/documentation/metal/building-a-shader-library-by-precompiling-source-files>
[^metal-fastmath]: Apple, "`fastMathEnabled`", `MTLCompileOptions`, Metal documentation. <https://developer.apple.com/documentation/metal/mtlcompileoptions/fastmathenabled>
[^apple-opencl]: Apple, "OpenCL", Apple Developer. <https://developer.apple.com/opencl/>
[^cuda-102]: NVIDIA, "CUDA Toolkit 10.2 Release Notes". <https://docs.nvidia.com/cuda/archive/10.2/cuda-toolkit-release-notes/index.html>
[^llvm-contract]: LLVM Project, "LLVM Language Reference Manual", section "Fast-Math Flags": `contract`. <https://llvm.org/docs/LangRef.html#fast-math-flags>
[^llvm-nvptx]: LLVM Project, "User Guide for NVPTX Back-end". <https://llvm.org/docs/NVPTXUsage.html>
[^llvm-amdgpu]: LLVM Project, "User Guide for AMDGPU Backend", including the section "Source Languages". <https://llvm.org/docs/AMDGPUUsage.html>
[^llvm-spirv]: LLVM Project, "User Guide for SPIR-V Target". <https://llvm.org/docs/SPIRVUsage.html>
[^mlir-gpu]: MLIR Project, "'gpu' Dialect": the compilation overview, `gpu.launch` and `gpu.launch_func`. <https://mlir.llvm.org/docs/Dialects/GPU/#gpulaunch-gpulaunchop>
[^mlir-passes]: MLIR Project, "Passes": `-gpu-kernel-outlining`. <https://mlir.llvm.org/docs/Passes/#-gpu-kernel-outlining>
[^iree-metal]: IREE Project, "Metal HAL driver", design document: the sections on kernel compilation and on workgroup and threadgroup size. <https://iree.dev/developers/design-docs/metal-hal-driver/>
[^spirv-cross]: Khronos Group, "SPIRV-Cross", README. <https://github.com/KhronosGroup/SPIRV-Cross>
[^moltenvk]: Khronos Group, "MoltenVK", README. <https://github.com/KhronosGroup/MoltenVK>
