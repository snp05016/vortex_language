# E2. Describing a target

<p class="page-intro">LLVM keeps what is true about a processor (its registers, its instructions, how calls pass arguments, how long each instruction takes) in declarative files, and a tool called TableGen turns them into the C++ tables that the shared parts of the code generator read. This chapter teaches you to read those descriptions, and gives Vortex's own back end the same habit: one place for each fact about a target.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 45 minutes · Builds on: [E1. The LLVM code generator pipeline](e1-llvm-codegen-pipeline.md), [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md), [A4. Calling conventions and ABIs](a4-calling-conventions.md), [B2. A second target: x86-64](b2-x86-64.md)</p>

???+ remember "Before you start, remember"

    ??? question "In the MIR line `%3:gpr32 = SUBSWrr %0, %1, implicit-def $nzcv`, what do `gpr32` and `SUBSWrr` name?"

        `gpr32` is the register class of the virtual register `%3`: the set
        of physical registers the allocator may later choose for it.
        `SUBSWrr` is a target instruction, the 32-bit flag-setting subtract
        with two register operands.

        Introduced in [E1. The LLVM code generator pipeline](e1-llvm-codegen-pipeline.md#machineinstr-and-mir-a-shared-language-mid-pipeline).

    ??? question "What happens to the upper 32 bits of `x0` when an instruction writes `w0`?"

        They become zero. `w0` names the low 32 bits of the same register
        as `x0`, and a write to the `w` name clears the rest.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md#registers-by-name).

    ??? question "On x86-64, an instruction writes the 16-bit register `ax`. What happens to the rest of `rax`?"

        Nothing: bits 16 to 63 keep their old values. Only a write to a
        32-bit name such as `eax` clears the upper half. A 16-bit or 8-bit
        write changes part of the register and leaves the rest alone.

        Introduced in [B2. A second target: x86-64](b2-x86-64.md#registers-by-another-name).

    ??? question "Where does AAPCS64 put the arguments of `f(i64, double, i64)`?"

        In `x0`, `d0` and `x1`. Integer and floating-point arguments are
        placed independently, so the `double` does not use up an integer
        register.

        Introduced in [A4. Calling conventions and ABIs](a4-calling-conventions.md#where-arguments-go-two-counters-not-one).

    ??? question "Which floating-point registers must a function preserve under AAPCS64, and how much of each?"

        `v8` to `v15`, and only their low 64 bits, the part named `d8` to
        `d15`. A caller that needs the upper half must save it itself.

        Introduced in [A4. Calling conventions and ABIs](a4-calling-conventions.md#caller-saved-and-callee-saved-registers).

!!! goals "In this chapter"

    - Read a TableGen class and `def`, predict the record that `llvm-tblgen` prints for it, and name the backend that will turn it into a table.
    - Work out, from a register's subregisters alone, whether a write to one register leaves another untouched, redefines it or changes part of it.
    - Trace an AArch64 argument placement in `llc` output back to the calling-convention rule that produced it, including the effect of register aliases.
    - Explain how a scheduling model attaches a latency to an instruction through a scheduling class, and why `llvm-mca` gives different answers for different `-mcpu` values.
    - Keep one target fact in one place in your own back end, and write the test that proves nothing else restates it.

Take one fact about AArch64: `w0` is the low 32 bits of `x0`.
[A2](a2-aarch64-assembly.md#registers-by-name) taught it to you as a reader of
assembly. A code generator needs it in at least four places. The assembler
must accept both names and encode them. The register allocator must know that
a value living in `x0` is damaged when something else is put in `w0`. The
calling-convention code must know that once an `i32` argument takes `w0`, the
next `i64` argument cannot have `x0`. The printer and the disassembler must
turn register numbers back into the right names.

Each of those four is target-independent code: one register allocator, one
assembler framework and one argument-lowering routine serve every target LLVM
supports. They cannot contain AArch64 facts, so they read them from tables.
LLVM does not write those tables by hand. Each target writes its facts once,
in a small declarative language, and a generator produces the tables, so the
allocator and the assembler cannot disagree about `w0`: both read tables made
from the same record. This chapter reads that language and the four kinds of
fact a target states in it: registers, instructions, calling conventions and
scheduling models.

## One fact, many readers

The language is **TableGen**. A TableGen file holds **records**: named
collections of fields with values. The program `llvm-tblgen` parses the files
and then runs one of its **backends**, a generator that walks the records and
prints something, usually a C++ include file full of tables. The overview puts
the division of labour in one line: "TableGen files have no real meaning
without a backend."[^tg-index] (The word backend is overloaded here. A
TableGen backend is one of these generators, not a target's code generator.)

A target keeps its description in several `.td` files. Writing an LLVM
Backend, LLVM's guide for new targets, lists them for a target named XXX: a
main `XXX.td` that includes the rest, `XXXRegisterInfo.td` for registers,
`XXXInstrInfo.td` and `XXXInstrFormats.td` for instructions,
`XXXCallingConv.td` for calling conventions and `XXXSchedule.td` for
scheduling[^writing-backend]. The generated files come back as includes
such as `XXXGenRegisterInfo.inc`, `XXXGenInstrInfo.inc` and
`XXXGenCallingConv.inc`, which the target's hand-written C++ pulls
in[^writing-backend]. AArch64 follows this layout: `AArch64.td` includes
`AArch64RegisterInfo.td`, `AArch64CallingConvention.td`,
`AArch64InstrInfo.td`, `AArch64Schedule.td` and one scheduling file per
modelled core, from `AArch64SchedA53.td` to
`AArch64SchedNeoverseV2.td`[^a64-td].

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="One set of target records fanning out through llvm-tblgen into five generated tables, each read by a different target-independent part of the code generator" aria-describedby="e2-fan-desc">
<title id="e2-fan-title">One set of target records, five generated tables</title>
<desc id="e2-fan-desc">At the top, a box labelled target description, the .td records. An arrow leads down to llvm-tblgen. From it, five arrows fan out to five generated tables, each produced by a different TableGen backend option: register info (-gen-register-info), instruction info (-gen-instr-info), assembly writer and matcher (-gen-asm-writer, -gen-asm-matcher), calling convention (-gen-callingconv) and subtarget, which holds the scheduling models (-gen-subtarget). Under each table, the part of the code generator that reads it: register allocation and liveness; instruction selection and every pass that inspects instructions; the assembler, printer and disassembler; argument and return-value lowering; the machine scheduler and llvm-mca.</desc>
<rect class="vx-box-accent" x="250" y="10" width="260" height="34" rx="4"/>
<text class="vx-mono" x="380" y="32" text-anchor="middle">target description (.td)</text>
<line class="vx-line" x1="380" y1="44" x2="380" y2="68"/>
<polygon class="vx-arrowhead" points="374,66 380,76 386,66"/>
<rect class="vx-box-strong" x="310" y="76" width="140" height="32" rx="4"/>
<text class="vx-mono" x="380" y="97" text-anchor="middle">llvm-tblgen</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5">
<line class="vx-line" x1="340" y1="108" x2="82" y2="150"/>
<polygon class="vx-arrowhead" points="76,146 82,156 88,146"/>
<rect class="vx-box" x="12" y="156" width="140" height="44" rx="4"/>
<text class="vx-text" x="82" y="175" text-anchor="middle" font-size="13">register info</text>
<text class="vx-mono" x="82" y="192" text-anchor="middle" font-size="10">-gen-register-info</text>
<text class="vx-text-muted" x="82" y="222" text-anchor="middle" font-size="12">register allocation,</text>
<text class="vx-text-muted" x="82" y="238" text-anchor="middle" font-size="12">liveness</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5">
<line class="vx-line" x1="360" y1="108" x2="231" y2="150"/>
<polygon class="vx-arrowhead" points="225,146 231,156 237,146"/>
<rect class="vx-box" x="161" y="156" width="140" height="44" rx="4"/>
<text class="vx-text" x="231" y="175" text-anchor="middle" font-size="13">instruction info</text>
<text class="vx-mono" x="231" y="192" text-anchor="middle" font-size="10">-gen-instr-info</text>
<text class="vx-text-muted" x="231" y="222" text-anchor="middle" font-size="12">selection, and every</text>
<text class="vx-text-muted" x="231" y="238" text-anchor="middle" font-size="12">pass that reads MIR</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5">
<line class="vx-line" x1="380" y1="108" x2="380" y2="150"/>
<polygon class="vx-arrowhead" points="374,146 380,156 386,146"/>
<rect class="vx-box" x="310" y="156" width="140" height="44" rx="4"/>
<text class="vx-text" x="380" y="175" text-anchor="middle" font-size="13">asm writer, matcher</text>
<text class="vx-mono" x="380" y="192" text-anchor="middle" font-size="10">-gen-asm-writer ...</text>
<text class="vx-text-muted" x="380" y="222" text-anchor="middle" font-size="12">printer, assembler,</text>
<text class="vx-text-muted" x="380" y="238" text-anchor="middle" font-size="12">disassembler</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5">
<line class="vx-line" x1="400" y1="108" x2="529" y2="150"/>
<polygon class="vx-arrowhead" points="523,146 529,156 535,146"/>
<rect class="vx-box" x="459" y="156" width="140" height="44" rx="4"/>
<text class="vx-text" x="529" y="175" text-anchor="middle" font-size="13">calling convention</text>
<text class="vx-mono" x="529" y="192" text-anchor="middle" font-size="10">-gen-callingconv</text>
<text class="vx-text-muted" x="529" y="222" text-anchor="middle" font-size="12">argument and</text>
<text class="vx-text-muted" x="529" y="238" text-anchor="middle" font-size="12">return lowering</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 5">
<line class="vx-line" x1="420" y1="108" x2="678" y2="150"/>
<polygon class="vx-arrowhead" points="672,146 678,156 684,146"/>
<rect class="vx-box" x="608" y="156" width="140" height="44" rx="4"/>
<text class="vx-text" x="678" y="175" text-anchor="middle" font-size="13">subtarget, schedules</text>
<text class="vx-mono" x="678" y="192" text-anchor="middle" font-size="10">-gen-subtarget</text>
<text class="vx-text-muted" x="678" y="222" text-anchor="middle" font-size="12">machine scheduler,</text>
<text class="vx-text-muted" x="678" y="238" text-anchor="middle" font-size="12">llvm-mca</text>
</g>
<text class="vx-text-accent" x="380" y="280" text-anchor="middle">each reader is shared by every target; only the tables differ</text>
</svg>
<figcaption>Figure 1. One description, many tables. Each TableGen backend reads the records it understands and writes one generated file; a different target-independent part of the code generator reads each file. Adding a register or fixing a latency means editing a record, and every reader picks up the change on the next build.</figcaption>
</figure>

Table-driven design has a cost, too. The facts become cheap to change and hard
to break, but finding one means reading records that a generator will
interpret, and the meaning of a field lives in the generator's C++, not next
to the field. The rest of this chapter is practice at that reading.

## Records, classes and defs

Here is a complete TableGen file. It is original to this chapter, describes a
toy register file, and uses no LLVM definitions:

```text
class ToyReg<string asm, int number> {
  string AsmName = asm;
  int Encoding = number;
  bit Reserved = false;
}

def R0 : ToyReg<"r0", 0>;
def R1 : ToyReg<"r1", 1>;
let Reserved = true in
def SP : ToyReg<"sp", 31>;
```

A **class** is an abstract record, a template for others; a **def** is a
concrete record, the kind backends
consume[^tg-progref]. `ToyReg` takes two **template arguments**, `asm` and
`number`, whose values fill fields when a def inherits from the
class[^tg-progref]. Each field has a type (`string`, `int`, `bit`, and others
such as lists and `dag`) and may have a default, like `Reserved`. The `let
... in` line overrides a field for the defs it encloses; TableGen applies such
overrides after the record has inherited all its fields, which is how one
register can differ from its siblings without a class of its
own[^tg-progref].

Run `llvm-tblgen` on the file with no options and it prints every record, which
is the default backend (`--print-records`, per `llvm-tblgen --help`). With LLVM
18.1.8 on the M4 Pro, the defs came out like this:

```text
def R0 {	// ToyReg
  string AsmName = "r0";
  int Encoding = 0;
  bit Reserved = 0;
}
def R1 {	// ToyReg
  string AsmName = "r1";
  int Encoding = 1;
  bit Reserved = 0;
}
def SP {	// ToyReg
  string AsmName = "sp";
  int Encoding = 31;
  bit Reserved = 1;
}
```

Every field is resolved: the template arguments are substituted, the default
filled in and the `let` applied. The comment after each name lists the classes
the record came from. Nothing in the output says that `SP` is a stack pointer
or that `Reserved` keeps it away from an allocator. Those meanings would come
from a backend written to read `ToyReg` records, and no such backend exists,
so this file means nothing yet.

How does a backend find its records? By class. `--dump-json` prints the same
records as JSON, including an index from each class to the defs that inherit
from it (here, `ToyReg` maps to `R0`, `R1` and `SP`). In C++, a backend asks
the record keeper for "all the concrete records that inherit from" a
class[^tg-record]. The calling-convention backend, for instance, starts from
every record derived from `CallingConv`[^cc-emitter]. A record's class is the
contract between the file and the generator: a def derived from `Register` is
one the register backend will read.

The backends that matter for a code generator, with the option that runs each
and what it produces[^tg-backends]:

| Option | Backend | What it writes |
| --- | --- | --- |
| `-gen-register-info` | RegisterInfo | enums and tables for registers, register classes and subregisters |
| `-gen-instr-info` | InstrInfo | the opcode enum and a descriptor per instruction |
| `-gen-asm-writer` | AsmWriter | the instruction printer |
| `-gen-asm-matcher` | AsmMatcher | the assembler's operand and mnemonic matcher |
| `-gen-disassembler` | Disassembler | decoding tables |
| `-gen-dag-isel` | DAGISel | the pattern matcher for SelectionDAG instruction selection |
| `-gen-callingconv` | CallingConv | one C++ function per calling convention |
| `-gen-subtarget` | Subtarget | processor and feature tables, including the scheduling models[^writing-backend] |

??? check "You add a field `bit Volatile = false;` to `ToyReg` and set it on `R1`. Which generated C++ file changes?"

    None. `--print-records` would show the new field, but no backend reads
    `Volatile`, so no table mentions it. A field matters only once some
    generator's C++ asks for it by name. In a real target, adding a field
    to an LLVM class such as `Register` would also mean changing the
    register backend and the C++ that reads its tables.

## Registers: names, classes and subregisters

LLVM's own `Register` class, in `Target.td`, has fields for the assembly name
(`AsmName`), the registers it overlaps (`Aliases`), the registers it contains
(`SubRegs`) with one **subregister index** for each (`SubRegIndices`), DWARF
numbers for debuggers and the hardware encoding (`HWEncoding`)[^target-td]. A
subregister index names a way of taking part of a register. It is declared
with `SubRegIndex<size, offset>`: the size of the part in bits and the offset
of its first bit[^target-td].

AArch64 uses this directly for A2's fact. `AArch64RegisterInfo.td` declares
an index `sub_32` of 32 bits, defines `W0` to `W30` as plain registers, and
defines each of `X0` to `X30` as a register whose one subregister is the `W`
register of the same number, reached through `sub_32`[^a64-regs]. The
floating-point bank nests deeper: `B` registers of 8 bits sit inside `H`
registers of 16, which sit inside `S` of 32, `D` of 64 and `Q` of 128, with
one index per step (`bsub`, `hsub`, `ssub`, `dsub`)[^a64-regs].

A **register class** is a set of registers that can hold the same kinds of
value. `RegisterClass` takes a namespace, the value types, an alignment and
the member list[^target-td]. AArch64's `GPR32` holds `i32` values in the `W`
registers plus `WZR`, `GPR64` holds `i64` in the `X` registers plus `XZR`,
and `FPR32` is the sequence `S0` to `S31`[^a64-regs]. The `gpr32` after a
virtual register in E1's MIR is this `GPR32` class. When instruction
selection gives a value the class `gpr32`, it promises the allocator only
that some member of that set will do.

### What the register backend makes of it

To see the generator's side, here is a toy target of our own with two 32-bit
registers, `A` and `B`, each split into 16-bit halves. It includes LLVM's
`Target.td`, so its records derive from the real `Register`, `SubRegIndex` and
`RegisterClass`:

```text
include "llvm/Target/Target.td"

def lo16 : SubRegIndex<16, 0>;
def hi16 : SubRegIndex<16, 16>;

class ToyReg<string n> : Register<n> { let Namespace = "Toy"; }

def AL : ToyReg<"al">;
def AH : ToyReg<"ah">;
let SubRegIndices = [lo16, hi16], CoveredBySubRegs = true in
def A : ToyReg<"a"> { let SubRegs = [AL, AH]; }
def BL : ToyReg<"bl">;
def BH : ToyReg<"bh">;
let SubRegIndices = [lo16, hi16], CoveredBySubRegs = true in
def B : ToyReg<"b"> { let SubRegs = [BL, BH]; }

def Half : RegisterClass<"Toy", [i16], 16, (add AL, AH, BL, BH)>;
def Word : RegisterClass<"Toy", [i32], 32, (add A, B)>;
```

(The full file also defines a trivial `Target` record, which this backend
requires.) `llvm-tblgen -I /usr/local/include -gen-register-info toy.td`
wrote a 579-line C++ file. Three pieces of it show the idea:

```text
namespace Toy {
enum {
  NoRegister,
  A = 1,
  AH = 2,
  AL = 3,
  B = 4,
  BH = 5,
  BL = 6,
  NUM_TARGET_REGS // 7
};
} // end namespace Toy

extern const MCRegisterInfo::SubRegCoveredBits ToySubRegIdxRanges[] = {
  { 65535, 65535 },
  { 16, 16 },	// hi16
  { 0, 16 },	// lo16
};
```

Every register became an enumerator, and every subregister index became an
(offset, size) pair, the form `MCRegisterInfo` stores as
`SubRegCoveredBits`[^mc-reginfo-h].

The third piece is a single call that
initializes the target's register information, and one of its arguments is
the number **4**, the count of **register units**. A register unit is one of
the smallest pieces the generator splits the register file into, built from
the registers that have no subregisters. Here the units are the four halves
`AL`, `AH`, `BL` and `BH`, and `A` is made of two of them. LLVM decides
whether two registers overlap by looking for a unit they have in common:
`MCRegisterInfo::regsOverlap` walks both registers' sorted unit lists and
returns true at the first match[^mc-reginfo]. Units turn every aliasing
question into a question about sets.

### Working out a partial write

The questions a liveness analysis ([C2](c2-liveness.md)) and a register
allocator ask are about writes. When an instruction writes one register, what
happens to each register that overlaps it? There are three answers. The other
register is **untouched** if they share no unit. It is **fully redefined** if
every one of its units is written; its old value is dead. It is **partly
changed** if some of its units are written and some are not: its value is
now a mix of old and new bits, so the old value is still partly live. B2's
16-bit write to `ax` inside `rax` is the classic partial change.

Take a toy register `R` of 32 bits with halves `RH` and `RL`, where `RL`
splits again into bytes `RL_HI` and `RL_LO`. The example below states only
which register contains which, as a target description does, and derives
everything else. A register with no subregisters becomes one unit. Every
other register is the union of its subregisters' units. Then one function
compares unit sets:

--8<-- "includes/examples/backend/e2-describing-a-target/subregister_effects.cpp.md"

Follow the first report by hand before reading it. The leaves, in order, are
`RH`, `RL_HI` and `RL_LO`, so they get units `u0`, `u1` and `u2`. `RL` is
`{u1, u2}` and `R` is `{u0, u1, u2}`. Writing `RL_LO` writes `{u2}`. `RH`
(`{u0}`) shares nothing with it: untouched. `RL_HI` (`{u1}`): untouched. `RL`
shares `u2` but keeps `u1`: partly changed. `R` shares `u2` and keeps `u0` and
`u1`: partly changed. Figure 2 draws the same case. The second report is
yours to predict: a write to `RL` covers `{u1, u2}`. Decide the fate of `R`,
`RH`, `RL_HI` and `RL_LO`, then compare with the program's output.

<figure class="vx-figure">
<svg viewBox="0 0 760 250" role="img" aria-label="A write to the byte RL_LO, shown against the nested registers R, RH, RL and RL_HI and the three register units they are built from" aria-describedby="e2-units-desc">
<title id="e2-units-title">What a write to RL_LO does to the registers around it</title>
<desc id="e2-units-desc">Bits 31 down to 0 run from left to right. The top bar is R, all 32 bits, marked partly changed. The second row has RH over bits 31 to 16, marked untouched, and RL over bits 15 to 0, marked partly changed. The third row has RL_HI over bits 15 to 8, untouched, and RL_LO over bits 7 to 0, highlighted as the register being written. Below, three unit boxes: u0 under RH, u1 under RL_HI and u2 under RL_LO, with u2 highlighted. R holds u0, u1 and u2; RL holds u1 and u2.</desc>
<text class="vx-text-muted" x="60" y="18">bit 31</text>
<text class="vx-text-muted" x="700" y="18" text-anchor="end">bit 0</text>
<rect class="vx-box-bad" x="60" y="26" width="640" height="34" rx="3"/>
<text class="vx-mono" x="380" y="48" text-anchor="middle">R: partly changed (u2 written, u0 and u1 kept)</text>
<rect class="vx-box" x="60" y="72" width="320" height="34" rx="3"/>
<text class="vx-mono" x="220" y="94" text-anchor="middle">RH: untouched</text>
<rect class="vx-box-bad" x="380" y="72" width="320" height="34" rx="3"/>
<text class="vx-mono" x="540" y="94" text-anchor="middle">RL: partly changed</text>
<rect class="vx-box" x="380" y="118" width="160" height="34" rx="3"/>
<text class="vx-mono" x="460" y="140" text-anchor="middle">RL_HI: untouched</text>
<rect class="vx-box-accent" x="540" y="118" width="160" height="34" rx="3"/>
<text class="vx-mono" x="620" y="140" text-anchor="middle">RL_LO: written</text>
<line class="vx-line" x1="60" y1="170" x2="700" y2="170"/>
<rect class="vx-box" x="60" y="182" width="320" height="30" rx="3"/>
<text class="vx-mono" x="220" y="202" text-anchor="middle">u0</text>
<rect class="vx-box" x="380" y="182" width="160" height="30" rx="3"/>
<text class="vx-mono" x="460" y="202" text-anchor="middle">u1</text>
<rect class="vx-cell-on" x="540" y="182" width="160" height="30" rx="3"/>
<text class="vx-mono" x="620" y="202" text-anchor="middle">u2</text>
<text class="vx-text-muted" x="380" y="238" text-anchor="middle">register units: one per register with no subregisters</text>
</svg>
<figcaption>Figure 2. A write to <code>RL_LO</code> writes only unit <code>u2</code>. A register that holds no <code>u2</code> is untouched; a register that holds <code>u2</code> and something else is partly changed, so its old value survives in part and the write counts as a use of it as well as a definition.</figcaption>
</figure>

The example assumes something that is not always true: that every register is
exactly the sum of its subregisters. `X0` breaks the assumption. Its only
subregister is `W0`, and bits 32 to 63 belong to no subregister at all. Build
units from that structure and `X0` and `W0` get the same single unit, so the
unit comparison would call a write to `w0` a full redefinition of `x0`. For
overlap questions that is harmless: the calling-convention code only needs to
know that the two cannot be handed out separately. For partial writes it is
wrong, so `Register` has a field for it, `CoveredBySubRegs`, false by
default and set only when the register's value is completely determined by
its subregisters. The comment beside it in `Target.td` uses x86 as the
example: `AX` is covered by `AL` and `AH`, while `EAX` is not covered by
`AX`[^target-td]. AArch64 leaves the field false for `X0` to `X30`[^a64-regs].
What the uncovered upper bits become after a write to `w0` (zero) is a fact
about the instructions, and no register record states it.

??? check "Change the toy so that `RL` has only one subregister, `RL_LO`, and no `RL_HI`. How many units are there, and is the program's verdict on a write to `RL_LO` still right?"

    Two: `RH` and `RL_LO` are the leaves, so `RH = {u0}`, `RL_LO = {u1}`,
    `RL = {u1}` and `R = {u0, u1}`. The program now reports that writing
    `RL_LO` fully redefines `RL`, which is wrong: bits 8 to 15 of `RL`
    belong to no subregister and keep their old value. This is the `EAX`
    and `AX` case. A description has to say which registers are covered by
    their subregisters, and a correct version of the program would treat a
    write to a subregister of an uncovered register as a partial change.

## Instructions: one record, many tables

An instruction record carries more fields than any one reader needs. LLVM's
`Instruction` class in `Target.td` includes the output operands
(`OutOperandList`, a `dag`, which is TableGen's type for a small tree
written in parentheses, here one beginning with `outs`), the input operands
(`InOperandList`, beginning with `ins`), the assembly template
(`AsmString`), the selection patterns (`Pattern`), registers read and written
implicitly (`Uses` and `Defs`), the size in bytes, flags such as `mayLoad`,
`mayStore`, `hasSideEffects` and `isCommutable`, and the list of scheduling
classes (`SchedRW`)[^target-td].

Here is one instruction for a toy target, again written for this chapter:

```text
def ADDrr : Instruction {
  let Namespace = "Toy";
  let OutOperandList = (outs GPR:$dst);
  let InOperandList = (ins GPR:$lhs, GPR:$rhs);
  let AsmString = "add $dst, $lhs, $rhs";
  let Size = 4;
  let hasSideEffects = false;
  let mayLoad = false;
  let mayStore = false;
  let isCommutable = true;
}
```

Running `-gen-instr-info` on the file (with three registers and a `GPR` class
alongside) put `ADDrr = 271` in the opcode enum. The first 271 numbers belong
to target-independent opcodes that `Target.td` and the files it includes
define for every target, such as `COPY`, which is 19, and the generic
`G_ADD` of GlobalISel. The descriptor row for `ADDrr` begins
`{ 271,	3,	1,	4, ...`: the opcode, three operands, one of them a
definition, and a size of 4 bytes, in the field order of
`MCInstrDesc`[^mc-instrdesc]. Its flags include `Commutable`, from
`isCommutable`. Running `-gen-asm-writer` on the same file produced a string
table containing `"add "`, the literal part of `AsmString`; the operands are
printed by generated code.

Figure 3 follows each field to the tables that read it.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="The fields of one instruction record, each connected to the generated tables that read it" aria-describedby="e2-instr-desc">
<title id="e2-instr-title">The fields of one instruction record and their readers</title>
<desc id="e2-instr-desc">On the left, one instruction record listing six fields: the output and input operand lists, the assembly string, the selection pattern, the implicit Defs and Uses, the size and encoding bits, and the scheduling classes. On the right, five generated tables. The operand lists feed instruction info, the assembly writer and matcher, and the DAG instruction selector. The assembly string feeds the assembly writer and matcher. The pattern feeds the DAG instruction selector. Defs and Uses feed instruction info, which becomes implicit operands such as implicit-def $nzcv in MIR. Size and encoding bits feed instruction info and the code emitter and disassembler. The scheduling classes feed the subtarget's scheduling tables.</desc>
<rect class="vx-box-strong" x="20" y="20" width="300" height="290" rx="6"/>
<text class="vx-mono" x="36" y="46">def ADDrr : Instruction</text>
<text class="vx-mono" x="36" y="86" font-size="12">OutOperandList, InOperandList</text>
<text class="vx-mono" x="36" y="126" font-size="12">AsmString</text>
<text class="vx-mono" x="36" y="166" font-size="12">Pattern</text>
<text class="vx-mono" x="36" y="206" font-size="12">Defs, Uses</text>
<text class="vx-mono" x="36" y="246" font-size="12">Size, encoding bits</text>
<text class="vx-mono" x="36" y="286" font-size="12">SchedRW</text>
<rect class="vx-box" x="500" y="30" width="240" height="40" rx="4"/>
<text class="vx-text" x="620" y="55" text-anchor="middle">instruction info</text>
<rect class="vx-box" x="500" y="90" width="240" height="40" rx="4"/>
<text class="vx-text" x="620" y="115" text-anchor="middle">asm writer and matcher</text>
<rect class="vx-box" x="500" y="150" width="240" height="40" rx="4"/>
<text class="vx-text" x="620" y="175" text-anchor="middle">DAG instruction selector</text>
<rect class="vx-box" x="500" y="210" width="240" height="40" rx="4"/>
<text class="vx-text" x="620" y="235" text-anchor="middle">code emitter, disassembler</text>
<rect class="vx-box" x="500" y="270" width="240" height="40" rx="4"/>
<text class="vx-text" x="620" y="295" text-anchor="middle">subtarget scheduling tables</text>
<path class="vx-flow" d="M270 82 L500 50"/>
<path class="vx-flow" d="M270 82 L500 110"/>
<path class="vx-flow" d="M270 82 L500 170"/>
<path class="vx-flow" d="M150 122 L500 110"/>
<path class="vx-flow" d="M120 162 L500 170"/>
<path class="vx-flow" d="M150 202 L500 50"/>
<path class="vx-flow" d="M200 242 L500 50"/>
<path class="vx-flow" d="M200 242 L500 230"/>
<path class="vx-flow" d="M120 282 L500 290"/>
</svg>
<figcaption>Figure 3. One instruction record, read in pieces. No generator reads every field, and most fields are read by more than one generator. That overlap is the point: the printer and the matcher cannot disagree about an operand's position, because both were generated from the same operand list.</figcaption>
</figure>

Two fields explain lines you have already read. E1's MIR showed
`SUBSWrr %0, %1, implicit-def $nzcv`. The `implicit-def` operand is there
because the multiclass that defines `SUBSWrr` sets `Defs = [NZCV]` for all
its instructions[^a64-formats], so every `SUBSWrr` the selector creates
carries it, and the scheduler and allocator
see the flags written without special code. The `Pattern` field holds a
`dag` that the DAG selector matches, such as "set a `GPR` to the `add` of two
`GPR`s"; targets also write standalone `Pat<match, result>`
records[^tg-selectiondag] when one instruction implements several IR shapes.
[C1](c1-instruction-selection.md) teaches the matching itself.

Real instruction sets have families: the same operation in 32 and 64 bits,
with a register or an immediate operand. A **multiclass** writes the family
once, and a `defm` stamps it out, defining several records at
once[^tg-progref]:

```text
class ToyInst<string asm, int size> {
  string AsmString = asm;
  int Size = size;
  bit MayLoad = false;
}

multiclass TwoForms<string mnemonic> {
  def rr : ToyInst<mnemonic # " $dst, $lhs, $rhs", 4>;
  def ri : ToyInst<mnemonic # " $dst, $lhs, #$imm", 4>;
}

defm ADD : TwoForms<"add">;
defm SUB : TwoForms<"sub">;
```

`llvm-tblgen` printed four defs, `ADDri`, `ADDrr`, `SUBri` and `SUBrr`: the
`defm` name is glued to each inner name, and `#` pasted the mnemonic into
each template. AArch64's names are built the same way. Its `MulAccum`
multiclass defines `Wrrr` and `Xrrr` forms, 32-bit and 64-bit
multiply-add[^a64-formats], and `AArch64InstrInfo.td` instantiates it as
`defm MADD`[^a64-instrinfo], producing `MADDWrrr` and `MADDXrrr`. Ask the
assembler which instruction `mul x2, x1, x1` is
(`llvm-mc -triple=aarch64 -show-inst`) and it answers `MADDXrrr`: `mul` is
an alias, a multiply-add whose addend is the zero register. The suffix
habits (`W` or `X` for width, then usually `r` or `i` per operand) are worth
learning,
because MIR, `-print-after-all` dumps and scheduling models all use these
names.

??? check "You edit one instruction's `AsmString` so that its two source operands are printed in the opposite order. Which generated tables change, and what breaks?"

    The assembly writer and the assembly matcher change; the instruction
    info, the selector and the scheduling tables do not. Printed assembly
    now shows the sources in swapped order, and the assembler reads text
    with the same swap, so the two agree with each other. What breaks is
    agreement with the outside world: other assemblers, and code written
    from the architecture manual, put the operands the other way. For a commutative `add` the computed value is the same; for
    `sub` every hand-written or foreign assembly file would compute the
    wrong result.

## Calling conventions: rules, not counters

A4 described AAPCS64 with two counters: each integer argument takes the next
`x` register, each floating-point argument the next `v` register. LLVM does
not keep counters. It keeps an ordered list of **rules**, and for each
argument it tries the rules from the top until one assigns a place. The rule
classes live in `TargetCallingConv.td`[^target-cc]. `CCIfType` applies its
action only to arguments of the listed types; `CCAssignToReg` "assigns the argument value to
the first available register" in its list; `CCAssignToStack` takes a stack
slot of a given size and alignment; `CCPromoteToType` widens a value first;
`CCDelegateTo` hands over to another convention[^writing-backend].

AArch64 has two main conventions for ordinary calls. `CC_AArch64_AAPCS` is
the standard one, used on Linux. `CC_AArch64_DarwinPCS` is Apple's, and it
is the one `llc` used for every example on this page: its default target is
`arm64-apple-darwin27.0.0`, and for a Darwin target the AArch64 lowering
code picks `CC_AArch64_DarwinPCS` for an ordinary C call without variable
arguments[^a64-isel]. For the scalar types in this chapter
both say the same thing: promote `i1`, `i8` and `i16` to `i32`; `i32` takes
the first free of `W0` to `W7`; `i64` the first free of `X0` to `X7`; `f32`
the first free of `S0` to `S7`; `f64` the first free of `D0` to
`D7`[^a64-cc].

They differ on the stack: the Darwin convention gives an `i32`
or `f32` a 4-byte slot where the standard one gives 8, one of the deviations
[A4](a4-calling-conventions.md#apples-arm64-deviations)
describes[^a64-cc].

### What the calling-convention backend generates

Rules become code. Here is a toy convention for the `A`/`B` target from the
register section:

```text
def CC_Toy : CallingConv<[
  CCIfType<[i16], CCAssignToReg<[AL, BL]>>,
  CCIfType<[i32], CCAssignToReg<[A, B]>>,
  CCAssignToStack<4, 4>
]>;
```

`llvm-tblgen -gen-callingconv` turned it into one C++ function. Its body,
lightly trimmed:

```text
  if (LocVT == MVT::i16) {
    static const MCPhysReg RegList1[] = {
      Toy::AL, Toy::BL
    };
    if (unsigned Reg = State.AllocateReg(RegList1)) {
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
      return false;
    }
  }
  ...
  int64_t Offset3 = State.AllocateStack(4, Align(4));
```

Each rule became an `if`, each register list a static array, and all the
state lives in a `CCState` object that the lowering code creates for each
call and each function body[^cc-emitter]. `CCState::AllocateReg` returns the
first register in the list that is not yet allocated and marks it allocated
"and any aliases"[^ccstate-h]. `MarkAllocated` loops over every alias of the
register, the register itself included, and sets a bit for each[^ccstate].

That one detail is where A4's two counters come from. `W0` and `X0` are
aliases, so an `i32` that takes `W0` also uses up `X0`, and the next `i64`
gets `X1`. `S0` and `D0` are aliases, so a `float` in `S0` pushes the next
`double` to `D1`. The integer rules share one pool of registers and the
floating-point rules share another. Counting per bank, as A4 did, is a
summary of how the pools empty.

### Walking the rules by hand

The example below is a small interpreter for the four scalar rules. Its
registers are stored as one flag per physical register, so taking `w` or `x`
number *i* sets the same flag:

--8<-- "includes/examples/backend/e2-describing-a-target/cc_rules.cpp.md"

Figure 4 walks the first call of the example, one argument at a time.

<figure class="vx-figure">
<div class="vx-stepper">
<div class="vx-step">
<p><strong>Step 1. a: i32.</strong> Integer pool empty. W0 is free, so a takes w0, and register 0 of the integer pool is now used, under both of its names, w0 and x0.</p>
<svg viewBox="0 0 760 210" role="img" aria-label="Step 1: argument a of type i32 matches the rule CCIfType&lt;[i32]&gt; → first free of W0–W7 and takes w0. Used so far, general-purpose: w0; floating-point: none.">
<rect class="vx-box-accent" x="20" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="88" y="34" text-anchor="middle" font-size="13">a: i32 → w0</text>
<rect class="vx-box" x="168" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="236" y="34" text-anchor="middle" font-size="13">b: float</text>
<rect class="vx-box" x="316" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="384" y="34" text-anchor="middle" font-size="13">c: i64</text>
<rect class="vx-box" x="464" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="532" y="34" text-anchor="middle" font-size="13">d: double</text>
<rect class="vx-box" x="612" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="680" y="34" text-anchor="middle" font-size="13">e: i32</text>
<text class="vx-text" x="20" y="76">rule: CCIfType&lt;[i32]&gt; → first free of W0–W7</text>
<text class="vx-text-muted" x="20" y="122">general-purpose, register 0 to 7</text>
<rect class="vx-cell-on" x="280" y="100" width="52" height="34" rx="3"/>
<text class="vx-mono" x="306" y="115" text-anchor="middle" font-size="11">w0</text>
<text class="vx-text-muted" x="306" y="129" text-anchor="middle" font-size="10">(x0)</text>
<rect class="vx-box" x="338" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="364" y="122" text-anchor="middle" font-size="11">1</text>
<rect class="vx-box" x="396" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="422" y="122" text-anchor="middle" font-size="11">2</text>
<rect class="vx-box" x="454" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="480" y="122" text-anchor="middle" font-size="11">3</text>
<rect class="vx-box" x="512" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="538" y="122" text-anchor="middle" font-size="11">4</text>
<rect class="vx-box" x="570" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="596" y="122" text-anchor="middle" font-size="11">5</text>
<rect class="vx-box" x="628" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="654" y="122" text-anchor="middle" font-size="11">6</text>
<rect class="vx-box" x="686" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="712" y="122" text-anchor="middle" font-size="11">7</text>
<text class="vx-text-muted" x="20" y="174">floating-point, register 0 to 7</text>
<rect class="vx-box" x="280" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="306" y="174" text-anchor="middle" font-size="11">0</text>
<rect class="vx-box" x="338" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="364" y="174" text-anchor="middle" font-size="11">1</text>
<rect class="vx-box" x="396" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="422" y="174" text-anchor="middle" font-size="11">2</text>
<rect class="vx-box" x="454" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="480" y="174" text-anchor="middle" font-size="11">3</text>
<rect class="vx-box" x="512" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="538" y="174" text-anchor="middle" font-size="11">4</text>
<rect class="vx-box" x="570" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="596" y="174" text-anchor="middle" font-size="11">5</text>
<rect class="vx-box" x="628" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="654" y="174" text-anchor="middle" font-size="11">6</text>
<rect class="vx-box" x="686" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="712" y="174" text-anchor="middle" font-size="11">7</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 2. b: float.</strong> The floating-point pool is separate and empty. S0 is free, so b takes s0; register 0 of that pool is now used as s0 and d0.</p>
<svg viewBox="0 0 760 210" role="img" aria-label="Step 2: argument b of type float matches the rule CCIfType&lt;[f32]&gt; → first free of S0–S7 and takes s0. Used so far, general-purpose: w0; floating-point: s0.">
<rect class="vx-box-strong" x="20" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="88" y="34" text-anchor="middle" font-size="13">a: i32 → w0</text>
<rect class="vx-box-accent" x="168" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="236" y="34" text-anchor="middle" font-size="13">b: float → s0</text>
<rect class="vx-box" x="316" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="384" y="34" text-anchor="middle" font-size="13">c: i64</text>
<rect class="vx-box" x="464" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="532" y="34" text-anchor="middle" font-size="13">d: double</text>
<rect class="vx-box" x="612" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="680" y="34" text-anchor="middle" font-size="13">e: i32</text>
<text class="vx-text" x="20" y="76">rule: CCIfType&lt;[f32]&gt; → first free of S0–S7</text>
<text class="vx-text-muted" x="20" y="122">general-purpose, register 0 to 7</text>
<rect class="vx-cell-on" x="280" y="100" width="52" height="34" rx="3"/>
<text class="vx-mono" x="306" y="115" text-anchor="middle" font-size="11">w0</text>
<text class="vx-text-muted" x="306" y="129" text-anchor="middle" font-size="10">(x0)</text>
<rect class="vx-box" x="338" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="364" y="122" text-anchor="middle" font-size="11">1</text>
<rect class="vx-box" x="396" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="422" y="122" text-anchor="middle" font-size="11">2</text>
<rect class="vx-box" x="454" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="480" y="122" text-anchor="middle" font-size="11">3</text>
<rect class="vx-box" x="512" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="538" y="122" text-anchor="middle" font-size="11">4</text>
<rect class="vx-box" x="570" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="596" y="122" text-anchor="middle" font-size="11">5</text>
<rect class="vx-box" x="628" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="654" y="122" text-anchor="middle" font-size="11">6</text>
<rect class="vx-box" x="686" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="712" y="122" text-anchor="middle" font-size="11">7</text>
<text class="vx-text-muted" x="20" y="174">floating-point, register 0 to 7</text>
<rect class="vx-cell-on" x="280" y="152" width="52" height="34" rx="3"/>
<text class="vx-mono" x="306" y="167" text-anchor="middle" font-size="11">s0</text>
<text class="vx-text-muted" x="306" y="181" text-anchor="middle" font-size="10">(d0)</text>
<rect class="vx-box" x="338" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="364" y="174" text-anchor="middle" font-size="11">1</text>
<rect class="vx-box" x="396" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="422" y="174" text-anchor="middle" font-size="11">2</text>
<rect class="vx-box" x="454" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="480" y="174" text-anchor="middle" font-size="11">3</text>
<rect class="vx-box" x="512" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="538" y="174" text-anchor="middle" font-size="11">4</text>
<rect class="vx-box" x="570" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="596" y="174" text-anchor="middle" font-size="11">5</text>
<rect class="vx-box" x="628" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="654" y="174" text-anchor="middle" font-size="11">6</text>
<rect class="vx-box" x="686" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="712" y="174" text-anchor="middle" font-size="11">7</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 3. c: i64.</strong> The i64 rule scans X0 to X7. X0 is already used, because a took w0, its alias. The first free one is X1.</p>
<svg viewBox="0 0 760 210" role="img" aria-label="Step 3: argument c of type i64 matches the rule CCIfType&lt;[i64]&gt; → first free of X0–X7 and takes x1. Used so far, general-purpose: w0, x1; floating-point: s0.">
<rect class="vx-box-strong" x="20" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="88" y="34" text-anchor="middle" font-size="13">a: i32 → w0</text>
<rect class="vx-box-strong" x="168" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="236" y="34" text-anchor="middle" font-size="13">b: float → s0</text>
<rect class="vx-box-accent" x="316" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="384" y="34" text-anchor="middle" font-size="13">c: i64 → x1</text>
<rect class="vx-box" x="464" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="532" y="34" text-anchor="middle" font-size="13">d: double</text>
<rect class="vx-box" x="612" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="680" y="34" text-anchor="middle" font-size="13">e: i32</text>
<text class="vx-text" x="20" y="76">rule: CCIfType&lt;[i64]&gt; → first free of X0–X7</text>
<text class="vx-text-muted" x="20" y="122">general-purpose, register 0 to 7</text>
<rect class="vx-cell-on" x="280" y="100" width="52" height="34" rx="3"/>
<text class="vx-mono" x="306" y="115" text-anchor="middle" font-size="11">w0</text>
<text class="vx-text-muted" x="306" y="129" text-anchor="middle" font-size="10">(x0)</text>
<rect class="vx-cell-on" x="338" y="100" width="52" height="34" rx="3"/>
<text class="vx-mono" x="364" y="115" text-anchor="middle" font-size="11">x1</text>
<text class="vx-text-muted" x="364" y="129" text-anchor="middle" font-size="10">(w1)</text>
<rect class="vx-box" x="396" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="422" y="122" text-anchor="middle" font-size="11">2</text>
<rect class="vx-box" x="454" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="480" y="122" text-anchor="middle" font-size="11">3</text>
<rect class="vx-box" x="512" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="538" y="122" text-anchor="middle" font-size="11">4</text>
<rect class="vx-box" x="570" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="596" y="122" text-anchor="middle" font-size="11">5</text>
<rect class="vx-box" x="628" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="654" y="122" text-anchor="middle" font-size="11">6</text>
<rect class="vx-box" x="686" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="712" y="122" text-anchor="middle" font-size="11">7</text>
<text class="vx-text-muted" x="20" y="174">floating-point, register 0 to 7</text>
<rect class="vx-cell-on" x="280" y="152" width="52" height="34" rx="3"/>
<text class="vx-mono" x="306" y="167" text-anchor="middle" font-size="11">s0</text>
<text class="vx-text-muted" x="306" y="181" text-anchor="middle" font-size="10">(d0)</text>
<rect class="vx-box" x="338" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="364" y="174" text-anchor="middle" font-size="11">1</text>
<rect class="vx-box" x="396" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="422" y="174" text-anchor="middle" font-size="11">2</text>
<rect class="vx-box" x="454" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="480" y="174" text-anchor="middle" font-size="11">3</text>
<rect class="vx-box" x="512" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="538" y="174" text-anchor="middle" font-size="11">4</text>
<rect class="vx-box" x="570" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="596" y="174" text-anchor="middle" font-size="11">5</text>
<rect class="vx-box" x="628" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="654" y="174" text-anchor="middle" font-size="11">6</text>
<rect class="vx-box" x="686" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="712" y="174" text-anchor="middle" font-size="11">7</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 4. d: double.</strong> The f64 rule scans D0 to D7. D0 is used through its alias s0, so d takes d1.</p>
<svg viewBox="0 0 760 210" role="img" aria-label="Step 4: argument d of type double matches the rule CCIfType&lt;[f64]&gt; → first free of D0–D7 and takes d1. Used so far, general-purpose: w0, x1; floating-point: s0, d1.">
<rect class="vx-box-strong" x="20" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="88" y="34" text-anchor="middle" font-size="13">a: i32 → w0</text>
<rect class="vx-box-strong" x="168" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="236" y="34" text-anchor="middle" font-size="13">b: float → s0</text>
<rect class="vx-box-strong" x="316" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="384" y="34" text-anchor="middle" font-size="13">c: i64 → x1</text>
<rect class="vx-box-accent" x="464" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="532" y="34" text-anchor="middle" font-size="13">d: double → d1</text>
<rect class="vx-box" x="612" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="680" y="34" text-anchor="middle" font-size="13">e: i32</text>
<text class="vx-text" x="20" y="76">rule: CCIfType&lt;[f64]&gt; → first free of D0–D7</text>
<text class="vx-text-muted" x="20" y="122">general-purpose, register 0 to 7</text>
<rect class="vx-cell-on" x="280" y="100" width="52" height="34" rx="3"/>
<text class="vx-mono" x="306" y="115" text-anchor="middle" font-size="11">w0</text>
<text class="vx-text-muted" x="306" y="129" text-anchor="middle" font-size="10">(x0)</text>
<rect class="vx-cell-on" x="338" y="100" width="52" height="34" rx="3"/>
<text class="vx-mono" x="364" y="115" text-anchor="middle" font-size="11">x1</text>
<text class="vx-text-muted" x="364" y="129" text-anchor="middle" font-size="10">(w1)</text>
<rect class="vx-box" x="396" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="422" y="122" text-anchor="middle" font-size="11">2</text>
<rect class="vx-box" x="454" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="480" y="122" text-anchor="middle" font-size="11">3</text>
<rect class="vx-box" x="512" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="538" y="122" text-anchor="middle" font-size="11">4</text>
<rect class="vx-box" x="570" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="596" y="122" text-anchor="middle" font-size="11">5</text>
<rect class="vx-box" x="628" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="654" y="122" text-anchor="middle" font-size="11">6</text>
<rect class="vx-box" x="686" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="712" y="122" text-anchor="middle" font-size="11">7</text>
<text class="vx-text-muted" x="20" y="174">floating-point, register 0 to 7</text>
<rect class="vx-cell-on" x="280" y="152" width="52" height="34" rx="3"/>
<text class="vx-mono" x="306" y="167" text-anchor="middle" font-size="11">s0</text>
<text class="vx-text-muted" x="306" y="181" text-anchor="middle" font-size="10">(d0)</text>
<rect class="vx-cell-on" x="338" y="152" width="52" height="34" rx="3"/>
<text class="vx-mono" x="364" y="167" text-anchor="middle" font-size="11">d1</text>
<text class="vx-text-muted" x="364" y="181" text-anchor="middle" font-size="10">(s1)</text>
<rect class="vx-box" x="396" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="422" y="174" text-anchor="middle" font-size="11">2</text>
<rect class="vx-box" x="454" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="480" y="174" text-anchor="middle" font-size="11">3</text>
<rect class="vx-box" x="512" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="538" y="174" text-anchor="middle" font-size="11">4</text>
<rect class="vx-box" x="570" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="596" y="174" text-anchor="middle" font-size="11">5</text>
<rect class="vx-box" x="628" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="654" y="174" text-anchor="middle" font-size="11">6</text>
<rect class="vx-box" x="686" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="712" y="174" text-anchor="middle" font-size="11">7</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 5. e: i32.</strong> The i32 rule scans W0 to W7. W0 and W1 are used through their aliases, so e takes w2.</p>
<svg viewBox="0 0 760 210" role="img" aria-label="Step 5: argument e of type i32 matches the rule CCIfType&lt;[i32]&gt; → first free of W0–W7 and takes w2. Used so far, general-purpose: w0, x1, w2; floating-point: s0, d1.">
<rect class="vx-box-strong" x="20" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="88" y="34" text-anchor="middle" font-size="13">a: i32 → w0</text>
<rect class="vx-box-strong" x="168" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="236" y="34" text-anchor="middle" font-size="13">b: float → s0</text>
<rect class="vx-box-strong" x="316" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="384" y="34" text-anchor="middle" font-size="13">c: i64 → x1</text>
<rect class="vx-box-strong" x="464" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="532" y="34" text-anchor="middle" font-size="13">d: double → d1</text>
<rect class="vx-box-accent" x="612" y="12" width="136" height="34" rx="4"/>
<text class="vx-mono" x="680" y="34" text-anchor="middle" font-size="13">e: i32 → w2</text>
<text class="vx-text" x="20" y="76">rule: CCIfType&lt;[i32]&gt; → first free of W0–W7</text>
<text class="vx-text-muted" x="20" y="122">general-purpose, register 0 to 7</text>
<rect class="vx-cell-on" x="280" y="100" width="52" height="34" rx="3"/>
<text class="vx-mono" x="306" y="115" text-anchor="middle" font-size="11">w0</text>
<text class="vx-text-muted" x="306" y="129" text-anchor="middle" font-size="10">(x0)</text>
<rect class="vx-cell-on" x="338" y="100" width="52" height="34" rx="3"/>
<text class="vx-mono" x="364" y="115" text-anchor="middle" font-size="11">x1</text>
<text class="vx-text-muted" x="364" y="129" text-anchor="middle" font-size="10">(w1)</text>
<rect class="vx-cell-on" x="396" y="100" width="52" height="34" rx="3"/>
<text class="vx-mono" x="422" y="115" text-anchor="middle" font-size="11">w2</text>
<text class="vx-text-muted" x="422" y="129" text-anchor="middle" font-size="10">(x2)</text>
<rect class="vx-box" x="454" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="480" y="122" text-anchor="middle" font-size="11">3</text>
<rect class="vx-box" x="512" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="538" y="122" text-anchor="middle" font-size="11">4</text>
<rect class="vx-box" x="570" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="596" y="122" text-anchor="middle" font-size="11">5</text>
<rect class="vx-box" x="628" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="654" y="122" text-anchor="middle" font-size="11">6</text>
<rect class="vx-box" x="686" y="100" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="712" y="122" text-anchor="middle" font-size="11">7</text>
<text class="vx-text-muted" x="20" y="174">floating-point, register 0 to 7</text>
<rect class="vx-cell-on" x="280" y="152" width="52" height="34" rx="3"/>
<text class="vx-mono" x="306" y="167" text-anchor="middle" font-size="11">s0</text>
<text class="vx-text-muted" x="306" y="181" text-anchor="middle" font-size="10">(d0)</text>
<rect class="vx-cell-on" x="338" y="152" width="52" height="34" rx="3"/>
<text class="vx-mono" x="364" y="167" text-anchor="middle" font-size="11">d1</text>
<text class="vx-text-muted" x="364" y="181" text-anchor="middle" font-size="10">(s1)</text>
<rect class="vx-box" x="396" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="422" y="174" text-anchor="middle" font-size="11">2</text>
<rect class="vx-box" x="454" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="480" y="174" text-anchor="middle" font-size="11">3</text>
<rect class="vx-box" x="512" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="538" y="174" text-anchor="middle" font-size="11">4</text>
<rect class="vx-box" x="570" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="596" y="174" text-anchor="middle" font-size="11">5</text>
<rect class="vx-box" x="628" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="654" y="174" text-anchor="middle" font-size="11">6</text>
<rect class="vx-box" x="686" y="152" width="52" height="34" rx="3"/>
<text class="vx-text-muted" x="712" y="174" text-anchor="middle" font-size="11">7</text>
</svg>
</div>
</div>
<figcaption>Figure 4. The rules applied to <code>mix(i32, float, i64, double, i32)</code>, one argument per step. Each pool has eight physical registers; a filled cell is used, under both of its names (the alias in brackets). The arguments land in <code>w0</code>, <code>s0</code>, <code>x1</code>, <code>d1</code> and <code>w2</code>: no counter is kept, yet each pool is used up in order.</figcaption>
</figure>

The second line of output shows the stack rule. Eight `i64` arguments empty
the general-purpose pool, so the ninth falls through its `CCIfType` rule
(no register free) to the stack rule and gets offset 0. The `f32` after it
still finds `s0` free, because the floating-point pool was never touched.

Now check the interpreter against the real code generator. The IR below has
the same signature as the first line of output:

--8<-- "includes/examples/backend/e2-describing-a-target/mixed_args.ll.md"

LLVM IR names no registers, so the harness can only check that the file is
valid. The placement appears when `llc` stops right after instruction
selection. With LLVM 18.1.8 on the M4 Pro,
`llc -O2 -stop-after=finalize-isel mixed_args.ll -o -` printed this line
at the top of the function body:

```text
    liveins: $w0, $s0, $x1, $d1, $w2
```

`liveins` lists the physical registers holding values on entry, in argument
order here: `w0`, `s0`, `x1`, `d1`, `w2`, the interpreter's answer. The
assembly shows the same thing from the instructions' side:

```text
_mix:
	scvtf	d3, w0
	fcvt	d0, s0
	scvtf	d2, x1
	fadd	d0, d3, d0
	fadd	d0, d0, d2
	scvtf	d2, w2
	fadd	d0, d0, d1
	fadd	d0, d0, d2
	ret
```

`scvtf` converts a signed integer register to floating point and `fcvt`
widens the `float`. Each argument register is read once, and `d1`, the
`double`, goes straight into an `fadd`. The result leaves in `d0`, which is
the return rule's choice: `RetCC_AArch64_AAPCS` sends an `f64` result to the
first of `D0` to `D7`[^a64-cc].

Here is one to finish yourself. A function takes `(i64 %a, i32 %b, double
%c, float %d)`. Walk the four rules: `%a` takes the first free `X`
register; `%b` takes the first free `W` register, remembering which flag
`%a` set; the two floating-point arguments follow the same logic in the other
pool.

??? check "Where do the four arguments of `(i64, i32, double, float)` land?"

    `x0`, `w1`, `d0`, `s1`. `%a` takes `X0`, which also marks `W0`, so
    `%b` gets `W1`. `%c` takes `D0`, which marks `S0`, so `%d` gets `S1`.
    `llc -stop-after=finalize-isel` on such a function printed
    `liveins: $x0, $w1, $d0, $s1`.

The callee-saved registers are a record too. `CSR_AArch64_AAPCS` lists
`X19` to `X28`, the link register, the frame pointer and `D8` to
`D15`[^a64-cc]. The floating-point entries are the `D` registers, not the
`Q` registers, which states in register-description terms AAPCS64's rule
that only the bottom 64 bits of `v8` to `v15` must be preserved[^aapcs64].
The prologue and epilogue code reads this list, and nothing else in the back
end restates it.

## Scheduling models: one instruction, several machines

The last kind of fact belongs to a processor, not to the instruction set. A
multiply is the same `MADDXrrr` on every AArch64 core, but how many cycles
pass before its result can be used depends on the core. LLVM calls a
specific core a **subtarget**, and each subtarget can carry a **scheduling
model**: the numbers the machine scheduler ([C6](c6-scheduling.md)) and
`llvm-mca` use to predict timing.

A processor is declared with a `ProcessorModel` record: the name that
`-mcpu` accepts, a scheduling model and a list of features, the switches that
`-mattr` turns on and off[^target-td]. In LLVM 18.1.8's `AArch64.td`,
`cortex-a53` uses `CortexA53Model` and `neoverse-v2` uses `NeoverseV2Model`,
while `apple-m1`, `apple-m2`, `apple-m3` and `apple-latest` all use one
model, `CycloneModel`[^a64-td]. There is no `apple-m4` in that release
(`llc -mtriple=aarch64 -mcpu=help` lists none). For the owner's M4 Pro, any
timing LLVM 18 predicts comes from a model shared by several generations of
Apple cores, not one written for the M4.

A model starts with a `SchedMachineModel` record of whole-core
properties[^sched-td]:

| Field | Meaning | `CortexA53Model` | `CycloneModel` |
| --- | --- | --- | --- |
| `IssueWidth` | most micro-operations issued per cycle | 2 | 6 |
| `MicroOpBufferSize` | micro-operations buffered for out-of-order execution | 0 | 192 |
| `LoadLatency` | cycles for a load that hits the cache | 3 | 4 |
| `MispredictPenalty` | extra cycles for a mispredicted branch | 9 | 16 |

The values are copied from the two model files in LLVM
18.1.8[^a64-a53][^a64-cyclone]; they are the model's numbers, which are
estimates, not measurements of the chips. With a `MicroOpBufferSize` of 0 the
scheduler does not consider an instruction whose inputs are not ready in the
current cycle; a value above 1 means the core executes out of
order[^mc-sched].

### From instruction to latency

Instructions do not carry latencies. They carry **scheduling classes**, named
kinds of work such as "writes a 64-bit integer multiply result". The
header of `TargetSchedule.td` describes this main method: an instruction
inherits from `Sched` with a list of `SchedWrite` and `SchedRead` types, one
per operand, and each subtarget defines a `WriteRes` giving each `SchedWrite`
its processor resources and latency, plus a `ReadAdvance` when an operand can
be read late[^sched-td]. AArch64's 64-bit multiply-add is declared with
`WriteIM64` for its result and `ReadIM`, `ReadIM`, `ReadIMA` for its three
sources[^a64-formats]. The Cortex-A53 model maps `WriteIM64` to its
multiply-accumulate unit with a latency of 4[^a64-a53]; the Cyclone model
maps it to its multiply unit with a latency of 5[^a64-cyclone]. Figure 5
follows the chain.

<figure class="vx-figure">
<svg viewBox="0 0 760 240" role="img" aria-label="The instruction MADDXrrr names the scheduling class WriteIM64, and three processor models each give that class their own latency" aria-describedby="e2-sched-desc">
<title id="e2-sched-title">One instruction, one scheduling class, three latencies</title>
<desc id="e2-sched-desc">On the left, the instruction mul x2, x1, x1, which is MADDXrrr. An arrow leads to its scheduling class for the result, WriteIM64. From WriteIM64, three arrows lead to three processor models. CortexA53Model, for -mcpu=cortex-a53: latency 4 on unit A53UnitMAC. CycloneModel, for -mcpu=apple-m1: latency 5 on unit CyUnitIM. NeoverseV2Model, for -mcpu=neoverse-v2: llvm-mca reports latency 2. The instruction and the scheduling class are the same for all three; only the models differ.</desc>
<rect class="vx-box-strong" x="16" y="92" width="170" height="56" rx="4"/>
<text class="vx-mono" x="101" y="115" text-anchor="middle">mul x2, x1, x1</text>
<text class="vx-mono" x="101" y="135" text-anchor="middle" font-size="12">= MADDXrrr</text>
<line class="vx-line" x1="186" y1="120" x2="238" y2="120"/>
<polygon class="vx-arrowhead" points="236,114 246,120 236,126"/>
<rect class="vx-box-accent" x="246" y="100" width="140" height="40" rx="4"/>
<text class="vx-mono" x="316" y="125" text-anchor="middle">WriteIM64</text>
<text class="vx-text-muted" x="316" y="164" text-anchor="middle" font-size="12">shared by every core</text>
<path class="vx-flow" d="M386 120 L470 42"/>
<path class="vx-flow" d="M386 120 L470 120"/>
<path class="vx-flow" d="M386 120 L470 198"/>
<rect class="vx-box" x="470" y="16" width="274" height="52" rx="4"/>
<text class="vx-mono" x="484" y="37" font-size="12">CortexA53Model (cortex-a53)</text>
<text class="vx-text" x="484" y="58" font-size="12">latency 4, unit A53UnitMAC</text>
<rect class="vx-box" x="470" y="94" width="274" height="52" rx="4"/>
<text class="vx-mono" x="484" y="115" font-size="12">CycloneModel (apple-m1)</text>
<text class="vx-text" x="484" y="136" font-size="12">latency 5, unit CyUnitIM</text>
<rect class="vx-box" x="470" y="172" width="274" height="52" rx="4"/>
<text class="vx-mono" x="484" y="193" font-size="12">NeoverseV2Model (neoverse-v2)</text>
<text class="vx-text" x="484" y="214" font-size="12">llvm-mca reports latency 2</text>
</svg>
<figcaption>Figure 5. Two levels of indirection. The instruction names a scheduling class once, in the instruction description; each processor model gives the class a latency and a unit. Adding a core means writing a new model, and no instruction record changes. The A53 and Cyclone values are read from their model files; the Neoverse V2 value is what <code>llvm-mca</code> 18.1.8 reports, since that model may assign the instruction its latency by another route.</figcaption>
</figure>

The header lists other routes too: a subtarget can map specific opcodes to
its own classes, which overrides what the instruction said, or define variants
chosen by C++ predicates; and the older **itineraries**, per-pipeline-stage
reservation tables, are still supported[^sched-td]. The effect is the same in
each case: the scheduler asks the model and never names a core.

### Seeing the model with `llvm-mca`

`llvm-mca` is "a performance analysis tool that uses information available
in LLVM (e.g. scheduling models)" to estimate how machine code
performs[^mca]. Given one line, `mul x2, x1, x1`, and three `-mcpu` values,
the "Instruction Info" table of `llvm-mca` 18.1.8 gave these latencies:

| `-mcpu` | Model | Latency of `mul x2, x1, x1` |
| --- | --- | --- |
| `cortex-a53` | `CortexA53Model` | 4 |
| `apple-m1` | `CycloneModel` | 5 |
| `neoverse-v2` | `NeoverseV2Model` | 2 |

The same tool, the same instruction and three answers, because only the table
changed. The documentation is candid about what that means: the analysis is
"inevitably affected by the quality of the scheduling models"[^mca].
[E3](e3-llvm-allocator-scheduler-mc.md#watching-a-schedule-without-running-it-llvm-mca)
uses `llvm-mca` on whole blocks.

The example below keeps both levels of indirection in miniature. Four
instructions name four scheduling classes; two toy models give the classes
latencies (invented for the illustration); one function computes when each
result is ready. It places no limit on how many instructions start in a
cycle, so each one waits only for the one input it reads:

--8<-- "includes/examples/backend/e2-describing-a-target/sched_model_latency.cpp.md"

Trace model A by hand. The load starts at 0 and its result is ready at 4. The
multiply reads it, so it starts at 4 and is ready at 7. The add is ready at 8
and the store at 9. Model B changes one number, the multiply's latency from 3
to 1, and the block shrinks from 9 cycles to 7 without any change to the
scheduling function. That is how a real model is corrected: someone measures
a core, edits a `WriteRes`, and every client of the model changes behaviour.

??? check "In the example, suppose the store read the load's result instead of the add's. When would it start under model A, and what would a real scheduler gain from that?"

    At cycle 4, when the load's result is ready, instead of cycle 8. The
    block would then take 8 cycles, set by the add, instead of 9. Once the
    instructions no longer form one chain, there is a choice of order, and
    a latency table is what lets a scheduler choose well: it can place
    independent work in the shadow of the multiply. [C6](c6-scheduling.md)
    builds that scheduler.

How close is a model to the core you own? The honest answer needs a
measurement. One method: time a long chain of dependent multiplies, where
each reads the previous result, so the time per multiply approaches its
latency. Run it on the machine, divide by the number of multiplies and by
the cycle time, and compare with the models. Fill in your own row; this book
does not have a measured number for the M4 Pro:

| Machine | Measured cycles per dependent `mul` | `CycloneModel` says | Date and compiler |
| --- | --- | --- | --- |
| | | 5 | |

Reading cycles directly needs a cycle counter, which [P4](../optimize/p4-counters-and-tools.md)
covers; [P1](../optimize/p1-measure-first.md) covers timing a loop without
fooling yourself.

## Reading a real target description

With the four kinds of record in hand, a real target is navigable. Four
habits help.

1. **Start from a name you have seen.** An opcode in MIR (`MADDXrrr`), a
   register class (`gpr32`, which is `GPR32` in the description) or a
   convention (`CC_AArch64_DarwinPCS`) is a record name. Search the target's
   `.td` files for it.
2. **Expect names built by multiclasses.** `MADDXrrr` appears nowhere as
   written; `MulAccum` builds it from `defm MADD` and `def Xrrr`. Search for
   the suffix pattern, or for the class that sets the field you care about.
3. **Let `llvm-tblgen` flatten the records for you.** In a checkout of
   `llvm-project` at the tag `llvmorg-18.1.8`, matching the local `llc`, run
   the target's main file with `--print-records` and the include paths for
   `llvm/include` and the target directory. Every record then appears with
   every field resolved, the same view the toy examples above gave.
4. **Read the backend when a field puzzles you.** A field's meaning is
   whatever the generator does with it. The generators live in
   `llvm/utils/TableGen`; `CallingConvEmitter.cpp` is short enough to read in
   one sitting and shows each rule class turning into a line of
   C++[^cc-emitter].

Writing an LLVM Backend covers a whole target in this order, drawing most of
its examples from SPARC[^writing-backend], and the Cpu0 tutorial builds one step by step for a
small invented processor[^cpu0].

## For Vortex

!!! vortex "Exercise"

    **Build** one target description for your back end: a single file of
    data (C++ tables, or a text file your compiler reads) that holds every
    fact about AArch64 and x86-64 that your B1 and B2 templates use.
    [B2](b2-x86-64.md#for-vortex) asked you to write these facts down in
    your architecture notes; this exercise moves them into the compiler
    and makes them the only copy.

    1. **Registers.** Each register with its name, its width, the larger
       register it lives in (if any), what a write to it does to the rest
       of that register, and whether it is reserved on each platform.
       Include every view your templates emit: `w` and `x` on AArch64,
       and on x86-64 the 64-bit names with any 32-, 16- and 8-bit names
       you use.
    2. **Calls.** Per target: the argument registers per bank in order,
       the return registers, the callee-saved set, the stack alignment at
       a call, and the symbol prefix.
    3. **Readers.** Change the code that prints assembly, places arguments
       and saves registers so that it reads these tables. It must not
       spell a register name or list a register itself.

    **Not yet.** No TableGen and no generator of your own: plain tables
    are enough. No instruction descriptions or selection patterns
    ([C1](c1-instruction-selection.md)), no register classes for an
    allocator you have not written ([C3](c3-linear-scan.md)), no
    scheduling model ([C6](c6-scheduling.md)).

    **Proof that it works.**

    - A consistency test that reads only the description and checks
      rules you first state in comments, each with its source. Every
      narrower view names a containing register that exists, with no
      cycles. No argument or return register is callee-saved on the same
      target. `x18` is marked reserved on Apple, per
      [A4](a4-calling-conventions.md#apples-arm64-deviations), and no
      reserved register appears in any list your templates take scratch
      registers from. The callee-saved sets match AAPCS64 and the SysV
      AMD64 psABI.
    - A "one place" check in CI: a script that searches your back end's
      source files, other than the description, for register names as
      string literals (`"x0"`, `"w8"`, `"rdi"`, `"%rax"`, and so on)
      and fails if it finds one.
    - Your whole behavioral suite from B2 passes unchanged on all three
      runners.

    **Done when** those pass, and each of three deliberate, temporary
    breakages is caught:

    | Breakage | What must fail |
    | --- | --- |
    | Mark nothing reserved on Apple | the consistency test |
    | Swap the first two integer argument registers for x86-64 | a behavioral test on Linux x86-64 in which Vortex code calls a C or C++ function with two integer arguments |
    | Paste one register name back into an emitter | the "one place" check |

    The second row needs a call across the boundary. Calls between two
    Vortex functions still pass, because caller and callee read the same
    wrong table; if nothing fails, add a test that calls into C or C++
    and run the row again. A check that has never failed has not shown
    that it can.

## Key ideas

!!! recap "You can now answer"

    - **Why does LLVM describe targets in TableGen instead of C++ tables?** Each fact is written once as a record, and every table that needs it is generated from that record, so shared readers such as the allocator and the assembler cannot disagree about it.
    - **What gives a TableGen record its meaning?** The backend that reads it. Records are resolved field values; a generator selects records by class and decides what each field does.
    - **How does LLVM decide whether two registers overlap?** By register units, the pieces of storage built from the subregister structure: two registers overlap when they share a unit.
    - **What distinguishes a full redefinition from a partial change?** Whether the write covers every unit of the other register; if it covers only some, the old value partly survives.
    - **Why does `(i32, float, i64, double)` land in `w0`, `s0`, `x1`, `d1` on AArch64?** The rules take the first free register of each list, and taking `W0` or `S0` also marks its alias `X0` or `D0` as used.
    - **Where does a latency come from in LLVM?** From the processor's scheduling model, which gives a latency to each scheduling class that instructions name; the instruction itself carries none.
    - **Why can `llvm-mca` give three latencies for one `mul`?** Each `-mcpu` selects a different scheduling model, and the tool is only as accurate as the model.

## Where this comes back

!!! next "You will use this again in"

    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *register classes*, *scheduling models*, *`llvm-mca`*
    - [E4. Testing back ends](e4-testing-backends.md): *`-stop-after` and `liveins`*, *checking generated tables*
    - [C2. Liveness](c2-liveness.md): *partial writes*, *register units*
    - [C3. Register allocation I: linear scan](c3-linear-scan.md): *register classes*, *reserved registers*
    - [C6. Instruction scheduling](c6-scheduling.md): *latency tables*, *in-order and out-of-order models*
    - [P4. Seeing inside the CPU: counters and tools](../optimize/p4-counters-and-tools.md): *`llvm-mca`*, *checking a model against a measurement*
    - [G9. GPU compilers inside LLVM](../gpu/g9-gpu-compilers-in-llvm.md): *targets described in TableGen*

## Sources and further reading

Read the TableGen overview and the Programmer's Reference first, then one
real calling-convention file and one scheduling model beside `llvm-mca`
output. All LLVM source links are pinned to the 18.1.8 release, the version
on the owner's machine.

[^tg-index]: LLVM Project, "TableGen Overview": records, classes and definitions, and the statement that the files mean nothing without a backend. <https://llvm.org/docs/TableGen/index.html>
[^tg-progref]: LLVM Project, "TableGen Programmer's Reference", sections 1.1 (concepts), 1.6.1 (`class`), 1.6.4 (`let`) and 1.6.5 (`multiclass`). <https://llvm.org/docs/TableGen/ProgRef.html>
[^tg-backends]: LLVM Project, "TableGen BackEnds": the RegisterInfo, InstrInfo, AsmWriter, AsmMatcher, Disassembler, DAGISel, CallingConv and Subtarget backends. The option names in the table are from `llvm-tblgen --help`, version 18.1.8. <https://llvm.org/docs/TableGen/BackEnds.html>
[^tg-record]: LLVM Project, `llvm/include/llvm/TableGen/Record.h`, `RecordKeeper::getAllDerivedDefinitions`, release 18.1.8. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/include/llvm/TableGen/Record.h>
[^tg-selectiondag]: LLVM Project, `llvm/include/llvm/Target/TargetSelectionDAG.td`, classes `Pattern` and `Pat`, release 18.1.8. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/include/llvm/Target/TargetSelectionDAG.td>
[^writing-backend]: LLVM Project, "Writing an LLVM Backend": the `.td` files of a target, the generated `.inc` files, the calling-convention classes and the SPARC walkthrough. <https://llvm.org/docs/WritingAnLLVMBackend.html>
[^target-td]: LLVM Project, `llvm/include/llvm/Target/Target.td`, release 18.1.8: classes `SubRegIndex`, `Register`, `RegisterClass`, `Instruction` and `ProcessorModel`. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/include/llvm/Target/Target.td>
[^mc-reginfo-h]: LLVM Project, `llvm/include/llvm/MC/MCRegisterInfo.h`, `SubRegCoveredBits`, release 18.1.8. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/include/llvm/MC/MCRegisterInfo.h>
[^mc-reginfo]: LLVM Project, `llvm/lib/MC/MCRegisterInfo.cpp`, `MCRegisterInfo::regsOverlap`, release 18.1.8. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/lib/MC/MCRegisterInfo.cpp>
[^mc-instrdesc]: LLVM Project, `llvm/include/llvm/MC/MCInstrDesc.h`, the fields of `MCInstrDesc`, release 18.1.8. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/include/llvm/MC/MCInstrDesc.h>
[^mc-sched]: LLVM Project, `llvm/include/llvm/MC/MCSchedule.h`, the comment on `MicroOpBufferSize`, release 18.1.8. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/include/llvm/MC/MCSchedule.h>
[^ccstate-h]: LLVM Project, `llvm/include/llvm/CodeGen/CallingConvLower.h`, `CCState::AllocateReg` and `CCState::AllocateStack`, release 18.1.8. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/include/llvm/CodeGen/CallingConvLower.h>
[^ccstate]: LLVM Project, `llvm/lib/CodeGen/CallingConvLower.cpp`, `CCState::MarkAllocated`, release 18.1.8. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/lib/CodeGen/CallingConvLower.cpp>
[^cc-emitter]: LLVM Project, `llvm/utils/TableGen/CallingConvEmitter.cpp`, release 18.1.8. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/utils/TableGen/CallingConvEmitter.cpp>
[^sched-td]: LLVM Project, `llvm/include/llvm/Target/TargetSchedule.td`, header comment and classes `SchedMachineModel`, `WriteRes` and `ReadAdvance`, release 18.1.8. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/include/llvm/Target/TargetSchedule.td>
[^a64-td]: LLVM Project, `llvm/lib/Target/AArch64/AArch64.td`, the `include` lines and the `ProcessorModel` records, release 18.1.8. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/lib/Target/AArch64/AArch64.td>
[^target-cc]: LLVM Project, `llvm/include/llvm/Target/TargetCallingConv.td`, classes `CCIfType`, `CCAssignToReg`, `CCAssignToStack`, `CCPromoteToType` and `CCDelegateTo`, release 18.1.8. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/include/llvm/Target/TargetCallingConv.td>
[^a64-isel]: LLVM Project, `llvm/lib/Target/AArch64/AArch64ISelLowering.cpp`, `AArch64TargetLowering::CCAssignFnForCall`, release 18.1.8. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/lib/Target/AArch64/AArch64ISelLowering.cpp>
[^a64-regs]: LLVM Project, `llvm/lib/Target/AArch64/AArch64RegisterInfo.td`, release 18.1.8. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/lib/Target/AArch64/AArch64RegisterInfo.td>
[^a64-cc]: LLVM Project, `llvm/lib/Target/AArch64/AArch64CallingConvention.td`: `CC_AArch64_AAPCS`, `CC_AArch64_DarwinPCS`, `RetCC_AArch64_AAPCS` and `CSR_AArch64_AAPCS`, release 18.1.8. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/lib/Target/AArch64/AArch64CallingConvention.td>
[^a64-formats]: LLVM Project, `llvm/lib/Target/AArch64/AArch64InstrFormats.td`, multiclasses `MulAccum` (with its `Sched` lists) and `AddSubS` (with `Defs = [NZCV]`), release 18.1.8. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/lib/Target/AArch64/AArch64InstrFormats.td>
[^a64-instrinfo]: LLVM Project, `llvm/lib/Target/AArch64/AArch64InstrInfo.td`, `defm MADD` and `defm SUBS`, release 18.1.8. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/lib/Target/AArch64/AArch64InstrInfo.td>
[^a64-a53]: LLVM Project, `llvm/lib/Target/AArch64/AArch64SchedA53.td`, `CortexA53Model` and the `WriteIM64` resource, release 18.1.8. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/lib/Target/AArch64/AArch64SchedA53.td>
[^a64-cyclone]: LLVM Project, `llvm/lib/Target/AArch64/AArch64SchedCyclone.td`, `CycloneModel` and the `WriteIM64` resource, release 18.1.8. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/lib/Target/AArch64/AArch64SchedCyclone.td>
[^mca]: LLVM Project, "llvm-mca - LLVM Machine Code Analyzer". <https://llvm.org/docs/CommandGuide/llvm-mca.html>
[^aapcs64]: Arm, "Procedure Call Standard for the Arm 64-bit Architecture (AArch64)", release 2025Q4, section on SIMD and floating-point registers: only the bottom 64 bits of `v8` to `v15` are callee-saved. <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
[^cpu0]: Chen Chung-Shu, "Tutorial: Creating an LLVM Backend for the Cpu0 Architecture". <https://jonathan2251.github.io/lbd/>
