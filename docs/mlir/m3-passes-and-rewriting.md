# M3. Passes and pattern rewriting

<p class="page-intro">MLIR transforms a module by running small, focused passes over it, and most of those passes work the same way: a pattern names a shape of IR it wants to replace, and a driver applies every matching pattern until none apply. This chapter teaches that loop, the canonicalization pass built on it, and the anchored, nesting pass manager that decides where each pass runs, so that Vortex's own emitted IR can be cleaned up the same way.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 30 minutes · Builds on: [M2. Reading MLIR](m2-reading-mlir.md), [O6. Redundancy: CSE, GVN, PRE and LICM](../optimize/o6-redundancy.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is every instruction, loop and function in MLIR, underneath?"

        An operation: results, a name, operands, successors, properties, regions, attributes, a type and a location.

        Introduced in [M2. Reading MLIR](m2-reading-mlir.md).

    ??? question "How do regions nest inside an operation, and which values can code inside one use?"

        A region holds blocks; an operation such as `func.func` or `scf.for` owns one or more regions as one of its parts. Code inside a region may use any value whose definition dominates it in that region, plus values from enclosing regions unless an enclosing operation is isolated from above.

        Introduced in [M2. Reading MLIR](m2-reading-mlir.md).

    ??? question "What does common subexpression elimination remove, and what must it prove first?"

        A second computation of a value already computed earlier on every path that reaches it. It must prove the two computations are the same operation on the same operands, and that nothing could have changed the result in between.

        Introduced in [O6. Redundancy: CSE, GVN, PRE and LICM](../optimize/o6-redundancy.md).

    ??? question "May a Vortex compiler turn `sum += a[row, k] * b[k, column]` into one fused multiply-add?"

        No. Each `f32` operation is one IEEE 754 operation, rounded to nearest with ties to even, and a compiler must not contract, reassociate or reorder them. The product is rounded, then the sum.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Explain what a rewrite pattern matches and replaces, and what it means for the greedy driver to reach a fixed point.
    - Recognize which rewrites belong in canonicalization and which do not.
    - Predict, from a pipeline's text, which operation a pass runs on and which passes see each other's output.
    - Read `mlir-opt`'s own before/after output to tell two passes' contributions apart.

## A pattern is a small, local rewrite

Start with one function and one flag. It adds a constant to a parameter, and separately multiplies another value by one that is never used again:

--8<-- "includes/examples/mlir/m3-passes-and-rewriting/identities.mlir.md"

Two things disappear. `%kept`'s addition of zero becomes just `%x`, because `arith.addi %v, 0` and `%v` are the same value: this is an **identity elimination**, a rewrite that replaces an operation with one of its own operands because the operation was a no-op for these particular constants. `%unused` disappears entirely, because once nothing uses its result and it has no side effect, keeping it around changes nothing about the program: this is **dead code elimination**, the same idea O5 gives it on the CPU side.[^o5] Neither rewrite needed to know what the function was for. Each looked at one operation, matched a shape (`add` with a zero operand; a result no other operation reads), and replaced it.

That is a **rewrite pattern**: code that matches a small piece of IR, usually one operation and sometimes a couple of its neighbors, and replaces it with something else, or with nothing. MLIR's pattern-rewriting infrastructure asks a pattern for two things: a `match` that either fails or commits to a rewrite, and the rewrite itself, performed through a `PatternRewriter` so the surrounding IR stays consistent (uses updated, dead operations erased).[^rewriter] The framework calls this method `matchAndRewrite`, and requires that the match succeed before any IR is touched: a pattern is not allowed to mutate anything and then report failure.[^rewriter] Each pattern also carries a **benefit**, a small integer that says how good the rewrite is expected to be, used only to break ties when more than one pattern could fire on the same operation.[^rewriter]

