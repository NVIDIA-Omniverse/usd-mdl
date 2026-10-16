<!-- SPDX-License-Identifier: Apache-2.0 -->

# Architecture

`usdMdl` is one shared library that connects NVIDIA's MDL SDK to OpenUSD
`Sdr`. The same binary contains the discovery/parser plugins, MDL-to-Sdr
conversion, the Ar entity resolver, and MDL SDK lifecycle management. There is
no separate runtime library or runtime ABI.

## Source and install layout

The source and installed responsibilities correspond directly:

| Source | Installed location | Purpose |
| --- | --- | --- |
| `src/usdMdl/*.cpp` | `plugin/usd/usdMdl.dll` (Windows), `plugin/usd/libusdMdl.so` (Linux) | Public APIs, Sdr entry points, module parsing |
| `src/usdMdl/converter/` | same shared library | MDL type/value/expression conversion |
| `src/usdMdl/runtime/` | same shared library | MDL SDK lifecycle and Ar resolution; internal `usdMdl::runtime` namespace |
| `src/usdMdl/*.h` selected by CMake | `include/usdMdl/` | Public C++ headers |
| `src/usdMdl/module.cpp`, wrappers | `lib/python/UsdMdl/` | Top-level Python package and extension |
| build-generated shader definitions and accompanying notices | `plugin/usd/usdMdl/resources/` | Canonical Preview Surface interfaces, provenance and OpenUSD license |
| generated package files | `lib/cmake/usdMdl/` | `find_package(usdMdl)` support |

The plugin manifest is installed at
`plugin/usd/usdMdl/resources/plugInfo.json`; the neighboring
`plugin/usd/plugInfo.json` includes each plugin's `resources/` directory.

## Runtime components

- `MdlDiscoveryPlugin` and `MdlParserPlugin` register the OpenUSD plugin
  interfaces and delegate to `MdlSdrIntegration`.
- `MdlSdrIntegration` owns discovery results and the short-lived cache used to
  hand preparsed `SdrShaderNode` objects to parser callbacks.
- `MdlModule` retains definition identities and module annotations while
  parsing. Registration or direct lookup owns one inspection transaction,
  passed through module inspection, definition selection, node conversion,
  and call/resource lookups. Accessed SDK handles are released before that
  read-only transaction is aborted; completed nodes own only USD data.
- `usdMdl::runtime::Context` attaches to a host-owned `INeuray` instance or
  creates one, configures paths, installs the Ar resolver, and owns SDK handles.
- `ModuleLoader` serializes database transaction completion for concurrent
  module requests. Its loading batch commits before an inspection transaction
  opens, so the new snapshot can see the loaded modules and dependencies.
- `EntityResolver` maps module/resource requests to `ArResolver`. Rejected
  logical resource paths receive a private identity that survives SDK rewriting
  and is rejected before later Ar lookup. Per-load records retain the original
  reference for reporting; see [Ar resolver integration](ar-resolver.md).

## Discovery

Startup discovery is disabled by default. Registry creation and search-URI
queries do not start MDL; see [discovery behavior](environment-variables.md#discovery-behavior)
for application guidance and the search-URI contract.

With `USD_MDL_DISCOVER_ON_STARTUP=1`, the first use of `SdrRegistry`:

1. OpenUSD finds `plugInfo.json` through `PXR_PLUGINPATH_NAME`.
2. `MdlDiscoveryPlugin::DiscoverShaderNodes()` initializes the runtime.
3. `IMdl_discovery_api::discover()` walks the configured MDL roots.
4. Each module is loaded and converted into discovery results plus preparsed
   nodes.
5. `MdlParserPlugin::ParseShaderNode()` moves the requested node from the
   cache. A mutex protects parallel parser callbacks.

Configured-path inspection reads `IMdl_configuration` independently of the
discovery graph. Both snapshots remain immutable after publication until shutdown.

## On-demand loading

An asset/sub-identifier lookup initializes the runtime on first use and converts
the selected definition without registering a module-wide catalog.

`UsdMdlAddModuleToRegistry()` and `UsdMdl.AddModuleToRegistry()`:

1. use the supplied resolved path from `SdfAssetPath`, or its authored path
   when no resolved path is supplied;
2. use the natural qualified name for an asset under an MDL root;
3. load and preparse the module, resolving through `Ar` during loading and
   module inspection; and
4. add each result to `SdrRegistry`.

The C++ and Python programs under `examples/` exercise this path from a USD
material binding.

Builtin array-constructor requests have a separate specialization path before
ordinary definition selection. The source asset identifies the element type's
defining module; the sub-identifier contains a local type name and count.
The integration loads that module through `ModuleLoader`; the SDK makes its
builtin definitions available with that load. It opens an inspection snapshot,
expands the local name, and uses
SDK name deserialization/serialization and type ownership to validate the request.
The existing Function/converter machinery then builds concrete input/output
properties. Nodes retain only USD data after the snapshot closes. The generic
template is omitted from discovery/registration; concrete Sdr identities are
cached by the registry on demand. Authoring metadata and resolved URIs describe
the element-type module, while the SDK definition stays builtin and
`mdl:mdlNameWithSignature` retains the fully resolved specialization.
Ordinary helpers continue through the normal
selector. See [constructor authoring and renderer responsibilities](sub-identifiers.md#builtin-array-constructors).

The supported content setup requires configured MDL roots for file-backed
modules and their resources; see [search-path configuration](environment-variables.md#mdl-search-paths).
Modules [embedded in the SDK](ar-resolver.md#embedded-sdk-modules-and-dummy-files)
can be loaded without physical source files.
The implementation also contains URI encoding for off-search-path loading,
which is outside the current supported scope.

## Diagnostics

The plugin reports runtime problems through USD `Tf` diagnostics. After an SDK
module-load call, it forwards execution-context warnings, errors and fatal
messages as USD warnings, including messages from successful loads. Module names,
SDK severity, kind, code, message text and recursive notes accompany each warning
in a structured payload. Information, verbose and debug messages require
`TF_DEBUG=USD_MDL_DIAGNOSTICS`.

`ModuleLoader` collects messages from the load's execution context and delivers
them after its loading-batch bookkeeping, outside loader locks. It does not
replace the application's SDK logger or replay messages on a cache hit.
Resolver rejection records use a separate per-load collector passed through the
SDK execution context. They retain authored paths and locations across imported
modules and are reported through the same USD warning path after batch completion.
Forwarding leaves load and registration results unchanged. See
[diagnostics](diagnostics.md) for the native payload interface, logging policy
and resource-reporting limitations.

## Lifecycle

See the [C++ lifecycle contract](native-integration.md#c-api) for attachment,
interface ownership and shutdown ordering.

`MdlSdrIntegration` also registers an exit handler as a final safety net. Its
handler clears cached nodes, detaches the entity resolver, releases SDK
handles, and shuts down only a self-owned instance. `Context` does not perform
SDK work from its destructor because static-destruction order across DLLs is
not reliable.

Windows and Linux are supported by the CI matrix. The CMake code contains
macOS loader/RPATH branches, but macOS is not a supported release platform
until a native build, plugin-load, Python, and shutdown job is added to CI.

For implementation changes and matching regression tests, use the
[contributor guide](../CONTRIBUTING.md#where-to-change-behavior).
