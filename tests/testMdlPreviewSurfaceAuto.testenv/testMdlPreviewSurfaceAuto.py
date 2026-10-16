# SPDX-License-Identifier: Apache-2.0
"""Fresh-process regression tests for opt-in canonical MDL discovery."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


_CHILD = r'''
import ctypes
import json
import os
import sys
import traceback
from pathlib import Path

exit_code = 0
try:
    from pxr import Plug, Sdf, Sdr, Usd, UsdShade
    import UsdMdl
    Plug.Registry().RegisterPlugins(os.environ.get("PXR_PLUGINPATH_NAME", ""))
    if os.environ.get("USDMDL_TEST_USD_PLUGIN_ROOT"):
        Plug.Registry().RegisterPlugins(os.environ["USDMDL_TEST_USD_PLUGIN_ROOT"])
    registry = Sdr.Registry()
    resources = Path(Plug.Registry().GetPluginWithName("usdMdl").resourcePath)
    stage = Usd.Stage.Open(str(resources / "UsdPreviewSurface.usda"))
    assert stage
    shaders = [UsdShade.Shader(p) for p in stage.Traverse() if p.IsA(UsdShade.Shader)]
    identifiers = [str(shader.GetIdAttr().Get()) for shader in shaders]
    family_ids = {str(shader.GetSourceAssetSubIdentifier("mdl")): str(shader.GetIdAttr().Get())
                  for shader in shaders}
    assert "UsdPreviewSurface" in family_ids
    enabled = os.environ["AUTO_TEST_ENABLED"] == "1"
    missing = os.environ.get("AUTO_TEST_MISSING") == "1"
    mdl_nodes = []
    for identifier in identifiers:
        mdl_node = registry.GetShaderNodeByIdentifierAndType(identifier, "mdl")
        glslfx_node = registry.GetShaderNodeByIdentifierAndType(identifier, "glslfx")
        assert glslfx_node and glslfx_node.IsValid(), (identifier, "missing glslfx")
        assert str(glslfx_node.GetSourceType()) == "glslfx"
        if enabled and not missing:
            assert mdl_node and mdl_node.IsValid(), (identifier, "missing mdl")
            assert str(mdl_node.GetSourceType()) == "mdl"
            assert mdl_node.GetIdentifier() == identifier
            assert registry.GetShaderNodeByIdentifier(identifier, ["mdl", "glslfx"]).GetSourceType() == "mdl"
            assert registry.GetShaderNodeByIdentifier(identifier, ["glslfx", "mdl"]).GetSourceType() == "glslfx"
            mdl_nodes.append(mdl_node)
        else:
            assert mdl_node is None, (identifier, "unexpected automatic mdl registration")
    assert all(node.GetMetadata().get("mdl:subIdentifier") != "ApplicationOnly"
               for node in registry.GetShaderNodesByFamily() if node.GetSourceType() == "mdl"), "unexpected full native catalog scan"
    if enabled and not missing:
        surface = registry.GetShaderNodeByIdentifierAndType(family_ids["UsdPreviewSurface"], "mdl")
        assert surface.GetShaderInput("normal").GetTypeAsSdfType().GetSdfType() == Sdf.ValueTypeNames.Normal3f
        assert not surface.GetShaderInput("useSpecularWorkflow").IsConnectable()
        texture = registry.GetShaderNodeByIdentifierAndType(family_ids["UsdUVTexture"], "mdl")
        assert texture.GetShaderInput("wrapS").GetDefaultValueAsSdfType() == "useMetadata"
        assert texture.GetShaderInput("wrapS").GetTypeAsSdfType().GetSdfType() == Sdf.ValueTypeNames.Token
        assert texture.GetShaderInput("file").GetMetadata()["__SDR__colorspace"] == "sourceColorSpace"
        assert texture.GetShaderInput("file").GetTypeAsSdfType().GetSdfType() == Sdf.ValueTypeNames.Asset
        transform = registry.GetShaderNodeByIdentifierAndType(family_ids["UsdTransform2d"], "mdl")
        assert transform.GetShaderInput("in").GetImplementationName() == "st"
    if not missing:
        # Manual registration is available even when the automatic switch is off.
        definitions = UsdMdl.AddShaderDefinitionsToRegistry(Sdf.AssetPath(str(resources / "UsdPreviewSurface.usda")))
        assert len(definitions) == len(identifiers), (len(definitions), len(identifiers))
        natives = UsdMdl.AddModuleToRegistry(Sdf.AssetPath(str(Path(os.environ["USD_MDL_SEARCH_PATHS"]) / "UsdPreviewSurface.mdl")))
        assert any(node.GetMetadata().get("mdl:subIdentifier") == "ApplicationOnly" for node in natives)
        if mdl_nodes:
            assert {node.GetIdentifier() for node in definitions} == {node.GetIdentifier() for node in mdl_nodes}
    print("AUTO_RESULT=" + json.dumps({"count": len(mdl_nodes), "ids": identifiers}))
except BaseException:
    exit_code = 1
    traceback.print_exc()
finally:
    if "UsdMdl" in globals():
        UsdMdl.Shutdown()
    sys.stdout.flush()
    sys.stderr.flush()
    if sys.platform == "win32":
        import ctypes.wintypes
        kernel = ctypes.windll.kernel32
        kernel.GetCurrentProcess.restype = ctypes.wintypes.HANDLE
        kernel.TerminateProcess.argtypes = [ctypes.wintypes.HANDLE, ctypes.wintypes.UINT]
        kernel.TerminateProcess(kernel.GetCurrentProcess(), exit_code)
    else:
        os._exit(exit_code)
'''


class PreviewSurfaceAutomaticLoadingTest(unittest.TestCase):
    def _run(self, setting: str | None, *, missing: bool = False) -> subprocess.CompletedProcess[str]:
        env = os.environ.copy()
        env.pop("USD_MDL_LOAD_USD_PREVIEW_SURFACE", None)
        if setting is not None:
            env["USD_MDL_LOAD_USD_PREVIEW_SURFACE"] = setting
        env["USD_MDL_DISCOVER_ON_STARTUP"] = "0"
        env["AUTO_TEST_ENABLED"] = "1" if setting in ("1", "true") else "0"
        env["AUTO_TEST_MISSING"] = "1" if missing else "0"
        with tempfile.TemporaryDirectory(prefix="usdMdlAutoMissing_") as empty_root:
            fixture_root = Path(os.environ["PXR_TEST_DIR"]) / "mdl"
            root = empty_root if missing else str(fixture_root)
            env["USD_MDL_SEARCH_PATHS"] = root
            env["MDL_USER_PATH"] = root
            env.pop("MDL_SYSTEM_PATH", None)
            result = subprocess.run([sys.executable, "-c", _CHILD], env=env,
                                    capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result_line = next((line for line in result.stdout.splitlines()
                            if line.startswith("AUTO_RESULT=")), None)
        self.assertIsNotNone(result_line, result.stdout + result.stderr)
        payload = json.loads(result_line.removeprefix("AUTO_RESULT="))
        self.assertEqual(payload["count"], len(payload["ids"]) if env["AUTO_TEST_ENABLED"] == "1" and not missing else 0)
        return result

    def test_unset_is_disabled_and_manual_registration_works(self) -> None:
        self._run(None)

    def test_zero_is_disabled_and_manual_registration_works(self) -> None:
        self._run("0")

    def test_false_is_disabled_and_manual_registration_works(self) -> None:
        self._run("false")

    def test_one_registers_mdl_beside_glslfx_without_full_scan(self) -> None:
        self._run("1")

    def test_true_registers_mdl_beside_glslfx_without_full_scan(self) -> None:
        self._run("true")

    def test_missing_mdl_module_warns_and_preserves_glslfx(self) -> None:
        result = self._run("1", missing=True)
        self.assertIn("::UsdPreviewSurface", result.stderr)
        self.assertIn("load_module failed", result.stderr)
        self.assertIn("Warning", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
