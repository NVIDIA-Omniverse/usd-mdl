//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MDL_CONVERTER_STRUCT_H
#define MDL_CONVERTER_STRUCT_H

#include "../annotation.h"
#include "converter.h"

PXR_NAMESPACE_OPEN_SCOPE

class MdlModule;

class StructConverter final : public Converter
{
public:
    /// Unrolls a struct IType into per-field output ports using
    /// the MDL field names.  `mdlModule` is forwarded to the
    /// per-field converters so resource-typed struct fields can resolve
    /// their asset paths via `MdlModule::ResolveResourceAssetPath`; may
    /// be nullptr if the struct contains no resource fields.
    static Converter::OutputsVec GetOutputs(const mi::neuraylib::IType* const iType,
                                            const mi::neuraylib::IAnnotation_block* const returnAnnotation,
                                            const std::vector<std::string> &outputNames,
                                            const MdlModule* mdlModule,
                                            const DebugContext& debugCtx);

    /// Constructs a converter for MDL struct type.
    StructConverter(const mi::neuraylib::IType* const iType, const MdlModule* mdlModule, const DebugContext& debugCtx)
        : Converter(iType, mdlModule, debugCtx)
    {
        using namespace mi::neuraylib;

        // early exit if upstream constructor failed.
        if (!_valid)
        {
            return;
        }

        _valid = false;

        _iTypeStruct = _iType.get_interface<const IType_struct>();
        if (!_iTypeStruct)
        {
            ERROR_GET_INTERFACE(_debugCtx, IType, IType_struct);
            return;
        }

        const IType_struct::Predefined_id predefinedId = _iTypeStruct->get_predefined_id();

        bool isUserStruct = false;

        switch (predefinedId)
        {
        case IType_struct::SID_USER:
            _structType = UsdMdlStructTypes->User;
            isUserStruct = true;
            break;

        case IType_struct::SID_MATERIAL_EMISSION:
            _structType = UsdMdlStructTypes->Material_Emission;
            break;

        case IType_struct::SID_MATERIAL_SURFACE:
            _structType = UsdMdlStructTypes->Material_Surface;
            break;

        case IType_struct::SID_MATERIAL_VOLUME:
            _structType = UsdMdlStructTypes->Material_Volume;
            break;

        case IType_struct::SID_MATERIAL_GEOMETRY:
            _structType = UsdMdlStructTypes->Material_Geometry;
            break;

        case IType_struct::SID_MATERIAL:
            _structType = UsdMdlStructTypes->Material;
            break;

        default:
            TF_ERROR(USD_MDL, "%s\t'%s' of predefined id: '%d' are not supported.", _debugCtx.format().c_str(),
                     typeid(IType_struct).name(), predefinedId);

            return;
        }

        _sdf_type = SdfValueTypeNames->Token;
        _type = isUserStruct ? SdrPropertyTypes->Struct : SdrPropertyTypes->Terminal;

        _renderType = UsdMdlTypes->Struct;
        _symbol = TfSafeString(_iTypeStruct->get_symbol());
        _valid = true;
    }

    /// Struct values are not converted to a single VtValue: USD has no
    /// general struct holder, and per-field unrolling happens up-front via
    /// `StructConverter::GetOutputs` (one output port per field).  Returning
    /// an empty `VtValue` here lets Sdr report the struct port without a
    /// default value, which is the only sensible behavior available.
    VtValue ConvertValue(
        const mi::neuraylib::IValue* const,
        VtDictionary* const = nullptr) const override
    {
        return VtValue();
    }

    /// Populates Sdr metadata specific to this type.
    void GetMetadata(SdrTokenMap& metadataMap, VtDictionary* const metadataDict = nullptr) const override
    {
        using namespace mi::neuraylib;

        if (metadataDict)
        {
            // store type annotations
            const mi::base::Handle<const IAnnotation_block> iAnnotationBlockType(_iTypeStruct->get_annotations());
            std::vector<std::string> annotationPrefixes = { UsdMdlMetadataTokens->Annotation.GetString(),
                                                            UsdMdlMetadataTokens->AnnotationType.GetString() };

            Annotation::GetMetadata(iAnnotationBlockType.get(), _debugCtx, *metadataDict, annotationPrefixes);
        }

        // store
        // 1. StructType
        metadataMap.emplace(UsdMdlMetadataTokens->StructType, _structType.GetString());

        Converter::GetMetadata(metadataMap, metadataDict);
    }

private:
    mi::base::Handle<const mi::neuraylib::IType_struct> _iTypeStruct;
    TfToken _structType;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // MDL_CONVERTER_STRUCT_H
