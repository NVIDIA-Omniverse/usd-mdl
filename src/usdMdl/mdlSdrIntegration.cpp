//
// SPDX-License-Identifier: Apache-2.0
//

#include "mdlSdrIntegration.h"
#include "addModuleToRegistry.h"
#include "shaderDefinitionsInternal.h"

#include "utils.h"
#include "tokens.h"
#include "ModuleLoader.h"
#include "Context.h"
#include "UriEncoding.h"

#include <mi/mdl_sdk.h>

#include <pxr/base/arch/fileSystem.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/hash.h>
#include <pxr/base/tf/instantiateSingleton.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>

#include <cstdlib>
#include <charconv>
#include <limits>
#include <memory>
#include <mutex>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

bool _SameInterface(const SdrShaderNode& left, const SdrShaderNode& right)
{
    if (left.GetShaderVersion() != right.GetShaderVersion()
        || left.GetShaderVersion().IsDefault() != right.GetShaderVersion().IsDefault()
        || left.GetName() != right.GetName() || left.GetFamily() != right.GetFamily()
        || left.GetContext() != right.GetContext() || left.GetMetadata() != right.GetMetadata()) return false;
    for (const bool output : {false, true}) {
        const auto& names = output ? left.GetShaderOutputNames() : left.GetShaderInputNames();
        const auto& otherNames = output ? right.GetShaderOutputNames() : right.GetShaderInputNames();
        if (names != otherNames) return false;
        for (const auto& name : names) {
            const auto* a = output ? left.GetShaderOutput(name) : left.GetShaderInput(name);
            const auto* b = output ? right.GetShaderOutput(name) : right.GetShaderInput(name);
            if (!a || !b || a->GetType() != b->GetType() || a->GetArraySize() != b->GetArraySize()
                || a->GetDefaultValue() != b->GetDefaultValue()
                || a->GetMetadata() != b->GetMetadata() || a->GetHints() != b->GetHints()
                || a->GetOptions() != b->GetOptions()) return false;
        }
    }
    return true;
}

SdrShaderNodeUniquePtr _CloneShaderNode(const SdrShaderNode& node)
{
    SdrShaderPropertyUniquePtrVec properties;
    for (const bool output : {false, true}) {
        const auto& names = output ? node.GetShaderOutputNames() : node.GetShaderInputNames();
        for (const auto& name : names) {
            const auto* property = output ? node.GetShaderOutput(name) : node.GetShaderInput(name);
            if (!property) return {};
            properties.emplace_back(new SdrShaderProperty(
                name, property->GetType(), property->GetDefaultValue(), output,
                property->GetArraySize(), property->GetMetadata(),
                property->GetHints(), property->GetOptions()));
        }
    }
    return SdrShaderNodeUniquePtr(new SdrShaderNode(
        node.GetIdentifier(), node.GetShaderVersion(), node.GetName(), node.GetFamily(),
        node.GetContext(), node.GetSourceType(), node.GetResolvedDefinitionURI(),
        node.GetResolvedImplementationURI(), std::move(properties), node.GetMetadata(),
        node.GetSourceCode()));
}

bool _MatchesPendingNode(const SdrShaderNode& node, const SdrShaderNodeDiscoveryResult& result)
{
    if (node.GetName() != result.name || node.GetFamily() != result.family
        || node.GetSourceType() != result.sourceType
        || node.GetShaderVersion() != result.version
        || node.GetShaderVersion().IsDefault() != result.version.IsDefault()) return false;
    const auto definition = node.GetMetadata().find(UsdMdlNodeMetadataTokens->MdlDbDefinitionName);
    if (!result.blindData.empty()) {
        return definition != node.GetMetadata().end() && definition->second == result.blindData
            && node.GetResolvedDefinitionURI() == node.GetResolvedImplementationURI();
    }
    const auto expected = result.metadata.find(UsdMdlNodeMetadataTokens->MdlDbDefinitionName);
    return expected == result.metadata.end()
        || (definition != node.GetMetadata().end() && definition->second == expected->second
            && node.GetResolvedDefinitionURI() == result.resolvedUri);
}

using mi::neuraylib::IMdl_info;
using mi::neuraylib::IMdl_module_info;
using mi::neuraylib::IMdl_package_info;

// Declare this owner before all module/definition/converter handles so they
// are released first, including on early returns and exception unwinding.
// Inspection writes only USD data; abort closes its snapshot without a commit.
class InspectionTransaction
{
public:
    explicit InspectionTransaction(usdMdl::runtime::Context& context)
        : _transaction(context.createTransaction())
    {
    }

    ~InspectionTransaction()
    {
        if (_transaction) {
            _transaction->abort();
        }
    }

    InspectionTransaction(const InspectionTransaction&) = delete;
    InspectionTransaction& operator=(const InspectionTransaction&) = delete;

    mi::neuraylib::ITransaction* get() const { return _transaction.get(); }

private:
    mi::base::Handle<mi::neuraylib::ITransaction> _transaction;
};

