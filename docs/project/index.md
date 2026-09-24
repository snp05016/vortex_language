# Vortex for reviewers

<p class="page-intro">For anyone assessing the project: what Vortex is, what exists in its repository today, what is being built next, and where the evidence for each claim lives. The first three sections take about two minutes to read.</p>

## What Vortex is

Vortex is a small, **statically typed** programming language for numerical
code such as matrix multiplication: the compiler knows the type of every value
and checks it before the program runs. The project has three parts: a language
[specification](../specification/index.md), a compiler written in C++26 and
built with CMake, and this documentation site. Every commit so far has one
author, Saumya Patel.[^history] Large language models were used heavily in
writing the documentation, and design decisions are made through human review;
the [authorship statement](../authorship.md) explains the process.

The first release, v0.1, is about correctness: it must compile the documented
Vortex programs into correct programs for the CPU and reject invalid programs
with useful error messages. Its final demonstration is a plain, unoptimized matrix
multiplication over fixed-size arrays of `f32` (32-bit floating-point numbers)
([goal](../roadmap.md#goal), [definition of done](../roadmap.md#definition-of-done)).
Optimization and GPU code generation come [after v0.1](../roadmap.md#after-v01),
and so does the planned work with **MLIR**, a framework from the LLVM compiler
project for building compilers.

## What exists today

The specification defines the v0.1 language, with 56 open or conflicting
questions settled by the [language decisions](../decisions/index.md). The
compiler implements only the start of it. Its repository holds:

- a **lexer**, which splits source text into **tokens** (the words and symbols
  of the language), with its tests;
- the definitions of the **syntax tree**, the program's structure as nested
  nodes;
- the first lines of a **parser**, the part that will build that tree from the
  tokens;
- a command-line entry point and a CMake build.

No part generates machine code yet. No **continuous integration (CI)** job
(the checks GitHub runs on each change) builds the compiler or runs its
tests.[^workflows] Each row below is one part and its **milestone**, a step of
the [roadmap](../roadmap.md) with a stated completion test. The statuses are
defined under [How claims are backed](#how-claims-are-backed).

| Part | Milestone | Status | Evidence |
| --- | --- | --- | --- |
| Build and test setup | [0](../roadmap.md#milestone-0-project-foundation) | In progress | [`CMakeLists.txt`](https://github.com/snp05016/vortex_language/blob/main/CMakeLists.txt), [`tests/`](https://github.com/snp05016/vortex_language/tree/main/tests); first commit [9d63dc6](https://github.com/snp05016/vortex_language/commit/9d63dc63114e06314555c22ba6f3171d0a142bb1) |
| Source files and error messages | [1](../roadmap.md#milestone-1-source-files-and-diagnostics) | In progress | [`main.cpp`](https://github.com/snp05016/vortex_language/blob/main/src/main.cpp) reads a source file; no error messages with line and column yet; first commit [00b5dd3](https://github.com/snp05016/vortex_language/commit/00b5dd30682415b8df528c612f68d84b5c2c3f74) |
| Lexer | [2](../roadmap.md#milestone-2-lexer) | In progress | [`lexer.cpp`](https://github.com/snp05016/vortex_language/blob/main/src/frontend/lexer.cpp), [`lexer_tests.cpp`](https://github.com/snp05016/vortex_language/blob/main/tests/lexer_tests.cpp); first commit [bd6d912](https://github.com/snp05016/vortex_language/commit/bd6d912b25b6804f5992a57fd02ce2f47c9d0dd0) |
| Parser and syntax tree | [3](../roadmap.md#milestone-3-parser-and-syntax-tree) | In progress | [`ast.h`](https://github.com/snp05016/vortex_language/blob/main/src/frontend/ast.h) defines the tree; [`parser.cpp`](https://github.com/snp05016/vortex_language/blob/main/src/frontend/parser.cpp) does not build it yet and has no tests; first commit [e3a206f](https://github.com/snp05016/vortex_language/commit/e3a206f83c2316a29485b1f5a2bf3f65ea935c1c) |
| Names and scopes | [4](../roadmap.md#milestone-4-names-and-scopes) | Not started | None yet |
| Types, mutability and control-flow checks | [5](../roadmap.md#milestone-5-types-mutability-and-control-flow-checks) | Not started | None yet |
| Machine code, functions, control flow and data in memory | [6](../roadmap.md#milestone-6-basic-cpu-code-generation) to [8](../roadmap.md#milestone-8-strings-arrays-structs-and-references) | Not started | None yet |
| Runtime safety checks | [9](../roadmap.md#milestone-9-runtime-safety) | Not started | None yet |
| Naive matrix multiplication | [10](../roadmap.md#milestone-10-matrix-multiplication) | Not started | None yet |
| Release checks for v0.1 | [11](../roadmap.md#milestone-11-v01-release-gate) | Not started | None yet |
| Compiler tests in CI | Not a milestone | Not started | The [workflows](https://github.com/snp05016/vortex_language/tree/main/.github/workflows) check the docs only |

The most recent commits that change the compiler, on 22 September 2026, edit
the syntax-tree definitions.[^c-074a41d][^c-cb8f5cd]

## What is being built next

1. **The compiler, one milestone at a time,** in the order of the table
   above: first finishing milestones 0 to 3, then 4 to 11. Each milestone has
   a chapter in the [compiler guide](../compiler/guide/index.md).
2. **Four books.** [Optimize](../optimize/index.md),
   [Back end](../backend/index.md), [GPU](../gpu/index.md) and
   [MLIR](../mlir/index.md) teach the ideas behind the work planned after
   v0.1. They are outlines for now, and each chapter is published when it is
   finished. By the project's rule, their code examples are small standalone
   programs, never Vortex compiler code.
3. **Ten case studies.** Each will report one piece of the work after v0.1,
   with its commits, CI runs and results. They are listed below.

## Case studies

A **case study** reports one piece of work on Vortex: the question, the method,
the commits, the CI runs and the results. All ten are plans for now: none has
started, and none has results. The [case studies page](case-studies/index.md)
explains the format and tracks each one's status. "Background" names where
the ideas behind each study are taught.

| Case study | In short | Background |
| --- | --- | --- |
| A1. [CPU matmul ladder generated by the compiler](case-studies/cpu-matmul-ladder.md) | Faster and faster matrix multiplication (matmul), one optimization per step of the ladder, with results kept exact | [P16, the ladder measured](../optimize/p16-capstone.md) |
| A2. [Measurement methodology and performance CI](case-studies/benchmark-harness.md) | A trustworthy way to time Vortex code, and CI that catches a planted slowdown | [P1, measure first](../optimize/p1-measure-first.md) |
| A3. [Differential testing and fuzzing](case-studies/differential-testing.md) | Run random programs through each way Vortex can execute them, and compare the results | [O12, testing an optimizer](../optimize/o12-testing-optimizers.md) |
| A4. [LLVM IR back end](case-studies/llvm-backend.md) | Generate code through LLVM IR, the input language of the LLVM compiler toolkit | [Stage 6](../compiler/guide/stage-6-first-machine-code.md), [Back end](../backend/index.md) |
| A5. [Upstream LLVM and MLIR contributions](case-studies/upstream-contributions.md) | Get reviewed changes into the LLVM and MLIR projects themselves | None |
| A6. [Native AArch64 back end](case-studies/native-aarch64-backend.md) | Generate 64-bit Arm (AArch64) machine code without LLVM, and compare it with the LLVM path | [B1, the simplest back end](../backend/b1-simplest-backend.md) |
| A7. [MLIR lowering path](case-studies/mlir-lowering-path.md) | Compile Vortex step by step through MLIR, and compare with A1's own optimizations | [M12, Vortex's GPU path](../mlir/m12-vortex-gpu-path.md) |
| A8. [GPU matmul ladder](case-studies/gpu-matmul-ladder.md) | The A1 ladder on a GPU, compared with the GPU maker's own library | [G10, the GPU matmul ladder](../gpu/g10-matmul-ladder.md) |
| A9. [Cost model and autotuner](case-studies/cost-model-autotuner.md) | Predict the best block sizes with a model of the machine, and compare with trying sizes out (autotuning) | [P15, choosing parameters](../optimize/p15-choosing-parameters.md) |
| A10. [ML framework bridge](case-studies/ml-framework-bridge.md) | Call Vortex code from PyTorch, a machine-learning (ML) framework, and compare with PyTorch's own | None |

## How claims are backed

Today the evidence is the repository itself: the source, the tests and the
commit history.[^history] The statuses in the tables above mean:

| Status | Meaning |
| --- | --- |
| Not started | The repository holds no code or tests for the part. |
| In progress | Code or tests for the part are in the repository, and the table links them and the first commit. |
| Done | The milestone's completion condition and the roadmap's [evidence list](../roadmap.md#evidence-required-before-calling-a-milestone-complete) are met, and a passing CI run is linked. No part can be Done until a CI job builds the compiler, even where code and tests exist. |

As each piece of work is merged, four rules apply:

- Every measured result about Vortex, such as a speed or a count of bugs
  found, links to the case study that produced it.
- Every case study links to the commits that did the work and the CI runs that
  checked it.
- Every measured number states the machine, the software versions, the Vortex
  commit, the input size, the date and the spread over repeated runs, as
  [How Vortex performance is measured](measuring.md) sets out. A number quoted
  from elsewhere cites its source.
- A status on this site changes only together with a link to its evidence: the
  first commit for In progress, the passing CI run for Done.

Testing is described in [How Vortex is tested](testing.md). Changes to this
site are listed in [What is new in the docs](changelog.md).

## Where to look next

- [Roadmap](../roadmap.md): every milestone and its completion condition.
- [Compiler architecture](../compiler/architecture.md): the planned stages of
  the compiler and the
  [current implementation boundary](../compiler/architecture.md#current-implementation-boundary).
- [Language decisions](../decisions/index.md): the reasons behind the
  language rules.
- The [source](https://github.com/snp05016/vortex_language/tree/main/src) and
  the [commit history](https://github.com/snp05016/vortex_language/commits/main)
  on GitHub.

## Sources

[^history]: Vortex repository, commit history of the `main` branch, GitHub. <https://github.com/snp05016/vortex_language/commits/main>
[^c-074a41d]: Vortex repository, commit 074a41d, 22 September 2026. It changes only `src/frontend/ast.h`. <https://github.com/snp05016/vortex_language/commit/074a41d42ce334ea78d0ef3d67e910c74d514e4f>
[^c-cb8f5cd]: Vortex repository, commit cb8f5cd, 22 September 2026. It changes `src/frontend/ast.h` and `src/frontend/token.h`; the same commit also created the first version of this website. <https://github.com/snp05016/vortex_language/commit/cb8f5cddfe6411b24420a3e716807b9a58d0e8e9>
[^workflows]: Vortex repository, the `.github/workflows` folder of the `main` branch, GitHub. <https://github.com/snp05016/vortex_language/tree/main/.github/workflows>
