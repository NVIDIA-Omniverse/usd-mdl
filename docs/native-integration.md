<!-- SPDX-License-Identifier: Apache-2.0 -->

# Native application integration

Native applications can link `usdMdl` to register modules and control the MDL
runtime through its C++ API. This guide covers MDL port connection checks, the
CMake package, initialization, SDK ownership, and shutdown.

Applications that use only OpenUSD plugin discovery do not need to link
`usdMdl`. They need the runtime installation's `plugin/usd` directory on
`PXR_PLUGINPATH_NAME` and the runtime libraries on the operating system's
library search path. The plugin can be installed beside OpenUSD and the MDL SDK;
it does not need to overlay either installation.

## Checking MDL port connections

The public `UsdMdlCanConnectTo` helper compares two `SdrShaderProperty`
declarations without initializing or calling the MDL SDK. Pass registry-owned
properties from the resolved nodes:

```c++
#include <usdMdl/canConnectTo.h>
#include <pxr/usd/sdr/shaderNode.h>

const bool canConnect = pxr::UsdMdlCanConnectTo(
    downstreamNode->GetShaderInput(pxr::TfToken("param")),
    upstreamNode->GetShaderOutput(pxr::TfToken("out")));
```

Either argument order is accepted. Null arguments, ports with the same
direction, disabled inputs and missing native type metadata return `false`.
This is a local structural check of MDL families, shapes, named types, array
lengths and explicit modifiers. See [connection checks](type-mapping.md#client-side-mdl-connection-checks)
for fixed/deferred array behavior and the graph-wide validation still needed.
Python exposes the same function as `UsdMdl.CanConnectTo`.

## CMake integration

[Stage a package](build.md#stage-a-package-for-another-project) in a private
directory such as `<usdMdl-checkout>/build/package` or your application's
dependency directory. This requires no system-wide installation. The staged
package exports the target `usdMdl::usdMdl`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_app LANGUAGES CXX)

find_package(usdMdl REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE usdMdl::usdMdl)
```

Configure with the OpenUSD, MDL SDK, and `usdMdl` install prefixes:

```sh
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="<OpenUSD-install>;<MDL-SDK-install>;<usdMdl-install>"
```

`CMAKE_PREFIX_PATH` is a list of package-install roots searched by
`find_package()`. All three entries are required because
`usdMdlConfig.cmake` finds its OpenUSD (`pxr`) and MDL SDK (`mdl`)
dependencies before importing `usdMdl::usdMdl`. On Windows and in a CMake
cache value, separate entries with semicolons; in a POSIX environment
variable, use the platform list separator.

### Package outputs

`find_package(usdMdl)` provides `usdMdl::usdMdl`, the shared library target
and its public include path.

It also exports installation metadata for configure-time checks and tests
against the installation found by CMake, such as our CI package validation:

- `usdMdl_PLUGIN_DIR`: absolute path to that installation's `plugin/usd`.

These absolute paths identify files on the build machine. Do not compile them
into a distributed DLL, shared library, or application through compile
definitions, generated headers, or other embedded defaults. Those locations
need not exist on the user's system. Keep their use to local installation
checks and tests; do not put them into shipped launchers either.

For interactive shells, use `<prefix>/bin/usd-mdl-env.sh` or
`<prefix>/bin/usd-mdl-env.bat`; see
[Environment variables](environment-variables.md).

For deployed applications, configure search paths from the actual runtime
installation and application content layout. The environment helpers locate
their installation relative to the script when invoked. Configure roots for
the modules referenced in your USD and their dependencies before the first
MDL/Sdr operation. `find_package(usdMdl)` does not change the process environment
or an application's Neuray configuration. The [search-path guide](environment-variables.md#mdl-search-paths)
explains how to retain application paths, enable user/system paths, and order
them. The environment helpers leave `USD_MDL_SEARCH_PATHS` unchanged, including
empty and unset values. Applications supply their MDL modules and libraries;
the package exports no MDL content paths.

On Windows, a directly linked C++ application also needs `<prefix>/plugin/usd`
on `PATH` before launch so the OS can find `usdMdl.dll`. The environment helper
sets USD plugin discovery and Python paths; it does not set the OS library path.
Include the dependency DLL directories described in
[library discovery](environment-variables.md#library-discovery-os-level) as well.

## C++ API

Register one module and inspect the returned nodes:

```c++
#include <usdMdl/addModuleToRegistry.h>
#include <usdMdl/initialize.h>
#include <pxr/usd/sdr/shaderNode.h>

#include <pxr/usd/sdf/assetPath.h>

#include <iostream>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "Usage: my_app <module.mdl>\n";
        return 1;
    }
    const std::vector<pxr::SdrShaderNodeConstPtr> nodes =
        pxr::UsdMdlAddModuleToRegistry(pxr::SdfAssetPath(argv[1]));
    bool foundValidNode = false;
    for (const auto& node : nodes) {
        if (node && node->IsValid()) {
            foundValidNode = true;
            std::cout << node->GetIdentifier() << '\n';
        }
    }
    pxr::UsdMdlShutdown();
    return foundValidNode ? 0 : 1;
}
```

### Register an authored USD interface

For ordinary MDL, `UsdMdlAddModuleToRegistry()` exposes the compiled MDL interface.
For a standard USD interface backed by MDL, include
`<usdMdl/shaderDefinitions.h>` and register its USD definition asset:

```c++
const auto nodes = pxr::UsdMdlAddShaderDefinitionsToRegistry(
    pxr::SdfAssetPath(definitionsFilename),
    pxr::SdfAssetPath("UsdPreviewSurface.mdl"));
