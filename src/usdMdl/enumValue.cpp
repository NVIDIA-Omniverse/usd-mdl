// SPDX-License-Identifier: Apache-2.0
#include "enumValue.h"
#include <mi/mdl_sdk.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/token.h>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE
mi::neuraylib::IValue_enum* UsdMdlCreateEnumValue(
    mi::neuraylib::IValue_factory* factory,
    const mi::neuraylib::IType_enum* type,
    const VtValue& value)
{
    if (!factory || !type) {
        TF_WARN("MDL enum binding requires a value factory and an enum type.");
        return nullptr;
    }
    std::string member;
    if (value.IsHolding<TfToken>()) member = value.UncheckedGet<TfToken>().GetString();
    else if (value.IsHolding<std::string>()) member = value.UncheckedGet<std::string>();
    else if (!value.IsHolding<int>()) {
        TF_WARN("MDL enum '%s': expected a token, string or integer value.", type->get_symbol());
        return nullptr;
    }
    for (mi::Size index = 0; index < type->get_size(); ++index) {
        const bool matches = value.IsHolding<int>()
            ? type->get_value_code(index) == value.UncheckedGet<int>()
            : member == type->get_value_name(index);
        if (matches) return factory->create_enum(type, index);
    }
    TF_WARN("MDL enum '%s': unknown member or code '%s'.", type->get_symbol(),
            value.IsHolding<int>() ? std::to_string(value.UncheckedGet<int>()).c_str() : member.c_str());
    return nullptr;
}
PXR_NAMESPACE_CLOSE_SCOPE
