//
// SPDX-License-Identifier: Apache-2.0
//

#include "function.h"

#include "annotation.h"
#include "propertyConversion.h"

#include "converter/converter.h"
#include "mdlModule.h"
#include <mi/mdl_sdk.h>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/usd/sdr/parserPlugin.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>
#include "tokens.h"
#include "utils.h"

#include <string>
#include <unordered_set>
using namespace mi::neuraylib;

PXR_NAMESPACE_OPEN_SCOPE

Function::Function(const std::string& definitionDbName,
                   const mi::neuraylib::IFunction_definition* definition,
                   const mi::neuraylib::IModule* module,
                   const TfToken& moduleUsdIdentifier,
                   MdlModule* const mdlModule)
    : _definitionDbName(definitionDbName),
      _moduleUsdIdentifier(moduleUsdIdentifier),
      _mdlModule(mdlModule)
{
    if (!definition) {
        return;
    }

    const char* const fullMdl = definition->get_mdl_name();
    const char* const modName = definition->get_mdl_module_name();
    std::string sigStr = fullMdl ? fullMdl : "";
    const std::string prefix = std::string(modName) + "::";
    // Builtin definition names omit the module prefix.
    if (TfStringStartsWith(sigStr, prefix)) {
        sigStr = sigStr.substr(prefix.size());
    }
    _mdlNameWithSignature = TfToken(sigStr);

    _isMaterial = definition->is_material();

    _subIdentifier = Utils::ComputeMdlSubIdentifier(definition, module);

    _debugCtx = {&_mdlModule->GetDebugCtx(), "function", _mdlNameWithSignature.GetText()};
}

bool Function::_GetNodeMetadata(
    const mi::neuraylib::IFunction_definition* definition,
    SdrTokenMap& metadata) const
{
    if (!definition) {
        return false;
    }

    VtDictionary metadataDict = _mdlModule->GetMetadata();
    const mi::base::Handle<const IAnnotation_block> iAnnotationBlock(definition->get_annotations());
    Annotation::GetMetadata(iAnnotationBlock.get(), _debugCtx, metadataDict);

    // Use the definition's own annotation, not inherited module metadata.
    // Hidden definitions remain usable; editors decide how to display them.
    const mi::neuraylib::Annotation_wrapper annotations(iAnnotationBlock.get());
    metadata[UsdMdlNodeMetadataTokens->Hidden] =
        annotations.get_annotation_index("::anno::hidden()") != static_cast<mi::Size>(-1)
        ? "true" : "false";

    if (!Annotation::BuildNodeMetadata(metadataDict, metadata))
    {
        return false;
    }

    if (const char* thumbnail = definition->get_thumbnail())
    {
        metadata.emplace(std::make_pair(UsdMdlMetadataTokens->Thumbnail, thumbnail));
    }

    if (definition->is_uniform())
    {
        metadata.emplace(std::make_pair(UsdMdlMetadataTokens->Modifier, UsdMdlTypeModifiers->Uniform.GetString()));
    }

    metadata.emplace(
        std::make_pair(UsdMdlMetadataTokens->Semantic, TfStringify(static_cast<int>(definition->get_semantic()))));

    return true;
}

