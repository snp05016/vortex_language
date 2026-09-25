# D2. JIT compilation

<p class="page-intro">A program can write machine code into its own memory and call it, with no object file and no linker in between. This chapter covers what that takes on AArch64: page permissions and the write-xor-execute rule, Apple's MAP_JIT, instruction-cache maintenance, and calling back into the host. It then shows what a JIT must redo that the toolchain used to do for it, which is what Vortex will need when it auto-tunes kernels.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 35 minutes · Builds on: [B3. Object files and assemblers](b3-object-files.md), [B4. Linking and loading](b4-linking-and-loading.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does a relocation record, and who resolves it?"

        Which bytes are unfinished, which symbol will finish them, and how to
        combine the symbol's final address with what is already in those
        bytes. A linker resolves every relocation once it has decided where
        each piece of the program will live.

        Introduced in [B3. Object files and assemblers](b3-object-files.md#relocations-the-holes-themselves).

    ??? question "Under AAPCS64, where do a function's first two integer arguments go, and where does its result come back?"

        The first eight general-purpose arguments go in `x0` through `x7`, in
        order, so the first two land in `x0` and `x1`. A general-purpose
        result comes back in `x0`.

        Introduced in [A4. Calling conventions and ABIs](a4-calling-conventions.md#where-arguments-go-two-counters-not-one).

    ??? question "How long is every AArch64 instruction, and what decides its bits?"

        Exactly one 32-bit word. A few fixed bits pick the instruction class,
        and the rest are that class's fields: registers, immediates, shift
        amounts.

        Introduced in [A1. The machine model](a1-machine-model.md#encoding-an-instruction-is-a-fixed-pattern-of-bits).

    ??? question "How far can a `bl` reach, and what does a linker do when the target is farther?"

        About 128 MiB either way: its 26-bit field counts words. When the
        target is out of range, the linker inserts a veneer (a thunk) that
        loads the full address into `x16` and branches through it.

        Introduced in [B4. Linking and loading](b4-linking-and-loading.md#when-a-call-cannot-reach-thunks-and-code-models).

!!! goals "In this chapter"

    - Encode a short instruction sequence by hand, place it in executable memory and call it through a function pointer.
    - Explain why no page is writable and executable at once, and use both routes around that rule: `mprotect` on Linux and macOS, `MAP_JIT` with a per-thread switch on Apple silicon.
    - Explain why freshly written instructions need an instruction-cache invalidation on AArch64, and name the instructions that perform it.
    - Generate a stub that calls back into the host program, and list the other jobs of a linker, loader and debugger that a JIT must take over.
    - Plan how a Vortex auto-tuner would compile, load and time kernel variants inside one process, and how to measure what that saves.

## A function that did not exist a moment ago

Every back end chapter so far has ended the same way: bytes land in a file,
a linker joins that file with others, a loader maps the result into a new
process, and only then does anything run. A **just-in-time compiler**, or
**JIT**, is a program that creates new machine code while it runs and then
runs that code itself. Eli Bendersky's working definition is that any program
which produces and runs executable code that was not part of it on disk is a
JIT[^bendersky]. There is no file and no linker in between.

Here is the smallest version, worked by hand. The function `x + y` for two
64-bit integers needs two AArch64 instructions: `add x0, x0, x1` and `ret`.
Asking `llvm-mc` for their encodings gives the bytes[^llvm-mc]:

```text
add x0, x0, x1     // encoding: [0x00,0x00,0x01,0x8b]
ret                // encoding: [0xc0,0x03,0x5f,0xd6]
```

AArch64 stores words little-endian, lowest byte first, so these are the words
`0x8b010000` and `0xd65f03c0`. [A1](a1-machine-model.md#encoding-an-instruction-is-a-fixed-pattern-of-bits)
took the first one apart: the top byte `0x8b` says "64-bit add, shifted
register", and the `1` in the middle is `Rm`, the register `x1`. A program
that copies these eight bytes into memory it is allowed to execute, and then
calls that address as a function of type `int64_t(int64_t, int64_t)`, has
compiled `x + y` just in time. The example below does exactly that and prints
`7`:

--8<-- "includes/examples/backend/d2-jit/jit_call.cpp.md"

The cast to a function pointer near the end does real work. When the C++
compiler compiles `add(3, 4)` through that pointer, it follows AAPCS64: `3`
goes in `x0`, `4` in `x1`, a `blr` jumps to the buffer, and the result is read
from `x0` afterwards[^aapcs64]. Those are the registers the two hand-written
instructions use. The generated code does not get to invent its own calling
convention: at the boundary with compiled code, it has to honour the one the
caller already follows.

JITs are common. Apple's porting guide gives a web browser turning script
code into machine code as its example[^apple-jit]; LLVM's ORC documentation
lists the debugger LLDB evaluating expressions, language runtimes such as
JVMs and Julia, and interactive interpreters such as Cling for C++[^orcv2].
For Vortex the reason is narrower. Auto-tuning, on the
roadmap for after v0.1, means generating many variants of one
kernel, timing each and keeping the fastest. Doing that inside one process
means each variant costs a write into memory instead of an object file, a
link and a process launch. Figure 1 sets the two routes side by side.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Two routes from generated code to a running function: through files, a linker and a loader, or inside one process">
<text class="vx-text" x="20" y="28" font-weight="600">Ahead of time (B1 to B4)</text>
<rect class="vx-box" x="20" y="44" width="110" height="52" rx="5"/>
<text class="vx-text" x="75" y="67" text-anchor="middle">compiler</text>
<text class="vx-text-muted" x="75" y="85" text-anchor="middle" font-size="11">writes .s / .o</text>
<rect class="vx-box" x="160" y="44" width="110" height="52" rx="5"/>
<text class="vx-text" x="215" y="67" text-anchor="middle">assembler</text>
<text class="vx-text-muted" x="215" y="85" text-anchor="middle" font-size="11">relocations left open</text>
<rect class="vx-box" x="300" y="44" width="110" height="52" rx="5"/>
<text class="vx-text" x="355" y="67" text-anchor="middle">linker</text>
<text class="vx-text-muted" x="355" y="85" text-anchor="middle" font-size="11">resolves symbols</text>
<rect class="vx-box" x="440" y="44" width="110" height="52" rx="5"/>
<text class="vx-text" x="495" y="67" text-anchor="middle">loader</text>
<text class="vx-text-muted" x="495" y="85" text-anchor="middle" font-size="11">maps text as RX</text>
<rect class="vx-box-strong" x="580" y="44" width="160" height="52" rx="5"/>
<text class="vx-text" x="660" y="67" text-anchor="middle">new process runs</text>
<text class="vx-text-muted" x="660" y="85" text-anchor="middle" font-size="11">debugger reads the file</text>
<line class="vx-line" x1="130" y1="70" x2="160" y2="70"/>
<line class="vx-line" x1="270" y1="70" x2="300" y2="70"/>
<line class="vx-line" x1="410" y1="70" x2="440" y2="70"/>
<line class="vx-line" x1="550" y1="70" x2="580" y2="70"/>
<text class="vx-text" x="20" y="150" font-weight="600">Just in time (this chapter)</text>
<rect class="vx-box-bad" x="20" y="164" width="720" height="118" rx="8"/>
<text class="vx-text-muted" x="36" y="184" font-size="11">one running process, one address space</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 3">
<rect class="vx-box" x="50" y="196" width="170" height="60" rx="5"/>
<text class="vx-text" x="135" y="220" text-anchor="middle">generator</text>
<text class="vx-text-muted" x="135" y="240" text-anchor="middle" font-size="11">encodes words, fills in addresses</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 3">
<rect class="vx-box-accent" x="290" y="196" width="190" height="60" rx="5"/>
<text class="vx-text" x="385" y="220" text-anchor="middle">code buffer</text>
<text class="vx-text-muted" x="385" y="240" text-anchor="middle" font-size="11">RW, then RX; cache invalidated</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 3">
<rect class="vx-box-strong" x="550" y="196" width="170" height="60" rx="5"/>
<text class="vx-text" x="635" y="220" text-anchor="middle">call through a pointer</text>
<text class="vx-text-muted" x="635" y="240" text-anchor="middle" font-size="11">AAPCS64 at the boundary</text>
</g>
<path class="vx-flow" d="M 220 226 L 290 226"/>
<path class="vx-flow" d="M 480 226 L 550 226"/>
</svg>
<figcaption>Figure 1. The two routes from generated code to a running function. Ahead of time, four tools and a file system sit between the compiler and the first instruction executed, and each one does a job: the linker resolves names, the loader sets page permissions, the file tells a debugger what the bytes are. A JIT keeps everything inside one process, so it must do each of those jobs itself.</figcaption>
</figure>

## Pages, permissions and W^X

Why does `jit_call.cpp` call `mmap` and `mprotect` rather than put its eight
bytes in an array? Because an ordinary array is not executable. The
processor's memory-management unit keeps **permission bits** for every page,
the fixed-size block in which memory is mapped: may it be read, written,
executed? Heap and stack pages are readable and writable but not executable.
If the processor tries to fetch an instruction from such a page, it faults.

A JIT needs memory it can write and then execute. The obvious request, one
page with both permissions, is exactly what attackers want: if a bug lets them
place bytes in memory that is writable and executable, those bytes run. The
rule against it is called **W^X**, "write xor execute": a page may be writable
or executable, never both at once. Bendersky's introduction follows it with
two calls[^bendersky]. First `mmap` a region as readable and writable and copy
the instructions in; then `mprotect` the same region to readable and
executable.

Both calls work in whole pages. `mprotect` requires its address to be aligned
to a page boundary[^mprotect], and `mmap` returns page-aligned memory, so the
eight bytes of `jit_call.cpp` occupy a page of their own. On this book's
machine (Apple M4 Pro, macOS 27.0, September 2026), `getconf PAGESIZE`
prints `16384`. A JIT that makes many small functions therefore carves them
out of larger regions rather than mapping a page per function.

macOS on Apple silicon enforces the rule directly. On the same machine, a
small test program that asked `mmap` for a readable, writable and executable
anonymous region was refused with `EACCES` (permission denied), and so was an
`mprotect` that tried to add execute permission to a writable page. The
two-step route, RW then RX, worked without any entitlement in a command-line
tool that does not opt into Apple's hardened runtime.

??? check "A program maps its buffer with `PROT_READ | PROT_WRITE | PROT_EXEC` in one call and never calls `mprotect`. It runs on some Linux machine. What is wrong with it?"

    It holds a page that is writable and executable at the same time, which
    is what W^X forbids: any bug that lets an attacker write into that buffer
    becomes arbitrary code execution. It is also not portable. On macOS on
    Apple silicon that request fails with `EACCES` unless the region is
    mapped with `MAP_JIT`, and the next section shows that even then no
    thread may write and execute it at the same moment.

## Apple silicon: MAP_JIT and a per-thread switch

`mprotect` changes a page for every thread in the process at once. A JIT
that compiles on one thread while another thread runs earlier output would
have to stop the second thread whenever the first one writes. Apple silicon
offers a different mechanism, and Apple's porting guide describes
it[^apple-jit]:

1. Map the region once with `mmap` and the flag `MAP_JIT`.
2. Before writing, call `pthread_jit_write_protect_np(0)`. The region becomes
   writable and not executable **for the calling thread only**.
3. Copy the instructions in, then call `pthread_jit_write_protect_np(1)`. The
   region is executable and not writable again for that thread.
4. Call `sys_icache_invalidate` on the new bytes before running them (the next
   section explains why).

The guide states the rule plainly: with this protection enabled, a thread
cannot write to a region and execute from it at the same time, and Apple
silicon enables it for all apps[^apple-jit].

--8<-- "includes/examples/backend/d2-jit/jit_map_jit.cpp.md"

The rule is enforced, and each half fails with a bus error, `SIGBUS`. On the
machine above, a variant of this example that called into the region while
its thread was still in the writable state was killed with `SIGBUS`, and so
was one that wrote to the region after switching back to executable. The
correct order ran and printed `12`.

Which apps need permission? Apple's **hardened runtime** is an opt-in mode
for signed apps that refuses to run code in any page without a valid code
signature. A JIT's output has no signature, so a hardened app must carry the
`com.apple.security.cs.allow-jit` **entitlement** (a signed permission in the
app's code signature) before `mmap` will accept `MAP_JIT`; such an app may
create only one `MAP_JIT` region. An app that does not adopt the hardened
runtime needs no entitlement to use `MAP_JIT`[^apple-jit]. The plain
`mprotect` route in a hardened app needs a broader exception,
`com.apple.security.cs.allow-unsigned-executable-memory`, which Apple
warns exposes an app to the common vulnerabilities of memory-unsafe
languages[^apple-unsigned].

Apple's guide now goes one step further. With the
`com.apple.security.cs.jit-write-allowlist` entitlement, an app writes only
through `pthread_jit_write_with_callback_np`, which switches the calling
thread to writable, runs a callback from a fixed list, and switches back. The
callback must treat its input as attacker-controlled and check the
instructions before copying them. On macOS, an app with that entitlement can
no longer call `pthread_jit_write_protect_np`[^apple-jit]. The examples here
use the simpler switch because they are unsigned command-line tools; a JIT
that ships inside a hardened app should follow the allowlist design.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="Two ways to keep a code buffer never writable and executable at once: the whole page changes with mprotect, or each thread's view changes with MAP_JIT">
<text class="vx-text" x="20" y="26" font-weight="600">mprotect route: the page itself changes, for every thread</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 4">
<rect class="vx-box" x="20" y="40" width="130" height="46" rx="5"/>
<text class="vx-mono" x="85" y="68" text-anchor="middle">RW: write</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4">
<rect class="vx-box-accent" x="200" y="40" width="130" height="46" rx="5"/>
<text class="vx-mono" x="265" y="68" text-anchor="middle">RX: call</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4">
<rect class="vx-box" x="380" y="40" width="130" height="46" rx="5"/>
<text class="vx-mono" x="445" y="68" text-anchor="middle">RW: rewrite</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 4">
<rect class="vx-box-accent" x="560" y="40" width="130" height="46" rx="5"/>
<text class="vx-mono" x="625" y="68" text-anchor="middle">RX: call</text>
</g>
<line class="vx-line" x1="150" y1="63" x2="200" y2="63"/>
<line class="vx-line" x1="330" y1="63" x2="380" y2="63"/>
<line class="vx-line" x1="510" y1="63" x2="560" y2="63"/>
<text class="vx-text-muted" x="175" y="104" text-anchor="middle" font-size="11">mprotect</text>
<text class="vx-text-muted" x="355" y="104" text-anchor="middle" font-size="11">mprotect</text>
<text class="vx-text-muted" x="535" y="104" text-anchor="middle" font-size="11">mprotect</text>
<text class="vx-text" x="20" y="150" font-weight="600">MAP_JIT route: one mapping, each thread's view changes</text>
<text class="vx-text" x="20" y="190">thread 1 (compiles)</text>
<rect class="vx-box-accent" x="200" y="170" width="130" height="36" rx="5"/>
<text class="vx-mono" x="265" y="193" text-anchor="middle">X</text>
<rect class="vx-box" x="380" y="170" width="130" height="36" rx="5"/>
<text class="vx-mono" x="445" y="193" text-anchor="middle">W: write(0)</text>
<rect class="vx-box-accent" x="560" y="170" width="130" height="36" rx="5"/>
<text class="vx-mono" x="625" y="193" text-anchor="middle">X: write(1)</text>
<text class="vx-text" x="20" y="244">thread 2 (runs code)</text>
<rect class="vx-box-accent" x="200" y="224" width="490" height="36" rx="5"/>
<text class="vx-mono" x="445" y="247" text-anchor="middle">X the whole time: keeps calling finished code</text>
<rect class="vx-box-bad" x="20" y="280" width="720" height="36" rx="5"/>
<text class="vx-text" x="380" y="303" text-anchor="middle">Never granted: W and X for one thread at one moment (EACCES, or SIGBUS on use)</text>
</svg>
<figcaption>Figure 2. Two ways to obey W^X. With <code>mprotect</code>, the page's own permissions flip between RW and RX, and every thread sees the change. With <code>MAP_JIT</code>, the mapping stays put and each thread's view flips: thread 1 makes the region writable for itself only while it writes (<code>pthread_jit_write_protect_np(0)</code>, shown as <code>write(0)</code>), while thread 2 keeps executing from it. Neither route ever lets one thread write and execute the same bytes at once.</figcaption>
</figure>

??? check "Thread 1 calls `pthread_jit_write_protect_np(0)` and starts writing a new function into the `MAP_JIT` region. Thread 2 is in the middle of running a finished function from the same region. What happens to thread 2?"

    Nothing. The switch changes the region's permissions for the calling
    thread only, so thread 2 still sees it as executable and keeps running.
    With `mprotect`, the same write would have removed execute permission
    for every thread, and thread 2 would fault on its next instruction fetch
    from that page.

## Keeping the instruction cache honest

Permission bits answer one question: may this page hold running code? They
do not answer another: has the processor already cached something else for
this address? AArch64 cores typically keep separate level-1 caches for
instructions and for data. A `memcpy` of new instructions is a series of data stores; it
goes through the data cache and does not update the instruction cache. Arm's
architecture does not require data writes and instruction fetches to be
coherent, though it allows implementations that make them so[^bramley].
Apple's guide says Apple silicon's instruction caches are not coherent with
its data caches, and that code must call `sys_icache_invalidate` before
running instructions on a recently updated page[^apple-jit].

The instructions behind that call are worth knowing, because every AArch64
JIT runs them. The level where instruction fetches and data accesses see the
same copy of memory is the **point of unification**, usually a level-2 cache;
the architecture does not fix where it is[^bramley]. Getting new instructions
from the data side to the instruction side takes five steps per cache line.
Jacob Bramley's Arm article gives the sequence[^bramley], and the Linux
AArch64 implementation of `__clear_cache` in LLVM's compiler-rt runs
the same one[^compiler-rt]:

| Instruction | What it does |
| --- | --- |
| `dc cvau, xN` | Clean the data-cache line at address `xN` to the point of unification: write the new bytes down to where instruction fetch can see them |
| `dsb ish` | Wait until that write has completed |
| `ic ivau, xN` | Invalidate the instruction-cache line at `xN` to the point of unification: throw away any stale copy |
| `dsb ish` | Wait until the invalidation has completed |
| `isb` | Discard instructions the core has already fetched, so the next ones are fetched again |

compiler-rt reads the cache-line sizes from the `CTR_EL0` register and loops
over the range one line at a time. The same register has two bits, IDC and
DIC, that let a core declare the cleaning or the invalidation unnecessary,
and compiler-rt skips each loop when its bit is set[^compiler-rt]. Figure 3
steps through the sequence for the `add`-to-`sub` rewrite used later in this
chapter.

<figure class="vx-figure">
<div class="vx-stepper">
<div class="vx-step">
<p><strong>Step 1: the store.</strong> The JIT copies <code>sub</code>'s word, <code>0xcb010000</code>, over <code>add</code>'s. The store lands in the data cache. The instruction cache still holds the old word from the last call, and so does the level-2 cache.</p>
<svg viewBox="0 0 700 250" role="img" aria-label="Step 1: the new word is in the level-1 data cache; the level-1 instruction cache and the level-2 cache still hold the old word">
<rect class="vx-box" x="20" y="20" width="660" height="120" rx="8"/>
<text class="vx-text-muted" x="36" y="40" font-size="11">one core</text>
<rect class="vx-box-bad" x="50" y="56" width="260" height="66" rx="5"/>
<text class="vx-text" x="180" y="80" text-anchor="middle">L1 instruction cache</text>
<text class="vx-mono" x="180" y="104" text-anchor="middle">0x8b010000 (stale add)</text>
<rect class="vx-box-accent" x="390" y="56" width="260" height="66" rx="5"/>
<text class="vx-text" x="520" y="80" text-anchor="middle">L1 data cache</text>
<text class="vx-mono" x="520" y="104" text-anchor="middle">0xcb010000 (new sub)</text>
<rect class="vx-box" x="180" y="170" width="340" height="60" rx="5"/>
<text class="vx-text" x="350" y="193" text-anchor="middle">L2: point of unification</text>
<text class="vx-mono" x="350" y="215" text-anchor="middle">0x8b010000 (old)</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 2: <code>dc cvau</code>, then <code>dsb ish</code>.</strong> The data-cache line is cleaned: its new contents are written down to the point of unification. The barrier waits for that to finish. The instruction cache is still stale.</p>
<svg viewBox="0 0 700 250" role="img" aria-label="Step 2: the new word has been written from the data cache to the level-2 cache; the instruction cache is still stale">
<rect class="vx-box" x="20" y="20" width="660" height="120" rx="8"/>
<text class="vx-text-muted" x="36" y="40" font-size="11">one core</text>
<rect class="vx-box-bad" x="50" y="56" width="260" height="66" rx="5"/>
<text class="vx-text" x="180" y="80" text-anchor="middle">L1 instruction cache</text>
<text class="vx-mono" x="180" y="104" text-anchor="middle">0x8b010000 (stale add)</text>
<rect class="vx-box-accent" x="390" y="56" width="260" height="66" rx="5"/>
<text class="vx-text" x="520" y="80" text-anchor="middle">L1 data cache</text>
<text class="vx-mono" x="520" y="104" text-anchor="middle">0xcb010000 (clean)</text>
<path class="vx-flow" d="M 520 122 L 470 170"/>
<rect class="vx-box-accent" x="180" y="170" width="340" height="60" rx="5"/>
<text class="vx-text" x="350" y="193" text-anchor="middle">L2: point of unification</text>
<text class="vx-mono" x="350" y="215" text-anchor="middle">0xcb010000 (new)</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 3: <code>ic ivau</code>, then <code>dsb ish</code>.</strong> The instruction-cache line for that address is invalidated. The stale <code>add</code> is gone; nothing has replaced it yet.</p>
<svg viewBox="0 0 700 250" role="img" aria-label="Step 3: the instruction cache line is now empty; the data cache and level-2 cache hold the new word">
<rect class="vx-box" x="20" y="20" width="660" height="120" rx="8"/>
<text class="vx-text-muted" x="36" y="40" font-size="11">one core</text>
<rect class="vx-box" x="50" y="56" width="260" height="66" rx="5"/>
<text class="vx-text" x="180" y="80" text-anchor="middle">L1 instruction cache</text>
<text class="vx-text-muted" x="180" y="104" text-anchor="middle">(line invalid)</text>
<rect class="vx-box-accent" x="390" y="56" width="260" height="66" rx="5"/>
<text class="vx-text" x="520" y="80" text-anchor="middle">L1 data cache</text>
<text class="vx-mono" x="520" y="104" text-anchor="middle">0xcb010000</text>
<rect class="vx-box-accent" x="180" y="170" width="340" height="60" rx="5"/>
<text class="vx-text" x="350" y="193" text-anchor="middle">L2: point of unification</text>
<text class="vx-mono" x="350" y="215" text-anchor="middle">0xcb010000</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 4: <code>isb</code>, then the call.</strong> The barrier throws away anything the core fetched early. The call misses in the instruction cache, the fetch goes to L2, and the core runs <code>sub</code>.</p>
<svg viewBox="0 0 700 250" role="img" aria-label="Step 4: the instruction fetch refills the instruction cache from level 2 with the new word">
<rect class="vx-box" x="20" y="20" width="660" height="120" rx="8"/>
<text class="vx-text-muted" x="36" y="40" font-size="11">one core</text>
<rect class="vx-box-accent" x="50" y="56" width="260" height="66" rx="5"/>
<text class="vx-text" x="180" y="80" text-anchor="middle">L1 instruction cache</text>
<text class="vx-mono" x="180" y="104" text-anchor="middle">0xcb010000 (sub)</text>
<rect class="vx-box-accent" x="390" y="56" width="260" height="66" rx="5"/>
<text class="vx-text" x="520" y="80" text-anchor="middle">L1 data cache</text>
<text class="vx-mono" x="520" y="104" text-anchor="middle">0xcb010000</text>
<path class="vx-flow" d="M 230 170 L 180 122"/>
<rect class="vx-box-accent" x="180" y="170" width="340" height="60" rx="5"/>
<text class="vx-text" x="350" y="193" text-anchor="middle">L2: point of unification</text>
<text class="vx-mono" x="350" y="215" text-anchor="middle">0xcb010000</text>
</svg>
</div>
</div>
<figcaption>Figure 3. Making one rewritten instruction visible to instruction fetch, step by step. The store only reaches the data cache. Cleaning pushes it down to the level both sides share, invalidating removes the stale copy from the instruction cache, and the final barrier makes the core fetch again. Skip the middle steps and the core may run the old <code>add</code>.</figcaption>
</figure>

Portable code does not write these instructions by hand. GCC and Clang
provide `__builtin___clear_cache(begin, end)`. GCC's manual says it flushes
the instruction cache for that range on targets that need it and has no
effect on targets that do not[^gcc-clearcache]. On Apple platforms
compiler-rt's `__clear_cache` calls `sys_icache_invalidate`; on x86-64 it
does nothing, with a comment that Intel processors keep instruction and data
caches unified[^compiler-rt]. That is why `jit_call.cpp` calls the builtin
right after `mprotect` and before the first call: it is correct on every
target and free where it is not needed.

??? check "A test JITs one function into a fresh buffer, never invalidates the instruction cache, and passes every time on an M4. Why is that not evidence that the invalidation is unnecessary?"

    Because there was probably nothing stale to fetch. A buffer that has
    never run code is unlikely to have an old line for that address in the
    instruction cache, so the first fetch goes past it. The failure needs a rewrite:
    old instructions cached from an earlier call, then new bytes at the same
    address. Even then it depends on whether the old line is still cached,
    so a test can pass by luck. Apple's guide makes the call a requirement
    for recently updated pages, not a matter for testing to decide.

## Rewriting one buffer

Nothing about the buffer in `jit_call.cpp` is used up after one call. The
same address can hold a different function a moment later, as long as every
write repeats the whole sequence: make it writable, copy, make it executable,
invalidate. `jit_recompile.cpp` writes `add x0, x0, x1; ret`, calls it, then
writes `sub x0, x0, x1; ret` over the same bytes and calls the same function
pointer again. `sub`'s word, `0xcb010000`, differs from `add`'s in one bit:
bit 30, the `op` field of the layout in A1.

--8<-- "includes/examples/backend/d2-jit/jit_recompile.cpp.md"

The second call prints `-1`, which is `3 - 4`, not `7`, because the sequence ran again
before it. Leave out a step on the second write and the failures differ in how
loud they are. Without the switch back to RW, the `memcpy` faults at once.
Without the switch to RX, the call faults at once. Without the invalidation,
nothing need fault at all: the core may run the cached `add` and print `7`
again, or it may not, depending on what the cache holds at that moment.

This is what an auto-tuner does, over and over: one buffer, many variants,
each one written, protected, invalidated, called and timed. The process stays
alive the whole time, with its inputs already in memory and its measurement
state intact.

## Calling back into the host

Real generated code does more than arithmetic on its arguments. It calls
back into the program that made it: to report a failed bounds check, to
allocate, to print. In an object file, such a call is a `bl` with a
relocation, and the linker fills in the distance to the callee
([B3](b3-object-files.md#relocations-the-holes-themselves)). A JIT has no
linker, but it has something better: it already knows the callee's final
address, because the callee is a function in its own running process. It can
write that address straight into the instructions.

It cannot always use `bl` to do it. A `bl` reaches only as far as its 26-bit
word offset allows, about 128 MiB either way (the ELF relocation for it checks
$-2^{27} \le X < 2^{27}$ bytes)[^aaelf64]. `mmap` may place the code buffer
anywhere in a 64-bit address space, far from the program's own text. The
general solution is the one a linker's veneer uses: build the full 64-bit
address in a register and branch through it. AAPCS64 names `x16` and `x17`
**IP0** and **IP1**, intra-procedure-call scratch registers that a veneer may
overwrite at a call, so no caller may expect them to survive one[^aapcs64].
That makes `x16` safe to use here.

Building 64 bits takes four instructions, because each carries a 16-bit
immediate. `movz` ("move wide with zero") writes one 16-bit slice and clears
the rest of the register; `movk` ("move wide with keep") writes one slice and
leaves the others alone. Both have the same layout, which `llvm-mc` confirms
for every word below[^llvm-mc]:

| Bits | 31 | 30-29 | 28-23 | 22-21 | 20-5 | 4-0 |
| --- | --- | --- | --- | --- | --- | --- |
| Field | `sf` = 1 (64-bit) | `opc`: `10` movz, `11` movk | `100101` | `hw`: which slice | `imm16` | `Rd` |

With `sf`, `opc` and the fixed bits in place, a `movz` starts from
`0xd2800000` and a `movk` from `0xf2800000`. Everything else is shifting and
OR-ing.

Work one by hand: `movk x16, #0xdead, lsl #16`, which writes `0xdead` into
bits 16 to 31 of `x16`. The shift of 16 is slice 1, so `hw = 1`, which sits
at bit 21: `1 << 21 = 0x00200000`. The immediate sits at bit 5:
`0xdead << 5 = 0x001bd5a0`. The register number 16 sits at bit 0: `0x10`.
Adding the parts to the base:

```text
  0xf2800000   movk, 64-bit
+ 0x00200000   hw = 1
+ 0x001bd5a0   imm16 = 0xdead
+ 0x00000010   Rd = x16
= 0xf2bbd5b0
```

`llvm-mc` prints `[0xb0,0xd5,0xbb,0xf2]` for the same instruction, which is
that word, lowest byte first. The last instruction of the stub is `br x16`,
the register-indirect branch, `0xd61f0000` with the register in bits 5 to 9:
`0xd61f0200`. Figure 4 shows how an address splits across the four words.

<figure class="vx-figure">
<svg viewBox="0 0 760 290" role="img" aria-label="A 64-bit address split into four 16-bit slices, each placed in x16 by one movz or movk instruction, followed by br x16">
<text class="vx-text" x="20" y="26" font-weight="600">Target address</text>
<rect class="vx-box-accent" x="160" y="10" width="140" height="34" rx="4"/>
<text class="vx-mono" x="230" y="32" text-anchor="middle">9abc</text>
<rect class="vx-box-accent" x="300" y="10" width="140" height="34" rx="4"/>
<text class="vx-mono" x="370" y="32" text-anchor="middle">5678</text>
<rect class="vx-box-accent" x="440" y="10" width="140" height="34" rx="4"/>
<text class="vx-mono" x="510" y="32" text-anchor="middle">dead</text>
<rect class="vx-box-accent" x="580" y="10" width="140" height="34" rx="4"/>
<text class="vx-mono" x="650" y="32" text-anchor="middle">beef</text>
<text class="vx-text-muted" x="230" y="60" text-anchor="middle" font-size="11">bits 63–48, hw = 3</text>
<text class="vx-text-muted" x="370" y="60" text-anchor="middle" font-size="11">bits 47–32, hw = 2</text>
<text class="vx-text-muted" x="510" y="60" text-anchor="middle" font-size="11">bits 31–16, hw = 1</text>
<text class="vx-text-muted" x="650" y="60" text-anchor="middle" font-size="11">bits 15–0, hw = 0</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5">
<line class="vx-line" x1="650" y1="66" x2="650" y2="88"/>
<rect class="vx-box" x="20" y="90" width="720" height="30" rx="4"/>
<text class="vx-mono" x="36" y="110">d297ddf0  movz x16, #0xbeef            x16 = 0x0000_0000_0000_beef</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5">
<rect class="vx-box" x="20" y="126" width="720" height="30" rx="4"/>
<text class="vx-mono" x="36" y="146">f2bbd5b0  movk x16, #0xdead, lsl #16   x16 = 0x0000_0000_dead_beef</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5">
<rect class="vx-box" x="20" y="162" width="720" height="30" rx="4"/>
<text class="vx-mono" x="36" y="182">f2cacf10  movk x16, #0x5678, lsl #32   x16 = 0x0000_5678_dead_beef</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5">
<rect class="vx-box" x="20" y="198" width="720" height="30" rx="4"/>
<text class="vx-mono" x="36" y="218">f2f35790  movk x16, #0x9abc, lsl #48   x16 = 0x9abc_5678_dead_beef</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 5">
<rect class="vx-box-strong" x="20" y="238" width="720" height="34" rx="4"/>
<text class="vx-mono" x="36" y="260">d61f0200  br x16                        jump; x0 to x7 untouched</text>
</g>
</svg>
<figcaption>Figure 4. The stub that <code>jit_call_host.cpp</code> generates, for the fixed address <code>0x9abc5678deadbeef</code>. Each 16-bit slice of the address goes into <code>x16</code> with one instruction, <code>movz</code> for the first and <code>movk</code> for the rest; the <code>hw</code> field picks the slice. The final <code>br</code> reaches any address, where a <code>bl</code> would reach only about 128 MiB either way.</figcaption>
</figure>

`jit_call_host.cpp` puts this together. It prints the five words for the
fixed address in Figure 4, so they can be compared with `llvm-mc`, then builds
the same stub for the address of a C++ function, `host_square`, and calls
the stub with `9`. Because the stub ends in `br` and not `blr`, it is a
**tail jump**: it does not set up a return address, so `host_square` returns
straight to the stub's caller, and `x0` still holds the argument when
`host_square` starts.

--8<-- "includes/examples/backend/d2-jit/jit_call_host.cpp.md"

??? check "The stub for `0x9abc5678deadbeef` needs `movk x16, #0x5678, lsl #32`. Work out its 32-bit word before opening the answer."

    `lsl #32` is slice 2, so `hw = 2`: `2 << 21 = 0x00400000`. The
    immediate: `0x5678 << 5 = 0x000acf00`. The register: `0x10`. Adding them
    to the `movk` base `0xf2800000` gives `0xf2cacf10`, the third line of the
    example's output. A mistake in `hw` would still assemble but would put
    `0x5678` in the wrong 16 bits, and the stub would jump to a wrong address.

## What a JIT must do for itself

Figure 1 promised that every tool the JIT skips leaves a job behind. The
stub above took over one of the linker's jobs. The table lists the rest:

| Job | Ahead of time | In a JIT |
| --- | --- | --- |
| Resolve a call to a named function | linker, from a relocation | the JIT writes the known address, as in the stub |
| Make code executable and nothing else | loader maps text read-and-execute | `mprotect` or `MAP_JIT`, then cache invalidation |
| Tell a debugger what the bytes are | the debugger reads the object file | register an in-memory object with the debugger |
| Name functions for a profiler | symbols in the executable | a symbol map written at run time |
| Pay for compilation | before the program starts | while the program waits |

Debuggers look for debug information in object files on disk, and JIT output
has none. GDB defines an interface for this: the JIT keeps a list of in-memory
object files holding DWARF, and calls a function named
`__jit_debug_register_code`, where the debugger has set a breakpoint so it can
read the new object from the process. LLDB implements the same interface; on
macOS it must be switched on with the setting
`plugin.jit-loader.gdb.enable`[^llvm-debugjit]. The DWARF in that object is
the same kind [D1](d1-debug-info.md) builds: line tables, and call frame
information so the debugger can unwind through JIT frames. Linux `perf` has a
lighter scheme: the JIT writes a text file named `/tmp/perf-<pid>.map`, one
line per function with its start address, size and name[^perf-jit].

The last row changes how a JIT's back end is built. Compile time is now run
time, so a JIT wants passes that are fast as well as good. That is the
setting Poletto and Sarkar designed linear-scan allocation for
([C3](c3-linear-scan.md)), and the budget that shapes Cranelift
([D3](d3-real-backends.md#cranelift-a-back-end-on-a-compile-time-budget)).
For a Vortex auto-tuner the budget is looser, since each variant will be
timed over many runs, but it is still paid once per variant.

## LLVM's JIT: ORC

Everything above encodes machine words by hand, which shows the mechanism
but is not how a compiler built on LLVM would do it. LLVM's JIT framework is
**ORC**, in its second design, ORCv2[^orcv2]. ORC links relocatable object
files (COFF, ELF and Mach-O) into a running process, and can compile LLVM IR
to such objects first. It aims to follow the symbol-resolution rules of static
and dynamic linkers, so that JIT'd code behaves like linked code.

Two ready-made classes cover common cases. `LLJIT` combines an IR
compilation layer with `RTDyldObjectLinkingLayer`, and compiles a symbol as
soon as its address is looked up. `LLLazyJIT` adds a `CompileOnDemandLayer`:
function bodies in a module added with `addLazyIRModule` are not compiled
until they are first called[^orcv2]. That laziness uses the same trick as the
stub above. A **lazy reexport** hands out the address of a function stub; the
first call through the stub enters the JIT, which compiles the real function
and updates the stub so later calls go straight there[^orcv2].

Below those classes, ORC's `ObjectLinkingLayer` wraps **JITLink**, a linker
that works on objects in memory. It turns an object into a graph, the `LinkGraph`, of
blocks of content and the symbols that name them, lays the blocks out, applies the relocations from
[B3](b3-object-files.md#relocations-the-holes-themselves), and can build
entries such as a global offset table along the way. Its memory manager,
`JITLinkMemoryManager`, allocates the target memory and applies memory
protections[^jitlink]: the W^X work of this chapter, done by a library.

In short, ORC still produces relocatable objects, but it links them in memory
and never writes them to a file. LLVM's Kaleidoscope tutorial, chapter 4,
wires a JIT into a toy language so that each top-level expression is
compiled and evaluated as soon as it is typed[^kal4].

## What it costs, measured

The case for a JIT in an auto-tuner is a cost comparison, so measure it
instead of assuming it. Two quantities matter, both on the machine you tune
on:

1. **One in-process variant switch.** Time the rewrite sequence of
   `jit_recompile.cpp` (RW, copy, RX, invalidate) for a buffer of the size
   of one kernel, repeated many times in a loop, and report the median.
2. **One out-of-process variant.** Time starting a separate, already-built
   executable that runs the same kernel once and exits (for example with
   `posix_spawn` and `waitpid`), and report the median. Add the time to link
   it if the variant is built fresh each time.

Use a monotonic clock, discard the first runs, and repeat until the median
stops moving, as [P1](../optimize/p1-measure-first.md) describes. Fill in
the table for your machine; this chapter does not supply numbers.

| Quantity | Median | Runs | Machine and date |
| --- | --- | --- | --- |
| RW, copy, RX, invalidate (one kernel's bytes) | | | |
| `posix_spawn` + `waitpid` of a prebuilt variant | | | |
| Link one variant with the system linker | | | |

If the second and third rows are small next to the time to run one kernel
many times, a tuner gains little from a JIT and relaunching is simpler. The
table decides.

## For Vortex

!!! vortex "Exercise"

    **Build a code buffer and load one compiled Vortex function into it.**
    Write a small utility for your compiler's test suite, outside the
    compiler's pipeline, that owns one executable region and offers two
    operations: install a sequence of instruction bytes, and return an entry
    address to call. Its interface must make a writable-and-executable state
    impossible to request: there is no call that leaves the region both
    writable and callable. Choose `mprotect` or `MAP_JIT` for macOS, write
    down why, and invalidate the instruction cache after every install.

    Then test it with bytes your own compiler produced:

    1. **Differential.** Compile a Vortex function that takes two `i32`
       parameters and returns an `i32`, using only operations that cannot
       fail at run time, such as `&`, `|` and `^`, so that it calls nothing
       and uses no globals.
       Extract its instruction bytes from the object file with the tools
       from [B3](b3-object-files.md#reading-what-the-assembler-made), and
       check there that no relocation points into those bytes. Install them,
       call the entry through a pointer whose C++ type matches AAPCS64 for
       that signature ([A4](a4-calling-conventions.md#where-arguments-go-two-counters-not-one)),
       and compare the result with the same function linked into a normal
       Vortex program, for at least ten argument pairs, including negative
       values and the largest and smallest `i32`.
    2. **Rewrite.** Install a second such function into the same region and
       check that the entry now computes the second function, not the first.
    3. **Refusal.** Give the utility a function whose object code does
       have a relocation, for example one that uses `+` if your overflow
       check calls the runtime ([stage 9](../compiler/guide/stage-9-runtime-safety.md#integer-overflow)),
       and check that it rejects it with an error rather than installing code that would
       jump through an unfilled field.

    Finally, fill in the table from "What it costs, measured" for your
    machine and keep it next to the test.

    **Not yet.** Do not make your code generator write into memory: the
    bytes come from the object file your back end already produces. Do not
    resolve relocations or build call stubs to the runtime; that is the next
    step once this one is solid. Do not add lazy compilation, threads,
    debugger registration or a perf map, and do not bring in ORC.

    **Done when** the three tests pass on macOS arm64 (and on Linux arm64,
    if your CI runs there), and each of these deliberate breakages is
    caught: the switch to executable removed (the call must fail the test,
    for example through a death test, not hang); the relocation check
    removed (test 3 must fail); and the cache invalidation removed. The
    last one cannot be caught reliably by running code, as the check
    question in "Keeping the instruction cache honest" explains, so give
    your utility a seam the test can observe, such as a counter of
    invalidations per install.

## Key ideas

!!! recap "You can now answer"

    - **What does a JIT skip, compared with B1 through B4?** The object file on disk, the linker and the loader. It writes code into its own memory and calls it directly, so it must resolve names, set permissions and inform debuggers itself.
    - **Why is no page writable and executable at once?** W^X: memory that is both lets any bug that writes attacker-chosen bytes run them. A JIT writes with the page RW, then switches it to RX before calling.
    - **What does Apple silicon add?** `MAP_JIT` plus a per-thread switch, `pthread_jit_write_protect_np`, so one thread can write while others execute; a hardened-runtime app also needs the `allow-jit` entitlement.
    - **Why must new instructions be followed by a cache invalidation on AArch64?** The instruction cache is not kept coherent with data stores; `dc cvau`, `ic ivau`, barriers and `isb` (or `__builtin___clear_cache`) make the new bytes visible to instruction fetch.
    - **How does JIT'd code call a host function anywhere in memory?** It builds the known 64-bit address in `x16` with `movz` and three `movk`, then branches with `br` or `blr`; `bl` reaches only about 128 MiB.
    - **What does `LLLazyJIT` trade?** A slower first call to each function, which compiles it through a stub, for never compiling functions that are never called.

## Where this comes back

!!! next "You will use this again in"

    - [D3. Reading real back ends](d3-real-backends.md): *a back end on a compile-time budget*
    - [P15. Choosing parameters: models or search](../optimize/p15-choosing-parameters.md): *timing generated variants*, *search inside one process*
    - [P16. Capstone: the ladder, measured](../optimize/p16-capstone.md): *comparing two measurements*, *measuring before deciding*

## Sources and further reading

Read Bendersky's article first for the whole idea in a few pages, then
Apple's porting guide for the rules on the machine this book uses. Bramley's
article is the clearest account of the cache sequence. The ORCv2 and JITLink
pages are the reference once you want LLVM to do the work.

[^bendersky]: Eli Bendersky, "How to JIT - an introduction", 5 November 2013: the definition of a JIT, and allocating RW memory with `mmap` and switching it to RX with `mprotect` (on x86-64 Linux). <https://eli.thegreenplace.net/2013/11/05/how-to-jit-an-introduction>
[^apple-jit]: Apple, "Porting just-in-time compilers to Apple silicon", Apple Developer Documentation: the hardened runtime and the `allow-jit` entitlement, one `MAP_JIT` region, the per-thread write protection enabled for all apps on Apple silicon, `pthread_jit_write_with_callback_np` and the allowlist entitlements, and `sys_icache_invalidate`. <https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon>
[^apple-unsigned]: Apple, "Allow Unsigned Executable Memory Entitlement" (`com.apple.security.cs.allow-unsigned-executable-memory`), Apple Developer Documentation. <https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.cs.allow-unsigned-executable-memory>
[^mprotect]: Linux man-pages project, `mprotect(2)`: the page-alignment requirement and the `EACCES` error. <https://man7.org/linux/man-pages/man2/mprotect.2.html>
[^bramley]: Jacob Bramley, "Caches and Self-Modifying Code", Arm Community blog (page dated 20 January 2025): the point of unification, the architecture not requiring coherence between data writes and instruction fetches, and the `dc cvau`, `dsb ish`, `ic ivau`, `dsb ish`, `isb` sequence. <https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/caches-self-modifying-code-implementing-clear-cache>
[^compiler-rt]: LLVM Project, compiler-rt, `compiler-rt/lib/builtins/clear_cache.c`: the AArch64 sequence with the `CTR_EL0` IDC and DIC checks, the call to `sys_icache_invalidate` on Apple platforms, and the empty x86 case. <https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/builtins/clear_cache.c>
[^gcc-clearcache]: GCC, "Other Builtins", GCC online documentation: `__builtin___clear_cache`. <https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html>
[^aapcs64]: Arm, "Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)", release 2025Q4: argument and result registers, and "Use of IP0 and IP1 by the linker". <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
[^aaelf64]: Arm, "ELF for the Arm 64-bit Architecture (AArch64)", release 2025Q4: the `R_AARCH64_CALL26` entry and its range check. <https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst>
[^llvm-mc]: LLVM Project, `llvm-mc` command guide: `-show-encoding`, used to check every instruction word on this page. <https://llvm.org/docs/CommandGuide/llvm-mc.html>
[^llvm-debugjit]: LLVM Project, "Debugging JIT-ed Code": the GDB JIT interface, `__jit_debug_register_code`, and LLDB's `plugin.jit-loader.gdb.enable` setting on macOS. <https://llvm.org/docs/DebuggingJITedCode.html>
[^perf-jit]: Linux kernel, `tools/perf/Documentation/jit-interface.txt`: the `/tmp/perf-%d.map` file and its line format. <https://github.com/torvalds/linux/blob/master/tools/perf/Documentation/jit-interface.txt>
[^orcv2]: LLVM Project, "ORC Design and Implementation" (ORCv2): use cases, JIT-linking of COFF, ELF and Mach-O objects, `LLJIT` and `LLLazyJIT`, and laziness through lazy reexports. <https://llvm.org/docs/ORCv2.html>
[^jitlink]: LLVM Project, "JITLink and ORC's ObjectLinkingLayer": `ObjectLinkingLayer`, the `LinkGraph`, GOT and PLT passes, and `JITLinkMemoryManager`. <https://llvm.org/docs/JITLink.html>
[^kal4]: LLVM Project, "My First Language Frontend with LLVM Tutorial", chapter 4, "Adding JIT and Optimizer Support". <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl04.html>
