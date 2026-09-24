# G13. Tile languages

<p class="page-intro">Triton, CUDA Tile, CuTe and Mojo move the block-tile, warp-tile and thread-tile split that G10's ladder wrote by hand into the compiler; this chapter asks what a language has to promise once it makes that move, and what it would take to try it in Vortex.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 20 minutes · Builds on: [G10. The GPU matmul ladder](g10-matmul-ladder.md)</p>

???+ remember "Before you start, remember"

    ??? question "In G10's ladder, what does a block tile let a whole thread block avoid, that reading every value straight from DRAM does not?"

        Rereading the same bytes from global memory. A block tile loads a slice of each operand into shared memory once, and every thread in the block reads that shared copy instead of going back to DRAM for each output it touches.

        Introduced in [G10. The GPU matmul ladder, rung 3](g10-matmul-ladder.md#rung-3-shared-memory-tiling-the-block-tile).

    ??? question "What is a warp (or SIMD-group), and how wide is one on NVIDIA and Apple GPUs?"

        A fixed-size group of threads that a GPU issues one instruction to at a time: 32 on NVIDIA hardware and on Apple GPUs.

        Introduced in [G2. The SIMT execution model](g2-simt.md#warps-wavefronts-and-simd-groups).

    ??? question "May a Vortex compiler reorder or fuse the additions inside a dot product's accumulation loop?"

        No. Every floating-point operation is one IEEE 754 operation, rounded once, in the order the program writes it; a compiler may not fuse, reassociate or reorder them without the programmer's explicit permission.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

    ??? question "Why can a Vortex compiler skip the per-thread boundary guard entirely for `multiply`'s 64 x 64 arrays, but not necessarily for a 70 x 70 one?"

        Because every Vortex array's extent is a compile-time constant, a compiler can check, before generating any code, whether a chosen block size divides an extent with no remainder. 64 divides evenly by several block sizes with no leftover thread; 70 does not divide evenly by every block size, so some block's warp needs the guard.

        Introduced in [G2. The SIMT execution model](g2-simt.md#bringing-this-back-to-vortex-fixed-shapes-and-the-boundary-guard).

    ??? question "What does MLIR's transform dialect take as input to describe a tiling, and what does running it produce?"

        A handful of target tile sizes, given to an operation such as `transform.structured.tile_using_for`, matched against a `linalg.matmul` (or similar) operation in the module. Running it produces a nest of loops around a smaller version of the same operation, without any change to the operation's own definition.

        Introduced in [G10. The GPU matmul ladder](g10-matmul-ladder.md#tiling-is-a-schedule-change-not-new-arithmetic).

!!! goals "In this chapter"

    - Explain why a tile language moves the block-tile, warp-tile and thread-tile split G10 wrote by hand into the compiler, and what stays in the programmer's hands once it does.
    - Recognize the same block-level operation in four tile languages (Triton, CUDA Tile, CuTe, Mojo), and name what each one fixes explicitly versus leaves to its compiler.
    - Trace one tiling schedule, MLIR's transform dialect, from a single block-level operation to a nested loop, and connect it to the rewrite G10 already showed.
    - Identify which parts of a tile-level operation's contract, shape, aliasing and floating-point order, a Vortex `kernel` would need to state explicitly for a compiler to tile it safely.
    - Decide, for one array shape and one tile size, whether a boundary (masked) tile is needed or the tile size divides the array evenly.

## One block-level operation instead of a whole ladder

Every rung of G10's ladder computed the same 64 x 64 matrix product; what changed, rung by rung, was who chose the mapping from output elements to threads, and how many memory levels that mapping staged data through. By rung 9, the programmer, in Boehm's own CUDA kernels, had written out the block tile's shared-memory load, the thread tile's register accumulators, and the warp tile's lane arithmetic, by hand, as ordinary index expressions.[^boehm] Nothing in the CUDA source states "this is a tile"; the tile shape lives only in the values the programmer chose for a handful of named constants (`BM`, `BN`, `BK`, `TM`, `TN`, `WM`, `WN`) and the index arithmetic built from them.

A **tile language**, in this chapter's sense, is a programming model in which the programmer instead writes one operation on a whole tile, a rectangular block of an array, and a separate part of the compiler chooses the mapping down to threads: which rung of G10's ladder to apply, with which tile sizes, staged through which levels of memory. The clearest way to see the difference is to look at the one place G10 already crossed this line. Its account of tiling as a schedule change did not write six nested loops by hand; it wrote a single `linalg.matmul` on a whole 16 x 8 by 8 x 16 tile, and a separate `transform.structured.tile_using_for [4, 4, 2]` schedule that MLIR's transform dialect turned into the loop nest:[^mlir-transform]

--8<-- "includes/examples/gpu/g13-tile-languages/tile_matmul.mlir.md"

Nothing about the `linalg.matmul` operation itself named a thread, a block, or a byte of shared memory; the three numbers in the schedule are the only place a tile size appears, and `mlir-opt` produced the nested nine-loop-deep-in-spirit structure (three loops here, since the running example tiles it once) from them. That split, one block-level operation plus a separate, replaceable schedule, is the shape every tile language in this chapter takes, whether the schedule is a compiler pass, a tuning search, or a library the programmer calls. Nothing about the `linalg.matmul` operation itself would need to change if a second schedule retiled it with `[8, 8, 4]` instead of `[4, 4, 2]`: the tile sizes live entirely in the schedule, so changing them changes only the loop bounds and subview sizes the schedule produces, never the operation that states what to compute.

## Who used to make each choice, rung by rung

Read G10's ladder again with one question: for each rung, who wrote the code that made the choice? Rung 1's mapping from `(row, column)` to a thread is a one-line index computation any GPU program needs, tile language or not. But rungs 3 through 9, the block tile's shared-memory staging, the thread tile's register accumulators, the vector loads, the bank-conflict padding, and the warp tile's lane arithmetic, were all choices Boehm's kernels stated by hand, each one a specific number of lines of index arithmetic and `__shared__` declarations.[^boehm] A tile compiler's job is to make those same choices itself, from a much smaller program: state the block-level operation once, and let the compiler decide how many of G10's rungs to apply and with which tile sizes.

This is not a claim that the compiler discovers something Boehm's hand-written kernels did not know; the reuse argument behind block tiling, thread tiling and warp tiling is exactly [G10's arithmetic-intensity formula](g10-matmul-ladder.md#rungs-4-and-5-register-tiling-the-thread-tile), unchanged. What moves is who applies it, and how quickly it can be re-applied when a target's shared-memory capacity or register file differs. A block-level program keeps working, unmodified, whether the schedule chooses a 32 x 32 or a 64 x 64 block tile for it; the equivalent hand-written CUDA kernel needs new arithmetic, and often a new pass over the whole kernel, for each choice.

## Triton: launch order is a schedule choice too

Triton is the tile language this book's research traces furthest: Tillet, Kung and Cox's 2019 paper gave it a source language, Triton-C, and an intermediate representation, Triton-IR, built for exactly this block-level style of GPU program.[^triton] Its compiler performs the coalescing, shared-memory staging and vectorization rungs of G10's ladder as automatic passes over a block-level program, rather than asking the programmer to write them; today's implementation lowers that program through its own stack of MLIR dialects (`tt`, `ttg`, and target-specific dialects such as `ttng` for NVIDIA and `amdg` for AMD) on the way to a target.[^triton-dialects] Two tuning quantities that G10's ladder either hard-coded or searched by hand, `num_warps` (how many warps cooperate on one block tile) and `num_stages` (how many iterations of software pipelining, G10's rung 10, to run in flight), become parameters the compiler can search, the same autotuning idea as G10's rung 8, now automated per program instead of hand-tuned per kernel.[^triton-tutorial]

Triton's own matrix-multiplication tutorial makes one further scheduling choice that sits above every rung of G10's ladder: the order in which the grid of output tiles is launched. Finishing one whole row of output tiles before starting the next, the order this chapter's `row_major` function produces, means that a short run of consecutive launches touches many distinct column-tiles of the second operand, one per column, even though a block only needs a few of them at once. Grouping a fixed number of row-tiles together, and sweeping every column inside that group before moving to the next group of rows, keeps each column's tile in reuse for the whole group instead of for one launch, at the cost of needing several row-tiles of the first operand resident at once instead of one.[^triton-tutorial] `grouped_launch_order.cpp` counts that trade directly, as a working set: the number of distinct row-tile and column-tile ids that appear in a sliding window of the launch order, with no timing or cache involved, only counting.

--8<-- "includes/examples/gpu/g13-tile-languages/grouped_launch_order.cpp.md"

For an 8 x 12 grid of tiles and a window of 8 consecutive launches, grouping four row-tiles at a time lowers the average number of distinct tiles a window touches; row-major order visits a new column-tile on almost every step inside one row, while the grouped order revisits the same handful of column-tiles across a whole group before moving on. This is a scheduling decision above the block, warp and thread tiles G10 covered: it never changes what one block tile computes, only in which order the grid of block tiles is handed to the GPU, which is exactly the kind of choice a tile compiler can make on the programmer's behalf once tiles, rather than individual threads, are the unit the program is written in.

??? check "grouped_launch_order.cpp reports a lower average working set for the grouped order than for row-major order, at window width 8. Why does grouping row-tiles together lower that count, and what real resource is 'working set' standing in for?"

    Sweeping every column inside a fixed group of row-tiles means the group's column-tiles of the second operand stay in reuse across the whole group, instead of being touched once and left behind as row-major order moves across one row. 'Working set' stands in for how many tiles would need to stay resident, most plausibly in an on-chip cache such as L2, to serve the launches inside a short window without a repeat access going back to a slower memory; the program counts distinct tile ids only, never a real cache or a timing.

## The same idea in three more forms

Triton is one point in a wider design space; three other systems state the same block-level contract differently.

NVIDIA's CUDA Tile model, introduced in CUDA 13.3, gives the programmer an explicit tile type inside an otherwise ordinary CUDA-like language: `cuda::tiles` in C++, and a Python front end, cuTile, both lowering to a shared CUDA Tile intermediate representation.[^cuda-tile] Where Triton's block-level program looks like an ordinary function over whole tensors, CUDA Tile keeps the host language's syntax and adds a type for "this value is a tile, of this shape, in this memory space," letting the same compiler infrastructure that already lowers ordinary CUDA kernels lower tile-typed ones too.

CUTLASS's CuTe takes a third approach: instead of a language whose compiler picks a schedule, CuTe is a C++ (and, via a Python DSL, a scripted) template library of composable **layouts**, algebraic descriptions of how a tile's logical indices map onto memory offsets and onto threads, that the programmer assembles explicitly rather than writing raw index arithmetic or waiting for a compiler pass to find one.[^cutlass-cute] The block, warp and thread tile hierarchy G10 borrowed from CUTLASS's own account of an efficient GEMM is exactly the hierarchy CuTe's layouts are built to express, one layout per level, composed into one description of the whole mapping.[^cutlass]

Mojo takes the systems-language route: a language with its own compiler, built on MLIR, that targets GPU kernels portable across NVIDIA, AMD and Apple silicon GPUs from one source, rather than one vendor's toolchain.[^mojo] Its GPU programming model exposes tiles and thread-block-level operations as language features, with the same underlying question every other system in this chapter answers: which parts of the block-tile-to-thread mapping the program states, and which parts its compiler fills in.

Underneath all four systems, the contract is the same shape: the programmer states a tile's logical shape and the operation to perform on it (a matmul, a reduction, a copy); the compiler, or in CuTe's case the layout the programmer chose from a library instead of the compiler itself, fills in everything G10's ladder covers below that: the thread, warp and lane mapping, the shared-memory or threadgroup-memory staging, the pipelining across loop iterations, and, for Triton and CUDA Tile, a search over the tile-size choices themselves.

??? check "CuTe and Triton both raise a GPU matmul to a block-level statement. What is the one thing CuTe's programmer still writes by hand that Triton's does not?"

    Which layout to use. CuTe gives the programmer a library of composable layout descriptions and asks them to assemble the block, warp and thread mapping from that library explicitly; Triton's compiler chooses (and can search over) that mapping itself from a block-level program that never names a layout at all. Both stop the programmer from writing raw thread-index arithmetic, but CuTe still asks for an explicit choice among ready-made mappings, where Triton asks for none.

## The same matmul at three granularities

<figure class="vx-figure">
<svg viewBox="0 0 800 400" role="img" aria-label="The same matmul, written at three granularities, and how much of the mapping decision each one leaves to the compiler" aria-describedby="g13-f1-desc">
<title id="g13-f1-title">The same matmul, written at three granularities</title>
<desc id="g13-f1-desc">Schematic diagram, not measured data. Three vertical bars of equal total height, one per granularity. Left bar, hand-written CUDA as in G10's ladder: almost all of the bar is the programmer's share (algorithm and the full thread, warp and block mapping written by hand), with a thin compiler share at the top (register allocation and instruction selection only). Middle bar, one block-level operation with an explicit MLIR transform-dialect schedule: roughly half the bar is the programmer's share (the algorithm and the three tile-size numbers of the schedule), the other half the compiler's share (the loop nest, the index arithmetic and the later mapping to threads). Right bar, an autotuned tile language such as Triton: most of the bar is the compiler's share (tile-size search, thread and warp mapping, shared-memory staging and pipelining), with a thin programmer share at the bottom (the algorithm and the block's logical shape only).</desc>
<text class="vx-text" x="20" y="24">Same 64 &#215; 64 matmul, three ways to write it (schematic: relative share, not a measurement)</text>
<line class="vx-line" x1="60" y1="320" x2="760" y2="320"/>
<rect class="vx-box" x="110" y="80" width="150" height="240"/>
<rect class="vx-box-accent" x="110" y="80" width="150" height="30"/>
<text class="vx-mono" x="185" y="66" text-anchor="middle">compiler: reg. alloc.</text>
<text class="vx-mono" x="185" y="220" text-anchor="middle">programmer: algorithm +</text>
<text class="vx-mono" x="185" y="236" text-anchor="middle">full thread/warp/block map</text>
<text class="vx-text-muted" x="185" y="345" text-anchor="middle">Hand-written CUDA</text>
<text class="vx-text-muted" x="185" y="361" text-anchor="middle">(G10's ladder)</text>
<rect class="vx-box" x="325" y="80" width="150" height="240"/>
<rect class="vx-box-accent" x="325" y="80" width="150" height="120"/>
<text class="vx-mono" x="400" y="66" text-anchor="middle">compiler: loop nest,</text>
<text class="vx-mono" x="400" y="50" text-anchor="middle">index arithmetic, thread map</text>
<text class="vx-mono" x="400" y="260" text-anchor="middle">programmer: algorithm +</text>
<text class="vx-mono" x="400" y="276" text-anchor="middle">3 tile-size numbers</text>
<text class="vx-text-muted" x="400" y="345" text-anchor="middle">Block op + explicit</text>
<text class="vx-text-muted" x="400" y="361" text-anchor="middle">schedule (transform dialect)</text>
<rect class="vx-box" x="540" y="80" width="150" height="240"/>
<rect class="vx-box-accent" x="540" y="80" width="150" height="210"/>
<text class="vx-mono" x="615" y="66" text-anchor="middle">compiler: tile-size search,</text>
<text class="vx-mono" x="615" y="50" text-anchor="middle">thread/warp map, staging,</text>
<text class="vx-mono" x="615" y="102" text-anchor="middle">pipelining</text>
<text class="vx-mono" x="615" y="306" text-anchor="middle">programmer: algorithm +</text>
<text class="vx-mono" x="615" y="300" text-anchor="middle"></text>
<text class="vx-text-muted" x="615" y="345" text-anchor="middle">Tile language, autotuned</text>
<text class="vx-text-muted" x="615" y="361" text-anchor="middle">(Triton)</text>
</svg>
<figcaption>Figure 1. The same 64 &#215; 64 matmul written at three granularities: hand-written CUDA (G10's ladder), a block-level MLIR operation with an explicit transform-dialect schedule, and an autotuned tile language. Shaded (upper) region: what the compiler decides. Outlined (lower) region: what the programmer states. Schematic, to show the direction of the shift; not a measurement of any real program.</figcaption>
</figure>

## What a block-level operation must still promise

Raising the unit of a program from a thread to a tile does not remove the questions G10 asked about every rung of its own ladder; it only asks them once, of the operation, instead of once per rung. Two of them matter enough to name.

The first is aliasing. A block-level "write this output tile" operation is safe for a compiler to stage through shared memory, split across warps, or pipeline across iterations only if the compiler also knows the output tile does not overlap either input tile; if it might, two block tiles' writes could race with each other's reads in ways no tiling schedule could safely reorder around. [Decision 25](../decisions/references.md#d25) settles exactly this question for `multiply`'s signature by making it a semantic error for a `&mut` argument to alias any other argument of the same call; a tile-level Vortex operation would need the same guarantee, stated once at its boundary, for the same reason G10 already gave: proving it once per function is cheaper than checking it once per rung.[^d25-note]

The second is floating-point order. G10's own ladder stayed silent on this question through rung 10 because none of its rungs, coalescing, staging, register tiling or warp tiling, ever reorders an arithmetic operation; only rung 11's tensor-core instructions fix their own accumulation order in hardware, which is why the [philosophy page](../philosophy.md#3-do-not-surprise-the-programmer) treats them as opt-in, not automatic.[^tensor-note] A tile language's block-level `dot` operation faces the identical question one level up: Triton's `tl.dot` and CUDA Tile's tile-level matrix multiply both lower to tensor-core instructions on hardware that has them, whenever the operand types and shapes allow it, because that is where a tile language's speed usually comes from.[^triton-tutorial] Stating a block-level matmul as one operation, rather than as G10's explicit `sum += a[row, k] * b[k, column]` loop, is therefore not a schedule change in the sense rungs 1 through 10 were; it hands the compiler a choice, tensor cores or not, that [decision 56](../decisions/numbers.md#d56) reserves for the programmer to grant explicitly. A tile-level Vortex `kernel` would need its own way to state that grant, separately from the schedule choices (tile sizes, launch order, staging) this chapter has otherwise treated as free for the compiler to make.

??? check "Why does writing a matmul as one block-level `dot` operation raise the tensor-core question in a way that G10's explicit `sum +=` loop never did?"

    G10's loop names the exact order its accumulation runs in, so a compiler that respects decision 56 has only one order to produce, without tensor cores, whatever it does. A block-level `dot` names no order at all; it is the compiler, not the program text, that decides whether to lower it to a sequence of scalar multiply-adds in a fixed order or to a tensor-core instruction with its own fixed, different order and often reduced precision. The block-level operation is silent on exactly the question decision 56 answers for the loop, which is why a tile language needs a separate, explicit way to grant or withhold permission for the faster, order-changing lowering.

## Boundary tiles: the guard, one level up

[G2](g2-simt.md#bringing-this-back-to-vortex-fixed-shapes-and-the-boundary-guard) already worked out what happens when an array's extent does not divide a block size evenly: some block's warp needs a runtime guard, `if (idx < n)`, and only the warps that straddle the array's edge pay for the extra pass that guard costs. A tile language faces the same question one level up, at the granularity of a whole tile rather than a single lane. If a tile size does not divide an array's extent, the tile that covers the array's edge is only partly inside it, and the operation reading or writing that tile needs a **mask**: a per-element predicate that says which of the tile's positions are real. Triton's tutorials write this explicitly, as a boolean mask passed to a tile load or store; CUDA Tile's predicated tile operations do the analogous thing through the type rather than through an explicit mask value.[^cuda-tile]

G2's argument for why Vortex can sometimes skip its guard entirely applies here without change: because every Vortex array's extent is a compile-time constant (decision 11 requires every dimension to be an integer constant expression, never a runtime value),[^d11-note] a compiler can check, at compile time and not at run time, whether a candidate tile size divides an array's extent with no remainder. `multiply`'s 64 x 64 arrays divide evenly by a 16 x 16 tile, an 8 x 8 tile, or any tile size that is a factor of 64; a tile-level operation over them never needs a mask, the same conclusion G2 reached for the per-lane guard on the same shape. A 70 x 70 array, by contrast, needs a mask (or a tile size chosen to divide 70, such as 7 x 7) for the same reason G2's 70-element example needed a guard for at least one warp: the shape and the tile size do not agree.

This is not a claim that Vortex should generate masked tile code; v0.1 has no GPU back end to generate anything for. It is the same observation `warp_partition.cpp` already made useful for checking a thread-tile split: knowing every shape and every candidate tile size at compile time turns "does this tile size need a mask" from a question a running kernel answers with a branch into a question a compiler, or a design exercise, can answer once, in advance, by division.

--8<-- "includes/examples/gpu/g13-tile-languages/warp_partition.cpp.md"

??? check "A candidate tile size of 16 x 16 is checked against `multiply`'s 64 x 64 arrays, and separately against a 70 x 70 array. Which check can a Vortex compiler decide at compile time, and what decides it?"

    Both, because both shapes are compile-time constants under [decision 11](../decisions/arrays.md#d11): the compiler needs only to divide 64 by 16 (4, no remainder: no mask needed) and 70 by 16 (4 remainder 6: a mask, or a different tile size, is needed) with integer arithmetic on known values, never a runtime check. What decides the answer is nothing about GPUs at all; it is ordinary integer division on two numbers the compiler already has.

## For Vortex

!!! vortex "Exercise"

    **Build** a tile-shape checker for `multiply`'s two array shapes from this book (64 x 64) and one more shape you choose that does not divide evenly by every tile size you will try, together with a short design memo.

    1. For a candidate tile size (`TileM`, `TileN`), write the checker so it reports, for a given array shape (`M`, `N`): whether the tile size divides the shape evenly in both directions; if not, the size of the boundary tile in each direction (the remainder); and how many boundary (partial) tiles the shape would produce along each axis.
    2. Extend the checker to accept a small list of candidate tile sizes for one shape and report which ones need no mask at all, echoing this chapter's 64 x 64 and 70 x 70 comparison but for tile sizes and shapes you choose.
    3. Write a five-bullet design memo, no more than half a page, answering: should a future Vortex `kernel` be tile-level (the programmer states one block-level operation, the compiler chooses the mapping) or thread-level (the programmer writes the mapping, the way G10's ladder does)? Ground each bullet in something this chapter or G10 established: which rungs a compiler can safely apply on its own ([decision 56](../decisions/numbers.md#d56)'s line between schedule and arithmetic), what a tile-level operation's contract would need to state explicitly (aliasing, floating-point order), and what Vortex's fixed shapes buy a tile-level design that a language with runtime shapes does not.

    **Not yet:** generating any GPU code for any rung, tile size or mask ([M12](../mlir/m12-vortex-gpu-path.md) weighs the paths); designing real syntax for a tile-level `kernel` (this exercise states what such a design would need to promise, not what it would look like); choosing tile sizes automatically ([P15](../optimize/p15-choosing-parameters.md)'s subject); anything involving tensor cores or other order-changing instructions ([G11](g11-matrix-units.md)'s subject).

    **Proof that it works:**

    - For `multiply`'s 64 x 64 shape, the checker reports no boundary tile for every tile size that is a factor of 64 (1, 2, 4, 8, 16, 32, 64), matching this chapter's own conclusion.
    - For the shape you choose that does not divide evenly, the checker reports a nonzero boundary tile size for at least one candidate tile size, and that boundary size, added to the number of full tiles times the tile size, equals the shape's extent exactly.
    - The design memo names, for at least one bullet, a specific rung or decision from this chapter or G10 that supports it, not a general preference stated without one.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is a tile language, in the sense this chapter uses it?** A programming model where the programmer states one operation on a whole tile, and a separate part of the compiler, not the programmer, chooses the thread, warp and block mapping, the memory staging and the pipelining that G10's ladder otherwise wrote by hand.
    - **What did rungs 3 through 9 of G10's ladder have in common, from a tile language's point of view?** All of them were choices a tile compiler can make on the programmer's behalf, from a smaller, block-level program, because none of them changes the arithmetic, only who holds a value and when.
    - **Name one thing Triton's compiler chooses that CuTe's programmer chooses instead.** The thread, warp and lane mapping (the layout): Triton's compiler picks and can search over it from a block-level program; CuTe's programmer assembles it explicitly from a library of composable layouts.
    - **Why does a block-level `dot` operation raise a question G10's explicit accumulation loop never had to answer?** The loop names its accumulation order directly, so only one lowering respects it; the block-level operation names no order, leaving the compiler to choose between a fixed-order lowering and a tensor-core instruction with a different, hardware-fixed order, which is exactly the choice [decision 56](../decisions/numbers.md#d56) reserves for the programmer.
    - **Why can a Vortex compiler decide, at compile time, whether a tile size needs a boundary mask?** Because every array shape is a compile-time constant under [decision 11](../decisions/arrays.md#d11), checking whether a tile size divides a shape evenly is ordinary integer division on known values, the same reasoning [G2](g2-simt.md#bringing-this-back-to-vortex-fixed-shapes-and-the-boundary-guard) already used for the per-lane guard.
    - **What does a tile-level operation's contract need to state that a hand-written thread-level kernel states implicitly, line by line?** Non-aliasing between its output and its inputs, and whether an order-changing lowering such as a tensor core is permitted; a hand-written kernel answers both by the exact code the programmer wrote, while a block-level operation answers neither unless something states it separately.

## Where this comes back

!!! next "You will use this again in"

    - [G11. Matrix units](g11-matrix-units.md): *tensor cores*, *opt-in precision*, *warp-level instruction*
    - [M9. Schedules as IR: the transform dialect](../mlir/m9-transform-dialect.md): *handles to payload IR*, *tile/fuse/vectorize as rewrites*
    - [M10. MLIR for GPUs](../mlir/m10-mlir-for-gpus.md): *outlining a block-level operation to a GPU kernel*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *tile-level versus thread-level kernels*, *the feature-decision worksheet*
    - [P15. Choosing parameters: models or search](../optimize/p15-choosing-parameters.md): *autotuning*, *tile-size search*

## Sources and further reading

Read the Triton paper first for the block-level programming model in its original form, then the matrix-multiplication tutorial for the launch-order idea `grouped_launch_order.cpp` measures.

[^boehm]: Simon Boehm, "How to Optimize a CUDA Matmul Kernel for cuBLAS-like Performance: a Worklog", December 2022: the hand-written index arithmetic and shared-memory staging in kernels 3 through 9. <https://siboehm.com/articles/22/CUDA-MMM>
[^mlir-transform]: MLIR Project, "Transform Dialect", the `transform.structured.tile_using_for` operation. <https://mlir.llvm.org/docs/Dialects/Transform/>
[^triton]: Tillet, Kung, Cox, "Triton: An Intermediate Language and Compiler for Tiled Neural Network Computations", MAPL 2019, pp. 10-19, doi:10.1145/3315508.3329973. <https://www.eecs.harvard.edu/~htk/publication/2019-mapl-tillet-kung-cox.pdf>
[^triton-dialects]: Triton Project, "MLIR Dialects", the `tt`, `ttg` and target-specific dialects. <https://triton-lang.org/main/dialects/dialects.html>
[^triton-tutorial]: Triton Project, "Tutorials: Matrix Multiplication", the grouped ordering for L2 reuse, `num_warps`, `num_stages`, and the tile-level `tl.dot`. <https://triton-lang.org/main/getting-started/tutorials/03-matrix-multiplication.html>
[^cuda-tile]: NVIDIA, "CUDA Programming Guide", v13.4.2, "Writing Tile Kernels", the `cuda::tiles` C++ type, cuTile, and predicated tile operations. <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-tile-kernels.html>
[^cutlass-cute]: NVIDIA, "CUTLASS" repository, the CuTe library and its Python DSL. <https://github.com/NVIDIA/cutlass>
[^cutlass]: NVIDIA, "CUTLASS: Efficient GEMM in CUDA", the block tile, warp tile and thread tile hierarchy CuTe's layouts express. <https://docs.nvidia.com/cutlass/latest/media/docs/cpp/efficient_gemm.html>
[^mojo]: Modular, Mojo GPU introductory tutorial, portability across NVIDIA, AMD and Apple silicon GPUs from one source. <https://max.modular.com/gpu/intro-tutorial>
[^d25-note]: [Decision 25, value semantics](../decisions/references.md#d25): a variable borrowed by a `&mut` argument must not appear in any other argument of the same call.
[^tensor-note]: [G10, beyond rung 9](g10-matmul-ladder.md#beyond-rung-9-where-the-free-lunch-ends): tensor-core instructions fix their own accumulation order and often their precision, in hardware, not in the program text.
[^d11-note]: [Decision 11, constant dimension expressions](../decisions/arrays.md#d11): every array dimension must be an integer constant expression, never a name or a runtime value.
