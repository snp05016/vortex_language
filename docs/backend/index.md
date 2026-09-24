<!-- generated overview -->
# Back end

<p class="page-intro">How a compiler turns its IR into machine code by itself: the machine, a first native back end, the classical pipeline, the tools around the code, and how LLVM does the same jobs.</p>


## A. The machine

| Chapter | What it covers | Status |
| --- | --- | --- |
| [A1. The machine model](a1-machine-model.md) | ISA, registers, memory, addressing modes and instruction encoding, with one hand-encoded instruction per ISA. | Published |
| [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md) | Syntax, directives, labels, loads and stores, branches and flags, with Apple's spellings. | Published |
| [A3. Floats and vectors in registers](a3-floats-and-vectors.md) | Floating-point and SIMD registers, scalar FP instructions, FMA and contraction, and a first look at lanes. | Being written |
| [A4. Calling conventions and ABIs](a4-calling-conventions.md) | AAPCS64, Apple's arm64 differences and the System V AMD64 ABI. | Published |
| [A5. Stack frames](a5-stack-frames.md) | Prologues and epilogues, frame records, alignment, red zones and callee-saved registers. | Published |

## B. A first native back end

| Chapter | What it covers | Status |
| --- | --- | --- |
| [B1. The simplest back end that works](b1-simplest-backend.md) | Macro-expansion instruction selection, every value on the stack, assembly text out, and differential testing. | Published |
| [B2. A second target: x86-64](b2-x86-64.md) | Retargeting the first back end, and finding what is machine-specific. | Published |
| [B3. Object files and assemblers](b3-object-files.md) | Sections, symbols and relocations in ELF and Mach-O, and encoding instructions yourself. | Published |
| [B4. Linking and loading](b4-linking-and-loading.md) | Static and dynamic linking, position-independent code, GOT and PLT, and loaders. | Published |

## C. The classical pipeline

| Chapter | What it covers | Status |
| --- | --- | --- |
| [C1. Instruction selection](c1-instruction-selection.md) | Tree tiling, maximal munch, optimal tiling and DAG selection. | Published |
| [C2. Liveness](c2-liveness.md) | Backward dataflow, live intervals and the shortcuts SSA allows. | Published |
| [C3. Register allocation I: linear scan](c3-linear-scan.md) | Poletto and Sarkar's linear scan, interval splitting, and an allocation checker. | Published |
| [C4. Register allocation II: graphs and SSA](c4-graph-coloring.md) | Chaitin and Briggs coloring, coalescing, SSA-based allocation and parallel moves. | Published |
| [C5. Spilling, splitting and rematerialization](c5-spilling.md) | Choosing what to spill, where spill code goes, and recomputing instead of reloading. | Published |
| [C6. Instruction scheduling](c6-scheduling.md) | Latency, throughput and list scheduling, and why the naive matmul loop is latency-bound. | Being written |
| [C7. Peephole optimization](c7-peephole.md) | Small window rewrites, load and store pairs, and superoptimization. | Published |

## D. Around the code

| Chapter | What it covers | Status |
| --- | --- | --- |
| [D1. Debug information](d1-debug-info.md) | DWARF line tables and DIEs, call frame information and unwinding. | Published |
| [D2. JIT compilation](d2-jit.md) | Executable memory, W^X, Apple's MAP_JIT and calling code generated at run time. | Published |
| [D3. Reading real back ends](d3-real-backends.md) | What QBE, Cranelift, TCC, Go and chibicc chose, and why. | Published |

## E. How LLVM does it

| Chapter | What it covers | Status |
| --- | --- | --- |
| [E1. The LLVM code generator pipeline](e1-llvm-codegen-pipeline.md) | SelectionDAG, GlobalISel and FastISel, MachineInstr and MIR, and the pass list. | Published |
| [E2. Describing a target](e2-describing-a-target.md) | TableGen descriptions of registers, instructions, calling conventions and scheduling models. | Published |
| [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md) | The greedy allocator, the machine scheduler, llvm-mca and the MC layer. | Published |
| [E4. Testing back ends](e4-testing-backends.md) | FileCheck on assembly, MIR tests, llvm-mc encoding tests and differential testing. | Being written |
