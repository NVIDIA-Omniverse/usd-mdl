// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "shaderDefinitions.h"
#include <pxr/usd/sdr/shaderNodeDiscoveryResult.h>
#include <utility>

PXR_NAMESPACE_OPEN_SCOPE
namespace UsdMdlShaderDefinitions {
using PreparedNode = std::pair<SdrShaderNodeDiscoveryResult, SdrShaderNodeUniquePtr>;
// Discovery runs while SdrRegistry is being constructed. Its preparation must
// load/inspect only the selected SDK modules and never access that registry.
std::vector<PreparedNode> Prepare(
    const SdfAssetPath& definitionsAsset, const SdfAssetPath& implementationAsset,
    bool registryFree = false);
}
PXR_NAMESPACE_CLOSE_SCOPE
