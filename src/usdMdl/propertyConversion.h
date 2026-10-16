//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "debugContext.h"

#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/valueTypeName.h>
#include <pxr/usd/sdr/shaderProperty.h>

#include <cstddef>

namespace mi { namespace neuraylib {
class IExpression;
class IType;
} }

PXR_NAMESPACE_OPEN_SCOPE

namespace UsdMdlPropertyConversion
{
/// Apply an explicit sdrUsdDefinitionType annotation before constructing Sdr.
/// Call before inserting the converter's fallback USD type into metadata.
/// The original SDK type/default preserve enum names even for aliased codes.
/// Nonconstant defaults remain unavailable; this function never evaluates MDL.
/// Without an explicit override, all arguments are left unchanged. On failure,
/// report the incompatible override and leave every argument unchanged.
bool FinalizeProperty(const mi::neuraylib::IType* mdlType,
                      const mi::neuraylib::IExpression* mdlDefault,
                      const SdfValueTypeName& nativeSdfType,
                      TfToken& sdrType,
                      size_t& arraySize,
                      VtValue& defaultValue,
                      SdrTokenMap& metadata,
                      SdrOptionVec& options,
                      const DebugContext& debugContext);
}

PXR_NAMESPACE_CLOSE_SCOPE
