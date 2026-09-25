# A10. ML framework bridge

<p class="page-intro">This case study will make Vortex kernels callable from PyTorch as custom operators and compare them, on the same shapes, with PyTorch's own execution, with torch.compile and with a Triton kernel. Nothing has been built yet, so every result table below is empty.</p>

<p class="vx-meta">Case study A10 · Status: Not started · Evidence so far: none</p>

This page was written before the work, on purpose. It fixes the question, the
comparisons and the success criteria now, so that results cannot quietly
redefine them later. Every table has defined columns and empty cells. A cell
gets a value only when that value was measured or counted, and each value must
trace back to a commit and a raw data file. Until then the page makes no claim.
It is one of the [case studies](index.md) described in
[Vortex for reviewers](../index.md).

## The question

**Question.** Called from PyTorch on the same shapes, how do Vortex's kernels
compare in accuracy and speed with PyTorch's eager mode, with torch.compile
and with a Triton kernel, and how much does calling them through the framework
cost?

**Claim.** None yet. When the tables are full, the claim will have this shape:
"On device *D* at shape *S*, the Vortex operator stayed within the stated
error bound and ran at *r* times the speed of the fastest alternative; below
size *n*, the cost of the call through PyTorch dominated."

A **custom operator** is a function registered with PyTorch so that it can be
called like a built-in operation.[^pt-custom-ops] **Eager mode** is PyTorch
running each operation as soon as it is called. **torch.compile** is
PyTorch's compiler entry point: it captures Python code with TorchDynamo and
hands it to a back end, `inductor` by default.[^torch-compile] **Triton** is a
language and compiler for tiled GPU kernels.[^triton-paper]

## Why employers care

In the compiler job postings read while planning these pages (September 2026),
most open roles served machine-learning workloads. Postings for those teams
often preferred familiarity with ML frameworks such as PyTorch and with ML
compilers, and many mentioned the models themselves.

