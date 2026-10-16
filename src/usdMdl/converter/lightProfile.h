//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MDL_CONVERTER_LIGHT_PROFILE_H
#define MDL_CONVERTER_LIGHT_PROFILE_H

#include "resource.h"

PXR_NAMESPACE_OPEN_SCOPE

class MdlModule;

class LightProfileConverter final : public ResourceConverter
{
public:
    /// Constructs a converter for MDL light_profile type.
    LightProfileConverter(const mi::neuraylib::IType* const iType,
                          const MdlModule* mdlModule,
                          const DebugContext& debugCtx)
        : ResourceConverter(iType, mdlModule, debugCtx)
    {
        _renderType = UsdMdlTypes->Light_Profile;
    }
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // MDL_CONVERTER_LIGHT_PROFILE_H
