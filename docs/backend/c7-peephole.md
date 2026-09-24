# C7. Peephole optimization

<p class="page-intro">A peephole pass looks at two or three adjacent instructions at a time, matches them against a small set of rewrite rules, and repeats until nothing more applies. It is the cheapest pass in the back end, and for Vortex it is what turns instruction selection's and linear scan's honest, local decisions into code that does not waste cycles on its own bookkeeping.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 20 minutes · Builds on: [C1. Instruction selection](c1-instruction-selection.md), [C3. Register allocation I: linear scan](c3-linear-scan.md).</p>

???+ remember "Before you start, remember"

    ??? question "What does instruction selection actually produce?"

        A sequence of real machine instructions, one match at a time: each
        piece of the compiler's IR is turned into the instruction (or short
        instruction pattern) the target uses for it, chosen without looking
        at what a *different* match produced next to it.

        Introduced in [C1. Instruction selection](c1-instruction-selection.md).

    ??? question "What is a spill, and why does spilled code lean on the stack so heavily?"

        A spill is writing a live value out to memory because too few
        registers were free to hold it. An allocator under register
        pressure stores a value right after computing it and reloads it
        right before using it, so spilled code fills up with store and load
        pairs that a register-rich allocation would never have needed.

        Introduced in [C3. Register allocation I: linear scan](c3-linear-scan.md).

    ??? question "What does it mean for a value to be live at some point in a program?"

        It has already been computed and something later will still read
        it, so anything that overwrites or discards it before that read is
        a miscompilation. An allocator keeps a live value in a register or a
        spill slot for exactly as long as it stays live, and no longer.

        Introduced in [C3. Register allocation I: linear scan](c3-linear-scan.md).

    ??? question "Why can code generated correctly still be wasteful?"

        Correctness and efficiency are different questions. A compiler can
        choose a legal instruction for every operation, obey every calling
        convention and addressing rule, and still emit a self-move, a
        reload of a value already sitting in a register, or a multiply
        where a shift would do, because nothing earlier in the pipeline was
        looking for those patterns.

        Introduced in [C1. Instruction selection](c1-instruction-selection.md).

!!! goals "In this chapter"

    - Explain why a pass that only ever looks at two or three adjacent instructions can still find real, repeatable savings after instruction selection and register allocation.
    - State the side condition every peephole rule needs, and describe what goes wrong when a rule skips it.
    - Recognize the rule shapes that recur across real peephole optimizers: eliminating dead work, combining adjacent instructions, and strength-reducing an operation to a cheaper one.
    - Distinguish a fixed, hand-written rule set from a superoptimizer that searches for correct rewrites by exhaustive or randomized testing.
    - Connect load/store pairing and strength reduction to the instructions Vortex's fixed-shape arrays, its matmul kernel and its `&mut` output convention actually generate.

## A pass that looks at almost nothing

Here is straight-line code a simple code generator might emit for a few
lines near the start of a function: a copy that linear scan's coalescing
did not manage to remove, an array index scaled by an 8-byte element size,
and three stores, the last of which is immediately read back.

```text
mov x1, x1
mul x2, x3, 8
str s0, [x0, 0]
str s1, [x0, 4]
str s2, [x0, 8]
ldr s2, [x0, 8]
```

Every one of these six instructions is legal. Instruction selection chose a
correct opcode for each IR operation it saw, and the allocator assigned a
correct register to each value. Nothing here is a bug. And yet a compiler
that stopped here would be leaving cycles on the table for no reason: line 1
does nothing, line 2 asks the multiplier to do a shifter's job, and lines 5
and 6 write a value to memory and then immediately read the same bytes back
into the same register, as if the register had forgotten what it was
holding.

A **peephole optimizer** is a pass built to notice exactly this kind of
thing: it looks at a short, fixed-size **window** of adjacent instructions
(the name comes from looking at code as if through a narrow peephole in a
door[^mckeeman]), checks whether the window matches one of a small set of
patterns, and if it does, rewrites the window to something equivalent but
cheaper. Applied to the code above, four small rules turn six instructions
into three:

--8<-- "includes/examples/backend/c7-peephole/peephole_fixpoint.cpp.md"

Nothing in that rewrite required understanding the function, the loop it
sits in, or the type system upstream. Each rule only ever asked a question
about one or two neighbors: "does this move's destination equal its
source?", "is this store immediately followed by a load of the same
register from the same address?", "are these two stores four bytes apart on
the same base?". That narrowness is the whole point. A peephole pass is
cheap to write, cheap to run, and easy to convince yourself is correct, one
rule at a time, precisely because it refuses to look at more than a
handful of instructions before deciding.

