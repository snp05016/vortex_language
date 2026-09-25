# A5. Stack frames

<p class="page-intro">Every call needs somewhere to keep what must outlive the next call: a return address, the registers a function borrows and must give back, and any local that does not fit in a register. This chapter builds that place, the stack frame, instruction by instruction, reads the frames real compilers build, and connects frame size to the runtime check Vortex needs when a function's local arrays outgrow the stack.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 45 minutes · Builds on: [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md), [A4. Calling conventions and ABIs](a4-calling-conventions.md)</p>

???+ remember "Before you start, remember"

    ??? question "What do x29 and x30 hold?"

        x29 is the frame pointer. x30 is the link register: `bl` writes the
        return address into it, and `ret` jumps to the address it holds.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md#registers-by-name).

    ??? question "What does `stp x29, x30, [sp, #-16]!` do?"

        It stores x29 at `sp - 16` and x30 at `sp - 8`, and writes `sp - 16`
        back into sp. The `!` marks pre-index addressing: the base register is
        updated to the address the instruction used.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md#loads-stores-and-addresses).

    ??? question "Which AArch64 registers must a callee hand back unchanged?"

        x19 to x28 (plus x29 and sp), and the low 64 bits of v8 to v15. Every
        other register may hold anything when the callee returns.

        Introduced in [A4. Calling conventions and ABIs](a4-calling-conventions.md#caller-saved-and-callee-saved-registers).

    ??? question "What must a Vortex program do when it runs out of call stack?"

        Stop at that point and report it like a failed runtime check, with the
        kind `stack`: one `runtime error[stack]` line on standard error and
        exit status 101. It must never carry on or end without a report.

        Introduced in [9. Runtime safety](../compiler/guide/stage-9-runtime-safety.md#running-out-of-stack). Decision: [record 46](../decisions/diagnostics.md#d46).

    ??? question "How many bytes does a `[f32; 64, 64]` occupy?"

        16,384: 4,096 elements of 4 bytes each, stored contiguously in
        row-major order with no gaps.

        Introduced in [8. Data in memory](../compiler/guide/stage-8-data-in-memory.md#arrays-in-memory). Decision: [record 43](../decisions/arrays.md#d43).

!!! goals "In this chapter"

    - Explain why a function that calls another needs a frame, and read a prologue and its epilogue instruction by instruction.
    - Walk a chain of frame records by hand, and state what AAPCS64 and Apple each require of it.
    - Lay out a frame's areas, with alignment padding, and check the layout against a compiler's listing.
    - Decide when a function may use the red zone, and explain why LLVM's AArch64 back end does not by default.
    - Explain why a frame larger than a guard page needs stack probes, and what that means for Vortex's `stack` runtime error.

## A function that needs somewhere to keep things

A function that calls nothing and fits in its registers needs no stack at all.
[A2](a2-aarch64-assembly.md#the-smallest-complete-file) opened with one: an
`add` and a `ret`. The moment a function calls another, two problems appear
that registers alone cannot solve.

The first is the return address. `bl helper` writes the address of the next
instruction into x30, so that `helper`'s `ret` can find its way back. But the
caller also arrived through a `bl`, and its own return address was in x30 too.
The call has just overwritten it. Unless the caller copied x30 somewhere
first, it can never return.

The second is every value the caller still needs after the call.
[A4](a4-calling-conventions.md#caller-saved-and-callee-saved-registers) split
the registers into two groups. A **caller-saved** register (x0 to x17, x18 where
the platform does not reserve it, and most of the vector registers) may hold anything after a call. A
**callee-saved** register (x19 to x28, x29, and the low 64 bits of v8 to v15)
must come back unchanged[^aapcs64-regs]. A value that must survive a call has
two places to go: a callee-saved register, or memory. And a callee-saved
register is only free to use once its old value, which belongs to someone
further up the call chain, has itself been put in memory.

That memory is the **stack**: a region each thread owns, addressed by the
stack pointer, sp. AAPCS64 calls its stack **full-descending**: it grows
toward lower addresses, and the bytes from sp upward are in use[^aapcs64-stack].
A function claims n bytes by subtracting n from sp and gives them back by
adding n. The bytes one call claims are its **frame**; Appel's textbook calls
the same thing an **activation record**[^appel]. The code at the start of a
function that builds its frame is the **prologue**, and the code before each
`ret` that takes it down is the **epilogue**.

### A frame built by hand

`sum_three_and_call(a, b, c)` computes `helper(a) + b + c + a*b`. It must keep
`b`, `c` and `a*b` alive across the call to `helper`, and its own return
address too. It uses all three ways a value can survive a call: `b` goes into
x19, a callee-saved general register; `c` goes into d8, the low half of the
callee-saved v8; and `a*b` goes into memory.

--8<-- "includes/examples/backend/a5-stack-frames/frame_record.s.md"

Read the prologue one instruction at a time.

1. `stp x29, x30, [sp, #-48]!` claims 48 bytes and, in the same instruction,
   stores the caller's frame pointer at the new sp and the return address 8
   bytes above it.
2. `mov x29, sp` points x29 at those two words.
3. `str x19, [sp, #16]` and `str d8, [sp, #24]` save the caller's values of
   the two callee-saved registers this function is about to reuse. They
   cannot share one `stp`: a pair instruction moves two registers of the same
   file, and x19 and d8 live in different files.

From here on, nothing the caller needs is only in a register the callee may
change. The body computes `a*b` and stores it at `[sp, #32]`, moves `b` into
x19 and `c` (converted to a double) into d8, and calls `helper`. The call
overwrites x30 and may overwrite x0 to x18; it must hand back x19, d8, x29 and
sp as they were.

The epilogue undoes the prologue. It reloads x19 and d8 from the same slots
they were saved in, then `ldp x29, x30, [sp], #48` reloads the frame pointer
and return address and adds 48 back to sp, and `ret` jumps to the restored
x30. The order matters in one place: the reloads must come before sp moves
up. Once sp is above a slot, the slot is outside the active stack, and
AAPCS64 forbids any access to that region[^aapcs64-stack].

Why 48 bytes, when the saves and `a*b` need only 36? Because AAPCS64 requires
sp to be a multiple of 16 whenever memory is accessed through it, a rule the
hardware itself enforces, and again at every public call[^aapcs64-stack].
A frame whose size is not a multiple of 16 would leave sp misaligned for the
whole body. So 36 rounds up to 48, and 12 bytes above `a*b` are padding.

Figure 1 steps through the prologue and epilogue, one state at a time.

<figure class="vx-figure">
<div class="vx-stepper">
<div class="vx-step">
<p><strong>Step 1. On entry.</strong> sp marks the lowest byte the caller owns. x29 still points at the caller's frame record, somewhere inside the caller's frame. x30 holds the return address.</p>
<svg viewBox="0 0 640 300" role="img" aria-label="Step 1, on entry: the caller's frame at the top. sp points at its lower edge. x29 points at the caller's frame record inside the caller's frame. Nothing below sp belongs to anyone yet.">
<defs><marker id="a5-f1-h1" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="240" y="20" width="200" height="70" rx="4"/>
<text class="vx-text" x="340" y="45" text-anchor="middle">caller's frame</text>
<rect class="vx-box-strong" x="270" y="55" width="140" height="24" rx="2"/>
<text class="vx-mono" x="340" y="72" text-anchor="middle">caller's record</text>
<line class="vx-line" x1="120" y1="67" x2="266" y2="67" marker-end="url(#a5-f1-h1)"/>
<text class="vx-text-accent" x="110" y="72" text-anchor="end">x29</text>
<line class="vx-line" x1="120" y1="90" x2="236" y2="90" marker-end="url(#a5-f1-h1)"/>
<text class="vx-text-accent" x="110" y="95" text-anchor="end">sp</text>
<text class="vx-text-muted" x="340" y="130" text-anchor="middle">lower addresses: not claimed</text>
<text class="vx-text-muted" x="460" y="30">high addresses</text>
<text class="vx-text-muted" x="460" y="280">low addresses</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 2. After <code>stp x29, x30, [sp, #-48]!</code>.</strong> sp has moved down 48 bytes and the lowest 16 now hold the frame record. x29 still points at the caller's record: for one instruction the new record exists but is not yet the head of the chain.</p>
<svg viewBox="0 0 640 300" role="img" aria-label="Step 2: sp has moved down 48 bytes. The bottom 16 bytes of the new frame hold saved x29 and x30. The two bands above them are claimed but not yet written. x29 still points at the caller's record.">
<defs><marker id="a5-f1-h2" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="240" y="20" width="200" height="70" rx="4"/>
<text class="vx-text" x="340" y="45" text-anchor="middle">caller's frame</text>
<rect class="vx-box-strong" x="270" y="55" width="140" height="24" rx="2"/>
<text class="vx-mono" x="340" y="72" text-anchor="middle">caller's record</text>
<rect class="vx-box" x="240" y="90" width="200" height="40" rx="2"/>
<text class="vx-text-muted" x="340" y="115" text-anchor="middle">not written yet</text>
<text class="vx-mono" x="450" y="115">#32</text>
<rect class="vx-box" x="240" y="130" width="200" height="40" rx="2"/>
<text class="vx-text-muted" x="340" y="155" text-anchor="middle">not written yet</text>
<text class="vx-mono" x="450" y="155">#16</text>
<rect class="vx-box-accent" x="240" y="170" width="200" height="40" rx="2"/>
<text class="vx-mono" x="340" y="195" text-anchor="middle">saved x29 | saved x30</text>
<text class="vx-mono" x="450" y="195">#0</text>
<line class="vx-line" x1="120" y1="67" x2="266" y2="67" marker-end="url(#a5-f1-h2)"/>
<text class="vx-text-accent" x="110" y="72" text-anchor="end">x29</text>
<line class="vx-line" x1="120" y1="210" x2="236" y2="210" marker-end="url(#a5-f1-h2)"/>
<text class="vx-text-accent" x="110" y="215" text-anchor="end">sp</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 3. Just before <code>bl helper</code>.</strong> x29 points at the new record, x19 and d8 are saved, and <code>a*b</code> sits in the top band. The 128 bytes below sp are Apple's red zone, but this function is about to call <code>helper</code>, which starts at the same sp and may use them itself.</p>
<svg viewBox="0 0 640 300" role="img" aria-label="Step 3: the 48-byte frame is complete. From high to low: a times b and padding at offset 32, saved x19 and d8 at offset 16, the frame record at offset 0. Both x29 and sp point at offset 0. Below sp, a dashed band marks the red zone, not usable because the function is about to call helper.">
<defs><marker id="a5-f1-h3" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="240" y="20" width="200" height="70" rx="4"/>
<text class="vx-text" x="340" y="45" text-anchor="middle">caller's frame</text>
<rect class="vx-box-strong" x="270" y="55" width="140" height="24" rx="2"/>
<text class="vx-mono" x="340" y="72" text-anchor="middle">caller's record</text>
<rect class="vx-box" x="240" y="90" width="200" height="40" rx="2"/>
<text class="vx-mono" x="340" y="115" text-anchor="middle">a*b | padding</text>
<text class="vx-mono" x="450" y="115">#32</text>
<rect class="vx-box" x="240" y="130" width="200" height="40" rx="2"/>
<text class="vx-mono" x="340" y="155" text-anchor="middle">saved x19 | saved d8</text>
<text class="vx-mono" x="450" y="155">#16</text>
<rect class="vx-box-accent" x="240" y="170" width="200" height="40" rx="2"/>
<text class="vx-mono" x="340" y="195" text-anchor="middle">saved x29 | saved x30</text>
<text class="vx-mono" x="450" y="195">#0</text>
<line class="vx-line" x1="120" y1="205" x2="236" y2="205" marker-end="url(#a5-f1-h3)"/>
<text class="vx-text-accent" x="110" y="210" text-anchor="end">x29, sp</text>
<path class="vx-line" d="M 240 190 C 200 190, 200 70, 266 67" marker-end="url(#a5-f1-h3)"/>
<rect class="vx-box-bad" x="240" y="214" width="200" height="40" rx="2"/>
<text class="vx-text-muted" x="340" y="239" text-anchor="middle">red zone: helper's now</text>
<text class="vx-text-muted" x="340" y="280" text-anchor="middle">the saved x29 links to the caller's record</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 4. After the epilogue.</strong> x19 and d8 were reloaded while the frame was still claimed; then <code>ldp x29, x30, [sp], #48</code> reloaded the record and released all 48 bytes. sp and x29 are back where step 1 found them, and <code>ret</code> jumps to x30.</p>
<svg viewBox="0 0 640 300" role="img" aria-label="Step 4: the 48 bytes below the caller's frame are drawn dashed, released. sp is back at the caller's lower edge and x29 is back at the caller's record, as in step 1.">
<defs><marker id="a5-f1-h4" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="240" y="20" width="200" height="70" rx="4"/>
<text class="vx-text" x="340" y="45" text-anchor="middle">caller's frame</text>
<rect class="vx-box-strong" x="270" y="55" width="140" height="24" rx="2"/>
<text class="vx-mono" x="340" y="72" text-anchor="middle">caller's record</text>
<rect class="vx-box-bad" x="240" y="90" width="200" height="120" rx="2"/>
<text class="vx-text-muted" x="340" y="155" text-anchor="middle">released: no longer ours</text>
<line class="vx-line" x1="120" y1="67" x2="266" y2="67" marker-end="url(#a5-f1-h4)"/>
<text class="vx-text-accent" x="110" y="72" text-anchor="end">x29</text>
<line class="vx-line" x1="120" y1="90" x2="236" y2="90" marker-end="url(#a5-f1-h4)"/>
<text class="vx-text-accent" x="110" y="95" text-anchor="end">sp</text>
</svg>
</div>
</div>
<figcaption>Figure 1. The frame of <code>sum_three_and_call</code> in four states. Offsets count up from sp, as the assembly writes them; higher addresses are drawn higher.</figcaption>
</figure>

??? check "Suppose the prologue saved x19 at `[sp, #24]` and d8 at `[sp, #16]`, but the epilogue was left as written. What goes wrong, and when would you notice?"

    The epilogue loads x19 from `[sp, #16]`, which now holds the caller's d8
    bits, and d8 from `[sp, #24]`, which holds the caller's x19. Both
    registers come back holding each other's values. This function still
    returns the right answer, so its own tests pass. The failure appears in
    the caller, or further up, the next time it reads x19 or d8: far from the
    bug. Had both sides been swapped together, nothing would change, because
    the frame's inside is this function's private business.

## The frame record and the chain

The two words at the bottom of Figure 1's frame are its **frame record**: the
caller's frame pointer, then the return address. AAPCS64 fixes their order:
the lower word points to the previous frame record and the higher word holds
the value LR had on entry. x29 must point at the record of the innermost
call[^aapcs64-fp].

Because each record holds the address of the record before it, the records
form a linked list through the stack, the **frame chain**. AAPCS64 marks its
end with a zero in the previous-record word. It also leaves the record's
position inside a frame unspecified: a compiler may put it at the bottom, the
top, or anywhere between[^aapcs64-fp].

Whether a platform keeps the chain at all is the platform's decision. AAPCS64
lists four levels, from a frame record at all times to none, with x29 then
free to use as an ordinary callee-saved register[^aapcs64-fp]. Apple chose a
strict one: x29 "must always address a valid frame record", though leaf
functions and tail calls may skip creating one, and in return stack traces
"are always meaningful, even without debug information"[^apple-regs].

Figure 2 draws the chain for four nested calls.

<figure class="vx-figure">
<svg viewBox="0 0 640 360" role="img" aria-label="Four frames stacked from main at the top to inner at the bottom. Each frame holds a two-word frame record: saved frame pointer and return address. The x29 register points at inner's record. Each saved frame pointer has an arrow to the record of the frame above it: inner to middle, middle to outer, outer to main. main's saved frame pointer continues to the frames of the code that started main, and the chain ends at a zero.">
<defs><marker id="a5-f2-h" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text-muted" x="330" y="20" text-anchor="middle">... up to a saved frame pointer of 0</text>
<rect class="vx-box" x="200" y="30" width="260" height="70" rx="4"/>
<text class="vx-text" x="215" y="52">main</text>
<rect class="vx-box-strong" x="280" y="62" width="170" height="28" rx="2"/>
<text class="vx-mono" x="290" y="81">fp | lr</text>
<rect class="vx-box" x="200" y="100" width="260" height="70" rx="4"/>
<text class="vx-text" x="215" y="122">outer</text>
<rect class="vx-box-strong" x="280" y="132" width="170" height="28" rx="2"/>
<text class="vx-mono" x="290" y="151">fp | lr</text>
<rect class="vx-box" x="200" y="170" width="260" height="70" rx="4"/>
<text class="vx-text" x="215" y="192">middle</text>
<rect class="vx-box-strong" x="280" y="202" width="170" height="28" rx="2"/>
<text class="vx-mono" x="290" y="221">fp | lr</text>
<rect class="vx-box" x="200" y="240" width="260" height="70" rx="4"/>
<text class="vx-text" x="215" y="262">inner</text>
<rect class="vx-box-accent" x="280" y="272" width="170" height="28" rx="2"/>
<text class="vx-mono" x="290" y="291">fp | lr</text>
<path class="vx-flow" d="M 300 272 C 250 250, 250 175, 278 205"/>
<path class="vx-flow" d="M 300 202 C 250 180, 250 105, 278 135"/>
<path class="vx-flow" d="M 300 132 C 250 110, 250 35, 278 65"/>
<path class="vx-flow" d="M 300 62 C 280 45, 300 30, 330 26"/>
<rect class="vx-box-accent" x="40" y="272" width="70" height="28" rx="4"/>
<text class="vx-mono" x="75" y="291" text-anchor="middle">x29</text>
<line class="vx-line" x1="110" y1="286" x2="276" y2="286" marker-end="url(#a5-f2-h)"/>
<text class="vx-text-muted" x="470" y="81">lr: a return address</text>
<text class="vx-text-muted" x="470" y="99">inside main's caller</text>
<text class="vx-text-muted" x="470" y="291">lr: a return address</text>
<text class="vx-text-muted" x="470" y="309">inside middle</text>
<text class="vx-text-muted" x="330" y="340" text-anchor="middle">stack grows down: each caller's record is at a higher address</text>
</svg>
<figcaption>Figure 2. The frame chain. x29 points at the innermost record; each record's first word points at its caller's record. A debugger that follows the chain and reads each record's second word gets one return address per active call: a backtrace, with no debug information.</figcaption>
</figure>

The next example walks the chain in a running program. Each function records
its own frame address on the way in, with GCC's and Clang's
`__builtin_frame_address(0)`, which returns the frame pointer register's value
in a function that has a frame[^gcc-frame]. The innermost function then
follows the saved frame pointers and checks each link.

--8<-- "includes/examples/backend/a5-stack-frames/frame_chain.cpp.md"

It prints:

```text
--8<-- "examples/backend/a5-stack-frames/frame_chain.expected"
```

The same program runs unchanged on x86-64, where the frame pointer is rbp
and the psABI's frame figure shows the same two words, the return address at
`8(%rbp)` and the previous rbp at `0(%rbp)`[^psabi-frame]. The `.toml` passes
`-fno-omit-frame-pointer` because the chain is optional there: the psABI
notes that a function may address its frame from rsp and use rbp for other
values instead[^psabi-frame]. Code built that way breaks the chain, and a
debugger then needs unwind tables to walk the stack ([D1](d1-debug-info.md)).

`helper` in the first example is the case Apple's rule exempts. It calls
nothing and changes neither x29 nor x30, so a backtrace taken inside it still
works: x29 still points at `sum_three_and_call`'s record, and x30 still holds
the way back.

For Vortex, keeping a record in every function that calls anything is worth
it even on Linux, where the ABI does not demand it. A bounds failure three
calls deep then produces a full `lldb bt` with no debug information and no
unwind tables, and the cost is small: a function that calls must save x30
anyway, and saving x29 beside it in the same `stp` adds one `mov` or `add`.

## Frame layout: what goes where

A frame has room for up to four kinds of things, and a compiler decides,
function by function, which it needs.

- The **frame record**, whenever the platform or the compiler keeps a chain.
- The **callee-saved area**: one slot for each callee-saved register the
  function reuses, saved once in the prologue and restored once in each
  epilogue, however many calls the body makes in between.
- **Locals and spill slots**: every local whose address is taken, every local
  array (a register cannot be indexed by a run-time value), and every value
  the register allocator could not keep in a register. A **spill slot** is a
  frame slot that holds such a value ([C5](c5-spilling.md)).
- The **outgoing argument area**: when the function calls something that
  takes arguments on the stack ([A4](a4-calling-conventions.md#where-arguments-go-two-counters-not-one)),
  it reserves room for the largest such call once, at the bottom of the
  frame, so that each call writes its arguments at fixed offsets from sp
  without moving sp.

The order of these areas is the compiler's choice, and two compilers built
from the same LLVM source already disagree. The comment at the top of LLVM's
AArch64 frame lowering draws both layouts: on Darwin the frame record sits at
the top of the callee-saved area, because Apple's compact unwind encoding and
existing tools depend on it; on other platforms it sits at the bottom of the
general-register saves. Below the saves come the fixed-size locals and spill
slots, and at the very bottom the outgoing argument area[^llvm-aarch64-frame].

<figure class="vx-figure">
<svg viewBox="0 0 660 380" role="img" aria-label="Two frame layouts side by side, high addresses at the top. Left, Darwin: frame record at the top, x29 pointing at it, then callee-saved general registers, then callee-saved floating-point registers, then locals and spill slots, then the outgoing argument area with sp at the bottom. Right, other AArch64 targets: callee-saved general registers at the top, then the frame record with x29 pointing at it, then floating-point saves, locals and spill slots, and outgoing arguments with sp at the bottom.">
<defs><marker id="a5-f3-h" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="190" y="22" text-anchor="middle">Darwin (Apple)</text>
<text class="vx-text" x="500" y="22" text-anchor="middle">Other AArch64 targets</text>
<rect class="vx-box" x="90" y="32" width="200" height="30" rx="2"/>
<text class="vx-text-muted" x="190" y="52" text-anchor="middle">caller's frame</text>
<rect class="vx-box-accent" x="90" y="62" width="200" height="36" rx="2"/>
<text class="vx-mono" x="190" y="85" text-anchor="middle">frame record</text>
<rect class="vx-box" x="90" y="98" width="200" height="44" rx="2"/>
<text class="vx-text" x="190" y="125" text-anchor="middle">saved x19..x28</text>
<rect class="vx-box" x="90" y="142" width="200" height="44" rx="2"/>
<text class="vx-text" x="190" y="169" text-anchor="middle">saved d8..d15</text>
<rect class="vx-box-strong" x="90" y="186" width="200" height="90" rx="2"/>
<text class="vx-text" x="190" y="228" text-anchor="middle">locals and</text>
<text class="vx-text" x="190" y="246" text-anchor="middle">spill slots</text>
<rect class="vx-box" x="90" y="276" width="200" height="44" rx="2"/>
<text class="vx-text" x="190" y="303" text-anchor="middle">outgoing arguments</text>
<line class="vx-line" x1="30" y1="98" x2="86" y2="98" marker-end="url(#a5-f3-h)"/>
<text class="vx-text-accent" x="30" y="92">x29</text>
<line class="vx-line" x1="30" y1="320" x2="86" y2="320" marker-end="url(#a5-f3-h)"/>
<text class="vx-text-accent" x="30" y="314">sp</text>
<rect class="vx-box" x="400" y="32" width="200" height="30" rx="2"/>
<text class="vx-text-muted" x="500" y="52" text-anchor="middle">caller's frame</text>
<rect class="vx-box" x="400" y="62" width="200" height="44" rx="2"/>
<text class="vx-text" x="500" y="89" text-anchor="middle">saved x19..x28</text>
<rect class="vx-box-accent" x="400" y="106" width="200" height="36" rx="2"/>
<text class="vx-mono" x="500" y="129" text-anchor="middle">frame record</text>
<rect class="vx-box" x="400" y="142" width="200" height="44" rx="2"/>
<text class="vx-text" x="500" y="169" text-anchor="middle">saved d8..d15</text>
<rect class="vx-box-strong" x="400" y="186" width="200" height="90" rx="2"/>
<text class="vx-text" x="500" y="228" text-anchor="middle">locals and</text>
<text class="vx-text" x="500" y="246" text-anchor="middle">spill slots</text>
<rect class="vx-box" x="400" y="276" width="200" height="44" rx="2"/>
<text class="vx-text" x="500" y="303" text-anchor="middle">outgoing arguments</text>
<line class="vx-line" x1="340" y1="142" x2="396" y2="142" marker-end="url(#a5-f3-h)"/>
<text class="vx-text-accent" x="340" y="136">x29</text>
<line class="vx-line" x1="340" y1="320" x2="396" y2="320" marker-end="url(#a5-f3-h)"/>
<text class="vx-text-accent" x="340" y="314">sp</text>
<text class="vx-text-muted" x="330" y="352" text-anchor="middle">Any area may be empty. Every boundary that sp stops at is a multiple of 16.</text>
</svg>
<figcaption>Figure 3. Where LLVM's AArch64 back end puts each area, drawn after the comment in its frame lowering source. Only the record's position differs between the two; both satisfy AAPCS64, which leaves that position open.</figcaption>
</figure>

A two-line C function shows the Darwin layout in practice. `f(a, b, c)`
returns `helper(a) + b + c`. Apple clang 21 at `-O2`, on the owner's M4 Pro
(2026-09-24), wrote this prologue and epilogue:

```gas
stp     x20, x19, [sp, #-32]!   ; claim 32 bytes; c will live in x19, b in x20
stp     x29, x30, [sp, #16]     ; frame record at the top of the frame
add     x29, sp, #16            ; x29 points at the record
        ; ... bl _helper and the additions ...
ldp     x29, x30, [sp, #16]
ldp     x20, x19, [sp], #32     ; release the frame in the last reload
ret
```

Compare it with `sum_three_and_call`, which put its record at the bottom: the
first `stp` claims the frame here too, but it stores the register saves, and
x29 is set with an `add` because the record is not at sp.

### Reading a compiler's frame

The listing a compiler writes is the ground truth for a frame, and `-O0` is
the easiest place to start, because at `-O0` clang gives every local its own
stack slot and reads and writes that slot at every use. `average3` copies its
three float arguments into a three-element array and averages it:

--8<-- "includes/examples/backend/a5-stack-frames/locals.cpp.md"

Apple clang 21, `-O0 -S -fno-stack-protector -target arm64-apple-macos`
(2026-09-24), wrote this, with the loop body elided:

```gas
sub     sp, sp, #32
str     s0, [sp, #28]        ; a arrives in s0 and gets its own slot
str     s1, [sp, #24]        ; b
str     s2, [sp, #20]        ; c
ldr     s0, [sp, #28]
str     s0, [sp, #8]         ; values[0] = a
ldr     s0, [sp, #24]
str     s0, [sp, #12]        ; values[1] = b
ldr     s0, [sp, #20]
str     s0, [sp, #16]        ; values[2] = c
movi.2d v0, #0000000000000000
str     s0, [sp, #4]         ; sum = 0.0
str     wzr, [sp]            ; i = 0
        ; ... the loop reads values[i] as [x8, x9, lsl #2], x8 = sp + 8 ...
ldr     s0, [sp, #4]
fmov    s1, #3.00000000
fdiv    s0, s0, s1
add     sp, sp, #32
ret
```

Draw the frame from the offsets, highest first: `a` at 28, `b` at 24, `c` at
20, `values` from 8 to 19, `sum` at 4 and `i` at 0. Six 4-byte objects and one
12-byte array fill exactly 32 bytes, which is already a multiple of 16, so
there is no padding. There is no frame record either: `average3` calls
nothing, and Apple's rule lets a leaf skip it. The whole prologue is one
`sub` and the whole epilogue one `add`.

??? check "Lay out this frame by hand, Darwin style. A function calls another, keeps two values in x19 and x20 and one double in d8 across the call, and has a local `i64` and a local `[f32; 3]` whose addresses are taken. What is the smallest frame size, and where does each item go?"

    Frame record 16 bytes, x19 and x20 16 bytes, d8 8 bytes, the `i64` 8
    bytes, the array 12 bytes: 60 bytes in total, rounded up to 64. Darwin
    style, from the top down: the record at offsets 48 to 63 (x29 = sp + 48),
    x19 and x20 at 32 to 47, d8 at 24, the `i64` at 16 (8-aligned), the array
    at 4 to 15, and 4 bytes of padding at 0 to 3. Other orders are valid too,
    as long as each item is aligned to its own size and the total is a
    multiple of 16. Check your answer against clang's listing for an
    equivalent C function: it may order the objects differently.

## The red zone

`average3` spent two instructions moving sp for 32 bytes of scratch space.
Some platforms let a leaf skip even that. The **red zone** is a fixed-size
area just below sp that the platform promises not to disturb, so a function
may keep temporaries there without claiming them.

Apple's arm64 ABI defines a red zone of the 128 bytes below sp, which
"Apple platforms don't modify ... during exceptions"[^apple-redzone]. The SysV
AMD64 psABI reserves 128 bytes beyond rsp that "shall not be modified by
signal or interrupt handlers", and says leaf functions may use it for their
whole frame instead of adjusting the stack pointer[^psabi-frame]. AAPCS64,
which Linux on arm64 follows, has no red zone: it forbids any thread to read
or write the stack below sp at all[^aapcs64-stack].

The promise holds only while no other function runs. A callee starts at the
caller's sp, so the caller's red zone is the callee's frame. Apple states it
directly: the caller "must assume that the callee modifies the contents of its
red zone"[^apple-redzone]. That is why Figure 1 marks the red zone as lost
the moment `sum_three_and_call` calls `helper`.

--8<-- "includes/examples/backend/a5-stack-frames/leaf_red_zone.s.md"

`average_red_zone` never moves sp. Its `stur` instructions write at negative
offsets from sp and its `ldur` instructions read them back; these are the
unscaled forms [A2](a2-aarch64-assembly.md#loads-stores-and-addresses)
introduced, needed because the plain `ldr` and `str` immediate forms cannot
encode a negative offset.

Having a red zone does not mean a compiler uses it. LLVM's AArch64 back end
keeps red-zone use behind a hidden option, `aarch64-redzone`, that is off by
default[^llvm-aarch64-frame]. For a leaf `.ll` function that stores two `i32`
values into a four-element local array, `llc` 18.1.8 for
`arm64-apple-macosx` (2026-09-24) wrote, without and with
`-aarch64-redzone`:

```gas
; default                          ; -aarch64-redzone
str     w0, [sp, #-16]!            stur    w0, [sp, #-16]
str     w1, [sp, #4]               stur    w1, [sp, #-12]
ldr     w8, [sp]                   ldur    w8, [sp, #-16]
ldr     w9, [sp, #4]               ldur    w9, [sp, #-12]
add     w0, w8, w9                 add     w0, w8, w9
add     sp, sp, #16                ret
ret
```

The default version already hides the claim inside a pre-index store, so
here the red zone saves exactly one instruction, the final `add`.

??? check "A Vortex function sums an array in a loop, prints the running total once halfway through with `print`, and keeps summing. The loop body calls nothing else. On Apple's targets, may the sum live in the red zone while the loop runs?"

    No. The function calls `print`, so it is not a leaf, and the red zone is
    only safe while nothing else runs. `print` starts at the same sp and may
    overwrite those bytes. The sum must survive the call in a callee-saved
    register (d8 to d15 hold a double's 64 bits) or in a slot inside the
    function's own frame.

## Frame indices: offsets come last

Everything so far had its offsets chosen in advance. A compiler cannot do
that early, because it does not yet know how many callee-saved registers the
function will need, or how many values the register allocator will spill.
So it names stack slots first and places them last.

In LLVM IR a stack slot is an `alloca`: it has a type and an alignment, and
LangRef describes it as memory released automatically when the function
returns[^llvm-langref]. It has no offset. After instruction selection, each
slot becomes a **frame index**: an abstract number standing for "stack object
number k", which instructions use as an operand until the real offset is
known[^llvm-backend]. The pass that decides the offsets is
**PrologEpilogInserter**. It runs after register allocation, finalizes the
frame layout, saves callee-saved registers, emits the prologue and epilogue,
and replaces every frame index with a register-plus-offset address; after it,
no frame index may appear[^llvm-pei].

The example below gives `llc` two functions: `mix`, with three small slots of
mixed alignment, and `tile`, with one slot of 16 KiB, which the next section
uses.

--8<-- "includes/examples/backend/a5-stack-frames/frame_objects.ll.md"

`llc -O2` 18.1.8 for `arm64-apple-macosx` (2026-09-24) turned `mix` into:

```gas
sub     sp, sp, #32
sxtw    x8, w2
add     w9, w0, w1
add     w10, w2, w2
stp     w0, w1, [sp, #24]    ; %pair:   offsets 24 to 31
add     w0, w9, w10
str     w2, [sp, #20]        ; %single: offsets 20 to 23
str     x8, [sp, #8]         ; %wide:   offsets 8 to 15, 8-aligned
add     sp, sp, #32
ret
```

Work out the layout by hand. The slots are placed from the top down in the
order the IR declared them: `%pair` takes 8 bytes at 24, `%single` 4 bytes at
20. `%wide` needs 8-byte alignment, so it cannot start at 16 (it would end at
23, overlapping `%single`); it goes to 8, leaving 16 to 19 as padding. That
makes 24 bytes, rounded up to 32 for sp, so 0 to 7 is padding too: 12 of the
32 bytes hold nothing.

The arithmetic never reads the stores back. The instruction selector saw
each load read a value just stored in the same block and used the register
directly (`add w10, w2, w2` is `%v2 + %v3lo`, both equal to `c`). The stores
themselves survive `llc`. Run through `opt -O2` first, the function has no
`alloca` left at all: SROA ([O7](../optimize/o7-inlining-and-sroa.md))
turns all three slots into plain values, and the frame disappears.

## Large frames: immediates, probes and the guard page

A 48-byte frame costs one instruction to claim. A `[f32; 64, 64]` local is 16 KiB, and two new
problems arrive with it.

The first is encoding. [A2](a2-aarch64-assembly.md#reaching-a-global-table)
showed that an `add` or `sub` immediate is 12 bits, optionally shifted left
by 12. A frame of 5,008 bytes fits neither form, so the compiler splits it.
For a C function with a 5,000-byte local array that it passes to `sink`,
Apple clang 21 at `-O2` (2026-09-24) wrote:

```gas
stp     x28, x27, [sp, #-32]!
stp     x29, x30, [sp, #16]
add     x29, sp, #16
mov     w9, #5008                   ; the size to claim, for the probe routine
adrp    x16, ___chkstk_darwin@GOTPAGE
ldr     x16, [x16, ___chkstk_darwin@GOTPAGEOFF]
blr     x16                         ; probe the new stack before claiming it
sub     sp, sp, #1, lsl #12         ; 4096
sub     sp, sp, #912                ; + 912 = 5008
add     x0, sp, #8
bl      _sink
add     sp, sp, #1, lsl #12
add     sp, sp, #912
ldp     x29, x30, [sp, #16]
ldp     x28, x27, [sp], #32
ret
```

Built for `aarch64-linux-gnu`, the same function saves x28 but not x27 (we do
not chase why here), and it calls no probe routine: there, clang probes only
when asked, with `-fstack-clash-protection`.

The second problem is the call to `___chkstk_darwin`. A common way to catch
a stack overflow is a **guard region** at the end of the stack: pages left
unmapped, so that touching them faults (LangRef's stack-probe attributes
assume one[^llvm-langref]). A program that recurses too deeply walks sp down
into the guard, the next store faults, and the runtime can report stack
exhaustion. That works only if the program touches the guard before it
touches anything below it. A single `sub sp, sp, #16384` can move sp past a
guard smaller than 16 KiB, and the next store then lands in whatever memory
lies beyond. Nothing faults, and the program keeps running on corrupted
memory.

A **stack probe** is the fix: before or while claiming a large frame, touch
the new stack at least once per guard-sized step, so that the guard cannot be
skipped. LangRef's `probe-stack` attribute states the guarantee: stack
accesses must be no further apart than the size of the guard region, and the
companion `stack-probe-size` attribute defaults that size to 4,096
bytes[^llvm-langref]. On the owner's machine, `lldb` shows that
`___chkstk_darwin` reads one byte every 4,096 bytes from sp down to sp minus
the requested size, x9. Apple clang 21 emitted the call for a local array of
4,096 bytes but not for one of 4,080 (measured 2026-09-24).

<figure class="vx-figure">
<svg viewBox="0 0 660 340" role="img" aria-label="Two copies of the same memory. From top to bottom: the thread's stack with sp partway down, the stack limit, an unmapped guard region, then other mapped memory. Left, unprobed: sp jumps 16 KiB in one step, past the guard, and the first store lands in other mapped memory with no fault. Right, probed: sp moves down in 4 KiB steps with a store after each step; the step that reaches the guard faults.">
<defs><marker id="a5-f4-h" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="180" y="22" text-anchor="middle">One jump of 16 KiB</text>
<text class="vx-text" x="490" y="22" text-anchor="middle">Probed in 4 KiB steps</text>
<rect class="vx-box" x="100" y="32" width="160" height="110" rx="2"/>
<text class="vx-text-muted" x="180" y="60" text-anchor="middle">thread's stack</text>
<rect class="vx-box-bad" x="100" y="142" width="160" height="50" rx="2"/>
<text class="vx-text-muted" x="180" y="172" text-anchor="middle">guard: unmapped</text>
<rect class="vx-box-strong" x="100" y="192" width="160" height="110" rx="2"/>
<text class="vx-text-muted" x="180" y="222" text-anchor="middle">other memory</text>
<line class="vx-line" x1="40" y1="100" x2="96" y2="100" marker-end="url(#a5-f4-h)"/>
<text class="vx-text-accent" x="40" y="94">sp</text>
<path class="vx-flow" d="M 270 100 C 320 140, 320 220, 270 260"/>
<circle class="vx-dot" cx="180" cy="260" r="6"/>
<text class="vx-text" x="180" y="288" text-anchor="middle">store: no fault</text>
<rect class="vx-box" x="410" y="32" width="160" height="110" rx="2"/>
<text class="vx-text-muted" x="490" y="60" text-anchor="middle">thread's stack</text>
<rect class="vx-box-bad" x="410" y="142" width="160" height="50" rx="2"/>
<text class="vx-text-muted" x="490" y="172" text-anchor="middle">guard: unmapped</text>
<rect class="vx-box-strong" x="410" y="192" width="160" height="110" rx="2"/>
<text class="vx-text-muted" x="490" y="222" text-anchor="middle">other memory</text>
<line class="vx-line" x1="350" y1="100" x2="406" y2="100" marker-end="url(#a5-f4-h)"/>
<text class="vx-text-accent" x="350" y="94">sp</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 3"><circle class="vx-dot" cx="490" cy="118" r="5"/><text class="vx-mono" x="580" y="122">probe</text></g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 3"><circle class="vx-dot" cx="490" cy="136" r="5"/><text class="vx-mono" x="580" y="140">probe</text></g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 3"><circle class="vx-dot" cx="490" cy="160" r="5"/><text class="vx-text" x="580" y="164">fault</text></g>
<text class="vx-text-muted" x="330" y="330" text-anchor="middle">Sizes are schematic: what matters is that no step is larger than the guard.</text>
</svg>
<figcaption>Figure 4. Why large frames need probes. Without them, one large claim can skip the guard region and write into unrelated memory without faulting. With them, the first touch inside the guard faults, which is the event a runtime can turn into a stack-exhaustion report.</figcaption>
</figure>

The `tile` function in `frame_objects.ll` asks for probes explicitly with
`"probe-stack"="inline-asm"`. `llc -O2` for `aarch64-linux-gnu` (2026-09-24)
claimed its 16 KiB in four steps of `sub sp, sp, #1, lsl #12` each followed
by `str xzr, [sp]`, a store of zero that exists only to touch the page.
Without the attribute, the same function claimed it with one `sub sp, sp,
#4, lsl #12` and no probe. For much larger frames clang emits a loop instead
of an unrolled sequence: a 64 KiB frame compiled with
`-fstack-clash-protection` for Linux got a four-instruction loop that steps
sp by 4,096 and stores zero at each step.

This is where frames meet Vortex's rules. This function is valid Vortex, and
its frame is at least 16 KiB:

```vortex
// items: valid
fn corner_sum(scale: f32) -> f32 {
    let mut tile: [f32; 64, 64] = [0.0; 64, 64];
    tile[0, 0] = scale;
    tile[63, 63] = scale;
    return tile[0, 0] + tile[63, 63];
}
```

[Record 46](../decisions/diagnostics.md#d46) requires that a program that
exhausts its stack stops with a `stack` report, never silently. If your
runtime relies on a guard region to notice exhaustion, it notices only if
every frame larger than the guard is probed. LLVM IR offers no help by
default: LangRef calls an `alloca` undefined if there is not enough stack
space for it[^llvm-langref]. Whatever back end you use, turning a guard-page
fault into Vortex's report is your runtime's job (stage 9 left the method to
you); making sure the fault happens is your frame lowering's.

??? check "A recursive Vortex function has a 64-byte frame and recurses a million times. Another function has one local `[f32; 64, 64]` and is called once, near the end of the stack. Which needs probes to meet record 46, and why?"

    The large one. The recursive function moves sp by 64 bytes at a time, so
    one of its frames always lands inside the guard before any frame lands
    below it, and the store that saves its frame record faults. The
    16 KiB frame can move sp past the whole guard in one instruction, and its
    first store may land in mapped memory. It needs probes; the small frames
    need none.

## Shrink-wrapping

One more choice is about where, not what. A function with an early exit
may not need its frame on every path. Compiled by Apple clang 21 at `-O2`
(2026-09-24), this C function

```c
int helper(int);
int checked(int x) {
  if (x < 0) return -1;
  return helper(x) + x;
}
```

became, with LLVM's default settings and with `-mllvm
-enable-shrink-wrap=false`:

```gas
; default                               ; shrink-wrapping off
_checked:                               _checked:
  tbnz  w0, #31, LBB0_2                   stp   x20, x19, [sp, #-32]!
  stp   x20, x19, [sp, #-32]!             stp   x29, x30, [sp, #16]
  stp   x29, x30, [sp, #16]               add   x29, sp, #16
  add   x29, sp, #16                      tbnz  w0, #31, LBB0_2
  mov   x19, x0                           mov   x19, x0
  bl    _helper                           bl    _helper
  add   w0, w0, w19                       add   w0, w0, w19
  ldp   x29, x30, [sp, #16]               b     LBB0_3
  ldp   x20, x19, [sp], #32             LBB0_2:
  ret                                     mov   w0, #-1
LBB0_2:                                 LBB0_3:
  mov   w0, #-1                           ldp   x29, x30, [sp, #16]
  ret                                     ldp   x20, x19, [sp], #32
                                          ret
```

On the left, a negative `x` tests one bit, loads -1 and returns: no frame is
built at all. On the right it builds a frame, saves two registers, and takes
it all down again for nothing. (x20 is saved on both sides but never
written; it fills the second half of the 16-byte slot one `stp` writes.)

This is **shrink-wrapping**: placing the saves and restores only around the
part of the function that needs them. Chow introduced the idea in 1988 for
callee-saved registers[^chow]. LLVM's version, the ShrinkWrap pass, is
coarser. It looks for one block where the whole prologue can go and one where
the whole epilogue can go: the prologue's block must dominate every frame
access, the epilogue's must post-dominate them, and every run through one
must be matched by a run through the other[^llvm-shrinkwrap]. It runs before
PrologEpilogInserter, which then emits the prologue and epilogue at the
points it found[^llvm-pei]. [O2](../optimize/o2-cfg-and-dominance.md)
defined dominance; here it decides where a frame begins.

For Vortex this matters most for runtime checks. A function whose only call
is on a failing check's reporting path could, with shrink-wrapping, build no
frame on the path that passes. That is an optimization for later; a first
back end builds one frame on entry.

## For Vortex

!!! vortex "Exercise"

    **Build** frame layout for your AArch64 back end: the step that decides,
    for each function, what goes in its frame and at what offset, and the
    prologue and epilogue that build and remove it. It runs after you know
    which callee-saved registers the function uses and which values live in
    memory (whatever your back end does for that today).

    1. For each function, list its stack objects with size and alignment:
       every local array, every local whose address is taken, and every value
       your back end keeps in memory. A `[f32; 64, 64]` local is one object
       of 16,384 bytes.
    2. Decide whether the function needs a frame record. Write down your rule
       for each target you support and why it satisfies that platform's
       level of frame-chain conformance.
    3. Assign every object an offset aligned to its own requirement, round
       the total to a multiple of 16, and record the layout in a form you can
       print for a test.
    4. Emit a prologue and matching epilogues. Reload callee-saved registers
       before releasing the frame.
    5. For every frame larger than 4,096 bytes, claim it so that no two
       stack touches are more than 4,096 bytes apart. Write a decision note
       stating the stack size your programs get
       ([record 46](../decisions/diagnostics.md#d46) requires it) and how a
       guard-page fault becomes the `stack` report.

    **Not yet:** the red zone; shrink-wrapping; frames for variable-sized
    objects (Vortex v0.1 has fixed shapes, so every size is known at compile
    time); spill slots chosen by a real register allocator
    ([C5](c5-spilling.md)); x86-64 frames ([B2](b2-x86-64.md)); unwind tables
    ([D1](d1-debug-info.md)).

    **Proof that it works:**

    - Every stage 0 to 10 golden test still passes.
    - A test compiles a program with one function per shape (a leaf with no
      locals, a function that calls, one with a `[f32; 3]` local, one with a
      `[f32; 64, 64]` local) and checks, from your printed layouts, that
      every frame size is a multiple of 16 and no two objects overlap.
    - Stopped in `lldb` at a breakpoint four Vortex calls deep, a program
      built with no debug information shows all four functions in `bt`.
    - A function that keeps a value in a callee-saved register across a call
      to a second function that overwrites that register returns the right
      answer.
    - Two programs end with `runtime error[stack]` and exit status 101: one
      that recurses without bound through a function with no locals, and one
      that recurses without bound through a function holding a
      `[f32; 256, 256]` local (256 KiB per frame). Neither may end with a bare
      signal or keep running.
    - A table in your notes, filled in from your compiler with its version
      and the date, of the frame size in bytes for: the stage 10 `multiply`,
      a leaf with no locals, and `corner_sum` above.

    | Function | Frame size (bytes) | Frame record? | Probed? |
    | --- | --- | --- | --- |
    | `multiply` (stage 10) | | | |
    | leaf, no locals | | | |
    | `corner_sum` | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **Why does a function that calls another need a frame?** Its `bl` overwrites x30, and every value it needs after the call must sit in memory or in a callee-saved register whose old value was first saved to memory.
    - **What is a frame record, and how does a debugger walk the chain?** Two words, the caller's frame pointer then the return address, with x29 pointing at them; the debugger follows each saved frame pointer to the next record until it finds zero.
    - **Must a leaf function on Apple's targets build a frame record?** No: Apple lets leaf functions and tail calls skip it, and x29 then still points at the caller's valid record.
    - **Why is a frame's size a multiple of 16 on AArch64?** AAPCS64 requires sp to be 16-byte aligned whenever memory is accessed through it, not only at calls.
    - **Who has a red zone?** Apple arm64 and SysV x86-64 (128 bytes each); AAPCS64 forbids touching memory below sp, and LLVM's AArch64 back end does not use a red zone by default.
    - **What is a frame index, and when does it become an offset?** A placeholder for a stack object; PrologEpilogInserter replaces it with a real address after register allocation.
    - **Why does a frame larger than the guard region need probes?** One large claim can skip the guard, so exhaustion writes into other memory instead of faulting; probes touch every guard-sized step.

## Where this comes back

!!! next "You will use this again in"

    - [B1. The simplest back end that works](b1-simplest-backend.md): *a slot per value*, *frame size computed before emitting code*
    - [B2. A second target: x86-64](b2-x86-64.md): *rbp chains*, *the SysV red zone*, *the return-address push*
    - [C5. Spilling, splitting and rematerialization](c5-spilling.md): *spill slots*, *frame layout after allocation*
    - [D1. Debug information](d1-debug-info.md): *the frame record*, *unwinding without a frame chain*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *frame indices*, *PrologEpilogInserter*, *shrink-wrapping*

## Sources and further reading

Read AAPCS64's sections on the stack and the frame pointer first; they are
short. Apple's arm64 page states the frame-record and red-zone rules in a few
lines each. The comment at the top of LLVM's `AArch64FrameLowering.cpp` is
the best single description of a production AArch64 frame. Bendersky's post
walks through an x86-64 frame for contrast[^bendersky], and Appel's chapter
treats frames in a machine-independent way.

[^aapcs64-regs]: Arm, "Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)", release 2025Q4, sections "General-purpose Registers" (r19 to r29 callee-saved) and "SIMD and Floating-Point Registers" (only the bottom 64 bits of v8 to v15 callee-saved). <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
[^aapcs64-stack]: Arm, AAPCS64, release 2025Q4, section "The Stack": the stack is full-descending; "Universal stack constraints" (no thread may access the inactive region below sp; SP mod 16 = 0 whenever memory is accessed through SP) and "Stack constraints at a public interface". <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
[^aapcs64-fp]: Arm, AAPCS64, release 2025Q4, section "The Frame Pointer": the frame record's two words, the zero that ends the chain, the unspecified position of the record in a frame, and the four levels of conformance a platform may choose. <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
[^apple-regs]: Apple, "Writing ARM64 code for Apple platforms", Apple Developer Documentation, section "Respect the purpose of specific CPU registers". <https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms>
[^apple-redzone]: Apple, "Writing ARM64 code for Apple platforms", section "Respect the stack's red zone". <https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms>
[^psabi-frame]: x86-64 psABI group, "System V Application Binary Interface, AMD64 Architecture Processor Supplement", section "The Stack Frame": the frame figure, the footnote on addressing the frame from rsp instead of rbp, and the 128-byte red zone. <https://gitlab.com/x86-psABIs/x86-64-ABI>
[^llvm-aarch64-frame]: LLVM Project, `llvm/lib/Target/AArch64/AArch64FrameLowering.cpp`: the header comment drawing the frame layout (frame record at the top of the callee-saved area on Darwin, at the bottom elsewhere; locals and spill slots below; the reserved call frame for outgoing arguments), and the `aarch64-redzone` option, default false. <https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/AArch64/AArch64FrameLowering.cpp>
[^llvm-pei]: LLVM Project, `llvm/lib/CodeGen/PrologEpilogInserter.cpp`, header comment: the pass finalizes the frame layout, saves callee-saved registers and emits the prologue and epilogue; it runs after register allocation, and no frame index operand may be created after it. The pass uses the save and restore points found by shrink-wrapping. <https://github.com/llvm/llvm-project/blob/main/llvm/lib/CodeGen/PrologEpilogInserter.cpp>
[^llvm-shrinkwrap]: LLVM Project, `llvm/lib/CodeGen/ShrinkWrap.cpp`, header comment: the Save and Restore points and the conditions that make them safe. <https://github.com/llvm/llvm-project/blob/main/llvm/lib/CodeGen/ShrinkWrap.cpp>
[^llvm-backend]: LLVM Project, "Writing an LLVM Backend": `eliminateFrameIndex`, which eliminates abstract frame indices from instructions. <https://llvm.org/docs/WritingAnLLVMBackend.html>
[^llvm-langref]: LLVM Project, "LLVM Language Reference Manual": the `alloca` instruction (released when the function returns; undefined if stack space is insufficient) and the function attributes `"probe-stack"`, `"stack-probe-size"` (default 4096) and `"frame-pointer"`. <https://llvm.org/docs/LangRef.html>
[^gcc-frame]: Free Software Foundation, *Using the GNU Compiler Collection*, "Getting the Return or Frame Address of a Function": `__builtin_frame_address`. <https://gcc.gnu.org/onlinedocs/gcc/Return-Address.html>
[^chow]: Fred C. Chow, "Minimizing register usage penalty at procedure calls", *Proceedings of the ACM SIGPLAN 1988 Conference on Programming Language Design and Implementation (PLDI '88)*. <https://doi.org/10.1145/53990.53999>
[^appel]: Andrew W. Appel, *Modern Compiler Implementation* (ML, Java and C editions), Cambridge University Press, 1998, chapter "Activation Records". <https://www.cs.princeton.edu/~appel/modern/toc.html>
[^bendersky]: Eli Bendersky, "Stack frame layout on x86-64", 6 September 2011. <https://eli.thegreenplace.net/2011/09/06/stack-frame-layout-on-x86-64>
