#!/usr/bin/env python3
"""Compile, run and check every example program under examples/.

Every code example in the docs is a standalone file in examples/, included into
pages with pymdownx.snippets, so the code on a page is exactly the code this
script checks. examples/README.md describes the layout and the metadata.

    python3 tools/docs/check_examples.py                 # check every example
    python3 tools/docs/check_examples.py --only '*lexer*' --jobs 4
    python3 tools/docs/check_examples.py --list          # what would run here
    python3 tools/docs/check_examples.py --manifest      # rewrite examples/manifest.json
    python3 tools/docs/check_examples.py --check         # exit 1 if the manifest is stale
    python3 tools/docs/check_examples.py --self-test

A docs pre-build step should call main(["--manifest"]); plain main() compiles
and runs everything.

CXX (default c++) builds .cpp files and CC (default cc) assembles .s files;
opt, llc, mlir-opt and nvcc are looked up on PATH. An example that needs a
missing tool, or another platform, is skipped with the reason. The exit status
is 1 when any example fails; skips never fail a run.
"""

import argparse
import base64
import difflib
import fnmatch
import json
import os
import platform
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
import tomllib
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXAMPLES = ROOT / "examples"
MANIFEST = EXAMPLES / "manifest.json"

BOOKS = ("build-v0.1", "backend", "optimize", "gpu", "mlir", "lang")
PLATFORMS = ("macos-arm64", "linux-arm64", "linux-x86_64")
# The tools each tag needs. None marks a tag this runner cannot serve yet.
TAGS = {
    "needs-llvm": ("opt", "llc"),
    "needs-mlir": ("mlir-opt",),
    "needs-cuda": ("nvcc",),
    "needs-metal": None,
}
# File extension -> (toolchain shown by --list, tag the extension implies).
KINDS = {
    ".cpp": ("c++", None),
    ".s": ("cc -c", None),
    ".ll": ("opt + llc", "needs-llvm"),
    ".mlir": ("mlir-opt", "needs-mlir"),
    ".cu": ("nvcc -c", "needs-cuda"),
    ".metal": ("metal", "needs-metal"),
}
SIDECARS = (".toml", ".expected")

CXX_FLAGS = ("-std=c++26", "-Wall", "-Wextra", "-Werror", "-O2")  # as the compiler's CMake
NVCC_FLAGS = ("-std=c++20", "-O2")  # nvcc has no C++26 mode
OPT_FLAGS = ("-passes=verify",)  # for .ll examples that set no flags
TIMEOUT = 120  # seconds per command

# Compiler Explorer language and compiler ids, checked against
# https://godbolt.org/api/compilers/<language> on 2026-09-23. Compiler Explorer
# has no nvcc trunk, so CUDA is pinned to the newest nvcc listed then (13.3).
CE_URL = "https://godbolt.org/clientstate/"


@dataclass(frozen=True)
class Meta:
    """An example's optional <name>.toml. Missing keys take these defaults."""

    platforms: tuple[str, ...] = ()  # empty means every platform
    tags: tuple[str, ...] = ()
    flags: tuple[str, ...] = ()  # appended to the toolchain's default flags
    run: bool = True  # .cpp only: run the program after building it
    follows: tuple[str, ...] = ()  # URLs of the references the example follows


@dataclass(frozen=True)
class Example:
    path: Path  # the source file
    rel: str  # the path people see: examples/<book>/<page-slug>/<file>
    meta: Meta
    expected: Path | None  # <name>.expected, when the example has one

    @property
    def tags(self) -> tuple[str, ...]:
        implied = KINDS[self.path.suffix][1]
        return self.meta.tags + ((implied,) if implied and implied not in self.meta.tags else ())


@dataclass(frozen=True)
class Result:
    example: Example
    status: str  # "pass", "fail" or "skip"
    reason: str = ""  # one line for the summary table
    log: str = ""  # the full story of a failure
    seconds: float = 0.0


def parse_meta(text: str) -> Meta:
    """Parse and validate a sidecar .toml. Raises ValueError with a readable message."""
    data = tomllib.loads(text)  # TOMLDecodeError is a ValueError
    fields = Meta.__dataclass_fields__
    for key, value in data.items():
        if key not in fields:
            raise ValueError(f"unknown key {key!r}; allowed keys: {', '.join(fields)}")
        if key == "run":
            if not isinstance(value, bool):
                raise ValueError("run must be true or false")
        elif not (isinstance(value, list) and all(isinstance(v, str) for v in value)):
            raise ValueError(f"{key} must be a list of strings")
    meta = Meta(**{k: v if k == "run" else tuple(v) for k, v in data.items()})
    for word, values, allowed in (("platform", meta.platforms, PLATFORMS), ("tag", meta.tags, TAGS)):
        for value in values:
            if value not in allowed:
                raise ValueError(f"unknown {word} {value!r}; use one of: {', '.join(allowed)}")
    for url in meta.follows:
        if not url.startswith(("https://", "http://")):
            raise ValueError(f"follows takes URLs, got {url!r}")
    return meta