<figure class="vx-figure" role="img" aria-label="Six before instructions on the left (a self-move, a multiply, three stores and a load) and three after instructions on the right (a shift, a paired store and a single store). A highlight steps through the before column in four stages, each connected by an arrow to what it becomes on the right: the self-move connects to nothing and is marked deleted; the multiply connects to the shift; the two float stores connect to the paired store; the last store and the reload connect to the single remaining store.">
<svg viewBox="0 0 760 290" xmlns="http://www.w3.org/2000/svg">
<text class="vx-text-accent" x="135" y="22" text-anchor="middle" font-weight="600">Before</text>
<text class="vx-text-accent" x="625" y="22" text-anchor="middle" font-weight="600">After</text>

<!-- Before column: six instructions -->
<g>
<rect class="vx-box" x="20" y="34" width="230" height="24" rx="3"/>
<text class="vx-mono" x="32" y="50">mov x1, x1</text>
<rect class="vx-box" x="20" y="64" width="230" height="24" rx="3"/>
<text class="vx-mono" x="32" y="80">mul x2, x3, 8</text>
<rect class="vx-box" x="20" y="94" width="230" height="24" rx="3"/>
<text class="vx-mono" x="32" y="110">str s0, [x0, 0]</text>
<rect class="vx-box" x="20" y="124" width="230" height="24" rx="3"/>
<text class="vx-mono" x="32" y="140">str s1, [x0, 4]</text>
<rect class="vx-box" x="20" y="154" width="230" height="24" rx="3"/>
<text class="vx-mono" x="32" y="170">str s2, [x0, 8]</text>
<rect class="vx-box" x="20" y="184" width="230" height="24" rx="3"/>
<text class="vx-mono" x="32" y="200">ldr s2, [x0, 8]</text>
</g>

<!-- After column: three instructions, centered against the six on the left -->
<g>
<rect class="vx-box-strong" x="510" y="79" width="230" height="24" rx="3"/>
<text class="vx-mono" x="522" y="95">lsl x2, x3, 3</text>
<rect class="vx-box-strong" x="510" y="109" width="230" height="24" rx="3"/>
<text class="vx-mono" x="522" y="125">stp s0, s1, [x0, 0]</text>
<rect class="vx-box-strong" x="510" y="139" width="230" height="24" rx="3"/>
<text class="vx-mono" x="522" y="155">str s2, [x0, 8]</text>
</g>

<!-- Step 1: self-move, eliminated, no line to the right -->
<g class="vx-seq" style="--vx-i: 0; --vx-n: 4">
<rect class="vx-box-accent" x="15" y="31" width="240" height="30" rx="4" fill="none"/>
<text class="vx-text-accent" x="270" y="50">eliminate</text>
<text class="vx-text-muted" x="270" y="65" font-size="11">(no replacement)</text>
</g>

<!-- Step 2: multiply to shift -->
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4">
<rect class="vx-box-accent" x="15" y="61" width="240" height="30" rx="4" fill="none"/>
<text class="vx-text-accent" x="270" y="80">strength-reduce</text>
<line class="vx-line" x1="360" y1="76" x2="500" y2="91"/>
<polygon class="vx-arrowhead" points="500,91 490,88 493,97"/>
</g>

<!-- Step 3: two stores paired -->
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4">
<rect class="vx-box-accent" x="15" y="91" width="240" height="60" rx="4" fill="none"/>
<text class="vx-text-accent" x="270" y="115">pair stores</text>
<line class="vx-line" x1="330" y1="121" x2="500" y2="121"/>
<polygon class="vx-arrowhead" points="500,121 490,116 490,126"/>
</g>

<!-- Step 4: store then reload, reload dropped -->
<g class="vx-seq" style="--vx-i: 3; --vx-n: 4">
<rect class="vx-box-accent" x="15" y="151" width="240" height="60" rx="4" fill="none"/>
<text class="vx-text-accent" x="270" y="175">drop reload</text>
<line class="vx-line" x1="330" y1="181" x2="500" y2="151"/>
<polygon class="vx-arrowhead" points="500,151 491,153 496,161"/>
</g>

