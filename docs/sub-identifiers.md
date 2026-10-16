<!-- SPDX-License-Identifier: Apache-2.0 -->

# MDL sub-identifiers

A USD shader references an MDL definition with two attributes:

```usda
uniform asset info:mdl:sourceAsset = @pkg/main.mdl@
uniform token info:mdl:sourceAsset:subIdentifier = "create_layer"
```

The source asset selects the module. For ordinary functions, the sub-identifier
selects an exported, non-removed definition in that module. Builtin
[array constructors](#builtin-array-constructors) instead use the element type's
defining module as the source asset. `::anno::hidden()` affects
[UI presentation](python-api.md#node-visibility), not eligibility or overload
selection. The currently supported setup requires
file-backed modules and their dependencies to be in configured
[MDL search paths](environment-variables.md#mdl-search-paths). Standard modules
[embedded in the SDK](ar-resolver.md#embedded-sdk-modules-and-dummy-files)
can be referenced without a physical source file.

## Authoring module source assets

Prefer MDL search paths for reusable material libraries. Author the module's
path within its search root, such as `acme/paint.mdl`, without a leading `/`
or `./`. This lets the library move independently of the scene: configure
the new search root without rewriting the USD references.

When building a shader from an Sdr node, use `mdl:moduleUsdIdentifier` for
`info:mdl:sourceAsset` and `mdl:subIdentifier` for
`info:mdl:sourceAsset:subIdentifier`. These describe the portable module and
definition identities. A resolved implementation location serves a different
purpose: it identifies the particular loaded module for access through Ar.
Copying that location verbatim into USD can tie the scene to one installation.

Use resolved Sdr URIs only to access the loaded module through Ar; they are
runtime locations. [Embedded modules](ar-resolver.md#embedded-sdk-modules-and-dummy-files)
retain portable authoring identifiers even when they have no physical URI.
See [node metadata](python-api.md#mdl-node-metadata) for the exact field meanings.

Reserve modules outside MDL search roots for content shipped with a particular
scene or asset. For that workflow, author a **strict relative path** such as
`./materials/paint.mdl` or `../materials/paint.mdl`, relative to the layer that
receives the authored opinion. This is the edit target layer, which need not
be the stage's root layer. Keep the module and layer's relative layout intact
when moving the asset. This is the recommended authoring policy for future or
experimental off-search-path use; that use remains outside the plugin's
currently supported scope.

For an off-search-path module, `mdl:moduleUsdIdentifier` is empty. The authoring
application must derive the layer-relative reference from the module's actual
location and the destination layer. Retain the `./` prefix for paths within
that layer's directory, and establish the destination of an anonymous layer
before computing the relative path. Imported library modules still need their
MDL search roots configured.

With USD's default filesystem resolver, the distinction is:

| Authored source asset | If the layer-relative file exists | If it is missing |
| --- | --- | --- |
| `materials/paint.mdl` (weak relative) | Select that file | Allow search-path lookup |
| `./materials/paint.mdl` (strict relative) | Select that file | Fail instead of selecting a library module with the same path |

MDL search roots and Ar search paths are separate configurations: the plugin
can find a module through MDL roots even when USD leaves the asset's resolved
path empty.

This follows USD's [layer anchoring behavior](https://openusd.org/release/api/layer_utils_8h.html).
[Custom Ar resolvers](https://openusd.org/release/api/ar_page_front.html) control
their own path interpretation. Verify equivalent strict-relative behavior
with the active resolver; do not apply filesystem-relative path calculations
to arbitrary resolver URIs.

### Default resolver limitations

The default resolver's layer-first behavior has two costs for library references:

- **Existence checks:** USD must check whether the layer-relative candidate
  exists before falling back to search paths. Filesystem checks can add I/O;
  caching can reduce repeated checks, but does not remove this policy.
- **No search-path-only spelling:** `materials/paint.mdl` cannot explicitly
  select the library module when a file with the same relative path exists
  beside the layer. This also applies to paths authored from
  `mdl:moduleUsdIdentifier`. Changing the MDL search-root order does not change
  USD's earlier layer-relative selection.

These costs motivate Omniverse's different MDL resolution policy, which gives
library modules priority and avoids layer anchoring for recognized library
references. See [Omniverse's MDL path resolution changes](https://docs.omniverse.nvidia.com/materials-and-rendering/latest/materials_release-notes/MDL_resolution_changes.html).
That policy depends on its resolver integration. This plugin follows the
application's active Ar resolver; providing a custom primary Ar resolver to
address these issues is outside its scope.

Users must therefore avoid accidentally overriding search-path modules with
layer-local files. For example, if a shader authors `@materials/paint.mdl@`,
placing another `materials/paint.mdl` beside its layer can change which module
it loads. Use distinct library package names and check scene content for such
collisions. Author `./` or `../` when a scene-local module is intended; those
prefixes make local intent explicit but do not provide a search-path-only form.

## What to author

Use the Sdr node's **`mdl:subIdentifier` metadata** when authoring a shader's
`info:mdl:sourceAsset:subIdentifier`. This is the canonical spelling:

| Definition | Canonical sub-identifier |
| --- | --- |
| Material | Simple name, e.g. `paint` |
| Function with no overloads | Simple name, e.g. `create_layer` |
| Overloaded function | Name and complete parameter-type list, e.g. `pick(float)` |
| Zero-parameter overload | Name with empty parentheses, e.g. `pick()` |
| Builtin array constructor | Concrete SDK specialization, e.g. `T[](...)<float3,2>`; see below |

The signature lists **parameter types**, not parameter names, values, or the
return type. A parameter with a default is still included.

For a function in `::pkg::main`, type names in canonical signatures are:

| Parameter type | Canonical spelling |
| --- | --- |
| Built-in, including builtin enums | `float`, `color`, `intensity_mode`, `float3`, `texture_2d`, etc. |
| Struct or enum defined in the same module | `Local`, `Mode` |
| Type from another module | `::pkg::another::Bar2`, `::tex::wrap_mode` |
| Fixed-size array | `int[3]`, `Local[2]` |
| Deferred-size array | SDK spelling, e.g. `float[N]` |

Cross-module names are absolute **MDL names**, not filesystem paths. They can
refer to modules in different search roots and remain unchanged when the
content is installed under a different filesystem root. This follows Kit's
unqualified spelling for local types and qualified spelling for external types.

Builtin types use unqualified names in signatures, including the builtin enum
`intensity_mode`. The SDK's builtin module does not count as an external module
for this rule. For example, a function in `::pkg::main` taking the builtin enum
`intensity_mode` and the regular enum `::tex::gamma_mode` uses the signature
`foo(intensity_mode,::tex::gamma_mode)`.

These rules describe USD sub-identifiers, not MDL source syntax. Property
metadata preserves type symbols such as `mdl.symbol = "::intensity_mode"` and
`mdl.symbol = "::tex::gamma_mode"`; those symbols are not necessarily the
spelling to use in a signature.

`mdl:moduleUsdIdentifier` is the module asset path, such as `pkg/main.mdl`.
`mdl:mdlNameWithSignature` is the full definition signature for inspection.
Neither replaces `mdl:subIdentifier` when authoring the sub-identifier.
The Sdr registry's identifier is a cache identifier, not an MDL sub-identifier.

## Builtin array constructors

Set `sourceAsset` to the module that defines the element type and use a local
type name in the constructor sub-identifier:

| `sourceAsset` | `subIdentifier` | Element type |
| --- | --- | --- |
| `mdl-builtin.mdl` | `T[](...)<float3,2>` | Builtin `float3` |
| `example.mdl` | `T[](...)<Light,2>` | `Light` defined by `example.mdl` |

This is an MDL-specific interpretation of the source asset: it supplies the
element-type module, while the constructor implementation comes from the SDK's
builtin module. Builtin types such as `float`, `color` and `float3` require
`mdl-builtin.mdl`. Named struct/enum types require their defining module;
importing the type into another module does not make that module its owner.
Qualified and package-relative element names are rejected.

The plugin loads the source module on demand, expands the local type name to
its SDK identity, and validates ownership. The ordinary
[module asset rules](#authoring-module-source-assets) apply, including configured
search roots and experimental off-search-path references. No additional type-module
attribute or application-side Sdr request adapter is needed. The count is a
positive decimal integer without leading zeros. Invalid types, malformed
spellings, zero counts and counts outside Sdr's integer size range are rejected;
unsupported port types do not produce partial nodes.

Each specialization exposes `value0` through `valueN-1` and one `out`. The
output is an immediate-sized array, as defined by the SDK array constructor;
each input has the element type and no default. The examples above expose
float3/float3[2] and Light/Light[2] respectively. Struct arrays use Sdr `struct` with array shape and
Sdf `token[]`; the tokens are connection placeholders, not serialized structs.

### Requesting nodes from an editor

The plugin creates concrete Sdr nodes **on demand**. It does not enumerate all
type/count combinations, and module registration omits the generic array
template. The editor obtains an element type and count, then requests:

```python
from pxr import Sdf, Sdr

node = Sdr.Registry().GetShaderNodeFromAsset(
    Sdf.AssetPath("mdl-builtin.mdl"), {}, "T[](...)<float3,2>", "mdl")
assert node and node.IsValid()
assert list(node.GetShaderInputNames()) == ["value0", "value1"]
```

Repeated requests reuse the registry identity; different modules, types or
counts have distinct identities. Explicit registration of an element module does not
change an existing specialization. Save the node's `mdl:moduleUsdIdentifier`
and `mdl:subIdentifier` in USD so a later process can resolve it without prior
registration. For an off-search-path module, `mdl:moduleUsdIdentifier` is empty;
derive a layer-relative asset reference as described under
[module assets](#authoring-module-source-assets). Resizing means requesting another
specialization and updating the authored graph; the plugin does not mutate a
cached Sdr definition.

To register a named USD interface with renamed ports, author the same concrete
specialization on a Shader definition and call
`UsdMdl.AddShaderDefinitionsToRegistry()`. Each concrete constructor argument
must have one USD input binding; element type and count are explicit rather than
inferred from the interface. See [array constructor interfaces](shader-definitions.md#array-constructor-interfaces)
for a complete definition asset and registration example.

### Authoring examples

A float3 array can be constructed with literal element inputs:

```usda
#usda 1.0
def Shader "Vectors"
{
    uniform token info:implementationSource = "sourceAsset"
    uniform asset info:mdl:sourceAsset = @mdl-builtin.mdl@
    uniform token info:mdl:sourceAsset:subIdentifier = "T[](...)<float3,2>"
    float3 inputs:value0 = (1, 0, 0)
    float3 inputs:value1 = (0, 1, 0)
    float3[] outputs:out (
        renderType = "[]"
        sdrMetadata = {
            string "mdl.arrayElementType" = "float3"
            string "mdl.arrayIsImmediateSized" = "true"
            string "mdl.arraySize" = "2"
            string sdrUsdDefinitionType = "float3[]"
        }
    )
}
```

For a module `example.mdl` containing this struct:

```mdl
mdl 1.8;
export struct Light { float intensity; color tint; };
```

construct the structs and connect their scalar outputs to the array inputs:

```usda
#usda 1.0
def Shader "LightA"
{
    uniform token info:implementationSource = "sourceAsset"
    uniform asset info:mdl:sourceAsset = @example.mdl@
    uniform token info:mdl:sourceAsset:subIdentifier = "Light(float,color)"
    float inputs:intensity = 0.3
    color3f inputs:tint = (1, 0, 0)
    token outputs:out
}
def Shader "LightB"
{
    uniform token info:implementationSource = "sourceAsset"
    uniform asset info:mdl:sourceAsset = @example.mdl@
    uniform token info:mdl:sourceAsset:subIdentifier = "Light(float,color)"
    float inputs:intensity = 0.5
    color3f inputs:tint = (0, 1, 0)
    token outputs:out
}
def Shader "Lights"
{
    uniform token info:implementationSource = "sourceAsset"
    uniform asset info:mdl:sourceAsset = @example.mdl@
    uniform token info:mdl:sourceAsset:subIdentifier = "T[](...)<Light,2>"
    token inputs:value0.connect = </LightA.outputs:out>
    token inputs:value1.connect = </LightB.outputs:out>
    token[] outputs:out (
        renderType = "[]"
        sdrMetadata = {
            string "mdl.arrayElementType" = "struct"
            string "mdl.symbol" = "::example::Light"
            string "mdl.arrayIsImmediateSized" = "true"
            string "mdl.arraySize" = "2"
            string sdrUsdDefinitionType = "token[]"
        }
    )
}
```

The metadata copies in these examples document the declarations. Editors read
accurate generated Sdr metadata for [connection checks](type-mapping.md#client-side-mdl-connection-checks);
existing helper graphs need not acquire these USD-authored copies.

### Empty arrays and renderer integration

Do not request `T[](...)<Type,0>`. Author a typed empty constant, for example
`token[] inputs:lights = []`, with the element type obtained from the resolved
consumer definition. An omitted input preserves its MDL default; explicitly
authoring `[]` overrides it. Zero-size constructor nodes remain a possible
future convenience feature. See [array defaults](type-mapping.md#struct-array-ports).

Sdr specialization is not a USD-to-MDL renderer implementation. An integration
must recognize the specialized node, retain type/count through graph handoff,
and create the SDK array call with correctly typed arguments. Instantiating the
generic definition with no arguments and attaching values afterwards is not
sufficient. The generated node distinguishes the following identities:

| Metadata | Example | Purpose |
| --- | --- | --- |
| `mdl:moduleUsdIdentifier` | `example.mdl` | Element-type module for USD authoring |
| `mdl:subIdentifier` | `T[](...)<Light,2>` | Authored specialization with a local type name |
| `mdl:mdlNameWithSignature` | `T[](...)<::example::Light,2>` | Resolved SDK specialization for renderer handoff |
| `mdl:mdl_db_module_name` | `mdl::example` | Loaded element-type module |
| `mdl:mdl_db_definition_name` | `mdl::T[](...)` | Builtin implementation definition |

The node's resolved URIs and `mdl:is_embedded_module` also describe the
element-type module. The builtin definition is not a definition in that module.
Use the SDK's `deserialize_function_name` with `mdl::` prefixed to
`mdl:mdlNameWithSignature` to recover concrete argument types in the resolving
SDK context. An integration using a separate SDK instance must load the element
module there and resolve the specialization in that context. Do not pass the
local authored name directly to SDK deserialization or save a machine-specific
resolved type identity as the authored sub-identifier.

Ordinary helper functions continue through ordinary definition lookup. Other
template-like builtins (indexing, length, conditional and casts) do not gain
specialization support from this array-constructor path. Renderer evaluation,
literal-array application and live graph editing require integration tests in
the consuming application; successful Sdr lookup alone does not establish them.

## Overriding resource parameters

Resources referenced by MDL modules **shall not be edited in place**, including
through SDK calls such as `reset_file()`. Those resources can be shared by
multiple definitions and uses. To change a texture parameter, supply an argument
for the particular function or material use.

In another MDL module, pass the replacement texture when calling the function
or material. For example, assuming `::acme::paint` exports a material `paint`
with a `texture_2d` parameter named `albedo`:

```mdl
mdl 1.8;
import ::acme::paint::*;

export material red_paint() = paint(
    albedo: texture_2d("./textures/red.png")
);
```

Here, `./textures/red.png` is relative to the module defining `red_paint`.

When working in USD, author an override on the corresponding shader input.
For an existing shader at `/Looks/Paint/Shader`, a stronger layer can contain:

```usda
#usda 1.0
over "Looks"
{
    over "Paint"
    {
        over "Shader"
        {
            asset inputs:albedo = @./textures/red.png@
        }
    }
}
```

The USD asset path is relative to the layer authoring this opinion. The override
changes the argument for that shader; the MDL definition and its resource
default remain unchanged. Use the parameter name and
[USD value type](type-mapping.md#value-representations) reported by Sdr.

## What lookup accepts

Lookup accepts canonical names and equivalent complete signatures. Even a
unique function or material can be requested with its full signature, so
existing content remains readable. Successful lookup still exposes the
canonical authoring spelling:

| Requested sub-identifier | MDL definition | Returned `mdl:subIdentifier` |
| --- | --- | --- |
| `create_layer` | Unique `create_layer(color,float)` | `create_layer` |
| `create_layer(color,float)` | Same unique function | `create_layer` |
| `paint(color,float)` | Material `paint(color,float)` | `paint` |
| `zero()` | Unique zero-parameter function | `zero` |
| `pick(Local)` | Overload taking `::pkg::main::Local` | `pick(Local)` |
| `pick(::pkg::main::Local)` | Same overload | `pick(Local)` |
| `pick(.::main::Local)` | Same overload | `pick(Local)` |
| `pick(.::another::Bar2)` | Overload taking `::pkg::another::Bar2` | `pick(::pkg::another::Bar2)` |
| `pick(..::shared::types::Thing)` | Overload taking `::shared::types::Thing` | `pick(::shared::types::Thing)` |

Explicitly relative type names use the **containing package** as their anchor:
for `::pkg::main`, `.::` starts in `::pkg`, and `..::` starts one package above.
There must still be a module and type after that prefix. Consequently,
`.::Local` is invalid; it is not an alias for a local type.

These relative forms are a sub-identifier convention using MDL's package
anchoring rules. They are not literal MDL source-level type expressions.
Matching them does not perform a second asset lookup or impose an additional
same-search-root/shadowing check. The loaded SDK definitions determine identity.

Whitespace around the name and parameter types is ignored, so
`create_layer( color, float )` also works. Canonical output omits that whitespace.
Loading existing USD does not rewrite its authored sub-identifier.

## Exact types and failures

By default, if a signature is supplied, every parameter must match in count,
order, and SDK type identity. Equivalent qualification is allowed; implicit
conversions, assignment compatibility, and guessing by an external type's
basename are not.
For re-exports, use the identity reported by the SDK. For example, re-exporting
`wrap_mode` from `::tex` does not make a parameter local when the SDK still
reports `::tex::wrap_mode`; its signature keeps that external qualification.

| Request / situation | Result |
| --- | --- |
| `create_layer(color,int)` when the definition takes `color,float` | Reject, even if the function is unique |
| `create_layer(color)` when the second parameter has a default | Reject: the signature is incomplete |
| `create_layer()` when the function has parameters | Reject: `()` explicitly means zero parameters |
| `pick(Thing)` when only `pick(::a::Thing)` exists | Reject: an unqualified named type must belong to the current module |
| Local `Thing` exists but no overload takes it | Reject `pick(Thing)` |
| Overloads take `::a::Thing` and `::b::Thing` | Qualified requests select their respective definitions; `pick(Thing)` does not guess |
| `arrays(int[4])` when the definition takes `int[3]` | Reject |
| `arrays(float[M])` when the SDK signature uses `float[N]` | Reject: deferred-size symbols currently match exactly |
| `::pkg::main::paint` or `::pkg::main::pick(float)` | Reject: the definition name must not include a module namespace |
| Empty sub-identifier, unknown definition, malformed signature, missing module | Reject |

All lookup paths use the same selection rules, whether the module was discovered
eagerly, registered explicitly, or loaded for the first shader lookup. Accepted
spellings can have separate Sdr cache entries but identify the same MDL definition.

## Ambiguous names without a signature

A bare name such as `pick` is rejected when multiple definitions match. The
diagnostic lists their canonical sub-identifiers. This can happen when a new
module version adds overloads to a formerly unique function.

For applications that need the historical first-overload behavior, set
[`USD_MDL_ALLOW_FIRST_OVERLOAD=1`](environment-variables.md#usd_mdl_allow_first_overload)
**before starting the application**. The plugin then selects the first eligible
overload in SDK enumeration order and warns with the selected canonical name,
unless the separate [struct constructor preference](#legacy-struct-constructor-references)
is enabled.
This option defaults to false. It applies only when the signature is absent:
incorrect explicit signatures and `pick()` without a zero-parameter overload
still fail. SDK enumeration order is not a substitute for a persistent explicit
signature; author the selected node's `mdl:subIdentifier` to remove ambiguity.

This option provides temporary compatibility for old legacy content and
**may be removed at any time**.

## Legacy struct constructor references

Legacy USD may reference a generated struct constructor using only the struct's
simple name. A struct can have multiple generated constructors, so that name
does not necessarily identify one definition. For example, a module can expose:

| Canonical sub-identifier | Constructor |
| --- | --- |
| `Record(Record)` | Copy an existing struct |
| `Record()` | Construct with default field values |
| `Record(int,float)` | Construct from field values |

When these constructors are eligible, a bare `Record` is ambiguous and rejected
by default. `USD_MDL_ALLOW_FIRST_OVERLOAD=1` permits the lookup with a warning,
but normally selects the first eligible constructor in SDK enumeration order. That can
be the copy constructor even when the graph authors individual field inputs.
Successful lookup therefore does not establish that the intended constructor
was selected.

For legacy graphs that expect field inputs, enable both settings before the
first MDL/Sdr operation:

```text
USD_MDL_ALLOW_FIRST_OVERLOAD=1
USD_MDL_PREFER_STRUCT_ELEMENTAL_CONSTRUCTOR=1
```

The second setting prefers the eligible elemental constructor, such as
`Record(int,float)`, and warns with the selected canonical name. It has no
effect without the first setting. If no eligible elemental constructor exists,
the first eligible SDK overload remains the fallback. Explicit signatures and
ordinary function overloads are unaffected. This is a selection policy, not a
check that the graph's inputs match the chosen constructor.

The preference defaults to false. See the [setting reference](environment-variables.md#usd_mdl_prefer_struct_elemental_constructor).

This option provides temporary compatibility for old legacy content and
**may be removed at any time**.

The plugin does not infer the intended constructor from authored USD inputs or
connections. For a lasting repair, choose the constructor the
graph needs and author its explicit canonical `mdl:subIdentifier`, for
example `Record(int,float)` for field inputs. The material-signature
compatibility option does not apply to struct constructors.

## Legacy material signatures

Materials have no overloads, and their canonical sub-identifier is their simple
name. Older content can nevertheless carry a full signature that no longer
matches the selected module version. Such mismatches are rejected by default.

Set [`USD_MDL_ALLOW_MATERIAL_SIGNATURE_MISMATCH=1`](environment-variables.md#usd_mdl_allow_material_signature_mismatch)
before starting the application to permit a fallback to the simple material
name after exact lookup fails. The selected module and definition eligibility
still apply. The plugin warns with the requested and actual signatures, and
returns the canonical simple name for future authoring. It does not rewrite
the USD or check existing inputs/connections against the new material version.

This setting does not relax function signatures or accept malformed signatures.
It is independent of `USD_MDL_ALLOW_FIRST_OVERLOAD`. **These options are temporary
compatibility settings for loading old legacy content and may be removed at
any time.** Author canonical sub-identifiers to avoid relying on them.

## Unsupported legacy qualified definition names

Some legacy content uses the usual source-asset attributes but stores a fully
qualified definition name in the sub-identifier. This is invalid and will not
be supported. For example, this anonymized reference is rejected:

```usda
uniform token info:implementationSource = "sourceAsset"
uniform asset info:mdl:sourceAsset = @materials/paint.mdl@
uniform token info:mdl:sourceAsset:subIdentifier = "::legacy::materials::paint::paint"
```

The source asset already selects the module. Correct this material reference
by changing the sub-identifier to `paint`. A definition's module prefix is
invalid even when it agrees with the source asset; the plugin does not strip
or interpret it. Neither legacy compatibility option enables this spelling.

This restriction applies to the definition name, not to parameter types inside
a signature. For example, `pick(::pkg::types::Thing)` remains valid when it
matches the selected module's definition.

## Unsupported legacy MDL schema

The older MDL schema using `info:id = "mdlMaterial"` and custom `module` and
`name` attributes is **not supported by this plugin**. For example:

```usda
def Shader "Paint"
{
    uniform token info:id = "mdlMaterial"
    custom asset module = @materials/paint.mdl@
    custom string name = "paint"
    token outputs:out
}
```

The plugin does not interpret these legacy attributes or migrate the stage.
Neither legacy sub-identifier compatibility option enables this schema.
Content must use the source-asset attributes instead:

```usda
uniform token info:implementationSource = "sourceAsset"
uniform asset info:mdl:sourceAsset = @materials/paint.mdl@
uniform token info:mdl:sourceAsset:subIdentifier = "paint"
```

Legacy content may also connect the material through generic `outputs:surface`.
When migrating a bound material, author its MDL render-context output, such as
`outputs:mdl:surface`, connected to the shader output.

## Modules outside search paths

This remains outside the supported content setup. The implementation can use
local names and package-relative names for encoded sibling modules; those
signatures avoid embedding the sibling's storage location. Dependencies with
stable absolute MDL names retain those names. This does not promise support
for arbitrary standalone module layouts. Configure
[MDL search roots](environment-variables.md#mdl-search-paths) for supported use.
