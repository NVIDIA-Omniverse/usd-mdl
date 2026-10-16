<!-- SPDX-License-Identifier: Apache-2.0 -->

# Inspecting a bound MDL material

Activate the [installed environment](environment-variables.md#quick-setup),
then add the repository's `examples/mdl` root before using MDL/Sdr:

```sh
export USD_MDL_SEARCH_PATHS="$PWD/examples/mdl${USD_MDL_SEARCH_PATHS:+:$USD_MDL_SEARCH_PATHS}"
python examples/inspect_mdl_material.py examples/mdl_bound_material.usda /World/Sphere
```

From Windows cmd.exe:

```bat
set "USD_MDL_SEARCH_PATHS=%CD%\examples\mdl;%USD_MDL_SEARCH_PATHS%"
python examples\inspect_mdl_material.py examples\mdl_bound_material.usda /World/Sphere
```

The C++ executable is built when `BUILD_TESTING=ON`. Run
`<build>/src/Release/inspectMdlMaterial.exe` on Windows or
`<build>/src/inspectMdlMaterial` on Linux with the same stage and prim-path
arguments. CTest supplies the build-tree environment automatically.

The maintained examples are:

- [`examples/inspect_mdl_material.cpp`](../examples/inspect_mdl_material.cpp)
- [`examples/inspect_mdl_material.py`](../examples/inspect_mdl_material.py)
- [`examples/mdl_bound_material.usda`](../examples/mdl_bound_material.usda)

Both programs open the USD stage, find the material bound to `/World/Sphere`,
read the MDL source asset and sub-identifier, register the module with
`UsdMdlAddModuleToRegistry()` or `UsdMdl.AddModuleToRegistry()`, and print its
inputs, defaults, annotations, and texture paths.

The C++ example queries the active instance with `UsdMdlGetNeuray()` after
registration, opens an SDK transaction, and accesses `IModule` and
`IFunction_definition` using the node's `mdl:mdl_db_module_name` and
`mdl:mdl_db_definition_name` metadata. It checks for success and prints their
database names, the module's qualified MDL name, and the definition's simple
name. The example uses usdMdl's runtime initialization and releases its SDK
handles before `UsdMdlShutdown()`.

A comment describes the host-owned case: call `UsdMdlInitialize(appNeuray)`
before any `Sdr` use and shut down the host instance after `UsdMdlShutdown()`.
The getter returns that same injected instance when one is supplied.

The Python binding does not expose `INeuray` pointers. The standalone Python
example uses the plugin-owned runtime and calls `UsdMdl.Shutdown()` after its
last MDL/Sdr operation. Python embedded in a native host uses whichever
instance that host attached before invoking the plugin.
