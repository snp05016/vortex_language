# G11. Matrix units

<p class="page-intro">A matrix unit is a piece of a GPU core that multiplies and accumulates one small, fixed-size tile of numbers in a single instruction, issued cooperatively by a whole warp. This chapter explains what that instruction actually does, what it deliberately does not tell the programmer, and why a compiler that wants to use it has to treat precision as a decision rather than a detail.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 30 minutes · Builds on: [G10. The GPU matmul ladder](g10-matmul-ladder.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is a warp, and how does one instruction serve all of its lanes?"

        A warp is a group of threads, 32 on current NVIDIA GPUs, that execute one instruction together; each thread is a **lane**. One load instruction carries one address per lane, and the memory system serves the whole group's addresses at once.

        Introduced in [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md).

    ??? question "What limits how fast a GPU finishes independent operations, once enough of them are in flight?"

        Its peak issue rate: how many instructions the hardware can issue per cycle. That is a fact about the chip, not about how much work is queued up waiting for it.

        Introduced in [G1. Throughput machines](g1-throughput-machines.md).

    ??? question "In what order are the elements of a `[f32; 64, 64]` array stored?"

        Row after row, the last index varying fastest, with a size and layout fixed by the array's type.

        Introduced in [Arrays and shapes, decision 43](../decisions/arrays.md#d43).

    ??? question "May a Vortex compiler reorder, fuse or reduce the precision of a floating-point operation on its own?"

        No. Each `f32` or `f64` operation is one IEEE 754 operation, rounded once, to nearest even, and a conforming implementation must not contract, reassociate, widen or flush it, unless a later, explicit mode allows it.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Explain why a matrix unit executes one small, fixed-size tile per instruction instead of one scalar multiply-add.
    - Describe what a fragment's API actually promises, and what it deliberately leaves unspecified.
    - Trace one line of instructions from CUDA's tensor-core intrinsics down through PTX to the newer asynchronous and warpgroup forms, and name Apple's and AMD's equivalents.
    - Recognize why a matrix unit's reduced-precision inputs change a program's answer, and why that makes them an opt-in for Vortex rather than a free optimization.
    - Judge what a compiler has to decide before it can target a matrix unit at all, separately from how it eventually does.

## One instruction, one tile

Take two tiny matrices, 2 rows and 2 columns each:

```text
A = | 2  0 |        B = | 1  3 |        C = | 1  1 |
    | 1  1 |            | 2  0 |            | 0  1 |
```

Computing `D = A * B + C` by hand means four dot products, one per cell of
`D`, each a pair of multiplies and an add, plus the matching cell of `C`:
eight multiplies, six adds, sixteen scalar instructions on an ordinary core.
`D[0, 0]` alone is `2 * 1 + 0 * 2 + 1 = 3`.

A **matrix unit** is a piece of hardware that computes the whole of `D = A *
B + C` for one small, fixed tile shape in a single instruction. Not two
matrices of whatever size the programmer wrote: exactly the shape the
instruction supports, no more and no fewer rows or columns, the same way a
scalar `fma` instruction always takes exactly three numbers. NVIDIA's WMMA
API, introduced with Volta in CUDA 9, exposes tiles as small as 16 rows by 16
columns by 16 of reduction depth;[^tensor-cores-blog] Apple's SIMD-group
matrix functions work over 8 by 8 tiles.[^msl-spec] The instruction does not
loop and it does not vary its shape at runtime. Whatever matrix the program
actually wants, a compiler has to break it into tiles of that one fixed
shape and issue one matrix-unit instruction per tile, chaining tiles along
the reduction dimension by feeding each instruction's output tile in as the
next one's `C`.

That chaining is the whole idea, and it scales the same way scalar
tiling does: an `N` by `N` by `N` matrix multiply, tiled into `T` by `T` by
`T` pieces along every axis, takes `(N / T)^3` matrix-unit instructions, and
every one of them stands in for `T^3` scalar multiply-adds. The total
arithmetic is unchanged. What shrinks is the number of instructions the
hardware has to fetch, decode and issue to get through it, at the tile size
its unit was built for.

--8<-- "includes/examples/gpu/g11-matrix-units/tile_ops.cpp.md"

At `T = 16`, a 64 by 64 by 64 multiply that would be 262,144 scalar
multiply-adds becomes 64 tile instructions; each one still accounts for
exactly 4,096 of them, so the two counts the program prints always agree.
Growing `T` shrinks the instruction count further, at the cost of a bigger
fixed tile the rest of the program has to fit its data into. That trade,
picking or discovering the tile shape a target's matrix unit actually
supports, is the compiler decision this chapter is building toward; nothing
here says how to make it.

??? check "A matrix unit's tile is 16 x 16 x 16, and a matmul is 128 x 128 x 128. How many tile instructions does it take, and how many scalar multiply-adds does each one stand in for?"

    `(128 / 16)^3 = 512` tile instructions, each covering `16^3 = 4,096`
    scalar multiply-adds; `512 * 4,096 = 2,097,152`, which is `128^3`, so the
    total work checks out exactly as it did for the 64 by 64 by 64 case above.

## A warp computes the tile together, but no lane sees the whole thing

A matrix unit's instruction is not private to one thread. On NVIDIA GPUs it
is a **warp-level** instruction: all 32 lanes of a warp issue it together,
and together they hold the tile's data, each lane owning a small, fixed
piece of it. Apple's equivalent is scoped to a **SIMD-group**, the same idea
under Metal's name for a warp.[^msl-spec] That piece is called a
**fragment**: an opaque handle to the lane's share of a tile, produced by a
load function and consumed by a store function, with no member a program can
read or assign directly.

The fragment's API is deliberately quiet about one thing: which lane holds
which element of the tile. The Metal Shading Language specification states
that the mapping from a SIMD-group matrix's elements to the SIMD-group's
lanes is unspecified,[^msl-spec] and CUDA's warp matrix functions document
only the load and store operations, never a layout a kernel is allowed to
depend on.[^wmma] The only promise either API makes is: load a tile into a
fragment, do only fragment operations with it, then store it, and the tile
that comes out is the tile that went in.

That promise is checkable without touching real hardware. Build two
different maps from an 8 by 8 tile's 64 cells onto 32 lanes with 2 slots
each, one that walks the tile row by row and one that walks it column by
column, and confirm each is a bijection: nothing collides, nothing is
dropped, and reading every `(lane, slot)` back reproduces the tile.

--8<-- "includes/examples/gpu/g11-matrix-units/fragment_mapping.cpp.md"

Both mappings pass. They also disagree with each other about almost every
cell, including the one this chapter picked to print.

<figure class="vx-figure">
<svg viewBox="0 0 620 310" role="img" aria-label="Two different, equally legal lane mappings for the same 8 by 8 fragment" aria-describedby="g11-f1-desc">
<title id="g11-f1-title">Two different, equally legal lane mappings for the same 8 by 8 fragment</title>
<desc id="g11-f1-desc">Two 8 by 8 grids of cells, each cell holding a lane number from 0 to 31 with each lane owning two cells. In the left grid, layout A, lanes run in contiguous pairs along each row: row 0 holds lanes 0, 0, 1, 1, 2, 2, 3, 3. In the right grid, layout B, lanes run in contiguous pairs down each column instead: column 0 holds lanes 0, 0, 1, 1, 2, 2, 3, 3 read downward. The cell at row 3, column 5 is highlighted in both grids: layout A gives it to lane 14, layout B gives it to lane 21. Both are legal fragments of the same tile; they disagree about which lane holds this cell and almost every other one.</desc>
<text class="vx-text" x="20" y="24">Same 8 x 8 tile, two fragment layouts (fragment_mapping.cpp)</text>
<text class="vx-text-muted" x="40" y="52">layout A: row-major</text>
<text class="vx-text-muted" x="368" y="52">layout B: column-major</text>
<rect class="vx-box" x="40" y="70" width="26" height="26"/>
<rect class="vx-box" x="66" y="70" width="26" height="26"/>
<rect class="vx-box" x="92" y="70" width="26" height="26"/>
<rect class="vx-box" x="118" y="70" width="26" height="26"/>
<rect class="vx-box" x="144" y="70" width="26" height="26"/>
<rect class="vx-box" x="170" y="70" width="26" height="26"/>
<rect class="vx-box" x="196" y="70" width="26" height="26"/>
<rect class="vx-box" x="222" y="70" width="26" height="26"/>
<rect class="vx-box" x="40" y="96" width="26" height="26"/>
<rect class="vx-box" x="66" y="96" width="26" height="26"/>
<rect class="vx-box" x="92" y="96" width="26" height="26"/>
<rect class="vx-box" x="118" y="96" width="26" height="26"/>
<rect class="vx-box" x="144" y="96" width="26" height="26"/>
<rect class="vx-box" x="170" y="96" width="26" height="26"/>
<rect class="vx-box" x="196" y="96" width="26" height="26"/>
<rect class="vx-box" x="222" y="96" width="26" height="26"/>
<rect class="vx-box" x="40" y="122" width="26" height="26"/>
<rect class="vx-box" x="66" y="122" width="26" height="26"/>
<rect class="vx-box" x="92" y="122" width="26" height="26"/>
<rect class="vx-box" x="118" y="122" width="26" height="26"/>
<rect class="vx-box" x="144" y="122" width="26" height="26"/>
<rect class="vx-box" x="170" y="122" width="26" height="26"/>
<rect class="vx-box" x="196" y="122" width="26" height="26"/>
<rect class="vx-box" x="222" y="122" width="26" height="26"/>
<rect class="vx-box" x="40" y="148" width="26" height="26"/>
<rect class="vx-box" x="66" y="148" width="26" height="26"/>
<rect class="vx-box" x="92" y="148" width="26" height="26"/>
<rect class="vx-box" x="118" y="148" width="26" height="26"/>
<rect class="vx-box" x="144" y="148" width="26" height="26"/>
<rect class="vx-box-accent" x="170" y="148" width="26" height="26"/>
<rect class="vx-box" x="196" y="148" width="26" height="26"/>
<rect class="vx-box" x="222" y="148" width="26" height="26"/>
<rect class="vx-box" x="40" y="174" width="26" height="26"/>
<rect class="vx-box" x="66" y="174" width="26" height="26"/>
<rect class="vx-box" x="92" y="174" width="26" height="26"/>
<rect class="vx-box" x="118" y="174" width="26" height="26"/>
<rect class="vx-box" x="144" y="174" width="26" height="26"/>
<rect class="vx-box" x="170" y="174" width="26" height="26"/>
<rect class="vx-box" x="196" y="174" width="26" height="26"/>
<rect class="vx-box" x="222" y="174" width="26" height="26"/>
<rect class="vx-box" x="40" y="200" width="26" height="26"/>
<rect class="vx-box" x="66" y="200" width="26" height="26"/>
<rect class="vx-box" x="92" y="200" width="26" height="26"/>
<rect class="vx-box" x="118" y="200" width="26" height="26"/>
<rect class="vx-box" x="144" y="200" width="26" height="26"/>
<rect class="vx-box" x="170" y="200" width="26" height="26"/>
<rect class="vx-box" x="196" y="200" width="26" height="26"/>
<rect class="vx-box" x="222" y="200" width="26" height="26"/>
<rect class="vx-box" x="40" y="226" width="26" height="26"/>
<rect class="vx-box" x="66" y="226" width="26" height="26"/>
<rect class="vx-box" x="92" y="226" width="26" height="26"/>
<rect class="vx-box" x="118" y="226" width="26" height="26"/>
<rect class="vx-box" x="144" y="226" width="26" height="26"/>
<rect class="vx-box" x="170" y="226" width="26" height="26"/>
<rect class="vx-box" x="196" y="226" width="26" height="26"/>
<rect class="vx-box" x="222" y="226" width="26" height="26"/>
<rect class="vx-box" x="40" y="252" width="26" height="26"/>
<rect class="vx-box" x="66" y="252" width="26" height="26"/>
<rect class="vx-box" x="92" y="252" width="26" height="26"/>
<rect class="vx-box" x="118" y="252" width="26" height="26"/>
<rect class="vx-box" x="144" y="252" width="26" height="26"/>
<rect class="vx-box" x="170" y="252" width="26" height="26"/>
<rect class="vx-box" x="196" y="252" width="26" height="26"/>
<rect class="vx-box" x="222" y="252" width="26" height="26"/>
<text class="vx-mono" x="53" y="87" text-anchor="middle" font-size="10">0</text>
<text class="vx-mono" x="79" y="87" text-anchor="middle" font-size="10">0</text>
<text class="vx-mono" x="105" y="87" text-anchor="middle" font-size="10">1</text>
<text class="vx-mono" x="131" y="87" text-anchor="middle" font-size="10">1</text>
<text class="vx-mono" x="157" y="87" text-anchor="middle" font-size="10">2</text>
<text class="vx-mono" x="183" y="87" text-anchor="middle" font-size="10">2</text>
<text class="vx-mono" x="209" y="87" text-anchor="middle" font-size="10">3</text>
<text class="vx-mono" x="235" y="87" text-anchor="middle" font-size="10">3</text>
<text class="vx-mono" x="53" y="113" text-anchor="middle" font-size="10">4</text>
<text class="vx-mono" x="79" y="113" text-anchor="middle" font-size="10">4</text>
<text class="vx-mono" x="105" y="113" text-anchor="middle" font-size="10">5</text>
<text class="vx-mono" x="131" y="113" text-anchor="middle" font-size="10">5</text>
<text class="vx-mono" x="157" y="113" text-anchor="middle" font-size="10">6</text>
<text class="vx-mono" x="183" y="113" text-anchor="middle" font-size="10">6</text>
<text class="vx-mono" x="209" y="113" text-anchor="middle" font-size="10">7</text>
<text class="vx-mono" x="235" y="113" text-anchor="middle" font-size="10">7</text>
<text class="vx-mono" x="53" y="139" text-anchor="middle" font-size="10">8</text>
<text class="vx-mono" x="79" y="139" text-anchor="middle" font-size="10">8</text>
<text class="vx-mono" x="105" y="139" text-anchor="middle" font-size="10">9</text>
<text class="vx-mono" x="131" y="139" text-anchor="middle" font-size="10">9</text>
<text class="vx-mono" x="157" y="139" text-anchor="middle" font-size="10">10</text>
<text class="vx-mono" x="183" y="139" text-anchor="middle" font-size="10">10</text>
<text class="vx-mono" x="209" y="139" text-anchor="middle" font-size="10">11</text>
<text class="vx-mono" x="235" y="139" text-anchor="middle" font-size="10">11</text>
<text class="vx-mono" x="53" y="165" text-anchor="middle" font-size="10">12</text>
<text class="vx-mono" x="79" y="165" text-anchor="middle" font-size="10">12</text>
<text class="vx-mono" x="105" y="165" text-anchor="middle" font-size="10">13</text>
<text class="vx-mono" x="131" y="165" text-anchor="middle" font-size="10">13</text>
<text class="vx-mono" x="157" y="165" text-anchor="middle" font-size="10">14</text>
<text class="vx-mono" x="183" y="165" text-anchor="middle" font-size="10">14</text>
<text class="vx-mono" x="209" y="165" text-anchor="middle" font-size="10">15</text>
<text class="vx-mono" x="235" y="165" text-anchor="middle" font-size="10">15</text>
<text class="vx-mono" x="53" y="191" text-anchor="middle" font-size="10">16</text>
<text class="vx-mono" x="79" y="191" text-anchor="middle" font-size="10">16</text>
<text class="vx-mono" x="105" y="191" text-anchor="middle" font-size="10">17</text>
<text class="vx-mono" x="131" y="191" text-anchor="middle" font-size="10">17</text>
<text class="vx-mono" x="157" y="191" text-anchor="middle" font-size="10">18</text>
<text class="vx-mono" x="183" y="191" text-anchor="middle" font-size="10">18</text>
<text class="vx-mono" x="209" y="191" text-anchor="middle" font-size="10">19</text>
<text class="vx-mono" x="235" y="191" text-anchor="middle" font-size="10">19</text>
<text class="vx-mono" x="53" y="217" text-anchor="middle" font-size="10">20</text>
<text class="vx-mono" x="79" y="217" text-anchor="middle" font-size="10">20</text>
<text class="vx-mono" x="105" y="217" text-anchor="middle" font-size="10">21</text>
<text class="vx-mono" x="131" y="217" text-anchor="middle" font-size="10">21</text>
<text class="vx-mono" x="157" y="217" text-anchor="middle" font-size="10">22</text>
<text class="vx-mono" x="183" y="217" text-anchor="middle" font-size="10">22</text>
<text class="vx-mono" x="209" y="217" text-anchor="middle" font-size="10">23</text>
<text class="vx-mono" x="235" y="217" text-anchor="middle" font-size="10">23</text>
<text class="vx-mono" x="53" y="243" text-anchor="middle" font-size="10">24</text>
<text class="vx-mono" x="79" y="243" text-anchor="middle" font-size="10">24</text>
<text class="vx-mono" x="105" y="243" text-anchor="middle" font-size="10">25</text>
<text class="vx-mono" x="131" y="243" text-anchor="middle" font-size="10">25</text>
<text class="vx-mono" x="157" y="243" text-anchor="middle" font-size="10">26</text>
<text class="vx-mono" x="183" y="243" text-anchor="middle" font-size="10">26</text>
<text class="vx-mono" x="209" y="243" text-anchor="middle" font-size="10">27</text>
<text class="vx-mono" x="235" y="243" text-anchor="middle" font-size="10">27</text>
<text class="vx-mono" x="53" y="269" text-anchor="middle" font-size="10">28</text>
<text class="vx-mono" x="79" y="269" text-anchor="middle" font-size="10">28</text>
<text class="vx-mono" x="105" y="269" text-anchor="middle" font-size="10">29</text>
<text class="vx-mono" x="131" y="269" text-anchor="middle" font-size="10">29</text>
<text class="vx-mono" x="157" y="269" text-anchor="middle" font-size="10">30</text>
<text class="vx-mono" x="183" y="269" text-anchor="middle" font-size="10">30</text>
<text class="vx-mono" x="209" y="269" text-anchor="middle" font-size="10">31</text>
<text class="vx-mono" x="235" y="269" text-anchor="middle" font-size="10">31</text>
<rect class="vx-box" x="368" y="70" width="26" height="26"/>
<rect class="vx-box" x="394" y="70" width="26" height="26"/>
<rect class="vx-box" x="420" y="70" width="26" height="26"/>
<rect class="vx-box" x="446" y="70" width="26" height="26"/>
<rect class="vx-box" x="472" y="70" width="26" height="26"/>
<rect class="vx-box" x="498" y="70" width="26" height="26"/>
<rect class="vx-box" x="524" y="70" width="26" height="26"/>
<rect class="vx-box" x="550" y="70" width="26" height="26"/>
<rect class="vx-box" x="368" y="96" width="26" height="26"/>
<rect class="vx-box" x="394" y="96" width="26" height="26"/>
<rect class="vx-box" x="420" y="96" width="26" height="26"/>
<rect class="vx-box" x="446" y="96" width="26" height="26"/>
<rect class="vx-box" x="472" y="96" width="26" height="26"/>
<rect class="vx-box" x="498" y="96" width="26" height="26"/>
<rect class="vx-box" x="524" y="96" width="26" height="26"/>
<rect class="vx-box" x="550" y="96" width="26" height="26"/>
<rect class="vx-box" x="368" y="122" width="26" height="26"/>
<rect class="vx-box" x="394" y="122" width="26" height="26"/>
<rect class="vx-box" x="420" y="122" width="26" height="26"/>
<rect class="vx-box" x="446" y="122" width="26" height="26"/>
<rect class="vx-box" x="472" y="122" width="26" height="26"/>
<rect class="vx-box" x="498" y="122" width="26" height="26"/>
<rect class="vx-box" x="524" y="122" width="26" height="26"/>
<rect class="vx-box" x="550" y="122" width="26" height="26"/>
<rect class="vx-box" x="368" y="148" width="26" height="26"/>
<rect class="vx-box" x="394" y="148" width="26" height="26"/>
<rect class="vx-box" x="420" y="148" width="26" height="26"/>
<rect class="vx-box" x="446" y="148" width="26" height="26"/>
<rect class="vx-box" x="472" y="148" width="26" height="26"/>
<rect class="vx-box-accent" x="498" y="148" width="26" height="26"/>
<rect class="vx-box" x="524" y="148" width="26" height="26"/>
<rect class="vx-box" x="550" y="148" width="26" height="26"/>
<rect class="vx-box" x="368" y="174" width="26" height="26"/>
<rect class="vx-box" x="394" y="174" width="26" height="26"/>
<rect class="vx-box" x="420" y="174" width="26" height="26"/>
<rect class="vx-box" x="446" y="174" width="26" height="26"/>
<rect class="vx-box" x="472" y="174" width="26" height="26"/>
<rect class="vx-box" x="498" y="174" width="26" height="26"/>
<rect class="vx-box" x="524" y="174" width="26" height="26"/>
<rect class="vx-box" x="550" y="174" width="26" height="26"/>
<rect class="vx-box" x="368" y="200" width="26" height="26"/>
<rect class="vx-box" x="394" y="200" width="26" height="26"/>
<rect class="vx-box" x="420" y="200" width="26" height="26"/>
<rect class="vx-box" x="446" y="200" width="26" height="26"/>
<rect class="vx-box" x="472" y="200" width="26" height="26"/>
<rect class="vx-box" x="498" y="200" width="26" height="26"/>
<rect class="vx-box" x="524" y="200" width="26" height="26"/>
<rect class="vx-box" x="550" y="200" width="26" height="26"/>
<rect class="vx-box" x="368" y="226" width="26" height="26"/>
<rect class="vx-box" x="394" y="226" width="26" height="26"/>
<rect class="vx-box" x="420" y="226" width="26" height="26"/>
<rect class="vx-box" x="446" y="226" width="26" height="26"/>
<rect class="vx-box" x="472" y="226" width="26" height="26"/>
<rect class="vx-box" x="498" y="226" width="26" height="26"/>
<rect class="vx-box" x="524" y="226" width="26" height="26"/>
<rect class="vx-box" x="550" y="226" width="26" height="26"/>
<rect class="vx-box" x="368" y="252" width="26" height="26"/>
<rect class="vx-box" x="394" y="252" width="26" height="26"/>
<rect class="vx-box" x="420" y="252" width="26" height="26"/>
<rect class="vx-box" x="446" y="252" width="26" height="26"/>
<rect class="vx-box" x="472" y="252" width="26" height="26"/>
<rect class="vx-box" x="498" y="252" width="26" height="26"/>
<rect class="vx-box" x="524" y="252" width="26" height="26"/>
<rect class="vx-box" x="550" y="252" width="26" height="26"/>
<text class="vx-mono" x="381" y="87" text-anchor="middle" font-size="10">0</text>
<text class="vx-mono" x="407" y="87" text-anchor="middle" font-size="10">4</text>
<text class="vx-mono" x="433" y="87" text-anchor="middle" font-size="10">8</text>
<text class="vx-mono" x="459" y="87" text-anchor="middle" font-size="10">12</text>
<text class="vx-mono" x="485" y="87" text-anchor="middle" font-size="10">16</text>
<text class="vx-mono" x="511" y="87" text-anchor="middle" font-size="10">20</text>
<text class="vx-mono" x="537" y="87" text-anchor="middle" font-size="10">24</text>
<text class="vx-mono" x="563" y="87" text-anchor="middle" font-size="10">28</text>
<text class="vx-mono" x="381" y="113" text-anchor="middle" font-size="10">0</text>
<text class="vx-mono" x="407" y="113" text-anchor="middle" font-size="10">4</text>
<text class="vx-mono" x="433" y="113" text-anchor="middle" font-size="10">8</text>
<text class="vx-mono" x="459" y="113" text-anchor="middle" font-size="10">12</text>
<text class="vx-mono" x="485" y="113" text-anchor="middle" font-size="10">16</text>
<text class="vx-mono" x="511" y="113" text-anchor="middle" font-size="10">20</text>
<text class="vx-mono" x="537" y="113" text-anchor="middle" font-size="10">24</text>
<text class="vx-mono" x="563" y="113" text-anchor="middle" font-size="10">28</text>
<text class="vx-mono" x="381" y="139" text-anchor="middle" font-size="10">1</text>
<text class="vx-mono" x="407" y="139" text-anchor="middle" font-size="10">5</text>
<text class="vx-mono" x="433" y="139" text-anchor="middle" font-size="10">9</text>
<text class="vx-mono" x="459" y="139" text-anchor="middle" font-size="10">13</text>
<text class="vx-mono" x="485" y="139" text-anchor="middle" font-size="10">17</text>
<text class="vx-mono" x="511" y="139" text-anchor="middle" font-size="10">21</text>
<text class="vx-mono" x="537" y="139" text-anchor="middle" font-size="10">25</text>
<text class="vx-mono" x="563" y="139" text-anchor="middle" font-size="10">29</text>
<text class="vx-mono" x="381" y="165" text-anchor="middle" font-size="10">1</text>
<text class="vx-mono" x="407" y="165" text-anchor="middle" font-size="10">5</text>
<text class="vx-mono" x="433" y="165" text-anchor="middle" font-size="10">9</text>
<text class="vx-mono" x="459" y="165" text-anchor="middle" font-size="10">13</text>
<text class="vx-mono" x="485" y="165" text-anchor="middle" font-size="10">17</text>
<text class="vx-mono" x="511" y="165" text-anchor="middle" font-size="10">21</text>
<text class="vx-mono" x="537" y="165" text-anchor="middle" font-size="10">25</text>
<text class="vx-mono" x="563" y="165" text-anchor="middle" font-size="10">29</text>
<text class="vx-mono" x="381" y="191" text-anchor="middle" font-size="10">2</text>
<text class="vx-mono" x="407" y="191" text-anchor="middle" font-size="10">6</text>
<text class="vx-mono" x="433" y="191" text-anchor="middle" font-size="10">10</text>
<text class="vx-mono" x="459" y="191" text-anchor="middle" font-size="10">14</text>
<text class="vx-mono" x="485" y="191" text-anchor="middle" font-size="10">18</text>
<text class="vx-mono" x="511" y="191" text-anchor="middle" font-size="10">22</text>
<text class="vx-mono" x="537" y="191" text-anchor="middle" font-size="10">26</text>
<text class="vx-mono" x="563" y="191" text-anchor="middle" font-size="10">30</text>
<text class="vx-mono" x="381" y="217" text-anchor="middle" font-size="10">2</text>
<text class="vx-mono" x="407" y="217" text-anchor="middle" font-size="10">6</text>
<text class="vx-mono" x="433" y="217" text-anchor="middle" font-size="10">10</text>
<text class="vx-mono" x="459" y="217" text-anchor="middle" font-size="10">14</text>
<text class="vx-mono" x="485" y="217" text-anchor="middle" font-size="10">18</text>
<text class="vx-mono" x="511" y="217" text-anchor="middle" font-size="10">22</text>
<text class="vx-mono" x="537" y="217" text-anchor="middle" font-size="10">26</text>
<text class="vx-mono" x="563" y="217" text-anchor="middle" font-size="10">30</text>
<text class="vx-mono" x="381" y="243" text-anchor="middle" font-size="10">3</text>
<text class="vx-mono" x="407" y="243" text-anchor="middle" font-size="10">7</text>
<text class="vx-mono" x="433" y="243" text-anchor="middle" font-size="10">11</text>
<text class="vx-mono" x="459" y="243" text-anchor="middle" font-size="10">15</text>
<text class="vx-mono" x="485" y="243" text-anchor="middle" font-size="10">19</text>
<text class="vx-mono" x="511" y="243" text-anchor="middle" font-size="10">23</text>
<text class="vx-mono" x="537" y="243" text-anchor="middle" font-size="10">27</text>
<text class="vx-mono" x="563" y="243" text-anchor="middle" font-size="10">31</text>
<text class="vx-mono" x="381" y="269" text-anchor="middle" font-size="10">3</text>
<text class="vx-mono" x="407" y="269" text-anchor="middle" font-size="10">7</text>
<text class="vx-mono" x="433" y="269" text-anchor="middle" font-size="10">11</text>
<text class="vx-mono" x="459" y="269" text-anchor="middle" font-size="10">15</text>
<text class="vx-mono" x="485" y="269" text-anchor="middle" font-size="10">19</text>
<text class="vx-mono" x="511" y="269" text-anchor="middle" font-size="10">23</text>
<text class="vx-mono" x="537" y="269" text-anchor="middle" font-size="10">27</text>
<text class="vx-mono" x="563" y="269" text-anchor="middle" font-size="10">31</text>
<text class="vx-text-accent" x="40" y="292">row 3, col 5 &#8594; lane 14</text>
<text class="vx-text-accent" x="368" y="292">row 3, col 5 &#8594; lane 21</text>
</svg>
<figcaption>Layout A and layout B both satisfy the only rule a fragment promises: store it back and you get the tile you loaded. Which lane holds cell (3, 5), or any other cell, is not part of that promise.</figcaption>
</figure>

??? check "Two GPUs implement WMMA's 16 x 16 x 16 fragment with different, undocumented lane mappings. Does a kernel that only calls the load and store functions on that fragment need to know which mapping is in use?"

    No. The fragment's contract is defined entirely by what load-then-store
    reproduces, not by which lane holds which element, so a kernel written
    only against the load, store and multiply-accumulate functions runs
    unchanged on either mapping. Only code that tried to read a lane's slots
    directly, which the API does not offer, could possibly depend on it.

## From one intrinsic to a whole family of instructions

WMMA is a C++ template API; underneath it, the actual hardware instructions
live one level down, in PTX, NVIDIA's virtual instruction set. PTX exposes
`wmma` directly, and, for more control over which lanes hold which
fragment elements, a pair of lower-level instructions: `mma`, the matrix
multiply-accumulate itself, and `ldmatrix`, which loads a tile from shared
memory straight into the layout `mma` expects.[^ptx] Splitting the two
matters because loading a tile and multiplying it are different costs: one
moves bytes, the other issues arithmetic, and a compiler or a hand-written
kernel may want to overlap many loads with one multiply, or the reverse.

The generations after Volta grew the same idea in two directions at once.
One is asynchrony: `cp.async` copies data from global memory into shared
memory without passing it through a register first, so a warp can start a
copy and go on to other work instead of stalling on it, and
`cp.async.bulk.tensor`, better known by its marketing name TMA (Tensor
Memory Accelerator), moves a whole multi-dimensional tile in one
instruction, computed by dedicated address-generation hardware rather than
by the lanes that issued it.[^async-copies] The other is scale: Hopper's
`wgmma.mma_async` is a **warpgroup**-level matrix multiply, issued
cooperatively by four warps (128 threads) at once over a tile bigger than
any single warp's `mma` could hold, and it is asynchronous in the same
sense as `cp.async`: the warpgroup issues it and can keep working while it
completes.[^hopper-tuning] PTX 9.4 also defines Blackwell's `tcgen05`
family, a further step toward tiles owned by dedicated tensor memory rather
than by the issuing threads' registers; this chapter does no more than name
it.[^ptx]

Every step in that line answers the same question the last one raised: once
a matrix unit computes a tile in one instruction, the next bottleneck is
getting tiles to it and from it fast enough to keep it fed, and each new
instruction moves more data, or bigger tiles, with less of the issuing
warp's own attention spent on the move.

??? check "What problem do cp.async and TMA solve that wmma alone does not?"

    They move a tile from global memory into shared memory without a
    register round trip and without stalling the issuing warp on a
    synchronous load first, so data movement for a later tile can overlap
    with matrix-unit instructions still working on an earlier one.

## The same idea, three names

Apple's Metal Shading Language exposes matrix units as **SIMD-group matrix
functions**, working over 8 by 8 tiles of types such as
`simdgroup_float8x8`; the types have existed since Metal 2.3, and the
hardware support behind them since Apple7, the GPU family in the M1 and
later chips, including the M4 Pro this book is written on.[^msl-spec]
Metal's feature-set tables list SIMD-scoped matrix multiply as an Apple7
capability, distinct from the ordinary SIMD-group reductions and shuffles
introduced earlier.[^metal-features] Metal 4 adds a further layer on top:
dedicated tensor types and the Metal Performance Primitives library, whose
`matmul2d` operation works at a coarser grain than a single SIMD-group
instruction.[^msl-spec]

AMD's CDNA architecture, used in its data-center GPUs, has its own matrix
cores, reached through **MFMA** (matrix fused multiply-accumulate)
instructions built into the compute units.[^hip-hw] The vocabulary changes
in every one of these three ecosystems: warp or SIMD-group, fragment or
SIMD-group matrix, `mma` or MFMA, but the shape of the idea does not. One
instruction, one small fixed tile, issued by a whole group of lanes that
between them hold every element and individually see none of the layout.

## Naming the instruction a compiler wants to emit

None of the details above are things a Vortex compiler could pattern-match
on a loop nest and simply do. Before a compiler can even ask whether a
matmul should target a matrix unit, it needs an instruction in its own
intermediate representation that means "multiply this tile by that tile and
accumulate", so that a lowering pass has something to match against a real
`wmma`, `mma`, SIMD-group or MFMA instruction later. MLIR's vector dialect
already has one: `vector.contract`, which names two input vectors, an
accumulator, and, through its indexing maps and iterator types, which axes
are multiplied, which are kept, and which are reduced over.[^vector-dialect]

--8<-- "includes/examples/gpu/g11-matrix-units/tile_contract.mlir.md"

Applied to `4x4xf32` vectors here, the same operation applies just as well
to whatever fixed tile shape a target's real matrix unit expects. What it
buys a compiler is a single, explicit place, one operation, to decide
whether a tile multiply becomes a loop of scalar multiply-adds or a call to
a hardware matrix instruction, instead of that decision being smeared across
however many individual arithmetic operations the tile's loop nest happened
to contain.

## Reduced precision is a decision, not a side effect

Real matrix-unit hardware earns much of its throughput by accepting inputs
narrower than the accumulator: WMMA's earliest tiles multiplied half or
mixed-precision inputs into a full `f32` accumulator, and later formats such
as TF32 trade some of a 32-bit input's mantissa for the same trick at wider
range. Feeding a matrix unit means feeding it the input type, and
therefore the input precision, it was built for.

That collides directly with a rule this book has already cited twice.
Decision 56 requires every `f32` or `f64` operation in Vortex to be exactly
one IEEE 754 operation, rounded once, with no contraction, reassociation or
loss of precision unless the program opts in explicitly. Converting an
`f32` input down to a narrower type before a multiply, or accumulating in a
different precision than the source values, is exactly the kind of change
that rule exists to catch: it changes the rounding of every intermediate
value, and on a matrix multiply summing many products, that can change the
final digits of the answer. The [philosophy page](../philosophy.md#performance-philosophy)
states the same limit from the optimizer's side: hardware-specific
instructions are allowed only while preserving a computation's declared
semantics. A matrix unit that takes narrower inputs does not preserve the
declared semantics of an `f32` multiply. It computes something related, and
computes it correctly by its own rules, but not the operation the program
declared.

None of this rules matrix units out. It means a Vortex compiler could only
route the [stage 10 kernel](../compiler/guide/stage-10-matrix-multiplication.md),
or any other `&mut`-output matmul, through a matrix unit's native precision
under an explicit mode the programmer asked for, the same "opt-in" the
safety philosophy already reserves for any numerical transformation that can
change a program's observable results. Without that opt-in, a matrix unit
is still usable, computing at the unit's own native width when that happens
to equal the program's declared type, or not used at all for that kernel.

??? check "Why can't a Vortex compiler swap a strict f32 matmul for a tensor-core version taking narrower inputs, on its own?"

    Because narrowing the inputs, or accumulating at a different width,
    changes the rounding of every intermediate product and sum, which
    [decision 56](../decisions/numbers.md#d56) forbids without an explicit
    opt-in. A golden-output test written against the strict kernel would
    legitimately stop matching:
    the compiler would have silently changed what the program computes,
    which the safety philosophy singles out as needing the programmer's
    permission, not the compiler's initiative.

## For Vortex

!!! vortex "Exercise"

    **Build**, in your compiler, a tile-op opportunity report for a fixed-shape
    matmul loop nest such as the stage 10 kernel's: given a candidate tile
    shape (rows, columns, reduction depth) supplied as an input, not detected
    from any real target, report, for that loop nest:

    1. Whether the array shapes the loop multiplies, taken from their
       fixed-size array types ([decision 43](../decisions/arrays.md#d43)),
       are exact multiples of the candidate tile shape in every dimension,
       and if not, the remainder left over in each dimension.
    2. If they are exact multiples, the number of tile instructions the loop
       would issue and the number of scalar multiply-adds each one stands
       for, using the identity this chapter's first example checks:
       `(N / T)^3` instructions of `T^3` multiply-adds each, generalized to
       the loop's actual, possibly unequal, row, column and reduction
       counts.
    3. Whether the kernel's element type matches a native input type you
       record by hand from one vendor's documentation for the candidate tile
       shape, and if it does not, mark the opportunity blocked pending an
       explicit opt-in, rather than silently substituting a narrower type.
    4. One remark per candidate, in the style the
       [sixth principle](../philosophy.md#6-explain-performance-decisions)
       asks for, such as `"16x16x16: exact fit, 4096 macs per instruction,
       blocked: kernel is f32, unit wants tf32"`.

    **Not yet:** choosing a tile shape automatically, emitting a real
    matrix-unit instruction or intrinsic, allocating fragments across lanes,
    or picking among CUDA, Metal and AMD targets: [M12](../mlir/m12-vortex-gpu-path.md)
    leaves that choice open.

    **Proof that it works:** a golden test against the stage 10 kernel's own
    shapes. At 64 x 64 x 64 with a 16 x 16 x 16 candidate, the report finds
    an exact fit and states 64 instructions of 4,096 macs each, matching
    `tile_ops.cpp`'s printed row for the same numbers. At a shape such as
    70 x 70 x 70 with the same candidate, the report states a remainder of 6
    in every dimension instead of rounding it away. A second, independent
    reimplementation of the `(N / T)^3` count, checked against the report on
    a few dozen shapes chosen by hand, never disagrees with it.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does a matrix unit compute in one instruction?** `D = A * B +
      C` over one small, fixed-size tile, issued cooperatively by a whole
      warp or SIMD-group.
    - **What is a fragment, and what does its API actually promise?** A
      lane's opaque share of a tile; the only promise is that storing it
      back after loading it reproduces the tile, not which lane holds which
      element.
    - **Why did CUDA add cp.async and TMA on top of wmma?** To move a tile
      from global memory into shared memory without a register round trip
      or a synchronous stall, so movement for one tile can overlap with
      matrix-unit compute on another.
    - **What is Hopper's warpgroup MMA a scale-up of?** The same per-warp
      tile instruction, issued cooperatively by four warps at once over a
      bigger tile, and asynchronous in the same sense as `cp.async`.
    - **What do Apple's SIMD-group matrices and AMD's MFMA correspond to?**
      The same idea, one small fixed tile per instruction, under different
      names: `simdgroup_float8x8` functions on Apple7 and later, MFMA matrix
      cores on AMD's CDNA compute units.
    - **Why can't a Vortex compiler swap in a matrix unit's reduced-precision
      inputs by itself?** Reduced precision changes rounding and sometimes
      range, which the language's strict floating-point rule reserves for
      an explicit, programmer-requested mode.

## Where this comes back

!!! next "You will use this again in"

    - [G12. Fusion case study: FlashAttention](g12-flashattention.md): *warpgroup MMA*, *TMA overlap*
    - [G13. Tile languages](g13-tile-languages.md): *tile shape*, *fragment*
    - [G15. Beyond GPUs: systolic arrays and accelerators](g15-systolic-arrays.md): *fixed-shape hardware tile*
    - [M8. Vectorization in MLIR](../mlir/m8-vectorization.md): *`vector.contract`*
    - [M10. MLIR for GPUs](../mlir/m10-mlir-for-gpus.md): *warp-level cooperative instructions*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *tile-op lowering*, *explicit precision opt-in*

## Sources and further reading

Read the tensor-core blog post first for the concrete picture, then the WMMA
and PTX references for the exact instructions, then the Metal specification
for Apple's equivalents.

[^tensor-cores-blog]: Mark Appleyard and Michael Yokim, "Programming Tensor Cores in CUDA 9", NVIDIA Developer Blog, 2017. <https://developer.nvidia.com/blog/programming-tensor-cores-cuda-9/>
[^wmma]: NVIDIA, "CUDA Programming Guide", v13.4, "Warp Matrix Functions". <https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-extensions.html#warp-matrix-functions>
[^ptx]: NVIDIA, "Parallel Thread Execution ISA", version 9.4. <https://docs.nvidia.com/cuda/parallel-thread-execution/index.html>
[^async-copies]: NVIDIA, "CUDA Programming Guide", v13.4, "Asynchronous Data Copies". <https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/async-copies.html>
[^hopper-tuning]: NVIDIA, "Hopper Tuning Guide". <https://docs.nvidia.com/cuda/hopper-tuning-guide/index.html>
[^msl-spec]: Apple, "Metal Shading Language Specification", version 4.1, sections 2.4, 2.22 and 6.8, and section 7 (Metal Performance Primitives). <https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf>
[^metal-features]: Apple, "Metal Feature Set Tables". <https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf>
[^hip-hw]: AMD, "HIP Documentation", "Hardware Implementation". <https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html>
[^vector-dialect]: MLIR Project, "'vector' Dialect", `vector.contract`. <https://mlir.llvm.org/docs/Dialects/Vector/#vectorcontract-vectorcontractionop>
