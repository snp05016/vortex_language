# C5. Spilling, splitting and rematerialization

<p class="page-intro">When an allocator runs out of registers it has three real choices: evict a value to memory, keep only part of its range in a register, or recompute it instead of reloading it. This chapter is about choosing well among the three.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 30 minutes · Builds on: [C3. Register allocation I: linear scan](c3-linear-scan.md).</p>

???+ remember "Before you start, remember"

    ??? question "What is a live range?"

        The stretch of a program, from a value's definition to its last
        use, during which that value's contents must be kept somewhere
        reachable. Two live ranges that overlap in time cannot share one
        register.

        Introduced in [C2. Liveness](c2-liveness.md).

    ??? question "When linear scan has no free register left for a new interval, what does it do?"

        It compares the new interval against the ones currently holding
        registers and evicts whichever is cheapest to give up, freeing
        that interval's register for the new one and marking the evicted
        interval spilled.

        Introduced in [C3. Register allocation I: linear scan](c3-linear-scan.md).

    ??? question "What is an interference graph, and what does k-colorable mean for one?"

        A graph with one node per live range and an edge between any two
        ranges that are live at the same time. It is k-colorable when
        every node can be given one of k colors (register names) so that
        no edge joins two same-colored nodes: every value gets a register
        without conflict.

        Introduced in [C4. Register allocation II: graphs and SSA](c4-graph-coloring.md).

    ??? question "Where does a spilled value actually live while it is not in a register?"

        In its function's own stack frame, in a slot the compiler reserves
        for it: the same region of memory that holds large array locals
        and the callee-saved registers a function promised to restore.

        Introduced in [A5. Stack frames](a5-stack-frames.md).

!!! goals "In this chapter"

    - Explain why a compiler cannot keep every live value in a register at once, no matter how good its allocator is.
    - Compute a live range's spill cost from its uses and their loop depth, and explain why a cheap-to-spill range is not the same as an unimportant one.
    - Recognize the difference between spilling a range everywhere it is live and splitting it so only the cold pieces go to memory.
    - Compute when recomputing a value beats reloading it, and name what rematerialization saves that a cheaper reload alone cannot.
    - Connect each choice to Vortex's matmul kernel: which of its values are worth protecting from the spiller, and why.

## When registers run out

Picture one point, partway through a loop body, where five values are all
still needed later: the row index, the column index, the loop index `k`,
the two operands just loaded from the input arrays, and a running sum. A
real AArch64 core has plenty of floating-point registers for that, but hold
the picture at three registers for a moment, because the shape of the
problem does not depend on the exact count, only on whether it is enough:

```text
    r0, r1, r2 available
    live here: row_base, col_base, k, a_val, b_val, acc      (6 values)
        acc = acc + a_val * b_val
        k = k + 1
    live here: row_base, col_base, k, acc                    (4 values)
```

Six values, three registers. However cleverly the allocator has been
assigning registers up to this point, at this one program point it is short
by three. This situation, where more values are live at once than there
are registers to hold them, is called **register pressure**, and it is a
property of one point in the program, not of the function as a whole: a
function can have plenty of slack everywhere except one busy inner loop.

[C3](c3-linear-scan.md) and [C4](c4-graph-coloring.md) each reach this same
moment by a different path (a sweep across sorted intervals, or an attempt
to color an interference graph) but they land on the same three responses.
**Spilling** a live range means giving up on keeping it in a register for
some stretch of the program: its value is stored to memory once, and
reloaded from memory at every point afterward where it is still needed.
**Splitting** a live range means cutting it into two or more shorter ranges
first, so that the decision to spill can be made separately for each piece:
a range can be in a register for its busy stretch and in memory everywhere
else. **Rematerializing** a value means throwing the reload away entirely
and recomputing the value instead, from operands that are still available.
The rest of this chapter takes each in turn.

??? check "At the marked point above, with 3 registers and 6 simultaneously live values, what is the fewest values that must give up their register?"

    Three: six live values minus three registers leaves three that cannot
    fit. The allocator is free to spill more than the minimum if that
    makes the rest of the function simpler, but it cannot get away with
    spilling fewer at that one point.

## Spilling is not free, and not every value costs the same to spill

