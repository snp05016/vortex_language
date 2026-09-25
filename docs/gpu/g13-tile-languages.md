# G13. Tile languages

<p class="page-intro">In G10 the programmer chose, line by line, which thread computes which output and which bytes pass through shared memory. A tile language takes those choices away: the programmer writes one operation on a whole tile, and the compiler picks the thread mapping. This chapter shows what such a compiler decides, what the programmer still decides, and which promises a future Vortex GPU kernel would have to make before a compiler could take the same choices over.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 40 minutes · Builds on: [G10. The GPU matmul ladder](g10-matmul-ladder.md)</p>

???+ remember "Before you start, remember"

    ??? question "In G10's ladder, what does a block tile save a thread block from doing?"

        Rereading the same values from global memory. The block loads a slice of each operand into shared memory once, and every thread of the block reads that shared copy many times.

        Introduced in [G10. The GPU matmul ladder](g10-matmul-ladder.md#rung-3-shared-memory-tiling-the-block-tile).

    ??? question "What are the three levels of tile in G10's warp-tiled kernel, and what does each one divide?"

        The block tile, divided among the warps of a block; the warp tile, divided among the 32 lanes of a warp; and the thread tile, the small rectangle of outputs one lane keeps in registers.

        Introduced in [G10. The GPU matmul ladder](g10-matmul-ladder.md#rung-9-warp-tiling-a-tile-between-the-block-and-the-thread).

    ??? question "When an array's length is not a multiple of the block size, what must the kernel add, and which warps pay for it?"

        A guard such as `if (idx < n)` around the work, so that threads past the end do nothing. Only the warps that straddle the end of the array have lanes that fail the test.

        Introduced in [G2. The SIMT execution model](g2-simt.md#bringing-this-back-to-vortex-fixed-shapes-and-the-boundary-guard).

    ??? question "What does MLIR's transform dialect add to a module, and what does `transform.structured.tile_using_for` produce?"

        A second piece of IR, the schedule, that names operations in the first and rewrites them. Given tile sizes, `tile_using_for` wraps the matched operation in loops and leaves a smaller copy of the same operation inside them.

        Introduced in [G10. The GPU matmul ladder](g10-matmul-ladder.md#tiling-is-a-schedule-change-not-new-arithmetic).

    ??? question "May a Vortex compiler replace `sum += a[row, k] * b[k, column]` with an instruction that adds the products in a different order?"

        No. Each floating-point operation is one IEEE 754 operation, rounded once, in the written order. Relaxed modes may only come later, as an explicit opt-in.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Trace one tile program by hand, including its masked edge tiles, and explain why its answer can match a plain triple loop bit for bit.
    - Name who chooses each part of a GPU matmul (tile shape, thread mapping, staging, launch order, precision) in hand-written CUDA, Triton, CUDA Tile and CuTe.
    - Count the tiles a launch order loads, and explain why grouping programs into bands reduces that count.
    - Follow a Triton kernel through its compiler stages and point to the stage where threads first appear.
    - State the promises (shapes, aliasing, floating-point order) that a Vortex GPU kernel would need before a compiler could choose its thread mapping.

## One program per tile, worked by hand

Take a small problem: `c = a * b` for `f32` matrices of 10 × 10, with 4 × 4 tiles. In G10's style the programmer would start from a thread: work out its row and column from its block and thread ids, loop over `k`, and write one element. A **tile program** starts from a tile instead. It is one function, run once per output tile; each run is called a **program**, and it knows only its **program id**, the number that says which tile it owns. Triton's original paper describes the model this way: each kernel instance looks single-threaded and is parallelized automatically, so the CUDA-style barriers and inter-thread communication never appear in the source.[^triton-model]

For our problem, 10 rows need three tiles of 4 (4 + 4 + 2) and so do 10 columns, so nine programs run. Program (2, 1) owns rows 8 to 11 and columns 4 to 7. Its body, written out in words:

1. Set a 4 × 4 accumulator to zero.
2. For each of the three `k` tiles (`k` = 0 to 3, 4 to 7, 8 to 11): load a 4 × 4 tile of `a` (rows 8 to 11) and a 4 × 4 tile of `b` (columns 4 to 7), multiply the two tiles, and add the product into the accumulator.
3. Store the accumulator into rows 8 to 11, columns 4 to 7 of `c`.

Rows 10 and 11 do not exist, and in the last `k` step neither do `k` = 10 and 11. A thread-level kernel would guard each thread. A tile program cannot branch per element, because it never names an element; it attaches a **mask** to the load or store instead: a tile of booleans, one per position, saying which positions are inside the array.

Masked-off loads read a stated padding value (zero here) and masked-off stores write nothing. Triton's matrix-multiplication tutorial masks its loads along `K` with a padding value of zero and masks its final store along `M` and `N`.[^triton-tutorial] The 2019 paper builds the same idea into its IR as predicated instructions, since a tile's elements cannot be branched on one by one.[^triton-pred]

Count what program (2, 1) does. Its store writes 2 × 4 = 8 elements and masks 8. Each of its sums runs over 12 values of `k`, of which the last two multiply a padded zero by a padded zero. Over all nine programs, 9 × 16 = 144 output slots are computed and 100 are real. `masked_tiles.cpp` runs exactly this on the CPU, one program at a time, and compares every output with the plain triple loop:

--8<-- "includes/examples/gpu/g13-tile-languages/masked_tiles.cpp.md"

All 100 outputs match bit for bit, and the reason is worth stating carefully, because [decision 56](../decisions/numbers.md#d56) depends on it. Each output's sum still adds its products in the order `k` = 0, 1, 2, ..., 9; the tile program only changes which program holds the sum and when it is written.

The two extra terms come after the real ones and are `+0.0`. Adding `+0.0` to any value except `-0.0` returns that value unchanged, and a sum that starts at `+0.0` can never become `-0.0` under round-to-nearest, because two numbers that cancel exactly give `+0.0`.[^ieee-zero] So the padding is exact. The last line of the output is the contrast: splitting `K` into chunks with their own partial sums, then adding the chunks, is a different order of additions, and 40 of the 100 outputs change.

??? check "Program (0, 2) in the example owns rows 0 to 3 and columns 8 to 11. How many of its 16 stored positions are masked, and does any of its `k` steps load padded values of `a`?"

    Columns 10 and 11 do not exist, so 4 rows × 2 columns = 8 positions are masked and 8 are stored. Its tiles of `a` cover rows 0 to 3, all real, so along `M` nothing is padded; but in the last `k` step, `k` = 10 and 11 are past the end for every program, so that step loads padded zeros from both `a` and `b`.

## Who makes each choice

G10's ladder made five kinds of decision, all by hand: the shape of each tile, which thread computes which element (the **thread mapping**), when data is staged through shared memory and where the barriers go, the order in which tiles are launched, and which instructions do the arithmetic. A tile language is defined by which of those it moves from the programmer to the compiler. Four systems answer differently; the table after them compares three of them with G10's hand-written CUDA.

**Triton** is a language embedded in Python whose kernels are tile programs. Its 2019 paper introduced a C-like language (Triton-C), an IR with tile types (Triton-IR) and a just-in-time compiler with an autotuner (Triton-JIT).[^triton-parts] Today's compiler is built on MLIR: its README says the back end was rewritten to use MLIR in version 2.0, and it dumps a kernel at stages named `ttir`, `ttgir`, `llir` and then `ptx` or `amdgcn`.[^triton-readme] The programmer chooses tile sizes (they must be powers of two, because a tile's index range comes from `tl.arange`, whose bounds must be powers of two),[^triton-arange] the launch order, and the masks. The compiler chooses the thread mapping, the staging and the barriers.

**CUDA Tile** is NVIDIA's tile model inside CUDA itself. The CUDA Programming Guide contrasts it directly with the thread-level (SIMT) style: in a tile kernel the programmer loads a whole tile, operates on it and stores it, and the compiler takes over mapping tile operations to the threads of each block. It has two front ends, cuTile Python and CUDA Tile C++ (the `cuda::tiles` namespace, in the CUDA Toolkit from version 13.3), which share one IR, CUDA Tile IR. Every tile dimension must be a power of two and known at compile time.[^cuda-tile]

**CuTe**, part of NVIDIA's CUTLASS library, keeps the thread mapping with the programmer but changes how it is written. Its README describes C++ templates for layouts of threads and of data, and CUTLASS 4 added Python DSLs built on the same concepts.[^cutlass-repo] A CuTe **layout** is a pair of a shape and a stride: it maps a coordinate inside the shape to an offset, written `(Shape):(Stride)`, so `(4,2):(1,4)` is a 4 × 2 column-major layout, and shapes may nest to describe tiles of tiles.[^cute-layout] Instead of writing G10's index arithmetic, the programmer composes layouts, and the library computes the indices.

**Mojo** is a systems language whose compiler is built on MLIR and which targets CPUs and GPUs from several vendors.[^mojo-vision] Its introductory GPU tutorial writes kernels in the thread-level style, indexing with block and thread ids, and provides a layout-aware tensor type, `TileTensor`, for tiled data.[^mojo-gpu] Mojo's compatibility list includes Apple M1 to M5 GPUs as known compatible,[^mojo-req] which matters for a reader whose only GPU is an M4 Pro: of the four, it is the one whose kernels can run on that machine.

| Decision | Hand-written CUDA (G10) | CuTe | Triton | CUDA Tile |
| --- | --- | --- | --- | --- |
| Tile shape | Programmer, as index arithmetic | Programmer, as layout shapes | Programmer; powers of two | Programmer; powers of two, compile-time |
| Thread mapping | Programmer | Programmer, as composed layouts | Compiler | Compiler |
| Shared-memory staging and barriers | Programmer | Not covered here | Compiler | Not covered here |
| Edge handling | `if` guard per thread | Not covered here | Masks on loads and stores | Masked loads and stores, or padding modes |
| Launch order | Programmer | Programmer, with CUTLASS helper functions | Programmer, as program-id arithmetic | Not covered here |
| Precision of a matrix product | The kernel's own loop and compiler flags | Not covered here | `tl.dot`'s `input_precision`, default `"tf32"` on NVIDIA tensor-core GPUs | Not covered here |
| Tuning | By hand (G10 rung 8) | Not covered here | Autotuner times listed configurations | Optimization hints to the compiler |

Two cells deserve a second look. Launch order stays with the programmer even in Triton: the tutorial's grouped ordering is ordinary arithmetic on the program id, written in the kernel.[^triton-tutorial] And the thread-mapping row is what separates Triton and CUDA Tile, where a compiler makes the choice, from CuTe, where a library helps the programmer write it down.

## Launch order: a schedule above the tile

Every program in the worked example reads a strip of `a` tiles (its row of tiles, along `K`) and a strip of `b` tiles (its column of tiles). Programs that run at about the same time and share a strip can share the copy of it that sits in the GPU's L2 cache. Which programs run together depends on the **launch order**, the mapping from program ids to tiles, so the order changes how many distinct tiles a group of neighbouring programs needs.

Triton's tutorial gives a worked case: a 9 × 9 grid of output tiles, with `K` also nine tiles deep. In row-major order the first nine programs compute the first row of `c`. They need that one row strip of `a` (9 tiles) and all nine column strips of `b` (81 tiles): 90 tiles. In **grouped order** programs walk down a band of rows before moving to the next column, so with a band of three rows the first nine programs compute a 3 × 3 square. They need three row strips and three column strips: 27 + 27 = 54 tiles.[^triton-tutorial]

<figure class="vx-figure">
<svg viewBox="0 0 760 390" role="img" aria-label="The first nine programs of a 9 by 9 tile grid, in row-major and grouped order, and the tiles of A and B each order needs" aria-describedby="g13-f1-desc">
<title id="g13-f1-title">Row-major and grouped launch order on a 9 by 9 grid of output tiles</title>
<desc id="g13-f1-desc">Two 9 by 9 grids of output tiles of C. Left, row-major order: programs 0 to 8 fill the first row, so they need one row strip of A (9 tiles) and all nine column strips of B (81 tiles), 90 tiles in all. Right, grouped order with a group of 3 rows: programs 0 to 8 fill a 3 by 3 square, walking down each column of the band before moving right, so they need three row strips of A and three column strips of B, 27 plus 27, 54 tiles in all. Small bars beside each grid mark which strips are needed.</desc>
<text class="vx-text" x="90" y="46">Row-major order</text>
<text class="vx-text-muted" x="60" y="82" text-anchor="middle">A</text>
<text class="vx-text-muted" x="302" y="70">B</text>
<rect class="vx-cell-on" x="93" y="62" width="16" height="14"/>
<rect class="vx-cell-on" x="115" y="62" width="16" height="14"/>
<rect class="vx-cell-on" x="137" y="62" width="16" height="14"/>
<rect class="vx-cell-on" x="159" y="62" width="16" height="14"/>
<rect class="vx-cell-on" x="181" y="62" width="16" height="14"/>
<rect class="vx-cell-on" x="203" y="62" width="16" height="14"/>
<rect class="vx-cell-on" x="225" y="62" width="16" height="14"/>
<rect class="vx-cell-on" x="247" y="62" width="16" height="14"/>
<rect class="vx-cell-on" x="269" y="62" width="16" height="14"/>
<rect class="vx-cell-on" x="62" y="93" width="14" height="16"/>
<rect class="vx-box" x="62" y="115" width="14" height="16"/>
<rect class="vx-box" x="62" y="137" width="14" height="16"/>
<rect class="vx-box" x="62" y="159" width="14" height="16"/>
<rect class="vx-box" x="62" y="181" width="14" height="16"/>
<rect class="vx-box" x="62" y="203" width="14" height="16"/>
<rect class="vx-box" x="62" y="225" width="14" height="16"/>
<rect class="vx-box" x="62" y="247" width="14" height="16"/>
<rect class="vx-box" x="62" y="269" width="14" height="16"/>
<rect class="vx-box-accent" x="90" y="90" width="22" height="22"/>
<text class="vx-mono" x="101.0" y="105" text-anchor="middle">0</text>
<rect class="vx-box-accent" x="112" y="90" width="22" height="22"/>
<text class="vx-mono" x="123.0" y="105" text-anchor="middle">1</text>
<rect class="vx-box-accent" x="134" y="90" width="22" height="22"/>
<text class="vx-mono" x="145.0" y="105" text-anchor="middle">2</text>
<rect class="vx-box-accent" x="156" y="90" width="22" height="22"/>
<text class="vx-mono" x="167.0" y="105" text-anchor="middle">3</text>
<rect class="vx-box-accent" x="178" y="90" width="22" height="22"/>
<text class="vx-mono" x="189.0" y="105" text-anchor="middle">4</text>
<rect class="vx-box-accent" x="200" y="90" width="22" height="22"/>
<text class="vx-mono" x="211.0" y="105" text-anchor="middle">5</text>
<rect class="vx-box-accent" x="222" y="90" width="22" height="22"/>
<text class="vx-mono" x="233.0" y="105" text-anchor="middle">6</text>
<rect class="vx-box-accent" x="244" y="90" width="22" height="22"/>
<text class="vx-mono" x="255.0" y="105" text-anchor="middle">7</text>
<rect class="vx-box-accent" x="266" y="90" width="22" height="22"/>
<text class="vx-mono" x="277.0" y="105" text-anchor="middle">8</text>
<rect class="vx-box" x="90" y="112" width="22" height="22"/>
<rect class="vx-box" x="112" y="112" width="22" height="22"/>
<rect class="vx-box" x="134" y="112" width="22" height="22"/>
<rect class="vx-box" x="156" y="112" width="22" height="22"/>
<rect class="vx-box" x="178" y="112" width="22" height="22"/>
<rect class="vx-box" x="200" y="112" width="22" height="22"/>
<rect class="vx-box" x="222" y="112" width="22" height="22"/>
<rect class="vx-box" x="244" y="112" width="22" height="22"/>
<rect class="vx-box" x="266" y="112" width="22" height="22"/>
<rect class="vx-box" x="90" y="134" width="22" height="22"/>
<rect class="vx-box" x="112" y="134" width="22" height="22"/>
<rect class="vx-box" x="134" y="134" width="22" height="22"/>
<rect class="vx-box" x="156" y="134" width="22" height="22"/>
<rect class="vx-box" x="178" y="134" width="22" height="22"/>
<rect class="vx-box" x="200" y="134" width="22" height="22"/>
<rect class="vx-box" x="222" y="134" width="22" height="22"/>
<rect class="vx-box" x="244" y="134" width="22" height="22"/>
<rect class="vx-box" x="266" y="134" width="22" height="22"/>
<rect class="vx-box" x="90" y="156" width="22" height="22"/>
<rect class="vx-box" x="112" y="156" width="22" height="22"/>
<rect class="vx-box" x="134" y="156" width="22" height="22"/>
<rect class="vx-box" x="156" y="156" width="22" height="22"/>
<rect class="vx-box" x="178" y="156" width="22" height="22"/>
<rect class="vx-box" x="200" y="156" width="22" height="22"/>
<rect class="vx-box" x="222" y="156" width="22" height="22"/>
<rect class="vx-box" x="244" y="156" width="22" height="22"/>
<rect class="vx-box" x="266" y="156" width="22" height="22"/>
<rect class="vx-box" x="90" y="178" width="22" height="22"/>
<rect class="vx-box" x="112" y="178" width="22" height="22"/>
<rect class="vx-box" x="134" y="178" width="22" height="22"/>
<rect class="vx-box" x="156" y="178" width="22" height="22"/>
<rect class="vx-box" x="178" y="178" width="22" height="22"/>
<rect class="vx-box" x="200" y="178" width="22" height="22"/>
<rect class="vx-box" x="222" y="178" width="22" height="22"/>
<rect class="vx-box" x="244" y="178" width="22" height="22"/>
<rect class="vx-box" x="266" y="178" width="22" height="22"/>
<rect class="vx-box" x="90" y="200" width="22" height="22"/>
<rect class="vx-box" x="112" y="200" width="22" height="22"/>
<rect class="vx-box" x="134" y="200" width="22" height="22"/>
<rect class="vx-box" x="156" y="200" width="22" height="22"/>
<rect class="vx-box" x="178" y="200" width="22" height="22"/>
<rect class="vx-box" x="200" y="200" width="22" height="22"/>
<rect class="vx-box" x="222" y="200" width="22" height="22"/>
<rect class="vx-box" x="244" y="200" width="22" height="22"/>
<rect class="vx-box" x="266" y="200" width="22" height="22"/>
<rect class="vx-box" x="90" y="222" width="22" height="22"/>
<rect class="vx-box" x="112" y="222" width="22" height="22"/>
<rect class="vx-box" x="134" y="222" width="22" height="22"/>
<rect class="vx-box" x="156" y="222" width="22" height="22"/>
<rect class="vx-box" x="178" y="222" width="22" height="22"/>
<rect class="vx-box" x="200" y="222" width="22" height="22"/>
<rect class="vx-box" x="222" y="222" width="22" height="22"/>
<rect class="vx-box" x="244" y="222" width="22" height="22"/>
<rect class="vx-box" x="266" y="222" width="22" height="22"/>
<rect class="vx-box" x="90" y="244" width="22" height="22"/>
<rect class="vx-box" x="112" y="244" width="22" height="22"/>
<rect class="vx-box" x="134" y="244" width="22" height="22"/>
<rect class="vx-box" x="156" y="244" width="22" height="22"/>
<rect class="vx-box" x="178" y="244" width="22" height="22"/>
<rect class="vx-box" x="200" y="244" width="22" height="22"/>
<rect class="vx-box" x="222" y="244" width="22" height="22"/>
<rect class="vx-box" x="244" y="244" width="22" height="22"/>
<rect class="vx-box" x="266" y="244" width="22" height="22"/>
<rect class="vx-box" x="90" y="266" width="22" height="22"/>
<rect class="vx-box" x="112" y="266" width="22" height="22"/>
<rect class="vx-box" x="134" y="266" width="22" height="22"/>
<rect class="vx-box" x="156" y="266" width="22" height="22"/>
<rect class="vx-box" x="178" y="266" width="22" height="22"/>
<rect class="vx-box" x="200" y="266" width="22" height="22"/>
<rect class="vx-box" x="222" y="266" width="22" height="22"/>
<rect class="vx-box" x="244" y="266" width="22" height="22"/>
<rect class="vx-box" x="266" y="266" width="22" height="22"/>
<text class="vx-text-muted" x="90" y="312">A: 1 row strip &#215; 9 = 9 tiles</text>
<text class="vx-text-muted" x="90" y="330">B: 9 column strips &#215; 9 = 81 tiles</text>
<text class="vx-text-muted" x="90" y="348">first 9 programs need 90 tiles</text>
<text class="vx-text" x="470" y="46">Grouped order, 3 rows per band</text>
<text class="vx-text-muted" x="440" y="82" text-anchor="middle">A</text>
<text class="vx-text-muted" x="682" y="70">B</text>
<rect class="vx-cell-on" x="473" y="62" width="16" height="14"/>
<rect class="vx-cell-on" x="495" y="62" width="16" height="14"/>
<rect class="vx-cell-on" x="517" y="62" width="16" height="14"/>
<rect class="vx-box" x="539" y="62" width="16" height="14"/>
<rect class="vx-box" x="561" y="62" width="16" height="14"/>
<rect class="vx-box" x="583" y="62" width="16" height="14"/>
<rect class="vx-box" x="605" y="62" width="16" height="14"/>
<rect class="vx-box" x="627" y="62" width="16" height="14"/>
<rect class="vx-box" x="649" y="62" width="16" height="14"/>
<rect class="vx-cell-on" x="442" y="93" width="14" height="16"/>
<rect class="vx-cell-on" x="442" y="115" width="14" height="16"/>
<rect class="vx-cell-on" x="442" y="137" width="14" height="16"/>
<rect class="vx-box" x="442" y="159" width="14" height="16"/>
<rect class="vx-box" x="442" y="181" width="14" height="16"/>
<rect class="vx-box" x="442" y="203" width="14" height="16"/>
<rect class="vx-box" x="442" y="225" width="14" height="16"/>
<rect class="vx-box" x="442" y="247" width="14" height="16"/>
<rect class="vx-box" x="442" y="269" width="14" height="16"/>
<rect class="vx-box-accent" x="470" y="90" width="22" height="22"/>
<text class="vx-mono" x="481.0" y="105" text-anchor="middle">0</text>
<rect class="vx-box-accent" x="492" y="90" width="22" height="22"/>
<text class="vx-mono" x="503.0" y="105" text-anchor="middle">3</text>
<rect class="vx-box-accent" x="514" y="90" width="22" height="22"/>
<text class="vx-mono" x="525.0" y="105" text-anchor="middle">6</text>
<rect class="vx-box" x="536" y="90" width="22" height="22"/>
<rect class="vx-box" x="558" y="90" width="22" height="22"/>
<rect class="vx-box" x="580" y="90" width="22" height="22"/>
<rect class="vx-box" x="602" y="90" width="22" height="22"/>
<rect class="vx-box" x="624" y="90" width="22" height="22"/>
<rect class="vx-box" x="646" y="90" width="22" height="22"/>
<rect class="vx-box-accent" x="470" y="112" width="22" height="22"/>
<text class="vx-mono" x="481.0" y="127" text-anchor="middle">1</text>
<rect class="vx-box-accent" x="492" y="112" width="22" height="22"/>
<text class="vx-mono" x="503.0" y="127" text-anchor="middle">4</text>
<rect class="vx-box-accent" x="514" y="112" width="22" height="22"/>
<text class="vx-mono" x="525.0" y="127" text-anchor="middle">7</text>
<rect class="vx-box" x="536" y="112" width="22" height="22"/>
<rect class="vx-box" x="558" y="112" width="22" height="22"/>
<rect class="vx-box" x="580" y="112" width="22" height="22"/>
<rect class="vx-box" x="602" y="112" width="22" height="22"/>
<rect class="vx-box" x="624" y="112" width="22" height="22"/>
<rect class="vx-box" x="646" y="112" width="22" height="22"/>
<rect class="vx-box-accent" x="470" y="134" width="22" height="22"/>
<text class="vx-mono" x="481.0" y="149" text-anchor="middle">2</text>
<rect class="vx-box-accent" x="492" y="134" width="22" height="22"/>
<text class="vx-mono" x="503.0" y="149" text-anchor="middle">5</text>
<rect class="vx-box-accent" x="514" y="134" width="22" height="22"/>
<text class="vx-mono" x="525.0" y="149" text-anchor="middle">8</text>
<rect class="vx-box" x="536" y="134" width="22" height="22"/>
<rect class="vx-box" x="558" y="134" width="22" height="22"/>
<rect class="vx-box" x="580" y="134" width="22" height="22"/>
<rect class="vx-box" x="602" y="134" width="22" height="22"/>
<rect class="vx-box" x="624" y="134" width="22" height="22"/>
<rect class="vx-box" x="646" y="134" width="22" height="22"/>
<rect class="vx-box" x="470" y="156" width="22" height="22"/>
<rect class="vx-box" x="492" y="156" width="22" height="22"/>
<rect class="vx-box" x="514" y="156" width="22" height="22"/>
<rect class="vx-box" x="536" y="156" width="22" height="22"/>
<rect class="vx-box" x="558" y="156" width="22" height="22"/>
<rect class="vx-box" x="580" y="156" width="22" height="22"/>
<rect class="vx-box" x="602" y="156" width="22" height="22"/>
<rect class="vx-box" x="624" y="156" width="22" height="22"/>
<rect class="vx-box" x="646" y="156" width="22" height="22"/>
<rect class="vx-box" x="470" y="178" width="22" height="22"/>
<rect class="vx-box" x="492" y="178" width="22" height="22"/>
<rect class="vx-box" x="514" y="178" width="22" height="22"/>
<rect class="vx-box" x="536" y="178" width="22" height="22"/>
<rect class="vx-box" x="558" y="178" width="22" height="22"/>
<rect class="vx-box" x="580" y="178" width="22" height="22"/>
<rect class="vx-box" x="602" y="178" width="22" height="22"/>
<rect class="vx-box" x="624" y="178" width="22" height="22"/>
<rect class="vx-box" x="646" y="178" width="22" height="22"/>
<rect class="vx-box" x="470" y="200" width="22" height="22"/>
<rect class="vx-box" x="492" y="200" width="22" height="22"/>
<rect class="vx-box" x="514" y="200" width="22" height="22"/>
<rect class="vx-box" x="536" y="200" width="22" height="22"/>
<rect class="vx-box" x="558" y="200" width="22" height="22"/>
<rect class="vx-box" x="580" y="200" width="22" height="22"/>
<rect class="vx-box" x="602" y="200" width="22" height="22"/>
<rect class="vx-box" x="624" y="200" width="22" height="22"/>
<rect class="vx-box" x="646" y="200" width="22" height="22"/>
<rect class="vx-box" x="470" y="222" width="22" height="22"/>
<rect class="vx-box" x="492" y="222" width="22" height="22"/>
<rect class="vx-box" x="514" y="222" width="22" height="22"/>
<rect class="vx-box" x="536" y="222" width="22" height="22"/>
<rect class="vx-box" x="558" y="222" width="22" height="22"/>
<rect class="vx-box" x="580" y="222" width="22" height="22"/>
<rect class="vx-box" x="602" y="222" width="22" height="22"/>
<rect class="vx-box" x="624" y="222" width="22" height="22"/>
<rect class="vx-box" x="646" y="222" width="22" height="22"/>
<rect class="vx-box" x="470" y="244" width="22" height="22"/>
<rect class="vx-box" x="492" y="244" width="22" height="22"/>
<rect class="vx-box" x="514" y="244" width="22" height="22"/>
<rect class="vx-box" x="536" y="244" width="22" height="22"/>
<rect class="vx-box" x="558" y="244" width="22" height="22"/>
<rect class="vx-box" x="580" y="244" width="22" height="22"/>
<rect class="vx-box" x="602" y="244" width="22" height="22"/>
<rect class="vx-box" x="624" y="244" width="22" height="22"/>
<rect class="vx-box" x="646" y="244" width="22" height="22"/>
<rect class="vx-box" x="470" y="266" width="22" height="22"/>
<rect class="vx-box" x="492" y="266" width="22" height="22"/>
<rect class="vx-box" x="514" y="266" width="22" height="22"/>
<rect class="vx-box" x="536" y="266" width="22" height="22"/>
<rect class="vx-box" x="558" y="266" width="22" height="22"/>
<rect class="vx-box" x="580" y="266" width="22" height="22"/>
<rect class="vx-box" x="602" y="266" width="22" height="22"/>
<rect class="vx-box" x="624" y="266" width="22" height="22"/>
<rect class="vx-box" x="646" y="266" width="22" height="22"/>
<text class="vx-text-muted" x="470" y="312">A: 3 row strips &#215; 9 = 27 tiles</text>
<text class="vx-text-muted" x="470" y="330">B: 3 column strips &#215; 9 = 27 tiles</text>
<text class="vx-text-muted" x="470" y="348">first 9 programs need 54 tiles</text>
</svg>
<figcaption>Figure 1. The same nine programs, launched in two orders. Numbers are program ids; filled bars beside each grid are the strips of A (left) and B (top) those programs read, each strip nine tiles long along K. The counts are the Triton tutorial's; <code>grouped_launch_order.cpp</code> reproduces them.</figcaption>
</figure>

`grouped_launch_order.cpp` maps program ids to tiles for any band height and counts the tiles the first nine programs need. A band of 1 is row-major order; a band of 9, the whole grid height, is column-major order:

--8<-- "includes/examples/gpu/g13-tile-languages/grouped_launch_order.cpp.md"

The counts fall from 90 to 54 and rise back to 90. Row-major order wastes `b` and column-major order wastes `a`; the square band balances them. Work the band of 2 by hand to check the table: programs 0 to 8 cover rows 0 and 1 of columns 0 to 3, plus row 0 of column 4, so they need 2 row strips and 5 column strips, (2 + 5) × 9 = 63 tiles. The program's last line checks that every order still launches each of the 81 tiles exactly once: a launch order is a permutation of the same work, never a change to it.

The tutorial reports what this is worth on one machine: more than 10 percent on some hardware, for example from 220 to 245 TFLOPS on an A100, for its `f16` matmul.[^triton-tutorial] That is one kernel on one GPU, quoted for its size, not a rule. CUTLASS offers the same idea as a family of functions that remap thread blocks to tiles so that blocks running together touch the same tiles of global memory.[^cutlass-gemm]

??? check "With a band of 4 rows, the first nine programs need 63 tiles, the same as with a band of 2. Which programs are they, and why do both bands land on the same count?"

    With a band of 4, programs 0 to 3 cover rows 0 to 3 of column 0, programs 4 to 7 rows 0 to 3 of column 1, and program 8 row 0 of column 2: 4 row strips and 3 column strips, (4 + 3) × 9 = 63. With a band of 2 it was 2 row strips and 5 column strips. The count depends only on rows plus columns covered, and 4 + 3 = 2 + 5.

## Inside a tile compiler: where threads appear

If the programmer never names a thread, the compiler must invent the mapping, and it has to do so somewhere between the tile program and the machine code. Triton makes that point visible, because its stages can be printed.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="Triton's compilation stages from a Python tile program to PTX or AMD GPU code, with the point where threads appear" aria-describedby="g13-f2-desc">
<title id="g13-f2-title">Where the thread mapping appears in Triton</title>
<desc id="g13-f2-desc">Five boxes left to right joined by arrows: a Python function decorated with triton.jit, one program per tile; ttir, the tt dialect, holding whole tiles and no threads; ttgir, the TritonGPU dialect, where each tile carries a layout giving its thread, warp and block tile sizes; llir, LLVM IR with per-thread code; and PTX for NVIDIA or AMD GPU code. A dashed line between ttir and ttgir marks where threads first appear. A band below lists the decisions the 2019 Triton paper assigns to the compiler: prefetching, tile-level peephole rewrites, hierarchical tiling, memory coalescing, shared-memory allocation and barrier insertion, with an autotuner searching tile sizes.</desc>
<defs><marker id="g13-f2-h" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="24">Triton's stages (the names its kernel dump uses)</text>
<rect class="vx-box" x="20" y="50" width="128" height="120" rx="4"/>
<text class="vx-mono" x="84.0" y="76" text-anchor="middle">@triton.jit</text>
<text class="vx-text-muted" x="84.0" y="100" text-anchor="middle">Python function</text>
<text class="vx-text-muted" x="84.0" y="117" text-anchor="middle">one program per tile</text>
<line class="vx-line" x1="148" y1="110" x2="166" y2="110" marker-end="url(#g13-f2-h)"/>
<rect class="vx-box" x="168" y="50" width="128" height="120" rx="4"/>
<text class="vx-mono" x="232.0" y="76" text-anchor="middle">ttir</text>
<text class="vx-text-muted" x="232.0" y="100" text-anchor="middle">tt dialect</text>
<text class="vx-text-muted" x="232.0" y="117" text-anchor="middle">whole tiles,</text>
<text class="vx-text-muted" x="232.0" y="134" text-anchor="middle">no threads</text>
<line class="vx-line" x1="296" y1="110" x2="314" y2="110" marker-end="url(#g13-f2-h)"/>
<rect class="vx-box-accent" x="316" y="50" width="128" height="120" rx="4"/>
<text class="vx-mono" x="380.0" y="76" text-anchor="middle">ttgir</text>
<text class="vx-text-muted" x="380.0" y="100" text-anchor="middle">ttg dialect</text>
<text class="vx-text-muted" x="380.0" y="117" text-anchor="middle">tiles + layouts:</text>
<text class="vx-text-muted" x="380.0" y="134" text-anchor="middle">thread, warp and</text>
<text class="vx-text-muted" x="380.0" y="151" text-anchor="middle">block tile sizes</text>
<line class="vx-line" x1="444" y1="110" x2="462" y2="110" marker-end="url(#g13-f2-h)"/>
<rect class="vx-box" x="464" y="50" width="128" height="120" rx="4"/>
<text class="vx-mono" x="528.0" y="76" text-anchor="middle">llir</text>
<text class="vx-text-muted" x="528.0" y="100" text-anchor="middle">LLVM IR</text>
<text class="vx-text-muted" x="528.0" y="117" text-anchor="middle">per-thread code</text>
<line class="vx-line" x1="592" y1="110" x2="610" y2="110" marker-end="url(#g13-f2-h)"/>
<rect class="vx-box" x="612" y="50" width="128" height="120" rx="4"/>
<text class="vx-mono" x="676.0" y="76" text-anchor="middle">ptx / amdgcn</text>
<text class="vx-text-muted" x="676.0" y="100" text-anchor="middle">NVIDIA PTX or</text>
<text class="vx-text-muted" x="676.0" y="117" text-anchor="middle">AMD GPU code</text>
<line class="vx-line" x1="306" y1="40" x2="306" y2="182" stroke-dasharray="4 4"/>
<text class="vx-text-accent" x="296" y="198" text-anchor="end">no threads yet</text>
<text class="vx-text-accent" x="316" y="198">threads appear</text>
<rect class="vx-box" x="20" y="214" width="720" height="96" rx="4"/>
<text class="vx-text" x="36" y="238">Decisions the 2019 paper gives the compiler (Tillet, Kung and Cox, section 5):</text>
<text class="vx-text-muted" x="36" y="258">prefetching, tile-level peephole rewrites, hierarchical tiling (tile, micro-tile, nano-tile),</text>
<text class="vx-text-muted" x="36" y="278">memory coalescing, shared-memory allocation from live ranges, barrier insertion by dataflow;</text>
<text class="vx-text-muted" x="36" y="298">an autotuner searches the tiling parameters. The programmer writes none of these.</text>
</svg>
<figcaption>Figure 2. A Triton kernel passes through two tile-level forms before any thread exists. The TritonGPU form attaches a layout to each tile, the same thread, warp and block tile sizes G10 chose by hand; only after that does code become per-thread LLVM IR. The band lists the passes the original paper describes; today's compiler organises the work as the MLIR stages above, but the division of labour is the same.</figcaption>
</figure>

The first form, `ttir`, is in the `tt` dialect: operations on whole tiles, one program per tile, and no threads. The second, `ttgir`, is in the TritonGPU dialect (`ttg`), and there every tile carries an **encoding**, an attribute that says how the tile's elements are spread over the hardware. The simplest, the blocked encoding, is described in Triton's source as three tuples giving the elements owned by each thread, each warp and each block: its parameters are `sizePerThread`, `threadsPerWarp`, `warpsPerCTA` and `order`, the axis that changes fastest.[^ttg-attrs] Those are G10's thread tile, warp tile and block tile, written as a type instead of as index arithmetic.

The same file defines other encodings for tiles that feed NVIDIA's and AMD's matrix instructions and for tiles held in shared memory, and the dialect has a `convert_layout` operation that moves a tile from one encoding to another.[^ttg-attrs][^ttg-ops] Only the third form, `llir`, is per-thread LLVM IR, which the LLVM back ends of [G9](g9-gpu-compilers-in-llvm.md) turn into PTX or AMD GPU code.

So "the compiler chooses the thread mapping" has a precise meaning: choosing an encoding for every tile, and paying for a `convert_layout` wherever two operations want different ones. A load wants neighbouring lanes on neighbouring addresses ([G4](g4-memory-performance.md#which-index-runs-across-the-warp)); a matrix instruction wants the fixed per-lane fragments of [G11](g11-matrix-units.md). G10's programmer settled that conflict by hand at every rung; here a pass settles it, tile by tile.

??? check "The blocked encoding `sizePerThread = [2, 2]`, `threadsPerWarp = [8, 4]`, `warpsPerCTA = [2, 1]` covers how large a region before its pattern repeats, and how many threads does it use?"

    One warp covers (2 × 8) × (2 × 4) = 16 × 8 elements, and two warps stacked along the first axis cover 32 × 8. It uses 8 × 4 = 32 threads per warp and 2 warps, 64 threads. A larger tile repeats the pattern, so each thread owns more than four elements.

The 2019 paper lists the passes that do this work.[^triton-passes] Two are target-independent: **prefetching**, which issues the next iteration's tile loads before the current iteration's arithmetic (G10's rung 10), and a tile-level peephole pass, which can cancel a transpose of a transpose.

Four are target-specific. **Hierarchical tiling** splits each tile into micro-tiles and nano-tiles to fit the machine: the paper's name for G10's block, warp and thread tiles. **Memory coalescing** orders the threads inside a micro-tile so that a column load touches fewer memory transactions. **Shared-memory allocation** computes the live range of each tile that should be staged and packs them into shared memory with a linear-time storage allocator, the same live-range idea as [C3](../backend/c3-linear-scan.md)'s register allocator. **Shared-memory synchronization** inserts barriers by a forward dataflow analysis that looks for read-after-write and write-after-read hazards on shared memory, the kind of analysis [O4](../optimize/o4-dataflow.md) builds.

The autotuner then searches the parameters of hierarchical tiling. In the paper it was an exhaustive search over powers of two: 32 to 128 for tile sizes, 8 to 32 for micro-tiles, 1 to 4 for nano-tiles.[^triton-autotune] Today's tutorial lists configurations by hand (block sizes, a group size, `num_warps` for the warps per program and `num_stages` for the pipeline depth of the loads) and the autotuner times each one on the actual problem size.[^triton-tutorial] That is G10's rung 8, run by the language's runtime instead of by a person with a spreadsheet.

## CUDA Tile: the same contract, stated in C++ and Python

CUDA Tile is worth reading closely, because its guide states the contract between program and compiler more explicitly than a tutorial does.[^cuda-tile] A tile's type fixes its element type and its shape, and every dimension of the shape is a compile-time power of two. Matrix products come in two forms, a plain matrix multiply and a multiply-accumulate that adds into an existing tile, and the guide's own kernels follow a common pattern of accumulating in `f32` whatever the input precision and converting on the store.

Edges are handled in the load and store operations, not in the program's control flow. In C++, a plain `load` or `store` assumes the whole tile is inside the array, and a partly out-of-bounds access is undefined behavior; the masked forms handle edge tiles, with a choice of padding values for loads. In Python, `ct.load` takes a padding mode, and a store checks its bounds and discards the positions outside the array.[^cuda-tile] The guide prefers the unmasked forms when the array divides evenly by the tile. So CUDA Tile makes the programmer say, at every load, whether the tile might cross an edge: a fact a Vortex compiler could work out by itself, as the next section shows.

## Edges, powers of two and fixed shapes

The power-of-two rule has a consequence for shapes like 70. In G2, a guard `if (idx < n)` could protect any block size. A tile language with power-of-two tiles cannot pick a tile that divides 70 (70 = 2 × 5 × 7, so no tile edge larger than 2 divides it), so every tile edge of 4 or more leaves a partial tile at the end, and the kernel must mask it.

For a 70 × 70 output and 16 × 16 tiles, each axis needs 5 tiles covering 80 positions, so 5 × 5 = 25 programs compute 6,400 slots for 4,900 real outputs. With 32 × 32 tiles, 3 tiles per axis cover 96, and 9 programs compute 9,216 slots. Larger tiles reuse more ([G10](g10-matmul-ladder.md#rungs-4-and-5-register-tiling-the-thread-tile)) but waste more at the edge.

MLIR's structured tiling handles the same edge differently. `edge_tiles.mlir` tiles a 10 × 10 matmul by 4 along `M` and `N` (a size of 0 leaves `K` untiled) and prints the result:

--8<-- "includes/examples/gpu/g13-tile-languages/edge_tiles.mlir.md"

There is no mask. Each loop computes the size of its current tile with `affine.min`, the smaller of 4 and the distance left to the edge, and the inner `linalg.matmul` works on subviews whose sizes are `?`, unknown until run time: 4, 4 and then 2. The tiling keeps the operation exact and makes the tile shape dynamic. A tile language does the opposite: it keeps the tile shape fixed, which the hardware mapping wants, and makes the data dynamic with masks. Both compute the same 100 outputs.

<figure class="vx-figure">
<svg viewBox="0 0 760 360" role="img" aria-label="Edge tiles of a 10 by 10 output covered by 4 by 4 tiles, as masked slots and as affine.min sizes" aria-describedby="g13-f3-desc">
<title id="g13-f3-title">Edge tiles: masks and shrinking tiles</title>
<desc id="g13-f3-desc">Left, a 12 by 12 grid of slots divided into nine 4 by 4 tiles. The top-left 10 by 10 cells are real outputs; the two extra rows and columns, 44 cells, are dashed: masked slots that are computed or skipped but never stored. The top-left tile is full, the tiles on the right and bottom edges are partly masked, and the bottom-right corner tile is masked in both directions. Right, three rows for the loop offsets 0, 4 and 8, each showing four cells of which min(4, 10 minus the offset) are real: 4, 4 and 2.</desc>
<rect class="vx-box" x="60" y="60" width="20" height="20"/>
<rect class="vx-box" x="80" y="60" width="20" height="20"/>
<rect class="vx-box" x="100" y="60" width="20" height="20"/>
<rect class="vx-box" x="120" y="60" width="20" height="20"/>
<rect class="vx-box" x="140" y="60" width="20" height="20"/>
<rect class="vx-box" x="160" y="60" width="20" height="20"/>
<rect class="vx-box" x="180" y="60" width="20" height="20"/>
<rect class="vx-box" x="200" y="60" width="20" height="20"/>
<rect class="vx-box" x="220" y="60" width="20" height="20"/>
<rect class="vx-box" x="240" y="60" width="20" height="20"/>
<rect class="vx-box-bad" x="260" y="60" width="20" height="20"/>
<rect class="vx-box-bad" x="280" y="60" width="20" height="20"/>
<rect class="vx-box" x="60" y="80" width="20" height="20"/>
<rect class="vx-box" x="80" y="80" width="20" height="20"/>
<rect class="vx-box" x="100" y="80" width="20" height="20"/>
<rect class="vx-box" x="120" y="80" width="20" height="20"/>
<rect class="vx-box" x="140" y="80" width="20" height="20"/>
<rect class="vx-box" x="160" y="80" width="20" height="20"/>
<rect class="vx-box" x="180" y="80" width="20" height="20"/>
<rect class="vx-box" x="200" y="80" width="20" height="20"/>
<rect class="vx-box" x="220" y="80" width="20" height="20"/>
<rect class="vx-box" x="240" y="80" width="20" height="20"/>
<rect class="vx-box-bad" x="260" y="80" width="20" height="20"/>
<rect class="vx-box-bad" x="280" y="80" width="20" height="20"/>
<rect class="vx-box" x="60" y="100" width="20" height="20"/>
<rect class="vx-box" x="80" y="100" width="20" height="20"/>
<rect class="vx-box" x="100" y="100" width="20" height="20"/>
<rect class="vx-box" x="120" y="100" width="20" height="20"/>
<rect class="vx-box" x="140" y="100" width="20" height="20"/>
<rect class="vx-box" x="160" y="100" width="20" height="20"/>
<rect class="vx-box" x="180" y="100" width="20" height="20"/>
<rect class="vx-box" x="200" y="100" width="20" height="20"/>
<rect class="vx-box" x="220" y="100" width="20" height="20"/>
<rect class="vx-box" x="240" y="100" width="20" height="20"/>
<rect class="vx-box-bad" x="260" y="100" width="20" height="20"/>
<rect class="vx-box-bad" x="280" y="100" width="20" height="20"/>
<rect class="vx-box" x="60" y="120" width="20" height="20"/>
<rect class="vx-box" x="80" y="120" width="20" height="20"/>
<rect class="vx-box" x="100" y="120" width="20" height="20"/>
<rect class="vx-box" x="120" y="120" width="20" height="20"/>
<rect class="vx-box" x="140" y="120" width="20" height="20"/>
<rect class="vx-box" x="160" y="120" width="20" height="20"/>
<rect class="vx-box" x="180" y="120" width="20" height="20"/>
<rect class="vx-box" x="200" y="120" width="20" height="20"/>
<rect class="vx-box" x="220" y="120" width="20" height="20"/>
<rect class="vx-box" x="240" y="120" width="20" height="20"/>
<rect class="vx-box-bad" x="260" y="120" width="20" height="20"/>
<rect class="vx-box-bad" x="280" y="120" width="20" height="20"/>
<rect class="vx-box" x="60" y="140" width="20" height="20"/>
<rect class="vx-box" x="80" y="140" width="20" height="20"/>
<rect class="vx-box" x="100" y="140" width="20" height="20"/>
<rect class="vx-box" x="120" y="140" width="20" height="20"/>
<rect class="vx-box" x="140" y="140" width="20" height="20"/>
<rect class="vx-box" x="160" y="140" width="20" height="20"/>
<rect class="vx-box" x="180" y="140" width="20" height="20"/>
<rect class="vx-box" x="200" y="140" width="20" height="20"/>
<rect class="vx-box" x="220" y="140" width="20" height="20"/>
<rect class="vx-box" x="240" y="140" width="20" height="20"/>
<rect class="vx-box-bad" x="260" y="140" width="20" height="20"/>
<rect class="vx-box-bad" x="280" y="140" width="20" height="20"/>
<rect class="vx-box" x="60" y="160" width="20" height="20"/>
<rect class="vx-box" x="80" y="160" width="20" height="20"/>
<rect class="vx-box" x="100" y="160" width="20" height="20"/>
<rect class="vx-box" x="120" y="160" width="20" height="20"/>
<rect class="vx-box" x="140" y="160" width="20" height="20"/>
<rect class="vx-box" x="160" y="160" width="20" height="20"/>
<rect class="vx-box" x="180" y="160" width="20" height="20"/>
<rect class="vx-box" x="200" y="160" width="20" height="20"/>
<rect class="vx-box" x="220" y="160" width="20" height="20"/>
<rect class="vx-box" x="240" y="160" width="20" height="20"/>
<rect class="vx-box-bad" x="260" y="160" width="20" height="20"/>
<rect class="vx-box-bad" x="280" y="160" width="20" height="20"/>
<rect class="vx-box" x="60" y="180" width="20" height="20"/>
<rect class="vx-box" x="80" y="180" width="20" height="20"/>
<rect class="vx-box" x="100" y="180" width="20" height="20"/>
<rect class="vx-box" x="120" y="180" width="20" height="20"/>
<rect class="vx-box" x="140" y="180" width="20" height="20"/>
<rect class="vx-box" x="160" y="180" width="20" height="20"/>
<rect class="vx-box" x="180" y="180" width="20" height="20"/>
<rect class="vx-box" x="200" y="180" width="20" height="20"/>
<rect class="vx-box" x="220" y="180" width="20" height="20"/>
<rect class="vx-box" x="240" y="180" width="20" height="20"/>
<rect class="vx-box-bad" x="260" y="180" width="20" height="20"/>
<rect class="vx-box-bad" x="280" y="180" width="20" height="20"/>
<rect class="vx-box" x="60" y="200" width="20" height="20"/>
<rect class="vx-box" x="80" y="200" width="20" height="20"/>
<rect class="vx-box" x="100" y="200" width="20" height="20"/>
<rect class="vx-box" x="120" y="200" width="20" height="20"/>
<rect class="vx-box" x="140" y="200" width="20" height="20"/>
<rect class="vx-box" x="160" y="200" width="20" height="20"/>
<rect class="vx-box" x="180" y="200" width="20" height="20"/>
<rect class="vx-box" x="200" y="200" width="20" height="20"/>
<rect class="vx-box" x="220" y="200" width="20" height="20"/>
<rect class="vx-box" x="240" y="200" width="20" height="20"/>
<rect class="vx-box-bad" x="260" y="200" width="20" height="20"/>
<rect class="vx-box-bad" x="280" y="200" width="20" height="20"/>
<rect class="vx-box" x="60" y="220" width="20" height="20"/>
<rect class="vx-box" x="80" y="220" width="20" height="20"/>
<rect class="vx-box" x="100" y="220" width="20" height="20"/>
<rect class="vx-box" x="120" y="220" width="20" height="20"/>
<rect class="vx-box" x="140" y="220" width="20" height="20"/>
<rect class="vx-box" x="160" y="220" width="20" height="20"/>
<rect class="vx-box" x="180" y="220" width="20" height="20"/>
<rect class="vx-box" x="200" y="220" width="20" height="20"/>
<rect class="vx-box" x="220" y="220" width="20" height="20"/>
<rect class="vx-box" x="240" y="220" width="20" height="20"/>
<rect class="vx-box-bad" x="260" y="220" width="20" height="20"/>
<rect class="vx-box-bad" x="280" y="220" width="20" height="20"/>
<rect class="vx-box" x="60" y="240" width="20" height="20"/>
<rect class="vx-box" x="80" y="240" width="20" height="20"/>
<rect class="vx-box" x="100" y="240" width="20" height="20"/>
<rect class="vx-box" x="120" y="240" width="20" height="20"/>
<rect class="vx-box" x="140" y="240" width="20" height="20"/>
<rect class="vx-box" x="160" y="240" width="20" height="20"/>
<rect class="vx-box" x="180" y="240" width="20" height="20"/>
<rect class="vx-box" x="200" y="240" width="20" height="20"/>
<rect class="vx-box" x="220" y="240" width="20" height="20"/>
<rect class="vx-box" x="240" y="240" width="20" height="20"/>
<rect class="vx-box-bad" x="260" y="240" width="20" height="20"/>
<rect class="vx-box-bad" x="280" y="240" width="20" height="20"/>
<rect class="vx-box-bad" x="60" y="260" width="20" height="20"/>
<rect class="vx-box-bad" x="80" y="260" width="20" height="20"/>
<rect class="vx-box-bad" x="100" y="260" width="20" height="20"/>
<rect class="vx-box-bad" x="120" y="260" width="20" height="20"/>
<rect class="vx-box-bad" x="140" y="260" width="20" height="20"/>
<rect class="vx-box-bad" x="160" y="260" width="20" height="20"/>
<rect class="vx-box-bad" x="180" y="260" width="20" height="20"/>
<rect class="vx-box-bad" x="200" y="260" width="20" height="20"/>
<rect class="vx-box-bad" x="220" y="260" width="20" height="20"/>
<rect class="vx-box-bad" x="240" y="260" width="20" height="20"/>
<rect class="vx-box-bad" x="260" y="260" width="20" height="20"/>
<rect class="vx-box-bad" x="280" y="260" width="20" height="20"/>
<rect class="vx-box-bad" x="60" y="280" width="20" height="20"/>
<rect class="vx-box-bad" x="80" y="280" width="20" height="20"/>
<rect class="vx-box-bad" x="100" y="280" width="20" height="20"/>
<rect class="vx-box-bad" x="120" y="280" width="20" height="20"/>
<rect class="vx-box-bad" x="140" y="280" width="20" height="20"/>
<rect class="vx-box-bad" x="160" y="280" width="20" height="20"/>
<rect class="vx-box-bad" x="180" y="280" width="20" height="20"/>
<rect class="vx-box-bad" x="200" y="280" width="20" height="20"/>
<rect class="vx-box-bad" x="220" y="280" width="20" height="20"/>
<rect class="vx-box-bad" x="240" y="280" width="20" height="20"/>
<rect class="vx-box-bad" x="260" y="280" width="20" height="20"/>
<rect class="vx-box-bad" x="280" y="280" width="20" height="20"/>
<rect class="vx-box-strong" x="60" y="60" width="80" height="80" fill-opacity="0"/>
<rect class="vx-box-strong" x="140" y="60" width="80" height="80" fill-opacity="0"/>
<rect class="vx-box-strong" x="220" y="60" width="80" height="80" fill-opacity="0"/>
<rect class="vx-box-strong" x="60" y="140" width="80" height="80" fill-opacity="0"/>
<rect class="vx-box-strong" x="140" y="140" width="80" height="80" fill-opacity="0"/>
<rect class="vx-box-strong" x="220" y="140" width="80" height="80" fill-opacity="0"/>
<rect class="vx-box-strong" x="60" y="220" width="80" height="80" fill-opacity="0"/>
<rect class="vx-box-strong" x="140" y="220" width="80" height="80" fill-opacity="0"/>
<rect class="vx-box-strong" x="220" y="220" width="80" height="80" fill-opacity="0"/>
<text class="vx-text" x="60" y="30">10 x 10 output, 4 x 4 tiles: 9 programs</text>
<text class="vx-text-accent" x="100" y="105" text-anchor="middle">full</text>
<text class="vx-text-accent" x="260" y="105" text-anchor="middle">edge</text>
<text class="vx-text-accent" x="100" y="265" text-anchor="middle">edge</text>
<text class="vx-text-accent" x="260" y="265" text-anchor="middle">corner</text>
<text class="vx-text-muted" x="60" y="326">solid cells: 100 real outputs; dashed: 44 masked slots</text>
<text class="vx-text" x="420" y="30">The same edge, as a loop (edge_tiles.mlir)</text>
<text class="vx-mono" x="420" y="95">i = 0</text>
<rect class="vx-cell-on" x="490" y="80" width="20" height="20"/>
<rect class="vx-cell-on" x="510" y="80" width="20" height="20"/>
<rect class="vx-cell-on" x="530" y="80" width="20" height="20"/>
<rect class="vx-cell-on" x="550" y="80" width="20" height="20"/>
<text class="vx-mono" x="590" y="95">min(4, 10 - 0) = 4</text>
<text class="vx-mono" x="420" y="155">i = 4</text>
<rect class="vx-cell-on" x="490" y="140" width="20" height="20"/>
<rect class="vx-cell-on" x="510" y="140" width="20" height="20"/>
<rect class="vx-cell-on" x="530" y="140" width="20" height="20"/>
<rect class="vx-cell-on" x="550" y="140" width="20" height="20"/>
<text class="vx-mono" x="590" y="155">min(4, 10 - 4) = 4</text>
<text class="vx-mono" x="420" y="215">i = 8</text>
<rect class="vx-cell-on" x="490" y="200" width="20" height="20"/>
<rect class="vx-cell-on" x="510" y="200" width="20" height="20"/>
<rect class="vx-box-bad" x="530" y="200" width="20" height="20"/>
<rect class="vx-box-bad" x="550" y="200" width="20" height="20"/>
<text class="vx-mono" x="590" y="215">min(4, 10 - 8) = 2</text>
<text class="vx-text-muted" x="420" y="270">A tile language keeps the tile 4 wide and masks</text>
<text class="vx-text-muted" x="420" y="288">2 lanes; the MLIR loop shrinks the last tile</text>
<text class="vx-text-muted" x="420" y="306">to 2. Both compute the same 10 outputs.</text>
</svg>
<figcaption>Figure 3. Two ways to handle an edge a tile does not divide. A tile language keeps every tile the same power-of-two shape and masks the positions outside the array (left). MLIR's tiling keeps the operation and shrinks the last tile with <code>affine.min</code> (right).</figcaption>
</figure>

Vortex has an advantage here that neither Triton nor MLIR's general tiling can assume. Every array dimension in Vortex is an integer constant expression ([decision 11](../decisions/arrays.md#d11)), so for any candidate tile size a compiler knows, before generating anything, how many tiles an axis needs, whether the last one is partial, and exactly which positions its mask excludes. For `multiply`'s 64 × 64 arrays, every power-of-two tile up to 64 divides evenly, so no mask is needed at all; for a 70 × 70 array, a mask is needed and its shape is known. What a Triton kernel checks at run time with a comparison per element, a Vortex compiler can decide once, by division.

## What a tile operation must still promise

Raising the unit of a program from a thread to a tile does not remove the questions G10 asked of every rung; it asks them once, of the operation. Three matter.

The first is **aliasing**. A compiler may stage an output tile through shared memory, split it across warps or pipeline its loads only if the output does not overlap either input; if it might, one program's stores could change what another program loads, and no order of programs would be safe. [Decision 25](../decisions/references.md#d25) settles this for `multiply`'s signature: a variable borrowed by a `&mut` argument must not appear in any other argument of the same call. A tile-level Vortex kernel would rely on the same rule, checked once at the call, as [G10](g10-matmul-ladder.md#what-the-ladder-never-changes) already did for its whole ladder.

The second is **floating-point order**, and here tile languages make a choice Vortex does not allow. A thread-level loop names its order of additions. A tile-level `dot` names none, and the compiler decides how to lower it. Triton's `tl.dot` has an `input_precision` argument whose default on NVIDIA GPUs with tensor cores is `"tf32"`, and its documentation warns that `f32` inputs may then be truncated to TF32, a 19-bit format, without rounding, which can bias the result.[^triton-dot]

So a Triton matmul on `f32` data does not, by default, compute what `multiply` computes. Under [decision 56](../decisions/numbers.md#d56) and the [philosophy page](../philosophy.md#3-do-not-surprise-the-programmer), a Vortex tile operation could lower to a matrix instruction only when the programmer has opted in; [G11](g11-matrix-units.md) takes up what that opt-in would mean.

The third is the **reduction axis**. Tiling `K` and walking its tiles in order, as the worked example did, keeps every sum's order, and padding with `+0.0` at the end is exact. Splitting `K` across programs or warps is different: CUTLASS describes a split-K strategy that launches extra thread blocks, one set per partition of `K`, and a sliced-K strategy that splits the block's `K` tile across warps, both to create parallelism when `M` and `N` are small.[^cutlass-gemm]

Each adds partial sums in a new order, and `masked_tiles.cpp` shows 40 of 100 outputs changing when it does. A tile compiler for Vortex could choose any tiling of `M` and `N`, any launch order and any staging on its own, but must keep `K` sequential for every output unless a relaxed mode is granted.

??? check "Why is padding `K` with zeros at the end of a sum exact, while splitting `K` into two halves and adding the halves is not?"

    Padding appends `+0.0` terms after the real ones, and adding `+0.0` to a sum that started at `+0.0` never changes it, so each output performs the same additions in the same order. Splitting computes two partial sums and then adds them, which regroups the additions; since floating-point addition is not associative, the rounding differs and some outputs change.

## Measuring it

This chapter claims no timings of its own. Triton runs on NVIDIA GPUs of compute capability 8.0 or newer and on AMD GPUs with ROCm,[^triton-readme] so its measurements need a rented machine; Mojo lists Apple M-series GPUs as known compatible,[^mojo-req] so a thread-level Mojo kernel can run on the M4 Pro.

1. On a rented NVIDIA or AMD GPU, run the Triton tutorial's matmul twice: once as written, and once with the group size set to 1, which is row-major order. Keep every other setting the same.
2. Check both outputs against a reference before timing anything, as [P1](../optimize/p1-measure-first.md) requires.
3. Time many runs, report the median and its spread, and grow the matrix until the difference stops changing.
4. On NVIDIA, read the L2 hit rate of both runs from Nsight Compute ([G14](g14-measuring-gpu-code.md)), and check that the grouped order's is higher.

| GPU | M = N = K | Group size | Median time | TFLOP/s | L2 hit rate |
| --- | --- | --- | --- | --- | --- |
| | | 1 | | | |
| | | 8 | | | |

Record the GPU, the driver, the Triton version and the date with the table.

## For Vortex

!!! vortex "Exercise"

    **Build** a tile-plan report for loop nests in your compiler's IR: given a nest with constant bounds that writes an array (such as `multiply`'s), and a candidate tile size for each loop, state what a tile-level lowering of that nest would have to do and what it would be allowed to do, without generating any GPU code.

    1. Classify each loop of the nest: **parallel** if different iterations write different elements of the output, **reduction** if iterations accumulate into the same element (the `k` loop of `multiply`). Use the dependence information of [P6](../optimize/p6-dependence-analysis.md), not the loop's name.
    2. For each loop and a candidate power-of-two tile size, report the number of tiles, whether the last tile is partial, its real extent, and, over the whole output, the number of slots computed and the number masked off.
    3. For each reduction loop, record whether a proposed tiling keeps each element's order of accumulation. Tiling it and walking the tiles in order keeps it; distributing its tiles across programs or warps does not, and your report must reject that under [decision 56](../decisions/numbers.md#d56) with a one-line reason.
    4. Record which fact makes staging safe: that the output parameter is `&mut` and so, by [decision 25](../decisions/references.md#d25), cannot overlap an input. If the nest writes an array that is also read in the same nest, the report must say so and refuse to plan.
    5. For a two-dimensional grid of output tiles and a band height, list the order in which tiles would be launched, and check that it is a permutation of all tiles.
    6. A remark for every plan, as the [sixth principle](../philosophy.md#6-explain-performance-decisions) asks, such as "tiled row and column by 16: 16 programs, no mask; k kept in order".

    **Not yet:** generating GPU code or MLIR for the plan ([M12](../mlir/m12-vortex-gpu-path.md) weighs the paths); choosing tile sizes automatically ([P15](../optimize/p15-choosing-parameters.md)); matrix instructions or any reduced-precision lowering ([G11](g11-matrix-units.md)); designing syntax for a tile-level kernel.

    **Proof that it works:**

    - Golden test for `multiply` at `[f32; 64, 64]` with tiles 16 × 16 × 8: `row` and `column` parallel, `k` a reduction kept in order; 16 programs; no partial tile; 0 masked slots.
    - The same nest at 70 × 70 with 16 × 16 output tiles: 5 tiles per axis, last tile extent 6, 25 programs, 6,400 slots computed and 1,500 masked; with 32 × 32 tiles, 9 programs, 9,216 slots and 4,316 masked.
    - A plan that splits `k` across programs is rejected, and the reason names decision 56.
    - A nest that reads and writes the same array, such as an in-place update `x[i] = x[i] + x[i - 1]`, is refused, and the reason names the overlap.
    - For a 9 × 9 grid, the launch order for band heights 1, 2, 3, 4 and 9 is a permutation of all 81 tiles, and for band 1 it is row-major order.
    - A differential test: for a few hundred random shapes and power-of-two tile sizes, the masked-slot count equals the count from a brute-force loop over every slot.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is a tile program?** A function run once per output tile, which knows only its program id and operates on whole tiles; the compiler, not the programmer, decides which thread holds which element.
    - **How does a tile program handle an edge that the tile does not divide?** With masks on its loads and stores: positions outside the array load a padding value and store nothing. MLIR's tiling instead shrinks the last tile with `affine.min`.
    - **Why can a masked tile program match the plain triple loop bit for bit?** Each output still adds its products in the same order, and the padded terms are `+0.0` added at the end, which never changes a sum that started at `+0.0`.
    - **Where does the thread mapping appear in Triton?** In the TritonGPU form, where each tile gets an encoding giving the elements per thread, threads per warp and warps per block: G10's three tile levels as a type.
    - **Why does grouped launch order load fewer tiles?** The programs running together cover a square of the output, so they share row strips of `a` and column strips of `b`; 9 programs on a 9 × 9 grid need 54 tiles instead of 90.
    - **What would a Vortex tile-level kernel have to promise, and what does Vortex already give it?** No aliasing between output and inputs (decision 25), sequential accumulation along the reduction axis (decision 56), and compile-time shapes (decision 11), which turn every mask into a fact known before code generation.

## Where this comes back

!!! next "You will use this again in"

    - [G14. Measuring GPU code](g14-measuring-gpu-code.md): *L2 hit rate*, *reproducing a published number*
    - [G15. Beyond GPUs: systolic arrays and accelerators](g15-systolic-arrays.md): *tile*, *edge tiles*, *utilization*
    - [M9. Schedules as IR: the transform dialect](../mlir/m9-transform-dialect.md): `tile_using_for`, *schedule separate from the operation*
    - [M10. MLIR for GPUs](../mlir/m10-mlir-for-gpus.md): *mapping tiles to blocks and threads*
    - [M11. End-to-end ML compilers](../mlir/m11-ml-compilers.md): *who chooses the schedule*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *tile-level or thread-level kernels*
    - [P15. Choosing parameters: models or search](../optimize/p15-choosing-parameters.md): *autotuning*, *tile-size search*

## Sources and further reading

Read the Triton paper first, sections 3 and 5, for the programming model and the passes; then the matrix-multiplication tutorial; then the CUDA Tile chapter of the CUDA Programming Guide for the same contract stated by a hardware vendor.

[^triton-model]: Philippe Tillet, H. T. Kung and David Cox, "Triton: An Intermediate Language and Compiler for Tiled Neural Network Computations", MAPL 2019, pp. 10-19, doi:10.1145/3315508.3329973, section 3.3, "Programming Model". <https://www.eecs.harvard.edu/~htk/publication/2019-mapl-tillet-kung-cox.pdf>
[^triton-pred]: Tillet, Kung and Cox, MAPL 2019, section 3.1 (the `@` predication prefix of Triton-C) and section 4.3, "Support for Tile-Level Control-Flow Analysis" (predicated instructions in Triton-IR). <https://www.eecs.harvard.edu/~htk/publication/2019-mapl-tillet-kung-cox.pdf>
[^triton-parts]: Tillet, Kung and Cox, MAPL 2019, section 1: the contributions list naming Triton-C, Triton-IR and Triton-JIT. <https://www.eecs.harvard.edu/~htk/publication/2019-mapl-tillet-kung-cox.pdf>
[^triton-passes]: Tillet, Kung and Cox, MAPL 2019, section 5.1, "Machine-Independent Passes" (pre-fetching, tile-level peephole optimization), and section 5.2, "Machine-Dependent Passes" (hierarchical tiling, memory coalescing, shared memory allocation, shared memory synchronization). <https://www.eecs.harvard.edu/~htk/publication/2019-mapl-tillet-kung-cox.pdf>
[^triton-autotune]: Tillet, Kung and Cox, MAPL 2019, section 5.3, "Auto-tuner". <https://www.eecs.harvard.edu/~htk/publication/2019-mapl-tillet-kung-cox.pdf>
[^triton-tutorial]: Triton Project, "Matrix Multiplication" tutorial: the sections on L2 cache optimizations (the 9 × 9 example and the A100 figure), pointer arithmetic, masked loads and the store mask, and the autotuning configurations. <https://triton-lang.org/main/getting-started/tutorials/03-matrix-multiplication.html>
[^triton-arange]: Triton Project, `triton.language.arange`: both bounds must be powers of two. <https://triton-lang.org/main/python-api/generated/triton.language.arange.html>
[^triton-dot]: Triton Project, `triton.language.dot`: the `input_precision` argument, its `"tf32"` default on NVIDIA GPUs with tensor cores, and the warning about truncating `f32` inputs. <https://triton-lang.org/main/python-api/generated/triton.language.dot.html>
[^triton-readme]: Triton Project, README on GitHub: supported hardware, the rewrite of the back end on MLIR in version 2.0, and the `TRITON_KERNEL_DUMP` stages. <https://github.com/triton-lang/triton>
[^ttg-attrs]: Triton Project, `TritonGPUAttrDefs.td`, main branch: `BlockedEncodingAttr` and the other encoding definitions. <https://github.com/triton-lang/triton/blob/main/include/triton/Dialect/TritonGPU/IR/TritonGPUAttrDefs.td>
[^ttg-ops]: Triton Project, `TritonGPUOps.td`, main branch: `TTG_ConvertLayoutOp`. <https://github.com/triton-lang/triton/blob/main/include/triton/Dialect/TritonGPU/IR/TritonGPUOps.td>
[^cuda-tile]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.4, "Writing Tile Kernels": the comparison with SIMT kernels, the tile type and power-of-two shapes, section 2.4.6, "Loading and Storing Tiles" (masked and padded forms, and the preference for unmasked forms when the array divides evenly), and the matrix multiply and multiply-accumulate operations with their `f32` accumulation pattern. <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-tile-kernels.html>
[^cutlass-repo]: NVIDIA, CUTLASS repository README: CuTe and the CUTLASS 4 Python DSLs. <https://github.com/NVIDIA/cutlass>
[^cute-layout]: NVIDIA, "CuTe Layouts", CUTLASS documentation. <https://docs.nvidia.com/cutlass/latest/media/docs/cpp/cute/01_layout.html>
[^cutlass-gemm]: NVIDIA, "CUTLASS: Efficient GEMM in CUDA", section "Optimizations": threadblock rasterization, split-K and sliced-K. <https://docs.nvidia.com/cutlass/latest/media/docs/cpp/efficient_gemm.html>
[^mojo-vision]: Modular, "Mojo vision": the MLIR-based compiler (KGEN) and targeting CPUs and GPUs from several vendors. <https://mojolang.org/docs/vision/>
[^mojo-gpu]: Modular, "Get started with GPU programming" (Mojo GPU tutorial): thread-level kernels and `TileTensor`. <https://max.modular.com/gpu/intro-tutorial>
[^mojo-req]: Modular, Mojo system requirements, "GPU compatibility": Apple M1 to M5 listed as known compatible. <https://mojolang.org/docs/requirements/>
[^ieee-zero]: David Goldberg, "What Every Computer Scientist Should Know About Floating-Point Arithmetic", ACM Computing Surveys 23(1), 1991: the section "Denormalized Numbers" (with gradual underflow, x − y is zero only when x = y) and the note that x − x = +0 except when rounding toward minus infinity. <https://docs.oracle.com/cd/E19957-01/806-3568/ncg_goldberg.html>