def discover(root: Path = EXAMPLES) -> tuple[list[Example], list[str]]:
    """Find the examples under root, sorted by path, plus every problem found:
    unknown or misplaced files, bad metadata, sidecars with no example."""
    examples: list[Example] = []
    errors: list[str] = []
    names: set[Path] = set()  # example paths without their extension

    def shown(path: Path) -> str:
        return path.relative_to(root.parent).as_posix()

    files = sorted(
        p for p in root.rglob("*")
        if p.is_file() and not any(part.startswith(".") for part in p.relative_to(root).parts)
    )
    for path in files:
        if path.parent == root and path.name in ("README.md", "manifest.json"):
            continue
        if path.suffix in SIDECARS:
            continue  # checked below, once every example is known
        if path.suffix not in KINDS:
            errors.append(f"{shown(path)}: unknown file type; examples end in {', '.join(KINDS)}")
            continue
        parts = path.relative_to(root).parts
        if len(parts) != 3 or parts[0] not in BOOKS:
            errors.append(f"{shown(path)}: examples live at examples/<book>/<page-slug>/<file>, "
                          f"where <book> is one of {', '.join(BOOKS)}")
            continue
        name = path.with_suffix("")
        if name in names:
            errors.append(f"{shown(path)}: another example here has the same name, "
                          "so the .toml and .expected files would be ambiguous")
            continue
        names.add(name)
        toml, expected = path.with_suffix(".toml"), path.with_suffix(".expected")
        try:
            meta = parse_meta(toml.read_text(encoding="utf-8")) if toml.exists() else Meta()
        except ValueError as error:
            errors.append(f"{shown(toml)}: {error}")
            continue
        has_output = path.suffix in (".ll", ".mlir") or (path.suffix == ".cpp" and meta.run)
        if expected.exists() and not has_output:
            errors.append(f"{shown(expected)}: {shown(path)} produces no output to compare")
            continue
        examples.append(Example(path, shown(path), meta, expected if expected.exists() else None))

    for path in files:
        if path.suffix in SIDECARS and path.with_suffix("") not in names:
            errors.append(f"{shown(path)}: no example named {path.stem!r} next to it")
    return examples, errors


def current_platform() -> str:
    system = {"darwin": "macos"}.get(sys.platform, sys.platform)
    machine = platform.machine().lower()
    arch = {"aarch64": "arm64", "amd64": "x86_64"}.get(machine, machine)
    return f"{system}-{arch}"


def tool(variable: str, default: str) -> list[str]:
    """The command in an environment variable such as CXX, split like a shell would."""
    return shlex.split(os.environ.get(variable) or default)


def skip_reason(example: Example, here: str) -> str | None:
    if example.meta.platforms and here not in example.meta.platforms:
        return f"platform: runs on {', '.join(example.meta.platforms)}"
    for tag in example.tags:
        needed = TAGS[tag]
        if needed is None:
            return f"{tag}: not supported by this runner yet"
        missing = [name for name in needed if shutil.which(name) is None]
        if missing:
            return f"{tag}: {', '.join(missing)} not found on PATH"
    return None


def steps(example: Example, out: Path) -> list[tuple[list[str], bool]]:
    """The commands that check an example, in order. True marks the command
    whose stdout is the example's output, compared with its .expected file.
    Commands run inside the example's folder, so the source is passed by bare
    name: tools such as opt print the name they were given."""
    name, flags = example.path.name, list(example.meta.flags)
    match example.path.suffix:
        case ".cpp":
            exe = str(out / example.path.stem)
            build = [*tool("CXX", "c++"), *CXX_FLAGS, *flags, name, "-o", exe]
            return [(build, False)] + ([([exe], True)] if example.meta.run else [])
        case ".s":
            return [([*tool("CC", "cc"), "-c", *flags, name, "-o", str(out / "out.o")], False)]
        case ".ll":
            return [(["opt", "-S", *(flags or OPT_FLAGS), name], True),
                    (["llc", "-O2", name, "-o", str(out / "out.s")], False)]
        case ".mlir":
            return [(["mlir-opt", *flags, name], True)]
        case ".cu":
            return [(["nvcc", "-c", *NVCC_FLAGS, *flags, name, "-o", str(out / "out.o")], False)]
    raise AssertionError(f"no steps for {example.rel}")  # .metal is always skipped


