//
// SPDX-License-Identifier: Apache-2.0
//

#include "mdlModule.h"

#include "annotation.h"
#include "utils.h"
#include "tokens.h"

#include "Context.h"
#include "EnvSettings.h"
#include "PathHelpers.h"
#include "RejectedResource.h"

#include <mi/mdl_sdk.h>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdr/parserPlugin.h>

#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

std::string
_ResolveEmbeddedModuleProxy(
    const SdfAssetPath& assetPath,
    const char* mdlName,
    mi::neuraylib::IMdl_entity_resolver* entityResolver)
{
    // Retain the proxy selected by USD, including a custom Ar identifier.
    ArResolver& ar = ArGetResolver();
    const ArResolvedPath resolved = assetPath.GetResolvedPath().empty()
        ? ar.Resolve(assetPath.GetAssetPath()) : ArResolvedPath(assetPath.GetResolvedPath());
    if (resolved && ar.OpenAsset(resolved)) {
        return resolved.GetPathString();
    }

    // USD may not know the MDL search roots. Reuse their existing Ar-backed
    // lookup policy; a missing optional proxy does not fail SDK module loading.
    // The builtin proxy has a public filename unrelated to the SDK module name.
    const char* proxyName = TfSafeString(mdlName) == Utils::BuiltinModuleName
        ? "::mdl-builtin" : mdlName;
    mi::base::Handle<mi::neuraylib::IMdl_resolved_module> proxy(
        entityResolver ? entityResolver->resolve_module(proxyName, nullptr, nullptr, 0, 0, nullptr)
                       : nullptr);
    return proxy ? TfSafeString(proxy->get_filename()) : std::string();
}

} // namespace

MdlModule::MdlModule(const std::string& qualifiedModuleName, const SdfAssetPath& assetPath,
                     mi::neuraylib::ITransaction* transaction)
    : _transaction(transaction)
    , _qualifiedModuleName(qualifiedModuleName)
    , _resolvedPath(assetPath.GetResolvedPath().empty()
        ? assetPath.GetAssetPath() : assetPath.GetResolvedPath())
{
    _debugCtx = {nullptr, "module", _resolvedPath.c_str()};

    usdMdl::runtime::Context& ctx = usdMdl::runtime::Context::instance();

    if (!_transaction) {
        return;
    }

    mi::base::Handle<const mi::IString> const moduleDbName(ctx.getMdlFactory()->get_db_module_name(qualifiedModuleName.c_str()));
    if (!moduleDbName || !moduleDbName->get_c_str() || !moduleDbName->get_c_str()[0]) {
        TF_WARN("%s\tget_db_module_name failed for '%s'.", _debugCtx.format().c_str(), qualifiedModuleName.c_str());
        return;
    }
    _moduleDbName = moduleDbName->get_c_str();

    bool ok = false;
    {
        mi::base::Handle<const mi::neuraylib::IModule> const module(
            _transaction->access<mi::neuraylib::IModule>(_moduleDbName.c_str()));
        if (!module) {
            TF_WARN("%s\tcould not access IModule '%s'.", _debugCtx.format().c_str(), _moduleDbName.c_str());
        } else {
            // The incoming path may still be authored (MDL-search-path-only
            // lookup) or resolve to a different storage identifier. The SDK
            // retains the exact filename supplied by the entity resolver.
            _resolvedPath = TfSafeString(module->get_filename());
            _isEmbedded = _resolvedPath.empty();

            // Embedded modules have a logical MDL name even without a file.
            // For file-backed modules, preserve the existing search-root policy.
            mi::base::Handle<const mi::IString> pathName(_isEmbedded ? nullptr :
                ctx.getMdlImpExpApi()->get_mdl_module_name(_resolvedPath.c_str()));
            const char* mdlName = _isEmbedded ? module->get_mdl_name()
                : (pathName ? pathName->get_c_str() : nullptr);
            if (_isEmbedded) {
                _resolvedPath = _ResolveEmbeddedModuleProxy(
                    assetPath, mdlName, ctx.getEntityResolver().get());
            }
            _debugCtx.name = _resolvedPath.c_str();

            // Off-search-path loading is outside the supported content setup.
            if (TfSafeString(mdlName) == Utils::BuiltinModuleName) {
                _moduleUsdIdentifier = TfToken(Utils::BuiltinModuleAsset);
            } else if (mdlName && TfStringStartsWith(mdlName, "::")) {
                mi::base::Handle<const mi::IString> decoded(
                    ctx.getMdlFactory()->decode_name(mdlName));
                _moduleUsdIdentifier = TfToken(
                    TfStringReplace(std::string(decoded->get_c_str()).substr(2), "::", "/") + ".mdl");
            }

            // Each definition inherits the module annotations before adding its own.
            const mi::base::Handle<const mi::neuraylib::IAnnotation_block> modAnnoBlock(module->get_annotations());
            static const std::vector<std::string> annotationPrefixes = {
                UsdMdlMetadataTokens->Annotation.GetString(),
                UsdMdlMetadataTokens->AnnotationModule.GetString()
            };
            Annotation::GetMetadata(modAnnoBlock.get(), _debugCtx, _metadataDict, annotationPrefixes);

            for (mi::Size i = 0, n = module->get_function_count(); i < n; ++i) {
                _LoadFunction(module->get_function(i), _transaction, module.get());
            }
            for (mi::Size i = 0, m = module->get_material_count(); i < m; ++i) {
                _LoadFunction(module->get_material(i), _transaction, module.get());
            }

            ok = true;
        }
    }

    if (ok) {
        _isValid = true;
    }
}

