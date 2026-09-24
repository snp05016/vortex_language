# D3. Reading real back ends

<p class="page-intro">Five small, real back ends, documented well enough to read end to end: what each chose for its IR, its instruction selector, its register allocator, and why. Reading a back end you did not write is its own skill, and it is the skill that lets you borrow a good idea for Vortex without first reading ten years of a production compiler's source.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 25 minutes · Builds on: [C1. Instruction selection](c1-instruction-selection.md), [C2. Liveness](c2-liveness.md), [C3. Register allocation I: linear scan](c3-linear-scan.md), [C4. Register allocation II: graphs and SSA](c4-graph-coloring.md), [C5. Spilling, splitting and rematerialization](c5-spilling.md)</p>

???+ remember "Before you start, remember"

    ??? question "How does Cranelift decide whether to fold a shift into a load's addressing mode, without ever building a separate DAG?"

        By computing use counts for every value up front, once, then walking
        the function backward in a single pass, matching many-to-one
        patterns as it goes. A value with more than one use cannot be
        folded away, because folding it into one consumer would leave the
        other consumers with nowhere to read it from.

        Introduced in [C1. Instruction selection](c1-instruction-selection.md#how-production-compilers-select).

    ??? question "Hjort Blindell's survey lines up instruction-selection approaches on a ladder with four rungs. What are they, in order of how much surrounding code each one looks at?"

        Macro expansion, tree covering, DAG covering, and graph covering.
        Each rung looks at more of the program before choosing an
        instruction, at a rising cost to compute.

        Introduced in [C1. Instruction selection](c1-instruction-selection.md#where-trees-stop-working).

    ??? question "Why does linear scan sweep a single instruction order instead of building an interference graph first?"

        Because building and coloring a graph is more than a fast compiler,
        or a JIT that runs on every call, can afford. Linear scan
        approximates the same liveness facts with one contiguous interval
        per value and allocates in one pass over that order.

        Introduced in [C3. Register allocation I: linear scan](c3-linear-scan.md#from-a-live-value-to-a-register).

    ??? question "What does it mean to rematerialize a value instead of reloading it from a spill slot?"

        Recomputing it with a short instruction sequence instead of storing
        and reloading it. A constant or a simple address is often cheaper
        to recompute at each use than to keep alive in memory across a
        routine.

        Introduced in [C5. Spilling, splitting and rematerialization](c5-spilling.md#rematerializing-instead-of-reloading).

!!! goals "In this chapter"

    - Read a paragraph of a back end's own documentation and answer four design questions from it: what its IR looks like, how it selects instructions, how it allocates registers, and where its output meets the rest of the toolchain.
    - Place each of five real back ends on the instruction-selection ladder C1 introduced, and explain what buys the ability to look at more of the program at once.
    - Compare Cranelift's regalloc2 and QBE's split spiller and linear allocator against the linear-scan algorithm C3 taught by hand.
    - Read a compiler's own debug output, such as a generated SSA dump, before guessing at its structure from prose alone.
    - Judge whether a source is still worth reading, and recognize when it is not.

## A paragraph, and four questions

Start with four sentences from QBE's own documentation, the kind of paragraph every back end's homepage has somewhere: QBE is a compiler back end that takes an SSA-form intermediate language as input, targets amd64, arm64 and riscv64, and hands its generated assembly to the system's own assembler and linker rather than writing object files itself.[^qbe-home] Nothing in that paragraph is instructions to read; it is four answers, already given, to the four questions this chapter asks of every back end:

1. **What is the IR?** An SSA-form intermediate language, described in its own reference document, not a reuse of an existing one.
2. **What is the selection strategy?** Not stated yet in this paragraph; QBE's own comparison page fills that in below.
3. **What is the allocation strategy?** Also not stated yet here.
4. **Where does the output meet the rest of the toolchain?** At the system assembler and linker: QBE never writes an object file or a relocation table itself.

Two of the four questions are already answered from one paragraph, and the other two are a page turn away. That is the whole method of this chapter: read a source, real or in this book, by hunting for these four answers first and filling in the mechanism only once you know which of the four questions it is actually answering. A wall of source code answers none of them directly; a design document, a blog post, or a `README` almost always answers at least one in its first few paragraphs.

## The four questions, and where C1 to C5 answered them once

[C1](c1-instruction-selection.md) taught one answer to question 2, tree covering with maximal munch and a dynamic-programming table, and named the ladder it sits on: macro expansion, tree covering, DAG covering, graph covering, each rung seeing more of the surrounding program at a rising cost to compute.[^hjort] [C3](c3-linear-scan.md) taught one answer to question 3, linear scan over live intervals, built for compilers that cannot afford [C4](c4-graph-coloring.md)'s interference graph. [C5](c5-spilling.md) taught what happens once an allocator runs out: spill everywhere, split around the cold parts of a range, or rematerialize instead of reloading.

Every real back end below answers all four questions too, and none of them answers question 2 or 3 the same way C1 and C3 did. That is not a sign C1 and C3 taught the wrong algorithm: linear scan and tree-based tiling are real, still-shipping answers, chosen by real compilers for real reasons (TCC's own answer to question 2, below, is closer to macro expansion than to anything in C1). What differs is which rung of the ladder a back end can afford to stand on, and how much of that rung it automates instead of hand-writing.

## QBE: read first

QBE states its own goal in one sentence worth paraphrasing rather than quoting: get most of an optimizing compiler's performance from a small fraction of its implementation size, an explicit tradeoff rather than an accident.[^qbe-vs-llvm] Its four answers:

- **IR.** SSA-form, described in a standalone reference; the frontend that feeds QBE writes this IL as text.[^qbe-il]
- **Selection.** Addressing-mode matching on amd64: QBE looks for the same kind of fused address computation [C1](c1-instruction-selection.md#folding-a-memory-operand-is-a-tiling-decision) covered, base plus scaled index, and folds it into one instruction's operand instead of emitting it separately.
- **Allocation.** A spiller and a linear allocator, kept as two separate passes rather than one combined pass, with hinting to prefer a value's most recently used register when more than one choice is legal; its spilling heuristic is loop-aware, in the same spirit as [C5](c5-spilling.md#spilling-is-not-free-and-not-every-value-costs-the-same-to-spill)'s spill-cost weighting by loop depth.[^qbe-home]
- **Output.** Assembly text, handed to the system's own assembler and linker, the same boundary [B1](b1-simplest-backend.md) chose for Vortex's first back end.

Reading QBE's own IL reference after this paragraph is a short trip: every claim above names the document that backs it, and the reference itself is a handful of pages, not a repository.

??? check "QBE splits its spiller from its allocator into two separate passes, where C3's linear scan folds spill decisions into the same sweep that assigns registers. What does QBE's split buy, and what does it cost?"

    Separating the two lets the spiller use information the allocator alone
    does not need, such as loop depth across the whole function, to decide
    what to spill before any register is assigned; the allocator then only
    has to color whatever is left. The cost is an extra pass over the
    function, and a spill decision made without yet knowing exactly how
    tight registers will be once allocation runs.

## Cranelift: read the blog before the code

Cranelift's own author recommends reading its design writing before its source, and the sequence of posts is short enough to follow in an afternoon: pattern-matching selection first, then correctness, then the allocator, then the rule language.[^cranelift-isel1] [^cranelift-isel3] [^cranelift-regalloc2] [^cranelift-isle] [C1](c1-instruction-selection.md#how-production-compilers-select) already summarized Cranelift's selector in three sentences; here is the same design in more depth, with its allocator alongside it.

- **IR.** CLIF, Cranelift's own SSA-form IR, lowered into VCode, a machine-instruction-shaped representation that still carries virtual registers.[^cranelift-isel1]
- **Selection.** One backward pass over VCode, using use counts computed once up front (the fact this chapter's second question above asked about): many-to-one patterns, a shift folded into a load's address, an immediate folded into an arithmetic instruction, match without ever constructing a separate DAG.[^cranelift-isel1] The patterns themselves are written in ISLE, a small typed term-rewriting language that compiles ahead of time into ordinary Rust pattern-matching code, so a rule is data the compiler reads once at its own build time, not an interpreter's workload at every compile.[^cranelift-isle]
- **Allocation.** regalloc2, built after an earlier linear-scan-style allocator proved too slow to correct: it backtracks over live-range "bundles" instead of committing to one interval at a time the way [C3](c3-linear-scan.md#the-algorithm-on-eight-intervals-and-three-registers) does, splitting a bundle only at an actual conflict point, and it represents control-flow joins with block parameters instead of the phi nodes [C1](c1-instruction-selection.md#where-trees-stop-working) used for SSA.[^cranelift-regalloc2] Alongside the allocator sits a symbolic checker, an independent piece of code that abstractly interprets the allocator's own output and confirms every register and stack slot it assigned still respects the original program's live ranges, backed by five separate fuzz targets that feed it adversarial inputs.[^cranelift-regalloc2]
- **Output.** A buffer of machine instructions inside the compiling process, not text handed to an external assembler; Cranelift's selection and allocation passes exist to serve exactly this, code generated and checked entirely inside one running compiler.

The checker is worth pausing on, because it answers a question this book has asked about Vortex's own back end more than once: how do you know an allocator is correct, not only fast? [C3](c3-linear-scan.md#checking-an-allocation-independently) built an allocation checker as a differential test against the allocator itself; Cranelift's checker is the same idea, written once and run on every compile, not only in tests.

The use-count idea itself is small enough to model directly. The program below computes use counts once for a tiny three-instruction IR, then walks it backward, folding a shift into a load's address only when the shift's result is read in exactly one place:

--8<-- "includes/examples/backend/d3-real-backends/fold_by_use_count.cpp.md"

When the same shifted value feeds a second, independent load, its use count rises to two and the fold no longer applies: the shift has to be computed once and kept, exactly the choice Cranelift's own selector makes for every value in a real function.

??? check "Cranelift's selector matches patterns backward, from the end of a function to its start, in a single pass. Why does that order matter for deciding whether a value's producer can be folded into its one consumer?"

    Walking backward means the pass already knows, by the time it reaches
    a value's producing instruction, exactly how many places read that
    value: every consumer appears earlier in program order, which is later
    in the backward walk. Matching forward would reach the producer before
    knowing whether a later instruction will also need it, so a fold
    decision made early could turn out to be wrong.

## Go: a compiler that shows its own work

[C1](c1-instruction-selection.md#how-production-compilers-select) already named the shape of Go's `lower` pass in one sentence; the detail worth adding here is how much of it is generated rather than hand-written, and how easy Go makes it to watch.

- **IR.** Generic, architecture-independent SSA, the same kind [O3](../optimize/o3-ssa.md) builds for Vortex's own middle end, turned into architecture-specific SSA by the `lower` pass.[^go-ssa]
- **Selection.** Most of `lower`'s rewrite rules are not Go source at all: they are read from compact rule files under `_gen/`, one file family per architecture, and compiled into the rewrite functions that actually run.[^go-ssa] The rule table sits between hand-written code and a fully general table-driven matcher, closer in spirit to ISLE than to `madd` and register-offset addressing being written out by hand the way [C1](c1-instruction-selection.md#tiles-and-maximal-munch)'s tiles were.
- **Allocation.** Not detailed in the sources this chapter cites; Go's SSA `README` documents the passes leading up to and following allocation, not the allocator's own algorithm, and this book does not guess at what a source does not say.
- **Output.** Go's own assembler, whose calling convention, called `ABIInternal`, the documentation is explicit is unstable and not the platform ABI: it can change between Go releases, unlike AAPCS64 or the System V AMD64 ABI, which [A4](a4-calling-conventions.md) treats as fixed contracts every compiler on the platform must honor.[^go-abi]

The most useful thing to borrow from Go is not a data structure; it is a habit. Building the compiler with `GOSSAFUNC` set to a function's name writes an HTML file showing that function after every single pass, `lower` included, in the compiler's own intermediate form.[^go-ssa] Reading a pass list in documentation tells you what happens; a dump like this shows you that it happened, on the actual program you asked about, which is the difference between believing a design document and checking it.

A rule table small enough to read in full still shows what a generated `lower` pass buys over hand-written code: a driver applies the whole table, bottom-up, until a sweep makes no further change, rather than the driver's author deciding case by case which order to check patterns in.

--8<-- "includes/examples/backend/d3-real-backends/rule_table_rewrite.cpp.md"

Both expressions above reach their simplest form after one rewriting sweep, but the driver does not assume that in advance: it keeps sweeping until a sweep changes nothing, which is what lets the same table handle a tree that needs one sweep and a tree that would need five, without the table's author having to know which in advance.

## TCC and chibicc: small enough to read end to end

Two back ends short enough that "read the source" is a reasonable afternoon, not a project.

TCC compiles in a single pass with no separate IR at all: as its recursive-descent parser recognizes an expression, it emits code immediately, tracking where each intermediate result currently lives, a register or nothing yet, on a small **value stack** rather than in a tree it will walk again later.[^tcc] That is the same shape as [C1](c1-instruction-selection.md#one-instruction-per-node-and-its-price)'s macro expansion, one decision per piece of syntax with no lookahead, but pushed one step earlier: there is no IR to tile at all, because code generation and parsing are the same pass. TCC's allocation strategy is not detailed in the reference this chapter cites, and its developer's guide still describes the compiler as targeting mainly the 32-bit x86 architecture, a reminder that a `README`'s age is itself a fact worth noting before trusting anything else it says.[^tcc]

A value stack this small is easy to build and watch work. The program below parses and emits in one pass, over an expression with no IR behind it at all: a wholly constant subexpression folds away with no instruction emitted for it, and a subexpression that reads a run-time input emits exactly one instruction, at the moment the parser finishes recognizing it:

--8<-- "includes/examples/backend/d3-real-backends/value_stack_codegen.cpp.md"

chibicc has no separate IR either, and no separate reference document: its author built it as a sequence of commits, each one corresponding to a chapter of an accompanying book, so the source's own history is the documentation.[^chibicc] It targets x86-64 under the System V ABI and walks the abstract syntax tree directly, emitting assembly text as it recurses, growing from arithmetic to control flow to structs one commit at a time. Reading chibicc well means reading it commit by commit, in the order it was written, rather than opening the finished source cold: a diff between two adjacent commits is usually one new kind of node and the handful of lines that emit it, which is a far smaller reading unit than the whole file.

??? check "TCC and chibicc both skip building a separate IR. What do they give up by doing that, and what do they gain?"

    They give up everything C1's tiling and this chapter's use-count folding
    depend on: without an IR to look ahead in, neither compiler can see
    that a shift feeds a load's address, or that a multiply feeds an add,
    before it has already emitted code for each piece on its own. What
    they gain is a compiler with one less representation to build,
    maintain and keep in sync with the source language, and a much shorter
    path from source text to generated code, which is exactly why both are
    small enough to read in full.

[Figure 1](#fig-1) lines the five back ends up against the same four questions at once, with a dash wherever this chapter's sources do not say.

<figure class="vx-figure" id="fig-1">
<svg viewBox="0 0 900 400" role="img" aria-label="Four design questions, answered by five real back ends" aria-describedby="d3-chart-desc">
<title id="d3-chart-title">Four design questions, answered by five real back ends</title>
<desc id="d3-chart-desc">A table with one row per back end and three columns: IR form, instruction selection strategy, and register allocation strategy. QBE, highlighted as the recommended first read: an SSA IL in text form; amd64 addressing-mode matching; a spiller and a linear allocator kept as separate passes, with hinting and a loop-aware spill heuristic. Cranelift: CLIF lowered to VCode; one backward pass matching patterns written in the ISLE rule language; regalloc2, which backtracks over live-range bundles instead of committing one interval at a time. Go: generic SSA lowered to machine-specific SSA by a lower pass; rewrite rules read from generated rule files; its allocator is not detailed in this chapter's sources, marked with a dash. TCC: no separate IR, code emitted while parsing; a value stack tracks where each intermediate result lives; allocator not detailed, marked with a dash. chibicc: an abstract syntax tree with no separate IR; the tree is walked directly and assembly emitted as it recurses; allocator not detailed, marked with a dash.</desc>

<rect class="vx-box-strong" x="8" y="8" width="115" height="34" rx="4"/>
<text class="vx-text" x="65" y="30" text-anchor="middle">Back end</text>
<rect class="vx-box-strong" x="131" y="8" width="230" height="34" rx="4"/>
<text class="vx-text" x="246" y="30" text-anchor="middle">IR form</text>
<rect class="vx-box-strong" x="369" y="8" width="270" height="34" rx="4"/>
<text class="vx-text" x="504" y="30" text-anchor="middle">Instruction selection</text>
<rect class="vx-box-strong" x="647" y="8" width="245" height="34" rx="4"/>
<text class="vx-text" x="769" y="30" text-anchor="middle">Register allocation</text>

<g>
<rect class="vx-box-accent" x="8" y="52" width="115" height="60" rx="4"/>
<text class="vx-text" x="65" y="86" text-anchor="middle">QBE</text>
<rect class="vx-box" x="131" y="52" width="230" height="60" rx="4"/>
<text class="vx-text" x="246" y="72" text-anchor="middle">SSA IL,</text>
<text class="vx-text" x="246" y="90" text-anchor="middle">text form</text>
<rect class="vx-box" x="369" y="52" width="270" height="60" rx="4"/>
<text class="vx-text" x="504" y="72" text-anchor="middle">amd64 addressing-</text>
<text class="vx-text" x="504" y="90" text-anchor="middle">mode matching</text>
<rect class="vx-box" x="647" y="52" width="245" height="60" rx="4"/>
<text class="vx-text" x="769" y="66" text-anchor="middle">split spiller + linear</text>
<text class="vx-text" x="769" y="84" text-anchor="middle">allocator, hinted,</text>
<text class="vx-text" x="769" y="100" text-anchor="middle">loop-aware spilling</text>
</g>

<g>
<rect class="vx-box" x="8" y="118" width="115" height="60" rx="4"/>
<text class="vx-text" x="65" y="152" text-anchor="middle">Cranelift</text>
<rect class="vx-box" x="131" y="118" width="230" height="60" rx="4"/>
<text class="vx-text" x="246" y="138" text-anchor="middle">CLIF, lowered</text>
<text class="vx-text" x="246" y="156" text-anchor="middle">to VCode</text>
<rect class="vx-box" x="369" y="118" width="270" height="60" rx="4"/>
<text class="vx-text" x="504" y="138" text-anchor="middle">one backward pass,</text>
<text class="vx-text" x="504" y="156" text-anchor="middle">rules written in ISLE</text>
<rect class="vx-box" x="647" y="118" width="245" height="60" rx="4"/>
<text class="vx-text" x="769" y="138" text-anchor="middle">regalloc2: backtracks</text>
<text class="vx-text" x="769" y="156" text-anchor="middle">over live-range bundles</text>
</g>

<g>
<rect class="vx-box" x="8" y="184" width="115" height="60" rx="4"/>
<text class="vx-text" x="65" y="218" text-anchor="middle">Go</text>
<rect class="vx-box" x="131" y="184" width="230" height="60" rx="4"/>
<text class="vx-text" x="246" y="204" text-anchor="middle">generic SSA, then</text>
<text class="vx-text" x="246" y="222" text-anchor="middle">machine SSA via lower</text>
<rect class="vx-box" x="369" y="184" width="270" height="60" rx="4"/>
<text class="vx-text" x="504" y="204" text-anchor="middle">rewrite rules read from</text>
<text class="vx-text" x="504" y="222" text-anchor="middle">generated rule files</text>
<rect class="vx-box" x="647" y="184" width="245" height="60" rx="4"/>
<text class="vx-text-muted" x="769" y="220" text-anchor="middle">- not detailed here -</text>
</g>

<g>
<rect class="vx-box" x="8" y="250" width="115" height="60" rx="4"/>
<text class="vx-text" x="65" y="284" text-anchor="middle">TCC</text>
<rect class="vx-box" x="131" y="250" width="230" height="60" rx="4"/>
<text class="vx-text" x="246" y="270" text-anchor="middle">none: code emitted</text>
<text class="vx-text" x="246" y="288" text-anchor="middle">while parsing</text>
<rect class="vx-box" x="369" y="250" width="270" height="60" rx="4"/>
<text class="vx-text" x="504" y="270" text-anchor="middle">value stack tracks each</text>
<text class="vx-text" x="504" y="288" text-anchor="middle">result's current location</text>
<rect class="vx-box" x="647" y="250" width="245" height="60" rx="4"/>
<text class="vx-text-muted" x="769" y="286" text-anchor="middle">- not detailed here -</text>
</g>

<g>
<rect class="vx-box" x="8" y="316" width="115" height="60" rx="4"/>
<text class="vx-text" x="65" y="350" text-anchor="middle">chibicc</text>
<rect class="vx-box" x="131" y="316" width="230" height="60" rx="4"/>
<text class="vx-text" x="246" y="350" text-anchor="middle">AST only, no separate IR</text>
<rect class="vx-box" x="369" y="316" width="270" height="60" rx="4"/>
<text class="vx-text" x="504" y="336" text-anchor="middle">tree walked directly,</text>
<text class="vx-text" x="504" y="354" text-anchor="middle">grown commit by commit</text>
<rect class="vx-box" x="647" y="316" width="245" height="60" rx="4"/>
<text class="vx-text-muted" x="769" y="352" text-anchor="middle">- not detailed here -</text>
</g>

</svg>
<figcaption>Figure 1. The four questions this chapter asks of every back end, answered for five of them at once. A dash marks a cell this chapter's sources do not fill in: a real gap in the documentation this chapter cites, not a claim that the back end has no allocator.</figcaption>
</figure>

## A source that stopped being worth reading

One back end nearly joined this chapter's table and did not: LuaJIT, whose SSA intermediate representation was once documented on its project wiki. That page now redirects to the project's home page and says nothing about the IR; only comments scattered through the source describe it today. A citation to the old wiki URL would look, to a later reader clicking it, exactly like every working citation in this chapter's source list, until it silently lands somewhere else. The brief this book's other chapters follow treats a source as usable only once it has actually been opened and shown to say what it is cited for, and this is the reason: a design document's age is not a detail to skim past, it is part of whether the document still says anything at all.

## For Vortex

!!! vortex "Exercise"

    Pick one back end from this chapter whose register allocator is
    documented in enough depth to read closely: QBE's split spiller and
    linear allocator, or Cranelift's regalloc2. Read its own design
    document (QBE's IL reference and comparison page, or regalloc2's
    `ION.md`) alongside [C3](c3-linear-scan.md) and [C5](c5-spilling.md),
    and write a one-page note, not code, that answers three questions:

    1. What does this allocator do differently from the linear-scan
       algorithm C3 taught by hand, at the level of an actual step in the
       algorithm, not only a name?
    2. Which one idea from it would you adopt for Vortex's own back end,
       and why does it fit Vortex's shape: fixed array shapes known at
       compile time ([decision 43](../decisions/arrays.md#d43)), a ban on
       floating-point contraction ([decision 56](../decisions/numbers.md#d56))
       that no allocator or selector is allowed to violate by reassociating
       or fusing operations on its own, and the matmul kernel's `&mut`
       output parameter, read and written on every iteration of the
       innermost loop?
    3. Which one idea from it would you *not* adopt yet, and what would
       have to change about Vortex's own compiler, or its workload, before
       it would be worth the added complexity?

    **Not yet.** Do not implement regalloc2's backtracking or QBE's split
    spiller; [C3](c3-linear-scan.md) and [C5](c5-spilling.md) already set
    the allocator Vortex's v0.1 back end builds. This exercise is a reading
    and judgment exercise, not an implementation one.

    **Done when** your note names the specific document you read (not only
    "the QBE website" or "the regalloc2 repository"), answers all three
    questions with a real step from the algorithm rather than a
    restatement of its name, and would let a teammate who has read C3 and
    C5 but not your source understand what is actually different about it.

## Key ideas

!!! recap "You can now answer"

    - **What four questions does this chapter ask of every back end?** What its IR looks like, how it selects instructions, how it allocates registers, and where its generated code meets the rest of the toolchain.
    - **What does QBE keep separate that C3's linear scan does not?** Its spiller and its allocator: two passes instead of one, so the spiller can use whole-function information, such as loop depth, before any register is assigned.
    - **What does Cranelift's selector compute before it starts matching, and why does it walk backward?** Use counts for every value, computed once up front; walking backward means it already knows, by the time it reaches a value's producer, how many places will read that value, so a fold decision is never made too early.
    - **What replaces hand-written pattern-matching code in both Cranelift and Go?** A rule table: ISLE, a typed term-rewriting language compiled ahead of time, for Cranelift; a generated rewrite-rule file for Go's `lower` pass.
    - **What do TCC and chibicc give up by having no separate IR, and what do they gain?** They cannot fold a computation into a later instruction the way a tiled or use-count-driven selector can, because nothing has looked ahead; they gain a compiler with one fewer representation to build and keep in sync with the language.
    - **Why does a source's age matter as much as its content?** A stale document can describe a target the project has since dropped, or point at a page that no longer exists; LuaJIT's SSA wiki page now redirects away with nothing left to read, which nothing about the page's writing style would have warned you of.

## Where this comes back

!!! next "You will use this again in"

    - [E1. The LLVM code generator pipeline](e1-llvm-codegen-pipeline.md): *the same four questions, asked of LLVM's own SelectionDAG and GlobalISel*
    - [E2. Describing a target](e2-describing-a-target.md): *TableGen as a generated, declarative description, the same role ISLE and Go's rule files play*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *LLVM's greedy allocator, read against regalloc2 and QBE's split allocator*
    - [E4. Testing back ends](e4-testing-backends.md): *Cranelift's symbolic checker and fuzz targets as a model for testing an allocator you write yourself*

## Sources and further reading

QBE's home page and IL reference are short enough to read in one sitting and are the best next stop after this chapter; Cranelift's four posts, read in the order cited below, cover pattern matching, correctness, the allocator and the rule language in turn.

[^qbe-home]: QBE project, "QBE: a simple compiler backend", home page. <https://c9x.me/compile/>
[^qbe-vs-llvm]: QBE project, "QBE vs LLVM". <https://c9x.me/compile/doc/llvm.html>
[^qbe-il]: QBE project, "QBE IL reference". <https://c9x.me/compile/doc/il.html>
[^hjort]: Gabriel Hjort Blindell, "Survey on Instruction Selection: An Extensive and Modern Literature Study", arXiv:1306.4898. <https://arxiv.org/abs/1306.4898>
[^cranelift-isel1]: Chris Fallin, "Cranelift's Instruction Selector, Part 1: Pattern-matching", 18 September 2020. <https://cfallin.org/blog/2020/09/18/cranelift-isel-1/>
[^cranelift-isel3]: Chris Fallin, "Correctness in Register Allocation" (Cranelift's instruction selector, part 3), 15 March 2021. <https://cfallin.org/blog/2021/03/15/cranelift-isel-3/>
[^cranelift-regalloc2]: Chris Fallin, "Cranelift's New Register Allocator, Part 4" (regalloc2), 9 June 2022; and Bytecode Alliance, regalloc2, `doc/ION.md` design document. <https://cfallin.org/blog/2022/06/09/cranelift-regalloc2/> ; <https://github.com/bytecodealliance/regalloc2/blob/main/doc/ION.md>
[^cranelift-isle]: Chris Fallin, "Cranelift's ISLE, Part 4: The ISLE Language", 20 January 2023. <https://cfallin.org/blog/2023/01/20/cranelift-isle/>
[^go-ssa]: The Go Authors, "SSA Backend", `cmd/compile/internal/ssa/README.md`: the `lower` pass, its generated rewrite rules, and `GOSSAFUNC`. <https://github.com/golang/go/blob/master/src/cmd/compile/internal/ssa/README.md>
[^go-abi]: The Go Authors, Go source, `cmd/compile/abi-internal.md`. <https://github.com/golang/go/blob/master/src/cmd/compile/abi-internal.md>
[^tcc]: Fabrice Bellard and the TinyCC project, "tcc-doc: TinyCC Reference Documentation" (developer's guide). <https://bellard.org/tcc/tcc-doc.html>
[^chibicc]: Rui Ueyama, chibicc, README. <https://github.com/rui314/chibicc>
