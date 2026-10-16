<!-- SPDX-License-Identifier: Apache-2.0 -->

# Environment variables

The plugin honors a small set of environment variables at runtime. They
all have safe defaults; you only need to set the ones whose default
doesn't match your setup.

## Quick setup

The install ships shell helpers that configure `PXR_PLUGINPATH_NAME` and
`PYTHONPATH`, with paths resolved relative to the helper's own location.
Configure `USD_MDL_SEARCH_PATHS` separately for your application's modules and
resources before the first MDL/Sdr operation.

POSIX shell (bash, zsh, sh):

```sh
eval "$(<prefix>/bin/usd-mdl-env.sh)"
```

Windows cmd.exe:

```bat
call <prefix>\bin\usd-mdl-env.bat
```

The `.bat` helper sets the vars in the calling cmd shell directly (it
uses plain `set "..."`); the `.sh` helper prints `export` statements
that `eval` consumes.

The helpers preserve `USD_MDL_SEARCH_PATHS` exactly, including an empty or unset
value. They prepend their Python package to `PYTHONPATH` and replace
`PXR_PLUGINPATH_NAME`. They do not configure external dependency DLL paths. If your application also loads other plugins, combine
its manifest paths explicitly. See the [complete launch example](#example).

The default initialization loads the MDL SDK by library name using the
operating system's search rules. Configure dependency library paths before
launching, or let a native host [supply an SDK library path or instance](native-integration.md#sdk-and-image-plugin-loading).
MDL content search paths below do not locate SDK binaries.

## Plugin discovery (OpenUSD)

### `PXR_PLUGINPATH_NAME`

USD's standard variable. Directory (or path-separator-delimited list of
directories) that OpenUSD scans for `plugInfo.json` files.

Set this to your install's `plugin/usd` directory so OpenUSD finds the
plugin's aggregator (`<prefix>/plugin/usd/plugInfo.json`) and from there
the per-plugin manifest (`<prefix>/plugin/usd/usdMdl/resources/plugInfo.json`).

```
PXR_PLUGINPATH_NAME=<prefix>/plugin/usd
```

Path separator: `;` on Windows, `:` elsewhere.

## MDL search paths

The currently supported workflow loads file-backed MDL modules and their content
from configured MDL search paths. Those modules and their resources must belong
to that configured content layout. Modules [embedded in the SDK](ar-resolver.md#embedded-sdk-modules-and-dummy-files),
such as `::math`, require no source file; optional dummy files follow the same
search-path setup.
Loading files outside every MDL search root is outside the current supported
scope, even though the resolver already implements some such cases.

For reusable libraries, author module paths relative to these roots using the
Sdr node's `mdl:moduleUsdIdentifier`. See
[authoring module source assets](sub-identifiers.md#authoring-module-source-assets)
for portable library references and the strict-relative convention for
modules shipped with a scene.

Set the environment before launching the application. A native host must
configure and start its MDL SDK instance before calling `UsdMdlInitialize(hostNeuray)`.
Automatic initialization can happen on the first `Sdr.Registry()` access,
shader-node lookup, or `UsdMdl.AddModuleToRegistry()` call. At initialization,
the plugin snapshots the configured MDL roots for its Ar-backed resolver;
later environment changes or SDK path additions do not update that snapshot.

### Choose the root that matches the USD reference

For a shader with the following source asset:

```usda
def Shader "Paint"
{
    uniform token info:implementationSource = "sourceAsset"
    uniform asset info:mdl:sourceAsset = @acme/paint.mdl@
    uniform token info:mdl:sourceAsset:subIdentifier = "paint"
}
```

the module's qualified MDL name is `::acme::paint`. A suitable content layout
is:

```text
/studio/materials/mdl/             <-- add this search root
    acme/
        paint.mdl                  <-- exports material paint
        textures/
            albedo.png
```

With `/studio/materials/mdl` configured, the module lookup tests
`/studio/materials/mdl/acme/paint.mdl`. A default texture declared in that
module as `./textures/albedo.png` belongs at
`/studio/materials/mdl/acme/textures/albedo.png`.

Add the directory above the first package component: adding only
`/studio/materials/mdl/acme` would make the module name `::paint`, which does
not match `acme/paint.mdl`. Roots form an ordered list; subdirectories retain
their package meaning and do not automatically become additional roots. Add
other libraries' roots as needed for imports such as `::shared::utilities`.
MDL standard modules such as `::df` and `::tex` are supplied by the compiler.

USD may also supply a layer-relative or already resolved module asset path;
the referenced module must still lie under a configured MDL root. Opening a
USD file does not add its directory to the MDL search paths.
`PXR_PLUGINPATH_NAME`, `PYTHONPATH`, and `CMAKE_PREFIX_PATH` locate plugins,
Python packages, and build dependencies respectively; they do not configure
MDL content lookup.

### `USD_MDL_SEARCH_PATHS`

Defined by this plugin. Filesystem MDL search paths separated by the
platform path separator (`;` on Windows, `:` elsewhere). Use absolute
directories and list higher-priority roots first. Entries are added in order
via `add_mdl_path()` and `add_resource_path()` during initialization. Empty
entries are ignored. The SDK validates each path; the plugin emits a warning
if adding it fails.

Default: `""` (no extra paths).

### Library ownership

Applications are responsible for supplying MDL modules, libraries and resources
in versions compatible with their authored content, including imported modules.
Use your own libraries or a matching release of the NVIDIA MDL Material Library
and configure their search roots explicitly. The package contains no MDL
modules or libraries. SDK-embedded standard modules remain available without
source files.

The generated Preview Surface USD interface is always installed, but its MDL
implementation is application-provided. See [shader definitions](shader-definitions.md#preview-surface-resource).

### Preserve application paths when using the helpers

Set `USD_MDL_SEARCH_PATHS` before invoking the helper to select your libraries.
Both helpers leave that variable unchanged, including an empty or unset value.
They do not add MDL search roots. The [effective search order](#effective-order-and-lookup)
also includes existing host roots and any user/system roots you enable.

To add a project's content ahead of existing application content, use the
following before launching the application.

POSIX shell:

```sh
export USD_MDL_SEARCH_PATHS="/studio/materials/mdl${USD_MDL_SEARCH_PATHS:+:$USD_MDL_SEARCH_PATHS}"
eval "$(/opt/usd-mdl/bin/usd-mdl-env.sh)"
```

Windows cmd.exe:

```bat
if defined USD_MDL_SEARCH_PATHS (
    set "USD_MDL_SEARCH_PATHS=D:\studio\materials\mdl;%USD_MDL_SEARCH_PATHS%"
) else (
    set "USD_MDL_SEARCH_PATHS=D:\studio\materials\mdl"
)
call C:\tools\usd-mdl\bin\usd-mdl-env.bat
```

Both examples produce `project root`, then the previous application entries.
Replace the example locations with your own installation and content
directories. Include any additional application libraries in your list in the
desired order.

A `.bat` launched from PowerShell cannot change the parent PowerShell
environment. Configure the MDL list directly there, for example:

```powershell
$mdlRoots = @(
    "D:\studio\materials\mdl"
)
if ($env:USD_MDL_SEARCH_PATHS) {
    $mdlRoots += $env:USD_MDL_SEARCH_PATHS
}
$env:USD_MDL_SEARCH_PATHS = $mdlRoots -join ';'
```

This PowerShell snippet configures only MDL roots; plugin and Python discovery
still need the variables described elsewhere on this page.

### Paths already configured on an application's Neuray

A native application can register roots using
`IMdl_configuration::add_mdl_path()` on its own `INeuray` instance. When that
same instance is passed to `UsdMdlInitialize(hostNeuray)`, the plugin retains
its existing MDL paths in their original order and appends its configured
paths. For example, within the host's initialization code:

```c++
mi::base::Handle<mi::neuraylib::IMdl_configuration> config(
    hostNeuray->get_api_component<mi::neuraylib::IMdl_configuration>());
if (!config || config->add_mdl_path("D:/studio/materials/mdl") != 0) {
    return 1;
}
config.reset();
if (!pxr::UsdMdlInitialize(hostNeuray)) {
    return 1;
}
```

Use the MDL SDK headers and `<usdMdl/initialize.h>` for this native setup.
Configure all roots before attaching usdMdl or using `Sdr`. Paths registered
on a different Neuray instance are not inherited. Python cannot inject an
`INeuray` pointer; a native host must do so before its Python code uses usdMdl,
or the process uses the plugin-owned runtime configured from the environment.

The shared root list comes from `get_mdl_path()`. Adding a directory only to
the SDK's separate resource-path list does not add it to usdMdl's Ar resolver;
MDL modules and their referenced resources use the MDL search roots.

### `USD_MDL_ADD_USER_PATHS`

Defined by this plugin. Boolean. When true, the plugin calls
`IMdl_configuration::add_mdl_user_paths()`, which adds the SDK's notion
of user MDL paths (platform-specific; typically driven by
`MDL_USER_PATH`). The paths added by the SDK are included in the resolver's
post-configuration snapshot.

Default: `false`. Set to `1` to enable.

The plugin also auto-enables this call if the legacy `MDL_USER_PATH`
environment variable is set to a non-empty value, so existing setups
that rely on `MDL_USER_PATH` keep working without touching this knob.

### `USD_MDL_ADD_SYSTEM_PATHS`

Defined by this plugin. Boolean. When true, the plugin calls
`IMdl_configuration::add_mdl_system_paths()` (analogous to
`add_mdl_user_paths()` but for system paths driven by `MDL_SYSTEM_PATH`).

Default: `false`. Auto-enabled if `MDL_SYSTEM_PATH` is set to a
non-empty value.

### `MDL_USER_PATH`, `MDL_SYSTEM_PATH`

Standard MDL SDK environment variables; honored transitively. Setting
either to a non-empty value also causes the plugin to call the
corresponding `add_mdl_user_paths()` / `add_mdl_system_paths()` SDK
function, as described above. Each variable is a list using `;` on Windows
and `:` on Linux, with entries kept in order. A nonempty value replaces the
SDK's default list for that category.

For example, these commands enable explicit user and system roots without
requiring either `USD_MDL_ADD_*` flag:

```bat
set "MDL_USER_PATH=D:\users\artist\mdl;D:\users\artist\shared-mdl"
set "MDL_SYSTEM_PATH=D:\studio\common\mdl"
```

```sh
export MDL_USER_PATH="/home/USERNAME/mdl:/home/USERNAME/shared-mdl"
export MDL_SYSTEM_PATH="/studio/common/mdl"
```

To enable the SDK's default locations instead, leave these variables unset
and set `USD_MDL_ADD_USER_PATHS=1` and/or `USD_MDL_ADD_SYSTEM_PATHS=1`.

| Category | Windows default | Linux default |
| --- | --- | --- |
| User | The user's Documents folder, followed by `mdl` | `$HOME/Documents/mdl` |
| System | `%PROGRAMDATA%\NVIDIA Corporation\mdl` | `/opt/nvidia/mdl` |

Neither category is enabled by the plugin by default. A false
`USD_MDL_ADD_USER_PATHS` or `USD_MDL_ADD_SYSTEM_PATHS` does not override a
nonempty corresponding `MDL_*_PATH`. To leave a category out, unset its
`MDL_*_PATH` variable and leave its `USD_MDL_ADD_*` flag false. This does not
remove paths the host has already registered. Set the SDK environment
variables before the host creates Neuray, because the SDK captures these
lists when constructing its configuration.

### Effective order and lookup

The following groups are combined from highest to lowest priority:

| Order | Source | When included |
| --- | --- | --- |
| 1 | Paths already registered on the supplied Neuray | A native host injects its configured instance. |
| 2 | `USD_MDL_SEARCH_PATHS`, from left to right | The variable is nonempty; helpers preserve application-selected roots. |
| 3 | MDL user paths, in SDK list order | `USD_MDL_ADD_USER_PATHS=1` or nonempty `MDL_USER_PATH`. |
| 4 | MDL system paths, in SDK list order | `USD_MDL_ADD_SYSTEM_PATHS=1` or nonempty `MDL_SYSTEM_PATH`. |

For a fresh plugin-owned runtime, group 1 is empty. If the host already added
its user or system paths before attachment, those entries are already in
group 1; their original positions are retained.

The final MDL path list is used for modules and their referenced resources
through OpenUSD `Ar`. Qualified `::acme::paint` requests try `acme/paint.mdl`
beneath those roots in order. Absolute logical resource paths such as
`/acme/textures/albedo.png` follow the same ordering. Relative MDL imports
and resource references are anchored to their owning module; once expressed
as absolute logical MDL paths, search-root precedence applies.

The first matching root wins. Conflicting content is a setup error; usdMdl
does not perform MDL's usual shadowing validation across remote roots. An
earlier root can therefore supply a texture even when its module was found in
a later root. See [shadowing behavior](ar-resolver.md#search-path-precedence-and-shadowing)
for the effect on Sdf authored and resolved paths. A lookup miss fails; the
plugin does not retry through an independent SDK filesystem resolver.

This ordering governs MDL search roots, not USD's earlier source-asset
resolution. With `ArDefaultResolver`, a matching layer-local file can override
a weak library reference such as `acme/paint.mdl` regardless of that ordering.
Avoid such collisions; see [default resolver limitations](sub-identifiers.md#default-resolver-limitations).

### If a USD-referenced module is missing

Check that the configured root plus the module's package path identifies an
existing file, and that roots for all imported application libraries are also
included. Check the launch environment, not only your interactive shell, and
configure it before the first MDL/Sdr operation. For a native host, inspect
`get_mdl_paths_length()` and `get_mdl_path(i)` on the supplied Neuray after
initialization to see the configured order. The [native example](../examples/inspect_mdl_material.cpp)
prints this list immediately after `UsdMdlInitialize()`; see [how to run it](examples.md).
Also check diagnostics for rejected roots and that the active Ar resolver can
access the content. Moving a module outside the configured roots is not a
supported workaround.

## Discovery behavior

### `USD_MDL_DISCOVER_ON_STARTUP`

Defined by this plugin. Boolean. Default: `false` (lazy loading).

We recommend leaving startup discovery disabled: MDL libraries can be large,
and scanning and loading every module increases application startup time.
With both discovery switches at their default, creating `SdrRegistry` does not initialize the MDL runtime
or scan its search paths. A shader lookup by source asset and sub-identifier
loads its module on demand; prior registration is unnecessary. The SDK also
loads that module's imports as needed.

Applications that need a node catalog should explicitly register the modules
they expose using `UsdMdlAddModuleToRegistry()` in C++ or
[`UsdMdl.AddModuleToRegistry()`](python-api.md#usdmdladdmoduletoregistryassetpath---listsdrshadernode)
in Python. For example, a material graph editor registers the modules containing
its default set of nodes when it builds its palette, and registers additional
modules as the user selects libraries. Registration adds all eligible exported
definitions from each requested module. Asset lookup alone does not populate
the discovery catalog used by Sdr enumeration.

Set `USD_MDL_DISCOVER_ON_STARTUP=1` before the first Sdr operation only when
the application needs discovery across all configured roots. This initializes
the runtime, scans the roots, and loads and preparses their modules at registry
startup. `0` / `false` / `no` / `off` explicitly select the default lazy behavior.
An explicit `UsdMdlInitialize()` still initializes the runtime immediately.

### `USD_MDL_LOAD_USD_PREVIEW_SURFACE`

Defined by this plugin. Boolean. Default: `false`.

Set to `1` / `true` before the first Sdr operation to discover the generated
standard Preview Surface interfaces with source type `mdl`. The application
must supply a compatible `UsdPreviewSurface.mdl` and its imports in the
configured MDL roots. The generated USD interface comes from the OpenUSD SDK
selected at build time; see [shader definitions](shader-definitions.md#preview-surface-resource).

This switch is independent of `USD_MDL_DISCOVER_ON_STARTUP`. Enabling it while
leaving full discovery disabled loads the canonical implementation module and
publishes its USD interfaces, without scanning all application libraries or
publishing unrelated native exports. MDL and GLSLFX entries can share a shader
identifier; request an exact source type or provide a source-type priority list.

A missing module warns and skips the MDL interfaces while preserving the
registry's other nodes. `0` / `false` select the default disabled behavior.
The explicit `AddShaderDefinitionsToRegistry` API remains available with either
setting. Configure this environment variable and MDL roots before the first
Sdr operation because the setting is cached for the process.

### Inspecting search URIs

`SdrRegistry::GetSearchURIs()` (Python: `Sdr.Registry().GetSearchURIs()`) queries
all discovery plugins. usdMdl contributes an empty list before its runtime is
initialized, without initializing it or issuing a warning. A later call after
initialization returns a snapshot of the SDK's configured MDL paths in lookup
order, with forward slashes and a trailing slash. Reading these paths does not
scan directories, load modules, or populate the node catalog.

The MDL snapshot stays fixed until shutdown; configure paths before
initialization. Sdr queries the plugins again on each call, so an early empty
response does not prevent a later call from reporting the configured paths.
Other plugins may contribute their own URIs to the returned list.

## Sub-identifier compatibility

### `USD_MDL_ALLOW_FIRST_OVERLOAD`

Defined by this plugin. Boolean. Default: `false`.

Set to `1` before the first MDL/Sdr operation to choose the first eligible
SDK overload for an ambiguous bare name. See
[ambiguous names](sub-identifiers.md#ambiguous-names-without-a-signature)
for behavior, examples, and limitations.

### `USD_MDL_PREFER_STRUCT_ELEMENTAL_CONSTRUCTOR`

Defined by this plugin. Boolean. Default: `false`.

Set to `1` together with `USD_MDL_ALLOW_FIRST_OVERLOAD=1`, before the first
MDL/Sdr operation, to prefer the field-input constructor for an ambiguous bare
struct name. See [legacy struct constructors](sub-identifiers.md#legacy-struct-constructor-references)
for behavior, examples, and limitations.

### `USD_MDL_ALLOW_MATERIAL_SIGNATURE_MISMATCH`

Defined by this plugin. Boolean. Default: `false`.

Set to `1` before the first MDL/Sdr operation to allow a material-only
simple-name fallback after a signature mismatch. See
[legacy material signatures](sub-identifiers.md#legacy-material-signatures)
for behavior, examples, and limitations.

All compatibility options are temporary and **may be removed at any time**.
Author canonical `mdl:subIdentifier` values for lasting compatibility.

## Diagnostics

### `TF_DEBUG`

OpenUSD's debug-symbol setting. MDL execution-context info, verbose and debug
messages are quiet by default. Set `TF_DEBUG=USD_MDL_DIAGNOSTICS` before loading
a module to print these messages. Warning, error and fatal context messages are
forwarded as USD warnings without enabling debug output. This does not change
SDK log levels or load results. See [diagnostics](diagnostics.md).

`USD_MDL` remains available for the plugin's existing conversion and discovery
traces. Separate multiple OpenUSD debug symbols with spaces.

## Python

### `PYTHONPATH`

Standard Python variable. Must include both:

- `<prefix>/lib/python` so `import UsdMdl` resolves the top-level
  `UsdMdl` package this plugin ships.
- OpenUSD's `lib/python` (or wherever your `pxr` package lives) so
  `from pxr import Sdf` etc. work.

Path separator: `;` on Windows, `:` elsewhere.

`UsdMdl` is **not** part of the `pxr` namespace — see
[python-api.md](python-api.md) for the rationale.

## Library discovery (OS-level)

### `PATH` (Windows) / `LD_LIBRARY_PATH` (Linux) / `DYLD_FALLBACK_LIBRARY_PATH` (macOS)

Standard OS variables. On Windows, put the directories containing the OpenUSD,
MDL SDK, Python, TBB, MaterialX, and Boost DLLs needed by your installation on
`PATH`, typically their `bin/` and `lib/` directories.

On Linux, installed RPATH entries help locate the plugin's linked dependencies,
including OpenUSD at its build-time location. The MDL SDK is loaded dynamically;
those entries do not guarantee that `libmdl_sdk.so` can be found. Set
`LD_LIBRARY_PATH` to include the SDK library directory and any dependency
directories not covered by your loader configuration, before launching the
application. The examples below configure both OpenUSD and MDL SDK locations.

See [SDK and image-plugin loading](native-integration.md#sdk-and-image-plugin-loading)
for explicit SDK selection and the required adjacent image plugins.
`CMAKE_PREFIX_PATH` selects build dependencies; it does not configure runtime
SDK lookup.

## Example

cmd.exe, plugin installed at `C:\src\repos\usd-mdl\install`, OpenUSD at
`C:\src\repos\OpenUSD\install`, MDL SDK at `C:\src\repos\MDL-SDK\install`.
Use the helper script to set the plugin-specific variables, then prepend
OpenUSD/MDL SDK to `PATH` and `PYTHONPATH` yourself:

```bat
call C:\src\repos\usd-mdl\install\bin\usd-mdl-env.bat
set "PYTHONPATH=C:\src\repos\OpenUSD\install\lib\python;%PYTHONPATH%"
set "PATH=C:\src\repos\OpenUSD\install\bin;C:\src\repos\OpenUSD\install\lib;C:\src\repos\MDL-SDK\install\bin;%PATH%"

python -c "import UsdMdl; print(UsdMdl.AddModuleToRegistry)"
```

Bash on Linux: set `PREFIX` to the staged usdMdl package, `OPENUSD` to the
OpenUSD installation, and `MDL_SDK_LIB_DIR` to the directory containing
`libmdl_sdk.so`. Replace these example paths with your local locations.

```sh
PREFIX=/path/to/usd-mdl/build/package
OPENUSD=/path/to/OpenUSD/install
MDL_SDK_LIB_DIR=/path/to/MDL-SDK/install/lib

eval "$("$PREFIX/bin/usd-mdl-env.sh")"
export PYTHONPATH="$OPENUSD/lib/python${PYTHONPATH:+:$PYTHONPATH}"
export LD_LIBRARY_PATH="$MDL_SDK_LIB_DIR:$OPENUSD/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
python -c "import UsdMdl; print(UsdMdl.AddModuleToRegistry)"
```

Alternatively, with those same three paths defined, configure the environment
without the helper and select your application's MDL content:

```sh
export PXR_PLUGINPATH_NAME="$PREFIX/plugin/usd"
export USD_MDL_SEARCH_PATHS="/studio/materials/mdl"
export PYTHONPATH="$PREFIX/lib/python:$OPENUSD/lib/python${PYTHONPATH:+:$PYTHONPATH}"
export LD_LIBRARY_PATH="$MDL_SDK_LIB_DIR:$OPENUSD/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
python -c "import UsdMdl; print(UsdMdl.AddModuleToRegistry)"
```
