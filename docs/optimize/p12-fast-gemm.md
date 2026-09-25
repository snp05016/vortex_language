# P12. Anatomy of a fast GEMM

<p class="page-intro">A tuned BLAS and the naive triple loop compute the same products. What separates them is how the data reaches the multiply-adds: copied once into packed buffers, walked by five nested loops sized from the machine's caches, and consumed by a small register-blocked micro-kernel. This chapter takes that design apart, counts what each piece costs, and shows which way of writing it keeps every bit of the naive answer, the version a strict Vortex compiler may emit.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 60 minutes · Builds on: [P8. Cache blocking](p8-cache-blocking.md), [P10. Vectorization](p10-vectorization.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does a 2 by 2 register block save over a plain scalar inner loop, and where do the saved values live?"

        Each step of `k` loads two values of `a` and two of `b` for four multiply-adds: one load per multiply-add instead of two. The four running sums stay in registers for the whole `k` loop, not in `c`.

        Introduced in [P7. Loop transformations](p7-loop-transformations.md#unroll-and-jam-and-register-blocks).

    ??? question "When may a tiled matrix multiplication still print the naive loop's exact bits?"

        When every element of `c` still adds its products one at a time, in increasing `k`, into one running value: the tiles along `k` must run from low to high. Tiles along `i` and `j` may run in any order.

        Introduced in [P8. Cache blocking](p8-cache-blocking.md#keeping-the-order-keeping-the-bits).

    ??? question "Why can a Vortex compiler assume that writing through a kernel's `&mut c` never changes `a` or `b`?"

        References 9.8 makes it a rule of the language: while `c` is lent as `&mut`, it may be used only through that reference and may not appear in any other argument of the same call, so storage reached through it cannot be reached through `a` or `b`.

        Introduced in [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md#promises-the-front-end-writes-down).

    ??? question "Why does a small local array indexed by a loop variable usually block SROA, and what removes the block?"

        SROA needs every use of a slot at a constant offset; a variable index could touch any element, so the whole array is kept as one piece. Fully unrolling the loop that indexes it turns every offset into a compile-time constant, which is why LLVM runs SROA again right after full unrolling.

        Introduced in [O7. Calls and aggregates: inlining and SROA](o7-inlining-and-sroa.md#aggregates-in-memory).

!!! goals "In this chapter"

    - Count the cache lines and pages a micro-kernel's operands touch in place and after packing, and explain what packing fixes that tiling leaves alone.
    - Trace the five loops Goto's and BLIS's designs put around one micro-kernel, and count by hand the copies, calls and multiply-adds they perform for a given shape.
    - Build a register-blocked micro-kernel over packed panels, and tell the way of accumulating that keeps the naive loop's bits from the way that does not.
    - Bound each of `mc`, `kc`, `nc`, `mr` and `nr` by the machine fact that limits it, and find that fact on your own machine.
    - Plan a packing and micro-kernel stage for Vortex that decision 56 permits without an opt-in.

## What tiling leaves behind

[P8](p8-cache-blocking.md) tiled the matrix multiplication so that the blocks of `a`, `b` and `c` a tile reuses fit in a cache. [P7](p7-loop-transformations.md#unroll-and-jam-and-register-blocks) went one level further down and kept an `mr` by `nr` block of `c` in registers. Put those two together and look at what the innermost code reads.

Take `f32` matrices whose rows are 1024 elements long. Vortex stores them in **row-major** order: the elements of one row sit next to each other, and the next row starts `columns` elements later ([Arrays and shapes 7.8](../specification/arrays.md#78-memory-and-layout)). Here one row is 4096 bytes. A 4 by 4 register block, at each step of `k`, needs four values from one row of `b`, `b[k, j..j+4]`, which are adjacent, and four values from one column of `a`, `a[i..i+4, k]`, which are 4096 bytes apart.

Run that block for 256 steps of `k`, as a tile would. The reads of `b` form a **sliver**: 256 rows, 16 bytes from each. Two facts about the memory system decide what that costs.

- The cache moves data in **cache lines**, fixed-size aligned chunks; on the author's Apple M4 Pro, `sysctl hw.cachelinesize` reports 128 bytes. Reading 16 bytes brings in a whole line.
- A program's addresses are virtual. Each access needs the physical **page** it lands on, and the processor keeps recent page translations in a small cache called the **translation lookaside buffer** (**TLB**). An access whose page is not in the TLB waits for the page tables to be read. `sysctl hw.pagesize` reports 16 KiB on the same machine.

The first example counts both, for the `b` sliver and for a 64 by 256 block of `a`, first where they sit in the big matrix and then copied into a contiguous buffer:

--8<-- "includes/examples/optimize/p12-fast-gemm/panel_footprint.cpp.md"

In place, the `b` sliver touches 256 lines and uses 16 bytes of each, so seven eighths of every line the cache fetches is data the micro-kernel does not need yet. It spans 64 pages. Copied into 4 KiB of contiguous memory, the same values fill 32 lines completely and sit on one page. The block of `a` shows the other half of the story: its rows are long enough to use whole lines, so the line count does not change, but its 64 rows are spread over 16 pages instead of 4.

Goto and van de Geijn built their matrix multiplication around exactly these observations. A submatrix of a big matrix needs many more TLB entries than its size requires, a TLB miss stalls the processor in a way prefetching cannot hide, and the amount of data the TLB can address is often what limits the size of the block of `a` that can be reused.[^goto08] Rows a power of two apart also tend to compete for the same few places in the cache, the self-interference [P8](p8-cache-blocking.md#fitting-is-not-enough-self-interference) described. Tiling chose how much data to reuse; it left that data scattered.

## Packing: copy once, read many times

**Packing** copies a block of an operand into a small contiguous buffer, laid out in exactly the order the micro-kernel will read it, and the loops that follow read the copy instead of the original. Goto and van de Geijn call the packed block of `a` `Ã` and the packed panel of `b` `B̃`, and BLIS keeps those names.[^goto08] [^blis15]

The copy is more memory traffic, not less. It pays because of reuse. Goto and van de Geijn put the arithmetic plainly: packing a `kc` by `n` panel of `b` costs time proportional to `kc × n` and is spread over `2 × m × n × kc` floating-point operations, so each copied element of `b` serves on the order of `m` operations; each copied element of `a` serves on the order of `n`.[^goto08] For large matrices the copy is a small fraction of the work.

What order is "the order the micro-kernel reads"? At each step of `k` it wants `mr` values of `a` from one column and `nr` values of `b` from one row. So `Ã` is cut into **micro-panels** of `mr` rows, and inside each micro-panel the `mr` values for `k = 0` come first, then those for `k = 1`, and so on. `B̃` is cut into micro-panels of `nr` columns, stored the same way, row by row. The BLIS kernel documentation describes the same two layouts: the `a` micro-panel stored by columns and the `b` micro-panel stored by rows.[^blis-k] Figure 1 shows the layout for a block of five rows of `a` and `mr = 2`.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="A five by three block of a, stored row-major, packed into micro-panels of two rows each, stored column by column, with the missing sixth row filled with zeros" aria-describedby="p12-f1-desc">
<title id="p12-f1-title">Packing a block of a into micro-panels</title>
<desc id="p12-f1-desc">Left, a grid of five rows and three columns holding the values 0 to 14 in row-major order, so row r holds 3r, 3r+1 and 3r+2. Rows 0 and 1 are shaded as micro-panel 0, rows 2 and 3 as micro-panel 1, and row 4 as micro-panel 2, with a dashed sixth row that does not exist in the matrix. An arrow labelled pack once leads right to a single strip of 18 cells: 0, 3, 1, 4, 2, 5, then 6, 9, 7, 10, 8, 11, then 12, 0, 13, 0, 14, 0. The zeros in the last third are marked as padding. Brackets under the strip label each group of two cells as one step of k.</desc>
<defs><marker id="p12-f1-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="28">a, row-major (5 rows × 3 columns)</text>
<rect class="vx-box-accent" x="20" y="44" width="150" height="60" rx="3"/>
<rect class="vx-box" x="20" y="104" width="150" height="60" rx="3"/>
<rect class="vx-box-accent" x="20" y="164" width="150" height="30" rx="3"/>
<rect class="vx-box-bad" x="20" y="194" width="150" height="30" rx="3"/>
<text class="vx-mono" x="45" y="64" text-anchor="middle">0</text><text class="vx-mono" x="95" y="64" text-anchor="middle">1</text><text class="vx-mono" x="145" y="64" text-anchor="middle">2</text>
<text class="vx-mono" x="45" y="94" text-anchor="middle">3</text><text class="vx-mono" x="95" y="94" text-anchor="middle">4</text><text class="vx-mono" x="145" y="94" text-anchor="middle">5</text>
<text class="vx-mono" x="45" y="124" text-anchor="middle">6</text><text class="vx-mono" x="95" y="124" text-anchor="middle">7</text><text class="vx-mono" x="145" y="124" text-anchor="middle">8</text>
<text class="vx-mono" x="45" y="154" text-anchor="middle">9</text><text class="vx-mono" x="95" y="154" text-anchor="middle">10</text><text class="vx-mono" x="145" y="154" text-anchor="middle">11</text>
<text class="vx-mono" x="45" y="184" text-anchor="middle">12</text><text class="vx-mono" x="95" y="184" text-anchor="middle">13</text><text class="vx-mono" x="145" y="184" text-anchor="middle">14</text>
<text class="vx-text-muted" x="95" y="214" text-anchor="middle">no row 5</text>
<text class="vx-text-muted" x="180" y="78">panel 0</text>
<text class="vx-text-muted" x="180" y="138">panel 1</text>
<text class="vx-text-muted" x="180" y="198">panel 2</text>
<path class="vx-flow" d="M240 134 L300 134" marker-end="url(#p12-f1-head)"/>
<text class="vx-text-muted" x="244" y="124">pack once</text>
<text class="vx-text" x="310" y="28">Ã: one contiguous buffer</text>
<rect class="vx-box-accent" x="310" y="118" width="144" height="32" rx="3"/>
<rect class="vx-box" x="454" y="118" width="144" height="32" rx="3"/>
<rect class="vx-box-accent" x="598" y="118" width="144" height="32" rx="3"/>
<text class="vx-mono" x="322" y="139" text-anchor="middle">0</text><text class="vx-mono" x="346" y="139" text-anchor="middle">3</text><text class="vx-mono" x="370" y="139" text-anchor="middle">1</text><text class="vx-mono" x="394" y="139" text-anchor="middle">4</text><text class="vx-mono" x="418" y="139" text-anchor="middle">2</text><text class="vx-mono" x="442" y="139" text-anchor="middle">5</text>
<text class="vx-mono" x="466" y="139" text-anchor="middle">6</text><text class="vx-mono" x="490" y="139" text-anchor="middle">9</text><text class="vx-mono" x="514" y="139" text-anchor="middle">7</text><text class="vx-mono" x="538" y="139" text-anchor="middle">10</text><text class="vx-mono" x="562" y="139" text-anchor="middle">8</text><text class="vx-mono" x="586" y="139" text-anchor="middle">11</text>
<text class="vx-mono" x="610" y="139" text-anchor="middle">12</text><text class="vx-text-muted" x="634" y="139" text-anchor="middle">0</text><text class="vx-mono" x="658" y="139" text-anchor="middle">13</text><text class="vx-text-muted" x="682" y="139" text-anchor="middle">0</text><text class="vx-mono" x="706" y="139" text-anchor="middle">14</text><text class="vx-text-muted" x="730" y="139" text-anchor="middle">0</text>
<path class="vx-line" d="M312 158 L358 158"/><text class="vx-text-muted" x="335" y="176" text-anchor="middle">k=0</text>
<path class="vx-line" d="M360 158 L406 158"/><text class="vx-text-muted" x="383" y="176" text-anchor="middle">k=1</text>
<path class="vx-line" d="M408 158 L452 158"/><text class="vx-text-muted" x="430" y="176" text-anchor="middle">k=2</text>
<text class="vx-text-muted" x="670" y="100" text-anchor="middle">zeros: padding</text>
<text class="vx-text-muted" x="310" y="226">Each step of k reads two neighbours; the three</text>
<text class="vx-text-muted" x="310" y="246">micro-panels follow one another with no gap.</text>
<rect class="vx-box-accent" x="20" y="286" width="22" height="14" rx="3"/><text class="vx-text-muted" x="48" y="297">one micro-panel (mr = 2 rows)</text>
<rect class="vx-box-bad" x="300" y="286" width="22" height="14" rx="3"/><text class="vx-text-muted" x="328" y="297">rows outside the matrix, packed as zeros</text>
</svg>
<figcaption>Figure 1. Packing five rows of <code>a</code> with <code>mr = 2</code>. Each micro-panel is stored column by column, so one step of <code>k</code> reads <code>mr</code> neighbouring values. The block has no sixth row, so the last micro-panel is padded with zeros and stays the same width as the others.</figcaption>
</figure>

The second example is that packing routine, for the same five rows:

--8<-- "includes/examples/optimize/p12-fast-gemm/pack_panel.cpp.md"

Five rows do not divide into strips of two, so the third strip is filled out with a row of zeros. The micro-kernel then always processes exactly `mr` rows, with no special case for the edge; the products it forms with the zero row land in accumulators whose results are thrown away.

BLIS describes this design: packed dimensions are rounded up to a multiple of `mr` or `nr` and zero-padded, the micro-kernel writes an edge tile to a temporary `mr` by `nr` buffer, and only the elements that exist are copied to `c`.[^blis15] (Newer BLIS versions have moved edge handling into the micro-kernel itself, which now receives the real tile size.[^blis-k]) The discarded results must stay discarded: if `b` holds an infinity, `0 × ∞` in a padded lane is NaN.

Packing is legal for a Vortex kernel for two reasons. It performs no arithmetic, so [decision 56](../decisions/numbers.md#d56) has nothing to say about it. And the copies of `a` and `b` stay correct while `c` is being written only because `c` cannot share storage with them, which is what [References 9.8](../specification/references.md#98-aliasing) guarantees for a `&mut` parameter.

??? check "The example packs 5 rows with `mr = 2`, giving three strips. With `mr = 4` instead, how many strips does the block need, and how many of the values in the packed buffer are padding when `kc = 3`?"

    Two strips, since 5 rows need two groups of four. The second strip holds row 4 and three padding rows, and each row contributes `kc = 3` values, so 9 of the 24 packed values are padding. Padding grows with `mr`, which is one reason edges cost more for wide micro-kernels.

## The five loops around one micro-kernel

Packing one block is a step, not a schedule. Goto and van de Geijn's algorithm, and the BLIS framework that generalizes it, arrange a whole matrix multiplication `c += a × b` (with `a` of size `m` by `k`, `b` of size `k` by `n`) as **five nested loops** around one **micro-kernel**. Smith and colleagues name the loops, from the outside in:[^smith14]

1. **`jc`** steps through the columns of `b` and `c`, `nc` at a time.
2. **`pc`** steps through `k`, `kc` at a time. Here the current `kc` by `nc` panel of `b` is packed into `B̃`.
3. **`ic`** steps through the rows of `a` and `c`, `mc` at a time. Here the current `mc` by `kc` block of `a` is packed into `Ã`.
4. **`jr`** steps through `B̃`, one micro-panel of `nr` columns at a time.
5. **`ir`** steps through `Ã`, one micro-panel of `mr` rows at a time, and calls the micro-kernel.

The micro-kernel multiplies one `mr` by `kc` micro-panel of `Ã` by one `kc` by `nr` micro-panel of `B̃` and adds the result into an `mr` by `nr` tile of `c` held in registers. It does this as `kc` **rank-1 updates**: at each step of `k`, every value of the `a` column slice is multiplied by every value of the `b` row slice, an **outer product**, and each of the `mr × nr` products is added to its own accumulator.[^smith14]

<figure class="vx-figure">
<svg viewBox="0 0 760 540" role="img" aria-label="Five nested loops around one micro-kernel, with the two loops that pack highlighted and the level of the memory hierarchy each piece of data is meant to occupy" aria-describedby="p12-f2-desc">
<title id="p12-f2-title">The five loops around one micro-kernel</title>
<desc id="p12-f2-desc">Six nested rectangles, largest to smallest. Outermost, jc, steps nc columns of b and c. Inside it, pc, highlighted, steps kc rows of b and packs a kc by nc panel into B tilde, meant for the L3 cache when there is one. Inside that, ic, highlighted, steps mc rows of a and packs an mc by kc block into A tilde, meant for the L2 cache. Inside that, jr steps one nr-wide micro-panel of B tilde, which sits in the L1 cache. Inside that, ir steps one mr-high micro-panel of A tilde. Innermost, strongly outlined, the micro-kernel keeps an mr by nr tile of c in registers and performs kc rank-1 updates. Two moving arrows run from the packing loops down into the micro-kernel.</desc>
<defs><marker id="p12-f2-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="20" y="20" width="560" height="500" rx="4"/>
<text class="vx-text" x="36" y="44">jc: nc columns of b and c</text>
<rect class="vx-box-accent" x="44" y="60" width="512" height="446" rx="4"/>
<text class="vx-text-accent" x="60" y="84">pc: kc rows of b, pack kc×nc into B̃</text>
<rect class="vx-box-accent" x="68" y="100" width="464" height="392" rx="4"/>
<text class="vx-text-accent" x="84" y="124">ic: mc rows of a, pack mc×kc into Ã</text>
<rect class="vx-box" x="92" y="140" width="416" height="338" rx="4"/>
<text class="vx-text" x="108" y="164">jr: one nr-wide micro-panel of B̃</text>
<rect class="vx-box" x="116" y="180" width="368" height="284" rx="4"/>
<text class="vx-text" x="132" y="204">ir: one mr-high micro-panel of Ã</text>
<rect class="vx-box-strong" x="140" y="220" width="320" height="170" rx="4"/>
<text class="vx-text" x="156" y="246">micro-kernel</text>
<text class="vx-mono" x="156" y="272">load the mr×nr tile of c</text>
<text class="vx-mono" x="156" y="294">for each of kc steps:</text>
<text class="vx-mono" x="172" y="316">mr values of Ã, nr of B̃</text>
<text class="vx-mono" x="172" y="338">mr×nr multiply-adds</text>
<text class="vx-mono" x="156" y="360">store the tile</text>
<path class="vx-flow" d="M300 88 C 330 120, 480 200, 440 300" marker-end="url(#p12-f2-head)"/>
<path class="vx-flow" d="M300 128 C 320 170, 420 220, 420 320" marker-end="url(#p12-f2-head)"/>
<text class="vx-text-muted" x="140" y="420">everything inside ic reads only Ã and B̃,</text>
<text class="vx-text-muted" x="140" y="440">never the original a and b</text>
<text class="vx-text" x="600" y="44">Where it is meant to live</text>
<text class="vx-text-muted" x="600" y="84">B̃: L3, if there is one</text>
<text class="vx-text-muted" x="600" y="124">Ã: L2</text>
<text class="vx-text-muted" x="600" y="164">B̃ micro-panel: L1</text>
<text class="vx-text-muted" x="600" y="204">Ã micro-panel: streamed</text>
<text class="vx-text-muted" x="600" y="222">from L2</text>
<text class="vx-text-muted" x="600" y="272">c tile: registers</text>
</svg>
<figcaption>Figure 2. The five loops around one micro-kernel. The two highlighted loops, <code>pc</code> and <code>ic</code>, are the ones that pack; everything inside them reads only the packed buffers. The right-hand column gives the level of the memory hierarchy each piece is sized for, following Smith and colleagues; a machine without an L3 has no level to aim <code>B̃</code> at.</figcaption>
</figure>

Why this nesting? Read Figure 2 from the inside. The `c` tile stays in registers for all `kc` steps of one call, which is P7's register block. The `B̃` micro-panel, `kc × nr` values, is reused by every `ir` iteration, so it should stay in the L1 cache. `Ã`, `mc × kc` values, is reused by every `jr` iteration, so it should stay in the L2 cache, and its micro-panels stream from there into registers. `B̃` as a whole is reused by every `ic` iteration; if there is an L3 cache the design tries to keep it there, and the main reason `jc` exists at all is to limit how much memory `B̃` needs.[^smith14]

Goto's original kernel hid the two innermost loops, `jr` and `ir`, inside one hand-written assembly routine. BLIS pulled them out into portable C, which leaves the micro-kernel as the only code that has to be written for each architecture.[^smith14] [^blis15] Goto and van de Geijn had already split the work the same way at the level above: their packing routines were written in C, because compilers optimize those copies well, and their kernel in assembly.[^goto08]

The cache levels in Figure 2 are a design target, not a law. On the author's M4 Pro, `sysctl -a | grep l3` prints nothing, and `sysctl hw.perflevel0.l2cachesize hw.perflevel0.cpusperl2` reports a 16 MiB L2 shared by 4 CPUs (checked 2026-09-24). Smith and colleagues already allow for `B̃` living in main memory when there is no L3.[^smith14] The loop structure does not change; the numbers `nc`, `mc` and `kc` are chosen from whatever levels the machine has.

??? check "The `pc` loop packs `b` and the `ic` loop, one level further in, packs `a`. What would it cost to swap them, packing `a` outside and `b` inside?"

    Each packed panel is reused by the loops inside the one that packs it. With `B̃` packed in `pc`, one packing of a `kc` by `nc` panel serves every `mc`-row block of `a` that `ic` visits. Packing `b` inside `ic` instead would repeat the copy of the same panel once per block of `a`, about `m / mc` times as much copying of `b`, for the same arithmetic. The order also follows cache size: the buffer packed further out is the larger one and is meant for the larger cache.

## Walking the loops by hand

Counting what the loops do is the fastest way to understand them. Take `m = n = k = 8`, with `nc = 8`, `kc = 4`, `mc = 4`, `nr = 2` and `mr = 2`, so that every block size divides the matrix.

- `jc` runs once (`8 / 8`). `pc` runs twice (`8 / 4`). For each `pc`, `ic` runs twice (`8 / 4`). Inside, `jr` runs `nc / nr = 4` times and `ir` runs `mc / mr = 2` times.
- The micro-kernel is called `1 × 2 × 2 × 4 × 2 = 32` times. Each call does `mr × nr × kc = 16` multiply-adds, so the total is 512, which is `8 × 8 × 8`: nothing is done twice and nothing is skipped.
- `B̃` is packed once per `pc` iteration: two panels of `4 × 8`, 64 values. That is `k × n`, so each element of `b` is copied once.
- `Ã` is packed once per `ic` iteration of each `pc`: four blocks of `4 × 4`, 64 values. That is `m × k` times the number of `jc` iterations, one here, so each element of `a` is also copied once.
- Each call loads `mr + nr = 4` values from the packed buffers per step of `k` and does 4 multiply-adds: one load per multiply-add, as P7 predicted for a 2 by 2 block.

Now follow one packed value. An element of `Ã` sits in one micro-panel, which is read by all 4 `jr` iterations, and each read feeds `nr = 2` multiply-adds: 8 in all, which is `nc`. An element of `B̃` is read by the 2 `ir` iterations of each of the 2 `ic` blocks and feeds `mr = 2` multiply-adds each time: 8 in all, which is `m`. These are Goto and van de Geijn's two amortization counts, `n` and `m` operations per copied element, with `nc` in place of `n` when `jc` runs more than once.[^goto08]

| Quantity | `m = n = k = 8`, blocks 8, 4, 4, 2, 2 | `m = 7`, `n = 10`, `k = 9`, blocks 6, 4, 4, 3, 2 |
| --- | --- | --- |
| Trips of `jc`, `pc`, `ic` | 1, 2, 2 | ?, ?, ? |
| Micro-panels per `B̃` and per `Ã` | 4 and 2 | ? and ? |
| Micro-kernel calls | 32 | ? |
| Multiply-adds executed (useful) | 512 (512) | ? (630) |
| Values copied into `B̃` | 64 | ? |
| Values copied into `Ã` | 64 | ? |

The right-hand column is yours: `nc = 6`, `kc = 4`, `mc = 4`, `nr = 3`, `mr = 2`, and no block size divides its dimension. Count the edge blocks as full-size blocks padded with zeros, as the packing routine does.

??? check "Fill in the right-hand column."

    - `jc` runs twice (columns 0 to 5, then 6 to 9), `pc` three times (`k` blocks of 4, 4 and 1), `ic` twice (rows 0 to 3, then 4 to 6).
    - Each `B̃` has 2 micro-panels of 3 columns (the second `jc` block has 4 columns, padded to 6), and each `Ã` has 2 micro-panels of 2 rows (the second `ic` block has 3 rows, padded to 4).
    - Calls: `2 × 3 × 2 × 2 × 2 = 48`.
    - Multiply-adds: each `jc` and `pc` pair makes 8 calls of `2 × 3 × kb` multiply-adds, where `kb` is 4, 4 or 1; that is `8 × 6 × 9 = 432` per `jc` block, 864 in all, against 630 useful ones. The rest multiply padding.
    - `B̃`: 6 padded columns by 9 rows per `jc` block, 108 values, of which 90 are real.
    - `Ã`: 8 padded rows by 9 columns per `jc` block, and there are two `jc` blocks, so 144 values. Every element of `a` is copied twice, once per `jc` iteration.

    The padding overhead is large here only because the matrices are tiny. It grows with the block sizes and shrinks, relative to the useful work, as the matrices grow.

The third example runs this shape through all five loops, with the packing, the zero padding and a micro-kernel that continues `c`'s running sums, and counts the same quantities. Its last line compares every element with the naive loop, bit for bit:

--8<-- "includes/examples/optimize/p12-fast-gemm/five_loops.cpp.md"

## The register-blocked micro-kernel

[P7](p7-loop-transformations.md#unroll-and-jam-and-register-blocks) showed that an `mr` by `nr` register block loads `mr + nr` values per step for `mr × nr` multiply-adds, so the ratio `(mr + nr) / (mr × nr)` falls as the block grows. The micro-kernel is that register block, reading packed micro-panels at unit stride. Three limits decide how large it can grow.

**Registers.** The accumulators and the values loaded at each step must all fit in registers. Goto and van de Geijn report that typically half the registers hold the `c` tile, leaving the rest for loading the next values of `Ã` and `B̃`, and that the cost of loading registers is best amortized when `mr` is about equal to `nr`.[^goto08]

**Latency.** Each accumulator is updated once per step of `k`, and the update cannot start until the previous update of the same accumulator has finished. Low, Igual, Smith and Quintana-Ortí turn this into a lower bound: the tile must hold at least `Nvec × L × Nfma` values, where `Nvec` is the number of values in one vector register, `L` the latency of one fused multiply-add in cycles and `Nfma` the number of them the processor can start per cycle.[^low16] Fewer independent accumulators than that, and the floating-point units wait.

**Contraction.** Low and colleagues' model assumes the update is one **fused multiply-add** (FMA), an instruction that computes `a × b + c` with a single rounding, and tuned kernels are built around it.[^low16] [Decision 56](../decisions/numbers.md#d56) forbids a Vortex compiler from fusing a multiply and an add unless the program opts in, so a strict micro-kernel does each update as a rounded multiply followed by a rounded add. Low and colleagues cover that case too: without an FMA instruction, `L` is the latency of a multiply plus the latency of an add.[^low16] [P11](p11-floating-point.md#fused-multiply-add-one-rounding-instead-of-two) explains why the two versions round differently, and [P10](p10-vectorization.md) is where the reader measures what fusing is worth on their own machine.

Inside the micro-kernel, the loops over the tile's rows and columns are always fully unrolled, and the loop over `k` is often unrolled a few times; the BLIS kernel documentation gives these as rules of thumb for kernel authors.[^blis-k] Full unrolling is also what lets the compiler keep the accumulators in registers at all, as a later section shows.

## Two ways to accumulate

When `k` is longer than `kc`, the `pc` loop runs more than once and the micro-kernel is called several times for the same tile of `c`, once per panel. There are two ways to write it.

- **C-initialized.** Load the `c` tile into the accumulators at the start of the call, add the `kc` new products to them, store them back. The running sum of each `c` element continues from one panel into the next.
- **Zero-initialized.** Start the accumulators at zero, add the `kc` products, then add the finished panel total into `c`. This is the shape of the BLIS micro-kernel's contract, `C11 := beta * C11 + alpha * A1 * B1`, and of Goto and van de Geijn's kernel, which computes the block's product into a temporary and then adds the temporary to `c` (their Figure 8).[^blis-k] [^goto08]

In exact arithmetic the two are the same. In floating point they are not, because they group the additions differently. Figure 3 shows one element of `c` with `k = 4` split into two panels of `kc = 2`.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Two orders of addition for one element of c with four products split into two panels: a single chain for the C-initialized kernel, and two separate panel sums joined at the end for the zero-initialized kernel" aria-describedby="p12-f3-desc">
<title id="p12-f3-title">The same four products, grouped two ways</title>
<desc id="p12-f3-desc">Left, labelled C-initialized: a single chain of boxes, 0, then plus p0, plus p1, a dashed panel boundary, then plus p2, plus p3, ending at c. This is the naive loop's order. Right, labelled zero-initialized: two short chains, 0 plus p0 plus p1 giving panel total t1, and 0 plus p2 plus p3 giving panel total t2. Then c equals 0 plus t1, and c plus t2, the last addition highlighted as the one that regroups the sum. A note says (p2 + p3) is rounded before it meets p0 + p1.</desc>
<defs><marker id="p12-f3-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="28">C-initialized (same as the naive loop)</text>
<rect class="vx-box" x="20" y="48" width="70" height="30" rx="3"/><text class="vx-mono" x="55" y="68" text-anchor="middle">0</text>
<rect class="vx-box" x="20" y="98" width="70" height="30" rx="3"/><text class="vx-mono" x="55" y="118" text-anchor="middle">+ p0</text>
<rect class="vx-box" x="20" y="148" width="70" height="30" rx="3"/><text class="vx-mono" x="55" y="168" text-anchor="middle">+ p1</text>
<rect class="vx-box" x="20" y="208" width="70" height="30" rx="3"/><text class="vx-mono" x="55" y="228" text-anchor="middle">+ p2</text>
<rect class="vx-box" x="20" y="258" width="70" height="30" rx="3"/><text class="vx-mono" x="55" y="278" text-anchor="middle">+ p3</text>
<path class="vx-flow" d="M55 78 L55 97" marker-end="url(#p12-f3-head)"/>
<path class="vx-flow" d="M55 128 L55 147" marker-end="url(#p12-f3-head)"/>
<path class="vx-flow" d="M55 178 L55 207" marker-end="url(#p12-f3-head)"/>
<path class="vx-flow" d="M55 238 L55 257" marker-end="url(#p12-f3-head)"/>
<path class="vx-line" d="M10 193 L240 193" stroke-dasharray="4 4"/>
<text class="vx-text-muted" x="100" y="188">panel boundary: c stored,</text>
<text class="vx-text-muted" x="100" y="210">then loaded again</text>
<text class="vx-text-muted" x="100" y="278">one running sum</text>
<text class="vx-text" x="300" y="28">Zero-initialized (one sum per panel)</text>
<rect class="vx-box" x="300" y="48" width="70" height="30" rx="3"/><text class="vx-mono" x="335" y="68" text-anchor="middle">0</text>
<rect class="vx-box" x="300" y="98" width="70" height="30" rx="3"/><text class="vx-mono" x="335" y="118" text-anchor="middle">+ p0</text>
<rect class="vx-box" x="300" y="148" width="70" height="30" rx="3"/><text class="vx-mono" x="335" y="168" text-anchor="middle">+ p1</text>
<text class="vx-mono" x="335" y="198" text-anchor="middle">t1</text>
<rect class="vx-box" x="420" y="48" width="70" height="30" rx="3"/><text class="vx-mono" x="455" y="68" text-anchor="middle">0</text>
<rect class="vx-box" x="420" y="98" width="70" height="30" rx="3"/><text class="vx-mono" x="455" y="118" text-anchor="middle">+ p2</text>
<rect class="vx-box" x="420" y="148" width="70" height="30" rx="3"/><text class="vx-mono" x="455" y="168" text-anchor="middle">+ p3</text>
<text class="vx-mono" x="455" y="198" text-anchor="middle">t2</text>
<path class="vx-flow" d="M335 78 L335 97" marker-end="url(#p12-f3-head)"/>
<path class="vx-flow" d="M335 128 L335 147" marker-end="url(#p12-f3-head)"/>
<path class="vx-flow" d="M455 78 L455 97" marker-end="url(#p12-f3-head)"/>
<path class="vx-flow" d="M455 128 L455 147" marker-end="url(#p12-f3-head)"/>
<rect class="vx-box" x="300" y="218" width="100" height="30" rx="3"/><text class="vx-mono" x="350" y="238" text-anchor="middle">c = 0 + t1</text>
<rect class="vx-box-bad" x="440" y="258" width="100" height="30" rx="3"/><text class="vx-mono" x="490" y="278" text-anchor="middle">c + t2</text>
<path class="vx-line" d="M335 204 L340 217" marker-end="url(#p12-f3-head)"/>
<path class="vx-line" d="M455 204 L480 257" marker-end="url(#p12-f3-head)"/>
<path class="vx-line" d="M380 248 L450 257" marker-end="url(#p12-f3-head)"/>
<text class="vx-text-muted" x="560" y="258">p2 + p3 is rounded</text>
<text class="vx-text-muted" x="560" y="278">before it meets p0 + p1</text>
</svg>
<figcaption>Figure 3. The same four products for one element of <code>c</code>, split into two panels. On the left, each panel continues the one running sum, so the additions happen in the naive loop's order. On the right, each panel sums from zero and the highlighted addition joins the two panel totals, a different grouping that can round differently.</figcaption>
</figure>

The fourth example runs both kernels on a 4 by 4 tile with `k = 8` split into two panels of `kc = 4`, using values whose sums round, and compares each against the naive loop. Its `.toml` adds `-ffp-contract=off`, so that no multiply and add are fused, as decision 56 requires:

--8<-- "includes/examples/optimize/p12-fast-gemm/micro_kernel.cpp.md"

The C-initialized kernel matches in all 16 elements, as it must: [P8](p8-cache-blocking.md#keeping-the-order-keeping-the-bits) showed that tiling `k` keeps the bits when the `k` tiles run in order and `c` is accumulated in place, and a C-initialized micro-kernel is exactly that. The zero-initialized kernel differs in 3 of the 16 elements. With only one panel (`k ≤ kc`) the two forms agree, since adding the one panel total to a `c` that starts at zero is exact.

Two details make the C-initialized form work. The kernel now reads `c` before writing it, while the stage 10 kernel only ever wrote `c = sum`, so the generated code must set `c` to zero first, as P8's tiled kernel does; the naive loop's `sum` also starts at `0.0`, so the first addition matches. And the BLIS documentation warns that when `beta` is zero, a kernel must not read `c` at all, because `c` may hold uninitialized memory, including NaN or infinity.[^blis-k] A zero-initialized kernel can skip that read; a C-initialized one depends on it.

??? check "A kernel has `k = 512` and `kc = 256`, so `pc` runs twice for every tile of `c`. Which of the two micro-kernel forms can a Vortex compiler emit without an opt-in, and why does the other one need one?"

    The C-initialized form: each `c` element still receives its 512 products one at a time, in increasing `k`, into one running value that the second call picks up where the first left it. The zero-initialized form computes two 256-product sums separately and then adds them, a regrouping of the same additions. Floating-point addition is not associative, so the result can differ from the naive loop's, and decision 56 forbids reassociation without an explicit opt-in.

## Keeping the accumulators in registers

The micro-kernel's accumulators are meant to live in registers for a whole call. In IR they often start out as something else: a small local array, `alloca [mr x nr x float]`, indexed by the tile's row and column. [O7](o7-inlining-and-sroa.md#aggregates-in-memory) named what removes it. **Scalar replacement of aggregates** (SROA) splits a local slot into one value per piece its uses touch, but only when every use is at a **constant offset**; a variable index could touch any element, so SROA leaves such an array alone.

That is why the loops over the tile are **fully unrolled**, copied once per iteration until no loop remains: every access to the accumulator array then has a constant index. LLVM's pass pipeline runs SROA again right after its full-unroll pass, with a comment that says it is there to delete small arrays after unrolling.[^llvm-pipelines] The fifth example is one row of four accumulators, already unrolled over two steps of `k`, so every index into `acc` is a literal:

--8<-- "includes/examples/optimize/p12-fast-gemm/accumulator_sroa.ll.md"

After `opt -passes=sroa`, the `alloca` and every load and store through it are gone. What remains is four independent chains of `fmul` and `fadd`, one per accumulator, each carrying its running value from one step of `k` to the next. That is what "the accumulators live in registers" means in IR: there is no memory operation left, only SSA values for [C4](../backend/c4-graph-coloring.md) to assign to registers. Notice also that each chain keeps its `fadd` in the order the source wrote it; SROA moves no arithmetic.

??? check "The micro-kernel example has `mr = nr = 4`, sixteen accumulators. If the loops over `i` and `j` inside it were left as loops, indexed by their loop variables, would SROA still remove the accumulator array?"

    No. A loop variable is not a compile-time constant, so every access could touch any element of the array; SROA treats the whole array as one piece it cannot split and leaves it in memory. Each update then becomes a load, an add and a store, and the register block's advantage is lost.

## Choosing mc, kc, nc, mr and nr

Each of the five parameters is bounded by a fact about the machine. The rules below come from Goto and van de Geijn's paper and from Smith and colleagues; the right-hand column is for the reader's own machine.

| Parameter | What bounds it | Rule from the literature | Fact to query | Your value |
| --- | --- | --- | --- | --- |
| `mr`, `nr` | Registers, and FMA latency | About half the registers for the `c` tile, `mr` about equal to `nr`[^goto08]; tile size at least `Nvec × L × Nfma`[^low16] | Number and width of vector registers; multiply, add and FMA latencies ([P5](p5-microarchitecture.md)) | |
| `kc` | L1 cache | `kc × nr` values under half of the L1, so loads of `Ã` and `c` do not evict `B̃`[^goto08] | `sysctl hw.perflevel0.l1dcachesize` | |
| `mc` | L2 cache and TLB reach | `Ã` about half of the smaller of the L2 and the memory the TLB can address[^goto08] | `sysctl hw.perflevel0.l2cachesize`, `hw.pagesize`; TLB entries from the vendor's documentation | |
| `nc` | L3 cache, or workspace | Keep `B̃` in the L3 if there is one; `jc` mainly limits the memory `B̃` needs[^smith14] | `sysctl -a`, searched for `l3` | |

Two remarks from Goto and van de Geijn make the rules concrete. In their experience the best `kc` made `kc` double-precision values fill half a page, and the set associativity and replacement policy of each cache further limit how much of it a block may occupy.[^goto08] The second point is P8's self-interference again: "fits in the cache" is necessary, not sufficient.

On the author's machine, `sysctl` reports the L1 size, the L2 size, the page size and the line size, but no TLB entry count and no L3. A real implementation has to take the TLB figure from the vendor or measure it, and has to decide what `nc` means without an L3.

These parameters do not have to come from search. Low, Igual, Smith and Quintana-Ortí derive all five from a model of the machine: its vector registers, its FMA latency and throughput, and the size, line size and associativity of each cache.[^low16] For every processor they studied, their `mr` and `nr` matched the values BLIS's developers had chosen by hand; their `kc` matched the experts' choice, and their `mc` was similar or identical on all but one processor.[^low16]

Low and colleagues did not report `nc` at all, because most of their processors had no L3 cache, which made `nc`, in their words, "for all practical purposes, redundant".[^low16] The M4 Pro is in the same position. [P15](p15-choosing-parameters.md) takes up the general question of models against search.

Whichever way the numbers are chosen, Vortex's philosophy sets one condition: a compiler may choose among semantically equivalent schedules by cost model, benchmark or auto-tuning, and "Auto-tuning must not change the observable meaning of a program" ([Performance philosophy](../philosophy.md#performance-philosophy)). The accumulation form decides whether this condition holds. With a C-initialized micro-kernel every choice of the five parameters gives the naive loop's bits, so any of them may be picked. With a zero-initialized one, `kc` decides where the panel sums are joined, so a tuner that changes `kc` changes the printed answer.

??? check "A tuner tries `kc = 128` and `kc = 256` for the same kernel and keeps the faster. Under which micro-kernel form is that allowed for Vortex, and what would the test in this chapter's exercise show under the other form?"

    It is allowed with the C-initialized form, because both values of `kc` produce the naive loop's bits, so the choice cannot change the output. With the zero-initialized form, `k` is split into panels at different places for the two values, the panel totals are joined in different groups, and the exact-equality test against the naive kernel can fail for one choice and pass for the other. The output would depend on a timing measurement.

## What one measured climb looked like

None of the numbers so far are timings. For the shape of a real climb, here is one published sequence. Simon Boehm measured 1024 by 1024 `f32` matrix multiplication on a quad-core Intel i7-6700, in milliseconds:[^boehm22]

| Step | Time (ms) |
| --- | --- |
| Naive loop | 4481 |
| Compiler flags: `-O3`, `-march=native`, `-ffast-math` | 1621 |
| Plus accumulating in a register | 1512 |
| Cache-aware loop order | 89 |
| Plus tiling for the L1 | 70 |
| Plus OpenMP, 8 threads | 16 |
| NumPy on the same machine | 8 |

Three cautions belong with this table. The flags step includes `-ffast-math`, which lets the compiler reassociate floating-point operations; Vortex's decision 56 forbids that by default. Boehm's steps stop before packing and a micro-kernel, which is where this chapter begins. And one machine's numbers are no promise for another: Boehm reports the same NumPy code taking 1 ms on an M1 Pro with Apple's Accelerate and about 8 ms with OpenBLAS, and attributes the difference to Apple's undocumented matrix instructions, not to anything in this chapter.[^boehm22]

A second write-up, Salykov's, builds an AVX2 and FMA `sgemm` with the same packing and micro-kernel structure and benchmarks it against OpenBLAS 0.3.26 on an AMD Ryzen 7 9700X, with results for an Intel Core Ultra 265 as well.[^salykova25] One detail is worth keeping: theory favours `mr` equal to `nr`, but on his processor a 16 by 6 kernel measured fastest. Models give a starting point; the machine has the last word.

[P16](p16-capstone.md) asks for the equivalent table on the reader's own machine, measured under [P1](p1-measure-first.md)'s protocol. For this chapter, the rows that matter are these:

| Kernel, 1024 by 1024 `f32` | Time | GFLOP/s | Bits equal to naive? |
| --- | --- | --- | --- |
| Naive, stage 10 order, no contraction | | | yes, by definition |
| Packed, C-initialized micro-kernel, no contraction | | | |
| Same, with FMA contraction allowed | | | |
| `cblas_sgemm` from Accelerate | | | |

The gap between the second and third rows is the price of decision 56 on your machine. The gap between the third and fourth is what remains for [P13](p13-multithreading.md), better parameters and the matrix hardware of [G11](../gpu/g11-matrix-units.md).

## For Vortex

!!! vortex "Exercise"

    Make your compiler emit a packed, register-blocked version of the [stage 10](../compiler/guide/stage-10-matrix-multiplication.md) matrix multiplication for shapes known at compile time, keeping the naive loop's exact result. If your compiler cannot yet emit loops over temporary buffers, write the design down first: the loops it will emit, the buffers it will allocate, and the remark it will print.

    **Build now.**

    - Packing loops for `a` and `b` that copy an `mc` by `kc` block and a `kc` by `nc` panel into contiguous buffers in micro-panel order, zero-padded to whole micro-panels.
    - The five loops, `jc`, `pc`, `ic`, `jr`, `ir`, with `pc` allowed to run more than once.
    - An `mr` by `nr` micro-kernel whose accumulators are separate SSA values, either by construction or through full unrolling followed by SROA. It loads its accumulators from `c` at the start of each call, adds the panel's products in increasing `k`, and stores only the elements that exist. `c` is set to zero before the loops begin.
    - Separate multiply and add instructions, never a fused multiply-add ([decision 56](../decisions/numbers.md#d56)).
    - One remark per kernel, in the stream from [O1](o1-optimizer-contract.md#remarks-the-optimizers-report): the five parameters, and for each one whether it came from a model (name the machine fact it used) or from a fixed default, plus the accumulation form.

    **Not yet.**

    - Zero-initialized panels, split-K reductions and fused multiply-adds. All three change the bits, so each needs an explicit opt-in that decision 56 has not defined yet.
    - Choosing the parameters by search ([P15](p15-choosing-parameters.md)) and running the outer loops on several threads ([P13](p13-multithreading.md)).
    - Shapes known only at run time, and matrix instructions such as SME.

    **Proof that it works.**

    - Exact equality (`==`, not a tolerance) with the unoptimized build on at least: the 64 by 64 kernel from P8; a rectangular shape in which none of `m`, `n` and `k` is a multiple of `mr`, `nr` or `kc`; and a shape with `k` at least three times `kc`, so that `pc` runs three or more times and its last panel is partial. Use values whose sums round, such as reciprocals of small integers.
    - The same test with an infinity in the last row of `b`, in a shape where the last micro-panel of `Ã` is padded. The output, including its infinities and NaNs, must match the naive build.
    - A mutation check: switch the micro-kernel to the zero-initialized form in a test build and confirm that the exact-equality test fails on the multi-panel shape. A test that still passes is not testing the order of additions.
    - A golden remark file for the stage 10 program, and a filled-in table:

    | Shape | `mc` | `kc` | `nc` | `mr` | `nr` | Source of each | Naive time | Packed time | Bits equal |
    | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
    | 64 by 64 | | | | | | | | | |
    | your rectangular shape | | | | | | | | | |

    Record the date, your compiler's version and the machine, following [P1](p1-measure-first.md).

## Key ideas

!!! recap "Questions you can now answer"

    - **What does packing fix that tiling alone does not?** Tiling bounds how much data is reused; packing puts that data in one contiguous buffer, in the micro-kernel's read order, so it uses whole cache lines and few pages.
    - **What are the five loops, and which two pack?** `jc`, `pc`, `ic`, `jr`, `ir`, from the outside in; `pc` packs a panel of `b` into `B̃`, `ic` packs a block of `a` into `Ã`.
    - **How many times is each element of `a` and of `b` copied?** Each element of `b` once; each element of `a` once per `jc` iteration, about `n / nc` times, plus padding at the edges.
    - **Which micro-kernel form keeps the naive loop's bits when `k` spans several panels?** The C-initialized one, which continues each `c` element's running sum from panel to panel; summing each panel from zero regroups the additions.
    - **What lets SROA turn the accumulator array into registers?** Full unrolling of the loops over the tile, which makes every index into the array a constant.
    - **What bounds each of `mr`, `nr`, `kc`, `mc` and `nc`?** Registers and FMA latency; the L1; the L2 and the TLB's reach; the L3 or the workspace for `B̃`.
    - **Why may a Vortex tuner change `kc` only with a C-initialized kernel?** Because only then does every value of `kc` produce the same bits, so tuning cannot change the program's meaning.

## Where this comes back

!!! next "You will use this again in"

    - [P13. Multithreading](p13-multithreading.md): *the five loops*, *which of them can run in parallel without a reduction*
    - [P15. Choosing parameters: models or search](p15-choosing-parameters.md): *mc, kc, nc, mr, nr*, *analytical model*
    - [P16. Capstone: the ladder, measured](p16-capstone.md): *packing*, *micro-kernel*, *the price of decision 56*
    - [C4. Register allocation II: graphs and SSA](../backend/c4-graph-coloring.md): *accumulators as SSA values*
    - [G11. Matrix units](../gpu/g11-matrix-units.md): *outer product*, *rank-1 update*, *packing*

## Sources and further reading

For a step-by-step build of a small kernel, start with the *How To Optimize GEMM* wiki[^htog], then BLISlab[^blislab], which turns the BLIS design into exercises, and the free course *LAFF-On Programming for High Performance*[^laff]. Goto and van de Geijn's paper explains why packing and TLB-aware block sizes exist; the BLIS papers and the KernelsHowTo document define the five loops and the micro-kernel's contract; Low and colleagues show the parameters can come from a model.

[^goto08]: Kazushige Goto and Robert A. van de Geijn, "Anatomy of High-Performance Matrix Multiplication", *ACM Transactions on Mathematical Software* 34(3), 2008: TLB considerations and packing (section 4.2), register blocking and the choice of `mr`, `nr`, `kc` and `mc` (section 6), packing in C and the kernel in assembly (section 7.1). <https://doi.org/10.1145/1356052.1356053> (authors' copy: <https://www.cs.utexas.edu/~flame/pubs/GotoTOMS_revision.pdf>)
[^blis15]: Field G. Van Zee and Robert A. van de Geijn, "BLIS: A Framework for Rapidly Instantiating BLAS Functionality", *ACM Transactions on Mathematical Software* 41(3), 2015: the micro-kernel and macro-kernel, and edge cases handled by zero-padded packing (section 5.1). <https://doi.org/10.1145/2764454> (authors' copy: <https://www.cs.utexas.edu/~flame/pubs/blis1_toms_rev3.pdf>)
[^smith14]: Tyler M. Smith, Robert van de Geijn, Mikhail Smelyanskiy, Jeff R. Hammond and Field G. Van Zee, "Anatomy of High-Performance Many-Threaded Matrix Multiplication", *IEEE 28th International Parallel and Distributed Processing Symposium*, 2014: the five loops, where each block is meant to reside (Figures 1 and 2), and the micro-kernel as rank-1 updates. <https://doi.org/10.1109/IPDPS.2014.110> (authors' copy: <https://www.cs.utexas.edu/~flame/pubs/blis3_ipdps14.pdf>)
[^blis-k]: BLIS Project, "KernelsHowTo", read on 2026-09-24: the `gemm` micro-kernel contract, the packed micro-panel layouts, the implementation notes on unrolling, edge cases and zero `beta`. <https://github.com/flame/blis/blob/master/docs/KernelsHowTo.md>
[^low16]: Tze Meng Low, Francisco D. Igual, Tyler M. Smith and Enrique S. Quintana-Ortí, "Analytical Modeling Is Enough for High-Performance BLIS", *ACM Transactions on Mathematical Software* 43(2), 2016: the architecture model (section 4.1), the latency bound on `mr × nr` (section 4.2), and the comparison with expert-chosen values (section 5). <https://doi.org/10.1145/2925987> (authors' copy: <https://www.cs.utexas.edu/~flame/pubs/TOMS-BLIS-Analytical.pdf>)
[^htog]: The FLAME Project, "How To Optimize GEMM" wiki, read on 2026-09-24: a step-by-step build of a small GEMM kernel. <https://github.com/flame/how-to-optimize-gemm/wiki>
[^laff]: Robert van de Geijn, Margaret Myers and Devangi Parikh, *LAFF-On Programming for High Performance*, read on 2026-09-24: a free course on loops, micro-kernels, caches and OpenMP. <https://www.cs.utexas.edu/~flame/laff/pfhp/>
[^blislab]: Jianyu Huang and Robert A. van de Geijn, "BLISlab: A Sandbox for Optimizing GEMM", arXiv:1609.00076, 2016. <https://arxiv.org/abs/1609.00076>
[^boehm22]: Simon Boehm, "Fast Multidimensional Matrix Multiplication on CPU from Scratch", August 2022: the i7-6700 measurements and the M1 Pro comparison. <https://siboehm.com/articles/22/Fast-MMM-on-CPU>
[^salykova25]: Amanzhol Salykov, "Advanced Matrix Multiplication Optimization on Modern Multi-Core Processors", January 2025: an AVX2 and FMA `sgemm` benchmarked against OpenBLAS, and the 16 by 6 kernel. <https://salykova.github.io/matmul-cpu>
[^llvm-pipelines]: LLVM Project, `PassBuilderPipelines.cpp`, release/18.x branch: SROA scheduled after `LoopFullUnrollPass`, with the comment "Delete small array after loop unroll." <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Passes/PassBuilderPipelines.cpp>
