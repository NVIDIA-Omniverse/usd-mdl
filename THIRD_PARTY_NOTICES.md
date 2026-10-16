<!-- SPDX-License-Identifier: Apache-2.0 -->

# Third-Party Notices

usdMdl builds generate a shader-definition resource derived from OpenUSD.
Installed packages redistribute it under the license stated below. The
source-only distribution includes the generator and accompanying legal text;
it does not contain the generated resource.
The `usdMdl` project code is licensed separately under Apache-2.0.

## OpenUSD shader definitions

Installed paths:
`plugin/usd/usdMdl/resources/UsdPreviewSurface.usda`,
`plugin/usd/usdMdl/resources/UsdPreviewSurface.provenance.json`,
`plugin/usd/usdMdl/resources/OpenUSD-LICENSE.txt`, and
`plugin/usd/usdMdl/resources/OpenUSD-NOTICE.txt`.

The build generates the MDL shader-definition asset from the canonical
`usdShaders` definitions of the OpenUSD SDK selected by CMake, adding MDL
source-asset and port binding metadata. The generated file identifies these
modifications, retains source comments and refers to the accompanying license
and attribution notice. The provenance records the selected USD version and
source, generator, output, license and notice hashes.

The bundled legal text comes from these upstream files:

- [OpenUSD-LICENSE.txt](src/usdMdl/resources/OpenUSD-LICENSE.txt) contains the
  [OpenUSD 25.11 LICENSE.txt](https://github.com/PixarAnimationStudios/OpenUSD/blob/v25.11/LICENSE.txt),
  including the Tomorrow Open Source Technology License 1.0 and upstream
  third-party notices.
- [OpenUSD-NOTICE.txt](src/usdMdl/resources/OpenUSD-NOTICE.txt) contains the
  [OpenUSD 26.05 NOTICE.txt](https://github.com/PixarAnimationStudios/OpenUSD/blob/v26.05/NOTICE.txt),
  with whitespace normalized. Its attribution text is identical in the reviewed
  25.11, 26.03, 26.05 and 26.08 releases.

The OpenUSD license terms are unchanged across those reviewed releases. The
26.03, 26.05 and 26.08 combined license files add PBRT sampling and Cycles IES
reader notices; the shader-definition resource does not incorporate those
implementations. This review covers the generated definitions, not redistribution
of an entire OpenUSD SDK or its dependencies.

A build can supply matching license and notice files for its selected
distribution. Both are copied unchanged beside the generated asset and their
hashes are recorded. Empty files are rejected; hashes identify the inputs but
do not establish legal applicability. Build maintainers must review the legal
text for their supplied SDK distribution and any custom shader definitions.
See [build configuration](docs/build.md#generated-preview-surface-definitions)
for the override options.

The portable MDL modules under `tests/` are original Apache-2.0 test fixtures;
they do not redistribute Kit's Preview Surface implementation.
