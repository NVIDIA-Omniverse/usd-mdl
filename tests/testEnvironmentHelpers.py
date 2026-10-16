# SPDX-License-Identifier: Apache-2.0
"""Check that environment helpers preserve application-owned MDL paths."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def check_helper(prefix, selected=None):
    env = os.environ.copy()
    env.pop("USD_MDL_SEARCH_PATHS", None)
    expected_python_tail = [str(prefix / name) for name in ("application python", "additional python")]
    env["PYTHONPATH"] = os.pathsep.join(expected_python_tail)
    if selected is not None:
        env["USD_MDL_SEARCH_PATHS"] = selected
    if os.name == "nt":
        helper = prefix / "bin" / "usd-mdl-env.bat"
        # cmd.exe does not understand subprocess.list2cmdline's backslash quotes.
        command = f'"{os.environ["COMSPEC"]}" /d /s /c "call "{helper}" && set"'
    else:
        helper = prefix / "bin" / "usd-mdl-env.sh"
        command = ["sh", "-c", 'eval "$(sh "$1")"; env', "sh", str(helper)]
    result = subprocess.run(command, env=env, text=True, capture_output=True, check=True)
    values = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
    if selected is None:
        assert "USD_MDL_SEARCH_PATHS" not in values, result.stderr
    else:
        if os.name != "nt":
            assert "USD_MDL_SEARCH_PATHS" in values, result.stderr
        assert values.get("USD_MDL_SEARCH_PATHS", "") == selected, (values.get("USD_MDL_SEARCH_PATHS"), selected, result.stderr)
    assert Path(values["PXR_PLUGINPATH_NAME"]).resolve() == prefix / "plugin" / "usd"
    actual_python_paths = values["PYTHONPATH"].split(os.pathsep)
    assert actual_python_paths[0] == str(prefix / "lib" / "python")
    assert actual_python_paths[1:] == expected_python_tail


with tempfile.TemporaryDirectory(prefix="usdMdl helper paths ") as directory:
    prefix = Path(directory).resolve()
    (prefix / "bin").mkdir()
    for suffix in ("sh", "bat"):
        shutil.copyfile(Path(sys.argv[1]) / f"usd-mdl-env.{suffix}.in",
                        prefix / "bin" / f"usd-mdl-env.{suffix}")
    custom = os.pathsep.join(str(prefix / name) for name in ("application", "matching library"))
    for selected in (None, "", custom):
        check_helper(prefix, selected)
    # Leftover directories from older installs must never select MDL roots.
    for relative in ("mdl/usd", "mdl/library/Base", "mdl/library/mdl"):
        (prefix / relative).mkdir(parents=True)
        for selected in (None, "", custom):
            check_helper(prefix, selected)
print("Environment helpers preserve application paths and leave absent paths unset.")