Spilling a value is not one instruction, it is a small tax paid over and
over. Giving up register `r2` for `acc` in the picture above means: one
store, right after `acc` is next written, into a slot in the function's
stack frame ([A5](a5-stack-frames.md)); and then one load before every
later use, because the value can no longer be assumed to already be
sitting in a register. A value used six times after its last store costs
six loads and one store, seven memory instructions that a version of the
function with one more register would not have needed at all.

That already suggests the first design question a spiller has to answer:
given several candidates that could be spilled at some point, which one?
Chaitin's original coloring allocator answered it with a **spill cost**
computed from where a value is touched: sum, over every definition and use
of the candidate, a weight of roughly ten raised to the loop nesting depth
of that touch.[^chaitin82] A use three loops deep counts for a thousand; a
use in straight-line code counts for one. The candidate to spill is the one
whose cost is lowest, because it is the one that will need the fewest,
and the cheapest, reloads afterward. The weighting is a coarse stand-in for
"this code probably runs many more times than that code", used because an
allocator rarely has a real execution profile to consult, only the
program's static shape.

The example below computes exactly this cost for three candidates that
could appear in a matmul-shaped loop: a loop-invariant bound, touched only
outside any loop; a row base pointer, read once on every trip around an
inner loop; and an accumulator, both read and written on every trip around
that loop, plus one more use after it. The invariant bound wins by a wide
margin, precisely because none of its touches are inside the loop:

--8<-- "includes/examples/backend/c5-spilling/spill_cost.cpp.md"

Notice what the ordering does not say. `row_base` costs more than `acc`
here only because it has three loop-depth-1 touches to `acc`'s two; nothing
about the formula asks whether a value is "important" in any larger sense.
A spill-cost heuristic answers a narrow question, how expensive is it to
spill this range right here, and answers it well. It has nothing to say
about whether spilling it is the right move compared with splitting it
instead, which is the next section's question.

??? check "Why does the loop-invariant bound win, even though it is used the same number of times (twice) as several other realistic candidates?"

    Because both of its touches are at loop depth 0, so each contributes a
    weight of 1 instead of 10 or 100. The formula does not count uses, it
    counts uses weighted by how many times the surrounding loops are
    likely to run.

## Where the spill code goes: everywhere, or only where it must

A spill cost tells the allocator which range to give up on. It does not
say where in that range the spill code has to live. The lazy answer, and
the one a first allocator naturally reaches for, is **spill everywhere**:
treat the whole range as memory-resident for its entire life, storing after
its one definition and reloading before every single use, loop uses
included. That is exactly what the picture in the previous section
computed: one store and one reload per use, no matter where that use falls.

But a range's uses are rarely spread evenly. The picture at the start of
this chapter had `acc` live across a loop that runs many times; if the
*only* reason `acc` needed to leave a register was pressure from other
values that are live just once, outside that loop, then reloading `acc`
on every trip around the loop is paying a cost the loop itself did not
create. **Splitting** the range fixes this by treating it as several
shorter ranges glued together: a piece that covers the loop can keep a
register for the loop's whole duration, even while the pieces before and
after it are spilled, as long as one reload puts the value back in a
register right as the loop begins.

