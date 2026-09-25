# G8. ISAs and IRs

<p class="page-intro">A GPU compiler has to hand its work to someone: a driver, an assembler, another compiler. This chapter reads the formats each vendor accepts, NVIDIA's PTX and SASS, AMD's published machine code, Khronos's SPIR-V and Apple's Metal Shading Language, far enough to know which one a Vortex back end could emit and what each one obliges it to write.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 40 minutes · Builds on: [G7. Programming models tour](g7-programming-models.md)</p>

???+ remember "Before you start, remember"

    ??? question "Why does a whole warp execute one instruction at a time, even when its lanes take different branches?"

        The hardware does not give each thread its own instruction stream. It groups threads (NVIDIA's warp, AMD's wavefront, Apple's SIMD-group) and issues one instruction to the whole group; lanes whose branch went the other way are switched off for those instructions and switched back on where the paths meet.

        Introduced in [G2. The SIMT execution model](g2-simt.md).

    ??? question "Where does the group size live in Vulkan and WebGPU, compared with CUDA and Metal?"

        Inside the compiled kernel (`LocalSize` in SPIR-V, `@workgroup_size` in WGSL), where CUDA, OpenCL and Metal give it at the launch.

        Introduced in [G7. Programming models tour](g7-programming-models.md).

    ??? question "What does a phi instruction do?"

        It picks a value according to which predecessor block control came from. It is how SSA form merges the values that two sides of a branch computed for the same variable.

        Introduced in [O3. SSA form: construction and destruction](../optimize/o3-ssa.md).

    ??? question "May a Vortex compiler fuse the multiply and the add in `sum += a[row, k] * b[k, column]` into one rounding?"

        No. Each floating-point operation is one IEEE 754 operation, rounded once, and must not be contracted, reassociated or reordered, on any target.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Explain what an instruction set architecture promises, and what makes one *virtual*.
    - Trace how a CUDA program carries PTX and machine code for several GPU generations, and which one the driver runs.
    - Follow an if/else through AMD's execution mask by hand, and say why that mask is the compiler's job on AMD hardware.
    - Decode SPIR-V instructions from their 32-bit words, and state when SPIR-V requires structured control flow.
    - Name, for each vendor, the lowest rung a compiler outside that vendor can target with a written specification, and what that rung obliges it to write to keep decision 56.

## One kernel, three texts

Take the smallest kernel worth naming: a one-sided clamp, `y[i] = max(x[i], 0)`, one comparison and one store per element. It stays the same through the whole chapter.

First, in Metal Shading Language (MSL), the text Apple documents:

--8<-- "includes/examples/gpu/g8-isas-and-irs/relu.metal.md"

Second, written in MLIR's vendor-neutral `gpu` dialect and lowered toward NVIDIA's `nvvm` dialect. That dialect becomes LLVM IR with NVIDIA-specific intrinsics, the input of LLVM's NVPTX back end:[^nvvm-dialect][^nvptx]

--8<-- "includes/examples/gpu/g8-isas-and-irs/relu_nvvm.mlir.md"

Third, the same source lowered toward `spirv`, MLIR's dialect for Khronos's SPIR-V:[^mlir-spirv]

--8<-- "includes/examples/gpu/g8-isas-and-irs/relu_spirv.mlir.md"

The MSL file is checked as text only: Apple's offline compiler needs a Metal toolchain component that this machine does not have, so the harness skips it. The other two are real `mlir-opt` 18 runs, and their output above is what the passes printed.

Read the three side by side and the same four facts appear in three vocabularies: where a thread is (`thread_position_in_grid`, `nvvm.read.ptx.sreg.tid.x`, a load from `LocalInvocationId`), which memory a pointer means (`device`, a plain LLVM pointer, `StorageBuffer`), the comparison (`max`, `fcmp "ogt"`, `FOrdGreaterThan`) and the branch. The branch is where the last two differ most: the NVVM version is a plain conditional branch, and the SPIR-V version wraps the same branch in a `spirv.mlir.selection` region. By the end of this chapter you can say why.

## What an ISA promises

An **instruction set architecture (ISA)** is the contract between software and a processor: which instructions exist, what each one does to registers and memory, and how each is **encoded**, that is, which bits in memory stand for it. The contract is what lets a program compiled today run on a chip built next year, as long as the new chip implements the same ISA. AArch64 from [A1](../backend/a1-machine-model.md) is one, and any compiler writer may read its manual.

GPU vendors split this contract in two. The chips change their machine code from one generation to the next, and vendors do not want every application to be recompiled for each new chip. So some of them publish a second, stable format above the machine code: a **virtual ISA**. A virtual ISA looks like an instruction set (registers, typed instructions, loads and stores) and describes a computation completely, but no chip decodes it. A further compiler, run by the developer ahead of time or by the driver when the program starts, translates it into the machine code of whichever GPU is installed.

NVIDIA's reference for PTX states the idea in its first sentence: PTX is "a low-level parallel thread execution virtual machine and instruction set architecture".[^ptx-isa] Its stated goals include "a stable ISA that spans multiple GPU generations" and "a machine-independent ISA for C/C++ and other compilers to target".[^ptx-isa] Those two goals pull in one direction: the stable layer is the one a compiler outside NVIDIA should emit.

Not every vendor adds this layer, and not every vendor documents what it has. The rest of the chapter reads four vendors' **ladders**, from source text at the top to machine code at the bottom, and asks one question of each rung: is there a written specification a compiler outside the vendor can target?

## NVIDIA: PTX is the contract, SASS is the product

### PTX as a language

A PTX module is text. It begins with a `.version` directive naming the PTX language version and a `.target` directive naming the GPU architecture it assumes; kernels are `.entry` functions.[^ptx-isa] Three features separate it from the assembly languages of the back end book.

**State spaces.** Every variable in PTX lives in a named **state space**, a storage area with its own size, speed and sharing rules: `.reg` for registers, `.global` for device memory, `.shared` for the per-block scratchpad from [G3](g3-memory-hierarchy.md), `.param` for kernel arguments, `.local` for per-thread memory, and others.[^ptx-isa] Loads and stores name the space they use: `ld.global` reads device memory, `st.shared` writes the scratchpad. The NVPTX back end represents the same spaces as numbered LLVM address spaces (1 for global, 3 for shared, 5 for local).[^nvptx] [G9](g9-gpu-compilers-in-llvm.md) shows what an optimizer does with them.

**Typed registers, allocated later.** PTX registers are declared with a type, for example `.f32`, `.u32` or the 1-bit `.pred`. The number of physical registers is limited and differs between GPUs; when a program uses more, the PTX reference says, register variables are spilled to memory.[^ptx-isa] Choosing which physical register holds each value, the problem of [C3](../backend/c3-linear-scan.md), is left to the step below PTX: `ptxas`, NVIDIA's optimizing assembler, or the driver.[^ptx-isa]

**Predicates.** A comparison writes a **predicate register**, one bit per thread, and any instruction can carry a guard predicate, written `@p`, that decides whether it takes effect.[^ptx-isa] `setp.gt.f32 p, a, b` sets `p` where `a > b`; an `@p bra` branches only where `p` is set.

That is enough to read what the NVVM listing will become. The table maps its lines to PTX by construct; the exact instructions depend on the NVPTX back end's choices.

| Line in `relu_nvvm` | PTX construct it becomes |
| --- | --- |
| `nvvm.read.ptx.sreg.tid.x`, `ctaid.x`, `ntid.x` | special registers `%tid.x`, `%ctaid.x`, `%ntid.x` (thread in block, block in grid, block size)[^ptx-isa] |
| `llvm.load` from the `x` pointer | a load naming a state space, `ld.global.f32` once the back end knows the pointer is global |
| `llvm.fcmp "ogt"` | `setp.gt.f32`, an ordered comparison (false if either input is NaN) writing a `.pred` register |
| `llvm.cond_br` | `@p bra`, or no branch at all if the back end chooses a select (`selp`) |
| `llvm.store` to `y` | `st.global.f32` |

### Two stages, and a fat binary

The ladder has two compilation stages, and `nvcc`'s documentation names them. The first stage compiles CUDA C++ for a **virtual architecture**, written `compute_80`, `compute_90` and so on, and produces PTX. The second stage compiles that PTX for a **real architecture**, written `sm_80`, `sm_86`, `sm_90`, and produces **SASS**, the machine code a specific GPU runs.[^nvcc] PTX's own `.target` rule makes the first stage forward-compatible: GPU generations follow what the reference calls an "onion layer model", each adding features and keeping the old ones, so PTX written for one target runs on later generations.[^ptx-isa] The exception is a target with an `a` suffix, such as `sm_90a`, which enables features of that one architecture and gives up running on later ones.[^ptx-isa]

SASS is less portable. The `nvcc` guide's example: code compiled for `sm_80` runs on all Ampere and Ada GPUs, while `sm_89` would probably yield better code if Ada GPUs are the only targets.[^nvcc] Neither runs on a GPU of a later generation.

The two mechanisms that close that gap are **just-in-time (JIT) compilation** and the **fat binary**. If a program carries PTX, the CUDA driver can compile it for the installed GPU when the program starts, at the cost of a longer start, which a persistent compilation cache reduces.[^nvcc] A fat binary carries several translations of the same kernel, and at launch the driver selects the most appropriate one.[^nvcc] Figure 1 follows the `nvcc` guide's own example, `--gpu-architecture=compute_80 --gpu-code=compute_80,sm_86,sm_89`: machine code for two GPUs, plus PTX for any GPU that comes later.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="A fat binary holding SASS for sm_86, SASS for sm_89 and PTX for compute_80; an sm_86 GPU and an sm_89 GPU each run their own SASS, and a later sm_90 GPU has the driver compile the PTX just in time">
<text class="vx-text" x="20" y="26">nvcc x.cu --gpu-architecture=compute_80 --gpu-code=compute_80,sm_86,sm_89</text>
<rect class="vx-box" x="20" y="50" width="260" height="250" rx="6"/>
<text class="vx-text-accent" x="150" y="76" text-anchor="middle">fat binary in the program</text>
<rect class="vx-box-strong" x="40" y="96" width="220" height="50" rx="4"/>
<text class="vx-text" x="150" y="118" text-anchor="middle">SASS for sm_86</text>
<text class="vx-text-muted" x="150" y="136" text-anchor="middle">machine code, one generation</text>
<rect class="vx-box-strong" x="40" y="160" width="220" height="50" rx="4"/>
<text class="vx-text" x="150" y="182" text-anchor="middle">SASS for sm_89</text>
<text class="vx-text-muted" x="150" y="200" text-anchor="middle">machine code, one generation</text>
<rect class="vx-box-accent" x="40" y="224" width="220" height="50" rx="4"/>
<text class="vx-text" x="150" y="246" text-anchor="middle">PTX for compute_80</text>
<text class="vx-text-muted" x="150" y="264" text-anchor="middle">virtual ISA, later GPUs too</text>
<line class="vx-line" x1="260" y1="121" x2="520" y2="121"/>
<polygon class="vx-arrowhead" points="520,116 528,121 520,126"/>
<line class="vx-line" x1="260" y1="185" x2="520" y2="185"/>
<polygon class="vx-arrowhead" points="520,180 528,185 520,190"/>
<line class="vx-line vx-flow" x1="260" y1="249" x2="520" y2="249"/>
<polygon class="vx-arrowhead" points="520,244 528,249 520,254"/>
<text class="vx-text-muted" x="394" y="113" text-anchor="middle">loaded as is</text>
<text class="vx-text-muted" x="394" y="177" text-anchor="middle">loaded as is</text>
<text class="vx-text-muted" x="394" y="241" text-anchor="middle">driver compiles it at start</text>
<rect class="vx-box" x="530" y="96" width="210" height="50" rx="4"/>
<text class="vx-text" x="635" y="126" text-anchor="middle">an sm_86 GPU</text>
<rect class="vx-box" x="530" y="160" width="210" height="50" rx="4"/>
<text class="vx-text" x="635" y="190" text-anchor="middle">an sm_89 GPU</text>
<rect class="vx-box" x="530" y="224" width="210" height="50" rx="4"/>
<text class="vx-text" x="635" y="246" text-anchor="middle">an sm_90 GPU</text>
<text class="vx-text-muted" x="635" y="264" text-anchor="middle">newer than the binary</text>
</svg>
<figcaption>Figure 1. What a fat binary holds and what the driver does with it, for the <code>nvcc</code> guide's example command. A GPU with matching machine code runs it directly; a GPU newer than every piece of machine code gets the PTX, compiled just in time. Without the PTX entry, the program would not run on the sm_90 GPU at all.</figcaption>
</figure>

??? check "A library ships only `sm_89` machine code, no PTX. A user installs it on a GPU from a later generation. What happens, and what one change to the build would have avoided it?"

    The driver finds no translation it can use: SASS is tied to its generation, and there is no PTX to compile just in time, so the kernel cannot be loaded. Adding a PTX entry (for example `compute_89` in the `--gpu-code` list) lets the driver compile the kernel for the new GPU when the program starts.

### SASS: listed, not specified

SASS has no reference in the sense that PTX does. What NVIDIA publishes is the CUDA Binary Utilities: `cuobjdump`, which dumps the SASS, PTX and ELF sections embedded in a program, and `nvdisasm`, which disassembles machine code and can print its control-flow graph and register live ranges.[^cuda-binutils] Their "Instruction Set Reference" is a table per architecture family (Turing, Ampere and Ada, Hopper, Blackwell and Rubin) giving each opcode a one-line description: `LDG` loads from global memory, `LDS` from shared memory, `STG` stores to global memory.[^cuda-binutils] There is no encoding, no operand semantics and no promise that the next generation keeps an opcode.

That is enough to read a listing, which is how performance engineers use SASS: to check that a load became one wide `LDG` rather than four narrow ones, or that a multiply and an add became one `FFMA`. It is not enough to write SASS, and a compiler outside NVIDIA has no business trying. Its lowest documented rung is PTX.

### PTX and decision 56

PTX has a rule that matters directly for Vortex. An `add.f32` or `mul.f32` written without a rounding modifier rounds to nearest even, but the PTX reference says such instructions "may be optimized aggressively": a multiply followed by an add, both without modifiers, may become one fused multiply-add.[^ptx-isa] An instruction with an explicit rounding modifier, such as `add.rn.f32`, is treated conservatively.[^ptx-isa]

So `add.f32` and `add.rn.f32` round the same way when each runs alone, but only the second promises to stay alone. A fused multiply-add rounds once where [decision 56](../decisions/numbers.md#d56) requires two roundings, so a Vortex back end that emits PTX, directly or through LLVM, must end up with the explicit modifier on every floating-point add and multiply. [G7](g7-programming-models.md#what-none-of-this-changes) found the same default one level up, in `nvcc`'s `--fmad` option; here it is in the ISA itself.

## AMD: the machine code is the published contract

AMD takes the other road. It publishes the ISA of the real hardware: manuals for every RDNA generation (RDNA, RDNA 2, 3, 3.5 and 4) and every CDNA generation (CDNA 1 to 5), with older manuals going back to the Radeon R600, plus the same data as machine-readable XML files and a C++ decoder library.[^amd-isa] AMD's page says what the manuals are for: specifying each instruction "in both text syntax and binary format", and giving compiler writers a reference of what each instruction does.[^amd-isa]

With the real ISA public, there is no virtual layer to publish. HIP and OpenCL compilers go from source through LLVM IR to LLVM's AMDGPU back end, which emits machine code into an ELF **code object** for one named processor such as `gfx90a` or `gfx1100`.[^amdgpu-backend] LLVM IR sits in the middle of that path as the compiler's working form, not as a format AMD asks anyone to ship. The cost of skipping the virtual layer is compatibility: a code object for one processor does not run on another. LLVM also defines **generic processors** (from code object version 6), targets whose single code object runs on a family of processors, possibly more slowly, which recovers part of what PTX gives NVIDIA.[^amdgpu-backend]

### Two kinds of register, and a mask

AMD's ISA shows a design choice that NVIDIA's PTX hides. A wavefront, 32 or 64 lanes wide depending on the processor and the compile option,[^amdgpu-backend] has two kinds of general-purpose register. A **vector register (VGPR)** holds one value per lane, like the registers G2 described. A **scalar register (SGPR)** holds one value for the whole wavefront; the AMDGPU guide accordingly counts vector registers per work-item and scalar registers per wavefront.[^amdgpu-backend] A value the compiler can prove is the same in every lane, such as a loop counter or a base address, lives in an SGPR and costs one register, not 32 or 64. [G9](g9-gpu-compilers-in-llvm.md#uniformity-one-value-per-warp-or-one-per-lane) explains how the compiler proves it.

The wavefront follows one instruction stream, and a register named **EXEC**, the **execution mask**, holds one bit per lane (32 or 64 bits, matching the wavefront): the hardware uses it to decide which lanes are active.[^amdgpu-backend] On AMD hardware the compiled code sets EXEC itself. The AMDGPU user guide gives the pattern for an if/else, in pseudo code: compute a mask of the lanes where the condition holds; run the THEN side with EXEC set to the saved mask AND the condition; run the ELSE side with EXEC set to the complement of the current mask AND the saved mask; then restore the saved mask.[^amdgpu-backend] The example runs that pattern on eight lanes:

--8<-- "includes/examples/gpu/g8-isas-and-irs/exec_mask.cpp.md"

Follow it by hand. Lane 0 is the rightmost digit. The inputs are `3, -1, 4, -1, -5, 9, 2, -6` for lanes 0 to 7, so the condition `x > 0` holds in lanes 0, 2, 5 and 6: reading lane 7 down to lane 0, `01100101`. All eight lanes entered, so the saved mask is `11111111`, and the THEN mask is `11111111 & 01100101 = 01100101`. Those four lanes copy `x` into `y`. For the ELSE side, the complement of the current mask is `10011010`, AND the saved mask gives `10011010`: the other four lanes, which write zero. Restoring `11111111` lets every lane continue.

Figure 2 draws the same four masks. Both sides of the branch are issued to the whole wavefront, one after the other; the mask decides which lanes feel each one.

<figure class="vx-figure">
<svg viewBox="0 0 760 250" role="img" aria-label="Four rows of eight lane cells for the steps enter, then, else and leave. In enter and leave all eight lanes are active. In then, lanes 0, 2, 5 and 6 are active. In else, lanes 1, 3, 4 and 7 are active.">
<text class="vx-text-muted" x="250" y="24" text-anchor="middle">lane 7</text>
<text class="vx-text-muted" x="600" y="24" text-anchor="middle">lane 0</text>
<text class="vx-text" x="20" y="61">enter</text>
<text class="vx-text" x="20" y="111">then: saved &amp; cond</text>
<text class="vx-text" x="20" y="161">else: ~EXEC &amp; saved</text>
<text class="vx-text" x="20" y="211">leave: saved</text>
<rect class="vx-box" x="230" y="40" width="40" height="32"/><rect class="vx-cell-on" x="230" y="40" width="40" height="32"/>
<rect class="vx-box" x="280" y="40" width="40" height="32"/><rect class="vx-cell-on" x="280" y="40" width="40" height="32"/>
<rect class="vx-box" x="330" y="40" width="40" height="32"/><rect class="vx-cell-on" x="330" y="40" width="40" height="32"/>
<rect class="vx-box" x="380" y="40" width="40" height="32"/><rect class="vx-cell-on" x="380" y="40" width="40" height="32"/>
<rect class="vx-box" x="430" y="40" width="40" height="32"/><rect class="vx-cell-on" x="430" y="40" width="40" height="32"/>
<rect class="vx-box" x="480" y="40" width="40" height="32"/><rect class="vx-cell-on" x="480" y="40" width="40" height="32"/>
<rect class="vx-box" x="530" y="40" width="40" height="32"/><rect class="vx-cell-on" x="530" y="40" width="40" height="32"/>
<rect class="vx-box" x="580" y="40" width="40" height="32"/><rect class="vx-cell-on" x="580" y="40" width="40" height="32"/>
<rect class="vx-box" x="230" y="90" width="40" height="32"/>
<rect class="vx-box" x="280" y="90" width="40" height="32"/><rect class="vx-cell-on" x="280" y="90" width="40" height="32"/>
<rect class="vx-box" x="330" y="90" width="40" height="32"/><rect class="vx-cell-on" x="330" y="90" width="40" height="32"/>
<rect class="vx-box" x="380" y="90" width="40" height="32"/>
<rect class="vx-box" x="430" y="90" width="40" height="32"/>
<rect class="vx-box" x="480" y="90" width="40" height="32"/><rect class="vx-cell-on" x="480" y="90" width="40" height="32"/>
<rect class="vx-box" x="530" y="90" width="40" height="32"/>
<rect class="vx-box" x="580" y="90" width="40" height="32"/><rect class="vx-cell-on" x="580" y="90" width="40" height="32"/>
<rect class="vx-box" x="230" y="140" width="40" height="32"/><rect class="vx-cell-on" x="230" y="140" width="40" height="32"/>
<rect class="vx-box" x="280" y="140" width="40" height="32"/>
<rect class="vx-box" x="330" y="140" width="40" height="32"/>
<rect class="vx-box" x="380" y="140" width="40" height="32"/><rect class="vx-cell-on" x="380" y="140" width="40" height="32"/>
<rect class="vx-box" x="430" y="140" width="40" height="32"/><rect class="vx-cell-on" x="430" y="140" width="40" height="32"/>
<rect class="vx-box" x="480" y="140" width="40" height="32"/>
<rect class="vx-box" x="530" y="140" width="40" height="32"/><rect class="vx-cell-on" x="530" y="140" width="40" height="32"/>
<rect class="vx-box" x="580" y="140" width="40" height="32"/>
<rect class="vx-box" x="230" y="190" width="40" height="32"/><rect class="vx-cell-on" x="230" y="190" width="40" height="32"/>
<rect class="vx-box" x="280" y="190" width="40" height="32"/><rect class="vx-cell-on" x="280" y="190" width="40" height="32"/>
<rect class="vx-box" x="330" y="190" width="40" height="32"/><rect class="vx-cell-on" x="330" y="190" width="40" height="32"/>
<rect class="vx-box" x="380" y="190" width="40" height="32"/><rect class="vx-cell-on" x="380" y="190" width="40" height="32"/>
<rect class="vx-box" x="430" y="190" width="40" height="32"/><rect class="vx-cell-on" x="430" y="190" width="40" height="32"/>
<rect class="vx-box" x="480" y="190" width="40" height="32"/><rect class="vx-cell-on" x="480" y="190" width="40" height="32"/>
<rect class="vx-box" x="530" y="190" width="40" height="32"/><rect class="vx-cell-on" x="530" y="190" width="40" height="32"/>
<rect class="vx-box" x="580" y="190" width="40" height="32"/><rect class="vx-cell-on" x="580" y="190" width="40" height="32"/>
<text class="vx-text-muted" x="640" y="111">copy x</text>
<text class="vx-text-muted" x="640" y="161">write 0</text>
</svg>
<figcaption>Figure 2. The execution mask through one if/else, for the example's eight lanes (filled cell: lane active). The THEN and ELSE rows are complements within the lanes that entered, so every lane runs exactly one side, and the wavefront spends time on both.</figcaption>
</figure>

This only works if the branch nests: the code needs one saved mask per open region, restored in reverse order. An arbitrary control-flow graph, with jumps into the middle of another branch's region, has no such stack. That is why LLVM's AMDGPU back end first rewrites control flow into nested regions with a pass called StructurizeCFG, which [G9](g9-gpu-compilers-in-llvm.md#structured-control-flow-structurizecfg-and-spir-v) opens. A compiler that emits LLVM IR for AMD does not have to structure its own branches; the back end does it.

??? check "In the example, what would the ELSE mask be if only lanes 0 to 3 had entered the if/else (saved mask `00001111`), with the same inputs?"

    The condition mask is still `01100101`, so THEN is `00001111 & 01100101 = 00000101` (lanes 0 and 2). ELSE is `~00000101 & 00001111 = 11111010 & 00001111 = 00001010` (lanes 1 and 3). Lanes 4 to 7 stay off on both sides, because they were off when the region began, which is why the saved mask, not all ones, appears in both formulas.

## Khronos: SPIR-V, one binary IR for many drivers

SPIR-V is a third answer. The Khronos specification defines it as a binary intermediate language for graphics shaders and compute kernels, passed by a **client API** (Vulkan, OpenCL, or another API that adopts it) into a driver.[^spirv-spec] It is neither one vendor's virtual ISA nor anyone's machine code: every vendor's Vulkan driver accepts it, and what happens below it is that vendor's business. It sits one level higher than PTX. The specification says instructions use SSA form, and aggregates are not flattened into registers.[^spirv-spec]

### Reading the words

A SPIR-V module is not text. The specification defines it as "a single linear stream of words", 32 bits each.[^spirv-spec] The first five words are a header: the magic number `0x07230203`, the version, a number identifying the tool that generated the module, the **bound** (every result id in the module is smaller than it), and a word reserved as zero. After the header, every instruction starts with one word whose high 16 bits give the instruction's length in words and whose low 16 bits give its **opcode**, the number that says which instruction it is.[^spirv-spec] Operands follow. Many are **ids**, the numbered names of results, types and blocks, printed `%33` by convention; others are plain numbers or enumerants.

Here is a real module decoded that way. `mlir-translate --serialize-spirv` turned this chapter's `relu_spirv` output into a 1,028-byte module (257 words) on the book's machine; the example keeps the header, two setup instructions and the instructions of the branch, and decodes them:

--8<-- "includes/examples/gpu/g8-isas-and-irs/spirv_words.cpp.md"

<figure class="vx-figure">
<svg viewBox="0 0 760 210" role="img" aria-label="The four words of one OpBranchConditional instruction. Word 0 is 0x000400FA, split into a high half 0x0004 meaning four words long and a low half 0x00FA meaning opcode 250. Words 1 to 3 are the ids 33, 37 and 38: the condition, the true label and the false label.">
<text class="vx-text" x="20" y="26">OpBranchConditional %33 %37 %38, as four 32-bit words</text>
<rect class="vx-box-strong" x="20" y="50" width="160" height="50" rx="4"/>
<text class="vx-mono vx-text" x="100" y="81" text-anchor="middle">0x0004</text>
<rect class="vx-box-accent" x="180" y="50" width="160" height="50" rx="4"/>
<text class="vx-mono vx-text" x="260" y="81" text-anchor="middle">0x00FA</text>
<rect class="vx-box" x="360" y="50" width="120" height="50" rx="4"/>
<text class="vx-mono vx-text" x="420" y="81" text-anchor="middle">33</text>
<rect class="vx-box" x="490" y="50" width="120" height="50" rx="4"/>
<text class="vx-mono vx-text" x="550" y="81" text-anchor="middle">37</text>
<rect class="vx-box" x="620" y="50" width="120" height="50" rx="4"/>
<text class="vx-mono vx-text" x="680" y="81" text-anchor="middle">38</text>
<text class="vx-text-muted" x="180" y="124" text-anchor="middle">word 0</text>
<text class="vx-text-muted" x="420" y="124" text-anchor="middle">word 1</text>
<text class="vx-text-muted" x="550" y="124" text-anchor="middle">word 2</text>
<text class="vx-text-muted" x="680" y="124" text-anchor="middle">word 3</text>
<text class="vx-text" x="100" y="154" text-anchor="middle">high 16 bits:</text>
<text class="vx-text" x="100" y="174" text-anchor="middle">4 words long</text>
<text class="vx-text" x="260" y="154" text-anchor="middle">low 16 bits:</text>
<text class="vx-text" x="260" y="174" text-anchor="middle">opcode 250</text>
<text class="vx-text" x="420" y="154" text-anchor="middle">condition</text>
<text class="vx-text-muted" x="420" y="174" text-anchor="middle">a bool id</text>
<text class="vx-text" x="550" y="154" text-anchor="middle">true label</text>
<text class="vx-text-muted" x="550" y="174" text-anchor="middle">a block id</text>
<text class="vx-text" x="680" y="154" text-anchor="middle">false label</text>
<text class="vx-text-muted" x="680" y="174" text-anchor="middle">a block id</text>
</svg>
<figcaption>Figure 3. One SPIR-V instruction in its binary form. The first word carries the length and the opcode, so a reader can skip an instruction it does not recognize; the operand words here are all ids.</figcaption>
</figure>

Walk the first lines by hand. Word 1 of the header, `0x00010000`, holds the bytes `0 | 1 | 0 | 0`: major version 1, minor 0. Word 3, `0x2c`, is 44, so ids run from 1 to 43. The first instruction, `0x00020011`, splits into `0x0002` (two words) and `0x0011` (opcode 17, `OpCapability`); its operand, 1, is the capability `Shader`. The next, `0x0003000e`, is three words of opcode 14, `OpMemoryModel`, with operands 0 and 1: the `Logical` addressing model and the `GLSL450` memory model.[^spirv-spec] A driver reading those two instructions knows it has a Vulkan-style module, one where pointers are abstract and cannot be cast to integers.

Now the part that answers the chapter's opening question. `OpFOrdGreaterThan` (opcode 186) is five words: a result type `%32`, the result `%33`, and the two operands `%30` and `%31`, the loaded element and the constant zero. Before decoding the next instruction, finish the one after it yourself: `0x000400fa` has a high half of 4 and a low half of `0xfa`, which is 250, `OpBranchConditional`, and its three ids are the condition `%33` and the two blocks `%37` and `%38`. Both blocks store into the same variable `%35` and branch to `%39`.

The instruction in between, `OpSelectionMerge %39 0`, is the one NVVM never emitted. It declares that the branch following it is a **selection**, and that `%39` is its **merge block**, the block where both sides meet again. Notice also what the module does not contain: no phi. The two sides store into a function-local variable and the merge block loads it back, a form the specification explicitly allows, since memory loads and stores can stand in for phis.[^spirv-spec]

### When branches must declare where they meet

**Structured control flow** means every branch region has one declared entry block (the **header**) and one declared merge block, and regions nest inside each other like brackets. SPIR-V lets any module declare this with merge instructions, `OpSelectionMerge` before a conditional branch and `OpLoopMerge` in a loop header.[^spirv-scf] For modules that declare the `Shader` capability, the kind Vulkan consumes, it is a validation rule: loops must be structured, and a conditional branch to two different blocks needs an `OpSelectionMerge` before it.[^spirv-shader-rules] The validation rules for the `Kernel` capability, the kind OpenCL consumes, contain no such requirement.[^spirv-shader-rules]

That settles the difference between the two lowerings. The `relu_spirv` module declares `Shader`, because it targets a Vulkan-style environment, so the pass had to produce a structured selection. MLIR's SPIR-V dialect represents one as a region: `spirv.mlir.selection` holds the header block, the two sides and the merge block, whose `spirv.mlir.merge` terminator leads to whatever follows the region.[^mlir-spirv] Serialization turns that region into the `OpSelectionMerge` instruction decoded above. The NVVM lowering has no such rule to satisfy, so it left a conditional branch whose meeting point is implied by the shape of the graph.

For a straight-line if/else the cost is nothing: the shape was already nested. The cost appears when a front end produces control flow that is not nested, such as a loop with two exits or a `break` out of a nested loop. Then some pass must restructure it, duplicating blocks or adding flag variables, before a Shader module can be written. LLVM's SPIR-V back end has its own structurizer for this, and [G9](g9-gpu-compilers-in-llvm.md#structured-control-flow-structurizecfg-and-spir-v) compares it with AMDGPU's.

??? check "A front end emits SPIR-V for an OpenCL driver, with the `Kernel` capability, and a loop that has two exits. Must it add merge instructions? Would the answer change for a Vulkan compute shader?"

    For the Kernel module, no: the specification's validation rules for Kernel capabilities do not require structured control flow. A Vulkan compute shader declares `Shader`, and then loops must be structured, with an `OpLoopMerge` naming a single merge block, so the two exits must be restructured into one before the module is valid.

### SPIR-V and decision 56

SPIR-V has a decoration that states decision 56 almost word for word. `NoContraction`, applied to an arithmetic instruction, says the operation cannot be combined with another into one; the specification's example is a multiply that must not become part of a fused multiply-add, and it adds that such operations may not be reassociated either.[^spirv-nocontraction] A Vortex emitter that writes SPIR-V for a Shader environment would attach it to every floating-point add and multiply, rather than depend on what a particular driver does when the decoration is absent.

## Apple: MSL is the floor

Apple documents two ways to compile a kernel. Offline, the `metal` command-line tool compiles each MSL source file into an **intermediate representation file**, and the `metal` and `metal-ar` tools combine such files into a Metal library or an archive.[^metal-precompile] At run time, a program can hand MSL source text to `makeLibrary(source:options:)` and get back a compiled library.[^metal-source] Both paths start from MSL. Neither page describes the intermediate file's format. The Metal Shading Language Specification 4.1 names that format, AIR, only in the description of one compiler option, `-frecord-sources`, and never defines it.[^msl-spec] The Metal shader converter translates another input, DXIL from Direct3D, into a Metal library; its page describes the tool and its C interface, not the format it writes.[^metal-converter]

So on Apple's ladder the lowest rung with a specification is the source language itself. A compiler that targets Metal emits MSL text, the way [G7](g7-programming-models.md#underneath-a-handful-of-targets) described, or SPIR-V that a translator such as SPIRV-Cross turns into MSL. On the book's machine (Apple M4 Pro, macOS 27, checked 2026-09-23), the run-time path compiled and ran MSL text without the separate Metal toolchain component that the offline compiler needs, so it is the path this machine can use today.

For decision 56 the obligation is the one [G7](g7-programming-models.md#what-none-of-this-changes) found: Metal's fast-math compile option, on by default, must be turned off for a Vortex kernel, because MSL text alone does not say how the compiler may rewrite floating-point arithmetic.

??? check "Both of Apple's documented paths accept MSL and return a working library. Why can a compiler outside Apple not target the intermediate file instead, even though the offline path writes one to disk?"

    Because nothing Apple publishes defines that file's format. A compiler could only imitate files it had observed, with no contract that the next Xcode release reads them the same way. MSL is the lowest rung with a written specification, so it is the lowest rung a compiler can target with a guarantee.

## The ladders side by side

Four vendors, and the documentation stops at a different height in each. NVIDIA documents a virtual ISA above its machine code. AMD documents the machine code itself. Khronos documents a binary IR shared across vendors, above whatever each driver does with it. Apple documents only the source language.

<figure class="vx-figure">
<svg viewBox="0 0 780 360" role="img" aria-label="Four columns, NVIDIA, AMD, Khronos and Apple, each with three rungs from source to machine code. Documented rungs have solid boxes, undocumented ones dashed boxes. NVIDIA: CUDA C++ and PTX documented, SASS listed only. AMD: HIP or OpenCL C and the AMDGPU machine code documented, LLVM IR internal. Khronos: GLSL, HLSL or OpenCL C and SPIR-V documented, each driver's machine code not. Apple: MSL documented, AIR and the GPU's machine code not.">
<g style="--vx-i: 0; --vx-n: 4">
<text class="vx-text-accent" x="105" y="24" text-anchor="middle">NVIDIA</text>
<rect class="vx-box-strong" x="20" y="34" width="170" height="46" rx="4"/>
<text class="vx-text" x="105" y="62" text-anchor="middle">CUDA C++</text>
<line class="vx-line" x1="105" y1="80" x2="105" y2="112"/>
<polygon class="vx-arrowhead" points="100,112 105,120 110,112"/>
<rect class="vx-box-strong" x="20" y="122" width="170" height="46" rx="4"/>
<text class="vx-text" x="105" y="150" text-anchor="middle">PTX (virtual ISA)</text>
<line class="vx-line" x1="105" y1="168" x2="105" y2="200"/>
<polygon class="vx-arrowhead" points="100,200 105,208 110,200"/>
<rect class="vx-box-bad" x="20" y="210" width="170" height="46" rx="4"/>
<text class="vx-text-muted" x="105" y="238" text-anchor="middle">SASS (opcodes listed)</text>
</g>
<g style="--vx-i: 1; --vx-n: 4">
<text class="vx-text-accent" x="295" y="24" text-anchor="middle">AMD</text>
<rect class="vx-box-strong" x="210" y="34" width="170" height="46" rx="4"/>
<text class="vx-text" x="295" y="62" text-anchor="middle">HIP / OpenCL C</text>
<line class="vx-line" x1="295" y1="80" x2="295" y2="112"/>
<polygon class="vx-arrowhead" points="290,112 295,120 300,112"/>
<rect class="vx-box-bad" x="210" y="122" width="170" height="46" rx="4"/>
<text class="vx-text-muted" x="295" y="150" text-anchor="middle">LLVM IR (internal)</text>
<line class="vx-line" x1="295" y1="168" x2="295" y2="200"/>
<polygon class="vx-arrowhead" points="290,200 295,208 300,200"/>
<rect class="vx-box-strong" x="210" y="210" width="170" height="46" rx="4"/>
<text class="vx-text" x="295" y="238" text-anchor="middle">AMDGPU machine code</text>
</g>
<g style="--vx-i: 2; --vx-n: 4">
<text class="vx-text-accent" x="485" y="24" text-anchor="middle">Khronos</text>
<rect class="vx-box-strong" x="400" y="34" width="170" height="46" rx="4"/>
<text class="vx-text" x="485" y="62" text-anchor="middle">GLSL / HLSL / OpenCL C</text>
<line class="vx-line" x1="485" y1="80" x2="485" y2="112"/>
<polygon class="vx-arrowhead" points="480,112 485,120 490,112"/>
<rect class="vx-box-strong" x="400" y="122" width="170" height="46" rx="4"/>
<text class="vx-text" x="485" y="150" text-anchor="middle">SPIR-V (binary IR)</text>
<line class="vx-line" x1="485" y1="168" x2="485" y2="200"/>
<polygon class="vx-arrowhead" points="480,200 485,208 490,200"/>
<rect class="vx-box-bad" x="400" y="210" width="170" height="46" rx="4"/>
<text class="vx-text-muted" x="485" y="238" text-anchor="middle">each driver's own code</text>
</g>
<g style="--vx-i: 3; --vx-n: 4">
<text class="vx-text-accent" x="675" y="24" text-anchor="middle">Apple</text>
<rect class="vx-box-strong" x="590" y="34" width="170" height="46" rx="4"/>
<text class="vx-text" x="675" y="62" text-anchor="middle">MSL</text>
<line class="vx-line" x1="675" y1="80" x2="675" y2="112"/>
<polygon class="vx-arrowhead" points="670,112 675,120 680,112"/>
<rect class="vx-box-bad" x="590" y="122" width="170" height="46" rx="4"/>
<text class="vx-text-muted" x="675" y="150" text-anchor="middle">AIR (.ir files)</text>
<line class="vx-line" x1="675" y1="168" x2="675" y2="200"/>
<polygon class="vx-arrowhead" points="670,200 675,208 680,200"/>
<rect class="vx-box-bad" x="590" y="210" width="170" height="46" rx="4"/>
<text class="vx-text-muted" x="675" y="238" text-anchor="middle">GPU machine code</text>
</g>
<rect class="vx-box-strong" x="20" y="290" width="26" height="16" rx="2"/>
<text class="vx-text" x="54" y="303">documented: a written specification a compiler outside the vendor can target</text>
<rect class="vx-box-bad" x="20" y="320" width="26" height="16" rx="2"/>
<text class="vx-text" x="54" y="333">not specified: internal, undocumented, or readable only through a disassembler</text>
</svg>
<figcaption>Figure 4. Every ladder has three rungs, but each vendor stops documenting at a different height. The lowest solid box in each column is the lowest rung a compiler outside that vendor can emit with a written contract: PTX, AMDGPU machine code, SPIR-V and MSL.</figcaption>
</figure>

The number of rungs is not the point; every column has three. What matters is the lowest solid box in each column, because that is what an outside compiler emits. Three of the four are reachable from LLVM or MLIR without writing an encoder: the NVPTX back end prints PTX, the AMDGPU back end writes code objects, and both LLVM and MLIR can produce SPIR-V.[^nvptx][^amdgpu-backend][^llvm-spirv][^mlir-spirv] The fourth, MSL, is source text that a compiler prints the way it would print C.

## What this chapter fixes for Vortex

Nothing here picks a target; [M12](../mlir/m12-vortex-gpu-path.md) does that, after [G9](g9-gpu-compilers-in-llvm.md) has covered the back-end problems (address spaces, uniformity, convergence, structurizing) that sit between any of these formats and a working kernel. What the chapter does fix is a list of obligations that follow the choice around.

For [decision 56](../decisions/numbers.md#d56), each rung has its own way of saying "round every operation once": explicit rounding modifiers in PTX, `NoContraction` in SPIR-V, fast math switched off for MSL, and no `contract` flag in LLVM IR for the AMD path, as G7 showed. None of them is the default. The two lowerings in this chapter already show that the lowering passes themselves leave arithmetic alone: both keep the single `arith.cmpf ogt` as one comparison. The step that must be careful is the one that emits floating-point adds and multiplies, and it must be careful in a different spelling for every target.

For control flow, the obligation depends on the target, not the program: a Vulkan-style SPIR-V module needs structured control flow; OpenCL SPIR-V, PTX and MSL do not; AMD needs it internally but its LLVM back end supplies it. Vortex's control flow is built from nested statements, not arbitrary jumps, which makes the Shader rule cheaper to meet than it is for a language with `goto`, as long as no pass destroys the nesting before the SPIR-V is written. Exits from the middle of a loop are the cases to test.

One last detail in the NVVM output: each memref argument arrived as five scalar arguments (two pointers, an offset, a size and a stride) and was reassembled into a struct. That is MLIR's convention for passing a memref to LLVM, not a rule of PTX or of any GPU. A Vortex kernel whose shapes are fixed by [decision 43](../decisions/arrays.md#d43) could pass two plain pointers instead. How a value crosses a kernel boundary is an ABI decision of the kind [A4](../backend/a4-calling-conventions.md) examines for CPUs, and G7's kernel interface is where Vortex records it.

## For Vortex

!!! vortex "Exercise"

    **Build** a GPU target table in your compiler's target description, and a report that prints it. No code generation: the table records facts this chapter established, so that a later emitter reads them instead of rediscovering them.

    1. One entry per representation: PTX, AMDGPU machine code through LLVM, SPIR-V with the `Shader` capability, SPIR-V with the `Kernel` capability, and MSL text.
    2. For each entry: the rung it occupies (source language, virtual ISA, binary IR or machine code); the specification it follows, with version and URL; whether selections and loops must be declared structured; whether divergent lanes are masked by hardware or by code the compiler writes; and the floating-point obligation, meaning what an emitter must write or switch off so that [decision 56](../decisions/numbers.md#d56) holds.
    3. A report, from a command-line flag of your choosing, that prints one entry or all of them in a stable order.

    **Not yet:** emitting any of these formats; choosing which one Vortex targets ([M12](../mlir/m12-vortex-gpu-path.md)); a structurizer, uniformity analysis or address spaces ([G9](g9-gpu-compilers-in-llvm.md)); anything about Apple's intermediate files beyond recording that they are undocumented.

    **Proof that it works:**

    - A golden test of the full report. The two SPIR-V entries must differ in exactly the structured-control-flow field; the PTX entry's floating-point obligation must name explicit rounding modifiers on add and multiply; the SPIR-V entries' must name `NoContraction`; the MSL entry's must name the fast-math option; the AMDGPU entry must say that divergence is handled by code that sets the execution mask.
    - A table-validity test that fails if any entry has an empty floating-point obligation or no specification URL, so that a sixth target cannot be added without answering both.
    - A hand check against a real file: run this chapter's `relu_spirv.mlir` through `mlir-opt --convert-gpu-to-spirv`, then `--spirv-update-vce`, keep only the `spirv.module`, and serialize it with `mlir-translate --no-implicit-module --serialize-spirv`. Decode its header and first instruction by hand, as in the walk-through, and confirm that the version and capability match what your SPIR-V `Shader` entry claims.

## Key ideas

!!! recap "Questions you can now answer"

    - **What makes an ISA virtual?** It is specified like an instruction set, but no chip decodes it; a further compiler (ahead of time or in the driver) translates it into the installed GPU's machine code. PTX is the example.
    - **How does a CUDA program run on a GPU newer than its compiler?** Its fat binary carries PTX next to the machine code, and the driver compiles that PTX just in time for the new GPU.
    - **Why does AMD publish no virtual ISA?** It publishes the real machine code, text syntax and binary format, for every RDNA and CDNA generation; LLVM's AMDGPU back end emits it directly.
    - **Who masks divergent lanes on AMD hardware?** The compiled code, by saving, narrowing, complementing and restoring the EXEC register around each branch region.
    - **How is a SPIR-V instruction laid out?** As 32-bit words; the first holds the word count (high 16 bits) and the opcode (low 16 bits), and the operands, often ids, follow.
    - **When must SPIR-V control flow be structured?** In modules with the `Shader` capability (Vulkan): loops need `OpLoopMerge` and conditional branches need `OpSelectionMerge`. `Kernel` modules (OpenCL) are not bound by that rule.
    - **What is the lowest rung an outside compiler can target on each ladder?** PTX for NVIDIA, the machine code for AMD, SPIR-V for Khronos drivers, MSL text for Apple.

## Where this comes back

!!! next "You will use this again in"

    - [G9. GPU compilers inside LLVM](g9-gpu-compilers-in-llvm.md): *PTX registers*, *address spaces*, *EXEC mask*, *StructurizeCFG*, *structured control flow*
    - [G11. Matrix units](g11-matrix-units.md): *PTX*, *architecture-specific targets*
    - [G13. Tile languages](g13-tile-languages.md): *PTX as a compiler's output*
    - [M10. MLIR for GPUs](../mlir/m10-mlir-for-gpus.md): *nvvm dialect*, *spirv dialect*, *spirv.mlir.selection*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *lowest documented rung*, *floating-point obligation*

## Sources and further reading

Read the PTX reference's introduction for the clearest short statement of why a virtual ISA exists, then the `nvcc` guide's section on GPU compilation for the two-stage model and fat binaries. For SPIR-V, read "Physical Layout" and "Structured Control Flow" in the specification with the decoder example open. The AMDGPU user guide is long; search it for "EXEC" to find the if/else pattern this chapter followed.

[^ptx-isa]: NVIDIA, "Parallel Thread Execution ISA", version 9.4: sections 1 (Introduction), 4 (Syntax), 5.1 (State Spaces), 9.3 (Predicated Execution), 9.7.3.3 (`add`), 10 (Special Registers) and 11.1.2 (`.target`). <https://docs.nvidia.com/cuda/parallel-thread-execution/index.html>
[^nvcc]: NVIDIA, "NVCC", section 5 (GPU Compilation): virtual architectures, just-in-time compilation and fatbinaries. <https://docs.nvidia.com/cuda/cuda-compiler-driver-nvcc/index.html>
[^cuda-binutils]: NVIDIA, "CUDA Binary Utilities": `cuobjdump`, `nvdisasm`, and section 4, "Instruction Set Reference". <https://docs.nvidia.com/cuda/cuda-binary-utilities/index.html>
[^nvptx]: LLVM Project, "User Guide for NVPTX Back-end": address spaces and the `llvm.nvvm.read.ptx.sreg.*` intrinsics. <https://llvm.org/docs/NVPTXUsage.html>
[^nvvm-dialect]: MLIR, "'nvvm' Dialect". <https://mlir.llvm.org/docs/Dialects/NVVMDialect/>
[^amd-isa]: AMD, "AMD GPU architecture programming documentation" (ISA manuals) and "AMD machine-readable GPU ISA documentation", GPUOpen. <https://gpuopen.com/amd-isa-documentation/> ; <https://gpuopen.com/machine-readable-isa/>
[^amdgpu-backend]: LLVM Project, "User Guide for AMDGPU Backend": processors and generic processors, `wavefrontsize64`, the ELF code object, SGPRs and VGPRs, and the EXEC-mask pseudo code for IF/THEN/ELSE. <https://llvm.org/docs/AMDGPUUsage.html>
[^spirv-spec]: Khronos Group, "SPIR-V Specification", unified: sections 1 (Introduction), 1.7 (Static Single Assignment), 2.3 (Physical Layout of a SPIR-V Module and Instruction) and 3 (Binary Form). <https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#PhysicalLayout>
[^spirv-scf]: Khronos Group, "SPIR-V Specification", unified, section 2.11, "Structured Control Flow". <https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#StructuredControlFlow>
[^spirv-shader-rules]: Khronos Group, "SPIR-V Specification", unified, sections 2.16.2 and 2.16.3, validation rules for Shader and Kernel capabilities. <https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#ShaderValidation>
[^spirv-nocontraction]: Khronos Group, "SPIR-V Specification", unified, section 3.2 (Decoration), `NoContraction`. <https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#Decoration>
[^llvm-spirv]: LLVM Project, "User Guide for SPIR-V Target". <https://llvm.org/docs/SPIRVUsage.html>
[^mlir-spirv]: MLIR, "'spirv' Dialect", section "Control Flow". <https://mlir.llvm.org/docs/Dialects/SPIR-V/>
[^metal-precompile]: Apple, "Building a shader library by precompiling source files". <https://developer.apple.com/documentation/metal/building-a-shader-library-by-precompiling-source-files>
[^metal-source]: Apple, "`makeLibrary(source:options:)`". <https://developer.apple.com/documentation/metal/mtldevice/makelibrary(source:options:)>
[^msl-spec]: Apple, "Metal Shading Language Specification", version 4.1, section 1.6.16. <https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf>
[^metal-converter]: Apple, "Metal shader converter". <https://developer.apple.com/metal/shader-converter/>
