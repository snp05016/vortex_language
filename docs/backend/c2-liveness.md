# C2. Liveness

<p class="page-intro">Register allocation needs to know, at every point in a program, which values are still worth keeping. This chapter computes that fact at instruction granularity, turns it into the live intervals and interference graphs the next three chapters build on, and shows the shortcuts SSA form allows.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 30 minutes · Builds on: [C1. Instruction selection](c1-instruction-selection.md), [O4. Dataflow analysis](../optimize/o4-dataflow.md).</p>

???+ remember "Before you start, remember"

    ??? question "What are the two equations that define a block's live-in and live-out sets, and which way does liveness flow?"

        Backward: out(b) is the union of in(s) over b's successors, and in(b)
        is use(b) union (out(b) minus def(b)). Facts travel from where a
        block's successors stand back toward the block itself.

        Introduced in [O4. Dataflow analysis](../optimize/o4-dataflow.md#summaries-and-equations).

    ??? question "For a backward analysis solved by round-robin passes, which visiting order tends to converge fastest?"

        Postorder: visit a block only after every block reachable from it (its
        successors, and anything past them) has already been visited once in
        this pass, the mirror image of the order a forward analysis like
        dominance prefers.

        Introduced in [O2. Control-flow graphs and dominance](../optimize/o2-cfg-and-dominance.md#visiting-the-blocks-in-order).

    ??? question "In SSA form, what is guaranteed about the relationship between a definition and every one of its uses?"

        The definition dominates every use: on any path that reaches a use,
        the definition has already run. A phi's operand is the one exception
        in position only, not in this rule; O3 places its use on the
        incoming edge, which the definition still dominates.

        Introduced in [O3. SSA form: construction and destruction](../optimize/o3-ssa.md#how-many-phis).

    ??? question "Why does an inline bounds check show up in the compiled program as an ordinary two-way branch, not as a call that vanishes until it fails?"

        Because I8's choice puts every check directly in the instruction
        stream: one edge continues, the other reaches the report. A check
        hidden inside a runtime call would hide its operands from the
        optimizer too.

        Introduced in [O5. Constants and dead code](../optimize/o5-constants-and-dead-code.md#checks-are-branches).

!!! goals "In this chapter"

    - Compute live-in and live-out at the granularity of one instruction, not one block, and explain why register allocation needs that finer grain.
    - Build a live interval from exact liveness, and recognize a hole: a point an interval claims as live that is not.
    - Build an interference graph from liveness and state the one rule it rests on.
    - Explain, and implement, the two ways SSA form lets liveness skip the general fixed point: path exploration and the loop-nesting-forest method.
    - Trace live ranges through the Vortex matrix multiplication kernel's inner loop and say which of its values cross a call.

## From block sets to instruction points

[O4](../optimize/o4-dataflow.md#liveness-by-hand) computed live variables one block at a time: a **use** set for what a block reads before it writes, a **def** set for what it writes, and the equations that push those sets backward across the control-flow graph until they stop changing. That answer is exactly what a block-level client needs. It is not enough for register allocation, which has to know when a value's register can be handed to something else, and blocks are usually too coarse for that.

Take a single Vortex block: three instructions, `t0 = a + b`, `t1 = a * b`, `store out, t0`. Block-level liveness says `a` and `b` are live-in and nothing is live-out. It says nothing about the middle: is `a` still needed after `t0 = a + b`, once `t1 = a * b` has also read it? Both instructions read `a`, so yes, but a value defined earlier in the same block and not needed by anything after its own last use should free its register right there, in the middle of the block, and a set that only exists at the block's two ends cannot say when. Register allocation runs after instruction selection ([C1](c1-instruction-selection.md) chose the concrete instructions), and every one of those instructions is a point where a register might become free.

The fix costs nothing new. Treat each instruction as its own tiny block, one instruction long, and the same backward equations from O4 apply unchanged: live-out of an instruction is live-in of the next, and live-in is `use ∪ (live-out \ def)`.[^cmu-live] A block with no internal branches has no join to wait for, so one backward pass over its instructions, from the last to the first, is already the fixed point; O4 says the same about a whole graph with no loops. `instruction_liveness.cpp` runs that pass over six three-address instructions computing `(a + b) * (a - b) + a * a`:

--8<-- "includes/examples/backend/c2-liveness/instruction_liveness.cpp.md"

Reading the table: `a` stays live from instruction 0 through instruction 3, because `t3 = a * a` is the last thing that reads it, even though two unrelated products (`t0`, `t1`) are computed in between. `t0` and `t1` are each live for exactly one instruction, the one right after they are defined. The last line reports the **peak** live-in size, three names at once (instruction 1 or 2), which is a lower bound on how many registers this block needs: fewer than three, and something must spill or reuse a register the moment two of those three are still both wanted.

The same walk applies to the Vortex matmul kernel's inner loop, `sum += a[row, k] * b[k, column]`. Once instruction selection has turned that into loads, a multiply and an add, the loop's accumulator `sum` is live-in and live-out of every one of those instructions on every iteration, because the next iteration reads it before writing it again; the two loaded values are each live for only the instructions between their load and the multiply that consumes them. A tight inner loop's register pressure is exactly this kind of count, made precise per instruction instead of guessed per block.

??? check "A block's use set lists `a`. Does that mean `a` is read in every instruction of the block?"

    No. The use set only says `a` is read before any instruction in the
    block writes it: it might be read once, at the very first instruction,
    and never touched again inside the block. Per-instruction liveness is
    what tells you exactly which instructions still need it.

## Live ranges, intervals and holes

A value's **live range** is the exact set of points where it is live, computed the way the previous section did it. Some clients want less detail: [C3](c3-linear-scan.md)'s linear scan allocator sweeps once through a numbered list of instructions and wants each name's story summarized as one **interval**, its first definition to its last use, so that "is this name live at instruction *n*" becomes one comparison instead of a set lookup.

An interval is cheap because it throws information away. A name that is defined, used, defined again with an unrelated value, and used again still gets one interval spanning both episodes, because an interval cannot represent a gap. The gap it hides is a **hole**: an instruction inside the interval where the name is not actually live.[^wimmer] `live_intervals.cpp` builds both views of the same seven instructions and reports where they disagree:

--8<-- "includes/examples/backend/c2-liveness/live_intervals.cpp.md"

`x` is defined at instruction 0, used at instruction 1, defined again (a second, unrelated value happens to keep the same name) at instruction 3, and used again at instruction 4. Its exact live ranges are `[0, 1]` and `[3, 4]`; its interval is `[0, 4]`, and instruction 2 is a hole, the point where `z = 5` runs while the interval still claims `x` needs a register. The last line measures the cost of that claim directly: at instruction 2, exactly two names (`y`, `z`) are truly live, but the interval view counts three, `x` included. An allocator working from intervals alone would treat instruction 2 as one register more crowded than it is, either spilling something that did not need to spill or refusing to reuse `x`'s register for the second `x`. [C5](c5-spilling.md) covers **splitting** an interval at its holes to recover exactly this precision.

This is also where a variable naming choice from earlier chapters pays for itself. [O3](../optimize/o3-ssa.md) builds SSA form precisely so that no name is ever reused for an unrelated value: renaming `x`'s second definition to a fresh name removes the hole outright, because each of the two ranges now gets its own interval. A compiler that keeps values in SSA form through instruction selection and only assigns registers afterward, the design [C4](c4-graph-coloring.md) is built around, never has to think about this kind of hole at all; one that lowers to a mutable-register model earlier does.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Live intervals for the names w, x, y and z from live_intervals.cpp, over instructions 0 through 6" aria-describedby="c2-f1-desc">
<title id="c2-f1-title">A live interval can claim more than a variable's exact live range</title>
<desc id="c2-f1-desc">A grid of seven instruction columns, numbered 0 through 6, with one row per name: w, x, y and z. w's bar runs solid from instruction 4 to instruction 6. x has two solid segments, instructions 0 to 1 and 3 to 4, with a dashed gap at instruction 2: the hole where the interval claims x is live but it is not. y's bar runs solid from instruction 1 to instruction 5. z's bar runs solid from instruction 2 to instruction 4. A dashed outline around the whole of column 2 marks it as the point where only y and z are truly live, while every interval that reaches across it, x's included, would still count it as occupied.</desc>
<text class="vx-text-muted" x="70" y="24">instruction</text>
<text class="vx-text" x="185" y="24" text-anchor="middle">0</text>
<text class="vx-text" x="265" y="24" text-anchor="middle">1</text>
<text class="vx-text" x="345" y="24" text-anchor="middle">2</text>
<text class="vx-text" x="425" y="24" text-anchor="middle">3</text>
<text class="vx-text" x="505" y="24" text-anchor="middle">4</text>
<text class="vx-text" x="585" y="24" text-anchor="middle">5</text>
<text class="vx-text" x="665" y="24" text-anchor="middle">6</text>
<line class="vx-line" x1="150" y1="34" x2="150" y2="256"/>
<line class="vx-line" x1="230" y1="34" x2="230" y2="256"/>
<line class="vx-line" x1="310" y1="34" x2="310" y2="256"/>
<line class="vx-line" x1="390" y1="34" x2="390" y2="256"/>
<line class="vx-line" x1="470" y1="34" x2="470" y2="256"/>
<line class="vx-line" x1="550" y1="34" x2="550" y2="256"/>
<line class="vx-line" x1="630" y1="34" x2="630" y2="256"/>
<line class="vx-line" x1="700" y1="34" x2="700" y2="256"/>
<rect x="310" y="34" width="70" height="222" fill="none" class="vx-box-strong" stroke-dasharray="2 2"/>
<text class="vx-text" x="70" y="88">w</text>
<text class="vx-text" x="70" y="138">x</text>
<text class="vx-text" x="70" y="188">y</text>
<text class="vx-text" x="70" y="238">z</text>
<rect class="vx-box-accent" x="470" y="70" width="230" height="26" rx="4"/>
<rect class="vx-box-accent" x="150" y="120" width="150" height="26" rx="4"/>
<rect class="vx-box" x="310" y="120" width="70" height="26" rx="4" fill="none" stroke-dasharray="3 2"/>
<rect class="vx-box-accent" x="390" y="120" width="150" height="26" rx="4"/>
<rect class="vx-box-accent" x="230" y="170" width="390" height="26" rx="4"/>
<rect class="vx-box-accent" x="310" y="220" width="230" height="26" rx="4"/>
<text class="vx-text-muted" x="345" y="112" text-anchor="middle">hole</text>
<text class="vx-text-muted" x="380" y="276" text-anchor="middle">only y and z are truly live at instruction 2</text>
</svg>
<figcaption>Figure 1. Live intervals for instruction_liveness.cpp's companion example, live_intervals.cpp. Solid bars are exact live ranges; the dashed segment in x's row is a hole, an instruction inside the interval where x is not really live.</figcaption>
</figure>

??? check "Two names never appear together in any block's use or def set. Can their live intervals still overlap?"

    Yes. Intervals are built per instruction, and two names with no block in
    common at the block-level view can still both be live-in at some
    instruction deep inside a block they do share, once the analysis is run
    at instruction granularity. Overlap is a fact about program points, not
    about which named sets happen to mention a variable.

## Interference

Two values **interfere** when some point has both of them live at once,[^appel] which is exactly the condition that forbids giving them the same register: whichever one that register held would be overwritten while the other still needed its own value. Liveness is the only fact this rule needs. An **interference graph** has one node per value and an edge between any two that are simultaneously live anywhere; [C4](c4-graph-coloring.md) colors this graph, one color per physical register, so that no edge connects two nodes of the same color.

In `live_intervals.cpp`'s instruction 2, `y` and `z` are both live, so they interfere: the same register cannot hold both. `x` is not live there (that was the hole), so even though its interval spans instruction 2, it does not truly interfere with whatever else is live at that point, another way the gap between the two views matters, this time in the allocator's direction rather than the compiler's. In the matmul kernel's inner loop, the accumulator `sum` interferes with every array base address and every index that is still needed after `sum` starts accumulating, which is most of the loop's live values at once: that is the register pressure [C3](c3-linear-scan.md) and [C4](c4-graph-coloring.md) have to solve for, and it is also why an FP accumulator that survives a call for free on one ABI and not another, as [A4](a4-calling-conventions.md#caller-saved-and-callee-saved-registers) showed, changes how expensive that interference is to satisfy.

One case interference deliberately does not flag: a plain copy `y = x`, right after which `x` is dead, does not need `x` and `y` in different registers at all, and coalescing them (giving them the same register, so the copy compiles to nothing) is a large part of what a real allocator spends its effort on. This chapter stops at building the graph; [C4](c4-graph-coloring.md) is where coalescing and coloring happen.

## What SSA form buys you

Every one of the earlier examples used a fixed-point pass: keep visiting blocks, recomputing use and def equations, until nothing changes. That is the general method, and it has to be general, because in an arbitrary control-flow graph a value's live range can depend on facts from anywhere else in the graph. SSA form removes exactly the source of that dependency. [O3](../optimize/o3-ssa.md#how-many-phis) established that in SSA, a definition dominates every one of its uses. That single fact is enough to replace the fixed point with something cheaper for liveness specifically, and Brandner, Boissinot, Darte, Dupont de Dinechin and Rastello describe two such methods.[^brandner]

The first, **path exploration**, works one variable at a time. For each use of a value, walk backward through the control-flow graph, along predecessor edges, marking every block the walk visits as live-in for that value, and stop the moment the walk reaches the block that defines the value or a block it has already visited on this walk. There is no separate check for convergence, because the stopping condition is built in: dominance guarantees the walk will reach the definition, and it will reach it in a bounded number of steps, since a block already visited is never explored again. A general dataflow pass has to ask "did this whole pass change anything" after every sweep over every block; path exploration asks nothing like that, because each variable's walk is a self-contained search with its own, known destination.

The second method builds a **loop nesting forest**, the structure O2 uses to classify a graph's back edges, and uses it together with a forward reachability test to decide, for a given block and a given value, whether that value is live there, again without iterating the whole graph to a fixed point. Both methods are described, with worked examples, in the SSA-form compiler design book's chapter on liveness.[^ssabook]

`path_exploration.cpp` builds a small SSA loop (a header with two phi-joined values, a branch, and a back edge) and computes liveness for three of its values both ways: the classic fixed point over all three at once, and path exploration one variable at a time.

--8<-- "includes/examples/backend/c2-liveness/path_exploration.cpp.md"

The two methods agree on every block, as they must (both compute the same live-in sets; only the work to get there differs). `x0`, defined before the loop and used only inside one of its branches, still needs a walk that reaches back through the loop's join point, because the walk cannot know in advance which of the header's two incoming edges is the one that matters; it must explore both, the same way a general dataflow pass would visit those blocks. The saving path exploration offers is not that every walk is short. It is that each variable's work is bounded by its own live range and needs no coordination with any other variable's walk, where the fixed point recomputes every variable's sets together, block by block, pass after pass, until the slowest of them to settle stops changing. On this six-block graph the two totals, printed at the bottom of the output, are close; the difference widens on larger graphs with many short-lived values, which is the case that matters in practice, since most values in a compiled program are used close to where they are defined.

??? check "Why does path exploration never need to ask 'has anything changed' the way the fixed point in O4 does?"

    Because each walk already knows where it must stop: SSA guarantees the
    definition dominates every use, so the walk from a use is certain to
    reach the definition (or a block it has already visited) in a bounded
    number of steps. A general dataflow pass has no such guarantee about an
    arbitrary fact on an arbitrary graph, so it has to keep sweeping until a
    whole pass leaves every set unchanged.

??? check "The stage 10 kernel's loop-carried accumulator, `sum`, is defined once per SSA (at the loop header, by a phi) and used on every iteration through the back edge. Does path exploration need to cross that back edge to find `sum`'s live range?"

    Yes, at least once: the phi's own operand coming from the loop body is a
    use on the edge into the header (O3), and that use is on the far side of
    the back edge from the header itself. The walk from that use reaches the
    header immediately, though, since the header is `sum`'s own definition;
    it does not need to circle the loop more than once.

## For Vortex

!!! vortex "Exercise"

    **Compute per-instruction liveness for your compiled matmul kernel's
    inner loop**, on whichever of the two loop orders your instruction
    selector currently emits. For each instruction in the loop body, list
    what is live-in: the array bases, the row, column and k indices, the
    accumulator, and anything instruction selection introduced (an
    intermediate address, a loaded value). Report the peak live-in size,
    the way `instruction_liveness.cpp` does, and identify which values are
    live across the whole loop (loop-carried) versus live for only one or
    two instructions.

    Then answer, without changing anything yet: if a bounds-check failure
    inside the loop calls into the runtime, which of the loop's live values
    would have to survive that call? Cross-reference your answer against
    [A4](a4-calling-conventions.md#caller-saved-and-callee-saved-registers)'s
    caller-saved and callee-saved registers for your chosen target, and note
    which of the loop's live values are floating point, since that is where
    AAPCS64 and SysV AMD64 disagree.

    Do not build a register allocator yet, and do not build interference
    graphs into your compiler's data structures yet; C3 and C4 are where
    those decisions belong. The test that proves this exercise is done is a
    printed table, one row per instruction in the loop body, with its exact
    live-in set, checked by hand against the instructions your compiler
    actually emits.

## Key ideas

!!! recap "You can now answer"

    - **Why is block-level liveness not enough for register allocation?** A register can become free in the middle of a block, at the last instruction that reads a value, and a live-out set that only exists at a block's two ends cannot say when.
    - **What is a live interval, and why is it cheaper than exact liveness?** The span from a name's first definition to its last use, checked with one comparison instead of a set lookup; it costs precision, because it cannot represent a name that goes dead and is reborn under the same name.
    - **What is a hole?** An instruction inside a live interval where the name is not actually live: the gap an interval hides by claiming continuous liveness between a first definition and a last use.
    - **What is the one condition for two values to interfere?** Being live at the same point, anywhere in the program; nothing about how they are used otherwise matters to this graph.
    - **What SSA fact makes path exploration work without a fixed point?** Every definition dominates every one of its uses, so a backward walk from a use is guaranteed to reach its own definition, giving each walk a known stopping point with no need to check for changes across the whole graph.
    - **Why does giving `x = 1; ...; x = 2` (an unrelated second use of the name `x`) two separate SSA names remove a hole instead of just describing it?** Each name then gets its own definition and its own live range, so the conservative interval for each spans only its own short life; there is no longer a single interval trying to cover two unrelated episodes.

## Where this comes back

!!! next "You will use this again in"

    - [C3. Register allocation I: linear scan](c3-linear-scan.md): *live intervals*, *holes*, *interval splitting*
    - [C4. Register allocation II: graphs and SSA](c4-graph-coloring.md): *the interference graph*, *coalescing a copy*, *SSA-form allocation*
    - [C5. Spilling, splitting and rematerialization](c5-spilling.md): *splitting an interval at a hole*, *loop-carried live ranges*

## Sources and further reading

[^brandner]: Brandner, Boissinot, Darte, Dupont de Dinechin, Rastello, "Computing Liveness Sets for SSA-Form Programs", INRIA Research Report RR-7503, 2011. <https://inria.hal.science/inria-00558509>
[^ssabook]: Rastello, Bouchez Tichadou (eds.), *SSA-based Compiler Design*, Springer, 2022, chapter "Liveness". Draft: <https://pfalcon.github.io/ssabook/latest/>
[^cmu-live]: Pfenning and Platzer, CMU 15-411, lecture 4, "Liveness Analysis", 2013. <https://www.cs.cmu.edu/~fp/courses/15411-f13/lectures/04-liveness.pdf>
[^wimmer]: Wimmer, Mössenböck, "Optimized Interval Splitting in a Linear Scan Register Allocator", VEE 2005. <https://doi.org/10.1145/1064979.1064998>
[^appel]: Appel, *Modern Compiler Implementation*, Cambridge University Press, 1998, chapter "Liveness Analysis". <https://www.cs.princeton.edu/~appel/modern/toc.html>
