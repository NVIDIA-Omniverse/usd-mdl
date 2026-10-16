<!-- SPDX-License-Identifier: Apache-2.0 -->

# Ar resolver integration

`usdMdl` installs an `IMdl_entity_resolver` that routes module and resource
reads through the active OpenUSD `ArResolver`. It does not fall back to the MDL
SDK filesystem resolver. An external module or ordinary resource miss returns
`nullptr`; a rejected logical resource spelling is preserved as described under
[rejected resources](#rejected-resources). This keeps custom scheme, filesystem,
and failure behavior under one asset-resolution policy.

The supported content setup currently requires file-backed modules and their
resources to belong to configured MDL search roots. See [search-path configuration](environment-variables.md#mdl-search-paths)
for USD reference examples, application-provided paths, user/system options,
and precedence. Resolver code for loading outside those roots already exists,
but that behavior is outside the current supported scope.
The [authoring recommendations](sub-identifiers.md#authoring-module-source-assets)
distinguish reusable library references from strict layer-relative paths for
modules shipped with a scene.

The resolver is installed by `Context::initializeLocked()` after
`INeuray::start()` and before the first `load_module()` call:

```c++
mi::base::Handle<mi::neuraylib::IMdl_entity_resolver> resolver(
    usdMdl::runtime::CreateArEntityResolver(impexp, configuredMdlPaths));
mdlConfiguration->set_entity_resolver(resolver.get());
```

Native consumers can query that same installed external resolver after
initialization. Adopt the returned reference into a handle and release it before
`UsdMdlShutdown()`; see [runtime access and shutdown](native-integration.md#runtime-access-and-shutdown):

```c++
mi::base::Handle<mi::neuraylib::IMdl_entity_resolver> resolver(
    pxr::UsdMdlGetEntityResolver());
```

In the currently supported SDK versions, `IMdl_configuration::get_entity_resolver()`
creates the MDL SDK's built-in filesystem resolver rather than returning the
external resolver set with `set_entity_resolver()`. The plugin accessor provides
access to that external resolver. MDL SDK 2026.1 is planned to provide a getter
for the active entity resolver directly through the SDK API.

The resolver uses the [root snapshot and precedence](environment-variables.md#effective-order-and-lookup)
established at initialization. Absolute MDL names such as `::pkg::module` are mapped
to `pkg/module.mdl` and probed under each root through `Ar`. Relative imports
are anchored to the owner's resolved file and the resolver derives the
absolute MDL name returned in `IMdl_resolved_module`; the SDK does not derive
that result for an external resolver. Standard MDL modules are handled by the
compiler before ordinary external asset lookup.

Module resolver callbacks accept MDL names, including encoded URI names and
relative import names, rather than raw filenames or URIs. Pass asset paths to
`UsdMdlAddModuleToRegistry()` or Sdr asset lookup, which derive the MDL name
before loading. Hosts calling the SDK's `load_module()` directly use absolute
MDL names such as `::pkg::paint`.

## Embedded SDK modules and dummy files

The MDL SDK embeds standard modules such as `::math` and `::state`. Their
definitions are available without an external MDL source file. For example,
this USD reference works through the plugin even when Ar cannot find `math.mdl`:

```usda
uniform asset info:mdl:sourceAsset = @math.mdl@
uniform token info:mdl:sourceAsset:subIdentifier = "sqrt(float)"
```

Every valid MDL Sdr node exposes `mdl:is_embedded_module`, with string value
`"true"` when its loaded SDK module has no backing source filename, and
`"false"` otherwise. This describes the SDK module, so an optional dummy file
does not change the flag. It is not an indication of a failed module load.

| Module | `mdl:is_embedded_module` | `mdl:moduleUsdIdentifier` | Both resolved Sdr URIs |
| --- | --- | --- | --- |
| File-backed `::pkg::paint` under an MDL root | `"false"` | `pkg/paint.mdl` | Ar identifier of the source file |
| Embedded `::math`, nonempty dummy available | `"true"` | `math.mdl` | Ar identifier of the dummy file |
| Embedded `::math`, no dummy available | `"true"` | `math.mdl` | Empty |

The two URI accessors are `GetResolvedImplementationURI()` and
`GetResolvedDefinitionURI()`. Nonempty values can be opened directly through
Ar. The portable authoring identifier remains valid with or without a dummy:
use `mdl:moduleUsdIdentifier` and `mdl:subIdentifier` to author a new USD shader.
An empty URI with the embedded flag means that no physical file is required.
A missing ordinary module still fails loading.

Some external resolvers, asset validators, or applications require every
source asset to identify a physical file. The workaround is a dummy `math.mdl`
at the root of the module library containing:

```mdl
mdl 1.7;
export using ::math import *;
```

The SDK does not compile the dummy's contents: it supplies the embedded
implementation. Empty files and files with invalid MDL syntax still select
the embedded definitions of `math.mdl` and `state.mdl`. This applies to
embedded modules; ordinary file-backed modules must contain valid MDL.

The dummy gives USD a physical asset. The plugin retains a dummy already
selected by USD, otherwise tries the authored path through Ar and then the
configured MDL search roots.
If none is found, lookup still succeeds with empty resolved Sdr URIs.

Use a nonempty dummy when a resolved Sdr URI is needed through the MDL search
roots. The current fallback requires an Ar data buffer, so a zero-byte file
leaves those URIs empty even though embedded-module lookup succeeds. A
zero-byte dummy already resolved by USD retains its URI because that path
only requires an openable asset.

Configure the library root for the external resolver or USD stage's Ar context
as well as for MDL. MDL search paths alone do not configure Ar: the plugin may
find a dummy through MDL roots while USD leaves the source asset's resolved
path empty. With `ArDefaultResolver`, successful embedded-module lookup emits
no warning or error merely because the dummy is absent. Independent validators
and custom resolvers can have their own diagnostics; the flag does not suppress
them.

Without dummy files, use on-demand shader lookup or explicitly register an
embedded module with `AddModuleToRegistry(Sdf.AssetPath("math.mdl"))`. Discovery
by scanning module files cannot enumerate an absent dummy.

### Base and builtin constructors

`::base` is also embedded in the SDK. Its nodes use `base.mdl` as their
authoring identifier and follow the optional-dummy behavior above.

Use `mdl-builtin.mdl` to reference the SDK's builtin constructors:

```usda
uniform asset info:mdl:sourceAsset = @mdl-builtin.mdl@
uniform token info:mdl:sourceAsset:subIdentifier = "float(float)"
```

The plugin maps that filename to the SDK module `::%3Cbuiltins%3E` and loads
it explicitly, so it can be the first module requested after initialization.
Direct lookup, registration, and file discovery use the same alias. A dummy
file is optional and its contents are ignored, as for the embedded modules
above. Paths whose final component is exactly `mdl-builtin.mdl` also select
the alias; a name such as `other-mdl-builtin.mdl` remains an ordinary module.

For the example, `mdl:moduleUsdIdentifier` remains `mdl-builtin.mdl` and
`mdl:subIdentifier` is `float(float)`. The database metadata preserves the
SDK identities: module `mdl::%3Cbuiltins%3E` and definition `mdl::float(float)`.
Builtin definition names omit the module prefix; use the metadata when
accessing them in an SDK transaction. The usual exact-signature rules apply,
so an ambiguous bare `float` is rejected by default.

## Off-search-path module names

This section describes implementation details outside the current supported
content setup. Applications should configure MDL search roots rather than
depend on this encoding to load standalone modules.

An absolute path or resolver URL outside the configured MDL roots has no
natural `::package::module` name. `EncodeUriToMdlName()` creates a deterministic
name that the resolver can reverse without a process-local registry.

The SDK load API, database, and external resolver callback use the same public
MDL names shown below.

| Asset | Public MDL name |
| --- | --- |
| `custom:pkg/foo.mdl` | `::custom%253A::pkg::foo` |
| `custom:foo.mdl` | `::custom%253A::foo` |
| `file:/home/user/foo.mdl` | `::file%253A%252F::home::user::foo` |
| `file:///home/user/foo.mdl` | `::file%253A%252F%252F%252F::home::user::foo` |
| `file:/C:/user/foo.mdl` | `::file%253A%252F::C%253A::user::foo` |
| `https://host/pkg/foo.mdl` | `::https%253A%252F%252F::host::pkg::foo` |
| `custom:/pkg/foo.mdl` | `::custom%253A%252F::pkg::foo` |

The encoder escapes both `:` and existing `%` characters. The first component
stores the scheme and its exact slash prefix: `:` becomes `%253A`, and each
following slash becomes `%252F`.
The remaining identifier starts in the next component even when no slash
follows the colon. Decoding does not depend on a list of known schemes.

Preserve the URI's slash spelling consistently, including for `file:`:
`scheme:...`, `scheme:/...`, `scheme://...`, and `scheme:///...` retain distinct encoded
identities. In particular, `file:/...` and `file:///...` can create separate
SDK modules for the same file outside the search roots. Choose one spelling
and use it consistently; the plugin does not fold triple slashes to one.

Encoded resource paths use the same prefix convention, `/` component
separators, and the original filename extension. For example,
`custom:pkg/texture.png` becomes `/custom%253A/pkg/texture.png`, whereas
`custom:/pkg/texture.png` becomes `/custom%253A%252F/pkg/texture.png`.

When an asset is under a configured MDL root, the encoder uses the shorter
natural form (`::pkg::foo`). The resolver finds that name by probing the same
root snapshot through `Ar`.
Ordinary components use the SDK's single-pass name escaping: a literal
`pkg/file%3A/paint.mdl` becomes `::pkg::file%253A::paint`. The resolver decodes
each component once for file lookup, preserving the literal `%3A` in the
directory name. The [reserved first-component convention](#limitations)
still applies.

## Relative imports and resources

For `.::sibling` and `..::parent` imports, the resolver:

1. converts the MDL relative name to a strict relative `.mdl` path;
2. combines the relative components with `owner_name` to derive an absolute
   MDL module name, rejecting traversal above the package root; and
3. resolves the file through `Ar` using the owner file as the anchor.

Parent traversal is bounded by the owner's package depth, with no fixed limit
on the number of leading `..` components.
For encoded URI modules, the scheme and a nonempty authority (such as a host)
form a protected root, even though they occupy separate encoded components.
Relative module imports and resource paths cannot use `..` to remove or replace
either component. For an owner at `http://nvidia.com/content/mdl/foo.mdl`, two
parent steps can reach `http://nvidia.com/`; a third is rejected before Ar
anchoring. Relative traversal therefore cannot select another host or scheme.
The active Ar resolver determines how accepted relative imports and resources
are anchored to their owner's URI.

Resources use the same owner anchor. Absolute MDL resource paths such as
`/pkg/textures/albedo.png` are probed beneath configured MDL roots in order;
the first match wins.

Logical MDL resource paths allow one leading `./` or leading `../` components
within the owner's package depth. Absolute paths and the rest of a relative
path cannot contain `.` or `..` components. Rejected logical resource requests
remain unresolved while their module can still load. They do not add an SDK
execution-context error. Custom resolver URIs retain their own path semantics.
Explicit Windows drive and backslash paths keep their existing native-path
handling. Leading-slash paths are validated as logical MDL paths before the
existing POSIX-path fallback.

`IValue_resource::get_file_path()` is used for Sdr asset defaults when the SDK
provides it; a referenced DB element's original filename is the fallback.
Invalid resource values remain empty instead of fabricating an asset path.

### Rejected resources

The resolver preserves a rejected logical resource reference across the SDK's
compilation and resource-loading phases. For example, an owner in
`::d3::nested::probe` cannot use `../../../texture.png`: that reference goes above
the MDL package root. A readable `/texture.png` remains a different, valid
reference and must not become the rejected reference's replacement.

For these rejections, the compiler receives a one-element resource descriptor
with a private absolute MDL identity instead of a null result. The identity
encodes the original spelling, owner and rejection details. It carries no image
bytes. Subsequent lookups of that identity return no resource before Ar or MDL
search roots are consulted. This prevents the SDK from normalizing the rejected
path into a different resource while keeping permissive module loading.

The private identity is internal to the resolver and SDK database. usdMdl does
not expose it as an Sdr asset default. The rejected default remains empty, and
[rejection diagnostics](diagnostics.md#rejected-resource-messages) identify the
original reference and source location. A legitimate `../../texture.png` or
`/texture.png` continues to resolve normally, including when used in the same
module as a rejected reference. Imported defaults retain their rejection
identity; there is no process-wide blacklist of normalized paths.

Native callers resolving an invalid logical path can receive this unreadable
descriptor on the initial callback. A non-null descriptor alone therefore does
not establish a usable resource: its filename is empty and `create_reader()`
returns null. SDK resource-loading lookups of its private identity return null.

This preservation applies to malformed logical MDL paths and traversal above
the owner's package root. Ordinary missing resources still follow the existing
lookup policy and can produce an empty default; see [limitations](#limitations).

Resolver results keep the MDL and storage namespaces separate:

- `get_mdl_file_path()` and its mask return canonical absolute MDL paths
  beginning with `/`;
- `get_filename()` retains the exact `Ar`-resolved backing identifier,
  normally a native OS path for local files or a scheme-bearing identifier for
  a custom resolver. Its mask restores the texture markers on the resolved
  files' common path; and
- Sdf resolved asset paths retain that same Ar identifier. Concrete resources
  can be opened directly with `ArGetResolver().OpenAsset(ArResolvedPath(path))`
  using the same resolver/context. Tile and frame masks must first be expanded.

The active resolver determines the format: Windows filesystem identifiers may
use drive letters and backslashes, POSIX identifiers use absolute `/` paths,
and custom resolvers may return URIs. usdMdl does not add `file:`, change
separators, or percent-encode these resolved values. A resolved path describes
the current runtime's resource location; it is not a portable authored path.

Authored MDL resource paths keep their logical package identity, which does
not identify the search root that supplied the resource. Explicit native paths
and URIs, and resources found only through Ar or an off-search-path owner,
receive a reversible encoded MDL path. Explicit resource paths are never
shortened against MDL roots, even when the resource lies inside one.

Encoded MDL resource paths preserve the selected storage identifier when passed
back to the entity resolver without an owner. Ordinary logical MDL paths can
also be resolved without an owner, but follow search-root precedence and do
not guarantee a round trip to the same backing resource. Use Ar directly to
open a concrete Sdf resolved path: a native POSIX path passed to the MDL entity
resolver is also interpreted as a logical MDL path and probes search roots
first.

Local `file:` fallback decodes percent escapes exactly once. For example,
`file:/textures/space%20%23%20%2520.png` identifies `space # %20.png`.
Malformed escapes, NULs, and URI queries/fragments have no native fallback.

### Search-path precedence and shadowing

This resolver intentionally omits MDL's usual shadowing validation. Checking
for conflicts across multiple remote search roots would require additional
resolution requests. Conflicting resources are treated as a content setup
error: the resolver accepts the first match in search-root order instead of
reporting a shadowing error.

For example, with `/root1` ahead of `/root2`, a module at `/root2/module.mdl`
may declare `./textures/a.png` as its default texture. Once the SDK represents
that resource as the absolute logical path `/textures/a.png`, a matching
`/root1/textures/a.png` takes precedence. This can happen during the initial
SDK load, not just during later re-resolution. The resulting Sdf asset path
has authored path `/textures/a.png` and resolved path
`/root1/textures/a.png`. Supplying the owner alongside that absolute
logical path does not constrain resolution to the owner's search root.

## Tiles and frame sequences

Native applications should call `IMdl_entity_resolver::resolve_resource()` on
the resolver returned by `UsdMdlGetEntityResolver()` after initialization.
This uses the configured MDL search roots, owner-relative resolution, and the
active Ar resolver/context, just as SDK resource loading through the plugin
does. Supported masks include `<UDIM>`, `<UVTILE0>`, `<UVTILE1>`, and
angle-bracketed frame markers such as `<####>`.

The returned `IMdl_resolved_resource` contains frame elements, each with its
tiles, resolved identifiers, and resource readers. The native example's
[`PrintResourceFiles()`](../examples/inspect_mdl_material.cpp) shows how to
expand an Sdr asset's resolved mask and enumerate the concrete filenames.

### Implementation details and limits

The entity resolver selects candidate paths, then calls the internal
[`ExpandTilesViaAr()`](../src/usdMdl/runtime/TileExpansion.cpp) helper:

1. `IMdl_impexp_api::frame_uvtile_marker_to_string()` substitutes a concrete
   frame and tile coordinate.
2. `ArResolver::Resolve()` probes the resulting identifier.
3. The entity resolver groups matches by frame and exposes them through
   `IMdl_resolved_resource`, with tiles ordered by `v`, then `u`.

Applications use the entity-resolver interface; the helper is not a public
API. Its probing limits also apply to calls through that interface.

Ar has no directory-enumeration interface. Tile expansion probes `u` and `v`
from 0 through 9 and frames from 0 through 1000, stopping after 32 consecutive
missing frames, including before the first hit. Sequences starting at 1001,
gaps of 32 or more frames, and tiles outside that grid are unsupported; an
existing prefix of a sequence can therefore be returned without later frames.

Mask construction uses the files that actually resolved, including files found
through an Ar search context. The resolved files must preserve the authored
suffix starting at the first marker's directory component, and must agree on
one common mask. A backend that rewrites that suffix or scatters tiles across
unrelated paths cannot currently expose a resource set through this interface.

## Readers

Module bytes are obtained with `ArAsset::GetBuffer()` and exposed to the SDK by
`ArAssetReader`. Resource readers open their `ArAsset` lazily. The MDL SDK does
not reopen the resolved identifier through a separate filesystem path.

## Resource formats

Resolution and decoding are separate. `Ar` locates bytes; MDL SDK support or
an image plugin decodes them.

| MDL resource | Format | Status |
| --- | --- | --- |
| `texture_2d`, `texture_cube` | PNG, JPEG, EXR, HDR, TIFF, and other OpenImageIO formats | Supported by the `nv_openimageio` plugin |
| `texture_2d` | DDS | Supported by the `dds` plugin |
| `texture_2d` | UDIM and frame sequences | Resolved through the [entity resolver](#tiles-and-frame-sequences) |
| `light_profile` | IES | Supported by the MDL SDK |
| `bsdf_measurement` | MBSDF | Supported by the MDL SDK |
| `texture_3d` | OpenVDB | Not supported by the shipped decoder set |
| `texture_ptex` | Ptex | Not supported by the MDL SDK |

## Limitations

- Resource paths beginning with `/__usdMdl_rejected__/` are reserved for
  [preserved rejection identities](#rejected-resources). The resolver rejects
  this namespace before Ar lookup, including unknown or malformed identities.
  Do not place authored resources under that logical MDL path.
- Reserve first package components consisting of a URI scheme followed by
  `%3A` and zero or more `%2F` markers for the plugin's
  [URI encoding](#off-search-path-module-names): this includes `%3A`, `%3A%2F`,
  `%3A%2F%2F`, and `%3A%2F%2F%2F` suffixes.
  These are literal directory-name spellings beneath an MDL search root;
  for example, avoid `file%3A/paint.mdl`. They can be mistaken for an encoded
  resolver URI, causing lookup to fail or load a different file. URI detection
  examines only the first package component, so this naming conflict does not
  apply to later components.
- Loading MDL modules or resources stored inside USDZ archives is not yet
  supported. Ar may read a package member such as
  `asset.usdz[materials/paint.mdl]`, but that does not establish MDL SDK module
  loading or dependency/resource resolution through this plugin. Use unpackaged
  MDL content under configured MDL search roots.
- With `ArDefaultResolver`, a layer-local module can override a library module
  referenced by a weak relative source asset such as `materials/paint.mdl`.
  The layer-relative existence check also precedes search-path lookup. See
  [default resolver limitations](sub-identifiers.md#default-resolver-limitations).
- Tiles and frame sequences have [bounded probing and common-mask requirements](#implementation-details-and-limits).
- MDLE containers are not supported by the Ar entity resolver. They fail like
  unsupported MDR/container input rather than bypassing `Ar`.
- The stock filesystem `ArDefaultResolver` does not resolve HTTP(S) URLs.
  HTTP and other server-backed schemes require an `Ar` URI resolver plugin
  registered for that scheme; usdMdl does not fetch those URLs itself.
- Direct Ar access to a resolver URI does not guarantee MDL SDK image import.
  With MDL SDK 2026.0.0, a custom resolver returning URI filenames can reach an
  image-loading path that opens them as local files, causing texture defaults
  to remain invalid even though Ar can read their bytes. See the SDK's
  [filename-based image loader](https://github.com/NVIDIA/MDL-SDK/blob/2026.0.0/src/io/image/image/image_canvas_impl.cpp#L137).
- Server-authority file URIs such as `file://server/share/mdl/resource.png` are
  not translated to Windows UNC paths. On Windows, a network share can instead
  be mapped to a drive and referenced through an ordinary absolute path such
  as `S:/mdl/resource.png`. The mapping and its credentials must be
  visible to the process; mappings are commonly scoped to a user and logon
  session and may therefore be unavailable to services, CI jobs, elevated
  processes, or other accounts.
- `ArResolver::Resolve()` must be able to resolve every configured filesystem
  root or custom identifier used by the content.
- Rejected logical paths are preserved internally and reported through
  [rejection diagnostics](diagnostics.md#rejected-resource-messages), but other
  resource values the MDL SDK marks invalid can still lose their requested path.
  An intentionally empty resource and a failed resource lookup can produce the
  same empty Sdr asset default. Module loading may still succeed; an empty default
  does not prove the author requested an empty resource. See
  [diagnostics and resource-reporting limitations](diagnostics.md).
