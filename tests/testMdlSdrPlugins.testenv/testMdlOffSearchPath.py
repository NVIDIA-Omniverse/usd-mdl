# SPDX-License-Identifier: Apache-2.0

import os
import shutil
import sys
import tempfile
from pathlib import Path

_ROOT = os.path.dirname(os.path.abspath(__file__))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from lib.mdl_test_base import MdlTestBase, UsdMdl, run_unittest_main_exit_clean
from pxr import Sdf, UsdShade


_BOOL_MDL_SRC = """\
mdl 1.8;

export bool test_off(bool param = true)
{
    return param;
}
"""

_NEIGHBOR_MDL_SRC = """\
mdl 1.8;

export bool inner_func(bool flag = true)
{
    return flag;
}
"""

_PRIMARY_WITH_REL_IMPORT_SRC = """\
mdl 1.8;

import .::off_neighbor::*;

export bool test_rel(bool param = false)
{
    return off_neighbor::inner_func(param);
}
"""

# Sibling module that only defines a struct type, imported by
# _OVERLOAD_PRIMARY_SRC below.  Used to exercise the off-search-path
# cross-module parameter-type case in ComputeMdlSubIdentifier.
_THING_MDL_SRC = """\
mdl 1.8;

export struct Thing
{
    int v = 0;
};
"""

# Off-path primary with an *overloaded* function, one overload of which
# takes a struct from an off-path sibling module.  Because the function
# is overloaded the sub-identifier carries a parameter signature, and
# the sibling struct type must render relative (`.::off_thing::Thing`)
# rather than leaking the absolute temp-dir path.
_OVERLOAD_PRIMARY_SRC = """\
mdl 1.8;

import .::off_thing::*;

export int over_use(int x = 0)
{
    return x;
}

export int over_use(off_thing::Thing t = off_thing::Thing())
{
    return t.v;
}
"""


# Keep implementation-specific encoded module identities out of authored
# MDL fixtures; deriving them in Python would duplicate the runtime encoder.
# The off-path tests below exercise:
#   - top-level off-path modules referenced via UsdShade.Shader
#     (the resolver decodes the encoded name and fetches via Ar);
#   - relative imports between two off-path siblings (the SDK
#     internally rewrites these to absolute encoded names and round-
#     trips them through our resolver).


