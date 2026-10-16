// SPDX-License-Identifier: Apache-2.0
#include "shaderDefinitionsInternal.h"
#include "addModuleToRegistry.h"
#include "mdlSdrIntegration.h"
#include "propertyConversion.h"
#include "converter/converter.h"
#include "runtime/Context.h"
#include "tokens.h"
#include "utils.h"

#include <mi/mdl_sdk.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/usd/ar/resolverContextBinder.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/sdr/filesystemDiscoveryHelpers.h>
#include <pxr/usd/sdr/shaderMetadataHelpers.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdShade/shaderDefUtils.h>

#include <algorithm>
#include <charconv>
#include <map>
#include <set>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE
namespace {
using namespace mi::neuraylib;

// Declare before every accessed SDK handle. Destruction releases those handles
// before closing the read-only snapshot, including on early returns.
class InspectionTransaction {
public:
    explicit InspectionTransaction(usdMdl::runtime::Context& context)
        : _transaction(context.createTransaction()) {}
    ~InspectionTransaction() { if (_transaction) _transaction->abort(); }
    InspectionTransaction(const InspectionTransaction&) = delete;
    InspectionTransaction& operator=(const InspectionTransaction&) = delete;
    ITransaction* get() const { return _transaction.get(); }
private:
    mi::base::Handle<ITransaction> _transaction;
};

bool _Reject(const UsdShadeShader& shader, const std::string& reason)
{
    TF_WARN("MDL shader definition <%s>: %s.",
            shader.GetPath().GetText(), reason.c_str());
    return false;
}

bool _IsMdlMetadata(const TfToken& key)
{
    return TfStringStartsWith(key.GetString(), "mdl.")
        || TfStringStartsWith(key.GetString(), "mdl:");
}

TfToken _Identifier(const UsdShadeShader& shader)
{
    TfToken identifier;
    shader.GetIdAttr().Get(&identifier);
    return identifier.IsEmpty() ? shader.GetPrim().GetName() : identifier;
}

mi::base::Handle<const IType_enum> _EnumType(const IType* type)
{
    mi::base::Handle<const IType> unaliased(type ? type->skip_all_type_aliases() : nullptr);
    if (unaliased && unaliased->get_kind() == IType::TK_ARRAY) {
        const mi::base::Handle<const IType_array> array(unaliased.get_interface<const IType_array>());
        const mi::base::Handle<const IType> element(array->get_element_type());
        unaliased = mi::base::Handle<const IType>(
            element ? element->skip_all_type_aliases() : nullptr);
    }
    return mi::base::Handle<const IType_enum>(
        unaliased ? unaliased->get_interface<const IType_enum>() : nullptr);
}

bool _EnumMember(const IType_enum* type, const std::string& name)
{
    for (mi::Size i = 0; i < type->get_size(); ++i) {
        const char* member = type->get_value_name(i);
        if (member && name == member) return true;
    }
    return false;
}

bool _EnumCode(const IType_enum* type, int code)
{
    for (mi::Size i = 0; i < type->get_size(); ++i) {
        if (type->get_value_code(i) == code) return true;
    }
    return false;
}

bool _ValidateEnum(const IType_enum* type, const VtValue& value)
{
    if (value.IsEmpty()) return true;
    if (value.IsHolding<std::string>())
        return _EnumMember(type, value.UncheckedGet<std::string>());
    if (value.IsHolding<TfToken>())
        return _EnumMember(type, value.UncheckedGet<TfToken>().GetString());
    if (value.IsHolding<int>()) return _EnumCode(type, value.UncheckedGet<int>());
    if (value.IsHolding<VtStringArray>()) {
        for (const auto& name : value.UncheckedGet<VtStringArray>())
            if (!_EnumMember(type, name)) return false;
        return true;
    }
    if (value.IsHolding<VtIntArray>()) {
        for (int code : value.UncheckedGet<VtIntArray>())
            if (!_EnumCode(type, code)) return false;
        return true;
    }
    return false;
}

// Mirror the public ShaderDefUtils property subset before calling it: its
// unsupported-type path emits TF_RUNTIME_ERROR, whereas this API omits invalid
// definitions with warnings. SDK compatibility is checked separately below.
bool _SupportedUsdType(const SdfValueTypeName& type)
{
    const auto scalar = type.GetScalarType();
    return scalar == SdfValueTypeNames->Bool || scalar == SdfValueTypeNames->Int
        || scalar == SdfValueTypeNames->Int2 || scalar == SdfValueTypeNames->Int3
        || scalar == SdfValueTypeNames->Int4 || scalar == SdfValueTypeNames->String
        || scalar == SdfValueTypeNames->Token || scalar == SdfValueTypeNames->Asset
        || scalar == SdfValueTypeNames->Float || scalar == SdfValueTypeNames->Float2
        || scalar == SdfValueTypeNames->Float3 || scalar == SdfValueTypeNames->Float4
        || scalar == SdfValueTypeNames->Color3f || scalar == SdfValueTypeNames->Color4f
        || scalar == SdfValueTypeNames->Point3f || scalar == SdfValueTypeNames->Vector3f
        || scalar == SdfValueTypeNames->Normal3f || scalar == SdfValueTypeNames->Matrix4d;
}

void _ConformStringDefault(VtValue& value)
{
    if (value.IsHolding<TfToken>()) {
        value = VtValue(value.UncheckedGet<TfToken>().GetString());
    } else if (value.IsHolding<VtTokenArray>()) {
        VtStringArray strings;
        for (const auto& token : value.UncheckedGet<VtTokenArray>())
            strings.push_back(token.GetString());
        value = VtValue(strings);
    }
}

// SDK handles remain within the caller's inspection. Constructor parameters
// and return type come from the concrete specialization, not mdl::T[](...).
struct ImplementationInterface {
    mi::base::Handle<const IType_list> parameters;
    mi::base::Handle<const IExpression_list> defaults;
    mi::base::Handle<const IType> returnType;
    mi::base::Handle<const IAnnotation_block> returnAnnotations;
    bool isMaterial = false;
    bool isArrayConstructor = false;
};

bool _BuildProperties(const UsdShadeShader& shader, const SdrShaderNode* native,
                      const ImplementationInterface& implementation,
                      SdrShaderPropertyUniquePtrVec& properties)
{
    const std::string shaderName = shader.GetPath().GetString();
    const DebugContext shaderContext{nullptr, "shader definition", shaderName.c_str()};
    const auto& parameters = implementation.parameters;
    const auto& defaults = implementation.defaults;
    const auto& returnType = implementation.returnType;
    if (!parameters || !returnType) return _Reject(shader, "SDK type information is unavailable");
    const mi::base::Handle<const IType> unaliasedReturn(returnType->skip_all_type_aliases());
    if (!unaliasedReturn) return _Reject(shader, "SDK return type cannot be resolved");
    const mi::base::Handle<const IType_struct> structure(
        unaliasedReturn->get_interface<const IType_struct>());
    const auto& returnAnnotations = implementation.returnAnnotations;
    const auto nativeLayout = Converter::GetOutputs(returnType.get(), returnAnnotations.get(),
                                                     nullptr, shaderContext);
    std::set<std::string> inputBindings;
    bool hasConstructorOutput = false;
    const auto connectable = shader.ConnectableAPI();
    for (const auto& input : connectable.GetInputs(false)) {
        if (!_SupportedUsdType(input.GetTypeName()))
            return _Reject(shader, "unsupported USD input type '" + input.GetTypeName().GetAsToken().GetString() + "'");
    }
    for (const auto& output : connectable.GetOutputs(false)) {
        if (!_SupportedUsdType(output.GetTypeName()))
            return _Reject(shader, "unsupported USD output type '" + output.GetTypeName().GetAsToken().GetString() + "'");
    }
    auto authoredProperties = UsdShadeShaderDefUtils::GetProperties(connectable);
    for (const auto& authored : authoredProperties) {
        const TfToken name = authored->GetName();
        const bool isOutput = authored->IsOutput();
        const UsdAttribute attribute = shader.GetPrim().GetAttribute(
            TfToken(std::string(isOutput ? "outputs:" : "inputs:") + name.GetString()));
        if (!attribute || authored->GetType() == SdrPropertyTypes->Unknown)
            return _Reject(shader, "unsupported authored property '" + name.GetString() + "'");
        SdrTokenMap authoredMetadata = authored->GetMetadata();
        VtDictionary authoredSdrMetadata;
        attribute.GetMetadata(TfToken("sdrMetadata"), &authoredSdrMetadata);
        // OpenUSD's GetProperties normalizes this spelling only on inputs.
        const auto plainName = authoredMetadata.find(TfToken("implementationName"));
        if (plainName != authoredMetadata.end()) {
            authoredMetadata[SdrPropertyMetadata->ImplementationName] = plainName->second;
            authoredMetadata.erase(plainName);
        }
        const auto implementationName = authoredMetadata.find(SdrPropertyMetadata->ImplementationName);
        const std::string binding = implementationName == authoredMetadata.end()
            ? name.GetString() : implementationName->second;
        if (binding.empty()) return _Reject(shader, "empty implementationName on '" + name.GetString() + "'");
        mi::base::Handle<const IType> mdlType;
        mi::base::Handle<const IExpression> mdlDefault;
        const SdrShaderProperty* nativeProperty = nullptr;
        bool wholeReturn = false;
        if (!isOutput) {
            mdlType = mi::base::Handle<const IType>(parameters->get_type(binding.c_str()));
            nativeProperty = native->GetShaderInput(TfToken(binding));
            if (!mdlType || !nativeProperty)
                return _Reject(shader, "input '" + name.GetString() + "' has unknown MDL binding '" + binding + "'");
            if (!inputBindings.insert(binding).second)
                return _Reject(shader, "multiple inputs bind the MDL parameter '" + binding + "'");
            if (defaults) mdlDefault = mi::base::Handle<const IExpression>(defaults->get_expression(binding.c_str()));
        } else {
            nativeProperty = native->GetShaderOutput(TfToken(binding));
            if (binding == UsdMdlTokens->DefaultOutputPortName.GetString()) {
                size_t matches = 0;
                bool isField = false;
                for (const auto& output : nativeLayout) {
                    if (output.name == binding) { ++matches; isField = output.isField; }
                }
                if (matches != 1)
                    return _Reject(shader, "ambiguous or unavailable MDL output binding '" + binding + "'");
                wholeReturn = !isField;
            }
            if (wholeReturn) {
                mdlType = returnType;
                hasConstructorOutput = true;
            } else if (structure) {
                const mi::Size index = structure->find_field(binding.c_str());
                if (index != static_cast<mi::Size>(-1))
                    mdlType = mi::base::Handle<const IType>(structure->get_field_type(index));
            }
            if (!mdlType)
                return _Reject(shader, "output '" + name.GetString() + "' has unknown MDL binding '" + binding + "'");
        }
        const DebugContext propertyContext{&shaderContext, isOutput ? "output" : "input", name.GetText()};
        auto converter = Converter::GetConverter(mdlType.get(), nullptr, propertyContext);
        if (!converter) return _Reject(shader, "unsupported MDL type on '" + binding + "'");
        SdrTokenMap metadata = nativeProperty ? nativeProperty->GetMetadata() : SdrTokenMap();
        converter->GetMetadata(metadata);
        for (const auto& item : authoredMetadata) {
            if (_IsMdlMetadata(item.first)) continue;
            // SdrShaderProperty inserts a default widget even when the USD
            // definition did not author one. Retain the native MDL widget.
            if (item.first == SdrPropertyMetadata->Widget
                && !authoredSdrMetadata.count(SdrPropertyMetadata->Widget.GetString())) continue;
            // A declaration without an authored connectability should retain
            // the native MDL annotation rather than GetProperties' default 1.
            if (!isOutput && nativeProperty && item.first == SdrPropertyMetadata->Connectable
                && !attribute.HasAuthoredMetadata(TfToken("connectability"))) continue;
            metadata[item.first] = item.second;
        }
        metadata[SdrPropertyMetadata->ImplementationName] = binding;
        metadata[SdrPropertyMetadata->SdrUsdDefinitionType] = attribute.GetTypeName().GetAsToken().GetString();
        const std::string help = attribute.GetDocumentation();
        if (!help.empty()) metadata[SdrPropertyMetadata->Help] = help;
        TfToken sdrType = converter->GetType();
        size_t arraySize = converter->GetArraySize();
        VtValue defaultValue = nativeProperty && !isOutput ? nativeProperty->GetDefaultValue() : VtValue();
        SdrOptionVec options = nativeProperty ? nativeProperty->GetOptions() : converter->GetOptions();
        const mi::base::Handle<const IType_enum> enumType(_EnumType(mdlType.get()));
        if (enumType) {
            // A native Sdr node may already have a symbolic enum annotation.
            // Start from the original SDK code/options so the authored USD
            // profile can request either numeric or symbolic representation.
            defaultValue = !isOutput
                ? converter->ConvertDefault(mdlDefault.get(), nullptr, nullptr) : VtValue();
            options = converter->GetOptions();
        }
        const bool terminal = authored->GetType() == SdrPropertyTypes->Terminal;
        if (terminal) {
            if (!isOutput || !wholeReturn || !implementation.isMaterial
                || attribute.GetTypeName() != SdfValueTypeNames->Token)
                return _Reject(shader, "terminal property '" + name.GetString() + "' must bind a whole MDL material return");
            sdrType = SdrPropertyTypes->Terminal;
            arraySize = 0;
        } else if (!UsdMdlPropertyConversion::FinalizeProperty(
                       mdlType.get(), mdlDefault.get(), converter->GetSdfType(),
                       sdrType, arraySize, defaultValue, metadata, options, propertyContext)) {
            return false;
        }
        // The authored interface can override a recoverable SDK default. It
        // does not require evaluation of a nonconstant SDK default expression.
        const bool authoredDefault = !isOutput && !authored->GetDefaultValue().IsEmpty();
        if (authoredDefault) {
            if (attribute.GetTypeName().GetScalarType() == SdfValueTypeNames->Asset
                || attribute.GetTypeName() == SdfValueTypeNames->BoolArray) {
                // GetProperties stringifies assets and converts bool arrays to
                // integer arrays. Preserve resolved asset identity and the USD
                // bool-array type, for which Sdr has no integer-array cast.
                if (!attribute.Get(&defaultValue))
                    return _Reject(shader, "cannot read typed authored default on '" + name.GetString() + "'");
            } else {
                defaultValue = authored->GetDefaultValue();
            }
        }
        if (sdrType == SdrPropertyTypes->String) _ConformStringDefault(defaultValue);
        if (enumType && !_ValidateEnum(enumType.get(), defaultValue))
            return _Reject(shader, "default on '" + name.GetString() + "' is not a member of the original MDL enum");
        if (!authored->GetOptions().empty()) {
            options = authored->GetOptions();
            if (enumType) {
                for (auto& option : options) {
                    const TfToken actual = option.second.IsEmpty() ? option.first : option.second;
                    if (sdrType == SdrPropertyTypes->String) {
                        if (!_EnumMember(enumType.get(), actual.GetString()))
                            return _Reject(shader, "option '" + actual.GetString() + "' is not a member of the original MDL enum");
                        option.second = actual;
                    } else {
                        int code = 0;
                        const std::string& text = actual.GetString();
                        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), code);
                        if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size()
                            || !_EnumCode(enumType.get(), code))
                            return _Reject(shader, "invalid integer MDL enum option '" + text + "'");
                    }
                }
            }
        }
        const mi::base::Handle<const IType> unaliased(mdlType->skip_all_type_aliases());
        const mi::base::Handle<const IType_array> array(
            unaliased ? unaliased->get_interface<const IType_array>() : nullptr);
        if (implementation.isArrayConstructor && authoredDefault
            && unaliased && unaliased->get_kind() == IType::TK_STRUCT)
            return _Reject(shader, "constructor input '" + name.GetString()
                + "' is a struct connection port and cannot have a literal default");
        // The converter counts USD storage entries, including flattened scalar
        // vectors/matrices. MDL array metadata retains the logical element count.
        if (authoredDefault && converter->GetSdfType().IsArray()
            && (!array || array->is_immediate_sized())) {
            const size_t storageSize = converter->GetArraySize();
            VtValue authoredValue;
            if (!attribute.Get(&authoredValue) || !authoredValue.IsArrayValued()
                || authoredValue.GetArraySize() != storageSize)
                return _Reject(shader, "authored array default on '" + name.GetString()
                    + "' has the wrong USD storage length (expected " + std::to_string(storageSize)
                    + ", got " + std::to_string(authoredValue.GetArraySize()) + ")");
        }
        SdrTokenMap hints = nativeProperty ? nativeProperty->GetHints() : SdrTokenMap();
        for (const auto& hint : authored->GetHints()) hints[hint.first] = hint.second;
        properties.emplace_back(new SdrShaderProperty(name, sdrType, defaultValue, isOutput,
                                                       arraySize, metadata, hints, options));
    }
    if (implementation.isArrayConstructor && !hasConstructorOutput)
        return _Reject(shader, "array constructor interface must expose its array return");
    for (mi::Size i = 0; i < parameters->get_size(); ++i) {
        const char* parameter = parameters->get_name(i);
        if (!parameter) return _Reject(shader, "unnamed SDK parameter");
        if (!inputBindings.count(parameter)) {
            const mi::base::Handle<const IExpression> fallback(
                defaults ? defaults->get_expression(parameter) : nullptr);
            if (!fallback) return _Reject(shader, "required MDL parameter '" + std::string(parameter) + "' is missing");
        }
    }
    return true;
}

} // namespace

