# P13. Multithreading

<p class="page-intro">Splitting a kernel across cores changes nothing about the answer, if you split the right loop, and everything about it, if you split the wrong one. This chapter is about telling the two apart, and about a kind of slowdown that leaves no trace in the answer at all.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 40 minutes · Builds on: [P12. Anatomy of a fast GEMM](p12-fast-gemm.md), [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is a cache line, and why does a cache move data in that unit rather than one value at a time?"

        A cache line is the fixed-size, aligned chunk a cache transfers on a miss, the unit a fetch costs, not one value. Two different addresses that fall in the same line move together, whether or not the code that touches them has anything to do with each other.

        Introduced in [P2. The memory hierarchy](p2-memory-hierarchy.md#cache-lines-and-why-order-matters).

    ??? question "What can a compiler assume about a value read through one parameter and written through a separate &mut parameter?"

        That they can never alias: storage reached through a `&mut` parameter is reached through no other parameter, so the write and the read can never touch the same bytes, and no runtime check is needed to reorder them.

        Introduced in [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md#one-store-one-load).

    ??? question "Matrix multiplication's dependence vector along i, j, k is (=, =, <). What does that say about the three loops?"

        The `<` on k means each trip depends on the one before it: they accumulate into the same `c[i, j]`. The `=` on i and j means those two loops carry no dependence at all: every value of i, and every value of j, could run in any order, including at the same time, without changing which values land in which `c[i, j]`.

        Introduced in [P7. Loop transformations](p7-loop-transformations.md#strip-mining-and-tiling).

    ??? question "What are the three kinds of optimizer remark, and what does each one report?"

        A **passed** remark reports a transformation that was made, a **missed** remark reports one that was attempted and not made, and an **analysis** remark reports something a pass worked out along the way, often the reason behind a passed or missed remark.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#remarks-the-optimizers-report).

!!! goals "In this chapter"

    - Recognize when a loop's iterations can run on separate threads with no synchronization, and when they need a reduction.
    - Explain why splitting matrix multiplication along i or j keeps the answer bitwise identical, and why splitting along k does not.
    - Reproduce false sharing: two threads with no shared variable, slowed down by one shared cache line, and fix it with alignment.
    - Name the difference between a data race and false sharing, and why only one of them changes the answer.
    - Connect thread placement, QoS and NUMA to what a compute-bound kernel needs from the scheduler.

## Splitting work without asking permission

Here is the smallest version of the question this chapter answers. A program sums the eight rows of a small grid, one row at a time, into an output array:

```cpp
for (int row = 0; row < 8; ++row) {
    out[row] = sum_of_row(row);
}
```

Nothing about row 3's sum depends on row 5's sum. A **thread** is an independent stream of instructions that the operating system can run on its own CPU core, at the same time as other threads in the same program, sharing the same memory. If two threads exist, one can run rows 0 to 3 and the other rows 4 to 7, and the loop finishes in about half the time, on a machine with a spare core to give it. Nothing needs to be locked, because the two threads never touch the same element of `out`.

The example below does exactly this, with `std::thread` rather than pseudocode. Apple clang on the owner's machine rejects `-fopenmp` outright ("unsupported option", checked 2026-09-24) [^local-openmp], so every threaded example in this chapter reaches the operating system's threads directly, through the C++ standard library's `std::thread`.[^cppref-thread] Production libraries such as BLIS and OpenBLAS more often use OpenMP, a directive-based model, current as OpenMP 6.0 (November 2024)[^openmp6]: a programmer writes `#pragma omp parallel for` above a loop, and the compiler generates the thread management. `std::thread` is the same idea with the management written out by hand, which is exactly what makes it a better teaching tool here: nothing is hidden behind a pragma.

--8<-- "includes/examples/optimize/p13-multithreading/split_rows.cpp.md"

Four threads, each given a contiguous, disjoint range of rows through plain function arguments; each thread writes only inside its own range of `out`; `join` waits for all four before the totals are read. A **data race** is two threads accessing the same memory location, with no ordering between them, where at least one access writes: it is undefined behavior in C++, and the reason this example is safe is that it has no shared write at all, only four disjoint ones. The output is deterministic and identical to what a single thread computes in the same order, because every thread's slice of the work is independent of every other's.

## Which loop in the kernel to split

[P12](p12-fast-gemm.md) builds the matrix multiplication kernel out of nested loops around blocks: an outer loop over row blocks of `c` (commonly called the `ic` loop, in the naming BLIS uses for its five-loop structure[^blis-mt]), a loop over column blocks (`jc`), a loop that walks the shared `k` dimension in panels (`pc`), and two more loops inside the micro-kernel. Splitting this nest across threads means picking one or more of those loops to run its iterations on different threads instead of one after another. The "before you start" box above already gives the answer for the outermost two: matrix multiplication's dependence vector along i, j, k is `(=, =, <)`, so i and j carry no dependence, and any split along either one is as safe as the two-thread row example above. Splitting along k is different: every block of k accumulates into the same `c[i, j]`, so two threads computing different k-ranges are writing the same output element, which is exactly a data race unless something changes.

The fix for a k-split is a **reduction**: give each thread its own private copy of the output block, let each accumulate its own partial sum over its share of k, then add the partial sums together at the end, once, after every thread has finished. This removes the race. It does not remove a second cost, one specific to floating point. Adding partial sums in a different grouping than the strict left-to-right order can change the last few bits of the result, because floating-point addition is not associative: `(a + b) + c` and `a + (b + c)` can round to different values. The example below computes the same sum of the same 2048 numbers two ways, once as a single accumulator that adds every term in order, and once as two half-sized accumulators added together at the end, the shape a two-thread k-split would produce:

--8<-- "includes/examples/optimize/p13-multithreading/k_split_reassociates.cpp.md"

The two results differ in their last bits, and `identical` prints `no`. Nothing here uses `-ffast-math` or any flag that relaxes floating-point rules: this is plain `float` addition, computed by two different, both mathematically valid, groupings of the same terms. Vortex's runtime and numerical rules commit to a single order for any given source program, so a k-split reduction is not a transformation the compiler may ever choose on its own; it belongs with FMA contraction as something that changes the observable answer, and Vortex would need to expose it as an explicit opt-in before generating it, the way [P11](p11-floating-point.md) covers for reassociation in general. Splitting along i or j never has this problem: each thread computes some of the C elements from start to finish, in exactly the order the source program specifies, so an i- or j-split kernel and a single-threaded one compute the identical sequence of additions for every element, on different cores at the same time. This is why the published research behind BLIS treats parallelizing `ic` and the innermost `jr` loop as the well-behaved case, and parallelizing `pc` (a k-split) as the one that needs a reduction and a different implementation path.[^smith14] BLIS itself parallelizes four of its five loops and reads a `BLIS_NUM_THREADS` environment variable to decide how many threads to use across them.[^blis-mt]

??? check "Why does splitting the k loop need a reduction, while splitting i or j does not?"

    Every trip around k writes into the same `c[i, j]`: the dependence vector's `<` on k means trip k+1 reads the value trip k left behind. Two threads computing different k-ranges would both write that same element with no ordering between them, which is a data race, so each must accumulate into a private copy that gets combined afterward. Splitting i or j hands each thread a different set of `c` elements to own outright, so there is nothing to combine, and no order to change.

## The proof a compiler needs, and the one it does not have yet

[O9](o9-alias-analysis.md#one-store-one-load) gives Vortex's compiler a theorem about a whole `&mut` parameter: nothing else in the function signature can reach the storage it points to. That theorem is exactly what removes the runtime alias checks a C compiler would otherwise place in front of the vectorized loop. Threading needs a second, narrower theorem that O9 does not supply: that two *sub-ranges* of the same `&mut` parameter, handed to two different threads, do not overlap each other. Splitting the `ic` loop across two threads by row means one thread gets rows 0 to 31 of `c` and the other rows 32 to 63; both threads received the same `&mut [f32; 64, 64]`, so O9's whole-parameter exclusivity is silent on whether the two threads' writes can collide. The extra fact that makes this safe is arithmetic, not aliasing: the two row ranges were chosen to partition `0..64` with no overlap, and every write a thread performs stays inside the rows it was given. A parallelizing pass has to construct and check that partition itself; nothing upstream hands it that proof for free.

??? check "A compiler splits a &mut array's rows across two threads, using O9's aliasing rule to justify it. Is that argument complete?"

    No. O9's rule says the whole array cannot be reached through any other parameter, which rules out `a` or `b` aliasing `c`. It says nothing about whether the two row ranges the compiler handed to the two threads overlap each other; that is a separate, arithmetic proof about the partition, not an aliasing fact.

## False sharing: correct, and slower anyway

Splitting work correctly is not the end of the story. Give two threads their own, entirely private counters, one each, with no shared variable anywhere in sight, and the program can still slow down measurably, for a reason invisible in the source code. The two counters live in memory, and if they happen to land close enough together, both fall inside the same cache line.

A cache line, from [P2](p2-memory-hierarchy.md#cache-lines-and-why-order-matters), moves as a unit. When core 0 writes its counter, the cache line holding it is marked exclusive to core 0's cache; if core 1's counter shares that line, core 1's next write has to fetch the line back from core 0 first, even though core 1 never reads or writes core 0's counter. The two cores end up passing the line back and forth on every write, each invalidating the other's copy. This is **false sharing**: threads with no logical data dependence, slowed down by a physical one, the shared cache line.[^drepper-fs] Figure 1 shows the mechanism.

<figure class="vx-figure">
<svg viewBox="0 0 760 360" role="img" aria-label="Two layouts for two per-thread counters: packed, where both fall on one cache line and the cores keep invalidating each other's copy, and padded, where each counter has its own line and the cores never interfere" aria-describedby="p13-f1-desc">
<title id="p13-f1-title">One shared line versus two private lines</title>
<desc id="p13-f1-desc">Left panel, labelled packed: a Core 0 box and a Core 1 box each send a flowing arrow down into one shared box marked 128-byte line, holding packed[0] and packed[1], which pulses to show it repeatedly changing hands. Right panel, labelled padded: a Core 0 box and a Core 1 box each send a plain, still arrow down into their own separate box, one marked 128-byte line holding padded[0], the other marked 128-byte line holding padded[1], with visible empty space between the two lines and no shared box.</desc>
<defs><marker id="p13-f1-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="24">Packed</text>
<text class="vx-text-muted" x="20" y="42">one line, two writers</text>
<rect class="vx-box" x="20" y="60" width="130" height="40" rx="4"/>
<text class="vx-text" x="85" y="85" text-anchor="middle">Core 0</text>
<rect class="vx-box" x="210" y="60" width="130" height="40" rx="4"/>
<text class="vx-text" x="275" y="85" text-anchor="middle">Core 1</text>
<path class="vx-flow" d="M85 100 L140 190" marker-end="url(#p13-f1-head)"/>
<path class="vx-flow" d="M275 100 L220 190" marker-end="url(#p13-f1-head)"/>
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
<path class="vx-line" d="M485 100 L485 190" marker-end="url(#p13-f1-head)"/>
<path class="vx-line" d="M675 100 L675 190" marker-end="url(#p13-f1-head)"/>
<rect class="vx-box" x="420" y="200" width="130" height="50" rx="4"/>
<text class="vx-mono" x="485" y="222" text-anchor="middle">128-byte line</text>
<text class="vx-mono" x="485" y="240" text-anchor="middle">padded[0]</text>
<rect class="vx-box" x="610" y="200" width="130" height="50" rx="4"/>
<text class="vx-mono" x="675" y="222" text-anchor="middle">128-byte line</text>
<text class="vx-mono" x="675" y="240" text-anchor="middle">padded[1]</text>
<text class="vx-text-muted" x="420" y="300">writes never touch</text>
<text class="vx-text-muted" x="420" y="316">a line the other core owns</text>
</svg>
<figcaption>Figure 1. Two per-thread counters packed next to each other share a cache line, so every write bounces it between cores; padding each counter out to its own line stops the bouncing, without changing what either thread computes.</figcaption>
</figure>

The fix is alignment, not a lock: pad each thread's data out to a full cache line so nothing else can ever share it. C++17 gives this constant a name, `std::hardware_destructive_interference_size`,[^cppref-hdis] which a program can pass to `alignas`. The example below builds two counters two ways, an ordinary packed pair and a pair padded with `alignas(std::hardware_destructive_interference_size)`, and checks, from the byte distance between adjacent array elements alone, whether each pair *can* share a line:

--8<-- "includes/examples/optimize/p13-multithreading/false_sharing_layout.cpp.md"

On the owner's machine, `std::hardware_destructive_interference_size` is 256 (Apple clang 21, measured by this example, 2026-09-24), twice the 128-byte line that [P7](p7-loop-transformations.md) reports from `sysctl hw.cachelinesize` on the same machine. The standard library's constant is a portable, conservative guess meant to be safe across nearby hardware, not a readout of the exact line size, and code that relies on it pays for two lines of padding to be sure of one. Note what this example does not measure: how much faster the padded version runs. That number needs a clock, and this book's examples never print one, since a wall-clock time is not deterministic across machines or even across two runs on the same machine. Measuring it is worth doing on your own hardware, following [P1](p1-measure-first.md)'s protocol: run both versions with several threads each incrementing their own counter many times, repeat each version several times, and report the median with a confidence interval, not one run.

| Threads | Packed, median time | Padded, median time | Speedup |
| --- | --- | --- | --- |
| 2 | | | |
| 4 | | | |
| 8 | | | |

??? check "Two threads never read or write each other's variables. Can the program still slow down because of the cores they run on?"

    Yes, if the two variables share a cache line. Every write to either one invalidates the other core's cached copy of the whole line, forcing it to be fetched again, even though neither thread's code ever names the other's variable. This is false sharing: a performance cost with no effect on the answer, unlike a data race, which is a correctness bug.

## Where the work runs

A thread is a promise to run somewhere, not a promise about which core. The owner's Apple M4 Pro has eight performance ("P") cores and four efficiency ("E") cores, with every four P-cores sharing one L2 cache (`sysctl hw.perflevel0.cpusperl2`, checked 2026-09-24) [^local-topology]; Apple's own guidance for tuning code on Apple silicon says a thread's **quality of service (QoS)** class, a hint about how urgent its work is, steers the scheduler's choice of which cluster runs it, and that a background QoS thread tends to land on a lower-performance core.[^apple-tuning] A matmul kernel is compute-bound work the caller is waiting on, so a real implementation should ask for a QoS class that keeps it on the fast cluster, rather than accept whatever default a generic `std::thread` gets.

A related placement question, on larger machines than this one, is **NUMA** (non-uniform memory access): on a multi-socket system, each socket has memory attached directly to it, and a core reading memory attached to another socket pays more for that read than a core reading its own socket's memory.[^lameter13] The M4 Pro is a single package, so this chapter's examples have nothing to measure here, but the underlying idea is the same one QoS addresses on one package: a scheduler decision about where a thread runs can matter as much as the algorithm that thread executes, and a compute-bound kernel benefits from telling the scheduler what it needs instead of leaving the choice to a generic default.[^drepper-numa]

??? check "Why doesn't the false-sharing fix (padding to a cache line) also solve the placement question this section raises?"

    They are different costs. False sharing is about two pieces of data landing too close together in memory; padding moves them apart. Placement is about which physical core, and which cluster or socket, runs a given thread; no amount of padding changes where the operating system schedules that thread. Fixing one leaves the other exactly as it was.

## For Vortex

!!! vortex "Exercise"

    **Build** a parallelization pass for your compiler's `ic` loop (the outer loop over row-blocks of the output, from your [P12](p12-fast-gemm.md) kernel), plus the small runtime it needs.

    1. **The proof obligation.** Before the pass may split a loop across threads, it must show two things: that the loop's dependence vector carries no dependence on the split dimension (the "remember" box above gives matrix multiplication's own vector), and that the row ranges it hands to different threads are disjoint. The second is arithmetic on the loop's own bounds, not aliasing, and it is new relative to [O9](o9-alias-analysis.md#for-vortex)'s whole-parameter exclusivity: your pass has to construct the partition and check it, not only cite the `&mut` rule.
    2. **A minimal thread pool.** A function that takes a row range and the kernel's usual arguments, spawns one thread per block (bounded by a fixed thread count, not one thread per row), and joins all of them before returning. It does not need work stealing, a persistent pool, or dynamic load balancing; a fixed static split, one contiguous range of rows per thread, is enough for a first version.
    3. **Remarks**, in the format [O1](o1-optimizer-contract.md#remarks-the-optimizers-report) set up: a passed remark for the split it makes, naming the thread count and the loop, in the shape the research behind this chapter suggests, "parallel over i-blocks, 8 threads, no reduction"; a missed remark, naming the reason, for any loop your pass considers and rejects, such as one whose dependence vector does not permit it.

    **Not yet:** splitting the `k` (`pc`) loop, which needs a reduction and changes floating-point results, and belongs with FMA contraction as a future, explicit opt-in ([P11](p11-floating-point.md)); QoS or core-affinity requests; a thread pool that outlives one call; nested parallelism across more than one loop at once; anything resembling NUMA-aware allocation.

    **Proof that it works:**

    - The contract test from [O1](o1-optimizer-contract.md#for-vortex) passes with the pass turned on: standard output, the error line and the exit status match the single-threaded build byte for byte, for every thread count from 1 to 8.
    - A stress test that runs the kernel hundreds of times with the same inputs and asserts the output is identical on every run: a passing run does not prove there is no race, since a race can hide, but a flaky one proves there is.
    - A golden remark file recording the pass's decision for this chapter's kernel.
    - A measurement, filled in from your own compiler and machine, with the date and thread count:

    | Threads | Output identical to serial? | Passed remarks | Missed remarks |
    | --- | --- | --- | --- |
    | 1 | | | |
    | 2 | | | |
    | 4 | | | |
    | 8 | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **What makes a loop safe to split across threads with no synchronization?** Its iterations write disjoint outputs and depend on no earlier iteration: a dependence vector with no ordering (`=`) on the split dimension.
    - **Why does splitting matrix multiplication along i or j keep the answer bitwise identical, while splitting along k does not?** i and j carry no dependence, so each thread computes some output elements start to finish in the source order. k accumulates into shared output, so a split needs a reduction, and combining partial sums in a different grouping can change the last bits of a floating-point result.
    - **What is a data race, and is it the same thing as false sharing?** A data race is two threads accessing the same memory with no ordering, one of them a write: undefined behavior, and a correctness bug. False sharing is two threads with no shared variable, slowed down because their separate variables share a cache line: a performance cost with no effect on the answer.
    - **What fixes false sharing?** Padding each thread's data out to its own cache line, commonly with `alignas(std::hardware_destructive_interference_size)`, so no two threads' data can ever occupy the same line.
    - **What proof does a threaded `&mut` split need, beyond O9's aliasing rule?** That the sub-ranges handed to different threads are disjoint, an arithmetic fact about the partition, not an aliasing fact about the parameter.
    - **What does a thread's QoS class influence?** Which cluster of cores the scheduler is likely to run it on; a background-priority thread tends to land on a lower-performance core.

## Where this comes back

!!! next "You will use this again in"

    - [P14. Algorithms and schedules](p14-algorithms-and-schedules.md): *thread count as a schedule choice*, *what a schedule may not change*
    - [P15. Choosing parameters: models or search](p15-choosing-parameters.md): *thread count as a tuned parameter*
    - [P16. Capstone: the ladder, measured](p16-capstone.md): *comparators that read a thread-count environment variable*
    - [O11. Undefined behavior, poison and correct optimization](o11-undefined-behavior.md): *a data race as undefined behavior*
    - [G6. Synchronization, atomics and reductions](../gpu/g6-synchronization.md): *reductions across many more threads than a CPU has cores*

## Sources and further reading

Read Drepper's chapter on multiprocessor concerns first for false sharing and NUMA in one place, then the Smith et al. paper for how BLIS turns one micro-kernel into a many-threaded library without changing it.

[^cppref-thread]: cppreference, `std::thread`. <https://en.cppreference.com/w/cpp/thread/thread>
[^openmp6]: OpenMP Architecture Review Board, "OpenMP Application Programming Interface", Version 6.0, November 2024. <https://www.openmp.org/specifications/>
[^local-openmp]: Checked locally: `clang -fopenmp` under Apple clang 21 on the owner's Apple M4 Pro reports "unsupported option", 2026-09-24.
[^blis-mt]: Field G. Van Zee, "BLIS Multithreading", BLIS documentation. <https://github.com/flame/blis/blob/master/docs/Multithreading.md>
[^smith14]: Tyler M. Smith, Robert van de Geijn, Mikhail Smelyanskiy, Jeff R. Hammond and Field G. Van Zee, "Anatomy of High-Performance Many-Threaded Matrix Multiplication", 28th IEEE International Parallel and Distributed Processing Symposium (IPDPS), 2014. <https://doi.org/10.1109/IPDPS.2014.110> (free copy: <https://www.cs.utexas.edu/~flame/pubs/blis3_ipdps14.pdf>)
[^drepper-fs]: Ulrich Drepper, "What Every Programmer Should Know About Memory", version 1.0, 2007, section 6.4.1, "Concurrent Use of Exclusive Cache Lines". <https://www.akkadia.org/drepper/cpumemory.pdf>
[^cppref-hdis]: cppreference, `std::hardware_destructive_interference_size`. <https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size>
[^local-topology]: Checked locally: `sysctl hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu hw.perflevel0.cpusperl2` on the owner's Apple M4 Pro report 8 performance cores, 4 efficiency cores, and 4 CPUs per performance-cluster L2, 2026-09-24.
[^apple-tuning]: Apple, "Tuning your code's performance for Apple silicon". <https://developer.apple.com/documentation/apple-silicon/tuning-your-code-s-performance-for-apple-silicon>
[^lameter13]: Christoph Lameter, "NUMA: An Overview", *ACM Queue* 11(7), 2013. <https://doi.org/10.1145/2508834.2513149>
[^drepper-numa]: Ulrich Drepper, "What Every Programmer Should Know About Memory", version 1.0, 2007, sections 5 ("What Programmers Can Do", NUMA-aware placement) and 6.5 ("NUMA Programming"). <https://www.akkadia.org/drepper/cpumemory.pdf>
