#!/usr/bin/env python3
"""Inventory the linked registry without trusting constructors in the host process."""
import argparse
import json
import subprocess
from pathlib import Path


def invoke(probe, *arguments):
    result = subprocess.run(
        [str(probe), *arguments], capture_output=True, text=True, timeout=30
    )
    if result.returncode:
        raise RuntimeError(f"exit {result.returncode}: {result.stderr.strip()}")
    return json.loads(result.stdout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    probe = args.probe.resolve()
    catalog = invoke(probe, "--list")
    failures = []
    for mode in ("standard", "streaming"):
        entries = catalog[mode]
        for entry in entries:
            try:
                entry.update(invoke(probe, "--describe", mode, entry["name"]))
            except (RuntimeError, subprocess.TimeoutExpired, json.JSONDecodeError) as error:
                entry["metadataError"] = str(error)
                failures.append(f"{mode}/{entry['name']}: {error}")
        print(f"{mode}: {len(entries)} registered algorithms")
    catalog["metadataFailures"] = failures
    args.output.write_text(json.dumps(catalog, indent=2) + "\n")
    print(f"metadata failures: {len(failures)}; inventory: {args.output}")
    for failure in failures:
        print(failure)
    # A partial manifest must not silently count as complete coverage.
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
