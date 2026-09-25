# G15. Beyond GPUs: systolic arrays and accelerators

<p class="page-intro">A systolic array is a grid of multiply-add cells that pass operands to their neighbours, so a value read from memory once is used by every cell it passes. This chapter builds one by hand, follows the idea into Google's first TPU, and shows why a compiler for such a machine must fix the schedule, the layout and the use of on-chip memory before the program runs, which is where Vortex's fixed shapes and strict arithmetic rules start to matter.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 40 minutes · Builds on: [G1. Throughput machines](g1-throughput-machines.md), [G11. Matrix units](g11-matrix-units.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is the difference between a latency-optimized and a throughput-optimized design?"

        A latency-optimized design spends its transistors on finishing one chain of dependent work as fast as possible. A throughput-optimized design spends them on running many independent pieces of work at once, so that the amount finished per second is high even though no single piece is fast.

        Introduced in [G1. Throughput machines](g1-throughput-machines.md#a-budget-spent-two-ways).

    ??? question "What turns reuse into locality?"

        Reuse belongs to the computation: the same data used by more than one step. Locality is reuse that is captured, because the second use arrives before the data has gone. A transformation cannot create or destroy reuse; it only decides how much of it is captured.

        Introduced in [P7. Loop transformations](../optimize/p7-loop-transformations.md#the-same-work-in-a-different-order).

    ??? question "What limits a kernel's attainable rate in the roofline model?"

        The smaller of the machine's peak compute rate and its memory bandwidth times the kernel's operational intensity (operations per byte moved from memory). Using each byte more times before it leaves the chip raises the intensity and can move a kernel from the bandwidth-bound side to the compute-bound side.

        Introduced in [P3. The roofline model](../optimize/p3-roofline.md#the-roofline-two-lines-and-a-ridge).

    ??? question "What does a GPU matrix unit compute in one instruction?"

        `D = A * B + C` over one small tile of fixed size, issued together by a whole warp or SIMD-group.

        Introduced in [G11. Matrix units](g11-matrix-units.md#one-instruction-one-tile).

    ??? question "May a Vortex compiler regroup the additions in `sum += a[row, k] * b[k, column]`?"

        No. Each floating-point operation is one IEEE 754 operation, rounded once, and must not be contracted, reassociated or reordered.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Trace an output-stationary systolic array cycle by cycle, and explain why it reads each operand from memory only once.
    - Explain Kung's argument for systolic designs in terms of operations per memory access, and connect it to the roofline.
    - Distinguish output-stationary, weight-stationary and row-stationary dataflows, and identify the one the first TPU uses.
    - Compute how much of a fixed *K* by *K* array does useful work, from padding and from pipeline fill and drain.
    - Explain why an accelerator's compiler fixes the schedule, the data layout and the on-chip memory before the program runs, and which of those choices Vortex's language rules constrain.

## A grid that never asks memory twice

Take the smallest matrix product worth doing by hand: two 3 by 3 matrices, `A` and `B`, giving `C = A * B`. One element is a dot product of three terms:

$$C[0][0] = A[0][0] \cdot B[0][0] + A[0][1] \cdot B[1][0] + A[0][2] \cdot B[2][0]$$

The nine elements need 27 multiplications, and each value of `A` and `B` takes part in three of them. `A[0][0]` is needed by `C[0][0]`, `C[0][1]` and `C[0][2]`; `B[0][0]` by `C[0][0]`, `C[1][0]` and `C[2][0]`. A single processor that loads both operands for every multiplication performs 54 loads for 18 distinct values. Registers or a cache can capture that reuse, but each use still costs an access to them.

Now place nine small multiply-add cells in a 3 by 3 grid, one cell per element of `C`, and wire each cell to its right-hand and lower neighbours. Feed row `i` of `A` in from the left edge, one value per cycle, and column `j` of `B` in from the top edge. Every cycle, each cell multiplies the `A` value and the `B` value it received this cycle, adds the product to its own running sum, and passes the `A` value right and the `B` value down. A value read from memory enters at an edge and is used by every cell along its row or column, handed from cell to cell, without being read again.

This is a **systolic array**: a grid of simple, identical cells that pass data to their neighbours in a fixed rhythm, with memory reached only through the cells on the edge.[^kung1982] Kung compares memory to the heart: it pulses data through the cells, as the heart pulses blood through the body. Because each cell here keeps its own element of `C` in place until the sum is complete, and only the operands move, this arrangement is called **output-stationary**: the output is the value that stays still.

## The schedule, cycle by cycle

If every row of `A` and every column of `B` started on the same cycle, cell `(0, 1)` would receive `A[0][0]` on cycle 1, one cycle after `B[0][1]` had passed through it, and would multiply `A[0][0]` by `B[1][1]`: the wrong pair. The fix is a **skew**: row `i` of `A` starts `i` cycles late, and column `j` of `B` starts `j` cycles late. Then `A[i][k]` enters row `i` at cycle `i + k` and needs `j` more cycles to reach column `j`, arriving at cycle `i + j + k`. `B[k][j]` enters column `j` at cycle `j + k` and needs `i` cycles to reach row `i`, arriving at the same cycle. Matching terms always meet.

So cell `(i, j)` forms term `k` of its dot product at cycle `i + j + k`. It starts at cycle `i + j`, forms one term per cycle, and finishes at cycle `i + j + K - 1` for a *K* by *K* product. The last cell, `(K-1, K-1)`, forms its last term at cycle `3K - 3`, so the whole product takes `3K - 2` cycles: 7 for `K = 3`, although each cell does only 3 multiply-adds.

`systolic_sim.cpp` models the array at the level of its registers. Each cell takes its `A` value from its left neighbour's register, or from memory if it sits on the left edge, and its `B` value from the register above, or from memory on the top edge. Each value carries the `k` it belongs to, so the program can check that the two operands a cell receives are always terms of the same `k`. It prints the `k` each cell works on, one group of three per grid row, counts memory reads and multiply-adds, and compares `C` with a triple loop.

--8<-- "includes/examples/gpu/g15-systolic-arrays/systolic_sim.cpp.md"

The last lines of the output confirm the claim from the first section: 18 reads from memory, one per element of `A` and `B`, feed 27 multiply-adds. Each value is read once and used three times. For a *K* by *K* product the ratio is `K` uses per read, so a bigger array gets more work from each value it reads.

Read the cycle lines against the rule. At cycle 2, the first group (row 0) prints `2 1 0`: cell `(0, 0)` forms its last term while cell `(0, 2)` forms its first. The busy cells are not a single anti-diagonal but a band of up to *K* neighbouring anti-diagonals: those with `i + j` between `t - K + 1` and `t`. The leading edge of the band, the cells forming their first term, is the **wavefront**; behind the band are finished cells and ahead of it are cells still waiting for their first pair. Figure 1 steps through the same run.

<figure class="vx-figure">
<div class="vx-stepper">
<div class="vx-step">
<p><strong>Cycle 0.</strong> Only cell (0, 0) has a pair: A[0][0] from the left edge and B[0][0] from the top edge meet there. Every other cell is still waiting.</p>
<svg viewBox="0 0 680 320" role="img" aria-label="Cycle 0: 1 of 9 cells busy: (0, 0) k=0.">
<text class="vx-text" x="20" y="24">cycle 0: 1 of 9 cells busy</text>
<text class="vx-text-accent" x="185" y="110" text-anchor="end">A[0][0] →</text>
<text class="vx-text-muted" x="185" y="190" text-anchor="end">(empty) →</text>
<text class="vx-text-muted" x="185" y="270" text-anchor="end">(empty) →</text>
<text class="vx-text-accent" x="270" y="55" text-anchor="middle">B[0][0] ↓</text>
<text class="vx-text-muted" x="420" y="55" text-anchor="middle">(empty) ↓</text>
<text class="vx-text-muted" x="570" y="55" text-anchor="middle">(empty) ↓</text>
<rect class="vx-cell-on" x="200" y="70" width="140" height="70"/>
<text class="vx-mono" x="270" y="102" text-anchor="middle">A[0][0]·B[0][0]</text>
<text class="vx-text-muted" x="270" y="124" text-anchor="middle">k = 0</text>
<rect class="vx-box" x="350" y="70" width="140" height="70"/>
<text class="vx-text-muted" x="420" y="110" text-anchor="middle">waiting</text>
<rect class="vx-box" x="500" y="70" width="140" height="70"/>
<text class="vx-text-muted" x="570" y="110" text-anchor="middle">waiting</text>
<rect class="vx-box" x="200" y="150" width="140" height="70"/>
<text class="vx-text-muted" x="270" y="190" text-anchor="middle">waiting</text>
<rect class="vx-box" x="350" y="150" width="140" height="70"/>
<text class="vx-text-muted" x="420" y="190" text-anchor="middle">waiting</text>
<rect class="vx-box" x="500" y="150" width="140" height="70"/>
<text class="vx-text-muted" x="570" y="190" text-anchor="middle">waiting</text>
<rect class="vx-box" x="200" y="230" width="140" height="70"/>
<text class="vx-text-muted" x="270" y="270" text-anchor="middle">waiting</text>
<rect class="vx-box" x="350" y="230" width="140" height="70"/>
<text class="vx-text-muted" x="420" y="270" text-anchor="middle">waiting</text>
<rect class="vx-box" x="500" y="230" width="140" height="70"/>
<text class="vx-text-muted" x="570" y="270" text-anchor="middle">waiting</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Cycle 1.</strong> The first values have moved one cell on. A[0][0] reached (0, 1) and B[0][0] reached (1, 0); new values entered at the edges. Three cells work, on the anti-diagonal i + j = 1 and on (0, 0).</p>
<svg viewBox="0 0 680 320" role="img" aria-label="Cycle 1: 3 of 9 cells busy: (0, 0) k=1, (0, 1) k=0, (1, 0) k=0.">
<text class="vx-text" x="20" y="24">cycle 1: 3 of 9 cells busy</text>
<text class="vx-text-accent" x="185" y="110" text-anchor="end">A[0][1] →</text>
<text class="vx-text-accent" x="185" y="190" text-anchor="end">A[1][0] →</text>
<text class="vx-text-muted" x="185" y="270" text-anchor="end">(empty) →</text>
<text class="vx-text-accent" x="270" y="55" text-anchor="middle">B[1][0] ↓</text>
<text class="vx-text-accent" x="420" y="55" text-anchor="middle">B[0][1] ↓</text>
<text class="vx-text-muted" x="570" y="55" text-anchor="middle">(empty) ↓</text>
<rect class="vx-cell-on" x="200" y="70" width="140" height="70"/>
<text class="vx-mono" x="270" y="102" text-anchor="middle">A[0][1]·B[1][0]</text>
<text class="vx-text-muted" x="270" y="124" text-anchor="middle">k = 1</text>
<rect class="vx-cell-on" x="350" y="70" width="140" height="70"/>
<text class="vx-mono" x="420" y="102" text-anchor="middle">A[0][0]·B[0][1]</text>
<text class="vx-text-muted" x="420" y="124" text-anchor="middle">k = 0</text>
<rect class="vx-box" x="500" y="70" width="140" height="70"/>
<text class="vx-text-muted" x="570" y="110" text-anchor="middle">waiting</text>
<rect class="vx-cell-on" x="200" y="150" width="140" height="70"/>
<text class="vx-mono" x="270" y="182" text-anchor="middle">A[1][0]·B[0][0]</text>
<text class="vx-text-muted" x="270" y="204" text-anchor="middle">k = 0</text>
<rect class="vx-box" x="350" y="150" width="140" height="70"/>
<text class="vx-text-muted" x="420" y="190" text-anchor="middle">waiting</text>
<rect class="vx-box" x="500" y="150" width="140" height="70"/>
<text class="vx-text-muted" x="570" y="190" text-anchor="middle">waiting</text>
<rect class="vx-box" x="200" y="230" width="140" height="70"/>
<text class="vx-text-muted" x="270" y="270" text-anchor="middle">waiting</text>
<rect class="vx-box" x="350" y="230" width="140" height="70"/>
<text class="vx-text-muted" x="420" y="270" text-anchor="middle">waiting</text>
<rect class="vx-box" x="500" y="230" width="140" height="70"/>
<text class="vx-text-muted" x="570" y="270" text-anchor="middle">waiting</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Cycle 2.</strong> The busiest start so far: six cells work. Cell (0, 0) forms its last term, k = 2. The leading anti-diagonal, i + j = 2, forms its first.</p>
<svg viewBox="0 0 680 320" role="img" aria-label="Cycle 2: 6 of 9 cells busy: (0, 0) k=2, (0, 1) k=1, (0, 2) k=0, (1, 0) k=1, (1, 1) k=0, (2, 0) k=0.">
<text class="vx-text" x="20" y="24">cycle 2: 6 of 9 cells busy</text>
<text class="vx-text-accent" x="185" y="110" text-anchor="end">A[0][2] →</text>
<text class="vx-text-accent" x="185" y="190" text-anchor="end">A[1][1] →</text>
<text class="vx-text-accent" x="185" y="270" text-anchor="end">A[2][0] →</text>
<text class="vx-text-accent" x="270" y="55" text-anchor="middle">B[2][0] ↓</text>
<text class="vx-text-accent" x="420" y="55" text-anchor="middle">B[1][1] ↓</text>
<text class="vx-text-accent" x="570" y="55" text-anchor="middle">B[0][2] ↓</text>
<rect class="vx-cell-on" x="200" y="70" width="140" height="70"/>
<text class="vx-mono" x="270" y="102" text-anchor="middle">A[0][2]·B[2][0]</text>
<text class="vx-text-muted" x="270" y="124" text-anchor="middle">k = 2</text>
<rect class="vx-cell-on" x="350" y="70" width="140" height="70"/>
<text class="vx-mono" x="420" y="102" text-anchor="middle">A[0][1]·B[1][1]</text>
<text class="vx-text-muted" x="420" y="124" text-anchor="middle">k = 1</text>
<rect class="vx-cell-on" x="500" y="70" width="140" height="70"/>
<text class="vx-mono" x="570" y="102" text-anchor="middle">A[0][0]·B[0][2]</text>
<text class="vx-text-muted" x="570" y="124" text-anchor="middle">k = 0</text>
<rect class="vx-cell-on" x="200" y="150" width="140" height="70"/>
<text class="vx-mono" x="270" y="182" text-anchor="middle">A[1][1]·B[1][0]</text>
<text class="vx-text-muted" x="270" y="204" text-anchor="middle">k = 1</text>
<rect class="vx-cell-on" x="350" y="150" width="140" height="70"/>
<text class="vx-mono" x="420" y="182" text-anchor="middle">A[1][0]·B[0][1]</text>
<text class="vx-text-muted" x="420" y="204" text-anchor="middle">k = 0</text>
<rect class="vx-box" x="500" y="150" width="140" height="70"/>
<text class="vx-text-muted" x="570" y="190" text-anchor="middle">waiting</text>
<rect class="vx-cell-on" x="200" y="230" width="140" height="70"/>
<text class="vx-mono" x="270" y="262" text-anchor="middle">A[2][0]·B[0][0]</text>
<text class="vx-text-muted" x="270" y="284" text-anchor="middle">k = 0</text>
<rect class="vx-box" x="350" y="230" width="140" height="70"/>
<text class="vx-text-muted" x="420" y="270" text-anchor="middle">waiting</text>
<rect class="vx-box" x="500" y="230" width="140" height="70"/>
<text class="vx-text-muted" x="570" y="270" text-anchor="middle">waiting</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Cycle 3.</strong> Cell (0, 0) is done and holds C[0][0]. Seven cells work, the most at any one cycle for a 3 by 3 array, and cell (2, 2) is the only one still waiting.</p>
<svg viewBox="0 0 680 320" role="img" aria-label="Cycle 3: 7 of 9 cells busy: (0, 1) k=2, (0, 2) k=1, (1, 0) k=2, (1, 1) k=1, (1, 2) k=0, (2, 0) k=1, (2, 1) k=0.">
<text class="vx-text" x="20" y="24">cycle 3: 7 of 9 cells busy</text>
<text class="vx-text-muted" x="185" y="110" text-anchor="end">(empty) →</text>
<text class="vx-text-accent" x="185" y="190" text-anchor="end">A[1][2] →</text>
<text class="vx-text-accent" x="185" y="270" text-anchor="end">A[2][1] →</text>
<text class="vx-text-muted" x="270" y="55" text-anchor="middle">(empty) ↓</text>
<text class="vx-text-accent" x="420" y="55" text-anchor="middle">B[2][1] ↓</text>
<text class="vx-text-accent" x="570" y="55" text-anchor="middle">B[1][2] ↓</text>
<rect class="vx-box-accent" x="200" y="70" width="140" height="70"/>
<text class="vx-text" x="270" y="110" text-anchor="middle">done: C[0][0]</text>
<rect class="vx-cell-on" x="350" y="70" width="140" height="70"/>
<text class="vx-mono" x="420" y="102" text-anchor="middle">A[0][2]·B[2][1]</text>
<text class="vx-text-muted" x="420" y="124" text-anchor="middle">k = 2</text>
<rect class="vx-cell-on" x="500" y="70" width="140" height="70"/>
<text class="vx-mono" x="570" y="102" text-anchor="middle">A[0][1]·B[1][2]</text>
<text class="vx-text-muted" x="570" y="124" text-anchor="middle">k = 1</text>
<rect class="vx-cell-on" x="200" y="150" width="140" height="70"/>
<text class="vx-mono" x="270" y="182" text-anchor="middle">A[1][2]·B[2][0]</text>
<text class="vx-text-muted" x="270" y="204" text-anchor="middle">k = 2</text>
<rect class="vx-cell-on" x="350" y="150" width="140" height="70"/>
<text class="vx-mono" x="420" y="182" text-anchor="middle">A[1][1]·B[1][1]</text>
<text class="vx-text-muted" x="420" y="204" text-anchor="middle">k = 1</text>
<rect class="vx-cell-on" x="500" y="150" width="140" height="70"/>
<text class="vx-mono" x="570" y="182" text-anchor="middle">A[1][0]·B[0][2]</text>
<text class="vx-text-muted" x="570" y="204" text-anchor="middle">k = 0</text>
<rect class="vx-cell-on" x="200" y="230" width="140" height="70"/>
<text class="vx-mono" x="270" y="262" text-anchor="middle">A[2][1]·B[1][0]</text>
<text class="vx-text-muted" x="270" y="284" text-anchor="middle">k = 1</text>
<rect class="vx-cell-on" x="350" y="230" width="140" height="70"/>
<text class="vx-mono" x="420" y="262" text-anchor="middle">A[2][0]·B[0][1]</text>
<text class="vx-text-muted" x="420" y="284" text-anchor="middle">k = 0</text>
<rect class="vx-box" x="500" y="230" width="140" height="70"/>
<text class="vx-text-muted" x="570" y="270" text-anchor="middle">waiting</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Cycle 4.</strong> The band of busy cells moves toward the bottom-right corner. Cells (0, 1) and (1, 0) are done; (2, 2) receives its first pair.</p>
<svg viewBox="0 0 680 320" role="img" aria-label="Cycle 4: 6 of 9 cells busy: (0, 2) k=2, (1, 1) k=2, (1, 2) k=1, (2, 0) k=2, (2, 1) k=1, (2, 2) k=0.">
<text class="vx-text" x="20" y="24">cycle 4: 6 of 9 cells busy</text>
<text class="vx-text-muted" x="185" y="110" text-anchor="end">(empty) →</text>
<text class="vx-text-muted" x="185" y="190" text-anchor="end">(empty) →</text>
<text class="vx-text-accent" x="185" y="270" text-anchor="end">A[2][2] →</text>
<text class="vx-text-muted" x="270" y="55" text-anchor="middle">(empty) ↓</text>
<text class="vx-text-muted" x="420" y="55" text-anchor="middle">(empty) ↓</text>
<text class="vx-text-accent" x="570" y="55" text-anchor="middle">B[2][2] ↓</text>
<rect class="vx-box-accent" x="200" y="70" width="140" height="70"/>
<text class="vx-text" x="270" y="110" text-anchor="middle">done: C[0][0]</text>
<rect class="vx-box-accent" x="350" y="70" width="140" height="70"/>
<text class="vx-text" x="420" y="110" text-anchor="middle">done: C[0][1]</text>
<rect class="vx-cell-on" x="500" y="70" width="140" height="70"/>
<text class="vx-mono" x="570" y="102" text-anchor="middle">A[0][2]·B[2][2]</text>
<text class="vx-text-muted" x="570" y="124" text-anchor="middle">k = 2</text>
<rect class="vx-box-accent" x="200" y="150" width="140" height="70"/>
<text class="vx-text" x="270" y="190" text-anchor="middle">done: C[1][0]</text>
<rect class="vx-cell-on" x="350" y="150" width="140" height="70"/>
<text class="vx-mono" x="420" y="182" text-anchor="middle">A[1][2]·B[2][1]</text>
<text class="vx-text-muted" x="420" y="204" text-anchor="middle">k = 2</text>
<rect class="vx-cell-on" x="500" y="150" width="140" height="70"/>
<text class="vx-mono" x="570" y="182" text-anchor="middle">A[1][1]·B[1][2]</text>
<text class="vx-text-muted" x="570" y="204" text-anchor="middle">k = 1</text>
<rect class="vx-cell-on" x="200" y="230" width="140" height="70"/>
<text class="vx-mono" x="270" y="262" text-anchor="middle">A[2][2]·B[2][0]</text>
<text class="vx-text-muted" x="270" y="284" text-anchor="middle">k = 2</text>
<rect class="vx-cell-on" x="350" y="230" width="140" height="70"/>
<text class="vx-mono" x="420" y="262" text-anchor="middle">A[2][1]·B[1][1]</text>
<text class="vx-text-muted" x="420" y="284" text-anchor="middle">k = 1</text>
<rect class="vx-cell-on" x="500" y="230" width="140" height="70"/>
<text class="vx-mono" x="570" y="262" text-anchor="middle">A[2][0]·B[0][2]</text>
<text class="vx-text-muted" x="570" y="284" text-anchor="middle">k = 0</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Cycle 5.</strong> Three cells still work. No value enters from the edges any more: row 2's last A value entered at cycle 4.</p>
<svg viewBox="0 0 680 320" role="img" aria-label="Cycle 5: 3 of 9 cells busy: (1, 2) k=2, (2, 1) k=2, (2, 2) k=1.">
<text class="vx-text" x="20" y="24">cycle 5: 3 of 9 cells busy</text>
<text class="vx-text-muted" x="185" y="110" text-anchor="end">(empty) →</text>
<text class="vx-text-muted" x="185" y="190" text-anchor="end">(empty) →</text>
<text class="vx-text-muted" x="185" y="270" text-anchor="end">(empty) →</text>
<text class="vx-text-muted" x="270" y="55" text-anchor="middle">(empty) ↓</text>
<text class="vx-text-muted" x="420" y="55" text-anchor="middle">(empty) ↓</text>
<text class="vx-text-muted" x="570" y="55" text-anchor="middle">(empty) ↓</text>
<rect class="vx-box-accent" x="200" y="70" width="140" height="70"/>
<text class="vx-text" x="270" y="110" text-anchor="middle">done: C[0][0]</text>
<rect class="vx-box-accent" x="350" y="70" width="140" height="70"/>
<text class="vx-text" x="420" y="110" text-anchor="middle">done: C[0][1]</text>
<rect class="vx-box-accent" x="500" y="70" width="140" height="70"/>
<text class="vx-text" x="570" y="110" text-anchor="middle">done: C[0][2]</text>
<rect class="vx-box-accent" x="200" y="150" width="140" height="70"/>
<text class="vx-text" x="270" y="190" text-anchor="middle">done: C[1][0]</text>
<rect class="vx-box-accent" x="350" y="150" width="140" height="70"/>
<text class="vx-text" x="420" y="190" text-anchor="middle">done: C[1][1]</text>
<rect class="vx-cell-on" x="500" y="150" width="140" height="70"/>
<text class="vx-mono" x="570" y="182" text-anchor="middle">A[1][2]·B[2][2]</text>
<text class="vx-text-muted" x="570" y="204" text-anchor="middle">k = 2</text>
<rect class="vx-box-accent" x="200" y="230" width="140" height="70"/>
<text class="vx-text" x="270" y="270" text-anchor="middle">done: C[2][0]</text>
<rect class="vx-cell-on" x="350" y="230" width="140" height="70"/>
<text class="vx-mono" x="420" y="262" text-anchor="middle">A[2][2]·B[2][1]</text>
<text class="vx-text-muted" x="420" y="284" text-anchor="middle">k = 2</text>
<rect class="vx-cell-on" x="500" y="230" width="140" height="70"/>
<text class="vx-mono" x="570" y="262" text-anchor="middle">A[2][1]·B[1][2]</text>
<text class="vx-text-muted" x="570" y="284" text-anchor="middle">k = 1</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Cycle 6.</strong> Only cell (2, 2) works, on its last term. After this cycle every cell holds its finished element of C.</p>
<svg viewBox="0 0 680 320" role="img" aria-label="Cycle 6: 1 of 9 cells busy: (2, 2) k=2.">
<text class="vx-text" x="20" y="24">cycle 6: 1 of 9 cells busy</text>
<text class="vx-text-muted" x="185" y="110" text-anchor="end">(empty) →</text>
<text class="vx-text-muted" x="185" y="190" text-anchor="end">(empty) →</text>
<text class="vx-text-muted" x="185" y="270" text-anchor="end">(empty) →</text>
<text class="vx-text-muted" x="270" y="55" text-anchor="middle">(empty) ↓</text>
<text class="vx-text-muted" x="420" y="55" text-anchor="middle">(empty) ↓</text>
<text class="vx-text-muted" x="570" y="55" text-anchor="middle">(empty) ↓</text>
<rect class="vx-box-accent" x="200" y="70" width="140" height="70"/>
<text class="vx-text" x="270" y="110" text-anchor="middle">done: C[0][0]</text>
<rect class="vx-box-accent" x="350" y="70" width="140" height="70"/>
<text class="vx-text" x="420" y="110" text-anchor="middle">done: C[0][1]</text>
<rect class="vx-box-accent" x="500" y="70" width="140" height="70"/>
<text class="vx-text" x="570" y="110" text-anchor="middle">done: C[0][2]</text>
<rect class="vx-box-accent" x="200" y="150" width="140" height="70"/>
<text class="vx-text" x="270" y="190" text-anchor="middle">done: C[1][0]</text>
<rect class="vx-box-accent" x="350" y="150" width="140" height="70"/>
<text class="vx-text" x="420" y="190" text-anchor="middle">done: C[1][1]</text>
<rect class="vx-box-accent" x="500" y="150" width="140" height="70"/>
<text class="vx-text" x="570" y="190" text-anchor="middle">done: C[1][2]</text>
<rect class="vx-box-accent" x="200" y="230" width="140" height="70"/>
<text class="vx-text" x="270" y="270" text-anchor="middle">done: C[2][0]</text>
<rect class="vx-box-accent" x="350" y="230" width="140" height="70"/>
<text class="vx-text" x="420" y="270" text-anchor="middle">done: C[2][1]</text>
<rect class="vx-cell-on" x="500" y="230" width="140" height="70"/>
<text class="vx-mono" x="570" y="262" text-anchor="middle">A[2][2]·B[2][2]</text>
<text class="vx-text-muted" x="570" y="284" text-anchor="middle">k = 2</text>
</svg>
</div>
</div>
<figcaption>Figure 1. The 3 by 3 output-stationary array of <code>systolic_sim.cpp</code>, one cycle per step. Labels outside the grid name the value entering each row from the left and each column from the top on that cycle. A busy cell shows the product it forms; <code>k</code> is the term of its dot product. The busy cells form a band of anti-diagonals that enters at the top-left corner and leaves at the bottom-right.</figcaption>
</figure>

At no cycle are all nine cells busy. The busiest cycle, cycle 3, has seven; over the whole run, 27 of the 63 cell-cycles (9 cells times 7 cycles) do useful work. That idle fraction is the cost of starting and stopping the pipeline, and a later section shows how to pay it once rather than for every product.

??? check "Cell (1, 2) of the 3 by 3 array: on which cycles is it busy, and which values of `A` and `B` does it multiply on its first busy cycle?"

    It starts at cycle `i + j = 3` and is busy on cycles 3, 4 and 5. On cycle 3 it forms term `k = 0`, the product `A[1][0] * B[0][2]`. `A[1][0]` entered row 1 at cycle 1 and moved two cells right; `B[0][2]` entered column 2 at cycle 2 and moved one cell down.

## Kung's argument: more operations per memory access

Kung's 1982 paper starts from a failure it saw repeated in special-purpose hardware: a fast device is built, and only afterwards does it turn out that its input and output cannot keep up with it.[^kung1982] His example is a host connection of 10 million bytes per second. If every operation reads or writes at least two bytes through it, the device performs at most 5 million operations per second, however fast its arithmetic is. The only way past that limit is to perform several operations for every item that crosses the connection.

Kung then sorts computations into two families. A computation is **compute-bound**, in his terms, when it performs more operations than it has input and output elements; otherwise it is **I/O-bound**.[^kung1982] Matrix multiplication is compute-bound, because every element of one matrix meets every element of a row or column of the other. Matrix addition is I/O-bound: one addition per pair of inputs. An I/O-bound computation can only go faster with more memory bandwidth. A compute-bound one can go faster with more cells, if the design reuses each item it reads.

His Figure 1 makes the point with one memory whose access takes 100 ns: a single processing element attached to it reaches at most 5 million operations per second, while an array of cells attached to the same memory can reach 30 million, because each item read is used at several cells.[^kung1982] In the language of [P3](../optimize/p3-roofline.md#operational-intensity-flops-per-byte-of-dram-traffic), the array does not raise the bandwidth roof; it raises the operational intensity, operations per byte fetched, until the kernel moves right, past the ridge, and the compute roof becomes the limit. The 3 by 3 array above raised it from one use per read to three.

Kung lists the other properties that make the design attractive for hardware: cells that are simple and identical, data and control that flow in regular patterns, and communication with the outside world only at the **boundary cells**, the ones on the array's edge.[^kung1982] The same regularity limits the array to computations whose data flow can be wired in, such as matrix multiplication and convolution.

## Which value stays put

A multiply-add involves three values: two operands and a running sum. The output-stationary array keeps the sum in place and moves the operands. Kung's paper already walks through a family of designs for convolution that differ in exactly this choice, naming each by which of the weights, the inputs and the results stay in a cell and which move: in design B1 the weights stay and the results move, in design R1 the results stay while inputs and weights flow in opposite directions, in design W1 the weights stay while inputs and results move.[^kung1982]

Sze, Chen, Yang and Emer's survey of neural-network hardware gives the modern names.[^sze2017] A **weight-stationary** dataflow reads each weight into a cell once and keeps it there, using it for as many multiply-adds as possible; inputs stream past and partial sums move on to be accumulated. An **output-stationary** dataflow keeps each partial sum in the cell that produces it, which saves reading and writing partial sums. A **row-stationary** dataflow, proposed for the Eyeriss accelerator, assigns a whole one-dimensional row convolution to each cell and aims to reuse weights, inputs and partial sums together, instead of favouring one of them.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="Two 3 by 3 arrays side by side. Left, output-stationary: A values enter from the left, B values from the top, and each cell holds its own element of C until the end. Right, weight-stationary: weights are loaded into the cells from the top before the run and stay there, activations enter from the left, and partial sums flow down and leave at the bottom into accumulators.">
<defs>
<marker id="g15-f2-head" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path class="vx-arrowhead" d="M0,0 L8,4 L0,8 z"/></marker>
</defs>
<text class="vx-text" x="190" y="24" text-anchor="middle">Output-stationary (systolic_sim.cpp)</text>
<text class="vx-text" x="570" y="24" text-anchor="middle">Weight-stationary (first TPU)</text>
<line class="vx-line" x1="30" y1="95" x2="95" y2="95" marker-end="url(#g15-f2-head)"/>
<line class="vx-line" x1="30" y1="155" x2="95" y2="155" marker-end="url(#g15-f2-head)"/>
<line class="vx-line" x1="30" y1="215" x2="95" y2="215" marker-end="url(#g15-f2-head)"/>
<text class="vx-text-accent" x="30" y="85">A in</text>
<line class="vx-line" x1="130" y1="40" x2="130" y2="65" marker-end="url(#g15-f2-head)"/>
<line class="vx-line" x1="190" y1="40" x2="190" y2="65" marker-end="url(#g15-f2-head)"/>
<line class="vx-line" x1="250" y1="40" x2="250" y2="65" marker-end="url(#g15-f2-head)"/>
<text class="vx-text-accent" x="290" y="55">B in</text>
<rect class="vx-box-strong" x="100" y="68" width="55" height="55"/><text class="vx-mono" x="127" y="100" text-anchor="middle">C</text>
<rect class="vx-box-strong" x="160" y="68" width="55" height="55"/><text class="vx-mono" x="187" y="100" text-anchor="middle">C</text>
<rect class="vx-box-strong" x="220" y="68" width="55" height="55"/><text class="vx-mono" x="247" y="100" text-anchor="middle">C</text>
<rect class="vx-box-strong" x="100" y="128" width="55" height="55"/><text class="vx-mono" x="127" y="160" text-anchor="middle">C</text>
<rect class="vx-box-strong" x="160" y="128" width="55" height="55"/><text class="vx-mono" x="187" y="160" text-anchor="middle">C</text>
<rect class="vx-box-strong" x="220" y="128" width="55" height="55"/><text class="vx-mono" x="247" y="160" text-anchor="middle">C</text>
<rect class="vx-box-strong" x="100" y="188" width="55" height="55"/><text class="vx-mono" x="127" y="220" text-anchor="middle">C</text>
<rect class="vx-box-strong" x="160" y="188" width="55" height="55"/><text class="vx-mono" x="187" y="220" text-anchor="middle">C</text>
<rect class="vx-box-strong" x="220" y="188" width="55" height="55"/><text class="vx-mono" x="247" y="220" text-anchor="middle">C</text>
<text class="vx-text-muted" x="190" y="270" text-anchor="middle">stays in a cell: the running sum of C</text>
<text class="vx-text-muted" x="190" y="292" text-anchor="middle">moves: A right, B down</text>
<text class="vx-text-muted" x="190" y="314" text-anchor="middle">leaves the array: finished C, once per tile</text>
<line class="vx-line" x1="410" y1="95" x2="475" y2="95" marker-end="url(#g15-f2-head)"/>
<line class="vx-line" x1="410" y1="155" x2="475" y2="155" marker-end="url(#g15-f2-head)"/>
<line class="vx-line" x1="410" y1="215" x2="475" y2="215" marker-end="url(#g15-f2-head)"/>
<text class="vx-text-accent" x="400" y="85">inputs in</text>
<text class="vx-text-muted" x="570" y="55" text-anchor="middle">weights preloaded from the top</text>
<rect class="vx-box-accent" x="480" y="68" width="55" height="55"/><text class="vx-mono" x="507" y="100" text-anchor="middle">W</text>
<rect class="vx-box-accent" x="540" y="68" width="55" height="55"/><text class="vx-mono" x="567" y="100" text-anchor="middle">W</text>
<rect class="vx-box-accent" x="600" y="68" width="55" height="55"/><text class="vx-mono" x="627" y="100" text-anchor="middle">W</text>
<rect class="vx-box-accent" x="480" y="128" width="55" height="55"/><text class="vx-mono" x="507" y="160" text-anchor="middle">W</text>
<rect class="vx-box-accent" x="540" y="128" width="55" height="55"/><text class="vx-mono" x="567" y="160" text-anchor="middle">W</text>
<rect class="vx-box-accent" x="600" y="128" width="55" height="55"/><text class="vx-mono" x="627" y="160" text-anchor="middle">W</text>
<rect class="vx-box-accent" x="480" y="188" width="55" height="55"/><text class="vx-mono" x="507" y="220" text-anchor="middle">W</text>
<rect class="vx-box-accent" x="540" y="188" width="55" height="55"/><text class="vx-mono" x="567" y="220" text-anchor="middle">W</text>
<rect class="vx-box-accent" x="600" y="188" width="55" height="55"/><text class="vx-mono" x="627" y="220" text-anchor="middle">W</text>
<line class="vx-flow" x1="507" y1="243" x2="507" y2="262" marker-end="url(#g15-f2-head)"/>
<line class="vx-flow" x1="567" y1="243" x2="567" y2="262" marker-end="url(#g15-f2-head)"/>
<line class="vx-flow" x1="627" y1="243" x2="627" y2="262" marker-end="url(#g15-f2-head)"/>
<text class="vx-text-muted" x="690" y="258">sums to</text>
<text class="vx-text-muted" x="690" y="274">accumulators</text>
<text class="vx-text-muted" x="570" y="292" text-anchor="middle">stays in a cell: one weight; moves: inputs right, sums down</text>
<text class="vx-text-muted" x="570" y="314" text-anchor="middle">leaves the array: a row of partial sums every cycle</text>
</svg>
<figcaption>Figure 2. Two ways to fill the same grid. Output-stationary keeps each output in its cell and moves both operands; weight-stationary keeps one operand, the weights, in the cells and moves the inputs and the partial sums. What stays decides what must travel between cells and what must be read from or written to the memories at the edge.</figcaption>
</figure>

The choice matters to the hardware because the values are not the same width. Kung observes that in design B1 the path that carries results may need to be much wider than the path that carries weights in B2, because results usually carry more bits for accuracy, and that a design whose results stay can keep extra bits in its accumulators at modest cost.[^kung1982] A weight-stationary array moves partial sums between cells every cycle, so its downward paths carry the wide values; an output-stationary array moves only the narrow operands and keeps the wide sums in place.

The choice also matters to the compiler, because it decides which dimension of the problem streams through the array and which dimensions are pinned to the grid. In the output-stationary array, the two output dimensions `i` and `j` are pinned to the grid and the reduction dimension `k` streams through time. In a weight-stationary array computing `inputs × weights`, the reduction dimension and one output dimension are pinned (a *K* by *K* tile of weights), and the other output dimension, often the batch of inputs, streams. Each dataflow maps the loop nest of [P7](../optimize/p7-loop-transformations.md) onto space and time differently.

??? check "A weight-stationary *K* by *K* array multiplies a batch of inputs by a weight matrix much larger than *K* by *K*. Which values have to be reloaded when the array moves to the next tile of weights, and which dimension of the problem can grow without any reloading?"

    The weights: the cells hold one *K* by *K* tile at a time, so each new tile of the weight matrix has to be shifted in. The streamed dimension, the number of input rows passed through the array while one weight tile is loaded, can grow freely: each additional input row reuses the tile that is already in place.

## The TPU: a systolic array as a product

Google's first Tensor Processing Unit, described by Jouppi and colleagues at ISCA 2017, has been in its datacenters since 2015 and runs one job: inference, the use of a trained neural network, not its training.[^jouppi2017] Its **matrix multiply unit** is a 256 by 256 grid of 8-bit multiply-accumulate cells, 65,536 in all, with a peak of 92 tera-operations per second at a 700 MHz clock. It uses the dataflow of the right-hand panel of Figure 2: the paper describes data flowing in from the left, weights loaded from the top and preloaded before use, and each multiply-accumulate moving through the array as a diagonal wavefront, the same wavefront as in Figure 1.

The rest of the chip exists to keep that grid fed. Weights come from an 8 GiB off-chip memory through a **Weight FIFO**, a queue four tiles deep; the matrix unit holds one 64 KiB tile of weights plus a second one, so the next tile can shift in during the 256 cycles that takes, while the current one is in use.[^jouppi2017] Inputs and intermediate results live in a 24 MiB **Unified Buffer**, and the 16-bit products are summed in 4 MiB of 32-bit accumulators below the array: together the 28 MiB of software-managed on-chip memory the abstract counts. The Unified Buffer takes almost a third of the die and the matrix unit a quarter; control takes 2%.

The TPU is not a processor in the CPU sense. It sits on the PCIe bus, and the host sends it instructions rather than letting it fetch its own, which the authors say makes it closer in spirit to a floating-point coprocessor than to a GPU.[^jouppi2017] There are about a dozen instructions, in the CISC tradition, of which five do most of the work: read host memory into the Unified Buffer, read weights into the Weight FIFO, multiply or convolve, apply the nonlinear activation function, and write results back to the host. One `MatrixMultiply` takes a variable number `B` of 256-element input rows, multiplies each by the 256 by 256 weight tile, and completes in `B` pipelined cycles.

Two choices in the design reflect the systolic trade directly. The first is the numeric format: the TPU runs networks after **quantization**, the conversion of trained floating-point weights and activations to narrow integers. The paper cites an estimate that 8-bit integer multiplies take about 6 times less energy and 6 times less area than IEEE 754 16-bit floating-point multiplies.[^jouppi2017] Narrower operands mean more cells in the same silicon and more values per cycle through the same paths. The price is precision, which inference tolerates and which [G11](g11-matrix-units.md#precision-what-the-unit-computes-is-not-what-f32-code-says) treats as a decision the programmer has to make.

The second is predictability. The authors argue that inference services are judged by their 99th-percentile response time, not their average, and that the TPU's deterministic execution model fits that requirement better than the time-varying optimizations of CPUs and GPUs.[^jouppi2017] The chip has no caches, no branch prediction, no out-of-order execution, no multithreading and no speculative prefetching. Each of those features adapts to the program while it runs. A systolic array has nothing to adapt: its schedule is fixed by its wiring and by the instructions it is sent, so the time a matrix multiply takes is known in advance.

## Tensor cores and matrix units, on the same scale

The tensor cores of [G11](g11-matrix-units.md) and the TPU's matrix unit compute the same thing, a multiply-accumulate over a fixed tile, but at different sizes, and the size decides the reuse. The TPU v4 paper compares its own design, whose TensorCores each contain four 128 by 128 matrix units, with NVIDIA's A100: each 128-element input to a TPU v4 matrix unit is reused 128 times, while the A100's 4 by 4 FP16 array multipliers reuse each input only 4 times. The authors offer this, tentatively, as one of three factors behind the A100's higher power: less reuse means more on-chip SRAM accesses.[^jouppi2023] That is the `K` uses per read of the first section, at two different values of `K`. A larger array is not free, though: the next two sections show the cost, in cells a matrix leaves empty and in cycles spent filling and draining.

## When the array is the wrong size

Every example so far used a *K* by *K* array for a *K* by *K* product. Real matrices rarely match the array, and the array cannot change size. A *K* by *K* output-stationary array computes an *N* by *N* result in *K* by *K* tiles, `ceil(N / K)` of them per side. Tiles on the far edges overrun the matrix and are **padded** with zeros: the padded cells take part in every cycle but contribute nothing. Because both sides of the tile pad, the useful fraction is a ratio of areas: `N² / (ceil(N / K) · K)²`.

Work one case by hand first: a 10 by 10 result on a 4 by 4 array. Three tiles per side cover 12 rows and 12 columns. The useful area is 100 cells of the 144 the tiles occupy, so the array does useful work on 69.4% of its cell-cycles. `utilization.cpp` computes the same fraction for a few more cases, including the 64 by 64 kernel of [G10](g10-matmul-ladder.md) and a case from the TPU paper.

--8<-- "includes/examples/gpu/g15-systolic-arrays/utilization.cpp.md"

When `K` divides `N`, as 16 divides 64, the tiles meet the edge exactly and nothing is wasted. When it does not, as with 24, the last tile in each direction overruns by 8, the padded square is 72 by 72, and 1,088 of its 5,184 cells are padding: 79.0% utilization. A remainder that looks small on one axis costs more once it is squared.

<figure class="vx-figure">
<svg viewBox="0 0 760 360" role="img" aria-label="Left: a 72 by 72 square split into a 3 by 3 grid of 24 by 24 tiles. The top-left 64 by 64 region is the real matrix; an L-shaped strip 8 wide along the right and bottom edges is padding, 1,088 of 5,184 cells, leaving 79.0% utilization. Right: a 600 by 600 matrix on a 256 by 256 unit takes a 3 by 3 grid of tiles, 9 steps, 61.0% utilization; on a 512 by 512 unit it takes a 2 by 2 grid, 4 steps, each four times longer, 34.3% utilization.">
<text class="vx-text" x="20" y="24">64 × 64 on a 24 × 24 array</text>
<rect class="vx-box-bad" x="20" y="40" width="288" height="288"/>
<rect class="vx-box-strong" x="20" y="40" width="256" height="256"/>
<line class="vx-line" x1="116" y1="40" x2="116" y2="328" stroke-dasharray="5 4"/>
<line class="vx-line" x1="212" y1="40" x2="212" y2="328" stroke-dasharray="5 4"/>
<line class="vx-line" x1="20" y1="136" x2="308" y2="136" stroke-dasharray="5 4"/>
<line class="vx-line" x1="20" y1="232" x2="308" y2="232" stroke-dasharray="5 4"/>
<text class="vx-mono" x="148" y="175" text-anchor="middle">real: 64 × 64</text>
<text class="vx-mono" x="148" y="195" text-anchor="middle">4,096 cells</text>
<text class="vx-text-muted" x="320" y="300">padding strip, 8 wide:</text>
<text class="vx-text-muted" x="320" y="318">1,088 of 5,184 cells</text>
<text class="vx-text-accent" x="320" y="340">utilization 79.0%</text>
<text class="vx-text" x="470" y="24">600 × 600 on the TPU (paper, section 7)</text>
<rect class="vx-box-bad" x="480" y="40" width="115" height="115"/>
<rect class="vx-box-strong" x="480" y="40" width="90" height="90"/>
<line class="vx-line" x1="518" y1="40" x2="518" y2="155" stroke-dasharray="4 3"/>
<line class="vx-line" x1="557" y1="40" x2="557" y2="155" stroke-dasharray="4 3"/>
<line class="vx-line" x1="480" y1="78" x2="595" y2="78" stroke-dasharray="4 3"/>
<line class="vx-line" x1="480" y1="117" x2="595" y2="117" stroke-dasharray="4 3"/>
<text class="vx-text-muted" x="610" y="70">256 × 256 unit</text>
<text class="vx-text-muted" x="610" y="90">3 × 3 = 9 steps</text>
<text class="vx-text-accent" x="610" y="110">61.0% useful</text>
<rect class="vx-box-bad" x="480" y="180" width="154" height="154"/>
<rect class="vx-box-strong" x="480" y="180" width="90" height="90"/>
<line class="vx-line" x1="557" y1="180" x2="557" y2="334" stroke-dasharray="4 3"/>
<line class="vx-line" x1="480" y1="257" x2="634" y2="257" stroke-dasharray="4 3"/>
<text class="vx-text-muted" x="645" y="210">512 × 512 unit</text>
<text class="vx-text-muted" x="645" y="230">2 × 2 = 4 steps,</text>
<text class="vx-text-muted" x="645" y="250">each 4 × longer</text>
<text class="vx-text-accent" x="645" y="270">34.3% useful</text>
</svg>
<figcaption>Figure 3. Padding wastes an area, not a length. Left: a 64 by 64 result on a 24 by 24 array needs a 3 by 3 grid of tiles; the shaded strip is padding. Right: the TPU paper's 600 by 600 example, drawn to scale; the larger unit needs fewer steps but wastes more of each one.</figcaption>
</figure>

The TPU paper met this effect when it modelled a larger matrix unit. Growing the unit from 256 by 256 to 512 by 512 made average performance slightly worse, which the authors compare to internal fragmentation with large memory pages, worse because it happens in two dimensions.[^jouppi2017-s7] Their example is a 600 by 600 matrix: 9 steps of 18 µs in total on the 256 by 256 unit, but 4 steps each four times longer, 32 µs, on the 512 by 512 one. The last two rows of the output give the same story as fractions: 61.0% of the smaller unit's work is useful, 34.3% of the larger one's.

Padding is not the only way cells sit empty. For one of the paper's six production applications, a convolutional network, the counters showed that on the cycles the matrix unit was active, only about half of the 65,536 cells held useful weights, because some layers had shallow feature depths: their weight matrices did not fill the 256 rows.[^jouppi2017] The layer, not the array, sets the shape.

Complete one case yourself before reading on: a 64 by 64 result on a 48 by 48 array. How many tiles per side, what padded size, what utilization? Then compare it with a 128 by 128 array, which covers the whole matrix in one tile. Is a bigger array ever better for this matrix?

??? check "Check your answer: 64 by 64 on a 48 by 48 array, and on a 128 by 128 array."

    On 48 by 48: `ceil(64 / 48) = 2` tiles per side, a 96 by 96 padded square, and 4,096 / 9,216 = 44.4% utilization. On 128 by 128: one tile, a 128 by 128 square, and 4,096 / 16,384 = 25.0%. For a single 64 by 64 product, no array larger than 64 by 64 helps: the extra cells can only hold padding. The 16 by 16 and 64 by 64 arrays reach 100%.

## Paying for fill and drain once

The `3K - 2` cycles of the worked schedule include the cycles before every cell has work, the **fill**, and the cycles after the first cells have finished, the **drain**. Together they cost `2(K - 1)` cycles, on top of the `K` cycles of work each cell does. A real workload runs many tiles, and nothing forces the array to empty between them.

Cell `(0, 0)` finishes its last term of one tile at cycle `K - 1`. If the operands of the next tile follow directly behind, it can start the next tile at cycle `K`, while cells further along are still working on the first. Each cell must then hand its finished sum out and restart from zero between tiles. Kung's results-stay designs do this with a tag bit on the first weight of a new result, which tells a cell to output its accumulator and reset it.[^kung1982] Under this model, fill and drain are paid once per run, and `T` tiles take `T · K + 2(K - 1)` cycles.

`pipeline_latency.cpp` counts busy cells cycle by cycle for an 8 by 8 array and a growing number of tiles, checks the total against that formula, and reports the fraction of cell-cycles that did work.

--8<-- "includes/examples/gpu/g15-systolic-arrays/pipeline_latency.cpp.md"

A single tile keeps the array busy for 36.4% of its cell-cycles. Sixty-four tiles back to back spread the same 14 cycles of fill and drain over 512 cycles of work and reach 97.3%. The array did not change between those rows; only the amount of work queued behind the first tile did. This is the argument of [G1](g1-throughput-machines.md#littles-law-how-much-work-has-to-be-in-flight) once more: a pipelined unit reaches its throughput only when enough independent work is in flight to cover its latency.

The TPU applies the same idea twice. One `MatrixMultiply` streams `B` input rows through one weight tile in `B` pipelined cycles, so a large `B` amortizes the fill and drain. And the second weight buffer in the matrix unit hides the 256 cycles of shifting in the next tile behind the current tile's work.[^jouppi2017] When few input rows share each weight tile, neither is enough. For one of the paper's convolutional networks, about 35% of cycles went to waiting for weights to load, during four fully connected layers that run at an operational intensity of only 32.

## What changes for a compiler

A CPU or a GPU makes many decisions while the program runs: which instruction issues next, which warp runs, which line the cache keeps. The TPU makes almost none of them. So every such decision moves into the software that prepares the program, before it runs. In the TPU's stack that software is the **user space driver**: it compiles a model the first time it is evaluated, reformats data into the order the TPU expects, translates the framework's calls into TPU instructions, and caches the resulting program.[^jouppi2017] Four kinds of decision fall to it.

**The schedule.** The order of instructions, and how they overlap, is fixed in advance. The paper describes the aim as keeping the matrix unit busy: weight reads are allowed to complete before their data arrives, so they overlap with multiplies, and between layers the matrix unit waits on explicit synchronization before reading results that the previous layer's activation step wrote.[^jouppi2017] Where a GPU's hardware hides latency with other warps, here the compiler hides it with an instruction order.

**The layout.** Data must be arranged so that each row of 256 inputs and each weight tile can be read in the order the array consumes it. That is what "reformats data into TPU order" means, and it is the kind of choice [decision 43](../decisions/arrays.md#d43) makes once for Vortex's arrays: a fixed order for elements in memory, chosen so the common access walks consecutive addresses.

**The on-chip memory.** The Unified Buffer and the accumulators are software-managed: no hardware decides what stays on chip. The paper sized the accumulators from the roofline's ridge point, about 1,350 operations per byte, rounded up to 2,048, and then doubled that to 4,096 so that the compiler could double-buffer, filling one half while the other is read; the Unified Buffer's size was chosen partly to simplify the compiler.[^jouppi2017] Deciding which tile lives where, and when a DMA moves it, is the compiler's version of the cache blocking of [P8](../optimize/p8-cache-blocking.md), with no cache to fall back on.

**The mapping.** Which dimensions are pinned to the grid, which stream, how large each tile is and how many tiles run back to back: the arithmetic of the last two sections is the compiler's cost model for this choice. It can be computed only if the shapes are known. A compiler for this kind of machine therefore prefers fixed, compile-time shapes, and treats a shape known only at run time as a problem to be solved by padding, by specializing for a few sizes, or by recompiling.

## The rules Vortex already has

Vortex has no accelerator back end, and this book does not propose one. But three of its existing rules decide what such a back end, or any mapping of the same kind onto a GPU's matrix units, would be allowed to do.

Shapes are part of types. The kernel of [G10](g10-matmul-ladder.md) is:

```vortex
// fragment
fn multiply(a: &[f32; 64, 64], b: &[f32; 64, 64], c: &mut [f32; 64, 64]) {
    // the triple loop of stage 10, at a fixed 64 by 64 shape
}
```

Every size in the mapping arithmetic above is a constant here, so tile counts, padding and cycle estimates can be computed while compiling. And [decision 25](../decisions/references.md#d25) guarantees that the storage behind `c` is reachable through no other argument of the call, so a mapping may keep partial results of `c` inside the array for many cycles without a read of `a` or `b` ever seeing them.

The third rule is [decision 56](../decisions/numbers.md#d56): no floating-point operation may be contracted, reassociated or reordered. An output-stationary cell that receives the full reduction, `k` from 0 upward, adds its products in the loop's order, starting from zero. The mapping keeps the bits, provided each cell rounds the product before adding it instead of using a fused multiply-add.

A weight-stationary array arranged as in Figure 2, with a reduction longer than *K*, is different. Each pass through the array sums *K* terms from zero, and the accumulator below adds that partial sum to the running total. That groups the additions differently from the loop, which decision 56 forbids for `f32`. Integer sums give the same result in any order as long as nothing overflows, so integer hardware such as the first TPU's does not face this question.

??? check "A mapping sends the 64 by 64 kernel through a 16 by 16 weight-stationary array, with the reduction dimension split into four passes of 16. Is the result allowed to differ from the loop's in the last bit, and what would an output-stationary mapping of the same kernel on the same array need to keep the bits?"

    No difference is allowed: each element's 64 products would be summed as four groups of 16, each starting from zero, and the groups then added, which reassociates the sum and can change the rounding. An output-stationary mapping keeps the bits if each cell receives all 64 terms in increasing `k`, starting from zero, and rounds each product before adding it (no fused multiply-add). The 64 by 64 output then takes 16 tiles of 16 by 16.

## For Vortex

!!! vortex "Exercise"

    **Build** a systolic mapping report for loop nests in your compiler's IR: for each nest that computes a matrix product over fixed-shape arrays, given a description of a hypothetical *K* by *K* array, report what mapping it onto that array would cost and whether the mapping would keep the program's results.

    1. A target description with three entries: the array side *K*, the dataflow (output-stationary, or weight-stationary arranged as in Figure 2), and whether a cell fuses its multiply and add into one rounding.
    2. Recognition of the product: the two dimensions that index the result and the reduction dimension, found from the subscripts ([P6](../optimize/p6-dependence-analysis.md) gives the tools), with their sizes taken from the array types.
    3. For each dataflow, which dimensions are pinned to the grid and which one streams, the tiles per side, the padded footprint, the fraction of cell-cycles that padding wastes, and the total cycles and utilization for the tiles run back to back, with fill and drain paid once.
    4. A verdict on results: whether the mapping adds each output's products in the loop's order from zero with one rounding per operation, as [decision 56](../decisions/numbers.md#d56) requires, and if not, the reason.
    5. A remark for each nest in the spirit of the [sixth principle](../philosophy.md#6-explain-performance-decisions), such as "output-stationary, K = 24: 3 × 3 tiles, 79.0% of cell-cycles useful; keeps the bits".

    **Not yet:** generating code for any real accelerator or choosing one; representing the schedule in IR, which [M9](../mlir/m9-transform-dialect.md) and [M12](../mlir/m12-vortex-gpu-path.md) discuss; row-stationary and convolution mappings; quantization, which changes results and so could only be an explicit opt-in; any claim about a real chip's speed.

    **Proof that it works:**

    - Golden tests for the `[f32; 64, 64]` kernel, output-stationary: *K* = 16 gives 4 × 4 tiles and 100.0%; *K* = 24 gives 3 × 3 tiles, a 72 × 72 footprint and 79.0%; *K* = 48 gives 44.4%; *K* = 128 gives 25.0%.
    - A golden test for a `[f32; 600, 600]` product on *K* = 256 and *K* = 512: 9 and 4 tile steps, 61.0% and 34.3%, matching this chapter's `utilization.cpp` and the TPU paper's step counts.
    - Cycle tests at *K* = 8: 1 tile takes 22 cycles (36.4%), 64 tiles take 526 cycles (97.3%), matching `pipeline_latency.cpp`.
    - Result verdicts: output-stationary with separate rounding keeps the bits; the same array with a fused cell does not; weight-stationary with *K* = 16 on the 64 by 64 kernel does not, and the remark names the four passes as the reason; weight-stationary with *K* = 64 does, since one pass covers the whole reduction.
    - A differential test: for a few hundred random shapes and array sizes, the report's cycle and utilization figures match a cycle-level simulation like `systolic_sim.cpp`, written independently of the report.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is a systolic array?** A grid of simple, identical cells that pass data to their neighbours in a fixed rhythm, so each value read from memory at the edge is used at every cell it passes.
    - **Why is the input skewed?** So that `A[i][k]` and `B[k][j]` reach cell `(i, j)` on the same cycle, `i + j + k`; the busy cells then form a band of anti-diagonals led by a wavefront.
    - **What is Kung's argument?** A device limited by I/O can only go faster by doing more operations per item it reads; for compute-bound problems such as matrix multiplication, an array that reuses each item at many cells does that without more bandwidth.
    - **What distinguishes output-, weight- and row-stationary dataflows?** Which value stays in a cell: the partial sum, the weight, or a row-sized piece of the convolution that reuses all three; the first TPU is weight-stationary.
    - **Where does a fixed array lose utilization?** In padding, which wastes an area when the array does not divide the matrix, and in fill and drain, which cost `2(K - 1)` cycles per run and are amortized by running many tiles back to back.
    - **Why is compiling for such a machine a problem of schedule, layout and memory?** The hardware has no caches, no out-of-order issue and no thread scheduler to adapt at run time, so software must fix all of these before the program runs.
    - **Which mappings may a Vortex compiler use for `f32`?** Only those that sum each output's products in the loop's order from zero with one rounding per operation, such as output-stationary with the full reduction streamed through each cell.

## Where this comes back

!!! next "You will use this again in"

    - [M9. Schedules as IR: the transform dialect](../mlir/m9-transform-dialect.md): *static schedule*, *tiling*
    - [M11. End-to-end ML compilers](../mlir/m11-ml-compilers.md): *layout*, *accelerator compiler*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *target description*, *utilization*, *keeping the bits*

## Sources and further reading

Read Kung's paper first; its first half is the argument of this chapter and its second half the family of convolution designs. Then read sections 2 and 7 of the TPU paper, and section V.B of the survey for the dataflow names.

[^kung1982]: H. T. Kung, "Why Systolic Architectures?", *Computer*, 15(1):37-46, 1982, doi:10.1109/MC.1982.1653825. Figure 1 and the section on the basic principle for the I/O argument; the convolution designs B1, B2, R1 and W1 for which values stay and which move. <http://www.eecs.harvard.edu/~htk/publication/1982-kung-why-systolic-architecture.pdf>
[^jouppi2017]: Norman P. Jouppi et al., "In-Datacenter Performance Analysis of a Tensor Processing Unit", ISCA 2017, arXiv:1704.04760. Section 2 for the architecture, instructions, systolic data flow and software stack; section 4 for the omitted features, the response-time argument and the utilization counters; section 1 for the cost of 8-bit multiplies. <https://arxiv.org/abs/1704.04760>
[^jouppi2017-s7]: Norman P. Jouppi et al., "In-Datacenter Performance Analysis of a Tensor Processing Unit", ISCA 2017, arXiv:1704.04760, section 7, "Evaluation of Alternative TPU Designs" (the 256 by 256 versus 512 by 512 comparison). <https://arxiv.org/abs/1704.04760>
[^jouppi2023]: Norman P. Jouppi et al., "TPU v4: An Optically Reconfigurable Supercomputer for Machine Learning with Hardware Support for Embeddings", ISCA 2023, arXiv:2304.01433. The TensorCore description, and section 7.5 on the A100's power. <https://arxiv.org/abs/2304.01433>
[^sze2017]: Vivienne Sze, Yu-Hsin Chen, Tien-Ju Yang and Joel S. Emer, "Efficient Processing of Deep Neural Networks: A Tutorial and Survey", arXiv:1703.09039, 2017, section V.B, "Energy-Efficient Dataflow for Accelerators". <https://arxiv.org/abs/1703.09039>
