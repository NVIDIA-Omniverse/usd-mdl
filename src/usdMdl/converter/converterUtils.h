//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MDL_USD_MDL_CONVERTER_UTILS_H
#define MDL_USD_MDL_CONVERTER_UTILS_H

#include <pxr/base/vt/dictionary.h>
#include <pxr/base/vt/value.h>
#include <pxr/pxr.h>

#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

namespace ConverterUtils
{

// Collect one list per metadata key, preserving empty slots so metadata
// indices continue to identify the corresponding resource-array elements.
void SetArrayMetadataValues(const std::vector<VtDictionary>& arrayMetadata, VtDictionary& metadata);
}


PXR_NAMESPACE_CLOSE_SCOPE

#endif // MDL_USD_MDL_CONVERTER_UTILS_H
