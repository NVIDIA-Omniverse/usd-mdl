//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MDL_CONVERTER_TEXTURE_H
#define MDL_CONVERTER_TEXTURE_H

#include "resource.h"

PXR_NAMESPACE_OPEN_SCOPE

class MdlModule;

class TextureConverter final : public ResourceConverter
{
public:
    /// Constructs a converter for MDL texture type.
    TextureConverter(const mi::neuraylib::IType* const iType, const MdlModule* mdlModule, const DebugContext& debugCtx)
        : ResourceConverter(iType, mdlModule, debugCtx)
    {
        using namespace mi::neuraylib;

        // early exit if upstream constructor failed.
        if (!_valid)
        {
            return;
        }

        _valid = false;

        const mi::base::Handle<const IType_texture> iTypeTexture(_iType.get_interface<const IType_texture>());
        if (!iTypeTexture)
        {
            ERROR_GET_INTERFACE(_debugCtx, IType, IType_texture);
            return;
        }

        const IType_texture::Shape shape = iTypeTexture->get_shape();

        switch (shape)
        {
        case IType_texture::Shape::TS_2D:
            _renderType = UsdMdlTypes->Texture2d;
            break;

        case IType_texture::Shape::TS_3D:
            _renderType = UsdMdlTypes->Texture3d;
            break;

        case IType_texture::Shape::TS_CUBE:
            _renderType = UsdMdlTypes->TextureCube;
            break;

        case IType_texture::Shape::TS_PTEX:
            _renderType = UsdMdlTypes->TexturePtex;
            break;

        case IType_texture::Shape::TS_BSDF_DATA:
            _renderType = UsdMdlTypes->TextureBsdfData;
            break;

        default:
            TF_ERROR(USD_MDL, "%s\tIType_texture::Shape '%d' not supported.", _debugCtx.format().c_str(), shape);

            return;
        }

        _valid = true;
    }

    /// Converts an MDL IValue to a VtValue, extracting gamma and selector metadata.
    VtValue ConvertValue(const mi::neuraylib::IValue* const iValue, VtDictionary* const metadata = nullptr) const override final
    {
        using namespace mi::neuraylib;

        if (metadata)
        {
            // All texture values contain a gamma field, and a few contain a selector field.
            // These values are stored as metadata.

            const mi::base::Handle<const IValue_texture> iValueTexture(iValue->get_interface<const IValue_texture>());
            if (!iValueTexture)
            {
                return VtValue();
            }

            std::vector<std::string> keyPath = { UsdMdlMetadataTokens->TextureGamma.GetString() };

            // value::gamma
            metadata->SetValueAtPath(keyPath, VtValue(iValueTexture->get_gamma()));

            if ((_renderType == UsdMdlTypes->Texture2d) || (_renderType == UsdMdlTypes->Texture3d))
            {
                // value::selector
                keyPath.back() = UsdMdlMetadataTokens->TextureSelector.GetString();
                metadata->SetValueAtPath(keyPath, VtValue(TfSafeString(iValueTexture->get_selector())));
            }
        }

        return ResourceConverter::ConvertValue(iValue, metadata);
    }
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // MDL_CONVERTER_TEXTURE_H
