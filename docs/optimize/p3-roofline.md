# P3. The roofline model

<p class="page-intro">A kernel's speed is capped by two things: how fast the machine can compute and how fast it can bring data in from memory. The roofline model puts both caps on one chart, so you can tell which one binds a kernel and what kind of change could raise it. For Vortex it gives the stage 10 matmul its first honest yardstick: a bound to measure it against, and an estimate the compiler can print without claiming it was measured.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 40 minutes · Builds on: [P2. The memory hierarchy](p2-memory-hierarchy.md)</p>

???+ remember "Before you start, remember"

    ??? question "Why does stage 10 leave the matmul kernel as a plain triple loop instead of optimizing it?"

        Three reasons build on each other: fast matrix multiplication is a large subject on its own; an optimization needs a correct, slow version to measure against and to check its answer against; and Vortex's principles forbid speed that silently changes results, so the naive loop is the baseline every faster version must reproduce bit for bit.

        Introduced in [Build v0.1, stage 10](../compiler/guide/stage-10-matrix-multiplication.md#why-speed-can-wait).

    ??? question "Why is the naive matmul loop's access to `b[k, column]` poor for the cache before any counter or timer says so?"

        It has neither spatial locality, since consecutive `k` jumps a whole row's width in memory rather than staying in one cache line, nor temporal locality, since each line of `b` is not revisited before the loop moves past it. `a[row, k]`, walked the same way, has spatial locality only.

        Introduced in [P2. The memory hierarchy](p2-memory-hierarchy.md#key-ideas).

    ??? question "How many scalar multiply-adds does one call to the stage 10 kernel perform, at its 64 &times; 64 &times; 64 size, and how do you know?"

        262,144: the inner `k` loop's trip count, 64, times the 64 &times; 64 values of `row` and `column`. The count follows from the loop bounds, which are all the constant 64.

        Introduced in [O8. Loops: structure, induction variables and bounds checks](o8-loops.md#the-checks-in-the-kernels-inner-loop).

    ??? question "What does marking Vortex's `&mut c` parameter noalias let the compiler skip, and what rule of the language backs it?"

        The runtime overlap check, and the second copy of the loop, that a vectorizer must otherwise keep in case `c` overlaps `a` or `b`. References 9.8 settles the question at compile time: storage behind a `&mut` parameter is reachable through no other parameter of the same call.

        Introduced in [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md#the-price-of-not-knowing-runtime-checks).

    ??? question "What is the difference between a passed remark, a missed remark and an analysis remark?"

        A passed remark reports a transformation the compiler made. A missed remark reports one it tried and could not make. An analysis remark reports what a pass worked out, often the reason behind a missed remark next to it.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#remarks-the-optimizers-report).

!!! goals "In this chapter"

    - Compute a kernel's operational intensity by hand, from its flop count and a model of the bytes it moves to and from DRAM.
    - State the roofline bound, find a machine's ridge point, and explain why the bound is a ceiling on performance and never a forecast of it.
    - Explain why tiling raises operational intensity without adding a flop, and estimate how large a tile must be to cross a given ridge point.
    - Read ceilings below the roof as missing optimizations, and name the ones Vortex's strict floating-point rules put in play for the stage 10 kernel.
    - Measure the two numbers a roofline needs on your own machine, and say what a compiler may and may not print about them.

## A question the flop count alone cannot answer

The stage 10 kernel performs 262,144 multiply-adds per call, a count [O8](o8-loops.md#the-checks-in-the-kernels-inner-loop) read off the loop bounds. This chapter counts each multiply-add as two **floating-point operations**, or **flops**: one multiply and one add. Whichever convention you choose, use the same one for the kernel and for the machine it runs on. One call therefore does 524,288 flops.

That number does not say how long the call takes, because the call does two kinds of work. It computes, and it moves `a`, `b` and `c` between memory and the core's registers. Williams, Waterman and Patterson, who introduced the roofline model, built it on the expectation that off-chip memory bandwidth would often be the resource that limits a program.[^roofline-om] Two kernels can do exactly the same flops and run at quite different speeds because they move different numbers of bytes.

The plain triple loop from stage 10 is the running example. For each `row` and `column` it walks `a` along a row and `b` down a column, adding products into `sum`. A tiled version computes the same sums in a different order, so that a block of `b` stays in a fast cache while many rows of `c` use it. It performs the identical 524,288 flops and moves fewer bytes. The **roofline model** turns "moves fewer bytes" into a number, and says, before you write a single tile, how much that number could buy.

## Operational intensity: flops per byte of DRAM traffic

**Operational intensity** is the number of flops a kernel performs for each byte of traffic between the caches and main memory (DRAM).[^roofline-om] The choice of which bytes to count is the heart of the model. A byte the program reads from L1 ten times costs one trip from DRAM, not ten, so it counts once. The authors chose the word "operational" over the older "arithmetic intensity" for two reasons: arithmetic intensity counts traffic between the processor and the cache, which would hide the effect of a cache optimization, and they wanted a term that also fits kernels whose operations are not arithmetic.[^roofline-om]

Figure 1 shows where the bytes are counted, and what three schedules of the 64 &times; 64 &times; 64 kernel send across that line.

<figure class="vx-figure">
<svg viewBox="0 0 720 270" role="img" aria-label="Where operational intensity counts bytes, and how many bytes three schedules of the 64 by 64 by 64 f32 matmul send across that boundary" aria-describedby="p3-f1-desc">
<title id="p3-f1-title">Bytes counted at the DRAM boundary</title>
<desc id="p3-f1-desc">Left, a memory hierarchy drawn as four stacked boxes: registers and core, L1 cache, L2 cache, and DRAM at the bottom. A dashed line between L2 and DRAM is labelled counted here, and an animated arrow carries data up from DRAM across it. Right, three horizontal bars, one per schedule, each split into segments for the bytes of A, B and C that cross the line. The naive bar is long, almost all of it B: 1,081,344 bytes, 0.485 flops per byte. The bar for tiling with t equal to 16 is short: 98,304 bytes, 5.333 flops per byte. The compulsory bar is shortest: 49,152 bytes, 10.667 flops per byte, one pass over each matrix. A and C segments are the same small width in every bar; only B changes.</desc>
<defs><marker id="p3-f1-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="20" y="20" width="170" height="34" rx="4"/>
<text class="vx-text" x="105" y="42" text-anchor="middle">registers and core</text>
<rect class="vx-box" x="20" y="66" width="170" height="34" rx="4"/>
<text class="vx-text" x="105" y="88" text-anchor="middle">L1 cache</text>
<rect class="vx-box" x="20" y="112" width="170" height="34" rx="4"/>
<text class="vx-text" x="105" y="134" text-anchor="middle">L2 cache</text>
<line class="vx-box-bad" x1="10" y1="166" x2="200" y2="166"/>
<text class="vx-text-accent" x="105" y="160" text-anchor="middle">counted here</text>
<rect class="vx-box-strong" x="20" y="186" width="170" height="34" rx="4"/>
<text class="vx-text" x="105" y="208" text-anchor="middle">DRAM</text>
<path class="vx-flow" d="M178 185 L178 148" marker-end="url(#p3-f1-head)"/>
<text class="vx-text-muted" x="20" y="244">reuse inside the caches</text>
<text class="vx-text-muted" x="20" y="260">costs no counted bytes</text>
<text class="vx-text" x="230" y="24">Bytes across the line, n = 64, f32</text>
<text class="vx-text-muted" x="230" y="52">naive: 1,081,344 bytes, 0.485 flops/byte</text>
<rect class="vx-box" x="230" y="58" width="7" height="22" rx="1"/>
<rect class="vx-box-accent" x="237" y="58" width="446" height="22" rx="1"/>
<rect class="vx-box" x="683" y="58" width="7" height="22" rx="1"/>
<text class="vx-text-muted" x="230" y="108">tiled, t = 16: 98,304 bytes, 5.333 flops/byte</text>
<rect class="vx-box" x="230" y="114" width="7" height="22" rx="1"/>
<rect class="vx-box-accent" x="237" y="114" width="28" height="22" rx="1"/>
<rect class="vx-box" x="265" y="114" width="7" height="22" rx="1"/>
<text class="vx-text-muted" x="230" y="164">compulsory: 49,152 bytes, 10.667 flops/byte</text>
<rect class="vx-box" x="230" y="170" width="7" height="22" rx="1"/>
<rect class="vx-box-accent" x="237" y="170" width="7" height="22" rx="1"/>
<rect class="vx-box" x="244" y="170" width="7" height="22" rx="1"/>
<rect class="vx-box-accent" x="230" y="214" width="22" height="14" rx="2"/>
<text class="vx-text-muted" x="258" y="225">bytes of B</text>
<rect class="vx-box" x="350" y="214" width="22" height="14" rx="2"/>
<text class="vx-text-muted" x="378" y="225">bytes of A (left) and C (right)</text>
<text class="vx-text-muted" x="230" y="256">All three do the same 524,288 flops. Only the traffic of B changes.</text>
</svg>
<figcaption>Figure 1. Operational intensity counts only the bytes that cross between the last cache and DRAM. The bars are drawn to scale from <code>intensity_model.cpp</code>'s estimates for the 64 &times; 64 &times; 64 kernel. The naive schedule, on a cache too small to keep <code>b</code> between rows, fetches all of <code>b</code> once per row of <code>c</code>; tiling fetches it four times; the compulsory floor fetches it once.</figcaption>
</figure>

### Counting by hand

Take the naive loop first, and assume the worst about the cache: nothing of `b` survives from one row of `c` to the next. Each multiply-add then brings one new 4-byte element of `b` from DRAM, and does 2 flops with it. That is 2 flops per 4 bytes, an intensity of 0.5, before counting anything else.

`a` and `c` add a little. Each is 64 &times; 64 &times; 4 = 16,384 bytes and crosses once, since a row of `a` stays in cache while its row of `c` is computed. So the naive traffic is 64 passes over `b` (64 &times; 16,384 = 1,048,576 bytes) plus 32,768 bytes for `a` and `c`: 1,081,344 bytes in all. Dividing, 524,288 / 1,081,344 = 0.485 flops per byte.

Now suppose a schedule keeps a block of `b` in cache while **t** rows of `c` use it, the idea behind **tiling**, which [P8](p8-cache-blocking.md) develops. Each element of `b` fetched from DRAM now serves t multiply-adds instead of one, so `b` crosses 64 / t times. At t = 16 that is 4 passes, 65,536 bytes, plus the same 32,768: 98,304 bytes and 5.333 flops per byte. The rule of thumb for f32, ignoring `a` and `c`, is an intensity of about t / 2: 2t flops for every 4 bytes of `b`.

No schedule can move less than one pass over each matrix, 3 &times; 16,384 = 49,152 bytes. That floor is the **compulsory traffic**: the bytes that must cross because each value has to arrive at least once. The paper ties the model to the "three Cs" classification of cache misses in exactly this way: compulsory misses set the least traffic and so the highest intensity a kernel can reach, while capacity and conflict misses lower it.[^roofline-3cs] Here the ceiling is 524,288 / 49,152 = 10.667 flops per byte.

`intensity_model.cpp` does this arithmetic for the stage 10 size and for a much larger one:

--8<-- "includes/examples/optimize/p3-roofline/intensity_model.cpp.md"

The two tables teach different things. At n = 2048 the naive row is 0.500, the "one element of `b` per multiply-add" figure, because the traffic of `a` and `c` has become negligible. The tiled rows follow t / 2 closely: 1.992, 7.877, 15.515. And the compulsory row, 341.333, shows how much reuse a large matmul holds in principle: the flops grow as n&sup3; while the data grows as n&sup2;.

### Which cache, which bytes

The model rests on an assumption about the cache, and at the stage 10 size the assumption fails on the machine this book was written on. The three 64 &times; 64 f32 matrices take 48 KiB together. [P2](p2-memory-hierarchy.md) read this M4 Pro's L1 data cache size on its performance cores as 128 KiB, with `sysctl`, on 2026-09-23. The whole problem fits in L1, so on that machine the naive loop's DRAM traffic is the compulsory 49,152 bytes on its first call, whatever its loop order.

That does not make the naive row wrong. It makes it a statement about a machine and a problem size, which is what operational intensity always is. At n = 2048 the matrices take 48 MiB, three times the 16 MiB L2 that P2 measured, and there the naive row's assumption is the realistic one. The paper makes the same point: kernels such as dense matrix work and FFT have an intensity that grows with problem size.[^roofline-3cs] The section [Which roof](#which-roof) returns to what bounds the small kernel instead.

??? check "The kernel stores `c` and never reads it. On a cache that allocates a line on a write miss, reading the old line in before overwriting it and later writing the whole line back, what do the compulsory traffic and intensity of the 64 &times; 64 &times; 64 kernel become?"

    `c` now crosses twice: once in, when each line is allocated, and once out, when it is written back. The compulsory traffic is 4 &times; 16,384 = 65,536 bytes, and the intensity 524,288 / 65,536 = 8.0 flops per byte. The paper counts its stencil kernel's compulsory traffic the same way, "on write-allocate architectures".[^roofline-3cs] STREAM, by contrast, counts only the bytes the program asked for, so its reported bandwidth leaves this extra read out.[^stream]

## The roofline: two lines and a ridge

A machine has a **peak floating-point performance**, the most flops per second its arithmetic units can complete, found from its specification or a microbenchmark. It also has a **peak memory bandwidth**, the most bytes per second its memory system can sustain behind the caches. The authors measured the second with their own benchmark rather than taking the DRAM chips' pin rate, which a program never sees.[^roofline-om] Given both, the **roofline bound** on any kernel is:

$$\text{Attainable GFlop/s} = \min(\text{Peak GFlop/s},\ \text{Peak GB/s} \times \text{Operational intensity})$$

On a log-log chart of performance against intensity, the second term is a diagonal line, and the first is a flat line at the peak. The two meet at the **ridge point**, the smallest intensity at which a kernel can reach the machine's peak.[^roofline-om] A kernel left of the ridge is **memory-bound**: its bound is set by bandwidth, and doubling its intensity doubles the bound. A kernel right of the ridge is **compute-bound**: moving further right buys nothing. The lines depend only on the machine, so one roofline serves every kernel run on it.[^roofline-om]

### A worked roofline

The paper's first example is a dual-socket system with two 2.2 GHz AMD Opteron X2 (model 2214) chips. Its peak double-precision performance is 17.6 GFlop/s and its peak memory bandwidth, from the authors' benchmark, is 15 GB/s.[^roofline-om] The ridge point is 17.6 / 15 = 1.173 flops per byte. The paper quotes the X2's ridge as 1.0 when it compares machines; this chapter uses the computed value.[^roofline-om]

That peak is for doubles, so `roofline_bound.cpp` places an f64 version of the n = 2048 matmul under it. With 8-byte elements the rule of thumb becomes t / 4 flops per byte, and the naive loop sits at 0.25:

--8<-- "includes/examples/optimize/p3-roofline/roofline_bound.cpp.md"

Check the naive row by hand: 15 GB/s &times; 0.250 flops per byte = 3.75 GFlop/s, about a fifth of the peak. Each doubling of t doubles the intensity and, while the kernel is left of the ridge, doubles the bound. Between t = 4 and t = 8 the kernel crosses the ridge, and from then on its bound is the flat 17.6. Figure 2 plots the six rows.

<figure class="vx-figure">
<svg viewBox="0 0 700 430" role="img" aria-label="Roofline for the Opteron X2 with six tile sizes of an f64 matmul placed under it" aria-describedby="p3-f2-desc">
<title id="p3-f2-title">A roofline with six schedules</title>
<desc id="p3-f2-desc">A log-log chart. The horizontal axis is operational intensity in flops per byte, from one eighth to 16, doubling at each tick. The vertical axis is attainable performance in GFlop/s, from 1 to 32. A diagonal line for 15 GB/s rises from the lower left and meets a flat line at 17.6 GFlop/s at the ridge point, 1.17 flops per byte. Six points light up one after another. Tile size 1 sits on the diagonal at intensity 0.25 and 3.7 GFlop/s. Tile size 2 is at 0.5 and 7.5. Tile size 4 is at 1.0 and 14.9, a little left of the ridge. Tile sizes 8, 16 and 32 sit on the flat line at 2, 3.9 and 7.8 flops per byte. Left of the ridge is labelled memory-bound, right of it compute-bound.</desc>
<line class="vx-line" x1="90" y1="30" x2="90" y2="370"/>
<line class="vx-line" x1="90" y1="370" x2="660" y2="370"/>
<text class="vx-text-muted" x="90" y="390" text-anchor="middle">1/8</text>
<text class="vx-text-muted" x="170" y="390" text-anchor="middle">1/4</text>
<text class="vx-text-muted" x="250" y="390" text-anchor="middle">1/2</text>
<text class="vx-text-muted" x="330" y="390" text-anchor="middle">1</text>
<text class="vx-text-muted" x="410" y="390" text-anchor="middle">2</text>
<text class="vx-text-muted" x="490" y="390" text-anchor="middle">4</text>
<text class="vx-text-muted" x="570" y="390" text-anchor="middle">8</text>
<text class="vx-text-muted" x="650" y="390" text-anchor="middle">16</text>
<text class="vx-text-muted" x="375" y="416" text-anchor="middle">operational intensity (flops per DRAM byte, log scale)</text>
<text class="vx-text-muted" x="82" y="364" text-anchor="end">1</text>
<text class="vx-text-muted" x="82" y="300" text-anchor="end">2</text>
<text class="vx-text-muted" x="82" y="236" text-anchor="end">4</text>
<text class="vx-text-muted" x="82" y="172" text-anchor="end">8</text>
<text class="vx-text-muted" x="82" y="108" text-anchor="end">16</text>
<text class="vx-text-muted" x="82" y="44" text-anchor="end">32</text>
<text class="vx-text-muted" x="24" y="200" text-anchor="middle" transform="rotate(-90 24 200)">attainable GFlop/s (log scale)</text>
<line class="vx-box-accent" x1="90" y1="302" x2="348" y2="95"/>
<line class="vx-box-accent" x1="348" y1="95" x2="650" y2="95"/>
<line class="vx-line" x1="348" y1="95" x2="348" y2="370" stroke-dasharray="4 4"/>
<text class="vx-mono" x="348" y="62" text-anchor="middle">ridge 1.17</text>
<text class="vx-text-muted" x="560" y="86" text-anchor="middle">peak 17.6 GFlop/s</text>
<text class="vx-text-muted" x="200" y="200" text-anchor="middle" transform="rotate(-38.66 200 200)">15 GB/s &#215; intensity</text>
<text class="vx-text-muted" x="220" y="345" text-anchor="middle">memory-bound</text>
<text class="vx-text-muted" x="500" y="250" text-anchor="middle">compute-bound</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 6"><circle class="vx-dot" cx="170" cy="238" r="5"/><text class="vx-mono" x="180" y="256">t = 1</text></g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 6"><circle class="vx-dot" cx="250" cy="174" r="5"/><text class="vx-mono" x="260" y="192">t = 2</text></g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 6"><circle class="vx-dot" cx="330" cy="110" r="5"/><text class="vx-mono" x="318" y="104" text-anchor="end">t = 4</text></g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 6"><circle class="vx-dot" cx="409" cy="95" r="5"/><text class="vx-mono" x="409" y="122" text-anchor="middle">t = 8</text></g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 6"><circle class="vx-dot" cx="488" cy="95" r="5"/><text class="vx-mono" x="488" y="122" text-anchor="middle">t = 16</text></g>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 6"><circle class="vx-dot" cx="566" cy="95" r="5"/><text class="vx-mono" x="566" y="122" text-anchor="middle">t = 32</text></g>
</svg>
<figcaption>Figure 2. The roofline of the paper's Opteron X2 system (17.6 GFlop/s, 15 GB/s),[^roofline-om] with the f64, n = 2048 schedules from <code>roofline_bound.cpp</code>. Each doubling of the tile moves the kernel one step right; the bound rises with it until the ridge, then stays flat. The points are bounds, not measurements: a real run lies somewhere on the vertical line below its point.</figcaption>
</figure>

The bound is an upper limit, and the paper says so directly: a kernel's performance must lie somewhere on the vertical line through its intensity, at or below the roof.[^roofline-om] Nothing in the chart promises that a tiled loop reaches 17.6 GFlop/s. It says that a naive loop at 0.25 flops per byte cannot beat 3.75 on this machine, however well its arithmetic is scheduled, and that tiling is the change that lifts that cap.

??? check "A machine's ridge point is 6 flops per byte for f32 kernels. Using the rule of thumb for the tiled matmul, roughly what tile factor t does a large f32 matmul need before it can be compute-bound there, and what else must be true for that tile to deliver it?"

    Intensity is about t / 2 for f32, so t must be at least about 12, in practice 16 if tiles are powers of two. The tile must also fit: the block of `b` and the t rows it serves have to stay in cache for the whole reuse, or capacity misses bring back the traffic the model assumed away. Crossing the ridge only removes the bandwidth cap; reaching the flat roof still needs the ceilings of the next section.

### The ridge describes the machine

The ridge point says something about a machine without any kernel in sight. If it is far to the right, only kernels with a great deal of reuse can reach the peak; if it is far to the left, almost any kernel can.[^roofline-om] The paper's example compares the X2 with its successor, the Opteron X4. The two share a socket and so the same DRAM channels. The X4 has twice the cores, and each core can issue two floating-point SSE2 instructions per clock where the X2 issues two every other clock, so the X4 has slightly more than four times the peak with the same bandwidth. Its ridge moves from 1.0 to 4.4.[^roofline-om]

A higher ridge is not free. A kernel below 1 flop per byte gains nothing from the X4, because both machines give it the same bandwidth-bound limit; the paper says as much.[^roofline-om] A kernel at 2 flops per byte, compute-bound on the X2, gains, but it is memory-bound on the X4 and cannot use the new peak until its intensity passes 4.4. The authors concluded that the ridge point predicted performance better than clock rate or peak, and the machine with the lowest ridge in their study was the easiest on which to reach its highest performance.[^roofline-conc]

??? check "Why does tiling change a kernel's operational intensity when it performs the same flops and touches the same array elements as the naive loop?"

    Operational intensity counts the bytes that cross between the last cache and DRAM, not the elements the source code names.[^roofline-om] Tiling does not change which elements the kernel touches; it changes how many times each one must be fetched again after being evicted. A block of `b` that serves t rows of `c` before it leaves the cache answers t times as many multiply-adds per DRAM byte, so the byte count falls while the flop count stays the same.

### One roof per configuration

The Opteron's 17.6 GFlop/s is the peak of all four cores of the two-socket system, and its 15 GB/s comes from benchmarks that use every technique the authors had for getting bandwidth, prefetching included.[^roofline-om] A single thread sees a lower roof on both counts. Keeping enough memory operations in flight to reach full bandwidth takes concurrency, which the paper notes is easier to supply with many cores than with one.[^roofline-fall] The stage 10 kernel runs on one thread, so its roofline must be built from one core's peak and the bandwidth one thread can draw. A roof from a different configuration is not a bound on it.

## Ceilings below the roof

A kernel that clears the ridge can still run far below the roof. The paper adds **ceilings**: lower lines under the roof, each standing for one optimization the kernel has not received. You cannot rise through a ceiling without performing its optimization, and the ceilings are stacked so that, to pass one, you must already have passed all those below it.[^roofline-ceil] For the Opteron X2 the authors name five:[^roofline-ceil]

1. **Improve instruction-level parallelism and use SIMD.** Keep enough independent instructions in flight to cover each unit's latency, for example by unrolling, and use SIMD instructions, which work on several operands at once.
2. **Balance the floating-point mix.** Peak usually needs as many additions as multiplications, because many machines have multiply-add instructions or equal numbers of adders and multipliers.
3. **Restructure loops for unit-stride access,** which engages the hardware prefetcher.
4. **Ensure memory affinity,** so each thread's data sits in the DRAM attached to its own chip.
5. **Use software prefetching** where it delivers more bandwidth than the hardware prefetcher alone.

The first two lower the flat roof; the last three lower the diagonal. The paper measures their heights on the X2, and Figure 3 draws them.

<figure class="vx-figure">
<svg viewBox="0 0 700 430" role="img" aria-label="The Opteron X2 roofline with the three compute ceilings and four bandwidth lines the paper measured" aria-describedby="p3-f3-desc">
<title id="p3-f3-title">Ceilings on the Opteron X2</title>
<desc id="p3-f3-desc">The same log-log axes as Figure 2. The flat roof at 17.6 GFlop/s has two flat ceilings below it: 8.8 GFlop/s, labelled floating-point mix imbalanced, and 2.2 GFlop/s, labelled no ILP or SIMD either. Four parallel diagonals rise from the lower left and stop at the roof: 15 GB/s with all memory optimizations, 11 GB/s without software prefetching, 4.8 GB/s without memory affinity as well, and 2.7 GB/s with only unit-stride optimization. The lower a diagonal, the further right it meets the roof.</desc>
<line class="vx-line" x1="90" y1="30" x2="90" y2="370"/>
<line class="vx-line" x1="90" y1="370" x2="660" y2="370"/>
<text class="vx-text-muted" x="90" y="390" text-anchor="middle">1/8</text>
<text class="vx-text-muted" x="170" y="390" text-anchor="middle">1/4</text>
<text class="vx-text-muted" x="250" y="390" text-anchor="middle">1/2</text>
<text class="vx-text-muted" x="330" y="390" text-anchor="middle">1</text>
<text class="vx-text-muted" x="410" y="390" text-anchor="middle">2</text>
<text class="vx-text-muted" x="490" y="390" text-anchor="middle">4</text>
<text class="vx-text-muted" x="570" y="390" text-anchor="middle">8</text>
<text class="vx-text-muted" x="650" y="390" text-anchor="middle">16</text>
<text class="vx-text-muted" x="375" y="416" text-anchor="middle">operational intensity (flops per DRAM byte, log scale)</text>
<text class="vx-text-muted" x="82" y="364" text-anchor="end">1</text>
<text class="vx-text-muted" x="82" y="300" text-anchor="end">2</text>
<text class="vx-text-muted" x="82" y="236" text-anchor="end">4</text>
<text class="vx-text-muted" x="82" y="172" text-anchor="end">8</text>
<text class="vx-text-muted" x="82" y="108" text-anchor="end">16</text>
<text class="vx-text-muted" x="82" y="44" text-anchor="end">32</text>
<text class="vx-text-muted" x="24" y="200" text-anchor="middle" transform="rotate(-90 24 200)">attainable GFlop/s (log scale)</text>
<line class="vx-box-accent" x1="90" y1="302" x2="348" y2="95"/>
<line class="vx-box-accent" x1="348" y1="95" x2="650" y2="95"/>
<line class="vx-box-bad" x1="268" y1="159" x2="650" y2="159"/>
<line class="vx-box-bad" x1="108" y1="287" x2="650" y2="287"/>
<line class="vx-line" x1="90" y1="331" x2="384" y2="95" stroke-dasharray="5 4"/>
<line class="vx-line" x1="149" y1="370" x2="480" y2="95" stroke-dasharray="5 4"/>
<line class="vx-line" x1="215" y1="370" x2="546" y2="95" stroke-dasharray="5 4"/>
<text class="vx-text-muted" x="650" y="87" text-anchor="end">17.6: peak</text>
<text class="vx-text-muted" x="650" y="151" text-anchor="end">8.8: floating-point mix imbalanced</text>
<text class="vx-text-muted" x="650" y="279" text-anchor="end">2.2: no ILP or SIMD either</text>
<text class="vx-mono" x="167" y="254">15</text>
<text class="vx-mono" x="204" y="254">11</text>
<text class="vx-mono" x="300" y="254">4.8</text>
<text class="vx-mono" x="366" y="254">2.7</text>
<text class="vx-text" x="400" y="302">Diagonals, in GB/s</text>
<text class="vx-text-muted" x="400" y="317">15: every memory optimization</text>
<text class="vx-text-muted" x="400" y="331">11: no software prefetching</text>
<text class="vx-text-muted" x="400" y="345">4.8: no memory affinity either</text>
<text class="vx-text-muted" x="400" y="359">2.7: unit stride only</text>
</svg>
<figcaption>Figure 3. Ceilings on the Opteron X2, redrawn from the numbers in the paper's section 4: compute ceilings at 8.8 GFlop/s without a balanced multiply-add mix and 2.2 GFlop/s without ILP or SIMD as well; bandwidth lines at 11 GB/s without software prefetching, 4.8 GB/s without memory affinity as well, and 2.7 GB/s with only unit-stride optimization.[^roofline-ceil] A kernel's intensity picks which ceilings lie above it.</figcaption>
</figure>

Reading the chart is a matter of looking straight up from a kernel's intensity. At 1/8 flop per byte every diagonal lies below every compute ceiling, so only memory optimizations can help. At 4 flops per byte every diagonal lies above both compute ceilings, so compute optimizations come first.

In between, around 1/2, the lines interleave and both kinds are worth trying. The gap between one ceiling and the next one up is the most that optimization can gain, and the order of the ceilings suggests the order to try them.[^roofline-ceil] The paper ranks those a compiler is most likely to achieve at the bottom, with one exception: floating-point balance depends on the kernel, and for one where multiplies and adds pair up naturally, it moves to the bottom.[^roofline-ceil]

### The ceilings the stage 10 kernel meets

Two ceilings meet Vortex's numerical rules head on. The first is the floating-point mix. A **fused multiply-add** computes `a * b + c` as one instruction with a single rounding. On a machine whose peak counts each fused multiply-add as two flops, and whose separate adds and multiplies use the same units, a program that never fuses spends two instructions where the peak assumes one, and can reach at most half of it.

In the paper's results for the IBM Cell, the ceiling labelled "Without FMA" sits at 14.6 GFlop/s under a double-precision peak of 29.3.[^roofline-t4] Vortex forbids fusing: [decision 56](../decisions/numbers.md#d56) requires each `f32` and `f64` operation to round on its own, so that golden outputs match on every conforming compiler, and allows relaxed modes later only as an explicit opt-in. A strict Vortex build therefore leaves this ceiling out of reach by design.

The second is instruction-level parallelism, and here the stage 10 loop is a sharp case. Its inner loop adds each product into one `sum`, and each addition needs the previous one's result. That is a single dependency chain: it runs at one addition per addition latency, however many adders the core has ([P5](p5-microarchitecture.md#latency-throughput-and-the-number-of-chains)). The paper names the remedy, covering latency with independent work such as unrolled loops.[^roofline-ceil] Decision 56 forbids the usual trick for a sum, splitting it into several partial sums, because that reorders the additions.

It does not forbid computing several elements of `c` at once. Each keeps its own sum in its own order, so the printed digits stay the same, and the core gets several independent chains. That is [unroll-and-jam](p7-loop-transformations.md#unroll-and-jam-and-register-blocks), and after [interchange](p7-loop-transformations.md#interchange) the same independence lets SIMD lanes each carry one element of `c`. Because `&mut c` is noalias, that vector loop needs no runtime overlap check. Both climb the ILP and SIMD ceiling without changing a bit of the answer.

??? check "Vortex's compiler must not fuse a multiply and an add, and must not reassociate a sum. Which of the Opteron's five ceilings do those two rules touch, which of them stays out of reach for a strict build of the stage 10 kernel, and which can still be climbed?"

    They touch the two compute ceilings. The floating-point mix ceiling stays out of reach, since on a machine whose peak counts fused multiply-adds it can only be reached by fusing. The ILP and SIMD ceiling can still be climbed, not by splitting `sum` into partial sums, which reorders additions, but by giving the core several elements of `c` to work on at once, each summed in its original order.

## Which roof

The paper's model uses DRAM bandwidth because the kernels it studies do not fit in the caches. It also answers the obvious objection: when the working set fits in L2, the diagonal can be the L2 bandwidth, intensity is counted in flops per L2 byte, the diagonal moves up and the ridge moves left.[^roofline-fall] A machine therefore has one diagonal for each level of its memory, and a kernel meets the one for the level its data comes from.

The stage 10 kernel at n = 64 is a kernel that fits in L1 on the author's machine. Its DRAM traffic is compulsory and its DRAM roof is far above it. What binds it is below every memory line: the single chain of additions in `sum`. Figure 4 sketches the situation.

<figure class="vx-figure">
<svg viewBox="0 0 700 400" role="img" aria-label="A schematic roofline with one diagonal for each memory level and a low ceiling for a single chain of additions" aria-describedby="p3-f4-desc">
<title id="p3-f4-title">One diagonal per memory level</title>
<desc id="p3-f4-desc">A schematic log-log chart with no numbers. A flat roof marks peak compute. Three parallel diagonals rise to meet it: the L1 bandwidth line furthest left and highest, then L2, then DRAM furthest right. Each meets the roof at its own ridge point, and the DRAM ridge is furthest right. A dashed horizontal line well below the roof is labelled one chain of additions, each waiting for the last. A pulsing dot on that dashed line, where every bandwidth line is above it, marks the stage 10 kernel at n equals 64, whose data sits in L1. Its position is illustrative.</desc>
<line class="vx-line" x1="90" y1="30" x2="90" y2="350"/>
<line class="vx-line" x1="90" y1="350" x2="680" y2="350"/>
<text class="vx-text-muted" x="385" y="376" text-anchor="middle">operational intensity (log scale)</text>
<text class="vx-text-muted" x="30" y="190" text-anchor="middle" transform="rotate(-90 30 190)">performance (log scale)</text>
<line class="vx-box-accent" x1="280" y1="80" x2="680" y2="80"/>
<text class="vx-text-muted" x="680" y="70" text-anchor="end">peak compute</text>
<line class="vx-line" x1="90" y1="232" x2="280" y2="80"/>
<line class="vx-line" x1="90" y1="328" x2="400" y2="80"/>
<line class="vx-line" x1="195" y1="340" x2="520" y2="80"/>
<circle class="vx-dot" cx="280" cy="80" r="4"/>
<circle class="vx-dot" cx="400" cy="80" r="4"/>
<circle class="vx-dot" cx="520" cy="80" r="4"/>
<text class="vx-text-muted" x="180" y="150" text-anchor="middle" transform="rotate(-38.66 180 150)">L1 bandwidth</text>
<text class="vx-text-muted" x="240" y="198" text-anchor="middle" transform="rotate(-38.66 240 198)">L2 bandwidth</text>
<text class="vx-text-muted" x="352" y="204" text-anchor="middle" transform="rotate(-38.66 352 204)">DRAM bandwidth</text>
<line class="vx-box-bad" x1="90" y1="270" x2="680" y2="270"/>
<text class="vx-text-muted" x="680" y="262" text-anchor="end">one chain of additions, each waiting for the last</text>
<circle class="vx-dot vx-pulse" cx="330" cy="270" r="6"/>
<text class="vx-text" x="340" y="296">stage 10 kernel, n = 64</text>
<text class="vx-text-muted" x="340" y="314">data in L1; position illustrative</text>
</svg>
<figcaption>Figure 4. A schematic, not measured data. Each memory level has its own diagonal, and the faster the level, the further left its ridge. The small stage 10 kernel draws its data from L1, so the DRAM line does not bind it; its single dependent chain of additions holds it below every memory line.</figcaption>
</figure>

The practical rule: before placing a kernel on a roofline, find where its working set lives, and use the diagonal for that level. A roofline for the wrong level gives a bound that is true and useless. For the matmul ladder in this book, the small kernel is a lesson in ILP ceilings, and a large one, whose working set outgrows the caches, is the lesson in intensity that the rest of this chapter describes.

??? check "The paper found that, after autotuning, three of its four kernels came close to their compulsory traffic, and that doubling the cache would not raise their intensity. Why not, and which kind of kernel does a bigger cache help?"

    A bigger cache removes capacity misses and some conflict misses, but a kernel already near its compulsory traffic has almost none left to remove; its intensity is at the ceiling compulsory misses set. A bigger cache helps a kernel whose working set is slightly too large for the cache it has, such as the paper's 128&sup3; FFT, where a larger cache holds a whole plane and cuts the traffic.[^roofline-fall]

## Measuring your own machine

Every number in this chapter's worked roofline belongs to a 2008 dual-socket Opteron. It is there because it is the one peak and bandwidth pair the chapter has from a published, dated source, and it says nothing about any machine Vortex targets. Using it for your own would break a rule the philosophy gives the compiler, and that applies as much to a person: [never present an unverified estimate as a measured result](../philosophy.md#programmer-and-compiler-responsibilities).

A roofline for your machine needs two measurements, taken under [P1](p1-measure-first.md)'s protocol:

- **Peak bandwidth.** The STREAM benchmark's **triad** computes `a(i) = b(i) + q*c(i)` and counts 24 bytes per element. Its rules ask for arrays of at least four times the total last-level cache, so every pass streams from DRAM.[^stream]
- **Peak flops.** Many independent chains of fused multiply-adds, enough to cover the instruction's latency; one chain would measure latency instead ([P5](p5-microarchitecture.md#latency-throughput-and-the-number-of-chains)).

`machine_probe.cpp` is a starting point for both. The harness compiles it but never runs it, because its output is timings:

--8<-- "includes/examples/optimize/p3-roofline/machine_probe.cpp.md"

Three cautions come with it. It is single-threaded, so it measures one core's roof, the right one for the stage 10 kernel and the wrong one for a multithreaded kernel ([P13](p13-multithreading.md)). Its flop loop reaches the SIMD roof only if the compiler turned the chains into vector instructions; open it in Compiler Explorer and look, and if it did not, you measured a ceiling, not the peak. And it needs [P1's escape hatch](p1-measure-first.md#keeping-the-compiler-from-helping-too-much) and [reporting rules](p1-measure-first.md#the-reporting-rules): the printed checksum only keeps the work alive, and a median of repeated runs is not yet an interval.

Fill in your own measurements, with the machine and the date, before you trust any ridge point computed from them:

| Machine | Date | Threads | Peak (GFlop/s, f32) | Peak (GFlop/s, f64) | Triad bandwidth (GB/s) | Ridge point, f32 (flops/byte) |
| --- | --- | --- | --- | --- | --- | --- |
| | | 1 | | | | |
| | | all | | | | |

Some facts about a machine need a query, not a timer: P2 read the cache line and cache sizes with `sysctl`. They say where a working set lives, which picks the roof, but no query returns the peak and the bandwidth themselves.

## For Vortex

!!! vortex "Exercise"

    **Build** an operational-intensity estimate that your compiler's `--explain` output ([P2](p2-memory-hierarchy.md#for-vortex)) prints for each function whose loop nest it fully understands, as a remark in the stream from [O1](o1-optimizer-contract.md#for-vortex).

    1. **Flops.** Count the floating-point additions, subtractions, multiplications and divisions each function executes, using the trip counts from [O8](o8-loops.md#for-vortex). A loop whose trip count is not known at compile time makes the count unknown, and the remark says so and names the loop.
    2. **Compulsory bytes.** The size of every array the function reads or writes, counted once. Print the ratio as the function's highest possible intensity, and say in the remark that it is a ceiling set by compulsory traffic.
    3. **Wording.** Every remark carries the word "estimate" and the assumptions it rests on. It never prints GFlop/s, a ridge point or the word "measured".

    **Not yet:** traffic beyond the compulsory floor, which needs a cache model ([P8](p8-cache-blocking.md)); a ridge point or attainable bound, which needs the reader's measured peak and bandwidth, not the compiler's knowledge; counting integer work or bounds checks; functions whose trip counts O8 cannot bound.

    **Proof that it works:**

    - For the stage 10 program, the remark reports 524,288 flops and 49,152 compulsory bytes for `multiply`, the values this chapter derived by hand, in a golden remark file.
    - Three more golden files, each for a function whose answer you work out on paper first: a loop that sums a `[f32; 1000]` array, a function with two sequential loops over different arrays, and a loop whose trip count comes from a parameter, which must produce the "unknown" remark.
    - A test over every remark your compiler produces on its whole test suite that fails if any intensity remark lacks the word "estimate" or contains "measured" or "GFlop/s".
    - A table, filled in by hand from your own measurements and the remark, with the machine and the date:

    | Kernel | Flops (remark) | Compulsory bytes (remark) | Highest intensity (remark) | Your machine's f32 ridge (measured) | Left or right of the ridge |
    | --- | --- | --- | --- | --- | --- |
    | stage 10 `multiply` | | | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **What is operational intensity?** Flops per byte of traffic between the last cache and DRAM, so reuse inside the caches costs nothing.
    - **What is the roofline bound?** Attainable performance is at most the smaller of the peak flop rate and the peak bandwidth times the kernel's intensity.
    - **What is the ridge point, and what does a ridge far to the right mean?** The intensity where the two lines meet; far to the right, only kernels with a great deal of reuse can reach the machine's peak.
    - **Why does tiling raise intensity without adding a flop?** Each byte of `b` brought from DRAM serves t multiply-adds instead of one, so traffic falls while the flops stay fixed; for an f32 matmul, intensity is about t / 2.
    - **What does a ceiling below the roof mean?** An optimization the kernel lacks; the gap to the next ceiling up is the most that optimization can gain.
    - **Which ceilings do Vortex's strict floating-point rules touch?** The multiply-add balance ceiling stays out of reach without fusion; the ILP ceiling stays open through independent elements of `c`, not split sums.
    - **Which roof applies to a kernel?** The one for the memory level its working set comes from, built for the number of threads it runs on.

## Where this comes back

!!! next "You will use this again in"

    - [P4. Seeing inside the CPU: counters and tools](p4-counters-and-tools.md): *measured bytes*, *checking an estimate against a counter*
    - [P5. The microarchitecture shelf](p5-microarchitecture.md): *latency against throughput*, *independent chains*
    - [P7. Loop transformations](p7-loop-transformations.md): *unroll-and-jam*, *interchange*
    - [P8. Cache blocking](p8-cache-blocking.md): *tile size against cache capacity*, *compulsory traffic*
    - [P10. Vectorization](p10-vectorization.md): *the SIMD ceiling*
    - [P12. Anatomy of a fast GEMM](p12-fast-gemm.md): *register blocking*, *reaching the roof*
    - [P16. Capstone: the ladder, measured](p16-capstone.md): *every rung on one measured roofline*
    - [G14. Measuring GPU code](../gpu/g14-measuring-gpu-code.md): *operational intensity on a throughput machine*

## Sources and further reading

Read the paper itself first: it is short, and sections 3 to 5 are the whole model, with the authors' Opteron figures. Section 7, a list of fallacies about the model, answers most of the objections a first reading raises. Section numbers below follow the Berkeley preprint of the paper, which is freely available.

[^roofline-om]: Samuel Williams, Andrew Waterman and David Patterson, "Roofline: An Insightful Visual Performance Model for Multicore Architectures", *Communications of the ACM* 52(4), April 2009, pages 65 to 76, <https://doi.org/10.1145/1498765.1498785>; preprint "Roofline: An Insightful Visual Performance Model for Floating-Point Programs and Multicore Architectures", <https://people.eecs.berkeley.edu/~kubitron/cs252/handouts/papers/RooflineVyNoYellow.pdf>. Section 3: off-chip bandwidth as the constraining resource, the definition of operational intensity and the reasons for the term, peak bandwidth from the authors' benchmark rather than pin bandwidth, the Opteron X2 (17.6 GFlop/s, 15 GB/s), the attainable-performance formula, the ridge point, and the Opteron X2 and X4 comparison (ridge from 1.0 to 4.4).
[^roofline-ceil]: Williams, Waterman and Patterson, section 4, "Adding ceilings to the model": the five Opteron X2 optimizations, the ceiling heights of Figure 2, the gap as potential reward, and the ordering of ceilings.
[^roofline-3cs]: Williams, Waterman and Patterson, section 5, "Tying the 3Cs to operational intensity" (compulsory misses set the least traffic; intensity that grows with problem size), and section 6.3.3 (the stencil's compulsory traffic on write-allocate architectures).
[^roofline-t4]: Williams, Waterman and Patterson, Table 4: the IBM Cell's "Without FMA" and "Peak DP" ceilings.
[^roofline-fall]: Williams, Waterman and Patterson, section 7, "Fallacies about Roofline": doubling cache size, concurrency and multicore, and rooflines built on L2 bandwidth.
[^roofline-conc]: Williams, Waterman and Patterson, section 8, "Conclusions": the ridge point as a better predictor than clock rate or peak performance.
[^stream]: John D. McCalpin, "STREAM: Sustainable Memory Bandwidth in High Performance Computers", run rules and definitions: the triad kernel and its 24 bytes per iteration, the array-size rule, and write-allocate traffic left out of the count. <https://www.cs.virginia.edu/stream/ref.html>
