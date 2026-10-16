# SPDX-License-Identifier: Apache-2.0

import os
import sys
import tempfile
from pathlib import Path

_ROOT = os.path.dirname(os.path.abspath(__file__))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from lib.mdl_test_base import MdlTestBase, UsdMdl, run_unittest_main_exit_clean
from pxr import Sdf, Sdr, Usd, UsdShade


class MdlCanConnectToTests(MdlTestBase):
    """Check native MDL declarations through the public Python helper."""

    def _ports(self, module: str, definition: str):
        node = self.validate_node(module, definition)
        input_port = node.GetShaderInput("param")
        output_port = node.GetShaderOutput(UsdMdl.Tokens.DefaultOutputPortName)
        self.assertIsNotNone(input_port)
        self.assertIsNotNone(output_port)
        return input_port, output_port

    def _assert_connection(self, expected: bool, first, second) -> None:
        self.assertIs(UsdMdl.CanConnectTo(first, second), expected)
        self.assertIs(UsdMdl.CanConnectTo(second, first), expected)

    def test_scalars_require_matching_mdl_types(self):
        float_input, float_output = self._ports("float.mdl", "test_float")
        int_input, int_output = self._ports("int.mdl", "test_int")
        bool_input, bool_output = self._ports("bool.mdl", "test_bool")
        self._assert_connection(True, float_input, float_output)
        self._assert_connection(True, int_input, int_output)
        self._assert_connection(True, bool_input, bool_output)
        self._assert_connection(False, float_input, int_output)
        self._assert_connection(False, bool_input, int_output)
        self._assert_connection(False, int_input, bool_output)

    def test_null_and_same_direction_are_rejected(self):
        input_port, output_port = self._ports("float.mdl", "test_float")
        for first, second in (
            (None, None), (None, input_port), (None, output_port),
            (input_port, input_port), (output_port, output_port),
        ):
            with self.subTest(first=first, second=second):
                self._assert_connection(False, first, second)

    def test_input_connectability_is_respected(self):
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_connectable")
        output = node.GetShaderOutput(UsdMdl.Tokens.DefaultOutputPortName)
        self.assertIsNotNone(output)
        for name in ("unannotated", "enabled", "marker"):
            with self.subTest(parameter=name):
                self._assert_connection(True, node.GetShaderInput(name), output)
        disabled = node.GetShaderInput("disabled")
        self.assertFalse(disabled.IsConnectable())
        self._assert_connection(False, disabled, output)

    def test_flattened_matrix_dimensions_remain_distinct(self):
        input23, output23 = self._ports("matrix.mdl", "test_matrix_float2x3")
        input32, output32 = self._ports("matrix.mdl", "test_matrix_float3x2")
        for port in (input23, output23, input32, output32):
            self.assertEqual(port.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.FloatArray)
            self.assertEqual(port.GetArraySize(), 6)
        self._assert_connection(True, input23, output23)
        self._assert_connection(False, input23, output32)
        self._assert_connection(False, input32, output23)

    def test_ordinary_aliases_match_the_underlying_type(self):
        alias_input, alias_output = self._ports("alias.mdl", "test_alias")
        int_input, int_output = self._ports("int.mdl", "test_int")
        self.assertEqual(alias_input.GetMetadata()[UsdMdl.Metadata.Symbol], "::alias::number")
        self.assertEqual(alias_input.GetMetadata()[UsdMdl.Metadata.Type], UsdMdl.Types.Int)
        self._assert_connection(True, alias_input, int_output)
        self._assert_connection(True, int_input, alias_output)

    def test_named_types_require_full_identity(self):
        for family in ("struct", "enum"):
            with self.subTest(family=family):
                input_a, output_a = self._ports("type_checks/identity.mdl", family + "_a")
                input_b, output_b = self._ports("type_checks/identity.mdl", family + "_b")
                self.assertEqual(input_a.GetMetadata()[UsdMdl.Metadata.Type], family)
                self.assertNotEqual(
                    input_a.GetMetadata()[UsdMdl.Metadata.Symbol],
                    input_b.GetMetadata()[UsdMdl.Metadata.Symbol],
                )
                self._assert_connection(True, input_a, output_a)
                self._assert_connection(True, input_b, output_b)
                self._assert_connection(False, input_a, output_b)
                self._assert_connection(False, input_b, output_a)

    def test_zero_length_and_fixed_array_lengths(self):
        empty_input, empty_output = self._ports("type_checks/identity.mdl", "empty_array")
        single_input, single_output = self._ports("type_checks/identity.mdl", "singleton_array")
        scalar_input, scalar_output = self._ports("float.mdl", "test_float")
        self.assertEqual(empty_input.GetMetadata()[UsdMdl.Metadata.Type], UsdMdl.Types.Array)
        self.assertEqual(empty_input.GetMetadata()[UsdMdl.Metadata.ArraySize], "0")
        self.assertEqual(empty_input.GetArraySize(), 0)
        self._assert_connection(True, empty_input, empty_output)
        self._assert_connection(True, single_input, single_output)
        self._assert_connection(False, empty_input, single_output)
        self._assert_connection(False, single_input, empty_output)
        self._assert_connection(False, empty_input, scalar_output)
        self._assert_connection(False, scalar_input, empty_output)

    def test_matrix_arrays_compare_element_shape(self):
        input23, output23 = self._ports("array.mdl", "test_matrix_float2x3_array")
        input32, output32 = self._ports("array.mdl", "test_matrix_float3x2_array")
        for port in (input23, output23, input32, output32):
            metadata = port.GetMetadata()
            self.assertEqual(metadata[UsdMdl.Metadata.ArraySize], "2")
            self.assertEqual(port.GetArraySize(), 12)
        self._assert_connection(True, input23, output23)
        self._assert_connection(False, input23, output32)
        self._assert_connection(False, input32, output23)

    def test_array_and_flattened_matrix_are_distinct(self):
        array_input, array_output = self._ports("array.mdl", "test_double_array")
        matrix_input, matrix_output = self._ports("matrix.mdl", "test_matrix_double2x3")
        self.assertEqual(array_output.GetArraySize(), matrix_output.GetArraySize())
        self.assertEqual(
            array_output.GetTypeAsSdfType().GetSdfType(),
            matrix_output.GetTypeAsSdfType().GetSdfType(),
        )
        self._assert_connection(False, array_input, matrix_output)
        self._assert_connection(False, matrix_input, array_output)

    def test_fixed_outputs_can_supply_deferred_inputs(self):
        deferred_input, deferred_output = self._ports(
            "type_checks/identity.mdl", "deferred_matrix_array"
        )
        fixed_input, fixed_output = self._ports("array.mdl", "test_matrix_float2x3_array")
        self.assertTrue(deferred_input.IsDynamicArray())
        self._assert_connection(True, deferred_input, fixed_output)
        self._assert_connection(False, fixed_input, deferred_output)
        self._assert_connection(True, deferred_input, deferred_output)

        deferred = self.validate_node("type_checks/identity.mdl", "deferred_matrix_array")
        empty_input, empty_output = self._ports("type_checks/identity.mdl", "empty_array")
        self._assert_connection(True, deferred.GetShaderInput("weights"), empty_output)
        self._assert_connection(False, empty_input, deferred_output)

    def test_named_array_elements_preserve_identity(self):
        for definition in ("struct_array", "enum_array"):
            with self.subTest(definition=definition):
                input_port, output_port = self._ports("type_checks/identity.mdl", definition)
                self._assert_connection(True, input_port, output_port)
                scalar_input, scalar_output = self._ports(
                    "type_checks/identity.mdl", definition.removesuffix("_array") + "_a"
                )
                self._assert_connection(False, input_port, scalar_output)
                self._assert_connection(False, scalar_input, output_port)
        struct_input, _ = self._ports("type_checks/identity.mdl", "struct_array")
        _, other_struct_output = self._ports("array.mdl", "test_struct_array")
        self.assertEqual(struct_input.GetMetadata()[UsdMdl.Metadata.ArraySize], "2")
        self.assertEqual(other_struct_output.GetMetadata()[UsdMdl.Metadata.ArraySize], "2")
        self._assert_connection(False, struct_input, other_struct_output)

    def test_explicit_varying_output_cannot_supply_uniform_input(self):
        uniform_input, uniform_output = self._ports(
            "type_modifiers.mdl", "test_type_modifier_uniform"
        )
        varying_input, varying_output = self._ports(
            "type_modifiers.mdl", "test_type_modifier_varying"
        )
        self.assertEqual(
            uniform_input.GetMetadata()[UsdMdl.Metadata.Modifier], UsdMdl.TypeModifiers.Uniform
        )
        self.assertEqual(
            varying_output.GetMetadata()[UsdMdl.Metadata.Modifier], UsdMdl.TypeModifiers.Varying
        )
        self._assert_connection(False, uniform_input, varying_output)
        self._assert_connection(True, varying_input, uniform_output)
        self._assert_connection(True, uniform_input, uniform_output)

    def test_shader_definition_keeps_native_type_across_authored_overrides(self):
        stage = Usd.Stage.CreateInMemory()
        shader = UsdShade.Shader.Define(stage, "/CanConnectProfile")
        shader.CreateIdAttr("UsdMdlFixtureCanConnectProfile")
        shader.SetSourceAsset(self.get_module_path("vector.mdl"), UsdMdl.Tokens.Mdl)
        shader.SetSourceAssetSubIdentifier("test_vector_float3(float3)", UsdMdl.Tokens.Mdl)
        incoming = shader.CreateInput("incoming", Sdf.ValueTypeNames.Float3)
        incoming.GetAttr().SetMetadata("sdrMetadata", {
            "implementationName": "param",
            "renderType": "custom incoming role",
            "mdl.type": "int",
        })
        outgoing = shader.CreateOutput("result", Sdf.ValueTypeNames.Float3)
        outgoing.GetAttr().SetMetadata("sdrMetadata", {
            "implementationName": "out",
            "renderType": "custom outgoing role",
            "mdl.type": "bool",
        })
        with tempfile.TemporaryDirectory() as temporary:
            definitions = Path(temporary) / "can_connect_profile.usda"
            self.assertTrue(stage.GetRootLayer().Export(str(definitions)))
            nodes = UsdMdl.AddShaderDefinitionsToRegistry(Sdf.AssetPath(str(definitions)))
        self.assertEqual(len(nodes), 1)
        input_port = nodes[0].GetShaderInput("incoming")
        output_port = nodes[0].GetShaderOutput("result")
        for port, role in (
            (input_port, "custom incoming role"), (output_port, "custom outgoing role"),
        ):
            with self.subTest(property=port.GetName()):
                metadata = port.GetMetadata()
                self.assertEqual(metadata[Sdr.PropertyMetadata.RenderType], role)
                self.assertEqual(metadata[UsdMdl.Metadata.Type], UsdMdl.Types.Float3)
        native_input, native_output = self._ports("vector.mdl", "test_vector_float3")
        self._assert_connection(True, input_port, native_output)
        self._assert_connection(True, native_input, output_port)

    def test_render_type_override_preserves_native_mdl_type(self):
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_type_override")
        normal_input = node.GetShaderInput("normal")
        native_output = node.GetShaderOutput(UsdMdl.Tokens.DefaultOutputPortName)
        metadata = normal_input.GetMetadata()
        self.assertEqual(metadata[Sdr.PropertyMetadata.RenderType], "normal")
        self.assertEqual(metadata[UsdMdl.Metadata.Type], UsdMdl.Types.Float3)
        self._assert_connection(True, normal_input, native_output)
        _, color_output = self._ports("color.mdl", "test_color")
        self._assert_connection(False, normal_input, color_output)


if __name__ == "__main__":
    run_unittest_main_exit_clean()
