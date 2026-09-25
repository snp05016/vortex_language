# E4. Testing back ends

<p class="page-intro">This chapter teaches the tests that keep a back end correct while it changes: FileCheck tests on assembly, MIR tests that run one pass, encoding tests that pin instructions to bytes, checkers that verify a pass's result, and differential runs that compare whole programs. Vortex will have two or more paths to machine code, and each of these tests guards a different place where they can go wrong.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 40 minutes · Builds on: [E1. The LLVM code generator pipeline](e1-llvm-codegen-pipeline.md)</p>

???+ remember "Before you start, remember"

    ??? question "What do `-stop-after=<pass>` and `-run-pass=<pass>` make `llc` do?"

        `-stop-after` writes the machine function as a `.mir` text file
        right after the named pass. `-run-pass` reads such a file back and
        runs only the named pass on it, so one pass can be studied with
        nothing before or after it.

        Introduced in [E1. The LLVM code generator pipeline](e1-llvm-codegen-pipeline.md#machineinstr-and-mir-a-shared-language-mid-pipeline).

    ??? question "Two back ends print different output for the same program. What do you know, and what would agreement not have told you?"

        At least one of them is wrong on that input, though not which one.
        Agreement proves nothing on its own: two paths built from the same
        misreading of a specification can agree and both be wrong.

        Introduced in [B1. The simplest back end that works](b1-simplest-backend.md#two-back-ends-one-oracle).

    ??? question "Which AArch64 condition codes compare as unsigned numbers?"

        `lo`, `ls`, `hi` and `hs`. A bounds check uses them, because a
        negative index read as unsigned is larger than any length.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md#flags-and-conditions).

    ??? question "What must a test-case reducer be able to decide without a person?"

        Whether a smaller candidate is still "interesting", that is, still
        shows the same failure. The reducer asks this thousands of times, so
        a script must answer it.

        Introduced in [O12. Testing an optimizer](../optimize/o12-testing-optimizers.md#test-case-reduction-from-a-page-to-a-sentence).

    ??? question "What does the allocation checker in C3 look for?"

        Two live intervals that were given the same register and overlap.
        It checks the allocator's answer with a second, much simpler
        program instead of trusting the allocator.

        Introduced in [C3. Register allocation I: linear scan](c3-linear-scan.md#checking-an-allocation-independently).

!!! goals "In this chapter"

    - Read and write a FileCheck test for `llc`, and explain what each directive (`CHECK`, `CHECK-LABEL`, `CHECK-NEXT`, `CHECK-NOT`) promises.
    - Decide how strict a check line should be, so that the test fails on a wrong compiler and survives a correct change.
    - Test one machine pass in isolation with a MIR test, and one instruction's bytes with an `llvm-mc` encoding test.
    - Explain how a symbolic checker proves one register allocation correct, and why running the code on numbers can miss the same bug.
    - Choose the narrowest test that shows a given back-end bug, and plan differential runs across back ends and machines.

A back end is hard to test for a reason that a parser is not. A parser has
one right answer for each input: this tree, or this error. A back end has
many. `x / 7` may become a divide instruction or a multiply by a
precomputed constant, the temporary may live in `w8` or `w9`, and two
independent instructions may come out in either order. All of these
programs are correct. A test that insists on one of them fails every time
someone improves the compiler, and a test that accepts anything catches
nothing.

So back-end testing splits into two questions. The first: **is the
code correct**? Does the compiled program compute what the source says?
Only running it, or checking it against the source with a separate program,
answers that. The second: **is it the code we meant**? Did the divide
become a multiply, did the two loads become one pair, is there no fused
multiply-add? A correct program can still fail this second test, and a
program can pass it and still be wrong. LLVM answers the second question
with a large suite of small text tests; this chapter starts there and then
turns to the first.

Figure 1 places each kind of test on the pipeline that [E1](e1-llvm-codegen-pipeline.md)
followed from LLVM IR to bytes.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="Where each kind of back-end test enters the pipeline" aria-describedby="e4-map-desc">
<title id="e4-map-title">Where each kind of back-end test enters the pipeline</title>
<desc id="e4-map-desc">A row of five boxes from left to right: LLVM IR, instruction selection, machine passes on MIR, assembly text, and bytes in an object file. Below the row, five brackets show what each test covers. An llc test with FileCheck spans from LLVM IR to assembly text and checks the text. A MIR test covers one machine pass, reading MIR and writing MIR. An encoding test with llvm-mc spans assembly text and bytes. A checker sits beside the machine passes and checks one pass's result against its input. A differential test spans the whole row plus running the program, and compares the output of two paths.</desc>
<rect class="vx-box" x="20" y="30" width="120" height="50" rx="4"/>
<text class="vx-text" x="80" y="60" text-anchor="middle">LLVM IR</text>
<rect class="vx-box" x="170" y="30" width="120" height="50" rx="4"/>
<text class="vx-text" x="230" y="52" text-anchor="middle">instruction</text>
<text class="vx-text" x="230" y="70" text-anchor="middle">selection</text>
<rect class="vx-box-strong" x="320" y="30" width="130" height="50" rx="4"/>
<text class="vx-text" x="385" y="52" text-anchor="middle">machine passes</text>
<text class="vx-text-muted" x="385" y="70" text-anchor="middle">on MIR</text>
<rect class="vx-box" x="480" y="30" width="120" height="50" rx="4"/>
<text class="vx-text" x="540" y="52" text-anchor="middle">assembly</text>
<text class="vx-text" x="540" y="70" text-anchor="middle">text</text>
<rect class="vx-box" x="630" y="30" width="110" height="50" rx="4"/>
<text class="vx-text" x="685" y="52" text-anchor="middle">bytes</text>
<text class="vx-text-muted" x="685" y="70" text-anchor="middle">object file</text>
<path class="vx-flow" d="M140 55 L170 55"/>
<path class="vx-flow" d="M290 55 L320 55"/>
<path class="vx-flow" d="M450 55 L480 55"/>
<path class="vx-flow" d="M600 55 L630 55"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5">
<line class="vx-line" x1="80" y1="108" x2="540" y2="108"/>
<line class="vx-line" x1="80" y1="100" x2="80" y2="116"/>
<line class="vx-line" x1="540" y1="100" x2="540" y2="116"/>
<text class="vx-text" x="310" y="134" text-anchor="middle">llc test + FileCheck: IR in, check the assembly text</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5">
<line class="vx-line" x1="330" y1="158" x2="440" y2="158"/>
<line class="vx-line" x1="330" y1="150" x2="330" y2="166"/>
<line class="vx-line" x1="440" y1="150" x2="440" y2="166"/>
<text class="vx-text" x="385" y="184" text-anchor="middle">MIR test: one pass</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5">
<line class="vx-line" x1="540" y1="208" x2="685" y2="208"/>
<line class="vx-line" x1="540" y1="200" x2="540" y2="216"/>
<line class="vx-line" x1="685" y1="200" x2="685" y2="216"/>
<text class="vx-text" x="612" y="234" text-anchor="middle">encoding test: llvm-mc</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5">
<rect class="vx-box-accent" x="170" y="196" width="220" height="44" rx="4"/>
<text class="vx-text" x="280" y="215" text-anchor="middle">checker: one pass's result</text>
<text class="vx-text-muted" x="280" y="232" text-anchor="middle">checked against its input</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 5">
<line class="vx-line" x1="20" y1="272" x2="740" y2="272"/>
<line class="vx-line" x1="20" y1="264" x2="20" y2="280"/>
<line class="vx-line" x1="740" y1="264" x2="740" y2="280"/>
<text class="vx-text" x="380" y="300" text-anchor="middle">differential test: source to running program, two paths, compare the output</text>
</g>
</svg>
<figcaption>Figure 1. Five kinds of test, each covering a different stretch of the back end. The narrow ones, a MIR test or an encoding test, point at one pass or one instruction when they fail. The wide one, a differential run, can find a bug anywhere but says little about where it is.</figcaption>
</figure>

## One test, read line by line

Here is a complete test in the form LLVM's own code-generator tests use. It
is an ordinary LLVM IR file with two kinds of comment added:

--8<-- "includes/examples/backend/e4-testing-backends/div_by_constant.ll.md"

The `RUN:` lines are for **lit**, the LLVM Integrated Tester, which finds
test files, runs the commands written in them and reports each file as
passed or failed[^lit]. lit replaces `%s` with the path of the test file
itself[^testing], so the first RUN line compiles this file for Linux on
AArch64 and pipes the assembly into FileCheck. If any command in the
pipeline fails, the whole test fails[^testing]. The second line does the
same for macOS. Both must pass.

**FileCheck** reads the `CHECK` lines from the file named on its command
line and matches them, in order, against its standard input[^filecheck].
In the plainest form, `CHECK: text` means "this text appears on some line
after the previous match". Horizontal whitespace is canonicalized, so a
space in a check line matches a tab in the output[^filecheck]: `umull x8,
w0` matches the tab-separated `umull	x8, w0` that `llc` prints. In every
test on this page, one space also matched the long runs of spaces that
`llc` and `llvm-mc` use to line up their comments.

Here is what `llc -O2` wrote for `div7` on this machine (LLVM 18.1.8, the
Linux triple, with the `.cfi` and alignment directives removed):

```gas
div7:                                   // @div7
// %bb.0:
	mov	w8, #18725                      // =0x4925
	movk	w8, #9362, lsl #16
	umull	x8, w0, w8
	lsr	x8, x8, #32
	sub	w9, w0, w8
	add	w8, w8, w9, lsr #1
	lsr	w0, w8, #2
	ret
```

Before matching anything, check that the listing is right, because a test
that pins wrong code is worse than none. `mov` and `movk` build the 32-bit
constant `0x24924925` from two 16-bit halves. `umull` multiplies the
unsigned `x` by it to a 64-bit product, and `lsr` keeps the high 32 bits.
Try `x = 100`: the product is 61,356,675,700, and its high half is 14. The
next three instructions correct for the constant being slightly too small:
`100 - 14 = 86`, `14 + 86 / 2 = 57`, and `57 >> 2 = 14`. The answer is
14, which is `100 / 7` rounded down. No `udiv` anywhere, which is what the
test exists to guarantee.

Now match the check lines against the listing, as Figure 2 does.
`CHECK-LABEL: div7:` finds the function's label. Because FileCheck matches
substrings, the same line also matches the Mach-O spelling `_div7:`, which is
why one set of checks serves both RUN lines. `CHECK-NOT: udiv` says that
`udiv` must not appear between the label and the next positive match. Then
`umull [[PROD:x[0-9]+]], w0, {{w[0-9]+}}` matches the multiply, with two new
pieces of syntax.

<figure class="vx-figure">
<svg viewBox="0 0 760 400" role="img" aria-label="How FileCheck matches check lines against llc output" aria-describedby="e4-match-desc">
<title id="e4-match-title">How FileCheck matches check lines against llc output</title>
<desc id="e4-match-desc">On the left, the six check lines for div7. On the right, the ten lines llc wrote for div7, from the label to ret. Arrows join each positive check to the output line it matches: CHECK-LABEL div7 to the label line, CHECK umull to the umull line, CHECK-NEXT lsr to the lsr line directly below it, and CHECK ret to the ret line. The two CHECK-NOT udiv lines are drawn as dashed bands over the output: the first covers the lines between the label and the umull, the second covers the lines between the lsr and the ret. The variable PROD is captured as x8 on the umull line and required again on the lsr line.</desc>
<text class="vx-text" x="20" y="24">check lines</text>
<text class="vx-text" x="442" y="24">llc -O2 output</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5">
<rect class="vx-box-strong" x="20" y="40" width="360" height="30" rx="4"/>
<text class="vx-mono" x="30" y="60">CHECK-LABEL: div7:</text>
<path class="vx-line" d="M380 55 L432 55"/>
<polygon class="vx-arrowhead" points="426,50 434,55 426,60"/>
</g>
<rect class="vx-box" x="20" y="80" width="360" height="30" rx="4"/>
<text class="vx-mono" x="30" y="100">CHECK-NOT: udiv</text>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5">
<rect class="vx-box-strong" x="20" y="120" width="360" height="46" rx="4"/>
<text class="vx-mono" x="30" y="138">CHECK: umull [[PROD:x[0-9]+]],</text>
<text class="vx-mono" x="30" y="158">       w0, {{w[0-9]+}}</text>
<path class="vx-line" d="M380 143 C 410 143, 410 175, 432 175"/>
<polygon class="vx-arrowhead" points="426,170 434,175 426,180"/>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5">
<rect class="vx-box-strong" x="20" y="176" width="360" height="30" rx="4"/>
<text class="vx-mono" x="30" y="196">CHECK-NEXT: lsr {{x[0-9]+}}, [[PROD]], #32</text>
<path class="vx-line" d="M380 191 C 410 191, 410 205, 432 205"/>
<polygon class="vx-arrowhead" points="426,200 434,205 426,210"/>
</g>
<rect class="vx-box" x="20" y="216" width="360" height="30" rx="4"/>
<text class="vx-mono" x="30" y="236">CHECK-NOT: udiv</text>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5">
<rect class="vx-box-strong" x="20" y="256" width="360" height="30" rx="4"/>
<text class="vx-mono" x="30" y="276">CHECK: ret</text>
<path class="vx-line" d="M380 271 C 410 271, 410 325, 432 325"/>
<polygon class="vx-arrowhead" points="426,320 434,325 426,330"/>
</g>
<rect class="vx-box-bad" x="434" y="70" width="316" height="90"/>
<rect class="vx-box-bad" x="434" y="220" width="316" height="90"/>
<text class="vx-mono" x="442" y="60">div7:</text>
<text class="vx-mono" x="442" y="90">// %bb.0:</text>
<text class="vx-mono" x="442" y="120">  mov   w8, #18725</text>
<text class="vx-mono" x="442" y="150">  movk  w8, #9362, lsl #16</text>
<text class="vx-mono" x="442" y="180">  umull x8, w0, w8</text>
<text class="vx-mono" x="442" y="210">  lsr   x8, x8, #32</text>
<text class="vx-mono" x="442" y="240">  sub   w9, w0, w8</text>
<text class="vx-mono" x="442" y="270">  add   w8, w8, w9, lsr #1</text>
<text class="vx-mono" x="442" y="300">  lsr   w0, w8, #2</text>
<text class="vx-mono" x="442" y="330">  ret</text>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 5">
<text class="vx-text-accent" x="592" y="356" text-anchor="middle">dashed bands: where each CHECK-NOT looks</text>
<text class="vx-text-accent" x="592" y="376" text-anchor="middle">PROD = x8, captured on umull, required on lsr</text>
</g>
</svg>
<figcaption>Figure 2. FileCheck moves a cursor forward through the output. Each positive check must match after the previous one; <code>CHECK-NEXT</code> must match on the next line. A <code>CHECK-NOT</code> looks only in the gap between the matches on either side of it, which is why the test states it twice.</figcaption>
</figure>

Text inside double braces, `{{w[0-9]+}}`, is a **regular expression**, a
pattern that matches a set of strings: here any `w` register. Everything
outside the braces is matched literally[^filecheck]. Text in double square
brackets with a colon, `[[PROD:x[0-9]+]]`, **captures** whatever the pattern
matched into a variable named `PROD`, and a later `[[PROD]]` must match that
same text[^filecheck]. In this output `PROD` becomes `x8`, and the next line
must shift `x8`.

`CHECK-NEXT` demands that its match sits on the line directly after the
previous match[^filecheck]. So the test says: some `x` register receives
the product, and the next instruction shifts that register right by 32.
It does not say which register. The allocator may choose `x9` tomorrow, and
the test still passes, because the register was never the point.

The `clamp` test uses the same tools for a different promise: two compares
and two conditional selects, with no branch, and the temporary that the
first `csel` writes is the one the second `cmp` and `csel` read. Five
`CHECK-NEXT` lines make the sequence exact, and one captured variable keeps
it independent of the register choice.

??? check "The test has two `CHECK-NOT: udiv` lines. What could slip through if only the first one were there?"

    A `udiv` placed after the `lsr` and before the `ret`. A `CHECK-NOT`
    only covers the gap between the positive matches around it, so the
    first one covers the lines from the label to the `umull` and nothing
    later[^filecheck]. With a positive check after the last `CHECK-NOT`
    (here `CHECK: ret`), the two gaps together cover the whole function.

### What FileCheck adds over searching

The ordering matters more than it looks. The next example checks one
wrong listing three ways. In it, `fast_div` still divides, and the `umull`
the test looks for belongs to the next function, `scale`:

--8<-- "includes/examples/backend/e4-testing-backends/ordered_match.cpp.md"

It prints:

```text
--8<-- "examples/backend/e4-testing-backends/ordered_match.expected"
```

The grep-like check passes because `umull` is somewhere in the file.
The ordered check also passes: it finds `fast_div:`, then moves its cursor
forward and finds the `umull` in `scale`, since nothing told it where
`fast_div` ends. Only the third check fails, because a **label check** cuts
the input into blocks at the labels first and then matches each group of
checks inside its own block[^filecheck]. The real FileCheck behaves the
same way on this listing: with plain `CHECK` lines it passes, and with a
`CHECK-LABEL` for each function it fails at the `umull` (checked with
FileCheck 18.1.8).

That is also why every function in a test file should start with a
`CHECK-LABEL`. Besides keeping each function's checks inside that
function, it lets FileCheck report a failure in one block and continue to
the next, so one run shows every broken function[^filecheck]. The LLVM
testing guide asks for FileCheck rather than `grep` in RUN lines for this
reason[^testing].

## A test that fails for the right reasons

A test earns its keep by failing when the compiler is wrong and passing when
it is not. Running the same file through two other configurations of the
same `llc` shows both halves.

At `-O0`, `llc` 18.1.8 emitted `udiv w0, w0, w8` for `div7`, and FileCheck
failed at the `umull` line, printing the `udiv` line as the "possible
intended match". The test was written for `-O2`, and at `-O0` a divide is
expected, so this run shows that the test does notice a `udiv` when one is
there. The same run also failed `clamp`, for a different reason: the
`-O0` code starts with `subs w8, w0, w1` instead of `cmp w0, w1`. Both
compute `w0 - w1` and set the same flags; `cmp` is an alias of `subs` that
throws the result away. The code is fine; the test was written for `-O2`.

With `-O2 -global-isel`, which selects instructions with GlobalISel instead
of SelectionDAG ([E1](e1-llvm-codegen-pipeline.md)), `div7` passed, and
`clamp` failed again:

```gas
clamp:                                  // @clamp
// %bb.0:
	cmp	w1, w0
	csel	w8, w1, w0, gt
	cmp	w2, w8
	csel	w0, w2, w8, lt
	ret
```

Read it with the condition table from [A2](a2-aarch64-assembly.md#flags-and-conditions).
`cmp w1, w0` followed by `gt` picks `w1` when `lo > x`, which is the same
choice as `x < lo`. The code is correct and the test rejects it, because
the check lines spelled out the operand order of the compare. That is the
central trade-off of text tests, and it has no single answer. The rule that
works is to make each check line as strict as the property the test is
about, and no stricter.

For `div7` the property is "a multiply instead of a divide", so the
registers are patterns and the other instructions are not mentioned. For
`clamp` the property is "no branch", and a looser test would say so
directly: after the label, a `CHECK-NOT` whose pattern matches any branch
mnemonic, then `CHECK: ret`. The pattern needs care. A plain `b.` would
also match the block comment `// %bb.0:` and fail on correct code; the
pattern `{{[[:space:]](b|b\.[a-z]+|cbn?z|tbn?z)[[:space:]]}}` matches only
a branch mnemonic standing on its own. With it, the test passed on both the
SelectionDAG and the GlobalISel output, and failed on a hand-made listing
with a `b.lt` in it (FileCheck 18.1.8).

Each kind of looseness has a directive.
`CHECK-DAG` matches a group of lines in any order, for output the
scheduler may reorder[^filecheck]. `--check-prefixes` lets one file hold
checks for several RUN lines, so lines shared by two targets use a common
prefix and the rest a prefix per target[^filecheck].
`--implicit-check-not` adds a `CHECK-NOT` for a pattern everywhere in the
file, which suits a rule such as "no `fmadd` anywhere"[^filecheck].

??? check "The `clamp` test fails on the GlobalISel output above. Which check line fails first, and what would you change if the only property you care about is that `clamp` has no branch?"

    `CHECK: cmp w0, w1`, the first line after the label, because GlobalISel
    compares `w1` with `w0`. For "no branch", replace the five exact lines
    with a `CHECK-NOT` for branch mnemonics followed by `CHECK: ret`, and
    at most check that two `csel` instructions appear. Checking the exact
    operands tests the instruction selector's habits, not the property.

### Generated check lines

Writing check lines by hand gets slow when a function compiles to fifty
instructions. LLVM ships scripts that write them:
`update_llc_test_checks.py` runs the RUN lines of an `llc` test and inserts
FileCheck lines for the output, with a `NOTE:` line at the top saying the
checks were generated; `update_mir_test_checks.py` does the same for MIR
output, and `update_test_checks.py` for `opt`[^testing]. The script emits a
`CHECK-LABEL` for each function and a `CHECK-NEXT` for each following
line[^utc], so a generated test pins the whole listing.

That sounds like the brittleness this section warned against, and it is.
LLVM's testing guide still asks for generated check lines "whenever
feasible"[^testing], because they pay off in a different way. When a patch
changes code generation, rerunning the script rewrites every affected test,
and the reviewer reads the change to the expected assembly as a diff. The
guide's pre-commit workflow builds on this: commit the new test first with
the current output as its checks, then the compiler change, so the second
commit shows exactly what changed[^testing]. A generated test does not say
what matters; a person reviewing its diff does.

Hand-written and generated checks answer different needs, as this table
sets out:

| | Hand-written checks | Generated checks |
| --- | --- | --- |
| What they pin | the property the author chose | every line of the listing |
| Fail when | the property breaks | anything in the function changes |
| After a correct change | still pass | rerun the script, review the diff |
| Good for | rules: "no divide", "no fused multiply-add" | the exact output of a pass under development |
| Risk | the author checked the wrong thing | a wrong listing is accepted into the test once and pinned |

The last row matters most. A generated test records whatever the compiler
did on the day it was generated. If that was wrong, the test now defends
the bug. Every generated listing needs the same hand check the `div7`
listing received above.

## Testing one pass: MIR tests

An `llc` test runs the whole pipeline, so when it fails, any of dozens of
passes could be to blame. A **MIR test** runs one. It starts from a `.mir`
file, the text form of a machine function from
[E1](e1-llvm-codegen-pipeline.md#machineinstr-and-mir-a-shared-language-mid-pipeline),
and uses `-run-pass` so that `llc` parses the file, runs only the named
pass and prints the result[^mir]. [E3](e3-llvm-allocator-scheduler-mc.md#mir-the-pipeline-made-visible-and-testable)
used this to look at the greedy allocator; here it becomes a test.

This test checks AArch64's load and store optimizer, the pass that turns
two neighbouring loads into one load-pair instruction
([A2](a2-aarch64-assembly.md) introduced `ldp`):

```yaml
# RUN: llc -mtriple=aarch64 -run-pass=aarch64-ldst-opt -o - %s | FileCheck %s
---
name:            sum_two
tracksRegLiveness: true
body:             |
  bb.0:
    liveins: $x0

    ; CHECK-LABEL: name: sum_two
    ; CHECK:       $w1, renamable $w2 = LDPWi renamable $x0, 0
    ; CHECK-NOT:   LDRWui
    ; CHECK:       $w0 = ADDWrr
    renamable $w1 = LDRWui renamable $x0, 0 :: (load (s32))
    renamable $w2 = LDRWui renamable $x0, 1 :: (load (s32))
    $w0 = ADDWrr killed renamable $w1, killed renamable $w2
    RET_ReallyLR implicit $w0
...
```

The input is already after register allocation: `$w1` and `$x0` are
physical registers, which is the state this pass expects. `LDRWui` is a
32-bit load with an unsigned offset counted in 4-byte units, so the two
loads read the words at `x0` and `x0 + 4`. The pass replaced them with
one `LDPWi` at offset 0, and the test passed with `llc` 18.1.8. The
`:: (load (s32))` parts are memory operands, which describe each access;
without them the pass left both loads alone on this machine. That is a
lesson in itself: a MIR test's input must carry everything the pass reads.

To show that the test exercises this pass and not something else, change
`-run-pass` to a pass that does not pair loads, such as `machine-cp`; the
test then fails at the `LDPWi` line. A test should be seen failing once
before it is trusted.

Hand-writing the input is fine for a small case. For a real bug, the MIR
reference describes the usual route: compile the IR that shows the bug with
`-stop-after` set to the pass that runs immediately before the one under test,
and use the file it writes as the input[^mir]. That file is long, because
it records everything the pipeline knows. The reference lists what can
usually go: `-simplify-mir` drops fields with default values, the frame
information is often unnecessary, branch probabilities can be removed,
and the embedded IR module can often be replaced or dropped[^mir]. MIR
cannot express everything; target-specific function state is one thing it
does not serialize, so some passes cannot be tested this way[^mir].

??? check "A patch changes the register coalescer, and fifteen `llc` tests start failing. Why is a MIR test for the coalescer the better regression test to add?"

    Each failing `llc` test runs the whole pipeline, so its failure could
    come from any pass that sees the coalescer's output, and a later change
    to any of those passes can break or hide it. A MIR test feeds the
    coalescer a fixed input and checks only its output, so it fails for
    this pass alone, runs fast, and keeps testing the same situation even
    when earlier passes change what they produce.

## Testing bytes: encoding tests

The last step of a back end turns assembly text into bytes. An assembler
bug is quiet: the listing looks right and the processor runs something
else. **Encoding tests** pin the bytes. `llvm-mc` is LLVM's machine-code
tool: it assembles and disassembles text for any target LLVM supports, and
`--show-encoding` prints each instruction's bytes beside it[^llvm-mc]. LLVM's
MC tests pair it with FileCheck:

--8<-- "includes/examples/backend/e4-testing-backends/encodings.s.md"

The bytes are printed in memory order, and AArch64 is little-endian, so
`[0x20,0x40,0x00,0x91]` is the 32-bit word `0x91004020`. Decode it by hand
with the field layout that LLVM's AArch64 target description gives for
this instruction[^aarch64-td], which is also Figure 3:

<figure class="vx-figure">
<svg viewBox="0 0 760 245" role="img" aria-label="The fields of the instruction word 0x91004020, add x0, x1, #16" aria-describedby="e4-bits-desc">
<title id="e4-bits-title">The fields of add x0, x1, #16</title>
<desc id="e4-bits-desc">The 32-bit word 0x91004020 split into fields from bit 31 on the left to bit 0 on the right. Bit 31, sf, is 1: a 64-bit operation. Bit 30 is 0: add rather than subtract. Bit 29 is 0: flags are not set. Bits 28 to 24 are 10001, which mark add or subtract with an immediate. Bits 23 and 22 are 00: the immediate is not shifted. Bits 21 to 10 hold 000000010000, the immediate 16. Bits 9 to 5 hold 00001, register x1. Bits 4 to 0 hold 00000, register x0. Below, the same bits in groups of four read as the hex digits 9, 1, 0, 0, 4, 0, 2, 0, which is 0x91004020.</desc>
<text class="vx-text-muted" x="20" y="24">bit</text>
<text class="vx-text-muted" x="52" y="24" text-anchor="middle">31</text>
<text class="vx-text-muted" x="100" y="24" text-anchor="middle">30</text>
<text class="vx-text-muted" x="148" y="24" text-anchor="middle">29</text>
<text class="vx-text-muted" x="236" y="24" text-anchor="middle">28 to 24</text>
<text class="vx-text-muted" x="346" y="24" text-anchor="middle">23 to 22</text>
<text class="vx-text-muted" x="496" y="24" text-anchor="middle">21 to 10</text>
<text class="vx-text-muted" x="638" y="24" text-anchor="middle">9 to 5</text>
<text class="vx-text-muted" x="712" y="24" text-anchor="middle">4 to 0</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 4">
<rect class="vx-box" x="30" y="34" width="44" height="44"/>
<text class="vx-mono" x="52" y="62" text-anchor="middle">1</text>
<rect class="vx-box" x="78" y="34" width="44" height="44"/>
<text class="vx-mono" x="100" y="62" text-anchor="middle">0</text>
<rect class="vx-box" x="126" y="34" width="44" height="44"/>
<text class="vx-mono" x="148" y="62" text-anchor="middle">0</text>
<rect class="vx-box-strong" x="174" y="34" width="124" height="44"/>
<text class="vx-mono" x="236" y="62" text-anchor="middle">10001</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4">
<rect class="vx-box" x="302" y="34" width="88" height="44"/>
<text class="vx-mono" x="346" y="62" text-anchor="middle">00</text>
<rect class="vx-box-accent" x="394" y="34" width="204" height="44"/>
<text class="vx-mono" x="496" y="62" text-anchor="middle">000000010000</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4">
<rect class="vx-box" x="602" y="34" width="72" height="44"/>
<text class="vx-mono" x="638" y="62" text-anchor="middle">00001</text>
<rect class="vx-box" x="678" y="34" width="68" height="44"/>
<text class="vx-mono" x="712" y="62" text-anchor="middle">00000</text>
</g>
<text class="vx-text" x="52" y="104" text-anchor="middle">sf</text>
<text class="vx-text-muted" x="52" y="122" text-anchor="middle">64-bit</text>
<text class="vx-text" x="100" y="104" text-anchor="middle">op</text>
<text class="vx-text-muted" x="100" y="122" text-anchor="middle">add</text>
<text class="vx-text" x="148" y="104" text-anchor="middle">S</text>
<text class="vx-text-muted" x="148" y="122" text-anchor="middle">no flags</text>
<text class="vx-text" x="236" y="104" text-anchor="middle">opcode</text>
<text class="vx-text-muted" x="236" y="122" text-anchor="middle">add/sub immediate</text>
<text class="vx-text" x="346" y="104" text-anchor="middle">shift</text>
<text class="vx-text-muted" x="346" y="122" text-anchor="middle">none</text>
<text class="vx-text" x="496" y="104" text-anchor="middle">imm12</text>
<text class="vx-text-muted" x="496" y="122" text-anchor="middle">16</text>
<text class="vx-text" x="638" y="104" text-anchor="middle">Rn</text>
<text class="vx-text-muted" x="638" y="122" text-anchor="middle">x1</text>
<text class="vx-text" x="712" y="104" text-anchor="middle">Rd</text>
<text class="vx-text-muted" x="712" y="122" text-anchor="middle">x0</text>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 4">
<text class="vx-text" x="30" y="156">the same 32 bits in groups of four, and each group as one hex digit:</text>
<rect class="vx-box" x="30" y="168" width="80" height="60" rx="4"/>
<text class="vx-mono" x="70" y="192" text-anchor="middle">1001</text>
<text class="vx-text-accent" x="70" y="217" text-anchor="middle">9</text>
<rect class="vx-box" x="120" y="168" width="80" height="60" rx="4"/>
<text class="vx-mono" x="160" y="192" text-anchor="middle">0001</text>
<text class="vx-text-accent" x="160" y="217" text-anchor="middle">1</text>
<rect class="vx-box" x="210" y="168" width="80" height="60" rx="4"/>
<text class="vx-mono" x="250" y="192" text-anchor="middle">0000</text>
<text class="vx-text-accent" x="250" y="217" text-anchor="middle">0</text>
<rect class="vx-box" x="300" y="168" width="80" height="60" rx="4"/>
<text class="vx-mono" x="340" y="192" text-anchor="middle">0000</text>
<text class="vx-text-accent" x="340" y="217" text-anchor="middle">0</text>
<rect class="vx-box" x="390" y="168" width="80" height="60" rx="4"/>
<text class="vx-mono" x="430" y="192" text-anchor="middle">0100</text>
<text class="vx-text-accent" x="430" y="217" text-anchor="middle">4</text>
<rect class="vx-box" x="480" y="168" width="80" height="60" rx="4"/>
<text class="vx-mono" x="520" y="192" text-anchor="middle">0000</text>
<text class="vx-text-accent" x="520" y="217" text-anchor="middle">0</text>
<rect class="vx-box" x="570" y="168" width="80" height="60" rx="4"/>
<text class="vx-mono" x="610" y="192" text-anchor="middle">0010</text>
<text class="vx-text-accent" x="610" y="217" text-anchor="middle">2</text>
<rect class="vx-box" x="660" y="168" width="80" height="60" rx="4"/>
<text class="vx-mono" x="700" y="192" text-anchor="middle">0000</text>
<text class="vx-text-accent" x="700" y="217" text-anchor="middle">0</text>
</g>
</svg>
<figcaption>Figure 3. <code>add x0, x1, #16</code> as a 32-bit word. Every field comes from a place in the assembly text: the register width sets bit 31, the mnemonic sets bits 30 to 22, and the three operands fill the rest. An encoding test pins all of them at once.</figcaption>
</figure>

The second instruction in the file shows why the check lines spell the
printed form, not the written one. `#4096` does not fit in 12 bits, so the
assembler sets the shift bit and encodes 1 shifted left by 12, and prints
`add x0, x0, #1, lsl #12`. The encoding test records that choice, and the
word `0x91400400` differs from the previous one in exactly the shift bit,
the immediate and the registers.

Encoding tests also cover input that must be rejected. A FileCheck test
can run `not llvm-mc`, where `not` inverts the exit status of the command
it runs[^testing], and check the error text. For `add x0, x0, #4097`,
`llvm-mc` 18.1.8 reported "expected compatible register, symbol or integer
in range [0, 4095]". The reverse direction, `--disassemble`, reads bytes and
prints assembly[^llvm-mc]: on this machine, the bytes
`0x00 0x04 0x40 0x91` came back as `add x0, x0, #1, lsl #12`. A **round
trip**, text to bytes to text, checks the assembler and the disassembler
against each other.

For a compiler that writes its own object files ([B3](b3-object-files.md)),
`llvm-mc` is the obvious **oracle**, the trusted second opinion that a
differential test compares against: encode every instruction the back end
can emit both ways and compare the bytes. One caution about choosing an
oracle. On macOS, `cc -c` on a `.s` file runs clang's `-cc1as`, its
built-in assembler, and it rejected `#4097` with the same message as
`llvm-mc`. The two share LLVM's AArch64 assembler, so they are one opinion,
not two. GNU `as` on the Linux arm64 runner is an independent one.

??? check "Your encoder and `llvm-mc` agree on every instruction in your tests. Name two ways the encoder could still be wrong."

    First, the tests may not cover the bug: an immediate at the edge of
    its range, a register such as `sp` or `xzr` that shares encoding 31,
    or a shifted form, if none of them appear in the test set. Second,
    agreement with an oracle that shares code with your encoder, or whose
    expectations you copied from your own encoder's output, proves
    nothing; the expected bytes must come from an independent source such
    as `llvm-mc`, GNU `as` or the architecture manual.

## Checking a result instead of a text

Text tests check what a pass printed. For some passes a stronger test is
possible: a separate, small program that reads the pass's input and output
and decides whether the output is a correct translation. This is a
**checker**. [C3](c3-linear-scan.md#checking-an-allocation-independently)
built one for linear scan that looks for overlapping intervals in the same
register. Cranelift's register allocator uses a stronger one, which Chris
Fallin describes as a symbolic checker based on **abstract
interpretation**: running the program on descriptions of values instead of
on numbers[^fallin].

The idea fits in one example. Before allocation, a program works on
**virtual registers**, unlimited names such as `v0` and `v1`. After
allocation it works on physical registers and stack slots, with moves,
spills and reloads added. The checker walks the allocated code and keeps a
table: for each register and slot, which virtual value it holds right now.
A load of `v0` into `r0` writes "`r0` holds `v0`". A move copies the entry.
And at every instruction that came from the original program, it checks
that each operand's location holds the virtual value the original
instruction read[^fallin].

--8<-- "includes/examples/backend/e4-testing-backends/alloc_checker.cpp.md"

It prints:

```text
--8<-- "examples/backend/e4-testing-backends/alloc_checker.expected"
```

The program computes `(v0 + v1) * v2` with two registers, so three live
values force a spill. The bad allocation reloads `v2` from slot `s0`, which
holds `v1`. Step through the checker's table for the bad version:

<div class="vx-stepper" markdown="1">
<div class="vx-step" markdown="1">

**Step 1. `r0 = load a`, `r1 = load b`**

Each load defines a virtual value, so the checker records it.

| `r0` | `r1` | `s0` | `s1` |
| --- | --- | --- | --- |
| v0 | v1 | unknown | unknown |

</div>
<div class="vx-step" markdown="1">

**Step 2. `s0 = r1`, `r1 = load c`, `s1 = r1`**

The spill copies `r1`'s entry into `s0`. The load of `c` overwrites `r1`
with `v2`, and the second spill copies that into `s1`. Moves are never
checked themselves; they only carry entries along.

| `r0` | `r1` | `s0` | `s1` |
| --- | --- | --- | --- |
| v0 | v2 | v1 | v2 |

</div>
<div class="vx-step" markdown="1">

**Step 3. `r1 = s0`, then `r0 = add r0, r1`**

The reload brings `v1` back. The `add` came from the original
`v3 = add v0, v1`, so the checker compares: `r0` holds `v0` and `r1` holds
`v1`, as required. The `add` then defines `v3` in `r0`.

| `r0` | `r1` | `s0` | `s1` |
| --- | --- | --- | --- |
| v3 | v1 | v1 | v2 |

</div>
<div class="vx-step" markdown="1">

**Step 4. `r1 = s0` (the bug)**

The reload meant for `v2` reads the wrong slot. Nothing is checked yet, so
the checker copies `s0`'s entry: `r1` now holds `v1`.

| `r0` | `r1` | `s0` | `s1` |
| --- | --- | --- | --- |
| v3 | v1 | v1 | v2 |

</div>
<div class="vx-step" markdown="1">

**Step 5. `r0 = mul r0, r1`**

This came from `v4 = mul v3, v2`. `r0` holds `v3`, as required. `r1` holds
`v1`, not `v2`, so the checker stops and names the step, the register, what
it held and what it should have held.

| `r0` | `r1` | `s0` | `s1` |
| --- | --- | --- | --- |
| v3 | **v1, expected v2** | v1 | v2 |

</div>
</div>

Now look at the first line of the output. With inputs `a = 2`, `b = 3`,
`c = 3`, both allocations return 15, because the wrong slot happens to hold
the same number. A differential test that ran only those inputs would pass
the broken allocator. With `c = 4` the results differ, but only a lucky
input finds that. The checker never uses numbers, so it finds the bug on
the first try, whatever the inputs. Fallin makes the same point with a
similar example: simulating concrete values before and after allocation
proves nothing unless every value is tried[^fallin].

Real code has branches, and at a point where two paths meet, the two
tables may disagree about a register. Fallin's checker handles this with
the dataflow machinery of [O4](../optimize/o4-dataflow.md). It merges the
tables at each join: a location that holds different values on different
paths becomes "conflicted", which is harmless until an instruction reads
it and an error when one does. It repeats the merge until no table
changes[^fallin]. The example stays with straight-line code.

A checker can run in two modes. It can check every compilation and refuse
to produce code when the check fails, which costs compile time on every
run. Or it can serve as the **oracle** for a fuzzer, a program that
generates random inputs, feeding random programs to the allocator alone
and reporting any that fail; Cranelift has supported the first mode as an
option and preferred the second, which it runs continuously[^fallin].

## Differential testing across back ends

The checker covers one pass. The widest test in Figure 1 covers
everything: compile the same program two ways, run both, compare what they
print. [B1](b1-simplest-backend.md#two-back-ends-one-oracle) introduced
this **differential testing** for a first back end, and
[O12](../optimize/o12-testing-optimizers.md#comparing-two-runs-differential-testing)
built the full loop of generating programs, comparing runs and reducing
failures. A back end adds three things to that loop.

**More pairs to compare.** Every second path is an oracle for the first.
A back end can be compared with another back end (native against the
LLVM path), with itself at another optimization level, and even with
itself using a different instruction selector: the GlobalISel output above
computes the same results as the SelectionDAG output for every input,
though its text differs. Comparisons across machines matter too. The
[three runners](b2-x86-64.md#three-machines-not-two) of B2 split the
work: in B2's examples no single runner catches every bug, from a divide
instruction that traps on one ISA to a symbol spelling that links only on
one platform.

**Inputs aimed at the back end.** Random program generators exist for this
purpose. Csmith generates random C programs free of undefined behavior and
compares compilers on them[^csmith]. YARPGen generates programs whose
variable types and value ranges are known when they are generated, so it
can rule out undefined behavior exactly, and it steers generation towards
code that triggers optimizations[^yarpgen]. Each YARPGen program prints a
hash of its global variables, and that one number must agree across every
compiler and optimization level[^yarpgen]. LLVM also fuzzes instruction
selection directly: `llvm-isel-fuzzer` feeds structured LLVM IR inputs to
the selector for a chosen triple[^fuzzing].

**Something to compare.** For Vortex the answer is already written down.
A run's observable behavior is the bytes on standard output (by
[record 4](../decisions/program.md#d4) a float prints as the shortest
decimal that reads back to the same value, so equal bytes mean equal
bits), the exit status, and for a run that fails a runtime check,
the kind and position of the failure. Because Vortex forbids fused
multiply-add and reordering of floating-point operations
([record 56](../decisions/numbers.md#d56)), two back ends must agree on
floating-point output bit for bit, with no tolerance. A difference in the
last bit of a matrix product is a bug in one of them.

When a random program exposes a difference, it is usually far too large to
debug. Reduce it first, as [O12](../optimize/o12-testing-optimizers.md#test-case-reduction-from-a-page-to-a-sentence)
describes. For a failure inside LLVM's code generator, `llvm-reduce` does
the work on the compiler's own input: it accepts LLVM IR or MIR, and it
calls an interestingness test, a script given with `--test`, to decide
whether each smaller candidate still fails[^reduce]. The reduced case then
becomes a regression test at the narrowest level that still shows the
bug: a MIR test if one pass is to blame, an `llc` test if the bug needs the
pipeline, an end-to-end run if it needs the whole program.

## Performance models are tested too

One part of a back end makes claims about hardware rather than about
meaning: the scheduling model that [E2](e2-describing-a-target.md) and
[E3](e3-llvm-allocator-scheduler-mc.md) described, with its latencies and
execution resources. Those numbers can be wrong without any program
computing a wrong answer, so no test in this chapter so far would notice.
`llvm-exegesis` measures instruction latency, throughput and port use on
the machine it runs on, using hardware performance counters, and its
stated main goal is to check LLVM's scheduling models against those
measurements[^exegesis]. Its benchmarking works only on Linux[^exegesis],
so on this book's setup it needs a Linux arm64 machine, not the Mac. The model's predictions themselves have text tests: `llvm-mca`
output has its own check-line script, `update_mca_test_checks.py`[^testing].

## Choosing the test

With all five kinds of test in hand, the choice for a given bug follows
from one rule: write the narrowest test that still shows it. A narrow test
fails for one reason, runs in milliseconds and points at the code to fix.

| The bug | The narrowest test |
| --- | --- |
| An instruction encodes to the wrong bytes | an `llvm-mc` (or own-encoder) encoding test |
| One machine pass rewrites code wrongly | a MIR test with `-run-pass` |
| The selector picks a slow or forbidden instruction | an `llc` test with FileCheck and a `CHECK-NOT` |
| The allocator assigns a location that holds the wrong value | a checker, driven by a fuzzer |
| Two paths give different output, cause unknown | a differential run, then reduction, then one of the tests above |

Two habits apply to every row. Write the test before the fix and watch it
fail, so it is known to detect the bug, then watch it pass. And keep a
test's check lines about the property: a test that breaks on every correct
change will be regenerated without being read, and then it protects
nothing.

## For Vortex

!!! vortex "Exercise"

    **Build the back-end test layer for your stage 10 kernel.** Whatever
    path to machine code you chose in
    [stage 6](../compiler/guide/stage-6-first-machine-code.md#choosing-a-back-end),
    add three kinds of test to your suite, driven by lit and FileCheck or
    by your own runner if it offers the same ordered, label-scoped
    matching. Each test must state in a comment the property it protects.

    1. **Assembly checks for `multiply`.** Compile the program from
       [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for)
       and check the assembly of `multiply` at two optimization levels, if
       your compiler has two, and for two platforms, macOS arm64 and one
       Linux. The checks must establish: no fused multiply-add instruction
       anywhere in the function ([record 56](../decisions/numbers.md#d56));
       every bounds check reaches its error path through an unsigned
       condition ([record 12](../decisions/arrays.md#d12)); and at least one
       floating-point store goes through the address that arrived with
       `c`. This turns the text checks of the
       [A2 exercise](a2-aarch64-assembly.md#for-vortex) into FileCheck tests.
    2. **A differential run.** Compile every end-to-end test and a fixed,
       checked-in set of at least 50 generated programs with two paths
       (your native back end and the v0.1 path, or two optimization
       levels), run both, and compare standard output bytes, exit status
       and the runtime-error line. Run it on all three CI runners.
    3. **An encoding test**, only if your back end writes its own object
       files: for each instruction form your encoder can emit, compare its
       bytes with `llvm-mc --show-encoding`, including one immediate at
       each end of its range.

    **Not yet.** Do not write a random-program generator for the whole
    language: the fixed set in item 2 can come from a small generator for
    arithmetic and loops over fixed-shape arrays, and
    [O12](../optimize/o12-testing-optimizers.md#random-programs-testing-what-nobody-thought-to-write)
    covers growing it. Do not generate check lines for the whole of
    `multiply`: a full listing pins your current register choices, and
    this exercise is about properties. Do not build a symbolic allocation
    checker until you have an allocator ([C3](c3-linear-scan.md)).

    **Done when** the suite passes on your unmodified compiler, keeps
    passing when you change something harmless (reverse the order in
    which your back end hands out scratch registers, or compile on the
    other platform so the label changes spelling), and fails for each of
    four deliberate, temporary breakages: contraction allowed in
    `multiply`; one bounds check switched to a signed condition; the
    operands of the subtraction template swapped, which only the
    differential run should catch; and, if you did item 3, one bit of one
    encoding flipped. Record which test caught each breakage. A breakage
    that no test catches is a missing test.

## Key ideas

!!! recap "You can now answer"

    - **Why can a back end not be tested by comparing its output with one expected listing?** Many different listings are correct, so an exact comparison fails on every harmless change and says nothing about which change was wrong.
    - **What does `CHECK-LABEL` add to plain `CHECK` lines?** It cuts the output into one block per label first, so each function's checks can only match inside that function.
    - **Where does a `CHECK-NOT` look?** Only in the gap between the positive matches before and after it.
    - **How strict should a check line be?** As strict as the property the test protects: patterns and captured variables for registers, exact text only for what matters.
    - **What does a MIR test give that an `llc` test does not?** It runs one pass on a fixed input, so a failure points at that pass alone.
    - **Why can a symbolic allocation checker find bugs that running the code misses?** It tracks which virtual value each location holds, so a wrong location is caught even when it happens to hold the same number.
    - **What must two Vortex back ends agree on in a differential run?** Standard output byte for byte (floating-point bits included), exit status, and the kind and position of any runtime error.

## Where this comes back

!!! next "You will use this again in"

    - [O12. Testing an optimizer](../optimize/o12-testing-optimizers.md): *differential testing*, *reduction*, *one test at the narrowest level*
    - [P16. Capstone: the ladder, measured](../optimize/p16-capstone.md): *bitwise comparison against a reference*
    - [G9. GPU compilers inside LLVM](../gpu/g9-gpu-compilers-in-llvm.md): *`llc` tests with FileCheck for another target*
    - [M3. Passes and pattern rewriting](../mlir/m3-passes-and-rewriting.md): *lit RUN lines*, *FileCheck on the output of one pass*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *a new path tested against the existing ones*

## Sources and further reading

The FileCheck manual and the LLVM Testing Infrastructure Guide are the two
documents to keep open while writing tests. Fallin's post is the clearest
account of a checker for a real allocator.

[^lit]: LLVM Project, "lit - LLVM Integrated Tester", command guide. <https://llvm.org/docs/CommandGuide/lit.html>
[^testing]: LLVM Project, "LLVM Testing Infrastructure Guide": sections "Regression tests", "Writing new regression tests" (RUN lines, pipelines, FileCheck instead of grep), "Generating assertions in regression tests", "Precommit workflow for tests", "Best practices for regression tests", and the substitution and tool lists (`%s`, `not`). <https://llvm.org/docs/TestingGuide.html>
[^filecheck]: LLVM Project, "FileCheck - Flexible pattern matching file verifier": the tutorial, the `CHECK-NEXT`, `CHECK-NOT`, `CHECK-DAG` and `CHECK-LABEL` directives, `--check-prefixes`, `--implicit-check-not`, whitespace handling, regular expressions and string substitution blocks. <https://llvm.org/docs/CommandGuide/FileCheck.html>
[^utc]: LLVM Project, `llvm/utils/UpdateTestChecks/common.py`, used by `update_llc_test_checks.py`: the `NOTE: Assertions have been autogenerated by` line and the `-LABEL` and `-NEXT` suffixes it writes. <https://github.com/llvm/llvm-project/blob/main/llvm/utils/UpdateTestChecks/common.py>
[^mir]: LLVM Project, "Machine IR (MIR) Format Reference Manual", sections "MIR Testing Guide", "Simplifying MIR files" and "Limitations". <https://llvm.org/docs/MIRLangRef.html>
[^llvm-mc]: LLVM Project, "llvm-mc - LLVM Machine Code Playground": `--assemble`, `--disassemble`, `--show-encoding` and `--triple`. <https://llvm.org/docs/CommandGuide/llvm-mc.html>
[^aarch64-td]: LLVM Project, `llvm/lib/Target/AArch64/AArch64InstrFormats.td`: the classes `BaseAddSubImm` and `AddSubImmShift` and the multiclass that sets bit 31 for the 64-bit form. <https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/AArch64/AArch64InstrFormats.td>
[^fallin]: Chris Fallin, "Cranelift, Part 3: Correctness in Register Allocation", 15 March 2021: the symbolic checker, the argument against checking with concrete values, the "conflicted" state and the merge at control-flow joins, and the runtime and fuzzing modes. <https://cfallin.org/blog/2021/03/15/cranelift-isel-3/>
[^csmith]: Csmith project, GitHub repository README: a random generator of C programs, free of undefined behavior, meant for finding compiler bugs by differential testing. The study behind it is Yang, Chen, Eide and Regehr, "Finding and understanding bugs in C compilers", PLDI 2011, <https://doi.org/10.1145/1993498.1993532>. <https://github.com/csmith-project/csmith>
[^yarpgen]: Intel, YARPGen (Yet Another Random Program Generator), GitHub repository README: programs without undefined behavior, generation guided by policies that make optimizations more likely, and a hash of global variables as the program's output. <https://github.com/intel/yarpgen>
[^fuzzing]: LLVM Project, "Fuzzing LLVM libraries and tools", section "llvm-isel-fuzzer". <https://llvm.org/docs/FuzzingLLVM.html>
[^reduce]: LLVM Project, "llvm-reduce - LLVM automatic testcase reducer": the `--test` interestingness script and the `-x` choice of IR or MIR input. <https://llvm.org/docs/CommandGuide/llvm-reduce.html>
[^exegesis]: LLVM Project, "llvm-exegesis - LLVM Machine Instruction Benchmark": description and supported platforms. <https://llvm.org/docs/CommandGuide/llvm-exegesis.html>
