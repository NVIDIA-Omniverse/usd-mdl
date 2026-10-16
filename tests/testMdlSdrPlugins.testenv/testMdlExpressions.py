#
# SPDX-License-Identifier: Apache-2.0
#

import os
import sys

_ROOT = os.path.dirname(os.path.abspath(__file__))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from lib.mdl_test_base import MdlTestBase, UsdMdl, run_unittest_main_exit_clean
from pxr import Sdf, Sdr


class ExpressionTests(MdlTestBase):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        if os.environ.get("USDMDL_TEST_REGISTER_DEFAULTS") == "1":
            # Run before creating any registry/node cache. Exceptions from this
            # Python entry point must fail setup rather than be swallowed.
            module = os.path.join(os.environ["PXR_TEST_DIR"], "mdl", "expression.mdl")
            nodes = UsdMdl.AddModuleToRegistry(Sdf.AssetPath(module))
            if not nodes or not all(node.IsValid() for node in nodes):
                raise AssertionError("expression module registration failed")

    def assert_unavailable(self, node, name, sdf_type):
        self.assertTrue(node.IsValid())
        prop = node.GetShaderInput(name)
        self.assertIsNotNone(prop)
        indicator = prop.GetTypeAsSdfType()
        self.assertTrue(indicator.HasSdfType())
        self.assertEqual(indicator.GetSdfType(), sdf_type)
        # Both APIs must report an unavailable default, not a fabricated zero.
        self.assertIsNone(prop.GetDefaultValue())
        self.assertIsNone(prop.GetDefaultValueAsSdfType())
        self.assertTrue(node.GetShaderOutputNames())
        return prop

    def test_nonconstant_defaults_remain_unavailable(self):
        # These expressions depend on shading state or on parameters that can
        # be overridden. Even constant arguments do not make a call a value.
        for definition, sdf_type in (
            ("test_call_expression", Sdf.ValueTypeNames.Float3),
            ("test_call_expression_with_args", Sdf.ValueTypeNames.Float3),
            ("test_call_expression_array", Sdf.ValueTypeNames.Float3Array),
            ("test_parameter_expression", Sdf.ValueTypeNames.Int),
            ("test_multi_parameter_expression", Sdf.ValueTypeNames.Int),
            ("test_parameter_call_expression", Sdf.ValueTypeNames.Float3),
            ("test_parameter_expression_color", Sdf.ValueTypeNames.Color3f),
            ("test_parameter_expression_color_splat", Sdf.ValueTypeNames.Color3f),
            ("test_parameter_expression_vector_bool4", Sdf.ValueTypeNames.BoolArray),
            ("test_parameter_expression_vector_splat", Sdf.ValueTypeNames.Float3),
            ("test_parameter_expression_float_matrix", Sdf.ValueTypeNames.FloatArray),
            ("test_parameter_expression_double_matrix", Sdf.ValueTypeNames.Matrix2d),
            ("test_parameter_expression_matrix_diagonal", Sdf.ValueTypeNames.Matrix2d),
            ("test_parameter_expression_matrix_columns", Sdf.ValueTypeNames.Matrix2d),
            ("test_parameter_expression_matrix_conversion", Sdf.ValueTypeNames.Matrix2d),
            ("test_parameter_expression_nonsquare_matrix_columns", Sdf.ValueTypeNames.FloatArray),
            ("test_parameter_expression_nonsquare_matrix_conversion", Sdf.ValueTypeNames.DoubleArray),
            ("parameter_nested", Sdf.ValueTypeNames.Matrix2d),
        ):
            with self.subTest(definition=definition):
                node = self.validate_node("expression.mdl", definition)
                self.assert_unavailable(node, "param", sdf_type)

    def test_root_expression_metadata_is_descriptive(self):
        for definition, kind, value in (
            ("test_call_expression", UsdMdl.ExpressionKinds.Call, "mdl::state::normal()"),
            ("test_call_expression_with_args", UsdMdl.ExpressionKinds.Call,
             "mdl::state::texture_tangent_v(int)"),
            ("test_parameter_expression", UsdMdl.ExpressionKinds.Parameter, "1"),
            ("test_multi_parameter_expression", UsdMdl.ExpressionKinds.Parameter, "1"),
        ):
            with self.subTest(definition=definition):
                prop = self.validate_node("expression.mdl", definition).GetShaderInput("param")
                self.assertEqual(prop.GetMetadata()[UsdMdl.Metadata.ExpressionKind], kind)
                self.assertEqual(prop.GetMetadata()[UsdMdl.Metadata.ExpressionValue], value)
                self.assertEqual(prop.GetMetadata()[UsdMdl.Metadata.HasDefault], "true")

    def test_state_color_does_not_raise_and_preserves_other_defaults(self):
        # No exception handler: a Tf.ErrorException from node creation must fail
        # this test. Native coverage also checks that no Tf errors were emitted.
        node = self.validate_node("expression.mdl", "state_color")
        self.assert_unavailable(node, "diffuse_color", Sdf.ValueTypeNames.Color3f)
        self.assertEqual(set(node.GetShaderInputNames()), {
            "diffuse_color", "highlight_color", "highlight_intensity"})
        self.validateValue(node.GetShaderInput("highlight_color").GetDefaultValue(), (0.2, 0.9, 0.6), is_default=True)
        self.assertEqual(node.GetShaderInput("highlight_intensity").GetDefaultValue(), 0.25)

    def test_missing_zero_folded_and_parameter_defaults_are_distinct(self):
        node = self.validate_node("expression.mdl", "default_boundaries")
        for name in node.GetShaderInputNames():
            self.assertEqual(node.GetShaderInput(name).GetMetadata()[UsdMdl.Metadata.HasDefault],
                             "false" if name == "required" else "true")
        for name in ("required", "referenced", "computed"):
            self.assert_unavailable(node, name, Sdf.ValueTypeNames.Float)
        for name, expected in (("zero", 0.0), ("folded", 6.0)):
            prop = node.GetShaderInput(name)
            self.assertEqual(prop.GetDefaultValue(), expected)
            self.assertEqual(prop.GetDefaultValueAsSdfType(), expected)
            self.assertEqual(prop.GetMetadata()[UsdMdl.Metadata.ExpressionKind],
                             UsdMdl.ExpressionKinds.Constant)
            self.assertNotIn(UsdMdl.Metadata.ExpressionValue, prop.GetMetadata())
        required_metadata = node.GetShaderInput("required").GetMetadata()
        self.assertNotIn(UsdMdl.Metadata.ExpressionKind, required_metadata)
        self.assertNotIn(UsdMdl.Metadata.ExpressionValue, required_metadata)
        for name in node.GetShaderOutputNames():
            self.assertNotIn(UsdMdl.Metadata.HasDefault,
                             node.GetShaderOutput(name).GetMetadata())
            self.assertNotIn(UsdMdl.Metadata.ExpressionKind,
                             node.GetShaderOutput(name).GetMetadata())

    def test_unrepresentable_constant_preserves_kind(self):
        node = self.validate_node("expression.mdl", "constant_struct")
        prop = self.assert_unavailable(node, "param", Sdf.ValueTypeNames.Token)
        self.assertEqual(prop.GetMetadata()[UsdMdl.Metadata.HasDefault], "true")
        self.assertEqual(prop.GetMetadata()[UsdMdl.Metadata.ExpressionKind],
                         UsdMdl.ExpressionKinds.Constant)
        self.assertNotIn(UsdMdl.Metadata.ExpressionValue, prop.GetMetadata())

    def test_folded_nested_constructors(self):
        self.validate_mdl(
            "expression.mdl", "constant_nested", Sdf.ValueTypeNames.Color3f,
            (0.1, 0.2, 0.3), {Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Color})
        self.validate_mdl(
            "expression.mdl", "test_literal_matrix_columns", Sdf.ValueTypeNames.Matrix2d,
            ((1.0, 2.0), (3.0, 4.0)),
            {Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Double2x2})

    def test_rectangular_diagonal_constants_preserve_column_order(self):
        for shape, expected in (
            ("2x3", [-2.5, 0, 0, 0, -2.5, 0]),
            ("3x2", [-2.5, 0, 0, -2.5, 0, 0]),
        ):
            for scalar, sdf_type in (
                ("float", Sdf.ValueTypeNames.FloatArray),
                ("double", Sdf.ValueTypeNames.DoubleArray),
            ):
                mdl_type = scalar + shape
                with self.subTest(mdl_type=mdl_type):
                    node = self.validate_node("expression.mdl", "test_diagonal_" + mdl_type)
                    self.assertEqual(node.GetShaderInput("d").GetDefaultValue(), -2.5)
                    self.assert_unavailable(node, "param", sdf_type)
                    prop = node.GetShaderInput("literal")
                    self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), sdf_type)
                    self.assertEqual(prop.GetMetadata()[Sdr.PropertyMetadata.RenderType], mdl_type)
                    self.assertEqual(list(prop.GetDefaultValue()), expected)
                    self.assertEqual(list(prop.GetDefaultValueAsSdfType()), expected)


if __name__ == "__main__":
    run_unittest_main_exit_clean()
