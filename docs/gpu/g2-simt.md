# G2. The SIMT execution model

<p class="page-intro">A GPU runs a loop by giving every iteration its own thread, then executes those threads in fixed-size groups that share one instruction at a time. This chapter shows how threads are numbered and grouped, counts what it costs when a group's threads want different instructions, and turns that count into a question a Vortex compiler can answer at compile time: which launch shapes leave lanes with nothing to do.</p>

<p class="vx-meta" markdown="1">Level: Foundations · Reading time: about 40 minutes · Builds on: [G1. Throughput machines](g1-throughput-machines.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does a CPU's loop vectorizer do to a loop's iterations?"

        It packs several iterations into one wide instruction: one vector `add` does the work of four or eight scalar ones, one iteration per lane of the vector register.

        Introduced in [P10. Vectorization](../optimize/p10-vectorization.md).

    ??? question "When can a loop's iterations run on separate threads with no synchronization between them?"

        When no iteration writes a location that another iteration reads or writes. Then the iterations can run in any order, or all at once, and the result is the same.

        Introduced in [P13. Multithreading](../optimize/p13-multithreading.md).

    ??? question "Why does a throughput-oriented machine keep many independent operations in flight?"

        Little's law: the throughput a resource delivers is the work in flight divided by the latency of each piece. While one operation waits on memory, others that are ready keep the hardware busy.

        Introduced in [G1. Throughput machines](g1-throughput-machines.md).

    ??? question "What is the immediate post-dominator of the block that ends in an `if`/`else` branch?"

        The first block that every path from the branch must reach: for a plain `if`/`else`, the block right after both arms, where the two paths meet again.

        Introduced in [O2. Control-flow graphs and dominance](../optimize/o2-cfg-and-dominance.md#post-dominance).

    ??? question "Does a Vortex compiler know the extent of an array such as `[f32; 70, 70]` while it compiles?"

        Yes. Every dimension must be an integer constant expression, and the compiler evaluates it when it resolves the array type, so each extent is a known number before any code is generated.

        Introduced in [Arrays and shapes, decision 11](../decisions/arrays.md#d11).

!!! goals "In this chapter"

    - Compute a thread's global index from its block and thread indices, and tell which threads of a one- or two-dimensional block share a warp.
    - Explain how SIMT differs from SIMD, and name the warp in NVIDIA's, AMD's and Apple's terms.
    - Trace a warp through a divergent `if`/`else` and a loop with per-lane trip counts, and count the instructions it issues and the lane slots that do work.
    - Recognize warp-uniform values and branches that a compiler can remove, and state what independent thread scheduling changed and what it did not.
    - Decide, for a loop nest over fixed-shape Vortex arrays and a given block shape, which warps are full, which straddle the array's edge, and which have no work.

## One thread, one loop iteration

Start with a loop a CPU would run one element after another:

```vortex
// items: valid
fn increment(x: &mut [f32; 4096]) {
    for i in 0..4096 {
        x[i] = x[i] + 1.0;
    }
}
```

A CPU runs the body 4,096 times, or, after [P10](../optimize/p10-vectorization.md)'s vectorizer, 4,096 / 8 times with eight iterations per instruction. A GPU takes a third route. The body becomes a **kernel**, a function that the GPU runs many times at once, and each run is a **thread** that handles one value of `i`. A GPU thread is not an operating-system thread: it is one run of the kernel, with its own registers and its own index, and nothing else. Thread 517 reads `x[517]`, adds `1.0` and writes `x[517]` back. It touches no other element, which is why the iterations can run in any order ([P13](../optimize/p13-multithreading.md)).

Launching the kernel means choosing two numbers: how many threads go in each **block**, and how many blocks to create. The collection of blocks is the **grid**.[^pg-threads] Every thread can read its block's index and its own index inside the block, and computes whatever flat index it needs from them. For a one-dimensional grid of blocks of 256 threads:

$$i = \text{block index} \times 256 + \text{thread index}$$

Thread 5 of block 2 handles `i = 2 × 256 + 5 = 517`. The kernel for `increment` needs 4,096 / 256 = 16 blocks.

Blocks and grids may have one, two or three dimensions, and CUDA names the indices `threadIdx.x`, `.y`, `.z`, `blockIdx`, and the sizes `blockDim` and `gridDim`.[^pg-threads] The extra dimensions are a convenience for indexing a matrix or a volume; the Programming Guide says they do not change performance.[^pg-threads] Inside a block, the threads are still put in one line, with `x` varying fastest: the **thread id** of `(x, y, z)` is

$$\text{id} = x + y \cdot D_x + z \cdot D_x D_y$$

where $D_x$ and $D_y$ are the block's width and height.[^pg-threads] That line is what the hardware cuts into groups, next.

A block is not free to be any size. Apple's GPUs allow up to 1,024 threads in a threadgroup (Metal's word for a block), a figure Apple gave at WWDC 2022.[^apple-scale] All threads of a block run on one **streaming multiprocessor** (SM, NVIDIA's term), **compute unit** (CU, AMD's) or **GPU core** (Apple's): the unit that holds their registers and the on-chip memory they share. [G3](g3-memory-hierarchy.md) opens that unit up.

## Warps, wavefronts and SIMD-groups

The SM does not schedule threads one at a time. It creates, schedules and executes them in groups of 32, and NVIDIA calls such a group a **warp**.[^pg-simt] A block's warps are always cut the same way: each warp holds 32 consecutive thread ids, and the first warp holds thread 0.[^pg-mt] AMD's HIP documentation calls the group a warp and notes that AMD's ISA manuals call it a **wavefront**; it holds 64 threads on CDNA (data-center) GPUs and 32 on RDNA (graphics) GPUs.[^hip-model]

Apple calls it a **SIMD-group**; the `threadExecutionWidth` of a Metal compute pipeline reports its width, which Apple says is 32 on all of its GPUs.[^apple-scale] This book says **warp** for all three, and **lane** for a thread's position inside its warp, 0 to 31. The word lane is borrowed from [P10](../optimize/p10-vectorization.md)'s vector registers, for a reason the next section makes precise.

The number of warps a block needs is its thread count divided by 32, rounded up.[^pg-mt] A block whose thread count is not a multiple of 32 still gets whole warps, and its last warp has lanes with no thread in them. Take a grid of 2 blocks of 40 threads, a size chosen badly on purpose. Each block needs `ceil(40 / 32) = 2` warps: one with 32 threads, and one with 8 threads and 24 empty lanes (Figure 1). Those 24 lanes cost the same issue slots as working ones and do nothing. The Programming Guide lists this case among the reasons a lane can be inactive.[^pg-its]

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="A grid of two blocks of 40 threads each, split into warps of 32 lanes. Each block has one full warp of 32 active lanes and one partial warp with only 8 active lanes; the remaining 24 lane slots of that warp are drawn empty, because the block has no thread left to put there.">
<title id="g2-f1-title">A grid of two blocks of 40 threads, split into warps of 32 lanes</title>
<desc id="g2-f1-desc">Two groups of two rows of small squares. Each row of 32 squares is one warp. In block 0, the first row (warp 0) has all 32 squares filled, meaning all 32 lanes are active; the second row (warp 1) has its first 8 squares filled and the remaining 24 drawn as empty outlines, meaning only 8 of its lanes are active and 24 are idle. Block 1 repeats the same pattern.</desc>
<text class="vx-text" x="20" y="26">One grid, two blocks of 40 threads each; a warp is 32 lanes</text>
<text class="vx-text" x="20" y="54">block 0 (40 threads)</text>
<rect class="vx-cell-on" x="148" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="166" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="184" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="202" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="220" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="238" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="256" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="274" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="292" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="310" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="328" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="346" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="364" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="382" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="400" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="418" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="436" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="454" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="472" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="490" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="508" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="526" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="544" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="562" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="580" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="598" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="616" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="634" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="652" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="670" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="688" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="706" y="76" width="16" height="24"/>
<text class="vx-text-muted" x="20" y="93">warp 0</text>
<rect class="vx-cell-on" x="148" y="112" width="16" height="24"/>
<rect class="vx-cell-on" x="166" y="112" width="16" height="24"/>
<rect class="vx-cell-on" x="184" y="112" width="16" height="24"/>
<rect class="vx-cell-on" x="202" y="112" width="16" height="24"/>
<rect class="vx-cell-on" x="220" y="112" width="16" height="24"/>
<rect class="vx-cell-on" x="238" y="112" width="16" height="24"/>
<rect class="vx-cell-on" x="256" y="112" width="16" height="24"/>
<rect class="vx-cell-on" x="274" y="112" width="16" height="24"/>
<rect class="vx-box" x="292" y="112" width="16" height="24"/>
<rect class="vx-box" x="310" y="112" width="16" height="24"/>
<rect class="vx-box" x="328" y="112" width="16" height="24"/>
<rect class="vx-box" x="346" y="112" width="16" height="24"/>
<rect class="vx-box" x="364" y="112" width="16" height="24"/>
<rect class="vx-box" x="382" y="112" width="16" height="24"/>
<rect class="vx-box" x="400" y="112" width="16" height="24"/>
<rect class="vx-box" x="418" y="112" width="16" height="24"/>
<rect class="vx-box" x="436" y="112" width="16" height="24"/>
<rect class="vx-box" x="454" y="112" width="16" height="24"/>
<rect class="vx-box" x="472" y="112" width="16" height="24"/>
<rect class="vx-box" x="490" y="112" width="16" height="24"/>
<rect class="vx-box" x="508" y="112" width="16" height="24"/>
<rect class="vx-box" x="526" y="112" width="16" height="24"/>
<rect class="vx-box" x="544" y="112" width="16" height="24"/>
<rect class="vx-box" x="562" y="112" width="16" height="24"/>
<rect class="vx-box" x="580" y="112" width="16" height="24"/>
<rect class="vx-box" x="598" y="112" width="16" height="24"/>
<rect class="vx-box" x="616" y="112" width="16" height="24"/>
<rect class="vx-box" x="634" y="112" width="16" height="24"/>
<rect class="vx-box" x="652" y="112" width="16" height="24"/>
<rect class="vx-box" x="670" y="112" width="16" height="24"/>
<rect class="vx-box" x="688" y="112" width="16" height="24"/>
<rect class="vx-box" x="706" y="112" width="16" height="24"/>
<text class="vx-text-muted" x="20" y="129">warp 1</text>
<text class="vx-text-muted" x="148" y="152">warp 0: 32 of 32 lanes active, warp 1: 8 of 32 active, 24 idle</text>
<text class="vx-text" x="20" y="182">block 1 (40 threads)</text>
<rect class="vx-cell-on" x="148" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="166" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="184" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="202" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="220" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="238" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="256" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="274" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="292" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="310" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="328" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="346" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="364" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="382" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="400" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="418" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="436" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="454" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="472" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="490" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="508" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="526" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="544" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="562" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="580" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="598" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="616" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="634" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="652" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="670" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="688" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="706" y="196" width="16" height="24"/>
<text class="vx-text-muted" x="20" y="213">warp 0</text>
<rect class="vx-cell-on" x="148" y="232" width="16" height="24"/>
<rect class="vx-cell-on" x="166" y="232" width="16" height="24"/>
<rect class="vx-cell-on" x="184" y="232" width="16" height="24"/>
<rect class="vx-cell-on" x="202" y="232" width="16" height="24"/>
<rect class="vx-cell-on" x="220" y="232" width="16" height="24"/>
<rect class="vx-cell-on" x="238" y="232" width="16" height="24"/>
<rect class="vx-cell-on" x="256" y="232" width="16" height="24"/>
<rect class="vx-cell-on" x="274" y="232" width="16" height="24"/>
<rect class="vx-box" x="292" y="232" width="16" height="24"/>
<rect class="vx-box" x="310" y="232" width="16" height="24"/>
<rect class="vx-box" x="328" y="232" width="16" height="24"/>
<rect class="vx-box" x="346" y="232" width="16" height="24"/>
<rect class="vx-box" x="364" y="232" width="16" height="24"/>
<rect class="vx-box" x="382" y="232" width="16" height="24"/>
<rect class="vx-box" x="400" y="232" width="16" height="24"/>
<rect class="vx-box" x="418" y="232" width="16" height="24"/>
<rect class="vx-box" x="436" y="232" width="16" height="24"/>
<rect class="vx-box" x="454" y="232" width="16" height="24"/>
<rect class="vx-box" x="472" y="232" width="16" height="24"/>
<rect class="vx-box" x="490" y="232" width="16" height="24"/>
<rect class="vx-box" x="508" y="232" width="16" height="24"/>
<rect class="vx-box" x="526" y="232" width="16" height="24"/>
<rect class="vx-box" x="544" y="232" width="16" height="24"/>
<rect class="vx-box" x="562" y="232" width="16" height="24"/>
<rect class="vx-box" x="580" y="232" width="16" height="24"/>
<rect class="vx-box" x="598" y="232" width="16" height="24"/>
<rect class="vx-box" x="616" y="232" width="16" height="24"/>
<rect class="vx-box" x="634" y="232" width="16" height="24"/>
<rect class="vx-box" x="652" y="232" width="16" height="24"/>
<rect class="vx-box" x="670" y="232" width="16" height="24"/>
<rect class="vx-box" x="688" y="232" width="16" height="24"/>
<rect class="vx-box" x="706" y="232" width="16" height="24"/>
<text class="vx-text-muted" x="20" y="249">warp 1</text>
<text class="vx-text-muted" x="148" y="272">warp 0: 32 of 32 lanes active, warp 1: 8 of 32 active, 24 idle</text>
<rect class="vx-cell-on" x="20" y="298" width="16" height="16"/>
<text class="vx-text-muted" x="44" y="311">active lane</text>
<rect class="vx-box" x="170" y="298" width="16" height="16"/>
<text class="vx-text-muted" x="194" y="311">lane slot present, no thread to fill it</text>
</svg>
<figcaption>Figure 1. A grid of two blocks of 40 threads, split into warps of 32 lanes. Each block's second warp has only 8 real threads; its other 24 lane slots still exist in hardware but do no work.</figcaption>
</figure>

In a two-dimensional block, the thread id formula decides which threads share a warp. A block 16 threads wide and 4 high has ids 0 to 63; ids 0 to 31 are rows `y = 0` and `y = 1`, so warp 0 covers two whole rows (Figure 2). A block 32 wide gives each row its own warp. This matters for memory: [G4](g4-memory-performance.md) shows that what a warp's load costs depends on which addresses its 32 lanes touch together, so the shape of a block decides which array elements travel together.

<figure class="vx-figure">
<svg viewBox="0 0 760 250" role="img" aria-label="A 16 by 4 thread block. Rows y 0 and 1 form warp 0, rows y 2 and 3 form warp 1, because thread ids count along x first." aria-describedby="g2-f2-desc">
<title id="g2-f2-title">Which threads of a 16 by 4 block share a warp</title>
<desc id="g2-f2-desc">A grid of 16 columns and 4 rows of cells, each holding its thread id, x plus 16 times y. The top two rows, ids 0 to 31, are shaded as warp 0. The bottom two rows, ids 32 to 63, are drawn plain as warp 1.</desc>
<text class="vx-text-muted" x="110" y="28">x = 0</text>
<text class="vx-text-muted" x="686" y="28" text-anchor="end">x = 15</text>
<text class="vx-text-muted" x="100" y="62" text-anchor="end">y = 0</text>
<rect class="vx-box-accent" x="110" y="40" width="34" height="32"/>
<text class="vx-mono" x="127" y="61" text-anchor="middle">0</text>
<rect class="vx-box-accent" x="146" y="40" width="34" height="32"/>
<text class="vx-mono" x="163" y="61" text-anchor="middle">1</text>
<rect class="vx-box-accent" x="182" y="40" width="34" height="32"/>
<text class="vx-mono" x="199" y="61" text-anchor="middle">2</text>
<rect class="vx-box-accent" x="218" y="40" width="34" height="32"/>
<text class="vx-mono" x="235" y="61" text-anchor="middle">3</text>
<rect class="vx-box-accent" x="254" y="40" width="34" height="32"/>
<text class="vx-mono" x="271" y="61" text-anchor="middle">4</text>
<rect class="vx-box-accent" x="290" y="40" width="34" height="32"/>
<text class="vx-mono" x="307" y="61" text-anchor="middle">5</text>
<rect class="vx-box-accent" x="326" y="40" width="34" height="32"/>
<text class="vx-mono" x="343" y="61" text-anchor="middle">6</text>
<rect class="vx-box-accent" x="362" y="40" width="34" height="32"/>
<text class="vx-mono" x="379" y="61" text-anchor="middle">7</text>
<rect class="vx-box-accent" x="398" y="40" width="34" height="32"/>
<text class="vx-mono" x="415" y="61" text-anchor="middle">8</text>
<rect class="vx-box-accent" x="434" y="40" width="34" height="32"/>
<text class="vx-mono" x="451" y="61" text-anchor="middle">9</text>
<rect class="vx-box-accent" x="470" y="40" width="34" height="32"/>
<text class="vx-mono" x="487" y="61" text-anchor="middle">10</text>
<rect class="vx-box-accent" x="506" y="40" width="34" height="32"/>
<text class="vx-mono" x="523" y="61" text-anchor="middle">11</text>
<rect class="vx-box-accent" x="542" y="40" width="34" height="32"/>
<text class="vx-mono" x="559" y="61" text-anchor="middle">12</text>
<rect class="vx-box-accent" x="578" y="40" width="34" height="32"/>
<text class="vx-mono" x="595" y="61" text-anchor="middle">13</text>
<rect class="vx-box-accent" x="614" y="40" width="34" height="32"/>
<text class="vx-mono" x="631" y="61" text-anchor="middle">14</text>
<rect class="vx-box-accent" x="650" y="40" width="34" height="32"/>
<text class="vx-mono" x="667" y="61" text-anchor="middle">15</text>
<text class="vx-text-muted" x="100" y="96" text-anchor="end">y = 1</text>
<rect class="vx-box-accent" x="110" y="74" width="34" height="32"/>
<text class="vx-mono" x="127" y="95" text-anchor="middle">16</text>
<rect class="vx-box-accent" x="146" y="74" width="34" height="32"/>
<text class="vx-mono" x="163" y="95" text-anchor="middle">17</text>
<rect class="vx-box-accent" x="182" y="74" width="34" height="32"/>
<text class="vx-mono" x="199" y="95" text-anchor="middle">18</text>
<rect class="vx-box-accent" x="218" y="74" width="34" height="32"/>
<text class="vx-mono" x="235" y="95" text-anchor="middle">19</text>
<rect class="vx-box-accent" x="254" y="74" width="34" height="32"/>
<text class="vx-mono" x="271" y="95" text-anchor="middle">20</text>
<rect class="vx-box-accent" x="290" y="74" width="34" height="32"/>
<text class="vx-mono" x="307" y="95" text-anchor="middle">21</text>
<rect class="vx-box-accent" x="326" y="74" width="34" height="32"/>
<text class="vx-mono" x="343" y="95" text-anchor="middle">22</text>
<rect class="vx-box-accent" x="362" y="74" width="34" height="32"/>
<text class="vx-mono" x="379" y="95" text-anchor="middle">23</text>
<rect class="vx-box-accent" x="398" y="74" width="34" height="32"/>
<text class="vx-mono" x="415" y="95" text-anchor="middle">24</text>
<rect class="vx-box-accent" x="434" y="74" width="34" height="32"/>
<text class="vx-mono" x="451" y="95" text-anchor="middle">25</text>
<rect class="vx-box-accent" x="470" y="74" width="34" height="32"/>
<text class="vx-mono" x="487" y="95" text-anchor="middle">26</text>
<rect class="vx-box-accent" x="506" y="74" width="34" height="32"/>
<text class="vx-mono" x="523" y="95" text-anchor="middle">27</text>
<rect class="vx-box-accent" x="542" y="74" width="34" height="32"/>
<text class="vx-mono" x="559" y="95" text-anchor="middle">28</text>
<rect class="vx-box-accent" x="578" y="74" width="34" height="32"/>
<text class="vx-mono" x="595" y="95" text-anchor="middle">29</text>
<rect class="vx-box-accent" x="614" y="74" width="34" height="32"/>
<text class="vx-mono" x="631" y="95" text-anchor="middle">30</text>
<rect class="vx-box-accent" x="650" y="74" width="34" height="32"/>
<text class="vx-mono" x="667" y="95" text-anchor="middle">31</text>
<text class="vx-text-muted" x="100" y="142" text-anchor="end">y = 2</text>
<rect class="vx-box" x="110" y="120" width="34" height="32"/>
<text class="vx-mono" x="127" y="141" text-anchor="middle">32</text>
<rect class="vx-box" x="146" y="120" width="34" height="32"/>
<text class="vx-mono" x="163" y="141" text-anchor="middle">33</text>
<rect class="vx-box" x="182" y="120" width="34" height="32"/>
<text class="vx-mono" x="199" y="141" text-anchor="middle">34</text>
<rect class="vx-box" x="218" y="120" width="34" height="32"/>
<text class="vx-mono" x="235" y="141" text-anchor="middle">35</text>
<rect class="vx-box" x="254" y="120" width="34" height="32"/>
<text class="vx-mono" x="271" y="141" text-anchor="middle">36</text>
<rect class="vx-box" x="290" y="120" width="34" height="32"/>
<text class="vx-mono" x="307" y="141" text-anchor="middle">37</text>
<rect class="vx-box" x="326" y="120" width="34" height="32"/>
<text class="vx-mono" x="343" y="141" text-anchor="middle">38</text>
<rect class="vx-box" x="362" y="120" width="34" height="32"/>
<text class="vx-mono" x="379" y="141" text-anchor="middle">39</text>
<rect class="vx-box" x="398" y="120" width="34" height="32"/>
<text class="vx-mono" x="415" y="141" text-anchor="middle">40</text>
<rect class="vx-box" x="434" y="120" width="34" height="32"/>
<text class="vx-mono" x="451" y="141" text-anchor="middle">41</text>
<rect class="vx-box" x="470" y="120" width="34" height="32"/>
<text class="vx-mono" x="487" y="141" text-anchor="middle">42</text>
<rect class="vx-box" x="506" y="120" width="34" height="32"/>
<text class="vx-mono" x="523" y="141" text-anchor="middle">43</text>
<rect class="vx-box" x="542" y="120" width="34" height="32"/>
<text class="vx-mono" x="559" y="141" text-anchor="middle">44</text>
<rect class="vx-box" x="578" y="120" width="34" height="32"/>
<text class="vx-mono" x="595" y="141" text-anchor="middle">45</text>
<rect class="vx-box" x="614" y="120" width="34" height="32"/>
<text class="vx-mono" x="631" y="141" text-anchor="middle">46</text>
<rect class="vx-box" x="650" y="120" width="34" height="32"/>
<text class="vx-mono" x="667" y="141" text-anchor="middle">47</text>
<text class="vx-text-muted" x="100" y="176" text-anchor="end">y = 3</text>
<rect class="vx-box" x="110" y="154" width="34" height="32"/>
<text class="vx-mono" x="127" y="175" text-anchor="middle">48</text>
<rect class="vx-box" x="146" y="154" width="34" height="32"/>
<text class="vx-mono" x="163" y="175" text-anchor="middle">49</text>
<rect class="vx-box" x="182" y="154" width="34" height="32"/>
<text class="vx-mono" x="199" y="175" text-anchor="middle">50</text>
<rect class="vx-box" x="218" y="154" width="34" height="32"/>
<text class="vx-mono" x="235" y="175" text-anchor="middle">51</text>
<rect class="vx-box" x="254" y="154" width="34" height="32"/>
<text class="vx-mono" x="271" y="175" text-anchor="middle">52</text>
<rect class="vx-box" x="290" y="154" width="34" height="32"/>
<text class="vx-mono" x="307" y="175" text-anchor="middle">53</text>
<rect class="vx-box" x="326" y="154" width="34" height="32"/>
<text class="vx-mono" x="343" y="175" text-anchor="middle">54</text>
<rect class="vx-box" x="362" y="154" width="34" height="32"/>
<text class="vx-mono" x="379" y="175" text-anchor="middle">55</text>
<rect class="vx-box" x="398" y="154" width="34" height="32"/>
<text class="vx-mono" x="415" y="175" text-anchor="middle">56</text>
<rect class="vx-box" x="434" y="154" width="34" height="32"/>
<text class="vx-mono" x="451" y="175" text-anchor="middle">57</text>
<rect class="vx-box" x="470" y="154" width="34" height="32"/>
<text class="vx-mono" x="487" y="175" text-anchor="middle">58</text>
<rect class="vx-box" x="506" y="154" width="34" height="32"/>
<text class="vx-mono" x="523" y="175" text-anchor="middle">59</text>
<rect class="vx-box" x="542" y="154" width="34" height="32"/>
<text class="vx-mono" x="559" y="175" text-anchor="middle">60</text>
<rect class="vx-box" x="578" y="154" width="34" height="32"/>
<text class="vx-mono" x="595" y="175" text-anchor="middle">61</text>
<rect class="vx-box" x="614" y="154" width="34" height="32"/>
<text class="vx-mono" x="631" y="175" text-anchor="middle">62</text>
<rect class="vx-box" x="650" y="154" width="34" height="32"/>
<text class="vx-mono" x="667" y="175" text-anchor="middle">63</text>
<line class="vx-line" x1="692" y1="40" x2="692" y2="106"/>
<line class="vx-line" x1="692" y1="120" x2="692" y2="186"/>
<text class="vx-text-accent" x="700" y="78">warp 0</text>
<text class="vx-text" x="700" y="158">warp 1</text>
<text class="vx-text-muted" x="110" y="218">thread id = x + 16 × y; each run of 32 consecutive ids is one warp</text>
</svg>
<figcaption>Figure 2. A 16 × 4 block. Thread ids count along <code>x</code> first, so each warp takes two whole rows of the block. A block 32 threads wide would give each row its own warp.</figcaption>
</figure>

The first example computes this bookkeeping: the global ids and thread counts of every warp in the 40-thread grid, and the `(x, y)` range each warp covers in a 16 × 4 and a 32 × 2 block.

--8<-- "includes/examples/gpu/g2-simt/thread_hierarchy.cpp.md"

??? check "A block is 8 × 8 threads. Which `(x, y)` positions make up its warp 1, and how many warps does the block have?"

    Two warps. The thread ids run from 0 to 63, with id `x + 8y`. Warp 1 holds ids 32 to 63, which are the rows `y = 4` to `y = 7`, all eight columns of each: four rows of the block share one warp.

## SIMT: one instruction, many threads

NVIDIA calls this organization **SIMT**, single instruction, multiple threads. A warp executes one common instruction at a time. Each thread still has its own instruction address and registers, so each is free to branch on its own.[^pg-simt] In the source, the programmer writes one scalar thread, with ordinary `if`s, loops and calls, as if it ran alone; from the functional point of view each thread can follow its own path.[^pg-simt-basics] The hardware runs 32 of those threads together whenever they are at the same instruction, which, in most kernels, is most of the time.

The difference from SIMD is where the width lives. [P10](../optimize/p10-vectorization.md)'s vector instruction exposes its width to software: the compiler packs eight iterations into one register, and it must handle leftover iterations, masks and non-contiguous data itself. A SIMT instruction describes what one thread does, and the hardware supplies the 32 copies.[^pg-simt] NVIDIA compares the warp width to the cache line size: a program is correct without knowing it, and fast only if it takes it into account.[^pg-simt] The rest of this chapter is about that second part.

Two further facts about the SM explain why warps exist at all. It issues each thread's instructions in order, without branch prediction or speculation,[^pg-hw] so a thread waiting on memory stalls, and with it anything that depends on it. Instead, each warp's registers and program state stay on the chip for the warp's whole life, so switching from one warp to another costs nothing: at every issue cycle, a scheduler picks a warp whose next instruction is ready.[^pg-mt] That is how the latency hiding of [G1](g1-throughput-machines.md) happens in practice, and [G5](g5-occupancy.md) counts how many warps it takes.

## Divergence: when a warp's lanes disagree

Here is a kernel body with a branch, run by an eight-lane warp so that it fits on the page (a real warp has 32 lanes):

```text
v = x[i]
if v < 0 { v = -v } else { v = v * 2 }
y[i] = v
```

With `x = [3, -1, 4, -1, -5, 9, 2, -6]`, lanes 1, 3, 4 and 7 want the `if` arm and the other four want the `else` arm. The warp has one instruction stream, so it cannot run both arms in the same cycle. It runs the `if` arm with only lanes 1, 3, 4 and 7 switched on, then the `else` arm with only the other four, then continues with all eight.[^pg-simt] The lanes of a warp that take part in the current instruction are its **active** lanes; the set is its **active mask**. When lanes of one warp want different paths, the warp **diverges**. Step through it in Figure 3.

<figure class="vx-figure">
<div class="vx-stepper">
<div class="vx-step">
<p><strong>Step 1. Before the branch.</strong> All eight lanes are active. Each loads its own <code>x[i]</code> and compares it with zero. The comparison gives the warp a mask: lanes 1, 3, 4 and 7 want the <code>if</code> arm, the rest want the <code>else</code> arm.</p>
<svg viewBox="0 0 760 210" role="img" aria-label="Step 1: active lanes 0, 1, 2, 3, 4, 5, 6, 7; the highlighted line is the one the warp issues.">
<text class="vx-text-accent" x="24" y="34">v = x[i]</text>
<text class="vx-text-accent" x="24" y="58">if v &lt; 0 {</text>
<text class="vx-mono" x="48" y="82">v = -v</text>
<text class="vx-mono" x="24" y="106">} else {</text>
<text class="vx-mono" x="48" y="130">v = v * 2</text>
<text class="vx-mono" x="24" y="154">}</text>
<text class="vx-mono" x="24" y="178">y[i] = v</text>
<rect class="vx-cell-on" x="300" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="326" y="32" text-anchor="middle">lane 0</text>
<text class="vx-mono" x="326" y="110" text-anchor="middle">3</text>
<rect class="vx-cell-on" x="356" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="382" y="32" text-anchor="middle">lane 1</text>
<text class="vx-mono" x="382" y="110" text-anchor="middle">-1</text>
<rect class="vx-cell-on" x="412" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="438" y="32" text-anchor="middle">lane 2</text>
<text class="vx-mono" x="438" y="110" text-anchor="middle">4</text>
<rect class="vx-cell-on" x="468" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="494" y="32" text-anchor="middle">lane 3</text>
<text class="vx-mono" x="494" y="110" text-anchor="middle">-1</text>
<rect class="vx-cell-on" x="524" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="550" y="32" text-anchor="middle">lane 4</text>
<text class="vx-mono" x="550" y="110" text-anchor="middle">-5</text>
<rect class="vx-cell-on" x="580" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="606" y="32" text-anchor="middle">lane 5</text>
<text class="vx-mono" x="606" y="110" text-anchor="middle">9</text>
<rect class="vx-cell-on" x="636" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="662" y="32" text-anchor="middle">lane 6</text>
<text class="vx-mono" x="662" y="110" text-anchor="middle">2</text>
<rect class="vx-cell-on" x="692" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="718" y="32" text-anchor="middle">lane 7</text>
<text class="vx-mono" x="718" y="110" text-anchor="middle">-6</text>
<text class="vx-text-muted" x="290" y="110" text-anchor="end">v</text>
<text class="vx-text" x="300" y="152">active mask, lane 0 first: <tspan class="vx-mono">11111111</tspan></text>
<text class="vx-text-muted" x="300" y="176">8 of 8 lanes active</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 2. The <code>if</code> arm.</strong> The warp issues <code>v = -v</code> once, with only lanes 1, 3, 4 and 7 switched on. Lanes 0, 2, 5 and 6 hold their values and do nothing.</p>
<svg viewBox="0 0 760 210" role="img" aria-label="Step 2: active lanes 1, 3, 4, 7; the highlighted line is the one the warp issues.">
<text class="vx-mono" x="24" y="34">v = x[i]</text>
<text class="vx-mono" x="24" y="58">if v &lt; 0 {</text>
<text class="vx-text-accent" x="48" y="82">v = -v</text>
<text class="vx-mono" x="24" y="106">} else {</text>
<text class="vx-mono" x="48" y="130">v = v * 2</text>
<text class="vx-mono" x="24" y="154">}</text>
<text class="vx-mono" x="24" y="178">y[i] = v</text>
<rect class="vx-box" x="300" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="326" y="32" text-anchor="middle">lane 0</text>
<text class="vx-mono" x="326" y="110" text-anchor="middle">3</text>
<rect class="vx-cell-on" x="356" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="382" y="32" text-anchor="middle">lane 1</text>
<text class="vx-mono" x="382" y="110" text-anchor="middle">1</text>
<rect class="vx-box" x="412" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="438" y="32" text-anchor="middle">lane 2</text>
<text class="vx-mono" x="438" y="110" text-anchor="middle">4</text>
<rect class="vx-cell-on" x="468" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="494" y="32" text-anchor="middle">lane 3</text>
<text class="vx-mono" x="494" y="110" text-anchor="middle">1</text>
<rect class="vx-cell-on" x="524" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="550" y="32" text-anchor="middle">lane 4</text>
<text class="vx-mono" x="550" y="110" text-anchor="middle">5</text>
<rect class="vx-box" x="580" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="606" y="32" text-anchor="middle">lane 5</text>
<text class="vx-mono" x="606" y="110" text-anchor="middle">9</text>
<rect class="vx-box" x="636" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="662" y="32" text-anchor="middle">lane 6</text>
<text class="vx-mono" x="662" y="110" text-anchor="middle">2</text>
<rect class="vx-cell-on" x="692" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="718" y="32" text-anchor="middle">lane 7</text>
<text class="vx-mono" x="718" y="110" text-anchor="middle">6</text>
<text class="vx-text-muted" x="290" y="110" text-anchor="end">v</text>
<text class="vx-text" x="300" y="152">active mask, lane 0 first: <tspan class="vx-mono">01011001</tspan></text>
<text class="vx-text-muted" x="300" y="176">4 of 8 lanes active</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 3. The <code>else</code> arm.</strong> The warp issues <code>v = v * 2</code> with the mask inverted. Two arms ran one after the other, and each lane was busy for only one of them.</p>
<svg viewBox="0 0 760 210" role="img" aria-label="Step 3: active lanes 0, 2, 5, 6; the highlighted line is the one the warp issues.">
<text class="vx-mono" x="24" y="34">v = x[i]</text>
<text class="vx-mono" x="24" y="58">if v &lt; 0 {</text>
<text class="vx-mono" x="48" y="82">v = -v</text>
<text class="vx-mono" x="24" y="106">} else {</text>
<text class="vx-text-accent" x="48" y="130">v = v * 2</text>
<text class="vx-mono" x="24" y="154">}</text>
<text class="vx-mono" x="24" y="178">y[i] = v</text>
<rect class="vx-cell-on" x="300" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="326" y="32" text-anchor="middle">lane 0</text>
<text class="vx-mono" x="326" y="110" text-anchor="middle">6</text>
<rect class="vx-box" x="356" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="382" y="32" text-anchor="middle">lane 1</text>
<text class="vx-mono" x="382" y="110" text-anchor="middle">1</text>
<rect class="vx-cell-on" x="412" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="438" y="32" text-anchor="middle">lane 2</text>
<text class="vx-mono" x="438" y="110" text-anchor="middle">8</text>
<rect class="vx-box" x="468" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="494" y="32" text-anchor="middle">lane 3</text>
<text class="vx-mono" x="494" y="110" text-anchor="middle">1</text>
<rect class="vx-box" x="524" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="550" y="32" text-anchor="middle">lane 4</text>
<text class="vx-mono" x="550" y="110" text-anchor="middle">5</text>
<rect class="vx-cell-on" x="580" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="606" y="32" text-anchor="middle">lane 5</text>
<text class="vx-mono" x="606" y="110" text-anchor="middle">18</text>
<rect class="vx-cell-on" x="636" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="662" y="32" text-anchor="middle">lane 6</text>
<text class="vx-mono" x="662" y="110" text-anchor="middle">4</text>
<rect class="vx-box" x="692" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="718" y="32" text-anchor="middle">lane 7</text>
<text class="vx-mono" x="718" y="110" text-anchor="middle">6</text>
<text class="vx-text-muted" x="290" y="110" text-anchor="end">v</text>
<text class="vx-text" x="300" y="152">active mask, lane 0 first: <tspan class="vx-mono">10100110</tspan></text>
<text class="vx-text-muted" x="300" y="176">4 of 8 lanes active</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 4. Reconverged.</strong> Both arms are done, so every lane is active again at the first statement after the <code>if</code>, the branch's immediate post-dominator. The store runs once for all eight lanes.</p>
<svg viewBox="0 0 760 210" role="img" aria-label="Step 4: active lanes 0, 1, 2, 3, 4, 5, 6, 7; the highlighted line is the one the warp issues.">
<text class="vx-mono" x="24" y="34">v = x[i]</text>
<text class="vx-mono" x="24" y="58">if v &lt; 0 {</text>
<text class="vx-mono" x="48" y="82">v = -v</text>
<text class="vx-mono" x="24" y="106">} else {</text>
<text class="vx-mono" x="48" y="130">v = v * 2</text>
<text class="vx-mono" x="24" y="154">}</text>
<text class="vx-text-accent" x="24" y="178">y[i] = v</text>
<rect class="vx-cell-on" x="300" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="326" y="32" text-anchor="middle">lane 0</text>
<text class="vx-mono" x="326" y="110" text-anchor="middle">6</text>
<rect class="vx-cell-on" x="356" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="382" y="32" text-anchor="middle">lane 1</text>
<text class="vx-mono" x="382" y="110" text-anchor="middle">1</text>
<rect class="vx-cell-on" x="412" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="438" y="32" text-anchor="middle">lane 2</text>
<text class="vx-mono" x="438" y="110" text-anchor="middle">8</text>
<rect class="vx-cell-on" x="468" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="494" y="32" text-anchor="middle">lane 3</text>
<text class="vx-mono" x="494" y="110" text-anchor="middle">1</text>
<rect class="vx-cell-on" x="524" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="550" y="32" text-anchor="middle">lane 4</text>
<text class="vx-mono" x="550" y="110" text-anchor="middle">5</text>
<rect class="vx-cell-on" x="580" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="606" y="32" text-anchor="middle">lane 5</text>
<text class="vx-mono" x="606" y="110" text-anchor="middle">18</text>
<rect class="vx-cell-on" x="636" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="662" y="32" text-anchor="middle">lane 6</text>
<text class="vx-mono" x="662" y="110" text-anchor="middle">4</text>
<rect class="vx-cell-on" x="692" y="40" width="52" height="44"/>
<text class="vx-text-muted" x="718" y="32" text-anchor="middle">lane 7</text>
<text class="vx-mono" x="718" y="110" text-anchor="middle">6</text>
<text class="vx-text-muted" x="290" y="110" text-anchor="end">v</text>
<text class="vx-text" x="300" y="152">active mask, lane 0 first: <tspan class="vx-mono">11111111</tspan></text>
<text class="vx-text-muted" x="300" y="176">8 of 8 lanes active</text>
</svg>
</div>
</div>
<figcaption>Figure 3. An eight-lane warp (real warps have 32 lanes) running an <code>if</code>/<code>else</code> on <code>x = [3, -1, 4, -1, -5, 9, 2, -6]</code>. Filled cells are active lanes, empty cells are masked off. The warp issues both arms, one after the other, then continues with every lane active.</figcaption>
</figure>

Figure 3 shows the two costs of divergence. The warp issues both arms, so the branch takes as long as the two arms together. And during each arm, some lanes do nothing, so fewer lane slots do work. The first is time; the second is wasted width. Divergence happens only inside a warp: two warps that take different paths do not slow each other down, because each has its own instruction stream.[^pg-simt] AMD's documentation describes the same mechanism in its own hardware: the whole warp still passes through the ALUs, and the results of the lanes not on the path are masked out.[^hip-model]

Where do the lanes rejoin? In the model this chapter uses, at the branch's immediate post-dominator ([O2](../optimize/o2-cfg-and-dominance.md#post-dominance)), the first point that every path from the branch must reach. For a structured `if`/`else` that is the statement after it. LLVM's documentation describes the same picture: threads diverge at a **divergent branch** and may later **reconverge** at a common program point.[^llvm-uniformity] Where exactly real hardware reconverges is a detail that [independent thread scheduling](#independent-thread-scheduling), below, loosened.

A model makes the cost countable. Give each arm a length in instructions. A warp issues an arm if at least one active lane needs it, and skips it if none does. Count the **issued** instructions (time) and the **useful** lane slots, active lanes times instructions (work). A warp's slots are 32 times the instructions it issued; the fraction of slots that did useful work is its **SIMT efficiency** in this model. The second example counts both for several branches with 10-instruction arms.

--8<-- "includes/examples/gpu/g2-simt/divergence_cost.cpp.md"

Read the rows in order. When every lane takes the same arm, the warp issues 10 instructions and all 320 slots do work. Any two-way split, by parity or by halves, issues 20 and fills 320 of 640 slots: for time, the count of arms taken matters, not which lanes take them. A four-way switch issues 40, and a switch on the lane number, one arm per lane, issues 320 instructions to do the work of 10. The guard row is different: an `if` with no `else` issues its body once, whatever the mask, so it costs no extra time. It costs width instead: 6 lanes work and 26 wait.

The model leaves out real effects, such as the branch instruction itself and the different lengths real arms have, and it says nothing about memory. It gives the right shape: time grows with the number of distinct paths a warp takes, and work stays the same.

??? check "A warp's 32 lanes each run a `switch` on `lane % 4` with four 10-instruction arms. How many instructions does the warp issue? What if the `switch` is on `block % 4` instead, where `block` is the block index?"

    On `lane % 4`: 40. Each of the four values occurs in 8 lanes, so the warp runs all four arms, 8 lanes at a time. On `block % 4`: 10. Every lane of a warp belongs to the same block, so all 32 lanes pick the same arm, and the warp runs only that one.

## Loops that run for different lengths

A loop is a branch that repeats: at the end of every iteration, each lane decides whether to go around again. If the trip count depends on the lane's own data, lanes finish at different times. The warp keeps issuing the body while any lane still needs it; a lane that has finished waits, masked off, until the last one is done (Figure 4). The loop takes as long as its longest lane.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="Eight lanes run a loop with trip counts 3, 1, 4, 1, 5, 2, 6 and 2. The warp runs 6 iterations, as many as its longest lane; 24 of its 48 lane slots do work." aria-describedby="g2-f4-desc">
<title id="g2-f4-title">A loop whose trip count differs per lane</title>
<desc id="g2-f4-desc">A grid with one row per lane and one column per loop iteration. Lane l has a filled cell for each iteration it still runs and empty cells after it has finished. Every row is six cells long, because the warp keeps issuing the loop body until lane 6, with six trips, is done.</desc>
<text class="vx-text-muted" x="178" y="38" text-anchor="middle">iter 0</text>
<text class="vx-text-muted" x="238" y="38" text-anchor="middle">iter 1</text>
<text class="vx-text-muted" x="298" y="38" text-anchor="middle">iter 2</text>
<text class="vx-text-muted" x="358" y="38" text-anchor="middle">iter 3</text>
<text class="vx-text-muted" x="418" y="38" text-anchor="middle">iter 4</text>
<text class="vx-text-muted" x="478" y="38" text-anchor="middle">iter 5</text>
<text class="vx-text-muted" x="136" y="69" text-anchor="end">lane 0</text>
<rect class="vx-cell-on" x="150" y="50" width="56" height="26"/>
<rect class="vx-cell-on" x="210" y="50" width="56" height="26"/>
<rect class="vx-cell-on" x="270" y="50" width="56" height="26"/>
<rect class="vx-box" x="330" y="50" width="56" height="26"/>
<rect class="vx-box" x="390" y="50" width="56" height="26"/>
<rect class="vx-box" x="450" y="50" width="56" height="26"/>
<text class="vx-mono" x="524" y="69">3 trips</text>
<text class="vx-text-muted" x="136" y="99" text-anchor="end">lane 1</text>
<rect class="vx-cell-on" x="150" y="80" width="56" height="26"/>
<rect class="vx-box" x="210" y="80" width="56" height="26"/>
<rect class="vx-box" x="270" y="80" width="56" height="26"/>
<rect class="vx-box" x="330" y="80" width="56" height="26"/>
<rect class="vx-box" x="390" y="80" width="56" height="26"/>
<rect class="vx-box" x="450" y="80" width="56" height="26"/>
<text class="vx-mono" x="524" y="99">1 trips</text>
<text class="vx-text-muted" x="136" y="129" text-anchor="end">lane 2</text>
<rect class="vx-cell-on" x="150" y="110" width="56" height="26"/>
<rect class="vx-cell-on" x="210" y="110" width="56" height="26"/>
<rect class="vx-cell-on" x="270" y="110" width="56" height="26"/>
<rect class="vx-cell-on" x="330" y="110" width="56" height="26"/>
<rect class="vx-box" x="390" y="110" width="56" height="26"/>
<rect class="vx-box" x="450" y="110" width="56" height="26"/>
<text class="vx-mono" x="524" y="129">4 trips</text>
<text class="vx-text-muted" x="136" y="159" text-anchor="end">lane 3</text>
<rect class="vx-cell-on" x="150" y="140" width="56" height="26"/>
<rect class="vx-box" x="210" y="140" width="56" height="26"/>
<rect class="vx-box" x="270" y="140" width="56" height="26"/>
<rect class="vx-box" x="330" y="140" width="56" height="26"/>
<rect class="vx-box" x="390" y="140" width="56" height="26"/>
<rect class="vx-box" x="450" y="140" width="56" height="26"/>
<text class="vx-mono" x="524" y="159">1 trips</text>
<text class="vx-text-muted" x="136" y="189" text-anchor="end">lane 4</text>
<rect class="vx-cell-on" x="150" y="170" width="56" height="26"/>
<rect class="vx-cell-on" x="210" y="170" width="56" height="26"/>
<rect class="vx-cell-on" x="270" y="170" width="56" height="26"/>
<rect class="vx-cell-on" x="330" y="170" width="56" height="26"/>
<rect class="vx-cell-on" x="390" y="170" width="56" height="26"/>
<rect class="vx-box" x="450" y="170" width="56" height="26"/>
<text class="vx-mono" x="524" y="189">5 trips</text>
<text class="vx-text-muted" x="136" y="219" text-anchor="end">lane 5</text>
<rect class="vx-cell-on" x="150" y="200" width="56" height="26"/>
<rect class="vx-cell-on" x="210" y="200" width="56" height="26"/>
<rect class="vx-box" x="270" y="200" width="56" height="26"/>
<rect class="vx-box" x="330" y="200" width="56" height="26"/>
<rect class="vx-box" x="390" y="200" width="56" height="26"/>
<rect class="vx-box" x="450" y="200" width="56" height="26"/>
<text class="vx-mono" x="524" y="219">2 trips</text>
<text class="vx-text-muted" x="136" y="249" text-anchor="end">lane 6</text>
<rect class="vx-cell-on" x="150" y="230" width="56" height="26"/>
<rect class="vx-cell-on" x="210" y="230" width="56" height="26"/>
<rect class="vx-cell-on" x="270" y="230" width="56" height="26"/>
<rect class="vx-cell-on" x="330" y="230" width="56" height="26"/>
<rect class="vx-cell-on" x="390" y="230" width="56" height="26"/>
<rect class="vx-cell-on" x="450" y="230" width="56" height="26"/>
<text class="vx-mono" x="524" y="249">6 trips</text>
<text class="vx-text-muted" x="136" y="279" text-anchor="end">lane 7</text>
<rect class="vx-cell-on" x="150" y="260" width="56" height="26"/>
<rect class="vx-cell-on" x="210" y="260" width="56" height="26"/>
<rect class="vx-box" x="270" y="260" width="56" height="26"/>
<rect class="vx-box" x="330" y="260" width="56" height="26"/>
<rect class="vx-box" x="390" y="260" width="56" height="26"/>
<rect class="vx-box" x="450" y="260" width="56" height="26"/>
<text class="vx-mono" x="524" y="279">2 trips</text>
<text class="vx-text" x="150" y="316">issued: 6 iterations (the longest lane); useful: 24 of 48 lane slots</text>
</svg>
<figcaption>Figure 4. A loop whose trip count comes from each lane's own data. The warp repeats the body while any lane still needs it, so it takes as long as its slowest lane, and a finished lane sits masked off until the loop ends for everyone.</figcaption>
</figure>

In Figure 4, the warp issues 6 iterations, and 24 of its 48 lane slots do work. The last two rows of the second example show the same effect at full width. When all 32 lanes make 16 trips of a 4-instruction body, the warp issues 64 instructions and every slot works. When lane `l` makes `l` trips, the warp issues 31 × 4 = 124 instructions, and 1,984 of its 3,968 slots do work, one half.

Kernels with data-dependent loops, such as a search that stops at the first match or a sparse row whose length varies, meet this all the time. Once the lanes leave the loop, they are reconverged, but a value computed inside the loop can differ between them because each lane left on a different iteration. LLVM calls this **temporal divergence**, and its uniformity analysis tracks it.[^llvm-uniformity]

??? check "Each lane of a 32-lane warp runs a loop `for k in 0..(lane % 4)` whose body is 4 instructions. How many instructions does the warp issue, and what fraction of its slots does useful work?"

    The trip counts are 0, 1, 2, 3, repeating, so the longest lane makes 3 trips and the warp issues 3 × 4 = 12 instructions, 384 slots. Each group of four lanes makes 0 + 1 + 2 + 3 = 6 trips, so the 32 lanes make 48 trips, 192 lane-instructions: half the slots.

## Uniform values, and branches that disappear

A value that is the same in every active lane of a warp is **uniform**; otherwise it is **divergent**. A branch on a uniform condition is a **uniform branch**, and the whole warp follows one side of it.[^llvm-uniformity] The block index, the block size and the kernel's arguments are uniform. The thread index is divergent, and so is anything computed from it or loaded from an address computed from it. A uniform branch costs one path, like the `block % 4` switch above.

Knowing that a value is uniform is useful beyond branches. LLVM's documentation notes that uniform values can be computed or stored on shared resources, and that the compiler must **linearize** a divergent branch, arranging both sides to run for the right lanes of the group, but not a uniform one.[^llvm-uniformity] AMD GPUs are an example of the first point. Next to the vector units, each CU has a **scalar unit** that executes instructions uniformly for all threads of a warp, with its own scalar registers. It handles control flow, address calculations, kernel arguments and warp-uniform values.[^hip-hw]

A compiler that proves a value uniform can keep it there, once, instead of in a vector register with a copy in every lane. How LLVM proves it is [G9](g9-gpu-compilers-in-llvm.md#uniformity-one-value-per-warp-or-one-per-lane)'s subject.

Some divergent branches need not be branches. If both arms only compute a value, the compiler can compute the condition and select the result, with no change of path at all. AMD's hardware documentation says that per-lane divergence is handled through **predication**: the instruction passes through the ALUs for the whole warp, and a mask decides which lanes keep the result.[^hip-hw][^hip-model] The third example shows the compiler side in MLIR's `gpu` dialect. It has operations for the indices this chapter computed by hand, `gpu.thread_id`, `gpu.block_id` and `gpu.block_dim`,[^mlir-thread-id] inside a `gpu.func` marked `kernel`.[^mlir-func] Its two kernels each branch on a thread's index, and `mlir-opt --canonicalize` runs MLIR's canonicalizer, a set of simplifying rewrites, over them.[^mlir-canon]

--8<-- "includes/examples/gpu/g2-simt/thread_branch.mlir.md"

In `@classify`, the `scf.if` only picks one of two constants, and the canonicalizer replaced it with arithmetic on the condition: the tag is the comparison `tid >= 16`, widened from one bit to 32. No lane now waits for another. In `@increment`, the arm loads and stores memory, and a lane past the end of the array must not do that, so the branch stays. That branch is the **boundary guard** that closes this chapter.

Divergence can also be cut by changing the data. At WWDC 2022, Apple described how Blender's Cycles renderer sorts ray hits by material type to reduce thread divergence, so that neighbouring threads shade the same material. Apple also reported the price: the sorted order spread the memory accesses out, and the memory management unit (MMU) became the limiter.[^apple-scale] Fewer paths per warp and fewer cache lines per warp are separate goals, and they can conflict.

## Independent thread scheduling

The picture so far has one program counter per warp and an active mask. That is how NVIDIA GPUs before compute capability 7.0 (the Volta generation) worked: one program counter shared by the warp's 32 threads, plus a mask of the active ones. A consequence was that threads of one warp on different sides of a branch could not signal each other, and a lock taken by one lane and awaited by another in the same warp could deadlock.[^pg-its]

From compute capability 7.0 on, NVIDIA GPUs keep a program counter and call stack for every thread, a design NVIDIA calls **independent thread scheduling**. The hardware can yield execution one thread at a time, and a scheduler decides how to group a warp's active threads for SIMT execution; threads can diverge and reconverge in groups smaller than the warp.[^pg-its] This does not remove the cost counted above. The paths of a diverged warp are still issued separately; what changed is how flexibly the hardware interleaves them, and that one lane waiting for another in the same warp can make progress.

It did change a correctness rule. Older code sometimes assumed that the 32 threads of a warp executed every instruction together, and skipped synchronization inside a warp, for example in a reduction. With independent thread scheduling, that assumption is invalid, and the Programming Guide asks for an explicit `__syncwarp()` at the points where the threads must meet.[^pg-its]

The same guide offers **warp functions** that exchange values between the lanes of a warp without memory: votes such as `__ballot_sync`, which returns a 32-bit mask of the lanes whose predicate is true, and shuffles such as `__shfl_sync`, which reads a register of another lane.[^pg-warp-fns] Each takes a mask naming the lanes that must take part. The guide warns that `__activemask()`, which reports the lanes active at the moment of the call, cannot be used to find which lanes execute a branch.[^pg-warp-fns] Such operations are **convergent**: their result depends on which threads execute them together, so a compiler must not move them across a divergent branch.[^llvm-uniformity] [G6](g6-synchronization.md#shuffles-moving-a-value-without-touching-memory) builds reductions out of them.

## Bringing this back to Vortex: fixed shapes and the boundary guard

[G4](g4-memory-performance.md) put one thread on each `(row, column)` element of the multiply kernel that the GPU chapters return to:

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

Turning the two outer loops into a grid is the first decision a GPU compiler makes for this kernel. G4 settled which loop variable runs across a warp's lanes: `column`, the last index, so that neighbouring lanes touch neighbouring addresses. That makes `column` the block's `x` and `row` its `y`. This chapter adds the other half: given a block shape, which warps are full, which have lanes with nothing to do, and whether a thread needs a guard at all.

Work a small case by hand first. A kernel over a `[f32; 100]` array launches blocks of 32 threads, one warp each. To cover 100 elements it needs `ceil(100 / 32) = 4` blocks, 128 threads. Warps 0, 1 and 2 cover elements 0 to 95; every lane has a real element. Warp 3 covers 96 to 127, and only 4 of its lanes have one. Its other 28 threads would read and write past the end of the array, so every thread runs the guard `if (i < 100)` first, as `@increment` does above.

Now count with this chapter's model. In warps 0 to 2, the guard is uniform, true in every lane: one path, full width. Warp 3 **straddles** the edge: 4 lanes true, 28 false. Its guard has no `else`, so, like the guard row of the second example, it costs no extra issue, only width. Of the 128 lane slots launched, 100 do work. With blocks of 64 threads, the count changes: 2 blocks, 4 warps, and the last warp again straddles with 4 of 32 lanes working. With blocks of 128, one block has 4 warps, and the same warp straddles.

A general-purpose kernel must assume the worst, because the size arrives as a run-time argument. A Vortex compiler need not. Every dimension of a Vortex array is an integer constant expression, evaluated when the type is resolved ([decision 11](../decisions/arrays.md#d11), [decision 52](../decisions/arrays.md#d52)), so for `multiply` the compiler knows both extents are 64 before it generates code.

If the block's width divides 64, its height divides 64 and its thread count is a multiple of 32, every launched thread has an element, every warp is full, and the guard can be left out, a decision the compiler can explain to the programmer as the [sixth principle](../philosophy.md#6-explain-performance-decisions) asks. The [performance philosophy](../philosophy.md#performance-philosophy) lists dividing the computation's indexes among the GPU's workers among the optimizations Vortex should eventually support; this is where that division meets the hardware.

For a shape such as `[f32; 70, 70]`, no block width that is a multiple of 32 divides 70, so with such a block some warps straddle the edge and the guard must stay in them. A width that divides 70, such as 35 or 70, removes the guard but leaves every block a partial warp, the case of Figure 1: the idle lanes move, they do not disappear. Some block shapes also launch warps that lie wholly outside the array: all their lanes find the guard false, so the branch is uniform and they skip the body, but they were still scheduled. The exercise counts all three kinds of warps.

??? check "A kernel over a `[f32; 70]` array launches 3 blocks of 32 threads each, with the guard `i < 70` at the top of every thread. Which warps straddle the edge, how many of their lanes work, and does the guard make any warp issue the body twice?"

    Blocks 0 and 1 cover elements 0 to 63: the guard is true in all their lanes, so it is uniform. Block 2 covers 64 to 95 and straddles: lanes 0 to 5 (elements 64 to 69) work and 26 lanes do not. No warp issues the body twice: the guard has no `else` arm, so the straddling warp runs the body once with 6 of 32 lanes active. Of 96 lane slots launched, 70 do work.

## Measuring it

No timings are claimed here. To see divergence on real hardware:

1. Write a Metal kernel for the M4 Pro (or a CUDA kernel on a rented NVIDIA GPU) whose body is a `switch` with four arms of equal, non-trivial arithmetic, chosen by one of: a constant, `simd_lane % 2`, `simd_lane % 4`, `simd_lane % 32`, or the threadgroup index modulo 4. On the owner's machine, compiling Metal source text at run time works without the offline Metal toolchain.
2. Check the output of every variant against a CPU computation before timing anything.
3. Time many launches of each and report the median with its spread, following [P1](../optimize/p1-measure-first.md).
4. Compare the ratios of the medians with the issued-instruction ratios this chapter's model predicts. Where they disagree, find out why: the compiler may have predicated the arms, or memory may dominate. [G14](g14-measuring-gpu-code.md) covers the profilers.

| Variant | Paths per warp (model) | Median time | Time relative to constant |
| --- | --- | --- | --- |
| constant | 1 | | |
| lane % 2 | 2 | | |
| lane % 4 | 4 | | |
| lane % 32 | 32 | | |
| threadgroup % 4 | 1 | | |

Record the machine, operating system, compiler and date with the table.

## For Vortex

!!! vortex "Exercise"

    Vortex's v0.1 compiler targets CPUs only ([stage 10](../compiler/guide/stage-10-matrix-multiplication.md)), so this is an analysis that prints a report, not code generation.

    **Build** a launch-shape report for a loop nest in your compiler's IR whose outer loops can run in parallel, with constant bounds taken from fixed-shape arrays. Its inputs are the extents of the parallel loops, which loop variable runs across the lanes (as chosen in [G4](g4-memory-performance.md)'s exercise), a block shape, and a warp width, 32 unless the target says otherwise.

    1. The grid: blocks in each direction, total blocks, warps per block, and whether the block's thread count leaves a partial warp.
    2. Whether the threads need a boundary guard.
    3. Every warp, classified as full (every thread has an element), straddling (some do), or empty (none do), with the three counts.
    4. The lane slots launched and the number that do work.
    5. A remark for the programmer, as the [sixth principle](../philosophy.md#6-explain-performance-decisions) asks, such as "block 32 × 8 divides 64 × 64: no guard, every warp full".

    **Not yet:** generating GPU code or choosing between GPU paths ([M12](../mlir/m12-vortex-gpu-path.md)), choosing the block shape for occupancy ([G5](g5-occupancy.md)), and nests whose bounds are not constants.

    **Proof that it works:** golden tests with a warp width of 32, `column` on the lanes, and the block shape written as width × height.

    - `multiply` at `[f32; 64, 64]`, block 32 × 8: a grid of 2 × 8 blocks, 8 warps per block, no guard, 128 full warps, and 4,096 of 4,096 slots working.
    - The same loops over `[f32; 70, 70]`, block 32 × 8: 3 × 9 blocks, 216 warps: 140 full, 70 straddling, 6 empty; 4,900 of 6,912 slots working.
    - `[f32; 70, 70]`, block 16 × 16: 5 × 5 blocks, 200 warps: 140 full, 35 straddling, 25 empty; 4,900 of 6,400.
    - A one-dimensional loop over `[f32; 80]` with blocks of 40 threads: no guard, but 2 partial warps; 80 of 128 slots working.
    - A differential test: for a few hundred random extents and block shapes, the counts match a brute-force program that walks every thread, as this chapter's examples do.

## Key ideas

!!! recap "Questions you can now answer"

    - **How does a thread find its element?** From its block index and thread index: in one dimension, block index × block size + thread index; inside a block, the id is `x + y·Dx + z·Dx·Dy`.
    - **What is a warp, and how wide is it?** The group of threads that executes one instruction at a time: 32 on NVIDIA (warp) and Apple (SIMD-group) GPUs, 64 on AMD CDNA and 32 on RDNA (wavefront).
    - **How does SIMT differ from SIMD?** A SIMD instruction exposes its width to the software, which must pack data and handle masks itself. A SIMT program describes one thread, and the hardware runs 32 of them together and masks lanes when they disagree.
    - **What does a divergent `if`/`else` cost?** The warp issues both arms one after the other, so time grows with the number of paths its lanes take, and in each arm the lanes on the other path sit idle.
    - **What does a loop with per-lane trip counts cost?** As many iterations as the longest lane needs, with finished lanes masked off until then.
    - **What did independent thread scheduling change?** Each thread has its own program counter, so threads can diverge and reconverge in smaller groups and wait for each other, and warp-synchronous code needs an explicit `__syncwarp`. Divergent paths still run separately.
    - **Why can a Vortex compiler drop the boundary guard for `multiply` at 64 × 64?** The extents are compile-time constants, and a block shape that divides both leaves no thread without an element.

## Where this comes back

!!! next "You will use this again in"

    - [G3. The GPU memory hierarchy](g3-memory-hierarchy.md): *block*, *streaming multiprocessor*
    - [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md): *warp*, *lane*, *thread id*
    - [G5. Occupancy and latency hiding](g5-occupancy.md): *warp*, *zero-cost warp switching*
    - [G6. Synchronization, atomics and reductions](g6-synchronization.md): *warp functions*, *convergent*, *`__syncwarp`*
    - [G9. GPU compilers inside LLVM](g9-gpu-compilers-in-llvm.md): *uniform*, *divergent branch*, *linearization*
    - [G10. The GPU matmul ladder](g10-matmul-ladder.md): *thread mapping*, *block shape*
    - [G13. Tile languages](g13-tile-languages.md): *boundary guard*
    - [M10. MLIR for GPUs](../mlir/m10-mlir-for-gpus.md): *`gpu.thread_id`*, *`gpu.block_id`*

## Sources and further reading

Read the Programming Guide's SIMT execution model and independent thread scheduling sections first, then LLVM's convergence and uniformity page for the compiler's view; the HIP and Apple sources cover the same ground for their hardware.

[^pg-simt-basics]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.1, "Basics of SIMT". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#basics-of-simt>
[^pg-threads]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.2, "Thread Hierarchy". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#thread-hierarchy>
[^pg-hw]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 3.2.2, "Hardware Implementation". <https://docs.nvidia.com/cuda/cuda-programming-guide/03-advanced/advanced-kernel-programming.html#hardware-implementation>
[^pg-simt]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 3.2.2.1, "SIMT Execution Model". <https://docs.nvidia.com/cuda/cuda-programming-guide/03-advanced/advanced-kernel-programming.html#simt-execution-model>
[^pg-its]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 3.2.2.1.1, "Independent Thread Scheduling". <https://docs.nvidia.com/cuda/cuda-programming-guide/03-advanced/advanced-kernel-programming.html#independent-thread-scheduling>
[^pg-mt]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 3.2.2.2, "Hardware Multithreading". <https://docs.nvidia.com/cuda/cuda-programming-guide/03-advanced/advanced-kernel-programming.html#hardware-multithreading>
[^pg-warp-fns]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 5.4.6, "Warp Functions". <https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-extensions.html#warp-functions>
[^hip-model]: AMD, "Programming model", HIP documentation, sections "Single instruction multiple threads (SIMT)" and "Warp (or Wavefront)". <https://rocm.docs.amd.com/projects/HIP/en/latest/understand/programming_model.html>
[^hip-hw]: AMD, "Hardware implementation", HIP documentation, sections "Scalar arithmetic logic unit (SALU)" and "Branch unit". <https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html>
[^apple-scale]: Apple, "Scale compute workloads across Apple GPUs", WWDC22 session 10159, 2022. <https://developer.apple.com/videos/play/wwdc2022/10159/>
[^llvm-uniformity]: LLVM Project, "Convergence And Uniformity". <https://llvm.org/docs/ConvergenceAndUniformity.html>
[^mlir-thread-id]: MLIR Project, "'gpu' Dialect", operations `gpu.thread_id`, `gpu.block_id` and `gpu.block_dim`. <https://mlir.llvm.org/docs/Dialects/GPU/#gputhread_id-gputhreadidop>
[^mlir-func]: MLIR Project, "'gpu' Dialect", operation `gpu.func`. <https://mlir.llvm.org/docs/Dialects/GPU/#gpufunc-gpugpufuncop>
[^mlir-canon]: MLIR Project, "Operation Canonicalization". <https://mlir.llvm.org/docs/Canonicalization/>
