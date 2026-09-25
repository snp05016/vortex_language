# M9. Schedules as IR: the transform dialect

<p class="page-intro">A second, ordinary MLIR module that holds handles to operations in the first one, and applies tile, fuse and vectorize operations to whatever those handles point at: a schedule written as IR instead of buried inside a pass.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 35 minutes · Builds on: [M2. Reading MLIR](m2-reading-mlir.md), [M3. Passes and pattern rewriting](m3-passes-and-rewriting.md), [M6. Loops: affine and scf](m6-affine-and-scf.md)</p>

???+ remember "Before you start, remember"

    ??? question "In SSA form, how many times is a named value defined, and what has to happen when a later step needs a changed version of it?"

        Exactly once. A step that wants a different value cannot mutate the old one in place; it produces a new name (a new SSA value) and everything downstream that needs the change uses the new name instead.

        Introduced in [O3. SSA form: construction and destruction](../optimize/o3-ssa.md#one-name-one-definition).

    ??? question "In MLIR's greedy pattern-rewrite driver, who decides which pattern runs where, and when does it stop?"

        The driver decides. It tries every registered pattern at every operation, in an order the pass's author does not control, and keeps going until no pattern matches anywhere in the region: a fixed point.

        Introduced in [M3. Passes and pattern rewriting](m3-passes-and-rewriting.md#the-greedy-driver-traced-by-hand).

    ??? question "In an operation's generic form, which bracket holds a property the operation's own verifier checks, and which holds a discardable attribute any pass may drop?"

        `<{ }>` holds properties: part of what the operation means, checked by its own verifier. Plain `{ }` holds discardable attributes, which carry a dialect prefix and can be dropped by a pass without changing the computation.

        Introduced in [M2. Reading MLIR](m2-reading-mlir.md#attributes-and-properties-hold-the-constants).

    ??? question "What does scf.forall promise about its iterations that scf.for does not?"

        scf.for is a sequential loop with one induction variable; each iteration may depend on state the previous one left behind. scf.forall's iterations carry no ordering requirement between them, so nothing in the loop itself forbids a compiler from running them out of order, or at once, on independent workers.

        Introduced in [M6. Loops: affine and scf](m6-affine-and-scf.md#parallel-loops-scfparallel-and-scfforall).

!!! goals "In this chapter"

    - Explain why the transform dialect writes a schedule as a second MLIR module instead of a pass argument, a config file, or a sequence of command-line flags.
    - Read a `transform.named_sequence`, following one handle from `transform.structured.match` through a transforming operation to the new handles it produces.
    - Explain why operations such as `tile_using_for` consume their input handle, and predict when reusing a handle after that point fails to verify.
    - Trace `tile_using_for`'s output against the loop vocabulary from M6, and say what changed between the payload before and after the schedule ran.
    - Connect handle-based scheduling to Halide's algorithm/schedule split and TVM's schedule search, and say what writing the schedule as IR adds that a search over opaque parameters does not.

## A payload file and a schedule file, in one module

Every MLIR file so far in this book has held one kind of thing: a program, or a fragment of one, in whatever dialect suited the level being shown. This chapter's files hold two. A **payload** is the IR being transformed: an ordinary function, built from `linalg`, `memref`, `scf` and `arith` operations exactly as earlier chapters wrote it. A **schedule** is a second piece of IR, in the **transform dialect**, that describes what to do to the payload: which operations to find, and which transformations to apply to them, in which order. Both live in the same file, inside the same top-level `module`, and both are ordinary MLIR: the schedule is parsed, verified and printed by the same infrastructure as the payload, not read out of a comment, a JSON sidecar or a compiler flag.

--8<-- "includes/examples/mlir/m9-transform-dialect/tile_matmul.mlir.md"

The payload here is `@mm`, a matmul over three `memref`s of fixed shape, written the way [M5's structured ops](m5-structured-ops.md) already write one: no loop, no address arithmetic, one named operation. The schedule is the `transform.named_sequence @__transform_main` below it. `--transform-interpreter`, the flag this example runs with, looks for a named sequence with that exact name and runs it once against the whole module as its starting point.

Inside the schedule, `transform.structured.match ops{["linalg.matmul"]} in %root` walks the payload looking for operations named `linalg.matmul`, and returns a **handle**: an SSA value, typed `!transform.any_op`, that stands for the set of payload operations it found. The handle is not a pointer the schedule can dereference however it likes; it is a value, produced by exactly one operation, that later transform operations pass around and consume like any other SSA value in this book. `transform.structured.tile_using_for %matmul [16, 16, 8]` takes that handle, tiles the matmul operation it points to by the given sizes along its three loop dimensions, and returns four new handles: one for the tiled `linalg.matmul` and one for each of the three loops the tiling introduced.

Nothing in the schedule mentions `64`, `32` or `16`, the matmul's actual shape, anywhere except inside `tile_using_for`'s own `[16, 16, 8]` argument. The schedule finds its target by operation name, not by a variable name or a shape written into the payload, and the same schedule would tile a matmul of any shape the same way, wherever `linalg.matmul` appears in whatever module it is run against.

<figure class="vx-figure">
<svg viewBox="0 0 760 400" role="img" aria-labelledby="m9-f1-title m9-f1-desc">
<title id="m9-f1-title">A schedule's handles point into the payload, before and after a consuming operation runs</title>
<desc id="m9-f1-desc">Two columns. The left column, labelled payload, holds two boxes stacked vertically: one box reading linalg.matmul near the top, and a second box lower down reading three scf.for loops plus a tiled linalg.matmul, connected to the first by a downward arrow labelled transform interpreter runs. The right column, labelled schedule, holds two boxes: transform.structured.match on linalg.matmul above, and transform.structured.tile_using_for on percent matmul with tile sizes sixteen, sixteen, eight below, connected top to bottom by a solid arrow labelled percent matmul, with a small note beside it reading consumed here. A dashed arrow runs from the match box across to the upper payload box, labelled finds by name. A second dashed arrow runs from the tile_using_for box down to the lower payload box, labelled percent tiled, percent loops colon three, new handles.</desc>
<defs>
<marker id="m9-f1-head" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker>
</defs>
<text class="vx-text-muted" x="150" y="30" text-anchor="middle">Payload</text>
<text class="vx-text-muted" x="590" y="30" text-anchor="middle">Schedule</text>
<rect class="vx-box-strong" x="40" y="50" width="220" height="60" rx="4"/>
<text class="vx-mono" x="150" y="85" text-anchor="middle">linalg.matmul</text>
<rect class="vx-box-strong" x="20" y="290" width="260" height="80" rx="4"/>
<text class="vx-mono" x="150" y="320" text-anchor="middle">3x scf.for</text>
<text class="vx-mono" x="150" y="342" text-anchor="middle">+ linalg.matmul (tiled)</text>
<path class="vx-line" d="M150 110 L150 290" marker-end="url(#m9-f1-head)"/>
<text class="vx-text-muted" x="220" y="205" text-anchor="middle">interpreter</text>
<text class="vx-text-muted" x="220" y="220" text-anchor="middle">runs the schedule</text>
<rect class="vx-box-accent" x="420" y="45" width="300" height="70" rx="4"/>
<text class="vx-mono" x="570" y="72" text-anchor="middle">transform.structured.match</text>
<text class="vx-mono" x="570" y="92" text-anchor="middle">ops{["linalg.matmul"]}</text>
<rect class="vx-box-accent" x="420" y="200" width="300" height="70" rx="4"/>
<text class="vx-mono" x="570" y="222" text-anchor="middle">tile_using_for %matmul</text>
<text class="vx-mono" x="570" y="242" text-anchor="middle">[16, 16, 8]</text>
<path class="vx-line" d="M570 115 L570 200" marker-end="url(#m9-f1-head)"/>
<text class="vx-mono" x="650" y="150" text-anchor="middle">%matmul</text>
<text class="vx-text-muted" x="650" y="185" text-anchor="middle">consumed here</text>
<path class="vx-line" d="M420 80 C 340 100, 300 90, 260 78" stroke-dasharray="4 3" marker-end="url(#m9-f1-head)"/>
<text class="vx-text-muted" x="340" y="60" text-anchor="middle">finds by name</text>
<path class="vx-line" d="M420 260 C 320 300, 300 320, 280 330" stroke-dasharray="4 3" marker-end="url(#m9-f1-head)"/>
<text class="vx-mono" x="380" y="370" text-anchor="middle">%tiled, %loops:3</text>
<text class="vx-text-muted" x="380" y="386" text-anchor="middle">new handles</text>
</svg>
<figcaption>Figure 1. The schedule (right) holds handles as ordinary SSA values. <code>match</code> produces <code>%matmul</code>, a handle into the payload's existing <code>linalg.matmul</code> (dashed arrow, left). <code>tile_using_for</code> consumes that handle and produces new ones, <code>%tiled</code> and <code>%loops</code>, pointing at what the payload becomes only after the interpreter runs: three <code>scf.for</code> loops around a smaller <code>linalg.matmul</code>. <code>%matmul</code> itself is not valid after this point.</figcaption>
</figure>

??? check "This schedule never names the matmul's shape, only its tile sizes. If the payload's memref types changed from 64x32 / 32x16 / 64x16 to some other size divisible by 16, 16 and 8, what in the schedule file would have to change?"

    Nothing. `transform.structured.match` finds the operation by name, and `tile_using_for [16, 16, 8]` tiles whatever three-dimensional structured operation it is handed. Only the payload's own memref types would change; the schedule is the same file either way.

## Handles are SSA values, and using one after it is consumed is an error

`tile_using_for` did more than read through `%matmul`'s handle: MLIR's [operation definitions](https://mlir.llvm.org/docs/Dialects/Transform/) mark its input operand as **consumed**. After a consuming transform operation runs, the payload operations the old handle pointed at may have been tiled, moved, or erased outright, so the handle no longer safely stands for anything. Using it again is rejected the way [O3](../optimize/o3-ssa.md#one-name-one-definition) already rejects writing to an SSA value twice, or the way an ownership system rejects a value used again after a move: the schedule's own author, not the interpreter, is responsible for threading the *new* handles a consuming operation returns to whatever comes next.

--8<-- "includes/examples/mlir/m9-transform-dialect/handle_invalidation.mlir.md"

`%matmul` above is produced once, by the match, and the first `tile_using_for` consumes it to tile the one payload matmul it points to. The second `tile_using_for`, on the next line, tries to use `%matmul` again, and the interpreter refuses to run the schedule at all: `mlir-opt`'s diagnostic names the transform operation that did the invalidating (the first `tile_using_for`, which "invalidates all handles to payload IR entities associated with this operand"), and points back to the payload's `linalg.matmul` itself as the entity that invalidation covers. Nothing about this check depends on what the second `tile_using_for` would otherwise have computed; the schedule is rejected before the interpreter runs anything past the first tiling.

??? check "Suppose the schedule above called transform.structured.match a second time, right before the second tile_using_for, to produce a fresh handle instead of reusing %matmul. Would the second tiling still fail to verify?"

    No. A fresh `transform.structured.match` would walk the payload as it exists at that point in the schedule and return a new handle standing for whatever it finds there, such as the tiled `linalg.matmul` the first `tile_using_for` left behind. That new handle was never consumed, so tiling it a second time is valid. The rule invalidates one specific handle when the operation that produced it is consumed; it does not forbid tiling the same payload operation more than once, only reusing the same stale SSA value to do it.

## Tiling produces a loop nest you already know how to read

The point of running the schedule is what it does to the payload, and that output is IR this book has already built the vocabulary for. `tile_using_for`'s result, after `mlir-opt --transform-interpreter` has applied it, is three nested `scf.for` loops around a smaller `linalg.matmul`, each loop bound and step an `arith.constant`, and each operand of the inner matmul a `memref.subview` cut from the original memref at that loop's current offset. Nothing about reading that nest is specific to the transform dialect: it is the same `scf.for` a hand-written loop would use, over the same kind of `memref` reference [M6](m6-affine-and-scf.md#a-loop-nest-is-a-set-of-points) already described.

The tiled matmul's `memref.subview` operands are the concrete version of what [M6's tiling section](m6-affine-and-scf.md#transformations-that-exactness-buys-tiling) described abstractly: the outer three loops walk the iteration space sixteen, sixteen and eight rows at a time, and each `linalg.matmul` at the bottom sees only its own 16x16x8 slice of the original arrays. `tile_using_for` did not invent a new way to express tiling; it produced the same structured loop nest this book already reads, and did so from a schedule that never had to be told how to build a loop, only which sizes to tile by.

## Fusing a producer into a consumer's loop

Tiling one operation is the schedule's smallest move. `transform.structured.fuse_into_containing_op` is a second one: given a handle to a **producer** operation and a handle to a loop that already contains a **consumer**, it rewrites the producer to run *inside* that loop, once per iteration, instead of running to completion beforehand and leaving its full result sitting in memory for the consumer to read back.

--8<-- "includes/examples/mlir/m9-transform-dialect/fuse_producer_consumer.mlir.md"

`@square_then_negate` squares every element of a 16x16 tensor, then negates every element of the result. Written this way, the payload has an ordinary producer-consumer relationship through `%sq`: the whole squared tensor exists as a value before the negate operation reads any of it. The schedule tiles only the consumer (`negate`) with `tile_using_forall`, producing a single `scf.forall` over a 4x4 grid of tiles, the parallel loop shape [M6](m6-affine-and-scf.md#parallel-loops-scfparallel-and-scfforall) introduced. `fuse_into_containing_op` then moves the producer (`square`) inside that loop: after fusion, each of the sixteen `scf.forall` iterations computes its own 4x4 tile of `square` from a slice of the original input, immediately feeds that tile to `negate`, and writes only that tile back out. A tensor the full 16x16 shape of `square`'s result is never materialized anywhere in the fused function; only 4x4 tiles ever exist as values.

Fusing a producer into a consumer's loop is not free in general: if two consumer tiles ever needed overlapping regions of the producer's output, as a stencil's neighboring output tiles do, the producer would run once per tile that touches each shared element, redoing work that the unfused version did once. This example has no such overlap. `square` and `negate` are both elementwise, so the 4x4 tile of `square` one `scf.forall` iteration computes is read by exactly that iteration's `negate` and by no other; the sixteen tiles partition the sixteen-by-sixteen domain with nothing shared between them. Fusion here removes the full-size intermediate tensor and adds no repeated work, a property specific to this producer-consumer shape, not a rule the transform dialect enforces or verifies on the schedule's behalf.

??? check "square_then_negate's producer computes x squared once for every element before fusion. After fusing square into negate's tiled loop, is any element of the input still squared more than once?"

    No. Because `square` and `negate` are both elementwise, each output tile's `negate` reads exactly the same-shaped tile of `square`, and the sixteen 4x4 tiles the schedule chose partition the 16x16 domain without overlap. Fusion changes when and in what size chunks `square` runs, and removes the full 16x16 intermediate value, but every input element is still squared exactly once. A producer feeding overlapping consumer tiles, such as a stencil's halo, would not have this property: fusion would recompute the elements every touching tile shares.

## Matching by name, and splitting a handle that matches more than one operation

`fuse_producer_consumer.mlir`'s schedule opens with `transform.structured.match ops{["linalg.generic"]} in %root`, which finds both `linalg.generic` operations, `square` and `negate`, and returns one handle standing for both, in the order the match walks the payload: `square` first, `negate` second, the same order they appear in the function. `transform.split_handle` takes that one handle and returns two, `%producer` and `%consumer`, one per matched operation, in that same order. Only after the split can the schedule tile `%consumer` alone and fuse `%producer` into the loop that tiling produced; `tile_using_forall` and `fuse_into_containing_op` each expect a handle to one kind of thing, and `split_handle` is how a schedule turns "the two ops I matched" into "this one, then that one."

A schedule that instead wanted to fuse a *third* operation, say a bias-add feeding into `negate` from a second producer, would need `transform.structured.match` to find three `linalg.generic` operations instead of two, and `split_handle` to return three results instead of two, one more `%bias` alongside `%producer` and `%consumer`; everything downstream that named `%producer` and `%consumer` by position would need to be checked against the new order rather than assumed to still line up.

??? check "Suppose the payload above also contained a third linalg.generic op, an unrelated bias-add nowhere near square or negate, appearing textually between them. What would transform.structured.match's single ops{[\"linalg.generic\"]} call now return, and what would need to change before split_handle could still pick out producer and consumer correctly?"

    The match would return one handle standing for all three operations, in the order they appear in the payload: square, then bias-add, then negate. `split_handle` would need a third result, and the schedule's positional assumption (first match is the producer, second is the consumer) would break, since the second match would now be the unrelated bias-add. A schedule matching more than the operations it actually wants either needs a more selective match (by name and by where it sits in the use-def chain) or must not rely on match order for which result is which.

## Heritage: Halide's schedules, TVM's search, and what IR adds

Separating *what* a computation produces from *how* it is executed is not new to MLIR. Halide split an image-processing pipeline's **algorithm**, the pure function from inputs to outputs, from a separate **schedule** describing loop order, tiling and storage, so that changing performance never required changing correctness, and a programmer could hand-tune the schedule directly.[^halide] TVM took the same split further for tensor programs: schedule **primitives** applied to an operator's computation graph, with the search over which primitives and parameters to use handled by a learned cost model rather than only by hand.[^tvm] The transform dialect keeps that same split, but changes the *language* the schedule is written in: not a domain-specific scheduling API bolted onto a host language, but ordinary MLIR, parsed, verified and printed by the same infrastructure as the payload it transforms. A schedule can be diffed like a payload, generated by another pass like a payload, and stored, versioned and read by a person the same way as the program it schedules; the dialect's own design paper works through the same motivation at length.[^m14]

That framing also explains why this chapter followed M3 rather than replacing it. `--canonicalize`'s greedy driver, from [M3](m3-passes-and-rewriting.md#the-greedy-driver-traced-by-hand), tries every registered pattern everywhere, in whatever order the driver picks, until nothing more matches: correct for cleanups where the order and target genuinely do not matter. A schedule is the opposite case: which operation gets tiled, by how much, in what order relative to fusion, changes the shape of the generated loops and, eventually, their performance, so the schedule's author needs to say exactly which operation, exactly when, and the transform interpreter runs exactly that sequence, once, in the order written. [M8's vector dialect chapter](m8-vectorization.md#where-this-fits-in-the-pipeline) already used one more operation from this family without stopping to explain it: `transform.structured.vectorize` rewrites a targeted structured operation directly into `vector.contract` and the transfer operations that chapter built up from scratch. Tiling, fusion and vectorization are three transformations from the same dialect, reached the same way, through a handle and a named operation in a schedule.

## For Vortex

!!! vortex "Exercise"

    **Build** two separate transform schedule files for the MLIR payload your tool already emits for the [stage 10 matmul kernel](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for): schedule A tiles the matmul with one set of sizes, schedule B tiles it with a different set, both dividing the kernel's fixed shapes evenly so neither needs padding.

    1. Both schedules target the same, unmodified payload file; only the `.mlir` schedule file differs between A and B.
    2. Each schedule is a `transform.named_sequence @__transform_main` that matches `linalg.matmul` by name and calls `tile_using_for` once, exactly as this chapter's first example did, with no operation this chapter did not cover.
    3. Running `mlir-opt --transform-interpreter` with schedule A and, separately, with schedule B, on the same input payload, produces two `scf.for` nests with different loop bounds and different `memref.subview` sizes.

    **Not yet:** fusion, `tile_using_forall`, vectorization, or any GPU-facing operation; a search over tile sizes ([P14](../optimize/p14-algorithms-and-schedules.md), [P15](../optimize/p15-choosing-parameters.md)) instead of two sizes you choose by hand; changing the payload itself to accommodate a size that does not divide evenly.

    **Proof that it works:**

    - Both tiled files pass `mlir-opt` with no options, and each, read by eye, shows loop bounds matching the tile sizes you chose.
    - Lower both tiled files the rest of the way to something you can run (following whatever path your tool already takes from `linalg` to an executable), and confirm both binaries, run on the same input arrays, write bit-identical output through the kernel's `&mut` output argument: two different loop structures, computing the one matmul [decision 56](../decisions/numbers.md#d56) already fixed with no reassociation, have no room to disagree.
    - A short note, in your own words, of which sizes you chose for A and B and why: divisibility into the fixed shape is the only requirement this exercise sets, but a real choice would also weigh what [P8](../optimize/p8-cache-blocking.md) and [P12](../optimize/p12-fast-gemm.md) already said about reuse and register pressure.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is a transform handle, and what is it not?** An SSA value in the schedule's own IR, produced by one operation (usually a match), that stands for a set of payload operations. It is not a pointer the schedule can dereference arbitrarily; every operation that produces or consumes a handle does so through the same operand and result rules every other MLIR value follows.
    - **Why does tile_using_for consume its input handle instead of leaving it valid?** Because the payload operations the handle pointed at may be tiled, moved or erased by the time the operation returns; a handle that stayed valid after that could no longer be trusted to mean what it meant when it was produced. The operation returns new handles to whatever exists afterward instead.
    - **What is the practical difference between running a schedule and running canonicalize's greedy driver?** The greedy driver applies whatever pattern matches, wherever it matches, until nothing more applies, in an order its author does not control. A schedule's author writes the exact sequence of operations, on exact handles, and the interpreter runs exactly that sequence once, in the order written.
    - **Why did the matmul-tiling schedule never mention the matmul's actual shape?** Because transform.structured.match finds its target by operation name, not by a variable name or a shape baked into the schedule; the same schedule file tiles any matmul it is pointed at, by the sizes it names.
    - **Why did fusing square into negate cost no redundant computation in this chapter's example?** Because both operations are elementwise, so each fused tile's producer computation is read by exactly the consumer tile it feeds, with no overlap between tiles. A producer whose consumer tiles overlap, such as a stencil, would recompute the shared elements once per tile that touches them.
    - **Where does the transform dialect's split of algorithm and schedule come from, and what does writing the schedule as IR add?** From Halide's algorithm/schedule separation and TVM's schedule primitives and search. Writing the schedule in the same IR infrastructure as the payload means it can be parsed, verified, diffed and generated by other passes the same way the payload can, instead of living in a separate scheduling language or a set of opaque flags.

## Where this comes back

!!! next "You will use this again in"

    - [M10. MLIR for GPUs](m10-mlir-for-gpus.md): *transform.gpu.map_forall_to_blocks* and *map_nested_forall_to_threads*, turning an `scf.forall` this chapter produced into a GPU launch grid.
    - [M12. Designing Vortex's GPU path](m12-vortex-gpu-path.md): *whether Vortex's own lowering writes schedules in this dialect, or hand-rolls tiling and fusion as ordinary compiler passes instead*.

## Sources and further reading

The dialect's own documentation is the primary reference for every operation this chapter used, including the ones cited by name but not shown, such as `transform.structured.vectorize`; its tutorial walks through building a schedule step by step, in more detail than this chapter's three short examples covered.[^transform][^transform-tutorial] The CGO 2025 paper is the place to read the design rationale for a second IR in full, including cases this chapter did not reach.[^m14] For the two systems that split scheduling from computation before MLIR did, read Halide's own paper for the algorithm/schedule separation this chapter's fusion example echoes,[^halide] and TVM's for how that separation scales to search rather than hand-tuning.[^tvm]

[^transform]: MLIR Project, "'transform' Dialect", read on 2026-09-24. <https://mlir.llvm.org/docs/Dialects/Transform/>
[^transform-tutorial]: MLIR Project, "Transform Dialect Tutorial", read on 2026-09-24. <https://mlir.llvm.org/docs/Tutorials/transform/>
[^m14]: Lücke, Zinenko, Moses, Steuwer, Cohen, "The MLIR Transform Dialect", CGO 2025, pp. 241-254, doi:10.1145/3696443.3708922. <https://arxiv.org/abs/2409.03864>
[^halide]: Ragan-Kelley et al., "Halide: A Language and Compiler for Optimizing Parallelism, Locality, and Recomputation in Image Processing Pipelines", PLDI 2013, pp. 519-530. <https://people.csail.mit.edu/jrk/halide-pldi13.pdf>
[^tvm]: Chen et al., "TVM: An Automated End-to-End Optimizing Compiler for Deep Learning", OSDI 2018, pp. 578-594. <https://www.usenix.org/conference/osdi18/presentation/chen> ; <https://arxiv.org/abs/1802.04799>
