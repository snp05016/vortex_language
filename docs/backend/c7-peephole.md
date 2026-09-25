# C7. Peephole optimization

<p class="page-intro">A peephole pass looks at a few neighbouring instructions at a time and replaces a pattern it recognizes with something cheaper, repeating until nothing more matches. This chapter builds one, shows the liveness facts every rule depends on, and follows the same ideas into LLVM's AArch64 back end and into programs that search for rules instead of waiting for a person to write them. For Vortex it is the cheapest way to clean up what a simple selector and allocator leave behind.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 40 minutes · Builds on: [C1. Instruction selection](c1-instruction-selection.md), [C3. Register allocation I: linear scan](c3-linear-scan.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does a macro-expanding code generator do with the result of every operation?"

        It writes the result to its own stack slot, and the next template
        loads it back from that slot. Each template looks only at its own
        node, so a store followed at once by a load of the same slot is
        normal output.

        Introduced in [B1. The simplest back end that works](b1-simplest-backend.md#one-template-per-operation).

    ??? question "Why can instruction selection miss a cheaper instruction that the machine offers?"

        A selector covers one tree (or one block's DAG) at a time with tiles.
        If a shift and the add that consumes it come from different
        statements, no single tile ever sees both, so the combined form,
        such as an add with a shifted operand, is never chosen.

        Introduced in [C1. Instruction selection](c1-instruction-selection.md).

    ??? question "When is a register live at a point in the code?"

        When some path from that point reaches a read of the register before
        any write to it. A register that is not live holds nothing anyone
        will look at, so an instruction whose only effect is to write it can
        go.

        Introduced in [C2. Liveness](c2-liveness.md#where-a-register-becomes-free).

    ??? question "What does `stp x29, x30, [sp, #-16]!` do, and what does `ldr w10, [x0], #4` do to `x0`?"

        The first stores two registers to two consecutive 8-byte slots with
        one instruction (a store pair). The second loads from `x0` and then
        adds 4 to `x0`: post-index addressing, where the base register moves
        after the access.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md).

    ??? question "Which instructions read the flags that `cmp x0, #0` sets?"

        Conditional instructions that follow it: a `b.eq` or `b.ne`, a
        `csel`, a `cset`. The flags stay until the next instruction that
        sets them, so a reader may sit several instructions later.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md#flags-and-conditions).

!!! goals "In this chapter"

    - Apply a set of peephole rules to a listing by hand, to a fixpoint, and explain why one rewrite can create the match for the next.
    - State the side condition each rule needs, usually that a register or the flags are dead, and show a rule firing without it producing a wrong answer.
    - Recognize the common rule shapes on AArch64: deleting dead work, forwarding a store to a load, strength reduction, folding a shift into an operand, pairing loads and stores, and folding a pointer update into post-index addressing.
    - Read LLVM's AArch64 pipeline and name where its peephole passes run and what the load/store optimizer changed in a small function.
    - Explain how a superoptimizer finds rules by search, and what separates a rule that passed tests from a rule that was proved.

## Code that is correct and wasteful

Here is what a naive code generator might emit for two array stores,
`a[i] = v; a[i + 1] = w`, with 8-byte elements. The index `i` was spilled
and reloaded, the scale factor was materialized in a register, and a copy
survived allocation with the same register on both sides:

```text
0  str x1, [sp, #16]      // spill i
1  ldr x1, [sp, #16]      // reload i
2  mov x9, #8             // element size
3  mul x2, x1, x9         // i * 8
4  add x3, x0, x2         // &a[i]
5  str x4, [x3, #0]       // a[i] = v
6  mov x5, x5             // a copy onto itself
7  str x6, [x3, #8]       // a[i + 1] = w
```

Every line is legal AArch64, and the program computes the right thing. It
is also twice as long as it needs to be. Line 1 reloads a value that is
still in `x1`. Line 6 does nothing. Lines 2 and 3 multiply by a power of
two, which a shift does. Line 4 could apply that shift itself, because an
AArch64 `add` accepts a shifted register as its second operand. Lines 5
and 7 write two neighbouring 8-byte slots, which one **store pair**
(`stp`) instruction does[^arm-pair].

None of this is a failure of an earlier pass doing its own job. The
selector ([C1](c1-instruction-selection.md)) chose instructions for each
tree without seeing the neighbouring trees, the allocator
([C3](c3-linear-scan.md)) decided where values live without rewriting the
instructions around them, and the spill code came from a rule that is
simple on purpose. What is missing is a pass that reads the finished
instructions side by side.

## A window, a set of rules, a fixpoint

McKeeman named the technique in 1965: redundant instructions can be
discarded in the final stage of compilation by **peephole optimization**[^mckeeman].
The name describes the method. The pass looks at the output through a small
opening, a **window** of a few adjacent instructions. A **rewrite rule** is
a pattern for the window plus a replacement, and the pass slides the window
down the code, trying every rule at every position.

Davidson and Fraser gave the idea its classic form in their optimizer PO:
it simulates each pair of adjacent instructions and replaces the pair with
a single cheaper instruction that has the same effect, whenever the machine
has one. Their stated guarantee is the target to aim for: when PO finishes,
no instruction and no adjacent pair can be replaced by a cheaper single
instruction[^davidson-fraser]. They also drew the practical conclusion:
with such a pass behind it, a code generator can stay naive and emit only
the simplest sequences[^davidson-fraser].

The example below writes five rules for the listing above:

| Rule | Window | Replacement | Side condition |
| --- | --- | --- | --- |
| R1 | `mov r, r` | nothing | none |
| R2 | `str a, [b, #o]` then `ldr c, [b, #o]` | the store, then `mov c, a` | none |
| R3 | `mov k, #2^n` then `mul d, s, k` | `lsl d, s, #n` | `k` dead afterwards |
| R4 | `lsl t, s, #n` then `add d, m, t` | `add d, m, s, lsl #n` | `t` dead afterwards |
| R5 | `str a, [b, #o]` then `str c, [b, #o+8]` | `stp a, c, [b, #o]` | `o` fits the `stp` field |

The **side condition** is the part of a rule that the window alone cannot
check. R3 deletes the instruction that wrote `k`; if a later instruction
reads `k`, deleting it is wrong. The next section is about exactly that.
The program applies the rules to a **fixpoint**: after each rewrite it
starts again from the top, and it stops only when a full pass over the code
finds nothing to rewrite.

--8<-- "includes/examples/backend/c7-peephole/peephole_fixpoint.cpp.md"

Eight instructions become three in the first run. The spill store at
position 0 stays: the pass cannot see whether anything later reads the
stack slot, so it treats memory as always needed.

### The walk, one rewrite at a time

Follow the first run by hand. Each step shows the code after one rewrite,
with the window that matched.

<div class="vx-stepper" markdown="1">
<div class="vx-step" markdown="1">

**Step 1. R2 at position 0**

The window is `str x1, [sp, #16]` and `ldr x1, [sp, #16]`: a load from the
slot the previous instruction wrote. Nothing between them could change
the slot, because nothing is between them, so the load would read the value
of `x1`. R2 turns it into a register move.

```text
0  str x1, [sp, #16]
1  mov x1, x1            <- was ldr x1, [sp, #16]
2  mov x9, #8
3  mul x2, x1, x9
4  add x3, x0, x2
5  str x4, [x3, #0]
6  mov x5, x5
7  str x6, [x3, #8]
```

</div>
<div class="vx-step" markdown="1">

**Step 2. R1 at position 1**

The rewrite in step 1 produced a move from `x1` to `x1`. R2 did not need a
special case for "same register"; R1 cleans up after it. Rules that each do
one small thing and rely on the fixpoint to combine them are easier to check
than one rule that tries to cover every case.

```text
0  str x1, [sp, #16]
1  mov x9, #8
2  mul x2, x1, x9
3  add x3, x0, x2
4  str x4, [x3, #0]
5  mov x5, x5
6  str x6, [x3, #8]
```

</div>
<div class="vx-step" markdown="1">

**Step 3. R3 at position 1**

The window is `mov x9, #8` and `mul x2, x1, x9`. 8 is $2^3$, so the multiply
is a shift left by 3. The rule deletes the `mov`, so it first asks whether
`x9` is read later: the rest of the code never reads it, and `x9` is not in
the live-out set, so it is dead.

```text
0  str x1, [sp, #16]
1  lsl x2, x1, #3        <- was mov x9, #8 and mul x2, x1, x9
2  add x3, x0, x2
3  str x4, [x3, #0]
4  mov x5, x5
5  str x6, [x3, #8]
```

</div>
<div class="vx-step" markdown="1">

**Step 4. R4 at position 1**

The shift that step 3 created now sits next to the add that reads it. R4
moves the shift into the add's second operand and deletes the `lsl`, after
checking that `x2` is dead once the add has read it.

```text
0  str x1, [sp, #16]
1  add x3, x0, x1, lsl #3
2  str x4, [x3, #0]
3  mov x5, x5
4  str x6, [x3, #8]
```

</div>
<div class="vx-step" markdown="1">

**Step 5. R1 at position 3**

The copy from `x5` to `x5` goes. The two stores through `x3` are now
neighbours.

```text
0  str x1, [sp, #16]
1  add x3, x0, x1, lsl #3
2  str x4, [x3, #0]
3  str x6, [x3, #8]
```

</div>
<div class="vx-step" markdown="1">

**Step 6. R5 at position 2, then nothing**

The stores use the same base, their offsets are 0 and 8, and they store
8-byte registers, so `stp` covers both. A last pass over the three
instructions matches no rule, and the pass stops.

```text
0  str x1, [sp, #16]
1  add x3, x0, x1, lsl #3
2  stp x4, x6, [x3, #0]
```

</div>
</div>

Three of the six rewrites exist only because an earlier rewrite made them
possible. This is why the pass repeats. Restarting from the top after every
change is the simplest correct policy and is slow on long blocks; a
production pass keeps a **worklist** of positions near each edit and
revisits only those.

Either way, the loop must end. Each of the five rules
removes at least one instruction, so the count falls with every rewrite and
the fixpoint is reached after at most as many rewrites as there were
instructions. A rule that can undo another rule's work, such as "put the
constant on the left" beside "put the constant on the right", breaks that
argument, and the pass can cycle.

<figure class="vx-figure">
<svg viewBox="0 0 760 360" role="img" aria-label="The eight-instruction listing and the six rewrites that turn it into three instructions" aria-describedby="c7-cascade-desc">
<title>The eight-instruction listing and the six rewrites that turn it into three instructions</title>
<desc id="c7-cascade-desc">On the left, the eight instructions, numbered 0 to 7. In the middle, three groups of rewrites, each drawn next to the instructions it touches and numbered in the order they fire: 1, R2 turns the reload at line 1 into mov x1, x1, and 2, R1 deletes it; 3, R3 turns mov x9, #8 and mul into lsl x2, x1, #3, and 4, R4 folds that shift into the add; 5, R1 deletes mov x5, x5, and 6, R5 pairs the two stores that are now neighbours. On the right, the three instructions that remain: str x1, [sp, #16], add x3, x0, x1, lsl #3, and stp x4, x6, [x3, #0].</desc>
<text class="vx-text-accent" x="20" y="28">Before: 8 instructions</text>
<text class="vx-text-accent" x="244" y="28">Rewrites, in the order they fire</text>
<text class="vx-text-accent" x="552" y="28">After: 3</text>
<rect class="vx-box" x="20" y="44" width="200" height="26" rx="3"/>
<text class="vx-mono" x="30" y="62">str x1, [sp, #16]</text>
<rect class="vx-box" x="20" y="78" width="200" height="26" rx="3"/>
<text class="vx-mono" x="30" y="96">ldr x1, [sp, #16]</text>
<rect class="vx-box" x="20" y="112" width="200" height="26" rx="3"/>
<text class="vx-mono" x="30" y="130">mov x9, #8</text>
<rect class="vx-box" x="20" y="146" width="200" height="26" rx="3"/>
<text class="vx-mono" x="30" y="164">mul x2, x1, x9</text>
<rect class="vx-box" x="20" y="180" width="200" height="26" rx="3"/>
<text class="vx-mono" x="30" y="198">add x3, x0, x2</text>
<rect class="vx-box" x="20" y="214" width="200" height="26" rx="3"/>
<text class="vx-mono" x="30" y="232">str x4, [x3, #0]</text>
<rect class="vx-box" x="20" y="248" width="200" height="26" rx="3"/>
<text class="vx-mono" x="30" y="266">mov x5, x5</text>
<rect class="vx-box" x="20" y="282" width="200" height="26" rx="3"/>
<text class="vx-mono" x="30" y="300">str x6, [x3, #8]</text>
<line class="vx-line" x1="230" y1="46" x2="230" y2="102"/>
<line class="vx-line" x1="230" y1="114" x2="230" y2="204"/>
<line class="vx-line" x1="230" y1="216" x2="230" y2="306"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 6">
<text class="vx-text" x="244" y="66">1. R2: the reload becomes mov x1, x1</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 6">
<text class="vx-text" x="244" y="90">2. R1: the self-move goes</text>
<line class="vx-line" x1="505" y1="84" x2="540" y2="61"/>
<polygon class="vx-arrowhead" points="547,57 537,56 541,65"/>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 6">
<text class="vx-text" x="244" y="144">3. R3: mov and mul become lsl</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 6">
<text class="vx-text" x="244" y="168">4. R4: the shift folds into add</text>
<line class="vx-line" x1="505" y1="158" x2="540" y2="158"/>
<polygon class="vx-arrowhead" points="547,158 537,153 537,163"/>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 6">
<text class="vx-text" x="244" y="246">5. R1: mov x5, x5 goes</text>
</g>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 6">
<text class="vx-text" x="244" y="270">6. R5: neighbouring stores pair</text>
<line class="vx-line" x1="505" y1="262" x2="540" y2="262"/>
<polygon class="vx-arrowhead" points="547,262 537,257 537,267"/>
</g>
<rect class="vx-box-strong" x="552" y="44" width="204" height="26" rx="3"/>
<text class="vx-mono" x="562" y="62">str x1, [sp, #16]</text>
<rect class="vx-box-strong" x="552" y="146" width="204" height="26" rx="3"/>
<text class="vx-mono" x="562" y="164">add x3, x0, x1, lsl #3</text>
<rect class="vx-box-strong" x="552" y="250" width="204" height="26" rx="3"/>
<text class="vx-mono" x="562" y="268">stp x4, x6, [x3, #0]</text>
<text class="vx-text-muted" x="20" y="334">Each group has the same shape: the first rewrite creates the pattern the second one needs.</text>
</svg>
<figcaption>Figure 1. The six rewrites of the first run, grouped by the instructions they touch. In every group the second rewrite matches only because the first one changed the code: a reload became a self-move, a multiply became a shift next to its add, and a deleted copy made two stores neighbours.</figcaption>
</figure>

??? check "Suppose the pass did not restart after a rewrite but carried on from the position where the rewrite happened. Which of the six rewrites would it miss, and why?"

    The pairing in step 6. The copy `mov x5, x5` sat at position 3, between
    the two stores. When the scan reached position 3 and deleted it, the
    store at position 2 had already been checked against its old neighbour,
    the `mov`, and had matched nothing. Carrying on from position 3 never
    looks at the pair (2, 3) again. After a deletion, the instruction
    before the edit has a new neighbour, so at least that position has to be
    checked again.

## What a rule may not do

Now run the same code with one change to the world around it: some later
code reads `x2`, the value `i * 8`. The second run of the example does this
by putting `x2` in the live-out set. R1, R2, R3 and R5 fire as before. R4
does not: it would delete `lsl x2, x1, #3`, and the later reader of `x2`
would find garbage. The result keeps the shift and a plain add, four
instructions instead of three.

The window was identical in both runs. What changed is a fact about the
rest of the program, and that is the general point: a rule's pattern is
local, but whether firing it preserves the program's meaning often is not.
The condition that comes up most is **liveness**. A rule that deletes an
instruction, or stops writing a register, must know that the register (or
the flags, or the memory) is dead afterwards: no later instruction reads it
before something writes it again.

<figure class="vx-figure">
<svg viewBox="0 0 760 250" role="img" aria-label="The same two-instruction window with a register dead in one case and live in the other" aria-describedby="c7-side-desc">
<title>The same two-instruction window with a register dead in one case and live in the other</title>
<desc id="c7-side-desc">Two panels. Both start with the window lsl x2, x1, #3 followed by add x3, x0, x2. In the left panel nothing later reads x2, and rule R4 fires: the result is the single instruction add x3, x0, x1, lsl #3. In the right panel a later instruction, str x2, [sp, #24], reads x2; an arc from the lsl to that store is labelled x2 still live, and R4 is refused, so both instructions stay.</desc>
<text class="vx-text-accent" x="20" y="28">Nothing later reads x2</text>
<rect class="vx-box-accent" x="20" y="44" width="220" height="26" rx="3"/>
<text class="vx-mono" x="30" y="62">lsl x2, x1, #3</text>
<rect class="vx-box-accent" x="20" y="76" width="220" height="26" rx="3"/>
<text class="vx-mono" x="30" y="94">add x3, x0, x2</text>
<rect class="vx-box" x="20" y="108" width="220" height="26" rx="3"/>
<text class="vx-text-muted" x="30" y="126">(no read of x2)</text>
<line class="vx-line" x1="130" y1="140" x2="130" y2="172"/>
<polygon class="vx-arrowhead" points="124,170 130,180 136,170"/>
<text class="vx-text" x="146" y="164">R4 fires</text>
<rect class="vx-box-strong" x="20" y="184" width="220" height="30" rx="3"/>
<text class="vx-mono" x="30" y="204">add x3, x0, x1, lsl #3</text>
<line class="vx-line" x1="380" y1="20" x2="380" y2="230"/>
<text class="vx-text-accent" x="410" y="28">A later store reads x2</text>
<rect class="vx-box-accent" x="410" y="44" width="220" height="26" rx="3"/>
<text class="vx-mono" x="420" y="62">lsl x2, x1, #3</text>
<rect class="vx-box-accent" x="410" y="76" width="220" height="26" rx="3"/>
<text class="vx-mono" x="420" y="94">add x3, x0, x2</text>
<rect class="vx-box" x="410" y="108" width="220" height="26" rx="3"/>
<text class="vx-mono" x="420" y="126">str x2, [sp, #24]</text>
<path class="vx-line" d="M 630 57 C 675 57, 675 121, 636 121" fill="none"/>
<polygon class="vx-arrowhead" points="636,121 646,116 646,126"/>
<text class="vx-text" x="682" y="86">x2 still</text>
<text class="vx-text" x="682" y="102">live</text>
<line class="vx-line" x1="520" y1="140" x2="520" y2="172"/>
<polygon class="vx-arrowhead" points="514,170 520,180 526,170"/>
<text class="vx-text" x="536" y="164">R4 refused</text>
<rect class="vx-box-bad" x="410" y="184" width="220" height="30" rx="3"/>
<text class="vx-text" x="420" y="204">both instructions stay</text>
</svg>
<figcaption>Figure 2. One window, two answers. The pattern R4 matches is the same on both sides; the difference is a read of <code>x2</code> outside the window, which only a liveness check can see.</figcaption>
</figure>

Flags are registers too, and they are the easiest to forget, because the
instruction that reads them can be far from the one that sets them. The
next example asks the liveness question for a compare. In program A, a
branch reads the flags the `cmp` sets; in program B nothing does:

--8<-- "includes/examples/backend/c7-peephole/liveness_side_condition.cpp.md"

The same `cmp` is unsafe to delete in one program and safe in the other.
This matters for a rule such as "`cmp x0, #0` then `b.eq L` becomes
`cbz x0, L`". `cbz` branches when a register is zero and leaves the flags
unchanged[^a64-cbz], so the rewrite deletes the only instruction that set
them. It is correct only if no instruction after the branch, on either
path, reads those flags before something sets them again. That question
crosses a block boundary, which a two-instruction window cannot answer on
its own: the pass needs the liveness sets that
[C2](c2-liveness.md#loops-the-same-equations-repeated) computes for every
block.

Bansal and Aiken made the same point precise for automatically found rules.
Two instruction sequences are only equivalent under a **context**, the set
of registers live when they end; their system records that set with every
sequence and checks equivalence against it. For memory, they conservatively
treat every location as live[^bansal-aiken], which is the choice this
chapter's example made for the spill slot.

??? check "In the second run, `x2` was live at the end and R4 was refused. R3 still deleted `mov x9, #8`. Why was that deletion allowed?"

    R3's side condition is about `x9`, the register whose write it removes.
    After the `mul`, nothing reads `x9` and it is not live at the end, so
    removing the instruction that set it changes nothing anyone can observe.
    R4's condition is about `x2`, and in the second run `x2` is read later.
    Each rule asks only about the registers it stops writing.

## A catalogue of rule shapes

Hand-written peephole optimizers hold hundreds of rules[^bansal-aiken], but
most rules fall into a few shapes. The shape tells you which side condition to look for.

**Delete dead work.** A self-move, a store to a slot that is stored again
before any load, an instruction whose destination is dead. The self-move is
the only one that needs no check at all; the others need liveness for
registers or for memory.

**Forward a store to a load.** A load from an address that the previous
store wrote reads the stored register, so it can become a move (R2). This
is the rule that pays most on spill-everything code from
[B1](b1-simplest-backend.md#one-template-per-operation), where every
template ends with a store that the next template loads back. Across a gap
of several instructions it needs more care: nothing between the two may
write the stored register, change the base register, or store to memory
that might overlap the slot.

**Fold a constant.** R3 is a machine-level constant fold: once the pass
knows `x9` holds 8, the multiply by `x9` is a multiply by 8, and then a
shift. LLVM's AArch64 pass `aarch64-mi-peephole-opt` does related work in
the other direction: it takes a constant that needs its own `mov` before an
`and`, `add` or `sub` and, where possible, splits it into two immediates
that those instructions can encode directly[^llvm-mi-peephole].

**Fold an operation into an operand.** R4 folds a shift into an `add`
operand. The same shape folds an `add` of a small constant into a load's
address, or an index computation into `[base, index, lsl #2]`. These are
exactly the tiles [C1](c1-instruction-selection.md) would have chosen if
the shift and its reader had been in one tree; the peephole pass catches
them when they were not.

**Merge a flag-setting pair.** LLVM's generic machine peephole pass removes
a `cmp` when an earlier `sub` already sets, or can be changed to set, the
flags the branch needs[^llvm-peephole]. The rule is sound only if nothing
between the two instructions changes the flags and the `sub`'s own flags
were not needed for something else.

**Pair loads and stores, and fold pointer updates.** Two loads or stores of
the same size to neighbouring addresses from one base become `ldp` or
`stp`[^arm-pair]. An `add` that advances the base register after a load
becomes the load's post-index form, `[x0], #8`[^arm-addr]. Both are the
subject of a section below.

**Strength-reduce.** Replace an operation with a cheaper one that computes
the same value: a multiply by $2^n$ becomes a shift, a multiply by 10
becomes a shift and an add. The same idea runs over loops at the IR level
in [O8](../optimize/o8-loops.md#strength-reduction-and-the-exit-test); in a
back end it runs again because constants often appear only after
selection. Strength reduction is where rules most often look right and are
wrong, and division shows why.

### A rule that is right for half its inputs

"Divide by two is a shift right by one" is true for unsigned values. For
signed values it is not. C++ integer division truncates toward zero, while
a right shift of a negative value rounds toward minus infinity (since
C++20 the shift is defined to be arithmetic)[^cpp-arith]. For `x = -3`,
`x / 2` is `-1` and `x >> 1` is `-2`. [O12](../optimize/o12-testing-optimizers.md)
uses this bug to motivate differential testing. With 8-bit values
there are only 256 inputs, so a rule can be checked on all of them:

--8<-- "includes/examples/backend/c7-peephole/signed_divide_rule.cpp.md"

The plain shift is wrong on 64 inputs, every negative odd value. It is
right when a side condition holds, `x >= 0`, which a compiler may know from
a range fact. The third candidate adds the sign bit before shifting, which
moves negative odd values up by one so that the shift's rounding matches
truncation, and it agrees on all 256 inputs. Apple clang 21 at `-O2`
compiles `int d(int x) { return x / 2; }` to exactly that shape
(checked on an Apple M4 Pro, 24 September 2026):

```gas
_d:
	add	w8, w0, w0, lsr #31
	asr	w0, w8, #1
	ret
```

`w0, lsr #31` is the sign bit of `x` as 0 or 1, added to `x` by one `add`
with a shifted operand, the same kind of operand R4 creates.

??? check "Is it correct to replace `sdiv w0, w0, w1` with `asr w0, w0, #2` when `w1` holds 4? If not, what would make it correct?"

    No, for the same reason as division by 2: for negative values that are
    not multiples of 4, the arithmetic shift rounds toward minus infinity and
    the division toward zero. For `x = -5`, `x / 4` is `-1`, `x >> 2` is `-2`.
    It becomes correct either with a side condition that `x` is not
    negative, or by adding 3 to negative values before the shift (in
    general $2^n - 1$, which is what the sign-bit trick does for $n = 1$).
    It also needs the usual one: `w1` must be dead afterwards if the rule
    deletes the instruction that loaded 4 into it.

## Pairs and post-index in a real back end

LLVM's AArch64 back end does the last two rule shapes in one machine pass,
`aarch64-ldst-opt`. Its source describes it as a pass of load and store
peephole optimizations that runs after register allocation, and its
statistics count pairs created, post-index and pre-index updates folded,
and loads replaced by the value of an earlier store[^llvm-ldst]. Here is
IR for two small functions, three `f32` stores to consecutive addresses and
a loop that reads two neighbouring values per trip:

--8<-- "includes/examples/backend/c7-peephole/three_stores.ll.md"

`llc` can stop the pipeline on either side of one pass, which turns it into
a before-and-after experiment. With `llc` 18.1.8 on the owner's Mac
(24 September 2026), `llc -O2 -mtriple=aarch64-apple-macos
-stop-before=aarch64-ldst-opt` and `-stop-after=aarch64-ldst-opt` show the
machine instructions around the pass. Written in assembly spelling, the
relevant lines are:

```text
                 before aarch64-ldst-opt        after aarch64-ldst-opt
store3           str s0, [x0]                   stp s0, s1, [x0]
                 str s1, [x0, #4]               str s2, [x0, #8]
                 str s2, [x0, #8]

sum_pairs loop   ldr s1, [x0]                   ldp s1, s2, [x0], #8
                 ldr s2, [x0, #4]               fadd s0, s0, s1
                 fadd s0, s0, s1                fadd s0, s0, s2
                 fadd s0, s0, s2                subs x1, x1, #1
                 subs x1, x1, #1                b.ne LBB1_1
                 add x0, x0, #8
                 b.ne LBB1_1
```

Three things in that output are worth reading slowly. The pass paired the
first two stores and left the third alone: `stp` takes exactly two
registers. In the loop it did two rewrites on the same load, pairing the
two `ldr` instructions and then folding the `add x0, x0, #8` into the pair
as a post-index update. And that `add` was not adjacent to the loads: two
`fadd` and a `subs` sat between them. The pass's window is not "two
neighbours" but a bounded scan, by default up to 20 instructions for a
matching load or store, and up to 100 for a base-register update, with
checks along the way[^llvm-ldst].

Those checks are the side conditions for pairing. As the source shows, the
scan tracks which registers the instructions in between modify and use, so
it cannot pair across a write to the base register; it stops at a store
that may alias the memory being loaded; and it refuses instructions with
ordered memory references such as volatile or atomic accesses[^llvm-ldst].
The encoding adds one more: the offset of an `stp` of `s` registers must be
a multiple of 4 between -256 and 252, and for `d` or `x` registers a
multiple of 8 between -512 and 504. Apple clang 21's assembler rejects
`stp s0, s1, [x0, #256]` with exactly that message (checked 24 September
2026); R5 in the example encodes the 8-byte range.

The same experiment places the peephole passes in the pipeline. With
`-debug-pass=Structure`, `llc` 18.1.8 at `-O2` for AArch64 lists
`peephole-opt` and `aarch64-mi-peephole-opt` before register allocation,
and `aarch64-ldst-opt` after it, after the prologue and epilogue are
inserted:

<figure class="vx-figure">
<svg viewBox="0 0 760 350" role="img" aria-label="Where LLVM's AArch64 peephole passes run in the -O2 machine pipeline" aria-describedby="c7-pipe-desc">
<title>Where LLVM's AArch64 peephole passes run in the -O2 machine pipeline</title>
<desc id="c7-pipe-desc">A vertical list of seven machine passes in the order llc 18.1.8 runs them at -O2 for AArch64, with other passes between them omitted. aarch64-isel, instruction selection. peephole-opt, highlighted, generic machine peephole rules such as removing a cmp after a flag-setting sub. aarch64-mi-peephole-opt, highlighted, AArch64 rules on virtual registers. Then a dividing line marked register allocation, with greedy below it. machine-cp, copy propagation. prologepilog, which inserts the prologue and epilogue. aarch64-ldst-opt, highlighted, which pairs loads and stores and folds base-register updates into pre- and post-index forms on physical registers.</desc>
<text class="vx-text-accent" x="20" y="26">Selected machine passes, in the order llc -O2 runs them (AArch64, LLVM 18.1.8)</text>
<rect class="vx-box" x="20" y="42" width="250" height="28" rx="3"/>
<text class="vx-mono" x="32" y="61">aarch64-isel</text>
<text class="vx-text-muted" x="290" y="61">instruction selection</text>
<rect class="vx-box-accent" x="20" y="80" width="250" height="28" rx="3"/>
<text class="vx-mono" x="32" y="99">peephole-opt</text>
<text class="vx-text" x="290" y="99">generic rules, such as a cmp after a flag-setting sub</text>
<rect class="vx-box-accent" x="20" y="118" width="250" height="28" rx="3"/>
<text class="vx-mono" x="32" y="137">aarch64-mi-peephole-opt</text>
<text class="vx-text" x="290" y="137">AArch64 rules, such as splitting a constant</text>
<line class="vx-line" x1="20" y1="162" x2="740" y2="162"/>
<rect class="vx-box" x="20" y="190" width="250" height="28" rx="3"/>
<text class="vx-mono" x="32" y="209">greedy</text>
<text class="vx-text-muted" x="290" y="209">register allocation: virtual registers above, physical below</text>
<rect class="vx-box" x="20" y="226" width="250" height="28" rx="3"/>
<text class="vx-mono" x="32" y="245">machine-cp</text>
<text class="vx-text-muted" x="290" y="245">copy propagation</text>
<rect class="vx-box" x="20" y="264" width="250" height="28" rx="3"/>
<text class="vx-mono" x="32" y="283">prologepilog</text>
<text class="vx-text-muted" x="290" y="283">prologue and epilogue inserted</text>
<rect class="vx-box-accent" x="20" y="302" width="250" height="28" rx="3"/>
<text class="vx-mono" x="32" y="321">aarch64-ldst-opt</text>
<text class="vx-text" x="290" y="321">pairs, pre- and post-index folding</text>
<line class="vx-line" x1="10" y1="56" x2="10" y2="316"/>
<polygon class="vx-arrowhead" points="4,312 10,322 16,312"/>
</svg>
<figcaption>Figure 3. Seven of the machine passes in LLVM's AArch64 <code>-O2</code> pipeline, in order, with the peephole passes highlighted. Rules that run before allocation see virtual registers, where a value has one definition and its uses can be counted; rules that run after allocation work on physical registers and must track which ones are live.</figcaption>
</figure>

There is a reason to run peephole rules on both sides of allocation. Before
it, registers are virtual and often in SSA form, so "is this value used
anywhere else?" is a use count. After it, the code is final: the spill
code, the copies the allocator inserted and the prologue's stores exist
only now, so only a late pass can pair or forward them. The price is that
a late pass must track the liveness of physical registers itself.

??? check "Finish this window by hand, with 8-byte `d` registers. Which of the rules apply, in what order, and what is the result? `str d0, [x1, #8]`, `str d1, [x1, #16]`, `cmp x7, #0`, `b.eq 1f`"

    The two stores pair: same base, offsets 8 apart for 8-byte registers,
    and 8 is a multiple of 8 inside -512 to 504, giving
    `stp d0, d1, [x1, #8]`. A rule that had hard-coded "4 bytes apart" from
    `s` registers would have missed it, and one that ignored the size
    entirely could pair `s` stores 8 bytes apart, which `stp` cannot express.
    The `cmp` and `b.eq` become `cbz x7, 1f` only if the flags are dead
    after the branch on both paths, which the window cannot see; without
    block liveness the pass must leave them.

## Where rules come from

Every rule so far was written by a person who looked at compiler output and
noticed a pattern. That is how peephole optimizers began, and in 2006 Bansal
and Aiken could still describe the rules of real peephole optimizers as
hand-written by experts in the target machine[^bansal-aiken]. Two
developments changed how rule sets are written and checked.

### Rules as data

When rules multiply, writing each one as matching code by hand becomes the
bottleneck, so compilers write them in a small **rule language** and
generate the matcher. Go's compiler keeps its rewrite rules in
`_gen/*.rules` files, in their own syntax, and `go generate` turns them
into Go code; the README notes that simple optimizations are quick to write
this way and complex ones do not fit[^go-ssa].
[D3](d3-real-backends.md#rules-and-the-driver-that-applies-them) reads
those rules and the fixpoint driver that applies them. Cranelift's ISLE
("instruction selection/lowering expressions") is typed, compiles to Rust
pattern-matching code, and is used both for lowering on four targets and
for machine-independent rewrites; it has a priority mechanism and an
overlap checker for rules that could match the same input[^isle].

A rule language also makes rules checkable. Alive is a language for
peephole optimizations on LLVM IR in which each rule has a pattern, a
replacement and an optional precondition; the tool either proves the rule
correct with an SMT solver or produces a counterexample, and generates C++
for LLVM. Its authors translated more than 300 of LLVM's InstCombine
optimizations into Alive and found eight of them wrong[^alive15].
[O11](../optimize/o11-undefined-behavior.md#checking-a-rewrite-instead-of-trusting-it)
follows that line of work to Alive2.

### Rules found by search

A **superoptimizer** turns the process around: instead of a person
noticing a pattern, a program searches for the shortest instruction
sequence that computes a given function. Massalin's original enumerated
all programs of length 1, then 2, and so on, over a chosen subset of the
68020's instructions[^massalin]. Proving each candidate equivalent with
boolean formulas was too slow, so the key idea was a quick probabilistic
test: run the candidate on a few chosen inputs and reject it at the first
wrong answer. Programs that survived were checked by hand[^massalin].

The toy below does the same on a machine small enough to test every input:
three 8-bit registers, and only shifts by 1 to 3 and adds. It looks for a
program that computes `x * 10`:

--8<-- "includes/examples/backend/c7-peephole/superoptimize_toy.cpp.md"

No program of one or two instructions works; the first three-instruction
program in the enumeration order does, and because the test covered all
256 inputs it is proved for 8-bit registers. It is not the decomposition a
person might write first (`x * 8 + x * 2`), and it is not what a real
compiler emits either. Apple clang 21 at `-O2` compiles `x * 10` for
AArch64 as `add w8, w0, w0, lsl #2` then `lsl w0, w8, #1`: two
instructions, because AArch64's `add` can shift one operand, which the toy
machine cannot. The search space is the instruction set; Massalin made the
same observation, that constraining the instructions and watching the
output teaches something about the instruction set itself[^massalin].

Bansal and Aiken turned superoptimization into a way to build a peephole
optimizer. Their system **harvests** instruction sequences from compiled
programs, enumerates candidate replacements, and filters them cheaply by
running both on a small set of test vectors (18 in their experiments). A
candidate that survives is checked exactly, by encoding both sequences as a
boolean formula for a SAT solver, taking the live registers and possible
memory aliasing into account. The results go into a database of rules that
a peephole pass applies[^bansal-aiken]. Unlike Massalin, they found
sequences that passed the tests and failed the exact check, often because
bits were lost along the way or because test addresses almost never
alias[^bansal-aiken].

<figure class="vx-figure">
<svg viewBox="0 0 760 290" role="img" aria-label="Massalin's superoptimizer and Bansal and Aiken's peephole superoptimizer as two pipelines" aria-describedby="c7-super-desc">
<title>Massalin's superoptimizer and Bansal and Aiken's peephole superoptimizer as two pipelines</title>
<desc id="c7-super-desc">Two rows of five boxes joined by arrows. Top row, Massalin 1987: one function to improve; enumerate all programs of length 1, then 2, and so on; run each on a few chosen inputs; check the survivors by hand; the shortest program found. Bottom row, Bansal and Aiken 2006: harvest sequences from compiled binaries; enumerate candidates; fingerprint and test on test vectors; prove equivalence with a SAT solver, given the live registers; store the results as rules for a peephole pass. The cheap test sits before the expensive check in both rows.</desc>
<text class="vx-text-accent" x="20" y="30">Massalin, 1987: one function, the shortest program</text>
<rect class="vx-box" x="20" y="44" width="136" height="74" rx="4"/>
<text class="vx-text" x="28" y="72">one function</text>
<text class="vx-text" x="28" y="90">to improve</text>
<rect class="vx-box" x="166" y="44" width="136" height="74" rx="4"/>
<text class="vx-text" x="174" y="66">enumerate</text>
<text class="vx-text" x="174" y="84">programs, length</text>
<text class="vx-text" x="174" y="102">1, then 2, ...</text>
<rect class="vx-box-accent" x="312" y="44" width="136" height="74" rx="4"/>
<text class="vx-text" x="320" y="72">run on a few</text>
<text class="vx-text" x="320" y="90">chosen inputs</text>
<rect class="vx-box" x="458" y="44" width="136" height="74" rx="4"/>
<text class="vx-text" x="466" y="72">check the</text>
<text class="vx-text" x="466" y="90">survivors by hand</text>
<rect class="vx-box-strong" x="604" y="44" width="136" height="74" rx="4"/>
<text class="vx-text" x="612" y="72">shortest</text>
<text class="vx-text" x="612" y="90">program found</text>
<line class="vx-line" x1="156" y1="81" x2="161" y2="81"/>
<polygon class="vx-arrowhead" points="166,81 159,77 159,85"/>
<line class="vx-line" x1="302" y1="81" x2="307" y2="81"/>
<polygon class="vx-arrowhead" points="312,81 305,77 305,85"/>
<line class="vx-line" x1="448" y1="81" x2="453" y2="81"/>
<polygon class="vx-arrowhead" points="458,81 451,77 451,85"/>
<line class="vx-line" x1="594" y1="81" x2="599" y2="81"/>
<polygon class="vx-arrowhead" points="604,81 597,77 597,85"/>
<text class="vx-text-accent" x="20" y="170">Bansal and Aiken, 2006: many sequences, a rule database</text>
<rect class="vx-box" x="20" y="184" width="136" height="74" rx="4"/>
<text class="vx-text" x="28" y="212">harvest sequences</text>
<text class="vx-text" x="28" y="230">from binaries</text>
<rect class="vx-box" x="166" y="184" width="136" height="74" rx="4"/>
<text class="vx-text" x="174" y="212">enumerate</text>
<text class="vx-text" x="174" y="230">candidates</text>
<rect class="vx-box-accent" x="312" y="184" width="136" height="74" rx="4"/>
<text class="vx-text" x="320" y="212">fingerprint and</text>
<text class="vx-text" x="320" y="230">test on vectors</text>
<rect class="vx-box-accent" x="458" y="184" width="136" height="74" rx="4"/>
<text class="vx-text" x="466" y="206">prove with a SAT</text>
<text class="vx-text" x="466" y="224">solver, given the</text>
<text class="vx-text" x="466" y="242">live registers</text>
<rect class="vx-box-strong" x="604" y="184" width="136" height="74" rx="4"/>
<text class="vx-text" x="612" y="206">store as rules</text>
<text class="vx-text" x="612" y="224">for a peephole</text>
<text class="vx-text" x="612" y="242">pass</text>
<line class="vx-line" x1="156" y1="221" x2="161" y2="221"/>
<polygon class="vx-arrowhead" points="166,221 159,217 159,225"/>
<line class="vx-line" x1="302" y1="221" x2="307" y2="221"/>
<polygon class="vx-arrowhead" points="312,221 305,217 305,225"/>
<line class="vx-line" x1="448" y1="221" x2="453" y2="221"/>
<polygon class="vx-arrowhead" points="458,221 451,217 451,225"/>
<line class="vx-line" x1="594" y1="221" x2="599" y2="221"/>
<polygon class="vx-arrowhead" points="604,221 597,217 597,225"/>
<text class="vx-text-muted" x="20" y="282">Both put a cheap test before the expensive check; only the second makes the check exact and keeps the results.</text>
</svg>
<figcaption>Figure 4. Two ways to find rewrites by search. Massalin's superoptimizer improves one function and relies on tests plus a human check. Bansal and Aiken's system works offline over many harvested sequences, proves each result with a solver under the live registers, and produces rules for an ordinary peephole pass.</figcaption>
</figure>

The difference between the two rows is the difference between "correct on
every input I tried" and "correct". A test finds most wrong candidates
quickly, and exhaustive testing is a proof when the input space is as small
as 256 values; for 64-bit registers and memory it is not, and the
signed-division rule above is the kind of mistake that survives a few
tests. Whatever produced a rule, a person or a search, the rule is only as
good as the check behind it.

## For Vortex

The code a first Vortex back end emits is the code this chapter started
from. [B1](b1-simplest-backend.md)'s templates store every result and load
it back; a linear-scan allocator leaves copies and spill code; and the
[stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for)
kernel repeats its inner loop once per multiply-add, so every wasted
instruction in that loop is paid on every trip. Because Vortex arrays have
fixed shapes and are stored row-major and contiguous
([record 43](../decisions/arrays.md#d43)), neighbouring elements sit at
offsets the compiler knows, which is what a pairing rule needs to prove two
accesses adjacent.

Three Vortex rules limit what a rule may do. Floating-point operations may
not be contracted, reassociated or reordered
([record 56](../decisions/numbers.md#d56)), so a rule that turns `fmul`
then `fadd` into one `fmadd` is out, although it has the shape of an
ordinary combine: with `a = 1 + 2^-12` and `c = -(1 + 2^-11)` in `f32`,
`fma(a, a, c)` is $2^{-24}$ while the separate multiply and add give 0
(Apple clang 21, `-ffp-contract=off`, checked 24 September 2026;
[A3](a3-floats-and-vectors.md#fused-multiply-add) explains why). Runtime
checks are observable behaviour ([O1](../optimize/o1-optimizer-contract.md#what-an-optimizer-must-keep)),
so no rule may delete the compare of a bounds or overflow check unless
another instruction already sets the same flags for the same branch. And a
store through a `&mut` parameter writes the caller's array
([record 40](../decisions/references.md#d40)), so a store that looks dead
inside the function is not.

!!! vortex "Exercise"

    **Build a peephole pass for your native back end.** It runs after
    register allocation, over one basic block at a time, and applies these
    rules to a fixpoint:

    1. delete a move whose source and destination are the same register;
    2. turn a load from a stack slot that the previous instruction stored
       into a register move;
    3. turn a multiply by a power of two held in a register into a shift,
       when that register is dead afterwards;
    4. pair two loads or two stores of the same size through the same base
       register at adjacent offsets into `ldp` or `stp`, when the offset
       fits the encoding;
    5. turn `cmp xN, #0` followed by `b.eq` or `b.ne` into `cbz` or `cbnz`,
       when the flags are dead after the branch on both paths.

    Take the liveness facts from your [C2](c2-liveness.md) analysis: the
    registers live out of each block, walked backwards to the window. Treat
    every memory location as live.

    **Not yet.** No rule language, no superoptimizer, no rules that cross
    block boundaries, no reordering of instructions, and no post-index
    folding (try it as a stretch goal once the five rules pass). No rule
    that fuses, reassociates or reorders `f32` or `f64` operations
    ([record 56](../decisions/numbers.md#d56)).

    **Done when** all of the following hold:

    - Each rule has two tests in the style of [E4](e4-testing-backends.md):
      one input where it fires, and one where its side condition blocks it
      (a live register, live flags, an offset out of range, a mix of `s`
      and `d` stores).
    - The stage 10 program prints byte-identical output with the pass on
      and off, and so does every program in your test suite.
    - A counter in the pass reports how many times each rule fired while
      compiling the stage 10 program. Record the static instruction count of
      `multiply` before and after in a table like the one below.
    - Three deliberate, temporary breakages each make a test fail: drop the
      dead-register check from rule 3, let rule 4 pair stores 4 bytes apart
      for 8-byte registers, and let rule 5 fire when the flags are live.

    | Program | Instructions in `multiply`, pass off | Pass on | Rule firings (1 to 5) |
    | --- | --- | --- | --- |
    | stage 10, `-O0` path | | | |
    | stage 10, your optimizing path | | | |

    If you also want a timing, run each binary at least ten times on an
    idle machine, report the median with the machine and date, and keep it
    separate from the static counts, which are the deterministic result.

## Key ideas

!!! recap "You can now answer"

    - **What is a peephole optimizer?** A pass that slides a small window over the instructions and replaces a matching pattern with a cheaper equivalent, repeating until no rule matches.
    - **Why run it to a fixpoint?** One rewrite can create the pattern for another: a forwarded load becomes a self-move, a deleted copy makes two stores neighbours.
    - **What does a side condition check that the pattern cannot?** Facts outside the window, usually that a register, the flags or memory the rewrite stops writing is dead afterwards.
    - **Why is `x / 2` to `x >> 1` wrong for signed integers?** Division truncates toward zero and the arithmetic shift rounds toward minus infinity; the fix adds the sign bit first.
    - **What does LLVM's `aarch64-ldst-opt` do, and when?** After register allocation, it pairs loads and stores into `ldp` and `stp` and folds base-register updates into pre- and post-index forms, scanning a bounded number of instructions rather than only neighbours.
    - **How does a peephole superoptimizer find rules?** It enumerates candidate sequences, filters them with tests, proves the survivors equivalent under the live registers, and stores them as rules.
    - **Which rule shapes are closed to Vortex today?** Any rule that contracts, reassociates or reorders floating-point operations, and any that removes the compare of a runtime check.

## Where this comes back

!!! next "You will use this again in"

    - [D3. Reading real back ends](d3-real-backends.md): *Go's rule files and their fixpoint driver*, *ISLE*
    - [E1. The LLVM code generator pipeline](e1-llvm-codegen-pipeline.md): *combines before selection*, *where contraction is decided*
    - [E4. Testing back ends](e4-testing-backends.md): *a MIR test of `aarch64-ldst-opt`*, *positive and negative tests for a rule*
    - [O11. Undefined behavior, poison and correct optimization](../optimize/o11-undefined-behavior.md): *proving a rewrite with a solver*, *preconditions*
    - [O12. Testing an optimizer](../optimize/o12-testing-optimizers.md): *differential testing of a rewrite*, *the signed-division bug*

## Sources and further reading

Read Davidson and Fraser's short paper first: it states the goal of a
peephole pass and its relation to a naive code generator better than any
later summary. Then Massalin's five pages, for the search idea, and Bansal
and Aiken for how search becomes a rule database. The header comment and
statistics of `AArch64LoadStoreOptimizer.cpp` are the quickest way into a
production pass; read them with `llc -stop-before` and `-stop-after` open
beside them, as this chapter did.

[^mckeeman]: William M. McKeeman, "Peephole Optimization", *Communications of the ACM* 8(7), 443-444, July 1965: the abstract (redundant instructions discarded in the final stage of compilation). <https://doi.org/10.1145/364995.365000>
[^davidson-fraser]: Jack W. Davidson and Christopher W. Fraser, "The Design and Application of a Retargetable Peephole Optimizer", *ACM Transactions on Programming Languages and Systems* 2(2), 1980: the abstract (PO simulates pairs of adjacent instructions and replaces them with a cheaper single instruction; naive code generators plus PO give good code). <https://doi.org/10.1145/357094.357098>
[^massalin]: Henry Massalin, "Superoptimizer: A Look at the Smallest Program", *Proceedings of ASPLOS II*, 1987: the abstract and sections 3.1 (boolean test) and 3.2 (probabilistic test). <https://doi.org/10.1145/36206.36194> (copy: <https://web.stanford.edu/class/cs343/resources/superoptimizer.pdf>)
[^bansal-aiken]: Sorav Bansal and Alex Aiken, "Automatic Generation of Peephole Superoptimizers", *Proceedings of ASPLOS XII*, 2006: sections 1 and 2 (harvester, enumerator, optimization database; equivalence under the set of live registers; memory assumed live), 3 (harvesting and fingerprinting) and 5.1 (execution test on 18 test vectors, boolean test with a SAT solver). <https://doi.org/10.1145/1168857.1168906> (copy: <https://theory.stanford.edu/~aiken/publications/papers/asplos06.pdf>)
[^alive15]: Nuno P. Lopes, David Menendez, Santosh Nagarakatte and John Regehr, "Provably Correct Peephole Optimizations with Alive", *Proceedings of PLDI*, 2015: the abstract and sections 1 and 2 (preconditions; more than 300 InstCombine optimizations translated, eight found wrong). <https://doi.org/10.1145/2737924.2737965> (copy: <https://users.cs.utah.edu/~regehr/papers/pldi15.pdf>)
[^go-ssa]: The Go Authors, "Introduction to the Go compiler's SSA backend", `src/cmd/compile/internal/ssa/README.md`, section "Hacking on SSA". <https://github.com/golang/go/blob/master/src/cmd/compile/internal/ssa/README.md>
[^isle]: Chris Fallin, "Cranelift's Instruction Selector DSL, ISLE: Term-Rewriting Made Practical", 20 January 2023. <https://cfallin.org/blog/2023/01/20/cranelift-isle/>
[^llvm-ldst]: LLVM Project, `llvm/lib/Target/AArch64/AArch64LoadStoreOptimizer.cpp`, release/18.x: the header comment (load/store peephole optimizations, run after register allocation and after prologue/epilogue insertion), the statistics, the options `aarch64-load-store-scan-limit` (default 20) and `aarch64-update-scan-limit` (default 100), and the checks for modified and used registers, aliasing stores and ordered memory references. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Target/AArch64/AArch64LoadStoreOptimizer.cpp>
[^llvm-peephole]: LLVM Project, `llvm/lib/CodeGen/PeepholeOptimizer.cpp`, release/18.x, header comment, "Optimize Comparisons". <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/CodeGen/PeepholeOptimizer.cpp>
[^llvm-mi-peephole]: LLVM Project, `llvm/lib/Target/AArch64/AArch64MIPeepholeOpt.cpp`, release/18.x, header comment, rules 1 to 3. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Target/AArch64/AArch64MIPeepholeOpt.cpp>
[^arm-pair]: Arm, A64 Instruction Set Architecture Guide (102374, version 1.3), "Loads and stores - load pair and store pair". <https://developer.arm.com/documentation/102374/0103/Loads-and-stores---load-pair-and-store-pair>
[^arm-addr]: Arm, A64 Instruction Set Architecture Guide (102374, version 1.3), "Loads and stores - addressing". <https://developer.arm.com/documentation/102374/0103/Loads-and-stores---addressing>
[^a64-cbz]: Arm, "Arm A-profile A64 Instruction Set Architecture" (DDI 0602, 2026-06), "CBZ". <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/CBZ--Compare-and-branch-on-zero->
[^cpp-arith]: cppreference.com, "Arithmetic operators": built-in multiplicative operators (the quotient is truncated toward zero) and bitwise shift operators (since C++20, right shift of a signed value is arithmetic). <https://en.cppreference.com/w/cpp/language/operator_arithmetic>
