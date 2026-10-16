# SPDX-License-Identifier: Apache-2.0
"""Diagnostics must preserve Python registration and cold lookup behavior."""
import os
from pathlib import Path
import sys

root = Path(os.environ["PXR_TEST_DIR"]) / "mdl"
os.environ["USD_MDL_SEARCH_PATHS"] = str(root)
os.environ["USD_MDL_DISCOVER_ON_STARTUP"] = "0"
os.environ["USD_MDL_ADD_USER_PATHS"] = "0"
os.environ["USD_MDL_ADD_SYSTEM_PATHS"] = "0"
for variable in ("MDL_USER_PATH", "MDL_SYSTEM_PATH", "TF_DEBUG"):
    os.environ.pop(variable, None)

from pxr import Ar, Sdf, Tf, Usd, UsdShade
import UsdMdl

registered = sys.argv[1] == "registered"
stage = Usd.Stage.CreateInMemory()
nodes = []
resource_failures = ("missing_resource", "corrupt_resource", "malformed_resource")
assert (root / "resources/corrupt_diagnostic_texture.png").is_file()
try:
    for index, name in enumerate(("clean", "warning", "valid_resource", *resource_failures, "invalid")):
        source = root / (name + ".mdl")
        asset = Sdf.AssetPath(str(source), Ar.GetResolver().Resolve(str(source)))
        if registered:
            result = list(UsdMdl.AddModuleToRegistry(asset))
            assert bool(result) == (name != "invalid"), (name, result)
            assert all(node and node.IsValid() for node in result), name
            nodes.extend(result)
        shader = UsdShade.Shader.Define(stage, "/Shader" + str(index))
        assert shader.SetSourceAsset(asset, "mdl")
        assert shader.SetSourceAssetSubIdentifier("entry", "mdl")
        try:
            node = shader.GetShaderNodeForSourceType("mdl")
        except Tf.ErrorException as error:
            # OpenUSD itself rejects a null parser result. Preserve that existing
            # cold-lookup behavior while ensuring compiler diagnostics add no errors.
            assert name == "invalid", (name, error)
            assert "Parser for asset" in str(error) and "of type mdl returned null" in str(error), error
            assert "invalid.mdl" in str(error), error
            node = None
        assert bool(node and node.IsValid()) == (name != "invalid"), name
        if name == "clean":
            assert node.GetShaderInput("value").GetDefaultValue() == 0.25
        if name == "warning":
            assert node.GetShaderInput("value").GetDefaultValue() == 0.5
        if name == "valid_resource" or name in resource_failures:
            texture = node.GetShaderInput("tex")
            assert texture, name
            value = texture.GetDefaultValue()
            assert isinstance(value, Sdf.AssetPath), (name, value)
            if name == "valid_resource":
                assert value.path, (name, value)
                assert value.resolvedPath, (name, value)
                assert Path(value.resolvedPath).samefile(root / "valid_diagnostic_texture.png"), value
            else:
                assert value == Sdf.AssetPath(), (name, value)
        # Repeating successful lookup must return the same cached definition.
        if name != "invalid":
            repeated = shader.GetShaderNodeForSourceType("mdl")
            assert repeated and repeated.GetIdentifier() == node.GetIdentifier(), name
        node = None
        repeated = None
    print("Python diagnostics behavior passed (" + sys.argv[1] + ")", flush=True)
finally:
    nodes.clear()
    UsdMdl.Shutdown()
