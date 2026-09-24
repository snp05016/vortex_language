# P5. The microarchitecture shelf

<p class="page-intro">The same instruction runs at different speeds on different chips. This chapter explains why, in terms of pipelines, out-of-order execution and execution ports, and teaches you to read the manuals that turn "it runs" into "it runs in N cycles" on one particular core.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 22 minutes · Builds on: [P2. The memory hierarchy](p2-memory-hierarchy.md), [P4. Seeing inside the CPU: counters and tools](p4-counters-and-tools.md)</p>

???+ remember "Before you start, remember"

    ??? question "What generally happens to a load's latency when the value it reads has to come from main memory instead of the cache closest to the core?"

        It grows sharply. Each level of the memory hierarchy sits further from the core and is slower to reach, which is why a loop's data-access pattern, not only its instruction count, decides how fast it runs.

        Introduced in [P2. The memory hierarchy](p2-memory-hierarchy.md#the-hierarchy-several-sizes-several-speeds).

    ??? question "Why does the order in which a loop nest walks an array matter, even though it touches the same elements either way?"

        Because it changes which bytes are already close to the core (in the same cache line, or a recently used one) when each access happens. The same set of elements, visited in a different order, can turn mostly-hit accesses into mostly-miss ones.

        Introduced in [P2. The memory hierarchy](p2-memory-hierarchy.md#cache-lines-and-why-order-matters).

    ??? question "What can a static analyzer such as llvm-mca tell you about a loop's assembly without running the program?"

        A predicted schedule: roughly how many instructions retire per cycle, which of the core's resources are the bottleneck, and where the model expects stalls. It does this from a description of the core, not from execution, so it knows nothing about cache misses or branch mispredictions.

        Introduced in [P4. Seeing inside the CPU: counters and tools](p4-counters-and-tools.md).

    ??? question "What can a hardware performance counter tell you that a single wall-clock timing cannot?"

        Why a change helped or did not: for example, whether fewer instructions ran, whether more of them retired per cycle, or whether fewer memory accesses missed the cache. A counter turns "it got faster" into a reason.

        Introduced in [P4. Seeing inside the CPU: counters and tools](p4-counters-and-tools.md).

!!! goals "In this chapter"

    - Explain why the same instruction set can run at different speeds on different chips, and why that pushes performance work down to a chip's own optimization guide.
    - Describe how a pipelined, superscalar, out-of-order core keeps several instructions in flight, and name the parts that make it possible: pipeline stages, issue width, execution ports and the reorder buffer.
    - Distinguish an instruction's latency from its throughput, and compute how many independent dependency chains it takes to hide one behind the other.
    - Read a vendor's per-instruction latency and throughput data well enough to pull out the one or two numbers a kernel's inner loop needs.
    - Recognize which manual on the reference shelf covers which chip, and why a manual written for the wrong chip generation can mislead you.

## One instruction, two chips, two speeds

Take a single AArch64 instruction: `fmadd s0, s1, s2, s0`, a fused multiply-add that computes `s0 = s0 + s1*s2` in one step. AArch64 is an **instruction set architecture** (ISA): a fixed contract that says which instructions exist, what each one computes, and how registers, memory and flags behave.[^langref] Apple's M4 and Arm's Neoverse V2 both implement AArch64, and both accept this exact instruction with this exact meaning. Neither chip is free to compute anything else when it sees `fmadd`.

What the ISA does not say is how long the instruction takes, or how many of them a core can have in flight at once. That is the chip's **microarchitecture**: the internal design that carries out the ISA's contract, built from pipelines, caches, branch predictors and functional units the ISA never mentions. Two chips can implement the same ISA with different microarchitectures, the way two engines can both burn gasoline through a four-stroke cycle and still have different horsepower. A compiler that emits AArch64 does not choose the microarchitecture; the person who bought the machine did. But a compiler (or a programmer) that wants a kernel's inner loop to run fast has to know the microarchitecture it will run on, because the ISA alone under-determines the answer.

This is why performance work reads two different kinds of document. An **ISA manual**, such as the Arm Architecture Reference Manual, tells you `fmadd` exists and what it computes: enough to generate correct code. A chip's own **optimization guide** tells you how long `fmadd` takes on that chip and how many of them it can start per cycle: what you need to generate *fast* code. The rest of this chapter builds the vocabulary those optimization guides assume, then shows where to find one for the chip you have.

## Instructions do not run start to finish, one at a time

A processor could, in principle, fetch one instruction, decode it, execute it, write its result, and only then start the next one. Some simple microcontrollers work close to that way. A **pipeline** breaks that sequence into stages, so that while one instruction is executing, the next is already being decoded, and the one after that is already being fetched. Each stage does one small piece of work every cycle and hands the instruction to the next stage, the way a factory line has one worker per station instead of one worker who builds a whole product before starting the next.

The example below models a small, deliberately simplified pipeline with four stages: fetch (F), decode (D), execute (E) and writeback (W). One instruction enters fetch per cycle, and each stage holds one instruction at a time, in program order. If an instruction reads a value the instruction immediately before it writes, decode has to wait until that write reaches writeback, because this simple model has no way to hand a result to the next instruction early:

--8<-- "includes/examples/optimize/p5-microarchitecture/pipeline_stages.cpp.md"

Read the two totals at the bottom. Four independent instructions, run one at a time with no overlap, cost four stages times four instructions: 16 cycles. Pipelined, with no hazard, they cost 7: the stages overlap, so the whole program finishes roughly one stage-width after the last instruction starts, not four stage-widths after the first one does. The third instruction, `add t2, t1`, reads the result `add t1` wrote, so its decode has to wait for that write to finish; the pipeline still overlaps everything else, but the wait shows up as two wasted cycles, a **bubble**, and the total rises to 9. Figure 1 draws the same schedule as a grid of cycles: read a column as "what every instruction is doing this cycle" and a row as "what one instruction did over time".

<figure class="vx-figure">
<svg viewBox="0 0 660 210" role="img" aria-labelledby="p5-pipe-title p5-pipe-desc">
<title id="p5-pipe-title">Four instructions through a four-stage pipeline, one with a stall</title>
<desc id="p5-pipe-desc">A grid of nine cycles by four instructions. mul t0 runs fetch, decode, execute, writeback in cycles 0 to 3 with no gaps. add t1 follows one cycle behind, in cycles 1 to 4. add t2, t1 is fetched in cycle 2, then sits in fetch for two more cycles because decode is not free until the value it needs, written by add t1, reaches writeback; its decode, execute and writeback run in cycles 5, 6 and 7. mul t3 is fetched only once add t2, t1 has moved out of the fetch stage, in cycle 5, and runs decode, execute, writeback in cycles 6, 7 and 8.</desc>
<text class="vx-text-muted" x="393" y="10" text-anchor="middle">cycle</text>
<text class="vx-text-muted" x="177" y="24" text-anchor="middle">0</text>
<text class="vx-text-muted" x="231" y="24" text-anchor="middle">1</text>
<text class="vx-text-muted" x="285" y="24" text-anchor="middle">2</text>
<text class="vx-text-muted" x="339" y="24" text-anchor="middle">3</text>
<text class="vx-text-muted" x="393" y="24" text-anchor="middle">4</text>
<text class="vx-text-muted" x="447" y="24" text-anchor="middle">5</text>
<text class="vx-text-muted" x="501" y="24" text-anchor="middle">6</text>
<text class="vx-text-muted" x="555" y="24" text-anchor="middle">7</text>
<text class="vx-text-muted" x="609" y="24" text-anchor="middle">8</text>
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
<rect class="vx-box" x="366" y="76" width="50" height="34" rx="3"/>
<text class="vx-text" x="391" y="98" text-anchor="middle">W</text>
<text class="vx-mono" x="10" y="140">add t2, t1</text>
<rect class="vx-box" x="258" y="118" width="50" height="34" rx="3"/>
<text class="vx-text" x="283" y="140" text-anchor="middle">F</text>
<rect class="vx-box-accent" x="312" y="118" width="50" height="34" rx="3"/>
<text class="vx-text" x="337" y="140" text-anchor="middle">F</text>
<rect class="vx-box-accent" x="366" y="118" width="50" height="34" rx="3"/>
<text class="vx-text" x="391" y="140" text-anchor="middle">F</text>
<rect class="vx-box" x="420" y="118" width="50" height="34" rx="3"/>
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
<figcaption>Figure 1. The dashed cells (the F held by <code>add t2, t1</code> in cycles 3 and 4) are the bubble: the instruction has been fetched but cannot decode yet, because decode needs the value <code>add t1</code> has not written back. Real cores shorten most bubbles like this one by forwarding a result straight from execute to a waiting decode instead of waiting for writeback; this model does not, so the bubble stays visible.</figcaption>
</figure>

??? check "In this chapter's pipeline model, why does the hazard cost exactly two cycles rather than one?"

    Without the hazard, `add t2, t1` would decode the cycle after it fetches, at cycle 3. With it, decode has to wait until `add t1`'s writeback, at cycle 4, finishes, so decode moves to cycle 5. The two cycles come from the gap between "decode could normally start" (cycle 3) and "the value is finally available" (cycle 5, one past the write at cycle 4): every stage the hazard adds a wait to shows up once in the total.

Vendor manuals do not usually draw this exact grid. They give you the two numbers a grid like this would reduce to for one instruction, which is the subject of the rest of this chapter.

## Doing more than one instruction at once

A pipeline alone still fetches and executes one instruction at a time; it only overlaps their stages. A core that is **superscalar** can fetch, decode and start executing more than one instruction in the same cycle: its **issue width** is how many instructions it can start per cycle. A core that is **out-of-order** does not require instructions to execute in the order they appear in the program. It fetches and decodes in order, but once an instruction's operands are ready, it can execute as soon as a suitable execution unit is free, even if an earlier instruction is still waiting on something slow, such as a cache miss. To make this safe, the core tracks every instruction that has started but not yet finished in a structure usually called the **reorder buffer** (ROB): each entry holds an in-flight instruction's result until every instruction before it has also finished, so that results still **retire**, become visible, in the original program order, even though they did not necessarily execute in that order.

An out-of-order core with a wide reorder buffer can hide many hazards by itself: if one instruction is stuck waiting on a dependency, the core looks further ahead in its instruction window for something unrelated to run instead. This sounds like it should make the reasoning in the previous section obsolete. It does not, for one reason: if the *only* available work is one long chain of dependent instructions, there is nothing unrelated for the core to run while it waits. No amount of looking ahead invents independent work that is not there. A dependency chain is a hazard hardware reordering cannot route around; it can only be hidden by giving the core other work to interleave with it. That is exactly what the next section is for.

??? check "An out-of-order core with a large reorder buffer is running one long chain of dependent additions, `x = x + a[0]`, `x = x + a[1]`, and so on. Does a bigger reorder buffer make this loop finish faster?"

    No, not on its own. Every addition depends on the one before it, so there is nothing independent in the instruction window for the core to run ahead of the chain. A bigger window lets the core look further for unrelated work, but there is no unrelated work here to find; the loop is bound by the chain's own latency, one addition at a time.

## Latency, throughput and how many chains hide one behind the other

A vendor's optimization guide describes each instruction with (at least) two numbers. **Latency** is how many cycles pass between an instruction starting and its result being ready to use. **Throughput**, usually published as a **reciprocal throughput** (cycles per instruction, the inverse of instructions per cycle), is the smallest average gap between two independent instructions of the same kind starting, set by how many of the core's execution ports can run that kind of instruction and how often each port can accept a new one. An **execution port** is one of a core's parallel lanes for carrying out an instruction; a core with, say, two ports that can both execute a floating-point multiply-add can start two independent multiply-adds in the same cycle, even though each one still takes several cycles to produce its result.

Latency and throughput answer different questions, and the gap between them is exactly the gap the previous two sections were building toward. If you issue one `fmadd` and use its result in the next `fmadd`, the second cannot start until the first one's latency has passed: a single dependency chain runs at one instruction every *latency* cycles, no faster, no matter how many ports the core has. If instead you have several **independent** `fmadd` chains, ones that do not read each other's results, the core can start one from a different chain every *reciprocal-throughput* cycles, because each waits on its own predecessor, not on the others.

This gives a formula for how many independent chains it takes to keep a core's ports fully busy despite an instruction's latency:

$$\text{independent chains needed} = \left\lceil \frac{\text{latency (cycles)}}{\text{reciprocal throughput (cycles)}} \right\rceil$$

Read the other way, using throughput as a rate (instructions per cycle) instead of its reciprocal, that is latency times throughput: how many instructions the ports *could* have finished during one instruction's latency, if they were never idle. Below that many chains, some port sits idle waiting for a chain's next value to become ready; at or above it, there is always another chain with an independent instruction ready to issue.

--8<-- "includes/examples/optimize/p5-microarchitecture/accumulator_count.cpp.md"

The three rows are not any real chip's numbers; they only exercise the formula. A single-port instruction whose latency is 3 cycles needs 3 chains to keep that one port busy every cycle: even with only one port, the port itself could start a new instruction every cycle, so three unrelated chains, taking turns, keep it fed. Give the same latency to an instruction that can run on two ports and the requirement doubles, to 6, because now there are two ports to keep fed instead of one. Double the latency again, keeping two ports, and the requirement doubles again, to 12: a slower instruction needs proportionally more independent work in flight to hide behind the same throughput.

Figure 2 draws the two-port, latency-3 row from the table as a timeline. On the left, one chain uses only one of the two ports, and even that port sits idle two cycles out of every three while the chain's own value is still in flight. On the right, six chains, taking turns two at a time, keep both ports busy on every cycle.

<figure class="vx-figure">
<svg viewBox="0 0 820 145" role="img" aria-labelledby="p5-ports-title p5-ports-desc">
<title id="p5-ports-title">One chain leaves a port idle; enough chains keep both ports busy</title>
<desc id="p5-ports-desc">Two six-cycle timelines for an instruction with a latency of 3 cycles and two ports. Left: one dependency chain, A, issues on port 0 at cycle 0 and again at cycle 3, once its first result is ready; port 1 is never used, and port 0 itself is idle in cycles 1 and 2. Right: six independent chains, A through F, issue two at a time, one per port, every cycle; each chain reissues at cycle 3 once its 3-cycle latency has passed, so both ports are busy in every one of the six cycles shown.</desc>
<text class="vx-text" x="243" y="18" text-anchor="middle">One chain: port 1 stays idle</text>
<text class="vx-text-muted" x="117" y="54" text-anchor="middle">0</text>
<text class="vx-text-muted" x="171" y="54" text-anchor="middle">1</text>
<text class="vx-text-muted" x="225" y="54" text-anchor="middle">2</text>
<text class="vx-text-muted" x="279" y="54" text-anchor="middle">3</text>
<text class="vx-text-muted" x="333" y="54" text-anchor="middle">4</text>
<text class="vx-text-muted" x="387" y="54" text-anchor="middle">5</text>
<text class="vx-mono" x="10" y="82">port 0</text>
<rect class="vx-box" x="90" y="60" width="50" height="34" rx="3"/>
<text class="vx-text" x="115" y="82" text-anchor="middle">A</text>
<rect class="vx-line" x="144" y="60" width="50" height="34" rx="3" fill="none"/>
<rect class="vx-line" x="198" y="60" width="50" height="34" rx="3" fill="none"/>
<rect class="vx-box" x="252" y="60" width="50" height="34" rx="3"/>
<text class="vx-text" x="277" y="82" text-anchor="middle">A</text>
<rect class="vx-line" x="306" y="60" width="50" height="34" rx="3" fill="none"/>
<rect class="vx-line" x="360" y="60" width="50" height="34" rx="3" fill="none"/>
<text class="vx-mono" x="10" y="122">port 1</text>
<rect class="vx-line" x="90" y="100" width="50" height="34" rx="3" fill="none"/>
<rect class="vx-line" x="144" y="100" width="50" height="34" rx="3" fill="none"/>
<rect class="vx-line" x="198" y="100" width="50" height="34" rx="3" fill="none"/>
<rect class="vx-line" x="252" y="100" width="50" height="34" rx="3" fill="none"/>
<rect class="vx-line" x="306" y="100" width="50" height="34" rx="3" fill="none"/>
<rect class="vx-line" x="360" y="100" width="50" height="34" rx="3" fill="none"/>
<text class="vx-text" x="643" y="18" text-anchor="middle">Six chains: both ports stay busy</text>
<text class="vx-text-muted" x="517" y="54" text-anchor="middle">0</text>
<text class="vx-text-muted" x="571" y="54" text-anchor="middle">1</text>
<text class="vx-text-muted" x="625" y="54" text-anchor="middle">2</text>
<text class="vx-text-muted" x="679" y="54" text-anchor="middle">3</text>
<text class="vx-text-muted" x="733" y="54" text-anchor="middle">4</text>
<text class="vx-text-muted" x="787" y="54" text-anchor="middle">5</text>
<text class="vx-mono" x="410" y="82">port 0</text>
<rect class="vx-box" x="490" y="60" width="50" height="34" rx="3"/>
<text class="vx-text" x="515" y="82" text-anchor="middle">A</text>
<rect class="vx-box" x="544" y="60" width="50" height="34" rx="3"/>
<text class="vx-text" x="569" y="82" text-anchor="middle">C</text>
<rect class="vx-box" x="598" y="60" width="50" height="34" rx="3"/>
<text class="vx-text" x="623" y="82" text-anchor="middle">E</text>
<rect class="vx-box" x="652" y="60" width="50" height="34" rx="3"/>
<text class="vx-text" x="677" y="82" text-anchor="middle">A</text>
<rect class="vx-box" x="706" y="60" width="50" height="34" rx="3"/>
<text class="vx-text" x="731" y="82" text-anchor="middle">C</text>
<rect class="vx-box" x="760" y="60" width="50" height="34" rx="3"/>
<text class="vx-text" x="785" y="82" text-anchor="middle">E</text>
<text class="vx-mono" x="410" y="122">port 1</text>
<rect class="vx-box" x="490" y="100" width="50" height="34" rx="3"/>
<text class="vx-text" x="515" y="122" text-anchor="middle">B</text>
<rect class="vx-box" x="544" y="100" width="50" height="34" rx="3"/>
<text class="vx-text" x="569" y="122" text-anchor="middle">D</text>
<rect class="vx-box" x="598" y="100" width="50" height="34" rx="3"/>
<text class="vx-text" x="623" y="122" text-anchor="middle">F</text>
<rect class="vx-box" x="652" y="100" width="50" height="34" rx="3"/>
<text class="vx-text" x="677" y="122" text-anchor="middle">B</text>
<rect class="vx-box" x="706" y="100" width="50" height="34" rx="3"/>
<text class="vx-text" x="731" y="122" text-anchor="middle">D</text>
<rect class="vx-box" x="760" y="100" width="50" height="34" rx="3"/>
<text class="vx-text" x="785" y="122" text-anchor="middle">F</text>
</svg>
<figcaption>Figure 2. Both timelines assume the same core: two ports, each able to execute this instruction, and a 3-cycle latency. On the left, a single chain has nothing to fill the gap while its own result is in flight, so five of the twelve port-cycles are wasted. On the right, six chains give the core enough independent work that no port-cycle is wasted; six is exactly what the formula predicts for a latency of 3 and a reciprocal throughput of 0.5.</figcaption>
</figure>

??? check "A core's guide lists an instruction's latency as 4 cycles and its throughput as one per cycle on a single port. How many independent chains does it take to keep that port busy every cycle, and would a second port change the answer?"

    Four chains: latency 4 divided by reciprocal throughput 1 is 4. A second port able to run the same instruction would not change the chain count implied by latency alone that way; it would let the core start two chains' instructions in the same cycle, so keeping *both* ports fed would need twice as many chains, 8, the same doubling this section's table shows going from one port to two.

The code below shows what those two accumulator patterns look like as AArch64 assembly: `chain_single` folds four products into one running accumulator, so each `fmadd` waits for the one before it; `chain_four` uses four separate accumulators and only adds them together at the end, so none of the four `fmadd` instructions in the loop depends on another:

--8<-- "includes/examples/optimize/p5-microarchitecture/dependency_chain.s.md"

Before reading on, work through one question with these two functions and nothing else: if this core's `fmadd` had a latency greater than its reciprocal throughput, which function would let the core keep its FMA ports fuller, and why does adding a fifth, sixth or tenth independent accumulator eventually stop helping? The second question is this chapter's formula stated backwards: past the chain count the formula gives, the ports are already saturated, so another chain has nowhere left to issue into. (`chain_four`'s four chains may or may not be enough for a given core; that depends on numbers this generic example deliberately does not supply.)

## The reference shelf

The numbers this chapter has been treating as inputs, an instruction's latency and reciprocal throughput, live in a small set of documents, one per vendor or per independent effort to measure what vendors do not publish in full. Reading the right one, for the right chip, is the actual skill this chapter is building toward.

For Apple Silicon, Apple's own **CPU Optimization Guide** covers the ISA extensions the chip supports, cache topology, latency and bandwidth tables, and the events its performance monitoring unit (PMU) exposes; it requires a free developer account and an agreement before it can be read.[^apple-guide] Apple also publishes a shorter, freely readable guide, **Tuning your code's performance for Apple silicon**, which explains techniques, such as giving the processor independent, out-of-order-friendly work instead of one long dependency chain, without the raw per-instruction tables.[^apple-tuning] For Arm's server and infrastructure cores, the **Neoverse V2 Software Optimization Guide** is Arm's counterpart, publicly listed by Arm as a reference for exactly this chip family.[^neoverse-v2]

For x86-64, three sources cover most of what you need, and they do not always agree, which is itself worth knowing before you trust a single number. Intel publishes its own **Optimization Reference Manual** for its processor families.[^intel-opt] Agner Fog's five freely available manuals independently re-measure instruction timings across many x86 chip generations, from Intel and AMD alike, and are a common second opinion against a vendor's own numbers.[^agner] **uops.info** takes a third approach: an automated measurement harness that runs on real hardware and publishes latency, throughput and port-usage data for a large set of x86 instructions, with its methodology described in a peer-reviewed paper.[^uops][^uops-paper]

Not every chip has an official low-level guide. Apple has not published detailed microarchitectural documentation for its M1 generation the way it later did for later chips; independent reverse engineering filled part of that gap. Dougall Johnson's notes on the M1's Firestorm core are a widely cited example, built by measuring the chip directly rather than from vendor documentation.[^dougall] They come with an important limit worth restating plainly: they describe the M1, not later Apple Silicon chips, and a number measured on one microarchitecture is not automatically true of its successor, even from the same vendor.[^dougall] Reading any of these manuals starts with checking that title: which chip, which core (a chip with performance and efficiency cores, for example, can have two different answers for the same instruction), and, since guides are revised, which date.

A static analyzer such as **llvm-mca**, covered in [P4](p4-counters-and-tools.md), turns a table like the ones these manuals publish into a predicted schedule for a specific piece of assembly, without your having to trace it by hand the way Figure 1 did.[^llvm-mca] It needs a model of the target core to do that, built from the same kind of latency, throughput and port data this chapter has been describing; when its model is missing or out of date for a chip, its predictions for that chip are only as good as the fallback it uses.

??? check "Dougall Johnson's Firestorm notes describe the M1. Why does that matter if you are tuning a kernel for an M4 Pro?"

    Because a chip's microarchitecture, unlike its ISA, does not have to stay the same across generations from the same vendor. The M4 is a later design; its pipeline depth, port count and per-instruction latencies can all differ from the M1's, so a number measured on the M1 is a clue, not a fact, about the M4. The safe use of such notes is to understand the *kind* of reasoning a vendor's chip rewards, and then confirm any specific number against a source for the chip you have, or by measuring it yourself.

## For Vortex

!!! vortex "Exercise"

    **Investigate, then build a small measurement, outside the compiler.** None of this chapter's exercise touches Vortex's source: it is the kind of investigation [P1](p1-measure-first.md) and [P3](p3-roofline.md) ask you to be able to do before you trust any performance claim about the kernel from [stage 10](../compiler/guide/stage-10-matrix-multiplication.md).

    1. On your own machine, find the M4 P-core's `fmadd` latency and reciprocal throughput in Apple's CPU Optimization Guide.[^apple-guide] Record both numbers together with the guide's section and today's date: a guide gets revised, and a number without its source and date cannot be checked later.
    2. Apply this chapter's formula to derive the minimum number of independent accumulators needed to keep the P-core's floating-point ports busy despite that latency.
    3. Write a small, standalone microbenchmark, separate from the Vortex compiler, that runs an accumulator chain like `chain_single` and `chain_four` in this chapter's assembly example at several chain counts (1, 2, 3, and on past the number step 2 derived), following [P1](p1-measure-first.md)'s measurement protocol.
    4. Plot or tabulate chain count against measured throughput and find the "knee": the chain count past which adding more stops helping.
    5. Compare the measured knee to the number step 2 derived from the guide. If they differ, write down which explanation is most likely: measurement noise, a detail the guide's number does not capture (such as a second dependency the microbenchmark introduces by accident), or a misread section of the guide. State which one, and why, rather than reporting only the numbers.

    **Not yet:** vectorized (SIMD) accumulation, which needs the port and lane model from [P10](p10-vectorization.md); the packing and micro-kernel design of a full GEMM, which needs [P12](p12-fast-gemm.md); and writing anything into the Vortex compiler that schedules instructions by hand, which is the back end's job, much later, once Vortex has one of its own ([C6](../backend/c6-scheduling.md)). This chapter is about reading and measuring numbers, not yet about generating code that assumes them.

    **Proof that it works:**

    - A short written note, dated, naming the guide's section, recording the M4 P-core's `fmadd` latency, reciprocal throughput, and the derived minimum accumulator count.
    - A table from the microbenchmark: chain count from 1 up to at least twice the derived count, each row's measured throughput, the machine, and the date.
    - A one-paragraph comparison of the measured knee to the derived count, stating whether they agree within the noise your protocol reports, and if not, which explanation from step 5 you settled on.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is the difference between an ISA and a microarchitecture?** The ISA is the fixed contract of which instructions exist and what they compute; the microarchitecture is one chip's internal design for carrying that contract out, which decides how fast.
    - **What does a pipeline let a core do that running each instruction start to finish cannot?** Overlap the stages of consecutive instructions, so the core is doing useful work on several instructions in the same cycle instead of on only one.
    - **What is a bubble, and where does it come from?** A cycle in which a pipeline stage sits idle because the instruction waiting to enter it cannot yet, usually because it needs a value an earlier instruction has not produced yet.
    - **Why does an out-of-order core with a large reorder buffer still care about independent work?** Because reordering only routes around hazards when there is something unrelated available to run; a single dependency chain offers nothing unrelated, so its own latency still bounds how fast it issues.
    - **What is the difference between an instruction's latency and its throughput?** Latency is how long one instruction takes to produce a result; throughput (usually published as its reciprocal, cycles per instruction) is how often independent instructions of that kind can start, set by how many execution ports can run them.
    - **How many independent chains does it take to hide an instruction's latency behind its throughput?** The latency divided by the reciprocal throughput, rounded up: equivalently, the latency times the throughput expressed as instructions per cycle.
    - **Why must a manual's chip and date be checked before trusting a number from it?** Because microarchitecture, unlike the ISA, changes across chip generations and sometimes across cores within one chip, and vendor guides are themselves revised over time.

## Where this comes back

!!! next "You will use this again in"

    - [P10. Vectorization](p10-vectorization.md): *execution ports*, *issue width*, *hiding latency behind throughput*, now across SIMD lanes as well as scalar chains
    - [P12. Anatomy of a fast GEMM](p12-fast-gemm.md): *minimum independent accumulators*, sized to fill a micro-kernel's registers instead of leaving ports idle
    - [P16. Capstone: the ladder, measured](p16-capstone.md): the same latency-and-throughput reasoning, applied to the whole matmul ladder and checked against measurement
    - [A1. The machine model](../backend/a1-machine-model.md): a formal model of registers, instructions and the machine a back end targets, of which this chapter's pipeline and ports are the performance-facing half
    - [C6. Instruction scheduling](../backend/c6-scheduling.md): a scheduler's job is exactly to order instructions using latency and throughput data like this chapter's, so that independent work fills the gaps dependency chains leave

## Sources and further reading

This chapter leans on the Apple, Arm, Intel, Agner Fog and uops.info material listed above as the primary path into each vendor's own numbers; read whichever one matches your machine before trusting a specific latency or throughput figure. LLVM's llvm-mca documentation is the fastest way to turn such numbers into a predicted schedule for a piece of assembly you already have, and Dougall Johnson's notes are worth reading once for the style of reasoning they model, with their M1-specific numbers treated as historical rather than current.

[^langref]: LLVM Project, "LLVM Language Reference Manual". <https://llvm.org/docs/LangRef.html>
[^apple-guide]: Apple, "Apple Silicon CPU Optimization Guide" (developer account and agreement required). <https://developer.apple.com/documentation/apple-silicon/cpu-optimization-guide>
[^apple-tuning]: Apple, "Tuning your code's performance for Apple silicon". <https://developer.apple.com/documentation/apple-silicon/tuning-your-code-s-performance-for-apple-silicon>
[^neoverse-v2]: Arm, "Arm Neoverse V2 Software Optimization Guide", document 109898. <https://developer.arm.com/documentation/109898/latest/>
[^intel-opt]: Intel, "Intel 64 and IA-32 Architectures Optimization Reference Manual". <https://www.intel.com/content/www/us/en/developer/articles/technical/intel64-and-ia32-architectures-optimization.html>
[^agner]: Agner Fog, optimization manuals. <https://www.agner.org/optimize/>
[^uops]: uops.info. <https://uops.info/>
[^uops-paper]: Andreas Abel and Jan Reineke, "uops.info: Characterizing Latency, Throughput, and Port Usage of Instructions on Intel Microarchitectures", ASPLOS 2019. <https://doi.org/10.1145/3297858.3304062>
[^dougall]: Dougall Johnson, "Firestorm", reverse-engineered notes on the Apple M1's performance core. <https://dougallj.github.io/applecpu/firestorm.html>
[^llvm-mca]: LLVM Project, "llvm-mca - LLVM Machine Code Analyzer", command guide. <https://llvm.org/docs/CommandGuide/llvm-mca.html>
