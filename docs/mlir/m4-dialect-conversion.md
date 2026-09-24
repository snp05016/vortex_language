# M4. Dialect conversion and lowering to LLVM

<p class="page-intro">A dialect conversion pass replaces every operation the target does not want with operations it does, and changes the types those operations carry along the way. This chapter takes one small function through that process, from four mixed dialects down to the llvm dialect, so you can read what a lowering pass actually did and predict when it will refuse to finish.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 22 minutes · Builds on: [M2. Reading MLIR](m2-reading-mlir.md), [M3. Passes and pattern rewriting](m3-passes-and-rewriting.md)</p>

???+ remember "Before you start, remember"

    ??? question "What replaces a phi at the top of an MLIR block, and where does the value it needs come from?"

        A block argument. The block does not ask "which path did control take"; instead, every predecessor's branch supplies the right value as an operand, and the block just receives it.

        Introduced in [M2. Reading MLIR](m2-reading-mlir.md).

    ??? question "In generic form, how can you tell a property from a discardable attribute without knowing the operation?"

        By the brackets. A property sits inside `<{ }>`, right after the operation's name; a discardable attribute sits in a plain `{ }` and its name carries a dialect prefix.

        Introduced in [M2. Reading MLIR](m2-reading-mlir.md).

    ??? question "Can a Vortex array's shape depend on a value known only at run time?"

        No. Every array's shape is a constant dimension expression, fixed when the program is compiled, so a function that takes a `[f32; 8, 16]` always takes exactly that shape.

        Introduced in [Arrays and shapes, decision 11](../decisions/arrays.md#d11).

    ??? question "What must be true of a mutable variable's stack slots before LLVM's `mem2reg` pass will turn them into SSA values for you?"

        Among other conditions, the slots must be created at the start of the function, in its entry block.

        Introduced in [Stage 6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#ssa-if-you-use-llvm).

!!! goals "In this chapter"

    - Explain what a conversion target's legal, illegal and dynamically legal operations decide, and why that decision is separate from which patterns exist.
    - Read a conversion pattern's adaptor and say why it can hand you a different value than the operation's own operand list does.
    - Trace what a `memref` becomes when a type converter turns it into an llvm dialect value, and why the result still carries a size and a stride at run time.
    - Distinguish partial, full and analysis conversion, and say which one a lowering pipeline's last pass should pick.

## A function with two dialects to remove

Start with the smallest function that still needs converting: clamping a value into the range zero to one, written branch-free so that both outcomes of each comparison are always computed.

--8<-- "includes/examples/mlir/m4-dialect-conversion/clamp.mlir.md"

Two dialects appear here, `arith` for the comparisons and selects and `func` for the function itself, and neither exists in the file `mlir-opt` prints back. Every operation changed its name: `arith.cmpf` became `llvm.fcmp`, `arith.select` became `llvm.select`, and `func.func` became `llvm.func`. This is what a **dialect conversion** pass does: given a starting module and a description of what the output is allowed to contain, it rewrites operations, one matched pattern at a time, until nothing left over disagrees with that description.[^conv]

The description is a **`ConversionTarget`**, an object built once per pass that answers one question for any operation: is it **legal**, **illegal**, or **dynamically legal**. `addLegalDialect` marks every operation of a dialect as legal, meaning the pass may leave it exactly as it is; `addIllegalDialect` marks a dialect where "no instance of a given operation is legal", meaning some pattern must replace every one of them; and `addDynamicallyLegalOp` covers the case where only some instances are legal, checked by a callback the pass supplies.[^conv] For `clamp.mlir`, the two passes shown each add one dialect to the legal set, `llvm` first and then `llvm` again alongside whatever `func`-to-`llvm` needs, and leave `arith` and `func` themselves out of it entirely, which is the same as marking them illegal by omission once a target's default is set that way.

This vocabulary matters because a conversion pass can fail for a reason that has nothing to do with any pattern being wrong. If an operation is illegal and no pattern matches it, the pass reports a legalization failure and stops; the target is not a suggestion.

??? check "clamp.mlir converts cleanly with `--convert-arith-to-llvm --convert-func-to-llvm --reconcile-unrealized-casts`, and no `scf` pass anywhere in the list. Why does the conversion target not complain that scf was never handled?"

    Because a target's legality rules are checked against the operations present in the module, not against a checklist of dialects the pass writer had in mind. `clamp01` contains no `scf` operation, so there is nothing for a missing `scf` pattern to fail on. A conversion target only ever asks about the operations in front of it.

## Patterns read an adaptor, not the original operands

A **conversion pattern** is a rewrite rule the driver considers for one operation type, in the same pattern-matching style [M3](m3-passes-and-rewriting.md) covers for ordinary rewrites, with one difference: instead of matching against the operation's own operands, it matches against an **adaptor**, a small wrapper holding "the most recent replacement values" for each operand, not the values the operation was originally written with.[^conv] The distinction only shows up once an earlier pattern has already run.

Follow it through `clamp01`. `arith.select %below, %lo, %x` uses `%below`, the result of `arith.cmpf olt, %x, %lo`. Suppose the driver converts the comparison first: `arith.cmpf` is replaced by `llvm.fcmp`, whose result is a fresh SSA value of the same `i1` type. When the driver then visits `arith.select`, the pattern for it does not read `%below` from the original operation; it reads the adaptor's operand list, which by then names the new `llvm.fcmp` result. The pattern never has to go looking for what replaced its inputs. That bookkeeping is the point of routing every pattern through an adaptor instead of the raw operation.

??? check "Inside the pattern that rewrites arith.select, would op.getOperands() and adaptor.getOperands() ever return different SSA values for the same call?"

    Yes, whenever an operand's defining operation has already been converted earlier in the same run. `op.getOperands()` still names the values `arith.select` was written with; `adaptor.getOperands()` names their current replacements. That is why a conversion pattern is written against the adaptor and never against the operation's own operand list.

## Types change too: the type converter and the memref descriptor

`clamp01` never needed its argument's *type* to change: `f32` is `f32` on both sides of the conversion. A function over arrays does not stay that simple. Here is a reduction over two eight-element arrays, `dot(a, b) = sum of a[i] * b[i]`, the same shape of accumulation the [stage 10 kernel](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for) runs once per output element, over one dimension instead of two:

--8<-- "includes/examples/mlir/m4-dialect-conversion/dot_full.mlir.md"

Four dialects disappear this time: `scf`, whose loop becomes a branch and a block argument that carries `%acc` and the induction variable, the same block-argument mechanism the remember box above named; `memref`, whose loads become pointer arithmetic; `arith`, as before; and `func`. The interesting change is what happens to the two `memref<8xf32>` parameters. `dot`'s signature took two arguments. `llvm.func @dot` takes ten.

A **`TypeConverter`** is the object that decided this. Besides rewriting operations, a conversion pass can carry a type converter, which maps each source type to a target type through `addConversion` callbacks.[^conv] For a ranked `memref`, MLIR's own conversion maps it to a five-field descriptor: an **allocated pointer** to the buffer as it was allocated, an **aligned pointer** to the data addressed by loads and stores, an **offset**, and one **size** and one **stride** per dimension, all as `index`-turned-integer values.[^targetllvmir] `memref<8xf32>` becomes `!llvm.struct<(ptr, ptr, i64, array<1 x i64>, array<1 x i64>)>`, checked directly against `dot_full.expected`.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="A memref value exploding into the five fields of its LLVM descriptor" aria-describedby="m4-f1-desc">
<title id="m4-f1-title">One memref value becomes five llvm values</title>
<desc id="m4-f1-desc">On the left, one box labelled memref of 8 times f32, a single SSA value. Five lines lead from it to five boxes stacked on the right, appearing one after another: allocated pointer, aligned pointer, offset as index turned i64, size of dimension zero as index turned i64, stride of dimension zero as index turned i64. A caption underneath each right-hand box names the LLVM type it carries: pointer, pointer, i64, i64, i64.</desc>
<rect class="vx-box-strong" x="20" y="110" width="200" height="70" rx="4"/>
<text class="vx-mono" x="120" y="140" text-anchor="middle">memref&lt;8xf32&gt;</text>
<text class="vx-text-muted" x="120" y="160" text-anchor="middle">one SSA value</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5">
<path class="vx-line" d="M220,120 C320,90 380,44 460,34"/>
<polygon class="vx-arrowhead" points="460,34 448,30 450,40"/>
<rect class="vx-box" x="470" y="14" width="270" height="40" rx="4"/>
<text class="vx-mono" x="484" y="39">allocated ptr</text>
<text class="vx-text-muted" x="700" y="39" text-anchor="end">ptr</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5">
<path class="vx-line" d="M220,128 C320,105 380,74 460,66"/>
<polygon class="vx-arrowhead" points="460,66 448,62 450,72"/>
<rect class="vx-box" x="470" y="46" width="270" height="40" rx="4"/>
<text class="vx-mono" x="484" y="71">aligned ptr</text>
<text class="vx-text-muted" x="700" y="71" text-anchor="end">ptr</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5">
<path class="vx-line" d="M220,140 C320,132 380,110 460,98"/>
<polygon class="vx-arrowhead" points="460,98 448,94 450,104"/>
<rect class="vx-box" x="470" y="78" width="270" height="40" rx="4"/>
<text class="vx-mono" x="484" y="103">offset</text>
<text class="vx-text-muted" x="700" y="103" text-anchor="end">i64</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5">
<path class="vx-line" d="M220,155 C320,160 380,146 460,130"/>
<polygon class="vx-arrowhead" points="460,130 448,127 452,137"/>
<rect class="vx-box" x="470" y="110" width="270" height="40" rx="4"/>
<text class="vx-mono" x="484" y="135">size[0]</text>
<text class="vx-text-muted" x="700" y="135" text-anchor="end">i64</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 5">
<path class="vx-line" d="M220,168 C320,188 380,178 460,162"/>
<polygon class="vx-arrowhead" points="460,162 448,160 453,169"/>
<rect class="vx-box" x="470" y="142" width="270" height="40" rx="4"/>
<text class="vx-mono" x="484" y="167">stride[0]</text>
<text class="vx-text-muted" x="700" y="167" text-anchor="end">i64</text>
</g>
<text class="vx-text-muted" x="20" y="220">One value in, five values out. At a function boundary the descriptor is not passed as one</text>
<text class="vx-text-muted" x="20" y="238">struct: each field becomes its own scalar argument, which is why @dot's ten parameters are</text>
<text class="vx-text-muted" x="20" y="256">five per array, not two.</text>
</svg>
<figcaption>Figure 1. What a rank-1 memref becomes once a TypeConverter finishes with it: not one value but five, matching the fields checked in <code>dot_full.expected</code>. A rank-2 memref, the shape Vortex's arrays use, adds one more size and one more stride field, seven in total.</figcaption>
</figure>

The unbundling at the function boundary is deliberate, not an accident of this one example: MLIR's calling convention "unbundles" a memref descriptor into individual scalar arguments for C compatibility, rather than passing a struct by value.[^targetllvmir] The design choice behind the llvm dialect itself is why the output reads as familiar LLVM IR at all: "the semantics of the LLVM dialect operations must correspond to the semantics of LLVM IR instructions", with any divergence treated as a bug, and each LLVM IR type mapping to exactly one MLIR type.[^llvmd] Nothing about `llvm.fmul`, `llvm.getelementptr` or `llvm.icmp` in `dot_full.expected` needs translating in spirit; only the syntax around them is MLIR's.

??? check "dot_full's llvm.func takes ten scalar arguments for two memref&lt;8xf32&gt; parameters, even though the type itself fixes the size at 8 and the stride at 1. Why does the descriptor still carry them as runtime values instead of baking them in?"

    Because the default memref-to-llvm lowering is uniform: it builds the same five-field descriptor whether the shape is static or dynamic, and leaves specialization for later. The caller, who does know the shape at the call site, is free to pass the literal constants 8 and 1 directly; the callee's signature makes no distinction between a size that happens to be constant and one that will not be known until run time.

## Partial conversion and the cast that means "not yet"

The two examples so far used every pattern needed to reach the llvm dialect completely. Run only one of those patterns, and the same source function tells a different story:

--8<-- "includes/examples/mlir/m4-dialect-conversion/dot_partial.mlir.md"

`--convert-arith-to-llvm` alone converts `arith.constant 0 : index` into `llvm.mlir.constant(0 : index) : i64`, because the type converter maps `index` to a fixed-width integer.[^conv] But `scf.for`, left untouched because `scf` was never marked illegal, still declares its bounds as `index`. The converted constant cannot feed it directly, so the driver inserts a `builtin.unrealized_conversion_cast`, described in the framework's own terms as a stand-in that "connects the newly created operation with the next op, without changing the types of the latter one".[^conv] Three of them appear in `dot_partial.expected`, one per loop bound.

This is **partial conversion**: `applyPartialConversion` legalizes the operations it can and leaves the rest, including any casts needed to keep the file type-correct in the meantime.[^conv] It is what lets a real pipeline work one dialect at a time, the way [M3](m3-passes-and-rewriting.md)'s small, focused passes already argued for: a pass that converts only `arith` does not need to know that `scf`, `memref` and `func` exist, only that it is allowed to leave them alone. `applyFullConversion` is the stricter sibling: every operation must end up legal, or the whole pass fails, which is what `clamp.mlir` and `dot_full.mlir` relied on to finish completely. A third mode, `applyAnalysisConversion`, runs the same legalization logic but only records which operations would have converted, without changing the IR at all, useful for asking "is this module ready" without committing to an answer.[^conv]

??? check "dot_partial.mlir still has three unrealized_conversion_cast operations in it. Could you hand this file to mlir-translate --mlir-to-llvmir as it stands?"

    No. Translating to real LLVM IR expects a module built from the llvm dialect and a small set of compatible ones; `unrealized_conversion_cast` has no LLVM IR counterpart at all. The casts are a promise that some later pass will finish the job, not a state the file can leave MLIR in. A working pipeline either runs the remaining conversion passes or, once every op around a cast has genuinely converted, runs `--reconcile-unrealized-casts` to remove the now-redundant bridge.

## From the llvm dialect to LLVM IR proper

Once every operation is `llvm` and no cast remains, as in `dot_full.expected`, the module is not merely *LLVM-like*: `mlir-translate --mlir-to-llvmir` turns it into a textual `.ll` file mechanically, operation for operation, because that correspondence was the design constraint on the dialect from the start.[^llvmd] From that `.ll` file on, everything [Stage 6](../compiler/guide/stage-6-first-machine-code.md) already describes applies unchanged: the same SSA form, the same phis (now genuinely LLVM's, generated from the block arguments `--convert-scf-to-cf` built), and the same path through `opt` and `llc` to an object file. Dialect conversion is not a separate universe from the rest of this book's back end material; it is one more way of arriving at the same LLVM IR, this time built up through several small, checked steps instead of by a hand-written emitter walking an AST once.

## Where Vortex's facts would meet this pass

| Vortex fact | What dialect conversion does with it |
| --- | --- |
| Fixed shapes ([decision 11](../decisions/arrays.md#d11)) | The type converter still emits a full descriptor with runtime size and stride fields; a Vortex-aware lowering could choose a smaller, specialized representation instead, since every shape is already known |
| Row-major layout ([decision 43](../decisions/arrays.md#d43)) | Encoded only in how a later pass computes offsets from the descriptor's stride fields; the descriptor's shape (three scalars, then one size and one stride per dimension) does not itself say which order the strides are in |
| One rounding per `f32` operation, no contraction ([decision 56](../decisions/numbers.md#d56)) | A conversion pattern for `arith.mulf` or `arith.addf` must carry the `fastmath = none` property across to its `llvm` replacement unchanged; a pattern that dropped it would be a silent, undetected violation |
| An `&mut` output overlaps no argument ([decision 25](../decisions/references.md#d25)) | Nothing a `ConversionTarget` or `TypeConverter` checks. Aliasing is a property of values, not of which dialect an operation belongs to, so this promise has to survive from whatever pass produced the MLIR in the first place |
| The matmul kernel's three array parameters | Each becomes its own group of seven scalar arguments once lowered (three plus one size and one stride per dimension, for a rank-2 array), a calling-convention cost worth knowing before comparing it against a hand-written ABI |

## For Vortex

!!! vortex "Exercise"

    **Build** a checklist, not code: for the MLIR file your [M2](m2-reading-mlir.md) exercise emits for the stage 10 kernel, or a file you write by hand in the same shape, work out the exact sequence of `mlir-opt` passes that takes it to the llvm dialect with nothing left over, and run it.

    1. List every dialect the file uses (`builtin`, `func`, `scf`, `memref`, `arith`, and whichever others your emitter chose) and, for each, the conversion pass that removes it.
    2. Order the passes so that each one only depends on dialects that are still present when it runs, ending with `--reconcile-unrealized-casts`.
    3. Count, by hand, how many scalar arguments the three array parameters become once `memref` is gone, using the size-and-stride-per-dimension rule this chapter checked, and compare it against what `mlir-opt` actually produces.
    4. Confirm the result with `mlir-translate --mlir-to-llvmir`: it should succeed with no diagnostics about an operation it does not understand.

    **Not yet:** writing a conversion pattern in C++ or touching MLIR's libraries directly, designing a Vortex-specific dialect ([M12](m12-vortex-gpu-path.md) is where that choice gets discussed), structured ops or `linalg` ([M5](m5-structured-ops.md)), and any decision about whether a future Vortex actually adopts MLIR.

    **Proof that it works:**

    - The full pass sequence, run once, produces a file with only `builtin` and `llvm` operations in it: `mlir-opt <your-flags> kernel.mlir | grep -v '"llvm\.\|"builtin\.'` prints nothing.
    - The same file round-trips through `mlir-translate --mlir-to-llvmir` without error.
    - Your hand count of scalar arguments per array matches the signature `mlir-opt` prints.
    - A canary: delete one pass from the middle of your sequence (for example the one converting `memref`) and rerun. `mlir-opt` should report a legalization failure naming an operation it could not convert, not a crash and not a silently wrong file. Save that message; it is the `ConversionTarget` doing exactly what section one of this chapter described.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does a `ConversionTarget` decide that patterns alone do not?** Whether the pass is even allowed to finish: every remaining operation must be legal, or dynamically legal by its own rule, once the patterns are done.
    - **Why does a conversion pattern take an adaptor instead of reading the operation's own operands?** Because an earlier pattern may already have replaced one of those operands with a new value of a possibly different type, and the adaptor always names the current one.
    - **What five fields does a memref become under the default lowering?** An allocated pointer, an aligned pointer, an offset, and one size and one stride per dimension, unbundled into separate scalar arguments at a function boundary.
    - **What is an `unrealized_conversion_cast`, and is it ever a valid final answer?** A materialization the driver inserts when a converted value must feed an operation that has not converted yet. It is a promise, not a destination: a module built to hand to `mlir-translate` must have none left.
    - **What is the difference between partial and full conversion?** Partial conversion legalizes what it can and leaves the rest, including bridging casts; full conversion requires every operation to end up legal or the whole pass fails.
    - **What does analysis conversion actually change in the IR?** Nothing. It runs the same legality check and records what would have converted, without rewriting anything.

## Where this comes back

!!! next "You will use this again in"

    - [M5. Structured ops: linalg, tensor and memref](m5-structured-ops.md): *conversion target*, *named operation lowering to loops*
    - [M6. Loops: affine and scf](m6-affine-and-scf.md): *`--convert-scf-to-cf`*, *block arguments from loop-carried values*
    - [M7. Bufferization](m7-bufferization.md): *memref*, *descriptor*, *aliasing that conversion itself does not track*
    - [M9. Schedules as IR: the transform dialect](m9-transform-dialect.md): *pattern*, *legality*, *payload IR*
    - [M10. MLIR for GPUs](m10-mlir-for-gpus.md): *`ConversionTarget`*, *TypeConverter*, *the same framework converting to `nvvm`, `rocdl` or SPIR-V instead of `llvm`*
    - [M12. Designing Vortex's GPU path](m12-vortex-gpu-path.md): *descriptor cost*, *calling convention*
    - [D1. Debug information](../backend/d1-debug-info.md): *location*, *what a rewrite must carry forward*
    - [E1. The LLVM code generator pipeline](../backend/e1-llvm-codegen-pipeline.md): *`.ll` file*, *what happens once real LLVM IR exists*
    - [O3. SSA form: construction and destruction](../optimize/o3-ssa.md): *block arguments*, *phi*, *building SSA from structured control flow*

## Sources and further reading

For the framework itself, read the Dialect Conversion document in full alongside `dot_partial.expected` open in another window; the "Type Conversion" and "Modes of Legalization" sections name every term this chapter used.[^conv] The Toy tutorial's chapters on partial lowering and on reaching LLVM walk a larger, hand-written dialect through the same two steps this chapter split into three small examples.[^toy5][^toy6] The `llvm` dialect and Target LLVM IR pages are the ones to keep open once you are reading a lowering's output instead of a summary of it.[^llvmd][^targetllvmir]

[^conv]: MLIR Project, "Dialect Conversion", sections "Conversion Target", "Type Conversion", "Modes of Legalization" and "Materializations". <https://mlir.llvm.org/docs/DialectConversion/>
[^llvmd]: MLIR Project, "'llvm' Dialect", sections "Dependency on LLVM IR" and "Built-in Type Compatibility". <https://mlir.llvm.org/docs/Dialects/LLVM/>
[^targetllvmir]: MLIR Project, "Target LLVM IR", sections "Ranked MemRef Types" and "Calling Conventions". <https://mlir.llvm.org/docs/TargetLLVMIR/>
[^toy5]: MLIR Project, "Chapter 5: Partial Lowering to Lower-Level Dialects for Optimization", Toy tutorial. <https://mlir.llvm.org/docs/Tutorials/Toy/Ch-5/>
[^toy6]: MLIR Project, "Chapter 6: Lowering to LLVM and CodeGeneration", Toy tutorial. <https://mlir.llvm.org/docs/Tutorials/Toy/Ch-6/>
