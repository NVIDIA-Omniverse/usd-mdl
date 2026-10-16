<!-- SPDX-License-Identifier: Apache-2.0 -->

# Node and property metadata

usdMdl translates MDL annotations into OpenUSD Sdr metadata when it parses a
shader definition. Node metadata describes the function or material; property
metadata describes an input or output. Standard Sdr accessors expose fields
such as labels, help, pages and connectability. The plugin also publishes
MDL-specific identity, type and default information.

For ordinary MDL modules, use annotations from the SDK's `::anno` module or
declare explicit Sdr annotations such as `SDR__label` in your own module.
Only the exact `SDR__` prefix opts a custom annotation into direct promotion;
unprefixed custom annotations remain raw metadata. The plugin requires no
separate USD annotation module or application callback.
For an interface with independently authored port names, types and defaults,
use [USD shader definitions](shader-definitions.md).

## Annotation scope and storage

| MDL annotation location | Sdr destination |
| --- | --- |
| Function or material definition | Node `GetMetadata()`; direct keys match `SdrNodeMetadata` |
| Input parameter | Input property's `GetMetadata()` and `GetHints()`; direct keys match `SdrPropertyMetadata` |
| Return declaration | Whole-value output property's metadata and hints |
| Exposed struct return field | That field's output property; return-declaration annotations do not propagate to fields |
| Module | Nested `mdl.annotationModule` inside each node's `mdl.annotation`; no direct promotion |
| Struct or enum input type | Nested `mdl.annotationType` inside the property's `mdl.annotation`; no direct promotion |
| Enum input member | Nested `mdl.annotationValue` under the property's `mdl.annotation`, indexed by declaration order; no direct promotion |

In C++, standard metadata and property hints are `SdrTokenMap` instances:
`TfToken` keys with string values. In Python, `GetMetadata()` and `GetHints()`
return dictionaries of those strings. They are separate maps. In particular,
`GetMetadata()["hints"]` does not populate `GetHints()`.

The plugin collects the occurrences returned by the SDK annotation block as
typed values in a nested `VtDictionary`. A scope with converted occurrences
contains an ordered `annotations` list of records. A record has a `name`
containing the full SDK-qualified annotation name and argument signature, and
an `arguments` dictionary containing the complete converted arguments.
Zero-argument annotations have an empty dictionary. An occurrence whose
arguments cannot all be converted is omitted. The SDK may suppress some
repeated built-in annotations during compilation; occurrences it does not
expose cannot be retained by the plugin.

Repeated annotations returned by the SDK remain separate records, including
repeated uses of the same name and overloads with different signatures. SDK
names retain the `::anno::` prefix; custom names retain their owning module.
Names are stored exactly as returned by the SDK, including any
language-version suffix. Records describe compiled annotations, including
arguments supplied by SDK defaults. Promotion derives Sdr fields and hints
from these records without removing them.

The scope dictionaries are serialized into the string-valued `mdl.annotation`
metadata field. This is a Python-style representation, not a live nested Python
dictionary or JSON. For example, two uses of a custom annotation are stored as:

```python
{
    "annotations": [
        {"name": "::studio::private_note(string)",
         "arguments": {"text": "Review surface color"}},
        {"name": "::studio::private_note(string)",
         "arguments": {"text": "Review roughness"}},
    ]
}
```

Nested module and type scopes use the same record format. Enum member scopes
remain under `mdl.annotationValue`, keyed by declaration index, with a separate
`annotations` list for each member. Scope nesting does not make these
annotations eligible for node or property promotion. Empty scopes omit the
`annotations` key; `mdl.annotation` itself is absent when there are no converted
records or nested scopes. Consumers should use `scope.get("annotations", [])`
when inspecting a parsed scope.

## Direct Sdr metadata annotations

An annotation's final name must start with the exact, case-sensitive `SDR__`
prefix. The plugin removes that prefix and matches the remainder against the
token **value** in `SdrNodeMetadata` or `SdrPropertyMetadata` from the OpenUSD SDK
used to build it. For example, `::studio::SDR__label` matches `Label`'s token
value, `label`. `label`, `sdr__label` and `SDR__Label` do not match and remain
raw metadata. The owning MDL module does not restrict an explicitly prefixed
annotation's eligible scope.

| Annotation name | Applicable scope | Sdr field / stored key |
| --- | --- | --- |
| `SDR__label` | Node and property | `Label` / `label` |
| `SDR__help` | Node and property | `Help` / `help` |
| `SDR__category` | Node | `SdrNodeMetadata->Category` / `category` |
| `SDR__page` | Property | `SdrPropertyMetadata->Page` / `page` |
| `SDR__widget` | Property | `SdrPropertyMetadata->Widget` / `widget` |
| `SDR__connectable` | Property | `SdrPropertyMetadata->Connectable` / `connectable` |
| `SDR__colorspace` | Property | `SdrPropertyMetadata->Colorspace` / `__SDR__colorspace` |
| `SDR__implementationName` | Node and property | `ImplementationName` / `__SDR__implementationName` |
| `SDR__sdrUsdDefinitionType` | Property | `SdrPropertyMetadata->SdrUsdDefinitionType` / `sdrUsdDefinitionType` |

