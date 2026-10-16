//
// SPDX-License-Identifier: Apache-2.0
//

#include "propertyConversion.h"

#include <mi/mdl_sdk.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/sdr/shaderMetadataHelpers.h>

#include <string>
#include <utility>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
using namespace mi::neuraylib;

bool _Reject(const DebugContext& context, const std::string& requested, const char* reason)
{
    TF_WARN("%s\tsdrUsdDefinitionType '%s' cannot represent this MDL property: %s.",
            context.format().c_str(), requested.c_str(), reason);
    return false;
}

mi::base::Handle<const IType_enum> _GetEnumType(const IType* type, bool& isArray)
{
    isArray = false;
    mi::base::Handle<const IType> unaliased(type ? type->skip_all_type_aliases() : nullptr);
    if (unaliased && unaliased->get_kind() == IType::TK_ARRAY) {
        const mi::base::Handle<const IType_array> array(unaliased.get_interface<const IType_array>());
        const mi::base::Handle<const IType> element(array ? array->get_element_type() : nullptr);
        unaliased = mi::base::Handle<const IType>(element ? element->skip_all_type_aliases() : nullptr);
        isArray = true;
    }
    return mi::base::Handle<const IType_enum>(
        unaliased ? unaliased->get_interface<const IType_enum>() : nullptr);
}

bool _ConvertEnumDefault(const IExpression* expression, bool isArray, VtValue& value)
{
    // ConvertDefault has already distinguished absent/nonconstant defaults.
    if (!expression || expression->get_kind() != IExpression::EK_CONSTANT) {
        return value.IsEmpty();
    }
    const mi::base::Handle<const IExpression_constant> constant(
        expression->get_interface<const IExpression_constant>());
    const mi::base::Handle<const IValue> source(constant ? constant->get_value() : nullptr);
    if (!source) {
        return false;
    }
    if (!isArray) {
        const mi::base::Handle<const IValue_enum> item(source.get_interface<const IValue_enum>());
        if (!item || !item->get_name()) {
            return false;
        }
        value = VtValue(std::string(item->get_name()));
        return true;
    }
    const mi::base::Handle<const IValue_array> array(source.get_interface<const IValue_array>());
    if (!array) {
        return false;
    }
    VtStringArray names;
    names.reserve(array->get_size());
    for (mi::Size i = 0; i < array->get_size(); ++i) {
        const mi::base::Handle<const IValue> itemValue(array->get_value(i));
        const mi::base::Handle<const IValue_enum> item(
            itemValue ? itemValue->get_interface<const IValue_enum>() : nullptr);
        if (!item || !item->get_name()) {
            return false;
        }
        names.push_back(item->get_name());
    }
    value = VtValue(names);
    return true;
}

void _NormalizeSdrType(const SdfValueTypeName& target, TfToken& type, size_t& arraySize)
{
    const SdfValueTypeName scalar = target.GetScalarType();
    if (scalar == SdfValueTypeNames->Bool || scalar == SdfValueTypeNames->Int) {
        type = SdrPropertyTypes->Int;
    } else if (scalar == SdfValueTypeNames->String || scalar == SdfValueTypeNames->Token
               || scalar == SdfValueTypeNames->Asset) {
        type = SdrPropertyTypes->String;
    } else if (scalar == SdfValueTypeNames->Float) {
        type = SdrPropertyTypes->Float;
    } else if (scalar == SdfValueTypeNames->Color3f) {
        type = SdrPropertyTypes->Color;
    } else if (scalar == SdfValueTypeNames->Color4f) {
        type = SdrPropertyTypes->Color4;
    } else if (scalar == SdfValueTypeNames->Normal3f) {
        type = SdrPropertyTypes->Normal;
    } else if (scalar == SdfValueTypeNames->Point3f) {
        type = SdrPropertyTypes->Point;
    } else if (scalar == SdfValueTypeNames->Vector3f) {
        type = SdrPropertyTypes->Vector;
    } else if (scalar == SdfValueTypeNames->Matrix4d) {
        type = SdrPropertyTypes->Matrix;
    } else if (!target.IsArray()) {
        if (scalar == SdfValueTypeNames->Float2 || scalar == SdfValueTypeNames->Float3
            || scalar == SdfValueTypeNames->Float4) {
            type = SdrPropertyTypes->Float;
            arraySize = scalar == SdfValueTypeNames->Float2 ? 2
                : scalar == SdfValueTypeNames->Float3 ? 3 : 4;
            return;
        }
        if (scalar == SdfValueTypeNames->Int2 || scalar == SdfValueTypeNames->Int3
            || scalar == SdfValueTypeNames->Int4) {
            type = SdrPropertyTypes->Int;
            arraySize = scalar == SdfValueTypeNames->Int2 ? 2
                : scalar == SdfValueTypeNames->Int3 ? 3 : 4;
            return;
        }
    }
    if (!target.IsArray()) {
        arraySize = 0;
    }
}


}

