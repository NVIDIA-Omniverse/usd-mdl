#
# SPDX-License-Identifier: Apache-2.0
#
# Standalone unittest + pxr. Tokens come from the C++ Python binding.
from __future__ import annotations

import ast
import os
import pprint
import re
import sys
import unittest
from pathlib import Path
from typing import Any

# UsdMdl is a top-level Python package (NOT nested under pxr/), so the
# plugin can be installed into any prefix without overlay-install.  The
# CTest wrapper puts the freshly built UsdMdl package on PYTHONPATH;
# installation is needed only for the separate installed-package smoke test.
from pxr import Ar, Plug, Sdf, Sdr, Usd, UsdShade
import UsdMdl


_plugins_registered = False
_UNCHECKED = object()


_PRIM_PATH_INVALID = re.compile(r"[^A-Za-z0-9_]")


def _sanitize_prim_name(s: str) -> str:
    """Coerce arbitrary text into a valid SdfPath name (alnum + underscore, no leading digit)."""
    out = _PRIM_PATH_INVALID.sub("_", s)
    if out and out[0].isdigit():
        out = "_" + out
    return out or "_"


def run_unittest_main_exit_clean(verbosity: int = 1) -> None:
    """Run tests then hard-exit via TerminateProcess (Windows) / os._exit.

    All usdMdl C++ code releases MDL SDK handles correctly and
    INeuray::shutdown() succeeds when called explicitly (verified by
    testMdlRuntimeShutdown phases 1-5).  However, when running under Python
    with pxr loaded, USD's SdrRegistry (TfSingleton) and boost-python
    prevent a clean exit: the interpreter/DLL teardown order during
    ExitProcess triggers re-entry into MDL SDK code after shutdown, or
    kills MDL SDK threads before our atexit handler runs.
    TerminateProcess bypasses all DLL detach handlers entirely.

    ctypes argtypes must be set explicitly so the HANDLE pseudo-handle
    (-1) is not truncated on 64-bit Python.
    """
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules["__main__"])
    runner = unittest.TextTestRunner(verbosity=verbosity, stream=sys.stdout)
    result = runner.run(suite)
    code = 0 if result.testsRun > 0 and result.wasSuccessful() else 1
    sys.stdout.flush(); sys.stderr.flush()

    if sys.platform == "win32":
        import ctypes
        import ctypes.wintypes

        kernel32 = ctypes.windll.kernel32
        kernel32.GetCurrentProcess.argtypes = []
        kernel32.GetCurrentProcess.restype = ctypes.wintypes.HANDLE
        kernel32.TerminateProcess.argtypes = [ctypes.wintypes.HANDLE, ctypes.wintypes.UINT]
        kernel32.TerminateProcess.restype = ctypes.wintypes.BOOL
        kernel32.TerminateProcess(kernel32.GetCurrentProcess(), code)
    else:
        os._exit(code)


def ensure_mdl_plugins_loaded() -> None:
    global _plugins_registered
    if _plugins_registered:
        return
    path = os.environ.get("PXR_PLUGINPATH_NAME", "")
    if path:
        Plug.Registry().RegisterPlugins(path)
    _plugins_registered = True


