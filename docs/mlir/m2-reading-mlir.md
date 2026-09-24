# M2. Reading MLIR

<p class="page-intro">Every instruction, loop and function in MLIR, in every dialect, is the same kind of object: an operation with operands, results, attributes, regions and a source location. This chapter teaches you to take that object apart with mlir-opt, so that you can read any dialect you meet, and shows where Vortex's fixed shapes, strict floating-point rules and mutable outputs would have to live in it.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 35 minutes · Builds on: [M1. Why MLIR](m1-why-mlir.md), [Stage 6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#ssa-if-you-use-llvm)</p>

???+ remember "Before you start, remember"

    ??? question "What does SSA promise, and what does a phi do?"

        In SSA form every named value is assigned exactly once, so a variable that changes becomes a series of versions. Where two paths meet, a phi picks which version to use, according to the path that was taken.

        Introduced in [Stage 6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#ssa-if-you-use-llvm).

    ??? question "What is a dialect?"

        A named family of operations, types and attributes, such as `arith` for arithmetic or `scf` for structured loops. One MLIR module can mix several, and lowering replaces the operations of one dialect with those of a lower one, a step at a time.

        Introduced in [M1. Why MLIR](m1-why-mlir.md).

    ??? question "In what order are the elements of a `[f32; 2, 3]` array stored?"

        Row after row, the last index varying fastest: `[0, 0]`, `[0, 1]`, `[0, 2]`, `[1, 0]`, `[1, 1]`, `[1, 2]`.

        Introduced in [Arrays and shapes, decision 43](../decisions/arrays.md#d43).

    ??? question "May a Vortex compiler turn `sum += a[row, k] * b[k, column]` into one fused multiply-add?"

        No. Each `f32` operation is one IEEE 754 operation, rounded to nearest with ties to even, and a compiler must not contract, reassociate or reorder them. The product is rounded, then the sum.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

    ??? question "What does passing `&mut c` promise about the other arguments of the same call?"

        That none of them is `c`. A variable lent as `&mut` may appear in no other argument of the call, so inside the callee the output shares storage with no input.

        Introduced in [References and mutability, decision 25](../decisions/references.md#d25).

!!! goals "In this chapter"

    - Read any MLIR operation by its parts: results, name, operands, successors, properties, regions, attributes, type and location.
    - Explain how operations, regions and blocks nest, which values an operation may use, and how block arguments replace phis.
    - Distinguish types, attributes and properties, and say where Vortex's shapes, floating-point rules and `&mut` outputs could and could not be recorded.
    - Use `mlir-opt` to print the generic form, show locations and verify a file, and predict which rule a broken file breaks.

## One function, two spellings

MLIR is hard to read at first, for a reason that turns out to help. Its code is made of **operations**, MLIR's unit of meaning: an addition, a loop and a whole function are each one operation. Each operation may define its own syntax: the parser for an operation belongs to its dialect, so operations from different dialects look different from one another.[^kun] Underneath, every operation has the same small set of parts, and `mlir-opt`, the tool that parses, checks, transforms and prints MLIR, can print any file in a form that shows those parts literally. This chapter uses that form as its reading lens.

Start with a function small enough to hold in your head. It evaluates the polynomial c0·x³ + c1·x² + c2·x + c3 by Horner's rule, as ((c0·x + c1)·x + c2)·x + c3, reading the four coefficients from memory:

--8<-- "includes/examples/mlir/m2-reading-mlir/horner.mlir.md"

The file is written in **custom form**, the syntax an operation defines for itself so that people can read it: `arith.mulf %acc, %x : f32` says "multiply these two `f32` values" with little punctuation. The output is the same function in **generic form**, a syntax that every operation also has, whatever it does and whichever dialect defines it. The example runs `mlir-opt --mlir-print-op-generic`; in MLIR 18.1.8 that option is listed only by `mlir-opt --help-hidden` (checked on the owner's machine on 2026-09-24). Both forms print the same in-memory IR, and MLIR's designers made both "fully round-trippable": each parses back to the same IR.[^mlir-paper]

Put one line of each side by side:

```mlir
%scaled = arith.mulf %acc, %x : f32
%6 = "arith.mulf"(%arg3, %arg1) <{fastmath = #arith.fastmath<none>}> : (f32, f32) -> f32
```

Read the generic line from left to right.

- `%6 =` names the **result**, the value this operation defines. It is an SSA value: defined once, never changed. The name `%scaled` has gone because names in a file exist for the reader only; they are "not persisted as part of the IR", and the printer numbers values instead.[^langref]
- `"arith.mulf"` is the operation's name, a quoted string made of a **dialect** namespace (`arith`), a dot, and the operation within that dialect (`mulf`, floating-point multiply). The Toy tutorial reads its own example, `toy.transpose`, as "the transpose operation in the toy dialect";[^toy2] read this one as the `mulf` operation in the `arith` dialect.
- `(%arg3, %arg1)` are the **operands**, the values the operation uses. `%acc` printed as `%arg3` because no operation defines it; the section on regions shows where it comes from.
- `<{fastmath = #arith.fastmath<none>}>` is a **property**, a constant stored inside the operation as part of what it means. This one says the multiply carries no fast-math permissions. The custom form left it out because `none` is the default.
- `: (f32, f32) -> f32` is the operation's type, written as a function type: the operand types, an arrow, the result types. The custom form wrote `f32` once because `arith.mulf` requires its two operands and its result to have the same type.[^arith]

That is the idea behind the generic form. Every operation, from an addition to a whole function, has the same fixed set of parts, and the generic printer writes each part the operation has, always in the same order. The MLIR paper states the design principle: a handful of concepts, namely types, operations and attributes, should be used "to express everything else".[^mlir-paper]

Five dialects appear in this one small function: `builtin` for the module around everything, `func` for the function and its return, `arith` for constants and arithmetic, `memref` for loads from memory, and `scf` for the loop. `mlir-opt` wrapped the function in a `builtin.module`, the usual top-level container, because the input had none.[^kun] The input wrote `return`, not `func.return`: inside a `func.func`, operations of the `func` dialect may drop their prefix, because the function names `func` as the default dialect of its body.[^asm]

In the loop, `"scf.for"(%1, %2, %1, %3)`, the value `%1` appears twice: the constant 1 is both the lower bound and the step. The `scf.for` documentation gives the order: lower bound, upper bound, step, then the starting value of each **loop-carried value**, a value that one iteration hands to the next, like the running total `%acc`.[^scf]

## The parts of an operation

The Horner function used six of an operation's parts: results, names, operands, properties, regions and types. The Language Reference, MLIR's specification, lists all of them.[^langref] An operation has zero or more results; a name; zero or more operands; zero or more **successors**, the blocks it may transfer control to; **properties**; zero or more **regions**, code nested inside it; a dictionary of other **attributes**; a type; and a **location**, the place in some source text it came from.

The generic form prints them in that order, each with its own brackets, so you can tell them apart without knowing anything about the operation. An operation with two results names them as one pack, `%r:2 = ...`, and each use picks one out as `%r#0` or `%r#1`.[^langref]

<figure class="vx-figure">
<svg viewBox="0 0 760 372" role="img" aria-label="The nine slots of a generic operation, in printing order" aria-describedby="m2-f1-desc">
<title id="m2-f1-title">The nine slots of a generic operation, in printing order</title>
<desc id="m2-f1-desc">Nine boxes in three rows of three, read left to right and top to bottom, in the order the generic printer writes an operation's parts. Each box holds an example from this chapter's files, with a name and a short explanation underneath. One: results, "%4 =", the values it defines. Two: name, the quoted string "scf.for", made of a dialect, a dot and the operation. Three: operands, "(%1, %2, %1, %3)", the values it uses. Four: successors, "&#91;^bb1, ^bb2&#93;", the blocks a terminator may jump to. Five: properties, "<{callee = @note_clamped}>", constants inherent to the operation. Six: regions, "({ ^bb0(...): ... })", code nested inside it. Seven: attributes, "{linalg.memoized_indexing_maps = ...}", other constants, named by a dialect. Eight: type, ": (f32, f32) -> f32", operand types then result types. Nine: location, loc("shop.calc":2:19), where it came from. A highlight moves from slot to slot in order.</desc>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 9">
<rect class="vx-box" x="20" y="30" width="220" height="40" rx="4"/>
<text class="vx-mono" x="130" y="55" text-anchor="middle">%4 =</text>
<text class="vx-text" x="20" y="92">1. results</text>
<text class="vx-text-muted" x="20" y="110">the values it defines</text>
<text class="vx-text-muted" x="20" y="126">horner: the loop's result</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 9">
<rect class="vx-box" x="270" y="30" width="220" height="40" rx="4"/>
<text class="vx-mono" x="380" y="55" text-anchor="middle">"scf.for"</text>
<text class="vx-text" x="270" y="92">2. name</text>
<text class="vx-text-muted" x="270" y="110">dialect, a dot, the operation</text>
<text class="vx-text-muted" x="270" y="126">horner: the loop</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 9">
<rect class="vx-box" x="520" y="30" width="220" height="40" rx="4"/>
<text class="vx-mono" x="630" y="55" text-anchor="middle">(%1, %2, %1, %3)</text>
<text class="vx-text" x="520" y="92">3. operands</text>
<text class="vx-text-muted" x="520" y="110">the values it uses</text>
<text class="vx-text-muted" x="520" y="126">horner: bounds, step, start</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 9">
<rect class="vx-box" x="20" y="146" width="220" height="40" rx="4"/>
<text class="vx-mono" x="130" y="171" text-anchor="middle">&#91;^bb1, ^bb2&#93;</text>
<text class="vx-text" x="20" y="208">4. successors</text>
<text class="vx-text-muted" x="20" y="226">blocks a terminator may jump to</text>
<text class="vx-text-muted" x="20" y="242">branches: cf.cond_br</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 9">
<rect class="vx-box" x="270" y="146" width="220" height="40" rx="4"/>
<text class="vx-mono" x="380" y="171" text-anchor="middle">&lt;{callee = @note_clamped}&gt;</text>
<text class="vx-text" x="270" y="208">5. properties</text>
<text class="vx-text-muted" x="270" y="226">constants inherent to the operation</text>
<text class="vx-text-muted" x="270" y="242">branches: func.call</text>
</g>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 9">
<rect class="vx-box" x="520" y="146" width="220" height="40" rx="4"/>
<text class="vx-mono" x="630" y="171" text-anchor="middle">({ ^bb0(...): ... })</text>
<text class="vx-text" x="520" y="208">6. regions</text>
<text class="vx-text-muted" x="520" y="226">code nested inside it</text>
<text class="vx-text-muted" x="520" y="242">horner: the loop body</text>
</g>
<g class="vx-seq" style="--vx-i: 6; --vx-n: 9">
<rect class="vx-box" x="20" y="262" width="330" height="40" rx="4"/>
<text class="vx-mono" x="185" y="287" text-anchor="middle">{linalg.memoized_indexing_maps = ...}</text>
<text class="vx-text" x="20" y="324">7. attributes</text>
<text class="vx-text-muted" x="20" y="342">other constants, named by a dialect</text>
<text class="vx-text-muted" x="20" y="358">named_matmul: linalg.matmul</text>
</g>
<g class="vx-seq" style="--vx-i: 7; --vx-n: 9">
<rect class="vx-box" x="370" y="262" width="165" height="40" rx="4"/>
<text class="vx-mono" x="452" y="287" text-anchor="middle">: (f32, f32) -&gt; f32</text>
<text class="vx-text" x="370" y="324">8. type</text>
<text class="vx-text-muted" x="370" y="342">operand types, then results</text>
<text class="vx-text-muted" x="370" y="358">horner: arith.mulf</text>
</g>
<g class="vx-seq" style="--vx-i: 8; --vx-n: 9">
<rect class="vx-box" x="555" y="262" width="185" height="40" rx="4"/>
<text class="vx-mono" x="647" y="287" text-anchor="middle">loc("shop.calc":2:19)</text>
<text class="vx-text" x="555" y="324">9. location</text>
<text class="vx-text-muted" x="555" y="342">where it came from</text>
<text class="vx-text-muted" x="555" y="358">locations: arith.mulf</text>
</g>
</svg>
<figcaption>Figure 1. The nine slots of the generic form, in the order the printer writes them, each filled with an example from this chapter's files. No operation in these files uses all nine; an empty slot is not printed. The brackets tell the slots apart: round brackets hold operands, square brackets successors, <code>&lt;{ }&gt;</code> properties, <code>({ })</code> regions and plain <code>{ }</code> other attributes. The location is printed only when you ask for it.</figcaption>
</figure>

Properties and attributes both hold **constant data**, values known when the IR is built and never computed at run time. The LangRef describes attributes as constant data for places "where a variable is never allowed"; its example is the predicate of an integer comparison, which says which comparison to make.[^langref] Which of the two slots a constant goes in depends on who owns its meaning, a question [a later section](#attributes-and-properties-hold-the-constants) answers.

Custom forms are shorter because they leave out what can be inferred or is the default: fast-math flags of `none`, types repeated across operands, and, for an `scf.for` without loop-carried values, even the terminator at the end of its body, which the parser inserts for you.[^scf] When a line of custom syntax puzzles you, print the generic form and sort its parts into Figure 1's slots.

??? check "In `%4 = "scf.for"(%1, %2, %1, %3) ({ ^bb0(%arg2: index, %arg3: f32): ... }) : (index, index, index, f32) -> f32`, which operand is the upper bound, and where does the loop variable come from?"

    `%2`, the constant 4. The operands are the lower bound, the upper bound and the step, then `%3`, the starting value of the loop-carried value. The loop variable is not an operand at all: it is `%arg2`, the first argument of the block inside the loop's region, and `%arg3` is the running value.

## Regions, blocks and what a value can see

The generic output of the Horner example nests. The function is an operation whose body sits inside `({ ... })`, and the loop within that body is another operation with its own `({ ... })`. The round-and-curly brackets hold regions. MLIR is built from three kinds of container that alternate all the way down: an operation holds regions, a region holds blocks, and a block holds operations.[^irstruct] Figure 2 draws the Horner function that way.

<figure class="vx-figure">
<svg viewBox="0 0 760 452" role="img" aria-label="The Horner function as operations, regions and blocks nested inside each other" aria-describedby="m2-f2-desc">
<title id="m2-f2-title">The Horner function as nested operations, regions and blocks</title>
<desc id="m2-f2-desc">Nested boxes. The outermost box is the builtin.module operation, whose region is one graph-region block with no terminator. Inside it is the func.func @horner operation, which is isolated from above. Its region holds one entry block whose arguments, %arg0 of type memref of 4 f32 and %arg1 of type f32, are the function's parameters. That block holds three constants, a memref.load that produces %3, the scf.for operation that produces %4, and func.return %4, its terminator. The scf.for box holds one region with one block whose arguments are %arg2, the loop variable, and %arg3, the running value. That block holds a memref.load, arith.mulf %arg3 times %arg1, arith.addf, and scf.yield %7, its terminator. The two occurrences of %arg1, where the function's block defines it and where the multiply inside the loop uses it, are highlighted together: a region may use values from the regions around it.</desc>
<rect class="vx-box-strong" x="20" y="20" width="720" height="412" rx="6"/>
<text class="vx-mono" x="36" y="44">builtin.module</text>
<text class="vx-text-muted" x="164" y="44">operation · its region: one graph-region block, no terminator</text>
<rect class="vx-box-strong" x="40" y="60" width="680" height="356" rx="6"/>
<text class="vx-mono" x="56" y="84">func.func @horner</text>
<text class="vx-text-muted" x="204" y="84">operation · isolated from above: nothing inside may use an outside value</text>
<rect class="vx-box" x="60" y="100" width="640" height="300" rx="4"/>
<rect class="vx-box-accent vx-pulse" x="283" y="110" width="44" height="20" rx="3"/>
<text class="vx-mono" x="76" y="124">^bb0(%arg0: memref&lt;4xf32&gt;, %arg1: f32)</text>
<text class="vx-text-muted" x="386" y="124">entry block: its arguments are the parameters</text>
<text class="vx-mono" x="84" y="154">%0 = arith.constant 0 : index</text>
<text class="vx-text-muted" x="340" y="154">%1 and %2 hold 1 and 4 the same way</text>
<text class="vx-mono" x="84" y="176">%3 = memref.load %arg0[%0]</text>
<rect class="vx-box-strong" x="76" y="190" width="608" height="176" rx="6"/>
<text class="vx-mono" x="92" y="212">%4 = "scf.for"(%1, %2, %1, %3)</text>
<text class="vx-text-muted" x="340" y="212">operation with one region: the loop body</text>
<rect class="vx-box" x="96" y="226" width="568" height="128" rx="4"/>
<text class="vx-mono" x="112" y="248">^bb0(%arg2: index, %arg3: f32)</text>
<text class="vx-text-muted" x="364" y="248">loop variable, then the running value</text>
<text class="vx-mono" x="120" y="274">%5 = memref.load %arg0[%arg2]</text>
<rect class="vx-box-accent vx-pulse" x="297" y="283" width="44" height="20" rx="3"/>
<text class="vx-mono" x="120" y="297">%6 = arith.mulf %arg3, %arg1</text>
<text class="vx-text-muted" x="360" y="297">uses %arg1 from the function's block</text>
<text class="vx-mono" x="120" y="320">%7 = arith.addf %6, %5</text>
<text class="vx-mono" x="120" y="342">scf.yield %7</text>
<text class="vx-text-muted" x="236" y="342">terminator: hands %7 to the next iteration</text>
<text class="vx-mono" x="84" y="388">func.return %4</text>
<text class="vx-text-muted" x="208" y="388">terminator: leaves the function</text>
</svg>
<figcaption>Figure 2. The Horner function drawn as containers. Heavy boxes are operations and light boxes are blocks; each region here holds a single block, so a region is drawn as the block it contains. Value names are those of the generic output, and operations are shortened. The two highlighted <code>%arg1</code>s are one value: defined as an argument of the function's block, used inside the loop's block.</figcaption>
</figure>

A **region** is an ordered list of blocks that belongs to an operation, and that operation decides what the region means.[^langref] The function's region is its body; the loop's region is the loop body, run once per iteration. A region has no name, and nothing outside its operation can branch into it.

A **block** is a list of operations. In a function or a loop body they run from top to bottom, and the last one is a **terminator**, an operation that says where control goes next.[^langref] `scf.yield` ends the loop body and hands the running value to the next iteration; `func.return` ends the function.

A block may take **block arguments**, values it receives when control enters it. The arguments of a region's first block, its **entry block**, are the region's own arguments, and the enclosing operation decides what they stand for.[^langref] For `func.func` they are the function's parameters, which is why `%coeffs` and `%x` printed as `%arg0` and `%arg1`.[^func] For `scf.for` they are the loop variable followed by one argument per loop-carried value, which is why `%i` and `%acc` became `%arg2` and `%arg3`.[^scf]

A loop-carried value such as `%acc` is how SSA expresses a variable that changes on every iteration. Each iteration receives the previous value as a block argument and passes the next one to `scf.yield`; after the last iteration, the loop's result `%4` holds the final value. Nothing is assigned twice.

**What a value can see.** A value defined inside a region never escapes it. The other direction is allowed by default: an operation inside a region may use a value defined outside, wherever the enclosing operation itself could have used it.[^langref] That is how the loop body multiplies by `%arg1`, the function's `x`.

An operation can refuse this by being **isolated from above**, meaning nothing inside it may use a value defined outside it. `func.func` is isolated,[^func] so everything a function body uses is defined inside the function; another function it reaches by name, as a later section shows. The reason is practical as well as tidy: the MLIR paper points out that isolated operations can be compiled in parallel, because no chain from a use to its definition crosses the boundary.[^mlir-paper]

Inside one region, the familiar SSA rule applies. A definition **dominates** a use when every path from the region's entry to the use passes through the definition, and a value may be used only where its definition dominates the use ([O2](../optimize/o2-cfg-and-dominance.md) builds dominance properly). That rule belongs to **SSACFG regions**, the usual kind, where operations run in order and blocks form a control-flow graph. MLIR also has **graph regions**, a single block whose operations have no order and may use each other's results freely, cycles included.[^langref] The module's body is one: a graph region holding one block with no terminator.[^builtin] Functions and loops have SSACFG regions.

A Vortex function body, a `for` body and each branch of an `if` are nested scopes too, and a Vortex compiler that lowered to MLIR would find its loop bodies already shaped like regions. The match is not exact.

A Vortex `let mut sum` that changes inside a loop has no direct SSA counterpart. The two usual translations are a loop-carried value, like `%acc`, and a slot in memory that the loop body loads and stores, such as one allocated on the stack with `memref.alloca`.[^memref] The second is the shortcut [stage 6](../compiler/guide/stage-6-first-machine-code.md#ssa-if-you-use-llvm) described for LLVM. MLIR has a `--mem2reg` pass that turns such slots into values and leaves the IR unchanged when it cannot.[^passes] In MLIR 18.1.8 it promoted a slot in straight-line code but left alone one that an `scf.for` body loads and stores (checked on 2026-09-24). The exercise at the end asks you to choose.

## Blocks that take arguments

Loops are structured: `scf.for` owns its body, and control cannot jump into it from outside. Branches between blocks need one more idea. This function returns `%x`, raised to `%floor` when it is below, and calls a logging function when it raises:

--8<-- "includes/examples/mlir/m2-reading-mlir/branches.mlir.md"

The join block `^done` declares a parameter, `%result: f32`, and every branch into it supplies an argument. `cf.cond_br` passes `%x` when the comparison is false, and `cf.br` in `^raise` passes `%floor`. In LLVM IR, the IR stage 6 described, the join block would instead begin with a phi listing one incoming value per predecessor. Figure 3 draws both.

<figure class="vx-figure">
<svg viewBox="0 0 760 372" role="img" aria-label="The same branching function with block arguments in MLIR and with a phi in LLVM IR" aria-describedby="m2-f3-desc">
<title id="m2-f3-title">Block arguments in MLIR against a phi in LLVM IR</title>
<desc id="m2-f3-desc">Two control-flow graphs of the same function side by side. On the left, MLIR: the entry block computes %below and ends with cf.cond_br; its true edge goes to ^raise, which calls @note_clamped and branches to ^done passing %floor; its false edge goes straight to ^done passing %x. ^done declares one argument, %result, and returns it. The two edges into ^done are animated to show a value travelling along each one. On the right, LLVM IR: the entry block ends with br i1 %below to label %raise or label %done; raise calls the function and branches to done; done begins with %result = phi float [%x, %entry], [%floor, %raise], which is highlighted, then returns %result. On the right the edges carry nothing: the join block chooses by predecessor.</desc>
<text class="vx-text" x="20" y="24">MLIR: each branch passes a value</text>
<text class="vx-text" x="400" y="24">LLVM IR: the join block picks one</text>
<rect class="vx-box" x="60" y="44" width="250" height="66" rx="4"/>
<text class="vx-mono" x="72" y="64">%below = arith.cmpf ...</text>
<text class="vx-mono" x="72" y="82">cf.cond_br %below,</text>
<text class="vx-mono" x="88" y="100">^raise, ^done(%x : f32)</text>
<rect class="vx-box" x="20" y="150" width="226" height="66" rx="4"/>
<text class="vx-mono" x="32" y="170">^raise:</text>
<text class="vx-mono" x="32" y="188">call @note_clamped(%x)</text>
<text class="vx-mono" x="32" y="206">cf.br ^done(%floor : f32)</text>
<rect class="vx-box" x="60" y="270" width="250" height="66" rx="4"/>
<text class="vx-mono" x="72" y="290">^done(%result: f32):</text>
<text class="vx-mono" x="72" y="308">return %result : f32</text>
<line class="vx-line" x1="133" y1="110" x2="133" y2="142"/>
<polygon class="vx-arrowhead" points="128,142 133,150 138,142"/>
<text class="vx-text-muted" x="140" y="134">true</text>
<path class="vx-flow" d="M133,216 L133,262"/>
<polygon class="vx-arrowhead" points="128,262 133,270 138,262"/>
<text class="vx-text-accent" x="141" y="246">%floor</text>
<path class="vx-flow" d="M290,110 L290,262"/>
<polygon class="vx-arrowhead" points="285,262 290,270 295,262"/>
<text class="vx-text-accent" x="298" y="186">%x</text>
<text class="vx-text-muted" x="298" y="202">false</text>
<line class="vx-line" x1="380" y1="36" x2="380" y2="356"/>
<rect class="vx-box" x="440" y="44" width="250" height="66" rx="4"/>
<text class="vx-mono" x="452" y="64">entry: %below = fcmp olt ...</text>
<text class="vx-mono" x="452" y="82">br i1 %below,</text>
<text class="vx-mono" x="468" y="100">label %raise, label %done</text>
<rect class="vx-box" x="400" y="150" width="226" height="66" rx="4"/>
<text class="vx-mono" x="412" y="170">raise:</text>
<text class="vx-mono" x="412" y="188">call @note_clamped(...)</text>
<text class="vx-mono" x="412" y="206">br label %done</text>
<rect class="vx-box" x="440" y="270" width="300" height="84" rx="4"/>
<rect class="vx-box-accent vx-pulse" x="446" y="294" width="288" height="40" rx="3"/>
<text class="vx-mono" x="452" y="290">done:</text>
<text class="vx-mono" x="452" y="308">%result = phi float</text>
<text class="vx-mono" x="468" y="326">[%x, %entry], [%floor, %raise]</text>
<text class="vx-mono" x="452" y="346">ret float %result</text>
<line class="vx-line" x1="513" y1="110" x2="513" y2="142"/>
<polygon class="vx-arrowhead" points="508,142 513,150 518,142"/>
<text class="vx-text-muted" x="520" y="134">true</text>
<line class="vx-line" x1="513" y1="216" x2="513" y2="262"/>
<polygon class="vx-arrowhead" points="508,262 513,270 518,262"/>
<line class="vx-line" x1="670" y1="110" x2="670" y2="262"/>
<polygon class="vx-arrowhead" points="665,262 670,270 675,262"/>
<text class="vx-text-muted" x="677" y="202">false</text>
</svg>
<figcaption>Figure 3. One function, two ways to merge values. In MLIR (left) the join block <code>^done</code> takes an argument and each branch passes a value along its edge. In LLVM IR (right) the edges carry nothing, and the phi at the top of <code>done</code> lists which value to take from which predecessor. Operand types are left out of some lines to keep them short.</figcaption>
</figure>

The two forms say the same thing. The MLIR rationale calls block arguments "representationally identical" to phis, then lists what they remove.[^rationale] Phis must sit at the top of their block, so passes keep skipping over them; block arguments have no such rule. Function parameters stop being a separate kind of value, because they are the entry block's arguments. And LLVM's rule that all the phis at the top of a block take effect at once, a known source of bugs when SSA is turned back into copies ([O3](../optimize/o3-ssa.md)), disappears.

A value that does not depend on the path taken, such as `%x` inside `^raise`, needs no argument at all: any block that its definition dominates can use it directly.[^langref]

The generic form of the branch shows two slots the Horner function did not use:

```mlir
"cf.cond_br"(%0, %arg0)[^bb1, ^bb2] <{operandSegmentSizes = array<i32: 1, 0, 1>}> : (i1, f32) -> ()
```

The square brackets hold the successors, renamed `^bb1` and `^bb2` by the printer, which also adds `// pred:` comments naming each block's predecessors. All the operands sit in one flat list, although they mean three different things: the condition, the values for the first successor and the values for the second. The property `operandSegmentSizes` says how to cut the list: one condition, no values for `^bb1`, one value for `^bb2`. An operation with more than one group of operands whose length can vary needs such a property, unless it declares that all its groups have the same length.[^ods]

The count and types in each group must match the target block's arguments,[^cf] and the verifier checks them: given an `i1` where the target block expects an `f32`, MLIR 18.1.8 reports a type mismatch for that successor's argument (checked on 2026-09-24).

The call shows a different way to refer to something. `"func.call"(%arg0) <{callee = @note_clamped}>` passes `%arg0` as an operand but names the function in a property, `@note_clamped`, rather than as a value. A function is a **symbol**: an operation with a name that is unique in the enclosing **symbol table**, here the module, and that other operations refer to with `@name`.[^symbols] Symbols are how code crosses isolation boundaries. A function cannot use another function's values, but it can name another function.

The declaration `func.func private @note_clamped(f32)` is a function operation whose region is empty, printed as `({ })` in the generic output. It is marked `private` because a symbol that is only declared may not be public;[^symbols] MLIR 18.1.8 rejects `func.func @note_clamped(f32)` with "symbol declaration cannot have public visibility" (checked on 2026-09-24).

??? check "If both branches of a `cf.cond_br` passed one value, as in `cf.cond_br %cond, ^then(%a : f32), ^else(%b : f32)`, what would `operandSegmentSizes` be, and what would the operand list look like?"

    `array<i32: 1, 1, 1>`, with the operands `(%cond, %a, %b)`: one condition, one value for the first successor and one for the second.

## Types say what a value is

Every value has a type, and the type system is open: any dialect may add types, written `!dialect.name<...>`, beside the **builtin** types that every dialect can use.[^langref] This chapter's files use a handful of builtin types, and the ideas behind them return in every later chapter.

**`index`** is an integer as wide as the target's machine word, used for subscripts and sizes.[^builtin] Memory is indexed with `index` values, which is why the Horner loop counts with `index` constants.

**Integers are signless.** `i32` means 32 bits and nothing about sign; each operation decides how to read the bits. `arith.addi` does the same thing for signed and unsigned values, while division comes as `arith.divsi` and `arith.divui`. LLVM IR works the same way, and the MLIR rationale explains why: adding two signed bytes and adding two unsigned bytes is one computation, and giving them different types only added casts that did nothing at the machine level.[^rationale]

For Vortex, this matters on the way down. `i32` and `u32` are different Vortex types, but both would become MLIR `i32`, and the difference would move into every operation whose result depends on it: division, remainder, right shift, less-than comparisons, conversions, and the overflow checks that [decision 34](../decisions/diagnostics.md#d34) requires on `+`, `-` and `*`. A lowering that picks the signed operation for a `u32` passes the verifier and gives wrong answers once a value reaches 2³¹.

**`memref<4xf32>`** is a **memref**, "a reference to a region of memory", with a shape and an element type.[^builtin] With no layout written, a memref gets the default layout, which the documentation defines in row-major order,[^builtin] the order [decision 43](../decisions/arrays.md#d43) fixes for Vortex arrays. A memref is a reference, so a function that stores through a memref argument changes memory its caller can see, which is what a `&mut` parameter does.

The documentation also says the buffer behind a memref can be aliased:[^builtin] two memref arguments may point at the same memory, and nothing in their types says otherwise. Vortex promises more. [Decision 25](../decisions/references.md#d25) forbids passing a variable both as `&mut` and as another argument of the same call, so a Vortex function knows its output overlaps none of its inputs. A memref type cannot state that promise; a compiler that wants to keep it has to carry it some other way.

**`tensor<4xf32>`**, which this chapter's files do not use, is the value counterpart: data with a shape but no layout you can control and no pointer you can take.[^builtin] It behaves like a Vortex array passed by value, which decision 25 copies, and [M7](m7-bufferization.md) is about turning tensors into memrefs.

Static shapes are part of these types. `memref<8x16xf32>` and `memref<16x4xf32>` are different types, as `[f32; 8, 16]` and `[f32; 16, 4]` are different Vortex types. A `?` in place of a size marks a dimension known only at run time.[^builtin] Vortex v0.1 never needs one, since every extent is a constant expression ([decision 11](../decisions/arrays.md#d11)) and a function handles [one shape](../compiler/guide/stage-10-matrix-multiplication.md#one-function-per-shape).

## Attributes and properties hold the constants

The branch example compares with `arith.cmpf olt, %x, %floor`, "ordered less than". In generic form the comparison became:

```mlir
%0 = "arith.cmpf"(%arg0, %arg1) <{fastmath = #arith.fastmath<none>, predicate = 4 : i64}> : (f32, f32) -> i1
```

`olt` turned into `predicate = 4 : i64`. The kind of comparison is not an operand, because it is never computed at run time: it is an **attribute**, a constant attached to the operation. This one is an integer attribute, which carries a type of its own, `i64` here.[^builtin] The rationale explains the number. The comparison kind is stored as an integer, and the custom form maps readable words such as `olt` onto it;[^rationale] the arith documentation lists the table, from `false` at 0 through `olt` at 4 to `true` at 15.[^arith]

An operation keeps its attributes in two places, and the generic form's brackets show which.[^langref]

- **Properties**, printed inside `<{ }>`, hold the attributes that belong to the operation's definition, the ones the operation checks itself: `predicate` for `arith.cmpf`, `callee` for `func.call`, `sym_name` and `function_type` for `func.func`.
- **Discardable attributes**, printed inside a plain `{ }`, have names that start with a dialect prefix, and that dialect, not the operation, gives them their meaning. The LangRef's example is `gpu.container_module`; the matrix example below prints one named `linalg.memoized_indexing_maps`.

The `fastmath` property deserves a Vortex reading. Every floating-point `arith` operation has one, and its value is a set of flags drawn from `none`, `reassoc`, `nnan`, `ninf`, `nsz`, `arcp`, `contract`, `afn` and `fast`.[^arith] The flags have the names of LLVM IR's fast-math flags, where each is a permission a later pass may use: `contract` allows a multiply and an add to be fused into one fused multiply-add, and `reassoc` allows reassociation, such as regrouping a sum.[^llvm-fmf] [Decision 56](../decisions/numbers.md#d56) forbids both and every other relaxation, so a Vortex kernel in MLIR has exactly one allowed value: `none`.

The verifier will not enforce that for you. MLIR 18.1.8 accepts `arith.addf %a, %b fastmath<contract> : f32` and records the permission as `<{fastmath = #arith.fastmath<contract>}>` (checked on 2026-09-24): the flag is legal MLIR, and Vortex's rule is not MLIR's rule. Whether a module keeps decision 56 is something you check by reading every floating-point operation's `fastmath` property, or by a test that reads them for you.

Decision 56 also fixes the rounding, to nearest with ties to even, and MLIR does not promise that either. According to the current arith documentation, the dialect uses that rounding for its own work, such as folding constants, but when an operation names no rounding mode, how it rounds at run time is up to the target's back end.[^arith] Newer MLIR can write the mode on the operation, as in `arith.addf %b, %c to_nearest_even : f64`, a syntax that 18.1.8 rejects.

??? check "In `"linalg.matmul"(%arg0, %arg1, %arg2) <{operandSegmentSizes = array<i32: 2, 1>}> ({ ... }) {linalg.memoized_indexing_maps = [#map, #map1, #map2]}`, which entry is a property and which is a discardable attribute? How can you tell without knowing linalg?"

    `operandSegmentSizes` is a property: it sits inside `<{ }>`. `linalg.memoized_indexing_maps` is a discardable attribute: it sits in the plain `{ }` after the region, and its name starts with a dialect prefix. The brackets alone tell you, whatever the operation.

## A named operation hides a region

Matrix multiplication, which Vortex's [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for) writes as three loops, is a single operation in MLIR's **linalg** dialect, the family of structured operations that [M5](m5-structured-ops.md) covers. Here are both, at the same sizes:

```vortex
// items: valid
fn multiply(a: &[f32; 8, 16], b: &[f32; 16, 4], c: &mut [f32; 8, 4]) {
    for row in 0..8 {
        for column in 0..4 {
            let mut sum: f32 = 0.0;
            for k in 0..16 {
                sum += a[row, k] * b[k, column];
            }
            c[row, column] = sum;
        }
    }
}
```

--8<-- "includes/examples/mlir/m2-reading-mlir/named_matmul.mlir.md"

`linalg.matmul` is a **named operation**: a common computation with a name of its own. The linalg documentation says its named operations follow the interface of `linalg.generic`, the general form that M5 reads in full.[^linalg] The generic printout shows what that means. Behind the one-line custom form is a region whose block receives three `f32` scalars, one element from each operand, multiplies the first two, adds the third, and yields the sum.

The three lines at the top of the output say which elements. Each defines an **attribute alias**, a short name such as `#map` that the printer writes in place of a long attribute value.[^langref] Each value is an **affine map**, a function from indices to indices ([M6](m6-affine-and-scf.md) gives the general rule). For each point `(d0, d1, d2)` of the computation, the first input supplies element `(d0, d2)`, the second `(d2, d1)` and the output `(d0, d1)`. With `d0`, `d1` and `d2` standing for `row`, `column` and `k`, those are `a[row, k]`, `b[k, column]` and `c[row, column]`.

Four things in this output connect to Vortex.

**The output is an operand.** `linalg.matmul` has no result. It writes into `%c`, listed after `outs`, and its generic form counts two inputs and one output (`operandSegmentSizes = array<i32: 2, 1>`). That is the shape of Vortex's `c: &mut [f32; 8, 4]`: the caller provides the storage and the function fills it.

**The body adds into the output.** The block's third argument is the output element's current value, and the sum it yields replaces it; the linalg documentation's own matrix-multiplication example describes its body as `C(m, n) += A(m, k) * B(k, n)`.[^linalg] The Vortex kernel above does something else: it starts each `sum` at `0.0` and stores it when the `k` loop ends, so whatever `c` held before the call is overwritten. A translation that used `linalg.matmul` alone would add the old contents of `c` into the answer.

**Two roundings, as decision 56 requires.** Inside the region, `arith.mulf` and `arith.addf` are separate operations with `fastmath<none>`: the product is rounded, then the sum. Nothing in the printed operation fixes the order of the additions over `k`, though. The region describes one step, and the loops that repeat it do not exist yet. Which order a lowering produces, and whether a transformation may change it, are questions for [M5](m5-structured-ops.md) and [M6](m6-affine-and-scf.md), with decision 56 in view.

**The types check the shapes.** The verifier accepts an `8x16` input times a `16x4` input into an `8x4` output and nothing else, in the same way that Vortex's stage 5 checker rejects a call with a mismatched array ([stage 10](../compiler/guide/stage-10-matrix-multiplication.md#shape-mistakes-are-compile-time-errors)). The next example shows the rejection.

## The verifier decides what is valid

Running `mlir-opt` on a file with no options parses it, runs the **verifier**, and prints the file back; the `mlir-opt` tutorial calls this a good way to test whether a file is well formed.[^mliropt] The MLIR paper describes the order of the checks.[^mlir-paper] First come structural rules that hold for every operation: types agree exactly, with no implicit conversion; each value is defined once, and every use respects dominance and visibility; no two symbols in one table share a name; and every block ends with a terminator. Then each operation's own verifier runs, checking, for example, that a matrix multiplication's shapes agree.

The next example holds five broken functions and one repaired function. Two options turn the file into a test. `--split-input-file` checks each chunk between `// -----` lines on its own, and `--verify-diagnostics` makes each `expected-error` comment an assertion: that error must be reported on the line the comment names, and no other error may appear.[^testing][^diagnostics] Only the valid chunk is printed.

--8<-- "includes/examples/mlir/m2-reading-mlir/verifier.mlir.md"

Each broken function breaks a different rule.

1. `@not_dominated` defines `%doubled` in `^then` and uses it in `^join`, but `^join` can also be reached straight from the entry block, on a path where `%doubled` never exists. The error cites the dominance rule and names both places: the use, and in a note, the definition. The repair at the end of the file passes a value along each edge as a block argument, the pattern of Figure 3.
2. `@no_terminator` ends its only block with an `arith.addf`, which says nothing about where control goes next.
3. `@wrong_return_type` returns an `i32` from a function whose type promises an `f32`. `func.return`'s own verifier compares the two, and nothing converts silently.
4. `@undefined_callee` calls `@missing`, a symbol the module does not contain; `func.call` checks that its callee names a function.
5. `@shape_mismatch` passes a `15x4` matrix where the multiplication needs 16 rows. This error comes from `linalg.matmul`'s own verifier, not from the structural rules.

The generic form also lets `mlir-opt` read operations from dialects it has never heard of, because it describes any operation completely. With `--allow-unregistered-dialect`, `mlir-opt` 18.1.8 round-trips a line such as `%r = "shop.total"(%a) {discount = 5 : i32} : (i32) -> i32`; without the option it refuses the line and names the option (both checked on 2026-09-24). An unknown operation gets only the structural checks, dominance among them. The Toy tutorial calls such operations opaque and advises against relying on them in a mature system.[^toy2]

## Locations travel with every operation

[Decision 14](../decisions/program.md#d14) requires the runtime error line of a compiled Vortex program to end with `at <file>:<line>:<column>`, so a compiler has to know, for every check it emits, where in the source the checked operation's span starts. MLIR keeps such a position on every operation. Each operation has a location, and the Toy tutorial stresses that it is mandatory: in LLVM IR a debug location is metadata that can be dropped, while in MLIR a pass that replaces an operation must give the new operation a location too.[^toy2] `mlir-opt` prints locations only when asked, with `--mlir-print-debuginfo`.[^toy2]

The last example pretends to be the output of a front end for a small calculator language whose line 2 reads `let total = price * (1.0 + rate)`:

--8<-- "includes/examples/mlir/m2-reading-mlir/locations.mlir.md"

The three operations built from that line point into `shop.calc`, at the columns of `1.0`, `+` and `*`. Everything written without a `loc(...)` received a position in the `.mlir` file itself, the only source `mlir-opt` knows about. The option `--mlir-print-local-scope` prints each location in place; without it, the printer defines aliases such as `#loc4 = loc("shop.calc":2:22)` at the end of the output and writes `loc(#loc4)` on each line. The printer also named the constant `%cst` instead of `%0`, because `arith.constant` suggests names for its results; for integers it suggests names such as `%c42_i32`.[^asm]

`loc("file":line:column)` is one of several builtin kinds of location. Others record a call site, attach a name, or fuse several locations into one when a pass merges operations.[^builtin] The MLIR paper makes this a design principle, so that anyone can trace how the final code was built from the original program.[^mlir-paper] For Vortex, the file-line-column kind has exactly the three parts that decision 14's error line ends with. Which column to record is the front end's choice: the calculator used each operator's column, while decision 14 prints the column where the failing operation's span starts.

## Reading a file you did not write

The same six steps work on any MLIR file, from any dialect.

1. **Run `mlir-opt` on it with no options.** If the file is invalid, the error names the rule that failed and the line. If it is valid, you get a normalized printout with values renumbered.
2. **List the dialects** by their prefixes, and open each one's page in the MLIR documentation. The paper notes that those pages are generated from the same definitions as the verifier, which helps keep the two in step.[^mlir-paper] Each operation's entry lists its **traits**, named properties that the verifier and passes rely on:[^traits] `IsolatedFromAbove` for `func.func`, `Terminator` for `cf.cond_br`, and `SameOperandsAndResultType` for `arith.mulf`, which is why its custom form writes one type.
3. **Print the generic form** of anything you cannot read by eye, and sort each line into Figure 1's slots.
4. **Find the regions.** For each one, name the operation that owns it and say what its entry block's arguments stand for; the operation's documentation tells you.
5. **Follow the terminators**: successors and the values passed to them inside a region, yields and returns out of it.
6. **Print locations** to tie each operation to the source it came from.

Try steps 3 and 4 on a function with three blanks. It returns the largest element of a five-element memref: it starts from element 0, visits elements 1 to 4, and keeps the larger of each element and the best so far.

```mlir
func.func @largest(%v: memref<5xf32>) -> f32 {
  %c0 = arith.constant 0 : index
  %c1 = arith.constant 1 : index
  %c5 = arith.constant 5 : index
  %first = memref.load %v[%c0] : memref<5xf32>
  %best = scf.for %i = %c1 to %c5 step %c1 iter_args(%sofar = ____) -> (f32) {
    %e = memref.load %v[%i] : memref<5xf32>
    %bigger = arith.cmpf ogt, %e, %sofar : f32
    %keep = arith.select %bigger, %e, %sofar : f32
    scf.yield ____ : f32
  }
  return ____ : f32
}
```

??? check "What fills the three blanks, and what will the comparison's `predicate` print as in generic form?"

    `%first` is the starting value of the loop-carried value, `%keep` is yielded to the next iteration, and `%best`, the loop's result, is returned. `ogt`, "ordered greater than", prints as `predicate = 2 : i64`. The completed function passes `mlir-opt` 18.1.8 (checked on 2026-09-24).

## Where Vortex's facts would live

The chapter has placed each of Vortex's promises somewhere in MLIR, or found that it has no place. Collected:

| Vortex fact | Where MLIR can record it | What checks it |
| --- | --- | --- |
| Fixed shapes ([decision 11](../decisions/arrays.md#d11)) | Static sizes in a `memref` or `tensor` type | The verifier, through each operation's type rules |
| Row-major layout ([decision 43](../decisions/arrays.md#d43)) | A `memref`'s default layout | The type itself |
| One rounding per operation ([decision 56](../decisions/numbers.md#d56)) | `fastmath = #arith.fastmath<none>` on every floating-point `arith` operation | Nothing in MLIR: your own test |
| Rounding to nearest, ties to even ([decision 56](../decisions/numbers.md#d56)) | Nothing in 18.1.8; newer MLIR can name a rounding mode | The back end that runs the code |
| An `&mut` output overlaps no argument ([decision 25](../decisions/references.md#d25)) | Nothing in a `memref` type | Nothing in MLIR: the compiler must carry it |
| `i32` against `u32` | The choice of operation, such as `divsi` or `divui` | Nothing: a wrong choice is a wrong answer |
| Positions for runtime errors ([decision 14](../decisions/program.md#d14)) | The location on every operation | Visible with `--mlir-print-debuginfo` |

MLIR's verifier guards structure: dominance, terminators, types and each operation's own rules. Everything a language promises beyond structure, and Vortex promises a good deal, has to be encoded by that language's compiler and checked by that compiler's tests.

## For Vortex

!!! vortex "Exercise"

    **Build** a way to read your own compiler's output as MLIR: a small tool, separate from the `vortex` command (whose options [decision 20](../decisions/program.md#d20) fixes), that runs your front end on a checked program and writes each function as an MLIR text file that `mlir-opt` 18 accepts. Use only the `builtin`, `func`, `arith`, `scf`, `cf` and `memref` dialects.

    1. Before any code, a one-page mapping from each construct of the [stage 10 kernel](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for) to MLIR: the two `&` array parameters and the `&mut` one, the `for` loops and the type of their variables, the `let mut sum` updated in the innermost loop, each `f32` operation, and the store into `c`. For `sum`, compare a loop-carried value with a one-element memory slot, and write down which you chose and why.
    2. Types that keep every shape static, so that an inconsistent shape in your tool's output, such as a parameter declared `2x3` but loaded as `3x2`, makes `mlir-opt` reject the file instead of giving a wrong answer later.
    3. A location on every operation, in the file-line-column form, pointing where the Vortex construct that produced it starts: the position [decision 14](../decisions/program.md#d14) prints when an operation fails.
    4. No fast-math flag other than `none` on any floating-point operation.
    5. A refusal that names the construct for anything outside the stage 10 kernel's subset: `print`, calls, an index whose runtime check could fail, integer arithmetic that needs an overflow check, structs.

    **Not yet:** lowering the MLIR to machine code or running it ([M4](m4-dialect-conversion.md)), `linalg` ([M5](m5-structured-ops.md)), tensors and bufferization ([M7](m7-bufferization.md)), GPUs ([M10](m10-mlir-for-gpus.md)), linking against MLIR's C++ libraries, and any decision about whether Vortex should use MLIR at all ([M12](m12-vortex-gpu-path.md)).

    **Proof that it works:**

    - The files for the stage 10 kernel, a square variant and one more shape all pass `mlir-opt` with no options.
    - A round trip: `mlir-opt --mlir-print-op-generic kernel.mlir | mlir-opt` prints exactly what `mlir-opt kernel.mlir` prints.
    - A golden test of the text written for the stage 10 kernel.
    - A test that reads the generic form and fails if any floating-point `arith` operation carries a `fastmath` value other than `none`.
    - With `--mlir-print-debuginfo`, the `arith.mulf` carries the line and column where `a[row, k] * b[k, column]` starts in the Vortex source.
    - A canary: in one emitted file, change a shape in one place only, for example `b`'s `3x2` to `2x2` in the function's signature, and confirm that `mlir-opt` rejects the file, so you know the check is running.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is every instruction, loop and function in MLIR, underneath?** An operation: results, a name, operands, successors, properties, regions, attributes, a type and a location.
    - **Why print the generic form?** It writes every part literally, in one fixed order, whatever the dialect, including the defaults a custom form hides.
    - **What replaces a phi?** A block argument: each branch passes a value to the block it jumps to.
    - **Which values may an operation use?** Values whose definitions dominate it in its region, and values from enclosing regions unless an enclosing operation is isolated from above; other functions are reached by symbol.
    - **Where does a constant that is part of an operation live?** In an attribute: inherent ones are properties in `<{ }>`, and discardable ones carry a dialect prefix in `{ }`.
    - **How would you check a module against decision 56?** Read the `fastmath` property of every floating-point operation; it must be `none`, because the verifier does not require it.
    - **What does `mlir-opt file.mlir` with no options do?** It parses, verifies and prints: the cheapest test that a file is well formed.

## Where this comes back

!!! next "You will use this again in"

    - [M3. Passes and pattern rewriting](m3-passes-and-rewriting.md): *generic form*, *verifier*, *region*
    - [M4. Dialect conversion and lowering to LLVM](m4-dialect-conversion.md): *types*, *signless integers*, *block arguments*
    - [M5. Structured ops: linalg, tensor and memref](m5-structured-ops.md): *named operation*, *region body*, *memref*, *outs*
    - [M6. Loops: affine and scf](m6-affine-and-scf.md): *scf.for*, *loop-carried value*, *index*
    - [M7. Bufferization](m7-bufferization.md): *tensor*, *memref*, *aliasing*
    - [M9. Schedules as IR: the transform dialect](m9-transform-dialect.md): *operation*, *region*, *attribute*
    - [M10. MLIR for GPUs](m10-mlir-for-gpus.md): *symbol*, *isolated from above*, *discardable attribute*
    - [M12. Designing Vortex's GPU path](m12-vortex-gpu-path.md): *fastmath flags*, *location*
    - [O3. SSA form: construction and destruction](../optimize/o3-ssa.md): *block arguments*, *phi*
    - [O12. Testing an optimizer](../optimize/o12-testing-optimizers.md): *verifier*, *expected diagnostics*, *round trip*
    - [D1. Debug information](../backend/d1-debug-info.md): *location*

## Sources and further reading

For depth, read the Language Reference's sections on high-level structure, regions and attributes with this chapter's examples open, then chapter 2 of the Toy tutorial, which defines a dialect's operations and prints them in both forms.[^toy2] The MLIR paper explains why the IR has this shape.[^mlir-paper] Jeremy Kun's tutorial series builds a small project on MLIR one commit at a time, starting with the build.[^kun-start]

[^langref]: MLIR Project, "MLIR Language Reference", sections "High-Level Structure", "Identifiers and keywords", "Operations", "Blocks", "Regions", "Type System", "Properties" and "Attributes". <https://mlir.llvm.org/docs/LangRef/>
[^mlir-paper]: Chris Lattner, Mehdi Amini, Uday Bondhugula, Albert Cohen, Andy Davis, Jacques Pienaar, River Riddle, Tatiana Shpeisman, Nicolas Vasilache and Oleksandr Zinenko, "MLIR: A Compiler Infrastructure for the End of Moore's Law", arXiv:2002.11054v2, 2020, sections 2, 3, 4.4, 4.5 and 4.6. Published as "MLIR: Scaling Compiler Infrastructure for Domain Specific Computation", *CGO 2021*, pp. 2-14, doi:10.1109/CGO51591.2021.9370308. <https://arxiv.org/abs/2002.11054>
[^toy2]: MLIR Project, "Chapter 2: Emitting Basic MLIR", Toy tutorial, sections on the anatomy of an operation, "Opaque API" and "Specifying a Custom Assembly Format". <https://mlir.llvm.org/docs/Tutorials/Toy/Ch-2/>
[^kun]: Jeremy Kun, "MLIR - Running and Testing a Lowering", *Math ∩ Programming*, 10 August 2023. <https://www.jeremykun.com/2023/08/10/mlir-running-and-testing-a-lowering/>
[^kun-start]: Jeremy Kun, "MLIR - Getting Started", *Math ∩ Programming*, 10 August 2023, and the tutorial's repository. <https://www.jeremykun.com/2023/08/10/mlir-getting-started/> and <https://github.com/j2kun/mlir-tutorial>
[^irstruct]: MLIR Project, "Understanding the IR Structure", section "Traversing the IR Nesting". <https://mlir.llvm.org/docs/Tutorials/UnderstandingTheIRStructure/>
[^rationale]: MLIR Project, "MLIR Rationale", sections "Block Arguments vs PHI nodes", "Integer signedness semantics" and "Specifying comparison kind as attribute". <https://mlir.llvm.org/docs/Rationale/Rationale/>
[^builtin]: MLIR Project, "Builtin Dialect", entries `builtin.module`, `IntegerAttr`, `IndexType`, `MemRefType` and `RankedTensorType`, and section "Location Attributes". <https://mlir.llvm.org/docs/Dialects/Builtin/>
[^arith]: MLIR Project, "'arith' Dialect", introduction and entries `arith.addf`, `arith.mulf`, `CmpFPredicate`, `FastMathFlagsAttr` and `FastMathFlags`. <https://mlir.llvm.org/docs/Dialects/ArithOps/>
[^llvm-fmf]: LLVM Project, "LLVM Language Reference Manual", section "Fast-Math Flags", entries `contract` and `reassoc`. <https://llvm.org/docs/LangRef.html#fast-math-flags>
[^scf]: MLIR Project, "'scf' Dialect", entry `scf.for`. <https://mlir.llvm.org/docs/Dialects/SCFDialect/>
[^func]: MLIR Project, "'func' Dialect", entry `func.func`. <https://mlir.llvm.org/docs/Dialects/Func/>
[^cf]: MLIR Project, "'cf' Dialect", entry `cf.cond_br`. <https://mlir.llvm.org/docs/Dialects/ControlFlowDialect/>
[^memref]: MLIR Project, "'memref' Dialect", entry `memref.alloca`. <https://mlir.llvm.org/docs/Dialects/MemRef/>
[^passes]: MLIR Project, "Passes", entry `-mem2reg`. <https://mlir.llvm.org/docs/Passes/>
[^ods]: MLIR Project, "Operation Definition Specification (ODS)", section "Variadic operands". <https://mlir.llvm.org/docs/DefiningDialects/Operations/>
[^symbols]: MLIR Project, "Symbols and Symbol Tables", sections "Symbol" and "Symbol Visibility". <https://mlir.llvm.org/docs/SymbolsAndSymbolTables/>
[^traits]: MLIR Project, "Traits", introduction and "Operation Traits List". <https://mlir.llvm.org/docs/Traits/>
[^linalg]: MLIR Project, "'linalg' Dialect", section "Named Payload-Carrying Ops" and the entries `linalg.generic` and `linalg.matmul`. <https://mlir.llvm.org/docs/Dialects/Linalg/>
[^mliropt]: MLIR Project, "Using `mlir-opt`", section "mlir-opt basics". <https://mlir.llvm.org/docs/Tutorials/MlirOpt/>
[^testing]: MLIR Project, "Testing Guide", section on diagnostic tests. <https://mlir.llvm.org/getting_started/TestingGuide/>
[^diagnostics]: MLIR Project, "Diagnostic Infrastructure", sections "Source Locations" and "SourceMgr Diagnostic Verifier Handler". <https://mlir.llvm.org/docs/Diagnostics/>
[^asm]: MLIR Project, "Customizing Assembly Behavior", section "Suggesting SSA/Block Names". <https://mlir.llvm.org/docs/DefiningDialects/Assembly/>
