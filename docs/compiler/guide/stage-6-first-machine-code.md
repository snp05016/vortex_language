# 6. The first machine code

<p class="page-intro">This stage carries a checked program over the peak and down the other side. It adds an intermediate representation, a back end, a small runtime and a link step, and it ends with an executable that prints 14.</p>

Everything up to [stage 5](stage-5-types-and-rules.md) was about
understanding. The compiler read the source, built a tree, connected names,
checked types and rules, and rejected anything ill-formed. Nothing it produced
could run. This stage is the first time the compiler makes something a
processor can execute.

On the [overview's mountain figure](index.md#the-shape-of-the-whole-thing),
this is the whole right slope, drawn thin. The goal is not a good back end.
The goal is a complete path from source file to running program, for the
smallest useful piece of Vortex, so that every later stage has somewhere to
land.

The roadmap is precise about how small that piece is. Milestone 6 asks for
`main`, literals, arithmetic, variables, `return`, and the call to `print` in
one test program. Functions and branches wait for
[stage 7](stage-7-functions-and-control-flow.md). Arrays, strings and structs
wait for [stage 8](stage-8-data-in-memory.md).

--8<-- "includes/remember/compiler__guide__stage-6-first-machine-code.md"

!!! goals "In this stage"

    - Explain why an intermediate representation sits between the front end
      and the back end.
    - Compare what generating LLVM IR, generating C and generating assembly
      each give up and gain.
    - Trace a checked program through lowering, an object file and a link
      step to a running executable.
    - Recognize why static single assignment does not have to be built yet.
    - State the exact output and exit-status contract the first end-to-end
      test checks.

## What this stage is for

[Milestone 6](../../roadmap.md#milestone-6-basic-cpu-code-generation) has four
bullets:

- generate CPU code for `main`, literals, arithmetic, variables and `return`;
- emit an object file or executable using the chosen back end;
- link the generated code with Vortex's small runtime;
- run the generated program and capture its output and its exit status (0
  when `main` returns).

And one program that has to work:

```vortex
// program: valid
fn main() {
    let result = 2 + 3 * 4;
    print(result);
}
```

The milestone is complete when this compiles, runs, prints `14`, and exits
with status 0.

The [architecture page](../architecture.md) gives the lowering pass one
firm rule. Its input is a "validated program", and it must not "accept
programs rejected by earlier phases". The back end is downstream of the gate
built in stage 5. It never sees a program that failed a check, and it must not
re-decide anything the front end already settled, such as the type of
`result`.

## Words for this stage

intermediate representation (IR)
: A way of writing the program down that is no longer Vortex source and not
  yet machine code. It sits between the front end and the back end.

lowering
: Rewriting a program in a lower-level form: from the checked tree to an IR,
  or from an IR to something closer to the machine.

front end
: The part of the compiler that reads and understands the source language:
  stages 1 to 5 of this guide.

back end
: The part of the compiler that produces code for a particular machine.

target
: The kind of machine and operating system the generated program is meant to
  run on.

instruction
: One basic operation a processor can carry out, such as adding two numbers or
  jumping to another place in the code.

register
: A small, very fast storage slot inside the processor. Instructions mostly
  work on values held in registers.

assembly language
: A text form of machine instructions, one instruction per line, readable by
  people. An **assembler** turns it into machine code.

object file
: A file of machine code that is not yet a complete program. It may use names,
  such as `print`, whose code lives somewhere else.

linker
: The tool that joins object files and libraries into one executable, filling
  in each name with the address of the code it refers to.

executable
: A file the operating system can start as a program.

runtime library
: A small body of code, supplied with the language rather than written by the
  programmer, that generated programs call for services such as printing.

entry point
: The place where a program starts running. For a Vortex program, the
  language-level entry point is `main`.

standard output
: The text stream a program writes to by default, usually shown in the
  terminal. Tests capture it to compare with the expected output.

exit status
: A small number a program hands back to the operating system when it ends,
  used to report success or failure.

static single assignment (SSA)
: A style of IR in which every named value is assigned exactly once. A
  variable that changes becomes a series of separately named versions.

phi
: In SSA, a marker at a point where two paths meet that picks which version of
  a value to use, depending on which path was taken.

pass
: One trip over the program that does one job, such as checking types or
  lowering to IR.

toolchain
: The set of outside programs the compiler depends on to finish its work, such
  as an assembler, a linker, or a C compiler.

## Why an IR sits in the middle

You could, in principle, walk the checked tree and print machine instructions
directly. Very small compilers do. Most compilers put an intermediate
representation in between, and Robert Nystrom's overview gives the main
reason. A shared IR means one front end per source language and one back end
per target, instead of a separate compiler for every pairing of the two.[^ci-map]

Vortex has only one source language, so that exact argument applies in a
narrower way. It has more than one future target. The
[architecture page](../architecture.md) says that GPU lowering is later work
and that it "must reuse the same validated language semantics rather than
creating a second interpretation of Vortex". An IR is where that promise is
kept. Everything above it is shared. Only the part below it changes.

<figure class="vx-figure">
<svg viewBox="0 0 760 340" role="img" aria-labelledby="t6-ir-title t6-ir-desc">
<title id="t6-ir-title">One front end, one IR, several possible back ends</title>
<desc id="t6-ir-desc">The front end, covering stages 1 to 5, flows into an intermediate representation. From the IR, three possible back ends branch out: generating LLVM IR, generating C source, and generating assembly directly. A fourth, dimmer branch marked later leads to a GPU path.</desc>
<rect class="vx-box-strong" x="20" y="135" width="180" height="70" rx="4"/>
<text class="vx-text" x="110" y="163" text-anchor="middle">Front end</text>
<text class="vx-text-muted" x="110" y="184" text-anchor="middle">stages 1 to 5</text>
<rect class="vx-box-accent" x="270" y="135" width="160" height="70" rx="4"/>
<text class="vx-text" x="350" y="163" text-anchor="middle">Vortex IR</text>
<text class="vx-text-muted" x="350" y="184" text-anchor="middle">not chosen yet</text>
<line class="vx-flow" x1="200" y1="170" x2="262" y2="170"/>
<polygon class="vx-arrowhead" points="262,165 270,170 262,175"/>
<path class="vx-flow" d="M430 160 L470 160 L470 55 L512 55"/>
<polygon class="vx-arrowhead" points="512,50 520,55 512,60"/>
<path class="vx-flow" d="M430 170 L512 170"/>
<polygon class="vx-arrowhead" points="512,165 520,170 512,175"/>
<path class="vx-flow" d="M430 180 L470 180 L470 285 L512 285"/>
<polygon class="vx-arrowhead" points="512,280 520,285 512,290"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 3">
<rect class="vx-box" x="520" y="25" width="220" height="60" rx="4"/>
<text class="vx-text" x="630" y="50" text-anchor="middle">Generate LLVM IR</text>
<text class="vx-text-muted" x="630" y="70" text-anchor="middle">LLVM makes the machine code</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 3">
<rect class="vx-box" x="520" y="140" width="220" height="60" rx="4"/>
<text class="vx-text" x="630" y="165" text-anchor="middle">Generate C</text>
<text class="vx-text-muted" x="630" y="185" text-anchor="middle">a C compiler finishes the job</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 3">
<rect class="vx-box" x="520" y="255" width="220" height="60" rx="4"/>
<text class="vx-text" x="630" y="280" text-anchor="middle">Generate assembly</text>
<text class="vx-text-muted" x="630" y="300" text-anchor="middle">you write every instruction</text>
</g>
<line class="vx-line" x1="350" y1="205" x2="350" y2="270"/>
<text class="vx-text-muted" x="350" y="290" text-anchor="middle">later: a GPU path</text>
<text class="vx-text-muted" x="350" y="306" text-anchor="middle">from the same IR</text>
<text class="vx-text-muted" x="20" y="30">understands Vortex</text>
<text class="vx-text-muted" x="20" y="48">never changes per target</text>
</svg>
<figcaption>Figure 1. The front end ends in an intermediate representation. Below it, the back end is a replaceable part. Any one of the three options on the right can turn the IR into machine code, and a later GPU path would start from the same IR instead of from the source.</figcaption>
</figure>

??? check "Why does an IR matter for Vortex, given that it has only one source language?"

    Not for Nystrom's front-end-times-back-end argument, since Vortex only has
    one front end. It matters because Vortex plans more than one target: a
    CPU back end now, a GPU path later. The IR is the one place where everything
    above stays shared and only the part below changes, which is what lets a
    later GPU path reuse the checked program instead of building a second
    interpretation of Vortex.

Vortex has not chosen its IR. The step-through on the
[overview page](index.md#follow-one-line-through-the-compiler) shows a sketch
of three lines for `let result = 2 + 3 * 4;`, and it says plainly that the
real one may look quite different. Whatever it becomes, for this milestone it
has to be able to say a few things clearly:

- the order in which work happens, which the tree only implied;
- the type of every value, because adding two `i32` values and adding two
  `f32` values are different instructions on a real processor;
- where each variable's value lives;
- a call to a function the compiler did not generate, `print`;
- where `main` begins and ends.

## Lowering the first program

**Lowering** is the step that turns the checked tree into IR. In the tree for
`2 + 3 * 4`, the multiplication sits below the addition. Nothing in the tree
says "do the multiplication first"; that is implied by the shape. After
lowering, the order is written out: multiply, then add, then store. When both
operands of an operator need work, the language fixes which comes first: the
left operand is evaluated before the right, so in `f() + g()` the call to `f`
happens first ([decision 38](../../decisions/statements.md#d38)). Lowering
writes the steps in that order.

The only facts lowering needs are ones earlier stages already worked out. The
parser decided the shape, name resolution connected `result` in `print(result)`
to its declaration, and type checking decided that every value in the line is
an `i32`. Lowering reads those answers. It does not make up new ones.

The next example lowers a calculator expression, `-(3 + 4) * 2 - 1`, outside
any compiler, to show the same shape on a tree with a unary operator and a
subtraction. Each operator node becomes one instruction, in an order
fixed by visiting children before parents, and each instruction names its
inputs either as a literal or as an earlier instruction's result.

--8<-- "includes/examples/build-v0.1/stage-6-first-machine-code/lower_expr.cpp.md"

The same rule covers floating-point values. The
[types chapter](../../specification/types-and-values.md#44-floating-point-values)
requires each `f32` or `f64` operation to be one IEEE 754 operation, rounded
to nearest with ties to even: no fused multiply-add, no reordering, no extra
precision and no flush-to-zero ([decision](../../decisions/numbers.md#d56)).
Whatever back end you choose, every setting that relaxes one of those stays
off. Floating-point division needs no check: dividing by zero gives an
infinity or NaN, as IEEE 754 specifies
([decision 24](../../decisions/numbers.md#d24)).

## Choosing a back end

This is the largest open decision in the roadmap. Milestone 6 says "using the
chosen backend" and never says which one. The docs do not choose, so neither
does this guide. What follows is what each common option gives and what it
costs, so that the choice can be made on purpose.

### Generating LLVM IR

LLVM is a large, widely used compiler back end. Its input is a language of its
own, LLVM IR, which the reference manual describes as a "universal IR" of
sorts, low enough that many languages can be mapped onto it.[^langref] The
Kaleidoscope tutorial generates LLVM IR from its syntax tree in chapter 3, and
uses a checker that LLVM provides to catch malformed IR before it goes any
further.[^kal3] In chapter 8 it asks LLVM for an object file for the current
machine, and links that object file with a small C++ program to make an
executable.[^kal8]

What it gives: machine code for many processors from one back end, a
well-tested optimizer for after v0.1, object files without writing your own
encoder, and a large body of documentation, including a book-length guide to
how common language features map onto LLVM IR.[^mapping]

What it costs: a very large dependency. LLVM's own getting-started guide says
an LLVM-only debug build needs about 1 to 3 GB of disk space, and it needs a
recent CMake.[^llvm-start] That weight lands on Milestone 0's promise that one
command builds the compiler. LLVM IR must also be in SSA form, which the next
section explains.

### Generating C

Instead of machine code, the compiler can write out a C source file and hand
it to an existing C compiler. Nystrom's overview describes this family of
compilers, which produce source code in another language and then rely on
that language's existing tools.[^ci-map]

What it gives: portability to anywhere a C compiler runs, readable output you
can inspect, and no machine-level detail in your own code.

What it costs: the generated C must say exactly what Vortex means, not what C
happens to mean for similar-looking code. Vortex's rules on integer overflow,
division and conversions are strict (see the
[runtime and numerical rules](../../language-tour/06-runtime-and-numerical-rules.md)),
so the generated code must spell out those checks itself rather than
inheriting C's behavior for its operators. The same goes for floating point:
by default
[GCC](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html#index-ffp-contract)
fuses `a * b + c` into one fused multiply-add outside strict ISO modes, and
[Clang](https://clang.llvm.org/docs/UsersManual.html#cmdoption-ffp-contract)
does so within an expression, so compile the generated C with contraction off,
for example `-ffp-contract=off`, and without fast-math options. Errors from the
C compiler, if any slip through, will talk about generated C lines, not Vortex
lines. And you now
depend on a C compiler being present at every use of `vortex`.

??? check "Your back end writes C, and the C it writes for `a * b + c` on `f64` values is correct line by line. Compiled with the C compiler's default settings, what can still break?"

    The floating-point rule. By default GCC may fuse the multiply and the add
    into one fused multiply-add outside strict ISO modes, and Clang may do so
    within an expression. A fused operation rounds once instead of twice, so
    the result can differ from two separate IEEE 754 operations, which the
    types chapter requires. Compile the generated C with contraction off
    (`-ffp-contract=off`) and without fast-math options.

### Generating assembly directly

The compiler can also write assembly language for one processor and let an
assembler and linker finish the job. This is how Abdulaziz Ghuloum's
incremental compiler works: it writes assembly code for Intel x86 processors,
and every step produces real code that runs on the hardware.[^ghuloum] Nora Sandler's C compiler series does the same, assembling
and linking its output with `gcc` and checking the result through the exit
code.[^sandler-blog]

What it gives: nothing is hidden. You see every instruction, the dependency
list is short, and you learn what the machine is actually doing.

What it costs: one back end per processor family. You handle registers,
calling conventions and instruction choice yourself, and there is no optimizer
unless you write one. v0.1 does not need an optimizer, so the last point
matters less now than it will later.

### A note for later

For the eventual tensor and GPU work, the MLIR project's Toy tutorial is worth
knowing about. It builds a small language with tensor values on top of MLIR
and shows how to lower it in steps, ending at LLVM.[^toy] It is not needed for
v0.1, and it is not a reason to pick any option above today.

### What the decision must settle

Whichever option wins, the decision record should answer:

- which processors and operating systems v0.1 supports, all of them 64-bit,
  since `usize` is 64 bits on every v0.1 target
  ([decision 42](../../decisions/numbers.md#d42));
- whether the compiler writes an object file and calls a system linker, or
  produces the executable itself;
- which outside tools must be installed, and how the one-command build and
  test from Milestone 0 finds them;
- what language the runtime is written in, and how it is built and linked;
- how the choice leaves room for the later GPU path the architecture page
  requires;
- how floating-point settings are pinned so results match the spec.

The choice itself stays yours:
[implementation choice I1](../../decisions/implementation.md#i1) prefers no
back end. As a default, it suggests that the written decision show five
things: each `f32` and `f64` operation is one IEEE 754 operation, with
contraction and fast-math options off; a program exits with status 0 when
`main` returns and with 101 after a runtime error; only 64-bit targets are
supported; Milestone 0's one command still builds and tests the project from a
fresh copy; and the targets and back end are listed among the
implementation-defined behaviors.

## SSA, if you use LLVM

If you choose LLVM, you will meet **static single assignment** form
immediately. The Kaleidoscope tutorial states it without softening: LLVM
requires its input in SSA form and has no other mode.[^kal7]

The idea is simple to state. In SSA, every named value is assigned exactly
once.[^mapping] A Vortex variable that changes does not fit that rule
directly, so it becomes a series of versions:

```text
let mut total = 1;          total.1 = 1
total = total + 2;          total.2 = total.1 + 2
print(total);               print(total.2)
```

Straight-line code like this is easy. The harder case is where two paths meet,
for example after an `if` in which only one branch changed `total`. After the
join, which version is current depends on which path ran. SSA answers with a
**phi**, a marker at the join that picks the right version according to the
path taken.[^mapping]

Building SSA with phis correctly is a well-studied problem. The classic paper
is by Cytron and colleagues, "Efficiently Computing Static Single Assignment
Form and the Control Dependence Graph", from 1991.[^cytron] A later paper by
Braun and colleagues presents a simpler method that translates directly from a
syntax tree into SSA without any analysis beforehand.[^braun]

You may not need either. Kaleidoscope chapter 7 describes the usual shortcut
for front ends that target LLVM: keep each mutable variable in memory on the
stack, and let an LLVM pass called mem2reg turn those memory slots into SSA
values afterwards.[^kal7] That chapter lists conditions for the pass to work,
among them that the slots are created at the very start of the function. The
*Mapping High Level Constructs* guide recommends the same approach.[^mapping]

For Milestone 6 none of this bites yet. The first program has no branches, so
there are no joins and no phis. It starts to matter in stage 7, when `if` and
`while` arrive.

??? check "Why does Milestone 6 not need to build or reason about a single phi?"

    A phi is only needed at a join point, where two control-flow paths meet
    holding different versions of a variable. The first program has no
    branches, so it has exactly one path and no joins. Phis become necessary
    starting in stage 7, once `if` and `while` create paths that can meet
    again.

## From object file to executable

Most compilers do not produce the final executable alone. They produce an
**object file**, which holds the machine code for the program but still has
holes in it. The call to `print` is one such hole: the object file says "call
the thing named `print`" without containing `print` itself. The **linker**
fills the holes by joining the object file with the runtime library, and
writes the executable.

<figure class="vx-figure">
<svg viewBox="0 0 760 260" role="img" aria-labelledby="t6-link-title t6-link-desc">
<title id="t6-link-title">From source file to printed output</title>
<desc id="t6-link-desc">At compile time a source file goes into the Vortex compiler, which writes an object file. At link time the linker joins the object file with the runtime library to make an executable. At run time the executable prints 14 to standard output and ends with exit status 0.</desc>
<text class="vx-text-muted" x="20" y="30">compile time</text>
<text class="vx-text-muted" x="470" y="30">link time</text>
<text class="vx-text-muted" x="610" y="30">run time</text>
<line class="vx-line" x1="450" y1="20" x2="450" y2="240"/>
<line class="vx-line" x1="590" y1="20" x2="590" y2="240"/>
<rect class="vx-box" x="20" y="60" width="110" height="50" rx="4"/>
<text class="vx-mono" x="75" y="90" text-anchor="middle">main.vx</text>
<rect class="vx-box-strong" x="155" y="60" width="140" height="50" rx="4"/>
<text class="vx-text" x="225" y="90" text-anchor="middle">Vortex compiler</text>
<rect class="vx-box" x="320" y="60" width="110" height="50" rx="4"/>
<text class="vx-text" x="375" y="90" text-anchor="middle">object file</text>
<rect class="vx-box-strong" x="470" y="60" width="100" height="50" rx="4"/>
<text class="vx-text" x="520" y="90" text-anchor="middle">linker</text>
<rect class="vx-box-accent" x="610" y="60" width="130" height="50" rx="4"/>
<text class="vx-text" x="675" y="90" text-anchor="middle">executable</text>
<rect class="vx-box" x="455" y="170" width="130" height="56" rx="4"/>
<text class="vx-text" x="520" y="194" text-anchor="middle">runtime library</text>
<text class="vx-text-muted" x="520" y="213" text-anchor="middle">print, program start</text>
<rect class="vx-box" x="610" y="170" width="130" height="56" rx="4"/>
<text class="vx-mono" x="675" y="194" text-anchor="middle">14</text>
<text class="vx-text-muted" x="675" y="213" text-anchor="middle">plus exit status 0</text>
<line class="vx-line" x1="130" y1="85" x2="147" y2="85"/>
<polygon class="vx-arrowhead" points="147,80 155,85 147,90"/>
<line class="vx-line" x1="295" y1="85" x2="312" y2="85"/>
<polygon class="vx-arrowhead" points="312,80 320,85 312,90"/>
<line class="vx-line" x1="430" y1="85" x2="462" y2="85"/>
<polygon class="vx-arrowhead" points="462,80 470,85 462,90"/>
<line class="vx-line" x1="570" y1="85" x2="602" y2="85"/>
<polygon class="vx-arrowhead" points="602,80 610,85 602,90"/>
<line class="vx-line" x1="520" y1="170" x2="520" y2="118"/>
<polygon class="vx-arrowhead" points="515,118 520,110 525,118"/>
<line class="vx-line" x1="675" y1="110" x2="675" y2="162"/>
<polygon class="vx-arrowhead" points="670,162 675,170 680,162"/>
<text class="vx-text-muted" x="20" y="140">the call to print is</text>
<text class="vx-text-muted" x="20" y="156">still a hole here</text>
<circle class="vx-dot" r="6">
<animateMotion dur="8s" repeatCount="indefinite" path="M75 85 L675 85 L675 198" keyPoints="0;0;0.83;1;1" keyTimes="0;0.08;0.75;0.9;1" calcMode="linear"/>
</circle>
</svg>
<figcaption>Figure 2. The first program's trip. The compiler writes an object file with a hole where <code>print</code> should be. The linker fills it with code from the runtime library. Only the executable can run, and running it is the only real proof that every earlier step worked.</figcaption>
</figure>

Kaleidoscope's chapter 8 ends in the same place: an object file from LLVM,
linked with a separate hand-written program by an ordinary C++ compiler, to
produce something that runs.[^kal8] Sandler's first chapter does it with
`gcc`, which assembles and links in one command.[^sandler-blog]

If you generate assembly or C, there is one more tool in the chain (the
assembler or the C compiler) before the linker. The shape does not change.

The next example models the hole-and-fill idea directly, for a tiny made-up
instruction set rather than for a real object format. A "module" is a list of
instructions; some call a symbol by name because the code that defines it
lives elsewhere. Linking checks every such name against a table of routines
before anything runs, and reports the first name it cannot find, the way a
real linker reports an undefined symbol instead of producing a broken program.

--8<-- "includes/examples/build-v0.1/stage-6-first-machine-code/resolve_symbols.cpp.md"

??? check "If the compiler's own test only checks that an object file was produced, what could still be broken?"

    Linking and start-up. An object file can exist and be perfectly
    well-formed while the link step fails to resolve `print`, or the runtime
    never reaches `main`. Only a test that runs the finished executable and
    checks its output and exit status shows that the whole chain works:
    compiler, linker and runtime together.

## The small runtime

The **runtime library** is the code every Vortex program gets without writing
it. Ghuloum's compiler shows how small it can start: its test driver links the
generated code with "a minimal run-time system (to support printing)", a few
lines of C that call the compiled code and print what it returns.[^ghuloum]
The *Mapping High Level Constructs* guide notes that support functions written
in another language are common and easy to connect to.[^mapping]

For Milestone 6, the Vortex runtime must provide two things:

- a way to start the program and reach `main`, which the
  [hello-world chapter](../../language-tour/03-hello-world.md) describes as the
  runtime beginning execution at the validated `main`;
- `print` for an `i32`, since that is what the first program prints.

Later stages will ask for more. The
[variables and types chapter](../../language-tour/04-variables-and-types.md)
shows `print` taking strings and other basic values, and even two arguments at
once, as in `print("rows:", rows)`. [Stage 9](stage-9-runtime-safety.md) will
need the runtime to stop a program with a clear runtime error. None of that is
needed to print `14`.

Keep the runtime small and dumb. Language rules belong in the compiler, where
they are checked once and tested directly. A runtime that starts making
decisions about Vortex semantics becomes a second, untested implementation of
the language.

### Two decisions that affect the first test {#two-open-decisions-that-affect-the-first-test}

The Milestone 6 test compares output and exit status, and both are now fixed.

**What `print` writes.**
[Programs and declarations 3.9](../../specification/declarations.md#39-built-in-functions)
defines `print` ([decision](../../decisions/program.md#d4)): it writes its
arguments to standard output with one space between them and a line feed after
the last. So `print(14)` writes three bytes: `1`, `4` and a line feed.

That layout rule, one space between arguments and one line feed after the
last, is one half of the contract. The other half is the text of each value,
which the same section fixes type by type: an `f64` holding sixteen prints as
`16.0`, for example, where C++'s `{}` format would print `16`. The next example
applies only the layout half, to ordinary C++ values, to show how little code
it takes once it is kept apart from the formatting of each value.

--8<-- "includes/examples/build-v0.1/stage-6-first-machine-code/join_print.cpp.md"

**What exit status a Vortex program returns.** A program whose `main` returns
exits with status 0, and a program stopped by a runtime error exits with 101
([Diagnostics 10.6](../../specification/diagnostics.md#106-runtime-reporting),
[decision](../../decisions/program.md#d14)), so stage 9's tests can tell the
two apart.

With both written down, the first end-to-end test has an exact expected
result.

## Many small passes

The architecture page already asks for "a sequence of small, inspectable
passes". Lowering is where that request is easiest to forget, because it is
tempting to go from tree to machine code in one large step.

There is a well-known argument for going the other way, as far as it will go.
Sarkar, Waddell and Dybvig describe compilers built from many small passes,
each of which performs a single task, and call them "nanopass"
compilers.[^nanopass] Their motivation was teaching: large passes are hard to
understand and hard to change without breaking something. A later paper by
Keep and Dybvig tested the idea on a commercial compiler, replacing five of the
original ten passes of the Chez Scheme compiler with more than fifty
nanopasses. Compile times stayed within a factor of two of the original, and
the generated code was faster.[^nanopass-2013]

This is a design philosophy, not a requirement. The Vortex docs ask for small,
inspectable passes, not for any particular number. But if a lowering step is
getting hard to test, splitting it in two is usually the right move.

## The first end-to-end test

Ghuloum's test driver is a good model for what one end-to-end test does. For
each test case it compiles the program, assembles and links it with the
runtime, runs the result, and compares the output with the expected
string, failing if any step fails.[^ghuloum] A Vortex version of that test for
the roadmap program checks, in order:

1. the compiler, run as `vortex <source> -o <path>`, accepts the program and
   exits with status 0 ([decision 20](../../decisions/program.md#d20));
2. the link step succeeds and produces an executable;
3. the executable runs and writes exactly `14` and a line feed to standard
   output;
4. the exit status is 0.

Two more tests belong next to it. One compiles a program that stage 5 rejects
and checks for exit status 1 and no executable, which is the architecture
page's lowering rule made visible. The other is the smallest possible program,
an empty `main`, which checks that start-up and exit work with nothing in
between: it writes nothing and exits with status 0.

```vortex
// program: valid
fn main() {
}
```

## What you need to have

<div class="vx-split" markdown="1">
<div markdown="1">

#### Need to have

- A written back-end decision that answers the questions above: the roadmap
  says "the chosen backend", so it has to be chosen.
- An IR, however simple, between the checked tree and the back end: this is
  where later targets will attach.
- Lowering for `main`, integer and floating-point literals, arithmetic, local
  variables and `return`: the Milestone 6 list.
- An object file or executable written by the compiler.
- A runtime with program start-up and `print` for `i32`.
- A link step that joins them into an executable.
- A runtime `print` for `i32` that follows the written format, and exit
  status 0 when `main` returns.
- An end-to-end test that compiles, links, runs, and compares output and exit
  status for the roadmap program.
- A test showing that a rejected program exits with status 1 and produces no
  executable.

</div>
<div class="vx-not-yet" markdown="1">

#### Not yet

- Calls to user-defined functions, `if`, loops, `break` and `continue`: these
  are [stage 7](stage-7-functions-and-control-flow.md).
- Arrays, strings, structs and references in generated code: these are
  [stage 8](stage-8-data-in-memory.md).
- Runtime checks for integer overflow, integer division by zero and bounds:
  these are [stage 9](stage-9-runtime-safety.md).
- Optimization of any kind: the roadmap puts it after v0.1.
- Debug information, which Kaleidoscope adds in chapter 9:[^kal9] no milestone
  asks for it.
- Running code inside the compiler on the fly, which Kaleidoscope adds in
  chapter 4:[^kal4] Vortex compiles ahead of time.
- More than one target, and GPU code: after v0.1.
- Your own SSA construction: not needed until branches exist, and perhaps not
  at all if the back end offers a promotion pass.

</div>
</div>

## What you do not need yet

The right-hand column above is the list. The general rule is that this stage
should be as thin as it can be while still being real. A back end that handles
one program correctly, end to end, is worth more at this point than a back end
that handles many constructs without ever producing a running executable.

## How you know it is finished

The roadmap's condition for
[Milestone 6](../../roadmap.md#milestone-6-basic-cpu-code-generation) is that
the first program "compiles, runs, prints `14`, and exits with status 0".
Behind that sentence sit
the roadmap's general rules for every milestone: the project builds from a
clean configuration, every test passes, and "the AST or generated output is
stable enough to inspect". So:

- the roadmap program writes `14` and a line feed, and exits with status 0;
- the back-end decision is written down, and the output and exit status
  follow the `print` and exit-status rules;
- the generated IR or assembly for the test program can be printed and read;
- a rejected program produces a diagnostic on standard error, exit status 1
  and no executable;
- every test from milestones 0 to 5 still passes.

## Traps

**Starting the back end before the front end is trustworthy.** The roadmap
allows work from a later milestone to be prototyped, but it "must not weaken or
bypass an earlier milestone's correctness rules". A back end that quietly
tolerates unchecked programs is exactly that kind of weakening.

**Letting the back end redo type decisions.** Stage 5 decided that `result` is
an `i32`. If the back end treats every number as a 64-bit value because that is
convenient, the program's behavior no longer matches the spec, and the
difference will surface later as overflow checks that fire at the wrong
values.

**Testing the IR instead of the program.** Printed IR is useful for reading,
and fragile as a test: it changes whenever you improve lowering. The
milestone's evidence is a program that runs and prints `14`. Keep the
behavioral test as the one that decides pass or fail.

**Mixing up the object file and the executable.** An object file is not a
program. If the test stops at "the compiler wrote a file", nothing has shown
that linking or start-up work.

**Hard-wiring the toolchain.** A path to a linker or compiler that only exists
on one machine breaks Milestone 0's one-command build for everyone else. Make
the tools the back end needs part of the documented decision.

**Letting the runtime grow opinions.** `print` should print. It should not
decide how Vortex values convert or when an operation is legal. Those rules
live in the compiler.

**Turning on optimizer settings by accident.** Some back ends optimize by
default, and C compilers may fuse a multiply and an add unless told not to.
Any setting that fuses, reorders or widens floating-point arithmetic, or
flushes tiny values to zero, breaks the spec's rule on floating-point results.

## Key ideas

!!! recap

    - Why does an IR sit between the front end and the back end? So the
      checked program has one shared representation, and only the part below
      it changes when the target changes.
    - What must the back end never do to a validated program? Re-decide
      anything the front end already settled, such as a value's type.
    - What is a phi for? Picking which SSA version of a value is current at a
      point where two control-flow paths join.
    - Why does Milestone 6 need no phis yet? The first program has no
      branches, so it has no join points.
    - What turns an object file into a program that can run? The linker,
      which fills in names such as `print` with real addresses from the
      runtime library.
    - What two facts does the Milestone 6 test check about the finished
      program? Its exact standard output and its exit status.
    - What does the compiler leave behind when it rejects a program? A
      diagnostic on standard error, exit status 1, and no executable.

## Where this comes back

--8<-- "includes/next/compiler__guide__stage-6-first-machine-code.md"

## How others teach this stage

**Kaleidoscope** splits this stage across several chapters. Chapter 3 turns
the syntax tree into LLVM IR, using LLVM's helper for building instructions
and its checker for catching mistakes.[^kal3] Chapter 7 explains why mutable
variables are awkward in SSA and how the memory-then-mem2reg approach avoids
building SSA by hand.[^kal7] Chapter 8 produces an object file and links it
with a C++ program.[^kal8] Read chapters 3 and 8 together for the shape of
this stage. The difference is scope: Kaleidoscope has one type and no runtime
of its own, while Vortex already has a full type system and needs `print`.

***Crafting Interpreters*** takes a different road. Its second half compiles
to bytecode for a virtual machine it writes itself, rather than to machine
code.[^ci-jump] Its overview chapter is still the best short explanation of why IRs,
back ends and runtimes exist, and of the option of compiling to another
language's source.[^ci-map]

**Ghuloum's incremental approach** is the closest match to Milestone 6. Its
first step compiles a single number to x86 assembly and links it with a tiny
C runtime that prints the result, and every later step extends a compiler that
already works end to end.[^ghuloum] **Sandler** follows the same plan, starting
from a program that returns 2 and checking the exit code.[^sandler-blog] Her
book goes on to use its own IR, which it later optimizes in a chapter titled
"Optimizing Tacky Programs".[^sandler-book] Vortex reaches this point later
than either, after a complete front end, but the first test is the same kind of
test.

***Mapping High Level Constructs to LLVM IR*** is a reference rather than a
tutorial. Keep it open if you choose LLVM. Its pages on local variables, on
SSA and phi, and on interoperating with a runtime library answer most of the
questions this stage raises.[^mapping]

The **nanopass** papers are worth reading before you design lowering, for the
argument rather than for the tools.[^nanopass][^nanopass-2013] The **MLIR Toy
tutorial** is worth reading after v0.1, when the GPU path becomes real.[^toy]

[^ci-map]: Robert Nystrom, *Crafting Interpreters*, chapter "A Map of the Territory". <https://craftinginterpreters.com/a-map-of-the-territory.html>
[^ci-jump]: Robert Nystrom, *Crafting Interpreters*, chapter "Jumping Back and Forth". <https://craftinginterpreters.com/jumping-back-and-forth.html>
[^langref]: LLVM Project, "LLVM Language Reference Manual". <https://llvm.org/docs/LangRef.html>
[^kal3]: LLVM Project, "My First Language Frontend with LLVM Tutorial", chapter 3, "Code generation to LLVM IR". <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl03.html>
[^kal4]: LLVM Project, "My First Language Frontend with LLVM Tutorial", chapter 4, "Adding JIT and Optimizer Support". <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl04.html>
[^kal7]: LLVM Project, "My First Language Frontend with LLVM Tutorial", chapter 7, "Extending the Language: Mutable Variables". <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl07.html>
[^kal8]: LLVM Project, "My First Language Frontend with LLVM Tutorial", chapter 8, "Compiling to Object Code". <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl08.html>
[^kal9]: LLVM Project, "My First Language Frontend with LLVM Tutorial", chapter 9, "Adding Debug Information". <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl09.html>
[^mapping]: Michael Rodler and Mikael Egevig, *Mapping High Level Constructs to LLVM IR*. <https://mapping-high-level-constructs-to-llvm-ir.readthedocs.io/en/latest/>
[^llvm-start]: LLVM Project, "Getting Started with the LLVM System". <https://llvm.org/docs/GettingStarted.html>
[^ghuloum]: Abdulaziz Ghuloum, "An Incremental Approach to Compiler Construction", *Proceedings of the 2006 Scheme and Functional Programming Workshop*, University of Chicago Technical Report TR-2006-06. <http://scheme2006.cs.uchicago.edu/11-ghuloum.pdf>
[^sandler-blog]: Nora Sandler, "Writing a C Compiler, Part 1", 29 November 2017. <https://norasandler.com/2017/11/29/Write-a-Compiler.html>
[^sandler-book]: Nora Sandler, *Writing a C Compiler: Build a Real Programming Language from Scratch*, No Starch Press, 2024. <https://nostarch.com/writing-c-compiler>
[^toy]: MLIR Project, "Toy Tutorial". <https://mlir.llvm.org/docs/Tutorials/Toy/>
[^cytron]: Ron Cytron, Jeanne Ferrante, Barry K. Rosen, Mark N. Wegman and F. Kenneth Zadeck, "Efficiently Computing Static Single Assignment Form and the Control Dependence Graph", *ACM Transactions on Programming Languages and Systems* 13(4), 1991. <https://doi.org/10.1145/115372.115320>
[^braun]: Matthias Braun, Sebastian Buchwald, Sebastian Hack, Roland Leißa, Christoph Mallon and Andreas Zwinkau, "Simple and Efficient Construction of Static Single Assignment Form", *Compiler Construction (CC 2013)*, Springer. <https://doi.org/10.1007/978-3-642-37051-9_6>
[^nanopass]: Dipanwita Sarkar, Oscar Waddell and R. Kent Dybvig, "A Nanopass Infrastructure for Compiler Education", *ICFP 2004*. <https://doi.org/10.1145/1016850.1016878>
[^nanopass-2013]: Andrew W. Keep and R. Kent Dybvig, "A Nanopass Framework for Commercial Compiler Development", *ICFP 2013*. <https://doi.org/10.1145/2500365.2500618>
