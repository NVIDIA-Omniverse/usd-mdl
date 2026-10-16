#!/usr/bin/env python3
#
# SPDX-License-Identifier: Apache-2.0
#

import argparse

import UsdMdl
from pxr import Sdf, Usd, UsdShade


def inspect_material(stage_path: str, prim_path: str) -> None:
    stage = Usd.Stage.Open(stage_path)
    if not stage:
        raise RuntimeError(f"cannot open stage: {stage_path}")

    prim = stage.GetPrimAtPath(Sdf.Path(prim_path))
    if not prim:
        raise RuntimeError(f"prim does not exist: {prim_path}")

    material, _ = UsdShade.MaterialBindingAPI(prim).ComputeBoundMaterial()
    if not material:
        raise RuntimeError(f"prim has no bound material: {prim_path}")

    surface = material.GetSurfaceOutput("mdl")
    sources, _ = surface.GetConnectedSources()
    if len(sources) != 1:
        raise RuntimeError("material does not have exactly one MDL surface source")

    shader = UsdShade.Shader(sources[0].source.GetPrim())
    source_asset = shader.GetSourceAsset("mdl")
    sub_identifier = shader.GetSourceAssetSubIdentifier("mdl")
    if not source_asset or not sub_identifier:
        raise RuntimeError("MDL source asset metadata is incomplete")

    nodes = UsdMdl.AddModuleToRegistry(source_asset)
    if not nodes:
        raise RuntimeError(f"usdMdl could not load {source_asset}")

    node = shader.GetShaderNodeForSourceType("mdl")
    if not node:
        raise RuntimeError(
            f"usdMdl could not find MDL definition {sub_identifier} in {source_asset}"
        )

    print(
        f"Prim {prim.GetPath()} uses material {material.GetPath()} "
        f"from {source_asset.path} (MDL definition {sub_identifier})"
    )

    print("Sdr inputs:")
    for name in node.GetShaderInputNames():
        input_property = node.GetShaderInput(name)
        default_value = input_property.GetDefaultValue()
        details = [f"type={input_property.GetType()}"]
        if isinstance(default_value, Sdf.AssetPath):
            details.extend(
                [
                    f"authored={default_value.path}",
                    f"resolved={default_value.resolvedPath}",
                ]
            )
        else:
            details.append(f"default={default_value}")
        if input_property.GetLabel():
            details.append(f"label={input_property.GetLabel()}")
        if input_property.GetHelp():
            details.append(f"help={input_property.GetHelp()}")
        if input_property.GetPage():
            details.append(f"page={input_property.GetPage()}")
        details.extend(f"{key}={value}" for key, value in input_property.GetHints().items())
        print(f"  {name}: {', '.join(details)}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Print the MDL material bound to a USD primitive."
    )
    parser.add_argument("stage", help="USD scene to open")
    parser.add_argument("prim", help="primitive path to inspect")
    args = parser.parse_args()
    try:
        inspect_material(args.stage, args.prim)
    finally:
        # Release MDL SDK handles before Python tears down extension modules.
        UsdMdl.Shutdown()


if __name__ == "__main__":
    main()
