# G5. Occupancy and latency hiding

<p class="page-intro">A GPU hides the time an instruction takes by always having some other independent instruction ready to issue. This chapter counts how many are needed with Little's law, shows the two places they can come from (more resident warps, or more independent work inside each thread), works out the budgets that cap the first, and follows the measurements in which a kernel ran faster at lower occupancy. A Vortex compiler that picks thread counts and tile sizes has to reason about both.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 40 minutes · Builds on: [G2. The SIMT execution model](g2-simt.md), [G3. The GPU memory hierarchy](g3-memory-hierarchy.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is a warp?"

        A group of threads, 32 on NVIDIA and Apple GPUs, that the hardware issues one instruction to at a time. Each thread is one lane of the warp.

        Introduced in [G2. The SIMT execution model](g2-simt.md).

    ??? question "What happens when a thread needs more live values than the registers it has been given?"

        The compiler spills some of them to local memory, which despite its name is a private slice of slow device memory, not on-chip storage.

        Introduced in [G3. The GPU memory hierarchy](g3-memory-hierarchy.md).

    ??? question "Why does a loop that adds into one accumulator run at the speed of the adder's latency rather than its throughput?"

        Each addition needs the previous one's result, so the additions form one dependency chain. Only several independent chains can keep a pipelined unit busy.

        Introduced in [P5. The microarchitecture shelf](../optimize/p5-microarchitecture.md).

    ??? question "May a Vortex compiler split the sum in a dot product into several partial sums?"

        No. Each floating-point operation is one IEEE 754 operation, rounded once, and must not be contracted, reassociated or reordered.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Explain how a warp scheduler hides latency, and when a cycle goes by with nothing issued.
    - Compute an SM's occupancy for a kernel from its block size, registers per thread and shared memory per block, and name the budget that limits it.
    - Use Little's law to compute how many independent operations must be in flight to reach a machine's throughput.
    - Explain why a kernel can reach full throughput at low occupancy through instruction-level parallelism, and why that often makes it faster.
    - Recognize which of these choices a Vortex compiler may make, and which one decision 56 forbids.

## A warp that waits

