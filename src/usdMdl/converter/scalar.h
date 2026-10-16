//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MDL_CONVERTER_SCALAR_H
#define MDL_CONVERTER_SCALAR_H

#include "converter.h"

#include <pxr/base/tf/stringUtils.h>

#include <type_traits>

PXR_NAMESPACE_OPEN_SCOPE

template <typename ValueType>
class ScalarConverter final : public Converter
{
public:
    ScalarConverter(const mi::neuraylib::IType* iType,
                    const SdfValueTypeName& sdfType, const TfToken& renderType,
                    const DebugContext& debugCtx)
        : Converter(iType, nullptr, debugCtx)
    {
        _sdf_type = sdfType;
        _type = _sdf_type.GetAsToken();
        _renderType = renderType;
    }

    VtValue ConvertValue(const mi::neuraylib::IValue* iValue,
                         VtDictionary* = nullptr) const override
    {
        ValueType value;
        if (mi::neuraylib::get_value(iValue, value) != 0) {
            return VtValue();
        }
        if constexpr (std::is_same_v<ValueType, const char*>) {
            // The SDK string is borrowed; the USD value must own its text.
            return VtValue(TfSafeString(value));
        } else {
            return VtValue(value);
        }
    }
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // MDL_CONVERTER_SCALAR_H