const VtDictionary&
MdlModule::GetMetadata() const
{
    return _metadataDict;
}

SdfAssetPath
MdlModule::ResolveResourceAssetPath(
    const mi::neuraylib::IValue_resource* val) const
{
    if (!val) {
        return {};
    }

    const char* const fp = val->get_file_path();
    if (fp && usdMdl::runtime::IsRejectedResourcePath(fp)) {
        return {};
    }
    const char* const dbName = val->get_value();
    std::string assetPath =
        fp && fp[0]
        ? usdMdl::runtime::path::NormalizeMdlResourceFilename(fp)
        : std::string();
    std::string resolvedPath;

    // The DB element is needed both for the fallback authored path and for the
    // physical filename produced by MDL resource resolution.
    if (!dbName || !dbName[0]) {
        return SdfAssetPath(assetPath);
    }

    usdMdl::runtime::Context& ctx = usdMdl::runtime::Context::instance();
    auto useResolvedFile = [&](const char* original, const char* resolved) {
        if (assetPath.empty()) {
            assetPath = usdMdl::runtime::path::NormalizeMdlResourceFilename(original);
        }
        resolvedPath = TfSafeString(resolved);
    };

    {
        using K = mi::neuraylib::IValue::Kind;
        switch (val->get_kind()) {
        case K::VK_TEXTURE: {
            mi::base::Handle<const mi::neuraylib::ITexture> const tex(
                _transaction->access<mi::neuraylib::ITexture>(dbName));
            const char* const imageDb = tex ? tex->get_image() : nullptr;
            if (imageDb && imageDb[0]) {
                mi::base::Handle<const mi::neuraylib::IImage> const image(
                    _transaction->access<mi::neuraylib::IImage>(imageDb));
                if (image) {
                    if (assetPath.empty()) {
                        assetPath = usdMdl::runtime::path::NormalizeMdlResourceFilename(
                            image->get_original_filename());
                    }
                    if (!image->is_animated() && !image->is_uvtile()) {
                        resolvedPath = TfSafeString(image->get_filename(0, 0));
                    }
                }
            }
            break;
        }
        case K::VK_LIGHT_PROFILE: {
            mi::base::Handle<const mi::neuraylib::ILightprofile> const lp(
                _transaction->access<mi::neuraylib::ILightprofile>(dbName));
            if (lp) {
                useResolvedFile(lp->get_original_filename(), lp->get_filename());
            }
            break;
        }
        case K::VK_BSDF_MEASUREMENT: {
            mi::base::Handle<const mi::neuraylib::IBsdf_measurement> const bm(
                _transaction->access<mi::neuraylib::IBsdf_measurement>(dbName));
            if (bm) {
                useResolvedFile(bm->get_original_filename(), bm->get_filename());
            }
            break;
        }
        default:
            break;
        }
    }

    // Resolve the mask as well as its concrete resources. This gives sequence
    // and UV-tile defaults one resolved identifier with their markers intact.
    if (!assetPath.empty() && resolvedPath.empty()) {
        if (const auto& resolver = ctx.getEntityResolver()) {
            mi::base::Handle<mi::neuraylib::IMdl_resolved_resource> const resource(
                resolver->resolve_resource(
                    assetPath.c_str(),
                    _resolvedPath.empty() ? nullptr : _resolvedPath.c_str(),
                    _qualifiedModuleName.empty() ? nullptr : _qualifiedModuleName.c_str(),
                    0,
                    0,
                    nullptr));
            if (resource) {
                resolvedPath = TfSafeString(resource->get_filename_mask());
            }
        }
    }

    if (assetPath.empty()) {
        return {};
    }
    // Preserve Ar's resolved identifier, including native path spelling and
    // resolver-specific URI syntax, so concrete resources open directly in Ar.
    return resolvedPath.empty()
        ? SdfAssetPath(assetPath)
        : SdfAssetPath(assetPath, resolvedPath);
}

