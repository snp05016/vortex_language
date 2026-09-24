# E2. Describing a target

<p class="page-intro">LLVM supports dozens of processors without a backend author hand-writing a separate register table, instruction table and calling-convention table for each one, in each of the tools that needs a copy. This chapter reads the tool that makes that possible, TableGen, and the four kinds of target facts it turns into code: registers, instructions, calling conventions and scheduling models.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 25 minutes · Builds on: [E1. The LLVM code generator pipeline](e1-llvm-codegen-pipeline.md), [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md), [A4. Calling conventions and ABIs](a4-calling-conventions.md).</p>

???+ remember "Before you start, remember"

    ??? question "How many bytes does a single AArch64 instruction occupy, no matter which one it is?"

        Four: one fixed 32-bit word. A decoder reads the same handful of
        field positions for every instruction of a given class. x86-64
        instructions vary instead, from one byte up to fifteen.

        Introduced in [A1. The machine model](a1-machine-model.md#encoding-an-instruction-is-a-fixed-pattern-of-bits).

    ??? question "What happens to the upper 32 bits of `x0` when code writes `w0`?"

        They are zeroed. `w0` names the low 32 bits of the same physical
        storage as `x0`, and writing the narrower view always clears the
        rest of the register.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md#registers-by-name).

    ??? question "Why does AAPCS64 keep two separate counters when it places call arguments, instead of one shared counter?"

        Because general-purpose arguments and floating-point arguments live
        in separate register banks, `x0`-`x7` and `v0`-`v7`. A shared
        counter would waste one bank's registers every time an argument of
        the other kind appeared.

        Introduced in [A4. Calling conventions and ABIs](a4-calling-conventions.md#where-arguments-go-two-counters-not-one).

    ??? question "Which AArch64 registers are callee-saved among the floating-point bank, and how much of each one?"

        `v8` through `v15`, and only their low 64 bits. A callee that
        overwrites the upper 64 bits of one of those registers has not
        broken its promise to the caller.

        Introduced in [A4. Calling conventions and ABIs](a4-calling-conventions.md#caller-saved-and-callee-saved-registers).

!!! goals "In this chapter"

    - Explain why LLVM generates its register, instruction, calling-convention and scheduling tables from one declarative description instead of hand-writing a separate copy of each fact for every tool that needs it.
    - Read a small class-and-def pair in TableGen's style and say what kind of table or lookup it will end up producing.
    - Compute, for a small hierarchy of overlapping subregisters, whether writing one leaves another untouched, fully redefines it, or only partly disturbs it.
    - Trace one AArch64 argument-placement decision in real compiled output back to the calling-convention rule that produced it.
    - State what "a record has no meaning until a backend interprets it" means, and give an example of the same record meaning two different things to two different generators.

## One fact, many consumers

Take one true statement about the AArch64 machine: `w0` names the low 32
bits of `x0`. [A2](a2-aarch64-assembly.md#registers-by-name) already showed
what that fact costs a compiler writer who ignores it: write `w0` expecting
it to leave the top half of `x0` alone, and the value silently loses its
upper bits. But knowing the fact is not the same as knowing everywhere it
matters. Inside LLVM's AArch64 backend, that one fact is needed by:

- the **assembler**, to accept `w0` and `x0` as names of registers at all,
  and to know they are not independent;
- the **register allocator**, to know that a value live in `x0` cannot be
  handed to something else just because the code only wrote `w0` this time
  (later chapters, from [C2](c2-liveness.md) on, build the liveness
  analysis this depends on);
- the **instruction selector**, to know which register class a 32-bit
  operation is legal to place a result in;
- the **assembly printer** and **disassembler**, to print and parse `w0`
  correctly rather than treating it as a 64-bit register that happens to be
  named oddly.

A backend that supports one target could get away with writing this fact
into each of those four places by hand, as ordinary C++. LLVM supports
several dozen targets, each with its own register file, and every one of
those four consumers exists once, shared across every target, because they
do the same *job* for AArch64 as for x86-64 or RISC-V. If each target
hand-wrote its facts separately for each shared consumer, adding a target
would mean editing code deep inside the register allocator, the assembler
and the disassembler, code that people working on those tools do not own
and should not need to touch just because someone added a target.

LLVM's answer is to write the fact exactly once, as **data**, and generate
the four (in practice, many more) C++ tables each consumer reads from that
one description. The tool that does the generating is called
**TableGen**: a small domain-specific language for writing down records,
plus a compiler, `llvm-tblgen`, that turns those records into C++ source
the rest of the build compiles normally.[^tablegen] "Describing a target"
in LLVM does not mean writing a backend's logic; it means writing the data
that logic, written once and shared across every target, gets configured
from.

<figure class="vx-figure" role="img" aria-label="One set of target description records, read by llvm-tblgen, fans out into five generated tables: register info, instruction info, the assembly matcher and writer, the calling convention, and the scheduling model. Each table is consumed by a different, target-independent part of the backend: register allocation and liveness read register info; instruction selection reads instruction info; the assembler and disassembler read the assembly matcher and writer; argument and result placement reads the calling convention; the machine scheduler and llvm-mca read the scheduling model.">
<svg viewBox="0 0 720 300" xmlns="http://www.w3.org/2000/svg">
<rect class="vx-box-accent" x="250" y="8" width="220" height="34" rx="4"/>
<text class="vx-mono" x="360" y="30" text-anchor="middle">target description (.td records)</text>

<line class="vx-line" x1="360" y1="42" x2="360" y2="66"/>
<polygon class="vx-arrowhead" points="354,66 360,76 366,66"/>

<rect class="vx-box" x="290" y="76" width="140" height="32" rx="4"/>
<text class="vx-mono" x="360" y="97" text-anchor="middle">llvm-tblgen</text>

<line class="vx-line" x1="330" y1="108" x2="80" y2="150"/>
<line class="vx-line" x1="350" y1="108" x2="220" y2="150"/>
<line class="vx-line" x1="370" y1="108" x2="360" y2="150"/>
<line class="vx-line" x1="390" y1="108" x2="500" y2="150"/>
<line class="vx-line" x1="410" y1="108" x2="640" y2="150"/>
<polygon class="vx-arrowhead" points="74,144 80,154 86,144"/>
<polygon class="vx-arrowhead" points="214,144 220,154 226,144"/>
<polygon class="vx-arrowhead" points="354,144 360,154 366,144"/>
<polygon class="vx-arrowhead" points="494,144 500,154 506,144"/>
<polygon class="vx-arrowhead" points="634,144 640,154 646,144"/>

<rect class="vx-box" x="10" y="156" width="140" height="34" rx="4"/>
<text class="vx-text" x="80" y="178" text-anchor="middle" font-size="12">Register info</text>
<rect class="vx-box" x="150" y="156" width="140" height="34" rx="4"/>
<text class="vx-text" x="220" y="178" text-anchor="middle" font-size="12">Instruction info</text>
<rect class="vx-box" x="290" y="156" width="140" height="34" rx="4"/>
<text class="vx-text" x="360" y="172" text-anchor="middle" font-size="12">Asm matcher</text>
<text class="vx-text" x="360" y="186" text-anchor="middle" font-size="12">/ writer</text>
<rect class="vx-box" x="430" y="156" width="140" height="34" rx="4"/>
<text class="vx-text" x="500" y="178" text-anchor="middle" font-size="12">Calling convention</text>
<rect class="vx-box" x="570" y="156" width="140" height="34" rx="4"/>
<text class="vx-text" x="640" y="178" text-anchor="middle" font-size="12">Scheduling model</text>

<text class="vx-text-muted" x="80" y="212" text-anchor="middle" font-size="11">regalloc,</text>
<text class="vx-text-muted" x="80" y="225" text-anchor="middle" font-size="11">liveness (C2-C4)</text>
<text class="vx-text-muted" x="220" y="212" text-anchor="middle" font-size="11">instruction</text>
<text class="vx-text-muted" x="220" y="225" text-anchor="middle" font-size="11">selection (C1)</text>
<text class="vx-text-muted" x="360" y="212" text-anchor="middle" font-size="11">assembler,</text>
<text class="vx-text-muted" x="360" y="225" text-anchor="middle" font-size="11">disassembler</text>
<text class="vx-text-muted" x="500" y="212" text-anchor="middle" font-size="11">argument and</text>
<text class="vx-text-muted" x="500" y="225" text-anchor="middle" font-size="11">result placement</text>
<text class="vx-text-muted" x="640" y="212" text-anchor="middle" font-size="11">scheduler,</text>
<text class="vx-text-muted" x="640" y="225" text-anchor="middle" font-size="11">llvm-mca (C6, E3)</text>
</svg>
<figcaption>One set of target description records, read by llvm-tblgen,
generates five separate tables. Each table has its own reader, shared
across every LLVM target, so a target adds a fact once and every reader
picks it up.</figcaption>
</figure>

## Records with no meaning of their own

A TableGen file declares **classes**, which work like templates, and
**defs**, which are concrete instances of a class with all of its fields
filled in. Neither one means anything on its own. A `def` is a bag of named
fields, nothing more, until some C++ program reads that bag and decides
what to do with it. The illustration below is not real AArch64 source
(LLVM's actual register description is considerably larger, and this
chapter's examples policy asks for original problems, not copied ones); it
shows the shape of the idea on the fact from the last section:

```text
// illustrative TableGen-style syntax; not compiled, and not LLVM's own
// AArch64 description
class Register<string name> {
  string Name = name;
}

class SubRegister<Register super, int bits> : Register<super.Name # "_view"> {
  Register Super = super;
  int Bits = bits;
}

def X0 : Register<"x0">;
def W0 : SubRegister<X0, 32>;
```

`Register` is a class: a template with one field, `Name`. `X0` is a def: a
concrete record with `Name` set to `"x0"`. `SubRegister` is a second class
that reuses `Register`'s field and adds two more, `Super` and `Bits`. `W0`
is a def of that second class, recording exactly the fact from the last
section: it is a 32-bit view onto `X0`. Nothing here says what a register
allocator should do with this information. `llvm-tblgen` runs several
different **backends** of its own (an unfortunate reuse of the word
"backend": here it means one of TableGen's own output generators, not a
target's code generator) over the same records, and each one asks a
different question of them. The register-info backend asks "which physical
registers exist, and which ones alias which others". The instruction-info
backend, reading a different kind of record entirely (one describing an
instruction rather than a register), asks "what are this instruction's
operands, and which register classes are they allowed to come from". The
assembly-matcher and assembly-writer backends ask "what text spells this
operand". The calling-convention and scheduling-model backends, covered
later in this chapter, ask about argument placement and timing. TableGen's
own reference calls this out directly: a record's meaning comes entirely
from the backend that reads it, and the same class can be reused by
completely different backends for completely different purposes.[^tablegen-backends]
A real target's description spans several `.td` files: one for registers,
one for instructions and their operand patterns, one for the calling
convention, one for the scheduling model, tied together by a `TargetMachine`
subclass that names them all.[^writing-backend]

??? check "Two different TableGen backends read the same `def` for the register `x0`. Can they disagree about what a valid program does with it?"

    Not usefully. Both backends are reading the same fields off the same
    record, so if the register-info backend believes `w0` aliases `x0` and
    the assembly-matcher backend does not, that is a bug in one of the two
    backends, not a legitimate difference of opinion. The point of writing
    the fact once is exactly to rule this kind of disagreement out; hand-
    written, duplicated copies of the same fact are what let it happen.

## A subregister is a claim about bits, not a separate register

The general version of the `w0`/`x0` relationship is a **subregister**: a
named view onto part of a larger register's storage, described by which
bits of the larger register it covers. Real targets nest these more deeply
than AArch64's two-level `w`/`x` split. Picture a toy 32-bit register `R`
with a low 16-bit half `RL` and a high 16-bit half `RH`, and `RL` itself
split into an upper byte `RL_HI` and a lower byte `RL_LO`. Given a write to
one of these five names, a backend needs to answer, for every other name:
does this write leave it alone, fully replace its value, or only partly
disturb it? Get this wrong in the register allocator and it will either
free up a register that still holds live data (unaffected wrongly reported
as replaced) or refuse to reuse one that is genuinely dead (replaced
wrongly reported as disturbed).

`examples/backend/e2-describing-a-target/subregister_effects.cpp` states
exactly that overlap question as data: each named view is a `[lo, hi)` bit
range, and one function classifies a pair of ranges by whether they share
no bits, whether the write's range covers the other's completely, or
whether they only partly overlap. This is the same computation a target's
subregister-index description exists to make available to the rest of the
backend, reduced to the one idea and away from AArch64's specific register
names:

--8<-- "includes/examples/backend/e2-describing-a-target/subregister_effects.cpp.md"

Its output (expand "Output" above) starts with writing `RL_LO`, the lowest
byte. `R` is only partly defined, because the write covers just 8 of its 32 bits.
`RH` is unaffected, since it shares no bits with `RL_LO` at all. `RL` is
partly defined for the same reason as `R`. Before reading the program's
second block of output, work out by hand what a write to `RH` (bits 16
through 31) does to `R`, `RL`, `RL_HI` and `RL_LO`. `RH` shares no bits
with any register below bit 16, so three of those four answers should be
"unaffected"; only `R`, whose range contains `RH`'s entirely, should come
back partly defined. The program's second block confirms it.

??? check "Real AArch64 does not nest this deeply: it stops at `w`/`x` for general-purpose registers. Why might a backend for a different processor need three or four levels of subregister instead of two?"

    Because the processor's own register file has that structure. x86-64
    is the standard example: `al` (an 8-bit view), `ax` (16-bit, containing
    `al`), `eax` (32-bit, containing `ax`), and `rax` (64-bit, containing
    `eax`) are four nested views of one piece of storage, and `ah` is a
    second, non-overlapping 8-bit view inside `ax`, sitting where `RL_HI`
    sits in this chapter's toy example. A subregister description has to
    match the real hardware, not the other way around; AArch64 needing only
    two levels is a fact about AArch64, not a limit of the mechanism.

## The calling convention you already know, generated

[A4](a4-calling-conventions.md#where-arguments-go-two-counters-not-one)
established AAPCS64's argument-placement rule from the outside: keep two
counters, one for general-purpose arguments and one for floating-point
ones, and place each argument in the next register its own counter points
to. That rule is not special-cased C++ inside LLVM's AArch64 backend
either. It is a table, read by one shared piece of code (a state machine
that walks a function's argument list and consults the table for each
argument in turn) that every target's calling convention reuses; only the
table's contents change from target to target.[^codegen] `.ll` files carry
no register names at all; a function's arguments are just typed values, in
order. Watching where AAPCS64's rule actually sends a mix of them is a way
to check the abstract rule from A4 against the real thing.

`examples/backend/e2-describing-a-target/mixed_args.ll` declares one
function with its integer and floating-point parameters deliberately
interleaved, `i64`, `float`, `i64`, `float`, so the register placement
cannot be mistaken for simple left-to-right numbering:

--8<-- "includes/examples/backend/e2-describing-a-target/mixed_args.ll.md"

The check above only confirms the IR itself is well formed (`opt -S
-passes=verify`, expanded under "Output"); it says nothing about registers,
since `.ll` carries no register names at all. Compiling the same file for
AArch64 with `llc -O2` (run locally; the real output, not invented) places
the two `i64` parameters, `%a` and `%c`, in `x0` and `x1`, and the two
`float` parameters, `%b` and `%d`, in `s0` and `s1`:

```text
_combine:
        fcvtzs  x8, s0
        fcvtzs  x9, s1
        add     x10, x0, x1
        add     x8, x10, x8
        add     x0, x8, x9
        ret
```

`%a` and `%c` never shared a counter with `%b` and `%d`, exactly as A4's
abstract rule predicted: the integer counter advanced twice, landing on
`x0` then `x1`; the floating-point counter, independently, also advanced
twice, landing on `s0` then `s1`. The table that told `llc` to do this is
AAPCS64's calling-convention description, generated by TableGen's
calling-convention backend from the same kind of declarative file this
chapter has been describing.[^aapcs64] The `fcvtzs` instructions are just
`%b` and `%d` being converted from float to a signed integer, the `fptosi`
in the IR; the point of this example is entirely in which registers the
arguments arrived in before that conversion happened. Vortex's own matmul
kernel, `multiply(a: &[f32; 2, 3], b: &[f32; 3, 2], c: &mut [f32; 2, 2])`,
takes three references and no floating-point argument, so its own call
would advance only the general-purpose counter, landing `&a`, `&b` and
`&mut c` in `x0`, `x1` and `x2` in that order; A4 already worked out that
placement by hand, and this section is what makes it a fact about a table
`llc` reads rather than a fact this book asserts.

<figure class="vx-figure" role="img" aria-label="The multiply kernel's three reference arguments, a, b and mut c, advancing the AAPCS64 general-purpose counter into x0, x1 and x2 in order, with the floating-point counter untouched because no argument is a scalar float or f64.">
<svg viewBox="0 0 520 110" xmlns="http://www.w3.org/2000/svg">
<rect class="vx-box" x="10" y="10" width="150" height="30" rx="4"/>
<text class="vx-mono" x="85" y="30" text-anchor="middle">&amp;a -&gt; x0</text>
<rect class="vx-box" x="180" y="10" width="150" height="30" rx="4"/>
<text class="vx-mono" x="255" y="30" text-anchor="middle">&amp;b -&gt; x1</text>
<rect class="vx-box" x="350" y="10" width="150" height="30" rx="4"/>
<text class="vx-mono" x="425" y="30" text-anchor="middle">&amp;mut c -&gt; x2</text>
<text class="vx-text-muted" x="260" y="70" text-anchor="middle" font-size="12">general-purpose counter only; floating-point counter unused</text>
</svg>
<figcaption>The matmul kernel's argument placement, following AAPCS64: three
references are three addresses, so all three advance the same
general-purpose counter mixed_args.ll exercised above.</figcaption>
</figure>

## Latency and throughput as another table

A target description covers more than where things live. It also covers
how long they take. Once instruction selection has chosen an instruction, a
later pass, the machine scheduler, decides what order to place instructions
in within a basic block; [C6](c6-scheduling.md) covers that pass in full.
That pass needs one more number per instruction, per processor: how many
cycles it takes before a dependent instruction can use its result. That
number is not a property of the instruction set. `mul` is one AArch64
mnemonic, executed by every Apple Silicon and Arm Cortex core, but the
number of cycles it takes to complete varies from one microarchitecture to
the next. TableGen's scheduling-model backend generates a table for this,
keyed by both opcode and subtarget, so the scheduling pass itself never
mentions a specific processor by name; it only ever asks the table for the
number.[^tablegen-backends] The same tables feed `llvm-mca`, a tool this
book's [E3](e3-llvm-allocator-scheduler-mc.md) and
[P4](../optimize/p4-counters-and-tools.md) chapters return to, which
predicts a basic block's throughput on a chosen processor model without
running it at all.

`examples/backend/e2-describing-a-target/sched_model_latency.cpp` reduces
this to a chain of four dependent toy instructions, `load`, `mul`, `add`,
`store`, each waiting for the one before it to finish, scheduled twice
against two different latency tables that share the same opcodes:

--8<-- "includes/examples/backend/e2-describing-a-target/sched_model_latency.cpp.md"

The scheduling arithmetic, "an instruction starts when its dependency
finishes, and finishes `latency` cycles later", is identical in both runs.
Only the numbers in `kGenericLatency` and `kFastMulLatency` differ, and
that alone is enough to move the chain's total from 9 cycles to 7. A real
scheduling model works the same way: adding support for a new
microarchitecture, or correcting a wrong latency for an existing one, means
editing a table, not the scheduler. This is exactly the kind of number
[P5](../optimize/p5-microarchitecture.md) and [P12](../optimize/p12-fast-gemm.md)
need when they ask how many cycles a fused multiply-add takes inside
Vortex's matmul kernel: a fact about the specific processor running the
kernel, not about the `fmadd` instruction in the abstract.

??? check "The `sched_model_latency.cpp` chain has every instruction depend on the one directly before it. What would change about the scheduling arithmetic if `store` depended on `load` directly, and not on `add`?"

    `store` could start as soon as `load` finished, at cycle 4, instead of
    waiting for `add` to finish at cycle 8 (generic model). This is exactly
    the situation a real machine scheduler looks for: once dependencies
    stop forming one straight chain, instructions with no dependency
    between them can run out of program order, or in parallel on a
    superscalar processor, and the scheduler's job becomes choosing a good
    order rather than only computing one forced order's length.

## For Vortex

!!! vortex "Exercise"

    Vortex's own back end, once you reach [B1](b1-simplest-backend.md) and
    beyond, does not use TableGen, and this exercise does not ask you to
    add it. It asks you to apply the lesson TableGen's design teaches,
    which does not depend on the tool: keep the facts that are true about a
    target in one place, as data, separate from the code that acts on
    them.

    Pick one concrete fact your compiler's back end will need and that this
    book's earlier chapters already gave you a source for: for example,
    which AArch64 register each argument to your matmul kernel's
    `multiply(a: &[f32; 2, 3], b: &[f32; 3, 2], c: &mut [f32; 2, 2])` lands
    in, following [A4](a4-calling-conventions.md)'s AAPCS64 rule (three
    references are three addresses, so all three are general-purpose
    arguments, landing in `x0`, `x1`, `x2` in order). Write that fact down
    as a small table, not as logic buried inside whatever code chooses
    where to place a value. Do not build a general table-driven code
    generator; one table, for one fact, is the whole exercise.

    Then write a test, separate from your compiler, that reads only your
    table and checks it against AAPCS64's own text: for a Vortex reference
    parameter, does the table say it lands in a general-purpose register,
    in argument order? The test should fail the moment the table is wrong,
    without needing to run your compiler or inspect any generated code at
    all, the same way a mistake in a `.td` file shows up in TableGen's own
    generated tables before any instruction is ever selected.

## Key ideas

!!! recap "You can now answer"

    - **Why does LLVM generate its register, instruction, calling-convention and scheduling tables instead of hand-writing them?** Because each kind of fact is needed by several shared, target-independent consumers (the register allocator, the assembler, the calling-convention state machine, the scheduler), and writing it once as data, then generating each consumer's table from it, rules out the consumers quietly disagreeing about the same fact.
    - **What is a TableGen class, and what is a def?** A class is a template: a named group of fields with no values yet. A def is one concrete record: a class (or a mix of classes) with every field filled in.
    - **What does it mean that "a record has no meaning until a backend interprets it"?** The record is only data; different TableGen backends (register-info, instruction-info, calling-convention, and others) read the same kind of record and ask different questions of it, so its effect on the compiler depends entirely on which backend is reading it.
    - **Given a write to one named view of a register, how do you decide whether another named view is unaffected, fully redefined, or partly disturbed?** Compare their bit ranges: no overlap is unaffected, the written range fully containing the other's range is a full redefinition, and any other overlap is a partial one.
    - **Why did `mixed_args.ll`'s two floating-point parameters land in `s0` and `s1` even though they were not adjacent in the argument list?** AAPCS64 keeps an independent counter for floating-point arguments; every floating-point argument advances that counter regardless of what kind of argument came before it in the list.
    - **Why can two different processors share one instruction set but need two different scheduling-model tables?** Latency and throughput are properties of a specific implementation's pipeline, not of the instruction set architecture it implements; two processors can execute the same `mul` instruction with different cycle counts.

## Where this comes back

!!! next "You will use this again in"

    - [C2. Liveness](c2-liveness.md): *subregister overlap deciding partial versus full definitions*
    - [C6. Instruction scheduling](c6-scheduling.md): *per-opcode latency and throughput driving list scheduling*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *the C++ classes TableGen's register-info, calling-convention and scheduling backends generate*
    - [P4. Seeing inside the CPU: counters and tools](../optimize/p4-counters-and-tools.md): *scheduling-model tables reused by `llvm-mca` to predict throughput without running the code*

## Sources and further reading

[^tablegen]: LLVM Project, "TableGen". <https://llvm.org/docs/TableGen/index.html>
[^tablegen-backends]: LLVM Project, "TableGen BackEnds". <https://llvm.org/docs/TableGen/BackEnds.html>
[^writing-backend]: LLVM Project, "Writing an LLVM Backend". <https://llvm.org/docs/WritingAnLLVMBackend.html>
[^codegen]: LLVM Project, "The LLVM Target-Independent Code Generator". <https://llvm.org/docs/CodeGenerator.html>
[^aapcs64]: Arm, "Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)", 2025Q4 release. <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
