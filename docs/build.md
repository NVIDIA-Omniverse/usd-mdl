<!-- SPDX-License-Identifier: Apache-2.0 -->

# Building usdMdl

`usdMdl` is a CMake C++17 project. It consumes installed OpenUSD and MDL SDK
CMake packages; neither dependency is vendored by this source tree.
These packages can live in project-local dependency directories.

For alpha development, build and test directly from the build tree. When
another project needs the exported CMake package, [stage it in a private
directory](#stage-a-package-for-another-project). No system-wide installation
is required.

## Prerequisites

| Dependency | Version | Notes |
| --- | --- | --- |
| CMake | 3.20+ | Configure, build, install, and run CTest |
| C++ compiler | MSVC 19.30+, GCC 11+, or Clang 13+ | Use the same ABI/toolchain family as OpenUSD |
| Python | 3.10+ | Interpreter and development headers |
| OpenUSD | 25.11+ | 25.08 lacks a required Sdf header |
| MDL SDK | 2025.0.5, 2026.0.0, and 2026.0.2 | Either build from source or grab a prebuilt release. All listed versions are covered by CI. |

Use the upstream instructions for the dependencies:

- [OpenUSD build instructions](https://github.com/PixarAnimationStudios/OpenUSD/blob/dev/BUILDING.md), including `build_usd.py`
- [MDL SDK source-build instructions](https://github.com/NVIDIA/MDL-SDK/blob/master/INSTALL.md)
- [MDL SDK prebuilt releases](https://github.com/NVIDIA/MDL-SDK/releases)

### MDL SDK 2026.0.2 runtime requirement

The published MDL SDK 2026.0.2 Windows runtime imports `nvcuda.dll` and needs
an installed NVIDIA driver even for CPU-only plugin use. This binary dependency
does not require a CUDA toolkit installation. A missing dependency can appear
as a failure to load `libmdl_sdk.dll` even though that file exists. Keep the SDK
headers, runtime, and image plugins from a matching release.

For a driver-free SDK, build the official 2026.0.2 source with
`-DMDL_ENABLE_GPU_BAKER=OFF`. This disables SDK GPU baking, which usdMdl does
not use. The [release notes](https://github.com/NVIDIA/MDL-SDK/releases/tag/2026.0.2)
describe the GPU-baker option and the fix for resources occurring inside arrays.

## Build the dependencies

For OpenUSD, the upstream script is the simplest supported source build on
Windows and Linux:

```sh
python <OpenUSD-source>/build_scripts/build_usd.py <OpenUSD-install>
```

The script downloads and builds OpenUSD's selected dependencies into the
given install prefix. Pass the same Python version and compiler family that
will build `usdMdl`.

For the MDL SDK, either follow its CMake/vcpkg source-build guide above or
extract a matching prebuilt release. The Linux CI-style prebuilt artifact is:

```sh
curl -fL -o mdl-sdk.tgz \
  https://github.com/NVIDIA/MDL-SDK/releases/download/2026.0.2/MDL-SDK-2026.0.2-391700.2276-linux-x86-64.tgz
mkdir -p <MDL-SDK-install>
tar -xzf mdl-sdk.tgz -C <MDL-SDK-install> --strip-components=1
```

## Configure and build

`CMAKE_PREFIX_PATH` must contain both install prefixes. CMake searches it for
`pxrConfig.cmake`/OpenUSD imported targets and `mdl-config.cmake`/MDL SDK
imported targets used by `find_package()`.

### Windows

Run from a Visual Studio 2022 developer shell:

```bat
cmake -S . -B build ^
  -DCMAKE_PREFIX_PATH="C:/sdk/OpenUSD;C:/sdk/MDL-SDK" ^
  -DCMAKE_INSTALL_PREFIX="%CD%/build/package"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure --no-tests=error
```

The target uses `/Zc:inline-` to match OpenUSD's MSVC registry behavior.

### Linux

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="<OpenUSD-install>;<MDL-SDK-install>" \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/package"
cmake --build build
ctest --test-dir build --output-on-failure --no-tests=error
```

This source-build route works with OpenUSD produced by `build_usd.py` and does
not require conda. If using a prebuilt OpenUSD distribution, compile `usdMdl`
with an ABI-compatible compiler and C++ runtime.

macOS is not a supported release platform. Some loader and RPATH branches are
present, but no macOS CI job currently validates build, plugin discovery,
Python loading, or shutdown.

## Build options

| Option | Default | Purpose |
| --- | --- | --- |
| `CMAKE_BUILD_TYPE` | generator-specific | Single-config build type |
| `CMAKE_PREFIX_PATH` | empty | OpenUSD and MDL SDK package roots |
| `CMAKE_INSTALL_PREFIX` | platform default | Package staging directory; the examples explicitly use `build/package` |
| `BUILD_TESTING` | `ON` | Build and register C++/Python tests and examples |
| `USDMDL_USD_SHADER_DEFINITIONS` | SDK resource lookup | Canonical `usdShaders/shaders/shaderDefs.usda` from the selected OpenUSD distribution |
| `USDMDL_USD_SHADER_DEFINITIONS_LICENSE` | Bundled OpenUSD license | Matching license copied beside the generated definitions |
| `USDMDL_USD_SHADER_DEFINITIONS_NOTICE` | Bundled OpenUSD NOTICE | Matching attribution notice copied beside the generated definitions |

## Generated Preview Surface definitions

The build generates `build/src/resources/UsdPreviewSurface.usda` from the
canonical `usdShaders` resource of the OpenUSD SDK selected by `find_package(pxr)`.
It searches that SDK's `plugin/usd` and `lib/usd` layouts, and requires its
Python bindings to work with the configured build interpreter. Configuration
checks their reported USD version against the CMake package version. A missing
resource or incompatible host Python binding produces a configuration error;
a different ambient `pxr` installation cannot supply the generator's APIs.

If a distribution does not install the canonical resource, supply its matching
source file explicitly:

```sh
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="<OpenUSD-install>;<MDL-SDK-install>" \
  -DUSDMDL_USD_SHADER_DEFINITIONS="<matching-OpenUSD-source>/pxr/usd/plugin/usdShaders/shaders/shaderDefs.usda"
```

For distributions with different legal text, set
`USDMDL_USD_SHADER_DEFINITIONS_LICENSE` and
`USDMDL_USD_SHADER_DEFINITIONS_NOTICE` to the matching license and attribution
notice. Both files must contain non-whitespace UTF-8 text (an optional UTF-8
BOM is accepted). The build copies them unchanged and records their hashes; it
does not verify their legal applicability.
Build maintainers must review them against the selected distribution and any
custom canonical input. See [third-party notices](../THIRD_PARTY_NOTICES.md)
for the bundled files' upstream sources and reviewed versions.

Generation preserves canonical inheritance and interface semantics, adding
only the MDL implementation bindings and missing symbolic token options.
Unknown shader families, missing required mapped ports and changed mapped
port types fail generation. New canonical shader versions retain their USD
identifiers and bind to the corresponding MDL family function. Optional
families absent from the selected SDK are reported in the provenance manifest.
The canonical layer must be self-contained: external composition dependencies
and nonempty relative asset defaults require an explicit packaging policy and
are rejected. Existing hierarchical relative MDL source assets, such as
`application/preview.mdl`, are rejected for the same reason: copying the layer
would change their anchor. Windows drive-relative paths such as `C:preview.mdl`
are also rejected. Bare module names, absolute paths, resolver URIs
and compatible exact sub-identifiers are preserved; conflicting function
names fail generation.

The build also produces `UsdPreviewSurface.provenance.json` and copies the
license and `OpenUSD-NOTICE.txt`. USDA source comments are retained in the
generated header, alongside the modification notice. The provenance records
the USD version, canonical source path and SHA-256 hashes, including
`licenseSha256` and `noticeSha256`. These resources are always installed.
Dependencies on the selected SDK settings, source, generator, license and
notice regenerate the resources when those inputs change.
Repeated standalone generation with identical inputs preserves file contents
and modification times.

## Tests

With `BUILD_TESTING=ON`, run the suite from the build tree. The test wrapper
stages fixtures and sets the plugin, Python, OpenUSD, and MDL SDK paths;
installation is not required before CTest.

```sh
ctest --test-dir build -C Release --output-on-failure --no-tests=error
```

See the [test guide](../tests/README.md) for fixture layout, coverage, and CI
package checks.

## Stage a package for another project

The configure commands above select `<checkout>/build/package` as a private
package directory. Setting `CMAKE_INSTALL_PREFIX` does not copy any files.
When ready to consume the plugin from another project, populate that directory:

```sh
cmake --install build --config Release
```

This assembles the library, public headers, Python module, plugin manifests,
generated shader definitions, license notices, helpers and CMake package under
the selected prefix. It needs no elevated permissions for a directory you own.
You can instead configure an absolute
prefix inside the consuming project's dependency directory.

The package includes no MDL modules or libraries. Applications supply their own
[MDL libraries](environment-variables.md#library-ownership) and configure their
search roots. Stage into a fresh directory when upgrading an older package;
installation does not remove files left by previous releases.

Add that package directory alongside OpenUSD and the MDL SDK in the consuming
project's `CMAKE_PREFIX_PATH`; see [CMake integration](native-integration.md#cmake-integration).
The exported `usdMdl::usdMdl` target currently comes from this staged package.

## Installed environment

Here `<prefix>` is the private package directory selected above.

```sh
eval "$(<prefix>/bin/usd-mdl-env.sh)"
```

```bat
call <prefix>\bin\usd-mdl-env.bat
```

See [runtime environment](environment-variables.md) for each path and opt-in.

## Troubleshooting

### CMake cannot find `pxr` or `mdl`

Confirm that `CMAKE_PREFIX_PATH` contains the install roots, not the source or
build directories. The MDL package file is normally under
`<MDL-SDK-install>/share/mdl/`; OpenUSD's package location depends on its
install configuration.

### Plugin discovery returns no MDL nodes

Check that `PXR_PLUGINPATH_NAME` contains `<usdMdl-install>/plugin/usd`, the
aggregator `plugInfo.json` exists there, and at least one concrete MDL root is
in `USD_MDL_SEARCH_PATHS`. Use `TF_DEBUG=PLUG_REGISTRATION` for OpenUSD plugin
diagnostics.

### Linux reports a missing `CXXABI` symbol

The OpenUSD binary and `usdMdl` were built against incompatible
`libstdc++` versions. This is common when a conda-forge OpenUSD is linked by a
system compiler. Build OpenUSD from source with the same compiler, or compile
`usdMdl` inside the distribution's toolchain environment.

### Python cannot import `UsdMdl`

Add `<usdMdl-install>/lib/python` to `PYTHONPATH` and ensure the plugin shared
library plus OpenUSD/MDL SDK dependencies are visible to the platform loader.
The installed environment helper configures these paths.
