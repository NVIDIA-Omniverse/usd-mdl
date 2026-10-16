//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MDL_CONVERTER_BSDF_MEASUREMENT_H
#define MDL_CONVERTER_BSDF_MEASUREMENT_H

#include "resource.h"

PXR_NAMESPACE_OPEN_SCOPE

class MdlModule;

class BsdfMeasurementConverter final : public ResourceConverter
{
public:
    /// Constructs a converter for MDL bsdf_measurement type.
    BsdfMeasurementConverter(const mi::neuraylib::IType* const iType,
                             const MdlModule* mdlModule,
                             const DebugContext& debugCtx)
        : ResourceConverter(iType, mdlModule, debugCtx)
    {
        _renderType = UsdMdlTypes->Bsdf_Measurement;
    }
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // MDL_CONVERTER_BSDF_MEASUREMENT_H