/// Snapshot the active MDL search paths from `IMdl_configuration` as a
/// vector of strings.  Used by `_DeriveQualifiedName` to drive the
/// search-path-relative shortening branch in `EncodeUriToMdlName`.
std::vector<std::string>
_SnapshotMdlSearchPaths(usdMdl::runtime::Context& ctx)
{
    std::vector<std::string> out;
    const auto& neuray = ctx.getNeuray();
    if (!neuray) {
        return out;
    }
    mi::base::Handle<mi::neuraylib::IMdl_configuration> cfg(
        neuray->get_api_component<mi::neuraylib::IMdl_configuration>());
    if (!cfg) {
        return out;
    }
    const mi::Size n = cfg->get_mdl_paths_length();
    out.reserve(n);
    for (mi::Size i = 0; i < n; ++i) {
        mi::base::Handle<const mi::IString> const p(cfg->get_mdl_path(i));
        if (p && p->get_c_str() && p->get_c_str()[0]) {
            out.emplace_back(p->get_c_str());
        }
    }
    return out;
}

/// Compute the qualified MDL name to hand to load_module for the asset at
/// \p assetPath. Map the builtin alias first, then try get_mdl_module_name, which
/// requires the file to live under some MDL search path.  If that fails,
/// produces a deterministic URI-encoded name via
/// `usdMdl::runtime::EncodeUriToMdlName`; the entity resolver decodes that
/// back to a URL and fetches via Ar.  Returns an empty string only if
/// both paths fail.
std::string
_DeriveQualifiedName(
    usdMdl::runtime::Context& ctx,
    const SdfAssetPath& assetPath)
{
    if (Utils::IsBuiltinModuleAsset(assetPath)) {
        return Utils::BuiltinModuleName;
    }
    const std::string& resolvedPath = assetPath.GetResolvedPath().empty()
        ? assetPath.GetAssetPath() : assetPath.GetResolvedPath();
    if (resolvedPath.empty()) {
        return {};
    }

    mi::base::Handle<const mi::IString> const mdlName(
        ctx.getMdlImpExpApi()->get_mdl_module_name(resolvedPath.c_str()));
    if (mdlName && mdlName->get_c_str() && mdlName->get_c_str()[0]) {
        return mdlName->get_c_str();
    }

    // Off-search-path asset: encode the URL as an MDL module name.
    return usdMdl::runtime::EncodeUriToMdlName(
        resolvedPath, _SnapshotMdlSearchPaths(ctx));
}

bool
_RunMdlDiscovery(
    usdMdl::runtime::Context& ctx,
    mi::base::Handle<const mi::neuraylib::IMdl_discovery_result>& discOut)
{
    discOut.reset();

    const auto& api = ctx.getMdlDiscoveryApi();
    if (!api) {
        TF_WARN("MdlSdrIntegration: IMdl_discovery_api is null.");
        return false;
    }

    const mi::Uint32 filter = static_cast<mi::Uint32>(mi::neuraylib::IMdl_info::DK_PACKAGE)
        | static_cast<mi::Uint32>(mi::neuraylib::IMdl_info::DK_MODULE)
        | static_cast<mi::Uint32>(mi::neuraylib::IMdl_info::DK_DIRECTORY);

    mi::base::Handle<const mi::neuraylib::IMdl_discovery_result> const discResult(api->discover(filter));
    if (!discResult) {
        TF_WARN("MdlSdrIntegration: discover() returned null.");
        return false;
    }

    discOut = discResult;
    return true;
}

// The source module has already been loaded and committed before this view.
// Both native lookup and USD interface registration validate one spelling here.
// The caller must hold _parsedNodesMutex while using SDK specialization APIs.
bool
_ResolveArrayConstructorTypes(usdMdl::runtime::Context& ctx,
                              mi::neuraylib::ITransaction* transaction,
                              const std::string& qualifiedName,
                              const TfToken& subIdentifier,
                              MdlSdrIntegration::ArrayConstructorTypes& typesOut)
{
    using namespace mi::neuraylib;
    const std::string& name = subIdentifier.GetString();
    typesOut = {};
    auto fail = [&](const char* reason) {
        TF_WARN("MDL array constructor '%s': %s", name.c_str(), reason);
        return false;
    };
    const std::string prefix = "T[](...)<";
    const auto comma = name.rfind(',');
    if (!TfStringStartsWith(name, prefix) || name.back() != '>'
        || comma == std::string::npos || comma <= prefix.size()) {
        return fail("expected T[](...)<elementType,positiveCount>.");
    }
    const std::string elementName = name.substr(prefix.size(), comma - prefix.size());
    const char* begin = name.data() + comma + 1;
    const char* end = name.data() + name.size() - 1;
    size_t count = 0;
    const auto parsed = std::from_chars(begin, end, count);
    if (parsed.ec != std::errc() || parsed.ptr != end
        || count > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return fail("invalid or unrepresentable element count.");
    }
    if (!count) {
        return fail("zero elements require a typed empty constant, not a constructor node.");
    }
    // Only local element names are authored. The SDK validates their spelling
    // after expansion against the source module; no relative/import lookup.
    if (elementName.find_first_of("<>,: \t\r\n") != std::string::npos
        || name.substr(comma + 1, name.size() - comma - 2) != std::to_string(count)) {
        return fail("expected a local element type and canonical decimal count.");
    }
    if (!transaction) {
        return fail("could not open an SDK inspection transaction.");
    }
    const std::string sdkElementName = qualifiedName == Utils::BuiltinModuleName
        ? elementName : qualifiedName + "::" + elementName;
    const std::string sdkName = prefix + sdkElementName + "," + std::to_string(count) + ">";
    const auto& api = ctx.getMdlImpExpApi();
    mi::base::Handle<const IDeserialized_function_name> deserialized(
        api->deserialize_function_name(transaction, ("mdl::" + sdkName).c_str(), nullptr, nullptr));
    if (!deserialized || TfSafeString(deserialized->get_db_name()) != "mdl::T[](...)") {
        return fail("SDK rejected the specialization.");
    }
    mi::base::Handle<const IType_list> inputs(deserialized->get_argument_types());
    if (!inputs || inputs->get_size() != count) {
        return fail("SDK specialization has an unexpected argument count.");
    }
    mi::base::Handle<const ISerialized_function_name> serialized(
        api->serialize_function_name(deserialized->get_db_name(), inputs.get(), nullptr, nullptr, nullptr));
    if (!serialized || sdkName != TfSafeString(serialized->get_function_name_without_module_name())) {
        return fail("specialization is not in canonical SDK serialized form.");
    }
    mi::base::Handle<IType_factory> types(ctx.getMdlFactory()->create_type_factory(transaction));
    mi::base::Handle<const IType> element(inputs->get_type(mi::Size(0)));
    if (!types || !element) {
        return fail("SDK element type information is unavailable.");
    }
    mi::base::Handle<const mi::IString> owner(types->get_mdl_module_name(element.get()));
    if (!owner || qualifiedName != TfSafeString(owner->get_c_str())) {
        return fail("source asset must identify the element type's defining module.");
    }
    mi::base::Handle<const IType_array> output(types->create_immediate_sized_array(element.get(), count));
    if (!output) {
        return fail("SDK could not create the specialized return type.");
    }
    typesOut.parameters = inputs;
    typesOut.returnType = output;
    typesOut.sdkName = sdkName;
    return true;
}

