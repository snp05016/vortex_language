# P2. The memory hierarchy

<p class="page-intro">Why the same array can be fast or slow depending only on the order you visit it in, and the vocabulary (line, set, associativity, working set) you need to reason about that.</p>

<p class="vx-meta" markdown="1">Level: Foundations · Reading time: about 25 minutes · Builds on: [P1. Measure first](p1-measure-first.md), [Build v0.1, stage 10](../compiler/guide/stage-10-matrix-multiplication.md)</p>

???+ remember "Before you start, remember"

    ??? question "In what order does a Vortex `[f32; rows, columns]` array store its elements?"

        Contiguously, with no gaps, in row-major order: the last index varies
        fastest, so `a[0, 0]` and `a[0, 1]` sit next to each other in memory.

        Introduced in [Build v0.1, stage 10](../compiler/guide/stage-10-matrix-multiplication.md),
        decided in [Arrays, record 43](../decisions/arrays.md#d43).

    ??? question "Why can the compiler treat `c` in `multiply(a: &[..], b: &[..], c: &mut [..])` as never overlapping `a` or `b`?"

        A `&mut` reference is exclusive: while it is alive, no other reference
        to the same data may exist. The compiler can pass that promise to
        LLVM as `noalias` without checking anything at run time.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md).

    ??? question "What is a register?"

        A small, very fast storage slot inside the processor. Instructions
        mostly work on values held in registers, not directly on memory.

        Introduced in [Build v0.1, stage 6](../compiler/guide/stage-6-first-machine-code.md#words-for-this-stage).

    ??? question "What must an optimizing rewrite keep unchanged, according to Vortex's contract?"

        Every byte `print` writes, in the same order, and every checked
        failure that was going to happen, still happens. A rewrite is free to
        change *how* the answer is produced, never *what* it is.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md).

!!! goals "In this chapter"

    - Explain why a value can take a hundred times longer to reach a register depending on where it currently lives.
    - Recognize a cache line, a set, associativity and a working set, and use them to predict whether an access pattern will miss.
    - Distinguish spatial locality from temporal locality, and say which one a given loop depends on.
    - Read a machine's own cache facts with `sysctl` or `getconf` instead of assuming a number from a book.
    - Explain why the naive matmul loop order is memory-bound before any counter confirms it.

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

Count the arithmetic: for an `m` by `k` times `k` by `n` product, this does
`m * n * k` multiplications and the same number of additions. That count does
not change no matter which loop is innermost, whether you swap `row` and
`column`, or whether you split the loops into blocks. And yet real
measurements of exactly this kind of code, at real sizes, show large,
repeatable differences between loop orders that do the identical arithmetic.
Ulrich Drepper measured this directly: reordering the loops of a naive triple
loop on a Core 2 processor brought it down to 23.4% of the original running
time, and working on submatrices brought it down further, to 17.3%[^drepper].
Nothing about the *arithmetic* explains that. Something about *where the
numbers are* when the arithmetic needs them does.

That something is the **memory hierarchy**: the fact that a real computer does
not have one kind of memory, but several, stacked from tiny and fast (a
handful of registers, read in about one clock cycle) to huge and slow (gigabytes
of DRAM, hundreds of cycles away). A processor cannot add two numbers that are
still in DRAM. It has to fetch them, through the stack, into a register first.
This chapter gives you the vocabulary to reason about that fetch: what a
**cache** actually stores, what a **cache line** is, why **associativity**
exists, and what a **working set** means. P3 to P8 then use that vocabulary to
explain, precisely, why each rung of the matmul ladder is faster than the one
below it.

Look at `multiply` again with this in mind. The inner loop reads `a[row, k]`
and `b[k, column]` for a fixed `row` and `column`, as `k` runs from 0 to the
shared dimension. Because Vortex arrays are row-major, `a[row, k]` walks
across one row of `a`: consecutive `k` means consecutive memory. But
`b[k, column]` walks *down a column* of `b`: consecutive `k` means jumping
ahead by one whole row's width in memory every single step. Those two access
patterns are not symmetric, even though the source code treats `a` and `b`
identically. The next two sections say precisely why that asymmetry costs
time.

## Cache lines and why order matters