<text class="vx-text-muted" x="135" y="245" text-anchor="middle" font-size="12">A highlight steps through the before column, left to right,</text>
<text class="vx-text-muted" x="135" y="261" text-anchor="middle" font-size="12">pausing on each match before moving to the next.</text>
</svg>
</figure>

## The window and the fixpoint

Formally, a peephole optimizer is driven by two pieces: a **window size**
(how many adjacent instructions a rule is allowed to look at, usually one to
three) and a set of **rewrite rules**, each a pattern to match and a
replacement to substitute. The pass slides the window along the
instruction list, position by position; wherever a rule's pattern matches,
the replacement takes its place.

A single sweep down the list is not enough, and the example above shows
why. Removing the self-move at position 0 does not only delete one
instruction, it also moves every later instruction one slot earlier, which
means a rule that did not match against its *old* neighbor might match
against its *new* one. In the running example, pairing the two `str`
instructions and dropping the redundant `ldr` are independent of the
self-move and the strength reduction, but in general a rewrite at one
position can expose a new match somewhere else entirely, including earlier
in the list. The only way to be sure nothing was missed is to keep
re-scanning until a full pass finds nothing left to rewrite: a
**fixpoint**, a state where applying every rule again changes nothing.
[^davidson-fraser] The example program above stops after four rewrites,
once no rule matches anywhere in the shortened list.

??? check "After the self-move at position 0 is removed, why does the pass rescan from the top instead of just continuing on to what was position 1?"

    Deleting an instruction shifts every later instruction's index, and it
    can also change which instructions are now adjacent to each other. A
    rule that needs the *old* neighbor of an instruction that just moved
    would no longer see it in the same window; a rule that only becomes
    possible once two instructions become neighbors, because something
    between them was deleted, would never fire if the scan just carried on
    forward. Restarting the scan (or, in a faster implementation, only
    re-checking positions near the edit) is what turns a single pass into a
    correct fixpoint.

## What a rule may not do

Every rule so far has been safe to apply on sight, because the toy program
has no other reader of the values a rule touches: nothing after position 1
reads `x1` again, nothing after the merged store touches `s0` or `s1`
separately. Real code is not always so considerate. Consider a rule that
looks tempting: "a compare immediately followed by another compare of the
same operands is redundant, drop the second one." That rule is unsound the
moment something *between* the two compares could change flags, or the
moment the first compare's result is still needed by a branch that has not
executed yet. A peephole rule's pattern describes what the window looks
like; it says nothing about what happens to use the values the window
produces. That is the job of a **side condition**: an extra check, evaluated
against the rest of the program, that a rule must pass before it is safe to
fire.[^davidson-fraser]

The condition that comes up constantly is **liveness**: is the flag,
register or memory location a rewrite would remove or change still going
to be read later, before anything redefines it? If yes, the rewrite is
unsound no matter how tidy it looks locally.

--8<-- "includes/examples/backend/c7-peephole/liveness_side_condition.cpp.md"

Program A's `cmp` cannot be dropped, because the branch right after the
`mov` still reads the flag it sets; the `mov`, on the other hand, can be
dropped, because `x1` is never read again. Program B has the identical
`cmp`, but with nothing left downstream to read its flag, so the same
instruction that was unsafe to delete in Program A is safe to delete in
Program B. The instruction did not change. The rest of the program did.
That is the entire reason a side condition has to look outside the window:
whether a rewrite is legal is a fact about what happens to run after it, not
about the two or three instructions the rule matched against.

??? check "True or false: because a peephole rule's window only ever spans two or three instructions, the rule never needs information from outside that window."

    False. The *pattern* a rule matches is local, but whether firing it is
    *safe* usually is not. A rule that deletes, reorders or merges
    instructions has to know whether something outside the window still
    depends on what it is about to remove or change, which means checking
    liveness (or, for a store, whether anything could still read that
    memory location) across the rest of the block. The window bounds what a
    rule is allowed to rewrite; it does not bound what the rule is allowed
    to know before rewriting it.

## A catalogue of rule shapes

Real peephole passes accumulate dozens of rules, but nearly all of them are
one of a handful of shapes. Recognizing the shape tells you what kind of
side condition to expect, and it is the vocabulary the next section's rule
sources use to describe what they generate.

**Eliminate.** Delete an instruction whose effect nothing downstream can
observe: a self-move, a store that gets overwritten before any load reads
it, a comparison whose flag result is dead. The self-move rule earlier in
this chapter needed no side condition at all, because a self-move changes
nothing about any register's value regardless of what runs before or after
it; the other eliminate rules need a liveness check, exactly as the
previous section described.