// The source asset's module owns the local element type; the implementation
// itself comes from the SDK's builtin module. All handles precede view abort.
SdrShaderNodeUniquePtr
_CreateArrayConstructor(usdMdl::runtime::Context& ctx,
                        const std::string& qualifiedName,
                        const SdfAssetPath& assetPath,
                        const SdrShaderNodeDiscoveryResult& request)
{
    InspectionTransaction inspection(ctx);
    MdlSdrIntegration::ArrayConstructorTypes types;
    if (!_ResolveArrayConstructorTypes(ctx, inspection.get(), qualifiedName,
                                       request.subIdentifier, types)) {
        return {};
    }
    MdlModule module(qualifiedName, assetPath, inspection.get());
    auto node = module.GetArrayConstructorNode(request, types.parameters.get(),
                                               types.returnType.get(), types.sdkName);
    if (!node || !node->IsValid()) {
        TF_WARN("MDL array constructor '%s': specialized ports cannot be represented by this Sdr plugin.",
                request.subIdentifier.GetText());
        return {};
    }
    return node;
}

/// Eagerly load \p qualifiedName + parse the resulting `IModule`.  For each
/// exported, non-removed function/material definition this produces:
///   - one SdrShaderNodeDiscoveryResult appended to \p results,
///   - one SdrShaderNodeUniquePtr stored in \p parsedNodes keyed by the
///     same identifier the parser plugin will see at parse time.
/// The transient `MdlModule` (and the underlying ITransaction it opens
/// internally) is dropped on return; ownership of the parsed nodes
/// transfers to \p parsedNodes.  Returns true if at least one node was
/// produced.
///
/// **The caller must hold `MdlSdrIntegration::_parsedNodesMutex`** while
/// invoking this function.  `_parsedNodes` mutation happens unprotected
/// here on purpose: callers serialize concurrent parser-plugin invocations
/// from Sdr's TBB-parallel parse path by acquiring that mutex once for
/// the whole preparse + lookup window.
bool
_PreparseModule(
    usdMdl::runtime::Context& ctx,
    const std::string& qualifiedName,
    const SdfAssetPath& assetPath,
    const std::string& resolvedPath,
    SdrShaderNodeDiscoveryResultVec& results,
    std::unordered_map<SdrIdentifier,
                       SdrShaderNodeUniquePtr,
                       SdrIdentifierHashFunctor>& parsedNodes)
{
    if (qualifiedName.empty() || resolvedPath.empty()) {
        return false;
    }

    if (ctx.getModuleLoader().loadModule(qualifiedName.c_str()) < 0) {
        TF_WARN("MdlSdrIntegration::_PreparseModule: load_module failed for '%s'.",
                qualifiedName.c_str());
        return false;
    }

    mi::base::Handle<const mi::IString> const moduleDbName(
        ctx.getMdlFactory()->get_db_module_name(qualifiedName.c_str()));
    if (!moduleDbName || !moduleDbName->get_c_str() || !moduleDbName->get_c_str()[0]) {
        TF_WARN("MdlSdrIntegration::_PreparseModule: get_db_module_name failed for '%s'.",
                qualifiedName.c_str());
        return false;
    }

    // Open the snapshot only after the load batch commits. Module inspection,
    // node conversion and referenced call/resource lookups share this view.
    InspectionTransaction inspection(ctx);
    mi::neuraylib::ITransaction* const txn = inspection.get();
    if (!txn) {
        return false;
    }
    MdlModule module(qualifiedName, assetPath, txn);
    if (!module.IsValid()) {
        TF_WARN("MdlSdrIntegration::_PreparseModule: could not parse module '%s'.",
                qualifiedName.c_str());
        return false;
    }

    // Identifier prefix mirrors SdrRegistry::_GetIdentifierForAsset; the
    // OpenUSD core hashes the SdfAssetPath the same way when an asset is
    // registered via GetShaderNodeForSourceType, so the identifiers we
    // pre-compute here line up with the ones Sdr later asks the parser to
    // produce.
    const std::string hashString = std::to_string(TfHash()(assetPath));
    const std::string baseName = TfGetBaseName(resolvedPath);

    bool produced = false;
    {
        mi::base::Handle<const mi::neuraylib::IModule> const mdlModule(
            txn->access<mi::neuraylib::IModule>(moduleDbName->get_c_str()));
        if (!mdlModule) {
            TF_WARN("MdlSdrIntegration::_PreparseModule: could not access IModule for '%s'.",
                    qualifiedName.c_str());
        } else {
            auto const emitOne = [&](const char* funcDbName) {
                if (!funcDbName) {
                    return;
                }
                mi::base::Handle<const mi::neuraylib::IFunction_definition> const def(
                    txn->access<mi::neuraylib::IFunction_definition>(funcDbName));
                if (!Utils::IsEligibleDefinition(def.get())) {
                    return;
                }
                if (def->get_semantic() == mi::neuraylib::IFunction_definition::DS_INTRINSIC_DAG_ARRAY_CONSTRUCTOR) {
                    // Concrete specializations are requested on demand. The
                    // generic template has no usable editor port interface.
                    return;
                }

                const TfToken subId =
                    Utils::ComputeMdlSubIdentifier(def.get(), mdlModule.get());

                SdrIdentifier identifier = SdrIdentifier(
                    TfStringPrintf("%s<%s><%s>", hashString.c_str(),
                                   subId.GetText(), UsdMdlTokens->Mdl.GetText()));

                // blindData carries the SDK DB element name so the parser
                // can match this exact definition without resorting to
                // subIdentifier-only lookup.
                const std::string blindData = std::string(funcDbName);

                SdrShaderNodeDiscoveryResult dr(
                    identifier,
                    SdrVersion().GetAsDefault(),
                    baseName,
                    TfToken(),
                    UsdMdlTokens->Mdl,
                    UsdMdlTokens->Mdl,
                    assetPath.GetAssetPath(),
                    resolvedPath,
                    std::string(),
                    SdrTokenMap(),
                    blindData,
                    subId);

                SdrShaderNodeUniquePtr node = module.GetSdrFromDiscoveryResult(dr);
                if (!node || !node->IsValid()) {
                    // Sdr permits non-null failure placeholders. They must not
                    // be advertised as successfully registered definitions.
                    TF_WARN("MdlSdrIntegration::_PreparseModule: could not parse definition '%s'; "
                            "skipping registration.", funcDbName);
                    return;
                }

                const auto pending = parsedNodes.find(identifier);
                if (pending != parsedNodes.end() && (!_MatchesPendingNode(*pending->second, dr))) {
                    TF_WARN("MDL native definition '%s': identifier has a different pending interface.",
                            identifier.GetText());
                    return;
                }
                parsedNodes.emplace(identifier, std::move(node));
                results.emplace_back(std::move(dr));
                produced = true;
            };

            for (mi::Size i = 0, n = mdlModule->get_function_count(); i < n; ++i) {
                emitOne(mdlModule->get_function(i));
            }
            for (mi::Size i = 0, m = mdlModule->get_material_count(); i < m; ++i) {
                emitOne(mdlModule->get_material(i));
            }
        }
    }
    return produced;
}

