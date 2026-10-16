<!-- SPDX-License-Identifier: Apache-2.0 -->

# Test resources

Resource data is covered by [CC0-1.0](../../../../LICENSE_RESOURCES.md). The fixtures are
checked in directly; no fixture generator is required.

| File | Content | Expected result |
| --- | --- | --- |
| `one.mbsdf`, `two.mbsdf` | Plain text containing `INVALID MBSDF` | SDK rejects the resources; scalar and array defaults have empty asset paths |
| `valid_one.mbsdf`, `valid_two.mbsdf` | Two different SDK CC0 BSDF measurements | SDK loads the resources; Sdr preserves scalar/array paths and Ar opens the expected bytes |
| `one.ies`, `two.ies` | SDK CC0 light profile under two filenames | SDK loads scalar and array light-profile defaults; Ar opens the expected bytes |
| `one.cube.png`, `two.cube.png` | Original 64 x 32 RGB checkerboards | SDK loads `texture_cube` defaults; Sdr retains cube type and array paths |
| `one.png`, `two.png` | Original 64 x 64 RGB checkerboards | Scalar and array `texture_2d` defaults |
| `tile_1001.png`, `frame_1.png` | Original 64 x 64 RGB checkerboards | UDIM and frame-sequence resource identifiers |
| `tiled_resource.*.png` | Four SDK CC0 example tiles | Combined animation and UDIM resolution |

`testMdlTypes` validates scalar and array defaults and opens the resulting
resource identifiers through Ar. The native `testMdlResources` also generates
an independent valid measurement using the SDK exporter at runtime.
The cube-texture tests exercise asset loading and type mapping, not cube
sampling or HDR values. Ptex and VDB decoding remain unsupported; their type
tests use empty constructors and require no binary fixtures.
