# Example programs

Every code example shown in the docs lives here as a small program. Pages pull
the file in with pymdownx.snippets, and CI compiles (and usually runs) the same
file, so the code a reader sees is exactly the code that was checked.

## Policy

- **Original.** Write each example yourself. Do not paste code from the
  references it follows, from tutorials, or from anywhere else.
- **Minimal and standalone.** One file, one idea, no local headers, standard
  library only (unless a tag says otherwise).
- **Cites what it follows.** When an example follows a reference, such as a
  cppreference page, a paper or an ABI document, list its URL under `follows`
  in the example's `.toml`.
- **Never Vortex compiler code.** `src/` belongs to the compiler's author.
  Examples teach the ideas and never copy, excerpt or preview the compiler
  itself.
- **Deterministic output.** The same program prints the same bytes on every
  run and machine: no timings, pointer addresses, random numbers, dates,
  thread interleavings or locale-dependent text. If a page talks about speed,
  measure it separately and describe the numbers in prose.

## Layout

```text
examples/<book>/<page-slug>/<name>.<ext>        the example
examples/<book>/<page-slug>/<name>.expected     expected stdout (optional)
examples/<book>/<page-slug>/<name>.toml         metadata (optional)
```

`<book>` is one of `build-v0.1`, `backend`, `optimize`, `gpu`, `mlir` or
`lang`, and `<page-slug>` names the page that shows the example. The runner
reports any other file it finds here as an error, so nothing is shown in the
docs without being checked.

## What the runner does with each file type

| Extension | Toolchain | Check |
| --- | --- | --- |
| `.cpp` | `$CXX` (default `c++`) | build with `-std=c++26 -Wall -Wextra -Werror -O2`, run, compare stdout |
| `.s` | `$CC` (default `cc`) | assemble with `cc -c`; set `platforms` to the matching architecture |
| `.ll` | `opt`, `llc` | `opt -S -passes=verify` (or your `flags`), compare stdout; then `llc -O2` |
| `.mlir` | `mlir-opt` | `mlir-opt` with your `flags`, compare stdout |
| `.cu` | `nvcc` | compile only: `nvcc -c -std=c++20 -O2` |
| `.metal` | none yet | always skipped: `needs-metal` |

The file extension chooses the toolchain. An example is skipped, with the
reason printed, when it targets another platform or needs a tool that is not
on `PATH` (`opt`, `llc`, `mlir-opt`, `nvcc`). A missing `$CXX` or `$CC` fails
the run instead, because every machine that checks examples needs both.
Skips never fail a run; failures do.

`<name>.expected` holds the exact stdout, compared byte for byte. It applies
to `.cpp` examples that run, and to `.ll` and `.mlir` examples, whose output is
the transformed IR. The tools run inside the example's folder, so file names
in their output (such as opt's `ModuleID`) do not depend on the machine. If a
page shows an example's output, include the `.expected` file on the page too,
so the output shown is the output checked.

## Metadata

An example needs a `<name>.toml` only when it departs from the defaults or
follows a reference. All keys are optional:

```toml
platforms = ["macos-arm64", "linux-arm64"]   # default: every platform
tags      = ["needs-llvm"]                   # default: none
flags     = ["-O3"]                          # appended to the default flags
run       = false                            # .cpp only; default: true
follows   = ["https://en.cppreference.com/cpp/string/byte/isalpha"]
```

- `platforms`: any of `macos-arm64`, `linux-arm64` and `linux-x86_64`.
- `tags`: any of `needs-llvm` (`opt` and `llc` on `PATH`), `needs-mlir`
  (`mlir-opt`), `needs-cuda` (`nvcc`) and `needs-metal` (not supported yet).
  The file extension adds its own tag: `.ll` needs LLVM, `.mlir` needs MLIR,
  `.cu` needs CUDA and `.metal` needs Metal.
- `flags`: for `.cpp` they are appended to the C++ flags above (later flags
  win, so `-O0` or `-Wno-error=...` work). For `.ll` they replace
  `-passes=verify` in the `opt` step. For `.mlir`, `.s` and `.cu` they are
  passed to the tool.
- `follows`: the URLs of the references the example follows.

Unknown keys, platforms or tags are errors, not silently ignored.

## Showing an example on a page

````markdown
```cpp title="examples/build-v0.1/stage-2-lexer/char_classes.cpp"
--8<-- "examples/build-v0.1/stage-2-lexer/char_classes.cpp"
```

Output:

```text
--8<-- "examples/build-v0.1/stage-2-lexer/char_classes.expected"
```
````

## Running the checks locally

```sh
python3 tools/docs/check_examples.py                 # check every example
python3 tools/docs/check_examples.py --only '*/optimize/*' --jobs 4
python3 tools/docs/check_examples.py --list          # what would run here, without compiling
CXX=g++-14 python3 tools/docs/check_examples.py      # try another compiler
```

The runner needs Python 3.12 or later and nothing outside the standard
library. CI (`.github/workflows/examples.yml`) runs it on macOS arm64, Linux
arm64 and Linux x86-64: Apple clang on macOS, and `g++-14` with Ubuntu's LLVM
18 (`llc`, `opt`, `mlir-opt`) on Linux.

## Compiler Explorer links

`examples/manifest.json` maps each `.cpp`, `.ll`, `.mlir` and `.cu` example to
its Compiler Explorer language, compilers and options, plus a ready-made
`https://godbolt.org/clientstate/...` URL that opens the file with the same
flags CI uses. The file is generated; after adding or editing an example, run:

```sh
python3 tools/docs/check_examples.py --manifest      # rewrite it
python3 tools/docs/check_examples.py --check         # CI: fail if it is stale
```

Compiler ids used: C++ `clang_trunk` (x86-64) and `armv8-clang-trunk`
(AArch64); LLVM IR `opttrunk` and `llctrunk`; MLIR `mliropttrunk`; CUDA
`nvcc133` (Compiler Explorer has no nvcc trunk). They were checked against
`https://godbolt.org/api/compilers/<language>` on 2026-09-23.

## Adding an example

1. Write `examples/<book>/<page-slug>/<name>.<ext>`.
2. If it prints output the page shows, save the output as `<name>.expected`,
   for example with `c++ -std=c++26 -O2 <name>.cpp -o /tmp/ex && /tmp/ex > <name>.expected`.
   Check by hand that the output is right before you commit it: the runner
   proves the output never changes, not that it was correct to begin with.
3. Add `<name>.toml` if it follows a reference or needs non-default settings.
4. Run `python3 tools/docs/check_examples.py --only '*<name>*'`, then
   `--manifest`.