def check(example: Example, here: str) -> Result:
    if reason := skip_reason(example, here):
        return Result(example, "skip", reason)
    start = time.monotonic()
    output = ""
    with tempfile.TemporaryDirectory(prefix="example-") as out:
        for argv, is_output in steps(example, Path(out)):
            shown = f"$ {shlex.join(argv)}\n"
            if shutil.which(argv[0]) is None:
                return Result(example, "fail", f"{argv[0]} not found",
                              f"{shown}{argv[0]} not found; install it or set CXX / CC")
            try:
                proc = subprocess.run(argv, cwd=example.path.parent, stdin=subprocess.DEVNULL,
                                      capture_output=True, encoding="utf-8", errors="replace",
                                      timeout=TIMEOUT)
            except subprocess.TimeoutExpired:
                return Result(example, "fail", f"timed out after {TIMEOUT}s", f"{shown}timed out")
            if proc.returncode != 0:
                return Result(example, "fail", f"{Path(argv[0]).name} exited with {proc.returncode}",
                              shown + proc.stdout + proc.stderr, time.monotonic() - start)
            if is_output:
                output = proc.stdout
    seconds = time.monotonic() - start
    if example.expected is not None:
        expected = example.expected.read_text(encoding="utf-8")
        if output != expected:
            diff = difflib.unified_diff(expected.splitlines(keepends=True),
                                        output.splitlines(keepends=True),
                                        example.expected.name, "actual output")
            return Result(example, "fail", "output differs from .expected", "".join(diff), seconds)
    return Result(example, "pass", seconds=seconds)


def compiler_explorer(example: Example) -> tuple[str, list[dict[str, str]]] | None:
    """(language, compilers) for an example's Compiler Explorer link, with the
    flags the local check uses; None for file types Compiler Explorer does not get."""
    flags = list(example.meta.flags)
    match example.path.suffix:
        case ".cpp":
            options = shlex.join([*CXX_FLAGS, *flags])
            return "c++", [{"id": "clang_trunk", "options": options},
                           {"id": "armv8-clang-trunk", "options": options}]
        case ".ll":
            return "llvm", [{"id": "opttrunk", "options": shlex.join(flags or OPT_FLAGS)},
                            {"id": "llctrunk", "options": "-O2"}]
        case ".mlir":
            return "mlir", [{"id": "mliropttrunk", "options": shlex.join(flags)}]
        case ".cu":
            return "cuda", [{"id": "nvcc133", "options": shlex.join([*NVCC_FLAGS, *flags])}]
    return None


def manifest_text(examples: list[Example]) -> str:
    """examples/manifest.json: example path -> Compiler Explorer language,
    compilers and a ready clientstate URL. Same examples, same bytes."""
    entries = {}
    for example in examples:
        if (ce := compiler_explorer(example)) is None:
            continue
        language, compilers = ce
        state = {"sessions": [{"id": 1, "language": language, "compilers": compilers,
                               "source": example.path.read_text(encoding="utf-8")}]}
        # Compiler Explorer's API docs ask for non-ASCII characters as \u
        # escapes, which ensure_ascii produces. URL-safe base64 keeps '/' out of
        # the URL path; Compiler Explorer decodes both alphabets.
        payload = json.dumps(state, separators=(",", ":"), ensure_ascii=True).encode()
        entries[example.rel] = {"language": language, "compilers": compilers,
                                "url": CE_URL + base64.urlsafe_b64encode(payload).decode()}
    return json.dumps(entries, indent=2, sort_keys=True) + "\n"


def sync_manifest(examples: list[Example], check_only: bool) -> int:
    text = manifest_text(examples)
    shown = MANIFEST.relative_to(ROOT).as_posix()
    if MANIFEST.exists() and MANIFEST.read_text(encoding="utf-8") == text:
        print(f"{shown} is up to date")
        return 0
    if check_only:
        print(f"{shown} is out of date; run: python3 tools/docs/check_examples.py --manifest",
              file=sys.stderr)
        return 1
    MANIFEST.write_text(text, encoding="utf-8")
    print(f"wrote {shown}")
    return 0