**Combine.** Replace two adjacent instructions with one that does the same
work. Folding a store-then-reload into a plain register move is one
example; the classic case in a load/store architecture is forming a
**paired load or store**, one instruction that moves two adjacent registers
to or from two adjacent memory locations instead of two separate
instructions doing it one at a time.[^mckeeman] AArch64 has `ldp` and `stp`
built in for exactly this. Folding address arithmetic into a
**post-indexed** addressing mode, where a single load or store both reads
memory and advances the base pointer by a fixed amount, is the same shape
applied to a loop that walks an array one element at a time.

**Strength-reduce.** Replace an expensive operation with a cheaper one that
computes the same result, when the operands allow it: a multiply or divide
by a power of two becomes a shift, a multiply by a small constant becomes a
shift-and-add sequence, a comparison against zero followed by a branch
becomes a single compare-and-branch instruction. This is the same idea that
[O5](../optimize/o5-constants-and-dead-code.md) already applies at the IR
level, run again after instruction selection has picked concrete opcodes, because
an immediate operand can turn out to be a convenient constant only once the
back end has committed to one.

??? check "Why does 'eliminate a self-move' need no side condition, while 'drop a redundant reload' and 'merge two stores into a pair' both do?"

    A self-move's effect on every register is nothing, in every possible
    continuation of the program: there is no state a self-move could have
    changed that a rule needs to worry about disturbing. Dropping a reload
    is only sound because the store immediately before it is known, from
    the window itself, to have just written the exact value the load would
    fetch; the moment something else could read that memory location
    between the two, or the store and the load stop being adjacent, that
    guarantee is gone. Merging two stores into a pair changes their
    granularity: anything that could observe the two writes happening
    separately, such as a debugger single-stepping between them or a
    concurrent reader, sees one atomic-looking write instead of two. A
    peephole pass over ordinary sequential code usually accepts that
    difference; a pass that also has to preserve fine-grained observability
    (for a debugger, or across threads) has to treat pairing as a rule with
    its own side condition too.

## Where the rules come from

Every rule in this chapter so far was written by hand: look at code a real
code generator produces, notice a pattern that always simplifies, write a
match and a replacement. That is how the earliest peephole optimizers
worked, and it still produces most of the rules a back end ships
with.[^mckeeman] Two later developments changed how such rule sets get
built and checked.

The first is a **rule DSL**: instead of writing matching code by hand for
each rule, the rules are written in a small domain-specific language and a
generator turns them into matching code (and, often, into the tables an
instruction selector uses in the first place, since selection and peephole
cleanup are both "recognize a pattern, emit a replacement" problems at
heart). Go's compiler writes its architecture-specific rewrites this way,
as flat lines such as "replace this operation shape with that one, when
this condition on the operands holds", compiled down to Go source by a
generator run over the rule file.[^go-ssa] Cranelift's ISLE language does
the same job for a Rust compiler back end, with a type system over
instruction and value shapes so that a rule which does not type-check
cannot compile.[^isle] A rule DSL does not remove the side-condition
problem from the previous section; it moves the condition into the rule's
own syntax, where a generator can at least check that every rule states
one.

The second is **superoptimization**: instead of a person noticing a
pattern, a program searches for one. The original superoptimizer took this
completely literally: given a short sequence to improve, exhaustively try
every shorter sequence from the target instruction set and keep the first
one that computes the same function, checked by testing it against enough
inputs to trust it.[^massalin] A later line of work automated the
*discovery* of whole rule sets this way: enumerate candidate left- and
right-hand sides up to some size, and for every candidate replacement, test
it against enough random inputs (and, where available, a solver) to accept
it as a general peephole rule rather than a one-off fix.[^bansal-aiken] The
example below does the most literal version of this, on a machine small
enough that the search finishes in a fraction of a second:

--8<-- "includes/examples/backend/c7-peephole/superoptimize_toy.cpp.md"

