#  PlatformIO pre-build hook: bake the firmware version into the build as FW_VERSION.
#  The source of truth is VERSION. Local interactive builds ask to bump the patch
#  version when firmware files changed but VERSION did not. CI stays non-interactive.
Import("env")
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(env.subst("$PROJECT_DIR"))
VERSION_FILE = ROOT / "VERSION"
SEMVER_RE = re.compile(r"^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$")


def run_git(args):
    try:
        return subprocess.check_output(
            ["git", *args],
            cwd=str(ROOT),
            stderr=subprocess.DEVNULL,
        ).decode().strip()
    except Exception:
        return ""


def read_version():
    try:
        version = VERSION_FILE.read_text(encoding="utf-8").strip()
    except FileNotFoundError:
        return "dev"
    if not SEMVER_RE.match(version):
        raise SystemExit(f"VERSION must be semantic version text like 0.1.0, got: {version!r}")
    return version


def version_from_head():
    if not run_git(["rev-parse", "--is-inside-work-tree"]):
        return ""
    return run_git(["show", "HEAD:VERSION"])


def firmware_changes_present():
    paths = ["src", "platformio.ini", "tools/version.py"]
    porcelain = run_git(["status", "--porcelain", "--", *paths])
    return bool(porcelain)


def bump_patch(version):
    major, minor, patch = version.split(".", 2)
    patch = re.match(r"\d+", patch).group(0)
    return f"{major}.{minor}.{int(patch) + 1}"


def maybe_prompt_for_bump(version):
    if os.environ.get("CI") or not sys.stdin.isatty() or not sys.stdout.isatty():
        return version
    if not firmware_changes_present():
        return version
    previous = version_from_head()
    if previous and previous.strip() != version:
        return version

    next_version = bump_patch(version)
    print(f"Firmware files changed and VERSION is still {version}.")
    answer = input(f"Increase VERSION to {next_version}? [y/N] ").strip().lower()
    if answer not in ("y", "yes"):
        return version
    VERSION_FILE.write_text(next_version + "\n", encoding="utf-8")
    return next_version


version = maybe_prompt_for_bump(read_version())
# StringifyMacro escapes it into a proper C string literal: -DFW_VERSION='"abc1234"'
env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version))])
print("FW_VERSION = %s" % version)
