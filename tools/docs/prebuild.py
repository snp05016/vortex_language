"""Generate build-time content before `zensical build`.

Zensical has no plugin hooks, so every generator runs here first:

    python tools/docs/prebuild.py           # regenerate what is stale
    python tools/docs/prebuild.py --check   # CI: exit 1 if anything is stale
"""

import importlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

# (module in tools/docs, arguments to regenerate, arguments to check).
# Each module exposes main(argv) -> exit status. Order matters: the example
# includes read the manifest that check_examples writes.
GENERATORS = [
    ("railroad", [], ["--check"]),
    ("check_examples", ["--manifest"], ["--check"]),
    ("example_links", [], ["--check"]),
    ("concepts", [], ["--check"]),
]


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    check = "--check" in argv
    status = 0
    for name, regenerate, verify in GENERATORS:
        print(f"prebuild: {name}", file=sys.stderr)
        module = importlib.import_module(name)
        status = max(status, module.main(verify if check else regenerate) or 0)
    return status


if __name__ == "__main__":
    sys.exit(main())
