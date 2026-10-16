//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MDL_CONVERTER_COLOR_H
#define MDL_CONVERTER_COLOR_H

#include "converter.h"

PXR_NAMESPACE_OPEN_SCOPE

class ColorConverter final : public Converter
{
public:
    /// Constructs a converter for MDL color type.
    ColorConverter(const mi::neuraylib::IType* const iType, const DebugContext& debugCtx)
        : Converter(iType, nullptr, debugCtx)
    {
        _sdf_type = SdfValueTypeNames->Color3f;
        _type = _sdf_type.GetAsToken();
        _renderType = UsdMdlTypes->Color;
    }

    /// Converts an MDL IValue to a VtValue for this type.
    ///
    /// MDL's `mi::math::Color` storage is 4 floats (r, g, b, a) but
    /// `Sdr` / `UsdShade` model `color` as `Color3f` (USD opacity is
    /// authored as a separate channel).  We deliberately drop the
    /// alpha component; supported production MDL content does not use
    /// the 4th channel as anything other than 1.0, and Kit's
    /// `omni.usdmdl` follows the same convention. If
    /// future MDL content starts populating the alpha channel
    /// meaningfully, we would need to either author it as separate
    /// `opacity` metadata or upgrade the Sdr type to `Color4f`.
    VtValue ConvertValue(const mi::neuraylib::IValue* const iValue, VtDictionary* const metadata = nullptr) const override
    {
        using namespace mi::neuraylib;

        // contains 4 components: r, g, b, a -- we use only r, g, b.
        mi::math::Color v;

        if (get_value(iValue, v) == 0)
        {
            return VtValue(GfVec3f(v.begin()));
        }

        return VtValue();
    }

};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // MDL_CONVERTER_COLOR_H
