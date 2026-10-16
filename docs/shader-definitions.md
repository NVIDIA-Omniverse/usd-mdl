# USD shader definitions backed by MDL

Ordinary MDL modules use `UsdMdl.AddModuleToRegistry()` or
`UsdMdlAddModuleToRegistry()`. Their interfaces follow the compiled MDL
parameters and return type. Explicit `SDR__` metadata annotations can provide UI
metadata and supported USD type conversions; they do not rename MDL parameters
or add a different output layout. See [metadata](metadata.md) for annotation
mappings and [types and defaults](type-mapping.md) for value conversions.

Use `UsdMdl.AddShaderDefinitionsToRegistry()` or
`UsdMdlAddShaderDefinitionsToRegistry()` when a standard USD interface has
its own port names, USD types, defaults, and outputs. The interface is authored
as ordinary `UsdShade` Shader definitions in a USD asset, with an MDL
implementation reference on each concrete Shader. This path uses no application
callback and requires no custom USD annotation module in the MDL source.

## Definition assets

Each concrete Shader supplies:

```usda
uniform token info:id = "UsdTransform2d"
uniform token info:implementationSource = "sourceAsset"
uniform asset info:mdl:sourceAsset = @UsdPreviewSurface.mdl@
uniform token info:mdl:sourceAsset:subIdentifier = "UsdTransform2d"
```

The `info:id` must be a valid USD namespaced identifier, such as
`UsdTransform2d`, `Studio:Transform2d`, or `StudioTransform2d_1_0`.
An omitted identifier defaults to the Shader prim name. Identifiers cannot
use the internal encoded MDL discovery form.