Start with the inner loop of the matrix product from [G4](g4-memory-performance.md#which-index-runs-across-the-warp), one thread per output element:

```vortex
// fragment
let mut sum: f32 = 0.0;
for k in 0..64 {
    sum += a[row, k] * b[k, column];
}
```

Every trip through this loop adds into `sum`, and every addition needs the result of the one before it. That is a single **dependency chain**, as in [P5](../optimize/p5-microarchitecture.md#latency-throughput-and-the-number-of-chains). On a GPU the whole warp runs this chain in step, 32 lanes at a time, so the warp as a whole must wait after each addition until its result comes back. The time from issuing an instruction to its result being usable is the instruction's **latency**. While the warp waits, it is **stalled**: it has an instruction to run, but not the input that instruction needs.

The hardware does not wait with it. Each SM (streaming multiprocessor) is split into four **sub-partitions**, each with its own warp scheduler, register file and execution units, and a warp stays on one sub-partition from launch to completion.[^ncu-sm] Every cycle, each scheduler looks at the warps it holds, its **active** warps, and picks one that is not stalled, an **eligible** warp, to issue from. A cycle with no eligible warp is a skipped **issue slot**, and NVIDIA's profiler guide says that many skipped slots indicate poor latency hiding.[^ncu-sched]

So there are two ways to fill the slots while one chain waits. The scheduler can switch to another warp's chain, or the same warp can have a second chain whose next instruction does not depend on the first. The first example is a toy scheduler with one issue slot per cycle and a latency of 8 cycles, numbers chosen for the illustration rather than taken from any GPU. Each warp runs 64 multiply-adds spread over some number of independent accumulators, and the program records which warp issued in each of the first 16 cycles.

--8<-- "includes/examples/gpu/g5-occupancy/scheduler.cpp.md"

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Issue slots over 16 cycles for three configurations with an 8-cycle latency: one warp with one chain issues on 2 of 16 cycles, four warps with one chain each issue on 8, and one warp with four independent chains also issues on 8." aria-describedby="g5-f1-desc">
<title id="g5-f1-title">Issue slots of a toy scheduler with an 8-cycle latency</title>
<desc id="g5-f1-desc">Three rows of 16 squares, one square per cycle. A filled square holds the letter of the warp that issued in that cycle; an empty square is a cycle with no eligible warp. Row one: one warp with one dependency chain issues in cycles 0 and 8 only. Row two: four warps, A to D, issue in cycles 0 to 3 and 8 to 11. Row three: one warp with four independent accumulators issues in cycles 0 to 3 and 8 to 11, the same pattern as row two.</desc>
<text class="vx-text" x="20" y="26">16 cycles of one scheduler, result latency 8 cycles</text>
<text class="vx-text-muted" x="263" y="56" text-anchor="middle">0</text>
<text class="vx-text-muted" x="293" y="56" text-anchor="middle">1</text>
<text class="vx-text-muted" x="323" y="56" text-anchor="middle">2</text>
<text class="vx-text-muted" x="353" y="56" text-anchor="middle">3</text>
<text class="vx-text-muted" x="383" y="56" text-anchor="middle">4</text>
<text class="vx-text-muted" x="413" y="56" text-anchor="middle">5</text>
<text class="vx-text-muted" x="443" y="56" text-anchor="middle">6</text>
<text class="vx-text-muted" x="473" y="56" text-anchor="middle">7</text>
<text class="vx-text-muted" x="503" y="56" text-anchor="middle">8</text>
<text class="vx-text-muted" x="533" y="56" text-anchor="middle">9</text>
<text class="vx-text-muted" x="563" y="56" text-anchor="middle">10</text>
<text class="vx-text-muted" x="593" y="56" text-anchor="middle">11</text>
<text class="vx-text-muted" x="623" y="56" text-anchor="middle">12</text>
<text class="vx-text-muted" x="653" y="56" text-anchor="middle">13</text>
<text class="vx-text-muted" x="683" y="56" text-anchor="middle">14</text>
<text class="vx-text-muted" x="713" y="56" text-anchor="middle">15</text>
<text class="vx-text" x="20" y="92">1 warp, 1 chain</text>
<text class="vx-text-muted" x="20" y="112">2 of 16 slots used</text>
<rect class="vx-cell-on" x="250" y="72" width="26" height="30"/>
<text class="vx-text" x="263" y="92" text-anchor="middle">A</text>
<rect class="vx-box" x="280" y="72" width="26" height="30"/>
<rect class="vx-box" x="310" y="72" width="26" height="30"/>
<rect class="vx-box" x="340" y="72" width="26" height="30"/>
<rect class="vx-box" x="370" y="72" width="26" height="30"/>
<rect class="vx-box" x="400" y="72" width="26" height="30"/>
<rect class="vx-box" x="430" y="72" width="26" height="30"/>
<rect class="vx-box" x="460" y="72" width="26" height="30"/>
<rect class="vx-cell-on" x="490" y="72" width="26" height="30"/>
<text class="vx-text" x="503" y="92" text-anchor="middle">A</text>
<rect class="vx-box" x="520" y="72" width="26" height="30"/>
<rect class="vx-box" x="550" y="72" width="26" height="30"/>
<rect class="vx-box" x="580" y="72" width="26" height="30"/>
<rect class="vx-box" x="610" y="72" width="26" height="30"/>
<rect class="vx-box" x="640" y="72" width="26" height="30"/>
<rect class="vx-box" x="670" y="72" width="26" height="30"/>
<rect class="vx-box" x="700" y="72" width="26" height="30"/>
<text class="vx-text" x="20" y="162">4 warps, 1 chain each</text>
<text class="vx-text-muted" x="20" y="182">8 of 16 slots used</text>
<rect class="vx-cell-on" x="250" y="142" width="26" height="30"/>
<text class="vx-text" x="263" y="162" text-anchor="middle">A</text>
<rect class="vx-cell-on" x="280" y="142" width="26" height="30"/>
<text class="vx-text" x="293" y="162" text-anchor="middle">B</text>
<rect class="vx-cell-on" x="310" y="142" width="26" height="30"/>
<text class="vx-text" x="323" y="162" text-anchor="middle">C</text>
<rect class="vx-cell-on" x="340" y="142" width="26" height="30"/>
<text class="vx-text" x="353" y="162" text-anchor="middle">D</text>
<rect class="vx-box" x="370" y="142" width="26" height="30"/>
<rect class="vx-box" x="400" y="142" width="26" height="30"/>
<rect class="vx-box" x="430" y="142" width="26" height="30"/>
<rect class="vx-box" x="460" y="142" width="26" height="30"/>
<rect class="vx-cell-on" x="490" y="142" width="26" height="30"/>
<text class="vx-text" x="503" y="162" text-anchor="middle">A</text>
<rect class="vx-cell-on" x="520" y="142" width="26" height="30"/>
<text class="vx-text" x="533" y="162" text-anchor="middle">B</text>
<rect class="vx-cell-on" x="550" y="142" width="26" height="30"/>
<text class="vx-text" x="563" y="162" text-anchor="middle">C</text>
<rect class="vx-cell-on" x="580" y="142" width="26" height="30"/>
<text class="vx-text" x="593" y="162" text-anchor="middle">D</text>
<rect class="vx-box" x="610" y="142" width="26" height="30"/>
<rect class="vx-box" x="640" y="142" width="26" height="30"/>
<rect class="vx-box" x="670" y="142" width="26" height="30"/>
<rect class="vx-box" x="700" y="142" width="26" height="30"/>
<text class="vx-text" x="20" y="232">1 warp, 4 chains</text>
<text class="vx-text-muted" x="20" y="252">8 of 16 slots used</text>
<rect class="vx-cell-on" x="250" y="212" width="26" height="30"/>
<text class="vx-text" x="263" y="232" text-anchor="middle">A</text>
<rect class="vx-cell-on" x="280" y="212" width="26" height="30"/>
<text class="vx-text" x="293" y="232" text-anchor="middle">A</text>
<rect class="vx-cell-on" x="310" y="212" width="26" height="30"/>
<text class="vx-text" x="323" y="232" text-anchor="middle">A</text>
<rect class="vx-cell-on" x="340" y="212" width="26" height="30"/>
<text class="vx-text" x="353" y="232" text-anchor="middle">A</text>
<rect class="vx-box" x="370" y="212" width="26" height="30"/>
<rect class="vx-box" x="400" y="212" width="26" height="30"/>
<rect class="vx-box" x="430" y="212" width="26" height="30"/>
<rect class="vx-box" x="460" y="212" width="26" height="30"/>
<rect class="vx-cell-on" x="490" y="212" width="26" height="30"/>
<text class="vx-text" x="503" y="232" text-anchor="middle">A</text>
<rect class="vx-cell-on" x="520" y="212" width="26" height="30"/>
<text class="vx-text" x="533" y="232" text-anchor="middle">A</text>
<rect class="vx-cell-on" x="550" y="212" width="26" height="30"/>
<text class="vx-text" x="563" y="232" text-anchor="middle">A</text>
<rect class="vx-cell-on" x="580" y="212" width="26" height="30"/>
<text class="vx-text" x="593" y="232" text-anchor="middle">A</text>
<rect class="vx-box" x="610" y="212" width="26" height="30"/>
<rect class="vx-box" x="640" y="212" width="26" height="30"/>
<rect class="vx-box" x="670" y="212" width="26" height="30"/>
<rect class="vx-box" x="700" y="212" width="26" height="30"/>
<text class="vx-text-accent" x="20" y="286">rows two and three keep the same number of operations in flight: 4</text>
</svg>
<figcaption>Figure 1. The first 16 cycles of three runs of the toy scheduler. One warp with one chain issues once every 8 cycles and leaves 14 of 16 slots empty. Four warps with one chain each, and one warp with four independent chains, fill the same 8 slots: the scheduler does not care where the independent instructions come from.</figcaption>
</figure>

Read the table from the top. One warp with one chain keeps the issue slot busy 12 percent of the time: it issues, then waits 8 cycles. Two warps double that, four warps double it again, and eight warps fill every cycle, because by the time warp H has issued, warp A's result is back. Sixteen warps cannot do better than every cycle; the extra warps only wait their turn. The lower half of the table gets the same percentages from one or two warps with more chains each. One warp with eight independent accumulators is as busy as eight warps with one.

??? check "In the toy scheduler, how many cycles does one warp with two chains need for its 64 instructions, and why is that about half of what one chain needs?"

    About 250 (the example prints 250). With two accumulators, instructions alternate between two chains, so two instructions issue back to back and then the warp waits for the first result: 2 issues per 8 cycles instead of 1. One chain needs 505 cycles, 63 waits of 8 plus the first issue.

## Occupancy: how many warps an SM can hold

The first way to hide latency, more warps, depends on how many warps an SM can keep **resident**: assigned to a scheduler, with their registers and shared memory allocated, ready to issue. **Occupancy** is the number of active warps on an SM divided by the most it can hold.[^bp-occ] It is a count of warps, not of work: it says nothing about how many independent instructions each warp has.

Three budgets decide how many warps fit, because the hardware places whole thread blocks on an SM and a block is placed only when all its resources are free.[^pg-occ] Take a Hopper SM, compute capability 9.0 (NVIDIA's version number for a GPU's features). Its guide gives these limits: 64 resident warps, 32 resident blocks, 65,536 32-bit registers, 228 KB of shared memory, and at most 255 registers per thread.[^hopper-occ]

- **Warp and block slots.** A block of 256 threads is 8 warps, so at most 64 ÷ 8 = 8 such blocks fit, well under the 32-block limit.
- **Registers.** At 32 registers per thread, one block uses 32 × 256 = 8,192 registers, and 65,536 ÷ 8,192 = 8 blocks fit. At 64 registers per thread, a block uses 16,384, and only 4 fit.
- **Shared memory.** A block that declares 32 KB of shared memory leaves room for 228 ÷ 32 = 7.1, so 7 blocks.

The number of resident blocks is the smallest of these, and occupancy follows from it. At 32 registers per thread and no shared memory, all budgets allow 8 blocks: 64 warps, 100 percent. At 64 registers per thread, registers allow 4 blocks: 32 warps, 50 percent, with half the warp slots empty and nothing able to use them. Figure 2 draws both.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="Two kernels with 256-thread blocks on a Hopper SM. At 32 registers per thread, eight blocks fill both the register file and the 64 warp slots. At 64 registers per thread, four blocks fill the register file while only 32 of the 64 warp slots are used." aria-describedby="g5-f2-desc">
<title id="g5-f2-title">One SM, two budgets, two kernels</title>
<desc id="g5-f2-desc">Each kernel has two bars of 64 cells. In the register bar each cell is 1,024 registers, one sixty-fourth of the 65,536-register file. In the warp bar each cell is one of the 64 warp slots. Blocks are shaded alternately. Kernel one, 32 registers per thread: each block takes 8 register cells and 8 warp cells, and 8 blocks fill both bars. Kernel two, 64 registers per thread: each block takes 16 register cells and 8 warp cells, so 4 blocks fill the register bar while the warp bar is half empty.</desc>
<text class="vx-text" x="20" y="26">256-thread blocks on one SM: 65,536 registers, 64 warp slots</text>
<text class="vx-text" x="20" y="60">32 registers per thread: 8 blocks, 64 warps, 100 percent</text>
<text class="vx-text-muted" x="20" y="88">registers</text>
<rect class="vx-box-accent" x="170" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="179" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="188" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="197" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="206" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="215" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="224" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="233" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="242" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="251" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="260" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="269" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="278" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="287" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="296" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="305" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="314" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="323" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="332" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="341" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="350" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="359" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="368" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="377" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="386" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="395" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="404" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="413" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="422" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="431" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="440" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="449" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="458" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="467" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="476" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="485" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="494" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="503" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="512" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="521" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="530" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="539" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="548" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="557" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="566" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="575" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="584" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="593" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="602" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="611" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="620" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="629" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="638" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="647" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="656" y="74" width="8" height="20"/>
<rect class="vx-box-accent" x="665" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="674" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="683" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="692" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="701" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="710" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="719" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="728" y="74" width="8" height="20"/>
<rect class="vx-box-strong" x="737" y="74" width="8" height="20"/>
<text class="vx-text-muted" x="20" y="118">warp slots</text>
<rect class="vx-box-accent" x="170" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="179" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="188" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="197" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="206" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="215" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="224" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="233" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="242" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="251" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="260" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="269" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="278" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="287" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="296" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="305" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="314" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="323" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="332" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="341" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="350" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="359" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="368" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="377" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="386" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="395" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="404" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="413" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="422" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="431" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="440" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="449" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="458" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="467" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="476" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="485" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="494" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="503" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="512" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="521" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="530" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="539" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="548" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="557" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="566" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="575" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="584" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="593" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="602" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="611" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="620" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="629" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="638" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="647" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="656" y="104" width="8" height="20"/>
<rect class="vx-box-accent" x="665" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="674" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="683" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="692" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="701" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="710" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="719" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="728" y="104" width="8" height="20"/>
<rect class="vx-box-strong" x="737" y="104" width="8" height="20"/>
<text class="vx-text" x="20" y="190">64 registers per thread: 4 blocks, 32 warps, 50 percent</text>
<text class="vx-text-muted" x="20" y="218">registers</text>
<rect class="vx-box-accent" x="170" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="179" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="188" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="197" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="206" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="215" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="224" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="233" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="242" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="251" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="260" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="269" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="278" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="287" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="296" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="305" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="314" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="323" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="332" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="341" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="350" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="359" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="368" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="377" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="386" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="395" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="404" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="413" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="422" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="431" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="440" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="449" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="458" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="467" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="476" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="485" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="494" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="503" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="512" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="521" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="530" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="539" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="548" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="557" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="566" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="575" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="584" y="204" width="8" height="20"/>
<rect class="vx-box-accent" x="593" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="602" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="611" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="620" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="629" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="638" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="647" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="656" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="665" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="674" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="683" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="692" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="701" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="710" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="719" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="728" y="204" width="8" height="20"/>
<rect class="vx-box-strong" x="737" y="204" width="8" height="20"/>
<text class="vx-text-muted" x="20" y="248">warp slots</text>
<rect class="vx-box-accent" x="170" y="234" width="8" height="20"/>
<rect class="vx-box-accent" x="179" y="234" width="8" height="20"/>
<rect class="vx-box-accent" x="188" y="234" width="8" height="20"/>
<rect class="vx-box-accent" x="197" y="234" width="8" height="20"/>
<rect class="vx-box-accent" x="206" y="234" width="8" height="20"/>
<rect class="vx-box-accent" x="215" y="234" width="8" height="20"/>
<rect class="vx-box-accent" x="224" y="234" width="8" height="20"/>
<rect class="vx-box-accent" x="233" y="234" width="8" height="20"/>
<rect class="vx-box-strong" x="242" y="234" width="8" height="20"/>
<rect class="vx-box-strong" x="251" y="234" width="8" height="20"/>
<rect class="vx-box-strong" x="260" y="234" width="8" height="20"/>
<rect class="vx-box-strong" x="269" y="234" width="8" height="20"/>
<rect class="vx-box-strong" x="278" y="234" width="8" height="20"/>
<rect class="vx-box-strong" x="287" y="234" width="8" height="20"/>
<rect class="vx-box-strong" x="296" y="234" width="8" height="20"/>
<rect class="vx-box-strong" x="305" y="234" width="8" height="20"/>
<rect class="vx-box-accent" x="314" y="234" width="8" height="20"/>
<rect class="vx-box-accent" x="323" y="234" width="8" height="20"/>
<rect class="vx-box-accent" x="332" y="234" width="8" height="20"/>
<rect class="vx-box-accent" x="341" y="234" width="8" height="20"/>
<rect class="vx-box-accent" x="350" y="234" width="8" height="20"/>
<rect class="vx-box-accent" x="359" y="234" width="8" height="20"/>
<rect class="vx-box-accent" x="368" y="234" width="8" height="20"/>
<rect class="vx-box-accent" x="377" y="234" width="8" height="20"/>
<rect class="vx-box-strong" x="386" y="234" width="8" height="20"/>
<rect class="vx-box-strong" x="395" y="234" width="8" height="20"/>
<rect class="vx-box-strong" x="404" y="234" width="8" height="20"/>
<rect class="vx-box-strong" x="413" y="234" width="8" height="20"/>
<rect class="vx-box-strong" x="422" y="234" width="8" height="20"/>
<rect class="vx-box-strong" x="431" y="234" width="8" height="20"/>
<rect class="vx-box-strong" x="440" y="234" width="8" height="20"/>
<rect class="vx-box-strong" x="449" y="234" width="8" height="20"/>
<rect class="vx-box" x="458" y="234" width="8" height="20"/>
<rect class="vx-box" x="467" y="234" width="8" height="20"/>
<rect class="vx-box" x="476" y="234" width="8" height="20"/>
<rect class="vx-box" x="485" y="234" width="8" height="20"/>
<rect class="vx-box" x="494" y="234" width="8" height="20"/>
<rect class="vx-box" x="503" y="234" width="8" height="20"/>
<rect class="vx-box" x="512" y="234" width="8" height="20"/>
<rect class="vx-box" x="521" y="234" width="8" height="20"/>
<rect class="vx-box" x="530" y="234" width="8" height="20"/>
<rect class="vx-box" x="539" y="234" width="8" height="20"/>
<rect class="vx-box" x="548" y="234" width="8" height="20"/>
<rect class="vx-box" x="557" y="234" width="8" height="20"/>
<rect class="vx-box" x="566" y="234" width="8" height="20"/>
<rect class="vx-box" x="575" y="234" width="8" height="20"/>
<rect class="vx-box" x="584" y="234" width="8" height="20"/>
<rect class="vx-box" x="593" y="234" width="8" height="20"/>
<rect class="vx-box" x="602" y="234" width="8" height="20"/>
<rect class="vx-box" x="611" y="234" width="8" height="20"/>
<rect class="vx-box" x="620" y="234" width="8" height="20"/>
<rect class="vx-box" x="629" y="234" width="8" height="20"/>
<rect class="vx-box" x="638" y="234" width="8" height="20"/>
<rect class="vx-box" x="647" y="234" width="8" height="20"/>
<rect class="vx-box" x="656" y="234" width="8" height="20"/>
<rect class="vx-box" x="665" y="234" width="8" height="20"/>
<rect class="vx-box" x="674" y="234" width="8" height="20"/>
<rect class="vx-box" x="683" y="234" width="8" height="20"/>
<rect class="vx-box" x="692" y="234" width="8" height="20"/>
<rect class="vx-box" x="701" y="234" width="8" height="20"/>
<rect class="vx-box" x="710" y="234" width="8" height="20"/>
<rect class="vx-box" x="719" y="234" width="8" height="20"/>
<rect class="vx-box" x="728" y="234" width="8" height="20"/>
<rect class="vx-box" x="737" y="234" width="8" height="20"/>
<text class="vx-text-accent" x="20" y="300">the register file runs out first; the empty warp slots cannot be used</text>
</svg>
<figcaption>Figure 2. Two kernels with 256-thread blocks on one Hopper SM. Each shaded run is one block's share of the register file (top bar of each pair) and of the warp slots (bottom bar). Doubling the registers per thread doubles each block's share of the register file, so the file holds half as many blocks and half the warp slots go unused.</figcaption>
</figure>

The block limit matters for small blocks. The CUDA Programming Guide's own example uses an SM of compute capability 10.0 with 32 block slots and 2,048 thread slots: blocks of 32 threads stop at 32 blocks, 1,024 threads, and so 50 percent occupancy, although neither registers nor threads ran out.[^pg-occ]

This counting is the rule, but it is not the whole rule. The Best Practices Guide gives a case that plain division gets wrong. On compute capability 7.0, with 65,536 registers and 64 warp slots per SM, a kernel using 37 registers per thread reaches 75 percent occupancy with 128-thread blocks (12 blocks), but only 63 percent with 320-thread blocks, because only four such blocks fit.[^bp-calc] Plain division says 13 and 5 blocks. The guide names one missing rule: registers are allocated to each warp in units of 256.[^bp-calc] The profiler guide supplies the other: each sub-partition has its own registers, allocated in fixed-size chunks, and each warp lives on one sub-partition.[^ncu-sm]

The second example applies the rules one at a time. Rounding 37 × 32 = 1,184 registers up to 1,280 per warp gives the guide's 12 blocks for the small blocks, but still 5 for the large ones. Splitting the file into four quarters of 16,384 registers, each holding whole warps, gives 12 warps per quarter, 48 in all, and 48 ÷ 10 = 4 blocks of 320 threads. With both rules the model matches both of the guide's numbers. The same program then checks the Programming Guide's examples and prints the Hopper rows worked out above.

--8<-- "includes/examples/gpu/g5-occupancy/occupancy.cpp.md"

The Hopper register rows use multiples of 256 registers per warp, so the three models agree on them; the rounding only changes register counts that are not. The model still leaves things out. It ignores anything the system reserves per block and any rounding of shared-memory allocations, so its shared-memory rows are upper bounds. Treat any hand-built calculator the same way: check it against the vendor's own calculator, which the Best Practices Guide points to in Nsight Compute, and against the runtime's occupancy API before trusting it.[^bp-calc]

??? check "On the Hopper SM above, a kernel uses 256-thread blocks, 40 registers per thread and 48 KB of shared memory per block. Which budget limits it, and what is its occupancy?"

    Warp slots allow 8 blocks. Registers: 40 × 32 = 1,280 per warp, 10,240 per block, and 65,536 ÷ 10,240 = 6.4, so 6 blocks (the partitioned model gives 12 warps per quarter, 48 warps, also 6 blocks). Shared memory: 228 ÷ 48 = 4.75, so 4 blocks. Shared memory limits it to 4 blocks, 32 warps, 50 percent occupancy.

## How many warps are enough: Little's law

Occupancy says how many warps are resident. It does not say how many are needed. That question has a general answer from queueing theory, **Little's law**, which Vasily Volkov applied to GPUs in a 2010 talk: the parallelism needed to reach full throughput equals latency times throughput.[^volkov-little] **Throughput** here is how many operations the machine can complete per cycle once it is busy, a rate, where latency is a time; Volkov's talk warns that the two are often confused.[^volkov-throughput]

A pipe makes the law concrete. If an operation takes 24 cycles and 8 new ones can start every cycle, then at full speed the pipe holds 24 × 8 = 192 operations at once, each at a different stage. Fewer than 192 independent operations in flight, and some cycles start fewer than 8. Those are Volkov's numbers for one SM of NVIDIA's G80 and GT200 generations: an arithmetic latency of about 24 cycles and 8 cores per SM, so about 192 operations in flight per SM.[^volkov-little]

<figure class="vx-figure">
<svg viewBox="0 0 760 280" role="img" aria-label="A grid of 24 columns by 8 rows, 192 operations in flight, for a latency of 24 cycles and a throughput of 8 operations per cycle." aria-describedby="g5-f3-desc">
<title id="g5-f3-title">Little's law: how many operations must be in flight</title>
<desc id="g5-f3-desc">A grid of 24 columns by 8 rows, 192 cells, each cell one operation between issue and completion. Each column is one cycle's worth of operations: the rightmost column has just started and the leftmost is about to finish after 24 cycles. Every cycle 8 operations enter on the right and 8 leave on the left, so the grid always holds 24 times 8, 192 operations.</desc>
<text class="vx-text" x="20" y="24">One SM, one kind of instruction: latency 24 cycles, throughput 8 per cycle</text>
<line class="vx-line" x1="141" y1="54" x2="619" y2="54"/>
<line class="vx-line" x1="141" y1="48" x2="141" y2="60"/>
<line class="vx-line" x1="619" y1="48" x2="619" y2="60"/>
<text class="vx-text-muted" x="380" y="44" text-anchor="middle">24 cycles: one operation's latency</text>
<rect class="vx-cell-on" x="141" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="161" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="181" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="201" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="221" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="241" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="261" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="281" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="301" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="321" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="341" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="361" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="381" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="401" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="421" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="441" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="461" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="481" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="501" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="521" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="541" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="561" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="581" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="601" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="141" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="161" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="181" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="201" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="221" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="241" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="261" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="281" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="301" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="321" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="341" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="361" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="381" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="401" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="421" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="441" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="461" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="481" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="501" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="521" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="541" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="561" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="581" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="601" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="141" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="161" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="181" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="201" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="221" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="241" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="261" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="281" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="301" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="321" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="341" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="361" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="381" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="401" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="421" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="441" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="461" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="481" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="501" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="521" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="541" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="561" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="581" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="601" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="141" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="161" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="181" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="201" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="221" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="241" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="261" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="281" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="301" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="321" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="341" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="361" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="381" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="401" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="421" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="441" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="461" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="481" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="501" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="521" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="541" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="561" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="581" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="601" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="141" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="161" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="181" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="201" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="221" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="241" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="261" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="281" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="301" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="321" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="341" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="361" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="381" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="401" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="421" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="441" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="461" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="481" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="501" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="521" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="541" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="561" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="581" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="601" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="141" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="161" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="181" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="201" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="221" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="241" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="261" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="281" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="301" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="321" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="341" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="361" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="381" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="401" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="421" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="441" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="461" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="481" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="501" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="521" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="541" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="561" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="581" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="601" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="141" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="161" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="181" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="201" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="221" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="241" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="261" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="281" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="301" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="321" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="341" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="361" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="381" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="401" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="421" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="441" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="461" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="481" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="501" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="521" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="541" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="561" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="581" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="601" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="141" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="161" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="181" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="201" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="221" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="241" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="261" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="281" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="301" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="321" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="341" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="361" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="381" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="401" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="421" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="441" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="461" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="481" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="501" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="521" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="541" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="561" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="581" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="601" y="200" width="18" height="16"/>
<line class="vx-line" x1="137" y1="145" x2="70" y2="145"/>
<polygon class="vx-arrowhead" points="54,145 70,138 70,152"/>
<text class="vx-text-muted" x="20" y="130">8 finish</text>
<line class="vx-line" x1="706" y1="145" x2="639" y2="145"/>
<polygon class="vx-arrowhead" points="623,145 639,138 639,152"/>
<text class="vx-text-muted" x="660" y="130">8 start</text>
<text class="vx-text-accent" x="380" y="250" text-anchor="middle">in flight = latency × throughput = 24 × 8 = 192 operations</text>
</svg>
<figcaption>Figure 3. Little's law drawn as a pipe. Each cell is one operation between issue and completion. To start 8 operations every cycle when each takes 24 cycles, the pipe must hold 192. The numbers are Volkov's for one G80 or GT200 SM.</figcaption>
</figure>

The third example computes the same product for the three GPU generations in Volkov's table. The needed parallelism grows with each: about 576 operations per SM on GF100 and 864 on GF104, because the number of cores per SM grew faster than the latency fell.[^volkov-little]

--8<-- "includes/examples/gpu/g5-occupancy/little_law.cpp.md"

Volkov's operations are thread-operations, one per lane. Divided by 32, the 192 of G80 is 6 warps, each with one independent instruction ready. That is also what the toy scheduler showed: its latency was 8 cycles and its throughput one warp instruction per cycle, so it needed 8 × 1 = 8 independent warp instructions in flight, and it reached 100 percent exactly when warps times chains reached 8.

??? check "A machine has a latency of 18 cycles and completes 32 thread-operations per cycle per SM. How many warps with one chain each does it need, and how many with three chains each?"

    Little's law gives 18 × 32 = 576 thread-operations, which is 18 warp instructions. With one chain per warp that is 18 warps; with three independent chains per warp, 6 warps.

## The other source: instruction-level parallelism

Little's law counts operations in flight, not warps. Volkov names the two ways to supply them. **Thread-level parallelism (TLP)** is more threads, each with one operation ready. **Instruction-level parallelism (ILP)** is more independent operations inside each thread.[^volkov-tlp-ilp] On G80, he notes, the 192 operations can come from 192 threads, 25 percent occupancy, or from 64 threads with 3 independent operations each, 8 percent.[^volkov-tlp-ilp]

He then measured it. On a GTX480, a GF100 part, he ran a loop of dependent multiply-adds in one thread block on one SM and varied the block size. With one chain per thread, the SM needed 576 threads to reach its peak, as Little's law predicts. With two independent chains per thread it needed 320 threads, with three 256, and with four 192; the gain stopped at four.[^volkov-ilp] The example prints those measurements beside the prediction of 576 divided by the number of chains.

The prediction is exact for one chain and optimistic after that: four chains should need 144 threads and needed 192. The talk reports the numbers without explaining the gap. The lesson is the direction, not the formula: more ILP means fewer threads, and a model's prediction has to be checked against the machine. Volkov also shows the same data the other way round: at a fixed 12.5 percent occupancy on that GPU, raising ILP alone raises the fraction of peak the SM reaches.[^volkov-ilp]

Some hardware needs ILP to reach its peak at all. A GF104 SM has 48 cores, and one instruction occupies 16 of them, so peak needs three instructions issued per cycle. The SM has only two warp schedulers, and the third instruction must come from a second independent instruction of a warp already issuing. Volkov's conclusion is that on GF104 more than 66 percent of peak requires ILP, whatever the occupancy.[^volkov-gf104]

Memory latency hides the same way. Volkov estimates that his GTX480 needed up to about 100 KB of loads in flight to reach its memory bandwidth: 25,000 threads fetching 4 bytes each, or 1,000 threads fetching 100 bytes each.[^volkov-memory] A copy kernel in which each thread loaded eight 16-byte values reached 87 percent of the pin bandwidth at 8 percent occupancy, and one loading fourteen reached 84 percent of peak at 4 percent occupancy.[^volkov-memory] A thread does not stall when it issues a load, only when an instruction needs the loaded value, so independent loads overlap.[^volkov-memory]

??? check "Volkov's copy kernel reached 84 percent of peak memory bandwidth at 4 percent occupancy. Which term of Little's law did the kernel change to get there with so few warps?"

    Neither term: latency and bandwidth are the machine's. It changed where the bytes in flight came from. Each thread issued fourteen independent 16-byte loads before needing any of them, so a few warps kept as many bytes in flight as a thousand single-load threads would.

## When lower occupancy wins

If ILP and TLP were interchangeable, occupancy would be a matter of taste. They are not, because the resource that ILP needs, registers, is also the one that sets occupancy. Every extra independent chain is another live value, and a GPU thread keeps its live values in registers. More registers per thread means fewer resident warps, as Figure 2 showed.

Volkov argues that registers are worth the trade because they are the only memory fast enough for peak arithmetic. On the GTX480, a multiply-add reads 12 bytes and writes 4; at the chip's 1.3 Tflop/s that is about 8.1 TB/s of operands, while shared memory delivers about 1.3 TB/s.[^volkov-registers] He counts shared-memory bandwidth as six times lower than register bandwidth on Fermi, against the then-current Programming Guide's claim that the two are equally fast when there are no bank conflicts.[^volkov-registers] So a kernel near peak must take most operands from registers, which may require many registers per thread, which means low occupancy.

His matrix multiplication case study makes the argument with numbers. He starts from the CUDA SDK's shared-memory matrix product on a GTX480 at 1024 × 1024, then gives each thread 2, 4 and 8 outputs instead of 1, shrinking the block to keep the work per block the same.[^volkov-matmul] Each output is an independent accumulator, so each step adds one chain of ILP, and each value loaded from shared memory is reused for several outputs.

| Outputs per thread | Registers per thread | Blocks per SM | Occupancy | Gflop/s |
| --- | --- | --- | --- | --- |
| 1 | 21 | 1 | 67% | 242 |
| 2 | 28 | 2 | 67% | 341 |
| 4 | 41 | 3 | 50% | 427 |
| 8 | 63 | 4 | 33% | 485 |
| 36 (MAGMA BLAS) | not given | 2 | 33% | 838 |

The table is Volkov's, from slides 52 to 65 of his talk.[^volkov-matmul] Performance doubled while occupancy halved. At eight outputs per thread the kernel moved 2.25 bytes of shared memory per flop instead of 4, and the MAGMA library's kernel, with 36 outputs per thread, moved 0.67.[^volkov-matmul] The first step shows a second effect: with 512-thread blocks, two blocks fit on the SM instead of one, so one block's global loads could overlap the other's arithmetic, which Volkov gives as one reason for its speedup at unchanged occupancy.[^volkov-matmul]

Volkov opens the talk with the same pattern in NVIDIA's own libraries on a G80. CUBLAS 2.0 used 64-thread blocks where 1.1 used 512, at 33 percent occupancy instead of 67, and ran at 204 Gflop/s instead of 128. CUFFT 2.3 used 64 threads instead of 256, at 17 percent occupancy instead of 33, and ran at 93 Gflop/s instead of 45.[^volkov-table] His summary: "Maximizing occupancy, you may lose performance".[^volkov-table]

??? check "In the table, going from 4 to 8 outputs per thread lowered occupancy from 50 to 33 percent and raised performance. Would lowering the one-output kernel to 33 percent occupancy without changing its work per thread, for example by declaring shared memory it never uses, do the same?"

    No. Occupancy would fall in the same way, but nothing would replace the lost warps: each thread would still have one chain and would still read every operand from shared memory. Lower occupancy helped because the registers it freed were spent on independent outputs that supply ILP and reuse loaded values. Occupancy was the price, not the cause.

## What the vendor guides say

NVIDIA's guides still lead with the occupancy view. The Programming Guide calls it good practice to keep occupancy as high as possible, because that hides latency.[^pg-occ] The Best Practices Guide says that low occupancy "always interferes with the ability to hide memory latency",[^bp-occ] a sentence Volkov quoted as a fallacy in 2010, beside his copy kernel at 4 percent occupancy.[^volkov-memory] The Nsight Compute guide repeats the claim for latencies in general.[^ncu-occ]

The same Best Practices Guide qualifies it two sections later. Raising occupancy from 66 to 100 percent generally does not give a matching speedup; a lower-occupancy kernel has more registers per thread and may spill less; and with enough exposed ILP a low occupancy can, in some cases, hide latency fully.[^bp-heur] It also gives the knobs that trade the two: `-maxrregcount` or `__launch_bounds__` cap the registers per thread to admit more warps.[^bp-calc] The Programming Guide adds the cost: a kernel that needs more registers than the cap spills to local memory, which changes its performance, sometimes for the better through higher occupancy and sometimes not.[^pg-occ]

Read the two positions together and the useful rule appears. Occupancy is one input to latency hiding. What must be high is the number of independent operations in flight, and a kernel can supply it with warps, with chains inside each warp, or with both. A compiler that maximizes occupancy blindly will pick small register budgets and tiles that give up the ILP and reuse that made Volkov's kernels fast.

## Apple GPUs

Apple talks about occupancy in threads rather than in a percentage of warp slots. A WWDC22 session on scaling compute work across Apple GPUs gives a rule of thumb of 1K to 2K concurrent threads per GPU core for relatively complex kernels, and introduces a maximum theoretical occupancy figure among Xcode 14's compiler statistics.[^ap9] The session does not give the per-core limits behind that figure.

The kinds of budget are the same: a SIMD-group of 32 threads on the owner's M4 Pro, per-thread registers, and threadgroup memory, of which Apple's session gives 32K per threadgroup.[^ap9] A Vortex target description for Apple GPUs therefore records the same fields as for NVIDIA, with each limit Apple does not publish marked unknown until it is measured, the convention [G4](g4-memory-performance.md#for-vortex) set.

## What does not change: the bits

Everything so far changes how many threads run and how work is split among them; none of it changes an arithmetic operation. Giving each thread two outputs instead of one gives it two independent sums, each still adding its products in increasing `k` from `0.0`, as [decision 56](../decisions/numbers.md#d56) requires. Each output belongs to one thread, and [decision 25](../decisions/references.md#d25) rules out the `&mut` output overlapping an input, so no reassignment of outputs to threads can make two threads race on one element.

One source of ILP is forbidden. A CPU compiler allowed to reassociate can split one sum into four partial sums, one per accumulator, and add them at the end ([P11](../optimize/p11-floating-point.md#the-rule-every-reordering-pass-needs)). That creates four chains from one, but it changes the order of the additions and so can change the result. A Vortex compiler may not do it. For the stage 10 kernel, its ILP must come from computing several outputs per thread, as in Volkov's case study, never from splitting one output's sum:

```vortex
// fragment
// two outputs per thread: two independent chains, each in source order
sum0 += a[row, k] * b[k, column];
sum1 += a[row, k] * b[k, column + 1];
```

## Measuring it

No GPU timings are claimed here. Reproduce the shape of Volkov's ILP curve on your own GPU:

1. Write a kernel that runs a long loop of multiply-adds on `ilp` independent accumulators, for `ilp` from 1 to 4, and stores every accumulator at the end so none is removed. On the owner's M4 Pro, a program that compiles Metal Shading Language source at run time works without the offline Metal toolchain (research notes, 2026-09-23).
2. Launch one threadgroup at a time, so the work runs on one GPU core, and vary its size in steps of 32.
3. Time many runs of each configuration and report the median with its spread, following [P1](../optimize/p1-measure-first.md). Convert to operations per second and to a fraction of the best one you see.
4. For each `ilp`, record the smallest threadgroup size that reaches that best rate. Compare the trend with Volkov's: fewer threads as `ilp` grows.
5. On NVIDIA, Nsight Compute reports theoretical and achieved occupancy side by side; its guide says a large gap between them usually means an unbalanced workload.[^ncu-occ] [G14](g14-measuring-gpu-code.md) covers the tools, including Xcode's for Metal.

| ilp | Smallest threads per core at best rate | Best rate (Gop/s) | Theoretical occupancy | Achieved occupancy |
| --- | --- | --- | --- | --- |
| 1 | | | | |
| 2 | | | | |
| 3 | | | | |
| 4 | | | | |

Record the machine, the operating system or driver version, the compiler and the date with the table.

## For Vortex

!!! vortex "Exercise"

    **Build** an occupancy and parallelism report for a GPU kernel shape in your compiler: given a launch shape, the kernel's resource use and a target description, state its occupancy, the budget that limits it, and whether the operations it keeps in flight reach what Little's law asks.

    1. Extend the target description from [G4's exercise](g4-memory-performance.md#for-vortex) with the per-SM limits this chapter used: warp slots, block slots, registers, shared memory, the register allocation unit per warp and the number of sub-partitions. Fill it for NVIDIA compute capabilities 7.0 and 9.0 from the sources cited here, and mark every Apple entry you have not measured as unknown.
    2. An occupancy function that takes threads per block, registers per thread and shared memory per block and reports resident blocks, resident warps, occupancy, and the name of the limiting budget, applying the allocation rules one at a time as the second example does.
    3. For the innermost loop of a kernel in your IR, the number of independent loop-carried chains ([P5](../optimize/p5-microarchitecture.md#latency-throughput-and-the-number-of-chains)), from the dependences your compiler already computes ([P6](../optimize/p6-dependence-analysis.md)).
    4. Given a latency and a throughput for one instruction class (numbers the user supplies or measures; leave them unset rather than inventing them), the parallelism Little's law asks for, and whether resident warps times chains reaches it.
    5. A remark for every report, as the [sixth principle](../philosophy.md#6-explain-performance-decisions) asks, such as "4 blocks, limited by registers; 32 warps × 2 chains = 64 of 18 needed warp instructions".

    **Not yet:** estimating a real kernel's register count from your IR (that needs register allocation for a GPU target, [C3](../backend/c3-linear-scan.md) onward); rewriting a kernel to compute several outputs per thread ([G10](g10-matmul-ladder.md)); generating GPU code at all ([M12](../mlir/m12-vortex-gpu-path.md) weighs the paths); choosing parameters by search ([P15](../optimize/p15-choosing-parameters.md)).

    **Proof that it works:**

    - Golden tests for the Best Practices Guide's example at compute capability 7.0: 37 registers per thread gives 12 blocks and 75 percent with 128-thread blocks, and 4 blocks and 62.5 percent (the guide rounds to 63) with 320-thread blocks, with registers named as the limit in both.
    - Golden tests for the Programming Guide's compute capability 10.0 examples: 768-thread blocks give 2 blocks and 75 percent; 32-thread blocks give 32 blocks and 50 percent, limited by block slots; 100 KB of shared memory per block allows 2 blocks.
    - Golden tests for this chapter's Hopper rows, including the check question's answer: 4 blocks, limited by shared memory.
    - The stage 10 kernel with one output per thread reports one chain; a version written by hand with two independent sums reports two; and no report ever proposes splitting one sum into partial sums.
    - A differential test: for a few hundred random budgets and kernel shapes, the occupancy function matches an independent brute-force count that places blocks on a simulated SM one at a time until one budget refuses.

## Key ideas

!!! recap "Questions you can now answer"

    - **How does a GPU hide latency?** Each cycle, each warp scheduler issues from a warp whose next instruction has its inputs ready; a cycle with no such warp is a skipped issue slot.
    - **What is occupancy?** The number of warps resident on an SM divided by the most it can hold, set by the tightest of its warp and block slots, registers and shared memory.
    - **Why do allocation rules matter?** Registers go to each warp in fixed units and to one sub-partition, so plain division can overestimate how many blocks fit.
    - **What does Little's law say?** The operations in flight needed for full throughput equal latency times throughput.
    - **Where can those operations come from?** From more warps (TLP) or from more independent chains per thread (ILP), in any mix.
    - **Why can lower occupancy be faster?** Registers spent on several outputs per thread supply ILP and reuse operands from the fastest memory, which can outweigh the warps given up.
    - **Which source of ILP may a Vortex compiler not use?** Splitting one floating-point sum into partial sums, because it reorders the additions.

## Where this comes back

!!! next "You will use this again in"

    - [G6. Synchronization, atomics and reductions](g6-synchronization.md): *resident blocks*, *warp scheduler*
    - [G10. The GPU matmul ladder](g10-matmul-ladder.md): *register tiling*, *occupancy*, *instruction-level parallelism*
    - [G12. Fusion case study: FlashAttention](g12-flashattention.md): *register budget*, *occupancy*
    - [G14. Measuring GPU code](g14-measuring-gpu-code.md): *theoretical occupancy*, *achieved occupancy*, *issue slots*
    - [C5. Spilling, splitting and rematerialization](../backend/c5-spilling.md): *register cap*, *spilling*
    - [P15. Choosing parameters: models or search](../optimize/p15-choosing-parameters.md): *occupancy model*, *measured optimum*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *target description*, *occupancy*

## Sources and further reading

Read Volkov's talk first: its 75 slides make this chapter's argument with measurements, and its matrix-multiply case study is a preview of [G10](g10-matmul-ladder.md). Then read the Best Practices Guide's occupancy sections for the mechanics, and the Nsight Compute guide's hardware model for what the profiler counts.

[^ncu-sm]: NVIDIA, "Nsight Compute Profiling Guide", "Hardware Model", "Streaming Multiprocessor": four SM sub partitions, each with a warp scheduler and register file; a warp stays on one sub partition; registers are allocated in fixed-size chunks. <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#streaming-multiprocessor>
[^ncu-sched]: NVIDIA, "Nsight Compute Profiling Guide", "Sections and Rules", "Scheduler Statistics": active, eligible and issued warps, and skipped issue slots as a sign of poor latency hiding. <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#sections-and-rules>
[^ncu-occ]: NVIDIA, "Nsight Compute Profiling Guide", "Sections and Rules", "Occupancy": definition, and theoretical versus achieved occupancy. <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#sections-and-rules>
[^bp-occ]: NVIDIA, "CUDA C++ Best Practices Guide", v13.4, section 11.1, "Occupancy". <https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#occupancy>
[^bp-calc]: NVIDIA, "CUDA C++ Best Practices Guide", v13.4, section 11.1.1, "Calculating Occupancy": the 37-register example on compute capability 7.0, allocation in units of 256 registers per warp, `-maxrregcount`, `__launch_bounds__` and the occupancy calculator. <https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#calculating-occupancy>
[^bp-heur]: NVIDIA, "CUDA C++ Best Practices Guide", v13.4, section 11.3, "Thread and Block Heuristics". <https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#thread-and-block-heuristics>
[^pg-occ]: NVIDIA, "CUDA Programming Guide", v13.4, section 2.3.7, "Kernel Launch and Occupancy", with Table 2 for compute capability 10.0. <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#kernel-launch-and-occupancy>
[^hopper-occ]: NVIDIA, "Hopper Tuning Guide", v13.4, section 1.4.1.1, "Occupancy": 64 warps, 64K 32-bit registers, 255 registers per thread, 32 blocks and 228 KB of shared memory per SM on compute capability 9.0. <https://docs.nvidia.com/cuda/hopper-tuning-guide/index.html#occupancy>
[^volkov-table]: Vasily Volkov, "Better Performance at Lower Occupancy", GPU Technology Conference, 22 September 2010, slides 2 and 3: the CUBLAS and CUFFT comparison on G80. <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-throughput]: Volkov, "Better Performance at Lower Occupancy", GTC 2010, slides 7 and 8: latency and throughput defined, and the claim that the two are often confused. <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-little]: Volkov, "Better Performance at Lower Occupancy", GTC 2010, slides 10 and 11: Little's law and the latency, throughput and parallelism of G80-GT200, GF100 and GF104. <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-tlp-ilp]: Volkov, "Better Performance at Lower Occupancy", GTC 2010, slides 12 to 14: thread-level and instruction-level parallelism, and the G80 figures of 25 and 8 percent occupancy. <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-gf104]: Volkov, "Better Performance at Lower Occupancy", GTC 2010, slide 14: GF104's 48 cores per SM and two warp schedulers. <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-ilp]: Volkov, "Better Performance at Lower Occupancy", GTC 2010, slides 15 to 22: the GTX480 experiment with 1 to 4 independent multiply-adds per thread. <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-memory]: Volkov, "Better Performance at Lower Occupancy", GTC 2010, slides 28 to 41: hiding memory latency, the copy kernels on GTX480, and the Best Practices Guide quotation. <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-registers]: Volkov, "Better Performance at Lower Occupancy", GTC 2010, slides 44 to 48: register and shared-memory bandwidth on GTX480 and Fermi. <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-matmul]: Volkov, "Better Performance at Lower Occupancy", GTC 2010, slides 52 to 66: the matrix multiplication case study on GTX480 and the MAGMA BLAS comparison. <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^ap9]: Apple, "Scale compute workloads across Apple GPUs", WWDC22 session 10159: threads per GPU core as occupancy, Xcode 14's theoretical occupancy, and threadgroup limits. <https://developer.apple.com/videos/play/wwdc2022/10159/>
