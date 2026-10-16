//
// SPDX-License-Identifier: Apache-2.0
//

#include "pxr/pxr.h"
#include <pxr/base/tf/pyLock.h>
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/sdr/declare.h"
#include "pxr/usd/sdr/shaderNode.h"
#include "pxr/external/boost/python.hpp"

#include "addModuleToRegistry.h"
#include "canConnectTo.h"
#include <pxr/usd/sdr/shaderProperty.h>
#include "shaderDefinitions.h"
#include "initialize.h"

#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

using namespace pxr_boost::python;

namespace {

pxr_boost::python::list
_AddModuleToRegistry(const SdfAssetPath& assetPath)
{
    std::vector<SdrShaderNodeConstPtr> nodes;
    {
        TF_PY_ALLOW_THREADS_IN_SCOPE();
        nodes = UsdMdlAddModuleToRegistry(assetPath);
    }
    pxr_boost::python::list result;
    for (auto node : nodes) {
        result.append(ptr(node));
    }
    return result;
}

pxr_boost::python::list
_AddShaderDefinitionsToRegistry(const SdfAssetPath& definitionsAsset,
                               const SdfAssetPath& implementationAsset)
{
    std::vector<SdrShaderNodeConstPtr> nodes;
    {
        TF_PY_ALLOW_THREADS_IN_SCOPE();
        nodes = UsdMdlAddShaderDefinitionsToRegistry(definitionsAsset, implementationAsset);
    }
    pxr_boost::python::list result;
    for (const auto* node : nodes) {
        result.append(ptr(node));
    }
    return result;
}

} // anon

void wrapMdlSdrIntegration()
{
    def("CanConnectTo", &UsdMdlCanConnectTo,
        (arg("property"), arg("other")),
        "Check local MDL port compatibility in either order. None or missing MDL "
        "declarations return False. Does not validate graph-wide array lengths "
        "or infer uniformity; see docs/python-api.md.");
    def("AddModuleToRegistry",
        &_AddModuleToRegistry,
        (arg("assetPath")),
        "Load one MDL module from `assetPath` (str or Sdf.AssetPath), "
        "register every exported function / material definition with "
        "Sdr.Registry, and return the resulting Sdr.ShaderNode list "
        "(empty on failure).  See docs/python-api.md for behaviour, "
        "URI encoding for off-search-path assets, and the supported "
        "argument types.");
    def("AddShaderDefinitionsToRegistry", &_AddShaderDefinitionsToRegistry,
        (arg("definitionsAsset"), arg("implementationAsset") = SdfAssetPath()),
        "Register standard USD shader interfaces backed by MDL source assets. "
        "Optional implementationAsset overrides their module path. Invalid interfaces "
        "emit diagnostics and are omitted; returned nodes are registry-owned.");
    def("Shutdown",
        &UsdMdlShutdown,
        "Release usdMdl's MDL SDK handles. Call this before a host-owned "
        "MDL SDK is shut down; it is safe to call more than once.");
}
