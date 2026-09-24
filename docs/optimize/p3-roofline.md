# P3. The roofline model

<p class="page-intro">Arithmetic intensity and the two ceilings that bound a kernel's speed.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 35 minutes · Builds on: [P2. The memory hierarchy](p2-memory-hierarchy.md)</p>

???+ remember "Before you start, remember"

    ??? question "Why does stage 10 leave the matmul kernel as a plain triple loop instead of optimizing it?"

        Three reasons build on each other: fast matrix multiplication is a large subject on its own; an optimization needs a correct, slow version to measure against and to check its answer against; and Vortex's principles forbid speed that silently changes results, so the naive loop is the baseline every faster version must reproduce bit for bit.

        Introduced in [Build v0.1, stage 10](../compiler/guide/stage-10-matrix-multiplication.md#why-speed-can-wait).

    ??? question "Why is the naive matmul loop's access to `b[k, column]` memory-bound before any counter or timer says so?"

        It has neither spatial locality, since consecutive `k` jumps a whole row's width in memory rather than staying in one cache line, nor temporal locality, since each line of `b` is not revisited before the loop moves past it. `a[row, k]`, walked the same way, has spatial locality only. A loop shaped like that moves far more DRAM traffic per flop than its arithmetic alone would suggest.

        Introduced in [P2. The memory hierarchy](p2-memory-hierarchy.md#key-ideas).

    ??? question "How many scalar multiply-adds does one call to the stage 10 kernel perform, at its 64 &times; 64 &times; 64 size, and how do you know?"

        262,144: the inner `k` loop's trip count, 64, times the 64 &times; 64 values of `row` and `column`. The count follows from the loop's endpoints, both the constant 64.

        Introduced in [O8. Loops: structure, induction variables and bounds checks](o8-loops.md#the-checks-in-the-kernels-inner-loop).

    ??? question "What does marking Vortex's `&mut c` parameter noalias let the compiler skip, and what rule of the language backs it?"

        The runtime check, and the two copies of the loop, that a vectorizer must otherwise keep in case `c` overlaps `a` or `b`. References 9.8 settles the question at compile time: storage behind a `&mut` parameter is reachable through no other parameter of the same call.

        Introduced in [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md#the-price-of-not-knowing-runtime-checks).

    ??? question "What is the difference between a passed remark, a missed remark and an analysis remark?"

        A passed remark reports a transformation the compiler made. A missed remark reports one it tried and could not make. An analysis remark reports what a pass worked out, often the reason behind a missed remark next to it.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#remarks-the-optimizers-report).

!!! goals "In this chapter"

    - Define operational intensity as flops per byte of traffic between the cache hierarchy and DRAM, and compute it for a small kernel.
    - State the roofline bound, and explain why it is an upper limit on performance, never a promise of it.
    - Find a kernel's ridge point from a machine's peak flops and peak bandwidth, and read what a ridge far to the right implies.
    - Explain why blocking a loop raises its operational intensity without adding a single flop, and connect that to Vortex's matmul kernel.
    - Name the difference between a missing ceiling and a missing measurement, and say what Vortex's `--explain` may honestly print about either.

## A question the flop count alone cannot answer

The stage 10 kernel performs 262,144 scalar multiply-adds per call, a fact [O8](o8-loops.md#the-checks-in-the-kernels-inner-loop) established from the inner loop's trip count. Multiply-adds are usually counted as two floating-point operations each, one multiply and one add,[^roofline-def] so one call does 524,288 flops. That number says nothing about how long the call takes. It says nothing because a call to `multiply` does not spend its time only computing; it also spends time moving `a`, `b` and `c` between DRAM and the CPU's registers, and on most machines that movement, not the arithmetic, decides the running time.

Two kernels can perform the same 524,288 flops and finish at very different speeds, because they move different numbers of bytes to do it. The plain triple loop from stage 10 walks `b` column by column in its innermost loop, and [P2](p2-memory-hierarchy.md#cache-lines-and-why-order-matters) already showed what that pattern costs: on this machine, an Apple M4 Pro whose cache line is 128 bytes, walking a matrix column-major touches a fresh line on almost every visit instead of reusing one 32 times. A version of the same loop that has been split into small tiles, so that a whole panel of `b` stays in a fast cache while many rows of `c` are computed from it, performs the identical 524,288 flops but moves far fewer bytes from DRAM, because each byte of `b` is reused many times before it is evicted. The **roofline model** is the tool this chapter builds for turning "moves fewer bytes" into a number, and for saying, before you write a single tile, how much that number could possibly buy you.

## Operational intensity: flops per byte of DRAM traffic

**Operational intensity** is the ratio of floating-point operations a kernel performs to the bytes of traffic it causes between the last level of cache and main memory.[^roofline-def] Williams, Waterman and Patterson, who introduce the term, are careful about which bytes count: not every byte the source code reads or writes, but only the bytes that a load or a store actually has to fetch from or send to DRAM after the cache hierarchy has done what it can.[^roofline-def] A byte that is read once and then reused ten times from L1 costs one DRAM transaction, not eleven; a kernel's operational intensity rewards exactly that kind of reuse. They chose the word "operational" over the more common "arithmetic intensity" for two reasons: their traffic is measured between the caches and memory, not between the processor and the caches, so the model can credit a cache optimization even when it changes nothing about the arithmetic; and they wanted a term general enough for kernels whose operations are not arithmetic at all.[^roofline-def]

For the stage 10 kernel, the flop count is fixed by its shape: 524,288, for any schedule that computes the same sums. The byte count is not fixed. `intensity_model.cpp` computes it two ways for the 64 &times; 64 &times; 64 kernel. The **naive** estimate assumes the worst case for the plain `ijk` loop: with no reuse across the outer `row` loop, the 64 &times; 64 panel of `b` is re-read from DRAM once for every one of the 64 rows of `c`, while `a` and `c` are each touched once. The **compulsory** estimate is the least any correct schedule can move: every one of the three 64 &times; 64 arrays of 4-byte floats has to be read or written at least once, so 3 &times; 64 &times; 64 &times; 4 = 49,152 bytes is a floor no schedule can go below.[^roofline-3cs] Between those two, the program models a family of **tiled** schedules, where a panel of `b` is reused across groups of `t` rows of `c` instead of one, so the number of times `b` is re-read from DRAM falls from 64 (the naive case) toward 1 (the compulsory case) as `t` grows from 1 toward 64:

--8<-- "includes/examples/optimize/p3-roofline/intensity_model.cpp.md"

Naive intensity is 0.485 flops per byte; tiling with `t` = 32 brings it to 8.0; the compulsory floor is 10.667. No line of this program does any more arithmetic than the kernel it describes; what changes across the rows of that table is how many times the same bytes have to make the trip from DRAM, a question about schedule, not about the sum being computed.

??? check "A kernel performs 2 &times; 10<sup>6</sup> flops while moving 5 &times; 10<sup>5</sup> bytes between the caches and DRAM. What is its operational intensity, and how would you decide whether that counts as high?"

    2,000,000 / 500,000 = 4 flops per byte. On its own, 4 is neither high nor low: whether it is enough to make a kernel compute-bound depends on the machine's ridge point, the subject of the next section. On the Opteron X2 example below, whose ridge sits at about 1.17 flops per byte, 4 is comfortably past it.

## The roofline: two ceilings and the ridge point

A machine has a **peak floating-point performance**, the most flops per second its arithmetic units can retire, found from its specification or a microbenchmark, and a **peak memory bandwidth**, the most bytes per second its memory system can sustain, found by running a bandwidth benchmark rather than by reading the DRAM chips' pin bandwidth, since the achievable rate is usually lower.[^roofline-def] Given both numbers, the **roofline bound** on any kernel's attainable performance is:

$$\text{Attainable GFlop/s} = \min(\text{Peak GFlop/s},\ \text{Peak GB/s} \times \text{Operational intensity})$$

The second term is a straight line through the origin on a log-log plot of performance against intensity, because performance over intensity is exactly bandwidth: a fixed slope. Plotted next to the first term, a horizontal line at the peak, the two lines meet at one point, the **ridge point**: the operational intensity at which a kernel first has enough reuse to need the full compute peak rather than more bandwidth.[^roofline-def] Left of the ridge, the diagonal is the lower of the two lines, and the bound is set by bandwidth: the kernel is **memory-bound**, and doubling its intensity would double its bound. Right of the ridge, the flat line is lower, and the bound is set by compute: the kernel is **compute-bound**, and moving further right buys nothing until the machine's peak changes.

Williams, Waterman and Patterson build the model for a 2.2 GHz, dual-socket AMD Opteron X2 (model 2214), whose peak double-precision performance is 17.6 GFlop/s and whose peak DRAM bandwidth, measured by their own microbenchmark rather than the STREAM benchmark, is 15 GB/s.[^roofline-ridge] Dividing one by the other places that machine's ridge point at 17.6 / 15 &asymp; 1.17 flops per byte, which is what their Figure 1b rounds to 1.0 on its log2-spaced axis. `roofline_bound.cpp` applies exactly that formula to the intensities `intensity_model.cpp` computed:

--8<-- "includes/examples/optimize/p3-roofline/roofline_bound.cpp.md"

At 0.485 flops per byte, the naive schedule's bound is 15 &times; 0.485 &asymp; 7.3 GFlop/s: bandwidth-limited, on this machine, to well under half of peak. At 1.778 and above, every tiled schedule's bound is already the full 17.6 GFlop/s, because each of those intensities clears the ridge at 1.17. Figure 1 draws both points on the same chart the formula builds.

<figure class="vx-figure">
<svg viewBox="0 0 720 420" role="img" aria-label="A roofline chart for the Opteron X2 numbers from Williams, Waterman and Patterson: peak 17.6 gigaflops per second, peak bandwidth 15 gigabytes per second. The x-axis is operational intensity in flops per byte, log2-scaled from 1/4 to 16. The y-axis is attainable performance in gigaflops per second, log2-scaled from 1 to 32. A diagonal bandwidth-bound line rises from the bottom left and meets a flat peak line at the ridge point, about 1.17 flops per byte. A point for the naive 64 by 64 by 64 matmul, at intensity 0.49, sits on the diagonal at 7.3 gigaflops per second, in the memory-bound region. A point for the same kernel tiled with t equal to 32, at intensity 8.0, sits on the flat line at the 17.6 gigaflop peak, in the compute-bound region.">
<line class="vx-line" x1="90" y1="40" x2="90" y2="380"/>
<line class="vx-line" x1="90" y1="380" x2="680" y2="380"/>
<line class="vx-line" x1="90" y1="238" x2="302" y2="95" stroke-width="3"/>
<line class="vx-line" x1="302" y1="95" x2="660" y2="95" stroke-width="3"/>
<line class="vx-line" x1="90" y1="386" x2="90" y2="380"/>
<text class="vx-text-muted" x="90" y="400" text-anchor="middle">1/4</text>
<line class="vx-line" x1="185" y1="386" x2="185" y2="380"/>
<text class="vx-text-muted" x="185" y="400" text-anchor="middle">1/2</text>
<line class="vx-line" x1="280" y1="386" x2="280" y2="380"/>
<text class="vx-text-muted" x="280" y="400" text-anchor="middle">1</text>
<line class="vx-line" x1="375" y1="386" x2="375" y2="380"/>
<text class="vx-text-muted" x="375" y="400" text-anchor="middle">2</text>
<line class="vx-line" x1="470" y1="386" x2="470" y2="380"/>
<text class="vx-text-muted" x="470" y="400" text-anchor="middle">4</text>
<line class="vx-line" x1="565" y1="386" x2="565" y2="380"/>
<text class="vx-text-muted" x="565" y="400" text-anchor="middle">8</text>
<line class="vx-line" x1="660" y1="386" x2="660" y2="380"/>
<text class="vx-text-muted" x="660" y="400" text-anchor="middle">16</text>
<text class="vx-text-muted" x="385" y="416" text-anchor="middle">Operational intensity (flops / byte, log&#8322; scale)</text>
<line class="vx-line" x1="84" y1="360" x2="90" y2="360"/>
<text class="vx-text-muted" x="78" y="364" text-anchor="end">1</text>
<line class="vx-line" x1="84" y1="296" x2="90" y2="296"/>
<text class="vx-text-muted" x="78" y="300" text-anchor="end">2</text>
<line class="vx-line" x1="84" y1="232" x2="90" y2="232"/>
<text class="vx-text-muted" x="78" y="236" text-anchor="end">4</text>
<line class="vx-line" x1="84" y1="168" x2="90" y2="168"/>
<text class="vx-text-muted" x="78" y="172" text-anchor="end">8</text>
<line class="vx-line" x1="84" y1="104" x2="90" y2="104"/>
<text class="vx-text-muted" x="78" y="108" text-anchor="end">16</text>
<line class="vx-line" x1="84" y1="40" x2="90" y2="40"/>
<text class="vx-text-muted" x="78" y="44" text-anchor="end">32</text>
<text class="vx-text-muted" x="26" y="210" text-anchor="middle" transform="rotate(-90 26 210)">Attainable performance (GFlop/s, log&#8322; scale)</text>
<circle class="vx-dot" cx="302" cy="95" r="4"/>
<text class="vx-mono" x="302" y="80" text-anchor="middle">ridge &asymp; 1.17</text>
<text class="vx-text-muted" x="150" y="300" text-anchor="middle">memory-bound</text>
<text class="vx-text-muted" x="430" y="300" text-anchor="middle">compute-bound</text>
<line class="vx-line" x1="181" y1="380" x2="181" y2="177" stroke-dasharray="4 4"/>
<circle class="vx-dot" cx="181" cy="177" r="5"/>
<text class="vx-text" x="196" y="163">naive</text>
<text class="vx-text-muted" x="196" y="180">0.49 flops/B, 7.3 GFlop/s</text>
<line class="vx-line" x1="565" y1="380" x2="565" y2="95" stroke-dasharray="4 4"/>
<circle class="vx-dot" cx="565" cy="95" r="5"/>
<text class="vx-text" x="470" y="66" text-anchor="middle">tiled, t = 32</text>
<text class="vx-text-muted" x="470" y="82" text-anchor="middle">8.0 flops/B, 17.6 GFlop/s</text>
</svg>
<figcaption>Figure 1. The roofline for the Opteron X2 numbers Williams, Waterman and Patterson measured.[^roofline-ridge] The naive 64 &times; 64 &times; 64 matmul from <code>intensity_model.cpp</code> sits under the diagonal, bandwidth-bound. Tiling with t = 32 does not add a flop; it moves the same kernel right, past the ridge, onto the flat ceiling.</figcaption>
</figure>

The ridge point also says something about a machine, on its own, independent of any kernel. Williams, Waterman and Patterson compare the Opteron X2 to its successor, the Opteron X4: the two share a socket and DRAM channels, so their peak bandwidth is close, but the X4 has twice the cores and each core can issue two SIMD floating-point instructions per cycle against the X2's one, so its peak flops are more than four times higher. Its ridge point shifts right accordingly, from 1.0 to 4.4.[^roofline-ridge] A higher ridge point is not free: it means more kernels fall to its left, memory-bound, and need more reuse before the extra compute hardware helps them at all.

??? check "A machine's peak floating-point performance doubles, through a wider SIMD unit, while its DRAM bandwidth stays the same. What happens to its ridge point, and to a kernel whose operational intensity does not change?"

    The ridge point doubles too, since it is peak flops divided by peak bandwidth. A kernel whose intensity stayed the same, and which used to sit at or past the old ridge, compute-bound, can now sit to the left of the new one: the same kernel becomes memory-bound on the faster machine, exactly what happens to the Opteron X2's kernels when compared against the X4's higher ridge.

??? check "Why does tiling change a kernel's operational intensity when it performs exactly the same flops and touches exactly the same array elements as the naive loop?"

    Operational intensity counts DRAM traffic, the bytes that cross the boundary between the last cache and main memory, not the bytes the source code logically reads.[^roofline-def] Tiling does not change how many elements the kernel touches; it changes how many times each one has to be re-fetched from DRAM after being evicted. A larger tile keeps a panel of `b` resident across more rows of `c`, so the same panel answers more of the multiply-adds before it leaves cache, and the DRAM byte count falls while the flop count stays exactly 524,288.

## Ceilings below the roofline

The roofline bound is an upper limit, not a forecast: a kernel that reaches its ridge point in principle can still fall well short of the roof in practice, if it fails to use the hardware in some other way. Williams, Waterman and Patterson add five such **ceilings** below the two main lines, each naming one optimization that stands between an unoptimized kernel and the roof: improving instruction-level parallelism and using SIMD; balancing the mix of floating-point multiplies and additions, since many machines have combined multiply-add instructions that need roughly equal counts of each to reach their full rate; restructuring loops for unit-stride memory access, which is what makes hardware prefetching effective; ensuring memory affinity on a multi-socket machine, so a core's data comes from the DRAM attached to its own socket; and using software prefetching where it beats the hardware prefetcher on its own.[^roofline-ceilings] The gap between an achieved point and the ceiling above it is the potential reward for chasing that one optimization; a point that is already close to a ceiling has little to gain from it and should look at the next one up instead.[^roofline-ceilings]

Two of these ceilings connect directly to how Vortex is specified. The unit-stride ceiling is why the ikj loop order matters for the stage 10 kernel: [O9](o9-alias-analysis.md#the-price-of-not-knowing-runtime-checks) already showed that, in that order, the innermost loop walks `b` and `c` with unit stride and, once `c` is known disjoint from `a` and `b`, needs no runtime alias check before the vectorizer can use it. The balanced-multiply-add ceiling is where Vortex's numerical rules bite. On most current hardware, reaching that ceiling in practice means issuing fused multiply-add instructions, which round their product-then-sum once instead of twice and so can change a sum's least significant bits.[^roofline-ceilings] Vortex's [philosophy](../philosophy.md#performance-philosophy) commits the compiler to never changing a specified floating-point result without the programmer's permission, so a strict build of the kernel leaves this ceiling below its reach on purpose, and the remark that says why is more useful to the programmer than the missing GFlop/s alone.

??? check "Vortex's compiler must not fuse a multiply and an add into a single rounding step unless the programmer allows it. Which of the five ceilings above does that leave out of automatic reach, and what should the compiler's remark for a strict build say?"

    The floating-point balance ceiling, which on most hardware is reached with fused multiply-add instructions. A useful remark names the ceiling and the reason it is missing rather than staying silent: something like "not fused: strict floating-point mode; the `contract` option would allow it", which tells the programmer both what was skipped and how to opt in, matching the philosophy's [principle 6, to explain performance decisions](../philosophy.md#6-explain-performance-decisions), rather than leave the programmer guessing.

## Measuring your own machine

Every number in the worked example above belongs to a 2008 dual-socket Opteron, not to any machine Vortex targets, because it is the only peak-flops and peak-bandwidth pair this chapter has a published, dated source for. Using it on your own machine would be exactly the mistake the philosophy's list of [compiler responsibilities](../philosophy.md#programmer-and-compiler-responsibilities) warns against: "never presenting an unverified performance estimate as a measured result." Building a roofline for a real machine needs two numbers this chapter does not supply, and [P1](p1-measure-first.md)'s measurement protocol is how you get them honestly: a peak-flops microbenchmark, an FMA or multiply-add chain with enough independent accumulators to hide the operation's latency, and a peak-bandwidth microbenchmark, a STREAM-like triad whose working set is too large to fit in any cache, so that every triad reads and writes go all the way to DRAM. Both need [P1's escape hatch](p1-measure-first.md#keeping-the-compiler-from-helping-too-much) to stop the optimizer from deleting or hoisting the very work being timed, and both need [P1's reporting rules](p1-measure-first.md#the-reporting-rules): a median with a confidence interval, not one run, and the machine and date written down next to the numbers.

Some facts about a machine do not need a timed benchmark, only a query: P2 already reports this chapter's own machine's cache line and L1 data cache size that way. Facts like those bound how large a tile can be before it stops fitting in a given cache level, which matters for [P8](p8-cache-blocking.md), but they are not the peak-flops and peak-bandwidth pair the roofline bound itself needs, and no query returns those two. Fill in your own measured pair, with the machine and the date, before trusting any ridge point you compute from it:

| Machine | Date | Peak (GFlop/s) | Peak bandwidth (GB/s) | Ridge point (flops/byte) |
| --- | --- | --- | --- | --- |
| | | | | |

## For Vortex

!!! vortex "Exercise"

    **Build** an operational-intensity estimate that `vortex --explain` can print for a kernel whose shape the compiler knows fully, on top of the trip counts from [O8](o8-loops.md#for-vortex) and the remark stream from [O1](o1-optimizer-contract.md#for-vortex).

    1. A flop counter over the compiler's own loop-nest representation: for each arithmetic operation inside loops with known trip counts, multiply its per-iteration count (1) by the product of the enclosing trip counts, and sum by operation kind. Give up, with an unknown count, on any loop whose trip count is not a compile-time constant or a value O8's analysis can bound.
    2. A byte-traffic estimate with two numbers, not one: the compulsory bound, three times each array's size for a kernel shaped like matmul, generalized to "each array's size, once, for every array the kernel reads or writes"; and, only if the pass pipeline has already tiled the loop nest, a second estimate that accounts for the tile size, built the same way `intensity_model.cpp` builds it.
    3. A remark, one per kernel, that prints both flop and byte counts, the ratio, and the word "estimate": never "measured", never a bare number that could be mistaken for one, in keeping with the [compiler responsibility](../philosophy.md#programmer-and-compiler-responsibilities) never to present an unverified performance estimate as a measured result.

    **Not yet:** printing a ridge point or an attainable-GFlop/s bound, since that needs a peak-flops and peak-bandwidth pair for the reader's own machine, which only P1's protocol can supply, not the compiler; modeling capacity or conflict misses precisely, which needs a cache model ([P8](p8-cache-blocking.md)); estimating traffic for a kernel whose loop nest has not been fully analyzed by O8's tools.

    **Proof that it works:** a table of the stage 10 kernel's flop and byte estimates at a few tile factors, checked by hand against a version of `intensity_model.cpp`'s arithmetic; and a grep-based check over the compiler's own test suite that every printed intensity, on every kernel, carries the word "estimate" and no printed number is ever labelled as measured GFlop/s.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is operational intensity?** Flops performed per byte of traffic between the last cache level and DRAM, after the cache hierarchy has filtered a kernel's true memory traffic.
    - **What is the roofline bound?** Attainable GFlop/s is at most the smaller of a machine's peak flops and its peak bandwidth times the kernel's operational intensity.
    - **What is the ridge point, and what does a ridge far to the right mean for a machine?** The intensity at which the two lines cross; a ridge far to the right means only kernels with a great deal of reuse can reach that machine's peak, and most kernels will be bandwidth-limited on it.
    - **Why does tiling a loop raise its operational intensity without adding a flop?** It reuses the same cached data across more of the computation before it is evicted, cutting DRAM traffic while the flop count stays fixed.
    - **What do the ceilings below the roofline mean?** Each names one optimization; the gap between an achieved point and the next ceiling up is that optimization's potential reward, and a point already near a ceiling should look at a different one.
    - **Which ceiling does Vortex's strict floating-point mode leave below reach on purpose, and why?** The balanced multiply-add ceiling, usually reached with fused multiply-add instructions; Vortex will not fuse without the programmer's permission, because fusing changes rounding.
    - **May a Vortex compiler print a ridge point for the reader's machine?** Not from its own knowledge: it may print an estimated operational intensity, labelled as an estimate, but the peak-flops and peak-bandwidth pair a ridge point needs can only come from a measurement under P1's protocol.

## Where this comes back

!!! next "You will use this again in"

    - [P4. Seeing inside the CPU: counters and tools](p4-counters-and-tools.md): *measured flops and bytes*, *checking an estimate against a counter*
    - [P7. Loop transformations](p7-loop-transformations.md): *transformations that raise intensity without adding flops*
    - [P8. Cache blocking](p8-cache-blocking.md): *tile size against cache capacity*, *the compulsory-to-naive gap*
    - [P10. Vectorization](p10-vectorization.md): *the ILP and SIMD ceiling*
    - [P12. Anatomy of a fast GEMM](p12-fast-gemm.md): *packing and register blocking as ceiling-filling optimizations*
    - [P16. Capstone: the ladder, measured](p16-capstone.md): *every rung plotted on one measured roofline*
    - [G14. Measuring GPU code](../gpu/g14-measuring-gpu-code.md): *operational intensity on a throughput machine*

## Sources and further reading

Read Williams, Waterman and Patterson's paper itself first: it is short, and sections 3 to 5 are the whole model, in the authors' own words and with their own Opteron figures. The five ceilings in section 4 are worth reading even where this chapter did not use them, since P4, P7 and P10 will.

[^roofline-def]: Samuel Williams, Andrew Waterman and David Patterson, "Roofline: An Insightful Visual Performance Model for Floating-Point Programs and Multicore Architectures", *Communications of the ACM* 52(4), 2009 (revised from the original 2008 technical report): the Introduction and section 3, "The Roofline Model", including the definition of operational intensity, the two reasons for that term over "arithmetic intensity", and the attainable-performance formula, read on 2026-09-24. <https://doi.org/10.1145/1498765.1498785>
[^roofline-ridge]: Williams, Waterman and Patterson, section 3 (the Opteron X2 model: 17.6 GFlop/s peak, 15 GB/s peak bandwidth from the authors' own benchmark, and the ridge point at 1.0 on Figure 1b) and section 4 (the Opteron X2 to X4 comparison: same sockets and DRAM channels, more than four times the peak flops per core, ridge point from 1.0 to 4.4), read on 2026-09-24. <https://doi.org/10.1145/1498765.1498785>
[^roofline-ceilings]: Williams, Waterman and Patterson, section 4, "Adding Ceilings to the Model", the five Opteron X2 optimizations and the rule for ranking ceilings by potential reward, read on 2026-09-24. <https://doi.org/10.1145/1498765.1498785>
[^roofline-3cs]: Williams, Waterman and Patterson, section 5, "Tying the 3Cs to Operational Intensity": compulsory misses set the minimum memory traffic and so the highest possible operational intensity, read on 2026-09-24. <https://doi.org/10.1145/1498765.1498785>
