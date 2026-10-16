#
# SPDX-License-Identifier: Apache-2.0
#
"""Minimal ctest wrapper for the usdMdl test suite.

Why it exists
-------------
OpenUSD does not install its `testWrapper.py` harness -- `Public.cmake`
references it via `${PROJECT_SOURCE_DIR}/cmake/macros/testWrapper.py`,
which is meaningless to plugins built against an installed OpenUSD.

Out-of-tree plugins therefore roll their own; this file is usdMdl's
copy of that pattern (cf. `Autodesk/maya-usd/cmake/test.cmake`).  It
keeps only the features `cmake/MdlPluginTesting.cmake` actually uses:

  - copy a per-test fixture directory into a fresh temp dir
  - set PXR_TEST_DIR / ARCH_AVOID_JIT for the child
  - prepend directories to PATH
  - forward KEY=VALUE env vars (with `<PXR_TEST_DIR>` substitution)
  - run a single command line and assert its exit code

Anything richer (diff-compare, image-diff, pre/post commands, output
redirection, baseline dirs) is intentionally absent.  Add features here
only if a registered test actually needs them.
"""

from __future__ import annotations

import argparse
import os
import platform
import shlex
import shutil
import subprocess
import sys
import tempfile


def _parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="usdMdl ctest wrapper")
    p.add_argument("--testenv-dir", required=True,
                   help="Directory whose contents are copied into the "
                        "per-test temp directory before the command runs.")
    p.add_argument("--env-var", dest="env_vars", action="append", default=[],
                   help="KEY=VALUE pair to inject. The literal string "
                        "'<PXR_TEST_DIR>' in VALUE is replaced with the "
                        "absolute path of the per-test temp directory.")
    p.add_argument("--pre-path", dest="pre_paths", action="append", default=[],
                   help="Directory to prepend to PATH (repeatable).")
    p.add_argument("--expected-return-code", type=int, default=0,
                   help="Test fails unless the command exits with this code.")
    p.add_argument("--verbose", "-v", action="store_true",
                   help="Log each step to stderr.")
    p.add_argument("cmd", help="The test command (single string, shell-parsed).")
    return p.parse_args()


def _convert_retcode(rc: int) -> int:
    """POSIX subprocess returns -N for signal N; convert to the conventional
    128+N so callers see the same exit codes the shell would report."""
    return rc if rc >= 0 else 128 + abs(rc)


def _copy_into(src: str, dest: str) -> None:
    """Copy contents of `src` (not the directory itself) into `dest`,
    which must already exist.  Raises FileNotFoundError if `src` does not
    exist -- a missing testenv directory is always a CMake configuration
    bug, never a test pass condition."""
    if not os.path.isdir(src):
        raise FileNotFoundError(f"testenv-dir does not exist: {src}")
    for entry in os.listdir(src):
        s = os.path.join(src, entry)
        d = os.path.join(dest, entry)
        if os.path.isdir(s):
            shutil.copytree(s, d)
        else:
            shutil.copy2(s, d)


def _split_command(command: str, *, windows: bool | None = None) -> list[str]:
    """Split CMake's command string without passing quote delimiters to argv."""
    if windows is None:
        windows = platform.system() == "Windows"
    tokens = shlex.split(command, posix=not windows)
    if windows:
        # Non-POSIX shlex retains surrounding quotes and preserves Windows
        # path backslashes. Strip one matching delimiter pair, including the
        # empty argument, while leaving interior quotes and backslashes alone.
        tokens = [token[1:-1] if len(token) >= 2 and token[0] in ("'", '\"')
                  and token[-1] == token[0] else token for token in tokens]
    return tokens


def main() -> int:
    args = _parse_args()
    verbose = args.verbose

    test_dir = tempfile.mkdtemp(prefix="usdMdlTest_")
    if verbose:
        print(f"[mdlTestWrapper] cwd: {test_dir}", file=sys.stderr)
    os.chdir(test_dir)

    if verbose:
        print(f"[mdlTestWrapper] copying testenv: {args.testenv_dir}",
              file=sys.stderr)
    try:
        _copy_into(args.testenv_dir, test_dir)
    except OSError as e:
        print(f"[mdlTestWrapper] error copying testenv: {e}", file=sys.stderr)
        # Leave test_dir behind so the CMake bug is debuggable.
        return 1

    env = os.environ.copy()
    for spec in args.env_vars:
        if "=" not in spec:
            print(f"[mdlTestWrapper] bad --env-var '{spec}' (need KEY=VALUE)",
                  file=sys.stderr)
            return 1
        key, value = spec.split("=", 1)
        if key == "PATH":
            print("[mdlTestWrapper] use --pre-path to edit PATH", file=sys.stderr)
            return 1
        env[key] = value.replace("<PXR_TEST_DIR>", test_dir)

    if args.pre_paths:
        delim = ";" if platform.system() == "Windows" else ":"
        env["PATH"] = delim.join(args.pre_paths + [env.get("PATH", "")])

    # Documented by OpenUSD; tests look here for their copied fixtures.
    env["PXR_TEST_DIR"] = test_dir
    # Suppress the MSVC just-in-time debugger so failures don't hang CI.
    env.setdefault("ARCH_AVOID_JIT", "1")

    cmd = _split_command(args.cmd)
    if verbose:
        print(f"[mdlTestWrapper] running: {cmd}", file=sys.stderr)

    rc = _convert_retcode(subprocess.call(cmd, env=env))
    if rc != args.expected_return_code:
        print(f"[mdlTestWrapper] exit code {rc} != expected "
              f"{args.expected_return_code}", file=sys.stderr)
        # Keep the per-test temp dir on failure to aid post-mortem; CI
        # systems with disk pressure can clear /tmp's usdMdlTest_* prefix
        # between runs.
        return 1

    # On success the per-test temp dir has no further use; remove it so
    # repeated ctest runs (especially `ctest -j`) don't accumulate
    # gigabytes of stale fixtures under /tmp.  Best-effort: any error
    # cleaning up is non-fatal.
    os.chdir(os.path.dirname(test_dir) or ".")
    shutil.rmtree(test_dir, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