A processor never fetches a single number from DRAM on its own. It fetches a
fixed-size, aligned chunk that contains that number, called a **cache line**:
the smallest unit a cache moves in one transfer. On the machine this chapter
was written on, an Apple M4 Pro, that unit is 128 bytes (`sysctl
hw.cachelinesize`, measured 2026-09-23): enough for 32 consecutive `f32`
values. Ask for `a[0, 0]` and the cache brings in `a[0, 0]` through roughly
`a[0, 31]` together, whether you asked for the rest of them or not. Line sizes
vary by machine, most commonly 64 or 128 bytes; the number is never something
to hard-code, only something to query, which is exactly what this chapter's
"For Vortex" exercise asks you to do.

This single fact explains the asymmetry above. Walking `a[row, k]` for
increasing `k` stays inside the same cache line for 32 steps before crossing
into the next one: the first access in a line pays the cost of fetching it,
and the next 31 are, from the cache's point of view, already there.
Walking `b[k, column]` for increasing `k` jumps by one full row's width in
memory on every step. Once that stride reaches the line size, essentially
every access lands in a line the cache has not seen yet.

`traversal_order.cpp` makes this exact, for a small matrix, by counting **line
transitions**: the number of times two consecutive visits land in different
cache lines. It does not measure time; it computes a property of an access
pattern, which is deterministic and needs no clock.

--8<-- "includes/examples/optimize/p2-memory-hierarchy/traversal_order.cpp.md"

For an 8-row, 64-column array of `f32`, with the real 128-byte line from
above, row-major traversal (matching how the array is stored) crosses into a
new line 16 times over its 512 visits. Column-major traversal of the same
array crosses into a new line on every single visit but the first: 512
transitions. The arithmetic performed by a loop that visits every element
exactly once is identical either way. The number of cache lines it disturbs is
not.