`--canonicalize` alone found both rewrites above without being told to look for either one specifically, because canonicalization is not one pattern: it is every canonicalization pattern every loaded dialect has registered, applied together.[^canon] `arith` registers the identity eliminations for its own operations; the "operation has no uses and no side effects, so erase it" rule is not even dialect-specific, it is part of the driver itself.

## The driver runs to a fixed point

A single pass over the operations once would have missed something here. Suppose the driver visited `%unused` before `%kept` was simplified, decided `%unused` had a use (the multiply's result did, until `%kept`'s rewrite made it irrelevant... in this particular function it does not, but in general one rewrite often creates the conditions for another). MLIR's answer is the **greedy pattern rewrite driver**: a worklist of operations to (re)examine, seeded with every operation in the region, which processes the worklist by applying whichever matching pattern has the highest benefit at each operation, and pushes an operation's neighbors back onto the worklist whenever it changes, because a neighbor that did not match before might match now.[^rewriter] It keeps going "until a fixed point is reached", meaning no operation in the worklist matches any pattern any more, or until a maximum number of rewrites is hit, a safety valve against a buggy pattern that loops forever.[^rewriter] Canonicalization is exactly this driver, run with the union of every registered canonicalization pattern.[^canon]

A fixed point is a useful guarantee: it means the order patterns happened to fire in does not change the final answer, only how many steps it took to get there, so long as the patterns themselves are confluent (any order of applying them reaches the same result). It does not mean two *different* passes reach a fixed point together just because each one does separately. The next example is one function, one command, but two passes:

--8<-- "includes/examples/mlir/m3-passes-and-rewriting/expose_duplicates.mlir.md"

`%a` and `%b` both compute `%x + 5`, but `%a` gets there by adding two constants first and `%b` names `5` directly, so as written they are not the same operation. **Common subexpression elimination** (CSE) removes a computation that repeats one already available, but it works by comparing operations structurally, same name and same operands, not by reasoning about arithmetic.[^o6] Run alone, CSE cannot see that `%a` and `%b` agree; O6 makes the same point about value numbering needing operations to look alike before it can prove they compute the same thing.[^o6] Canonicalize's constant folding runs first in this command, replaces `2 + 3` with the literal `5`, and rewrites `%a` to the same `arith.addi %x, 5` shape as `%b`. Only then does CSE, running second in the same invocation, find the duplicate and remove it. Run `--cse` by itself on this file and `%a` and `%b` stay two separate additions; canonicalize does not do CSE's job either, because deduplicating identical operations across a region is not a local, per-operation rewrite. Composing the two passes finds a redundancy that neither finds alone, and in this file, the order they run in inside one command matters: canonicalize has to go first so CSE has something identical to compare.

??? check "Would swapping the two flags, `--cse --canonicalize`, give the same final IR here?"

    No. `--cse` first sees `%a` and `%b` in their original, differently-shaped forms and merges nothing. `--canonicalize` then runs on the result and folds the constants exactly as before, but the duplicate addition it now creates is never re-examined by a CSE pass, because CSE already ran. The file ends with `%a` and `%b` both present, only their constants folded. Passing `--canonicalize --cse --canonicalize` a third time would not help either: canonicalize's own patterns have no rule for "these two operands happen to be the same SSA value, replace `x + x` with something smaller", so it leaves the duplicate alone once CSE is gone from the sequence.

## Passes are anchored on an operation type

Every pass in MLIR is written against one kind of operation: an `OperationPass<OpT>` runs once for each operation of type `OpT` in the IR, and touches only that operation's own regions, never its parents or its siblings.[^passmgmt] `--canonicalize` and `--cse` both happen to be written generically enough to anchor on almost anything with a region, which is why running them with no other flags, on a whole module, worked in the two examples above: `mlir-opt` picks an anchor for you when you pass a single top-level flag. Once a pipeline needs to say *where* a pass runs, relative to another pass or relative to a specific level of nesting, it is written as text, and the text has to nest the same way the IR does: `builtin.module(func.func(canonicalize, cse))` reads as "inside the module, inside each function, run canonicalize then cse", and a `PassManager` built from that string builds one nested `OpPassManager` per level, each scoped to operations of the named type.[^passmgmt]

