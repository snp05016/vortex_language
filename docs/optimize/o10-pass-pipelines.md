# O10. Pass managers and pipelines

<p class="page-intro">A compiler never runs one optimization. It runs dozens, in an order someone chose, over pieces of the program that nest inside one another: the module, its call graph, each function and each loop. This chapter is about the machinery that runs them: how a pass manager decides where each pass runs, what it remembers between passes, and why the same passes in another order produce another program. For Vortex, the order decides whether the facts the vectorizer needs exist by the time it looks.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 50 minutes · Builds on: [O6. Redundancy: CSE, GVN, PRE and LICM](o6-redundancy.md), [O7. Calls and aggregates: inlining and SROA](o7-inlining-and-sroa.md), [O8. Loops: structure, induction variables and bounds checks](o8-loops.md), [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md)</p>

???+ remember "Before you start, remember"

    ??? question "When is a computation fully redundant, and where does LICM move a computation that gives the same value on every trip?"

        It is fully redundant when every path from the function's entry to it has already computed the same value, and GVN then reuses the earlier result. LICM moves a loop-invariant computation into the loop's preheader, the block that runs once before the loop starts.

        Introduced in [O6. Redundancy: CSE, GVN, PRE and LICM](o6-redundancy.md#work-done-twice).

    ??? question "Why does LLVM's inliner visit the call graph bottom-up?"

        So that when it measures a call site, the callee's own calls are already inlined and its body already simplified. A callee visited bottom-up is final.

        Introduced in [O7. Calls and aggregates: inlining and SROA](o7-inlining-and-sroa.md#deciding-in-order-bottom-up-over-the-call-graph).

    ??? question "What does loop simplify form guarantee, and what does loop-closed SSA form add?"

        A preheader, a single back edge and dedicated exits. Loop-closed SSA form (LCSSA) sends every value used after the loop through a one-input phi in an exit block.

        Introduced in [O8. Loops: structure, induction variables and bounds checks](o8-loops.md#the-shape-of-a-loop).

    ??? question "What does MemorySSA's walker return for a load, and why is that answer worth keeping?"

        The nearest access above the load that may write the location it reads. Building MemorySSA walks the whole function, so a pass that keeps it valid saves every later pass from building it again.

        Introduced in [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md#the-walker).

    ??? question "What do passed, missed and analysis remarks report?"

        A passed remark reports a transformation that was made, a missed remark one that was attempted and not made, and an analysis remark a fact a pass worked out, often the reason behind a missed remark.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#remarks-the-optimizers-report).

!!! goals "In this chapter"

    - Trace by hand how the same two passes produce two different programs in two orders, and name the problem this shows.
    - Recognize LLVM's four IR units and the adaptors between them, and predict the order in which a nested pipeline visits functions and loops.
    - Read and write `-passes=` pipeline text, and print what a string builds and how it runs.
    - Explain how `PreservedAnalyses` lets a pass manager keep analyses between passes, and what a pass promises with `all()`, `none()` or a named set.
    - Use a remark stream to show that a pipeline's order decides what the vectorizer can prove.

## One loop, two orders

[O6](o6-redundancy.md#your-turn-the-kernels-inner-loop) ended its kernel exercise with an ordering claim: once LICM has moved `row * 64` out of the `k` loop, GVN can reuse it after the loop, so LICM must run first. Here is the smallest function with that shape. `row_total` adds up one row of a table 64 columns wide and writes the total into the row's first cell, so `%row * 64` appears twice, in the loop body and after the loop. The first example runs GVN, then LICM:

--8<-- "includes/examples/optimize/o10-pass-pipelines/gvn_then_licm.ll.md"

Follow it by hand. GVN runs first and asks of `%start.again`, in the exit block, whether every path from the entry has already computed `%row * 64`. One path has not: entry, header, exit, the path taken when `%n` is 0 and the loop runs zero times. The body does not dominate the exit, so GVN changes nothing. LICM runs second, sees that `%start` has the same value on every trip, and hoists it into `entry`. Nothing runs after LICM to notice that `entry` now computes what the exit computes again. (The phi `%total.lcssa` is loop-closed SSA form; the section on adaptors says what adds it.)

The second example runs the same two passes the other way round:

--8<-- "includes/examples/optimize/o10-pass-pipelines/licm_then_gvn.ll.md"

LICM hoists `%start` into `entry` first. `entry` dominates every block of the function, so when GVN asks its question, every path to the exit has computed `%row * 64` already. `%start.again` is fully redundant, and GVN replaces it with `%start`; it also folds the one-input phi. The same passes on the same input leave one multiplication instead of two.

This is **phase ordering**: what a pass can do depends on what the passes before it have done, so the order of the passes is part of the optimizer's design. Click and Cooper open their 1995 paper on combining optimizations with the same observation: many optimizations "exhibit a phase ordering problem", and the best code may require iterating them until nothing changes.[^cc95] No single order settles it: Kulkarni and his coauthors point out that the best order depends on the function, the compiler and the target. For a research compiler with 15 phases, they enumerated every distinct version of a function that some ordering could produce, for more than 98% of the functions in their benchmark suite.[^kul06]

??? check "In `gvn_then_licm.ll`, would a second GVN at the end remove `%start.again`? In `licm_then_gvn.ll`, what would a second LICM at the end change?"

    A second GVN removes it: after LICM, `entry` computes `%row * 64` and dominates the exit, as in the second example. On LLVM 18.1.8, `gvn,loop-mssa(licm),gvn` leaves one multiplication. A second LICM finds nothing left to hoist, but the one-input phi `%total.lcssa` comes back, because the machinery that runs loop passes converts the loop to loop-closed SSA form before every loop pipeline. Running a pass again is one answer to phase ordering, and LLVM's default pipelines use it often.

## What a pass manager does

A compiler that runs many passes needs a component that answers questions no single pass can:

- **In what order** the passes run, which the two examples show matters.
- **Over which piece of the program** each pass runs: a module, one call-graph component, one function or one loop.
- **Which analyses are still valid**, so that a later pass gets the existing dominator tree or MemorySSA instead of building it again.
- **When to stop**, since passes create work for one another.

That component is the **pass manager**. LLVM has had two. The older one, now called the **legacy pass manager**, scheduled analyses as if they were passes: each pass declared ahead of time which analyses it required and which it preserved, and the manager ran the required ones in between. It could not give a pass that walks the call graph the analyses of arbitrary functions, which the inliner needed in order to look at its callees' profile data, including through small wrapper functions.[^npm-blog] The **new pass manager** separates the two jobs. Passes transform, and an **analysis manager** computes an analysis when a pass asks for it, caches the result, and discards it when a transformation may have made it wrong.[^npm-blog] LLVM's optimizer uses the new pass manager; its target code generator still uses the legacy one.[^npm]

The loop at the center of the new pass manager is short. For each pass in its list, `PassManager::run` in LLVM 18 asks its **pass instrumentation**, callbacks that run before and after every pass, whether to run the pass at all; runs it; tells the analysis manager which analyses the pass preserved, so that it can drop the others; and lets the instrumentation look at the result.[^llvm-pm] The instrumentation is also how `optnone` functions and LLVM's bisection option skip passes.[^npm-blog]

A pass reports what it preserved through its return value. A function pass has a `run` method of the form `PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM)`, and LLVM's tutorial pass, which only prints each function's name, returns `PreservedAnalyses::all()`: it changed nothing, so every analysis is still valid.[^writing-pass] A pass that changed the program and kept nothing up to date returns `PreservedAnalyses::none()`. Between the two, a pass can name what it kept: one analysis it updated as it went, such as the dominator tree, or a whole set, such as `preserveSet<CFGAnalyses>()` for a pass that changed no branch, which keeps every analysis that depends only on the control-flow graph.[^npm]

The first C++ example builds this machinery in miniature. The program is a list of numbers, its two analyses answer "is it sorted?" and "what is its sum?", and its log uses the words of LLVM's own log:

--8<-- "includes/examples/optimize/o10-pass-pipelines/preserved.cpp.md"

Read the log from the top. The first `check` asks for both analyses, and both are computed. `add_one` changes every value but not their order, so it returns `sorted` preserved and `sum` not, and the manager drops `sum`. The second `check` gets `sorted` from the cache, silently, and pays for `sum` again. `reverse` is the mirror image. Four computations answer six queries. A manager that trusted no pass would compute six times; one that never dropped anything would have printed `sum=10` after `add_one`, a wrong answer. Preserving too little costs compile time, and preserving too much costs correctness. Figure 1 draws the same run.

<figure class="vx-figure">
<svg viewBox="0 0 760 262" role="img" aria-label="The first example's analysis cache across five passes: four computations answer six queries" aria-describedby="o10-f1-desc">
<title id="o10-f1-title">An analysis cache, pass by pass</title>
<desc id="o10-f1-desc">Five pass boxes run left to right along the top, joined by arrows: check, add_one, check, reverse and check. Under each box is what the pass keeps: check keeps all, add_one keeps sorted, reverse keeps sum. Below are two rows of cells, one for the sorted analysis and one for sum, showing each cached result after each pass. Sorted: computed, kept, cached, dropped, computed. Sum: computed, dropped, computed, kept, cached. Computed cells have an accent outline, kept and cached cells a plain outline, and dropped cells a dashed outline. A legend explains the outlines, and a note gives the total: six queries, four computations.</desc>
<defs><marker id="o10-f1-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box-strong" x="108" y="14" width="112" height="34" rx="4"/>
<text class="vx-mono" x="164" y="36" text-anchor="middle">check</text>
<rect class="vx-box-strong" x="236" y="14" width="112" height="34" rx="4"/>
<text class="vx-mono" x="292" y="36" text-anchor="middle">add_one</text>
<rect class="vx-box-strong" x="364" y="14" width="112" height="34" rx="4"/>
<text class="vx-mono" x="420" y="36" text-anchor="middle">check</text>
<rect class="vx-box-strong" x="492" y="14" width="112" height="34" rx="4"/>
<text class="vx-mono" x="548" y="36" text-anchor="middle">reverse</text>
<rect class="vx-box-strong" x="620" y="14" width="112" height="34" rx="4"/>
<text class="vx-mono" x="676" y="36" text-anchor="middle">check</text>
<path class="vx-line" d="M220 31 L235 31" marker-end="url(#o10-f1-head)"/>
<path class="vx-line" d="M348 31 L363 31" marker-end="url(#o10-f1-head)"/>
<path class="vx-line" d="M476 31 L491 31" marker-end="url(#o10-f1-head)"/>
<path class="vx-line" d="M604 31 L619 31" marker-end="url(#o10-f1-head)"/>
<text class="vx-text-muted" x="164" y="66" text-anchor="middle">keeps all</text>
<text class="vx-text-muted" x="292" y="66" text-anchor="middle">keeps sorted</text>
<text class="vx-text-muted" x="420" y="66" text-anchor="middle">keeps all</text>
<text class="vx-text-muted" x="548" y="66" text-anchor="middle">keeps sum</text>
<text class="vx-text-muted" x="676" y="66" text-anchor="middle">keeps all</text>
<text class="vx-text" x="16" y="109">sorted</text>
<text class="vx-text" x="16" y="173">sum</text>
<rect class="vx-box-accent" x="108" y="84" width="112" height="40" rx="4"/>
<text class="vx-mono" x="164" y="109" text-anchor="middle">computed</text>
<rect class="vx-box" x="236" y="84" width="112" height="40" rx="4"/>
<text class="vx-mono" x="292" y="109" text-anchor="middle">kept</text>
<rect class="vx-box" x="364" y="84" width="112" height="40" rx="4"/>
<text class="vx-mono" x="420" y="109" text-anchor="middle">cached</text>
<rect class="vx-box-bad" x="492" y="84" width="112" height="40" rx="4"/>
<text class="vx-mono" x="548" y="109" text-anchor="middle">dropped</text>
<rect class="vx-box-accent" x="620" y="84" width="112" height="40" rx="4"/>
<text class="vx-mono" x="676" y="109" text-anchor="middle">computed</text>
<rect class="vx-box-accent" x="108" y="148" width="112" height="40" rx="4"/>
<text class="vx-mono" x="164" y="173" text-anchor="middle">computed</text>
<rect class="vx-box-bad" x="236" y="148" width="112" height="40" rx="4"/>
<text class="vx-mono" x="292" y="173" text-anchor="middle">dropped</text>
<rect class="vx-box-accent" x="364" y="148" width="112" height="40" rx="4"/>
<text class="vx-mono" x="420" y="173" text-anchor="middle">computed</text>
<rect class="vx-box" x="492" y="148" width="112" height="40" rx="4"/>
<text class="vx-mono" x="548" y="173" text-anchor="middle">kept</text>
<rect class="vx-box" x="620" y="148" width="112" height="40" rx="4"/>
<text class="vx-mono" x="676" y="173" text-anchor="middle">cached</text>
<rect class="vx-box-accent" x="108" y="210" width="22" height="14" rx="3"/>
<text class="vx-text-muted" x="138" y="221">computed: this query paid</text>
<rect class="vx-box" x="330" y="210" width="22" height="14" rx="3"/>
<text class="vx-text-muted" x="360" y="221">kept or cached: free</text>
<rect class="vx-box-bad" x="520" y="210" width="22" height="14" rx="3"/>
<text class="vx-text-muted" x="550" y="221">dropped: the next query pays</text>
<text class="vx-text-muted" x="108" y="250">6 queries, 4 computations</text>
</svg>
<figcaption>Figure 1. The first C++ example's cache, pass by pass. Each column is one pass and each row one analysis. A computed result was paid for by a query; a kept result survived the pass above it, and a cached one answered a query for free; a dropped one was discarded because the pass above it did not promise to keep it, and the next query pays again.</figcaption>
</figure>

LLVM prints the same kind of log with `-debug-pass-manager`.[^npm] This is what LLVM 18.1.8 printed, on standard error, for the second example's pipeline, `loop-mssa(licm),gvn` (on the owner's M4 Pro, 2026-09-24):

```text
Running analysis: InnerAnalysisManagerProxy<FunctionAnalysisManager, Module> on [module]
Running pass: LoopSimplifyPass on row_total (16 instructions)
Running analysis: LoopAnalysis on row_total
Running analysis: DominatorTreeAnalysis on row_total
Running analysis: AssumptionAnalysis on row_total
Running analysis: TargetIRAnalysis on row_total
Running pass: LCSSAPass on row_total (16 instructions)
Running analysis: MemorySSAAnalysis on row_total
Running analysis: AAManager on row_total
Running analysis: TargetLibraryAnalysis on row_total
Running analysis: BasicAA on row_total
Running analysis: ScopedNoAliasAA on row_total
Running analysis: TypeBasedAA on row_total
Running analysis: OuterAnalysisManagerProxy<ModuleAnalysisManager, Function> on row_total
Running analysis: ScalarEvolutionAnalysis on row_total
Running analysis: InnerAnalysisManagerProxy<LoopAnalysisManager, Function> on row_total
Running pass: LICMPass on header
Running pass: GVNPass on row_total (17 instructions)
Running analysis: MemoryDependenceAnalysis on row_total
Running analysis: OptimizationRemarkEmitterAnalysis on row_total
Clearing all analysis results for: <possibly invalidated loop>
Invalidating analysis: ScalarEvolutionAnalysis on row_total
Invalidating analysis: InnerAnalysisManagerProxy<LoopAnalysisManager, Function> on row_total
Invalidating analysis: MemoryDependenceAnalysis on row_total
Running pass: VerifierPass on [module]
Running analysis: VerifierAnalysis on [module]
```

Read it like the toy's log. The dominator tree is computed once, for LoopSimplify, and every later user (LCSSA, MemorySSA, LICM and GVN) gets the cached copy: there is no second `DominatorTreeAnalysis` line. MemorySSA is built because `loop-mssa` asked for it, and LICM runs on `header`, the loop's header block. GVN then changes the function, and the last `Clearing` and `Invalidating` lines are the analysis manager acting on GVN's answer: scalar evolution, the loop analyses and the memory-dependence results GVN built for itself are dropped. The dominator tree is not, because GVN said it was still valid. `opt` adds the final `VerifierPass` itself.

??? check "What should each of these passes return? (a) One that prints a report about every function. (b) One that rewrites `x * 2` as `x << 1` and never touches a branch. (c) One that splits a critical edge, updates the dominator tree as it does so, and keeps nothing else up to date."

    (a) `PreservedAnalyses::all()`, like LLVM's tutorial pass. (b) `preserveSet<CFGAnalyses>()`: the instructions changed, so analyses of values may be stale, but no block or edge changed, so the dominator tree and the loop information still hold. (c) A `PreservedAnalyses` that names the dominator tree and nothing else: the control-flow graph changed, so the CFG set as a whole is not preserved, but that one analysis was kept up to date. Returning `all()` from (b) would leave a stale result in the cache for the next pass to trust, a wrong answer waiting for an input that exposes it.

## Four places a pass can run

An LLVM pass does not run over "the program". It runs over one **IR unit**, and there are four, nested: the **module**, which is the whole file; a **CGSCC**, one strongly connected component of the call graph ([O7](o7-inlining-and-sroa.md#deciding-in-order-bottom-up-over-the-call-graph)); a **function**; and a **loop**, nested in that order, with the call-graph level optional.[^npm] LLVM's developers considered making the call-graph level required, but the cost of building the call graph, and the extra code nesting needs, kept it optional.[^npm-blog] A pass states its unit through the first parameter of its `run` method.[^writing-pass]

A pass manager holds passes of one unit only. To run a smaller unit's pass inside a larger unit's pipeline, the pass is wrapped in an **adaptor**: a pass of the larger unit that runs the wrapped pass on every smaller unit inside it. LLVM has four: module to CGSCC, CGSCC to function, module to function, and function to loop.[^npm] A pass manager is itself a pass of its unit, so one adaptor can wrap a whole list of passes.[^npm] Figure 2 shows the levels and the adaptors between them.

<figure class="vx-figure">
<svg viewBox="0 0 760 372" role="img" aria-label="LLVM's four IR units, module, CGSCC, function and loop, with the adaptor that connects each level to the next" aria-describedby="o10-f2-desc">
<title id="o10-f2-title">IR units and the adaptors between them</title>
<desc id="o10-f2-desc">Four boxes stacked from top to bottom. Module: the whole file, with globalopt and ipsccp as example passes. CGSCC, marked optional: one component of the call graph, with inline. Function: one function, with sroa, gvn and irce. Loop: one loop, with licm and loop-rotate. Arrows lead down from each box to the next. On the right, level with each arrow, is the adaptor that makes the step and the pipeline text that creates it: createModuleToPostOrderCGSCCPassAdaptor, written cgscc, which visits components callees first; createCGSCCToFunctionPassAdaptor, written function inside cgscc; and createFunctionToLoopPassAdaptor, written loop or loop-mssa, which runs LoopSimplify and LCSSA first and then each loop, innermost first. A dashed arrow to the right of the boxes goes from module straight to function, skipping the call graph; it is labelled createModuleToFunctionPassAdaptor, written function at the top level.</desc>
<defs><marker id="o10-f2-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box-strong" x="40" y="16" width="260" height="52" rx="4"/>
<text class="vx-text" x="56" y="38">module</text>
<text class="vx-text-muted" x="56" y="57">the whole file: globalopt, ipsccp</text>
<rect class="vx-box" x="40" y="112" width="260" height="52" rx="4"/>
<text class="vx-text" x="56" y="134">CGSCC (optional)</text>
<text class="vx-text-muted" x="56" y="153">one call-graph component: inline</text>
<rect class="vx-box" x="40" y="208" width="260" height="52" rx="4"/>
<text class="vx-text" x="56" y="230">function</text>
<text class="vx-text-muted" x="56" y="249">one function: sroa, gvn, irce</text>
<rect class="vx-box" x="40" y="304" width="260" height="52" rx="4"/>
<text class="vx-text" x="56" y="326">loop</text>
<text class="vx-text-muted" x="56" y="345">one loop: licm, loop-rotate</text>
<path class="vx-line" d="M170 68 L170 111" marker-end="url(#o10-f2-head)"/>
<path class="vx-line" d="M170 164 L170 207" marker-end="url(#o10-f2-head)"/>
<path class="vx-line" d="M170 260 L170 303" marker-end="url(#o10-f2-head)"/>
<path class="vx-line" d="M300 42 L318 42 L318 234 L301 234" stroke-dasharray="5 4" marker-end="url(#o10-f2-head)"/>
<text class="vx-mono" x="334" y="86">createModuleToPostOrderCGSCCPassAdaptor</text>
<text class="vx-text-muted" x="334" y="103">written cgscc(…); visits components callees first</text>
<text class="vx-mono" x="334" y="134">createModuleToFunctionPassAdaptor</text>
<text class="vx-text-muted" x="334" y="151">dashed: function(…) at the top level, no call graph</text>
<text class="vx-mono" x="334" y="182">createCGSCCToFunctionPassAdaptor</text>
<text class="vx-text-muted" x="334" y="199">written function(…) inside cgscc(…)</text>
<text class="vx-mono" x="334" y="278">createFunctionToLoopPassAdaptor</text>
<text class="vx-text-muted" x="334" y="295">written loop(…) or loop-mssa(…); runs LoopSimplify</text>
<text class="vx-text-muted" x="334" y="311">and LCSSA first, then each loop, innermost first</text>
</svg>
<figcaption>Figure 2. LLVM's four IR units and the adaptors between them. Each box is a unit a pass can run on, with passes that run there according to <code>opt --print-passes</code> (LLVM 18.1.8). Each arrow is an adaptor, named on the right with the pipeline text that creates it. The dashed arrow skips the call graph, which is optional.</figcaption>
</figure>

The loop adaptor does more than find the loops. Before it runs any loop pass, it puts each loop into loop simplify form where it can and into loop-closed SSA form always; it keeps the loop information, the dominator tree, scalar evolution and alias analysis available to every loop pass; and it runs the loop passes over each loop nest from the innermost loop outward.[^llvm-lpm] The first two jobs are why [O8](o8-loops.md#the-shape-of-a-loop)'s first example gained a preheader when its pipeline named only `loop-rotate`, and why `%total.lcssa` appeared above. The third needs a loop nest to show:

--8<-- "includes/examples/optimize/o10-pass-pipelines/loop_adaptor.ll.md"

The pipeline names one loop pass. Before it runs, LoopSimplify gives the outer loop the preheader it lacked, `outer.preheader`, with a phi for the two starting values of `%r`. Then `licm` runs twice, once per loop, innermost first. On the inner loop it hoists `%ksq` into `row`, the block that runs once before each row. On the outer loop, `row` is an ordinary block, so `licm` hoists `%ksq` again, into `outer.preheader`. The order is what makes one pass enough: LICM skips blocks that belong to inner loops, which it expects to be done already.[^llvm-licm] With `-debug-pass-manager`, the log shows `LICMPass on inner` before `LICMPass on outer`.

Visiting order also decides how passes should be grouped. Two separate adaptors run the first pass on every function, then the second on every function; one adaptor around a function pass manager that holds both runs both passes on the first function, then both on the second. LLVM's guide prefers the grouped form for cache locality, and notes that for loop passes it can even improve the result, since running all loop passes on one loop may let a later loop be optimized more.[^npm]

The call graph adds a second question: whether a function pass runs inside the call-graph walk or after it. Figure 3 shows the orders that `-debug-pass-manager` printed for a module in which `main` calls `f` and `f` calls `g`.

<figure class="vx-figure">
<svg viewBox="0 0 760 360" role="img" aria-label="The order in which four pipelines visit the functions main, f and g" aria-describedby="o10-f3-desc">
<title id="o10-f3-title">Visit orders for grouped, separate and nested pipelines</title>
<desc id="o10-f3-desc">Four rows of six boxes each, time running left to right. Row one, function(instcombine,gvn): instcombine on main, gvn on main, instcombine on f, gvn on f, instcombine on g, gvn on g. Row two, function(instcombine),function(gvn): instcombine on main, f and g, then gvn on main, f and g. A note between the pairs of rows says that main calls f, f calls g, and the file lists main, f, g. Row three, cgscc(inline,function(instcombine)): inline on g, instcombine on g, inline on f, instcombine on f, inline on main, instcombine on main. Row four, cgscc(inline),function(instcombine): inline on g, f and main, then instcombine on main, f and g. Boxes for gvn and inline have an accent outline.</desc>
<defs><marker id="o10-f3-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text-muted" x="626" y="22">time</text>
<path class="vx-line" d="M660 18 L736 18" marker-end="url(#o10-f3-head)"/>
<text class="vx-mono" x="16" y="24">function(instcombine,gvn)</text>
<rect class="vx-box" x="16" y="32" width="112" height="42" rx="4"/>
<text class="vx-mono" x="72" y="50" text-anchor="middle">instcombine</text>
<text class="vx-text-muted" x="72" y="66" text-anchor="middle">on main</text>
<rect class="vx-box-accent" x="138" y="32" width="112" height="42" rx="4"/>
<text class="vx-mono" x="194" y="50" text-anchor="middle">gvn</text>
<text class="vx-text-muted" x="194" y="66" text-anchor="middle">on main</text>
<rect class="vx-box" x="260" y="32" width="112" height="42" rx="4"/>
<text class="vx-mono" x="316" y="50" text-anchor="middle">instcombine</text>
<text class="vx-text-muted" x="316" y="66" text-anchor="middle">on f</text>
<rect class="vx-box-accent" x="382" y="32" width="112" height="42" rx="4"/>
<text class="vx-mono" x="438" y="50" text-anchor="middle">gvn</text>
<text class="vx-text-muted" x="438" y="66" text-anchor="middle">on f</text>
<rect class="vx-box" x="504" y="32" width="112" height="42" rx="4"/>
<text class="vx-mono" x="560" y="50" text-anchor="middle">instcombine</text>
<text class="vx-text-muted" x="560" y="66" text-anchor="middle">on g</text>
<rect class="vx-box-accent" x="626" y="32" width="112" height="42" rx="4"/>
<text class="vx-mono" x="682" y="50" text-anchor="middle">gvn</text>
<text class="vx-text-muted" x="682" y="66" text-anchor="middle">on g</text>
<text class="vx-mono" x="16" y="106">function(instcombine),function(gvn)</text>
<rect class="vx-box" x="16" y="114" width="112" height="42" rx="4"/>
<text class="vx-mono" x="72" y="132" text-anchor="middle">instcombine</text>
<text class="vx-text-muted" x="72" y="148" text-anchor="middle">on main</text>
<rect class="vx-box" x="138" y="114" width="112" height="42" rx="4"/>
<text class="vx-mono" x="194" y="132" text-anchor="middle">instcombine</text>
<text class="vx-text-muted" x="194" y="148" text-anchor="middle">on f</text>
<rect class="vx-box" x="260" y="114" width="112" height="42" rx="4"/>
<text class="vx-mono" x="316" y="132" text-anchor="middle">instcombine</text>
<text class="vx-text-muted" x="316" y="148" text-anchor="middle">on g</text>
<rect class="vx-box-accent" x="382" y="114" width="112" height="42" rx="4"/>
<text class="vx-mono" x="438" y="132" text-anchor="middle">gvn</text>
<text class="vx-text-muted" x="438" y="148" text-anchor="middle">on main</text>
<rect class="vx-box-accent" x="504" y="114" width="112" height="42" rx="4"/>
<text class="vx-mono" x="560" y="132" text-anchor="middle">gvn</text>
<text class="vx-text-muted" x="560" y="148" text-anchor="middle">on f</text>
<rect class="vx-box-accent" x="626" y="114" width="112" height="42" rx="4"/>
<text class="vx-mono" x="682" y="132" text-anchor="middle">gvn</text>
<text class="vx-text-muted" x="682" y="148" text-anchor="middle">on g</text>
<text class="vx-text-muted" x="16" y="188">main calls f, f calls g; the file lists main, f, g</text>
<text class="vx-mono" x="16" y="216">cgscc(inline,function(instcombine))</text>
<rect class="vx-box-accent" x="16" y="224" width="112" height="42" rx="4"/>
<text class="vx-mono" x="72" y="242" text-anchor="middle">inline</text>
<text class="vx-text-muted" x="72" y="258" text-anchor="middle">on (g)</text>
<rect class="vx-box" x="138" y="224" width="112" height="42" rx="4"/>
<text class="vx-mono" x="194" y="242" text-anchor="middle">instcombine</text>
<text class="vx-text-muted" x="194" y="258" text-anchor="middle">on g</text>
<rect class="vx-box-accent" x="260" y="224" width="112" height="42" rx="4"/>
<text class="vx-mono" x="316" y="242" text-anchor="middle">inline</text>
<text class="vx-text-muted" x="316" y="258" text-anchor="middle">on (f)</text>
<rect class="vx-box" x="382" y="224" width="112" height="42" rx="4"/>
<text class="vx-mono" x="438" y="242" text-anchor="middle">instcombine</text>
<text class="vx-text-muted" x="438" y="258" text-anchor="middle">on f</text>
<rect class="vx-box-accent" x="504" y="224" width="112" height="42" rx="4"/>
<text class="vx-mono" x="560" y="242" text-anchor="middle">inline</text>
<text class="vx-text-muted" x="560" y="258" text-anchor="middle">on (main)</text>
<rect class="vx-box" x="626" y="224" width="112" height="42" rx="4"/>
<text class="vx-mono" x="682" y="242" text-anchor="middle">instcombine</text>
<text class="vx-text-muted" x="682" y="258" text-anchor="middle">on main</text>
<text class="vx-mono" x="16" y="298">cgscc(inline),function(instcombine)</text>
<rect class="vx-box-accent" x="16" y="306" width="112" height="42" rx="4"/>
<text class="vx-mono" x="72" y="324" text-anchor="middle">inline</text>
<text class="vx-text-muted" x="72" y="340" text-anchor="middle">on (g)</text>
<rect class="vx-box-accent" x="138" y="306" width="112" height="42" rx="4"/>
<text class="vx-mono" x="194" y="324" text-anchor="middle">inline</text>
<text class="vx-text-muted" x="194" y="340" text-anchor="middle">on (f)</text>
<rect class="vx-box-accent" x="260" y="306" width="112" height="42" rx="4"/>
<text class="vx-mono" x="316" y="324" text-anchor="middle">inline</text>
<text class="vx-text-muted" x="316" y="340" text-anchor="middle">on (main)</text>
<rect class="vx-box" x="382" y="306" width="112" height="42" rx="4"/>
<text class="vx-mono" x="438" y="324" text-anchor="middle">instcombine</text>
<text class="vx-text-muted" x="438" y="340" text-anchor="middle">on main</text>
<rect class="vx-box" x="504" y="306" width="112" height="42" rx="4"/>
<text class="vx-mono" x="560" y="324" text-anchor="middle">instcombine</text>
<text class="vx-text-muted" x="560" y="340" text-anchor="middle">on f</text>
<rect class="vx-box" x="626" y="306" width="112" height="42" rx="4"/>
<text class="vx-mono" x="682" y="324" text-anchor="middle">instcombine</text>
<text class="vx-text-muted" x="682" y="340" text-anchor="middle">on g</text>
</svg>
<figcaption>Figure 3. The order in which four pipelines visit the functions of one module, as <code>-debug-pass-manager</code> printed it on LLVM 18.1.8. Top: a grouped function pipeline runs both passes on one function before moving on, and two adaptors run the first pass everywhere before the second starts. Bottom: nested inside the call-graph walk, <code>instcombine</code> runs on each function right after the inliner has finished with it, callees first; placed after the walk, it runs once over every function, in file order. The inliner's unit is a component, written in parentheses as the log writes it.</figcaption>
</figure>

Nested inside the walk, `instcombine` cleans up each function immediately after the inliner is done with it. That is the shape of LLVM's default pipelines: `buildInlinerPipeline` adds the whole function simplification pipeline inside the CGSCC walk,[^llvm-pipelines] which is how, as O7 said, a callee is already simplified when its callers measure it. Run after the walk instead, the inliner would measure every callee before any cleanup, and each caller would inherit copies of code that was about to be simplified. The walk may also go round again. When a pass in it turns an indirect call into a direct one, `DevirtSCCRepeatedPass` reruns the component's pipeline, up to a fixed number of times,[^llvm-cgscc] and LLVM 18.1.8 prints that bound in its -O2 pipeline as `devirt<4>`.

Nesting also limits what a pass may ask for. A pass can compute any analysis of its own unit, and of the units inside it through a **proxy**, an analysis whose result is the inner unit's analysis manager (the log above opens with one). It may read an analysis of an enclosing unit only if one is already cached, never compute one: a function pass that computed a module analysis could scan every function once per function, which is quadratic, and if passes over different functions ever ran in parallel, what happened to be cached would make results nondeterministic.[^npm] Loops are the exception: loop passes read function analyses such as the dominator tree and keep them up to date themselves.[^npm] A pipeline that needs an outer analysis cached writes `require<name>` before the inner pipeline, a pass whose only job is to request that analysis.[^npm] The -O2 pipeline does this for its global alias analysis before the call-graph walk.

## The pipeline as text

`opt -passes=` takes a string, and `-p` is an alias for it. Names are separated by commas, and a name followed by parentheses holds a pipeline one level down: `cgscc(…)`, `function(…)`, `loop(…)`, and `loop-mssa(…)` for a loop pipeline that keeps MemorySSA.[^npm] Some passes take parameters in angle brackets, such as `default<O2>` or `licm<allowspeculation>`. `opt --print-passes` lists every pass and analysis with the unit it runs on, and the parameters each accepts.[^npm]

Writing every level by hand is tedious, so the parser has two conveniences, and only two:[^npm]

1. If the first name is not a module pass, the whole pipeline goes into a pass manager for the first pass's unit.
2. Inside a pass manager, a pass of a smaller unit is wrapped in the adaptor that fits, when one exists.

`PassBuilder::parsePassPipeline` applies the first rule by wrapping the whole text in `cgscc(…)`, `function(…)` or `function(loop(…))`, and chooses `loop-mssa` when the first pass is `licm`, which needs MemorySSA. The adaptor that the second rule adds inside a function pipeline never uses MemorySSA.[^llvm-passbuilder] Everything else is an error. Here is what `opt` 18.1.8 builds from a few strings, as `-print-pipeline-passes` prints them, with parameters removed and without the final `verify` that `opt` adds:

| You write | `opt` builds | Why |
| --- | --- | --- |
| `sroa,instcombine` | `function(sroa,instcombine)` | Rule 1: one function pipeline for the whole list. |
| `inline,sccp` | `cgscc(inline,function(sccp))` | Rule 1 opens a CGSCC pipeline, and rule 2 puts `sccp` inside the walk. |
| `cgscc(inline),sccp` | `cgscc(inline),function(sccp)` | The walk is closed, so `sccp` runs over every function afterwards. |
| `sccp,inline` | error: unknown function pass 'inline' | A function pipeline cannot hold a CGSCC pass. |
| `licm` | `function(loop-mssa(licm))` | Rule 1, with MemorySSA for `licm`. |
| `gvn,licm` | `function(gvn,loop(licm))` | Rule 2, without MemorySSA. |
| `licm,gvn` | error: unknown loop pass 'gvn' | Rule 1 made a loop pipeline, and `gvn` is a function pass. |
| `inline,loop-mssa(licm)` | error: invalid use of 'loop-mssa' pass as cgscc pipeline | Rule 2 adds one adaptor, not two. `inline,function(loop-mssa(licm))` works. |

The `gvn,licm` row is the one that bites: it parses, and running it stops with "LICM requires MemorySSA (loop-mssa)".[^llvm-licm] The `inline,sccp` row is the one that surprises: its names are in the same order as in `cgscc(inline),sccp`, but the two pipelines visit functions in different orders, as the bottom rows of Figure 3 showed. Printing a pipeline before trusting it costs one command.

??? check "Write a pipeline string that runs IRCE after LLVM's default -O2 pipeline. Why is `default<O2>,loop(irce)` rejected?"

    `default<O2>,irce`, which `opt` builds as the -O2 pipeline followed by `function(irce)`. `opt --print-passes` on LLVM 18 lists `irce` among the function passes, so it cannot sit in a loop pipeline: `default<O2>,function(loop(irce))` fails with "unknown loop pass 'irce'". `default<O2>,loop(irce)` fails even earlier. After `default<O2>` the parser is at module level, and there is no adaptor from a module straight to a loop pipeline, so it reports "invalid use of 'loop' pass as module pipeline".

## Default pipelines

Most users never write `-passes=`. `PassBuilder` builds LLVM's standard pipelines, `default<O0>` to `default<O3>`, and `default<Os>` and `default<Oz>` for size, and a front end gets one with a single call.[^npm] `buildPerModuleDefaultPipeline` assembles each from two halves, a simplification pipeline and then an optimization pipeline.[^llvm-pipelines] Figure 4 outlines the result for -O2.

<figure class="vx-figure">
<svg viewBox="0 0 760 492" role="img" aria-label="An outline of LLVM 18.1.8's -O2 pipeline in three columns: simplification before the inliner, the inliner walk, and optimization" aria-describedby="o10-f4-desc">
<title id="o10-f4-title">The -O2 pipeline in outline</title>
<desc id="o10-f4-desc">Three columns joined by arrows. Column one, simplify before the inliner: function of simplifycfg, sroa and early-cse; ipsccp and globalopt; function of mem2reg, instcombine and simplifycfg; always-inline; require globals-aa. Column two, the inliner walk, written cgscc of devirt 4: inline and function-attrs, then a function pipeline of sroa, early-cse, simplifycfg, instcombine, a loop-mssa group with licm, loop-rotate, licm and simple-loop-unswitch, simplifycfg and instcombine, a loop group with loop-idiom, indvars, loop-deletion and loop-unroll-full, then sroa, gvn, sccp, bdce, instcombine, adce, memcpyopt, dse, loop-mssa of licm, simplifycfg and instcombine; after the walk, deadargelim, globalopt and globaldce. Column three, optimize after the call-graph walk: elim-avail-extern, recompute-globalsaa, then a function pipeline of loop-rotate and loop-deletion, loop-distribute, loop-vectorize, loop-load-elim, instcombine, simplifycfg, slp-vectorizer, instcombine, loop-unroll, sroa, instcombine, loop-mssa of licm and simplifycfg; then globaldce and constmerge. Five lines are outlined: the loop-mssa group with the first licm, the loop group with indvars, the sroa after it, the line with gvn, sccp and bdce, and loop-vectorize. Ellipses mark passes left out.</desc>
<defs><marker id="o10-f4-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box-strong" x="16" y="14" width="228" height="50" rx="4"/>
<text class="vx-text" x="28" y="35">1. Simplify</text>
<text class="vx-text-muted" x="28" y="54">before the inliner</text>
<rect class="vx-box-strong" x="266" y="14" width="228" height="50" rx="4"/>
<text class="vx-text" x="278" y="35">2. Inliner walk</text>
<text class="vx-mono" x="278" y="55">cgscc(devirt&lt;4&gt;(…))</text>
<rect class="vx-box-strong" x="516" y="14" width="228" height="50" rx="4"/>
<text class="vx-text" x="528" y="35">3. Optimize</text>
<text class="vx-text-muted" x="528" y="54">after the call-graph walk</text>
<path class="vx-line" d="M244 39 L265 39" marker-end="url(#o10-f4-head)"/>
<path class="vx-line" d="M494 39 L515 39" marker-end="url(#o10-f4-head)"/>
<rect class="vx-box-accent" x="285" y="166" width="150" height="19" rx="3"/>
<rect class="vx-box-accent" x="285" y="238" width="205" height="19" rx="3"/>
<rect class="vx-box-accent" x="285" y="292" width="65" height="19" rx="3"/>
<rect class="vx-box-accent" x="285" y="310" width="135" height="19" rx="3"/>
<rect class="vx-box-accent" x="535" y="184" width="143" height="19" rx="3"/>
<text class="vx-mono" x="26" y="90">function(</text>
<text class="vx-mono" x="40" y="108">simplifycfg, sroa,</text>
<text class="vx-mono" x="40" y="126">early-cse)</text>
<text class="vx-mono" x="26" y="144">ipsccp, globalopt</text>
<text class="vx-mono" x="26" y="162">function(</text>
<text class="vx-mono" x="40" y="180">mem2reg, instcombine,</text>
<text class="vx-mono" x="40" y="198">simplifycfg)</text>
<text class="vx-mono" x="26" y="216">always-inline</text>
<text class="vx-mono" x="26" y="234">require&lt;globals-aa&gt;</text>
<text class="vx-mono" x="276" y="90">inline, function-attrs, …</text>
<text class="vx-mono" x="276" y="108">function(</text>
<text class="vx-mono" x="290" y="126">sroa, early-cse, …</text>
<text class="vx-mono" x="290" y="144">simplifycfg, instcombine,</text>
<text class="vx-mono" x="290" y="162">…</text>
<text class="vx-mono" x="290" y="180">loop-mssa(…, licm,</text>
<text class="vx-mono" x="304" y="198">loop-rotate, licm,</text>
<text class="vx-mono" x="304" y="216">simple-loop-unswitch)</text>
<text class="vx-mono" x="290" y="234">simplifycfg, instcombine</text>
<text class="vx-mono" x="290" y="252">loop(loop-idiom, indvars,</text>
<text class="vx-mono" x="304" y="270">loop-deletion,</text>
<text class="vx-mono" x="304" y="288">loop-unroll-full)</text>
<text class="vx-mono" x="290" y="306">sroa, …</text>
<text class="vx-mono" x="290" y="324">gvn, sccp, bdce,</text>
<text class="vx-mono" x="290" y="342">instcombine, …, adce,</text>
<text class="vx-mono" x="290" y="360">memcpyopt, dse, …</text>
<text class="vx-mono" x="290" y="378">loop-mssa(licm), …</text>
<text class="vx-mono" x="290" y="396">simplifycfg, instcombine)</text>
<text class="vx-mono" x="276" y="414">function-attrs, …))</text>
<text class="vx-mono" x="276" y="438">deadargelim, globalopt,</text>
<text class="vx-mono" x="276" y="456">globaldce</text>
<text class="vx-mono" x="526" y="90">elim-avail-extern, …</text>
<text class="vx-mono" x="526" y="108">recompute-globalsaa</text>
<text class="vx-mono" x="526" y="126">function(</text>
<text class="vx-mono" x="540" y="144">…, loop(loop-rotate,</text>
<text class="vx-mono" x="554" y="162">loop-deletion),</text>
<text class="vx-mono" x="540" y="180">loop-distribute, …</text>
<text class="vx-mono" x="540" y="198">loop-vectorize, …</text>
<text class="vx-mono" x="540" y="216">loop-load-elim,</text>
<text class="vx-mono" x="540" y="234">instcombine, simplifycfg,</text>
<text class="vx-mono" x="540" y="252">slp-vectorizer, …</text>
<text class="vx-mono" x="540" y="270">instcombine,</text>
<text class="vx-mono" x="540" y="288">loop-unroll, …</text>
<text class="vx-mono" x="540" y="306">sroa, …, instcombine,</text>
<text class="vx-mono" x="540" y="324">loop-mssa(licm), …</text>
<text class="vx-mono" x="540" y="342">simplifycfg)</text>
<text class="vx-mono" x="526" y="360">globaldce, constmerge, …</text>
<rect class="vx-box-accent" x="16" y="470" width="22" height="14" rx="3"/>
<text class="vx-text-muted" x="46" y="481">outlined: positions the text discusses; … marks passes left out</text>
</svg>
<figcaption>Figure 4. The -O2 pipeline of LLVM 18.1.8 in outline, as <code>opt -passes='default&lt;O2&gt;' -print-pipeline-passes</code> prints it, with parameters removed and many passes left out. The first two columns are the simplification half and the third the optimization half. Outlined, from the top of column two: the first LICM runs, before the only GVN; the loop group whose passes do not keep MemorySSA; SROA after full unrolling; SCCP followed by dead-code elimination; and, in column three, the vectorizer.</figcaption>
</figure>

Almost every position in the outline is a phase-ordering decision, and the pipeline's source explains many of them in comments:[^llvm-pipelines]

- Each function's simplification starts with SROA, which turns local memory into SSA values.
- The loop passes form two groups. The first runs inside `loop-mssa(…)`. The second, `loop(…)`, holds loop idiom recognition, IndVarSimplify, loop deletion and full unrolling, which do not preserve MemorySSA, and a loop pipeline can keep MemorySSA only if every pass in it preserves it. MemorySSA's documentation says the same from the other side: a pass that adds, deletes or moves instructions must update MemorySSA through its update interface, as LICM does, or give it up.[^llvm-mssa] This is where O9's analysis is kept, and where it is thrown away.
- The first LICM runs come before the only GVN, the order the first two examples need.
- SROA runs again right after full unrolling, to delete small arrays whose indices unrolling made constant ([O7](o7-inlining-and-sroa.md)).
- SCCP is followed by dead-code elimination on bits, then InstCombine to fold what that exposed, and later aggressive dead-code elimination to catch the rest ([O5](o5-constants-and-dead-code.md)). A comment above SCCP admits that it is not clear why it runs after the loop passes rather than before.
- The vectorizer runs late, in the optimization half, after the whole call-graph walk. Loops are rotated again first, because earlier passes may have undone their rotation, and the global alias analysis is recomputed so that the vectorizer can use it.

Cleanup passes repeat because other passes keep making work for them. In the -O2 pipeline of LLVM 18.1.8, `instcombine` and `simplifycfg` each appear 8 times, `sroa` and `licm` 4 times each, and `gvn` once (counted in `-print-pipeline-passes` output on the owner's machine, 2026-09-24). The pipeline is a fixed sequence, not a loop run until nothing changes: it repeats cleanups at chosen points, and the repetition it does have, such as `devirt<4>`, is bounded. For passes that feed each other, Click and Cooper point to another way out: combine them into one analysis. Their combination of conditional constant propagation with value numbering finds facts that no number of repetitions of the two separate passes finds, and they name Wegman and Zadeck's algorithm, the one behind O5's SCCP, as an earlier combination of this kind.[^cc95]

## A pipeline for a language

LLVM's advice to front-end authors starts from the defaults: the -O2 and -O3 pipelines have been "carefully tuned for C and C++", and a new language will almost certainly need its own pass order.[^llvm-fe] Two of its suggestions fit Vortex. A language with many rarely failing guard conditions, such as null, type and range checks, should consider one or two extra runs of loop unswitching and LICM, since the standard order may leave checks in loops that could have been removed. A language with range checks should consider IRCE, which is not in the standard pass order.[^llvm-fe] LLVM 18.1.8 agrees: `irce` appears nowhere in `default<O1>`, `default<O2>` or `default<O3>`. The advice ends with a test anyone can run: put the optimized IR through -O2 again, and if it improves noticeably, the order needs work.[^llvm-fe]

A language does not have to replace the default pipeline to change it. `PassBuilder` has **extension points**, fixed places in its default pipelines, such as the start, where a front end registers a callback that adds its own passes.[^llvm-pb-h] Clang adds its sanitizer passes this way.[^npm]

For the kernel, the order decides what the vectorizer sees. [O8](o8-loops.md#why-the-checks-matter-for-the-vectorizer) showed that LLVM's vectorizer gives up on a loop whose trip count scalar evolution cannot compute. A loop as a front end first emits it ([O3](o3-ssa.md#stack-slots-and-mem2reg)) keeps its counter in a stack slot, and a counter in memory gives scalar evolution nothing to count. The last example runs the vectorizer, then SROA, then the vectorizer again, and prints the remark stream:

--8<-- "includes/examples/optimize/o10-pass-pipelines/vectorize_order.ll.md"

The first two remarks are the reasons: no induction variable, and therefore no trip count. Their pass name is empty because the width was forced, which makes the vectorizer report its analyses whatever filters are set.[^llvm-lvl] The third remark is the verdict, missed. Then SROA turns the stack slot into a phi, and the same pass with the same options reports a vectorized loop of width 4; the fourth remark only says that interleaving is off, as the flags asked. Nothing about the loop changed except what ran before the vectorizer. In the -O2 pipeline, SROA runs early and more than once, and the vectorizer runs after IndVarSimplify and loop deletion, which is where O8 found the kernel's checks gone and its trip counts computable.

A remark stream is therefore the evidence for an order. The serialized stream, written with `-pass-remarks-output`, is YAML with one record per remark.[^llvm-remarks] Saved once as a golden file, it turns "this order is right" into a test that fails, with a named reason, when a change to the pipeline turns a passed remark into a missed one.

??? check "After SROA, IndVarSimplify and O8's check removal, the kernel's `k` loop is still not vectorized. What else does the vectorizer need, and can any order of passes supply it?"

    Two facts, and no order supplies either. One is that `c` does not overlap `a` or `b`, which comes from the language's reference rule: the front end writes it into the IR as `noalias` ([O9](o9-alias-analysis.md#for-vortex)), so it exists before the first pass runs. The other is permission to reorder the additions into `sum`, which Vortex's strict floating point withholds ([P10](p10-vectorization.md)). Pass order decides only whether facts that passes can derive exist in time. It cannot derive a fact the program does not contain, or grant a permission the language does not give.

## For Vortex

!!! vortex "Exercise"

    **Decide first.** List the passes your middle end has, from constant propagation ([O5](o5-constants-and-dead-code.md#for-vortex)) to check removal ([O8](o8-loops.md#for-vortex)). For each one, write the unit it runs on (your compiler may have only functions and loops), the analyses it reads, and, for each analysis your compiler caches, whether the pass keeps it valid. Then choose a default order and justify every adjacency that matters by a named fact: LICM before value numbering (this chapter's first two examples), or constant propagation before dead-code elimination ([O5](o5-constants-and-dead-code.md)). If your back end is LLVM, also write the pipeline string you will hand it: the default level, what you add (IRCE, and any extra rounds of unswitching and LICM), and where, checked with `opt -print-pipeline-passes`.

    **Then build:**

    1. The pipeline as data: a list of pass names, each tagged with its unit, and a driver that runs it. A command-line option replaces the default list with a pipeline written as text, and reports an unknown name, or a name at the wrong unit, as an error.
    2. Preserved sets: each pass returns which cached analyses it kept valid, and the driver drops the rest after every pass.
    3. A log mode like `-debug-pass-manager`: one line for each pass run, each analysis computed and each analysis dropped.
    4. A print mode like `-print-pipeline-passes`, which prints the pipeline as it will run.
    5. A checking mode for preservation: after each pass, recompute every analysis it claimed to preserve and compare the result with the cached copy. A difference is a bug in that pass's answer.

    **Not yet:** a call-graph walk, unless you built [O7](o7-inlining-and-sroa.md#for-vortex)'s inliner, and then only the bottom-up order with each callee simplified before its callers; running passes in parallel; rerunning the pipeline until nothing changes, since a fixed list with deliberate repeats is enough; any search for the best order.

    **Proof that it works:**

    - A golden remark stream ([O1](o1-optimizer-contract.md#for-vortex)) for the stage 10 kernel with the default order, checked in with the test suite.
    - A swap that must change nothing: two passes that share no facts, swapped, give the same remark stream and the same output.
    - A swap that must change something: value numbering moved before LICM. The remark for the multiplication after the `k` loop, line (15) of [O6](o6-redundancy.md#your-turn-the-kernels-inner-loop)'s kernel, changes from passed to missed, with a reason.
    - LLVM's sanity check, applied to your own pipeline: run the optimized IR through it a second time. The second run changes nothing, or the log names what changed and the order is fixed.
    - The checking mode passes on the whole test suite.
    - A table filled in from your log, with the date and your compiler's version:

    | Pipeline | Passes run | Analyses computed | Analyses dropped | Remarks changed vs. default |
    | --- | --- | --- | --- | --- |
    | Default order | | | | none |
    | Default order, run twice | | | | |
    | Two independent passes swapped | | | | |
    | Value numbering before LICM | | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **Why can the same two passes produce two different programs?** A pass sees the program the earlier passes left: LICM's hoisted multiplication is what lets GVN find the redundancy. That is phase ordering, and no single order is best for every function.
    - **What does a pass manager keep between passes, and how does it know what to drop?** Cached analyses. Each pass returns a `PreservedAnalyses`, and after every pass the manager drops whatever the pass did not preserve.
    - **What are LLVM's IR units, and what does an adaptor add?** Module, CGSCC (optional), function and loop. An adaptor runs a smaller unit's pass on every smaller unit inside a larger one; the loop adaptor also canonicalizes loops and visits them innermost first.
    - **Why do `inline,sccp` and `cgscc(inline),sccp` build different pipelines?** The first nests `sccp` inside the call-graph walk, right after each component's inlining; the second runs it over every function once the walk is done.
    - **Why does -O2 contain InstCombine eight times?** Later passes keep creating work for it. LLVM repeats cleanups at chosen points instead of rerunning the pipeline until nothing changes.
    - **Why should a language with range checks choose its own pipeline?** LLVM's defaults are tuned for C and C++, and leave out IRCE and extra rounds of unswitching and LICM.
    - **How does a remark stream show that an order is right?** The same pass reports missed in one position and passed in another, and a golden remark file turns that difference into a failing test.

## Where this comes back

!!! next "You will use this again in"

    - [O11. Undefined behavior, poison and correct optimization](o11-undefined-behavior.md): *two passes that assume different rules about the same IR*
    - [O12. Testing an optimizer](o12-testing-optimizers.md): *golden remark streams*, *bisecting a pipeline*
    - [P7. Loop transformations](p7-loop-transformations.md): *passes the default pipeline leaves out*
    - [P10. Vectorization](p10-vectorization.md): *facts proven before the vectorizer runs*
    - [M3. Passes and pattern rewriting](../mlir/m3-passes-and-rewriting.md): *a textual pipeline anchored on operations*, *nesting*
    - [M9. Schedules as IR: the transform dialect](../mlir/m9-transform-dialect.md): *an order of transformations written as data*
    - [E1. The LLVM code generator pipeline](../backend/e1-llvm-codegen-pipeline.md): *the legacy pass manager, which still runs code generation*

## Sources and further reading

Start with LLVM's short guide to the new pass manager, then the blog post on why the old design could not serve the inliner. Then read the comments in `PassBuilderPipelines.cpp`, from `buildFunctionSimplificationPipeline` on: they are the nearest thing LLVM has to a design document for its pass order. Click and Cooper state the phase-ordering problem and one cure, and Kulkarni and his coauthors show how large the space of orders is.

[^npm]: LLVM Project, "Using the New Pass Manager", sections "Just Tell Me How To Run The Default Optimization Pipeline With The New Pass Manager", "Adding Passes to a Pass Manager", "Inserting Passes into Default Pipelines", "Using Analyses", "Invoking opt" and "Status of the New and Legacy Pass Managers". <https://llvm.org/docs/NewPassManager.html>
[^npm-blog]: Arthur Eubanks, "The New Pass Manager", LLVM Project Blog, 26 March 2021: sections "What is LLVM's new pass manager?", "Design" and "Making the new pass manager the default pass manager". <https://blog.llvm.org/posts/2021-03-26-the-new-pass-manager/>
[^writing-pass]: LLVM Project, "Writing an LLVM Pass": the `HelloWorldPass` example, its `run` method and its `PreservedAnalyses::all()` return. <https://llvm.org/docs/WritingAnLLVMNewPMPass.html>
[^llvm-pm]: LLVM Project, `PassManager.h`, release/18.x branch: `PassManager::run`, and the comment on `PreservedAnalyses`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/include/llvm/IR/PassManager.h>
[^llvm-lpm]: LLVM Project, `LoopPassManager.h`, release/18.x branch: the list of guarantees at the top of the file, and the `FunctionToLoopPassAdaptor` constructor. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/include/llvm/Transforms/Scalar/LoopPassManager.h>
[^llvm-cgscc]: LLVM Project, `CGSCCPassManager.h`, release/18.x branch: the comment on `DevirtSCCRepeatedPass`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/include/llvm/Analysis/CGSCCPassManager.h>
[^llvm-pb-h]: LLVM Project, `PassBuilder.h`, release/18.x branch: the comments on the `register…EPCallback` methods, among them `registerPipelineStartEPCallback`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/include/llvm/Passes/PassBuilder.h>
[^llvm-passbuilder]: LLVM Project, `PassBuilder.cpp`, release/18.x branch: `parsePassPipeline`, `isLoopPassName`, and how a loop pass name is parsed inside a function pipeline. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Passes/PassBuilder.cpp>
[^llvm-pipelines]: LLVM Project, `PassBuilderPipelines.cpp`, release/18.x branch: `buildPerModuleDefaultPipeline`, `buildInlinerPipeline`, and the comments in `buildFunctionSimplificationPipeline` and `buildModuleOptimizationPipeline`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Passes/PassBuilderPipelines.cpp>
[^llvm-licm]: LLVM Project, `LICM.cpp`, release/18.x branch: `LICMPass::run`, which stops when MemorySSA is missing, and `hoistRegion`, which skips blocks in inner loops. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Scalar/LICM.cpp>
[^llvm-mssa]: LLVM Project, "MemorySSA", section "Invalidation and updating". <https://llvm.org/docs/MemorySSA.html>
[^llvm-remarks]: LLVM Project, "Remarks", section "Serialized remarks" and the `-pass-remarks-output` option. <https://llvm.org/docs/Remarks.html>
[^llvm-lvl]: LLVM Project, `LoopVectorizationLegality.cpp`, release/18.x branch: `LoopVectorizeHints::vectorizeAnalysisPassName`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Vectorize/LoopVectorizationLegality.cpp>
[^llvm-fe]: LLVM Project, "Performance Tips for Frontend Authors", section "Pass Ordering". <https://llvm.org/docs/Frontend/PerformanceTips.html>
[^cc95]: Cliff Click and Keith D. Cooper, "Combining Analyses, Combining Optimizations", *ACM Transactions on Programming Languages and Systems* 17(2), 1995, pages 181 to 196: the abstract, section 1 and section 2.4, read in the preprint copy hosted by CiteSeerX. <https://doi.org/10.1145/201059.201061>
[^kul06]: Prasad A. Kulkarni, David B. Whalley, Gary S. Tyson and Jack W. Davidson, "Exhaustive Optimization Phase Order Space Exploration", *International Symposium on Code Generation and Optimization (CGO '06)*, 2006, pages 306 to 318: the abstract and section 1. <https://doi.org/10.1109/CGO.2006.15>; <https://www.ittc.ku.edu/~kulkarni/CARS/papers/cgo06.pdf>
