#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate the MDL interface from the OpenUSD selected by CMake."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile

# These are backend bindings, not copies of the canonical USD declarations.
# Missing optional families permit older USD releases; new families require an
# explicit MDL implementation decision. Versions retain their USD identifiers.
BINDINGS = {
    "UsdPreviewSurface": {
        "required": {"outputs:surface": ("token",), "outputs:displacement": ("token",)},
        "aliases": {"outputs:surface": "out", "outputs:displacement": "out"},
        "tokens": {"inputs:opacityMode": ("transparent", "presence")},
    },
    "UsdUVTexture": {
        "required": {"inputs:file": ("asset",), "outputs:r": ("float",),
                     "outputs:g": ("float",), "outputs:b": ("float",),
                     "outputs:a": ("float",), "outputs:rgb": ("float3", "color3f")},
        "metadata": {"inputs:file": {"__SDR__colorspace": "sourceColorSpace"}},
        "tokens": {"inputs:wrapS": ("useMetadata", "black", "clamp", "repeat", "mirror"),
                   "inputs:wrapT": ("useMetadata", "black", "clamp", "repeat", "mirror"),
                   "inputs:sourceColorSpace": ("auto", "sRGB", "raw")},
    },
    "UsdTransform2d": {
        "required": {"inputs:in": ("float2",), "outputs:result": ("float2",)},
        "aliases": {"inputs:in": "st", "outputs:result": "out"},
    },
}
for _suffix, _type in {
    "float": "float", "float2": "float2", "float3": "float3", "float4": "float4",
    "int": "int", "string": "string", "normal": "normal3f", "point": "point3f",
    "vector": "vector3f", "matrix": "matrix4d",
}.items():
    BINDINGS[f"UsdPrimvarReader_{_suffix}"] = {
        "required": {"outputs:result": (_type,)},
        "aliases": {"outputs:result": "out"},
    }


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _write_if_different(path: Path, data: bytes) -> None:
    if path.is_file() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + ".",
                                         suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def _load_usd(args):
    package = Path(args.usd_python).resolve()
    prefix = Path(args.usd_prefix).resolve()
    if not (package / "pxr" / "__init__.py").is_file():
        raise ValueError(f"selected SDK Python package is missing: {package}")
    # -I ignores ambient PYTHONPATH; this explicit package must win even when
    # the interpreter has another USD distribution in its site-packages.
    sys.path.insert(0, str(package))
    runtime_paths = [prefix / name for name in ("bin", "lib", "plugin/usd")]
    os.environ["PATH"] = os.pathsep.join(str(p) for p in runtime_paths) + os.pathsep + os.environ.get("PATH", "")
    os.environ["PXR_PLUGINPATH_NAME"] = os.pathsep.join(
        str(prefix / name) for name in ("lib/usd", "plugin/usd"))
    # POSIX loader paths must be set before Python starts. CMake supplies this;
    # direct CLI use restarts once before loading any USD extension modules.
    if sys.platform in ("linux", "darwin"):
        loader = "DYLD_LIBRARY_PATH" if sys.platform == "darwin" else "LD_LIBRARY_PATH"
        selected = os.pathsep.join(str(prefix / name) for name in ("lib", "lib64", "bin"))
        current = os.environ.get(loader, "")
        if current != selected and not current.startswith(selected + os.pathsep):
            marker = "_USDMDL_GENERATOR_LOADER_RESTART"
            if os.environ.get(marker) == selected:
                raise ValueError("selected SDK native loader environment was not retained; use a compatible host Python interpreter")
            os.environ[marker] = selected
            os.environ[loader] = selected + (os.pathsep + current if current else "")
            if __name__ != "__main__":
                raise ValueError("selected SDK native loader paths must be configured before importing USD")
            os.execv(sys.executable, [sys.executable, "-I", str(Path(__file__).resolve()), *sys.argv[1:]])
    dll_handles = []
    if hasattr(os, "add_dll_directory"):
        dll_handles = [os.add_dll_directory(str(p)) for p in runtime_paths if p.is_dir()]
    try:
        import pxr
        from pxr import Sdf, Usd, UsdShade
    except ImportError as exc:
        raise ValueError("cannot import the selected SDK's host-compatible USD Python bindings: " + str(exc)) from exc
    if Path(pxr.__file__).resolve().parent != package / "pxr":
        raise ValueError("Python imported pxr from a different SDK")
    expected = tuple(int(component) for component in args.usd_version.split("."))
    actual = tuple(Usd.GetVersion())
    if len(expected) != 3 or actual != expected:
        raise ValueError(f"selected USD version mismatch: CMake expects {expected}, Python provides {actual}")
    return Sdf, Usd, UsdShade, actual, dll_handles


