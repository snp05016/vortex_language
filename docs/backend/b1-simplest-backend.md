# B1. The simplest back end that works

<p class="page-intro">A back end that never chooses: one fixed block of instructions per operation, one stack slot per value, and the system assembler and linker to finish the job. Its code is slow on purpose. In return it is small enough to get right quickly, and it gives Vortex a second, independent path to test every later back end against.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 40 minutes · Builds on: [A1. The machine model](a1-machine-model.md), [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md), [A3. Floats and vectors in registers](a3-floats-and-vectors.md), [A4. Calling conventions and ABIs](a4-calling-conventions.md), [A5. Stack frames](a5-stack-frames.md)</p>

???+ remember "Before you start, remember"

    ??? question "Can an AArch64 `add` take one of its operands straight from memory?"

        No. AArch64 is a load/store machine: arithmetic reads and writes
        registers only, and memory is reached only through loads and stores.
        A value kept in memory must be loaded before anything can be done
        with it.

        Introduced in [A1. The machine model](a1-machine-model.md#loadstore-machines-and-register-memory-machines).

    ??? question "What does `.globl` do to a label, and what happens without it?"

        It makes the name visible to the linker, so other files can call the
        function. Without it, the label is private to the file that defines
        it.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md#the-smallest-complete-file).

    ??? question "What does `bl` do to x30, and what must a function that calls another do about it?"

        `bl` writes the return address into x30, overwriting the caller's
        own return address. A function that calls must save x30 first,
        normally together with x29 as a frame record in its prologue.

        Introduced in [A5. Stack frames](a5-stack-frames.md#the-frame-record-and-the-chain).

    ??? question "What must sp always be a multiple of on AArch64?"

        16 bytes. A frame's size is rounded up to a multiple of 16, even when
        the data in it needs less.

        Introduced in [A5. Stack frames](a5-stack-frames.md#frame-layout-what-goes-where).

    ??? question "How many times does `fmadd` round, and how many times do `fmul` followed by `fadd` round?"

        `fmadd` rounds once, after the multiply and the add. The two
        separate instructions round twice, once each, so the last bit of the
        result can differ.

        Introduced in [A3. Floats and vectors in registers](a3-floats-and-vectors.md#fused-multiply-add).

!!! goals "In this chapter"

    - Explain what macro expansion is, what it buys and what it gives up, and where it sits among the instruction-selection techniques later chapters teach.
    - Trace an expression, a loop and a call through a macro-expansion code generator by hand, with one stack slot per value.
    - Assemble and link generated AArch64 text with the system toolchain, and wrap the templates in a correct prologue and epilogue.
    - Recognize why a floating-point template must never fuse a multiply and an add, and why this scheme gets that right without trying.
    - Set up a differential test that compares a native back end against a second, independent path, and say what its results do and do not prove.

## One template per operation

[Stage 6](../compiler/guide/stage-6-first-machine-code.md#choosing-a-back-end)
asked for a compiler that turns `2 + 3 * 4` into a running program that
prints `14`, and left the back end open: generate LLVM IR, generate C, or
generate assembly directly. This chapter takes the third road on AArch64, the
target [A1](a1-machine-model.md) to [A5](a5-stack-frames.md) cover, with as
little cleverness as possible.

A back end normally makes three kinds of decision: which instructions to use,
which register holds each value, and in what order the instructions run. The
usual pipeline separates the first two. Pfenning's lecture notes describe
**instruction selection** that emits instructions over **temps**, an
unlimited supply of pretend registers, followed by **register allocation**,
which assigns each temp a machine register or a stack slot[^cmu-isel]. The
back end in this chapter makes the simplest possible choice at each step. It
selects instructions by a fixed table, it gives every temp its own stack slot,
and it keeps the order the IR already has.

Take a four-node expression tree for `(7 - 2) * 3`. A **code generator** turns
a checked, typed tree or IR into instructions. The simplest kind, **macro
expansion**, gives every kind of node one fixed block of instructions, a
**template**, chosen by the node's own kind and nothing else: not what its
operands are, not what happens to its result, not what instruction came
before it.

Hjort Blindell's survey of instruction selection starts its history here. In
the first designs, from the late 1960s, selection was "driven by matching
templates over the code that constitute the input program", and a match ran
the corresponding macro to print assembly[^hjort]. The survey makes two
points that matter for this chapter. When several templates could match, these
selectors took the first one, so there was no choice to optimize. And keeping
the templates apart from the code that walks the program made the compiler
easier to move to a new machine: replace the table, keep the walker[^hjort].

Four node kinds need four templates: a constant, and the three binary
operators. Each template writes its result to a fresh **stack slot**, a
fixed place in the function's stack frame, and assumes nothing about its
operands except which slots hold them:

```text
Const(v)  -> slot s:      mov w0, #v
                          str w0, [sp, #4*s]

Add(l, r) -> slot s:      ldr w0, [sp, #4*ls]
                          ldr w1, [sp, #4*rs]
                          add w0, w0, w1
                          str w0, [sp, #4*s]
```

`Sub` and `Mul` repeat `Add`'s template with `sub` or `mul` in place of
`add`. Here `ls` and `rs` are the slots of the left and right operands, and
each slot is 4 bytes because every value here is an `i32`. The generator walks
the tree in **postorder**, children before parents, left child before right,
and each node returns the slot its result landed in. The example below is that
walk:

--8<-- "includes/examples/backend/b1-simplest-backend/macro_expansion.cpp.md"

Figure 1 draws the same walk: the tree on the left, and the stack frame on
the right filling one slot at a time, in the order the code above visits
the nodes.

<figure class="vx-figure">
<svg viewBox="0 0 760 340" role="img" aria-label="Macro expansion for (7 minus 2) times 3, filling a stack frame one slot at a time" aria-describedby="b1-f1-desc">
<title id="b1-f1-title">Macro expansion for (7 - 2) * 3</title>
<desc id="b1-f1-desc">Left: an expression tree. Mul is the root, with children Sub and the constant 3. Sub has children the constants 7 and 2. Right: two small boxes labelled w0 and w1, captioned "reloaded and overwritten by every template", above five stack slots that fill from top to bottom in the order the tree is walked: slot 0 holds 7, slot 1 holds 2, slot 2 holds the Sub result 5, slot 3 holds 3, and slot 4, marked as the final result, holds the Mul result 15. Each slot fades in after the previous one, in that order.</desc>
<defs><marker id="b1-f1-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="20">Expression tree</text>
<text class="vx-text" x="460" y="20">Stack frame, filled in postorder</text>
<rect class="vx-box-strong" x="150" y="36" width="90" height="36" rx="4"/>
<text class="vx-text" x="195" y="59" text-anchor="middle">Mul</text>
<rect class="vx-box" x="40" y="126" width="90" height="36" rx="4"/>
<text class="vx-text" x="85" y="149" text-anchor="middle">Sub</text>
<rect class="vx-box" x="270" y="126" width="70" height="36" rx="4"/>
<text class="vx-text" x="305" y="149" text-anchor="middle">3</text>
<rect class="vx-box" x="10" y="216" width="70" height="36" rx="4"/>
<text class="vx-text" x="45" y="239" text-anchor="middle">7</text>
<rect class="vx-box" x="110" y="216" width="70" height="36" rx="4"/>
<text class="vx-text" x="145" y="239" text-anchor="middle">2</text>
<line class="vx-line" x1="195" y1="72" x2="95" y2="126" marker-end="url(#b1-f1-head)"/>
<line class="vx-line" x1="195" y1="72" x2="305" y2="126" marker-end="url(#b1-f1-head)"/>
<line class="vx-line" x1="85" y1="162" x2="45" y2="216" marker-end="url(#b1-f1-head)"/>
<line class="vx-line" x1="85" y1="162" x2="145" y2="216" marker-end="url(#b1-f1-head)"/>
<rect class="vx-box" x="460" y="36" width="60" height="30" rx="4"/>
<text class="vx-text" x="490" y="56" text-anchor="middle">w0</text>
<rect class="vx-box" x="540" y="36" width="60" height="30" rx="4"/>
<text class="vx-text" x="570" y="56" text-anchor="middle">w1</text>
<text class="vx-text-muted" x="460" y="86">reloaded and overwritten by every template</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5">
<rect class="vx-box" x="460" y="106" width="260" height="32" rx="4"/>
<text class="vx-mono" x="472" y="127">slot 0  [sp, #0]   = 7</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5">
<rect class="vx-box" x="460" y="148" width="260" height="32" rx="4"/>
<text class="vx-mono" x="472" y="169">slot 1  [sp, #4]   = 2</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5">
<rect class="vx-box" x="460" y="190" width="260" height="32" rx="4"/>
<text class="vx-mono" x="472" y="211">slot 2  [sp, #8]   = 5 (Sub)</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5">
<rect class="vx-box" x="460" y="232" width="260" height="32" rx="4"/>
<text class="vx-mono" x="472" y="253">slot 3  [sp, #12]  = 3</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 5">
<rect class="vx-box-accent" x="460" y="274" width="260" height="32" rx="4"/>
<text class="vx-mono" x="472" y="295">slot 4  [sp, #16]  = 15 (Mul, final)</text>
</g>
</svg>
<figcaption>Figure 1. Macro expansion for (7 - 2) * 3. Each node's template runs once, in postorder, and writes its result to the next free stack slot (right). <code>w0</code> and <code>w1</code>, top right, are the only registers any template touches, reloaded and overwritten every time.</figcaption>
</figure>

Every template reads at most two values, both freshly loaded, and writes at
most one, stored at once. A value lives in a register only for the few
instructions inside one template, so no two values ever need a register at
the same time. That is what "every value in a stack slot" buys: there is
nothing to allocate. The price is a `str` after every result and an `ldr`
before every use, memory traffic that a better back end avoids by leaving the
value in a register. [C2](c2-liveness.md) and [C3](c3-linear-scan.md) exist
to win that traffic back.

Ghuloum's incremental compiler for a subset of Scheme uses the same idea on
x86. Its result register is `%eax`; for a binary primitive it saves one
operand's value on the stack, at a **stack index** that moves 4 bytes further
for every saved value, and combines the other operand with the saved one
later[^ghuloum]. Local variables go on the stack too, with an environment
that maps each variable to its stack location[^ghuloum].

One detail differs from this chapter. For `(+ e0 e1)` Ghuloum emits the code for `e1` first, then
`e0`[^ghuloum]. Vortex fixes the order: operands are evaluated left to right
([decision 38](../decisions/statements.md#d38)). With calls that print, or
with two runtime checks that could fail, the order is visible, so a template
borrowed from a right-to-left design needs changing.

chibicc, a small C compiler that emits x86-64 assembly text, shows the idea
in a real code base. Its code generator is a stack machine: it computes one
operand into `%rax`, pushes it, computes the other and pops the saved value
into a second register[^chibicc-codegen]. Its README states that there is no
optimization pass and that the code is probably at least twice as slow as
GCC's output[^chibicc]. [D3](d3-real-backends.md) reads it in more detail.

??? check "Why does the `Sub` template load the left operand's slot into `w0` and the right one's into `w1`? What goes wrong if a `Sub` template is copied from `Add` with the two loads swapped?"

    `sub w0, w0, w1` computes `w0 - w1`, so `w0` must hold the left
    operand. `Add` gives the same answer with the loads either way round,
    because addition is commutative, so a copied template that swapped them
    passes every test of `+`. The same swap in `Sub` makes every subtraction
    compute `right - left`: `7 - 2` gives `-5`. The evaluation order stays
    correct, because the operands are still *computed* left first; only the
    registers they are loaded into are swapped. This is the planted bug in
    the differential test at the end of this chapter.

### Complete the trace

Half of a trace for `2 * (3 + 4) - 1` is filled in. Complete the rest, using
the same postorder walk and the same rule for offsets as the worked example.

| Step | Node | Slot | Instructions | Value |
| --- | --- | --- | --- | --- |
| 1 | `Const 2` | 0 | `mov w0, #2` / `str w0, [sp, #0]` | 2 |
| 2 | `Const 3` | 1 | `mov w0, #3` / `str w0, [sp, #4]` | 3 |
| 3 | `Const 4` | 2 | `mov w0, #4` / `str w0, [sp, #8]` | 4 |
| 4 | `Add(slot 1, slot 2)` | 3 | ? | ? |
| 5 | `Mul(slot 0, slot 3)` | 4 | ? | ? |
| 6 | `Const 1` | 5 | ? | ? |
| 7 | `Sub(slot 4, slot 5)` | 6 | ? | ? |

??? note "The completed trace"

    - Step 4: `ldr w0, [sp, #4]` / `ldr w1, [sp, #8]` / `add w0, w0, w1` / `str w0, [sp, #12]`, value 7.
    - Step 5: `ldr w0, [sp, #0]` / `ldr w1, [sp, #12]` / `mul w0, w0, w1` / `str w0, [sp, #16]`, value 14.
    - Step 6: `mov w0, #1` / `str w0, [sp, #20]`, value 1.
    - Step 7: `ldr w0, [sp, #16]` / `ldr w1, [sp, #20]` / `sub w0, w0, w1` / `str w0, [sp, #24]`, value 13.

    Seven values, seven slots, 28 bytes, so a 32-byte frame. The slot count
    is known as soon as the tree is, because the generator never reuses a
    slot. Vortex's fixed shapes give the same property to a whole function:
    every array dimension is a compile-time constant
    ([decision 11](../decisions/arrays.md#d11)), so the size of every local,
    and of the frame, is known before the first instruction is emitted.

## Statements, variables and loops

Expressions are only part of a program. Three more kinds of template turn
this into a back end for a language with variables and loops, and each keeps
the rule that nothing stays in a register between templates.

A **variable** gets a slot of its own for the whole function, like Ghuloum's
environment of stack locations[^ghuloum]. `let s = 0` expands the constant
into its slot, then copies it: a load from the value's slot and a store to
the variable's slot. A read of `s` is a load from its slot, and an assignment
is a store to it. The copy is wasteful, one `ldr` and one `str` that a better
selector would drop, and it is also uniform: every expression result, whatever
produced it, is copied the same way.

A **comparison** such as `i <= n` becomes a value like any other: 1 if it
holds and 0 if not. Its template loads both operands, compares them with
`cmp`, turns the flags into 0 or 1 with `cset` and stores the result. The
condition code in the `cset` depends on the operand type:
[A2](a2-aarch64-assembly.md#flags-and-conditions) showed that `le` is the
signed test and `ls` the unsigned one, so a `usize` comparison and an `i32`
comparison differ in that one word of the template.

A **branch** needs a label to jump to, and a template that runs twice must
not print the same label twice. Ghuloum's conditional takes two fresh names
from a counter each time it runs, one for the else branch and one for the
join, and emits a compare and a jump around the code for each branch[^ghuloum].
A `while` loop needs the same: a label before the condition, the condition's
template, a branch out when its slot holds 0, the body, and a branch back.
Figure 2 draws that shape.

<figure class="vx-figure">
<svg viewBox="0 0 760 380" role="img" aria-label="The while-loop template: a top label, the condition template, a branch out when the condition is zero, the body, and a branch back to the top" aria-describedby="b1-f2-desc">
<title id="b1-f2-title">The while-loop template</title>
<desc id="b1-f2-desc">Five boxes stacked from top to bottom. First, the label Lwhile_top0. Second, the condition template, which computes i less than or equal to n into slot 5 with ldr, ldr, cmp, cset le and str. Third, the exit test: ldr w0 from slot 5, cmp w0 with 0, b.eq Lwhile_end0. Fourth, the body templates, which update s and i through their slots. Fifth, b Lwhile_top0. An arrow from the fifth box returns up the left side to the first box, labelled "back edge". An arrow from the third box leaves on the right side and goes down to a sixth box, the label Lwhile_end0, labelled "condition was 0". A note says the number 0 in both labels comes from a counter, so the next loop gets Lwhile_top1 and Lwhile_end1.</desc>
<defs><marker id="b1-f2-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box-strong" x="180" y="16" width="300" height="34" rx="4"/>
<text class="vx-mono" x="330" y="38" text-anchor="middle">Lwhile_top0:</text>
<rect class="vx-box" x="180" y="66" width="300" height="52" rx="4"/>
<text class="vx-text" x="330" y="86" text-anchor="middle">condition template: i &lt;= n</text>
<text class="vx-mono" x="330" y="106" text-anchor="middle">ldr, ldr, cmp, cset le, str slot 5</text>
<rect class="vx-box-accent" x="180" y="134" width="300" height="52" rx="4"/>
<text class="vx-mono" x="330" y="154" text-anchor="middle">ldr w0, [slot 5]; cmp w0, #0</text>
<text class="vx-mono" x="330" y="174" text-anchor="middle">b.eq Lwhile_end0</text>
<rect class="vx-box" x="180" y="202" width="300" height="52" rx="4"/>
<text class="vx-text" x="330" y="222" text-anchor="middle">body templates</text>
<text class="vx-text-muted" x="330" y="242" text-anchor="middle">s = s + i; i = i + 1, all through slots</text>
<rect class="vx-box" x="180" y="270" width="300" height="34" rx="4"/>
<text class="vx-mono" x="330" y="292" text-anchor="middle">b Lwhile_top0</text>
<rect class="vx-box-strong" x="180" y="330" width="300" height="34" rx="4"/>
<text class="vx-mono" x="330" y="352" text-anchor="middle">Lwhile_end0:</text>
<line class="vx-line" x1="330" y1="50" x2="330" y2="66" marker-end="url(#b1-f2-head)"/>
<line class="vx-line" x1="330" y1="118" x2="330" y2="134" marker-end="url(#b1-f2-head)"/>
<line class="vx-line" x1="330" y1="186" x2="330" y2="202" marker-end="url(#b1-f2-head)"/>
<line class="vx-line" x1="330" y1="254" x2="330" y2="270" marker-end="url(#b1-f2-head)"/>
<path class="vx-line vx-flow" d="M180 287 L130 287 L130 33 L180 33" marker-end="url(#b1-f2-head)"/>
<text class="vx-text-muted" x="20" y="164">back edge</text>
<path class="vx-line vx-flow" d="M480 160 L540 160 L540 347 L480 347" marker-end="url(#b1-f2-head)"/>
<text class="vx-text-muted" x="552" y="256">condition was 0</text>
<text class="vx-text-muted" x="552" y="40">the 0 in both labels</text>
<text class="vx-text-muted" x="552" y="58">comes from a counter:</text>
<text class="vx-text-muted" x="552" y="76">the next loop gets</text>
<text class="vx-mono" x="552" y="96">Lwhile_top1</text>
</svg>
<figcaption>Figure 2. The <code>while</code> template. The condition is an ordinary expression template that leaves 0 or 1 in a slot; the loop adds only two labels, a conditional branch out and an unconditional branch back. Nothing is in a register when control reaches either label, so the code on both sides of a branch agrees about where every value is without any bookkeeping.</figcaption>
</figure>

The last sentence of that caption is why this scheme is hard to get
wrong. When a later back end keeps values in registers, every label is a
point where two paths join, and both must agree on which register holds
which value. [C2](c2-liveness.md) is about computing exactly that. Here the
answer is the same at every label: every value is in its slot.

### A loop, expanded by hand

The file below is the toy function
`sum_to(n) { let s = 0; let i = 1; while i <= n { s = s + i; i = i + 1; } return s; }`,
expanded template by template. Read it with the slot table in its header
comment:

--8<-- "includes/examples/backend/b1-simplest-backend/sum_to.s.md"

Trace the first pass through the loop with `n = 2`. The prologue stores 2 in
slot 0. The two `let` templates leave `s = 0` in slot 1 and `i = 1` in slot
2. At `Lwhile_top0`, `cmp w0, w1` compares 1 with 2, `cset w0, le` writes 1,
and the `b.eq` falls through. The body stores `s + i = 1` in slot 6 and
copies it to slot 1, then stores `i + 1 = 2` in slot 8 and copies it to slot
2.

The second pass adds 2, so `s` is 3 and `i` is 3. On the third test `cset`
writes 0, `b.eq` jumps to `Lwhile_end0`, and the epilogue returns slot 1. The
harness assembles this file with `cc -c`; linked with a small C caller, it
returned 55 for `sum_to(10)` and 0 for `sum_to(0)` (Apple clang 21, macOS 27
on an Apple M4 Pro, 24 September 2026).

Count what the loop costs. Each pass runs 23 instructions from
`Lwhile_top0` to the `b`, and 15 of them are loads and stores. Counting
cycles would need measurement ([P1](../optimize/p1-measure-first.md)), but
the count alone says where the later chapters will find their gains.

A Vortex program needs two more things inside its templates, and both reuse
earlier chapters. First, `+`, `-` and `*` on integers are checked: a result
that does not fit stops the program with a runtime error
([decision 34](../decisions/diagnostics.md#d34)). The template for a checked
`add` uses the flag-setting form and branches to the error path, the pattern
[A2 left half finished](a2-aarch64-assembly.md#checked-arithmetic-half-finished).
Second, an array element read or write expands into the bounds check of
[stage 9](../compiler/guide/stage-9-runtime-safety.md#array-bounds), the
address arithmetic of
[A2](a2-aarch64-assembly.md#one-element-of-a-fixed-shape-array), and one load
or store. A template may be long; it only has to be fixed.

## Calls and the frame around the templates

Templates do not build frames. A function also needs a **prologue** that
claims its frame and an **epilogue** that gives it back, as
[A5](a5-stack-frames.md) built by hand. This back end needs the simplest
version of each, because the only thing in its frame is slots.

The frame size comes straight from the slot count. `(7 - 2) * 3` used five
4-byte slots, 20 bytes; AAPCS64 requires sp to stay a multiple of 16[^aapcs64],
and Apple's platforms follow the same rule[^apple-arm64], so the frame is 32
bytes. Slot sizes follow the type: 4 bytes for `i32` and `f32`, 8 for `f64`,
`usize` and addresses, and a whole fixed-shape array is one object of its
full size, placed with the alignment rules of
[A5](a5-stack-frames.md#frame-layout-what-goes-where).

Arguments and return values are the only values that arrive or leave in
registers, following [A4](a4-calling-conventions.md#where-arguments-go-two-counters-not-one).
The prologue stores each incoming argument to its own slot at once, as
`sum_to` does with `w0`, so every later template loads it like anything else.
A call reverses this: load each argument from its slot into `x0` to `x7` (or
`s0` and `d0` onwards for floats), `bl`, and store the result register into a
fresh slot. A `&mut` parameter is one address in an integer register, and a
write through it is a store to that address.

The call template needs no **caller-saved** register handling at all. A4
split the registers into those a call may overwrite and those it must
preserve[^aapcs64]. A register allocator must save every caller-saved
register that holds a value still needed after the call. In this scheme no
register holds anything between templates, so at a `bl` there is nothing to
save. The only register at risk is x30, which the `bl` itself overwrites.
That is why a function that calls anything saves x29 and x30 as a frame record
in its prologue. Apple requires x29 to address a valid frame record, and
allows leaf functions to skip creating one[^apple-arm64]. Figure 3 shows the
frame of a function that calls.

<figure class="vx-figure">
<svg viewBox="0 0 760 380" role="img" aria-label="The frame of a calling function in the stack-slot scheme: a frame record at the top, slots below it, padding, and sp at the bottom; beside it the prologue, a call template and the epilogue" aria-describedby="b1-f3-desc">
<title id="b1-f3-title">A calling function's frame</title>
<desc id="b1-f3-desc">Left: a column of stack memory, higher addresses at the top. At the top is the caller's frame. Below it, the frame record: saved x29 and saved x30, 16 bytes, with x29 pointing at it. Below that, slot 0 holding argument a, slot 1 holding argument b, slots 2 to k holding intermediate values and the result of the call, then padding up to a multiple of 16, and sp at the bottom. Right: three blocks of code. The prologue: stp x29, x30, [sp, #-16]!; mov x29, sp; sub sp, sp, #N; then str w0 and str w1 into slots 0 and 1. The call template: ldr w0 from an argument slot, bl helper, str w0 to a fresh slot, with the note "no register holds a value here, so nothing to save". The epilogue: ldr w0 from the result slot, add sp, sp, #N, ldp x29, x30, [sp], #16, ret.</desc>
<text class="vx-text" x="20" y="22">Stack, higher addresses up</text>
<rect class="vx-box" x="20" y="36" width="220" height="34" rx="4"/>
<text class="vx-text-muted" x="130" y="58" text-anchor="middle">caller's frame</text>
<rect class="vx-box-strong" x="20" y="78" width="220" height="44" rx="4"/>
<text class="vx-mono" x="130" y="98" text-anchor="middle">saved x30</text>
<text class="vx-mono" x="130" y="115" text-anchor="middle">saved x29</text>
<text class="vx-mono" x="248" y="120">&lt;- x29</text>
<rect class="vx-box" x="20" y="130" width="220" height="30" rx="4"/>
<text class="vx-mono" x="130" y="150" text-anchor="middle">slot 0: a</text>
<rect class="vx-box" x="20" y="166" width="220" height="30" rx="4"/>
<text class="vx-mono" x="130" y="186" text-anchor="middle">slot 1: b</text>
<rect class="vx-box" x="20" y="202" width="220" height="62" rx="4"/>
<text class="vx-mono" x="130" y="228" text-anchor="middle">slots 2 .. k</text>
<text class="vx-text-muted" x="130" y="248" text-anchor="middle">temps, call results</text>
<rect class="vx-box" x="20" y="270" width="220" height="30" rx="4"/>
<text class="vx-text-muted" x="130" y="290" text-anchor="middle">padding to a multiple of 16</text>
<text class="vx-mono" x="248" y="305">&lt;- sp</text>
<text class="vx-text-muted" x="20" y="330">N = slots + padding, known before</text>
<text class="vx-text-muted" x="20" y="348">the first instruction is emitted</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 3">
<rect class="vx-box" x="330" y="36" width="410" height="96" rx="4"/>
<text class="vx-text" x="344" y="56">prologue</text>
<text class="vx-mono" x="344" y="78">stp x29, x30, [sp, #-16]!; mov x29, sp</text>
<text class="vx-mono" x="344" y="98">sub sp, sp, #N</text>
<text class="vx-mono" x="344" y="118">str w0, [slot 0]; str w1, [slot 1]</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 3">
<rect class="vx-box-accent" x="330" y="146" width="410" height="96" rx="4"/>
<text class="vx-text" x="344" y="166">call template</text>
<text class="vx-mono" x="344" y="188">ldr w0, [slot a]; bl _helper</text>
<text class="vx-mono" x="344" y="208">str w0, [fresh slot]</text>
<text class="vx-text-muted" x="344" y="230">no register holds a value here: nothing to save</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 3">
<rect class="vx-box" x="330" y="256" width="410" height="96" rx="4"/>
<text class="vx-text" x="344" y="276">epilogue</text>
<text class="vx-mono" x="344" y="298">ldr w0, [result slot]; add sp, sp, #N</text>
<text class="vx-mono" x="344" y="318">ldp x29, x30, [sp], #16</text>
<text class="vx-mono" x="344" y="338">ret</text>
</g>
</svg>
<figcaption>Figure 3. The frame of a function that calls, in the stack-slot scheme. The frame record at the top protects the one register a <code>bl</code> destroys. Everything else lives in slots, so the call template saves nothing, which is the part a register allocator later has to work for.</figcaption>
</figure>

Two limits of the instruction set reach into the templates, and both show up
first in programs with big numbers or big frames. `mov w0, #v` cannot hold
every 32-bit constant: `llvm-mc` 18.1.8 accepted `#65535` and `#65536` but
rejected `#70000`, which takes a `mov` of the low 16 bits and a
`movk w0, #1, lsl #16` for the high ones. And `ldr w0, [sp, #off]` has a
limited offset: it accepted `#16380` and rejected `#16384`, and the 8-byte
`ldr x0` stopped at `#32760` (all checked on 24 September 2026).

A function with a `[f32; 64, 64]` local already has a 16 KiB frame, so the
slot template must fall back to computing the address in a register when the offset does
not fit. [A5](a5-stack-frames.md#large-frames-immediates-probes-and-the-guard-page)
covers claiming such a frame safely.

??? check "A function in this scheme computes `x`, calls `helper()`, then uses `x`. Which registers must it save around the `bl`, and why is the answer different for a back end with a register allocator?"

    None around the call itself. `x` was stored to its slot the moment its
    template finished, and the use after the call loads it back from there;
    the call cannot touch the slot. The only register the call destroys that
    matters is x30, and it was saved once, in the prologue, as part of the
    frame record. A register allocator that kept `x` in a caller-saved
    register such as `w9` would have to save and restore it around the call,
    or move `x` to a callee-saved register, which in turn must be saved in
    the prologue.

## Text out, and the toolchain finishes the job

The templates are strings. This back end's whole output is **assembly
text**, printed line by line, and it leans on the same two programs a C
compiler leans on: an **assembler**, which turns the text into an object
file, and a **linker**, which joins object files and libraries into a program
the operating system can run. `cc -c file.s` runs only the assembler; `cc`
without `-c` runs both, and `cc out.s runtime.c -o program` builds a whole
program in one command.

Here is the `(7 - 2) * 3` output from the first example, with the prologue
and epilogue it was missing:

--8<-- "includes/examples/backend/b1-simplest-backend/stack_calc.s.md"

The body is the expander's output line for line. The prologue claims 32
bytes, the epilogue loads slot 4 into `w0`, where the call standard expects
an `int` result, and gives the frame back. Linked into a small C caller, it
returned 15 (Apple clang 21, macOS 27 on an Apple M4 Pro, 24 September 2026).
The name `_stack_calc` has the leading underscore Mach-O adds to C names, and
the `L` labels in `sum_to.s` are the Mach-O spelling of what ELF writes
`.Lwhile_top0`; [A2](a2-aarch64-assembly.md#branches-and-loops) covers both.

So a code generator that prints these lines to a file and then runs `cc` has
a complete path from tree to running program. It depends on the same
toolchain as a v0.1 back end that
["generates C"](../compiler/guide/stage-6-first-machine-code.md#generating-c).
The templates never needed to know how the assembler encodes
`sub w0, w0, w1` into 32 bits; that belongs to [B3](b3-object-files.md), for
the day a compiler wants to write object files itself.

Printing text has a second benefit: the output is readable. When a test
fails, the `.s` file is the first thing to open, and each block in it can be
traced back to the IR operation that printed it. A back end that writes
bytes directly loses that until it grows a disassembler.

## What a floating-point template must never do

Vortex's kernels are floating-point kernels, and the rule that matters most
for them is not about registers. [Decision 56](../decisions/numbers.md#d56)
fixes every `f32` and `f64` operation to its exact IEEE 754 result, rounded to
nearest with ties to even: no contraction, no reordering, no wider format.

The trap is concrete. `sum += a[row, k] * b[k, column]`, the inner line of the
[stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for)
kernel, is two operations: a multiply, then an add. Under this chapter's
scheme it becomes one `fmul` template and one `fadd` template, two rounded
results. `fmadd` computes the same expression in one instruction but rounds
once ([A3](a3-floats-and-vectors.md#fused-multiply-add)), so the last bit can
differ.

```vortex
// fragment
sum += a[row, k] * b[k, column];
```

C compilers are allowed to make that substitution. Clang's manual says its
default, `-ffp-contract=on`, permits fusion within one statement, and that
`off` disables it[^clang-um]. A macro-expansion back end never faces the
question, because it never looks at two nodes together: the multiply's
template and the add's template are chosen and emitted independently. It
meets decision 56 by construction.

The risk arrives later, when a smarter
selector covers several nodes with one instruction ([C1](c1-instruction-selection.md))
or a peephole pass merges neighbours ([C7](c7-peephole.md)): each needs an
explicit rule that forbids the fused form for Vortex code. The same holds
for the reference path. If it generates C, it must be compiled with
contraction off, or the two paths will disagree for a reason that is not a
bug in either template.

Under this scheme `sum` is a stack slot like any other, loaded before the
add and stored after it, once per iteration of the innermost loop. That is
the waste this chapter accepts in exchange for getting every result right
first; [C2](c2-liveness.md) to [C5](c5-spilling.md) earn it back.

??? check "Why do the multiply and the add stay two instructions here without any rule saying so, and what would a back end that selects larger patterns need?"

    Each template covers one node, so the multiply node and the add node
    each produce their own instruction, and each result is rounded once, as
    decision 56 requires. A selector that covers several nodes with one
    instruction could match `add(x, mul(a, b))` with `fmadd`, which rounds
    once for both operations. It needs an explicit rule that forbids that
    pattern (or only allows it in an opt-in relaxed mode), and a test that
    fails if `fmadd` ever appears in Vortex float code.

## Two back ends, one oracle

A back end this simple is not trustworthy on its own; it is trustworthy in
pairs. By the time a native back end exists, a Vortex compiler already has a
working path, the one chosen at [stage 6](../compiler/guide/stage-6-first-machine-code.md#choosing-a-back-end):
LLVM IR, C, or an earlier assembly generator. Running the same program
through both and comparing what happens is **differential testing**. No one
writes down the expected output; two implementations that were not built to
make the same mistakes are run on the same input, and any disagreement means
at least one of them is wrong.

Yang, Chen, Eide and Regehr built Csmith, a generator of random C programs,
and spent three years using it to find compiler bugs. They report more
than 325 previously unknown bugs, and every compiler they tested crashed and
silently generated wrong code on valid input[^yang]. Csmith's README describes
its oracle as differential testing, and stresses that its programs avoid
undefined behavior[^csmith]. That matters: a program whose meaning is
undefined gives two correct compilers licence to disagree, and the test
learns nothing.

Figure 4 draws the arrangement for Vortex.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Differential testing: one Vortex program goes through the stage 6 back end and through the new native back end, both link the same runtime, and a comparator checks standard output, the runtime error line and the exit status" aria-describedby="b1-f4-desc">
<title id="b1-f4-title">Two back ends, one oracle</title>
<desc id="b1-f4-desc">On the left, one box: program.vx. Two arrows leave it. The upper path goes through the stage 6 back end (LLVM IR, C or assembly), then a box labelled executable A. The lower path goes through the B1 native back end, which writes a .s file, then cc -c and the linker, then executable B. A box labelled "same runtime library" sits between the two paths and feeds both links. Each executable runs and produces standard output, a runtime error line if any, and an exit status, 0 or 101. Both results flow into a comparator box on the right, which either reports "agree" or reports the first difference.</desc>
<defs><marker id="b1-f4-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="16" y="126" width="110" height="44" rx="4"/>
<text class="vx-mono" x="71" y="153" text-anchor="middle">program.vx</text>
<rect class="vx-box" x="170" y="36" width="200" height="54" rx="4"/>
<text class="vx-text" x="270" y="58" text-anchor="middle">stage 6 back end</text>
<text class="vx-text-muted" x="270" y="78" text-anchor="middle">LLVM IR, C or assembly</text>
<rect class="vx-box-strong" x="170" y="206" width="200" height="54" rx="4"/>
<text class="vx-text" x="270" y="228" text-anchor="middle">B1 native back end</text>
<text class="vx-text-muted" x="270" y="248" text-anchor="middle">.s, then cc -c and the linker</text>
<rect class="vx-box" x="190" y="124" width="160" height="48" rx="4"/>
<text class="vx-text" x="270" y="145" text-anchor="middle">same runtime</text>
<text class="vx-text-muted" x="270" y="163" text-anchor="middle">print, errors, start-up</text>
<rect class="vx-box" x="410" y="36" width="130" height="54" rx="4"/>
<text class="vx-text" x="475" y="58" text-anchor="middle">executable A</text>
<text class="vx-text-muted" x="475" y="78" text-anchor="middle">stdout, error, status</text>
<rect class="vx-box" x="410" y="206" width="130" height="54" rx="4"/>
<text class="vx-text" x="475" y="228" text-anchor="middle">executable B</text>
<text class="vx-text-muted" x="475" y="248" text-anchor="middle">stdout, error, status</text>
<rect class="vx-box-accent" x="590" y="116" width="154" height="64" rx="4"/>
<text class="vx-text" x="667" y="140" text-anchor="middle">compare</text>
<text class="vx-text-muted" x="667" y="160" text-anchor="middle">agree, or the first</text>
<text class="vx-text-muted" x="667" y="175" text-anchor="middle">difference</text>
<line class="vx-line" x1="126" y1="140" x2="170" y2="70" marker-end="url(#b1-f4-head)"/>
<line class="vx-line" x1="126" y1="156" x2="170" y2="226" marker-end="url(#b1-f4-head)"/>
<line class="vx-line" x1="270" y1="124" x2="270" y2="90" marker-end="url(#b1-f4-head)"/>
<line class="vx-line" x1="270" y1="172" x2="270" y2="206" marker-end="url(#b1-f4-head)"/>
<line class="vx-line" x1="370" y1="63" x2="410" y2="63" marker-end="url(#b1-f4-head)"/>
<line class="vx-line" x1="370" y1="233" x2="410" y2="233" marker-end="url(#b1-f4-head)"/>
<line class="vx-line" x1="540" y1="63" x2="600" y2="116" marker-end="url(#b1-f4-head)"/>
<line class="vx-line" x1="540" y1="233" x2="600" y2="180" marker-end="url(#b1-f4-head)"/>
<circle class="vx-dot" r="5">
<animateMotion dur="6s" repeatCount="indefinite" path="M126 140 L170 70 L475 63 L600 116"/>
</circle>
<circle class="vx-dot" r="5">
<animateMotion dur="6s" repeatCount="indefinite" path="M126 156 L170 226 L475 233 L600 180"/>
</circle>
</svg>
<figcaption>Figure 4. Differential testing for a Vortex compiler. Both paths link the same runtime, so a difference points at code generation rather than at <code>print</code>. The comparison covers everything a user can see: standard output, the runtime error line on standard error, and the exit status (0, or 101 after a runtime error).</figcaption>
</figure>

Sharing the runtime is deliberate. If each path had its own `print`, a
difference in how the two print an `f32` would look like a code generation
bug. Comparing the exit status and the error line matters as much as
comparing output: [decision 14](../decisions/program.md#d14) fixes exit status
101 and exactly one line on standard error when a runtime check fails, and a
native back end that forgets a bounds check prints wrong numbers, or none,
instead of that line.

The example below is differential testing in miniature. Its reference path
is a recursive evaluator. Its native path is a macro expander that emits
instructions for a two-register machine and a small interpreter that runs
them, so the comparison exercises the templates themselves. The `Sub`
template has the operand swap from the first check question planted in it:

--8<-- "includes/examples/backend/b1-simplest-backend/differential_check.cpp.md"

Output:

```text
--8<-- "examples/backend/b1-simplest-backend/differential_check.expected"
```

The comparison finds the bug without any test that was written about it:
every case without a subtraction agrees, and two with a subtraction
disagree. Look at the last case. `4 - 4` contains a subtraction and still
agrees, because swapping equal operands changes nothing. A fixed test list
can hold the construct that has a bug and still miss the bug. That is the
argument for generating inputs, many and varied, which is where
[E4](e4-testing-backends.md#differential-testing-across-back-ends) takes this
technique.

??? check "The two paths agree on every program in your suite. What does that prove, and what does a disagreement prove?"

    Agreement proves only that the two paths behave alike on those inputs.
    Both could be wrong the same way: two back ends built from the same
    misreading of the specification, or sharing a buggy runtime function,
    agree with each other. And inputs that never reach a bug, like `4 - 4`
    above, agree too. A disagreement proves that at least one path is wrong
    on that input, but not which one. The newer path is the natural first
    suspect; a value worked out by hand from the specification settles it.

## For Vortex

!!! vortex "Exercise"

    **Build** a macro-expansion back end for AArch64 over your compiler's
    IR, covering every construct your v0.1 compiler already accepts, and a
    differential test that compares it with the back end you chose at
    [stage 6](../compiler/guide/stage-6-first-machine-code.md#choosing-a-back-end).

    1. One fixed template per IR operation. Every operand is loaded from its
       slot immediately before use, and every result is stored to a fresh
       slot immediately after. Checked integer operations, comparisons,
       branches, calls and array accesses are templates too; labels come from
       a counter.
    2. Slot assignment and frame size computed for each function before any
       instruction is emitted, using the frame layout you built in
       [A5](a5-stack-frames.md#for-vortex), and a template for loads and
       stores whose offset does not fit the instruction's immediate field.
    3. Float templates that never fuse, and a note listing, for your stage 6
       path, which compiler flags keep contraction off there as well.
    4. A driver that writes the `.s` file, runs the system assembler and
       linker with the same runtime library the stage 6 path uses, and
       produces an executable.
    5. A differential test that builds every program in your test suite
       both ways, runs both, and reports the first program whose standard
       output, runtime error line or exit status differs.

    **Not yet:** no instruction choice beyond fixed templates
    ([C1](c1-instruction-selection.md)), no value kept in a register across
    templates ([C2](c2-liveness.md), [C3](c3-linear-scan.md)), no peephole
    cleanup ([C7](c7-peephole.md)), no second target ([B2](b2-x86-64.md)),
    no object-file writer of your own ([B3](b3-object-files.md)), and no
    random program generator yet ([E4](e4-testing-backends.md)).

    **Proof that it works:**

    - Every stage 6 to stage 10 test builds with the new back end, and the
      differential test reports zero disagreements on the whole suite,
      including the runtime-safety programs whose expected result is an
      error line and exit status 101.
    - The differential test fails, naming the right program, for each of
      three deliberate, temporary breakages: the two loads of the `i32`
      subtraction template swapped; the bounds check left out of the
      array-read template; and a slot offset off by 4 in the template that
      stores a call's result. A test that has never failed has
      not shown that it can.
    - A test compiles the stage 10 kernel and fails if any of `fmadd`,
      `fmsub`, `fnmadd` or `fnmsub` appears in the generated `.s`.
    - A function with a local array larger than 16 KiB, which reads its last
      element, compiles, assembles and returns the right value.
    - A table in your notes, filled in from your own compiler with its
      version and the date:

      | Program | Instructions emitted | Loads and stores | Frame bytes |
      | --- | --- | --- | --- |
      | stage 6's first program | | | |
      | the stage 10 `multiply` | | | |

      Keep it: it is the baseline the register-allocation chapters
      improve on.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is macro expansion?** One fixed template per kind of IR
      operation, chosen by the node's own kind alone, with no knowledge of
      its neighbours.
    - **Why does "every value in a stack slot" need no register allocator?**
      No value stays in a register past its own template, so no two values
      ever compete for one, and nothing needs saving around a call except
      the return address.
    - **Why do branches and labels need no bookkeeping in this scheme?** At every label
      every value is in its slot, so the paths that meet there already agree.
    - **How big is the frame?** The sum of the slot sizes, rounded up to a
      multiple of 16, plus a frame record if the function calls.
    - **What must a floating-point template never do?** Fuse a multiply and
      an add into one rounding; one template per node never does.
    - **What does a differential test prove?** A disagreement proves a bug in
      at least one path; agreement proves only that both behave alike on
      those inputs.

## Where this comes back

!!! next "You will use this again in"

    - [B2. A second target: x86-64](b2-x86-64.md): *one template per
      operation*, *the system toolchain*
    - [B3. Object files and assemblers](b3-object-files.md): *assembly
      text out*, *labels*
    - [C1. Instruction selection](c1-instruction-selection.md): *macro
      expansion as the first rung*
    - [C2. Liveness](c2-liveness.md): *every value in its slot at every
      label*
    - [C3. Register allocation I: linear scan](c3-linear-scan.md): *turning
      stack slots back into registers*
    - [C7. Peephole optimization](c7-peephole.md): *cleaning up
      template output*
    - [E4. Testing back ends](e4-testing-backends.md): *differential
      testing*, *random programs*

## Sources and further reading

Read Ghuloum first: the stack discipline in this chapter is his, on x86 and
in Scheme, and the paper grows a whole working compiler from it. Then read
chapter 2 of Hjort Blindell's survey, which follows macro expansion from the
1960s to its pairing with peephole optimization, the Davidson-Fraser
approach, which the survey names as the basis of GCC's back
end[^hjort]. Pfenning's lecture notes show the step after this chapter,
selection into temps followed by allocation. Appel's "Instruction Selection"
chapter[^appel] covers the same ground in textbook form.

[^aapcs64]: Arm, "Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)", release 2025Q4: the stack constraint that sp is a multiple of 16, and the caller-saved and callee-saved registers. <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
[^apple-arm64]: Apple, "Writing ARM64 code for Apple platforms": x29 must address a valid frame record (leaf functions may skip one), the 128-byte red zone, 16-byte stack alignment. <https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms>
[^appel]: Andrew W. Appel, *Modern Compiler Implementation*, Cambridge University Press, 1998: table of contents, chapter "Instruction Selection". <https://www.cs.princeton.edu/~appel/modern/toc.html>
[^chibicc]: Rui Ueyama, chibicc `README`: no optimization pass, output probably at least twice as slow as GCC's. <https://github.com/rui314/chibicc>
[^chibicc-codegen]: Rui Ueyama, chibicc, `codegen.c`: `push`, `pop` and the code for binary operators. <https://github.com/rui314/chibicc/blob/main/codegen.c>
[^clang-um]: Clang Project, "Clang Compiler User's Manual", entry `-ffp-contract`, read on 2026-09-24. <https://clang.llvm.org/docs/UsersManual.html#cmdoption-ffp-contract>
[^cmu-isel]: Frank Pfenning, "Lecture Notes on Instruction Selection", lecture 2, 15-411 Compiler Design, Carnegie Mellon University, 2013, sections 1 and 4. <https://www.cs.cmu.edu/~fp/courses/15411-f13/lectures/02-instsel.pdf>
[^csmith]: Csmith project, GitHub repository `README`: a random generator of C programs, free of undefined behavior, for finding compiler bugs with differential testing as the oracle. <https://github.com/csmith-project/csmith>
[^ghuloum]: Abdulaziz Ghuloum, "An Incremental Approach to Compiler Construction", *Proceedings of the 2006 Scheme and Functional Programming Workshop*, University of Chicago Technical Report TR-2006-06: sections "Binary Primitives", "Local Variables" and "Conditional Expressions". <http://scheme2006.cs.uchicago.edu/11-ghuloum.pdf>
[^hjort]: Gabriel Hjort Blindell, "Survey on Instruction Selection: An Extensive and Modern Literature Study", arXiv 1306.4898, 2013: chapter 2, sections 2.1 "The principle", 2.2.4 "Falling out of fashion" and 2.3.2 on the Davidson-Fraser approach. <https://arxiv.org/abs/1306.4898>
[^yang]: Xuejun Yang, Yang Chen, Eric Eide and John Regehr, "Finding and Understanding Bugs in C Compilers", *PLDI 2011*, abstract. <https://doi.org/10.1145/1993498.1993532>
