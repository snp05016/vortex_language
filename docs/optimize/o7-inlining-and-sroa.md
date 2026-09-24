# O7. Calls and aggregates: inlining and SROA

<p class="page-intro">A call hides the callee from the caller and the caller from the callee, and a struct kept in memory hides its fields from every analysis that works on values; inlining removes the first wall by copying a function's body into its caller, and scalar replacement of aggregates removes the second by splitting structs and small arrays into separate values. Together they let the Vortex kernel, and any helper it calls, reach the later passes as plain scalar code.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 45 minutes · Builds on: [O3. SSA form: construction and destruction](o3-ssa.md), [O5. Constants and dead code](o5-constants-and-dead-code.md)</p>

???+ remember "Before you start, remember"

    ??? question "What must a stack slot satisfy for mem2reg to promote it?"

        It must be an alloca in the entry block, hold a single value such as a scalar or a pointer, and be used only by direct loads and stores. A struct or an array is not a single value.

        Introduced in [O3. SSA form: construction and destruction](o3-ssa.md#stack-slots-and-mem2reg).

    ??? question "Why does a `let mut` local passed as `&mut` to a function stay in memory?"

        The callee receives its address, so the call might read or write it. Unless the call is inlined first, every read of the variable after the call is a load.

        Introduced in [O3. SSA form: construction and destruction](o3-ssa.md#stack-slots-and-mem2reg).

    ??? question "What does an optimistic analysis such as LLVM's `sccp` assume about blocks it has not reached yet?"

        That they are dead until proven reachable. A branch whose condition becomes a constant has only one live successor, and everything reachable only through the other one is never counted.

        Introduced in [O4. Dataflow analysis](o4-dataflow.md#optimism-where-to-start).

    ??? question "What must a Vortex optimizer keep when a runtime check fails?"

        Everything printed before the failure, then one error line with the kind and source position of the failing operation, then exit status 101.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#vortexs-list).

    ??? question "May a Vortex compiler pass a by-value struct or array argument by its address instead of copying it?"

        Yes. Parameters are immutable, and a variable lent as `&mut` may not appear in any other argument of the same call, so the callee can never tell a hidden address from a copy.

        Introduced in [Build v0.1, stage 8](../compiler/guide/stage-8-data-in-memory.md#references-and-passing-without-copying).

!!! goals "In this chapter"

    - Explain why inlining matters more for what it lets later passes see than for the call it removes.
    - Inline a call by hand: split the calling block, copy and rename the body, turn each return into a jump, and keep every operation's source position.
    - Estimate a call site's inline cost the way LLVM 18 does, and read the inliner's remarks, including its refusals.
    - Explain how SROA cuts a stack slot into slices and partitions, and predict when it must leave a slot alone.
    - Plan inlining and scalar replacement for Vortex under its rules for value semantics, `&mut`, strict floating point, recursion and runtime errors.

## A helper that hides the loop

Here is the kernel from [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for), at the size O1 to O3 use, with its multiplication moved into a helper, the way a programmer tidying the code might write it:

```vortex
// items: valid
fn product(a: &[f32; 64, 64], b: &[f32; 64, 64], row: i32, column: i32, k: i32) -> f32 {
    return a[row, k] * b[k, column];
}

fn multiply(a: &[f32; 64, 64], b: &[f32; 64, 64], c: &mut [f32; 64, 64]) {
    for row in 0..64 {
        for column in 0..64 {
            let mut sum: f32 = 0.0;
            for k in 0..64 {
                sum += product(&a, &b, row, column, k);
            }
            c[row, column] = sum;
        }
    }
}
```

The answer does not change. `product` rounds the product to `f32` and returns it, and `multiply` adds it to `sum`: the same two roundings, in the same order, as the stage 10 kernel, so every bit of `c` is the same ([decision 56](../decisions/numbers.md#d56)). What changes is what the optimizer can see.

In `multiply`, the loops say that `row`, `column` and `k` stay between 0 and 63. In `product` they are parameters, and a parameter of type `i32` can hold anything. The bounds checks on `a[row, k]` and `b[k, column]` therefore sit in the one function where nothing bounds the indices, and the range analysis of [O8](o8-loops.md), which could prove them in bounds from the loops, runs in the function where the checks are not. The call hides more than ranges:

- **Loop-invariant work.** The address of row `row` of `a` does not change while `k` runs, but it is computed inside `product`, once per call, where [O6](o6-redundancy.md)'s loop-invariant code motion cannot lift it out of a loop that is not there.
- **Vectorization.** LLVM's loop vectorizer handles a call in a loop body only when the call maps to an intrinsic or a vector version of the callee is available. For a call to an ordinary function such as `product`, it gives up with the remark "call instruction cannot be vectorized".[^llvm-lvl]
- **The call itself.** Every iteration passes five arguments, calls and returns: 262,144 calls for one 64 × 64 product.

**Inlining** removes the wall. It replaces a call with a copy of the called function's body, in which the parameters are replaced by the call's arguments. The function that makes the call is the **caller**, the called function is the **callee**, and the call instruction together with its position in the caller is a **call site**. Theodoridis, Grosser and Su list the benefits in that order: inlining removes the call's overhead, can shrink the program, and, which matters more, enables other optimizations.[^theodoridis] Inlined, `product` turns back into the stage 10 kernel, and everything this book does to that kernel applies again.

The C++ keyword `inline` is not what this chapter is about. Since C++98 it has meant that a function may be defined in several translation units; cppreference notes that inline substitution cannot be observed under the language's rules, so compilers may substitute calls to functions not marked `inline` and may call functions that are.[^cpp-inline] Vortex v0.1 has no such keyword and no hints, so every decision below is the compiler's own.

## Inlining by hand

A callee with several returns shows every step. This function limits a value to the range from 0 to 1, and `brighten` calls it:

```vortex
// items: valid
fn clamp01(x: f32) -> f32 {
    if x < 0.0 {
        return 0.0;
    }
    if x > 1.0 {
        return 1.0;
    }
    return x;
}

fn brighten(v: f32) -> f32 {
    return clamp01(v * 2.0) + 1.0;
}
```

In a control-flow graph ([O2](o2-cfg-and-dominance.md)), `brighten` is one block, `t = v * 2.0; r = clamp01(t); y = r + 1.0; return y`, and `clamp01` is five: a test `E`, a second test `F`, and three returns. Inlining one call takes five steps. LLVM's `InlineFunction.cpp` performs the first four, in a slightly different order (it copies the body before it splits the block), and the inliner pass in `Inliner.cpp` performs the fifth; a hand inliner takes the same steps:[^llvm-inline-function] [^llvm-inliner]

1. **Split the calling block at the call.** Everything after the call moves to a new block, the **continuation**, which will receive control when the inlined body finishes. Here the continuation starts with `y = r + 1.0`.
2. **Copy the callee's blocks into the caller, renaming as it goes.** Each parameter maps to its argument, so the copy of `x < 0.0` reads `t < 0.0`, and every value the callee defines gets a fresh name in the caller, as SSA form requires ([O3](o3-ssa.md)). The calling block now ends by entering the copy of `E`.
3. **Turn each return into a jump to the continuation.** When the callee returns from several places, a phi at the top of the continuation chooses the returned value by the edge that arrived; with a single return, the returned value replaces the call's result directly.
4. **Move the callee's fixed-size stack slots to the caller's entry block**, the only place where mem2reg and SROA, a pass covered later in this chapter, look for slots to remove.[^llvm-perftips]
5. **Delete the call.** If that was the last call to a function no other file can see, delete the function too.

<figure class="vx-figure">
<svg viewBox="0 0 760 540" role="img" aria-label="Inlining clamp01 into brighten: before, a call into a five-block callee; after, the callee's blocks inside brighten with every return turned into a jump to a join block" aria-describedby="o7-f1-desc">
<title id="o7-f1-title">Inlining a callee with three returns</title>
<desc id="o7-f1-desc">Top, before inlining. The block brighten holds t = v * 2.0, r = clamp01(t), y = r + 1.0 and return y; a moving dashed line labelled call runs from the call to the callee. The callee clamp01 has block E testing x less than 0.0, whose true edge goes to R1, return 0.0, and whose false edge goes to F, testing x greater than 1.0; F's true edge goes to R2, return 1.0, and its false edge to R3, return x. Bottom, after inlining. The block brighten holds t = v * 2.0 and then the test t less than 0.0. Its true edge goes to R1, now goto join, and its false edge to F, now testing t greater than 1.0, whose true edge goes to R2, goto join, and whose false edge goes to R3, goto join. The three edges into the block join are drawn as moving dashes labelled 0.0, 1.0 and t. The block join holds r = phi(R1: 0.0, R2: 1.0, R3: t), then y = r + 1.0 and return y.</desc>
<defs><marker id="o7-f1-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="24">Before: brighten calls clamp01</text>
<rect class="vx-box" x="20" y="40" width="190" height="116" rx="4"/>
<text class="vx-text" x="32" y="60">brighten</text>
<text class="vx-mono" x="32" y="82">t = v * 2.0</text>
<rect class="vx-box-accent" x="26" y="89" width="136" height="20" rx="3"/>
<text class="vx-mono" x="32" y="104">r = clamp01(t)</text>
<text class="vx-mono" x="32" y="126">y = r + 1.0</text>
<text class="vx-mono" x="32" y="148">return y</text>
<path class="vx-flow" d="M162 99 C 230 99, 240 57, 300 57" marker-end="url(#o7-f1-head)"/>
<text class="vx-text-accent" x="222" y="128">call</text>
<text class="vx-text-muted" x="300" y="32">clamp01(x)</text>
<rect class="vx-box" x="300" y="40" width="190" height="34" rx="4"/>
<text class="vx-text" x="312" y="62">E</text>
<text class="vx-mono" x="340" y="62">x &lt; 0.0 ?</text>
<rect class="vx-box" x="550" y="40" width="190" height="34" rx="4"/>
<text class="vx-text" x="562" y="62">R1</text>
<text class="vx-mono" x="596" y="62">return 0.0</text>
<rect class="vx-box" x="300" y="110" width="190" height="34" rx="4"/>
<text class="vx-text" x="312" y="132">F</text>
<text class="vx-mono" x="340" y="132">x &gt; 1.0 ?</text>
<rect class="vx-box" x="550" y="110" width="190" height="34" rx="4"/>
<text class="vx-text" x="562" y="132">R2</text>
<text class="vx-mono" x="596" y="132">return 1.0</text>
<rect class="vx-box" x="300" y="180" width="190" height="34" rx="4"/>
<text class="vx-text" x="312" y="202">R3</text>
<text class="vx-mono" x="340" y="202">return x</text>
<line class="vx-line" x1="490" y1="57" x2="550" y2="57" marker-end="url(#o7-f1-head)"/>
<text class="vx-text-muted" x="500" y="50">true</text>
<line class="vx-line" x1="395" y1="74" x2="395" y2="110" marker-end="url(#o7-f1-head)"/>
<text class="vx-text-muted" x="401" y="96">false</text>
<line class="vx-line" x1="490" y1="127" x2="550" y2="127" marker-end="url(#o7-f1-head)"/>
<text class="vx-text-muted" x="500" y="120">true</text>
<line class="vx-line" x1="395" y1="144" x2="395" y2="180" marker-end="url(#o7-f1-head)"/>
<text class="vx-text-muted" x="401" y="166">false</text>
<text class="vx-text-muted" x="550" y="202">each return goes back to brighten</text>
<line class="vx-line" x1="20" y1="252" x2="740" y2="252"/>
<text class="vx-text" x="20" y="286">After: the body in place of the call</text>
<rect class="vx-box" x="20" y="302" width="210" height="74" rx="4"/>
<text class="vx-text" x="32" y="322">brighten</text>
<text class="vx-mono" x="32" y="344">t = v * 2.0</text>
<text class="vx-mono" x="32" y="366">t &lt; 0.0 ?</text>
<rect class="vx-box" x="290" y="302" width="190" height="34" rx="4"/>
<text class="vx-text" x="302" y="324">R1</text>
<text class="vx-mono" x="336" y="324">goto join</text>
<rect class="vx-box" x="20" y="410" width="210" height="34" rx="4"/>
<text class="vx-text" x="32" y="432">F</text>
<text class="vx-mono" x="60" y="432">t &gt; 1.0 ?</text>
<rect class="vx-box" x="290" y="410" width="190" height="34" rx="4"/>
<text class="vx-text" x="302" y="432">R2</text>
<text class="vx-mono" x="336" y="432">goto join</text>
<rect class="vx-box" x="20" y="480" width="210" height="34" rx="4"/>
<text class="vx-text" x="32" y="502">R3</text>
<text class="vx-mono" x="66" y="502">goto join</text>
<rect class="vx-box-accent" x="540" y="372" width="200" height="152" rx="4"/>
<text class="vx-text" x="552" y="394">join</text>
<text class="vx-mono" x="552" y="418">r = phi(R1: 0.0,</text>
<text class="vx-mono" x="610" y="438">R2: 1.0,</text>
<text class="vx-mono" x="610" y="458">R3: t)</text>
<text class="vx-mono" x="552" y="484">y = r + 1.0</text>
<text class="vx-mono" x="552" y="508">return y</text>
<line class="vx-line" x1="230" y1="319" x2="290" y2="319" marker-end="url(#o7-f1-head)"/>
<text class="vx-text-muted" x="238" y="312">true</text>
<line class="vx-line" x1="125" y1="376" x2="125" y2="410" marker-end="url(#o7-f1-head)"/>
<text class="vx-text-muted" x="131" y="397">false</text>
<line class="vx-line" x1="230" y1="427" x2="290" y2="427" marker-end="url(#o7-f1-head)"/>
<text class="vx-text-muted" x="238" y="420">true</text>
<line class="vx-line" x1="125" y1="444" x2="125" y2="480" marker-end="url(#o7-f1-head)"/>
<text class="vx-text-muted" x="131" y="466">false</text>
<path class="vx-flow" d="M480 319 L610 319 L610 372" marker-end="url(#o7-f1-head)"/>
<text class="vx-text-accent" x="520" y="312">0.0</text>
<line class="vx-flow" x1="480" y1="427" x2="540" y2="427" marker-end="url(#o7-f1-head)"/>
<text class="vx-text-accent" x="494" y="420">1.0</text>
<line class="vx-flow" x1="230" y1="497" x2="540" y2="497" marker-end="url(#o7-f1-head)"/>
<text class="vx-text-accent" x="380" y="490">t</text>
</svg>
<figcaption>Figure 1. Inlining <code>clamp01</code> into <code>brighten</code>. Top: the call hands <code>t</code> to the parameter <code>x</code>, and each of the three returns goes back to the caller. Bottom: copies of the callee's blocks now sit in <code>brighten</code> with <code>t</code> in place of <code>x</code>; the calling block runs straight into the first test, each return became a jump to the continuation <code>join</code>, and the phi in <code>join</code> picks the returned value by the edge it arrived on. The moving dashes are the edges that carry a return value.</figcaption>
</figure>

After inlining, the optimizations of earlier chapters get to work on the result. If `brighten` were inlined in turn into a caller that passes a constant, [O5](o5-constants-and-dead-code.md)'s constant propagation would fold both tests, and the untaken returns would become dead code. Inlining makes this possible without doing any of it itself; it only puts the callee's code where the caller's facts can reach it.

Two details of the copy matter for correctness. First, a copied operation keeps the source position it had in the callee. LLVM gives every inlined instruction a debug location that keeps its own line, column and scope and adds an **inlined-at** link, a reference to the call site it was copied into, so the location records both where the operation was written and where it now runs.[^llvm-inline-function] [^langref-dilocation]

Second, promises attached to the callee's parameters must survive the copy. A `noalias` parameter promises that while the call runs, memory reached through that pointer is not also reached through any pointer not derived from it, whenever something modifies that memory during the call.[^langref-attrs] Once the call is gone there is no "during the call", so LLVM turns each such promise into **alias scope** metadata: labels on the inlined loads and stores that say which of them cannot touch the same memory, with a new scope domain for every inlining.[^langref-noalias] [^llvm-inline-function] [O9](o9-alias-analysis.md) reads that metadata.

## Deciding: the cost of a call site

Copying every callee into every caller would make a program enormous, and a recursive function has no end to copy. So an inliner decides call site by call site. Choosing the best set of call sites is hard in a precise sense: Theodoridis and his coauthors call the general problem as hard as the NP-complete knapsack problem, citing Scheifler's analysis from 1977, and describe good choices as depending both on the other inlining choices and on the rest of the optimization pipeline.[^theodoridis] Compilers therefore decide with heuristics. LLVM's is greedy: for each call site, estimate what inlining would add, compare the estimate with a limit, and decide.

LLVM 18's rule, in `InlineCost.cpp`, has four parts.[^llvm-inline-cost]

- **Cost.** The analysis walks the callee's instructions and adds 5 units (`inline-instr-cost`) for each one it expects to survive inlining. It starts from a credit for what disappears with the call: 5 per argument, 5 for the call instruction, and a call penalty of 25. A two-argument call therefore starts at −40.
- **Context.** The walk treats the call's arguments as known. When an argument is a constant, instructions that fold are free, and when a branch folds, the blocks on the untaken side are never visited. The source calls these control-flow simplifications the most important thing it tracks, because proving a block dead can shift the cost dramatically. This is the optimistic view of [O4](o4-dataflow.md#optimism-where-to-start), applied to one call site at a time.
- **Threshold.** The cost is compared with a **threshold**, 225 by default. The threshold moves with the call site: it rises by half when the callee, with this call's arguments in place, runs as one straight path with no branch left to decide, and it rises for callees dense in vector instructions. The source itself calls these bonuses somewhat arbitrary, grown over time partly by accident. Hints and profiles move the threshold too: a callee marked `inlinehint` gets at least 325, and profile data can mark a call site hot or cold.
- **Decision.** The call is inlined when the cost is below the threshold.

Other defaults live in `InlineCost.h`: a threshold of 250 at `-O3`, 50 for functions optimized for size (`-Os`) and 5 for minimum size (`-Oz`), and a bonus of 15,000 when the call is the last one to a function with internal linkage. `InlineCost.cpp` explains that bonus: inlining the last call to such a function is guaranteed to reduce code size, because the function can then be deleted.[^llvm-inline-cost-h] [^llvm-inline-cost] Some decisions do not depend on the numbers. A callee marked `noinline` is refused outright, one marked `alwaysinline` is inlined whenever possible, ignoring the threshold, and a callee that calls itself is refused as recursive as soon as the walk reaches that call.[^llvm-inline-cost] [^langref-attrs]

Theodoridis and his coauthors measured how far such a rule is from the best choice, for code size. They reduced the search space enough to try every combination of inlining decisions for 1,135 files of the SPEC2017 benchmarks, found a significant gap between LLVM's heuristic and the optimum, and reported that a simple autotuning strategy beat LLVM's heuristic by 7% on average on SPEC2017 (2022).[^theodoridis]

The first example asks LLVM 18 for its decisions on five call sites and prints its remarks, the YAML records that [O1](o1-optimizer-contract.md#remarks-the-optimizers-report) introduced:

--8<-- "includes/examples/optimize/o7-inlining-and-sroa/inline_cost.ll.md"

Every number can be checked by hand. The first call to `@shade` passes the constant 0, so the comparison and the branch fold, the slow block is never visited, and nothing is counted beyond the credit: cost −40. With the branch folded, the callee runs as one straight path, so the threshold is 225 plus half of it, rounded down: 337. The second call passes an unknown `%mode`. Both blocks are live, the single-block bonus is withdrawn, and nine instructions are counted: −40 + 45 = 5, against 225. The call to `@square` gets the last-call bonus: −35 for a one-argument call, plus 5 for its multiply, minus 15,000. The same call that was cheap enough in `@fast_caller` costs 5 in `@small_caller` as well, but that caller carries `minsize`, so its threshold is 5, and 5 is not below 5: the remark is `TooCostly`. The call from `@factorial` to itself is refused as recursive, marked `(cost=never)`, before any counting.

??? check "A callee has two parameters and 60 instructions. Its first block tests the second parameter and branches to one of two halves of 29 instructions each. Using the rule above, which of two calls is inlined at the default threshold: one whose second argument is a constant, and one whose second argument is unknown?"

    The constant call counts about one half: −40 + 29 × 5 = 105. The folded callee is a single path, so the threshold is 337, and the call is inlined. The unknown call counts everything: −40 + 60 × 5 = 260, against 225, so it is refused as too costly. The callee is the same; the call sites differ. (LLVM's own count differs slightly: for example, it treats the first return it meets as free.)

## Deciding in order: bottom-up over the call graph

The costs above assume the callee's body is final. It is not, if the callee has calls of its own that will be inlined later. The order in which an inliner visits functions decides which version of each callee it measures.

The structure that fixes the order is the **call graph**: one node per function, and an edge from each caller to each function it calls. Recursion makes cycles. Functions that can all reach one another through calls form a **strongly connected component**: `eval` and `apply` below, which call each other, form one, and a function that is not recursive forms a component on its own. Tarjan's algorithm finds the components in time proportional to the size of the graph, and LLVM's implementation of it, `scc_iterator`, has the property the inliner needs: when a function in component S1 calls a function in component S2, it visits S1 after S2.[^llvm-scc-iterator] Visiting the components in that order is called **bottom-up**, or post-order.

LLVM's inliner is bottom-up, and it runs inside a walk that, for each component, first inlines calls and then runs the function simplification pipeline on each of its functions, SROA among them.[^llvm-inliner] [^llvm-pipelines] The header of LLVM's call-graph pass manager states the consequence: at each call edge, the callee has already been optimized as much as possible.[^llvm-cgscc] A callee measured bottom-up is final: its own calls are already inlined, and its body is already simplified. Recursion is visible too: a call whose callee lies in the component being processed may lead back to its own caller, and such calls need special care.

The second example runs this scheme on a toy call graph: a calculator program with a threshold of 12 and a rule that never inlines a recursive callee.

--8<-- "includes/examples/optimize/o7-inlining-and-sroa/bottom_up.cpp.md"

Figure 2 draws the same run.

<figure class="vx-figure">
<div class="vx-stepper">
<div class="vx-step">
<p><strong>Step 1. The call graph.</strong> Six functions with their sizes, 58 in all. <code>eval</code> and <code>apply</code> call each other, so they form one component (dashed outline). The bottom-up order is <code>sq</code>, <code>norm</code>, <code>eval</code> and <code>apply</code>, <code>report</code>, <code>main</code>.</p>
<svg viewBox="0 0 760 330" role="img" aria-label="Bottom-up inlining step 1: the call graph with main 5, eval 10, apply 8, norm 3, sq 2 and report 30; main calls eval, report and norm; eval and apply call each other; eval calls norm; norm calls sq twice. Total size 58, threshold 12.">
<defs><marker id="o7-f2a-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-line" x="30" y="100" width="140" height="146" rx="8" stroke-dasharray="5 4"/>
<line class="vx-line" x1="215" y1="60" x2="130" y2="110" marker-end="url(#o7-f2a-head)"/>
<line class="vx-line" x1="255" y1="60" x2="340" y2="110" marker-end="url(#o7-f2a-head)"/>
<line class="vx-line" x1="235" y1="60" x2="235" y2="200" marker-end="url(#o7-f2a-head)"/>
<line class="vx-line" x1="85" y1="146" x2="85" y2="200" marker-end="url(#o7-f2a-head)"/>
<line class="vx-line" x1="115" y1="200" x2="115" y2="146" marker-end="url(#o7-f2a-head)"/>
<line class="vx-line" x1="160" y1="140" x2="200" y2="200" marker-end="url(#o7-f2a-head)"/>
<line class="vx-line" x1="235" y1="236" x2="235" y2="280" marker-end="url(#o7-f2a-head)"/>
<text class="vx-text-muted" x="243" y="264">×2</text>
<rect class="vx-box" x="175" y="24" width="120" height="36" rx="4"/>
<text class="vx-mono" x="235" y="47" text-anchor="middle">main 5</text>
<rect class="vx-box" x="40" y="110" width="120" height="36" rx="4"/>
<text class="vx-mono" x="100" y="133" text-anchor="middle">eval 10</text>
<rect class="vx-box" x="310" y="110" width="120" height="36" rx="4"/>
<text class="vx-mono" x="370" y="133" text-anchor="middle">report 30</text>
<rect class="vx-box" x="40" y="200" width="120" height="36" rx="4"/>
<text class="vx-mono" x="100" y="223" text-anchor="middle">apply 8</text>
<rect class="vx-box" x="175" y="200" width="120" height="36" rx="4"/>
<text class="vx-mono" x="235" y="223" text-anchor="middle">norm 3</text>
<rect class="vx-box" x="175" y="280" width="120" height="36" rx="4"/>
<text class="vx-mono" x="235" y="303" text-anchor="middle">sq 2</text>
<text class="vx-text" x="490" y="40">Before any inlining</text>
<text class="vx-text-muted" x="490" y="70">total size 58</text>
<text class="vx-text-muted" x="490" y="92">threshold 12</text>
<text class="vx-text-muted" x="490" y="114">never inline a recursive callee</text>
<text class="vx-text-muted" x="490" y="146">components, bottom-up:</text>
<text class="vx-mono" x="490" y="168">sq, norm, eval+apply,</text>
<text class="vx-mono" x="490" y="188">report, main</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 2. Visit <code>sq</code>, then <code>norm</code>.</strong> <code>sq</code> calls nothing. Both calls from <code>norm</code> to <code>sq</code> are small enough, so <code>norm</code> grows from 3 to 7, and <code>sq</code>, which nobody calls any more, is deleted.</p>
<svg viewBox="0 0 760 330" role="img" aria-label="Bottom-up inlining step 2: sq is inlined into norm twice, norm grows to 7 and sq is deleted.">
<defs><marker id="o7-f2b-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-line" x="30" y="100" width="140" height="146" rx="8" stroke-dasharray="5 4"/>
<line class="vx-line" x1="215" y1="60" x2="130" y2="110" marker-end="url(#o7-f2b-head)"/>
<line class="vx-line" x1="255" y1="60" x2="340" y2="110" marker-end="url(#o7-f2b-head)"/>
<line class="vx-line" x1="235" y1="60" x2="235" y2="200" marker-end="url(#o7-f2b-head)"/>
<line class="vx-line" x1="85" y1="146" x2="85" y2="200" marker-end="url(#o7-f2b-head)"/>
<line class="vx-line" x1="115" y1="200" x2="115" y2="146" marker-end="url(#o7-f2b-head)"/>
<line class="vx-line" x1="160" y1="140" x2="200" y2="200" marker-end="url(#o7-f2b-head)"/>
<rect class="vx-box" x="175" y="24" width="120" height="36" rx="4"/>
<text class="vx-mono" x="235" y="47" text-anchor="middle">main 5</text>
<rect class="vx-box" x="40" y="110" width="120" height="36" rx="4"/>
<text class="vx-mono" x="100" y="133" text-anchor="middle">eval 10</text>
<rect class="vx-box" x="310" y="110" width="120" height="36" rx="4"/>
<text class="vx-mono" x="370" y="133" text-anchor="middle">report 30</text>
<rect class="vx-box" x="40" y="200" width="120" height="36" rx="4"/>
<text class="vx-mono" x="100" y="223" text-anchor="middle">apply 8</text>
<rect class="vx-box-accent" x="175" y="200" width="120" height="36" rx="4"/>
<text class="vx-mono" x="235" y="223" text-anchor="middle">norm 7</text>
<rect class="vx-box-bad" x="175" y="280" width="120" height="36" rx="4"/>
<text class="vx-text-muted" x="235" y="303" text-anchor="middle">sq deleted</text>
<text class="vx-text" x="490" y="40">Visit sq, then norm</text>
<text class="vx-mono" x="490" y="70">inline sq into norm: 5</text>
<text class="vx-mono" x="490" y="92">inline sq into norm: 7</text>
<text class="vx-mono" x="490" y="114">delete sq</text>
<text class="vx-text-muted" x="490" y="146">total size 60</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 3. Visit <code>eval</code> and <code>apply</code>.</strong> The calls between them stay: each callee is recursive. <code>norm</code> is final and small, so <code>eval</code> absorbs it and grows to 17. <code>norm</code> survives, because <code>main</code> still calls it.</p>
<svg viewBox="0 0 760 330" role="img" aria-label="Bottom-up inlining step 3: eval and apply keep their calls to each other, norm is inlined into eval, and eval grows to 17.">
<defs><marker id="o7-f2c-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-line" x="30" y="100" width="140" height="146" rx="8" stroke-dasharray="5 4"/>
<line class="vx-line" x1="215" y1="60" x2="130" y2="110" marker-end="url(#o7-f2c-head)"/>
<line class="vx-line" x1="255" y1="60" x2="340" y2="110" marker-end="url(#o7-f2c-head)"/>
<line class="vx-line" x1="235" y1="60" x2="235" y2="200" marker-end="url(#o7-f2c-head)"/>
<line class="vx-line" x1="85" y1="146" x2="85" y2="200" marker-end="url(#o7-f2c-head)"/>
<line class="vx-line" x1="115" y1="200" x2="115" y2="146" marker-end="url(#o7-f2c-head)"/>
<text class="vx-text-muted" x="100" y="264" text-anchor="middle">recursive: calls kept</text>
<rect class="vx-box" x="175" y="24" width="120" height="36" rx="4"/>
<text class="vx-mono" x="235" y="47" text-anchor="middle">main 5</text>
<rect class="vx-box-accent" x="40" y="110" width="120" height="36" rx="4"/>
<text class="vx-mono" x="100" y="133" text-anchor="middle">eval 17</text>
<rect class="vx-box" x="310" y="110" width="120" height="36" rx="4"/>
<text class="vx-mono" x="370" y="133" text-anchor="middle">report 30</text>
<rect class="vx-box-accent" x="40" y="200" width="120" height="36" rx="4"/>
<text class="vx-mono" x="100" y="223" text-anchor="middle">apply 8</text>
<rect class="vx-box-strong" x="175" y="200" width="120" height="36" rx="4"/>
<text class="vx-mono" x="235" y="223" text-anchor="middle">norm 7</text>
<text class="vx-text" x="490" y="40">Visit eval and apply</text>
<text class="vx-mono" x="490" y="70">keep eval → apply: recursive</text>
<text class="vx-mono" x="490" y="92">inline norm into eval: 17</text>
<text class="vx-mono" x="490" y="114">keep apply → eval: recursive</text>
<text class="vx-text-muted" x="490" y="146">total size 67</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 4. Visit <code>report</code>, then <code>main</code>.</strong> <code>main</code> keeps its call to the recursive <code>eval</code> and to <code>report</code>, which is too big, and absorbs <code>norm</code>, which is then deleted. The program ends at 67: bigger than the 58 it started with, with fewer calls on its paths.</p>
<svg viewBox="0 0 760 330" role="img" aria-label="Bottom-up inlining step 4: main keeps its calls to eval and report, norm is inlined into main, main grows to 12 and norm is deleted. Total size 67.">
<defs><marker id="o7-f2d-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-line" x="30" y="100" width="140" height="146" rx="8" stroke-dasharray="5 4"/>
<line class="vx-line" x1="215" y1="60" x2="130" y2="110" marker-end="url(#o7-f2d-head)"/>
<line class="vx-line" x1="255" y1="60" x2="340" y2="110" marker-end="url(#o7-f2d-head)"/>
<line class="vx-line" x1="85" y1="146" x2="85" y2="200" marker-end="url(#o7-f2d-head)"/>
<line class="vx-line" x1="115" y1="200" x2="115" y2="146" marker-end="url(#o7-f2d-head)"/>
<rect class="vx-box-accent" x="175" y="24" width="120" height="36" rx="4"/>
<text class="vx-mono" x="235" y="47" text-anchor="middle">main 12</text>
<rect class="vx-box-strong" x="40" y="110" width="120" height="36" rx="4"/>
<text class="vx-mono" x="100" y="133" text-anchor="middle">eval 17</text>
<rect class="vx-box-accent" x="310" y="110" width="120" height="36" rx="4"/>
<text class="vx-mono" x="370" y="133" text-anchor="middle">report 30</text>
<rect class="vx-box-strong" x="40" y="200" width="120" height="36" rx="4"/>
<text class="vx-mono" x="100" y="223" text-anchor="middle">apply 8</text>
<rect class="vx-box-bad" x="175" y="200" width="120" height="36" rx="4"/>
<text class="vx-text-muted" x="235" y="223" text-anchor="middle">norm deleted</text>
<text class="vx-text" x="490" y="40">Visit report, then main</text>
<text class="vx-mono" x="490" y="70">keep main → eval: recursive</text>
<text class="vx-mono" x="490" y="92">keep main → report: too big</text>
<text class="vx-mono" x="490" y="114">inline norm into main: 12</text>
<text class="vx-mono" x="490" y="136">delete norm</text>
<text class="vx-text-muted" x="490" y="168">total size 67</text>
</svg>
</div>
</div>
<figcaption>Figure 2. The second example's run, one component at a time. Functions visited in the current step have the accent outline, functions visited in earlier steps a heavier outline, and deleted functions a dashed one. Each size is a function's instructions other than calls; an inlined call adds the callee's current size to the caller.</figcaption>
</figure>

A real inliner must also keep working after its first pass over a component's calls. Inlining a callee copies the callee's remaining calls into the caller, and those new call sites need decisions too. LLVM appends them to the end of its worklist and records, for each, which inlined function it came from. This **inline history** lets the inliner refuse to inline a function back into code that came from inlining that same function, which could otherwise go on forever in some unusual call graphs.[^llvm-inliner] The toy example sidesteps the problem with its blunter rule of never inlining a recursive callee.

??? check "In the second example, `main` decides about `norm` after `norm` has absorbed both calls to `sq`. What would a top-down inliner, deciding for `main` first, have measured, and what would it have had to do afterwards?"

    It would have measured `norm` at size 3, before `sq` was inlined into it, and inlined that version into `main`. The two calls to `sq` would then appear in `main` as new call sites, each needing its own decision, and the same happens again in `eval`, so the work of inlining `sq` is repeated in every caller of `norm`. Bottom-up, `sq` is inlined once, into `norm`, and every later decision sees the finished `norm`.

## What a Vortex inliner must keep

Inlining is correct when the program with the call and the program with the copied body behave identically, and for most code that is automatic: the same operations run on the same values in the same order. Vortex's contract ([O1](o1-optimizer-contract.md#what-an-optimizer-must-keep)) adds five places to look.

**Source positions of checks.** A bounds check copied from `product` into `multiply` must still report the position of `a[row, k]` in `product`, where the failing operation is written, not the call in `multiply`. If each check passes its position to the runtime's error routine as constant arguments, the copy carries the constants along and nothing more is needed. If positions come from anything that says "the current function", inlining breaks them.

**Strict floating point.** Inlining never changes a floating-point result by itself, but it puts operations next to each other that a call kept apart: after inlining `product`, its multiply sits directly before `multiply`'s addition. A mode that fuses across statements, such as Clang's `-ffp-contract=fast`,[^clang-fp] could now turn the pair into one fused multiply-add. Decision 56 forbids fusion, so a Vortex compiler emits no fusion permission anywhere and inlining has nothing to spread. When a relaxed mode arrives as an opt-in, attach it to operations, not functions: LLVM IR does that, and it requires every instruction involved in a fusion or reassociation to carry the permission, so a strict callee inlined into a relaxed caller stays strict.[^langref-fmf]

**`&mut` exclusivity.** A `&mut` parameter is exclusive for the whole call ([decision 25](../decisions/references.md#d25)), and O1 suggested marking it `noalias`. After inlining there is no call, so the fact must be kept another way: LLVM's alias scopes, described above, or an equivalent record in your own IR.

**Value semantics.** Vortex copies structs and arrays passed by value, and a compiler may pass them by hidden address instead because the callee cannot tell ([decision 25](../decisions/references.md#d25)). That argument does not cover the callee's result. In `result = times(result, z)`, with `p` passed as the address of `result`, a callee that wrote its result directly into `result` would change its own argument `p` while still reading it. The straightforward lowering gives the result a temporary and copies it into `result` after the call. The copy looks wasteful, but after inlining, SROA removes both the temporary and the copy, as the fourth example shows below.

**Recursion and the stack.** Vortex allows recursion ([decision 3](../decisions/names.md#d3)), so the inliner needs a rule that keeps it from expanding a cycle forever (the simplest: never inline a call whose callee is in the caller's own component), and a rule for stack use. Inlining moves the callee's fixed-size slots into the caller's frame,[^llvm-inline-function] so a recursive function that absorbs a callee with a large local array uses more stack at every level of the recursion. LLVM refuses exactly that case: it does not inline a callee whose slots total more than 1,024 bytes into a recursive caller.[^llvm-inline-cost] [^llvm-inline-cost-h] [Decision 46](../decisions/diagnostics.md#d46) requires running out of stack to stop the program with a `stack` report, but whether a given recursion depth runs out now depends on inlining decisions, and so on the optimization level. O1's exercise asked whether that is allowed; your inliner is where the answer takes effect.

## Aggregates in memory

After inlining, many values that crossed the call boundary as structs are left in stack slots. A front end usually gives every struct or array local a slot, an alloca of an **aggregate** type (a struct or an array), and reads and writes fields through addresses. [O3](o3-ssa.md#stack-slots-and-mem2reg) showed that mem2reg promotes only slots holding a single value. LLVM's advice to front-end authors goes further: avoid creating values of aggregate type at all, and load and store individual fields instead.[^llvm-perftips] That leaves a slot per struct, used field by field, which is exactly the input the next pass needs.

**Scalar replacement of aggregates**, **SROA** for short, replaces such a slot with one slot per piece that its uses touch, and then promotes each piece to SSA values as mem2reg would; LLVM's pass documentation describes it as breaking aggregate allocas into individual allocas per member and then into scalar SSA form.[^llvm-passes] LLVM's implementation works in byte ranges rather than fields, so a copy of a whole struct and a load of one field are described in the same terms.[^llvm-sroa]

1. **Slices.** Every use of the slot becomes a **slice**: the range of bytes it touches, and whether the use can be cut into pieces. A load or store of one `float` cannot be cut. A `memcpy` or `memset` can, since copying bytes 0 to 7 is the same as copying 0 to 3 and then 4 to 7, and so can an ordinary load or store of an integer, which front ends sometimes use to move raw bytes.
2. **Partitions.** Slices that cannot be cut and overlap one another are grouped into a **partition**, a byte range that must stay together. Slices that can be cut are split at partition boundaries.
3. **Rewriting.** Each partition becomes a new slot of a type that fits its uses, a `float`, an integer, or a vector, and each use is rewritten to address the new slot.
4. **Promotion.** The new slots are promoted to SSA values, so SROA also does the work of mem2reg.

Figure 3 follows this for the slot `%pos` of the fourth example, in the next section: a two-`float` struct called `Vec2`, after inlining has put every use of it in view.

<figure class="vx-figure">
<svg viewBox="0 0 760 360" role="img" aria-label="SROA on an eight-byte Vec2 slot: five slices, two partitions, two new float slots, and two phis" aria-describedby="o7-f3-desc">
<title id="o7-f3-title">SROA cuts a slot into partitions</title>
<desc id="o7-f3-desc">A slot named pos of eight bytes: field x in bytes 0 to 3 and field y in bytes 4 to 7. Below it, the slices of its uses: a store and a load of x covering bytes 0 to 3, a store and a load of y covering bytes 4 to 7, and a memcpy covering all eight bytes, drawn dashed and marked as splittable. Below them, two partitions, bytes 0 to 3 and bytes 4 to 7; the memcpy is cut at byte 4. Below those, two new slots of type float. At the bottom, the SSA values after promotion: two phis in the loop header, one for x and one for y. The rows light up from top to bottom.</desc>
<text class="vx-text-muted" x="20" y="60">slot %pos</text>
<rect class="vx-box-strong" x="170" y="36" width="200" height="36"/>
<rect class="vx-box-strong" x="370" y="36" width="200" height="36"/>
<text class="vx-mono" x="270" y="59" text-anchor="middle">x : float</text>
<text class="vx-mono" x="470" y="59" text-anchor="middle">y : float</text>
<text class="vx-text-muted" x="170" y="88" text-anchor="middle">0</text>
<text class="vx-text-muted" x="370" y="88" text-anchor="middle">4</text>
<text class="vx-text-muted" x="570" y="88" text-anchor="middle">8</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 4">
<text class="vx-text-muted" x="20" y="130">slices</text>
<rect class="vx-box" x="170" y="100" width="200" height="22" rx="3"/>
<text class="vx-mono" x="180" y="116">store x, load x</text>
<rect class="vx-box" x="370" y="100" width="200" height="22" rx="3"/>
<text class="vx-mono" x="380" y="116">store y, load y</text>
<rect class="vx-box" x="170" y="130" width="400" height="22" rx="3" stroke-dasharray="6 4"/>
<text class="vx-mono" x="180" y="146">memcpy from %next</text>
<text class="vx-text-muted" x="580" y="146">can be cut</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4">
<text class="vx-text-muted" x="20" y="196">partitions</text>
<rect class="vx-box-accent" x="172" y="178" width="194" height="28" rx="3"/>
<text class="vx-mono" x="269" y="197" text-anchor="middle">bytes 0 to 3</text>
<rect class="vx-box-accent" x="374" y="178" width="194" height="28" rx="3"/>
<text class="vx-mono" x="471" y="197" text-anchor="middle">bytes 4 to 7</text>
<text class="vx-text-muted" x="580" y="197">memcpy cut at 4</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4">
<text class="vx-text-muted" x="20" y="252">new slots</text>
<rect class="vx-box" x="172" y="234" width="194" height="28" rx="3"/>
<text class="vx-mono" x="269" y="253" text-anchor="middle">float slot</text>
<rect class="vx-box" x="374" y="234" width="194" height="28" rx="3"/>
<text class="vx-mono" x="471" y="253" text-anchor="middle">float slot</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 4">
<text class="vx-text-muted" x="20" y="316">SSA values</text>
<text class="vx-mono" x="170" y="306">%pos.sroa.0.0 = phi float</text>
<text class="vx-mono" x="170" y="326">  [0.0, %entry], [%x.i, %body]</text>
<text class="vx-mono" x="460" y="306">%pos.sroa.4.0 = phi float</text>
<text class="vx-mono" x="460" y="326">  [0.0, %entry], [%y.i, %body]</text>
</g>
</svg>
<figcaption>Figure 3. SROA on the slot <code>%pos</code> of the fourth example, after inlining. Each use is a slice: a byte range, and whether it can be cut. The loads and stores of single floats cannot be cut, so they fix two partitions; the <code>memcpy</code> can, and is cut between them. Each partition becomes a new <code>float</code> slot, and promotion turns each slot into SSA values, the two phis of the loop header. The rows light up in that order.</figcaption>
</figure>

If one use had read bytes 0 to 7 as a single `double`, that slice could not be cut, and it would overlap both fields: the whole struct would be one partition. SROA gives up on a slot altogether in two situations, both visible in `SROA.cpp`'s slice builder: when the slot's address **escapes**, meaning it is stored somewhere, passed to a call, or otherwise used in a way the pass cannot follow, and when a use touches the slot at an offset that is not a constant.[^llvm-sroa] The third example shows both refusals next to two successes:

--8<-- "includes/examples/optimize/o7-inlining-and-sroa/sroa_split.ll.md"

`@length_squared` and `@copy_then_read` lost their slots entirely; in the second, the `memcpy` was split field by field, and the load of `w.y` became the argument `%y`. `@pick` kept its array, because `a[i]` with an unknown `i` could touch any element. `@shown` kept its struct, because `@inspect` received the address.

The array case explains a detail of LLVM's pipeline. It runs SROA early in each function's simplification, again right after loops are **fully unrolled**, which means copied once per iteration so that no loop is left, and once more late in the pipeline, because unrolling may have turned variable offsets into constants. A comment at the second place says the pass is there to delete small arrays.[^llvm-pipelines] A loop `for i in 0..4` over a local four-element array cannot be scalar-replaced while `i` varies. Unrolled by its whole trip count ([P7](p7-loop-transformations.md#unrolling)), it becomes four copies with `i` equal to 0, 1, 2 and 3, and then it can. The same two steps can turn the small local array of accumulators that a register-blocked matrix kernel keeps into separate values ([P12](p12-fast-gemm.md)). [P7](p7-loop-transformations.md) met a transformation with a similar name, Callahan, Carr and Kennedy's scalar replacement, which rewrites references to a reused array element as references to a scalar temporary; SROA works on whole local aggregates.

??? check "Which of these stack slots can SROA replace: (a) a local `Vec2` whose fields are read and written directly; (b) a local four-element array indexed by the variable of a loop that has not been unrolled; (c) a local struct passed as `&mut` to a function that was not inlined?"

    (a) Yes: every use is at a constant offset and the address goes nowhere. (b) Not yet: the offset of each access varies, so SROA leaves the whole array alone until full unrolling makes every index a constant. (c) No: the address escapes into the call. Inlining the call would make every use visible, which is the subject of the next section.

## Why they come as a pair

The two transformations need each other. SROA needs every use of a slot in view, and a call hides the uses inside the callee. The inliner, in turn, does better when its callees have been cleaned up by SROA, and LLVM's cost analysis anticipates the pass: when an argument points into one of the caller's slots, loads and stores through it inside the callee cost nothing, because SROA is expected to remove them after inlining, and their cost is charged back if the callee does something with the pointer that would defeat SROA.[^llvm-inline-cost]

The fourth example is a small 2D walk. `@walk` keeps a position in a struct slot and moves it with `@add`, which takes both operands and its result by address, as a front end passes struct values; the result goes to a temporary `%next`, which is then copied into `%pos`, exactly the lowering that value semantics asked for above.

--8<-- "includes/examples/optimize/o7-inlining-and-sroa/inline_then_sroa.ll.md"

Run with SROA alone, the file keeps all three struct slots, the call and the copy; only the counter `%i` is promoted, since its address never leaves `@walk`. Run with inlining first, `@add` disappears into the loop, every use of `%pos`, `%step` and `%next` becomes visible, and SROA replaces all three. What is left is the loop the source meant: `x` and `y` travel around it as two `float` phis, each step adds 1.0 and 0.5 in the original order, and `@add` itself, internal and no longer called, is deleted. The three `noalias.scope.decl` calls are the alias scopes that replaced `@add`'s `noalias` parameters.

## Your turn: complex numbers in a loop

Here is a Vortex function that raises a complex number to a power by repeated multiplication:

```vortex
// items: valid
struct Complex {
    re: f32,
    im: f32,
}

fn times(p: Complex, q: Complex) -> Complex {
    return Complex { re: p.re * q.re - p.im * q.im, im: p.re * q.im + p.im * q.re };
}

fn power(z: Complex, n: i32) -> Complex {
    let mut result = Complex { re: 1.0, im: 0.0 };
    for i in 0..n {
        result = times(result, z);
    }
    return result;
}
```

Assume a front end that gives `result` a slot, passes `p` and `q` by hidden address, and returns through a temporary that is then copied into `result`. `power`'s own parameter `z` arrives the same way, as an address from its caller. After inlining `times` into `power`, running SROA, and moving the two loads of `z`'s fields out of the loop ([O6](o6-redundancy.md)'s loop-invariant code motion), the loop looks like this, with `zr` and `zi` standing for the loaded fields:

```text
entry:  re0 = 1.0
        im0 = 0.0
        goto head
head:   re1 = phi(entry: re0, body: ___)
        im1 = phi(entry: im0, body: ___)
        i1  = phi(entry: 0, body: i2)
        if i1 < n goto body else done
body:   t1  = re1 * zr
        t2  = im1 * zi
        re2 = ___
        t3  = ___
        t4  = im1 * zr
        im2 = t3 + t4
        i2  = i1 + 1
        goto head
done:   return (re1, im1)
```

Fill in the four blanks. Then answer: how many struct slots did the unoptimized `power` use, counting the temporary, and how many are left? Can any printed result differ from the unoptimized program's? And which pairs of operations in `body` must not be fused?

??? check "Answers for the loop"

    - `re1 = phi(entry: re0, body: re2)`, `im1 = phi(entry: im0, body: im2)`, `re2 = t1 - t2` and `t3 = re1 * zi`.
    - Two before: `result` and the temporary that receives each returned value. `z` is not a slot of `power`; it lives in the caller. None after. Once `times` is inlined, `p` and `q` are visibly the addresses of `result` and `z`, every use of `result` and of the temporary is a load or store at a constant offset or a copy between them, and SROA replaces both slots, copy included.
    - No. Each product, sum and difference has the same operands in the same order as before, and neither inlining nor SROA regroups floating-point operations.
    - `t1` with `re2` and `t2` with `re2`, which a fused multiply-subtract could merge, and `t3` or `t4` with `im2`. Before inlining, these multiplies and additions were already next to each other in one expression of `times`, where a C compiler using Clang's default, `-ffp-contract=on`, could fuse them.[^clang-fp] Decision 56 rules that out in Vortex, before inlining and after.

## For Vortex

!!! vortex "Exercise"

    **Decide first.** Write a short design note: will your compiler inline in its own IR, before its back end, or leave inlining to LLVM? If LLVM, list what your lowering must give it: internal linkage for every function except `main`, stack slots at the start of the entry block, fields loaded and stored one at a time rather than whole aggregates, and `noalias` on every `&mut` parameter. Then answer O1's open question for your contract: may the recursion depth at which a program runs out of stack change between optimization levels?

    **Then build:**

    1. The call graph of a checked program, with its strongly connected components, and a dump that lists the components bottom-up.
    2. An inliner for direct calls in your IR: split the calling block, copy and rename the callee, map parameters to arguments, turn returns into jumps to the continuation with a phi when there are several, move fixed-size slots to the entry block, and keep every operation's source position. Never inline a function into its own component.
    3. A cost rule with a threshold set by a command-line option, and a remark for every call site, inlined or kept, with the numbers and the reason, such as `kept: recursive` or `kept: cost 260 >= 225`.
    4. Scalar replacement for struct locals that are never borrowed and for local arrays whose every index is a constant, one variable per field or element, followed by the SSA construction you chose in O3.

    **Not yet:** decisions driven by profiles; inlining only part of a callee; cloning a function for particular constant arguments; scalar replacement of variables lent with `&` or `&mut` (that needs [O9](o9-alias-analysis.md)); arrays indexed by loop variables, which wait for full unrolling ([P7](p7-loop-transformations.md#unrolling)).

    **Proof that it works:**

    - Differential tests: every test program prints the same bytes, writes the same runtime error line and exits with the same status with the threshold at 0 and at a large value, and with scalar replacement on and off.
    - A bounds check that fails inside an inlined helper reports the helper's line and column, not the call's.
    - A pair of mutually recursive functions: the inliner finishes, both calls remain, and the remarks say why.
    - The helper version of `multiply` from this chapter: after inlining, the inner loop in your IR dump contains no call.
    - A program that recurses without end stops with the `stack` report at every optimization level ([decision 46](../decisions/diagnostics.md#d46)).
    - A table for three programs of your choice, filled from your compiler's own output, with the date and your compiler's version:

    | Program | Calls before | Calls after | Stack slots before | Stack slots after | IR instructions before | IR instructions after |
    | --- | --- | --- | --- | --- | --- | --- |
    | | | | | | | |
    | | | | | | | |
    | | | | | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **What does inlining buy besides removing a call?** Context: the callee's code sees the caller's constants, ranges and addresses, so folding, check removal, vectorization and SROA can reach it.
    - **How does LLVM 18 decide one call site?** It walks the callee with the call's arguments in place, counts 5 per surviving instruction minus what the call costs, and inlines if that is below a threshold, 225 by default and adjusted per call site.
    - **Why bottom-up?** Each callee is measured after its own inlining and simplification, and recursion stays inside strongly connected components.
    - **What does SROA need to replace a slot?** Every use visible, at a constant offset: no escaping address and no variable index.
    - **Why do inlining and SROA come together?** A slot whose address is passed to a call cannot be replaced; inlining removes the call, and SROA then turns the slot into SSA values.
    - **What must a Vortex inliner preserve beyond the values?** Each check's source position, strict floating point with no new fusion, the `&mut` exclusivity facts, a temporary for results that alias an argument, and termination on recursion.

## Where this comes back

!!! next "You will use this again in"

    - [O8. Loops: structure, induction variables and bounds checks](o8-loops.md): *bounds checks proved after inlining*, *ranges from the caller's loops*
    - [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md): *alias scopes*, *escaping addresses*
    - [O10. Pass managers and pipelines](o10-pass-pipelines.md): *call-graph walk*, *SROA after unrolling*
    - [O12. Testing an optimizer](o12-testing-optimizers.md): *differential tests with inlining on and off*
    - [P10. Vectorization](p10-vectorization.md): *calls in loop bodies*
    - [P12. Anatomy of a fast GEMM](p12-fast-gemm.md): *accumulator arrays in registers*
    - [A4. Calling conventions and ABIs](../backend/a4-calling-conventions.md): *argument setup*, *hidden result slot*
    - [A5. Stack frames](../backend/a5-stack-frames.md): *frame size*, *stack slots*
    - [D1. Debug information](../backend/d1-debug-info.md): *inlined-at locations*

## Sources and further reading

Start with Theodoridis, Grosser and Su for why inlining decisions are hard and how far a heuristic can be from the best choice. Then read LLVM's `InlineCost.cpp` with the first example beside it: the file is long, but `updateThreshold`, `onAnalysisStart` and `finalizeAnalysis` hold most of the rule. The header comment and the `AllocaSlices` class of `SROA.cpp` are the best short description of SROA. Trofin and his coauthors give a short overview of LLVM's inliner in their section 2.2, then replace its heuristic for code size with a trained policy; in 2021 they reported up to 7% smaller code than LLVM's `-Oz`.[^mlgo]

[^theodoridis]: Theodoros Theodoridis, Tobias Grosser and Zhendong Su, "Understanding and Exploiting Optimal Function Inlining", *Proceedings of the 27th ACM International Conference on Architectural Support for Programming Languages and Operating Systems (ASPLOS '22)*, 2022: the abstract and section 1. <https://doi.org/10.1145/3503222.3507744> (authors' copy: <https://ethz.ch/content/dam/ethz/special-interest/infk/ast-dam/documents/Theodoridis-ASPLOS22-Inlining-Paper.pdf>)
[^mlgo]: Mircea Trofin, Yundi Qian, Eugene Brevdo, Zinan Lin, Krzysztof Choromanski and David Li, "MLGO: a Machine Learning Guided Compiler Optimizations Framework", arXiv:2101.04808, 2021: the abstract and section 2.2. <https://arxiv.org/abs/2101.04808>
[^cpp-inline]: cppreference.com, "inline specifier", section "Explanation". <https://en.cppreference.com/w/cpp/language/inline>
[^llvm-lvl]: LLVM Project, `LoopVectorizationLegality.cpp`, release/18.x branch: the check on calls in `canVectorizeInstrs`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Vectorize/LoopVectorizationLegality.cpp>
[^llvm-inline-function]: LLVM Project, `InlineFunction.cpp`, release/18.x branch: `fixupLineNumbers` and `inlineDebugLoc`, `AddAliasScopeMetadata`, the moving of static allocas to the caller's entry block, and the splitting of the calling block with a phi for several returns. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Utils/InlineFunction.cpp>
[^langref-dilocation]: LLVM Project, "LLVM Language Reference Manual", section "DILocation". <https://llvm.org/docs/LangRef.html#dilocation>
[^langref-noalias]: LLVM Project, "LLVM Language Reference Manual", section "'noalias' and 'alias.scope' Metadata". <https://llvm.org/docs/LangRef.html#noalias-and-alias-scope-metadata>
[^llvm-inline-cost]: LLVM Project, `InlineCost.cpp`, release/18.x branch: the options `inline-threshold`, `inline-instr-cost` and `inline-call-penalty`; the comments on `SimplifiedValues` and `SROAArgValues` in `CallAnalyzer`; `visitInstruction`; `updateThreshold`; `onAnalysisStart`; `finalizeAnalysis`; `getCallsiteCost`; the recursion check in `visitCallBase`; the stack-size check for recursive callers; `isSoleCallToLocalFunction`; `getAttributeBasedInliningDecision`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Analysis/InlineCost.cpp>
[^llvm-inline-cost-h]: LLVM Project, `InlineCost.h`, release/18.x branch: namespace `InlineConstants`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/include/llvm/Analysis/InlineCost.h>
[^langref-attrs]: LLVM Project, "LLVM Language Reference Manual", sections "Parameter Attributes" (`noalias`) and "Function Attributes" (`alwaysinline`, `minsize` and `noinline`). <https://llvm.org/docs/LangRef.html#function-attributes>
[^llvm-scc-iterator]: LLVM Project, `SCCIterator.h`, release/18.x branch: the file header. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/include/llvm/ADT/SCCIterator.h>
[^llvm-inliner]: LLVM Project, `Inliner.cpp`, release/18.x branch: the file header, the comment on processing calls within a component, and the inline history. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/IPO/Inliner.cpp>
[^llvm-pipelines]: LLVM Project, `PassBuilderPipelines.cpp`, release/18.x branch: `buildInlinerPipeline`, the SROA passes in the function simplification pipelines, and the comments on SROA after loop unrolling. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Passes/PassBuilderPipelines.cpp>
[^llvm-cgscc]: LLVM Project, `CGSCCPassManager.h`, release/18.x branch: the file header. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/include/llvm/Analysis/CGSCCPassManager.h>
[^clang-fp]: Clang Project, "Clang Compiler User's Manual", option `-ffp-contract`. <https://clang.llvm.org/docs/UsersManual.html#cmdoption-ffp-contract>
[^langref-fmf]: LLVM Project, "LLVM Language Reference Manual", section "Fast-Math Flags", subsection "Rewrite-based flags", read on 2026-09-24. <https://llvm.org/docs/LangRef.html#fast-math-flags>
[^llvm-perftips]: LLVM Project, "Performance Tips for Frontend Authors", sections "Use of allocas" and "Avoid creating values of aggregate type". <https://llvm.org/docs/Frontend/PerformanceTips.html>
[^llvm-passes]: LLVM Project, "LLVM's Analysis and Transform Passes", entries `sroa` and `mem2reg`. <https://llvm.org/docs/Passes.html>
[^llvm-sroa]: LLVM Project, `SROA.cpp`, release/18.x branch: the file header, the comment on the SROA pass, the classes `Slice` and `AllocaSlices`, and the handling of unknown offsets and escaping pointers in `SliceBuilder`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Scalar/SROA.cpp>