def _family(identifier: str) -> str:
    for name in BINDINGS:
        if re.fullmatch(re.escape(name) + r"(?:_[0-9]+(?:_[0-9]+)?)?", identifier):
            return name
    raise ValueError(f"no MDL binding is defined for shader '{identifier}'")


def _patch_metadata(attribute, values) -> None:
    metadata = dict(attribute.GetMetadata("sdrMetadata") or {})
    for key, value in values.items():
        existing = metadata.get(key)
        if key == "implementationName":
            existing = metadata.get("__SDR__implementationName", existing)
        if existing is not None and str(existing) != value:
            raise ValueError(f"{attribute.GetPath()}: conflicting existing binding metadata '{key}'")
    metadata.update(values)
    if not attribute.SetMetadata("sdrMetadata", metadata):
        raise ValueError(f"cannot author binding metadata on {attribute.GetPath()}")


def _source_comments(data: bytes) -> list[str]:
    """Retain USDA comments that Sdf parsing/export would otherwise discard.

    Skip string, asset and path literals so their '#' characters do not become
    comments. Binary layers have no textual comments; their authored metadata
    is retained by TransferContent below.
    """
    if not data.startswith(b"#usda "):
        return []
    text = data.decode("utf-8")
    comments = []
    index = text.index("\n") + 1  # Exclude the USDA format declaration.
    while index < len(text):
        char = text[index]
        if char == "#":
            end = text.find("\n", index)
            if end < 0:
                end = len(text)
            comments.append(text[index:end].rstrip("\r"))
            index = end
        elif char in ('"', "'", "@", "<"):
            delimiter = ">" if char == "<" else char
            if char != "<" and text.startswith(char * 3, index):
                delimiter = char * 3
            index += len(delimiter)
            while index < len(text):
                if text[index] == "\\":
                    index += 2
                elif text.startswith(delimiter, index):
                    index += len(delimiter)
                    break
                else:
                    index += 1
        else:
            index += 1
    return comments