This nesting is not merely notation. An operation that anchors a nested pass manager must be `IsolatedFromAbove`, meaning nothing inside it can refer to a value defined outside it (`func.func` has this property; a plain `scf.for` loop does not).[^passmgmt] That is what lets MLIR run the pass managers for two different functions in parallel with no risk of one clobbering a value the other still needs: neither can see into the other's region at all. The cost of that guarantee is that a pass anchored on `func.func` cannot rewrite anything at the module level, and a pass anchored on the module cannot look inside a function's body unless it is explicitly told to walk in.

The anchor also has to be a real operation name, and getting it wrong fails quietly rather than loudly:

--8<-- "includes/examples/mlir/m3-passes-and-rewriting/wrong_anchor.mlir.md"

`builtin.module(canonicalize(cse))` parses without complaint, because `canonicalize` is a legal identifier wherever an operation name is expected, and `mlir-opt` cannot tell "the pass named canonicalize" from "an operation named canonicalize" from the string alone. What it builds is a pass manager that runs `cse` inside every operation literally named `canonicalize`, and there is no such operation anywhere in this module, so `cse` runs zero times. The module comes back unchanged: the two `arith.muli` operations are still both there. Writing the anchor as an actual operation type, `builtin.module(func.func(canonicalize, cse))`, runs `cse` where the module's functions actually live, and does dedupe the multiply. `mlir-opt` also refuses the opposite mistake outright: asking a pass manager built for `func.func` to run directly on a `builtin.module` fails with an error naming both operation types, because a pass can only run inside its anchor.

<figure class="vx-figure">
<svg viewBox="0 0 720 330" role="img" aria-label="Two pass pipelines over the same module: one anchored on func.func, one accidentally anchored on nothing" aria-describedby="m3-f1-desc">
<title id="m3-f1-title">A correct anchor against a mis-typed one</title>
<desc id="m3-f1-desc">Two side-by-side trees. Left, labelled correct: builtin.module contains two func.func boxes, each pointing to a small box holding canonicalize and cse; both fire. Right, labelled wrong: builtin.module contains the same two func.func boxes, but the pass manager text nests cse inside a box literally named canonicalize; since no operation in the module is named canonicalize, that box is empty and crossed out, and cse never runs on either function.</desc>
<text class="vx-text-accent" x="20" y="26">builtin.module(func.func(canonicalize, cse))</text>
<rect class="vx-box-strong" x="20" y="42" width="320" height="250" rx="6"/>
<text class="vx-text-muted" x="34" y="64">builtin.module</text>
<rect class="vx-box" x="40" y="80" width="130" height="90" rx="4"/>
<text class="vx-mono" x="105" y="100" text-anchor="middle">func.func @f</text>
<rect class="vx-box-accent" x="52" y="112" width="106" height="46" rx="4"/>
<text class="vx-text" x="105" y="132" text-anchor="middle">canonicalize</text>
<text class="vx-text" x="105" y="150" text-anchor="middle">cse</text>
<text class="vx-text-muted" x="105" y="180" text-anchor="middle">runs</text>
<rect class="vx-box" x="190" y="80" width="130" height="90" rx="4"/>
<text class="vx-mono" x="255" y="100" text-anchor="middle">func.func @g</text>
<rect class="vx-box-accent" x="202" y="112" width="106" height="46" rx="4"/>
<text class="vx-text" x="255" y="132" text-anchor="middle">canonicalize</text>
<text class="vx-text" x="255" y="150" text-anchor="middle">cse</text>
<text class="vx-text-muted" x="255" y="180" text-anchor="middle">runs</text>
<text class="vx-text" x="34" y="270" font-weight="600">correct: each function is cleaned up</text>
<text class="vx-text-accent" x="380" y="26">builtin.module(canonicalize(cse))</text>
<rect class="vx-box-strong" x="380" y="42" width="320" height="250" rx="6"/>
<text class="vx-text-muted" x="394" y="64">builtin.module</text>
<rect class="vx-box" x="400" y="80" width="130" height="90" rx="4"/>
<text class="vx-mono" x="465" y="100" text-anchor="middle">func.func @f</text>
<text class="vx-text-muted" x="465" y="140" text-anchor="middle">unchanged</text>
<rect class="vx-box" x="550" y="80" width="130" height="90" rx="4"/>
<text class="vx-mono" x="615" y="100" text-anchor="middle">func.func @g</text>
<text class="vx-text-muted" x="615" y="140" text-anchor="middle">unchanged</text>
<rect class="vx-box-bad" x="400" y="190" width="280" height="60" rx="4"/>
<text class="vx-mono" x="540" y="215" text-anchor="middle">operation named "canonicalize"</text>
<text class="vx-text-muted" x="540" y="233" text-anchor="middle">cse would run here, but nothing matches</text>
<text class="vx-text" x="394" y="270" font-weight="600">wrong: cse runs zero times</text>
</svg>
<figcaption>A pipeline's nesting has to name real operation types. Nesting a pass inside another pass's name, instead of inside an operation name, builds a pass manager that matches nothing.</figcaption>
</figure>