class MdlTestBase(unittest.TestCase):
    """Base class for the Python MDL SDR plugin tests.

    Sets up an in-memory `Usd.Stage`, exposes a `Sdr.Registry`, and provides
    helpers that author `UsdShade.Shader` prims pointing at fixture .mdl
    files and resolve them back through `SdrRegistry`.
    """

    debug = False
    debug_value = False

    @classmethod
    def setUpClass(cls) -> None:
        ensure_mdl_plugins_loaded()

    def setUp(self) -> None:
        pxr_test = os.environ.get("PXR_TEST_DIR")
        if not pxr_test:
            self.fail("PXR_TEST_DIR must be set by mdlTestWrapper.py")
        self.mdl_path = Path(pxr_test) / "mdl"
        self.stage = Usd.Stage.CreateInMemory()
        self.sdr_registry = Sdr.Registry()

    def get_module_path(self, mdl_module: str) -> Sdf.AssetPath:
        module_path = str(self.mdl_path / mdl_module)
        self.assertTrue(os.path.exists(module_path), msg=f"missing {module_path}")
        return Sdf.AssetPath(module_path, Ar.GetResolver().Resolve(module_path))

    def create_prim(self, prim_path: str, prim_type: str):
        prim = self.stage.DefinePrim(prim_path, prim_type)
        self.assertIsNotNone(prim)
        return prim

    def assert_resolved_asset_bytes(self, resolved_path: str, expected_file: Path):
        """Consume the identifier directly through Ar, without conversion or Resolve."""
        asset = Ar.GetResolver().OpenAsset(Ar.ResolvedPath(resolved_path))
        self.assertIsNotNone(asset, msg=f"Ar could not open {resolved_path!r}")
        self.assertEqual(asset.GetBuffer(), expected_file.read_bytes())

    def create_usdshade_shader(self, prim_path="/World/Shader", mdl_module="", subidentifier=""):
        prim = self.create_prim(prim_path, "Shader")
        usdshade_shader = UsdShade.Shader(prim)
        self.assertIsNotNone(usdshade_shader)
        if mdl_module and subidentifier:
            source_asset = self.get_module_path(mdl_module)
            res = usdshade_shader.SetSourceAsset(source_asset, UsdMdl.Tokens.Mdl)
            self.assertTrue(res)
            res = usdshade_shader.SetSourceAssetSubIdentifier(subidentifier, UsdMdl.Tokens.Mdl)
            self.assertTrue(res)
        return usdshade_shader

    def _resolve_sdr_through_usdshade(self, usdshade_shader, *, source_label: str):
        """Resolve a UsdShade.Shader prim to its SdrShaderNode via the canonical USD path.

        Authors `info:mdl:sourceAsset` + sub-identifier on the prim, then asks USD to
        resolve it: UsdShadeShader::GetShaderNodeForSourceType -> SdrRegistry.
        """
        sdr_node = usdshade_shader.GetShaderNodeForSourceType(UsdMdl.Tokens.Mdl)
        if sdr_node is None:
            prim = usdshade_shader.GetPrim()
            self.fail(
                f"No Sdr node resolved through UsdShade for {source_label} (prim {prim.GetPath()})"
            )
        return sdr_node

    def validate_node(self, mdl_module: str, subidentifier: str):
        prim_name = _sanitize_prim_name(Path(mdl_module).stem + "_" + subidentifier)
        prim_path = "/World/" + prim_name
        usdshade_shader = self.create_usdshade_shader(
            prim_path=prim_path, mdl_module=mdl_module, subidentifier=subidentifier)
        return self._resolve_sdr_through_usdshade(
            usdshade_shader, source_label=f"{mdl_module!r}/{subidentifier!r}")

    def debug_print(self, sdr_node):
        def print_metadata(metadata):
            to_print = {}
            for k, v in metadata.items():
                to_print[k] = self.evalStr(v)
            pprint.pprint(to_print, indent=2)

        print(f"\n\ndebug_print()\tsdr node name: {sdr_node.GetName()}")
        print("\nsdr_node_metadata:")
        print_metadata(self.evalStr(sdr_node.GetMetadata()))
        print("\ninputs:")
        for name in sdr_node.GetShaderInputNames():
            print(f"sdr property name: {name}")
            sdr_property = sdr_node.GetShaderInput(name)
            print(f"\tdefault value: {sdr_property.GetDefaultValue()}")
            print(f"\thints:")
            print_metadata(self.evalStr(sdr_property.GetHints()))
            print(f"\tmetadata:")
            print_metadata(self.evalStr(sdr_property.GetMetadata()))
        print("\noutputs:")
        for name in sdr_node.GetShaderOutputNames():
            print(f"sdr property name: {name}")
            sdr_property = sdr_node.GetShaderOutput(name)
            print(f"\thints:")
            print_metadata(self.evalStr(sdr_property.GetHints()))
            print(f"\tmetadata:")
            print_metadata(self.evalStr(sdr_property.GetMetadata()))

    def evalStr(self, s: Any) -> Any:
        """Convert Sdr metadata strings holding python-literal tuples/lists
        (e.g. \"(1.0, 0.0, 0.0)\") back into their structured form.  Uses
        `ast.literal_eval` (not `eval`) so the harness can't be tricked into
        running arbitrary code via a malformed fixture."""
        if isinstance(s, str):
            try:
                s = ast.literal_eval(s)
            except (ValueError, SyntaxError):
                pass
        return s

    def isClose(self, a, b) -> None:
        if a is None or b is None:
            self.assertIs(a, b, "missing value does not match a numeric default")
        else:
            self.assertLess(abs(a - b), 1e-7)

    def _metadata_get(self, a, k):
        """Sdr metadata maps may use TfToken keys; expected dicts use str keys."""
        if a is None:
            return None
        if k in a:
            return a[k]
        ks = str(k)
        for ak in a.keys():
            if str(ak) == ks:
                return a[ak]
        return None

    def validateValue(self, sdr, to_check, key=None, prefix="", *, is_default=False):
        if self.debug_value:
            print(f"{prefix}validateValue()\tkey: {key}\tsdr: {sdr}\tto_check: {to_check}")
        a = sdr if is_default else self.evalStr(sdr)
        b = to_check if is_default else self.evalStr(to_check)
        if isinstance(b, dict):
            if not isinstance(a, dict):
                self.fail(f"expected dict metadata, got {type(a)!r} for key {key!r}")
            for k, v in b.items():
                self.validateValue(self._metadata_get(a, k), v, key=k, prefix=prefix + "\t", is_default=is_default)
        elif isinstance(b, tuple) or isinstance(b, list):
            if a is None:
                self.fail(f"expected sequence for key {key!r}, got None")
            self.assertEqual(len(a), len(b), f"sequence length differs for key {key!r}")
            for av, bv in zip(a, b):
                self.validateValue(av, bv, key=key, prefix=prefix + "\t", is_default=is_default)
        elif (key == UsdMdl.Metadata.Symbol) or (key == UsdMdl.Metadata.ArrayDeferredSizeSymbol):
            self.assertEqual(a, b)
        elif (key == UsdMdl.Metadata.ExpressionValue) and isinstance(a, str):
            self.assertEqual(a, b)
        elif str(key) in ("isDynamicArray", str(Sdr.PropertyMetadata.IsDynamicArray)):
            # Presence-flag: OpenUSD's Sdr layer canonicalizes the value
            # differently across versions ('' on some, '1' on others). The
            # test only cares that the metadata key is present.
            self.assertIsNotNone(a, f"missing presence-flag metadata key {key!r}")
        elif isinstance(b, Sdf.AssetPath):
            aAssetPath = a
            if isinstance(a, str):
                aAssetPath = Sdf.AssetPath(a.replace("@", ""))
            self.assertEqual(aAssetPath.path, b.path)
            if not b.path or b.resolvedPath:
                self.assertEqual(aAssetPath.resolvedPath, b.resolvedPath)
        elif is_default and type(b) in (bool, int, str):
            self.assertIs(type(a), type(b))
            self.assertEqual(a, b)
        elif isinstance(b, (bool, int, str, type, Sdf.AssetPath)):
            # `type` covers test metadata values that pass a Python class
            # object (e.g. Sdf.AssetPath) for structural comparison via str().
            if a != b and str(a) != str(b):
                self.assertEqual(a, b)
        else:
            self.isClose(a, b)

    def validate_property(
        self, name, sdr_node, value_type_name, is_input, metadata, default_value=_UNCHECKED, options=None
    ):
        if self.debug_value:
            print(f"\n\nvalidate_property()\t{name}")
        if is_input:
            sdr_property = sdr_node.GetShaderInput(name)
        else:
            sdr_property = sdr_node.GetShaderOutput(name)
        self.assertIsNotNone(sdr_property)
        type_indicator = sdr_property.GetTypeAsSdfType()
        if isinstance(value_type_name, Sdf.ValueTypeName):
            # Matching an Sdr token's spelling is not a successful USD type lookup.
            self.assertTrue(type_indicator.HasSdfType(), name)
            self.assertEqual(type_indicator.GetSdfType(), value_type_name)
            self.assertEqual(sdr_property.GetType(), str(value_type_name))
        else:
            # Opaque struct/terminal/unknown ports describe connections, not values.
            self.assertEqual(sdr_property.GetType(), value_type_name)
        if isinstance(default_value, list):
            self.assertTrue(sdr_property.IsArray())
            if not sdr_property.IsDynamicArray():
                self.assertEqual(sdr_property.GetArraySize(), len(default_value))
        if default_value is None:
            self.assertIsNone(sdr_property.GetDefaultValue())
            self.assertIsNone(sdr_property.GetDefaultValueAsSdfType())
        elif default_value is not _UNCHECKED:
            self.validateValue(sdr_property.GetDefaultValue(), default_value, is_default=True)
            if isinstance(value_type_name, Sdf.ValueTypeName):
                self.validateValue(sdr_property.GetDefaultValueAsSdfType(), default_value, is_default=True)
        if options:
            self.validate_property_options(sdr_property, options)
        sdr_metadata = sdr_property.GetMetadata()
        self.validateValue(sdr_metadata, metadata)

    def validate_property_options(self, sdr_property, options_to_check):
        options = sdr_property.GetOptions()
        self.assertIsNotNone(options)
        self.assertEqual(len(options), len(options_to_check))
        for option in options_to_check:
            self.assertTrue(option in options)

    def validate_mdl(
        self,
        mdl_module,
        subidentifier,
        value_type_name,
        default_value,
        metadata,
        input_metadata=None,
        options=None,
        checkOutputs=True,
    ):
        sdr_node = self.validate_node(mdl_module, subidentifier)
        if self.debug:
            self.debug_print(sdr_node)
        if checkOutputs:
            output_names = sdr_node.GetShaderOutputNames()
            self.assertTrue(output_names, "expected at least one shader output")
            for outputName in output_names:
                self.validate_property(outputName, sdr_node, value_type_name, False, metadata)
        if input_metadata:
            metadata = dict(metadata)
            metadata.update(input_metadata)
        self.validate_property("param", sdr_node, value_type_name, True, metadata, default_value, options=options)
        return sdr_node