std::vector<UsdMdlShaderDefinitions::PreparedNode>
UsdMdlShaderDefinitions::Prepare(const SdfAssetPath& definitionsAsset,
                                  const SdfAssetPath& implementationAsset,
                                  bool registryFree)
{
    std::vector<PreparedNode> prepared;
    const std::string path = definitionsAsset.GetResolvedPath().empty()
        ? definitionsAsset.GetAssetPath() : definitionsAsset.GetResolvedPath();
    if (path.empty()) { TF_WARN("MDL shader definitions: empty USD definition asset."); return prepared; }
    const SdfLayerRefPtr layer = SdfLayer::FindOrOpen(path);
    if (!layer) { TF_WARN("MDL shader definitions: cannot open '%s'.", path.c_str()); return prepared; }
    const SdfLayerRefPtr session = SdfLayer::CreateAnonymous("usdMdlShaderDefinitions");
    const UsdStageRefPtr stage = UsdStage::Open(layer, session);
    if (!stage) { TF_WARN("MDL shader definitions: cannot compose '%s'.", path.c_str()); return prepared; }
    stage->SetEditTarget(session);
    const ArResolverContextBinder resolverContext(stage->GetPathResolverContext());
    const std::string definitionUri = layer->GetResolvedPath().empty()
        ? layer->GetIdentifier() : layer->GetResolvedPath().GetPathString();
    std::map<TfToken, size_t> identifiers;
    for (const UsdPrim& prim : stage->Traverse()) {
        if (prim.IsAbstract() || !prim.IsA<UsdShadeShader>()) continue;
        ++identifiers[_Identifier(UsdShadeShader(prim))];
    }
    using AssetKey = std::pair<std::string, std::string>;
    struct PreparedModule {
        std::vector<SdrShaderNodeUniquePtr> owned;
        std::vector<SdrShaderNodeConstPtr> nodes;
    };
    std::map<AssetKey, PreparedModule> modules;
    auto& context = usdMdl::runtime::Context::instance();
    for (const UsdPrim& prim : stage->Traverse()) {
        if (prim.IsAbstract() || !prim.IsA<UsdShadeShader>()) continue;
        const UsdShadeShader shader(prim);
        const TfToken identifier = _Identifier(shader);
        if (!SdfPath::IsValidNamespacedIdentifier(identifier.GetString())) {
            _Reject(shader, "shader identifier '" + identifier.GetString()
                + "' must be a valid USD namespaced identifier");
            continue;
        }
        if (identifiers[identifier] != 1) { _Reject(shader, "duplicate shader identifier '" + identifier.GetString() + "'"); continue; }
        if (shader.GetImplementationSource() != UsdShadeTokens->sourceAsset) {
            _Reject(shader, "implementationSource must be sourceAsset"); continue;
        }
        SdfAssetPath asset = implementationAsset;
        if (asset.GetAssetPath().empty() && asset.GetResolvedPath().empty())
            shader.GetSourceAsset(&asset, UsdMdlTokens->Mdl);
        TfToken subIdentifier;
        if (asset.GetAssetPath().empty() && asset.GetResolvedPath().empty()) {
            _Reject(shader, "missing MDL source asset"); continue;
        }
        if (!shader.GetSourceAssetSubIdentifier(&subIdentifier, UsdMdlTokens->Mdl)
            || subIdentifier.IsEmpty()) {
            _Reject(shader, "missing MDL sourceAsset:subIdentifier"); continue;
        }
        Utils::MdlSubIdentifier request;
        const bool isArrayConstructor = TfStringStartsWith(subIdentifier.GetString(), "T[]");
        if (!isArrayConstructor && !Utils::ParseMdlSubIdentifier(subIdentifier.GetString(), request)) {
            _Reject(shader, "malformed MDL sub-identifier '" + subIdentifier.GetString() + "'"); continue;
        }
        SdrShaderNodeUniquePtr constructor;
        auto module = modules.end();
        if (isArrayConstructor) {
            // This path loads and specializes without entering SdrRegistry,
            // including when Prepare runs during registry construction.
            constructor = MdlSdrIntegration::PrepareArrayConstructorNode(asset, subIdentifier);
            if (!constructor) {
                _Reject(shader, "cannot resolve MDL array constructor specialization"); continue;
            }
        } else {
            const AssetKey key(asset.GetAssetPath(), asset.GetResolvedPath());
            module = modules.find(key);
            if (module == modules.end()) {
                PreparedModule source;
                if (registryFree) {
                    source.owned = MdlSdrIntegration::PrepareNativeModuleNodes(asset);
                    for (const auto& node : source.owned) source.nodes.push_back(node.get());
                } else {
                    source.nodes = UsdMdlAddModuleToRegistry(asset);
                }
                module = modules.emplace(key, std::move(source)).first;
            }
        }
        InspectionTransaction inspection(context);
        if (!inspection.get()) { _Reject(shader, "cannot open SDK inspection transaction"); continue; }
        mi::base::Handle<const IFunction_definition> definition;
        ImplementationInterface implementation;
        const SdrShaderNode* native = constructor.get();
        if (isArrayConstructor) {
            const auto owner = native->GetMetadata().find(UsdMdlNodeMetadataTokens->MdlDbModuleName);
            const mi::base::Handle<const IModule> sdkModule(owner == native->GetMetadata().end()
                ? nullptr : inspection.get()->access<IModule>(owner->second.c_str()));
            MdlSdrIntegration::ArrayConstructorTypes types;
            if (!sdkModule || !MdlSdrIntegration::ResolveArrayConstructorTypes(
                    inspection.get(), TfSafeString(sdkModule->get_mdl_name()), subIdentifier, types)) {
                _Reject(shader, "concrete MDL constructor type information is unavailable"); continue;
            }
            implementation.parameters = types.parameters;
            implementation.returnType = types.returnType;
            implementation.isArrayConstructor = true;
        } else {
            size_t matches = 0;
            for (const auto* candidate : module->second.nodes) {
                if (!candidate || !candidate->IsValid()) continue;
                const auto dbName = candidate->GetMetadata().find(UsdMdlNodeMetadataTokens->MdlDbDefinitionName);
                if (dbName == candidate->GetMetadata().end()) continue;
                const mi::base::Handle<const IFunction_definition> sdk(
                    inspection.get()->access<IFunction_definition>(dbName->second.c_str()));
                if (sdk && Utils::MatchesMdlSubIdentifier(request, sdk.get())) {
                    ++matches; native = candidate; definition = sdk;
                }
            }
            if (matches != 1) {
                _Reject(shader, "MDL sub-identifier '" + subIdentifier.GetString()
                    + "' must match exactly one compiled definition"); continue;
            }
            implementation.parameters = mi::base::Handle<const IType_list>(definition->get_parameter_types());
            implementation.defaults = mi::base::Handle<const IExpression_list>(definition->get_defaults());
            implementation.returnType = mi::base::Handle<const IType>(definition->get_return_type());
            implementation.returnAnnotations = mi::base::Handle<const IAnnotation_block>(definition->get_return_annotations());
            implementation.isMaterial = definition->is_material();
        }
        SdrShaderPropertyUniquePtrVec properties;
        if (!_BuildProperties(shader, native, implementation, properties)) continue;
        SdrShaderNodeDiscoveryResultVec discoveries;
        if (isArrayConstructor) {
            // The SDK already validated the element module and specialization.
            // Builtin/embedded modules may have no physical implementation URI;
            // ShaderDefUtils discovery requires a resolved source asset file.
            discoveries.emplace_back(identifier, SdrVersion().GetAsDefault(),
                identifier.GetString(), TfToken(), UsdMdlTokens->Mdl, UsdMdlTokens->Mdl,
                definitionUri, definitionUri);
        } else {
            // Author only the anonymous session so standard discovery sees the
            // SDK-selected implementation without changing the input asset.
            if (!shader.SetSourceAsset(SdfAssetPath(native->GetResolvedImplementationURI()), UsdMdlTokens->Mdl)) {
                _Reject(shader, "cannot prepare the MDL implementation reference"); continue;
            }
            discoveries = UsdShadeShaderDefUtils::GetDiscoveryResults(shader, definitionUri);
        }
        auto discovery = std::find_if(discoveries.begin(), discoveries.end(), [](const auto& item) {
            return item.sourceType == UsdMdlTokens->Mdl;
        });
        if (discovery == discoveries.end()) { _Reject(shader, "MDL implementation cannot be resolved for discovery"); continue; }
        TfToken family, name;
        SdrVersion version;
        if (!SdrFsHelpersSplitShaderIdentifier(identifier, &family, &name, &version)) continue;
        discovery->identifier = identifier;
        discovery->family = family;
        discovery->name = name.GetString();
        discovery->version = version.GetAsDefault();
        discovery->discoveryType = UsdMdlTokens->Mdl;
        discovery->subIdentifier = subIdentifier;
        SdrTokenMap metadata = native->GetMetadata();
        for (const auto& item : shader.GetSdrMetadata())
            if (!_IsMdlMetadata(item.first)) metadata[item.first] = item.second;
        const auto mdlName = metadata.find(UsdMdlNodeMetadataTokens->MdlNameWithSignature);
        if (mdlName == metadata.end()) { _Reject(shader, "original MDL function identity is unavailable"); continue; }
        metadata[SdrNodeMetadata->ImplementationName] = mdlName->second;
        const std::string help = prim.GetDocumentation();
        if (!help.empty()) metadata[SdrNodeMetadata->Help] = help;
        const std::string primvars = UsdShadeShaderDefUtils::GetPrimvarNamesMetadataString(metadata, shader.ConnectableAPI());
        if (!primvars.empty()) metadata[SdrNodeMetadata->Primvars] = primvars;
        discovery->metadata = metadata;
        SdrShaderNodeUniquePtr node(new SdrShaderNode(
            identifier, discovery->version, discovery->name, family,
            native->GetContext(), UsdMdlTokens->Mdl, definitionUri,
            native->GetResolvedImplementationURI(), std::move(properties), metadata));
        if (!node->IsValid()) { _Reject(shader, "constructed USD interface is invalid"); continue; }
        prepared.emplace_back(*discovery, std::move(node));
    }
    return prepared;
}
PXR_NAMESPACE_CLOSE_SCOPE