## What belongs in canonicalization, and what does not

Canonicalization is deliberately narrow. MLIR's own guidance groups its patterns into a short list: identity elimination, folding a scalar constant, folding an operation with its inverse, removing a value nothing uses, simplifying control flow that cannot actually branch, and propagating or folding casts.[^canon] Two rules keep the list that short. First, a canonicalization pattern must move the IR toward one agreed **canonical form** so that later passes and analyses can rely on it: a project should not have several equally valid spellings of "add zero" floating around for every later pass to special-case.[^canon] Second, canonicalize is not the place for a rewrite whose benefit depends on the target machine or on a cost model, or for a pattern that could itself be expensive to check: MLIR's guidance states plainly that performance improvements are not the point, and warns against patterns with worse than constant or near-constant cost, because canonicalize is meant to run often, between other passes, as cheap upkeep.[^canon] A rewrite that is only sometimes a good idea, such as tiling a loop or choosing between two instruction sequences with different latencies, belongs in a dedicated pass with its own place in the pipeline, not in canonicalize.

This is why canonicalize left the `fastmath` property alone in every example above: this chapter's inputs used integer arithmetic, which has no such property, but the same driver runs over `arith.addf` and `arith.mulf` too, and their canonicalization patterns only fold constants and remove identities, they do not add or change fast-math permissions.[^arith] A pattern that silently attached `fastmath = <fast>` to a floating-point operation to enable a fusion would not be canonicalization by MLIR's own definition: it is not free, and it is not something every later pass could assume was already done. Whether such a permission may ever be attached at all is a question decision 56 answers for Vortex before any pass gets near the IR: never.[^d56]

??? check "Canonicalize folded `%c2 = arith.constant 2` and `%c3 = arith.constant 3` into one reused `%c5_i32` even before CSE ran, in the second example. Is that CSE's job?"

    No, and it did not run CSE. Constant folding replaces an operation with a value it can compute at compile time; the pass keeps a small table of the constants it has already materialized so that folding two different expressions to the same value reuses one `arith.constant`, rather than emitting a fresh one each time. That reuse is part of canonicalize's own constant-folding machinery, not a side effect of CSE, and it is why the folded `%c5_i32` appears only once even though this chapter never ran `--cse` alone on that step.

??? check "Would it be safe to run canonicalize on a region with `--allow-unregistered-dialect` operations mixed in?"

    Canonicalize only applies the patterns registered for the operations it recognizes; an unregistered operation has none, so the driver leaves it exactly as it found it, the same way M2 found that unregistered operations get only the structural checks, nothing dialect-specific.

## Watching a pipeline work

