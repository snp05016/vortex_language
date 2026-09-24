# M7. Bufferization

<p class="page-intro">A tensor is a value with a shape; a memref is a name for a region of memory. This chapter follows one small function through the pass that turns the first into the second, and shows when that pass can overwrite data in place and when it must copy it first.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 17 minutes · Builds on: [M5. Structured ops: linalg, tensor and memref](m5-structured-ops.md)</p>

???+ remember "Before you start, remember"

    ??? question "In MLIR, can two memref arguments legally point at the same memory, and does anything in the type say otherwise?"

        Yes. A `memref<...>` type records a shape, an element type and a layout, nothing about who else may read or write the same bytes. Two memref arguments may alias and the type system has no way to forbid it.

        Introduced in [M2. Reading MLIR](m2-reading-mlir.md).

    ??? question "What is a tensor value, and what Vortex thing does it behave like?"

        Data with a shape but no layout you can choose and no address you can take: MLIR's value counterpart to a memref. It behaves like a Vortex array passed by value, which decision 25 always copies rather than aliases.

        Introduced in [M2. Reading MLIR](m2-reading-mlir.md).

    ??? question "What does scf.forall's shared output promise, and which Vortex habit does it echo?"

        Each iteration writes into its own slot of a value threaded through as the loop's output, instead of returning a fresh value. It is close to Vortex's own habit of writing a result through a `&mut` parameter rather than returning it.

        Introduced in [M6. Loops: affine and scf](m6-affine-and-scf.md).

    ??? question "Within one Vortex call, may a variable borrowed as &mut also appear as another argument of the same call?"

        No: that is a semantic error. A callee's `&mut` output parameter is guaranteed to share storage with none of the call's other arguments.

        Introduced in [References, decision 25](../decisions/references.md#d25).

!!! goals "In this chapter"

    - Explain why MLIR represents a whole-array computation as an immutable tensor value before any pass decides where it lives in memory.
    - Read destination-passing style: what an `outs` operand promises, and what `tensor.empty` stands for when a computation has no natural destination of its own.
    - Trace what One-Shot Bufferize does to a function's tensor arguments and results, and where its `memref.alloc` calls come from.
    - Predict, for a small tensor function, which SSA values can share one buffer and which force a `memref.copy`, by finding a read-after-write conflict.
    - Connect that in-place-or-copy decision to Vortex's own `&mut` output parameter and its no-aliasing call rule.

MLIR programs that manipulate whole arrays are usually written first in tensor form: shapes and dataflow, no statement anywhere of where a value will live. A real program needs memory, so something has to close that gap. **Bufferization** is the name for that step: turning every tensor value into a memref backed by real storage, while keeping the function's own contract exactly what it was. That contract is the same one [O1](../optimize/o1-optimizer-contract.md) states for any optimizing pass: a transformation may change how a computation is represented and how much memory it touches, but never what it computes. Bufferization is an unusually literal case of that contract, since its entire job is to change representation (value to address) without moving a single result.

**One-Shot Bufferize** is MLIR's implementation of that step for the `tensor`, `linalg`, `scf` and related dialects: one pass, run once, that walks a function's tensor SSA values and decides, value by value, whether a result can occupy the same memory as one of its inputs or needs memory of its own.[^bufferization] The rest of this chapter works through what that decision looks like on functions small enough to trace by hand.

## A function that allocates nothing, until it does

Here is a function that multiplies every element of a 4-element array by a runtime constant, written in the `linalg` and `tensor` dialects [M5](m5-structured-ops.md) introduces:

--8<-- "includes/examples/mlir/m7-bufferization/scale_dps.mlir.md"

`%in` and the result are both `tensor<4xf32>` values. A **tensor**, in MLIR, is data with a shape and an element type but no address and no layout a program can observe: an SSA value like any other, produced once and never written to again, the value-typed counterpart to the pointer-like `memref`.[^tensor] `%init` is built by `tensor.empty`, an operation that returns a tensor of the given shape and says nothing about its contents: not zero, not garbage in a way a program could read, simply a placeholder for "a value of this shape will go here". Nothing in this function has allocated memory. Nothing needs to yet: every value is still just a fact about a shape and a computation, the same way an SSA value in any other dialect is a fact before it is a storage location.

`mlir-opt --one-shot-bufferize=bufferize-function-boundaries` turns every tensor in this file into a memref; the output above the fold is the same file, the output below it is what that flag produces. Two changes matter. First, `%in`'s type at the function boundary becomes `memref<4xf32, strided<[?], offset: ?>>`, a memref with a layout MLIR cannot pin down at compile time, because a caller's own memref might be a slice of something larger; `bufferize-function-boundaries` is what turns tensor parameters and results into memrefs at all, since by default the pass leaves a function's own signature alone and bufferizes only its body.[^bufferization] Second, `tensor.empty` disappears and a `memref.alloc` with a fixed alignment takes its place: the point where the placeholder becomes sixteen real bytes.

## Destination-passing style: naming the output before you compute it

`linalg.generic`'s `outs(%init : tensor<4xf32>)` operand is not an input the way `ins(%in : ...)` is: `scale`'s body never reads `%acc`, the block argument `outs` supplies, only writes to it. This pattern, where an operation takes an operand that names the value its result must overwrite, is **destination-passing style** (DPS): "for every tensor result, a DPS op has a corresponding tensor operand", and that pairing is what lets bufferization "alias it with the op result and perform the operation in-place" instead of guessing which of several operands a result might reuse.[^bufferization] `tensor.empty` exists because DPS still requires a destination operand even when the computation has none of its own to offer: a fresh, empty one stands in, and the bufferized program allocates real memory for it.

Naming the destination up front is also what makes an in-place write possible at all. A pass that only saw a result's *value* would have to guess which, if any, of the operation's inputs it could safely overwrite; a pass that sees an explicit `outs` operand has the answer handed to it, and can skip an allocation whenever that operand's buffer is free to reuse. Skipping an allocation is not free by accident: it is the difference between one pass over memory that is already resident and one pass that first has to write a whole new region out, the kind of cost [P2](../optimize/p2-memory-hierarchy.md) covers in general. Destination-passing style is how bufferization gets to make that choice explicitly, operation by operation, instead of pattern-matching after the fact.

This is worth pausing on for Vortex specifically. A Vortex function does not return its matrix product; it writes it through a `&mut` output parameter, `multiply(a: &[f32; M, K], b: &[f32; K, N], c: &mut [f32; M, N])`, decided before a single multiply-add runs. That is destination-passing style, chosen for Vortex's source language before MLIR enters the picture at all: the caller names the destination, the callee writes into it, and nothing about the computation's own shape has to guess where its answer belongs. `linalg.generic`'s `outs` operand is the same idea, written at the level of one operation instead of one function, with `tensor.empty` filling the gap for computations, like a fresh scale or a fresh sum, that a source language would just return.

??? check "Why does an operation whose output has nothing to do with any input, such as scale's result, still need an outs operand instead of just using ins and a result type?"

    Because DPS makes the destination-aliasing decision explicit and uniform, instead of special-casing operations by whether they happen to have a natural destination. Every `linalg` op's result can be traced to exactly one tensor operand, `outs`, so bufferization has one rule to apply everywhere: try to make the result share the destination's buffer. `tensor.empty` supplies that operand when nothing else would, at the cost of one allocation the pass has to materialize, which is exactly what happened to `scale`.

## When two live values need two different buffers

Sharing a buffer in place is only safe when nothing still needs the old contents. Here is a function where that fails:

--8<-- "includes/examples/mlir/m7-bufferization/patch_conflict.mlir.md"

`tensor.insert_slice` writes `%patch` into a 4-element window of `%t` and calls the result `%patched`. So far this looks like `scale`: one destination-shaped operand, one result. But the function returns *both* `%patched` and `%t` itself, unpatched. If bufferization let `%patched` overwrite `%t`'s own buffer in place, the second return value would come back already patched, which is not what the function says. This is a **read-after-write conflict**: a value (`%t`) is read again, here by the second `return`, after another operation (`tensor.insert_slice`) would have written through the same memory. One-Shot Bufferize is a whole-function analysis over the tensor IR's SSA use-def chains, built exactly to find conflicts like this one: for each candidate in-place write, it checks every remaining use of the value that write would clobber, and only writes in place when no such use survives.[^bufferization] Here one does, so the pass, run with the same `bufferize-function-boundaries` flag as `scale`, keeps `%t`'s own buffer untouched and materializes a fresh one for `%patched`. Its output, below the source above, has a `memref.copy %arg0, %alloc` that makes the conflict visible: every byte of the input is duplicated before anything is written, so the write into `%alloc[0:4]` (a `memref.subview` and a second copy) leaves `%arg0` exactly as the caller passed it. Figure 1 traces the same function lane by lane, from its two tensor arguments and two tensor results down to the three buffers that come out the other side.

<figure class="vx-figure">
<svg viewBox="0 0 640 440" role="img" aria-label="Bufferizing patch_then_reread: one tensor argument is read twice, so bufferization copies it into a new buffer instead of writing the patch in place" aria-describedby="m7-f1-desc">
<title id="m7-f1-title">Bufferizing a read-after-write conflict</title>
<desc id="m7-f1-desc">Two panels, tensor values on top and memrefs on the bottom, divided by a horizontal line. Top panel: box t of type tensor 8xf32 and box patch of type tensor 4xf32 both feed into a tensor.insert_slice box, which produces patched, tensor 8xf32. A separate arrow curves from t, over the top of the diagram, directly to a second result box also labelled t, returned again unchanged, bypassing the insert_slice op entirely. Bottom panel: box arg0, memref 8xf32, and box arg1, memref 4xf32, are the buffers for t and patch. An arrow labelled memref.copy, drawn as a highlighted flowing line and marked as the forced copy, runs from arg0 to a new box alloc, memref 8xf32, shown in an accent color. A second arrow from arg1 into alloc is labelled write into 0 to 4. Alloc feeds into a box labelled return 0 equals alloc. A separate arrow bypasses alloc entirely, running from arg0 down and across to a box labelled return 1 equals arg0 unchanged.</desc>
<text class="vx-text" x="320" y="26" text-anchor="middle">Before bufferization: tensor values (SSA)</text>
<text class="vx-text" x="320" y="256" text-anchor="middle">After One-Shot Bufferize: memrefs</text>
<line class="vx-line" x1="10" y1="240" x2="630" y2="240"/>
<rect class="vx-box" x="30" y="60" width="140" height="50" rx="4"/>
<text class="vx-mono" x="100" y="82" text-anchor="middle">%t</text>
<text class="vx-text-muted" x="100" y="100" text-anchor="middle">tensor&lt;8xf32&gt;</text>
<rect class="vx-box" x="30" y="150" width="140" height="50" rx="4"/>
<text class="vx-mono" x="100" y="172" text-anchor="middle">%patch</text>
<text class="vx-text-muted" x="100" y="190" text-anchor="middle">tensor&lt;4xf32&gt;</text>
<rect class="vx-box-strong" x="215" y="100" width="210" height="50" rx="4"/>
<text class="vx-mono" x="320" y="130" text-anchor="middle">tensor.insert_slice</text>
<rect class="vx-box" x="470" y="60" width="140" height="50" rx="4"/>
<text class="vx-mono" x="540" y="82" text-anchor="middle">%patched</text>
<text class="vx-text-muted" x="540" y="100" text-anchor="middle">tensor&lt;8xf32&gt;</text>
<rect class="vx-box" x="470" y="150" width="140" height="50" rx="4"/>
<text class="vx-mono" x="540" y="172" text-anchor="middle">%t</text>
<text class="vx-text-muted" x="540" y="190" text-anchor="middle">returned again</text>
<line class="vx-line" x1="170" y1="85" x2="215" y2="115"/>
<polygon class="vx-arrowhead" points="206,109 206,121 218,115"/>
<line class="vx-line" x1="170" y1="175" x2="215" y2="140"/>
<polygon class="vx-arrowhead" points="206,134 206,146 218,140"/>
<line class="vx-line" x1="425" y1="115" x2="470" y2="85"/>
<polygon class="vx-arrowhead" points="459,80 459,92 471,85"/>
<path class="vx-line" d="M100,60 L100,42 L540,42 L540,150"/>
<polygon class="vx-arrowhead" points="533,141 547,141 540,152"/>
<text class="vx-text-muted" x="320" y="38" text-anchor="middle">%t used again, unchanged</text>
<rect class="vx-box" x="30" y="280" width="140" height="50" rx="4"/>
<text class="vx-mono" x="100" y="302" text-anchor="middle">%arg0</text>
<text class="vx-text-muted" x="100" y="320" text-anchor="middle">memref&lt;8xf32&gt;</text>
<rect class="vx-box" x="30" y="370" width="140" height="50" rx="4"/>
<text class="vx-mono" x="100" y="392" text-anchor="middle">%arg1</text>
<text class="vx-text-muted" x="100" y="410" text-anchor="middle">memref&lt;4xf32&gt;</text>
<rect class="vx-box-accent" x="220" y="325" width="200" height="50" rx="4"/>
<text class="vx-mono" x="320" y="347" text-anchor="middle">%alloc</text>
<text class="vx-text-muted" x="320" y="365" text-anchor="middle">memref&lt;8xf32&gt;, new</text>
<rect class="vx-box" x="470" y="280" width="140" height="50" rx="4"/>
<text class="vx-mono" x="540" y="302" text-anchor="middle">return 0</text>
<text class="vx-text-muted" x="540" y="320" text-anchor="middle">= %alloc</text>
<rect class="vx-box" x="470" y="370" width="140" height="50" rx="4"/>
<text class="vx-mono" x="540" y="392" text-anchor="middle">return 1</text>
<text class="vx-text-muted" x="540" y="410" text-anchor="middle">= %arg0, unchanged</text>
<text class="vx-text-accent" x="195" y="296" text-anchor="middle">memref.copy</text>
<text class="vx-text-muted" x="195" y="312" text-anchor="middle">the forced copy</text>
<line class="vx-flow" x1="170" y1="310" x2="220" y2="340"/>
<polygon class="vx-arrowhead" points="211,332 211,346 223,339"/>
<line class="vx-line" x1="170" y1="390" x2="220" y2="360"/>
<polygon class="vx-arrowhead" points="211,353 211,367 223,360"/>
<text class="vx-text-muted" x="195" y="425" text-anchor="middle">write into [0:4]</text>
<line class="vx-line" x1="420" y1="340" x2="470" y2="305"/>
<polygon class="vx-arrowhead" points="459,299 459,311 471,305"/>
<path class="vx-line" d="M100,330 L100,430 L540,430 L540,420"/>
<polygon class="vx-arrowhead" points="533,422 547,422 540,410"/>
</svg>
<figcaption>Figure 1. Bufferizing <code>patch_then_reread</code> (<code>patch_conflict.mlir</code>). <code>%t</code> is read twice after the write: once through <code>tensor.insert_slice</code>, once as the second return value. One-Shot Bufferize keeps <code>%arg0</code> as <code>%t</code>'s own buffer for that direct return, but copies it into a fresh <code>%alloc</code> before writing the patch, so the write never clobbers the value the second return still needs.</figcaption>
</figure>

??? check "A function chains three linalg.generic ops, each writing into its own fresh tensor.empty destination and consuming only the previous op's result. Before reading on: how many memref.alloc operations does bufferize-function-boundaries produce?"

    Three, one per `tensor.empty`, checked locally with `mlir-opt` 18.1.8 on 2026-09-24. Each op's destination is a distinct placeholder with no relationship to any other op's destination, so nothing in the IR tells the pass those three buffers could be the same piece of memory; One-Shot Bufferize resolves in-place-or-copy per destination, not by searching for buffers to reuse across a whole chain. A pass built to shrink that count exists, but it runs separately, after bufferization, not as part of the analysis this chapter covers.

## Crossing the tensor and memref boundary by hand

`bufferize-function-boundaries` converts a whole function's signature at once. Sometimes a program instead mixes code that already speaks memrefs, such as a hand-written runtime routine, with code still written in tensors, and the two must meet inside one function without running the whole-module pass across both. Two operations cross that boundary explicitly:

--8<-- "includes/examples/mlir/m7-bufferization/tensor_memref_bridge.mlir.md"

`bufferization.to_tensor` takes a memref and returns a tensor SSA value that reads it once, as of that point in the program; `bufferization.to_memref` goes the other way, handing back the buffer a tensor value would occupy.[^bufferization] MLIR 18.1.8 names the second op `to_memref`; the same documentation page, read today, calls it `to_buffer`, a rename from a newer MLIR than the one pinned for this book. Both names describe the same operation.

A function that calls a hand-written runtime routine taking a memref, from code otherwise written entirely in tensors, is the ordinary reason to reach for these two ops: the routine's signature is fixed, memref in, and `to_tensor` is what lets the rest of the function keep treating its result as an ordinary immutable value instead of a buffer to track manually. These two ops are an escape hatch, not the normal path: `first_element` and `as_memref` above are checked only by parsing and verifying, never by running One-Shot Bufferize's analysis over them. Checked locally (`mlir-opt` 18.1.8, 2026-09-24): handing that analysis a bare `to_tensor` like the one above fails, `'bufferization.to_tensor' op to_tensor ops without restrict are not supported by One-Shot Analysis`, until the op carries a `restrict` attribute. `restrict` is "similar to the C restrict keyword": it promises that no other `to_tensor` in the function reads an aliasing memref, the same shape of promise as C's keyword and as Vortex's own [decision 25](../decisions/references.md#d25), and for the same reason. Only with that promise stated can the analysis reason about the tensor IR alone, the way it does for `patch_conflict.mlir`, without having to track every memref a program might have handed it from outside.

??? check "Why can One-Shot Bufferize's whole-function analysis reason about patch_conflict.mlir without ever seeing a restrict attribute, when a bare to_tensor needs one?"

    Because `patch_then_reread` never crosses the tensor and memref boundary by hand. Every value in it starts as a tensor argument and ends as a tensor result; the analysis sees a complete, closed set of tensor uses and can trace every read of `%t` itself. A `to_tensor` op reads a memref that arrived from outside that closed picture, one the analysis was never asked to bufferize, so nothing tells it whether some other operation already holds an alias of the same memory. `restrict` is how the writer of the IR supplies the fact the analysis cannot derive on its own.

## For Vortex

!!! vortex "Exercise"

    **Write**, on paper, a destination-passing reading of the stage 10 matrix multiplication kernel: for each of its three loops and its one `f32` accumulation, name the value that plays the role `outs` plays for `linalg.generic`, and say whether it is the function's own `&mut` parameter or a value your note invents to stand in for one (the way `tensor.empty` stands in for `scale`'s destination).

    1. A short table: for the kernel's output array `c`, decide whether the whole array is one destination or each row (or each element) is its own, and justify the choice using what this chapter's examples showed about which operand a write targets.
    2. For the accumulation `sum += a[row, k] * b[k, column]`, decide whether its running sum is a destination in the DPS sense at all, or a loop-carried value with no memory identity until the loop finishes, the way `suffix_sum.mlir` in [M6](m6-affine-and-scf.md) threaded a sum with no memref in sight. State which one it is and why.
    3. Using the read-after-write reasoning from `patch_conflict.mlir`: does anything in the kernel read `a`, `b`, or the already-written part of `c` again after some other part of the kernel has written to it? Write down every read that happens after a write to the same array, or state that none exists.
    4. From that answer, predict whether a real bufferization of this kernel would need any `memref.copy` at all, beyond `c` itself already being a `&mut` buffer with no tensor step involved. Give your reasoning, not a running compiler's answer.

    **Not yet:** writing any part of a lowering from Vortex's own IR to `linalg` or `tensor` (that is a full compiler pass, not an exercise), running the kernel through `mlir-opt --one-shot-bufferize` yourself (nothing here produces that IR to feed it), and any change to `src/`.

    **Proof that it works:** a one-page note covering all four points above, each answer tied to a specific line of reasoning from this chapter (destination-passing style, read-after-write conflicts, or the loop-carried-value idea from M6), not to a claim about what a compiler happens to do.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is a tensor, and how is it different from a memref?** An immutable SSA value with a shape and an element type, no address and no layout a program can observe; a memref is a reference to a region of memory that can be read, written and aliased.
    - **What does destination-passing style add to an operation's operands?** A tensor operand, `outs`, that names the value the result should be understood to overwrite, so bufferization has one place to look when deciding whether a result can reuse its destination's buffer.
    - **What does `tensor.empty` mean?** A tensor of a given shape with no promised contents, used as a destination operand when a computation has no other value to write into.
    - **What decides whether One-Shot Bufferize writes a result in place or copies first?** Whether any use of the value the write would overwrite survives past that point: a read-after-write conflict forces a copy; no surviving read allows an in-place write.
    - **What does `bufferize-function-boundaries` change that the default pass does not?** It converts a function's own tensor parameters and results into memrefs; by default the pass bufferizes only a function's body and leaves its signature alone.
    - **What does the `restrict` attribute on `bufferization.to_tensor` promise, and why does the analysis need it?** That no other `to_tensor` in the function reads an aliasing memref, the same shape of promise as C's `restrict` keyword, needed because the analysis otherwise cannot see past the boundary a hand-written `to_tensor` crosses.

## Where this comes back

!!! next "You will use this again in"

    - [M8. Vectorization in MLIR](m8-vectorization.md): *memref*, *in-place write*, *destination-passing style*
    - [M9. Schedules as IR: the transform dialect](m9-transform-dialect.md): *One-Shot Bufferize*, *tensor.empty*, *outs*
    - [M10. MLIR for GPUs](m10-mlir-for-gpus.md): *memref*, *allocation*, *function-boundary bufferization*
    - [O9. Memory: alias analysis and MemorySSA](../optimize/o9-alias-analysis.md): *read-after-write conflict*, *aliasing*, *whole-function analysis*

## Sources and further reading

The Bufferization page is the primary reference for this chapter: its sections on destination-passing style, on the tensor and buffer boundary, and on function-boundary bufferization cover every mechanism this chapter names.[^bufferization] The tensor and memref dialect pages document the individual operations the examples use.[^tensor][^memref] [M5](m5-structured-ops.md) covers `linalg.generic`, indexing maps and `outs` in more depth; read it alongside this chapter if the `scale` example's syntax is unfamiliar.

[^bufferization]: MLIR Project, "Bufferization". <https://mlir.llvm.org/docs/Bufferization/>
[^tensor]: MLIR Project, "'tensor' Dialect". <https://mlir.llvm.org/docs/Dialects/TensorOps/>
[^memref]: MLIR Project, "'memref' Dialect". <https://mlir.llvm.org/docs/Dialects/MemRef/>
