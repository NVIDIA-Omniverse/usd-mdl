//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/declare.h>
#include <pxr/usd/sdr/shaderNode.h>

#include "api.h"

#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

/// Load one MDL module from \p assetPath and register exported, non-removed
/// function/material definitions with `SdrRegistry`. Return only valid shader
/// nodes, including hidden definitions with `mdl:hidden` UI metadata.
/// Off-search-path assets (URLs that no configured MDL search path covers) are routed through this plugin's Ar-backed entity resolver via a
/// URI-encoded qualified name (see runtime/UriEncoding.h); in-search-path
/// assets use their natural MDL qualified name.
///
/// Module-loading failures emit a TF diagnostic and return an empty vector.
/// Individual parse failures emit diagnostics and are omitted from the result;
/// successfully parsed definitions from the same module are still returned.
///
/// This is usdMdl's public module-loading entry point. Runtime ownership and
/// host-provided INeuray configuration are exposed separately through
/// initialize.h.
USDMDL_API
std::vector<SdrShaderNodeConstPtr>
UsdMdlAddModuleToRegistry(const SdfAssetPath& assetPath);

PXR_NAMESPACE_CLOSE_SCOPE
