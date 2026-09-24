# G4. Memory performance: coalescing and bank conflicts

<p class="page-intro">A GPU serves the memory requests of a whole warp of threads at once, and it charges for the pieces of memory their addresses touch, not for the bytes the threads asked for. This chapter counts those pieces in global memory and in shared memory, uses the counts to fix a matrix transpose, and shows that the choices behind them (which index runs across the threads, how a tile is laid out, how wide a load is) belong to a Vortex compiler.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 40 minutes · Builds on: [G3. The GPU memory hierarchy](g3-memory-hierarchy.md)</p>

???+ remember "Before you start, remember"

    ??? question "Which threads of a 32 × 32 thread block form one warp?"

        Thirty-two threads with the same `y` and consecutive `x`: one row of the block. Threads are numbered with `x` varying fastest, and each run of 32 consecutive threads is a warp, which executes one instruction for all of its threads together.

        Introduced in [G2. The SIMT execution model](g2-simt.md).

    ??? question "What is shared memory, and when may a thread read a value that another thread stored there?"

        A small on-chip scratchpad that every thread of one thread block can read and write, much faster than global memory. A thread may read another thread's value only after both have passed a barrier that orders the store before the load.

        Introduced in [G3. The GPU memory hierarchy](g3-memory-hierarchy.md).

    ??? question "In what order are the elements of a `[f32; 64, 64]` array stored?"

        Row after row, the last index varying fastest: `m[i, j + 1]` sits next to `m[i, j]`, and `m[i + 1, j]` is a row, 256 bytes, away.

        Introduced in [Arrays and shapes, decision 43](../decisions/arrays.md#d43).

    ??? question "Why does a loop that walks down a column of a row-major matrix waste a CPU's cache?"

        Memory moves between levels in whole cache lines. A walk along a row uses every value in each line it fetches; a walk down a column uses one value per line and fetches a new line for every read.

        Introduced in [P2. The memory hierarchy](../optimize/p2-memory-hierarchy.md).

    ??? question "May a Vortex compiler regroup the additions in `sum += a[row, k] * b[k, column]`?"

        No. Each floating-point operation is one IEEE 754 operation, rounded once, and must not be contracted, reassociated or reordered.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Count the 32-byte sectors that one warp's load or store touches, and predict what a stride or a misaligned start costs.
    - Count the passes that a shared-memory access needs from its bank mapping, and remove a conflict by padding the tile.
    - Choose which loop index should vary across a warp's lanes, and explain why a transpose needs shared memory whichever index is chosen.
    - Decide when a compiler may emit a 16-byte vector load, and why padding for banks can forbid it.
    - Explain why none of these changes can alter a Vortex program's results.

## Thirty-two addresses, one instruction

Start with a scaled copy:

```vortex
// items: valid
fn scale(x: &[f32; 4096], y: &mut [f32; 4096]) {
    for i in 0..4096 {
        y[i] = 2.0 * x[i];
    }
}
```

A GPU version runs one thread per element: thread `i` loads `x[i]`, multiplies it and stores `y[i]`. On NVIDIA GPUs, threads execute in warps of 32 ([G2](g2-simt.md)), one instruction for all 32 threads, which are the warp's **lanes**. So the load of `x[i]` is not one address. It is one instruction carrying 32 addresses, the start of `x` plus 4 × `i` for 32 consecutive values of `i`: 128 neighbouring bytes.

The memory system does not serve that as 32 separate 4-byte reads. Instead, the CUDA Programming Guide explains, global memory is accessed in 32-byte transactions, and the warp combines the requests of its threads into as many transactions as it takes to cover them.[^pg-coalesce] NVIDIA's profiler calls the unit a **sector**, an aligned 32-byte chunk of memory; four sectors make one 128-byte cache line.[^ncu-terms] If `x` starts on a sector boundary, as arrays allocated by the CUDA runtime do (it aligns them to at least 256 bytes),[^bp-coalesce] the 128 bytes of this load fill exactly four sectors, and every byte moved is a byte some lane asked for. This merging of the lanes' requests into whole sectors is called **coalescing**.

Now change one thing: thread `i` reads `x[8 * i]` from a longer array. Neighbouring lanes are now 32 bytes apart, so every address falls in a sector of its own. The warp moves 32 sectors, 1,024 bytes, to deliver 128 useful bytes, a utilization of 12.5 percent, which the Programming Guide gives as the worst case.[^pg-coalesce]

<figure class="vx-figure">
<svg viewBox="0 0 760 430" role="img" aria-label="How many 32-byte sectors one warp-wide load touches, for three strides" aria-describedby="g4-f1-desc">
<title id="g4-f1-title">How many 32-byte sectors one warp-wide load touches, for three strides</title>
<desc id="g4-f1-desc">Three rows. In each, 32 small boxes stand for the 32 lanes of a warp, and lines run from each lane to the 32-byte sector that holds its f32 value. With stride 1 the 32 lines converge on 4 sectors, all fully used: 128 bytes moved. With stride 2 they land on 8 sectors, each half used: 256 bytes moved. With stride 8 every lane has its own sector, each one eighth used: 1,024 bytes moved for the same 128 useful bytes.</desc>
<text class="vx-text" x="20" y="24">One load instruction: 32 lanes, one f32 (4 bytes) each</text>
<text class="vx-text" x="20" y="62">stride 1</text>
<text class="vx-text-muted" x="20" y="84">4 sectors</text>
<text class="vx-text-muted" x="20" y="101">128 bytes moved</text>
<text class="vx-text-muted" x="20" y="118">every byte used</text>
<line class="vx-line" x1="187.5" y1="64" x2="425.5" y2="116"/>
<line class="vx-line" x1="204.5" y1="64" x2="425.5" y2="116"/>
<line class="vx-line" x1="221.5" y1="64" x2="425.5" y2="116"/>
<line class="vx-line" x1="238.5" y1="64" x2="425.5" y2="116"/>
<line class="vx-line" x1="255.5" y1="64" x2="425.5" y2="116"/>
<line class="vx-line" x1="272.5" y1="64" x2="425.5" y2="116"/>
<line class="vx-line" x1="289.5" y1="64" x2="425.5" y2="116"/>
<line class="vx-line" x1="306.5" y1="64" x2="425.5" y2="116"/>
<line class="vx-line" x1="323.5" y1="64" x2="442.5" y2="116"/>
<line class="vx-line" x1="340.5" y1="64" x2="442.5" y2="116"/>
<line class="vx-line" x1="357.5" y1="64" x2="442.5" y2="116"/>
<line class="vx-line" x1="374.5" y1="64" x2="442.5" y2="116"/>
<line class="vx-line" x1="391.5" y1="64" x2="442.5" y2="116"/>
<line class="vx-line" x1="408.5" y1="64" x2="442.5" y2="116"/>
<line class="vx-line" x1="425.5" y1="64" x2="442.5" y2="116"/>
<line class="vx-line" x1="442.5" y1="64" x2="442.5" y2="116"/>
<line class="vx-line" x1="459.5" y1="64" x2="459.5" y2="116"/>
<line class="vx-line" x1="476.5" y1="64" x2="459.5" y2="116"/>
<line class="vx-line" x1="493.5" y1="64" x2="459.5" y2="116"/>
<line class="vx-line" x1="510.5" y1="64" x2="459.5" y2="116"/>
<line class="vx-line" x1="527.5" y1="64" x2="459.5" y2="116"/>
<line class="vx-line" x1="544.5" y1="64" x2="459.5" y2="116"/>
<line class="vx-line" x1="561.5" y1="64" x2="459.5" y2="116"/>
<line class="vx-line" x1="578.5" y1="64" x2="459.5" y2="116"/>
<line class="vx-line" x1="595.5" y1="64" x2="476.5" y2="116"/>
<line class="vx-line" x1="612.5" y1="64" x2="476.5" y2="116"/>
<line class="vx-line" x1="629.5" y1="64" x2="476.5" y2="116"/>
<line class="vx-line" x1="646.5" y1="64" x2="476.5" y2="116"/>
<line class="vx-line" x1="663.5" y1="64" x2="476.5" y2="116"/>
<line class="vx-line" x1="680.5" y1="64" x2="476.5" y2="116"/>
<line class="vx-line" x1="697.5" y1="64" x2="476.5" y2="116"/>
<line class="vx-line" x1="714.5" y1="64" x2="476.5" y2="116"/>
<rect class="vx-box-strong" x="180" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="197" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="214" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="231" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="248" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="265" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="282" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="299" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="316" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="333" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="350" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="367" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="384" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="401" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="418" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="435" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="452" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="469" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="486" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="503" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="520" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="537" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="554" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="571" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="588" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="605" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="622" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="639" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="656" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="673" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="690" y="50" width="15" height="14"/>
<rect class="vx-box-strong" x="707" y="50" width="15" height="14"/>
<rect class="vx-box" x="418" y="116" width="15" height="26"/>
<rect class="vx-cell-on" x="418" y="116" width="15" height="26"/>
<rect class="vx-box" x="435" y="116" width="15" height="26"/>
<rect class="vx-cell-on" x="435" y="116" width="15" height="26"/>
<rect class="vx-box" x="452" y="116" width="15" height="26"/>
<rect class="vx-cell-on" x="452" y="116" width="15" height="26"/>
<rect class="vx-box" x="469" y="116" width="15" height="26"/>
<rect class="vx-cell-on" x="469" y="116" width="15" height="26"/>
<text class="vx-text" x="20" y="190">stride 2</text>
<text class="vx-text-muted" x="20" y="212">8 sectors</text>
<text class="vx-text-muted" x="20" y="229">256 bytes moved</text>
<text class="vx-text-muted" x="20" y="246">half the bytes used</text>
<line class="vx-line" x1="187.5" y1="192" x2="391.5" y2="244"/>
<line class="vx-line" x1="204.5" y1="192" x2="391.5" y2="244"/>
<line class="vx-line" x1="221.5" y1="192" x2="391.5" y2="244"/>
<line class="vx-line" x1="238.5" y1="192" x2="391.5" y2="244"/>
<line class="vx-line" x1="255.5" y1="192" x2="408.5" y2="244"/>
<line class="vx-line" x1="272.5" y1="192" x2="408.5" y2="244"/>
<line class="vx-line" x1="289.5" y1="192" x2="408.5" y2="244"/>
<line class="vx-line" x1="306.5" y1="192" x2="408.5" y2="244"/>
<line class="vx-line" x1="323.5" y1="192" x2="425.5" y2="244"/>
<line class="vx-line" x1="340.5" y1="192" x2="425.5" y2="244"/>
<line class="vx-line" x1="357.5" y1="192" x2="425.5" y2="244"/>
<line class="vx-line" x1="374.5" y1="192" x2="425.5" y2="244"/>
<line class="vx-line" x1="391.5" y1="192" x2="442.5" y2="244"/>
<line class="vx-line" x1="408.5" y1="192" x2="442.5" y2="244"/>
<line class="vx-line" x1="425.5" y1="192" x2="442.5" y2="244"/>
<line class="vx-line" x1="442.5" y1="192" x2="442.5" y2="244"/>
<line class="vx-line" x1="459.5" y1="192" x2="459.5" y2="244"/>
<line class="vx-line" x1="476.5" y1="192" x2="459.5" y2="244"/>
<line class="vx-line" x1="493.5" y1="192" x2="459.5" y2="244"/>
<line class="vx-line" x1="510.5" y1="192" x2="459.5" y2="244"/>
<line class="vx-line" x1="527.5" y1="192" x2="476.5" y2="244"/>
<line class="vx-line" x1="544.5" y1="192" x2="476.5" y2="244"/>
<line class="vx-line" x1="561.5" y1="192" x2="476.5" y2="244"/>
<line class="vx-line" x1="578.5" y1="192" x2="476.5" y2="244"/>
<line class="vx-line" x1="595.5" y1="192" x2="493.5" y2="244"/>
<line class="vx-line" x1="612.5" y1="192" x2="493.5" y2="244"/>
<line class="vx-line" x1="629.5" y1="192" x2="493.5" y2="244"/>
<line class="vx-line" x1="646.5" y1="192" x2="493.5" y2="244"/>
<line class="vx-line" x1="663.5" y1="192" x2="510.5" y2="244"/>
<line class="vx-line" x1="680.5" y1="192" x2="510.5" y2="244"/>
<line class="vx-line" x1="697.5" y1="192" x2="510.5" y2="244"/>
<line class="vx-line" x1="714.5" y1="192" x2="510.5" y2="244"/>
<rect class="vx-box-strong" x="180" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="197" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="214" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="231" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="248" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="265" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="282" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="299" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="316" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="333" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="350" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="367" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="384" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="401" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="418" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="435" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="452" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="469" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="486" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="503" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="520" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="537" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="554" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="571" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="588" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="605" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="622" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="639" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="656" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="673" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="690" y="178" width="15" height="14"/>
<rect class="vx-box-strong" x="707" y="178" width="15" height="14"/>
<rect class="vx-box" x="384" y="244" width="15" height="26"/>
<rect class="vx-cell-on" x="384" y="257" width="15" height="13"/>
<rect class="vx-box" x="401" y="244" width="15" height="26"/>
<rect class="vx-cell-on" x="401" y="257" width="15" height="13"/>
<rect class="vx-box" x="418" y="244" width="15" height="26"/>
<rect class="vx-cell-on" x="418" y="257" width="15" height="13"/>
<rect class="vx-box" x="435" y="244" width="15" height="26"/>
<rect class="vx-cell-on" x="435" y="257" width="15" height="13"/>
<rect class="vx-box" x="452" y="244" width="15" height="26"/>
<rect class="vx-cell-on" x="452" y="257" width="15" height="13"/>
<rect class="vx-box" x="469" y="244" width="15" height="26"/>
<rect class="vx-cell-on" x="469" y="257" width="15" height="13"/>
<rect class="vx-box" x="486" y="244" width="15" height="26"/>
<rect class="vx-cell-on" x="486" y="257" width="15" height="13"/>
<rect class="vx-box" x="503" y="244" width="15" height="26"/>
<rect class="vx-cell-on" x="503" y="257" width="15" height="13"/>
<text class="vx-text" x="20" y="318">stride 8</text>
<text class="vx-text-muted" x="20" y="340">32 sectors</text>
<text class="vx-text-muted" x="20" y="357">1,024 bytes moved</text>
<text class="vx-text-muted" x="20" y="374">1 byte in 8 used</text>
<line class="vx-line" x1="187.5" y1="320" x2="187.5" y2="372"/>
<line class="vx-line" x1="204.5" y1="320" x2="204.5" y2="372"/>
<line class="vx-line" x1="221.5" y1="320" x2="221.5" y2="372"/>
<line class="vx-line" x1="238.5" y1="320" x2="238.5" y2="372"/>
<line class="vx-line" x1="255.5" y1="320" x2="255.5" y2="372"/>
<line class="vx-line" x1="272.5" y1="320" x2="272.5" y2="372"/>
<line class="vx-line" x1="289.5" y1="320" x2="289.5" y2="372"/>
<line class="vx-line" x1="306.5" y1="320" x2="306.5" y2="372"/>
<line class="vx-line" x1="323.5" y1="320" x2="323.5" y2="372"/>
<line class="vx-line" x1="340.5" y1="320" x2="340.5" y2="372"/>
<line class="vx-line" x1="357.5" y1="320" x2="357.5" y2="372"/>
<line class="vx-line" x1="374.5" y1="320" x2="374.5" y2="372"/>
<line class="vx-line" x1="391.5" y1="320" x2="391.5" y2="372"/>
<line class="vx-line" x1="408.5" y1="320" x2="408.5" y2="372"/>
<line class="vx-line" x1="425.5" y1="320" x2="425.5" y2="372"/>
<line class="vx-line" x1="442.5" y1="320" x2="442.5" y2="372"/>
<line class="vx-line" x1="459.5" y1="320" x2="459.5" y2="372"/>
<line class="vx-line" x1="476.5" y1="320" x2="476.5" y2="372"/>
<line class="vx-line" x1="493.5" y1="320" x2="493.5" y2="372"/>
<line class="vx-line" x1="510.5" y1="320" x2="510.5" y2="372"/>
<line class="vx-line" x1="527.5" y1="320" x2="527.5" y2="372"/>
<line class="vx-line" x1="544.5" y1="320" x2="544.5" y2="372"/>
<line class="vx-line" x1="561.5" y1="320" x2="561.5" y2="372"/>
<line class="vx-line" x1="578.5" y1="320" x2="578.5" y2="372"/>
<line class="vx-line" x1="595.5" y1="320" x2="595.5" y2="372"/>
<line class="vx-line" x1="612.5" y1="320" x2="612.5" y2="372"/>
<line class="vx-line" x1="629.5" y1="320" x2="629.5" y2="372"/>
<line class="vx-line" x1="646.5" y1="320" x2="646.5" y2="372"/>
<line class="vx-line" x1="663.5" y1="320" x2="663.5" y2="372"/>
<line class="vx-line" x1="680.5" y1="320" x2="680.5" y2="372"/>
<line class="vx-line" x1="697.5" y1="320" x2="697.5" y2="372"/>
<line class="vx-line" x1="714.5" y1="320" x2="714.5" y2="372"/>
<rect class="vx-box-strong" x="180" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="197" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="214" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="231" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="248" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="265" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="282" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="299" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="316" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="333" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="350" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="367" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="384" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="401" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="418" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="435" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="452" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="469" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="486" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="503" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="520" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="537" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="554" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="571" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="588" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="605" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="622" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="639" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="656" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="673" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="690" y="306" width="15" height="14"/>
<rect class="vx-box-strong" x="707" y="306" width="15" height="14"/>
<rect class="vx-box" x="180" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="180" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="197" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="197" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="214" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="214" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="231" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="231" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="248" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="248" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="265" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="265" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="282" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="282" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="299" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="299" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="316" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="316" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="333" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="333" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="350" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="350" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="367" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="367" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="384" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="384" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="401" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="401" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="418" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="418" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="435" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="435" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="452" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="452" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="469" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="469" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="486" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="486" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="503" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="503" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="520" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="520" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="537" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="537" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="554" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="554" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="571" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="571" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="588" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="588" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="605" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="605" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="622" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="622" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="639" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="639" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="656" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="656" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="673" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="673" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="690" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="690" y="394.8" width="15" height="3.2"/>
<rect class="vx-box" x="707" y="372" width="15" height="26"/>
<rect class="vx-cell-on" x="707" y="394.8" width="15" height="3.2"/>
<text class="vx-text-muted" x="180" y="44">lane 0</text>
<text class="vx-text-muted" x="722" y="44" text-anchor="end">lane 31</text>
<text class="vx-text-muted" x="20" y="424">each lower box is one 32-byte sector, at the same scale in every row; shading shows the share used</text>
</svg>
<figcaption>Figure 1. One warp-wide load of <code>f32</code> values at three strides. Each lane's line ends at the 32-byte sector that holds its value, and the shading shows how much of each sector the warp uses. The useful bytes are the same 128 in every row; what grows with the stride is the number of sectors moved.</figcaption>
</figure>

Both cases follow one rule. On NVIDIA GPUs of compute capability (NVIDIA's version number for a GPU's features) 6.0 and later, the accesses of a warp coalesce into as many 32-byte transactions as are needed to serve all its threads.[^bp-coalesce] Put as a cost: **one warp-wide access costs the number of distinct aligned 32-byte sectors that its 32 addresses touch.** The **stride** of an access, the distance between the addresses of neighbouring lanes, predicts most of that number. Strides here count `f32` elements unless they name bytes: stride 1 is 4 bytes and stride 8 is 32 bytes. The first example counts the common cases.

--8<-- "includes/examples/gpu/g4-memory-performance/sectors.cpp.md"

Order inside the sectors does not matter: the reversed access still costs four, as does any permutation within the four sectors.[^bp-coalesce] Alignment does: the same 128 bytes starting 16 bytes into a sector straddle five sectors.[^bp-coalesce] Doubling the stride doubles the cost until the stride reaches 32 bytes; beyond that every lane has its own sector, so walking down a column of a 64-wide matrix costs no more than stride 8. A word that every lane reads costs one sector. Sixteen bytes per lane fill 16 sectors, the ideal for 128-bit accesses in the profiler's documentation.[^ncu-l1]

A sector count is what a request asks of the first-level cache, not what reaches DRAM: some sectors may already be cached, and the profiler reports hits and misses separately.[^ncu-l1] For misaligned copies on a Tesla V100, NVIDIA expected four-fifths of the aligned throughput from the sector count and measured about nine-tenths, because adjacent warps reused lines their neighbours had already fetched.[^bp-coalesce] For large strides, Mark Harris measured poor bandwidth on every architecture he tried: addresses far apart leave the hardware nothing to combine.[^harris-global]

The 32-byte figure is NVIDIA's current rule, not a law. Harris describes compute capability 1.0 hardware, which coalesced the accesses of half a warp, 16 threads, only when they were aligned and in sequence, and 2.0 hardware, which combined a warp's accesses into as few 128-byte lines as possible.[^harris-global] AMD's coalescing hardware groups a warp's addresses into the fewest cache-line requests, and AMD recommends aligning data to 64-byte or 128-byte lines.[^amd-coalesce]

Apple's Metal Shading Language Specification does not describe how the accesses of a SIMD-group, Apple's name for a warp, combine when they reach device memory, Metal's name for global memory.[^msl] On the owner's M4 Pro, a Metal compute pipeline reports a SIMD-group width (`threadExecutionWidth`) of 32 (checked on 2026-09-24), so the same experiments apply there, with the sector size measured rather than assumed. The rule of thumb carries across all of them: neighbouring lanes should touch neighbouring addresses. The sizes belong to the target.

??? check "Lane `t` of a warp reads `x[t + 2]` from an `f32` array whose first element starts a sector. How many sectors does the load touch? How many for `x[2 * t + 1]`?"

    Five for the first: the lanes cover bytes 8 to 135, which reach into sectors 0 to 4, so 160 bytes move for 128 used. Eight for the second: lane `t` reads bytes 4 + 8t to 7 + 8t, which together span bytes 4 to 255, sectors 0 to 7, each half used.

## Which index runs across the warp

Now the kernel Vortex was built for, from [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for), with 64 × 64 matrices:

```vortex
// items: valid
fn multiply(a: &[f32; 64, 64], b: &[f32; 64, 64], c: &mut [f32; 64, 64]) {
    for row in 0..64 {
        for column in 0..64 {
            let mut sum: f32 = 0.0;
            for k in 0..64 {
                sum += a[row, k] * b[k, column];
            }
            c[row, column] = sum;
        }
    }
}
```

The simplest GPU version runs the two outer loops in parallel: one thread for each `(row, column)` pair, each thread running the whole `k` loop and storing one element of `c`. It is the first rung of the ladder in [G10](g10-matmul-ladder.md). Every version of it does the same arithmetic. What the compiler must still decide is the **thread mapping**: which of `row` and `column` varies across the 32 lanes of a warp, and which stays fixed.

Count one step of the `k` loop for one warp, both ways (Figure 2). If the lanes vary `column`, all 32 lanes read the same `a[row, k]`, one sector whose value every lane receives; they read 32 neighbouring elements of row `k` of `b`, four sectors; and at the end they store 32 neighbouring elements of `c`, four sectors. If the lanes vary `row`, the roles swap: `a[row, k]` gives 32 addresses a row, 256 bytes, apart, 32 sectors; `b[k, column]` is one sector; and the store to `c` runs down a column, 32 sectors.

<figure class="vx-figure">
<svg viewBox="0 0 760 420" role="img" aria-label="The three accesses of one warp in the matrix product, under two thread mappings" aria-describedby="g4-f2-desc">
<title id="g4-f2-title">The three accesses of one warp in the matrix product, under two thread mappings</title>
<desc id="g4-f2-desc">Two rows of three squares, each square a 64 by 64 matrix: a, b and c. In the top row the 32 lanes of a warp vary column and share row. All lanes read the same element of a, one sector; they read 32 neighbouring elements of row k of b, four sectors; and at the end they store 32 neighbouring elements of c, four sectors. In the bottom row the lanes vary row and share column. They read 32 elements of column k of a, each in its own sector, 32 sectors; one element of b, one sector; and they store 32 elements down a column of c, 32 sectors.</desc>
<text class="vx-text" x="20" y="24">One warp of the product, 64 × 64 f32 matrices: which index do the lanes vary?</text>
<text class="vx-text" x="20" y="110">lanes vary column</text>
<text class="vx-text-muted" x="20" y="128">all 32 lanes share</text>
<text class="vx-text-muted" x="20" y="144">one row</text>
<text class="vx-mono" x="220" y="60" text-anchor="middle">a[row, k]</text>
<rect class="vx-box" x="170" y="70" width="100" height="100"/>
<rect class="vx-cell-on" x="230.5" y="99.2" width="7" height="7"/>
<text class="vx-text-muted" x="220" y="192" text-anchor="middle">1 sector: one address</text>
<text class="vx-mono" x="380" y="60" text-anchor="middle">b[k, column]</text>
<rect class="vx-box" x="330" y="70" width="100" height="100"/>
<rect class="vx-cell-on" x="330" y="130.5" width="50" height="6"/>
<text class="vx-text-muted" x="380" y="192" text-anchor="middle">4 sectors: 32 neighbours</text>
<text class="vx-mono" x="540" y="60" text-anchor="middle">c[row, column]</text>
<rect class="vx-box" x="490" y="70" width="100" height="100"/>
<rect class="vx-cell-on" x="490" y="99.2" width="50" height="6"/>
<text class="vx-text-muted" x="540" y="192" text-anchor="middle">4 sectors</text>
<text class="vx-text-muted" x="630" y="96">loads per k step:</text>
<text class="vx-text-accent" x="630" y="115">5 sectors</text>
<text class="vx-text-muted" x="630" y="134">store at the end:</text>
<text class="vx-text-accent" x="630" y="153">4 sectors</text>
<text class="vx-text" x="20" y="290">lanes vary row</text>
<text class="vx-text-muted" x="20" y="308">all 32 lanes share</text>
<text class="vx-text-muted" x="20" y="324">one column</text>
<text class="vx-mono" x="220" y="240" text-anchor="middle">a[row, k]</text>
<rect class="vx-box" x="170" y="250" width="100" height="100"/>
<rect class="vx-cell-on" x="230.5" y="250" width="6" height="50"/>
<text class="vx-text-muted" x="220" y="372" text-anchor="middle">32 sectors: 256 B apart</text>
<text class="vx-mono" x="380" y="240" text-anchor="middle">b[k, column]</text>
<rect class="vx-box" x="330" y="250" width="100" height="100"/>
<rect class="vx-cell-on" x="359.2" y="310.5" width="7" height="7"/>
<text class="vx-text-muted" x="380" y="372" text-anchor="middle">1 sector: one address</text>
<text class="vx-mono" x="540" y="240" text-anchor="middle">c[row, column]</text>
<rect class="vx-box" x="490" y="250" width="100" height="100"/>
<rect class="vx-cell-on" x="519.2" y="250" width="6" height="50"/>
<text class="vx-text-muted" x="540" y="372" text-anchor="middle">32 sectors</text>
<text class="vx-text-muted" x="630" y="276">loads per k step:</text>
<text class="vx-text-accent" x="630" y="295">33 sectors</text>
<text class="vx-text-muted" x="630" y="314">store at the end:</text>
<text class="vx-text-accent" x="630" y="333">32 sectors</text>
<line class="vx-line" x1="20" y1="212" x2="740" y2="212"/>
<text class="vx-text-muted" x="20" y="410">each highlight marks what one warp touches: row and k are fixed, and the 32 lanes cover a block of 32 rows or columns</text>
</svg>
<figcaption>Figure 2. One warp of the matrix product under two thread mappings. When the lanes vary <code>column</code>, the warp reads one element of <code>a</code> and a stretch of a row of <code>b</code>, and stores a stretch of a row of <code>c</code>. When they vary <code>row</code>, the reads of <code>a</code> and the store to <code>c</code> run down columns, 256 bytes between neighbouring lanes. The arithmetic is identical; the sectors are not.</figcaption>
</figure>

That is 5 sectors against 33 for every step of `k`, and 4 against 32 for the store. The ratio of sectors is not the ratio of times. In the second mapping, the sector a lane fetches for `a[row, k]` also holds its values for up to seven later steps of `k`, and a cache may still hold that sector when the lane needs them. The Best Practices Guide weighs the same hope in its own matrix product: with many warps running on one streaming multiprocessor, the line may be evicted between iterations.[^bp-ab]

Only a measurement settles it, and Simon Boehm made one. On an RTX A6000 multiplying 4092 × 4092 `f32` matrices, his naive kernel gave consecutive threads consecutive rows and reached 309 GFLOP/s; giving them consecutive columns instead raised that to 1,986.5 GFLOP/s, and the memory throughput he measured rose from 15 GB/s to 110 GB/s.[^boehm]

Boehm adds an observation that matters more to a compiler writer: the change left the load instructions in the assembly as they were, because coalescing happens in hardware at run time.[^boehm] The compiler decides the mapping; the hardware merges whatever addresses the mapping produces. Pick the wrong index and the kernel is still correct, several times slower, and its loads look no different.

The choice can be made from the program text. For each access, ask how far its address moves when the loop variable given to the lanes grows by one. In a Vortex access `m[i, j]` to an array of type `[f32; R, C]`, stored row-major by [decision 43](../decisions/arrays.md#d43), one step of `j` moves 4 bytes and one step of `i` moves 4 × C bytes, whatever the loop bounds are.

A stride of 0 is a **broadcast**, one value for every lane, costing one sector. A stride of 4 bytes coalesces into four sectors. A stride of 32 bytes or more costs a sector per lane. So the lanes should vary the loop variable that is the last subscript of the accesses that run most often: here `column`, the last subscript of both `b` and `c`.

The same arithmetic covers structs. If lane `i` reads `points[i].x` from an array of type `[Point; 4096]`, where `Point` holds two `f32` fields `x` and `y`, the stride is the size of a whole `Point`, 8 bytes if the implementation adds no padding, so half of every sector moved holds `y` values that no lane asked for. Two separate arrays, one of `x` values and one of `y` values, bring the stride back to 4 bytes; AMD's guide recommends this **structure-of-arrays** layout for the same reason.[^amd-coalesce] [Decision 43](../decisions/arrays.md#d43) stores an array's elements whole and a struct's fields in declaration order, so choosing between the two layouts is the programmer's decision. The compiler's part is to report the stride.

Vortex gives a compiler every fact this needs. Shapes are fixed, so each stride is a constant. The storage order is decided, not left to each implementation. And a subscript keeps its dimensions, where C's `a[x * K + i]` flattens them into one expression in which `K` may be known only at run time (compare [P6](../optimize/p6-dependence-analysis.md)). The [philosophy](../philosophy.md#performance-philosophy) lists coalesced global-memory access among the GPU optimizations Vortex should eventually support, and its [sixth principle](../philosophy.md#6-explain-performance-decisions) asks the compiler to report what it did, which here takes one line: "mapped `column` to lanes: `b` and `c` coalesce, `a` is broadcast".

Now a kernel where the rule runs out. A transpose writes each element into the mirrored position of another matrix:

```vortex
// items: valid
fn transpose(source: &[f32; 64, 64], target: &mut [f32; 64, 64]) {
    for i in 0..64 {
        for j in 0..64 {
            target[j, i] = source[i, j];
        }
    }
}
```

Work out both mappings before reading on.

??? check "With the lanes varying `j`, how many sectors do one warp's load and store touch? With the lanes varying `i`?"

    Varying `j`: the load `source[i, j]` has a stride of 4 bytes, four sectors, and the store `target[j, i]` has a stride of 256 bytes, 32 sectors. Varying `i` swaps the two. No mapping makes both coalesce, because the index that comes last in the read comes first in the write.

## Shared memory and its banks

A transpose needs a place to turn the data around: read a **tile**, a small square block of the matrix, along its rows, where the source is contiguous, and write it out along the rows of the target, where the target is contiguous. The shared memory of [G3](g3-memory-hierarchy.md) is that place, and the Programming Guide's own transpose uses it this way.[^pg-transpose] Shared memory, though, has a rule of its own.

Take a 32 × 32 tile of `f32` values in shared memory, stored row by row, and let a warp read column 5. Lane `t` reads element `[t, 5]`, which is word 32t + 5 of the tile.

On NVIDIA GPUs, shared memory is divided into 32 **banks**, arranged so that successive 32-bit words fall in successive banks: word `w` lives in bank `w mod 32`.[^pg-banks] Each bank delivers one 32-bit word per clock cycle, so an access whose 32 addresses fall in 32 different banks is served all at once.[^bp-banks]

Column 5 is the opposite case. Word 32t + 5 lies in bank 5 for every `t`, so all 32 lanes want different words from one bank. That is a **bank conflict**. The hardware splits a conflicting request into as many conflict-free requests as it needs and serves them one after another, dividing the bandwidth it delivers by their number.[^bp-banks] Call each of those requests a **pass**: reading this column takes 32 passes.

Two cases escape the rule. Lanes that read the same word receive it together, by broadcast, in one pass. Lanes that write the same word leave one of their values, and which one is undefined.[^pg-banks] NVIDIA's profiler calls each serialized pass a **wavefront**, and its shared memory table counts wavefronts and bank conflicts in separate columns.[^ncu-terms][^ncu-smem]

When lane `t` reads word s·t + c, a stride of `s` words with `s` at least 1, the number of passes is gcd(s, 32). Two lanes `t` and `t'` share a bank when s·(t − t') is a multiple of 32, which happens exactly when t − t' is a multiple of 32 / gcd(s, 32). Each bank that is hit therefore serves gcd(s, 32) lanes, all wanting different words.

Any odd stride is free of conflicts; stride 2 takes two passes, a two-way conflict; a column of a 32-wide tile, stride 32, is the worst case there is. (Stride 0 is the broadcast above: one pass.) The Programming Guide draws strides 1, 2 and 3 and reaches the same verdicts.[^pg-banks]

The fix costs one word per row. Declare the tile 32 × 33 and never use the last column. Element `[t, c]` is then word 33t + c, in bank (t + c) mod 32, which differs for every `t`. A column read becomes stride 33, which the modular arithmetic of the bank mapping turns into stride 1, as the Best Practices Guide puts it.[^bp-aat] This is **padding**: spending a little shared memory to shift the bank in which each row starts. Figure 3 shows the effect on a toy machine with 8 banks.

<figure class="vx-figure">
<svg viewBox="0 0 760 380" role="img" aria-label="Which bank holds each word of a tile, without and with padding" aria-describedby="g4-f3-desc">
<title id="g4-f3-title">Which bank holds each word of a tile, without and with padding</title>
<desc id="g4-f3-desc">A toy machine with 8 banks and 8 lanes. Left, an 8 by 8 tile stored row by row: every cell in a column shows the same bank number, so 8 lanes reading one column all wait on one bank and need 8 passes. Right, the same tile with one unused column of padding, 8 by 9: the bank numbers in each column are all different, so reading a column takes one pass. Columns 0 to 3 are highlighted in turn.</desc>
<text class="vx-text" x="20" y="24">A toy with 8 banks and 8 lanes; word w sits in bank w mod 8</text>
<text class="vx-text-muted" x="20" y="44">each cell shows its bank; NVIDIA GPUs have 32 banks and 32 lanes, and the pattern is the same</text>
<text class="vx-text" x="60" y="72">8 × 8 tile</text>
<rect class="vx-box" x="60" y="84" width="28" height="28"/>
<rect class="vx-box" x="88" y="84" width="28" height="28"/>
<rect class="vx-box" x="116" y="84" width="28" height="28"/>
<rect class="vx-box" x="144" y="84" width="28" height="28"/>
<rect class="vx-box" x="172" y="84" width="28" height="28"/>
<rect class="vx-box" x="200" y="84" width="28" height="28"/>
<rect class="vx-box" x="228" y="84" width="28" height="28"/>
<rect class="vx-box" x="256" y="84" width="28" height="28"/>
<rect class="vx-box" x="60" y="112" width="28" height="28"/>
<rect class="vx-box" x="88" y="112" width="28" height="28"/>
<rect class="vx-box" x="116" y="112" width="28" height="28"/>
<rect class="vx-box" x="144" y="112" width="28" height="28"/>
<rect class="vx-box" x="172" y="112" width="28" height="28"/>
<rect class="vx-box" x="200" y="112" width="28" height="28"/>
<rect class="vx-box" x="228" y="112" width="28" height="28"/>
<rect class="vx-box" x="256" y="112" width="28" height="28"/>
<rect class="vx-box" x="60" y="140" width="28" height="28"/>
<rect class="vx-box" x="88" y="140" width="28" height="28"/>
<rect class="vx-box" x="116" y="140" width="28" height="28"/>
<rect class="vx-box" x="144" y="140" width="28" height="28"/>
<rect class="vx-box" x="172" y="140" width="28" height="28"/>
<rect class="vx-box" x="200" y="140" width="28" height="28"/>
<rect class="vx-box" x="228" y="140" width="28" height="28"/>
<rect class="vx-box" x="256" y="140" width="28" height="28"/>
<rect class="vx-box" x="60" y="168" width="28" height="28"/>
<rect class="vx-box" x="88" y="168" width="28" height="28"/>
<rect class="vx-box" x="116" y="168" width="28" height="28"/>
<rect class="vx-box" x="144" y="168" width="28" height="28"/>
<rect class="vx-box" x="172" y="168" width="28" height="28"/>
<rect class="vx-box" x="200" y="168" width="28" height="28"/>
<rect class="vx-box" x="228" y="168" width="28" height="28"/>
<rect class="vx-box" x="256" y="168" width="28" height="28"/>
<rect class="vx-box" x="60" y="196" width="28" height="28"/>
<rect class="vx-box" x="88" y="196" width="28" height="28"/>
<rect class="vx-box" x="116" y="196" width="28" height="28"/>
<rect class="vx-box" x="144" y="196" width="28" height="28"/>
<rect class="vx-box" x="172" y="196" width="28" height="28"/>
<rect class="vx-box" x="200" y="196" width="28" height="28"/>
<rect class="vx-box" x="228" y="196" width="28" height="28"/>
<rect class="vx-box" x="256" y="196" width="28" height="28"/>
<rect class="vx-box" x="60" y="224" width="28" height="28"/>
<rect class="vx-box" x="88" y="224" width="28" height="28"/>
<rect class="vx-box" x="116" y="224" width="28" height="28"/>
<rect class="vx-box" x="144" y="224" width="28" height="28"/>
<rect class="vx-box" x="172" y="224" width="28" height="28"/>
<rect class="vx-box" x="200" y="224" width="28" height="28"/>
<rect class="vx-box" x="228" y="224" width="28" height="28"/>
<rect class="vx-box" x="256" y="224" width="28" height="28"/>
<rect class="vx-box" x="60" y="252" width="28" height="28"/>
<rect class="vx-box" x="88" y="252" width="28" height="28"/>
<rect class="vx-box" x="116" y="252" width="28" height="28"/>
<rect class="vx-box" x="144" y="252" width="28" height="28"/>
<rect class="vx-box" x="172" y="252" width="28" height="28"/>
<rect class="vx-box" x="200" y="252" width="28" height="28"/>
<rect class="vx-box" x="228" y="252" width="28" height="28"/>
<rect class="vx-box" x="256" y="252" width="28" height="28"/>
<rect class="vx-box" x="60" y="280" width="28" height="28"/>
<rect class="vx-box" x="88" y="280" width="28" height="28"/>
<rect class="vx-box" x="116" y="280" width="28" height="28"/>
<rect class="vx-box" x="144" y="280" width="28" height="28"/>
<rect class="vx-box" x="172" y="280" width="28" height="28"/>
<rect class="vx-box" x="200" y="280" width="28" height="28"/>
<rect class="vx-box" x="228" y="280" width="28" height="28"/>
<rect class="vx-box" x="256" y="280" width="28" height="28"/>
<text class="vx-text" x="420" y="72">8 × 9 tile: one column of padding</text>
<rect class="vx-box" x="420" y="84" width="28" height="28"/>
<rect class="vx-box" x="448" y="84" width="28" height="28"/>
<rect class="vx-box" x="476" y="84" width="28" height="28"/>
<rect class="vx-box" x="504" y="84" width="28" height="28"/>
<rect class="vx-box" x="532" y="84" width="28" height="28"/>
<rect class="vx-box" x="560" y="84" width="28" height="28"/>
<rect class="vx-box" x="588" y="84" width="28" height="28"/>
<rect class="vx-box" x="616" y="84" width="28" height="28"/>
<rect class="vx-box-bad" x="644" y="84" width="28" height="28"/>
<rect class="vx-box" x="420" y="112" width="28" height="28"/>
<rect class="vx-box" x="448" y="112" width="28" height="28"/>
<rect class="vx-box" x="476" y="112" width="28" height="28"/>
<rect class="vx-box" x="504" y="112" width="28" height="28"/>
<rect class="vx-box" x="532" y="112" width="28" height="28"/>
<rect class="vx-box" x="560" y="112" width="28" height="28"/>
<rect class="vx-box" x="588" y="112" width="28" height="28"/>
<rect class="vx-box" x="616" y="112" width="28" height="28"/>
<rect class="vx-box-bad" x="644" y="112" width="28" height="28"/>
<rect class="vx-box" x="420" y="140" width="28" height="28"/>
<rect class="vx-box" x="448" y="140" width="28" height="28"/>
<rect class="vx-box" x="476" y="140" width="28" height="28"/>
<rect class="vx-box" x="504" y="140" width="28" height="28"/>
<rect class="vx-box" x="532" y="140" width="28" height="28"/>
<rect class="vx-box" x="560" y="140" width="28" height="28"/>
<rect class="vx-box" x="588" y="140" width="28" height="28"/>
<rect class="vx-box" x="616" y="140" width="28" height="28"/>
<rect class="vx-box-bad" x="644" y="140" width="28" height="28"/>
<rect class="vx-box" x="420" y="168" width="28" height="28"/>
<rect class="vx-box" x="448" y="168" width="28" height="28"/>
<rect class="vx-box" x="476" y="168" width="28" height="28"/>
<rect class="vx-box" x="504" y="168" width="28" height="28"/>
<rect class="vx-box" x="532" y="168" width="28" height="28"/>
<rect class="vx-box" x="560" y="168" width="28" height="28"/>
<rect class="vx-box" x="588" y="168" width="28" height="28"/>
<rect class="vx-box" x="616" y="168" width="28" height="28"/>
<rect class="vx-box-bad" x="644" y="168" width="28" height="28"/>
<rect class="vx-box" x="420" y="196" width="28" height="28"/>
<rect class="vx-box" x="448" y="196" width="28" height="28"/>
<rect class="vx-box" x="476" y="196" width="28" height="28"/>
<rect class="vx-box" x="504" y="196" width="28" height="28"/>
<rect class="vx-box" x="532" y="196" width="28" height="28"/>
<rect class="vx-box" x="560" y="196" width="28" height="28"/>
<rect class="vx-box" x="588" y="196" width="28" height="28"/>
<rect class="vx-box" x="616" y="196" width="28" height="28"/>
<rect class="vx-box-bad" x="644" y="196" width="28" height="28"/>
<rect class="vx-box" x="420" y="224" width="28" height="28"/>
<rect class="vx-box" x="448" y="224" width="28" height="28"/>
<rect class="vx-box" x="476" y="224" width="28" height="28"/>
<rect class="vx-box" x="504" y="224" width="28" height="28"/>
<rect class="vx-box" x="532" y="224" width="28" height="28"/>
<rect class="vx-box" x="560" y="224" width="28" height="28"/>
<rect class="vx-box" x="588" y="224" width="28" height="28"/>
<rect class="vx-box" x="616" y="224" width="28" height="28"/>
<rect class="vx-box-bad" x="644" y="224" width="28" height="28"/>
<rect class="vx-box" x="420" y="252" width="28" height="28"/>
<rect class="vx-box" x="448" y="252" width="28" height="28"/>
<rect class="vx-box" x="476" y="252" width="28" height="28"/>
<rect class="vx-box" x="504" y="252" width="28" height="28"/>
<rect class="vx-box" x="532" y="252" width="28" height="28"/>
<rect class="vx-box" x="560" y="252" width="28" height="28"/>
<rect class="vx-box" x="588" y="252" width="28" height="28"/>
<rect class="vx-box" x="616" y="252" width="28" height="28"/>
<rect class="vx-box-bad" x="644" y="252" width="28" height="28"/>
<rect class="vx-box" x="420" y="280" width="28" height="28"/>
<rect class="vx-box" x="448" y="280" width="28" height="28"/>
<rect class="vx-box" x="476" y="280" width="28" height="28"/>
<rect class="vx-box" x="504" y="280" width="28" height="28"/>
<rect class="vx-box" x="532" y="280" width="28" height="28"/>
<rect class="vx-box" x="560" y="280" width="28" height="28"/>
<rect class="vx-box" x="588" y="280" width="28" height="28"/>
<rect class="vx-box" x="616" y="280" width="28" height="28"/>
<rect class="vx-box-bad" x="644" y="280" width="28" height="28"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 4">
<rect class="vx-box-accent" x="60" y="84" width="28" height="28"/>
<rect class="vx-box-accent" x="60" y="112" width="28" height="28"/>
<rect class="vx-box-accent" x="60" y="140" width="28" height="28"/>
<rect class="vx-box-accent" x="60" y="168" width="28" height="28"/>
<rect class="vx-box-accent" x="60" y="196" width="28" height="28"/>
<rect class="vx-box-accent" x="60" y="224" width="28" height="28"/>
<rect class="vx-box-accent" x="60" y="252" width="28" height="28"/>
<rect class="vx-box-accent" x="60" y="280" width="28" height="28"/>
<rect class="vx-box-accent" x="420" y="84" width="28" height="28"/>
<rect class="vx-box-accent" x="420" y="112" width="28" height="28"/>
<rect class="vx-box-accent" x="420" y="140" width="28" height="28"/>
<rect class="vx-box-accent" x="420" y="168" width="28" height="28"/>
<rect class="vx-box-accent" x="420" y="196" width="28" height="28"/>
<rect class="vx-box-accent" x="420" y="224" width="28" height="28"/>
<rect class="vx-box-accent" x="420" y="252" width="28" height="28"/>
<rect class="vx-box-accent" x="420" y="280" width="28" height="28"/>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4">
<rect class="vx-box-accent" x="88" y="84" width="28" height="28"/>
<rect class="vx-box-accent" x="88" y="112" width="28" height="28"/>
<rect class="vx-box-accent" x="88" y="140" width="28" height="28"/>
<rect class="vx-box-accent" x="88" y="168" width="28" height="28"/>
<rect class="vx-box-accent" x="88" y="196" width="28" height="28"/>
<rect class="vx-box-accent" x="88" y="224" width="28" height="28"/>
<rect class="vx-box-accent" x="88" y="252" width="28" height="28"/>
<rect class="vx-box-accent" x="88" y="280" width="28" height="28"/>
<rect class="vx-box-accent" x="448" y="84" width="28" height="28"/>
<rect class="vx-box-accent" x="448" y="112" width="28" height="28"/>
<rect class="vx-box-accent" x="448" y="140" width="28" height="28"/>
<rect class="vx-box-accent" x="448" y="168" width="28" height="28"/>
<rect class="vx-box-accent" x="448" y="196" width="28" height="28"/>
<rect class="vx-box-accent" x="448" y="224" width="28" height="28"/>
<rect class="vx-box-accent" x="448" y="252" width="28" height="28"/>
<rect class="vx-box-accent" x="448" y="280" width="28" height="28"/>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4">
<rect class="vx-box-accent" x="116" y="84" width="28" height="28"/>
<rect class="vx-box-accent" x="116" y="112" width="28" height="28"/>
<rect class="vx-box-accent" x="116" y="140" width="28" height="28"/>
<rect class="vx-box-accent" x="116" y="168" width="28" height="28"/>
<rect class="vx-box-accent" x="116" y="196" width="28" height="28"/>
<rect class="vx-box-accent" x="116" y="224" width="28" height="28"/>
<rect class="vx-box-accent" x="116" y="252" width="28" height="28"/>
<rect class="vx-box-accent" x="116" y="280" width="28" height="28"/>
<rect class="vx-box-accent" x="476" y="84" width="28" height="28"/>
<rect class="vx-box-accent" x="476" y="112" width="28" height="28"/>
<rect class="vx-box-accent" x="476" y="140" width="28" height="28"/>
<rect class="vx-box-accent" x="476" y="168" width="28" height="28"/>
<rect class="vx-box-accent" x="476" y="196" width="28" height="28"/>
<rect class="vx-box-accent" x="476" y="224" width="28" height="28"/>
<rect class="vx-box-accent" x="476" y="252" width="28" height="28"/>
<rect class="vx-box-accent" x="476" y="280" width="28" height="28"/>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 4">
<rect class="vx-box-accent" x="144" y="84" width="28" height="28"/>
<rect class="vx-box-accent" x="144" y="112" width="28" height="28"/>
<rect class="vx-box-accent" x="144" y="140" width="28" height="28"/>
<rect class="vx-box-accent" x="144" y="168" width="28" height="28"/>
<rect class="vx-box-accent" x="144" y="196" width="28" height="28"/>
<rect class="vx-box-accent" x="144" y="224" width="28" height="28"/>
<rect class="vx-box-accent" x="144" y="252" width="28" height="28"/>
<rect class="vx-box-accent" x="144" y="280" width="28" height="28"/>
<rect class="vx-box-accent" x="504" y="84" width="28" height="28"/>
<rect class="vx-box-accent" x="504" y="112" width="28" height="28"/>
<rect class="vx-box-accent" x="504" y="140" width="28" height="28"/>
<rect class="vx-box-accent" x="504" y="168" width="28" height="28"/>
<rect class="vx-box-accent" x="504" y="196" width="28" height="28"/>
<rect class="vx-box-accent" x="504" y="224" width="28" height="28"/>
<rect class="vx-box-accent" x="504" y="252" width="28" height="28"/>
<rect class="vx-box-accent" x="504" y="280" width="28" height="28"/>
</g>
<text class="vx-mono" x="74" y="103" text-anchor="middle">0</text>
<text class="vx-mono" x="102" y="103" text-anchor="middle">1</text>
<text class="vx-mono" x="130" y="103" text-anchor="middle">2</text>
<text class="vx-mono" x="158" y="103" text-anchor="middle">3</text>
<text class="vx-mono" x="186" y="103" text-anchor="middle">4</text>
<text class="vx-mono" x="214" y="103" text-anchor="middle">5</text>
<text class="vx-mono" x="242" y="103" text-anchor="middle">6</text>
<text class="vx-mono" x="270" y="103" text-anchor="middle">7</text>
<text class="vx-mono" x="74" y="131" text-anchor="middle">0</text>
<text class="vx-mono" x="102" y="131" text-anchor="middle">1</text>
<text class="vx-mono" x="130" y="131" text-anchor="middle">2</text>
<text class="vx-mono" x="158" y="131" text-anchor="middle">3</text>
<text class="vx-mono" x="186" y="131" text-anchor="middle">4</text>
<text class="vx-mono" x="214" y="131" text-anchor="middle">5</text>
<text class="vx-mono" x="242" y="131" text-anchor="middle">6</text>
<text class="vx-mono" x="270" y="131" text-anchor="middle">7</text>
<text class="vx-mono" x="74" y="159" text-anchor="middle">0</text>
<text class="vx-mono" x="102" y="159" text-anchor="middle">1</text>
<text class="vx-mono" x="130" y="159" text-anchor="middle">2</text>
<text class="vx-mono" x="158" y="159" text-anchor="middle">3</text>
<text class="vx-mono" x="186" y="159" text-anchor="middle">4</text>
<text class="vx-mono" x="214" y="159" text-anchor="middle">5</text>
<text class="vx-mono" x="242" y="159" text-anchor="middle">6</text>
<text class="vx-mono" x="270" y="159" text-anchor="middle">7</text>
<text class="vx-mono" x="74" y="187" text-anchor="middle">0</text>
<text class="vx-mono" x="102" y="187" text-anchor="middle">1</text>
<text class="vx-mono" x="130" y="187" text-anchor="middle">2</text>
<text class="vx-mono" x="158" y="187" text-anchor="middle">3</text>
<text class="vx-mono" x="186" y="187" text-anchor="middle">4</text>
<text class="vx-mono" x="214" y="187" text-anchor="middle">5</text>
<text class="vx-mono" x="242" y="187" text-anchor="middle">6</text>
<text class="vx-mono" x="270" y="187" text-anchor="middle">7</text>
<text class="vx-mono" x="74" y="215" text-anchor="middle">0</text>
<text class="vx-mono" x="102" y="215" text-anchor="middle">1</text>
<text class="vx-mono" x="130" y="215" text-anchor="middle">2</text>
<text class="vx-mono" x="158" y="215" text-anchor="middle">3</text>
<text class="vx-mono" x="186" y="215" text-anchor="middle">4</text>
<text class="vx-mono" x="214" y="215" text-anchor="middle">5</text>
<text class="vx-mono" x="242" y="215" text-anchor="middle">6</text>
<text class="vx-mono" x="270" y="215" text-anchor="middle">7</text>
<text class="vx-mono" x="74" y="243" text-anchor="middle">0</text>
<text class="vx-mono" x="102" y="243" text-anchor="middle">1</text>
<text class="vx-mono" x="130" y="243" text-anchor="middle">2</text>
<text class="vx-mono" x="158" y="243" text-anchor="middle">3</text>
<text class="vx-mono" x="186" y="243" text-anchor="middle">4</text>
<text class="vx-mono" x="214" y="243" text-anchor="middle">5</text>
<text class="vx-mono" x="242" y="243" text-anchor="middle">6</text>
<text class="vx-mono" x="270" y="243" text-anchor="middle">7</text>
<text class="vx-mono" x="74" y="271" text-anchor="middle">0</text>
<text class="vx-mono" x="102" y="271" text-anchor="middle">1</text>
<text class="vx-mono" x="130" y="271" text-anchor="middle">2</text>
<text class="vx-mono" x="158" y="271" text-anchor="middle">3</text>
<text class="vx-mono" x="186" y="271" text-anchor="middle">4</text>
<text class="vx-mono" x="214" y="271" text-anchor="middle">5</text>
<text class="vx-mono" x="242" y="271" text-anchor="middle">6</text>
<text class="vx-mono" x="270" y="271" text-anchor="middle">7</text>
<text class="vx-mono" x="74" y="299" text-anchor="middle">0</text>
<text class="vx-mono" x="102" y="299" text-anchor="middle">1</text>
<text class="vx-mono" x="130" y="299" text-anchor="middle">2</text>
<text class="vx-mono" x="158" y="299" text-anchor="middle">3</text>
<text class="vx-mono" x="186" y="299" text-anchor="middle">4</text>
<text class="vx-mono" x="214" y="299" text-anchor="middle">5</text>
<text class="vx-mono" x="242" y="299" text-anchor="middle">6</text>
<text class="vx-mono" x="270" y="299" text-anchor="middle">7</text>
<text class="vx-mono" x="434" y="103" text-anchor="middle">0</text>
<text class="vx-mono" x="462" y="103" text-anchor="middle">1</text>
<text class="vx-mono" x="490" y="103" text-anchor="middle">2</text>
<text class="vx-mono" x="518" y="103" text-anchor="middle">3</text>
<text class="vx-mono" x="546" y="103" text-anchor="middle">4</text>
<text class="vx-mono" x="574" y="103" text-anchor="middle">5</text>
<text class="vx-mono" x="602" y="103" text-anchor="middle">6</text>
<text class="vx-mono" x="630" y="103" text-anchor="middle">7</text>
<text class="vx-mono" x="658" y="103" text-anchor="middle">0</text>
<text class="vx-mono" x="434" y="131" text-anchor="middle">1</text>
<text class="vx-mono" x="462" y="131" text-anchor="middle">2</text>
<text class="vx-mono" x="490" y="131" text-anchor="middle">3</text>
<text class="vx-mono" x="518" y="131" text-anchor="middle">4</text>
<text class="vx-mono" x="546" y="131" text-anchor="middle">5</text>
<text class="vx-mono" x="574" y="131" text-anchor="middle">6</text>
<text class="vx-mono" x="602" y="131" text-anchor="middle">7</text>
<text class="vx-mono" x="630" y="131" text-anchor="middle">0</text>
<text class="vx-mono" x="658" y="131" text-anchor="middle">1</text>
<text class="vx-mono" x="434" y="159" text-anchor="middle">2</text>
<text class="vx-mono" x="462" y="159" text-anchor="middle">3</text>
<text class="vx-mono" x="490" y="159" text-anchor="middle">4</text>
<text class="vx-mono" x="518" y="159" text-anchor="middle">5</text>
<text class="vx-mono" x="546" y="159" text-anchor="middle">6</text>
<text class="vx-mono" x="574" y="159" text-anchor="middle">7</text>
<text class="vx-mono" x="602" y="159" text-anchor="middle">0</text>
<text class="vx-mono" x="630" y="159" text-anchor="middle">1</text>
<text class="vx-mono" x="658" y="159" text-anchor="middle">2</text>
<text class="vx-mono" x="434" y="187" text-anchor="middle">3</text>
<text class="vx-mono" x="462" y="187" text-anchor="middle">4</text>
<text class="vx-mono" x="490" y="187" text-anchor="middle">5</text>
<text class="vx-mono" x="518" y="187" text-anchor="middle">6</text>
<text class="vx-mono" x="546" y="187" text-anchor="middle">7</text>
<text class="vx-mono" x="574" y="187" text-anchor="middle">0</text>
<text class="vx-mono" x="602" y="187" text-anchor="middle">1</text>
<text class="vx-mono" x="630" y="187" text-anchor="middle">2</text>
<text class="vx-mono" x="658" y="187" text-anchor="middle">3</text>
<text class="vx-mono" x="434" y="215" text-anchor="middle">4</text>
<text class="vx-mono" x="462" y="215" text-anchor="middle">5</text>
<text class="vx-mono" x="490" y="215" text-anchor="middle">6</text>
<text class="vx-mono" x="518" y="215" text-anchor="middle">7</text>
<text class="vx-mono" x="546" y="215" text-anchor="middle">0</text>
<text class="vx-mono" x="574" y="215" text-anchor="middle">1</text>
<text class="vx-mono" x="602" y="215" text-anchor="middle">2</text>
<text class="vx-mono" x="630" y="215" text-anchor="middle">3</text>
<text class="vx-mono" x="658" y="215" text-anchor="middle">4</text>
<text class="vx-mono" x="434" y="243" text-anchor="middle">5</text>
<text class="vx-mono" x="462" y="243" text-anchor="middle">6</text>
<text class="vx-mono" x="490" y="243" text-anchor="middle">7</text>
<text class="vx-mono" x="518" y="243" text-anchor="middle">0</text>
<text class="vx-mono" x="546" y="243" text-anchor="middle">1</text>
<text class="vx-mono" x="574" y="243" text-anchor="middle">2</text>
<text class="vx-mono" x="602" y="243" text-anchor="middle">3</text>
<text class="vx-mono" x="630" y="243" text-anchor="middle">4</text>
<text class="vx-mono" x="658" y="243" text-anchor="middle">5</text>
<text class="vx-mono" x="434" y="271" text-anchor="middle">6</text>
<text class="vx-mono" x="462" y="271" text-anchor="middle">7</text>
<text class="vx-mono" x="490" y="271" text-anchor="middle">0</text>
<text class="vx-mono" x="518" y="271" text-anchor="middle">1</text>
<text class="vx-mono" x="546" y="271" text-anchor="middle">2</text>
<text class="vx-mono" x="574" y="271" text-anchor="middle">3</text>
<text class="vx-mono" x="602" y="271" text-anchor="middle">4</text>
<text class="vx-mono" x="630" y="271" text-anchor="middle">5</text>
<text class="vx-mono" x="658" y="271" text-anchor="middle">6</text>
<text class="vx-mono" x="434" y="299" text-anchor="middle">7</text>
<text class="vx-mono" x="462" y="299" text-anchor="middle">0</text>
<text class="vx-mono" x="490" y="299" text-anchor="middle">1</text>
<text class="vx-mono" x="518" y="299" text-anchor="middle">2</text>
<text class="vx-mono" x="546" y="299" text-anchor="middle">3</text>
<text class="vx-mono" x="574" y="299" text-anchor="middle">4</text>
<text class="vx-mono" x="602" y="299" text-anchor="middle">5</text>
<text class="vx-mono" x="630" y="299" text-anchor="middle">6</text>
<text class="vx-mono" x="658" y="299" text-anchor="middle">7</text>
<text class="vx-text-muted" x="60" y="334">a column is one bank:</text>
<text class="vx-text-muted" x="60" y="351">8 lanes queue, 8 passes</text>
<text class="vx-text-muted" x="420" y="334">a column spans all 8 banks:</text>
<text class="vx-text-muted" x="420" y="351">1 pass; the dashed cells are never read</text>
</svg>
<figcaption>Figure 3. The bank of every word of a tile, on a toy machine with 8 banks. Without padding, each column lies in a single bank, so 8 lanes reading a column need 8 passes. With one unused word at the end of each row, each row starts one bank later, and every column spans all 8 banks. NVIDIA's shared memory has 32 banks, and a 32 × 33 tile behaves the same way.</figcaption>
</figure>

The second example counts passes for strides and for the tiles above, checking the gcd rule against a direct count.

--8<-- "includes/examples/gpu/g4-memory-performance/bank_conflicts.cpp.md"

??? check "A 32 × 32 tile is padded to 32 × 34 instead. How many passes does a warp need to read one column?"

    Two. The stride is 34 words and gcd(34, 32) = 2, so each bank that is hit serves two lanes that want different words. Any odd row length removes the conflict, and 33 is the smallest.

This model covers 4-byte words, the case NVIDIA documents for compute capability 5.x and later, where the warp size and the number of banks are both 32.[^bp-banks] Other machines differ. NVIDIA's compute capability 1.x had 16 banks, and 3.x could switch to 8-byte banks.[^harris-shared] AMD's local data share (LDS), its equivalent of shared memory, has 32 or 64 banks of 4 bytes depending on the architecture family, and AMD quotes different throughputs for 4-byte and 16-byte values.[^amd-lds] The Metal Shading Language Specification defines threadgroup memory, Metal's name for shared memory, without describing banks.[^msl] Bank count and bank width are therefore properties of the target, and for accesses wider than one word the vendor's rules or a profiler decide.

## Case study: a transpose, counted and measured

A transpose does no arithmetic. It reads each byte once and writes it once, exactly as a copy does, so a copy of the same size is its speed limit. Harris's study of the transpose measures both and reports **effective bandwidth**: twice the size of the matrix (read once, written once) divided by the running time.[^harris-transpose]

Here the matrix is 64 × 64 `f32` values, processed by 32 × 32 thread blocks, so a warp is one row of a block.[^pg-threads][^pg-transpose-conflicts] There are four versions:

1. **Copy**: the lanes vary `j`, read `source[i, j]` and write `target[i, j]`. Both accesses coalesce. This is the limit.
2. **Naive transpose**: the Vortex kernel above, with the lanes varying `j`. The load coalesces; each warp's store touches 32 sectors.
3. **Tiled**: each block reads a 32 × 32 tile of `source` row by row, coalesced, and stores it row by row into a shared tile, free of conflicts. It then waits at a **barrier** ([G6](g6-synchronization.md)), a point that no thread of the block passes until every thread has reached it, because each thread is about to read elements that other threads stored.[^pg-transpose] Finally each warp reads one column of the shared tile and writes it as a row of `target`, coalesced. The column read is a 32-way conflict.
4. **Tiled and padded**: the same, with a 32 × 33 tile.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="Transposing one 32 by 32 tile through shared memory" aria-describedby="g4-f4-desc">
<title id="g4-f4-title">Transposing one 32 by 32 tile through shared memory</title>
<desc id="g4-f4-desc">Three squares: an input tile in global memory, a tile in shared memory with one extra column of padding, and the output tile in global memory. In step 1 a warp reads one row of the input tile, which is coalesced, and writes it as one row of the shared tile. After a barrier, in step 2, a warp reads one column of the shared tile and writes it as one row of the output tile, which is coalesced again. The column read is the only strided access, and it happens in shared memory, where the padding keeps it free of bank conflicts.</desc>
<text class="vx-text" x="110" y="62" text-anchor="middle">input tile</text>
<text class="vx-text-muted" x="110" y="79" text-anchor="middle">global memory</text>
<rect class="vx-box" x="40" y="90" width="140" height="140"/>
<text class="vx-text" x="380" y="62" text-anchor="middle">shared tile</text>
<text class="vx-text-muted" x="380" y="79" text-anchor="middle">32 × 33, with padding</text>
<rect class="vx-box" x="310" y="90" width="140" height="140"/>
<text class="vx-text" x="650" y="62" text-anchor="middle">output tile</text>
<text class="vx-text-muted" x="650" y="79" text-anchor="middle">global memory</text>
<rect class="vx-box" x="580" y="90" width="140" height="140"/>
<rect class="vx-box-bad" x="450" y="90" width="8" height="140"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 2">
<rect class="vx-box-accent" x="40" y="130" width="140" height="10"/>
<rect class="vx-box-accent" x="310" y="130" width="140" height="10"/>
<line class="vx-line" x1="186" y1="135" x2="300" y2="135"/>
<polygon class="vx-arrowhead" points="300,130 308,135 300,140"/>
<text class="vx-text-accent" x="245" y="124" text-anchor="middle">step 1</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 2">
<rect class="vx-box-accent" x="380" y="90" width="10" height="140"/>
<rect class="vx-box-accent" x="580" y="160" width="140" height="10"/>
<line class="vx-line" x1="464" y1="165" x2="570" y2="165"/>
<polygon class="vx-arrowhead" points="570,160 578,165 570,170"/>
<text class="vx-text-accent" x="517" y="154" text-anchor="middle">step 2</text>
</g>
<text class="vx-text-muted" x="110" y="258" text-anchor="middle">step 1 reads rows:</text>
<text class="vx-text-muted" x="110" y="275" text-anchor="middle">neighbouring lanes,</text>
<text class="vx-text-muted" x="110" y="292" text-anchor="middle">neighbouring addresses</text>
<text class="vx-text-muted" x="380" y="258" text-anchor="middle">written by rows, read by columns:</text>
<text class="vx-text-muted" x="380" y="275" text-anchor="middle">32 × 32 tile: 32-way conflict</text>
<text class="vx-text-muted" x="380" y="292" text-anchor="middle">32 × 33 tile: no conflict</text>
<text class="vx-text-muted" x="650" y="258" text-anchor="middle">step 2 writes rows:</text>
<text class="vx-text-muted" x="650" y="275" text-anchor="middle">neighbouring lanes,</text>
<text class="vx-text-muted" x="650" y="292" text-anchor="middle">neighbouring addresses</text>
<text class="vx-text-muted" x="517" y="190" text-anchor="middle">after a barrier</text>
</svg>
<figcaption>Figure 4. The tiled transpose. Step 1 reads rows of the input tile and stores them as rows of the shared tile; step 2, after a barrier, reads columns of the shared tile and writes them as rows of the output. Both global accesses coalesce. The strided access happens in shared memory, where the padding column (dashed) keeps it free of bank conflicts.</figcaption>
</figure>

The third example runs all four versions on the CPU, warp by warp, counting sectors for global memory and passes for shared memory, and checks bit for bit that every transpose puts each input value in its mirrored place.

--8<-- "includes/examples/gpu/g4-memory-performance/transpose_costs.cpp.md"

The matrix is 16,384 bytes, 512 sectors. The copy reads 512 sectors and writes 512. The naive transpose reads the same 512 but writes 4,096, one sector per lane for each of 128 warps: four and a half times the copy's total traffic. Staging through shared memory brings global traffic back to exactly the copy's and moves the strided access on chip, where it costs 32 passes per warp: 4,224 passes in all, 128 for the row stores and 4,096 for the column reads. Padding cuts the shared-memory side to one pass per instruction, 256 in all.

Harris measured the same four versions, plus a copy that stages each tile through shared memory as the tiled transpose does, on 1024 × 1024 matrices with error-correcting memory (ECC) on, using 32 × 8 thread blocks in which each thread moves four elements:[^harris-transpose]

| Kernel | Tesla M2050 (GB/s) | Tesla K20c (GB/s) |
| --- | --- | --- |
| copy | 105.2 | 136.0 |
| copy through a shared tile | 104.6 | 152.3 |
| naive transpose | 18.8 | 55.3 |
| tiled, 32 × 32 tile | 51.3 | 97.6 |
| tiled, 32 × 33 tile | 99.5 | 144.3 |

The counts separate the slow versions from the fast ones, but they cannot rank versions whose counts are equal: on the K20c the padded transpose even beat the plain copy. Padding brought the tiled kernel to about 95 percent of the faster copy on each GPU, and Harris concludes that coalescing is by far the most important of the steps.[^harris-transpose]

The Best Practices Guide shows the same pattern inside a matrix product. Its kernel for C = AAᵀ reads `A` with a whole row between neighbouring lanes, and on a Tesla V100 it ran at 12.8 GB/s; staging tiles through shared memory, one of them transposed, raised that to 140.2 GB/s, and padding the transposed tile raised it to 199.4 GB/s.[^bp-aat] Shared memory has a second use in matrix kernels, reuse: in the guide's C = AB kernels, its gain comes from avoiding repeated reads of the same data, not from coalescing.[^bp-ab] [G10](g10-matmul-ladder.md) needs both. These GPUs are old: the numbers show the shape of the effect, and your own measurements should replace them.

A compiler that generates the tiled kernel has to write the padding down somewhere. In MLIR's `gpu` dialect, a kernel function can declare its shared memory as a **memory attribution**: a buffer that belongs to the function itself, placed in `workgroup` memory, which all threads of one thread block share, with a type that carries its shape.[^mlir-gpu] The fourth example is the padded kernel written that way; `mlir-opt` 18 parses, verifies and prints it. The padding lives in the type, `memref<32x33xf32, #gpu.address_space<workgroup>>`, so every later pass sees it. [M10](../mlir/m10-mlir-for-gpus.md) takes this path further.

--8<-- "includes/examples/gpu/g4-memory-performance/transpose_tile.mlir.md"

??? check "The Programming Guide's version stores into its shared tile down a column and reads it along a row. Which of the two shared-memory accesses conflicts, and does a 32 × 33 tile still fix it?"

    The store: lane `t` writes word 32t + c of a 32 × 32 tile, every lane in one bank, a 32-way conflict, while the row read is free of conflicts. With 33-word rows the store writes word 33t + c, one lane per bank. Padding fixes a column access in either direction, and the guide draws the same two tiles.[^pg-transpose-conflicts]

## Wider loads, and who may issue them

A lane can load 8 or 16 bytes at once, such as four neighbouring `f32` values, with one **vector load**. A warp of 16-byte loads touches 16 sectors, 512 bytes, all of them used: the last row of the first example. The gain is in instructions. Justin Luitjens and Rajeshwari Devaramani report that vector loads raise bandwidth and cut the instruction count; their copy kernels executed a half and a quarter as many instructions with 8-byte and 16-byte accesses.[^vec] The cost is in registers: each lane holds more values at once, so a kernel already short of registers or of parallelism may do better with scalar loads.[^vec] [G5](g5-occupancy.md) weighs that trade.

Vector loads come with a rule. **PTX** (Parallel Thread Execution), the virtual instruction set that CUDA compilers generate, requires every memory access to be aligned to its size: a 16-byte `ld.v4.b32` needs an address that is a multiple of 16, and a misaligned address has undefined behavior.[^ptx-align] So a compiler must prove the alignment before it emits one. Boehm met this in his worklog. Four scalar loads through a `float*` did not become one 128-bit load, which he attributes to the compiler having no way to verify that the pointer was aligned; casting the pointer to `float4*` served as his promise that it was.[^boehm]

LLVM's pass for the job is the **load-store vectorizer**, which merges neighbouring loads or stores into vector ones. Its source says that nothing in it is specific to GPUs but that NVIDIA and AMD GPUs motivated it: on those machines a vector load fills a series of scalar registers, so taking the vector apart again costs nothing.[^llvm-lsv] LLVM's back ends for NVIDIA and AMD GPUs, NVPTX and AMDGPU, both add it to their pipelines.[^llvm-gpu-pipelines] It merges a chain of neighbouring accesses only when the chain's alignment is a multiple of its size or the target accepts the misaligned access,[^llvm-lsv] and LLVM's generic target rules accept none.[^llvm-tti]

The last example asks where alignment comes from. Its first function loads four neighbours from a row of a 64 × 64 array aligned to 16 bytes; its second does the same from a padded 32 × 33 tile.

--8<-- "includes/examples/gpu/g4-memory-performance/vector_loads.ll.md"

In the square array, element `[i, 4q]` sits 256i + 16q bytes from the start, always a multiple of 16. LLVM's `infer-alignment` pass proves that from the array's alignment and shape alone, and the vectorizer then emits one `<4 x float>` load. In the padded tile a row is 132 bytes long, so `[i, 4q]` is only known to be 4-byte aligned, and the loads stay scalar.

That is a real conflict between this chapter's two fixes, and arithmetic shows that padding cannot settle it. A column read of 4-byte words is free of conflicts only when the row length in words is odd, and every row starts on a 16-byte boundary only when the row length is a multiple of 4. No number is both, so with padding a compiler must decide which access to favour.

Padding is not the only way to rearrange a tile, either: PTX's tensor copies can write shared memory in **swizzled** layouts, which hold the data in a different order from global memory "for access performance reasons".[^ptx-swizzle] In PTX's tables the unit that moves is a 16-byte piece of a row, and the pieces trade places from one row to the next, so each piece keeps its 16-byte alignment while neighbouring rows stop lining up bank for bank. [G10](g10-matmul-ladder.md) returns to them.

Vortex starts in a better position than C. [Decision 43](../decisions/arrays.md#d43) fixes the order of the elements and leaves sizes, alignment and padding to the implementation, which must document them. A compiler that places every array on a 16-byte boundary, and says so, can work out the alignment of an access from the shape and the subscripts, as `infer-alignment` did, and needs no promise from the programmer.

## What does not change: the bits

Every technique in this chapter moves values or changes which thread handles which element; none changes an arithmetic operation. A new thread mapping leaves each `c[row, column]` adding its products in increasing `k` from `0.0`, as [decision 56](../decisions/numbers.md#d56) requires. Staging copies values bit for bit, padding adds words that nothing reads, and a vector load reads the same bytes as four scalar loads. The transpose example checks the naive and staged versions bit for bit. So a Vortex compiler may apply all of them without asking the programmer. The parallel sums of [G6](g6-synchronization.md) are different: splitting one sum among threads changes the order of its additions, and so can change the bits.

One condition goes beyond arithmetic. Every GPU version runs the iterations of the loop nest at the same time, and the tiled transpose holds a tile of `source` on chip while other blocks write `target`. Both are safe only if no store to `target` can change `source`. A C compiler given two pointers must assume that they might overlap, unless the programmer promises otherwise. A Vortex compiler knows they cannot: [decision 25](../decisions/references.md#d25) forbids the variable lent as `&mut` for `target` from appearing in any other argument of the same call.

## Measuring it

No GPU timings are claimed here. Collect your own:

1. Write the four transposes by hand, in Metal Shading Language for the M4 Pro or in CUDA on a rented NVIDIA GPU. On the owner's machine, a small program that compiles MSL source text at run time works without the offline Metal toolchain (checked on 2026-09-24).
2. Check every output against a CPU transpose, bit for bit, before timing anything.
3. Time many runs of each kernel and report the median with its spread, following [P1](../optimize/p1-measure-first.md).
4. Compute the effective bandwidth as 2 × N² × 4 bytes divided by the median time, for an N × N matrix. Grow N until the copy's effective bandwidth stops changing: a matrix small enough to stay in the caches flatters every kernel.
5. On NVIDIA, read Sectors/Req from the L1/TEX table of Nsight Compute and the bank conflicts from its shared memory table,[^ncu-l1][^ncu-smem] and compare them with the counts of the third example. [G14](g14-measuring-gpu-code.md) covers the tools, including Xcode's for Metal.

| Kernel | N | Median time | Effective bandwidth | % of copy | Sectors/Req | Bank conflicts |
| --- | --- | --- | --- | --- | --- | --- |
| copy | | | | | | |
| naive transpose | | | | | | |
| tiled, 32 × 32 tile | | | | | | |
| tiled, 32 × 33 tile | | | | | | |

Record the machine, the operating system or driver version, the compiler and the date with the table.

## For Vortex

!!! vortex "Exercise"

    **Build** a memory-access report for loop nests in your compiler's IR: for a nest with constant bounds and affine subscripts ([P6](../optimize/p6-dependence-analysis.md)), and a choice of one parallel loop variable to vary across the lanes of a warp, state what every array access costs one warp.

    1. A target description holding the facts this chapter used: warp width, sector size, bank count, bank width, and the alignment a vector access needs. Fill it for NVIDIA from the sources cited here, and mark each Apple entry you have not measured on the M4 Pro as unknown.
    2. For each access, its byte stride with respect to the lane variable, computed from the subscripts, the fixed shape and the element sizes in your layout document, and its class: broadcast, coalesced or strided, with the number of sectors per warp request.
    3. A mapping chooser that tries each parallel loop variable as the lane variable and keeps the one whose accesses touch the fewest sectors over the whole nest, breaking ties by the order the loops are written in.
    4. For a proposed shared tile, given its row length and element type, the passes that a warp needs to read one row and to read one column, and the smallest padding that removes the column conflict.
    5. For each access, the largest power of two known to divide its address, given the base alignment your layout document promises, and whether a 16-byte vector access there would be legal.
    6. A remark for every choice, as the [sixth principle](../philosophy.md#6-explain-performance-decisions) asks, such as "mapped `column` to lanes: `b` and `c` coalesce, `a` is broadcast".

    **Not yet:** generating GPU code ([M12](../mlir/m12-vortex-gpu-path.md) weighs the paths), rewriting a kernel to stage tiles through shared memory ([G10](g10-matmul-ladder.md)), barriers ([G6](g6-synchronization.md)), occupancy ([G5](g5-occupancy.md)) and swizzled layouts.

    **Proof that it works:**

    - Golden tests for the stage 10 kernel at `[f32; 64, 64]`: with `column` on the lanes, 1 sector for `a`, 4 for `b` and 4 for the store to `c`; with `row`, 32, 1 and 32. The chooser picks `column`.
    - The transpose kernel: every mapping reports one strided access, and the remark says that no mapping coalesces both.
    - Structs: for `points[i].x` in a `[Point; 4096]` whose `Point` holds two `f32` fields, the stride is the size of `Point` from your layout document, and the sector count follows from it (8 sectors when that size is 8 bytes).
    - Tiles: reading a column of a 32 × 32 `f32` tile takes 32 passes, of a 32 × 33 tile 1, of a 32 × 34 tile 2, and the suggested padding for 32 × 32 is one element.
    - Alignment: with 16-byte array bases, `[i, 4q]` of a `[f32; 64, 64]` array is 16-byte aligned and `[i, 4q]` of a `[f32; 32, 33]` array is not.
    - A differential test: on a few hundred random affine accesses and shapes, the report's sector and pass counts match an independent brute-force counter like this chapter's examples.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does one warp-wide load cost in global memory?** The number of distinct aligned 32-byte sectors its 32 addresses touch, on current NVIDIA GPUs.
    - **Which index should vary across a warp's lanes?** The loop variable that is the last subscript of the accesses that run most often, so that neighbouring lanes touch neighbouring addresses.
    - **When do lanes conflict in shared memory?** When they want different words in the same bank; a stride of `s` words costs gcd(s, 32) passes.
    - **Why does a transpose need shared memory?** Its read and its write have their contiguous index on different loop variables, so no mapping coalesces both; a tile turns the data around on chip.
    - **What does padding cost?** An unused column per tile, and the 16-byte alignment of its rows that vector loads need.
    - **Why may a Vortex compiler apply all of this freely?** It moves values and reassigns work but never changes or reorders arithmetic, and `&mut` rules out overlap between the output and the inputs.

## Where this comes back

!!! next "You will use this again in"

    - [G5. Occupancy and latency hiding](g5-occupancy.md): *vector loads*, *register pressure*
    - [G6. Synchronization, atomics and reductions](g6-synchronization.md): *barrier*, *broadcast*
    - [G10. The GPU matmul ladder](g10-matmul-ladder.md): *coalescing*, *thread mapping*, *padding*, *swizzled layout*, *vector loads*
    - [G13. Tile languages](g13-tile-languages.md): *thread mapping*
    - [G14. Measuring GPU code](g14-measuring-gpu-code.md): *sectors per request*, *bank conflicts*, *effective bandwidth*
    - [M10. MLIR for GPUs](../mlir/m10-mlir-for-gpus.md): *workgroup memory attribution*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *target description*, *thread mapping*

## Sources and further reading

Read the Programming Guide's memory-performance section first, then Harris's three posts in order, then Boehm's worklog.

[^pg-threads]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.2, "Thread Hierarchy". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#thread-hierarchy>
[^pg-coalesce]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.4.1, "Coalesced Global Memory Access". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#coalesced-global-memory-access>
[^pg-banks]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.4.2, "Shared Memory Access Patterns". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#shared-memory-access-patterns>
[^pg-transpose]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.4.2.1, "Matrix Transpose Example Using Shared Memory". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#matrix-transpose-example-using-shared-memory>
[^pg-transpose-conflicts]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.4.2.2, "Shared Memory Bank Conflicts", figures 17 and 18. <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#shared-memory-bank-conflicts>
[^bp-coalesce]: NVIDIA, "CUDA C++ Best Practices Guide", 13.4, section 10.2.1, "Coalesced Access to Global Memory", with subsections 10.2.1.1 to 10.2.1.3. <https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#coalesced-access-to-global-memory>
[^bp-banks]: NVIDIA, "CUDA C++ Best Practices Guide", 13.4, section 10.2.3.1, "Shared Memory and Memory Banks". <https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#shared-memory-and-memory-banks>
[^bp-ab]: NVIDIA, "CUDA C++ Best Practices Guide", 13.4, section 10.2.3.2, "Shared Memory in Matrix Multiplication (C=AB)". <https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#shared-memory-in-matrix-multiplication-c-ab>
[^bp-aat]: NVIDIA, "CUDA C++ Best Practices Guide", 13.4, section 10.2.3.3, "Shared Memory in Matrix Multiplication (C=AAT)". <https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#shared-memory-in-matrix-multiplication-c-aat>
[^ncu-terms]: NVIDIA, "Nsight Compute: Profiling Guide", v2026.3.1, section 2.3.7, "Quantities": the entries for request, sector and wavefront. <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#quantities>
[^ncu-smem]: NVIDIA, "Nsight Compute: Profiling Guide", v2026.3.1, section 2.11.1, "Shared Memory": the Wavefronts and Bank Conflicts columns. <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#memory-tables-smem>
[^ncu-l1]: NVIDIA, "Nsight Compute: Profiling Guide", v2026.3.1, section 2.11.2, "L1/TEX Cache": the Sectors, Sectors/Req and Hit Rate columns. <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#memory-tables-l1>
[^ptx-align]: NVIDIA, "Parallel Thread Execution ISA", version 9.4, sections 5.4.5, "Alignment", and 6.4.1, "Addresses as Operands". <https://docs.nvidia.com/cuda/parallel-thread-execution/index.html#addresses-as-operands>
[^ptx-swizzle]: NVIDIA, "Parallel Thread Execution ISA", version 9.4, section 5.5.7, "Swizzling Modes". <https://docs.nvidia.com/cuda/parallel-thread-execution/index.html#tensor-swizzling-modes>
[^harris-global]: Mark Harris, "How to Access Global Memory Efficiently in CUDA C/C++ Kernels", NVIDIA Technical Blog, 7 January 2013, sections "Misaligned Data Accesses" and "Strided Memory Access". <https://developer.nvidia.com/blog/how-access-global-memory-efficiently-cuda-c-kernels/>
[^harris-shared]: Mark Harris, "Using Shared Memory in CUDA C/C++", NVIDIA Technical Blog, 28 January 2013, section "Shared memory bank conflicts". <https://developer.nvidia.com/blog/using-shared-memory-cuda-cc/>
[^harris-transpose]: Mark Harris, "An Efficient Matrix Transpose in CUDA C/C++", NVIDIA Technical Blog, 18 February 2013. <https://developer.nvidia.com/blog/efficient-matrix-transpose-cuda-cc/>
[^vec]: Justin Luitjens and Rajeshwari Devaramani, "CUDA Pro Tip: Increase Performance with Vectorized Memory Access", NVIDIA Technical Blog, 4 August 2025 (an update of a post first published on 4 December 2013). <https://developer.nvidia.com/blog/cuda-pro-tip-increase-performance-with-vectorized-memory-access/>
[^boehm]: Simon Boehm, "How to Optimize a CUDA Matmul Kernel for cuBLAS-like Performance: a Worklog", December 2022: the results table and the sections "Kernel 1", "Kernel 2" and "Kernel 6". <https://siboehm.com/articles/22/CUDA-MMM>
[^amd-coalesce]: AMD, "Hardware implementation", HIP 7.15.0 documentation, section "Memory coalescing". <https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html#memory-coalescing>
[^amd-lds]: AMD, "Hardware implementation", HIP 7.15.0 documentation, section "Local data share (LDS)". <https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html#local-data-share-lds>
[^msl]: Apple, "Metal Shading Language Specification", version 4.1, 2026. The full text defines threadgroup memory and contains neither "bank" nor "coalesce" (searched on 2026-09-24). <https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf>
[^mlir-gpu]: MLIR Project, "'gpu' Dialect", sections "GPU address spaces" and "Memory attribution". <https://mlir.llvm.org/docs/Dialects/GPU/#memory-attribution>
[^llvm-lsv]: LLVM Project, `LoadStoreVectorizer.cpp`, release/18.x branch: the file header, and the alignment test in `Vectorizer::splitChainByAlignment`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Vectorize/LoadStoreVectorizer.cpp>
[^llvm-gpu-pipelines]: LLVM Project, release/18.x branch: `NVPTXPassConfig::addIRPasses` in `NVPTXTargetMachine.cpp` and `AMDGPUPassConfig::addCodeGenPrepare` in `AMDGPUTargetMachine.cpp`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Target/NVPTX/NVPTXTargetMachine.cpp> and <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Target/AMDGPU/AMDGPUTargetMachine.cpp>
[^llvm-tti]: LLVM Project, `TargetTransformInfoImpl.h`, release/18.x branch: the default `allowsMisalignedMemoryAccesses`, which returns false. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/include/llvm/Analysis/TargetTransformInfoImpl.h>