void
MdlModule::_LoadFunction(
    const char* funcDatabaseName,
    mi::neuraylib::ITransaction* t,
    const mi::neuraylib::IModule* module)
{
    if (!funcDatabaseName || !t || !module) {
        return;
    }

    mi::base::Handle<const mi::neuraylib::IFunction_definition> const definition(
        t->access<mi::neuraylib::IFunction_definition>(funcDatabaseName));
    if (!definition) {
        TF_WARN("%s\tUnable to access function '%s'.", _debugCtx.format().c_str(), funcDatabaseName);
        return;
    }

    if (!Utils::IsEligibleDefinition(definition.get())) {
        return;
    }

    Function function(
        funcDatabaseName,
        definition.get(),
        module,
        _moduleUsdIdentifier,
        this);

    const std::string dbKey(funcDatabaseName);
    _functionsByDbName.emplace(dbKey, std::move(function));
}

SdrShaderNodeUniquePtr
MdlModule::GetArrayConstructorNode(const SdrShaderNodeDiscoveryResult& discoveryRes,
                                 const mi::neuraylib::IType_list* inputs,
                                 const mi::neuraylib::IType* output,
                                 const std::string& sdkName)
{
    if (!_isValid || !inputs || !output) {
        return {};
    }
    const char* dbName = "mdl::T[](...)";
    mi::base::Handle<const mi::neuraylib::IFunction_definition> definition(
        _transaction->access<mi::neuraylib::IFunction_definition>(dbName));
    mi::base::Handle<const mi::neuraylib::IModule> builtinModule(definition
        ? _transaction->access<mi::neuraylib::IModule>(definition->get_module()) : nullptr);
    if (!definition || !builtinModule) {
        return {};
    }
    // SDK definition ownership remains builtin; the USD module context is the
    // element owner so authoring metadata and resolved URIs retain the anchor.
    Function function(dbName, definition.get(), builtinModule.get(), _moduleUsdIdentifier, this);
    return function.GetSdrFromDiscoveryResult(discoveryRes, _transaction, inputs, output, sdkName);
}

