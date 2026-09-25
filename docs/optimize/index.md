<!-- generated overview -->
# Optimize

<p class="page-intro">How a compiler makes correct code fast: the middle end that removes waste, then the CPU performance work that turns the naive matrix multiplication into a fast one.</p>

The P chapters form one path, from the naive matrix multiplication to a fast one: [the CPU matmul ladder](ladder.md) shows every rung, the chapter that teaches it and whether it keeps the bits.


## The middle end

| Chapter | What it covers | Status |
| --- | --- | --- |
| [O1. The optimizer's contract](o1-optimizer-contract.md) | What an optimization may and may not change, how to prove a transformation correct, and how a compiler reports what it did (optimization remarks). | Published |
| [O2. Control-flow graphs and dominance](o2-cfg-and-dominance.md) | Basic blocks, edges, dominators and the dominator tree: the map every later analysis walks. | Published |
| [O3. SSA form: construction and destruction](o3-ssa.md) | Static single assignment, phi nodes, building SSA from a CFG and taking it apart again. | Published |
| [O4. Dataflow analysis](o4-dataflow.md) | Lattices, transfer functions and fixed points: the general machine behind liveness, reaching definitions and constant propagation. | Published |
| [O5. Constants and dead code](o5-constants-and-dead-code.md) | Constant folding and propagation, sparse conditional constant propagation, and aggressive dead code elimination. | Published |
| [O6. Redundancy: CSE, GVN, PRE and LICM](o6-redundancy.md) | Finding work the program repeats and doing it once: common subexpressions, value numbering, partial redundancy and loop-invariant code motion. | Published |
| [O7. Calls and aggregates: inlining and SROA](o7-inlining-and-sroa.md) | When to copy a function body into its caller, and how splitting structs and small arrays into scalars lets later passes see through them. | Published |
| [O8. Loops: structure, induction variables and bounds checks](o8-loops.md) | Natural loops, induction variables, trip counts and scalar evolution, and removing the bounds checks Vortex inserts. | Published |
| [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md) | Deciding when two memory accesses cannot touch the same place, and why Vortex's &mut makes that easy. | Published |
| [O10. Pass managers and pipelines](o10-pass-pipelines.md) | How passes are scheduled, why their order matters, and how LLVM's pass pipelines are built. | Published |
| [O11. Undefined behavior, poison and correct optimization](o11-undefined-behavior.md) | Why optimizers exploit undefined behavior, what poison and undef mean in LLVM, and how Vortex's trapping rules fit. | Published |
| [O12. Testing an optimizer](o12-testing-optimizers.md) | Golden tests, differential testing, random programs, equivalence modulo inputs and test-case reduction. | Published |

## CPU performance

| Chapter | What it covers | Status |
| --- | --- | --- |
| [P1. Measure first](p1-measure-first.md) | How to time code so the numbers mean something, before changing anything. | Published |
| [P2. The memory hierarchy](p2-memory-hierarchy.md) | Caches, lines, associativity, bandwidth and latency, and what they mean for loops over arrays. | Published |
| [P3. The roofline model](p3-roofline.md) | Arithmetic intensity and the two ceilings that bound a kernel's speed. | Published |
| [P4. Seeing inside the CPU: counters and tools](p4-counters-and-tools.md) | Hardware performance counters, profilers and static analyzers such as llvm-mca. | Published |
| [P5. The microarchitecture shelf](p5-microarchitecture.md) | Pipelines, out-of-order execution, ports and the manuals that describe them. | Published |
| [P6. Dependence analysis](p6-dependence-analysis.md) | Which loop iterations depend on which: distance and direction vectors, and the tests that compute them. | Published |
| [P7. Loop transformations](p7-loop-transformations.md) | Interchange, fusion, fission, unrolling and unroll-and-jam, and when each is legal. | Published |
| [P8. Cache blocking](p8-cache-blocking.md) | Tiling loops so the data they reuse stays in cache. | Published |
| [P9. The polyhedral model](p9-polyhedral-model.md) | Loop nests as integer sets and affine maps, and scheduling them as a whole. | Published |
| [P10. Vectorization](p10-vectorization.md) | SIMD on NEON, SVE and SME, loop and SLP vectorizers, and what stops them. | Published |
| [P11. Floating point under optimization](p11-floating-point.md) | Which transformations change floating-point results, and how Vortex keeps them bitwise identical. | Published |
| [P12. Anatomy of a fast GEMM](p12-fast-gemm.md) | Packing, register micro-kernels and the Goto and BLIS structure of fast matrix multiplication. | Published |
| [P13. Multithreading](p13-multithreading.md) | Splitting a kernel across cores, and what that does to results and speed. | Published |
| [P14. Algorithms and schedules](p14-algorithms-and-schedules.md) | Separating what to compute from how to compute it: Halide, TVM and Exo. | Published |
| [P15. Choosing parameters: models or search](p15-choosing-parameters.md) | Analytical cost models against empirical autotuning for tile sizes and unroll factors. | Published |
| [P16. Capstone: the ladder, measured](p16-capstone.md) | Every rung of the CPU matmul ladder, measured against Accelerate, OpenBLAS and BLIS. | Published |
