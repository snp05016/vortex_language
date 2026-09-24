# A5. Stack frames

<p class="page-intro">Every call needs somewhere to put what must outlive it: a return address, the registers a function borrows and must give back, and any local that does not fit in a register. This chapter builds that place, the stack frame, instruction by instruction, and connects its shape to how large a Vortex function's local arrays can safely make it.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 40 minutes · Builds on: [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md), [A4. Calling conventions and ABIs](a4-calling-conventions.md)</p>

???+ remember "Before you start, remember"

    ??? question "What do x29 and x30 hold, by AAPCS64's own names for them?"

        x29 is the frame pointer (FP). x30 is the link register (LR), which holds the address the caller's `bl` will return to.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md#registers-by-name).

    ??? question "What does `stp x29, x30, [sp, #-16]!` do, and what kind of addressing mode is `[sp, #-16]!`?"

        It writes x29, then x30, to the two addresses starting at sp − 16, and only then sets sp to that new address. `!` marks pre-index addressing: the base register updates as a side effect of the access, after the address is computed but before or as the memory operation happens.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md#loads-stores-and-addresses).

    ??? question "What must a Vortex program do when it runs out of call stack?"

        Stop, with a `runtime error[stack]` report on standard error and exit status 101. It must never crash silently or keep running past that point.

        Introduced in [stage 9. Runtime safety](../compiler/guide/stage-9-runtime-safety.md#running-out-of-stack).

    ??? question "In what order are the elements of a Vortex array stored?"

        Contiguously, with no gap between elements, in row-major order: the last index varies fastest.

        Introduced in [decision 43. Memory layout](../decisions/arrays.md#d43).

!!! goals "In this chapter"

    - Explain why a function that calls another needs a frame, and name the one thing every AArch64 frame must protect for its caller.
    - Draw a frame's areas, in the order a real compiler places them, and explain why the total is always a multiple of 16 bytes.
    - Read a prologue and its matching epilogue instruction by instruction, and predict what breaks if one line of either is missing or out of order.
    - Explain the red zone: which of Vortex's targets have one, what it is for, and why a function that calls anything must give it up.
    - Connect a frame index in generated code to the stack offset a back end eventually resolves it to, and place shrink-wrapping in that process.

## A function that needs somewhere to keep things

A function that touches nothing but its own registers and calls nothing else needs no frame at all. [A2](a2-aarch64-assembly.md#the-smallest-complete-file) opened with exactly that: `add_ints`, one `add` and a `ret`, no stack traffic anywhere. The moment a function calls another function, two problems appear that registers alone cannot solve.

The first is the return address. `bl helper` puts the address of the next instruction into x30, so that `helper`'s own `ret` can find its way back. But if the caller itself needs to call something after `helper` returns, or if `helper` calls a third function, x30 gets overwritten again before the caller ever reads it. The value has to go somewhere that survives the call: memory.

The second is everything the caller was keeping in registers that it still needs after the call. AAPCS64 divides the general registers into two kinds: some, x9 to x15, a callee may clobber freely; the rest, x19 to x29, it must restore before it returns, whichever function ends up using them[^aapcs64-callee]. A value the caller keeps in a caller-saved register such as x9 does not survive `bl` unless the caller does something about it first. It has exactly two ways to survive: move into a callee-saved register, which the callee (and everything it calls) is contractually bound to hand back unchanged, or spill to memory, the same as the return address.

That memory is the **stack**: a region each thread owns, addressed by its own stack pointer, sp, and organized so that a function can claim some of it on entry and give it back on exit, all in constant time. AAPCS64 calls this discipline **full-descending**: the stack occupies falling addresses, sp always points just past the last byte in use, and claiming n more bytes means subtracting n from sp[^aapcs64-stack]. The claimed region, for one call, is that call's **frame** (Appel's older, more general term is **activation record**, from before "frame" became the default word for exactly this[^appel]). A frame does not have to hold only the return address and saved registers; it is also where a local variable lives when it does not fit in a register, or when its address is taken and something else needs to read it later.

Put both problems in one function and the frame becomes concrete. `sum_three_and_call` below computes `helper(a) + b + c + a*b`. It needs `b` and `c` to survive the call to `helper`, needs its own return address (x30) to survive it too, and needs `x29`, the frame pointer, which the next section explains:

--8<-- "includes/examples/backend/a5-stack-frames/frame_record.s.md"

Read the prologue's four instructions in order. `stp x29, x30, [sp, #-48]!` is doing two things in one instruction: subtracting 48 from sp, claiming the frame, and writing the caller's x29 and x30 into the first 16 bytes of it, at the new, lower sp. `mov x29, sp` then makes x29 point at this function's own frame, not the caller's. The two `str` instructions save x19 and d8, the registers this function is about to reuse for `b` and `c`. Only after all four instructions has the function done anything unsafe to undo: from here on, `bl helper` cannot lose anything that matters, because everything that matters is either in a callee-saved register the callee must preserve, or already written to memory this function owns.

The epilogue reverses those four steps in the opposite order: restore x19 and d8, then pop the frame record and deallocate in the same `ldp ..., [sp], #48` that started it, then `ret`, which jumps to whatever x30 (now restored) holds. A prologue and its epilogue are mirror images, and that symmetry is not a style choice. If the epilogue restored x19 before deallocating the frame, or forgot d8, the caller would get back a register it lent out and never received back: a bug that shows up nowhere near this function, in whichever other function next reads a clobbered x19 or d8.

??? check "If `sum_three_and_call` swapped the offsets its prologue and epilogue both use for x19 and d8 (`str d8, [sp, #16]` and `str x19, [sp, #24]` in the prologue, with the matching `ldr` instructions swapped the same way in the epilogue), what would change? What if only the prologue were swapped and the epilogue left as written?"

    Swapped consistently, nothing observable changes: each register is still written to, and later read back from, the same slot as itself, just a different one than before. The frame's own internal layout is this function's choice, and nothing outside the function depends on which offset holds which register.

    Swapped in the prologue alone, with the epilogue unchanged, is a real bug: `ldr x19, [sp, #16]` would now load what the prologue stored there, d8's original value, into x19, and `ldr d8, [sp, #24]` would load x19's original value into d8. The two registers come back swapped, silently, and the caller gets back the wrong value in each.

## The frame record

x29 and x30, saved together at a fixed pair of offsets, are the **frame record**. AAPCS64 requires more than that the two values be saved somewhere: it requires x29 itself to point at them, with the caller's own saved x29 at the lower address and the caller's x30 at the eight bytes above it[^aapcs64-frame]. Every frame record built this way points at the frame record below it, all the way down to the first call in the program, which AAPCS64 marks with a zero instead of another address[^aapcs64-frame]. A debugger walks that chain to print a backtrace, and it needs no debug information to do it: `x29` alone is enough to find every caller, and every caller's caller.

Apple's arm64 ABI leans on that chain harder than AAPCS64 requires. It asks that x29 "always address a valid frame record", with one exception: "leaf functions or tail calls may opt not to create an entry in this list"[^apple-frame]. The reward is unconditional: "stack traces are always meaningful, even without debug information"[^apple-frame]. This matters more for Vortex than it might first appear. [Stage 9](../compiler/guide/stage-9-runtime-safety.md) fills a program with runtime checks, and every one of them can call a reporting function on its failing path. A crash, or a check that fires somewhere unexpected, is far easier to read from `lldb bt` when every frame in the chain is walkable, and it costs the compiler nothing extra: a function that already calls something else has to build a real frame record for the reasons above, whether or not the platform requires it.

`helper`, in the example above, is exactly the exception the Apple ABI describes. It calls nothing, uses only caller-saved registers, and needs neither a return-address save nor a frame record: `add w0, w0, #1` and `ret` are the whole function. A leaf function's frame, when it has one at all, holds only its own locals.

## Frame layout: what goes where

Nothing in AAPCS64 says where inside a frame the frame record, the saved registers or the locals must sit, only that a frame record, once built, is found at the address x29 holds[^aapcs64-frame]. The order in `sum_three_and_call` (record, then saved registers, then a local) is Apple clang's choice for this function, not a rule. A frame typically has room for up to four kinds of things, and a real compiler decides, function by function, which of them it needs:

- the **frame record**: saved FP and LR, present whenever the function calls anything, or (on Apple's targets) whenever the platform's stricter rule asks for one anyway;
- the **callee-saved area**: every callee-saved register the function reuses for its own purposes, one slot per register, saved once on entry and restored once on exit, regardless of how many times the function runs a loop or makes a call in between;
- **locals and spill slots**: named variables whose address is taken, that do not fit in a register, or that a register allocator ran out of registers for, plus any local array, which almost always lives here because indexing a register is not an operation any of these ISAs has;
- the **outgoing argument area**, when this function itself calls another with more arguments than fit in argument registers, or with arguments AAPCS64 places on the stack regardless (variadic arguments, on Apple's targets[^aapcs64-callee]): a block this function's own prologue reserves so the call sites within it need only write into it, not adjust sp on every call.

x86-64 orders things differently again. The SysV AMD64 psABI's own figure of a stack frame puts the return address and the caller's saved rbp just above the current frame, exactly as AAPCS64's frame record does, but everything below that, locals, spills, and outgoing arguments, is addressed relative to rbp with no fixed layout the ABI names[^bendersky]. [B2](b2-x86-64.md) picks up x86-64's own frame conventions, including the option most compilers now take of skipping rbp entirely and addressing everything from rsp[^bendersky].

Figure 1 draws `sum_three_and_call`'s frame at the three moments that matter: on entry, before any of this has happened; after the prologue, with the frame built and the call about to happen; and after the epilogue, which undoes it.

<figure class="vx-figure">
<div class="vx-stepper">
<div class="vx-step">
<p><strong>Step 1. On entry.</strong> Nothing has run yet. sp and x29 both still point into the caller's own frame; the 48 bytes this function is about to claim are not yet its own.</p>
<svg viewBox="0 0 640 300" role="img" aria-label="Step 1: on entry to sum_three_and_call. A box labelled the caller's frame occupies the top of the diagram. Arrows for sp and x29 both point at its lower edge, the boundary below which nothing has been claimed yet.">
<defs><marker id="a5-f1a-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="220" y="30" width="200" height="60" rx="4"/>
<text class="vx-text" x="236" y="64" text-anchor="middle">caller's frame</text>
<line class="vx-line" x1="220" y1="90" x2="150" y2="90" marker-end="url(#a5-f1a-head)"/>
<text class="vx-text-accent" x="60" y="84" text-anchor="middle">sp, x29</text>
<text class="vx-text-muted" x="440" y="94">nothing claimed below here</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 2. After the prologue.</strong> 48 bytes now belong to this function, in three 16-byte bands: the frame record at the lowest address, then the saved registers, then the local and its padding. x29 and sp both point at the record, its lowest address. The 128 bytes below sp are Apple's red zone, but this function cannot use them: it is about to call <code>helper</code>, whose own prologue is free to write there.</p>
<svg viewBox="0 0 640 300" role="img" aria-label="Step 2: after the prologue, about to call helper. The caller's frame sits at the top. Below it, sum_three_and_call's own 48-byte frame has three bands, high address to low: product and padding at offset 32, x19 and d8 at offset 16, and the frame record, x29 and x30, at offset 0. sp and x29 both point at offset 0, the record's own address. Below that is a hatched band labelled the 128-byte red zone, off limits because this function calls out.">
<defs><marker id="a5-f1b-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="220" y="14" width="200" height="40" rx="4"/>
<text class="vx-text-muted" x="236" y="38" text-anchor="middle">caller's frame</text>
<rect class="vx-box" x="220" y="54" width="200" height="44" rx="2"/>
<text class="vx-mono" x="236" y="80">product, padding</text>
<text class="vx-text-muted" x="430" y="80">#32</text>
<rect class="vx-box" x="220" y="98" width="200" height="44" rx="2"/>
<text class="vx-mono" x="236" y="124">x19, d8</text>
<text class="vx-text-muted" x="430" y="124">#16</text>
<rect class="vx-box-strong" x="220" y="142" width="200" height="44" rx="2"/>
<text class="vx-mono" x="236" y="168">x29, x30 (record)</text>
<text class="vx-text-muted" x="430" y="168">#0</text>
<line class="vx-line" x1="220" y1="186" x2="150" y2="186" marker-end="url(#a5-f1b-head)"/>
<text class="vx-text-accent" x="60" y="180" text-anchor="middle">sp, x29</text>
<rect class="vx-box-bad" x="220" y="186" width="200" height="40" rx="2"/>
<line class="vx-line" x1="232" y1="196" x2="252" y2="216"/>
<line class="vx-line" x1="252" y1="196" x2="272" y2="216"/>
<line class="vx-line" x1="272" y1="196" x2="292" y2="216"/>
<line class="vx-line" x1="292" y1="196" x2="312" y2="216"/>
<line class="vx-line" x1="312" y1="196" x2="332" y2="216"/>
<line class="vx-line" x1="332" y1="196" x2="352" y2="216"/>
<line class="vx-line" x1="352" y1="196" x2="372" y2="216"/>
<line class="vx-line" x1="372" y1="196" x2="392" y2="216"/>
<line class="vx-line" x1="392" y1="196" x2="412" y2="216"/>
<text class="vx-text-muted" x="430" y="210">red zone</text>
<text class="vx-text-muted" x="236" y="250">128 bytes, off limits: helper() may use its own</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 3. The epilogue, undoing it.</strong> <code>ldr x19</code>, then <code>ldr d8</code>, then <code>ldp x29, x30, [sp], #48</code>: the saves come back in the same order, and the frame is deallocated in the instruction that reads the record out of it. sp and x29 return to exactly where step 1 found them.</p>
<svg viewBox="0 0 640 300" role="img" aria-label="Step 3: the epilogue has run. The 48-byte frame from step 2 is drawn with a dashed outline, showing it is deallocated. sp and x29 are back at the boundary below the caller's frame, the same position as step 1.">
<defs><marker id="a5-f1c-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="220" y="30" width="200" height="60" rx="4"/>
<text class="vx-text" x="236" y="64" text-anchor="middle">caller's frame</text>
<rect class="vx-box" x="220" y="90" width="200" height="96" rx="2" style="stroke-dasharray:6 5" fill="none"/>
<text class="vx-text-muted" x="236" y="144" text-anchor="middle">deallocated</text>
<line class="vx-line" x1="220" y1="90" x2="150" y2="90" marker-end="url(#a5-f1c-head)"/>
<text class="vx-text-accent" x="60" y="84" text-anchor="middle">sp, x29</text>
<text class="vx-text-muted" x="440" y="94">back where step 1 found them</text>
</svg>
</div>
</div>
<figcaption>Figure 1. <code>sum_three_and_call</code>'s frame, at three moments: on entry, after the prologue and after the epilogue. Offsets are written as the assembly writes them, counting up from sp.</figcaption>
</figure>

??? check "Why must the total size of a frame be a multiple of 16 bytes on AArch64, and not merely at the moment of a call?"

    AAPCS64 states the rule twice: once for every access, and again for calls specifically. "At any point at which memory is accessed via SP... SP mod 16 = 0"[^aapcs64-stack], a hardware requirement the processor itself enforces, not only a convention checked at call boundaries. A frame whose size were not a multiple of 16 would leave sp misaligned for every load and store inside the function, not only for the calls it makes.

Vortex's fixed-shape arrays make this arithmetic concrete rather than abstract. A local `[f32; 8, 8]` tile, the kind [P8](../optimize/p8-cache-blocking.md) blocks a larger matrix into, is 256 bytes: two 16-byte bands' worth of nothing but that one array, before the function has saved a single register. The stage 10 kernel's own arrays, `a`, `b` and `c`, are `&[f32; 64, 64]` and `&mut [f32; 64, 64]`: references, four or eight bytes each, not the 16 KiB the arrays themselves would occupy if `multiply` took them by value instead of by reference. A Vortex function's frame size depends on which of its fixed-shape arrays are local and which arrive as references, exactly the distinction [A4](a4-calling-conventions.md) draws between passing a value and passing its address.

## The red zone

A leaf function, one that calls nothing, can go further than skipping its frame record: on the targets that offer it, it can skip building a frame at all. The **red zone** is the memory just below sp that the platform promises not to touch on its own, so a function that never calls anything, and therefore never causes anything else to run at a lower address than its own sp, may use that space for temporaries without ever moving sp.

Apple's arm64 ABI states the size plainly: "the 128 bytes immediately below the stack pointer," which "Apple platforms don't modify... during exceptions," so that "user-mode programs can rely on the bytes below the stack pointer to not change unexpectedly"[^apple-redzone]. The x86-64 SysV psABI grants the same 128 bytes, for the same reason: it calls the area "reserved," not to "be modified by signal or interrupt handlers," so that "leaf functions may use this area for their entire stack frame, rather than adjusting the stack pointer"[^sysv-redzone]. Both ABIs withdraw the promise the moment a function calls anything: Apple's guidance is direct, "the caller must assume that the callee modifies the contents of its red zone. The caller must therefore create a proper stack frame"[^apple-redzone]. `sum_three_and_call`, from the first section, is exactly that caller: it calls `helper`, so its own red zone is not its to use, and it builds the 48-byte frame Figure 1 draws instead.

AAPCS64, the ABI Linux arm64 uses, promises nothing of the kind: nowhere does it reserve bytes below sp the way Apple's and SysV's documents do. A leaf function compiled for Linux arm64 that needs scratch space either keeps everything in registers or moves sp like any other function would. The next example is Apple-only for exactly this reason:

--8<-- "includes/examples/backend/a5-stack-frames/leaf_red_zone.s.md"

`average_red_zone` never touches sp. Its two `stur` instructions write below it, into the red zone, and the matching `ldur` instructions read the values straight back; `stur` and `ldur` are the unscaled forms [A2](a2-aarch64-assembly.md#loads-stores-and-addresses) introduced, needed here because the offsets are negative. Nothing about this particular function requires spilling `a` and `b` at all, since both fit comfortably in registers for the rest of the computation; it does so anyway to show the technique in isolation. A real compiler reaches for the red zone under register pressure: a leaf function with more live values than registers can hold spills into its red zone first, before it would ever consider moving sp for a function that calls nothing else.

??? check "A Vortex program prints an `f64` accumulator midway through a loop, then keeps accumulating into it. Why can this not use the red zone, even on Apple's targets, even though the loop itself calls nothing?"

    `print` is itself a call. The function containing this loop calls `print`, so by Apple's own rule it must "create a proper stack frame" rather than rely on its red zone, and its own red zone becomes unavailable the moment it makes that call, exactly as `sum_three_and_call`'s is. The accumulator has to survive the call the ordinary way: in a callee-saved register, such as one of AAPCS64's v8-v15, of which only the low 64 bits are guaranteed callee-saved[^aapcs64-callee], or spilled to this function's own frame.

## Frame indices and shrink-wrapping

Everything so far has been a frame a person laid out by hand. A compiler builds the same kind of frame automatically, but not all at once: it decides what needs stack space long before it decides where in the frame that space goes. LLVM's IR represents a stack allocation with `alloca`, which "allocates memory on the stack frame of the currently executing function, to be automatically released when this function returns"[^llvm-alloca], and nothing in that instruction says at what offset. Instruction selection and register allocation both refer to such a slot only by a **frame index**, an opaque placeholder; only after register allocation has finished, and the compiler finally knows how many callee-saved registers the function needs to save, does it compute real offsets and rewrite every frame index into an sp-relative address[^llvm-codegen]. The next example shows three `alloca`s of different sizes going into one AArch64 function:

--8<-- "includes/examples/backend/a5-stack-frames/alloca_frame.ll.md"

On the owner's machine, `llc -O2` (LLVM 18.1.8, arm64-apple-macosx, 2026-09-24) turned this into a 32-byte frame with no frame record at all, since `mix` is itself a leaf:

```gas
sub     sp, sp, #32
stp     w0, w1, [sp, #24]   ; the 2-element array, offset 24
str     w2, [sp, #20]       ; the single i32, offset 20
str     x8, [sp, #8]        ; the i64 (x8 holds sext(c)), offset 8
add     sp, sp, #32
```

Two things are worth noticing. First, offsets 12 to 19 are unused: the 2-element array needs 4-byte alignment and the doubleword needs 8, so the back end left an 8-byte gap between them rather than pack them tighter, the same kind of padding Figure 1's own local band carried. Second, the function's final answer is computed entirely in registers (`add w9, w0, w1` and so on, not shown above), because `llc`'s own instruction selector recognized that each load was reading back a value it had just stored, and forwarded the register directly; the stores themselves still happen, because nothing in this raw `.ll` file, run through `llc` alone rather than a full optimizing pipeline, proves the memory is truly dead. A load from a frame index is not always this cheap to eliminate, which is exactly why the offsets, and the padding between them, are worth reading straight from the tool rather than assuming.

One more decision happens after offsets are chosen: not every function needs its callee-saved area on every path through it. **Shrink-wrapping**, introduced by Chow for exactly this purpose, moves the register saves and restores off a function's entry and exit and onto the specific edges that need them, so that a path through the function that never uses a saved register never pays for saving it[^chow]. In LLVM this runs as its own machine-level pass, late in the back end, after register allocation has settled which registers need saving and frame indices have become real offsets: the last step between "this function needs a frame" and the actual bytes of prologue and epilogue a linker will see.

## Reading a real frame

The frame in Figure 1 was hand-written to make its four saves easy to point at. A compiler's own frame, for a much smaller function, looks different mainly in what it leaves out. `average3` below keeps its three parameters in a three-element array rather than three scalar locals, so that even at `-O0`, which keeps every local in memory instead of a register, the array has to be real:

--8<-- "includes/examples/backend/a5-stack-frames/locals.cpp.md"

Compiled with Apple clang 21 at `-O0 -S -fno-stack-protector -target arm64-apple-macos` (2026-09-24), `average3` becomes:

```gas
sub     sp, sp, #32
str     s0, [sp, #28]        ; a arrives in s0, spilled here
str     s1, [sp, #24]        ; b in s1
str     s2, [sp, #20]        ; c in s2
ldr     s0, [sp, #28]
str     s0, [sp, #8]         ; values[0] = a
ldr     s0, [sp, #24]
str     s0, [sp, #12]        ; values[1] = b
ldr     s0, [sp, #20]
str     s0, [sp, #16]        ; values[2] = c
        ; ... the loop that sums values[0], [1] and [2] into sp+4 goes here ...
ldr     s0, [sp, #4]
fmov    s1, #3.00000000
fdiv    s0, s0, s1
add     sp, sp, #32
ret
```

`average3` calls nothing, so like `helper`, it needs no frame record: `sub sp, sp, #32` and the matching `add` are the whole of its prologue and epilogue. All 32 bytes are locals: three 4-byte slots for the incoming arguments as they land, the three-element `values` array they get copied into, a slot for the running `sum`, and one for the loop counter `i`, adding up exactly to the 32 bytes reserved, with none left over. That the frame is this tidy is somewhat lucky; the general lesson is the one from the previous section, that padding for alignment can appear anywhere a compiler mixes differently sized locals, and the only way to know exactly where is to read the listing a real compiler wrote.

??? check "`average3` is a leaf function, yet at `-O0` it still spends nine instructions moving values between stack slots before it does any arithmetic. Why does `-O0` do this, and what would change at a higher optimization level?"

    `-O0` is deliberately not clever: it gives every source-level local a fixed stack slot and reads and writes that slot for every use, so that the generated code maps directly onto the source and a debugger can find any variable by name at any line. A higher optimization level promotes locals whose address is never taken into registers wherever they fit, the same way `mix` in the previous section had its loads and stores collapsed into direct register use.

## For Vortex

!!! vortex "Exercise"

    **Build** the piece of your back end that decides which of a function's values need frame space at all, and lays out the frame for the ones that do, on top of the calling-convention work from [A4](a4-calling-conventions.md) and whatever instruction-selection or register-allocation state your compiler already tracks per function.

    1. A per-function list of what needs stack space: every local whose address is taken or that does not fit in a register, every callee-saved register the function's own body uses, and, if the function itself calls something with stack-passed arguments, the outgoing argument area those calls need.
    2. A frame-record decision: build one whenever the function calls anything; on AArch64, save x29 and x30 together at a fixed offset and point x29 at them, exactly as AAPCS64 requires. A function that calls nothing may skip it.
    3. An offset for everything on the list, keeping like-sized things together is enough for a first version, respecting each value's natural alignment, and padding the total up to a multiple of 16.
    4. A prologue that claims the frame in as few instructions as your target allows, and an epilogue that is its exact mirror image: same registers, opposite order.
    5. One frame layout per function is enough to start; do not chase per-call-site savings yet.

    **Not yet:** the red zone (it saves a handful of bytes and only on Apple's targets; wait until everything else is measured and working); shrink-wrapping (a real optimization, not part of a first back end); spilling driven by a real register allocator ([C5](c5-spilling.md) covers what a spill even is); anything for x86-64 until [B2](b2-x86-64.md).

    **Proof that it works:**

    - Every stage 0 to 10 golden test still passes once functions route their locals and calls through this frame layout instead of whatever ad hoc storage came before it.
    - A function with a local fixed-shape array large enough to force real stack use (an `[f32; 8, 8]` or bigger) produces correct output, and its frame size, read from your own compiler's generated assembly, is a multiple of 16.
    - `lldb bt`, run on a program stopped inside a call at least three functions deep, prints every frame's name. On Apple's targets this is a direct test of the frame-record rule; document what you did to make it true on Linux arm64 too.
    - A function that calls another, and keeps a value alive across that call, produces the same result whether that value happens to end up in a callee-saved register or spilled to the stack: vary which by hand, in your compiler's own frame layout, and check both.
    - A table, filled in from your compiler, with the date and its version: frame size, in bytes, for the stage 10 `multiply` kernel, for a function with one local `[f32; 8, 8]` tile, and for the leaf `helper`-style function this chapter used to introduce a frame with nothing in it.

## Key ideas

!!! recap "Questions you can now answer"

    - **Why does a function that calls another need a frame, even if every value involved would otherwise fit in registers?** Its own return address and any value it needs after the call cannot survive in a caller-saved register across `bl`, and a callee-saved register can only be reused if its old value is saved somewhere first.
    - **What is a frame record, and what must AArch64 code do to walk one?** The saved FP and LR of one call, with FP pointing at them; walking the chain means following each frame's saved FP to the one below it, down to a zero that marks the first call.
    - **Why must a leaf function on Apple's targets still sometimes build a frame record?** It must not, if it truly calls nothing; the exception AAPCS64 and Apple both name is exactly "leaf functions or tail calls," and any function that calls anything else, even once, falls outside it.
    - **What is the red zone, and which of Vortex's targets have one?** 128 bytes below sp that a leaf function may use without moving sp, present on Apple's arm64 ABI and on x86-64's SysV ABI, absent from AAPCS64, the ABI Linux arm64 uses.
    - **Why is a frame's size always a multiple of 16 bytes on AArch64?** AAPCS64 requires SP mod 16 = 0 at every memory access through sp, a hardware-checked rule, not only a convention observed at calls.
    - **What is a frame index, and when does it become a real offset?** A placeholder for a stack slot whose location is not yet decided; it becomes an sp-relative offset only after register allocation, once the compiler knows exactly which callee-saved registers the function needs to save.
    - **What does shrink-wrapping change, and what does it leave alone?** It moves register saves and restores onto only the edges that need them, so an unused path through a function pays nothing for a save the rest of the function needs; it does not change what gets saved, only where.

## Where this comes back

!!! next "You will use this again in"

    - [A4. Calling conventions and ABIs](a4-calling-conventions.md): *the outgoing argument area*, *which registers a call clobbers*
    - [B1. The simplest back end that works](b1-simplest-backend.md): *a frame layout simple enough to hand-write*, *the prologue and epilogue as fixed templates*
    - [B2. A second target: x86-64](b2-x86-64.md): *rbp versus rsp-relative frames*, *the SysV red zone*
    - [C5. Spilling, splitting and rematerialization](c5-spilling.md): *spill slots as part of the frame*, *what a spill costs beyond the store itself*
    - [D1. Debug information](d1-debug-info.md): *the frame record as an unwind table's simplest case*, *walking frames without debug info*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *frame indices*, *PrologEpilogInserter*, *shrink-wrapping*

## Sources and further reading

Read AAPCS64's own sections on the stack and the frame pointer first; they are short, and every other source here assumes them. Apple's ARM64 guide is the one page that states the red zone and the frame-record rule in plain language, with the exact exception for leaf functions. Bendersky's post is the clearest walk through an x86-64 frame for contrast with AArch64's. Appel's chapter is the classic name for all of this, "activation records," from before compiler writers mostly said "frame." Chow's paper is short and worth reading end to end for shrink-wrapping, not only skimmed for the one idea this chapter borrows from it.

[^aapcs64-frame]: ARM, "Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)", release 2025Q4, section "The Frame Pointer", read 2026-09-24. <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
[^aapcs64-callee]: ARM, "Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)", release 2025Q4, sections naming r19-r29 and v8-v15 as callee-saved (with only the low 64 bits of v8-v15 guaranteed) and requiring every variadic argument on the stack on Apple's targets, read 2026-09-24. <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
[^aapcs64-stack]: ARM, "Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)", release 2025Q4, sections "Universal stack constraints" and "Stack constraints at a public interface", read 2026-09-24. <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
[^apple-frame]: Apple, "Writing ARM64 code for Apple platforms", section "The Frame Pointer", read 2026-09-24. <https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms>
[^apple-redzone]: Apple, "Writing ARM64 code for Apple platforms", section "Respect the stack's red zone", read 2026-09-24. <https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms>
[^sysv-redzone]: x86-64 psABI Committee, "System V Application Binary Interface, AMD64 Architecture Processor Supplement", the description of the 128-byte area beyond RSP and the red zone, read 2026-09-24. <https://gitlab.com/x86-psABIs/x86-64-ABI>
[^appel]: Andrew W. Appel, *Modern Compiler Implementation* (ML/Java/C), Cambridge University Press, 1998, chapter "Activation Records". <https://www.cs.princeton.edu/~appel/modern/toc.html>
[^bendersky]: Eli Bendersky, "Stack frame layout on x86-64", 6 September 2011. <https://eli.thegreenplace.net/2011/09/06/stack-frame-layout-on-x86-64>
[^chow]: Frederick Chow, "Minimizing register usage penalty at procedure calls", *Proceedings of the ACM SIGPLAN 1988 Conference on Programming Language Design and Implementation (PLDI '88)*. <https://doi.org/10.1145/53990.53999>
[^llvm-codegen]: LLVM Project, "The LLVM Target-Independent Code Generator", section "Stack Frame Layout", read 2026-09-24. <https://llvm.org/docs/CodeGenerator.html>
[^llvm-alloca]: LLVM Project, "LLVM Language Reference Manual", section "'alloca' Instruction", read 2026-09-24. <https://llvm.org/docs/LangRef.html#alloca-instruction>