/// Walk the discovery graph rooted at \p info, calling `_PreparseModule`
/// for each `DK_MODULE` node.  The caller (GetDiscoveryResults) passes
/// `mu` so we can take the lock per-module rather than across the whole
/// recursion -- worker threads that arrive via Sdr's TBB-parallel parse
/// path don't need to block on the entire walk.
void
_WalkMdlDiscoveryGraph(
    usdMdl::runtime::Context& ctx,
    const IMdl_info* info,
    SdrShaderNodeDiscoveryResultVec& results,
    std::unordered_map<SdrIdentifier,
                       SdrShaderNodeUniquePtr,
                       SdrIdentifierHashFunctor>& parsedNodes,
    std::mutex& mu)
{
    if (!info) {
        return;
    }

    switch (info->get_kind()) {
    case IMdl_info::DK_MODULE: {
        mi::base::Handle<const IMdl_module_info> const mod(
            info->get_interface<const IMdl_module_info>());
        if (!mod) {
            break;
        }
        mi::base::Handle<const mi::IString> const resolved(mod->get_resolved_path());
        const std::string resolvedPath = resolved ? resolved->get_c_str() : "";
        const char* const qn = info->get_qualified_name();
        if (!qn || !qn[0]) {
            break;
        }

        const SdfAssetPath assetPath(resolvedPath, resolvedPath);
        std::lock_guard<std::mutex> lock(mu);
        // File discovery sees the optional dummy, but the SDK must load the
        // embedded module rather than compile that file as ordinary source.
        const char* qualifiedName = Utils::IsBuiltinModuleAsset(assetPath)
            ? Utils::BuiltinModuleName : qn;
        _PreparseModule(ctx, qualifiedName, assetPath, resolvedPath, results, parsedNodes);
        break;
    }
    case IMdl_info::DK_PACKAGE:
    case IMdl_info::DK_DIRECTORY: {
        mi::base::Handle<const IMdl_package_info> const pkg(
            info->get_interface<const IMdl_package_info>());
        if (!pkg) {
            break;
        }
        for (mi::Size i = 0, n = pkg->get_child_count(); i < n; ++i) {
            // IMdl_package_info::get_child() returns a retained interface.
            // Keep it in a Handle so every graph walk releases that reference.
            mi::base::Handle<const IMdl_info> const child(pkg->get_child(i));
            if (child) {
                _WalkMdlDiscoveryGraph(ctx, child.get(), results, parsedNodes, mu);
            }
        }
        break;
    }
    default:
        break;
    }
}

} // namespace

