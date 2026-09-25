# G11. Matrix units

<p class="page-intro">A matrix unit multiplies and accumulates a whole small tile of numbers in one instruction, issued together by a group of threads. This chapter follows that instruction from NVIDIA's first tensor cores to today's asynchronous forms and to Apple's and AMD's equivalents, counts what it takes to keep one fed, and shows why, for a Vortex compiler, using one is a decision about the program's numbers, not only about its speed.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 45 minutes · Builds on: [G10. The GPU matmul ladder](g10-matmul-ladder.md), [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is a warp, and how many threads does one NVIDIA warp or one Apple SIMD-group hold?"

        A fixed-size group of threads that the GPU issues one instruction to at a time; each thread is one of the warp's lanes. It is 32 threads wide on NVIDIA GPUs and on Apple GPUs, where it is called a SIMD-group.

        Introduced in [G2. The SIMT execution model](g2-simt.md).

    ??? question "Why does giving each thread a t × t tile of outputs raise a matmul's arithmetic intensity?"

        Each value a thread reads is used t times, once for every output in its row or column of the tile, so the operations per byte read grow with t: t / 4 FLOPs per byte for `f32`.

        Introduced in [G10. The GPU matmul ladder](g10-matmul-ladder.md).

    ??? question "What is shared memory, and when may a thread read a value that another thread stored there?"

        A small on-chip scratchpad shared by the threads of one block. A thread may read another thread's value only after both have passed a barrier that orders the store before the load.

        Introduced in [G3. The GPU memory hierarchy](g3-memory-hierarchy.md).

    ??? question "How are the elements of a `[f32; 64, 64]` array laid out?"

        Row after row, the last index varying fastest. The shape is part of the type, so a compiler knows every dimension when it compiles the program.

        Introduced in [Arrays and shapes, decision 43](../decisions/arrays.md#d43).

    ??? question "May a Vortex compiler fuse, reorder or narrow the floating-point operations in `sum += a[row, k] * b[k, column]`?"

        No. Each `f32` operation is one IEEE 754 operation, rounded to nearest even, in the order written; contraction, reassociation, wider or narrower evaluation and flushing subnormals are all forbidden unless a later, explicit mode allows them.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Compute how many matrix-unit instructions a matrix product needs, and chain them along the reduction dimension by hand.
    - Explain what a fragment is, what the WMMA contract promises about it, and what PTX's `mma` promises instead.
    - Count the operand loads a warp tile of fragments needs, and relate that count to the register tiling of [G10](g10-matmul-ladder.md).
    - Trace how NVIDIA's instructions moved tile loading away from the lanes, from `wmma` through `cp.async` and TMA to `wgmma` and `tcgen05`, and name Apple's and AMD's equivalents.
    - Decide, from vendor documentation, whether a given matrix-unit instruction preserves [decision 56](../decisions/numbers.md#d56), and explain why the answer makes matrix units an explicit opt-in for Vortex.

## One instruction, one tile

Take three tiny matrices, 2 rows by 2 columns each:

```text
A = | 2  0 |        B = | 1  3 |        C = | 1  1 |
    | 1  1 |            | 2  0 |            | 0  1 |
```

Computing `D = A × B + C` by hand takes one dot product per cell of `D`. `D[0, 0]` is `2 × 1 + 0 × 2 + 1 = 3`; the other cells give `D = [[3, 7], [3, 4]]`. That is 8 multiplications and 8 additions, sixteen scalar instructions on an ordinary core, or eight if the core fuses each multiply with its add.

A **matrix unit** is hardware that computes all of `D = A × B + C` for one small tile of fixed shape in a single instruction. The shape is part of the instruction, the way three operands are part of a scalar `fma`: a program that wants a bigger product must cut it into tiles of exactly that shape. NVIDIA calls its matrix units **tensor cores**. On the Volta V100, each tensor core multiplied 4 × 4 matrices of 16-bit floats and added the result into 4 × 4 accumulators of 16-bit or 32-bit floats, 64 fused multiply-adds per clock; eight of them per streaming multiprocessor made 1,024 floating-point operations per clock, according to NVIDIA's 2017 introduction.[^n14]

The same post describes how software sees that hardware. The threads of a warp use several tensor cores at once to perform a larger 16 × 16 × 16 operation, and CUDA 9 exposed it as the **WMMA** API (warp matrix multiply-accumulate), whose example code notes that 16 × 16 × 16 was the only shape it supported.[^n14] Here the shape is written m × n × k: `D` is m × n, and k is the **reduction depth**, the length of the dot products the instruction sums.

A bigger product becomes a loop of these instructions. Split each operand into tiles; each output tile of `D` is a sum over k of tile products, and the instruction's `C` input lets that sum be built one step at a time: each result is fed back in as the next instruction's `C`. Figure 1 shows a 4 × 4 × 4 product built from 2 × 2 × 2 tiles.

<figure class="vx-figure">
<svg viewBox="0 0 760 310" role="img" aria-label="A 4 by 4 product built from 2 by 2 tile instructions chained along k" aria-describedby="g11-f1-desc">
<title id="g11-f1-title">A 4 by 4 product built from 2 by 2 tile instructions chained along k</title>
<desc id="g11-f1-desc">Three 4 by 4 matrices, A, B and D, each drawn as a 2 by 2 grid of 2 by 2 tiles. The top row of tiles of A (A00 and A01), the left column of tiles of B (B00 and B10) and the top-left tile of D (D00) are highlighted. Below, a chain of four boxes: C00 enters the first tile instruction, mma of A00 and B00; its result enters the second, mma of A01 and B10; the result is D00. Each output tile of D needs two instructions, and the four output tiles need eight.</desc>
<defs><marker id="g11-f1-h" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="24">D = A × B + C, tile shape 2 × 2 × 2, matrices 4 × 4</text>
<rect class="vx-box-accent" x="40" y="50" width="52" height="52"/>
<text class="vx-mono" x="66" y="81" text-anchor="middle">A<tspan baseline-shift="sub" font-size="10">00</tspan></text>
<rect class="vx-box-accent" x="96" y="50" width="52" height="52"/>
<text class="vx-mono" x="122" y="81" text-anchor="middle">A<tspan baseline-shift="sub" font-size="10">01</tspan></text>
<rect class="vx-box" x="40" y="106" width="52" height="52"/>
<text class="vx-mono" x="66" y="137" text-anchor="middle">A<tspan baseline-shift="sub" font-size="10">10</tspan></text>
<rect class="vx-box" x="96" y="106" width="52" height="52"/>
<text class="vx-mono" x="122" y="137" text-anchor="middle">A<tspan baseline-shift="sub" font-size="10">11</tspan></text>
<text class="vx-text-muted" x="94" y="180" text-anchor="middle">A</text>
<text class="vx-text" x="176" y="110" text-anchor="middle">×</text>
<rect class="vx-box-accent" x="210" y="50" width="52" height="52"/>
<text class="vx-mono" x="236" y="81" text-anchor="middle">B<tspan baseline-shift="sub" font-size="10">00</tspan></text>
<rect class="vx-box" x="266" y="50" width="52" height="52"/>
<text class="vx-mono" x="292" y="81" text-anchor="middle">B<tspan baseline-shift="sub" font-size="10">01</tspan></text>
<rect class="vx-box-accent" x="210" y="106" width="52" height="52"/>
<text class="vx-mono" x="236" y="137" text-anchor="middle">B<tspan baseline-shift="sub" font-size="10">10</tspan></text>
<rect class="vx-box" x="266" y="106" width="52" height="52"/>
<text class="vx-mono" x="292" y="137" text-anchor="middle">B<tspan baseline-shift="sub" font-size="10">11</tspan></text>
<text class="vx-text-muted" x="264" y="180" text-anchor="middle">B</text>
<text class="vx-text" x="346" y="110" text-anchor="middle">+ C  →</text>
<rect class="vx-box-accent" x="400" y="50" width="52" height="52"/>
<text class="vx-mono" x="426" y="81" text-anchor="middle">D<tspan baseline-shift="sub" font-size="10">00</tspan></text>
<rect class="vx-box" x="456" y="50" width="52" height="52"/>
<text class="vx-mono" x="482" y="81" text-anchor="middle">D<tspan baseline-shift="sub" font-size="10">01</tspan></text>
<rect class="vx-box" x="400" y="106" width="52" height="52"/>
<text class="vx-mono" x="426" y="137" text-anchor="middle">D<tspan baseline-shift="sub" font-size="10">10</tspan></text>
<rect class="vx-box" x="456" y="106" width="52" height="52"/>
<text class="vx-mono" x="482" y="137" text-anchor="middle">D<tspan baseline-shift="sub" font-size="10">11</tspan></text>
<text class="vx-text-muted" x="454" y="180" text-anchor="middle">D</text>
<text class="vx-mono" x="530" y="80" font-size="11">D00 = A00·B00 + A01·B10 + C00</text>
<text class="vx-text-muted" x="530" y="104">2 instructions per output tile</text>
<text class="vx-text-muted" x="530" y="124">4 output tiles → 8 instructions</text>
<text class="vx-text-muted" x="530" y="144">each: 2 × 2 × 2 = 8 multiply-adds</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 4"><rect class="vx-box" x="40" y="230" width="90" height="44"/><text class="vx-mono" x="85.0" y="257" text-anchor="middle">C00</text></g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4"><rect class="vx-box-strong" x="180" y="230" width="150" height="44"/><text class="vx-mono" x="255.0" y="257" text-anchor="middle">mma(A00, B00, ·)</text></g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4"><rect class="vx-box-strong" x="400" y="230" width="150" height="44"/><text class="vx-mono" x="475.0" y="257" text-anchor="middle">mma(A01, B10, ·)</text></g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 4"><rect class="vx-box-accent" x="620" y="230" width="90" height="44"/><text class="vx-mono" x="665.0" y="257" text-anchor="middle">D00</text></g>
<line class="vx-line" x1="130" y1="252" x2="176" y2="252" marker-end="url(#g11-f1-h)"/>
<line class="vx-line" x1="330" y1="252" x2="396" y2="252" marker-end="url(#g11-f1-h)"/>
<line class="vx-line" x1="550" y1="252" x2="616" y2="252" marker-end="url(#g11-f1-h)"/>
<text class="vx-text-muted" x="40" y="218">the chain for D00: each result is the next instruction's C</text>
</svg>
<figcaption>Figure 1. A 4 × 4 × 4 product from 2 × 2 × 2 tile instructions. Output tile D<sub>00</sub> needs the top row of tiles of A and the left column of tiles of B: two instructions, chained so that the first result is the second instruction's C. Four output tiles make eight instructions of eight multiply-adds, the same 64 multiply-adds as the scalar loop.</figcaption>
</figure>

In general, an M × N × K product on an m × n × k tile shape that divides it exactly takes (M / m) × (N / n) × (K / k) instructions, each standing for m × n × k multiply-adds. The total arithmetic does not change. What shrinks is the number of instructions the hardware must fetch, decode and issue to get through it. The first example models the instruction on integer tiles, builds the 4 × 4 × 4 product of Figure 1 and checks it against the triple loop, then counts instructions for three real tile shapes.

--8<-- "includes/examples/gpu/g11-matrix-units/tile_ops.cpp.md"

For the stage 10 kernel's 64 × 64 × 64 product, a 16 × 16 × 16 shape needs 64 instructions of 4,096 multiply-adds, and an 8 × 8 × 8 shape needs 512 of 512. The shape a target offers is a fact about the target, and the products a program wants rarely come in multiples of it; what to do with the leftover rows and columns is the boundary problem [G13](g13-tile-languages.md) takes up.

??? check "A matmul is 128 × 64 × 32 and the unit's tile shape is 16 × 8 × 16. How many instructions does it take, and how long is each output tile's chain?"

    (128 / 16) × (64 / 8) × (32 / 16) = 8 × 8 × 2 = 128 instructions, each of 16 × 8 × 16 = 2,048 multiply-adds, which together make 128 × 64 × 32 = 262,144. Each of the 64 output tiles is a chain of K / k = 2 instructions.

## A tile shared by a warp: the fragment

A matrix-unit instruction does not belong to one thread. On NVIDIA GPUs every lane of the warp must execute it, and the CUDA Programming Guide warns that it may appear in conditional code only when the condition is the same for the whole warp; otherwise execution is likely to hang.[^wmma] The 16 × 16 operands live in the lanes' registers, spread across all 32 of them. Each lane's share is called a **fragment**: in CUDA, an object that holds one lane's section of a matrix, filled by a load function, consumed by the multiply-accumulate function and written back by a store function.[^wmma]

The WMMA API says nothing about which lane holds which element. The Programming Guide states that the mapping of matrix elements into a fragment is unspecified and may change in future architectures, so a program reads individual results only after storing the fragment to memory.[^wmma] PTX, NVIDIA's virtual instruction set, says the same of its `wmma` instructions, and adds a consequence: because the layout depends on the target architecture, a fragment produced in a function compiled for one architecture may not work as an operand in a function compiled for another, even when the two link together.[^ptx-wmma]

That silence is deliberate. It is what lets one CUDA program run on GPUs whose tensor cores want their operands arranged differently. The price is that a program can do little with a fragment besides load it, multiply it and store it. The guide allows direct element access only for an operation applied uniformly to every element, such as scaling a whole tile, where the layout cannot matter.[^wmma]

Apple and MLIR made the same choice. The Metal Shading Language defines `simdgroup_float8x8` and its half and bfloat relatives as 8 × 8 matrices whose operations run cooperatively across a SIMD-group, and states that the mapping of their elements to threads is unspecified.[^msl] MLIR's `gpu` dialect gives the idea a type, `!gpu.mma_matrix`, whose layout its documentation calls opaque.[^mlir-gpu] We return to that type when a compiler has to name the operation.

??? check "A kernel loads a WMMA accumulator fragment, adds a bias that differs from column to column, and stores it. The code reads the fragment's elements directly and assumes lane 0 holds the first row. Is that a bug, even if the kernel passes its tests on today's GPU?"

    Yes. The bias differs by column, so the operation is not uniform across elements, and it depends on which lane holds which column, which WMMA leaves unspecified and free to change between architectures. The portable version stores the fragment, adds the bias from memory, and reloads it, or uses an instruction whose layout is documented.

## When the layout is the contract: mma and ldmatrix

One level down, PTX offers a second way in. Its `mma` instruction computes the same `D = A × B + C`, collectively by the whole warp, but the distribution of matrix elements across the lanes must be done explicitly before the instruction runs.[^ptx-wmma] For every shape and type, PTX gives a formula from a lane and a register index to a row and a column. For the 32-bit accumulator of `mma.m16n8k16`, a 16 × 8 tile available from compute capability 8.0, the lanes form eight groups of four: `group = lane >> 2` and `t = lane % 4`. Registers `c0` and `c1` hold row `group`, and `c2` and `c3` hold row `group + 8`; within each row, the lane holds columns `2t` and `2t + 1`.[^ptx-mma-layout][^ptx-mma]

Work it by hand for lane 5 before looking at Figure 2: group 1, t = 1, so rows 1 and 9, columns 2 and 3. The second example applies the formula to all 32 lanes, prints the owner of every element, and checks that each of the 128 elements has exactly one owner.

--8<-- "includes/examples/gpu/g11-matrix-units/mma_layout.cpp.md"

<figure class="vx-figure">
<svg viewBox="0 0 700 430" role="img" aria-label="Which lane owns each element of the 16 by 8 f32 accumulator of mma.m16n8k16" aria-describedby="g11-f2-desc">
<title id="g11-f2-title">Which lane owns each element of the 16 by 8 f32 accumulator of mma.m16n8k16</title>
<desc id="g11-f2-desc">A grid of 16 rows and 8 columns. Each cell shows the number of the lane that holds that accumulator element. Row 0 reads 0, 0, 1, 1, 2, 2, 3, 3; row 1 reads 4, 4, 5, 5, 6, 6, 7, 7; and so on to row 7, which reads 28, 28, 29, 29, 30, 30, 31, 31. Rows 8 to 15 repeat rows 0 to 7. The four cells of lane 5 are highlighted: row 1, columns 2 and 3, and row 9, columns 2 and 3. Each lane owns two neighbouring elements in one row and the same two columns eight rows further down.</desc>
<text class="vx-text" x="20" y="24">mma.m16n8k16, f32 accumulator: owner lane of each element (mma_layout.cpp)</text>
<text class="vx-text-muted" x="85.0" y="66" text-anchor="middle">0</text>
<text class="vx-text-muted" x="115.0" y="66" text-anchor="middle">1</text>
<text class="vx-text-muted" x="145.0" y="66" text-anchor="middle">2</text>
<text class="vx-text-muted" x="175.0" y="66" text-anchor="middle">3</text>
<text class="vx-text-muted" x="205.0" y="66" text-anchor="middle">4</text>
<text class="vx-text-muted" x="235.0" y="66" text-anchor="middle">5</text>
<text class="vx-text-muted" x="265.0" y="66" text-anchor="middle">6</text>
<text class="vx-text-muted" x="295.0" y="66" text-anchor="middle">7</text>
<text class="vx-text-muted" x="190" y="46" text-anchor="middle">column</text>
<text class="vx-text-muted" x="60" y="89" text-anchor="end">0</text>
<rect class="vx-box" x="70" y="74" width="30" height="21"/>
<text class="vx-mono" x="85.0" y="89" text-anchor="middle" font-size="11">0</text>
<rect class="vx-box" x="100" y="74" width="30" height="21"/>
<text class="vx-mono" x="115.0" y="89" text-anchor="middle" font-size="11">0</text>
<rect class="vx-box" x="130" y="74" width="30" height="21"/>
<text class="vx-mono" x="145.0" y="89" text-anchor="middle" font-size="11">1</text>
<rect class="vx-box" x="160" y="74" width="30" height="21"/>
<text class="vx-mono" x="175.0" y="89" text-anchor="middle" font-size="11">1</text>
<rect class="vx-box" x="190" y="74" width="30" height="21"/>
<text class="vx-mono" x="205.0" y="89" text-anchor="middle" font-size="11">2</text>
<rect class="vx-box" x="220" y="74" width="30" height="21"/>
<text class="vx-mono" x="235.0" y="89" text-anchor="middle" font-size="11">2</text>
<rect class="vx-box" x="250" y="74" width="30" height="21"/>
<text class="vx-mono" x="265.0" y="89" text-anchor="middle" font-size="11">3</text>
<rect class="vx-box" x="280" y="74" width="30" height="21"/>
<text class="vx-mono" x="295.0" y="89" text-anchor="middle" font-size="11">3</text>
<text class="vx-text-muted" x="60" y="110" text-anchor="end">1</text>
<rect class="vx-box" x="70" y="95" width="30" height="21"/>
<text class="vx-mono" x="85.0" y="110" text-anchor="middle" font-size="11">4</text>
<rect class="vx-box" x="100" y="95" width="30" height="21"/>
<text class="vx-mono" x="115.0" y="110" text-anchor="middle" font-size="11">4</text>
<rect class="vx-box-accent" x="130" y="95" width="30" height="21"/>
<text class="vx-text-accent" x="145.0" y="110" text-anchor="middle" font-size="11">5</text>
<rect class="vx-box-accent" x="160" y="95" width="30" height="21"/>
<text class="vx-text-accent" x="175.0" y="110" text-anchor="middle" font-size="11">5</text>
<rect class="vx-box" x="190" y="95" width="30" height="21"/>
<text class="vx-mono" x="205.0" y="110" text-anchor="middle" font-size="11">6</text>
<rect class="vx-box" x="220" y="95" width="30" height="21"/>
<text class="vx-mono" x="235.0" y="110" text-anchor="middle" font-size="11">6</text>
<rect class="vx-box" x="250" y="95" width="30" height="21"/>
<text class="vx-mono" x="265.0" y="110" text-anchor="middle" font-size="11">7</text>
<rect class="vx-box" x="280" y="95" width="30" height="21"/>
<text class="vx-mono" x="295.0" y="110" text-anchor="middle" font-size="11">7</text>
<text class="vx-text-muted" x="60" y="131" text-anchor="end">2</text>
<rect class="vx-box" x="70" y="116" width="30" height="21"/>
<text class="vx-mono" x="85.0" y="131" text-anchor="middle" font-size="11">8</text>
<rect class="vx-box" x="100" y="116" width="30" height="21"/>
<text class="vx-mono" x="115.0" y="131" text-anchor="middle" font-size="11">8</text>
<rect class="vx-box" x="130" y="116" width="30" height="21"/>
<text class="vx-mono" x="145.0" y="131" text-anchor="middle" font-size="11">9</text>
<rect class="vx-box" x="160" y="116" width="30" height="21"/>
<text class="vx-mono" x="175.0" y="131" text-anchor="middle" font-size="11">9</text>
<rect class="vx-box" x="190" y="116" width="30" height="21"/>
<text class="vx-mono" x="205.0" y="131" text-anchor="middle" font-size="11">10</text>
<rect class="vx-box" x="220" y="116" width="30" height="21"/>
<text class="vx-mono" x="235.0" y="131" text-anchor="middle" font-size="11">10</text>
<rect class="vx-box" x="250" y="116" width="30" height="21"/>
<text class="vx-mono" x="265.0" y="131" text-anchor="middle" font-size="11">11</text>
<rect class="vx-box" x="280" y="116" width="30" height="21"/>
<text class="vx-mono" x="295.0" y="131" text-anchor="middle" font-size="11">11</text>
<text class="vx-text-muted" x="60" y="152" text-anchor="end">3</text>
<rect class="vx-box" x="70" y="137" width="30" height="21"/>
<text class="vx-mono" x="85.0" y="152" text-anchor="middle" font-size="11">12</text>
<rect class="vx-box" x="100" y="137" width="30" height="21"/>
<text class="vx-mono" x="115.0" y="152" text-anchor="middle" font-size="11">12</text>
<rect class="vx-box" x="130" y="137" width="30" height="21"/>
<text class="vx-mono" x="145.0" y="152" text-anchor="middle" font-size="11">13</text>
<rect class="vx-box" x="160" y="137" width="30" height="21"/>
<text class="vx-mono" x="175.0" y="152" text-anchor="middle" font-size="11">13</text>
<rect class="vx-box" x="190" y="137" width="30" height="21"/>
<text class="vx-mono" x="205.0" y="152" text-anchor="middle" font-size="11">14</text>
<rect class="vx-box" x="220" y="137" width="30" height="21"/>
<text class="vx-mono" x="235.0" y="152" text-anchor="middle" font-size="11">14</text>
<rect class="vx-box" x="250" y="137" width="30" height="21"/>
<text class="vx-mono" x="265.0" y="152" text-anchor="middle" font-size="11">15</text>
<rect class="vx-box" x="280" y="137" width="30" height="21"/>
<text class="vx-mono" x="295.0" y="152" text-anchor="middle" font-size="11">15</text>
<text class="vx-text-muted" x="60" y="173" text-anchor="end">4</text>
<rect class="vx-box" x="70" y="158" width="30" height="21"/>
<text class="vx-mono" x="85.0" y="173" text-anchor="middle" font-size="11">16</text>
<rect class="vx-box" x="100" y="158" width="30" height="21"/>
<text class="vx-mono" x="115.0" y="173" text-anchor="middle" font-size="11">16</text>
<rect class="vx-box" x="130" y="158" width="30" height="21"/>
<text class="vx-mono" x="145.0" y="173" text-anchor="middle" font-size="11">17</text>
<rect class="vx-box" x="160" y="158" width="30" height="21"/>
<text class="vx-mono" x="175.0" y="173" text-anchor="middle" font-size="11">17</text>
<rect class="vx-box" x="190" y="158" width="30" height="21"/>
<text class="vx-mono" x="205.0" y="173" text-anchor="middle" font-size="11">18</text>
<rect class="vx-box" x="220" y="158" width="30" height="21"/>
<text class="vx-mono" x="235.0" y="173" text-anchor="middle" font-size="11">18</text>
<rect class="vx-box" x="250" y="158" width="30" height="21"/>
<text class="vx-mono" x="265.0" y="173" text-anchor="middle" font-size="11">19</text>
<rect class="vx-box" x="280" y="158" width="30" height="21"/>
<text class="vx-mono" x="295.0" y="173" text-anchor="middle" font-size="11">19</text>
<text class="vx-text-muted" x="60" y="194" text-anchor="end">5</text>
<rect class="vx-box" x="70" y="179" width="30" height="21"/>
<text class="vx-mono" x="85.0" y="194" text-anchor="middle" font-size="11">20</text>
<rect class="vx-box" x="100" y="179" width="30" height="21"/>
<text class="vx-mono" x="115.0" y="194" text-anchor="middle" font-size="11">20</text>
<rect class="vx-box" x="130" y="179" width="30" height="21"/>
<text class="vx-mono" x="145.0" y="194" text-anchor="middle" font-size="11">21</text>
<rect class="vx-box" x="160" y="179" width="30" height="21"/>
<text class="vx-mono" x="175.0" y="194" text-anchor="middle" font-size="11">21</text>
<rect class="vx-box" x="190" y="179" width="30" height="21"/>
<text class="vx-mono" x="205.0" y="194" text-anchor="middle" font-size="11">22</text>
<rect class="vx-box" x="220" y="179" width="30" height="21"/>
<text class="vx-mono" x="235.0" y="194" text-anchor="middle" font-size="11">22</text>
<rect class="vx-box" x="250" y="179" width="30" height="21"/>
<text class="vx-mono" x="265.0" y="194" text-anchor="middle" font-size="11">23</text>
<rect class="vx-box" x="280" y="179" width="30" height="21"/>
<text class="vx-mono" x="295.0" y="194" text-anchor="middle" font-size="11">23</text>
<text class="vx-text-muted" x="60" y="215" text-anchor="end">6</text>
<rect class="vx-box" x="70" y="200" width="30" height="21"/>
<text class="vx-mono" x="85.0" y="215" text-anchor="middle" font-size="11">24</text>
<rect class="vx-box" x="100" y="200" width="30" height="21"/>
<text class="vx-mono" x="115.0" y="215" text-anchor="middle" font-size="11">24</text>
<rect class="vx-box" x="130" y="200" width="30" height="21"/>
<text class="vx-mono" x="145.0" y="215" text-anchor="middle" font-size="11">25</text>
<rect class="vx-box" x="160" y="200" width="30" height="21"/>
<text class="vx-mono" x="175.0" y="215" text-anchor="middle" font-size="11">25</text>
<rect class="vx-box" x="190" y="200" width="30" height="21"/>
<text class="vx-mono" x="205.0" y="215" text-anchor="middle" font-size="11">26</text>
<rect class="vx-box" x="220" y="200" width="30" height="21"/>
<text class="vx-mono" x="235.0" y="215" text-anchor="middle" font-size="11">26</text>
<rect class="vx-box" x="250" y="200" width="30" height="21"/>
<text class="vx-mono" x="265.0" y="215" text-anchor="middle" font-size="11">27</text>
<rect class="vx-box" x="280" y="200" width="30" height="21"/>
<text class="vx-mono" x="295.0" y="215" text-anchor="middle" font-size="11">27</text>
<text class="vx-text-muted" x="60" y="236" text-anchor="end">7</text>
<rect class="vx-box" x="70" y="221" width="30" height="21"/>
<text class="vx-mono" x="85.0" y="236" text-anchor="middle" font-size="11">28</text>
<rect class="vx-box" x="100" y="221" width="30" height="21"/>
<text class="vx-mono" x="115.0" y="236" text-anchor="middle" font-size="11">28</text>
<rect class="vx-box" x="130" y="221" width="30" height="21"/>
<text class="vx-mono" x="145.0" y="236" text-anchor="middle" font-size="11">29</text>
<rect class="vx-box" x="160" y="221" width="30" height="21"/>
<text class="vx-mono" x="175.0" y="236" text-anchor="middle" font-size="11">29</text>
<rect class="vx-box" x="190" y="221" width="30" height="21"/>
<text class="vx-mono" x="205.0" y="236" text-anchor="middle" font-size="11">30</text>
<rect class="vx-box" x="220" y="221" width="30" height="21"/>
<text class="vx-mono" x="235.0" y="236" text-anchor="middle" font-size="11">30</text>
<rect class="vx-box" x="250" y="221" width="30" height="21"/>
<text class="vx-mono" x="265.0" y="236" text-anchor="middle" font-size="11">31</text>
<rect class="vx-box" x="280" y="221" width="30" height="21"/>
<text class="vx-mono" x="295.0" y="236" text-anchor="middle" font-size="11">31</text>
<text class="vx-text-muted" x="60" y="257" text-anchor="end">8</text>
<rect class="vx-box" x="70" y="242" width="30" height="21"/>
<text class="vx-mono" x="85.0" y="257" text-anchor="middle" font-size="11">0</text>
<rect class="vx-box" x="100" y="242" width="30" height="21"/>
<text class="vx-mono" x="115.0" y="257" text-anchor="middle" font-size="11">0</text>
<rect class="vx-box" x="130" y="242" width="30" height="21"/>
<text class="vx-mono" x="145.0" y="257" text-anchor="middle" font-size="11">1</text>
<rect class="vx-box" x="160" y="242" width="30" height="21"/>
<text class="vx-mono" x="175.0" y="257" text-anchor="middle" font-size="11">1</text>
<rect class="vx-box" x="190" y="242" width="30" height="21"/>
<text class="vx-mono" x="205.0" y="257" text-anchor="middle" font-size="11">2</text>
<rect class="vx-box" x="220" y="242" width="30" height="21"/>
<text class="vx-mono" x="235.0" y="257" text-anchor="middle" font-size="11">2</text>
<rect class="vx-box" x="250" y="242" width="30" height="21"/>
<text class="vx-mono" x="265.0" y="257" text-anchor="middle" font-size="11">3</text>
<rect class="vx-box" x="280" y="242" width="30" height="21"/>
<text class="vx-mono" x="295.0" y="257" text-anchor="middle" font-size="11">3</text>
<text class="vx-text-muted" x="60" y="278" text-anchor="end">9</text>
<rect class="vx-box" x="70" y="263" width="30" height="21"/>
<text class="vx-mono" x="85.0" y="278" text-anchor="middle" font-size="11">4</text>
<rect class="vx-box" x="100" y="263" width="30" height="21"/>
<text class="vx-mono" x="115.0" y="278" text-anchor="middle" font-size="11">4</text>
<rect class="vx-box-accent" x="130" y="263" width="30" height="21"/>
<text class="vx-text-accent" x="145.0" y="278" text-anchor="middle" font-size="11">5</text>
<rect class="vx-box-accent" x="160" y="263" width="30" height="21"/>
<text class="vx-text-accent" x="175.0" y="278" text-anchor="middle" font-size="11">5</text>
<rect class="vx-box" x="190" y="263" width="30" height="21"/>
<text class="vx-mono" x="205.0" y="278" text-anchor="middle" font-size="11">6</text>
<rect class="vx-box" x="220" y="263" width="30" height="21"/>
<text class="vx-mono" x="235.0" y="278" text-anchor="middle" font-size="11">6</text>
<rect class="vx-box" x="250" y="263" width="30" height="21"/>
<text class="vx-mono" x="265.0" y="278" text-anchor="middle" font-size="11">7</text>
<rect class="vx-box" x="280" y="263" width="30" height="21"/>
<text class="vx-mono" x="295.0" y="278" text-anchor="middle" font-size="11">7</text>
<text class="vx-text-muted" x="60" y="299" text-anchor="end">10</text>
<rect class="vx-box" x="70" y="284" width="30" height="21"/>
<text class="vx-mono" x="85.0" y="299" text-anchor="middle" font-size="11">8</text>
<rect class="vx-box" x="100" y="284" width="30" height="21"/>
<text class="vx-mono" x="115.0" y="299" text-anchor="middle" font-size="11">8</text>
<rect class="vx-box" x="130" y="284" width="30" height="21"/>
<text class="vx-mono" x="145.0" y="299" text-anchor="middle" font-size="11">9</text>
<rect class="vx-box" x="160" y="284" width="30" height="21"/>
<text class="vx-mono" x="175.0" y="299" text-anchor="middle" font-size="11">9</text>
<rect class="vx-box" x="190" y="284" width="30" height="21"/>
<text class="vx-mono" x="205.0" y="299" text-anchor="middle" font-size="11">10</text>
<rect class="vx-box" x="220" y="284" width="30" height="21"/>
<text class="vx-mono" x="235.0" y="299" text-anchor="middle" font-size="11">10</text>
<rect class="vx-box" x="250" y="284" width="30" height="21"/>
<text class="vx-mono" x="265.0" y="299" text-anchor="middle" font-size="11">11</text>
<rect class="vx-box" x="280" y="284" width="30" height="21"/>
<text class="vx-mono" x="295.0" y="299" text-anchor="middle" font-size="11">11</text>
<text class="vx-text-muted" x="60" y="320" text-anchor="end">11</text>
<rect class="vx-box" x="70" y="305" width="30" height="21"/>
<text class="vx-mono" x="85.0" y="320" text-anchor="middle" font-size="11">12</text>
<rect class="vx-box" x="100" y="305" width="30" height="21"/>
<text class="vx-mono" x="115.0" y="320" text-anchor="middle" font-size="11">12</text>
<rect class="vx-box" x="130" y="305" width="30" height="21"/>
<text class="vx-mono" x="145.0" y="320" text-anchor="middle" font-size="11">13</text>
<rect class="vx-box" x="160" y="305" width="30" height="21"/>
<text class="vx-mono" x="175.0" y="320" text-anchor="middle" font-size="11">13</text>
<rect class="vx-box" x="190" y="305" width="30" height="21"/>
<text class="vx-mono" x="205.0" y="320" text-anchor="middle" font-size="11">14</text>
<rect class="vx-box" x="220" y="305" width="30" height="21"/>
<text class="vx-mono" x="235.0" y="320" text-anchor="middle" font-size="11">14</text>
<rect class="vx-box" x="250" y="305" width="30" height="21"/>
<text class="vx-mono" x="265.0" y="320" text-anchor="middle" font-size="11">15</text>
<rect class="vx-box" x="280" y="305" width="30" height="21"/>
<text class="vx-mono" x="295.0" y="320" text-anchor="middle" font-size="11">15</text>
<text class="vx-text-muted" x="60" y="341" text-anchor="end">12</text>
<rect class="vx-box" x="70" y="326" width="30" height="21"/>
<text class="vx-mono" x="85.0" y="341" text-anchor="middle" font-size="11">16</text>
<rect class="vx-box" x="100" y="326" width="30" height="21"/>
<text class="vx-mono" x="115.0" y="341" text-anchor="middle" font-size="11">16</text>
<rect class="vx-box" x="130" y="326" width="30" height="21"/>
<text class="vx-mono" x="145.0" y="341" text-anchor="middle" font-size="11">17</text>
<rect class="vx-box" x="160" y="326" width="30" height="21"/>
<text class="vx-mono" x="175.0" y="341" text-anchor="middle" font-size="11">17</text>
<rect class="vx-box" x="190" y="326" width="30" height="21"/>
<text class="vx-mono" x="205.0" y="341" text-anchor="middle" font-size="11">18</text>
<rect class="vx-box" x="220" y="326" width="30" height="21"/>
<text class="vx-mono" x="235.0" y="341" text-anchor="middle" font-size="11">18</text>
<rect class="vx-box" x="250" y="326" width="30" height="21"/>
<text class="vx-mono" x="265.0" y="341" text-anchor="middle" font-size="11">19</text>
<rect class="vx-box" x="280" y="326" width="30" height="21"/>
<text class="vx-mono" x="295.0" y="341" text-anchor="middle" font-size="11">19</text>
<text class="vx-text-muted" x="60" y="362" text-anchor="end">13</text>
<rect class="vx-box" x="70" y="347" width="30" height="21"/>
<text class="vx-mono" x="85.0" y="362" text-anchor="middle" font-size="11">20</text>
<rect class="vx-box" x="100" y="347" width="30" height="21"/>
<text class="vx-mono" x="115.0" y="362" text-anchor="middle" font-size="11">20</text>
<rect class="vx-box" x="130" y="347" width="30" height="21"/>
<text class="vx-mono" x="145.0" y="362" text-anchor="middle" font-size="11">21</text>
<rect class="vx-box" x="160" y="347" width="30" height="21"/>
<text class="vx-mono" x="175.0" y="362" text-anchor="middle" font-size="11">21</text>
<rect class="vx-box" x="190" y="347" width="30" height="21"/>
<text class="vx-mono" x="205.0" y="362" text-anchor="middle" font-size="11">22</text>
<rect class="vx-box" x="220" y="347" width="30" height="21"/>
<text class="vx-mono" x="235.0" y="362" text-anchor="middle" font-size="11">22</text>
<rect class="vx-box" x="250" y="347" width="30" height="21"/>
<text class="vx-mono" x="265.0" y="362" text-anchor="middle" font-size="11">23</text>
<rect class="vx-box" x="280" y="347" width="30" height="21"/>
<text class="vx-mono" x="295.0" y="362" text-anchor="middle" font-size="11">23</text>
<text class="vx-text-muted" x="60" y="383" text-anchor="end">14</text>
<rect class="vx-box" x="70" y="368" width="30" height="21"/>
<text class="vx-mono" x="85.0" y="383" text-anchor="middle" font-size="11">24</text>
<rect class="vx-box" x="100" y="368" width="30" height="21"/>
<text class="vx-mono" x="115.0" y="383" text-anchor="middle" font-size="11">24</text>
<rect class="vx-box" x="130" y="368" width="30" height="21"/>
<text class="vx-mono" x="145.0" y="383" text-anchor="middle" font-size="11">25</text>
<rect class="vx-box" x="160" y="368" width="30" height="21"/>
<text class="vx-mono" x="175.0" y="383" text-anchor="middle" font-size="11">25</text>
<rect class="vx-box" x="190" y="368" width="30" height="21"/>
<text class="vx-mono" x="205.0" y="383" text-anchor="middle" font-size="11">26</text>
<rect class="vx-box" x="220" y="368" width="30" height="21"/>
<text class="vx-mono" x="235.0" y="383" text-anchor="middle" font-size="11">26</text>
<rect class="vx-box" x="250" y="368" width="30" height="21"/>
<text class="vx-mono" x="265.0" y="383" text-anchor="middle" font-size="11">27</text>
<rect class="vx-box" x="280" y="368" width="30" height="21"/>
<text class="vx-mono" x="295.0" y="383" text-anchor="middle" font-size="11">27</text>
<text class="vx-text-muted" x="60" y="404" text-anchor="end">15</text>
<rect class="vx-box" x="70" y="389" width="30" height="21"/>
<text class="vx-mono" x="85.0" y="404" text-anchor="middle" font-size="11">28</text>
<rect class="vx-box" x="100" y="389" width="30" height="21"/>
<text class="vx-mono" x="115.0" y="404" text-anchor="middle" font-size="11">28</text>
<rect class="vx-box" x="130" y="389" width="30" height="21"/>
<text class="vx-mono" x="145.0" y="404" text-anchor="middle" font-size="11">29</text>
<rect class="vx-box" x="160" y="389" width="30" height="21"/>
<text class="vx-mono" x="175.0" y="404" text-anchor="middle" font-size="11">29</text>
<rect class="vx-box" x="190" y="389" width="30" height="21"/>
<text class="vx-mono" x="205.0" y="404" text-anchor="middle" font-size="11">30</text>
<rect class="vx-box" x="220" y="389" width="30" height="21"/>
<text class="vx-mono" x="235.0" y="404" text-anchor="middle" font-size="11">30</text>
<rect class="vx-box" x="250" y="389" width="30" height="21"/>
<text class="vx-mono" x="265.0" y="404" text-anchor="middle" font-size="11">31</text>
<rect class="vx-box" x="280" y="389" width="30" height="21"/>
<text class="vx-mono" x="295.0" y="404" text-anchor="middle" font-size="11">31</text>
<text class="vx-text-muted" x="26" y="242" text-anchor="middle" transform="rotate(-90 26 242)">row</text>
<text class="vx-text" x="360" y="108">The rule PTX gives, per lane:</text>
<text class="vx-mono" x="360" y="132" font-size="12">group = lane >> 2</text>
<text class="vx-mono" x="360" y="156" font-size="12">t     = lane % 4</text>
<text class="vx-mono" x="360" y="180" font-size="12">c0, c1: row group,     cols 2t, 2t+1</text>
<text class="vx-mono" x="360" y="204" font-size="12">c2, c3: row group + 8, cols 2t, 2t+1</text>
<text class="vx-text-accent" x="360" y="252">lane 5: group 1, t = 1</text>
<text class="vx-text-accent" x="360" y="276">rows 1 and 9, columns 2 and 3</text>
<text class="vx-text-muted" x="360" y="324">128 elements, 32 lanes, 4 each.</text>
<text class="vx-text-muted" x="360" y="348">WMMA promises no such map;</text>
<text class="vx-text-muted" x="360" y="372">mma requires the program to follow it.</text>
</svg>
<figcaption>Figure 2. The documented layout of the <code>mma.m16n8k16</code> <code>f32</code> accumulator. Every lane holds four of the 128 elements: a pair of neighbouring columns in one row, and the same pair eight rows below. Lane 5's four elements are highlighted. WMMA fragments of the same size promise no map at all.</figcaption>
</figure>

A documented layout is a promise a compiler can build on. Code that emits `mma` knows which output elements each lane holds, so it can apply a bias that varies by column, or a conversion before the store, directly in registers. It also knows exactly what each lane must hold before the instruction runs, and that is a second job: getting the operands into that shape.

For that job PTX has **ldmatrix**, available from compute capability 7.5: all 32 lanes together load one, two or four small matrices (8 × 8 in the basic shape) of 16-bit or narrower elements from shared memory into their registers, for use by `mma`.[^ptx-ldmatrix] Splitting the work this way separates two costs, moving bytes and doing arithmetic, which a kernel can then overlap.

CUTLASS, NVIDIA's template library for fast matrix products, builds its warp-level products on exactly these instructions, `mma.sync` or `wmma`, fed from shared memory into registers, and keeps two sets of fragments so that one is loaded while the other is used.[^cutlass]

??? check "Using the formula, which lane holds accumulator element (row 14, column 5) of `mma.m16n8k16`, and in which register?"

    Row 14 is at least 8, so it is `c2` or `c3` of group 14 − 8 = 6. Column 5 is `2t + 1` with t = 2, so the register is `c3` (the odd column) and the lane is 4 × 6 + 2 = 26. Row 14 of the printed grid confirms it: `26 26 27 27` puts lane 26 in columns 4 and 5.

## Feeding the unit: reuse at the fragment level

A matrix unit finishes a tile in one instruction, so the next question is the one [G10](g10-matmul-ladder.md) asked of every rung: can the data arrive fast enough? The answer uses the same reuse argument, one level up. Instead of each thread holding a tile of scalar accumulators, each warp holds a **warp tile** of fm × fn accumulator fragments. At each step of k it loads fm fragments of A and fn fragments of B, and every A fragment meets every B fragment once: fm × fn instructions from fm + fn loads.

Work one case by hand, with Apple's 8 × 8 × 8 SIMD-group operation. With a 2 × 2 warp tile, one step of k loads 4 fragments of 64 elements, 256 elements, and issues 4 instructions of 512 multiply-adds, 2,048: eight multiply-adds per element loaded. A 1 × 1 warp tile gets four. In general the ratio is 8 × fm × fn / (fm + fn), and for a square warp tile of side f it is 4f, growing linearly with the tile side as the thread tile's intensity did in G10.

The third example walks the loops of a 64 × 64 × 64 product for several warp tiles and counts loads and instructions directly.

--8<-- "includes/examples/gpu/g11-matrix-units/fragment_reuse.cpp.md"

The last column is the cost. An 8 × 8 accumulator holds 64 values, two per lane on average across a 32-lane SIMD-group, so a 4 × 4 warp tile keeps 32 accumulator values in every lane's registers, before any operand fragments. [G5](g5-occupancy.md) explains what spending registers does to the number of warps a core can keep in flight. CUTLASS's guidance points the same way from the other side: choose a large warp-level tile to maximize reuse within the warp.[^cutlass] The best warp tile is the largest one the register budget allows, and the register budget belongs to the target.

## Moving tiles without the lanes

Each later generation of NVIDIA's instructions attacks the same bottleneck: getting tiles to the unit without spending the lanes' instructions and registers on the move (Figure 3).

<figure class="vx-figure">
<svg viewBox="0 0 850 420" role="img" aria-label="How operand tiles reach NVIDIA's matrix unit in four instruction generations" aria-describedby="g11-f3-desc">
<title id="g11-f3-title">How operand tiles reach NVIDIA's matrix unit in four instruction generations</title>
<desc id="g11-f3-desc">Four rows, one per generation, across five columns: global memory, shared memory, registers, the matrix unit and tensor memory. Row 1, wmma from compute capability 7.0: wmma.load brings a tile from memory into the warp's registers, and wmma.mma, issued by one warp, reads them. Row 2, compute capability 8.0: cp.async copies from global to shared memory without passing through registers, ldmatrix loads shared memory into registers in the layout mma needs, and mma.sync is issued by one warp. Row 3, Hopper, compute capability 9.0: a TMA bulk tensor copy moves a whole tile from global to shared memory, and wgmma, issued by a warpgroup of four warps, reads B straight from shared memory and A from registers or shared memory, with the accumulator in registers. Row 4, sm_100a: tcgen05.mma is issued by a single thread and accumulates into a dedicated tensor memory. From row to row, fewer steps pass through the lanes' own registers.</desc>
<defs><marker id="g11-f3-h" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text-muted" x="255" y="40" text-anchor="middle">global</text>
<text class="vx-text-muted" x="385" y="40" text-anchor="middle">shared</text>
<text class="vx-text-muted" x="515" y="40" text-anchor="middle">registers</text>
<text class="vx-text-muted" x="645" y="40" text-anchor="middle">matrix unit</text>
<text class="vx-text-muted" x="775" y="40" text-anchor="middle">tensor memory</text>
<text class="vx-text" x="20" y="92">CC 7.0</text>
<text class="vx-text-muted" x="20" y="110">wmma</text>
<text class="vx-text-accent" x="20" y="128">issued by 1 warp</text>
<rect class="vx-box" x="205" y="78" width="100" height="40"/>
<text class="vx-mono" x="255" y="103" text-anchor="middle" font-size="12">tile</text>
<rect class="vx-box" x="465" y="78" width="100" height="40"/>
<text class="vx-mono" x="515" y="103" text-anchor="middle" font-size="12">fragments</text>
<line class="vx-flow" x1="305" y1="98" x2="463" y2="98" marker-end="url(#g11-f3-h)"/>
<text class="vx-text-muted" x="385.0" y="140" text-anchor="middle">wmma.load</text>
<rect class="vx-box-accent" x="595" y="78" width="100" height="40"/>
<text class="vx-mono" x="645" y="103" text-anchor="middle" font-size="12">D = A·B + D</text>
<line class="vx-flow" x1="565" y1="98" x2="593" y2="98" marker-end="url(#g11-f3-h)"/>
<text class="vx-text-muted" x="580.0" y="140" text-anchor="middle">wmma.mma</text>
<text class="vx-text" x="20" y="176">CC 8.0</text>
<text class="vx-text-muted" x="20" y="194">cp.async, ldmatrix, mma</text>
<text class="vx-text-accent" x="20" y="212">issued by 1 warp</text>
<rect class="vx-box" x="205" y="162" width="100" height="40"/>
<text class="vx-mono" x="255" y="187" text-anchor="middle" font-size="12">tile</text>
<rect class="vx-box" x="335" y="162" width="100" height="40"/>
<text class="vx-mono" x="385" y="187" text-anchor="middle" font-size="12">tile</text>
<line class="vx-flow" x1="305" y1="182" x2="333" y2="182" marker-end="url(#g11-f3-h)"/>
<text class="vx-text-muted" x="320.0" y="224" text-anchor="middle">cp.async</text>
<rect class="vx-box" x="465" y="162" width="100" height="40"/>
<text class="vx-mono" x="515" y="187" text-anchor="middle" font-size="12">fragments</text>
<line class="vx-flow" x1="435" y1="182" x2="463" y2="182" marker-end="url(#g11-f3-h)"/>
<text class="vx-text-muted" x="450.0" y="224" text-anchor="middle">ldmatrix</text>
<rect class="vx-box-accent" x="595" y="162" width="100" height="40"/>
<text class="vx-mono" x="645" y="187" text-anchor="middle" font-size="12">D = A·B + D</text>
<line class="vx-flow" x1="565" y1="182" x2="593" y2="182" marker-end="url(#g11-f3-h)"/>
<text class="vx-text-muted" x="580.0" y="224" text-anchor="middle">mma.sync</text>
<text class="vx-text" x="20" y="260">CC 9.0</text>
<text class="vx-text-muted" x="20" y="278">TMA, wgmma</text>
<text class="vx-text-accent" x="20" y="296">issued by 4 warps</text>
<rect class="vx-box" x="205" y="246" width="100" height="40"/>
<text class="vx-mono" x="255" y="271" text-anchor="middle" font-size="12">tile</text>
<rect class="vx-box" x="335" y="246" width="100" height="40"/>
<text class="vx-mono" x="385" y="271" text-anchor="middle" font-size="12">tile</text>
<line class="vx-flow" x1="305" y1="266" x2="333" y2="266" marker-end="url(#g11-f3-h)"/>
<text class="vx-text-muted" x="320.0" y="308" text-anchor="middle">TMA copy</text>
<rect class="vx-box-accent" x="595" y="246" width="100" height="40"/>
<text class="vx-mono" x="645" y="271" text-anchor="middle" font-size="12">D = A·B + D</text>
<line class="vx-flow" x1="435" y1="266" x2="593" y2="266" marker-end="url(#g11-f3-h)"/>
<text class="vx-text-muted" x="515.0" y="308" text-anchor="middle">wgmma (B from shared)</text>
<text class="vx-text" x="20" y="344">sm_100a</text>
<text class="vx-text-muted" x="20" y="362">tcgen05</text>
<text class="vx-text-accent" x="20" y="380">issued by 1 thread</text>
<rect class="vx-box" x="205" y="330" width="100" height="40"/>
<text class="vx-mono" x="255" y="355" text-anchor="middle" font-size="12">tile</text>
<rect class="vx-box" x="335" y="330" width="100" height="40"/>
<text class="vx-mono" x="385" y="355" text-anchor="middle" font-size="12">tile</text>
<line class="vx-flow" x1="305" y1="350" x2="333" y2="350" marker-end="url(#g11-f3-h)"/>
<text class="vx-text-muted" x="320.0" y="392" text-anchor="middle">TMA copy</text>
<rect class="vx-box-accent" x="595" y="330" width="100" height="40"/>
<text class="vx-mono" x="645" y="355" text-anchor="middle" font-size="12">D = A·B + D</text>
<line class="vx-flow" x1="435" y1="350" x2="593" y2="350" marker-end="url(#g11-f3-h)"/>
<text class="vx-text-muted" x="515.0" y="392" text-anchor="middle">tcgen05.mma</text>
<rect class="vx-box" x="725" y="330" width="100" height="40"/>
<text class="vx-mono" x="775" y="355" text-anchor="middle" font-size="12">D</text>
<line class="vx-flow" x1="695" y1="350" x2="723" y2="350" marker-end="url(#g11-f3-h)"/>
<text class="vx-text-muted" x="710.0" y="392" text-anchor="middle">accumulator</text>
</svg>
<figcaption>Figure 3. How operand tiles reach NVIDIA's matrix unit. With <code>wmma</code>, the warp loads fragments into its registers and issues the multiply. From compute capability 8.0, <code>cp.async</code> copies global memory to shared memory without a register in between, and <code>ldmatrix</code> fills the fragments. From 9.0, one TMA copy moves a whole tile, and <code>wgmma</code> reads B straight from shared memory. On <code>sm_100a</code>, one thread issues <code>tcgen05.mma</code>, and the accumulator lives in a dedicated tensor memory.</figcaption>
</figure>

**Asynchronous copies** came first. PTX's `cp.async`, from compute capability 8.0, starts a copy of 4, 8 or 16 bytes from global to shared memory and returns control to the thread before the copy completes; the thread later waits on a group of copies or on a barrier object.[^ptx-cpasync] The Programming Guide, which calls the mechanism LDGSTS, notes that copying directly into shared memory also reduces register use, since the data no longer passes through a register on the way.[^pg-async] This is what makes the double buffering of G10's rung 10 cheap to issue: the next tile loads while the unit works on the current one.

The **Tensor Memory Accelerator** (TMA), from compute capability 9.0, copies a whole tile, of up to five dimensions, between global and shared memory. A **tensor map**, usually built on the host, describes the array's layout, and the hardware does the address arithmetic the lanes used to do, which the guide calls error-prone and repetitive.[^pg-async] PTX exposes it as `cp.async.bulk.tensor`.[^ptx-bulk] One instruction now describes a whole tile, where each `cp.async` moves at most 16 bytes for its thread.[^ptx-cpasync]

The unit grew as well. Hopper's `wgmma.mma_async` is issued by a **warpgroup**: four contiguous warps, 128 threads, the first of whose warp numbers is a multiple of four. Its B operand must be in shared memory, described by a matrix descriptor, and its A operand may be in registers or in shared memory, so B never passes through the lanes' registers at all. It is asynchronous: the warpgroup issues it, commits it to a group, and later waits for the group to finish. It requires the `sm_90a` target.[^ptx-wgmma]

The latest step in PTX 9.4 is the fifth-generation family, `tcgen05`. Its `tcgen05.mma` has single-thread semantics: one thread issuing it starts the whole matrix operation, unlike the warp-wide `mma.sync` and the warpgroup-wide `wgmma`. The accumulator lives in **tensor memory**, a dedicated on-chip memory that on `sm_100a` holds 128 rows by 512 columns of 32-bit cells per thread block.[^ptx-tcgen05] Down the rows of Figure 3, the issuing group goes from a warp to four warps to one thread, and fewer operands pass through the lanes' registers.

## The same idea on Apple and AMD GPUs

On Apple GPUs the matrix unit's interface is the **SIMD-group matrix**. The Metal Shading Language has supported the types since Metal 2.3: 8 × 8 matrices of `half`, `float`, and, from Metal 3.1, `bfloat`, with functions to load, store and compute `d = a × b + c` across a SIMD-group.[^msl] Apple's feature tables list SIMD-scoped matrix multiply operations for the Apple7 GPU family and later, the family of the M1; the owner's M4 Pro belongs to Apple9.[^fst] The specification describes these functions, not the hardware behind them, so this chapter makes no claim about that hardware.

Metal 4 adds a coarser layer. **Tensors** are multidimensional types with a layout of extents and strides, and a **cooperative tensor** is a tensor whose elements are split across the threads that share it, with a layout the specification calls device specific: a fragment under another name. The Metal Performance Primitives library provides `matmul2d`, a matrix product run by one thread, one SIMD-group or several, and the specification now suggests it in place of SIMD-group matrices.[^msl] One parameter of `matmul2d` matters for this chapter: `relaxed_precision`, false by default, which allows the operation to truncate the mantissa of `float` inputs before multiplying.[^msl] Apple made reduced precision an explicit request.

AMD's data-center GPUs of the CDNA line, from the MI100 on, have **MFMA** units (matrix fused multiply-add) that process a tile per instruction and run alongside the ordinary vector units. An example instruction, `v_mfma_f32_16x16x4f16`, multiplies 16-bit inputs with an inner dimension of 4 into a 16 × 16 tile of 32-bit accumulators held in vector registers. AMD's documentation lists INT8, FP16, BF16 and FP32 among the supported types, and describes the units as small systolic arrays, the design [G15](g15-systolic-arrays.md) studies.[^amd]

| | NVIDIA | Apple | AMD |
| --- | --- | --- | --- |
| Group that issues it | warp; warpgroup (`wgmma`); one thread (`tcgen05`) | SIMD-group; one thread to several SIMD-groups for `matmul2d` | not covered by the page cited |
| Where operands live | lanes' registers; shared memory for `wgmma`'s B | SIMD-group matrices, cooperative tensors | vector registers named in the instruction |
| Lane layout | unspecified for WMMA; a formula for PTX `mma` | unspecified; device specific for cooperative tensors | not covered by the page cited |
| Example shape | 16 × 16 × 16 (WMMA), 16 × 8 × 16 (`mma`) | 8 × 8 × 8 | 16 × 16 × 4 (`v_mfma_f32_16x16x4f16`) |

The vocabulary changes; the idea does not: one instruction, one small tile of fixed shape, operands held by a group of threads under a documented or opaque map.

## Naming the tile operation in a compiler's IR

A compiler cannot target any of these instructions until its IR has an operation that means "multiply this tile by that tile and accumulate". MLIR has them at several levels.

The most general is `vector.contract` in the `vector` dialect, which names two operands and an accumulator and says, through indexing maps and iterator types, which dimensions are kept and which are summed.[^mlir-vector] It knows nothing about warps; [M8](../mlir/m8-vectorization.md) covers it. One level down, the `gpu` dialect models the WMMA contract directly: `gpu.subgroup_mma_load_matrix` produces a `!gpu.mma_matrix` value, a fragment typed with its shape, element type and role (`"AOp"`, `"BOp"` or `"COp"`), `gpu.subgroup_mma_compute` multiplies and accumulates, and `gpu.subgroup_mma_store_matrix` writes the result back.[^mlir-gpu] The fourth example is one warp computing a 16 × 16 output tile with two steps of k, chained as in Figure 1; `mlir-opt` 18 parses, verifies and prints it.

--8<-- "includes/examples/gpu/g11-matrix-units/subgroup_mma.mlir.md"

On the owner's machine (Apple M4 Pro, `mlir-opt` 18.1.8, checked 2026-09-24), wrapping the same operations in a `gpu.module` and running `convert-gpu-to-nvvm` turned them into `nvvm.wmma.load`, `nvvm.wmma.mma` and `nvvm.wmma.store`, with each lane's share of the `f32` accumulator as a structure of eight `f32` values: 256 elements over 32 lanes. The fragment's type stayed opaque right up to the point where a target made it concrete.

MLIR also has operations at the explicit level. The `nvgpu` dialect has `nvgpu.ldmatrix`, `nvgpu.mma.sync` and `nvgpu.warpgroup.mma`, and the `amdgpu` dialect has `amdgpu.mfma` and `amdgpu.wmma`.[^mlir-nvgpu][^mlir-amdgpu] The layering mirrors the hardware's: a target-neutral contraction, a warp-level fragment whose layout is hidden, and target instructions whose layout is fixed. Choosing where Vortex enters that stack is a question for [M12](../mlir/m12-vortex-gpu-path.md).

## Precision: what the unit computes is not what `f32` code says

So far a matrix unit has looked like a faster way to do the same arithmetic. It is not the same arithmetic, by the vendors' own documents.

**The input types are narrower.** The WMMA table lists `__half`, `__nv_bfloat16`, `precision::tf32`, 8-bit integers and `double` as multiplicand types, and no `float`.[^wmma-types] The Programming Guide's table of Tensor Core input types for every compute capability from 7.5 to 12.x has columns for FP64, TF32, BF16, FP16, FP8, FP6, FP4, INT8 and INT4, and none for FP32.[^cc-types] **TF32** is a format with `f32`'s exponent range and at least 10 bits of mantissa; **BF16** keeps the same range with 7 bits.[^ptx-formats][^wmma-types] An `f32` value has 23. A kernel written over `f32` arrays reaches NVIDIA's tensor cores only by rounding its inputs to one of the narrower formats first, and WMMA makes the program do that conversion itself.[^wmma-types]

**The order and rounding of the sum are not specified.** For `f16`, `bf16`, `tf32` and 8-bit float inputs with 32-bit accumulators, PTX states that accumulation happens with at least single precision, and that the accumulation order, the rounding, and the handling of subnormal inputs are unspecified. For `f64`, each multiply and add is carried out with the precision of a fused multiply-add.[^ptx-mma] [Decision 56](../decisions/numbers.md#d56) requires each `f32` product and each sum to be rounded separately, in the program's order, and forbids wider evaluation: each of those statements permits something it forbids. Even `f64` on a tensor core is a contraction.

<figure class="vx-figure">
<svg viewBox="0 0 800 250" role="img" aria-label="The strict f32 dot product next to what an f32-in, f32-out matrix-unit instruction may do" aria-describedby="g11-f4-desc">
<title id="g11-f4-title">The strict f32 dot product next to what a matrix-unit instruction may do</title>
<desc id="g11-f4-desc">Two rows of four boxes. Top row, the strict path decision 56 requires: f32 inputs; each product rounded to f32; each sum rounded to f32; the sums taken in increasing k. Bottom row, a matrix-unit instruction with f32 data on both ends: inputs rounded to tf32 or bf16 first; products formed at the instruction's precision; accumulation at least f32; accumulation order, rounding and subnormal handling left unspecified by PTX. The first, second and fourth boxes of the bottom row are dashed, marking the steps that differ from the top row.</desc>
<defs><marker id="g11-f4-h" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="28">Vortex f32 kernel (decision 56)</text>
<text class="vx-text" x="20" y="148">Matrix-unit instruction fed from f32 arrays</text>
<rect class="vx-box" x="20" y="44" width="170" height="56"/>
<text class="vx-text" x="105" y="68" text-anchor="middle" font-size="13">f32 inputs</text>
<text class="vx-text-muted" x="105" y="88" text-anchor="middle">a[k], b[k]</text>
<rect class="vx-box" x="216" y="44" width="170" height="56"/>
<text class="vx-text" x="301" y="68" text-anchor="middle" font-size="13">product</text>
<text class="vx-text-muted" x="301" y="88" text-anchor="middle">rounded to f32</text>
<line class="vx-line" x1="192" y1="72" x2="212" y2="72" marker-end="url(#g11-f4-h)"/>
<rect class="vx-box" x="412" y="44" width="170" height="56"/>
<text class="vx-text" x="497" y="68" text-anchor="middle" font-size="13">sum</text>
<text class="vx-text-muted" x="497" y="88" text-anchor="middle">rounded to f32</text>
<line class="vx-line" x1="388" y1="72" x2="408" y2="72" marker-end="url(#g11-f4-h)"/>
<rect class="vx-box" x="608" y="44" width="170" height="56"/>
<text class="vx-text" x="693" y="68" text-anchor="middle" font-size="13">order</text>
<text class="vx-text-muted" x="693" y="88" text-anchor="middle">k = 0, 1, 2, ...</text>
<line class="vx-line" x1="584" y1="72" x2="604" y2="72" marker-end="url(#g11-f4-h)"/>
<rect class="vx-box-bad" x="20" y="164" width="170" height="56"/>
<text class="vx-text" x="105" y="188" text-anchor="middle" font-size="13">inputs narrowed</text>
<text class="vx-text-muted" x="105" y="208" text-anchor="middle">to tf32 or bf16</text>
<rect class="vx-box" x="216" y="164" width="170" height="56"/>
<text class="vx-text" x="301" y="188" text-anchor="middle" font-size="13">product</text>
<text class="vx-text-muted" x="301" y="208" text-anchor="middle">at the precision PTX sets</text>
<line class="vx-line" x1="192" y1="192" x2="212" y2="192" marker-end="url(#g11-f4-h)"/>
<rect class="vx-box" x="412" y="164" width="170" height="56"/>
<text class="vx-text" x="497" y="188" text-anchor="middle" font-size="13">sum</text>
<text class="vx-text-muted" x="497" y="208" text-anchor="middle">at least f32</text>
<line class="vx-line" x1="388" y1="192" x2="408" y2="192" marker-end="url(#g11-f4-h)"/>
<rect class="vx-box-bad" x="608" y="164" width="170" height="56"/>
<text class="vx-text" x="693" y="188" text-anchor="middle" font-size="13">order, rounding</text>
<text class="vx-text-muted" x="693" y="208" text-anchor="middle">unspecified</text>
<line class="vx-line" x1="584" y1="192" x2="604" y2="192" marker-end="url(#g11-f4-h)"/>
<text class="vx-text-muted" x="20" y="242">dashed: a step that can change the bits of the result</text>
</svg>
<figcaption>Figure 4. The dot product a Vortex <code>f32</code> kernel must compute, next to the one a tensor-core instruction computes from the same arrays, as PTX describes it. The dashed steps can change the result's bits; the product and sum steps may too, since PTX sets only lower bounds on their precision.</figcaption>
</figure>

The fifth example shows both effects on numbers small enough to check by hand. Sixteen products `a[k] × 1` with `a[k] = 1 + 2⁻¹²` sum to exactly 16.00390625 in strict `f32`. Rounded to 10 mantissa bits, the least PTX promises for TF32, `1 + 2⁻¹²` becomes 1, and the sum becomes 16. With `a[k] = 1 + 2⁻⁹`, TF32 keeps the value and BF16 loses it. The last line sums the same 16 values, 2²⁴, fourteen ones and −2²⁴, in two orders.

--8<-- "includes/examples/gpu/g11-matrix-units/narrowed_inputs.cpp.md"

Left to right, each 1 added to 2²⁴ is lost to rounding, since 2²⁴ + 1 is halfway between two `f32` values and rounds to the even one, 2²⁴, and the total is 0. The pairwise tree adds the ones to each other first and loses only one of them: 13. The exact answer is 14. Neither order is wrong by IEEE 754's rules; they are different computations, and a unit whose order is unspecified may perform either one, or another.

This is where [G10](g10-matmul-ladder.md#what-the-ladder-never-changes) said the free ride ends. The [philosophy](../philosophy.md#performance-philosophy) allows hardware-specific instructions while preserving the computation's declared semantics, and its [safety section](../philosophy.md#safety-philosophy) asks that numerical transformations that can change observable results require an explicit language mode or programmer permission. So a Vortex compiler may not route a strict `f32` matmul through a tensor core on its own initiative, however much faster it would be.

It may do so only under a mode the programmer asked for, and that mode needs a name, a documented meaning, and a way to test programs written under it: golden outputs, which decision 56 exists to keep reproducible, no longer hold bit for bit. Apple's `relaxed_precision` flag is one vendor's answer to the same design question.[^msl]

??? check "AMD's documentation lists FP32 among MFMA's input types. Does that alone make an FP32 MFMA instruction a legal target for a strict Vortex `f32` matmul?"

    No. Decision 56 is about more than the input type: it fixes the rounding of each product and each sum and the order of the sums. The source cited here lists FP32 as a supported type without saying how the instruction rounds or in what order it adds. Until a vendor document states that it matches IEEE 754 operation by operation, the answer is unknown, and an unknown must be treated as a change the programmer has to request.

## Measuring it

No matrix-unit timings are claimed here. The Mac this book is written on can run SIMD-group matrices, so collect your own:

1. Write four Metal kernels for a 1024 × 1024 × 1024 product: the warp-tiled SIMT kernel from [G10](g10-matmul-ladder.md) in `float`; the same tiling with `simdgroup_float8x8`; the same with `simdgroup_half8x8`, whose multiply-accumulate takes `half` inputs and a `half` accumulator; and, if your OS has Metal 4, `matmul2d` with `relaxed_precision` false and true. Compile them from source text at run time, which works without the offline Metal toolchain (see [G4](g4-memory-performance.md#measuring-it)).
2. Compute a reference product on the CPU with strict `f32` arithmetic in the order of the stage 10 kernel.
3. For each kernel, count the outputs that match the reference bit for bit and record the largest absolute difference. Do this before timing anything.
4. Time many runs and report the median and spread ([P1](../optimize/p1-measure-first.md)); convert to GFLOP/s as 2 × 1024³ divided by the median time.

| Kernel | Median time | GFLOP/s | Outputs bit-identical to strict | Largest difference |
| --- | --- | --- | --- | --- |
| SIMT, `float` | | | | |
| `simdgroup_float8x8` | | | | |
| `simdgroup_half8x8` | | | | |
| `matmul2d`, `relaxed_precision = false` | | | | |
| `matmul2d`, `relaxed_precision = true` | | | | |

Record the machine, the OS version and the date with the table. The row that matters most is the second: whether Apple's `float` path reproduces strict `f32` bit for bit is something the specification does not say, and only a measurement over many inputs can suggest an answer.

## For Vortex

!!! vortex "Exercise"

    **Build** a matrix-unit feasibility report for matmul-shaped loop nests in your compiler's IR: the stage 10 kernel, and any nest your compiler already recognizes as `c[i, j] += a[i, k] * b[k, j]` over fixed-shape arrays ([decision 43](../decisions/arrays.md#d43)). The report states, for each candidate instruction, whether it fits, what it would cost to feed, and whether Vortex may use it.

    1. A target table with one row per instruction: vendor, name, shape m × n × k, A and B types, accumulator type, the group that issues it, and whether its lane layout is documented. Fill it by hand for WMMA 16 × 16 × 16 (`half` into `float`), WMMA 16 × 16 × 8 (`tf32` into `float`), `mma.m16n8k16` (`f16` into `f32`), `simdgroup_float8x8` and `v_mfma_f32_16x16x4f16`, and give every entry the source it came from.
    2. Fit: for each row, whether M, N and K are multiples of m, n and k, and the remainder in each dimension when they are not.
    3. Count: the number of instructions, and the multiply-adds each one stands for.
    4. Feed: for a warp tile of fm × fn fragments given as input, the fragments loaded per step of k, the multiply-adds per element loaded, and the accumulator values per lane, rejecting warp tiles over a per-lane register budget that is also an input.
    5. Precision: for each row, the list of ways the instruction departs from [decision 56](../decisions/numbers.md#d56) for the kernel's element type (narrowed inputs, unspecified accumulation order, unspecified rounding, fused multiply-add), each with its source, and a verdict: "allowed", "blocked: needs an explicit mode", or "unknown: the documentation does not say", with unknown treated as blocked.
    6. One remark per candidate, as the [sixth principle](../philosophy.md#6-explain-performance-decisions) asks, such as "WMMA 16x16x16: fits, 64 instructions of 4096 multiply-adds, blocked: inputs narrowed from f32 to f16, accumulation order unspecified".

    **Not yet:** emitting any matrix-unit instruction or intrinsic, the syntax or semantics of a relaxed-precision mode, choosing tile shapes or warp tiles automatically, shared-memory staging and asynchronous copies, and choosing a GPU target ([M12](../mlir/m12-vortex-gpu-path.md) weighs the paths).

    **Proof that it works:**

    - Golden tests for the stage 10 kernel at `[f32; 64, 64]`: WMMA 16 × 16 × 16 fits with 64 instructions of 4,096 multiply-adds and is blocked; `mma.m16n8k16` fits with 128 of 2,048 and is blocked; `simdgroup_float8x8` fits with 512 of 512 and is unknown, so blocked.
    - A 70 × 70 × 70 product reports a remainder of 6 in every dimension for the 16 × 16 × 16 and 8 × 8 × 8 shapes, and never rounds it away.
    - Feed: with 8 × 8 × 8 fragments, a 2 × 2 warp tile reports 8 multiply-adds per element and 8 accumulator values per lane; 4 × 4 reports 16 and 32, and is rejected under a budget of 16.
    - Every "blocked" and "unknown" verdict names at least one source; removing a source from the table turns an "allowed" into "unknown", never the reverse.
    - A differential test: on a few hundred shapes and warp tiles, the report's counts match an independent brute-force counter like `fragment_reuse.cpp`.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does a matrix unit compute in one instruction?** `D = A × B + C` on one small tile of fixed shape, issued together by a warp, a warpgroup, a SIMD-group or, for `tcgen05`, one thread.
    - **How does a bigger product use it?** Cut into tiles; each output tile is a chain of instructions along k, each result feeding the next as `C`: (M / m)(N / n)(K / k) instructions in all.
    - **What does a WMMA fragment promise?** Only that load, multiply-accumulate and store work together; which lane holds which element is unspecified and may change between architectures.
    - **What does PTX's `mma` promise instead?** A formula from lane and register to row and column, which the program must follow and may rely on.
    - **How do you keep a matrix unit fed?** Hold a warp tile of fm × fn accumulators so each loaded fragment is used several times: 8 × fm × fn / (fm + fn) multiply-adds per element for 8 × 8 × 8 fragments, paid for in registers.
    - **What did `cp.async`, TMA, `wgmma` and `tcgen05` change?** Who moves the tiles and where operands live: copies that skip registers, whole-tile copies computed by hardware, operands read from shared memory, and accumulators in tensor memory.
    - **Why is a matrix unit an opt-in for Vortex?** Vendors document narrowed inputs and an unspecified accumulation order and rounding, which decision 56 forbids unless the programmer asks.

## Where this comes back

!!! next "You will use this again in"

    - [G12. Fusion case study: FlashAttention](g12-flashattention.md): *warp tile*, *asynchronous copy*, *tensor cores*
    - [G13. Tile languages](g13-tile-languages.md): *tile shape*, *boundary tiles*, *fragment*
    - [G14. Measuring GPU code](g14-measuring-gpu-code.md): *bit-identical outputs*, *GFLOP/s*
    - [G15. Beyond GPUs: systolic arrays and accelerators](g15-systolic-arrays.md): *fixed-shape hardware tile*, *systolic array*
    - [M8. Vectorization in MLIR](../mlir/m8-vectorization.md): *`vector.contract`*
    - [M10. MLIR for GPUs](../mlir/m10-mlir-for-gpus.md): *`gpu` dialect*, *lowering to NVVM*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *target table*, *explicit precision mode*

## Sources and further reading

Read NVIDIA's 2017 tensor-core post first for the picture, then the WMMA section of the Programming Guide and PTX's warp-level matrix chapter side by side, then the SIMD-group matrix and Metal Performance Primitives sections of the Metal Shading Language Specification.

[^n14]: Mark Appleyard and Michael Yokim, "Programming Tensor Cores in CUDA 9", NVIDIA Technical Blog, 17 October 2017: the sections on Volta's tensor cores and on the WMMA API. <https://developer.nvidia.com/blog/programming-tensor-cores-cuda-9/>
[^wmma]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 5.4.11, "Warp Matrix Functions", and 5.4.11.1, "Description". <https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-extensions.html#warp-matrix-functions>
[^wmma-types]: NVIDIA, "CUDA Programming Guide", v13.4.2, sections 5.4.11.2, "Alternate Floating Point", and 5.4.11.6, "Element Types and Matrix Sizes". <https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-extensions.html#element-types-and-matrix-sizes>
[^cc-types]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 5.1.3, Table 33, "Input Data Types Supported by Tensor Core Acceleration per Compute Capability". <https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/compute-capabilities.html#compute-capabilities-table-tensor-core-data-types-per-compute-capability>
[^pg-async]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 4.12, "Asynchronous Data Copies": 4.12.1, "Using LDGSTS", and 4.12.2, "Using the Tensor Memory Accelerator (TMA)". <https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/async-copies.html>
[^ptx-formats]: NVIDIA, "Parallel Thread Execution ISA", version 9.4, section 5.2.3, "Alternate Floating-Point Data Formats". <https://docs.nvidia.com/cuda/parallel-thread-execution/index.html#alternate-floating-point-data-formats>
[^ptx-wmma]: NVIDIA, "Parallel Thread Execution ISA", version 9.4, section 9.7.16, "Warp Level Matrix Multiply-Accumulate Instructions", and 9.7.16.4.1, "Matrix Fragments for WMMA". <https://docs.nvidia.com/cuda/parallel-thread-execution/index.html#warp-level-matrix-instructions>
[^ptx-mma-layout]: NVIDIA, "Parallel Thread Execution ISA", version 9.4, section 9.7.16.5.8, "Matrix Fragments for mma.m16n8k16 with floating point type". <https://docs.nvidia.com/cuda/parallel-thread-execution/index.html#warp-level-matrix-fragment-mma-16816-float>
[^ptx-mma]: NVIDIA, "Parallel Thread Execution ISA", version 9.4, section 9.7.16.5.14, "Multiply-and-Accumulate Instruction: mma": the "Precision and rounding" paragraphs and the target notes. <https://docs.nvidia.com/cuda/parallel-thread-execution/index.html#warp-level-matrix-instructions-mma>
[^ptx-ldmatrix]: NVIDIA, "Parallel Thread Execution ISA", version 9.4, section 9.7.16.5.15, "Warp-level matrix load instruction: ldmatrix". <https://docs.nvidia.com/cuda/parallel-thread-execution/index.html#warp-level-matrix-instructions-ldmatrix>
[^ptx-cpasync]: NVIDIA, "Parallel Thread Execution ISA", version 9.4, section 9.7.10.28.3.1, "cp.async". <https://docs.nvidia.com/cuda/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-cp-async>
[^ptx-bulk]: NVIDIA, "Parallel Thread Execution ISA", version 9.4, section 9.7.10.28.5.3, "cp.async.bulk.tensor". <https://docs.nvidia.com/cuda/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-cp-async-bulk-tensor>
[^ptx-wgmma]: NVIDIA, "Parallel Thread Execution ISA", version 9.4, section 9.7.17, "Asynchronous Warpgroup Level Matrix Multiply-Accumulate Instructions": 9.7.17.1, "Warpgroup", 9.7.17.5.1, "Register Fragments and Shared Memory Matrix Layouts", and the target notes of `wgmma.mma_async`. <https://docs.nvidia.com/cuda/parallel-thread-execution/index.html#asynchronous-warpgroup-level-matrix-instructions>
[^ptx-tcgen05]: NVIDIA, "Parallel Thread Execution ISA", version 9.4, sections 9.7.18.1, "Tensor Memory", and 9.7.18.10.10.1, "tcgen05.mma". <https://docs.nvidia.com/cuda/parallel-thread-execution/index.html#tensor-memory>
[^cutlass]: NVIDIA, "CUTLASS: Efficient GEMM in CUDA": the sections "Warp-level GEMM" and "Pipelining". <https://docs.nvidia.com/cutlass/latest/media/docs/cpp/efficient_gemm.html>
[^msl]: Apple, "Metal Shading Language Specification", version 4.1, 2026: sections 2.4, "SIMD-group Matrix Data Types"; 2.22, "Tensor Types", with 2.22.3, "Cooperative Tensor Type"; 6.8, "SIMD-Group Matrix Functions"; 7.1, "Execution Scopes"; and 7.2.1, "Matrix Multiplication", Table 7.4. <https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf>
[^fst]: Apple, "Metal Feature Set Tables": the GPU family table (M1-series in Apple7, M4-series in Apple9) and the row "SIMD-scoped matrix multiply operations". <https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf>
[^amd]: AMD, "Hardware implementation", HIP documentation, section "Matrix fused multiply-add (MFMA)". <https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html#matrix-fused-multiply-add-mfma>
[^mlir-vector]: MLIR Project, "'vector' Dialect", `vector.contract`. <https://mlir.llvm.org/docs/Dialects/Vector/#vectorcontract-vectorcontractionop>
[^mlir-gpu]: MLIR Project, "'gpu' Dialect", the `gpu.subgroup_mma_*` operations and the `MMAMatrix` type. <https://mlir.llvm.org/docs/Dialects/GPU/#gpusubgroup_mma_compute-gpusubgroupmmacomputeop>
[^mlir-nvgpu]: MLIR Project, "'nvgpu' Dialect": `nvgpu.ldmatrix`, `nvgpu.mma.sync` and `nvgpu.warpgroup.mma`. <https://mlir.llvm.org/docs/Dialects/NVGPU/>
[^mlir-amdgpu]: MLIR Project, "'amdgpu' Dialect": `amdgpu.mfma` and `amdgpu.wmma`. <https://mlir.llvm.org/docs/Dialects/AMDGPU/#amdgpumfma-amdgpumfmaop>
