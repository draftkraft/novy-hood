#!/usr/bin/env python3
"""Bump the project VERSION file.

Usage:
  tools/bump_version.py patch
  tools/bump_version.py minor
  tools/bump_version.py major
"""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
VERSION_FILE = ROOT / "VERSION"
SEMVER_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")


def main() -> int:
    part = sys.argv[1] if len(sys.argv) > 1 else "patch"
    if part not in {"major", "minor", "patch"}:
        print("usage: tools/bump_version.py [major|minor|patch]", file=sys.stderr)
        return 2

    current = VERSION_FILE.read_text(encoding="utf-8").strip()
    match = SEMVER_RE.match(current)
    if not match:
        print(f"VERSION must be major.minor.patch, got {current!r}", file=sys.stderr)
        return 1

    major, minor, patch = map(int, match.groups())
    if part == "major":
        major, minor, patch = major + 1, 0, 0
    elif part == "minor":
        minor, patch = minor + 1, 0
    else:
        patch += 1

    next_version = f"{major}.{minor}.{patch}"
    VERSION_FILE.write_text(next_version + "\n", encoding="utf-8")
    print(next_version)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
