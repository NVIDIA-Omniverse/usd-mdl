# SPDX-License-Identifier: Apache-2.0
"""Individually reported public USD lookup tests, in isolated CTest processes."""
import argparse
import itertools
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from mdl_subidentifier_cases import cases, legacy_material_cases


def worker(root, mode, allow, reverse, material_mismatch, prefer_struct):
    os.environ["USD_MDL_SEARCH_PATHS"] = os.pathsep.join(
        str(root / path) for path in ("root", "deps"))
    for key in ("MDL_USER_PATH", "MDL_SYSTEM_PATH", "USD_MDL_ALLOW_FIRST_OVERLOAD",
                "USD_MDL_ALLOW_MATERIAL_SIGNATURE_MISMATCH",
                "USD_MDL_PREFER_STRUCT_ELEMENTAL_CONSTRUCTOR"):
        os.environ.pop(key, None)
    if allow != "unset":
        os.environ["USD_MDL_ALLOW_FIRST_OVERLOAD"] = allow
    if prefer_struct != "unset":
        os.environ["USD_MDL_PREFER_STRUCT_ELEMENTAL_CONSTRUCTOR"] = prefer_struct
    if material_mismatch not in (None, "unset"):
        os.environ["USD_MDL_ALLOW_MATERIAL_SIGNATURE_MISMATCH"] = material_mismatch
    os.environ["USD_MDL_ADD_USER_PATHS"] = "0"
    os.environ["USD_MDL_ADD_SYSTEM_PATHS"] = "0"
    os.environ["USD_MDL_DISCOVER_ON_STARTUP"] = str(int(mode == "eager"))

    from pxr import Ar, Sdf, Sdr, Tf, Usd, UsdShade
    import UsdMdl

    all_cases = (cases() if material_mismatch is None else
                 legacy_material_cases(material_mismatch == "1"))
    if not all_cases:
        raise AssertionError("empty sub-identifier case table")
    stage = Usd.Stage.CreateInMemory()
    index = itertools.count()

    def source(module):
        return root / ((module[1:] if module.startswith("@") else "root/" + module) + ".mdl")

    def lookup(request, module="pkg/main"):
        shader = UsdShade.Shader.Define(stage, f"/Shader{next(index)}")
        shader.SetSourceAsset(Sdf.AssetPath(str(source(module))), "mdl")
        shader.SetSourceAssetSubIdentifier(request, "mdl")
        try:
            return shader.GetShaderNodeForSourceType("mdl")
        except Tf.ErrorException as error:
            if "returned null" not in str(error):
                raise
            return None

    class LookupTests(unittest.TestCase):
        def check_module_asset(self, node, module):
            # Open the returned identifier directly, without resolving it again
            # or giving Ar the MDL search roots. Check content, not just spelling.
            uri = node.GetResolvedImplementationURI()
            asset = Ar.GetResolver().OpenAsset(Ar.ResolvedPath(uri))
            self.assertTrue(asset, f"implementation identifier is not Ar-openable: {uri!r}")
            self.assertEqual(asset.GetBuffer(), source(module).read_bytes())
            self.assertEqual(node.GetResolvedDefinitionURI(), uri)
            self.assertEqual(node.GetMetadata()["mdl:moduleUsdIdentifier"],
                             "" if module.startswith("@") else module + ".mdl")
            self.assertEqual(node.GetMetadata()["mdl:is_embedded_module"], "false")

        @classmethod
        def setUpClass(cls):
            if mode == "registered":
                modules = {case.module for case in all_cases}
                modules.update(("materials/paint", "@deps/scene/materials/paint"))
                for module in sorted(modules):
                    nodes = UsdMdl.AddModuleToRegistry(Sdf.AssetPath(str(source(module))))
                    if not nodes or not all(node and node.IsValid() for node in nodes):
                        raise AssertionError(f"fixture module did not register valid nodes: {module}")
            elif mode == "eager":
                registry = Sdr.Registry()
                discovered = [node for node in registry.GetShaderNodesByFamily()
                              if node and node.GetSourceType() == "mdl"]
                if not all(node and node.IsValid() for node in discovered):
                    raise AssertionError("eager discovery returned an invalid node")
                expected = {"mdl::pkg::main::" + signature for signature in (
                    "create_layer(color,float)", "eligible(float)", "hidden_paint(float)",
                    "Local.i(::pkg::main::Local)")}
                found = {node.GetMetadata().get("mdl:mdl_db_definition_name")
                         for node in discovered}
                if not expected.issubset(found):
                    raise AssertionError(f"eager discovery missed definitions: {expected - found}")

        def check_case(self, case):
            node = lookup(case.request, case.module)
            if case.canonical is None:
                self.assertFalse(node and node.IsValid(), case.request)
                return
            self.assertTrue(node and node.IsValid(), case.request)
            md = node.GetMetadata()
            self.assertEqual(md["mdl:subIdentifier"], case.canonical)
            self.assertEqual(set(node.GetShaderInputNames()), set(case.inputs))
            self.assertTrue(Path(node.GetResolvedImplementationURI()).samefile(source(case.module)))
            self.check_module_asset(node, case.module)
            if case.module.startswith("@"):
                # Off-path SDK names encode the storage location. Assert portable
                # authoring data separately from these runtime-only DB identities.
                module_db = md["mdl:mdl_db_module_name"]
                self.assertTrue(module_db.startswith("mdl::file%"), module_db)
                self.assertTrue(module_db.endswith("::main"), module_db)
                owner = module_db[3:]
                signature = case.signature.format(owner=owner, package=owner.rsplit("::", 1)[0])
            else:
                module_db = "mdl::" + case.module.replace("/", "::")
                self.assertEqual(md["mdl:mdl_db_module_name"], module_db)
                signature = case.signature
            self.assertEqual(md["mdl:mdl_db_definition_name"], module_db + "::" + signature)
            authored = lookup(case.canonical, case.module)
            self.assertTrue(authored and authored.IsValid())
            self.assertEqual(authored.GetMetadata()["mdl:mdl_db_definition_name"],
                             md["mdl:mdl_db_definition_name"])

        def test_hidden_metadata_and_overloads(self):
            for request, hidden in (("eligible(float)", "true"),
                                    ("hidden_paint", "true"),
                                    ("Local.i", "true"),
                                    ("eligible(int)", "false"),
                                    ("paint", "false")):
                with self.subTest(request=request):
                    node = lookup(request)
                    self.assertTrue(node and node.IsValid())
                    self.assertEqual(node.GetMetadata()[UsdMdl.NodeMetadata.Hidden], hidden)
                    if hidden == "true":
                        self.assertIn("hidden", node.GetMetadata()[UsdMdl.Metadata.Annotation])
            node = lookup("eligible")
            if allow == "1":
                self.assertTrue(node and node.IsValid())
                self.assertEqual(node.GetMetadata()[UsdMdl.NodeMetadata.MdlDbDefinitionName],
                                 "mdl::pkg::main::eligible(int)")
            else:
                self.assertFalse(node and node.IsValid())

        def test_module_isolation(self):
            # Same definition name/signature in different modules must not alias.
            first = lookup("create_layer(color,float)")
            second = lookup("create_layer(color,float)", "pkg/second")
            again = lookup("create_layer(color,float)")
            self.assertTrue(first and second and again)
            self.assertEqual(second.GetMetadata()["mdl:mdl_db_definition_name"],
                             "mdl::pkg::second::create_layer(color,float)")
            self.assertEqual(first.GetIdentifier(), again.GetIdentifier())
            self.assertNotEqual(first.GetIdentifier(), second.GetIdentifier())
            self.assertAlmostEqual(float(second.GetShaderInput("alpha").GetDefaultValue()), 0.8,
                             msg="unexpected fixture default")

        def test_missing_module(self):
            self.assertFalse(lookup("create_layer", "pkg/missing"))

        def test_search_path_source_asset(self):
            # USD has no MDL search-root context here. The plugin must still
            # resolve an authored module path through its configured MDL roots.
            shader = UsdShade.Shader.Define(stage, f"/Shader{next(index)}")
            shader.SetSourceAsset(Sdf.AssetPath("pkg/main.mdl"), "mdl")
            self.assertEqual(shader.GetSourceAsset("mdl").resolvedPath, "")
            for request, canonical, signature in (
                ("create_layer", "create_layer", "create_layer(color,float)"),
                ("create_layer(color,float)", "create_layer", "create_layer(color,float)"),
                ("read_local(Local)", "read_local", "read_local(::pkg::main::Local)"),
            ):
                with self.subTest(request=request):
                    shader.SetSourceAssetSubIdentifier(request, "mdl")
                    node = shader.GetShaderNodeForSourceType("mdl")
                    self.assertTrue(node and node.IsValid())
                    self.assertEqual(node.GetMetadata()["mdl:subIdentifier"], canonical)
                    self.assertEqual(node.GetMetadata()["mdl:mdl_db_definition_name"],
                                     "mdl::pkg::main::" + signature)
                    self.check_module_asset(node, "pkg/main")

            # Exercise the pre-parsed registration path with the same unresolved
            # source asset, as well as UsdShade's on-demand parser above.
            nodes = UsdMdl.AddModuleToRegistry(Sdf.AssetPath("pkg/main.mdl"))
            selected = [node for node in nodes if node.GetMetadata().get("mdl:subIdentifier")
                        == "create_layer"]
            self.assertEqual(len(selected), 1)
            self.assertTrue(selected[0].IsValid())
            self.check_module_asset(selected[0], "pkg/main")
            shader.SetSourceAssetSubIdentifier("create_layer(color,int)", "mdl")
            try:
                node = shader.GetShaderNodeForSourceType("mdl")
            except Tf.ErrorException as error:
                self.assertIn("returned null", str(error))
            else:
                self.assertFalse(node and node.IsValid())

        def test_layer_local_module_overrides_search_path_module(self):
            library = root / "root/materials/paint.mdl"
            local = root / "deps/scene/materials/paint.mdl"
            self.assertNotEqual(library.read_bytes(), local.read_bytes())

            # Both candidates are under configured MDL roots, with distinct
            # qualified names. Only the USD layer anchor changes which file
            # the identical weak-relative source asset selects.
            for scene_dir, expected, marker, module_db in (
                (root / "no-local-module", library, 11, "mdl::materials::paint"),
                (local.parent.parent, local, 29, "mdl::scene::materials::paint"),
            ):
                with self.subTest(layer_directory=scene_dir.name):
                    scene_dir.mkdir(parents=True, exist_ok=True)
                    local_stage = Usd.Stage.CreateNew(str(scene_dir / "collision.usda"))
                    shader = UsdShade.Shader.Define(local_stage, "/Shader")
                    shader.SetSourceAsset(Sdf.AssetPath("materials/paint.mdl"), "mdl")
                    shader.SetSourceAssetSubIdentifier("paint", "mdl")
                    asset = shader.GetSourceAsset("mdl")
                    if expected == library:
                        self.assertFalse((scene_dir / "materials/paint.mdl").exists())
                        self.assertEqual(asset.resolvedPath, "")
                    else:
                        self.assertTrue(Path(asset.resolvedPath).samefile(local))

                    node = shader.GetShaderNodeForSourceType("mdl")
                    self.assertTrue(node and node.IsValid())
                    self.assertEqual(node.GetShaderInput("marker").GetDefaultValue(), marker)
                    self.assertEqual(node.GetMetadata()["mdl:mdl_db_definition_name"],
                                     module_db + "::paint(int)")
                    uri = node.GetResolvedImplementationURI()
                    self.assertTrue(Path(uri).samefile(expected))
                    resolved_asset = Ar.GetResolver().OpenAsset(Ar.ResolvedPath(uri))
                    self.assertTrue(resolved_asset)
                    self.assertEqual(resolved_asset.GetBuffer(), expected.read_bytes())

        def test_relative_source_asset(self):
            # Keep the USD layer anchor: authoring an SdfAssetPath into an
            # anonymous stage discards its precomputed resolved-path component.
            relative_stage = Usd.Stage.CreateNew(str(root / "relative.usda"))
            shader = UsdShade.Shader.Define(relative_stage, "/Shader")
            shader.SetSourceAsset(Sdf.AssetPath("./root/pkg/main.mdl"), "mdl")
            for request in ("create_layer(color,float)", "create_layer"):
                shader.SetSourceAssetSubIdentifier(request, "mdl")
                node = shader.GetShaderNodeForSourceType("mdl")
                self.assertTrue(node and node.IsValid())
                self.assertEqual(node.GetMetadata()["mdl:mdl_db_definition_name"],
                                 "mdl::pkg::main::create_layer(color,float)")
                self.check_module_asset(node, "pkg/main")
            relative_stage.GetRootLayer().Save()

        def test_missing_strict_relative_module_does_not_use_search_paths(self):
            layer_dir = root / "scene" / "nested"
            layer_dir.mkdir(parents=True, exist_ok=True)
            local_stage = Usd.Stage.CreateNew(str(layer_dir / "missing-local.usda"))
            self.assertTrue(source("pkg/main").is_file())

            # Positive control: the same module and definition are available
            # through MDL search paths when the asset permits search lookup.
            library_shader = UsdShade.Shader.Define(local_stage, "/Library")
            library_shader.SetSourceAsset(Sdf.AssetPath("pkg/main.mdl"), "mdl")
            library_shader.SetSourceAssetSubIdentifier("create_layer", "mdl")
            library_node = library_shader.GetShaderNodeForSourceType("mdl")
            self.assertTrue(library_node and library_node.IsValid())
            self.assertEqual(library_node.GetMetadata()["mdl:mdl_db_definition_name"],
                             "mdl::pkg::main::create_layer(color,float)")

            for i, asset in enumerate(("./pkg/main.mdl", "../pkg/main.mdl")):
                with self.subTest(asset=asset):
                    self.assertFalse((layer_dir / asset).exists())
                    shader = UsdShade.Shader.Define(local_stage, f"/Local{i}")
                    shader.SetSourceAsset(Sdf.AssetPath(asset), "mdl")
                    shader.SetSourceAssetSubIdentifier("create_layer", "mdl")
                    self.assertEqual(shader.GetSourceAsset("mdl").resolvedPath, "")
                    # Even the already loaded library module must not satisfy
                    # this explicitly layer-relative request. Some USD versions
                    # report a parser miss as a Tf exception instead of None.
                    try:
                        node = shader.GetShaderNodeForSourceType("mdl")
                    except Tf.ErrorException as error:
                        self.assertIn("returned null", str(error))
                    else:
                        self.assertFalse(node and node.IsValid())

        def test_bare_overload_policy(self):
            node = lookup("pick")
            if allow == "1":
                self.assertTrue(node and node.IsValid())
                self.assertEqual(node.GetMetadata()["mdl:subIdentifier"], "pick()")
                self.assertEqual(node.GetMetadata()["mdl:mdl_db_definition_name"], "mdl::pkg::main::pick()")
            else:
                self.assertFalse(node and node.IsValid())

        def test_bare_struct_constructor_policy(self):
            node = lookup("Local")
            if allow != "1":
                self.assertFalse(node and node.IsValid())
            else:
                self.assertTrue(node and node.IsValid())
                expected = ("Local(int,float)" if prefer_struct == "1"
                            else "Local(::pkg::main::Local)")
                self.assertEqual(node.GetMetadata()["mdl:mdl_db_definition_name"],
                                 "mdl::pkg::main::" + expected)
                if prefer_struct == "1":
                    self.assertEqual(set(node.GetShaderInputNames()), {"i", "f"})
                    self.assertEqual(node.GetShaderInput("i").GetDefaultValue(), 7)
                    self.assertAlmostEqual(node.GetShaderInput("f").GetDefaultValue(), 0.5)

            # Neither opt-in may redirect an explicit constructor signature.
            for request, signature in (("Local(Local)", "Local(::pkg::main::Local)"),
                                       ("Local(int,float)", "Local(int,float)")):
                with self.subTest(request=request):
                    exact = lookup(request)
                    self.assertTrue(exact and exact.IsValid())
                    self.assertEqual(exact.GetMetadata()["mdl:mdl_db_definition_name"],
                                     "mdl::pkg::main::" + signature)
            self.assertFalse(lookup("Local(int,int)"))

            ordinary = lookup("make_local")
            if allow == "1":
                self.assertTrue(ordinary and ordinary.IsValid())
                self.assertEqual(ordinary.GetMetadata()["mdl:mdl_db_definition_name"],
                                 "mdl::pkg::main::make_local(int)")
                self.assertEqual(ordinary.GetShaderInput("value").GetDefaultValue(), 17)
            else:
                self.assertFalse(ordinary and ordinary.IsValid())

        def test_no_zero_parameter_overload(self):
            self.assertFalse(lookup("collision()"))

    # Each row is a unittest, not a hidden iteration within one test_contract.
    names = []
    for number, case in enumerate(all_cases):
        name = f"test_{number:03}_{case.group}"
        def check(self, case=case):
            self.check_case(case)
        check.__doc__ = f"{case.module}: {case.request!r} -> {case.canonical or 'reject'}"
        setattr(LookupTests, name, check)
        names.append(name)
    if reverse:
        names.reverse()
    # Discover the additional methods so a new test cannot silently be omitted.
    names += sorted(set(unittest.defaultTestLoader.getTestCaseNames(LookupTests)) - set(names))
    suite = unittest.TestSuite(LookupTests(name) for name in names)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    UsdMdl.Shutdown()
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(0 if result.testsRun > 0 and result.wasSuccessful() else 1)


