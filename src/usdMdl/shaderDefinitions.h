// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "api.h"
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

/// Register MDL implementations with interfaces authored as standard UsdShade
/// Shader definitions. Each Shader supplies an mdl sourceAsset and an exact
/// sourceAsset:subIdentifier. Names, USD types, defaults and outputs come from
/// the USD definition; original MDL identities remain available for binding.
/// Explicit array constructor specializations use the element type's defining
/// module and expose every concrete valueN argument plus an array return.
/// USD implementationName metadata may rename those arguments and the output.
/// An optional implementationAsset replaces the sourceAsset on every Shader,
/// useful with the installed Preview Surface interface template.
/// Invalid definitions emit diagnostics and are omitted. Registry-owned nodes
/// remain immutable; an identifier must identify one definition for source mdl.
USDMDL_API
std::vector<SdrShaderNodeConstPtr>
UsdMdlAddShaderDefinitionsToRegistry(
    const SdfAssetPath& definitionsAsset,
    const SdfAssetPath& implementationAsset = SdfAssetPath());

PXR_NAMESPACE_CLOSE_SCOPE