bool UsdMdlPropertyConversion::FinalizeProperty(
    const mi::neuraylib::IType* mdlType,
    const mi::neuraylib::IExpression* mdlDefault,
    const SdfValueTypeName& nativeSdfType,
    TfToken& sdrType,
    size_t& arraySize,
    VtValue& defaultValue,
    SdrTokenMap& metadata,
    SdrOptionVec& options,
    const DebugContext& debugContext)
{
    const auto requested = metadata.find(SdrPropertyMetadata->SdrUsdDefinitionType);
    if (requested == metadata.end()) {
        return true;
    }
    const SdfValueTypeName target = SdfSchema::GetInstance().FindType(requested->second);
    if (!target) {
        return _Reject(debugContext, requested->second, "unknown USD type");
    }
    if (!mdlType || !nativeSdfType) {
        return _Reject(debugContext, requested->second, "missing original MDL type information");
    }
    bool isEnumArray = false;
    const mi::base::Handle<const IType_enum> enumType = _GetEnumType(mdlType, isEnumArray);
    const SdfValueTypeName scalar = target.GetScalarType();
    const bool symbolicEnum = enumType
        && (scalar == SdfValueTypeNames->Token || scalar == SdfValueTypeNames->String);
    const bool stringToken = nativeSdfType.GetScalarType() == SdfValueTypeNames->String
        && scalar == SdfValueTypeNames->Token;
    if (target.IsArray() != nativeSdfType.IsArray()
        || (!symbolicEnum && !stringToken && target.GetType() != nativeSdfType.GetType())) {
        return _Reject(debugContext, requested->second, "incompatible value type or array shape");
    }
    // USD 25.11 does not register string-array/token-array casts. Supplying
    // symbolic string arrays to Sdr would silently replace the USD default.
    if (target == SdfValueTypeNames->TokenArray && (symbolicEnum || stringToken)) {
        return _Reject(debugContext, requested->second, "symbolic token arrays are not supported");
    }
    const auto dynamic = metadata.find(SdrPropertyMetadata->IsDynamicArray);
    if (!target.IsArray() && dynamic != metadata.end() && ShaderMetadataHelpers::IsTruthy(SdrPropertyMetadata->IsDynamicArray, metadata)) {
        return _Reject(debugContext, requested->second, "scalar USD type has dynamic-array metadata");
    }

    TfToken convertedType = sdrType;
    size_t convertedSize = arraySize;
    VtValue convertedDefault = defaultValue;
    SdrOptionVec convertedOptions = options;
    SdrTokenMap convertedMetadata = metadata;

    if (symbolicEnum) {
        if (!_ConvertEnumDefault(mdlDefault, isEnumArray, convertedDefault)) {
            return _Reject(debugContext, requested->second, "enum default has no recoverable symbolic value");
        }
        convertedOptions.clear();
        for (mi::Size i = 0; i < enumType->get_size(); ++i) {
            const char* name = enumType->get_value_name(i);
            if (!name) {
                return _Reject(debugContext, requested->second, "enum option has no name");
            }
            convertedOptions.emplace_back(TfToken(name), TfToken(name));
        }
    }
    if (!convertedDefault.IsEmpty()) {
        // Enum names and strings stay strings in Sdr; OpenUSD converts the
        // scalar string to TfToken for GetDefaultValueAsSdfType.
        const SdfValueTypeName storageType = scalar == SdfValueTypeNames->Token
            && (symbolicEnum || stringToken) ? SdfValueTypeNames->String : scalar;
        const SdfValueTypeName storage = target.IsArray() ? storageType.GetArrayType() : storageType;
        VtValue compatible = VtValue::CastToTypeid(convertedDefault, storage.GetType().GetTypeid());
        if (compatible.IsEmpty()) {
            return _Reject(debugContext, requested->second, "default cannot be converted without losing its shape");
        }
        convertedDefault = compatible;
        if (target == SdfValueTypeNames->Bool) {
            convertedDefault = VtValue(convertedDefault.UncheckedGet<bool>() ? 1 : 0);
        }
    }
    // Struct/distribution token placeholders retain their original Sdr kind.
    if (nativeSdfType != SdfValueTypeNames->Token && nativeSdfType != SdfValueTypeNames->TokenArray) {
        _NormalizeSdrType(target, convertedType, convertedSize);
    }
    if (target.IsArray() && convertedSize == 0) {
        if (dynamic != metadata.end() && !ShaderMetadataHelpers::IsTruthy(SdrPropertyMetadata->IsDynamicArray, metadata)) {
            return _Reject(debugContext, requested->second, "zero-sized array requires array metadata");
        }
        convertedMetadata[SdrPropertyMetadata->IsDynamicArray] = "1";
    }
    convertedMetadata[SdrPropertyMetadata->SdrUsdDefinitionType] = target.GetAsToken().GetString();
    sdrType = convertedType;
    arraySize = convertedSize;
    defaultValue = std::move(convertedDefault);
    metadata = std::move(convertedMetadata);
    options = std::move(convertedOptions);
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