PXR_NAMESPACE_OPEN_SCOPE

TF_INSTANTIATE_SINGLETON(MdlSdrIntegration);

MdlSdrIntegration::MdlSdrIntegration()
{
    EnsureInitialized();
}

bool
MdlSdrIntegration::EnsureInitialized()
{
    if (_mdlSdkReady) {
        return true;
    }

    _mdlSdkReady = usdMdl::runtime::Context::instance().ensureInitialized();
    if (_mdlSdkReady && !_shutdownRegistered) {
        std::atexit([] { MdlSdrIntegration::Get().Shutdown(); });
        _shutdownRegistered = true;
    }
    return _mdlSdkReady;
}

void
MdlSdrIntegration::Shutdown()
{
    if (!_mdlSdkReady) {
        return;
    }

    {
        // Shutdown is externally quiescent, just like initialization. Parser
        // callbacks use only _parsedNodesMutex and never take this mutex.
        std::lock_guard<std::mutex> previewLock(_previewSurfaceMutex);
        _previewSurfaceDiscoveryResults.clear();
        _previewSurfaceInitialized = false;
    }

    // Drop everything we own *before* tearing down the Context.  The only
    // mi::base::Handle we still own here is _lastDiscovery; SdrShaderNode
    // values in _parsedNodes carry only OpenUSD value types (TfToken,
    // std::string, ...) and are safe to release in any order relative to
    // shutdown().  Lock acquisition order respects the invariant
    // documented on `_discoveryMutex`: discovery state first, then
    // parsed-node cache.
    {
        std::lock_guard<std::mutex> discoveryLock(_discoveryMutex);
        std::lock_guard<std::mutex> parsedLock(_parsedNodesMutex);
        _parsedNodes.clear();
        _previewSurfaceNodes.clear();
        _lastDiscovery.reset();
        _searchPaths.clear();
        _searchPathsInitialized = false;
        _discoveryInitialized = false;
    }
    _mdlSdkReady = false;

    usdMdl::runtime::Context::instance().shutdown();
}

MdlSdrIntegration::~MdlSdrIntegration()
{
    Shutdown();
}

std::vector<SdrShaderNodeUniquePtr>
MdlSdrIntegration::PrepareNativeModuleNodes(const SdfAssetPath& assetPath)
{
    auto& self = Get();
    if (!self._mdlSdkReady) return {};
    auto& context = usdMdl::runtime::Context::instance();
    const std::string path = assetPath.GetResolvedPath().empty()
        ? assetPath.GetAssetPath() : assetPath.GetResolvedPath();
    const std::string qualifiedName = _DeriveQualifiedName(context, assetPath);
    if (path.empty() || qualifiedName.empty()) return {};
    SdrShaderNodeDiscoveryResultVec results;
    std::unordered_map<SdrIdentifier, SdrShaderNodeUniquePtr, SdrIdentifierHashFunctor> owned;
    // Match ordinary parser SDK serialization, but keep all nodes local and
    // never publish discovery results through a registry that is constructing.
    std::lock_guard<std::mutex> parsedLock(self._parsedNodesMutex);
    if (!_PreparseModule(context, qualifiedName, assetPath, path, results, owned)) return {};
    std::vector<SdrShaderNodeUniquePtr> nodes;
    for (const auto& result : results) {
        const auto found = owned.find(result.identifier);
        if (found != owned.end()) nodes.push_back(std::move(found->second));
    }
    return nodes;
}

