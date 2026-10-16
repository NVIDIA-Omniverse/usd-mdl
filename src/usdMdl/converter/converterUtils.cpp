//
// SPDX-License-Identifier: Apache-2.0
//

#include "converterUtils.h"

#include <pxr/base/vt/value.h>
#include <pxr/pxr.h>

#include <map>

PXR_NAMESPACE_OPEN_SCOPE

namespace ConverterUtils
{

void SetArrayMetadataValues(const std::vector<VtDictionary>& arrayMetadata, VtDictionary& metadata)
{
    using ValueMap = std::map<std::string, std::vector<VtValue>>;

    const size_t arraySize = arrayMetadata.size();
    ValueMap valueMap;

    for (size_t i = 0; i < arraySize; ++i)
    {
        const VtDictionary& vtDict = arrayMetadata[i];

        for (auto&& P : vtDict)
        {
            // Size each column up front: absent metadata stays at its original
            // array index rather than collapsing the remaining values left.
            auto it = valueMap.try_emplace(P.first, arraySize).first;
            it->second[i] = P.second;
        }
    }

    for (auto&& P : valueMap)
    {
        metadata.SetValueAtPath(P.first, VtValue(P.second));
    }
}

}

PXR_NAMESPACE_CLOSE_SCOPE
