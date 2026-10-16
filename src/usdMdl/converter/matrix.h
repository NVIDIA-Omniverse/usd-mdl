//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MDL_CONVERTER_MATRIX_H
#define MDL_CONVERTER_MATRIX_H

#include "converter.h"

#include <algorithm>

PXR_NAMESPACE_OPEN_SCOPE

class MatrixConverter final : public Converter
{
public:
    /// Constructs a converter for MDL matrix type.
    MatrixConverter(const mi::neuraylib::IType* const iType, const DebugContext& debugCtx)
        : Converter(iType, nullptr, debugCtx)
    {
        /* Sdf matrix types exist only for square doubles: matrix[2|3|4]d.
           Non-square and/or float matrices will be stored as
           arrays and it will be left up to the consumer of this
           data to handle accordingly.
        */

        using namespace mi::neuraylib;

        // early exit if upstream constructor failed.
        if (!_valid)
        {
            return;
        }

        _valid = false;

        const mi::base::Handle<const IType_matrix> iTypeMatrix(_iType.get_interface<const IType_matrix>());
        if (!iTypeMatrix)
        {
            ERROR_GET_INTERFACE(_debugCtx, IType, IType_matrix);
            return;
        }

        const mi::base::Handle<const IType_vector> iTypeVector(iTypeMatrix->get_element_type());
        if (!iTypeVector)
        {
            ERROR_GET_ITEM(_debugCtx, IType_matrix, IType_vector);
            return;
        }

        const mi::base::Handle<const IType_atomic> elementType(iTypeVector->get_element_type());
        if (!elementType)
        {
            ERROR_GET_ITEM(_debugCtx, IType_vector, IType_atomic);
            return;
        }

        _columns = iTypeMatrix->get_size();
        _rows = iTypeVector->get_size();

        const IType::Kind elementKind = elementType->get_kind();

        if (((_columns < 2) || (_columns > 4)) || ((_rows < 2) || (_rows > 4)) ||
            ((elementKind != IType::TK_FLOAT) && (elementKind != IType::TK_DOUBLE)))
        {
            const std::string size = TfStringPrintf("(%llu, %llu)",
                static_cast<unsigned long long>(_rows),
                static_cast<unsigned long long>(_columns));

            ERROR_COMPOUND_NOT_SUPPORTED(_debugCtx, IType_matrix, TfStringify(elementKind), size);

            return;
        }

        const bool isDouble = (elementKind == IType::TK_DOUBLE);

        switch (_columns)
        {
        case 2:
            switch (_rows)
            {
            case 2:
                if (isDouble)
                {
                    _sdf_type = SdfValueTypeNames->Matrix2d;
                    _renderType = UsdMdlTypes->Double2x2;
                }
                else
                {
                    _sdf_type = SdfValueTypeNames->FloatArray;
                    _renderType = UsdMdlTypes->Float2x2;
                }

                break;

            case 3:
                if (isDouble)
                {
                    _sdf_type = SdfValueTypeNames->DoubleArray;
                    _renderType = UsdMdlTypes->Double2x3;
                }
                else
                {
                    _sdf_type = SdfValueTypeNames->FloatArray;
                    _renderType = UsdMdlTypes->Float2x3;
                }

                break;

            default:
                if (isDouble)
                {
                    _sdf_type = SdfValueTypeNames->DoubleArray;
                    _renderType = UsdMdlTypes->Double2x4;
                }
                else
                {
                    _sdf_type = SdfValueTypeNames->FloatArray;
                    _renderType = UsdMdlTypes->Float2x4;
                }

                break;
            }

            break;

        case 3:
            switch (_rows)
            {
            case 2:
                if (elementKind == IType::TK_DOUBLE)
                {
                    _sdf_type = SdfValueTypeNames->DoubleArray;
                    _renderType = UsdMdlTypes->Double3x2;
                }
                else
                {
                    _sdf_type = SdfValueTypeNames->FloatArray;
                    _renderType = UsdMdlTypes->Float3x2;
                }

                break;

            case 3:
                if (elementKind == IType::TK_DOUBLE)
                {
                    _sdf_type = SdfValueTypeNames->Matrix3d;
                    _renderType = UsdMdlTypes->Double3x3;
                }
                else
                {
                    _sdf_type = SdfValueTypeNames->FloatArray;
                    _renderType = UsdMdlTypes->Float3x3;
                }

                break;

            default:
                if (elementKind == IType::TK_DOUBLE)
                {
                    _sdf_type = SdfValueTypeNames->DoubleArray;
                    _renderType = UsdMdlTypes->Double3x4;
                }
                else
                {
                    _sdf_type = SdfValueTypeNames->FloatArray;
                    _renderType = UsdMdlTypes->Float3x4;
                }

                break;
            }

            break;

        default:
            switch (_rows)
            {
            case 2:
                if (elementKind == IType::TK_DOUBLE)
                {
                    _sdf_type = SdfValueTypeNames->DoubleArray;
                    _renderType = UsdMdlTypes->Double4x2;
                }
                else
                {
                    _sdf_type = SdfValueTypeNames->FloatArray;
                    _renderType = UsdMdlTypes->Float4x2;
                }

                break;

            case 3:
                if (elementKind == IType::TK_DOUBLE)
                {
                    _sdf_type = SdfValueTypeNames->DoubleArray;
                    _renderType = UsdMdlTypes->Double4x3;
                }
                else
                {
                    _sdf_type = SdfValueTypeNames->FloatArray;
                    _renderType = UsdMdlTypes->Float4x3;
                }

                break;

            default:
                if (elementKind == IType::TK_DOUBLE)
                {
                    _sdf_type = SdfValueTypeNames->Matrix4d;
                    _renderType = UsdMdlTypes->Double4x4;
                }
                else
                {
                    _sdf_type = SdfValueTypeNames->FloatArray;
                    _renderType = UsdMdlTypes->Float4x4;
                }

                break;
            }

            break;
        }

        _type = _sdf_type.GetAsToken();
        _valid = true;
    }

