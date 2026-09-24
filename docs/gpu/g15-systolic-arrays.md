# G15. Beyond GPUs: systolic arrays and accelerators

<p class="page-intro">A systolic array is a grid of arithmetic cells that never fetches an operand from memory more than once: each value is pushed from neighbour to neighbour and used again at every cell it passes through. This chapter builds one by hand, then asks what a compiler has to decide before such a grid can run at all.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 30 minutes · Builds on: [G1. Throughput machines](g1-throughput-machines.md), [G11. Matrix units](g11-matrix-units.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is the difference between a latency-optimized and a throughput-optimized design?"

        A latency-optimized design spends its transistor budget on making one thread's chain of dependent work finish as fast as possible. A throughput-optimized design spends it on running many independent pieces of work at once, so that the total amount finished per second is high even though no one piece is fast.

        Introduced in [G1. Throughput machines](g1-throughput-machines.md#a-budget-spent-two-ways).

    ??? question "What turns reuse into locality?"

        Reuse belongs to the computation: the same data used by more than one step. Locality is reuse that is actually captured, because the second use happens before the data is gone. A design cannot create or destroy reuse, only decide whether the reuse a computation already has gets captured or wasted.

        Introduced in [P7. Loop transformations](../optimize/p7-loop-transformations.md#the-same-work-in-a-different-order).

    ??? question "What does the roofline bound say a kernel's attainable rate cannot exceed?"

        The smaller of the machine's peak compute rate and its peak bandwidth times the kernel's operational intensity (flops per byte moved from memory). Raising operational intensity, by reusing each byte more times before it leaves the chip, can move a kernel from the bandwidth-bound side of the bound to the compute-bound side.

        Introduced in [G1. Throughput machines](g1-throughput-machines.md#the-same-ceiling-the-roofline-bound-already-described), from [P3. The roofline model](../optimize/p3-roofline.md).

    ??? question "In what order are the elements of a `[f32; 64, 64]` array stored?"

        Row after row, the last index varying fastest: `m[i, j + 1]` sits next to `m[i, j]`, and `m[i + 1, j]` is a whole row away, and both dimensions are fixed at compile time.

        Introduced in [Arrays and shapes, decision 43](../decisions/arrays.md#d43).

!!! goals "In this chapter"

    - Explain why a systolic array's cells never fetch an operand from memory more than once, using a 3 by 3 example worked by hand.
    - Trace an output-stationary array's cycle-by-cycle schedule and recognize its diagonal wavefront.
    - Compute the utilization a *K* by *K* array reaches on an *N* by *N* matrix multiplication when *K* does not divide *N*.
    - Explain what the TPU's designers traded away for a fixed, deterministic pipeline of multiply-adds, and what they gained by trading it.
    - Frame an accelerator compiler's job as choosing a static schedule and layout before the program runs, rather than adapting to it while the program runs.

## A grid that never asks memory twice

Take the smallest matrix multiplication worth doing by hand: two 3 by 3 matrices, `A` and `B`, producing `C = A * B`. Element `C[0][0]` is `A[0][0]*B[0][0] + A[0][1]*B[1][0] + A[0][2]*B[2][0]`, three multiplications and two additions. A single core computes it by reading six values out of memory (or a register file standing in for memory), one pair at a time, and writing one value back. Every one of the nine elements of `C` repeats that pattern: 27 multiplications and 18 additions, and a great deal of that traffic is the same numbers read again. `A[0][0]` is needed by `C[0][0]`, `C[0][1]` and `C[0][2]`; `B[0][0]` is needed by `C[0][0]`, `C[1][0]` and `C[2][0]`. A single core that reads straight from memory pays for that reuse every time, because it has nowhere to keep a value between one use and the next except by re-fetching it or spending register file space on it.

Now imagine nine tiny arithmetic cells arranged in a 3 by 3 grid, one cell for each element of `C`, and wire each cell to its neighbour above, below, left and right. Feed the rows of `A` in from the left, one row per grid row, and the columns of `B` in from the top, one column per grid column, but stagger their entry: row `i` of `A` starts one cycle later than row `i - 1`, and column `j` of `B` starts one cycle later than column `j - 1`. Every cycle, a value already inside the grid moves one cell in its direction of travel, `A`'s values moving right and `B`'s values moving down, and whichever cell a pair of values reaches together does one multiply-add. This is a **systolic array**: a grid of simple processing cells that pumps operands through itself in a fixed rhythm, the way a heart pumps blood, each cell doing one piece of arithmetic on whatever arrives and passing the operands on.[^kung1982] No cell ever asks memory for an operand a second time. Every value `A[i][k]` is read from memory once and then reused by every cell in row `i` that still needs it, simply by being handed along; the same is true of every `B[k][j]` down its column.

This is the concrete form of the abstract idea from [P8](../optimize/p8-cache-blocking.md): reuse only pays off when it turns into locality, when the second use of a value arrives before that value is gone. A cache captures locality by guessing which values are worth holding onto. A systolic array captures it by construction: the wiring between cells guarantees that a value is exactly where the next cell that needs it will look, on exactly the cycle it needs to be there, because the timing was chosen to make that true.

## The schedule, cycle by cycle

The staggered entry above needs a precise rule, and the rule is what makes the array **output-stationary**: each cell owns one element of the output, accumulates it in place across several cycles, and never sends it anywhere until it is finished. For a *K* by *K* array computing the product of two *K* by *K* matrices, cell `(i, j)` needs the *K* products `A[i][0]*B[0][j]`, `A[i][1]*B[1][j]`, ..., `A[i][K-1]*B[K-1][j]`, summed in some order. Stagger the inputs so that `A[i][k]` enters row `i` at cycle `i + k` and then moves one cell right per cycle, and `B[k][j]` enters column `j` at cycle `j + k` and then moves one cell down per cycle. Both values reach cell `(i, j)` on the same cycle: `A[i][k]` has travelled `j` cells since entering at cycle `i + k`, arriving at cycle `i + k + j`; `B[k][j]` has travelled `i` cells since entering at cycle `j + k`, arriving at the same cycle `i + j + k`. Cell `(i, j)` does one multiply-add whenever a pair arrives, for `k` running from 0 to `K - 1`, and is finished after its last pair, at cycle `i + j + K - 1`.

`systolic_sim.cpp` simulates exactly this schedule for a 3 by 3 array multiplying two fixed 3 by 3 integer matrices, printing which cell is active on every cycle and the value of `k` it is working on, then checks the result against a direct triple loop.

--8<-- "includes/examples/gpu/g15-systolic-arrays/systolic_sim.cpp.md"

Read the cycle log against the arrival rule above: at cycle 0, only cell `(0, 0)` has anything to do, because it is the only cell where `i + j = 0`. At cycle 1, cells `(0, 1)` and `(1, 0)` join in, both at `i + j = 1`. The set of active cells at any cycle is exactly the cells on one anti-diagonal, `i + j` constant, and that diagonal sweeps from the top-left corner toward the bottom-right corner as the cycle count rises. The last cell to start, `(2, 2)`, does not receive its first pair until cycle 4, and does not finish its last one until cycle 6; the whole array needs `3K - 2` cycles, 7 for `K = 3`, from the first value entering to the last value leaving.

<figure class="vx-figure">
<svg viewBox="0 0 760 440" role="img" aria-label="A 3 by 3 output-stationary systolic array at cycle 2, with a triangular wavefront of active cells" aria-describedby="g15-f1-desc">
<title id="g15-f1-title">The wavefront of an output-stationary systolic array at cycle 2</title>
<desc id="g15-f1-desc">A 3 by 3 grid of cells, rows 0 to 2 top to bottom and columns 0 to 2 left to right. Three arrows enter from the left, one per row, labelled that row i's values of A enter at cycle i and move one cell right per cycle. Three arrows enter from the top, one per column, labelled that column j's values of B enter at cycle j and move one cell down per cycle. At the pictured cycle, 2, the six cells where the row index plus the column index is 0, 1 or 2 are highlighted and each shows the k value of the product it is forming: (0,0) k=2, (0,1) k=1, (1,0) k=1, (0,2) k=0, (1,1) k=0, (2,0) k=0. The remaining three cells, (1,2), (2,1) and (2,2), are dim: no pair has reached them yet. A dashed diagonal line separates the highlighted triangle from the dim one, labelled as the wavefront, and a caption box states that the full array needs 3K - 2 cycles, 7 for K = 3.</desc>
<text class="vx-text" x="20" y="26">A enters from the left, staggered by row</text>
<text class="vx-text" x="480" y="26" text-anchor="end">B enters from the top, staggered by column</text>
<line class="vx-flow" x1="60" y1="155" x2="200" y2="155" marker-end="url(#g15-f1-head)"/>
<text class="vx-text-muted" x="65" y="145">row 0: cycle 0</text>
<line class="vx-flow" x1="60" y1="255" x2="200" y2="255" marker-end="url(#g15-f1-head)"/>
<text class="vx-text-muted" x="65" y="245">row 1: cycle 1</text>
<line class="vx-flow" x1="60" y1="355" x2="200" y2="355" marker-end="url(#g15-f1-head)"/>
<text class="vx-text-muted" x="65" y="345">row 2: cycle 2</text>
<line class="vx-flow" x1="260" y1="60" x2="260" y2="110" marker-end="url(#g15-f1-head)"/>
<text class="vx-text-muted" x="215" y="55">col 0: cycle 0</text>
<line class="vx-flow" x1="390" y1="60" x2="390" y2="110" marker-end="url(#g15-f1-head)"/>
<text class="vx-text-muted" x="345" y="55">col 1: cycle 1</text>
<line class="vx-flow" x1="520" y1="60" x2="520" y2="110" marker-end="url(#g15-f1-head)"/>
<text class="vx-text-muted" x="475" y="55">col 2: cycle 2</text>
<defs>
<marker id="g15-f1-head" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path class="vx-arrowhead" d="M0,0 L8,4 L0,8 z"/></marker>
</defs>
<rect class="vx-box-strong vx-pulse" x="200" y="110" width="120" height="90"/>
<text class="vx-mono" x="260" y="160" text-anchor="middle">(0,0) k=2</text>
<rect class="vx-box-strong vx-pulse" x="330" y="110" width="120" height="90"/>
<text class="vx-mono" x="390" y="160" text-anchor="middle">(0,1) k=1</text>
<rect class="vx-box-strong vx-pulse" x="460" y="110" width="120" height="90"/>
<text class="vx-mono" x="520" y="160" text-anchor="middle">(0,2) k=0</text>
<rect class="vx-box-strong vx-pulse" x="200" y="210" width="120" height="90"/>
<text class="vx-mono" x="260" y="260" text-anchor="middle">(1,0) k=1</text>
<rect class="vx-box-strong vx-pulse" x="330" y="210" width="120" height="90"/>
<text class="vx-mono" x="390" y="260" text-anchor="middle">(1,1) k=0</text>
<rect class="vx-box" x="460" y="210" width="120" height="90"/>
<text class="vx-text-muted" x="520" y="260" text-anchor="middle">(1,2) idle</text>
<rect class="vx-box-strong vx-pulse" x="200" y="310" width="120" height="90"/>
<text class="vx-mono" x="260" y="360" text-anchor="middle">(2,0) k=0</text>
<rect class="vx-box" x="330" y="310" width="120" height="90"/>
<text class="vx-text-muted" x="390" y="360" text-anchor="middle">(2,1) idle</text>
<rect class="vx-box" x="460" y="310" width="120" height="90"/>
<text class="vx-text-muted" x="520" y="360" text-anchor="middle">(2,2) idle</text>
<line class="vx-line" x1="460" y1="200" x2="200" y2="400" stroke-dasharray="6 5"/>
<text class="vx-text-muted" x="240" y="410">wavefront: cells with row + column &le; 2</text>
<text class="vx-mono" x="380" y="430" text-anchor="middle">full array: 3K - 2 cycles (7 for K = 3, matching systolic_sim.cpp)</text>
</svg>
<figcaption>Figure 1. An output-stationary array's active cells at one cycle form a diagonal wavefront that sweeps from the entry corner to the exit corner as A and B stream in. Cells ahead of the front are still waiting for their first pair; cells behind it have already finished and are holding a completed output.</figcaption>
</figure>

??? check "Cell (1, 1) does its first multiply-add at cycle 2, not cycle 0. Why does it have to wait?"

    `A[1][0]` enters row 1 at cycle 1 (row `i` starts at cycle `i`) and needs one more cycle to travel from column 0 to column 1, arriving at cycle 2. `B[0][1]` enters column 1 at cycle 1 in the same way and needs one cycle to travel down to row 1, also arriving at cycle 2. Cell (1, 1) cannot start until both of its operands have made the trip the staggered schedule assigns them, at cycle `i + j = 2`.

## What "systolic" bought Kung's original argument

Kung's 1982 paper does not start from a diagram; it starts from a bottleneck. A chip's arithmetic capacity grows roughly with the number of transistors it can fit, but the number of pins carrying data on and off the chip, and the bandwidth behind those pins, grows far more slowly. A design that reads every operand from off-chip memory for every arithmetic operation is I/O-bound long before it is compute-bound, exactly the left side of the roofline bound from [P3](../optimize/p3-roofline.md#the-roofline-two-ceilings-and-the-ridge-point): its operational intensity, flops per byte crossing the chip boundary, is stuck too low to reach the compute ceiling. Kung's answer is to make each operand that does cross the boundary do many multiply-adds before it leaves the chip, by pushing it from cell to cell instead of returning it to memory between uses.[^kung1982] The 3 by 3 array above realizes this literally: `A[0][0]` is fetched once and used by three cells, `B[0][0]` is fetched once and used by three cells, and the only traffic across the chip's edge is the nine input rows and columns going in and the nine finished elements of `C` coming out, not the 27 intermediate products a naive core would otherwise re-read operands for.

The price is regularity. A systolic array's cells all run the same tiny program, wired to fixed neighbours, on a schedule fixed before the array is built. There is no branch to predict, no instruction to fetch, no cache to miss: every cell's job each cycle is decided entirely by which cycle it is. That rigidity is exactly why an array like this cannot run an arbitrary program the way a CPU or a GPU's SIMT core can; it can only run the one computation, or family of computations, its wiring and timing were built for. A systolic array trades the generality of a programmable core for a pipeline that does one shape of arithmetic at a rate limited only by how fast operands can be pushed through it, and a cell never reads from or writes to a shared memory or cache the way a SIMT core's every instruction does: its operands are pushed to it by its neighbours, so it has no address to compute and no memory request to issue for them.

## The TPU: a systolic array as a product

Google's first Tensor Processing Unit, described by Jouppi et al. at ISCA 2017, puts a systolic array at the center of a chip built for one job: running trained neural networks in a datacenter at high volume and low latency, not training them and not running anything else.[^jouppi2017] Its matrix multiply unit is a 256 by 256 grid of 8-bit multiply-accumulate cells, 65,536 of them, wired exactly as the 3 by 3 example above is, just far larger, with a peak throughput the paper reports as 92 TOPS (tera-operations per second) and a 28 MiB on-chip buffer feeding it.[^jouppi2017] Three of that design's choices follow directly from the previous two sections. It uses 8-bit integers instead of 32-bit floats, because inference tolerates the lower precision and narrower operands mean more multiply-add cells fit in the same silicon and the same pin bandwidth carries more of them per cycle: the I/O bottleneck Kung's argument starts from, made concrete in a real budget. It is output-stationary, accumulating each output in place across the systolic pipeline exactly as the worked example did, so a value fetched from the on-chip buffer is reused across every cell in its row or column before the buffer is asked for it again. And it is controlled by a short, matrix-multiply-sized instruction set issued by a host CPU, not by independent per-thread instruction streams: the array is one large fused operation from the host's point of view, matching the earlier point that a systolic array does not run arbitrary programs, it runs one shape of computation, over and over, at scale.

That combination of a fixed pipeline and a fixed, quantized numeric format also buys something the roofline bound does not measure directly: a deterministic answer. There is exactly one order in which each output element's partial products are summed, fixed by the array's wiring, so the same inputs produce the same bits on every run. [P11](../optimize/p11-floating-point.md) makes the general point that floating-point addition is not associative, so two schedules that sum the same numbers in a different order can print different bits for the same mathematical answer; an accelerator whose schedule is fixed at design time sidesteps that question entirely, because there is only ever one schedule to ask about.

??? check "The TPU's designers chose 8-bit integer operands over 32-bit floats for its matrix unit. What does that choice cost, and what does it buy?"

    It costs precision and range: an 8-bit integer represents far fewer distinct values than a 32-bit float, and the workload (trained-network inference) has to tolerate that. It buys silicon and bandwidth: narrower operands mean more multiply-accumulate cells fit in the same area, and the same pin bandwidth moves more of them per cycle, which is exactly what a systolic design is trying to maximize.

## When the array is the wrong size

Every worked example so far picked a *K* by *K* array for a *K* by *K* product, so every cell had work to do on every cycle. Real matrices rarely arrive sized to match a real array, and the array is fixed in silicon: it cannot grow or shrink to fit. A *K* by *K* array computing an *N* by *N* product tiles the *N* by *N* matrix into `ceil(N / K)` tiles per side, runs one *K* by *K* multiply per tile pair, and pads any tile that overruns the matrix's edge with zeros. Those padded rows and columns still occupy cells and still cost cycles; they simply contribute nothing to the answer.

`utilization.cpp` computes the padded size and the fraction of cell-cycles that do useful work for a few (*N*, *K*) pairs, including Vortex's own matmul kernel size, 64 by 64.

--8<-- "includes/examples/gpu/g15-systolic-arrays/utilization.cpp.md"

When `K` divides `N` exactly, as 16 divides 64, every tile lands flush against the matrix's edge and utilization is 100%: no cell ever computes a product involving a padded zero. When it does not, as 24 does not divide 64, the last tile in each direction overruns by 8 rows or columns, and that waste is squared, because both dimensions of the array pad independently: a `72 x 72` padded footprint for a `64 x 64` matrix wastes `72*72 - 64*64` cell-cycles, bringing utilization to 79.0%. The smaller example, a 10 by 10 matrix on a 4 by 4 array, shows the same effect at a size worth tracing by hand: three tiles per side, a `12 x 12` footprint, and 69.4% utilization, the number `utilization.cpp` prints. Notice that the waste is an area, not a length: padding happens independently on both the row axis and the column axis, so a remainder that looks small in one dimension can still cost a much larger fraction of the padded footprint's total area. Work out one more case yourself before moving on: what utilization does a 64 by 64 matrix reach on a 48 by 48 array, and does making the array bigger than the matrix ever help?

## Paying for the pipeline once, not per tile

The `3K - 2` cycle count from the worked schedule counts a single tile's fill, the cycles before every cell has something to do, and drain, the cycles after the first cells finish but before the last ones do. A real workload rarely runs one tile and stops: a big matrix multiplication tiled onto a *K* by *K* array runs many *K* by *K* tile pairs back to back, and there is no reason to let the array sit empty between them. Once cell `(K-1, K-1)` of one tile finishes, the array can already be receiving the first operands of the next tile at cells `(0, 0)`, exactly the way one thread finishing does not have to wait for every other thread in [G1](g1-throughput-machines.md#littles-law-how-much-work-has-to-be-in-flight)'s account of keeping a pipelined resource busy. Fill and drain then happen once for the whole run, not once per tile, and the steady middle costs exactly `K` cycles per tile.

`pipeline_latency.cpp` computes total cycles under that model, fill once plus `K` cycles per tile plus drain once, for a fixed 8 by 8 array processing a growing number of tiles, and compares the result to the number of cycles an array with no fill or drain at all would need.

--8<-- "includes/examples/gpu/g15-systolic-arrays/pipeline_latency.cpp.md"

A single tile, `T = 1`, pays the full `3K - 2` overhead relative to its own 8 cycles of useful work and reaches only 36.4% utilization. By `T = 64` tiles the same fixed 14-cycle overhead is spread across `64 * 8 = 512` cycles of useful work, and utilization has climbed to 97.3%. Nothing about the array changed between those two rows: the only thing that changed is how much work was in flight behind the fill-and-drain cost before it had to pay for another one.

??? check "Why does utilization keep rising toward 100% as T grows in pipeline_latency.cpp's model, even though every run still pays 2(K - 1) cycles of fill and drain?"

    The fill-and-drain cost is paid once per run, not once per tile, so it is a fixed cost that a growing amount of useful work is divided among. As T grows, that fixed cost becomes a smaller and smaller fraction of the total cycles, the same amortization argument G1 makes about supplying enough independent work to keep a pipelined resource's throughput ceiling actually reached rather than merely available.

## What changes for a compiler

Every earlier chapter in this book assumed a program that a scheduler, a warp scheduler or an out-of-order core, gets to make some decisions about at run time: which warp issues next, which instruction from a ready queue goes first. A systolic array's compiler does not get that freedom, because the hardware has none to give it. Every decision that determines whether the array's cells are ever fed the right operand at the right cycle has to be made before the program runs, and baked into the operand schedule and the data's layout in memory. That makes targeting an accelerator like this a **scheduling-and-layout problem** in the strictest sense: the compiler's output is not instructions a flexible core will later reorder or predict around, it is a fixed timetable.

Three of those decisions map directly onto ideas this book has already built. First, which operand stays put: this chapter's examples were all output-stationary, but a **weight-stationary** design instead holds one matrix's values fixed in the cells and streams both the other operand and the output through, and a **row-stationary** design (used by some vision accelerators) picks yet another operand to fix; the choice changes which values need to be re-read from off-chip memory and how often, exactly the reuse-versus-locality tradeoff [P8](../optimize/p8-cache-blocking.md) already makes for cache tiling, now with the reuse pattern fixed in silicon instead of chosen by a loop order. Second, how a matrix's shape maps onto the array's fixed size: the padding and utilization arithmetic worked out above is not optional bookkeeping, it is the layout decision that determines how many cell-cycles a given matrix actually costs, and it has to be made statically, because the array cannot ask at run time how big its input turned out to be. Third, the fixed accumulation order this chapter noted as a side effect of a fixed schedule is also a layout decision with a floating-point consequence: a compiler targeting an array like this chooses, once, the order every output element's terms are summed in, and that choice is then permanent for every run, the same commitment [Decision 43](../decisions/arrays.md#d43) makes for Vortex's own arrays by fixing row-major order rather than leaving it open per platform.

Vortex's own design choices line up with all three. Its arrays are fixed-shape: `[f32; 64, 64]` carries its dimensions in the type, known at compile time, which is exactly the fact a systolic mapping needs and a general-purpose runtime array would have to discover or guess at. Its matmul kernel takes its output through a `&mut` parameter that no other reference can alias during the call, the same fact [G1](g1-throughput-machines.md#a-budget-spent-two-ways) uses to let a compiler skip a runtime aliasing check; that same fact is what makes an output-stationary mapping sound in the first place, because it guarantees the array's cells can each privately own one slice of `c` across many cycles without any other part of the program observing it mid-accumulation:

```vortex
// fragment
fn matmul(a: &[f32; 64, 64], b: &[f32; 64, 64], c: &mut [f32; 64, 64]) { /* ... */ }
```

And Vortex's floating-point rules are strict about evaluation order rather than leaving it open, which is the same commitment a systolic mapping's fixed accumulation schedule makes for a different reason: both choose one order and hold to it, instead of letting an optimizer or a scheduler pick a different one on a different run.

??? check "Vortex's [f32; 64, 64] matmul kernel's shape is fixed at compile time. Why does that matter more for a systolic mapping than it would for code running on a general-purpose core?"

    A general-purpose core discovers the loop bounds at run time and adapts, one instruction at a time, to whatever shape it is given. A systolic array's schedule and layout are fixed before the program runs and cannot adapt at run time at all, so the mapping has to know the exact shape in advance; a compile-time-known shape is exactly what makes that mapping something the compiler can compute once, rather than something the array itself would have to be told.

## For Vortex

!!! vortex "Exercise"

    **Build** a utilization-and-cycle report for Vortex's fixed-shape matmul kernel, mapped onto a hypothetical *K* by *K* output-stationary systolic array: given *K*, report the tile count, the padded footprint, the utilization percentage this chapter's `utilization.cpp` computes, and, for a chosen number of tiles processed back to back, the total cycle count and steady-state utilization `pipeline_latency.cpp`'s model computes.

    1. The report's input is a single number, *K* (the array's side), plus, for the pipelining part, a tile count *T*; Vortex's own matrix size, 64, is fixed by [Decision 43](../decisions/arrays.md#d43) and needs no input of its own.
    2. The utilization arithmetic itself: tile count `ceil(64 / K)`, padded footprint `tiles * K`, and utilization as this chapter defines it, the useful area over the padded area.
    3. The pipelining arithmetic: total cycles as fill once, `K` cycles per tile, drain once, compared against the cycle count an array with no fill or drain at all would need, exactly as `pipeline_latency.cpp` computes it for one fixed *K*.
    4. A short written judgment, for two or three values of *K* the reader picks, of whether that array size is a good fit for a 64 by 64 matrix, referring to the utilization and steady-state numbers the report itself produced, not a guess.

    **Not yet:** deciding whether Vortex would ever target a real systolic accelerator, or which one; mapping the kernel's actual loop nest onto an array's cells and wiring in a compiler pass ([M9](../mlir/m9-transform-dialect.md) and [M12](../mlir/m12-vortex-gpu-path.md) weigh how a schedule like this would even be represented as IR); weight-stationary or row-stationary alternatives, which change the reuse pattern and are worth a written comparison only after the output-stationary case is solid; any real accelerator's actual clock rate, cell count or throughput, which this chapter gives no verified number for beyond the one paper it cites.

    **Proof that it works:**

    - Golden tests reproducing this chapter's own tables: `utilization.cpp`'s four `(N, K)` rows, and `pipeline_latency.cpp`'s six `T` rows at `K = 8`.
    - A property test: for random positive `K` at `N = 64`, the reported utilization is `4096 / (tiles * K)^2` where `tiles = ceil(64 / K)`, matching a direct recomputation, and is always in `(0, 100]`.
    - A property test: for random positive `K` and `T`, the reported cycle count is `T * K + 2 * (K - 1)`, and steady-state utilization strictly increases as `T` increases with `K` fixed.
    - A boundary test: `K = 64` reports 100% utilization with no padding at all, and `K = 1` reports 100% utilization too, for a different reason worth stating in the report's own written judgment.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is a systolic array?** A grid of arithmetic cells wired to fixed neighbours that pumps operands through itself on a fixed schedule, so each value fetched from memory is reused by every cell it passes through instead of being re-fetched.
    - **What does "output-stationary" mean?** Each cell owns one element of the output and accumulates it in place across several cycles, never sending a partial sum anywhere until it is finished.
    - **Why does a systolic array's active region form a diagonal wavefront?** The staggered entry schedule makes cell `(i, j)` receive its first pair of operands at cycle `i + j`, so all cells with the same `i + j` become active on the same cycle, and that set sweeps from the entry corner to the exit corner as the cycle count rises.
    - **Why does utilization fall when K does not divide N?** The array pads any tile that overruns the matrix's edge, and because both dimensions pad independently, the wasted cell-cycles are an area, not a length, so a small remainder in one dimension can still waste a large fraction of the padded footprint.
    - **What does batching several tiles back to back buy?** The fixed fill-and-drain overhead of loading and draining the array is paid once per run instead of once per tile, so a growing amount of useful work divides a fixed cost among more cycles, and utilization climbs toward 100%.
    - **What did the TPU's designers trade for a fixed, deterministic pipeline?** The generality of a programmable core: 8-bit operands instead of floats, one instruction set issued by a host instead of independent per-thread streams, and one fixed accumulation order instead of one an optimizer could vary.
    - **Why is targeting a systolic array a scheduling-and-layout problem for a compiler?** Every decision that would otherwise be made at run time, by a scheduler reordering instructions or a cache deciding what to keep, has to be made once, statically, and baked into the operand schedule and the data's layout, because the hardware has no run-time flexibility to make those decisions itself.

## Where this comes back

!!! next "You will use this again in"

    - [M9. Schedules as IR: the transform dialect](../mlir/m9-transform-dialect.md): *static schedule*, *tiling*
    - [M11. End-to-end ML compilers](../mlir/m11-ml-compilers.md): *accelerator back end*, *deterministic pipeline*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *output-stationary*, *utilization*

## Sources and further reading

[^kung1982]: H. T. Kung, "Why Systolic Architectures?", *Computer*, 15(1):37-46, 1982, doi:10.1109/MC.1982.1653825. <http://www.eecs.harvard.edu/~htk/publication/1982-kung-why-systolic-architecture.pdf>
[^jouppi2017]: Norman P. Jouppi et al., "In-Datacenter Performance Analysis of a Tensor Processing Unit", ISCA 2017 (the matrix multiply unit's cell count, peak throughput and on-chip buffer size). <https://arxiv.org/abs/1704.04760>
