# C5. Spilling, splitting and rematerialization

<p class="page-intro">When more values are live than there are registers, the allocator must send some of them to memory. This chapter is about doing that cheaply: choosing which value to give up, putting its loads and stores outside the loops, and recomputing values instead of reloading them. For Vortex, it decides whether a kernel's inner loop touches only its arrays or also the stack.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 45 minutes · Builds on: [C3. Register allocation I: linear scan](c3-linear-scan.md), [C4. Register allocation II: graphs and SSA](c4-graph-coloring.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is a value's live range?"

        The set of program points where the value is live: a later
        instruction will still read it before anything overwrites it. It
        starts at the definition and ends at the last use, and it can have
        holes where the value is not needed.

        Introduced in [C2. Liveness](c2-liveness.md#live-ranges-and-live-intervals).

    ??? question "When linear scan finds every register taken, which interval does it spill?"

        Among the intervals holding a register and the one arriving now,
        the one whose end point is furthest away.

        Introduced in [C3. Register allocation I: linear scan](c3-linear-scan.md#the-algorithm-on-eight-intervals-and-three-registers).

    ??? question "How many registers does a function in SSA form need?"

        Exactly Maxlive: the largest number of values live at any one
        point. It cannot need fewer, since that many values are live
        together, and its chordal interference graph can always be colored
        with that many.

        Introduced in [C4. Register allocation II: graphs and SSA](c4-graph-coloring.md#ssa-graphs-color-with-maxlive-registers).

    ??? question "Which general-purpose registers must a function called under AAPCS64 give back unchanged?"

        The callee-saved ones, `x19` to `x28` (plus the frame pointer and
        the stack pointer). Every other register may hold garbage when a
        call returns.

        Introduced in [A4. Calling conventions and ABIs](a4-calling-conventions.md#caller-saved-and-callee-saved-registers).

    ??? question "Where in a stack frame do a function's own temporaries live?"

        In slots in the frame's local area, addressed at a fixed offset from
        `sp` or the frame pointer. The back end gives each slot its final
        offset only after register allocation, once it knows everything
        the frame must hold.

        Introduced in [A5. Stack frames](a5-stack-frames.md#frame-layout-what-goes-where).

!!! goals "In this chapter"

    - Explain what spill code is, and why spilling a value lowers register pressure only between its uses, never at them.
    - Compute a spill cost from loop depth, and use it to choose which value to give up in a small kernel.
    - Place spill code outside loops by splitting a live range, and count what that saves.
    - Recognize which values can be rematerialized, and compare recomputing them with reloading them on AArch64.
    - Test a Vortex allocator's spilling by forcing it to run with fewer registers than the machine has.

## When registers run out

Here is a straight-line block with one input, the address `p`, and three
registers to work with:

```text
       live-in: p
    1  a = load [p]
    2  b = load [p + 8]
    3  c = a + 1
    4  d = b * c
    5  e = a + d
    6  f = b + c
    7  g = e * f
    8  return g
```

Count the values live after each instruction. After 2, `p` is dead and `a`
and `b` are live: two. After 3, `a`, `b` and `c`: three. After 4, all of
`a`, `b`, `c` and `d` are still needed (`a` and `d` by instruction 5, `b`
and `c` by instruction 6): four. After 5 the count drops back to three
(`b`, `c`, `e`), then two, then one.

The number of values live at one point is the **register pressure** at that
point. The largest pressure anywhere in a function is **Maxlive**. Here
Maxlive is 4 and there are 3 registers, so no assignment of registers to
values can work, however clever: at the point after instruction 4, four
values need four homes. Pressure is a property of a point, not of the
function. This block is short of registers at exactly one place.

<figure class="vx-figure">
<div class="vx-stepper">
<div class="vx-step">
<p><strong>Before.</strong> Each bar is one value's live range. The row underneath counts the bars that cross each gap between instructions. After instruction 4, four ranges cross, one more than there are registers.</p>
<svg viewBox="0 0 720 360" role="img" aria-label="Live ranges of p, a, b, c, d, e, f and g over instructions 1 to 8, with register pressure 2, 2, 3, 4, 3, 2, 1 between them. The 4 after instruction 4 is marked as more than three registers.">
<text class="vx-text" x="20" y="54">p</text>
<rect class="vx-box" x="60" y="40" width="120" height="18" rx="3"/>
<text class="vx-text" x="20" y="84">a</text>
<rect class="vx-box" x="100" y="70" width="320" height="18" rx="3"/>
<text class="vx-text" x="20" y="114">b</text>
<rect class="vx-box-strong" x="180" y="100" width="320" height="18" rx="3"/>
<text class="vx-text" x="20" y="144">c</text>
<rect class="vx-box" x="260" y="130" width="240" height="18" rx="3"/>
<text class="vx-text" x="20" y="174">d</text>
<rect class="vx-box" x="340" y="160" width="80" height="18" rx="3"/>
<text class="vx-text" x="20" y="204">e</text>
<rect class="vx-box" x="420" y="190" width="160" height="18" rx="3"/>
<text class="vx-text" x="20" y="234">f</text>
<rect class="vx-box" x="500" y="220" width="80" height="18" rx="3"/>
<text class="vx-text" x="20" y="264">g</text>
<rect class="vx-box" x="580" y="250" width="80" height="18" rx="3"/>
<line class="vx-line" x1="60" y1="282" x2="680" y2="282"/>
<g class="vx-text-muted" font-size="11">
<text x="100" y="298" text-anchor="middle">1</text>
<text x="180" y="298" text-anchor="middle">2</text>
<text x="260" y="298" text-anchor="middle">3</text>
<text x="340" y="298" text-anchor="middle">4</text>
<text x="420" y="298" text-anchor="middle">5</text>
<text x="500" y="298" text-anchor="middle">6</text>
<text x="580" y="298" text-anchor="middle">7</text>
<text x="660" y="298" text-anchor="middle">8</text>
</g>
<text class="vx-text-muted" x="20" y="336">live</text>
<g class="vx-mono">
<text x="140" y="336" text-anchor="middle">2</text>
<text x="220" y="336" text-anchor="middle">2</text>
<text x="300" y="336" text-anchor="middle">3</text>
<rect class="vx-box-bad" x="366" y="318" width="28" height="26" rx="3"/>
<text x="380" y="336" text-anchor="middle">4</text>
<text x="460" y="336" text-anchor="middle">3</text>
<text x="540" y="336" text-anchor="middle">2</text>
<text x="620" y="336" text-anchor="middle">1</text>
</g>
</svg>
</div>
<div class="vx-step">
<p><strong>After spilling <code>b</code>.</strong> A store right after instruction 2 copies <code>b</code> to its stack slot. <code>b</code> keeps its register until instruction 4 reads it, then gives it up, and a load right before instruction 6 brings it back. The pressure after instruction 4 falls to three.</p>
<svg viewBox="0 0 720 360" role="img" aria-label="The same live ranges with b cut into three parts: a piece in a register from its definition to its use at instruction 4, a dashed piece in its stack slot from there to right before instruction 6, and a short reloaded piece. Pressure is now 2, 2, 3, 3, 3, 2, 1.">
<text class="vx-text" x="20" y="54">p</text>
<rect class="vx-box" x="60" y="40" width="120" height="18" rx="3"/>
<text class="vx-text" x="20" y="84">a</text>
<rect class="vx-box" x="100" y="70" width="320" height="18" rx="3"/>
<text class="vx-text" x="20" y="114">b</text>
<rect class="vx-box-strong" x="180" y="100" width="160" height="18" rx="3"/>
<rect class="vx-box-bad" x="340" y="100" width="136" height="18" rx="3"/>
<text class="vx-text-muted" x="408" y="113" text-anchor="middle" font-size="11">in its slot</text>
<rect class="vx-box-strong" x="476" y="100" width="24" height="18" rx="3"/>
<text class="vx-text-accent" x="204" y="96" text-anchor="middle">store</text>
<text class="vx-text-accent" x="488" y="96" text-anchor="middle">reload</text>
<text class="vx-text" x="20" y="144">c</text>
<rect class="vx-box" x="260" y="130" width="240" height="18" rx="3"/>
<text class="vx-text" x="20" y="174">d</text>
<rect class="vx-box" x="340" y="160" width="80" height="18" rx="3"/>
<text class="vx-text" x="20" y="204">e</text>
<rect class="vx-box" x="420" y="190" width="160" height="18" rx="3"/>
<text class="vx-text" x="20" y="234">f</text>
<rect class="vx-box" x="500" y="220" width="80" height="18" rx="3"/>
<text class="vx-text" x="20" y="264">g</text>
<rect class="vx-box" x="580" y="250" width="80" height="18" rx="3"/>
<line class="vx-line" x1="60" y1="282" x2="680" y2="282"/>
<g class="vx-text-muted" font-size="11">
<text x="100" y="298" text-anchor="middle">1</text>
<text x="180" y="298" text-anchor="middle">2</text>
<text x="260" y="298" text-anchor="middle">3</text>
<text x="340" y="298" text-anchor="middle">4</text>
<text x="420" y="298" text-anchor="middle">5</text>
<text x="500" y="298" text-anchor="middle">6</text>
<text x="580" y="298" text-anchor="middle">7</text>
<text x="660" y="298" text-anchor="middle">8</text>
</g>
<text class="vx-text-muted" x="20" y="336">in registers</text>
<g class="vx-mono">
<text x="140" y="336" text-anchor="middle">2</text>
<text x="220" y="336" text-anchor="middle">2</text>
<text x="300" y="336" text-anchor="middle">3</text>
<text x="380" y="336" text-anchor="middle">3</text>
<text x="460" y="336" text-anchor="middle">3</text>
<text x="540" y="336" text-anchor="middle">2</text>
<text x="620" y="336" text-anchor="middle">1</text>
</g>
</svg>
</div>
</div>
<figcaption>Figure 1. The eight-instruction block, before and after spilling <code>b</code>. Spilling cuts a live range into short pieces in registers around its definition and its uses, joined by a piece in memory. Only the memory piece stops competing for a register, so the spill helps exactly where that piece lies.</figcaption>
</figure>

An allocator that is short of registers has three moves, and every
allocator in [C3](c3-linear-scan.md) and [C4](c4-graph-coloring.md)
reaches them one way or another:

- **Spilling** a value means keeping it in memory, in a **stack slot**
  reserved for it in the function's frame, for some part of its life:
  a store puts it there, and a load brings it back before it is used.
- **Splitting** a live range means cutting it into shorter pieces that are
  allocated separately, so that one value can sit in a register in one part
  of the program and in memory, or in a different register, in another.
- **Rematerializing** a value means computing it again where it is needed
  instead of loading it back, which is possible when it is cheap to compute
  from things that are still at hand, such as a constant.

The rest of the chapter takes them in that order, then puts them together
the way modern allocators do.

## What spill code is

Spill `b` in the block above. A store follows its definition, and a load
comes before its use after the peak:

```text
    2  b = load [p + 8]
       store b -> [slot_b]       spill
    3  c = a + 1
    4  d = b * c                 b's register is free after this
    5  e = a + d
       b' = load [slot_b]        reload
    6  f = b' + c
```

`b` keeps its register until instruction 4, its last use before the peak,
and the store has already put a copy in the slot. A simpler spiller places
a store after every definition and a load before every use, so it would
also reload `b` before instruction 4. That rule is called **spill
everywhere**, and it is what Chaitin's allocator does: once a value is
chosen, it is stored at each definition and reloaded at each
use.[^chaitin82] Figure 1 shows the placement in the listing.

Two facts fall out of this listing, and both shape everything that follows.

First, spilling does not make a value disappear. The spilled `b` becomes
several short live ranges, one around each definition and one around each
use, and each of those still needs a register for an instant. Chaitin makes
the same point: a spilled node is not removed from the graph, because the
program must still reload the value at each use and store it at each
definition, so the interference graph has to be rebuilt after spill code is
inserted.[^chaitin82]

Second, spilling a value helps only between its uses. Try spilling `a`
instead of `b`. `a` is read by instruction 5, right where the pressure
peaks, so it must be back in a register there, and the pressure after
instruction 4 is four again. Chaitin's allocator guards against a sharper form
of the same mistake: when a value is local to one block and nothing dies
between its definition and its last use, spilling it cannot make the
program colorable, so its spill cost is set to infinity.[^chaitin82]

??? check "Instead of `b`, could you spill `c` to fix the block, and what would it cost?"

    Yes. `c` is not read by instruction 5, so a store after its definition
    at instruction 3, giving up its register after instruction 4 reads it,
    and a reload before instruction 6 take it out of a register across the
    peak. The pressure after 4 becomes three. It costs the same as spilling
    `b`, one store and one load. `d` and `a` would not help: both are read
    by instruction 5.

### Which value to evict in straight-line code

In one basic block, there is a rule with a long record. When a register
must be freed, evict the value whose **next use** is furthest in the
future. After instruction 4, the next uses are `a` at 5, `d` at 5, `b` at 6
and `c` at 6, so `b` or `c` goes, which is what the check above found.

The rule comes from paging, not from compilers. Belady's **MIN algorithm**
replaces the memory page whose next use is furthest away, and with full
knowledge of the future it makes the fewest possible replacements. Braun and
Hack report that the same rule, applied to the registers of one basic
block, has been highly successful for straight-line code, and extend it to
whole control-flow graphs; a later section returns to their
version.[^braunhack]
A reload is the register version of a page fault, and a value with no
further use costs nothing to evict.

## Choosing whom to spill: cost

Straight-line code hides the question that matters most. A load in a loop
runs once per trip, so a spill that puts a load in a loop can cost
thousands of loads where a spill elsewhere costs one. The allocator needs
an estimate of how often each instruction runs.

Chaitin's allocator makes the estimate from the program's shape. It
assumes each instruction takes one cycle, and that code in a loop runs ten
times as often as the code around it. The **spill cost** of a value is then
the number of its definitions and uses, each weighted by that estimated
frequency, so a touch at loop depth $d$ counts $10^d$. When coloring is
blocked, the allocator spills the value whose cost divided by its current
degree in the interference graph is smallest.[^chaitin82] Dividing by the
degree favours values that interfere with many others: removing one of
those relieves more neighbours at once.

### The kernel's values, by hand

Take the stage 10 kernel,[^stage10] with its three nested loops over fixed
shapes: `row` runs 2 trips, `column` 2 and `k` 3. A plain lowering, before
any loop optimization, looks like this:

```text
    row = 0                                   depth 0
    row loop:    if row >= 2 exit             depth 1
      column = 0
      column loop: if column >= 2 exit        depth 2
        sum = 0.0 ; k = 0
        k loop:  if k >= 3 exit               depth 3
          x = load a[row, k]
          y = load b[k, column]
          sum = sum + x * y
          k = k + 1
        store c[row, column] = sum            depth 2
        column = column + 1
      row = row + 1                           depth 1
```

Consider the six general-purpose values: the three array addresses `a`,
`b` and `c`, which arrive in registers as arguments, and the counters.
List the loop depth of each definition and use:

| Value | Touches (loop depth of each) | Cost, $10^d$ |
| --- | --- | --- |
| `a` | argument 0, load 3 | 1001 |
| `b` | argument 0, load 3 | 1001 |
| `c` | argument 0, store 2 | 101 |
| `row` | init 0, compare 1, load 3, store 2, increment 1 and 1 | 1131 |
| `column` | init 1, compare 2, load 3, store 2, increment 2 and 2 | 1410 |
| `k` | init 2, compare 3, two loads 3, increment 3 and 3 | 5100 |

All six are live together inside the `k` loop, so in the interference graph
they form a clique and have the same degree. Dividing by it changes
nothing, and `c` is the value to spill: it is the only one that no
instruction in the innermost loop touches. Spilled everywhere, `c` costs one
store on entry and one load per trip of the `column` loop, four loads per
call, and none of them in the `k` loop.

<figure class="vx-figure">
<svg viewBox="0 0 720 360" role="img" aria-label="The plainly lowered kernel on the left, with its row, column and k loops shaded as nested bands. On the right, one vertical line per value, a, b, c, row, column and k, with a dot at each line of code that defines or uses it. Every value except c has a dot inside the k loop band; c's line crosses the band with no dot and is touched only by the store after the loop.">
<rect class="vx-box" x="30" y="58" width="400" height="266" rx="4"/>
<rect class="vx-box" x="50" y="102" width="375" height="198" rx="4"/>
<rect class="vx-box-accent" x="70" y="146" width="350" height="110" rx="4"/>
<text class="vx-text-muted" x="425" y="252" text-anchor="end" font-size="11">k loop: 12 runs per call</text>
<text class="vx-mono" x="40" y="54" font-size="12">row = 0</text>
<text class="vx-mono" x="40" y="76" font-size="12">row loop: row &lt; 2 ?</text>
<text class="vx-mono" x="60" y="98" font-size="12">column = 0</text>
<text class="vx-mono" x="60" y="120" font-size="12">column loop: column &lt; 2 ?</text>
<text class="vx-mono" x="80" y="142" font-size="12">sum = 0.0 ; k = 0</text>
<text class="vx-mono" x="80" y="164" font-size="12">k loop: k &lt; 3 ?</text>
<text class="vx-mono" x="100" y="186" font-size="12">x = load a[row, k]</text>
<text class="vx-mono" x="100" y="208" font-size="12">y = load b[k, column]</text>
<text class="vx-mono" x="100" y="230" font-size="12">sum = sum + x * y</text>
<text class="vx-mono" x="100" y="252" font-size="12">k = k + 1</text>
<text class="vx-mono" x="80" y="274" font-size="12">store c[row, column] = sum</text>
<text class="vx-mono" x="60" y="296" font-size="12">column = column + 1</text>
<text class="vx-mono" x="40" y="318" font-size="12">row = row + 1</text>
<text class="vx-text" x="470" y="30" text-anchor="middle">a</text>
<line class="vx-line" x1="470" y1="40" x2="470" y2="322"/>
<circle class="vx-dot" cx="470" cy="182" r="4"/>
<text class="vx-text" x="510" y="30" text-anchor="middle">b</text>
<line class="vx-line" x1="510" y1="40" x2="510" y2="322"/>
<circle class="vx-dot" cx="510" cy="204" r="4"/>
<text class="vx-text" x="550" y="30" text-anchor="middle">c</text>
<rect class="vx-box-accent" x="547" y="40" width="6" height="282"/>
<circle class="vx-dot" cx="550" cy="270" r="4"/>
<text class="vx-text" x="600" y="30" text-anchor="middle">row</text>
<line class="vx-line" x1="600" y1="50" x2="600" y2="322"/>
<circle class="vx-dot" cx="600" cy="50" r="4"/>
<circle class="vx-dot" cx="600" cy="72" r="4"/>
<circle class="vx-dot" cx="600" cy="182" r="4"/>
<circle class="vx-dot" cx="600" cy="270" r="4"/>
<circle class="vx-dot" cx="600" cy="314" r="4"/>
<text class="vx-text" x="650" y="30" text-anchor="middle">column</text>
<line class="vx-line" x1="650" y1="94" x2="650" y2="300"/>
<circle class="vx-dot" cx="650" cy="94" r="4"/>
<circle class="vx-dot" cx="650" cy="116" r="4"/>
<circle class="vx-dot" cx="650" cy="204" r="4"/>
<circle class="vx-dot" cx="650" cy="270" r="4"/>
<circle class="vx-dot" cx="650" cy="292" r="4"/>
<text class="vx-text" x="698" y="30" text-anchor="middle">k</text>
<line class="vx-line" x1="698" y1="138" x2="698" y2="256"/>
<circle class="vx-dot" cx="698" cy="138" r="4"/>
<circle class="vx-dot" cx="698" cy="160" r="4"/>
<circle class="vx-dot" cx="698" cy="182" r="4"/>
<circle class="vx-dot" cx="698" cy="204" r="4"/>
<circle class="vx-dot" cx="698" cy="248" r="4"/>
<text class="vx-text-accent" x="550" y="348" text-anchor="middle">c: no touch in the k loop</text>
</svg>
<figcaption>Figure 2. The kernel's general-purpose values over its plain lowering. Each dot is a definition or a use. The shaded band is the <code>k</code> loop, whose body runs twelve times per call. Every value but <code>c</code> is touched inside it; <code>c</code> crosses the band untouched, so spilling it puts no load or store in the innermost loop.</figcaption>
</figure>

--8<-- "includes/examples/backend/c5-spilling/spill_cost.cpp.md"

The second column is a Vortex-specific refinement. The ten-per-loop guess
exists because a compiler usually does not know how many times a loop runs.
A Vortex kernel over fixed-shape arrays does: the body of the `k` loop runs
exactly $2 \times 2 \times 3 = 12$ times. The exact weights 1, 2, 4 and 12
change every cost but not the order here. On a loop that runs twice inside
one that runs a thousand times, they would.

At full size neither AArch64 nor x86-64 needs to spill anything in this
kernel ([C4](c4-graph-coloring.md#applying-it-to-the-vortex-matmul-kernel)).
The costs matter when something takes registers away: a bigger kernel,
unrolling ([P7](../optimize/p7-loop-transformations.md)), a call, or a test
that caps the register count on purpose, which this chapter's exercise
builds.

??? check "Cap the general-purpose registers at four instead of five. After `c` is spilled, which value goes next, and where do its loads land?"

    `a` or `b`, tied at 1001. Counting only the six named values, five
    are still live inside the `k` loop (`a`, `b`, `row`, `column`, `k`),
    so one of them must be reloaded
    in the loop no matter what, and the cheapest has only one touch there.
    Its load runs on every trip of the `k` loop: twelve times per call.
    This is a case where spilling in the loop cannot be avoided, only made
    as rare as possible.

## Where the spill code goes

The cost decides which value to give up. It does not decide where the
loads and stores go, and spill everywhere puts them at every use, loops
included. The waste shows up in two situations that Braun and Hack use to
open their paper:[^braunhack]

1. A value is defined before a loop in which pressure is high, and used
   only after it. The good placement is a store before the loop and a load
   after it. Here spill everywhere does no harm: the value has no use in
   the loop, so it has no load there. That was `c` in the kernel.
2. A value is used in a loop, but had to leave its register before the
   loop, because pressure was high there. Spill everywhere loads it before
   every use, on every trip. Loading it once, in front of the loop, is
   better whenever the loop has a free register.

The second case needs a way to say "memory before the loop, register from
the loop's entry on". That is exactly a split: cut the live range at the
loop entry, spill the first piece, and give the second piece a register.
The only new instruction is a load on the edge into the loop, which runs
once per entry into the loop, not once per trip.

<figure class="vx-figure">
<svg viewBox="0 0 720 300" role="img" aria-label="Two timelines for one value. In spill everywhere, a store follows the definition and a load precedes the read inside the loop, which runs on every trip, plus one more load after the loop. In split, the store is the same but a single load on the loop's entry edge puts the value in a register for the whole loop and the read after it.">
<text class="vx-text" x="20" y="26">Spill everywhere</text>
<rect class="vx-box" x="300" y="40" width="240" height="90" rx="4"/>
<text class="vx-text-muted" x="420" y="58" text-anchor="middle">loop, n trips</text>
<path class="vx-line" d="M 520 118 C 560 118 560 70 520 70" />
<line class="vx-line" x1="70" y1="100" x2="660" y2="100" stroke-dasharray="4 4"/>
<rect class="vx-box-strong" x="66" y="94" width="12" height="12"/>
<text class="vx-text-muted" x="72" y="84" text-anchor="middle">def, store</text>
<rect class="vx-box-bad" x="120" y="88" width="150" height="24" rx="3"/>
<text class="vx-text-muted" x="195" y="126" text-anchor="middle">no free register</text>
<circle class="vx-dot" cx="420" cy="100" r="6"/>
<text class="vx-text-accent" x="420" y="84" text-anchor="middle">load, every trip</text>
<circle class="vx-dot" cx="620" cy="100" r="6"/>
<text class="vx-text-muted" x="620" y="84" text-anchor="middle">load</text>
<text class="vx-text" x="20" y="176">Split at the loop entry</text>
<rect class="vx-box" x="300" y="190" width="240" height="90" rx="4"/>
<text class="vx-text-muted" x="420" y="208" text-anchor="middle">loop, n trips</text>
<path class="vx-line" d="M 520 268 C 560 268 560 220 520 220" />
<line class="vx-line" x1="70" y1="250" x2="300" y2="250" stroke-dasharray="4 4"/>
<line class="vx-flow" x1="300" y1="250" x2="660" y2="250"/>
<rect class="vx-box-strong" x="66" y="244" width="12" height="12"/>
<text class="vx-text-muted" x="72" y="234" text-anchor="middle">def, store</text>
<rect class="vx-box-bad" x="120" y="238" width="150" height="24" rx="3"/>
<text class="vx-text-muted" x="195" y="276" text-anchor="middle">no free register</text>
<circle class="vx-dot" cx="290" cy="250" r="6"/>
<text class="vx-text-accent" x="290" y="220" text-anchor="end">one load, on the entry edge</text>
<text class="vx-text-muted" x="560" y="238">in a register</text>
</svg>
<figcaption>Figure 3. One value, spilled two ways. Dashed segments are the value in its stack slot; the flowing segment is the value in a register. Spilling everywhere loads it inside the loop, so the load runs once per trip. Splitting at the loop entry moves the only load onto the edge into the loop, where it runs once.</figcaption>
</figure>

The example counts the loads and stores that run in the second situation,
for a value only read in the loop (an array base) and for one read and
written on every trip (a running sum). It assumes the free register stays
free through the read after the loop. Twelve trips is the `k` loop's count
for one call of the kernel:

--8<-- "includes/examples/backend/c5-spilling/spill_everywhere_vs_split.cpp.md"

Spill everywhere grows with the trip count, and for the sum it also puts a
store in every trip, because the sum is redefined there. The split version
does not depend on the trip count at all.

### Choosing split points

Wimmer and Mössenböck's linear-scan allocator for the HotSpot client
compiler splits intervals whenever pressure is too high, then moves each
split to a better place using three rules that need no dataflow
analysis:[^wm05]

- **Move split points out of loops.** A spill or reload may move earlier
  than where the allocator first decided it, so a reload that would land
  inside a loop moves in front of it.
- **Move split points to block boundaries.** Where control flow already
  separates the pieces, the move that joins them may become unnecessary on
  some edges.
- **Store once, after the definition.** Most values are defined once and
  used many times. If the one store happens right after the definition,
  the slot is up to date on every path, and every later store to it can be
  deleted, however often the value is spilled and reloaded.

The same allocator adds pseudo uses at the end of each loop, right before
the backward branch, for values used in the loop. Its eviction rule looks
only at future uses, and without them it could spill a value whose next
use is back at the top of the loop.[^wm05]

A split has a price of its own. When the pieces on two sides of an edge
end up in different places, a move has to run on that edge: a load, a
store, or a register copy. These are the same parallel moves that SSA
destruction creates, and [C4](c4-graph-coloring.md#moves-that-still-have-to-run)
shows how to sequence them.

LLVM's greedy allocator makes splitting its main tool. When a live range
finds no free register, it first tries to evict ranges with a lower spill
weight, then to split the range around the regions where it is busy, such
as one hot loop, and spills only when splitting will not help.[^olesen]
Cranelift's regalloc2 splits a bundle of ranges at the point where a
conflict first appears, and moves pieces that do not need a register into
a **spill bundle** that is allowed to live in a stack slot.[^regalloc2]
[E3](e3-llvm-allocator-scheduler-mc.md#the-greedy-allocator)
reads LLVM's version in detail.

??? check "A value is defined before a loop, read once on every trip, and read again after the loop. Pressure is high before the loop and high after it, but the loop has a free register. Where do the spill instructions go?"

    A store after the definition. A load on the edge into the loop, so the
    value is in a register for every trip. After the loop, the register is
    needed by other values again, but the slot is still up to date (the
    value was never changed), so no store is needed on the way out: the
    read after the loop gets its own load right before it. Two loads and one
    store in total, whatever the trip count.

## Calls: a crowded point you did not write

A call is the most common reason for a spill in real code. The callee may
overwrite every **caller-saved** register, so any value live across the call
must spend it either in a **callee-saved** register, which the callee
promises to restore, or in a stack slot. AAPCS64 has ten callee-saved
general-purpose registers, `x19` to `x28`.[^aapcs64] Wimmer and
Mössenböck's allocator models a call as fixed intervals that block the
registers it may overwrite; when every register is blocked that way, an
interval that crosses the call is split before it.[^wm05]

The example loads twelve values, calls a function, and then adds the
twelve up:

--8<-- "includes/examples/backend/c5-spilling/crowded_call.ll.md"

Here is part of what `llc -O2 -mtriple=arm64-apple-macosx` (LLVM 18.1.8)
prints for it, on an Apple M4 Pro with macOS 27 in September 2026:

```text
    stp  x28, x27, [sp, #16]    ; 16-byte Folded Spill
    ...                          (four more pairs, x26 down to x19)
    ldr  x19, [x0]
    ...                          (the other loads fill x19-x28)
    ldr  x9, [x0, #48]
    ldr  x8, [x0, #88]
    stp  x9, x8, [sp]           ; 16-byte Folded Spill
    bl   _tick
    ...
    ldr  x9, [sp]               ; 8-byte Folded Reload
    ...
    mov  x10, #52719            ; =0xcdef
    movk x10, #35243, lsl #16
    ...
    ldr  x9, [sp, #8]           ; 8-byte Folded Reload
```

Ten values live in `x19` to `x28` and two go to stack slots, stored as a
pair right before the call and reloaded after it. The ten callee-saved
registers are not free either: the prologue saves their old contents and
the epilogue restores them, and LLVM labels those saves "Spill" too. The
trade is visible. A callee-saved register costs one store and one load per
call of *this* function. A slot around a call costs one store and one load
per execution of *that* call. When the call sits in a loop, the
callee-saved register wins.

The 64-bit constant was never live across the call. It is built after the
call, near its one use, with a `mov` and three `movk`, so it costs no
register during the call and no slot.

??? check "Vortex's bounds-failure path calls into the runtime, prints an error and exits with status 101. Which of the kernel's values must survive that call?"

    None. The call never returns to the kernel, so nothing is live after
    it and nothing needs a callee-saved register or a slot on its account.
    An allocator that treated it as an ordinary call would force values
    live across it into callee-saved registers or slots, and could add
    spill code to the hot loop for a path that never comes back.

## Rematerializing instead of reloading

Some values do not need a slot at all. If a value can be recomputed
cheaply wherever it is needed, the allocator can drop it from its register
and compute it again before the next use: no store, no slot, no load.
Briggs, Cooper and Torczon name the values that qualify. They must be
cheaply computable from operands available throughout the procedure, such
as integer constants (and on some machines floating-point ones), constant
offsets from the frame pointer or the static data area pointer, and loads
from a known constant location.[^remat]

Chaitin's allocator already took recomputation into account when
estimating spill costs, but only for a live range that holds a single
value.[^chaitin82] Briggs and his coauthors handle the harder case of a
variable that is a constant in one part of the program and a changing value
in another, such as a pointer set to an array's start and then advanced in
a loop. They tag each value, using a variant of Wegman and Zadeck's sparse
constant propagation, with the instruction that computes it or with "not
rematerializable", then split live ranges into pieces with different
tags.[^remat] The constant piece is recomputed, and only the changing piece
competes for a slot.

<figure class="vx-figure">
<svg viewBox="0 0 720 250" role="img" aria-label="Two ways to bring back the address of a global table after a crowded stretch. Reload: adrp and add compute it, a store writes it to a stack slot, and a load reads it back before the use. Rematerialize: adrp and add compute it, the register is dropped, and adrp and add run again right before the use; no slot exists.">
<text class="vx-text" x="20" y="26">Reload</text>
<rect class="vx-box" x="20" y="40" width="175" height="44" rx="4"/>
<text class="vx-mono" x="107" y="58" text-anchor="middle" font-size="11">adrp x8, _t@PAGE</text>
<text class="vx-mono" x="107" y="74" text-anchor="middle" font-size="11">add x8, x8, _t@PAGEOFF</text>
<rect class="vx-box-strong" x="205" y="46" width="120" height="32" rx="4"/>
<text class="vx-mono" x="265" y="67" text-anchor="middle" font-size="11">str x8, [sp, #8]</text>
<rect class="vx-box-bad" x="335" y="46" width="160" height="32" rx="4"/>
<text class="vx-text-muted" x="415" y="67" text-anchor="middle">crowded stretch</text>
<rect class="vx-box-strong" x="505" y="46" width="125" height="32" rx="4"/>
<text class="vx-mono" x="567" y="67" text-anchor="middle" font-size="11">ldr x8, [sp, #8]</text>
<text class="vx-text" x="645" y="67">use</text>
<rect class="vx-box" x="265" y="100" width="302" height="26" rx="4"/>
<text class="vx-text-muted" x="416" y="118" text-anchor="middle">stack slot [sp, #8] holds the address</text>
<text class="vx-text" x="20" y="160">Rematerialize</text>
<rect class="vx-box" x="20" y="174" width="175" height="44" rx="4"/>
<text class="vx-mono" x="107" y="192" text-anchor="middle" font-size="11">adrp x8, _t@PAGE</text>
<text class="vx-mono" x="107" y="208" text-anchor="middle" font-size="11">add x8, x8, _t@PAGEOFF</text>
<text class="vx-text-muted" x="265" y="201" text-anchor="middle">register dropped</text>
<rect class="vx-box-bad" x="335" y="180" width="160" height="32" rx="4"/>
<text class="vx-text-muted" x="415" y="201" text-anchor="middle">crowded stretch</text>
<rect class="vx-box-accent" x="505" y="174" width="175" height="44" rx="4"/>
<text class="vx-mono" x="592" y="192" text-anchor="middle" font-size="11">adrp x8, _t@PAGE</text>
<text class="vx-mono" x="592" y="208" text-anchor="middle" font-size="11">add x8, x8, _t@PAGEOFF</text>
<text class="vx-text" x="688" y="201">use</text>
<text class="vx-text-muted" x="415" y="240" text-anchor="middle">no slot, no store, no load</text>
</svg>
<figcaption>Figure 4. Bringing back a global table's address after a crowded stretch. The reload plan needs a slot, a store and a load. The rematerialize plan runs the same two instructions that computed the address in the first place, because both depend only on the table's name, which never changes.</figcaption>
</figure>

On AArch64 the candidates have concrete costs. A small integer constant is
one `mov`; a full 64-bit constant can take a `mov` and three `movk`, as the
`llc` output above showed; a global's address is `adrp` plus `add`
([A2](a2-aarch64-assembly.md#reaching-a-global-table)); a stack slot's
address is one `add` to `sp`. A running sum is not a candidate at all: it
is computed from its own previous value, which is gone once the register is
reused.

--8<-- "includes/examples/backend/c5-spilling/rematerialize.cpp.md"

The instruction count alone does not settle it. Recomputing a small
constant or a stack address is cheaper on every count. The 64-bit constant
takes sixteen instructions against five for four uses, but none of them
touch memory, while the reload plan makes five memory accesses and needs a
slot. Course notes from CMU list values that rematerialize with one or two
instructions among the things a spill heuristic should favour when breaking
ties.[^cmu-ra] Olesen's post gives rematerializing a constant-pool load,
instead of spilling it to the stack, as an example of what an allocator can
do, and notes that rematerialization can make live ranges shorter or leave
them unused.[^olesen]

??? check "A pointer `q` is set to the address of a global array, and then `q = q + 4` runs on every trip of a loop. Which parts of `q`'s live range can be rematerialized?"

    Only the part before the loop, where `q` still holds the array's
    address: that is `adrp` plus `add`, computed from a name that never
    changes. Once the loop has advanced it, `q` depends on how many trips
    have run, which is not available anywhere else. Splitting the range
    between the two parts is what lets the allocator treat them
    differently.

## Floating-point values and decision 56

Vortex requires every `f32` and `f64` operation to give the IEEE 754
result, rounded to nearest with ties to even, with no contraction, no
reordering and no wider format.[^numbers56] Spill code is safe under that
rule as long as it copies bits. A store of an `s` register and a load back
into one reproduce the value exactly; nothing is rounded on the way.

Rematerialization needs one more thought. Recomputing a floating-point
constant is exact: the same bits come back. Recomputing a floating-point
expression is allowed by the rule only if the recomputation performs the
same operations, one rounding each, on the same inputs. A remat that
turned `x * y + z` into one fused multiply-add to save an instruction would
round once instead of twice and could change the answer, which decision 56
forbids. In practice the question rarely arises: the values worth
rematerializing are constants and addresses, and a running sum like the
kernel's `sum` is never a candidate.

Calls add one AArch64 detail. AAPCS64 makes `v8` to `v15` callee-saved,
but only their low 64 bits; the caller must preserve anything wider.[^aapcs64]
An `f32` or `f64` in `d8` survives a call. A 128-bit vector, which later
chapters on vectorization produce, does not, and must be spilled around the
call or kept in a register the callee does not touch.

## Spill slots

Each spilled value needs a slot, but not a slot of its own. Two spilled
values whose live ranges never overlap can share one, the same way two
values share a register. Regalloc2 tracks spilled bundles in spill sets and
lets several spill sets share one slot, provided none of them
overlap.[^regalloc2] LLVM's code generator pipeline lists a pass named
`StackSlotColoring` among its register allocation passes, a name that
describes the same idea: coloring slots the way an allocator colors
registers.[^braun17]
Sharing keeps the frame small, which matters for recursion and for the
offsets a load or store can encode directly
([A5](a5-stack-frames.md#large-frames-immediates-probes-and-the-guard-page)).

Splitting also changes what a debugger must be told. A variable that was
in `x3` for five instructions, then in a slot, then in `x9`, has a
different location over each address range. DWARF records that with a
location list, one entry per range, and the allocator is the only part of
the compiler that knows the ranges. [D1](d1-debug-info.md) describes the
format.

## Deciding spills before assigning registers

Everything so far makes spill decisions while registers are being
assigned: linear scan decides as it sweeps, and a coloring allocator
decides when coloring gets stuck. SSA form allows a cleaner division. In
SSA form, the number of registers a program needs equals its maximum
register pressure, so once spilling has lowered the pressure to $k$ at
every point, registers can be assigned by a linear-time algorithm that is
guaranteed not to need any more spill code.[^braunhack]
[C4](c4-graph-coloring.md#ssa-graphs-color-with-maxlive-registers) shows
why. The spiller is then a pass of its own, with one precise goal.

Braun and Hack's spiller is such a pass, built on the MIN rule from the
start of this chapter. It walks the blocks in reverse postorder and runs
MIN in each one, with three additions for control flow:[^braunhack]

- **Global next-use distances.** A liveness-style analysis records, for
  each variable, the distance to its next use, taking the minimum where
  paths join. Edges that leave a loop get a huge length, so a use
  after the loop always looks further away than any use inside it.
- **The register set at a block's entry.** At an ordinary block, it is
  taken from what the predecessors had in registers at their exits. At a
  loop header, it is filled with the variables used next inside the loop,
  which has the effect of hoisting reloads out of the loop.
- **Coupling code on edges.** Where a predecessor's exit does not match a
  block's entry, spills and reloads are added on that edge.

Reloads give a variable more than one definition, so the pass finishes by
rebuilding SSA form. On the CINT2000 benchmarks on x86, the paper reports
54.5% fewer executed loads and 61.5% fewer executed stores than a linear-scan
allocator with live-range splitting, and 58.2% and 41.9% fewer than a
graph-coloring allocator.[^braunhack]

QBE, a small back end, takes the same route. Its own summary lists a
spiller separate from the register allocator, made possible by SSA form,
and a spilling heuristic based on loop analysis.[^qbe]

??? check "Braun and Hack give edges that leave a loop a huge length. What would go wrong in the kernel's `k` loop without it?"

    Measured in instructions, the store into `c` after the loop and the
    load from `a` at the top of the next trip are each about one
    instruction past the loop header's compare. Plain distances rate `c`
    no further away than `a`, so MIN may evict `a` and put its reload
    inside the loop, on every trip. With the long exit edge, every use
    after the loop is further away than any use inside it, and `c`, which
    the loop never touches, is evicted instead.

## For Vortex

!!! vortex "Exercise"

    **Build.** Extend the linear-scan allocator from [C3](c3-linear-scan.md)
    (or the coloring allocator from [C4](c4-graph-coloring.md), if that is
    the one you kept) in four steps, each tested before the next:

    1. A debug option that caps the number of registers the allocator may
       use in each register class, below what the machine has. It exists
       only for tests: real programs rarely run out of AArch64's registers,
       and code that never runs is code that is never tested.
    2. Spill choice weighted by loop depth, as in this chapter's cost table.
       Where your compiler knows a loop's trip count, you may use exact
       counts instead; record which one you chose and why.
    3. Splitting at loop boundaries: a value that must leave its register
       before a loop, but has a register free inside it, is reloaded once
       on the loop's entry edge, not inside the loop. A value defined once
       is stored once, right after its definition.
    4. Rematerialization of integer constants, global addresses and stack
       slot addresses: such a value never gets a spill slot.

    **Not yet.** Braun and Hack's SSA spiller, spill-slot sharing, and
    anything that uses profile data. Do not change the stage 10 kernel or
    your instruction selector to avoid spills; the allocator has to cope.

    **The test that proves it works.**

    - *Same output under pressure.* Run every program in your test suite,
      including stage 10's kernel with its known answer, at every register
      cap from the smallest your instruction set allows up to the full
      count, and compare the output byte for byte with the uncapped run.
      Extend C3's allocation checker to stack slots: every reload must read
      a slot last written with the same value, the idea behind the checker
      Fallin built for regalloc2.[^fallin]
    - *Placement.* Write a test program with a crowded stretch before a
      loop and a value read inside it. Under a cap that forces that value
      out, assert that the loop body contains no load from a stack slot.
    - *Rematerialization.* Write a test program in which a large integer
      constant and a global's address are live across a crowded stretch.
      Under a cap, assert that neither is stored to the stack.
    - *The kernel stays clean.* At the full register count, assert that
      the `k` loop of stage 10's kernel contains no load or store through
      `sp` or `x29`: only the loads from `a` and `b`.

    Fill in a table from your own runs for the kernel, counting the spill
    loads and stores in the `k` loop's instructions:

    | GPR cap | Spill everywhere: loads, stores in `k` loop | With splitting: loads, stores in `k` loop | Values spilled |
    | --- | --- | --- | --- |
    | full | | | |
    | 5 | | | |
    | 4 | | | |

    **Done when** all four tests pass, and each fails for a deliberate,
    temporary breakage: a reload placed at the use instead of on the loop
    entry edge, rematerialization turned off, and two spilled values given
    the same slot while both are live.

## Key ideas

!!! recap "You can now answer"

    - **Why can spilling a value fail to lower the register pressure where it is too high?** A spilled value still needs a register at each of its uses; if one of them is at the crowded point, spilling it does not help there.
    - **How does Chaitin estimate the cost of spilling a value?** Its definitions and uses, each weighted by ten for every loop around it, divided by its degree when choosing among candidates.
    - **In straight-line code, which value should be evicted?** The one whose next use is furthest away, Belady's MIN rule.
    - **What does splitting at a loop entry buy over spilling everywhere?** A load on the entry edge that runs once, instead of a load (and for a value written in the loop, a store) on every trip.
    - **Which values can be rematerialized?** Those cheaply computable from operands available everywhere: constants, frame-relative and global addresses, loads from a constant location. Never a running sum.
    - **Why is spill code safe under decision 56?** It copies bits and computes nothing; only a rematerialization that recomputes a floating-point expression differently could change a result.
    - **What does SSA form let a spiller do?** Lower the pressure to $k$ everywhere as a separate pass, after which assignment needs no more spill code.

## Where this comes back

!!! next "You will use this again in"

    - [C6. Instruction scheduling](c6-scheduling.md): *register pressure*, *scheduling against spills*
    - [C7. Peephole optimization](c7-peephole.md): *spill slots*, *store and reload pairs*
    - [D1. Debug information](d1-debug-info.md): *location lists*, *a variable that moves between registers and slots*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *the greedy allocator*, *live-range splitting*, *eviction by spill weight*

## Sources and further reading

The examples are original programs that follow the descriptions cited in
their `.toml` files. The `llc` listing was produced on the machine and date
stated beside it. The benchmark percentages are quoted from Braun and
Hack's paper with its benchmark suite and machine; they were not measured
here.

[^chaitin82]: Gregory J. Chaitin, "Register allocation & spilling via graph coloring", SIGPLAN '82 Symposium on Compiler Construction, 1982, section 5. <https://doi.org/10.1145/800230.806984>
[^braunhack]: Matthias Braun and Sebastian Hack, "Register Spilling and Live-Range Splitting for SSA-Form Programs", Compiler Construction (CC), 2009, sections 1 to 5. <https://doi.org/10.1007/978-3-642-00722-4_13>
[^wm05]: Christian Wimmer and Hanspeter Mössenböck, "Optimized interval splitting in a linear scan register allocator", VEE 2005, sections 3.2 and 4. <https://doi.org/10.1145/1064979.1064998>
[^remat]: Preston Briggs, Keith D. Cooper and Linda Torczon, "Rematerialization", PLDI 1992. <https://doi.org/10.1145/143095.143143>
[^olesen]: Jakob Stoklund Olesen, "Greedy Register Allocation in LLVM 3.0", LLVM Project Blog, 18 September 2011. <https://blog.llvm.org/2011/09/greedy-register-allocation-in-llvm-30.html>
[^regalloc2]: Bytecode Alliance, regalloc2 design notes, "ION.md" (bundles, spill bundles, spill sets and slots). <https://github.com/bytecodealliance/regalloc2/blob/main/doc/ION.md>
[^qbe]: QBE, project home page, feature list. <https://c9x.me/compile/>
[^cmu-ra]: Frank Pfenning and André Platzer, "Lecture Notes on Register Allocation", 15-411 Compiler Design, Carnegie Mellon University, 2013, section 7. <https://www.cs.cmu.edu/~fp/courses/15411-f13/lectures/03-regalloc.pdf>
[^braun17]: Matthias Braun, "Welcome to the Back End: The LLVM Machine Representation", 2017 LLVM Developers' Meeting (slide "Code Generation Pipeline"). <https://llvm.org/devmtg/2017-10/slides/Braun-Welcome%20to%20the%20Back%20End.pdf>
[^aapcs64]: Arm, "Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)", sections on general-purpose and SIMD and floating-point registers. <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
[^fallin]: Chris Fallin, "Cranelift, Part 3: Correctness in Register Allocation", 15 March 2021. <https://cfallin.org/blog/2021/03/15/cranelift-isel-3/>
[^stage10]: [Build v0.1, stage 10, "The program the milestone asks for"](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for).
[^numbers56]: [Numbers, decision 56](../decisions/numbers.md#d56): every `f32` and `f64` operation gives the IEEE 754 result, rounded to nearest with ties to even, with no contraction, reordering or wider format.
