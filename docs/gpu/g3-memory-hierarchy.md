# G3. The GPU memory hierarchy

<p class="page-intro">A GPU has no single "memory": a value can live in a register that belongs to one thread, a scratchpad that belongs to one block, a cache that belongs to one core, or a pool that belongs to the whole device, and each choice changes who can see the value and how fast it moves. This chapter names each level in CUDA, HIP and Metal vocabulary, gives the capacities that are known, and explains what "unified memory" changes on Apple silicon.</p>

<p class="vx-meta" markdown="1">Level: Foundations · Reading time: about 30 minutes · Builds on: [G2. The SIMT execution model](g2-simt.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is a warp, and why does it matter that 32 threads execute one instruction together?"

        A warp is a group of threads (32 on current NVIDIA GPUs) that the hardware runs in lockstep, one instruction for the whole group at once. Because the group moves together, what one thread's instruction costs often depends on what all 32 threads' addresses look like together, not on any one thread alone.

        Introduced in [G2. The SIMT execution model](g2-simt.md).

    ??? question "Why is a GPU built around many small, slow-clocked cores instead of a few fast ones?"

        A GPU is a throughput machine: it accepts long latency per operation in exchange for having thousands of operations in flight at once, and hides that latency by switching to other ready work. A CPU is the opposite trade, a latency machine.

        Introduced in [G1. Throughput machines](g1-throughput-machines.md).

    ??? question "In what order are the elements of a `[f32; 64, 64]` array stored?"

        Row after row, the last index varying fastest: `m[i, j + 1]` sits next to `m[i, j]` in memory.

        Introduced in [Arrays and shapes, decision 43](../decisions/arrays.md#d43).

    ??? question "What does a cache line do to a CPU's memory system, and why does it reward sequential access?"

        Memory moves between levels of the hierarchy in whole lines, usually tens of bytes, not single values. Reading one byte of a line brings the rest along for free; a program that then uses its neighbours pays for the line once instead of many times.

        Introduced in [P2. The memory hierarchy](../optimize/p2-memory-hierarchy.md).

!!! goals "In this chapter"

    - Name each level of a GPU's on-chip and off-chip memory in CUDA, HIP and Metal vocabulary, and say which thread or group of threads owns it.
    - Explain why a thread's registers and a block's shared memory are both fast because they are both small, and both small because they are both shared out among many resident threads.
    - Decide, for a value that several threads of one block need, whether it belongs in shared memory or should stay a private, per-thread value.
    - Describe what "unified memory" means on Apple silicon, and which storage mode controls whether the CPU can read a GPU buffer directly.
    - Read an MSL or MLIR `gpu`-dialect address space and say which level of the hierarchy it names.

## One loop, run by many threads at once

Look at one output element of a matrix product, computed the way [stage 10](../compiler/guide/stage-10-matrix-multiplication.md)'s reference implementation computes it:

```vortex
// items: valid
fn multiply(a: &[f32; 2, 3], b: &[f32; 3, 2], c: &mut [f32; 2, 2]) {
    for row in 0..2 {
        for column in 0..2 {
            let mut sum: f32 = 0.0;
            for k in 0..3 {
                sum += a[row, k] * b[k, column];
            }
            c[row, column] = sum;
        }
    }
}
```

On a CPU this is one loop nest, run once. A GPU version of the same idea runs the two outer loops as thread indices: thread `(row, column)` computes one element of `c`, and every thread runs the same `k` loop, reading a row of `a` and a column of `b` element by element. Nothing about that description says where `sum`, `a[row, k]` or `b[k, column]` live while the loop runs, and the answer changes what the kernel costs.

`sum` never leaves the thread that owns it: no other thread reads or writes it, so the fastest place for it is a **register**, storage private to one thread. `a[row, k]` is read again by every thread that shares this `row`, once per value of `column`: if those threads can read it from a place closer than the memory where the array lives, the loop does less work for the same result. That closer place is **shared memory**, storage private to one block of threads but visible to all of them. Between the register and the array's real location, a chip also has **caches** that hold recently touched data without either thread or programmer asking for it, and finally the array itself lives in **device memory**, a pool visible to every thread on the GPU. Four levels, four different owners: one thread, one block, one core's recent history, and the whole device.

## A thread's own memory: registers

A **register** holds one value, addressed by name rather than by an address, and reading or writing it costs one instruction operand, not a memory access. NVIDIA's programming guide lists registers as the first and fastest of a kernel's memory spaces, and says each thread has its own, invisible to every other thread, including others in the same block.[^n2] AMD's HIP documentation gives the physical name for the same idea on its GPUs: scalar and vector general-purpose registers (SGPRs and VGPRs) hold data that is uniform across a wave or private to one lane.[^a2]

Registers are scarce, and the scarcity is architectural, not accidental: a chip fixes the number of registers per core once, in silicon, and every thread resident on that core draws from the same pool. NVIDIA's Hopper tuning guide states the Hopper SM's register file as 65,536 32-bit registers, shared by every thread block resident on that SM at once, with a per-thread ceiling of 255.[^n10] Give one thread more live values than fit in its share of that pool and the compiler must **spill**: write some values to memory instead, under a name ("local memory" in CUDA) that is misleading, because it is not on-chip at all: it is an ordinary, private slice of the same device memory pool everything else in this chapter calls slow.[^n2] [G5](g5-occupancy.md) turns this scarcity into the central trade of occupancy: fewer registers per thread lets more threads be resident at once.

??? check "The `sum` loop above reads `a[row, k]` and `b[k, column]` three times each (once per `k`) but writes `c[row, column]` once. Why does keeping `sum` in a register cost nothing extra, while keeping a whole row of `a` in a register would?"

    `sum` is one value, live for the loop's whole body, exactly what one register holds. A row of `a` is three values, and the thread would need three registers to hold them, taken from the same fixed-size pool every other resident thread also draws from; at some row length the compiler runs out and spills. A single accumulator scales to any loop length for free; a whole row does not.

## A block's shared scratchpad

Threads within one block can also cooperate through memory that is not private to any one of them: an on-chip scratchpad that the whole block shares, filled and read at the programmer's or compiler's choice rather than by hardware guesswork. Three programming models give the same physical idea three names. CUDA calls it **shared memory**, on-chip memory partitioned among the thread blocks resident on an SM, usable as a low-latency, high-bandwidth alternative to global memory for data a block reuses.[^n2] HIP calls the AMD equivalent the **local data share (LDS)**, again a per-workgroup scratchpad.[^a2] Metal calls it **threadgroup memory**, memory a compute kernel's threads share within one threadgroup.[^ap1]

Whichever name is used, three facts hold. First, a block's shared memory is invisible to every other block: two blocks computing different tiles of the same matrix product cannot see each other's scratchpad, only their own. Second, its capacity is fixed and shared out in the same way the register file is, so a kernel that asks for a large tile leaves room for fewer blocks per core. NVIDIA's Hopper tuning guide gives the Hopper SM's shared memory as 228 KB of physical capacity, of which one thread block may address up to 227 KB.[^n10] On Apple silicon, asking a device directly gives the same kind of number: `MTLDevice.maxThreadgroupMemoryLength` reads 32,768 bytes on the owner's Apple M4 Pro (macOS 27.0, measured 2026-09-23), matching the 32 KB that Apple's Metal Feature Set Tables document for the Apple4 GPU family onward.[^ap2] Third, unlike a register, shared memory is read and write like any other addressable memory: any thread in the block can read a value any other thread in the block wrote, once both have passed a point where the hardware guarantees the write is visible ([G6](g6-synchronization.md) covers that point, the **barrier**).

The first worked example turns the two budgets above into a table: for a few tile sizes of a tiled matrix product ([G10](g10-matmul-ladder.md) builds the real kernel), how many copies of that tile fit in one block's shared-memory budget on each chip.

--8<-- "includes/examples/gpu/g3-memory-hierarchy/block_budgets.cpp.md"

The pattern holds everywhere it appears: a 64 x 64 tile (16 KB per operand, 32 KB for two) fills the entire Apple GPU budget and leaves no room for a second block's tile at that size on the same core, while the same tile is a small fraction of Hopper's larger budget. A tile size that suits one chip can be too large for another, which is exactly the kind of fact a Vortex compiler's target description has to carry rather than assume.

## Caches: what the hardware fills for you

Below shared memory and above device memory sit **caches**: on-chip memory the hardware fills automatically from whatever the running threads most recently read, with no explicit store instruction naming it. NVIDIA's programming guide places an **L1 cache** per SM, and (on the architectures it covers) shares its physical storage with that SM's shared memory, so a kernel can trade capacity between the two;[^n2] an **L2 cache** sits below that, shared by every SM on the device and the point where the whole chip agrees on the current value of a memory location.[^n2] AMD's hardware guide gives its L2 the same role: a coherence point visible to every compute unit.[^a2]

The difference from shared memory is control. A programmer or compiler decides what shared memory or threadgroup memory holds and when; a cache decides for itself, based on what was read recently, and can evict a value a kernel still wants before it is read again. That difference matters for [G4](g4-memory-performance.md), where whether an access lands in cache or not decides much of what coalescing has to fix, and it is why this chapter cannot give useful capacity numbers for GPU caches the way it did for shared memory: L1 capacity is often configurable per kernel launch, trading against shared memory, and no single figure describes it.

## Device memory, and the address space each value lives in

At the base of the hierarchy is **device memory**: the pool every thread on the GPU can reach, backed off-chip by GDDR or HBM DRAM on a discrete card,[^n2] and, on a system-on-chip like Apple silicon, by the same DRAM the CPU uses (the next section returns to what that sharing changes). Every array a kernel is given a pointer to, before any tiling or caching, starts here. It is also the largest pool by a wide margin: shared memory and registers are counted in tens of kilobytes per block or per thread; device memory is counted in gigabytes.

A kernel language makes the level a value lives in part of its type, not only a fact about performance. The Metal Shading Language declares four **address spaces** for a pointer or reference: `device`, a pointer into the device memory pool, read and write by every thread; `constant`, a read-only pointer into device memory, for data every thread reads but none writes; `thread`, a value private to the thread that names it (a register, spilling to device memory only if the compiler cannot keep it in one); and `threadgroup`, a value shared by every thread in one threadgroup.[^ap1] CUDA and HIP make the same four distinctions with keywords and defaults instead of a type qualifier: a plain pointer parameter is `device`-like global memory, `__constant__` names constant memory, a local variable is thread-private, and `__shared__` (CUDA) or `__shared__` (HIP, following CUDA's spelling) names a threadgroup-like allocation.[^n2] MLIR's `gpu` dialect represents the same distinction structurally: a kernel function's ordinary memref arguments are global, and it declares extra buffers as **memory attributions**, each typed with an explicit `#gpu.address_space<...>`: `workgroup` for a buffer the whole block shares, `private` for one the compiler allocates per thread.[^m16]

The second example shows both attributions in one kernel. A thread doubles a value it alone needs, keeping it in a `private` buffer, then stages a copy into a `workgroup` buffer so a different thread, at a different lane, can read it after a barrier: exactly the register-versus-shared-memory choice from the opening example, written as two address spaces rather than left implicit.

--8<-- "includes/examples/gpu/g3-memory-hierarchy/address_spaces.mlir.md"

??? check "Which address space would `a[row, k]` need if a tiled kernel first copies a tile of `a` into shared memory, and every thread in the block reads its own element from that copy?"

    `workgroup` (Metal: `threadgroup`). Every thread in the block reads from the tile, including threads other than the one that copied any given element in, so the tile cannot be a `private` or `thread` value; it must be visible to the whole block.

<figure class="vx-figure">
<svg viewBox="0 0 720 460" role="img" aria-label="A pyramid showing four levels of GPU memory, smallest and fastest at the top" aria-describedby="g3-f1-desc">
<title id="g3-f1-title">A pyramid showing four levels of GPU memory, smallest and fastest at the top</title>
<desc id="g3-f1-desc">Four stacked trapezoids, narrowest at the top and widest at the bottom. Top: registers, owned by one thread. Second: shared memory (CUDA), LDS (HIP), threadgroup memory (Metal), owned by one block. Third: L1 and L2 caches, filled automatically by hardware. Bottom: device memory, GDDR or HBM, or on Apple silicon the same DRAM the CPU uses, owned by the whole device. A label on the right reads capacity grows and speed falls moving down; a label on the left reads visibility grows moving down: one thread, one block, one core's history, the whole device.</desc>
<text class="vx-text" x="360" y="26" text-anchor="middle">Registers &#8594; shared memory &#8594; caches &#8594; device memory</text>

<polygon class="vx-box-strong" points="300,50 420,50 440,110 280,110"/>
<text class="vx-text" x="360" y="76" text-anchor="middle">registers</text>
<text class="vx-mono" x="360" y="94" text-anchor="middle">one thread</text>

<polygon class="vx-box-accent" points="270,120 450,120 480,190 240,190"/>
<text class="vx-text" x="360" y="148" text-anchor="middle">shared memory / LDS / threadgroup memory</text>
<text class="vx-mono" x="360" y="166" text-anchor="middle">one block</text>
<text class="vx-text-muted" x="360" y="182" text-anchor="middle">227 KB per block (Hopper) &#183; 32 KB per threadgroup (Apple, measured)</text>

<polygon class="vx-box" points="230,200 490,200 520,280 200,280"/>
<text class="vx-text" x="360" y="234" text-anchor="middle">L1 and L2 caches</text>
<text class="vx-mono" x="360" y="252" text-anchor="middle">filled by hardware</text>
<text class="vx-text-muted" x="360" y="268" text-anchor="middle">no fixed capacity: L1 often shares its budget with shared memory</text>

<polygon class="vx-box" points="180,290 540,290 580,390 140,390"/>
<text class="vx-text" x="360" y="330" text-anchor="middle">device memory (GDDR / HBM, or unified DRAM)</text>
<text class="vx-mono" x="360" y="350" text-anchor="middle">the whole device</text>
<text class="vx-text-muted" x="360" y="368" text-anchor="middle">gigabytes: every array starts here</text>

<line class="vx-line" x1="600" y1="60" x2="600" y2="380"/>
<polygon class="vx-arrowhead" points="600,380 595,368 605,368"/>
<text class="vx-text-muted" x="612" y="70">smaller,</text>
<text class="vx-text-muted" x="612" y="84">faster</text>
<text class="vx-text-muted" x="612" y="360">larger,</text>
<text class="vx-text-muted" x="612" y="374">slower</text>

<line class="vx-line" x1="120" y1="60" x2="120" y2="380"/>
<text class="vx-text-muted" x="30" y="70">visible to</text>
<text class="vx-text-muted" x="30" y="84">one thread</text>
<text class="vx-text-muted" x="30" y="360">visible to</text>
<text class="vx-text-muted" x="30" y="374">whole device</text>
</svg>
<figcaption>Four levels of GPU memory, narrowest and fastest at the top. Capacity and visibility both grow moving down; the two cited numbers (Hopper's 227 KB per block, Apple's measured 32 KB per threadgroup) sit at the level this chapter can give exact figures for.</figcaption>
</figure>

## Unified memory on Apple silicon

Everything above the last row of the pyramid is the same story on any GPU: registers, shared memory, caches, and a pool of device memory the chip alone can reach. The last row is where Apple silicon changes the story. A discrete GPU has its own DRAM, physically separate from the CPU's: getting an array from one to the other means an explicit copy across a bus. Apple silicon's GPU and CPU instead share one pool of DRAM and, WWDC22 states, the same last-level cache and system-level cache; on the owner's Apple M4 Pro, `MTLDevice.hasUnifiedMemory` reads `true` (measured 2026-09-23).[^ap9] There is no separate GPU DRAM to copy into.

Sharing the same physical memory does not by itself mean every buffer is visible to both processors: Metal still asks the programmer to choose a **storage mode** per resource, and the mode, not the chip's physical layout, decides who may address a buffer's bytes. Apple's guide to choosing a storage mode lists `shared`, one copy of the data that both the CPU and the GPU can address, appropriate on Apple silicon precisely because the underlying memory already is unified; `private`, a GPU-only copy the CPU has no address for at all, chosen when only the GPU ever touches a resource; and `memoryless`, storage that exists only while a render pass uses it and is never backed by memory beyond the tile itself.[^ap6] The third worked example turns that rule into a table.

--8<-- "includes/examples/gpu/g3-memory-hierarchy/storage_modes.cpp.md"

??? check "A kernel writes a result buffer that the CPU needs to read immediately afterward, with no further GPU work planned for it. Which storage mode fits, and why would `private` be the wrong choice even though it exists on the same unified DRAM?"

    `shared`. `private` denies the CPU an address for the buffer at all, regardless of whether the underlying DRAM is physically unified: the storage mode is Metal's own access rule, not a description of the hardware, and choosing it for data the CPU must read would make that read impossible without an extra GPU-side copy back into a `shared` buffer.

This is the same idea Vortex already states for the CPU: a mutable reference gives its holder exclusive access, and [decision 25](../decisions/references.md#d25) forbids the array lent as a function's `&mut` output from appearing as any other argument in the same call, precisely so nothing else can read or write it mid-computation. Metal's storage modes are the same rule at a coarser grain, decided once per buffer instead of once per call: `shared` says "more than one party may need this," `private` says "exactly one party, the GPU, ever will." A Vortex compiler choosing where a GPU kernel's inputs and `&mut` output should live is answering the same kind of question this section asks of Metal, for a target where the answer might be free (unified memory) or might cost an explicit transfer (a discrete GPU).

## Key ideas

!!! recap "Questions you can now answer"

    - **What is a register, and why is it the fastest place to hold a value?** Storage private to one thread, addressed by name in an instruction rather than by a memory address, drawn from a fixed-size per-core pool shared by every resident thread.
    - **What do CUDA shared memory, HIP's LDS and Metal's threadgroup memory have in common?** The same physical idea under three names: on-chip scratchpad memory owned by one block of threads, filled and read explicitly, with a fixed capacity that this chapter gave exact, cited numbers for on two chips.
    - **How does shared memory differ from a cache?** A cache is filled by hardware based on recent accesses and can evict a wanted value; shared memory holds only what a kernel explicitly stores there, and it stays until the kernel overwrites or the block ends.
    - **What is device memory, and why is every array there to start with?** The large, off-chip (or, on Apple silicon, unified) pool visible to the whole GPU; every pointer a kernel receives names a location in it before any tiling or caching narrows things down.
    - **What does "unified memory" mean on Apple silicon, and does it make every buffer visible to the CPU?** The CPU and GPU share the same physical DRAM, but a buffer's storage mode (`shared`, `private` or `memoryless`) still decides which processor may address it; unification removes the need for a copy, not the access-control choice itself.
    - **Which MLIR `gpu`-dialect address space would a tile shared by a whole block use?** `workgroup`, declared as a memory attribution on the kernel function; a value private to one thread uses `private` instead.

## For Vortex

!!! vortex "Exercise"

    **Build** a target description and a placement rule for the values a GPU-generating Vortex back end would need to place, without generating any GPU code yet.

    1. A struct (or its written-down field list, if you are not yet ready to code it) naming, per target, the facts this chapter used: shared-memory or threadgroup-memory bytes per block, and whether the target has unified host/device memory. Fill it for the two chips this chapter cited numbers for, and mark every field you have not measured or cited as unknown rather than guessing.
    2. A rule, given a Vortex value inside a loop nest generated for a GPU kernel (a scalar accumulator like `sum` above, an array slice read by one thread only, or an array slice read by every thread in a block), that assigns it one of: private/register, workgroup/shared, or device/global. State the rule in words first (for example, "a value read by more than one thread of the same block, and written by at most one of them, goes in shared memory"); do not write the code generator itself.
    3. For [stage 10](../compiler/guide/stage-10-matrix-multiplication.md)'s `multiply` signature, `fn multiply(a: &[f32; 2, 3], b: &[f32; 3, 2], c: &mut [f32; 2, 2])`, work through your rule by hand for a hypothetical GPU version that tiles `a` and `b` through shared memory: which of `a`, `b`, `c` and `sum` lands in which of the three places, and why the `&mut` on `c` matters for whether it could ever be shared.
    4. A one-paragraph note on what changes for your placement rule on a unified-memory target versus a discrete one, without deciding yet whether Vortex will target either ([M12](../mlir/m12-vortex-gpu-path.md) covers that decision).

    **Not yet:** emitting GPU code in any form ([M12](../mlir/m12-vortex-gpu-path.md) weighs the paths first), coalescing or bank-conflict analysis ([G4](g4-memory-performance.md)), occupancy accounting that trades registers against resident threads ([G5](g5-occupancy.md)), and barriers or cross-thread synchronization ([G6](g6-synchronization.md)).

    **Proof that it works:** a written table, one row per value in the `multiply` example, each row naming a placement and a one-sentence reason drawn from your rule; and a second table for a reduction kernel (many threads producing one shared sum) showing that your rule correctly refuses to place the partial sums in registers alone, since a register is invisible outside its own thread.

## Where this comes back

!!! next "You will use this again in"

    - [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md): *shared memory*, *bank*, *threadgroup memory*
    - [G5. Occupancy and latency hiding](g5-occupancy.md): *registers*, *register spilling*, *shared memory budget*
    - [G6. Synchronization, atomics and reductions](g6-synchronization.md): *shared memory*, *barrier*
    - [G10. The GPU matmul ladder](g10-matmul-ladder.md): *shared memory tile*, *device memory*
    - [M10. MLIR for GPUs](../mlir/m10-mlir-for-gpus.md): *workgroup address space*, *memory attribution*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *unified memory*, *address space*

## Sources and further reading

Read the CUDA Programming Guide's kernel and memory sections first, then the HIP and Metal pages for the vocabulary each uses for the same ideas.

[^n2]: NVIDIA, "CUDA Programming Guide", v13.4, "Writing SIMT Kernels" (thread hierarchy and memory spaces: registers, local, shared, constant and global memory; L1/L2 caches sharing SM storage with shared memory). <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html>
[^n10]: NVIDIA, "Hopper Tuning Guide": the SM register file (65,536 32-bit registers, 255 per thread) and shared memory (228 KB per SM, up to 227 KB addressable per thread block). <https://docs.nvidia.com/cuda/hopper-tuning-guide/index.html>
[^a2]: AMD, "Hardware implementation", HIP 7.15.0 documentation (SGPRs and VGPRs, the local data share, and the L2 cache as a device-wide coherence point). <https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html>
[^ap1]: Apple, "Metal Shading Language Specification", version 4.1, section 4, "Address Space Qualifiers" (`device`, `constant`, `thread` and `threadgroup`). <https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf>
[^ap2]: Apple, "Metal Feature Set Tables" (32 KB of threadgroup memory for the Apple4 GPU family onward). Cross-checked against `MTLDevice.maxThreadgroupMemoryLength`, which read 32768 on the owner's Apple M4 Pro, macOS 27.0, on 2026-09-23. <https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf>
[^ap6]: Apple, "Choosing a resource storage mode for Apple GPUs" (the `shared`, `private` and `memoryless` storage modes and who may address each). <https://developer.apple.com/documentation/metal/choosing-a-resource-storage-mode-for-apple-gpus>
[^ap9]: Apple, WWDC22, "Scale compute workloads across Apple GPUs" (unified memory, and the shared last-level and system-level caches). `MTLDevice.hasUnifiedMemory` read `true` on the owner's Apple M4 Pro, macOS 27.0, on 2026-09-23. <https://developer.apple.com/videos/play/wwdc2022/10159/>
[^m16]: MLIR Project, "'gpu' Dialect", section "Memory attribution" (`workgroup` and `private` address spaces on a `gpu.func`). <https://mlir.llvm.org/docs/Dialects/GPU/>
