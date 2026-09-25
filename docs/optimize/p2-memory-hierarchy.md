# P2. The memory hierarchy

<p class="page-intro">The same loop over the same array can run several times faster or slower depending only on the order in which it visits memory. This chapter explains why: what a cache stores, how an address finds its place in one, why a miss happens, and how to measure the hierarchy of your own machine. Every later rung of the Vortex matmul ladder is argued in these terms.</p>

<p class="vx-meta" markdown="1">Level: Foundations · Reading time: about 40 minutes · Builds on: [P1. Measure first](p1-measure-first.md), [Build v0.1, stage 10](../compiler/guide/stage-10-matrix-multiplication.md)</p>

???+ remember "Before you start, remember"

    ??? question "In what order does a Vortex `[f32; rows, columns]` array store its elements?"

        Contiguously, with no gaps, in row-major order: the last index varies
        fastest, so `a[0, 0]` and `a[0, 1]` sit next to each other in memory,
        and `a[1, 0]` sits one whole row further on.

        Introduced in [Build v0.1, stage 10](../compiler/guide/stage-10-matrix-multiplication.md),
        decided in [Arrays, record 43](../decisions/arrays.md#d43).

    ??? question "What is a register?"

        A small, fast storage slot inside the processor. Instructions
        mostly work on values held in registers, not directly on memory.

        Introduced in [Build v0.1, stage 6](../compiler/guide/stage-6-first-machine-code.md#words-for-this-stage).

    ??? question "Why is one timed run not a measurement?"

        It is one sample from a noisy process. The operating system, the
        clock speed and the choice of core all move it, and one sample shows
        none of that spread.

        Introduced in [P1. Measure first](p1-measure-first.md#a-stopwatch-is-not-a-measurement).

    ??? question "What may a compiler do to a timing loop whose result is never used?"

        Delete it. Doing work and throwing the answer away looks the same from
        outside as not doing it, so the as-if rule allows the loop to vanish
        and the clock to time nothing.

        Introduced in [P1. Measure first](p1-measure-first.md#keeping-the-compiler-from-helping-too-much).

!!! goals "In this chapter"

    - Explain why the time a load takes depends on where its value currently lives, using a published latency table.
    - Split an address into tag, set and offset, and predict when two addresses compete for the same place in a cache.
    - Classify a miss as compulsory, capacity or conflict, and say which kind of locality a loop relies on.
    - Read the cache facts your machine reports, and know which facts it does not report.
    - Measure the latency staircase of your own machine with a pointer chase, and read the cache sizes off it.

## A kernel that is slow for a reason you cannot see in its arithmetic

Look again at the multiply function from stage 10:

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

Count the arithmetic. For an `m` by `k` matrix times a `k` by `n` one, the
function does `m * n * k` multiplications and as many additions. Visiting the
same products in another order, or in blocks, changes none of those counts.

Yet the time does change. Ulrich Drepper multiplied two 1000 by 1000 matrices
of `double` on an Intel Core 2 in 2007. Copying the second matrix into a
transposed temporary first, so that the inner loop reads both inputs along
rows, brought the run down to 23.4% of the original cycles, copy included.
Working on small sub-matrices instead brought it to 17.3%.[^drepper] The
arithmetic was the same in all three versions. Where the numbers were, when
the arithmetic needed them, was not.

That "where" is the **memory hierarchy**. A computer does not have one kind
of memory but several, stacked from small and fast to large and slow. At the
top are the registers. Below them are one or more **caches**: small memories
on the processor that keep copies of recently used parts of main memory, so
that the next use of the same data is quick. At the bottom is main memory,
DRAM, which holds everything. On a load-store architecture such as AArch64,
arithmetic instructions read only registers, so every value in memory has to
travel up the stack into a register before it can take part in a sum.

How much the level matters is a matter of record. Drepper lists the access
times Intel gave for the Pentium M, in processor cycles:[^drepper]

| Where the value is | Cycles to reach it (Pentium M) |
| --- | --- |
| Register | 1 or fewer |
| L1 data cache | about 3 |
| L2 cache | about 14 |
| Main memory | about 240 |

These numbers belong to one old processor, and you will measure your own
before the end of the chapter. The ratios are the lesson: the bottom of the
stack is two orders of magnitude further away than the top. A loop that finds
its data near the top runs at the speed of its arithmetic. A loop that keeps
reaching the bottom runs at the speed of memory, whatever its arithmetic.

Now read `multiply` with this in mind. The inner loop reads `a[row, k]` and
`b[k, column]` as `k` runs over the shared dimension. Arrays are row-major, so
`a[row, k]` walks along a row: consecutive `k`, consecutive addresses. But
`b[k, column]` walks down a column: each step of `k` jumps a whole row of `b`
ahead in memory. The source treats `a` and `b` alike. Memory does not, and the
rest of this chapter says exactly why.

## Cache lines and why order matters

A cache never fetches one number on its own. It fetches the aligned,
fixed-size block of memory that contains the number, called a **cache line**:
the unit a cache stores and moves in one transfer. Drepper gives the
reasons. Every entry in a cache needs a tag saying which address it holds,
and a tag per word would cost about as much space as the word. Programs tend
to use neighbouring memory together, so fetching the neighbours is usually a
bargain. And DRAM delivers several words in a row far more cheaply than the
same words one at a time.[^drepper]

On the machine these chapters were written on, an Apple M4
Pro, a line is 128 bytes (`sysctl hw.cachelinesize`, checked 2026-09-24):
room for 32 consecutive `f32` values.[^local] Drepper describes lines of 64
or 128 bytes in 2007; the size varies between machines, so it is a fact to
query, never one to hard-code.[^drepper]

"Aligned" means the line boundaries are fixed by the address alone. With
128-byte lines, bytes 0 to 127 form one line, bytes 128 to 255 the next, and
the line an address belongs to is the address divided by 128, rounded down.
Ask for `a[0, 0]` of an array that starts on a line boundary, and the cache
brings in `a[0, 0]` to `a[0, 31]` together, whether you wanted the rest or
not.

That one fact explains the asymmetry. Walking along a row, the first access to
a line pays for the fetch, and the next 31 find their values already there.
Walking down a column of a matrix whose rows are at least 128 bytes long, each
step lands in a different line. To see how different, count **line
transitions**: the number of times two consecutive visits fall in different
lines. `traversal_order.cpp` counts them for an 8 by 64 array of `f32`, with
the M4's 128-byte line. It measures no time; it computes a property of the
visiting order, so its output is the same on every machine.

--8<-- "includes/examples/optimize/p2-memory-hierarchy/traversal_order.cpp.md"

Both orders visit all 512 elements, and both touch the same 16 lines. Row
order enters each line once and stays for 32 visits: 16 transitions. Column
order leaves the line on every visit: 512 transitions. Figure 1 draws the same
comparison at a size that fits on the page.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="A 4 by 8 array with lines of 4 elements, visited in row order and in column order" aria-describedby="p2-f1-desc">
<title id="p2-f1-title">The same lines, visited in two orders</title>
<desc id="p2-f1-desc">Two copies of a grid of 4 rows and 8 columns. Heavy outlines group each row into two lines of 4 cells. In the left copy the cells are numbered 1 to 32 row by row, so numbers 1 to 4 share the first line, 5 to 8 the second, and so on: 8 line entries in 32 visits. In the right copy the cells are numbered down the columns, so 1, 2, 3 and 4 fall in four different lines and every visit enters a new line: 32 line entries in 32 visits.</desc>
<text class="vx-text" x="20" y="24">Row order: along each row</text>
<text class="vx-text" x="400" y="24">Column order: down each column</text>
<g transform="translate(20,40)">
<rect class="vx-box-strong" x="-3" y="-3" width="168" height="42"/>
<rect class="vx-box-strong" x="169" y="-3" width="168" height="42"/>
<rect class="vx-box-strong" x="-3" y="43" width="168" height="42"/>
<rect class="vx-box-strong" x="169" y="43" width="168" height="42"/>
<rect class="vx-box-strong" x="-3" y="89" width="168" height="42"/>
<rect class="vx-box-strong" x="169" y="89" width="168" height="42"/>
<rect class="vx-box-strong" x="-3" y="135" width="168" height="42"/>
<rect class="vx-box-strong" x="169" y="135" width="168" height="42"/>
<rect class="vx-box" x="0" y="0" width="36" height="36"/><text class="vx-mono" x="18" y="23" text-anchor="middle">1</text>
<rect class="vx-box" x="42" y="0" width="36" height="36"/><text class="vx-mono" x="60" y="23" text-anchor="middle">2</text>
<rect class="vx-box" x="84" y="0" width="36" height="36"/><text class="vx-mono" x="102" y="23" text-anchor="middle">3</text>
<rect class="vx-box" x="126" y="0" width="36" height="36"/><text class="vx-mono" x="144" y="23" text-anchor="middle">4</text>
<rect class="vx-box" x="172" y="0" width="36" height="36"/><text class="vx-mono" x="190" y="23" text-anchor="middle">5</text>
<rect class="vx-box" x="214" y="0" width="36" height="36"/><text class="vx-mono" x="232" y="23" text-anchor="middle">6</text>
<rect class="vx-box" x="256" y="0" width="36" height="36"/><text class="vx-mono" x="274" y="23" text-anchor="middle">7</text>
<rect class="vx-box" x="298" y="0" width="36" height="36"/><text class="vx-mono" x="316" y="23" text-anchor="middle">8</text>
<rect class="vx-box" x="0" y="46" width="36" height="36"/><text class="vx-mono" x="18" y="69" text-anchor="middle">9</text>
<rect class="vx-box" x="42" y="46" width="36" height="36"/><text class="vx-mono" x="60" y="69" text-anchor="middle">10</text>
<rect class="vx-box" x="84" y="46" width="36" height="36"/><text class="vx-mono" x="102" y="69" text-anchor="middle">11</text>
<rect class="vx-box" x="126" y="46" width="36" height="36"/><text class="vx-mono" x="144" y="69" text-anchor="middle">12</text>
<rect class="vx-box" x="172" y="46" width="36" height="36"/><text class="vx-mono" x="190" y="69" text-anchor="middle">13</text>
<rect class="vx-box" x="214" y="46" width="36" height="36"/><text class="vx-mono" x="232" y="69" text-anchor="middle">14</text>
<rect class="vx-box" x="256" y="46" width="36" height="36"/><text class="vx-mono" x="274" y="69" text-anchor="middle">15</text>
<rect class="vx-box" x="298" y="46" width="36" height="36"/><text class="vx-mono" x="316" y="69" text-anchor="middle">16</text>
<rect class="vx-box" x="0" y="92" width="36" height="36"/><text class="vx-mono" x="18" y="115" text-anchor="middle">17</text>
<rect class="vx-box" x="42" y="92" width="36" height="36"/><text class="vx-mono" x="60" y="115" text-anchor="middle">18</text>
<rect class="vx-box" x="84" y="92" width="36" height="36"/><text class="vx-mono" x="102" y="115" text-anchor="middle">19</text>
<rect class="vx-box" x="126" y="92" width="36" height="36"/><text class="vx-mono" x="144" y="115" text-anchor="middle">20</text>
<rect class="vx-box" x="172" y="92" width="36" height="36"/><text class="vx-mono" x="190" y="115" text-anchor="middle">21</text>
<rect class="vx-box" x="214" y="92" width="36" height="36"/><text class="vx-mono" x="232" y="115" text-anchor="middle">22</text>
<rect class="vx-box" x="256" y="92" width="36" height="36"/><text class="vx-mono" x="274" y="115" text-anchor="middle">23</text>
<rect class="vx-box" x="298" y="92" width="36" height="36"/><text class="vx-mono" x="316" y="115" text-anchor="middle">24</text>
<rect class="vx-box" x="0" y="138" width="36" height="36"/><text class="vx-mono" x="18" y="161" text-anchor="middle">25</text>
<rect class="vx-box" x="42" y="138" width="36" height="36"/><text class="vx-mono" x="60" y="161" text-anchor="middle">26</text>
<rect class="vx-box" x="84" y="138" width="36" height="36"/><text class="vx-mono" x="102" y="161" text-anchor="middle">27</text>
<rect class="vx-box" x="126" y="138" width="36" height="36"/><text class="vx-mono" x="144" y="161" text-anchor="middle">28</text>
<rect class="vx-box" x="172" y="138" width="36" height="36"/><text class="vx-mono" x="190" y="161" text-anchor="middle">29</text>
<rect class="vx-box" x="214" y="138" width="36" height="36"/><text class="vx-mono" x="232" y="161" text-anchor="middle">30</text>
<rect class="vx-box" x="256" y="138" width="36" height="36"/><text class="vx-mono" x="274" y="161" text-anchor="middle">31</text>
<rect class="vx-box" x="298" y="138" width="36" height="36"/><text class="vx-mono" x="316" y="161" text-anchor="middle">32</text>
</g>
<g transform="translate(400,40)">
<rect class="vx-box-strong" x="-3" y="-3" width="168" height="42"/>
<rect class="vx-box-strong" x="169" y="-3" width="168" height="42"/>
<rect class="vx-box-strong" x="-3" y="43" width="168" height="42"/>
<rect class="vx-box-strong" x="169" y="43" width="168" height="42"/>
<rect class="vx-box-strong" x="-3" y="89" width="168" height="42"/>
<rect class="vx-box-strong" x="169" y="89" width="168" height="42"/>
<rect class="vx-box-strong" x="-3" y="135" width="168" height="42"/>
<rect class="vx-box-strong" x="169" y="135" width="168" height="42"/>
<rect class="vx-box" x="0" y="0" width="36" height="36"/><text class="vx-mono" x="18" y="23" text-anchor="middle">1</text>
<rect class="vx-box" x="42" y="0" width="36" height="36"/><text class="vx-mono" x="60" y="23" text-anchor="middle">5</text>
<rect class="vx-box" x="84" y="0" width="36" height="36"/><text class="vx-mono" x="102" y="23" text-anchor="middle">9</text>
<rect class="vx-box" x="126" y="0" width="36" height="36"/><text class="vx-mono" x="144" y="23" text-anchor="middle">13</text>
<rect class="vx-box" x="172" y="0" width="36" height="36"/><text class="vx-mono" x="190" y="23" text-anchor="middle">17</text>
<rect class="vx-box" x="214" y="0" width="36" height="36"/><text class="vx-mono" x="232" y="23" text-anchor="middle">21</text>
<rect class="vx-box" x="256" y="0" width="36" height="36"/><text class="vx-mono" x="274" y="23" text-anchor="middle">25</text>
<rect class="vx-box" x="298" y="0" width="36" height="36"/><text class="vx-mono" x="316" y="23" text-anchor="middle">29</text>
<rect class="vx-box" x="0" y="46" width="36" height="36"/><text class="vx-mono" x="18" y="69" text-anchor="middle">2</text>
<rect class="vx-box" x="42" y="46" width="36" height="36"/><text class="vx-mono" x="60" y="69" text-anchor="middle">6</text>
<rect class="vx-box" x="84" y="46" width="36" height="36"/><text class="vx-mono" x="102" y="69" text-anchor="middle">10</text>
<rect class="vx-box" x="126" y="46" width="36" height="36"/><text class="vx-mono" x="144" y="69" text-anchor="middle">14</text>
<rect class="vx-box" x="172" y="46" width="36" height="36"/><text class="vx-mono" x="190" y="69" text-anchor="middle">18</text>
<rect class="vx-box" x="214" y="46" width="36" height="36"/><text class="vx-mono" x="232" y="69" text-anchor="middle">22</text>
<rect class="vx-box" x="256" y="46" width="36" height="36"/><text class="vx-mono" x="274" y="69" text-anchor="middle">26</text>
<rect class="vx-box" x="298" y="46" width="36" height="36"/><text class="vx-mono" x="316" y="69" text-anchor="middle">30</text>
<rect class="vx-box" x="0" y="92" width="36" height="36"/><text class="vx-mono" x="18" y="115" text-anchor="middle">3</text>
<rect class="vx-box" x="42" y="92" width="36" height="36"/><text class="vx-mono" x="60" y="115" text-anchor="middle">7</text>
<rect class="vx-box" x="84" y="92" width="36" height="36"/><text class="vx-mono" x="102" y="115" text-anchor="middle">11</text>
<rect class="vx-box" x="126" y="92" width="36" height="36"/><text class="vx-mono" x="144" y="115" text-anchor="middle">15</text>
<rect class="vx-box" x="172" y="92" width="36" height="36"/><text class="vx-mono" x="190" y="115" text-anchor="middle">19</text>
<rect class="vx-box" x="214" y="92" width="36" height="36"/><text class="vx-mono" x="232" y="115" text-anchor="middle">23</text>
<rect class="vx-box" x="256" y="92" width="36" height="36"/><text class="vx-mono" x="274" y="115" text-anchor="middle">27</text>
<rect class="vx-box" x="298" y="92" width="36" height="36"/><text class="vx-mono" x="316" y="115" text-anchor="middle">31</text>
<rect class="vx-box" x="0" y="138" width="36" height="36"/><text class="vx-mono" x="18" y="161" text-anchor="middle">4</text>
<rect class="vx-box" x="42" y="138" width="36" height="36"/><text class="vx-mono" x="60" y="161" text-anchor="middle">8</text>
<rect class="vx-box" x="84" y="138" width="36" height="36"/><text class="vx-mono" x="102" y="161" text-anchor="middle">12</text>
<rect class="vx-box" x="126" y="138" width="36" height="36"/><text class="vx-mono" x="144" y="161" text-anchor="middle">16</text>
<rect class="vx-box" x="172" y="138" width="36" height="36"/><text class="vx-mono" x="190" y="161" text-anchor="middle">20</text>
<rect class="vx-box" x="214" y="138" width="36" height="36"/><text class="vx-mono" x="232" y="161" text-anchor="middle">24</text>
<rect class="vx-box" x="256" y="138" width="36" height="36"/><text class="vx-mono" x="274" y="161" text-anchor="middle">28</text>
<rect class="vx-box" x="298" y="138" width="36" height="36"/><text class="vx-mono" x="316" y="161" text-anchor="middle">32</text>
</g>
<text class="vx-text-accent" x="20" y="244">8 line entries in 32 visits</text>
<text class="vx-text-muted" x="20" y="264">each line is fetched once and used 4 times</text>
<text class="vx-text-accent" x="400" y="244">32 line entries in 32 visits</text>
<text class="vx-text-muted" x="400" y="264">each line is entered 4 times, one value each time</text>
<text class="vx-text-muted" x="20" y="290">heavy outline: one cache line (4 elements in this drawing, 32 f32 on the M4 Pro)</text>
</svg>
<figcaption>Figure 1. The storage is the same in both panels; only the visiting order differs. With lines of 4 elements, row order enters each of the 8 lines once. Column order enters a line on every visit, and whether each entry costs a fetch depends on whether the line is still in the cache when the walk comes back to it.</figcaption>
</figure>

A transition is not yet a miss. Column order returns to each line 4 times in
Figure 1, and 32 times in the example, and a return is cheap if the line is
still cached. For an array this small it will be: 16 lines of 128 bytes are
2 KiB. The cost appears when the lines visited between two returns to the same
line are more than the cache can keep. The next two sections say how much a
cache keeps and where it keeps it.

Writes follow the same rule. A store changes part of a line, so the processor
loads the whole line first; a cache cannot hold part of a line. The changed
line is marked **dirty**, and in the common **write-back** design it is
written to the next level only when it is evicted, not on every
store.[^drepper] `c[row, column] = sum` therefore costs a line fetch the first
time it touches a line of `c`, like any read.

??? check "Both orders in `traversal_order.cpp` touch the same 16 lines. On a real machine, would the column order be slower for this 8 by 64 array?"

    Probably not by much. The whole array is 2 KiB, so all 16 lines fit in
    any first-level cache at once. After the first 16 fetches, every return
    to a line finds it still there. Line transitions show how often an order
    leaves a line; they become misses only when too much else is visited
    before the order comes back.

## The hierarchy: several sizes, several speeds

A machine usually has more than one cache. The first level, **L1**, sits
beside each core and is split into a data cache (L1d) and an instruction
cache. The second level, **L2**, is larger and often shared by several cores.
Some processors add an **L3**. Each level is larger and slower than the one
above it, and a miss at one level becomes a request to the next.[^drepper]
Figure 2 draws the levels the owner's M4 Pro reports.

<figure class="vx-figure">
<svg viewBox="0 0 760 320" role="img" aria-label="The cache levels an Apple M4 Pro reports through sysctl: per-core L1 data caches, an L2 per cluster of cores, and main memory" aria-describedby="p2-f2-desc">
<title id="p2-f2-title">Cache levels of the owner's M4 Pro</title>
<desc id="p2-f2-desc">Three clusters of cores side by side above a wide box for main memory. The first performance cluster has four cores, each with its own 128 KiB L1 data cache, and one 16 MiB L2 shared by the four. A second performance cluster is drawn the same way. An efficiency cluster has four cores, each with a 64 KiB L1 data cache, sharing a 4 MiB L2. All three L2 caches connect to main memory. No L3 is reported. Labels on the right say that size grows and distance grows going down.</desc>
<defs><marker id="p2-f2-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="22">Performance cluster</text>
<text class="vx-text" x="250" y="22">Performance cluster</text>
<text class="vx-text" x="480" y="22">Efficiency cluster</text>
<g transform="translate(20,34)">
<rect class="vx-box" x="0" y="0" width="48" height="30" rx="3"/><text class="vx-text-muted" x="24" y="20" text-anchor="middle">core</text>
<rect class="vx-box" x="54" y="0" width="48" height="30" rx="3"/><text class="vx-text-muted" x="78" y="20" text-anchor="middle">core</text>
<rect class="vx-box" x="108" y="0" width="48" height="30" rx="3"/><text class="vx-text-muted" x="132" y="20" text-anchor="middle">core</text>
<rect class="vx-box" x="162" y="0" width="48" height="30" rx="3"/><text class="vx-text-muted" x="186" y="20" text-anchor="middle">core</text>
<rect class="vx-box-accent" x="0" y="40" width="48" height="34" rx="3"/><text class="vx-mono" x="24" y="55" text-anchor="middle">L1d</text><text class="vx-mono" x="24" y="69" text-anchor="middle">128K</text>
<rect class="vx-box-accent" x="54" y="40" width="48" height="34" rx="3"/><text class="vx-mono" x="78" y="55" text-anchor="middle">L1d</text><text class="vx-mono" x="78" y="69" text-anchor="middle">128K</text>
<rect class="vx-box-accent" x="108" y="40" width="48" height="34" rx="3"/><text class="vx-mono" x="132" y="55" text-anchor="middle">L1d</text><text class="vx-mono" x="132" y="69" text-anchor="middle">128K</text>
<rect class="vx-box-accent" x="162" y="40" width="48" height="34" rx="3"/><text class="vx-mono" x="186" y="55" text-anchor="middle">L1d</text><text class="vx-mono" x="186" y="69" text-anchor="middle">128K</text>
<rect class="vx-box-strong" x="0" y="96" width="210" height="44" rx="4"/><text class="vx-mono" x="105" y="123" text-anchor="middle">L2 16 MiB, shared by 4</text>
<path class="vx-line" d="M105 74 L105 95" marker-end="url(#p2-f2-head)"/>
<path class="vx-line" d="M105 140 L105 219" marker-end="url(#p2-f2-head)"/>
</g>
<g transform="translate(250,34)">
<rect class="vx-box" x="0" y="0" width="48" height="30" rx="3"/><text class="vx-text-muted" x="24" y="20" text-anchor="middle">core</text>
<rect class="vx-box" x="54" y="0" width="48" height="30" rx="3"/><text class="vx-text-muted" x="78" y="20" text-anchor="middle">core</text>
<rect class="vx-box" x="108" y="0" width="48" height="30" rx="3"/><text class="vx-text-muted" x="132" y="20" text-anchor="middle">core</text>
<rect class="vx-box" x="162" y="0" width="48" height="30" rx="3"/><text class="vx-text-muted" x="186" y="20" text-anchor="middle">core</text>
<rect class="vx-box-accent" x="0" y="40" width="48" height="34" rx="3"/><text class="vx-mono" x="24" y="55" text-anchor="middle">L1d</text><text class="vx-mono" x="24" y="69" text-anchor="middle">128K</text>
<rect class="vx-box-accent" x="54" y="40" width="48" height="34" rx="3"/><text class="vx-mono" x="78" y="55" text-anchor="middle">L1d</text><text class="vx-mono" x="78" y="69" text-anchor="middle">128K</text>
<rect class="vx-box-accent" x="108" y="40" width="48" height="34" rx="3"/><text class="vx-mono" x="132" y="55" text-anchor="middle">L1d</text><text class="vx-mono" x="132" y="69" text-anchor="middle">128K</text>
<rect class="vx-box-accent" x="162" y="40" width="48" height="34" rx="3"/><text class="vx-mono" x="186" y="55" text-anchor="middle">L1d</text><text class="vx-mono" x="186" y="69" text-anchor="middle">128K</text>
<rect class="vx-box-strong" x="0" y="96" width="210" height="44" rx="4"/><text class="vx-mono" x="105" y="123" text-anchor="middle">L2 16 MiB, shared by 4</text>
<path class="vx-line" d="M105 74 L105 95" marker-end="url(#p2-f2-head)"/>
<path class="vx-line" d="M105 140 L105 219" marker-end="url(#p2-f2-head)"/>
</g>
<g transform="translate(480,34)">
<rect class="vx-box" x="0" y="0" width="48" height="30" rx="3"/><text class="vx-text-muted" x="24" y="20" text-anchor="middle">core</text>
<rect class="vx-box" x="54" y="0" width="48" height="30" rx="3"/><text class="vx-text-muted" x="78" y="20" text-anchor="middle">core</text>
<rect class="vx-box" x="108" y="0" width="48" height="30" rx="3"/><text class="vx-text-muted" x="132" y="20" text-anchor="middle">core</text>
<rect class="vx-box" x="162" y="0" width="48" height="30" rx="3"/><text class="vx-text-muted" x="186" y="20" text-anchor="middle">core</text>
<rect class="vx-box" x="0" y="40" width="48" height="34" rx="3"/><text class="vx-mono" x="24" y="55" text-anchor="middle">L1d</text><text class="vx-mono" x="24" y="69" text-anchor="middle">64K</text>
<rect class="vx-box" x="54" y="40" width="48" height="34" rx="3"/><text class="vx-mono" x="78" y="55" text-anchor="middle">L1d</text><text class="vx-mono" x="78" y="69" text-anchor="middle">64K</text>
<rect class="vx-box" x="108" y="40" width="48" height="34" rx="3"/><text class="vx-mono" x="132" y="55" text-anchor="middle">L1d</text><text class="vx-mono" x="132" y="69" text-anchor="middle">64K</text>
<rect class="vx-box" x="162" y="40" width="48" height="34" rx="3"/><text class="vx-mono" x="186" y="55" text-anchor="middle">L1d</text><text class="vx-mono" x="186" y="69" text-anchor="middle">64K</text>
<rect class="vx-box" x="0" y="96" width="210" height="44" rx="4"/><text class="vx-mono" x="105" y="123" text-anchor="middle">L2 4 MiB, shared by 4</text>
<path class="vx-line" d="M105 74 L105 95" marker-end="url(#p2-f2-head)"/>
<path class="vx-line" d="M105 140 L105 219" marker-end="url(#p2-f2-head)"/>
</g>
<rect class="vx-box-strong" x="20" y="254" width="670" height="40" rx="4"/>
<text class="vx-text" x="355" y="279" text-anchor="middle">Main memory (DRAM): no L3 reported</text>
<text class="vx-text-muted" x="706" y="60">small,</text>
<text class="vx-text-muted" x="706" y="76">near</text>
<text class="vx-text-muted" x="706" y="268">large,</text>
<text class="vx-text-muted" x="706" y="284">far</text>
<path class="vx-line" d="M726 88 L726 248" marker-end="url(#p2-f2-head)"/>
</svg>
<figcaption>Figure 2. The data caches of the owner's Apple M4 Pro, as <code>sysctl</code> reports them on 2026-09-24: 128 KiB of L1d per performance core, one 16 MiB L2 per four performance cores, 64 KiB of L1d per efficiency core, a 4 MiB L2 for the four efficiency cores, and no L3. The two performance clusters follow from 8 performance cores at 4 per L2. Latencies are not reported by <code>sysctl</code>; the last section measures them.</figcaption>
</figure>

The M4 Pro has two kinds of core, and they have different caches. `sysctl`
reports each kind under its own name: `hw.perflevel0.*` for the performance
cores and `hw.perflevel1.*` for the efficiency cores. The short name
`hw.l1dcachesize` reports 64 KiB on this machine, the efficiency cores'
figure, not the 128 KiB a performance core has.[^local] A program that reads
one key and assumes it describes every core will be wrong on half the
machine. [P1](p1-measure-first.md#a-stopwatch-is-not-a-measurement) showed
that macOS decides which kind of core runs a thread; the cache that thread
sees depends on that decision too.

A **working set** is the data a piece of code uses repeatedly over some
stretch of its run, measured in bytes or in lines. The central rule of the
hierarchy is about working sets. While a working set fits in a level,
repeated use of it is served from that level. Once it outgrows the level,
repeated use falls through to the next one down. Drepper measured the rule
on a Pentium 4 with a 16 KiB L1d and a 1 MiB L2, walking a list laid out in
memory: the time per element showed three distinct levels, with the steps
where the working set passed 2^14 and 2^20 bytes.[^drepper]

### A cache in miniature

`working_set_levels.cpp` shows the rule without a clock. It models a toy
hierarchy counted in lines, an L1 of 8 lines and an L2 of 32, each keeping
the lines it has used most recently and evicting the one used least recently:
**least recently used (LRU)** replacement, the policy Drepper says most caches
use.[^drepper] It walks working sets of several sizes many times, once in the
same cyclic order every pass and once in a fresh random order every pass, and
reports which level served each access after the first pass.

--8<-- "includes/examples/optimize/p2-memory-hierarchy/working_set_levels.cpp.md"

Read the cyclic rows first. Up to 8 lines, L1 serves everything. At 9 lines
L1 serves nothing at all. Walking 9 lines in a circle through an 8-line LRU
cache, the line about to be used is always the one evicted most recently:
by the time the walk comes back to line 0, lines 1 to 8 have pushed it out.
The same happens to L2 between 32 and 33 lines. One line too many does not
cost one miss per pass; it costs every hit.

The random rows are softer. At 9 lines about two thirds of the accesses still
hit L1, because a random order often returns to a line before 8 others have
passed. The drop is gradual: L2's share grows step by step as the working
set grows past L1, and main memory's share past L2. Real measurements look more like the random rows than the
cyclic ones. Drepper's curves have rounded steps, because the rest of the
system uses the caches too, and he notes that exact LRU grows expensive to
maintain as associativity grows, so hardware may approximate it.[^drepper]

To turn the table into a cost, weight each level by its latency. With the
Pentium M's cycles from the first section, the random 9-line case costs about
$\frac{2}{3} \times 3 + \frac{1}{3} \times 14 \approx 6.7$ cycles per access,
and the cyclic 9-line case costs 14. Same data, same size, twice the cost,
because of the order.

## Sets and associativity

A cache cannot afford to search every entry on every load. Drepper works the
numbers for a 4 MB cache with 64-byte lines: 65,536 entries, and the lookup
has a few cycles to find the one that matches. A **fully associative** cache,
where any line can sit in any entry, needs a comparator per entry, which is
practical only for caches of a few dozen entries.[^drepper] Real data caches
restrict where each line may go.

The cache is divided into **sets**, and the address decides the set. Drepper
splits an address into three fields.[^drepper] The low bits are the
**offset**: which byte within the line. The next bits are the **set index**:
which set the line must go in. The remaining high bits are the **tag**: stored
with the line, so that a lookup can tell which of the many lines that share a
set is present.

Each set has room for a fixed number of lines, called
**ways**, and the number of ways is the cache's **associativity**. A
**direct-mapped** cache has one way per set, so a new line always evicts the
old one. An **N-way set-associative** cache has N ways, and a lookup compares
the N tags of one set at once.

Work one address by hand, in a toy cache with 64-byte lines, 16 sets and 2
ways. A 64-byte line needs 6 offset bits and 16 sets need 4 index bits; the
tag is everything above.

1. Take the address `0x1234`, which is 4660.
2. Offset: 4660 mod 64 = 52. The byte is 52 bytes into its line.
3. Line number: 4660 / 64 = 72, rounded down.
4. Set: 72 mod 16 = 8. Tag: 72 / 16 = 4.

Every address whose line number is 8 more than a multiple of 16 lands in set
8. In this toy cache those addresses are 16 × 64 = 1024 bytes apart: the size
of one way. Two addresses that far apart compete for the same two slots, even
if the other 15 sets are empty.

`associativity_conflict.cpp` builds this cache and prints the split for
`0x1234`. Then it reads the first element of each of 8 rows of an `f32`
matrix, twice, as a column walk does, for two row lengths.

--8<-- "includes/examples/optimize/p2-memory-hierarchy/associativity_conflict.cpp.md"

Figure 3 steps through the result.

<figure class="vx-figure">
<div class="vx-stepper">
<div class="vx-step">
<p><strong>Step 1. Split the address.</strong> The low 6 bits of <code>0x1234</code> give the offset, 52. The next 4 bits give the set, 8. The top bits give the tag, 4, which is stored with the line.</p>
<svg viewBox="0 0 760 170" role="img" aria-label="The address 0x1234 written in binary and split into a tag of 4, a set index of 8 and an offset of 52">
<text class="vx-text" x="20" y="24">0x1234 = 4660, in binary</text>
<rect class="vx-box" x="20" y="44" width="270" height="44" rx="4"/>
<text class="vx-mono" x="155" y="72" text-anchor="middle">0 0 0 1 0 0</text>
<rect class="vx-box-accent" x="296" y="44" width="180" height="44" rx="4"/>
<text class="vx-mono" x="386" y="72" text-anchor="middle">1 0 0 0</text>
<rect class="vx-box" x="482" y="44" width="258" height="44" rx="4"/>
<text class="vx-mono" x="611" y="72" text-anchor="middle">1 1 0 1 0 0</text>
<text class="vx-text" x="155" y="114" text-anchor="middle">tag: bits 15 to 10</text>
<text class="vx-text-accent" x="386" y="114" text-anchor="middle">set: bits 9 to 6</text>
<text class="vx-text" x="611" y="114" text-anchor="middle">offset: bits 5 to 0</text>
<text class="vx-mono" x="155" y="140" text-anchor="middle">= 4</text>
<text class="vx-mono vx-text-accent" x="386" y="140" text-anchor="middle">= 8</text>
<text class="vx-mono" x="611" y="140" text-anchor="middle">= 52</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 2. Rows of 1024 bytes.</strong> Row <code>r</code> starts at byte 1024 × r, line 16 × r, so every row start maps to set 0. Eight lines want a set with two ways. Each new line evicts the least recently used one, and the second pass misses 8 times out of 8, while 15 sets stay empty.</p>
<svg viewBox="0 0 760 250" role="img" aria-label="Sixteen sets of two ways. All eight rows map to set 0, which can hold only two of them; the other fifteen sets are empty">
<text class="vx-text" x="20" y="22">set</text>
<text class="vx-text" x="70" y="22">way 0</text>
<text class="vx-text" x="160" y="22">way 1</text>
<rect class="vx-box-bad" x="60" y="32" width="84" height="22" rx="3"/><text class="vx-mono" x="102" y="48" text-anchor="middle">row 7</text>
<rect class="vx-box-bad" x="150" y="32" width="84" height="22" rx="3"/><text class="vx-mono" x="192" y="48" text-anchor="middle">row 6</text>
<text class="vx-mono vx-text-accent" x="30" y="48" text-anchor="middle">0</text>
<g>
<text class="vx-mono" x="30" y="72" text-anchor="middle">1</text><rect class="vx-box" x="60" y="58" width="84" height="18" rx="3"/><rect class="vx-box" x="150" y="58" width="84" height="18" rx="3"/>
<text class="vx-mono" x="30" y="96" text-anchor="middle">2</text><rect class="vx-box" x="60" y="82" width="84" height="18" rx="3"/><rect class="vx-box" x="150" y="82" width="84" height="18" rx="3"/>
<text class="vx-mono" x="30" y="120" text-anchor="middle">3</text><rect class="vx-box" x="60" y="106" width="84" height="18" rx="3"/><rect class="vx-box" x="150" y="106" width="84" height="18" rx="3"/>
<text class="vx-mono" x="30" y="144" text-anchor="middle">…</text>
<text class="vx-mono" x="30" y="168" text-anchor="middle">15</text><rect class="vx-box" x="60" y="154" width="84" height="18" rx="3"/><rect class="vx-box" x="150" y="154" width="84" height="18" rx="3"/>
</g>
<text class="vx-text" x="280" y="48">rows 0, 1, 2, 3, 4, 5 were here and were evicted</text>
<text class="vx-text" x="280" y="72">second pass: row 0 is gone, evicts row 6;</text>
<text class="vx-text" x="280" y="92">row 1 is gone, evicts row 7; and so on</text>
<text class="vx-text-accent" x="280" y="130">misses: 8 on the first pass, 8 on the second</text>
<text class="vx-text-muted" x="280" y="154">8 lines are a quarter of the cache: conflict, not capacity</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 3. Rows of 1088 bytes.</strong> One line longer per row. Row <code>r</code> starts at line 17 × r, and 17 × r mod 16 = r, so the eight rows land in sets 0 to 7, one line each. The second pass hits 8 times out of 8.</p>
<svg viewBox="0 0 760 250" role="img" aria-label="Sixteen sets of two ways. Rows 0 to 7 land one each in sets 0 to 7; no set holds more than one of them">
<text class="vx-text" x="20" y="22">set</text>
<text class="vx-text" x="70" y="22">way 0</text>
<text class="vx-text" x="160" y="22">way 1</text>
<text class="vx-mono" x="30" y="44" text-anchor="middle">0</text><rect class="vx-box-accent" x="60" y="30" width="84" height="18" rx="3"/><text class="vx-mono" x="102" y="44" text-anchor="middle">row 0</text><rect class="vx-box" x="150" y="30" width="84" height="18" rx="3"/>
<text class="vx-mono" x="30" y="66" text-anchor="middle">1</text><rect class="vx-box-accent" x="60" y="52" width="84" height="18" rx="3"/><text class="vx-mono" x="102" y="66" text-anchor="middle">row 1</text><rect class="vx-box" x="150" y="52" width="84" height="18" rx="3"/>
<text class="vx-mono" x="30" y="88" text-anchor="middle">2</text><rect class="vx-box-accent" x="60" y="74" width="84" height="18" rx="3"/><text class="vx-mono" x="102" y="88" text-anchor="middle">row 2</text><rect class="vx-box" x="150" y="74" width="84" height="18" rx="3"/>
<text class="vx-mono" x="30" y="110" text-anchor="middle">3</text><rect class="vx-box-accent" x="60" y="96" width="84" height="18" rx="3"/><text class="vx-mono" x="102" y="110" text-anchor="middle">row 3</text><rect class="vx-box" x="150" y="96" width="84" height="18" rx="3"/>
<text class="vx-mono" x="30" y="132" text-anchor="middle">4</text><rect class="vx-box-accent" x="60" y="118" width="84" height="18" rx="3"/><text class="vx-mono" x="102" y="132" text-anchor="middle">row 4</text><rect class="vx-box" x="150" y="118" width="84" height="18" rx="3"/>
<text class="vx-mono" x="30" y="154" text-anchor="middle">5</text><rect class="vx-box-accent" x="60" y="140" width="84" height="18" rx="3"/><text class="vx-mono" x="102" y="154" text-anchor="middle">row 5</text><rect class="vx-box" x="150" y="140" width="84" height="18" rx="3"/>
<text class="vx-mono" x="30" y="176" text-anchor="middle">6</text><rect class="vx-box-accent" x="60" y="162" width="84" height="18" rx="3"/><text class="vx-mono" x="102" y="176" text-anchor="middle">row 6</text><rect class="vx-box" x="150" y="162" width="84" height="18" rx="3"/>
<text class="vx-mono" x="30" y="198" text-anchor="middle">7</text><rect class="vx-box-accent" x="60" y="184" width="84" height="18" rx="3"/><text class="vx-mono" x="102" y="198" text-anchor="middle">row 7</text><rect class="vx-box" x="150" y="184" width="84" height="18" rx="3"/>
<text class="vx-mono vx-text-muted" x="30" y="226" text-anchor="middle">8…15</text><text class="vx-text-muted" x="60" y="226">empty</text>
<text class="vx-text-accent" x="280" y="110">misses: 8 on the first pass, 0 on the second</text>
<text class="vx-text-muted" x="280" y="134">the same 8 lines, spread over 8 sets</text>
</svg>
</div>
</div>
<figcaption>Figure 3. The toy cache of <code>associativity_conflict.cpp</code>: 16 sets, 2 ways, 64-byte lines. Step 1 splits an address; steps 2 and 3 place the start of each of 8 matrix rows. When the row length is a multiple of the way size, 1024 bytes, every row competes for one set; one line more per row spreads them out.</figcaption>
</figure>

The lesson generalizes. Addresses a multiple of the way size apart share a
set. A column walk over a matrix whose row length is a multiple of the way
size, often a power of two, piles its lines into a few sets and evicts them
while the cache is mostly empty. Padding each row by one line spreads them
out. [P8](p8-cache-blocking.md#fitting-is-not-enough-self-interference)
comes back to this with real tile sizes.

### Three reasons a line is missing

Every miss has a cause, and the causes need different cures. The standard
names, all three used by Drepper, are:[^drepper]

- A **compulsory miss** happens on the first use of a line: it has never been
  in the cache. Only fetching less data, or fetching it earlier, helps.
- A **capacity miss** happens because the working set is larger than the
  cache, so lines are evicted before their reuse. The cyclic 9-line walk
  above misses L1 for this reason. Reordering the loops so that reuse comes
  sooner helps.
- A **conflict miss** happens because too many lines in use map to one set,
  though the cache as a whole has room. The 1024-byte rows above miss for this
  reason. Changing the layout, such as padding, helps; so does more
  associativity.

??? check "A direct-mapped cache and a 2-way cache of the same total size alternate between two addresses exactly one direct-mapped cache size apart. Which of them misses on every access, and what kind of miss is it?"

    The direct-mapped one. In it the two addresses share a set, and a set
    has one slot, so each access evicts the other address: after the first
    two accesses, every access misses. In the 2-way cache, a way is half the
    size, so the addresses still share a set, but the set holds both. The
    misses in the direct-mapped cache are conflict misses: two lines are
    nowhere near its capacity.

Your own processor's associativity is one fact you may not be able to read.
Linux reports it for each cache, with the number of sets, in
`/sys/devices/system/cpu/cpu*/cache/index*/ways_of_associativity`.[^sysfs]
`sysctl` on the owner's M4 Pro reports sizes and the line size, and no
associativity at all.[^local] When the fact is missing, a program must either
measure it, as [P8](p8-cache-blocking.md#measuring-the-effect) shows, or
stay clear of strides that would expose it.

## Prefetching: the hardware guesses ahead

Go back to Drepper's Pentium 4 measurement. When the working set outgrew
L1d, the time per element of a sequential walk rose to about 9 cycles, well
below the 14 or more that an L2 access takes. The processor had seen the
walk moving through consecutive lines and started fetching the next line
before it was asked for. That is **hardware prefetching**.[^drepper]

Drepper describes how prefetchers of his time decide. A prefetch starts after
two or more misses that form a pattern: consecutive lines, or on newer
processors a fixed stride of lines. Patterns that are not linear are not
recognized. And a prefetcher does not cross a page boundary, because the next
page might not be mapped, and touching it could cause a fault the program
never asked for.[^drepper]

Two consequences follow for anyone who reads a timing. First, a sequential
walk measures the prefetcher as much as the cache: the time per element
depends on how far ahead the hardware fetched, not only on where the data
was. Second, to measure the latency of a level, you have to stop the
prefetcher from guessing. Drepper's random-order list walk did that: with the
list order shuffled, the time per element beyond L2 rose to 450 cycles and
more, above the 200 to 300 cycles his machine needed to reach main
memory.[^drepper]

### A pointer chase

The standard way to defeat both the prefetcher and the out-of-order core is
a **pointer chase**: an array in which each slot holds the index of the next
slot to visit, linked in a random order into one cycle through every slot.
Each load needs the result of the previous load to know its own address, so
no two loads can overlap and no pattern exists to predict.
[P5](p5-microarchitecture.md#latency-throughput-and-the-number-of-chains)
explains why independent loads can overlap and dependent ones cannot.

`pointer_chase.cpp` builds such chains. Run as the examples harness runs it,
it checks two properties that a latency test depends on: that the chain from
slot 0 visits every slot before returning, and how many hops stay inside one
128-byte line. A sequential chain stays in its line for 15 hops of every 16.
A shuffled chain almost never does.

--8<-- "includes/examples/optimize/p2-memory-hierarchy/pointer_chase.cpp.md"

The cycle length matters: a chain that closed early into a short loop would
keep its few slots in L1 and report L1 latency for any array size. Linking a
shuffled visiting order end to end, last back to first, cannot close early.
The same file has a second mode, `--time`, which the last section uses.

??? check "A student measures latency by reading `data[i]` for `i` from 0 to `n` and dividing the time by `n`. The curve shows almost no step at the L2 boundary. What went wrong?"

    Two things hide the step. The addresses are sequential, so the
    prefetcher fetches lines before they are needed. And each load's address
    is known without the previous load's value, so the core overlaps many
    loads at once. The loop measures streaming throughput, not latency. A
    pointer chase in random order removes both effects.

## The TLB: caching translations, not data

So far every address has been treated as the place the data lives. A program
works with **virtual addresses**, which the processor translates to physical
addresses through page tables that the operating system keeps in memory. A
**page** is the unit of translation: every address in one page shares one
translation. Walking the page tables on every load would take several
dependent memory accesses, so the processor keeps recent translations in a
small cache of its own, the **translation lookaside buffer (TLB)**.[^drepper]
Like data caches, TLBs come in levels, and a first-level TLB is small because
it must answer at once.[^drepper]

A TLB entry covers one page, so the memory a TLB can cover without a miss,
its **reach**, is the number of entries times the page size. The M4 Pro's
pages are 16 KiB (`sysctl hw.pagesize`), four times the 4 KiB common
elsewhere.[^local] `sysctl` does not report the TLB's number of entries.
Drepper found his Pentium 4's by experiment: he placed each list element on a
page of its own, and the time per element jumped once the pages outnumbered
the TLB, which put its size at 64 entries.[^drepper]

A TLB miss can happen on an access that hits the data cache, and it adds to
the cost of one that does not. Column walks are where this bites. On the M4
Pro, a 4096 by 4096 `f32` matrix has rows of 16 KiB, one page each, so a walk
down one column touches 4096 pages: 4096 translations to keep, for 4096
values. A row walk of the same length touches one page, or two if the row
does not start on a page boundary. [P8](p8-cache-blocking.md#copying-the-block-and-the-tlb)
and [P12](p12-fast-gemm.md) copy blocks into contiguous buffers partly to
shrink the number of pages a block spans.

## Locality in the stage 10 kernel, walked by hand

Two words summarize what makes caches work. **Spatial locality** is the use
of addresses near one used a moment ago: `a[row, 1]` after `a[row, 0]`, in the same
line. **Temporal locality** is the use of the same address again soon:
`a[row, k]` read once for every `column`, a benefit only if its line is still
cached when the next `column` comes round. Now apply both to `multiply` at
real sizes, with square `N` by `N` matrices of `f32` and the M4 Pro's L1d of
128 KiB, which is 1024 lines of 128 bytes.

**The a matrix.** For one `(row, column)` the `k` loop reads row `row` of `a`
in order: `N / 32` lines, 31 of every 32 reads hitting a line already
fetched. The next `column` reads the same row again. That is temporal reuse,
and it works if the row's lines survive one pass of the `k` loop.

**The b matrix.** For one `(row, column)` the `k` loop reads column `column`
of `b`. Once `N` is 32 or more, each row of `b` is at least 128 bytes, so each
`k` lands in a different line: `N` lines for `N` reads, no spatial reuse
within the pass. But `b[k, column + 1]` lies in the same line as
`b[k, column]` for 31 of every 32 columns. The next pass of the `k` loop wants
the same `N` lines again. Whether it finds them depends on capacity.

**One pass of the `k` loop** touches `N` lines of `b` plus `N / 32` lines of
`a`. For `N = 512`, that is 512 + 16 = 528 lines, which fit in 1024. The next
`column` finds `b`'s lines still in L1, and 31 of every 32 column passes hit.
Counting capacity alone, the naive order does well at this size.

Drepper's case is the other side of the line. His Core 2 had a 32 KiB L1d
with 64-byte lines, 512 lines in all, and his `N` was 1000 `double`s, 8,000
bytes per row. One pass of the inner loop needed 1000 lines of the second
matrix alone. By the time the loop came back for the next column, the line it
wanted had long been evicted.[^drepper] Every read of that matrix missed, and
transposing it turned those reads into row reads with spatial locality: one
miss per eight `double`s instead of one per `double`.

Three caveats keep this honest.

- **Conflicts.** At `N = 512` a row of `b` is 2048 bytes, a power of two, so
  the 512 lines of a column can use only as many sets as 2048 bytes go into
  the way size: one set, if a way is 2048 bytes or smaller. Unless those sets
  have 512 ways between them, the lines evict each other and the capacity
  count is too hopeful. `sysctl` cannot settle it; it does not report the
  associativity.
- **Prefetching.** A column is a fixed stride, so a stride prefetcher may
  hide part of the walk's cost.
- **Size.** At the 64 by 64 size that [P1](p1-measure-first.md) times, the
  three matrices together are 48 KiB and fit in L1 entirely. At that size
  the memory hierarchy barely matters, and a measurement will say so.

??? check "Now take N = 1024 on the M4 Pro. Do the lines of b survive from one column to the next?"

    One pass of the `k` loop touches 1024 lines of `b` and 32 of `a`: 1056
    lines, more than the 1024 that L1d holds. The walk is cyclic, as in
    `working_set_levels.cpp`, so under LRU replacement each line is evicted
    shortly before it is wanted again, and the reads of `b` fall through to L2.
    The 1056 lines are 132 KiB, far below the 16 MiB L2, so L2 serves them.
    Rows of 4096 bytes are also a power of two, so conflicts can make it worse.

The naive order depends on luck at every size: whether one column of `b` fits
in L1, whether its row length avoids a bad stride. The rest of the ladder
removes the luck. [P3](p3-roofline.md) turns bytes moved into a bound on
speed. [P6](p6-dependence-analysis.md) and [P7](p7-loop-transformations.md)
prove when loops may be reordered and reorder them so that the innermost loop
walks rows. [P8](p8-cache-blocking.md) cuts the loops into blocks whose
working set fits a chosen level by construction.

## Measuring the staircase on your machine

The examples above count; none of them times anything. The size of each
level on your machine is in `sysctl` or `/sys`, but its latency is not, and
the only honest way to get it is to measure. `pointer_chase.cpp --time` does
that: for working sets from 16 KiB to 256 MiB, doubling each step, it builds
a shuffled chain, touches every slot once, then times 2^24 dependent loads
five times and prints the median nanoseconds per load. The chase result is
stored to a `volatile` variable so the compiler cannot delete the loop,
the danger [P1](p1-measure-first.md#keeping-the-compiler-from-helping-too-much)
described.

Build it with the same flags as the harness, run it on an idle machine, and
repeat the whole run as separate launches, as P1 asks. It can take minutes.
Plot nanoseconds per load against working-set size on a logarithmic axis, and
fill in the table with your machine's name and the date.

| Working set | Median ns per load, launch 1 | Launch 2 | Launch 3 | Level `sysctl` predicts |
| --- | --- | --- | --- | --- |
| 16 KiB | | | | |
| 64 KiB | | | | |
| 256 KiB | | | | |
| 1 MiB | | | | |
| 4 MiB | | | | |
| 16 MiB | | | | |
| 64 MiB | | | | |
| 256 MiB | | | | |

What to expect is a staircase: flat stretches while the working set fits a
level, and a rise after each boundary, as in Figure 4.

<figure class="vx-figure">
<svg viewBox="0 0 760 290" role="img" aria-label="A schematic latency staircase: flat while the working set fits a level, rising after each level's size, with a sharp dashed version for a cyclic walk under exact LRU and a rounded solid version as measurements usually look" aria-describedby="p2-f4-desc">
<title id="p2-f4-title">The latency staircase, schematic</title>
<desc id="p2-f4-desc">A chart with working-set size on a logarithmic horizontal axis and latency per load on the vertical axis, without numbers. Vertical dashed lines mark the M4 Pro performance core's L1d size, 128 KiB, and its L2 size, 16 MiB. A dashed curve jumps straight up at each boundary, as the cyclic rows of the toy model do. A solid curve rises gradually over each boundary instead, as random-order walks and Drepper's plots do. Three flat regions are labelled L1, L2 and DRAM.</desc>
<line class="vx-line" x1="70" y1="230" x2="720" y2="230"/>
<line class="vx-line" x1="70" y1="230" x2="70" y2="24"/>
<text class="vx-text" x="395" y="278" text-anchor="middle">working set size (logarithmic)</text>
<text class="vx-text" x="30" y="128" text-anchor="middle" transform="rotate(-90 30 128)">time per load</text>
<line class="vx-line" x1="270" y1="230" x2="270" y2="30" stroke-dasharray="4 4"/>
<line class="vx-line" x1="470" y1="230" x2="470" y2="30" stroke-dasharray="4 4"/>
<text class="vx-mono vx-text-accent" x="270" y="250" text-anchor="middle">L1d: 128 KiB</text>
<text class="vx-mono vx-text-accent" x="470" y="250" text-anchor="middle">L2: 16 MiB</text>
<path class="vx-line" d="M 70 210 L 270 210 L 270 160 L 470 160 L 470 60 L 720 60" stroke-dasharray="6 4"/>
<path class="vx-line" d="M 70 206 L 240 206 C 275 206 290 158 330 156 L 430 156 C 480 154 500 70 560 64 L 720 58"/>
<text class="vx-text" x="160" y="196" text-anchor="middle">L1</text>
<text class="vx-text" x="370" y="146" text-anchor="middle">L2</text>
<text class="vx-text" x="620" y="48" text-anchor="middle">DRAM</text>
<line class="vx-line" x1="520" y1="120" x2="550" y2="120" stroke-dasharray="6 4"/>
<text class="vx-text-muted" x="558" y="124">cyclic walk, exact LRU</text>
<line class="vx-line" x1="520" y1="142" x2="550" y2="142"/>
<text class="vx-text-muted" x="558" y="146">what measurements show</text>
</svg>
<figcaption>Figure 4. A schematic, not data: the vertical axis has no numbers because only your measurement can supply them. The dashed curve is the sharp step of a cyclic walk under exact LRU, as in the cyclic rows of <code>working_set_levels.cpp</code>. The solid curve is the rounded shape that random-order walks and Drepper's measurements show. The two boundaries are the M4 Pro performance core's L1d and L2 sizes from <code>sysctl</code>.</figcaption>
</figure>

Read your curve against the sizes `sysctl` reports, and expect it to
disagree in places. The first step may sit at 64 KiB rather than 128 KiB if
the thread ran on an efficiency core. The rise past L2 may be steeper than
the cache alone explains, because a random walk over hundreds of MiB also
misses the TLB on almost every load. A flat stretch that ends early can mean
that something else shares the cache. Each disagreement is a fact about the
machine worth writing down beside the table.

## For Vortex

!!! vortex "Exercise"

    **Build** a target-facts record that your compiler fills in when it starts, and an `--explain` report that prints it. Later chapters add to the same report ([P3](p3-roofline.md#for-vortex) adds an intensity estimate), so it deserves a stable format now.

    1. **The facts:** line size, L1d size, L2 size and how many cores share it, and page size, for each kind of core the machine has. Associativity and number of sets where the system reports them (Linux does, in `/sys/devices/system/cpu/`), and the words "not reported" where it does not (macOS).
    2. **Provenance:** every value printed with the command, key or file it came from, such as `sysctl hw.perflevel0.l1dcachesize`. A query that fails prints "not reported", never a default from a book.
    3. **An override:** a command-line option that reads the facts from a file instead of the host, so that tests, and later cross-compilation, do not depend on the machine running the compiler.
    4. **A remark:** the same facts as one analysis remark in the remark stream from [O1](o1-optimizer-contract.md#for-vortex), marked as queried, not measured.

    **Not yet:** any use of these facts to change generated code (tile sizes are [P8](p8-cache-blocking.md#for-vortex)'s job, and searching for them [P15](p15-choosing-parameters.md)'s), latency measurement inside the compiler, and guesses at facts the system does not report.

    **Proof that it works:**

    - A script, not your eyes, compares every value `--explain` prints on your machine with the output of the command it names, and fails on any difference.
    - A golden test with the override file: the `--explain` output is byte-identical on every machine that runs it, CI included.
    - A planted gap: an override file with the L2 size missing makes `--explain` print "not reported" for it, and the test fails if any number appears instead.
    - On a Mac with two kinds of core, both are printed. The test fails if the only L1d reported comes from the short key `hw.l1dcachesize`, which on the owner's M4 Pro gives the efficiency cores' 64 KiB.
    - A build with `--explain` and one without produce byte-identical executables.
    - Run `pointer_chase.cpp --time` and fill in the table above. The steps in your curve should fall near the sizes `--explain` prints; where they do not, write down the reason you found.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is a cache line?** The fixed-size, aligned block of memory a cache stores and moves as one unit; a fetch costs a whole line, not one value.
    - **How does an address find its place in a set-associative cache?** Its low bits are the offset within the line, the next bits choose the set, and the high bits are the tag compared against the lines in that set's ways.
    - **What are the three kinds of miss?** Compulsory (first use of a line), capacity (the working set is larger than the cache) and conflict (too many lines in use map to one set).
    - **What is the difference between spatial and temporal locality?** Spatial locality uses addresses near one used a moment ago, often in the same line; temporal locality uses the same address again before its line is evicted.
    - **Why is the naive matmul loop's access to `b[k, column]` poor for the cache?** Consecutive `k` jump a whole row, so there is no spatial locality within a pass, and the line reused by the next `column` survives only if a whole column's lines fit in the cache, which fails once `N` lines outgrow L1.
    - **Why does a latency benchmark chase pointers in a random order?** Each load's address depends on the previous load, so loads cannot overlap, and a random order gives the prefetcher no pattern to follow.
    - **What does the TLB cache, and why can it miss when the data cache hits?** Virtual-to-physical translations, one per page; an access pattern spread over more pages than the TLB holds misses it even when the lines themselves are cached.

## Where this comes back

!!! next "You will use this again in"

    - [P3. The roofline model](p3-roofline.md): *working set*, *compulsory miss*, *bytes moved*
    - [P5. The microarchitecture shelf](p5-microarchitecture.md): *load latency*, *dependent loads*
    - [P7. Loop transformations](p7-loop-transformations.md): *spatial locality*, *temporal locality*
    - [P8. Cache blocking](p8-cache-blocking.md): *working set*, *conflict miss*, *way size*, *TLB reach*
    - [P12. Anatomy of a fast GEMM](p12-fast-gemm.md): *cache level*, *TLB*
    - [P13. Multithreading](p13-multithreading.md): *cache line*, *shared L2*
    - [G3. The GPU memory hierarchy](../gpu/g3-memory-hierarchy.md): *cache line*, *hierarchy level*

## Sources and further reading

Read Drepper. It is long, but section 3 covers everything in this chapter with
measurements, section 4 covers virtual memory and the TLB, and section 6.2.1
works through the matrix multiplication example step by step. The
measurements are from 2007 processors; the method has not aged.

[^drepper]: Ulrich Drepper, "What Every Programmer Should Know About Memory", Red Hat, 2007. Sections 3.2 (cache lines, the tag, set and offset split, loading a line before a write, the Pentium M access times), 3.3.1 (fully associative, direct-mapped and set-associative caches), 3.3.2 (working-set measurements on a Pentium 4, prefetching, the TLB experiment, random access), 3.3.3 (write-back), 3.3.5 (LRU replacement), 3.5.2 (compulsory and capacity misses, line sizes), 4.3 (the TLB), 6.2.1 (the matrix multiplication measurements on a Core 2, conflict misses) and 6.3.1 (hardware prefetching). <https://www.akkadia.org/drepper/cpumemory.pdf> (also serialized on LWN, starting at <https://lwn.net/Articles/250967/>)
[^local]: Queried by the author on the owner's Apple M4 Pro (macOS 27), 2026-09-24: `sysctl hw.cachelinesize` (128), `hw.perflevel0.l1dcachesize` (131072), `hw.perflevel0.l2cachesize` (16777216), `hw.perflevel0.cpusperl2` (4), `hw.perflevel0.physicalcpu` (8), `hw.perflevel1.l1dcachesize` (65536), `hw.perflevel1.l2cachesize` (4194304), `hw.l1dcachesize` (65536), `hw.pagesize` (16384); no L3, associativity or TLB keys appear in `sysctl -a`. Values describe this one machine; query them on any other.
[^sysfs]: The Linux kernel, "sysfs-devices-system-cpu", ABI documentation, entry for `/sys/devices/system/cpu/cpu*/cache/index*/` (attributes `coherency_line_size`, `number_of_sets`, `ways_of_associativity` and others). <https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-devices-system-cpu>
