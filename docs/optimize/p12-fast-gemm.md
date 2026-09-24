# P12. Anatomy of a fast GEMM

<p class="page-intro">The naive matrix-multiplication loop and a tuned BLAS run the same algorithm; the distance between them is almost entirely packing and a register-blocked micro-kernel. This chapter builds both, in the order Goto and BLIS built them, and says exactly which steps keep every bit of the naive answer and which do not.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 55 minutes · Builds on: [P8. Cache blocking](p8-cache-blocking.md), [P10. Vectorization](p10-vectorization.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does a 2 by 2 register block save over a plain scalar inner loop, and where do the saved values live?"

        Each step of k loads two values of `a` and two of `b` for four multiply-adds instead of four loads for four multiply-adds: one load per multiply-add instead of two. The four running sums stay in registers for the whole `k` loop, not in `c`.

        Introduced in [P7. Loop transformations](p7-loop-transformations.md#unroll-and-jam-and-register-blocks).

    ??? question "Why can a Vortex compiler mark a matrix kernel's `&mut c` parameter `noalias`?"

        References 9.8 makes it a theorem, not a guess: while `c` is lent as `&mut`, it may be used only through that reference and may not appear in any other argument of the same call, so storage reached through it cannot be reached through `a` or `b` either.

        Introduced in [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md#promises-the-front-end-writes-down).

    ??? question "Why does a small local array indexed by a loop variable usually block SROA, and what removes the block?"

        SROA needs every use of a slot at a constant offset; a variable index could touch any element, so the whole array is kept as one partition. Fully unrolling the loop that indexes it turns every offset into a compile-time constant, which is why LLVM runs SROA again right after full unrolling.

        Introduced in [O7. Calls and aggregates: inlining and SROA](o7-inlining-and-sroa.md#aggregates-in-memory).

    ??? question "What must stay true about the order of additions for a loop transformation to keep a matmul's results bitwise identical?"

        Every product for a given `c[i][j]` must still be added into that element's running sum one at a time, in increasing `k`, starting from the same initial value. Reordering across different `c` elements is free; reordering or regrouping the additions into one element is not.

        Introduced in [P7. Loop transformations](p7-loop-transformations.md#what-a-vortex-transformation-must-also-keep).

!!! goals "In this chapter"

    - Explain why a tiled loop still stalls on stride, and what packing buys that tiling alone does not.
    - Derive the five nested loops the Goto and BLIS designs put around one micro-kernel, and say what each loop packs and where it is meant to live in the cache hierarchy.
    - Build a register-blocked micro-kernel over packed panels and predict, from the central finding of this book's research, which of its steps can change a floating-point bit and which cannot.
    - Recognize the five blocking parameters, `mc`, `kc`, `nc`, `mr` and `nr`, what legality constraint bounds each one, and where a real number for each would come from.
    - Plan a packing-and-micro-kernel stage for Vortex's own generated code, with remarks that show its parameters and where they came from.

## Why a tiled loop still stalls

[P8](p8-cache-blocking.md) tiles the naive loop nest so that the block of `a`, `b` and `c` a tile touches fits in some cache. Tiling fixes how much data a tile visits. It does nothing about the order the tile visits it in, and for the stage 10 kernel that order is still bad for one of the two operands.

Vortex arrays are stored contiguously in **row-major** order: for `[f32; rows, columns]`, elements of one row sit next to each other, and moving to the next row jumps by `columns` elements ([Arrays and shapes 7.8](../specification/arrays.md#78-memory-and-layout)). The stage 10 kernel walks `b` as `b[k, column]` with `column` fixed and `k` advancing:

```vortex
// program: valid
fn multiply(a: &[f32; 2, 3], b: &[f32; 3, 2], c: &mut [f32; 2, 2]) {
    for row in 0..2 {
        for column in 0..2 {
            let mut sum: f32 = 0.0;
            for k in 0..3 {
                sum += a[row, k] * b[k, column];
            }
            c[row, column] = sum;
        }
    }
}
```

Fixing `column` and advancing `k` steps to a different row of `b` each time: the address moves by `columns` elements, not by one. `a[row, k]` is the opposite case, unit stride, the one [P7](p7-loop-transformations.md#interchange) already interchanges for. Interchange cannot fix `b` too, because some loop has to be innermost, and whichever one is, one of the two operands is walked across rows. Tiling shrinks the working set but keeps the same stride pattern inside every tile.

**Packing** removes the stride instead of tolerating it: copy the block of `b` (or `a`) that a tile needs into a new, small buffer, laid out in exactly the order the reads that follow will want, once, and read the copy instead of the original. Copying looks like wasted work, and it is more data movement, not less; the point is that every read after the copy is unit stride, in a buffer small enough to stay in a fixed cache level for the whole time it is reused. The copy touches every byte of the block once; the reads that follow it can touch each byte many times, once per row (or column) of the other operand, so the copy's cost is paid once and the stride win is collected every time after. This is the transformation Goto and van de Geijn built a tuned BLAS around, and it is where the phrase "Goto's algorithm" comes from.[^goto08]

Packing changes nothing about *what* is computed, only where the bytes sit before the multiply-adds read them. The central finding of this book's research puts it plainly: packing is "none (only copies data)", so results stay bitwise identical.[^goto08] The first example packs a small block by hand:

--8<-- "includes/examples/optimize/p12-fast-gemm/pack_panel.cpp.md"

The block asked for, 5 rows, does not divide evenly by the strip width `mr = 2`, so the third strip is padded: its second row reads past the matrix and gets 0.0 instead. A zero row multiplied into an accumulator adds nothing, so a micro-kernel that always processes exactly `mr` rows per strip, whether or not the last strip is full, produces the right answer without a special case for the edge. This is how BLIS and Goto's algorithm both handle edges: pack with zero-padding, and let the micro-kernel stay the same shape everywhere.[^blis15] [^smith14]

??? check "The example above packs 5 rows with `mr = 2`, giving three strips. If `mc = 5` and `mr = 4` instead, how many strips does the block need, and how many padding rows does the last one carry?"

    `⌈5 / 4⌉ = 2` strips. The first is full (4 real rows); the second holds row 4 plus three padding rows, since it always reads `mr = 4` rows regardless of how many are real.

## The five loops around one micro-kernel

Packing one block is a building block, not a schedule. Goto and van de Geijn's design, and BLIS's generalization of it, wrap **five nested loops** around **one micro-kernel**, and give each loop a name and a job: pack one operand, pack the other, or step across the already-packed data.[^goto08] [^blis15] Reading from the outside in:

- **`jc`** steps `nc` columns of `b` and `c` at a time.
- **`pc`** steps `kc` rows of `b` (and columns of `a`) at a time. This is where `b`'s `kc` × `nc` slab gets **packed** into a buffer this book calls `B̃`, following BLIS's notation.[^blis15]
- **`ic`** steps `mc` rows of `a` at a time. This is where `a`'s `mc` × `kc` block gets packed into `Ã`.
- **`jr`** steps `nr` columns within `B̃`'s current panel.
- **`ir`** steps `mr` rows within `Ã`'s current block, and calls the **micro-kernel**, which multiplies an `mr` × `kc` strip of `Ã` by a `kc` × `nr` strip of `B̃` into an `mr` × `nr` tile of `c` held in registers.

<figure class="vx-figure">
<svg viewBox="0 0 760 560" role="img" aria-label="Five nested loops around one micro-kernel, colour-coded by what each level packs" aria-describedby="p12-f1-desc">
<title id="p12-f1-title">The Goto and BLIS five-loop structure</title>
<desc id="p12-f1-desc">Six nested rectangles, largest to smallest. Outermost, jc, steps nc columns of b and c. Inside it, pc, accent-coloured, steps kc rows of b and packs a kc by nc slab into the buffer B tilde. Inside that, ic, accent-coloured, steps mc rows of a and packs an mc by kc block into A tilde. Inside that, jr steps nr columns within B tilde's panel. Inside that, ir steps mr rows within A tilde's block. Innermost, strongly outlined, the micro-kernel: mr by nr accumulators in registers, fed one k at a time from A tilde and B tilde, with a flowing arrow from the two packed buffers into the register tile.</desc>
<rect class="vx-box" x="20" y="20" width="700" height="500" rx="4"/>
<text class="vx-text" x="36" y="44">jc: step nc columns of b, c</text>
<rect class="vx-box-accent" x="50" y="66" width="640" height="440" rx="4"/>
<text class="vx-text-accent" x="66" y="90">pc: step kc rows of b</text>
<text class="vx-text-muted" x="66" y="108">packs kc×nc of b into B̃</text>
<rect class="vx-box-accent" x="80" y="130" width="580" height="360" rx="4"/>
<text class="vx-text-accent" x="96" y="154">ic: step mc rows of a</text>
<text class="vx-text-muted" x="96" y="172">packs mc×kc of a into Ã</text>
<rect class="vx-box" x="110" y="194" width="520" height="280" rx="4"/>
<text class="vx-text" x="126" y="218">jr: step nr columns within B̃'s panel</text>
<rect class="vx-box" x="140" y="240" width="460" height="210" rx="4"/>
<text class="vx-text" x="156" y="264">ir: step mr rows within Ã's block</text>
<rect class="vx-box-strong" x="170" y="288" width="400" height="140" rx="4"/>
<text class="vx-text" x="186" y="312">micro-kernel</text>
<text class="vx-mono" x="186" y="334">mr×nr accumulators, registers</text>
<text class="vx-mono" x="186" y="354">one k at a time from Ã, B̃</text>
<path class="vx-flow" d="M300 264 C 320 300, 320 320, 250 340" marker-end="url(#p12-f1-head)"/>
<path class="vx-flow" d="M520 218 C 500 260, 500 300, 500 340" marker-end="url(#p12-f1-head)"/>
<defs><marker id="p12-f1-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text-muted" x="186" y="452">Ã: sized for TLB reach and one cache level</text>
<text class="vx-text-muted" x="186" y="472">B̃: sized for another (larger) cache level</text>
</svg>
<figcaption>Figure 1. The five loops the Goto and BLIS designs put around one micro-kernel. The two accent-outlined loops, <code>pc</code> and <code>ic</code>, are where packing happens: <code>pc</code> packs a slab of <code>b</code> into <code>B̃</code>, <code>ic</code> packs a block of <code>a</code> into <code>Ã</code>. The two flowing arrows show both packed buffers feeding the innermost micro-kernel, one <code>k</code> at a time. Everything below <code>pc</code> and <code>ic</code> reads only the packed buffers, never the original arrays.</figcaption>
</figure>

BLIS's own account names a cache level for each loop: `Ã` sized to live in the L2, `B̃` sized to live in the L3, the current `B̃` micro-panel in the L1, and the `c` tile in registers.[^blis15] [^smith14] That mapping assumes a three-level cache with a shared L3, which is not universal. Query the facts before trusting a mapping like this one: on the author's Apple M4 Pro, `sysctl -a | grep l3` reports nothing, and the P-core L2 is 16 MiB shared by 4 CPUs (`sysctl hw.perflevel0.l2cachesize hw.perflevel0.cpusperl2`, checked 2026-09-24). With no L3 to aim `B̃` at, `nc` on this machine has to target the same shared L2 that `Ã` targets, or spill past it deliberately and rely on the packed buffer's own locality rather than residency. The five-loop shape does not depend on having three cache levels; the sizes each loop's parameter is chosen from do.

`Ã`'s size has a second constraint besides cache capacity: Goto and van de Geijn size the `mc` × `kc` block by whichever is smaller, the cache's capacity or the reach of the **translation lookaside buffer** (**TLB**), the small cache of virtual-to-physical page translations that a memory access needs a hit in to avoid an extra, slow page-table walk.[^goto08] A block that fits in the L2 but spans more pages than the TLB holds entries for pays for that miss on every access, no matter how the data cache behaves. The author's machine's page size, `sysctl hw.pagesize`, is 16 KiB, and a real implementation queries the TLB's own entry count (not exposed by `sysctl` on this machine) the same way, on whatever machine it runs.

??? check "BLIS packs `b` into `B̃` in the `pc` loop and `a` into `Ã` in the `ic` loop, one level further in. Why is `a` packed inside the loop that packs `b`, rather than the other way around?"

    `pc` steps `kc` rows of `b` at a time and packs a `kc` × `nc` slab once per `pc` iteration; `ic` then reuses that same packed slab across every `mc`-row block of `a` it visits. Packing `a` outside `pc` would mean re-packing all of `a` for every `kc` slab of `b`, far more copying for the same reuse.

## The register-blocked micro-kernel

[P7](p7-loop-transformations.md#unroll-and-jam-and-register-blocks) built the 2 by 2 case: two rows of `a`, two columns of `b`, four running sums that live in registers for the whole `k` loop, cutting loads per multiply-add from 2 to 1. A general `mr` × `nr` block generalizes the same count, loads over multiply-adds equal to `(mr + nr) / (mr · nr)`: more accumulators divide the same `mr + nr` loads over more work, so the ratio keeps falling as the block grows, until it runs out of registers to hold `mr · nr` accumulators plus the values loaded each step.

The **micro-kernel** is exactly this register block, run over the packed panels instead of the original arrays: at each step of `k`, load `mr` contiguous values from `Ã` and `nr` contiguous values from `B̃` (both unit stride, because both were packed that way), and perform `mr · nr` multiply-adds into an `mr` × `nr` tile of accumulators. Goto and van de Geijn wrote the packing in C and the micro-kernel itself in assembly, because it is the one piece of the whole scheme that has to know the target's register count and instruction set;[^goto08] BLIS keeps that same division and documents the micro-kernel's contract precisely: `C11 := beta * C11 + alpha * A1 * B1`, one call per `kc` panel, with `beta` set to 0 for the first panel that touches a `c` tile and 1 afterward.[^blis-k]

The second example builds one such micro-kernel, `mr = nr = 4`, over hand-packed panels, and checks it against a plain triple loop on the same logical values:

--8<-- "includes/examples/optimize/p12-fast-gemm/micro_kernel.cpp.md"

The two answers match bit for bit, and this is not a coincidence to be trusted blindly: it follows from the central finding of this book's research. This kernel's `K` fits inside one `kc` panel, so its accumulators start at zero and add each `k` in increasing order, once, exactly the order the naive triple loop uses for the same element. `Register-blocked micro-kernel whose accumulators start from the loaded C tile: unchanged, identical`, the research calls it, a design choice rather than a proof about floating point.[^goto08]

That guarantee needs the qualifier "one panel" because it stops holding once `K` is larger than `kc` and the `pc` loop runs more than once. BLIS's own contract, quoted above, computes each panel's contribution with its own zero-initialized accumulators and then adds the whole panel's result into `c` in one step, `C11 := beta*C11 + alpha*A1*B1`. That is a different grouping of the same additions: strict left-to-right accumulation adds one product to `c` at a time, while the panel version adds `kc` products together first and folds the result into `c` as a single addition. Floating-point addition is not associative, so the two groupings can round differently. The research states this as a table entry: bits differ once `k` splits at `kc` boundaries.[^blis-k] For Vortex, this puts multi-panel `k`-splitting in the same category as FMA contraction: a real optimization that a strict-by-default compiler cannot enable silently, because [decision 56](../decisions/numbers.md#d56) forbids reassociating floating-point additions without an explicit opt-in.

??? check "A GEMM kernel processes K = 512 with kc = 256, so the pc loop runs twice per output tile. Under decision 56, is this kernel's result guaranteed to be bitwise identical to the naive triple loop's, and why?"

    Not guaranteed, without more care. Two pc iterations means two packed panels, each with its own zero-initialized accumulator; the update `c += panel1_result; c += panel2_result` regroups the 512 additions into two batches of 256 instead of one running sum of 512, which can round differently from the naive loop. A compiler bound by decision 56 would need to accumulate across panels the way the naive loop does, one product at a time into the same running value, to keep the guarantee, or mark the kernel as using a relaxed floating-point mode explicitly.

## Why the accumulators need SROA's help

The micro-kernel's `mr` × `nr` accumulators are meant to live in registers for the whole `k` loop. In IR, they usually start out as something else: a fixed-size local array, `alloca [mr * nr x float]`, indexed by expressions built from `i` and `j`. [O7](o7-inlining-and-sroa.md#aggregates-in-memory) already named the fix this needs, in passing, as an example of the array case: **scalar replacement of aggregates** replaces a slot with one slot per piece its uses touch, but only when every use is at a **constant offset**; a variable index could touch any element, so SROA leaves such an array alone.

A micro-kernel's `i` and `j` loops are meant to be **fully unrolled**, copied once per iteration until no loop remains, precisely so every access to the accumulator array becomes a constant offset. LLVM's own pipeline schedules SROA again right after unrolling for this reason.[^llvm-pipelines] The third example writes a 4-wide accumulator row, already unrolled over two steps of `k`, so every index into `acc` is a literal integer:

--8<-- "includes/examples/optimize/p12-fast-gemm/accumulator_sroa.ll.md"

Run through `opt -passes=sroa`, the `alloca [4 x float]` and every load and store through it disappear; what is left is four independent chains of `fmul`/`fadd`, one per accumulator, each threading its own running value from one step of `k` to the next. This is what "the accumulators live in registers" means concretely in IR: not a hint to a later register allocator, but the literal absence of any memory operation to allocate a register for. [C4](../backend/c4-graph-coloring.md) picks physical registers for exactly these SSA values once they exist in this form.

??? check "The micro-kernel example above has mr = nr = 4, sixteen accumulators. If the ir and jr loops around it were left un-unrolled, indexed by loop-carried variables, would SROA still remove the accumulator array?"

    No. A loop-carried index is not a compile-time constant, so every access could touch any element of the array; SROA would treat the whole array as one partition it cannot see through and leave the slot in memory.

## Choosing mc, kc, nc, mr and nr

Five parameters govern everything above, and each one is bounded by a legality constraint rather than free to pick:

- **`kc`**: the packed `B̃` micro-panel used by one `jr`/`ir` pass must fit, alongside room for the accumulators' operands, in whichever cache level it targets; BLIS's own rule of thumb is `kc · nr` under half of the L1, leaving the other half for the rest of what the micro-kernel touches.[^blis15]
- **`mc`**: the packed `Ã` block must fit in its target cache level and inside the TLB's reach, whichever is smaller.[^goto08]
- **`nc`**: sized so the packed `B̃` slab, `kc` × `nc`, fits the cache level BLIS's design aims it at; on a machine with no L3, this has to be re-derived rather than assumed, as noted above.
- **`mr`, `nr`**: bounded by the register file: `mr · nr` accumulators plus the values loaded each step of `k` must fit in the architecture's vector registers, with room left for the compiler's own bookkeeping.

These are not free parameters to guess at. Low, Igual, Smith and Quintana-Ortí show they can be derived analytically from a small number of hardware facts, cache sizes, latencies and the number of registers, rather than found only by search over the whole space; their model reaches performance close to empirically tuned BLIS on the platforms they tested.[^low16] [P15](p15-choosing-parameters.md) covers the general question of model-driven against search-driven tuning; here the point is narrower: whichever way a Vortex compiler picks these five numbers, decision 56's neighbor rule about auto-tuning applies just as it does to search-based tuning elsewhere in this book: **the tuning space must stay meaning-preserving.** A compiler is free to choose `mc`, `kc`, `nc`, `mr` and `nr` however it likes, from a model or from measurement, because every value in that space produces the identical bits shown above; it is not free to fold a `kc`-splitting choice into the same knob without disclosing the change decision 56 requires.

## What one measured climb looked like

None of the numbers above are Vortex's own; the shape of a real climb is worth seeing once, with its source attached. Boehm measured a sequence of kernels on an Intel i7-6700 (Skylake), 1024 × 1024 `f32` matrices, milliseconds per run:[^boehm22]

| Step | Time (ms) |
| --- | --- |
| Naive | 4481 |
| Compiler flags (including fast-math) | 1621 |
| Register accumulation | 1512 |
| Loop reorder | 89 |
| L1 tiling | 70 |
| Multithreaded (unspecified core count) | 16 |
| Vendor BLAS (NumPy/MKL) | 8 |

Two cautions belong with this table. First, "compiler flags" includes `-ffast-math`, which Vortex's decision 56 forbids by default: some of that first jump is exactly the reassociation this chapter has been careful to keep separate from packing and register blocking. Second, one machine's numbers are not a promise about another machine: Boehm elsewhere attributes some of Apple's Accelerate BLAS's speed on an M1 Pro to Apple's own matrix instructions, not to any technique in this chapter, an attribution, not a benchmark of the technique itself.[^boehm22] A newer report, Salykov's, describes reaching performance competitive with OpenBLAS 0.3.26 on a Ryzen 7 9700X and a Core Ultra 265 using the same packing and micro-kernel structure, without numbers this book can responsibly repeat second-hand.[^salykova25] [P16](p16-capstone.md) is where this book asks for the equivalent table measured on the reader's own machine, against Accelerate, OpenBLAS and BLIS, under the protocol [P1](p1-measure-first.md) sets out.

For a worked walkthrough of building a small GEMM kernel up through these same rungs, one instruction at a time, the *How To Optimize GEMM* wiki and van de Geijn, Myers and Parikh's free course cover the same ground this chapter compresses, with runnable code the reader steps through themselves.[^htog] [^laff] [^blislab]

## For Vortex

!!! vortex "Exercise"

    Build, in your own compiler or as a design document if the compiler is not there yet, a packing-and-micro-kernel stage for the matrix-multiplication kernel: emit a packing routine for each operand and a register-blocked micro-kernel, driven by five parameters, `mc`, `kc`, `nc`, `mr` and `nr`.

    **Build now.**

    - A packing routine that copies an `mc` × `kc` (or `kc` × `nc`) block into a contiguous, zero-padded buffer in micro-kernel order, following this chapter's first example.
    - A register-blocked micro-kernel, `mr` × `nr`, that reads only the packed buffers and keeps its accumulators as separate values (by construction, or by relying on full unrolling plus SROA as this chapter's third example does).
    - A remark, printed once per compiled kernel, that states the five chosen parameters and, for each, whether it came from a model (name the cache fact it used) or from a fixed default; [O1's remark format](o1-optimizer-contract.md#remarks-the-optimizers-report) is the template.
    - A single `kc` panel per `k` dimension (no `pc`-loop splitting yet), so the whole kernel stays inside the bitwise-identical case this chapter establishes, with no floating-point opt-in required.

    **Not yet.**

    - Splitting `k` across multiple `kc` panels. That crosses into the "differs" side of decision 56 and needs an explicit relaxed-mode decision before it is legal to enable by default.
    - Choosing `mc`, `kc` and `nc` by search; a fixed model, even a rough one, is enough to test the rest of this exercise, and [P15](p15-choosing-parameters.md) is where search belongs.
    - Multithreading the outer loops; that is [P13](p13-multithreading.md).

    **Test.** Compare the packed, register-blocked kernel's output against the naive kernel's, element by element, with exact equality (`==`, not a tolerance) on at least one square and one rectangular shape whose dimensions are not multiples of `mr`, `nr` or `kc`, so the zero-padding path is exercised. Record the chosen `mc`, `kc`, `nc`, `mr`, `nr` and their sources in a table like P1's, with the date and your compiler's version.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does packing fix that tiling alone does not?** The stride an operand is read with inside a tile; packing copies it once into a buffer that every later read of that tile visits at unit stride.
    - **What are the five loops, and which two pack something?** `jc`, `pc`, `ic`, `jr`, `ir`, outermost to innermost; `pc` packs `b` into `B̃`, `ic` packs `a` into `Ã`.
    - **Why must the cache-level mapping for those five loops be checked on the target machine rather than assumed?** BLIS's own account names an L3 for `B̃`; a machine with no L3, such as the author's M4 Pro, has nothing there to aim it at, so the mapping has to be re-derived from that machine's own facts.
    - **When is a register-blocked micro-kernel's result bitwise identical to the naive loop's, and when does that stop holding?** Identical when its accumulators run over one `kc` panel covering the whole `k` dimension, in increasing `k` order from zero; it stops once `k` splits across more than one panel, because folding each panel's total into `c` regroups the additions.
    - **What has to be true of an accumulator array's indices before SROA will remove it?** Every index must be a compile-time constant, which is exactly what fully unrolling the loop that generated those indices produces.
    - **What legality constraint bounds each of mc, kc, nc, mr and nr?** `kc` and `mc` by a cache level's capacity (and `mc` also by TLB reach); `nc` by the cache level BLIS's design aims the packed `B̃` slab at; `mr` and `nr` by the register file.

## Where this comes back

!!! next "You will use this again in"

    - [P13. Multithreading](p13-multithreading.md): *parallelizing the ic, jc and jr loops*, *per-thread packing buffers*
    - [P15. Choosing parameters: models or search](p15-choosing-parameters.md): *deriving mc, kc, nc, mr, nr analytically or by search*
    - [P16. Capstone: the ladder, measured](p16-capstone.md): *measuring this chapter's rungs against Accelerate, OpenBLAS and BLIS*
    - [C4. Register allocation II: graphs and SSA](../backend/c4-graph-coloring.md): *allocating the micro-kernel's accumulator values to physical registers*
    - [G11. Matrix units](../gpu/g11-matrix-units.md): *the same packing and blocking ideas feeding a hardware matrix instruction instead of scalar registers*

## Sources and further reading

Start with the *How To Optimize GEMM* wiki for a step-by-step build of a small kernel, then read Goto and van de Geijn's paper for why packing and the TLB-aware block sizes exist; the BLIS papers generalize the same structure into the five-loop design this chapter names directly, and their KernelsHowTo document is the shortest precise statement of the micro-kernel's contract. Low, Igual, Smith and Quintana-Ortí show the blocking parameters do not have to come from search.

[^goto08]: Kazushige Goto and Robert A. van de Geijn, "Anatomy of High-Performance Matrix Multiplication", *ACM Transactions on Mathematical Software* 34(3), 2008: the packing design, TLB-aware block sizing, and the packing/kernel split. <https://doi.org/10.1145/1356052.1356053> (authors' copy: <https://www.cs.utexas.edu/~flame/pubs/GotoTOMS_revision.pdf>)
[^blis15]: Field G. Van Zee and Robert A. van de Geijn, "BLIS: A Framework for Rapidly Instantiating BLAS Functionality", *ACM Transactions on Mathematical Software* 41(3), 2015: the five-loop structure, the `Ã`/`B̃` notation, and the cache-level mapping. <https://doi.org/10.1145/2764454> (authors' copy: <https://www.cs.utexas.edu/~flame/pubs/blis1_toms_rev3.pdf>)
[^smith14]: Tyler M. Smith, Robert van de Geijn, Mikhail Smelyanskiy, Jeff R. Hammond and Field G. Van Zee, "Anatomy of High-Performance Many-Threaded Matrix Multiplication", *2014 IEEE 28th International Parallel and Distributed Processing Symposium*: the micro-kernel's role in the five loops and per-panel accumulation. <https://doi.org/10.1109/IPDPS.2014.110> (authors' copy: <https://www.cs.utexas.edu/~flame/pubs/blis3_ipdps14.pdf>)
[^blis-k]: BLIS Project, "KernelsHowTo", read on 2026-09-24: the micro-kernel contract `C11 := beta*C11 + alpha*A1*B1`. <https://github.com/flame/blis/blob/master/docs/KernelsHowTo.md>
[^low16]: Tze Meng Low, Francisco D. Igual, Tyler M. Smith and Enrique S. Quintana-Orti, "Analytical Modeling Is Enough for High-Performance BLIS", *ACM Transactions on Mathematical Software* 43(2), 2016: deriving `mc`, `kc`, `nc`, `mr`, `nr` from hardware facts instead of search. <https://doi.org/10.1145/2925987> (authors' copy: <https://www.cs.utexas.edu/~flame/pubs/TOMS-BLIS-Analytical.pdf>)
[^htog]: The FLAME Project, "How To Optimize GEMM" wiki, read on 2026-09-24: a step-by-step build of a small GEMM kernel. <https://github.com/flame/how-to-optimize-gemm/wiki>
[^laff]: Robert van de Geijn, Margaret Myers and Devangi Parikh, *LAFF-On Programming for High Performance*, read on 2026-09-24: the free course this chapter's structure follows, on loops, micro-kernels, caches and threading. <https://www.cs.utexas.edu/~flame/laff/pfhp/>
[^blislab]: Jianyu Huang and Robert A. van de Geijn, "BLISlab: A Sandbox for Optimizing GEMM", arXiv:1609.00076, 2016. <https://arxiv.org/abs/1609.00076>
[^boehm22]: Simon Boehm, "Fast Multidimensional Matrix Multiplication on CPU from Scratch", August 2022: the i7-6700 measurements and the Accelerate/M1 Pro attribution. <https://siboehm.com/articles/22/Fast-MMM-on-CPU>
[^salykova25]: Amanzhol Salykov, "Advanced Matrix Multiplication Optimization on Modern Multi-Core Processors", January 2025. <https://salykova.github.io/matmul-cpu>
[^llvm-pipelines]: LLVM Project, `PassBuilderPipelines.cpp`, release/18.x branch: the comment on running SROA again after full loop unrolling. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Passes/PassBuilderPipelines.cpp>
