<!-- SPDX-License-Identifier: Apache-2.0 -->

# Python API: `UsdMdl`

The plugin ships a Python extension module imported as:

```python
import UsdMdl
```

**`UsdMdl` is a top-level package**, not `pxr.UsdMdl`. See the
[Why not `pxr.UsdMdl`?](#why-not-pxrusdmdl) section at the bottom for
the rationale.

## Setup

Activate the [runtime environment](environment-variables.md#quick-setup) so
OpenUSD can load the plugin and Python can import both `pxr` and `UsdMdl`.
Configure [MDL search roots](environment-variables.md#mdl-search-paths) before
the first MDL/Sdr operation.

## API surface

### `UsdMdl.CanConnectTo(property, other) -> bool`

Check whether two `Sdr.ShaderProperty` ports have compatible native MDL
declarations. Either argument order is accepted; the helper identifies the
input and output. `None`, two inputs, two outputs, a non-connectable input,
and properties without recognized `mdl.type` metadata return `False`.

For two resolved MDL nodes, use their Sdr properties directly:

```python
import UsdMdl

input_port = downstream_node.GetShaderInput("param")
output_port = upstream_node.GetShaderOutput("out")
can_connect = UsdMdl.CanConnectTo(input_port, output_port)
```

The check uses original MDL type metadata even when `renderType` or the USD
representation has been overridden. It compares exact type families, shapes,
named enum/struct identity and fixed array lengths. A fixed array output can
supply a deferred-size input; a deferred output cannot supply a fixed input.
An explicitly varying output cannot supply a uniform input. No implicit MDL
conversions are attempted. Missing required metadata or malformed type, array
or modifier metadata is rejected.

A `True` result describes local structural compatibility. The helper does not
bind deferred lengths across a function call, infer uniformity from the graph,
validate USD graph topology, or evaluate an MDL call. An absent modifier does
not prove uniformity. See [connection checks](type-mapping.md#client-side-mdl-connection-checks)
for the rules and limits. The C++ equivalent is `pxr::UsdMdlCanConnectTo` from
[`canConnectTo.h`](../src/usdMdl/canConnectTo.h).

### `UsdMdl.AddModuleToRegistry(assetPath) -> list[Sdr.ShaderNode]`

Load one MDL module from `assetPath` and register its exported, non-removed
function and material definitions with `Sdr.Registry()`. Return only nodes
for which `IsValid()` is true. Hidden definitions are included; their
[visibility metadata](#node-visibility) lets editors control presentation.

Equivalent to the C++ free function
`pxr::UsdMdlAddModuleToRegistry(SdfAssetPath)`.

`assetPath` may be:

- A bare `str` (treated as a USD asset path).
- An `Sdf.AssetPath` (preserves any pre-resolved path).

For file-backed content, the supported input identifies a module under a
configured MDL search root; the module uses its natural MDL qualified name.
[Embedded SDK modules](ar-resolver.md#embedded-sdk-modules-and-dummy-files),
such as `math.mdl` and `base.mdl`, can also be registered without source files.
Use `Sdf.AssetPath("mdl-builtin.mdl")` to register
[builtin constructors](ar-resolver.md#base-and-builtin-constructors).
The generic array constructor is omitted: request a concrete
[array specialization](sub-identifiers.md#requesting-nodes-from-an-editor)
through Sdr on demand.
Off-search-path loading is
outside the current supported scope, even though the implementation contains
URI-encoding support for some such cases. An absolute asset path is not a
substitute for configuring its MDL root.

Returns an empty list on failure (empty path, MDL SDK not initialized,
module fails to load); a `Tf` diagnostic is emitted in those cases.
An individual definition that fails to parse is omitted with a diagnostic;
other successfully parsed definitions are still returned. A module with no
eligible, successfully parsed definitions also returns an empty list.

The returned `Sdr.ShaderNode` instances are owned by `Sdr.Registry()` --
they are non-owning Python views.  Hold the registry alive (e.g. by
keeping `Sdr.Registry()` reachable) if you intend to use them later.

A material graph editor can use this API to register the modules containing
its default palette, then add libraries on request. Startup discovery is
disabled by default; see [discovery behavior](environment-variables.md#discovery-behavior)
for the distinction between catalog registration and lazy asset lookup.

Example:

```python
from pxr import Sdf, Sdr
import UsdMdl

try:
    nodes = UsdMdl.AddModuleToRegistry(Sdf.AssetPath("OmniPBR.mdl"))
    for node in nodes:
        print(node.GetIdentifier(), node.GetSourceType())

    if nodes:
        registry = Sdr.Registry()
        node = registry.GetShaderNodeByIdentifierAndType(
            nodes[0].GetIdentifier(), UsdMdl.Tokens.Mdl)
finally:
    UsdMdl.Shutdown()
```

### `UsdMdl.AddShaderDefinitionsToRegistry(definitionsAsset[, implementationAsset]) -> list[Sdr.ShaderNode]`

Register MDL implementations using interfaces authored as `UsdShade` Shader
definitions in a USD asset. `definitionsAsset` and the optional
`implementationAsset` accept strings or `Sdf.AssetPath` values. Each concrete
Shader supplies an `mdl` source asset and sub-identifier; its authored USD
properties provide exposed names, types, defaults, options, and metadata.
Abstract class prims are skipped. Invalid definitions emit diagnostics and are
omitted; successfully registered nodes are owned by `Sdr.Registry()`.
Explicit array constructor specializations are supported with the same
[element-module contract](sub-identifiers.md#builtin-array-constructors) as asset
lookup. Their interfaces expose every concrete argument exactly once and retain
required inputs when no USD default is authored. See
[array constructor interfaces](shader-definitions.md#array-constructor-interfaces)
for an example with renamed ports.

When supplied, `implementationAsset` replaces every Shader's MDL source asset.
It selects an application-owned module while retaining the interface asset's
individual sub-identifiers. Configure that module's MDL search root and imports
before the first MDL/Sdr operation. Registry identifiers remain immutable once
cached.

Equivalent to the C++ function
`pxr::UsdMdlAddShaderDefinitionsToRegistry(definitionsAsset, implementationAsset)`
in `<usdMdl/shaderDefinitions.h>`. See [USD shader definitions](shader-definitions.md)
for the installed Preview Surface interface, port bindings, renderer enum
conversion, and an example.

### `UsdMdl.Shutdown() -> None`

Release `usdMdl`'s MDL SDK handles and plugin-owned runtime state. Call this
after the last `Sdr`/`usdMdl` operation and before Python interpreter teardown.
It is safe to call more than once. Python cannot inject a host-owned
`INeuray` pointer; native hosts use the C++ lifecycle API for that case.

## Token registries

The token sets defined in [src/usdMdl/tokens.h](../src/usdMdl/tokens.h)
are exposed as Python attributes on the `UsdMdl` module. Each is a
generated holder class with one attribute per defined token. The token
**values** are the string literals that appear in Sdr metadata
dictionaries, so use these constants instead of hard-coding strings.

| Python attribute | C++ token set | Use |
|---|---|---|
| `UsdMdl.Tokens` | `UsdMdlTokens` | `Mdl` (source type string `"mdl"`), `DefaultOutputPortName` (`"out"`). |
| `UsdMdl.Types` | `UsdMdlTypes` | MDL type names used in render-type and array-element metadata, rather than USD storage type names (`Color`, `Float3`, `Texture2d`, `Bsdf`, `Edf`, `Struct`, ...). |
| `UsdMdl.Metadata` | `UsdMdlMetadataTokens` | Keys of MDL-specific Sdr **property**-level metadata (`Annotation`, `StructType`, `HasDefault`, `ExpressionKind`, `TextureGamma`, `Thumbnail`, ...). Exposed as `UsdMdl.Metadata` rather than `UsdMdl.MetadataTokens` to match OpenUSD's `Sdr.PropertyMetadata` convention. See [default metadata](type-mapping.md#structs-and-default-expressions). |
| `UsdMdl.NodeMetadata` | `UsdMdlNodeMetadataTokens` | Keys of MDL-specific Sdr **node**-level metadata exposed on `SdrShaderNode::GetMetadata()`, described below. |
| `UsdMdl.ExpressionKinds` | `UsdMdlExpressionKinds` | Default-expression metadata values (`Constant`, `Call`, `Parameter`); see [default expressions](type-mapping.md#structs-and-default-expressions). |
| `UsdMdl.StructTypes` | `UsdMdlStructTypes` | Values for `Metadata.StructType` (`material`, `material_surface`, `material_emission`, `material_geometry`, `material_volume`, `user`). |
| `UsdMdl.TypeModifiers` | `UsdMdlTypeModifiers` | Values for `Metadata.Modifier` (`uniform`, `varying`). |
| `UsdMdl.FunctionDefinitionTypes` | `UsdMdlFunctionDefinitionTypes` | Values for `NodeMetadata.DefinitionType` (`function`, `material`). |

Example:

```python
import UsdMdl

# Access the source-type string the plugin registers under:
print(UsdMdl.Tokens.Mdl)               # 'mdl'

# Walk Sdr metadata using typed token keys:
metadata = node.GetShaderInput("param").GetMetadata()
struct_type = metadata.get(UsdMdl.Metadata.StructType)
if struct_type == UsdMdl.StructTypes.Material:
    ...
```

The exact list of values per holder is the C++ `*_TOKENS` macro in
[tokens.h](../src/usdMdl/tokens.h); it's the source of truth.

### MDL node metadata

See [node and property metadata](metadata.md) for explicit `SDR__` annotation
promotion, built-in mappings, conflict rejection and precedence. Unprefixed
custom annotations remain in the serialized `mdl.annotation` field; they do
not set standard Sdr fields. That field retains ordered records for every
convertible occurrence returned by the SDK, including full names, signatures
and complete arguments. Promoted annotations remain in those records as well; see
[annotation storage](metadata.md#annotation-scope-and-storage).
The keys below describe SDK identity and plugin-specific node information.

These string values are available through `node.GetMetadata()` in Python or
`SdrShaderNode::GetMetadata()` in C++. The C++ token set is
`UsdMdlNodeMetadataTokens`; Python uses `UsdMdl.NodeMetadata`.

| Token | Metadata key | Meaning |
| --- | --- | --- |
| `ModuleUsdIdentifier` | `mdl:moduleUsdIdentifier` | Portable module reference with forward slashes and `.mdl`, e.g. `nvidia/core_definitions.mdl`. For file-backed modules, relative to the first matching MDL search root; for embedded modules, derived from the logical MDL name, e.g. `math.mdl`, even without a dummy file. Empty for file-backed modules outside configured roots. |
| `MdlNameWithSignature` | `mdl:mdlNameWithSignature` | Definition name and complete parameter type signature, with the owning module prefix removed, e.g. `diffuse(color,float,float3)`. For a specialized builtin array constructor, the resolved SDK specialization, e.g. `T[](...)<::example::Light,2>`, distinct from the local authored sub-identifier. |
| `MdlDbModuleName` | `mdl:mdl_db_module_name` | Exact Neuray database name of the loaded module, e.g. `mdl::nvidia::core_definitions`. |
| `MdlDbDefinitionName` | `mdl:mdl_db_definition_name` | Exact Neuray database name of the definition, e.g. `mdl::nvidia::core_definitions::diffuse(color,float,float3)`. |
| `IsEmbeddedModule` | `mdl:is_embedded_module` | String `"true"` when the loaded SDK module has no backing source filename, otherwise `"false"`. Remains `"true"` when an optional dummy file supplies the resolved Sdr URIs. See [embedded modules](ar-resolver.md#embedded-sdk-modules-and-dummy-files). |
| `SubIdentifier` | `mdl:subIdentifier` | USD source-asset sub-identifier, following the [sub-identifier rules](sub-identifiers.md). |
| `DefinitionType` | `mdl:definitionType` | `material` or `function`. |
| `Hidden` | `mdl:hidden` | String `"true"` when the definition has `::anno::hidden()`, otherwise `"false"`; see [node visibility](#node-visibility). |

For [array constructors](sub-identifiers.md#builtin-array-constructors), the
module identifier, module DB name, embedded-module flag and resolved node URIs
describe the element type's defining module. The definition DB name remains
`mdl::T[](...)` in the builtin module. Use the local `SubIdentifier` for USD
authoring and the resolved `MdlNameWithSignature` for SDK specialization.

The database names can be passed directly to `ITransaction::access<IModule>()`
and `ITransaction::access<IFunction_definition>()` on the same Neuray instance
after registration. Check the returned handles: these names describe loaded
database elements, and are not a replacement for module loading or persistent
USD asset references. Python exposes the strings but does not expose SDK
transactions.

For reusable material libraries, author `ModuleUsdIdentifier` as the shader's
source asset and `SubIdentifier` as its sub-identifier. See
[authoring module source assets](sub-identifiers.md#authoring-module-source-assets)
for library portability, layer-relative references, and resolver behavior.

### Node visibility

Every valid MDL node exposes `UsdMdl.NodeMetadata.Hidden`. This UI hint is
independent of validity: hidden functions and materials remain available for
lookup, registration, and use in graphs. Generated struct-field accessors
may also carry the annotation. The original annotation remains in
`mdl.annotation` metadata.

An editor can omit hidden definitions from its normal node picker:

```python
visible_nodes = [
    node for node in nodes
    if node.GetMetadata().get(UsdMdl.NodeMetadata.Hidden, "false") != "true"
]
```

Treat an absent key as false when consuming metadata from older plugin versions.
`mdl:hidden` is a plugin metadata key; Sdr has no standard whole-node visibility
field. An application decides whether to author corresponding USD scene-object
UI hints. Property `shownIf` expressions are a separate mechanism.

`IsValid()` indicates successful parsing of the Sdr definition. It does not
validate a connected material graph or guarantee that it will compile or render.
Callers using general Sdr lookup APIs should still check for a missing or invalid
node before reading its interface.

### Property UI hints

MDL's `anno::enable_if` condition is preserved as the original string under
the existing `enable_if` key in `SdrShaderProperty.GetHints()`:

```python
condition = node.GetShaderInput("shadow_intensity").GetHints().get("enable_if")
# For anno::enable_if("enable_shadows == true"): "enable_shadows == true"
```

The plugin does not parse, evaluate or validate this string as a USD expression.
It does not copy it into `shownIf`: MDL conditions can contain function calls
and enum symbols that USD expressions do not interpret equivalently, and
enabling a control is distinct from controlling its visibility. The consuming
application decides how to handle the MDL condition. The MDL SDK provides
`mi::neuraylib::IMdl_evaluator_api::is_function_parameter_enabled()` to evaluate
whether a parameter is enabled for a given MDL function call. An absent annotation
produces no `enable_if` hint. The MDL SDK still processes annotations as part
of normal module compilation.

## Limitations

See [MDL types and defaults](type-mapping.md) for the Sdr-to-Sdf type
mapping and the limits on struct values and default expressions. Struct
constructor nodes can express graph overrides; they do not provide an Sdr
property default.

For accepted lookup spellings and temporary compatibility settings, see
[sub-identifiers](sub-identifiers.md). The [legacy MDL schema](sub-identifiers.md#unsupported-legacy-mdl-schema)
must be migrated to source-asset references by the application.

For unsupported resource formats, containers and invalid-resource ambiguity,
see [resolver limitations](ar-resolver.md#limitations).

## C++ entry point

The [native integration guide](native-integration.md#c-api) covers the equivalent
registration calls, MDL port connection helper, host-owned Neuray attachment
and SDK interface ownership. Python exposes registration, port connection
checks and shutdown, but no Neuray pointers.

## Why not `pxr.UsdMdl`?

OpenUSD's `pxr` is a regular Python package. A separate `pxr/UsdMdl` directory
on `PYTHONPATH` would be ignored unless installed into OpenUSD's own package.
The top-level `UsdMdl` package allows installation beside read-only or shared
OpenUSD distributions. It still uses OpenUSD's standard `Tf` wrapping machinery.
