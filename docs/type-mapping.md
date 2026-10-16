<!-- SPDX-License-Identifier: Apache-2.0 -->

# MDL types and defaults

An Sdr property describes both an MDL parameter and a possible USD attribute.
These are different type systems. The plugin retains the MDL type through
metadata and converts representable constant defaults to USD/Gf/Vt values.

## Reading property types

- `GetType()` returns the Sdr property type token.
- `GetTypeAsSdfType()` asks OpenUSD for the USD attribute type. Check the
  returned type indicator's `HasSdfType()` before using it in C++.
- `GetDefaultValue()` returns the plugin's converted default, when available.
  An empty value does not by itself mean that the MDL parameter has no default.
- `mdl.type` records the original MDL type family or shape, such as `float3`,
  `float2x3`, `texture_2d`, or `struct`. Arrays use `[]` together with
  `mdl.arrayElementType`. `renderType` starts with the same value but can be
  overridden by annotations or USD shader definitions. Named types also use
  `mdl.symbol`; `mdl.modifier` records `uniform` or `varying` when present.

For representable value types, the plugin supplies OpenUSD's standard
`sdrUsdDefinitionType` property metadata. `GetTypeAsSdfType()` therefore
returns a valid type indicator with the converter's exact Sdf type, including
precision, color roles, and array containers. `GetDefaultValueAsSdfType()` uses
that type when presenting the default for USD authoring.

This explicit declaration matters because Sdr's type vocabulary does not
recognize every Sdf spelling. For example, the existing Sdr token `color3f`
does not itself select the USD color type in all supported OpenUSD versions.
The metadata specifies `color3f` without changing `GetType()` or the raw default.

