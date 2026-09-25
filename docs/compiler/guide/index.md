# Building the Vortex compiler

<p class="page-intro">A stage-by-stage reading guide. Each stage says what the compiler needs to have, what it can safely leave out for now, and how you can tell the stage is finished. It does not tell you how to write the code; that part is yours.</p>

This guide is for someone who can already program but has never built a
compiler. Every technical word is explained the first time it appears, and the
[word list](words.md) collects all of them in one place. Where the guide says
how other people have approached a stage, it cites the book or tutorial it is
drawing on, so you can go and read the original.

The guide follows the same twelve milestones as the
[implementation roadmap](../../roadmap.md). The roadmap is the checklist. This
guide is the reading that explains why each item on the checklist is there.

## What a compiler is

A **compiler** is a program that translates other programs. It reads text that
a person wrote, works out what that text means, and writes out instructions
that a computer's processor can carry out directly. The text going in is the
**source code**. The instructions coming out are **machine code**, and a file
of machine code that the operating system can start is an **executable**.

Translation is only half the job. A compiler also refuses to translate programs
that do not make sense, and it explains the problem well enough for the
programmer to fix it. For Vortex, that second job matters as much as the first.
The [specification](../../specification/index.md) lists many programs that
must be rejected, and each rejection needs a clear message pointing at the
right place in the source.

## The shape of the whole thing

Robert Nystrom describes the path through a compiler as a climb up one side of
a mountain and down the other.[^ci-map] On the way up, the compiler takes flat
text and builds a richer and richer understanding of it. At the top it holds a
complete, checked description of the program. On the way down, it turns that
description into something more and more like the target machine, until only
machine code is left.

<figure class="vx-figure">
<svg viewBox="0 0 760 320" role="img" aria-labelledby="map-title map-desc">
<title id="map-title">The Vortex compiler drawn as a mountain</title>
<desc id="map-desc">The left slope climbs from source text through the lexer, parser, name resolution and type checking to a checked program at the peak. The right slope descends through lowering and code generation to an executable.</desc>
<path class="vx-line" d="M40 280 L380 60 L720 280"/>
<line class="vx-line" x1="20" y1="280" x2="740" y2="280"/>
<text class="vx-text-muted" x="120" y="302">front end: understand the program</text>
<text class="vx-text-muted" x="440" y="302">back end: rebuild it for the machine</text>
<circle class="vx-box-strong" cx="40" cy="280" r="7"/>
<text class="vx-text" x="52" y="270">Source text</text>
<circle class="vx-box" cx="108" cy="236" r="7"/>
<text class="vx-text" x="40" y="216" text-anchor="start">Lexer</text>
<circle class="vx-box" cx="176" cy="192" r="7"/>
<text class="vx-text" x="104" y="172">Parser</text>
<circle class="vx-box" cx="244" cy="148" r="7"/>
<text class="vx-text" x="118" y="140">Name resolution</text>
<circle class="vx-box" cx="312" cy="104" r="7"/>
<text class="vx-text" x="166" y="96">Type and rule checks</text>
<circle class="vx-box-accent" cx="380" cy="60" r="9"/>
<text class="vx-text-accent" x="380" y="36" text-anchor="middle">Checked program</text>
<circle class="vx-box" cx="482" cy="126" r="7"/>
<text class="vx-text" x="496" y="122">Lowering</text>
<circle class="vx-box" cx="584" cy="192" r="7"/>
<text class="vx-text" x="598" y="188">Code generation</text>
<circle class="vx-box-strong" cx="720" cy="280" r="7"/>
<text class="vx-text" x="708" y="264" text-anchor="end">Executable</text>
<circle class="vx-dot" r="6">
<animateMotion dur="9s" repeatCount="indefinite" path="M40 280 L380 60 L720 280" keyPoints="0;0;0.5;0.5;1;1" keyTimes="0;0.06;0.46;0.54;0.94;1" calcMode="linear"/>
</circle>
</svg>
<figcaption>Figure 1. The stages of the Vortex compiler, drawn after the mountain in Nystrom's "A Map of the Territory". The dot is one program making the trip. Everything on the left slope is about understanding; everything on the right is about producing.</figcaption>
</figure>

The left slope is usually called the **front end** and the right slope the
**back end**. Nystrom puts it this way: the front end is "specific to the source
language", while the back end is concerned with "the final architecture where
the program will run".[^ci-map] Between them sits some form of
**intermediate representation**, or **IR**: a way of writing the program down
that is no longer Vortex source text but is not yet machine code either.

