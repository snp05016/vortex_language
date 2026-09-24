# B4. Linking and loading

<p class="page-intro">A compiled object file cannot run by itself: the names it uses are still only names, and its code has no place in memory yet. This chapter follows one program's compiled pieces through both jobs that fix that, linking and loading, and into the mechanism, the procedure linkage table and the global offset table, that lets a program call code that is not even present until it starts.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 30 minutes · Builds on: [B3. Object files and assemblers](b3-object-files.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does a relocation ask the linker to do?"

        Patch one specific spot in an object file's code or data with an
        address the compiler could not know yet, such as the address of a
        page that holds a global.

        Introduced in [A2. Reading and writing AArch64 assembly, "Reaching a global table"](a2-aarch64-assembly.md#reaching-a-global-table).

    ??? question "What does an ABI fix, so two independently compiled pieces of code can call each other correctly?"

        Exactly where each argument goes, where the result comes back, and
        which registers a callee must leave as it found them.

        Introduced in [A4. Calling conventions and ABIs, "A contract with no compiler in the room"](a4-calling-conventions.md#a-contract-with-no-compiler-in-the-room).

    ??? question "What is a veneer?"

        A short stub a linker inserts, within reach of a branch instruction
        whose real target is too far away, that reloads the full target
        address and jumps to it indirectly.

        Introduced in [A2. Reading and writing AArch64 assembly, "Branches and loops"](a2-aarch64-assembly.md#branches-and-loops).

    ??? question "In the machine model, what is memory?"

        One flat array of bytes, each with its own number, an address. An
        instruction that names an address is naming a place in that array.

        Introduced in [A1. The machine model, "Memory as one long row of bytes, and how an address is computed"](a1-machine-model.md#memory-as-one-long-row-of-bytes-and-how-an-address-is-computed).

!!! goals "In this chapter"

    - Explain why a compiled object file cannot run on its own, and name the two jobs, linking and loading, that turn it into a running program.
    - Trace a call to an imported function through the procedure linkage table and the global offset table, on its first call and on every later one.
    - Distinguish position-independent code from a position-independent executable, and say what each buys.
    - Compute whether a call's displacement fits an AArch64 branch instruction, and say what the linker does when it does not.
    - Connect linking and loading to Vortex: what crosses a link boundary when a compiled program calls into its runtime.

## What a linker adds that a compiler alone cannot

Take a two-piece program: one file defines a function named `report` that
prints a value, and a second file's `main` calls it. Compiling each file
separately produces two object files, and neither one is a program you can
run. `main.o`'s symbol table lists `report` as **undefined**: a name the
file uses but does not define, next to a relocation that says where in
`main`'s own code that name's final address belongs. `writer.o` lists
`report` as **defined**, at some offset inside its own `.text` section.
Running `main.o` on its own fails immediately, because the instruction meant
to call `report` has nothing at that spot yet: whatever produced the object
file left the slot as a placeholder, trusting a later step to fill it in.

That later step is what a **linker** does. It reads every object file (and,
as the next section covers, every archive) named on the command line, builds
one combined symbol table, and for every undefined reference finds the
matching definition and rewrites its relocation with the real address, now
that the linker knows where in the final file every piece landed. It also
merges same-named sections from every input file into one: every `.text`
from every object file becomes one `.text` in the output. That merging is
why a symbol's final address depends on the whole set of inputs, not only
on the file that defines it: moving `writer.o` earlier or later on the
command line changes every address that comes after it.

None of this changes what any instruction computes. Linking decides where
code and data end up and how one piece of code finds another; it never
touches an instruction's own arithmetic. A `fmadd` that Vortex's strict
floating-point rules require to stay a single fused operation is exactly as
fused after linking as it was in the object file the back end wrote; only
its address moved.

## Static linking: archives and the search that finds them

A **static library** (a `.a` file on both platforms this book targets) is
not itself one object file: it is an **archive**, a table of contents over
many `.o` members, most of which a given program will never need. If a
linker pulled in every member of a C library merely because the program
links against it, a `printf`-only program would carry the code for
`scanf`, an allocator, and hundreds of other functions it never calls.
Archives avoid that by being searched **on demand**: a member is pulled into
the link only if it currently defines a symbol that is still undefined.[^levine][^taylor]

"Currently" matters. Pulling in one member can turn symbols *it* references
into new undefined symbols, so a single pass through the archive can reach a
member too early, find nothing it needs yet, and move on, even though a
later member in the same pass creates the need that member would have
satisfied. The example below models exactly that: an undefined-symbol set,
and an archive searched to a fixed point, not only once, so that an archive
whose members are not listed in perfect dependency order still resolves.

--8<-- "includes/examples/backend/b4-linking-and-loading/symbol_resolution.cpp.md"

It prints:

```text
--8<-- "examples/backend/b4-linking-and-loading/symbol_resolution.expected"
```

`extra.o`, which defines `flush`, sits before `iolib.o`, the member that
needs it. The first pass pulls in `mathlib.o` and `iolib.o`, and only
afterward notices that `flush` is now undefined; `extra.o` has to wait for a
second pass, even though it was already scanned once. A linker that gave up
after one pass, the way the oldest Unix linkers did, would leave `flush`
unresolved and fail the link, which is the historical reason library order
on a command line can matter at all: `-lA -lB` links only if every need `A`
creates is satisfied by something later on the line.

??? check "Why does the archive search above need more than one pass?"

    Because pulling in `iolib.o` creates a new undefined reference,
    `flush`, only after `extra.o`, the member that defines it, has already
    been scanned and skipped in the same pass. A second pass revisits
    `extra.o` with the updated undefined set and pulls it in.

## Dynamic linking: one copy of the code, many programs

Static linking copies every needed piece of code straight into the output
file. A **shared library** (a `.so` on Linux, a `.dylib` on macOS) instead
stays a separate file, and the operating system maps the same physical pages
of it into every process that uses it: a hundred programs linked against the
same C library share one copy of its code in memory, and a fix to that
library reaches every one of them without re-linking anything that uses
it.[^drepper] On macOS 27, ordinary programs link dynamically against
`libSystem` by default and receive its fixes the same way.[^hellosilicon]

The cost of that sharing is a level of indirection. A statically linked call
to `report` is a direct branch to a fixed offset, decided once and for all
at link time. A call to a function that lives in a shared library cannot be
a direct branch to one fixed address, because the library's code might load
at a different address in every process, or even at a different address on
every run of the same program, which the next two sections explain in turn:
first the mechanism that makes the indirection affordable, then the
property, position independence, that makes a variable load address
possible at all.

## The procedure linkage table and the global offset table

[A2](a2-aarch64-assembly.md#reaching-a-global-table) already used one
**global offset table (GOT)** slot, for a global *variable* whose address
the linker could not bake into the instructions: two instructions computed
the table's own address, and a load read the variable's real address out of
it. Calling a function that lives in a shared library needs the same idea,
plus one more piece, because a function call can defer the cost of finding
that address until the function is called for the first time.

The extra piece is the **procedure linkage table (PLT)**: for every function
a program imports from a shared library, the linker writes a small stub, a
handful of instructions, that the program calls instead of calling the
function directly. The stub's job is to read a GOT slot and jump to
whatever address is there. Nothing here is unusual yet; it is what fills
that GOT slot, and when, that is called **lazy binding**.[^maskray-plt]

At link time, the GOT slot for an unresolved import does not hold the
function's address at all: it holds the address of a small **resolver**
built into the loader. The first time the program calls the function, the
PLT stub reads the GOT slot, finds the resolver, and jumps to it. The
resolver looks up the function's real address by name (a search the last
section of this chapter comes back to), then does two things: it
overwrites the GOT slot with that real address, and it jumps on to the
function, so even the first call still reaches its target. Every later
call reads the same GOT slot, which by then holds the real address, and
jumps straight there. No resolver, no lookup: the same cost a statically
linked call always had.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Two calls to an imported function, print, through the PLT and GOT" aria-describedby="b4-plt-desc">
<title id="b4-plt-title">Two calls to an imported function through the PLT and GOT</title>
<desc id="b4-plt-desc">Two rows, each showing a call to print through a PLT stub and a GOT slot. First call: call print() reaches print at plt, which reads GOT of print, currently set to the resolver. The resolver looks up print in the runtime, rewrites GOT of print to point at print directly, and jumps on to print. Second call: call print() again reaches print at plt, which reads GOT of print, now set directly to print, and jumps straight there with no resolver involved.</desc>
<text class="vx-text-accent" x="20" y="18">First call</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 9">
<rect class="vx-box" x="20" y="30" width="110" height="44" rx="4"/>
<text class="vx-mono" x="75" y="57" text-anchor="middle">call print()</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 9">
<rect class="vx-box" x="160" y="30" width="100" height="44" rx="4"/>
<text class="vx-mono" x="210" y="57" text-anchor="middle">print@plt</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 9">
<rect class="vx-box-bad" x="290" y="30" width="150" height="44" rx="4"/>
<text class="vx-mono" x="365" y="52" text-anchor="middle">GOT[print]</text>
<text class="vx-text-muted" x="365" y="68" text-anchor="middle">= resolver</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 9">
<rect class="vx-box" x="290" y="122" width="150" height="44" rx="4"/>
<text class="vx-text" x="365" y="140" text-anchor="middle">dynamic linker's</text>
<text class="vx-text" x="365" y="156" text-anchor="middle">resolver</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 9">
<rect class="vx-box-accent" x="590" y="30" width="150" height="44" rx="4"/>
<text class="vx-text" x="665" y="48" text-anchor="middle">print</text>
<text class="vx-text-muted" x="665" y="64" text-anchor="middle">(in the runtime)</text>
</g>
<line class="vx-line" x1="130" y1="52" x2="156" y2="52"/>
<polygon class="vx-arrowhead" points="156,47 164,52 156,57"/>
<line class="vx-line" x1="260" y1="52" x2="286" y2="52"/>
<polygon class="vx-arrowhead" points="286,47 294,52 286,57"/>
<line class="vx-line" x1="365" y1="74" x2="365" y2="118"/>
<polygon class="vx-arrowhead" points="360,118 365,126 370,118"/>
<path class="vx-line" d="M440 132 C 520 132, 560 90, 588 62"/>
<polygon class="vx-arrowhead" points="580,58 592,58 585,68"/>
<path class="vx-line" d="M300 122 C 250 100, 250 80, 288 68" style="stroke-dasharray:4 4"/>
<polygon class="vx-arrowhead" points="284,62 282,74 293,70"/>
<text class="vx-text-muted" x="150" y="105">rewrites</text>
<text class="vx-text-muted" x="150" y="119">GOT[print]</text>
<text class="vx-text-accent" x="20" y="198">Second call</text>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 9">
<rect class="vx-box" x="20" y="210" width="110" height="44" rx="4"/>
<text class="vx-mono" x="75" y="237" text-anchor="middle">call print()</text>
</g>
<g class="vx-seq" style="--vx-i: 6; --vx-n: 9">
<rect class="vx-box" x="160" y="210" width="100" height="44" rx="4"/>
<text class="vx-mono" x="210" y="237" text-anchor="middle">print@plt</text>
</g>
<g class="vx-seq" style="--vx-i: 7; --vx-n: 9">
<rect class="vx-box-accent" x="290" y="210" width="150" height="44" rx="4"/>
<text class="vx-mono" x="365" y="232" text-anchor="middle">GOT[print]</text>
<text class="vx-text-muted" x="365" y="248" text-anchor="middle">= print</text>
</g>
<g class="vx-seq" style="--vx-i: 8; --vx-n: 9">
<rect class="vx-box-accent" x="590" y="210" width="150" height="44" rx="4"/>
<text class="vx-text" x="665" y="228" text-anchor="middle">print</text>
<text class="vx-text-muted" x="665" y="244" text-anchor="middle">(in the runtime)</text>
</g>
<line class="vx-line" x1="130" y1="232" x2="156" y2="232"/>
<polygon class="vx-arrowhead" points="156,227 164,232 156,237"/>
<line class="vx-line" x1="260" y1="232" x2="286" y2="232"/>
<polygon class="vx-arrowhead" points="286,227 294,232 286,237"/>
<line class="vx-line" x1="440" y1="232" x2="586" y2="232"/>
<polygon class="vx-arrowhead" points="586,227 594,232 586,237"/>
<text class="vx-text-muted" x="510" y="220" text-anchor="middle">no resolver</text>
</svg>
<figcaption>Figure 1. The same imported function, <code>print</code>, called twice. The first call finds an unresolved GOT slot, detours through the resolver, and leaves the slot rewritten. The second call reads the now-resolved slot and reaches the target directly, with no resolver in the path.</figcaption>
</figure>

A program that imports a hundred functions and calls ninety of them never
pays the resolver's cost for the ten it does not call: their GOT slots
stay unresolved for the life of the program. That is the saving
lazy binding buys, at the price of every first call being slower than every
later one, worth remembering the next time a benchmark's first iteration
looks anomalous for reasons that have nothing to do with the code being
measured. The next example models the state machine above directly, for two
imports and five calls between them, printing which path each call took and
nothing else: no real address appears anywhere in this book's example
output.

--8<-- "includes/examples/backend/b4-linking-and-loading/lazy_binding.cpp.md"

It prints:

```text
--8<-- "examples/backend/b4-linking-and-loading/lazy_binding.expected"
```

Eager binding is available too, usually as a linker or loader option: every
GOT slot is resolved before the program's `main` ever runs, trading a
slower start for a security property, that every GOT slot is fixed and
read-only before any of the program's own code executes, closing off a
class of exploit that overwrites a GOT slot while the program is
running.[^maskray-got]

??? check "A program calls an imported function 1000 times in a loop. Roughly how many of those calls pay the resolver's cost, with lazy binding?"

    One. The first call rewrites the GOT slot; every one of the remaining
    999 reads the already-resolved slot and jumps straight to the target,
    at the same cost a direct call would have had.

## Position-independent code, and position-independent executables

A shared library gains nothing from being mapped into many processes at
once if every process needs it at a different address, which happens
whenever whatever else is already mapped differs from process to process,
unless the library's own code does not care what address it ends up at.
Code that runs correctly no matter where in memory it is placed is called
**position-independent code (PIC)**.
[A2](a2-aarch64-assembly.md#reaching-a-global-table)'s `adrp`/`add` pair is
already an example: it computes a global's address relative to the current
instruction, the program counter, never as an absolute number baked into
the instruction stream, so the same bytes work whether that code sits at
one address or another.[^bendersky-pic]

A **position-independent executable (PIE)** takes the same property one
step further and applies it to the program's own main executable, not only
the libraries it loads. Without PIE, an executable's own code and data sit
at a fixed address chosen once, at link time, forever, every time it runs.
With PIE, the loader picks that base address anew on every run, usually at
random, which is what **address space layout randomization (ASLR)** means.
Placing code and data unpredictably does not make a program's own logic any
more correct, but it makes many memory-corruption exploits considerably
harder to write, because an attacker's payload can no longer assume a
function or a stack buffer sits at one fixed, known address.[^bendersky-loadtime]

PIE has a cost worth naming honestly: every access to a global now goes
through the `adrp`/`add` sequence, or the GOT-indirect one for data the
linker cannot place at a fixed offset at all, instead of the one instruction
that would carry an absolute address directly. On a target that starts
every process as a PIE by default, as macOS 27 does, that cost is
what "a global" always compiles to; there is no non-PIE code path left to
compare it against.[^hellosilicon]

??? check "Why does calling a function in a shared library need a GOT slot, when a call to a function in the same executable does not?"

    A direct branch works only when the distance between call site and
    target is fixed at link time. A shared library's own load address can
    differ from process to process, or from run to run under ASLR, so the
    distance to a function inside it is not fixed; the GOT slot is filled
    in once that address is finally known, at load time.

## When an address will not fit: relocation overflow and veneers

Every relocatable field in an instruction has a fixed width, and AArch64's
unconditional branch-and-link instruction, `bl`, is no exception: it
encodes its target as a 26-bit signed count of 4-byte instruction words, so
the byte displacement from the call site to the target must land within
$\pm 2^{27}$ bytes, about 128 MiB, and must itself be a multiple of
4.[^maskray-reloc] A linker computes that displacement only once it knows
the final address of both the call site and the target, which for a large
program, or for a call that crosses from the main executable into a
far-away shared library, can be larger than any single relocation field was
ever built to hold. This is a **relocation overflow**, and it is a link-time
error, not a compile-time one: nothing about the call instruction the
compiler emitted was wrong, only the distance between where the two pieces
ended up.

When a linker meets an out-of-range `bl`, it does not have to fail the
link. [A2](a2-aarch64-assembly.md#branches-and-loops) already named the fix,
a **veneer**: a short stub, placed within reach of the original call site,
that loads the target's full address into a scratch register (AAPCS64
reserves `x16`/`x17`, the intra-procedure-call registers, for exactly this)
and branches to it indirectly. The original `bl` is redirected to the
veneer instead of the real target, and the veneer's own indirect branch has
no range limit at all, because it is not encoded as a fixed-width
offset.[^lld][^maskray-reloc]

The next example computes the check a linker runs before deciding whether a
call needs a veneer: whether a displacement fits `bl`'s signed, 4-byte-scaled
26-bit field.

--8<-- "includes/examples/backend/b4-linking-and-loading/relocation_overflow.cpp.md"

It prints:

```text
--8<-- "examples/backend/b4-linking-and-loading/relocation_overflow.expected"
```

A back end that only ever targets a small program, as Vortex's does through
this book, will rarely meet this in practice: 128 MiB of code is a great
deal of code. It becomes routine once a program grows well past that size,
which is exactly why compilers expose a choice of **code model**: a promise
to the linker about how large the program and its data will be, so the
linker can choose smaller, cheaper relocation sequences when the promise is
"small" and fall back to the general, veneer-tolerant ones only when it is
not.[^maskray-reloc]

??? check "The compiler emitted a correct bl instruction, and the link still fails with a relocation overflow. Whose bug is this?"

    Neither the compiler's nor, usually, the programmer's. The instruction
    was correct for the displacement the compiler could see at compile
    time; the overflow only exists once the linker places both pieces in
    the final file and finds them farther apart than one `bl` can reach.
    The fix is a veneer, or a different code model, not a change to the
    call itself.

## What the loader does before your program's first instruction runs

Everything so far in this chapter happens once, at link time, and is baked
into the file on disk. One job is left for the moment the program
starts: a **loader** (`ld.so` on Linux, `dyld` on macOS) maps the
executable's and every shared library's segments into the new process's
address space, applies whatever relocations depend on addresses not known
until this moment (this is where a PIE's own base address, picked for this run, gets
folded into every relocation that needed it), sets every GOT slot to either
its resolver or its final address depending on whether binding is lazy or
eager, and only then transfers control to the program's entry point.

Recent macOS releases changed how the middle step, applying relocations, is
done. Instead of the loader walking a list of relocation records and
writing each one out before the program starts, **chained fixups** store
the fixups as a linked list threaded through the pages that need them,
which the kernel can apply lazily, one page at a time, exactly when that
page is first faulted in, rather than all at once at launch.[^wwdc22-link]
The result for this chapter's purposes is the same either way: every
address a relocation named ends up correct before any code that reads it
runs. It is a reminder that "the loader" names a job, not one fixed
algorithm, and that the job keeps changing as launch-time performance keeps
mattering more.

## For Vortex

!!! vortex "Exercise"

    Every Vortex program from [stage 6](../compiler/guide/stage-6-first-machine-code.md)
    onward compiles at least one call to a function it does not define:
    `print`. That call is already, at the machine level, exactly the kind
    of undefined reference this chapter's first example resolves, and once
    Vortex's runtime is linked as a shared library instead of a static one,
    it is also the kind of call the procedure linkage table's stub and
    resolver stand in front of.

    Take a Vortex program that compiles and links successfully today. Build
    it twice: once linked against the runtime as it is built now, and once
    against a second build of the same runtime, an intentionally different
    binary (add one unused function to it, or reorder its object files, so
    its layout differs), with the same interface, the same functions, the
    same signatures, still exported. Do not recompile the Vortex-generated
    object file between the two links.

    Then check, for each link, which calls in the resulting binary go
    through a stub. On Linux, `objdump -d` shows an imported call next to
    `@plt`; on macOS, look in the `__stubs` section, the same names
    [A2](a2-aarch64-assembly.md) showed you how to read. Record whether
    that differs between a debug build and a release build, and between
    linking the runtime statically and dynamically.

    Then, without building anything more, look at one specific call: the
    one that passes `&mut c` to a matmul kernel like the one
    [A4](a4-calling-conventions.md#a-contract-with-no-compiler-in-the-room)
    traced, `multiply(&a, &b, &mut c)`. Nothing about that call crosses a
    link boundary today, because `multiply` and its caller are compiled
    together. Write down which of this chapter's mechanisms, a relocation,
    a symbol-table entry, a PLT stub, would have to carry the `&mut`
    reference across a link boundary if a future Vortex let one compiled
    module call a `multiply` defined in another, and whether the array's
    compile-time shape, `[f32; 2, 3]`, needs to cross that boundary too or
    can be assumed identical on both sides.

    **Not yet.** Do not write a linker, a loader, or your own archive
    format; this exercise only reads the ones already on your machine. Do
    not add a PIE, ASLR or code-model flag to the compiler's own output:
    `c++` and its linker already choose those for you, and this exercise is
    about reading their result, not choosing it. Do not build a real
    cross-module `multiply` call: the paragraph above asks you to reason
    about what it would need, not to implement it.

    **Done when** both links, the same generated object against two
    different runtime builds, succeed and produce a program that prints the
    same output either way, and your `objdump` or `nm` evidence names, by
    symbol, which calls into the runtime are indirected through a stub and
    which are not, for at least one static and one dynamic link.

## Key ideas

!!! recap "You can now answer"

    - **Why can't an object file be run directly?** Its references to other pieces are still only names, and its code has no place in memory yet: a linker resolves the names, a loader picks the place.
    - **Why does searching a static archive sometimes take more than one pass?** Pulling in one member to satisfy a need can create a new undefined reference to a member earlier in the archive that an earlier pass already scanned and skipped.
    - **What is different about the first call through a lazily bound PLT stub?** It goes through a resolver that looks up the real address and rewrites the GOT slot; every later call reads that slot directly, at the same cost a direct call would have had.
    - **What makes code position-independent?** It never encodes an absolute address as an immediate; every address it needs is computed relative to the program counter or read from a table the linker or loader fills in.
    - **What does a PIE add on top of PIC?** The main executable itself, not only the libraries it loads, gets placed at an address chosen anew each run, which is what makes ASLR possible.
    - **When does a linker insert a veneer?** When a relocation's computed displacement does not fit the fixed-width field of the instruction it targets, such as `bl`'s 26-bit range on AArch64.

## Where this comes back

!!! next "You will use this again in"

    - [D1. Debug information](d1-debug-info.md): *symbol tables*, *unwinding across linked pieces*
    - [D2. JIT compilation](d2-jit.md): *loading code at run time*, *executable memory*
    - [D3. Reading real back ends](d3-real-backends.md): *how other tool-chains link and load their output*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *emitting relocations from the MC layer*
    - [E4. Testing back ends](e4-testing-backends.md): *checking linked output, not only assembly text*

## Sources and further reading

Levine's book is the classic full treatment of linking and loading and
remains the best single starting point; Taylor's series and MaskRay's posts
go deeper on the specific mechanisms this chapter only sketched.

[^levine]: John R. Levine, *Linkers and Loaders*, Morgan Kaufmann, 1999 (free manuscript chapters). <https://www.iecc.com/linker/>
[^taylor]: Ian Lance Taylor, "Linkers", a 20-part series; index at LWN.net. <https://lwn.net/Articles/276782/>
[^drepper]: Ulrich Drepper, "How To Write Shared Libraries", 10 December 2011. <https://www.akkadia.org/drepper/dsohowto.pdf>
[^maskray-plt]: Fangrui Song (MaskRay), "All about Procedure Linkage Table", 2021 (updated 2024). <https://maskray.me/blog/all-about-procedure-linkage-table>
[^maskray-got]: Fangrui Song (MaskRay), "All about Global Offset Table". <https://maskray.me/blog/all-about-global-offset-table>
[^maskray-reloc]: Fangrui Song (MaskRay), "Relocation overflow and code models". <https://maskray.me/blog/relocation-overflow-and-code-models>
[^bendersky-pic]: Eli Bendersky, "Position Independent Code (PIC) in shared libraries", 3 November 2011. <https://eli.thegreenplace.net/2011/11/03/position-independent-code-pic-in-shared-libraries/>
[^bendersky-loadtime]: Eli Bendersky, "Load-time relocation of shared libraries", 25 August 2011. <https://eli.thegreenplace.net/2011/08/25/load-time-relocation-of-shared-libraries/>
[^wwdc22-link]: Apple, WWDC22, "Link fast: Improve build and launch times". <https://developer.apple.com/videos/play/wwdc2022/110362/>
[^hellosilicon]: HelloSilicon, GitHub repository `below/HelloSilicon`. <https://github.com/below/HelloSilicon>
[^lld]: LLVM Project, "lld - The LLVM Linker". <https://lld.llvm.org/>
