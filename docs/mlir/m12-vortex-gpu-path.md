# M12. Designing Vortex's GPU path

<p class="page-intro">Five ways a compiler's own IR can reach a GPU, traced with one matrix multiply through the steps this book's Mac can check, and weighed against the promises Vortex already makes: fixed shapes, strict floating point and a CPU reference to agree with. The chapter lays out the options and the questions each must answer; it makes no decision.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 35 minutes · Builds on: [M10. MLIR for GPUs](m10-mlir-for-gpus.md), [M11. End-to-end ML compilers](m11-ml-compilers.md)</p>

???+ remember "Before you start, remember"

    ??? question "Why can a Vortex compiler drop the boundary guard `if (i < n)` from `multiply` at 64 × 64?"

        The extents are compile-time constants, part of the array types. A block shape whose width and height both divide 64 leaves no launched thread without an element, so no thread needs the guard.

        Introduced in [G2. The SIMT execution model](../gpu/g2-simt.md#bringing-this-back-to-vortex-fixed-shapes-and-the-boundary-guard).

    ??? question "What does MLIR's kernel-outlining pass do, and what does it leave behind?"

        It moves the body of a `gpu.launch` into a kernel function inside a separate `gpu.module`, so that host code and device code can be compiled apart, and it leaves a `gpu.launch_func` at the place where the region was.

        Introduced in [G7. Programming models tour](../gpu/g7-programming-models.md#same-source-or-two).

    ??? question "What is a memref descriptor?"

        The run-time record that a `memref` becomes when it is lowered to the `llvm` dialect: pointers to the data, an offset, and a size and a stride for each dimension.

        Introduced in [M4. Dialect conversion and lowering to LLVM](m4-dialect-conversion.md#types-change-too).

    ??? question "May a Vortex compiler turn `sum += a[row, k] * b[k, column]` into one fused multiply-add?"

        No. Each `f32` operation is one IEEE 754 operation, rounded to nearest with ties to even, and a compiler must not contract, reassociate or reorder them. The product is rounded, then the sum.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

    ??? question "Which GPU schedules can change the bits of a sum, and which cannot?"

        Changing which thread computes which output cannot, as long as each output's sum is still added in increasing `k` by one thread. Splitting one sum among several threads can, because it changes the grouping of the additions.

        Introduced in [G6. Synchronization, atomics and reductions](../gpu/g6-synchronization.md#choosing-a-reduction-vortex-can-promise).

!!! goals "In this chapter"

    - Trace a fixed-shape matrix multiply through MLIR's GPU passes by hand, and predict which block and thread compute each output.
    - Explain what a kernel's calling convention costs for a general `memref`, and what a fixed shape removes.
    - Compare five ways to reach a GPU by the targets they reach, the work they leave to Vortex and what this Mac can check.
    - Identify, for any of the five, the defaults that would break Vortex's floating-point promise, and the questions it must still answer.
    - Write a feature decision worksheet for one GPU path, with a test that would prove it agrees with the CPU reference.

## The kernel, and the facts it arrives with

The GPU chapters keep returning to one function, the 64 × 64 version of the [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for) matrix multiply:

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

By the time a GPU path sees this function, the front end has settled four facts about it. Every array's shape is a constant in its type ([decision 11](../decisions/arrays.md#d11)), stored row after row ([decision 43](../decisions/arrays.md#d43)). The output `c` is lent as `&mut`, so it overlaps neither input ([decision 25](../decisions/references.md#d25)). Every `f32` operation rounds once, in the written order, with no fused multiply-add ([decision 56](../decisions/numbers.md#d56)). And each `sum` starts at `0.0` and adds its products in increasing `k`.

A GPU path is a way to keep all four while turning the two outer loops into threads. The rest of the chapter asks what each candidate path does with them.

## A matrix multiply through MLIR's GPU passes

Start with the route that can be checked on this book's Mac without a GPU. The example below writes a 4 × 4 matrix multiply as one `linalg.matmul` on `memref`s, small enough to read every line of the output. Four stock `mlir-opt` passes then map it onto a GPU: `--convert-linalg-to-parallel-loops` turns the matmul into an `scf.parallel` loop over rows and columns around an ordinary `scf.for` over `k`; `--gpu-map-parallel-loops` tags each parallel dimension with a hardware dimension; `--convert-parallel-loops-to-gpu` rewrites the tagged loop as a `gpu.launch`; and `--gpu-kernel-outlining` moves the launch body into a kernel.[^passes] [^gpu]

--8<-- "includes/examples/mlir/m12-vortex-gpu-path/matmul-naive-launch.mlir.md"

Read the output in three places.

First, the **launch configuration**, the grid and block sizes a launch asks for: `blocks in (%0, %1, %c1_0) threads in (%c1_0, %c1_0, %c1_0)`. Both `%0` and `%1` are `(4 - 0) ceildiv 1`, which is 4, so the grid is 4 × 4 blocks and each block has one thread. Nothing in these passes chose a block size, so each iteration of the parallel loops became a block of its own. The kernel even records the fact, as `gpu.known_block_size = array<i32: 1, 1, 1>`, a promise the gpu dialect lets later passes rely on.[^gpu]

Second, **which thread owns which output**. `%12` is `block_id x` scaled and offset by `%arg0` and `%arg1`, which the launch passes as 1 and 0, so `%12` is the block's `x` number. It is the first index of `%arg2`, the matrix `a`, and of `%arg4`, the output: `%12` is the row. By the same reading `%13`, from `block_id y`, is the column. The mapping pass numbered the hardware dimensions in loop order, so the outer loop, `row`, went to `x`.

Third, the **body**. The `scf.for` over `k` loads `c[row, column]`, multiplies `a[row, k]` by `b[k, column]`, adds the product to the loaded value and stores the sum back, for `k` from 0 to 3 in order. One thread runs the whole loop for its element, and each operation carries no fast-math flag. That is the same sequence of roundings as the CPU loop, on one condition: `linalg.matmul` adds into whatever `c` already holds, while Vortex's `sum` starts at `0.0`. A lowering that uses `linalg.matmul` has to fill `c` with zeros first.

For the real `multiply`, the same passes would ask for 64 × 64 = 4,096 blocks of one thread each. G2's warps, G4's coalescing and G10's tiles all assume blocks of many threads, so this default is a correct kernel and a poor schedule.

### Tiling changes the launch, not the arithmetic

The second example inserts one pass before the mapping: `--scf-parallel-loop-tiling=parallel-loop-tile-sizes=2,2`, which splits each parallel dimension into a loop over tiles and a loop inside a tile.[^passes]

--8<-- "includes/examples/mlir/m12-vortex-gpu-path/matmul-tiled-launch.mlir.md"

The launch is now `blocks in (%2, %3, ...) threads in (%4, %5, ...)`. `%2` and `%3` are `(4 - 0) ceildiv 2`, so the grid is 2 × 2 blocks. `%4` and `%5` are `(2 - 0) ceildiv 1`, so each block is 2 × 2 threads. The loop over tiles became the grid and the loop inside a tile became the block.

Work one element by hand to see it. Inside the kernel, `%16 = %14 + %12`: the thread's `x` number plus twice the block's `x` number. `%17` is the same for `y`. So the element in row 3, column 1 belongs to block `(1, 0)`, thread `(1, 1)`: 2 × 1 + 1 = 3 and 2 × 0 + 1 = 1.

<figure class="vx-figure">
<svg viewBox="0 0 800 300" role="img" aria-label="The 4 by 4 output matrix painted with the block and thread that compute each element, before and after tiling" aria-describedby="m12-f2-desc">
<desc id="m12-f2-desc">Two 4 by 4 grids, one per example, with rows numbered 0 to 3 down the side and columns 0 to 3 across the top. Each cell shows the block number and the thread number that compute that element of c. In the left grid, from the naive example, cell row r, column c is block (r, c), thread (0, 0): sixteen blocks of one thread. In the right grid, from the tiled example, thick outlines group the cells into four 2 by 2 tiles; cell row r, column c is block (r div 2, c div 2), thread (r mod 2, c mod 2). The cell at row 3, column 1 is highlighted in both: block (3, 1) thread (0, 0) on the left, block (1, 0) thread (1, 1) on the right.</desc>
<text class="vx-text" x="80" y="46">Naive: 4 × 4 blocks of 1 thread</text>
<text class="vx-text-muted" x="112" y="70" text-anchor="middle">col 0</text>
<text class="vx-text-muted" x="176" y="70" text-anchor="middle">col 1</text>
<text class="vx-text-muted" x="240" y="70" text-anchor="middle">col 2</text>
<text class="vx-text-muted" x="304" y="70" text-anchor="middle">col 3</text>
<text class="vx-text-muted" x="72" y="107" text-anchor="end">row 0</text>
<rect class="vx-box" x="80" y="80" width="64" height="44"/>
<text class="vx-mono" x="112" y="98" text-anchor="middle">b 0,0</text>
<text class="vx-mono vx-text-muted" x="112" y="115" text-anchor="middle">t 0,0</text>
<rect class="vx-box" x="144" y="80" width="64" height="44"/>
<text class="vx-mono" x="176" y="98" text-anchor="middle">b 0,1</text>
<text class="vx-mono vx-text-muted" x="176" y="115" text-anchor="middle">t 0,0</text>
<rect class="vx-box" x="208" y="80" width="64" height="44"/>
<text class="vx-mono" x="240" y="98" text-anchor="middle">b 0,2</text>
<text class="vx-mono vx-text-muted" x="240" y="115" text-anchor="middle">t 0,0</text>
<rect class="vx-box" x="272" y="80" width="64" height="44"/>
<text class="vx-mono" x="304" y="98" text-anchor="middle">b 0,3</text>
<text class="vx-mono vx-text-muted" x="304" y="115" text-anchor="middle">t 0,0</text>
<text class="vx-text-muted" x="72" y="151" text-anchor="end">row 1</text>
<rect class="vx-box" x="80" y="124" width="64" height="44"/>
<text class="vx-mono" x="112" y="142" text-anchor="middle">b 1,0</text>
<text class="vx-mono vx-text-muted" x="112" y="159" text-anchor="middle">t 0,0</text>
<rect class="vx-box" x="144" y="124" width="64" height="44"/>
<text class="vx-mono" x="176" y="142" text-anchor="middle">b 1,1</text>
<text class="vx-mono vx-text-muted" x="176" y="159" text-anchor="middle">t 0,0</text>
<rect class="vx-box" x="208" y="124" width="64" height="44"/>
<text class="vx-mono" x="240" y="142" text-anchor="middle">b 1,2</text>
<text class="vx-mono vx-text-muted" x="240" y="159" text-anchor="middle">t 0,0</text>
<rect class="vx-box" x="272" y="124" width="64" height="44"/>
<text class="vx-mono" x="304" y="142" text-anchor="middle">b 1,3</text>
<text class="vx-mono vx-text-muted" x="304" y="159" text-anchor="middle">t 0,0</text>
<text class="vx-text-muted" x="72" y="195" text-anchor="end">row 2</text>
<rect class="vx-box" x="80" y="168" width="64" height="44"/>
<text class="vx-mono" x="112" y="186" text-anchor="middle">b 2,0</text>
<text class="vx-mono vx-text-muted" x="112" y="203" text-anchor="middle">t 0,0</text>
<rect class="vx-box" x="144" y="168" width="64" height="44"/>
<text class="vx-mono" x="176" y="186" text-anchor="middle">b 2,1</text>
<text class="vx-mono vx-text-muted" x="176" y="203" text-anchor="middle">t 0,0</text>
<rect class="vx-box" x="208" y="168" width="64" height="44"/>
<text class="vx-mono" x="240" y="186" text-anchor="middle">b 2,2</text>
<text class="vx-mono vx-text-muted" x="240" y="203" text-anchor="middle">t 0,0</text>
<rect class="vx-box" x="272" y="168" width="64" height="44"/>
<text class="vx-mono" x="304" y="186" text-anchor="middle">b 2,3</text>
<text class="vx-mono vx-text-muted" x="304" y="203" text-anchor="middle">t 0,0</text>
<text class="vx-text-muted" x="72" y="239" text-anchor="end">row 3</text>
<rect class="vx-box" x="80" y="212" width="64" height="44"/>
<text class="vx-mono" x="112" y="230" text-anchor="middle">b 3,0</text>
<text class="vx-mono vx-text-muted" x="112" y="247" text-anchor="middle">t 0,0</text>
<rect class="vx-cell-on" x="144" y="212" width="64" height="44"/>
<text class="vx-mono" x="176" y="230" text-anchor="middle">b 3,1</text>
<text class="vx-mono vx-text-muted" x="176" y="247" text-anchor="middle">t 0,0</text>
<rect class="vx-box" x="208" y="212" width="64" height="44"/>
<text class="vx-mono" x="240" y="230" text-anchor="middle">b 3,2</text>
<text class="vx-mono vx-text-muted" x="240" y="247" text-anchor="middle">t 0,0</text>
<rect class="vx-box" x="272" y="212" width="64" height="44"/>
<text class="vx-mono" x="304" y="230" text-anchor="middle">b 3,3</text>
<text class="vx-mono vx-text-muted" x="304" y="247" text-anchor="middle">t 0,0</text>
<text class="vx-text" x="470" y="46">Tiled 2 × 2: 2 × 2 blocks of 2 × 2 threads</text>
<text class="vx-text-muted" x="502" y="70" text-anchor="middle">col 0</text>
<text class="vx-text-muted" x="566" y="70" text-anchor="middle">col 1</text>
<text class="vx-text-muted" x="630" y="70" text-anchor="middle">col 2</text>
<text class="vx-text-muted" x="694" y="70" text-anchor="middle">col 3</text>
<text class="vx-text-muted" x="462" y="107" text-anchor="end">row 0</text>
<rect class="vx-box" x="470" y="80" width="64" height="44"/>
<text class="vx-mono" x="502" y="98" text-anchor="middle">b 0,0</text>
<text class="vx-mono vx-text-muted" x="502" y="115" text-anchor="middle">t 0,0</text>
<rect class="vx-box" x="534" y="80" width="64" height="44"/>
<text class="vx-mono" x="566" y="98" text-anchor="middle">b 0,0</text>
<text class="vx-mono vx-text-muted" x="566" y="115" text-anchor="middle">t 0,1</text>
<rect class="vx-box" x="598" y="80" width="64" height="44"/>
<text class="vx-mono" x="630" y="98" text-anchor="middle">b 0,1</text>
<text class="vx-mono vx-text-muted" x="630" y="115" text-anchor="middle">t 0,0</text>
<rect class="vx-box" x="662" y="80" width="64" height="44"/>
<text class="vx-mono" x="694" y="98" text-anchor="middle">b 0,1</text>
<text class="vx-mono vx-text-muted" x="694" y="115" text-anchor="middle">t 0,1</text>
<text class="vx-text-muted" x="462" y="151" text-anchor="end">row 1</text>
<rect class="vx-box" x="470" y="124" width="64" height="44"/>
<text class="vx-mono" x="502" y="142" text-anchor="middle">b 0,0</text>
<text class="vx-mono vx-text-muted" x="502" y="159" text-anchor="middle">t 1,0</text>
<rect class="vx-box" x="534" y="124" width="64" height="44"/>
<text class="vx-mono" x="566" y="142" text-anchor="middle">b 0,0</text>
<text class="vx-mono vx-text-muted" x="566" y="159" text-anchor="middle">t 1,1</text>
<rect class="vx-box" x="598" y="124" width="64" height="44"/>
<text class="vx-mono" x="630" y="142" text-anchor="middle">b 0,1</text>
<text class="vx-mono vx-text-muted" x="630" y="159" text-anchor="middle">t 1,0</text>
<rect class="vx-box" x="662" y="124" width="64" height="44"/>
<text class="vx-mono" x="694" y="142" text-anchor="middle">b 0,1</text>
<text class="vx-mono vx-text-muted" x="694" y="159" text-anchor="middle">t 1,1</text>
<text class="vx-text-muted" x="462" y="195" text-anchor="end">row 2</text>
<rect class="vx-box" x="470" y="168" width="64" height="44"/>
<text class="vx-mono" x="502" y="186" text-anchor="middle">b 1,0</text>
<text class="vx-mono vx-text-muted" x="502" y="203" text-anchor="middle">t 0,0</text>
<rect class="vx-box" x="534" y="168" width="64" height="44"/>
<text class="vx-mono" x="566" y="186" text-anchor="middle">b 1,0</text>
<text class="vx-mono vx-text-muted" x="566" y="203" text-anchor="middle">t 0,1</text>
<rect class="vx-box" x="598" y="168" width="64" height="44"/>
<text class="vx-mono" x="630" y="186" text-anchor="middle">b 1,1</text>
<text class="vx-mono vx-text-muted" x="630" y="203" text-anchor="middle">t 0,0</text>
<rect class="vx-box" x="662" y="168" width="64" height="44"/>
<text class="vx-mono" x="694" y="186" text-anchor="middle">b 1,1</text>
<text class="vx-mono vx-text-muted" x="694" y="203" text-anchor="middle">t 0,1</text>
<text class="vx-text-muted" x="462" y="239" text-anchor="end">row 3</text>
<rect class="vx-box" x="470" y="212" width="64" height="44"/>
<text class="vx-mono" x="502" y="230" text-anchor="middle">b 1,0</text>
<text class="vx-mono vx-text-muted" x="502" y="247" text-anchor="middle">t 1,0</text>
<rect class="vx-cell-on" x="534" y="212" width="64" height="44"/>
<text class="vx-mono" x="566" y="230" text-anchor="middle">b 1,0</text>
<text class="vx-mono vx-text-muted" x="566" y="247" text-anchor="middle">t 1,1</text>
<rect class="vx-box" x="598" y="212" width="64" height="44"/>
<text class="vx-mono" x="630" y="230" text-anchor="middle">b 1,1</text>
<text class="vx-mono vx-text-muted" x="630" y="247" text-anchor="middle">t 1,0</text>
<rect class="vx-box" x="662" y="212" width="64" height="44"/>
<text class="vx-mono" x="694" y="230" text-anchor="middle">b 1,1</text>
<text class="vx-mono vx-text-muted" x="694" y="247" text-anchor="middle">t 1,1</text>
<rect class="vx-line" x="470" y="80" width="128" height="88" style="stroke-width:3"/>
<rect class="vx-line" x="598" y="80" width="128" height="88" style="stroke-width:3"/>
<rect class="vx-line" x="470" y="168" width="128" height="88" style="stroke-width:3"/>
<rect class="vx-line" x="598" y="168" width="128" height="88" style="stroke-width:3"/>
<text class="vx-text-muted" x="20" y="290">b x,y = gpu.block_id x, y · t x,y = gpu.thread_id x, y · highlighted: c[3, 1]</text>
</svg>
<figcaption>Figure 1. Who computes what, read off the two examples' output. Left: the naive mapping, one block per element. Right: after 2 × 2 tiling, one block per tile and one thread per element of the tile. In both, <code>x</code> follows the row. The highlighted element is <code>c[3, 1]</code>. The <code>k</code> loop inside each cell is the same in both grids.</figcaption>
</figure>

Two things did not change. The `scf.for` over `k` and its load, multiply, add and store are the same operations as before, so every element's sum is still formed by one thread in increasing `k`. This is [G10](../gpu/g10-matmul-ladder.md#tiling-is-a-schedule-change-not-new-arithmetic)'s point seen in IR: tiling the output changes the schedule, not the arithmetic. And `x` still follows `row`. Neighbouring threads in `x` handle neighbouring rows of the same column, which in a row-major `c` are a whole row apart in memory. [G4](../gpu/g4-memory-performance.md#which-index-runs-across-the-warp) put `column` on `x` so that a warp's lanes touch neighbouring addresses; these passes, left to their defaults, do the opposite.

Something above these passes has to choose tile sizes and the order of dimensions: a cost model, a target description or the programmer. The passes only carry out the choice.

??? check "Predict the launch for the same pipeline on a 64 × 64 `linalg.matmul` with tile sizes `32,8`. Then say what a warp's 32 lanes have in common."

    Rows are tiled by 32 and columns by 8, so the grid is 64 / 32 = 2 blocks in `x` by 64 / 8 = 8 in `y`, and each block is 32 × 8 threads. A warp is 32 consecutive thread numbers, `x` varying fastest, so one warp holds `x` = 0 to 31 at a single `y`: 32 different rows of one column. Its 32 stores to `c` are 64 floats apart. With `mlir-opt` 18.1.8 the launch reads `blocks in (%c2, %c8, %c1) threads in (%c32, %c8, %c1)` (checked on 2026-09-24).

## One step further: lowering into NVVM

The next step toward NVIDIA hardware rewrites the `gpu` operations inside a kernel into the `nvvm` dialect, MLIR's model of NVIDIA's device intrinsics.[^nvvm] The example takes a kernel that writes its own block and thread number:

--8<-- "includes/examples/mlir/m12-vortex-gpu-path/gpu-to-nvvm-ids.mlir.md"

`gpu.block_id x` became `nvvm.read.ptx.sreg.ctaid.x` and `gpu.thread_id x` became `nvvm.read.ptx.sreg.tid.x`: reads of the special registers that hold a thread's block and thread number. The function became an `llvm.func` marked `nvvm.kernel`. Its single argument became five.

Those five are the **memref descriptor** that M4 introduced, **unbundled**, passed field by field rather than as one struct. MLIR's default calling convention does this for every ranked `memref` argument, and the descriptor holds, in order, the pointer that was allocated, the pointer aligned for access, an offset in elements, and one size and one stride per dimension.[^llvmir] For `memref<2xi32>` that is two pointers and three `i64`s; for a two-dimensional `memref<64x64xf32>` it would be seven arguments. The kernel's first act is to pack them back into a struct.

A general `memref` needs all of them. If its shape is `?`, known only at run time, the kernel cannot index it without being told the sizes and strides. Vortex's arrays never have that problem: `multiply`'s shapes are in its types. MLIR has a convention for exactly this case.

--8<-- "includes/examples/mlir/m12-vortex-gpu-path/gpu-to-nvvm-bare-ptr.mlir.md"

With `use-bare-ptr-memref-call-conv`, the kernel takes one pointer. The option requires every `memref` to have a static shape, and the convention also requires the default layout and a single pointer for allocation and access.[^passes] [^llvmir] Inside the kernel the descriptor is still built, but its offset, size and stride are now `llvm.mlir.constant`s, known when the kernel is compiled.

<figure class="vx-figure">
<svg viewBox="0 0 760 260" role="img" aria-label="The kernel arguments for one memref under the default and the bare-pointer calling conventions" aria-describedby="m12-f3-desc">
<desc id="m12-f3-desc">Top: five argument boxes, allocated pointer, aligned pointer, offset, size of dimension 0 and stride of dimension 0, each with an arrow down into one long box, the descriptor struct rebuilt inside the kernel. Bottom: one argument box, a pointer used as both the allocated and the aligned pointer, followed by three highlighted boxes, offset 0, size 2 and stride 1, marked as constants that come from the type.</desc>
<defs><marker id="m12-f3-h" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="24">Default convention: five kernel arguments for one memref&lt;2xi32&gt;</text>
<rect class="vx-box" x="20" y="38" width="136" height="46" rx="4"/>
<text class="vx-mono" x="88" y="57" text-anchor="middle">%arg0: ptr</text>
<text class="vx-text-muted" x="88" y="76" text-anchor="middle">allocated</text>
<line class="vx-line" x1="88" y1="84" x2="88" y2="110" marker-end="url(#m12-f3-h)"/>
<rect class="vx-box" x="166" y="38" width="136" height="46" rx="4"/>
<text class="vx-mono" x="234" y="57" text-anchor="middle">%arg1: ptr</text>
<text class="vx-text-muted" x="234" y="76" text-anchor="middle">aligned</text>
<line class="vx-line" x1="234" y1="84" x2="234" y2="110" marker-end="url(#m12-f3-h)"/>
<rect class="vx-box" x="312" y="38" width="136" height="46" rx="4"/>
<text class="vx-mono" x="380" y="57" text-anchor="middle">%arg2: i64</text>
<text class="vx-text-muted" x="380" y="76" text-anchor="middle">offset</text>
<line class="vx-line" x1="380" y1="84" x2="380" y2="110" marker-end="url(#m12-f3-h)"/>
<rect class="vx-box" x="458" y="38" width="136" height="46" rx="4"/>
<text class="vx-mono" x="526" y="57" text-anchor="middle">%arg3: i64</text>
<text class="vx-text-muted" x="526" y="76" text-anchor="middle">size[0]</text>
<line class="vx-line" x1="526" y1="84" x2="526" y2="110" marker-end="url(#m12-f3-h)"/>
<rect class="vx-box" x="604" y="38" width="136" height="46" rx="4"/>
<text class="vx-mono" x="672" y="57" text-anchor="middle">%arg4: i64</text>
<text class="vx-text-muted" x="672" y="76" text-anchor="middle">stride[0]</text>
<line class="vx-line" x1="672" y1="84" x2="672" y2="110" marker-end="url(#m12-f3-h)"/>
<rect class="vx-box-strong" x="20" y="112" width="720" height="34" rx="4"/>
<text class="vx-mono" x="380" y="134" text-anchor="middle">descriptor rebuilt in the kernel: struct&lt;(ptr, ptr, i64, array&lt;1 x i64&gt;, array&lt;1 x i64&gt;)&gt;</text>
<text class="vx-text" x="20" y="186">Bare-pointer convention: one kernel argument</text>
<rect class="vx-box" x="20" y="200" width="136" height="46" rx="4"/>
<text class="vx-mono" x="88" y="219" text-anchor="middle">%arg0: ptr</text>
<text class="vx-text-muted" x="88" y="238" text-anchor="middle">allocated = aligned</text>
<rect class="vx-box-accent" x="312" y="200" width="136" height="46" rx="4"/>
<text class="vx-mono" x="380" y="219" text-anchor="middle">offset = 0</text>
<text class="vx-text-muted" x="380" y="238" text-anchor="middle">constant</text>
<rect class="vx-box-accent" x="458" y="200" width="136" height="46" rx="4"/>
<text class="vx-mono" x="526" y="219" text-anchor="middle">size[0] = 2</text>
<text class="vx-text-muted" x="526" y="238" text-anchor="middle">constant</text>
<rect class="vx-box-accent" x="604" y="200" width="136" height="46" rx="4"/>
<text class="vx-mono" x="672" y="219" text-anchor="middle">stride[0] = 1</text>
<text class="vx-text-muted" x="672" y="238" text-anchor="middle">constant</text>
<text class="vx-text-muted" x="166" y="228">+ from the type:</text>
</svg>
<figcaption>Figure 2. One <code>memref&lt;2xi32&gt;</code> argument under two calling conventions, read off the two NVVM examples. With the default convention, every descriptor field is a run-time argument. With a static shape and the bare-pointer convention, only the data pointer crosses the boundary and the rest are constants in the kernel.</figcaption>
</figure>

The difference is more than argument count. A constant stride lets the kernel's address arithmetic fold, and a kernel that knows `c` has 64 columns can compute an offset with a shift. What neither signature carries is decision 25: nothing on the pointer says that `c` overlaps neither input. As [M2](m2-reading-mlir.md#where-vortexs-facts-would-live) found, the `memref` type has no place for that fact, so a Vortex lowering would have to attach it by other means or lose it.

### Where this Mac stops

The `nvvm` dialect is still MLIR. `mlir-translate --mlir-to-llvmir` turns it into LLVM IR, with calls to `llvm.nvvm.read.ptx.sreg.tid.x` and a kernel annotation, and that step works on this Mac too. The step after it does not. PTX, the text form that NVIDIA's driver loads, comes from `llc` with the NVPTX back end, and the `llc` 18.1.8 on this machine registers only AArch64 and ARM64 targets: asked for `nvptx64`, it reports an invalid target (checked on 2026-09-24).[^nvptx]

<figure class="vx-figure">
<svg viewBox="0 0 760 310" role="img" aria-label="Which steps of two GPU routes can be checked on this book's Mac" aria-describedby="m12-f4-desc">
<desc id="m12-f4-desc">Top: eight boxes in two rows for the MLIR route toward NVIDIA: linalg.matmul, scf.parallel, gpu.launch, gpu.module plus launch_func, nvvm dialect and LLVM IR are highlighted as checked on this Mac with mlir-opt or mlir-translate; PTX text, which needs llc with NVPTX, and running on a GPU, which needs NVIDIA hardware, are marked as not runnable here. Bottom: three highlighted boxes for the direct-MSL route: MSL source text written by hand, compiled by makeLibrary at run time, and run on the Apple GPU on 2026-09-23.</desc>
<defs><marker id="m12-f4-h" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="24">MLIR route toward NVIDIA</text>
<rect class="vx-box-accent" x="20" y="38" width="165" height="52" rx="4"/>
<text class="vx-mono" x="102" y="60" text-anchor="middle">linalg.matmul</text>
<text class="vx-text-muted" x="102" y="80" text-anchor="middle">mlir-opt</text>
<line class="vx-line" x1="185" y1="64" x2="203" y2="64" marker-end="url(#m12-f4-h)"/>
<rect class="vx-box-accent" x="205" y="38" width="165" height="52" rx="4"/>
<text class="vx-mono" x="287" y="60" text-anchor="middle">scf.parallel</text>
<text class="vx-text-muted" x="287" y="80" text-anchor="middle">mlir-opt</text>
<line class="vx-line" x1="370" y1="64" x2="388" y2="64" marker-end="url(#m12-f4-h)"/>
<rect class="vx-box-accent" x="390" y="38" width="165" height="52" rx="4"/>
<text class="vx-mono" x="472" y="60" text-anchor="middle">gpu.launch</text>
<text class="vx-text-muted" x="472" y="80" text-anchor="middle">mlir-opt</text>
<line class="vx-line" x1="555" y1="64" x2="573" y2="64" marker-end="url(#m12-f4-h)"/>
<rect class="vx-box-accent" x="575" y="38" width="165" height="52" rx="4"/>
<text class="vx-mono" x="657" y="60" text-anchor="middle">gpu.module + launch_func</text>
<text class="vx-text-muted" x="657" y="80" text-anchor="middle">mlir-opt</text>
<rect class="vx-box-accent" x="20" y="128" width="165" height="52" rx="4"/>
<text class="vx-mono" x="102" y="150" text-anchor="middle">nvvm dialect</text>
<text class="vx-text-muted" x="102" y="170" text-anchor="middle">mlir-opt</text>
<line class="vx-line" x1="185" y1="154" x2="203" y2="154" marker-end="url(#m12-f4-h)"/>
<rect class="vx-box-accent" x="205" y="128" width="165" height="52" rx="4"/>
<text class="vx-mono" x="287" y="150" text-anchor="middle">LLVM IR</text>
<text class="vx-text-muted" x="287" y="170" text-anchor="middle">mlir-translate</text>
<line class="vx-line" x1="370" y1="154" x2="388" y2="154" marker-end="url(#m12-f4-h)"/>
<rect class="vx-box-bad" x="390" y="128" width="165" height="52" rx="4"/>
<text class="vx-mono" x="472" y="150" text-anchor="middle">PTX text</text>
<text class="vx-text-muted" x="472" y="170" text-anchor="middle">needs llc with NVPTX</text>
<line class="vx-line" x1="555" y1="154" x2="573" y2="154" marker-end="url(#m12-f4-h)"/>
<rect class="vx-box-bad" x="575" y="128" width="165" height="52" rx="4"/>
<text class="vx-mono" x="657" y="150" text-anchor="middle">runs on a GPU</text>
<text class="vx-text-muted" x="657" y="170" text-anchor="middle">needs NVIDIA hardware</text>
<path class="vx-line" d="M657 90 L657 110 L102 110 L102 126" marker-end="url(#m12-f4-h)"/>
<text class="vx-text" x="20" y="232">Direct-MSL route toward Apple</text>
<rect class="vx-box-accent" x="20" y="246" width="165" height="52" rx="4"/>
<text class="vx-mono" x="102" y="268" text-anchor="middle">MSL source text</text>
<text class="vx-text-muted" x="102" y="288" text-anchor="middle">hand-written today</text>
<line class="vx-line" x1="185" y1="272" x2="203" y2="272" marker-end="url(#m12-f4-h)"/>
<rect class="vx-box-accent" x="205" y="246" width="165" height="52" rx="4"/>
<text class="vx-mono" x="287" y="268" text-anchor="middle">makeLibrary(source:)</text>
<text class="vx-text-muted" x="287" y="288" text-anchor="middle">Metal, at run time</text>
<line class="vx-line" x1="370" y1="272" x2="388" y2="272" marker-end="url(#m12-f4-h)"/>
<rect class="vx-box-accent" x="390" y="246" width="165" height="52" rx="4"/>
<text class="vx-mono" x="472" y="268" text-anchor="middle">runs on the Apple GPU</text>
<text class="vx-text-muted" x="472" y="288" text-anchor="middle">ran on 2026-09-23</text>
<rect class="vx-box-accent" x="575" y="246" width="20" height="20"/><text class="vx-text-muted" x="602" y="261">checked on this Mac</text>
<rect class="vx-box-bad" x="575" y="276" width="20" height="20"/><text class="vx-text-muted" x="602" y="291">cannot run here</text>
</svg>
<figcaption>Figure 3. How far each route gets on the Mac this book was written on. Every MLIR step is text in, text out, and checkable with <code>mlir-opt</code> and <code>mlir-translate</code>; the first step that needs a GPU-specific code generator is the first one that fails. The Metal route is short, and every step runs.</figcaption>
</figure>

A fresh build fixes the first gap. Homebrew's `llvm` formula ships the MLIR tools, and its build recipe enables every LLVM target, NVPTX and AMDGPU included.[^brew] Nothing on this Mac fixes the second: NVIDIA's CUDA 10.2 was the last toolkit and driver release to support macOS.[^cuda102]

??? check "`mlir-opt` on this Mac turns `gpu.thread_id x` into `nvvm.read.ptx.sreg.tid.x`, yet cannot produce a line of PTX. Why do the two steps differ?"

    Lowering into `nvvm` is a rewrite from one MLIR dialect to another: the pass needs to know NVVM's operations, not NVIDIA's instruction encoding, and it ships with `mlir-opt`. PTX is produced by LLVM's NVPTX code generator, which has to be compiled into `llc`, and this machine's `llc` was built for AArch64 only.

## Five roads out of one IR

Five shapes are worth comparing. All five start from the facts of the first section; they differ in what they emit and who does the rest of the work.

<figure class="vx-figure">
<svg viewBox="0 0 760 410" role="img" aria-label="Five roads from Vortex's own IR to GPU targets" aria-describedby="m12-f1-desc">
<desc id="m12-f1-desc">A box on the left, Vortex's own IR, with arrows to five option boxes stacked in a column. A, direct MSL, highlighted, leads to Apple GPU. B, LLVM IR, leads to NVIDIA, AMD and SPIR-V for Vulkan and OpenCL. C, MLIR, leads to the same three and also to Apple GPU, labelled via SPIRV-Cross. D, a library or tile language, leads to the library's own targets. E, hybrid, leads to Apple GPU, NVIDIA and AMD. A note says the highlighted road is the one that ran a kernel on this book's Mac.</desc>
<defs><marker id="m12-f1-h" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box-strong" x="20" y="180" width="120" height="60" rx="4"/>
<text class="vx-text" x="80" y="206" text-anchor="middle">Vortex's</text>
<text class="vx-text" x="80" y="224" text-anchor="middle">own IR</text>
<rect class="vx-box" x="520" y="26" width="220" height="40" rx="4"/>
<text class="vx-text" x="630" y="51" text-anchor="middle">Apple GPU (Metal)</text>
<rect class="vx-box" x="520" y="106" width="220" height="40" rx="4"/>
<text class="vx-text" x="630" y="131" text-anchor="middle">NVIDIA (PTX)</text>
<rect class="vx-box" x="520" y="166" width="220" height="40" rx="4"/>
<text class="vx-text" x="630" y="191" text-anchor="middle">AMD (AMDGPU)</text>
<rect class="vx-box" x="520" y="226" width="220" height="40" rx="4"/>
<text class="vx-text" x="630" y="251" text-anchor="middle">SPIR-V (Vulkan, OpenCL)</text>
<rect class="vx-box" x="520" y="266" width="220" height="40" rx="4"/>
<text class="vx-text" x="630" y="291" text-anchor="middle">the library's own targets</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5">
<line class="vx-line" x1="140" y1="200" x2="198" y2="46" marker-end="url(#m12-f1-h)"/>
<rect class="vx-box-accent" x="200" y="20" width="200" height="52" rx="4"/>
<text class="vx-text" x="300" y="42" text-anchor="middle">A. Direct MSL</text>
<text class="vx-text-muted" x="300" y="62" text-anchor="middle">MSL source text</text>
<line class="vx-line" x1="400" y1="46" x2="518" y2="46" marker-end="url(#m12-f1-h)"/>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5">
<line class="vx-line" x1="140" y1="205" x2="198" y2="126" marker-end="url(#m12-f1-h)"/>
<rect class="vx-box" x="200" y="100" width="200" height="52" rx="4"/>
<text class="vx-text" x="300" y="122" text-anchor="middle">B. LLVM IR</text>
<text class="vx-text-muted" x="300" y="142" text-anchor="middle">LLVM's GPU back ends</text>
<line class="vx-line" x1="400" y1="126" x2="518" y2="126" marker-end="url(#m12-f1-h)"/>
<line class="vx-line" x1="400" y1="130" x2="518" y2="186" marker-end="url(#m12-f1-h)"/>
<line class="vx-line" x1="400" y1="134" x2="518" y2="246" marker-end="url(#m12-f1-h)"/>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5">
<line class="vx-line" x1="140" y1="210" x2="198" y2="206" marker-end="url(#m12-f1-h)"/>
<rect class="vx-box" x="200" y="180" width="200" height="52" rx="4"/>
<text class="vx-text" x="300" y="202" text-anchor="middle">C. MLIR</text>
<text class="vx-text-muted" x="300" y="222" text-anchor="middle">linalg, scf, gpu, nvvm …</text>
<line class="vx-line" x1="400" y1="200" x2="518" y2="130" marker-end="url(#m12-f1-h)"/>
<line class="vx-line" x1="400" y1="206" x2="518" y2="190" marker-end="url(#m12-f1-h)"/>
<line class="vx-line" x1="400" y1="212" x2="518" y2="246" marker-end="url(#m12-f1-h)"/>
<line class="vx-line" x1="400" y1="194" x2="518" y2="56" marker-end="url(#m12-f1-h)"/>
<text class="vx-text-muted" x="416" y="94">via SPIRV-Cross</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5">
<line class="vx-line" x1="140" y1="215" x2="198" y2="286" marker-end="url(#m12-f1-h)"/>
<rect class="vx-box" x="200" y="260" width="200" height="52" rx="4"/>
<text class="vx-text" x="300" y="282" text-anchor="middle">D. Library or tile DSL</text>
<text class="vx-text-muted" x="300" y="302" text-anchor="middle">calls, not code</text>
<line class="vx-line" x1="400" y1="286" x2="518" y2="286" marker-end="url(#m12-f1-h)"/>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 5">
<line class="vx-line" x1="140" y1="220" x2="198" y2="366" marker-end="url(#m12-f1-h)"/>
<rect class="vx-box" x="200" y="340" width="200" height="52" rx="4"/>
<text class="vx-text" x="300" y="362" text-anchor="middle">E. Hybrid</text>
<text class="vx-text-muted" x="300" y="382" text-anchor="middle">A for Apple, B or C elsewhere</text>
<line class="vx-line" x1="400" y1="352" x2="518" y2="60" marker-end="url(#m12-f1-h)"/>
<line class="vx-line" x1="400" y1="362" x2="518" y2="140" marker-end="url(#m12-f1-h)"/>
<line class="vx-line" x1="400" y1="372" x2="518" y2="200" marker-end="url(#m12-f1-h)"/>
</g>
<text class="vx-text-muted" x="520" y="340">Highlighted: the one road that</text>
<text class="vx-text-muted" x="520" y="358">ran a kernel on this book's Mac.</text>
</svg>
<figcaption>Figure 4. One IR, five roads. A reaches only Apple GPUs and is the only road that has run a kernel on the Mac this book was written on. B and C reach NVIDIA, AMD and SPIR-V; C also has a documented route to Apple through SPIR-V and SPIRV-Cross. D hands the kernel to someone else's code. E combines A with B or C.</figcaption>
</figure>

**A. Direct MSL.** Vortex's IR is printed as Metal Shading Language source text and compiled by Metal, at run time with `makeLibrary(source:options:)`, which compiles an MSL string into a library of functions, or ahead of time into a `.metallib`.[^makelib] [^precompile] In MSL, `multiply` would be a function marked `kernel` that receives its arrays as pointers in the `device` address space, each bound to a numbered buffer slot, and reads its position from a parameter marked `thread_position_in_grid`.[^msl] No descriptor crosses the boundary: shapes would be constants in the text.

Apple documents two ways into its GPUs: MSL source, and DirectX's DXIL, which the Metal shader converter translates into a `.metallib`.[^shaderconv] The intermediate format inside a `.metallib` is not documented. So for a compiler that wants to target Apple GPUs from its own IR, printing MSL is the documented route, and it is the one IREE takes: its Metal driver embeds MSL source and can compile it when the program runs.[^iree-metal]

This route has already run here. A small Swift program compiled an MSL string with `makeLibrary(source:options:)` and dispatched a kernel over about a million elements, with no Metal toolchain installed beyond what macOS provides (Apple M4 Pro, macOS 27, 2026-09-23). The pipeline reported a `threadExecutionWidth` of 32 and at most 1,024 threads per threadgroup; the device reported 32,768 bytes of threadgroup memory and unified memory.

The cost is reach and text. MSL reaches Apple GPUs only. Kernels are generated as strings, so Vortex tracks MSL's language versions the way any source-to-source compiler tracks its target. And MSL's defaults are not Vortex's: the specification sets the default math mode to `fast`, which permits reassociation and contraction across statements, and even the `safe` mode allows contraction within a statement, so `sum += a * b` may become one fused multiply-add unless contraction is turned off with `-ffp-contract=off` or a pragma.[^msl] At run time the same choice is `MTLCompileOptions.mathMode`.[^mathmode]

**B. LLVM GPU back ends.** Vortex's IR becomes LLVM IR, and LLVM's NVPTX, AMDGPU or SPIR-V back end does the rest, the three back ends [G9](../gpu/g9-gpu-compilers-in-llvm.md) opened.[^nvptx] [^amdgpu] [^spirv-target] If Vortex's CPU back end already emits LLVM IR, much of the machinery is shared. The cost is what G9 spent a chapter on: choosing address-space numbers, marking kernels with the right calling convention, calling target intrinsics for thread numbers, and keeping convergent operations out of divergent code. For the SPIR-V target, control flow must also be structured. Every one of those is Vortex's job in this option, because LLVM IR is the first thing Vortex hands over.

**C. MLIR.** Vortex's IR becomes MLIR, most naturally `linalg` on statically shaped `memref`s, or `affine` and `scf` loops, then `gpu`, then `nvvm`, `rocdl` or `spirv`.[^linalg] [^gpu] [^nvvm] [^spirv-dialect] This is the road the last two sections walked. Tiling, bufferization and vectorization exist as passes, and [transform-dialect](m9-transform-dialect.md) schedules are inspectable IR rather than flags, which fits the [second design principle](../philosophy.md#2-say-what-to-calculate-then-decide-how-to-run-it). The gpu dialect also carries the rest of the trip: target attributes such as `#nvvm.target` and a `gpu-module-to-binary` pass that serializes each GPU module into a `gpu.binary`.[^gpu] Apple GPUs are reachable only indirectly, from the `spirv` dialect through SPIRV-Cross, which translates SPIR-V into MSL, the route [M11](m11-ml-compilers.md#irees-route-to-an-apple-gpu) followed through IREE.[^spirv-cross] [^iree-metal]

The costs of C are a dependency and a distance. MLIR is a large C++ project with no stable API: Triton's README says its build will not work at an arbitrary LLVM version and records the exact LLVM commit it builds against.[^triton] Every pass name in this chapter was checked on 18.1.8, while Homebrew's current `llvm` is 23.1.2.[^brew] The distance is diagnostics: MLIR reports errors against MLIR locations, and mapping them back to a Vortex source span, as [M2](m2-reading-mlir.md#locations-travel-with-every-operation) set up, is Vortex's work.

**D. A library or a tile language.** Vortex's IR becomes a call into existing code: a Metal Performance Shaders matrix multiply, a cuBLAS call or a generated Triton kernel.[^mps] [^triton] Someone else has tuned the kernel, so this is the shortest road to good numbers. It is also the road on which Vortex decides least. The library chooses the summation order and the tile sizes, so every Vortex promise about bits becomes a question about what that library documents. And the compiler work this book teaches happens elsewhere.

**E. Hybrid.** Vortex's own IR does the scheduling and the diagnostics, and only the last step differs by target: MSL for Apple (A), and LLVM IR or MLIR for NVIDIA and AMD (B or C). Vortex's diagnostics stay in one place, and a proven lowering is borrowed only where writing one would repeat known work. The cost is two emitters to build and test, and a schedule whose meaning (what a tile size means, what a reduction's grouping means) must be defined once, above both, so that a program does not mean something different on different hardware.

| Option | Reaches | Runs on this Mac today | Vortex writes | Main risk |
| --- | --- | --- | --- | --- |
| A. Direct MSL | Apple GPUs | Yes: MSL compiled at run time | An MSL printer and a Metal host | One vendor; MSL's fast-math defaults |
| B. LLVM back ends | NVIDIA, AMD, SPIR-V | No: this `llc` lacks GPU targets | LLVM IR with address spaces, kernel conventions, intrinsics | Every GPU detail is Vortex's, from the first byte |
| C. MLIR | NVIDIA, AMD, SPIR-V; Apple via SPIRV-Cross | Up to LLVM IR, not to PTX | A lowering into `linalg`, `scf` or `gpu`, and a location map | A large, fast-moving dependency |
| D. Library or DSL | What the library supports | Depends on the library | Calls and data layout | Bits and schedule are the library's |
| E. Hybrid | Union of A and B or C | The Apple half | Two emitters under one schedule | Two paths to keep equivalent |

??? check "Options B and C end at the same three back ends. What is different about what arrives there?"

    Who made the decisions before it arrived. In B, Vortex writes LLVM IR directly, so every address space, kernel attribute, thread-number intrinsic and loop is chosen by Vortex before LLVM sees it. In C, Vortex writes higher-level MLIR, and passes such as tiling, outlining and `convert-gpu-to-nvvm` make those choices in dialects built to represent them; the `nvvm`, `rocdl` or `spirv` IR that reaches the back end is produced by MLIR. The back ends cannot tell which road their input took.

## What every option still has to answer

Naming a road is not the same as having a compiler. Whichever of the five a Vortex GPU path takes, it owes an answer to each of these.

- **Thread mapping.** Which loop becomes `x`, which tile sizes, and where they come from. The worked example showed the stock passes putting `row` on `x` and choosing one thread per block. [G4](../gpu/g4-memory-performance.md#for-vortex) and [G5](../gpu/g5-occupancy.md#for-vortex) built a **target description**, a record of one chip's SIMD width, threadgroup memory and register budget, for this purpose.
- **The host half.** Something has to allocate buffers, copy or share data, compile or load the kernel and launch it. On Metal that is a command queue and a compute encoder, as in the Swift experiment; in MLIR it is `gpu.launch_func` lowered to runtime calls.[^gpu] Apple's unified memory removes the copies but not the launch ([G3](../gpu/g3-memory-hierarchy.md#unified-memory-on-apple-silicon)).
- **Address spaces.** Which memories the language names, and which only the compiler knows about. MSL spells them `device` and `threadgroup`;[^msl] NVPTX and AMDGPU number them ([G9](../gpu/g9-gpu-compilers-in-llvm.md#address-spaces-which-memory-a-pointer-means)).
- **Barriers under divergence.** A barrier or shuffle must not be moved across a divergent branch ([G9](../gpu/g9-gpu-compilers-in-llvm.md#convergent-operations-what-an-optimizer-must-not-move)). Every barrier Vortex introduces needs that protection, not only the ones borrowed passes know about.
- **Reduction order.** Mapping outputs to threads keeps each sum's order, as the worked example showed. Splitting one sum among threads (a **split-K** schedule, which divides the `k` loop between blocks and adds their partial sums) changes it ([G4](../gpu/g4-memory-performance.md#what-does-not-change-the-bits), [G6](../gpu/g6-synchronization.md#choosing-a-reduction-vortex-can-promise)). A Vortex compiler must either forbid such schedules for `f32` or ask the programmer's permission.
- **Floating-point defaults.** Each road has its own. MLIR's `arith` operations default to no fast-math flags, and a Vortex lowering must never add `contract` or `reassoc`.[^arith] MSL defaults to fast math, and contraction must be turned off explicitly.[^msl] A road whose defaults cannot be pinned cannot keep decision 56.
- **Matrix units.** Tensor cores and SIMD-group matrix instructions compute with their own precision and accumulation rules ([G11](../gpu/g11-matrix-units.md#precision-what-the-unit-computes-is-not-what-f32-code-says)); using one for `f32` code needs the programmer's explicit permission.
- **Reporting.** The [sixth design principle](../philosophy.md#6-explain-performance-decisions) asks the compiler to explain its performance decisions: which target, which tile sizes, whether the guard was dropped and why.

??? check "Which of these keep every `c[row, column]` bit-identical to the CPU loop, assuming no fused multiply-add: (1) the naive launch, (2) the 2 × 2 tiled launch, (3) splitting `k` in half between two blocks and adding the halves at the end?"

    (1) and (2). In both, one thread forms each sum, starting from zero and adding products in increasing `k`, the CPU's order. (3) computes (products 0 to 31) + (products 32 to 63), a different grouping, which can round differently.

## A path that fits a Mac-only project

Deciding what to build first, with one Apple-silicon laptop and no rented GPU, is narrower than choosing a road, and it needs no final answer.

Three facts constrain it. Metal is local and works: a kernel compiled from MSL source ran here with nothing extra installed. MLIR's GPU passes are local and checkable up to LLVM IR, as Figure 3 showed, and a build of LLVM with GPU targets would carry them to PTX and AMDGPU text. NVIDIA hardware is not local: CUDA has not supported macOS since 10.2, so running a CUDA or PTX kernel means renting a Linux machine, and AMD's stack is in the same position.[^cuda102]

A learning path shaped by those facts starts with hand-written MSL to build intuition for kernels, then works through MLIR's GPU stack, where every step before the device can be checked here, and treats NVIDIA and AMD hardware as a later decision, made when there is something specific to measure.

Continuous integration raises a fourth question. GitHub's documentation of its Apple-silicon runners says nothing about GPU or Metal access, so this chapter records GPU execution in CI as unknown.[^runners] Text-level tests (golden MLIR, MSL or PTX, compared byte for byte) have no such problem: the examples in this chapter are tests of that kind, and they run wherever `mlir-opt` runs.

??? check "The direct-MSL road and the MLIR road can both be checked on this Mac, but not to the same depth. What is the difference?"

    The MSL road can be checked all the way: Metal compiles the source and runs the kernel on the GPU, so a test can compare its output with the CPU reference. The MLIR road can be checked as text up to LLVM IR, which proves the lowering is well formed but runs nothing; the step to PTX needs an NVPTX-enabled `llc`, and running the result needs NVIDIA hardware.

## For Vortex

!!! vortex "Exercise"

    **Build** a design document, not code: a [feature decision worksheet](../philosophy.md#feature-decision-worksheet) for one of options A to E, applied to `multiply` at `[f32; 64, 64]`. Answer all eight of the worksheet's questions.

    1. **Problem:** which of the [target workloads](../philosophy.md#target-workloads) would a first GPU path serve, and why does it favour your option over the other four?
    2. **Example:** what your option emits for `multiply`, at the level of this chapter's examples: the kernel's parameters, how `row` and `column` become a thread's identity, the launch configuration, and whether the guard is needed. Include a hand trace naming the block and thread that compute `c[37, 5]`.
    3. **Boundary:** one Vortex program your option would handle badly or not at all, and which other option would handle it better.
    4. **Compiler knowledge:** what the compiler must know that type checking does not tell it: the target description's fields, and anything else your option needs.
    5. **Safety:** how your option keeps decision 25 (an `&mut` output overlaps no input) and decision 56 (no contraction, no reassociation), naming the exact default of your road that you must override and where.
    6. **Targets:** your answers to each item in [What every option still has to answer](#what-every-option-still-has-to-answer). "Inherited from the underlying stack" is acceptable where it is true, if you say which document says so.
    7. **Diagnostics:** what the compiler reports when a requested block shape does not fit the target (too many threads, or too much threadgroup memory), and the remark it prints when it drops the guard.
    8. **Testing:** a test that compares your GPU result with the CPU reference for `multiply`, stating whether it demands identical bits or a tolerance and why, from decision 56 and your target's documented floating-point rules; a golden-text test of the emitted kernel; and a test that fails if the launch configuration changes.

    **Not yet:** choosing Vortex's final option; writing an MSL, LLVM IR or MLIR emitter or any other compiler code; automatic tile-size search ([P15](../optimize/p15-choosing-parameters.md)); matrix units; a GPU test harness.

    **Proof that it works:**

    - Every one of the eight questions has an answer, and question 6 addresses every item of the list by name.
    - Your hand trace in question 2 agrees with a second trace done by someone else from your worksheet alone.
    - For question 5, you can point to the line of a specification or documentation page that sets the default you override.
    - A reader who has not seen this chapter can tell from your worksheet which of the five options you chose and why.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does MLIR's stock pipeline do with a matmul when nothing chooses a block size?** It launches one block of one thread per output element, and maps the outer loop, `row`, to `x`.
    - **What does tiling the parallel loops change?** The launch configuration and which thread owns which element; the `k` loop and its arithmetic stay the same.
    - **Why does one `memref` become five kernel arguments, and when can it become one?** The default convention passes each descriptor field separately; with static shapes the bare-pointer convention passes one pointer and makes the rest constants.
    - **Where does this Mac stop on the MLIR road to NVIDIA?** At LLVM IR: PTX needs an `llc` with NVPTX, and running needs NVIDIA hardware.
    - **Which road has run a kernel on this Mac, and what must it override to keep decision 56?** Direct MSL; its default math mode and its contraction default.
    - **Which schedules can change a sum's bits?** Only those that split one sum among threads, such as split-K; remapping outputs to threads cannot.
    - **What does option D give up?** Control of the schedule and the summation order, which become the library's, along with the answer to decision 56.

## Where this comes back

!!! next "You will use this again in"

    - [G14. Measuring GPU code](../gpu/g14-measuring-gpu-code.md): *launch configuration*, *target description*
    - [P15. Choosing parameters: models or search](../optimize/p15-choosing-parameters.md): *tile sizes*, *thread mapping*
    - [E4. Testing back ends](../backend/e4-testing-backends.md): *golden-text tests*, *comparing with a reference*

## Sources and further reading

To go further, read the gpu dialect page and the "LLVM IR Target" page side by side with this chapter's examples, then IREE's Metal driver design document, which is option C with an Apple ending.[^gpu] [^llvmir] [^iree-metal] The Metal Shading Language Specification, sections 1.6.3 and 5.1 to 5.2, is the reference for option A.[^msl]

[^passes]: MLIR Project, "Passes", entries `-convert-linalg-to-parallel-loops`, `-gpu-map-parallel-loops`, `-convert-parallel-loops-to-gpu`, `-gpu-kernel-outlining`, `-scf-parallel-loop-tiling` and `-convert-gpu-to-nvvm` (option `use-bare-ptr-memref-call-conv`). <https://mlir.llvm.org/docs/Passes/>
[^gpu]: MLIR Project, "'gpu' Dialect": `gpu.launch_func`, the `known_block_size` and `known_grid_size` attributes, kernel outlining, GPU target attributes and the `gpu-module-to-binary` pass. <https://mlir.llvm.org/docs/Dialects/GPU/>
[^nvvm]: MLIR Project, "'nvvm' Dialect". <https://mlir.llvm.org/docs/Dialects/NVVMDialect/>
[^llvmir]: MLIR Project, "LLVM IR Target", sections "Ranked MemRef Types", "Default Calling Convention for Ranked MemRef" and "Bare Pointer Calling Convention for Ranked MemRef". <https://mlir.llvm.org/docs/TargetLLVMIR/>
[^linalg]: MLIR Project, "'linalg' Dialect". <https://mlir.llvm.org/docs/Dialects/Linalg/>
[^spirv-dialect]: MLIR Project, "'spirv' Dialect". <https://mlir.llvm.org/docs/Dialects/SPIR-V/>
[^arith]: MLIR Project, "'arith' Dialect", entries `FastMathFlagsAttr` and `FastMathFlags`. <https://mlir.llvm.org/docs/Dialects/ArithOps/>
[^nvptx]: LLVM Project, "User Guide for NVPTX Back-end". <https://llvm.org/docs/NVPTXUsage.html>
[^amdgpu]: LLVM Project, "User Guide for AMDGPU Backend". <https://llvm.org/docs/AMDGPUUsage.html>
[^spirv-target]: LLVM Project, "User Guide for SPIR-V Target". <https://llvm.org/docs/SPIRVUsage.html>
[^msl]: Apple, "Metal Shading Language Specification", version 4.1, sections 1.6.3 "Math Intrinsics Compiler Options", 4.1 "Device Address Space", 4.4 "Threadgroup Address Space", 5.1.3 "Compute Functions (Kernels)", 5.2.1 "Locating Buffer, Texture, and Sampler Arguments" and 5.2.3.6 "Kernel Function Input Attributes". <https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf>
[^makelib]: Apple, "makeLibrary(source:options:)", Metal documentation. <https://developer.apple.com/documentation/metal/mtldevice/makelibrary(source:options:)>
[^mathmode]: Apple, "mathMode", `MTLCompileOptions`, Metal documentation. <https://developer.apple.com/documentation/metal/mtlcompileoptions/mathmode>
[^precompile]: Apple, "Building a shader library by precompiling source files", Metal documentation. <https://developer.apple.com/documentation/metal/building-a-shader-library-by-precompiling-source-files>
[^shaderconv]: Apple, "Metal shader converter". <https://developer.apple.com/metal/shader-converter/>
[^mps]: Apple, "MPSMatrixMultiplication", Metal Performance Shaders documentation. <https://developer.apple.com/documentation/metalperformanceshaders/mpsmatrixmultiplication>
[^iree-metal]: IREE Project, "Metal HAL driver" design document, section "Shader/kernel compilation". <https://iree.dev/developers/design-docs/metal-hal-driver/>
[^spirv-cross]: Khronos Group, SPIRV-Cross repository. <https://github.com/KhronosGroup/SPIRV-Cross>
[^triton]: Triton Project, repository README, section "Building with a custom LLVM". <https://github.com/triton-lang/triton>
[^brew]: Homebrew, `llvm` formula (version 23.1.2 on 2026-09-24) and its build recipe. <https://formulae.brew.sh/formula/llvm> and <https://github.com/Homebrew/homebrew-core/blob/master/Formula/l/llvm.rb>
[^cuda102]: NVIDIA, "CUDA Toolkit 10.2 Release Notes", section 2.1 "General CUDA". <https://docs.nvidia.com/cuda/archive/10.2/cuda-toolkit-release-notes/index.html>
[^runners]: GitHub, "GitHub-hosted runners reference". <https://docs.github.com/en/actions/reference/runners/github-hosted-runners>
