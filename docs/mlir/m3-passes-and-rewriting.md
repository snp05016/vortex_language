# M3. Passes and pattern rewriting

<p class="page-intro">MLIR changes a module by running passes over it, and most of its cleanups work the same way inside: small rewrite patterns, each matching one shape of IR, applied by a driver until none matches. This chapter teaches patterns, folds, the greedy driver and canonicalization by tracing them by hand, then the pass manager that decides where each pass runs, so that you can clean up the MLIR your Vortex tool emits and prove the cleanup kept Vortex's rules.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 35 minutes · Builds on: [M2. Reading MLIR](m2-reading-mlir.md), [O1. The optimizer's contract](../optimize/o1-optimizer-contract.md), [O6. Redundancy: CSE, GVN, PRE and LICM](../optimize/o6-redundancy.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is every instruction, loop and function in MLIR, underneath?"

        An operation: results, a name, operands, successors, properties, regions, attributes, a type and a location.

        Introduced in [M2. Reading MLIR](m2-reading-mlir.md).

    ??? question "What does it mean for an operation to be isolated from above?"

        Nothing inside it may use a value defined outside it. `func.func` is isolated, so a function body reaches other functions only by symbol, and separate functions can be compiled in parallel.

        Introduced in [M2. Reading MLIR](m2-reading-mlir.md).

    ??? question "What does common subexpression elimination look for?"

        A computation that repeats one already available: the same operation on the same operands, with nothing in between that could change the result. It replaces the second with the first.

        Introduced in [O6. Redundancy: CSE, GVN, PRE and LICM](../optimize/o6-redundancy.md).

    ??? question "Why is rewriting `x + 0.0` to `x` wrong for floats, while `x + (-0.0)` to `x` is right?"

        Rounding to nearest, `-0.0 + 0.0` is `+0.0`, so the first rewrite changes the result when `x` is `-0.0`. Adding `-0.0` returns every input unchanged.

        Introduced in [O1. The optimizer's contract](../optimize/o1-optimizer-contract.md#floating-point-identities-that-are-false).

    ??? question "May a Vortex compiler turn `sum += a[row, k] * b[k, column]` into one fused multiply-add?"

        No. Each `f32` operation is one IEEE 754 operation, rounded to nearest with ties to even, and a compiler must not contract, reassociate or reorder them. The product is rounded, then the sum.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Explain what a rewrite pattern and a fold hook each match and replace, and what the rules on each one protect.
    - Trace the greedy driver's worklist by hand to its fixed point, and predict the output of `--canonicalize` on a small function.
    - Decide whether a rewrite belongs in canonicalization, including floating-point rewrites under Vortex's strict rules.
    - Write a textual pass pipeline that anchors each pass on the right operation, and use MLIR's IR printing to say which pass made which change.

## Two cleanups nobody asked for

Start with a function that does two useless things: it adds zero to its parameter, and it computes a product that nothing reads.

--8<-- "includes/examples/mlir/m3-passes-and-rewriting/identities.mlir.md"

`mlir-opt --canonicalize` returned a function with no arithmetic left. Two different rules did the work. The addition disappeared because `arith.addi` knows that adding zero gives back its other operand: every use of `%kept` became a use of `%x`. The multiply disappeared because nothing used its result and it has no side effect, so keeping it could not change what the program does. The second rule, deleting unused operations, is the **dead code elimination** of [O5](../optimize/o5-constants-and-dead-code.md#unreachable-code-and-dead-code); here it needed no analysis, only a use count of zero.

Neither rule knew what `@cleanup` was for. Each looked at one operation and its operands, recognized a shape and replaced the operation. That is the whole idea of this chapter, and MLIR builds most of its transformations on it. The canonicalization document states the second rule for every level of IR, "elimination of operations that have no side effects and have no uses", and gives adding zero as its first example of a no-op worth removing.[^canon]

## Patterns: match a shape, then replace it

A **rewrite pattern** is a small piece of compiler code that recognizes one shape of IR and replaces it with another. The shape starts at one operation, the pattern's **root**, and may reach back through the root's operands to the operations that define them. Because values in SSA form point to their definitions, the shape is a small directed acyclic graph, a **DAG**, and MLIR's documentation calls its framework "a general DAG-to-DAG transformation framework".[^rewriter]

Here is a pattern that `arith` registers, written in its source as a comment above the rule: `addi(addi(x, c0), c1) -> addi(x, c0 + c1)`.[^arith-td] Its root is the outer addition. It matches only when the root's second operand is a constant and its first operand is itself an addition whose second operand is a constant. The rewrite builds one new constant and one new addition, and replaces the root with the new addition. Figure 1 draws it on `(x + 2) + 3`.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="A rewrite pattern matching a two-operation DAG rooted at an addition and replacing it" aria-describedby="m3-f1-desc">
<title id="m3-f1-title">The pattern addi(addi(x, c0), c1) matched and applied</title>
<desc id="m3-f1-desc">Two dataflow graphs. Left, before: the parameter x and the constant 2 feed %a = arith.addi; %a and the constant 3 feed %b = arith.addi, the root; %b feeds its users. The root, its first operand's defining addition and the two constants are highlighted as the matched shape. Right, after: x and a new constant 5 feed a new arith.addi, which now feeds the users that %b had. The old %a and the constants 2 and 3 are drawn dashed, marked no users left, erased as dead code.</desc>
<text class="vx-text" x="20" y="24">before: the pattern matches at the root %b</text>
<rect class="vx-box" x="40" y="44" width="80" height="34" rx="4"/>
<text class="vx-mono" x="80" y="66" text-anchor="middle">%x</text>
<rect class="vx-box-accent" x="170" y="44" width="80" height="34" rx="4"/>
<text class="vx-mono" x="210" y="66" text-anchor="middle">2</text>
<rect class="vx-box-accent" x="80" y="120" width="170" height="34" rx="4"/>
<text class="vx-mono" x="165" y="142" text-anchor="middle">%a = addi</text>
<rect class="vx-box-accent" x="280" y="120" width="80" height="34" rx="4"/>
<text class="vx-mono" x="320" y="142" text-anchor="middle">3</text>
<rect class="vx-box-accent vx-pulse" x="150" y="196" width="170" height="34" rx="4"/>
<text class="vx-mono" x="235" y="218" text-anchor="middle">%b = addi</text>
<text class="vx-text-muted" x="330" y="218">root</text>
<line class="vx-line" x1="80" y1="78" x2="140" y2="120"/>
<line class="vx-line" x1="210" y1="78" x2="190" y2="120"/>
<line class="vx-line" x1="165" y1="154" x2="210" y2="196"/>
<line class="vx-line" x1="320" y1="154" x2="270" y2="196"/>
<line class="vx-line" x1="235" y1="230" x2="235" y2="262"/>
<text class="vx-text-muted" x="245" y="258">users of %b</text>
<line class="vx-line" x1="400" y1="30" x2="400" y2="280"/>
<text class="vx-text" x="420" y="24">after: a new addition takes the root's users</text>
<rect class="vx-box" x="440" y="44" width="80" height="34" rx="4"/>
<text class="vx-mono" x="480" y="66" text-anchor="middle">%x</text>
<rect class="vx-box-accent" x="570" y="44" width="80" height="34" rx="4"/>
<text class="vx-mono" x="610" y="66" text-anchor="middle">5</text>
<rect class="vx-box-accent" x="490" y="196" width="170" height="34" rx="4"/>
<text class="vx-mono" x="575" y="218" text-anchor="middle">addi</text>
<line class="vx-line" x1="480" y1="78" x2="550" y2="196"/>
<line class="vx-line" x1="610" y1="78" x2="600" y2="196"/>
<line class="vx-line" x1="575" y1="230" x2="575" y2="262"/>
<text class="vx-text-muted" x="585" y="258">the same users</text>
<rect class="vx-box" x="620" y="110" width="126" height="54" rx="4" stroke-dasharray="4 3"/>
<text class="vx-mono" x="683" y="132" text-anchor="middle">%a, 2, 3</text>
<text class="vx-text-muted" x="683" y="152" text-anchor="middle">no users: erased</text>
</svg>
<figcaption>Figure 1. A pattern whose shape is two operations deep. Highlighted on the left is everything the pattern inspects: the root, the addition that defines its first operand, and both constants. On the right the root's users read the new addition instead. The old operations were not touched by the pattern; they die because nothing uses them any more.</figcaption>
</figure>

Three things about a pattern are fixed when it is built.[^rewriter]

- Its **root operation name**, such as `arith.addi`, so the driver offers it only operations of that kind. A pattern may instead match any operation, but must say so explicitly.
- Its **benefit**, a small number stating how good the rewrite is expected to be. When several patterns match the same operation, the driver tries the one with the highest benefit first.
- Its `matchAndRewrite` method, which both checks the shape and, if the shape is there, performs the replacement.

The method has two rules, and both exist to protect the driver. First, it must not change the IR until it has decided the match succeeds, and it must report success exactly when it changed something. A pattern that edits half the IR and then reports failure leaves the driver believing nothing happened. Second, every change goes through a **PatternRewriter**, an object the driver hands to the pattern: creating, replacing, erasing or editing an operation in place are all calls on it. The rewriter tells the driver about each change as it happens, which is how the driver knows what to look at next.[^rewriter]

??? check "Suppose `%a = arith.addi %x, %c2` has a second user, say the function also returns `%a`. Does the pattern still fire on `%b = arith.addi %a, %c3`, and does the function get smaller?"

    It fires: its shape is still there, and nothing in it asks whether `%a` has other users. `%b` becomes `arith.addi %x, %c5`, but `%a` cannot be erased, so the function still holds two additions and now one more constant (MLIR 18.1.8 printed exactly that, checked on 2026-09-24). This is why the canonicalization document advises rewriting toward fewer uses of a value: some patterns only pay off when a value has a single user.[^canon]

## Folds: the rewrites that create nothing

The addition of zero in the first example was not removed by a pattern. It was removed by a **fold**, a method an operation defines on itself that answers one question: can this operation be replaced by something that already exists? A fold may return one of the operation's existing operands, as `x + 0` returns `x`. It may return a constant value, as `2 + 3` returns 5, which the driver then turns into an `arith.constant` operation. Or it may update the operation in place. It may not create new operations, and it may not erase the operation itself.[^canon]

Those limits make a fold cheap and safe to call from anywhere. The canonicalizer calls it, and so does the conversion framework of [M4](m4-dialect-conversion.md), and a compiler building IR can call `createOrFold` so that `2 + 3` never exists in the first place. The document's advice is to write a canonicalization as a fold whenever it can be one, and as a pattern only when it cannot.[^canon] The rule `addi(addi(x, c0), c1)` cannot be a fold: its result needs a new constant and a new addition, neither of which exists yet.

MLIR 18's folds for integer addition and subtraction show the typical range.[^arith-cpp] `arith.addi` folds `x + 0` to `x`, folds `(a - b) + b` to `a`, and folds two constants into their sum. `arith.subi` folds `x - x` to 0 and `x - 0` to `x`. Each rule inspects one operation and, at most, the operations defining its operands.

Some rules hold for every operation of every dialect, so the canonicalizer applies them without being told. Operations with no side effects and no uses are erased. Constants are folded. For an operation marked commutative, constant operands are moved to the right, so `2 + x` becomes `x + 2` and every later rule has one shape to match instead of two. Finally, constants are made unique and moved to the entry block of the nearest region that is isolated from above, such as a function body, so that the constant 5 exists once however many folds produce it.[^canon]

## Writing patterns: three ways, one mechanism

A pattern can be written three ways, and all three produce the same object for the driver.

- **In C++**, as a class with a `matchAndRewrite` method. Chapter 3 of the Toy tutorial writes one that removes `transpose(transpose(x))`, and registers it as a canonicalization pattern of its `transpose` operation.[^toy3]
- **In TableGen**, as a **declarative rewrite rule** (DRR): a source shape and a result shape, from which the build generates the C++ class.[^drr] The arith rule of Figure 1 is written this way.[^arith-td]
- **In PDLL**, a language designed for writing patterns, which can be compiled ahead of time or loaded while the compiler runs.[^pdll]

An operation offers its canonicalization patterns through a method its dialect writes, `getCanonicalizationPatterns`, and its fold through a method named `fold`; flags in the operation's definition declare that each exists.[^canon] That is how canonicalization stays open: the canonicalizer does not contain a list of rewrites, it asks every loaded dialect for theirs.

This chapter writes no pattern of its own. Its examples run `mlir-opt` over `.mlir` files, which is how the book checks examples, and which is also the right first tool: before writing a pattern, you learn what the existing ones already do to your IR.

## The greedy driver, traced by hand

A set of patterns does nothing until a **driver** applies it. The driver chooses which operation to look at next, offers it the patterns rooted at its name, and decides when to stop. MLIR has several drivers; the one the canonicalizer uses is the **greedy pattern rewrite driver**.[^rewriter]

It keeps a **worklist**, a list of operations still to be examined, seeded with every operation in the region it is given. It takes one operation from the list, tries the operation's fold and its patterns, highest benefit first, and applies the first that succeeds. It is greedy because it takes the first rewrite that applies at the operation in front of it, and never goes back to try another. When a rewrite changes or creates operations, the driver puts the affected operations back on the list, because a rewrite often creates the shape another rule is waiting for.

The driver stops at a **fixed point**, where no operation on the list matches anything, or when a configured maximum number of iterations is used up.[^rewriter]

The next function needs every kind of rewrite met so far, each one enabled by the one before:

--8<-- "includes/examples/mlir/m3-passes-and-rewriting/chain.mlir.md"

Seven operations became three. Figure 2 traces one order in which the driver can reach that answer. It visits operations in program order, which the canonicalizer's `top-down` option asks for; the real driver's order can differ in detail, but here it cannot change the result: with `top-down=false` MLIR 18.1.8 printed the same function (checked on 2026-09-24).

<figure class="vx-figure">
<div class="vx-stepper">
<div class="vx-step">
<p><strong>Step 1. Seed.</strong> Every operation in the function goes on the worklist in program order. Nothing has changed yet.</p>
<svg viewBox="0 0 760 230" role="img" aria-label="Greedy driver step 1: the seven operations of @chain are listed, and the worklist holds all of them in order.">
<text class="vx-text" x="20" y="22">IR</text>
<text class="vx-mono" x="20" y="50">%c2 = arith.constant 2</text>
<text class="vx-mono" x="20" y="74">%a  = arith.addi %c2, %x</text>
<text class="vx-mono" x="20" y="98">%c3 = arith.constant 3</text>
<text class="vx-mono" x="20" y="122">%b  = arith.addi %a, %c3</text>
<text class="vx-mono" x="20" y="146">%z  = arith.subi %b, %b</text>
<text class="vx-mono" x="20" y="170">%r  = arith.addi %b, %z</text>
<text class="vx-mono" x="20" y="194">return %r</text>
<line class="vx-line" x1="470" y1="10" x2="470" y2="220"/>
<text class="vx-text" x="490" y="22">worklist (next first)</text>
<text class="vx-mono" x="490" y="50">%c2 %a %c3 %b %z %r return</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 2. Visit <code>%a</code>.</strong> <code>arith.addi</code> is commutative and its constant is on the left, so the global rule for commutative operations moves it right, in place. <code>%b</code>, which uses <code>%a</code>, is already waiting on the list.</p>
<svg viewBox="0 0 760 230" role="img" aria-label="Greedy driver step 2: %a is rewritten in place to arith.addi %x, %c2. The worklist now holds %c3, %b, %z, %r and return.">
<text class="vx-text" x="20" y="22">IR</text>
<text class="vx-mono" x="20" y="50">%c2 = arith.constant 2</text>
<rect class="vx-box-accent" x="14" y="58" width="430" height="24" rx="3"/>
<text class="vx-mono" x="20" y="74">%a  = arith.addi %x, %c2</text>
<text class="vx-text-muted" x="290" y="74">constant moved right</text>
<text class="vx-mono" x="20" y="98">%c3 = arith.constant 3</text>
<text class="vx-mono" x="20" y="122">%b  = arith.addi %a, %c3</text>
<text class="vx-mono" x="20" y="146">%z  = arith.subi %b, %b</text>
<text class="vx-mono" x="20" y="170">%r  = arith.addi %b, %z</text>
<text class="vx-mono" x="20" y="194">return %r</text>
<line class="vx-line" x1="470" y1="10" x2="470" y2="220"/>
<text class="vx-text" x="490" y="22">worklist (next first)</text>
<text class="vx-mono" x="490" y="50">%c3 %b %z %r return</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 3. Visit <code>%b</code>.</strong> No fold applies, but the pattern of Figure 1 now matches, because step 2 put the constant where the pattern looks for it. It creates the constant 5 and a new addition, <code>%b'</code>, and replaces <code>%b</code>. The users of the replacement go back on the list; <code>%a</code>, <code>%c2</code> and <code>%c3</code> have lost their last users and are erased.</p>
<svg viewBox="0 0 760 230" role="img" aria-label="Greedy driver step 3: the pattern replaces %b with a new addition of %x and the constant 5. %a and the constants 2 and 3 are erased. The worklist holds %z, %r and return.">
<text class="vx-text" x="20" y="22">IR</text>
<rect class="vx-box-accent" x="14" y="34" width="430" height="48" rx="3"/>
<text class="vx-mono" x="20" y="50">%c5 = arith.constant 5</text>
<text class="vx-mono" x="20" y="74">%b' = arith.addi %x, %c5</text>
<text class="vx-text-muted" x="290" y="62">new, replaces %b</text>
<text class="vx-text-muted" x="20" y="98">%a, %c2, %c3: no users, erased</text>
<text class="vx-mono" x="20" y="146">%z  = arith.subi %b', %b'</text>
<text class="vx-mono" x="20" y="170">%r  = arith.addi %b', %z</text>
<text class="vx-mono" x="20" y="194">return %r</text>
<line class="vx-line" x1="470" y1="10" x2="470" y2="220"/>
<text class="vx-text" x="490" y="22">worklist (next first)</text>
<text class="vx-mono" x="490" y="50">%z %r return</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 4. Visit <code>%z</code>.</strong> <code>arith.subi</code>'s fold sees the same value on both sides and returns the constant 0. The driver materializes it as an <code>arith.constant</code> and replaces <code>%z</code> with it; <code>%r</code>, its user, is still on the list.</p>
<svg viewBox="0 0 760 230" role="img" aria-label="Greedy driver step 4: %z folds to the constant 0. The worklist holds %r and return.">
<text class="vx-text" x="20" y="22">IR</text>
<text class="vx-mono" x="20" y="50">%c5 = arith.constant 5</text>
<text class="vx-mono" x="20" y="74">%b' = arith.addi %x, %c5</text>
<rect class="vx-box-accent" x="14" y="130" width="430" height="24" rx="3"/>
<text class="vx-mono" x="20" y="146">%c0 = arith.constant 0</text>
<text class="vx-text-muted" x="290" y="146">fold of %b' - %b'</text>
<text class="vx-mono" x="20" y="170">%r  = arith.addi %b', %c0</text>
<text class="vx-mono" x="20" y="194">return %r</text>
<line class="vx-line" x1="470" y1="10" x2="470" y2="220"/>
<text class="vx-text" x="490" y="22">worklist (next first)</text>
<text class="vx-mono" x="490" y="50">%r return</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 5. Visit <code>%r</code>, then <code>return</code>.</strong> Adding zero folds to <code>%b'</code>, so <code>return</code> now uses <code>%b'</code> and the constant 0 dies. Nothing on the list matches any more: a fixed point. The constant 5 sits at the top of the function, where uniqued constants go.</p>
<svg viewBox="0 0 760 230" role="img" aria-label="Greedy driver step 5: %r folds to %b', the constant 0 is erased, and the worklist is empty. The function is a constant 5, one addition and a return.">
<text class="vx-text" x="20" y="22">IR</text>
<text class="vx-mono" x="20" y="50">%c5 = arith.constant 5</text>
<text class="vx-mono" x="20" y="74">%b' = arith.addi %x, %c5</text>
<rect class="vx-box-accent" x="14" y="82" width="430" height="24" rx="3"/>
<text class="vx-mono" x="20" y="98">return %b'</text>
<text class="vx-text-muted" x="290" y="98">fold of %b' + 0</text>
<line class="vx-line" x1="470" y1="10" x2="470" y2="220"/>
<text class="vx-text" x="490" y="22">worklist (next first)</text>
<text class="vx-text-muted" x="490" y="50">empty: fixed point</text>
</svg>
</div>
</div>
<figcaption>Figure 2. The greedy driver on <code>@chain</code>, one visit at a time. Each rewrite makes the shape the next one needs: moving the constant lets the pattern match, the pattern's result makes <code>%z</code> foldable, and folding <code>%z</code> makes <code>%r</code> foldable. Operations are shown in order of their position in the function; the driver's bookkeeping of exactly when it erases dead operations is simplified.</figcaption>
</figure>

A pass that visited each operation once, in order, would have stopped after step 3 with `%z` and `%r` rewritten against a `%b` that no longer existed, or would have missed them. The worklist is what makes the order forgiving: whatever a rewrite changes gets looked at again.

A fixed point is a promise about the end state, not about uniqueness. It says no rule applies any more; it does not say every order of applying the rules reaches the same IR. That second property, called **confluence**, depends on the rules themselves. The canonicalization document asks for it in practical terms: repeated application should converge, and "unstable or cyclic rewrites are considered a bug".[^canon] The iteration limit exists for when that fails, so that a pair of patterns undoing each other stops rather than running forever.[^canon] The `test-convergence` option of `--canonicalize` makes the pass fail instead of stopping quietly, which turns a cycle into a test failure.[^passes]

Now a half-finished one. Predict, before reading the answer, what `--canonicalize` prints for this function:

```mlir
func.func @turn(%x: i32, %y: i32) -> (i32, i32) {
  %c7 = arith.constant 7 : i32
  %a = arith.subi %x, %c7 : i32
  %b = arith.addi %a, %c7 : i32
  %d = arith.subi %b, %x : i32
  %e = arith.muli %y, %y : i32
  %f = arith.muli %y, %y : i32
  %g = arith.addi %e, %f : i32
  return %d, %g : i32, i32
}
```

??? check "Which operations survive `--canonicalize`, and what does adding `--cse` after it change?"

    `%b` is `(x - 7) + 7`, which `arith.addi`'s fold `(a - b) + b` turns into `%x`. Then `%d` is `x - x`, which folds to the constant 0, and `%a` and `%c7` die. The first result is the constant 0. The two multiplies survive: they are identical, but no fold or pattern looks for an identical operation elsewhere in the function. `--canonicalize --cse` merges them, leaving one `arith.muli` and `arith.addi %0, %0`. MLIR 18.1.8 printed both results so (checked on 2026-09-24).

## What belongs in canonicalization

**Canonicalization** is MLIR's one shared cleanup: a single pass that applies the canonicalization patterns and folds of every loaded dialect with the greedy driver.[^canon] Its name comes from its purpose. A **canonical form** is one agreed spelling for things that could be written several ways, like `x + 2` rather than `2 + x`. If every pass can assume constants sit on the right, no pass needs a second version of each rule for the left.

The document is explicit that the goal is to make later analyses and optimizations more effective, and that performance improvements "are not necessary for canonicalization".[^canon] Its list of community-agreed rewrites is short: removing identities and no-ops, folding scalar constants, folding an operation with its inverse, removing unused or redundant values, simplifying control flow that cannot branch, and a few rules about shapes and casts.[^canon]

Four rules decide what stays out.[^canon]

- **No expensive patterns.** The canonicalizer runs again and again between other passes, so a pattern whose matching costs more than a local look, or that needs a complicated cost model, does not belong. Tiling a loop, or choosing between instruction sequences by their latency on some chip, is a pass of its own.
- **No lost meaning.** A canonicalization must not throw away information: the original meaning must stay recoverable from the result.
- **No correctness depending on it.** A pipeline must still work if every run of the canonicalizer is removed. It is cleanup, not a required step.
- **Convergence.** As above: rules that undo each other are bugs.

The document also admits that the canonical form has no formal definition and changes as the community adds and removes rules.[^canon] For a compiler built on a pinned MLIR, that has a practical consequence: canonicalizer output belongs in golden tests only alongside the MLIR version that produced it.

### Floating point: the identities that are false

Integer identities hold for every input; many floating-point ones do not, as [O1](../optimize/o1-optimizer-contract.md#floating-point-identities-that-are-false) showed. A canonicalizer that treated `f32` like `i32` would break [decision 56](../decisions/numbers.md#d56) without adding a single fast-math flag. This file tests five candidate identities:

--8<-- "includes/examples/mlir/m3-passes-and-rewriting/float_identities.mlir.md"

Two rewrites happened and three did not. `x + (-0.0)` became `x` and `x * 1.0` became `x`: both return every input unchanged, including `-0.0`, infinities and NaN. `x + 0.0` stayed, because it changes `-0.0` into `+0.0`. `x * 0.0` stayed, because it is NaN for an infinite `x` and `-0.0` for a negative one. `x - x` stayed, because it is NaN for an infinite `x`. The fold hooks in MLIR 18 match this exactly: `arith.addf` folds away an added negative zero, `arith.mulf` a multiplication by one, and otherwise both fold only when every operand is a constant.[^arith-cpp]

The folds that fired are exact, so they are allowed under decision 56, which forbids changes to results rather than changes to the IR. That is the test to apply to any rewrite of floating-point code, whoever wrote it: does it produce the same rounded result for every input?

## Composing passes: canonicalize, then CSE

Canonicalization is not the only cleanup. **CSE**, common subexpression elimination, is a separate pass. MLIR's version finds an operation that repeats an earlier one exactly: the same name, operands and attributes, with source locations ignored.[^cse] It relies on each operation's declared memory effects to know which it may remove.[^passes] An operation that writes memory is never merged, and one that only reads is merged only with an identical read earlier in the same block, with no write between them.[^cse] It compares shapes; it does no arithmetic, so the key it uses is close to the one [O6](../optimize/o6-redundancy.md#what-goes-into-the-key) builds for value numbering.

That makes the two passes complements. This function computes `x + 5` twice, spelled two ways:

--8<-- "includes/examples/mlir/m3-passes-and-rewriting/expose_duplicates.mlir.md"

Canonicalize folds `2 + 3` into 5, and the uniquing of constants makes that the same `arith.constant 5` that `%b` already uses. Now `%a` and `%b` are identical operations, and CSE, running second, replaces `%b` with `%a`. Run alone, `--cse` finds nothing, because the two additions have different operands. Run alone, `--canonicalize` leaves both additions, because no fold or pattern searches the function for a twin; that search is not local to one operation.

??? check "Would `--cse --canonicalize`, the same two passes in the other order, give the same result?"

    No. CSE runs first and sees two different additions, so it merges nothing. Canonicalize then folds the constants and leaves two identical `arith.addi %arg0, %c5_i32` operations, and no pass runs after it to merge them. MLIR 18.1.8 prints exactly that (checked on 2026-09-24). Order matters because each pass can create work for the other; a pipeline that wants both results runs canonicalize, then CSE, and often canonicalize again.

## Passes and where they run

The examples so far passed `--canonicalize` and `--cse` as flags. Each is a **pass**: one named transformation or analysis that the **pass manager** runs over the IR in a set order. A pass runs on one operation at a time, the **current operation**, and may change only what is nested inside it. It must not read the operations beside it, its **siblings**, because another thread may be changing them; it may read its parents but not change them.[^passmgmt] These rules are what let MLIR run a pass on many functions at once.

Some passes are written for one kind of operation, or for operations that implement a given interface, such as all function-like operations. Others, including canonicalize and CSE, are **op-agnostic**: they work on whatever operation they are given.[^passmgmt] With bare flags, `mlir-opt` runs them on the top-level `builtin.module`, and the canonicalizer then walks every function inside it; MLIR 18.1.8's IR printing, described below, shows the whole module after each pass (checked on 2026-09-24).

To say where a pass runs, you nest pass managers. An **OpPassManager** holds a list of passes and is **anchored** on an operation type: it runs its passes, in order, on each operation of that type nested directly inside the operation its parent runs on.[^passmgmt] The nesting mirrors the IR, as Figure 3 shows. The documentation lists two requirements for an anchor: it must be a registered operation, and it must be isolated from above, since a pass on a function that could see values outside the function could reach into its neighbours' use lists.[^passmgmt]

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="The IR's nesting beside the pass manager's nesting, with one function pipeline per function running side by side" aria-describedby="m3-f3-desc">
<title id="m3-f3-title">Pass managers nest the way the IR nests</title>
<desc id="m3-f3-desc">Left, the IR: a builtin.module holding three func.func operations, @f, @g and @h. Right, the pass manager built from builtin.module(func.func(canonicalize, cse)): an outer pass manager anchored on builtin.module holding one nested pass manager anchored on func.func, which holds canonicalize then cse. Below, three lanes, one per function, each running canonicalize then cse, drawn side by side with animated arrows, because each function is isolated from above and no lane can see another's values.</desc>
<text class="vx-text" x="20" y="22">the IR</text>
<rect class="vx-box-strong" x="20" y="34" width="300" height="130" rx="6"/>
<text class="vx-mono" x="34" y="56">builtin.module</text>
<rect class="vx-box" x="36" y="70" width="84" height="70" rx="4"/>
<text class="vx-mono" x="78" y="110" text-anchor="middle">func @f</text>
<rect class="vx-box" x="128" y="70" width="84" height="70" rx="4"/>
<text class="vx-mono" x="170" y="110" text-anchor="middle">func @g</text>
<rect class="vx-box" x="220" y="70" width="84" height="70" rx="4"/>
<text class="vx-mono" x="262" y="110" text-anchor="middle">func @h</text>
<text class="vx-text" x="380" y="22">the pass manager</text>
<rect class="vx-box-strong" x="380" y="34" width="360" height="130" rx="6"/>
<text class="vx-mono" x="394" y="56">OpPassManager: builtin.module</text>
<rect class="vx-box-accent" x="400" y="70" width="320" height="70" rx="4"/>
<text class="vx-mono" x="414" y="92">OpPassManager: func.func</text>
<text class="vx-mono" x="414" y="120">1. canonicalize   2. cse</text>
<text class="vx-text" x="20" y="196">running it: one lane per function, side by side</text>
<text class="vx-mono" x="20" y="226">@f</text>
<text class="vx-mono" x="20" y="256">@g</text>
<text class="vx-mono" x="20" y="286">@h</text>
<rect class="vx-box" x="70" y="210" width="150" height="24" rx="3"/>
<text class="vx-mono" x="145" y="227" text-anchor="middle">canonicalize</text>
<rect class="vx-box" x="280" y="210" width="90" height="24" rx="3"/>
<text class="vx-mono" x="325" y="227" text-anchor="middle">cse</text>
<path class="vx-flow" d="M220,222 L276,222"/>
<rect class="vx-box" x="70" y="240" width="150" height="24" rx="3"/>
<text class="vx-mono" x="145" y="257" text-anchor="middle">canonicalize</text>
<rect class="vx-box" x="280" y="240" width="90" height="24" rx="3"/>
<text class="vx-mono" x="325" y="257" text-anchor="middle">cse</text>
<path class="vx-flow" d="M220,252 L276,252"/>
<rect class="vx-box" x="70" y="270" width="150" height="24" rx="3"/>
<text class="vx-mono" x="145" y="287" text-anchor="middle">canonicalize</text>
<rect class="vx-box" x="280" y="270" width="90" height="24" rx="3"/>
<text class="vx-mono" x="325" y="287" text-anchor="middle">cse</text>
<path class="vx-flow" d="M220,282 L276,282"/>
<text class="vx-text-muted" x="400" y="232">each lane runs both passes on one function</text>
<text class="vx-text-muted" x="400" y="256">before moving on; lanes may run at once,</text>
<text class="vx-text-muted" x="400" y="280">because functions are isolated from above</text>
</svg>
<figcaption>Figure 3. The pipeline <code>builtin.module(func.func(canonicalize, cse))</code> beside the IR it runs on. The nested pass manager matches the functions nested in the module. It runs its whole list on one function before the next, and different functions may be handled by different threads.</figcaption>
</figure>

Running the whole list on one function before moving to the next is deliberate. The documentation gives two reasons: the compiler touches one function's data at a time, which suits the cache, and each function becomes one job for a thread, instead of one job per pass per function.[^passmgmt]

### Pipelines as text

`mlir-opt` builds pass managers from a string given to `--pass-pipeline`. The grammar is small: an anchor, then in parentheses a comma-separated list, where each element is either a pass name, optionally with options in braces, or another anchor with its own list.[^passmgmt] The anchor `any` makes a pass manager that runs on any operation able to anchor one.

```text
builtin.module(func.func(canonicalize, cse))
builtin.module(func.func(canonicalize{top-down=false}, cse), canonicalize)
```

The first runs both passes on each function. The second adds an option to the first canonicalize and then, after all functions are done, runs canonicalize once more on the module as a whole. The options each pass accepts are listed on the Passes page;[^passes] the list there tracks the current development version, so check it against `mlir-opt --help` for your version: MLIR 18.1.8 lists `top-down`, `region-simplify`, `max-iterations`, `max-num-rewrites`, `test-convergence`, `disable-patterns` and `enable-patterns` for canonicalize (checked on 2026-09-24).

The grammar has one trap. Parentheses after a name make that name an anchor, whatever the name is:

--8<-- "includes/examples/mlir/m3-passes-and-rewriting/wrong_anchor.mlir.md"

`builtin.module(canonicalize(cse))` does not mean "canonicalize, then CSE". It builds a pass manager anchored on operations named `canonicalize`, holding CSE. There are none, so CSE runs zero times and the duplicate multiply survives. The documentation requires an anchor to be a registered operation, but MLIR 18.1.8 accepted this string without complaint, and accepted `frobnicate(cse)` the same way (checked on 2026-09-24). Figure 4 contrasts the two pipelines. The opposite mistake is caught: `--pass-pipeline='func.func(cse)'` on a module fails with "can't run 'func.func' pass manager on 'builtin.module' op", because the outermost anchor must be the operation the tool starts from.

<figure class="vx-figure">
<svg viewBox="0 0 720 300" role="img" aria-label="Two pass pipelines over the same module: one anchored on func.func, one anchored on an operation name that does not exist" aria-describedby="m3-f4-desc">
<title id="m3-f4-title">A correct anchor against a misread one</title>
<desc id="m3-f4-desc">Two side-by-side trees. Left, builtin.module(func.func(canonicalize, cse)): the module holds two functions, and each has a box holding canonicalize and cse; both run. Right, builtin.module(canonicalize(cse)): the module holds the same two functions, marked unchanged, and below them a box marked in the error colour for an operation named canonicalize, where cse would run; no such operation exists, so cse runs nowhere.</desc>
<text class="vx-text-accent" x="20" y="26">builtin.module(func.func(canonicalize, cse))</text>
<rect class="vx-box-strong" x="20" y="42" width="320" height="220" rx="6"/>
<text class="vx-text-muted" x="34" y="64">builtin.module</text>
<rect class="vx-box" x="40" y="80" width="130" height="100" rx="4"/>
<text class="vx-mono" x="105" y="100" text-anchor="middle">func.func @f</text>
<rect class="vx-box-accent" x="52" y="112" width="106" height="46" rx="4"/>
<text class="vx-mono" x="105" y="132" text-anchor="middle">canonicalize</text>
<text class="vx-mono" x="105" y="150" text-anchor="middle">cse</text>
<rect class="vx-box" x="190" y="80" width="130" height="100" rx="4"/>
<text class="vx-mono" x="255" y="100" text-anchor="middle">func.func @g</text>
<rect class="vx-box-accent" x="202" y="112" width="106" height="46" rx="4"/>
<text class="vx-mono" x="255" y="132" text-anchor="middle">canonicalize</text>
<text class="vx-mono" x="255" y="150" text-anchor="middle">cse</text>
<text class="vx-text" x="34" y="240">both passes run on each function</text>
<text class="vx-text-accent" x="380" y="26">builtin.module(canonicalize(cse))</text>
<rect class="vx-box-strong" x="380" y="42" width="320" height="220" rx="6"/>
<text class="vx-text-muted" x="394" y="64">builtin.module</text>
<rect class="vx-box" x="400" y="80" width="130" height="60" rx="4"/>
<text class="vx-mono" x="465" y="100" text-anchor="middle">func.func @f</text>
<text class="vx-text-muted" x="465" y="124" text-anchor="middle">unchanged</text>
<rect class="vx-box" x="550" y="80" width="130" height="60" rx="4"/>
<text class="vx-mono" x="615" y="100" text-anchor="middle">func.func @g</text>
<text class="vx-text-muted" x="615" y="124" text-anchor="middle">unchanged</text>
<rect class="vx-box-bad" x="400" y="156" width="280" height="56" rx="4"/>
<text class="vx-mono" x="540" y="178" text-anchor="middle">any op named "canonicalize"</text>
<text class="vx-text-muted" x="540" y="198" text-anchor="middle">cse would run here: there is none</text>
<text class="vx-text" x="394" y="240">cse runs zero times</text>
</svg>
<figcaption>Figure 4. In a pipeline string, a name followed by parentheses is always an anchor. Nesting CSE inside <code>canonicalize(...)</code> builds a pass manager for an operation that does not exist, so nothing runs and nothing reports it.</figcaption>
</figure>

??? check "Write a pipeline that runs CSE on every function and then canonicalize once on the whole module."

    `builtin.module(func.func(cse), canonicalize)`. The `func.func(...)` element is a nested pass manager, and `canonicalize`, written after it without parentheses, is a pass added to the module's own pass manager, so it runs after every function's CSE is done. MLIR 18.1.8 accepts it (checked on 2026-09-24).

## Watching a pipeline work

The final module does not say which pass made which change. MLIR's pass manager can print the IR around each pass, through what it calls an instrumentation.[^passmgmt] In `mlir-opt`, `--mlir-print-ir-before-all` and `--mlir-print-ir-after-all` print the IR before or after every pass, and `--mlir-print-ir-after-change`, added to the second, skips passes that changed nothing. The dumps go to standard error, so they do not disturb the output file.

Run over the duplicate-addition example with the pipeline `builtin.module(func.func(cse, canonicalize, cse))`, MLIR 18.1.8 printed two dumps, headed `IR Dump After Canonicalizer (canonicalize)` and `IR Dump After CSE (cse)` (checked on 2026-09-24). The first CSE is missing because it changed nothing, which is itself the answer to "was the first CSE useful here?". The two dumps are the direct record of which pass did which half of the work. Inside a function pipeline, each dump shows only the function; `--mlir-print-ir-module-scope` prints the whole module instead, which in MLIR 18.1.8 must be combined with `--mlir-disable-threading`.

Three more tools answer other questions. `--mlir-pass-statistics` prints the counters passes keep; CSE, for instance, counts the operations it removed.[^passes] `--mlir-timing` reports the time spent in each pass, useful when a pipeline gets slow and never suitable for a golden test.[^passmgmt] And the greedy driver can log every operation it visits and every pattern it tries, with `-debug-only=greedy-rewriter`;[^rewriter] the release build of `mlir-opt` 18.1.8 on the owner's machine does not accept that option, so it needs an MLIR built with debug output enabled.

A golden test, like this book's example checker, compares one final output to a saved file. Keep it to that. Use the IR dumps while developing, to learn which pass does what, and to explain a golden file that changed after an MLIR upgrade.

## For Vortex

!!! vortex "Exercise"

    **Build** a cleanup step for the MLIR that your [M2 tool](m2-reading-mlir.md#for-vortex) writes for the [stage 10 kernel](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for): one pass-pipeline string, kept in one place in your test scripts, run with `mlir-opt --pass-pipeline=...`, plus tests that prove the cleanup kept Vortex's rules.

    1. **The pipeline.** Anchor the cleanup on `func.func`, and decide from this chapter whether it needs canonicalize once, or canonicalize, CSE and canonicalize again. Justify the choice with the IR dumps of `--mlir-print-ir-after-all --mlir-print-ir-after-change`: a pass whose dump never appears on your kernel is a pass you have to argue for.
    2. **A prediction first.** Before running it, list each `index` computation and each `f32` operation in the emitted file, and write down which you expect to be folded, rewritten, merged or left alone, and by which pass. Then run it and compare line by line with the dumps. Explain every mismatch; do not delete it.
    3. **Decision 56 survives.** A test that fails unless the cleaned file has the same number of `arith.mulf` and `arith.addf` operations as the emitted one, in the same order, each with `fastmath` equal to `none`. [Decision 56](../decisions/numbers.md#d56) allows the exact identities of the float example and nothing else; say in one sentence why your kernel contains none of them, or which ones it contains.
    4. **Stores survive.** A test that fails unless every `memref.store` of the emitted file is still present, in the same order. Write down why CSE may not merge two stores even when they look identical.
    5. **Not relying on it.** The canonicalization document says a pipeline must work with the canonicalizer removed. Show that yours does: the uncleaned file passes `mlir-opt` with no options, as the cleaned one does.

    **Not yet:** no pattern, pass or dialect of your own, and no C++ that links MLIR's libraries. Lowering to the `llvm` dialect is [M4](m4-dialect-conversion.md).

    **Proof that it works:**

    - Golden files for the emitted and the cleaned kernel, each recorded with the `mlir-opt --version` that produced it.
    - The pipeline also passes with `canonicalize{test-convergence=true}` in place of `canonicalize`.
    - A canary for step 3: hand-edit one copy of the emitted file so that one `arith.mulf` carries `fastmath<contract>`, and confirm your test fails on the cleaned result.
    - A canary for the float rules: add a line computing `%s + 0.0` in `f32` to a copy of the file, and confirm the addition is still there after cleanup; change it to `%s + -0.0`, and confirm it is gone. Your prediction list from step 2 should have said both.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does a rewrite pattern do?** It matches a small DAG of operations starting at a root and replaces the root through the PatternRewriter, changing nothing unless the match succeeds.
    - **How does a fold differ from a pattern?** A fold may only return an existing value or a constant, or update its operation in place; it never creates operations, so it can run anywhere.
    - **Why does the greedy driver use a worklist?** Each rewrite can create the shape another rule matches, so changed operations go back on the list until nothing matches: a fixed point.
    - **What keeps a rewrite out of canonicalization?** Being expensive or cost-model driven, losing information, being needed for correctness, or undoing another rule.
    - **Which float identities may a canonicalizer apply under decision 56?** Only exact ones, such as `x + (-0.0)` and `x * 1.0`; `x + 0.0`, `x * 0.0` and `x - x` change some results.
    - **Why does `canonicalize` then `cse` find more than either alone?** Canonicalize makes equal computations identical; CSE merges identical ones but cannot see equality it would have to compute.
    - **What does `builtin.module(canonicalize(cse))` do?** Nothing: parentheses make `canonicalize` an anchor, no operation has that name, and MLIR 18.1.8 does not report it.

## Where this comes back

!!! next "You will use this again in"

    - [M4. Dialect conversion and lowering to LLVM](m4-dialect-conversion.md): *patterns*, *fold*, *pass pipeline*
    - [M5. Structured ops: linalg, tensor and memref](m5-structured-ops.md): *canonicalization*, *pattern*
    - [M9. Schedules as IR: the transform dialect](m9-transform-dialect.md): *greedy driver*, *pattern*
    - [O10. Pass managers and pipelines](../optimize/o10-pass-pipelines.md): *pass ordering*, *nesting*, *anchor*
    - [O12. Testing an optimizer](../optimize/o12-testing-optimizers.md): *golden output*, *canary*
    - [C7. Peephole optimization](../backend/c7-peephole.md): *local rewrite*, *fixed point*

## Sources and further reading

Read the Pattern Rewriting document's sections on defining patterns and on the greedy driver, then the Canonicalization document whole: it is short, and it is as much a policy as a description.[^rewriter][^canon] The Pass Infrastructure document's sections on the operation pass, the pass manager and the textual pipeline cover the second half of this chapter.[^passmgmt] Chapter 3 of the Toy tutorial is the place to see a pattern written in C++ and in DRR before writing one.[^toy3] The MLIR documentation tracks the current development version; where this chapter says what MLIR 18.1.8 did, it was checked with that version.

[^rewriter]: MLIR Project, "Pattern Rewriting: Generic DAG-to-DAG Rewriting", sections "Defining Patterns" (with "Benefit", "Root Operation Name" and "matchAndRewrite implementation"), "Pattern Rewriter", "Pattern Application" and "Greedy Pattern Rewrite Driver" with its "Debugging". <https://mlir.llvm.org/docs/PatternRewriter/>
[^canon]: MLIR Project, "Operation Canonicalization", sections "General Design", "What is the Canonical Form?", "Globally Applied Rules" and its section on defining canonicalization patterns. <https://mlir.llvm.org/docs/Canonicalization/>
[^passmgmt]: MLIR Project, "Pass Infrastructure", sections "Operation Pass", "Op-Agnostic Operation Passes", "OpPassManager", "Textual Pass Pipeline Specification" and "Standard Instrumentations" ("Pass Timing", "IR Printing"). <https://mlir.llvm.org/docs/PassManagement/>
[^passes]: MLIR Project, "Passes", entries `-canonicalize` (with its options) and `-cse` (with its statistics). <https://mlir.llvm.org/docs/Passes/>
[^cse]: LLVM Project, release/18.x branch, `mlir/lib/Transforms/CSE.cpp`: the hashing and equivalence of operations (`OperationEquivalence`, ignoring locations) and the handling of operations with memory effects. <https://github.com/llvm/llvm-project/blob/release/18.x/mlir/lib/Transforms/CSE.cpp>
[^arith-td]: LLVM Project, release/18.x branch, `mlir/lib/Dialect/Arith/IR/ArithCanonicalization.td`, the rule `AddIAddConstant`. <https://github.com/llvm/llvm-project/blob/release/18.x/mlir/lib/Dialect/Arith/IR/ArithCanonicalization.td>
[^arith-cpp]: LLVM Project, release/18.x branch, `mlir/lib/Dialect/Arith/IR/ArithOps.cpp`, the methods `AddIOp::fold`, `SubIOp::fold`, `AddFOp::fold`, `MulFOp::fold` and `AddIOp::getCanonicalizationPatterns`. <https://github.com/llvm/llvm-project/blob/release/18.x/mlir/lib/Dialect/Arith/IR/ArithOps.cpp>
[^toy3]: MLIR Project, "Chapter 3: High-level Language-Specific Analysis and Transformation", Toy tutorial, sections "Optimize Transpose using C++ style pattern-match and rewrite" and "Optimize Reshapes using DRR". <https://mlir.llvm.org/docs/Tutorials/Toy/Ch-3/>
[^drr]: MLIR Project, "Table-driven Declarative Rewrite Rule (DRR)", introduction. <https://mlir.llvm.org/docs/DeclarativeRewrites/>
[^pdll]: MLIR Project, "PDLL - PDL Language", section "Introduction". <https://mlir.llvm.org/docs/PDLL/>
