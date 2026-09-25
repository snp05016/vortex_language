# How to read the assembly your compiler generates

<p class="page-intro">Use this when your own back end is producing something, and the only question left is whether it is producing the right thing: a `print` that comes out in the wrong place, a loop that looks unrolled when it should not be, or a runtime error that never seems to fire. Reading the assembly function by function is the fastest way to find out what your compiler did, as opposed to what you meant it to do.</p>

## Before you start

- [Stage 6: The first machine code](../guide/stage-6-first-machine-code.md) has to be working: your compiler already turns a checked program into an object file or executable, using the back end you chose in [that stage's "Choosing a back end"](../guide/stage-6-first-machine-code.md#choosing-a-back-end) section.
- [Stage 7: Functions and control flow](../guide/stage-7-functions-and-control-flow.md) helps once your programs have more than one function and a branch, since this recipe reads a listing [basic block](../guide/stage-7-functions-and-control-flow.md#basic-blocks-and-control-flow-graphs) by basic block.
- You do not need [stage 8](../guide/stage-8-data-in-memory.md) or later stages; the same procedure applies once arrays and structs arrive, with more addresses to track.
- `clang`, `otool` or `llvm-objdump`, and `lldb` are on your `PATH`. On Linux, use `objdump` where this page says `otool`.

## Steps

1. **Get the assembly text out of your compiler.** Which command does this depends on the back end you chose in stage 6:
    - If your compiler generates LLVM IR, pipe that IR through `llc -O0 -o -` to print AArch64 text to standard output (leave off `-o -` and give a file name to save it).
    - If your compiler generates C, compile the generated C file yourself with `clang -S -O0 -ffp-contract=off -o out.s generated.c`.
    - If your compiler generates assembly directly, it already has the text; add a driver flag (or read the file you already write before assembling) so you can see it without running the linker.

    Whichever path applies, also produce an `-O2` version of the same function, using the matching `-O2` flag on `llc` or `clang`. Reading the two side by side is how [A2's "Reading any listing" procedure](../../backend/a2-aarch64-assembly.md#reading-any-listing) recommends seeing what an optimizer removed, and it is exactly the exercise A2 sets for the stage 10 kernel: "make your build able to show the AArch64 assembly of a compiled Vortex program: from your own emitter, from `llc`, or from your C compiler's `-S`."

    Test this step by running it on the roadmap's first end-to-end program from stage 6 (`let result = 2 + 3 * 4; print(result);`) and confirming you get text with one label for `main`, not a binary file.

2. **Read one function at a time.** Apply [A2's six-step procedure](../../backend/a2-aarch64-assembly.md#reading-any-listing) to the `-O0` listing first:
    1. find the function's label and read up to the next label;
    2. skip the `.cfi` lines on a first pass;
    3. mark every basic block: a branch ends one, and a branch target starts the next, the same blocks [stage 7](../guide/stage-7-functions-and-control-flow.md#basic-blocks-and-control-flow-graphs) defines;
    4. write down what each argument register (`x0` onward, per [A4's argument-register rule](../../backend/a4-calling-conventions.md#where-arguments-go-two-counters-not-one)) holds on entry, and follow each value forward through the listing;
    5. for every flag-setting instruction, find where the flags are read and translate the pair back into a source-level comparison;
    6. only then open the `-O2` listing and note which instructions from `-O0` are gone, merged, or reordered.

    Test this step on a function with at least one branch: annotate the `-O0` listing by hand, one comment per instruction naming the Vortex expression it came from, the way A2's own exercise asks for the stage 10 kernel. If any instruction has no comment, you have not finished reading it.

3. **Compare with what clang emits for the same function, written in C++.** Away from the compiler's source tree, write a small C++ function with the same shape as the Vortex function you are reading: same number and kind of arguments, same arithmetic, no library calls. Compile it for the target you care about, since the same source can pick different instructions on different targets:

    ```text
    clang++ -S -O0 -o scratch.s --target=arm64-apple-macos scratch.cpp
    clang++ -S -O0 -o scratch.s --target=aarch64-linux-gnu scratch.cpp
    ```

    Read clang's listing with the same procedure as step 2. A mismatch is informative either way: it should trace back to a rule in [A4](../../backend/a4-calling-conventions.md) or [A5](../../backend/a5-stack-frames.md) (a callee-saved register, a frame-record convention Apple's arm64 requires and Linux does not), or to an optimization clang's C++ front end performs that your compiler does not yet. An instruction that matches neither explanation is a lead worth chasing further, not a difference to shrug off.

    Test this step by listing every instruction that differs between your function and clang's same-shaped one, and writing one sentence against each: the rule that explains it, or a note that it is unexplained.

4. **Disassemble the object file, and check a second compiler's output.** The assembly text from step 1 is what you asked the assembler for; disassembling the file it produced shows what it wrote, which is the only way to catch an assembler or encoding mistake instead of a compiler one.

    - On macOS, `otool -tV path/to/file.o` disassembles the text section with symbol names attached; `otool -L path/to/binary` lists the libraries it links against; `otool -hv path/to/binary` shows the file's header flags.
    - On Linux, or with LLVM's own tools on macOS, `llvm-objdump -d path/to/file.o` disassembles (`objdump -d` works the same way); add `-r` to print relocations alongside, and use `-t` to print the symbol table.

    To check your reading against an independent compiler for the identical, same-shaped C++ function from step 3, paste it into [Compiler Explorer](https://godbolt.org/), pick an AArch64 target, and set the optimization level to match.

    Test this step by confirming that the disassembly's function labels appear in the same order as your assembly listing's labels, and that the instruction count for the function you annotated in step 2 matches.

5. **Step through the running program in lldb.** Static reading tells you what the compiler wrote; stepping tells you whether the processor does what you predicted.

    ```text
    lldb path/to/binary
    (lldb) breakpoint set -n <function-name>
    (lldb) run
    (lldb) disassemble -f
    (lldb) stepi
    (lldb) register read
    (lldb) bt
    ```

    `breakpoint set -n <function-name>` stops at the start of a function by name; `breakpoint set --file <name> --line <N>` stops at a specific source line instead, when debug information is present. Once stopped, `disassemble -f` shows the current frame's function; `stepi` executes one machine instruction at a time, while `next` steps statement by statement; `register read` shows the registers you named in step 2; `bt` shows the call stack.

    Test this step by breaking on the function you annotated in step 2, running to it, and stepping one instruction at a time with `stepi`, checking after each step that `register read` shows the value your step-2 annotation predicted. A register that does not match the prediction means the annotation was wrong, not the processor.

Keep the annotated `-O0` listing from step 2 as a plain text file next to your notes (outside the compiler's own test data). Committing it lets a later `git diff` show exactly which instructions changed the next time you touch the back end.

## Check that it worked

- You can produce a fresh `-O0` and `-O2` assembly listing for any Vortex function on demand, using the command from step 1.
- You have one fully annotated `-O0` listing, one comment per instruction, for a function with at least one branch, and no instruction is left unexplained.
- `otool`'s or `llvm-objdump`'s disassembly of the assembled object matches that listing's instructions, in the same order.
- Stepping through that function's breakpoint in lldb with `stepi` changes the registers in the order your annotation predicted.

## Related

- [A2. Reading and writing AArch64 assembly](../../backend/a2-aarch64-assembly.md)
- [A4. Calling conventions and ABIs](../../backend/a4-calling-conventions.md)
- [A5. Stack frames](../../backend/a5-stack-frames.md)
- [D1. Debug information](../../backend/d1-debug-info.md)
- [Stage 6: The first machine code](../guide/stage-6-first-machine-code.md)
- [Stage 7: Functions and control flow](../guide/stage-7-functions-and-control-flow.md)
- [Compiler architecture](../architecture.md)