def toolchain_line(here: str) -> str:
    cxx = tool("CXX", "c++")
    version = "not found"
    if shutil.which(cxx[0]):
        lines = subprocess.run([*cxx, "--version"], capture_output=True, text=True).stdout.splitlines()
        version = lines[0] if lines else "unknown version"
    found = [name for name in ("opt", "llc", "mlir-opt", "nvcc") if shutil.which(name)]
    missing = [name for name in ("opt", "llc", "mlir-opt", "nvcc") if name not in found]
    return (f"platform {here}; {shlex.join(cxx)}: {version}; "
            f"found: {' '.join(found) or 'none'}; missing: {' '.join(missing) or 'none'}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--list", action="store_true",
                        help="list the examples and whether each would run here, without compiling")
    parser.add_argument("--only", action="append", metavar="GLOB",
                        help="check only paths matching GLOB, e.g. 'examples/optimize/*' (repeatable)")
    parser.add_argument("--jobs", "-j", type=int, default=os.cpu_count() or 1, metavar="N",
                        help="check N examples at a time (default: one per CPU)")
    parser.add_argument("--manifest", action="store_true",
                        help="rewrite examples/manifest.json and exit, without compiling")
    parser.add_argument("--check", action="store_true",
                        help="exit 1 if examples/manifest.json is out of date, without compiling")
    parser.add_argument("--self-test", action="store_true", help="test this script's own parsing")
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error("--jobs must be at least 1")
    if args.self_test:
        return self_test()

    examples, errors = discover()
    if errors:
        print("examples/ has problems:", *errors, sep="\n  ", file=sys.stderr)
        return 1
    if args.manifest or args.check:
        return sync_manifest(examples, check_only=args.check)  # always covers every example
    if args.only:
        examples = [e for e in examples if any(fnmatch.fnmatch(e.rel, g) for g in args.only)]
        if not examples:
            print(f"no example matches {' or '.join(args.only)}", file=sys.stderr)
            return 1
    if not examples:
        print("no examples found")
        return 0

    here = current_platform()
    width = max(len(e.rel) for e in examples)
    if args.list:
        for e in examples:
            status = skip_reason(e, here)
            print(f"{e.rel:<{width}}  {KINDS[e.path.suffix][0]:<9}  {'skip: ' + status if status else 'check'}")
        return 0

    print(toolchain_line(here))
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(lambda e: check(e, here), examples))  # keeps path order
    for r in results:
        if r.status == "fail":
            print(f"\n--- FAIL {r.example.rel}\n{r.log.rstrip()}")
    print()
    for r in results:
        seconds = f"{r.seconds:5.2f}s" if r.status != "skip" else "     -"
        print(f"{r.status.upper():4}  {seconds}  {r.example.rel:<{width}}  {r.reason}".rstrip())
    counts = {s: sum(r.status == s for r in results) for s in ("pass", "fail", "skip")}
    print(f"\n{counts['pass']} passed, {counts['fail']} failed, {counts['skip']} skipped")
    return 1 if counts["fail"] else 0


def self_test() -> int:
    assert parse_meta("") == Meta()
    meta = parse_meta('platforms = ["linux-arm64"]\nflags = ["-O3"]\nrun = false\n'
                      'follows = ["https://example.com/"]')
    assert (meta.platforms, meta.flags, meta.run) == (("linux-arm64",), ("-O3",), False)
    for bad in ('colour = "red"', 'platforms = ["windows"]', 'tags = ["needs-gpu"]',
                'run = "no"', 'flags = "-O3"', 'follows = ["see the book"]', 'run = '):
        try:
            parse_meta(bad)
        except ValueError:
            continue
        raise AssertionError(f"accepted bad metadata: {bad!r}")

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp) / "examples"
        page = root / "lang" / "some-page"
        page.mkdir(parents=True)
        (page / "a.cpp").write_text("int main() { return 0; } // é\n", encoding="utf-8")
        (page / "a.expected").write_text("")
        (page / "b.s").write_text("")
        (page / "b.expected").write_text("")  # .s has no output to compare
        (page / "c.expected").write_text("")  # no example c
        (page / "d.cc").write_text("")  # unknown file type
        (page / ".DS_Store").write_text("")  # hidden files are ignored
        (root / "no-such-book" / "p").mkdir(parents=True)
        (root / "no-such-book" / "p" / "e.cpp").write_text("")
        examples, errors = discover(root)
        assert [e.rel for e in examples] == ["examples/lang/some-page/a.cpp"], examples
        assert len(errors) == 4, errors

        text = manifest_text(examples)
        assert text == manifest_text(examples)  # deterministic
        entry = json.loads(text)["examples/lang/some-page/a.cpp"]
        blob = entry["url"].removeprefix(CE_URL)
        assert blob.isascii() and "/" not in blob and "+" not in blob
        state = json.loads(base64.urlsafe_b64decode(blob))
        assert state["sessions"][0]["source"] == "int main() { return 0; } // é\n"
        assert entry["compilers"][0]["options"] == "-std=c++26 -Wall -Wextra -Werror -O2"

    assert current_platform().count("-") == 1
    print("self-test passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
