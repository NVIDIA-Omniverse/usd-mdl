<!-- SPDX-License-Identifier: Apache-2.0 -->

# Changelog

Notable changes to usdMdl are recorded here, using the
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/) format.

## [0.1.0] - 2026-10-15

First public alpha, distributed as source. C++, Python, CMake, metadata,
installation layout and behavior interfaces may change before 1.0.
Applications should pin the plugin and its dependencies together.

### Added

- OpenUSD Sdr discovery and parsing of NVIDIA MDL functions and materials,
  with on-demand lookup, explicit registration and optional startup discovery.
  See the [Python API](docs/python-api.md).
- Ar-based module and resource resolution, including SDK-embedded modules,
  with separate logical module identifiers and resolved asset URIs.
  See [asset resolution](docs/ar-resolver.md).
- MDL type mapping, constant defaults, array shape metadata and expression-default
  information. See [types and defaults](docs/type-mapping.md).
- Annotation metadata and UI hints, with explicit `SDR__` annotations for Sdr
  fields and ordered records preserving full annotation arguments.
  See [annotation mapping](docs/metadata.md).
- USD-authored shader interfaces with renamed ports and authored defaults,
  plus generated Preview Surface definitions with opt-in MDL interface loading.
  See [shader definitions](docs/shader-definitions.md).
- Array constructor specializations, including struct arrays and USD-authored
  interfaces. See [array constructors](docs/sub-identifiers.md#builtin-array-constructors).
- C++ and Python APIs, local [MDL connection checks](docs/type-mapping.md#client-side-mdl-connection-checks),
  the `usdMdl::usdMdl` CMake target and runtime ownership controls.
  See [native integration](docs/native-integration.md).
- MDL SDK warnings and structured diagnostics through USD, with optional debug
  output. See [diagnostics](docs/diagnostics.md).
- [Python and C++ examples](docs/examples.md), [authoring guides](docs/README.md)
  and Linux/Windows CI coverage for OpenUSD 25.11, 26.03 and 26.05 with MDL SDK
  2025.0.5, 2026.0.0 and 2026.0.2. See [build requirements](docs/build.md#prerequisites).

### Limitations

- No prebuilt plugin binaries or bundled MDL libraries; applications supply
  [libraries matching their content](docs/environment-variables.md#library-ownership).
  The MDL SDK 2026.0.2 Windows runtime requires an NVIDIA driver; see the
  [source-build alternative](docs/build.md#mdl-sdk-202602-runtime-requirement).
- Kit/OVRTX integration is separate; no adapter is supplied. Sdr lookup alone
  does not establish renderer evaluation, literal-array application or live-edit
  support. See [renderer responsibilities](docs/sub-identifiers.md#empty-arrays-and-renderer-integration).
- Portable libraries require compatible MDL search roots. Off-search-path modules
  are experimental; USDZ-contained MDL and MDLE containers are unsupported.
  Failed resource lookups may appear as empty asset defaults.
  See [resolver limitations](docs/ar-resolver.md#limitations) and [diagnostics](docs/diagnostics.md).
- Expression defaults are not evaluated. Empty arrays use typed constants;
  zero-size constructors and literal nonempty struct constants are unsupported.
  Array indexing, length, conditional and cast template specializations are
  outside this release. See [types and defaults](docs/type-mapping.md).