<figure class="vx-figure">
<svg viewBox="0 0 780 380" role="img" aria-labelledby="c5-f1-title c5-f1-desc">
<title id="c5-f1-title">Spilling everywhere reloads on every loop trip; splitting reloads once, before the loop</title>
<desc id="c5-f1-desc">Two panels show the same live range in a function that contains one loop. Top panel, spill everywhere: a store follows the value's definition, then a reload marker appears before every later read, including a fresh one on every trip around the loop and one more after it. Bottom panel, split: the store after the definition is unchanged, but only one reload appears, right before the loop; from there the value stays in a register, shown as a solid line, through every loop read and the read that follows.</desc>
<text class="vx-text" x="20" y="28">Spill everywhere</text>
<rect class="vx-box" x="20" y="40" width="740" height="120" rx="4"/>
<rect class="vx-box-accent" x="340" y="55" width="220" height="90" rx="4"/>
<text class="vx-text-muted" x="450" y="75" text-anchor="middle">k-loop, several trips</text>
<line class="vx-line" x1="60" y1="140" x2="700" y2="140" stroke-dasharray="4 3"/>
<rect class="vx-box-strong" x="56" y="136" width="8" height="8"/>
<circle class="vx-dot" cx="380" cy="140" r="4"/>
<circle class="vx-dot" cx="420" cy="140" r="4"/>
<circle class="vx-dot" cx="460" cy="140" r="4"/>
<circle class="vx-dot" cx="500" cy="140" r="4"/>
<text class="vx-text-muted" x="530" y="130">...</text>
<circle class="vx-dot" cx="650" cy="140" r="4"/>
<text class="vx-text-muted" x="64" y="120">def, store</text>
<text class="vx-text-muted" x="380" y="120">reload each trip</text>
<text class="vx-text-muted" x="620" y="120">reload after</text>
<text class="vx-text" x="20" y="218">Split</text>
<rect class="vx-box" x="20" y="230" width="740" height="120" rx="4"/>
<rect class="vx-box-accent" x="340" y="245" width="220" height="90" rx="4"/>
<text class="vx-text-muted" x="450" y="265" text-anchor="middle">k-loop, several trips</text>
<line class="vx-line" x1="60" y1="330" x2="340" y2="330" stroke-dasharray="4 3"/>
<line class="vx-flow" x1="340" y1="330" x2="700" y2="330"/>
<rect class="vx-box-strong" x="56" y="326" width="8" height="8"/>
<circle class="vx-dot" cx="340" cy="330" r="4"/>
<text class="vx-text-muted" x="64" y="310">def, store</text>
<text class="vx-text-muted" x="340" y="310">one reload, right before the loop</text>
<text class="vx-text-muted" x="600" y="310">register covers loop and the read after it</text>
<rect class="vx-box-strong" x="600" y="360" width="8" height="8"/>
<text class="vx-text-muted" x="614" y="367">store</text>
<circle class="vx-dot" cx="700" cy="360" r="4"/>
<text class="vx-text-muted" x="714" y="367">reload</text>
</svg>
<figcaption>Figure 1. The same live range in a function with one loop, spilled two ways. Spilling everywhere (top) reloads the value on every trip around the loop, plus once more afterward: five markers shown, more implied by the ellipsis. Splitting (bottom) stores the value once but reloads it only once, right before the loop, after which it stays in a register (the solid segment) through every loop read and the read that follows.</figcaption>
</figure>

The example below counts the memory traffic both ways for a value defined
once before an eight-trip loop, read once per trip, and read once more
after it. Spilling everywhere pays for a reload on every one of those
reads. Splitting pays for exactly one reload, because from the loop's
first instruction onward the value is sitting in a register the whole
time, the same as if it had never been spilled at all:

--8<-- "includes/examples/backend/c5-spilling/spill_everywhere_vs_split.cpp.md"

The gap grows with the trip count: an eight-trip loop shows an eight-fold
difference in loads here, and a loop that runs longer widens it further,
which is exactly why a cost formula that weights loop depth (the previous
section) and a spiller that can act on that weighting by splitting (this
section) belong together. LLVM's greedy allocator is named for how far it
takes this idea: rather than choosing once between "keep in a register"
and "spill everywhere", it tries several ways to carve a difficult range
into pieces, including carving out exactly the piece that covers one loop,
before falling back to an ordinary spill for whatever is left over.[^olesen]

??? check "In the split strategy above, why does the value need to be stored at all, if the loop reload is what actually matters?"

    Because control can still reach a point after the definition but before
    the loop where the value is needed and no longer fits in a register;
    the store is what makes it safe to have evicted it from a register in
    the first place. Splitting changes *where* reloads are needed, not
    whether the initial store is.

## Rematerializing instead of reloading

A reload always costs the same: one load instruction, paid at every use,
and it depends on a store having happened first. Some values do not need
that machinery at all, because they can be recomputed from operands that
are already going to be available wherever they are needed. A small
integer constant is the simplest case: recomputing it is one instruction,
the same size as the load would have been, and it never needed a stack
slot to begin with. An array's base address, computed once as a fixed
pointer plus a fixed offset, is only slightly more expensive to recompute,
typically an add or two. Briggs, Cooper and Torczon named this move
**rematerialization**: instead of treating "spilled" as a promise to
reload from a fixed memory location, treat it as a promise to reproduce
the same value some other way, and let the allocator pick whichever is
cheaper at each use.[^remat]

Rematerialization changes the comparison from the previous section in one
important way: it removes the store entirely, not only some of the loads.
A value that is never spilled to memory needs no stack slot at all, which
matters most exactly when stack slots, not only registers, are themselves
scarce. The example below compares the two strategies' total instruction
count against the number of times a value is used again, for a cheap
constant (one instruction to recompute) and for an address (two
instructions, a multiply and an add):

