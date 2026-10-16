#
# SPDX-License-Identifier: Apache-2.0
#
"""Smoke-test the installed package against test-owned MDL content."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path

from pxr import Sdf, Usd
import UsdMdl


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> None:
    install_prefix = Path(os.environ["USD_MDL_INSTALL_PREFIX"]).resolve()
    package_file = Path(UsdMdl.__file__).resolve()
    installed_python_root = (install_prefix / "lib" / "python").resolve()

    _require(
        package_file.is_relative_to(installed_python_root),
        f"UsdMdl was not imported from the install tree: {package_file}",
    )
    _require(not (install_prefix / "mdl").exists(), "unexpected bundled MDL content")
    resources = install_prefix / "plugin" / "usd" / "usdMdl" / "resources"
    for name in ("UsdPreviewSurface.usda", "UsdPreviewSurface.provenance.json", "OpenUSD-LICENSE.txt", "OpenUSD-NOTICE.txt"):
        _require((resources / name).is_file(), f"installed shader resource missing: {name}")
    provenance = json.loads((resources / "UsdPreviewSurface.provenance.json").read_text(encoding="utf-8"))
    _require(provenance["schemaVersion"] == 1, "unsupported installed resource provenance")
    _require(provenance["usdVersion"] == list(Usd.GetVersion()), "generated resource has a different USD version")
    for key, name in (("outputSha256", "UsdPreviewSurface.usda"), ("licenseSha256", "OpenUSD-LICENSE.txt"),
                      ("noticeSha256", "OpenUSD-NOTICE.txt")):
        digest = hashlib.sha256((resources / name).read_bytes()).hexdigest()
        _require(provenance[key] == digest, f"installed resource hash mismatch: {name}")
    for name in ("OpenUSD-LICENSE.txt", "OpenUSD-NOTICE.txt"):
        _require((resources / name).read_text(encoding="utf-8-sig").strip(), f"installed legal text is empty: {name}")
    _require(any(item["mdlDefinition"] == "UsdPreviewSurface" for item in provenance["shaders"]),
             "installed canonical Preview Surface interface is missing")
    _require(callable(UsdMdl.AddShaderDefinitionsToRegistry), "installed shader registration API missing")
    _require(
        (install_prefix / "THIRD_PARTY_NOTICES.md").is_file(),
        "installed third-party notices are missing",
    )

    _require(
        (install_prefix / "LICENSE").is_file(),
        "installed project license is missing",
    )

    search_paths = [
        Path(path).resolve()
        for path in os.environ["USD_MDL_SEARCH_PATHS"].split(os.pathsep)
    ]
    module = "installed_smoke"
    fixture = Path(__file__).resolve().parent / "mdl" / f"{module}.mdl"
    expected_paths = [fixture.parent.resolve()]
    _require(
        search_paths == expected_paths,
        f"helper changed the application's selected MDL search paths: {search_paths}",
    )

    nodes = UsdMdl.AddModuleToRegistry(Sdf.AssetPath(str(fixture)))
    try:
        _require(len(nodes) == 1, f"expected one {module} node, got {len(nodes)}")
        node = nodes[0]
        _require(node.IsValid(), "installed node is invalid")
        prop = node.GetShaderInput("value")
        _require(prop.GetDefaultValue() == 0.5, "incorrect application default")
        _require(prop.GetLabel() == "Value", "application label annotation was not mapped")
        _require(not prop.IsConnectable(), "application connectable annotation was not mapped")
        _require(prop.GetHelp() == "Application-supplied parameter", "embedded SDK annotation was not mapped")
        inputs = [str(name) for name in node.GetShaderInputNames()]
        outputs = [str(name) for name in node.GetShaderOutputNames()]
        _require(inputs == ["value"], f"unexpected inputs: {inputs}")
        _require(outputs == ["out"], f"unexpected outputs: {outputs}")
        _require(str(node.GetSourceType()) == "mdl", f"unexpected source type: {node.GetSourceType()}")
    finally:
        node = None
        nodes.clear()
        UsdMdl.Shutdown()

    print("Installed usdMdl package smoke test passed.")


if __name__ == "__main__":
    main()
