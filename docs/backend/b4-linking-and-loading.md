# B4. Linking and loading

<p class="page-intro">An object file is not yet a program: its calls still name functions it does not contain, and none of its code has an address. This chapter follows two object files through the linker, which joins them into one file, and the loader, which puts that file in memory and connects it to shared libraries. It also shows the stubs and tables that make a call into Vortex's runtime work when the runtime lives in a separate library.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 40 minutes · Builds on: [B3. Object files and assemblers](b3-object-files.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does an undefined symbol in an object file record?"

        A name the file uses but does not define, such as a runtime function
        it calls. The entry has no address; it tells the linker whose address
        a later patch needs.

        Introduced in [B3. Object files and assemblers, "Symbols: names the linker must match up"](b3-object-files.md#symbols-names-the-linker-must-match-up).

    ??? question "What three things does a relocation say?"

        Which bytes are unfinished, which symbol will finish them, and how to
        combine that symbol's final address with what is already there.

        Introduced in [B3. Object files and assemblers, "Relocations: the holes themselves"](b3-object-files.md#relocations-the-holes-themselves).

    ??? question "How far can a `bl` reach, and what does a linker do when the target is farther?"

        ±128 MiB, because the offset is a signed 26-bit count of 4-byte
        words. Beyond that the linker inserts a veneer, a short stub within
        reach that jumps the rest of the way and may overwrite `x16` and `x17`.

        Introduced in [B3. Object files and assemblers, "Relocations: the holes themselves"](b3-object-files.md#relocations-the-holes-themselves).

    ??? question "How does AArch64 code read a variable defined in another library?"

        Through the global offset table: `adrp` and `ldr` load the variable's
        address from a table slot, and a third instruction loads the value.

        Introduced in [A2. Reading and writing AArch64 assembly, "Reaching a global table"](a2-aarch64-assembly.md#reaching-a-global-table).

!!! goals "In this chapter"

    - Lay out merged sections and apply a call relocation by hand, down to the patched instruction word.
    - Predict which archive members a traditional Unix linker pulls in, and explain why the order of libraries on the command line can break a link.
    - Trace a call into a shared library through its stub and table slot, on the first call and on every later one, for ELF and for Mach-O.
    - Explain what position-independent code and executables cost on AArch64, using the instructions a compiler emits for each choice.
    - Describe what the kernel and the dynamic linker do between starting a program and running its `main`.

## Two object files that cannot run

Take a two-file program. `main.c` calls a function `report`; `report.c`
defines it. Compiling each file separately gives two object files, and
neither can run. In `main.o` the call is a `bl` instruction whose offset
field is zero, with an `R_AARCH64_CALL26` relocation naming `report`, and
`report` appears in the symbol table as undefined. In `report.o`, `report`
is defined at offset 0 of that file's `.text` section. Neither file knows
where its code will sit in memory.

The **linker** turns such files into one program, in three steps that every
linker performs in some form[^taylor-2][^levine]:

1. **Symbol resolution.** Read every input's symbol table and match each
   undefined name to exactly one definition.
2. **Layout.** Merge sections of the same kind from all inputs (every
   `.text` into one output `.text`, every `.data` into one `.data`) and give
   each merged section, and so each symbol, a final address.
3. **Relocation.** Visit every relocation and patch the bytes it names, now
   that every address is known.

Layout comes before relocation because a patch needs both ends of a
reference: where the instruction ended up and where its target ended up.
Resolution comes first of all because it decides which files take part.

The linker's edits are confined to addresses. It fills in address fields
and, on some targets, rewrites the few instructions that compute an address
(A2's linker optimization hints are one such case). It never changes what a
floating-point or integer instruction computes, so the exact arithmetic
Vortex's back end chose is the arithmetic the program runs.

## A relocation applied by hand

Here are the three steps on real numbers. Suppose `main.o`'s `.text` is
0x18 bytes long with its `bl` at offset 0x8, `report.o`'s `.text` is 0x10
bytes long, and the linker starts the output `.text` at 0x10000 (a toy
address; real linkers choose their own). With `main.o` first on the command
line, `main.o`'s code occupies 0x10000 to 0x10017 and `report.o`'s starts
right after it, at 0x10018. So `report` is at 0x10018 and the `bl` is at
0x10008.

Arm's ELF specification defines the patch for `R_AARCH64_CALL26` as
`S + A - P`, where **S** is the address of the symbol, **A** is the
**addend**, a constant stored with the relocation (0 for a plain call), and
**P** is the address of the place being patched. It also says the result,
X, must satisfy $-2^{27} \le X < 2^{27}$, and that bits 27 to 2 of X go into
the instruction's 26-bit field[^aaelf64]. Working through it:

- X = 0x10018 + 0 - 0x10008 = 16 bytes, a multiple of 4 and in range.
- The field holds X / 4 = 4.
- `bl` with a zero field is 0x94000000, so the patched word is 0x94000004.

That is the word `llvm-mc` 18.1.8 produces for `bl #16` (bytes `04 00 00 94`,
little-endian). Figure 1 draws the same layout.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Two object files' text sections merged into one output text section, and the call between them patched" aria-describedby="b4-layout-desc">
<title id="b4-layout-title">Layout, then relocation</title>
<desc id="b4-layout-desc">On the left, two input text sections: main.o, 0x18 bytes, with a bl to report at offset 0x8 and a relocation R_AARCH64_CALL26 against report; report.o, 0x10 bytes, with report at offset 0. Arrows carry both into one output text section on the right, main.o's part at 0x10000 and report.o's part at 0x10018. A curved arrow runs from the bl at 0x10008 to report at 0x10018, labelled X equals S minus P equals 16. Below, the patched instruction word is 0x94000004: bl with a 26-bit field of 4.</desc>
<text class="vx-text-muted" x="20" y="24">inputs</text>
<text class="vx-text-muted" x="440" y="24">output .text</text>
<rect class="vx-box" x="20" y="40" width="250" height="96" rx="4"/>
<text class="vx-text" x="34" y="62">main.o .text (0x18 bytes)</text>
<rect class="vx-box-bad" x="34" y="76" width="222" height="44" rx="3"/>
<text class="vx-mono" x="46" y="95">+0x8  bl report</text>
<text class="vx-text-muted" x="46" y="112">CALL26 against report</text>
<rect class="vx-box" x="20" y="160" width="250" height="70" rx="4"/>
<text class="vx-text" x="34" y="182">report.o .text (0x10 bytes)</text>
<text class="vx-mono" x="46" y="210">+0x0  report:</text>
<line class="vx-line" x1="270" y1="88" x2="432" y2="88"/>
<polygon class="vx-arrowhead" points="432,83 440,88 432,93"/>
<line class="vx-line" x1="270" y1="195" x2="432" y2="195"/>
<polygon class="vx-arrowhead" points="432,190 440,195 432,200"/>
<rect class="vx-box" x="440" y="40" width="290" height="96" rx="4"/>
<text class="vx-mono" x="454" y="62">0x10000  main.o's code</text>
<rect class="vx-box-accent" x="454" y="76" width="262" height="44" rx="3"/>
<text class="vx-mono" x="466" y="95">0x10008  bl report</text>
<text class="vx-text-muted" x="466" y="112">patched: 0x94000004</text>
<rect class="vx-box" x="440" y="136" width="290" height="94" rx="4"/>
<rect class="vx-box-accent" x="454" y="180" width="262" height="30" rx="3"/>
<text class="vx-mono" x="466" y="200">0x10018  report:</text>
<text class="vx-text-muted" x="454" y="160">report.o's code</text>
<path class="vx-line" d="M716 98 C 760 120, 760 170, 718 192"/>
<polygon class="vx-arrowhead" points="724,186 716,196 712,184"/>
<text class="vx-text-accent" x="20" y="262">X = S + A - P = 0x10018 + 0 - 0x10008 = 16</text>
<text class="vx-text-accent" x="20" y="286">field = X / 4 = 4, so the word is 0x94000000 | 4 = 0x94000004</text>
</svg>
<figcaption>Figure 1. The linker first places both <code>.text</code> sections in the output, which fixes the address of the <code>bl</code> (P) and of <code>report</code> (S). Only then can it compute the distance and write it into the instruction.</figcaption>
</figure>

The next example does both steps for three layouts: the one above, the same
two files in the other order, and one with 128 MiB of other code between
them.

--8<-- "includes/examples/backend/b4-linking-and-loading/call26_patch.cpp.md"

It prints:

```text
--8<-- "examples/backend/b4-linking-and-loading/call26_patch.expected"
```

Swapping the order of two files on the command line changed the call's
direction and every bit of its offset field: the backward call stores X / 4
= -6 in two's complement, 0x3FFFFFA. The third layout shows a limit that
has nothing to do with the code: the call is correct, but the distance the
layout created is 16 bytes past the largest offset `bl` can hold. A
**relocation overflow** like this appears only at link time, because only
the linker knows the distance.

??? check "Complete the patch: a `bl` at 0x20000 calls a function at 0x1FFF0. What are X, the 26-bit field and the instruction word?"

    X = 0x1FFF0 - 0x20000 = -16. The field is -16 / 4 = -4, which in 26
    bits of two's complement is 0x3FFFFFC. The word is
    0x94000000 | 0x3FFFFFC = 0x97FFFFFC, which is what `llvm-mc` encodes
    for `bl #-16`.

## Static libraries: archives searched on demand

A **static library** is an **archive**: many object files, its
**members**, bundled by the `ar` tool into one `.a` file together with a
symbol table that says which member defines which name. The linker does not
copy every member into the program. It pulls a member in only when that
member defines a symbol that is still undefined, which is how every program
can link against one large C library and receive only the parts it
uses[^taylor-11][^wwdc22].

A pulled-in member counts as if it had been named on the command line, so
its own undefined references join the set, and they may be satisfied by
other members of the same archive. The linker therefore repeats its search
of an archive until a pass pulls in nothing more[^taylor-11]. What it does
not do, in the traditional Unix scheme, is come back to an archive after
moving past it. GNU ld's manual says an archive is searched once, at its
position on the command line, and an undefined symbol that appears later
does not cause a second search[^gnu-ld].

The example models that rule. `main.o` needs `parse` and `show`;
`libtext.a` holds `pad.o`, `show.o` and `trim.o`, in that order, and `show.o`
needs `pad`; `libparse.a` holds `parse.o`, which needs `trim`.

--8<-- "includes/examples/backend/b4-linking-and-loading/symbol_resolution.cpp.md"

It prints:

```text
--8<-- "examples/backend/b4-linking-and-loading/symbol_resolution.expected"
```

In both links, `pad.o` is skipped on the first pass over `libtext.a`,
because nothing needs `pad` until `show.o` arrives; the second pass pulls it
in. The two links differ only in the order of the archives. In the first,
the need for `trim` appears when `libparse.a` is searched, after `libtext.a`
is finished, and the link fails. In the second, `parse.o` creates the need
before `libtext.a` is searched, and everything resolves.

The ordering rule is historical: the first Unix linkers searched archives
member by member with no symbol table, and archives were sorted with the
`lorder` and `tsort` tools so that definitions came after their
users[^taylor-11].

Linkers still offer ways around it. GNU ld's
`--start-group` and `--end-group` search a group of archives repeatedly
until no new undefined references appear, at a documented cost in link
time[^gnu-ld]. LLVM's lld keeps every symbol it has seen in any archive, so
a later need can pull a member from an archive it has already passed; its
documentation says it knows of no program this fails to link[^lld]. Apple's
linker, in WWDC22's walk-through, loads every object file on the command
line before it looks in libraries for what is still undefined[^wwdc22].

??? check "A link fails with `undefined symbol: trim` although `libtext.a` defines `trim` and is on the command line. What would you look at first?"

    Where `libtext.a` sits relative to the object or archive that needs
    `trim`. A traditional Unix linker searches an archive only at its own
    position, so if the need appears in a file listed later, `libtext.a`
    has already been passed. Moving it after that file, or grouping the
    archives, fixes the link.

## Which definition wins, and which code survives

Resolution is more than matching names. Ian Lance Taylor lists what matters
when a linker meets the same name twice: among other things, whether each
occurrence is a definition or a reference, whether a definition is
**strong** (ordinary) or **weak** (one the linker may discard), its
visibility, and whether it comes from an object file or a shared
library[^taylor-12].

The common cases follow a few rules. A reference
resolves to whatever definition exists. Two strong definitions of one name
in object files are a multiple-definition error. A strong definition beats
a weak one, which is how the identical copies of an inline C++ function
that several files emit collapse into one. A definition in the program
beats one in a shared library[^taylor-12].

The list has no entry for a function's parameter types. A linker checks that
the name `report` is defined, not that the caller and the definition agree
on what `report` takes; that agreement is the calling convention's job,
upheld by whoever compiled both sides.

Linkers can also drop code nobody reaches. GNU ld's `--gc-sections` keeps
the section holding the entry point and every section reachable from kept
sections through relocations, and discards the rest[^gnu-ld]. The unit is a
whole section, so an unused function survives if it shares a section with a
used one. Mach-O cuts finer: an object file marked with
`.subsections_via_symbols`, which clang writes into every file it compiles
for Apple platforms (A2), tells the linker it may split each section at
every symbol and strip the blocks nothing uses[^apple-as]. Apple's linker
does this with `-dead_strip`[^wwdc22].

## Shared libraries: one copy in memory

Static linking copies library code into every program that uses it. A
**shared library** (a `.so` file on Linux, a `.dylib` on macOS) stays a
separate file. The operating system loads its code into physical memory
once and maps those pages into every process that uses it[^drepper], and a
new version of the library reaches its programs without relinking
them[^taylor-13]. When a program links against one, the static linker does
not copy code; it records the names the program uses and the library's path,
a promise that something at run time will keep[^wwdc22].

That something is the **dynamic linker** (`ld.so` on Linux, `dyld` on
macOS), which runs inside the new process before the program does. The
price of sharing is paid there and in the code: the library's address is
not known until the program starts, so every reference across the boundary
needs a step that can be finished at run time. Taylor sets the trade out
plainly: shared libraries save memory and can be upgraded, while a
statically linked program avoids the cost of position-independent code and
can be tested more reliably[^taylor-13].

macOS makes the choice for the system library. HelloSilicon's notes point
out that Darwin does not generally support statically linked executables,
so every program links `libSystem.dylib` dynamically[^hellosilicon]. On an
Apple M4 Pro with macOS 27 in September 2026, `otool -L` on a hello-world
program built by Apple clang 21 listed exactly one library,
`/usr/lib/libSystem.B.dylib`. Your own libraries, including a language
runtime, can still be static archives.

## Calling into a shared library: stubs, the PLT and the GOT

A `bl` holds a fixed distance, and the distance from a program to a shared
library is not fixed. The static linker's answer is to give the call a
nearby target that it controls. For every function a program imports, it
writes a **stub**, a few instructions in the program's own code, and points
every call to that function at the stub. The stub loads the real address
from a slot in a table of addresses and branches there.

On ELF the stubs
together form the **procedure linkage table (PLT)**, and their slots live in
`.got.plt`, a part of the **global offset table (GOT)** set aside for
them[^sysvabi64][^maskray-plt]. The stubs are code and never change; only
the table, which is data, is written at run time.

Arm's System V ABI spells out the AArch64 version[^sysvabi64]. Entry
`PLT[N]` puts the address of its `.got.plt` slot in `x16`, loads the slot's
contents into `x17` and branches to `x17`. These are the intra-procedure-call
registers IP0 and IP1, the same two a veneer may overwrite, and for the same
reason: the caller cannot know the stub is there. The first entry, `PLT[0]`,
is shared by all the others and calls the dynamic linker.

### The first call and every later one

With **lazy binding**, the dynamic linker leaves each slot pointing at
`PLT[0]` at startup instead of at the function. The first call to `report`
goes to its stub, which loads the slot and branches to `PLT[0]`. `PLT[0]`
calls the dynamic linker's **resolver**, which works out from `x16` which
slot this was, finds the symbol through the relocation for that slot, looks
`report` up in the loaded libraries, writes its address into the slot, and
jumps to `report`[^sysvabi64]. Every later call goes stub, slot, `report`,
with no resolver (Figure 2). The lookup is not cheap: `ld.so` searches the
executable's symbol table first and then each needed library in
turn[^maskray-plt].

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="The first and second call to an imported function through a PLT entry and its GOT slot" aria-describedby="b4-plt-desc">
<title id="b4-plt-title">Lazy binding through the PLT</title>
<desc id="b4-plt-desc">Two rows. First call: bl report at plt reaches PLT[N], which loads the .got.plt slot for report. The slot still holds the address of PLT[0], so PLT[N] branches there. PLT[0] calls the dynamic linker's resolver, which looks report up, writes report's address into the slot, shown with a dashed arrow, and then jumps on to report. Second call: bl reaches PLT[N], which loads the slot, now holding report, and branches straight to report. The resolver is not involved.</desc>
<text class="vx-text-accent" x="20" y="20">First call</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 10">
<rect class="vx-box" x="20" y="34" width="120" height="44" rx="4"/>
<text class="vx-mono" x="80" y="61" text-anchor="middle">bl report@plt</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 10">
<rect class="vx-box" x="170" y="34" width="110" height="44" rx="4"/>
<text class="vx-mono" x="225" y="54" text-anchor="middle">PLT[N]</text>
<text class="vx-text-muted" x="225" y="70" text-anchor="middle">x16, x17, br</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 10">
<rect class="vx-box-bad" x="310" y="34" width="160" height="44" rx="4"/>
<text class="vx-mono" x="390" y="54" text-anchor="middle">slot for report</text>
<text class="vx-text-muted" x="390" y="70" text-anchor="middle">holds PLT[0]</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 10">
<rect class="vx-box" x="310" y="130" width="160" height="44" rx="4"/>
<text class="vx-mono" x="390" y="157" text-anchor="middle">PLT[0]</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 10">
<rect class="vx-box-strong" x="510" y="130" width="160" height="44" rx="4"/>
<text class="vx-text" x="590" y="150" text-anchor="middle">resolver</text>
<text class="vx-text-muted" x="590" y="166" text-anchor="middle">in the dynamic linker</text>
</g>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 10">
<rect class="vx-box-accent" x="600" y="34" width="140" height="44" rx="4"/>
<text class="vx-mono" x="670" y="54" text-anchor="middle">report</text>
<text class="vx-text-muted" x="670" y="70" text-anchor="middle">in the library</text>
</g>
<line class="vx-line" x1="140" y1="56" x2="162" y2="56"/>
<polygon class="vx-arrowhead" points="162,51 170,56 162,61"/>
<line class="vx-line" x1="280" y1="56" x2="302" y2="56"/>
<polygon class="vx-arrowhead" points="302,51 310,56 302,61"/>
<line class="vx-line" x1="390" y1="78" x2="390" y2="122"/>
<polygon class="vx-arrowhead" points="385,122 390,130 395,122"/>
<line class="vx-line" x1="470" y1="152" x2="502" y2="152"/>
<polygon class="vx-arrowhead" points="502,147 510,152 502,157"/>
<line class="vx-line" x1="640" y1="130" x2="660" y2="86"/>
<polygon class="vx-arrowhead" points="655,86 662,78 664,90"/>
<path class="vx-line" d="M540 130 C 520 100, 500 90, 478 70" style="stroke-dasharray:4 4"/>
<polygon class="vx-arrowhead" points="482,64 472,66 478,76"/>
<text class="vx-text-muted" x="500" y="108">writes report</text>
<text class="vx-text-accent" x="20" y="222">Every later call</text>
<g class="vx-seq" style="--vx-i: 6; --vx-n: 10">
<rect class="vx-box" x="20" y="236" width="120" height="44" rx="4"/>
<text class="vx-mono" x="80" y="263" text-anchor="middle">bl report@plt</text>
</g>
<g class="vx-seq" style="--vx-i: 7; --vx-n: 10">
<rect class="vx-box" x="170" y="236" width="110" height="44" rx="4"/>
<text class="vx-mono" x="225" y="256" text-anchor="middle">PLT[N]</text>
<text class="vx-text-muted" x="225" y="272" text-anchor="middle">x16, x17, br</text>
</g>
<g class="vx-seq" style="--vx-i: 8; --vx-n: 10">
<rect class="vx-box-accent" x="310" y="236" width="160" height="44" rx="4"/>
<text class="vx-mono" x="390" y="256" text-anchor="middle">slot for report</text>
<text class="vx-text-muted" x="390" y="272" text-anchor="middle">holds report</text>
</g>
<g class="vx-seq" style="--vx-i: 9; --vx-n: 10">
<rect class="vx-box-accent" x="600" y="236" width="140" height="44" rx="4"/>
<text class="vx-mono" x="670" y="256" text-anchor="middle">report</text>
<text class="vx-text-muted" x="670" y="272" text-anchor="middle">in the library</text>
</g>
<line class="vx-line" x1="140" y1="258" x2="162" y2="258"/>
<polygon class="vx-arrowhead" points="162,253 170,258 162,263"/>
<line class="vx-line" x1="280" y1="258" x2="302" y2="258"/>
<polygon class="vx-arrowhead" points="302,253 310,258 302,263"/>
<line class="vx-line" x1="470" y1="258" x2="592" y2="258"/>
<polygon class="vx-arrowhead" points="592,253 600,258 592,263"/>
<text class="vx-text-muted" x="535" y="248" text-anchor="middle">no resolver</text>
<text class="vx-text-muted" x="20" y="316">The stub and PLT[0] are code in the program; the slot is data, the only thing written at run time.</text>
</svg>
<figcaption>Figure 2. Lazy binding on AArch64 ELF. The slot starts out pointing at <code>PLT[0]</code>, so the first call detours through the resolver, which rewrites the slot. Later calls still pass through the stub and the slot, but not the resolver.</figcaption>
</figure>

Even a resolved call is not free. It still runs the stub's loads and an
indirect branch, where a call inside one program is a single `bl`. GCC
and Clang offer `-fno-plt`, which loads the target from the GOT at the call
site and skips the stub; MaskRay judges the trade weak on AArch64, where the
call site then needs three instructions of its own[^maskray-plt].

The example models lazy binding with real function pointers. Each slot
starts out holding the resolver; the stub records its slot in a variable
that plays `x16`'s part. The program imports three functions but calls only
two.

--8<-- "includes/examples/backend/b4-linking-and-loading/lazy_binding.cpp.md"

It prints:

```text
--8<-- "examples/backend/b4-linking-and-loading/lazy_binding.expected"
```

Lazily, the resolver runs once for each function the program calls, at its first
call; `cube` is never looked up. Eagerly, all three lookups happen before
the first call, so the program starts slower and never pays again. In a
benchmark, lazy binding moves that cost into the first iteration, one more
reason to discard warm-up runs.

### Eager binding and read-only tables

**Eager binding** resolves every slot before any of the program's code
runs. On ELF, linking with `-z now` asks for it, and so does setting the
environment variable `LD_BIND_NOW` at run time[^sysvabi64][^maskray-plt].
GNU ld still makes lazy binding its default[^gnu-ld], but MaskRay reports
that many Linux distributions have switched to eager binding for
security[^maskray-plt].

The reason is that a writable table of code
addresses is a target: an attacker who can overwrite a slot redirects the
next call. With eager binding nothing needs to write the table after
startup, so the linker can place it in a region marked **RELRO**
(relocation read-only), which the dynamic linker makes read-only once
relocation is done. Doing this for `.got.plt` as well as `.got` is called
full RELRO[^maskray-got][^sysvabi64].

### The same call on macOS

Mach-O has the same structure under different names. Calls go to entries in
the `__stubs` section, and each stub loads a pointer from a data section
and branches through it[^wwdc22]. MaskRay describes a lazy form in which
each pointer first leads into a helper section that calls dyld,
much as ELF's slots lead to `PLT[0]`[^maskray-plt]. The files current tools
produced on the M4 Pro with macOS 27 described above do not use it. A
hello-world program built by Apple clang 21 looked like this:

- `main` called `puts` with a `bl` to a stub in `__TEXT,__stubs`.
- The stub was three instructions: `adrp x16`, `ldr x16` from a slot in
  `__DATA_CONST,__got`, and `br x16`.
- There was no stub-helper section, and `dyld_info -fixups` listed both
  slots as binds to `libSystem`, done at launch.

So on this system imported functions are bound eagerly, at launch, and
their slots sit in the `__DATA_CONST` segment rather than in ordinary
writable data.

??? check "A loop calls an imported function 1,000 times with lazy binding. How many calls run the resolver, and does each of the other calls cost the same as a call to a function in the program itself?"

    One: the first call rewrites the slot. The other 999 skip the resolver,
    but each still goes through the stub, which loads the slot and branches
    indirectly. A call to a function in the same program is a single `bl`.

## Position-independent code and executables

A shared library may be loaded at a different address in each process, yet
its code pages are supposed to be shared. The code must therefore work
unchanged at any address. Such code is **position-independent code (PIC)**.
The alternative, patching absolute addresses into the code at load time,
takes time at every start and leaves the patched pages private to each
process, and writable while they are patched, which is a security
risk[^bendersky-pic].

PIC rests on one fact: when the linker lays out a library, the distance
between any instruction and any data in the same library is fixed, wherever
the whole library lands[^bendersky-pic][^sysvabi64]. A2's `adrp` and `add`
pair uses exactly that. For anything that might live in another component,
the code instead loads the address from a GOT slot that the dynamic linker
fills in[^sysvabi64].

A **position-independent executable (PIE)** applies the same rule to the
main program. GNU ld describes a PIE as an executable that the dynamic
linker relocates to whatever address the operating system chooses, which
may differ from one run to the next[^gnu-ld]. Choosing that address at
random is part of **address space layout randomization (ASLR)**, which
dyld also applies to libraries[^wwdc22]. An attacker who cannot predict
where code sits has a harder time aiming at it. On macOS, clang makes every
executable a PIE and `-no-pie` has no effect[^hellosilicon]; `otool -hv` on
the hello-world program above showed the `PIE` flag.

### What PIC costs on AArch64

The cost depends on what the code refers to, and it shows in the
instructions. The function below reads `ext_count`, declared `extern`, and
`own_count`, defined in the same file:

```c
extern int ext_count;
int own_count = 5;
int read_both(void) { return ext_count + own_count; }
```

Apple clang 21, targeting `aarch64-linux-gnu` at `-O1` on the same M4 Pro in
September 2026, compiled it three ways; `llvm-objdump -dr` 18.1.8 showed:

| Mode | `own_count` | `ext_count` | Instructions |
| --- | --- | --- | --- |
| `-fno-pic` (fixed address) | `adrp`, `ldr` | `adrp`, `ldr` | 6 |
| `-fPIE` (executable) | `adrp`, `ldr` | `adrp`, `ldr` from the GOT, `ldr` | 7 |
| `-fPIC` (shared library) | `adrp`, `ldr` from the GOT, `ldr` | `adrp`, `ldr` from the GOT, `ldr` | 8 |

Three things stand out. First, even position-dependent AArch64 code reaches
globals with `adrp`, because no instruction can hold a 64-bit address (A2),
so PIC costs nothing extra for data the linker can see. Second, a PIE loads
`ext_count`'s address from the GOT, because the variable might come from a
shared library.

Third, in a shared library even `own_count` goes through
the GOT. On ELF a default-visibility symbol defined in a shared library can
be **preempted**, replaced at run time by a definition of the same name
elsewhere (the "program beats library" rule above), so the compiler cannot
assume the definition it sees is the one that will be used. Declaring the
symbol with hidden visibility, or keeping it `static`, removes that
indirection[^maskray-got]. In every mode the calls in this chapter stayed a
plain `bl` with `R_AARCH64_CALL26`; the linker decides whether a stub is
needed.

Load-time work follows the same split. A pointer to something inside the
same library needs only the library's load address added, a **rebase** in
Apple's terms and a relative relocation in ELF's, and costs the same small
amount however many libraries are loaded. A pointer to a named symbol needs
a **bind**, a lookup through the loaded libraries, and costs more as there
are more of them[^drepper][^wwdc22].

## When a call cannot reach: thunks and code models

The third layout in the patching example overflowed. For calls, the fix is
routine. AAELF64 allows a linker to insert a **veneer** for a
`R_AARCH64_CALL26` or `R_AARCH64_JUMP26` target that is out of range, and
lets the veneer overwrite `x16`, `x17` and the condition flags[^aaelf64].
MaskRay calls the linker-made kind **range extension thunks**; in his
example the thunk loads the target's full address from a literal stored
next to it into `x16` and branches through `x16`[^maskray-reloc]. The ABI asks that executable sections
be kept under 127 MiB so that veneers placed after a section still reach
it[^sysvabi64].

Data references have no such rescue. `adrp` reaches ±4 GiB[^sysvabi64],
and AAELF64 permits veneers only for branch relocations: any other
relocation that does not fit is an error the linker must report[^aaelf64].
So the compiler must know the program's size in advance. A **code model** is that
promise, made with `-mcmodel`, and it chooses which instruction sequences
the compiler emits. Arm's ABI defines four for AArch64[^sysvabi64]:

| Model | Maximum text size | Maximum span of text and data |
| --- | --- | --- |
| tiny | 1 MiB | 1 MiB |
| small | 2 GiB | 4 GiB |
| medium | 2 GiB | no restriction |
| large | 2 GiB | no restriction |

The small model is the recommended default. The large model is aimed at
programs with large amounts of data, not large amounts of code[^sysvabi64];
in MaskRay's comparison, its data references load each address from a
literal, while calls remain plain `bl` instructions and rely on thunks for
reach[^maskray-reloc]. Vortex's programs are far from these limits, but a
back end that emits its own `adrp` sequences is quietly assuming the small
model.

??? check "The compiler emitted a correct `bl`, and the link still fails with a relocation overflow. Whose mistake is it, and what are the fixes?"

    No one's code is wrong: the layout put the call and its target farther
    apart than 26 bits can express. For a `bl` a linker that inserts
    veneers or thunks fixes it without help; for an `adrp` to data, the fix
    is a larger code model, which changes the instructions the compiler
    emits, or a smaller program.

## The loader: from starting a program to `main`

Everything so far produced a file. Running it takes two more parties
(Figure 3).

On Linux, the kernel reads the executable's program headers and maps each
loadable segment with the permissions it asks for. One program header,
`PT_INTERP`, names the dynamic linker, and the kernel maps that file the
same way. It puts an **auxiliary vector**, a list of tag and value pairs,
on the new process's stack to tell the dynamic linker where the program
is, and starts the dynamic linker, not the program[^drepper]. The dynamic
linker then loads the libraries the program depends on, relocates the
program and every library, runs their initializers in dependency order, and
jumps to the program's entry point[^drepper].

On macOS the same steps have Mach-O names. The hello-world program's load
commands included `LC_LOAD_DYLINKER`, naming `/usr/lib/dyld`,
`LC_LOAD_DYLIB` for `libSystem`, and `LC_MAIN`, the entry-point command;
HelloSilicon explains that linking with `-lSystem` is what adds
`LC_MAIN`[^hellosilicon]. As WWDC22 describes it, dyld parses the
executable, finds and maps each library it needs, recursively, looks up
every bind, applies the fixups, and runs initializers from the bottom of the
dependency tree up[^wwdc22].

<figure class="vx-figure">
<svg viewBox="0 0 760 250" role="img" aria-label="The steps between starting a program and running main, on Linux and on macOS" aria-describedby="b4-load-desc">
<title id="b4-load-title">From exec to main</title>
<desc id="b4-load-desc">Two rows of five steps, the same shape on each system. Linux: the kernel maps the executable's segments; the kernel maps the dynamic linker named by PT_INTERP; ld.so loads the needed libraries; ld.so applies relocations; ld.so runs initializers and jumps to the entry point. macOS: the kernel maps the executable; the kernel maps dyld, named by LC_LOAD_DYLINKER; dyld maps the libraries named by LC_LOAD_DYLIB; fixups, rebases and binds, applied at launch or by the kernel at page-in; dyld runs initializers and calls main, found through LC_MAIN. A label marks the first two steps as the kernel's and the last three as the dynamic linker's, running inside the new process.</desc>
<text class="vx-text-muted" x="20" y="22">kernel</text>
<text class="vx-text-muted" x="320" y="22">dynamic linker, inside the new process</text>
<line class="vx-line" x1="310" y1="30" x2="310" y2="236" style="stroke-dasharray:4 4"/>
<text class="vx-text-accent" x="20" y="50">Linux (ELF)</text>
<rect class="vx-box" x="20" y="60" width="130" height="60" rx="4"/>
<text class="vx-text" x="85" y="86" text-anchor="middle">map the</text>
<text class="vx-text" x="85" y="104" text-anchor="middle">executable</text>
<rect class="vx-box" x="165" y="60" width="130" height="60" rx="4"/>
<text class="vx-text" x="230" y="86" text-anchor="middle">map ld.so</text>
<text class="vx-mono" x="230" y="104" text-anchor="middle">PT_INTERP</text>
<rect class="vx-box" x="325" y="60" width="130" height="60" rx="4"/>
<text class="vx-text" x="390" y="86" text-anchor="middle">load needed</text>
<text class="vx-text" x="390" y="104" text-anchor="middle">libraries</text>
<rect class="vx-box-strong" x="470" y="60" width="130" height="60" rx="4"/>
<text class="vx-text" x="535" y="86" text-anchor="middle">apply</text>
<text class="vx-text" x="535" y="104" text-anchor="middle">relocations</text>
<rect class="vx-box-accent" x="615" y="60" width="130" height="60" rx="4"/>
<text class="vx-text" x="680" y="86" text-anchor="middle">initializers,</text>
<text class="vx-text" x="680" y="104" text-anchor="middle">then entry point</text>
<text class="vx-text-accent" x="20" y="152">macOS (Mach-O)</text>
<rect class="vx-box" x="20" y="162" width="130" height="60" rx="4"/>
<text class="vx-text" x="85" y="188" text-anchor="middle">map the</text>
<text class="vx-text" x="85" y="206" text-anchor="middle">executable</text>
<rect class="vx-box" x="165" y="162" width="130" height="60" rx="4"/>
<text class="vx-text" x="230" y="188" text-anchor="middle">map dyld</text>
<text class="vx-mono" x="230" y="206" text-anchor="middle">LC_LOAD_DYLINKER</text>
<rect class="vx-box" x="325" y="162" width="130" height="60" rx="4"/>
<text class="vx-text" x="390" y="188" text-anchor="middle">map libraries</text>
<text class="vx-mono" x="390" y="206" text-anchor="middle">LC_LOAD_DYLIB</text>
<rect class="vx-box-strong" x="470" y="162" width="130" height="60" rx="4"/>
<text class="vx-text" x="535" y="188" text-anchor="middle">rebases, binds</text>
<text class="vx-text-muted" x="535" y="206" text-anchor="middle">or at page-in</text>
<rect class="vx-box-accent" x="615" y="162" width="130" height="60" rx="4"/>
<text class="vx-text" x="680" y="188" text-anchor="middle">initializers,</text>
<text class="vx-text" x="680" y="206" text-anchor="middle">then LC_MAIN</text>
<line class="vx-line" x1="150" y1="90" x2="160" y2="90"/>
<line class="vx-line" x1="295" y1="90" x2="320" y2="90"/>
<line class="vx-line" x1="455" y1="90" x2="465" y2="90"/>
<line class="vx-line" x1="600" y1="90" x2="610" y2="90"/>
<line class="vx-line" x1="150" y1="192" x2="160" y2="192"/>
<line class="vx-line" x1="295" y1="192" x2="320" y2="192"/>
<line class="vx-line" x1="455" y1="192" x2="465" y2="192"/>
<line class="vx-line" x1="600" y1="192" x2="610" y2="192"/>
<circle class="vx-dot" r="5">
<animateMotion dur="7s" repeatCount="indefinite" path="M85 60 L680 60" keyPoints="0;0;1;1" keyTimes="0;0.1;0.85;1" calcMode="linear"/>
</circle>
</svg>
<figcaption>Figure 3. Starting a dynamically linked program. The kernel maps the executable and the dynamic linker that the executable names; the dynamic linker, running in the new process, loads the libraries, applies relocations and runs initializers before the program's own code starts.</figcaption>
</figure>

Apple has also changed how the relocation step is stored and when it runs.
With **chained fixups**, the load commands record only where the first
fixup sits in each data page; each pointer-sized location holds, besides its
own target, the offset to the next fixup, so the fixups form a chain through
the page.

Because the information lives in the page itself, the kernel can
apply a page's fixups when the page is first touched, which Apple calls
page-in linking. dyld uses it for launch, not for libraries opened later
with `dlopen`[^wwdc22]. The hello-world program carried an
`LC_DYLD_CHAINED_FIXUPS` load command. Either way the rule for the program
is the same: every address a relocation names is correct before the code
that reads it runs.

For a Vortex program, "the program's entry point" is the runtime's start
code, which reaches the validated `main` as
[stage 6](../compiler/guide/stage-6-first-machine-code.md#the-small-runtime)
requires. Everything in this section happens before that code runs.

## For Vortex

!!! vortex "Exercise"

    **Map every crossing between a Vortex program and its runtime.** Your
    runtime provides program start, `print` and the runtime-error report
    ([stage 6](../compiler/guide/stage-6-first-machine-code.md#the-small-runtime),
    [stage 9](../compiler/guide/stage-9-runtime-safety.md#stopping-with-a-clear-runtime-error)).
    Every call from generated code to those functions crosses a link
    boundary. Write a test, runnable from your build, that takes the
    [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for)
    program and checks:

    1. **Every undefined symbol is intended.** List the undefined symbols
       of the Vortex-generated object file (`nm -u` or `llvm-nm -u`). Each
       must be a runtime function or a C library function your compiler is
       meant to call, and each runtime name must be defined by the runtime
       you link.
    2. **Static and shared runtime, same object.** Build the runtime twice:
       as a static archive and as a shared library (`.dylib` on macOS,
       `.so` on Linux, if your CI runs there). Link the same object file,
       without recompiling it, against each. For each executable, record for
       every runtime function whether the call reaches it through a stub
       (`__TEXT,__stubs` in `otool -v -s` on macOS, `@plt` in `objdump -d`
       on Linux) or with a direct `bl`, and whether `dyld_info -fixups` or
       `readelf -r` lists it as a bind.
    3. **Position independence.** Check that each executable is a PIE
       (the `PIE` flag in `otool -hv`, or type `DYN` in `readelf -h`) and,
       on Linux, that `readelf -d` shows no `TEXTREL` entry.
    4. **A broken link fails with a name.** Link once without the object file
       that defines the runtime-error report, and check that the link fails
       with a message naming that symbol.

    Then answer one question in writing, beside the test: the linker matches
    names, not types. If the runtime's `print` changed its parameter type
    but kept its name, which of your existing tests would notice, and at
    what stage?

    **Not yet.** Do not write a linker, a loader or an archive reader; this
    exercise reads what the system tools produce. Do not load code at run
    time with `dlopen` or a JIT ([D2](d2-jit.md)). Do not add code-model or
    visibility flags to your compiler's output to change what you observe:
    record the defaults first. Keep shipping whichever runtime form you use
    today; the second form exists only for the comparison.

    **Done when** both executables run the stage 10 program with the same
    output and exit status, your record names every runtime symbol with
    "stub" or "direct" for each link, and the test fails, naming the
    missing symbol, when the runtime-error report is left out of the link.

## Key ideas

!!! recap "You can now answer"

    - **What are a linker's three steps?** Resolve every undefined name to one definition, lay out the merged sections to give everything an address, then patch every relocation.
    - **How does a linker patch a `bl`?** It computes X = S + A - P, checks that X is a multiple of 4 in $[-2^{27}, 2^{27})$, and writes X / 4 into the instruction's 26-bit field.
    - **Why can library order on the command line break a link?** A traditional Unix linker searches each archive only at its own position, so a need that appears later cannot pull a member from an archive it has already passed.
    - **What does the first call through a lazily bound PLT entry do differently?** It finds its slot pointing at `PLT[0]`, runs the resolver, which writes the real address into the slot; later calls still use the stub but skip the resolver.
    - **Why does a shared library load even its own globals through the GOT?** On ELF a default-visibility definition in a shared library can be preempted by another component, so the compiler cannot assume the definition it sees is the one used.
    - **What can a linker fix when a reference is out of range?** A call, with a veneer or thunk; not an `adrp` to data, which needs a larger code model chosen at compile time.
    - **What runs before a dynamically linked program's first instruction?** The kernel maps the executable and its dynamic linker; the dynamic linker loads libraries, applies rebases and binds, and runs initializers.

## Where this comes back

!!! next "You will use this again in"

    - [D1. Debug information](d1-debug-info.md): *symbol tables*, *what the linker keeps and what it strips*
    - [D2. JIT compilation](d2-jit.md): *resolving symbols inside a running process*, *making memory executable*
    - [D3. Reading real back ends](d3-real-backends.md): *how other compilers hand their output to a linker*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *emitting relocations from the MC layer*
    - [E4. Testing back ends](e4-testing-backends.md): *checking linked output, not only assembly text*

## Sources and further reading

Taylor's twenty-part series is the best free tour of how a linker works
inside; MaskRay's posts go deeper on the PLT, the GOT and code models, and
Arm's System V ABI is the reference for the AArch64 details. Levine's book
remains the classic full treatment.

[^aaelf64]: Arm, "ELF for the Arm 64-bit Architecture (AArch64)" (AAELF64), release 2025Q4: the relocation operation notation S, A and P, the `R_AARCH64_CALL26` entry and its range check, and the rules for linker veneers, including that they may corrupt IP0, IP1 and the flags. <https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst>
[^sysvabi64]: Arm, "System V ABI for the Arm 64-bit Architecture" (SYSVABI64), release 2025Q4: "Code Models" (the model table, the 127 MiB section limit for veneers), and "Program Loading and Dynamic Linking" (position independence, the GOT, the Procedure Linkage Table and lazy and eager binding). <https://github.com/ARM-software/abi-aa/blob/main/sysvabi64/sysvabi64.rst>
[^taylor-2]: Ian Lance Taylor, "Linkers part 2", Airs, 2007: what a linker does, including symbol resolution and relocation. Index of the series at LWN.net: <https://lwn.net/Articles/276782/>. <https://www.airs.com/blog/archives/39>
[^taylor-11]: Ian Lance Taylor, "Linkers part 11", Airs, 2007: archives, their symbol table, the repeated search within one archive and the position rule; the author's reply to a reader on `lorder` and `tsort`. <https://www.airs.com/blog/archives/48>
[^taylor-12]: Ian Lance Taylor, "Linkers part 12", Airs, 2007: symbol resolution, the properties that matter, and the rules for strong, weak and shared-library definitions. <https://www.airs.com/blog/archives/49>
[^taylor-13]: Ian Lance Taylor, "Linkers part 13", Airs, 2007: static linking against dynamic linking. <https://www.airs.com/blog/archives/50>
[^levine]: John R. Levine, *Linkers and Loaders*, Morgan Kaufmann, 1999; the author's page lists the chapters, among them "Libraries", "Relocation", "Shared libraries" and "Dynamic linking and loading". <https://www.iecc.com/linker/>
[^gnu-ld]: Free Software Foundation, *The GNU Linker*, "Command-line Options": `-l` (an archive is searched once, at its position), `--start-group`, `--gc-sections`, `-z lazy`, `-pie`. <https://sourceware.org/binutils/docs/ld/Options.html>
[^lld]: LLVM Project, "The ELF, COFF and Wasm Linkers", section "Efficient archive file handling". <https://lld.llvm.org/NewLLD.html>
[^wwdc22]: Nick Kledzik, "Link fast: Improve build and launch times", Apple WWDC22 session 110362 (transcript): selective loading from static libraries, `-dead_strip`, how dylibs share pages, stubs in `__TEXT`, rebases and binds, dyld's launch steps, chained fixups and page-in linking. <https://developer.apple.com/videos/play/wwdc2022/110362/>
[^drepper]: Ulrich Drepper, "How To Write Shared Libraries", version 4.1.2, 10 December 2011: sharing code through virtual memory, program startup (`PT_INTERP`, the auxiliary vector, the dynamic linker's tasks) and the cost of relative and symbol relocations. <https://www.akkadia.org/drepper/dsohowto.pdf>
[^maskray-plt]: Fangrui Song (MaskRay), "All about Procedure Linkage Table", 2021, updated January 2024: eager and lazy binding, `-z now`, the lookup order, `-fno-plt`, the AArch64 PLT and Mach-O stubs. <https://maskray.me/blog/2021-09-19-all-about-procedure-linkage-table>
[^maskray-got]: Fangrui Song (MaskRay), "All about Global Offset Table", 2021: when compilers use GOT indirection, preemptible symbols and visibility, and `PT_GNU_RELRO`. <https://maskray.me/blog/2021-08-29-all-about-global-offset-table>
[^maskray-reloc]: Fangrui Song (MaskRay), "Relocation overflow and code models", 2023, updated December 2025: relocation overflow errors, AArch64 range extension thunks and the AArch64 code models. <https://maskray.me/blog/2023-05-14-relocation-overflow-and-code-models>
[^bendersky-pic]: Eli Bendersky, "Position Independent Code (PIC) in shared libraries", 3 November 2011: the problems of load-time relocation and the fixed offset between text and data that PIC relies on. <https://eli.thegreenplace.net/2011/11/03/position-independent-code-pic-in-shared-libraries/>
[^hellosilicon]: HelloSilicon, GitHub repository `below/HelloSilicon`: notes that Darwin does not generally support statically linked executables, that `-lSystem` adds `LC_MAIN`, and that clang always builds position-independent executables on macOS. <https://github.com/below/HelloSilicon>
[^apple-as]: Apple, *OS X Assembler Reference*, "Assembler Directives" (archived documentation): `.subsections_via_symbols` and dead-code stripping. <https://developer.apple.com/library/archive/documentation/DeveloperTools/Reference/Assembler/040-Assembler_Directives/asm_directives.html>