These are examples, not a fixed allowlist. All tokens in the appropriate
metadata token set participate; the available fields depend on the OpenUSD
version. A node-only key on a property, or a property-only key on a node,
remains raw annotation metadata. The name must also be a legal MDL identifier;
the plugin does not sanitize punctuation in metadata keys.

For Sdr token values beginning with `__SDR__`, the suffix without that reserved
prefix is accepted as an alias after removing the annotation's `SDR__` prefix.
Thus `SDR__colorspace` maps to `__SDR__colorspace`, and
`SDR__implementationName` maps to `__SDR__implementationName`. The full-token
form, such as `SDR____SDR__colorspace`, also matches. These names start with a
letter and remain legal MDL identifiers; an annotation beginning directly with
`__SDR__` is not legal MDL.

For direct promotion, a single argument supplies its value regardless of the
argument's name. A zero-argument marker supplies `"1"`. Multiple arguments have
no unambiguous single value and remain raw annotations. Boolean values become
`"0"` or `"1"`; strings remain verbatim; string arrays become pipe-delimited
lists such as `"Main|Advanced"`. Other convertible values use their string
representation. Authors must supply values appropriate to the target Sdr field;
generic promotion does not validate each field's meaning.

### Precedence and repeated annotations

A direct Sdr annotation wins over an SDK annotation fallback or a
converter-supplied metadata value, regardless of where the SDK fallback occurs
in the annotation block. An explicitly empty direct value also wins.

Every eligible explicit annotation that targets the same Sdr field must
produce the same promoted string value. For example,
`::first::SDR__label("First")` and `::second::SDR__label("Second")` emit a
diagnostic and reject the node, in either order. This also applies to repeated
uses of the same qualified annotation, overloads with different signatures,
and a reserved-key alias and its full-token form. Identical values are accepted.
SDK fallback annotations do not create these conflicts.

Each occurrence is considered separately. A multi-argument occurrence remains
raw and does not replace or hide an earlier eligible single-value occurrence.
Unprefixed, unknown and wrong-scope annotations remain raw as well. All
convertible occurrences are retained in the ordered raw records, including
those that supplied the promoted value.

### Connectability, names and types

An input without an explicit `SDR__connectable` annotation gets
`connectable = "1"`. Use a boolean annotation: `SDR__connectable(false)` stores
`"0"` and makes it non-connectable; `SDR__connectable(true)` stores `"1"`.
OpenUSD always treats output properties as connectable, even if their metadata
contains `"0"`.

`SDR__label` changes a display label, not `GetName()` or the MDL parameter name.
`SDR__implementationName` supplies backend-name metadata; it does not rename the
exposed port or change the SDK definition. Author an independent interface
through [USD shader definitions](shader-definitions.md) when exposed names or
output layout must differ.

