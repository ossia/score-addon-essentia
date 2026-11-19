#!/usr/bin/env python3
"""Apply the reviewed fixes to an isolated pinned Essentia source tree."""
import argparse
import hashlib
from pathlib import Path
import subprocess

REVISION = "23ae7baddecfc3227ba23ad69fa11f73ac5293a4"
PATCH_LEVEL = 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--git", default="git")
    args = parser.parse_args()
    source = args.source.resolve()
    patch = Path(__file__).with_name("essentia-safety.patch").resolve()
    if (source / ".git").exists():
        revision = subprocess.check_output(
            [args.git, "rev-parse", "HEAD"], cwd=source, text=True).strip()
        if revision != REVISION:
            raise SystemExit(f"Expected Essentia {REVISION}, found {revision}")
    command = [args.git, "apply", "--whitespace=nowarn"]
    check = subprocess.run(command + ["--check", str(patch)], cwd=source,
                           text=True, capture_output=True)
    if check.returncode == 0:
        subprocess.run(command + [str(patch)], cwd=source, check=True)
    else:
        reverse = subprocess.run(command + ["--reverse", "--check", str(patch)],
                                 cwd=source, text=True, capture_output=True)
        if reverse.returncode != 0:
            raise SystemExit("Essentia differs from the reviewed source; refusing a partial patch.\n"
                             + check.stderr + reverse.stderr)
    digest = hashlib.sha256(patch.read_bytes()).hexdigest()
    header = source / "src/essentia/score_essentia_patch_level.h"
    content = ("#pragma once\n"
               f"#define SCORE_ESSENTIA_PATCH_LEVEL {PATCH_LEVEL}\n"
               f'#define SCORE_ESSENTIA_UPSTREAM_REVISION "{REVISION}"\n'
               f'#define SCORE_ESSENTIA_PATCH_SHA256 "{digest}"\n')
    if not header.exists() or header.read_text() != content:
        header.write_text(content)
    print(f"Essentia {REVISION}: score patch level {PATCH_LEVEL} ({digest[:12]})")


if __name__ == "__main__":
    main()