Scalar opaque struct and distribution ports retain their existing treatment. A
successful token lookup for a struct port is not a serializable struct value;
a fallback token with `HasSdfType() == false` is not a valid value-type mapping.
The metadata does not serialize struct fields or evaluate arbitrary expressions.
Struct arrays have explicit `token[]` storage, with an empty constant as the
only representable struct-array value; see [struct-array ports](#struct-array-ports).

## Value representations

The table describes the USD storage exposed for representable values and the
separate opaque port representations. Struct and distribution placeholders
do not provide literal USD defaults.

`C` and `R` below are matrix dimensions from 2 through 4; `N` is an array length.

| MDL type | USD value representation | MDL information to retain |
| --- | --- | --- |
| `bool`, `int`, `float`, `double`, `string` | Matching scalar type | Uniform/varying modifier |
| `color` | `color3f` / `GfVec3f` | RGB color role; MDL `color` has three components |
| Enum | `int` | Qualified enum symbol and named integer options |
| `int2`, `int3`, `int4` | `int2`, `int3`, `int4` / Gf integer vectors | Vector shape |
| `float2`, `float3`, `float4` | Matching Gf float vectors | Vector shape |
| `double2`, `double3`, `double4` | Matching Gf double vectors | Vector shape and precision |
| `bool2`, `bool3`, `bool4` | `bool[]` with 2, 3, or 4 components | Original vector shape |
| `double2x2`, `double3x3`, `double4x4` | `matrix2d`, `matrix3d`, `matrix4d` | Original MDL matrix type |
| Rectangular `doubleCxR` | Flat `double[]`, `C * R` components | Both dimensions |
| All `floatCxR` | Flat `float[]`, `C * R` components | Both dimensions and float precision |
| Texture, light profile, BSDF measurement | `asset` / `SdfAssetPath` | Resource kind; texture shape, gamma and selector where applicable |
| Arrays of representable scalars, enums, colors, vectors, square double matrices, or resources | Corresponding USD array type | MDL element type and fixed/deferred length |
| Arrays of boolean vectors or flattened matrices | One flat component array | Element shape and MDL element count |
| User struct | Opaque Sdr `struct` port, token placeholder | Qualified MDL struct symbol |
| Built-in material structs | Sdr `terminal` port, token placeholder | Built-in struct kind |
| Array of structs | Sdr `struct` with array shape, Sdf `token[]`; only empty constants have literal values | MDL element identity and array information |
| `bsdf`, `hair_bsdf`, `edf`, `vdf` | Opaque token placeholder | Distribution kind; no scalar USD value |

The storage type alone cannot distinguish all MDL types: `bool[3]` and
`bool3` both use `bool[]`; `float2x3` and `float3x2` both use six floats.
Consumers must retain the MDL metadata. For an array of two `bool3` values,
the USD array contains six booleans and `GetArraySize()` is six. Deferred-size
arrays use the dynamic-array flag and `mdl.arrayDeferredSizeSymbol` rather than
a fixed count. Their actual default, if present, has its own length.

For example, the builtin enum `intensity_mode` and the regular enum
`::tex::gamma_mode` both use Sdf `int` storage and `renderType = "enum"`:

| Enum | `mdl.symbol` | Named integer options |
| --- | --- | --- |
| Builtin `intensity_mode` | `::intensity_mode` | `intensity_radiant_exitance = 0`, `intensity_power = 1` |
| Regular `::tex::gamma_mode` | `::tex::gamma_mode` | `gamma_default = 0`, `gamma_linear = 1`, `gamma_srgb = 2` |

A constant `intensity_radiant_exitance` or `tex::gamma_default` default remains
integer zero. The symbol distinguishes the enums even when their stored values
match. Follow the [signature spelling rules](sub-identifiers.md#what-to-author)
when using enum types in sub-identifiers.

For example, an MDL `float[5]` has an Sdf `float[]` type, a
`VtArray<float>` default when representable, and `GetArraySize() == 5`.
The Sdr property also carries:

~~~ini
renderType = "[]"
mdl.type = "[]"
mdl.arrayElementType = "float"
mdl.arrayIsImmediateSized = "true"
mdl.arraySize = "5"
sdrUsdDefinitionType = "float[]"
~~~

The `"[]"` render-type marker and `mdl.arrayElementType` are this plugin's
MDL metadata convention. The [OpenUSD Sdr documentation](https://openusd.org/release/api/_sdr__shader_property.html)
describes arrays through the property type, `arraySize` and `isDynamicArray`;
it does not prescribe an array `renderType` notation. `GetArraySize()` and
`IsDynamicArray()` expose the standard size fields. For MDL array identity,
use the original `mdl.type = "[]"`; OpenUSD's `IsArray()` reports false for a fixed
zero-length array because both its size and dynamic flag are zero/false.
A valid Sdf type's `IsArray()` still identifies its USD array storage.
The marker does not control Sdf type lookup; `sdrUsdDefinitionType` provides
the explicit `float[]` mapping.

The USD `float[]` type alone does not enforce MDL's fixed length. Consumers
must validate authored overrides against the MDL parameter's size requirement.
Creating a USD input from the returned Sdf type and default does not
automatically copy the Sdr metadata or author a length constraint.

Every MDL array property exposes `mdl.arrayIsImmediateSized` as `"true"` or
`"false"`. Immediate-sized declarations also expose `mdl.arraySize`, the decimal
**MDL element count**, including zero. Deferred declarations omit that count
and expose `mdl.arrayDeferredSizeSymbol`. These describe the declaration, not
the length of a default value or an attached upstream value. A `bool3[2]` has
`mdl.arraySize = "2"` even though `GetArraySize()` counts six stored booleans.

Flattened matrix components follow MDL column order: `float2x3` contains two
columns of three components, stored consecutively. Matrix arrays concatenate
those per-matrix sequences. Square double matrices store that MDL sequence as
Gf rows under the current mapping. Converting the container alone does not
establish equivalent MDL and Gf transform conventions.

For example, this MDL matrix has columns `(1, 2, 3)` and `(4, 5, 6)`:

```mdl
float2x3(float3(1.0, 2.0, 3.0), float3(4.0, 5.0, 6.0))
```

Its converted default is a flat float array: `[1, 2, 3, 4, 5, 6]`.
An array containing two such matrices has 12 stored components; each group
of six is one MDL matrix.

Resources have additional [resolution and format limits](ar-resolver.md).
A declared `asset` type or an empty asset default does not establish that the
resource was found and decoded.

## MDL annotations and Sdr metadata

See [node and property metadata](metadata.md) for annotation scope, direct Sdr
fields using the explicit `SDR__` prefix, MDL built-in mappings, conflict
rejection, precedence and UI hints. Supported USD type conversions are described
below.

## Explicit USD types and symbolic enums

A property annotation `SDR__sdrUsdDefinitionType("normal3f")` selects the USD value
representation while preserving the declared MDL type. Compatible float3
representations use canonical Sdr role types: `color3f` becomes `Color`,
`normal3f` becomes `Normal`, `point3f` becomes `Point`, and `vector3f` becomes
`Vector`. Their constants remain `GfVec3f` values. OpenUSD's generic
`CanConnectTo()` accepts connections between these float3 roles and rejects
float2 outputs; consumers still need the MDL checks described below.

The requested name must resolve through `SdfSchema` and preserve the native
value's storage and array shape. An unknown name, a float2-to-normal3f request,
a scalar-to-array request, or another incompatible conversion emits a diagnostic
and rejects the node. An override cannot manufacture a default for a required
parameter or an expression that depends on parameters or shading state.
`mdl.hasDefault` and the original expression metadata retain that distinction.
Annotations on exposed struct fields apply to those fields independently.
Return annotations apply to the whole-value output and do not propagate to
unrolled fields.

MDL enums normally retain integer USD defaults and name-to-integer options.
A `SDR__sdrUsdDefinitionType("token")` annotation on an enum instead creates an Sdr
`String` property with a symbolic string default, an Sdf `token` type, and
name-to-name options. The SDK enum member's name supplies the default, avoiding
reconstruction from sparse integer codes. The SDK can canonicalize a compiled
default with duplicate codes to the first member with that code; all declared
member names remain distinct options. A string parameter can also use the
`token` representation; empty string defaults remain available. Symbolic
token-array conversion is rejected because the supported OpenUSD version does
not provide the required string-array-to-token-array default conversion.

## Interfaces authored as USD shader definitions

Use `UsdMdl.AddShaderDefinitionsToRegistry(definitionsAsset, implementationAsset)`
to register an MDL implementation with an interface authored in a standard USD
shader-definition asset. The second `Sdf.AssetPath` is optional; when supplied,
it replaces the MDL source asset on every Shader in the definition file.
Each Shader authors an `info:id`, `info:implementationSource = "sourceAsset"`,
`info:mdl:sourceAsset`, and an exact `info:mdl:sourceAsset:subIdentifier`.
For an overloaded function, include its parameter signature to identify one
SDK definition. Invalid or ambiguous bindings emit diagnostics and are omitted.
Use valid USD namespaced identifiers that uniquely identify the interface:
this reserves the generated native MDL identifier format for native nodes.
Registered nodes remain immutable, and native MDL nodes keep their separate
identifiers and interfaces.

The authored USD property name is the exposed port name. Standard Sdr
`implementationName` metadata binds it to a native MDL parameter or output:

```usda
float2 inputs:in (
    sdrMetadata = { string implementationName = "st" }
)
float2 outputs:result (
    sdrMetadata = { string implementationName = "out" }
)
```

This permits a USD input named `in` to bind to an MDL input named `st`, since
`in` is reserved in MDL. An authored USD type selects the same compatible value
conversion described above. An authored USD default takes precedence; when
absent, the native constant default remains available. Symbolic enum token
values must be converted back to the declared SDK enum when constructing the
MDL call; the standard Sdr interface does not change that enum's native codes.

Texture channel outputs such as `r`, `g`, `b`, `a`, and `rgb` bind to real SDK
struct fields exposed by `::anno::node_output_port_default`. Each field retains
its own annotations. Material terminal names `surface` and `displacement` can
both use `implementationName = "out"`, with `renderType = "terminal surface"`
and `"terminal displacement"` respectively. These aliases identify the whole
MDL material. The renderer selects its surface or `geometry.displacement`
expression; an alias does not extract an MDL struct field named `displacement`.

## Client-side MDL connection checks

Use `UsdMdl.CanConnectTo(property, other)` in Python or
`pxr::UsdMdlCanConnectTo(property, other)` in C++ for a local structural check
of two Sdr MDL ports. [The Python API](python-api.md#usdmdlcanconnecttoproperty-other---bool)
shows an example. The helper reads original native `mdl.type` metadata,
which remains available when an annotation or USD shader definition overrides
`renderType`. Properties without a recognized native type are rejected;
there is no fallback to `renderType` or the USD storage type.

`SdrShaderProperty::CanConnectTo()` is a generic Sdr check. In OpenUSD 26.05,
it accepts `float2x3` to `float3x2` and distinct struct types with matching
storage, while rejecting a fixed float array connected to a deferred-size
float-array input. The plugin cannot override this non-virtual method.

The local helper requires one output and one connectable input; either
argument order is accepted. Null properties are rejected. It compares exact
MDL families and shapes without implicit conversions. For enums and structs,
including array elements, the full nonempty qualified `mdl.symbol` must match.
Ordinary type aliases can also carry `mdl.symbol`; their alias names do not
make otherwise identical types incompatible.

| Information | Where the helper reads it |
| --- | --- |
| Original MDL family, vector or matrix shape | `mdl.type`, e.g. `float2x3` means two columns and three rows |
| MDL array identity | `mdl.type = "[]"`, including fixed zero-length arrays |
| Array element family and shape | `mdl.arrayElementType`, e.g. `float2x3` |
| Named enum or struct identity, including array elements | Full qualified `mdl.symbol` |
| Immediate versus deferred declaration | `mdl.arrayIsImmediateSized` (`"true"` / `"false"`) |
| Fixed array's MDL element count | `mdl.arraySize`, including zero |
| Deferred array declaration | Nonempty `mdl.arrayDeferredSizeSymbol` |
| Uniform/varying qualification | `mdl.modifier` when available |

Fixed array inputs and outputs must have equal MDL element counts. A fixed
output can supply a deferred-size input. A deferred-size output cannot supply
a fixed input because its length is unknown. Two deferred-size ports with
matching element types are accepted without comparing their size symbols.
A symbol such as `N` belongs to one function call; identical spellings on
unrelated calls do not establish a shared length. The caller must bind lengths
and check all inputs and returns using the same size symbol across that call.
Missing required metadata or malformed type, array or modifier metadata is
rejected.

For example:

| MDL port | `mdl.type` | `mdl.arrayElementType` | `mdl.arraySize` | `GetArraySize()` |
| --- | --- | --- | --- | --- |
| `float2x3` | `float2x3` | absent | absent | 6 |
| `float2x3[2]` | `[]` | `float2x3` | 2 | 12 |
| `float3x2[2]` | `[]` | `float3x2` | 2 | 12 |
| `double3x3[2]` | `[]` | `double3x3` | 2 | 2 |
| `float2x3[N]` | `[]` | `float2x3` | absent | 0 |
| `float[0]` | `[]` | `float` | 0 | 0 |

`GetArraySize()` counts stored elements/components and cannot determine MDL
shape or length by itself. Flattened float matrices and rectangular double
matrices contribute `C * R` components per MDL element; boolean vectors
contribute their width. Square double matrices, numeric vectors and colors
each occupy one USD array element. Defaults do not define a port's declared
length; they may be absent, and a deferred default has its own length.

The helper rejects an explicitly varying output supplying a uniform input.
An absent modifier does not prove that an expression is uniform: unqualified
results can depend on their arguments and graph. With MDL SDK 2026.0.0,
explicitly qualified array parameters and returns expose no modifier through
`IType::get_all_type_modifiers()`, including the element type. Function-level
`mdl.modifier` is not a substitute for a parameter's qualification.

A successful local check does not establish valid USD topology, compatible
authored USD port types, graph-wide length bindings or uniformity, legal MDL
conversions, or renderer evaluation. Use resolved Sdr declarations rather than
stale metadata copies authored on USD ports. When constructing MDL calls,
validate them with the SDK definition identified by
`mdl:mdl_db_definition_name`.

## Texture gamma and color spaces

Texture metadata retains the SDK's gamma and selector in `mdl.textureGamma`
and `mdl.textureSelector`. The plugin does not convert gamma into a named USD
color space or automatically author `colorSpace` metadata. A direct
`SDR__colorspace` annotation can identify a separate colorspace parameter;
it does not derive a color space from a texture's gamma.

Named color-space mappings, including the meaning of automatic selection, are
deferred pending agreement on MDL/USD color-space behavior. Consumers must not
interpret the absence of a named space as an explicit linear or sRGB choice.

## Resource-array defaults

Resource arrays use Sdf `asset[]`. Each converted element retains its authored
MDL path and its Ar-resolved identifier. Texture arrays also expose ordered
`mdl.textureGamma` and `mdl.textureSelector` metadata lists; element `i` in each
list belongs to asset `i`. These metadata lists are separate from the USD
asset-array value and are not copied automatically when authoring that value.

This conversion requires an SDK constant array. An array constructor that
references other parameters has no Sdr default, even when those parameters
have constant texture defaults. Their scalar defaults remain available; the
plugin does not substitute them into the array expression.

For example, consider these MDL parameter declarations:

```mdl
uniform texture_2d first = texture_2d("/resources/one.png"),
uniform texture_2d second = texture_2d("/resources/two.png"),
uniform texture_2d[2] textures = texture_2d[](first, second)
```

`first` and `second` have asset defaults. Calling
`node.GetShaderInput("textures").GetDefaultValue()` returns `None` in Python
because the array default references those parameters. Authoring a different
`first` argument must still affect the MDL array default.

[MDL SDK 2026.0.2](https://github.com/NVIDIA/MDL-SDK/releases/tag/2026.0.2)
fixes resource enumeration when textures occur only inside a literal array.
Literal light-profile and cube-texture arrays also retain their resources on
this SDK.

Older supported SDKs return empty resources for these array-only defaults.
With 2026.0.0, referencing the same textures as scalar defaults in the module
allows the literal array to convert successfully. An empty converted element
is not proof that the MDL author specified an empty resource.

## Struct port metadata

For a user-defined MDL struct, `GetType()` returns Sdr `struct` and
`GetTypeAsSdfType()` returns an Sdf `token` placeholder. The current plugin
stores the category and the qualified MDL type name separately. For example,
a type named `::materialx::core::color4` is described by:

~~~ini
renderType = "struct"
mdl.type = "struct"
mdl.symbol = "::materialx::core::color4"
mdl.structType = "user"
~~~

Consumers should use the original `mdl.type` together with `mdl.symbol` to
identify the MDL struct type. USD does not resolve that symbol into an MDL definition.

The [OpenUSD Sdr documentation](https://openusd.org/release/api/_sdr__shader_property.html#special-types)
recommends a combined notation that includes the struct name in `renderType`:

~~~ini
renderType = "struct ::materialx::core::color4"
~~~

The plugin currently retains the separate fields shown above. Whether to adopt
the combined notation, and how to preserve compatibility for consumers of the
existing metadata, is deferred to a future specification decision. Neither
notation defines a serialization of struct values or adds literal defaults.

## Struct-array ports

A struct array has Sdr type `struct`, `mdl.type = "[]"` and initially `renderType = "[]"`,
`mdl.arrayElementType = "struct"`, the qualified `mdl.symbol`, and explicit
`sdrUsdDefinitionType = "token[]"`. Its scalar element ports use `token`.
Matching token storage alone does not make different struct types compatible.
Use [builtin array constructors](sub-identifiers.md#builtin-array-constructors)
to connect individual structs into a nonempty array, or an ordinary typed MDL
helper returning that array type.

An SDK constant empty struct array converts to an empty `VtTokenArray`.
Nonempty struct constants remain unavailable (`None` in Python); strings in a
nonempty `token[]` are not a serialization of struct fields. Call defaults and
missing defaults also remain unavailable as literal values and retain their
distinct default metadata.

A parameter with a deferred-sized array type, such as `Light[<N>] lights`,
declares a size symbol `N` whose value is determined by the supplied argument.
In contrast, `Light[2] lights` declares exactly two elements. An empty default
supplies zero elements but does not change the parameter's deferred-sized
declaration: `mdl.arrayIsImmediateSized` remains `"false"`, rather than becoming
an immediate-sized declaration of length zero. Here, "deferred" describes the
array's size, not delayed evaluation.

For USD authoring, an explicitly authored empty array requests a zero-element
argument; an unauthored input preserves the MDL default. The consumer's MDL
definition supplies the empty array's element type. Applying these arguments
is the renderer integration's responsibility; the Sdr plugin does not implement
USD-to-MDL argument application or prove a renderer handles those cases.

## Structs and default expressions

USD attributes do not provide a general value type for an arbitrary MDL
struct. The plugin exposes a struct port's identity so that a graph can refer
to it, but does not serialize the struct's fields as one attribute default.
The token placeholder is not an encoding of the struct value. A type that
happens to contain color and alpha fields is still a struct; it is not
automatically converted to a four-component color.

To override a struct input in a USD shader graph, create a shader node for the
constructor function of that particular MDL struct type, author its field
inputs, and connect its struct output to the consuming shader's input. The
MDL integration consuming the graph must preserve the qualified struct type;
matching generic token storage is not sufficient type checking.

That connection is an authored graph override. It cannot be stored as the
default value of an Sdr property: a property default is a value, not a nested
shader graph. The plugin also supports annotation-driven exposure of struct
return fields through `anno::node_output_port_default`; those output ports do
not supply literal defaults for struct inputs.

The plugin converts only representable values supplied by the SDK as
`IExpression_constant`. Constructors, including nested constructors, work when
the SDK has already folded them to a constant. For example,
`color(float3(0.1, 0.2, 0.3))` can provide a color default. Source syntax alone
does not determine whether the SDK supplies a constant.

Calls, parameter references, and state-dependent expressions have no converted
default. The plugin does not parse or evaluate them, follow referenced
parameters, substitute zeros, or partially populate arrays. For example,
`color(state::normal())` and an array constructed from other parameters retain
their typed Sdr inputs, but both `GetDefaultValue()` and
`GetDefaultValueAsSdfType()` return an empty `VtValue` (`None` in Python).
An unsupported expression does not invalidate the node or raise a Python
exception. A missing MDL default also returns an empty value; an actual
constant zero remains zero.

Every input property has `mdl.hasDefault`: the string `"true"` when the SDK
definition supplies a default expression, or `"false"` when it does not.
This flag is independent of value conversion, so an unavailable Sdr value
with `mdl.hasDefault = "true"` still has an MDL default. Output properties
do not have this entry. In Python, use `UsdMdl.Metadata.HasDefault` as the key.

`mdl.expressionKind` describes the SDK's root default expression:

| SDK expression kind | `mdl.expressionKind` | `mdl.expressionValue` |
| --- | --- | --- |
| `EK_CONSTANT` | `Constant` | Absent; the converted value is available through `GetDefaultValue()` |
| `EK_CALL` | `Call` | Function-definition database name, e.g. `mdl::state::texture_tangent_v(int)` |
| `EK_PARAMETER` | `Parameter` | Zero-based parameter index |

`Constant` is recorded even when the value cannot be represented in Sdr, such
as a struct constant; its default value remains unavailable. Missing defaults
have neither expression metadata entry. Call arguments and
nested expression metadata are not expanded. These fields do not encode a
complete, reconstructible expression graph.

For example, this MDL definition exercises missing, constant, parameter, and
call defaults:

```mdl
export float default_boundaries(
    float required,
    float zero = 0.0,
    float folded = 2.0 * 3.0,
    float referenced = folded,
    float computed = folded * 2.0)
{
    return required + zero + folded + referenced + computed;
}
```

Given its Sdr `node`, inspect default presence, value, and expression kind together:

```python
import UsdMdl

for name in ("required", "zero", "folded", "referenced", "computed"):
    prop = node.GetShaderInput(name)
    metadata = prop.GetMetadata()
    has_default = metadata[UsdMdl.Metadata.HasDefault] == "true"
    kind = metadata.get(UsdMdl.Metadata.ExpressionKind)
    print(name, has_default, prop.GetDefaultValue(), kind)
# required False None None
# zero True 0.0 Constant
# folded True 6.0 Constant
# referenced True None Parameter
# computed True None Call
```

`referenced` also has `mdl.expressionValue = "2"`, the index of `folded`.
Its default follows the supplied `folded` argument rather than always being
`6.0`. For an unrepresentable constant such as a struct default
`ConstantRecord(7)`, `has_default` is `True`, the value is `None`, and the kind
is `Constant`: the plugin cannot store that constant as an Sdr default.

Applications that need the original defaults should inspect the SDK definition
in a transaction on the shared Neuray instance, using
[mdl:mdl_db_definition_name](python-api.md#mdl-node-metadata). When building an
MDL call, leave an input unspecified to retain the definition's MDL default,
rather than synthesizing an override from an unsupported Sdr default.
