#
# SPDX-License-Identifier: Apache-2.0
#

import math
import os
import sys
import unittest
from pathlib import Path

_ROOT = os.path.dirname(os.path.abspath(__file__))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from lib.mdl_test_base import MdlTestBase, UsdMdl, run_unittest_main_exit_clean
from pxr import Gf, Sdf, Sdr, Usd, UsdShade


class Type_Tests(MdlTestBase):
    def array_only_resources_supported(self):
        # SDK 2026.0.2 fixed enumeration of resources occurring only in arrays.
        # CTest supplies the package version, so a regression on a fixed SDK
        # must fail rather than selecting expectations from observed behavior.
        version = os.environ["USDMDL_TEST_MDL_SDK_VERSION"]
        return tuple(int(part) for part in version.split(".")) >= (2026, 0, 2)

    def resource_array_defaults(self, extension):
        if not self.array_only_resources_supported():
            return [Sdf.AssetPath(), Sdf.AssetPath()]
        return [Sdf.AssetPath(f"/resources/{name}.{extension}") for name in ("one", "two")]

    def assert_resource_array_bytes(self, node, extension):
        if self.array_only_resources_supported():
            values = node.GetShaderInput("param").GetDefaultValue()
            for value, name in zip(values, ("one", "two")):
                self.assert_resolved_asset_bytes(
                    value.resolvedPath, self.mdl_path / "resources" / f"{name}.{extension}")

    def test_textures_used_only_in_literal_array(self):
        node = self.validate_node("texture_array_only.mdl", "literal_array")
        prop = node.GetShaderInput("param")
        self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.AssetArray)
        self.assertEqual(prop.GetArraySize(), 2)
        self.assertFalse(prop.IsDynamicArray())
        values = prop.GetDefaultValue()
        self.assertEqual(len(values), 2)
        self.assertEqual([value.path for value in values],
                         [value.path for value in self.resource_array_defaults("png")])
        self.assert_resource_array_bytes(node, "png")
        if self.array_only_resources_supported():
            metadata = prop.GetMetadata()
            self.assertEqual(self.evalStr(metadata[UsdMdl.Metadata.TextureGamma]), [1, 2.2])
            self.assertEqual(self.evalStr(metadata[UsdMdl.Metadata.TextureSelector]), ["R", "G"])

    def test_content_defaults_survive_usd_authoring(self):
        node = self.validate_node("content_defaults.mdl", "content_defaults")
        expected = {
            "enabled": (Sdf.ValueTypeNames.Bool, True),
            "channels": (Sdf.ValueTypeNames.BoolArray, [True, False, True]),
            "identifier": (Sdf.ValueTypeNames.Int, 16777217),
            "precise": (Sdf.ValueTypeNames.Double, 0.12345678901234568),
            "precise_vector": (Sdf.ValueTypeNames.Double3,
                               Gf.Vec3d(0.12345678901234568, 0.23456789012345678, 0.34567890123456789)),
            "uv_scale": (Sdf.ValueTypeNames.Float2, Gf.Vec2f(-0.25, 8.0)),
            "emission": (Sdf.ValueTypeNames.Color3f, Gf.Vec3f(0.125, 0.5, 4.0)),
            "label": (Sdf.ValueTypeNames.String, 'red #50% "surface"'),
            "selection": (Sdf.ValueTypeNames.Int, 43),
            # Fixed arrays must remain arrays even at common vector lengths.
            "weights": (Sdf.ValueTypeNames.FloatArray, [0.25, 0.75]),
            "samples": (Sdf.ValueTypeNames.FloatArray, [1.0, 2.0, 3.0]),
            "indices": (Sdf.ValueTypeNames.IntArray, [1, 2, 3, 4]),
            # Flattened MDL matrices retain column order. Gf matrices store
            # that same element sequence as rows under the current contract.
            "transform": (Sdf.ValueTypeNames.FloatArray, [1, 2, 3, 4, 5, 6, 7, 8, 10]),
            "precise_transform": (Sdf.ValueTypeNames.Matrix3d, Gf.Matrix3d(1, 2, 3, 4, 5, 6, 7, 8, 10)),
        }
        self.assertEqual(set(node.GetShaderInputNames()), set(expected))
        shader = UsdShade.Shader.Define(self.stage, "/AuthoredDefaults")
        for name, (sdf_type, value) in expected.items():
            with self.subTest(parameter=name):
                prop = node.GetShaderInput(name)
                type_indicator = prop.GetTypeAsSdfType()
                self.assertTrue(type_indicator.HasSdfType(), name)
                self.assertEqual(type_indicator.GetSdfType(), sdf_type)
                actual = prop.GetDefaultValueAsSdfType()
                self.assertIsNotNone(actual)
                self.assertEqual(list(actual) if isinstance(value, list) else actual, value)
                if name in ("precise", "precise_vector"):
                    for default in (prop.GetDefaultValue(), prop.GetDefaultValueAsSdfType()):
                        self.assertIs(type(default), type(value))
                        if name == "precise":
                            self.assertEqual(default, value)
                        else:
                            for component, expected_component in zip(default, value):
                                self.assertTrue(math.isclose(component, expected_component,
                                                             rel_tol=1e-15, abs_tol=0.0))
                # Use Sdr's returned type to author the attribute; the independent
                # expectation above catches wrong roles, precision, and containers.
                self.assertTrue(shader.CreateInput(name, type_indicator.GetSdfType()).Set(actual))
        layer = Sdf.Layer.CreateAnonymous()
        self.assertTrue(layer.ImportFromString(self.stage.GetRootLayer().ExportToString()))
        reopened = Usd.Stage.Open(layer)
        shader = UsdShade.Shader(reopened.GetPrimAtPath("/AuthoredDefaults"))
        for name, (sdf_type, value) in expected.items():
            with self.subTest(reloaded_parameter=name):
                input_attr = shader.GetInput(name)
                self.assertTrue(input_attr, name)
                self.assertEqual(input_attr.GetTypeName(), sdf_type)
                actual = input_attr.Get()
                self.assertEqual(list(actual) if isinstance(value, list) else actual, value)
        self.validate_property_options(
            node.GetShaderInput("selection"), [("disabled", "-7"), ("automatic", "10"), ("authored", "43")])

    def test_required_parameters_have_no_fabricated_default(self):
        node = self.validate_node("content_defaults.mdl", "required_parameters")
        self.assertEqual(set(node.GetShaderInputNames()), {"amount", "uv_scale", "image"})
        expected_types = {
            "amount": Sdf.ValueTypeNames.Float,
            "uv_scale": Sdf.ValueTypeNames.Float2,
            "image": Sdf.ValueTypeNames.Asset,
        }
        for name, sdf_type in expected_types.items():
            prop = node.GetShaderInput(name)
            self.assertIsNone(prop.GetDefaultValue(), name)
            self.assertIsNone(prop.GetDefaultValueAsSdfType(), name)
            indicator = prop.GetTypeAsSdfType()
            self.assertTrue(indicator.HasSdfType(), name)
            self.assertEqual(indicator.GetSdfType(), sdf_type)

    def test_opaque_ports_retain_their_type_boundaries(self):
        for module, definition, sdr_type, has_sdf_type in (
            ("struct.mdl", "test_struct", Sdr.PropertyTypes.Struct, True),
            ("material.mdl", "test_material", Sdr.PropertyTypes.Terminal, True),
            ("array.mdl", "test_struct_array", Sdr.PropertyTypes.Struct, True),
            ("distribution.mdl", "test_bsdf", "token", False),
            ("distribution.mdl", "test_edf", "token", False),
            ("distribution.mdl", "test_vdf", "token", False),
            ("distribution.mdl", "test_hair_bsdf", "token", False),
        ):
            with self.subTest(definition=definition):
                node = self.validate_node(module, definition)
                output_names = node.GetShaderOutputNames()
                self.assertEqual(len(output_names), 1)
                for name in output_names:
                    prop = node.GetShaderOutput(name)
                    self.assertEqual(prop.GetType(), sdr_type)
                    indicator = prop.GetTypeAsSdfType()
                    self.assertEqual(indicator.HasSdfType(), has_sdf_type)
                    if has_sdf_type:
                        self.assertEqual(indicator.GetSdfType(), Sdf.ValueTypeNames.TokenArray
                                         if definition == "test_struct_array" else Sdf.ValueTypeNames.Token)
                    self.assertIsNone(prop.GetDefaultValue())

    def test_default_comparison_rejects_missing_values(self):
        # A missing scalar, component, or numeric annotation must fail the
        # same comparison used by all the content/default tests below.
        for actual, expected in (
            (None, 0.25),
            ([1.0, None], [1.0, 0.25]),
            ({"min": None}, {"min": 0.25}),
            ([1.0, 0.25], [1.0, None]),
        ):
            with self.subTest(actual=actual, expected=expected):
                with self.assertRaises(AssertionError):
                    self.validateValue(actual, expected)
        # None is a valid placeholder in expression metadata, provided both
        # the observed and expected entries are absent.
        self.validateValue([None, 0.25], [None, 0.25])

    def test_default_comparison_preserves_scalar_types(self):
        for actual, expected in ((True, 1), (1, True), ("1", 1), (1, "1"),
                                 ([1, False], [True, False])):
            with self.subTest(actual=actual, expected=expected):
                with self.assertRaises(AssertionError):
                    self.validateValue(actual, expected, is_default=True)
        for value in (True, 1, "1", "True", "[]", ["1", "True"]):
            self.validateValue(value, value, is_default=True)

    def test_metadata_preserves_array_element_shapes(self):
        # MDL arrays and matrices may have identical USD storage. The element
        # type and dimensions must remain distinguishable without CanConnectTo.
        for definition, element_type, sdf_type, component_count in (
            ("test_matrix_float2x3_array", "float2x3", Sdf.ValueTypeNames.FloatArray, 12),
            ("test_matrix_float3x2_array", "float3x2", Sdf.ValueTypeNames.FloatArray, 12),
            ("test_matrix_double2x3_array", "double2x3", Sdf.ValueTypeNames.DoubleArray, 12),
            ("test_matrix_double3x3_array", "double3x3", Sdf.ValueTypeNames.Matrix3dArray, 2),
            ("test_vector_bool3_array", "bool3", Sdf.ValueTypeNames.BoolArray, 6),
            ("test_color_array", "color", Sdf.ValueTypeNames.Color3fArray, 3),
        ):
            node = self.validate_node("array.mdl", definition)
            for prop in (node.GetShaderInput("param"), node.GetShaderOutput("out")):
                with self.subTest(definition=definition, property=prop.GetName()):
                    metadata = prop.GetMetadata()
                    self.assertEqual(metadata[Sdr.PropertyMetadata.RenderType], "[]")
                    self.assertEqual(metadata[UsdMdl.Metadata.ArrayElementType], element_type)
                    self.assertEqual(prop.GetArraySize(), component_count)
                    self.assertFalse(prop.IsDynamicArray())
                    indicator = prop.GetTypeAsSdfType()
                    self.assertTrue(indicator.HasSdfType())
                    self.assertEqual(indicator.GetSdfType(), sdf_type)

    def test_named_type_metadata_retains_package_identity(self):
        for definition, family, symbol, element_type in (
            ("struct_a", "struct", "StructA", None),
            ("struct_b", "struct", "StructB", None),
            ("enum_a", "enum", "EnumA", None),
            ("enum_b", "enum", "EnumB", None),
            ("struct_array", "[]", "StructA", "struct"),
            ("enum_array", "[]", "EnumA", "enum"),
        ):
            node = self.validate_node("type_checks/identity.mdl", definition)
            for prop in (node.GetShaderInput("param"), node.GetShaderOutput("out")):
                with self.subTest(definition=definition, property=prop.GetName()):
                    metadata = prop.GetMetadata()
                    self.assertEqual(metadata[Sdr.PropertyMetadata.RenderType], family)
                    # Exact qualified names; a suffix-only match loses package identity.
                    self.assertEqual(metadata[UsdMdl.Metadata.Symbol], "::type_checks::identity::" + symbol)
                    if element_type:
                        self.assertEqual(metadata[UsdMdl.Metadata.ArrayElementType], element_type)
                        self.assertEqual(prop.GetArraySize(), 2)

    def test_empty_and_singleton_array_identity(self):
        for definition, count, expected in (
            ("empty_array", 0, []), ("singleton_array", 1, [2.0]),
        ):
            node = self.validate_node("type_checks/identity.mdl", definition)
            for prop in (node.GetShaderInput("param"), node.GetShaderOutput("out")):
                with self.subTest(definition=definition, property=prop.GetName()):
                    # Sdr IsArray cannot distinguish fixed zero length from a
                    # scalar. MDL metadata and Sdf type must still identify it.
                    self.assertEqual(prop.GetMetadata()[Sdr.PropertyMetadata.RenderType], "[]")
                    self.assertEqual(prop.GetMetadata()[UsdMdl.Metadata.ArrayElementType], "float")
                    self.assertEqual(prop.GetArraySize(), count)
                    self.assertFalse(prop.IsDynamicArray())
                    indicator = prop.GetTypeAsSdfType()
                    self.assertTrue(indicator.HasSdfType())
                    self.assertEqual(indicator.GetSdfType(), Sdf.ValueTypeNames.FloatArray)
                    self.assertTrue(indicator.GetSdfType().isArray)
            self.assertEqual(list(node.GetShaderInput("param").GetDefaultValue()), expected)
            self.assertEqual(list(node.GetShaderInput("param").GetDefaultValueAsSdfType()), expected)

    def test_deferred_array_size_identity(self):
        node = self.validate_node("type_checks/identity.mdl", "deferred_matrix_array")
        for prop, element_type in (
            (node.GetShaderInput("param"), "float2x3"),
            (node.GetShaderInput("weights"), "float"),
            (node.GetShaderOutput("out"), "float2x3"),
        ):
            with self.subTest(property=prop.GetName()):
                metadata = prop.GetMetadata()
                self.assertEqual(metadata[Sdr.PropertyMetadata.RenderType], "[]")
                self.assertEqual(metadata[UsdMdl.Metadata.ArrayElementType], element_type)
                self.assertEqual(metadata[UsdMdl.Metadata.ArrayDeferredSizeSymbol], "N")
                self.assertTrue(prop.IsDynamicArray())
                self.assertEqual(prop.GetArraySize(), 0)

    def test_texture_array_defaults_open_real_assets(self):
        node = self.validate_node("texture_array_defaults.mdl", "literal_array")
        prop = node.GetShaderInput("param")
        indicator = prop.GetTypeAsSdfType()
        self.assertTrue(indicator.HasSdfType())
        self.assertEqual(indicator.GetSdfType(), Sdf.ValueTypeNames.AssetArray)
        self.assertTrue(prop.IsArray())
        self.assertEqual(prop.GetArraySize(), 2)
        self.assertFalse(prop.IsDynamicArray())
        values = prop.GetDefaultValue()
        self.assertIsNotNone(values)
        self.assertEqual(len(values), 2)
        self.assertEqual(prop.GetDefaultValueAsSdfType(), values)
        self.validateValue(prop.GetMetadata(), {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Texture2d,
            UsdMdl.Metadata.TextureGamma: [1.0, 2.2],
            UsdMdl.Metadata.TextureSelector: ["R", "G"],
        })
        self.assertNotEqual(
            (self.mdl_path / "resources/one.png").read_bytes(),
            (self.mdl_path / "resources/two.png").read_bytes())
        for asset, filename in zip(values, ("one.png", "two.png")):
            self.assertEqual(asset.path, "/resources/" + filename)
            expected_file = self.mdl_path / "resources" / filename
            self.assertEqual(Path(asset.resolvedPath), expected_file)
            self.assert_resolved_asset_bytes(asset.resolvedPath, expected_file)
        # Sdf authoring must preserve both ordered authored paths.
        # Resolved paths are runtime data, not serialized USD values.
        shader = UsdShade.Shader.Define(self.stage, "/TextureArray")
        self.assertTrue(shader.CreateInput("textures", indicator.GetSdfType()).Set(values))
        layer = Sdf.Layer.CreateAnonymous()
        self.assertTrue(layer.ImportFromString(self.stage.GetRootLayer().ExportToString()))
        reopened = Usd.Stage.Open(layer)
        attr = reopened.GetAttributeAtPath("/TextureArray.inputs:textures")
        self.assertEqual(attr.GetTypeName(), Sdf.ValueTypeNames.AssetArray)
        self.assertEqual([a.path for a in attr.Get()], ["/resources/one.png", "/resources/two.png"])

    def test_parameter_texture_array_has_no_constant_default(self):
        node = self.validate_node("texture_array_defaults.mdl", "parameter_array")
        prop = node.GetShaderInput("param")
        self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.AssetArray)
        self.assertEqual(prop.GetArraySize(), 2)
        self.assertIsNone(prop.GetDefaultValue())
        self.assertIsNone(prop.GetDefaultValueAsSdfType())
        self.assertEqual(prop.GetMetadata()[UsdMdl.Metadata.ExpressionKind], UsdMdl.ExpressionKinds.Call)
        # The scalar defaults still contain the original distinct textures.
        # The array may use overrides of these inputs at execution time, so
        # copying the scalar defaults into it would invent a constant value.
        for name, filename in (("first", "one.png"), ("second", "two.png")):
            asset = node.GetShaderInput(name).GetDefaultValue()
            self.assertIsInstance(asset, Sdf.AssetPath)
            self.assertEqual(asset.path, "/resources/" + filename)
            self.assert_resolved_asset_bytes(asset.resolvedPath, self.mdl_path / "resources" / filename)

    def test_type_alias(self):
        mdl_module = "alias.mdl"
        subidentifier = "test_alias"
        value_type_name = Sdf.ValueTypeNames.Int
        default_value = 1234

        metadata = {Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Int, UsdMdl.Metadata.Symbol: "::alias::number"}

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_bsdfMeasurement(self):
        mdl_module = "bsdfMeasurement.mdl"
        subidentifier = "test_bsdfMeasurement"
        value_type_name = Sdf.ValueTypeNames.Asset
        # The compact fixture is intentionally not a decodable MBSDF payload;
        # the invalid resource must not expose a fabricated authoring path.
        default_value = Sdf.AssetPath()

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Bsdf_Measurement,
            Sdr.PropertyMetadata.IsAssetIdentifier: "1",
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata, checkOutputs=False)

    def test_type_bsdfMeasurement_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_bsdfMeasurement_array"
        value_type_name = Sdf.ValueTypeNames.AssetArray
        # Invalid resources preserve the array slots without fabricated paths.
        default_value = [Sdf.AssetPath(), Sdf.AssetPath()]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Bsdf_Measurement,
            Sdr.PropertyMetadata.IsAssetIdentifier: "1",
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata, checkOutputs=False)

    def test_valid_bsdf_measurements(self):
        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Bsdf_Measurement,
            Sdr.PropertyMetadata.IsAssetIdentifier: "1",
        }
        for name in ("valid_one", "valid_two"):
            with self.subTest(name=name):
                node = self.validate_mdl(
                    "bsdfMeasurementValid.mdl", name, Sdf.ValueTypeNames.Asset,
                    Sdf.AssetPath(f"/resources/{name}.mbsdf"), metadata,
                    checkOutputs=False)
                asset = node.GetShaderInput("param").GetDefaultValue()
                expected = self.mdl_path / "resources" / f"{name}.mbsdf"
                self.assertEqual(Path(asset.resolvedPath), expected)
                self.assert_resolved_asset_bytes(asset.resolvedPath, expected)

    def test_valid_bsdf_measurement_array(self):
        names = ("valid_one", "valid_two")
        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Bsdf_Measurement,
            Sdr.PropertyMetadata.IsAssetIdentifier: "1",
        }
        node = self.validate_mdl(
            "bsdfMeasurementValid.mdl", "valid_array", Sdf.ValueTypeNames.AssetArray,
            [Sdf.AssetPath(f"/resources/{name}.mbsdf") for name in names], metadata,
            checkOutputs=False)
        prop = node.GetShaderInput("param")
        self.assertEqual(prop.GetArraySize(), 2)
        self.assertFalse(prop.IsDynamicArray())
        values = prop.GetDefaultValue()
        self.assertEqual(len(values), 2)
        for asset, name in zip(values, names):
            expected = self.mdl_path / "resources" / f"{name}.mbsdf"
            self.assertEqual(Path(asset.resolvedPath), expected)
            self.assert_resolved_asset_bytes(asset.resolvedPath, expected)

    def test_type_bool(self):
        mdl_module = "bool.mdl"
        subidentifier = "test_bool"
        value_type_name = Sdf.ValueTypeNames.Bool
        default_value = True

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Bool,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_bool_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_bool_array"
        value_type_name = Sdf.ValueTypeNames.BoolArray
        default_value = [True, False, True, False]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Bool,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_color(self):
        mdl_module = "color.mdl"
        subidentifier = "test_color"
        value_type_name = Sdf.ValueTypeNames.Color3f
        default_value = (0.1, 0.2, 0.3)

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Color,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_color_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_color_array"
        value_type_name = Sdf.ValueTypeNames.Color3fArray
        default_value = [(0.1, 0.2, 0.3), (0.4, 0.5, 0.6), (0.7, 0.8, 0.9)]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Color,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_double(self):
        mdl_module = "double.mdl"
        subidentifier = "test_double"
        value_type_name = Sdf.ValueTypeNames.Double
        default_value = 0.1

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Double,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_double_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_double_array"
        value_type_name = Sdf.ValueTypeNames.DoubleArray
        default_value = [1.1, 2.2, 3.3, 4.4, 5.5, 6.6]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Double,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_dynamic_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_dynamic_array"
        value_type_name = Sdf.ValueTypeNames.Color3fArray
        default_value = [(1.0, 1.0, 1.0)]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Color,
            UsdMdl.Metadata.ArrayDeferredSizeSymbol: "N",
            # Presence-flag: OpenUSD's Sdr canonicalizes the value differently
            # across versions ('' vs '1'). validateValue() treats this key as
            # presence-only, so any non-None value passes.
            Sdr.PropertyMetadata.IsDynamicArray: "",
        }

        node = self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata, checkOutputs=False)
        prop = node.GetShaderInput("param")
        self.assertTrue(prop.IsDynamicArray())
        self.assertEqual(prop.GetArraySize(), 0)

    def test_type_enum(self):
        mdl_module = "enum.mdl"
        subidentifier = "test_enum"
        value_type_name = Sdf.ValueTypeNames.Int
        default_value = 3

        metadata = {Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Enum, UsdMdl.Metadata.Symbol: "::tex::wrap_mode"}

        options = [("wrap_clamp", "0"), ("wrap_repeat", "1"), ("wrap_mirrored_repeat", "2"), ("wrap_clip", "3")]

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata, options=options)

    def test_type_enum_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_enum_array"
        value_type_name = Sdf.ValueTypeNames.IntArray
        default_value = [3, 2, 0]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Enum,
            UsdMdl.Metadata.Symbol: "::tex::wrap_mode",
        }

        options = [("wrap_clamp", "0"), ("wrap_repeat", "1"), ("wrap_mirrored_repeat", "2"), ("wrap_clip", "3")]

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata, options=options)

    def test_type_float(self):
        mdl_module = "float.mdl"
        subidentifier = "test_float"
        value_type_name = Sdf.ValueTypeNames.Float
        default_value = 0.1

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Float,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_float_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_float_array"
        value_type_name = Sdf.ValueTypeNames.FloatArray
        default_value = [1.1, 2.2, 3.3, 4.4, 5.5]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Float,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_int(self):
        mdl_module = "int.mdl"
        subidentifier = "test_int"
        value_type_name = Sdf.ValueTypeNames.Int
        default_value = 1

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Int,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_int_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_int_array"
        value_type_name = Sdf.ValueTypeNames.IntArray
        default_value = [1, 2, 3, 4]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Int,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_lightProfile(self):
        mdl_module = "lightProfile.mdl"
        subidentifier = "test_lightProfile"
        value_type_name = Sdf.ValueTypeNames.Asset
        default_value = Sdf.AssetPath("/resources/one.ies")

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Light_Profile,
            Sdr.PropertyMetadata.IsAssetIdentifier: "1",
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata, checkOutputs=False)

    def test_type_lightProfile_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_lightProfile_array"
        value_type_name = Sdf.ValueTypeNames.AssetArray
        default_value = self.resource_array_defaults("ies")

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Light_Profile,
            Sdr.PropertyMetadata.IsAssetIdentifier: "1",
        }

        node = self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata, checkOutputs=False)
        self.assert_resource_array_bytes(node, "ies")

    def test_type_material(self):
        mdl_module = "material.mdl"
        subidentifier = "test_material"
        value_type_name = Sdr.PropertyTypes.Terminal
        default_value = None

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Struct,
            UsdMdl.Metadata.StructType: UsdMdl.StructTypes.Material,
            UsdMdl.Metadata.Symbol: "::material",
        }

        input_metadata = {
            UsdMdl.Metadata.ExpressionKind: UsdMdl.ExpressionKinds.Call,
            UsdMdl.Metadata.ExpressionValue: "mdl::material::diffuse(color,float,float3)",
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata, input_metadata)

    def test_type_matrix_2x2d(self):
        mdl_module = "matrix.mdl"
        subidentifier = "test_matrix_double2x2"
        value_type_name = Sdf.ValueTypeNames.Matrix2d
        default_value = ((0.1, 0.2), (0.3, 0.4))

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Double2x2,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_2x2d_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_matrix_double2x2_array"
        value_type_name = Sdf.ValueTypeNames.Matrix2dArray
        default_value = [((0.1, 0.2), (0.3, 0.4)), ((1.1, 1.2), (1.3, 1.4))]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Double2x2,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_2x3d(self):
        mdl_module = "matrix.mdl"
        subidentifier = "test_matrix_double2x3"
        value_type_name = Sdf.ValueTypeNames.DoubleArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Double2x3,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_2x3d_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_matrix_double2x3_array"
        value_type_name = Sdf.ValueTypeNames.DoubleArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Double2x3,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_2x4d(self):
        mdl_module = "matrix.mdl"
        subidentifier = "test_matrix_double2x4"
        value_type_name = Sdf.ValueTypeNames.DoubleArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Double2x4,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_2x4d_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_matrix_double2x4_array"
        value_type_name = Sdf.ValueTypeNames.DoubleArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Double2x4,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_3x2d(self):
        mdl_module = "matrix.mdl"
        subidentifier = "test_matrix_double3x2"
        value_type_name = Sdf.ValueTypeNames.DoubleArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Double3x2,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_3x2d_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_matrix_double3x2_array"
        value_type_name = Sdf.ValueTypeNames.DoubleArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Double3x2,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_3x3d(self):
        mdl_module = "matrix.mdl"
        subidentifier = "test_matrix_double3x3"
        value_type_name = Sdf.ValueTypeNames.Matrix3d
        default_value = ((0.1, 0.2, 0.3), (0.4, 0.5, 0.6), (0.7, 0.8, 0.9))

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Double3x3,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_3x3d_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_matrix_double3x3_array"
        value_type_name = Sdf.ValueTypeNames.Matrix3dArray
        default_value = [
            ((0.1, 0.2, 0.3), (0.4, 0.5, 0.6), (0.7, 0.8, 0.9)),
            ((1.1, 1.2, 1.3), (1.4, 1.5, 1.6), (1.7, 1.8, 1.9)),
        ]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Double3x3,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_3x4d(self):
        mdl_module = "matrix.mdl"
        subidentifier = "test_matrix_double3x4"
        value_type_name = Sdf.ValueTypeNames.DoubleArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Double3x4,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_3x4d_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_matrix_double3x4_array"
        value_type_name = Sdf.ValueTypeNames.DoubleArray
        default_value = [
            0.1,
            0.2,
            0.3,
            0.4,
            0.5,
            0.6,
            0.7,
            0.8,
            0.9,
            1.0,
            1.1,
            1.2,
            1.1,
            1.2,
            1.3,
            1.4,
            1.5,
            1.6,
            1.7,
            1.8,
            1.9,
            2.0,
            2.1,
            2.2,
        ]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Double3x4,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_4x2d(self):
        mdl_module = "matrix.mdl"
        subidentifier = "test_matrix_double4x2"
        value_type_name = Sdf.ValueTypeNames.DoubleArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Double4x2,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_4x2d_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_matrix_double4x2_array"
        value_type_name = Sdf.ValueTypeNames.DoubleArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Double4x2,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_4x3d(self):
        mdl_module = "matrix.mdl"
        subidentifier = "test_matrix_double4x3"
        value_type_name = Sdf.ValueTypeNames.DoubleArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Double4x3,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_4x3d_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_matrix_double4x3_array"
        value_type_name = Sdf.ValueTypeNames.DoubleArray
        default_value = [
            0.1,
            0.2,
            0.3,
            0.4,
            0.5,
            0.6,
            0.7,
            0.8,
            0.9,
            1.0,
            1.1,
            1.2,
            1.1,
            1.2,
            1.3,
            1.4,
            1.5,
            1.6,
            1.7,
            1.8,
            1.9,
            2.0,
            2.1,
            2.2,
        ]
        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Double4x3,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_4x4d(self):
        mdl_module = "matrix.mdl"
        subidentifier = "test_matrix_double4x4"
        value_type_name = Sdf.ValueTypeNames.Matrix4d
        default_value = ((0.1, 0.2, 0.3, 0.4), (0.5, 0.6, 0.7, 0.8), (0.9, 1.0, 1.1, 1.2), (1.3, 1.4, 1.5, 1.6))

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Double4x4,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_4x4d_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_matrix_double4x4_array"
        value_type_name = Sdf.ValueTypeNames.Matrix4dArray
        default_value = [
            ((0.1, 0.2, 0.3, 0.4), (0.5, 0.6, 0.7, 0.8), (0.9, 1.0, 1.1, 1.2), (1.3, 1.4, 1.5, 1.6)),
            ((1.1, 1.2, 1.3, 1.4), (1.5, 1.6, 1.7, 1.8), (1.9, 2.0, 2.1, 2.2), (2.3, 2.4, 2.5, 2.6)),
        ]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Double4x4,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_2x2f(self):
        mdl_module = "matrix.mdl"
        subidentifier = "test_matrix_float2x2"
        value_type_name = Sdf.ValueTypeNames.FloatArray
        default_value = [0.1, 0.2, 0.3, 0.4]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Float2x2,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_2x2f_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_matrix_float2x2_array"
        value_type_name = Sdf.ValueTypeNames.FloatArray
        default_value = [0.1, 0.2, 0.3, 0.4, 1.1, 1.2, 1.3, 1.4]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Float2x2,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_2x3f(self):
        mdl_module = "matrix.mdl"
        subidentifier = "test_matrix_float2x3"
        value_type_name = Sdf.ValueTypeNames.FloatArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Float2x3,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_2x3f_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_matrix_float2x3_array"
        value_type_name = Sdf.ValueTypeNames.FloatArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Float2x3,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_2x4f(self):
        mdl_module = "matrix.mdl"
        subidentifier = "test_matrix_float2x4"
        value_type_name = Sdf.ValueTypeNames.FloatArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Float2x4,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_2x4f_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_matrix_float2x4_array"
        value_type_name = Sdf.ValueTypeNames.FloatArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Float2x4,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_3x2f(self):
        mdl_module = "matrix.mdl"
        subidentifier = "test_matrix_float3x2"
        value_type_name = Sdf.ValueTypeNames.FloatArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Float3x2,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_3x2f_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_matrix_float3x2_array"
        value_type_name = Sdf.ValueTypeNames.FloatArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Float3x2,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_3x3f(self):
        mdl_module = "matrix.mdl"
        subidentifier = "test_matrix_float3x3"
        value_type_name = Sdf.ValueTypeNames.FloatArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Float3x3,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_3x3f_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_matrix_float3x3_array"
        value_type_name = Sdf.ValueTypeNames.FloatArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Float3x3,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_3x4f(self):
        mdl_module = "matrix.mdl"
        subidentifier = "test_matrix_float3x4"
        value_type_name = Sdf.ValueTypeNames.FloatArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Float3x4,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_3x4_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_matrix_float3x4_array"
        value_type_name = Sdf.ValueTypeNames.FloatArray
        default_value = [
            0.1,
            0.2,
            0.3,
            0.4,
            0.5,
            0.6,
            0.7,
            0.8,
            0.9,
            1.0,
            1.1,
            1.2,
            1.1,
            1.2,
            1.3,
            1.4,
            1.5,
            1.6,
            1.7,
            1.8,
            1.9,
            2.0,
            2.1,
            2.2,
        ]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Float3x4,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_4x2f(self):
        mdl_module = "matrix.mdl"
        subidentifier = "test_matrix_float4x2"
        value_type_name = Sdf.ValueTypeNames.FloatArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Float4x2,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_4x2_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_matrix_float4x2_array"
        value_type_name = Sdf.ValueTypeNames.FloatArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Float4x2,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_4x3f(self):
        mdl_module = "matrix.mdl"
        subidentifier = "test_matrix_float4x3"
        value_type_name = Sdf.ValueTypeNames.FloatArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Float4x3,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_4x3_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_matrix_float4x3_array"
        value_type_name = Sdf.ValueTypeNames.FloatArray
        default_value = [
            0.1,
            0.2,
            0.3,
            0.4,
            0.5,
            0.6,
            0.7,
            0.8,
            0.9,
            1.0,
            1.1,
            1.2,
            1.1,
            1.2,
            1.3,
            1.4,
            1.5,
            1.6,
            1.7,
            1.8,
            1.9,
            2.0,
            2.1,
            2.2,
        ]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Float4x3,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_4x4f(self):
        mdl_module = "matrix.mdl"
        subidentifier = "test_matrix_float4x4"
        value_type_name = Sdf.ValueTypeNames.FloatArray
        default_value = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Float4x4,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_matrix_4x4f_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_matrix_float4x4_array"
        value_type_name = Sdf.ValueTypeNames.FloatArray
        default_value = [
            0.1,
            0.2,
            0.3,
            0.4,
            0.5,
            0.6,
            0.7,
            0.8,
            0.9,
            1.0,
            1.1,
            1.2,
            1.3,
            1.4,
            1.5,
            1.6,
            1.1,
            1.2,
            1.3,
            1.4,
            1.5,
            1.6,
            1.7,
            1.8,
            1.9,
            2.0,
            2.1,
            2.2,
            2.3,
            2.4,
            2.5,
            2.6,
        ]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Float4x4,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_string(self):
        mdl_module = "string.mdl"
        subidentifier = "test_string"
        value_type_name = Sdf.ValueTypeNames.String
        default_value = "aStringValue"

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.String,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_string_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_string_array"
        value_type_name = Sdf.ValueTypeNames.StringArray
        default_value = ["my", "mind", "is", "going", ""]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.String,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_struct(self):
        mdl_module = "struct.mdl"
        subidentifier = "test_struct"
        value_type_name = Sdr.PropertyTypes.Struct
        default_value = None

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Struct,
            UsdMdl.Metadata.StructType: UsdMdl.StructTypes.User,
            UsdMdl.Metadata.Symbol: "::struct::TestStruct",
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_struct_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_struct_array"
        value_type_name = Sdr.PropertyTypes.Struct
        default_value = None

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Struct,
            UsdMdl.Metadata.Symbol: "::array::SimpleStruct",
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_texture2D(self):
        mdl_module = "texture.mdl"
        subidentifier = "test_texture2D"
        value_type_name = Sdf.ValueTypeNames.Asset
        default_value = Sdf.AssetPath("/resources/one.png")

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Texture2d,
            Sdr.PropertyMetadata.IsAssetIdentifier: "1",
        }

        input_metadata = {UsdMdl.Metadata.TextureGamma: "1"}

        node = self.validate_mdl(
            mdl_module, subidentifier, value_type_name, default_value, metadata, input_metadata, checkOutputs=False
        )
        asset_path = node.GetShaderInput("param").GetDefaultValue()
        self.assertEqual(asset_path.path, "/resources/one.png")
        self.assertEqual(
            Path(asset_path.resolvedPath),
            self.mdl_path / "resources" / "one.png",
        )
        self.assert_resolved_asset_bytes(asset_path.resolvedPath, self.mdl_path / "resources/one.png")

    def test_type_texture2D_udim(self):
        """UDIM tile-set: <UDIM> marker is preserved in the asset path default."""
        mdl_module = "texture.mdl"
        subidentifier = "test_texture2D_udim"
        value_type_name = Sdf.ValueTypeNames.Asset
        default_value = Sdf.AssetPath("/resources/tile_<UDIM>.png")

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Texture2d,
            Sdr.PropertyMetadata.IsAssetIdentifier: "1",
        }
        input_metadata = {UsdMdl.Metadata.TextureGamma: "1"}

        node = self.validate_mdl(
            mdl_module, subidentifier, value_type_name, default_value, metadata, input_metadata, checkOutputs=False
        )
        asset_path = node.GetShaderInput("param").GetDefaultValue()
        self.assertEqual(asset_path.path, "/resources/tile_<UDIM>.png")
        self.assertEqual(
            Path(asset_path.resolvedPath),
            self.mdl_path / "resources" / "tile_<UDIM>.png",
        )
        self.assert_resolved_asset_bytes(
            asset_path.resolvedPath.replace("<UDIM>", "1001"), self.mdl_path / "resources/tile_1001.png")

    def test_type_texture2D_animated(self):
        """Animated frame sequence: <####> marker is preserved in the asset path default."""
        mdl_module = "texture.mdl"
        subidentifier = "test_texture2D_animated"
        value_type_name = Sdf.ValueTypeNames.Asset
        default_value = Sdf.AssetPath("/resources/frame_<####>.png")

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Texture2d,
            Sdr.PropertyMetadata.IsAssetIdentifier: "1",
        }
        input_metadata = {UsdMdl.Metadata.TextureGamma: "1"}

        node = self.validate_mdl(
            mdl_module, subidentifier, value_type_name, default_value, metadata, input_metadata, checkOutputs=False
        )
        asset_path = node.GetShaderInput("param").GetDefaultValue()
        self.assertEqual(asset_path.path, "/resources/frame_<####>.png")
        self.assertEqual(
            Path(asset_path.resolvedPath),
            self.mdl_path / "resources" / "frame_<####>.png",
        )
        self.assert_resolved_asset_bytes(
            asset_path.resolvedPath.replace("<####>", "1"), self.mdl_path / "resources/frame_1.png")

    def test_type_texture2D_animated_udim(self):
        """Combined frame and UDIM markers preserve directly openable tile identifiers."""
        mdl_module = "texture.mdl"
        subidentifier = "test_texture2D_animated_udim"
        value_type_name = Sdf.ValueTypeNames.Asset
        default_value = Sdf.AssetPath("/resources/tiled_resource.<##>.<UDIM>.png")

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Texture2d,
            Sdr.PropertyMetadata.IsAssetIdentifier: "1",
        }
        input_metadata = {UsdMdl.Metadata.TextureGamma: "1"}

        node = self.validate_mdl(
            mdl_module,
            subidentifier,
            value_type_name,
            default_value,
            metadata,
            input_metadata,
            checkOutputs=False,
        )
        asset_path = node.GetShaderInput("param").GetDefaultValue()
        self.assertEqual(asset_path.path, "/resources/tiled_resource.<##>.<UDIM>.png")
        self.assertEqual(
            Path(asset_path.resolvedPath),
            self.mdl_path / "resources" / "tiled_resource.<##>.<UDIM>.png",
        )
        for frame, tile in (("00", "1001"), ("00", "1002"), ("00", "1011"), ("02", "1012")):
            self.assert_resolved_asset_bytes(
                asset_path.resolvedPath.replace("<##>", frame).replace("<UDIM>", tile),
                self.mdl_path / f"resources/tiled_resource.{frame}.{tile}.png")

    def test_type_texture2D_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_texture2D_array"
        value_type_name = Sdf.ValueTypeNames.AssetArray
        default_value = self.resource_array_defaults("png")

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Texture2d,
            Sdr.PropertyMetadata.IsAssetIdentifier: "1",
        }

        node = self.validate_mdl(
            mdl_module, subidentifier, value_type_name, default_value, metadata, checkOutputs=False
        )
        self.assert_resource_array_bytes(node, "png")

    # texture_3d (.vdb) load is unsupported by the MDL SDK: no image
    # plugin ships for that format.  See docs/ar-resolver.md
    # "Limitations".  The .mdl fixture uses an empty texture_3d()
    # constructor; these tests check type mapping and empty defaults only.
    def test_type_texture3D_empty(self):
        mdl_module = "texture.mdl"
        subidentifier = "test_texture3D"
        value_type_name = Sdf.ValueTypeNames.Asset
        default_value = Sdf.AssetPath()

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Texture3d,
            Sdr.PropertyMetadata.IsAssetIdentifier: "1",
        }

        self.validate_mdl(
            mdl_module, subidentifier, value_type_name, default_value, metadata, checkOutputs=False
        )

    def test_type_texture3D_array_empty(self):
        mdl_module = "array.mdl"
        subidentifier = "test_texture3D_array"
        value_type_name = Sdf.ValueTypeNames.AssetArray
        default_value = [Sdf.AssetPath(), Sdf.AssetPath()]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Texture3d,
            Sdr.PropertyMetadata.IsAssetIdentifier: "1",
        }

        self.validate_mdl(
            mdl_module, subidentifier, value_type_name, default_value, metadata, checkOutputs=False
        )

    def test_type_textureCube(self):
        mdl_module = "texture.mdl"
        subidentifier = "test_textureCube"
        value_type_name = Sdf.ValueTypeNames.Asset
        default_value = Sdf.AssetPath("/resources/one.cube.png")

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.TextureCube,
            Sdr.PropertyMetadata.IsAssetIdentifier: "1",
        }

        input_metadata = {
            UsdMdl.Metadata.TextureGamma: "1",
        }

        self.validate_mdl(
            mdl_module, subidentifier, value_type_name, default_value, metadata, input_metadata, checkOutputs=False
        )

    def test_type_textureCube_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_textureCube_array"
        value_type_name = Sdf.ValueTypeNames.AssetArray
        default_value = self.resource_array_defaults("cube.png")

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.TextureCube,
            Sdr.PropertyMetadata.IsAssetIdentifier: "1",
        }

        node = self.validate_mdl(
            mdl_module, subidentifier, value_type_name, default_value, metadata, checkOutputs=False
        )
        self.assert_resource_array_bytes(node, "cube.png")

    # texture_ptex load is unsupported by the MDL SDK: no Ptex image
    # plugin ships, and even MDL SDK examples explicitly note that
    # texture_ptex resolution is not supported.  The .mdl fixture
    # uses an empty texture_ptex() constructor so the module still
    # loads; these tests check type mapping and empty defaults only.
    def test_type_texturePtex_empty(self):
        mdl_module = "texture.mdl"
        subidentifier = "test_texturePtex"
        value_type_name = Sdf.ValueTypeNames.Asset
        default_value = Sdf.AssetPath()

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.TexturePtex,
            Sdr.PropertyMetadata.IsAssetIdentifier: "1",
        }

        self.validate_mdl(
            mdl_module, subidentifier, value_type_name, default_value, metadata, checkOutputs=False
        )

    def test_type_texturePtex_array_empty(self):
        mdl_module = "array.mdl"
        subidentifier = "test_texturePtex_array"
        value_type_name = Sdf.ValueTypeNames.AssetArray
        default_value = [Sdf.AssetPath(), Sdf.AssetPath()]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.TexturePtex,
            Sdr.PropertyMetadata.IsAssetIdentifier: "1",
        }

        self.validate_mdl(
            mdl_module, subidentifier, value_type_name, default_value, metadata, checkOutputs=False
        )

    def test_type_modifier_uniform(self):
        mdl_module = "type_modifiers.mdl"
        subidentifier = "test_type_modifier_uniform"
        value_type_name = Sdf.ValueTypeNames.Int
        default_value = 1

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Int,
            UsdMdl.Metadata.Modifier: UsdMdl.TypeModifiers.Uniform,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_modifier_varying(self):
        mdl_module = "type_modifiers.mdl"
        subidentifier = "test_type_modifier_varying"
        value_type_name = Sdf.ValueTypeNames.Int
        default_value = 1

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Int,
            UsdMdl.Metadata.Modifier: UsdMdl.TypeModifiers.Varying,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_bool2(self):
        mdl_module = "vector.mdl"
        subidentifier = "test_vector_bool2"
        value_type_name = Sdf.ValueTypeNames.BoolArray
        default_value = [True, False]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Bool2,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_bool2_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_vector_bool2_array"
        value_type_name = Sdf.ValueTypeNames.BoolArray
        default_value = [True, False, False, True]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Bool2,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_bool3(self):
        mdl_module = "vector.mdl"
        subidentifier = "test_vector_bool3"
        value_type_name = Sdf.ValueTypeNames.BoolArray
        default_value = [True, False, True]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Bool3,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_bool3_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_vector_bool3_array"
        value_type_name = Sdf.ValueTypeNames.BoolArray
        default_value = [True, False, True, False, True, False]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Bool3,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_bool4(self):
        mdl_module = "vector.mdl"
        subidentifier = "test_vector_bool4"
        value_type_name = Sdf.ValueTypeNames.BoolArray
        default_value = [True, False, True, False]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Bool4,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_bool4_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_vector_bool4_array"
        value_type_name = Sdf.ValueTypeNames.BoolArray
        default_value = [True, False, True, False, False, True, False, True]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Bool4,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_double2(self):
        mdl_module = "vector.mdl"
        subidentifier = "test_vector_double2"
        value_type_name = Sdf.ValueTypeNames.Double2
        default_value = (0.1, 0.2)

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Double2,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_double2_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_vector_double2_array"
        value_type_name = Sdf.ValueTypeNames.Double2Array
        default_value = [(0.1, 0.2), (1.1, 1.2)]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Double2,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_double3(self):
        mdl_module = "vector.mdl"
        subidentifier = "test_vector_double3"
        value_type_name = Sdf.ValueTypeNames.Double3
        default_value = (0.1, 0.2, 0.3)

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Double3,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_double3_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_vector_double3_array"
        value_type_name = Sdf.ValueTypeNames.Double3Array
        default_value = [(0.1, 0.2, 0.3), (1.1, 1.2, 1.3)]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Double3,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_double4(self):
        mdl_module = "vector.mdl"
        subidentifier = "test_vector_double4"
        value_type_name = Sdf.ValueTypeNames.Double4
        default_value = (0.1, 0.2, 0.3, 0.4)

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Double4,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_double4_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_vector_double4_array"
        value_type_name = Sdf.ValueTypeNames.Double4Array
        default_value = [(0.1, 0.2, 0.3, 0.4), (1.1, 1.2, 1.3, 1.4)]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Double4,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_float2(self):
        mdl_module = "vector.mdl"
        subidentifier = "test_vector_float2"
        value_type_name = Sdf.ValueTypeNames.Float2
        default_value = (0.1, 0.2)

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Float2,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_float2_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_vector_float2_array"
        value_type_name = Sdf.ValueTypeNames.Float2Array
        default_value = [(0.1, 0.2), (1.1, 1.2)]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Float2,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_float3(self):
        mdl_module = "vector.mdl"
        subidentifier = "test_vector_float3"
        value_type_name = Sdf.ValueTypeNames.Float3
        default_value = (0.1, 0.2, 0.3)

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Float3,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_float3_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_vector_float3_array"
        value_type_name = Sdf.ValueTypeNames.Float3Array
        default_value = [(0.1, 0.2, 0.3), (1.1, 1.2, 1.3)]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Float3,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_float4(self):
        mdl_module = "vector.mdl"
        subidentifier = "test_vector_float4"
        value_type_name = Sdf.ValueTypeNames.Float4
        default_value = (0.1, 0.2, 0.3, 0.4)

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Float4,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_float4_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_vector_float4_array"
        value_type_name = Sdf.ValueTypeNames.Float4Array
        default_value = [(0.1, 0.2, 0.3, 0.4), (1.1, 1.2, 1.3, 1.4)]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Float4,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_int2(self):
        mdl_module = "vector.mdl"
        subidentifier = "test_vector_int2"
        value_type_name = Sdf.ValueTypeNames.Int2
        default_value = (1, 2)

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Int2,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_int2_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_vector_int2_array"
        value_type_name = Sdf.ValueTypeNames.Int2Array
        default_value = [(1, 2), (2, 1)]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Int2,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_int3(self):
        mdl_module = "vector.mdl"
        subidentifier = "test_vector_int3"
        value_type_name = Sdf.ValueTypeNames.Int3
        default_value = (1, 2, 3)

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Int3,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_int3_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_vector_int3_array"
        value_type_name = Sdf.ValueTypeNames.Int3Array
        default_value = [(1, 2, 3), (3, 2, 1)]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Int3,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_int4(self):
        mdl_module = "vector.mdl"
        subidentifier = "test_vector_int4"
        value_type_name = Sdf.ValueTypeNames.Int4
        default_value = (1, 2, 3, 4)

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Int4,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)

    def test_type_vector_int4_array(self):
        mdl_module = "array.mdl"
        subidentifier = "test_vector_int4_array"
        value_type_name = Sdf.ValueTypeNames.Int4Array
        default_value = [(1, 2, 3, 4), (4, 3, 2, 1)]

        metadata = {
            Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Array,
            UsdMdl.Metadata.ArrayElementType: UsdMdl.Types.Int4,
        }

        self.validate_mdl(mdl_module, subidentifier, value_type_name, default_value, metadata)


if __name__ == "__main__":
    run_unittest_main_exit_clean()