Vortex is not meant to be a machine-learning framework; that is a stated
[non-goal](../../philosophy.md#non-goals). But a kernel language is only
useful to those teams if its kernels can be called from where their work
runs. A bridge that is measured honestly against the framework's own compiler
shows where a kernel compiler sits in that stack, and what the framework
already does well.

## What to build

This study makes sense only after the
[CPU matmul ladder](cpu-matmul-ladder.md) (A1) or the
[GPU matmul ladder](gpu-matmul-ladder.md) (A8) exists, since it needs kernels
worth calling. Two facts about Vortex v0.1 shape the rest:

- **Shapes are fixed at compile time.** v0.1 has no runtime-sized arrays
  ([arrays, section 7.9](../../specification/arrays.md#79-excluded-array-behavior)),
  so each shape gets its own function
  ([stage 10](../../compiler/guide/stage-10-matrix-multiplication.md#one-function-per-shape)).
  The bridge therefore compiles one kernel per supported shape ahead of time
  and rejects other shapes with a clear error.
- **The array ABI is not specified yet.** Vortex fixes the order elements are
  stored in: contiguous, row-major
  ([arrays, section 7.8](../../specification/arrays.md#78-memory-and-layout);
  [decision 43](../../decisions/arrays.md#d43)). But element size, alignment
  and padding stay implementation-defined, and ABI compatibility with other
  languages is not specified in v0.1
  ([structs, section 8.8](../../specification/structs.md#88-layout)). Before
  any kernel can be called from C++, a
  [decision record](../../decisions/index.md) must fix how exported kernels
  lay out arguments at that boundary and receive shared and mutable
  references.

| Part | What it does | Done when | Taught in |
| --- | --- | --- | --- |
| Export decision | Fixes how a compiled Vortex kernel is called from C++: symbol names, array layout, and how shared and mutable references are passed | A decision record, and a C++ test that calls a compiled kernel directly | [A4](../../backend/a4-calling-conventions.md) |
| Custom operator | Registers each Vortex kernel with PyTorch from C++, after checking data type, device, shape and memory layout | `torch.library.opcheck`, which tests that an operator is registered correctly,[^pt-custom-ops] passes, and a Python test calls the operator | [M11](../../mlir/m11-ml-compilers.md) |
| Shape table | Maps each supported shape to its compiled kernel | A test passes an unsupported shape and receives the error | [Stage 10](../../compiler/guide/stage-10-matrix-multiplication.md) |
| torch.compile support | Registers the "fake" implementation that torch.compile uses to trace through the operator without real data[^pt-custom-ops] | A compiled model that calls the operator gives the same results as eager mode | [M11](../../mlir/m11-ml-compilers.md) |
| Comparison kernels | The same computations in eager mode, with torch.compile, and as a Triton kernel based on the Triton matmul tutorial[^triton-tutorial] | Each one stays within the error bound below | [G13](../../gpu/g13-tile-languages.md) |

The workloads are matrix products in square, tall-and-thin and batched shapes
(the batched ones of the kind attention layers compute), and a matrix product
followed by an elementwise operation, to see which paths fuse the two into
one kernel. Softmax, and so full attention, is left out: v0.1 defines no
exponential function.

Not in this study: gradients (automatic differentiation is outside Vortex's
initial scope), shapes known only at run time, frameworks other than PyTorch,
and packaging for distribution.

## Method

The measurement protocol is on
[How Vortex performance is measured](../measuring.md), and the test protocol
is on [How Vortex is tested](../testing.md). This section adds only what is
specific to this study.

### What is compared

Four ways to compute each workload:

1. PyTorch eager mode.
2. torch.compile, in its default mode and in `max-autotune` mode, which the
   documentation says uses Triton-based or template-based matrix
   multiplications on supported devices.[^torch-compile]
3. A Triton kernel based on the official tutorial, on the NVIDIA machine only.
4. The Vortex kernel, through the custom operator.

**Accuracy.** Each result is compared with a float64 reference computed by
PyTorch, as the largest relative error. Exact equality between paths is not
expected, because each library may add in its own order. Instead, every path
must stay within an error bound written down before the run, which grows with
the length of the sums.

**Speed.** Runs are timed with `torch.utils.benchmark`, which handles warm-up
and CUDA synchronization and uses one thread unless told otherwise.[^pt-benchmark]
Thread counts are set to match across paths and recorded. First calls, which
include compilation for torch.compile,[^torch-compile] are timed separately
from steady-state calls.

**Cost of the bridge.** The same Vortex kernel is timed when called directly
from C++ and when called through the custom operator. For small inputs, run
time can be dominated by overhead (Python, framework dispatch, kernel
launches) rather than by arithmetic or memory traffic.[^brrr] This comparison
shows how much of that overhead the bridge adds.

The environment is recorded with `torch.__config__.show()`, which describes
how the installed PyTorch was built.[^pt-config]

### Machines

- **The owner's Mac** runs the CPU paths: eager mode, torch.compile and the
  Vortex CPU kernel. If the Apple track of case study A8 exists, the Vortex
  GPU kernel is compared there with PyTorch on its MPS device, PyTorch's back
  end for Apple GPUs built on Metal.[^pt-mps]
- **A rented NVIDIA machine** runs the GPU paths, including Triton. Triton's
  README lists NVIDIA and AMD GPUs as supported and CPUs as under
  development,[^triton-readme] so the Triton column is filled only there.

### What counts as success

1. **Within the bound.** Every path's result stays within the error bound set
   in advance, and `torch.library.opcheck` passes.
2. **Every cell filled.** Every table cell has a measured median and
   confidence interval, including the cells where Vortex loses.
3. **Overhead explained.** The bridge's own cost is measured, and the page
   states the size below which it dominates.
4. **Equal conditions.** Every path runs with the same data type, device,
   shapes and thread count. Any exception is written next to the number it
   affects.
5. **Reasons, not only rankings.** For each workload the page says which path
   is fastest and why, citing the kernel evidence from A1 or A8.

## Setup

| Item | Value |
| --- | --- |
| Machines: CPU, GPU, memory | |
| Operating systems | |
| PyTorch version, and the output of `torch.__config__.show()` | |
| Triton version | |
| CUDA and driver versions (NVIDIA machine) | |
| Thread count for each path | |
| Shapes, and the commit that fixed the list | |
| Error bound, and the commit that fixed it | |
| Vortex commit | |
| Date of the run | |

## Results

When the tables are filled, each row gets one or two sentences of
explanation, backed by the kernel evidence or the measurements. A number
without an explanation does not go in.

### Accuracy

| Workload and shape | Device | Eager | torch.compile | Triton | Vortex | Bound |
| --- | --- | --- | --- | --- | --- | --- |
| Square matrix product | | | | | | |
| Tall-and-thin matrix product | | | | | | |
| Batched matrix product | | | | | | |
| Matrix product plus elementwise operation | | | | | | |

- **Eager, torch.compile, Triton, Vortex:** the largest relative error
  against the float64 reference.
- **Bound:** the error bound for this row, as set before the run.

### Speed

| Workload and shape | Device | Eager | torch.compile | torch.compile, max-autotune | Triton | Vortex | Fastest |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Square matrix product | | | | | | | |
| Tall-and-thin matrix product | | | | | | | |
| Batched matrix product | | | | | | | |
| Matrix product plus elementwise operation | | | | | | | |

- **Each path:** median steady-state time with its 95% confidence interval.
- **Fastest:** the fastest path, named only when its confidence interval does
  not overlap the next one's; otherwise "tie".

### Cost of the bridge

| Shape | Vortex kernel called from C++ | Vortex through the custom operator | Difference |
| --- | --- | --- | --- |
| Smallest shape | | | |
| Middle shape | | | |
| Largest shape | | | |

- **First two columns:** median time with its 95% confidence interval.
- **Difference:** the second column minus the first, with its confidence
  interval.

### First calls

| Workload | torch.compile, first call | torch.compile, max-autotune, first call | Triton, first call | Vortex, ahead-of-time compile |
| --- | --- | --- | --- | --- |
| Square matrix product | | | | |
| Matrix product plus elementwise operation | | | | |

- **First call:** wall-clock time of the first call, compilation included.
- **Vortex, ahead-of-time compile:** the time to compile the kernel before the
  program runs, since Vortex does not compile during the call.

## Analysis

Empty until the first measured run. This section will explain, for each
workload, why the fastest path won: which loops fused, which path stayed
memory-bound, and what the kernel evidence from A1 or A8 predicted.

## What did not work

Filled in as the work goes: one row per attempt that failed or was dropped.
Failed attempts are part of the evidence, because they show how the final
design was reached.

| Attempt | What happened | What it taught | Commit |
| --- | --- | --- | --- |
| | | | |

## Threats to validity

- **Different sums.** PyTorch's CPU matrix product calls a BLAS library whose
  order of additions is not Vortex's, so results differ in the last bits. The
  comparison uses a shared float64 reference rather than equality between
  paths.
- **PyTorch's build.** Which BLAS library and which thread settings PyTorch
  uses depend on how it was built and installed. The setup table records the
  build description.
- **Threads.** A path that quietly uses more threads compares thread counts,
  not kernels. `torch.utils.benchmark` uses one thread by
  default,[^pt-benchmark] and the other paths are set to match.
- **Compilation time.** torch.compile compiles on first use.[^torch-compile]
  The steady-state tables exclude that time, and the first-call table shows
  it.
- **Chosen shapes.** Shapes where Vortex happens to do well would mislead. The
  shape list is committed before measuring and includes shapes where Vortex
  is expected to lose.
- **Fusion is part of the result.** If one path fuses the elementwise
  operation into the matrix product and another does not, that difference is
  real. The speed table notes which paths fused.
- **Versions move.** Results hold for the recorded PyTorch, Triton and CUDA
  versions only. The PyTorch documentation cited here is for version 2.14.

## Reproduce

Empty until the first measured run. This section will give one command that,
from a clean checkout, builds the compiled kernels, registers the custom
operator, runs every comparison on both machines, writes the raw data into
the repository, and regenerates the tables on this page.

## What a reviewer should look at

| Evidence | What to check | Where |
| --- | --- | --- |
| One command that runs every comparison and regenerates the tables | It runs on a machine that matches the setup table | |
| The operator registration and its tests | `opcheck` and the unsupported-shape test pass | |
| The export decision record | How arrays and references cross from C++ into Vortex code | |
| The shape list and the error bound | Both were committed before the measurements | |
| Raw data | A CSV file with every timed run, one per machine | |
| Environment records | The `torch.__config__.show()` output and GPU details for each machine | |
| Kernel evidence | Links to the A1 or A8 rows for each Vortex kernel used | |

## Sources

[^pt-custom-ops]: PyTorch, "Custom C++ and CUDA Operators", PyTorch tutorials. <https://docs.pytorch.org/tutorials/advanced/cpp_custom_ops.html>
[^torch-compile]: PyTorch, "torch.compile", PyTorch 2.14 documentation. <https://docs.pytorch.org/docs/2.14/generated/torch.compile.html>
[^triton-paper]: Philippe Tillet, H. T. Kung and David Cox, "Triton: An Intermediate Language and Compiler for Tiled Neural Network Computations", *MAPL 2019*, pages 10 to 19. <https://www.eecs.harvard.edu/~htk/publication/2019-mapl-tillet-kung-cox.pdf>
[^triton-tutorial]: Triton, "Matrix Multiplication", Triton tutorials. <https://triton-lang.org/main/getting-started/tutorials/03-matrix-multiplication.html>
[^pt-benchmark]: PyTorch, "PyTorch Benchmark", PyTorch recipes. <https://docs.pytorch.org/tutorials/recipes/recipes/benchmark.html>
[^brrr]: Horace He, "Making Deep Learning go Brrrr From First Principles". <https://horace.io/brrr_intro.html>
[^pt-config]: PyTorch, "torch.__config__", PyTorch 2.14 documentation. <https://docs.pytorch.org/docs/2.14/config_mod.html>
[^pt-mps]: PyTorch, "MPS backend", PyTorch 2.14 documentation. <https://docs.pytorch.org/docs/2.14/notes/mps.html>
[^triton-readme]: Triton project, repository README. <https://github.com/triton-lang/triton>
