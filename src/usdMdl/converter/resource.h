//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MDL_CONVERTER_RESOURCE_H
#define MDL_CONVERTER_RESOURCE_H

#include "converter.h"
#include "../mdlModule.h"

PXR_NAMESPACE_OPEN_SCOPE

class MdlModule;

class ResourceConverter : public Converter
{
public:
    /// Constructs a converter for MDL resource types (textures, light profiles, BSDF measurements).
    ResourceConverter(const mi::neuraylib::IType* const iType, const MdlModule* mdlModule, const DebugContext& debugCtx)
        : Converter(iType, mdlModule, debugCtx)
    {
        _sdf_type = SdfValueTypeNames->Asset;
        _type = _sdf_type.GetAsToken();
    }

    /// Converts an MDL IValue to a VtValue for this type.
    VtValue ConvertValue(const mi::neuraylib::IValue* const iValue, VtDictionary* const metadata = nullptr) const override
    {
        using namespace mi::neuraylib;

        const mi::base::Handle<const IValue_resource> val(iValue->get_interface<const IValue_resource>());
        if (val)
        {
            static const VtValue kEmpty = VtValue(SdfAssetPath());

            // Preserve the MDL-absolute logical path as the authored value and
            // store Ar's unmodified resolved identifier as the resolved value.
            const SdfAssetPath assetPath =
                _module ? _module->ResolveResourceAssetPath(val.get()) : SdfAssetPath();
            if (assetPath.GetAssetPath().empty())
            {
                return kEmpty;
            }
            return VtValue(assetPath);
        }

        return VtValue();
    }

    /// Populates Sdr metadata specific to this type.
    void GetMetadata(SdrTokenMap& metadataMap, VtDictionary* const metadataDict = nullptr) const override
    {
        // store
        // 1. IsAssetIdentifier

        metadataMap.emplace(std::make_pair(SdrPropertyMetadata->IsAssetIdentifier, "1"));

        Converter::GetMetadata(metadataMap, metadataDict);
    }
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // MDL_CONVERTER_RESOURCE_H