class OffSearchPathTests(MdlTestBase):
    """Verify that .mdl files outside any MDL search path can still be loaded
    when referenced from a UsdShade.Shader via absolute path."""

    def _copy_to_tempdir(self, files):
        """files: dict[name -> content]. Returns Path to temp dir (kept alive
        via self for the lifetime of the test method)."""
        if not hasattr(self, "_tempdirs"):
            self._tempdirs = []
        tmp = tempfile.mkdtemp(prefix="mdl_offpath_")
        self._tempdirs.append(tmp)
        for name, content in files.items():
            (Path(tmp) / name).write_text(content)
        return Path(tmp)

    def tearDown(self):
        for d in getattr(self, "_tempdirs", []):
            shutil.rmtree(d, ignore_errors=True)

    def _author_shader(self, prim_path, absolute_mdl_path, subidentifier):
        prim = self.create_prim(prim_path, "Shader")
        shader = UsdShade.Shader(prim)
        self.assertIsNotNone(shader)
        asset = Sdf.AssetPath(absolute_mdl_path)
        self.assertTrue(shader.SetSourceAsset(asset, UsdMdl.Tokens.Mdl))
        self.assertTrue(shader.SetSourceAssetSubIdentifier(subidentifier, UsdMdl.Tokens.Mdl))
        return shader

    def test_off_search_path_module_resolves(self):
        """A standalone .mdl outside MDL_USER_PATH resolves via URI-encoded name + Ar."""
        tmp = self._copy_to_tempdir({"off_solo.mdl": _BOOL_MDL_SRC})
        mdl_path = str(tmp / "off_solo.mdl")

        # Sanity: this path is NOT under MDL_USER_PATH.
        self.assertFalse(
            mdl_path.lower().startswith(str(self.mdl_path).lower()),
            msg=f"test fixture sanity: {mdl_path} should not live under {self.mdl_path}")

        shader = self._author_shader("/World/OffSolo", mdl_path, "test_off")
        sdr_node = shader.GetShaderNodeForSourceType(UsdMdl.Tokens.Mdl)
        self.assertIsNotNone(sdr_node,
                             msg=f"off-search-path .mdl failed to resolve: {mdl_path}")
        self.assertTrue(sdr_node.IsValid())

        # Compatibility loading outside roots must not invent a search-path identifier.
        self.assertEqual(sdr_node.GetMetadata()[UsdMdl.NodeMetadata.ModuleUsdIdentifier], "")

        # Sanity-check the parsed node has the expected 'param' input.
        param = sdr_node.GetShaderInput("param")
        self.assertIsNotNone(param)

    def test_off_search_path_texture_defaults(self):
        """Real SDK loading preserves ordinary and tiled resources through Sdr."""
        tmp = self._copy_to_tempdir({"off_textures.mdl": '''mdl 1.8;
            export texture_2d single(uniform texture_2d image = texture_2d("./space # %20.png")) = image;
            export texture_2d tiled(uniform texture_2d image = texture_2d("./tile <UDIM>.png")) = image;
        '''})
        for name in ("space # %20.png", "tile 1001.png", "tile 1002.png"):
            shutil.copyfile(self.mdl_path / "resources/one.png", tmp / name)
        for definition, filename in (("single", "space # %20.png"), ("tiled", "tile <UDIM>.png")):
            shader = self._author_shader(
                "/World/OffTexture_" + definition, str(tmp / "off_textures.mdl"), definition)
            node = shader.GetShaderNodeForSourceType(UsdMdl.Tokens.Mdl)
            self.assertIsNotNone(node)
            self.assertTrue(node.IsValid())
            value = node.GetShaderInput("image").GetDefaultValue()
            self.assertIsInstance(value, Sdf.AssetPath)
            self.assertTrue(value.path.startswith("/"))
            self.assertEqual(Path(value.resolvedPath), tmp / filename)
            if definition == "single":
                self.assert_resolved_asset_bytes(value.resolvedPath, tmp / filename)
            else:
                for tile in ("1001", "1002"):
                    self.assert_resolved_asset_bytes(
                        value.resolvedPath.replace("<UDIM>", tile), tmp / f"tile {tile}.png")

    def test_same_off_path_module_dedups(self):
        """Two USD shaders referencing the same off-path .mdl share one Sdr id."""
        tmp = self._copy_to_tempdir({"off_dedup.mdl": _BOOL_MDL_SRC})
        mdl_path = str(tmp / "off_dedup.mdl")

        s1 = self._author_shader("/World/OffDedupA", mdl_path, "test_off")
        s2 = self._author_shader("/World/OffDedupB", mdl_path, "test_off")
        n1 = s1.GetShaderNodeForSourceType(UsdMdl.Tokens.Mdl)
        n2 = s2.GetShaderNodeForSourceType(UsdMdl.Tokens.Mdl)
        self.assertIsNotNone(n1)
        self.assertIsNotNone(n2)
        self.assertEqual(n1.GetIdentifier(), n2.GetIdentifier())

    def test_off_search_path_with_relative_import(self):
        """Off-path .mdl that imports a sibling .mdl (also off-path).

        Exercises the encoded-owner -> relative-import code path in
        ArEntityResolver::resolve_module: the import statement
        '.::off_neighbor::*' in the primary module anchors on the
        primary's URL via Ar, encodes the resolved sibling URL, and
        the SDK accepts the resolution.

        The primary references inner_func via the qualified form
        `off_neighbor::inner_func` because that is the MDL semantics of the
        plain `import qual::*;` form (imports into a named scope, not the
        global scope - see existing fixtures like bsdfMeasurement.mdl which
        uses `df::measured_bsdf` after `import ::df::*;`).
        """
        tmp = self._copy_to_tempdir({
            "off_neighbor.mdl": _NEIGHBOR_MDL_SRC,
            "off_primary.mdl": _PRIMARY_WITH_REL_IMPORT_SRC,
        })
        mdl_path = str(tmp / "off_primary.mdl")

        shader = self._author_shader("/World/OffRel", mdl_path, "test_rel")
        sdr_node = shader.GetShaderNodeForSourceType(UsdMdl.Tokens.Mdl)
        self.assertIsNotNone(sdr_node,
                             msg=f"off-path .mdl with relative import failed: {mdl_path}")
        self.assertTrue(sdr_node.IsValid())

    def test_off_path_cross_module_param_is_relative(self):
        """Overloaded off-path function with an off-path sibling struct
        parameter renders the type relative (`.::off_thing::Thing`),
        independent of the absolute temp-dir location.

        This locks the determinism property the reverse (deserialization)
        direction depends on: the sub-identifier the formatter emits at
        module-load time is the same relocatable string regardless of
        where the asset lives on disk, so the cached `Function` map keys
        match what USD content authors.
        """
        tmp = self._copy_to_tempdir({
            "off_thing.mdl": _THING_MDL_SRC,
            "off_overload.mdl": _OVERLOAD_PRIMARY_SRC,
        })
        mdl_path = str(tmp / "off_overload.mdl")

        expected_subid = "over_use(.::off_thing::Thing)"
        shader = self._author_shader("/World/OffOverload", mdl_path, expected_subid)
        sdr_node = shader.GetShaderNodeForSourceType(UsdMdl.Tokens.Mdl)
        self.assertIsNotNone(
            sdr_node,
            msg=f"off-path overloaded .mdl failed to resolve relative sub-id: {mdl_path}")
        self.assertTrue(sdr_node.IsValid())

        # Resolved to the struct-taking overload (param `t`), not the
        # int overload (param `x`).
        self.assertIsNotNone(
            sdr_node.GetShaderInput("t"),
            msg="expected the struct overload (input 't'), got a different overload")

        # The node's own sub-identifier round-trips to exactly what we
        # authored (a fixed relocatable string), proving no absolute
        # temp-dir path leaked into it.
        actual_subid = sdr_node.GetMetadata().get("mdl:subIdentifier", "")
        self.assertEqual(actual_subid, expected_subid)


if __name__ == "__main__":
    run_unittest_main_exit_clean()
