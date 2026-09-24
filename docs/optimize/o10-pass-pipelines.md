# O10. Pass managers and pipelines

<p class="page-intro">A compiler does not run one optimization: it runs dozens, in an order someone chose, over a program broken into module, function and loop pieces that nest inside one another. This chapter is about the machinery that runs them: what a pass manager schedules, what it remembers between passes, and why the order it was given changes the answer.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 45 minutes · Builds on: [O1. The optimizer's contract](o1-optimizer-contract.md), [O7. Calls and aggregates: inlining and SROA](o7-inlining-and-sroa.md), [O8. Loops: structure, induction variables and bounds checks](o8-loops.md), [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does a passed, missed or analysis remark report?"

        A passed remark reports a transformation that was made; a missed remark reports one that was attempted and not made; an analysis remark reports something a pass worked out, often the reason behind a missed remark.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#remarks-the-optimizers-report).

    ??? question "Why does LLVM's inliner visit functions bottom-up over the call graph, rather than in source order?"

        So that by the time it measures a call site, the callee has already been inlined into and simplified as much as it will be: a callee visited bottom-up is final, so its measured size and behavior do not change later.

        Introduced in [O7. Calls and aggregates: inlining and SROA](o7-inlining-and-sroa.md#deciding-in-order-bottom-up-over-the-call-graph).

    ??? question "What three things does loop simplify form guarantee about a loop?"

        A preheader (a single entering block whose only successor is the header), a single latch (one back edge), and dedicated exits (every exit block's predecessors lie inside the loop). LoopSimplify adds whatever is missing.

        Introduced in [O8. Loops: structure, induction variables and bounds checks](o8-loops.md#the-shape-of-a-loop).

    ??? question "What does MemorySSA's walker return for a load, and why is that answer worth keeping around?"

        The nearest access above the load that may write the location it reads, after skipping stores that cannot touch it. Building that answer walks the whole function, so a pass that can leave it valid, and says so, saves every later pass from asking again.

        Introduced in [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md#the-walker).

!!! goals "In this chapter"

    - Explain why the same two passes can produce two different results depending on which one runs first, and name the mechanism.
    - Recognize LLVM's four IR units, module, CGSCC, function and loop, and what an adaptor does to run a pass written for one of them inside a pipeline built for another.
    - Read and write the `-passes=` pipeline text, including its nested and auto-promoted forms.
    - Explain what `PreservedAnalyses` is, why a pass manager needs it, and what a pass promises when it returns `PreservedAnalyses::all()`.
    - Connect a language's choice of pipeline, not only its choice of passes, to what the vectorizer is able to prove about the matrix kernel.

## One pipeline, two passes, one surprise

Here is a function and its only caller, already lowered to LLVM IR, exactly as this chapter's first example has it:

```llvm
define internal i32 @classify(i32 %x) {
entry:
  %pos = icmp sgt i32 %x, 0
  br i1 %pos, label %then, label %else
then:
  %doubled = mul i32 %x, 2
  br label %join
else:
  %negated = sub i32 0, %x
  br label %join
join:
  %result = phi i32 [ %doubled, %then ], [ %negated, %else ]
  ret i32 %result
}

define i32 @caller() {
entry:
  %r = call i32 @classify(i32 10)
  ret i32 %r
}
```

`caller` always passes 10. Ask two questions about this program separately and neither gets far. "Can `classify`'s branch be folded?" No: `%x` is a parameter, and a function pass looking only at `classify` has no idea what any caller passes it. "Can the call in `caller` be simplified?" No: `classify` is opaque from outside, a black box that returns an `i32`.

Put the two questions behind one pass, and the second question answers the first. `opt`'s inliner does not only copy `classify`'s body into `caller`; after it finishes a component of the call graph, it runs a small function-simplification pipeline, instcombine and SROA among its passes, over every function that pipeline changed.[^llvm-pipelines] Once `classify`'s body sits inside `caller`, `%x` is no longer a parameter: it is the constant 10, wherever the inlined copy used it. `%pos` folds to `true`, the branch folds away, and `%doubled` folds to `20`.

--8<-- "includes/examples/optimize/o10-pass-pipelines/phase_order.ll.md"

One `-passes=inline` and the whole function is `ret i32 20`. No instcombine was named in the pipeline text; the inliner's own cleanup did that work, because it runs cleanup on exactly the functions inlining touched, immediately, before anything else gets a turn.[^llvm-pipelines] Run instcombine or SCCP on `caller` by itself, before inlining, and neither pass can see past the call: there is nothing yet to fold. This is **phase ordering**: what one pass can prove often depends on what an earlier pass already exposed, and a compiler's pipeline is the record of which passes run in which order, and how many times.

??? check "Why does neither instcombine nor SCCP fold `caller`'s call, run alone, before inlining?"

    Both are function passes: each looks at one function's body and the values already in it. `%x` inside `classify` is a parameter with no known value until something substitutes it, and `caller`'s only view of `classify` is an opaque call, whose return value is unknown until something looks inside. Folding needs both facts in one function at once, which is exactly what inlining creates.

## What a pass manager does

Every optimizer this book has covered so far is one pass: a piece of code that reads a program, decides something, and sometimes rewrites it. A real compiler runs many of them, and something has to decide four things a single pass never does on its own:

- **What order** the passes run in, since [the last section](#one-pipeline-two-passes-one-surprise) showed the order changes the answer.
- **Which piece of the program** each pass sees: an alias query in [O9](o9-alias-analysis.md) needs the whole function; a loop pass needs one loop.
- **Which analyses are still valid** after a pass runs, so that a later pass which needs, say, MemorySSA does not have to rebuild it from nothing if the pass before it made no change that could have invalidated it.
- **When to stop**, since a pipeline that keeps finding new work to do would never finish.

The component that answers these questions is the **pass manager**. LLVM's current one, the **new pass manager**, replaced an older design whose central weakness was the third point: a pass running inside a call-graph walk had no supported way to ask for a fresh analysis of some other function, which is exactly what a good inliner needs to know a callee's cost after that callee has itself changed.[^npm-blog] The new pass manager makes every dependency explicit: a pass declares which analyses it uses, and returns a value that declares which analyses survive it.

That returned value is `PreservedAnalyses`. A transform pass's `run` method has the shape `PreservedAnalyses run(IRUnit &Unit, AnalysisManager<IRUnit> &AM)`, and its return value is the pass manager's only way of finding out what happened.[^writing-pass] A pass that touched nothing returns `PreservedAnalyses::all()`, the same value LLVM's own tutorial pass returns when it only prints a report: "all analyses (for example dominator tree) are still valid after this pass since we didn't modify any functions."[^writing-pass] A pass that rewrote the program without any promises returns `PreservedAnalyses::none()`, and everything cached about that function is thrown away and rebuilt the next time something asks for it. In between, a pass can name exactly the analyses it left alone, such as a transform that changes values but never touches control flow, and preserve the dominator tree while discarding the rest.

A minimal version of the same idea, with no LLVM involved, makes the trade concrete:

--8<-- "includes/examples/optimize/o10-pass-pipelines/preserved.cpp.md"

`add_one` changes every value, so `sorted` survives; it never reorders anything. `reverse` changes no value, so `sum` survives; it only reorders. Neither transform recomputes an analysis its own change could not have touched, and the analysis it might have touched is not looked at again until something asks. Multiply this by dozens of real passes and hundreds of analyses, and the saving is the whole reason a pass manager tracks preservation at all: a function that ran through ten passes that all preserved the dominator tree built it once.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Two cached analyses, sorted and sum, tracked across two transforms" aria-describedby="o10-f1-desc">
<title id="o10-f1-title">An analysis cache surviving the transform that cannot have broken it</title>
<desc id="o10-f1-desc">Three columns, labelled before, after add_one and after reverse. Two rows, labelled sorted and sum. Each cell is a shelf: lit when the cached value from the previous column is still valid and was reused, dim with a recompute mark when the transform invalidated it and it had to be built again. Before: both lit, both just computed. After add_one, which preserves sorted: sorted stays lit and reused; sum is dim, recomputed. After reverse, which preserves sum: sum stays lit and reused; sorted is dim, recomputed.</desc>
<text class="vx-text" x="16" y="28">before</text>
<text class="vx-text" x="296" y="28">after add_one</text>
<text class="vx-text" x="546" y="28">after reverse</text>
<text class="vx-text-muted" x="16" y="48">(preserves sorted)</text>
<text class="vx-text-muted" x="296" y="48">(preserves sorted)</text>
<text class="vx-text-muted" x="546" y="48">(preserves sum)</text>
<line class="vx-line" x1="246" y1="16" x2="246" y2="284"/>
<line class="vx-line" x1="496" y1="16" x2="496" y2="284"/>
<text class="vx-text" x="16" y="110">sorted</text>
<text class="vx-text" x="16" y="220">sum</text>
<rect class="vx-box-accent" x="86" y="80" width="140" height="46" rx="4"/>
<text class="vx-mono" x="156" y="108" text-anchor="middle">computed</text>
<rect class="vx-box-accent" x="86" y="190" width="140" height="46" rx="4"/>
<text class="vx-mono" x="156" y="218" text-anchor="middle">computed</text>
<rect class="vx-box-accent" x="336" y="80" width="140" height="46" rx="4"/>
<text class="vx-mono" x="406" y="108" text-anchor="middle">reused</text>
<rect class="vx-box" x="336" y="190" width="140" height="46" rx="4"/>
<text class="vx-mono" x="406" y="218" text-anchor="middle">recomputed</text>
<rect class="vx-box" x="586" y="80" width="140" height="46" rx="4"/>
<text class="vx-mono" x="656" y="108" text-anchor="middle">recomputed</text>
<rect class="vx-box-accent" x="586" y="190" width="140" height="46" rx="4"/>
<text class="vx-mono" x="656" y="218" text-anchor="middle">reused</text>
</svg>
<figcaption>Figure 1. The two analyses from the C++ example, tracked across two transforms. A lit shelf is a cached value the next query reused without recomputing it; a dim shelf is one the transform's own <code>PreservedAnalyses</code> answer forced back to unknown, paid for again on the next question.</figcaption>
</figure>

## Four places a pass can run

An LLVM pass does not run over "the program"; it runs over one **IR unit**, and there are four of them, nested inside one another: a **module**, the whole file; a **CGSCC**, one strongly connected component of the call graph, the unit [O7](o7-inlining-and-sroa.md#deciding-in-order-bottom-up-over-the-call-graph) built the bottom-up inlining order from; a **function**; and a **loop**, nested inside the function that contains it. The IR hierarchy is module, then optionally CGSCC, then function, then loop.[^npm] A pass declares which unit it wants, once, in its own code, and never has to know how it got there.

Getting it there is the job of an **adaptor**: a wrapper that runs a pass meant for a smaller unit at every position of a larger one. `createFunctionToLoopPassAdaptor` lets a loop pass run inside a function pass manager, by finding every loop in the function and running the loop pass on each; `createModuleToFunctionPassAdaptor` and `createModuleToPostOrderCGSCCPassAdaptor` do the equivalent for functions and call-graph components inside a module.[^npm] A pass author writes `licm`'s `run` method once, against a loop; the adaptor is what makes `-passes='loop-mssa(licm)'` visit every loop of every function of the module in turn.

An adaptor does one more thing besides finding the smaller units: it makes sure each one is in the shape the pass it wraps expects. [O8](o8-loops.md#the-shape-of-a-loop) named that shape for loops, loop simplify form, a preheader, a single latch and dedicated exits, and said the pass managers add it automatically before any loop pass runs. This chapter's second example is a loop whose source text has no preheader at all, two separate blocks branch straight into its header from outside the loop, run through nothing but `-passes='loop-mssa(licm)'`:

--8<-- "includes/examples/optimize/o10-pass-pipelines/loop_adaptor.ll.md"

The pipeline text names one pass, `licm`, nested one level under `loop-mssa`. The output has a block, `header.preheader`, that the source never wrote. `FunctionToLoopPassAdaptor` built it, merging the loop's two outside edges into one, before `licm` ever ran, exactly the canonicalization [O8](o8-loops.md#the-shape-of-a-loop) described happening "automatically." Once the loop has a single preheader, `licm` has somewhere to put `%ksq`, a value that does not change between iterations, and moves it there, out of the loop it was written inside. A `licm` that had to check for a preheader itself, and build one when missing, would duplicate work every loop pass needs; the adaptor does it once, in one place, for all of them.

??? check "`inline` is a CGSCC pass and `licm` is a loop pass. What text would you write to run `inline` and then, inside every resulting function, `licm`?"

    `-passes='inline,function(loop-mssa(licm))'`, or, since a bare loop-level pass name is promoted to the nearest enclosing level automatically, the shorter `-passes='inline,loop-mssa(licm)'` parses to the same nesting. `inline` needs no `cgscc(...)` wrapper for the same reason: naming it at the top level already promotes it to the level it belongs to.

## The pipeline as text

`opt -passes=` takes a string, not a list of flags, and the string can be as flat as a comma-separated run of names or as explicit as nested parentheses. `sroa,instcombine` and `function(sroa,instcombine)` parse to the same pipeline: consecutive names that all belong to one level are wrapped in a pass manager for that level automatically, without writing the wrapper.[^npm] `loop-mssa(licm)` and `cgscc(inline)` write the level by hand, which is required only when the pipeline needs to step down a level, from function to loop or from module to CGSCC, in the middle of a flat list. Mixing levels without that nesting is a syntax error the parser catches immediately: a pipeline that has already committed to a run of function-level names, such as `sccp,simplifycfg`, cannot be handed a CGSCC-level name like `inline` next without first closing that run, because the parser is still inside the function pass manager it opened for `sccp`.

`--print-passes` lists every pass `opt` knows, tagged with the unit it runs on, which settles the question of how to spell a given pipeline without guessing.[^npm] A frontend author writing Vortex's own default pipeline as a string, the way this chapter's exercise asks for, would reach for the same tool: print what is registered, pick the level each pass belongs to, and nest only where the string needs to step down.

## Default pipelines, and when to leave them

Most of a compiler's users never write `-passes=` at all. `clang -O2` selects one of `PassBuilder`'s built-in pipelines, `default<O0>` through `default<O3>`, plus `Os` and `Oz` for size, each a fixed, tuned sequence assembled from the same passes this book has covered chapter by chapter: mem2reg and SROA from [O3](o3-ssa.md) and [O7](o7-inlining-and-sroa.md), the redundancy passes from [O6](o6-redundancy.md), inlining in its bottom-up CGSCC walk from [O7](o7-inlining-and-sroa.md), the loop passes from [O8](o8-loops.md), and the vectorizer whose legality checks lean on everything [O9](o9-alias-analysis.md) built. `PassBuilder` runs several of its scalar-cleanup passes more than once, at different points, because a later pass creates work an earlier one already finished elsewhere in the function; that repetition is itself a phase-ordering decision, made once by LLVM's own maintainers and shipped as the default.

Those defaults are tuned for one pair of source languages. LLVM's own guidance says it plainly: the standard pipelines are "carefully tuned for C and C++," not for whatever a frontend author's own language needs, and a frontend "will almost certainly need to use a custom pass order to achieve optimal performance."[^llvm-fe] Two of its suggestions matter directly to a language with Vortex's shape. A language with implicit guard conditions, such as a bounds check compiled into every array access, may be worth running extra rounds of loop-unswitch and LICM for, since a guard the front end wrote is exactly the kind of loop-invariant branch those passes exist to hoist or remove.[^llvm-fe] And a range-checked language should consider running IRCE, the pass [O8](o8-loops.md#removing-a-check-with-a-proof) used to remove the `clear_first` example's check: IRCE sits outside the default `-O2` and `-O3` pipelines, so a language whose checks look like Vortex's has to ask for it by name.[^llvm-fe]

This is the sense in which pass order is a language design decision, not only a compiler-engineering one. The matmul kernel from [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for) reaches the vectorizer only after several earlier passes have already done their work: [O8](o8-loops.md#removing-a-check-with-a-proof) proves the loop's index checks always hold and marks the induction variable's step `nsw`; [O9](o9-alias-analysis.md#for-vortex) attaches the `noalias` fact that `c`'s storage is reachable through no other parameter of the call, which is what lets the vectorizer skip the runtime overlap check a C compiler must emit for three unrelated `float *` parameters. Neither fact is optional, and neither is free: proving it costs an earlier pass's pass over the function, on every function, whether or not a later pass ends up using the answer. A pipeline that ran the vectorizer first, before those facts existed, would see the same IR a C compiler sees for the same loop, and would either add the runtime check anyway or refuse to vectorize at all. The order is not a matter of taste; it is which facts exist yet.

The same bookkeeping this chapter has been describing decides what those earlier passes cost the ones after them. [O9](o9-alias-analysis.md#the-walker) built MemorySSA once, by hand, to answer a clobber query. In a real pipeline, MemorySSA is an analysis like any other: LICM, EarlyCSE and dead store elimination can all be asked to use it, and a pass that only rewrites values without changing which store a load's clobber query would return can preserve it, saving every pass after it from rebuilding the same walk of the function.[^llvm-mssa] A pass that inserts or removes a store cannot make that promise, and must say so; MemorySSA is rebuilt the next time something needs it. Whether that rebuild happens once or many times over one run of the pipeline is exactly the question `PreservedAnalyses` answers.

??? check "A frontend author reads that IRCE is not part of `-O2`. What has to be true of the pipeline string for IRCE to run at all?"

    The pipeline has to name it explicitly, nested at the loop level, for example inside `-passes='default<O2>,loop(irce)'` or a hand-built pipeline that includes `irce` where the language's own passes belong. Nothing about compiling with `-O2` adds a pass the default pipeline does not list.

## For Vortex

!!! vortex "Exercise"

    **Decide first.** Write your middle-end pipeline as data: an ordered list of pass names, not a sequence of function calls hardcoded into your driver. For each pass, note the IR unit it runs over (your compiler need not have four; even two, function and loop, is enough to make ordering visible) and which of the other passes' facts it depends on, such as the bounds-check pass from [O8](o8-loops.md#for-vortex) needing to run before whatever consumes its `nsw` flags. Decide, and write down why, where in the list [O9](o9-alias-analysis.md#for-vortex)'s `noalias` attributes are attached relative to anything that would use them.

    **Then build:**

    1. A pipeline driver that reads the list and runs each pass in order over the right unit. It need not have adaptors as general as LLVM's; a driver that knows about exactly the units your compiler has is enough.
    2. A `PreservedAnalyses`-shaped answer from each pass: at minimum, a boolean each pass returns for each analysis your compiler caches (a dominator tree, a set of alias facts, anything from [O2](o2-cfg-and-dominance.md) through [O9](o9-alias-analysis.md) that a pass computes once and a later pass might reuse), saying whether that pass left it valid.
    3. A way to print the pipeline as it ran: which passes ran, in what order, and for each analysis, whether it was computed, reused or invalidated, so the log this chapter's second example produced by hand is something your own compiler produces on request.

    **Not yet:** a general nesting mechanism for arbitrary new IR units; parallel or incremental pass scheduling; a pass that only runs when an earlier one reports it found something (that is a fixpoint loop, and a fixed, ordered list is enough for v0.1); anything from [O11](o11-undefined-behavior.md) about which orderings are unsound, only which are unproven.

    **Proof that it works:**

    - A snapshot test of the remark stream ([O1](o1-optimizer-contract.md#remarks-the-optimizers-report)) for the stage 10 kernel, run with your pipeline's default order, checked into the test suite the way a golden output is.
    - The same kernel with two passes in the list swapped, where the swap should change nothing observable (say, two independent cleanup passes) and a golden diff of zero remarks changed confirms it.
    - The same kernel with two passes in the list swapped where the swap should change something (the bounds-check pass moved after whatever consumes its facts), and a remark that was a passed remark before the swap now reads missed, with a reason naming the missing fact.
    - A count, from your pipeline's own log, of how many times each analysis was recomputed over one run of the kernel through your default pipeline, dated, with your compiler's version, next to the same count for a pipeline that runs the same passes in reverse order.

    | Pipeline | Passes | Analyses recomputed | Remarks changed vs. default |
    | --- | --- | --- | --- |
    | Default order | | | N/A |
    | Two independent passes swapped | | | |
    | Bounds-check pass moved late | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **Why can two passes together prove something neither proves alone?** A later pass sees the program the way the earlier pass left it: a fact one pass exposes, such as a parameter becoming a constant after inlining, is a fact the next pass can use, even though it could not have found it by itself.
    - **What does `PreservedAnalyses` let a pass manager avoid?** Rebuilding an analysis a pass could not have invalidated; a pass that changed nothing, or changed only what it explicitly names, tells the manager exactly what is still trustworthy.
    - **What are LLVM's four IR units, and which is optional?** Module, CGSCC, function and loop, nested in that order; CGSCC is optional, since a pipeline can go straight from module to function.
    - **What does an adaptor do that a pass author never has to?** It finds every instance of the smaller unit inside the larger one the pipeline is currently at, and puts each instance into the canonical shape the wrapped pass expects, such as a loop's preheader, before running it.
    - **Why is `sroa,instcombine` a valid pipeline string with no explicit nesting, but `sccp,simplifycfg,inline` is not?** The first two are both function passes, auto-promoted into one implicit function pass manager; `inline` is a CGSCC pass, and the string has already committed to a function-level run by the time it appears, with no nesting syntax to step back up.
    - **Why does LLVM's documentation say a frontend "will almost certainly need" its own pass order?** The default pipelines are tuned for C and C++; a language with its own guard conditions, range checks or other shape that C does not have exposes passes, such as IRCE, that the default order never runs.

## Where this comes back

!!! next "You will use this again in"

    - [O11. Undefined behavior, poison and correct optimization](o11-undefined-behavior.md): *what a pipeline may assume between passes*, *the cost of getting an order wrong*
    - [O12. Testing an optimizer](o12-testing-optimizers.md): *snapshot-testing a remark stream*, *golden output across pipeline changes*
    - [P7. Loop transformations](p7-loop-transformations.md): *where a new loop pass fits in the pipeline*
    - [P10. Vectorization](p10-vectorization.md): *the facts the vectorizer needs already proven*
    - [P16. Capstone: the ladder, measured](p16-capstone.md): *a pipeline built up rung by rung*
    - [M3. Passes and pattern rewriting](../mlir/m3-passes-and-rewriting.md): *MLIR's own pass manager, at a different granularity*
    - [M9. Schedules as IR: the transform dialect](../mlir/m9-transform-dialect.md): *a pipeline written as data, inside the IR itself*

## Sources and further reading

Read the New Pass Manager guide first: the IR unit hierarchy, the adaptor functions and the pipeline text grammar are all there, and this chapter's examples check against LLVM 18.1.8's actual behavior for each claim. Writing an LLVM Pass explains `PreservedAnalyses` from a pass author's side, in the smallest example LLVM ships. The New Pass Manager blog post is worth reading once for why the legacy design could not give an inliner what it needed, which is the reason the chapters after this one, and the one before it, all assume the new design. Performance Tips for Frontend Authors' Pass Ordering section is short and says, better than this chapter can, why a language's own pipeline is not the same question as which passes exist.

[^npm]: LLVM Project, "LLVM's New Pass Manager", sections describing the module/CGSCC/function/loop IR unit hierarchy, the adaptor functions (`createModuleToFunctionPassAdaptor`, `createModuleToPostOrderCGSCCPassAdaptor`, `createFunctionToLoopPassAdaptor`), the `-passes=` pipeline text grammar and its auto-promotion of bare pass names, and `--print-passes`, read on 2026-09-24. <https://llvm.org/docs/NewPassManager.html>
[^writing-pass]: LLVM Project, "Writing an LLVM Pass (New PM Version)", the `run` method signature for a function pass, the `HelloWorldPass` example and its `PreservedAnalyses::all()` return, read on 2026-09-24. <https://llvm.org/docs/WritingAnLLVMNewPMPass.html>
[^npm-blog]: LLVM Blog, "The New Pass Manager" (2021-03-26): why the legacy pass manager could not support retrieving fresh analyses for arbitrary functions from inside a CGSCC pass, and how the new design makes nesting between IR units explicit through adaptors. <https://blog.llvm.org/posts/2021-03-26-the-new-pass-manager/>
[^llvm-fe]: LLVM Project, "Performance Tips for Frontend Authors", section "Pass Ordering", read on 2026-09-24. <https://llvm.org/docs/Frontend/PerformanceTips.html>
[^llvm-pipelines]: LLVM Project, `PassBuilderPipelines.cpp`, release/18.x branch: `buildInlinerPipeline` and the function-simplification pipeline it runs over every function an inlined call site changed. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Passes/PassBuilderPipelines.cpp>
[^llvm-mssa]: LLVM Project, "MemorySSA", sections "Invalidation and updating" and "Use and Def optimization", read on 2026-09-24. <https://llvm.org/docs/MemorySSA.html>