Having a middle point pays off later. If Vortex one day runs on a GPU, the
whole left slope stays the same. Only the right slope needs a second path.
The [architecture page](../architecture.md) says the same thing as a rule: GPU
work "must reuse the same validated language semantics".

## Follow one line through the compiler

The roadmap's first end-to-end test is a program that computes `2 + 3 * 4` and
prints `14`. Step through what happens to its middle line. Each step shows what
one stage receives and what it hands to the next.

<div class="vx-stepper" markdown="1">
<div class="vx-step" markdown="1">

**Step 1. Source text**

```vortex
// statements: valid
let result = 2 + 3 * 4;
```

To the compiler, this starts life as nothing more than a row of characters:
`l`, `e`, `t`, a space, `r`, and so on. Nothing yet knows that `let` is a
keyword or that `3 * 4` should happen before the addition.

</div>
<div class="vx-step" markdown="1">

**Step 2. Tokens (after the lexer)**

| Token kind | Spelling |
| --- | --- |
| keyword | `let` |
| identifier | `result` |
| operator | `=` |
| integer literal | `2` |
| operator | `+` |
| integer literal | `3` |
| operator | `*` |
| integer literal | `4` |
| punctuation | `;` |

The **lexer** groups characters into **tokens**, the words and symbols of the
language. Spaces are gone. Each token remembers where it came from in the file,
so that later stages can point at it in an error message.

</div>
<div class="vx-step" markdown="1">

**Step 3. A syntax tree (after the parser)**

```text
let result
└── +
    ├── 2
    └── *
        ├── 3
        └── 4
```

The **parser** arranges the tokens into a **syntax tree** that follows the
grammar. The multiplication sits lower in the tree than the addition, which is
how the tree records that `3 * 4` is worked out first. No brackets were needed
in the source because the grammar's precedence rules decide this.

</div>
<div class="vx-step" markdown="1">

**Step 4. Names connected (after name resolution)**

The declaration introduces a new name, `result`, into the current scope. When a
later line says `print(result)`, name resolution connects that use to this
declaration. If the later line had misspelled it as `resutl`, this is the
stage that reports an unknown name.

</div>
<div class="vx-step" markdown="1">

**Step 5. Types checked (after type checking)**

All three literals are integer literals with no other type demanded, so each
one takes the default type `i32`, as the
[types chapter](../../specification/types-and-values.md) requires. `*` and `+`
on two `i32` values produce an `i32`, so `result` is inferred to be an `i32`.
Nothing in the program needs to say so.

</div>
<div class="vx-step" markdown="1">

**Step 6. Lowered (into an intermediate representation)**

```text
t1 = multiply 3, 4
t2 = add 2, t1
store t2 into result
```

**Lowering** rewrites the checked tree as a list of small, ordered steps. The
tree shape is gone; the order of work is now explicit. This listing is only a
sketch to show the idea. Vortex has not chosen an IR, and the real one may look
quite different.

</div>
<div class="vx-step" markdown="1">

**Step 7. Machine code, then running it**

The **code generator** turns those steps into instructions for a real
processor. A **linker** joins that code with Vortex's small **runtime**
library, which supplies things like `print`, and writes the executable. When
the executable runs, it prints `14`.

</div>
</div>

## Why build it in stages

Most older textbooks explain a compiler one pass at a time, in the order the
passes run: all of scanning, then all of parsing, and so on. Abdulaziz Ghuloum
argued that this makes it easy to lose the big picture, because the reader
does not see a working compiler until the very end.[^ghuloum] His alternative
was to grow the compiler in small steps where "every step yields a fully
working compiler for a progressively expanding subset" of the
language.[^ghuloum] Nora Sandler's C compiler series is built the same way and
names Ghuloum's paper as its model.[^sandler-blog] Its first program simply
returns the number 2.

The LLVM tutorial takes a middle road. It builds a small language called
Kaleidoscope across ten chapters, adding one topic per chapter, so that you
can "skip ahead as you wish".[^kal]

The Vortex roadmap is closer to the traditional order. It finishes the front
end (milestones 2 to 5) before generating any code (milestone 6). There is a
good reason for this. Vortex's main promise is that bad programs are rejected
with clear errors before they run, and that promise lives almost entirely in
the front end. The roadmap still leaves room for Ghuloum's idea: "work from a
later milestone may be prototyped", as long as it does not weaken the rules of
an earlier one. A thin path that compiles one tiny program all the way to an
executable is a fine early experiment. Just do not let it become the reason a
front-end rule gets skipped.

## How each stage page is laid out

Every stage page has the same parts, in the same order, so you always know
where to look.

