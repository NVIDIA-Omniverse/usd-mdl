<!-- SPDX-License-Identifier: Apache-2.0 -->

# usdMdl


`usdMdl` is an OpenUSD `Sdr` discovery and parser plugin for NVIDIA MDL. It
exposes MDL functions and materials as shader nodes, resolves modules and
resources through OpenUSD `Ar`, and provides C++ and Python APIs for on-demand
registration. Loading is lazy by default; applications can register selected
modules for a node catalog or opt in to startup discovery.

Applications can inspect parameter types, defaults, and annotations through
the standard `Sdr` API to build material graph editors, populate node catalogs,
and inspect MDL materials referenced by USD scenes.

Applications supply [MDL modules, libraries and resources matching their
content](docs/environment-variables.md#library-ownership) and configure their
search roots. The package does not include MDL libraries.

Public alpha releases use `0.x` versions. C++, Python, CMake, metadata,
install-layout and behavior interfaces may change before `1.0`.
See the [changelog](CHANGELOG.md) for scope and limitations.

## Build and run

`usdMdl` is distributed as source; prebuilt binaries are not provided.

Install compatible OpenUSD and MDL SDK packages following the
[build guide](docs/build.md), then configure both dependency prefixes:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="<OpenUSD-install>;<MDL-SDK-install>" -DCMAKE_INSTALL_PREFIX="$PWD/build/package"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure --no-tests=error
```

To use the plugin in an application, [stage a private dependency package](docs/build.md#stage-a-package-for-another-project),
then activate the [runtime environment](docs/environment-variables.md#quick-setup)
and configure your content roots before first MDL/Sdr use. Register a module:

```python
import UsdMdl

try:
    for node in UsdMdl.AddModuleToRegistry("my_module.mdl"):
        if node.IsValid():
            print(node.GetIdentifier())
finally:
    UsdMdl.Shutdown()
```

For a complete scene, run the [Python or C++ material example](docs/examples.md).
Native applications use [the CMake target and lifecycle API](docs/native-integration.md).

## Documentation

Use the [documentation index](docs/README.md) to find setup, authoring, API,
type-mapping and resolver references. For implementation work, start with
[CONTRIBUTING.md](CONTRIBUTING.md).

## Security

Report potential security vulnerabilities privately through the channels in the
[NVIDIA security policy](https://github.com/NVIDIA-Omniverse/.github/blob/main/SECURITY.md).
Do not report vulnerabilities through public GitHub issues or pull requests.

## License

The source code, test scripts and original MDL modules are licensed under
[Apache-2.0](LICENSE). Test and example resource data are covered by
[CC0-1.0](LICENSE_RESOURCES.md). The generated OpenUSD shader definitions retain
the license recorded in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
External dependencies retain their respective licenses.
