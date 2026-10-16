//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "defines.h"

#include <mi/mdl_sdk.h>

#include <string>
#include <vector>

MDL_RUNTIME_NAMESPACE_OPEN

/// One concrete entity discovered by probing a UDIM / frame-sequence mask
/// through pxr::ArGetResolver().
struct ResolvedTile
{
    std::string resolvedUrl;   ///< the absolute resolved identifier Ar returned
    std::string concretePath;  ///< the concrete mask substitution passed to Ar
    mi::Size frame = 0;       ///< 0 when the mask has no frame marker
    mi::Sint32 u = 0;
    mi::Sint32 v = 0;
};

/// Result of probing a mask path.  `tiles` is sorted frame-major, then v, then u.
struct TileExpansionResult
{
    std::vector<ResolvedTile> tiles;
    mi::neuraylib::Uvtile_mode uvtileMode = mi::neuraylib::UVTILE_MODE_NONE;
    bool hasSequenceMarker = false;
};

/// Probe the asset backend for the concrete (frame, u, v) entities a mask
/// expands to, using IMdl_impexp_api::frame_uvtile_marker_to_string for the
/// substitutions and ArGetResolver().Resolve() to test each candidate.
///
/// \param impexp                IMdl_impexp_api from the context; must be non-null
///                              if \p maskUrl contains any marker.
/// \param maskUrl               Mask path (may contain <UDIM> / <UVTILE0> /
///                              <UVTILE1> and/or `#` runs).
/// \param ownerFilePath         Optional anchor for Ar relative resolution.
///                              Pass empty/nullptr-equivalent for absolute paths.
/// \param uvtileGridSize        Range probed in each uv axis (default 10).
/// \param frameMaxIndex         Maximum frame index to probe before stopping.
/// \param frameMissEarlyOut     Stop the frame walk after this many consecutive
///                              frame indices produced no hits.
///
/// \return A result whose `tiles` is empty when no recognized marker is present
///         OR when no candidate resolved.  Callers should fall back to plain
///         single-path handling in that case.
TileExpansionResult
ExpandTilesViaAr(
    mi::neuraylib::IMdl_impexp_api* impexp,
    const std::string& maskUrl,
    const std::string& ownerFilePath = {},
    int uvtileGridSize = 10,
    int frameMaxIndex = 1000,
    int frameMissEarlyOut = 32);

MDL_RUNTIME_NAMESPACE_CLOSE
