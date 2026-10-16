#
# SPDX-License-Identifier: Apache-2.0
#

import os
import subprocess
import sys

_ROOT = os.path.dirname(os.path.abspath(__file__))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from lib.mdl_test_base import MdlTestBase, UsdMdl, run_unittest_main_exit_clean
from pxr import Gf, Sdf, Sdr


def _record(name, arguments):
    """An expected occurrence; callers supply the complete SDK signature."""
    return {"name": name, "arguments": arguments}


class AnnotationTests(MdlTestBase):
    def _records(self, subject):
        raw = self.evalStr(subject.GetMetadata()[UsdMdl.Metadata.Annotation])
        records = raw["annotations"]
        self.assertIsInstance(records, list)
        for record in records:
            self.assertEqual(set(record), {"name", "arguments"})
            self.assertIsInstance(record["name"], str)
            self.assertIsInstance(record["arguments"], dict)
        return records

    def validate_hints(self, mdl_module, subidentifier, metadata_to_check):
        sdr_node = self.validate_node(mdl_module, subidentifier)
        if self.debug:
            self.debug_print(sdr_node)
        for inputName, v in metadata_to_check.items():
            sdr_property = sdr_node.GetShaderInput(inputName)
            self.assertIsNotNone(sdr_property)
            sdr_property_hints = sdr_property.GetHints()
            self.validateValue(sdr_property_hints, v)

    def validate_input_annotations(self, mdl_module, subidentifier, metadata_to_check):
        sdr_node = self.validate_node(mdl_module, subidentifier)
        if self.debug:
            self.debug_print(sdr_node)
        for inputName, v in metadata_to_check.items():
            sdr_property = sdr_node.GetShaderInput(inputName)
            self.assertIsNotNone(sdr_property)
            sdr_property_metadata = sdr_property.GetMetadata()
            self.validateValue(sdr_property_metadata, v)

    def validate_outputs(self, mdl_module, subidentifier, expected_outputs):
        sdr_node = self.validate_node(mdl_module, subidentifier)
        output_names = sdr_node.GetShaderOutputNames()
        self.assertIsNotNone(output_names)
        self.assertTrue(len(expected_outputs) == len(output_names))
        for output_name in output_names:
            expected = expected_outputs[output_name]
            self.assertIsNotNone(expected)
            sdr_property = sdr_node.GetShaderOutput(output_name)
            self.assertIsNotNone(sdr_property)
            self.validate_property(output_name, sdr_node, expected["value_type_name"], False, expected["metadata"])

    def test_annotation_range(self):
        mdl_module = "annotation.mdl"
        subidentifier = "test_annotation_range"
        hints = {
            "pIntSoft": {"soft_range": {"min": 0, "max": 10}},
            "pColorHard": {"hard_range": {"min": (0.1, 0.2, 0.3), "max": (0.4, 0.5, 0.6)}},
        }
        self.validate_hints(mdl_module, subidentifier, hints)

    def test_annotation_hidden(self):
        mdl_module = "annotation.mdl"
        subidentifier = "test_annotation_hidden"
        hints = {
            "pInt": {"hidden": 1},
        }
        self.validate_hints(mdl_module, subidentifier, hints)

    def test_annotation_array(self):
        mdl_module = "annotation.mdl"
        subidentifier = "test_annotation_array"
        metadata = {
            "pInt": {UsdMdl.Metadata.Annotation: {"annotations": [
                _record("::anno::key_words(string[N])", {"words": ["one", "two"]}),
            ]}},
        }
        self.validate_input_annotations(mdl_module, subidentifier, metadata)

    def test_annotation_tuple(self):
        mdl_module = "annotation.mdl"
        subidentifier = "test_annotation_tuple"
        metadata = {
            "pInt": {
                UsdMdl.Metadata.Annotation: {
                    "annotations": [_record("::anno::version(int,int,int,string)", {
                        "major": 1,
                        "minor": 2,
                        "patch": 3,
                        "prerelease": "I can't\\tbreak metadata",
                    })]
                }
            },
        }
        self.validate_input_annotations(mdl_module, subidentifier, metadata)

    def test_annotation_string(self):
        def _validate_page(sdr_node, input_name, expected):
            sdr_property = sdr_node.GetShaderInput(input_name)
            self.assertIsNotNone(sdr_property)
            page = sdr_property.GetPage()
            self.assertIsNotNone(page)
            self.assertTrue(page == expected)

        mdl_module = "annotation.mdl"
        subidentifier = "test_annotation_string"
        sdr_node = self.validate_node(mdl_module, subidentifier)
        sdr_property = sdr_node.GetShaderInput("pInt1")
        self.assertIsNotNone(sdr_property)
        label = sdr_property.GetLabel()
        self.assertIsNotNone(label)
        self.assertTrue(label == "Parameter")
        _validate_page(sdr_node, "pInt2", "A")
        _validate_page(sdr_node, "pInt3", "AA:BB")
        _validate_page(sdr_node, "pInt4", "AAA:BBB:CCC")

    def test_annotation_return(self):
        mdl_module = "annotation.mdl"
        subidentifier = "test_annotation_return"
        sdr_node = self.validate_node(mdl_module, subidentifier)
        sdr_property = sdr_node.GetShaderOutput(UsdMdl.Tokens.DefaultOutputPortName)
        self.assertIsNotNone(sdr_property)
        helpStr = sdr_property.GetHelp()
        self.assertIsNotNone(helpStr)
        self.assertTrue(helpStr == "Return type int")

    def test_annotation_function(self):
        mdl_module = "annotation.mdl"
        subidentifier = "test_annotation_function"
        metadata = {"help": "I am a MDL function", UsdMdl.Metadata.Modifier: UsdMdl.TypeModifiers.Uniform}
        sdr_node = self.validate_node(mdl_module, subidentifier)
        sdr_node_metadata = sdr_node.GetMetadata()
        self.validateValue(sdr_node_metadata, metadata)

    def test_annotation_module(self):
        mdl_module = "annotation.mdl"
        subidentifier = "test_annotation_module"
        metadata = {
            UsdMdl.Metadata.Annotation: {
                UsdMdl.Metadata.AnnotationModule: {
                    "annotations": [_record("::anno::description(string)",
                                            {"description": "I am a MDL module"})],
                }
            }
        }
        sdr_node = self.validate_node(mdl_module, subidentifier)
        sdr_node_metadata = sdr_node.GetMetadata()
        self.validateValue(sdr_node_metadata, metadata)

    def test_annotation_struct_value_and_fields(self):
        mdl_module = "annotation.mdl"
        subidentifier = "test_annotation_struct_value_and_fields"
        expected_outputs = {
            "i": {
                "value_type_name": Sdf.ValueTypeNames.Int,
                "metadata": {
                    Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Int,
                },
            },
            "f": {
                "value_type_name": Sdf.ValueTypeNames.Float,
                "metadata": {
                    Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Float,
                },
            },
            UsdMdl.Tokens.DefaultOutputPortName: {
                "value_type_name": Sdr.PropertyTypes.Struct,
                "metadata": {
                    Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Struct,
                    UsdMdl.Metadata.StructType: UsdMdl.StructTypes.User,
                    UsdMdl.Metadata.Symbol: "::annotation::TestStruct",
                },
            },
        }
        self.validate_outputs(mdl_module, subidentifier, expected_outputs)

    def test_annotation_struct_fields_only(self):
        mdl_module = "annotation.mdl"
        subidentifier = "test_annotation_struct_fields_only"
        expected_outputs = {
            "i": {
                "value_type_name": Sdf.ValueTypeNames.Int,
                "metadata": {
                    Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Int,
                },
            },
            "f": {
                "value_type_name": Sdf.ValueTypeNames.Float,
                "metadata": {
                    Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Float,
                },
            },
        }
        self.validate_outputs(mdl_module, subidentifier, expected_outputs)

    def test_annotation_struct_value_only(self):
        mdl_module = "annotation.mdl"
        subidentifier = "test_annotation_struct_value_only"
        expected_outputs = {
            UsdMdl.Tokens.DefaultOutputPortName: {
                "value_type_name": Sdr.PropertyTypes.Struct,
                "metadata": {
                    Sdr.PropertyMetadata.RenderType: UsdMdl.Types.Struct,
                    UsdMdl.Metadata.StructType: UsdMdl.StructTypes.User,
                    UsdMdl.Metadata.Symbol: "::annotation::TestStruct",
                },
            }
        }
        self.validate_outputs(mdl_module, subidentifier, expected_outputs)

    def test_annotation_enum(self):
        mdl_module = "annotation.mdl"
        subidentifier = "test_annotation_enum"
        metadata = {
            "param": {
                UsdMdl.Metadata.Annotation: {
                    UsdMdl.Metadata.AnnotationType: {"annotations": [
                        _record("::anno::description(string)", {"description": "I am a MDL enum"}),
                    ]},
                    UsdMdl.Metadata.AnnotationValue: {
                        "0": {"annotations": [
                            _record("::anno::description(string)", {"description": "I am value low"}),
                        ]},
                        "1": {"annotations": [
                            _record("::anno::description(string)", {"description": "I am value medium"}),
                        ]},
                        "2": {"annotations": [
                            _record("::anno::description(string)", {"description": "I am value high"}),
                        ]},
                    },
                }
            },
        }
        self.validate_input_annotations(mdl_module, subidentifier, metadata)

    def test_annotation_struct_type(self):
        mdl_module = "annotation.mdl"
        subidentifier = "test_annotation_struct_type"
        metadata = {
            "pStruct": {
                UsdMdl.Metadata.Annotation: {
                    UsdMdl.Metadata.AnnotationType: {"annotations": [
                        _record("::anno::description(string)", {"description": "I am a MDL struct"}),
                    ]},
                }
            },
        }
        self.validate_input_annotations(mdl_module, subidentifier, metadata)

    # ---- enable_if is an opaque MDL string, not a USD visibility expression ----

    def _check_enable_if(self, subidentifier, param_name, expected_expr):
        sdr_node = self.validate_node("enable_if.mdl", subidentifier)
        prop = sdr_node.GetShaderInput(param_name)
        self.assertIsNotNone(prop, f"missing input {param_name}")
        hints = prop.GetHints()
        self.assertIn("enable_if", hints)
        self.assertEqual(hints["enable_if"], expected_expr)
        self.assertNotIn("shownIf", prop.GetMetadata())
        self.assertEqual(prop.GetShownIf(), "")

    def test_enable_if_bool_eq(self):
        self._check_enable_if("test_enable_if_bool_eq",
                              "shadow_intensity", "enable_shadows == true")

    def test_enable_if_int_gt(self):
        self._check_enable_if("test_enable_if_int_gt",
                              "layer2_weight", "num_layers > 1")

    def test_enable_if_negation(self):
        self._check_enable_if("test_enable_if_negation",
                              "value", "!is_locked")

    def test_enable_if_and(self):
        self._check_enable_if("test_enable_if_and",
                              "tex_scale", "use_texture == true && mode > 0")

    def test_enable_if_or(self):
        self._check_enable_if("test_enable_if_or",
                              "shared_value", "override_a == true || override_b == true")

    def test_enable_if_neq(self):
        self._check_enable_if("test_enable_if_neq",
                              "quality", "render_mode != 0")

    def test_enable_if_mdl_expressions(self):
        # Compare against authored strings without parsing or evaluating them.
        # Calls, enum symbols and whitespace must retain their MDL spelling.
        for definition, expected in (
            ("test_enable_if_texture", "::tex::texture_isvalid(texture)"),
            ("test_enable_if_math", "::math::average(emissive_factor)>0.0"),
            ("test_enable_if_enum", "alpha_mode==mask"),
            ("test_enable_if_whitespace", "  enabled  &&\n\t(count > 1)  "),
        ):
            with self.subTest(definition=definition):
                self._check_enable_if(definition, "weight", expected)

    def test_no_enable_if_when_unannotated(self):
        node = self.validate_node("enable_if.mdl", "test_enable_if_bool_eq")
        prop = node.GetShaderInput("enable_shadows")
        self.assertNotIn("enable_if", prop.GetHints())
        self.assertNotIn("shownIf", prop.GetMetadata())
        self.assertEqual(prop.GetShownIf(), "")

    def test_enable_if_load_has_no_warnings(self):
        # A fresh process captures native Tf warnings too, which Python's
        # redirect_stderr would miss. Disable discovery to isolate this module.
        code = """
import sys
from pxr import Sdf
import UsdMdl
try:
    nodes = UsdMdl.AddModuleToRegistry(Sdf.AssetPath(sys.argv[1]))
    assert any(node.GetMetadata().get("mdl:subIdentifier") == "test_enable_if_math"
               for node in nodes), "enable_if fixture did not load"
finally:
    UsdMdl.Shutdown()
"""
        env = dict(os.environ, USD_MDL_DISCOVER_ON_STARTUP="0")
        result = subprocess.run(
            [sys.executable, "-c", code, str(self.mdl_path / "enable_if.mdl")],
            env=env, capture_output=True, text=True, timeout=60)
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, output)
        # SDK/runtime informational messages are allowed; warnings are not.
        self.assertNotRegex(output, r"(?im)^(?:warning|error|fatal(?: error)?)\s*:")

    # ---- Direct Sdr metadata annotations ----

    def test_direct_property_metadata(self):
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_property")
        prop = node.GetShaderInput("param")
        self.assertIsNotNone(prop)
        self.assertEqual(str(prop.GetLabel()), "Parameter label")
        self.assertEqual(prop.GetHelp(), "Parameter help")
        self.assertEqual(str(prop.GetPage()), "Direct:Page")
        self.assertEqual(str(prop.GetWidget()), "slider")
        self.assertEqual(prop.GetMetadata()[Sdr.PropertyMetadata.Role], "none")
        self.assertEqual(prop.GetDefaultValue(), 1)
        self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.Int)

    def test_direct_property_metadata_overrides_standard_annotations(self):
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_property_precedence")
        for name, suffix in (("direct_first", "first"), ("direct_last", "last")):
            with self.subTest(parameter=name):
                prop = node.GetShaderInput(name)
                self.assertIsNotNone(prop)
                self.assertEqual(str(prop.GetLabel()), "Direct " + suffix)
                self.assertEqual(prop.GetHelp(), "Direct " + suffix + " help")
                self.assertEqual(str(prop.GetPage()), "Direct:" + suffix.capitalize())
                self.assertEqual(prop.GetMetadata()[Sdr.PropertyMetadata.Hints], "direct-" + suffix)
        empty = node.GetShaderInput("empty_label")
        self.assertEqual(empty.GetMetadata()[Sdr.PropertyMetadata.Label], "")
        self.assertEqual(str(empty.GetLabel()), "")

    def test_direct_node_metadata_overrides_standard_annotations(self):
        for suffix in ("first", "last"):
            with self.subTest(order=suffix):
                node = self.validate_node("sdr_metadata.mdl", "test_metadata_node_direct_" + suffix)
                self.assertEqual(str(node.GetLabel()), "Direct node " + suffix)
                self.assertEqual(node.GetHelp(), "Direct node " + suffix + " help")
                self.assertEqual(str(node.GetCategory()), "utilities")
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_node_direct_first")
        metadata = node.GetMetadata()
        self.assertEqual(metadata[Sdr.NodeMetadata.Departments], "lookdev|lighting")
        self.assertEqual(metadata[Sdr.NodeMetadata.Pages], "Main|Advanced")
        self.assertEqual(metadata[Sdr.NodeMetadata.OpenPages], "Main")
        self.assertEqual([str(value) for value in node.GetDepartments()], ["lookdev", "lighting"])
        self.assertEqual([str(value) for value in node.GetOpenPages()], ["Main"])

    def test_direct_metadata_overrides_converter_defaults(self):
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_type_override")
        prop = node.GetShaderInput("normal")
        self.assertIsNotNone(prop)
        metadata = prop.GetMetadata()
        self.assertEqual(metadata[Sdr.PropertyMetadata.SdrUsdDefinitionType], "normal3f")
        self.assertEqual(metadata[Sdr.PropertyMetadata.RenderType], "normal")
        indicator = prop.GetTypeAsSdfType()
        self.assertTrue(indicator.HasSdfType())
        self.assertEqual(indicator.GetSdfType(), Sdf.ValueTypeNames.Normal3f)
        self.assertEqual(prop.GetType(), Sdr.PropertyTypes.Normal)
        self.assertIsInstance(prop.GetDefaultValue(), Gf.Vec3f)
        self.assertEqual(prop.GetDefaultValue(), Gf.Vec3f(1.0, 2.0, 3.0))
        self.assertEqual(prop.GetDefaultValueAsSdfType(), Gf.Vec3f(1.0, 2.0, 3.0))

    def test_direct_usd_roles_use_canonical_sdr_types(self):
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_roles")
        roles = (
            ("color_value", Sdr.PropertyTypes.Color, Sdf.ValueTypeNames.Color3f,
             Gf.Vec3f(0.1, 0.2, 0.3)),
            ("normal_value", Sdr.PropertyTypes.Normal, Sdf.ValueTypeNames.Normal3f,
             Gf.Vec3f(0.0, 0.0, 1.0)),
            ("point_value", Sdr.PropertyTypes.Point, Sdf.ValueTypeNames.Point3f,
             Gf.Vec3f(1.0, 2.0, 3.0)),
            ("vector_value", Sdr.PropertyTypes.Vector, Sdf.ValueTypeNames.Vector3f,
             Gf.Vec3f(4.0, 5.0, 6.0)),
            ("color_as_vector", Sdr.PropertyTypes.Vector, Sdf.ValueTypeNames.Vector3f,
             Gf.Vec3f(0.25, 0.5, 0.75)),
        )
        outputs = []
        for role in ("color", "normal", "point", "vector"):
            output_node = self.validate_node("sdr_metadata.mdl", "test_metadata_" + role + "_output")
            outputs.append(output_node.GetShaderOutput(UsdMdl.Tokens.DefaultOutputPortName))
        float2 = self.validate_node("sdr_metadata.mdl", "test_metadata_float2_output")
        float2_output = float2.GetShaderOutput(UsdMdl.Tokens.DefaultOutputPortName)
        for name, sdr_type, sdf_type, default in roles:
            with self.subTest(parameter=name):
                prop = node.GetShaderInput(name)
                self.assertEqual(prop.GetType(), sdr_type)
                self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), sdf_type)
                self.assertEqual(prop.GetDefaultValue(), default)
                self.assertEqual(prop.GetDefaultValueAsSdfType(), default)
                # OpenUSD accepts connections across its four float3 roles.
                for output in outputs:
                    self.assertTrue(prop.CanConnectTo(output))
                    self.assertTrue(output.CanConnectTo(prop))
                self.assertFalse(prop.CanConnectTo(float2_output))
                self.assertFalse(prop.CanConnectTo(node.GetShaderInput("normal_value")))

    def test_direct_token_enum_preserves_symbolic_defaults_and_options(self):
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_tokens")
        prop = node.GetShaderInput("enum_token")
        self.assertEqual(prop.GetType(), Sdr.PropertyTypes.String)
        self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.Token)
        self.assertIs(type(prop.GetDefaultValue()), str)
        # The SDK canonicalizes the compiled duplicate-code constant to middle.
        self.assertEqual(prop.GetDefaultValue(), "middle")
        self.assertEqual(prop.GetDefaultValueAsSdfType(), "middle")
        self.assertEqual(list(prop.GetOptions()), [
            ("negative", "negative"), ("middle", "middle"),
            ("last", "last"), ("alias", "alias")])
        self.assertIn("SparseMode", prop.GetMetadata()[UsdMdl.Metadata.Symbol])
        ordinary = node.GetShaderInput("enum_integer")
        self.assertEqual(ordinary.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.Int)
        self.assertIs(type(ordinary.GetDefaultValue()), int)
        self.assertEqual(ordinary.GetDefaultValue(), 8)
        self.assertEqual(list(ordinary.GetOptions()), [
            ("negative", "-3"), ("middle", "8"), ("last", "42"), ("alias", "8")])

    def test_direct_token_string_defaults(self):
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_tokens")
        for name, expected in (("string_token", "auto"), ("empty_token", "")):
            with self.subTest(parameter=name):
                prop = node.GetShaderInput(name)
                self.assertEqual(prop.GetType(), Sdr.PropertyTypes.String)
                self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.Token)
                self.assertIs(type(prop.GetDefaultValue()), str)
                self.assertEqual(prop.GetDefaultValue(), expected)
                self.assertEqual(prop.GetDefaultValueAsSdfType(), expected)
                self.assertEqual(list(prop.GetOptions()), [])

    def test_direct_token_does_not_fabricate_unavailable_defaults(self):
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_token_defaults")
        for name, has_default in (("required", "false"), ("referenced", "true")):
            with self.subTest(parameter=name):
                prop = node.GetShaderInput(name)
                self.assertEqual(prop.GetType(), Sdr.PropertyTypes.String)
                self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.Token)
                self.assertIsNone(prop.GetDefaultValue())
                self.assertIsNone(prop.GetDefaultValueAsSdfType())
                self.assertEqual(prop.GetMetadata()[UsdMdl.Metadata.HasDefault], has_default)
        required = node.GetShaderInput("required").GetMetadata()
        self.assertNotIn(UsdMdl.Metadata.ExpressionKind, required)
        referenced = node.GetShaderInput("referenced").GetMetadata()
        self.assertEqual(referenced[UsdMdl.Metadata.ExpressionKind], UsdMdl.ExpressionKinds.Parameter)
        self.assertEqual(referenced[UsdMdl.Metadata.ExpressionValue], "1")

    def test_direct_struct_field_metadata_stays_on_each_field(self):
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_fields")
        self.assertEqual(set(node.GetShaderOutputNames()), {"normal", "rgb", "alpha", "untouched"})
        for name, label, sdr_type, sdf_type in (
            ("normal", "Normal field", Sdr.PropertyTypes.Normal, Sdf.ValueTypeNames.Normal3f),
            ("rgb", "RGB field", Sdr.PropertyTypes.Color, Sdf.ValueTypeNames.Color3f),
            ("alpha", "Alpha field", "float", Sdf.ValueTypeNames.Float),
        ):
            with self.subTest(field=name):
                prop = node.GetShaderOutput(name)
                self.assertEqual(str(prop.GetLabel()), label)
                self.assertEqual(prop.GetType(), sdr_type)
                self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), sdf_type)
        self.assertNotIn(Sdr.PropertyMetadata.Label, node.GetMetadata())
        self.assertEqual(node.GetShaderOutput("alpha").GetMetadata()[
            Sdr.PropertyMetadata.SdrUsdDefinitionType], "float")

    def test_identical_direct_metadata_across_namespaces_is_accepted(self):
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_namespace_identical")
        self.assertEqual(str(node.GetShaderInput("local_last").GetLabel()), "Shared property")
        self.assertEqual(str(node.GetShaderInput("other_last").GetLabel()), "Shared property")
        self.assertEqual(str(node.GetLabel()), "Shared node")
        for name, enabled in (("local_false_last", False), ("other_false_last", False),
                              ("marker_and_bool", True)):
            with self.subTest(parameter=name):
                prop = node.GetShaderInput(name)
                self.assertEqual(prop.GetMetadata()[Sdr.PropertyMetadata.Connectable],
                                 "1" if enabled else "0")
                self.assertEqual(prop.IsConnectable(), enabled)
        other_last = self.validate_node("sdr_metadata.mdl", "test_metadata_namespace_node_other_last")
        self.assertEqual(str(other_last.GetLabel()), "Shared node")

    def test_conflicting_direct_metadata_rejects_affected_nodes(self):
        # Native diagnostics are emitted outside Python's stderr redirection.
        # A clean subprocess also keeps rejected registry entries isolated.
        code = """
import sys
from pxr import Sdf, Tf, Usd, UsdShade
import UsdMdl
nodes = []
stage = None
shader = None
try:
    asset = Sdf.AssetPath(sys.argv[1])
    nodes = UsdMdl.AddModuleToRegistry(asset)
    names = {node.GetMetadata().get("mdl:subIdentifier") for node in nodes}
    assert names == {"valid_metadata_control"}, names
    assert nodes[0].GetShaderInput("value").GetLabel() == "Control"
    assert nodes[0].GetShaderInput("value").GetDefaultValue() == 0.5
    stage = Usd.Stage.CreateInMemory()
    rejected = (
        "property_conflict_local_first", "property_conflict_other_first",
        "node_conflict_local_first", "node_conflict_other_first",
        "output_conflict_local_first", "output_conflict_other_first",
        "connectable_conflict_local_first", "connectable_conflict_other_first",
        "property_conflict_repeated", "node_conflict_repeated", "output_conflict_repeated",
        "property_conflict_overloaded", "node_conflict_overloaded", "output_conflict_overloaded",
        "property_conflict_connectable_repeated", "property_conflict_connectable_overloaded",
        "property_conflict_before_ambiguous",
    )
    for index, definition in enumerate(rejected):
        shader = UsdShade.Shader.Define(stage, "/Rejected_" + str(index))
        shader.SetSourceAsset(asset, "mdl")
        shader.SetSourceAssetSubIdentifier(definition, "mdl")
        try:
            rejected_node = shader.GetShaderNodeForSourceType("mdl")
        except Tf.ErrorException as error:
            assert "returned null" in str(error), str(error)
        else:
            assert rejected_node is None, definition
finally:
    shader = None
    stage = None
    nodes.clear()
    UsdMdl.Shutdown()
"""
        env = dict(os.environ, USD_MDL_DISCOVER_ON_STARTUP="0",
                   USD_MDL_LOAD_USD_PREVIEW_SURFACE="0")
        result = subprocess.run(
            [sys.executable, "-c", code, str(self.mdl_path / "sdr_metadata_conflicts.mdl")],
            env=env, capture_output=True, text=True, timeout=60)
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, output)
        self.assertIn("Conflicting Sdr metadata annotations", output)
        for annotation in ("SDR__label", "SDR__connectable"):
            self.assertIn("::sdr_metadata_conflicts::" + annotation, output)
            self.assertIn("::sdr_metadata_other::" + annotation, output)
        self.assertGreaterEqual(output.count("for field 'label'"), 13, output)
        self.assertGreaterEqual(output.count("for field 'connectable'"), 4, output)

    def test_whole_return_annotations_do_not_leak_to_struct_fields(self):
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_fields_return_annotations")
        self.assertEqual(set(node.GetShaderOutputNames()), {
            "normal", "rgb", "alpha", "untouched", str(UsdMdl.Tokens.DefaultOutputPortName)})
        whole = node.GetShaderOutput(UsdMdl.Tokens.DefaultOutputPortName)
        self.assertEqual(str(whole.GetLabel()), "Whole return")
        self.assertEqual(whole.GetHelp(), "Whole return help")
        self.assertEqual(whole.GetMetadata()[Sdr.PropertyMetadata.SdrUsdDefinitionType], "token")
        for name, label, sdf_type in (
            ("normal", "Normal field", Sdf.ValueTypeNames.Normal3f),
            ("rgb", "RGB field", Sdf.ValueTypeNames.Color3f),
            ("alpha", "Alpha field", Sdf.ValueTypeNames.Float),
        ):
            with self.subTest(field=name):
                prop = node.GetShaderOutput(name)
                self.assertEqual(str(prop.GetLabel()), label)
                self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), sdf_type)
                self.assertNotIn(Sdr.PropertyMetadata.Help, prop.GetMetadata())
        untouched = node.GetShaderOutput("untouched")
        self.assertNotIn(Sdr.PropertyMetadata.Label, untouched.GetMetadata())
        self.assertNotIn(Sdr.PropertyMetadata.Help, untouched.GetMetadata())
        self.assertEqual(untouched.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.Float)

    def test_invalid_usd_type_overrides_reject_native_nodes(self):
        code = """
import sys
from pxr import Sdf
import UsdMdl
try:
    nodes = UsdMdl.AddModuleToRegistry(Sdf.AssetPath(sys.argv[1]))
    assert not nodes, 'incompatible type overrides produced registry nodes'
finally:
    UsdMdl.Shutdown()
"""
        env = dict(os.environ, USD_MDL_DISCOVER_ON_STARTUP="0")
        result = subprocess.run(
            [sys.executable, "-c", code, str(self.mdl_path / "sdr_metadata_invalid.mdl")],
            env=env, capture_output=True, text=True, timeout=60)
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, output)
        for requested in ("normal3q", "normal3f", "float3[]", "token"):
            self.assertIn("sdrUsdDefinitionType '" + requested + "'", output)

    def test_direct_metadata_value_conversion(self):
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_values")
        expected = {
            "integer_value": "12",
            "boolean_value": "0",
            "float_value": "1.25",
            "array_value": "red|green|blue",
            "empty_array": "",
        }
        for name, value in expected.items():
            with self.subTest(parameter=name):
                prop = node.GetShaderInput(name)
                self.assertIsNotNone(prop)
                self.assertEqual(prop.GetMetadata()[Sdr.PropertyMetadata.Role], value)
        vector = node.GetShaderInput("vector_value")
        self.assertEqual(self.evalStr(vector.GetMetadata()[Sdr.PropertyMetadata.Role]), (1, 2, 3))
        numeric_array = node.GetShaderInput("numeric_array")
        self.assertEqual(self.evalStr(numeric_array.GetMetadata()[Sdr.PropertyMetadata.Role]), [4, 5, 6])

    def test_direct_connectable_metadata(self):
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_connectable")
        for name, enabled in (("unannotated", True), ("disabled", False),
                              ("enabled", True), ("marker", True)):
            with self.subTest(parameter=name):
                prop = node.GetShaderInput(name)
                self.assertIsNotNone(prop)
                self.assertEqual(prop.GetMetadata()[Sdr.PropertyMetadata.Connectable], "1" if enabled else "0")
                self.assertEqual(prop.IsConnectable(), enabled)

    def test_identical_repeated_annotations_and_overloads_are_preserved(self):
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_repeated_annotations")
        for name in ("bool_last", "int_last", "repeated_false"):
            with self.subTest(parameter=name):
                prop = node.GetShaderInput(name)
                self.assertEqual(prop.GetMetadata()[Sdr.PropertyMetadata.Connectable], "0")
                self.assertFalse(prop.IsConnectable())
        enabled = node.GetShaderInput("repeated_true")
        self.assertEqual(enabled.GetMetadata()[Sdr.PropertyMetadata.Connectable], "1")
        self.assertTrue(enabled.IsConnectable())
        self.assertEqual(str(node.GetShaderInput("number_last").GetLabel()), "4.25")
        self.assertEqual(str(node.GetShaderInput("string_last").GetLabel()), "2.5")
        self.assertEqual(self._records(node.GetShaderInput("bool_last")), [
            _record("::sdr_metadata::SDR__connectable(int)", {"value": 0}),
            _record("::sdr_metadata::SDR__connectable(bool)", {"enabled": 0}),
        ])
        self.assertEqual(self._records(node.GetShaderInput("repeated_false")), [
            _record("::sdr_metadata::SDR__connectable(bool)", {"enabled": 0}),
            _record("::sdr_metadata::SDR__connectable(bool)", {"enabled": 0}),
        ])
        self.assertEqual(self._records(node.GetShaderInput("number_last")), [
            _record("::sdr_metadata::SDR__label(string)", {"value": "4.25"}),
            _record("::sdr_metadata::SDR__label(float)", {"number": 4.25}),
        ])

    def test_multiargument_occurrence_does_not_hide_valid_scalar(self):
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_repeated_annotations")
        ambiguous = node.GetShaderInput("ambiguous_last")
        self.assertEqual(str(ambiguous.GetLabel()), "Earlier direct label")
        self.assertEqual(self._records(ambiguous), [
            _record("::anno::display_name(string)", {"name": "Standard fallback"}),
            _record("::sdr_metadata::SDR__label(string)", {"value": "Earlier direct label"}),
            _record("::sdr_metadata::SDR__label(int,int)", {"first": 10, "second": 20}),
        ])
        ambiguous = node.GetShaderInput("ambiguous_first")
        self.assertEqual(str(ambiguous.GetLabel()), "Later direct label")
        self.assertEqual(self._records(ambiguous), [
            _record("::sdr_metadata::SDR__label(int,int)", {"first": 30, "second": 40}),
            _record("::sdr_metadata::SDR__label(string)", {"value": "Later direct label"}),
            _record("::anno::display_name(string)", {"name": "Standard fallback"}),
        ])

    def test_raw_annotations_preserve_all_occurrences_and_full_signatures(self):
        node = self.validate_node("sdr_metadata_occurrences.mdl", "test_raw_occurrences")
        self.assertEqual(str(node.GetLabel()), "Node label")
        self.assertEqual(self._records(node), [
            _record("::sdr_metadata_other::note(string)", {"text": "Node first"}),
            _record("::sdr_metadata_other::marker()", {}),
            _record("::sdr_metadata_other::note(string)", {"text": "Node second"}),
            _record("::sdr_metadata_other::SDR__label(string)", {"text": "Node label"}),
            _record("::sdr_metadata_other::SDR__label(string)", {"text": "Node label"}),
        ])
        prop = node.GetShaderInput("value")
        self.assertEqual(str(prop.GetLabel()), "Promoted label")
        self.assertEqual(self._records(prop), [
            _record("::sdr_metadata_other::note(string)", {"text": "Input first"}),
            _record("::sdr_metadata_other::marker()", {}),
            _record("::sdr_metadata_other::note(int,string)", {"index": 7, "text": "Input overload"}),
            _record("::sdr_metadata_other::note(string)", {"text": "Input second"}),
            _record("::anno::display_name(string)", {"name": "SDK label"}),
            _record("::sdr_metadata_other::SDR__label(string)", {"text": "Promoted label"}),
            _record("::sdr_metadata_other::SDR__label(string)", {"text": "Promoted label"}),
        ])
        output = node.GetShaderOutput(UsdMdl.Tokens.DefaultOutputPortName)
        self.assertEqual(self._records(output), [
            _record("::sdr_metadata_other::note(string)", {"text": "Output first"}),
            _record("::sdr_metadata_other::marker()", {}),
            _record("::sdr_metadata_other::note(string)", {"text": "Output second"}),
        ])

    def test_nested_scopes_preserve_occurrences_without_promotion(self):
        node = self.validate_node("sdr_metadata_occurrences.mdl", "test_nested_occurrences")
        raw = self.evalStr(node.GetMetadata()[UsdMdl.Metadata.Annotation])
        self.assertEqual(raw[UsdMdl.Metadata.AnnotationModule], {"annotations": [
            _record("::sdr_metadata_other::note(string)", {"text": "Module first"}),
            _record("::sdr_metadata_other::marker()", {}),
            _record("::sdr_metadata_other::note(string)", {"text": "Module second"}),
            _record("::sdr_metadata_other::SDR__label(string)", {"text": "Module label stays raw"}),
        ]})
        self.assertNotIn(Sdr.NodeMetadata.Label, node.GetMetadata())
        mode = node.GetShaderInput("mode")
        raw = self.evalStr(mode.GetMetadata()[UsdMdl.Metadata.Annotation])
        self.assertEqual(raw[UsdMdl.Metadata.AnnotationType], {"annotations": [
            _record("::sdr_metadata_other::note(string)", {"text": "Enum first"}),
            _record("::sdr_metadata_other::marker()", {}),
            _record("::sdr_metadata_other::note(string)", {"text": "Enum second"}),
        ]})
        # Value scopes use declaration index, not enum integer values 2 and 17.
        self.assertEqual(raw[UsdMdl.Metadata.AnnotationValue], {
            "0": {"annotations": [
                _record("::sdr_metadata_other::note(string)", {"text": "Low first"}),
                _record("::sdr_metadata_other::note(string)", {"text": "Low second"}),
                _record("::sdr_metadata_other::marker()", {}),
            ]},
            "1": {"annotations": [_record("::sdr_metadata_other::marker()", {})]},
        })
        structure = node.GetShaderInput("structure")
        raw = self.evalStr(structure.GetMetadata()[UsdMdl.Metadata.Annotation])
        self.assertEqual(raw[UsdMdl.Metadata.AnnotationType], {"annotations": [
            _record("::sdr_metadata_other::note(string)", {"text": "Struct first"}),
            _record("::sdr_metadata_other::marker()", {}),
            _record("::sdr_metadata_other::note(string)", {"text": "Struct second"}),
        ]})

    def test_usage_accumulates_in_order_and_direct_hints_take_precedence(self):
        node = self.validate_node("sdr_metadata_occurrences.mdl", "test_usage_occurrences")
        accumulated = node.GetShaderInput("accumulated")
        self.assertEqual(accumulated.GetMetadata()[Sdr.PropertyMetadata.Hints],
                         "texture|coordinate|normal")
        self.assertEqual(self._records(accumulated), [
            _record("::anno::usage(string)", {"hint": "texture"}),
            _record("::anno::usage(string)", {"hint": "coordinate"}),
            _record("::anno::usage(string)", {"hint": "texture"}),
            _record("::anno::usage(string)", {"hint": "normal"}),
        ])
        for name, expected in (("direct_first", "explicit-first"),
                               ("direct_last", "explicit-last")):
            with self.subTest(parameter=name):
                prop = node.GetShaderInput(name)
                self.assertEqual(prop.GetMetadata()[Sdr.PropertyMetadata.Hints], expected)
                usages = [record for record in self._records(prop)
                          if record["name"] == "::anno::usage(string)"]
                self.assertEqual(usages, [
                    _record("::anno::usage(string)", {"hint": "fallback-first"}),
                    _record("::anno::usage(string)", {"hint": "fallback-second"}),
                ])

    def test_sdk_scalar_fallbacks_and_hints_use_last_complete_occurrence(self):
        node = self.validate_node("sdr_metadata_legacy_occurrences.mdl", "legacy_scalar_occurrences")
        self.assertEqual(str(node.GetLabel()), "Last node label")
        self.assertEqual(node.GetHelp(), "Last node help")
        prop = node.GetShaderInput("value")
        self.assertEqual(str(prop.GetLabel()), "Last property label")
        self.assertEqual(prop.GetHelp(), "Last property help")
        self.assertEqual(str(prop.GetPage()), "Last:Section")
        # The compiler reports C256 for the second authored soft_range and
        # omits it from its annotation block. Preserve every SDK occurrence,
        # rather than claiming we can recover annotations it suppressed.
        self.assertEqual(self.evalStr(prop.GetHints()["soft_range"]), {"min": 0, "max": 10})
        self.assertEqual(self.evalStr(prop.GetHints()["ui_order"]), {"order": 5})
        records = self._records(prop)
        self.assertEqual(len(records), 9)
        self.assertEqual(records[:4], [
            _record("::anno::display_name(string)", {"name": "First property label"}),
            _record("::anno::display_name(string)", {"name": "Last property label"}),
            _record("::anno::description(string)", {"description": "First property help"}),
            _record("::anno::description(string)", {"description": "Last property help"}),
        ])
        # The compiler upgrades MDL 1.7 in_group calls to current overloads
        # and supplies collapsed=false. Keep its full compiled signatures.
        self.assertEqual(records[4:6], [
            _record("::anno::in_group(string,bool)", {"group": "First", "collapsed": 0}),
            _record("::anno::in_group(string,string,bool)",
                    {"group": "Last", "subgroup": "Section", "collapsed": 0}),
        ])
        self.assertEqual(records[6:], [
            _record("::anno::soft_range(int,int)", {"min": 0, "max": 10}),
            _record("::anno::ui_order(int)", {"order": 3}),
            _record("::anno::ui_order(int)", {"order": 5}),
        ])

    def test_direct_colorspace_metadata(self):
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_colorspace")
        prop = node.GetShaderInput("texture")
        self.assertIsNotNone(prop)
        self.assertEqual(prop.GetMetadata()[Sdr.PropertyMetadata.Colorspace], "texture_colorspace")

    def test_direct_return_metadata(self):
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_return")
        self.assertEqual([str(name) for name in node.GetShaderOutputNames()],
                         [str(UsdMdl.Tokens.DefaultOutputPortName)])
        prop = node.GetShaderOutput(UsdMdl.Tokens.DefaultOutputPortName)
        self.assertIsNotNone(prop)
        self.assertEqual(str(prop.GetLabel()), "Result label")
        self.assertEqual(prop.GetHelp(), "Result help")
        self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.Float)

    def test_custom_annotations_preserve_namespace_and_context(self):
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_namespace_and_context")
        metadata = node.GetMetadata()
        for key in (Sdr.NodeMetadata.Label, Sdr.NodeMetadata.Help, Sdr.PropertyMetadata.Widget):
            self.assertNotIn(key, metadata)
        raw = self.evalStr(metadata[UsdMdl.Metadata.Annotation])
        self.assertIn(
            _record("::sdr_metadata::display_name(string)", {"name": "Custom node display name"}),
            raw["annotations"])
        self.assertIn(
            _record("::sdr_metadata::description(string)", {"description": "Custom node description"}),
            raw["annotations"])
        self.assertIn(
            _record("::sdr_metadata::private_note(int,string)", {"count": 8, "message": "Keep node note"}),
            raw["annotations"])
        self.assertIn(
            _record("::sdr_metadata::SDR__widget(string)", {"value": "property-only"}),
            raw["annotations"])
        for name, value in (("SDR__Label", "Wrong node suffix case"),
                            ("sdr__label", "Wrong node prefix case"),
                            ("SDR_label", "Wrong node separator"),
                            ("SDR__notAField", "Unknown node suffix")):
            self.assertIn(_record("::sdr_metadata::" + name + "(string)", {"value": value}),
                          raw["annotations"])

        prop = node.GetShaderInput("custom")
        self.assertIsNotNone(prop)
        metadata = prop.GetMetadata()
        for key in (Sdr.PropertyMetadata.Label, Sdr.PropertyMetadata.Help, Sdr.NodeMetadata.Category):
            self.assertNotIn(key, metadata)
        self.assertNotIn("hidden", prop.GetHints())
        self.assertNotIn("enable_if", prop.GetHints())
        raw = self.evalStr(metadata[UsdMdl.Metadata.Annotation])
        self.assertIn(
            _record("::sdr_metadata::display_name(string)", {"name": "Custom display name"}),
            raw["annotations"])
        self.assertIn(
            _record("::sdr_metadata::description(string)", {"description": "Custom description"}),
            raw["annotations"])
        self.assertIn(
            _record("::sdr_metadata::private_note(int,string)", {"count": 7, "message": "Keep me"}),
            raw["annotations"])
        self.assertIn(
            _record("::sdr_metadata::Label(string)", {"value": "Wrong case"}),
            raw["annotations"])
        self.assertIn(
            _record("::sdr_metadata::SDR__category(string)", {"value": "node-only"}),
            raw["annotations"])
        for name, value in (("SDR__Label", "Wrong prefixed case"),
                            ("sdr__label", "Wrong prefix case"),
                            ("SDR_label", "Wrong separator"),
                            ("SDR__notAField", "Unknown suffix")):
            self.assertIn(_record("::sdr_metadata::" + name + "(string)", {"value": value}),
                          raw["annotations"])
        self.assertIn(
            _record("::sdr_metadata::hidden()", {}),
            raw["annotations"])
        self.assertIn(
            _record("::sdr_metadata::enable_if(string)", {"condition": "custom > 0"}),
            raw["annotations"])

        ambiguous = node.GetShaderInput("ambiguous")
        self.assertIsNotNone(ambiguous)
        self.assertEqual(str(ambiguous.GetLabel()), "Standard label")
        raw = self.evalStr(ambiguous.GetMetadata()[UsdMdl.Metadata.Annotation])
        self.assertIn(
            _record("::sdr_metadata::SDR__label(int,int)", {"first": 10, "second": 20}),
            raw["annotations"])

    def test_unprefixed_sdr_names_remain_raw_metadata(self):
        node = self.validate_node("sdr_metadata.mdl", "test_metadata_unprefixed")
        self.assertEqual(str(node.GetLabel()), "SDK node label")
        self.assertEqual(node.GetHelp(), "SDK node help")
        self.assertNotIn(Sdr.NodeMetadata.Category, node.GetMetadata())
        raw = self.evalStr(node.GetMetadata()[UsdMdl.Metadata.Annotation])
        self.assertIn(
            _record("::sdr_metadata::label(string)", {"value": "Bare node label"}),
            raw["annotations"])
        self.assertIn(
            _record("::sdr_metadata::help(string)", {"text": "Bare node help"}),
            raw["annotations"])
        self.assertIn(
            _record("::sdr_metadata::category(string)", {"value": "Bare category"}),
            raw["annotations"])

        prop = node.GetShaderInput("value")
        self.assertEqual(str(prop.GetLabel()), "SDK property label")
        self.assertEqual(prop.GetHelp(), "SDK property help")
        self.assertEqual(prop.GetMetadata()[Sdr.PropertyMetadata.Connectable], "1")
        self.assertTrue(prop.IsConnectable())
        self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.Float3)
        self.assertEqual(prop.GetMetadata()[Sdr.PropertyMetadata.SdrUsdDefinitionType], "float3")
        self.assertEqual(prop.GetMetadata()[Sdr.PropertyMetadata.RenderType], "float3")
        self.assertEqual(prop.GetDefaultValue(), Gf.Vec3f(1.0, 2.0, 3.0))
        raw = self.evalStr(prop.GetMetadata()[UsdMdl.Metadata.Annotation])
        self.assertIn(
            _record("::sdr_metadata::label(string)", {"value": "Bare property label"}),
            raw["annotations"])
        self.assertIn(
            _record("::sdr_metadata::help(string)", {"text": "Bare property help"}),
            raw["annotations"])
        self.assertIn(
            _record("::sdr_metadata::sdrUsdDefinitionType(string)", {"value": "normal3f"}),
            raw["annotations"])
        self.assertIn(
            _record("::sdr_metadata::renderType(string)", {"value": "normal"}),
            raw["annotations"])
        disabled = node.GetShaderInput("bare_disabled")
        self.assertEqual(disabled.GetMetadata()[Sdr.PropertyMetadata.Connectable], "1")
        self.assertTrue(disabled.IsConnectable())
        # Raw boolean annotation metadata serializes false as 0.
        self.assertEqual(self.evalStr(disabled.GetMetadata()[UsdMdl.Metadata.Annotation]),
                         {"annotations": [
                             _record("::sdr_metadata::connectable(bool)", {"enabled": 0}),
                         ]})


if __name__ == "__main__":
    run_unittest_main_exit_clean()
