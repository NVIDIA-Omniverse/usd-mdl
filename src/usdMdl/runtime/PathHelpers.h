//
// SPDX-License-Identifier: Apache-2.0
//
// Pure string helpers shared by EntityResolver and TileExpansion.
//
// Promoted from anonymous-namespace statics so the malformed-input
// branches can be directly unit-tested (the SDK never feeds these
// functions garbage on the happy path, leaving large slices of code
// unreachable from integration tests alone).  Header is internal to
// the usdMdl library; it is not installed and not part of the
// public ABI.

#pragma once

#include "defines.h"
#include "../api.h"

#include <string>

MDL_RUNTIME_NAMESPACE_OPEN
namespace path {

/// True for an absolute Windows drive path such as `C:/foo` or `C:\\foo`.
USDMDL_API
bool IsWindowsDrivePath(const std::string& p);

/// Return the length of an RFC 3986 URI scheme, or zero when none is present.
/// Windows drive paths are not treated as one-letter URI schemes.
USDMDL_API
std::size_t GetUriSchemeLength(const std::string& p);

/// True if \p p is an MDLE archive member reference (`foo.mdle:inner`
/// or `foo.MDLE:inner`). MDLE containers are outside the Ar resolver's
/// supported contract and are rejected when this returns true.
USDMDL_API
bool LooksLikeMdleContainer(const std::string& p);

/// True if \p p contains an MDL frame-sequence marker of the form
/// `<#>`, `<##>`, ... (angle-bracketed run of one or more '#'s).
/// Matches MDL-SDK's compilercore_file_resolution.cpp::is_frame_marker.
USDMDL_API
bool HasFrameMarker(const std::string& p);

struct FrameMarker
{
    std::size_t position = std::string::npos;
    std::size_t digits = 0;
};

FrameMarker FindFrameMarker(const std::string& p);

/// Length of a recognized texture marker starting at position, or zero.
std::size_t ResourceMarkerLength(const std::string& p, std::size_t position);

/// True if \p p contains any uvtile marker (`<UDIM>`, `<UVTILE0>`,
/// `<UVTILE1>`) or a frame-sequence marker per `HasFrameMarker`.
USDMDL_API
bool HasTilingOrSequenceMarker(const std::string& p);

/// Normalize an MDL resource filename for SdfAssetPath. Relative MDL paths
/// gain a leading slash; absolute filesystem paths and identifiers with a
/// syntactically valid URI scheme are preserved verbatim.
USDMDL_API
std::string NormalizeMdlResourceFilename(const char* raw);

/// Convert an MDL relative module name like `.::foo::bar` or
/// `..::..::pkg::baz` into a relative file path like `./foo/bar.mdl` or
/// `../../pkg/baz.mdl`.  Returns an empty string for:
///   - absolute names (start with `::`)
///   - empty / null input
///   - malformed or non-leading `.` / `..` components
///   - empty tail after the dots
/// Package depth is checked against the owner by the entity resolver before I/O.
/// Non-dot components are decoded once from public MDL name encoding.
USDMDL_API
std::string MdlRelativeModuleNameToFilePath(const char* mdlName);

} // namespace path
MDL_RUNTIME_NAMESPACE_CLOSE
