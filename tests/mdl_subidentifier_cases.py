# SPDX-License-Identifier: Apache-2.0
"""Lookup expectations derived from MDL fixture declarations, not plugin helpers."""
from dataclasses import dataclass
import itertools


@dataclass
class Case:
    group: str
    request: str
    canonical: str | None
    signature: str = ""
    inputs: tuple = ()
    module: str = "pkg/main"


def legacy_material_cases(enabled):
    """Expected definitions come from the fixture, including stale type lists."""
    result = []
    for name, signature, inputs, stale in (
        ("paint", "paint(color,float)", ("tint", "weight"),
         ("paint(color,int)", "paint(float,color)", "paint(color)", "paint()",
          "paint(color,float,int)", "paint(::old::RemovedType)",
          " paint ( color , int ) ")),
        ("plain", "plain()", (), ("plain(float)",)),
        ("hidden_paint", "hidden_paint(float)", ("weight",), ("hidden_paint(int)",)),
        ("paint_local", "paint_local(::pkg::main::Local)", ("value",),
         ("paint_local(::old::Local)",)),
        ("paint_resources", "paint_resources(texture_2d[2])", ("images",),
         ("paint_resources(texture_2d[3])",)),
    ):
        for request in (name, signature):
            result.append(Case("exact_material", request, name, signature, inputs))
        for request in stale:
            result.append(Case("legacy_material_fallback", request,
                               name if enabled else None, signature, inputs))
    # Equivalent type qualification is an exact match, with no fallback warning.
    result.append(Case("exact_material", "paint_local(.::main::Local)",
                       "paint_local", "paint_local(::pkg::main::Local)", ("value",)))
    # The source asset still selects the module, even with the same material name.
    result.append(Case("legacy_material_fallback", "paint(int)",
                       "paint" if enabled else None, "paint(float)", ("other_weight",),
                       "pkg/second"))
    for request in (
        "missing(float)", "Paint(color,int)", "private_paint(int)",
        "paint(", "paint(color,float))", "paint(color,,float)", "paint(color,float,)",
        "paint((color),float)", "paint(color,float)junk", "paint(color;float)",
        "paint(color float)", "paint(texture_2d[2)", "paint(texture_2d2])",
        "paint(texture_2d[])", "paint(texture_2d[[2]])",
        "create_layer(color,int)", "create_layer()", "pick(double)",
        "collision()", "Local(int,int)",
    ):
        result.append(Case("legacy_still_rejected", request, None))
    # A material present in another module must not become a fallback candidate.
    result.append(Case("legacy_module_isolation", "plain(float)", None, module="pkg/second"))
    return result


