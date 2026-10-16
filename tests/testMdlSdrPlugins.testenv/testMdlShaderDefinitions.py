#
# SPDX-License-Identifier: Apache-2.0
#

import os
import subprocess
import sys
import tempfile
from pathlib import Path

_ROOT = os.path.dirname(os.path.abspath(__file__))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from lib.mdl_test_base import MdlTestBase, UsdMdl, run_unittest_main_exit_clean
from pxr import Gf, Sdf, Sdr, Usd, UsdShade


# The USD storage count differs from the logical MDL array count for flattened
# bool vectors and float matrices. Authored values differ from the SDK defaults.
_ARRAY_DEFAULT_CASES = (
    ("BoolVectorArray", "array.mdl", "test_vector_bool3_array(bool3[2])", "param",
     Sdf.ValueTypeNames.BoolArray, [False, True, True, True, False, False], 2),
    ("FloatMatrixArray", "array.mdl", "test_matrix_float2x2_array(float2x2[2])", "param",
     Sdf.ValueTypeNames.FloatArray, [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0], 2),
    ("BoolVector", "vector.mdl", "test_vector_bool3(bool3)", "param",
     Sdf.ValueTypeNames.BoolArray, [False, True, False], None),
    ("FloatMatrix", "matrix.mdl", "test_matrix_float2x2(float2x2)", "param",
     Sdf.ValueTypeNames.FloatArray, [1.0, 2.0, 3.0, 4.0], None),
    ("BoolArray", "sdr_metadata.mdl", "test_metadata_bool_array(bool[2])", "values",
     Sdf.ValueTypeNames.BoolArray, [True, False], 2),
    ("FloatVectorArray", "array.mdl", "test_vector_float2_array(float2[2])", "param",
     Sdf.ValueTypeNames.Float2Array, [Gf.Vec2f(2.0, 3.0), Gf.Vec2f(4.0, 5.0)], 2),
)