<figure class="vx-figure">
<svg viewBox="0 0 760 430" role="img" aria-label="A four-by-four array, storage fixed, visited in row order and then in column order" aria-describedby="p2-f1-desc">
<desc id="p2-f1-desc">A grid of 16 cells arranged as 4 rows of 4, colored in bands of 2 cells to represent one cache line each. The top panel numbers all 16 visits in row-major order: consecutive numbers usually share a color before the color changes. The bottom panel numbers the same 16 cells in column-major order: the color changes on almost every consecutive number.</desc>
<text class="vx-text-accent" x="16" y="24" font-size="15" font-weight="600">Row-major visiting order (stride 1)</text>
<g transform="translate(16,36)">
<rect class="vx-box" x="0" y="0" width="40" height="40" rx="4"/>
<text class="vx-mono" x="20" y="25" font-size="14" text-anchor="middle">1</text>
<rect class="vx-box" x="44" y="0" width="40" height="40" rx="4"/>
<text class="vx-mono" x="64" y="25" font-size="14" text-anchor="middle">2</text>
<rect class="vx-box-accent" x="88" y="0" width="40" height="40" rx="4"/>
<text class="vx-mono" x="108" y="25" font-size="14" text-anchor="middle">3</text>
<rect class="vx-box-accent" x="132" y="0" width="40" height="40" rx="4"/>
<text class="vx-mono" x="152" y="25" font-size="14" text-anchor="middle">4</text>
<rect class="vx-box-accent" x="0" y="44" width="40" height="40" rx="4"/>
<text class="vx-mono" x="20" y="69" font-size="14" text-anchor="middle">5</text>
<rect class="vx-box-accent" x="44" y="44" width="40" height="40" rx="4"/>
<text class="vx-mono" x="64" y="69" font-size="14" text-anchor="middle">6</text>
<rect class="vx-box" x="88" y="44" width="40" height="40" rx="4"/>
<text class="vx-mono" x="108" y="69" font-size="14" text-anchor="middle">7</text>
<rect class="vx-box" x="132" y="44" width="40" height="40" rx="4"/>
<text class="vx-mono" x="152" y="69" font-size="14" text-anchor="middle">8</text>
<rect class="vx-box" x="0" y="88" width="40" height="40" rx="4"/>
<text class="vx-mono" x="20" y="113" font-size="14" text-anchor="middle">9</text>
<rect class="vx-box" x="44" y="88" width="40" height="40" rx="4"/>
<text class="vx-mono" x="64" y="113" font-size="14" text-anchor="middle">10</text>
<rect class="vx-box-accent" x="88" y="88" width="40" height="40" rx="4"/>
<text class="vx-mono" x="108" y="113" font-size="14" text-anchor="middle">11</text>
<rect class="vx-box-accent" x="132" y="88" width="40" height="40" rx="4"/>
<text class="vx-mono" x="152" y="113" font-size="14" text-anchor="middle">12</text>
<rect class="vx-box-accent" x="0" y="132" width="40" height="40" rx="4"/>
<text class="vx-mono" x="20" y="157" font-size="14" text-anchor="middle">13</text>
<rect class="vx-box-accent" x="44" y="132" width="40" height="40" rx="4"/>
<text class="vx-mono" x="64" y="157" font-size="14" text-anchor="middle">14</text>
<rect class="vx-box" x="88" y="132" width="40" height="40" rx="4"/>
<text class="vx-mono" x="108" y="157" font-size="14" text-anchor="middle">15</text>
<rect class="vx-box" x="132" y="132" width="40" height="40" rx="4"/>
<text class="vx-mono" x="152" y="157" font-size="14" text-anchor="middle">16</text>
</g>
<text class="vx-text-accent" x="16" y="230" font-size="15" font-weight="600">Column-major visiting order (stride 4 cells)</text>
<g transform="translate(16,242)">
<rect class="vx-box" x="0" y="0" width="40" height="40" rx="4"/>
<text class="vx-mono" x="20" y="25" font-size="14" text-anchor="middle">1</text>
<rect class="vx-box" x="44" y="0" width="40" height="40" rx="4"/>
<text class="vx-mono" x="64" y="25" font-size="14" text-anchor="middle">5</text>
<rect class="vx-box-accent" x="88" y="0" width="40" height="40" rx="4"/>
<text class="vx-mono" x="108" y="25" font-size="14" text-anchor="middle">9</text>
<rect class="vx-box-accent" x="132" y="0" width="40" height="40" rx="4"/>
<text class="vx-mono" x="152" y="25" font-size="14" text-anchor="middle">13</text>
<rect class="vx-box-accent" x="0" y="44" width="40" height="40" rx="4"/>
<text class="vx-mono" x="20" y="69" font-size="14" text-anchor="middle">2</text>
<rect class="vx-box-accent" x="44" y="44" width="40" height="40" rx="4"/>
<text class="vx-mono" x="64" y="69" font-size="14" text-anchor="middle">6</text>
<rect class="vx-box" x="88" y="44" width="40" height="40" rx="4"/>
<text class="vx-mono" x="108" y="69" font-size="14" text-anchor="middle">10</text>
<rect class="vx-box" x="132" y="44" width="40" height="40" rx="4"/>
<text class="vx-mono" x="152" y="69" font-size="14" text-anchor="middle">14</text>
<rect class="vx-box" x="0" y="88" width="40" height="40" rx="4"/>
<text class="vx-mono" x="20" y="113" font-size="14" text-anchor="middle">3</text>
<rect class="vx-box" x="44" y="88" width="40" height="40" rx="4"/>
<text class="vx-mono" x="64" y="113" font-size="14" text-anchor="middle">7</text>
<rect class="vx-box-accent" x="88" y="88" width="40" height="40" rx="4"/>
<text class="vx-mono" x="108" y="113" font-size="14" text-anchor="middle">11</text>
<rect class="vx-box-accent" x="132" y="88" width="40" height="40" rx="4"/>
<text class="vx-mono" x="152" y="113" font-size="14" text-anchor="middle">15</text>
<rect class="vx-box-accent" x="0" y="132" width="40" height="40" rx="4"/>
<text class="vx-mono" x="20" y="157" font-size="14" text-anchor="middle">4</text>
<rect class="vx-box-accent" x="44" y="132" width="40" height="40" rx="4"/>
<text class="vx-mono" x="64" y="157" font-size="14" text-anchor="middle">8</text>
<rect class="vx-box" x="88" y="132" width="40" height="40" rx="4"/>
<text class="vx-mono" x="108" y="157" font-size="14" text-anchor="middle">12</text>
<rect class="vx-box" x="132" y="132" width="40" height="40" rx="4"/>
<text class="vx-mono" x="152" y="157" font-size="14" text-anchor="middle">16</text>
</g>
<g transform="translate(600,36)">
<rect class="vx-box" x="0" y="0" width="18" height="18"/>
<text class="vx-text" x="24" y="14" font-size="12">one cache line</text>
<rect class="vx-box-accent" x="0" y="26" width="18" height="18"/>
<text class="vx-text" x="24" y="40" font-size="12">the next line</text>
<text class="vx-text-muted" x="0" y="70" font-size="12">numbers: visit order</text>
</g>
</svg>
<figcaption>Figure 1. Storage never changes: each row is split into two lines here, two cells wide, to stand in for the real 128-byte, 32-element line at a size that fits on the page. Row-major visiting order (top) keeps consecutive numbers inside one colored band before crossing to the next. Column-major visiting order (bottom), over the identical stored data, changes color on almost every consecutive number, which <code>traversal_order.cpp</code> counts exactly at the real line width.</figcaption>
</figure>