def cases():
    cases = [
        ("create_layer(color,float)", "create_layer", "create_layer(color,float)", ["value", "alpha"]),
        ("create_layer", "create_layer", "create_layer(color,float)", ["value", "alpha"]),
        ("create_layer( color, float )", "create_layer", "create_layer(color,float)", ["value", "alpha"]),
        ("zero()", "zero", "zero()", []), ("zero", "zero", "zero()", []),
        ("plain()", "plain", "plain()", []), ("plain", "plain", "plain()", []),
        ("paint(color,float)", "paint", "paint(color,float)", ["tint", "weight"]),
        ("paint", "paint", "paint(color,float)", ["tint", "weight"]),
        ("external_only(::pkg::another::Thing)", "external_only",
         "external_only(::pkg::another::Thing)", ["external_value"]),
        ("external_local(::other::types::Local)", "external_local",
         "external_local(::other::types::Local)", ["external_value"]),
    ]
    type_cases = [
        ("Local", "::pkg::main::Local", ["Local", "::pkg::main::Local", ".::main::Local"], "local_value", "read_local"),
        ("Mode", "::pkg::main::Mode", ["Mode", "::pkg::main::Mode", ".::main::Mode"], "mode_value", "read_enum"),
        ("::pkg::another::Bar2", "::pkg::another::Bar2", ["::pkg::another::Bar2", ".::another::Bar2", "..::pkg::another::Bar2"], "sibling_value", "read_sibling"),
        ("::tex::wrap_mode", "::tex::wrap_mode", ["::tex::wrap_mode"], "library_value", "read_library"),
        ("int", "int", ["int"], "integer_value", None),
        ("float", "float", ["float"], "float_value", None),
    ]
    for canonical_type, full, spellings, param, unique in type_cases:
        for spelling in spellings:
            cases.append((f"pick({spelling})", f"pick({canonical_type})", f"pick({full})", [param]))
            if unique:
                cases.append((f"{unique}({spelling})", unique, f"{unique}({full})", [param]))
            if canonical_type in ("Local", "::pkg::another::Bar2"):
                name = "paint_local" if canonical_type == "Local" else "paint_sibling"
                cases.append((f"{name}({spelling})", name, f"{name}({full})", ["value"]))
        if unique:
            cases.append((unique, unique, f"{unique}({full})", [param]))
    cases.append(("pick()", "pick()", "pick()", []))
    for name, param in (("Mode", "v"), ("int", "x")):
        for ty in ("Mode", "::pkg::main::Mode", ".::main::Mode"):
            cases.append((f"{name}({ty})", name, f"{name}(::pkg::main::Mode)", [param]))
    for owner, param in (("pkg::another", "from_a"), ("other::types", "from_b")):
        sig = f"collision(::{owner}::Thing)"
        cases.append((sig, sig, sig, [param]))
    for sig in ("arrays(int[3])", "arrays(float[N])"):
        cases.append((sig, sig, sig, ["a"]))
    for ty, param in (("float3", "v"), ("float2x3", "m"), ("texture_2d[2]", "images")):
        sig = f"shapes({ty})"
        cases.append((sig, sig, sig, [param]))
    for ty in ("Local", "::pkg::main::Local", ".::main::Local"):
        cases.append((f"local_array({ty}[2])", "local_array", "local_array(::pkg::main::Local[2])", ["a"]))
        cases.append((f"Local({ty})", "Local(Local)", "Local(::pkg::main::Local)", ["other"]))
    cases.append(("Local(int,float)", "Local(int,float)", "Local(int,float)", ["i", "f"]))
    for local, sibling in itertools.product(type_cases[0][2], type_cases[2][2]):
        cases.append((f"mixed({local},{sibling},float)", "mixed",
                      "mixed(::pkg::main::Local,::pkg::another::Bar2,float)",
                      ["local_value", "sibling_value", "weight"]))

    invalid = [
        "", "missing", "missing(int)", "create_layer(color,int)",
        "create_layer(float,color)", "create_layer(color)", "create_layer()",
        "create_layer(color,float,int)", "create_layer(color,float", "create_layer(color,float,)",
        "create_layer(color,,float)", "create_layer(color,float)junk", "create_layer((color),float)",
        "pick(.::Local)", "pick(Bar2)", "pick(wrap_mode)", "pick(::wrong::Local)",
        "Local.i(int)", "Local.i(::other::types::Local)",
        "pick(double)", "collision()", "collision(::wrong::Thing)",
        "external_only(::other::types::Thing)",
        "external_local(Local)", "read_local(::other::types::Local)",
        "read_local(.::another::Local)", "read_sibling(Bar2)",
        "paint(color,int)", "paint(color)", "paint()", "plain(float)",
        "paint_local(.::Local)", "paint_sibling(Bar2)",
        "arrays(int[4])", "arrays(float[M])", "arrays(float[3])", "arrays(float[<N>])",
        "local_array(Local[3])", "shapes(float2)", "shapes(float3x2)", "shapes(texture_3d[2])",
    ]
    result = [Case("valid", request, canonical, signature, tuple(inputs))
              for request, canonical, signature, inputs in cases]
    result.extend(Case("invalid", request, None) for request in invalid)

    def yes(group, request, canonical, signature, inputs, module="pkg/main"):
        result.append(Case(group, request, canonical, signature, tuple(inputs), module))

    def no(group, requests, module="pkg/main"):
        result.extend(Case(group, request, None, module=module) for request in requests)

    for spelling in ("Thing", "::pkg::main::Thing", ".::main::Thing"):
        yes("local_basename", f"pick_thing({spelling})", "pick_thing(Thing)",
            "pick_thing(::pkg::main::Thing)", ["local_value"])
        no("local_without_overload", [f"collision({spelling})", f"external_only({spelling})"])
    for ty, param in (("::pkg::another::Thing", "from_a"), ("::other::types::Thing", "from_b")):
        yes("external_basename", f"pick_thing({ty})", f"pick_thing({ty})", f"pick_thing({ty})", [param])
    for ty, full, param in (("Local", "::pkg::main::Local", "local_value"),
                          ("::other::types::Local", "::other::types::Local", "external_value")):
        yes("local_external_collision", f"scoped({ty})", f"scoped({ty})", f"scoped({full})", [param])
    yes("eligibility", "eligible(int)", "eligible(int)", "eligible(int)", ["visible"])
    yes("eligibility", "eligible(float)", "eligible(float)", "eligible(float)", ["hidden_value"])
    for request in ("hidden_paint", "hidden_paint(float)"):
        yes("eligibility", request, "hidden_paint", "hidden_paint(float)", ["weight"])
    for request in ("Local.i", "Local.i(Local)", "Local.i(::pkg::main::Local)"):
        yes("eligibility", request, "Local.i", "Local.i(::pkg::main::Local)", ["s"])
    yes("eligibility", "public_caller(int)", "public_caller", "public_caller(int)", ["value"])
    no("eligibility", ["private_function", "private_function(int)", "private_paint", "private_paint(float)"])

    for ty in ("::pkg::another::Bar2", ".::another::Bar2", "..::pkg::another::Bar2"):
        for size in ("2", "N"):
            full = f"sibling_array(::pkg::another::Bar2[{size}])"
            yes("qualified_arrays", f"sibling_array({ty}[{size}])", full, full, ["a"])
    for ty in ("Mode", "::pkg::main::Mode", ".::main::Mode"):
        yes("enum_arrays", f"enum_array({ty}[2])", "enum_array", "enum_array(::pkg::main::Mode[2])", ["a"])
        yes("material_enum", f"paint_enum({ty})", "paint_enum", "paint_enum(::pkg::main::Mode)", ["value"])
    yes("material_resources", "paint_resources(texture_2d[2])", "paint_resources",
        "paint_resources(texture_2d[2])", ["images"])
    no("qualified_arrays", ["sibling_array(Bar2[2])", "sibling_array(.::another::Bar2[3])",
        "sibling_array(::other::types::Bar2[2])", "enum_array(Mode[3])", "paint_resources(texture_2d)"])

    for ty, param, spellings in (
        ("Local", "own", ("Local", "::pkg::deep::main::Local", ".::main::Local")),
        ("::pkg::another::Thing", "cousin", ("::pkg::another::Thing", "..::another::Thing", "..::..::pkg::another::Thing")),
        ("::other::types::Thing", "distant", ("::other::types::Thing", "..::..::other::types::Thing"))):
        full = "::pkg::deep::main::Local" if ty == "Local" else ty
        for spelling in spellings:
            yes("nested_packages", f"nested({spelling})", f"nested({ty})", f"nested({full})", [param], "pkg/deep/main")
    no("nested_packages", ["nested(.::another::Thing)", "nested(..::other::types::Thing)",
        "nested(..::..::..::other::types::Thing)", "nested(Thing)", "nested(::pkg::main::Local)"], "pkg/deep/main")
    for ty in ("Local", "::top::Local", ".::top::Local"):
        yes("root_package", f"top({ty})", "top(Local)", "top(::top::Local)", ["own"], "top")
    yes("root_package", "top(.::pkg::another::Thing)", "top(::pkg::another::Thing)",
        "top(::pkg::another::Thing)", ["external_value"], "top")
    no("root_package", ["top(..::pkg::another::Thing)", "top(.::Local)"], "top")

    # Re-exporting does not change these SDK parameter identities.
    for name, ty, short in (("reexport_enum", "::tex::wrap_mode", "wrap_mode"),
                             ("reexport_struct", "::pkg::another::Bar2", "Bar2")):
        yes("reexports", f"{name}({ty})", name, f"{name}({ty})", ["value"], "pkg/reexport")
        no("reexports", [f"{name}({short})", f"{name}(::pkg::reexport::{short})"], "pkg/reexport")
    yes("reexports", "reexport_struct(.::another::Bar2)", "reexport_struct",
        "reexport_struct(::pkg::another::Bar2)", ["value"], "pkg/reexport")

    types = ["bool", "int", "float", "double", "string", "color", "intensity_mode",
             "texture_2d", "texture_3d", "texture_cube", "texture_ptex", "light_profile", "bsdf_measurement"]
    types += [f"{base}{size}" for base in ("bool", "int", "float", "double") for size in (2, 3, 4)]
    types += [f"{base}{columns}x{rows}" for base in ("float", "double")
              for columns in (2, 3, 4) for rows in (2, 3, 4)]
    for ty in types:
        signature = f"builtin({ty})"
        yes("builtin_types", signature, signature, signature, ["value"], "pkg/builtins")
    for ty in ("bsdf", "hair_bsdf", "edf", "vdf", "material", "material_surface",
               "material_emission", "material_volume", "material_geometry"):
        for request in (f"pass_{ty}", f"pass_{ty}({ty})"):
            yes("declarative_types", request, f"pass_{ty}", f"pass_{ty}({ty})", ["value"], "pkg/builtins")
    for ty in ("bool", "int", "float", "double", "string", "color", "float3", "float2x3",
               "texture_2d", "light_profile", "bsdf_measurement"):
        signature = f"builtin_array({ty}[2])"
        yes("builtin_arrays", signature, signature, signature, ["values"], "pkg/builtins")
        no("builtin_arrays", [f"builtin_array({ty})", f"builtin_array({ty}[3])"], "pkg/builtins")
    no("builtin_types", ["builtin(::pkg::builtins::float)", "builtin(uniform float)",
        "builtin(varying float)", "builtin(float5)", "builtin(float2x5)", "builtin(float[])",
        "builtin(::intensity_mode)", "builtin(texture_2d<float>)"], "pkg/builtins")

    for request in (" create_layer(color,float) ", "create_layer ( color , float )", "create_layer\t(color,\tfloat)",
                    "\ncreate_layer(color,float)\n"):
        yes("whitespace", request, "create_layer", "create_layer(color,float)", ["value", "alpha"])
    no("malformed", [" ", "()", "(float)", "create_layer)", "create_layer(color,float))",
        "create_layer(color,float)(color,float)", "create_layer(color;float)", "create_layer(color float)",
        "create_layer(,color,float)", "create layer(color,float)", "Create_layer(color,float)",
        "::pkg::main::create_layer(color,float)", "create_layer(color/*comment*/,float)",
        "pick(.::::main::Local)", "pick(..::.::main::Local)", "pick(.::main::)", "pick(.::)"])

    for request, canonical, signature, inputs in (
        ("unique", "unique", "unique({owner}::Local)", ["own"]),
        ("unique(Local)", "unique", "unique({owner}::Local)", ["own"]),
        ("unique(.::main::Local)", "unique", "unique({owner}::Local)", ["own"]),
        ("use(Local)", "use(Local)", "use({owner}::Local)", ["own"]),
        ("use(.::neighbor::Thing)", "use(.::neighbor::Thing)", "use({package}::neighbor::Thing)", ["sibling"]),
        ("use(::tex::wrap_mode)", "use(::tex::wrap_mode)", "use(::tex::wrap_mode)", ["library"]),
        ("sibling_array(.::neighbor::Thing[2])", "sibling_array", "sibling_array({package}::neighbor::Thing[2])", ["a"]),
        ("paint(Local)", "paint", "paint({owner}::Local)", ["own"]),
        ("paint", "paint", "paint({owner}::Local)", ["own"])):
        yes("offpath", request, canonical, signature, inputs, "@offpath/main")
    no("offpath", ["use(Thing)", "use(wrap_mode)", "use(.::Local)", "use(::wrong::Local)",
        "unique(.::neighbor::Thing)", "unique(float)", "unique()", "paint(float)",
        "sibling_array(.::neighbor::Thing[3])"], "@offpath/main")
    # Every distinct accepted definition also gets an invalid explicit signature.
    # This detects a fallback that accidentally accepts arbitrary type lists for
    # just one category (e.g. materials, builtins, or imported types).
    covered = set()
    for case in list(result):
        key = (case.module, case.signature)
        if case.canonical is None or "(" not in case.request or key in covered:
            continue
        covered.add(key)
        request = case.request.strip()
        separator = "" if request.endswith("()") else ","
        result.append(Case("never_ignore_signature", request[:-1] + separator + "__invalid_type__)",
                           None, module=case.module))
    return result
