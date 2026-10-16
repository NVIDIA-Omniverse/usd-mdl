//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MDL_CONVERTER_ARRAY_H
#define MDL_CONVERTER_ARRAY_H

#include "converter.h"
#include "converterUtils.h"
#include "enum.h"
#include "matrix.h"
#include "texture.h"

#include <pxr/base/tf/diagnostic.h>

#include <algorithm>
#include <type_traits>

PXR_NAMESPACE_OPEN_SCOPE

#define ERROR_GET_INDEXED_VALUE(PREFIX, T, IDX) ERROR_GET_INDEXED_ITEM(PREFIX, IValue_array, T, IDX)

class MdlModule;

class ArrayConverter final : public Converter
{
public:
    /// Constructs a converter for MDL array type.
    ArrayConverter(const mi::neuraylib::IType* const iType, const MdlModule* mdlModule, const DebugContext& debugCtx)
        : Converter(iType, mdlModule, debugCtx)
        , _isDynamic(false)
        , _elementKind(mi::neuraylib::IType::TK_BOOL)
        , _inputArraySize(0)
        , _elementSize(1)
    {
        // Boolean vector ITypes, non-square and float matrices
        // do not have a 1-1 mapping with a USD type.  This means that we are
        // storing these types as arrays.
        // This means the array size of the output VtArray will not be equivalent
        // to the size on the input ITypeArray.
        // We compensate for this below by using separate variables to hold
        // the input and output array sizes.

        using namespace mi::neuraylib;

        // early exit if upstream constructor failed.
        if (!_valid)
        {
            return;
        }

        _valid = false;

        const mi::base::Handle<const IType_array> iTypeArray(_iType.get_interface<const IType_array>());

        if (!iTypeArray)
        {
            ERROR_GET_INTERFACE(_debugCtx, IType, IType_array);

            return;
        }

        _isDynamic = !iTypeArray->is_immediate_sized();

        const mi::base::Handle<const IType> elementIType(iTypeArray->get_element_type());

        if (!elementIType)
        {
            ERROR_GET_ITEM(_debugCtx, IType_array, IType);
            return;
        }

        _elementConverter = Converter::GetConverter(elementIType.get(), _module, _debugCtx);

        if (!_elementConverter)
        {
            return;
        }

        if (_isDynamic)
        {
            _deferredSizeSymbol = TfSafeString(iTypeArray->get_deferred_size());
        }
        else
        {
            _inputArraySize = iTypeArray->get_size();
        }

        _elementSize = 1;

        const auto& elementType = _elementConverter->GetSdfType();

        const mi::base::Handle<const IType> elementUnaliasedIType(elementIType->skip_all_type_aliases());

        _elementKind = elementUnaliasedIType->get_kind();

        switch (_elementKind)
        {
        case IType::TK_BOOL:
            _sdf_type = SdfValueTypeNames->BoolArray;
            break;

        case IType::TK_BSDF_MEASUREMENT:
            _sdf_type = SdfValueTypeNames->AssetArray;
            break;

        case IType::TK_COLOR:
            _sdf_type = SdfValueTypeNames->Color3fArray;
            break;

        case IType::TK_DOUBLE:
            _sdf_type = SdfValueTypeNames->DoubleArray;
            break;

        case IType::TK_ENUM:
            _sdf_type = SdfValueTypeNames->IntArray;
            break;

        case IType::TK_FLOAT:
            _sdf_type = SdfValueTypeNames->FloatArray;
            break;

        case IType::TK_INT:
            _sdf_type = SdfValueTypeNames->IntArray;
            break;

        case IType::TK_LIGHT_PROFILE:
            _sdf_type = SdfValueTypeNames->AssetArray;
            break;

        case IType::TK_MATRIX:
            if ((elementType == SdfValueTypeNames->DoubleArray) ||
                (elementType == SdfValueTypeNames->FloatArray))
            {
                _sdf_type = elementType;
                _elementSize = _elementConverter->GetArraySize();
            }
            else if (elementType == SdfValueTypeNames->Matrix2d)
                _sdf_type = SdfValueTypeNames->Matrix2dArray;

            else if (elementType == SdfValueTypeNames->Matrix3d)
                _sdf_type = SdfValueTypeNames->Matrix3dArray;

            else
                _sdf_type = SdfValueTypeNames->Matrix4dArray;

            break;

        case IType::TK_STRING:
            _sdf_type = SdfValueTypeNames->StringArray;
            break;

        case IType::TK_STRUCT:
            _sdf_type = SdfValueTypeNames->TokenArray;
            break;

        case IType::TK_TEXTURE:
            _sdf_type = SdfValueTypeNames->AssetArray;
            break;

        case IType::TK_VECTOR:
            if (elementType == SdfValueTypeNames->BoolArray)
            {
                _sdf_type = elementType;
                _elementSize = _elementConverter->GetArraySize();
            }
            else if (elementType == SdfValueTypeNames->Double2)
                _sdf_type = SdfValueTypeNames->Double2Array;

            else if (elementType == SdfValueTypeNames->Double3)
                _sdf_type = SdfValueTypeNames->Double3Array;

            else if (elementType == SdfValueTypeNames->Double4)
                _sdf_type = SdfValueTypeNames->Double4Array;

            else if (elementType == SdfValueTypeNames->Float2)
                _sdf_type = SdfValueTypeNames->Float2Array;

            else if (elementType == SdfValueTypeNames->Float3)
                _sdf_type = SdfValueTypeNames->Float3Array;

            else if (elementType == SdfValueTypeNames->Float4)
                _sdf_type = SdfValueTypeNames->Float4Array;

            else if (elementType == SdfValueTypeNames->Int2)
                _sdf_type = SdfValueTypeNames->Int2Array;

            else if (elementType == SdfValueTypeNames->Int3)
                _sdf_type = SdfValueTypeNames->Int3Array;

            else
                _sdf_type = SdfValueTypeNames->Int4Array;

            break;

        default:
            ERROR_COMPOUND_NOT_SUPPORTED(
                _debugCtx, IType_array, TfStringify(_elementKind), TfStringify(_inputArraySize * _elementSize));

            return;
        }

        const std::string& elementSymbol = _elementConverter->GetSymbol();
        if (!elementSymbol.empty())
        {
            _symbol = elementSymbol;
        }

        if (_elementKind == IType::TK_STRUCT)
        {
            _type = SdrPropertyTypes->Struct;
        }
        else
        {
            _type = _sdf_type.GetAsToken();
        }
        _renderType = UsdMdlTypes->Array;
        _elementRenderType = _elementConverter->GetRenderType();
        _valid = true;
    }

