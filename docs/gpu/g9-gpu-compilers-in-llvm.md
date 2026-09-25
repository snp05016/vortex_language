# G9. GPU compilers inside LLVM

<p class="page-intro">NVPTX, AMDGPU and SPIR-V are LLVM back ends built on the pipeline E1 walked through, but a target that runs one program on many lanes at once has to answer four questions a CPU back end never asks: which memory a pointer means, which values are the same in every lane, which operations must not move across a branch, and what shape the control flow must have. This chapter answers each one with an example you can run on this machine, and turns the second into the analysis a Vortex GPU path would need first.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 40 minutes · Builds on: [G8. ISAs and IRs](g8-isas-and-irs.md), [E1. The LLVM code generator pipeline](../backend/e1-llvm-codegen-pipeline.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does a warp do when its lanes disagree about a branch?"

        It runs both sides, one after the other, with the lanes that did not choose the side currently running masked off. A branch on a value that is the same in every lane (warp-uniform) costs one pass, because every lane makes the same choice.

        Introduced in [G2. The SIMT execution model](g2-simt.md).

    ??? question "What does an address space on a kernel pointer tell the compiler?"

        Which memory the pointed-to value lives in: device memory every thread can reach, a block's shared scratchpad, a read-only constant pool, or a thread's own private storage.

        Introduced in [G3. The GPU memory hierarchy](g3-memory-hierarchy.md).

    ??? question "What makes a barrier or a shuffle a convergent operation?"

        Its result depends on which threads execute it together. A branch that sends some lanes elsewhere changes that set, so the operation cannot be moved across the branch as freely as an ordinary instruction.

        Introduced in [G6. Synchronization, atomics and reductions](g6-synchronization.md).

    ??? question "What does a phi at the top of a block choose?"

        The value that arrived along the edge control came in on: one incoming value per predecessor block, picked by which predecessor ran last.

        Introduced in [O3. SSA form: construction and destruction](../optimize/o3-ssa.md).

    ??? question "Where in `llc` do virtual registers become physical ones?"

        In the register allocator, which is one pass in the code generator's pass list (greedy at `-O2`, fast at `-O0`), between instruction selection and the final emission of machine code.

        Introduced in [E1. The LLVM code generator pipeline](../backend/e1-llvm-codegen-pipeline.md).

!!! goals "In this chapter"

    - Trace one kernel from LLVM IR through `llc` to PTX, an AMDGPU code object and a SPIR-V module, and name what each back end adds to E1's pipeline and what it leaves out.
    - Read an address-space number on an LLVM pointer and find the two things that give it meaning: the target's table and the data layout.
    - Run uniformity analysis by hand, including the two ways a value becomes divergent although every operand it reads is uniform.
    - Show with `opt` why an optimizer must not hoist, sink or merge a convergent call, and say what convergence-control tokens add to the `convergent` attribute.
    - Trace a divergent branch through `StructurizeCFG`, and explain why AMDGPU runs it while SPIR-V shaders need a structurizer of their own.

## One kernel, three back ends

Start with the kernel LLVM's NVPTX guide uses as its tutorial: vector addition, where each thread adds one pair of elements.[^l1] Written in LLVM IR for the NVPTX back end, it differs from the CPU functions of E1 in three places. The target triple is `nvptx64-nvidia-cuda`. The function is declared with the `ptx_kernel` calling convention, which marks it as a **kernel**: a function the host launches, as opposed to a device function that only other GPU code calls. And the thread's position in the launch comes from an intrinsic, `llvm.nvvm.read.ptx.sreg.tid.x`, which reads the thread's index within its block from a special register.[^l1] Its pointer arguments carry `addrspace(1)`, which the next section explains.

The guide compiles that file with `llc`, naming a GPU generation with `-mcpu=sm_XX`.[^l1] `llc` runs the pipeline E1 followed: SelectionDAG builds and legalizes a graph per block, instruction selection matches it against patterns generated from the target's TableGen description, and the MC layer prints the result. What comes out is not machine code but **PTX**, NVIDIA's virtual instruction set from [G8](g8-isas-and-irs.md), as text. A second tool, NVIDIA's `ptxas` or the CUDA driver's just-in-time compiler, turns PTX into SASS, the machine code a particular GPU runs.[^l1]

One pass of E1's pipeline is missing from that list. NVPTX's pass configuration makes both of `llc`'s register-assignment hooks unreachable, and a comment in the same file says that, for this target, every register is still virtual after register allocation.[^nvptx-tm] NVPTX leaves every value in a virtual register and prints PTX that way; the physical assignment happens later, when the PTX is compiled to SASS for a real chip.

The other two GPU back ends end differently. AMDGPU compiles to the real instruction set of AMD's GPUs, selected with a triple such as `amdgcn-amd-amdhsa` (the guide now spells the architecture `amdgpu` and accepts `amdgcn` as a legacy alias) and a processor name, and emits an ELF **code object** that the runtime loads directly.[^l2]

AMDGPU does run a register allocator, and one with more to decide than AArch64's: the hardware has **scalar general-purpose registers (SGPRs)** and **vector general-purpose registers (VGPRs)**.[^l2] A VGPR holds one value per lane; an SGPR holds one value for the whole wavefront, which is only correct for a value every lane agrees on.

The SPIR-V back end emits a SPIR-V module, selected with `spirv32`, `spirv64` or the logical-addressing `spirv` triple, for an OpenCL or a Vulkan environment.[^l3] As [G8](g8-isas-and-irs.md) described, a vendor's driver then compiles that module for its own hardware.

<figure class="vx-figure">
<svg viewBox="0 0 760 390" role="img" aria-label="One LLVM IR kernel flowing through opt and llc into three GPU back ends with different outputs" aria-describedby="g9-f1-desc">
<title id="g9-f1-title">One LLVM IR kernel, three GPU back ends</title>
<desc id="g9-f1-desc">On the left, an LLVM IR kernel carrying a kernel calling convention, address spaces on its pointers and a thread-index intrinsic. It passes through opt and then llc, the pipeline from E1. llc forks three ways. NVPTX skips register allocation and prints PTX text, which ptxas or the driver compiles to SASS. AMDGPU structurizes control flow, allocates SGPRs and VGPRs and emits an ELF code object. SPIR-V, for shader targets, runs its own structurizer and emits a SPIR-V module for a Vulkan or OpenCL driver.</desc>
<rect class="vx-box-strong" x="10" y="150" width="150" height="90" rx="4"/>
<text class="vx-text" x="85" y="172" text-anchor="middle">LLVM IR kernel</text>
<text class="vx-mono vx-text-muted" x="85" y="194" text-anchor="middle">ptx_kernel</text>
<text class="vx-mono vx-text-muted" x="85" y="211" text-anchor="middle">ptr addrspace(1)</text>
<text class="vx-mono vx-text-muted" x="85" y="228" text-anchor="middle">tid.x intrinsic</text>
<line class="vx-line" x1="160" y1="195" x2="190" y2="195"/>
<polygon class="vx-arrowhead" points="190,190 198,195 190,200"/>
<rect class="vx-box" x="198" y="160" width="120" height="70" rx="4"/>
<text class="vx-text" x="258" y="190" text-anchor="middle">opt</text>
<text class="vx-text-muted" x="258" y="210" text-anchor="middle">IR passes</text>
<line class="vx-line" x1="318" y1="195" x2="340" y2="195"/>
<polygon class="vx-arrowhead" points="340,190 348,195 340,200"/>
<rect class="vx-box" x="348" y="160" width="120" height="70" rx="4"/>
<text class="vx-text" x="408" y="190" text-anchor="middle">llc</text>
<text class="vx-text-muted" x="408" y="210" text-anchor="middle">E1's pipeline</text>
<path class="vx-flow" d="M468 180 C 490 120, 500 70, 520 60"/>
<polygon class="vx-arrowhead" points="516,55 524,60 516,65"/>
<path class="vx-flow" d="M468 195 L 520 195"/>
<polygon class="vx-arrowhead" points="516,190 524,195 516,200"/>
<path class="vx-flow" d="M468 210 C 490 270, 500 320, 520 330"/>
<polygon class="vx-arrowhead" points="516,325 524,330 516,335"/>
<rect class="vx-box-accent" x="524" y="20" width="226" height="84" rx="4"/>
<text class="vx-text" x="637" y="40" text-anchor="middle">NVPTX</text>
<text class="vx-text-muted" x="637" y="60" text-anchor="middle">no register allocation</text>
<text class="vx-mono" x="637" y="78" text-anchor="middle">PTX text</text>
<text class="vx-text-muted" x="637" y="96" text-anchor="middle">then ptxas or driver: SASS</text>
<rect class="vx-box-accent" x="524" y="153" width="226" height="84" rx="4"/>
<text class="vx-text" x="637" y="173" text-anchor="middle">AMDGPU</text>
<text class="vx-text-muted" x="637" y="193" text-anchor="middle">StructurizeCFG, EXEC masks</text>
<text class="vx-text-muted" x="637" y="211" text-anchor="middle">SGPRs and VGPRs allocated</text>
<text class="vx-mono" x="637" y="229" text-anchor="middle">ELF code object</text>
<rect class="vx-box-accent" x="524" y="288" width="226" height="84" rx="4"/>
<text class="vx-text" x="637" y="308" text-anchor="middle">SPIR-V</text>
<text class="vx-text-muted" x="637" y="328" text-anchor="middle">shaders: SPIRVStructurizer</text>
<text class="vx-mono" x="637" y="346" text-anchor="middle">SPIR-V module</text>
<text class="vx-text-muted" x="637" y="364" text-anchor="middle">then a Vulkan or OpenCL driver</text>
<text class="vx-text-muted" x="10" y="300">Same MachineInstr, same MC layer;</text>
<text class="vx-text-muted" x="10" y="318">each back end adds its own passes</text>
<text class="vx-text-muted" x="10" y="336">and ends in a different format.</text>
</svg>
<figcaption>Figure 1. One kernel, three back ends. Everything to the left of the fork is E1's pipeline. What each GPU back end adds, and skips, sits in its box: NVPTX never assigns physical registers, AMDGPU structurizes control flow and allocates two register files, and a SPIR-V shader target runs its own structurizer.</figcaption>
</figure>

None of the three is available on this machine. `llc --version` lists its registered targets, and the local LLVM 18.1.8 lists only the AArch64 family: `aarch64`, `aarch64_32`, `aarch64_be`, `arm64` and `arm64_32` (observed 24 September 2026). GPU back ends are separate components that a distribution builds in or leaves out. Every example in this chapter therefore runs a target-independent pass through `opt` on a small `.ll` file, or models an analysis in C++; each says which back end it stands in for. To see real PTX or AMDGPU assembly for the tutorial kernel, paste it into Compiler Explorer, as [G8](g8-isas-and-irs.md) suggests.

??? check "NVPTX never assigns physical registers. What does that mean for a reader who counts the registers in `llc`'s PTX output to judge a kernel's register pressure?"

    The count means little. PTX registers are virtual, as many as the code wants, so the PTX says how many values the code names, not how many physical registers the kernel will occupy. The physical assignment, and any spilling, happens when `ptxas` or the driver compiles the PTX to SASS for a specific GPU, so the number that decides occupancy ([G5](g5-occupancy.md)) is only visible after that step.

## Address spaces: which memory a pointer means

A GPU kernel needs more than one kind of pointer. In the tutorial kernel, the input arrays live in the device's large main memory. A tiled kernel ([G10](g10-matmul-ladder.md)) also keeps a tile in the small on-chip scratchpad that one thread block shares. Ordinary LLVM IR has one pointer type, `ptr`, and it can carry a small integer tag: `ptr addrspace(1)` is a pointer into **address space** 1. The Language Reference defines address space 0 as the default and says that the meaning of every other number is target-specific.[^langref] The IR itself attaches nothing to the number 3; a back end does.

NVPTX and AMDGPU each publish a table. The numbers happen to line up for the four memories G3 introduced, but one word does not:

| Number | NVPTX name[^l1] | AMDGPU name[^l2] | What it is |
| --- | --- | --- | --- |
| 0 | Generic | Generic (flat) | Any of the others, decided at run time |
| 1 | Global | Global | Device memory every thread can reach |
| 3 | Shared | Local (LDS) | The scratchpad one thread block shares |
| 4 | Constant | Constant | Read-only device memory |
| 5 | Local | Private (scratch) | Memory private to one thread |

"Local" names the per-thread memory on NVPTX and the shared scratchpad on AMDGPU. Both tables have more rows than these five: NVPTX adds 7 for a per-cluster shared memory, and AMDGPU defines further numbers for buffer resources and other hardware.[^l1][^l2]

Address space 0, the **generic address space**, is a pointer that can point into several of the others. NVPTX provides intrinsics to convert between generic and specific pointers, and AMDGPU implements generic accesses with flat instructions that check at run time which range the address falls in.[^l1][^l2] A generic access pays for that check and a specific one does not, so back ends try to recover the specific space: NVPTX's pass configuration runs LLVM's `InferAddressSpaces` pass, which rewrites a generic access into a specific one when it can prove where the pointer came from, and SPIR-V's runs it for shader targets.[^nvptx-tm][^spirv-tm]

The number also reaches passes that know nothing about GPUs, through the **data layout**, the string at the top of a module that tells target-independent passes how big each type is. One of its entries, `p[n]:<size>:<abi>`, gives the pointer size for address space `n`.[^langref] AMDGPU's table lists local pointers as 32 bits wide while global pointers are 64.[^l2] The example below keeps only that fact in its data layout and runs `instcombine` over two small functions.

--8<-- "includes/examples/gpu/g9-gpu-compilers-in-llvm/addrspaces.ll.md"

The global pointer's `ptrtoint` stays a 64-bit operation. The shared pointer's becomes a 32-bit `ptrtoint` followed by a zero extension, and the index of the `getelementptr` into shared memory is truncated to 32 bits, because address arithmetic in a 32-bit address space never needs more. `instcombine` has no idea what "shared" means; it read the pointer size from the data layout.

A back end supplies both halves: its own data layout string, which every target-independent pass reads, and the instruction selection that turns a load from address space 3 into the instruction for that memory. AArch64's back end gives the numbers no meaning: on this machine, `llc` compiles a load through `ptr addrspace(3)` to the same `ldr` as a load through a plain `ptr` (observed with LLVM 18.1.8).

Kernels need one more convention: a way to say which function is the entry point. NVPTX uses the `ptx_kernel` calling convention, and AMDGPU has its own, `amdgpu_kernel`.[^l1][^l2] Something has to split a program into host code and device code before either is chosen. Clang compiles a CUDA file more than once: a host compilation, and a device compilation for each GPU architecture.[^l6] MLIR's `gpu` dialect does the split as a pass, `gpu-kernel-outlining`, which moves the body of a `gpu.launch` into a function in a separate GPU module;[^m16] [M10](../mlir/m10-mlir-for-gpus.md) takes that path in detail.

??? check "Delete the `target datalayout` line from `addrspaces.ll` and run the same `instcombine`. What changes in the output, and why?"

    Nothing is narrowed any more: the shared pointer's `ptrtoint` stays 64-bit and the `getelementptr` keeps its 64-bit index. Without a `p3` entry, address space 3 has no pointer size of its own and gets the default, so `instcombine` has no reason to treat it differently from address space 1. The pass never looked at the number itself, only at what the data layout says about it.

## Uniformity: one value per warp, or one per lane

Consider a value computed from the kernel's arguments alone, such as `n * 2` where `n` is the array length. Every lane of a warp computes the same number. A value computed from the thread index is different in every lane. LLVM calls the first kind **uniform** and the second **divergent**; a branch is uniform when its condition is uniform, and divergent otherwise.[^l4] **Uniformity analysis** decides, for every value and every branch in a function, which of the two it is.

A GPU back end wants the answer for two reasons, both given in LLVM's description of the analysis.[^l4] A uniform value can be computed or stored once for the whole group of lanes; on AMDGPU that means an SGPR instead of a VGPR. A divergent branch has to be **linearized**, turned into straight-line code that runs both sides under masks as G2 described, while a uniform branch can stay a real jump, because the whole warp goes one way.

The analysis is only as good as the target's help: it asks the target which values are never uniform (the sources of divergence) and whether branches can diverge at all. On a CPU target the answer to the second question is no, and the analysis reports every value uniform.[^ua] You can see this on this machine: `opt -disable-output -passes='print<uniformity>'` on any function prints `ALL VALUES UNIFORM`, because AArch64 has no lanes to diverge.

The analysis starts from **seeds**, the values the target declares divergent (the thread-index intrinsics are the main ones), and spreads from there. Three rules decide the rest.

The first is plain data flow. A value that reads a divergent operand is divergent: `c1 = tid < n` differs across lanes because `tid` does.

The second rule is less obvious. Take `r = phi(a, b)`, where `a = n * 2` is computed in one arm of a branch on `c1` and `b = n * 3` in the other. Both are uniform: every lane that computes `a` gets the same number. But the phi picks `a` in the lanes that took the first arm and `b` in the lanes that took the second, so `r` holds two different values across one warp.

A phi at a **join**, a block where the two paths from a divergent branch meet again, is divergent even when every incoming value is uniform. LLVM's definition says exactly this: a phi is uniform only if converged lanes choose the same incoming value, and that value is uniform.[^l4]

The third concerns loops. Suppose a loop runs while `i1 < tid`. Inside the loop, `i` and `i1` are uniform: at each trip around the loop, every lane still in it holds the same count. The exit branch is divergent, though, so lanes leave on different trips, and a use of `i1` after the loop sees a different final count in each lane. LLVM calls this **temporal divergence**: the values inside the loop are uniform, but a use outside receives them from different iterations.[^l4]

<figure class="vx-figure">
<svg viewBox="0 0 700 600" role="img" aria-label="The worked kernel's control-flow graph with each value marked uniform or divergent and the rule that made it divergent" aria-describedby="g9-f2-desc">
<title id="g9-f2-title">Uniformity in the worked kernel</title>
<desc id="g9-f2-desc">Block entry computes tid, a divergent seed, n, uniform, and c1, divergent by operand. A divergent branch on c1 leads to then1, computing a, uniform, and else1, computing b, uniform. They meet at merge1, where the phi r is divergent by the join rule; c2 in merge1 is uniform. A uniform branch on c2 leads to then2, computing t, uniform, and to merge2, whose phi s stays uniform because its branch was uniform. Then block loop holds the phi i and i1, both uniform, and c3, divergent by operand because it reads tid; loop has an edge back to itself. The divergent exit leads to exit, where e reads i1 and is divergent by the loop-exit rule.</desc>
<rect class="vx-box" x="240" y="10" width="220" height="74" rx="4"/>
<text class="vx-text" x="250" y="28">entry</text>
<text class="vx-mono vx-text-accent" x="350" y="28">tid  seed</text>
<text class="vx-mono" x="350" y="48">n    uniform</text>
<text class="vx-mono vx-text-accent" x="350" y="68">c1   operand</text>
<line class="vx-line" x1="300" y1="84" x2="160" y2="120"/>
<polygon class="vx-arrowhead" points="165,114 157,121 167,124"/>
<line class="vx-line" x1="400" y1="84" x2="540" y2="120"/>
<polygon class="vx-arrowhead" points="533,114 543,121 531,124"/>
<text class="vx-text-accent" x="350" y="108" text-anchor="middle">divergent branch</text>
<rect class="vx-box" x="60" y="122" width="200" height="44" rx="4"/>
<text class="vx-text" x="70" y="148">then1</text>
<text class="vx-mono" x="140" y="148">a  uniform</text>
<rect class="vx-box" x="440" y="122" width="200" height="44" rx="4"/>
<text class="vx-text" x="450" y="148">else1</text>
<text class="vx-mono" x="520" y="148">b  uniform</text>
<line class="vx-line" x1="160" y1="166" x2="300" y2="200"/>
<polygon class="vx-arrowhead" points="293,194 303,201 291,204"/>
<line class="vx-line" x1="540" y1="166" x2="400" y2="200"/>
<polygon class="vx-arrowhead" points="407,194 397,201 409,204"/>
<rect class="vx-box-bad" x="240" y="202" width="220" height="56" rx="4"/>
<text class="vx-text" x="250" y="222">merge1</text>
<text class="vx-mono vx-text-accent" x="330" y="222">r = phi  join</text>
<text class="vx-mono" x="330" y="244">c2  uniform</text>
<line class="vx-line" x1="400" y1="258" x2="540" y2="290"/>
<polygon class="vx-arrowhead" points="533,284 543,291 531,294"/>
<line class="vx-line" x1="330" y1="258" x2="330" y2="340"/>
<polygon class="vx-arrowhead" points="325,334 330,342 335,334"/>
<text class="vx-text-muted" x="200" y="300">uniform branch</text>
<rect class="vx-box" x="440" y="292" width="200" height="44" rx="4"/>
<text class="vx-text" x="450" y="318">then2</text>
<text class="vx-mono" x="520" y="318">t  uniform</text>
<line class="vx-line" x1="540" y1="336" x2="420" y2="352"/>
<polygon class="vx-arrowhead" points="427,346 417,353 429,357"/>
<rect class="vx-box" x="240" y="342" width="220" height="44" rx="4"/>
<text class="vx-text" x="250" y="368">merge2</text>
<text class="vx-mono" x="330" y="368">s = phi  uniform</text>
<line class="vx-line" x1="350" y1="386" x2="350" y2="412"/>
<polygon class="vx-arrowhead" points="345,406 350,414 355,406"/>
<rect class="vx-box" x="240" y="414" width="220" height="74" rx="4"/>
<text class="vx-text" x="250" y="432">loop</text>
<text class="vx-mono" x="320" y="432">i = phi  uniform</text>
<text class="vx-mono" x="320" y="452">i1       uniform</text>
<text class="vx-mono vx-text-accent" x="320" y="472">c3       operand</text>
<path class="vx-line" d="M460 440 C 520 420, 520 490, 460 470"/>
<polygon class="vx-arrowhead" points="467,465 459,471 468,476"/>
<text class="vx-text-muted" x="530" y="458">back edge</text>
<line class="vx-line" x1="350" y1="488" x2="350" y2="522"/>
<polygon class="vx-arrowhead" points="345,516 350,524 355,516"/>
<text class="vx-text-accent" x="360" y="510">divergent exit</text>
<rect class="vx-box-bad" x="240" y="524" width="220" height="44" rx="4"/>
<text class="vx-text" x="250" y="550">exit</text>
<text class="vx-mono vx-text-accent" x="310" y="550">e = i1  loop exit</text>
<text class="vx-text-muted" x="20" y="590">Accent text: divergent, and the rule. Marked boxes: divergent with uniform operands.</text>
</svg>
<figcaption>Figure 2. Uniformity in the worked kernel. Data flow makes <code>c1</code> and <code>c3</code> divergent. The join rule makes <code>r</code> divergent although <code>a</code> and <code>b</code> are uniform, while <code>s</code>, the same shape behind a uniform branch, stays uniform. The loop-exit rule makes <code>e</code> divergent although <code>i1</code> is uniform inside the loop.</figcaption>
</figure>

The example below builds that kernel as a table of values and branches and applies the three rules until nothing changes. A value can only move from uniform to divergent, never back, so the iteration stops.

--8<-- "includes/examples/gpu/g9-gpu-compilers-in-llvm/uniformity.cpp.md"

Follow the output line by line. `tid` is the seed. `c1` reads it, so it is divergent by the operand rule. `a` and `b` read only `n` and stay uniform: they sit inside the arms of a divergent branch, but every lane that computes `a` computes the same `a`. `r` is the phi where those arms meet, and the branch that split them was divergent, so `r` is divergent by the join rule.

`s` has the same shape, a phi at the join of a branch, but that branch tests `c2 = n > 4`, which is uniform, so the whole warp took one side and `s` stays uniform. Inside the loop, `i` and `i1` are uniform; `c3` reads `tid` and is divergent, which makes the loop's exit divergent, and `e`, reading `i1` after the loop, is divergent by the loop-exit rule.

LLVM's analysis does the same thing on real IR, with two extensions this model skips. It finds joins itself, from the control-flow graph, instead of being told where they are, and it handles irreducible loops, loops with more than one entry, where the question of which lanes are converged depends on which block is treated as the loop header.[^l4] Its uniform results are guarantees; a value it cannot prove uniform is reported divergent, which costs speed but never correctness.

??? check "Extend the kernel by hand. Add `w = a + 1` in `then1`, `u = s + r` in `merge2`, and `f = e - i1` in `exit`. Which are uniform, which divergent, and by which rule?"

    `w` is uniform: it reads only `a`, which is uniform, and a value inside a divergent arm is not divergent for that reason alone. `u` is divergent by the operand rule, because it reads `r`. `f` is divergent too, by the loop-exit rule (it reads `i1` after the loop) and by the operand rule (it reads `e`); either one is enough.

## Convergent operations: what an optimizer must not move

[G6](g6-synchronization.md) explained why a barrier or a shuffle is **convergent**: its result depends on which threads execute it together. In LLVM IR such an operation is always a call, to a function or intrinsic that carries the `convergent` attribute, and a generic pass must not change the set of threads that execute it together.[^l5] The danger is not exotic. Passes that know nothing about GPUs move calls around every day: SimplifyCFG hoists identical instructions out of both arms of a branch, loop passes sink and hoist code, the inliner copies it.

LLVM's documentation gives the standard example: two subgroup sums, one in each arm of a branch on a lane's own value. As written, each sum adds up the lanes that took its arm. Hoisted above the branch and merged into one call, the sum adds up every lane, a different answer.[^l5] The example below reproduces that situation with two functions of the same shape, one calling an ordinary function in each arm and one calling a convergent one.

--8<-- "includes/examples/gpu/g9-gpu-compilers-in-llvm/convergent.ll.md"

In `@plain`, SimplifyCFG saw the same call with the same argument at the top of both arms, hoisted it into `entry`, and then removed the branch, the two empty arms and the phi, leaving two instructions. In `@wave`, the only difference is the `convergent` attribute on the callee, and SimplifyCFG left the function exactly as it was written.

<figure class="vx-figure">
<svg viewBox="0 0 700 250" role="img" aria-label="Four lanes computing two per-arm sums versus one hoisted sum" aria-describedby="g9-f3-desc">
<title id="g9-f3-title">Why a convergent sum cannot be hoisted</title>
<desc id="g9-f3-desc">Four lanes hold d equal to 3, minus 1, 5 and minus 2. As written, lanes 0 and 2 take the gains arm and their wave sum is 8; lanes 1 and 3 take the losses arm and their wave sum is minus 3. After hoisting, all four lanes join one wave sum, which is 5, so every lane receives 5 instead of 8 or minus 3.</desc>
<text class="vx-text" x="20" y="24">As written: one sum per arm</text>
<rect class="vx-box" x="20" y="40" width="70" height="36" rx="3"/><text class="vx-mono" x="55" y="63" text-anchor="middle">3</text>
<rect class="vx-box" x="100" y="40" width="70" height="36" rx="3"/><text class="vx-mono" x="135" y="63" text-anchor="middle">-1</text>
<rect class="vx-box" x="180" y="40" width="70" height="36" rx="3"/><text class="vx-mono" x="215" y="63" text-anchor="middle">5</text>
<rect class="vx-box" x="260" y="40" width="70" height="36" rx="3"/><text class="vx-mono" x="295" y="63" text-anchor="middle">-2</text>
<text class="vx-text-muted" x="20" y="96">lane 0</text><text class="vx-text-muted" x="100" y="96">lane 1</text><text class="vx-text-muted" x="180" y="96">lane 2</text><text class="vx-text-muted" x="260" y="96">lane 3</text>
<rect class="vx-box-accent" x="20" y="120" width="310" height="44" rx="4"/>
<text class="vx-text" x="30" y="140">gains arm, lanes 0 and 2</text>
<text class="vx-mono" x="30" y="157">wave_sum = 3 + 5 = 8</text>
<rect class="vx-box-accent" x="20" y="176" width="310" height="44" rx="4"/>
<text class="vx-text" x="30" y="196">losses arm, lanes 1 and 3</text>
<text class="vx-mono" x="30" y="213">wave_sum = -1 + -2 = -3</text>
<text class="vx-text" x="370" y="24">Hoisted above the branch</text>
<rect class="vx-box" x="370" y="40" width="70" height="36" rx="3"/><text class="vx-mono" x="405" y="63" text-anchor="middle">3</text>
<rect class="vx-box" x="450" y="40" width="70" height="36" rx="3"/><text class="vx-mono" x="485" y="63" text-anchor="middle">-1</text>
<rect class="vx-box" x="530" y="40" width="70" height="36" rx="3"/><text class="vx-mono" x="565" y="63" text-anchor="middle">5</text>
<rect class="vx-box" x="610" y="40" width="70" height="36" rx="3"/><text class="vx-mono" x="645" y="63" text-anchor="middle">-2</text>
<rect class="vx-box-bad" x="370" y="120" width="310" height="100" rx="4"/>
<text class="vx-text" x="380" y="140">every lane, one call</text>
<text class="vx-mono" x="380" y="160">wave_sum = 3 - 1 + 5 - 2 = 5</text>
<text class="vx-text-muted" x="380" y="186">lanes 0 and 2 get 5, not 8</text>
<text class="vx-text-muted" x="380" y="206">lanes 1 and 3 get 5, not -3</text>
<text class="vx-text-muted" x="20" y="244">A hoist that is correct for @lane_sum changes every lane's answer for @wave_sum.</text>
</svg>
<figcaption>Figure 3. Why a convergent sum cannot be hoisted. With four lanes holding 3, −1, 5 and −2, the program as written computes two sums, one over each arm's lanes. Hoisting the call makes every lane join a single sum.</figcaption>
</figure>

The attribute alone has a weakness. It says that a call must not gain or lose participating threads, but not which threads it was meant to participate with, and LLVM's own documentation admits that the attribute's semantics as implemented differ from its documented semantics and remain under-specified.[^l5]

Current LLVM therefore adds **convergence-control tokens**. A call to `llvm.experimental.convergence.entry` at the start of a function produces a token that stands for the set of threads that entered the function together; `llvm.experimental.convergence.loop` produces one per loop iteration; and `llvm.experimental.convergence.anchor` produces one for an implementation-defined set of threads. A convergent call names the set it belongs to with a `"convergencectrl"` operand bundle carrying one of these tokens.[^l5]

The relationship becomes a use of a value, which every pass already knows how to respect. A call with a token is **controlled**; one without is **uncontrolled**, and a function may not mix the two.[^l5] The intrinsics exist in the local LLVM 18: `opt -passes=verify` accepts a function that calls `llvm.experimental.convergence.entry`.

Convergence and uniformity meet here. The documentation adds that the hoist in the example becomes legal if the compiler can prove the branch condition uniform, because then the whole group takes one arm and the call already had every thread.[^l5] The better the uniformity analysis, the more freely a GPU compiler can optimize around convergent code.

??? check "Suppose `@wave` branched on a kernel argument instead of on `%d`, and the compiler could prove that argument uniform. Would hoisting `@wave_sum` above the branch still be wrong?"

    No. With a uniform condition every lane of the group takes the same arm, so the call in that arm already runs with all of them; hoisting it does not change who participates, and LLVM's convergence documentation allows the hoist in that case. `@wave` as written branches on `%d`, a per-lane value, which is why the call has to stay where it is.

## Structured control flow: StructurizeCFG and SPIR-V

A divergent branch is linearized by running both arms with lanes masked off. For an if-then-else that is straightforward, but LLVM IR allows any control-flow graph: a branch that jumps out of the middle of a loop, two loops sharing an exit, blocks entered from several directions. Masking code that shape is hard.

AMDGPU's answer is to reshape the graph first. Its code generator runs, before instruction selection, a short sequence of IR passes: one that merges divergent exits into one, `FixIrreducible`, `UnifyLoopExits`, and then **`StructurizeCFG`**.[^amdgpu-tm] After those, `SIAnnotateControlFlow` annotates the divergent branches that remain with AMDGPU-specific intrinsics, skipping any branch the uniformity analysis proves uniform, so that the back end can linearize them with the **execution mask (EXEC)**, the register whose bits say which lanes are active.[^amdgpu-tm][^siacf]

AMDGPU's guide shows the result for an if-then-else: save EXEC, AND it with the condition's lane mask and run the then-arm, invert within the saved mask and run the else-arm, then restore EXEC.[^l2]

`StructurizeCFG` rewrites a function one single-entry, single-exit region at a time, so that every if-then-else takes one fixed shape: a branch whose true side enters the arm and whose false side skips it, with new **Flow blocks** where the paths rejoin, and the choice of which arm still has to run carried in phi nodes.[^l7] Loops get the same treatment, with every exit routed through a Flow block that holds the back edge.[^l7]

--8<-- "includes/examples/gpu/g9-gpu-compilers-in-llvm/structurize.ll.md"

Walk through the output with four lanes whose `%lane` values are −1, 0, 2 and 5. In the original, lanes 2 and 3 (values 2 and 5) take `then` and lanes 0 and 1 take `else`. After the pass, `entry` tests the inverted condition, `icmp sle`, and sends lanes 0 and 1 to `else` while lanes 2 and 3 go to the new `Flow` block.

Lanes 0 and 1 then reach `Flow` too. There two phis wait: `%0` holds the partial result, 20 for lanes that came through `else` and `undef` for the rest, and `%1` says whether a lane still has to visit `then`, false for lanes that came through `else` and true for lanes that came straight from `entry`.

`Flow` branches on `%1`: lanes 2 and 3 run `then` and all four meet at `merge`, where the final phi picks 10 from `then` or `%0` from `Flow`. Every lane returns what it returned before: 20 for lanes 0 and 1, 10 for lanes 2 and 3. The `undef` is harmless, because a lane that receives it always passes through `then`, and the final phi replaces it with 10.

<figure class="vx-figure">
<svg viewBox="0 0 720 360" role="img" aria-label="lane_select before and after StructurizeCFG, with the lanes active in each block" aria-describedby="g9-f4-desc">
<title id="g9-f4-title">lane_select before and after StructurizeCFG</title>
<desc id="g9-f4-desc">Left, before: entry branches to then, active lanes 2 and 3, and to else, active lanes 0 and 1; both reach merge. Right, after: entry branches to else, lanes 0 and 1, or directly to Flow; else goes to Flow, where all four lanes are present and two phis record the partial result and whether a lane still needs then; Flow branches to then, lanes 2 and 3, or to merge; then goes to merge, where all four lanes finish. Under an execution mask the right-hand graph runs top to bottom, one block at a time.</desc>
<text class="vx-text" x="20" y="22">Before</text>
<rect class="vx-box" x="90" y="36" width="140" height="40" rx="4"/>
<text class="vx-text" x="160" y="54" text-anchor="middle">entry</text><text class="vx-mono vx-text-muted" x="160" y="70" text-anchor="middle">lanes 0 1 2 3</text>
<line class="vx-line" x1="130" y1="76" x2="80" y2="126"/><polygon class="vx-arrowhead" points="80,118 77,128 87,124"/>
<line class="vx-line" x1="190" y1="76" x2="240" y2="126"/><polygon class="vx-arrowhead" points="233,124 243,128 240,118"/>
<rect class="vx-box" x="20" y="128" width="120" height="40" rx="4"/>
<text class="vx-text" x="80" y="146" text-anchor="middle">then</text><text class="vx-mono vx-text-muted" x="80" y="162" text-anchor="middle">lanes 2 3</text>
<rect class="vx-box" x="180" y="128" width="120" height="40" rx="4"/>
<text class="vx-text" x="240" y="146" text-anchor="middle">else</text><text class="vx-mono vx-text-muted" x="240" y="162" text-anchor="middle">lanes 0 1</text>
<line class="vx-line" x1="80" y1="168" x2="130" y2="218"/><polygon class="vx-arrowhead" points="122,216 132,220 129,210"/>
<line class="vx-line" x1="240" y1="168" x2="190" y2="218"/><polygon class="vx-arrowhead" points="191,210 188,220 198,216"/>
<rect class="vx-box" x="90" y="220" width="140" height="40" rx="4"/>
<text class="vx-text" x="160" y="238" text-anchor="middle">merge</text><text class="vx-mono vx-text-muted" x="160" y="254" text-anchor="middle">lanes 0 1 2 3</text>
<text class="vx-text-muted" x="20" y="300">Two arms side by side: which one</text>
<text class="vx-text-muted" x="20" y="318">runs first is not written down.</text>
<text class="vx-text" x="380" y="22">After StructurizeCFG</text>
<rect class="vx-box" x="470" y="36" width="160" height="40" rx="4"/>
<text class="vx-text" x="550" y="54" text-anchor="middle">entry</text><text class="vx-mono vx-text-muted" x="550" y="70" text-anchor="middle">lanes 0 1 2 3</text>
<line class="vx-line" x1="500" y1="76" x2="450" y2="100"/><polygon class="vx-arrowhead" points="455,94 446,102 457,104"/>
<rect class="vx-box" x="380" y="102" width="130" height="40" rx="4"/>
<text class="vx-text" x="445" y="120" text-anchor="middle">else</text><text class="vx-mono vx-text-muted" x="445" y="136" text-anchor="middle">lanes 0 1</text>
<line class="vx-line" x1="600" y1="76" x2="600" y2="162"/><polygon class="vx-arrowhead" points="595,156 600,164 605,156"/>
<line class="vx-line" x1="445" y1="142" x2="520" y2="164"/><polygon class="vx-arrowhead" points="513,158 522,165 511,168"/>
<rect class="vx-box-accent" x="440" y="164" width="250" height="56" rx="4"/>
<text class="vx-text" x="565" y="182" text-anchor="middle">Flow</text><text class="vx-mono vx-text-muted" x="565" y="198" text-anchor="middle">lanes 0 1 2 3</text>
<text class="vx-mono" x="565" y="214" text-anchor="middle">%0 partial, %1 need then</text>
<line class="vx-line" x1="500" y1="220" x2="450" y2="244"/><polygon class="vx-arrowhead" points="455,238 446,246 457,248"/>
<rect class="vx-box" x="380" y="246" width="130" height="40" rx="4"/>
<text class="vx-text" x="445" y="264" text-anchor="middle">then</text><text class="vx-mono vx-text-muted" x="445" y="280" text-anchor="middle">lanes 2 3</text>
<line class="vx-line" x1="620" y1="220" x2="620" y2="300"/><polygon class="vx-arrowhead" points="615,294 620,302 625,294"/>
<line class="vx-line" x1="445" y1="286" x2="520" y2="304"/><polygon class="vx-arrowhead" points="513,298 522,306 511,309"/>
<rect class="vx-box" x="480" y="304" width="170" height="40" rx="4"/>
<text class="vx-text" x="565" y="322" text-anchor="middle">merge</text><text class="vx-mono vx-text-muted" x="565" y="338" text-anchor="middle">lanes 0 1 2 3</text>
</svg>
<figcaption>Figure 4. <code>lane_select</code> before and after <code>StructurizeCFG</code>, for four lanes with <code>%lane</code> = −1, 0, 2, 5. After the pass the graph is a chain: each arm is entered or skipped from one block, and the lanes that skip an arm wait for it at a Flow block. That is the order a masked machine runs it in, with EXEC set to the listed lanes in each block.</figcaption>
</figure>

The pass has a cost: in the rewritten function every lane passes through `Flow`, and phis carry state that a plain branch kept in the program counter. For a uniform branch that cost buys nothing, since the whole warp goes one way. `StructurizeCFG` therefore takes the uniformity analysis as an input and has an option to leave uniform regions as they are.[^l7] In the current AMDGPU pipeline that option is off; uniform branches are instead left unannotated by `SIAnnotateControlFlow`, the pass that follows.[^amdgpu-tm][^siacf]

SPIR-V has a structure requirement of its own, but it is a rule about the binary format, not about masking. The specification lets a module declare **structured control flow**: a merge instruction in a header block names the block where the branches of a selection or loop come back together, and those constructs must nest and be entered and left only in listed ways.[^k4] For modules that declare the Shader capability, the kind Vulkan consumes, the validation rules require it: every loop must be structured, and so must every conditional branch or switch, apart from a few branches that go straight to a merge block already declared.[^k4v] Kernel modules, the OpenCL kind, have no such rule.

LLVM's SPIR-V back end follows the same split. For a shader subtarget it runs `InferAddressSpaces`, `LoopSimplify`, a pass that turns cross-block values into memory, a pass that merges each region's exits, and then its own `SPIRVStructurizer`, which works out the merge blocks SPIR-V needs to declare.[^spirv-tm] It does not use `StructurizeCFG`. [G8](g8-isas-and-irs.md) showed the MLIR side of the same rule: lowered to SPIR-V, even a simple `scf.if` gains an explicit merge region.

## What the back ends share

Put the sections side by side and a GPU back end differs from E1's AArch64 walk-through by additions, not a different pipeline. Instruction selection still runs on SelectionDAG or GlobalISel, `MachineInstr` is still the shared language, and the MC layer still prints the result.

The additions are the four answers this chapter found. Address-space numbers get their meaning from the target's table and data layout, and passes like `InferAddressSpaces` try to recover specific spaces from generic pointers. Uniformity analysis, seeded by the target, tells the back end which values can live once per warp and which branches need masking. The `convergent` attribute and convergence tokens stop generic passes from changing who takes part in a barrier or a shuffle. And a structurizer, `StructurizeCFG` for AMDGPU or `SPIRVStructurizer` for SPIR-V shaders, reshapes control flow into the form the target can execute or encode.

None of these is specific to CUDA, HIP or any vendor. A compiler that targets GPUs through MLIR meets them again on the way down, because MLIR's GPU lowerings end in these same LLVM back ends or in SPIR-V ([G8](g8-isas-and-irs.md)). A compiler that writes its own GPU back end meets all four with no help. For Vortex, whose GPU path [M12](../mlir/m12-vortex-gpu-path.md) weighs, the one worth building early is uniformity: it needs nothing but the IR you already have, and every later decision in this book, from coalescing to shared-memory tiling, asks which values are the same across a warp.

## For Vortex

!!! vortex "Exercise"

    **Build** a uniformity report for kernels in your compiler's IR. Given a function, a choice of which loop variables are mapped to the lanes of a warp (the thread mapping from [G4](g4-memory-performance.md)), and the block and warp shape, classify every value and every branch as uniform or divergent.

    1. Seeds: the values that depend on the lane-mapped variables. A variable mapped to the block index, not the lane, is uniform within a warp.
    2. The three rules of this chapter: operand, join (a phi or merged variable where the two sides of a divergent branch meet) and loop exit (a value used after a loop whose exit condition is divergent). Find the joins from your control-flow graph ([O2](../optimize/o2-cfg-and-dominance.md)), not from a hand-written list.
    3. Iteration to a fixed point, so that a divergent value discovered late still reaches every use.
    4. A remark per branch, such as "branch on `column < n` is divergent: both sides run under a mask".

    **Not yet:** generating GPU code, structurizing control flow, a `convergent` flag on operations (Vortex has no barriers or shuffles yet) and address spaces in the type system. [M12](../mlir/m12-vortex-gpu-path.md) decides which of these Vortex builds itself and which it gets from LLVM or MLIR.

    **Proof that it works:**

    - Golden test, the stage 10 `multiply` kernel at `[f32; 64, 64]`, with `column` taken from the thread's `x` index and `row` from its `y` index in blocks 32 threads wide, so that one warp shares a `row`: `row` uniform, `column` divergent, `k` uniform, the address of `a[row, k]` uniform, the address of `b[k, column]` divergent, the running sum divergent, the address of `c[row, column]` divergent. Compare with G4's sector counts: the uniform address is the one G4 calls a broadcast.
    - Golden test, a boundary guard `if column < n { c[row, column] = sum }`: the branch is divergent; a value computed inside the guarded block from `row` and `n` alone is uniform.
    - Golden test, a join: the kernel from this chapter's example, written in Vortex, with `r`, `s` and `e` classified as the example prints them.
    - Golden test, a loop whose trip count depends on `column`: the loop counter is uniform inside the loop and divergent when read after it.
    - A soundness check: execute each test kernel on the CPU once per lane of one warp, with the lane's indices passed in as ordinary arguments, record every value each lane computes, and confirm that every value the report calls uniform had one result across all lanes that computed it in the same iteration. The report may call a value divergent that happens to agree; it may never call a value uniform that disagrees.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does a GPU target add to E1's pipeline?** Passes, not a new pipeline: target meanings for address spaces, a uniformity analysis, protection for convergent calls, and a structurizer. NVPTX also removes one pass, register assignment, because PTX registers are virtual.
    - **What gives an address-space number its meaning?** The target: its published table, its data layout (which target-independent passes read) and its instruction selection. The IR only says that non-zero address spaces are target-specific.
    - **How can a value be divergent when every operand is uniform?** At a join of a divergent branch, where lanes pick different incoming values of a phi, and after a loop with a divergent exit, where lanes leave on different iterations.
    - **What does SimplifyCFG do with identical calls in both arms of a branch?** Hoists them and deletes the branch, unless the callee is convergent; then it leaves them, because hoisting would change which lanes take part.
    - **What do convergence tokens add to the `convergent` attribute?** They name the set of threads a convergent call belongs to, as an ordinary value use, where the attribute alone is under-specified.
    - **Why does AMDGPU run `StructurizeCFG`, and why doesn't SPIR-V?** AMDGPU needs nested regions to linearize divergent branches under the EXEC mask; SPIR-V shaders need declared merge blocks in the binary, which LLVM's SPIR-V back end computes with its own `SPIRVStructurizer`.

## Where this comes back

!!! next "You will use this again in"

    - [G10. The GPU matmul ladder](g10-matmul-ladder.md): *shared address space*, *uniform addresses and broadcast*
    - [G11. Matrix units](g11-matrix-units.md): *warp-level instructions*, *convergent operations*
    - [G13. Tile languages](g13-tile-languages.md): *lowering to LLVM IR for a GPU back end*
    - [M10. MLIR for GPUs](../mlir/m10-mlir-for-gpus.md): *kernel outlining*, *lowering to NVVM, ROCDL and SPIR-V*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *uniformity*, *which back end to target*

## Sources and further reading

Read the NVPTX guide's tutorial first, then "Convergence And Uniformity" up to its section on irreducible cycles, then the overview and examples of "Convergent Operation Semantics". The pass-pipeline source files are short and worth reading with this chapter's figures open.

[^l1]: LLVM Project, "User Guide for NVPTX Back-end": "Marking Functions as Kernels", "Address Spaces", "NVPTX Intrinsics" and "Tutorial: A Simple Compute Kernel". <https://llvm.org/docs/NVPTXUsage.html>
[^l2]: LLVM Project, "User Guide for AMDGPU Backend": "Target Triples", "Address Spaces", "Register Identifier" and the EXEC-mask example under "DWARF Extensions". <https://llvm.org/docs/AMDGPUUsage.html>
[^l3]: LLVM Project, "User Guide for SPIR-V Target": "Target Triples". <https://llvm.org/docs/SPIRVUsage.html>
[^l4]: LLVM Project, "Convergence And Uniformity": "Introduction", "Motivation", "Uniformity" and "Divergent Cycle Exits". <https://llvm.org/docs/ConvergenceAndUniformity.html>
[^l5]: LLVM Project, "Convergent Operation Semantics": "Overview", "Examples of Convergent Operations", "Convergence Control Intrinsics" and "Uncontrolled Convergent Operations". <https://llvm.org/docs/ConvergentOperations.html>
[^l6]: LLVM Project, "Compiling CUDA with clang": "Compilation Models". <https://llvm.org/docs/CompileCudaWithLLVM.html>
[^l7]: LLVM Project, `StructurizeCFG.cpp`, the pass's header comment and its uniform-region options (main branch, read September 2026). <https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/lib/Transforms/Scalar/StructurizeCFG.cpp>
[^langref]: LLVM Project, "LLVM Language Reference Manual": "Pointer Type" and "Data Layout". <https://llvm.org/docs/LangRef.html#pointer-type>
[^ua]: LLVM Project, `UniformityAnalysis.cpp`, the check for targets without branch divergence (main branch, read September 2026). <https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/lib/Analysis/UniformityAnalysis.cpp>
[^nvptx-tm]: LLVM Project, `NVPTXTargetMachine.cpp`, `NVPTXPassConfig` (main branch, read September 2026). <https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/lib/Target/NVPTX/NVPTXTargetMachine.cpp>
[^amdgpu-tm]: LLVM Project, `AMDGPUTargetMachine.cpp`, `GCNPassConfig::addPreISel` (main branch, read September 2026). <https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/lib/Target/AMDGPU/AMDGPUTargetMachine.cpp>
[^siacf]: LLVM Project, `SIAnnotateControlFlow.cpp`, the file comment and `isUniform` (main branch, read September 2026). <https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/lib/Target/AMDGPU/SIAnnotateControlFlow.cpp>
[^spirv-tm]: LLVM Project, `SPIRVTargetMachine.cpp`, `SPIRVPassConfig::addISelPrepare` (main branch, read September 2026). <https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/lib/Target/SPIRV/SPIRVTargetMachine.cpp>
[^k4]: Khronos Group, "SPIR-V Specification", section 2.11, "Structured Control Flow". <https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#StructuredControlFlow>
[^k4v]: Khronos Group, "SPIR-V Specification", section 2.16.2, "Validation Rules for Shader Capabilities". <https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#ShaderValidation>
[^m16]: MLIR Project, "'gpu' Dialect": `gpu.launch` and the kernel-outlining pass. <https://mlir.llvm.org/docs/Dialects/GPU/>