Every example on this page ran one command and compared one printed module against another, by eye or by diff. That is enough for a two-pass pipeline on a ten-line function, and it is exactly what this book's own example checker does: one command, one expected output, byte for byte. It stops being enough once a pipeline has more than a couple of stages, because the final module no longer says which pass is responsible for which change.

`mlir-opt` has a pair of flags for that: `-mlir-print-ir-before-all` prints the IR immediately before every pass in the pipeline runs, and `-mlir-print-ir-after-all` prints it immediately after, so a pipeline of five passes produces five (or ten) labelled snapshots instead of one final answer.[^passmgmt] Pointed at the two-pass pipeline from the earlier example, `-mlir-print-ir-after-all` would print the module once with `%a` and `%b` already folded to the same shape (canonicalize's output) and once more with `%a` gone and every use of it rewritten to `%b` (cse's output), and the two snapshots are the direct record of which pass did which half of the work, rather than something to infer from reading the source of two passes side by side. The same flags are how you would confirm, on your own machine and your own pinned MLIR version, a claim this chapter only asserts from having run it once here: that swapping `--cse` and `--canonicalize` really does leave the duplicate addition behind, and exactly where in the pipeline it survives.

This chapter's own examples do not use these flags, because the book's checker compares one exact stdout against one saved file, and a full before/after trace is easy to make non-deterministic (pass names, internal identifiers, timing) or simply long, for a two-pass pipeline where the two separate commands already show the same story with less output. Reach for `-mlir-print-ir-after-all` when a pipeline is large enough, or new enough, that you no longer trust yourself to predict which pass produced a given line of IR; keep it out of a golden test, where a smaller, exact, single command is easier to keep passing across an MLIR upgrade.

## For Vortex