// clang-format off
SdrShaderNodeUniquePtr
Function::GetSdrFromDiscoveryResult(const SdrShaderNodeDiscoveryResult& discoveryRes,
                                    ITransaction* transaction,
                                    const IType_list* specializedInputs,
                                    const IType* specializedOutput,
                                    const std::string& specializedName) const
{
    // The integration layer and Sdr cache complete nodes. Keeping a second
    // property cache here adds mutable state without avoiding any conversion.
    SdrShaderPropertyUniquePtrVec properties;
    SdrTokenMap definitionMetadata;
    if (!transaction) {
        return SdrParserPlugin::GetInvalidShaderNode(discoveryRes);
    }
    bool ready = false;
    {
        mi::base::Handle<const IFunction_definition> definition(
            transaction->access<IFunction_definition>(_definitionDbName.c_str()));
        ready = definition && (specializedInputs
            ? _GetSpecializedProperties(specializedInputs, specializedOutput, transaction, properties)
            : _GetNodeProperties(definition.get(), transaction, properties))
            && _GetNodeMetadata(definition.get(), definitionMetadata);
    }
    if (!ready) {
        return SdrParserPlugin::GetInvalidShaderNode(discoveryRes);
    }

    SdrTokenMap metadata = discoveryRes.metadata;
    metadata.insert(definitionMetadata.begin(), definitionMetadata.end());

    metadata.emplace(UsdMdlNodeMetadataTokens->ModuleUsdIdentifier, _moduleUsdIdentifier.GetString());
    metadata[UsdMdlNodeMetadataTokens->SubIdentifier] = _subIdentifier.GetString();
    metadata.emplace(UsdMdlNodeMetadataTokens->MdlNameWithSignature, _mdlNameWithSignature.GetString());
    if (specializedInputs) {
        // The DB definition stays generic; renderer handoff must retain the
        // serialized specialization instead of losing its element type/count.
        metadata[UsdMdlNodeMetadataTokens->SubIdentifier] = discoveryRes.subIdentifier.GetString();
        metadata[UsdMdlNodeMetadataTokens->MdlNameWithSignature] = specializedName;
    }
    metadata.emplace(UsdMdlNodeMetadataTokens->MdlDbModuleName, _mdlModule->GetDbName());
    metadata.emplace(UsdMdlNodeMetadataTokens->MdlDbDefinitionName, _definitionDbName);
    metadata[UsdMdlNodeMetadataTokens->IsEmbeddedModule] =
        _mdlModule->IsEmbedded() ? "true" : "false";
    metadata.emplace(
        UsdMdlNodeMetadataTokens->DefinitionType,
        _isMaterial ? UsdMdlFunctionDefinitionTypes->Material.GetString()
                    : UsdMdlFunctionDefinitionTypes->Function.GetString());

    const TfToken context = _isMaterial ? SdrNodeContext->Surface : SdrNodeContext->Pattern;

    return SdrShaderNodeUniquePtr(new SdrShaderNode(
        discoveryRes.identifier,
        discoveryRes.version,
        discoveryRes.name,
        discoveryRes.family,
        context,
        discoveryRes.sourceType,
        _mdlModule->GetResolvedPath(),
        _mdlModule->GetResolvedPath(),
        std::move(properties),
        metadata,
        discoveryRes.sourceCode));
}

bool
Function::_GetNodeProperty(
    const std::string& name,
    ITransaction* transaction,
    Converter* const converter,
    const IAnnotation_block* const iAnnotationBlock,
    const IExpression* const iExpression,
    const bool isOutput,
    const DebugContext& paramDebugCtx,
    SdrShaderPropertyUniquePtrVec& properties,
    const IAnnotation_block* fieldAnnotations) const
{
    VtValue defaultValue;
    SdrTokenMap metadata;

    VtDictionary metadataDict;

    Annotation::GetMetadata(iAnnotationBlock, paramDebugCtx, metadataDict);
    Annotation::GetMetadata(fieldAnnotations, paramDebugCtx, metadataDict);

    converter->GetMetadata(metadata, !isOutput ? &metadataDict : nullptr);

    if (!isOutput)
    {
        // Presence comes from the SDK, independent of Sdr value conversion.
        metadata[UsdMdlMetadataTokens->HasDefault] = iExpression ? "true" : "false";
        defaultValue = converter->ConvertDefault(iExpression, transaction, &metadataDict);
    }

    SdrTokenMap hints;

    if (!Annotation::BuildPropertyMetadata(metadataDict, metadata, hints))
    {
        return false;
    }

    TfToken propertyType = converter->GetType();
    size_t arraySize = converter->GetArraySize();
    SdrOptionVec options = converter->GetOptions();
    if (!UsdMdlPropertyConversion::FinalizeProperty(
            converter->GetMdlType(), iExpression, converter->GetSdfType(),
            propertyType, arraySize, defaultValue, metadata, options, paramDebugCtx)) {
        return false;
    }

    // Sdr's type vocabulary does not recognize every Sdf spelling (e.g.
    // color3f or bool). Supply the converter's exact USD type as a default.
    // Explicit Sdr metadata takes precedence. Token is an opaque placeholder
    // for structs/distributions and must retain Sdr's existing treatment.
    const SdfValueTypeName& sdfType = converter->GetSdfType();
    if (sdfType && sdfType != SdfValueTypeNames->Token)
    {
        metadata.emplace(SdrPropertyMetadata->SdrUsdDefinitionType,
                         sdfType.GetAsToken().GetString());
    }

    properties.emplace_back(SdrShaderPropertyUniquePtr(
        new SdrShaderProperty(
            TfToken(name),                // parameter name
            propertyType,                 // type
            defaultValue,                 // default value.
            isOutput,                     // is this an output parameter
            arraySize,                    // if array/vector type, size
            metadata,                     // parameter annotation metadata
            hints,                        // parameter hints
            options                       // display/value pairs if enumeration
        )));

    return true;
}
// clang-format on