    /// Returns the total element count for fixed-size arrays, or zero for dynamic arrays.
    size_t GetArraySize() const override
    {
        return _isDynamic ? 0 : _inputArraySize * _elementSize;
    }

    /// Converts an MDL IValue_array to a VtValue for this type.
    VtValue ConvertValue(const mi::neuraylib::IValue* const iValue, VtDictionary* const metadata = nullptr) const override
    {
        using namespace mi::neuraylib;

        const mi::base::Handle<const IValue_array> iValueArray(
            iValue->get_interface<IValue_array>());

        if (!iValueArray)
        {
            return VtValue();
        }

        switch (_elementKind)
        {
        case IType::TK_BOOL:
            return _BuildArray<bool, bool>(iValue);

        case IType::TK_BSDF_MEASUREMENT:
            return _BuildArrayFromConverter<SdfAssetPath>(iValue);

        case IType::TK_COLOR:
            return _BuildArrayFromConverter<GfVec3f>(iValue);

        case IType::TK_DOUBLE:
            return _BuildArray<mi::Float64, mi::Float64>(iValue);

        case IType::TK_ENUM:
            return _BuildArray<mi::Sint32, mi::Sint32>(iValue);

        case IType::TK_FLOAT:
            return _BuildArray<mi::Float32, mi::Float32>(iValue);

        case IType::TK_INT:
            return _BuildArray<mi::Sint32, mi::Sint32>(iValue);

        case IType::TK_LIGHT_PROFILE:
            return _BuildArrayFromConverter<SdfAssetPath>(iValue);

        case IType::TK_MATRIX:
            if (_elementRenderType == UsdMdlTypes->Double2x2)
                return _BuildArray<mi::math::Matrix<double, 2, 2>, GfMatrix2d>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Double2x3)
                return _BuildArray<mi::math::Matrix<double, 2, 3>, double>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Double2x4)
                return _BuildArray<mi::math::Matrix<double, 2, 4>, double>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Double3x2)
                return _BuildArray<mi::math::Matrix<double, 3, 2>, double>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Double3x3)
                return _BuildArray<mi::math::Matrix<double, 3, 3>, GfMatrix3d>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Double3x4)
                return _BuildArray<mi::math::Matrix<double, 3, 4>, double>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Double4x2)
                return _BuildArray<mi::math::Matrix<double, 4, 2>, double>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Double4x3)
                return _BuildArray<mi::math::Matrix<double, 4, 3>, double>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Double4x4)
                return _BuildArray<mi::math::Matrix<double, 4, 4>, GfMatrix4d>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Float2x2)
                return _BuildArray<mi::math::Matrix<float, 2, 2>, float>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Float2x3)
                return _BuildArray<mi::math::Matrix<float, 2, 3>, float>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Float2x4)
                return _BuildArray<mi::math::Matrix<float, 2, 4>, float>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Float3x2)
                return _BuildArray<mi::math::Matrix<float, 3, 2>, float>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Float3x3)
                return _BuildArray<mi::math::Matrix<float, 3, 3>, float>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Float3x4)
                return _BuildArray<mi::math::Matrix<float, 3, 4>, float>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Float4x2)
                return _BuildArray<mi::math::Matrix<float, 4, 2>, float>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Float4x3)
                return _BuildArray<mi::math::Matrix<float, 4, 3>, float>(iValue);

            else
                return _BuildArray<mi::math::Matrix<float, 4, 4>, float>(iValue);

        case IType::TK_STRING:
            return _BuildArrayFromConverter<std::string>(iValue);

        case IType::TK_STRUCT:
            // Empty arrays carry no struct fields. Nonempty struct values have
            // no literal USD representation; unavailable is distinct from [].
            return iValueArray->get_size() == 0 ? VtValue(VtTokenArray()) : VtValue();

        case IType::TK_TEXTURE:
            return _BuildArrayFromConverter<SdfAssetPath>(iValue, metadata);

        case IType::TK_VECTOR:
            if (_elementRenderType == UsdMdlTypes->Bool2)
                return _BuildArray<mi::math::Vector<bool, 2>, bool>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Bool3)
                return _BuildArray<mi::math::Vector<bool, 3>, bool>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Bool4)
                return _BuildArray<mi::math::Vector<bool, 4>, bool>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Double2)
                return _BuildArray<mi::math::Vector<mi::Float64, 2>, GfVec2d>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Double3)
                return _BuildArray<mi::math::Vector<mi::Float64, 3>, GfVec3d>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Double4)
                return _BuildArray<mi::math::Vector<mi::Float64, 4>, GfVec4d>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Float2)
                return _BuildArray<mi::math::Vector<mi::Float32, 2>, GfVec2f>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Float3)
                return _BuildArray<mi::math::Vector<mi::Float32, 3>, GfVec3f>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Float4)
                return _BuildArray<mi::math::Vector<mi::Float32, 4>, GfVec4f>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Int2)
                return _BuildArray<mi::math::Vector<mi::Sint32, 2>, GfVec2i>(iValue);

            else if (_elementRenderType == UsdMdlTypes->Int3)
                return _BuildArray<mi::math::Vector<mi::Sint32, 3>, GfVec3i>(iValue);

            else
                return _BuildArray<mi::math::Vector<mi::Sint32, 4>, GfVec4i>(iValue);

        default:
            ERROR_COMPOUND_NOT_SUPPORTED(
                _debugCtx, IValue_array, TfStringify(_elementKind), TfStringify(_inputArraySize * _elementSize));

            return VtValue();
        }
    }

    /// Returns enum option pairs from the element converter.
    const SdrOptionVec& GetOptions() const override
    {
        return _elementConverter->GetOptions();
    }

    /// Populates Sdr metadata specific to this type.
    void GetMetadata(SdrTokenMap& metadataMap, VtDictionary* const metadataDict = nullptr) const override
    {
        // store
        // 1. IsAssetIdentifier, if applicable
        // 2. IsDynamicArray, if applicable
        // 3. ArrayDeferredSizeSymbol, if applicable
        // 4. ArrayElementType

        using namespace mi::neuraylib;

        if ((_elementKind == IType::TK_BSDF_MEASUREMENT) || (_elementKind == IType::TK_LIGHT_PROFILE) ||
            (_elementKind == IType::TK_TEXTURE))
        {
            metadataMap.emplace(std::make_pair(SdrPropertyMetadata->IsAssetIdentifier, "1"));
        }

        if (_isDynamic)
        {
            metadataMap.emplace(SdrPropertyMetadata->IsDynamicArray, "");

            metadataMap.emplace(UsdMdlMetadataTokens->ArrayDeferredSizeSymbol, _deferredSizeSymbol);
        }

        metadataMap.emplace(UsdMdlMetadataTokens->ArrayElementType, _elementRenderType.GetString());
        metadataMap.emplace(UsdMdlMetadataTokens->ArrayIsImmediateSized, _isDynamic ? "false" : "true");
        if (!_isDynamic) {
            metadataMap.emplace(UsdMdlMetadataTokens->ArraySize, std::to_string(_inputArraySize));
        }

        Converter::GetMetadata(metadataMap, metadataDict);
    }

