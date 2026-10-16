//
// SPDX-License-Identifier: Apache-2.0
//

#include "mdlDiscovery.h"

#include "EnvSettings.h"
#include "runtime/Context.h"
#include "mdlSdrIntegration.h"

#include <pxr/base/tf/envSetting.h>
#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/thisPlugin.h>
#include <pxr/base/tf/diagnostic.h>

PXR_NAMESPACE_OPEN_SCOPE

SDR_REGISTER_DISCOVERY_PLUGIN(MdlDiscoveryPlugin)

SdrShaderNodeDiscoveryResultVec
MdlDiscoveryPlugin::DiscoverShaderNodes(const Context&)
{
    const bool preview = TfGetEnvSetting(USD_MDL_LOAD_USD_PREVIEW_SURFACE);
    const bool native = TfGetEnvSetting(USD_MDL_DISCOVER_ON_STARTUP);
    if (!preview && !native) return {};
    auto& integration = MdlSdrIntegration::Get();
    SdrShaderNodeDiscoveryResultVec results;
    if (preview) {
        const std::string resource = PlugFindPluginResource(PLUG_THIS_PLUGIN, "UsdPreviewSurface.usda");
        if (resource.empty()) {
            TF_WARN("MDL Preview Surface: cannot find the plugin's UsdPreviewSurface.usda resource.");
        } else {
            results = integration.GetUsdPreviewSurfaceDiscoveryResults(SdfAssetPath(resource));
        }
    }
    // The independent flag controls only a full native MDL catalog scan.
    if (native) {
        auto catalog = integration.GetDiscoveryResults();
        results.insert(results.end(), catalog.begin(), catalog.end());
    }
    return results;
}

const SdrStringVec&
MdlDiscoveryPlugin::GetSearchURIs() const
{
    // Get() initializes the SDK, so check readiness before touching it.
    // An early query must not publish an empty snapshot for later queries.
    if (!usdMdl::runtime::Context::instance().isInitialized()) {
        static const SdrStringVec empty;
        return empty;
    }
    return MdlSdrIntegration::Get().GetSearchPaths();
}

PXR_NAMESPACE_CLOSE_SCOPE
