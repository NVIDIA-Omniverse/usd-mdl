#
# SPDX-License-Identifier: Apache-2.0
#
"""Integration tests for distribution ports, hidden-function metadata,
registration, and diagnostics for broken modules."""

import os
import sys
from pathlib import Path

_ROOT = os.path.dirname(os.path.abspath(__file__))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from lib.mdl_test_base import MdlTestBase, UsdMdl, run_unittest_main_exit_clean
from pxr import Ar, Sdf, Sdr


class DistributionTypeTests(MdlTestBase):
    """Each distribution type maps to a Token in USD with a distinct renderType.

    MDL forbids material parameters of distribution type, so the
    fixture functions RETURN the distribution; the converter runs on
    the function's output port."""

    def _validate_distribution(self, subidentifier, render_type):
        node = self.validate_node("distribution.mdl", subidentifier)
        # Default output port name is "out" (see UsdMdlTokens.DefaultOutputPortName);
        # iterate to stay robust to renaming.
        outputs = list(node.GetShaderOutputNames())
        self.assertEqual(len(outputs), 1,
                         msg=f"distribution.mdl/{subidentifier}: expected exactly one output port, got {outputs!r}")
        sdr_output = node.GetShaderOutput(outputs[0])
        self.assertIsNotNone(sdr_output)
        self.assertEqual(sdr_output.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.Token)
        self.assertIsNone(sdr_output.GetDefaultValue())
        metadata = sdr_output.GetMetadata()
        self.assertEqual(
            str(self._metadata_get(metadata, Sdr.PropertyMetadata.RenderType)),
            render_type,
            msg=f"distribution.mdl/{subidentifier}: renderType mismatch on output")

    def test_type_bsdf(self):
        self._validate_distribution("test_bsdf", UsdMdl.Types.Bsdf)

    def test_type_edf(self):
        self._validate_distribution("test_edf", UsdMdl.Types.Edf)

    def test_type_vdf(self):
        self._validate_distribution("test_vdf", UsdMdl.Types.Vdf)

    def test_type_hair_bsdf(self):
        self._validate_distribution("test_hair_bsdf", UsdMdl.Types.Hair_Bsdf)


class FunctionFilterTests(MdlTestBase):
    """Hidden annotations affect UI metadata, while definitions remain usable."""

    def test_visible_function_resolves(self):
        node = self.validate_node("hidden_and_removed.mdl", "test_visible")
        self.assertEqual(node.GetMetadata()[UsdMdl.NodeMetadata.Hidden], "false")

    def test_hidden_function_is_registered(self):
        nodes = UsdMdl.AddModuleToRegistry(
            Sdf.AssetPath(str(self.mdl_path / "hidden_and_removed.mdl")))
        self.assertTrue(nodes)
        self.assertTrue(all(node and node.IsValid() for node in nodes))
        by_name = {node.GetMetadata()[UsdMdl.NodeMetadata.SubIdentifier]: node
                   for node in nodes}
        self.assertIn("test_visible", by_name)
        node = by_name["test_hidden"]
        self.assertEqual(node.GetMetadata()[UsdMdl.NodeMetadata.Hidden], "true")
        self.assertEqual(node.GetShaderInput("param").GetDefaultValue(), 2)


class BrokenModuleTests(MdlTestBase):
    """`mdl_invalid/broken.mdl` is syntactically + semantically invalid.
    Loading it must fail cleanly while exercising the SDK error-message
    dispatch loop in ModuleLoader::loadModule (the for-loop over
    `ctx->get_messages_count()` and each message's notes -- about 30
    lines that were unreachable from the happy-path tests).

    The fixture lives in a `mdl_invalid/` sibling directory so the
    healthy-module sweep tests (testMdlSdrPlugins, testMdlDiscovery)
    never see it.
    """

    def _broken_module_path(self):
        # PXR_TEST_DIR points at the per-test copy of testenv/, which
        # contains both `mdl/` and `mdl_invalid/`.  The healthy sweep
        # only walks `mdl/`; we reach into the sibling explicitly here.
        pxr_test = os.environ.get("PXR_TEST_DIR")
        self.assertTrue(pxr_test, msg="PXR_TEST_DIR must be set by mdlTestWrapper.py")
        return Path(pxr_test) / "mdl_invalid" / "broken.mdl"

    def test_invalid_module_returns_no_nodes_and_does_not_crash(self):
        broken = str(self._broken_module_path())
        self.assertTrue(os.path.exists(broken), msg=f"missing fixture: {broken}")
        asset = Sdf.AssetPath(broken, Ar.GetResolver().Resolve(broken))
        # AddModuleToRegistry catches the SDK error internally and returns
        # an empty list; the process must not abort and SdrRegistry must
        # not contain a node for any of broken.mdl's would-be exports.
        nodes = UsdMdl.AddModuleToRegistry(asset)
        self.assertEqual(list(nodes), [],
                         msg="invalid MDL must produce zero Sdr nodes (got: %r)" % (nodes,))


if __name__ == "__main__":
    run_unittest_main_exit_clean()