def run_isolated(root, mode, allow, order, material_mismatch=None, prefer_struct="unset"):
    command = [sys.executable, __file__, "--worker", "--root", str(root),
               "--mode", mode, "--allow", allow, "--order", order,
               "--prefer-struct", prefer_struct]
    if material_mismatch is not None:
        command += ["--material-mismatch", material_mismatch]
    run = subprocess.run(command,
                         capture_output=True, text=True, timeout=180)
    print(run.stdout + run.stderr, end="", flush=True)
    if run.returncode:
        raise SystemExit(run.returncode)
    expected = ("Defaulting to: 'pick()'." if allow == "1" else
                "ambiguous MDL sub-identifier 'pick'; use one of:")
    if expected not in run.stderr:
        raise AssertionError(f"missing required diagnostic: {expected}")
    struct_warning = ("ambiguous MDL struct sub-identifier 'Local'; selected elemental "
                      "constructor 'Local(int,float)' because "
                      "USD_MDL_PREFER_STRUCT_ELEMENTAL_CONSTRUCTOR "
                      "and USD_MDL_ALLOW_FIRST_OVERLOAD are enabled.")
    if (struct_warning in run.stderr) != (allow == "1" and prefer_struct == "1"):
        raise AssertionError("unexpected or missing struct preference diagnostic")
    warnings = [line for line in run.stderr.splitlines()
                if "ignoring mismatched material signature" in line]
    expected_fallbacks = ([case for case in legacy_material_cases(True)
                           if case.group == "legacy_material_fallback"]
                          if material_mismatch == "1" else [])
    if len(warnings) != len(expected_fallbacks):
        raise AssertionError(f"expected {len(expected_fallbacks)} material warnings, got {warnings}")
    for case in expected_fallbacks:
        expected = (f"ignoring mismatched material signature '{case.request}'; "
                    f"selected '{case.signature}'")
        if not any(expected in line for line in warnings):
            raise AssertionError(f"missing material diagnostic: {expected}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", action="store_true")
    parser.add_argument("--root", type=Path, default=Path(os.environ.get("PXR_TEST_DIR", ".")))
    parser.add_argument("--mode", choices=["cold", "registered", "eager"], default="cold")
    parser.add_argument("--allow", choices=["unset", "0", "1"], default="unset")
    parser.add_argument("--prefer-struct", choices=["unset", "0", "1"], default="unset")
    parser.add_argument("--material-mismatch", choices=["unset", "0", "1"],
                        help="Run focused legacy-material cases with this option value")
    parser.add_argument("--order", choices=["forward", "reverse"], default="forward")
    parser.add_argument("--relocate", action="store_true")
    args = parser.parse_args()
    if args.worker:
        worker(args.root, args.mode, args.allow, args.order == "reverse",
               args.material_mismatch, args.prefer_struct)
    elif args.relocate:
        # Includes both search-path packages and the standalone sibling bundle.
        run_isolated(args.root, "cold", "unset", "forward")
        with tempfile.TemporaryDirectory(prefix="usdmdl relocated ") as temp:
            copied = Path(temp) / "different location"
            shutil.copytree(args.root, copied)
            run_isolated(copied, "cold", "unset", "reverse")
    else:
        run_isolated(args.root, args.mode, args.allow, args.order,
                     args.material_mismatch, args.prefer_struct)
