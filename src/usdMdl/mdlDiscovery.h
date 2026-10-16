//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MDL_PLUGIN_MDL_DISCOVERY_H
#define MDL_PLUGIN_MDL_DISCOVERY_H

#include <pxr/pxr.h>
#include <pxr/usd/sdr/discoveryPlugin.h>

PXR_NAMESPACE_OPEN_SCOPE

class MdlDiscoveryPlugin final : public SdrDiscoveryPlugin
{
public:
    MdlDiscoveryPlugin() = default;

    ~MdlDiscoveryPlugin() override = default;

    SdrShaderNodeDiscoveryResultVec DiscoverShaderNodes(const Context&) override;

    const SdrStringVec& GetSearchURIs() const override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
