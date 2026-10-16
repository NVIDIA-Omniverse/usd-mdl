//
// SPDX-License-Identifier: Apache-2.0
//

#include "mdlParser.h"

#include "mdlSdrIntegration.h"
#include "tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace {

// Single source of truth for the `"mdl"` source-type token is
// `UsdMdlTokens->Mdl` (see tokens.h).  We wrap it in a function-local
// static to dodge the static-initialization-order problem on the
// `SdrTokenVec` that MdlParserPlugin::GetDiscoveryTypes() returns by
// reference.
const SdrTokenVec&
_DiscoveryTypes()
{
    static const SdrTokenVec kTypes = { UsdMdlTokens->Mdl };
    return kTypes;
}

}

SDR_REGISTER_PARSER_PLUGIN(MdlParserPlugin)

SdrShaderNodeUniquePtr
MdlParserPlugin::ParseShaderNode(const SdrShaderNodeDiscoveryResult& discoveryResult)
{
    return MdlSdrIntegration::Get().GetSdrFromDiscoveryResult(discoveryResult);
}

const SdrTokenVec&
MdlParserPlugin::GetDiscoveryTypes() const
{
    return _DiscoveryTypes();
}

const TfToken&
MdlParserPlugin::GetSourceType() const
{
    return UsdMdlTokens->Mdl;
}

PXR_NAMESPACE_CLOSE_SCOPE