??? check "Both traversal orders in `traversal_order.cpp` visit every one of the 512 elements exactly once. Why does one of them touch so many more distinct lines in a row?"

    Touching every element once is about the *arithmetic*: same count either
    way. Line transitions are about *order*: row-major order keeps 32
    consecutive visits inside one line before moving on, so it reuses each
    fetched line 32 times. Column-major order's stride (one row's width) is
    larger than a line, so consecutive visits almost never share a line, and
    each one effectively pays for a fresh line.

## The hierarchy: several sizes, several speeds

A cache is not the only place a value can live, and there is not only one
cache. A typical machine, this one included, stacks several levels between
the registers and DRAM: a small **L1** cache closest to a core (128 KiB of
data on this M4 Pro's performance cores), a larger, often shared **L2**
(16 MiB, shared by four performance cores on this machine), and sometimes an
L3 beyond that (this machine reports none)[^local]. Each level is bigger and
slower than the one before it. A **working set** is the set of addresses a
piece of code touches repeatedly over some stretch of its execution. The
central fact of the hierarchy is simple to state and easy to forget while
writing loops: *as long as a working set fits in a level, repeated access to
it is fast; once it stops fitting, the code falls back to the next, slower
level, for every access that misses.*

`working_set_levels.cpp` turns the two sizes above into a classifier: given a
working-set size in KiB, which level does it fit? This is the same sweep a
real pointer-chasing latency benchmark would run (from 4 KiB up to 256 MiB,
doubling each step), except this program answers "which level" instead of
"how many nanoseconds", so its output needs no clock and is exactly repeatable.

--8<-- "includes/examples/optimize/p2-memory-hierarchy/working_set_levels.cpp.md"

If you ran an actual pointer-chasing benchmark, cyclically permuted so
prefetching cannot help it, and plotted nanoseconds per load against working
set size, you would see something like the next figure: flat stretches
("plateaus") while the working set fits a level, and a jump at each boundary
where it stops fitting. This is the general shape Drepper's measurements
show[^drepper]; the boundaries below are this machine's own, not assumed ones.

<figure class="vx-figure">
<svg viewBox="0 0 760 260" role="img" aria-label="A schematic latency staircase: flat while the working set fits a cache level, a step up at each level boundary" aria-describedby="p2-f2-desc">
<desc id="p2-f2-desc">A step chart. The x axis is working set size on a log scale, with the L1 and L2 boundaries of this machine marked. The y axis is relative access latency, unlabeled in absolute units. The curve is flat, low, inside L1, steps up and stays flat inside L2, then steps up again and stays flat beyond L2 (DRAM).</desc>
<line class="vx-line" x1="60" y1="220" x2="720" y2="220"/>
<line class="vx-line" x1="60" y1="220" x2="60" y2="20"/>
<text class="vx-text" x="390" y="248" font-size="13" text-anchor="middle">working set size (log scale)</text>
<text class="vx-text" x="24" y="120" font-size="13" text-anchor="middle" transform="rotate(-90 24 120)">latency</text>
<path class="vx-line" d="M 60 200 L 260 200 L 260 140 L 440 140 L 440 60 L 720 60" fill="none" stroke-width="2.5"/>
<line class="vx-line vx-text-muted" x1="260" y1="220" x2="260" y2="60" stroke-dasharray="4 4"/>
<line class="vx-line vx-text-muted" x1="440" y1="220" x2="440" y2="60" stroke-dasharray="4 4"/>
<circle class="vx-dot" cx="260" cy="200" r="4"/>
<circle class="vx-dot" cx="260" cy="140" r="4"/>
<circle class="vx-dot" cx="440" cy="140" r="4"/>
<circle class="vx-dot" cx="440" cy="60" r="4"/>
<text class="vx-mono vx-text-accent" x="260" y="236" font-size="12" text-anchor="middle">L1: 128 KiB</text>
<text class="vx-mono vx-text-accent" x="440" y="236" font-size="12" text-anchor="middle">L2: 16 MiB</text>
<text class="vx-text" x="150" y="192" font-size="12" text-anchor="middle">fits in L1</text>
<text class="vx-text" x="350" y="132" font-size="12" text-anchor="middle">fits in L2</text>
<text class="vx-text" x="590" y="52" font-size="12" text-anchor="middle">DRAM</text>
</svg>
<figcaption>Figure 2. A schematic, not measured data: real curves have noise, and prefetching can hide part of a jump. The plateau-then-step shape is the general, repeatedly observed pattern described in the Drepper source below; the two boundaries marked are this machine's own L1 and L2 sizes, read with <code>sysctl</code> on 2026-09-23, not assumed numbers.</figcaption>
</figure>

??? check "An array is 2 MiB. It does not fit in this machine's 128 KiB L1. Does every access to it miss the L1 cache?"

    No. It does not fit *all at once*, but any given 128-byte line of it, once
    fetched, still sits in L1 until something evicts it. Accessing nearby
    elements right after the line that holds them was fetched still hits.
    What the 2 MiB size rules out is keeping the *whole array* resident in L1
    at the same time, the way `working_set_levels.cpp` classifies it: as an
    L2-resident working set, not an L1-resident one.

## Sets and associativity

A cache cannot simply hold "whatever was used recently" in an unstructured
pile: checking an arbitrary pile for a match on every load would itself be
slow. Instead, a cache splits its space into **sets**, and a given address is
only allowed to live in one specific set, chosen by some of its address bits.
**Associativity** is how many lines each set can hold at once: a
**direct-mapped** cache has one slot per set (a new line always evicts
whatever was already there), while an **N-way set-associative** cache has N
slots, so N different addresses that land in the same set can coexist. A
**conflict miss** is a miss caused purely by two addresses landing in the same
set, not by the cache being full overall.

`associativity_conflict.cpp` builds two tiny toy caches, one direct-mapped and
one 2-way, and alternates between two addresses (called `100` and `200` here)
that are chosen to map to the same set:

--8<-- "includes/examples/optimize/p2-memory-hierarchy/associativity_conflict.cpp.md"

The direct-mapped cache never hits again after the first two (cold) misses:
every access to `100` evicts `200` from the only slot in that set, and every
access to `200` evicts `100` right back, forever. The 2-way cache holds both
at once after the first two misses, so every access after that is a hit. Both
caches have the same *number of sets*; associativity, not capacity, is the
only difference, and it is the only reason one of them keeps thrashing and the
other does not.

??? check "A direct-mapped cache and a 2-way cache in this example have the same set count. Why does only one of them thrash on two addresses that share a set?"

    A set with one slot can hold only one of the two addresses at a time, so
    switching between them is switching what occupies that single slot: a
    miss every time. A set with two slots holds both addresses at once, so
    after the first miss on each, neither has to be evicted for the other.

## The TLB: caching translations, not data

Everything so far assumed you already know the physical address a load needs.
In practice a program works in **virtual addresses**, and every one of them
has to be translated to a physical address before memory can be touched. That
translation is itself a lookup into page tables kept in memory, so a
processor caches recent translations in a small structure called a
**translation lookaside buffer (TLB)**, the same idea as a data cache, applied
to translations instead of values[^drepper]. A TLB has a fixed number of
entries, each covering one page (16 KiB on this machine, from `sysctl
hw.pagesize`[^local]), so its **reach** (the total bytes it can cover without
a miss) is entries times page size. Touch data spread across more distinct
pages than the TLB has entries for, in a pattern that keeps revisiting old
pages, and every access pays a translation miss on top of whatever the data
cache does. This becomes important again in P8: block sizes for cache
blocking are chosen against both a cache's capacity and the TLB's reach, not
capacity alone.

??? check "Why can a program have plenty of L1 hits and still run slowly because of the TLB?"

    A data cache hit only means the *value* at that address is nearby. It
    says nothing about whether the *translation* for that address is also
    cached. An access pattern that revisits the same handful of cache lines
    but scatters those lines across many different pages can hit the data
    cache on every access while still missing the TLB repeatedly.

## Two kinds of locality, and why matmul has only one for free

Two words summarize everything above, and it is worth having them precisely.
**Spatial locality** is reuse of *nearby* addresses: visiting `a[row, 0]` then
`a[row, 1]` benefits from spatial locality because they share a cache line.
**Temporal locality** is reuse of the *same* address across time: visiting
`a[row, k]` now and again later benefits from temporal locality only if the
line holding it has not been evicted in between.

The naive `multiply` above gets spatial locality for free on `a` (row-major
storage matches the inner loop's stride) but gets neither kind for `b`: its
column stride defeats spatial locality within a line, and because the inner
loop runs the full shared dimension before `row` advances, `b`'s lines get no
temporal reuse either before the loop moves on. This is exactly the gap the
next chapters close: P3 gives you a model (the roofline) for how much that
gap can cost, P6 to P8 give you legal ways to reorder and retile the loops so
that reuse actually happens before a line is evicted, and P4 gives you the
counters to confirm, on real hardware, that a rewrite helped for the reason
you think it did.

## For Vortex

!!! vortex "Exercise"

    **Build:** teach your compiler's driver to read this host's cache facts
    at startup, the same way this chapter did: cache line size, L1 data
    cache size, and page size, using `sysctl` (macOS) or `getconf` /
    `/sys/devices/system/cpu/cpu0/cache/` (Linux). Print them from an
    `--explain` (or similarly named) diagnostic flag, each one labelled with
    which command produced it, matching how this chapter cited its own
    numbers.

    **Not yet:** do not use these facts to change what code your compiler
    generates. No auto-tuning, no picking a tile size, no branching codegen
    on the detected line size. That is P8's and P15's job, once you have a
    legality story (P6, O9) and a way to measure whether a choice actually
    helped (P1, P4). For now this is purely diagnostic output.

    **Test:** run your driver's `--explain` on two different machines (or
    the same machine with `sysctl` piped in by hand for comparison), and
    check that the printed numbers match what `sysctl` or `getconf` report
    directly, byte for byte. The exercise is done when the numbers your
    compiler prints are never hard-coded: change the machine, and the output
    changes with it.

## Key ideas

!!! recap "You can now answer"

    - **What is a cache line?** The fixed-size, aligned chunk a cache moves in one transfer; the unit a fetch actually costs, not one value.
    - **What is a working set?** The set of addresses a piece of code touches repeatedly over some stretch of its run.
    - **What is the difference between spatial and temporal locality?** Spatial locality reuses nearby addresses (often the same line); temporal locality reuses the same address later in time.
    - **What is a conflict miss, and why does associativity prevent it?** A miss caused by two addresses sharing a cache set, not by the cache being full; more ways per set means more addresses can share a set without evicting each other.
    - **What does the TLB cache, and why can it miss even when the data cache hits?** Virtual-to-physical translations, one per page; a pattern that touches many pages can miss the TLB while still hitting the data cache on the values themselves.
    - **Why is `multiply`'s naive loop order memory-bound before any counter says so?** Its access to `b` has neither spatial locality (large column stride) nor temporal locality (each line of `b` is not revisited before the loop moves past it), while its access to `a` has spatial locality only.

## Where this comes back

!!! next "You will use this again in"

    - [P3. The roofline model](p3-roofline.md): *working set*, *DRAM traffic*
    - [P4. Seeing inside the CPU: counters and tools](p4-counters-and-tools.md): *cache line*, *L1 miss*
    - [P6. Dependence analysis](p6-dependence-analysis.md): *row-major layout*
    - [P7. Loop transformations](p7-loop-transformations.md): *spatial locality*, *temporal locality*
    - [P8. Cache blocking](p8-cache-blocking.md): *working set*, *TLB reach*, *conflict miss*
    - [G3. The GPU memory hierarchy](../gpu/g3-memory-hierarchy.md): *cache line*, *hierarchy level*

## Sources and further reading

Read Drepper first: it is long, but sections 3 and 6.2.1 alone cover
everything in this chapter with real measurements on real hardware, and nowhere
else are working sets, associativity and a matmul loop order tied together as
directly. Apple's tuning guide is the free, no-login companion for readers on
Apple Silicon specifically.

[^drepper]: Ulrich Drepper, "What Every Programmer Should Know About Memory", Red Hat, 2007, sections 3 ("CPU caches"), 3.1 ("Cache operation at high level"), 3.3 ("Cache associativity"), 4 ("Virtual memory", for the TLB) and 6.2.1 ("Cache use") for the transposed and sub-matrix matmul measurements. <https://www.akkadia.org/drepper/cpumemory.pdf> (serialized at <https://lwn.net/Articles/250967/>)
[^local]: Measured by the author on an Apple M4 Pro (macOS 27): `sysctl hw.cachelinesize`, `sysctl hw.perflevel0.l1dcachesize`, `sysctl hw.perflevel0.l2cachesize`, `sysctl hw.perflevel0.cpusperl2` and `sysctl hw.pagesize`, 2026-09-23. Values are this machine's own; query, never assume, on any other machine, including CI runners.
