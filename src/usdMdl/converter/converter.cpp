//
// SPDX-License-Identifier: Apache-2.0
//

#include "converter.h"

#include "../annotation.h"
#include "../debugCodes.h"
#include "tokens.h"

#include <mi/mdl_sdk.h>

#include "array.h"
#include "bsdfMeasurement.h"
#include "color.h"
#include "distribution.h"
#include "enum.h"
#include "lightProfile.h"
#include "matrix.h"
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/usd/sdf/types.h>
#include "scalar.h"
#include "struct.h"
#include "texture.h"
#include "vector.h"

using namespace mi::neuraylib;

PXR_NAMESPACE_OPEN_SCOPE

std::unique_ptr<Converter> Converter::GetConverter(const IType* const iType, const MdlModule* mdlModule, const DebugContext& debugCtx)
{
    if (!iType) {
        return nullptr;
    }
    const mi::base::Handle<const IType> unaliasedIType(iType->skip_all_type_aliases());
    if (!unaliasedIType)
    {
        TF_ERROR(USD_MDL, "%s\tCannot get unaliased '%s'.", debugCtx.format().c_str(), typeid(IType).name());

        return std::unique_ptr<Converter>(nullptr);
    }

    std::unique_ptr<Converter> converter;
    const IType::Kind kind = unaliasedIType->get_kind();

    switch (kind)
    {
    case IType::TK_ARRAY:
        converter = std::make_unique<ArrayConverter>(iType, mdlModule, debugCtx);
        break;

    case IType::TK_BOOL:
        converter = std::make_unique<ScalarConverter<bool>>(
            iType, SdfValueTypeNames->Bool, UsdMdlTypes->Bool, debugCtx);
        break;

    case IType::TK_BSDF:
        converter = std::make_unique<BsdfConverter>(iType, debugCtx);
        break;

    case IType::TK_BSDF_MEASUREMENT:
        converter = std::make_unique<BsdfMeasurementConverter>(iType, mdlModule, debugCtx);
        break;

    case IType::TK_COLOR:
        converter = std::make_unique<ColorConverter>(iType, debugCtx);
        break;

    case IType::TK_DOUBLE:
        converter = std::make_unique<ScalarConverter<mi::Float64>>(
            iType, SdfValueTypeNames->Double, UsdMdlTypes->Double, debugCtx);
        break;

    case IType::TK_EDF:
        converter = std::make_unique<EdfConverter>(iType, debugCtx);
        break;

    case IType::TK_ENUM:
        converter = std::make_unique<EnumConverter>(iType, debugCtx);
        break;

    case IType::TK_FLOAT:
        converter = std::make_unique<ScalarConverter<mi::Float32>>(
            iType, SdfValueTypeNames->Float, UsdMdlTypes->Float, debugCtx);
        break;

    case IType::TK_HAIR_BSDF:
        converter = std::make_unique<HairBsdfConverter>(iType, debugCtx);
        break;

    case IType::TK_INT:
        converter = std::make_unique<ScalarConverter<mi::Sint32>>(
            iType, SdfValueTypeNames->Int, UsdMdlTypes->Int, debugCtx);
        break;

    case IType::TK_LIGHT_PROFILE:
        converter = std::make_unique<LightProfileConverter>(iType, mdlModule, debugCtx);
        break;

    case IType::TK_MATRIX:
        converter = std::make_unique<MatrixConverter>(iType, debugCtx);
        break;

    case IType::TK_STRING:
        converter = std::make_unique<ScalarConverter<const char*>>(
            iType, SdfValueTypeNames->String, UsdMdlTypes->String, debugCtx);
        break;

    case IType::TK_STRUCT:
        converter = std::make_unique<StructConverter>(iType, mdlModule, debugCtx);
        break;

    case IType::TK_TEXTURE:
        converter = std::make_unique<TextureConverter>(iType, mdlModule, debugCtx);
        break;

    case IType::TK_VDF:
        converter = std::make_unique<VdfConverter>(iType, debugCtx);
        break;

    case IType::TK_VECTOR:
        converter = std::make_unique<VectorConverter>(iType, debugCtx);
        break;

    default:
        TF_WARN(USD_MDL, "%s\tCannot find converter for unsupported kind: '%d'.", debugCtx.format().c_str(), kind);
        return nullptr;
    }

    // If the converter failed during initialization, drop it.
    if (converter && !converter->_valid) {
        converter.reset();
    }

    return converter;
}

VtValue Converter::ConvertConstantExpression(const IExpression* const expression,
                                             const MdlModule* mdlModule,
                                             const DebugContext& debugCtx)
{
    if (!expression || expression->get_kind() != IExpression::EK_CONSTANT) {
        return VtValue();
    }
    const mi::base::Handle<const IType> type(expression->get_type());
    if (std::unique_ptr<Converter> converter = GetConverter(type.get(), mdlModule, debugCtx)) {
        return converter->ConvertDefault(expression, nullptr, nullptr);
    }
    return VtValue();
}