```

The optional second asset selects one application-owned MDL module for all
Shader definitions in the asset. Their individual sub-identifiers still select
its functions and materials. Exposed USD properties come from the definition
asset; `GetImplementationName()` on nodes and properties retains the compiled
MDL binding. The [shader-definition guide](shader-definitions.md) describes the
installed Preview Surface resource and material terminal aliases.

Renderers must construct compatible SDK argument values. For a USD token backed
by an MDL enum, `<usdMdl/enumValue.h>` provides `UsdMdlCreateEnumValue()` using the
caller's live value factory and original enum type. It accepts exact token/string
member names or native integer codes and returns one retained `IValue_enum`
reference; unknown or unsupported values return null with a diagnostic. Adopt
the result into an SDK handle and create the argument expression in the caller's
transaction. Registration does not replace renderer graph construction.

### Attach a host runtime

Applications that already own an MDL SDK instance must configure and start it,
then attach it before using `Sdr` or `usdMdl`. Detach before the host shuts it down:

```c++
#include <usdMdl/initialize.h>

mi::neuraylib::INeuray* neuray = GetHostNeuray();
if (!pxr::UsdMdlInitialize(neuray)) {
    return 1;
}

// Use Sdr or pxr::UsdMdlAddModuleToRegistry().

pxr::UsdMdlShutdown();
```

`UsdMdlInitialize()` without an argument selects a plugin-owned instance.
An attached host instance is retained but never shut down by `usdMdl`.
It must be in `STARTED` state; unstarted instances are rejected without
configuration changes. The host is responsible for loading its image plugins
before starting the SDK.
`UsdMdlShutdown()` releases all plugin state; call it while the MDL SDK is
still valid.

## Host configuration ownership

Attaching installs usdMdl's Ar entity resolver and adds the configured MDL
and resource search paths to the host instance. `UsdMdlShutdown()` clears
the installed resolver and leaves the search paths in place. It does not
restore the host's previous resolver or search-path configuration. Attachment
failure after configuration changes likewise does not restore that state.

If restoration is needed, the host must retain its previous custom resolver
and save its search-path configuration before attaching, then restore them
after detaching or a failed attachment. Retain an owning SDK interface reference
to the custom resolver so it remains valid when replaced. See
[resolver access](ar-resolver.md) for SDK getter availability.

## SDK and image-plugin loading

By default, plugin-owned initialization loads `libmdl_sdk.dll` on Windows or
`libmdl_sdk.so` on Linux by name, using the operating system's library search
rules. This includes `PATH` on Windows and `LD_LIBRARY_PATH`/applicable RPATH
or RUNPATH entries on Linux. Automatic initialization uses the same behavior;
the SDK location from the plugin's build is not embedded as a runtime default.

A native application can choose the SDK library explicitly before the first
MDL/Sdr operation:

```c++
if (!pxr::UsdMdlInitialize("D:/MyApp/mdl/libmdl_sdk.dll")) {
    return 1;
}
```

The path argument names the library file, not its installation directory.
A null or empty path selects the default library name. An explicit path is
used without fallback; its dependencies must still be available to the
platform loader. The path is used only for the first initialization and does
not replace an active runtime. The three overloads are `UsdMdlInitialize()`,
`UsdMdlInitialize(const char*)`, and `UsdMdlInitialize(INeuray*)`.
Use the no-argument call for default loading; a bare `nullptr` is ambiguous
between the two pointer overloads.

For an SDK loaded by the plugin, the `nv_openimageio` plugin and `dds` plugin
must be beside the actual loaded SDK library. usdMdl loads them only from that
directory; it does not search for them by name. A missing or unloadable image
plugin produces a warning; initialization can still succeed, but decoding
resources that require that plugin is unavailable.

For a host-owned Neuray instance, the application chooses how and where to load
the SDK and its image plugins. usdMdl uses the already started instance without
loading image plugins or imposing an adjacent-plugin layout.

## Diagnostics and SDK loggers

Module-load warnings and errors are reported through USD `Tf` diagnostics,
including warnings from successful loads. A native `TfDiagnosticMgr::Delegate`
can read the original SDK severity, kind, code, module name and recursive notes
from each forwarded warning's `VtDictionary` payload. Resolver rejection
warnings carry separate details about the authored path, owner, source position
and reason. See [diagnostics](diagnostics.md) for both payloads and debug-output
controls.

usdMdl leaves the receiving logger on a host-owned SDK instance unchanged. A host
can use `ILogging_configuration::set_receiving_logger()` for other SDK activity;
consuming both loggers can produce duplicate reports of one problem.

## Runtime access and shutdown

`UsdMdlGetNeuray()` and `UsdMdlGetEntityResolver()` return the active SDK instance
and Ar-backed resolver respectively. Each non-null result carries one retained
reference owned by the caller, following the MDL SDK convention. With the MDL
SDK headers (`<mi/mdl_sdk.h>`) and `<usdMdl/initialize.h>` included, adopt it into
a `mi::base::Handle` without an additional retain:

```c++
{
    mi::base::Handle<mi::neuraylib::INeuray> neuray(pxr::UsdMdlGetNeuray());
    mi::base::Handle<mi::neuraylib::IMdl_entity_resolver> resolver(
        pxr::UsdMdlGetEntityResolver());
    // Use these interfaces while the runtime is initialized.
}
pxr::UsdMdlShutdown();
```

Both queries return null before initialization and after shutdown; neither
initializes the runtime. The [C++ material example](../examples/inspect_mdl_material.cpp)
uses Neuray to access a definition by its `mdl:mdl_db_definition_name` Sdr metadata.
Release the returned references, all derived SDK handles, and transactions before
`UsdMdlShutdown()`, and serialize their use with runtime lifecycle calls.
Interface references do not keep the SDK running or its library loaded, and
do not transfer responsibility for shutting down the instance.