The source asset identifies the MDL module. The sub-identifier selects its
function or material; use an exact signature for an overloaded definition.
For an [array constructor interface](#array-constructor-interfaces), it instead
identifies the element type's defining module and a concrete specialization.
Configure the module and its imports under the application's
[MDL search roots](environment-variables.md#mdl-search-paths) before the first
MDL/Sdr operation. Supplying an absolute implementation asset does not replace
this configuration requirement.

The USD asset provides exposed names, typed defaults, options, documentation,
and connectability. Inheritance can share property metadata across definitions;
abstract class prims are not registered as nodes. `sdrMetadata.implementationName`
selects the original MDL parameter or output when its USD name differs.
The `SDR__` prefix applies only to MDL annotation names; USD `sdrMetadata` keys
retain their standard spellings:

```usda
float2 inputs:in = (0, 0) (
    sdrMetadata = {
        string implementationName = "st"
    }
)
float2 outputs:result (
    sdrMetadata = {
        string implementationName = "out"
    }
)
```

Authored input defaults take precedence over recoverable MDL defaults.
Asset defaults retain their typed asset path and resolved identity. For ordinary
functions, when a USD input has no authored default, registration converts its
original MDL default
into the requested USD type, including numeric or symbolic enum representations.
Nonconstant MDL defaults remain available through the original SDK definition.

The authored property types must belong to OpenUSD's ShaderDefUtils subset:
bool, int and int vectors, float and float vectors, color3f/color4f,
point3f/vector3f/normal3f, matrix4d, string, token, or asset, and compatible arrays.
Registration also checks each type against its actual MDL binding; unsupported
value conversions produce diagnostics and omit the node. Struct array
constructors support token-array connection storage, as described below.

The referenced MDL definition must support that interface. Registration checks
bindings and supported value conversions; invalid definitions produce diagnostics
and are omitted. Registry-owned nodes are immutable: an identifier and source
type must identify one definition. Register the desired interface before asking
Sdr for that identifier. Re-registering an identical interface returns its cached
node; changing its binding, properties, defaults, options, or metadata emits a
diagnostic and leaves the existing node intact. Use a new identifier for a new
interface.

## Array constructor interfaces

A USD-authored interface can bind directly to a concrete builtin array
constructor. Author its `T[](...)<Type,N>` specialization explicitly; registration
does not infer the element type or count from USD ports. Use the same
[element-module ownership and local type-name rules](sub-identifiers.md#builtin-array-constructors)
as on-demand constructor lookup. The count must be positive; zero-size
constructor nodes remain unsupported.

For example, save this definition as `PairDefinitions.usda`:

```usda
#usda 1.0
def Shader "MakePair"
{
    uniform token info:id = "MakePair"
    uniform token info:implementationSource = "sourceAsset"
    uniform asset info:mdl:sourceAsset = @mdl-builtin.mdl@
    uniform token info:mdl:sourceAsset:subIdentifier = "T[](...)<float,2>"
    float inputs:first = 10 (
        sdrMetadata = {
            string implementationName = "value0"
        }
    )
    float inputs:second = 20 (
        sdrMetadata = {
            string implementationName = "value1"
        }
    )
    float[] outputs:result (
        sdrMetadata = {
            string implementationName = "out"
        }
    )
}
```

Register it through the existing shader-definition API:

```python
from pxr import Sdf
import UsdMdl

try:
    nodes = UsdMdl.AddShaderDefinitionsToRegistry(
        Sdf.AssetPath("PairDefinitions.usda"))
    node = next(node for node in nodes if node.GetIdentifier() == "MakePair")
    assert node.GetShaderInput("first").GetImplementationName() == "value0"
    assert node.GetShaderInput("second").GetImplementationName() == "value1"
    assert node.GetShaderOutput("result").GetImplementationName() == "out"
finally:
    UsdMdl.Shutdown()
```

`first` binds to array position zero and `second` to position one, regardless
of USD property order. Every concrete argument, `value0` through `valueN-1`,
must be exposed exactly once. Renamed ports use `sdrMetadata.implementationName`;
ports retaining their MDL names need no alias. Unknown, duplicate or missing
argument bindings, incompatible property types, and invalid specializations
produce diagnostics and omit that definition.

Constructor arguments have no MDL defaults. An exposed input without an authored
USD default remains required; registration does not supply a zero value.
Authored literal defaults use the supported USD-to-MDL type conversions described
above. Struct elements use `token` inputs and a `token[]` output as connection
placeholders, with native named-type metadata retained. They do not support
literal struct serialization. This connection storage does not enable general
token-array value conversion or symbolic enum arrays.

Registration preserves the local authored specialization and the resolved SDK
specialization as distinct [constructor identities](sub-identifiers.md#empty-arrays-and-renderer-integration).
The optional implementation-asset override must still identify the element type's
owning module. Repeated registration uses the same immutable interface cache
rules as ordinary shader definitions. The renderer creates the specialized SDK
call using the concrete argument bindings; registering `MakePair` alone does
not evaluate the array.

## Preview Surface resource

The plugin ships `UsdPreviewSurface.usda` in
`<prefix>/plugin/usd/usdMdl/resources`. The build generates it from the canonical
`usdShaders` definitions supplied by the OpenUSD SDK selected by CMake. It
preserves the SDK's identifiers, inheritance, port types, defaults, options,
connectability, and documentation, then adds the MDL source asset and backend
binding metadata. A version suffix on a canonical identifier remains part of
that identifier; its MDL binding uses the corresponding unversioned function.
The generator rejects unknown shader families and incompatible mapped ports
instead of silently producing an interface for an unsupported backend.

`UsdPreviewSurface.provenance.json` records the selected USD version, canonical
source path and SHA-256 hashes of the source, generator, generated asset,
license and attribution notice. The generated asset, provenance, OpenUSD license
and `OpenUSD-NOTICE.txt` are always installed. Source comments are retained in
the generated USDA header alongside its modification notice. See
[build configuration](build.md#generated-preview-surface-definitions) for SDK
resource selection and overrides.

The application supplies a compatible `UsdPreviewSurface.mdl` and its imports
in the configured [MDL roots](environment-variables.md#mdl-search-paths).
The package does not redistribute Kit's implementation files.

Automatic registration is opt-in. Set `USD_MDL_LOAD_USD_PREVIEW_SURFACE=1` before
the first Sdr operation to discover these MDL interfaces alongside the SDK's
GLSLFX entries. This switch is independent of `USD_MDL_DISCOVER_ON_STARTUP`:
it loads only the generated interface and its implementation module, without
scanning the native MDL catalog. With the default `false`, applications can
register the same asset explicitly through `AddShaderDefinitionsToRegistry`.
A missing implementation warns and omits the MDL interfaces; the registry's
other source types remain usable.

Use an exact source-type lookup or an explicit priority list when both MDL and
GLSLFX implement the same identifier:

```python
from pxr import Sdr

registry = Sdr.Registry()
mdl_surface = registry.GetShaderNodeByIdentifierAndType("UsdPreviewSurface", "mdl")
glslfx_surface = registry.GetShaderNodeByIdentifierAndType("UsdPreviewSurface", "glslfx")
```

For explicit registration:

```python
from pathlib import Path
from pxr import Plug, Sdf
import UsdMdl

plugin = Plug.Registry().GetPluginWithName("usdMdl")
definitions = Path(plugin.resourcePath) / "UsdPreviewSurface.usda"
try:
    nodes = UsdMdl.AddShaderDefinitionsToRegistry(
        Sdf.AssetPath(str(definitions)),
        Sdf.AssetPath("UsdPreviewSurface.mdl"),
    )
    for node in nodes:
        print(node.GetIdentifier(), node.GetImplementationName())
finally:
    UsdMdl.Shutdown()
```

The shipped bindings preserve the standard USD interface:

| USD property | MDL binding |
| --- | --- |
| `UsdTransform2d.inputs:in` | Parameter `st` |
| Primvar-reader and transform `outputs:result` | Whole return value `out` |
| `UsdUVTexture.outputs:r`, `g`, `b`, `a`, `rgb` | Corresponding return-struct fields |
| `UsdPreviewSurface.outputs:surface`, `displacement` | Whole material return value `out`, with the canonical surface/displacement terminal render types |

The two material terminals alias the compiled material. They do not extract a
first-level `displacement` struct field or evaluate `geometry.displacement`
themselves. The renderer compiles the material and selects the required material
expression through its normal terminal handling.

## Renderer binding

An Sdr node's `GetImplementationName()` identifies the original compiled MDL
function with its signature or the resolved array constructor specialization.
Each property's `GetImplementationName()` identifies
the original MDL parameter, return-struct field, or whole return `out`. Use these
names when constructing MDL calls and graph edges; the exposed USD names remain
the names authored in the scene. Original SDK module/definition identities remain
available in the node's [MDL metadata](python-api.md#mdl-node-metadata).

USD role types such as `normal3f`, `point3f`, and `vector3f` retain their authored
USD meaning while binding to the compatible MDL value type. The texture file
metadata identifies `sourceColorSpace` through Sdr's standard colorspace key.
The Preview Surface wrap and opacity inputs retain token defaults and allowed-token choices.
A renderer must translate such symbolic USD tokens into the original MDL enum;
passing a token directly as an integer SDK argument does not perform that
conversion.

Native consumers can use `<usdMdl/enumValue.h>`:

```c++
mi::base::Handle<mi::neuraylib::IValue_enum> value(
    pxr::UsdMdlCreateEnumValue(
        valueFactory, originalEnumType,
        pxr::VtValue(pxr::TfToken("presence"))));
```

`UsdMdlCreateEnumValue()` accepts `TfToken` or `std::string` member names and
native integer codes. It resolves symbolic member names exactly, including enums
with sparse codes or aliases. It returns null with a diagnostic for unsupported
values, unknown names/codes, or invalid factory/type inputs. The successful result
carries one retained SDK reference; adopt it into a `mi::base::Handle` or release
it. The factory and original enum type belong to the caller's live SDK
transaction. The renderer remains responsible for creating the argument
expression and for handling texture assets, color-space inputs, primvar lookup,
and material terminals. Python registration does not expose SDK value factories.

See the [native integration guide](native-integration.md) for SDK ownership and
shutdown, and [OpenUSD attribution](../THIRD_PARTY_NOTICES.md#openusd-shader-definitions)
for the resource's license.
