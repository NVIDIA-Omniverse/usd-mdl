# SPDX-License-Identifier: Apache-2.0

import os
import sys

_ROOT = os.path.dirname(os.path.abspath(__file__))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from lib.mdl_test_base import MdlTestBase, UsdMdl, run_unittest_main_exit_clean
from pxr import Sdf, Sdr, UsdShade


class UsdMdlBindingTests(MdlTestBase):
    """Test the UsdMdl Python binding (UsdMdl.AddModuleToRegistry) via UsdShade.

    Each test authors a UsdShade.Shader on the in-memory stage, reads back its
    `info:mdl:sourceAsset`, hands that asset path to AddModuleToRegistry, and
    verifies the returned SdrShaderNodes are also resolvable through
    UsdShade.Shader.GetShaderNodeForSourceType.
    """

    def _author_shader(self, prim_path, mdl_module, subidentifier=""):
        prim = self.create_prim(prim_path, "Shader")
        shader = UsdShade.Shader(prim)
        self.assertIsNotNone(shader)
        asset = self.get_module_path(mdl_module)
        self.assertTrue(shader.SetSourceAsset(asset, UsdMdl.Tokens.Mdl))
        if subidentifier:
            self.assertTrue(shader.SetSourceAssetSubIdentifier(subidentifier, UsdMdl.Tokens.Mdl))
        return shader

    def test_add_module_returns_nodes(self):
        """AddModuleToRegistry, fed an asset read back from UsdShade, returns nodes."""
        shader = self._author_shader("/World/AddReturnsNodes", "color.mdl")
        src = shader.GetSourceAsset(UsdMdl.Tokens.Mdl)
        self.assertIsNotNone(src)
        nodes = UsdMdl.AddModuleToRegistry(src)
        self.assertIsInstance(nodes, list)
        self.assertGreater(len(nodes), 0, "Expected at least one node from color.mdl")

    def test_returned_nodes_in_sdr_registry(self):
        """Nodes returned by AddModuleToRegistry round-trip through SdrRegistry."""
        shader = self._author_shader("/World/AddInRegistry", "color.mdl")
        src = shader.GetSourceAsset(UsdMdl.Tokens.Mdl)
        nodes = UsdMdl.AddModuleToRegistry(src)
        sdr = Sdr.Registry()
        for node in nodes:
            found = sdr.GetShaderNodeByIdentifierAndType(
                node.GetIdentifier(), node.GetSourceType()
            )
            self.assertIsNotNone(
                found,
                f"Node {node.GetIdentifier()} not found in SdrRegistry",
            )

    def test_returned_nodes_resolvable_via_usdshade(self):
        """Every node from AddModuleToRegistry must also resolve via UsdShade.Shader."""
        seed = self._author_shader("/World/AddResolveSeed", "color.mdl")
        src = seed.GetSourceAsset(UsdMdl.Tokens.Mdl)
        nodes = UsdMdl.AddModuleToRegistry(src)
        self.assertGreater(len(nodes), 0)

        sub_id_key = UsdMdl.NodeMetadata.SubIdentifier
        for i, node in enumerate(nodes):
            # mdl:subIdentifier metadata is set by our parser; use it to author
            # a fresh UsdShade.Shader and resolve through the canonical USD path.
            sub = None
            md = node.GetMetadata() or {}
            for k, v in md.items():
                if str(k) == sub_id_key:
                    sub = str(v)
                    break
            self.assertIsNotNone(sub, msg=f"node {node.GetIdentifier()} missing {sub_id_key}")

            prim_path = f"/World/AddResolve_{i:03d}"
            child = self._author_shader(prim_path, "color.mdl", subidentifier=sub)
            resolved = child.GetShaderNodeForSourceType(UsdMdl.Tokens.Mdl)
            self.assertIsNotNone(
                resolved,
                msg=f"UsdShade.Shader at {prim_path} (sub={sub!r}) failed to resolve",
            )
            self.assertEqual(resolved.GetIdentifier(), node.GetIdentifier())

    def test_add_module_empty_path_returns_empty(self):
        """AddModuleToRegistry with an unset UsdShade source asset should be empty."""
        prim = self.create_prim("/World/EmptyAsset", "Shader")
        shader = UsdShade.Shader(prim)
        self.assertIsNotNone(shader)
        src = shader.GetSourceAsset(UsdMdl.Tokens.Mdl)
        self.assertIsNone(src, msg="expected no sourceAsset on freshly-defined Shader prim")
        nodes = UsdMdl.AddModuleToRegistry(Sdf.AssetPath(""))
        self.assertIsInstance(nodes, list)
        self.assertEqual(len(nodes), 0)

    def test_node_module_identity(self):
        """Equal basenames in different packages retain distinct module identities."""
        self.assertEqual(UsdMdl.NodeMetadata.MdlDbModuleName, "mdl:mdl_db_module_name")
        self.assertEqual(UsdMdl.NodeMetadata.MdlDbDefinitionName, "mdl:mdl_db_definition_name")
        for package, expected in (
            ("first", {"choose(float)": 0.25, "choose(int)": 7, "surface()": None}),
            ("second", {"choose(float)": 0.75}),
        ):
            module_path = f"node_metadata/{package}/definitions.mdl"
            nodes = UsdMdl.AddModuleToRegistry(self.get_module_path(module_path))
            actual = {}
            for node in nodes:
                metadata = node.GetMetadata()
                self.assertEqual(metadata[UsdMdl.NodeMetadata.ModuleUsdIdentifier], module_path)
                self.assertEqual(metadata["mdl:mdl_db_module_name"],
                                 f"mdl::node_metadata::{package}::definitions")
                signature = metadata[UsdMdl.NodeMetadata.MdlNameWithSignature]
                self.assertEqual(metadata["mdl:mdl_db_definition_name"],
                                 f"mdl::node_metadata::{package}::definitions::{signature}")
                value = node.GetShaderInput("value")
                actual[signature] = value.GetDefaultValue() if value else None
            self.assertEqual(actual, expected)


if __name__ == "__main__":
    run_unittest_main_exit_clean()
