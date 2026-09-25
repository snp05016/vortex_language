# M1. Why MLIR

<p class="page-intro">Compilers that need more than one intermediate representation usually build each one by hand, with its own parser, printer, checker and pass machinery. MLIR offers one framework in which many levels live side by side. This chapter follows one small computation down four of those levels with mlir-opt, and measures what each step keeps and what it throws away, which is the question Vortex has to answer before it grows a second IR.</p>

<p class="vx-meta" markdown="1">Level: Foundations · Reading time: about 35 minutes · Builds on: [Stage 6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#why-an-ir-sits-in-the-middle)</p>

???+ remember "Before you start, remember"

    ??? question "Why do most compilers put an intermediate representation between the source language and the target machine?"

        A shared IR means one front end per source language and one back end per target, instead of a separate compiler for every pairing. For Vortex, with one language, the IR is where the plan to add a GPU path later is kept: everything above it is shared, and only the part below it changes.

        Introduced in [Stage 6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#why-an-ir-sits-in-the-middle).

    ??? question "What does SSA form promise about a named value, and how can a front end avoid building it by hand for LLVM?"

        Every named value is assigned exactly once. LLVM requires SSA, but a front end can keep each mutable variable in a stack slot and let the mem2reg pass turn those slots into SSA values.

        Introduced in [Stage 6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#ssa-if-you-use-llvm).

    ??? question "What did the nanopass work argue about the size of compiler passes?"

        That many small passes, each doing one task, are easier to understand and change than a few large ones, and that a production compiler rebuilt this way stayed within a factor of two in compile time.

        Introduced in [Stage 6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#many-small-passes).

    ??? question "In what order are the elements of a `[f32; 2, 3]` array stored?"

        Row after row, the last index varying fastest: `[0, 0]`, `[0, 1]`, `[0, 2]`, `[1, 0]`, `[1, 1]`, `[1, 2]`.

        Introduced in [Arrays and shapes, decision 43](../decisions/arrays.md#d43).

    ??? question "May a Vortex compiler turn `sum += a[row, k] * b[k, column]` into one fused multiply-add?"

        No. Each `f32` operation is one IEEE 754 operation, rounded to nearest with ties to even, and a compiler must not contract, reassociate or reorder them.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Explain the problem MLIR was built for: compilers that each rebuild the same machinery for their own mid-level IR.
    - Read a file that mixes three dialects, and say what each dialect contributes.
    - Trace one computation through four levels with `mlir-opt`, and compute by hand where one array element ends up at the lowest level.
    - Tell apart a fact the IR states, a fact a pass can re-derive by analysis, and a fact the IR no longer states, using passes you can run.
    - Weigh what adopting MLIR would give a compiler like Vortex against what it would cost.

## Every compiler grows its own middle

Stage 6 put one IR between the Vortex front end and its back end. LLVM IR is the usual choice for that slot, and it is a good one for what it models. The MLIR paper describes LLVM IR as roughly "C with vectors": scalar values, memory, basic blocks and branches.[^mlir-paper] That level is right for register allocation and instruction selection. It is the wrong level for questions a language designer cares about. The paper's own example is source-level analysis of C++, which is hard once the program is LLVM IR.

So languages add a level of their own above LLVM. The paper lists Swift, Rust, Julia and Fortran as languages that each built their own IR for language-specific optimization and checking; Swift's is called SIL and Rust's MIR, and both sit between the language's syntax tree and LLVM IR.[^mlir-paper] Machine learning frameworks went further. The paper's first figure shows a TensorFlow model reaching hardware through a crowd of separate graph IRs and compilers (XLA HLO, TensorRT, nGraph, Core ML, TensorFlow Lite and others) that shared no infrastructure.[^mlir-paper]

Each of those IRs needs the same supporting pieces: a data structure, a way to print it and read it back, a checker for its rules (a verifier, [M2](m2-reading-mlir.md#the-verifier-decides-what-is-valid) gives the term), a way to run passes over it, and source locations for error messages. Building them well is expensive and rarely anyone's first priority. The paper names the results it saw: poor error messages, failures in edge cases, unpredictable performance, and difficulty supporting new hardware.[^mlir-paper] Nothing about those problems is specific to machine learning. They are what happens when every project writes its own middle.

MLIR's answer is to make a new abstraction level cheap to define and introduce, with the common machinery provided "in the box".[^mlir-paper] It does three things: it standardizes one SSA-based data structure for all IRs, gives a declarative way to define the operations of a new level, and ships the common infrastructure (parsing, printing, location tracking, pass management, multithreaded compilation) once, for every level. Figure 1 draws the difference.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="Two ways to have three IR levels. On the left, built by hand: a tree IR, a loop IR and a machine IR, each with its own parser, printer, verifier and pass runner stacked beneath it, so every piece is written three times. On the right, MLIR: three dialects, linalg, affine and llvm, connected by lowering passes, all resting on one shared band of infrastructure that holds the parser and printer, the verifier framework, the pass manager, and locations and diagnostics.">
<text class="vx-text" x="20" y="26">Built by hand: each level brings its own</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 2">
<rect class="vx-box-strong" x="20" y="44" width="104" height="34" rx="4"/>
<text class="vx-text" x="72" y="66" text-anchor="middle">tree IR</text>
<rect class="vx-box" x="20" y="88" width="104" height="28" rx="4"/>
<text class="vx-text-muted" x="72" y="107" text-anchor="middle">parser</text>
<rect class="vx-box" x="20" y="122" width="104" height="28" rx="4"/>
<text class="vx-text-muted" x="72" y="141" text-anchor="middle">printer</text>
<rect class="vx-box" x="20" y="156" width="104" height="28" rx="4"/>
<text class="vx-text-muted" x="72" y="175" text-anchor="middle">verifier</text>
<rect class="vx-box" x="20" y="190" width="104" height="28" rx="4"/>
<text class="vx-text-muted" x="72" y="209" text-anchor="middle">pass runner</text>
<rect class="vx-box-strong" x="134" y="44" width="104" height="34" rx="4"/>
<text class="vx-text" x="186" y="66" text-anchor="middle">loop IR</text>
<rect class="vx-box" x="134" y="88" width="104" height="28" rx="4"/>
<text class="vx-text-muted" x="186" y="107" text-anchor="middle">parser</text>
<rect class="vx-box" x="134" y="122" width="104" height="28" rx="4"/>
<text class="vx-text-muted" x="186" y="141" text-anchor="middle">printer</text>
<rect class="vx-box" x="134" y="156" width="104" height="28" rx="4"/>
<text class="vx-text-muted" x="186" y="175" text-anchor="middle">verifier</text>
<rect class="vx-box" x="134" y="190" width="104" height="28" rx="4"/>
<text class="vx-text-muted" x="186" y="209" text-anchor="middle">pass runner</text>
<rect class="vx-box-strong" x="248" y="44" width="104" height="34" rx="4"/>
<text class="vx-text" x="300" y="66" text-anchor="middle">machine IR</text>
<rect class="vx-box" x="248" y="88" width="104" height="28" rx="4"/>
<text class="vx-text-muted" x="300" y="107" text-anchor="middle">parser</text>
<rect class="vx-box" x="248" y="122" width="104" height="28" rx="4"/>
<text class="vx-text-muted" x="300" y="141" text-anchor="middle">printer</text>
<rect class="vx-box" x="248" y="156" width="104" height="28" rx="4"/>
<text class="vx-text-muted" x="300" y="175" text-anchor="middle">verifier</text>
<rect class="vx-box" x="248" y="190" width="104" height="28" rx="4"/>
<text class="vx-text-muted" x="300" y="209" text-anchor="middle">pass runner</text>
<text class="vx-text-muted" x="20" y="250">every piece written again for each level,</text>
<text class="vx-text-muted" x="20" y="268">plus a hand-written translation between levels</text>
</g>
<line class="vx-line" x1="380" y1="20" x2="380" y2="300"/>
<text class="vx-text" x="404" y="26">MLIR: dialects on one shared base</text>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 2">
<rect class="vx-box-accent" x="404" y="44" width="96" height="34" rx="4"/>
<text class="vx-mono" x="452" y="66" text-anchor="middle">linalg</text>
<rect class="vx-box-accent" x="524" y="44" width="96" height="34" rx="4"/>
<text class="vx-mono" x="572" y="66" text-anchor="middle">affine</text>
<rect class="vx-box-accent" x="644" y="44" width="96" height="34" rx="4"/>
<text class="vx-mono" x="692" y="66" text-anchor="middle">llvm</text>
<line class="vx-flow" x1="500" y1="61" x2="516" y2="61"/>
<polygon class="vx-arrowhead" points="516,56 524,61 516,66"/>
<line class="vx-flow" x1="620" y1="61" x2="636" y2="61"/>
<polygon class="vx-arrowhead" points="636,56 644,61 636,66"/>
<rect class="vx-box-strong" x="404" y="100" width="336" height="124" rx="4"/>
<text class="vx-text" x="572" y="128" text-anchor="middle">parser and printer</text>
<text class="vx-text" x="572" y="154" text-anchor="middle">verifier framework</text>
<text class="vx-text" x="572" y="180" text-anchor="middle">pass manager</text>
<text class="vx-text" x="572" y="206" text-anchor="middle">locations and diagnostics</text>
<text class="vx-text-muted" x="404" y="250">a new level adds its operations and rules;</text>
<text class="vx-text-muted" x="404" y="268">lowering between levels is ordinary passes</text>
</g>
</svg>
<figcaption>Figure 1. Three IR levels built two ways. By hand (left), each level carries its own parser, printer, verifier and pass runner, and moving between levels is a separate translator. In MLIR (right), each level is a dialect that defines only its operations, types and rules; the machinery underneath is written once, and lowering from one dialect to the next is done by passes that run on the same infrastructure.</figcaption>
</figure>

This is the chapter's claim, and it is a claim about engineering cost, not about what a compiler can compute. A hand-built compiler can have as many levels as it likes. The rest of the chapter shows, with a real file and real passes, what the shared version looks like and where its limits are.

## One computation, three dialects, one file

The running example sums each row of a 3×4 matrix of `f32` into a vector of three sums. It is small enough to follow by hand and has two kinds of loop in it: the three rows do not depend on each other, and the four elements of one row all add into the same sum.

--8<-- "includes/examples/mlir/m1-why-mlir/row-sum-to-affine.mlir.md"

Read the input first; the output belongs to the next section. Every operation name has a prefix before the dot: `func.func`, `linalg.generic`, `arith.addf`, `linalg.yield`. The prefix names a **dialect**, a named family of operations, types and attributes that is defined and loaded as one unit.[^mlir-paper] Here `func` provides functions and `return`, `linalg` provides operations over whole arrays, and `arith` provides scalar arithmetic. The type `memref<3x4xf32>` is a memref, MLIR's type for a buffer in memory with a known element type and shape ([M2](m2-reading-mlir.md#types-say-what-a-value-is) gives the general rule); it comes from the `builtin` dialect that every module can use.[^builtin]

Three dialects share one function, and nothing about that is unusual. The paper states it as a design rule: operations from different dialects can coexist at any level of the IR at any time.[^mlir-paper] The reason it works is that every operation, whatever its dialect, is built from the same parts: results, operands, attributes, regions and a location. The Language Reference calls this MLIR's high-level structure.[^langref] [M2](m2-reading-mlir.md) takes those parts apart; for now it is enough that `mlir-opt` can read, check and print this file without knowing anything special about `linalg`.

The `linalg.generic` operation is the interesting one. It does not spell out a loop. It gives an **indexing map** per operand, a rule that says which element of the operand each iteration touches, and a region, a body of operations nested inside the operation, to run once per iteration ([M2](m2-reading-mlir.md#regions-blocks-and-what-a-value-can-see) gives the general rule).[^linalg] The maps say iteration `(i, j)` reads `m[i, j]` and updates `sums[i]`. The `iterator_types` line gives each loop dimension an **iterator type**, a kind: `i` is `"parallel"`, meaning its iterations may run in any order, and `j` is `"reduction"`, meaning its iterations combine into one result. The `linalg` documentation says these kinds are declared so that transformations can use them, and that they may be "hard (or even impossible)" to recover from lower-level code.[^linalg]

??? check "You want each row's product instead of its sum, and separately you want column sums instead of row sums. Which dialect's operations change in each case?"

    For products, only the body changes: `arith.addf` becomes `arith.mulf`, an `arith` operation. (The caller would also have to fill `sums` with ones instead of zeros, because the operation accumulates into what is already there.) For column sums, the `linalg` operation changes: the output map becomes `(i, j) -> (j)` and the iterator kinds swap to `["reduction", "parallel"]`. The `func` signature changes too, because the output is now `memref<4xf32>`. The `arith` body stays the same.

## The lowering staircase

`mlir-opt` does more than check files. Most of what it does is run **passes**, each a transformation that reads a module and rewrites it, named on the command line in the order they run.[^passes] A pass that replaces operations of one level with operations of a lower one is a **lowering**. The example above ran `--convert-linalg-to-affine-loops`, and its output is the same computation one level down: two nested `affine.for` loops and explicit loads and stores.

The **`affine` dialect** is a loop level with strict rules. Loop bounds and array subscripts must be affine functions (sums of constants times variables, plus a constant) of the surrounding loop variables and of values that stay fixed while the loops run.[^affine] Those rules exist for analysis: they let a pass compute exactly which iterations touch which elements, as the next section shows.

Now take the same affine loop nest the rest of the way down. The example below runs six passes. `--lower-affine` turns the affine loops into `scf.for` loops, from the `scf` (structured control flow) dialect, whose bounds are ordinary values, and turns the loads into plain `memref.load`. `--convert-scf-to-cf` replaces structured loops with blocks and branches. The last four convert the remaining operations into the **`llvm` dialect**, which mirrors LLVM IR inside MLIR, and clean up.[^passes]

--8<-- "includes/examples/mlir/m1-why-mlir/row-sum-to-llvm.mlir.md"

This way of moving down a step at a time, each step an ordinary pass that produces a file the same verifier accepts, is **progressive lowering**. The paper lists it among MLIR's design principles and contrasts it with compilers that have a few fixed levels.[^mlir-paper] Its example is Clang, which lowers through five: the syntax tree, LLVM IR, SelectionDAG, MachineInstr and MCInst. The paper calls such fixed pipelines rigid; MLIR's aim is to let each compiler define the levels it needs. Stage 6 already argued for [many small passes](../compiler/guide/stage-6-first-machine-code.md#many-small-passes) within one level; MLIR applies the same idea between levels.

<figure class="vx-figure">
<svg viewBox="0 0 760 460" role="img" aria-label="A staircase of four levels. Level 1, linalg, states iterator types parallel and reduction. An arrow labeled convert-linalg-to-affine-loops leads down to level 2, affine, with two affine.for loops from 0 to 3 and 0 to 4. An arrow labeled lower-affine leads to level 3, scf plus memref, with scf.for loops whose bounds are arith.constant values. An arrow labeled five more passes leads to level 4, llvm, with blocks and branches and each memref turned into scalars. Two side paths are highlighted. From level 1, a different pass, convert-linalg-to-parallel-loops, produces scf.parallel over i with scf.for over j, keeping the marker. From level 2, the analysis pass affine-parallelize produces affine.parallel over i, re-deriving the fact.">
<g class="vx-seq" style="--vx-i: 0; --vx-n: 6">
<rect class="vx-box-strong" x="20" y="20" width="240" height="66" rx="4"/>
<text class="vx-text" x="32" y="42">1. linalg</text>
<text class="vx-mono" x="32" y="60">iterator_types =</text>
<text class="vx-mono" x="32" y="77">["parallel", "reduction"]</text>
</g>
<path class="vx-flow" d="M120 86 L120 164 L172 164"/>
<polygon class="vx-arrowhead" points="172,159 180,164 172,169"/>
<text class="vx-text-muted vx-mono" x="130" y="112">--convert-linalg-to-affine-loops</text>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 6">
<rect class="vx-box" x="180" y="132" width="240" height="66" rx="4"/>
<text class="vx-text" x="192" y="154">2. affine</text>
<text class="vx-mono" x="192" y="172">affine.for %i = 0 to 3</text>
<text class="vx-mono" x="208" y="189">affine.for %j = 0 to 4</text>
</g>
<path class="vx-flow" d="M280 198 L280 276 L332 276"/>
<polygon class="vx-arrowhead" points="332,271 340,276 332,281"/>
<text class="vx-text-muted vx-mono" x="290" y="226">--lower-affine</text>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 6">
<rect class="vx-box" x="340" y="244" width="240" height="66" rx="4"/>
<text class="vx-text" x="352" y="266">3. scf + memref</text>
<text class="vx-mono" x="352" y="284">scf.for ... memref.load</text>
<text class="vx-text-muted" x="352" y="301">bounds are arith.constant values</text>
</g>
<path class="vx-flow" d="M440 310 L440 380 L492 380"/>
<polygon class="vx-arrowhead" points="492,375 500,380 492,385"/>
<text class="vx-text-muted vx-mono" x="450" y="338">5 more passes</text>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 6">
<rect class="vx-box" x="500" y="348" width="240" height="66" rx="4"/>
<text class="vx-text" x="512" y="370">4. llvm</text>
<text class="vx-mono" x="512" y="388">llvm.cond_br, llvm.br</text>
<text class="vx-text-muted" x="512" y="405">each memref became scalars</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 6">
<line class="vx-flow" x1="260" y1="53" x2="462" y2="53"/>
<polygon class="vx-arrowhead" points="462,48 470,53 462,58"/>
<text class="vx-text-muted vx-mono" x="276" y="44">--convert-linalg-</text>
<text class="vx-text-muted vx-mono" x="276" y="76">to-parallel-loops</text>
<rect class="vx-box-accent" x="470" y="20" width="270" height="66" rx="4"/>
<text class="vx-mono" x="482" y="44">scf.parallel over i</text>
<text class="vx-mono" x="498" y="62">scf.for over j</text>
<text class="vx-text-muted" x="482" y="79">the pass read the marker</text>
</g>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 6">
<line class="vx-flow" x1="420" y1="165" x2="462" y2="165"/>
<polygon class="vx-arrowhead" points="462,160 470,165 462,170"/>
<rect class="vx-box-accent" x="470" y="132" width="270" height="66" rx="4"/>
<text class="vx-mono" x="482" y="156">affine.parallel over i</text>
<text class="vx-mono" x="498" y="174">affine.for over j</text>
<text class="vx-text-muted" x="482" y="191">found again by analysis</text>
<text class="vx-text-muted vx-mono" x="470" y="216">--affine-parallelize</text>
</g>
<text class="vx-text-muted" x="20" y="400">Heavy box: the marker is written in the IR.</text>
<text class="vx-text-muted" x="20" y="420">Accent: another pass keeps or re-derives it.</text>
</svg>
<figcaption>Figure 2. The row sums at four levels, each produced from the one above by the passes on the arrow. Only level 1 writes down that rows are independent and that each row is a reduction; levels 2 to 4 are loops and branches. The accent boxes are two ways back to the fact: a different lowering from level 1 that carries it into <code>scf.parallel</code>, and an analysis at level 2 that proves it again from the affine subscripts. No pass shown here recovers it from level 3 or 4.</figcaption>
</figure>

### Walking one element down by hand

Pick the element `m[1, 2]`, row 1, column 2, and follow it to the bottom.

At level 1 there is no loop and no address. The indexing map `(i, j) -> (i, j)` says only that iteration `i = 1, j = 2` reads `m[1, 2]`. At level 2 the same fact is an affine load, `affine.load %arg0[%arg2, %arg3]`, with `%arg2 = 1` and `%arg3 = 2`. At level 3 it is a `memref.load` with the same two subscripts. Up to here the shape, `3x4`, is part of the type, and the verifier rejects a load with the wrong number of subscripts.

At level 4 the memref is gone. The first thing to see is the function's signature: `@row_sum` now takes twelve arguments. MLIR represents a memref at this level as a **descriptor**, a small record holding two pointers (the pointer the buffer was allocated with, and an aligned pointer that loads and stores use), an offset in elements, one size per dimension and one stride per dimension.[^target-llvm] A two-dimensional memref needs seven values and a one-dimensional one needs five; when a descriptor is a function argument, the conversion passes each field as a separate scalar.[^target-llvm] That is `%arg0` to `%arg6` for `m` and `%arg7` to `%arg11` for `sums`.

Now the address. In block `^bb4`, `%17` is the row counter and `%22` the column counter. With `%17 = 1` and `%22 = 2`:

- `%24` is field 1 of `m`'s descriptor: the aligned pointer.
- `%26 = llvm.mul %17, %25` with `%25 = 4`, so `%26 = 4`.
- `%27 = llvm.add %26, %22`, so `%27 = 6`.
- `llvm.getelementptr %24[%27] ... f32` steps 6 elements of type `f32` past the aligned pointer, which is 24 bytes.

Element 6 is exactly where row-major order puts `m[1, 2]`: four elements of row 0, then two more. The multiplier 4 is the row length, the stride of dimension 0. The `builtin` dialect defines a memref with no explicit layout as row-major, with strides `[N, 1]` for an M×N shape, so the lowering could compute the stride from the type and write it as a constant.[^builtin] This is the same layout Vortex fixes in [decision 43](../decisions/arrays.md#d43).

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="The seven scalar arguments that carry a memref of 3 by 4 f32 at the llvm level: allocated pointer, aligned pointer, offset, size 0, size 1, stride 0 and stride 1, bound to arg0 through arg6. Only the aligned pointer is read by the loop; the others are marked not read, because the static type already fixes them. Below, the twelve elements of the matrix in memory, numbered 0 to 11 and grouped into three rows of four. Element 6, which is m[1, 2], is highlighted. Three lines of arithmetic show how the lowered code reaches it: 1 times 4 is 4, 4 plus 2 is 6, and 6 elements of 4 bytes is 24 bytes past the aligned pointer.">
<text class="vx-text" x="20" y="24">memref&lt;3x4xf32&gt; at the llvm level: seven scalar arguments</text>
<rect class="vx-box" x="20" y="38" width="98" height="46" rx="4"/>
<text class="vx-text" x="69" y="57" text-anchor="middle">allocated</text>
<text class="vx-mono" x="69" y="76" text-anchor="middle">%arg0</text>
<rect class="vx-box-strong" x="122" y="38" width="98" height="46" rx="4"/>
<text class="vx-text" x="171" y="57" text-anchor="middle">aligned</text>
<text class="vx-mono" x="171" y="76" text-anchor="middle">%arg1</text>
<rect class="vx-box" x="224" y="38" width="98" height="46" rx="4"/>
<text class="vx-text" x="273" y="57" text-anchor="middle">offset</text>
<text class="vx-mono" x="273" y="76" text-anchor="middle">%arg2</text>
<rect class="vx-box" x="326" y="38" width="98" height="46" rx="4"/>
<text class="vx-text" x="375" y="57" text-anchor="middle">size 0</text>
<text class="vx-mono" x="375" y="76" text-anchor="middle">%arg3</text>
<rect class="vx-box" x="428" y="38" width="98" height="46" rx="4"/>
<text class="vx-text" x="477" y="57" text-anchor="middle">size 1</text>
<text class="vx-mono" x="477" y="76" text-anchor="middle">%arg4</text>
<rect class="vx-box" x="530" y="38" width="98" height="46" rx="4"/>
<text class="vx-text" x="579" y="57" text-anchor="middle">stride 0</text>
<text class="vx-mono" x="579" y="76" text-anchor="middle">%arg5</text>
<rect class="vx-box" x="632" y="38" width="98" height="46" rx="4"/>
<text class="vx-text" x="681" y="57" text-anchor="middle">stride 1</text>
<text class="vx-mono" x="681" y="76" text-anchor="middle">%arg6</text>
<text class="vx-text-muted" x="69" y="102" text-anchor="middle">not read</text>
<text class="vx-text-accent" x="171" y="102" text-anchor="middle">read</text>
<text class="vx-text-muted" x="477" y="102" text-anchor="middle">not read: the type already fixes these</text>
<path class="vx-flow" d="M171 108 L171 124 L48 124 L48 142"/>
<polygon class="vx-arrowhead" points="43,142 48,150 53,142"/>
<text class="vx-text-muted" x="132" y="146" text-anchor="middle">row 0</text>
<text class="vx-text-muted" x="356" y="146" text-anchor="middle">row 1</text>
<text class="vx-text-muted" x="580" y="146" text-anchor="middle">row 2</text>
<g>
<rect class="vx-box" x="20" y="152" width="56" height="34"/><text class="vx-mono" x="48" y="174" text-anchor="middle">0</text>
<rect class="vx-box" x="76" y="152" width="56" height="34"/><text class="vx-mono" x="104" y="174" text-anchor="middle">1</text>
<rect class="vx-box" x="132" y="152" width="56" height="34"/><text class="vx-mono" x="160" y="174" text-anchor="middle">2</text>
<rect class="vx-box" x="188" y="152" width="56" height="34"/><text class="vx-mono" x="216" y="174" text-anchor="middle">3</text>
<rect class="vx-box" x="244" y="152" width="56" height="34"/><text class="vx-mono" x="272" y="174" text-anchor="middle">4</text>
<rect class="vx-box" x="300" y="152" width="56" height="34"/><text class="vx-mono" x="328" y="174" text-anchor="middle">5</text>
<rect class="vx-box-accent" x="356" y="152" width="56" height="34"/><text class="vx-mono" x="384" y="174" text-anchor="middle">6</text>
<rect class="vx-box" x="412" y="152" width="56" height="34"/><text class="vx-mono" x="440" y="174" text-anchor="middle">7</text>
<rect class="vx-box" x="468" y="152" width="56" height="34"/><text class="vx-mono" x="496" y="174" text-anchor="middle">8</text>
<rect class="vx-box" x="524" y="152" width="56" height="34"/><text class="vx-mono" x="552" y="174" text-anchor="middle">9</text>
<rect class="vx-box" x="580" y="152" width="56" height="34"/><text class="vx-mono" x="608" y="174" text-anchor="middle">10</text>
<rect class="vx-box" x="636" y="152" width="56" height="34"/><text class="vx-mono" x="664" y="174" text-anchor="middle">11</text>
</g>
<line class="vx-line" x1="244" y1="146" x2="244" y2="192"/>
<line class="vx-line" x1="468" y1="146" x2="468" y2="192"/>
<text class="vx-text-accent" x="384" y="206" text-anchor="middle">m[1, 2]</text>
<text class="vx-mono" x="20" y="236">%26 = llvm.mul %17, %25     1 × 4 = 4</text>
<text class="vx-mono" x="20" y="258">%27 = llvm.add %26, %22     4 + 2 = 6</text>
<text class="vx-mono" x="20" y="280">llvm.getelementptr %24[%27] 6 × 4 bytes = 24 bytes past the aligned pointer</text>
</svg>
<figcaption>Figure 3. Where <code>m[1, 2]</code> ends up. At the llvm level the memref arrives as seven scalars, and for this static shape the loop reads only the aligned pointer: the sizes and strides are also present as arguments, but the lowering used the constants the type fixed instead. Row-major order puts <code>m[1, 2]</code> at element 6, and the three highlighted operations in <code>^bb4</code> compute exactly that.</figcaption>
</figure>

??? check "In the llvm-dialect output, where are the loop bounds 3 and 4 now, and what in the file still says they are loop bounds?"

    They are `llvm.mlir.constant(3 : index)` and `llvm.mlir.constant(4 : index)`, compared with the counters by `llvm.icmp "slt"` in blocks `^bb1` and `^bb3`, whose `llvm.cond_br` either enters the body or leaves. Nothing marks them as bounds. A loop exists only as a pattern: a block that compares, a branch into a body, and a branch back. The constant 4 even appears twice with two meanings, as the column bound (`%20`) and as the row stride (`%25`), and nothing in the IR tells the two uses apart. A pass that wants the loop back must rediscover it from the control-flow graph, which is what LLVM's loop analysis does ([O8](../optimize/o8-loops.md)).

## What each step keeps, and what it drops

Go back to level 2 in the first example's output and look for the words `parallel` and `reduction`. They are not there. `--convert-linalg-to-affine-loops` produced two ordinary `affine.for` loops, which say nothing about the order their iterations may run in. The lowering did what its name says and nothing more: it turned the operation into loops, and the iterator kinds were not part of what it produced.

A different pass, reading the same file, makes a different choice:

--8<-- "includes/examples/mlir/m1-why-mlir/row-sum-to-parallel.mlir.md"

`--convert-linalg-to-parallel-loops` turned the `"parallel"` dimension into an `scf.parallel` loop and the `"reduction"` dimension into an ordinary `scf.for` inside it. The `scf` documentation defines `scf.parallel` as a loop whose iteration space can be iterated in any order.[^scf] So the fact survived, carried into an operation whose meaning includes it. Figure 2's upper accent box is this path. The lesson is not that one pass is right and the other wrong. Each lowering is a choice about what to carry forward, and the pipeline you pick decides which facts later passes will see.

A dropped fact is not always lost. The affine level was designed so that dependences between iterations can be computed exactly from the subscripts, without guessing.[^mlir-paper] Give the affine nest to an analysis:

--8<-- "includes/examples/mlir/m1-why-mlir/row-sum-rediscover.mlir.md"

`--affine-parallelize` converts an `affine.for` into an `affine.parallel` when it can show the iterations are independent.[^passes] It proved that different values of `i` touch different `sums[i]`, and turned the outer loop into `affine.parallel`. It left the inner loop alone, because every `j` of one row reads and writes the same `sums[i]`: a **loop-carried dependence**, where one iteration uses a value that an earlier iteration wrote. The pass rediscovered the fact that `linalg` had stated, and saw for itself that the inner loop cannot run in parallel as written, by analysis instead of by trust.

That gives three states a fact can be in at a given level, and Figure 4 fills them in for the row sums:

- **stated**: written in the IR, in a type, an attribute or an operation whose meaning includes it;
- **derivable**: not written, but a pass at that level can prove it again from what is written;
- **not stated**: neither, as far as any pass in this chapter goes.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="A table of five facts about the row sums against four levels. Rows are independent: stated at linalg, derivable at affine, not stated at scf and llvm. Each row is one reduction: stated at linalg, derivable at affine, not stated at scf and llvm. Loop nest of 3 by 4 iterations: stated at linalg by the operand shapes, stated at affine and scf, not stated at llvm, where only branches remain. Shape 3 by 4: stated in the type at linalg, affine and scf; only constants at llvm. Row-major layout: stated by the memref type at linalg, affine and scf; at llvm only as the arithmetic i times 4 plus j.">
<text class="vx-text" x="20" y="28">fact</text>
<text class="vx-text" x="326" y="28" text-anchor="middle">linalg</text>
<text class="vx-text" x="446" y="28" text-anchor="middle">affine</text>
<text class="vx-text" x="566" y="28" text-anchor="middle">scf</text>
<text class="vx-text" x="686" y="28" text-anchor="middle">llvm</text>
<text class="vx-text" x="20" y="64">rows are independent</text>
<rect class="vx-box-strong" x="270" y="42" width="112" height="34" rx="3"/><text class="vx-text" x="326" y="64" text-anchor="middle">stated</text>
<rect class="vx-box-accent" x="390" y="42" width="112" height="34" rx="3"/><text class="vx-text" x="446" y="64" text-anchor="middle">derivable</text>
<rect class="vx-box" x="510" y="42" width="112" height="34" rx="3"/><text class="vx-text-muted" x="566" y="64" text-anchor="middle">not stated</text>
<rect class="vx-box" x="630" y="42" width="112" height="34" rx="3"/><text class="vx-text-muted" x="686" y="64" text-anchor="middle">not stated</text>
<text class="vx-text" x="20" y="108">each row is one reduction</text>
<rect class="vx-box-strong" x="270" y="86" width="112" height="34" rx="3"/><text class="vx-text" x="326" y="108" text-anchor="middle">stated</text>
<rect class="vx-box-accent" x="390" y="86" width="112" height="34" rx="3"/><text class="vx-text" x="446" y="108" text-anchor="middle">derivable</text>
<rect class="vx-box" x="510" y="86" width="112" height="34" rx="3"/><text class="vx-text-muted" x="566" y="108" text-anchor="middle">not stated</text>
<rect class="vx-box" x="630" y="86" width="112" height="34" rx="3"/><text class="vx-text-muted" x="686" y="108" text-anchor="middle">not stated</text>
<text class="vx-text" x="20" y="152">a 3 × 4 loop nest</text>
<rect class="vx-box-strong" x="270" y="130" width="112" height="34" rx="3"/><text class="vx-text" x="326" y="152" text-anchor="middle">stated</text>
<rect class="vx-box-strong" x="390" y="130" width="112" height="34" rx="3"/><text class="vx-text" x="446" y="152" text-anchor="middle">stated</text>
<rect class="vx-box-strong" x="510" y="130" width="112" height="34" rx="3"/><text class="vx-text" x="566" y="152" text-anchor="middle">stated</text>
<rect class="vx-box" x="630" y="130" width="112" height="34" rx="3"/><text class="vx-text-muted" x="686" y="152" text-anchor="middle">branches only</text>
<text class="vx-text" x="20" y="196">shape 3 × 4</text>
<rect class="vx-box-strong" x="270" y="174" width="112" height="34" rx="3"/><text class="vx-text" x="326" y="196" text-anchor="middle">in the type</text>
<rect class="vx-box-strong" x="390" y="174" width="112" height="34" rx="3"/><text class="vx-text" x="446" y="196" text-anchor="middle">in the type</text>
<rect class="vx-box-strong" x="510" y="174" width="112" height="34" rx="3"/><text class="vx-text" x="566" y="196" text-anchor="middle">in the type</text>
<rect class="vx-box" x="630" y="174" width="112" height="34" rx="3"/><text class="vx-text-muted" x="686" y="196" text-anchor="middle">constants</text>
<text class="vx-text" x="20" y="240">row-major layout</text>
<rect class="vx-box-strong" x="270" y="218" width="112" height="34" rx="3"/><text class="vx-text" x="326" y="240" text-anchor="middle">in the type</text>
<rect class="vx-box-strong" x="390" y="218" width="112" height="34" rx="3"/><text class="vx-text" x="446" y="240" text-anchor="middle">in the type</text>
<rect class="vx-box-strong" x="510" y="218" width="112" height="34" rx="3"/><text class="vx-text" x="566" y="240" text-anchor="middle">in the type</text>
<rect class="vx-box" x="630" y="218" width="112" height="34" rx="3"/><text class="vx-mono" x="686" y="240" text-anchor="middle">i*4 + j</text>
<text class="vx-text-muted" x="20" y="284">Following the main staircase: linalg, then affine loops, then scf, then llvm.</text>
</svg>
<figcaption>Figure 4. What the IR says about the row sums at each level of the main staircase. Heavy cells are stated, accent cells derivable, plain cells not stated. "Derivable" means a pass at that level can prove the fact again: <code>--affine-parallelize</code> did so at the affine level. At the llvm level the shape and the layout survive only inside arithmetic, as the constants 3 and 4 and the expression that computes an address; a reader who knows what to look for can see them, but no type states them and the verifier checks none of it.</figcaption>
</figure>

The paper names the principle behind this table. It asks the system to keep higher-level structure for as long as analysis or optimization needs it, because recovering structure after lowering is fragile, and it wants each loss of structure to be deliberate.[^mlir-paper] The `linalg` design notes make the same argument against **raising**, the practice of rebuilding a high-level form from low-level code before transforming it: patterns found that way can be broken by earlier transformations, which makes their detection fragile or impossible.[^linalg-rationale] MLIR does not prevent you from lowering too early. It makes it cheap to have a level where the fact is still stated, and it lets you check, with passes you can run, which levels have it.

??? check "Now take the column sums from the first check: the iterator kinds become reduction for `i` and parallel for `j`, and the output map becomes `(i, j) -> (j)`. Predict what `--convert-linalg-to-parallel-loops` produces, and what `--affine-parallelize` does to the affine version."

    `--convert-linalg-to-parallel-loops` keeps the operation's loop order, so the outer loop over `i` becomes an `scf.for` and the inner loop over `j` becomes an `scf.parallel` inside it. `--affine-parallelize` reaches the same answer by analysis: different `j` write different `sums[j]`, so the inner loop becomes `affine.parallel`, and every `i` updates the same four sums, so the outer loop stays `affine.for`. Copy `row-sum-to-parallel.mlir`, change the output map, the iterator kinds and the output type to `memref<4xf32>`, and run both passes to check.

## A marker is a promise, not a proof

The `"parallel"` marker was true in the row sums. What happens when it is false?

--8<-- "includes/examples/mlir/m1-why-mlir/row-sum-false-marker.mlir.md"

Here both dimensions are marked `"parallel"`, although the four columns of a row all add into the same `sums[i]`. The verifier accepts the file. `--convert-linalg-to-parallel-loops` trusts the marker and produces one two-dimensional `scf.parallel`, whose iterations may run in any order, including at the same time. Four iterations then read `sums[i]`, add, and write it back, with nothing ordering them. Run in parallel, that is a data race that can lose additions; lowered to a sequential loop, it happens to give the right sums.

This is by design. The `linalg` documentation describes the iterator kinds as a contract: the front end or the user guarantees them, and the compiler may take advantage of them.[^linalg] The verifier checks that the operation is well formed (the number of maps matches the number of operands, each map has the right number of dimensions), not that the claim is true. `--convert-linalg-to-affine-loops` ignored the marker, so it produced the same correct sequential loops for the false file as for the true one. An analysis such as `--affine-parallelize` would have refused to parallelize `j`. A pass that trusts the marker is only as correct as whoever wrote it.

For Vortex this cuts both ways. Some Vortex facts are exactly this kind of promise, backed by the language. [Decision 25](../decisions/references.md#d25) forbids a variable borrowed by `&mut` from appearing in any other argument of the same call, and stage 5 rejects programs that break the rule. A Vortex front end that emits a marker built on that rule is passing on a fact it has already checked, which is the situation the `linalg` contract assumes. A front end that emits a marker it has not checked is making the same mistake as the file above, and no MLIR verifier will catch it.

## What MLIR gives you, and what it asks of you

The paper lists the principles the design follows.[^mlir-paper] Read them against what you have seen:

- **Little built in, everything customizable.** A few concepts (operations, types, attributes) express everything else; `linalg`, `affine` and `llvm` are all dialects, none privileged.
- **SSA and regions.** Values are SSA, and operations can hold nested regions, which is how `affine.for` and `linalg.generic` carry their bodies.
- **Progressive lowering.** Small steps across many levels, which the staircase showed.
- **Maintain higher-level semantics.** Keep structure until it is no longer needed, which the fact table measured.
- **IR validation.** A verifier that each dialect extends with its own rules.
- **Declarative rewrite patterns.** Transformations written as rules a machine can analyze ([M3](m3-passes-and-rewriting.md)).
- **Source location tracking.** Every operation carries a source location, and passes keep it as they rewrite ([M2](m2-reading-mlir.md)).

Behind the principles is infrastructure a dialect author does not write. Operations are declared in **ODS** (Operation Definition Specification), a TableGen-based format from which MLIR generates the C++ operation classes, their verifiers and their documentation.[^mlir-paper] The pass manager runs on any operation at any depth of nesting, not on a fixed set such as modules and functions, and it can run passes on separate functions in parallel.[^mlir-paper] The textual form round-trips: because a pass has no hidden state, running it alone on a file gives the same result as running it inside a full pipeline.[^mlir-paper] Every example in this chapter depends on that property: a text file in, one pass, a text file out, compared byte for byte.

Generic passes work across dialects through what operations declare about themselves. The paper's examples: dead-code elimination and common-subexpression elimination need only to know that an operation has no side effects, which an operation states as a **trait**, a property declared once in its definition.[^mlir-paper] A single canonicalization pass collects simplification rules from every dialect, where LLVM has separate special-purpose passes such as InstCombine and DAGCombine.[^mlir-paper] Passes treat an operation they know nothing about conservatively.[^mlir-paper] The Toy tutorial shows the extreme case: for an operation no dialect has registered, MLIR still enforces structural rules such as dominance, but otherwise treats it as opaque.[^toy]

??? check "You define a dialect with an operation `shape.area` that only computes a number from its operands, but you do not declare any traits. A later pass finds a `shape.area` whose result is never used. What happens, and what one change lets the generic dead-code pass remove it?"

    The operation stays. A pass that knows nothing about an operation must assume it may have side effects, such as writing memory, so deleting it could change the program. Declaring the operation free of side effects, as a trait in its ODS definition, lets dead-code elimination and similar generic passes treat it like any pure arithmetic operation, without either pass knowing about your dialect.

The approach has users well beyond machine learning. The MLIR project's list includes Flang, LLVM's Fortran front end, whose FIR and HLFIR dialects model Fortran before LLVM code generation; CIRCT, which applies MLIR to hardware design; and IREE, OpenXLA and ONNX-MLIR for machine-learning models.[^users] The paper describes Flang's case: modelling Fortran's virtual dispatch tables as first-class operations enabled a devirtualization pass, and using MLIR let the team spend its effort on the IR design for its domain instead of on basic infrastructure.[^mlir-paper]

The costs are real too:

- **A large dependency.** MLIR is built from the LLVM repository as one of its projects, with `-DLLVM_ENABLE_PROJECTS=mlir`.[^getting-started] Its API is C++, and its operations are defined in TableGen.
- **Version drift.** This book pins `mlir-opt` 18.1.8. The online documentation follows the development version, and it already differs: the local tool describes `--lower-affine` as lowering to "Standard and SCF" operations, while the current Passes page says "Arith and SCF".[^passes] Pass names and options can change between releases.
- **A vocabulary to learn.** Region, block, symbol, dialect, attribute against property, before the first useful pass ([M2](m2-reading-mlir.md)).
- **Little guidance.** The paper's own authors write that MLIR gives little guidance on how to design a good abstraction, and that this is not well understood yet.[^mlir-paper] MLIR makes a new level cheap to build; it does not tell you what the level should say.

None of this makes MLIR the right choice for every compiler. A compiler with one source language, one IR and one CPU back end, which is what the v0.1 guide builds, needs none of it. The question becomes real when a compiler wants a level above its current IR, as Vortex will when it looks at array-level optimization and a GPU path. [M12](m12-vortex-gpu-path.md) weighs that for Vortex, after [M5](m5-structured-ops.md), [M6](m6-affine-and-scf.md) and [M10](m10-mlir-for-gpus.md) have shown what the framework offers for arrays, loops and GPUs.

## For Vortex

!!! vortex "Exercise"

    **Build** a level dump for your own compiler and a fact ledger for the [stage 10 kernel](../compiler/guide/stage-10-matrix-multiplication.md), `fn multiply(a: &[f32; 2, 3], b: &[f32; 3, 2], c: &mut [f32; 2, 2])`. This is test infrastructure. It adds no new IR level and changes no output of the `vortex` command, whose options [decision 20](../decisions/program.md#d20) fixes.

    1. A test-only way to print, for one checked program, every level your compiler has as text: the checked tree (the `--ast` output may already be it), your IR if you have one, and what your back end emits (LLVM IR, C or assembly, whichever you chose in stage 6).
    2. A golden test per level for the stage 10 kernel, so that a change at any level fails a test that names the level.
    3. A ledger: a table with one row per fact and one column per level, each cell **stated**, **derivable** or **not stated**, with the line of the dump that shows it. The facts: constant array shapes ([decision 11](../decisions/arrays.md#d11)), row-major layout ([decision 43](../decisions/arrays.md#d43)), one rounding per `f32` operation with no contraction ([decision 56](../decisions/numbers.md#d56)), and the `&mut` rule ([decision 25](../decisions/references.md#d25)).
    4. For every fact that is not stated at your lowest level, one sentence: which later step would need it (a pass that reorders iterations, a vectorizer, a GPU path), or "none yet".
    5. Three to five sentences comparing your ledger with Figure 4: which of your "not stated" cells would a dialect such as `linalg` or `memref` state as a type or an attribute, and which would no MLIR level state for you. Treat the answer as provisional; [M12](m12-vortex-gpu-path.md) is where the decision is made.

    **Not yet:** emitting MLIR (that is [M2](m2-reading-mlir.md)'s exercise), adding an IR level, any optimization that uses the facts, and deciding whether Vortex should use MLIR.

    **Proof that it works:**

    - The golden tests pass, and a hand edit to any one golden file makes exactly one test fail, naming its level.
    - A test reads your lowest text level for the stage 10 kernel and fails if any floating-point operation carries a permission to contract or reassociate (in LLVM IR, the fast-math flags `contract`, `reassoc` or `fast`[^llvm-fmf]), so decision 56 is checked where it is easiest to lose.
    - For the element `a[1, 2]`, the ledger shows, at your lowest level, the offset in elements your compiler computes, and it equals the row-major offset you work out by hand from decision 43.
    - Every "stated" or "derivable" cell in the ledger cites a line of a dump or a check that proves it, not a recollection.

## Key ideas

!!! recap "Questions you can now answer"

    - **What problem was MLIR built to solve?** Languages and ML frameworks each built their own mid-level IR and, with it, their own parser, printer, verifier and pass machinery; MLIR shares that machinery across all levels.
    - **What is a dialect?** A named family of operations, types and attributes, such as `linalg`, `affine` or `arith`; one module can mix several, and the same tools read, check and print them all.
    - **What is progressive lowering?** Moving from a high level to a low one in small passes, each producing IR the same verifier accepts, instead of one large translation.
    - **What happens to a memref at the llvm level?** It becomes a descriptor of two pointers, an offset, and a size and stride per dimension, passed as separate scalars; with a static shape the code uses constants from the type.
    - **Can a fact dropped by a lowering come back?** Sometimes: at the affine level `--affine-parallelize` re-derives which loops are parallel from exact subscripts; at the llvm level no pass in this chapter can.
    - **Does the verifier check that `"parallel"` is true?** No. Iterator kinds are a promise from whoever wrote the operation, and a pass that trusts a false one can produce a wrong program.
    - **What does adopting MLIR cost?** A large C++ and TableGen dependency that changes between releases, a new vocabulary, and little guidance on what a good level should say.

## Where this comes back

!!! next "You will use this again in"

    - [M2. Reading MLIR](m2-reading-mlir.md): *operation*, *region*, *dialect*, *verifier*
    - [M3. Passes and pattern rewriting](m3-passes-and-rewriting.md): *pass*, *trait*, *canonicalization*
    - [M4. Dialect conversion and lowering to LLVM](m4-dialect-conversion.md): *the llvm dialect*, *memref descriptor*
    - [M5. Structured ops: linalg, tensor and memref](m5-structured-ops.md): *`linalg.generic`*, *indexing maps*, *iterator types as a contract*
    - [M6. Loops: affine and scf](m6-affine-and-scf.md): *`affine.for`*, *`affine.parallel`*, *`scf.parallel`*
    - [M9. Schedules as IR: the transform dialect](m9-transform-dialect.md): *choosing which facts a pipeline keeps*
    - [M12. Designing Vortex's GPU path](m12-vortex-gpu-path.md): *the adoption trade, weighed for Vortex*
    - [P6. Dependence analysis](../optimize/p6-dependence-analysis.md): *loop-carried dependence*, *derivable facts*
    - [P9. The polyhedral model](../optimize/p9-polyhedral-model.md): *affine subscripts*, *exact dependences*
    - [O8. Loops: structure, induction variables and bounds checks](../optimize/o8-loops.md): *rediscovering loops from branches*
    - [O10. Pass managers and pipelines](../optimize/o10-pass-pipelines.md): *many small passes*, *pipeline order*
    - [E1. The LLVM code generator pipeline](../backend/e1-llvm-codegen-pipeline.md): *fixed levels: SelectionDAG, MachineInstr, MCInst*

## Sources and further reading

The MLIR paper is the primary source for why the project takes this shape; its introduction and its sections on design principles and consequences are worth reading whole.[^mlir-paper] The `linalg` rationale explains why structured operations keep information instead of lowering early.[^linalg-rationale] The Toy tutorial's second chapter shows what defining a dialect involves.[^toy] The dialect, pass and conversion pages below are the primary sources for every operation and flag this chapter ran.

[^mlir-paper]: Chris Lattner, Mehdi Amini, Uday Bondhugula, Albert Cohen, Andy Davis, Jacques Pienaar, River Riddle, Tatiana Shpeisman, Nicolas Vasilache and Oleksandr Zinenko, "MLIR: A Compiler Infrastructure for the End of Moore's Law", arXiv:2002.11054v2, 2020, sections 1, 1.2, 2, 3 ("Dialects"), 4.1, 4.3, 4.4, 5.2, 5.3, 6.1 and 6.4 (section numbers are the arXiv version's). Published as "MLIR: Scaling Compiler Infrastructure for Domain Specific Computation", *CGO 2021*, pp. 2-14, doi:10.1109/CGO51591.2021.9370308. <https://arxiv.org/abs/2002.11054>
[^langref]: MLIR Project, "MLIR Language Reference", section "High-Level Structure". <https://mlir.llvm.org/docs/LangRef/>
[^builtin]: MLIR Project, "Builtin Dialect", entries `MemRefType` (default identity layout, interpreted in row-major fashion) and `StridedLayoutAttr` (strides `[N, 1]` for a row-major M×N type). <https://mlir.llvm.org/docs/Dialects/Builtin/>
[^linalg]: MLIR Project, "'linalg' Dialect", sections "Payload-Carrying Ops" and "Property 3: The Type Of Iterators is Defined Explicitly". <https://mlir.llvm.org/docs/Dialects/Linalg/>
[^linalg-rationale]: MLIR Project, "Linalg Dialect Rationale: The Case For Compiler-Friendly Custom Operations", sections "Preservation of Information" and "Declarative Specification: Avoid Raising". <https://mlir.llvm.org/docs/Rationale/RationaleLinalgDialect/>
[^affine]: MLIR Project, "'affine' Dialect", section "Polyhedral Structures" and entries `affine.for` and `affine.parallel`. <https://mlir.llvm.org/docs/Dialects/Affine/>
[^scf]: MLIR Project, "'scf' Dialect", entries `scf.for` and `scf.parallel`. <https://mlir.llvm.org/docs/Dialects/SCFDialect/>
[^target-llvm]: MLIR Project, "LLVM IR Target", section "Ranked MemRef Types" and the function signature conversion examples. <https://mlir.llvm.org/docs/TargetLLVMIR/>
[^passes]: MLIR Project, "Passes", entries `-convert-linalg-to-affine-loops`, `-convert-linalg-to-parallel-loops`, `-affine-parallelize`, `-lower-affine`, `-convert-scf-to-cf`, `-finalize-memref-to-llvm` and `-convert-func-to-llvm`. The local descriptions quoted come from `mlir-opt --help` for version 18.1.8. <https://mlir.llvm.org/docs/Passes/>
[^toy]: MLIR Project, "Chapter 2: Emitting Basic MLIR", Toy tutorial, sections "Interfacing with MLIR" and "Opaque API". <https://mlir.llvm.org/docs/Tutorials/Toy/Ch-2/>
[^users]: MLIR Project, "Users of MLIR". <https://mlir.llvm.org/users/>
[^getting-started]: MLIR Project, "Getting Started", section on Unix-like compile and testing. <https://mlir.llvm.org/getting_started/>
[^llvm-fmf]: LLVM Project, "LLVM Language Reference Manual", section "Fast-Math Flags", entries `contract`, `reassoc` and `fast`. <https://llvm.org/docs/LangRef.html#fast-math-flags>
