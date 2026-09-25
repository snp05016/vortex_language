# M10. MLIR for GPUs

<p class="page-intro">MLIR's `gpu` dialect writes a kernel launch as ordinary IR: a grid of blocks, a body that every thread runs, and memory the threads of a block share. This chapter follows one small kernel at a time from a parallel loop to a launch, out of its host function into a GPU module, and down to NVIDIA's, AMD's and Khronos's IRs, and marks the point where a Mac without an NVPTX or AMDGPU back end has to stop. It is one of the open paths from loops to NVIDIA, AMD and SPIR-V code that M12 weighs for Vortex.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 40 minutes · Builds on: [M4. Dialect conversion and lowering to LLVM](m4-dialect-conversion.md), [G2. The SIMT execution model](../gpu/g2-simt.md)</p>

???+ remember "Before you start, remember"

    ??? question "A kernel over 4,096 elements runs in blocks of 256 threads. Which element does thread 5 of block 2 handle, and how many blocks does the launch need?"

        Element 2 × 256 + 5 = 517, and 4,096 / 256 = 16 blocks. Each thread computes its own index from its block's index and its index inside the block.

        Introduced in [G2. The SIMT execution model](../gpu/g2-simt.md#one-thread-one-loop-iteration).

    ??? question "What does it mean for an operation to be isolated from above?"

        Nothing inside its regions may use a value defined outside it. `func.func` is isolated, so a function body sees only its own arguments and the values it defines, and reaches other functions by symbol name.

        Introduced in [M2. Reading MLIR](m2-reading-mlir.md#regions-blocks-and-what-a-value-can-see).

    ??? question "What does a `memref<8xf32>` argument become when a function is lowered to the llvm dialect?"

        A five-field descriptor: the allocated pointer, the aligned pointer, an offset, and one size and one stride per dimension. The bare pointer convention passes a single pointer instead, which works only when every shape is static.

        Introduced in [M4. Dialect conversion and lowering to LLVM](m4-dialect-conversion.md#the-memref-descriptor-at-a-function-boundary).

    ??? question "What does `scf.forall` promise about its iterations, and who checks that promise?"

        That they are independent, so they may run in any order or at once. The verifier does not check it: the promise belongs to whoever wrote the IR.

        Introduced in [M6. Loops: affine and scf](m6-affine-and-scf.md#parallel-loops-that-the-writer-promises-scfparallel-and-scfforall).

    ??? question "What is a block's shared memory, and when may one thread read what another thread stored there?"

        An on-chip scratchpad that every thread of one block can read and write, for the kernel's run. A thread may read another thread's value only after both have passed a barrier, which no thread of the block passes until all have reached it.

        Introduced in [G3. The GPU memory hierarchy](../gpu/g3-memory-hierarchy.md#shared-memory-a-blocks-scratchpad).

!!! goals "In this chapter"

    - Read a `gpu.launch` and a `gpu.launch_func`, and explain what kernel outlining moves out of a host function and why it must.
    - Predict by hand the grid and block that the parallel-loop passes and a transform schedule produce, including which loop index runs along thread `x`.
    - Place a buffer in workgroup memory with a memory attribution, and follow it and `gpu.barrier` into the `nvvm`, `rocdl` and `spirv` dialects.
    - Explain what target attributes, `gpu-module-to-binary` and `gpu-to-llvm` add after the kernel is lowered, and which of those steps need an LLVM built with a GPU back end.
    - Recognize the `nvgpu` dialect as the bridge to NVIDIA's asynchronous copies and matrix instructions.

## One kernel, written inline

A GPU program has two halves. The **host** is the CPU side: it allocates buffers, chooses how many threads to start and waits for them. The **device** is the GPU, which runs the kernel. In CUDA the two halves share a source file and a compiler splits them ([G7](../gpu/g7-programming-models.md#same-source-or-two)). MLIR's **`gpu` dialect** lets both halves live in one module, at a level between the loops above and the vendor intrinsics below. Its documentation describes a programming model like CUDA's or OpenCL's, meant to hide device- and driver-specific details, and warns that the dialect changes more often than most.[^gpu] Every behaviour this chapter shows was checked with `mlir-opt` 18.1.8 on the owner's machine on 2026-09-24.

Start with a host function that brightens 256 pixel values, stored as `f32` between 0 and 1: add a bias, then cap the result at 1.0. The kernel is written inline, as the body of a `gpu.launch`:

--8<-- "includes/examples/mlir/m10-mlir-for-gpus/outline_launch.mlir.md"

Read the input first. **`gpu.launch`** is an operation whose one region is the kernel body. Its operands are the launch configuration: three grid sizes, here `(4, 1, 1)` blocks, then three block sizes, here `(64, 1, 1)` threads. A kernel that needs fewer than three dimensions sets the unused sizes to 1.[^gpu] Four blocks of 64 threads make 256 threads, one per pixel, and each thread computes its pixel's index as `%bx * 64 + %tx`, the formula of [G2](../gpu/g2-simt.md#one-thread-one-loop-iteration).

The names `%bx`, `%tx` and the others are the region's **block arguments** (M2's term, not GPU blocks). The region has twelve of them, in a fixed order: the block's index along `x`, `y` and `z`, the thread's index along `x`, `y` and `z`, then the grid and block sizes.[^gpu] The documentation gives the reason for handing them over as arguments: an analysis that asks which value is the thread's `x` index, for instance to judge whether loads are coalesced, can read the answer off the IR instead of reconstructing it.[^gpu]

`gpu.launch` is not isolated from above. Its body uses four values defined outside it: the arguments `%pixels` and `%bias`, and the constants `%c64` and `%one`. That is allowed here, and it is the reason the next step exists.

## Outlining: from a region to a function

A GPU cannot run a region inside a CPU function. The kernel has to become a separate function, in a separate module that a GPU compiler can take away and compile for a different machine. **Kernel outlining** is that move, and `--gpu-kernel-outlining` performs it.[^passes] The output above has four new pieces.

1. A **`gpu.module`** named `@brighten_kernel`: a module whose single block holds code meant for the device. It is a symbol table and it is isolated from above.[^gpu] Keeping device code in its own operation also lets a pass pipeline run passes on device code only, which later sections do.
2. A **`gpu.func`** marked `kernel`: a function that the host may launch. Its body "is executed by multiple work items", with no promise about their order, and they need not run in lockstep; ordering between them takes a barrier.[^gpu] Like `func.func`, it is isolated from above.
3. **`gpu.launch_func`** in the host function, in place of the launch. It names the kernel by a nested symbol, `@brighten_kernel::@brighten_kernel`: the module, then the function inside it.[^gpu]
4. `gpu.container_module` on the top-level module, a discardable attribute that `gpu.launch_func` requires on the module that holds its GPU modules.[^gpu]

The captured values are where outlining does real work. The kernel is isolated, so every value that the region used from outside must now arrive as an argument: `args(%c64 : index, %arg0 : memref<256xf32>, %arg1 : f32, %cst : f32)` on the launch, and four matching parameters on `gpu.func`. Figure 1 draws the move.

<figure class="vx-figure">
<svg viewBox="0 0 760 360" role="img" aria-label="Kernel outlining: a launch region that captures four outside values becomes a kernel function with four parameters" aria-describedby="m10-f1-desc">
<title id="m10-f1-title">What kernel outlining moves</title>
<desc id="m10-f1-desc">Two panels. Left, before outlining: the function brighten, with arguments pixels and bias, defines the constants c64 and one, then holds a gpu.launch region. The region's five lines compute the index from bx, c64 and tx, load from pixels, add bias, take the minimum with one, and store to pixels. A highlighted line under the region says that it reads four values from outside: c64, pixels, bias and one. An arrow labelled outline leads to the right panel. Right, after outlining: the function brighten now holds only gpu.launch_func naming brighten_kernel inside brighten_kernel, with blocks 4, 1, 1 and threads 64, 1, 1, and a highlighted argument list of the same four values. Below it, a gpu.module named brighten_kernel holds gpu.func brighten_kernel, whose four highlighted parameters are an index, a memref and two f32 values. A note says the function is isolated from above, so its body sees only those four. An animated arrow runs from the argument list to the parameters.</desc>
<text class="vx-text" x="20" y="24">before: one function, one region</text>
<rect class="vx-box-strong" x="20" y="36" width="340" height="304" rx="6"/>
<text class="vx-mono" x="34" y="60">func.func @brighten(%pixels, %bias)</text>
<text class="vx-mono" x="44" y="88">%c64 = arith.constant 64</text>
<text class="vx-mono" x="44" y="110">%one = arith.constant 1.0</text>
<rect class="vx-box" x="36" y="124" width="308" height="200" rx="4"/>
<text class="vx-mono" x="48" y="148">gpu.launch blocks(..) threads(..)</text>
<text class="vx-mono" x="60" y="176">%i = %bx * %c64 + %tx</text>
<text class="vx-mono" x="60" y="200">%v = load %pixels[%i]</text>
<text class="vx-mono" x="60" y="224">%lit = addf %v, %bias</text>
<text class="vx-mono" x="60" y="248">%capped = minimumf %lit, %one</text>
<text class="vx-mono" x="60" y="272">store %capped, %pixels[%i]</text>
<text class="vx-text-accent" x="48" y="306">reads 4 outside values: %c64,</text>
<text class="vx-text-accent" x="48" y="320">%pixels, %bias, %one</text>
<text class="vx-text-muted" x="362" y="180">outline</text>
<line class="vx-line" x1="362" y1="190" x2="392" y2="190"/>
<polygon class="vx-arrowhead" points="392,185 400,190 392,195"/>
<text class="vx-text" x="400" y="24">after --gpu-kernel-outlining</text>
<rect class="vx-box-strong" x="400" y="36" width="340" height="122" rx="6"/>
<text class="vx-mono" x="414" y="60">func.func @brighten(%pixels, %bias)</text>
<text class="vx-mono" x="424" y="86">gpu.launch_func @brighten_kernel::</text>
<text class="vx-mono" x="440" y="104">@brighten_kernel</text>
<text class="vx-mono" x="440" y="122">blocks (4,1,1) threads (64,1,1)</text>
<text class="vx-text-accent" x="424" y="146">args(%c64, %pixels, %bias, %one)</text>
<rect class="vx-box-strong" x="400" y="176" width="340" height="164" rx="6"/>
<text class="vx-mono" x="414" y="200">gpu.module @brighten_kernel</text>
<rect class="vx-box" x="414" y="212" width="300" height="116" rx="4"/>
<text class="vx-mono" x="426" y="236">gpu.func @brighten_kernel(</text>
<text class="vx-text-accent" x="436" y="256">index, memref&lt;256xf32&gt;,</text>
<text class="vx-text-accent" x="436" y="274">f32, f32) kernel</text>
<text class="vx-text-muted" x="426" y="300">isolated from above: the body</text>
<text class="vx-text-muted" x="426" y="316">sees only these four values</text>
<path class="vx-flow" d="M728,146 C756,180 756,230 720,258"/>
</svg>
<figcaption>Figure 1. Kernel outlining, drawn from <code>outline_launch.mlir</code> and its output. The launch region may read values defined around it; the kernel function may not, so each captured value becomes one entry in <code>args(...)</code> on the host side and one parameter of <code>gpu.func</code> on the device side, in the same order. Operation syntax is shortened.</figcaption>
</figure>

Three details in the output are worth reading slowly.

- The kernel begins with twelve operations, `gpu.block_id x` through `gpu.block_dim z`, that recreate the twelve block arguments the launch region had. Unused ones stay until a cleanup removes them. The documentation's own example of an outlined kernel shows the same injected operations.[^gpu]
- The original body now sits in a second block, `^bb1`, reached by `cf.br`. The launch region was spliced in as it was, and a canonicalization pass would merge the two blocks.
- `gpu.func` gained two discardable attributes, `gpu.known_block_size = array<i32: 64, 1, 1>` and `gpu.known_grid_size = array<i32: 4, 1, 1>`, because the launch sizes were constants. They are promises: launching the kernel with any other size "is undefined behavior", and in exchange an index such as `gpu.thread_id x` may be assumed smaller than 64.[^gpu]

That last point matters for a language like Vortex, where every array extent is known at compile time. The launch sizes follow from the shapes, and a compiler that records them gives later passes a bound on every thread index for free.

??? check "Run `--gpu-launch-sink-index-computations` before `--gpu-kernel-outlining` on the same file. Which kernel arguments disappear, and why is removing them safe?"

    `%c64` and `%one`. The sinking pass moves index computations into the launch body,[^passes] and here it copied both constants in, so outlining no longer sees them as captured and the kernel takes only `%pixels` and `%bias`: `args(%arg0 : memref<256xf32>, %arg1 : f32)` (checked on 2026-09-24). A constant has no run-time input, so recomputing it on the device gives the same value as passing it.

<figure class="vx-figure">
<svg viewBox="0 0 760 290" role="img" aria-label="The path from parallel loops to GPU binaries, with the steps this machine can run marked solid" aria-describedby="m10-f2-desc">
<title id="m10-f2-title">From loops to a GPU binary, and where this Mac stops</title>
<desc id="m10-f2-desc">Two rows of boxes joined by numbered arrows. Top row, device code: parallel loops, scf.parallel or scf.forall; arrow 1 to gpu.launch, the inline kernel; arrow 2 to gpu.module holding gpu.func kernel; arrow 3 fans out to three boxes, the nvvm, rocdl and spirv dialects; arrow 4 leads from nvvm and rocdl to a dashed box, gpu.binary with an object per target, and from spirv to a solid box, SPIR-V words. Bottom row, host code: arrow 2 also leads from gpu.launch down to func.func holding gpu.launch_func; arrow 5 leads to a dashed box, the llvm dialect with runtime calls; arrow 6 leads to a dashed box, LLVM IR with the binary embedded. Solid boxes are steps that ran with mlir-opt 18.1.8 on this machine; dashed boxes need an LLVM built with the NVPTX or AMDGPU target.</desc>
<text class="vx-text-muted" x="10" y="20">device code</text>
<rect class="vx-box" x="10" y="60" width="130" height="60" rx="4"/>
<text class="vx-mono" x="75" y="86" text-anchor="middle">scf.parallel</text>
<text class="vx-mono" x="75" y="106" text-anchor="middle">scf.forall</text>
<line class="vx-line" x1="140" y1="90" x2="158" y2="90"/>
<polygon class="vx-arrowhead" points="158,85 165,90 158,95"/>
<text class="vx-text-accent" x="149" y="82" text-anchor="middle">1</text>
<rect class="vx-box" x="165" y="60" width="130" height="60" rx="4"/>
<text class="vx-mono" x="230" y="86" text-anchor="middle">gpu.launch</text>
<text class="vx-text-muted" x="230" y="106" text-anchor="middle">inline kernel</text>
<line class="vx-line" x1="295" y1="90" x2="313" y2="90"/>
<polygon class="vx-arrowhead" points="313,85 320,90 313,95"/>
<text class="vx-text-accent" x="305" y="82" text-anchor="middle">2</text>
<rect class="vx-box" x="320" y="60" width="130" height="60" rx="4"/>
<text class="vx-mono" x="385" y="86" text-anchor="middle">gpu.module</text>
<text class="vx-mono" x="385" y="106" text-anchor="middle">gpu.func kernel</text>
<text class="vx-text-accent" x="454" y="112" text-anchor="middle">3</text>
<path class="vx-line" d="M450,90 L462,90 L462,47 L468,47"/>
<path class="vx-line" d="M462,90 L468,90"/>
<path class="vx-line" d="M462,90 L462,133 L468,133"/>
<polygon class="vx-arrowhead" points="468,42 475,47 468,52"/>
<polygon class="vx-arrowhead" points="468,85 475,90 468,95"/>
<polygon class="vx-arrowhead" points="468,128 475,133 468,138"/>
<rect class="vx-box" x="475" y="30" width="110" height="34" rx="4"/>
<text class="vx-mono" x="530" y="52" text-anchor="middle">nvvm</text>
<rect class="vx-box" x="475" y="73" width="110" height="34" rx="4"/>
<text class="vx-mono" x="530" y="95" text-anchor="middle">rocdl</text>
<rect class="vx-box" x="475" y="116" width="110" height="34" rx="4"/>
<text class="vx-mono" x="530" y="138" text-anchor="middle">spirv</text>
<text class="vx-text-accent" x="600" y="40" text-anchor="middle">4</text>
<line class="vx-line" x1="585" y1="47" x2="608" y2="55"/>
<line class="vx-line" x1="585" y1="90" x2="608" y2="80"/>
<polygon class="vx-arrowhead" points="606,50 615,58 604,60"/>
<polygon class="vx-arrowhead" points="604,75 615,77 606,85"/>
<rect class="vx-box-bad" x="615" y="30" width="135" height="77" rx="4"/>
<text class="vx-mono" x="682" y="62" text-anchor="middle">gpu.binary</text>
<text class="vx-text-muted" x="682" y="82" text-anchor="middle">an object per target</text>
<line class="vx-line" x1="585" y1="133" x2="608" y2="133"/>
<polygon class="vx-arrowhead" points="608,128 615,133 608,138"/>
<rect class="vx-box" x="615" y="116" width="135" height="34" rx="4"/>
<text class="vx-mono" x="682" y="138" text-anchor="middle">SPIR-V words</text>
<text class="vx-text-muted" x="10" y="190">host code</text>
<path class="vx-line" d="M230,120 L230,230 L313,230"/>
<polygon class="vx-arrowhead" points="313,225 320,230 313,235"/>
<text class="vx-text-accent" x="240" y="222">2</text>
<rect class="vx-box" x="320" y="200" width="130" height="60" rx="4"/>
<text class="vx-mono" x="385" y="226" text-anchor="middle">func.func</text>
<text class="vx-mono" x="385" y="246" text-anchor="middle">gpu.launch_func</text>
<line class="vx-line" x1="450" y1="230" x2="468" y2="230"/>
<polygon class="vx-arrowhead" points="468,225 475,230 468,235"/>
<text class="vx-text-accent" x="459" y="222" text-anchor="middle">5</text>
<rect class="vx-box-bad" x="475" y="200" width="110" height="60" rx="4"/>
<text class="vx-mono" x="530" y="226" text-anchor="middle">llvm</text>
<text class="vx-text-muted" x="530" y="246" text-anchor="middle">runtime calls</text>
<line class="vx-line" x1="585" y1="230" x2="608" y2="230"/>
<polygon class="vx-arrowhead" points="608,225 615,230 608,235"/>
<text class="vx-text-accent" x="598" y="222" text-anchor="middle">6</text>
<rect class="vx-box-bad" x="615" y="200" width="135" height="60" rx="4"/>
<text class="vx-mono" x="682" y="226" text-anchor="middle">LLVM IR</text>
<text class="vx-text-muted" x="682" y="246" text-anchor="middle">binary embedded</text>
</svg>
<figcaption>Figure 2. The road this chapter follows. 1: <code>--gpu-map-parallel-loops</code> and <code>--convert-parallel-loops-to-gpu</code>, or a transform schedule. 2: <code>--gpu-kernel-outlining</code>, which produces both the device module and the host launch. 3: <code>--convert-gpu-to-nvvm</code>, <code>-rocdl</code> or <code>-spirv</code>. 4: a target attribute and <code>--gpu-module-to-binary</code>, or <code>mlir-translate --serialize-spirv</code>. 5: <code>--gpu-to-llvm</code>. 6: <code>mlir-translate --mlir-to-llvmir</code>. Solid boxes ran with <code>mlir-opt</code> 18.1.8 on the owner's Mac; dashed boxes need an LLVM built with the NVPTX or AMDGPU target.</figcaption>
</figure>

Figure 2 is the map for the rest of the chapter. The next two sections cover step 1, where the launch comes from; then come memory, the three targets of step 3, and the steps after.

## From parallel loops to a launch, predicted by hand

Nobody writes `gpu.launch` by hand for every kernel. A compiler starts from loops whose iterations are known to be independent and decides how to spread them over blocks and threads. The oldest route in MLIR is a pass pipeline over `scf.parallel`, the parallel loop of [M6](m6-affine-and-scf.md#parallel-loops-that-the-writer-promises-scfparallel-and-scfforall). Here it is on a blend of two 8 × 16 images, `out[row, col] = a[row, col] + b[row, col]`:

--8<-- "includes/examples/mlir/m10-mlir-for-gpus/parallel_to_launch.mlir.md"

Before reading the output, predict it. The pipeline has three steps, and each can be done with pencil and paper.

1. **Tile.** `--scf-parallel-loop-tiling` with sizes 4 and 8 splits the one loop over 8 × 16 points into two nested parallel loops.[^passes] The outer **tile loop** steps over tile origins: rows 0 and 4, columns 0 and 8, which is 2 × 2 = 4 tiles. The inner **point loop** walks the 4 × 8 points inside one tile.
2. **Map.** `--gpu-map-parallel-loops` attaches a **mapping attribute** to each loop, a record of which hardware index will run it. It is greedy: the first parallel loop it meets goes to blocks, the second to threads, and within each loop the first dimension goes to `x`, the second to `y`, the third to `z`, and any more stay sequential.[^passes]
3. **Convert.** `--convert-parallel-loops-to-gpu` turns the mapped loops into a `gpu.launch`, sized by the loops' trip counts; outlining follows.[^passes]

So the tile loop's dimensions become the grid: rows to block `x` with 8 / 4 = 2 blocks, columns to block `y` with 16 / 8 = 2 blocks. The point loop's dimensions become the block: rows to thread `x` with 4 threads, columns to thread `y` with 8. The prediction is `blocks in (2, 2, 1) threads in (4, 8, 1)`, and the output agrees.

Now read the kernel against the prediction. `%4 = affine.apply #map(%0)[%arg0, %arg1]` computes `block_id x * 4 + 0`, the first row of the block's tile, and `%6` computes `thread_id x * 1 + 0`, the offset inside it. The row is `%8 = %6 + %4`, built from the `x` indices; the column `%9` comes from the `y` indices. The five `index` parameters before the memrefs are the loops' steps and lower bounds, captured and passed in as in Figure 1.

Without the tiling step, the same pipeline has only one parallel loop to map, so it maps it to blocks and has nothing left for threads: `blocks in (8, 16, 1) threads in (1, 1, 1)`, 128 blocks of one thread each (checked on 2026-09-24). The pass does what its name says. Whether a block of one thread is a good idea is a question it does not ask, and [M12](m12-vortex-gpu-path.md) shows the same result on a matrix multiply.

A tile size that does not divide the extent needs care. With sizes 3 and 8, the last row tile holds only 2 rows, so tiling gives the point loop a bound computed with `affine.min`. In 18.1.8, `--convert-parallel-loops-to-gpu` then left both loops untouched, with no launch and no diagnostic (checked on 2026-09-24). The tiling option `no-min-max-bounds` asks for a fixed inner bound with an in-bounds check instead,[^passes] and with it the launch appears as `blocks in (3, 2, 1) threads in (3, 8, 1)`, with an `scf.if` around the body: the boundary guard of [G2](../gpu/g2-simt.md#bringing-this-back-to-vortex-fixed-shapes-and-the-boundary-guard), generated.

??? check "Run the same pipeline with tile sizes 2 and 16. What launch do you predict, and which block and thread compute `out[5, 9]`?"

    Rows: 8 / 2 = 4 blocks along `x`, 2 threads along `x`. Columns: 16 / 16 = 1 block along `y`, 16 threads along `y`. So `blocks in (4, 1, 1) threads in (2, 16, 1)`, which `mlir-opt` 18.1.8 prints. Row 5 lies in the tile starting at row 4, block `x` = 2, at offset 1, thread `x` = 1. Column 9 lies in the only column tile, block `y` = 0, at thread `y` = 9.

## Which index runs along thread x

The prediction came true, but look at which index landed where. The greedy mapping put `row`, the first dimension, on thread `x`. [G4](../gpu/g4-memory-performance.md#which-index-runs-across-the-warp) explains why that choice matters: NVIDIA's hardware numbers a block's threads with `x` varying fastest and cuts that line into warps of 32 consecutive ids,[^cuda-simt][^cuda-adv] and a warp's loads are cheapest when its lanes touch neighbouring addresses. In a row-major array, neighbouring addresses differ in the last index, `col`.

MLIR offers a second route that lets the compiler writer name the mapping instead of accepting a pass's habit. A **transform schedule** ([M9](m9-transform-dialect.md#a-payload-file-and-a-schedule-file-in-one-module)) tiles the same blend, written as one `linalg.add`, into two nested `scf.forall` loops and labels their dimensions with mapping attributes such as `#gpu.block<y>` and `#gpu.thread<x>`:

--8<-- "includes/examples/mlir/m10-mlir-for-gpus/forall_to_launch.mlir.md"

`transform.gpu.map_forall_to_blocks` with `generate_gpu_launch` wraps the function's top-level `scf.forall` in a new `gpu.launch` and replaces its induction variables with block ids, following the mapping attribute. `transform.gpu.map_nested_forall_to_threads` then does the same for the inner loop with thread ids, and sets the block size from its mandatory `block_dims`.[^transform] The launch is `blocks (2, 2, 1)` and `threads (8, 4, 1)`: columns on `x`, as asked. Both operations accept only `scf.forall` loops over buffers, with static trip counts and at most three dimensions.[^transform]

Two details in the output come from the operation's definition. The `gpu.barrier` at the end of the body comes from its `sync_after_distribute` setting, which controls whether a barrier follows each distributed loop and which inserted one here without being asked.[^transform] And `block_dims` need not equal the loop's size. If the block has more threads than the loop has iterations, the extra threads are **predicated**, skipped by a guard: with `block_dims = [16, 4, 1]` the body sits inside `scf.if` on `thread_id x < 8`.[^transform] With fewer threads than iterations, `[4, 4, 1]`, the transform fails, reporting that 32 required threads overflow the 16 available (both checked on 2026-09-24).

Figure 3 puts the two mappings side by side on one 4 × 8 tile.

<figure class="vx-figure">
<svg viewBox="0 0 760 270" role="img" aria-label="Lane numbers on one 4 by 8 tile under the two mappings" aria-describedby="m10-f3-desc">
<title id="m10-f3-title">Lane order under two mappings</title>
<desc id="m10-f3-desc">Two grids of 4 rows and 8 columns, each cell showing the lane number of the thread that handles that element, where the lane number is thread x plus the block width times thread y. Left, the pass route with row on thread x: lane equals row plus 4 times column, so column 0 holds lanes 0, 1, 2, 3 from top to bottom, column 1 holds 4 to 7, and so on to column 7 with 28 to 31. Lanes 0 to 3 are highlighted down column 0; the note says they are 16 floats apart in memory. Right, the schedule with column on thread x: lane equals column plus 8 times row, so row 0 holds lanes 0 to 7 from left to right and row 3 holds 24 to 31. Lanes 0 to 3 are highlighted along row 0; the note says their addresses are consecutive.</desc>
<text class="vx-text" x="56" y="24">passes: row on thread x</text>
<text class="vx-text-muted" x="56" y="44">lane = row + 4 × col</text>
<text class="vx-text" x="436" y="24">schedule: col on thread x</text>
<text class="vx-text-muted" x="436" y="44">lane = col + 8 × row</text>
<g>
<rect class="vx-cell-on" x="56" y="60" width="38" height="34"/><text class="vx-mono" x="75" y="82" text-anchor="middle">0</text>
<rect class="vx-box" x="94" y="60" width="38" height="34"/><text class="vx-mono" x="113" y="82" text-anchor="middle">4</text>
<rect class="vx-box" x="132" y="60" width="38" height="34"/><text class="vx-mono" x="151" y="82" text-anchor="middle">8</text>
<rect class="vx-box" x="170" y="60" width="38" height="34"/><text class="vx-mono" x="189" y="82" text-anchor="middle">12</text>
<rect class="vx-box" x="208" y="60" width="38" height="34"/><text class="vx-mono" x="227" y="82" text-anchor="middle">16</text>
<rect class="vx-box" x="246" y="60" width="38" height="34"/><text class="vx-mono" x="265" y="82" text-anchor="middle">20</text>
<rect class="vx-box" x="284" y="60" width="38" height="34"/><text class="vx-mono" x="303" y="82" text-anchor="middle">24</text>
<rect class="vx-box" x="322" y="60" width="38" height="34"/><text class="vx-mono" x="341" y="82" text-anchor="middle">28</text>
<rect class="vx-cell-on" x="56" y="94" width="38" height="34"/><text class="vx-mono" x="75" y="116" text-anchor="middle">1</text>
<rect class="vx-box" x="94" y="94" width="38" height="34"/><text class="vx-mono" x="113" y="116" text-anchor="middle">5</text>
<rect class="vx-box" x="132" y="94" width="38" height="34"/><text class="vx-mono" x="151" y="116" text-anchor="middle">9</text>
<rect class="vx-box" x="170" y="94" width="38" height="34"/><text class="vx-mono" x="189" y="116" text-anchor="middle">13</text>
<rect class="vx-box" x="208" y="94" width="38" height="34"/><text class="vx-mono" x="227" y="116" text-anchor="middle">17</text>
<rect class="vx-box" x="246" y="94" width="38" height="34"/><text class="vx-mono" x="265" y="116" text-anchor="middle">21</text>
<rect class="vx-box" x="284" y="94" width="38" height="34"/><text class="vx-mono" x="303" y="116" text-anchor="middle">25</text>
<rect class="vx-box" x="322" y="94" width="38" height="34"/><text class="vx-mono" x="341" y="116" text-anchor="middle">29</text>
<rect class="vx-cell-on" x="56" y="128" width="38" height="34"/><text class="vx-mono" x="75" y="150" text-anchor="middle">2</text>
<rect class="vx-box" x="94" y="128" width="38" height="34"/><text class="vx-mono" x="113" y="150" text-anchor="middle">6</text>
<rect class="vx-box" x="132" y="128" width="38" height="34"/><text class="vx-mono" x="151" y="150" text-anchor="middle">10</text>
<rect class="vx-box" x="170" y="128" width="38" height="34"/><text class="vx-mono" x="189" y="150" text-anchor="middle">14</text>
<rect class="vx-box" x="208" y="128" width="38" height="34"/><text class="vx-mono" x="227" y="150" text-anchor="middle">18</text>
<rect class="vx-box" x="246" y="128" width="38" height="34"/><text class="vx-mono" x="265" y="150" text-anchor="middle">22</text>
<rect class="vx-box" x="284" y="128" width="38" height="34"/><text class="vx-mono" x="303" y="150" text-anchor="middle">26</text>
<rect class="vx-box" x="322" y="128" width="38" height="34"/><text class="vx-mono" x="341" y="150" text-anchor="middle">30</text>
<rect class="vx-cell-on" x="56" y="162" width="38" height="34"/><text class="vx-mono" x="75" y="184" text-anchor="middle">3</text>
<rect class="vx-box" x="94" y="162" width="38" height="34"/><text class="vx-mono" x="113" y="184" text-anchor="middle">7</text>
<rect class="vx-box" x="132" y="162" width="38" height="34"/><text class="vx-mono" x="151" y="184" text-anchor="middle">11</text>
<rect class="vx-box" x="170" y="162" width="38" height="34"/><text class="vx-mono" x="189" y="184" text-anchor="middle">15</text>
<rect class="vx-box" x="208" y="162" width="38" height="34"/><text class="vx-mono" x="227" y="184" text-anchor="middle">19</text>
<rect class="vx-box" x="246" y="162" width="38" height="34"/><text class="vx-mono" x="265" y="184" text-anchor="middle">23</text>
<rect class="vx-box" x="284" y="162" width="38" height="34"/><text class="vx-mono" x="303" y="184" text-anchor="middle">27</text>
<rect class="vx-box" x="322" y="162" width="38" height="34"/><text class="vx-mono" x="341" y="184" text-anchor="middle">31</text>
</g>
<g>
<rect class="vx-cell-on" x="436" y="60" width="38" height="34"/><text class="vx-mono" x="455" y="82" text-anchor="middle">0</text>
<rect class="vx-cell-on" x="474" y="60" width="38" height="34"/><text class="vx-mono" x="493" y="82" text-anchor="middle">1</text>
<rect class="vx-cell-on" x="512" y="60" width="38" height="34"/><text class="vx-mono" x="531" y="82" text-anchor="middle">2</text>
<rect class="vx-cell-on" x="550" y="60" width="38" height="34"/><text class="vx-mono" x="569" y="82" text-anchor="middle">3</text>
<rect class="vx-box" x="588" y="60" width="38" height="34"/><text class="vx-mono" x="607" y="82" text-anchor="middle">4</text>
<rect class="vx-box" x="626" y="60" width="38" height="34"/><text class="vx-mono" x="645" y="82" text-anchor="middle">5</text>
<rect class="vx-box" x="664" y="60" width="38" height="34"/><text class="vx-mono" x="683" y="82" text-anchor="middle">6</text>
<rect class="vx-box" x="702" y="60" width="38" height="34"/><text class="vx-mono" x="721" y="82" text-anchor="middle">7</text>
<rect class="vx-box" x="436" y="94" width="38" height="34"/><text class="vx-mono" x="455" y="116" text-anchor="middle">8</text>
<rect class="vx-box" x="474" y="94" width="38" height="34"/><text class="vx-mono" x="493" y="116" text-anchor="middle">9</text>
<rect class="vx-box" x="512" y="94" width="38" height="34"/><text class="vx-mono" x="531" y="116" text-anchor="middle">10</text>
<rect class="vx-box" x="550" y="94" width="38" height="34"/><text class="vx-mono" x="569" y="116" text-anchor="middle">11</text>
<rect class="vx-box" x="588" y="94" width="38" height="34"/><text class="vx-mono" x="607" y="116" text-anchor="middle">12</text>
<rect class="vx-box" x="626" y="94" width="38" height="34"/><text class="vx-mono" x="645" y="116" text-anchor="middle">13</text>
<rect class="vx-box" x="664" y="94" width="38" height="34"/><text class="vx-mono" x="683" y="116" text-anchor="middle">14</text>
<rect class="vx-box" x="702" y="94" width="38" height="34"/><text class="vx-mono" x="721" y="116" text-anchor="middle">15</text>
<rect class="vx-box" x="436" y="128" width="38" height="34"/><text class="vx-mono" x="455" y="150" text-anchor="middle">16</text>
<rect class="vx-box" x="474" y="128" width="38" height="34"/><text class="vx-mono" x="493" y="150" text-anchor="middle">17</text>
<rect class="vx-box" x="512" y="128" width="38" height="34"/><text class="vx-mono" x="531" y="150" text-anchor="middle">18</text>
<rect class="vx-box" x="550" y="128" width="38" height="34"/><text class="vx-mono" x="569" y="150" text-anchor="middle">19</text>
<rect class="vx-box" x="588" y="128" width="38" height="34"/><text class="vx-mono" x="607" y="150" text-anchor="middle">20</text>
<rect class="vx-box" x="626" y="128" width="38" height="34"/><text class="vx-mono" x="645" y="150" text-anchor="middle">21</text>
<rect class="vx-box" x="664" y="128" width="38" height="34"/><text class="vx-mono" x="683" y="150" text-anchor="middle">22</text>
<rect class="vx-box" x="702" y="128" width="38" height="34"/><text class="vx-mono" x="721" y="150" text-anchor="middle">23</text>
<rect class="vx-box" x="436" y="162" width="38" height="34"/><text class="vx-mono" x="455" y="184" text-anchor="middle">24</text>
<rect class="vx-box" x="474" y="162" width="38" height="34"/><text class="vx-mono" x="493" y="184" text-anchor="middle">25</text>
<rect class="vx-box" x="512" y="162" width="38" height="34"/><text class="vx-mono" x="531" y="184" text-anchor="middle">26</text>
<rect class="vx-box" x="550" y="162" width="38" height="34"/><text class="vx-mono" x="569" y="184" text-anchor="middle">27</text>
<rect class="vx-box" x="588" y="162" width="38" height="34"/><text class="vx-mono" x="607" y="184" text-anchor="middle">28</text>
<rect class="vx-box" x="626" y="162" width="38" height="34"/><text class="vx-mono" x="645" y="184" text-anchor="middle">29</text>
<rect class="vx-box" x="664" y="162" width="38" height="34"/><text class="vx-mono" x="683" y="184" text-anchor="middle">30</text>
<rect class="vx-box" x="702" y="162" width="38" height="34"/><text class="vx-mono" x="721" y="184" text-anchor="middle">31</text>
</g>
<text class="vx-text-muted" x="50" y="82" text-anchor="end">r0</text>
<text class="vx-text-muted" x="50" y="116" text-anchor="end">r1</text>
<text class="vx-text-muted" x="50" y="150" text-anchor="end">r2</text>
<text class="vx-text-muted" x="50" y="184" text-anchor="end">r3</text>
<text class="vx-text-muted" x="430" y="82" text-anchor="end">r0</text>
<text class="vx-text-muted" x="430" y="116" text-anchor="end">r1</text>
<text class="vx-text-muted" x="430" y="150" text-anchor="end">r2</text>
<text class="vx-text-muted" x="430" y="184" text-anchor="end">r3</text>
<text class="vx-text-accent" x="56" y="226">lanes 0 to 3 walk down column 0:</text>
<text class="vx-text-accent" x="56" y="244">addresses 16 floats apart</text>
<text class="vx-text-accent" x="436" y="226">lanes 0 to 3 walk along row 0:</text>
<text class="vx-text-accent" x="436" y="244">consecutive addresses</text>
</svg>
<figcaption>Figure 3. One 4 × 8 tile of the 8 × 16 blend, with each element labelled by the lane of the thread that handles it. The lane number is the thread's linear id in its block, <code>x</code> plus block width times <code>y</code>. Left: the pass pipeline's greedy mapping. Right: the transform schedule's explicit mapping. Here each block is one warp of 32, so both warps touch the same 32 elements; the order differs, and with wider blocks so does the set.</figcaption>
</figure>

In this tiny example the difference is order only, as the caption says, because a block of 32 threads is a single warp. Scale the blocks up and it becomes a difference in what each warp touches. That is the next check.

MLIR 18.1.8 gives the pass route no option to change its habit. The pass documentation now describes a `mapping-policy` option that sends the innermost loop to `x`,[^passes] and 18.1.8 rejects it as unknown (checked on 2026-09-24). With 18.1.8 you control the order by writing it: list `col` first in the `scf.parallel`, and `col` lands on thread `x`.

??? check "A 64 × 64 blend goes through the pass route with tile sizes 8 and 32, `row` listed first. Which elements do the 32 lanes of warp 0 touch? What change gives warp 0 one contiguous run instead?"

    The block is `threads (8, 32, 1)`, 256 threads, with `row` on `x`. Warp 0 holds linear ids 0 to 31, so `x` runs 0 to 7 and `y` runs 0 to 3: rows 0 to 7, columns 0 to 3, which is eight separate pieces of 4 floats (16 bytes each), one per row. List `col` first and tile by 32 and 8: then `x` runs over 32 columns, and warp 0 reads row 0, columns 0 to 31, one run of 128 bytes. The CUDA guide counts such accesses in 32-byte transactions: the run needs four, while the eight pieces need eight, each half used.[^cuda-simt] A transform schedule that maps `col` to `#gpu.thread<x>` gives the same.

## Memory the block shares

The blend reads each input once, so its threads never need each other. Many kernels do: a thread loads a value that a neighbour will use, and the block keeps it in its scratchpad ([G3](../gpu/g3-memory-hierarchy.md#shared-memory-a-blocks-scratchpad)). The `gpu` dialect writes the level of memory into the memref's type, as an **address space** attribute with four values: `global`, `workgroup`, `private` and `constant`.[^gpu] **Workgroup memory** is shared by all threads of one block. **Private memory** belongs to one thread. **Constant memory** is read-only for the whole run. [G3](../gpu/g3-memory-hierarchy.md#address-spaces-the-level-in-the-type) tabulates what CUDA, Metal and LLVM call each one.

A kernel gets workgroup memory through a **memory attribution**: a buffer declared on the `gpu.func` (or `gpu.launch`) itself, with `workgroup(...)` or `private(...)`, that appears in the body as an extra block argument after the function's own arguments.[^gpu] The buffer lives exactly as long as one run of the kernel. The documentation explains the choice: the underlying models declare such buffers at module level, but tying them to the function makes their lifetime visible and discourages using them to pass data between launches, which should go through arguments.[^gpu]

This kernel reverses each block's 64 values in place. Each thread copies one value into the tile, all threads wait, and each reads back its mirror image:

--8<-- "includes/examples/mlir/m10-mlir-for-gpus/workgroup_nvvm.mlir.md"

**`gpu.barrier`** makes the wait. It holds each thread until every thread of the workgroup has reached it, and makes the memory accesses they made before it visible to all of them.[^gpu] Without it, thread 0 could read slot 63 before thread 63 had written it. The documentation adds a rule with teeth: once one thread of the workgroup has executed a particular dynamic instance of the barrier, every other thread must execute that same instance.[^gpu] A barrier inside a branch that only some threads take breaks the rule; [G6](../gpu/g6-synchronization.md#convergence-why-these-operations-cannot-sit-behind-a-split) explains why.

`--convert-gpu-to-nvvm` lowers the kernel to the **`nvvm` dialect**, MLIR's LLVM-based dialect for NVIDIA GPUs, whose operations mostly map one to one onto NVVM intrinsics of LLVM IR.[^nvvm] Walk through the output:

- The attribution became `llvm.mlir.global internal @__wg_reverse_tiles_0() {addr_space = 3 : i32}`: a 64-float global in address space 3, which the NVPTX back end reserves for shared memory.[^nvptx] Every access to it goes through a `!llvm.ptr<3>`.
- `gpu.block_id x` and `gpu.thread_id x` became `nvvm.read.ptx.sreg.ctaid.x` and `nvvm.read.ptx.sreg.tid.x`, reads of PTX special registers, each followed by `llvm.sext` from `i32` to `i64`. The pass lowers `index` to the machine word unless its `index-bitwidth` option says otherwise.[^passes]
- `gpu.barrier` became `nvvm.barrier0`.
- The function became an `llvm.func` with the `nvvm.kernel` attribute, the mark that makes it launchable. In LLVM IR, the NVPTX user guide now marks kernels with the `ptx_kernel` calling convention.[^nvptx]
- `%data` arrived as one `!llvm.ptr` because the example sets `use-bare-ptr-memref-call-conv`, which the pass allows only when every memref has a static shape.[^passes] Without it, each memref argument would become M4's five-field descriptor.

## One kernel, three targets

Run the same kernel through `--convert-gpu-to-rocdl` instead, and compare the two outputs line by line. They differ in four lines (checked on 2026-09-24):

| Line in the kernel | `nvvm` output | `rocdl` output |
| --- | --- | --- |
| kernel mark | `nvvm.kernel` | `rocdl.kernel` |
| block index | `nvvm.read.ptx.sreg.ctaid.x` | `rocdl.workgroup.id.x` |
| thread index | `nvvm.read.ptx.sreg.tid.x` | `rocdl.workitem.id.x` |
| barrier | `nvvm.barrier0` | `rocdl.barrier` |

Everything else, the loads, stores, address arithmetic and the global in address space 3, is identical `llvm` dialect code. AMDGPU happens to number its workgroup scratchpad, the local data share, 3 as well.[^amdgpu] The **`rocdl` dialect** holds one-to-one wrappers around the AMDGPU back end's intrinsics, by its own rule.[^rocdl] This is [M4](m4-dialect-conversion.md)'s conversion framework at work: one set of patterns converts the shared dialects to `llvm`, and a small target-specific set converts the `gpu` operations.

The third target is different in kind. **SPIR-V** is the Khronos Group's binary intermediate language for shaders and compute kernels, used by Vulkan and OpenCL.[^spirv] It is not LLVM IR, and MLIR models it with its own **`spirv` dialect**, designed to stay at SPIR-V's semantic level so that it serializes to the binary format and back directly.[^spirv] Here is a kernel that halves 250 values with 256 threads, so every thread checks its index first:

--8<-- "includes/examples/mlir/m10-mlir-for-gpus/guard_spirv.mlir.md"

SPIR-V wants facts that the `gpu` dialect leaves open, and the input has to state them. `spirv.target_env` lists the SPIR-V version, **capabilities** (optional feature sets a module may declare it uses, such as `Shader`) and extensions the target accepts. `spirv.entry_point_abi` fixes the workgroup size on the kernel. `#spirv.storage_class<StorageBuffer>` says where the buffer lives. Remove the entry point attribute and the kernel is not converted, with a remark naming the missing attribute; remove the target environment and conversion fails at the first `memref.load` (both checked on 2026-09-24).

Four things changed in the output.

- **Arguments became bindings.** A SPIR-V entry point cannot take parameters. By default the pass turns each `gpu.func` argument into a resource numbered in order, in descriptor set 0.[^passes] `spirv.interface_var_abi<(0, 0)>` on the argument is that record: set 0, binding 0, the slot the host binds the buffer to.
- **Indices became built-in variables.** `gpu.block_id x` became a load from `WorkgroupId` and `gpu.thread_id x` a load from `LocalInvocationId`, each a vector of three `i32` from which `CompositeExtract` takes element 0.
- **The branch became a region.** SPIR-V requires **structured control flow**: every branch declares where its paths meet. The dialect writes that as a `spirv.mlir.selection` region whose first block branches and whose last block, holding only `spirv.mlir.merge`, is where the paths meet.[^spirv] [G8](../gpu/g8-isas-and-irs.md#when-branches-must-declare-where-they-meet) reads the same rule in the binary.
- **The requirements shrank.** `--spirv-update-vce` computed the smallest version, capabilities and extensions the module needs, from the limits in the target environment.[^passes] The target allows version 1.3; the module declares `v1.0`.

`mlir-translate --serialize-spirv` turns the `spirv.module` into a SPIR-V binary, and `--deserialize-spirv` reads it back into the dialect (checked on 2026-09-24). Not everything converted. A `gpu.barrier` in a similar kernel became `spirv.ControlBarrier <Workgroup>, <Workgroup>, <AcquireRelease|WorkgroupMemory>`, but the reversing kernel's workgroup attribution did not convert: one spelling of its memory space crashed `mlir-opt` 18.1.8 and another failed to legalize the function (checked on 2026-09-24). On this version, the NVVM and ROCDL paths are the more complete.

??? check "In `guard_spirv.expected`, why does `@halve` take `!spirv.ptr<!spirv.struct<(!spirv.array<250 x f32, stride=4> [0])>, StorageBuffer>` rather than something like a memref, and what tells the host where to bind it?"

    SPIR-V has no memref and no descriptor: a buffer is a pointer, in a storage class, to a struct wrapping an array whose layout is spelled out (stride 4 bytes, member offset 0). The entry point takes no ordinary parameters, so the argument carries `spirv.interface_var_abi<(0, 0)>`: descriptor set 0, binding 0, the resource slot the host must bind the buffer to.

## From a module to a binary

Everything so far rewrote MLIR into MLIR, which `mlir-opt` can do on any machine. The next steps call real GPU back ends.

A **target attribute** on a `gpu.module` says what to compile it for. `--nvvm-attach-target` attaches `#nvvm.target`, and `--rocdl-attach-target` attaches `#rocdl.target`, each with options such as the chip, the optimization level and the features.[^passes] A module may carry several. Then `--gpu-module-to-binary` **serializes** each GPU module with each of its targets and replaces it with a **`gpu.binary`** operation holding one object per target; its `format` option chooses an offloading representation, assembly, a binary or a fat binary.[^passes][^gpu]

On the host side, `--gpu-to-llvm` converts `gpu.launch_func` and the other host operations. It does not call the CUDA or ROCm APIs directly: it calls a small wrapper library that gives those runtimes one stable interface.[^passes] At translation to LLVM IR, the binary becomes a constant in the host program, loaded at start-up, and each launch becomes a lookup and a call through the wrapper, which the documentation's example shows as `mgpuModuleLoad` and `mgpuLaunchKernel`.[^gpu] The documentation's full example pipeline runs outlining, target attachment, `convert-gpu-to-nvvm` on the GPU modules, `gpu-to-llvm` and `gpu-module-to-binary`, then `mlir-translate`.[^gpu]

On the owner's Mac the path stops at the dashed boxes of Figure 2. The LLVM on the path, 18.1.8, registers only AArch64 targets, and `--gpu-module-to-binary` after an NVVM target is attached fails with the message "The `NVPTX` target was not built" (checked on 2026-09-24). In 18.1.8, `--gpu-to-llvm` refused the launch while its kernel was still a `gpu.module` rather than a `gpu.binary`, so the host half cannot be finished either (checked on 2026-09-24). MLIR's Getting Started page shows the fix, a build with `-DLLVM_TARGETS_TO_BUILD="Native;NVPTX;AMDGPU"`.[^start] The documentation also describes `gpu-lower-to-nvvm-pipeline`, a ready-made NVVM pipeline that expects explicitly parallel IR and does no parallelization of its own;[^gpu] this 18.1.8 build does not register it (checked on 2026-09-24).

Two lessons for a Vortex compiler sit in these options. The first is that decision 56's promise has to survive into the device code. `arith.addf` with `fastmath<none>` in the brighten kernel became `llvm.fadd` with `fastmathFlags = #llvm.fastmath<none>` under `--convert-gpu-to-nvvm` (checked on 2026-09-24), so nothing is lost on the way down. The target attributes are another matter: `nvvm-attach-target` has `fast` and `ftz` (flush subnormal values to zero), and `rocdl-attach-target` has `fast`, `daz`, `unsafe-math` and `finite-only`.[^passes] [Decision 56](../decisions/numbers.md#d56) forbids every one of them.

The second is that the host side is a runtime design. The wrapper library, how buffers reach the device, and whether the host waits for each launch (by default it does; `async` tokens let it continue, with `gpu.wait` to join) are choices,[^gpu] and [M12](m12-vortex-gpu-path.md) weighs them.

## nvgpu: a bridge to NVIDIA's special instructions

The `gpu` dialect covers what every GPU has. Fast NVIDIA kernels also use instructions that only NVIDIA has, and the **`nvgpu` dialect** sits between the general dialects (`gpu`, `vector`, `memref`) and `nvvm` to express them while keeping memrefs and vectors as operands.[^nvgpu] Four families matter for matrix multiplication, the kernel [G10](../gpu/g10-matmul-ladder.md) and [G11](../gpu/g11-matrix-units.md) build.

- `nvgpu.device_async_copy` starts a copy from global to shared memory without making the thread wait; copies are collected into groups, and `nvgpu.device_async_wait` waits for a group.[^nvgpu]
- `nvgpu.ldmatrix` loads a matrix fragment from shared memory into registers, as the step between a `vector.transfer_read` and NVVM's `ldmatrix`.[^nvgpu]
- `nvgpu.mma.sync` is a warp-wide matrix multiply-accumulate, the step between `vector.contract` and NVVM's `mma.sync`.[^nvgpu]
- `nvgpu.tma.async.load` loads a tile from global to shared memory through the tensor memory accelerator, and `nvgpu.warpgroup.mma` multiplies and accumulates across a warpgroup of four warps.[^nvgpu]

The pattern is the same as for the rest of this chapter: a general operation such as `vector.contract` is lowered to `nvgpu`, then by `--convert-nvgpu-to-nvvm` to `nvvm`, which the NVVM dialect page names as the typical pipeline.[^nvvm] MLIR 18.1.8 has that pass and an `nvgpu-optimize-shared-memory` pass that rearranges shared-memory accesses to reduce bank conflicts (both listed by `mlir-opt --help`, checked on 2026-09-24). This chapter's examples stop short of them; G11 explains what the instructions do.

## Where Vortex's facts would meet the GPU path

[M2](m2-reading-mlir.md#where-vortexs-facts-would-live) placed Vortex's promises in MLIR's CPU dialects. The GPU path adds new places, and new gaps:

| Vortex fact | Where the GPU path can record it | What checks it |
| --- | --- | --- |
| Fixed shapes ([decision 11](../decisions/arrays.md#d11)) | Constant launch sizes, `gpu.known_block_size`, static memrefs that allow bare pointers | Nothing: a launch with other sizes is undefined behaviour |
| Row-major layout ([decision 43](../decisions/arrays.md#d43)) | Which loop index is mapped to thread `x` | Nothing: a slow mapping is still correct |
| Independent iterations (your own analysis, with [decision 25](../decisions/references.md#d25)) | `scf.parallel` or `scf.forall` | Nothing: the writer's promise ([M6](m6-affine-and-scf.md#parallel-loops-that-the-writer-promises-scfparallel-and-scfforall)) |
| Strict floating point ([decision 56](../decisions/numbers.md#d56)) | `fastmath<none>` carried into `llvm`; no fast-math option on a target attribute | Nothing in MLIR: your own test |
| Positions for runtime errors ([decision 14](../decisions/program.md#d14)) | Locations survive; the device has no error path chosen | An open question for [M12](m12-vortex-gpu-path.md) |

The pattern of M2 repeats. MLIR checks structure; everything Vortex promises beyond structure must be encoded by the Vortex compiler and checked by its tests.

## For Vortex

!!! vortex "Exercise"

    **Build** a GPU branch in the MLIR-emitting tool from [M2's exercise](m2-reading-mlir.md#for-vortex), extended in [M4](m4-dialect-conversion.md#for-vortex) and [M6](m6-affine-and-scf.md#for-vortex), for the [stage 10 kernel](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for): emit its two outer loops as a parallel loop, then drive this chapter's upstream passes to a `gpu.module` lowered to the `nvvm` dialect and to SPIR-V. The tool stays separate from the `vortex` command, whose options [decision 20](../decisions/program.md#d20) fixes.

    1. **On paper, before any code:** why the `row` and `column` iterations are independent and the `k` iterations are not, naming the decisions that make each fact true; which of `row` and `column` must run along thread `x`, and why ([G4](../gpu/g4-memory-performance.md#which-index-runs-across-the-warp)); and, for the tile sizes you choose, the grid and block you expect.
    2. **An independence gate.** The tool emits a parallel loop only for loops that your dependence analysis from [P6](../optimize/p6-dependence-analysis.md#for-vortex) proves independent. Every other loop stays sequential, and the tool writes a remark naming the dependence it found.
    3. **A mapping you chose.** Use either route of this chapter, the pass pipeline or a transform schedule, and write down why. The index you named in step 1 must end up on thread `x`.
    4. **An uneven shape.** Pick one shape whose extent your tile size does not divide, and make the pipeline produce a launch with a guard, not the silent no-op this chapter showed.
    5. **Decision 56 on the device.** No floating-point operation in the `nvvm` output carries a fast-math flag, and your scripts attach no target attribute with `fast`, `ftz`, `daz`, `unsafe-math` or `finite-only`.

    **Not yet:** a `kernel` keyword or any syntax for parallel loops ([tour chapter 7](../language-tour/07-kernels-and-parallel-execution.md)); building a GPU binary or running a kernel, which needs an LLVM with the NVPTX or AMDGPU target; host code, `gpu-to-llvm` and a runtime; workgroup-memory tiling ([G10](../gpu/g10-matmul-ladder.md)); `nvgpu` and matrix units ([G11](../gpu/g11-matrix-units.md)); choosing Vortex's GPU route ([M12](m12-vortex-gpu-path.md)).

    **Proof that it works:**

    - A prediction test: for the stage 10 shape, a square shape and your uneven shape, the tool writes the grid and block it expects before running `mlir-opt`, and a test compares them with the printed `gpu.launch_func`.
    - A mapping test that reads the kernel and fails unless `gpu.thread_id x` feeds the index you named in step 1, in every load and store.
    - A refusal test: a Vortex function whose outer loop carries a dependence, such as P7's [`wave`](../optimize/p7-loop-transformations.md#skewing-and-the-unimodular-view) nest, gets sequential loops and a remark, and no `gpu.launch`.
    - A CPU cross-check: the parallel-loop form, before any GPU pass, lowered with your M4 pipeline and run, prints the stage 10 known answer bit for bit.
    - A SPIR-V round trip: for the uneven shape, `mlir-translate --serialize-spirv` and then `--deserialize-spirv` both succeed on your lowered module.
    - Two canaries: swap your mapping so the other index runs along `x` and confirm the mapping test fails; attach an NVVM target with `ftz` and confirm the decision 56 test fails.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does kernel outlining do, and why is it needed?** It moves a `gpu.launch` body into a `gpu.func` inside a `gpu.module` and leaves a `gpu.launch_func`; because the kernel is isolated from above, every captured value becomes an argument.
    - **How do parallel loops become a launch?** Tiling makes a tile loop and a point loop; the greedy mapper sends the first to blocks and the second to threads, first dimension to `x`; conversion builds the `gpu.launch`.
    - **What decides which index runs along thread `x`?** The pass route's habit (the first dimension) or a schedule's mapping attributes; in row-major data, the last index belongs there.
    - **Where does workgroup memory come from?** A memory attribution on the kernel, typed with the `workgroup` address space, synchronized with `gpu.barrier`; NVVM and ROCDL both place it in LLVM address space 3.
    - **What does SPIR-V need that NVVM does not?** A target environment, a fixed workgroup size, storage classes, descriptor bindings for arguments, and structured control flow.
    - **Which steps need a GPU back end?** Serializing a module with a target attribute and finishing the host launch; everything up to the `nvvm`, `rocdl` and `spirv` dialects, and SPIR-V serialization, runs on any machine.
    - **How can decision 56 break on the GPU path?** Through fast-math or flush-to-zero options on a target attribute, even when every `arith` operation carries `fastmath<none>`.

## Where this comes back

!!! next "You will use this again in"

    - [M11. End-to-end ML compilers](m11-ml-compilers.md): *gpu dialect*, *nvvm*, *spirv* as the last layers under IREE and Triton
    - [M12. Designing Vortex's GPU path](m12-vortex-gpu-path.md): *kernel outlining*, *target attribute*, *runtime wrapper*, *known block size*
    - [G10. The GPU matmul ladder](../gpu/g10-matmul-ladder.md): *workgroup memory*, *barrier*, *tile sizes as launch configuration*
    - [G11. Matrix units](../gpu/g11-matrix-units.md): *nvgpu.mma.sync*, *ldmatrix*, *asynchronous copy*
    - [G13. Tile languages](../gpu/g13-tile-languages.md): *who chooses the thread mapping*

## Sources and further reading

Read the `gpu` dialect page's sections on address spaces, memory attribution and compilation with this chapter's examples open, then the entries for `gpu.launch`, `gpu.launch_func` and `gpu.func`.[^gpu] The SPIR-V dialect page's design principles and control flow sections explain why that dialect looks unlike the others.[^spirv] For what the NVVM output becomes, read the NVPTX user guide's sections on kernels and address spaces.[^nvptx] The transform dialect's GPU operations are documented with the rest of the transform ops.[^transform]

[^gpu]: MLIR Project, "'gpu' Dialect", introduction, sections "GPU address spaces", "Memory attribution" and "GPU Compilation" (with "Default NVVM Compilation Pipeline", "Module serialization", "Offloading LLVM translation" and "The binary operation"), and the entries `gpu.barrier`, `gpu.func`, `gpu.launch`, `gpu.launch_func`, `gpu.module`, `gpu.thread_id` and `gpu.wait`. <https://mlir.llvm.org/docs/Dialects/GPU/>
[^passes]: MLIR Project, "Passes", entries `-gpu-kernel-outlining`, `-gpu-launch-sink-index-computations`, `-gpu-map-parallel-loops`, `-convert-parallel-loops-to-gpu`, `-scf-parallel-loop-tiling`, `-convert-gpu-to-nvvm`, `-convert-gpu-to-rocdl`, `-convert-gpu-to-spirv`, `-spirv-update-vce`, `-nvvm-attach-target`, `-rocdl-attach-target`, `-gpu-module-to-binary` and `-gpu-to-llvm`. <https://mlir.llvm.org/docs/Passes/>
[^transform]: MLIR Project, "Transform Dialect", entries `transform.gpu.map_forall_to_blocks` and `transform.gpu.map_nested_forall_to_threads`. <https://mlir.llvm.org/docs/Dialects/Transform/>
[^nvvm]: MLIR Project, "'nvvm' Dialect", introduction, "Scope and Capabilities" and "Placement in the Lowering Pipeline". <https://mlir.llvm.org/docs/Dialects/NVVMDialect/>
[^rocdl]: MLIR Project, "'rocdl' Dialect", introduction and "Dialect inclusion criteria and guidelines". <https://mlir.llvm.org/docs/Dialects/ROCDLDialect/>
[^nvgpu]: MLIR Project, "'nvgpu' Dialect", introduction and the entries `nvgpu.device_async_copy`, `nvgpu.ldmatrix`, `nvgpu.mma.sync`, `nvgpu.tma.async.load` and `nvgpu.warpgroup.mma`. <https://mlir.llvm.org/docs/Dialects/NVGPU/>
[^spirv]: MLIR Project, "SPIR-V Dialect", introduction, "Design Guidelines", "Dialect design principles" and "Control Flow". <https://mlir.llvm.org/docs/Dialects/SPIR-V/>
[^nvptx]: LLVM Project, "User Guide for NVPTX Back-end", sections "Marking Functions as Kernels" and "Address Spaces". <https://llvm.org/docs/NVPTXUsage.html>
[^amdgpu]: LLVM Project, "User Guide for AMDGPU Backend", section "Address Spaces". <https://llvm.org/docs/AMDGPUUsage.html>
[^start]: MLIR Project, "Getting Started", the example CMake command. <https://mlir.llvm.org/getting_started/>
[^cuda-simt]: NVIDIA, "CUDA Programming Guide", v13.4, section 2.3, "Writing SIMT Kernels", subsections 2.3.2 "Thread Hierarchy" and 2.3.4.1 "Coalesced Global Memory Access". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html>
[^cuda-adv]: NVIDIA, "CUDA Programming Guide", v13.4, section 3.2, "Advanced Kernel Programming", subsection 3.2.2.1 "SIMT Execution Model". <https://docs.nvidia.com/cuda/cuda-programming-guide/03-advanced/advanced-kernel-programming.html>