- **Before you start, remember.** A few questions on earlier ideas the stage
  builds on, each with a link to where it was taught.
- **In this stage.** The stage's goals, in a few lines.
- **What this stage is for.** A plain description of the stage's job.
- **Words for this stage.** The new terms, each explained in a sentence or two.
- **The stage's ideas.** The sections that teach the stage's work, with at
  least one figure (often animated), short C++ examples that solve a smaller,
  different problem, and check questions to answer before you open them.
- **What you need to have.** The things the stage must provide before you move
  on.
- **What you do not need yet.** Things that are tempting to build now but
  belong to a later stage or to a later version of Vortex.
- **How you know it is finished.** The evidence, matched to the roadmap
  milestone.
- **Traps.** Mistakes that are easy to make at this stage.
- **Key ideas.** Questions you can now answer, each with a one-line answer.
- **Where this comes back.** The later pages that use the stage's ideas.
- **How others teach this stage.** Where to read about the same stage in
  Kaleidoscope, *Crafting Interpreters* and other sources.

## The stages

| Stage | What it adds | Roadmap |
| --- | --- | --- |
| [0. The workbench](stage-0-workbench.md) | A compiler command, a build, and a test runner | Milestone 0 |
| [1. Source text and error messages](stage-1-source-and-diagnostics.md) | Reading files, tracking positions, reporting problems | Milestone 1 |
| [2. The lexer](stage-2-lexer.md) | Characters become tokens | Milestone 2 |
| [3. The parser and the syntax tree](stage-3-parser-and-tree.md) | Tokens become a tree | Milestone 3 |
| [4. Names and scopes](stage-4-names-and-scopes.md) | Every name finds its declaration | Milestone 4 |
| [5. Types and language rules](stage-5-types-and-rules.md) | Every value has a type; bad programs stop here | Milestone 5 |
| [6. The first machine code](stage-6-first-machine-code.md) | An IR, a back end, a runtime, an executable | Milestone 6 |
| [7. Functions and control flow](stage-7-functions-and-control-flow.md) | Calls, branches and loops in generated code | Milestone 7 |
| [8. Data in memory](stage-8-data-in-memory.md) | Strings, arrays, structs and references | Milestone 8 |
| [9. Runtime safety](stage-9-runtime-safety.md) | Checks that stop a program before it does harm | Milestone 9 |
| [10. Matrix multiplication](stage-10-matrix-multiplication.md) | The first real Vortex program | Milestone 10 |
| [11. Release](stage-11-release.md) | Proving that all of it works together | Milestone 11 |

Where a stage meets a language rule, it links the
[decision record](../../decisions/index.md) that explains why the rule was
chosen. Where it leaves a choice to you, it links the suggested default in
[Implementation choices](../../decisions/implementation.md), which you may
follow or replace, as long as you write your choice down.

## How to use the sources

The guide leans on a small set of sources, listed with notes in the
[reading list](reading-list.md). Two are worth knowing from the start.

The LLVM **Kaleidoscope** tutorial is written in C++, like the Vortex compiler,
and it "assumes you know C++, but no previous compiler experience is
necessary".[^kal] It is honest about its shortcuts: it says it does "not show
best practices in software engineering principles" so that it can stay
focused.[^kal] Read it for the ideas, not as a model for how Vortex code should
be organized.

*Crafting Interpreters* builds two complete implementations of one language and
explains every step in plain prose.[^ci] It builds interpreters rather than a
compiler to machine code, but its chapters on scanning, parsing, and resolving
names apply to Vortex almost directly.

When a source and a Vortex document disagree, the Vortex document wins. The
[specification](../../specification/index.md) defines the language, and the
[conformance chapter](../../specification/conformance.md#11-document-authority)
sets the order of authority between the documents.

[^ci-map]: Robert Nystrom, *Crafting Interpreters*, chapter "A Map of the Territory". <https://craftinginterpreters.com/a-map-of-the-territory.html>
[^ci]: Robert Nystrom, *Crafting Interpreters* (2021). <https://craftinginterpreters.com/>
[^ghuloum]: Abdulaziz Ghuloum, "An Incremental Approach to Compiler Construction", *Proceedings of the 2006 Scheme and Functional Programming Workshop*, University of Chicago Technical Report TR-2006-06. <http://scheme2006.cs.uchicago.edu/11-ghuloum.pdf>
[^sandler-blog]: Nora Sandler, "Writing a C Compiler, Part 1", 29 November 2017. <https://norasandler.com/2017/11/29/Write-a-Compiler.html>
[^kal]: LLVM Project, "My First Language Frontend with LLVM Tutorial". <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html>