def generate(args, modules) -> None:
    Sdf, Usd, UsdShade, version, _dll_handles = modules
    source = Path(args.input).resolve(strict=True)
    output = Path(args.output).resolve()
    provenance = Path(args.provenance).resolve()
    license_source = Path(args.license).resolve(strict=True)
    license_output = Path(args.license_output).resolve() if args.license_output else output.parent / "OpenUSD-LICENSE.txt"
    notice_source = Path(args.notice).resolve(strict=True)
    notice_output = output.parent / "OpenUSD-NOTICE.txt"
    destinations = (output, provenance, license_output, notice_output)
    def same_file(a, b):
        return a == b or (a.exists() and b.exists() and a.samefile(b))
    if any(same_file(destination, source) for destination in destinations):
        raise ValueError("generated files must not overwrite the canonical input")
    if any(same_file(destination, protected) for destination in destinations
           for protected in (license_source, notice_source, Path(__file__).resolve())):
        raise ValueError("generated files must not overwrite the license/notice inputs or generator")
    if any(same_file(a, b) for index, a in enumerate(destinations) for b in destinations[index + 1:]):
        raise ValueError("generated files must be distinct")
    source_data = source.read_bytes()
    license_data = license_source.read_bytes()
    notice_data = notice_source.read_bytes()
    for name, data in (("license", license_data), ("notice", notice_data)):
        try:
            legal_text = data.decode("utf-8-sig")
        except UnicodeDecodeError as exc:
            raise ValueError(f"shader-definition {name} must be UTF-8 text") from exc
        if not legal_text.strip():
            raise ValueError(f"shader-definition {name} must not be empty")
    original = Sdf.Layer.FindOrOpen(str(source))
    if original is None:
        raise ValueError(f"cannot open canonical shader definitions: {source}")
    dependencies = (original.GetCompositionAssetDependencies() if hasattr(original, "GetCompositionAssetDependencies")
                    else original.GetExternalReferences())
    if dependencies:
        raise ValueError("external composition dependencies are not supported: " + ", ".join(sorted(dependencies)))
    # Input/output asset defaults and hierarchical MDL assets would acquire a
    # different anchor in the binary tree. Bare MDL names intentionally resolve
    # through the application's MDL search roots, rather than this USD layer.
    attribute_paths = []
    original.Traverse(Sdf.Path.absoluteRootPath,
                      lambda path: attribute_paths.append(path) if path.IsPropertyPath() else None)
    for path in attribute_paths:
        spec = original.GetAttributeAtPath(path)
        if spec is None:
            continue
        is_mdl_asset = str(spec.name) == "info:mdl:sourceAsset"
        if str(spec.name).startswith("info:") and not is_mdl_asset:
            continue
        if spec.typeName in (Sdf.ValueTypeNames.Asset, Sdf.ValueTypeNames.AssetArray):
            value = spec.default
            values = (value,) if spec.typeName == Sdf.ValueTypeNames.Asset else (value or ())
            for asset in values:
                text = asset.path if asset is not None else ""
                drive_path = bool(re.match(r"^[a-zA-Z]:", text))
                absolute = Path(text).is_absolute() or bool(re.match(r"^[a-zA-Z]:[/\\]", text)) or text.startswith("\\\\")
                uri = not drive_path and bool(re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", text))
                relative = bool(text) and not absolute and not uri
                if relative and is_mdl_asset and ("/" in text or "\\" in text or drive_path):
                    raise ValueError(f"{path}: relative MDL source asset requires an explicit resource packaging policy to preserve its layer anchor")
                if relative and not is_mdl_asset:
                    raise ValueError(f"{path}: relative asset default requires an explicit resource packaging policy")
    layer = Sdf.Layer.CreateAnonymous("UsdPreviewSurface.usda")
    layer.TransferContent(original)
    # Remove authored GLSLFX specs, including classes and inactive variants.
    # Removing an inherited property only on a concrete shader is insufficient.
    glslfx_properties = []
    layer.Traverse(Sdf.Path.absoluteRootPath,
                   lambda path: glslfx_properties.append(path)
                   if path.IsPropertyPath() and str(path.name).startswith("info:glslfx:") else None)
    asset_owners = set()
    for path in glslfx_properties:
        spec = layer.GetAttributeAtPath(path)
        if spec is None:
            raise ValueError(f"unsupported GLSLFX property spec {path}")
        if str(spec.name) == "info:glslfx:sourceAsset":
            asset_owners.add(spec.owner.path)
        spec.owner.RemoveProperty(spec)
    for owner_path in sorted(asset_owners):
        owner = layer.GetPrimAtPath(owner_path)
        existing = owner.properties.get("info:mdl:sourceAsset")
        if existing is None:
            replacement = Sdf.AttributeSpec(owner, "info:mdl:sourceAsset", Sdf.ValueTypeNames.Asset,
                                            Sdf.VariabilityUniform, False)
            replacement.default = Sdf.AssetPath("UsdPreviewSurface.mdl")
    stage = Usd.Stage.Open(layer)
    if stage is None:
        raise ValueError(f"cannot compose canonical shader definitions: {source}")
    shaders = []
    identifiers = set()
    present = set()
    for prim in stage.TraverseAll():
        if prim.IsAbstract() or not prim.IsA(UsdShade.Shader):
            continue
        shader = UsdShade.Shader(prim)
        identifier = str(shader.GetIdAttr().Get() or prim.GetName())
        if not Sdf.Path.IsValidNamespacedIdentifier(identifier):
            raise ValueError(f"invalid USD shader identifier '{identifier}'")
        if identifier in identifiers:
            raise ValueError(f"duplicate USD shader identifier '{identifier}'")
        identifiers.add(identifier)
        family = _family(identifier)
        rule = BINDINGS[family]
        present.add(family)
        for name, types in rule.get("required", {}).items():
            attribute = prim.GetAttribute(name)
            if not attribute:
                raise ValueError(f"{prim.GetPath()}: required property '{name}' is missing")
            if str(attribute.GetTypeName()) not in types:
                raise ValueError(f"{attribute.GetPath()}: unsupported mapped type '{attribute.GetTypeName()}', expected {types}")
        for name, binding in rule.get("aliases", {}).items():
            _patch_metadata(prim.GetAttribute(name), {"implementationName": binding})
        for name, metadata in rule.get("metadata", {}).items():
            _patch_metadata(prim.GetAttribute(name), metadata)
        for name, tokens in rule.get("tokens", {}).items():
            attribute = prim.GetAttribute(name)
            if attribute and str(attribute.GetTypeName()) == "token" and not attribute.HasAuthoredMetadata("allowedTokens"):
                attribute.SetMetadata("allowedTokens", list(tokens))
        asset = shader.GetSourceAsset("mdl")
        if asset is None or not asset.path:
            if not shader.SetSourceAsset(Sdf.AssetPath("UsdPreviewSurface.mdl"), "mdl"):
                raise ValueError(f"cannot author MDL source asset for {prim.GetPath()}")
        sub_identifier = shader.GetSourceAssetSubIdentifier("mdl")
        if sub_identifier:
            request = str(sub_identifier)
            local_name = request.split("(", 1)[0].rsplit("::", 1)[-1]
            if local_name != family or ("(" in request and not request.endswith(")")):
                raise ValueError(f"{prim.GetPath()}: conflicting existing MDL sub-identifier '{request}'")
        elif not shader.SetSourceAssetSubIdentifier(family, "mdl"):
            raise ValueError(f"cannot author MDL sub-identifier for {prim.GetPath()}")
        shaders.append({"identifier": identifier, "primPath": str(prim.GetPath()),
                        "mdlDefinition": str(shader.GetSourceAssetSubIdentifier("mdl"))})
    if "UsdPreviewSurface" not in present:
        raise ValueError("canonical input has no concrete UsdPreviewSurface definition")
    text = layer.ExportToString()
    header = ("#usda 1.0\n#\n"
              "# Licensed under the terms set forth in OpenUSD-LICENSE.txt.\n"
              "# See OpenUSD-NOTICE.txt for upstream attribution.\n"
              f"# Generated from selected OpenUSD {'.'.join(map(str, version))} usdShaders definitions.\n"
              "# Modified to bind the standard USD interface to MDL implementations.\n")
    if not text.startswith("#usda 1.0\n"):
        raise ValueError("USD did not export a USDA layer")
    comments = _source_comments(source_data)
    if comments:
        header += "#\n# Comments retained from the canonical source:\n" + "\n".join(comments) + "\n"
    generated = (header + text[len("#usda 1.0\n"):]).encode("utf-8")
    manifest = {
        "schemaVersion": 1,
        "usdVersion": list(version),
        "sourceAsset": source.as_posix(),
        "sourceSha256": _sha256(source_data),
        "licenseSha256": _sha256(license_data),
        "noticeSha256": _sha256(notice_data),
        "generatorSha256": _sha256(Path(__file__).read_bytes()),
        "outputSha256": _sha256(generated),
        "shaders": sorted(shaders, key=lambda shader: shader["primPath"]),
        "missingOptionalShaders": sorted(set(BINDINGS) - present),
    }
    _write_if_different(output, generated)
    _write_if_different(license_output, license_data)
    _write_if_different(notice_output, notice_data)
    _write_if_different(provenance, (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8"))
    print(f"Generated {len(shaders)} MDL shader interfaces from OpenUSD {'.'.join(map(str, version))}.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input")
    parser.add_argument("--output")
    parser.add_argument("--provenance")
    parser.add_argument("--license")
    parser.add_argument("--license-output")
    parser.add_argument("--notice")
    parser.add_argument("--usd-version", required=True)
    parser.add_argument("--usd-python", required=True)
    parser.add_argument("--usd-prefix", required=True)
    parser.add_argument("--probe", action="store_true")
    args = parser.parse_args()
    try:
        modules = _load_usd(args)
        if args.probe:
            print(f"Validated selected OpenUSD {'.'.join(map(str, modules[3]))} Python bindings.")
        else:
            if not all((args.input, args.output, args.provenance, args.license, args.notice)):
                raise ValueError("generation requires --input, --output, --provenance, --license and --notice")
            generate(args, modules)
    except Exception as exc:
        print(f"Shader definition generation failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
