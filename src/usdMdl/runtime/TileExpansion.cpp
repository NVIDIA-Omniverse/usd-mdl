//
// SPDX-License-Identifier: Apache-2.0
//

#include "TileExpansion.h"

#include "PathHelpers.h"

#include <mi/mdl_sdk.h>

#include <pxr/usd/ar/resolvedPath.h>
#include <pxr/usd/ar/resolver.h>

#include <cstring>

PXR_NAMESPACE_USING_DIRECTIVE

MDL_RUNTIME_NAMESPACE_OPEN

namespace {

constexpr const char* kUdim    = "<UDIM>";
constexpr const char* kUvtile0 = "<UVTILE0>";
constexpr const char* kUvtile1 = "<UVTILE1>";

mi::neuraylib::Uvtile_mode
DetectUvtileMode(const std::string& s)
{
    if (s.find(kUdim) != std::string::npos) {
        return mi::neuraylib::UVTILE_MODE_UDIM;
    }
    if (s.find(kUvtile0) != std::string::npos) {
        return mi::neuraylib::UVTILE_MODE_UVTILE0;
    }
    if (s.find(kUvtile1) != std::string::npos) {
        return mi::neuraylib::UVTILE_MODE_UVTILE1;
    }
    return mi::neuraylib::UVTILE_MODE_NONE;
}

struct ProbeResult
{
    std::string resolvedUrl;
    std::string concretePath;

    explicit operator bool() const { return !resolvedUrl.empty(); }
};

/// Substitute markers in \p mask via the SDK's helper, then probe Ar.
/// Returns both the concrete substitution and Ar's resolved URL on a hit.
ProbeResult
ProbeOne(
    mi::neuraylib::IMdl_impexp_api* impexp,
    const std::string& mask,
    const ArResolvedPath& anchor,
    const path::FrameMarker& frameMarker,
    mi::Size frame,
    mi::Sint32 u,
    mi::Sint32 v)
{
    ArResolver& resolver = ArGetResolver();
    auto probe = [&](const std::string& candidate) -> ProbeResult {
        mi::base::Handle<const mi::IString> const subbed(
            impexp->frame_uvtile_marker_to_string(candidate.c_str(), frame, u, v));
        if (!subbed || !subbed->get_c_str() || !subbed->get_c_str()[0]) {
            return {};
        }
        const std::string identifier =
            resolver.CreateIdentifier(subbed->get_c_str(), anchor);
        if (identifier.empty()) {
            return {};
        }
        const ArResolvedPath resolved = resolver.Resolve(identifier);
        return resolved
            ? ProbeResult{resolved.GetPathString(), subbed->get_c_str()}
            : ProbeResult{};
    };

    if (const ProbeResult resolved = probe(mask)) {
        return resolved;
    }

    const std::string frameNumber = std::to_string(frame);
    if (frameNumber.size() >= frameMarker.digits) {
        return {};
    }
    // The SDK emits the shortest valid frame spelling. Also try the allowed
    // zero-padded spellings, e.g. `00` for frame 0 and marker `<##>`.
    for (std::size_t width = frameNumber.size() + 1;
         width <= frameMarker.digits;
         ++width) {
        std::string candidate(mask);
        candidate.replace(
            frameMarker.position,
            frameMarker.digits + 2,
            std::string(width - frameNumber.size(), '0') + frameNumber);
        if (const ProbeResult resolved = probe(candidate)) {
            return resolved;
        }
    }
    return ProbeResult{};
}

} // namespace

TileExpansionResult
ExpandTilesViaAr(
    mi::neuraylib::IMdl_impexp_api* impexp,
    const std::string& maskUrl,
    const std::string& ownerFilePath,
    int uvtileGridSize,
    int frameMaxIndex,
    int frameMissEarlyOut)
{
    TileExpansionResult out;
    if (maskUrl.empty() || !impexp) {
        return out;
    }

    out.uvtileMode = DetectUvtileMode(maskUrl);
    const path::FrameMarker frameMarker = path::FindFrameMarker(maskUrl);
    out.hasSequenceMarker = frameMarker.digits != 0;

    // Markerless input: caller handles via the plain single-file path.
    if (out.uvtileMode == mi::neuraylib::UVTILE_MODE_NONE && !out.hasSequenceMarker) {
        return out;
    }

    if (uvtileGridSize < 1) {
        uvtileGridSize = 1;
    }
    if (frameMaxIndex < 0) {
        frameMaxIndex = 0;
    }
    if (frameMissEarlyOut < 1) {
        frameMissEarlyOut = 1;
    }

    ArResolvedPath anchor;
    if (!ownerFilePath.empty()) {
        anchor = ArResolvedPath(ownerFilePath);
    }

    // Lambda: probe the uv grid (or just (0,0) when there's no uvtile marker)
    // at the given frame.  Appends hits to out.tiles.  Returns true iff any
    // tile resolved at this frame (used to drive frame-axis early-out).
    auto probeFrame = [&](mi::Size frame) -> bool {
        bool anyHit = false;
        if (out.uvtileMode == mi::neuraylib::UVTILE_MODE_NONE) {
            const ProbeResult result = ProbeOne(impexp, maskUrl, anchor, frameMarker, frame, 0, 0);
            if (result) {
                out.tiles.push_back(
                    {result.resolvedUrl, result.concretePath, frame, 0, 0});
                anyHit = true;
            }
            return anyHit;
        }
        for (int v = 0; v < uvtileGridSize; ++v) {
            for (int u = 0; u < uvtileGridSize; ++u) {
                const ProbeResult result = ProbeOne(impexp, maskUrl, anchor, frameMarker, frame, u, v);
                if (result) {
                    out.tiles.push_back(
                        {result.resolvedUrl, result.concretePath, frame, u, v});
                    anyHit = true;
                }
            }
        }
        return anyHit;
    };

    if (!out.hasSequenceMarker) {
        probeFrame(0);
        return out;
    }

    int consecutiveMiss = 0;
    for (mi::Size f = 0;
         f <= static_cast<mi::Size>(frameMaxIndex);
         ++f) {
        const bool hit = probeFrame(f);
        if (hit) {
            consecutiveMiss = 0;
        } else {
            ++consecutiveMiss;
            if (consecutiveMiss >= frameMissEarlyOut) {
                break;
            }
        }
    }

    // tiles is already frame-major (outer loop) then v-major then u-major.
    return out;
}

MDL_RUNTIME_NAMESPACE_CLOSE