`SDR__sdrUsdDefinitionType` selects a compatible USD value representation while
preserving the declared MDL type. Its conversion is explicitly validated;
unknown or incompatible types emit a diagnostic and reject the node. See
[explicit USD types and symbolic enums](type-mapping.md#explicit-usd-types-and-symbolic-enums)
for role conversions, symbolic enum defaults/options and array limitations.

Setting a metadata string does not override every Sdr accessor. For example,
node `GetPages()` derives pages from property `page` values; a `SDR__options`
annotation does not replace the property's converter-supplied option vector.
Presence-based flags such as `isAssetIdentifier` and `defaultinput` are enabled
by the key's presence: `SDR__isAssetIdentifier(false)` or
`SDR__defaultinput(false)` still authors the key. Omit the annotation when you
want no explicit flag; converters may still supply one for the declared MDL
type, such as an asset identifier for a texture.

`SDR__colorspace` identifies a companion colorspace parameter; it does not infer
a named color space from a texture's gamma. See
[texture color spaces](type-mapping.md#texture-gamma-and-color-spaces).

## MDL built-in annotation mappings

Only annotations from the SDK's `::anno` module receive the following
fallback translations. A custom `::studio::display_name` or
`::studio::description` remains raw metadata. Direct `SDR__label`,
`SDR__help`, `SDR__page` and `SDR__hints` annotations take precedence over their
respective fallbacks. SDK fallback and hint handling does not require `SDR__`.

| MDL built-in annotation | Node metadata | Property metadata | Value used |
| --- | --- | --- | --- |
| `::anno::display_name(name)` | `SdrNodeMetadata->Label` (`label`) | `SdrPropertyMetadata->Label` (`label`) | `name` |
| `::anno::description(description)` | `SdrNodeMetadata->Help` (`help`) | `SdrPropertyMetadata->Help` (`help`) | `description` |
| `::anno::in_group(group, subgroup, subsubgroup)` | No promotion | `SdrPropertyMetadata->Page` (`page`) | Nonempty group names joined by `:`; stop at the first missing or empty group |
| `::anno::usage(hint)` | No promotion | `SdrPropertyMetadata->Hints` (`hints`) | Collect all hints in order, remove repeated identical hints and join them with `\|`; this is metadata, separate from `GetHints()` |

For scalar SDK fallbacks, the last eligible occurrence returned by the SDK
supplies the complete value. The plugin does not merge arguments from
separate occurrences. This is the plugin's resolution policy, rather than a
restriction on MDL annotation repetition. `::anno::usage` instead collects
its hints: for example, `usage("color"), usage("normal"), usage("color")`
produces `"color|normal"`. An explicit `SDR__hints` value replaces that
entire fallback list. The original occurrences remain in `mdl.annotation` in
either case.

### Property UI hints

These SDK annotations populate the property's separate `GetHints()` map.
They do not populate standard node metadata fields. The consuming application
interprets the hints; promotion itself does not enforce ranges, hide controls
or evaluate conditions. For a repeated hint annotation, the last eligible
complete occurrence returned by the SDK supplies the value; earlier returned
occurrences remain raw.

| MDL built-in annotation | Property hint key | Stored value |
| --- | --- | --- |
| `::anno::deprecated` | `deprecated` | String representation of the annotation's arguments, or `"1"` for a marker |
| `::anno::enable_if(condition)` | `enable_if` | Original condition string, preserved verbatim |
| `::anno::hard_range(min, max)` | `hard_range` | String representation of the complete `min`/`max` argument dictionary |
| `::anno::hidden()` | `hidden` | `"1"` |
| `::anno::soft_range(min, max)` | `soft_range` | String representation of the complete `min`/`max` argument dictionary |
| `::anno::ui_order` | `ui_order` | String representation of the annotation's arguments |
| `::anno::unused()` | `unused` | `"1"` |

`enable_if` is opaque MDL expression text. The plugin does not parse or evaluate
it, and does not copy it to Sdr's `shownIf` metadata. See the
[Python hint example](python-api.md#property-ui-hints) for access and SDK evaluation.

### Other built-in behavior

| MDL built-in annotation | Node/property behavior |
| --- | --- |
| Definition-level `::anno::hidden()` | Sets the plugin's node key `mdl:hidden` to `"true"`; otherwise it is `"false"`. The original annotation remains raw. It does not invalidate or remove the node. Module-level hidden annotations do not set this flag. |
| `::anno::thumbnail` | The SDK's definition thumbnail accessor supplies node `mdl.thumbnail` when a thumbnail is available. The SDK can also discover a thumbnail by filename; this is separate from generic annotation promotion. |
| `::anno::version` | Remains raw annotation metadata; it does not set the Sdr registry version or select a default node version. |
| Return or struct-type `::anno::node_output_port_default` | Controls struct return-field exposure, independently of metadata promotion. A return-declaration annotation takes precedence over the struct type's annotation. See [struct outputs](type-mapping.md#structs-and-default-expressions). |

Other convertible built-in annotations remain in raw metadata unless handled
by the SDK or another documented conversion. MDL-specific node identity keys
are described in the [Python API](python-api.md#mdl-node-metadata); property type
and default-expression keys are described in [types and defaults](type-mapping.md).

## Example: direct annotations override SDK labels

Save this as an application-owned `metadata_example.mdl` under a configured
[MDL search root](environment-variables.md#mdl-search-paths):

```mdl
mdl 1.8;
import ::anno::*;

annotation SDR__label(string text);
annotation SDR__connectable(bool enabled);
annotation SDR__category(string name);

export color tint_color(color tint = color(0.5) [[
    anno::display_name("Color"),
    SDR__label("Surface Tint"),
    anno::description("Color applied to the surface."),
    anno::in_group("Main", "Surface"),
    SDR__connectable(false)
]])
[[
    anno::display_name("Tint Utility"),
    SDR__label("Color Tint"),
    SDR__category("utilities")
]]
{
    return tint;
}
```

Given the returned Sdr node, the result is:

```python
from pxr import Sdr

print(node.GetMetadata()[Sdr.NodeMetadata.Label])  # Color Tint
print(node.GetMetadata()[Sdr.NodeMetadata.Category])  # utilities
prop = node.GetShaderInput("tint")
print(prop.GetName())         # tint
print(prop.GetLabel())        # Surface Tint
print(prop.GetHelp())         # Color applied to the surface.
print(prop.GetPage())         # Main:Surface
print(prop.IsConnectable())   # False
```

The label is `Surface Tint` because the direct `SDR__label` annotation wins over
`anno::display_name`. Reversing those two annotations produces the same label.
See [registration and lifetime](python-api.md#api-surface)
for obtaining nodes and shutting down the SDK.