SdrShaderNodeUniquePtr
MdlModule::GetSdrFromDiscoveryResult(const SdrShaderNodeDiscoveryResult& discoveryRes) const
{
    if (!_isValid) {
        return SdrParserPlugin::GetInvalidShaderNode(discoveryRes);
    }

    // Prefer stable MDL SDK DB name from discovery (see `_PreparseModule` in mdlSdrIntegration.cpp).
    if (!discoveryRes.blindData.empty()) {
        const auto itDb = _functionsByDbName.find(discoveryRes.blindData);
        if (itDb != _functionsByDbName.end()) {
            return itDb->second.GetSdrFromDiscoveryResult(discoveryRes, _transaction);
        }
    }

    Utils::MdlSubIdentifier request;
    if (!Utils::ParseMdlSubIdentifier(discoveryRes.subIdentifier.GetString(), request)) {
        TF_WARN("%s: invalid MDL sub-identifier '%s'.", _debugCtx.format().c_str(),
                discoveryRes.subIdentifier.GetText());
        return SdrParserPlugin::GetInvalidShaderNode(discoveryRes);
    }

    std::vector<const Function*> matches;
    const bool allowBareFallback = !request.hasSignature
        && TfGetEnvSetting(USD_MDL_ALLOW_FIRST_OVERLOAD);
    const bool preferStructElemental = allowBareFallback
        && TfGetEnvSetting(USD_MDL_PREFER_STRUCT_ELEMENTAL_CONSTRUCTOR);
    const Function* structElemental = nullptr;
    const bool allowMaterialMismatch = request.hasSignature
        && TfGetEnvSetting(USD_MDL_ALLOW_MATERIAL_SIGNATURE_MISMATCH);
    const Function* materialFallback = nullptr;
    {
        mi::base::Handle<const mi::neuraylib::IModule> const module(
            _transaction->access<mi::neuraylib::IModule>(_moduleDbName.c_str()));
        if (module) {
            const auto consider = [&](const char* dbName) {
                const auto it = _functionsByDbName.find(dbName);
                if (it == _functionsByDbName.end()) {
                    return; // Not an exported, supported definition.
                }
                mi::base::Handle<const mi::neuraylib::IFunction_definition> def(
                    _transaction->access<mi::neuraylib::IFunction_definition>(dbName));
                if (def && Utils::MatchesMdlSubIdentifier(request, def.get())) {
                    matches.push_back(&it->second);
                    if (preferStructElemental && !structElemental
                        && def->get_semantic() == mi::neuraylib::IFunction_definition::DS_ELEM_CONSTRUCTOR) {
                        // Constructor semantics avoid mistaking ordinary functions
                        // returning a struct for field-input constructors.
                        mi::base::Handle<const mi::neuraylib::IType> returnType(def->get_return_type());
                        mi::base::Handle<const mi::neuraylib::IType> type(returnType->skip_all_type_aliases());
                        if (type->get_kind() == mi::neuraylib::IType::TK_STRUCT) {
                            structElemental = &it->second;
                        }
                    }
                } else if (def && allowMaterialMismatch && def->is_material()
                           && request.name == def->get_mdl_simple_name()) {
                    // Materials have no overloads. Keep the eligible name match
                    // only as a fallback, after exact matching has failed.
                    materialFallback = &it->second;
                }
            };
            // Preserve SDK enumeration order for the opt-in bare-name fallback.
            for (mi::Size i = 0; i < module->get_function_count(); ++i) {
                consider(module->get_function(i));
            }
            for (mi::Size i = 0; i < module->get_material_count(); ++i) {
                consider(module->get_material(i));
            }
        }
    }
    if (matches.size() == 1) {
        return matches.front()->GetSdrFromDiscoveryResult(discoveryRes, _transaction);
    }
    if (matches.empty() && materialFallback) {
        TF_WARN("%s: ignoring mismatched material signature '%s'; selected '%s' "
                "because USD_MDL_ALLOW_MATERIAL_SIGNATURE_MISMATCH is enabled.",
                _debugCtx.format().c_str(), discoveryRes.subIdentifier.GetText(),
                materialFallback->GetMdlNameWithSignature().GetText());
        return materialFallback->GetSdrFromDiscoveryResult(discoveryRes, _transaction);
    }
    if (matches.size() > 1) {
        if (allowBareFallback) {
            if (structElemental) {
                TF_WARN("%s: ambiguous MDL struct sub-identifier '%s'; selected elemental "
                        "constructor '%s' because USD_MDL_PREFER_STRUCT_ELEMENTAL_CONSTRUCTOR "
                        "and USD_MDL_ALLOW_FIRST_OVERLOAD are enabled.",
                        _debugCtx.format().c_str(), discoveryRes.subIdentifier.GetText(),
                        structElemental->GetSubIdentifier().GetText());
                return structElemental->GetSdrFromDiscoveryResult(discoveryRes, _transaction);
            }
            WARN_MULTIPLE_OVERLOADS(_debugCtx, discoveryRes.subIdentifier.GetText(),
                                   matches.front()->GetSubIdentifier().GetText());
            return matches.front()->GetSdrFromDiscoveryResult(discoveryRes, _transaction);
        }
        std::vector<std::string> choices;
        for (const Function* function : matches) {
            choices.push_back(function->GetSubIdentifier().GetString());
        }
        TF_WARN("%s: ambiguous MDL sub-identifier '%s'; use one of: %s.",
                _debugCtx.format().c_str(), discoveryRes.subIdentifier.GetText(),
                TfStringJoin(choices, ", ").c_str());
    } else {
        TF_WARN("%s: no definition matches MDL sub-identifier '%s'.",
                _debugCtx.format().c_str(), discoveryRes.subIdentifier.GetText());
    }
    return SdrParserPlugin::GetInvalidShaderNode(discoveryRes);
}

PXR_NAMESPACE_CLOSE_SCOPE
