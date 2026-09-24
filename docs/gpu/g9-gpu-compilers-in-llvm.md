# G9. GPU compilers inside LLVM

<p class="page-intro">NVPTX, AMDGPU and SPIR-V are ordinary LLVM back ends, built on the same pipeline E1 already walked through, but a SIMT target forces three new questions onto that pipeline: which memory a pointer means, which values can differ across a warp's lanes, and which operations must never move across a branch that only some lanes take. This chapter answers each one with a small, checkable example.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 25 minutes · Builds on: [G8. ISAs and IRs](g8-isas-and-irs.md), [E1. The LLVM code generator pipeline](../backend/e1-llvm-codegen-pipeline.md)</p>

???+ remember "Before you start, remember"

    ??? question "What are the two frameworks LLVM's `llc` uses to turn IR into machine instructions, and which one does `-O2` use by default?"

        SelectionDAG, which builds a small graph per basic block and combines, legalizes and selects on it, and GlobalISel, which works directly on whole functions already in MIR form. `-O2` uses SelectionDAG by default; `-O0` uses GlobalISel.

        Introduced in [E1. The LLVM code generator pipeline](../backend/e1-llvm-codegen-pipeline.md#two-ways-in-selectiondag-and-globalisel).

    ??? question "What does a basic block never contain in its middle?"

        A branch, or a place a branch can land. Control enters a block only at its top and leaves only at its bottom.

        Introduced in [O2. Control-flow graphs and dominance](../optimize/o2-cfg-and-dominance.md).

    ??? question "What is a warp, and what do its threads do together?"

        A fixed-size group of threads that a GPU core issues one instruction to at a time. Every thread in the group either executes that instruction or sits it out, together with the rest of the group.

        Introduced in [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md).

    ??? question "May a Vortex compiler regroup the additions in a floating-point reduction, or fuse a multiply and an add on its own?"

        No. Each `f32` or `f64` operation is exactly one IEEE 754 operation, rounded once, and a compiler may not reorder, reassociate or contract it unless the program asks for that.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Explain what a GPU target adds to the `llc` pipeline E1 already covered, and name the three problems every LLVM-based GPU back end has to solve that an AArch64 or x86-64 back end does not.
    - Read an address space number on an LLVM pointer type and say what decides its meaning, on NVPTX, on AMDGPU, and on a target that defines none of its own.
    - Run a small uniformity analysis by hand, and explain why a value can be divergent even when every one of its operands is uniform.
    - Explain why a barrier or a shuffle must never move across a branch that only some lanes take, and name the mechanism current LLVM uses to protect that guarantee.
    - Trace one divergent branch through `StructurizeCFG`, and connect the result to SPIR-V's structured control-flow requirement.

## One more target, on the same pipeline

E1 followed a four-line integer function through `llc`: SelectionDAG or GlobalISel turns LLVM IR into `MachineInstr` objects in SSA form, a register allocator assigns physical registers, and the MC layer turns the result into bytes. Nothing about that description mentioned a target by name, because none of it is AArch64-specific. The same pipeline, the same `MachineInstr`, the same MIR text format, is what NVPTX, AMDGPU and the SPIR-V back end use too. LLVM's own back end list makes this concrete: run

```text
$ llc --version
```

and read the "Registered Targets" section. On this machine (LLVM 18.1.8, observed locally, 24 September 2026) it lists only `aarch64`, `aarch64_32`, `aarch64_be`, `arm64` and `arm64_32`: the AArch64 family, and nothing else. LLVM ships NVPTX, AMDGPU and SPIR-V as separate target components, built in only when a distribution enables them; this build does not. Every example in this chapter that needs a real GPU target therefore stays with the target-independent parts of the pipeline, `opt` and IR-level passes, or compiles for AArch64 to show what stays the same across targets. Where the NVPTX or AMDGPU documentation describes something this machine cannot run, the text says so.

What changes, going from AArch64 to a GPU target, is not the pipeline. It is three questions a GPU back end has to answer that a CPU back end never asks, because a CPU back end compiles code for one thread at a time and a GPU back end compiles one program that many threads run together, in lockstep, as a **warp**: which physical memory a pointer means (address spaces), which values are guaranteed to be the same across every thread of a warp (uniformity), and which operations must keep every thread of a warp doing the same thing at the same time, never split apart by an optimization (convergence). The rest of this chapter takes each question in turn, with a small example for each, and ends by showing that a structured-control-flow target such as SPIR-V adds a fourth, related job: rewriting arbitrary control flow into a shape it is legal to express at all.

## Address spaces: which memory a pointer means

Take the smallest possible reason a GPU kernel needs more than one kind of pointer: one value already sitting in the device's large, slow main memory, and one value already staged into a small, fast, on-chip scratchpad that only the threads of one thread block can see. Ordinary LLVM IR gives a pointer type nothing to tell these apart; `ptr` is `ptr`, LLVM's single opaque pointer type since IR dropped pointee types. The example below adds exactly one number to each pointer to fix that.

--8<-- "includes/examples/gpu/g9-gpu-compilers-in-llvm/addrspaces.ll.md"

`ptr addrspace(1)` and `ptr addrspace(3)` are the same LLVM pointer type, `ptr`, each carrying a small integer tag: an **address space**. The IR verifier treats the number as opaque data, nothing more: `opt -S -passes=verify` accepts any pointer with any address space number, and two pointers with different numbers are never assumed to alias. What a specific number *means*, whether it is global memory, on-chip shared memory, a register-mapped constant bank, or nothing special at all, is a decision the target's back end makes, not a rule the IR itself enforces. Compile this same file for AArch64, this machine's only registered target, and every load or store in it becomes an ordinary `ldr` or `str`, addrspace 1 and addrspace 3 treated identically: AArch64 has no notion of a "shared" or "constant" address space distinct from ordinary memory, so its back end is free to treat every address space as identical, and it does. The tag is meaningful only to a back end that chooses to read it.

NVPTX's back end does choose to read it, and documents a fixed table: address space 1 is global memory, 3 is shared memory, 4 is constant memory, 5 is thread-local memory, and 7 names a newer per-cluster shared memory.[^l1] AMDGPU defines its own set, by name rather than by a single shared numbering: `flat`, `global`, `region`, `local` (its equivalent of shared memory, also called LDS), `constant` and `private`.[^l2] The numbers themselves are conventions specific to each target, agreed between the front end that emits the IR and the back end that consumes it; nothing in LLVM's core IR ties address space 1 to global memory in general, only NVPTX's back end and its documentation do. This is also why a kernel's entry point needs a calling convention a normal function does not: NVPTX defines `ptx_kernel` for exactly that purpose, marking which functions a host launches versus which ones only a kernel calls internally.[^l1]

The other half of getting a value into a kernel at all is deciding which code counts as "the kernel" in the first place. A GPU program usually starts as ordinary functions calling other functions; something has to lift the one function that runs on the device out into its own compilation unit, decide its calling convention, and mark it as an entry point. LLVM's classic route does this by attribute and metadata on an existing function. MLIR's `gpu` dialect names the same job **kernel outlining**: a dedicated pass that moves the body of a parallel region into a new function inside a separate `gpu.module`, leaving a `gpu.launch_func` call behind at the call site.[^m16] Both approaches solve the same problem, drawing the boundary between host code and device code; MLIR's version is covered in depth once the GPU dialect itself is, in [M10. MLIR for GPUs](../mlir/m10-mlir-for-gpus.md).

??? check "Why does compiling `addrspaces.ll` on this machine's AArch64 `llc` produce ordinary `ldr`/`str` instructions instead of anything address-space-specific?"

    Because AArch64's back end does not define distinct physical memories for address spaces 1 and 3; Apple silicon's unified memory model has no separate on-chip shared-memory bank the way an NVIDIA SM does. An address space number is meaningful only to a back end that chooses to interpret it, and AArch64's back end chooses not to, so every address space compiles to the same load or store it would use for address space 0.

## Uniformity: same value, or one value per lane

A warp executes one instruction for all of its threads at once. If every thread's copy of a value is guaranteed identical, a compiler can hold that value in a single scalar register, shared across the warp; if two threads can hold different values, the compiler needs one register's worth of storage per lane, a vector register. Deciding which case applies to which value is **uniformity analysis**, and it runs before or alongside instruction selection on every SIMT target, because it decides which kind of register a value gets.[^l4]

The seed of every divergent value is the thread's own identity: whatever intrinsic reads a lane's index within its warp returns a different number for every lane, by definition, so it starts out divergent, and nothing can make it uniform again. From there, two rules decide everything else. The first is ordinary data flow: any value computed from a divergent operand is divergent, the same way any value computed from an uninitialized one would be tainted. The second is less obvious, and it is the one worth working through by hand: a value can be divergent even when every operand feeding it is uniform, if the block that computes it is only reached by some of a divergent branch's lanes. Different lanes disagree about whether that block runs at all, so even a constant computed inside it counts as divergent, because some lanes never see it get computed.

<figure class="vx-figure">
<svg viewBox="0 0 640 460" role="img" aria-labelledby="g9-fig-title g9-fig-desc">
<title id="g9-fig-title">Uniformity propagating through a divergent branch</title>
<desc id="g9-fig-desc">Block entry computes tid, a divergent seed, base, a uniform constant, and cond, divergent because it reads tid. The branch on cond is divergent, so two lanes of the same warp can take different arms. Block then computes a_then from base alone, and block else computes a_else from base alone; both are marked divergent anyway, dashed, because each block is reached by only some of the branch's lanes. Block merge computes r as a phi of a_then and a_else, divergent because both its operands already are.</desc>
<rect class="vx-box" x="210" y="15" width="220" height="110" rx="4"/>
<text class="vx-text" x="320" y="35" text-anchor="middle">entry</text>
<text class="vx-mono" x="320" y="55" text-anchor="middle">tid = thread id</text>
<text class="vx-mono" x="320" y="72" text-anchor="middle">base = 5</text>
<text class="vx-mono vx-text-accent" x="320" y="89" text-anchor="middle">cond = tid &lt; 2</text>
<text class="vx-text-muted" x="320" y="107" text-anchor="middle">tid, cond: divergent · base: uniform</text>
<path class="vx-flow" d="M270 125 C 220 155, 170 165, 135 190"/>
<polygon class="vx-arrowhead" points="135,185 143,190 133,196"/>
<path class="vx-flow" d="M370 125 C 420 155, 470 165, 505 190"/>
<polygon class="vx-arrowhead" points="505,185 513,190 503,196"/>
<rect class="vx-box-bad" x="40" y="190" width="190" height="80" rx="4"/>
<text class="vx-text" x="135" y="212" text-anchor="middle">then</text>
<text class="vx-mono vx-text-accent" x="135" y="233" text-anchor="middle">a_then = base * 2</text>
<text class="vx-text-muted" x="135" y="253" text-anchor="middle">divergent: block-only</text>
<rect class="vx-box-bad" x="410" y="190" width="190" height="80" rx="4"/>
<text class="vx-text" x="505" y="212" text-anchor="middle">else</text>
<text class="vx-mono vx-text-accent" x="505" y="233" text-anchor="middle">a_else = base * 3</text>
<text class="vx-text-muted" x="505" y="253" text-anchor="middle">divergent: block-only</text>
<path class="vx-line" d="M150 270 C 190 300, 210 310, 260 335"/>
<polygon class="vx-arrowhead" points="260,330 268,336 258,341"/>
<path class="vx-line" d="M490 270 C 450 300, 430 310, 380 335"/>
<polygon class="vx-arrowhead" points="380,330 372,336 382,341"/>
<rect class="vx-box-bad" x="210" y="335" width="220" height="90" rx="4"/>
<text class="vx-text" x="320" y="357" text-anchor="middle">merge</text>
<text class="vx-mono vx-text-accent" x="320" y="378" text-anchor="middle">r = phi(a_then, a_else)</text>
<text class="vx-text-muted" x="320" y="398" text-anchor="middle">divergent: operands already are</text>
<circle class="vx-dot" r="5">
<animateMotion dur="6s" repeatCount="indefinite" path="M270 125 C 220 155, 170 165, 135 190 L135 230 C190 300, 210 310, 260 335 L320 335" keyPoints="0;0;1;1" keyTimes="0;0.05;0.9;1" calcMode="linear"/>
</circle>
<circle class="vx-dot" r="5">
<animateMotion dur="6s" repeatCount="indefinite" path="M370 125 C 420 155, 470 165, 505 190 L505 230 C450 300, 430 310, 380 335 L320 335" keyPoints="0;0;1;1" keyTimes="0;0.05;0.9;1" calcMode="linear"/>
</circle>
<text class="vx-text-muted" x="320" y="445" text-anchor="middle">two lanes of one warp, taking different arms of the same divergent branch, rejoin at merge</text>
</svg>
<figcaption>Figure 1. Uniformity propagating through a divergent branch. Solid boxes hold values every lane agrees on; dashed boxes hold values that can differ per lane, either because they read a divergent operand (<code>cond</code>, from <code>tid</code>) or because their block is reached by only some of a divergent branch's lanes (<code>a_then</code>, <code>a_else</code>), a rule that then carries into <code>r</code> at the merge point regardless of what <code>r</code> itself reads.</figcaption>
</figure>

The example below builds the same four-block, six-value program in code and applies exactly those two rules, once, in program order.

--8<-- "includes/examples/gpu/g9-gpu-compilers-in-llvm/uniformity.cpp.md"

`tid` starts divergent, by the seed rule. `cond` uses `tid`, so it is divergent by the first rule: data flow. `base` uses nothing, so it stays uniform. `a_then` and `a_else` are each computed only from `base`, a uniform value, so the first rule alone would call them uniform; but each one lives in a block that only one side of the branch on `cond` reaches, so the second rule marks both of them divergent regardless. Finally `r`, the value defined where the two branches rejoin, reads `a_then` and `a_else` as its two operands (this is exactly what a phi node does: it is a value, not a control-flow construct, with one operand per predecessor block); since both operands are already divergent, `r` is divergent by the first rule again, with no separate case needed for merge points. The two rules alone are enough to explain every line of output: the second rule is what makes uniformity genuinely a property of the program's shape, not only of its data.

??? check "In the example, `a_then` is computed only from `base`, a uniform value. Why is `a_then` divergent anyway?"

    Because uniformity is not only about which values feed an operation, it is about which lanes execute the operation at all. `a_then` lives inside the `then` block, which only the lanes that took the `then` side of the branch on `cond` reach. Some lanes of the warp compute `a_then`, and some never do, which is enough to make the value divergent even though every lane that does compute it would compute the same number.

## Convergent operations: what must never move

Some operations are not only sensitive to which lanes run them, they depend on a specific, known set of lanes running them together, at the same point in the program. A **barrier** that makes every thread of a block wait until all threads have reached it is meaningless if an optimization hoists it out of one arm of a branch and not the other: the threads that took the other arm never reach a matching barrier, and the ones that did are left waiting for threads that will never arrive. A **shuffle**, which reads a value directly out of another lane's registers, is meaningless if the compiler duplicates it into two different points in the program, because "another lane" can mean a different, wrong lane depending on which copy runs. Both operations are marked **convergent**: LLVM's `addrspaces.ll` example above declares `@barrier` with exactly this attribute, and the rule it carries is narrow and absolute, not a hint. A convergent operation must keep executing with the same set of threads that would have executed it in the unoptimized program; no general-purpose transformation is allowed to change that set, by moving the call across a branch, by duplicating it into two branches, or by sinking it into a loop that would run it a different number of times per lane.[^l5]

Older LLVM releases expressed this with the `convergent` function attribute alone, which said only "do not touch this call," without saying which other convergent calls in the function it needed to stay grouped with. Current LLVM adds **convergence-control tokens**: intrinsic calls such as `llvm.experimental.convergence.entry` and `llvm.experimental.convergence.anchor` each produce a token value, and a convergent call carries one of these tokens in a `convergencectrl` operand bundle, naming the specific convergence region it belongs to.[^l5] This matters once a function has more than one convergent operation and more than one loop or branch nesting them differently; the attribute alone cannot say which barrier corresponds to which, while a token makes that relationship an explicit data dependency the rest of the optimizer already knows how to preserve, because respecting a value's dependencies is a rule every pass already follows.

## Structured control flow: what SPIR-V requires

A branch on a divergent condition is exactly what the `then`/`else` example above modeled: some lanes take one arm, some the other, and both arms eventually rejoin. NVPTX and AMDGPU can represent that directly, as an ordinary conditional branch in IR that the hardware executes by running both arms with the inactive lanes masked off. LLVM's own SPIR-V back end takes the same kind of IR as its input, through target triples such as `spirv64` and a logical-addressing `spirv` variant, for the OpenCL and Vulkan environments.[^l3] What it must produce is different: SPIR-V is defined as **structured control flow**. Its specification requires that every selection and every loop declare its own merge block up front, that each header block "structurally dominate" that merge block, and that the resulting regions nest, entered and exited only in the specific ways the specification lists, never by a branch that jumps into the middle of another construct or out of it early.[^k4] An arbitrary, reducible CFG, the general shape `opt` and `llc` both work with everywhere else in this book, is not automatically legal SPIR-V, and something has to turn one into the other before a SPIR-V back end can emit it.

That something is `StructurizeCFG`, an LLVM pass that rewrites an arbitrary CFG into single-entry, single-exit nested regions, turning a divergent branch's control-flow choice into a data-flow one: a boolean value that says which side each lane's execution belongs to, computed once and carried through a new block the pass inserts.[^l7] Which back ends run it automatically, and under which conditions, is not settled by any one document this chapter can point to; running the pass directly, on a small example, shows what it does without needing that answer.

--8<-- "includes/examples/gpu/g9-gpu-compilers-in-llvm/structurize.ll.md"

Before the pass, `lane_select` is the same divergent-branch shape as the uniformity example: `entry` branches to `then` or `else` on a per-lane condition, and both rejoin at `merge`. After it, there is a new block, `Flow`, and the branch out of `entry` no longer goes to `then` or `else` at all; it goes to `else` or `Flow`, and `Flow` in turn holds two phi nodes and its own branch, one that decides whether to visit `then`. (The pass also flips the sense of the original comparison, `icmp sgt` becomes `icmp sle`; this is `StructurizeCFG` choosing which arm to reach directly and which to route through `Flow`, and it changes nothing about which value `lane_select` returns for a given `%lane`.) The two new phi nodes are the boolean and the partial result: one of them, `%1`, is exactly the "does this lane still need to visit `then`" value the second rule of uniformity analysis was tracking by hand a section ago, now made an explicit value in the IR instead of an implicit property of which block a lane is standing in. Every block in the rewritten function has exactly one predecessor edge that matters for reaching it structurally, and the whole shape nests: `Flow` inside `entry`'s successor, `then` inside `Flow`'s successor, both closing at `merge`. That is precisely SPIR-V's requirement, produced mechanically from a CFG that did not meet it.

??? check "After `StructurizeCFG` runs, does `lane_select` still return the same value as before, for every input?"

    Yes. The pass changes how the choice between the two arms is represented, from a branch that skips one arm entirely to a nested structure where every lane's path is bounded and a boolean phi records which result to use, but it computes the same function. Nothing about the pass is an optimization in the sense of doing less work; it is a legalization, changing the shape of the computation without changing what it computes, the same relationship instruction selection has to the IR it consumes.

## Still one pipeline

Put the three sections together, and a GPU back end's difference from E1's AArch64 walkthrough is additive, not a replacement. The same `MachineInstr` objects, the same SSA-to-physical-register handoff, the same MC layer at the end, still apply; NVPTX and AMDGPU are back ends in exactly E1's sense, described by the same kind of TableGen target description a native back end uses. What a SIMT target's description adds is: address space numbers with target-specific meaning, read by instruction selection when it decides where a load or store goes; a uniformity analysis, run early enough that later passes know which values need one register per lane and which need one register for the whole warp; and convergence tokens, threaded through any pass that might otherwise reorder, duplicate, or sink a call across control flow it should not cross. A target with a structured IR at its far end, such as SPIR-V, adds one more step before final lowering: `StructurizeCFG` or an equivalent legalization, turning whatever CFG the optimizer produced into the nested shape that target's binary format requires.

None of these four problems, address spaces, uniformity, convergence, structured control flow, is specific to CUDA, to HIP, or to any one vendor's programming model. They are what "compile one program for many lanes running together" costs any back end that takes it on, LLVM-based or not, and a compiler that targets GPUs by writing its own back end instead of going through LLVM inherits the same four problems, unsolved, on day one.

## For Vortex

!!! vortex "Exercise"

    **Pick one target you are not going to build a back end for yet, NVPTX or AMDGPU, and work through your matmul kernel's addresses and values on paper against it.** Take the naive one-thread-per-output-element mapping of
    `multiply(a: &[f32; M, K], b: &[f32; K, N], c: &mut [f32; M, N])`
    ([stage 10](../compiler/guide/stage-10-matrix-multiplication.md)): each thread computes its own `row` and `column` from its lane and block indices, then loops `k` from `0` to `K`, reading `a[row, k]` and `b[k, column]`, accumulating into a running sum, and finally storing that sum into `c[row, column]`.

    Write down, using the two rules from the uniformity section, whether each of `row`, `column`, `k`, the address computed for `a[row, k]`, the address computed for `b[k, column]`, the running sum, and the final address written into `c` is uniform or divergent across one warp of threads, and say in one sentence why for each. Then, using your chosen target's address-space table (NVPTX's or AMDGPU's), write one sentence per pointer in the kernel, `a`, `b` and `c`, naming the address space it would carry if every buffer starts in the device's main memory, and one more sentence saying what would have to change about that address space if a later version of the kernel first copied a tile of `a` or `b` into on-chip shared memory before reading it, the way [G10](g10-matmul-ladder.md) eventually will.

    **Not yet.** Do not write NVPTX, AMDGPU or LLVM IR that lowers this kernel, and do not try to install an NVPTX- or AMDGPU-enabled LLVM on this machine to test it: this exercise is about reading the two analyses this chapter introduced against a kernel you already have, not producing a new implementation. Do not decide here whether Vortex's own back end will go through LLVM at all; that choice belongs to [stage 6](../compiler/guide/stage-6-first-machine-code.md#choosing-a-back-end) and [E1](../backend/e1-llvm-codegen-pipeline.md).

    **Done when** you have a written uniform-or-divergent judgment, with a reason, for every named value in the mapping above; one sentence per pointer naming its address space under your chosen target; and one sentence identifying which of the kernel's per-thread values are the same for every thread in a whole thread block, the observation [G10](g10-matmul-ladder.md) turns into a real optimization once shared-memory tiling is on the table.

## Key ideas

!!! recap "You can now answer"

    - **What does a GPU target add to the pipeline E1 already described?** Nothing that replaces it: the same `MachineInstr`, SSA-to-physical-register handoff and MC layer still apply. It adds target-specific meanings for address spaces, a uniformity analysis, convergence tracking, and, for a structured target, a legalization pass.
    - **What decides what an address space number means?** The target's back end. LLVM's IR verifier treats the number as opaque; NVPTX and AMDGPU each define their own table, and a target that defines none, such as this machine's AArch64, treats every address space identically.
    - **Why can a value be divergent even though every operand feeding it is uniform?** Because uniformity depends on which lanes execute the block that computes it, not only on the values that block reads. A value defined inside only one arm of a divergent branch is divergent by that alone.
    - **Why must a convergent operation never be moved across a branch that only some lanes take?** Because operations such as barriers and shuffles depend on a specific set of threads executing together; moving or duplicating the call can strand some threads waiting for others that never arrive, or read a value from the wrong lane.
    - **What does `StructurizeCFG` change, and what does it leave the same?** It changes an arbitrary CFG's shape into nested, single-entry single-exit regions, and it turns a branch's control-flow choice into a boolean value carried through a new block. It leaves the function computing the same result for the same input.
    - **Why is SPIR-V's structured control-flow requirement a real constraint, not a style preference?** Because SPIR-V's binary format defines a selection or loop construct by its merge block, declared up front; a CFG that does not nest cleanly cannot be expressed in that format at all, whatever it computes.

## Where this comes back

!!! next "You will use this again in"

    - [G10. The GPU matmul ladder](g10-matmul-ladder.md): *address spaces*, *which per-thread values are shared across a thread block*
    - [G11. Matrix units](g11-matrix-units.md): *cooperative, warp-wide instructions that assume their inputs are already in the right per-lane or uniform place*
    - [G6. Synchronization, atomics and reductions](g6-synchronization.md): *convergent barriers and shuffles, in depth*
    - [M10. MLIR for GPUs](../mlir/m10-mlir-for-gpus.md): *kernel outlining as a structural pass, instead of an attribute on an existing function*

## Sources and further reading

[^l1]: LLVM Project, "User Guide for NVPTX Back-end". <https://llvm.org/docs/NVPTXUsage.html>
[^l2]: LLVM Project, "User Guide for AMDGPU Backend". <https://llvm.org/docs/AMDGPUUsage.html>
[^l3]: LLVM Project, "User Guide for SPIR-V Target". <https://llvm.org/docs/SPIRVUsage.html>
[^l4]: LLVM Project, "Convergence And Uniformity". <https://llvm.org/docs/ConvergenceAndUniformity.html>
[^l5]: LLVM Project, "Convergent Operation Semantics". <https://llvm.org/docs/ConvergentOperations.html>
[^l7]: LLVM Project, "StructurizeCFG.cpp" (source). <https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/lib/Transforms/Scalar/StructurizeCFG.cpp>
[^k4]: Khronos Group, "SPIR-V Specification", section 2.11, "Structured Control Flow". <https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#StructuredControlFlow>
[^m16]: MLIR, "'gpu' Dialect". <https://mlir.llvm.org/docs/Dialects/GPU/>
