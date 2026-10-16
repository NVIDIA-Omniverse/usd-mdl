//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MDL_CONVERTER_H
#define MDL_CONVERTER_H

#include "../debugContext.h"
#include "../errorMessages.h"
#include "tokens.h"
#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/sdf/valueTypeName.h>
#include <pxr/usd/sdr/shaderProperty.h>

#include <mi/mdl_sdk.h>
#include <utility>

PXR_NAMESPACE_OPEN_SCOPE

class MdlModule;

class Converter
{
public:
    struct Output {
        std::unique_ptr<Converter> converter;
        std::string name;
        bool isField;
        Output(std::unique_ptr<Converter> converter, std::string name, bool isField = false)
            : converter(std::move(converter)), name(std::move(name)), isField(isField) {}
    };
    typedef std::vector<Output> OutputsVec;

    // Return a valid converter, or nullptr for unsupported types/failed initialization.
    static std::unique_ptr<Converter> GetConverter(const mi::neuraylib::IType* const iType,
                                                   const MdlModule* mdlModule,
                                                   const DebugContext& debugCtx);

    // Convert an SDK constant. Nonconstant expressions have no value.
    // `mdlModule` is required for expressions
    // whose type carries resources (`IValue_resource`); passing nullptr
    // is acceptable for annotation arguments without resources.
    static VtValue ConvertConstantExpression(const mi::neuraylib::IExpression* const iExpression,
                                             const MdlModule* mdlModule,
                                             const DebugContext& debugCtx);

    // Mdl functions can only return a single value, however in USD
    // it's possible for a UsdShade node to have multiple output ports.
    // Using annotation data some IType's can be instructed to unroll/expand
    // from a single value into multiple outputs.
    // Given an IType and the annotation associated with the port it represents
    // the function will return a list of outputs.
    // Presently only IType_Structs have the ability to unroll/expand
    // into multiple outputs.
    //
    // `mdlModule` is the owning `MdlModule` for the definition whose
    // return type is being expanded.  It is required for output ports
    // that resolve to resource types so the converter can call
    // `MdlModule::ResolveResourceAssetPath`; the parameter may be
    // nullptr for return types without resources.
    static OutputsVec GetOutputs(const mi::neuraylib::IType* const iType,
                                 const mi::neuraylib::IAnnotation_block* const returnAnnotation,
                                 const MdlModule* mdlModule,
                                 const DebugContext& debugCtx);

    /// Constructs a converter for the given MDL type.
    Converter(const mi::neuraylib::IType* const iType, const MdlModule* mdlModule, const DebugContext& debugCtx);

    /// Destroys the converter.
    virtual ~Converter(){};

    /// Constructor status, used when a concrete converter is built directly.
    /// GetConverter() already filters invalid instances before returning them.
    bool IsValid() const
    {
        return _valid;
    }

    /// Borrow the original unaliased SDK type during node construction.
    const mi::neuraylib::IType* GetMdlType() const { return _iType.get(); }

    // Sdr or Sdf type
    const TfToken& GetType() const
    {
        return _type;
    }

    /// Returns the SDF value type name for this MDL type.
    const SdfValueTypeName& GetSdfType() const
    {
        return _sdf_type;
    }

    // UsdMdlType
    const TfToken& GetRenderType() const
    {
        return _renderType;
    }

    // This will be non-zero for
    // 1. immediate sized IType_Array's
    // 2. Vectors of bool (there is no boolean vector type in USD)
    // 3. Non-square and/or float matrices
    virtual size_t GetArraySize() const
    {
        return 0;
    }

    // options associated with the type
    // this will be set by IType_Enum or IArray's of IType_Enum
    virtual const SdrOptionVec& GetOptions() const
    {
        static SdrOptionVec options;
        return options;
    }

    // Convert an SDK value; return an empty VtValue when it cannot be represented.
    virtual VtValue ConvertValue(
        const mi::neuraylib::IValue* const,
        VtDictionary* const = nullptr) const
    {
        return VtValue();
    }

    // Convert a constant default, or describe a nonconstant root expression in
    // metadata and return an empty VtValue. Never evaluate an expression graph.
    // Borrow the expression's transaction for call references. Constant resources
    // use the same inspection transaction through the converter's MdlModule.
    VtValue ConvertDefault(const mi::neuraylib::IExpression* const expression,
                           mi::neuraylib::ITransaction* transaction,
                           VtDictionary* const metadata) const;

    // populate metadataMap, if this is not an output value pass in  metadataDict to gather
    // additional value & type metadata for enum and struct IType's
    virtual void GetMetadata(SdrTokenMap& metadataMap, VtDictionary* const metadataDict = nullptr) const;

    // Symbol if aliased type
    const std::string& GetSymbol() const
    {
        return _symbol;
    }

protected:
    const mi::base::Handle<const mi::neuraylib::IType> _iType;
    const MdlModule* _module;  // the module that contains the definition of the function currently processed. needed for resources.
    DebugContext _debugCtx;
    TfToken _type;
    SdfValueTypeName _sdf_type;
    TfToken _renderType;
    std::string _symbol;
    mi::neuraylib::IType::Kind _kind;
    TfToken _modifier;
    bool _valid;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // MDL_CONVERTER_H