Notice what the search found: not the "obvious" `x*8 + x*2` decomposition a
person reaching for strength reduction would probably write down, but a
different, equally correct one, `r0 = r0 << 1` (turning `x` into `2x` in
place), `r1 = r0 << 2` (turning that into `8x`), `r0 = r0 + r1` (`2x + 8x =
10x`). Nothing told the search to prefer one decomposition over the other;
it accepted the first three-instruction program, in a fixed enumeration
order, that passed every one of the 256 possible 8-bit inputs. That is
both the appeal of superoptimization and its cost: it can find rewrites a
person would not have thought to write down, but "correct on every input I
tried" is doing real work in that sentence, and the space of programs to
search grows fast with program length and instruction-set size, which is
why real superoptimizers bound the search carefully and lean on solvers
rather than brute force once the target sequences get much longer than the
one above.

## A window you can finish yourself

One more window, from a different naive code generator, with only three of
this chapter's four rules given a chance to run:

```text
mov x4, x4
mul x5, x6, 4
str d0, [x1, 0]
str d1, [x1, 8]
```

The self-move at line 1 disappears, exactly as before. Line 2 strength-reduces:
4 is `2^2`, so it becomes a shift by 2. What about lines 3 and 4? They are
two adjacent stores to the same base register `x1`... but check the
offsets before reaching for the pairing rule: `0` and `8`, eight bytes
apart, not four. The pairing rule from earlier in this chapter required
*consecutive* 4-byte offsets, because it modeled single-precision (`s0`,
`s1`, four bytes each) stores; these are `d0` and `d1`, double-precision
registers, eight bytes each, and eight bytes apart is exactly consecutive
*for that width*. A rule written only for one operand width, applied
without checking it, would either miss this legitimate pairing opportunity
or, worse, fire on the wrong width and generate a `stp` whose second slot
does not line up with where the second value actually needs to live. The
rule's pattern has to include the element size, not only "four bytes
apart", before it can be trusted on both float and double stores.

## For Vortex

Vortex's back end will get exactly the kind of code this chapter's examples
are modeled on. A straightforward code generator, followed by a simple
allocator, reliably produces self-moves from coalescing that almost worked,
and reload pairs from values that got spilled and then immediately needed
again. The [matmul kernel](../compiler/guide/stage-10-matrix-multiplication.md)'s
inner loop is exactly where this matters most: it runs the same handful of
instructions once per output element, so a self-move or a redundant reload
inside it is not a one-time cost, it is paid on every iteration. Vortex's
**fixed-shape arrays** mean every element offset in that loop is a compile-time
constant, which is precisely the condition a load/store pairing rule needs to
recognize two stores (or loads) as consecutive; and the kernel writes its
result through a `&mut` output reference, so the store instructions a
pairing rule would act on are, in the naive lowering, the exact instructions
that convention generates.

One boundary matters more for Vortex than for a general-purpose compiler.
Vortex's specification promises no silent floating-point transformation
that changes a specified result. A peephole rule that spots a multiply
immediately followed by an add and fuses them into a single fused
multiply-add instruction is a completely standard combine rule in a
compiler with no such promise, and it can change a result's last bit: with
`a = b = 1 + 2^-12` and `c = -(1 + 2^-11)` in `float`, a fused multiply-add
gives `2^-24`, while the plain multiply followed by the plain add gives
`0`, checked on this machine (Apple clang 21, `-ffp-contract=off`, 24
September 2026). [A2](a2-aarch64-assembly.md) and
[A3](a3-floats-and-vectors.md) cover the register-level side of this; the
point here is that this kind of fusion is exactly the shape a combine rule
looks for, so it belongs on the excluded list until a future language
option asks for it, not because the rewrite is unsafe in the usual peephole
sense (nothing downstream misreads a stale value), but because it is
unsafe in the sense Vortex's floating-point rules specifically rule out.

!!! vortex "Exercise"

    Write three to five peephole rules for the instructions your own code
    generator actually emits after instruction selection and register
    allocation: pick real patterns you can see in your compiler's own
    output, not ones invented for the exercise. At minimum, include a rule
    that needs no side condition (a self-move or an equivalent no-op) and
    one that does (a redundant reload, or a pairing rule, gated on the
    liveness or addressing fact it depends on). Run your rules to a
    fixpoint, the way this chapter's example does.

    Do not build a rule DSL or a general-purpose superoptimizer yet: a
    handful of hand-written rules, each with a clear side condition, is the
    right size for a first pass. Do not add a rule that fuses a
    floating-point multiply and add, or any other rule that could change a
    specified `f32` or `f64` result, until the language gives you a reason
    to.

    Prove each rule two ways. First, a small positive test and a negative
    test per rule (a FileCheck-style pair showing the rewrite firing, and a
    case, such as a live flag or register, where the side condition
    correctly blocks it). Second, a differential check across your test
    kernels, including the matmul kernel: run the unoptimized and the
    peephole-optimized code on the same inputs and assert the `f32` results
    are bit-identical. A rule that passes the FileCheck tests but fails the
    differential check has a side condition you have not found yet.

