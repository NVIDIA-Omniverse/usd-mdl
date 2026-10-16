# SPDX-License-Identifier: Apache-2.0
"""Embedded module authoring and optional proxy files through public USD APIs."""
import argparse
import itertools
import os
from pathlib import Path
import re
import subprocess
import sys
import unittest


def worker(root, dummy, mode, content, module_set):
    first, second = root / "mdl", root / "second"
    second.mkdir(exist_ok=True)
    os.environ["USD_MDL_SEARCH_PATHS"] = os.pathsep.join(map(str, (first, second)))
    for key in ("MDL_USER_PATH", "MDL_SYSTEM_PATH", "PXR_AR_DEFAULT_SEARCH_PATH"):
        os.environ.pop(key, None)
    os.environ["USD_MDL_ADD_USER_PATHS"] = "0"
    os.environ["USD_MDL_ADD_SYSTEM_PATHS"] = "0"
    os.environ["USD_MDL_DISCOVER_ON_STARTUP"] = str(int(mode == "eager"))
    definitions = (("math", "sqrt(float)", "sqrt(float)"),
                   ("state", "normal", "normal()"),
                   ("base", "texture_return.tint",
                    "texture_return.tint(::base::texture_return)"))
    if module_set == "builtins":
        definitions = tuple(("mdl-builtin", signature, signature) for signature in (
            "float(float)", "float(int)", "float3(float,float,float)", "color(float,float,float)",
            "int(intensity_mode)", "material_emission(edf,color,intensity_mode)",
            "texture_2d(string,::tex::gamma_mode,string)")) + (
                ("mdl-builtin", "intensity_mode", "intensity_mode(intensity_mode)"),)
        # A suffix match would incorrectly redirect this ordinary module too.
        (first / "other-mdl-builtin.mdl").write_text(
            "mdl 1.7;\nexport float value(float input = 0.625) { return input; }\n",
            encoding="utf-8")
    module_names = {module for module, _, _ in definitions}

    def definition_db_name(module, signature):
        return "mdl::" + ("" if module == "mdl-builtin" else module + "::") + signature

    if dummy != "absent":
        for directory in (first, second):
            for module in module_names:
                text = {
                    "proxy": f"// {directory.name} proxy\nmdl 1.7;\nexport using ::{module} import *;\n",
                    "empty": "",
                    "invalid": f"// {directory.name} invalid proxy\nmdl 1.7;\nexport float broken( = ;\n",
                }[content]
                if module == "mdl-builtin" and content == "proxy":
                    text = f"// {directory.name} proxy\nmdl 1.7;\nexport float dummy() {{ return 29.0; }}\n"
                (directory / (module + ".mdl")).write_text(text, encoding="utf-8")

    from pxr import Ar, Sdf, Sdr, Usd, UsdShade
    import UsdMdl

    # Reverse Ar's root order to verify that a selected USD proxy is retained,
    # rather than replaced by the first matching MDL search-root file.
    context = Ar.ResolverContext(Ar.DefaultResolverContext(
        [str(second), str(first)] if dummy == "ar-context" else []))
    stage = Usd.Stage.CreateInMemory("embedded.usda", pathResolverContext=context)
    index = itertools.count()

    def shader_for(asset, subid):
        shader = UsdShade.Shader.Define(stage, f"/Shader{next(index)}")
        shader.SetSourceAsset(Sdf.AssetPath(asset), "mdl")
        shader.SetSourceAssetSubIdentifier(subid, "mdl")
        return shader

    class EmbeddedTests(unittest.TestCase):
        @classmethod
        def setUpClass(cls):
            cls.nodes = {}
            registered = {}
            discovered = None
            if mode == "eager":
                discovered = {node.GetMetadata().get("mdl:mdl_db_definition_name")
                              for node in Sdr.Registry().GetShaderNodesByFamily()
                              if node and node.IsValid()}
            for module, subid, signature in definitions:
                shader = shader_for(module + ".mdl", subid)
                source = shader.GetSourceAsset("mdl")
                if dummy == "ar-context":
                    assert Path(source.resolvedPath).samefile(second / (module + ".mdl"))
                else:
                    assert source.resolvedPath == "", source
                if mode == "registered":
                    if module not in registered:
                        registered[module] = UsdMdl.AddModuleToRegistry(source)
                        assert all(node and node.IsValid() for node in registered[module])
                    nodes = registered[module]
                    selected = [n for n in nodes if n.GetMetadata().get("mdl:mdl_db_definition_name")
                                == definition_db_name(module, signature)]
                    assert len(selected) == 1
                    cls.nodes[module, subid] = selected[0]
                else:
                    # Eager discovery is triggered by the registry and scans the
                    # real proxies when present; no proxy is required for lookup.
                    if mode == "eager" and dummy != "absent":
                        assert definition_db_name(module, signature) in discovered
                    cls.nodes[module, subid] = shader.GetShaderNodeForSourceType("mdl")
                assert cls.nodes[module, subid] and cls.nodes[module, subid].IsValid()

        def test_metadata(self):
            self.assertEqual(UsdMdl.NodeMetadata.IsEmbeddedModule, "mdl:is_embedded_module")
            for module, subid, signature in definitions:
                with self.subTest(module=module):
                    metadata = self.nodes[module, subid].GetMetadata()
                    self.assertEqual(metadata["mdl:is_embedded_module"], "true")
                    self.assertEqual(metadata["mdl:mdl_db_module_name"],
                                     "mdl::%3Cbuiltins%3E" if module == "mdl-builtin" else f"mdl::{module}")
                    self.assertEqual(metadata["mdl:moduleUsdIdentifier"], module + ".mdl")
                    self.assertEqual(metadata["mdl:subIdentifier"], subid)
                    self.assertEqual(metadata["mdl:mdl_db_definition_name"],
                                     definition_db_name(module, signature))

        def test_resolved_proxy(self):
            for module, subid, _ in definitions:
                with self.subTest(module=module):
                    node = self.nodes[module, subid]
                    uri = node.GetResolvedImplementationURI()
                    self.assertEqual(node.GetResolvedDefinitionURI(), uri)
                    if dummy == "absent":
                        self.assertEqual(uri, "")
                        self.assertFalse((first / (module + ".mdl")).exists())
                    else:
                        expected = (second if dummy == "ar-context" else first) / (module + ".mdl")
                        resolved = Ar.GetResolver().Resolve(str(expected))
                        asset = Ar.GetResolver().OpenAsset(resolved)
                        self.assertTrue(asset)
                        self.assertEqual(asset.GetSize(), expected.stat().st_size)
                        if content == "empty":
                            self.assertEqual(expected.read_bytes(), b"")
                            self.assertIsNone(asset.GetBuffer())
                            # The MDL-root fallback requires an Ar buffer; a
                            # USD-resolved proxy only needs an openable asset.
                            self.assertEqual(uri, "" if dummy == "mdl-roots" else str(resolved))
                        else:
                            self.assertEqual(uri, str(resolved))
                            self.assertEqual(asset.GetBuffer(), expected.read_bytes())

        def test_authoring_from_metadata(self):
            for module, subid, signature in definitions:
                with self.subTest(module=module):
                    metadata = self.nodes[module, subid].GetMetadata()
                    shader = shader_for(metadata["mdl:moduleUsdIdentifier"],
                                        metadata["mdl:subIdentifier"])
                    node = shader.GetShaderNodeForSourceType("mdl")
                    self.assertTrue(node and node.IsValid())
                    self.assertEqual(node.GetMetadata()["mdl:mdl_db_definition_name"],
                                     definition_db_name(module, signature))

        def test_file_backed_module(self):
            node = shader_for("regular.mdl", "value").GetShaderNodeForSourceType("mdl")
            self.assertTrue(node and node.IsValid())
            self.assertEqual(node.GetMetadata()["mdl:is_embedded_module"], "false")
            self.assertEqual(node.GetMetadata()["mdl:moduleUsdIdentifier"], "regular.mdl")
            self.assertEqual(node.GetShaderInput("input").GetDefaultValue(), 0.25)

        @unittest.skipUnless(module_set == "builtins", "builtin enum metadata")
        def test_builtin_enums(self):
            intensity_options = [("intensity_radiant_exitance", "0"), ("intensity_power", "1")]

            def check_enum(prop, symbol, options, default=None, has_default=None):
                self.assertTrue(prop)
                self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), Sdf.ValueTypeNames.Int)
                self.assertEqual(prop.GetMetadata()["renderType"], "enum")
                self.assertEqual(prop.GetMetadata()["mdl.symbol"], symbol)
                self.assertEqual(prop.GetOptions(), options)
                self.assertEqual(prop.GetDefaultValue(), default)
                self.assertEqual(prop.GetMetadata().get("mdl.hasDefault"), has_default)
                self.assertEqual(prop.GetMetadata().get("mdl.expressionKind"),
                                 "Constant" if has_default == "true" else None)

            copy = self.nodes["mdl-builtin", "intensity_mode"]
            check_enum(copy.GetShaderInput("value"), "::intensity_mode", intensity_options, 0, "true")
            check_enum(copy.GetShaderOutput("out"), "::intensity_mode", intensity_options)
            # The SDK symbol has a leading namespace, but its signature spelling
            # is the builtin type name, as for float and color.
            explicit = shader_for("mdl-builtin.mdl", "intensity_mode(intensity_mode)")
            self.assertEqual(explicit.GetShaderNodeForSourceType("mdl").GetMetadata()[
                "mdl:mdl_db_definition_name"], "mdl::intensity_mode(intensity_mode)")
            cast = self.nodes["mdl-builtin", "int(intensity_mode)"]
            check_enum(cast.GetShaderInput("value"), "::intensity_mode", intensity_options,
                       has_default="false")
            emission = self.nodes["mdl-builtin", "material_emission(edf,color,intensity_mode)"]
            check_enum(emission.GetShaderInput("mode"), "::intensity_mode", intensity_options, 0, "true")
            texture = self.nodes["mdl-builtin", "texture_2d(string,::tex::gamma_mode,string)"]
            check_enum(texture.GetShaderInput("gamma"), "::tex::gamma_mode",
                       [("gamma_default", "0"), ("gamma_linear", "1"), ("gamma_srgb", "2")], 0, "true")

        @unittest.skipUnless(module_set == "builtins", "builtin alias boundary")
        def test_similarly_named_module(self):
            node = shader_for("other-mdl-builtin.mdl", "value").GetShaderNodeForSourceType("mdl")
            self.assertTrue(node and node.IsValid())
            metadata = node.GetMetadata()
            self.assertEqual(metadata["mdl:is_embedded_module"], "false")
            self.assertEqual(metadata["mdl:moduleUsdIdentifier"], "other-mdl-builtin.mdl")
            self.assertEqual(node.GetShaderInput("input").GetDefaultValue(), 0.625)

        @unittest.skipUnless(module_set == "builtins" and dummy != "absent", "builtin proxy path")
        def test_absolute_dummy_path(self):
            expected = second / "mdl-builtin.mdl"
            # Both native separators and USD-style slashes select the same alias.
            for path in {str(expected), expected.as_posix()}:
                with self.subTest(path=path):
                    node = shader_for(path, "float(int)").GetShaderNodeForSourceType("mdl")
                    self.assertTrue(node and node.IsValid())
                    metadata = node.GetMetadata()
                    self.assertEqual(metadata["mdl:moduleUsdIdentifier"], "mdl-builtin.mdl")
                    self.assertEqual(metadata["mdl:mdl_db_definition_name"], "mdl::float(int)")
                    self.assertEqual(node.GetResolvedImplementationURI(),
                                     str(Ar.GetResolver().Resolve(str(expected))))

    try:
        result = unittest.TextTestRunner(verbosity=2).run(
            unittest.defaultTestLoader.loadTestsFromTestCase(EmbeddedTests))
    finally:
        UsdMdl.Shutdown()
    return 0 if result.testsRun > 0 and result.wasSuccessful() else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--dummy", choices=("absent", "mdl-roots", "ar-context"), required=True)
    parser.add_argument("--mode", choices=("cold", "registered", "eager"), required=True)
    parser.add_argument("--content", choices=("proxy", "empty", "invalid"), default="proxy")
    parser.add_argument("--module-set", choices=("standard", "builtins"), default="standard")
    parser.add_argument("--worker", action="store_true")
    args = parser.parse_args()
    if args.worker:
        raise SystemExit(worker(Path(os.environ["PXR_TEST_DIR"]), args.dummy, args.mode,
                                args.content, args.module_set))
    # Capture native diagnostics too; Python redirect_stderr cannot see TF/SDK logs.
    run = subprocess.run([sys.executable, __file__, *sys.argv[1:], "--worker"],
                         capture_output=True, text=True, timeout=120)
    print(run.stdout + run.stderr, end="", flush=True)
    if run.returncode:
        raise SystemExit(run.returncode)
    diagnostics = [line for line in (run.stdout + run.stderr).splitlines()
                   if re.search(r"(?i)\b(warning|warn|error|fatal)\b", line)]
    if diagnostics:
        raise AssertionError(f"successful embedded-module lookup emitted diagnostics: {diagnostics}")