class ShaderDefinitionTests(MdlTestBase):
    def _nodes(self):
        definitions = Path(os.environ["PXR_TEST_DIR"]) / "preview_surface.usda"
        nodes = UsdMdl.AddShaderDefinitionsToRegistry(Sdf.AssetPath(str(definitions)))
        result = {str(node.GetIdentifier()): node for node in nodes}
        self.assertEqual(set(result), {
            "UsdMdlFixturePreviewUv", "UsdMdlFixturePreviewTransform2d",
            "UsdMdlFixturePreviewTexture", "UsdMdlFixturePreviewSurface"})
        for node in result.values():
            self.assertTrue(node.IsValid())
            self.assertEqual(str(node.GetSourceType()), "mdl")
        return result

    def test_definitions_resolve_from_registry_and_repeat_registration(self):
        nodes = self._nodes()
        repeated = self._nodes()
        for identifier, node in nodes.items():
            with self.subTest(identifier=identifier):
                registered = self.sdr_registry.GetShaderNodeByIdentifierAndType(identifier, "mdl")
                self.assertIsNotNone(registered)
                self.assertEqual(registered.GetShaderInputNames(), node.GetShaderInputNames())
                self.assertEqual(registered.GetShaderOutputNames(), node.GetShaderOutputNames())
                self.assertEqual(repeated[identifier].GetShaderInputNames(), node.GetShaderInputNames())
                shader = UsdShade.Shader.Define(self.stage, "/World/" + identifier)
                shader.CreateIdAttr(identifier)
                resolved = shader.GetShaderNodeForSourceType("mdl")
                self.assertIsNotNone(resolved)
                self.assertEqual(str(resolved.GetIdentifier()), identifier)

    def test_concurrent_first_registration_has_no_gil_mutex_deadlock(self):
        code = """
import concurrent.futures
import sys
import threading
from pxr import Sdf
import UsdMdl
barrier = threading.Barrier(6)
def register(index):
    barrier.wait(timeout=15)
    if index % 2 == 0:
        nodes = UsdMdl.AddShaderDefinitionsToRegistry(Sdf.AssetPath(sys.argv[1]))
        return 'proxy', {str(node.GetIdentifier()) for node in nodes}
    nodes = UsdMdl.AddModuleToRegistry(Sdf.AssetPath(sys.argv[2]))
    return 'native', {node.GetMetadata().get('mdl:subIdentifier') for node in nodes}
try:
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        results = list(pool.map(register, range(6)))
    expected = {'UsdMdlFixturePreviewUv', 'UsdMdlFixturePreviewTransform2d',
                'UsdMdlFixturePreviewTexture', 'UsdMdlFixturePreviewSurface'}
    native_expected = {'PreviewUv', 'PreviewTransform2d', 'PreviewTexture', 'PreviewSurface'}
    assert all(names == expected if kind == 'proxy' else native_expected <= names
               for kind, names in results), results
finally:
    UsdMdl.Shutdown()
"""
        env = dict(os.environ, USD_MDL_DISCOVER_ON_STARTUP="0")
        definitions = str(Path(os.environ["PXR_TEST_DIR"]) / "preview_surface.usda")
        result = subprocess.run([sys.executable, "-c", code, definitions,
                                 str(self.mdl_path / "preview_surface.mdl")],
                                env=env, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_changed_interface_identifier_is_rejected_without_replacement(self):
        code = """
import sys
from pxr import Gf, Sdf, Sdr, Usd, UsdShade
import UsdMdl
stage = Usd.Stage.CreateInMemory()
shader = UsdShade.Shader.Define(stage, '/Mutable')
shader.CreateIdAttr('UsdMdlFixtureMutableInterface')
shader.SetSourceAsset(Sdf.AssetPath(sys.argv[1]), 'mdl')
shader.SetSourceAssetSubIdentifier('PreviewTransform2d(float2,float,float2,float2)', 'mdl')
input_prop = shader.CreateInput('in', Sdf.ValueTypeNames.Float2)
input_prop.GetAttr().SetMetadata('sdrMetadata', {'implementationName': 'st'})
shader.CreateInput('rotation', Sdf.ValueTypeNames.Float)
shader.CreateInput('scale', Sdf.ValueTypeNames.Float2)
shader.CreateInput('translation', Sdf.ValueTypeNames.Float2)
output = shader.CreateOutput('result', Sdf.ValueTypeNames.Float2)
output.GetAttr().SetMetadata('sdrMetadata', {'implementationName': 'out'})
try:
    stage.GetRootLayer().Export(sys.argv[2])
    original = UsdMdl.AddShaderDefinitionsToRegistry(Sdf.AssetPath(sys.argv[2]))
    assert len(original) == 1, 'initial interface did not register'
    assert original[0].GetShaderInput('in').GetDefaultValueAsSdfType() == Gf.Vec2f(0.0)
    input_prop.Set(Gf.Vec2f(7.0, 9.0))
    stage.GetRootLayer().Export(sys.argv[2])
    changed = UsdMdl.AddShaderDefinitionsToRegistry(Sdf.AssetPath(sys.argv[2]))
    assert not changed, 'changed interface reused an immutable registry identifier'
    cached = Sdr.Registry().GetShaderNodeByIdentifierAndType('UsdMdlFixtureMutableInterface', 'mdl')
    assert cached.GetShaderInput('in').GetDefaultValueAsSdfType() == Gf.Vec2f(0.0), 'original node replaced'
finally:
    UsdMdl.Shutdown()
"""
        env = dict(os.environ, USD_MDL_DISCOVER_ON_STARTUP="0")
        with tempfile.TemporaryDirectory() as temporary:
            result = subprocess.run([
                sys.executable, "-c", code, str(self.mdl_path / "preview_surface.mdl"),
                str(Path(temporary) / "mutable.usda")],
                env=env, capture_output=True, text=True, timeout=60)
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, output)
        self.assertIn("different binding or interface", output)

    def test_definition_cannot_impersonate_future_native_identifier(self):
        code = """
import os
import subprocess
import sys
from pxr import Sdf, Sdr, Usd, UsdShade
import UsdMdl
probe = r'''
import sys
from pxr import Sdf
import UsdMdl
try:
    nodes = UsdMdl.AddModuleToRegistry(Sdf.AssetPath(sys.argv[1]))
    node = next(node for node in nodes
                if node.GetMetadata().get('mdl:subIdentifier') == 'test_metadata_property')
    print('NATIVE_IDENTIFIER=' + str(node.GetIdentifier()), flush=True)
finally:
    UsdMdl.Shutdown()
'''
# A separate process obtains the stable native ID without populating this
# process's cold Sdr/SDK caches with the future native module.
result = subprocess.run([sys.executable, '-c', probe, sys.argv[1]],
                        env=dict(os.environ, USD_MDL_DISCOVER_ON_STARTUP='0'),
                        capture_output=True, text=True, timeout=30)
assert result.returncode == 0, result.stdout + result.stderr
claimed = next(line.split('=', 1)[1] for line in result.stdout.splitlines()
               if line.startswith('NATIVE_IDENTIFIER='))
stage = Usd.Stage.CreateInMemory()
shader = UsdShade.Shader.Define(stage, '/Claimed')
shader.CreateIdAttr(claimed)
shader.SetSourceAsset(Sdf.AssetPath(sys.argv[2]), 'mdl')
shader.SetSourceAssetSubIdentifier('PreviewTransform2d(float2,float,float2,float2)', 'mdl')
input_prop = shader.CreateInput('in', Sdf.ValueTypeNames.Float2)
input_prop.GetAttr().SetMetadata('sdrMetadata', {'implementationName': 'st'})
output_prop = shader.CreateOutput('result', Sdf.ValueTypeNames.Float2)
output_prop.GetAttr().SetMetadata('sdrMetadata', {'implementationName': 'out'})
try:
    stage.GetRootLayer().Export(sys.argv[3])
    interface = UsdMdl.AddShaderDefinitionsToRegistry(Sdf.AssetPath(sys.argv[3]))
    assert not interface, 'definition was allowed to claim a generated native identifier'
    native_nodes = UsdMdl.AddModuleToRegistry(Sdf.AssetPath(sys.argv[1]))
    assert any(str(node.GetIdentifier()) == claimed for node in native_nodes), 'ordinary native node failed to register'
    cached = Sdr.Registry().GetShaderNodeByIdentifierAndType(claimed, 'mdl')
    assert cached.GetShaderInput('param') is not None, 'native node was displaced by interface'
    assert cached.GetShaderInput('in') is None, 'interface masqueraded as native node'
finally:
    UsdMdl.Shutdown()
"""
        env = dict(os.environ, USD_MDL_DISCOVER_ON_STARTUP="0")
        with tempfile.TemporaryDirectory() as temporary:
            result = subprocess.run([
                sys.executable, "-c", code, str(self.mdl_path / "sdr_metadata.mdl"),
                str(self.mdl_path / "preview_surface.mdl"), str(Path(temporary) / "claimed.usda")],
                env=env, capture_output=True, text=True, timeout=60)
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, output)
        self.assertIn("must be a valid USD namespaced identifier", output)

    def test_usd_input_and_output_names_keep_native_binding(self):
        node = self._nodes()["UsdMdlFixturePreviewTransform2d"]
        self.assertEqual(set(node.GetShaderInputNames()), {"in", "rotation", "scale", "translation"})
        self.assertEqual(set(node.GetShaderOutputNames()), {"result"})
        input_prop = node.GetShaderInput("in")
        self.assertEqual(str(input_prop.GetImplementationName()), "st")
        self.assertEqual(input_prop.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.Float2)
        self.assertEqual(input_prop.GetDefaultValueAsSdfType(), Gf.Vec2f(0.0))
        output_prop = node.GetShaderOutput("result")
        self.assertEqual(str(output_prop.GetImplementationName()), "out")
        native = self.validate_node("preview_surface.mdl", "PreviewTransform2d")
        self.assertNotEqual(str(native.GetIdentifier()), str(node.GetIdentifier()))
        self.assertEqual(set(native.GetShaderInputNames()), {"st", "rotation", "scale", "translation"})
        self.assertEqual(set(native.GetShaderOutputNames()), {str(UsdMdl.Tokens.DefaultOutputPortName)})

    def test_texture_outputs_are_native_sdk_fields(self):
        node = self._nodes()["UsdMdlFixturePreviewTexture"]
        self.assertEqual(set(node.GetShaderOutputNames()), {"r", "g", "b", "a", "rgb"})
        for name in ("r", "g", "b", "a", "rgb"):
            with self.subTest(output=name):
                prop = node.GetShaderOutput(name)
                self.assertEqual(str(prop.GetImplementationName()), name)
                self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(),
                                 Sdf.ValueTypeNames.Color3f if name == "rgb" else Sdf.ValueTypeNames.Float)
        self.assertEqual(node.GetShaderOutput("rgb").GetType(), Sdr.PropertyTypes.Color)
        self.assertEqual(node.GetShaderInput("file").GetMetadata()[Sdr.PropertyMetadata.Colorspace],
                         "sourceColorSpace")
        native = self.validate_node("preview_surface.mdl", "PreviewTexture")
        self.assertEqual(set(native.GetShaderOutputNames()), {"r", "g", "b", "a", "rgb"})
        native_wrap = native.GetShaderInput("wrapS")
        self.assertEqual(native_wrap.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.Int)
        self.assertIs(type(native_wrap.GetDefaultValue()), int)
        self.assertEqual(native_wrap.GetDefaultValue(), 20)
        self.assertEqual(list(native_wrap.GetOptions()), [
            ("black", "-1"), ("clamp", "2"), ("repeat", "7"),
            ("mirror", "11"), ("useMetadata", "20")])
        self.assertEqual(native.GetShaderInput("sourceColorSpace").GetTypeAsSdfType().GetSdfType(),
                         Sdf.ValueTypeNames.String)
        self.assertTrue(native_wrap.IsConnectable())
        self.assertNotIn(Sdr.PropertyMetadata.Colorspace, native.GetShaderInput("file").GetMetadata())

    def test_terminal_aliases_bind_the_whole_material(self):
        node = self._nodes()["UsdMdlFixturePreviewSurface"]
        self.assertEqual(set(node.GetShaderOutputNames()), {"surface", "displacement"})
        for name in ("surface", "displacement"):
            with self.subTest(terminal=name):
                prop = node.GetShaderOutput(name)
                self.assertEqual(str(prop.GetImplementationName()), "out")
                self.assertEqual(prop.GetMetadata()[Sdr.PropertyMetadata.RenderType], "terminal " + name)
                self.assertIsNone(prop.GetDefaultValue())
        native = self.validate_node("preview_surface.mdl", "PreviewSurface")
        self.assertEqual(set(native.GetShaderOutputNames()), {str(UsdMdl.Tokens.DefaultOutputPortName)})

    def test_symbolic_wrap_and_opacity_defaults(self):
        nodes = self._nodes()
        texture = nodes["UsdMdlFixturePreviewTexture"]
        for name in ("wrapS", "wrapT"):
            with self.subTest(parameter=name):
                prop = texture.GetShaderInput(name)
                self.assertEqual(prop.GetType(), Sdr.PropertyTypes.String)
                self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.Token)
                self.assertEqual(prop.GetDefaultValue(), "useMetadata")
                self.assertEqual(prop.GetDefaultValueAsSdfType(), "useMetadata")
                self.assertEqual(list(prop.GetOptions()), [
                    ("black", "black"), ("clamp", "clamp"), ("repeat", "repeat"),
                    ("mirror", "mirror"), ("useMetadata", "useMetadata")])
                self.assertFalse(prop.IsConnectable())
        source_color_space = texture.GetShaderInput("sourceColorSpace")
        self.assertEqual(source_color_space.GetDefaultValueAsSdfType(), "auto")
        self.assertFalse(source_color_space.IsConnectable())
        opacity = nodes["UsdMdlFixturePreviewSurface"].GetShaderInput("opacityMode")
        self.assertEqual(opacity.GetDefaultValueAsSdfType(), "transparent")
        self.assertEqual(list(opacity.GetOptions()), [
            ("transparent", "transparent"), ("presence", "presence")])
        self.assertFalse(opacity.IsConnectable())

    def test_roles_and_unavailable_state_default(self):
        node = self._nodes()["UsdMdlFixturePreviewSurface"]
        normal = node.GetShaderInput("normal")
        self.assertEqual(normal.GetType(), Sdr.PropertyTypes.Normal)
        self.assertEqual(normal.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.Normal3f)
        self.assertIsNone(normal.GetDefaultValue())
        self.assertIsNone(normal.GetDefaultValueAsSdfType())
        self.assertEqual(normal.GetMetadata()[UsdMdl.Metadata.HasDefault], "true")
        self.assertEqual(normal.GetMetadata()[UsdMdl.Metadata.ExpressionKind], UsdMdl.ExpressionKinds.Call)
        diffuse = node.GetShaderInput("diffuseColor")
        self.assertEqual(diffuse.GetType(), Sdr.PropertyTypes.Color)
        self.assertEqual(diffuse.GetDefaultValueAsSdfType(), Gf.Vec3f(0.18))
        output_node = self.validate_node("sdr_metadata.mdl", "test_metadata_point_output")
        output = output_node.GetShaderOutput(UsdMdl.Tokens.DefaultOutputPortName)
        self.assertTrue(normal.CanConnectTo(output))
        self.assertTrue(diffuse.CanConnectTo(output))
        float2_node = self.validate_node("sdr_metadata.mdl", "test_metadata_float2_output")
        self.assertFalse(normal.CanConnectTo(float2_node.GetShaderOutput(UsdMdl.Tokens.DefaultOutputPortName)))

    def test_usd_integer_enum_interface_uses_original_sdk_codes(self):
        stage = Usd.Stage.CreateInMemory()
        shader = UsdShade.Shader.Define(stage, "/IntegerEnumInterface")
        shader.CreateIdAttr("UsdMdlFixtureIntegerEnumInterface")
        shader.SetSourceAsset(self.get_module_path("sdr_metadata.mdl"), "mdl")
        shader.SetSourceAssetSubIdentifier("test_metadata_tokens(SparseMode,SparseMode,string,string)", "mdl")
        shader.CreateInput("enum_token", Sdf.ValueTypeNames.Int)
        result = shader.CreateOutput("result", Sdf.ValueTypeNames.Int)
        result.GetAttr().SetMetadata("sdrMetadata", {"implementationName": "out"})
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "integer_enum.usda"
            self.assertTrue(stage.GetRootLayer().Export(str(path)))
            nodes = UsdMdl.AddShaderDefinitionsToRegistry(Sdf.AssetPath(str(path)))
        self.assertEqual(len(nodes), 1)
        prop = nodes[0].GetShaderInput("enum_token")
        self.assertEqual(prop.GetType(), Sdr.PropertyTypes.Int)
        self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.Int)
        self.assertIs(type(prop.GetDefaultValue()), int)
        self.assertEqual(prop.GetDefaultValue(), 8)
        self.assertEqual(prop.GetDefaultValueAsSdfType(), 8)
        self.assertEqual(list(prop.GetOptions()), [
            ("negative", "-3"), ("middle", "8"), ("last", "42"), ("alias", "8")])
        native = self.validate_node("sdr_metadata.mdl", "test_metadata_tokens")
        native_prop = native.GetShaderInput("enum_token")
        self.assertEqual(native_prop.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.Token)
        self.assertEqual(native_prop.GetDefaultValue(), "middle")
        self.assertEqual(list(native_prop.GetOptions()), [
            ("negative", "negative"), ("middle", "middle"),
            ("last", "last"), ("alias", "alias")])

    def test_asset_defaults_preserve_native_and_authored_resolved_identity(self):
        native = self.validate_node("texture.mdl", "test_texture2D")
        native_default = native.GetShaderInput("param").GetDefaultValueAsSdfType()
        self.assertIsInstance(native_default, Sdf.AssetPath)
        self.assertTrue(native_default.resolvedPath)
        stage = Usd.Stage.CreateInMemory()
        expected_authored = None
        for suffix in ("Native", "Authored"):
            shader = UsdShade.Shader.Define(stage, "/" + suffix)
            shader.CreateIdAttr("UsdMdlFixtureAssetDefault" + suffix)
            shader.SetSourceAsset(self.get_module_path("texture.mdl"), "mdl")
            shader.SetSourceAssetSubIdentifier("test_texture2D(texture_2d)", "mdl")
            prop = shader.CreateInput("param", Sdf.ValueTypeNames.Asset)
            if suffix == "Authored":
                self.assertTrue(prop.Set(Sdf.AssetPath(str(self.mdl_path / "resources" / "one.png"))))
                expected_authored = prop.Get()
                self.assertTrue(expected_authored.resolvedPath)
            result = shader.CreateOutput("result", Sdf.ValueTypeNames.Color3f)
            result.GetAttr().SetMetadata("sdrMetadata", {"implementationName": "out"})
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "asset_defaults.usda"
            self.assertTrue(stage.GetRootLayer().Export(str(path)))
            nodes = UsdMdl.AddShaderDefinitionsToRegistry(Sdf.AssetPath(str(path)))
        self.assertEqual(len(nodes), 2)
        by_id = {str(node.GetIdentifier()): node for node in nodes}
        for suffix, expected in (("Native", native_default), ("Authored", expected_authored)):
            with self.subTest(default=suffix):
                prop = by_id["UsdMdlFixtureAssetDefault" + suffix].GetShaderInput("param")
                self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.Asset)
                actual = prop.GetDefaultValueAsSdfType()
                self.assertIsInstance(actual, Sdf.AssetPath)
                self.assertEqual(actual.path, expected.path)
                self.assertEqual(actual.resolvedPath, expected.resolvedPath)
                self.assert_resolved_asset_bytes(actual.resolvedPath, self.mdl_path / "resources" / "one.png")

    def test_authored_boolean_array_default_retains_boolean_storage(self):
        stage = Usd.Stage.CreateInMemory()
        shader = UsdShade.Shader.Define(stage, "/BooleanArrayInterface")
        shader.CreateIdAttr("UsdMdlFixtureBooleanArrayInterface")
        shader.SetSourceAsset(self.get_module_path("sdr_metadata.mdl"), "mdl")
        shader.SetSourceAssetSubIdentifier("test_metadata_bool_array(bool[2])", "mdl")
        values = shader.CreateInput("values", Sdf.ValueTypeNames.BoolArray)
        self.assertTrue(values.Set([True, False]))
        result = shader.CreateOutput("result", Sdf.ValueTypeNames.BoolArray)
        result.GetAttr().SetMetadata("sdrMetadata", {"implementationName": "out"})
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "boolean_array.usda"
            self.assertTrue(stage.GetRootLayer().Export(str(path)))
            nodes = UsdMdl.AddShaderDefinitionsToRegistry(Sdf.AssetPath(str(path)))
        self.assertEqual(len(nodes), 1)
        prop = nodes[0].GetShaderInput("values")
        self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.BoolArray)
        default = prop.GetDefaultValueAsSdfType()
        self.assertEqual(list(default), [True, False])
        self.assertTrue(all(type(value) is bool for value in default))
        self.assertTrue(prop.IsArray())
        self.assertEqual(prop.GetArraySize(), 2)
        native = self.validate_node("sdr_metadata.mdl", "test_metadata_bool_array")
        self.assertEqual(list(native.GetShaderInput("values").GetDefaultValueAsSdfType()), [False, True])

    def _array_default_shader(self, stage, identifier, module, subidentifier,
                              input_name, usd_type, values):
        shader = UsdShade.Shader.Define(stage, "/" + identifier)
        shader.CreateIdAttr(identifier)
        shader.SetSourceAsset(self.get_module_path(module), "mdl")
        shader.SetSourceAssetSubIdentifier(subidentifier, "mdl")
        self.assertTrue(shader.CreateInput(input_name, usd_type).Set(values))
        result = shader.CreateOutput("result", usd_type)
        result.GetAttr().SetMetadata("sdrMetadata", {"implementationName": "out"})
        return shader

    def test_authored_array_defaults_use_usd_storage_shape(self):
        stage = Usd.Stage.CreateInMemory()
        for case, module, subidentifier, input_name, usd_type, values, _ in _ARRAY_DEFAULT_CASES:
            self._array_default_shader(stage, "UsdMdlFixtureAuthored" + case,
                                       module, subidentifier, input_name, usd_type, values)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "array_defaults.usda"
            self.assertTrue(stage.GetRootLayer().Export(str(path)))
            nodes = UsdMdl.AddShaderDefinitionsToRegistry(Sdf.AssetPath(str(path)))
        by_id = {str(node.GetIdentifier()): node for node in nodes}
        self.assertEqual(set(by_id), {"UsdMdlFixtureAuthored" + case[0] for case in _ARRAY_DEFAULT_CASES})
        for case, _, _, input_name, usd_type, values, mdl_size in _ARRAY_DEFAULT_CASES:
            with self.subTest(case=case):
                node = by_id["UsdMdlFixtureAuthored" + case]
                self.assertTrue(node.IsValid())
                prop = node.GetShaderInput(input_name)
                self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), usd_type)
                self.assertEqual(list(prop.GetDefaultValueAsSdfType()), values)
                self.assertEqual(list(prop.GetDefaultValue()), values)
                self.assertTrue(prop.IsArray())
                self.assertFalse(prop.IsDynamicArray())
                for port in (prop, node.GetShaderOutput("result")):
                    self.assertEqual(port.GetArraySize(), len(values))
                    if mdl_size is None:
                        self.assertNotIn(UsdMdl.Metadata.ArraySize, port.GetMetadata())
                    else:
                        self.assertEqual(port.GetMetadata()[UsdMdl.Metadata.ArraySize], str(mdl_size))

    def test_wrong_authored_array_storage_lengths_are_rejected(self):
        # Capture native warnings and check registry absence in a fresh process.
        code = """
import sys
from pathlib import Path
from pxr import Sdf, Sdr, Usd, UsdShade
import UsdMdl
from testMdlShaderDefinitions import _ARRAY_DEFAULT_CASES
try:
    for case, module, subidentifier, input_name, usd_type, values, mdl_size in _ARRAY_DEFAULT_CASES:
        sizes = {0, len(values) - 1, len(values) + 1}
        if mdl_size is not None:
            sizes.add(mdl_size)
        sizes.discard(len(values))
        for size in sorted(sizes):
            identifier = 'UsdMdlFixtureRejected' + case + str(size)
            stage = Usd.Stage.CreateInMemory()
            shader = UsdShade.Shader.Define(stage, '/' + identifier)
            shader.CreateIdAttr(identifier)
            shader.SetSourceAsset(Sdf.AssetPath(str(Path(sys.argv[1]) / module)), 'mdl')
            shader.SetSourceAssetSubIdentifier(subidentifier, 'mdl')
            assert shader.CreateInput(input_name, usd_type).Set([values[0]] * size)
            result = shader.CreateOutput('result', usd_type)
            result.GetAttr().SetMetadata('sdrMetadata', {'implementationName': 'out'})
            path = str(Path(sys.argv[2]) / (identifier + '.usda'))
            assert stage.GetRootLayer().Export(path)
            nodes = UsdMdl.AddShaderDefinitionsToRegistry(Sdf.AssetPath(path))
            assert not nodes, (identifier, 'invalid shader definition was registered')
            cached = Sdr.Registry().GetShaderNodeByIdentifierAndType(identifier, 'mdl')
            assert cached is None, (identifier, 'invalid shader definition remains in registry')
finally:
    UsdMdl.Shutdown()
"""
        env = dict(os.environ, USD_MDL_DISCOVER_ON_STARTUP="0")
        with tempfile.TemporaryDirectory() as temporary:
            result = subprocess.run([sys.executable, "-c", code, str(self.mdl_path), temporary],
                                    env=env, capture_output=True, text=True, timeout=60)
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, output)
        for case, _, _, input_name, _, values, mdl_size in _ARRAY_DEFAULT_CASES:
            sizes = {0, len(values) - 1, len(values) + 1, mdl_size} - {None, len(values)}
            for size in sizes:
                with self.subTest(case=case, storage_length=size):
                    identifier = "UsdMdlFixtureRejected" + case + str(size)
                    self.assertIn("MDL shader definition </" + identifier
                                  + ">: authored array default on '" + input_name
                                  + "' has the wrong USD storage length (expected "
                                  + str(len(values)) + ", got " + str(size) + ")", output)

    def test_authored_empty_and_deferred_array_defaults_remain_valid(self):
        stage = Usd.Stage.CreateInMemory()
        self._array_default_shader(stage, "UsdMdlFixtureAuthoredEmptyArray",
                                   "type_checks/identity.mdl", "empty_array(float[0])",
                                   "param", Sdf.ValueTypeNames.FloatArray, [])
        # Deferred MDL arrays have no fixed storage length, even when their
        # native default is nonempty and the element converter flattens matrices.
        for case, values in (("Empty", []), ("Larger", [float(value) for value in range(1, 13)])):
            self._array_default_shader(stage, "UsdMdlFixtureAuthoredDeferred" + case,
                                       "type_checks/identity.mdl", "deferred_matrix_array",
                                       "param", Sdf.ValueTypeNames.FloatArray, values)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "empty_deferred_defaults.usda"
            self.assertTrue(stage.GetRootLayer().Export(str(path)))
            nodes = UsdMdl.AddShaderDefinitionsToRegistry(Sdf.AssetPath(str(path)))
        by_id = {str(node.GetIdentifier()): node for node in nodes}
        self.assertEqual(set(by_id), {"UsdMdlFixtureAuthoredEmptyArray",
                                    "UsdMdlFixtureAuthoredDeferredEmpty", "UsdMdlFixtureAuthoredDeferredLarger"})
        prop = by_id["UsdMdlFixtureAuthoredEmptyArray"].GetShaderInput("param")
        self.assertEqual(list(prop.GetDefaultValueAsSdfType()), [])
        self.assertEqual(prop.GetArraySize(), 0)
        self.assertEqual(prop.GetMetadata()[UsdMdl.Metadata.ArrayIsImmediateSized], "true")
        self.assertEqual(prop.GetMetadata()[UsdMdl.Metadata.ArraySize], "0")
        for case, values in (("Empty", []), ("Larger", [float(value) for value in range(1, 13)])):
            with self.subTest(default=case):
                prop = by_id["UsdMdlFixtureAuthoredDeferred" + case].GetShaderInput("param")
                self.assertEqual(list(prop.GetDefaultValueAsSdfType()), values)
                self.assertEqual(prop.GetArraySize(), 0)
                self.assertTrue(prop.IsDynamicArray())
                self.assertEqual(prop.GetMetadata()[UsdMdl.Metadata.ArrayDeferredSizeSymbol], "N")
                self.assertNotIn(UsdMdl.Metadata.ArraySize, prop.GetMetadata())

    def test_native_widget_retained_until_usd_authors_an_override(self):
        stage = Usd.Stage.CreateInMemory()
        widget_class = stage.CreateClassPrim(Sdf.Path("/WidgetClass"))
        inherited = widget_class.CreateAttribute("inputs:param", Sdf.ValueTypeNames.Int)
        self.assertTrue(inherited.SetMetadata("sdrMetadata", {"widget": "inheritedNumber"}))
        for name, widget in (("Retained", None), ("Overridden", "number"), ("Inherited", None)):
            shader = UsdShade.Shader.Define(stage, "/Widget" + name)
            shader.CreateIdAttr("UsdMdlFixtureWidget" + name)
            if name == "Inherited":
                self.assertTrue(shader.GetPrim().GetInherits().AddInherit(Sdf.Path("/WidgetClass")))
            shader.SetSourceAsset(self.get_module_path("sdr_metadata.mdl"), "mdl")
            shader.SetSourceAssetSubIdentifier("test_metadata_property(int)", "mdl")
            prop = shader.CreateInput("param", Sdf.ValueTypeNames.Int)
            prop.Set(1)
            if widget is not None:
                prop.GetAttr().SetMetadata("sdrMetadata", {"widget": widget})
            result = shader.CreateOutput("result", Sdf.ValueTypeNames.Int)
            result.GetAttr().SetMetadata("sdrMetadata", {"implementationName": "out"})
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "widgets.usda"
            self.assertTrue(stage.GetRootLayer().Export(str(path)))
            nodes = UsdMdl.AddShaderDefinitionsToRegistry(Sdf.AssetPath(str(path)))
        self.assertEqual(len(nodes), 3)
        by_id = {str(node.GetIdentifier()): node for node in nodes}
        self.assertEqual(by_id["UsdMdlFixtureWidgetRetained"].GetShaderInput("param").GetWidget(), "slider")
        self.assertEqual(by_id["UsdMdlFixtureWidgetOverridden"].GetShaderInput("param").GetWidget(), "number")
        self.assertEqual(by_id["UsdMdlFixtureWidgetInherited"].GetShaderInput("param").GetWidget(), "inheritedNumber")
        native = self.validate_node("sdr_metadata.mdl", "test_metadata_property")
        self.assertEqual(native.GetShaderInput("param").GetWidget(), "slider")

    def test_optional_module_override(self):
        # Distinct identifiers keep this independent of the registry's immutable cache.
        layer = Sdf.Layer.OpenAsAnonymous(str(Path(os.environ["PXR_TEST_DIR"]) / "preview_surface.usda"))
        self.assertIsNotNone(layer)
        stage = Usd.Stage.Open(layer)
        self.assertIsNotNone(stage)
        for prim in stage.Traverse():
            shader = UsdShade.Shader(prim)
            if shader:
                shader.CreateIdAttr(str(shader.GetIdAttr().Get()) + "Override")
                shader.SetSourceAsset(Sdf.AssetPath("missing_implementation.mdl"), "mdl")
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "override.usda"
            self.assertTrue(stage.GetRootLayer().Export(str(path)))
            nodes = UsdMdl.AddShaderDefinitionsToRegistry(
                Sdf.AssetPath(str(path)), self.get_module_path("preview_surface.mdl"))
        self.assertEqual(len(nodes), 4)
        self.assertEqual({str(node.GetIdentifier()) for node in nodes}, {
            "UsdMdlFixturePreviewUvOverride", "UsdMdlFixturePreviewTransform2dOverride",
            "UsdMdlFixturePreviewTextureOverride", "UsdMdlFixturePreviewSurfaceOverride"})
        transform = next(node for node in nodes if str(node.GetIdentifier()).endswith("Transform2dOverride"))
        self.assertEqual(str(transform.GetShaderInput("in").GetImplementationName()), "st")

    def _check_rejected_definition(self, case, subidentifier, input_type, input_name,
                                   implementation_name, diagnostic_terms):
        # Fresh processes capture native diagnostics and prevent a rejected ID
        # from interacting with another test's singleton registry state.
        code = """
import sys
from pxr import Sdf, Usd, UsdShade
import UsdMdl
stage = Usd.Stage.CreateInMemory()
shader = UsdShade.Shader.Define(stage, '/Invalid')
shader.CreateIdAttr('UsdMdlFixtureRejected' + sys.argv[3])
shader.SetSourceAsset(Sdf.AssetPath(sys.argv[1]), 'mdl')
shader.SetSourceAssetSubIdentifier(sys.argv[4], 'mdl')
prop = shader.CreateInput(sys.argv[6], getattr(Sdf.ValueTypeNames, sys.argv[5]))
prop.GetAttr().SetMetadata('sdrMetadata', {'implementationName': sys.argv[7]})
output = shader.CreateOutput('result', Sdf.ValueTypeNames.Float2)
output.GetAttr().SetMetadata('sdrMetadata', {'implementationName': 'out'})
stage.GetRootLayer().Export(sys.argv[2])
try:
    nodes = UsdMdl.AddShaderDefinitionsToRegistry(Sdf.AssetPath(sys.argv[2]))
    assert not nodes, 'invalid shader definition was registered'
finally:
    UsdMdl.Shutdown()
"""
        env = dict(os.environ, USD_MDL_DISCOVER_ON_STARTUP="0")
        with tempfile.TemporaryDirectory() as temporary:
            result = subprocess.run([
                sys.executable, "-c", code,
                str(self.mdl_path / "preview_surface.mdl"), str(Path(temporary) / "invalid.usda"),
                case, subidentifier, input_type, input_name, implementation_name],
                env=env, capture_output=True, text=True, timeout=60)
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, output)
        for term in diagnostic_terms:
            self.assertIn(term, output)

    def test_unknown_native_input_binding_is_rejected(self):
        self._check_rejected_definition("UnknownInput", "PreviewTransform2d(float2,float,float2,float2)",
                                        "Float2", "in", "missing", ("missing",))

    def test_incompatible_usd_input_type_is_rejected(self):
        self._check_rejected_definition("WrongType", "PreviewTransform2d(float2,float,float2,float2)",
                                        "Normal3f", "in", "st", ("normal3f",))

    def test_unsupported_usd_types_warn_and_omit_without_coding_error(self):
        for value_type, spelling in (("Double", "double"), ("Matrix3d", "matrix3d")):
            with self.subTest(usd_type=spelling):
                self._check_rejected_definition("Unsupported" + value_type,
                    "PreviewTransform2d(float2,float,float2,float2)",
                    value_type, "in", "st", ("unsupported USD input type '" + spelling + "'",))

    def test_ambiguous_native_function_name_is_rejected(self):
        self._check_rejected_definition("Ambiguous", "Overloaded", "Float", "value", "value", ("Overloaded",))


