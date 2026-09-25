# P5. The microarchitecture shelf

<p class="page-intro">The same machine code runs at different speeds on different chips. This chapter explains why, in terms of pipelines, out-of-order execution, execution ports and dependency chains; shows how to bound a loop's speed by hand from a table of latencies; and teaches you to find those tables, and to distrust them until a measurement agrees. For Vortex it explains why the stage 10 kernel's inner loop waits on one addition per trip, and why the compiler may not fix that by regrouping the sum.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 45 minutes · Builds on: [P2. The memory hierarchy](p2-memory-hierarchy.md), [P4. Seeing inside the CPU: counters and tools](p4-counters-and-tools.md)</p>

???+ remember "Before you start, remember"

    ??? question "What happens to a load's latency when the value it reads has to come from main memory instead of the cache closest to the core?"

        It grows by a large factor. Each level of the memory hierarchy is further from the core and slower to reach, which is why a loop's access pattern, not only its instruction count, decides how fast it runs.

        Introduced in [P2. The memory hierarchy](p2-memory-hierarchy.md#the-hierarchy-several-sizes-several-speeds).

    ??? question "What does a peak-flops microbenchmark need, besides a loop full of multiply-adds, to reach the compute roof?"

        Enough independent accumulators to hide the latency of the operation it repeats. With one accumulator, every operation waits for the one before it, and the benchmark measures latency instead of peak throughput.

        Introduced in [P3. The roofline model](p3-roofline.md#measuring-your-own-machine).

    ??? question "What must a performance measurement report besides one number?"

        A median over repeated runs with a confidence interval, and the machine and date it was taken on. One run of one timing is not a measurement.

        Introduced in [P1. Measure first](p1-measure-first.md#the-reporting-rules).

    ??? question "What do a cycle counter and an instruction counter tell you together that a stopwatch cannot?"

        Instructions per cycle: whether a change made the program run fewer instructions, or run the same instructions with fewer idle cycles between them. That turns "it got faster" into a reason.

        Introduced in [P4. Seeing inside the CPU: counters and tools](p4-counters-and-tools.md).

    ??? question "May a Vortex compiler rewrite `(a + b) + c` as `a + (b + c)` when the values are `f32`?"

        No. The two groupings round different intermediate sums, so they can give different bits, and decision 56 forbids reassociating floating-point operations.

        Introduced in [O6. Redundancy: CSE, GVN, PRE and LICM](o6-redundancy.md#what-goes-into-the-key).

!!! goals "In this chapter"

    - Explain why one instruction set can run the same code at different speeds on different cores, and name the parts of a core that decide the speed: pipeline stages, issue width, execution ports, register renaming and the reorder buffer.
    - Distinguish an instruction's latency from its throughput, and compute how many independent dependency chains it takes to keep a core's ports busy.
    - Bound a loop's cycles per trip by hand from three limits, the loop-carried chain, the ports and the issue width, and check the bound against llvm-mca.
    - Find the latency and throughput of an instruction for the core you have, and say which manual, model or measurement each number came from.
    - Explain why the stage 10 kernel's inner loop is bound by the latency of one `f32` addition, and which transformations may remove that bound without changing a bit of the result.

## One loop, three models, three answers

Here is the innermost loop of the [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for) kernel, `sum += a[row, k] * b[k, column]`, written by hand in AArch64 assembly. The multiplication and the addition are separate instructions, each rounded, because [decision 56](../decisions/numbers.md#d56) forbids fusing them. A second loop beside it computes four dot products at once, one for each of four columns:

--8<-- "includes/examples/optimize/p5-microarchitecture/dependency_chain.s.md"

Now ask LLVM's tool `llvm-mca` how many cycles each trip around each loop takes. It answers from a **scheduling model**, a table that LLVM keeps for each processor it knows: how long each instruction takes, and which parts of the core it occupies.[^llvm-mca] Three of LLVM's AArch64 models give three different answers for the same bytes:

| Model (`-mcpu`) | `one_chain`, cycles per trip | `four_columns`, cycles per trip |
| --- | --- | --- |
| `apple-m1` | 4.0 | 4.0 |
| `neoverse-n1` | 2.0 | 4.0 |
| `neoverse-v2` | 2.0 | 2.0 |

(llvm-mca 18.1.8, 1,000 simulated trips, run on the owner's M4 Pro on 2026-09-24. These are predictions from models, not measurements of any chip.)

Two things stand out. The first loop runs at different speeds on different models, although every model agrees on what each instruction computes. And `four_columns`, which does four times the arithmetic of `one_chain`, takes no longer per trip on two of the models, and twice as long on the third. By the end of this chapter you will be able to derive every number in the table by hand.

## The ISA is a contract; the microarchitecture is a design

AArch64 is an **instruction set architecture** (ISA): a contract that says which instructions exist, what each one computes, and how registers and memory behave. Every core in the table implements it, and `fadd s0, s0, s1` means the same rounded sum on each. The contract says nothing about speed. [A1](../backend/a1-machine-model.md#one-line-two-different-machines) makes the same point from the back end's side: an architecture manual promises a meaning, not a timing.

How fast the instruction runs is decided by the core's **microarchitecture**: the internal design that carries out the contract, built from pipelines, buffers, predictors and execution units that the ISA never mentions. Two cores can implement one ISA with different microarchitectures, and one chip can hold more than one. The owner's Apple M4 Pro reports two performance levels through `sysctl`: `hw.perflevel0` is named Performance and has 8 cores, and `hw.perflevel1` is named Efficiency and has 4 (checked on 2026-09-24). The same loop can take a different number of cycles on each kind of core.

This is why performance work reads two kinds of document. An architecture manual tells you what `fadd` computes, which is enough to generate correct code. A chip vendor's **optimization guide** tells you how long `fadd` takes on one core and how many can start per cycle, which is what you need to generate fast code. Apple's guide for its own chips, for example, lists among its contents the CPU's structures and tables of instruction latency and bandwidth.[^apple-guide] The rest of this chapter builds the vocabulary those guides assume.

## Instructions do not run start to finish, one at a time

A processor could fetch one instruction, decode it, execute it, write its result, and only then start the next. A **pipeline** splits that work into stages, so that while one instruction executes, the next is being decoded and the one after that fetched. Each stage does its part every cycle and hands the instruction on, like a factory line with one worker per station instead of one worker who builds a whole product before starting the next.

The example below models a small pipeline with four stages: fetch (F), decode (D), execute (E) and writeback (W). One instruction enters fetch per cycle, and each stage holds one instruction at a time, in program order. If an instruction reads the register that the instruction immediately before it writes, its decode waits until that write has finished writeback, because this model has no way to pass a result along early:

--8<-- "includes/examples/optimize/p5-microarchitecture/pipeline_stages.cpp.md"

Four instructions, run one at a time, would cost four stages times four instructions, 16 cycles. Pipelined with no dependence between them, they cost 7: the last instruction enters fetch in cycle 3 and leaves writeback in cycle 6. The third instruction, `add t2, t1`, reads the result of `add t1`, which finishes writeback in cycle 4. So `add t2, t1` cannot decode until cycle 5 instead of cycle 3, and it waits in fetch.

The two empty cycles are a **bubble**: stages that hold no useful work because an instruction is waiting for a value. Everything behind the waiting instruction waits too, which is why `mul t3` is fetched two cycles late and the total rises to 9. Figure 1 draws the schedule.

<figure class="vx-figure">
<svg viewBox="0 0 660 210" role="img" aria-label="Four instructions in a four-stage pipeline; the third waits two cycles in fetch for a value, and the fourth is delayed behind it" aria-describedby="p5-f1-desc">
<desc id="p5-f1-desc">A grid of nine cycles, 0 to 8, by four instructions. mul t0 runs fetch, decode, execute and writeback in cycles 0 to 3. add t1 follows one cycle behind, in cycles 1 to 4. add t2, t1 is fetched in cycle 2 and stays in fetch in cycles 3 and 4, drawn dashed, because it needs the value add t1 writes back in cycle 4; its decode, execute and writeback run in cycles 5, 6 and 7. mul t3 cannot enter fetch until add t2, t1 leaves it, so it is fetched in cycle 5 and finishes writeback in cycle 8.</desc>
<text class="vx-text-muted" x="393" y="10" text-anchor="middle">cycle</text>
<text class="vx-text-muted" x="175" y="26" text-anchor="middle">0</text>
<text class="vx-text-muted" x="229" y="26" text-anchor="middle">1</text>
<text class="vx-text-muted" x="283" y="26" text-anchor="middle">2</text>
<text class="vx-text-muted" x="337" y="26" text-anchor="middle">3</text>
<text class="vx-text-muted" x="391" y="26" text-anchor="middle">4</text>
<text class="vx-text-muted" x="445" y="26" text-anchor="middle">5</text>
<text class="vx-text-muted" x="499" y="26" text-anchor="middle">6</text>
<text class="vx-text-muted" x="553" y="26" text-anchor="middle">7</text>
<text class="vx-text-muted" x="607" y="26" text-anchor="middle">8</text>
<text class="vx-mono" x="10" y="56">mul t0</text>
<rect class="vx-box" x="150" y="34" width="50" height="34" rx="3"/>
<text class="vx-text" x="175" y="56" text-anchor="middle">F</text>
<rect class="vx-box" x="204" y="34" width="50" height="34" rx="3"/>
<text class="vx-text" x="229" y="56" text-anchor="middle">D</text>
<rect class="vx-box" x="258" y="34" width="50" height="34" rx="3"/>
<text class="vx-text" x="283" y="56" text-anchor="middle">E</text>
<rect class="vx-box" x="312" y="34" width="50" height="34" rx="3"/>
<text class="vx-text" x="337" y="56" text-anchor="middle">W</text>
<text class="vx-mono" x="10" y="98">add t1</text>
<rect class="vx-box" x="204" y="76" width="50" height="34" rx="3"/>
<text class="vx-text" x="229" y="98" text-anchor="middle">F</text>
<rect class="vx-box" x="258" y="76" width="50" height="34" rx="3"/>
<text class="vx-text" x="283" y="98" text-anchor="middle">D</text>
<rect class="vx-box" x="312" y="76" width="50" height="34" rx="3"/>
<text class="vx-text" x="337" y="98" text-anchor="middle">E</text>
<rect class="vx-box-accent" x="366" y="76" width="50" height="34" rx="3"/>
<text class="vx-text" x="391" y="98" text-anchor="middle">W</text>
<text class="vx-mono" x="10" y="140">add t2, t1</text>
<rect class="vx-box" x="258" y="118" width="50" height="34" rx="3"/>
<text class="vx-text" x="283" y="140" text-anchor="middle">F</text>
<rect class="vx-box-bad" x="312" y="118" width="50" height="34" rx="3"/>
<text class="vx-text-muted" x="337" y="140" text-anchor="middle">F</text>
<rect class="vx-box-bad" x="366" y="118" width="50" height="34" rx="3"/>
<text class="vx-text-muted" x="391" y="140" text-anchor="middle">F</text>
<rect class="vx-box-accent" x="420" y="118" width="50" height="34" rx="3"/>
<text class="vx-text" x="445" y="140" text-anchor="middle">D</text>
<rect class="vx-box" x="474" y="118" width="50" height="34" rx="3"/>
<text class="vx-text" x="499" y="140" text-anchor="middle">E</text>
<rect class="vx-box" x="528" y="118" width="50" height="34" rx="3"/>
<text class="vx-text" x="553" y="140" text-anchor="middle">W</text>
<text class="vx-mono" x="10" y="182">mul t3</text>
<rect class="vx-box" x="420" y="160" width="50" height="34" rx="3"/>
<text class="vx-text" x="445" y="182" text-anchor="middle">F</text>
<rect class="vx-box" x="474" y="160" width="50" height="34" rx="3"/>
<text class="vx-text" x="499" y="182" text-anchor="middle">D</text>
<rect class="vx-box" x="528" y="160" width="50" height="34" rx="3"/>
<text class="vx-text" x="553" y="182" text-anchor="middle">E</text>
<rect class="vx-box" x="582" y="160" width="50" height="34" rx="3"/>
<text class="vx-text" x="607" y="182" text-anchor="middle">W</text>
</svg>
<figcaption>Figure 1. The schedule printed by the pipeline example. The dashed cells are the bubble: <code>add t2, t1</code> has been fetched but cannot decode until the writeback of <code>add t1</code> (outlined) has finished. Real pipelines shorten such waits by <strong>forwarding</strong>, passing a result from the end of execute straight to the instruction that needs it; this model leaves forwarding out so that the wait stays visible.</figcaption>
</figure>

??? check "Change the example so that `mul t3` reads the result of `add t2, t1` as well. How many cycles does the program take now, and why?"

    Eleven. `add t2, t1` finishes writeback in cycle 7, so `mul t3` cannot decode until cycle 8 instead of cycle 6, and it writes back in cycle 10. Each dependent pair adds its own wait, because in this model a value is usable only after writeback. A dependency chain turns a pipeline back into something close to one instruction at a time.

Vendor guides do not draw this grid. They give two numbers per instruction, its latency and its throughput, that summarize what the grid would show for that instruction on that core. Before those numbers make sense, one more piece of the machine is needed.

## Doing more than one instruction at once

A pipeline overlaps the stages of consecutive instructions, but still starts at most one per cycle. A **superscalar** core fetches, decodes and starts several instructions in the same cycle; its **issue width** is how many. Inside the core, the work is done by **execution units**, reached through **execution ports**: the lanes through which an instruction is sent to a unit that can run it. A core might have four integer ports, two load and store ports and three floating-point ports, and an instruction can start only when a port of its kind is free.

LLVM's model for the `apple-m1` row of the table has exactly that shape. It dispatches six micro-operations per cycle, and it has four integer pipes, two load and store pipes and three floating-point and vector pipes, of which two can multiply.[^cyclone] Many cores split a complex instruction into simpler **micro-operations** (µops), each of which is scheduled on its own.[^agner-asm] The model counts each of `one_chain`'s loads, which load a value and also advance their address register, as two.

### Out of order

An **out-of-order** core fetches and decodes in program order, but lets an instruction execute as soon as its operands are ready and a port is free, even if an older instruction is still waiting. Agner Fog's example is a load that misses the cache: while it waits, the core runs later instructions that do not depend on it.[^agner-asm] This is how a core overlaps several of the slow memory accesses [P2](p2-memory-hierarchy.md#the-hierarchy-several-sizes-several-speeds) measured, provided the program gives it independent loads to start.

Running instructions early is only safe if the program cannot tell. Two pieces of hardware make it so.

- **Register renaming** removes false dependences. `one_chain` writes `s1` on every trip, and the next trip's load writes `s1` again before the old value has necessarily been read. Renaming gives each write a fresh physical register, so the second write need not wait for the first value's last reader. Agner Fog shows two computations that share one register and run as fast as when they use two, because the processor renames it.[^agner-asm] What renaming cannot remove is a **true dependence**, where an instruction needs the value another one computes.
- The **reorder buffer** (ROB) holds every instruction that has started but not finished, in program order. An instruction **retires**, making its result part of the program's visible state, only when every older instruction has retired. llvm-mca simulates the same structure: its retire control unit retires instructions in order and frees their physical registers.[^llvm-mca]

The size of these buffers bounds how far ahead the core can look. LLVM's `apple-m1` model buffers 192 micro-operations, a number its comment says is based on the reorder buffer.[^cyclone]

### Guessing branches

A loop ends with a branch, and the core must fetch the next instructions long before it knows which way the branch goes. So it predicts, from the branch's past behavior, and executes the predicted path **speculatively**, keeping the results in the reorder buffer until the branch is resolved. A wrong guess, a **misprediction**, throws that work away and refetches. 

Agner Fog puts the cost at 12 to more than 50 cycles on the x86 processors he describes, depending on pipeline length;[^agner-asm] LLVM's `apple-m1` model charges 16, with a comment that 14 to 19 are typical.[^cyclone] A loop's back branch, taken on every trip but the last, and a Vortex bounds check, which almost never fails, are both predictable. They cost issue slots and ports, not waiting. Figure 2 puts the parts together.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="An out-of-order core: an in-order front end, an out-of-order middle with a scheduler and execution ports, and in-order retirement from the reorder buffer" aria-describedby="p5-f2-desc">
<desc id="p5-f2-desc">A block diagram read left to right. The in-order front end has three boxes: fetch with a branch predictor, decode into micro-operations, and rename to physical registers. The rename box feeds two structures: the reorder buffer, a long box along the bottom that holds every in-flight micro-operation in program order, and the scheduler, which holds micro-operations until their operands are ready. The scheduler sends micro-operations through three groups of ports: integer ports I0 to I3, load and store ports LS0 and LS1, and floating-point and vector ports V0 to V2. Results flow back to the reorder buffer, which retires micro-operations in program order at its right end. Moving dashes show micro-operations flowing from the front end into the scheduler, out through the ports and back into the reorder buffer. Labels mark the front end and retirement as in order and the scheduler and ports as out of order.</desc>
<defs><marker id="p5-f2-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text-muted" x="20" y="22">in order</text>
<rect class="vx-box" x="20" y="36" width="120" height="44" rx="4"/>
<text class="vx-text" x="80" y="55" text-anchor="middle">fetch</text>
<text class="vx-text-muted" x="80" y="72" text-anchor="middle">branch predictor</text>
<rect class="vx-box" x="20" y="100" width="120" height="44" rx="4"/>
<text class="vx-text" x="80" y="119" text-anchor="middle">decode</text>
<text class="vx-text-muted" x="80" y="136" text-anchor="middle">into micro-ops</text>
<rect class="vx-box" x="20" y="164" width="120" height="44" rx="4"/>
<text class="vx-text" x="80" y="183" text-anchor="middle">rename</text>
<text class="vx-text-muted" x="80" y="200" text-anchor="middle">physical registers</text>
<path class="vx-line" d="M80 80 L80 99" marker-end="url(#p5-f2-head)"/>
<path class="vx-line" d="M80 144 L80 163" marker-end="url(#p5-f2-head)"/>
<text class="vx-text-accent" x="200" y="22">out of order</text>
<rect class="vx-box-accent" x="200" y="36" width="170" height="172" rx="4"/>
<text class="vx-text" x="285" y="60" text-anchor="middle">scheduler</text>
<text class="vx-text-muted" x="285" y="80" text-anchor="middle">waits for operands,</text>
<text class="vx-text-muted" x="285" y="96" text-anchor="middle">then for a free port</text>
<path class="vx-flow" d="M140 186 L199 186" marker-end="url(#p5-f2-head)"/>
<text class="vx-text-muted" x="160" y="178">issue</text>
<rect class="vx-box" x="430" y="36" width="56" height="30" rx="3"/>
<text class="vx-mono" x="458" y="56" text-anchor="middle">I0</text>
<rect class="vx-box" x="490" y="36" width="56" height="30" rx="3"/>
<text class="vx-mono" x="518" y="56" text-anchor="middle">I1</text>
<rect class="vx-box" x="550" y="36" width="56" height="30" rx="3"/>
<text class="vx-mono" x="578" y="56" text-anchor="middle">I2</text>
<rect class="vx-box" x="610" y="36" width="56" height="30" rx="3"/>
<text class="vx-mono" x="638" y="56" text-anchor="middle">I3</text>
<text class="vx-text-muted" x="680" y="56">integer</text>
<rect class="vx-box" x="430" y="98" width="56" height="30" rx="3"/>
<text class="vx-mono" x="458" y="118" text-anchor="middle">LS0</text>
<rect class="vx-box" x="490" y="98" width="56" height="30" rx="3"/>
<text class="vx-mono" x="518" y="118" text-anchor="middle">LS1</text>
<text class="vx-text-muted" x="560" y="118">load, store</text>
<rect class="vx-box-strong" x="430" y="160" width="56" height="30" rx="3"/>
<text class="vx-mono" x="458" y="180" text-anchor="middle">V0</text>
<rect class="vx-box-strong" x="490" y="160" width="56" height="30" rx="3"/>
<text class="vx-mono" x="518" y="180" text-anchor="middle">V1</text>
<rect class="vx-box-strong" x="550" y="160" width="56" height="30" rx="3"/>
<text class="vx-mono" x="578" y="180" text-anchor="middle">V2</text>
<text class="vx-text-muted" x="620" y="180">float, vector</text>
<path class="vx-flow" d="M370 60 L429 51" marker-end="url(#p5-f2-head)"/>
<path class="vx-flow" d="M370 120 L429 113" marker-end="url(#p5-f2-head)"/>
<path class="vx-flow" d="M370 180 L429 175" marker-end="url(#p5-f2-head)"/>
<rect class="vx-box" x="20" y="238" width="640" height="40" rx="4"/>
<text class="vx-text" x="40" y="263">reorder buffer: every in-flight micro-op, oldest on the right</text>
<text class="vx-text-accent" x="670" y="252">retire</text>
<text class="vx-text-accent" x="670" y="268">in order</text>
<path class="vx-line" d="M80 208 L80 237" marker-end="url(#p5-f2-head)"/>
<path class="vx-flow" d="M560 190 L560 237" marker-end="url(#p5-f2-head)"/>
<text class="vx-text-muted" x="566" y="222">results</text>
</svg>
<figcaption>Figure 2. The parts of an out-of-order core that this chapter uses. Instructions enter and leave in program order; in between, each micro-operation waits in the scheduler only for its own operands and a free port of its kind. The port counts are those of LLVM's <code>apple-m1</code> model: four integer, two load and store, three floating-point and vector.</figcaption>
</figure>

??? check "An out-of-order core with a large reorder buffer runs `x = x + a[0]`, `x = x + a[1]`, and so on, one `f32` addition per element. Does a bigger reorder buffer make this loop faster?"

    No. The loads can run ahead, but every addition needs the result of the one before it, which is a true dependence, and renaming cannot remove it. A bigger window lets the core find more independent work, and this loop has none to find. It runs at one addition per addition latency.

## Latency, throughput and the number of chains

A vendor guide describes each instruction with at least two numbers. Agner Fog defines them this way.[^agner-asm] **Latency** is the number of cycles from the moment an instruction starts to execute until its result is ready for the next instruction. **Throughput** is how many independent instructions of that kind can start per cycle; most tables print its **reciprocal throughput**, the average number of cycles between the starts of two independent instructions of the same kind. His example is floating-point addition on Intel's Core 2: a latency of 3 cycles and a reciprocal throughput of 1, so a chain of dependent additions costs 3 cycles each, and independent additions cost 1 cycle each.

The two numbers answer different questions. Latency limits a **dependency chain**, a series of instructions each of which needs the result of the one before. Throughput limits independent work, and it is set by how many ports can run the instruction and how often each port accepts a new one. A port that accepts a new addition every cycle, while the previous one is still in progress, is **pipelined**; floating-point units usually are.[^agner-asm]

A loop that updates one accumulator has a **loop-carried** dependency chain: each trip's addition needs the previous trip's sum.[^agner-asm] To keep every port busy, the core needs enough independent chains that, while one chain's result is in flight, another has an instruction ready. During one latency of L cycles, P pipelined ports can start L × P instructions, so

$$\text{independent chains needed} = \frac{\text{latency}}{\text{reciprocal throughput}} = L \times P$$

rounded up to a whole chain. Agner Fog states the same rule for accumulators: the optimal number is the latency divided by the reciprocal throughput.[^agner-asm] The next example simulates it for a latency of 3 cycles on 2 ports:

--8<-- "includes/examples/optimize/p5-microarchitecture/chains_on_ports.cpp.md"

With one chain, the rate is one instruction every 3 cycles, 0.33 per cycle, and each added chain adds another 0.33. At 6 chains the rate reaches 2.00, one instruction per port per cycle, and a seventh or eighth chain changes nothing: there is no free port left for it. That bend is the **knee**, and it is what a measurement of a real core looks for. Figure 3 shows the two ends of the table as port timelines.

<figure class="vx-figure">
<svg viewBox="0 0 820 170" role="img" aria-label="Port timelines for an instruction with a latency of 3 cycles on two ports: one chain leaves ten of twelve port-cycles empty, six chains fill all of them" aria-describedby="p5-f3-desc">
<desc id="p5-f3-desc">Two timelines of six cycles each, for an instruction with a latency of 3 cycles and two ports. Left, one chain, A: A starts on port 0 in cycle 0 and again in cycle 3, when its first result is ready. Port 0 is empty in cycles 1, 2, 4 and 5, and port 1 is empty in all six cycles: ten of twelve port-cycles are wasted. Right, six chains, A to F: A and B start in cycle 0, C and D in cycle 1, E and F in cycle 2, and A and B again in cycle 3, when their results are ready, and so on. Both ports are busy in every cycle. The cells light up one column at a time to show the passage of cycles.</desc>
<text class="vx-text" x="243" y="18" text-anchor="middle">One chain: 2 of 12 port-cycles used</text>
<text class="vx-text" x="643" y="18" text-anchor="middle">Six chains: 12 of 12 used</text>
<text class="vx-text-muted" x="115" y="48" text-anchor="middle">0</text>
<text class="vx-text-muted" x="169" y="48" text-anchor="middle">1</text>
<text class="vx-text-muted" x="223" y="48" text-anchor="middle">2</text>
<text class="vx-text-muted" x="277" y="48" text-anchor="middle">3</text>
<text class="vx-text-muted" x="331" y="48" text-anchor="middle">4</text>
<text class="vx-text-muted" x="385" y="48" text-anchor="middle">5</text>
<text class="vx-mono" x="10" y="82">port 0</text>
<text class="vx-mono" x="10" y="122">port 1</text>
<rect class="vx-box-accent vx-seq" style="--vx-i:0;--vx-n:6" x="90" y="60" width="50" height="34" rx="3"/>
<text class="vx-text" x="115" y="82" text-anchor="middle">A</text>
<rect class="vx-box-bad" x="144" y="60" width="50" height="34" rx="3"/>
<rect class="vx-box-bad" x="198" y="60" width="50" height="34" rx="3"/>
<rect class="vx-box-accent vx-seq" style="--vx-i:3;--vx-n:6" x="252" y="60" width="50" height="34" rx="3"/>
<text class="vx-text" x="277" y="82" text-anchor="middle">A</text>
<rect class="vx-box-bad" x="306" y="60" width="50" height="34" rx="3"/>
<rect class="vx-box-bad" x="360" y="60" width="50" height="34" rx="3"/>
<rect class="vx-box-bad" x="90" y="100" width="50" height="34" rx="3"/>
<rect class="vx-box-bad" x="144" y="100" width="50" height="34" rx="3"/>
<rect class="vx-box-bad" x="198" y="100" width="50" height="34" rx="3"/>
<rect class="vx-box-bad" x="252" y="100" width="50" height="34" rx="3"/>
<rect class="vx-box-bad" x="306" y="100" width="50" height="34" rx="3"/>
<rect class="vx-box-bad" x="360" y="100" width="50" height="34" rx="3"/>
<text class="vx-text-muted" x="515" y="48" text-anchor="middle">0</text>
<text class="vx-text-muted" x="569" y="48" text-anchor="middle">1</text>
<text class="vx-text-muted" x="623" y="48" text-anchor="middle">2</text>
<text class="vx-text-muted" x="677" y="48" text-anchor="middle">3</text>
<text class="vx-text-muted" x="731" y="48" text-anchor="middle">4</text>
<text class="vx-text-muted" x="785" y="48" text-anchor="middle">5</text>
<text class="vx-mono" x="420" y="82">port 0</text>
<text class="vx-mono" x="420" y="122">port 1</text>
<rect class="vx-box-accent vx-seq" style="--vx-i:0;--vx-n:6" x="490" y="60" width="50" height="34" rx="3"/>
<text class="vx-text" x="515" y="82" text-anchor="middle">A</text>
<rect class="vx-box-accent vx-seq" style="--vx-i:1;--vx-n:6" x="544" y="60" width="50" height="34" rx="3"/>
<text class="vx-text" x="569" y="82" text-anchor="middle">C</text>
<rect class="vx-box-accent vx-seq" style="--vx-i:2;--vx-n:6" x="598" y="60" width="50" height="34" rx="3"/>
<text class="vx-text" x="623" y="82" text-anchor="middle">E</text>
<rect class="vx-box-accent vx-seq" style="--vx-i:3;--vx-n:6" x="652" y="60" width="50" height="34" rx="3"/>
<text class="vx-text" x="677" y="82" text-anchor="middle">A</text>
<rect class="vx-box-accent vx-seq" style="--vx-i:4;--vx-n:6" x="706" y="60" width="50" height="34" rx="3"/>
<text class="vx-text" x="731" y="82" text-anchor="middle">C</text>
<rect class="vx-box-accent vx-seq" style="--vx-i:5;--vx-n:6" x="760" y="60" width="50" height="34" rx="3"/>
<text class="vx-text" x="785" y="82" text-anchor="middle">E</text>
<rect class="vx-box-accent vx-seq" style="--vx-i:0;--vx-n:6" x="490" y="100" width="50" height="34" rx="3"/>
<text class="vx-text" x="515" y="122" text-anchor="middle">B</text>
<rect class="vx-box-accent vx-seq" style="--vx-i:1;--vx-n:6" x="544" y="100" width="50" height="34" rx="3"/>
<text class="vx-text" x="569" y="122" text-anchor="middle">D</text>
<rect class="vx-box-accent vx-seq" style="--vx-i:2;--vx-n:6" x="598" y="100" width="50" height="34" rx="3"/>
<text class="vx-text" x="623" y="122" text-anchor="middle">F</text>
<rect class="vx-box-accent vx-seq" style="--vx-i:3;--vx-n:6" x="652" y="100" width="50" height="34" rx="3"/>
<text class="vx-text" x="677" y="122" text-anchor="middle">B</text>
<rect class="vx-box-accent vx-seq" style="--vx-i:4;--vx-n:6" x="706" y="100" width="50" height="34" rx="3"/>
<text class="vx-text" x="731" y="122" text-anchor="middle">D</text>
<rect class="vx-box-accent vx-seq" style="--vx-i:5;--vx-n:6" x="760" y="100" width="50" height="34" rx="3"/>
<text class="vx-text" x="785" y="122" text-anchor="middle">F</text>
<rect class="vx-box-bad" x="90" y="146" width="22" height="14" rx="3"/>
<text class="vx-text-muted" x="118" y="157">port idle</text>
<rect class="vx-box-accent" x="220" y="146" width="22" height="14" rx="3"/>
<text class="vx-text-muted" x="248" y="157">an instruction of that chain starts</text>
</svg>
<figcaption>Figure 3. The first and sixth rows of the simulation, drawn as ports over time. Each chain's next instruction must wait 3 cycles for its predecessor's result. One chain leaves both ports mostly empty; six chains, the number the formula gives for a latency of 3 on 2 ports, leave no port-cycle empty.</figcaption>
</figure>

??? check "A guide lists an instruction with a latency of 4 cycles and a reciprocal throughput of 0.5. How many independent chains keep its ports busy, and how many ports does the reciprocal throughput imply?"

    Eight chains: 4 divided by 0.5. A reciprocal throughput of 0.5 means two independent instructions can start per cycle, which usually means two pipelined ports. With fewer than eight chains, some port-cycles stay empty; with more, the extra chains wait for a port.

The same rule sizes the core of a fast matrix multiplication. Low, Igual, Smith and Quintana-Ortí require the block of output elements that a micro-kernel keeps in registers to hold at least as many elements as the vector width times the fused multiply-add latency times the number of such instructions the core can start per cycle, so that the floating-point pipelines never stall.[^low16] [P12](p12-fast-gemm.md#the-register-blocked-micro-kernel) builds that micro-kernel.

### Where independent chains come from

Agner Fog's way to get more chains out of a sum is to split it: four accumulators, each adding every fourth element, added together at the end.[^agner-asm] For integers that is exact. For `f32` it is reassociation: the four partial sums round differently from one running sum, so the result can differ in its last bits, and [decision 56](../decisions/numbers.md#d56) forbids a Vortex compiler to do it.

`four_columns` gets its chains from somewhere else. It computes four different outputs, and each output is still summed in `k` order with one rounded multiply and one rounded add per step, exactly as `one_chain` would compute it. The four sums never read each other, so they are four independent chains, and every bit of every result is unchanged. [P7](p7-loop-transformations.md#unroll-and-jam-and-register-blocks) derives this shape from the stage 10 loop nest as unroll-and-jam.

## Three bounds on a loop

The chain count explains half of the opening table. The other half needs a way to find which limit a loop hits first. A loop in steady state cannot run a trip faster than any of three bounds allows:

1. **The recurrence bound.** Follow each loop-carried chain once around the loop and add the latencies on it. The longest such cycle is the fastest a trip can go, however many ports are free.
2. **The resource bound.** For each kind of port, count the micro-operations per trip that need it, and divide by the number of such ports.
3. **The issue bound.** Count all micro-operations per trip, and divide by the issue width.

The trip takes at least the largest of the three. The second and third are what llvm-mca reports as **Block RThroughput**, the reciprocal throughput of the loop body as if it had no loop-carried dependences.[^llvm-mca]

Take `one_chain` under the `apple-m1` model. llvm-mca prints the model's figures for each instruction: the two loads are two micro-operations each and run on the load and store pipes; `fmul` and `fadd` are one each, on the floating-point pipes, and `fadd` has a latency of 4 cycles; `subs` and `b.ne` are one each, on the integer pipes. (These figures come from LLVM's model file, which gives `fadd` on single-precision registers a latency of 4.[^cyclone])

- **Recurrence.** The chain through `s0` holds one `fadd`: 4 cycles per trip. The counter `x2` also carries a chain, through `subs`, of 1 cycle.
- **Resources.** Two floating-point micro-operations on three pipes: 0.67 cycles. Two loads on two load pipes: 1.0. Four integer micro-operations on four integer pipes: 1.0.
- **Issue.** Eight micro-operations at six per cycle: 1.33 cycles.

The largest is 4, from the recurrence, and llvm-mca simulates 4,011 cycles for 1,000 trips. Its Block RThroughput is 1.3, the issue bound: without the chain through `s0`, this loop could run three times faster. Figure 4 shows the four bounds side by side.

<figure class="vx-figure">
<svg viewBox="0 0 760 290" role="img" aria-label="The dependence graph of one trip of one_chain, with its loop-carried edge, and bars comparing its four bounds: recurrence 4 cycles, issue 1.33, loads 1.0, floating-point pipes 0.67" aria-describedby="p5-f4-desc">
<desc id="p5-f4-desc">Left: the dependence graph of one trip of the one_chain loop. Two loads, ldr s1 and ldr s2, both feed fmul, which feeds fadd s0. An edge with moving dashes leaves fadd and returns to it, labelled next trip, 4 cycles: the loop-carried chain. Separately, subs x2 has its own loop-carried edge, labelled 1 cycle, and feeds b.ne. Right: four horizontal bars on a scale from 0 to 4 cycles per trip. The recurrence bar reaches 4 and is highlighted as the bound that wins. The issue bar reaches 1.33, the load pipes bar 1.0 and the floating-point pipes bar 0.67. A note says llvm-mca simulates 4.0 cycles per trip.</desc>
<defs><marker id="p5-f4-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="22">One trip of one_chain</text>
<rect class="vx-box" x="20" y="40" width="110" height="30" rx="4"/>
<text class="vx-mono" x="75" y="60" text-anchor="middle">ldr s1</text>
<rect class="vx-box" x="150" y="40" width="110" height="30" rx="4"/>
<text class="vx-mono" x="205" y="60" text-anchor="middle">ldr s2</text>
<rect class="vx-box" x="85" y="100" width="110" height="30" rx="4"/>
<text class="vx-mono" x="140" y="120" text-anchor="middle">fmul</text>
<rect class="vx-box-accent" x="85" y="160" width="110" height="30" rx="4"/>
<text class="vx-mono" x="140" y="180" text-anchor="middle">fadd s0</text>
<path class="vx-line" d="M75 70 L120 99" marker-end="url(#p5-f4-head)"/>
<path class="vx-line" d="M205 70 L160 99" marker-end="url(#p5-f4-head)"/>
<path class="vx-line" d="M140 130 L140 159" marker-end="url(#p5-f4-head)"/>
<path class="vx-flow" d="M85 182 L50 182 L50 222 L230 222 L230 168 L196 168" marker-end="url(#p5-f4-head)"/>
<text class="vx-text-accent" x="60" y="244">next trip: 4 cycles</text>
<rect class="vx-box" x="280" y="100" width="90" height="30" rx="4"/>
<text class="vx-mono" x="325" y="120" text-anchor="middle">subs x2</text>
<rect class="vx-box" x="280" y="160" width="90" height="30" rx="4"/>
<text class="vx-mono" x="325" y="180" text-anchor="middle">b.ne</text>
<path class="vx-line" d="M325 130 L325 159" marker-end="url(#p5-f4-head)"/>
<path class="vx-line" d="M370 108 L392 108 L392 80 L310 80 L310 99" marker-end="url(#p5-f4-head)"/>
<text class="vx-text-muted" x="330" y="72">1 cycle</text>
<text class="vx-text" x="430" y="22">Cycles per trip, apple-m1 model</text>
<text class="vx-text-muted" x="430" y="62">recurrence</text>
<rect class="vx-box-accent" x="530" y="48" width="200" height="20" rx="3"/>
<text class="vx-mono" x="736" y="63">4</text>
<text class="vx-text-muted" x="430" y="102">issue</text>
<rect class="vx-box" x="530" y="88" width="67" height="20" rx="3"/>
<text class="vx-mono" x="603" y="103">1.33</text>
<text class="vx-text-muted" x="430" y="142">load pipes</text>
<rect class="vx-box" x="530" y="128" width="50" height="20" rx="3"/>
<text class="vx-mono" x="586" y="143">1.0</text>
<text class="vx-text-muted" x="430" y="182">float pipes</text>
<rect class="vx-box" x="530" y="168" width="33" height="20" rx="3"/>
<text class="vx-mono" x="569" y="183">0.67</text>
<path class="vx-line" d="M530 200 L730 200"/>
<text class="vx-text-muted" x="530" y="218" text-anchor="middle">0</text>
<text class="vx-text-muted" x="630" y="218" text-anchor="middle">2</text>
<text class="vx-text-muted" x="730" y="218" text-anchor="middle">4</text>
<text class="vx-text-muted" x="430" y="256">The largest bound wins: llvm-mca</text>
<text class="vx-text-muted" x="430" y="274">simulates 4.0 cycles per trip.</text>
</svg>
<figcaption>Figure 4. Left: one trip of <code>one_chain</code> as a dependence graph. Only <code>fadd</code> feeds its own next copy, so the loop-carried chain holds one addition. Right: the four bounds under LLVM's <code>apple-m1</code> model. The loop has three times the throughput it can use, and waits on one addition's latency every trip.</figcaption>
</figure>

### Your turn: four columns

Bound `four_columns` the same way before reading the answer. Per trip it has one load (two micro-operations), two paired loads (two each, both on the load pipes), an `add`, four `fmul`, four `fadd`, `subs` and `b.ne`.

??? check "What are the three bounds for `four_columns`, which one wins, and how many columns would make the recurrence and the resources meet?"

    The recurrence is still 4 cycles: each of the four sums carries a chain of one `fadd`, and the chains run side by side. The floating-point pipes take 8 micro-operations on 3 pipes, 2.67 cycles; the load pipes 3 loads on 2 pipes, 1.5 cycles. Issue takes 17 micro-operations at 6 per cycle, 2.83 cycles, and that matches llvm-mca's Block RThroughput of 2.8. The recurrence wins at 4, so four columns cost what one did: four times the work per cycle.

    Each column adds two floating-point micro-operations, so n columns need 2n/3 cycles of pipe time. That reaches 4 at six columns, which is the chain formula again: a latency of 4 divided by a reciprocal throughput of 2/3 per column step. With llvm-mca 18.1.8 (checked on 2026-09-24), a six-column version of the loop takes 4,015 cycles for 1,000 trips, still about 4 per trip for 50 percent more work, and an eight-column version takes 5,347, where the pipes, at 16/3 = 5.33 cycles, have taken over from the recurrence.

The same reasoning explains the rest of the opening table. LLVM's `neoverse-n1` model gives `fadd` a latency of 2 and a reciprocal throughput of 0.5, and `fmul` the same throughput, so two floating-point operations start per cycle. `one_chain` is bound at 2 by its recurrence, and `four_columns`, with 8 floating-point micro-operations at two per cycle, at 4 by its resources. The `neoverse-v2` model also has a latency of 2, with a reciprocal throughput of 0.25, four per cycle, so both loops sit at 2. (The latencies and reciprocal throughputs are those llvm-mca 18.1.8 prints for each model.)

## The reference shelf

The latencies and port counts that the bounds need live in a small set of documents. Knowing which one describes the core you have, and how its numbers were obtained, is the skill this chapter is after.

- **Apple silicon.** Apple's CPU Optimization Guide covers the ISA and its SIMD, floating-point and SME instructions, cache topology, the microarchitecture with tables of instruction latency and bandwidth, and performance-monitoring events, for M-series and A-series chips. Reading it needs a developer account and an extra agreement.[^apple-guide] A separate, public article, "Tuning your code's performance for Apple silicon", explains that a task's quality-of-service class influences which kind of core runs it, and that background work is more likely to run on the efficiency cores.[^apple-tuning]
- **Arm's Neoverse cores.** Arm publishes a Software Optimization Guide for each Neoverse core; for Neoverse V2 it is document 109898.[^neoverse-v2]
- **Intel.** Intel publishes an optimization reference manual for its 64-bit and 32-bit x86 processors.[^intel-opt]
- **Agner Fog's manuals.** Five free manuals for x86 processors from Intel, AMD and VIA. The third describes out-of-order execution, register renaming, pipelines, execution units and branch prediction for each processor, and the fourth lists instruction latencies and throughputs. He states that the microarchitecture manual is based on his own research and measurements rather than on official sources.[^agner]
- **uops.info.** Abel and Reineke built automatically generated microbenchmarks that measure the latency, throughput and port usage of x86 instructions on Intel's Core generations from Nehalem to Coffee Lake, and they report cases where their results differ considerably from earlier sources.[^uops-paper] The results are published at uops.info.[^uops]
- **Reverse-engineered notes.** Where a vendor publishes less, measurements fill the gap. Dougall Johnson's notes on the Firestorm core of the Apple M1 are one such source, and they describe the M1, not later Apple chips.[^dougall]

Every number on this shelf belongs to one core, and sometimes to one revision of one document. Before you use one, check the title: which chip, which kind of core (the M4 Pro has two), how the number was obtained (vendor documentation, independent measurement, or a model), and the date.

??? check "Dougall Johnson's notes describe the M1's performance core. You are tuning for the M4 Pro's performance cores. How should you use a latency from his tables?"

    As a hypothesis to test, not as a fact. A microarchitecture, unlike the ISA, can change completely between generations, so the M4's latencies and port counts may differ from the M1's. Look the number up in a source for the M4 if one exists, and measure it on your machine in any case, with the date and core type written next to it.

## When the model and the machine disagree

llvm-mca's documentation is direct about its limits. It does not model branch prediction, the cache hierarchy, or store-to-load forwarding, and the quality of its analysis is "inevitably affected by the quality of the scheduling models in LLVM".[^llvm-mca] A model is someone's description of a core, and it can be old, simplified or wrong.

The `apple-m1` row of the opening table is an example. LLVM 18.1.8 reports the owner's M4 Pro as host CPU `apple-m1` (checked with `llvm-mca --version` on 2026-09-24), and it has no `apple-m4` at all. In LLVM's release 18 sources, `apple-m1`, `apple-m2`, `apple-m3` and `apple-latest` all use one scheduling model, `CycloneModel`.[^aarch64-td] LLVM 18.1.8's `llc -mcpu=help` describes `apple-a7` as the CPU "formerly known as Cyclone", and the model file says its latencies mirror sections of a "Tuning Guide v1.0.1".[^cyclone] The `apple-m1` numbers in this chapter describe that model, not the M4.

Models also hold details that a two-number table hides. LLVM's model of Cyclone gives a single-precision fused multiply-add a latency of 8 cycles, and attaches to it a **read advance** of 4 cycles for results that come from another such instruction.[^cyclone] LLVM's scheduling description explains a read advance as an operand that is always read that many cycles later than usual, which lets the reading instruction start earlier relative to the one that wrote it.[^target-schedule] 

Which operand gets it matters. With llvm-mca 18.1.8 (checked on 2026-09-24), a chain through the first multiplicand, `fmadd s0, s0, s2, s3`, costs 4 cycles per step, while a chain through the addend, `fmadd s0, s1, s2, s0`, the shape of every accumulation, costs 8. Whether either number is true of any Apple core is a question only a measurement answers. Vortex never emits the fused instruction, but the lesson carries over: a latency belongs to a pair of operands, not only to an instruction.

So the numbers a compiler uses should be checked against a measurement of the core it targets. To measure the M4 Pro's `f32` addition, time a loop of dependent additions for latency and a loop of many independent chains for throughput, following [P1](p1-measure-first.md#the-reporting-rules)'s rules, with contraction turned off and the work kept on the performance cores. Fill in the table:

| Quantity, `f32` addition | LLVM 18.1.8 `apple-m1` model | Apple guide, M4 P-core | Measured, M4 Pro P-core | Machine, date |
| --- | --- | --- | --- | --- |
| Latency (cycles) | 4 | | | |
| Reciprocal throughput (cycles) | 0.33 | | | |
| Independent chains needed | 12 | | | |

??? check "Your measured latency for `f32` addition differs from the `apple-m1` model's 4 cycles. Which number should a cost model in your compiler use, and what should it record next to it?"

    The measured one, for the core you measured, because the model describes a different, older core. Record its source (the measurement), the machine, the core type and the date, so that anyone can repeat it, and keep the model's number beside it to show how far apart they are. If the Apple guide lists the instruction, record that too; three agreeing sources are stronger than one.

## For Vortex

Under decision 56, the stage 10 kernel's inner loop is `one_chain`: one rounded multiply, then one rounded add into `sum`, every trip. Its speed is set by the latency of one `f32` addition, whatever else the loop contains. The compiler may not split `sum` into partial sums, because that reassociates. It may make chains the other way, by computing several outputs at once, which [P7](p7-loop-transformations.md#unroll-and-jam-and-register-blocks) builds. Before either, the compiler should be able to say which chain limits a loop, and how much.

!!! vortex "Exercise"

    **Build** a recurrence remark for innermost loops, on the SSA form from [O3](o3-ssa.md#for-vortex), the loops from [O2](o2-cfg-and-dominance.md#for-vortex) and the remark stream from [O1](o1-optimizer-contract.md#for-vortex).

    1. **A latency table for one target core**, kept as data, not code: for each operation kind the recurrence can contain (`f32` and `f64` addition, subtraction and multiplication at least, and integer addition), a latency in cycles, with the source of each number (guide section, model, or your own measurement), the core type and the date. Fill it from this chapter's measurement table.
    2. **Chain finding.** For each innermost loop, find every cycle of values that starts at a phi in the loop header and returns to it through the latch. List the operations on each cycle, and add up their latencies from the table.
    3. **The remark**, one per loop, naming the variable that carries the longest cycle (such as `sum`), the operations on it, and the resulting bound in cycles per trip, marked as an estimate with the table's source and date. For a floating-point chain it must not suggest splitting the variable; it may say that independent chains need several outputs computed at once.

    **Not yet:** the resource and issue bounds, which need a port model of the core; unroll-and-jam and register blocking ([P7](p7-loop-transformations.md#unroll-and-jam-and-register-blocks), [P12](p12-fast-gemm.md#the-register-blocked-micro-kernel)); vectorization ([P10](p10-vectorization.md)); instruction scheduling ([C6](../backend/c6-scheduling.md)); reading latencies from LLVM's models; any reassociation.

    **Proof that it works:**

    - A golden remark file for the stage 10 program: one chain, through `sum`, of one `f32` addition per trip.
    - Three test programs with golden remarks: a loop that updates `x = x * r + c` in `f32`, whose chain holds a multiplication and an addition, so the bound is their sum; a loop with two independent `f32` sums, which reports two chains and the same bound as one; and a loop whose only loop-carried value is its integer counter.
    - A standalone measurement of `f32` addition on the M4 Pro's performance cores, outside the compiler, with chains from 1 to twice the count your table predicts, reported by P1's rules, whose knee agrees with the table within its confidence interval, or a note saying which number is wrong and why.
    - For the stage 10 kernel at 64 by 64, the remark's bound beside the measured cycles per inner trip from [P4](p4-counters-and-tools.md)'s cycle counter:

    | Kernel | Remark: cycles per inner trip (estimate) | Measured cycles per inner trip | Core type | Machine, date |
    | --- | --- | --- | --- | --- |
    | stage 10 `multiply`, 64 by 64 | | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **What is the difference between an ISA and a microarchitecture?** The ISA fixes what each instruction computes; the microarchitecture is one core's design for doing it, and it decides the speed.
    - **What do register renaming and the reorder buffer each make possible?** Renaming removes false dependences through reused register names; the reorder buffer lets instructions finish out of order and still retire in program order.
    - **What is the difference between latency and reciprocal throughput?** Latency is how long one instruction takes to produce its result; reciprocal throughput is how often independent instructions of that kind can start.
    - **How many independent chains keep a core's ports busy?** The latency divided by the reciprocal throughput, rounded up: latency times the number of pipelined ports.
    - **Which three bounds limit a loop's cycles per trip?** The longest loop-carried chain's total latency, the busiest kind of port, and the issue width; the largest wins.
    - **Why is the stage 10 inner loop bound by one addition's latency, and what may Vortex do about it?** Its sum is one chain of `f32` additions; splitting it would reassociate, so independent chains must come from computing several outputs at once.
    - **Why check a table's source and date before using its number?** Microarchitectures change between generations and between core types on one chip, and LLVM 18's model for every Apple Mac chip it knows is the model of Cyclone, the Apple A7.

## Where this comes back

!!! next "You will use this again in"

    - [P7. Loop transformations](p7-loop-transformations.md): *independent chains from several outputs*, *unroll-and-jam*
    - [P10. Vectorization](p10-vectorization.md): *execution ports*, *issue width*
    - [P12. Anatomy of a fast GEMM](p12-fast-gemm.md): *chains needed to fill the pipelines*, *register-blocked micro-kernel*
    - [P16. Capstone: the ladder, measured](p16-capstone.md): *latency-bound and throughput-bound rungs*
    - [C6. Instruction scheduling](../backend/c6-scheduling.md): *latency*, *dependency chains*, *critical path*
    - [E2. Describing a target](../backend/e2-describing-a-target.md): *scheduling models*, *read advance*
    - [E3. LLVM's allocator, scheduler and MC layer](../backend/e3-llvm-allocator-scheduler-mc.md): *llvm-mca*, *model against measurement*

## Sources and further reading

Start with sections 9.2 to 9.6 of Agner Fog's assembly manual: a few pages that cover out-of-order execution, renaming, latency, throughput, dependency chains and branch prediction, with examples. Then run llvm-mca with `-timeline` on this chapter's assembly example and read its "How llvm-mca works" section beside the output. Read section 4.2 of Low and colleagues to see the chain rule size a real micro-kernel.

[^agner-asm]: Agner Fog, "Optimizing subroutines in assembly language: An optimization guide for x86 platforms", manual 2 of his optimization manuals, last updated 2025-12-18: sections 9.2 "Out of order execution" (register renaming, micro-operations, execution units), 9.4 "Instruction latency and throughput", 9.5 "Break dependency chains" (examples 9.3a and 9.3b) and 9.6 "Jumps and calls" (branch prediction and misprediction cost). <https://www.agner.org/optimize/optimizing_assembly.pdf>
[^agner]: Agner Fog, "Software optimization resources", the list of the five optimization manuals and the description of manual 3, "The microarchitecture of Intel, AMD and VIA CPUs". <https://www.agner.org/optimize/>
[^llvm-mca]: LLVM Project, "llvm-mca - LLVM Machine Code Analyzer", sections "Description", "Using Markers" and "How llvm-mca works" (the dispatch, issue, write-back and retire stages, the retire control unit and the register file). <https://llvm.org/docs/CommandGuide/llvm-mca.html>
[^cyclone]: LLVM Project, `AArch64SchedCyclone.td`, release/18.x branch: the `CycloneModel` definition (issue width, micro-op buffer, mispredict penalty), the processor resources `CyUnitI`, `CyUnitLS`, `CyUnitV` and `CyUnitVM`, and the entries for `FADDSrr` and `FMADDSrrr`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Target/AArch64/AArch64SchedCyclone.td>
[^aarch64-td]: LLVM Project, `AArch64.td`, release/18.x branch: the `ProcessorModel` definitions for `apple-m1`, `apple-m2`, `apple-m3` and `apple-latest`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Target/AArch64/AArch64.td>
[^target-schedule]: LLVM Project, `TargetSchedule.td`, release/18.x branch: the fields of `SchedMachineModel` and the comment on `ProcReadAdvance`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/include/llvm/Target/TargetSchedule.td>
[^low16]: Tze Meng Low, Francisco D. Igual, Tyler M. Smith and Enrique S. Quintana-Orti, "Analytical Modeling Is Enough for High-Performance BLIS", *ACM Transactions on Mathematical Software* 43(2), 2016: section 4.2.2, "Latency of instructions", inequality (1). <https://doi.org/10.1145/2925987> (authors' copy: <https://www.cs.utexas.edu/~flame/pubs/TOMS-BLIS-Analytical.pdf>)
[^apple-guide]: Apple, "Apple Silicon CPU Optimization Guide", documentation page: the summary of contents and the access requirements. <https://developer.apple.com/documentation/apple-silicon/cpu-optimization-guide>
[^apple-tuning]: Apple, "Tuning your code's performance for Apple silicon", section "Assign Quality-of-Service (QoS) Classes to Work". <https://developer.apple.com/documentation/apple-silicon/tuning-your-code-s-performance-for-apple-silicon>
[^neoverse-v2]: Arm, "Arm Neoverse V2 Software Optimization Guide", document 109898. <https://developer.arm.com/documentation/109898/latest/>
[^intel-opt]: Intel, "Intel 64 and IA-32 Architectures Optimization Reference Manual". <https://www.intel.com/content/www/us/en/developer/articles/technical/intel64-and-ia32-architectures-optimization.html>
[^uops-paper]: Andreas Abel and Jan Reineke, "uops.info: Characterizing Latency, Throughput, and Port Usage of Instructions on Intel Microarchitectures", *Proceedings of the 24th International Conference on Architectural Support for Programming Languages and Operating Systems (ASPLOS)*, 2019: the abstract. <https://doi.org/10.1145/3297858.3304062> (preprint: <https://arxiv.org/abs/1810.04610>)
[^uops]: Andreas Abel and Jan Reineke, uops.info, measured instruction data. <https://uops.info/>
[^dougall]: Dougall Johnson, "Firestorm Overview", reverse-engineered notes on the performance core of the Apple M1. <https://dougallj.github.io/applecpu/firestorm.html>