bool Function::_GetSpecializedProperties(
    const IType_list* inputs, const IType* output, ITransaction* transaction,
    SdrShaderPropertyUniquePtrVec& properties) const
{
    auto add = [&](const char* name, const IType* type, bool isOutput) {
        const DebugContext debug{&_debugCtx, isOutput ? "output" : "parameter", name};
        auto converter = Converter::GetConverter(type, _mdlModule, debug);
        // A specialization must expose every port, never a plausible partial node.
        return converter && _GetNodeProperty(name, transaction, converter.get(), nullptr,
                                              nullptr, isOutput, debug, properties);
    };
    for (mi::Size i = 0; i < inputs->get_size(); ++i) {
        const mi::base::Handle<const IType> type(inputs->get_type(i));
        if (!add(inputs->get_name(i), type.get(), false)) {
            return false;
        }
    }
    return output && add("out", output, true);
}

bool Function::_GetNodeProperties(
    const mi::neuraylib::IFunction_definition* definition,
    ITransaction* transaction,
    SdrShaderPropertyUniquePtrVec& properties) const
{
    if (!definition) {
        return false;
    }
    if (const mi::Size parameterCount = definition->get_parameter_count())
    {
        const mi::base::Handle<const IType_list> parameterTypes(definition->get_parameter_types());
        const mi::base::Handle<const IExpression_list> parameterDefaults(definition->get_defaults());
        const mi::base::Handle<const IAnnotation_list> parameterAnnotations(definition->get_parameter_annotations());

        for (mi::Size i = 0; i < parameterCount; ++i)
        {
            const std::string& name = TfSafeString(definition->get_parameter_name(i));
            const mi::base::Handle<const IType> pType(parameterTypes->get_type(name.c_str()));
            const mi::base::Handle<const IExpression> expression(parameterDefaults->get_expression(name.c_str()));
            const mi::base::Handle<const IAnnotation_block> annotation(
                parameterAnnotations->get_annotation_block(name.c_str()));

            if (!pType) {
                continue;
            }

            const DebugContext paramDebugCtx{&_debugCtx, "parameter", name.c_str()};
            std::unique_ptr<Converter> converter(Converter::GetConverter(pType.get(), _mdlModule, paramDebugCtx));

            // Report skipped inputs so consumers can detect a node whose
            // exposed ports do not cover the complete MDL definition.
            if (!converter) {
                TF_WARN("%s\tunsupported MDL parameter type; input '%s' will not be exposed on "
                        "the Sdr shader node.",
                        paramDebugCtx.format().c_str(), name.c_str());
                continue;
            }
            if (!_GetNodeProperty(name, transaction, converter.get(), annotation.get(), expression.get(),
                                  false, paramDebugCtx, properties))
            {
                return false;
            }
        }
    }

    // outputs
    const mi::base::Handle<const IType> pType(definition->get_return_type());
    const mi::base::Handle<const IAnnotation_block> annotation(definition->get_return_annotations());

    const Converter::OutputsVec outputs =
        Converter::GetOutputs(pType.get(), annotation.get(), _mdlModule, _debugCtx);

    std::unordered_set<std::string> outputNames;
    for (const Converter::Output& output : outputs)
    {
        const std::string& name = output.name;
        if (!outputNames.insert(name).second) {
            TF_WARN("%s\tduplicate MDL output '%s'.", _debugCtx.format().c_str(), name.c_str());
            return false;
        }
        const DebugContext paramDebugCtx{&_debugCtx, "output", name.c_str()};

        mi::base::Handle<const IAnnotation_block> fieldAnnotations;
        const IAnnotation_block* outputAnnotations = annotation.get();
        const mi::base::Handle<const IType> unaliased(pType->skip_all_type_aliases());
        const mi::base::Handle<const IType_struct> structure(
            unaliased->get_interface<const IType_struct>());
        if (structure && output.isField) {
            const mi::Size fieldIndex = structure->find_field(name.c_str());
            if (fieldIndex != static_cast<mi::Size>(-1)) {
                outputAnnotations = nullptr;
                fieldAnnotations = mi::base::Handle<const IAnnotation_block>(
                    structure->get_field_annotations(fieldIndex));
            }
        }
        if (!output.converter.get() || !_GetNodeProperty(name, transaction, output.converter.get(), outputAnnotations, nullptr, true,
                                                     paramDebugCtx, properties, fieldAnnotations.get()))
        {
            return false;
        }
    }

    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
