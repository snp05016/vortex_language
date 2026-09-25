# M4. Dialect conversion and lowering to LLVM

<p class="page-intro">Lowering MLIR to LLVM IR is a chain of dialect conversions: each pass rewrites the operations of one dialect into the llvm dialect, and the framework keeps the module well typed in between with temporary casts. This chapter takes two small functions through that chain by hand, so that you can predict what each pass does, read the error when a pass is missing, and see what Vortex's fixed-shape arrays cost at a function boundary.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 40 minutes · Builds on: [M2. Reading MLIR](m2-reading-mlir.md), [M3. Passes and pattern rewriting](m3-passes-and-rewriting.md)</p>

???+ remember "Before you start, remember"

    ??? question "What replaces a phi in MLIR?"

        A block argument. Each branch into a block passes one value per argument, and the block receives whichever value the branch that was taken passed.

        Introduced in [M2. Reading MLIR](m2-reading-mlir.md#blocks-that-take-arguments).

    ??? question "What does a rewrite pattern do?"

        It matches a small DAG of operations rooted at one operation and, if the match succeeds, replaces the root through the rewriter. A driver decides which operation to offer to which pattern, and when to stop.

        Introduced in [M3. Passes and pattern rewriting](m3-passes-and-rewriting.md#patterns-match-a-shape-then-replace-it).

    ??? question "How does a fold differ from a pattern?"

        A fold may only return an existing value or a constant, or update its own operation in place. It never creates new operations, so any driver can call it safely.

        Introduced in [M3. Passes and pattern rewriting](m3-passes-and-rewriting.md#folds-the-rewrites-that-create-nothing).

    ??? question "What is a `memref<4xf32>`, and what layout does it have when none is written?"

        A reference to a region of memory with a static shape of four `f32` elements. With no layout written it gets the default one, row-major, the order Vortex fixes for its arrays.

        Introduced in [M2. Reading MLIR](m2-reading-mlir.md#types-say-what-a-value-is).

    ??? question "Can a Vortex array's shape depend on a value known only at run time?"

        No. Every extent is a constant expression, so a function that takes a `[f32; 8, 16]` always takes exactly that shape.

        Introduced in [Arrays and shapes, decision 11](../decisions/arrays.md#d11).

!!! goals "In this chapter"

    - Explain what a conversion target decides for each operation (legal, dynamically legal, illegal or unknown), and predict what partial and full conversion do with each.
    - Name each cast the driver inserts when a value changes type as a source or a target materialization, and say why a conversion pattern reads its operands from an adaptor.
    - Trace a function through a pass-per-dialect pipeline to the llvm dialect, and diagnose a missing or misordered pass from the error it produces.
    - Read the memref descriptor and the two calling conventions that carry it across a function boundary, and say what each costs a language whose shapes are all static.
    - Translate the result to LLVM IR and check that fast-math flags and source locations survived the trip.

## A function with two dialects to remove

Start with the smallest function that needs converting: clamping a value into the range zero to one, written branch-free, so both outcomes of each comparison are computed.

--8<-- "includes/examples/mlir/m4-dialect-conversion/clamp.mlir.md"

Two dialects appear in the input, `arith` for the comparisons and selects and `func` for the function, and neither survives. `arith.cmpf` became `llvm.fcmp`, `arith.select` became `llvm.select`, `func.func` became `llvm.func` and `return` became `llvm.return`. The **llvm dialect** is MLIR's model of LLVM IR inside MLIR, and its documentation states the rule that makes it trustworthy: its operations mean what the matching LLVM IR instructions mean, and "any divergence is considered a bug".[^llvmd]

Each flag in the example is one pass, and each pass is a **dialect conversion**: given a description of which operations the output may contain and a set of rewrite patterns, the framework rewrites operations until the module fits the description, or reports why it cannot.[^conv] The rest of this chapter is about the three parts of that sentence: the description, the patterns, and the types that change along the way.

## The conversion target decides what may remain

The description is a **conversion target**, an object that answers one question about any operation: may it stay as it is? The Dialect Conversion document gives three answers, each settable for a single operation or a whole dialect.[^conv]

- **Legal**: every instance of the operation may stay. A pass targeting LLVM marks the whole llvm dialect legal.
- **Dynamically legal**: some instances may stay, and a callback decides which. The document's example is `arith.addi` legal only on 32-bit integers. The Toy tutorial marks its print operation legal only once its operands have been lowered.[^toy5]
- **Illegal**: no instance may stay. Every one must be rewritten, or the conversion fails.

An operation with none of the three marks is **unknown**, and the modes of the next section treat unknown operations differently.[^conv] A target can also mark an operation **recursively legal**, which makes everything nested inside it legal too, whatever its own mark.[^conv]

The targets of real passes are small. In MLIR 18.1.8, the target shared by the passes that lower to LLVM says two things: the llvm dialect is legal, and so is the bridging cast you will meet shortly. Everything else is unknown to it. The pass that lowers structured loops to branches, `--convert-scf-to-cf`, marks the loop operations of `scf` illegal and every other operation dynamically legal, with a callback that always says yes.[^src-passes]

The patterns are M3's rewrite patterns with one addition, described in the next sections, and they need not reach the target in one step. If patterns rewrite `bar.add` to `baz.add` and `baz.add` to `foo.add`, and only `foo` is legal, the framework chains the two by itself; the document calls this building a graph of conversions.[^conv] The Toy tutorial relies on it when it lowers loops to LLVM through intermediate dialects in one pass, and calls it **transitive lowering**.[^toy6]

### How the driver treats one operation

The framework walks the operations in preorder, an operation before the operations nested in its regions.[^conv] For each one, the 18.1.8 driver asks the questions of Figure 1 in order.[^src-dc]

<figure class="vx-figure">
<svg viewBox="0 0 760 430" role="img" aria-label="Flowchart of how the conversion driver legalizes one operation" aria-describedby="m4-f1-desc">
<title id="m4-f1-title">How the conversion driver legalizes one operation</title>
<desc id="m4-f1-desc">A flowchart read from top to bottom. Take the next operation in preorder. First question: is it legal on the target, statically or because its callback says yes? If yes, keep it. If no, second question: does it fold? If yes, use the folded result. If no, third question: does some pattern replace it with operations that can be legalized in turn? If yes, replace it and legalize the new operations the same way. If no, the operation cannot be legalized, and the mode decides: a full conversion fails; a partial conversion fails if the operation was marked illegal; a partial conversion leaves an unknown operation where it is.</desc>
<rect class="vx-box-strong" x="250" y="14" width="260" height="40" rx="4"/>
<text class="vx-text" x="380" y="39" text-anchor="middle">next operation, in preorder</text>
<line class="vx-line" x1="380" y1="54" x2="380" y2="78"/>
<polygon class="vx-arrowhead" points="375,78 380,86 385,78"/>
<rect class="vx-box" x="250" y="86" width="260" height="50" rx="4"/>
<text class="vx-text" x="380" y="107" text-anchor="middle">legal on the target?</text>
<text class="vx-text-muted" x="380" y="126" text-anchor="middle">statically, or its callback says yes</text>
<line class="vx-line" x1="510" y1="111" x2="552" y2="111"/>
<polygon class="vx-arrowhead" points="552,106 560,111 552,116"/>
<text class="vx-text-muted" x="520" y="102">yes</text>
<rect class="vx-box-accent" x="560" y="91" width="180" height="40" rx="4"/>
<text class="vx-text" x="650" y="116" text-anchor="middle">keep it</text>
<line class="vx-line" x1="380" y1="136" x2="380" y2="160"/>
<polygon class="vx-arrowhead" points="375,160 380,168 385,160"/>
<text class="vx-text-muted" x="390" y="154">no</text>
<rect class="vx-box" x="250" y="168" width="260" height="40" rx="4"/>
<text class="vx-text" x="380" y="193" text-anchor="middle">does it fold?</text>
<line class="vx-line" x1="510" y1="188" x2="552" y2="188"/>
<polygon class="vx-arrowhead" points="552,183 560,188 552,193"/>
<text class="vx-text-muted" x="520" y="179">yes</text>
<rect class="vx-box-accent" x="560" y="168" width="180" height="40" rx="4"/>
<text class="vx-text" x="650" y="193" text-anchor="middle">use the folded result</text>
<line class="vx-line" x1="380" y1="208" x2="380" y2="232"/>
<polygon class="vx-arrowhead" points="375,232 380,240 385,232"/>
<text class="vx-text-muted" x="390" y="226">no</text>
<rect class="vx-box" x="250" y="240" width="260" height="50" rx="4"/>
<text class="vx-text" x="380" y="261" text-anchor="middle">does a pattern replace it with</text>
<text class="vx-text" x="380" y="280" text-anchor="middle">operations that legalize in turn?</text>
<line class="vx-line" x1="510" y1="265" x2="552" y2="265"/>
<polygon class="vx-arrowhead" points="552,260 560,265 552,270"/>
<text class="vx-text-muted" x="520" y="256">yes</text>
<rect class="vx-box-accent" x="560" y="240" width="180" height="50" rx="4"/>
<text class="vx-text" x="650" y="261" text-anchor="middle">replace it; legalize</text>
<text class="vx-text" x="650" y="280" text-anchor="middle">the new operations</text>
<line class="vx-line" x1="380" y1="290" x2="380" y2="314"/>
<polygon class="vx-arrowhead" points="375,314 380,322 385,314"/>
<text class="vx-text-muted" x="390" y="308">no</text>
<rect class="vx-box-strong" x="250" y="322" width="260" height="36" rx="4"/>
<text class="vx-text" x="380" y="345" text-anchor="middle">cannot be legalized: the mode decides</text>
<line class="vx-line" x1="300" y1="358" x2="135" y2="376"/>
<line class="vx-line" x1="380" y1="358" x2="380" y2="376"/>
<line class="vx-line" x1="460" y1="358" x2="625" y2="376"/>
<rect class="vx-box-bad" x="20" y="376" width="230" height="44" rx="4"/>
<text class="vx-text" x="135" y="396" text-anchor="middle">full conversion</text>
<text class="vx-text-muted" x="135" y="413" text-anchor="middle">the pass fails</text>
<rect class="vx-box-bad" x="265" y="376" width="230" height="44" rx="4"/>
<text class="vx-text" x="380" y="396" text-anchor="middle">partial, marked illegal</text>
<text class="vx-text-muted" x="380" y="413" text-anchor="middle">the pass fails</text>
<rect class="vx-box" x="510" y="376" width="230" height="44" rx="4"/>
<text class="vx-text" x="625" y="396" text-anchor="middle">partial, unknown</text>
<text class="vx-text-muted" x="625" y="413" text-anchor="middle">left where it is</text>
</svg>
<figcaption>Figure 1. The questions the MLIR 18.1.8 driver asks of each operation, in order. Folding comes before patterns, so the folds of M3 get the first chance at every operation that is not already legal. A replacement is itself legalized by the same questions, which is how chains of patterns reach the target. An analysis conversion asks the same questions but only records which operations would have been legalized.</figcaption>
</figure>

Two consequences are worth holding on to. A pass can succeed while leaving operations of its own dialect behind, if they are unknown to its target and no pattern matches them. And the error for an illegal operation says which operation failed, not which pattern you forgot.

??? check "`--convert-arith-to-llvm` meets `%s = arith.addf %a, %b : tensor<4xf32>`, an addition of two whole tensors, for which it has no pattern. Does the pass fail?"

    No. Its target marks only the llvm dialect and the bridging cast legal, so `arith.addf` is unknown, and a partial conversion leaves an unknown operation it cannot legalize where it is. MLIR 18.1.8 prints the addition back unchanged and exits successfully (checked on 2026-09-24). A pass that succeeds has not promised that its dialect is gone.

## Partial, full and analysis conversion

The mode is chosen by the function the pass calls, and the Dialect Conversion document defines three.[^conv]

- **Partial conversion** (`applyPartialConversion`) legalizes as many operations as it can. It fails only if an operation explicitly marked illegal remains; unknown operations may stay.
- **Full conversion** (`applyFullConversion`) succeeds only if every operation ends up legal. After it, only operations the target knows remain.
- **Analysis conversion** (`applyAnalysisConversion`) runs a partial conversion to find out which operations could be legalized, records them, and changes nothing.

The passes of the clamp example all use partial conversion: in the 18.1.8 source, the passes for `arith`, `cf`, `memref`, `func` and `scf` each call `applyPartialConversion`.[^src-passes] That is what lets each pass handle one dialect and ignore the others. Nothing in the chain requires a complete result until the end, and the next sections show what does.

The Toy tutorial takes the other route. Its last lowering puts every pattern it needs into one pass and runs a full conversion, so the pass itself guarantees that only the llvm dialect remains.[^toy6] MLIR's own documentation leans the same way: the separate per-dialect passes are "primarily useful for testing and prototyping", and it recommends using the collections of patterns together.[^targetllvmir] MLIR 18.1.8 also has a pass, `--convert-to-llvm`, that gathers the patterns of every dialect in the module that offers them and runs them as one partial conversion.[^passes][^src-passes] This chapter uses separate passes anyway, because they let you watch each dialect disappear.

## Types change too

`clamp01` never changed a type: `f32` is a legal type in the llvm dialect as it stands.[^llvmd] Here is a function whose types must change. It computes a dot product over two eight-element arrays, the loop that the [stage 10 kernel](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for) runs for each output element, and this example runs only the `arith` pass on it:

--8<-- "includes/examples/mlir/m4-dialect-conversion/dot_partial.mlir.md"

`arith.constant 0 : index` became `llvm.mlir.constant(0 : index) : i64`. The llvm dialect has no `index` type, so something must decide what `index` becomes. That something is a **type converter**, an object the patterns share that maps each source type to target types, through callbacks registered with `addConversion`.[^conv] The LLVM one maps `index` to an integer as wide as the data layout of the enclosing module says, `i64` here, and conversion passes let you override the width.[^targetllvmir] With `--convert-arith-to-llvm=index-bitwidth=32` the same constants become `i32` (checked on 2026-09-24).

The constant changed type, but its users did not. `scf.for` was never converted, and it still requires `index` bounds. The framework does not let a use change type silently, since that could change what the using operation means.[^conv] So it inserted a `builtin.unrealized_conversion_cast` from the new `i64` back to `index` for each loop bound. An **unrealized conversion cast** is an operation that converts values between two types without saying how: it has no meaning of its own and exists only so that the module stays well typed while the conversion is unfinished.

### Materializations: casts in both directions

The type converter's second job is **materialization**, producing IR that turns a value of one type into a value of another. The document names two directions.[^conv]

- A **source materialization** turns a converted value back into its original type, for a user that has not been converted. The three casts above are source materializations: new `i64` values made into `index` for `scf.for`.
- A **target materialization** turns a value that has not been converted into the converted type, for a pattern that expects it. An `llvm.add` needs `i64` operands; if one operand is still an `index` value, the driver supplies a cast from `index` to `i64`.

The LLVM type converter uses `unrealized_conversion_cast` for both, and leaves it to later passes to remove the casts once both sides have been converted.[^targetllvmir] If a needed materialization cannot be built, the whole conversion fails.[^conv] Figure 2 shows both directions at once, in the dot function after the first three passes of the next section have run.

<figure class="vx-figure">
<svg viewBox="0 0 760 290" role="img" aria-label="A source materialization and a target materialization in the partly converted dot function" aria-describedby="m4-f2-desc">
<title id="m4-f2-title">Source and target materializations</title>
<desc id="m4-f2-desc">Two rows, each read from left to right: the value's definition, the cast the driver inserted, and the value's user. Top row: the converted constant, llvm.mlir.constant 0 of type i64, feeds a cast from i64 to index, labelled source materialization, which feeds cf.br to the loop header, not yet converted, which wants index. Bottom row: the header block's argument, still of type index because the block was not converted, feeds a cast from index to i64, labelled target materialization, which feeds llvm.icmp and llvm.add, converted operations that want i64. The two casts are highlighted.</desc>
<text class="vx-text-muted" x="20" y="24">definition</text>
<text class="vx-text-muted" x="285" y="24">cast inserted by the driver</text>
<text class="vx-text-muted" x="520" y="24">user</text>
<rect class="vx-box" x="20" y="40" width="230" height="60" rx="4"/>
<text class="vx-mono" x="32" y="64">llvm.mlir.constant(0)</text>
<text class="vx-text-muted" x="32" y="86">converted: an i64</text>
<line class="vx-line" x1="250" y1="70" x2="277" y2="70"/>
<polygon class="vx-arrowhead" points="277,65 285,70 277,75"/>
<rect class="vx-box-accent vx-pulse" x="285" y="40" width="210" height="60" rx="4"/>
<text class="vx-mono" x="297" y="64">cast i64 to index</text>
<text class="vx-text-accent" x="297" y="86">source materialization</text>
<line class="vx-line" x1="495" y1="70" x2="512" y2="70"/>
<polygon class="vx-arrowhead" points="512,65 520,70 512,75"/>
<rect class="vx-box" x="520" y="40" width="220" height="60" rx="4"/>
<text class="vx-mono" x="532" y="64">cf.br ^bb1(%1, ...)</text>
<text class="vx-text-muted" x="532" y="86">not converted: wants index</text>
<rect class="vx-box" x="20" y="150" width="230" height="60" rx="4"/>
<text class="vx-mono" x="32" y="174">^bb1(%5: index, ...)</text>
<text class="vx-text-muted" x="32" y="196">not converted: an index</text>
<line class="vx-line" x1="250" y1="180" x2="277" y2="180"/>
<polygon class="vx-arrowhead" points="277,175 285,180 277,185"/>
<rect class="vx-box-accent vx-pulse" x="285" y="150" width="210" height="60" rx="4"/>
<text class="vx-mono" x="297" y="174">cast index to i64</text>
<text class="vx-text-accent" x="297" y="196">target materialization</text>
<line class="vx-line" x1="495" y1="180" x2="512" y2="180"/>
<polygon class="vx-arrowhead" points="512,175 520,180 512,185"/>
<rect class="vx-box" x="520" y="150" width="220" height="60" rx="4"/>
<text class="vx-mono" x="532" y="174">llvm.icmp, llvm.add</text>
<text class="vx-text-muted" x="532" y="196">converted: want i64</text>
<text class="vx-text-muted" x="20" y="250">Top: a converted value flows to an unconverted user. Bottom: an unconverted value</text>
<text class="vx-text-muted" x="20" y="270">flows to converted users. Once both sides are converted, each cast has nothing left to do.</text>
</svg>
<figcaption>Figure 2. The two directions of materialization, taken from the dot function after <code>--convert-scf-to-cf --convert-cf-to-llvm --convert-arith-to-llvm</code> in MLIR 18.1.8. The loop header <code>^bb1</code> still takes <code>index</code> arguments, because no pass so far has converted block signatures in a <code>func.func</code>.</figcaption>
</figure>

### The adaptor: operands as they are now

The bottom row of Figure 2 explains the one thing that sets a **conversion pattern** apart from M3's patterns. Its `matchAndRewrite` receives the matched operation and an **adaptor**, a view of the operation's operands that holds "the most recent replacement values" for each of them.[^conv] When the pattern for `arith.addi` rewrites the loop counter's increment, `op.getOperands()` still names `%5`, the `index` block argument the operation was written with. The adaptor names the cast of `%5` to `i64`, a value of the type the new `llvm.add` needs. The pattern builds its replacement from the adaptor and never checks types itself.

The document is careful about what the adaptor promises: the type of each value, as the pattern's type converter says, and not which value. It may be the replacement some pattern produced, or a transitory cast.[^conv] It also says why the old operands stay visible at all. By default the driver delays some changes, such as replacing an operation's uses, until the conversion ends, so that it can roll back patterns that led to a dead end; until then the old IR stays in place and patterns can still see it.[^conv]

??? check "The pattern for `arith.cmpi` runs on `%2 = arith.cmpi slt, %0, %c8 : index`, where `%c8` has already been converted to an `i64` constant and `%0` is the loop header's unconverted `index` argument. What do `op.getOperands()` and the adaptor's operands each hold?"

    `op.getOperands()` holds `%0` and `%c8`, both of type `index`: the values the operation was written with. The adaptor holds two `i64` values: a cast of `%0` to `i64` (a target materialization) and the new `llvm.mlir.constant`, the most recent replacement of `%c8`. The pattern builds `llvm.icmp` from the adaptor's values, which is why the output compares `i64` values.

## A pipeline, one pass per dialect

The same function, run through the whole pipeline, comes out as the llvm dialect and nothing else:

--8<-- "includes/examples/mlir/m4-dialect-conversion/dot_full.mlir.md"

The two `memref<8xf32>` parameters became ten scalar parameters, the loop became three blocks and a back edge, and each load became an `llvm.extractvalue`, an `llvm.getelementptr` and an `llvm.load`. Figure 3 steps through the six passes. It counts the operations of each dialect after every pass, taken from `mlir-opt --mlir-print-ir-after-all` on MLIR 18.1.8 (checked on 2026-09-24), with the module itself left out. Try to predict each step before you read it.

<figure class="vx-figure">
<div class="vx-stepper">
<div class="vx-step">
<p><strong>Step 1. The input.</strong> Four dialects: <code>func</code> (the function and its return), <code>scf</code> (the loop and its yield), <code>arith</code> (four constants, a multiply and an add) and <code>memref</code> (two loads).</p>
<svg viewBox="0 0 760 222" role="img" aria-label="Operation counts input: func 2, scf 2, cf 0, arith 6, memref 2, llvm 0, casts 0.">
<text class="vx-mono" x="20" y="28">func</text>
<rect class="vx-box" x="110" y="14" width="36" height="18" rx="2"/>
<text class="vx-text" x="154" y="28">2</text>
<text class="vx-mono" x="20" y="57">scf</text>
<rect class="vx-box" x="110" y="43" width="36" height="18" rx="2"/>
<text class="vx-text" x="154" y="57">2</text>
<text class="vx-mono" x="20" y="86">cf</text>
<text class="vx-text-muted" x="116" y="86">0</text>
<text class="vx-mono" x="20" y="115">arith</text>
<rect class="vx-box" x="110" y="101" width="108" height="18" rx="2"/>
<text class="vx-text" x="226" y="115">6</text>
<text class="vx-mono" x="20" y="144">memref</text>
<rect class="vx-box" x="110" y="130" width="36" height="18" rx="2"/>
<text class="vx-text" x="154" y="144">2</text>
<text class="vx-mono" x="20" y="173">llvm</text>
<text class="vx-text-muted" x="116" y="173">0</text>
<text class="vx-mono" x="20" y="202">casts</text>
<text class="vx-text-muted" x="116" y="202">0</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 2. After <code>--convert-scf-to-cf</code>.</strong> The loop is gone. In its place: a <code>cf.br</code> into a header block whose two arguments carry the counter and the running sum, an <code>arith.cmpi</code> and a <code>cf.cond_br</code> that test the counter, and a body that ends with an <code>arith.addi</code> and a <code>cf.br</code> back to the header. No type changed, so there is no cast.</p>
<svg viewBox="0 0 760 222" role="img" aria-label="Operation counts after convert-scf-to-cf: func 2, scf 0, cf 3, arith 8, memref 2, llvm 0, casts 0.">
<text class="vx-mono" x="20" y="28">func</text>
<rect class="vx-box" x="110" y="14" width="36" height="18" rx="2"/>
<text class="vx-text" x="154" y="28">2</text>
<text class="vx-mono" x="20" y="57">scf</text>
<text class="vx-text-accent" x="116" y="57">0</text>
<text class="vx-mono" x="20" y="86">cf</text>
<rect class="vx-box-accent" x="110" y="72" width="54" height="18" rx="2"/>
<text class="vx-text" x="172" y="86">3</text>
<text class="vx-mono" x="20" y="115">arith</text>
<rect class="vx-box-accent" x="110" y="101" width="144" height="18" rx="2"/>
<text class="vx-text" x="262" y="115">8</text>
<text class="vx-mono" x="20" y="144">memref</text>
<rect class="vx-box" x="110" y="130" width="36" height="18" rx="2"/>
<text class="vx-text" x="154" y="144">2</text>
<text class="vx-mono" x="20" y="173">llvm</text>
<text class="vx-text-muted" x="116" y="173">0</text>
<text class="vx-mono" x="20" y="202">casts</text>
<text class="vx-text-muted" x="116" y="202">0</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 3. After <code>--convert-cf-to-llvm</code>.</strong> Only the conditional branch converted, to <code>llvm.cond_br</code>. The two <code>cf.br</code> pass <code>index</code> values to a header whose arguments are still <code>index</code>, and the branch patterns refuse a branch whose converted operands would not match its target block. Partial conversion leaves them.</p>
<svg viewBox="0 0 760 222" role="img" aria-label="Operation counts after convert-cf-to-llvm: func 2, scf 0, cf 2, arith 8, memref 2, llvm 1, casts 0.">
<text class="vx-mono" x="20" y="28">func</text>
<rect class="vx-box" x="110" y="14" width="36" height="18" rx="2"/>
<text class="vx-text" x="154" y="28">2</text>
<text class="vx-mono" x="20" y="57">scf</text>
<text class="vx-text-muted" x="116" y="57">0</text>
<text class="vx-mono" x="20" y="86">cf</text>
<rect class="vx-box-accent" x="110" y="72" width="36" height="18" rx="2"/>
<text class="vx-text" x="154" y="86">2</text>
<text class="vx-mono" x="20" y="115">arith</text>
<rect class="vx-box" x="110" y="101" width="144" height="18" rx="2"/>
<text class="vx-text" x="262" y="115">8</text>
<text class="vx-mono" x="20" y="144">memref</text>
<rect class="vx-box" x="110" y="130" width="36" height="18" rx="2"/>
<text class="vx-text" x="154" y="144">2</text>
<text class="vx-mono" x="20" y="173">llvm</text>
<rect class="vx-box-accent" x="110" y="159" width="18" height="18" rx="2"/>
<text class="vx-text" x="136" y="173">1</text>
<text class="vx-mono" x="20" y="202">casts</text>
<text class="vx-text-muted" x="116" y="202">0</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 4. After <code>--convert-arith-to-llvm</code>.</strong> All eight <code>arith</code> operations became <code>llvm</code> operations, and the <code>index</code> values they produce became <code>i64</code>. Three casts appear: two source materializations turn <code>i64</code> back into <code>index</code> for the operands of the two <code>cf.br</code>, and one target materialization turns the header's <code>index</code> argument into <code>i64</code> for <code>llvm.icmp</code> and <code>llvm.add</code> (Figure 2).</p>
<svg viewBox="0 0 760 222" role="img" aria-label="Operation counts after convert-arith-to-llvm: func 2, scf 0, cf 2, arith 0, memref 2, llvm 9, casts 3.">
<text class="vx-mono" x="20" y="28">func</text>
<rect class="vx-box" x="110" y="14" width="36" height="18" rx="2"/>
<text class="vx-text" x="154" y="28">2</text>
<text class="vx-mono" x="20" y="57">scf</text>
<text class="vx-text-muted" x="116" y="57">0</text>
<text class="vx-mono" x="20" y="86">cf</text>
<rect class="vx-box" x="110" y="72" width="36" height="18" rx="2"/>
<text class="vx-text" x="154" y="86">2</text>
<text class="vx-mono" x="20" y="115">arith</text>
<text class="vx-text-accent" x="116" y="115">0</text>
<text class="vx-mono" x="20" y="144">memref</text>
<rect class="vx-box" x="110" y="130" width="36" height="18" rx="2"/>
<text class="vx-text" x="154" y="144">2</text>
<text class="vx-mono" x="20" y="173">llvm</text>
<rect class="vx-box-accent" x="110" y="159" width="162" height="18" rx="2"/>
<text class="vx-text" x="280" y="173">9</text>
<text class="vx-mono" x="20" y="202">casts</text>
<rect class="vx-box-bad" x="110" y="188" width="54" height="18" rx="2"/>
<text class="vx-text" x="172" y="202">3</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 5. After <code>--finalize-memref-to-llvm</code>.</strong> Each load became an <code>llvm.extractvalue</code> of the aligned pointer, an <code>llvm.getelementptr</code> and an <code>llvm.load</code>. Three more casts: each <code>memref</code> parameter into its descriptor struct, and the header's argument into <code>i64</code> once more, for the addresses.</p>
<svg viewBox="0 0 760 222" role="img" aria-label="Operation counts after finalize-memref-to-llvm: func 2, scf 0, cf 2, arith 0, memref 0, llvm 15, casts 6.">
<text class="vx-mono" x="20" y="28">func</text>
<rect class="vx-box" x="110" y="14" width="36" height="18" rx="2"/>
<text class="vx-text" x="154" y="28">2</text>
<text class="vx-mono" x="20" y="57">scf</text>
<text class="vx-text-muted" x="116" y="57">0</text>
<text class="vx-mono" x="20" y="86">cf</text>
<rect class="vx-box" x="110" y="72" width="36" height="18" rx="2"/>
<text class="vx-text" x="154" y="86">2</text>
<text class="vx-mono" x="20" y="115">arith</text>
<text class="vx-text-muted" x="116" y="115">0</text>
<text class="vx-mono" x="20" y="144">memref</text>
<text class="vx-text-accent" x="116" y="144">0</text>
<text class="vx-mono" x="20" y="173">llvm</text>
<rect class="vx-box-accent" x="110" y="159" width="270" height="18" rx="2"/>
<text class="vx-text" x="388" y="173">15</text>
<text class="vx-mono" x="20" y="202">casts</text>
<rect class="vx-box-bad" x="110" y="188" width="108" height="18" rx="2"/>
<text class="vx-text" x="226" y="202">6</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 6. After <code>--convert-func-to-llvm</code>.</strong> The signature now has ten scalar parameters, and the body starts by rebuilding two descriptors from them with <code>llvm.insertvalue</code>, each cast back to <code>memref</code> for the old users. The header's arguments became <code>i64</code>, so the two <code>cf.br</code> converted too. Nine casts remain, each one part of a round trip or unused.</p>
<svg viewBox="0 0 760 222" role="img" aria-label="Operation counts after convert-func-to-llvm: func 0, scf 0, cf 0, arith 0, memref 0, llvm 31, casts 9.">
<text class="vx-mono" x="20" y="28">func</text>
<text class="vx-text-accent" x="116" y="28">0</text>
<text class="vx-mono" x="20" y="57">scf</text>
<text class="vx-text-muted" x="116" y="57">0</text>
<text class="vx-mono" x="20" y="86">cf</text>
<text class="vx-text-accent" x="116" y="86">0</text>
<text class="vx-mono" x="20" y="115">arith</text>
<text class="vx-text-muted" x="116" y="115">0</text>
<text class="vx-mono" x="20" y="144">memref</text>
<text class="vx-text-muted" x="116" y="144">0</text>
<text class="vx-mono" x="20" y="173">llvm</text>
<rect class="vx-box-accent" x="110" y="159" width="558" height="18" rx="2"/>
<text class="vx-text" x="676" y="173">31</text>
<text class="vx-mono" x="20" y="202">casts</text>
<rect class="vx-box-bad" x="110" y="188" width="162" height="18" rx="2"/>
<text class="vx-text" x="280" y="202">9</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 7. After <code>--reconcile-unrealized-casts</code>.</strong> Each round trip of casts is replaced by the value it started from, and unused casts are erased. Thirty-one <code>llvm</code> operations and nothing else: the module <code>dot_full</code> prints.</p>
<svg viewBox="0 0 760 222" role="img" aria-label="Operation counts after reconcile-unrealized-casts: func 0, scf 0, cf 0, arith 0, memref 0, llvm 31, casts 0.">
<text class="vx-mono" x="20" y="28">func</text>
<text class="vx-text-muted" x="116" y="28">0</text>
<text class="vx-mono" x="20" y="57">scf</text>
<text class="vx-text-muted" x="116" y="57">0</text>
<text class="vx-mono" x="20" y="86">cf</text>
<text class="vx-text-muted" x="116" y="86">0</text>
<text class="vx-mono" x="20" y="115">arith</text>
<text class="vx-text-muted" x="116" y="115">0</text>
<text class="vx-mono" x="20" y="144">memref</text>
<text class="vx-text-muted" x="116" y="144">0</text>
<text class="vx-mono" x="20" y="173">llvm</text>
<rect class="vx-box" x="110" y="159" width="558" height="18" rx="2"/>
<text class="vx-text" x="676" y="173">31</text>
<text class="vx-mono" x="20" y="202">casts</text>
<text class="vx-text-accent" x="116" y="202">0</text>
</svg>
</div>
</div>
<figcaption>Figure 3. The dot function through the six passes of <code>dot_full</code>, as MLIR 18.1.8 prints it after each one. Each bar counts the operations of one dialect; bars that changed at a step are drawn in the accent colour, and casts in the warning colour. A cast is a promise that some later pass will finish a conversion; the pipeline is done when none is left.</figcaption>
</figure>

Two steps in Figure 3 do not go as the pass names suggest. `--convert-cf-to-llvm` converts the conditional branch but neither `cf.br`. Both pass values to the loop header, whose arguments are still `index`, and the 18.1.8 branch patterns refuse to build an `llvm.br` whose operands would not match the types of the block it jumps to.[^src-passes] MLIR's documentation warns about this case: the separate passes can fail to convert branches whose types disagree with their target blocks.[^targetllvmir]

The two branches are converted by `--convert-func-to-llvm`, which converts the function's signature and the arguments of every block in its body. In 18.1.8 that pass also carries the `arith` and `cf` patterns, with a comment in the source marking them for removal once the dedicated passes suffice.[^src-passes] That detail matters when you reorder the pipeline.

### The last pass: reconcile the casts

After `--convert-func-to-llvm`, nine casts remain and no pass converts anything else. Look at the kind of chain they form, here written with `!desc` for the descriptor struct type:

```mlir
%6  = builtin.unrealized_conversion_cast %5 : !desc to memref<8xf32>
%14 = builtin.unrealized_conversion_cast %6 : memref<8xf32> to !desc
%27 = llvm.extractvalue %14[1] : !desc
```

The first cast was a source materialization from the signature conversion, and the second a target materialization from the load conversion. Together they go from `!desc` back to `!desc`, so `%14` could be `%5`. The pass `--reconcile-unrealized-casts` finds such chains: when the casts in a chain end at the type they started from, their users get the original value and the casts are erased; casts with no users are erased too. A chain that ends at a different type, or whose middle value still has a user that is not a cast, cannot be removed.[^src-reconcile]

Its target marks the cast illegal, so that last case is an error. This is the check the rest of the pipeline postponed. Each earlier pass was allowed to leave work unfinished, but a cast that cannot be removed means the conversion is incomplete: some operation on one side of it was never converted.[^src-reconcile] The error for that case names the cast, not the missing pass, which the next section shows.

## A pass left out

Suppose the `memref` pass is left out. Before reading on, predict which pass reports the problem and which operation the error names. The next example runs the pipeline without `--finalize-memref-to-llvm` on a function that adds the first and last elements of a four-element array. The comment line starting `expected-error` and the option `--verify-diagnostics` turn the expected failure into a test, as in [M2](m2-reading-mlir.md#the-verifier-decides-what-is-valid): the file passes only while `mlir-opt` reports exactly that error.

--8<-- "includes/examples/mlir/m4-dialect-conversion/ends_missing_pass.mlir.md"

Without `--verify-diagnostics`, MLIR 18.1.8 prints the error at the parameter `%v`:

```text
error: failed to legalize operation 'builtin.unrealized_conversion_cast' that was explicitly marked illegal
note: see current operation: %6 = "builtin.unrealized_conversion_cast"(%5) : (!llvm.struct<(ptr, ptr, i64, array<1 x i64>, array<1 x i64>)>) -> memref<4xf32>
```

Every pass before the last one succeeded. `--convert-func-to-llvm` rewrote the signature regardless, because nothing in its target requires `memref.load` to be converted first, and it bridged the new descriptor back to a `memref` for the two loads. Then `--reconcile-unrealized-casts` found a chain from the struct to `memref<4xf32>` whose end has live users, two `memref.load` operations, and could not remove it.

To see those users, add `--mlir-print-ir-after-failure`, which prints the module as the failing pass left it; in this build it shows both `memref.load` operations still reading from the cast (checked on 2026-09-24). The general method is the one the error suggests: find the cast the error names, follow its result to the users that are not casts, and the dialect of those users names the missing pass.

**Order matters for the same reason.** Running `--convert-func-to-llvm` first, or `--convert-scf-to-cf` last, also fails in `--reconcile-unrealized-casts`, both times on a cast from `i64` to `index` (checked on 2026-09-24). In both orders the loop header was created by `--convert-scf-to-cf` after `--convert-func-to-llvm` had converted the blocks that existed then. No later pass converts block arguments, so the header keeps its `index` arguments and the two `cf.br` into it survive. The rule to take away: a pass that creates blocks or operations of another dialect must run before the pass that converts them. Not every order matters, though: running `--finalize-memref-to-llvm` after `--convert-func-to-llvm` also succeeds, with loads that read the pointer argument directly instead of extracting it from the descriptor.

??? check "You run `--convert-scf-to-cf --convert-cf-to-llvm --convert-arith-to-llvm --finalize-memref-to-llvm --reconcile-unrealized-casts` on the dot function, leaving out `--convert-func-to-llvm`. Which operations are left, and does the last pass succeed?"

    `func.func` and `func.return` are left, and so are the two `cf.br`, because only the signature conversion in `--convert-func-to-llvm` changes the loop header's `index` arguments. The casts that bridge those `index` arguments to the converted `i64` users cannot be removed, since their ends have live users of a different type, so `--reconcile-unrealized-casts` fails with the same message about an illegal cast. Run it to confirm, and use `--mlir-print-ir-after-failure` to see the survivors.

## The memref descriptor at a function boundary

The dot function's signature grew from two parameters to ten. The type converter maps a ranked memref to one value, a struct called the **memref descriptor**, with five fields:[^targetllvmir]

1. the **allocated pointer**, the buffer as it was allocated, needed only to free it;
2. the **aligned pointer**, the properly aligned start of the data that loads and stores address;
3. the **offset**, in elements, from the aligned pointer to the first element the memref refers to;
4. the **sizes**, one per dimension, in elements;
5. the **strides**, one per dimension: how many elements of the buffer to skip to reach the next index in that dimension.

For `memref<8xf32>` that is `!llvm.struct<(ptr, ptr, i64, array<1 x i64>, array<1 x i64>)>`, as the output shows. A size that the type fixes is still stored, as a constant; the document says this normalization serves as an ABI for linking against separately compiled functions.[^targetllvmir]

At a function boundary the struct is split. The documentation says memref structs appearing as function arguments are "unbundled into individual function arguments", to allow metadata such as aliasing information on each pointer.[^targetllvmir] A rank-1 memref therefore becomes five arguments, and a memref of rank $n$ becomes two pointers and $2n + 1$ integers. Figure 4 draws both conventions.

<figure class="vx-figure">
<svg viewBox="0 0 760 320" role="img" aria-label="A memref value, its five-field descriptor, and the arguments each calling convention passes" aria-describedby="m4-f4-desc">
<title id="m4-f4-title">The memref descriptor and two calling conventions</title>
<desc id="m4-f4-desc">On the left, one value of type memref of 8 f32. An arrow leads to a struct with five fields: index 0, allocated pointer; index 1, aligned pointer, highlighted; index 2, offset, an i64; index 3, sizes, an array of one i64; index 4, strides, an array of one i64. Two arrows lead from the struct to the right. The upper one reaches the default calling convention: five arguments, pointer, pointer, i64, i64, i64. The lower one reaches the bare pointer convention: one argument, the aligned pointer, allowed only for static shapes with the default layout.</desc>
<rect class="vx-box-strong" x="20" y="126" width="160" height="60" rx="4"/>
<text class="vx-mono" x="100" y="152" text-anchor="middle">memref&lt;8xf32&gt;</text>
<text class="vx-text-muted" x="100" y="172" text-anchor="middle">one value</text>
<line class="vx-line" x1="180" y1="156" x2="212" y2="156"/>
<polygon class="vx-arrowhead" points="212,151 220,156 212,161"/>
<rect class="vx-box-strong" x="220" y="20" width="240" height="276" rx="6"/>
<text class="vx-mono" x="236" y="44">!llvm.struct&lt;(...)&gt;</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5">
<rect class="vx-box" x="236" y="58" width="208" height="36" rx="3"/>
<text class="vx-mono" x="248" y="81">[0] allocated ptr</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5">
<rect class="vx-box-accent" x="236" y="104" width="208" height="36" rx="3"/>
<text class="vx-mono" x="248" y="127">[1] aligned ptr</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5">
<rect class="vx-box" x="236" y="150" width="208" height="36" rx="3"/>
<text class="vx-mono" x="248" y="173">[2] offset: i64</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5">
<rect class="vx-box" x="236" y="196" width="208" height="36" rx="3"/>
<text class="vx-mono" x="248" y="219">[3] sizes: [1 x i64]</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 5">
<rect class="vx-box" x="236" y="242" width="208" height="36" rx="3"/>
<text class="vx-mono" x="248" y="265">[4] strides: [1 x i64]</text>
</g>
<line class="vx-line" x1="460" y1="120" x2="492" y2="90"/>
<polygon class="vx-arrowhead" points="487,86 496,86 492,95"/>
<rect class="vx-box" x="500" y="30" width="240" height="110" rx="4"/>
<text class="vx-text" x="514" y="54">default convention</text>
<text class="vx-mono" x="514" y="82">ptr, ptr, i64, i64, i64</text>
<text class="vx-text-muted" x="514" y="106">five arguments per rank-1 array;</text>
<text class="vx-text-muted" x="514" y="124">2 + (2n + 1) for rank n</text>
<line class="vx-line" x1="460" y1="200" x2="492" y2="224"/>
<polygon class="vx-arrowhead" points="487,229 496,229 492,220"/>
<rect class="vx-box" x="500" y="180" width="240" height="110" rx="4"/>
<text class="vx-text" x="514" y="204">bare pointer convention</text>
<text class="vx-mono" x="514" y="232">ptr</text>
<text class="vx-text-muted" x="514" y="256">the aligned pointer only;</text>
<text class="vx-text-muted" x="514" y="274">static shape, default layout</text>
</svg>
<figcaption>Figure 4. What a ranked memref becomes. The type converter maps it to one struct; at a function boundary the default convention passes each field as its own argument, and the bare pointer convention passes only the aligned pointer, rebuilding the other fields from the type inside the callee. In <code>dot_full</code>, the loads read only field [1], the highlighted one.</figcaption>
</figure>

The loads in `dot_full` read only the aligned pointer. The offset (0) and the stride (1) are fixed by the type, so the `memref` lowering used them as constants instead of reading them from the descriptor; the offset, size and stride arguments arrive and go unused. The same holds in two dimensions: for a load from a `memref<2x3xf32>`, the 18.1.8 lowering computes the row times the constant 3, plus the column, the row-major address that the default layout promises (checked on 2026-09-24).

If every memref argument is static and has the default layout, MLIR offers a second convention. With the **bare pointer calling convention**, a memref argument is passed as one pointer to the aligned data. The documentation lists its conditions: default layout, all dimensions static, and memory allocated so that the allocated and aligned pointers match, or else allocated and freed by the same function.[^targetllvmir]

--8<-- "includes/examples/mlir/m4-dialect-conversion/ends_bare_ptr.mlir.md"

The signature takes one `!llvm.ptr`, and the first lines of the body rebuild a complete descriptor from it and from constants: the pointer in both pointer fields, an offset of 0, a size of 4 and a stride of 1. Inside the function the descriptor is still what everything uses; only the boundary changed. The constant subscripts 0 and 3 went straight into the address arithmetic.

Inside the function, the descriptor costs nothing once LLVM's optimizer has run. For the dot function, translating both versions to LLVM IR and running `opt -O2` from LLVM 18.1.8 gives the same instructions in both bodies; no `insertvalue` or `extractvalue` survives (checked on 2026-09-24). What differs is the signature: ten arguments against two, and every caller of the default version must produce all ten. When you care about call overhead, measure it on your own machine with a benchmark that calls the function many times; this chapter has no measured figure to give you.

For code in C that must call the default version, MLIR can emit a wrapper. The unit attribute `llvm.emit_c_interface` on a function asks the conversion to add a function named `_mlir_ciface_` followed by the original name, which takes each memref as a pointer to a struct laid out as Clang lays out the matching C struct.[^targetllvmir]

??? check "The stage 10 kernel takes three rank-2 arrays. How many arguments do they become under the default convention, and how many under the bare pointer convention? Which of the three conditions for the bare pointer convention does Vortex's decision 11 settle?"

    Each rank-2 memref becomes two pointers, one offset, two sizes and two strides: seven arguments, so 21 in all. Under the bare pointer convention the three arrays become three pointers. Decision 11 settles "all dimensions static". The default layout is a choice of the lowering (Vortex's row-major order is that default), and the condition on allocated and aligned pointers depends on how the caller allocates the arrays, which decision 11 does not say.

## From the llvm dialect to LLVM IR

Once only the llvm dialect remains, `mlir-translate --mlir-to-llvmir` writes an LLVM IR module. The document describes the scheme: functions are declared first so that they can refer to each other, then each function is translated block by block, and each block argument becomes a phi at the top of its block, whose incoming values are filled in once every block exists.[^targetllvmir] The llvm dialect has no phi operation for that reason: terminators pass values to block arguments instead.[^llvmd] Translating the output of `dot_full` produces the loop header this way:

```llvm
21:                                               ; preds = %25, %10
  %22 = phi i64 [ %34, %25 ], [ 0, %10 ]
  %23 = phi float [ %33, %25 ], [ 0.000000e+00, %10 ]
```

The block arguments `%16` and `%17` of `^bb1` became two phis, one incoming value from each of the two branches that passed them. The types map one to one: each LLVM IR type has exactly one MLIR counterpart, a builtin type such as `i64` or `f32` where one exists and an llvm dialect type such as `!llvm.struct` where none does.[^llvmd] From this `.ll` file on, everything [stage 6](../compiler/guide/stage-6-first-machine-code.md#ssa-if-you-use-llvm) described applies unchanged: `opt`, `llc`, an object file.

Only the llvm dialect and a few dialects with their own translation can go this far. Given the partial output of `dot_partial`, `mlir-translate` 18.1.8 stops at the first `scf.for` and reports that the `scf` dialect is not registered with it (checked on 2026-09-24).

**Fast-math flags travel.** The `arith` patterns carry each operation's `fastmath` property over to the llvm operation. An `arith.mulf` with `fastmath<contract>` became `llvm.fmul` with `fastmathFlags = #llvm.fastmath<contract>`, and after translation `fmul contract float` (checked on 2026-09-24). An operation with `none` gets no flag at any stage. In LLVM IR, `contract` permits fusing the multiply with a following add into one fused multiply-add.[^llvm-fmf] The conversion neither adds nor removes such permissions; whatever the MLIR carried, the LLVM IR carries.

**Locations travel too.** With `--mlir-print-debuginfo`, the `llvm.fmul` in the output of `dot_full` carries the location of the `arith.mulf` it replaced, line 19 of `dot_full.mlir` (checked on 2026-09-24). The patterns gave each new operation the location of the operation it replaced, which is how M2's mandatory locations survive a lowering.

??? check "The optimized LLVM IR of the dot function (from `opt -O2`) still contains `fadd float %14, 0.000000e+00`, an addition of zero. Why did neither MLIR's conversion nor LLVM's optimizer remove it?"

    The conversion does not optimize: it rewrote `arith.addf %acc, %p` with `%acc` starting at `0.0` into `llvm.fadd`, one for one. LLVM's optimizer then unrolled the loop and put the constant into the first addition, but it may not delete `x + 0.0`, because when `x` is `-0.0` the result is `+0.0`, not `x`. That is M3's rule for the float identities that are false, applied by a different compiler with the same meaning of `fadd`.

## Where Vortex's facts meet the conversion

| Vortex fact | What the conversion does with it |
| --- | --- |
| Fixed shapes ([decision 11](../decisions/arrays.md#d11)) | Every array qualifies for the bare pointer convention's shape condition; under the default convention, static sizes are still passed as arguments |
| Row-major layout ([decision 43](../decisions/arrays.md#d43)) | The default memref layout; loads compute row-major addresses from the constant strides the type implies |
| No contraction, no reassociation ([decision 56](../decisions/numbers.md#d56)) | `fastmath` flags are carried unchanged into LLVM IR, so `none` must hold before conversion, and a test can check the `.ll` file for flags |
| An `&mut` output overlaps no argument ([decision 25](../decisions/references.md#d25)) | Not known to the conversion. An `llvm.noalias` attribute written on a memref parameter reached the translated pointer under the bare pointer convention and was dropped under the default one, in 18.1.8 (checked on 2026-09-24) |
| Positions for runtime errors ([decision 14](../decisions/program.md#d14)) | Each llvm operation keeps the location of the operation it replaced |

## For Vortex

!!! vortex "Exercise"

    **Build** a lowering step for the MLIR your [M2 tool](m2-reading-mlir.md#for-vortex) writes and your [M3 cleanup](m3-passes-and-rewriting.md#for-vortex) cleans, for the [stage 10 kernel](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for): one `mlir-opt` pipeline, kept in one place in your test scripts, that ends in the llvm dialect, followed by `mlir-translate --mlir-to-llvmir`, `llc` and a link against a small test driver that calls the kernel.

    1. **A census first.** Before choosing passes, list every dialect in the cleaned file and the operations of each. For each dialect, name the pass that removes it and the dialects that pass creates, and order the pipeline from that list. Write down, for each pass, whether you expect it to leave casts behind.
    2. **A calling convention, chosen.** Decide between the default and the bare pointer convention for the kernel's three arrays, and write down why, with the argument count each would give. Your test driver must pass exactly the arguments the signature asks for.
    3. **Decision 56 survives into LLVM IR.** A test that fails if any floating-point instruction in the `.ll` file carries a fast-math flag, or if the file, before `opt` runs, has a different number of `fmul` and `fadd` instructions than your MLIR has `arith.mulf` and `arith.addf` operations.
    4. **The same answers.** Run the lowered kernel and the kernel your own compiler builds on the same inputs, and compare the outputs bit for bit.

    **Not yet:** writing a conversion pattern or a pass in C++, a dialect of your own ([M12](m12-vortex-gpu-path.md) discusses that choice), `linalg` ([M5](m5-structured-ops.md)), bufferization ([M7](m7-bufferization.md)), GPUs ([M10](m10-mlir-for-gpus.md)), and any decision about whether Vortex adopts MLIR.

    **Proof that it works:**

    - `mlir-opt` with your pipeline prints a module in which every operation belongs to the llvm dialect, checked by a test, and `mlir-translate --mlir-to-llvmir` accepts it without a diagnostic.
    - The argument count in the printed `llvm.func` signature matches the count you wrote down in step 2.
    - The outputs of step 4 match bit for bit, for the stage 10 shapes and for one more shape.
    - A canary for step 3: hand-edit one copy of the MLIR so that one `arith.mulf` carries `fastmath<contract>`, lower it, and confirm your test fails on the `.ll` file.
    - A canary for the pipeline: remove the pass that converts `memref` and confirm the pipeline fails with an error about an unrealized conversion cast; save the message, and use `--mlir-print-ir-after-failure` to show which operations were left unconverted.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does a conversion target decide?** Whether each operation may remain: legal, dynamically legal by a callback, illegal, or unknown when it has no mark.
    - **What is the difference between partial and full conversion?** Partial conversion may leave unknown operations and fails only on illegal ones; full conversion fails unless every operation ends up legal. Analysis conversion only records what could be legalized.
    - **Why does a conversion pattern read an adaptor?** Its operands may already have been replaced, possibly by values of a converted type; the adaptor gives the current values in the types the pattern expects.
    - **What is an unrealized conversion cast?** A materialization that bridges a converted value and an unconverted use, in either direction; `--reconcile-unrealized-casts` removes the round trips and fails on any cast that still has work to do.
    - **Why does a missing pass produce an error about a cast?** Every pass before the last is a partial conversion and succeeds; only the cast removal requires completeness, and the cast it cannot remove is the one next to the unconverted operation.
    - **What does a `memref` become at a function boundary?** Under the default convention, its descriptor's fields as separate arguments, $2n + 3$ for rank $n$; under the bare pointer convention, one pointer, allowed only for static shapes with the default layout.
    - **Which of Vortex's promises does the conversion carry to LLVM IR by itself?** Fast-math flags and locations, unchanged; aliasing, not in 18.1.8.

## Where this comes back

!!! next "You will use this again in"

    - [M5. Structured ops: linalg, tensor and memref](m5-structured-ops.md): *memref*, *progressive lowering*
    - [M6. Loops: affine and scf](m6-affine-and-scf.md): *`--convert-scf-to-cf`*, *block arguments from loop-carried values*
    - [M7. Bufferization](m7-bufferization.md): *memref descriptor*, *function boundary*, *calling convention*
    - [M10. MLIR for GPUs](m10-mlir-for-gpus.md): *conversion target*, *type converter*, *the same framework lowering to other dialects than llvm*
    - [M12. Designing Vortex's GPU path](m12-vortex-gpu-path.md): *descriptor cost*, *bare pointer convention*
    - [A4. Calling conventions and ABIs](../backend/a4-calling-conventions.md): *argument count*, *what a signature costs a caller*
    - [O3. SSA form: construction and destruction](../optimize/o3-ssa.md): *block arguments*, *phi*
    - [O12. Testing an optimizer](../optimize/o12-testing-optimizers.md): *expected diagnostics*, *canary*
    - [E1. The LLVM code generator pipeline](../backend/e1-llvm-codegen-pipeline.md): *what happens once LLVM IR exists*

## Sources and further reading

Read the Dialect Conversion document in full, with the dot example's `--mlir-print-ir-after-all` output open beside it; its sections on the conversion target, the adaptor and type conversion name every idea of this chapter.[^conv] Then the Target LLVM IR document, which specifies the descriptor and the calling conventions, and chapters 5 and 6 of the Toy tutorial, which build a partial and then a full lowering for a small language of their own.[^targetllvmir][^toy5][^toy6] The MLIR documentation tracks the current development version; where this chapter says what MLIR 18.1.8 did, it was checked with that version, and the 18.1.8 sources cited below are the ones to read for the details.[^src-passes][^src-dc][^src-reconcile]

[^conv]: MLIR Project, "Dialect Conversion", sections "Modes of Conversion", "Conversion Target", "Recursive Legality", "Rewrite Pattern Specification", "Conversion Patterns" (with "Remapped Operands / Adaptor", "Immediate vs. Delayed IR Modification" and "Type Safety") and "Type Conversion". <https://mlir.llvm.org/docs/DialectConversion/>
[^llvmd]: MLIR Project, "'llvm' Dialect", introduction and sections "PHI Nodes and Block Arguments" and "Built-in Type Compatibility". <https://mlir.llvm.org/docs/Dialects/LLVM/>
[^targetllvmir]: MLIR Project, "LLVM IR Target", sections "Conversion to the LLVM Dialect", "Index Type", "Ranked MemRef Types", "Function Types", "Bare Pointer Calling Convention for Ranked MemRef", "C-compatible wrapper emission" and "Translation to LLVM IR". <https://mlir.llvm.org/docs/TargetLLVMIR/>
[^toy5]: MLIR Project, "Chapter 5: Partial Lowering to Lower-Level Dialects for Optimization", Toy tutorial, sections "Conversion Target" and "Partial Lowering". <https://mlir.llvm.org/docs/Tutorials/Toy/Ch-5/>
[^toy6]: MLIR Project, "Chapter 6: Lowering to LLVM and CodeGeneration", Toy tutorial, sections "Lowering to LLVM", "Conversion Target" and "Full Lowering". <https://mlir.llvm.org/docs/Tutorials/Toy/Ch-6/>
[^passes]: MLIR Project, "Passes", entries `-convert-to-llvm`, `-convert-scf-to-cf`, `-finalize-memref-to-llvm` and `-reconcile-unrealized-casts`. <https://mlir.llvm.org/docs/Passes/>
[^src-passes]: LLVM Project, tag llvmorg-18.1.8, `mlir/lib/Conversion/`: `LLVMCommon/ConversionTarget.cpp` (the target of the LLVM lowering passes), `ArithToLLVM/ArithToLLVM.cpp`, `ControlFlowToLLVM/ControlFlowToLLVM.cpp` (`BranchOpLowering`), `ConvertToLLVM/ConvertToLLVMPass.cpp`, `MemRefToLLVM/MemRefToLLVM.cpp`, `FuncToLLVM/FuncToLLVM.cpp` (the pass's `runOnOperation`, with the comment on issue 70982) and `SCFToControlFlow/SCFToControlFlow.cpp`. <https://github.com/llvm/llvm-project/tree/llvmorg-18.1.8/mlir/lib/Conversion>
[^src-dc]: LLVM Project, tag llvmorg-18.1.8, `mlir/lib/Transforms/Utils/DialectConversion.cpp`, `OperationLegalizer::legalize` and `OperationConverter::convert`. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/mlir/lib/Transforms/Utils/DialectConversion.cpp>
[^src-reconcile]: LLVM Project, tag llvmorg-18.1.8, `mlir/lib/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.cpp`, the pattern `UnrealizedConversionCastPassthrough` and the pass's target. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/mlir/lib/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.cpp>
[^llvm-fmf]: LLVM Project, "LLVM Language Reference Manual", section "Fast-Math Flags", entry `contract`. <https://llvm.org/docs/LangRef.html#fast-math-flags>