SdrShaderNodeUniquePtr
MdlSdrIntegration::PrepareArrayConstructorNode(
    const SdfAssetPath& assetPath, const TfToken& subIdentifier)
{
    auto& self = Get();
    if (!self._mdlSdkReady) return {};
    auto& context = usdMdl::runtime::Context::instance();
    const std::string path = assetPath.GetResolvedPath().empty()
        ? assetPath.GetAssetPath() : assetPath.GetResolvedPath();
    const std::string qualifiedName = _DeriveQualifiedName(context, assetPath);
    if (path.empty() || qualifiedName.empty()) return {};
    std::lock_guard<std::mutex> parsedLock(self._parsedNodesMutex);
    if (context.getModuleLoader().loadModule(qualifiedName.c_str()) < 0) {
        TF_WARN("MdlSdrIntegration::PrepareArrayConstructorNode: load_module failed for '%s'.",
                qualifiedName.c_str());
        return {};
    }
    // Same identity as native asset lookup, independent of any authored USD
    // interface identifier. This owned blueprint is never published to Sdr.
    const SdrIdentifier identifier(TfStringPrintf("%s<%s><%s>",
        std::to_string(TfHash()(assetPath)).c_str(), subIdentifier.GetText(),
        UsdMdlTokens->Mdl.GetText()));
    const SdrShaderNodeDiscoveryResult request(
        identifier, SdrVersion().GetAsDefault(), TfGetBaseName(path), TfToken(),
        UsdMdlTokens->Mdl, UsdMdlTokens->Mdl, assetPath.GetAssetPath(), path,
        std::string(), SdrTokenMap(), std::string(), subIdentifier);
    return _CreateArrayConstructor(context, qualifiedName, assetPath, request);
}

bool
MdlSdrIntegration::ResolveArrayConstructorTypes(
    mi::neuraylib::ITransaction* transaction, const std::string& qualifiedModuleName,
    const TfToken& subIdentifier, ArrayConstructorTypes& types)
{
    auto& self = Get();
    types = {};
    if (!self._mdlSdkReady) return false;
    std::lock_guard<std::mutex> parsedLock(self._parsedNodesMutex);
    return _ResolveArrayConstructorTypes(usdMdl::runtime::Context::instance(),
        transaction, qualifiedModuleName, subIdentifier, types);
}

SdrShaderNodeDiscoveryResultVec
MdlSdrIntegration::GetUsdPreviewSurfaceDiscoveryResults(const SdfAssetPath& definitionsAsset)
{
    if (!_mdlSdkReady) return {};
    std::lock_guard<std::mutex> previewLock(_previewSurfaceMutex);
    if (_previewSurfaceInitialized) return _previewSurfaceDiscoveryResults;
    auto prepared = UsdMdlShaderDefinitions::Prepare(definitionsAsset, SdfAssetPath(), true);
    {
        std::lock_guard<std::mutex> parsedLock(_parsedNodesMutex);
        for (auto& entry : prepared) {
            const auto existing = _parsedNodes.find(entry.first.identifier);
            if (existing != _parsedNodes.end()
                && (!_MatchesPendingNode(*existing->second, entry.first)
                    || !_SameInterface(*existing->second, *entry.second))) {
                TF_WARN("MDL Preview Surface '%s': identifier has a different pending interface.",
                        entry.first.identifier.GetText());
                continue;
            }
            if (existing == _parsedNodes.end()) {
                auto pending = _CloneShaderNode(*entry.second);
                if (!pending) continue;
                _parsedNodes.emplace(entry.first.identifier, std::move(pending));
            }
            _previewSurfaceDiscoveryResults.push_back(entry.first);
            _previewSurfaceNodes.emplace(entry.first.identifier, std::move(entry.second));
        }
    }
    _previewSurfaceInitialized = true;
    return _previewSurfaceDiscoveryResults;
}

std::vector<SdrShaderNodeConstPtr>
MdlSdrIntegration::AddModuleToRegistry(const SdfAssetPath& assetPath)
{
    MdlSdrIntegration& self = Get();
    std::vector<SdrShaderNodeConstPtr> nodes;
    if (!self._mdlSdkReady) {
        return nodes;
    }

    usdMdl::runtime::Context& ctx = usdMdl::runtime::Context::instance();

    const std::string& resolvedPath =
        assetPath.GetResolvedPath().empty() ? assetPath.GetAssetPath() : assetPath.GetResolvedPath();
    if (resolvedPath.empty()) {
        TF_WARN("MdlSdrIntegration::AddModuleToRegistry: empty asset path.");
        return nodes;
    }

    const std::string qualifiedName = _DeriveQualifiedName(ctx, assetPath);
    if (qualifiedName.empty()) {
        TF_WARN("MdlSdrIntegration::AddModuleToRegistry: "
                "could not derive MDL module name from '%s'.", resolvedPath.c_str());
        return nodes;
    }

    SdrShaderNodeDiscoveryResultVec discoveryResults;
    {
        std::lock_guard<std::mutex> lock(self._parsedNodesMutex);
        if (!_PreparseModule(ctx, qualifiedName, assetPath, resolvedPath,
                             discoveryResults, self._parsedNodes)) {
            return nodes;
        }
    }

    // Hand each pre-parsed result to Sdr.  AddDiscoveryResult drives Sdr to
    // call MdlParserPlugin::ParseShaderNode -> GetSdrFromDiscoveryResult,
    // which acquires the lock fresh on this same thread and moves the
    // matching pre-parsed node out of _parsedNodes.  The lock above is
    // released before this loop so that synchronous callback can acquire
    // it without recursion.
    SdrRegistry& sdrReg = SdrRegistry::GetInstance();
    for (const SdrShaderNodeDiscoveryResult& dr : discoveryResults) {
        sdrReg.AddDiscoveryResult(dr);
        SdrShaderNodeConstPtr node =
            sdrReg.GetShaderNodeByIdentifierAndType(dr.identifier, dr.sourceType);
        {
            std::lock_guard<std::mutex> parsedLock(self._parsedNodesMutex);
            self._parsedNodes.erase(dr.identifier);
        }
        const auto definition = node ? node->GetMetadata().find(UsdMdlNodeMetadataTokens->MdlDbDefinitionName)
                                     : SdrTokenMap::const_iterator();
        if (node && node->IsValid() && definition != node->GetMetadata().end()
            && definition->second == dr.blindData
            && node->GetResolvedDefinitionURI() == node->GetResolvedImplementationURI()) {
            nodes.push_back(node);
        } else {
            TF_WARN("MdlSdrIntegration::AddModuleToRegistry: no valid matching native Sdr node for '%s'.",
                    dr.identifier.GetText());
        }
    }

    return nodes;
}

