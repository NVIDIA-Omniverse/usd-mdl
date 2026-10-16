//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MDL_CONVERTER_ENUM_H
#define MDL_CONVERTER_ENUM_H

#include "../annotation.h"
#include "converter.h"

PXR_NAMESPACE_OPEN_SCOPE

class EnumConverter final : public Converter
{
public:
    /// Constructs a converter for MDL enum type.
    EnumConverter(const mi::neuraylib::IType* const iType, const DebugContext& debugCtx)
        : Converter(iType, nullptr, debugCtx)
    {
        using namespace mi::neuraylib;

        // early exit if upstream constructor failed.
        if (!_valid)
        {
            return;
        }

        _valid = false;

        _iTypeEnum = mi::base::Handle<const IType_enum>(_iType.get_interface<const IType_enum>());

        if (!_iTypeEnum)
        {
            ERROR_GET_INTERFACE(_debugCtx, IType, IType_enum);

            return;
        }

        for (mi::Size i = 0, valueCount = _iTypeEnum->get_size(); i < valueCount; ++i)
        {
            _options.emplace_back(
                SdrOption(TfToken(_iTypeEnum->get_value_name(i)), TfToken(TfStringify(_iTypeEnum->get_value_code(i)))));
        }

        _sdf_type = SdfValueTypeNames->Int;
        _type = _sdf_type.GetAsToken();
        _renderType = UsdMdlTypes->Enum;
        _symbol = TfSafeString(_iTypeEnum->get_symbol());
        _modifier = UsdMdlTypeModifiers->Uniform;
        _valid = true;
    }

    /// Returns enum option pairs for this type.
    const SdrOptionVec& GetOptions() const override
    {
        return _options;
    }

    /// Converts an MDL IValue to a VtValue for this type.
    VtValue ConvertValue(const mi::neuraylib::IValue* const iValue, VtDictionary* const metadata = nullptr) const override
    {
        using namespace mi::neuraylib;

        mi::Sint32 v;

        if (get_value<mi::Sint32>(iValue, v) == 0)
        {
            return VtValue(v);
        }

        return VtValue();
    }

    /// Populates Sdr metadata specific to this type.
    void GetMetadata(SdrTokenMap& metadataMap, VtDictionary* const metadataDict = nullptr) const override
    {
        using namespace mi::neuraylib;

        if (metadataDict)
        {
            // store type and value annotations
            const mi::base::Handle<const IAnnotation_block> iAnnotationBlockType(_iTypeEnum->get_annotations());
            std::vector<std::string> annotationPrefixes = { UsdMdlMetadataTokens->Annotation.GetString(),
                                                            UsdMdlMetadataTokens->AnnotationType.GetString() };

            Annotation::GetMetadata(iAnnotationBlockType.get(), _debugCtx, *metadataDict, annotationPrefixes);

            annotationPrefixes.back() = UsdMdlMetadataTokens->AnnotationValue.GetString();
            annotationPrefixes.push_back(""); // index;

            for (mi::Size i = 0, valueCount = _iTypeEnum->get_size(); i < valueCount; ++i)
            {
                const mi::base::Handle<const IAnnotation_block> iAnnotationBlockValue(
                    _iTypeEnum->get_value_annotations(i));
                if (iAnnotationBlockValue)
                {
                    annotationPrefixes.back() = TfStringify(i);
                    Annotation::GetMetadata(iAnnotationBlockValue.get(), _debugCtx, *metadataDict, annotationPrefixes);
                }
            }
        }

        Converter::GetMetadata(metadataMap, metadataDict);
    }

private:
    SdrOptionVec _options;
    mi::base::Handle<const mi::neuraylib::IType_enum> _iTypeEnum;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // MDL_CONVERTER_ENUM_H