    /// Returns the flat element count for non-square or float matrices, zero for square double matrices.
    size_t GetArraySize() const override
    {
        if ((_renderType == UsdMdlTypes->Double2x2) || (_renderType == UsdMdlTypes->Double3x3) ||
            (_renderType == UsdMdlTypes->Double4x4))
        {
            return 0;
        }

        return _columns * _rows;
    }

    /// Converts an MDL IValue to a VtValue for this type.
    VtValue ConvertValue(
        const mi::neuraylib::IValue* const iValue,
        VtDictionary* const = nullptr) const override
    {
        if (_renderType == UsdMdlTypes->Double2x2)
            return _BuildGfMatrix<GfMatrix2d>(iValue);

        else if (_renderType == UsdMdlTypes->Double3x3)
            return _BuildGfMatrix<GfMatrix3d>(iValue);

        else if (_renderType == UsdMdlTypes->Double4x4)
            return _BuildGfMatrix<GfMatrix4d>(iValue);

        else if (_sdf_type == SdfValueTypeNames->DoubleArray)
            return _ConvertToArray<mi::Float64>(iValue);

        else
            return _ConvertToArray<mi::Float32>(iValue);
    }

private:
    template <class GF_TYPE>
    VtValue _BuildGfMatrix(const mi::neuraylib::IValue* const iValue) const
    {
        using namespace mi::neuraylib;

        typedef mi::math::Matrix<typename GF_TYPE::ScalarType, GF_TYPE::numRows, GF_TYPE::numColumns> NEURAY_MATRIX_TYPE;

        // Copy by row and column to preserve matrix coordinates independently
        // of the SDK and GfMatrix storage layouts, without aliasing their types.
        NEURAY_MATRIX_TYPE v;
        if (get_value<typename GF_TYPE::ScalarType, GF_TYPE::numRows, GF_TYPE::numColumns>(iValue, v) == 0)
        {
            GF_TYPE mtx;
            for (int r = 0; r < GF_TYPE::numRows; ++r) {
                for (int c = 0; c < GF_TYPE::numColumns; ++c) {
                    mtx[r][c] = v(r, c);
                }
            }
            return VtValue(mtx);
        }

        return VtValue();
    }

    template <class NEURAY_MATRIX_TYPE>
    VtValue _BuildVtArray(const mi::neuraylib::IValue* const iValue) const
    {
        using namespace mi::neuraylib;

        // mi::math::Matrix exposes a [begin, end) range over its element
        // storage in declaration (row-major) order.  std::copy from there
        // into the VtArray is well-defined regardless of byte layout.
        NEURAY_MATRIX_TYPE v;
        if (get_value<typename NEURAY_MATRIX_TYPE::value_type, NEURAY_MATRIX_TYPE::ROWS, NEURAY_MATRIX_TYPE::COLUMNS>(
                iValue, v) == 0)
        {
            VtArray<typename NEURAY_MATRIX_TYPE::value_type> a(NEURAY_MATRIX_TYPE::SIZE);
            std::copy(v.begin(), v.end(), a.begin());
            return VtValue(a);
        }

        return VtValue();
    }

    template <class NEURAY_FLOAT_TYPE>
    VtValue _ConvertToArray(const mi::neuraylib::IValue* const iValue) const
    {
        switch (_columns)
        {
        case 2:
            return (_rows == 2) ? _BuildVtArray<mi::math::Matrix<NEURAY_FLOAT_TYPE, 2, 2>>(iValue) :
                   (_rows == 3) ? _BuildVtArray<mi::math::Matrix<NEURAY_FLOAT_TYPE, 2, 3>>(iValue) :
                                  _BuildVtArray<mi::math::Matrix<NEURAY_FLOAT_TYPE, 2, 4>>(iValue);

        case 3:
            return (_rows == 2) ? _BuildVtArray<mi::math::Matrix<NEURAY_FLOAT_TYPE, 3, 2>>(iValue) :
                   (_rows == 3) ? _BuildVtArray<mi::math::Matrix<NEURAY_FLOAT_TYPE, 3, 3>>(iValue) :
                                  _BuildVtArray<mi::math::Matrix<NEURAY_FLOAT_TYPE, 3, 4>>(iValue);

        default:
            return (_rows == 2) ? _BuildVtArray<mi::math::Matrix<NEURAY_FLOAT_TYPE, 4, 2>>(iValue) :
                   (_rows == 3) ? _BuildVtArray<mi::math::Matrix<NEURAY_FLOAT_TYPE, 4, 3>>(iValue) :
                                  _BuildVtArray<mi::math::Matrix<NEURAY_FLOAT_TYPE, 4, 4>>(iValue);
        }
    }

private:
    mi::Size _columns;
    mi::Size _rows;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // MDL_CONVERTER_MATRIX_H