std::vector<SdrShaderNodeConstPtr>
MdlSdrIntegration::AddShaderDefinitionsToRegistry(
    const SdfAssetPath& definitionsAsset, const SdfAssetPath& implementationAsset)
{
    auto& self = Get();
    if (!self._mdlSdkReady) return {};
    std::lock_guard<std::mutex> registrationLock(self._shaderDefinitionsMutex);
    auto prepared = UsdMdlShaderDefinitions::Prepare(definitionsAsset, implementationAsset);
    auto& registry = SdrRegistry::GetInstance();
    std::vector<SdrShaderNodeConstPtr> nodes;
    for (auto& entry : prepared) {
        const auto& result = entry.first;
        const auto* existing = registry.GetShaderNodeByIdentifierAndType(
            result.identifier, result.sourceType);
        if (existing) {
            if (!existing->IsValid()
                || existing->GetResolvedDefinitionURI() != entry.second->GetResolvedDefinitionURI()
                || existing->GetResolvedImplementationURI() != entry.second->GetResolvedImplementationURI()
                || existing->GetMetadata().find(UsdMdlNodeMetadataTokens->MdlDbDefinitionName)
                    == existing->GetMetadata().end()
                || existing->GetMetadata().at(UsdMdlNodeMetadataTokens->MdlDbDefinitionName)
                    != entry.second->GetMetadata().at(UsdMdlNodeMetadataTokens->MdlDbDefinitionName)
                || !_SameInterface(*existing, *entry.second)) {
                TF_WARN("MDL shader definition '%s': registry identifier already has a different binding or interface.",
                        result.identifier.GetText());
                continue;
            }
            nodes.push_back(existing);
            continue;
        }
        const auto* preparedNode = entry.second.get();
        {
            std::lock_guard<std::mutex> parsedLock(self._parsedNodesMutex);
            if (!self._parsedNodes.emplace(result.identifier, std::move(entry.second)).second) {
                TF_WARN("MDL shader definition '%s': identifier has a pending native definition.",
                        result.identifier.GetText());
                continue;
            }
        }
        // The synchronous parser callback acquires _parsedNodesMutex itself.
        registry.AddDiscoveryResult(result);
        const auto* node = registry.GetShaderNodeByIdentifierAndType(
            result.identifier, result.sourceType);
        {
            std::lock_guard<std::mutex> parsedLock(self._parsedNodesMutex);
            self._parsedNodes.erase(result.identifier);
        }
        if (node && node->IsValid() && node == preparedNode) nodes.push_back(node);
        else TF_WARN("MDL shader definition '%s': registry publication failed or identifier changed concurrently.",
                     result.identifier.GetText());
    }
    return nodes;
}

const SdrStringVec&
MdlSdrIntegration::GetSearchPaths() const
{
    // Sdr's API returns this vector by reference, so it must remain immutable
    // after publication. A lock around clear+repopulate alone is insufficient:
    // another caller could mutate the vector after the first caller returns.
    std::lock_guard<std::mutex> lock(_discoveryMutex);

    if (_searchPathsInitialized) {
        return _searchPaths;
    }

    if (!_mdlSdkReady) {
        return _searchPaths;
    }

    usdMdl::runtime::Context& ctx = usdMdl::runtime::Context::instance();
    _searchPaths = _SnapshotMdlSearchPaths(ctx);
    for (std::string& path : _searchPaths) {
        path = TfStringReplace(path, ARCH_PATH_SEP, "/");
        if (!TfStringEndsWith(path, "/")) {
            path += "/";
        }
    }
    _searchPathsInitialized = true;
    return _searchPaths;
}

SdrShaderNodeDiscoveryResultVec
MdlSdrIntegration::GetDiscoveryResults()
{
    if (!_mdlSdkReady) {
        return {};
    }

    mi::base::Handle<const mi::neuraylib::IMdl_discovery_result> pinned;
    {
        std::lock_guard<std::mutex> lock(_discoveryMutex);
        if (!_discoveryInitialized) {
            _RunMdlDiscovery(usdMdl::runtime::Context::instance(), _lastDiscovery);
            _discoveryInitialized = true;
        }
        // Retain the graph while walking outside this lock: the walker takes
        // _parsedNodesMutex per module, and path queries need no graph access.
        pinned = _lastDiscovery;
    }

    SdrShaderNodeDiscoveryResultVec results;
    if (!pinned) {
        return results;
    }

    mi::base::Handle<const mi::neuraylib::IMdl_package_info> const root(pinned->get_graph());
    if (!root) {
        TF_WARN("MdlSdrIntegration: IMdl_discovery_result::get_graph() returned null; no MDL modules "
                "will be enumerated. Check MDL search paths / MDL_PATH / install layout.");
        return results;
    }

    usdMdl::runtime::Context& ctx = usdMdl::runtime::Context::instance();
    _WalkMdlDiscoveryGraph(ctx, root.get(), results, _parsedNodes, _parsedNodesMutex);

    return results;
}

