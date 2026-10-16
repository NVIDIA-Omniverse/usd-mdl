#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Check the examples against authored content, without querying Sdr in the test."""

import ast
from pathlib import Path
import re
import subprocess
import sys
import unittest


EXAMPLE = Path(sys.argv.pop(1)).resolve()
IS_CPP = EXAMPLE.suffix != ".py"
COMMAND = [str(EXAMPLE)] if IS_CPP else [sys.executable, str(EXAMPLE)]


class TestInspectMdlMaterial(unittest.TestCase):
    def run_example(self, stage="mdl_bound_material.usda", *, succeeds=True):
        result = subprocess.run(
            COMMAND + [stage, "/World/Sphere"],
            capture_output=True, text=True, timeout=60,
        )
        self.assertEqual(result.returncode == 0, succeeds, result.stdout + result.stderr)
        return result

    def inputs(self, output):
        rows = re.findall(r"^  (\w+): ([^\n]+)\n((?:    .*\n)*)", output, re.MULTILINE)
        self.assertEqual(len(rows), len({name for name, _, _ in rows}), output)
        return {
            name: (dict(field.split("=", 1) for field in re.split(r", (?=\w+=)", fields)), files)
            for name, fields, files in rows
        }

    def test_sample_parameters(self):
        output = self.run_example().stdout
        self.assertIn(
            "Prim /World/Sphere uses material /World/Looks/Red "
            "from mdl/example_package/example_module.mdl (MDL definition red)\n", output,
        )
        inputs = self.inputs(output)
        # Expectations come from examples/mdl/example_package/example_module.mdl, including
        # its three resource path spellings and independently authored defaults.
        expected = {
            "enabled": dict(type="bool", default=True, label="Enabled", help="Enable the red surface"),
            "roughness": dict(type="float", default=0.2, label="Roughness", page="Surface",
                              soft_range={"min": 0, "max": 1}),
            "intensity_percent": dict(type="int", default=100, label="Intensity", page="Surface",
                                      hard_range={"min": 0, "max": 100}),
            "tint": dict(type="float3", default=(1, 0, 0), label="Tint", help="Surface color", page="Surface"),
        }
        for name, color, label, path_kind in (
            ("color_texture", "red", "Color", "Strict-relative"),
            ("green_texture", "green", "Green", "Absolute"),
            ("blue_texture", "blue", "Blue", "Weak-relative"),
        ):
            texture = Path(f"mdl/example_package/textures/{color}_checker.png").resolve()
            expected[name] = dict(
                type="asset", authored=f"/example_package/textures/{color}_checker.png",
                resolved=str(texture),
                label=f"{label} Texture", help=f"{path_kind} MDL resource path", page="Surface",
            )
            if IS_CPP:
                self.assertIn(name, inputs, output)
                files = re.findall(r"^      (.+)$", inputs[name][1], re.MULTILINE)
                self.assertEqual([Path(filename).resolve(strict=True) for filename in files], [texture], output)
        self.assertEqual(set(inputs), set(expected), output)
        for name, fields in expected.items():
            for key, value in fields.items():
                with self.subTest(input=name, field=key):
                    self.assertIn(key, inputs[name][0], output)
                    actual = inputs[name][0][key]
                    if key == "resolved":
                        # Ar may preserve a Windows short path (e.g. RUNNER~1).
                        # Check the selected file, not its native path spelling.
                        self.assertTrue(Path(actual).samefile(value), actual)
                        continue
                    if key in ("default", "soft_range", "hard_range"):
                        actual = ast.literal_eval(actual)
                    if isinstance(value, float):
                        self.assertAlmostEqual(actual, value, places=6)
                    else:
                        self.assertEqual(actual, value)
        if IS_CPP:
            self.assertIn("MDL database module: mdl::example_package::example_module\n", output)
            self.assertIn("MDL qualified module name: ::example_package::example_module\n", output)
            self.assertIn(
                "MDL database definition: mdl::example_package::example_module::red"
                "(bool,float,int,float3,texture_2d,texture_2d,texture_2d)\n", output,
            )
            self.assertIn("MDL simple name: red\n", output)

    def test_nested_module_definition(self):
        module = Path("mdl/nested/alternate.mdl")
        module.parent.mkdir(parents=True, exist_ok=True)
        module.write_text(
            "mdl 1.8;\nexport material bronze() = material();\n"
            "export material gold(float weight = 0.5) = material(ior: color(weight));\n",
            encoding="utf-8",
        )
        scene = Path("mdl_bound_material.usda").read_text(encoding="utf-8")
        Path("nested.usda").write_text(
            scene.replace("mdl/example_package/example_module.mdl", "mdl/nested/alternate.mdl")
                 .replace('= "red"', '= "gold"'), encoding="utf-8",
        )
        output = self.run_example("nested.usda").stdout
        inputs = self.inputs(output)
        self.assertEqual(set(inputs), {"weight"}, output)
        self.assertEqual(inputs["weight"][0]["type"], "float")
        self.assertEqual(float(inputs["weight"][0]["default"]), 0.5)
        if IS_CPP:
            self.assertIn("MDL database module: mdl::nested::alternate\n", output)
            self.assertIn("MDL qualified module name: ::nested::alternate\n", output)
            self.assertIn("MDL database definition: mdl::nested::alternate::gold(float)\n", output)
            self.assertIn("MDL simple name: gold\n", output)

    def test_tiled_resource_files(self):
        package = Path("mdl/tiled")
        package.mkdir(parents=True, exist_ok=True)
        files = []
        for tile, color in ((1001, "red"), (1012, "blue")):
            filename = package / f"tile.{tile}.png"
            filename.write_bytes(
                Path(f"mdl/example_package/textures/{color}_checker.png").read_bytes()
            )
            files.append(filename.resolve())
        (package / "sample.mdl").write_text(
            'mdl 1.8;\nexport material red(uniform texture_2d tiles = '
            'texture_2d("./tile.<UDIM>.png")) = material();\n', encoding="utf-8",
        )
        scene = Path("mdl_bound_material.usda").read_text(encoding="utf-8")
        Path("tiled.usda").write_text(
            scene.replace("mdl/example_package/example_module.mdl", "mdl/tiled/sample.mdl"),
            encoding="utf-8",
        )
        output = self.run_example("tiled.usda").stdout
        inputs = self.inputs(output)
        self.assertEqual(set(inputs), {"tiles"}, output)
        self.assertEqual(inputs["tiles"][0]["authored"], "/tiled/tile.<UDIM>.png")
        mask = Path(inputs["tiles"][0]["resolved"])
        self.assertTrue(mask.parent.samefile(package), mask)
        self.assertEqual(mask.name, "tile.<UDIM>.png")
        if IS_CPP:
            actual = re.findall(r"^      (.+)$", inputs["tiles"][1], re.MULTILINE)
            self.assertEqual([Path(filename).resolve(strict=True) for filename in actual], files, output)

    def test_missing_definition_fails(self):
        scene = Path("mdl_bound_material.usda").read_text(encoding="utf-8")
        Path("missing.usda").write_text(
            scene.replace('= "red"', '= "missing"'), encoding="utf-8",
        )
        result = self.run_example("missing.usda", succeeds=False)
        # Python can raise OpenUSD's parser error before returning a null node.
        self.assertRegex(
            result.stderr, r"could not find MDL definition missing|Parser for asset .* returned null",
        )
        self.assertNotIn("MDL simple name:", result.stdout)


if __name__ == "__main__":
    unittest.main()
