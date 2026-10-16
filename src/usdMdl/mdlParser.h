//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MDL_PLUGIN_MDL_PARSER_H
#define MDL_PLUGIN_MDL_PARSER_H

#include <pxr/pxr.h>
#include <pxr/usd/sdr/parserPlugin.h>

PXR_NAMESPACE_OPEN_SCOPE

class MdlParserPlugin final : public SdrParserPlugin
{
public:
    MdlParserPlugin() = default;

    ~MdlParserPlugin() override = default;

    SdrShaderNodeUniquePtr ParseShaderNode(
        const SdrShaderNodeDiscoveryResult& discoveryResult) override;

    const SdrTokenVec& GetDiscoveryTypes() const override;

    const TfToken& GetSourceType() const override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