--8<-- "includes/examples/backend/c5-spilling/rematerialize.cpp.md"

A one-instruction constant wins outright, at every use count this example
tries: even eight uses, eight recomputations, beats one store plus eight
reloads, because the store the reload strategy always pays is more than a
single recompute ever costs. A two-instruction address is a closer call in
this simple count: it breaks even at one reuse and loses to plain
reloading beyond that, in this model. Real allocators still rematerialize
addresses freely, and the reason is not visible in an instruction count
alone: avoiding the stack slot also avoids competing for stack space with
every other spilled value, and it avoids extending that value's
interference with everything else live across the same stretch of program,
which this simple comparison does not price in at all.

## Rematerialization and Vortex's floating-point rules

Vortex requires every `f32` and `f64` operation to be exactly one IEEE 754
operation, rounded to nearest with ties to even, with no reassociation, no
reordering and no evaluation in extra precision
([decision 56](../decisions/numbers.md#d56)). That rule has a direct
consequence for rematerialization: recomputing a value is safe exactly
when it reproduces the same sequence of rounded operations, in the same
order, from the same inputs, not merely "a calculation that gives the same
mathematical answer". An accumulator like `acc` in the picture at the top
of this chapter, built up by repeated `acc = acc + a_val * b_val`, can be
rematerialized safely, because replaying that same chain of additions from
the same operands in the same order always rounds the same way every time
it is replayed: Vortex's strictness is what makes the replay deterministic
down to the bit. A compiler that instead tried to "recompute" a
partial sum by adding the same terms in a different order, or by folding a
multiply and an add into one fused operation to save an instruction, would
not be rematerializing that value at all: it would be computing a
different, and under decision 56, a non-conforming one.

## Deciding spills before coloring, in SSA form

Everything so far described spilling and splitting as choices made *while*
an allocator assigns registers: linear scan ([C3](c3-linear-scan.md))
decides as it sweeps forward through sorted intervals, and a graph-coloring
allocator ([C4](c4-graph-coloring.md)) decides as it tries and fails to
color the interference graph it built. Give the allocator a program in
[SSA form](../optimize/o3-ssa.md) instead, and the two decisions, what to
spill and how to color, can be pulled apart into separate passes entirely.
Hack showed why the separation pays off: an SSA-form program's
interference graph is chordal (its nodes admit a perfect elimination
order), and a chordal graph can always be colored optimally by a simple
greedy pass once its perfect elimination order is known, no backtracking
required.[^hack] That result only holds, though, once every range that
cannot fit has already been removed or split; the hard combinatorial part of
allocation moves entirely into deciding *that*, ahead of time, and coloring
becomes closer to bookkeeping. Braun and Hack extend the same separation
from spilling alone to splitting live ranges as its own prior pass.[^braunhack]

Two allocators built to this design make the split visible in practice.
QBE's own account of its register allocator describes spilling and
coalescing as steps that run before coloring rather than during it, and it
estimates which loop-carried ranges are worth splitting from loop nesting
depth, the same signal this chapter's spill-cost formula used.[^qbe] The
Cranelift project's `regalloc2` keeps a related structure at a finer grain
than one contiguous range per value: it tracks a live range as a set of
smaller *bundles*, and because a bundle can be moved to a stack slot
independently of the rest of its range, splitting is something the data
structure represents directly, rather than something bolted on
afterward.[^regalloc2] Coloring-time spilling, splitting-as-a-separate-pass,
and bundle-based splitting are three different answers to the same
question this chapter has been asking throughout: not only what to spill,
but when that decision gets made relative to everything else the allocator
is doing.

??? check "Why does putting a program into SSA form first make spill and split decisions easier to separate from coloring?"

    Because SSA form gives the interference graph a chordal structure,
    which a simple greedy pass can color optimally once a perfect
    elimination order for it is known. The hard, combinatorial part of
    allocation, deciding what does not fit in a register, moves entirely
    into an earlier, separate pass; coloring what is left no longer needs
    to backtrack or guess.

## For Vortex

!!! vortex "Exercise"

    **Give your allocator a way to run out of registers on purpose.** Add
    a debug option, checked only in your own tests, that caps the number
    of registers the allocator is allowed to use below the real machine's
    count. Do not change anything about how spilling itself is decided;
    the point is only to make register pressure, which the real register
    count on AArch64 or x86-64 rarely creates for small test programs, easy
    to trigger on demand. A handful of small hand-written test programs
    that need, say, five or six simultaneously live values, run under a
    cap of three or four registers, are enough to force spill code into
    existence in your test suite.

    **Then, without changing the matmul kernel itself, show that its inner
    loop stays clean.** [Stage 10's kernel](../compiler/guide/stage-10-matrix-multiplication.md)
    passes its output through a `&mut` reference
    ([decision 40](../decisions/references.md#d40)), and its inner loop
    touches five or six values at once, much like the picture that opened
    this chapter. Write a test, in whatever form your test harness already
    checks generated assembly, that inspects the instructions inside the
    k-loop specifically and asserts that the only loads and stores present
    are the ones that read `a` and `b` and write through `c`: no reload or
    spill-store of the accumulator, the row base or the column base
    anywhere inside that loop. Do not write the allocator changes this
    exercise's test would require; write only the register-pressure test
    programs, the register-cap option, and the assertion. If the assertion
    fails against your current allocator, that failure is the specification
    for the work this chapter describes, not a bug in the test.

## Key ideas

!!! recap "You can now answer"

    - **Why can a compiler run out of registers even for a small function?** Because register pressure, the count of values simultaneously live, can exceed the register count at one single program point, regardless of how many registers the machine has in total or how few the function uses on average.
    - **Why does a use inside a loop cost more to spill than one outside it?** Because the reload it needs runs every time the loop body runs; the classic cost heuristic weights each touch by roughly ten for every loop nesting level to reflect that.
    - **What is the difference between spilling a range everywhere and splitting it?** Spilling everywhere keeps the whole range in memory for its entire life, paying for a reload at every use; splitting cuts the range into pieces first, so a hot piece, such as one that covers a loop, can keep a register while the cold pieces around it do not.
    - **When does rematerializing a value beat reloading it?** When recomputing it from operands that are still available costs fewer instructions than a reload would, and it always beats reloading on one count regardless of instruction count: it never needs a stack slot or a store at all.
    - **Why is rematerializing Vortex's floating-point accumulator as safe as reloading it?** Because Vortex fixes the order and rounding of every floating-point operation; replaying the same operations on the same inputs in the same order always reproduces the same bits, so recomputation and reloading a stored copy are indistinguishable in their result.
    - **Why does putting a program into SSA form first make spilling and splitting easier to separate from coloring?** Because SSA form gives the interference graph a chordal structure that a simple greedy pass can color optimally once its elimination order is known, so the hard part, deciding what does not fit in a register, can be settled entirely beforehand instead of during coloring.

## Where this comes back

!!! next "You will use this again in"

    - [C6. Instruction scheduling](c6-scheduling.md): *register pressure*, *scheduling before allocation*
    - [C7. Peephole optimization](c7-peephole.md): *spill slots*, *load and store pairs*
    - [D2. JIT compilation](d2-jit.md): *stack slots*, *generated code that must stay correct under register pressure*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *the greedy allocator*, *live-range splitting*

## Sources and further reading

[^chaitin82]: Chaitin, "Register allocation & spilling via graph coloring", ACM SIGPLAN Symposium on Compiler Construction, 1982. <https://doi.org/10.1145/800230.806984>
[^remat]: Briggs, Cooper, Torczon, "Rematerialization", PLDI 1992. <https://doi.org/10.1145/143095.143143>
[^olesen]: Olesen, "Greedy Register Allocation in LLVM 3.0", LLVM Project Blog, 18 September 2011. <https://blog.llvm.org/2011/09/greedy-register-allocation-in-llvm-30.html>
[^regalloc2]: Bytecode Alliance, "regalloc2: the Ion allocator" (ION.md design notes). <https://github.com/bytecodealliance/regalloc2/blob/main/doc/ION.md>
[^qbe]: QBE project documentation, "QBE vs LLVM". <https://c9x.me/compile/doc/llvm.html>
[^hack]: Hack, "Register Allocation for Programs in SSA-Form", doctoral thesis, Karlsruhe Institute of Technology, 2007. <https://publikationen.bibliothek.kit.edu/1000007166>
[^braunhack]: Braun, Hack, "Register Spilling and Live-Range Splitting for SSA-Form Programs", Compiler Construction (CC), 2009. <https://doi.org/10.1007/978-3-642-00722-4_13>
