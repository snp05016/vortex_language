# P13. Multithreading

<p class="page-intro">A fast single-core kernel still leaves most of the chip idle. This chapter splits the kernel from P12 across cores: which loop to split so that every thread owns its own part of the answer, what a compiler must prove before it splits, and the costs that threads add without changing a single bit of the result.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 50 minutes · Builds on: [P12. Anatomy of a fast GEMM](p12-fast-gemm.md), [P6. Dependence analysis](p6-dependence-analysis.md), [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is a cache line, and what happens to two unrelated values that fall inside the same one?"

        The fixed-size, aligned block a cache moves as a unit, 128 bytes on the owner's M4 Pro. Two values in the same line are fetched, kept and evicted together, whether or not the code that uses them is related.

        Introduced in [P2. The memory hierarchy](p2-memory-hierarchy.md#cache-lines-and-why-order-matters).

    ??? question "In `(row, column, k)` order, what is the direction vector of the matmul kernel's only loop-carried dependence, and what does each component say?"

        `(=, =, <)`. The dependence runs through `c[row, column]`, which every trip of `k` reads and writes, so `k` carries it (`<`). Two different values of `row`, or of `column`, never touch the same element of `c`, so those two loops carry nothing (`=`).

        Introduced in [P6. Dependence analysis](p6-dependence-analysis.md#distance-and-direction-vectors).

    ??? question "Name the five loops around the BLIS micro-kernel, from the outside in, and the block each one packs, if any."

        `jc` over columns of `b` and `c`; `pc` over `k`, which packs a panel of `b` into `B̃`; `ic` over rows of `a` and `c`, which packs a block of `a` into `Ã`; then `jr` and `ir` over micro-panels of `B̃` and `Ã`, around the micro-kernel.

        Introduced in [P12. Anatomy of a fast GEMM](p12-fast-gemm.md#the-five-loops-around-one-micro-kernel).

    ??? question "What may a Vortex compiler assume about storage reached through a `&mut` parameter?"

        That no other parameter of the same call reaches it, so a store through it never changes a value loaded through another parameter.

        Introduced in [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md#one-store-one-load).

    ??? question "Why may a Vortex compiler not add up an `f32` sum in a different grouping, even when the new grouping is faster?"

        Floating-point addition rounds after every step, so a different grouping can give different bits, and decision 56 requires every operation to give the result of the order the program wrote.

        Introduced in [P11. Floating point under optimization](p11-floating-point.md#the-rule-every-reordering-pass-needs).

!!! goals "In this chapter"

    - Recognize which loops of a nest can run on separate threads with no synchronization, and which need a reduction.
    - Explain, loop by loop, what the threads share when each of the five GEMM loops is split, and why splitting `ic` or `jr` keeps the answer bitwise identical while splitting `pc` does not.
    - State the two facts a compiler must prove before it splits a Vortex loop, and why O9's aliasing rule supplies neither of them.
    - Find false sharing and load imbalance in a split by hand, and fix each.
    - Relate thread placement, QoS classes and NUMA to what a compute-bound kernel needs from the operating system.

## Splitting work without asking permission

Here is the smallest version of the question this chapter answers. A program sums the eight rows of a small grid into an output array, one row at a time:

```cpp
for (int row = 0; row < 8; ++row) {
    out[row] = sum_of_row(row);
}
```

Nothing about row 3's sum depends on row 5's. A **thread** is an independent stream of instructions inside one program: the operating system can run it on its own core, at the same time as the program's other threads, and all of them share the program's memory. With two threads, one can sum rows 0 to 3 while the other sums rows 4 to 7. On a machine with two idle cores the loop can finish in about half the time, and nothing needs a lock, because the two threads never touch the same element of `out`.

The first example does this with four threads. It uses the C++ standard library's `std::thread`, because Apple clang on the owner's machine rejects `-fopenmp` with "unsupported option" (checked 2026-09-24).[^local] Most numerical libraries use **OpenMP** instead, a standard set of compiler directives, now at version 6.0, released in November 2024.[^openmp6] A programmer writes a line such as `#pragma omp parallel for` above a loop, and the compiler and its runtime create the threads and divide the iterations. `std::thread` does the same job with every step written out, which suits a chapter about what those steps are.

--8<-- "includes/examples/optimize/p13-multithreading/split_rows.cpp.md"

The main thread **forks**: it starts four threads, each with its own range of rows, passed as plain arguments. Then it **joins**: `join` waits until a thread has finished, and the main thread reads `out` only after all four joins return. This shape, fork, work, join, is the **fork-join model**. Two details of `std::thread` matter here. Arguments are copied into the new thread unless wrapped, so `out` goes through `std::ref`,[^cppref-thread-ctor] and destroying a thread object that was never joined calls `std::terminate`.[^cppref-thread-dtor]

What makes the example correct is that no two threads touch the same element. C++ says that two evaluations **conflict** when one of them writes a memory location and the other reads or writes the same location. A **data race** is a pair of conflicting evaluations in different threads, not both atomic, with neither ordered before the other; a program with a data race has undefined behavior. Different memory locations, on the other hand, may be written by different threads at the same time with no synchronization at all.[^cppref-mt] Each element of `out` is its own location, and each has exactly one writer.

The output does not depend on how the operating system schedules the four threads, because each row is summed by one thread in the order the serial loop would use. That is the property this chapter keeps asking for: a split that changes when each part of the answer is computed, and never what it is.

## Which loop in the kernel to split

[P12](p12-fast-gemm.md#the-five-loops-around-one-micro-kernel) arranged matrix multiplication as five loops around a micro-kernel: `jc`, `pc`, `ic`, `jr`, `ir`. Splitting the nest across threads means choosing one or more of these loops and running its iterations on different threads instead of one after another. The dependence vector from the box above decides which choices are safe. Every loop except `pc` walks rows or columns of `c`, which carry no dependence, so different iterations write different elements of `c`. The `pc` loop walks `k`, and every one of its iterations adds into the same block of `c`.

Smith and colleagues went through the five loops one at a time, asking what the threads would share in each case and which cache that shared data lives in.[^smith14] Their answers, for a split of each loop on its own:

| Loop split | What the threads share | Parallelism available | Their verdict |
| --- | --- | --- | --- |
| Inside the micro-kernel (`k` within one call) | one `mr × nr` block of `c` | tiny units of work | ill-advised: overhead, and a costly reduction |
| `ir` | one micro-panel of `B̃`, in L1 | `mc / mr` iterations, a few | only when `mc / mr` is large, which it usually is not |
| `jr` | one block `Ã`, in L2 | `nc / nr` iterations, many | good; suits threads that share an L2 |
| `ic` | one panel `B̃`, in L3 or memory | grows with `m` | good when `m` is large; each thread packs its own `Ã` |
| `pc` | the same block of `c`, written by all | grows with `k` | only in special cases, with private copies of `c` and a reduction |
| `jc` | all of `a`, in memory | grows with `n` | suits separate L3 caches, such as multiple sockets |

The pattern in the table is about memory, not arithmetic. Splitting `jr` lets several threads work from one packed `Ã`, so threads that share an L2 cache can share one copy of it; if their L2 caches are private, the pieces of `Ã` must be copied between caches by the hardware. Splitting `ic` gives each thread its own `Ã`, which suits private L2 caches, and forces the blocks to shrink when the threads share one L2.[^smith14]

BLIS follows the same analysis. It can split four of its five loops, all but `pc`, whose iterations all update the same part of `c`; `BLIS_NUM_THREADS` sets the total number of threads, and variables such as `BLIS_IC_NT` and `BLIS_JR_NT` set the split of each loop by hand. It runs on OpenMP or on POSIX threads.[^blis-mt]

Figure 1 shows who owns what under three of the splits.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="Ownership of the output matrix c under three splits across three threads: split ic gives each thread a band of rows, split jr gives each thread a strip of columns, and split pc gives every thread the whole of c, as a private copy that must be added up at the end" aria-describedby="p13-f1-desc">
<title id="p13-f1-title">Who owns which part of c</title>
<desc id="p13-f1-desc">Three panels, each showing the output matrix c as a square, split across three threads T0, T1 and T2. Left, split ic: the square is cut into three horizontal bands labelled T0, T1 and T2, and the caption below says each element has one writer. Middle, split jr: the square is cut into three vertical strips labelled T0, T1 and T2, with the same caption. Right, split pc: three small full squares labelled T0, T1 and T2, each a private copy of the whole of c, with arrows from all three into one final square labelled c equals the sum of the copies, and a caption saying every element has three writers, so the copies must be added, which regroups the sum.</desc>
<defs><marker id="p13-f1-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="24">Split ic</text>
<text class="vx-text-muted" x="20" y="42">bands of rows</text>
<rect class="vx-box" x="20" y="60" width="180" height="60"/>
<rect class="vx-box-accent" x="20" y="120" width="180" height="60"/>
<rect class="vx-box-strong" x="20" y="180" width="180" height="60"/>
<text class="vx-mono" x="110" y="95" text-anchor="middle">T0</text>
<text class="vx-mono" x="110" y="155" text-anchor="middle">T1</text>
<text class="vx-mono" x="110" y="215" text-anchor="middle">T2</text>
<text class="vx-text-muted" x="20" y="272">one writer per element</text>
<text class="vx-text" x="270" y="24">Split jr</text>
<text class="vx-text-muted" x="270" y="42">strips of columns</text>
<rect class="vx-box" x="270" y="60" width="60" height="180"/>
<rect class="vx-box-accent" x="330" y="60" width="60" height="180"/>
<rect class="vx-box-strong" x="390" y="60" width="60" height="180"/>
<text class="vx-mono" x="300" y="155" text-anchor="middle">T0</text>
<text class="vx-mono" x="360" y="155" text-anchor="middle">T1</text>
<text class="vx-mono" x="420" y="155" text-anchor="middle">T2</text>
<text class="vx-text-muted" x="270" y="272">one writer per element</text>
<text class="vx-text" x="520" y="24">Split pc</text>
<text class="vx-text-muted" x="520" y="42">a private copy of all of c each</text>
<rect class="vx-box" x="520" y="60" width="60" height="60"/>
<rect class="vx-box-accent" x="600" y="60" width="60" height="60"/>
<rect class="vx-box-strong" x="680" y="60" width="60" height="60"/>
<text class="vx-mono" x="550" y="95" text-anchor="middle">T0</text>
<text class="vx-mono" x="630" y="95" text-anchor="middle">T1</text>
<text class="vx-mono" x="710" y="95" text-anchor="middle">T2</text>
<path class="vx-flow" d="M550 120 L610 179" marker-end="url(#p13-f1-head)"/>
<path class="vx-flow" d="M630 120 L630 179" marker-end="url(#p13-f1-head)"/>
<path class="vx-flow" d="M710 120 L650 179" marker-end="url(#p13-f1-head)"/>
<rect class="vx-box-bad" x="580" y="180" width="100" height="60"/>
<text class="vx-mono" x="630" y="206" text-anchor="middle">c =</text>
<text class="vx-mono" x="630" y="224" text-anchor="middle">T0+T1+T2</text>
<text class="vx-text-muted" x="520" y="272">three writers per element:</text>
<text class="vx-text-muted" x="520" y="290">the copies must be added,</text>
<text class="vx-text-muted" x="520" y="308">which regroups every sum</text>
</svg>
<figcaption>Figure 1. Ownership of <code>c</code> when three threads split one loop. Splitting <code>ic</code> or <code>jr</code> hands each thread a disjoint part of <code>c</code> that it computes from start to finish. Splitting <code>pc</code> gives every thread a share of <code>k</code> for every element, so each thread needs a private copy of <code>c</code>, and the moving arrows are the reduction that adds the copies at the end.</figcaption>
</figure>

### Splitting k needs a reduction

Two threads computing different ranges of `k` would both write every element of `c`: a data race. Smith and colleagues give the two ways out: a lock around each update, or a private copy of the block of `c` for each thread, started at zero, with the copies added together once every thread has finished.[^smith14] Combining per-thread partial results in this way is a **reduction**. The copies remove the race. They do not keep the answer, because the reduction adds the same products in a different grouping. The second example splits one sum of 2,048 terms into 1, 2, 4 and 8 slices, the way a `pc` split with that many threads would, and compares the bits:

--8<-- "includes/examples/optimize/p13-multithreading/k_split_reassociates.cpp.md"

Every split except the trivial one gives a different result, and each thread count gives its own. Nothing in the program uses `-ffast-math`: every addition is an ordinary rounded `float` addition, and only the grouping changes. The example also shows what a reduction does *not* break. With a fixed number of slices, combined in a fixed order, the result is the same on every run however the threads are scheduled. A reduction is deterministic but depends on the thread count, and a result that changes when the reader moves to a machine with more cores is still a changed answer.

[Decision 56](../decisions/numbers.md#d56) forbids a Vortex compiler from reordering or regrouping floating-point operations unless the program opts in, so a `pc` split belongs with fused multiply-add as something only an explicit opt-in may turn on ([P11](p11-floating-point.md)). Splitting `ic` or `jr` never has this problem. Each thread computes its elements of `c` from start to finish, adding the products in increasing `k`, exactly as the single-threaded kernel does, so every element gets the same sequence of roundings on whichever core computes it. [P12](p12-fast-gemm.md#two-ways-to-accumulate) met the same question inside one thread, where a micro-kernel that starts each `kc` panel from zero regroups the sum without any threads at all.

??? check "Why does splitting the `k` loop need a reduction, while splitting `row` or `column` does not?"

    Every trip around `k` updates the same `c[row, column]`: the `<` in the dependence vector is exactly that. Two threads with different ranges of `k` would both write the same element with no ordering between them, a data race, so each must accumulate into a private copy, and the copies must be added afterwards. Splitting `row` or `column` gives each thread a different set of elements to own outright, so there is nothing to combine and no grouping to change.

### Walking a split by hand

Take the stage 10 kernel at size 64: `c`, `a` and `b` are `[f32; 64, 64]`, stored row after row ([decision 43](../decisions/arrays.md#d43)). Block the rows with `mc = 8`, so the `ic` loop has eight iterations, and split it across threads with a **static schedule**: before any thread starts, thread `t` of `T` receives blocks `t × 8 / T` up to, but not including, `(t + 1) × 8 / T`, each rounded down. Write that range as `[t × 8 / T, (t + 1) × 8 / T)`.

- With **two threads**, thread 0 gets blocks `[0, 4)`, rows 0 to 31, and thread 1 gets blocks `[4, 8)`, rows 32 to 63.
- With **three threads**, the shares are `[0, 2)`, `[2, 5)` and `[5, 8)`: 16, 24 and 24 rows. On three equal cores the kernel ends when a 24-row thread ends, where a perfect division would take 21⅓ rows, so the three cores are busy for 8/9 of the time.
- With **four threads**, every thread gets two blocks, 16 rows, and the split is perfect.
- With **five threads**, the largest share is still two blocks. The fifth thread adds nothing to the finishing time, because 8 blocks cannot be divided more finely than the block size allows.

The general name for the problem in the last three cases is **load imbalance**: the threads finish at different times, and the slowest one decides when the kernel is done. Smith and colleagues saw it in their measurements, where performance varied with how evenly `m` or `n` divided among the threads, because the micro-kernel's `mr × nr` block is the smallest unit a thread can be given.[^smith14]

A related bound applies to any split. If a fraction `s` of the work cannot be split at all, then with `T` threads the time is at least `s + (1 − s) / T` of the serial time, so the speedup can never exceed `1 / s`, however many threads run. This bound is known as **Amdahl's law**. In the kernel, packing is the part to watch. When several threads share one `Ã`, they share the work of packing it, and Smith and colleagues observe that a small `Ã` leaves little packing to share, so some threads finish packing before others and sit idle.[^smith14]

The third example computes these static shares for eight items and checks, by counting owners, that every item belongs to exactly one thread:

--8<-- "includes/examples/optimize/p13-multithreading/static_partition.cpp.md"

??? check "The kernel's rows are split with `mc = 8` across six threads. What are the shares, and what fraction of the time are the six cores busy?"

    Thread `t` gets blocks from `t × 8 / 6` up to `(t + 1) × 8 / 6`, each rounded down: `[0, 1)`, `[1, 2)`, `[2, 4)`, `[4, 5)`, `[5, 6)` and `[6, 8)`, so four threads get one block and two get two. The kernel ends when a two-block thread ends, while a perfect split would give each thread 8/6 of a block, so the cores are busy for (8/6) / 2 = 2/3 of the time. A smaller `mc` gives finer blocks and a better balance, at the cost of a smaller `Ã` per thread.

## The proof a compiler needs, and the one it does not have yet

A person who parallelizes the kernel checks the split by eye. A compiler must prove it, and the proof has two parts.

The first part is the dependence test. The loop being split must carry no dependence: in the direction vector, its component must be `=`, as it is for `row` and `column` and is not for `k`. [P6](p6-dependence-analysis.md) computes these vectors, and the kernel's are simple because its arrays have fixed shapes and its subscripts are the loop counters themselves.

The second part is about what the threads are handed. [O9](o9-alias-analysis.md#one-store-one-load) gives Vortex a theorem about a whole `&mut` parameter: no other parameter of the call reaches its storage ([References 9.8](../specification/references.md#98-aliasing)). That rules out `a` or `b` sharing memory with `c`, and it is what lets a vectorizer skip runtime alias checks.

The theorem says nothing about two *pieces* of `c`. When the `ic` loop is split, every thread writes through the same `&mut [f32; 64, 64]`, and whether their writes can collide is a question about the pieces: thread 0's rows are `0..32`, thread 1's are `32..64`, and the proof that they are disjoint is arithmetic on the loop bounds. The pass that makes the split must build that partition itself and check that every write a thread makes stays inside the rows it was given. Nothing upstream supplies it.

Vortex adds a third condition that C compilers do not face in the same form. A Vortex program's observable behavior includes its printed output, the error line of a runtime check that fails, and the exit status, in order ([O1](o1-optimizer-contract.md#vortexs-list)).

Version 0.1 has no parallel loop statement ([Statements 6.12](../specification/statements.md#612-excluded-statements)), so every thread a Vortex program runs is the compiler's own idea, and it must be invisible. A loop body that prints, or that contains a check that may fail, cannot be split as it stands. Two threads could print in either order, and if two iterations would both fail a check, the serial program reports the earlier iteration's error while the threaded one might report the other. The kernel's body qualifies only after [O8](o8-loops.md#removing-a-check-with-a-proof) has proved its bounds checks can never fail and removed them; its `f32` additions and multiplications cannot fail at all.

??? check "A compiler splits a `&mut` array's rows across two threads and cites O9's aliasing rule as its justification. What is missing from the argument?"

    O9's rule says the whole array cannot be reached through any other parameter, which rules out `a` or `b` overlapping `c`. It says nothing about whether the row ranges handed to the two threads overlap each other; that is a separate, arithmetic fact about the partition. For Vortex the argument also needs the loop body to contain no `print` and no check that may fail, so that running iterations out of order cannot change the output or the error line.

## False sharing: correct, and slower anyway

A correct split can still run slowly for a reason the source code does not show. Give two threads their own counters, one each, with no variable shared anywhere. If the two counters lie close together in memory, they fall inside the same cache line.

Caches keep copies of lines, and a **cache coherence protocol** keeps those copies consistent. Drepper describes the common one, **MESI**, named after the four states a cached line can be in: Modified, Exclusive, Shared and Invalid. A core may write a line only while its cache holds the only copy, so a write to a line that another core holds first invalidates that core's copy.[^drepper]

When core 0 writes its counter, core 1's copy of the line becomes invalid; when core 1 then writes its own counter, it must fetch the line back, which invalidates core 0's copy, and so on for every write. The two cores pass one line back and forth though neither ever reads the other's counter. This is **false sharing**: no data is shared, only the line. Figure 2 shows the mechanism.

<figure class="vx-figure">
<svg viewBox="0 0 760 360" role="img" aria-label="Two layouts for two per-thread counters: packed, where both fall on one cache line and the cores keep invalidating each other's copy, and padded, where each counter has its own line and the cores never interfere" aria-describedby="p13-f2-desc">
<title id="p13-f2-title">One shared line versus two private lines</title>
<desc id="p13-f2-desc">Left panel, labelled packed: a Core 0 box and a Core 1 box each send a moving arrow down into one shared box marked 128-byte line, holding packed[0] and packed[1], which pulses to show it repeatedly changing hands. Right panel, labelled padded: a Core 0 box and a Core 1 box each send a plain, still arrow down into their own separate box, one marked 128-byte line holding padded[0], the other marked 128-byte line holding padded[1], with empty space between the two lines and no shared box.</desc>
<defs><marker id="p13-f2-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="24">Packed</text>
<text class="vx-text-muted" x="20" y="42">one line, two writers</text>
<rect class="vx-box" x="20" y="60" width="130" height="40" rx="4"/>
<text class="vx-text" x="85" y="85" text-anchor="middle">Core 0</text>
<rect class="vx-box" x="210" y="60" width="130" height="40" rx="4"/>
<text class="vx-text" x="275" y="85" text-anchor="middle">Core 1</text>
<path class="vx-flow" d="M85 100 L140 190" marker-end="url(#p13-f2-head)"/>
<path class="vx-flow" d="M275 100 L220 190" marker-end="url(#p13-f2-head)"/>
<rect class="vx-box-bad vx-pulse" x="95" y="200" width="170" height="50" rx="4"/>
<text class="vx-mono" x="180" y="222" text-anchor="middle">128-byte line</text>
<text class="vx-mono" x="180" y="240" text-anchor="middle">packed[0] | packed[1]</text>
<text class="vx-text-muted" x="20" y="300">every write on either core</text>
<text class="vx-text-muted" x="20" y="316">invalidates the other's copy</text>
<text class="vx-text" x="420" y="24">Padded</text>
<text class="vx-text-muted" x="420" y="42">each writer owns a line</text>
<rect class="vx-box" x="420" y="60" width="130" height="40" rx="4"/>
<text class="vx-text" x="485" y="85" text-anchor="middle">Core 0</text>
<rect class="vx-box" x="610" y="60" width="130" height="40" rx="4"/>
<text class="vx-text" x="675" y="85" text-anchor="middle">Core 1</text>
<path class="vx-line" d="M485 100 L485 190" marker-end="url(#p13-f2-head)"/>
<path class="vx-line" d="M675 100 L675 190" marker-end="url(#p13-f2-head)"/>
<rect class="vx-box" x="420" y="200" width="130" height="50" rx="4"/>
<text class="vx-mono" x="485" y="222" text-anchor="middle">128-byte line</text>
<text class="vx-mono" x="485" y="240" text-anchor="middle">padded[0]</text>
<rect class="vx-box" x="610" y="200" width="130" height="50" rx="4"/>
<text class="vx-mono" x="675" y="222" text-anchor="middle">128-byte line</text>
<text class="vx-mono" x="675" y="240" text-anchor="middle">padded[1]</text>
<text class="vx-text-muted" x="420" y="300">writes never touch</text>
<text class="vx-text-muted" x="420" y="316">a line the other core owns</text>
</svg>
<figcaption>Figure 2. Two per-thread counters packed next to each other share a cache line, so every write moves the line from one core's cache to the other's; padding each counter out to its own line stops the traffic without changing what either thread computes. The line size is the M4 Pro's.</figcaption>
</figure>

How much it costs depends on the machine. Drepper measured threads that each increment their own memory location 500 million times, pinned to the four processors of a machine with four Pentium 4 processors. He reports the overhead, the time with all the locations on one line divided by the time with a line for each thread, as 390%, 734% and 1,147% for two, three and four threads. On a single quad-core Core 2 QX 6700 the same test showed only a slight overhead that did not grow with the number of cores.[^drepper] The lesson is not a number to remember but a thing to test for, on the machine that will run the code.

The fix is layout, not a lock: give each thread's frequently written data a line of its own. C++17 names a distance for this, `std::hardware_destructive_interference_size`: the smallest distance between two objects that keeps them from false sharing. Its value is implementation-defined.[^cppref-hdis] A type declared with `alignas` of that constant starts on a new boundary of that size, so two array elements of the type can never share a line of that size. The fourth example builds a packed pair of counters and a padded pair, and checks from the byte distance between neighbours alone whether each pair can share a line:

--8<-- "includes/examples/optimize/p13-multithreading/false_sharing_layout.cpp.md"

With Apple clang 21 on the owner's M4 Pro, the constant is 256, twice the 128-byte line that `sysctl hw.cachelinesize` reports (both checked 2026-09-24).[^local] The constant is fixed when the program is compiled, so it cannot follow the processor the program later runs on, and here padding to it spends twice the memory the M4 Pro's line requires.

The example prints no times, because a time is not deterministic. Measure the difference yourself with [P1](p1-measure-first.md)'s protocol: several threads each increment their own counter many times, once packed and once padded, each version repeated, reporting the median and an interval rather than one run.

| Threads | Packed, median time | Padded, median time | Packed ÷ padded, with interval |
| --- | --- | --- | --- |
| 2 | | | |
| 4 | | | |
| 8 | | | |

### False sharing in the kernel

The matmul kernel has no counters, but its threads write `c`, and splits of `c` have edges. A row of `[f32; 64, 64]` is 64 × 4 = 256 bytes, two M4 Pro lines. Splitting by rows, as `ic` does, puts every boundary at the start of a row, a multiple of 256 bytes from the start of `c`; if `c` itself starts on a line boundary, which a compiler can arrange, no line holds elements of two threads.

Splitting by columns is different. One line holds 32 consecutive `f32` values of one row, so a boundary between two column strips that is not a multiple of 32 columns puts both threads' elements into the same line, in every one of the 64 rows.

??? check "Four threads split the 64 columns of `c` into strips of 16. Which cache lines of `c` are written by more than one thread, on a machine with 128-byte lines?"

    All of them. Each row has two lines, columns 0 to 31 and 32 to 63. Threads 0 and 1 both write the first line of every row, and threads 2 and 3 both write the second. Strips of 32 columns, two threads, or a layout that pads each strip to a line boundary would remove the sharing; splitting whole rows instead of columns avoids it for any thread count, since every boundary then falls at the start of a row.

## What more threads cannot fix

Threads multiply the arithmetic a kernel can do per second. They do not multiply the rate at which memory can deliver data. Drepper points out that a processor's bandwidth to memory is shared by all its cores, and that a program can end up limited by that bandwidth rather than by its computation.[^drepper] In roofline terms ([P3](p3-roofline.md)), threads raise the compute roof by up to the number of cores, while the memory roof is set by bandwidth that all the cores share. Once a kernel's threads together use all of it, more threads add nothing, and the blocking of [P8](p8-cache-blocking.md) and [P12](p12-fast-gemm.md), which cuts the traffic, is what helps.

For scale, one published case: Boehm's CPU matmul tutorial, at size 1024 on a four-core Intel i7-6700, went from 70 ms to 16 ms when an OpenMP directive split its row and column tiles over eight threads.[^boehm22] That build used `-ffast-math`, which decision 56 forbids, so it shows the shape of the gain on one machine, not what Vortex will get on the reader's.

## Where the work runs

A thread is a request to run somewhere, not a choice of core. The owner's M4 Pro reports eight performance cores, whose `sysctl` level is `perflevel0`, and four efficiency cores, `perflevel1`, with four CPUs per L2 cache at each level: two groups of four performance cores, each group sharing a 16 MiB L2, and one group of four efficiency cores sharing another (checked 2026-09-24).[^local] Figure 3 draws that layout and puts Smith and colleagues' cache analysis on it.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="The M4 Pro's cores as sysctl reports them: two clusters of four performance cores, each cluster sharing one L2 cache, and one cluster of four efficiency cores sharing another L2, all connected to main memory, with notes on which GEMM loop suits threads inside one cluster and across clusters" aria-describedby="p13-f3-desc">
<title id="p13-f3-title">Where threads can land on the M4 Pro</title>
<desc id="p13-f3-desc">Three clusters side by side above a wide box labelled main memory. The first two clusters each hold four small boxes labelled P, above one box labelled L2, 16 MiB, shared by 4. The third cluster holds four small boxes labelled E above one box labelled L2, shared by 4. Lines connect each L2 to main memory. Under the first cluster a note says threads in one cluster share one L2, so they can share one packed A tilde, the jr split. Between the two performance clusters a note says threads in different clusters have separate L2 caches, so each needs its own A tilde, the ic split. Under the efficiency cluster a note says a static share placed here finishes later than one on a performance core.</desc>
<rect class="vx-box" x="20" y="20" width="220" height="130" rx="6"/>
<text class="vx-text-muted" x="30" y="40">performance cluster</text>
<rect class="vx-box-accent" x="32" y="52" width="44" height="36" rx="3"/>
<rect class="vx-box-accent" x="84" y="52" width="44" height="36" rx="3"/>
<rect class="vx-box-accent" x="136" y="52" width="44" height="36" rx="3"/>
<rect class="vx-box-accent" x="188" y="52" width="44" height="36" rx="3"/>
<text class="vx-mono" x="54" y="75" text-anchor="middle">P</text>
<text class="vx-mono" x="106" y="75" text-anchor="middle">P</text>
<text class="vx-mono" x="158" y="75" text-anchor="middle">P</text>
<text class="vx-mono" x="210" y="75" text-anchor="middle">P</text>
<rect class="vx-box-strong" x="32" y="100" width="200" height="36" rx="3"/>
<text class="vx-mono" x="132" y="123" text-anchor="middle">L2, 16 MiB, shared by 4</text>
<rect class="vx-box" x="270" y="20" width="220" height="130" rx="6"/>
<text class="vx-text-muted" x="280" y="40">performance cluster</text>
<rect class="vx-box-accent" x="282" y="52" width="44" height="36" rx="3"/>
<rect class="vx-box-accent" x="334" y="52" width="44" height="36" rx="3"/>
<rect class="vx-box-accent" x="386" y="52" width="44" height="36" rx="3"/>
<rect class="vx-box-accent" x="438" y="52" width="44" height="36" rx="3"/>
<text class="vx-mono" x="304" y="75" text-anchor="middle">P</text>
<text class="vx-mono" x="356" y="75" text-anchor="middle">P</text>
<text class="vx-mono" x="408" y="75" text-anchor="middle">P</text>
<text class="vx-mono" x="460" y="75" text-anchor="middle">P</text>
<rect class="vx-box-strong" x="282" y="100" width="200" height="36" rx="3"/>
<text class="vx-mono" x="382" y="123" text-anchor="middle">L2, 16 MiB, shared by 4</text>
<rect class="vx-box" x="520" y="20" width="220" height="130" rx="6"/>
<text class="vx-text-muted" x="530" y="40">efficiency cluster</text>
<rect class="vx-box" x="532" y="52" width="44" height="36" rx="3"/>
<rect class="vx-box" x="584" y="52" width="44" height="36" rx="3"/>
<rect class="vx-box" x="636" y="52" width="44" height="36" rx="3"/>
<rect class="vx-box" x="688" y="52" width="44" height="36" rx="3"/>
<text class="vx-mono" x="554" y="75" text-anchor="middle">E</text>
<text class="vx-mono" x="606" y="75" text-anchor="middle">E</text>
<text class="vx-mono" x="658" y="75" text-anchor="middle">E</text>
<text class="vx-mono" x="710" y="75" text-anchor="middle">E</text>
<rect class="vx-box-strong" x="532" y="100" width="200" height="36" rx="3"/>
<text class="vx-mono" x="632" y="123" text-anchor="middle">L2, shared by 4</text>
<path class="vx-line" d="M130 150 L130 190"/>
<path class="vx-line" d="M380 150 L380 190"/>
<path class="vx-line" d="M630 150 L630 190"/>
<rect class="vx-box" x="20" y="190" width="720" height="34" rx="4"/>
<text class="vx-text" x="380" y="212" text-anchor="middle">main memory</text>
<text class="vx-text-accent" x="20" y="252">inside one cluster:</text>
<text class="vx-text-muted" x="20" y="270">one L2, so threads can share</text>
<text class="vx-text-muted" x="20" y="288">one packed Ã (split jr)</text>
<text class="vx-text-accent" x="270" y="252">across clusters:</text>
<text class="vx-text-muted" x="270" y="270">separate L2s, so each needs</text>
<text class="vx-text-muted" x="270" y="288">its own Ã (split ic)</text>
<text class="vx-text-accent" x="520" y="252">on this cluster:</text>
<text class="vx-text-muted" x="520" y="270">an equal static share</text>
<text class="vx-text-muted" x="520" y="288">finishes at a different time</text>
</svg>
<figcaption>Figure 3. The owner's M4 Pro as <code>sysctl</code> describes it, with Smith and colleagues' advice placed on it: threads that share an L2 suit a <code>jr</code> split, threads with separate L2 caches an <code>ic</code> split. BLIS's own documentation gives the same mapping. A kernel that uses all twelve cores also meets two kinds of core running at different speeds.</figcaption>
</figure>

The operating system, not the program, decides which cores run its threads. Apple's guidance for Apple silicon says a thread's **quality of service** (QoS) class, a label that tells the system how important and how urgent its work is, influences where the system runs it: background work is more likely to run on lower-performance cores. The same page recommends setting QoS classes, for POSIX threads with `pthread_set_qos_class_self_np`, instead of setting thread priorities by hand.[^apple-tuning] A kernel whose caller is waiting for the answer is not background work, and a threaded Vortex runtime should say so rather than accept a default.

Mixed cores also change load balance. Apple's page warns that with a static distribution of work, threads on the two kinds of core finish at noticeably different times. It recommends dividing the work into more pieces than there are cores, and handing them out dynamically, as its own `concurrentPerform` does with a **work-stealing** scheduler, in which a thread that runs out of work takes pieces from another's queue; for that function it suggests at least three times as many iterations as cores.[^apple-tuning] The walk-through above reached the same trade-off from the other side: more, smaller blocks balance better, and each block is less efficient.

Larger machines add another placement question. In a machine with several processor sockets, each socket usually has memory attached directly to it, and reaching memory attached to another socket costs more; such a machine has **non-uniform memory access** (NUMA). Drepper devotes a section to the hardware and another to programming for it, and extends the idea to caches: threads on cores that share a cache cooperate faster than threads that do not.[^drepper] Smith and colleagues suggest splitting the outermost `jc` loop across sockets that have separate L3 caches, with a separate `B̃` for each NUMA node, kept in that node's local memory.[^smith14] The M4 Pro is one chip, so this chapter's examples have nothing to measure here.

## For Vortex

!!! vortex "Exercise"

    **Build** a pass that runs the `ic` loop of your compiler's GEMM kernel from [P12](p12-fast-gemm.md#for-vortex) on several threads, and the small runtime it calls.

    1. **The legality check.** Before the pass splits a loop, it must establish three facts and record each one: that the loop's component of the dependence vector from [P6](p6-dependence-analysis.md#for-vortex) is `=`; that the ranges it hands to different threads are disjoint and together cover the loop's iterations, proved from the loop's own bounds and the thread count, not by citing the `&mut` rule of [O9](o9-alias-analysis.md#for-vortex); and that the loop body contains no `print`, no call, and no runtime check that [O8](o8-loops.md#for-vortex) has not proved can never fail.
    2. **A fork-join runtime.** A function that takes a range of blocks, the kernel's arguments and a thread count, gives each thread one contiguous share of the blocks, and joins every thread before it returns. Read the thread count from a setting the user controls, such as an environment variable of your choosing, with a default. Each thread packs into its own buffer.
    3. **Remarks**, in the stream from [O1](o1-optimizer-contract.md#for-vortex): a passed remark for each split, naming the loop, the thread count and the absence of a reduction, in the shape "parallel over i-blocks, 8 threads, no reduction"; a missed remark for every loop the pass considers and rejects, naming which of the three facts failed.

    **Not yet:** splitting `pc` or any other reduction loop, which regroups floating-point sums and waits for an explicit opt-in ([P11](p11-floating-point.md)); splitting two loops at once; a pool of threads that outlives one call; work stealing or any dynamic schedule; QoS requests and core affinity; NUMA-aware allocation.

    **Proof that it works:**

    - The contract test from [O1](o1-optimizer-contract.md#for-vortex) passes with the pass on, for every thread count from 1 to 12: standard output, the error line and the exit status match the single-threaded build byte for byte.
    - Three programs with golden remark files, one per rejected fact: a loop that sums into one `f32` variable across its iterations; a loop whose body prints; and a loop whose body indexes an array with a value the compiler cannot bound. Each must stay serial, with a missed remark that names the reason.
    - A stress test that runs the threaded kernel many times on the same inputs and checks that every output is identical to the serial one. A clean run does not prove there is no race, since a race can hide; a single differing run proves there is one.
    - A measurement, filled in from your compiler on your machine, with the date, the matrix size and your compiler's version:

    | Threads | Output identical to serial? | Median time | 95% interval | Speedup over 1 thread | Passed remarks | Missed remarks |
    | --- | --- | --- | --- | --- | --- | --- |
    | 1 | | | | | | |
    | 2 | | | | | | |
    | 4 | | | | | | |
    | 8 | | | | | | |
    | 12 | | | | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **When can a loop's iterations run on separate threads with no synchronization?** When the loop carries no dependence, `=` in its component of the direction vector, so different iterations write different locations.
    - **Why does splitting `ic` or `jr` keep the kernel's answer bitwise identical, while splitting `pc` does not?** Each `ic` or `jr` thread computes its own elements of `c` in the serial order; a `pc` split needs private copies of `c` added at the end, which regroups every sum and makes the answer depend on the thread count.
    - **What does a compiler prove before it splits a Vortex loop?** That the loop carries no dependence, that the threads' ranges partition its iterations, and that the body cannot print or fail, since every thread in a Vortex program must be invisible.
    - **What is the difference between a data race and false sharing?** A data race is two unsynchronized accesses to the same location, one a write: undefined behavior in C++. False sharing is different locations in the same cache line, written by different cores: correct, and slower.
    - **What limits the speedup of a correct split?** Load imbalance, since the slowest thread decides when the kernel ends; work that cannot be split; memory bandwidth shared by all cores; and, on mixed cores, threads that run at different speeds.
    - **What does a thread's QoS class influence on Apple silicon?** Which kind of core the system is likely to run it on; background work tends to land on lower-performance cores.

## Where this comes back

!!! next "You will use this again in"

    - [P14. Algorithms and schedules](p14-algorithms-and-schedules.md): *parallel as a schedule step*, *what a schedule may not change*
    - [P15. Choosing parameters: models or search](p15-choosing-parameters.md): *block sizes and the threads that share them*
    - [P16. Capstone: the ladder, measured](p16-capstone.md): *rungs 8 and 8b*, *matching thread counts in a fair comparison*
    - [G6. Synchronization, atomics and reductions](../gpu/g6-synchronization.md): *reductions across many more threads than a CPU has cores*

## Sources and further reading

Read section III of Smith and colleagues first: a few pages that go through the five loops one at a time and ask what the threads would share. Then read Drepper's section 6.4 for false sharing and memory bandwidth, and the BLIS multithreading guide for how the same analysis becomes a user's settings.

[^local]: Checked on the owner's Apple M4 Pro with Apple clang 21, 2026-09-24: `clang++ -fopenmp` reports "unsupported option"; a two-line program prints `std::hardware_destructive_interference_size` as 256; `sysctl hw.cachelinesize` reports 128; `sysctl hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu hw.perflevel0.cpusperl2 hw.perflevel1.cpusperl2 hw.perflevel0.l2cachesize` reports 8, 4, 4, 4 and 16777216.
[^openmp6]: OpenMP Architecture Review Board, "OpenMP API Specifications" page: OpenMP API 6.0, November 2024. <https://www.openmp.org/specifications/>
[^cppref-thread-ctor]: cppreference, `std::thread::thread`, section "Notes": arguments are moved or copied, and references must be wrapped. <https://en.cppreference.com/w/cpp/thread/thread/thread>
[^cppref-thread-dtor]: cppreference, `std::thread::~thread`. <https://en.cppreference.com/w/cpp/thread/thread/~thread>
[^cppref-mt]: cppreference, "Multi-threaded executions and data races", section "Data races". <https://en.cppreference.com/w/cpp/language/multithread>
[^smith14]: Tyler M. Smith, Robert van de Geijn, Mikhail Smelyanskiy, Jeff R. Hammond and Field G. Van Zee, "Anatomy of High-Performance Many-Threaded Matrix Multiplication", *IEEE 28th International Parallel and Distributed Processing Symposium*, 2014: section III, parts A to F (each loop in turn, including the race and reduction in part E and NUMA in part F), section IV.C (the fork-join model and the choice of loops on the Xeon Phi), and the discussion of packing and load balance in the Xeon Phi and Blue Gene/Q results. <https://doi.org/10.1109/IPDPS.2014.110> (authors' copy: <https://www.cs.utexas.edu/~flame/pubs/blis3_ipdps14.pdf>)
[^blis-mt]: BLIS Project, "Multithreading", read on 2026-09-24: sections "Enabling multithreading" and "Specifying multithreading". <https://github.com/flame/blis/blob/master/docs/Multithreading.md>
[^drepper]: Ulrich Drepper, "What Every Programmer Should Know About Memory", version 1.0, 2007: section 3.3.4, "Multi-Processor Support" (MESI); section 5, "NUMA Support"; section 6.4.1, "Concurrency Optimizations", with Figures 6.10 and 6.11 (false sharing measured on four Pentium 4 processors and on a Core 2 QX 6700); section 6.4.3, "Bandwidth Considerations"; section 6.5, "NUMA Programming". <https://www.akkadia.org/drepper/cpumemory.pdf>
[^cppref-hdis]: cppreference, `std::hardware_destructive_interference_size`, `std::hardware_constructive_interference_size`. <https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size>
[^boehm22]: Simon Boehm, "Fast Multidimensional Matrix Multiplication on CPU from Scratch", August 2022: the multithreading section and the results table on an Intel i7-6700, built with `-O3 -march=native -ffast-math`. <https://siboehm.com/articles/22/Fast-MMM-on-CPU>
[^apple-tuning]: Apple, "Tuning your code's performance for Apple silicon", read on 2026-09-24: sections "Assign Quality-of-Service (QoS) Classes to Work" and "Manage Parallel-Computation Tasks Efficiently". <https://developer.apple.com/documentation/apple-silicon/tuning-your-code-s-performance-for-apple-silicon>
