# M1. Why MLIR

<p class="page-intro">One tool can print a program as a high-level array operation, a loop nest, or a block of near-machine instructions, and check every one of them the same way. This chapter follows one small computation down that ladder and asks, at every step, what MLIR is buying you: dialects that coexist in one file, lowering in small steps, and infrastructure you do not have to write yourself.</p>

<p class="vx-meta" markdown="1">Level: Foundations · Reading time: about 18 minutes · Builds on: [Stage 6. The first machine code](../compiler/guide/stage-6-first-machine-code.md)</p>

???+ remember "Before you start, remember"

    ??? question "Why do most compilers put an intermediate representation between the source language and the target machine, instead of translating directly?"

        A shared IR means one front end per source language and one back end per target, instead of a separate compiler for every pairing of the two. It is also where a promise like "the same validated semantics, more than one target" gets kept: everything above the IR is shared, and only the part below it changes.

        Introduced in [Stage 6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#why-an-ir-sits-in-the-middle).

    ??? question "What does SSA form promise about a named value, and why did stage 6 require it before emitting LLVM IR?"

        Every named value is assigned exactly once. LLVM IR requires this of every function it accepts, so a Vortex front end that emits LLVM IR must already be in that form before it gets there.

        Introduced in [Stage 6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#ssa-if-you-use-llvm).

    ??? question "In what order are the elements of a `[f32; 2, 3]` array stored?"

        Row after row, the last index varying fastest: `[0, 0]`, `[0, 1]`, `[0, 2]`, `[1, 0]`, `[1, 1]`, `[1, 2]`.

        Introduced in [Arrays and shapes, decision 43](../decisions/arrays.md#d43).

    ??? question "May a Vortex compiler turn `sum += a[row, k] * b[k, column]` into one fused multiply-add?"

        No. Each `f32` operation is one IEEE 754 operation, rounded to nearest with ties to even, and a compiler must not contract, reassociate or reorder them. The product is rounded, then the sum.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Explain what a dialect is, and read one MLIR file that mixes three of them in a single module.
    - Trace one small computation through four dialects, using nothing but `mlir-opt` and the passes it ships with.
    - Say, for a concrete marker in the IR, at which step it stops being available to later passes, and show that this is something you can test, not merely something you are told.
    - Weigh what adopting MLIR buys a project like Vortex against what building an equivalent two-level scheme by hand would cost.

## One computation, three dialects, one file

Here is a function that reads two four-element arrays and writes their elementwise sum into a third:

```mlir title="examples/mlir/m1-why-mlir/staircase-1-linalg-to-affine.mlir"
--8<-- "examples/mlir/m1-why-mlir/staircase-1-linalg-to-affine.mlir"
```

Running `mlir-opt` on this file with no options at all parses it, checks it, and prints it back:

```text
--8<-- "examples/mlir/m1-why-mlir/staircase-1-linalg-to-affine.expected"
```

That command is not the interesting part yet; the file is. Look at the names before the dots: `func.func`, `linalg.generic`, `arith.addf`, `linalg.yield`. Each prefix names a **dialect**: a family of operations, types and attributes that a piece of MLIR ships as one unit, such as `func` for functions and calls, `linalg` for whole-array computations, or `arith` for scalar arithmetic. This one file uses three dialects at once, in one module, checked by one verifier. Nothing about MLIR forces a file to pick a single dialect and stay there. `mlir-opt` does not know or care which mixture of dialects it is looking at; it knows how to parse an operation, and every dialect's operations are built from the same handful of parts (results, operands, attributes, regions), which the Language Reference calls MLIR's high-level structure and which [M2](m2-reading-mlir.md) takes apart in full.[^langref] That uniformity is the first half of this chapter's answer to "why MLIR": the tool that reads, checks, prints and rewrites this file would do the same job for a file that used ten dialects, or one.

The `linalg.generic` operation in the middle is the interesting one. It is `linalg`'s general-purpose, "payload-carrying" operation: instead of a fixed op per computation, it takes an indexing map per array and a small region that runs once per index.[^linalg] Its `indexing_maps` say that every one of the three arrays is indexed by the same loop variable `i`, and its `iterator_types` say that dimension is `"parallel"`: nothing in the definition requires the four additions to happen in any particular order, or even one at a time. That is a real fact about the computation, stated once, at a level where "these four additions are independent" is a property of the operation rather than something you would have to prove by staring at a loop. Keep that fact in mind; the next two sections are about what happens to it.

??? check "This file's three dialects each contribute one part of the picture. Which dialect would you have to change to turn this into a function with three arguments and one array output, and which would you have to change to turn the addition into a multiply?"

    `func` owns the function signature (its parameter and result types), so a fourth argument or a different result type is a `func` change. `arith` owns the elementwise operation, so swapping `arith.addf` for `arith.mulf` is an `arith` change. `linalg` would not need to change for either edit: it only says "apply this body, pointwise, over these three arrays," and does not care how many arrays there are or what the body computes.

## The lowering staircase

`mlir-opt` does more than check files; most of what it does is run **passes**, transformations that rewrite an MLIR module.[^passes] One pass, `--convert-linalg-to-affine-loops`, takes the file above and replaces the `linalg.generic` operation with an explicit `affine.for` loop, the `affine` dialect's structured loop over a fixed range:[^affine]

```mlir title="examples/mlir/m1-why-mlir/staircase-2-affine-to-scf.mlir"
--8<-- "examples/mlir/m1-why-mlir/staircase-2-affine-to-scf.mlir"
```

Running that same file through `--lower-affine` turns the `affine.for` loop into a more ordinary one, with the loop bounds computed by explicit `arith.constant` operations and the loads and stores now plain `memref` operations instead of `affine` ones:

```text
--8<-- "examples/mlir/m1-why-mlir/staircase-2-affine-to-scf.expected"
```

One more step takes that same `scf`-level file (shown again below, as its own checked example) through a short sequence of conversion passes ending at `--convert-func-to-llvm`:

```mlir title="examples/mlir/m1-why-mlir/staircase-3-scf-to-llvm.mlir"
--8<-- "examples/mlir/m1-why-mlir/staircase-3-scf-to-llvm.mlir"
```

That sequence removes the structured loop entirely. What is left is blocks and a branch, in the `llvm` dialect that mirrors LLVM IR inside MLIR, and every `memref` argument has become a small struct carrying a pointer, an offset and a size:[^llvmd]

```text
--8<-- "examples/mlir/m1-why-mlir/staircase-3-scf-to-llvm.expected"
```

<figure class="vx-figure">
<svg viewBox="0 0 920 700" role="img" aria-labelledby="m1-f1-title m1-f1-desc">
<title id="m1-f1-title">The same computation at four levels, and one place where the path forks</title>
<desc id="m1-f1-desc">A staircase of four boxes, each lower and further right than the last, connected by short arrows labeled with the mlir-opt pass that produces the next box from the one before. Box one, linalg: the operation says its loop dimension is parallel. Box two, affine: a for loop from 0 to 4; the parallel marker is not repeated anywhere in this box. Box three, scf and memref: a for loop with explicit bounds and address computation by hand. Box four, llvm: blocks and a branch, with the array arguments now a pointer-offset-size struct. Below the staircase, a second, separate pair of boxes shows a fork from box one: the same linalg operation, run through a different pass, convert-linalg-to-parallel-loops, produces an scf.parallel operation directly, which does keep the parallel marker, skipping the affine step altogether.</desc>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 6">
<rect class="vx-box-strong" x="20" y="20" width="190" height="40" rx="4"/>
<text class="vx-mono" x="115" y="45" text-anchor="middle">iterator_types = ["parallel"]</text>
<text class="vx-text" x="20" y="82">1. linalg</text>
<text class="vx-text-muted" x="20" y="100">knows: safe to reorder</text>
</g>
<path class="vx-flow" d="M210 60 L250 150"/>
<polygon class="vx-arrowhead" points="250,150 240,144 248,138"/>
<text class="vx-text-muted vx-mono" x="212" y="120">--convert-linalg-</text>
<text class="vx-text-muted vx-mono" x="212" y="134">to-affine-loops</text>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 6">
<rect class="vx-box" x="250" y="150" width="190" height="40" rx="4"/>
<text class="vx-mono" x="345" y="175" text-anchor="middle">affine.for %i = 0 to 4</text>
<text class="vx-text" x="250" y="212">2. affine</text>
<text class="vx-text-muted" x="250" y="230">a loop; no order marker</text>
</g>
<path class="vx-flow" d="M440 190 L480 280"/>
<polygon class="vx-arrowhead" points="480,280 470,274 478,268"/>
<text class="vx-text-muted vx-mono" x="442" y="250">--lower-affine</text>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 6">
<rect class="vx-box" x="480" y="280" width="220" height="40" rx="4"/>
<text class="vx-mono" x="590" y="305" text-anchor="middle">scf.for ... memref.load</text>
<text class="vx-text" x="480" y="342">3. scf + memref</text>
<text class="vx-text-muted" x="480" y="360">a loop; address by hand</text>
</g>
<path class="vx-flow" d="M700 320 L740 410"/>
<polygon class="vx-arrowhead" points="740,410 730,404 738,398"/>
<text class="vx-text-muted vx-mono" x="702" y="380">5 passes, ending</text>
<text class="vx-text-muted vx-mono" x="702" y="394">--convert-func-to-llvm</text>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 6">
<rect class="vx-box" x="700" y="410" width="200" height="40" rx="4"/>
<text class="vx-mono" x="800" y="435" text-anchor="middle">llvm.br ^bb1(%i : i64)</text>
<text class="vx-text" x="700" y="472">4. llvm</text>
<text class="vx-text-muted" x="700" y="490">blocks; array is a struct</text>
</g>
<text class="vx-text-muted" x="20" y="560">The same linalg op, one different single step:</text>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 6">
<rect class="vx-box-accent" x="20" y="580" width="190" height="40" rx="4"/>
<text class="vx-mono" x="115" y="605" text-anchor="middle">iterator_types = ["parallel"]</text>
<text class="vx-text" x="20" y="642">1. linalg (same file)</text>
</g>
<path class="vx-flow" d="M210 600 L330 600"/>
<polygon class="vx-arrowhead" points="330,600 322,594 322,606"/>
<text class="vx-text-muted vx-mono" x="215" y="590">--convert-linalg-to-parallel-loops</text>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 6">
<rect class="vx-box-accent" x="330" y="580" width="220" height="40" rx="4"/>
<text class="vx-mono" x="440" y="605" text-anchor="middle">scf.parallel (%i) = (0) to (4)</text>
<text class="vx-text" x="330" y="642">scf, one step from linalg</text>
<text class="vx-text-muted" x="330" y="660">marker kept, affine skipped</text>
</g>
</svg>
<figcaption>Figure 1. The same four-element array addition at four levels, produced by running the pass named on each arrow. Reading down the staircase, the fact that the loop's four iterations are independent is stated once, at the top, and is not repeated at any lower level: nothing in box two, three or four says it. The lower pair shows that the staircase is not the only path: a different single pass turns the same linalg operation directly into <code>scf.parallel</code>, which does carry the fact forward, skipping the affine level shown above it.</figcaption>
</figure>

This sequence of small steps, each an ordinary pass, each producing a file that the same verifier and the same printer accept, is **progressive lowering**: moving from a high-level dialect to a low-level one a step at a time, rather than in one large translation. [Stage 6](../compiler/guide/stage-6-first-machine-code.md#many-small-passes) already argued for small, inspectable passes over one large lowering step, for a compiler with two levels (a checked tree and machine code). MLIR takes the same argument and applies it between IR levels too: instead of one pass that turns array-level code into blocks and branches, there are several, each short enough to read that you can point at the line where a `memref` load turned into an address computed from a struct's pointer and offset.

??? check "The affine-level file names the loop bound 4 directly, in the loop header. By the time you reach the llvm-dialect file, that same bound has moved. Where is it now, and what changed about how it is checked?"

    It is now the operand of an `llvm.mlir.constant(4 : index)`, compared against the loop counter by an `llvm.icmp "slt"`, with the branch that continues or exits the loop chosen by that comparison. At the affine level, 4 is part of the loop's syntax, and a mismatched bound the verifier can reason about structurally. At the llvm level it is one integer flowing through ordinary instructions, no different in kind from any other value; nothing marks it as a loop bound at all.

## What survives a step, and what does not

The `"parallel"` marker on this file's `linalg.generic` is a good test case for a question every one of these passes raises: what does this step keep, and what does it throw away? You do not have to take the chapter's word for the answer. Change the marker from `"parallel"` to `"reduction"`, leaving everything else the same:

```mlir title="examples/mlir/m1-why-mlir/staircase-1c-marker-ignored.mlir"
--8<-- "examples/mlir/m1-why-mlir/staircase-1c-marker-ignored.mlir"
```

Run it through `mlir-opt` with no options, and the verifier accepts it exactly as before; nothing in the file's shape depends on which word is there. Run it through `--convert-linalg-to-affine-loops`, and the output is, byte for byte, the same `affine.for` loop shown two sections back. The pass never looks at `iterator_types` at all: it always emits one ordinary sequential loop per dimension. Whatever the marker meant at the `linalg` level, this step does not use it, and once you are looking at the `affine.for` loop, there is no way to recover which word was there.

That is not the only path out of the `linalg` level, though. A different pass, `--convert-linalg-to-parallel-loops`, reads the same file and produces `scf.parallel` directly, `scf`'s explicitly parallel loop, one level lower than `affine.for` and carrying the "may run in any order" fact forward into an operation whose name says so.[^scf] Figure 1's lower pair shows this fork. The lesson is not that one path is correct and the other is a bug: it is that "progressive lowering" names a strategy, not a single fixed pipeline, and every fork is a place where a real decision gets made, on purpose or by not thinking about it. A cost model, a scheduler, or a human reading the code all need to know, at some point, which iterations of a loop are independent; that information exists cleanly for exactly as long as you keep choosing steps that preserve it, and the affine loop in Figure 1's top row is the demonstration that it can also quietly disappear.

Vortex's matmul kernel (`fn multiply(a: &[f32; 2, 3], b: &[f32; 3, 2], c: &mut [f32; 2, 2])`, from [stage 10](../compiler/guide/stage-10-matrix-multiplication.md)) has exactly this kind of fact riding along with it: the two `&` arguments are read-only and the `&mut` one overlaps neither, which is what lets a compiler reorder the multiply-add pairs or spread them across threads at all. [References, decision 25](../decisions/references.md#d25) states that guarantee once, in the language. Nothing forces an MLIR representation of that kernel to restate it at every level; whether it can be restated, and at which level, is a question this chapter's staircase already answers by example, and the exercise below asks you to answer it for Vortex's own facts.

## Why build this instead of a two-level scheme

Stage 6 asked Vortex to choose one IR between its front end and its back ends. That is the ordinary shape of a compiler: a checked tree, one intermediate form, and machine code. A project that outgrows two levels, wanting a high-level form close to the source (arrays, shapes, whole-array operations) and a low-level form close to the machine (loops, addresses, registers), has a choice to make. One option is to build both by hand: two data structures, two parsers if either is ever written as text, two printers, two verifiers, and a translation between them that amounts to its own small compiler pass. A project that later wants a third level, or a fourth, pays that cost again.

MLIR is the other option: one op/region/attribute data structure, described once, that every dialect reuses. Defining a new dialect, as the Toy tutorial walks through step by step, means writing down a handful of operations and their verification rules; the parser, the printer, the pass manager, and the diagnostics with source locations that [M2](m2-reading-mlir.md) and [D1](../backend/d1-debug-info.md) cover come for free, because they are not part of any one dialect.[^toy] Lattner et al. present MLIR as infrastructure for exactly this situation: a compiler ecosystem where many teams each want a representation suited to their own level, and would otherwise each rebuild the same supporting machinery to get one.[^mlir-paper] The four files in this chapter are one small, checked demonstration of that claim: four different vocabularies, one tool reading and rewriting all of them, on the same machine that runs the rest of this book's examples (`mlir-opt` 18.1.8, installed alongside the LLVM this project already builds against).

Adopting MLIR is not free. It is a sizeable dependency with its own build and its own vocabulary to learn (regions, blocks, symbols, the distinction between an attribute and a property that [M2](m2-reading-mlir.md#the-parts-of-an-operation) covers), and pulling in the whole framework for one dialect's worth of use is a heavier decision than writing one pass by hand. What this chapter has shown is the other side of that trade: a staircase like Figure 1's is not a special trick MLIR performs once; it is what every dialect in the project gets, by construction, for however many levels a compiler wants to define. [M12](m12-vortex-gpu-path.md) is where that trade gets weighed for Vortex specifically, once later chapters have shown what the framework offers for arrays ([M5](m5-structured-ops.md)), loops ([M6](m6-affine-and-scf.md)) and GPUs ([M10](m10-mlir-for-gpus.md)).

## For Vortex

!!! vortex "Exercise"

    **Build** nothing yet; this exercise is a short written memo, not code. Its subject is whether a hand-built two-level scheme or an MLIR-based one is the better fit for Vortex's stated needs (a high-level array form and a low-level machine-facing one), and the point is to reach that judgment by evidence you gathered yourself, not by assuming the answer.

    1. Run `mlir-opt` on this chapter's five example files yourself (the invocation is in each file's opening comment), and fill in a small table with one row per fact: fixed array shapes ([decision 11](../decisions/arrays.md#d11)), row-major layout ([decision 43](../decisions/arrays.md#d43)), the one-rounding-per-operation rule ([decision 56](../decisions/numbers.md#d56)), and the `&mut` non-overlap rule ([decision 25](../decisions/references.md#d25)). For each, name the lowest of this chapter's four levels at which something in the file's own structure (a type, an attribute, an op's defined semantics) still states the fact, the way `iterator_types` states "safe to reorder" only at the `linalg` level.
    2. For any fact that runs out of levels, that is, is not stated anywhere in the `llvm`-dialect file either, say in one sentence whose job it becomes to guarantee it: the Vortex compiler's own pass, or nothing at all, because the underlying hardware or ABI already does.
    3. Write the memo: three to five sentences comparing what you would have to build yourself, if Vortex's IR pair were two hand-written data structures instead, against what step 1 showed you get for free from one framework. Name at least one concrete piece of infrastructure this chapter used without writing it (the parser, the verifier, the pass that ran `--convert-linalg-to-affine-loops`, the diagnostic that would fire if you broke the file). Reach a tentative answer, not a final one: [M12](m12-vortex-gpu-path.md) is where Vortex's own decision gets made, after several more chapters' worth of evidence.

    **Not yet:** writing any part of a Vortex-to-MLIR translator, choosing MLIR (or rejecting it) for Vortex, anything involving GPUs ([M10](m10-mlir-for-gpus.md)), and reading or writing the generic form in detail ([M2](m2-reading-mlir.md)).

    **Proof that it works:** the table from step 1 names a specific level for each fact, backed by a command you ran yourself and an output you read yourself, not a guess; and the memo from step 3 names at least one piece of reusable infrastructure by its actual name (a pass name, a flag, a dialect), not a generic phrase like "the tooling".

## Key ideas

!!! recap "Questions you can now answer"

    - **What is a dialect?** A family of operations, types and attributes that MLIR ships as one unit, such as `linalg` or `arith`; one module can mix several, and the same parser, verifier and printer handle all of them.
    - **What is progressive lowering?** Moving from a high-level dialect to a low-level one through a sequence of small, separately testable passes, rather than one large translation.
    - **Does every lowering step preserve every fact the higher level stated?** No. This chapter measured one: the `linalg.generic` "parallel" marker survives `--convert-linalg-to-parallel-loops` but not `--convert-linalg-to-affine-loops`, even though both start from the same file.
    - **What does MLIR give a dialect author that they did not have to write?** A parser, a printer, a pass manager, diagnostics with source locations, and a verifier framework, all shared with every other dialect in the project.
    - **What is the concrete cost of adopting it?** A sizeable dependency, with its own build and its own vocabulary (region, block, symbol, attribute versus property), against writing a smaller, purpose-built pair of data structures by hand.
    - **Where would Vortex's own facts, such as fixed shapes or the `&mut` non-overlap rule, have to live in a representation like this?** Sometimes in a type or an attribute, checkable by MLIR's own verifier; otherwise nowhere in the IR, and the responsibility of Vortex's own compiler passes and tests, which the exercise above asks you to work out fact by fact.

## Where this comes back

!!! next "You will use this again in"

    - [M2. Reading MLIR](m2-reading-mlir.md): *operation*, *region*, *dialect*, *generic form*
    - [M3. Passes and pattern rewriting](m3-passes-and-rewriting.md): *pass*, *pass manager*, *`--convert-linalg-to-affine-loops`*
    - [M4. Dialect conversion and lowering to LLVM](m4-dialect-conversion.md): *the llvm dialect*, *type converter*, *memref descriptor*
    - [M5. Structured ops: linalg, tensor and memref](m5-structured-ops.md): *`linalg.generic`*, *indexing maps*, *iterator types*
    - [M6. Loops: affine and scf](m6-affine-and-scf.md): *`affine.for`*, *`scf.for`*, *`scf.parallel`*
    - [M9. Schedules as IR: the transform dialect](m9-transform-dialect.md): *what a lowering step preserves*, *choosing a path*
    - [M10. MLIR for GPUs](m10-mlir-for-gpus.md): *the parallel marker*, *`gpu.launch_func`*
    - [M12. Designing Vortex's GPU path](m12-vortex-gpu-path.md): *the adoption trade weighed for Vortex*
    - [O1. The optimizer's contract](../optimize/o1-optimizer-contract.md): *what a transformation is allowed to change*
    - [O10. Pass managers and pipelines](../optimize/o10-pass-pipelines.md): *many small passes*, *pipeline order*
    - [D1. Debug information](../backend/d1-debug-info.md): *source locations that travel with an operation*

## Sources and further reading

The MLIR paper is the primary source for why the project takes this shape; read its introduction and the sections on dialects and lowering alongside this chapter's four files.[^mlir-paper] The Toy tutorial's second chapter is the fastest way to see what defining your own dialect involves.[^toy] The Language Reference and the specific dialect and pass documentation below are the primary sources for every operation and flag this chapter used.

[^mlir-paper]: Chris Lattner, Mehdi Amini, Uday Bondhugula, Albert Cohen, Andy Davis, Jacques Pienaar, River Riddle, Tatiana Shpeisman, Nicolas Vasilache and Oleksandr Zinenko, "MLIR: A Compiler Infrastructure for the End of Moore's Law", arXiv:2002.11054, 2020. Published as "MLIR: Scaling Compiler Infrastructure for Domain Specific Computation", *CGO 2021*, pp. 2-14, doi:10.1109/CGO51591.2021.9370308. <https://arxiv.org/abs/2002.11054>
[^toy]: MLIR Project, "Chapter 2: Emitting Basic MLIR", Toy tutorial. <https://mlir.llvm.org/docs/Tutorials/Toy/Ch-2/>
[^langref]: MLIR Project, "MLIR Language Reference", section "High-Level Structure". <https://mlir.llvm.org/docs/LangRef/>
[^linalg]: MLIR Project, "'linalg' Dialect", sections "Overview" and the `linalg.generic` entry. <https://mlir.llvm.org/docs/Dialects/Linalg/>
[^affine]: MLIR Project, "'affine' Dialect", section "Operations", entry `affine.for`. <https://mlir.llvm.org/docs/Dialects/Affine/>
[^scf]: MLIR Project, "'scf' Dialect", entries `scf.for` and `scf.parallel`. <https://mlir.llvm.org/docs/Dialects/SCFDialect/>
[^llvmd]: MLIR Project, "'llvm' Dialect", section "Memref Types". <https://mlir.llvm.org/docs/Dialects/LLVM/>
[^passes]: MLIR Project, "Passes", entries `-convert-linalg-to-affine-loops`, `-convert-linalg-to-parallel-loops`, `-lower-affine` and `-convert-func-to-llvm`. <https://mlir.llvm.org/docs/Passes/>
