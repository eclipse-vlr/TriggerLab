#!/usr/bin/env python3
"""Check or apply the repository's C++ style with clang-format 18."""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--fix", action="store_true", help="rewrite files instead of checking")
parser.add_argument("--formatter", default="clang-format-18")
args = parser.parse_args()
version = subprocess.check_output([args.formatter, "--version"], text=True)
if not re.search(r"clang-format version 18\.", version):
    parser.error("clang-format 18 is required")
root = Path(__file__).resolve().parent.parent
sources = sorted(
    path for folder in ("include", "src", "tests")
    for path in (root / folder).rglob("*")
    if path.suffix in {".cpp", ".hpp", ".inl"}
)
command = [args.formatter, "-i"] if args.fix else [args.formatter, "--dry-run", "--Werror"]
subprocess.run(command + [str(path) for path in sources], check=True, cwd=root)
