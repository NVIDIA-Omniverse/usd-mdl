//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MDL_CONVERTER_DISTRIBUTION_H
#define MDL_CONVERTER_DISTRIBUTION_H

#include "converter.h"

PXR_NAMESPACE_OPEN_SCOPE

// Distribution ports carry a render type, but have no representable USD value.
// Keep Converter's empty-value behavior for their defaults.
class BsdfConverter final : public Converter
{
public:
    BsdfConverter(const mi::neuraylib::IType* iType, const DebugContext& debugCtx)
        : Converter(iType, nullptr, debugCtx)
    {
        _sdf_type = SdfValueTypeNames->Token;
        _type = _sdf_type.GetAsToken();
        _renderType = UsdMdlTypes->Bsdf;
    }
};

class EdfConverter final : public Converter
{
public:
    EdfConverter(const mi::neuraylib::IType* iType, const DebugContext& debugCtx)
        : Converter(iType, nullptr, debugCtx)
    {
        _sdf_type = SdfValueTypeNames->Token;
        _type = _sdf_type.GetAsToken();
        _renderType = UsdMdlTypes->Edf;
    }
};

class VdfConverter final : public Converter
{
public:
    VdfConverter(const mi::neuraylib::IType* iType, const DebugContext& debugCtx)
        : Converter(iType, nullptr, debugCtx)
    {
        _sdf_type = SdfValueTypeNames->Token;
        _type = _sdf_type.GetAsToken();
        _renderType = UsdMdlTypes->Vdf;
    }
};

class HairBsdfConverter final : public Converter
{
public:
    HairBsdfConverter(const mi::neuraylib::IType* iType, const DebugContext& debugCtx)
        : Converter(iType, nullptr, debugCtx)
    {
        _sdf_type = SdfValueTypeNames->Token;
        _type = _sdf_type.GetAsToken();
        _renderType = UsdMdlTypes->Hair_Bsdf;
    }
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // MDL_CONVERTER_DISTRIBUTION_H
