#!/usr/bin/env python3
"""Summarize CTest results and reject skipped native acceptance checks in CI."""
import argparse
import json
import os
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def summarize(junit, log):
    root = ET.parse(junit).getroot()
    cases = list(root.iter("testcase"))
    results = {"passed": [], "skipped": [], "failed": []}
    for case in cases:
        if case.find("failure") is not None or case.find("error") is not None:
            status = "failed"
        elif case.find("skipped") is not None or case.get("status") in {"notrun", "disabled"}:
            status = "skipped"
        else:
            status = "passed"
        results[status].append(case.get("name", "<unnamed>"))
    results["internal_skips"] = [line.strip() for line in log.splitlines()
                                 if re.search(r"\bSKIP(?:PED)?\s*:", line, re.IGNORECASE)]
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--junit", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--require-complete", action="store_true")
    args = parser.parse_args()
    if not args.junit.is_file() or not args.log.is_file():
        raise SystemExit("Native test results missing: build/test step did not complete")
    results = summarize(args.junit, args.log.read_text(encoding="utf-8", errors="replace"))
    print(json.dumps(results, indent=2))
    summary = "\n".join(["### Native test coverage", "",
                         f"Passed: {len(results['passed'])}; skipped: {len(results['skipped'])}; "
                         f"failed: {len(results['failed'])}; internal skips: {len(results['internal_skips'])}.",
                         "", *["- Skipped: " + name for name in results["skipped"]],
                         *["- " + line for line in results["internal_skips"]], ""])
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as output:
            output.write(summary)
    if not results["passed"] or results["failed"]:
        raise SystemExit("Native test suite did not pass")
    if args.require_complete and (results["skipped"] or results["internal_skips"]):
        raise SystemExit("Interactive acceptance incomplete: use an idle interactive test desktop")


if __name__ == "__main__":
    main()