!!! vortex "Exercise"

    **Build** a golden-file test around the MLIR your [M2 tool](m2-reading-mlir.md#for-vortex) already emits for the stage 10 kernel: run `mlir-opt --canonicalize` and, separately, `mlir-opt --canonicalize --cse`, and save each output next to the original.

    1. Before running either command, list every `arith.addf` and `arith.mulf` in the emitted file and write down, for each, whether canonicalize is allowed to touch it at all. [Decision 56](../decisions/numbers.md#d56) forbids fusing or reordering the multiply and the add in the kernel's inner product, so the property to check afterward is narrow: does `fastmath` still read `none` on every one of them, and is the count of floating-point operations unchanged?
    2. The kernel's loop bounds and array indices are ordinary `index` arithmetic around [fixed, compile-time shapes](../decisions/arrays.md#d11). Predict which of those index computations canonicalize's identity and constant-folding patterns could legally simplify, run the tool, and check your prediction against the diff instead of reading it after the fact.
    3. The kernel writes into its `&mut` output inside the loop. CSE only removes an operation that repeats one already computed; a store is not an SSA value with a result to compare, so two stores are never candidates for merging regardless of their addresses. Write one paragraph connecting that fact to [decision 25](../decisions/references.md#d25): explain why an optimizer that never merges or reorders stores by default is exactly what a promise like "no argument aliases the `&mut` output" needs from the passes that run before your own back end sees the code.
    4. **Not yet:** no new pattern, dialect or pass of your own; this exercise only runs `mlir-opt`'s existing `--canonicalize` and `--cse` over a file your M2 tool already produces, and only reasons about what came back.

    **Proof that it works:**

    - Three saved files: the emitted kernel, its `--canonicalize` output and its `--canonicalize --cse` output, each still accepted by plain `mlir-opt` with no options.
    - A test that scans all three for any floating-point `arith` operation whose `fastmath` is not `none`, and fails if it finds one.
    - A written prediction from step 2, checked line by line against the real diff, with any mismatch explained rather than deleted.
    - A canary: hand-edit the emitted file so one `arith.mulf` in the inner product carries `fastmath = <contract>` instead of `none`, rerun `--canonicalize`, and confirm the flag survives unchanged, so you know your fastmath check would actually catch a real emitter bug and is not passing by accident.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does a rewrite pattern do?** It matches a small shape of IR, usually one operation, and either replaces it or leaves it alone; it must not mutate anything unless the match succeeds.
    - **What does the greedy driver guarantee?** That it keeps applying matching patterns, re-examining any operation whose neighbor just changed, until none match: a fixed point, reached in some order that does not affect the final IR as long as the patterns agree with each other.
    - **Why can canonicalize alone not do CSE's job?** Its patterns look at one operation's own shape and operands; deduplicating two differently-built but equal computations across a region needs a pass built for that comparison.
    - **What must every operation that anchors a nested pass manager be?** Isolated from above: nothing inside it may refer to a value defined outside it, which is what lets separate instances run without interfering.
    - **What happens when a pipeline's text nests a pass inside a name that is not a real operation type?** Nothing fails; the inner pass simply matches zero operations and the IR comes back unchanged.
    - **What two properties keep a rewrite out of canonicalization even if it is correct?** Depending on a cost model or the target, and costing more than the driver can afford to run repeatedly between every other pass.

## Where this comes back

!!! next "You will use this again in"

    - [M4. Dialect conversion and lowering to LLVM](m4-dialect-conversion.md): *ConversionPattern*, *legal and illegal operations*, *anchored pass*
    - [M5. Structured ops: linalg, tensor and memref](m5-structured-ops.md): *named op canonicalizers*, *fusion as a pattern*
    - [M9. Schedules as IR: the transform dialect](m9-transform-dialect.md): *explicit schedule versus a greedy driver's implicit one*
    - [O5. Constants and dead code](../optimize/o5-constants-and-dead-code.md): *constant folding*, *dead code elimination*, *fixed point*
    - [O6. Redundancy: CSE, GVN, PRE and LICM](../optimize/o6-redundancy.md): *structural comparison versus value numbering*
    - [O10. Pass managers and pipelines](../optimize/o10-pass-pipelines.md): *pass ordering*, *nesting*, *legality between passes*
    - [O12. Testing an optimizer](../optimize/o12-testing-optimizers.md): *golden output*, *canary tests*, *round trip*

## Sources and further reading

For depth, read the Pattern Rewriting document's sections on the greedy driver and on writing a pattern's `matchAndRewrite`, then the Pass Infrastructure document's sections on `OperationPass` and pipeline text, both with this chapter's three files open and `mlir-opt`'s own before/after output next to them.[^rewriter][^passmgmt] The Canonicalization document is short and worth reading whole before writing any pattern of your own, MLIR's or otherwise: it is as much a style guide for what belongs in a cheap, repeated cleanup pass as it is a description of the mechanism.[^canon]

[^rewriter]: MLIR Project, "Pattern Rewriting: Generic DAG-to-DAG Rewriting", sections "RewritePattern", "PatternRewriter", "PatternBenefit" and "Applying patterns" (the greedy pattern rewrite driver). <https://mlir.llvm.org/docs/PatternRewriter/>
[^canon]: MLIR Project, "Operation Canonicalization", sections "General Design" and "What is the Canonical Form?". <https://mlir.llvm.org/docs/Canonicalization/>
[^passmgmt]: MLIR Project, "Pass Infrastructure", sections "OperationPass", "OpPassManager" and "Textual Pass Pipeline Specification". <https://mlir.llvm.org/docs/PassManagement/>
[^arith]: MLIR Project, "'arith' Dialect", entries `arith.addf`, `arith.mulf`, `FastMathFlagsAttr` and `FastMathFlags`. <https://mlir.llvm.org/docs/Dialects/ArithOps/>
[^o5]: This chapter's O5, [Constants and dead code](../optimize/o5-constants-and-dead-code.md).
[^o6]: This chapter's O6, [Redundancy: CSE, GVN, PRE and LICM](../optimize/o6-redundancy.md).
[^d56]: [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).