class ArrayConstructorDefinitionTests(MdlTestBase):
    def _array_shader(self, stage, identifier, element="float", input_type=None,
                      output_type=None, asset="mdl-builtin.mdl", reverse=True):
        input_type = input_type or Sdf.ValueTypeNames.Float
        output_type = output_type or Sdf.ValueTypeNames.FloatArray
        shader = UsdShade.Shader.Define(stage, Sdf.Path("/" + identifier))
        shader.CreateIdAttr(identifier)
        module = Sdf.AssetPath(asset) if asset == "mdl-builtin.mdl" else self.get_module_path(asset)
        shader.SetSourceAsset(module, "mdl")
        shader.SetSourceAssetSubIdentifier("T[](...)<" + element + ",2>", "mdl")
        ports = [("first", "value0"), ("second", "value1")]
        for name, binding in reversed(ports) if reverse else ports:
            prop = shader.CreateInput(name, input_type)
            prop.GetAttr().SetMetadata("sdrMetadata", {"implementationName": binding})
        output = shader.CreateOutput("result", output_type)
        output.GetAttr().SetMetadata("sdrMetadata", {"implementationName": "out"})
        return shader

    def _register_array_stage(self, stage):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "array_definitions.usda"
            self.assertTrue(stage.GetRootLayer().Export(str(path)))
            nodes = UsdMdl.AddShaderDefinitionsToRegistry(Sdf.AssetPath(str(path)))
        return {str(node.GetIdentifier()): node for node in nodes}, path

    def _run_array_worker(self, code, *arguments):
        # Each worker begins with cold SDK/Sdr caches and captures native warnings.
        prefix = """
import concurrent.futures
import runpy
import sys
import tempfile
import threading
from pathlib import Path
from pxr import Sdf, Sdr, Usd, UsdShade
import UsdMdl
namespace = runpy.run_path(sys.argv[1], run_name='array_definition_fixture')
fixture = namespace['ArrayConstructorDefinitionTests']()
fixture.setUp()
"""
        result = subprocess.run(
            [sys.executable, "-c", prefix + code, str(Path(__file__).resolve()), *arguments],
            env=dict(os.environ, USD_MDL_DISCOVER_ON_STARTUP="0"),
            capture_output=True, text=True, timeout=60)
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, output)
        return output

    def test_array_constructor_aliases_types_defaults_and_identity(self):
        stage = Usd.Stage.CreateInMemory()
        identifier = "UsdMdlFixtureArrayPair"
        shader = self._array_shader(stage, identifier)
        self.assertTrue(shader.GetInput("first").Set(10.0))
        # SDK identity and type metadata cannot be replaced by authored opinions.
        shader.SetSdrMetadata({"mdl:mdl_db_definition_name": "forged"})
        shader.GetOutput("result").SetSdrMetadata({
            "implementationName": "out", "mdl.arraySize": "99"})
        nodes, definition_path = self._register_array_stage(stage)
        self.assertEqual(set(nodes), {identifier})
        node = nodes[identifier]
        self.assertTrue(node.IsValid())
        self.assertEqual(set(node.GetShaderInputNames()), {"first", "second"})
        self.assertEqual(list(node.GetShaderOutputNames()), ["result"])
        metadata = node.GetMetadata()
        self.assertEqual(metadata["mdl:subIdentifier"], "T[](...)<float,2>")
        self.assertEqual(metadata["mdl:mdlNameWithSignature"], "T[](...)<float,2>")
        self.assertEqual(metadata["mdl:mdl_db_definition_name"], "mdl::T[](...)")
        self.assertEqual(metadata["mdl:mdl_db_module_name"], "mdl::%3Cbuiltins%3E")
        self.assertEqual(metadata["mdl:moduleUsdIdentifier"], "mdl-builtin.mdl")
        self.assertEqual(str(node.GetImplementationName()), "T[](...)<float,2>")
        self.assertEqual(Path(node.GetResolvedDefinitionURI()), definition_path)
        self.assertEqual(node.GetResolvedImplementationURI(), "")
        for name, binding in (("first", "value0"), ("second", "value1")):
            prop = node.GetShaderInput(name)
            self.assertEqual(str(prop.GetImplementationName()), binding)
            self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.Float)
            self.assertEqual(prop.GetMetadata()["mdl.hasDefault"], "false")
            self.assertNotIn("mdl.expressionKind", prop.GetMetadata())
        self.assertEqual(node.GetShaderInput("first").GetDefaultValueAsSdfType(), 10.0)
        self.assertIsNone(node.GetShaderInput("second").GetDefaultValueAsSdfType())
        result = node.GetShaderOutput("result")
        self.assertEqual(str(result.GetImplementationName()), "out")
        self.assertEqual(result.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.FloatArray)
        self.assertEqual(result.GetArraySize(), 2)
        self.assertFalse(result.IsDynamicArray())
        self.assertEqual(result.GetMetadata()["mdl.arrayIsImmediateSized"], "true")
        self.assertEqual(result.GetMetadata()["mdl.arraySize"], "2")
        self.assertIsNone(result.GetDefaultValueAsSdfType())
        cached = self.sdr_registry.GetShaderNodeByIdentifierAndType(identifier, "mdl")
        self.assertEqual(cached.GetMetadata(), node.GetMetadata())
        scene_shader = UsdShade.Shader.Define(self.stage, Sdf.Path("/UseArrayPair"))
        scene_shader.CreateIdAttr(identifier)
        self.assertEqual(scene_shader.GetShaderNodeForSourceType("mdl").GetIdentifier(), identifier)

    def test_array_constructor_named_struct_and_enum_interfaces(self):
        stage = Usd.Stage.CreateInMemory()
        for element, input_type, output_type in (
                ("StructA", Sdf.ValueTypeNames.Token, Sdf.ValueTypeNames.TokenArray),
                ("EnumA", Sdf.ValueTypeNames.Token, Sdf.ValueTypeNames.IntArray)):
            shader = self._array_shader(stage, "UsdMdlFixtureArray" + element, element,
                                        input_type, output_type, "type_checks/identity.mdl")
            if element == "EnumA":
                shader.GetInput("first").Set("a1")
        nodes, _ = self._register_array_stage(stage)
        self.assertEqual(set(nodes), {"UsdMdlFixtureArrayStructA", "UsdMdlFixtureArrayEnumA"})
        for element, output_type in (("StructA", Sdf.ValueTypeNames.TokenArray),
                                     ("EnumA", Sdf.ValueTypeNames.IntArray)):
            node = nodes["UsdMdlFixtureArray" + element]
            symbol = "::type_checks::identity::" + element
            self.assertEqual(node.GetMetadata()["mdl:subIdentifier"], "T[](...)<" + element + ",2>")
            self.assertEqual(str(node.GetImplementationName()), "T[](...)<" + symbol + ",2>")
            self.assertEqual(node.GetMetadata()["mdl:mdl_db_module_name"], "mdl::type_checks::identity")
            self.assertEqual(Path(node.GetResolvedImplementationURI()).resolve(),
                             (self.mdl_path / "type_checks/identity.mdl").resolve())
            for name in ("first", "second"):
                prop = node.GetShaderInput(name)
                self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.Token)
                self.assertEqual(prop.GetMetadata()["mdl.symbol"], symbol)
                self.assertEqual(prop.GetMetadata()["mdl.hasDefault"], "false")
            output = node.GetShaderOutput("result")
            self.assertEqual(output.GetTypeAsSdfType().GetSdfType(), output_type)
            self.assertEqual(output.GetMetadata()["mdl.symbol"], symbol)
            self.assertEqual(output.GetMetadata()["mdl.arraySize"], "2")
        enum_input = nodes["UsdMdlFixtureArrayEnumA"].GetShaderInput("first")
        self.assertEqual(enum_input.GetDefaultValueAsSdfType(), "a1")
        self.assertEqual(list(enum_input.GetOptions()), [("a0", "a0"), ("a1", "a1")])
        self.assertIsNone(nodes["UsdMdlFixtureArrayStructA"].GetShaderInput("first").GetDefaultValueAsSdfType())

    def test_array_constructor_flattened_element_defaults(self):
        stage = Usd.Stage.CreateInMemory()
        cases = [("bool3", Sdf.ValueTypeNames.BoolArray, [True, False, True]),
                 ("float2x2", Sdf.ValueTypeNames.FloatArray, [1.0, 2.0, 3.0, 4.0])]
        for element, value_type, value in cases:
            shader = self._array_shader(stage, "UsdMdlFixtureFlat" + element,
                                        element, value_type, value_type)
            self.assertTrue(shader.GetInput("first").Set(value))
        nodes, _ = self._register_array_stage(stage)
        self.assertEqual(set(nodes), {"UsdMdlFixtureFlatbool3", "UsdMdlFixtureFlatfloat2x2"})
        for element, value_type, value in cases:
            node = nodes["UsdMdlFixtureFlat" + element]
            prop = node.GetShaderInput("first")
            self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), value_type)
            self.assertEqual(list(prop.GetDefaultValueAsSdfType()), value)
            self.assertEqual(prop.GetArraySize(), len(value))
            self.assertEqual(prop.GetMetadata()["mdl.hasDefault"], "false")
            output = node.GetShaderOutput("result")
            self.assertEqual(output.GetTypeAsSdfType().GetSdfType(), value_type)
            self.assertEqual(output.GetArraySize(), 2 * len(value))
            self.assertEqual(output.GetMetadata()["mdl.arraySize"], "2")
            self.assertFalse(output.IsDynamicArray())

    def test_documented_array_constructor_definition(self):
        guide = (Path(__file__).resolve().parents[2] / "docs/shader-definitions.md").read_text(encoding="utf-8")
        section = guide.split("## Array constructor interfaces\n", 1)[1]
        text = section.split("```usda\n", 1)[1].split("```", 1)[0]
        stage = Usd.Stage.CreateInMemory()
        self.assertTrue(stage.GetRootLayer().ImportFromString(text))
        nodes, _ = self._register_array_stage(stage)
        self.assertEqual(set(nodes), {"MakePair"})
        node = nodes["MakePair"]
        for name, binding, default in (("first", "value0", 10.0), ("second", "value1", 20.0)):
            prop = node.GetShaderInput(name)
            self.assertEqual(str(prop.GetImplementationName()), binding)
            self.assertEqual(prop.GetDefaultValueAsSdfType(), default)
        self.assertEqual(str(node.GetShaderOutput("result").GetImplementationName()), "out")
        self.assertEqual(node.GetShaderOutput("result").GetArraySize(), 2)

    def test_array_constructor_implementation_module_override(self):
        for element, input_type, output_type, asset in (
                ("float", Sdf.ValueTypeNames.Float, Sdf.ValueTypeNames.FloatArray, "mdl-builtin.mdl"),
                ("StructA", Sdf.ValueTypeNames.Token, Sdf.ValueTypeNames.TokenArray, "type_checks/identity.mdl")):
            with self.subTest(element=element):
                stage = Usd.Stage.CreateInMemory()
                identifier = "UsdMdlFixtureArrayOverride" + element
                shader = self._array_shader(stage, identifier, element, input_type, output_type, asset)
                shader.SetSourceAsset(Sdf.AssetPath("missing_implementation.mdl"), "mdl")
                implementation = Sdf.AssetPath(asset) if element == "float" else self.get_module_path(asset)
                with tempfile.TemporaryDirectory() as temporary:
                    path = Path(temporary) / "array_override.usda"
                    self.assertTrue(stage.GetRootLayer().Export(str(path)))
                    nodes = UsdMdl.AddShaderDefinitionsToRegistry(Sdf.AssetPath(str(path)), implementation)
                self.assertEqual(len(nodes), 1)
                self.assertEqual(str(nodes[0].GetIdentifier()), identifier)
                self.assertEqual(str(nodes[0].GetShaderInput("first").GetImplementationName()), "value0")
                self.assertEqual(nodes[0].GetShaderOutput("result").GetTypeAsSdfType().GetSdfType(), output_type)

    def test_array_constructor_cache_native_order_and_concurrent_first_lookup(self):
        code = """
mode = sys.argv[2]
identifier = 'UsdMdlFixtureArrayWorker'
stage = Usd.Stage.CreateInMemory()
shader = fixture._array_shader(stage, identifier, reverse=mode != 'native_first')
shader.GetInput('first').Set(10.0)
def native():
    return Sdr.Registry().GetShaderNodeFromAsset(
        Sdf.AssetPath('mdl-builtin.mdl'), {}, 'T[](...)<float,2>', 'mdl')
try:
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary) / 'array.usda'
        stage.GetRootLayer().Export(str(path))
        def register():
            nodes = UsdMdl.AddShaderDefinitionsToRegistry(Sdf.AssetPath(str(path)))
            assert len(nodes) == 1, nodes
            return nodes[0]
        if mode == 'concurrent':
            barrier = threading.Barrier(6)
            def request(index):
                barrier.wait(timeout=15)
                return register() if index % 2 == 0 else native()
            with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
                nodes = list(pool.map(request, range(6)))
            interface, ordinary = nodes[:2]
            assert all(node and node.IsValid() for node in nodes)
            assert all(node.GetIdentifier() == (identifier if index % 2 == 0 else ordinary.GetIdentifier())
                       for index, node in enumerate(nodes))
        elif mode == 'native_first':
            ordinary, interface = native(), register()
        else:
            interface, ordinary = register(), native()
        assert interface.GetIdentifier() != ordinary.GetIdentifier()
        assert set(ordinary.GetShaderInputNames()) == {'value0', 'value1'}
        assert ordinary.GetShaderInput('value0').GetDefaultValueAsSdfType() is None
        assert interface.GetShaderInput('first').GetImplementationName() == 'value0'
        assert interface.GetShaderInput('second').GetImplementationName() == 'value1'
        assert interface.GetShaderInput('first').GetDefaultValueAsSdfType() == 10.0
        assert register().GetMetadata() == interface.GetMetadata()
        shader.GetInput('first').Set(20.0)
        stage.GetRootLayer().Export(str(path))
        assert not UsdMdl.AddShaderDefinitionsToRegistry(Sdf.AssetPath(str(path)))
        cached = Sdr.Registry().GetShaderNodeByIdentifierAndType(identifier, 'mdl')
        assert cached.GetShaderInput('first').GetDefaultValueAsSdfType() == 10.0
        assert native().GetMetadata() == ordinary.GetMetadata()
finally:
    UsdMdl.Shutdown()
"""
        for mode in ("interface_first", "native_first", "concurrent"):
            with self.subTest(mode=mode):
                self.assertIn("different binding or interface", self._run_array_worker(code, mode))

    def test_array_constructor_invalid_interfaces_warn_and_leave_no_registry_node(self):
        code = """
cases = [
    ('MissingInput', 'float', 'Float', 'FloatArray', 'mdl-builtin.mdl', 'value1'),
    ('DuplicateBinding', 'float', 'Float', 'FloatArray', 'mdl-builtin.mdl', 'value0'),
    ('UnknownBinding', 'float', 'Float', 'FloatArray', 'mdl-builtin.mdl', 'value2'),
    ('WrongInput', 'float', 'Int', 'FloatArray', 'mdl-builtin.mdl', 'incompatible'),
    ('WrongOutput', 'float', 'Float', 'Float', 'mdl-builtin.mdl', 'incompatible'),
    ('UnknownOutput', 'float', 'Float', 'FloatArray', 'mdl-builtin.mdl', 'missing'),
    ('Malformed', 'float', 'Float', 'FloatArray', 'mdl-builtin.mdl', 'T[](...)'),
    ('ZeroCount', 'float', 'Float', 'FloatArray', 'mdl-builtin.mdl', 'zero elements'),
    ('WrongModule', 'float', 'Float', 'FloatArray', 'type_checks/identity.mdl', 'specialization'),
    ('BooleanLength', 'bool3', 'BoolArray', 'BoolArray', 'mdl-builtin.mdl', 'length'),
    ('MatrixLength', 'float2x2', 'FloatArray', 'FloatArray', 'mdl-builtin.mdl', 'length'),
    ('InvalidEnum', 'EnumA', 'Token', 'IntArray', 'type_checks/identity.mdl', 'original MDL enum'),
    ('SymbolicEnumOutput', 'EnumA', 'Token', 'TokenArray', 'type_checks/identity.mdl', 'symbolic token arrays'),
    ('StructLiteral', 'StructA', 'Token', 'TokenArray', 'type_checks/identity.mdl', 'literal default'),
    ('MissingOutput', 'float', 'Float', 'FloatArray', 'mdl-builtin.mdl', 'expose its array return'),
]
try:
    for case, element, input_type, output_type, asset, reason in cases:
        print('BEGIN_REJECTION ' + case, file=sys.stderr, flush=True)
        stage = Usd.Stage.CreateInMemory()
        identifier = 'UsdMdlFixtureArrayRejected' + case
        shader = fixture._array_shader(stage, identifier, element,
            getattr(Sdf.ValueTypeNames, input_type), getattr(Sdf.ValueTypeNames, output_type), asset)
        if case == 'MissingInput':
            shader.GetPrim().RemoveProperty('inputs:second')
        elif case in ('DuplicateBinding', 'UnknownBinding'):
            shader.GetInput('second').SetSdrMetadata({'implementationName': reason})
        elif case == 'UnknownOutput':
            shader.GetOutput('result').SetSdrMetadata({'implementationName': reason})
        elif case == 'Malformed':
            shader.SetSourceAssetSubIdentifier('T[](...)', 'mdl')
        elif case == 'ZeroCount':
            shader.SetSourceAssetSubIdentifier('T[](...)<float,0>', 'mdl')
        elif case == 'BooleanLength':
            shader.GetInput('first').Set([True, False])
        elif case == 'MatrixLength':
            shader.GetInput('first').Set([1.0, 2.0, 3.0])
        elif case == 'InvalidEnum':
            shader.GetInput('first').Set('unknown_member')
        elif case == 'StructLiteral':
            shader.GetInput('first').Set('not_a_struct_value')
        elif case == 'MissingOutput':
            shader.GetPrim().RemoveProperty('outputs:result')
        nodes, _ = fixture._register_array_stage(stage)
        assert not nodes, (case, nodes)
        assert Sdr.Registry().GetShaderNodeByIdentifierAndType(identifier, 'mdl') is None, case
        print('END_REJECTION ' + case, file=sys.stderr, flush=True)
finally:
    UsdMdl.Shutdown()
"""
        output = self._run_array_worker(code)
        cases = [("MissingInput", "value1"), ("DuplicateBinding", "value0"),
                 ("UnknownBinding", "value2"), ("WrongInput", "incompatible"),
                 ("WrongOutput", "incompatible"), ("UnknownOutput", "missing"),
                 ("Malformed", "T[](...)"), ("ZeroCount", "zero elements"),
                 ("WrongModule", "specialization"), ("BooleanLength", "length"),
                 ("MatrixLength", "length"), ("InvalidEnum", "original MDL enum"),
                 ("SymbolicEnumOutput", "symbolic token arrays"),
                 ("StructLiteral", "literal default"),
                 ("MissingOutput", "expose its array return")]
        for case, reason in cases:
            with self.subTest(case=case):
                diagnostic = output.split("BEGIN_REJECTION " + case + "\n", 1)[1].split(
                    "END_REJECTION " + case + "\n", 1)[0]
                self.assertIn(reason, diagnostic)
                self.assertNotIn("Coding Error", diagnostic)


if __name__ == "__main__":
    run_unittest_main_exit_clean()
