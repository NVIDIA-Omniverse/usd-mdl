//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MDL_CONVERTER_VECTOR_H
#define MDL_CONVERTER_VECTOR_H

#include "converter.h"

#include <algorithm>

PXR_NAMESPACE_OPEN_SCOPE

class VectorConverter final : public Converter
{
public:
    /// Constructs a converter for MDL vector type.
    VectorConverter(const mi::neuraylib::IType* const iType, const DebugContext& debugCtx)
        : Converter(iType, nullptr, debugCtx)
    {
        // Usd does not have a boolean vector type so we store Bool[2|3|4] types
        // as Array's of Booleans. It will be left up to the consumer of this
        // data to handle accordingly.

        using namespace mi::neuraylib;

        // early exit if upstream constructor failed.
        if (!_valid)
        {
            return;
        }

        _valid = false;

        const mi::base::Handle<const IType_vector> iTypeVector(_iType.get_interface<const IType_vector>());

        if (!iTypeVector)
        {
            ERROR_GET_INTERFACE(_debugCtx, IType, IType_vector);
            return;
        }

        const mi::base::Handle<const IType_atomic> elementType(iTypeVector->get_element_type());

        if (!elementType)
        {
            ERROR_GET_ITEM(_debugCtx, IType_vector, IType_atomic);
            return;
        }

        _dimensions = iTypeVector->get_size();
        const IType::Kind elementKind = elementType->get_kind();

        if ((_dimensions >= 2) && (_dimensions <= 4))
        {
            switch (elementKind)
            {
            case IType::TK_BOOL:
                _sdf_type = SdfValueTypeNames->BoolArray;

                _renderType = (_dimensions == 2) ? UsdMdlTypes->Bool2 :
                              (_dimensions == 3) ? UsdMdlTypes->Bool3 :
                                                   UsdMdlTypes->Bool4;
                break;

            case IType::TK_INT:
                switch (_dimensions)
                {
                case 2:
                    _sdf_type = SdfValueTypeNames->Int2;
                    _renderType = UsdMdlTypes->Int2;
                    break;

                case 3:
                    _sdf_type = SdfValueTypeNames->Int3;
                    _renderType = UsdMdlTypes->Int3;
                    break;

                default:
                    _sdf_type = SdfValueTypeNames->Int4;
                    _renderType = UsdMdlTypes->Int4;
                    break;
                }

                break;

            case IType::TK_DOUBLE:
                switch (_dimensions)
                {
                case 2:
                    _sdf_type = SdfValueTypeNames->Double2;
                    _renderType = UsdMdlTypes->Double2;
                    break;

                case 3:
                    _sdf_type = SdfValueTypeNames->Double3;
                    _renderType = UsdMdlTypes->Double3;
                    break;

                default:
                    _sdf_type = SdfValueTypeNames->Double4;
                    _renderType = UsdMdlTypes->Double4;
                    break;
                }

                break;

            case IType::TK_FLOAT:
                switch (_dimensions)
                {
                case 2:
                    _sdf_type = SdfValueTypeNames->Float2;
                    _renderType = UsdMdlTypes->Float2;
                    break;

                case 3:
                    _sdf_type = SdfValueTypeNames->Float3;
                    _renderType = UsdMdlTypes->Float3;
                    break;

                default:
                    _sdf_type = SdfValueTypeNames->Float4;
                    _renderType = UsdMdlTypes->Float4;
                    break;
                }

                break;

            default:
                break;
            }
        }

        if (!_sdf_type)
        {
            ERROR_COMPOUND_NOT_SUPPORTED(_debugCtx, IType_vector, TfStringify(elementKind), TfStringify(_dimensions));
            return;
        }

        _type = _sdf_type.GetAsToken();
        _valid = true;
    }

    /// Returns the vector dimension for bool vectors (stored as arrays), zero otherwise.
    size_t GetArraySize() const override
    {
        if (_sdf_type == SdfValueTypeNames->BoolArray)
            return _dimensions;

        return 0;
    }

    /// Converts an MDL IValue to a VtValue for this type.
    VtValue ConvertValue(
        const mi::neuraylib::IValue* const iValue,
        VtDictionary* const = nullptr) const override
    {
        if (_renderType == UsdMdlTypes->Bool2)
            return _BuildVtArray<mi::math::Vector<bool, 2>>(iValue);

        else if (_renderType == UsdMdlTypes->Bool3)
            return _BuildVtArray<mi::math::Vector<bool, 3>>(iValue);

        else if (_renderType == UsdMdlTypes->Bool4)
            return _BuildVtArray<mi::math::Vector<bool, 4>>(iValue);

        else if (_renderType == UsdMdlTypes->Double2)
            return _BuildGfVec<GfVec2d>(iValue);

        else if (_renderType == UsdMdlTypes->Double3)
            return _BuildGfVec<GfVec3d>(iValue);

        else if (_renderType == UsdMdlTypes->Double4)
            return _BuildGfVec<GfVec4d>(iValue);

        else if (_renderType == UsdMdlTypes->Float2)
            return _BuildGfVec<GfVec2f>(iValue);

        else if (_renderType == UsdMdlTypes->Float3)
            return _BuildGfVec<GfVec3f>(iValue);

        else if (_renderType == UsdMdlTypes->Float4)
            return _BuildGfVec<GfVec4f>(iValue);

        else if (_renderType == UsdMdlTypes->Int2)
            return _BuildGfVec<GfVec2i>(iValue);

        else if (_renderType == UsdMdlTypes->Int3)
            return _BuildGfVec<GfVec3i>(iValue);

        else
            return _BuildGfVec<GfVec4i>(iValue);
    }

private:
    template <class GF_TYPE>
    VtValue _BuildGfVec(const mi::neuraylib::IValue* const iValue) const
    {
        using namespace mi::neuraylib;

        typedef mi::math::Vector<typename GF_TYPE::ScalarType, GF_TYPE::dimension> NEURAY_VECTOR_TYPE;

        NEURAY_VECTOR_TYPE v;

        if (get_value<typename GF_TYPE::ScalarType, GF_TYPE::dimension>(iValue, v) == 0)
        {
            GF_TYPE vec(v.begin());
            return VtValue(vec);
        }

        return VtValue();
    }

    template <class NEURAY_VECTOR_TYPE>
    VtValue _BuildVtArray(const mi::neuraylib::IValue* const iValue) const
    {
        using namespace mi::neuraylib;

        // Copy through the SDK vector's element iterators so conversion does
        // not depend on the SDK and USD containers sharing a memory layout.
        NEURAY_VECTOR_TYPE v;
        if (get_value<typename NEURAY_VECTOR_TYPE::value_type, NEURAY_VECTOR_TYPE::DIMENSION>(iValue, v) == 0)
        {
            VtArray<typename NEURAY_VECTOR_TYPE::value_type> a(NEURAY_VECTOR_TYPE::DIMENSION);
            std::copy(v.begin(), v.end(), a.begin());
            return VtValue(a);
        }

        return VtValue();
    }

private:
    size_t _dimensions;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // MDL_CONVERTER_VECTOR_H