std::vector<Converter::Output> Converter::GetOutputs(const IType* const iType,
                                                     const IAnnotation_block* const returnAnnotation,
                                                     const MdlModule* mdlModule,
                                                     const DebugContext& debugCtx)
{
    if (!iType) {
        return {};
    }
    const std::vector<std::string> outputNames = {UsdMdlTokens->DefaultOutputPortName.GetString()};

    const mi::base::Handle<const IType> unaliasedIType(iType->skip_all_type_aliases());
    if (!unaliasedIType) {
        return {};
    }
    if (unaliasedIType->get_kind() == IType::TK_STRUCT)
    {
        return StructConverter::GetOutputs(iType, returnAnnotation, outputNames, mdlModule, debugCtx);
    }

    const DebugContext outputDebugCtx{&debugCtx, "output", UsdMdlTokens->DefaultOutputPortName.GetText()};

    std::vector<Converter::Output> outputs;
    for (const std::string& name: outputNames)
    {
        outputs.emplace_back(Converter::GetConverter(iType, mdlModule, outputDebugCtx), name);
    }

    return outputs;
}

Converter::Converter(const IType* const iType, const MdlModule* mdlModule, const DebugContext& debugCtx)
    : _iType(iType->skip_all_type_aliases()),
      _module(mdlModule),
      _debugCtx(debugCtx),
      _type(SdrPropertyTypes->Unknown),
      _kind(iType->get_kind()),
      _valid(true)
{
    const mi::Uint32 typeModifiers = iType->get_all_type_modifiers();

    if (typeModifiers != IType::MK_NONE)
    {
        if ((typeModifiers & IType::MK_UNIFORM) != 0)
        {
            _modifier = UsdMdlTypeModifiers->Uniform;
        }
        else if ((typeModifiers & IType::MK_VARYING) != 0)
        {
            _modifier = UsdMdlTypeModifiers->Varying;
        }
    }

    if (_kind == IType::TK_ALIAS)
    {
        const mi::base::Handle<const IType_alias> iTypeAlias(iType->get_interface<const IType_alias>());

        if (iTypeAlias)
        {
            _symbol = TfSafeString(iTypeAlias->get_symbol());
        }
        else
        {
            ERROR_GET_INTERFACE(_debugCtx, IType, IType_alias);
            _valid = false;
        }
    }
}

VtValue Converter::ConvertDefault(const IExpression* const expression,
                                  ITransaction* transaction,
                                  VtDictionary* const metadata) const
{
    if (!expression) {
        return VtValue();
    }
    if (expression->get_kind() == IExpression::EK_CONSTANT) {
        // Preserve the SDK kind even when the value has no Sdr representation.
        if (metadata) {
            metadata->SetValueAtPath(UsdMdlMetadataTokens->ExpressionKind.GetString(),
                                     VtValue(UsdMdlExpressionKinds->Constant));
        }
        const mi::base::Handle<const IExpression_constant> constant(
            expression->get_interface<IExpression_constant>());
        if (!constant) {
            return VtValue();
        }
        const mi::base::Handle<const IValue> value(constant->get_value());
        return value ? ConvertValue(value.get(), metadata) : VtValue();
    }

    // Nonconstant defaults are unavailable. Record only the root expression's
    // identity; do not evaluate calls, constructors, or parameter references.
    if (metadata && expression->get_kind() == IExpression::EK_PARAMETER) {
        const mi::base::Handle<const IExpression_parameter> parameter(
            expression->get_interface<IExpression_parameter>());
        if (!parameter) {
            return VtValue();
        }
        metadata->SetValueAtPath(UsdMdlMetadataTokens->ExpressionKind.GetString(),
                                 VtValue(UsdMdlExpressionKinds->Parameter));
        metadata->SetValueAtPath(UsdMdlMetadataTokens->ExpressionValue.GetString(),
                                 VtValue(parameter->get_index()));
    }
    else if (metadata && expression->get_kind() == IExpression::EK_CALL) {
        metadata->SetValueAtPath(UsdMdlMetadataTokens->ExpressionKind.GetString(),
                                 VtValue(UsdMdlExpressionKinds->Call));
        const mi::base::Handle<const IExpression_call> call(
            expression->get_interface<IExpression_call>());
        if (!call || !call->get_call()) {
            return VtValue();
        }
        if (transaction) {
            const mi::base::Handle<const IFunction_call> functionCall(
                transaction->access<IFunction_call>(call->get_call()));
            if (functionCall) {
                metadata->SetValueAtPath(UsdMdlMetadataTokens->ExpressionValue.GetString(),
                    VtValue(TfSafeString(functionCall->get_function_definition())));
            }
        }
    }
    return VtValue();
}

void Converter::GetMetadata(SdrTokenMap& metadataMap, VtDictionary* const) const
{
    // store
    // 1. RenderType
    // 2. Symbol, if set
    // 3. Modifier, if set

    // Standard renderType can be overridden by annotations or USD interfaces.
    // Preserve the SDK declaration independently for local connection checks.
    metadataMap[UsdMdlMetadataTokens->Type] = _renderType.GetString();
    metadataMap.emplace(std::make_pair(SdrPropertyMetadata->RenderType, _renderType.GetString()));

    if (!_symbol.empty())
    {
        metadataMap.emplace(std::make_pair(UsdMdlMetadataTokens->Symbol, _symbol));
    }

    if (!_modifier.IsEmpty())
    {
        metadataMap.emplace(std::make_pair(UsdMdlMetadataTokens->Modifier, _modifier.GetString()));
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
