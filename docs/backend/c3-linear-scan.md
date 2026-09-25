# C3. Register allocation I: linear scan

<p class="page-intro">Linear scan gives registers to live intervals in one sorted sweep, without ever building an interference graph. It is the allocator a fast compiler or a JIT reaches for first, and the one to build first in Vortex: simple enough to check by hand, fast enough to run on every function, and the base that production allocators extend.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 40 minutes · Builds on: [C2. Liveness](c2-liveness.md), [O2. Control-flow graphs and dominance](../optimize/o2-cfg-and-dominance.md)</p>

???+ remember "Before you start, remember"

    ??? question "Why must a live interval be computed from liveness, not from where a name is mentioned?"

        Because a value can be live where it is never mentioned. An array
        address read once inside a loop is live around the back edge, so its
        interval must cover the whole loop; the span of its mentions would
        let another value take its register in the middle of the loop.

        Introduced in [C2. Liveness](c2-liveness.md#live-ranges-and-live-intervals).

    ??? question "What is register pressure, and what does its largest value tell you?"

        The number of values live at a point. Its largest value over a
        function is a lower bound on the registers the code needs: above it,
        something must live in memory at that point.

        Introduced in [C2. Liveness](c2-liveness.md#where-a-register-becomes-free).

    ??? question "What is a lifetime hole?"

        A stretch inside a value's interval where the value is dead: nothing
        will read what it holds. A single `[start, end]` range cannot show the
        hole, so it claims a register there anyway.

        Introduced in [C2. Liveness](c2-liveness.md#live-ranges-and-live-intervals).

    ??? question "In reverse postorder, where does a block come relative to its predecessors?"

        After all of them, except a predecessor that reaches it along a back
        edge. In a loop, the header comes before the blocks of the body that
        jump back to it.

        Introduced in [O2. Control-flow graphs and dominance](../optimize/o2-cfg-and-dominance.md#visiting-the-blocks-in-order).

    ??? question "What may a callee do to a caller-saved register?"

        Overwrite it. A caller that needs the value after the call must keep
        it somewhere else: in a callee-saved register or on the stack.

        Introduced in [A4. Calling conventions and ABIs](a4-calling-conventions.md#caller-saved-and-callee-saved-registers).

!!! goals "In this chapter"

    - Run Poletto and Sarkar's linear scan by hand: sort intervals by start, expire, allocate, and spill the interval that ends furthest away.
    - Explain when the furthest-end rule is optimal, and show with a use count where it spills the wrong value.
    - Number instructions so that an instruction's last operand and its result do not overlap, and explain why the instruction order changes the quality of an allocation but not its correctness.
    - Describe what production linear-scan allocators add to the plain algorithm: lifetime holes, interval splitting, fixed registers, resolution moves and SSA form.
    - Test an allocator with an independent checker, random inputs and a cap on the register count.

## From a live value to a register

[C2](c2-liveness.md) computes, for every point in a function, which values are **live**: which ones some later instruction will read before anything overwrites them. Register allocation asks the next question. The machine has a small, fixed set of registers, and each holds one value at a time. Which register should hold each value, and which values must live in memory instead because no register is free?

Two values can share a register only if they are never live at the same point. Two values that are live at the same point **interfere**: giving them one register would let the second overwrite the first while the first is still needed. [C4](c4-graph-coloring.md) turns this into a graph problem, with one node per value and an edge per interfering pair, and colors the graph with one color per register. That approach sees interference exactly, but it pays for it: the interference graph can have a number of edges that grows with the square of the number of values.[^ps99]

A compiler that runs while someone waits cannot always pay that. A **just-in-time compiler** (JIT) translates a function while the program that calls it is running, so compile time adds to run time. Poletto and Sarkar designed **linear scan** for this setting: dynamic compilers, JITs and interactive development tools, where both compile time and code quality matter.[^ps99] It never builds a graph. It asks a cheaper question, "in what order do values start and stop being live?", and answers it with one sorted sweep.

## Live intervals: one range instead of a graph

Linear scan works on **live intervals**, which [C2](c2-liveness.md#live-ranges-and-live-intervals) built. Number the instructions in some order. A value's live interval is a range `[i, j]` such that the value is live at no instruction numbered before `i` and at none after `j`.[^ps99] The interval is a conservative summary: the value may be dead in parts of it, but it is certainly not live outside it.

Take six instructions with no branches, numbered by position:

```text
1  a = 2
2  b = 3
3  c = a + b
4  d = a * b
5  e = c + d
6  return e
```

`a` is written at 1 and read for the last time at 4, so its interval is `[1, 4]`. `b` is written at 2 and last read at 4: `[2, 4]`. `c` runs from 3 to its read at 5, `[3, 5]`; `d` from 4 to 5, `[4, 5]`; and `e` from 5 to the return at 6, `[5, 6]`.

Interference between two intervals is now a question about two pairs of numbers. `[s1, e1]` and `[s2, e2]` overlap when `s1 <= e2` and `s2 <= e1`: one comparison each way, with no graph to build or store.[^ps99] `a` at `[1, 4]` and `c` at `[3, 5]` overlap at 3 and 4, so they need different registers. `b` at `[2, 4]` and `e` at `[5, 6]` do not, so one register can hold `b` until 4 and `e` from 5.

The price of the summary is precision. An interval claims every number between its ends, including any lifetime hole, so two intervals can overlap even where the two values are never live together. Plain linear scan accepts that loss in exchange for speed; the section on [holes and splitting](#beyond-the-plain-algorithm) shows how later allocators win it back. There is also a smaller loss in how the positions are numbered, which the six instructions above already show and which [a later section](#numbering-positions-two-slots-per-instruction) fixes.

## The algorithm, on eight intervals and three registers

Linear scan visits the intervals once, in order of increasing start point. It keeps one list, **active**: the intervals that currently hold a register, sorted by increasing end point, so the one that ends soonest is at the front. A **pool** holds the registers no active interval is using. At each new interval, the **current** one, it makes three moves:[^ps99]

1. **Expire.** Walk `active` from the front. Every interval whose end point is before the current start point is finished: remove it and return its register to the pool. Stop at the first interval that ends at or after the current start, because `active` is sorted and every later interval ends later still.
2. **Allocate.** If the pool is not empty, give one of its registers to the current interval and insert the interval into `active` at the place its end point dictates.
3. **Spill.** If `active` already holds one interval per register, one of the competing intervals must go to memory. Linear scan picks whichever ends last: either the last interval in `active` or the current one. If an active interval ends later, the current interval takes its register and the active one is **spilled**, which here means it lives in a stack slot for its whole interval. Otherwise the current interval is spilled.

That is the whole algorithm. In pseudocode, in this book's own words:

```text
for each interval i, by increasing start:
    for each interval j in active, by increasing end:
        if end(j) >= start(i): stop
        remove j from active; return register(j) to the pool
    if active holds R intervals:
        last = the interval in active with the largest end
        if end(last) > end(i):
            register(i) = register(last); spill last
            remove last from active; insert i into active by end
        else:
            spill i
    else:
        register(i) = a register from the pool; insert i into active by end
```

Run it by hand on eight made-up intervals and three registers, `r0`, `r1` and `r2`. When several registers are free, take the lowest-numbered one.

| Interval | a | b | c | d | e | f | g | h |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Start | 1 | 2 | 3 | 5 | 6 | 7 | 8 | 10 |
| End | 8 | 4 | 9 | 6 | 10 | 7 | 12 | 11 |

The table below records each step: what expired, what the algorithm decided, and the state of `active` afterward, written as `name(end)` in the order the list keeps.

| Current | Expired | Decision | `active` afterward | Pool |
| --- | --- | --- | --- | --- |
| a `[1,8]` | none | `r0` | a(8) | r1, r2 |
| b `[2,4]` | none | `r1` | b(4), a(8) | r2 |
| c `[3,9]` | none | `r2` | b(4), a(8), c(9) | none |
| d `[5,6]` | b, since 4 < 5 | `r1` | d(6), a(8), c(9) | none |
| e `[6,10]` | none: d ends at 6, not before 6 | spill e: c ends at 9, e at 10 | d(6), a(8), c(9) | none |
| f `[7,7]` | d | `r1` | f(7), a(8), c(9) | none |
| g `[8,12]` | f | `r1` | a(8), c(9), g(12) | none |
| h `[10,11]` | a and c | `r0` | h(11), g(12) | r2 |

Two rows deserve a second look. At `d`, `b` has ended at 4, before `d` starts at 5, so its register goes back to the pool and `d` takes it. At `e`, `d` ends at 6, which is the same point where `e` starts; 6 is not before 6, so `d` is still live there and has not expired. All three registers are held. The candidate for spilling is the last interval in `active`, `c`, which ends at 9, but `e` ends at 10, later still. Giving `e` the register of `c` would keep the longer interval in a register, so the algorithm spills `e` itself and leaves `active` unchanged.

`linear-scan.cpp` implements the same three moves and prints every decision. Its output matches the table line for line.

--8<-- "includes/examples/backend/c3-linear-scan/linear-scan.cpp.md"

<figure class="vx-figure">
<svg viewBox="0 0 700 360" role="img" aria-label="Eight live intervals, a to h, drawn as bars over positions 1 to 12 and labeled with the register linear scan gives each. e is marked spilled. Column 6 is outlined because four intervals cover it." aria-describedby="c3-f1-desc">
<title id="c3-f1-title">Linear scan sweeping eight intervals with three registers</title>
<desc id="c3-f1-desc">Twelve columns, one per position. Each interval is a bar covering the columns from its start to its end: a from 1 to 8 in r0, b from 2 to 4 in r1, c from 3 to 9 in r2, d from 5 to 6 in r1, e from 6 to 10 spilled, f at 7 in r1, g from 8 to 12 in r1, h from 10 to 11 in r0. Column 6 is outlined: a, c, d and e all cover it, one more than there are registers. A vertical line travels from left to right, the order in which the algorithm meets the start points.</desc>
<rect class="vx-box-bad" x="300" y="30" width="44" height="276" fill-opacity="0"/>
<text class="vx-text-muted" x="322" y="22" text-anchor="middle" font-size="11">4 intervals, 3 registers</text>
<g class="vx-text-muted" font-size="11">
<text x="102" y="328" text-anchor="middle">1</text>
<text x="146" y="328" text-anchor="middle">2</text>
<text x="190" y="328" text-anchor="middle">3</text>
<text x="234" y="328" text-anchor="middle">4</text>
<text x="278" y="328" text-anchor="middle">5</text>
<text x="322" y="328" text-anchor="middle">6</text>
<text x="366" y="328" text-anchor="middle">7</text>
<text x="410" y="328" text-anchor="middle">8</text>
<text x="454" y="328" text-anchor="middle">9</text>
<text x="498" y="328" text-anchor="middle">10</text>
<text x="542" y="328" text-anchor="middle">11</text>
<text x="586" y="328" text-anchor="middle">12</text>
</g>
<line class="vx-line" x1="80" y1="310" x2="608" y2="310"/>
<text class="vx-text-muted" x="608" y="350" text-anchor="end" font-size="11">position</text>
<text class="vx-mono" x="50" y="57">a</text>
<rect class="vx-box-accent" x="80" y="40" width="352" height="24" rx="3"/>
<text class="vx-mono" x="256" y="57" text-anchor="middle" font-size="12">r0</text>
<text class="vx-mono" x="50" y="91">b</text>
<rect class="vx-box" x="124" y="74" width="132" height="24" rx="3"/>
<text class="vx-mono" x="190" y="91" text-anchor="middle" font-size="12">r1</text>
<text class="vx-mono" x="50" y="125">c</text>
<rect class="vx-box-strong" x="168" y="108" width="308" height="24" rx="3"/>
<text class="vx-mono" x="322" y="125" text-anchor="middle" font-size="12">r2</text>
<text class="vx-mono" x="50" y="159">d</text>
<rect class="vx-box" x="256" y="142" width="88" height="24" rx="3"/>
<text class="vx-mono" x="300" y="159" text-anchor="middle" font-size="12">r1</text>
<text class="vx-mono" x="50" y="193">e</text>
<rect class="vx-box-bad" x="300" y="176" width="220" height="24" rx="3"/>
<text class="vx-mono" x="410" y="193" text-anchor="middle" font-size="12">spilled</text>
<text class="vx-mono" x="50" y="227">f</text>
<rect class="vx-box" x="344" y="210" width="44" height="24" rx="3"/>
<text class="vx-mono" x="366" y="227" text-anchor="middle" font-size="12">r1</text>
<text class="vx-mono" x="50" y="261">g</text>
<rect class="vx-box" x="388" y="244" width="220" height="24" rx="3"/>
<text class="vx-mono" x="498" y="261" text-anchor="middle" font-size="12">r1</text>
<text class="vx-mono" x="50" y="295">h</text>
<rect class="vx-box-accent" x="476" y="278" width="88" height="24" rx="3"/>
<text class="vx-mono" x="520" y="295" text-anchor="middle" font-size="12">r0</text>
<g class="vx-travel" style="--vx-distance: 528px">
<line class="vx-line" x1="80" y1="34" x2="80" y2="306"/>
<circle class="vx-dot" cx="80" cy="34" r="5"/>
</g>
</svg>
<figcaption>Figure 1. The eight intervals, each bar covering its positions, labeled with the register <code>linear-scan.cpp</code> assigns. <code>b</code>, <code>d</code>, <code>f</code> and <code>g</code> never overlap, so all four use <code>r1</code> in turn. At position 6 four intervals are live and only three registers exist, so one of them must be in memory there; linear scan chooses <code>e</code>, the one that ends last.</figcaption>
</figure>

The outlined column in Figure 1 is a useful sanity check. At position 6, four intervals overlap. With three registers, at least one of those four values must be in memory at that point, whatever allocator is used: if n intervals overlap at some point and there are R registers, at least n − R of them must reside in memory.[^ps99] Linear scan spilled exactly one, so on this input it could not have done better.

??? check "If `d` started at 4 instead of 5, could it still take `r1` from `b`?"

    No. The expire step removes an interval only when its end is before
    the current start, and 4 is not before 4. So when `d` arrives at 4,
    `b` is still active and still holds `r1`, and `d` must look elsewhere.
    The two intervals overlap at 4: both values are needed at that
    position, so they cannot share a register.

### Your turn: two registers

Run the same eight intervals with only `r0` and `r1`. Work through the rows before opening the answer: which intervals are spilled, and does the result meet the lower bound from the overlap count?

??? check "Which intervals does linear scan spill with two registers, and is that the fewest possible?"

    `c` and `e`. At `c` (start 3), `active` holds `b(4)` and `a(8)`; `a`
    ends at 8, before `c`'s 9, so `c` is spilled. At `d`, `b` expires and
    `d` takes `r1`. At `e` (start 6), `active` holds `d(6)` and `a(8)`;
    `e` ends at 10, later than `a`, so `e` is spilled. `f`, `g` and `h`
    each find a free register once `d`, `f` and `a` expire. Four intervals
    overlap at position 6 (`a`, `c`, `d`, `e`), so with two registers at
    least two values must be in memory there. Two spills is the minimum.

## Why the interval that ends last

The spill rule is a **heuristic**: a rule of thumb that is fast to apply and usually good, without a promise of the best answer. Poletto and Sarkar chose it for the remaining length of the intervals. The interval that ends furthest away holds a register for the longest time to come, so giving up that interval frees a register for the longest stretch of future intervals.[^ps99]

The rule has a pedigree. In straight-line code where every value is defined once and used once, it spills the fewest values possible. Poletto and Sarkar trace this to Belady's replacement algorithm for virtual memory, which evicts the page that will be needed furthest in the future.[^ps99] Once intervals cover many uses spread over many blocks, that guarantee no longer holds, though Poletto and Sarkar report that the rule still works well in practice.

Spilling the newcomer every time is the obvious alternative, and it is worse. Suppose an interval `p` starts early and runs long, and several short intervals arrive while `p` holds a register. Spilling each short newcomer costs one spill per newcomer, while spilling `p` once frees its register for all of them. `spill-choice.cpp` runs linear scan on five intervals with two registers under three rules: spill the newcomer, spill the interval that ends last, and spill the interval with the fewest uses.

--8<-- "includes/examples/backend/c3-linear-scan/spill-choice.cpp.md"

The furthest-end rule spills one interval where the newcomer rule spills two. But the example also gives each interval a **use count**, the number of instructions that read or write it, standing in for how often the program touches it. A spilled value costs a memory access at each use, so the one interval the furthest-end rule gives up, `p`, is the most expensive one to lose: eight memory accesses, against four for the newcomer rule and three for the fewest-uses rule. The furthest-end rule minimizes how many values go to memory, not how many memory accesses the program then makes.

<figure class="vx-figure">
<svg viewBox="0 0 700 420" role="img" aria-label="Three copies of the same five intervals, p to t, over positions 1 to 10, one per spill rule, with the spilled bars marked. Newcomer spills r and t, furthest end spills p, fewest uses spills q and s." aria-describedby="c3-f2-desc">
<title id="c3-f2-title">Three spill rules on the same five intervals</title>
<desc id="c3-f2-desc">Three panels stacked vertically. Each shows five bars: p from 1 to 10 with 8 uses, q from 2 to 4 with 1 use, r from 3 to 5, s from 5 to 7 and t from 6 to 8, each with 2 uses. A vertical line at position 3 marks the first point where three intervals compete for two registers. In the newcomer panel, r and t are marked spilled: two intervals, four memory accesses. In the furthest-end panel, only p is marked spilled: one interval, eight memory accesses. In the fewest-uses panel, q and s are marked spilled: two intervals, three memory accesses.</desc>
<g font-size="11">
<text class="vx-text" x="20" y="24">spill the newcomer</text>
<text class="vx-mono" x="60" y="54">p</text><rect class="vx-box" x="90" y="42" width="300" height="16" rx="3"/><text class="vx-text-muted" x="400" y="54">8 uses</text>
<text class="vx-mono" x="60" y="74">q</text><rect class="vx-box" x="120" y="62" width="90" height="16" rx="3"/>
<text class="vx-mono" x="60" y="94">r</text><rect class="vx-box-bad" x="150" y="82" width="90" height="16" rx="3"/>
<text class="vx-mono" x="60" y="114">s</text><rect class="vx-box" x="210" y="102" width="90" height="16" rx="3"/>
<text class="vx-mono" x="60" y="134">t</text><rect class="vx-box-bad" x="240" y="122" width="90" height="16" rx="3"/>
<text class="vx-text-accent" x="470" y="94">2 intervals, 4 accesses</text>
<text class="vx-text" x="20" y="164">spill the interval that ends last</text>
<text class="vx-mono" x="60" y="194">p</text><rect class="vx-box-bad" x="90" y="182" width="300" height="16" rx="3"/><text class="vx-text-muted" x="400" y="194">8 uses</text>
<text class="vx-mono" x="60" y="214">q</text><rect class="vx-box" x="120" y="202" width="90" height="16" rx="3"/>
<text class="vx-mono" x="60" y="234">r</text><rect class="vx-box" x="150" y="222" width="90" height="16" rx="3"/>
<text class="vx-mono" x="60" y="254">s</text><rect class="vx-box" x="210" y="242" width="90" height="16" rx="3"/>
<text class="vx-mono" x="60" y="274">t</text><rect class="vx-box" x="240" y="262" width="90" height="16" rx="3"/>
<text class="vx-text-accent" x="470" y="234">1 interval, 8 accesses</text>
<text class="vx-text" x="20" y="304">spill the interval with the fewest uses</text>
<text class="vx-mono" x="60" y="334">p</text><rect class="vx-box" x="90" y="322" width="300" height="16" rx="3"/><text class="vx-text-muted" x="400" y="334">8 uses</text>
<text class="vx-mono" x="60" y="354">q</text><rect class="vx-box-bad" x="120" y="342" width="90" height="16" rx="3"/><text class="vx-text-muted" x="400" y="354">1 use</text>
<text class="vx-mono" x="60" y="374">r</text><rect class="vx-box" x="150" y="362" width="90" height="16" rx="3"/>
<text class="vx-mono" x="60" y="394">s</text><rect class="vx-box-bad" x="210" y="382" width="90" height="16" rx="3"/>
<text class="vx-mono" x="60" y="414">t</text><rect class="vx-box" x="240" y="402" width="90" height="16" rx="3"/>
<text class="vx-text-accent" x="470" y="374">2 intervals, 3 accesses</text>
</g>
<line class="vx-line" x1="165" y1="34" x2="165" y2="416" stroke-dasharray="4 4"/>
</svg>
<figcaption>Figure 2. The five intervals of <code>spill-choice.cpp</code> under three spill rules; spilled bars are marked. The dashed line at position 3 is the first point where three intervals compete for two registers. Spilling the long interval <code>p</code> once removes every later conflict, but <code>p</code> is also the most used, so the fewest spills is not the fewest memory accesses.</figcaption>
</figure>

Poletto and Sarkar measured this trade-off too. They tried spilling the interval with the smallest estimated use count, among the newcomer and the active intervals, instead of the one that ends last. On their benchmarks, compiled with Machine SUIF for a DEC Alpha 21164, the two rules gave similar run times except on `fpppp`, where the length rule ran in 90.8 seconds and the use-count rule in 198.6.[^ps99] Neither rule wins everywhere, because the right choice depends on where in the program the uses fall. [C5](c5-spilling.md) weighs each use by how deep in a loop it sits, which is what production allocators do.

??? check "Can plain linear scan spill anything when no position is covered by more than R intervals?"

    No. The spill step runs only when `active` already holds R intervals
    after expiring. Every interval left in `active` ends at or after the
    current start, and started at or before it, so all R of them cover the
    current start point, and so does the current interval: R + 1 intervals
    at one position. If no position has more than R, that never happens.

## Numbering positions: two slots per instruction

Go back to the six instructions at the start of the chapter. With one number per instruction, `a` and `b` are last read at 4 and `d` is written at 4, so all three intervals contain 4, and so does `c`: four intervals overlap there. Yet a machine instruction reads its operands before it writes its result. `mul x0, x0, x1` on AArch64 is fine, and [C2](c2-liveness.md#where-a-register-becomes-free) counted the true register pressure of code like this by the same reasoning. Only three values are ever live at once here, and with three registers the one-number intervals would force a spill that the code does not need.

The fix is to give each instruction two positions: an earlier one where it reads, and a later one where it writes. Wimmer and Mössenböck's allocator numbers instructions 2, 4, 6 and so on, so that a range can end between two instructions and the allocator can place spill loads and stores at the odd numbers in between.[^wm05] `positions.cpp` uses a variant of the same idea: instruction `i` reads at `2i` and writes at `2i + 1`. It builds the intervals both ways and reports the most intervals that cover any single number.

--8<-- "includes/examples/backend/c3-linear-scan/positions.cpp.md"

<figure class="vx-figure">
<svg viewBox="0 0 700 270" role="img" aria-label="The five intervals of positions.cpp drawn twice: with one number per instruction, four bars cover position 4; with two numbers per instruction, at most three bars cover any position, and d starts after a and b end." aria-describedby="c3-f3-desc">
<title id="c3-f3-title">One number per instruction against two</title>
<desc id="c3-f3-desc">Two panels. Left, one number per instruction, positions 1 to 6: a from 1 to 4, b from 2 to 4, c from 3 to 5, d from 4 to 5, e from 5 to 6; column 4 is outlined and holds four bars. Right, two numbers per instruction, positions 1 to 12: a from 3 to 8, b from 5 to 8, c from 7 to 10, d from 9 to 10, e from 11 to 12; column 8 is outlined and holds three bars, and d begins at 9, after a and b have ended.</desc>
<text class="vx-text" x="70" y="24">one number per instruction</text>
<text class="vx-text" x="400" y="24">two numbers per instruction</text>
<rect class="vx-box-bad" x="178" y="36" width="36" height="160" fill-opacity="0"/>
<rect class="vx-box-bad" x="554" y="36" width="22" height="160" fill-opacity="0"/>
<g font-size="11">
<text class="vx-mono" x="40" y="60">a</text><rect class="vx-box-accent" x="70" y="46" width="144" height="20" rx="3"/>
<text class="vx-mono" x="40" y="90">b</text><rect class="vx-box-accent" x="106" y="76" width="108" height="20" rx="3"/>
<text class="vx-mono" x="40" y="120">c</text><rect class="vx-box-accent" x="142" y="106" width="108" height="20" rx="3"/>
<text class="vx-mono" x="40" y="150">d</text><rect class="vx-box-accent" x="178" y="136" width="72" height="20" rx="3"/>
<text class="vx-mono" x="40" y="180">e</text><rect class="vx-box-accent" x="214" y="166" width="72" height="20" rx="3"/>
<rect class="vx-box-accent" x="444" y="46" width="132" height="20" rx="3"/>
<rect class="vx-box-accent" x="488" y="76" width="88" height="20" rx="3"/>
<rect class="vx-box-accent" x="532" y="106" width="88" height="20" rx="3"/>
<rect class="vx-box-accent" x="576" y="136" width="44" height="20" rx="3"/>
<rect class="vx-box-accent" x="620" y="166" width="44" height="20" rx="3"/>
<g class="vx-text-muted">
<text x="88" y="214" text-anchor="middle">1</text><text x="124" y="214" text-anchor="middle">2</text><text x="160" y="214" text-anchor="middle">3</text><text x="196" y="214" text-anchor="middle">4</text><text x="232" y="214" text-anchor="middle">5</text><text x="268" y="214" text-anchor="middle">6</text>
<text x="411" y="214" text-anchor="middle">1</text><text x="455" y="214" text-anchor="middle">3</text><text x="499" y="214" text-anchor="middle">5</text><text x="543" y="214" text-anchor="middle">7</text><text x="587" y="214" text-anchor="middle">9</text><text x="631" y="214" text-anchor="middle">11</text>
</g>
<text class="vx-text-accent" x="196" y="244" text-anchor="middle">4 cover position 4</text>
<text class="vx-text-accent" x="565" y="244" text-anchor="middle">at most 3 anywhere</text>
</g>
</svg>
<figcaption>Figure 3. The same five values, numbered two ways. With one number per instruction, the last reads of <code>a</code> and <code>b</code> and the write of <code>d</code> share position 4, so four intervals overlap. With a read slot and a write slot per instruction, <code>d</code> starts after <code>a</code> and <code>b</code> end, and the interval count matches the true register pressure of three.</figcaption>
</figure>

One detail has to survive the change. The read-then-write order holds for most instructions, but not for one that writes part of its result before it has finished reading its inputs. Such an output is **early-clobber**: it must not share a register with any input. LLVM's inline assembly has a marker for exactly this, and LLVM then keeps the output out of every input's register.[^langref] In interval terms, an early-clobber result starts at the read position, not the write position. Vortex's first back end can ignore the case until an instruction needs it, but the numbering should leave room for it.

## Ordering instructions for a scan

Linear scan needs a numbering of instructions, and a function with branches and loops has no single natural one. Poletto and Sarkar state the key fact plainly: the choice of order does not affect the correctness of the algorithm, only the quality of the allocation.[^ps99] The reason is in the definition. An interval is computed from liveness over whatever numbering is chosen, and it covers every numbered point where the value is live. Two values live at the same point therefore always get overlapping intervals, in any order, and the allocator keeps them apart.

What the order changes is how much each interval over-claims. [C2](c2-liveness.md#live-ranges-and-live-intervals) showed a value defined before an `if`, read only in the `else` branch, with the `then` branch laid out in between: the interval covers the whole `then` branch, a hole it did not need. Poletto and Sarkar used **depth-first order**, which is reverse postorder of the control-flow graph; they also tried the order in which instructions appear in the program text, and on their benchmarks the two produced roughly similar code.[^ps99]

Loops need a property of their own. A value that is live at a loop's header, because it was defined before the loop and is used inside it or after it, is live in every block of the loop: each iteration reaches the next through the back edge.[^wf10] If the loop's blocks are contiguous in the order, one range from the header to the loop's last block covers all of that.

Wimmer and Franz use an order with two guarantees: every block comes after its predecessors except along back edges, and the blocks of one loop are contiguous, with no other block between them.[^wf10] The earlier allocator of Wimmer and Mössenböck also moves rarely executed blocks, such as exception handlers, to the end of the function, so they do not stretch the intervals of the hot code.[^wm05]

??? check "A loop's blocks are laid out as header, body, then an error-report block, then the latch that jumps back to the header. What happens to the intervals of values used in the loop?"

    Every value live at the header must be live in every block of the
    loop, including the latch, so its interval stretches across the
    error-report block that sits between the body and the latch. The
    allocator reserves registers there even if that block reads only
    constants. The result is still correct; it is only worse, because
    those intervals now overlap anything live in the error block. Moving
    the error block after the latch, outside the loop, removes the
    over-claim.

## What one pass costs

The expire step touches each finished interval once, plus one interval where it stops. The spill step looks only at the last interval in `active`. The expensive part is inserting an interval into `active` at the right place: with a balanced tree that costs O(log R) for R registers, so the whole scan over V intervals costs O(V log R), and with a plain linear search it costs O(V × R), which Poletto and Sarkar used because R is small in practice.[^ps99] Sorting the intervals by start point adds O(V log V) if they are not produced in order already.

Their measurements show where that matters, each tied to its setting. On programs built to stress allocators, with n values all live at once, the whole code generation with linear scan was over 600 times faster than with graph coloring at n = 512, measured in the `tcc` dynamic compiler on a 168 MHz UltraSPARC-I; graph coloring suffered from the quadratic cost of building and coloring the graph.[^ps99] On SPEC benchmarks compiled with Machine SUIF for a 500 MHz Alpha 21164, code from linear scan ran within 12% of the speed of code from an iterated-coalescing graph allocator for all but two benchmarks.[^ps99]

The same paper also points at the part of linear scan that is not cheap. In the `tcc` measurements most of the allocator's time went into liveness analysis and into turning liveness into intervals, not into the scan itself.[^ps99] A fast allocator needs a fast liveness pass: C2's worklist, or the SSA shortcuts at the end of [C2](c2-liveness.md#what-ssa-form-buys).

To see the cost in your own compiler, measure rather than assume. Time the liveness pass, the interval construction and the scan separately over a large generated function, at several sizes, and fill in a table like this one with your machine and date:

| Values live at once | Instructions | Liveness | Intervals | Scan |
| --- | --- | --- | --- | --- |
| | | | | |
| | | | | |

## Calls and fixed registers

Real machines constrain which register a value may use. Arguments and return values arrive in specific registers ([A4](a4-calling-conventions.md#where-arguments-go-two-counters-not-one)), some instructions require an operand in a particular register, and a call destroys every caller-saved register. Plain linear scan knows nothing of this, and Poletto and Sarkar describe two ways to add it.[^ps99]

For an operand that must be in a particular register, **pre-allocate** it: give its interval that register before the scan, and when the scan meets it, spill (or move to another register) whichever active interval holds that register. For calls, one option is to allocate as if calls did not exist and then insert saves and restores around each call for the caller-saved registers that hold live values. The other is to treat each caller-saved register as unavailable at every call, so that only an interval that crosses no call may use it.

Wimmer and Mössenböck make the second option concrete with **fixed intervals**: one per physical register, covering the positions where that register is not available. A call adds a short range at the call's position to the fixed interval of every register the call destroys, so an interval that spans the call cannot hold any of them there.[^wm05] On AArch64 that means the caller-saved registers only; a value live across a call can still stay in a callee-saved register such as `x19`, at the cost of saving and restoring that register in the prologue and epilogue ([A5](a5-stack-frames.md)).

## Beyond the plain algorithm

Plain linear scan gives each value one interval and one location for its whole life: a register or a stack slot. That makes it simple, and it has a pleasant consequence: because a value never changes location, no moves are needed where control flow joins. It also wastes registers in two ways that later allocators recover.

The first waste is holes. Wimmer and Mössenböck store an interval as a list of disjoint ranges, so a hole is visible.[^wm05] At each position, besides the active intervals, they keep **inactive** intervals: those that started before the position and end after it but have a hole there. A register held by an inactive interval is free during the hole, and the allocator may lend it to an interval that fits inside the hole.[^wm05]

The second waste is spilling a whole interval when a register was missing for only part of it. **Interval splitting** cuts an interval into pieces, each allocated separately: a value can start in a register, move to a stack slot while registers are scarce, and come back to a register before its next use.[^wm05]

Wimmer and Mössenböck's allocator splits in two situations. If a register is free only for the first part of the current interval, it assigns that register and splits the interval where the register stops being free. If no register is free at all, it spills whichever interval, of the current one and those holding registers, is not used for the longest time, and splits the spilled interval before its next use, where it will be reloaded.[^wm05]

Splitting brings its own bill. Once a value can live in different places in different parts of the code, two blocks joined by an edge may disagree about where the value is: in a register at the end of one, on the stack at the start of the next. A **resolution** pass visits every control-flow edge after allocation and inserts moves wherever the locations differ, ordering them carefully when one register is both a source and a destination.[^wm05] [O3](../optimize/o3-ssa.md#sequentializing-a-parallel-copy) sequentialized the same kind of parallel copy.

Two earlier and later steps round out the family. Traub, Holloway and Smith's **second-chance binpacking** already allowed a value's lifetime to be split several times, tracked lifetime holes of both values and registers, and needed its own resolution pass; Poletto and Sarkar found it produced somewhat better code than plain linear scan at two to three times the allocation time.[^ths98] [^ps99] Wimmer and Franz later ran linear scan directly on SSA form. With one definition per value and a block order that keeps loops contiguous, they build intervals in one backward pass without a liveness fixed point, skip some intersection tests that SSA makes unnecessary, and fold SSA destruction into the resolution pass.[^wf10]

<figure class="vx-figure">
<svg viewBox="0 0 700 250" role="img" aria-label="At one position of the scan, four intervals in four states: one handled because it ended, one active because it covers the position, one inactive because it has a hole there, and one unhandled because it starts later. A fifth interval is split: a register part, a stack part and a reloaded register part." aria-describedby="c3-f4-desc">
<title id="c3-f4-title">Interval states and a split interval</title>
<desc id="c3-f4-desc">A vertical dashed line marks the current position at 7. Interval u ends at 4, before the line: handled. Interval v covers the line: active. Interval w has two ranges, 2 to 5 and 9 to 12, with a hole across the line: inactive, so its register is free during the hole. Interval x starts at 10: unhandled. Interval y is split: in a register from 1 to 6, in a stack slot from 6 to 10, and reloaded into a register from 10 to 12.</desc>
<line class="vx-line" x1="344" y1="20" x2="344" y2="210" stroke-dasharray="5 4"/>
<text class="vx-text-muted" x="344" y="228" text-anchor="middle" font-size="11">current position</text>
<g font-size="11">
<text class="vx-mono" x="30" y="46">u</text><rect class="vx-box" x="80" y="32" width="176" height="20" rx="3"/><text class="vx-text-muted" x="560" y="46">handled: ended</text>
<text class="vx-mono" x="30" y="84">v</text><rect class="vx-box-accent" x="168" y="70" width="308" height="20" rx="3"/><text class="vx-text-muted" x="560" y="84">active: covers it</text>
<text class="vx-mono" x="30" y="122">w</text><rect class="vx-box-accent" x="124" y="108" width="176" height="20" rx="3"/><rect class="vx-box-bad" x="300" y="108" width="132" height="20" rx="3" fill-opacity="0" stroke-dasharray="4 3"/><rect class="vx-box-accent" x="432" y="108" width="176" height="20" rx="3"/><text class="vx-text-muted" x="366" y="122" text-anchor="middle">hole</text><text class="vx-text-muted" x="620" y="122">inactive</text>
<text class="vx-mono" x="30" y="160">x</text><rect class="vx-box" x="476" y="146" width="132" height="20" rx="3"/><text class="vx-text-muted" x="620" y="160">unhandled</text>
<text class="vx-mono" x="30" y="198">y</text><rect class="vx-box-strong" x="80" y="184" width="220" height="20" rx="3"/><rect class="vx-box-bad" x="300" y="184" width="176" height="20" rx="3"/><rect class="vx-box-strong" x="476" y="184" width="132" height="20" rx="3"/>
<text class="vx-mono" x="190" y="198" text-anchor="middle">register</text><text class="vx-mono" x="388" y="198" text-anchor="middle">stack slot</text><text class="vx-mono" x="542" y="198" text-anchor="middle">register</text>
</g>
</svg>
<figcaption>Figure 4. What an interval-splitting allocator tracks at one point of the scan. Beside the active intervals it keeps inactive ones, whose register is free during their hole. A split interval <code>y</code> changes location: a store where it leaves its register, a load before its next use. Plain linear scan has only the handled, active and unhandled states and never splits.</figcaption>
</figure>

## Linear scan in production compilers

Linear scan has run in compilers people use every day. LLVM made it the default register allocator in 2004 and replaced it with the **greedy** allocator in LLVM 3.0. Olesen's announcement explains why: the rewriter that cleaned up after spills accounted for about half of linear scan's compile time, and full live-range splitting was impractical, because it would need backtracking over earlier decisions, which linear scan's active list makes expensive. He reports the greedy allocator's code as 1 to 2% smaller and up to 10% faster than linear scan's, without naming the benchmarks.[^olesen11] Greedy keeps live intervals and checks interference against them rather than building a graph; [E3](e3-llvm-allocator-scheduler-mc.md#the-greedy-allocator) reads it.

The Java HotSpot client compiler adopted the splitting linear scan described above. Wimmer and Mössenböck report that it compiled as fast as the local allocator of the Sun JDK 5.0 client compiler, and that integer benchmarks ran about 15% faster.[^wm05] Cranelift's earlier allocator library, regalloc.rs, offered a linear-scan algorithm alongside a backtracking one, and its developers checked both with the symbolic checker described in [Checking an allocation independently](#checking-an-allocation-independently).[^fallin21] [D3](d3-real-backends.md) reads the allocators of several real back ends side by side.

## Applying it to the Vortex matmul kernel

Stage 10's kernel is the running example of this part of the book:[^stage10]

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

[C2](c2-liveness.md#your-turn-the-kernels-k-loop) wrote the innermost loop as blocks P (before the loop), H (the header), K1 and K2 (the two bounds checks), K3 (the arithmetic), S (the step), X (after the loop) and R (the error report), and worked out where each value is live. Lay those blocks out in that order, with R last, and the intervals follow from C2's answer. The three array addresses and the two outer counters, `a`, `b`, `c`, `row` and `column`, are live at H, so they cover the whole loop; `c`, `row` and `column` are also read in X. `k1` runs from H to S. `sum1` runs from H to X but is dead in S, a hole the single interval covers anyway.

<figure class="vx-figure">
<svg viewBox="0 0 700 300" role="img" aria-label="Intervals of the kernel's k loop over the block order P, H, K1, K2, K3, S, X, R. Five long-lived values cover P to X, k1 covers H to S, sum1 covers H to X with a hole at S, sum2 covers K3 to S, k2 covers S. Nothing reaches R." aria-describedby="c3-f5-desc">
<title id="c3-f5-title">The kernel's k loop as intervals</title>
<desc id="c3-f5-desc">Eight columns, one per block: P, H, K1, K2, K3, S, X and R. A bracket over H to S marks the loop's blocks, which are contiguous. One bar labeled "a, b, c, row, column (each)" covers P through X and continues past X, because the outer loops still need them. k0 and sum0 cover P only. k1 covers H to S. sum1 covers H to X, with S drawn as a hole. sum2 covers K3 to S. k2 covers S. Column K3 is labeled: seven values live on entry, before temporaries. Column R holds no bars.</desc>
<g font-size="11">
<g class="vx-text-muted">
<text x="162" y="24" text-anchor="middle">P</text><text x="226" y="24" text-anchor="middle">H</text><text x="290" y="24" text-anchor="middle">K1</text><text x="354" y="24" text-anchor="middle">K2</text><text x="418" y="24" text-anchor="middle">K3</text><text x="482" y="24" text-anchor="middle">S</text><text x="546" y="24" text-anchor="middle">X</text><text x="610" y="24" text-anchor="middle">R</text>
</g>
<line class="vx-line" x1="194" y1="36" x2="514" y2="36"/>
<line class="vx-line" x1="194" y1="32" x2="194" y2="40"/><line class="vx-line" x1="514" y1="32" x2="514" y2="40"/>
<text class="vx-text-muted" x="354" y="52" text-anchor="middle">loop blocks, contiguous</text>
<text class="vx-mono" x="10" y="80">a b c row column</text><rect class="vx-box-accent" x="130" y="66" width="448" height="20" rx="3"/><text class="vx-text-muted" x="596" y="80">and on</text>
<text class="vx-mono" x="10" y="112">k0, sum0</text><rect class="vx-box" x="130" y="98" width="64" height="20" rx="3"/>
<text class="vx-mono" x="10" y="144">k1</text><rect class="vx-box" x="194" y="130" width="320" height="20" rx="3"/>
<text class="vx-mono" x="10" y="176">sum1</text><rect class="vx-box-strong" x="194" y="162" width="256" height="20" rx="3"/><rect class="vx-box-bad" x="450" y="162" width="64" height="20" rx="3" fill-opacity="0" stroke-dasharray="4 3"/><rect class="vx-box-strong" x="514" y="162" width="64" height="20" rx="3"/><text class="vx-text-muted" x="482" y="176" text-anchor="middle">hole</text>
<text class="vx-mono" x="10" y="208">sum2</text><rect class="vx-box-strong" x="386" y="194" width="128" height="20" rx="3"/>
<text class="vx-mono" x="10" y="240">k2</text><rect class="vx-box" x="450" y="226" width="64" height="20" rx="3"/>
<rect class="vx-box-bad" x="386" y="60" width="64" height="192" fill-opacity="0"/>
<text class="vx-text-accent" x="418" y="274" text-anchor="middle">7 live on entry</text>
<text class="vx-text-accent" x="418" y="290" text-anchor="middle">plus temporaries</text>
</g>
</svg>
<figcaption>Figure 5. The kernel's <code>k</code> loop in the block order P, H, K1, K2, K3, S, X, R, drawn at block granularity from C2's liveness. Values live at the loop header cover the whole contiguous loop. <code>sum1</code> is dead in S, but its single interval covers S. The error block R comes last, so no interval has to stretch across it. Integer and floating-point values are allocated from separate register files, so the seven long-lived values compete in two separate scans.</figcaption>
</figure>

Count what competes for each register file at K3. The general-purpose file holds `a`, `b`, `c`, `row`, `column` and `k1`, plus short-lived address arithmetic for `a[row, k1]` and `b[k1, column]`. The floating-point file holds `sum1` plus the two loaded elements and their product. Linear scan runs once per **register class**, the set of registers an operand may use, so the two files never compete. AArch64 has 31 general-purpose registers, a few of them reserved by the platform ([A2](a2-aarch64-assembly.md#registers-by-name)), and 32 floating-point registers ([A1](a1-machine-model.md#registers-names-for-the-fastest-storage-a-processor-has)). As long as the temporaries stay few, the largest overlap in each file is far below its register count, so by the check question on overlap above, linear scan will not spill in this kernel.

Something is missing from the list: the loop bounds. Every dimension in a Vortex array type is fixed at compile time,[^stage10] so the bounds 2, 2 and 3 are constants. A compare such as `cmp x9, #3` takes a small constant as an immediate operand ([A2](a2-aarch64-assembly.md#flags-and-conditions)), so the bound never needs a register. In a language whose array sizes are known only at run time, each bound would be one more interval spanning its loop.

The placement of R matters too. If a bounds-check failure branched to a report block laid out inside the loop, every interval live at H would have to cover it, as the check question on block order showed. Placing cold blocks after the loop keeps the loop's intervals as short as its liveness allows.

??? check "Why does `row` cover the whole innermost loop, although the loop does not change it?"

    Because the loop reads it: the address of `a[row, k1]` depends on
    `row` in every iteration, and X reads it again for `c[row, column]`.
    A value read inside a loop and defined before it is live at the loop's
    header, so it is live in every block of the loop, and its interval
    covers them all. Only an optimizer that computed the row's base
    address once before the loop ([O6](../optimize/o6-redundancy.md))
    would let `row` die before the loop starts.

## Checking an allocation independently

An allocator can be subtly wrong in ways that reading its output does not reveal: an off-by-one in the expire test shows up only as two values sharing a register somewhere in a long function, and the program computes a wrong number much later. The remedy used throughout this book applies here: write a second, much simpler program whose only job is to check the first one's answer.

For linear scan the checker follows from the definition of interference. Given a finished assignment, a list of intervals each with a register or "spilled", confirm that no two intervals holding the same register overlap. `allocation-checker.cpp` runs that test on the assignment `linear-scan.cpp` produced, and then on a copy with one register changed by hand, to show it catching the conflict.

--8<-- "includes/examples/backend/c3-linear-scan/allocation-checker.cpp.md"

This checker trusts the intervals it is given. If interval construction is wrong, say a loop-carried value's interval stops short of the back edge, the allocator and the checker agree on the same wrong answer. A stronger checker looks at the program after allocation instead. Fallin describes one for Cranelift: it walks the allocated code as an abstract interpreter, recording for each register and stack slot which original value it provably holds, and flags any instruction that reads a location not holding the value the original program read. It runs against randomly generated programs as a fuzzing oracle.[^fallin21] [E4](e4-testing-backends.md) builds on the same idea.

## For Vortex

!!! vortex "Exercise"

    **Build.** A linear-scan register allocator for your compiler's machine-level code, one scan per register class.

    - Choose a block order in which every block follows its predecessors except along back edges, the blocks of each loop are contiguous, and the error-report blocks come last. Number the instructions with separate read and write positions.
    - Build one interval per virtual register from the liveness you computed in [C2](c2-liveness.md), so that a value live at a loop header covers the whole loop.
    - Run the plain algorithm: sort by start, expire, allocate, and spill the interval that ends last. A spilled value lives in its own stack slot for its whole life; decide how its uses get a register for the moment they need one, and account for those registers before the scan.
    - Keep values that are live across a call out of caller-saved registers, using either of the two approaches in [Calls and fixed registers](#calls-and-fixed-registers).
    - Write an allocation checker that is separate from the allocator: it receives the intervals and the assignment and reports any two intervals that share a register and overlap.

    **Do not build yet.** Lifetime holes, interval splitting and resolution moves ([C5](c5-spilling.md)); allocation on SSA form; coalescing of copies ([C4](c4-graph-coloring.md)); use-count or loop-depth spill weights. The plain algorithm is the baseline those improvements will be measured against.

    **The test that proves it works.**

    1. Run the checker on every function your test suite compiles, including stage 10's `multiply`: zero conflicts.
    2. Fuzz the allocator and checker together: generate thousands of random interval sets (random starts and lengths, one to eight registers), allocate each, and check each. Also assert that the spill count is at least the largest overlap minus the register count, and zero when the largest overlap fits.
    3. Add a debug option that caps the number of allocatable registers. With the cap at three or four, compile and run every end-to-end test, including the matmul program; every output must match the output without the cap. This is the test that exercises your spill code.
    4. With no cap, print the spill count for `multiply` and compare it with the largest overlap your intervals report. By the argument in this chapter, the count must be zero; if it is not, find out which interval is longer than C2's liveness says it should be.

## Key ideas

!!! recap

    - **What does linear scan work on, instead of an interference graph?** Live intervals over one numbering of the instructions; two values may share a register exactly when their intervals do not overlap.
    - **What are its three moves at each interval?** Expire the active intervals that ended before the new start, allocate a free register if there is one, otherwise spill whichever of the new interval and the active intervals ends last.
    - **When is the furthest-end rule optimal, and what does it ignore?** It spills the fewest values in straight-line code with one definition and one use per value; it ignores how often a value is used, so it may spill the most expensive one.
    - **Why give each instruction a read position and a write position?** So an operand read for the last time and the result written by the same instruction do not overlap, and the intervals do not overstate register pressure.
    - **Does the instruction order affect correctness?** No: intervals computed from liveness over any order keep interfering values apart; the order affects how much each interval over-claims, and so the quality.
    - **What do production linear-scan allocators add?** Intervals with holes and an inactive set, splitting with reloads, fixed intervals for calls and register constraints, a resolution pass on control-flow edges, and in some cases SSA form.
    - **How do you know an allocator is right?** Check every result with a separate overlap checker, fuzz both on random inputs, and run the whole test suite with the register count capped so that spill code runs.

## Where this comes back

!!! next "You will use this again in"

    - [C4. Register allocation II: graphs and SSA](c4-graph-coloring.md): *interference*, *register class*, *spill*
    - [C5. Spilling, splitting and rematerialization](c5-spilling.md): *interval splitting*, *lifetime hole*, *use count*
    - [C6. Instruction scheduling](c6-scheduling.md): *register pressure*, *instruction order*
    - [D3. Reading real back ends](d3-real-backends.md): *linear scan*, *allocation checker*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *live interval*, *greedy allocation*, *splitting*
    - [E4. Testing back ends](e4-testing-backends.md): *allocation checker*, *fuzzing*

## Sources and further reading

Poletto and Sarkar's paper is short and readable, and its Figure 1 is the whole algorithm; read it first. Wimmer and Mössenböck's paper is the best account of what a production linear-scan allocator adds, and Wimmer and Franz show the SSA version. The examples on this page are original code written against those descriptions.

[^ps99]: Massimiliano Poletto and Vivek Sarkar, "Linear Scan Register Allocation", ACM TOPLAS 21(5), 1999, sections 1 to 6. <https://doi.org/10.1145/330249.330250> (author copy: <http://web.cs.ucla.edu/~palsberg/course/cs132/linearscan.pdf>)
[^wm05]: Christian Wimmer and Hanspeter Mössenböck, "Optimized Interval Splitting in a Linear Scan Register Allocator", VEE 2005, sections 1 to 3. <https://doi.org/10.1145/1064979.1064998> (author copy: <https://ssw.jku.at/Research/Papers/Wimmer05/Wimmer05.pdf>)
[^wf10]: Christian Wimmer and Michael Franz, "Linear Scan Register Allocation on SSA Form", CGO 2010, sections 1 to 4. <https://doi.org/10.1145/1772954.1772979> (author copy: <http://www.christianwimmer.at/Publications/Wimmer10a/Wimmer10a.pdf>)
[^ths98]: Omri Traub, Glenn Holloway and Michael D. Smith, "Quality and Speed in Linear-scan Register Allocation", PLDI 1998. <https://doi.org/10.1145/277650.277714>
[^olesen11]: Jakob Stoklund Olesen, "Greedy Register Allocation in LLVM 3.0", LLVM Project Blog, 18 September 2011. <https://blog.llvm.org/2011/09/greedy-register-allocation-in-llvm-30.html>
[^fallin21]: Chris Fallin, "Cranelift, Part 3: Correctness in Register Allocation", 15 March 2021. <https://cfallin.org/blog/2021/03/15/cranelift-isel-3/>
[^langref]: LLVM Project, "LLVM Language Reference Manual", section "Output constraints" of inline assembler constraint strings. <https://llvm.org/docs/LangRef.html#output-constraints>
[^stage10]: [Build v0.1, stage 10, "The program the milestone asks for"](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for); array dimensions are compile-time constants by [Arrays, decision 11](../decisions/arrays.md#d11) and [Arrays and shapes, 7.2](../specification/arrays.md#72-dimension-rules).
