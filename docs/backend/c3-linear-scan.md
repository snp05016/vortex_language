# C3. Register allocation I: linear scan

<p class="page-intro">Poletto and Sarkar's linear-scan algorithm turns a set of live intervals into a register assignment in one pass over an instruction order, trading a graph-coloring allocator's guarantees for the speed a fast compiler or a JIT needs.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 35 minutes · Builds on: [C2. Liveness](c2-liveness.md), [O4. Dataflow analysis](../optimize/o4-dataflow.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is the live-in set of a block, and how is it built from live-out?"

        The variables live at the start of a block: those the block reads before writing them (its use set), plus those live at its end that the block does not write itself. in(b) = use(b) ∪ (out(b) \ def(b)).

        Introduced in [O4. Dataflow analysis](../optimize/o4-dataflow.md#liveness-by-hand).

    ??? question "Why can an ISA only afford a handful of registers?"

        A register is the fastest storage a processor has, built directly into its datapath rather than reached over a memory bus, and that speed is exactly why there can only be a few: AArch64 names 31 general-purpose registers, x86-64 sixteen.

        Introduced in [A1. The machine model](a1-machine-model.md#registers-names-for-the-fastest-storage-a-processor-has).

    ??? question "What is the difference between a caller-saved and a callee-saved register?"

        A caller-saved register may be overwritten by any call, so the caller must save it first if it still needs the value afterward. A callee-saved register must come back from a call holding the value it went in with.

        Introduced in [A4. Calling conventions and ABIs](a4-calling-conventions.md#caller-saved-and-callee-saved-registers).

    ??? question "How does AAPCS64 pass the arguments to `fn multiply(a: &[f32; 2, 3], b: &[f32; 3, 2], c: &mut [f32; 2, 2])`?"

        As three addresses, one per reference, in `x0`, `x1` and `x2`: the first three of the eight integer-or-address argument registers.

        Introduced in [A4. Calling conventions and ABIs](a4-calling-conventions.md#a-contract-with-no-compiler-in-the-room).

!!! goals "In this chapter"

    - Explain what a live interval is and how it approximates live-in/live-out facts with one contiguous range.
    - Run Poletto and Sarkar's linear-scan algorithm by hand: sort by start, keep an active list sorted by end, expire, allocate or spill.
    - Explain why linear scan always spills the interval with the furthest endpoint, and what that rule costs against an optimal choice.
    - Compare linear scan's target and running time against graph coloring's, and place both against production allocators.
    - Choose an instruction order and an interval representation for a Vortex compiler's own linear-scan allocator, and test the result against a checker.

## From a live value to a register

[C2](c2-liveness.md) computes, for every point in a function, which values are **live**: which ones a later instruction will still read before anything overwrites them. Register allocation asks the next question: which of a machine's small, fixed set of registers can hold each of those values, given that a register holds only one value at a time.

Two values can share a register only if they are never live at the same point. Say two values **interfere** when some point exists where both are live: giving interfering values the same register would let one clobber the other before it is read. Allocating registers is therefore a coloring problem: pick registers ("colors") for every value so that no two interfering values share one, using no more colors than the machine actually has, and sending whatever is left over to memory. [C4](c4-graph-coloring.md) attacks this literally, building an **interference graph** whose nodes are values and whose edges are interference, then coloring it the way a map is colored so that no two adjacent countries share a color. This chapter takes a different route, built for compilers that cannot afford to construct that graph at all.

## Why not always build the graph

An interference graph can have an edge between any pair of values that are simultaneously live, which means deciding, for every pair, whether they ever coincide. Chaitin's original graph-coloring allocator does exactly that,[^chaitin82] and it produces excellent code, which is why [C4](c4-graph-coloring.md) is worth learning even though this chapter goes first. But checking every pair of values costs time that grows with the square of how many values there are, in the worst case, before coloring even begins. For a numerical kernel compiled once and then run a million times, that cost is easy to justify. For a compiler that must produce code while something else is waiting on it, such as a JIT compiling a function the moment it is first called, or a language server re-checking a file on every keystroke, it usually is not.

Poletto and Sarkar built **linear scan** for exactly that second setting: an allocator whose running time stays close to linear in the size of the program, that never builds an interference graph at all, and that still keeps most of a good allocation's benefit, designed with fast, dynamic compilation such as a JIT in mind, where compile time is part of what the user pays for on every run.[^ps99] The idea it trades on: instead of asking "which pairs of values are simultaneously live", which needs a graph, it asks a cheaper, closely related question, "in what order do values start and stop being live", and answers that with a single sorted scan.

Production allocators have followed this lineage further than the JITs Poletto and Sarkar had in mind. LLVM's earlier allocator was itself a linear scan; for LLVM 3.0, it was replaced with a new "greedy" allocator built on the same live-interval representation rather than on Chaitin's graph. Olesen's announcement reports the change producing code 1 to 2 percent smaller and, in some cases, up to 10 percent faster, entirely from smarter live-range splitting and eviction order, without adopting graph coloring.[^olesen11] [E3](e3-llvm-allocator-scheduler-mc.md) reads that allocator in full; this chapter builds the live-interval idea it, and Cranelift's regalloc2 after it, both rest on.

## Live intervals: one range instead of a graph

Take five values computed one after another, no branches, an original example distinct from any real kernel:

```text
1  a = 2
2  b = 3
3  c = a + b
4  d = a * b
5  e = c + d
6  return e
```

Number each instruction by its position in this order. A **live interval** is the range of positions from a value's definition to its last use, written `[start, end]`.[^ps99] Reading the code: `a` is defined at 1 and last read at 4, inside `d = a * b`, so its interval is `[1, 4]`. `b`, defined at 1 and also last read at 4, gets `[1, 4]` too. `c` is defined at 3 and last read at 5: `[3, 5]`. `d` is defined at 4, last read at 5: `[4, 5]`. `e` is defined at 5, last read at 6: `[5, 6]`.

Two intervals that overlap describe values that are live at the same point, which is exactly interference: `[1,4]` and `[3,5]` overlap at positions 3 and 4, so `a` and `c` cannot share a register. Checking overlap between two ranges of integers, `a.start <= b.end && b.start <= a.end`, is a single comparison, computed in constant time, in contrast to deciding and storing one graph edge per interfering pair. That is the whole saving linear scan is built on.

A live interval is also a real approximation, not only a repackaging, of the live-in/live-out facts [O4](../optimize/o4-dataflow.md#liveness-by-hand) computes. Live-in/live-out is exact at every program point; an interval collapses a value's entire liveness into one contiguous range from its first definition to its last use, even where the value is genuinely dead in between, across a branch neither the definition nor the use touches. Such a gap is called a **hole**, and the plain algorithm below still reserves a register for a value across its own holes, which is the main source of the extra spills that make simple linear scan produce worse code than an allocator with exact liveness. This chapter builds the plain version first; the section after the worked example describes how later work narrows that gap.

## The algorithm, on eight intervals and three registers

Linear scan keeps one list, **active**: the intervals currently holding a register, kept sorted by increasing end point. It visits every interval once, in order of increasing start point, and does one of three things:[^ps99]

1. **Expire.** Remove from `active` every interval whose end point is before the current interval's start point; whatever register it held is free again.
2. **Allocate.** If a register is now free, give it to the current interval and add the interval to `active`.
3. **Spill.** If `active` already holds one interval per available register, one interval must give up its register. Linear scan always picks the interval in `active` with the *furthest* end point, whether that is the interval just arriving or one already running: it has the most work left to do without a register, so giving it up now costs the least, since a value spilled early in its own interval and one spilled late cost the same number of memory accesses either way, but spilling the one that runs longest keeps a register free for everything shorter that follows.[^ps99]

Run this on eight made-up intervals with three registers, sorted by start:

| Interval | Start | End |
| --- | --- | --- |
| a | 1 | 8 |
| b | 2 | 4 |
| c | 3 | 9 |
| d | 5 | 6 |
| e | 6 | 10 |
| f | 7 | 7 |
| g | 8 | 12 |
| h | 10 | 11 |

`a`, `b` and `c` each find a free register in turn: `r0`, `r1`, `r2`. At `d` (start 5), `b` has already ended (its end, 4, is before 5), so `b` expires, freeing `r1`, which `d` takes. At `e` (start 6), `d` has just reached its own end (6 is not *before* 6, by the rule above, so `d` has not expired yet), and all three registers are held, by `d`, `a` and `c`. The interval in `active` with the furthest end is `c`, ending at 9; `e` itself ends at 10, later still, so evicting `c` would only postpone the same problem, and the algorithm spills `e`, the newcomer, instead. `f` (start 7) then finds `d` has expired (6 is before 7) and takes its register; `g` (start 8) finds `f` has expired; `h` (start 10) finds both `a` and `c` have expired, and takes one of the two freed registers. `linear-scan.cpp`, below, runs exactly this table and prints every expire, allocate and spill decision, plus the final assignment.

--8<-- "includes/examples/backend/c3-linear-scan/linear-scan.cpp.md"

<figure class="vx-figure">
<svg viewBox="0 0 760 400" role="img" aria-labelledby="c3-f1-title c3-f1-desc">
<title id="c3-f1-title">Linear scan sweeping eight live intervals with three registers</title>
<desc id="c3-f1-desc">Eight horizontal bars, one per interval a through h, laid out along a horizontal axis numbered 1 to 12. Bars are colored by outcome: a and h share one color for register r0, b, d, f and g share a second color for r1, c has a third color for r2, and e is drawn in a fourth, muted color marking it spilled. b, d, f and g never touch, so sharing one color is safe even though four different intervals hold it in turn. An animated vertical line travels left to right across the axis from position 1 to 12, marking the order in which the algorithm visits each interval's start point.</desc>
<line class="vx-line" x1="70" y1="340" x2="660" y2="340"/>
<g class="vx-text-muted" font-size="10">
<text x="90" y="356" text-anchor="middle">1</text>
<text x="140" y="356" text-anchor="middle">2</text>
<text x="190" y="356" text-anchor="middle">3</text>
<text x="240" y="356" text-anchor="middle">4</text>
<text x="290" y="356" text-anchor="middle">5</text>
<text x="340" y="356" text-anchor="middle">6</text>
<text x="390" y="356" text-anchor="middle">7</text>
<text x="440" y="356" text-anchor="middle">8</text>
<text x="490" y="356" text-anchor="middle">9</text>
<text x="540" y="356" text-anchor="middle">10</text>
<text x="590" y="356" text-anchor="middle">11</text>
<text x="640" y="356" text-anchor="middle">12</text>
</g>
<text class="vx-text-muted" x="660" y="378" text-anchor="end" font-size="11">instruction position</text>
<text class="vx-text" x="20" y="68">a</text>
<rect class="vx-box-accent" x="90" y="52" width="350" height="24" rx="3"/>
<text class="vx-mono" x="265" y="68" text-anchor="middle" font-size="12">r0</text>
<text class="vx-text" x="20" y="104">b</text>
<rect class="vx-box" x="140" y="88" width="100" height="24" rx="3"/>
<text class="vx-mono" x="190" y="104" text-anchor="middle" font-size="12">r1</text>
<text class="vx-text" x="20" y="140">c</text>
<rect class="vx-box-strong" x="190" y="124" width="300" height="24" rx="3"/>
<text class="vx-mono" x="340" y="140" text-anchor="middle" font-size="12">r2</text>
<text class="vx-text" x="20" y="176">d</text>
<rect class="vx-box" x="290" y="160" width="50" height="24" rx="3"/>
<text class="vx-mono" x="315" y="176" text-anchor="middle" font-size="12">r1</text>
<text class="vx-text" x="20" y="212">e</text>
<rect class="vx-box-bad" x="340" y="196" width="200" height="24" rx="3"/>
<text class="vx-mono" x="440" y="212" text-anchor="middle" font-size="12">spilled</text>
<text class="vx-text" x="20" y="248">f</text>
<rect class="vx-box" x="378" y="232" width="24" height="24" rx="3"/>
<text class="vx-mono" x="440" y="248" font-size="12">r1</text>
<text class="vx-text" x="20" y="284">g</text>
<rect class="vx-box" x="440" y="268" width="200" height="24" rx="3"/>
<text class="vx-mono" x="540" y="284" text-anchor="middle" font-size="12">r1</text>
<text class="vx-text" x="20" y="320">h</text>
<rect class="vx-box-accent" x="540" y="304" width="50" height="24" rx="3"/>
<text class="vx-mono" x="565" y="320" text-anchor="middle" font-size="12">r0</text>
<g class="vx-travel" style="--vx-distance: 550px">
<line class="vx-line" x1="90" y1="38" x2="90" y2="336"/>
<circle class="vx-dot" cx="90" cy="38" r="5"/>
</g>
</svg>
<figcaption>Figure 1. Eight live intervals over three registers, colored by the assignment <code>linear-scan.cpp</code> computes. <code>b</code>, <code>d</code>, <code>f</code> and <code>g</code> never overlap, so all four safely share <code>r1</code> in turn. <code>e</code> is the one interval spilled: when it starts, at position 6, every register is already held by an interval that ends no earlier than <code>e</code> itself does.</figcaption>
</figure>

??? check "Why does the algorithm spill the interval with the furthest end, rather than always spilling whichever interval just arrived?"

    Spilling the newcomer is only correct when the newcomer itself has the furthest end; when an interval already in `active` runs longer than the newcomer, evicting the newcomer instead leaves the same problem for whatever comes after it, one step later. Spilling by furthest end always removes as much future register pressure as any single spill decision can remove, which is the greedy argument Poletto and Sarkar's algorithm is built on.[^ps99]

??? check "In the worked table, `b` ends at 4 and `d` starts at 5. If `d` started at 4 instead, would `b` have expired before `d` was considered?"

    No. The rule expires an interval only when its end is *before* the current interval's start (`it->end < current_start`), and 4 is not before 4. `b` and `d` would both need a register at position 4, so they would interfere and could not share `r1`.

## Ordering instructions for a scan

Linear scan needs one property from its instruction order: every definition has to come before every use it reaches, in the order the algorithm sweeps. Reverse postorder over the control-flow graph, the same order [O2](../optimize/o2-cfg-and-dominance.md#visiting-the-blocks-in-order) uses for dataflow, gives that property for any acyclic path, and a value's own definition always precedes its uses inside a straight run of code.

Loops need care. A value live across a loop's back edge, such as an accumulator updated on every iteration, is not listed once per iteration: the instruction order lists the loop body exactly once, the way the source program writes it, and the value's interval has to span from before the loop to after it to cover every iteration's reads and writes with one contiguous range. That is a second, coarser approximation stacked on top of the one from holes: an interval spanning a loop reserves a register for the value across the loop's entire body, including any point inside the body where a linear-scan-oblivious reader might have expected the value to be temporarily free. This is not a bug to route around; it is the accurate statement that the value genuinely is live on every iteration, and a register spent on it there is a register correctly spent.

## Second-chance binpacking and splitting an interval

Traub, Holloway and Smith's PLDI 1998 paper attacks the hole problem the previous two sections raised: a single `[start, end]` range can force a spill for a value that only needs a register during two short stretches near the ends of a long interval and is genuinely dead in between.[^ths98] Their **second-chance binpacking** allocator lets a spilled value's interval reopen later: once a value has been sent to memory, it can regain a register at its next use rather than staying in memory for the rest of its original interval, at the cost of a reload where it comes back. Wimmer and Mössenböck later formalized this as **interval splitting**: cutting one interval into two or more shorter pieces at chosen points, each piece allocated independently, so that a value's register only has to be reserved where it is actually needed, not across the holes a single range would otherwise claim.[^wm05] [C5](c5-spilling.md) is where splitting, and the question of *where* to cut for the least total cost, becomes the chapter's main subject.

Wimmer and Franz pushed the same family one step further: linear scan over values already in [O3](../optimize/o3-ssa.md)'s SSA form, where every value has one definition by construction, removes the extra bookkeeping plain linear scan needs to track which of several assignments to the same source variable is live at a given point.[^wf10] That version is closer to what modern JITs, and LLVM's own greedy allocator, actually run; [E3](e3-llvm-allocator-scheduler-mc.md) picks it up from here.

## Applying it to the Vortex matmul kernel

Stage 10's matmul kernel gives a small but real body to run this on:

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

List the values a compiler's instruction selector produces here, and where each one's interval runs. `a`, `b` and `c` arrive as addresses in registers, per the calling convention this chapter's remember box already named, and each is read (`a`, `b`) or written (`c`) throughout the function, so each has an interval spanning the whole body: the widest three intervals in the function. `row` is read by every instruction that computes `a[row, k]` and `c[row, column]`, so its interval spans the outer two loops together. `column` spans the inner two. `k` and `sum` are each live only inside the innermost loop, `sum` a little longer than `k` because it is read once more after the `k` loop ends, to store into `c[row, column]`.

Notice what does not appear on that list: a live value for the loop bound. `row` runs to a fixed shape's dimension, 2; `column` to 2; `k` to 3; and because Vortex's array shapes are part of the type, fixed at compile time,[^stage10] every one of those bounds is a constant the instruction selector can fold straight into a compare instruction, never a value that has to occupy a register waiting to be compared against. A back end for a language where array shapes are runtime values would need a live interval for each bound; a Vortex back end, for this reason alone, allocates one fewer register than the general case per loop nest, for every kernel whose shapes are known at compile time, which fixed-shape arrays make the common case rather than an optimization someone has to earn.

The three widest intervals, `a`, `b` and `c`, are exactly the ones AAPCS64 already placed in `x0`, `x1` and `x2` on entry. A register allocator is free to leave them there for the whole function if nothing else needs those particular registers; moving a value that is already correctly placed only costs an instruction for no benefit. Whether `sum`, the narrowest interval, ever needs to leave a register for memory depends on how many other values are live at the same program point, which is exactly the question this chapter's algorithm answers, and which the exercise below asks the reader to answer for their own compiler's chosen instruction order.

One more property is worth stating plainly, because an allocator that got it wrong would be a correctness bug, not a performance one: moving a value between a register and a stack slot must never change the value itself. Vortex's floating-point rules already require every `f32` and `f64` operation to produce the exact IEEE 754 result, with no extended precision and no reordering across a spill or reload.[^numbers56] A register allocator that is only choosing *where* a value lives, never recomputing *what* it is, satisfies this automatically as long as it copies bits faithfully; the discipline belongs to the instruction selector and the optimizer, not to this chapter's algorithm, but it is worth remembering that "just an allocation detail" is only true because those earlier stages already guarantee it.

??? check "Why do `a`, `b` and `c` get the widest live intervals in the function, wider even than `row`?"

    Because every one of them is read or written by the innermost loop body, on every iteration of every enclosing loop, so no point in the function exists where any of the three is provably dead. `row`, by contrast, is not read again during the innermost `k` loop itself (only `a[row, k]` and, later, `c[row, column]` need it), so a splitting allocator, unlike the plain one this chapter builds, could in principle free `row`'s register for part of that loop and reclaim it after.

## Checking an allocation independently

An allocator is exactly the kind of code that is easy to get subtly wrong and hard to notice by reading the output: a mistaken comparison only shows up as two values quietly sharing a register somewhere in the middle of a long function. The fix is the same one used throughout this book: build a second, much simpler piece of code whose only job is to check the first one's answer, and run them against each other rather than trusting either alone.

For register allocation, the checker is almost the same one-line overlap test this chapter already derived: given a finished assignment, a list of intervals each carrying either a register or "spilled", confirm that no two intervals holding the same register overlap. `allocation-checker.cpp` runs that check against the assignment `linear-scan.cpp` computed above, then against the same assignment with one register changed by hand, to show the checker catching the conflict that change creates.

--8<-- "includes/examples/backend/c3-linear-scan/allocation-checker.cpp.md"

## For Vortex

!!! vortex "Exercise"

    **Build.** In your own compiler, choose a linear instruction order for a function body (reverse postorder over its control-flow graph is a safe default) and compute a live interval for every value in that order, using the liveness facts [C2](c2-liveness.md) computes. Implement Poletto and Sarkar's linear-scan algorithm over those intervals: sort by start, an active list sorted by end, expire, allocate, spill by furthest end. Separately, implement an **allocation checker**: a function that takes a finished assignment (an interval and either a register or "spilled" for every value) and confirms that no two intervals sharing a register overlap, independent of the allocator that produced the assignment, the way `allocation-checker.cpp` checks `linear-scan.cpp`'s output above.

    **Do not build yet.** Interval splitting or second-chance reopening; the plain algorithm above, with a hole costing a whole register for a whole interval, is the correct starting point, and [C5](c5-spilling.md) is where splitting earns its complexity. Graph coloring as an alternative allocator; [C4](c4-graph-coloring.md) covers it on the same kernel so the two can be compared on real numbers instead of guessed ones. Any handling of calls that pins specific values to specific registers ahead of the general algorithm; that refinement belongs with the calling-convention work [A4](a4-calling-conventions.md) already did.

    **The test that proves it works.** Run the checker against the allocator's own output on the matmul kernel and on at least one function with a loop, and confirm zero conflicts. Then fuzz: generate random sets of intervals (random start and end pairs, a random small register count) and run every one through both the allocator and the checker; a conflict the checker finds is a bug in the allocator, not in the checker, precisely because the checker's only job is the one-line overlap test this chapter derived, which is far simpler to get right than the allocator itself. This mirrors the independent-checker idea Fallin describes for Cranelift's regalloc2, where a separate abstract interpreter re-derives what every register and stack slot should hold and flags any mismatch.[^regalloc2]

## Key ideas

!!! recap

    - **What is a live interval?** The range of instruction positions from a value's definition to its last use, `[start, end]`, computed over one linear order of the program.
    - **Why does linear scan not build an interference graph?** Because overlap between two `[start, end]` ranges is a constant-time comparison, so the algorithm never has to decide or store interference for every pair of values the way a graph-coloring allocator does.
    - **What are the algorithm's three moves?** Expire an interval whose register has freed up, allocate a free register to a new interval, or spill when none is free.
    - **Which interval gets spilled, and why?** The one in the active set with the furthest end point, current or already running: it has the most work left without a register, so giving it up costs the least.
    - **What is a hole, and why does it matter?** A stretch where a value is genuinely dead in the middle of its own interval; plain linear scan still reserves a register across it, which is the main source of avoidable spills the next chapters remove.
    - **Why do Vortex's fixed-shape arrays make register allocation slightly easier than the general case?** A loop bound that is part of a fixed array shape is a compile-time constant, never a live value competing for a register the way a runtime-sized loop's bound would be.

## Where this comes back

!!! next "You will use this again in"

    - [C4. Register allocation II: graphs and SSA](c4-graph-coloring.md): *live interval*, *interference*, *spill*
    - [C5. Spilling, splitting and rematerialization](c5-spilling.md): *hole*, *interval splitting*, *second-chance binpacking*
    - [C6. Instruction scheduling](c6-scheduling.md): *register pressure*, *live interval*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *live interval*, *SSA-form linear scan*, *greedy allocation*

## Sources and further reading

This chapter's algorithm and its terms follow Poletto and Sarkar's paper directly; both example programs are original code checked against that description, not transcribed from it.[^ps99] The production-allocator numbers are quoted from their own announcements, each with the compiler and version they describe, not measured on this book's own machine.

[^chaitin82]: Gregory J. Chaitin, "Register allocation & spilling via graph coloring", SIGPLAN '82. <https://doi.org/10.1145/800230.806984>
[^ps99]: Massimiliano Poletto and Vivek Sarkar, "Linear Scan Register Allocation", ACM TOPLAS 21(5), 1999. <https://doi.org/10.1145/330249.330250> (author copy: <http://web.cs.ucla.edu/~palsberg/course/cs132/linearscan.pdf>)
[^ths98]: David Traub, Glenn Holloway and Michael D. Smith, "Quality and speed in linear-scan register allocation", PLDI 1998. <https://doi.org/10.1145/277650.277714>
[^wm05]: Christian Wimmer and Hanspeter Mössenböck, "Optimized interval splitting in a linear scan register allocator", VEE 2005. <https://doi.org/10.1145/1064979.1064998>
[^wf10]: Christian Wimmer and Michael Franz, "Linear scan register allocation on SSA form", CGO 2010. <https://doi.org/10.1145/1772954.1772979>
[^olesen11]: Jakob Stoklund Olesen, "Greedy Register Allocation in LLVM 3.0", LLVM Project Blog, 18 September 2011. <https://blog.llvm.org/2011/09/greedy-register-allocation-in-llvm-30.html>
[^regalloc2]: Chris Fallin, "Correctness in Register Allocation" (2021), on the checker built for Cranelift's allocator, and his 2022 post on regalloc2 itself. <https://cfallin.org/blog/2021/03/15/cranelift-isel-3/> ; <https://cfallin.org/blog/2022/06/09/cranelift-regalloc2/>
[^stage10]: [Build v0.1, stage 10, "The program the milestone asks for"](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for); array dimensions known at compile time follow [Arrays, decision 11](../decisions/arrays.md#d11) and [Arrays and shapes, 7.2](../specification/arrays.md#72-dimension-rules).
[^numbers56]: [Numbers, decision 56](../decisions/numbers.md#d56): every `f32` and `f64` operation gives the IEEE 754 result, rounded to nearest with ties to even, with no contraction, reordering or wider format.
