# SPDX-License-Identifier: Apache-2.0
"""Validate generated interfaces against the selected SDK's canonical stage."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from pxr import Gf, Sdf, Usd, UsdShade


_ARGS = None


class GenerateShaderDefinitionsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="usdMdlGenerate_")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.output = self.directory / "UsdPreviewSurface.usda"
        self.provenance = self.directory / "UsdPreviewSurface.provenance.json"

    def _run(self, *, source: Path | None = None, version: str | None = None,
             output: Path | None = None, license_source: Path | None = None,
             notice_source: Path | None = None) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, "-I", _ARGS.generator,
             "--input", str(source or Path(_ARGS.source)),
             "--output", str(output or self.output),
             "--provenance", str(self.provenance), "--license", str(license_source or Path(_ARGS.license)),
             "--notice", str(notice_source or Path(_ARGS.notice)),
             "--usd-version", version or _ARGS.usd_version,
             "--usd-python", _ARGS.usd_python, "--usd-prefix", _ARGS.usd_prefix],
            capture_output=True, text=True, timeout=45)

    def _generate(self, **kwargs) -> Usd.Stage:
        result = self._run(**kwargs)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        stage = Usd.Stage.Open(str(self.output))
        self.assertIsNotNone(stage)
        return stage

    def _copy_source(self) -> Usd.Stage:
        layer = Sdf.Layer.OpenAsAnonymous(_ARGS.source)
        self.assertIsNotNone(layer)
        stage = Usd.Stage.Open(layer)
        self.assertIsNotNone(stage)
        return stage

    def _save_source(self, stage: Usd.Stage) -> Path:
        path = self.directory / "canonical.usda"
        self.assertTrue(stage.GetRootLayer().Export(str(path)))
        return path

    def _assert_rejected(self, source: Path, diagnostic: str) -> None:
        result = self._run(source=source)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(diagnostic, result.stderr)
        self.assertFalse(self.output.exists())
        self.assertFalse(self.provenance.exists())

    def test_canonical_roles_defaults_options_metadata_and_inheritance_survive(self) -> None:
        source = self._copy_source()
        generated = self._generate()
        source_shaders = {str(prim.GetPath()): prim for prim in source.TraverseAll()
                          if not prim.IsAbstract() and prim.IsA(UsdShade.Shader)}
        generated_shaders = {str(prim.GetPath()): prim for prim in generated.TraverseAll()
                             if not prim.IsAbstract() and prim.IsA(UsdShade.Shader)}
        self.assertEqual(set(generated_shaders), set(source_shaders))
        for path, original in source_shaders.items():
            patched = generated_shaders[path]
            with self.subTest(shader=path):
                self.assertEqual(patched.GetMetadata("documentation"), original.GetMetadata("documentation"))
                self.assertEqual(patched.GetMetadata("sdrMetadata"), original.GetMetadata("sdrMetadata"))
                self.assertEqual(patched.GetMetadata("inheritPaths"), original.GetMetadata("inheritPaths"))
                self.assertEqual(UsdShade.Shader(patched).GetIdAttr().Get(), UsdShade.Shader(original).GetIdAttr().Get())
                shader = UsdShade.Shader(patched)
                self.assertEqual(shader.GetSourceAsset("mdl").path, "UsdPreviewSurface.mdl")
                self.assertEqual(shader.GetImplementationSource(), "sourceAsset")
                for attribute in original.GetAttributes():
                    name = str(attribute.GetName())
                    if not name.startswith(("inputs:", "outputs:")):
                        continue
                    actual = patched.GetAttribute(name)
                    self.assertTrue(actual, (path, name))
                    self.assertEqual(actual.GetTypeName(), attribute.GetTypeName(), (path, name))
                    self.assertEqual(actual.Get(), attribute.Get(), (path, name))
                    for key in ("documentation", "displayName", "displayGroup", "allowedTokens", "connectability"):
                        self.assertEqual(actual.GetMetadata(key), attribute.GetMetadata(key), (path, name, key))
                    # The binding patch may add metadata, but must retain every canonical entry.
                    for key, value in (attribute.GetMetadata("sdrMetadata") or {}).items():
                        self.assertEqual((actual.GetMetadata("sdrMetadata") or {}).get(key), value, (path, name, key))
        for prim in generated.TraverseAll():
            self.assertFalse(any(str(attr.GetName()).startswith("info:glslfx:")
                                 for attr in prim.GetAttributes()), str(prim.GetPath()))
        self.assertTrue(generated.GetPrimAtPath("/UsdPrimvarReader").IsAbstract())
        reader = generated.GetPrimAtPath("/UsdPrimvarReader_normal")
        self.assertTrue(reader.GetAttribute("inputs:varname"))
        self.assertEqual(reader.GetAttribute("outputs:result").GetTypeName(), Sdf.ValueTypeNames.Normal3f)
        self.assertEqual(generated.GetPrimAtPath("/UsdPreviewSurface").GetAttribute("inputs:normal").GetTypeName(), Sdf.ValueTypeNames.Normal3f)
        self.assertEqual(generated.GetPrimAtPath("/UsdUVTexture").GetAttribute("inputs:wrapS").GetMetadata("connectability"), "interfaceOnly")
        self.assertEqual(generated.GetPrimAtPath("/UsdTransform2d").GetAttribute("inputs:in").GetMetadata("sdrMetadata")["implementationName"], "st")
        self.assertEqual(generated.GetPrimAtPath("/UsdPreviewSurface").GetAttribute("outputs:displacement").GetMetadata("sdrMetadata")["implementationName"], "out")

    def test_provenance_records_exact_inputs_and_generated_bytes(self) -> None:
        self._generate()
        manifest = json.loads(self.provenance.read_text(encoding="utf-8"))
        sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
        self.assertEqual(manifest["schemaVersion"], 1)
        self.assertEqual(manifest["usdVersion"], [int(value) for value in _ARGS.usd_version.split(".")])
        self.assertEqual(manifest["sourceAsset"], Path(_ARGS.source).resolve().as_posix())
        self.assertEqual(manifest["sourceSha256"], sha(_ARGS.source))
        self.assertEqual(manifest["generatorSha256"], sha(_ARGS.generator))
        self.assertEqual(manifest["licenseSha256"], sha(_ARGS.license))
        self.assertEqual(manifest["noticeSha256"], sha(_ARGS.notice))
        self.assertEqual(manifest["outputSha256"], sha(self.output))
        self.assertEqual((self.directory / "OpenUSD-LICENSE.txt").read_bytes(), Path(_ARGS.license).read_bytes())
        self.assertEqual((self.directory / "OpenUSD-NOTICE.txt").read_bytes(), Path(_ARGS.notice).read_bytes())
        header = self.output.read_text(encoding="utf-8").split('def Shader', 1)[0]
        self.assertIn("# See OpenUSD-NOTICE.txt for upstream attribution.", header)
        self.assertIn("# Modified to bind the standard USD interface to MDL implementations.", header)
        identifiers = [entry["identifier"] for entry in manifest["shaders"]]
        self.assertEqual(len(identifiers), len(set(identifiers)))
        self.assertIn("UsdPreviewSurface", identifiers)

    def test_regeneration_is_stable_and_follows_changed_canonical_defaults(self) -> None:
        source = self._copy_source()
        source_path = self._save_source(source)
        self._generate(source=source_path)
        before = {path: (path.read_bytes(), path.stat().st_mtime_ns)
                  for path in (self.output, self.provenance, self.directory / "OpenUSD-LICENSE.txt",
                               self.directory / "OpenUSD-NOTICE.txt")}
        self._generate(source=source_path)
        self.assertEqual(before, {path: (path.read_bytes(), path.stat().st_mtime_ns) for path in before})
        source.GetPrimAtPath("/UsdPreviewSurface").GetAttribute("inputs:diffuseColor").Set(Gf.Vec3f(0.1, 0.2, 0.3))
        self._save_source(source)
        regenerated = self._generate(source=source_path)
        self.assertEqual(regenerated.GetPrimAtPath("/UsdPreviewSurface").GetAttribute("inputs:diffuseColor").Get(), Gf.Vec3f(0.1, 0.2, 0.3))
        self.assertNotEqual(before[self.output][0], self.output.read_bytes())
        self.assertNotEqual(before[self.provenance][0], self.provenance.read_bytes())

    def test_source_attribution_comments_survive_export(self) -> None:
        source = self._copy_source()
        # Hash characters inside authored strings are data, not source comments.
        documentation = 'Documentation with caf\u00e9, \u6750\u8cea, "quotes", # hash and\n# a second line.'
        source.GetPrimAtPath("/UsdPreviewSurface").SetDocumentation(documentation)
        path = self._save_source(source)
        comments = ["# Copyright 2099 Test Fixture Authors", "# Additional attribution: synthetic fixture only"]
        text = path.read_text(encoding="utf-8")
        text = text.replace("#usda 1.0\n", "#usda 1.0\n" + comments[0] + "\n", 1)
        path.write_text(text + "\n" + comments[1] + "\n", encoding="utf-8", newline="\n")
        generated = self._generate(source=path)
        self.assertEqual(generated.GetPrimAtPath("/UsdPreviewSurface").GetDocumentation(), documentation)
        output = self.output.read_text(encoding="utf-8")
        for comment in comments:
            self.assertEqual(output.count(comment), 1)
        self.assertEqual(output.count("# a second line."), 1)

    def test_hashes_in_literals_are_not_misidentified_as_source_comments(self) -> None:
        source = self._copy_source()
        prim = source.GetPrimAtPath("/UsdPreviewSurface")
        asset = "mdl://fixture/preview#asset.mdl"
        self.assertTrue(UsdShade.Shader(prim).SetSourceAsset(Sdf.AssetPath(asset), "mdl"))
        text = 'Quoted "text" and # a hash'
        prim.CreateAttribute("fixture:text", Sdf.ValueTypeNames.String).Set(text)
        path = self._save_source(source)
        with path.open("a", encoding="utf-8") as stream:
            stream.write("\n# Attribution after literals: synthetic fixture only\n")
        generated = self._generate(source=path)
        self.assertEqual(generated.GetPrimAtPath("/UsdPreviewSurface").GetAttribute("fixture:text").Get(), text)
        self.assertEqual(UsdShade.Shader(generated.GetPrimAtPath("/UsdPreviewSurface")).GetSourceAsset("mdl").path, asset)
        header = self.output.read_text(encoding="utf-8").split('def Shader', 1)[0]
        self.assertIn("# Attribution after literals: synthetic fixture only", header)
        self.assertNotIn("#asset.mdl", header)
        self.assertNotIn("# a hash", header)

    def test_empty_license_and_notice_are_rejected_before_writing(self) -> None:
        for kind in ("license", "notice"):
            for data in (b"", b" \r\n\t", "\u3000\u00a0\u2003".encode("utf-8"),
                         b"\xef\xbb\xbf", "\ufeff \u3000\n".encode("utf-8")):
                with self.subTest(kind=kind, data=data):
                    path = self.directory / f"empty-{kind}.txt"
                    path.write_bytes(data)
                    result = self._run(**{kind + "_source": path})
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(f"{kind} must not be empty", result.stderr)
                    for output in (self.output, self.provenance,
                                   self.directory / "OpenUSD-LICENSE.txt",
                                   self.directory / "OpenUSD-NOTICE.txt"):
                        self.assertFalse(output.exists())

    def test_invalid_utf8_legal_text_is_rejected_before_writing(self) -> None:
        for kind in ("license", "notice"):
            with self.subTest(kind=kind):
                path = self.directory / f"invalid-{kind}.txt"
                path.write_bytes(b"Invalid UTF-8: \xff")
                result = self._run(**{kind + "_source": path})
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(f"{kind} must be UTF-8 text", result.stderr)
                for output in (self.output, self.provenance,
                               self.directory / "OpenUSD-LICENSE.txt",
                               self.directory / "OpenUSD-NOTICE.txt"):
                    self.assertFalse(output.exists())

    def test_custom_notice_is_copied_unchanged_and_tracks_updates(self) -> None:
        notice = self.directory / "selected-notice.txt"
        for data in (b"Synthetic distribution attribution.\r\n",
                     b"Synthetic distribution attribution.\r\nAdditional author.\r\n",
                     "\ufeffAttribution: caf\u00e9, \u6750\u8cea.\r\n".encode("utf-8")):
            notice.write_bytes(data)
            self._generate(notice_source=notice)
            self.assertEqual((self.directory / "OpenUSD-NOTICE.txt").read_bytes(), data)
            manifest = json.loads(self.provenance.read_text(encoding="utf-8"))
            self.assertEqual(manifest["noticeSha256"], hashlib.sha256(data).hexdigest())

    def test_generation_refuses_to_overwrite_the_notice_input(self) -> None:
        notice = self.directory / "selected-notice.txt"
        notice.write_bytes(Path(_ARGS.notice).read_bytes())
        before = notice.read_bytes()
        result = self._run(output=notice, notice_source=notice)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must not overwrite", result.stderr)
        self.assertEqual(notice.read_bytes(), before)
        self.assertFalse(self.provenance.exists())

    def test_external_composition_dependencies_are_rejected(self) -> None:
        source = self._copy_source()
        dependency = Sdf.Layer.CreateNew(str(self.directory / "dependency.usda"))
        self.assertIsNotNone(dependency)
        self.assertTrue(dependency.Save())
        source.GetRootLayer().subLayerPaths.append(dependency.identifier)
        self._assert_rejected(self._save_source(source), "external composition dependencies are not supported")
    def test_relative_asset_defaults_require_a_resource_packaging_policy(self) -> None:
        source = self._copy_source()
        self.assertTrue(source.GetPrimAtPath("/UsdUVTexture").GetAttribute("inputs:file").Set(Sdf.AssetPath("fallback.png")))
        self._assert_rejected(self._save_source(source), "relative asset default requires an explicit resource packaging policy")

    def test_existing_mdl_asset_and_exact_signature_are_preserved(self) -> None:
        source = self._copy_source()
        shader = UsdShade.Shader(source.GetPrimAtPath("/UsdPrimvarReader_float"))
        self.assertTrue(shader.SetSourceAsset(Sdf.AssetPath("preview.mdl"), "mdl"))
        self.assertTrue(shader.SetSourceAssetSubIdentifier("UsdPrimvarReader_float(string,float)", "mdl"))
        generated = self._generate(source=self._save_source(source))
        actual = UsdShade.Shader(generated.GetPrimAtPath("/UsdPrimvarReader_float"))
        self.assertEqual(actual.GetSourceAsset("mdl").path, "preview.mdl")
        self.assertEqual(actual.GetSourceAssetSubIdentifier("mdl"), "UsdPrimvarReader_float(string,float)")
        manifest = json.loads(self.provenance.read_text(encoding="utf-8"))
        entry = next(item for item in manifest["shaders"] if item["identifier"] == "UsdPrimvarReader_float")
        self.assertEqual(entry["mdlDefinition"], "UsdPrimvarReader_float(string,float)")

    def test_existing_relative_mdl_asset_requires_a_packaging_policy(self) -> None:
        for asset in ("application/preview.mdl", r"application\preview.mdl", "C:preview.mdl"):
            with self.subTest(asset=asset):
                source = self._copy_source()
                shader = UsdShade.Shader(source.GetPrimAtPath("/UsdPrimvarReader_float"))
                self.assertTrue(shader.SetSourceAsset(Sdf.AssetPath(asset), "mdl"))
                self._assert_rejected(self._save_source(source), "relative MDL source asset requires an explicit resource packaging policy")

    def _assert_existing_mdl_asset_preserved(self, asset: str) -> None:
        source = self._copy_source()
        shader = UsdShade.Shader(source.GetPrimAtPath("/UsdPrimvarReader_float"))
        self.assertTrue(shader.SetSourceAsset(Sdf.AssetPath(asset), "mdl"))
        generated = self._generate(source=self._save_source(source))
        actual = UsdShade.Shader(generated.GetPrimAtPath("/UsdPrimvarReader_float"))
        self.assertEqual(actual.GetSourceAsset("mdl").path, asset)

    def test_existing_absolute_mdl_asset_is_preserved(self) -> None:
        self._assert_existing_mdl_asset_preserved((self.directory / "preview.mdl").as_posix())

    def test_existing_mdl_resolver_uri_is_preserved(self) -> None:
        self._assert_existing_mdl_asset_preserved("mdl://application/preview.mdl")
    def test_conflicting_existing_mdl_definition_is_rejected(self) -> None:
        source = self._copy_source()
        shader = UsdShade.Shader(source.GetPrimAtPath("/UsdPrimvarReader_float"))
        self.assertTrue(shader.SetSourceAsset(Sdf.AssetPath("preview.mdl"), "mdl"))
        self.assertTrue(shader.SetSourceAssetSubIdentifier("DifferentFunction(string,float)", "mdl"))
        self._assert_rejected(self._save_source(source), "conflicting existing MDL sub-identifier")

    def test_wrong_sdk_version_fails_before_writing_outputs(self) -> None:
        result = self._run(version="0.99.0")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("selected USD version mismatch", result.stderr)
        self.assertFalse(self.output.exists())
        self.assertFalse(self.provenance.exists())

    def test_unknown_concrete_shader_requires_an_explicit_backend_binding(self) -> None:
        source = self._copy_source()
        shader = UsdShade.Shader.Define(source, "/FutureShader")
        shader.CreateIdAttr("UsdFutureShader")
        self._assert_rejected(self._save_source(source), "no MDL binding is defined for shader 'UsdFutureShader'")

    def test_missing_mapped_property_is_rejected(self) -> None:
        source = self._copy_source()
        self.assertTrue(source.GetPrimAtPath("/UsdTransform2d").RemoveProperty("inputs:in"))
        self._assert_rejected(self._save_source(source), "required property 'inputs:in' is missing")

    def test_changed_mapped_property_type_is_rejected(self) -> None:
        source = self._copy_source()
        prim = source.GetPrimAtPath("/UsdTransform2d")
        self.assertTrue(prim.RemoveProperty("inputs:in"))
        prim.CreateAttribute("inputs:in", Sdf.ValueTypeNames.Float3)
        self._assert_rejected(self._save_source(source), "unsupported mapped type 'float3'")

    def test_versioned_identifiers_retain_identity_and_use_family_binding(self) -> None:
        source = self._copy_source()
        UsdShade.Shader(source.GetPrimAtPath("/UsdPreviewSurface")).CreateIdAttr("UsdPreviewSurface_2_3")
        generated = self._generate(source=self._save_source(source))
        shader = UsdShade.Shader(generated.GetPrimAtPath("/UsdPreviewSurface"))
        self.assertEqual(shader.GetIdAttr().Get(), "UsdPreviewSurface_2_3")
        self.assertEqual(shader.GetSourceAssetSubIdentifier("mdl"), "UsdPreviewSurface")
        manifest = json.loads(self.provenance.read_text(encoding="utf-8"))
        entry = next(item for item in manifest["shaders"] if item["identifier"] == "UsdPreviewSurface_2_3")
        self.assertEqual(entry["mdlDefinition"], "UsdPreviewSurface")

    def test_optional_shader_absence_is_reported_without_fabricated_definition(self) -> None:
        source = self._copy_source()
        self.assertTrue(source.RemovePrim("/UsdPrimvarReader_matrix"))
        generated = self._generate(source=self._save_source(source))
        self.assertFalse(generated.GetPrimAtPath("/UsdPrimvarReader_matrix"))
        manifest = json.loads(self.provenance.read_text(encoding="utf-8"))
        self.assertIn("UsdPrimvarReader_matrix", manifest["missingOptionalShaders"])

    def test_missing_required_preview_surface_is_rejected(self) -> None:
        source = self._copy_source()
        self.assertTrue(source.RemovePrim("/UsdPreviewSurface"))
        self._assert_rejected(self._save_source(source), "no concrete UsdPreviewSurface definition")

    def test_generation_refuses_to_overwrite_the_license_input(self) -> None:
        license_source = self.directory / "selected-license.txt"
        license_source.write_bytes(Path(_ARGS.license).read_bytes())
        before = license_source.read_bytes()
        result = self._run(output=license_source, license_source=license_source)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must not overwrite", result.stderr)
        self.assertEqual(license_source.read_bytes(), before)
        self.assertFalse(self.provenance.exists())
    def test_generation_refuses_to_overwrite_the_canonical_input(self) -> None:
        source = self._save_source(self._copy_source())
        before = source.read_bytes()
        result = self._run(source=source, output=source)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must not overwrite the canonical input", result.stderr)
        self.assertEqual(source.read_bytes(), before)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    for argument in ("generator", "source", "usd-version", "usd-python", "usd-prefix", "license", "notice"):
        parser.add_argument("--" + argument, required=True)
    _ARGS = parser.parse_args()
    unittest.main(argv=[sys.argv[0]], verbosity=2)
