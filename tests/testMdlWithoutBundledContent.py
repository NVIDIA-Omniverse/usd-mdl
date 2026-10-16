# SPDX-License-Identifier: Apache-2.0
"""Load application content with no repository MDL roots configured."""

import os
from pathlib import Path

# Isolate this contract from a developer's default libraries before importing USD.
root = Path(os.environ["PXR_TEST_DIR"]) / "mdl"
os.environ["USD_MDL_SEARCH_PATHS"] = str(root)
for variable in ("MDL_USER_PATH", "MDL_SYSTEM_PATH"):
    os.environ.pop(variable, None)
for variable in ("USD_MDL_ADD_USER_PATHS", "USD_MDL_ADD_SYSTEM_PATHS",
                 "USD_MDL_DISCOVER_ON_STARTUP"):
    os.environ[variable] = "0"

import UsdMdl
from pxr import Sdf

nodes = UsdMdl.AddModuleToRegistry(Sdf.AssetPath("application_only.mdl"))
try:
    assert len(nodes) == 1 and nodes[0].IsValid(), "application module did not load"
    assert nodes[0].GetShaderInput("value").GetDefaultValue() == 0.5
finally:
    nodes.clear()
    UsdMdl.Shutdown()