## Key ideas

!!! recap "You can now answer"

    - **What is a peephole optimizer?** A pass that slides a small,
      fixed-size window over adjacent instructions, matches the window
      against a set of rewrite rules, and replaces a match with something
      cheaper but equivalent.
    - **Why does a single pass over the instruction list not suffice?** A
      rewrite can shift instructions' positions or make two instructions
      adjacent that were not adjacent before, exposing new matches; only
      repeating the scan to a fixpoint guarantees nothing was missed.
    - **What is a side condition, and why does it have to look outside the
      window?** An extra check a rule must pass before firing, usually a
      liveness question: will the value, flag or memory location the
      rewrite removes or changes still be read later? That is a fact about
      the rest of the program, not about the window itself.
    - **What are the three rule shapes this chapter names?** Eliminate
      (delete something with no observable effect), combine (merge
      adjacent instructions, such as forming a paired load or store), and
      strength-reduce (replace an expensive operation with a cheaper
      equivalent one).
    - **How does a superoptimizer differ from a hand-written rule set?** It
      finds a rewrite (or a whole rule) by searching short candidate
      programs and testing each one against enough inputs to trust it,
      rather than by a person noticing the pattern first.
    - **Why is a fused multiply-add combine rule out of bounds for Vortex
      today?** Vortex's specification forbids a silent floating-point
      transformation that changes a specified result, and fusing a multiply
      and an add can change the last bit of an `f32` or `f64` result.

## Where this comes back

!!! next "You will use this again in"

    - [D2. JIT compilation](d2-jit.md): *fixpoint rewriting*, *testing a rewrite against inputs before trusting it*
    - [D3. Reading real back ends](d3-real-backends.md): *rule DSLs*, *hand-written vs. generated rule tables*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *machine-level peephole passes*, *load/store pairing*

## Sources and further reading

This chapter's rule shapes and the term "peephole optimization" itself come
from McKeeman's original paper; the side-condition framing follows Davidson
and Fraser's retargetable peephole optimizer, which is where "does the
window's pattern still hold given what the rest of the program does" first
became a design question rather than an afterthought.[^mckeeman][^davidson-fraser]
Massalin's superoptimizer and Bansal and Aiken's automatic peephole
superoptimizer generation are the two papers behind the "search for a rule
instead of writing one" section.[^massalin][^bansal-aiken] LLVM's own
AArch64 back end runs a dedicated machine-level load/store optimization
pass at `-O2`: running `llc -O2 -mtriple=aarch64-unknown-linux-gnu
-debug-pass=Structure` with LLVM 18.1.8 on this machine (24 September 2026)
lists `aarch64-mi-peephole-opt`, `peephole-opt` and `aarch64-ldst-opt` among
the machine-level passes that run before register allocation and after it,
confirming that a production back end keeps a peephole pass in the pipeline
rather than treating it as a one-off cleanup step.

[^mckeeman]: William M. McKeeman, "Peephole Optimization", *Communications of the ACM* 8(7), 1965. <https://doi.org/10.1145/364995.365000>
[^davidson-fraser]: Jack W. Davidson and Christopher W. Fraser, "The Design and Application of a Retargetable Peephole Optimizer", *ACM Transactions on Programming Languages and Systems* 2(2), 1980. <https://doi.org/10.1145/357094.357098>
[^massalin]: Henry Massalin, "Superoptimizer: A Look at the Smallest Program", *ASPLOS* 1987. <https://doi.org/10.1145/36206.36194>
[^bansal-aiken]: Sorav Bansal and Alex Aiken, "Automatic Generation of Peephole Superoptimizers", *ASPLOS* 2006. <https://doi.org/10.1145/1168857.1168906>
[^go-ssa]: The Go Authors, "SSA Backend Rewrite Rules", Go compiler internals README. <https://github.com/golang/go/blob/master/src/cmd/compile/internal/ssa/README.md>
[^isle]: Chris Fallin, "Cranelift's Instruction Selector, Part 4: ISLE, a Domain-Specific Language for Instruction Selection", 20 January 2023. <https://cfallin.org/blog/2023/01/20/cranelift-isle/>