private:
    std::pair<mi::Size, mi::Size> _GetInputOutputArraySizes(const mi::neuraylib::IValue* const iValue) const
    {
        using namespace mi::neuraylib;

        if (!_isDynamic)
        {
            return std::make_pair(_inputArraySize, _inputArraySize * _elementSize);
        }

        const mi::base::Handle<const IValue_array> iValueArray(
            iValue->get_interface<IValue_array>());

        if (!iValueArray)
        {
            ERROR_GET_INTERFACE(_debugCtx, IValue, IValue_array);
            return std::make_pair(0, 0);
        }

        const mi::Size inputSize = iValueArray->get_size();
        return std::make_pair(inputSize, inputSize * _elementSize);
    }

    template <class NEURAY_TYPE, class ARRAY_TYPE>
    VtValue _BuildArray(const mi::neuraylib::IValue* const iValue) const
    {
        using namespace mi::neuraylib;

        const std::pair<mi::Size, mi::Size> inOutSizes = _GetInputOutputArraySizes(iValue);

        if (inOutSizes.first == 0) {
            return VtValue(VtArray<ARRAY_TYPE>());
        }

        VtArray<ARRAY_TYPE> a(inOutSizes.second);
        const size_t offset = inOutSizes.second / inOutSizes.first;

        // Copy elements without assuming SDK and USD types share a memory
        // layout or accessing storage through an unrelated type. Three cases:
        //   1. NEURAY_TYPE arithmetic (offset==1): direct slot assignment.
        //   2. ARRAY_TYPE arithmetic (scalar fanout): copy `offset` values
        //      from v's storage into the next `offset` flat slots.
        //   3. NEURAY_TYPE and ARRAY_TYPE both compound (e.g.
        //      mi::math::Vector<float,3> -> GfVec3f): copy v's elements
        //      into the Gf type's flat data() buffer.
        for (mi::Size i = 0, idx = 0; i < inOutSizes.first; ++i, idx += offset)
        {
            NEURAY_TYPE v;
            if (get_value<NEURAY_TYPE>(iValue, i, v) != 0)
            {
                ERROR_GET_INDEXED_VALUE(_debugCtx, NEURAY_TYPE, i);
                return VtValue();
            }
            if constexpr (std::is_arithmetic_v<NEURAY_TYPE>) {
                a[idx] = v;
            } else if constexpr (std::is_arithmetic_v<ARRAY_TYPE>) {
                std::copy(v.begin(), v.end(), a.begin() + idx);
            } else {
                std::copy(v.begin(), v.end(), a[idx].data());
            }
        }

        return VtValue(a);
    }

    // Each converted MDL element occupies one USD slot (including Gf colors).
    // Flattened vectors/matrices use _BuildArray instead. Keep metadata slots
    // aligned with values so a missing selector cannot shift later elements.
    template <class ARRAY_TYPE>
    VtValue _BuildArrayFromConverter(const mi::neuraylib::IValue* const iValue, VtDictionary* const metadata = nullptr) const
    {
        using namespace mi::neuraylib;

        const mi::base::Handle<const IValue_array> iValueArray(
            iValue->get_interface<IValue_array>());

        if (!iValueArray)
        {
            ERROR_GET_INTERFACE(_debugCtx, IValue, IValue_array);
            return VtValue();
        }

        const std::pair<mi::Size, mi::Size> inOutSizes = _GetInputOutputArraySizes(iValue);

        VtArray<ARRAY_TYPE> a(inOutSizes.second);

        std::vector<VtDictionary> itemMetadataVec(metadata ? inOutSizes.first : 0);

        for (mi::Size i = 0; i < inOutSizes.first; ++i)
        {
            const mi::base::Handle<const IValue> component(iValueArray->get_value(i));

            if (!component)
            {
                ERROR_GET_INDEXED_ITEM(_debugCtx, IValue_array, IValue, i);
                return VtValue();
            }

            VtDictionary* const itemMetadata = metadata ? &itemMetadataVec[i] : nullptr;

            const VtValue itemValue = _elementConverter->ConvertValue(component.get(), itemMetadata);

            if (itemValue.IsEmpty())
            {
                return VtValue();
            }

            if (!itemValue.IsHolding<ARRAY_TYPE>())
            {
                ERROR_VTVALUE_GET(_debugCtx, ARRAY_TYPE, itemValue);
                return VtValue();
            }

            a[i] = itemValue.UncheckedGet<ARRAY_TYPE>();
        }

        // process item metadata
        if (metadata)
        {
            ConverterUtils::SetArrayMetadataValues(itemMetadataVec, *metadata);
        }

        return VtValue(a);
    }

private:
    bool _isDynamic;
    std::string _deferredSizeSymbol;
    TfToken _elementRenderType;
    mi::neuraylib::IType::Kind _elementKind;
    std::unique_ptr<Converter> _elementConverter;
    size_t _inputArraySize;
    size_t _elementSize;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // MDL_CONVERTER_ARRAY_H
