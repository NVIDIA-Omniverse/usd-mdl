# SPDX-License-Identifier: Apache-2.0
"""Reproduce rejected paths through cold and registered public Python lookup."""
import os
from pathlib import Path
import subprocess
import sys


def worker(mode: str) -> None:
    root = Path(os.environ["PXR_TEST_DIR"]) / "mdl"
    os.environ["USD_MDL_SEARCH_PATHS"] = str(root)
    os.environ["USD_MDL_DISCOVER_ON_STARTUP"] = "0"
    os.environ["USD_MDL_ADD_USER_PATHS"] = "0"
    os.environ["USD_MDL_ADD_SYSTEM_PATHS"] = "0"
    for variable in ("MDL_USER_PATH", "MDL_SYSTEM_PATH", "TF_DEBUG"):
        os.environ.pop(variable, None)
    from pxr import Ar, Sdf, Usd, UsdShade
    import UsdMdl

    stage = Usd.Stage.CreateInMemory()
    retained = []
    cases = (
        ("../../../texture.png", None),
        ("./../nested/texture.png", None),
        ("./texture.png", root / "d3/nested/texture.png"),
        ("../../texture.png", root / "texture.png"),
        ("./sub/../tile_<UDIM>.png", None),
        ("./sub/../frame.<##>.png", None),
    )
    (root / "d3/nested/sub").mkdir(exist_ok=True)
    try:
        for index, (path, expected) in enumerate(cases):
            source = root / "d3/nested" / ("python_" + str(index) + ".mdl")
            source.write_text(
                'mdl 1.8;\nexport texture_2d probe(\n'
                f'uniform texture_2d tex = texture_2d("{path}"),\n'
                'uniform texture_2d root = texture_2d("/texture.png")) { return tex; }\n',
                encoding="utf-8",
            )
            asset = Sdf.AssetPath(str(source), Ar.GetResolver().Resolve(str(source)))
            if mode == "registered":
                nodes = list(UsdMdl.AddModuleToRegistry(asset))
                assert len(nodes) == 1 and nodes[0].IsValid(), (path, nodes)
                retained.extend(nodes)
            shader = UsdShade.Shader.Define(stage, "/Shader" + str(index))
            assert shader.SetSourceAsset(asset, "mdl")
            assert shader.SetSourceAssetSubIdentifier("probe", "mdl")
            node = shader.GetShaderNodeForSourceType("mdl")
            assert node and node.IsValid(), path
            texture = node.GetShaderInput("tex").GetDefaultValue()
            assert isinstance(texture, Sdf.AssetPath), (path, texture)
            if expected is None:
                assert texture == Sdf.AssetPath(), (path, texture)
            else:
                assert texture.path and Path(texture.resolvedPath).samefile(expected), (path, texture)
            valid_root = node.GetShaderInput("root").GetDefaultValue()
            assert valid_root.path == "/texture.png", valid_root
            assert Path(valid_root.resolvedPath).samefile(root / "texture.png"), valid_root
            repeated = shader.GetShaderNodeForSourceType("mdl")
            assert repeated and repeated.GetIdentifier() == node.GetIdentifier(), path
            node = repeated = None
        print("Rejected-resource Python passed " + mode, flush=True)
    finally:
        retained.clear()
        UsdMdl.Shutdown()


def main() -> None:
    mode = sys.argv[1]
    assert mode in ("cold", "registered"), mode
    if len(sys.argv) > 2 and sys.argv[2] == "--worker":
        worker(mode)
        return
    result = subprocess.run(
        [sys.executable, str(Path(__file__).resolve()), mode, "--worker"],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=90,
    )
    output = result.stdout + result.stderr
    assert result.returncode == 0, (mode, result.returncode, output)
    assert "Rejected-resource Python passed " + mode in output, output
    # The SDK's independent logger retains its raw messages. Assert the USD
    # presentation channel, whose lines are emitted by Tf's default delegate.
    usd_warnings = "\n".join(line for line in output.splitlines() if line.startswith("Warning:"))
    for path in ("../../../texture.png", "./../nested/texture.png", "./sub/../tile_<UDIM>.png", "./sub/../frame.<##>.png"):
        assert path in usd_warnings, (path, output)
    assert "/__usdMdl_rejected__/" not in usd_warnings, usd_warnings
    print(output, end="", flush=True)


if __name__ == "__main__":
    main()
