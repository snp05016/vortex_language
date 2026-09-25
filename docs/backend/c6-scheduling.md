# C6. Instruction scheduling

<p class="page-intro">An instruction scheduler reorders the instructions of a block so that the processor waits less for results, without changing what any instruction computes. This chapter builds the dependence graph, runs list scheduling by hand, weighs it against register pressure and against an out-of-order core that reorders on its own, and shows why the inner loop of the Vortex matrix multiplication is bound by the latency of one addition, a limit that no reordering can remove.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 45 minutes · Builds on: [C1. Instruction selection](c1-instruction-selection.md), [C2. Liveness](c2-liveness.md), [C3. Register allocation I: linear scan](c3-linear-scan.md)</p>

???+ remember "Before you start, remember"

    ??? question "When do two values interfere, and what does that forbid?"

        When some point in the program has both of them live at once. Two
        values that interfere cannot share a register, so the number of
        values live at the same point is the number of registers that point
        needs.

        Introduced in [C2. Liveness](c2-liveness.md#interference).

    ??? question "What one property must the instruction order that linear scan sweeps have?"

        Every definition comes before every use it reaches. Any order with
        that property works, which leaves a compiler free to choose among
        many orders of the same block.

        Introduced in [C3. Register allocation I: linear scan](c3-linear-scan.md#ordering-instructions-for-a-scan).

    ??? question "What are a flow, an anti and an output dependence?"

        A flow (true) dependence: a later access reads what an earlier one
        wrote. An anti dependence: a later access writes what an earlier one
        read. An output dependence: two accesses write the same place. Only
        a flow dependence carries a value; all three fix an order.

        Introduced in [P6. Dependence analysis](../optimize/p6-dependence-analysis.md#naming-what-changes-flow-anti-and-output-dependence).

    ??? question "Which reorderings of floating-point work leave every result's bits unchanged?"

        Those that change only which independent result is computed first.
        Changing the grouping inside one result's chain of additions, or
        fusing a multiply and an add, changes its roundings, and Vortex
        forbids both.

        Introduced in [P11. Floating point under optimization](../optimize/p11-floating-point.md#the-rule-every-reordering-pass-needs). Decision: [record 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Build the dependence graph of a basic block, with each edge weighted by the latency of the instruction it leaves, and find its critical path.
    - Run list scheduling by hand with a critical-path priority, and check a finished schedule against the graph.
    - Explain why scheduling before register allocation raises register pressure, and why scheduling after it meets false dependences.
    - Predict when reordering helps a core and when the core's own out-of-order hardware has already done the work, and confirm the prediction with llvm-mca.
    - Compute the recurrence bound of a loop, and explain why the stage 10 kernel's inner loop runs at one iteration per addition latency whatever its schedule.

A processor starts an instruction, and a few cycles later its result is
ready. The **latency** of an instruction is that delay: the cycles from its
start until an instruction that needs its result can start
([P5](../optimize/p5-microarchitecture.md#latency-throughput-and-the-number-of-chains)
defines and measures it). An instruction that needs the result waits, and a
simple core does nothing useful meanwhile.

**Instruction scheduling** is the back-end pass that orders instructions so
that less of that waiting happens. It moves an instruction only past others
that do not depend on it, so every instruction reads the same inputs and
computes the same bits. Its correctness argument fits in a sentence; its
benefit depends entirely on the machine. This chapter works on one small block,
then on the inner loop of the
[stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for)
matrix multiplication.

## Nine instructions, two orders

Here is a basic block that computes the squared length of a 3-vector,
`x*x + y*y + z*z`, added left to right, and stores it. It is written in the
order a simple instruction selector ([B1](b1-simplest-backend.md),
[C1](c1-instruction-selection.md)) produces: each square is added in as soon
as it exists.

```text
1  x  = load v[0]
2  xx = x * x
3  y  = load v[1]
4  yy = y * y
5  s  = xx + yy
6  z  = load v[2]
7  zz = z * z
8  t  = s + zz
9  store out, t
```

Run it on a made-up teaching machine. It starts at most one instruction per
cycle, **in order**: an instruction that is not ready holds up every
instruction behind it. A load, a multiply and an add each have a latency of 3
cycles; a store, 1. These numbers keep the arithmetic small; they come
from no real core. When an instruction must wait for an input, the
machine **stalls**: that cycle starts nothing.

In source order, `x` starts in cycle 0 and is ready in cycle 3, so `xx`
starts in cycle 3, and `y` behind it in cycle 4. `yy` must wait for `y` until
cycle 7, `s` for `yy` until cycle 10, and so on down the block. The store
starts in cycle 20 and completes in cycle 21. Nine instructions took 21
cycles, and 12 of those cycles started nothing.

Nothing forces that. The three loads read no computed value, so all three
could start at once, and each multiply could then start as soon as its own
load is ready. The next program builds the block, orders it by the method this
chapter develops, and prints both timelines:

--8<-- "includes/examples/backend/c6-scheduling/list_schedule.cpp.md"

The reordered block finishes in 14 cycles instead of 21, with 5 empty cycles
instead of 12. It runs the same nine instructions, each reading the same
inputs. The additions still compute `(x*x + y*y) + z*z`, in that grouping, so
the result is the same bits. Only the waiting moved.

## The dependence graph

To reorder safely, a scheduler first writes down which instructions must stay
in order. The result is a **dependence graph**: one node per instruction, and
an edge from `a` to `b` when `b` must not start until `a` has made progress.
Every edge points forward in the block, so the graph has no cycles: it is a
**directed acyclic graph**, a DAG.

When `b` reads a value that `a` computes, `b` cannot start until `a`'s result
is ready, so the edge carries a weight: the latency of `a`. This is a flow
dependence, called **read after write** (RAW) in hardware texts. Every edge of
the vector block is of this kind, and every one weighs 3 cycles, because every
instruction that produces a value has latency 3. Figure 1 draws the graph.

<figure class="vx-figure">
<svg viewBox="0 0 760 430" role="img" aria-label="Dependence graph of the squared-length block, with each node's priority" aria-describedby="c6-dag-desc">
<title id="c6-dag-title">Dependence graph of the squared-length block</title>
<desc id="c6-dag-desc">Nine nodes in five rows. Top row: the three loads x, y and z, with priorities 13, 13 and 10. Second row: the three multiplies xx, yy and zz, with priorities 10, 10 and 7; each is reached by an arrow from its own load. Third row: s = xx + yy, priority 7, reached from xx and yy. Fourth row: t = s + zz, priority 4, reached from s and zz. Bottom: the store, priority 1, reached from t. Every arrow stands for a latency of 3 cycles. The paths from x and from y through their multiplies, s, t and the store are highlighted: they are the critical paths, 13 cycles long.</desc>
<rect class="vx-box-accent" x="40" y="20" width="180" height="44" rx="4"/>
<text class="vx-mono" x="54" y="47">x = load v[0]</text>
<text class="vx-text-accent" x="206" y="47" text-anchor="end">13</text>
<rect class="vx-box-accent" x="290" y="20" width="180" height="44" rx="4"/>
<text class="vx-mono" x="304" y="47">y = load v[1]</text>
<text class="vx-text-accent" x="456" y="47" text-anchor="end">13</text>
<rect class="vx-box" x="540" y="20" width="180" height="44" rx="4"/>
<text class="vx-mono" x="554" y="47">z = load v[2]</text>
<text class="vx-text-muted" x="706" y="47" text-anchor="end">10</text>
<rect class="vx-box-accent" x="40" y="110" width="180" height="44" rx="4"/>
<text class="vx-mono" x="54" y="137">xx = x * x</text>
<text class="vx-text-accent" x="206" y="137" text-anchor="end">10</text>
<rect class="vx-box-accent" x="290" y="110" width="180" height="44" rx="4"/>
<text class="vx-mono" x="304" y="137">yy = y * y</text>
<text class="vx-text-accent" x="456" y="137" text-anchor="end">10</text>
<rect class="vx-box" x="540" y="110" width="180" height="44" rx="4"/>
<text class="vx-mono" x="554" y="137">zz = z * z</text>
<text class="vx-text-muted" x="706" y="137" text-anchor="end">7</text>
<rect class="vx-box-accent" x="165" y="200" width="180" height="44" rx="4"/>
<text class="vx-mono" x="179" y="227">s = xx + yy</text>
<text class="vx-text-accent" x="331" y="227" text-anchor="end">7</text>
<rect class="vx-box-accent" x="290" y="290" width="180" height="44" rx="4"/>
<text class="vx-mono" x="304" y="317">t = s + zz</text>
<text class="vx-text-accent" x="456" y="317" text-anchor="end">4</text>
<rect class="vx-box-accent" x="290" y="370" width="180" height="44" rx="4"/>
<text class="vx-mono" x="304" y="397">store out, t</text>
<text class="vx-text-accent" x="456" y="397" text-anchor="end">1</text>
<line class="vx-line" x1="130" y1="64" x2="130" y2="100"/>
<polygon class="vx-arrowhead" points="124,100 136,100 130,110"/>
<line class="vx-line" x1="380" y1="64" x2="380" y2="100"/>
<polygon class="vx-arrowhead" points="374,100 386,100 380,110"/>
<line class="vx-line" x1="630" y1="64" x2="630" y2="100"/>
<polygon class="vx-arrowhead" points="624,100 636,100 630,110"/>
<path class="vx-line" d="M130 154 C 130 176, 230 170, 230 190"/>
<polygon class="vx-arrowhead" points="224,190 236,190 230,200"/>
<path class="vx-line" d="M380 154 C 380 176, 280 170, 280 190"/>
<polygon class="vx-arrowhead" points="274,190 286,190 280,200"/>
<path class="vx-line" d="M255 244 C 255 266, 350 260, 350 280"/>
<polygon class="vx-arrowhead" points="344,280 356,280 350,290"/>
<path class="vx-line" d="M630 154 C 630 230, 420 230, 420 280"/>
<polygon class="vx-arrowhead" points="414,280 426,280 420,290"/>
<line class="vx-line" x1="380" y1="334" x2="380" y2="360"/>
<polygon class="vx-arrowhead" points="374,360 386,360 380,370"/>
<text class="vx-text-muted" x="560" y="360">each arrow: 3 cycles</text>
<text class="vx-text-muted" x="560" y="380">number: priority, the longest</text>
<text class="vx-text-muted" x="560" y="398">path to the end, in cycles</text>
</svg>
<figcaption>Figure 1. The dependence graph of the squared-length block. Each number is the node's priority: the length, in cycles, of the longest path from the start of that instruction to the end of the block. The highlighted nodes lie on a path of 13 cycles, the critical path, and no schedule can finish the block in fewer.</figcaption>
</figure>

The longest path through the graph, counting each edge's weight and the last
node's own latency, is the **critical path**. Here it runs from `x` (or `y`)
through its multiply, `s`, `t` and the store: 3 + 3 + 3 + 3 + 1 = 13 cycles. No
order can finish sooner, however many instructions the machine starts per
cycle, because each step on that path must wait for the one before. The
critical path is a lower bound on the block's time, the same way a loop's
dependency chain bounds a loop in
[P5](../optimize/p5-microarchitecture.md#three-bounds-on-a-loop).

The same idea gives each node a number. The **priority** of a node here is the
length of the longest path from its start to the end of the block: its own
latency plus the largest priority among the nodes that read its result. The
program computes it in one backward sweep, because every edge points forward
in the original order. A node with a large priority has a long chain of work
behind it, so delaying it delays the end.

??? check "Swap the latencies: loads now take 1 cycle and multiplies 5. Which nodes have the highest priority, and how long is the critical path?"

    Work up from the store: store 1, `t` 3 + 1 = 4, `s` 3 + 4 = 7, `zz`
    5 + 4 = 9, `xx` and `yy` 5 + 7 = 12, `z` 1 + 9 = 10, and `x` and `y`
    1 + 12 = 13. So `x` and `y` still lead, and the critical path is still
    13 cycles, now 1 + 5 + 3 + 3 + 1. The multiplies weigh more and the loads
    less, but the path through `s` is still the longest: the shape of the
    graph decides which nodes are critical as much as the latencies do.

## List scheduling

The method the program uses is **list scheduling**, the standard heuristic for
one block. It builds the schedule one cycle at a time. It keeps a **ready
list**: the instructions not yet scheduled whose inputs will all be ready in
the current cycle. Each cycle it takes the ready instruction with the highest
priority, schedules it, and moves to the next cycle; when the list is empty, the
cycle is a stall. On a machine that starts several instructions per cycle, it
takes up to that many from the list before moving on.

Gibbons and Muchnick described this shape in 1986 for a pipelined machine at
Hewlett-Packard: a pass after code generation that builds a DAG for each basic
block and chooses instructions from it heuristically, without looking ahead.
Earlier algorithms for reducing pipeline stalls had worst-case running times of
at least $O(n^4)$ for $n$ instructions; theirs is $O(n^2)$, and their abstract
reports that it reorders almost as effectively in practice[^gm86].

### The walk, cycle by cycle

Follow the program's output with Figure 1 beside you. Ties go to the
instruction that came first in the source.

| Cycle | Ready list (priority) | Starts | Why |
| --- | --- | --- | --- |
| 0 | `x` 13, `y` 13, `z` 10 | `x` | highest priority, first in the source |
| 1 | `y` 13, `z` 10 | `y` | |
| 2 | `z` 10 | `z` | `xx` needs `x`, ready in cycle 3 |
| 3 | `xx` 10 | `xx` | `yy` needs `y`, ready in cycle 4 |
| 4 | `yy` 10 | `yy` | |
| 5 | `zz` 7 | `zz` | |
| 6 | empty | nothing | `s` needs `yy`, ready in cycle 7 |
| 7 | `s` 7 | `s` | |
| 8, 9 | empty | nothing | `t` needs `s`, ready in cycle 10 |
| 10 | `t` 4 | `t` | |
| 11, 12 | empty | nothing | the store needs `t` |
| 13 | store 1 | store | complete in cycle 14 |

Fourteen cycles, and 13 is impossible on this machine: `x` and `y` both head
13-cycle paths, and only one can start in cycle 0. So this schedule is
optimal, but that is luck, not a guarantee. List scheduling is greedy, and on
other graphs a choice that looks best in one cycle costs more later, which is
why Gibbons and Muchnick call their method heuristic[^gm86].

Figure 2 draws both timelines on one scale.

<figure class="vx-figure">
<svg viewBox="0 0 760 200" role="img" aria-label="Timelines of the two orders on the teaching machine: 21 cycles in source order, 14 after list scheduling" aria-describedby="c6-gantt-desc">
<title id="c6-gantt-title">Two timelines of the same nine instructions</title>
<desc id="c6-gantt-desc">A cycle axis from 0 to 21. Upper row, source order: x at cycle 0, xx at 3, y at 4, yy at 7, s at 10, z at 11, zz at 14, t at 17 and the store at 20, done at 21; twelve cycles start nothing. Lower row, list schedule: x, y and z at cycles 0, 1 and 2, xx, yy and zz at 3, 4 and 5, s at 7, t at 10 and the store at 13, done at 14; five cycles start nothing. The cells of the lower row light up in order.</desc>
<text class="vx-text-muted" x="120" y="22" text-anchor="middle">0</text>
<text class="vx-text-muted" x="270" y="22" text-anchor="middle">5</text>
<text class="vx-text-muted" x="420" y="22" text-anchor="middle">10</text>
<text class="vx-text-muted" x="570" y="22" text-anchor="middle">15</text>
<text class="vx-text-muted" x="720" y="22" text-anchor="middle">20</text>
<text class="vx-text-muted" x="10" y="22">cycle</text>
<text class="vx-text" x="10" y="62">source</text>
<text class="vx-text" x="10" y="80">order</text>
<rect class="vx-box" x="105" y="44" width="630" height="44" rx="3"/>
<rect class="vx-box-strong" x="106" y="48" width="28" height="36" rx="3"/>
<text class="vx-mono" x="120" y="71" text-anchor="middle">x</text>
<rect class="vx-box-strong" x="196" y="48" width="28" height="36" rx="3"/>
<text class="vx-mono" x="210" y="71" text-anchor="middle">xx</text>
<rect class="vx-box-strong" x="226" y="48" width="28" height="36" rx="3"/>
<text class="vx-mono" x="240" y="71" text-anchor="middle">y</text>
<rect class="vx-box-strong" x="316" y="48" width="28" height="36" rx="3"/>
<text class="vx-mono" x="330" y="71" text-anchor="middle">yy</text>
<rect class="vx-box-strong" x="406" y="48" width="28" height="36" rx="3"/>
<text class="vx-mono" x="420" y="71" text-anchor="middle">s</text>
<rect class="vx-box-strong" x="436" y="48" width="28" height="36" rx="3"/>
<text class="vx-mono" x="450" y="71" text-anchor="middle">z</text>
<rect class="vx-box-strong" x="526" y="48" width="28" height="36" rx="3"/>
<text class="vx-mono" x="540" y="71" text-anchor="middle">zz</text>
<rect class="vx-box-strong" x="616" y="48" width="28" height="36" rx="3"/>
<text class="vx-mono" x="630" y="71" text-anchor="middle">t</text>
<rect class="vx-box-strong" x="706" y="48" width="28" height="36" rx="3"/>
<text class="vx-mono" x="720" y="71" text-anchor="middle">st</text>
<text class="vx-text" x="10" y="132">list</text>
<text class="vx-text" x="10" y="150">schedule</text>
<rect class="vx-box" x="105" y="114" width="420" height="44" rx="3"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 9"><rect class="vx-box-accent" x="106" y="118" width="28" height="36" rx="3"/><text class="vx-mono" x="120" y="141" text-anchor="middle">x</text></g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 9"><rect class="vx-box-accent" x="136" y="118" width="28" height="36" rx="3"/><text class="vx-mono" x="150" y="141" text-anchor="middle">y</text></g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 9"><rect class="vx-box-accent" x="166" y="118" width="28" height="36" rx="3"/><text class="vx-mono" x="180" y="141" text-anchor="middle">z</text></g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 9"><rect class="vx-box-accent" x="196" y="118" width="28" height="36" rx="3"/><text class="vx-mono" x="210" y="141" text-anchor="middle">xx</text></g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 9"><rect class="vx-box-accent" x="226" y="118" width="28" height="36" rx="3"/><text class="vx-mono" x="240" y="141" text-anchor="middle">yy</text></g>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 9"><rect class="vx-box-accent" x="256" y="118" width="28" height="36" rx="3"/><text class="vx-mono" x="270" y="141" text-anchor="middle">zz</text></g>
<g class="vx-seq" style="--vx-i: 6; --vx-n: 9"><rect class="vx-box-accent" x="316" y="118" width="28" height="36" rx="3"/><text class="vx-mono" x="330" y="141" text-anchor="middle">s</text></g>
<g class="vx-seq" style="--vx-i: 7; --vx-n: 9"><rect class="vx-box-accent" x="406" y="118" width="28" height="36" rx="3"/><text class="vx-mono" x="420" y="141" text-anchor="middle">t</text></g>
<g class="vx-seq" style="--vx-i: 8; --vx-n: 9"><rect class="vx-box-accent" x="496" y="118" width="28" height="36" rx="3"/><text class="vx-mono" x="510" y="141" text-anchor="middle">st</text></g>
<text class="vx-text-muted" x="740" y="104" text-anchor="end">done at 21: 12 empty cycles</text>
<text class="vx-text-accent" x="535" y="141">done at 14: 5 empty cycles</text>
<text class="vx-text-muted" x="105" y="186">each cell is one cycle; a gap is a cycle in which the machine waits for a result</text>
</svg>
<figcaption>Figure 2. The same nine instructions on the teaching machine (one instruction per cycle, in order, latency 3 except the store). Source order starts each multiply right behind its load and waits. The list schedule starts the three independent loads first, so later waits overlap with useful work. The gaps that remain in the lower row lie on the critical path, and no order removes them.</figcaption>
</figure>

### Your turn: two instructions per cycle

Change the machine so that it can start two instructions per cycle, with the
same latencies and the same priorities. Before you read on, schedule the block
by hand: fill in the cycle in which each of the nine instructions starts, and
the cycle in which the store completes. Then compare with the critical path.

??? check "With two instructions per cycle, when does each instruction start, and is the schedule optimal?"

    Cycle 0: `x` and `y`. Cycle 1: `z` (nothing else is ready). Cycle 3:
    `xx` and `yy`. Cycle 4: `zz`. Cycle 6: `s`. Cycle 9: `t`. Cycle 12: the
    store, complete in cycle 13. That equals the critical path of 13 cycles,
    so no schedule does better: the second slot per cycle let `x` and `y`
    start together, which removed the one cycle the single-issue schedule
    lost. Set `kIssueWidth = 2` in the example to check; its source-order
    timeline also improves, to 19 cycles, because only the slot for `y`
    behind `xx` opens up.

### Checking a schedule

A schedule is correct when every instruction appears once and starts no
earlier than each input's start plus that input's latency. The check knows
nothing about priorities, so it can test a scheduler it did not come from; the
example runs it on both orders, as [C3](c3-linear-scan.md) checked an
allocation independently of the allocator.

## Where the edges come from on a real machine

A scheduler for real machine code must find three more kinds of edge, and a
missing edge produces wrong answers, not slow ones.

**Register reuse.** After register allocation, two unrelated values may share a
physical register. Then a later instruction that writes the register must stay
after every earlier instruction that reads the old value (an anti dependence,
**write after read**, WAR) and after the earlier write (an output dependence,
**write after write**, WAW). Neither edge carries a value, so neither needs a
full latency, but both fix an order. These are **false dependences**: they
come from the choice of registers, not from the computation. The next section
shows one in real compiler output.

**Memory.** A store and a later load of one address form a flow dependence
through memory; two stores to it, an output dependence. When the compiler
cannot prove two addresses differ, it must add the edge anyway, and proving
them different is alias analysis
([O9](../optimize/o9-alias-analysis.md#one-store-one-load)). Vortex helps:
while `&mut c` is live, `c` may be used only through it, a rule the compiler
checks, which makes code generation's no-overlap assumption "a checked
guarantee" ([record 41](../decisions/references.md#d41)). So in the stage 10
kernel a load from `a` may move past a store through `c`.

**Control and hidden state.** A block scheduler never moves an instruction
across a branch. In Vortex a bounds check ends in a branch to the
runtime-error path ([record 12](../decisions/arrays.md#d12)), so the load it
guards starts a later block and cannot rise above its check. Instructions
also depend on state no operand names: a `cmp` writes the condition flags and
a `b.lo` reads them ([A2](a2-aarch64-assembly.md#flags-and-conditions)), so no
other flag-setting instruction may come between them.

??? check "After allocation, a block contains `ldr w8, [x0]`, then `add w9, w8, #1`, then `ldr w8, [x1]`. May the second load move above the add?"

    No. The add reads `w8`, and the second load writes `w8`: a write after
    read. Moved above the add, the load would overwrite `w8` before the add
    read it, and the add would compute from the wrong value. With a different
    register for the second load, the edge disappears and the load may move
    freely. That is why the choice of registers can take choices away from
    a scheduler.

## Scheduling against register pressure

The fast schedule of the vector block keeps more values alive at once. In
source order, at most two values are live at any point. After the list
schedule starts all three loads, `x`, `y` and `z` are live together, and right
after `zz` starts so are `xx`, `yy` and `zz`: three registers instead of two.
The number of values live at a point is the **register pressure** there, and
by [C2](c2-liveness.md#interference) it is the number of registers that point
needs.

A larger block makes the cost plain. The next example squares eight loaded
values and stores each square, like the body of a loop unrolled eight times,
and measures both cycles and the most values live at once:

--8<-- "includes/examples/backend/c6-scheduling/pressure.cpp.md"

| Schedule | Cycles | Most values live |
| --- | --- | --- |
| Source order | 56 | 1 |
| List schedule, latency first | 24 | 8 |
| List schedule, at most 4 live | 24 | 4 |
| List schedule, at most 2 live | 30 | 2 |

The latency-first schedule starts all eight loads, because every load has the
same, highest priority, and then needs eight registers. The capped schedule
reaches the same 24 cycles with four: once four loads are in flight, the first
multiply is ready, and starting it frees a register as fast as another load
would take one. Only the cap of two costs time. Hiding latency costs registers:
each instruction started early holds its result until something reads it.

That trade-off is the **phase-ordering problem** between scheduling and
register allocation, and each order of the two passes pays for it differently.

- **Schedule first.** The scheduler sees only the true dependences and can
  move freely. But it can raise pressure beyond the registers the machine has,
  and then the allocator spills ([C5](c5-spilling.md)), adding loads and stores
  that may cost more than the stalls saved.
- **Allocate first.** Pressure is settled, but reusing registers has added the
  false dependences of the last section, and they forbid many of the moves the
  scheduler would have made.

Compilers answer with both passes and with schedulers that watch pressure.
Goodman and Hsu proposed a prepass scheduler that tracks free registers and
switches between reducing pipeline delays and minimizing register use[^gh88].
GCC schedules before allocation (`-fschedule-insns`) and after it
(`-fschedule-insns2`), and `-fsched-pressure` makes the first pass watch
pressure; its manual notes that x86 turns the first pass off at every level
and AArch64 below `-O3`[^gcc-sched].

LLVM's generic machine scheduler, which can run before allocation, compares
two candidates early on by whether they would push pressure over the target's
limit, then by critical pressure, and only later by latency[^llvm-misched].
[E3](e3-llvm-allocator-scheduler-mc.md#the-machine-scheduler-choosing-an-order-not-a-value)
places it in LLVM's pipeline.

Figure 3 shows the choice in real output: the vector block compiled by `llc`
18.1.8 for two processors (from `len2.ll`, shown in the next section; Apple
M4 Pro, 2026-09-24). With the machine scheduler turned off
(`-enable-misched=false`), `llc` wrote the right-hand order for the A53 as
well, so the early load on the left is the scheduler's work.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Two llc schedules of the squared-length block: three registers and an early load, or two registers and a false dependence" aria-describedby="c6-war-desc">
<title id="c6-war-title">One block, two register choices</title>
<desc id="c6-war-desc">Left, llc for cortex-a53: ldp s0, s1 loads x and y; ldr s2 loads z immediately after; then the multiplies and adds. z has its own register, s2, so its load moves up to the top. Right, llc for apple-m1: ldp s0, s1; fmul s0; fmul s1; fadd s0, s0, s1; then ldr s1 loads z into s1, reusing it. An arrow from the fadd that reads s1 to the ldr that writes s1 marks a write-after-read dependence: the load of z cannot move above the add.</desc>
<text class="vx-text" x="30" y="24">-mcpu=cortex-a53 (in order)</text>
<rect class="vx-box" x="30" y="36" width="320" height="250" rx="4"/>
<text class="vx-mono" x="46" y="64">ldp  s0, s1, [x0]</text>
<rect class="vx-box-accent" x="40" y="74" width="300" height="26" rx="3"/>
<text class="vx-mono" x="46" y="92">ldr  s2, [x0, #8]</text>
<text class="vx-mono" x="46" y="120">fmul s0, s0, s0</text>
<text class="vx-mono" x="46" y="148">fmul s1, s1, s1</text>
<text class="vx-mono" x="46" y="176">fadd s0, s0, s1</text>
<text class="vx-mono" x="46" y="204">fmul s1, s2, s2</text>
<text class="vx-mono" x="46" y="232">fadd s0, s0, s1</text>
<text class="vx-mono" x="46" y="260">str  s0, [x1]</text>
<text class="vx-text-muted" x="200" y="120">z gets s2,</text>
<text class="vx-text-muted" x="200" y="138">so its load</text>
<text class="vx-text-muted" x="200" y="156">moves to the top</text>
<text class="vx-text" x="410" y="24">-mcpu=apple-m1 (out of order)</text>
<rect class="vx-box" x="410" y="36" width="320" height="250" rx="4"/>
<text class="vx-mono" x="426" y="64">ldp  s0, s1, [x0]</text>
<text class="vx-mono" x="426" y="92">fmul s0, s0, s0</text>
<text class="vx-mono" x="426" y="120">fmul s1, s1, s1</text>
<rect class="vx-box-strong" x="420" y="130" width="160" height="26" rx="3"/>
<text class="vx-mono" x="426" y="148">fadd s0, s0, s1</text>
<rect class="vx-box-bad" x="420" y="158" width="160" height="26" rx="3"/>
<text class="vx-mono" x="426" y="176">ldr  s1, [x0, #8]</text>
<text class="vx-mono" x="426" y="204">fmul s1, s1, s1</text>
<text class="vx-mono" x="426" y="232">fadd s0, s0, s1</text>
<text class="vx-mono" x="426" y="260">str  s0, [x1]</text>
<path class="vx-flow" d="M580 143 C 640 143, 640 171, 588 171"/>
<polygon class="vx-arrowhead" points="592,165 582,171 592,177"/>
<text class="vx-text-accent" x="650" y="150">reads s1,</text>
<text class="vx-text-accent" x="650" y="168">then s1 is</text>
<text class="vx-text-accent" x="650" y="186">rewritten</text>
</svg>
<figcaption>Figure 3. The same IR compiled for two processors. For the in-order Cortex-A53 model, the scheduler hoisted the third load and gave <code>z</code> a register of its own. For the <code>apple-m1</code> model it left the load late, and the allocator reused <code>s1</code>: the load of <code>z</code> now writes a register the add before it reads, a write-after-read edge that pins it there. Both listings also pair the first two loads into one <code>ldp</code>, a rewrite that C7 studies.</figcaption>
</figure>

## What the core already does

Many cores are **out of order**: they decode in program order, but start
each instruction from a window of waiting ones as soon as its inputs and a
unit are ready, and they rename registers so that false dependences vanish
([P5](../optimize/p5-microarchitecture.md#out-of-order) describes the
hardware). Such a core schedules at run time, over a window far larger than
nine instructions.

LLVM records the difference in each processor's scheduling model. The field
`MicroOpBufferSize` counts the micro-operations a processor can buffer for
out-of-order execution: 0 means an instruction that is not ready is not
considered that cycle ("latency is paramount", says the header), and more
than 1 means out of order[^llvm-mcsched]. The Cortex-A53 model sets it to 0,
noting that the A53 is in order[^llvm-a53]; the model behind `apple-m1` sets
it to 192, based on the reorder buffer[^llvm-cyclone].

The next example writes both orders of the vector block in assembly and checks
that they return the same bits as each other and as a C++ reference:

--8<-- "includes/examples/backend/c6-scheduling/len2_orders.cpp.md"

It prints:

```text
--8<-- "examples/backend/c6-scheduling/len2_orders.expected"
```

The two `llc` listings in Figure 3 came from this IR, with the commands in
its comment:

--8<-- "includes/examples/backend/c6-scheduling/len2.ll.md"

Now give the four sequences to llvm-mca, the machine code analyzer
[E3](e3-llvm-allocator-scheduler-mc.md#watching-a-schedule-without-running-it-llvm-mca)
introduces, which simulates code on a scheduling model. For an in-order model
it issues each instruction once its operand registers are available and its
resources free, up to the issue width[^llvm-mca]. The sequences are the bodies
of `len2_source` and `len2_listed` and the two `llc` listings, without `ret`,
run once (`-iterations=1`; llvm-mca 18.1.8, Apple M4 Pro, 2026-09-24):

| Instruction order | `-mcpu=cortex-a53`, cycles | `-mcpu=apple-m1`, cycles |
| --- | --- | --- |
| Source order (`len2_source`) | 39 | 23 |
| Loads first (`len2_listed`) | 28 | 23 |
| `llc` for `cortex-a53` | 28 | 23 |
| `llc` for `apple-m1` | 34 | 23 |

On the in-order model, the order decides the time: loading early saves 11 of 39
cycles, and `llc`'s own schedule for that core matches the hand-made one. On
the out-of-order model, all four orders take the same 23 cycles. The core
starts the loads early by itself, whatever the order in the file, and renames
`s1` so that the false dependence in the `apple-m1` listing costs nothing. That fits
what `llc` did for `apple-m1`: on that model, hoisting the load buys nothing,
and leaving it late saves a register.

These are model predictions, not measurements. llvm-mca models neither the
front end nor branch prediction, and its load unit knows nothing of the
caches[^llvm-mca]
([P5](../optimize/p5-microarchitecture.md#when-the-model-and-the-machine-disagree)
compares models with a machine). Time real code as
[P1](../optimize/p1-measure-first.md) describes before you believe a speedup.

So block scheduling matters most on in-order cores, and on VLIW machines such
as the Warp processors of Lam's paper, where the compiler decides what runs in
each cycle[^lam88]; GPUs hide latency mostly by switching threads
([G5](../gpu/g5-occupancy.md#how-many-warps-are-enough-littles-law)). On a large
out-of-order core, a block scheduler's choices about registers can matter more
than its choices about latency. One limit binds every core: no instruction
starts before its inputs exist. That limit bounds the next loop.

## Loops: the recurrence sets the pace

Here is the stage 10 kernel, as C3 showed it. Its `k` loop runs only three
times for these shapes, but the same loop over larger shapes is where the time
goes:

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

Its inner loop body, in AArch64 assembly without the bounds checks, is two
loads, a multiply, an add, and the loop bookkeeping. Here it is as a text file
for llvm-mca (not an example the harness assembles):

```gas
loop:
        ldr     s0, [x0, x2, lsl #2]    // a[row, k]
        ldr     s1, [x1, x3, lsl #2]    // b[k, column]
        fmul    s1, s0, s1              // rounded product
        fadd    s4, s4, s1              // sum += product, rounded
        add     x2, x2, #1              // next k along a's row
        add     x3, x3, x5              // next row of b
        cmp     x2, x6
        b.ne    loop
```

Within one trip, a block scheduler has little to do: the loads come first
already, and the multiply and add each need the instruction before. The cost
lies between trips. Each trip's `fadd` reads `s4`, the sum the previous trip's
`fadd` wrote. That edge runs from one iteration to the next, a **loop-carried
dependence**, and it closes a cycle in the graph: `fadd` to `fadd`, one
iteration apart. A cycle of dependences through a loop is a **recurrence**.

Across iterations, the loads and the multiply of trip $k+1$ need nothing from
trip $k$, so a machine may start them early. The `fadd` of trip $k+1$ cannot
start until the `fadd` of trip $k$ has finished. Figure 4 draws what follows:
the independent work runs ahead, and the additions form a queue.

<figure class="vx-figure">
<svg viewBox="0 0 760 250" role="img" aria-label="Four iterations of the one-accumulator loop: loads and multiplies run ahead, additions wait for each other" aria-describedby="c6-rec-desc">
<title id="c6-rec-title">A recurrence sets the pace of a loop</title>
<desc id="c6-rec-desc">Four rows, iterations 0 to 3, on a cycle axis from 0 to 24. In each row a load bar starts at the iteration number and lasts 4 cycles, and a multiply bar follows it for 4 cycles, so the multiplies finish at cycles 8, 9, 10 and 11. The add bars, highlighted, start at cycles 8, 12, 16 and 20: each waits for the previous iteration's add to finish, and arrows join the end of each add to the start of the next. The gap between a finished multiply and its add grows by 3 cycles per iteration.</desc>
<text class="vx-text-muted" x="10" y="22">cycle</text>
<text class="vx-text-muted" x="120" y="22" text-anchor="middle">0</text>
<text class="vx-text-muted" x="220" y="22" text-anchor="middle">4</text>
<text class="vx-text-muted" x="320" y="22" text-anchor="middle">8</text>
<text class="vx-text-muted" x="420" y="22" text-anchor="middle">12</text>
<text class="vx-text-muted" x="520" y="22" text-anchor="middle">16</text>
<text class="vx-text-muted" x="620" y="22" text-anchor="middle">20</text>
<text class="vx-text-muted" x="720" y="22" text-anchor="middle">24</text>
<text class="vx-text" x="10" y="57">trip 0</text>
<rect class="vx-box" x="120" y="40" width="100" height="26" rx="3"/>
<text class="vx-mono" x="170" y="58" text-anchor="middle">ldr</text>
<rect class="vx-box-strong" x="220" y="40" width="100" height="26" rx="3"/>
<text class="vx-mono" x="270" y="58" text-anchor="middle">fmul</text>
<rect class="vx-box-accent" x="320" y="40" width="100" height="26" rx="3"/>
<text class="vx-mono" x="370" y="58" text-anchor="middle">fadd</text>
<text class="vx-text" x="10" y="107">trip 1</text>
<rect class="vx-box" x="145" y="90" width="100" height="26" rx="3"/>
<text class="vx-mono" x="195" y="108" text-anchor="middle">ldr</text>
<rect class="vx-box-strong" x="245" y="90" width="100" height="26" rx="3"/>
<text class="vx-mono" x="295" y="108" text-anchor="middle">fmul</text>
<rect class="vx-box-accent" x="420" y="90" width="100" height="26" rx="3"/>
<text class="vx-mono" x="470" y="108" text-anchor="middle">fadd</text>
<text class="vx-text" x="10" y="157">trip 2</text>
<rect class="vx-box" x="170" y="140" width="100" height="26" rx="3"/>
<text class="vx-mono" x="220" y="158" text-anchor="middle">ldr</text>
<rect class="vx-box-strong" x="270" y="140" width="100" height="26" rx="3"/>
<text class="vx-mono" x="320" y="158" text-anchor="middle">fmul</text>
<rect class="vx-box-accent" x="520" y="140" width="100" height="26" rx="3"/>
<text class="vx-mono" x="570" y="158" text-anchor="middle">fadd</text>
<text class="vx-text" x="10" y="207">trip 3</text>
<rect class="vx-box" x="195" y="190" width="100" height="26" rx="3"/>
<text class="vx-mono" x="245" y="208" text-anchor="middle">ldr</text>
<rect class="vx-box-strong" x="295" y="190" width="100" height="26" rx="3"/>
<text class="vx-mono" x="345" y="208" text-anchor="middle">fmul</text>
<rect class="vx-box-accent" x="620" y="190" width="100" height="26" rx="3"/>
<text class="vx-mono" x="670" y="208" text-anchor="middle">fadd</text>
<path class="vx-flow" d="M420 66 L420 84"/>
<polygon class="vx-arrowhead" points="414,82 426,82 420,90"/>
<path class="vx-flow" d="M520 116 L520 134"/>
<polygon class="vx-arrowhead" points="514,132 526,132 520,140"/>
<path class="vx-flow" d="M620 166 L620 184"/>
<polygon class="vx-arrowhead" points="614,182 626,182 620,190"/>
<text class="vx-text-muted" x="120" y="240">loads and multiplies may start every cycle; each add waits 4 cycles for the last</text>
</svg>
<figcaption>Figure 4. Four trips of the one-accumulator loop, with every latency 4 cycles to keep the drawing simple. Nothing stops the loads and multiplies of later trips from starting early, and an out-of-order core starts them. The additions cannot overlap: each reads the sum the one before it writes. However the instructions are ordered, the loop completes one trip per addition latency.</figcaption>
</figure>

A compiler can overlap iterations itself. **Software pipelining** rewrites a
loop so that one pass through the new body works on several original
iterations at different stages: the loads of trip $k+2$, the multiply of trip
$k+1$, the add of trip $k$. Lam describes it as starting iterations at a
constant interval before earlier ones complete[^lam88]. Rau's **modulo
scheduling** is a framework for such algorithms[^rau94]; LLVM's
`MachinePipeliner` implements one, Swing Modulo Scheduling, and GCC another
behind `-fmodulo-sched`[^llvm-swp][^gcc-sched].

The number such a schedule tries to minimize is the **initiation interval**
(II): the cycles between the starts of two consecutive iterations. Two lower
bounds limit it, and LLVM's pipeliner starts its search at the larger of
them[^llvm-swp]:

- The **resource bound** (ResMII): if an iteration needs $u$ operations of one
  kind and the machine has $n$ units for them, no II below $\lceil u/n \rceil$
  can work.
- The **recurrence bound** (RecMII): for each cycle of dependences, the total
  latency around it, divided by the number of iterations it spans, rounded up.
  LLVM's code states it as the smallest II satisfying
  $\text{delay}(c) - \text{II} \times \text{distance}(c) \le 0$ for every
  cycle $c$[^llvm-swp].

The next program computes both bounds for the dot-product loop on a made-up
machine with two load units, two floating-point units and an add latency of 4,
the latency of the scalar `fadd` in LLVM's `apple-m1` model[^llvm-cyclone].
The "columns" rows compute that many outputs per trip, each summed in its own
unchanged order:

--8<-- "includes/examples/backend/c6-scheduling/recurrence.cpp.md"

| Loop | ResMII | RecMII | II | Cycles per output |
| --- | --- | --- | --- | --- |
| 1 column | 1 | 4 | 4 | 4.00 |
| 4 columns | 4 | 4 | 4 | 1.00 |
| 8 columns | 8 | 4 | 8 | 1.00 |

With one accumulator, the units could start a trip every cycle, but the
recurrence allows one every 4. The loop is **latency-bound**: its speed is set
by the latency of one instruction, not by how much hardware it uses. llvm-mca
agrees on the real body above. For `-mcpu=apple-m1` it reported 412 cycles
for 100 trips and 4012 for 1000, 4 cycles per trip, while its "Block
RThroughput", the cycles per trip in the absence of loop-carried
dependences[^llvm-mca], was 3.0 (llvm-mca 18.1.8, Apple M4 Pro, 2026-09-24).

So the naive matrix multiplication loop is latency-bound, and no scheduler
fixes it. A block scheduler, a software pipeliner and an out-of-order core all
reorder instructions within one dependence graph, and the recurrence is part
of the graph. Only changing the graph removes it, in one of two ways:

- **Change the arithmetic.** Split `sum` into four partial sums and add them at
  the end. The graph now has four short recurrences, but the additions are
  grouped differently, so the rounding changes, and
  [record 56](../decisions/numbers.md#d56) forbids a Vortex compiler to do it
  on its own. [P10](../optimize/p10-vectorization.md#reductions-ordered-or-reassociated)
  follows the bits.
- **Change the loop nest.** Compute several outputs in one trip, as the
  "4 columns" row does. Each `c[row, column]` is still summed in `k` order with
  the same roundings, so every bit is unchanged, and the four recurrences run
  side by side. That transformation, unroll-and-jam, belongs to the middle end
  ([P7](../optimize/p7-loop-transformations.md#unroll-and-jam-and-register-blocks));
  [P5](../optimize/p5-microarchitecture.md#one-loop-three-models-three-answers)
  ran the same shape through llvm-mca's `apple-m1` model: 4 cycles per trip,
  for four outputs.

The "8 columns" row shows where that stops paying: once the floating-point
units are full, the resource bound takes over, and more columns only add
registers. [P12](../optimize/p12-fast-gemm.md#the-register-blocked-micro-kernel)
sizes the register block of a fast matrix multiplication by exactly this
balance.

??? check "A loop body updates two accumulators, `p = p * r` (a multiply, latency 4) and `q = q + p` (an add, latency 3), where the add reads the `p` of the same trip. What is RecMII?"

    Two recurrences. `p` feeds itself through one multiply per trip: 4 / 1,
    so 4. `q` feeds itself through one add: 3 / 1, so 3. The edge from `p`
    to `q` lies on no cycle, so it adds no bound. RecMII is the larger, 4:
    the multiply chain sets the pace, and reordering the add cannot change
    that.

## For Vortex

!!! vortex "Exercise"

    **Build a list scheduler for straight-line blocks, and a checker for it.**
    In your own back end, after instruction selection and before register
    allocation, build the dependence graph of each basic block: flow edges
    weighted by a latency table you write for your target, memory edges
    between any store and any other memory access your compiler cannot prove
    apart, and edges that keep flag writers and flag readers together. Then
    schedule each block with list scheduling and a critical-path priority.
    Write the checker as separate code: given the graph and a finished order,
    it confirms that the order is a permutation of the block and that every
    edge points forward. Keep a switch that turns the scheduler off.

    Take your latencies from a documented source, such as the scheduling
    model LLVM uses for your processor or the vendor's optimization guide,
    and record which source and version each number came from.

    **Not yet.** Do not schedule across branches, and do not move any
    instruction past a bounds check or any other branch to the runtime-error
    path. Do not build software pipelining or a pressure-aware scheduler;
    first measure whether plain list scheduling raises your spill count.
    Do not split accumulators or regroup any floating-point operation: the
    scheduler may only reorder, never rewrite ([record 56](../decisions/numbers.md#d56)).

    **Done when** three things hold. The checker accepts every block your
    test suite compiles, and it rejects a deliberately broken scheduler that
    swaps one dependent pair. The programs' outputs, including the
    [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for)
    kernel's printed results, are identical bit for bit with the scheduler on
    and off. And for at least five blocks from your compiler's output, llvm-mca
    run with `-mcpu=cortex-a53` and with `-mcpu=apple-m1` reports no more
    cycles for the scheduled block than for the unscheduled one; fill in the
    table below with the machine, the llvm-mca version and the date. A block
    that got slower is a bug report against your latency table or your
    priority, not a number to hide.

    | Block | Cortex-A53 model, off | on | apple-m1 model, off | on |
    | --- | --- | --- | --- | --- |
    | | | | | |

    **Then write a short design note** for the stage 10 kernel: which
    transformations could remove the recurrence bound of its inner loop
    without changing a single bit of `c`, which ones would change bits and
    so need an explicit opt-in, and which compiler phase each belongs to.

## Key ideas

!!! recap "You can now answer"

    - **What may an instruction scheduler change, and what may it not?** The order of instructions that do not depend on each other; never which instructions run, what they read, or how the arithmetic is grouped.
    - **What does an edge of the dependence graph weigh, and what is the critical path?** The latency of the instruction the edge leaves; the longest weighted path through the block, a lower bound on its time.
    - **How does list scheduling choose?** Each cycle, among the instructions whose inputs are ready, it starts the one with the longest path to the end of the block, and it stalls when none is ready.
    - **Why do scheduling and register allocation fight?** Scheduling early work raises the number of values live at once, which can force spills, while allocating first reuses registers and adds false dependences that block reordering.
    - **When does reordering a block matter most?** On in-order cores, where a stalled instruction holds up the rest; an out-of-order core starts ready work itself and renames registers, so the order in the file matters much less.
    - **Why is the stage 10 inner loop latency-bound?** Each trip's `fadd` reads the sum the last one wrote, a recurrence that allows one trip per addition latency whatever the schedule.
    - **What can remove that bound without changing a bit?** Computing several outputs per trip (unroll-and-jam), each summed in its own order; splitting one sum into partial sums changes the rounding and is forbidden by default.

## Where this comes back

!!! next "You will use this again in"

    - [C7. Peephole optimization](c7-peephole.md): *pairing loads into `ldp`*, *rewrites that must respect the same dependences*
    - [E2. Describing a target](e2-describing-a-target.md#scheduling-models-one-instruction-several-machines): *scheduling models*, *latency tables*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md#the-machine-scheduler-choosing-an-order-not-a-value): *the machine scheduler*, *llvm-mca*
    - [P7. Loop transformations](../optimize/p7-loop-transformations.md#unroll-and-jam-and-register-blocks): *unroll-and-jam*, *independent recurrences*
    - [P10. Vectorization](../optimize/p10-vectorization.md#reductions-ordered-or-reassociated): *ordered reductions*, *reassociation*
    - [P12. Anatomy of a fast GEMM](../optimize/p12-fast-gemm.md#the-register-blocked-micro-kernel): *latency times units*, *register pressure*
    - [G5. Occupancy and latency hiding](../gpu/g5-occupancy.md#how-many-warps-are-enough-littles-law): *hiding latency with other work*

## Sources and further reading

Read Gibbons and Muchnick first: six pages, and the whole of block scheduling
in its classic form. Then read the comment at the top of LLVM's
`MachinePipeliner.cpp` and the `tryCandidate` function in
`MachineScheduler.cpp`, with llvm-mca's `-timeline` view open on this chapter's
two orders. Cooper and Torczon's *Engineering a Compiler* (chapter 12) and
Appel's *Modern Compiler Implementation* (the chapter on scheduling and
pipelining) are the textbook treatments.

[^gm86]: Philip B. Gibbons and Steven S. Muchnick, "Efficient instruction scheduling for a pipelined architecture", *Proceedings of the 1986 SIGPLAN Symposium on Compiler Construction*, pp. 11 to 16: the abstract (a DAG per basic block, scheduled heuristically in a pass after code generation; $O(n^2)$ worst case against at least $O(n^4)$ for earlier algorithms). <https://doi.org/10.1145/12276.13312>
[^gh88]: James R. Goodman and Wei-Chung Hsu, "Code scheduling and register allocation in large basic blocks", *Proceedings of the 2nd International Conference on Supercomputing* (ICS), 1988: the abstract (integrated prepass scheduling that tracks available registers). <https://doi.org/10.1145/55364.55407>
[^lam88]: Monica Lam, "Software pipelining: an effective scheduling technique for VLIW machines", *Proceedings of the ACM SIGPLAN 1988 Conference on Programming Language Design and Implementation* (PLDI): the abstract. <https://doi.org/10.1145/53990.54022>
[^rau94]: B. Ramakrishna Rau, "Iterative modulo scheduling: an algorithm for software pipelining loops", *Proceedings of the 27th Annual International Symposium on Microarchitecture* (MICRO-27), 1994: the abstract. <https://doi.org/10.1145/192724.192731>
[^gcc-sched]: Free Software Foundation, *Using the GNU Compiler Collection*, "Options That Control Optimization": `-fschedule-insns`, `-fschedule-insns2`, `-fsched-pressure` and `-fmodulo-sched`. <https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html>
[^llvm-misched]: LLVM Project, `llvm/lib/CodeGen/MachineScheduler.cpp`, release/18.x: the file comment (scheduling before register allocation) and `GenericScheduler::tryCandidate`, which compares register-pressure excess and critical pressure before latency. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/CodeGen/MachineScheduler.cpp>
[^llvm-swp]: LLVM Project, `llvm/lib/CodeGen/MachinePipeliner.cpp`, release/18.x: the file comment (Swing Modulo Scheduling, run before register allocation), `calculateRecMII` and `setMII` (MII is the larger of ResMII and RecMII). <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/CodeGen/MachinePipeliner.cpp>
[^llvm-mcsched]: LLVM Project, `llvm/include/llvm/MC/MCSchedule.h`, release/18.x: the comment on `MicroOpBufferSize` and `isOutOfOrder`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/include/llvm/MC/MCSchedule.h>
[^llvm-a53]: LLVM Project, `llvm/lib/Target/AArch64/AArch64SchedA53.td`, release/18.x: `CortexA53Model` (`MicroOpBufferSize = 0`, in order; `IssueWidth = 2`). <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Target/AArch64/AArch64SchedA53.td>
[^llvm-cyclone]: LLVM Project, `llvm/lib/Target/AArch64/AArch64SchedCyclone.td`, release/18.x: `CycloneModel` (`MicroOpBufferSize = 192`, based on the reorder buffer), the three floating-point and vector pipes, and the 4-cycle `FADDSrr`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Target/AArch64/AArch64SchedCyclone.td>; `AArch64.td` in the same branch maps `apple-m1` to `CycloneModel`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Target/AArch64/AArch64.td>
[^llvm-mca]: LLVM Project, "llvm-mca - LLVM Machine Code Analyzer": the timeline view, in-order issue, and the limits of the model (no front end, no branch prediction, no cache hierarchy in the load/store unit). <https://llvm.org/docs/CommandGuide/llvm-mca.html>