SdrShaderNodeUniquePtr
MdlSdrIntegration::GetSdrFromDiscoveryResult(const SdrShaderNodeDiscoveryResult& discoveryRes)
{
    if (!_mdlSdkReady) {
        return SdrShaderNodeUniquePtr();
    }

    // SdrRegistry parallelizes parser-plugin invocations over TBB; this
    // method is reentered concurrently from multiple worker threads.
    // Hold _parsedNodesMutex for the entire fast/slow path so the
    // cache lookup, on-demand selection, and move-out are atomic.
    // Node construction never re-enters MdlSdrIntegration, so a
    // non-recursive mutex is sufficient.
    std::lock_guard<std::mutex> lock(_parsedNodesMutex);

    // Fast path: the node was pre-built by _PreparseModule during eager
    // discovery or AddModuleToRegistry.  Move it out and erase the entry so
    // the SDK-derived strings backing it can be released.
    {
        const auto it = _parsedNodes.find(discoveryRes.identifier);
        if (it != _parsedNodes.end() && it->second
            && _MatchesPendingNode(*it->second, discoveryRes)) {
            SdrShaderNodeUniquePtr node = std::move(it->second);
            _parsedNodes.erase(it);
            if (node) {
                return node;
            }
        }
    }

    // Automatic USD interfaces retain pure-USD blueprints. Another callback
    // may have consumed the pending node before Sdr finishes inserting it.
    const auto preview = _previewSurfaceNodes.find(discoveryRes.identifier);
    if (preview != _previewSurfaceNodes.end()
        && _MatchesPendingNode(*preview->second, discoveryRes)) {
        return _CloneShaderNode(*preview->second);
    }

    // Slow path: an authored source asset/sub-identifier needs selection in
    // its module. This also handles alternate spellings after registration.
    const std::string& resolvedPath =
        discoveryRes.resolvedUri.empty() ? discoveryRes.uri : discoveryRes.resolvedUri;
    if (resolvedPath.empty()) {
        TF_WARN("MdlSdrIntegration::GetSdrFromDiscoveryResult: empty resolved URI.");
        return SdrShaderNodeUniquePtr();
    }

    usdMdl::runtime::Context& ctx = usdMdl::runtime::Context::instance();
    const SdfAssetPath assetPath(discoveryRes.uri, discoveryRes.resolvedUri);
    const std::string qualifiedName = _DeriveQualifiedName(ctx, assetPath);
    if (qualifiedName.empty()) {
        TF_WARN("MdlSdrIntegration::GetSdrFromDiscoveryResult: "
                "could not derive MDL module name from '%s'.", resolvedPath.c_str());
        return SdrShaderNodeUniquePtr();
    }

    // All authored spellings use the same SDK-identity selector. Build only
    // the requested node, with the identifier Sdr assigned to this request.
    if (ctx.getModuleLoader().loadModule(qualifiedName.c_str()) < 0) {
        TF_WARN("MdlSdrIntegration::GetSdrFromDiscoveryResult: load_module failed for '%s'.",
                qualifiedName.c_str());
        return SdrShaderNodeUniquePtr();
    }
    if (TfStringStartsWith(discoveryRes.subIdentifier.GetString(), "T[]")) {
        return _CreateArrayConstructor(ctx, qualifiedName, assetPath, discoveryRes);
    }
    InspectionTransaction inspection(ctx);
    if (!inspection.get()) {
        return SdrShaderNodeUniquePtr();
    }
    MdlModule module(qualifiedName, assetPath, inspection.get());
    SdrShaderNodeUniquePtr node = module.GetSdrFromDiscoveryResult(discoveryRes);
    if (node && !node->IsValid()) {
        node.reset(); // Preserve an empty result for unsuccessful asset lookup.
    }
    return node;
}

// Public free-function entry point declared in addModuleToRegistry.h.
// Thin forwarder to the internal singleton: lets external consumers reach
// the only externally-useful method without including the full
// mdlSdrIntegration.h (which pulls in <mi/mdl_sdk.h> + mdlModule.h).
std::vector<SdrShaderNodeConstPtr>
UsdMdlAddModuleToRegistry(const SdfAssetPath& assetPath)
{
    return MdlSdrIntegration::AddModuleToRegistry(assetPath);
}

std::vector<SdrShaderNodeConstPtr>
UsdMdlAddShaderDefinitionsToRegistry(
    const SdfAssetPath& definitionsAsset, const SdfAssetPath& implementationAsset)
{
    return MdlSdrIntegration::AddShaderDefinitionsToRegistry(definitionsAsset, implementationAsset);
}

PXR_NAMESPACE_CLOSE_SCOPE
