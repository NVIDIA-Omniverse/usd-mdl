#
# SPDX-License-Identifier: Apache-2.0
#

import os
import sys
from pathlib import Path

_ROOT = os.path.dirname(os.path.abspath(__file__))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from lib.mdl_test_base import MdlTestBase, UsdMdl, run_unittest_main_exit_clean
from pxr import Sdf, Sdr, UsdShade


class DiscoveryTests(MdlTestBase):
    def test_arithmetic_fixture_definitions(self):
        # A fixture named math.mdl loaded SDK built-ins instead of these exports.
        for type_name, a, b in (("float", 0.1, 0.2), ("int", 1, 2)):
            signature = f"test_add({type_name},{type_name})"
            with self.subTest(signature=signature):
                node = self.validate_node("arithmetic.mdl", signature)
                self.assertEqual(node.GetMetadata()["mdl:mdl_db_definition_name"],
                                 "mdl::arithmetic::" + signature)
                self.assertAlmostEqual(node.GetShaderInput("a").GetDefaultValue(), a)
                self.assertAlmostEqual(node.GetShaderInput("b").GetDefaultValue(), b)

    def test_standard_module_has_no_source_file(self):
        nodes = UsdMdl.AddModuleToRegistry(Sdf.AssetPath("math.mdl"))
        selected = [node for node in nodes if node.GetMetadata().get("mdl:mdl_db_definition_name")
                    == "mdl::math::sqrt(float)"]
        self.assertEqual(len(selected), 1)
        node = selected[0]
        self.assertTrue(node.IsValid())
        self.assertEqual(node.GetResolvedImplementationURI(), "")
        self.assertEqual(node.GetResolvedDefinitionURI(), "")
        self.assertEqual(node.GetMetadata()["mdl:moduleUsdIdentifier"], "math.mdl")
        self.assertEqual(node.GetMetadata()["mdl:is_embedded_module"], "true")

    def test_mdl_discover_on_startup(self):
        """Modules containing valid shader definitions are discovered through USD.

        Authors one UsdShade.Shader per .mdl file on the in-memory stage, then walks
        the stage to extract sourceAssets and cross-references them with the set of
        files Sdr.Registry has discovered.
        """
        non_shader_modules = {
            "sdr_metadata_invalid.mdl": "every exported function has an incompatible USD type override",
            "sdr_metadata_other.mdl": "exports annotations, with no shader definitions",
        }
        all_modules = sorted(file.name for file in Path(self.mdl_path).glob("*.mdl"))
        self.assertTrue(set(non_shader_modules) <= set(all_modules))
        modules = [name for name in all_modules if name not in non_shader_modules]
        self.assertGreater(len(modules), 0, msg=f"no .mdl files under {self.mdl_path}")

        # Author one UsdShade.Shader per .mdl file (no subidentifier needed for
        # this test; we are exercising discovery, not per-function resolution).
        for i, mdl_name in enumerate(modules):
            prim_path = f"/World/MdlShader_{i:03d}"
            prim = self.create_prim(prim_path, "Shader")
            shader = UsdShade.Shader(prim)
            self.assertIsNotNone(shader)
            asset = self.get_module_path(mdl_name)
            self.assertTrue(shader.SetSourceAsset(asset, UsdMdl.Tokens.Mdl))

        # Build the set of .mdl files Sdr has discovered.
        discovered_files = set()
        for sdr_node_id in self.sdr_registry.GetShaderNodeIdentifiers(filter=Sdr.VersionFilterAllVersions):
            sdr_node = self.sdr_registry.GetShaderNodeByIdentifier(sdr_node_id)
            if (not sdr_node) or (str(sdr_node.GetSourceType()) != UsdMdl.Tokens.Mdl):
                continue
            discovered_files.add(Path(sdr_node.GetResolvedImplementationURI()).name)

        for name, reason in non_shader_modules.items():
            self.assertNotIn(name, discovered_files, msg=f"{name} advertised a shader despite {reason}")

        # Walk the stage; for each authored UsdShade.Shader, read back its
        # sourceAsset and assert Sdr discovery saw the same .mdl.
        found = set()
        for prim in self.stage.Traverse():
            shader = UsdShade.Shader(prim)
            if not shader:
                continue
            src = shader.GetSourceAsset(UsdMdl.Tokens.Mdl)
            if not src:
                continue
            mdl_name = Path(src.path).name
            self.assertIn(mdl_name, discovered_files,
                          msg=f"{prim.GetPath()}: {mdl_name} not in Sdr discovery")
            found.add(mdl_name)

        self.assertEqual(set(modules), found,
                         msg=f"missing {set(modules) - found}")

    def test_unqualified_same_module_overload_resolves(self):
        """Kit's unqualified local type identifies the struct overload."""
        sdr_node = self.validate_node("sub_identifier.mdl", "over_id(Local)")

        # Bound the struct overload (param `s`), not an int overload
        # (params `x` / `y`).
        self.assertIsNotNone(
            sdr_node.GetShaderInput("s"),
            msg="over_id(Local) bound the wrong overload")

        # The resolved node's own sub-identifier is the current form.
        self.assertEqual(
            sdr_node.GetMetadata().get("mdl:subIdentifier", ""),
            "over_id(Local)")


if __name__ == "__main__":
    run_unittest_main_exit_clean()
