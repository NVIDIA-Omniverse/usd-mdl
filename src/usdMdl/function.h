//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MDL_FUNCTION_H
#define MDL_FUNCTION_H

#include "converter/converter.h"
#include "debugContext.h"
#include <pxr/usd/sdr/shaderNodeDiscoveryResult.h>

#include <mi/mdl_sdk.h>

PXR_NAMESPACE_OPEN_SCOPE

class MdlModule;

/// Retains a definition's identity while its module is being inspected.
/// SDK handles are acquired only while building a requested Sdr node.
class Function
{
public:
    /// Construct from an MDL function definition and its parent module context.
    Function(const std::string& definitionDbName,
             const mi::neuraylib::IFunction_definition* definition,
             const mi::neuraylib::IModule* module,
             const TfToken& moduleUsdIdentifier,
             MdlModule* mdlModule);

    /// Move-only; the parent module owns each definition wrapper.
    Function(Function&&) = default;

    /// Non-copyable.
    Function(const Function&) = delete;

    /// Non-copyable.
    Function& operator=(const Function&) = delete;

    /// Non-move-assignable.
    Function& operator=(Function&&) = delete;

    ~Function() = default;

    /// The USD sub-identifier token for this function within its module.
    const TfToken& GetSubIdentifier() const
    {
        return _subIdentifier;
    }

    /// The MDL function name including its parameter type signature,
    /// with the owner module prefix stripped (e.g.
    /// `my_func(::mod::Thing)`).  Surfaced as the
    /// `mdl:mdlNameWithSignature` node metadata for diagnostics.
    const TfToken& GetMdlNameWithSignature() const
    {
        return _mdlNameWithSignature;
    }

    /// Construct a node on demand; MdlSdrIntegration/Sdr own the resulting cache.
    /// The caller keeps the borrowed transaction open through conversion.
    /// For builtin specialization, supply concrete inputs/output and the SDK
    /// serialized name; the discovery sub-identifier retains local type names.
    SdrShaderNodeUniquePtr GetSdrFromDiscoveryResult(
        const SdrShaderNodeDiscoveryResult& discoveryRes,
        mi::neuraylib::ITransaction* transaction,
        const mi::neuraylib::IType_list* specializedInputs = nullptr,
        const mi::neuraylib::IType* specializedOutput = nullptr,
        const std::string& specializedName = {}) const;

private:
    bool _GetNodeMetadata(const mi::neuraylib::IFunction_definition* definition,
                          SdrTokenMap& metadata) const;

    bool _GetNodeProperty(const std::string& name,
                          mi::neuraylib::ITransaction* transaction,
                          Converter* const converter,
                          const mi::neuraylib::IAnnotation_block* const iAnnotationBlock,
                          const mi::neuraylib::IExpression* const iExpression,
                          const bool isOutput,
                          const DebugContext& paramDebugCtx,
                          SdrShaderPropertyUniquePtrVec& properties,
                          const mi::neuraylib::IAnnotation_block* fieldAnnotations = nullptr) const;

    bool _GetNodeProperties(const mi::neuraylib::IFunction_definition* definition,
                            mi::neuraylib::ITransaction* transaction,
                            SdrShaderPropertyUniquePtrVec& properties) const;

    bool _GetSpecializedProperties(const mi::neuraylib::IType_list* inputs,
                                   const mi::neuraylib::IType* output,
                                   mi::neuraylib::ITransaction* transaction,
                                   SdrShaderPropertyUniquePtrVec& properties) const;

private:
    std::string _definitionDbName;
    TfToken _moduleUsdIdentifier;
    MdlModule* const _mdlModule;
    DebugContext _debugCtx;

    TfToken _mdlNameWithSignature;
    TfToken _subIdentifier;
    bool _isMaterial = false;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // MDL_FUNCTION_H
