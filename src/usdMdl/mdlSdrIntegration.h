//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "api.h"

#include <mi/mdl_sdk.h>

#include <pxr/base/tf/singleton.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/declare.h>
#include <pxr/usd/sdr/shaderNodeDiscoveryResult.h>

#include "mdlModule.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

/// Internal singleton bridging MDL SDK discovery / loading and OpenUSD's
/// `SdrRegistry`.  Not part of the installed public API: external consumers
/// reach this through the free function `UsdMdlAddModuleToRegistry` declared
/// in addModuleToRegistry.h.  Direct in-repo users are `MdlDiscoveryPlugin`
/// and `MdlParserPlugin` (via Get()) and the testMdlRuntimeShutdown bisection
/// binary.  The Python wrapper goes through `UsdMdlAddModuleToRegistry`, not
/// this class.
///
/// Owns:
///  - the per-discovery-identifier `SdrShaderNodeUniquePtr` cache.  Eager
///    discovery and `AddModuleToRegistry` build one node per exported MDL
///    definition up front via `_PreparseModule`, drop the transient
///    `MdlModule`, and stash the nodes here keyed by the identifier the
///    parser plugin will later receive in its discovery result.  Parse
///    becomes a hash-map lookup with move-out; nodes retain USD data only,
///    while SDK database elements remain available on the shared Neuray.
///  - the last IMdl_discovery_result, independent of configured-path inspection,
///  - the atexit handler that triggers a clean Context::shutdown() before
///    static destruction races SdrRegistry's singletons.
class MdlSdrIntegration
{
public:
    /// Return the process-wide singleton instance.
    USDMDL_API
    static MdlSdrIntegration& Get()
    {
        return TfSingleton<MdlSdrIntegration>::GetInstance();
    }

    /// Ensure the singleton is ready after the runtime Context has been
    /// initialized. This is normally done by the constructor, but the public
    /// UsdMdlInitialize entry point also calls it so a host can inject an
    /// INeuray instance after an earlier automatic initialization attempt
    /// failed.
    bool EnsureInitialized();

    /// Load one MDL module, register its definitions with SdrRegistry, and return the parsed nodes.
    /// Internal: the only caller is the same-TU free function
    /// `UsdMdlAddModuleToRegistry`, which is the actual exported entry point.
    static std::vector<SdrShaderNodeConstPtr> AddModuleToRegistry(const SdfAssetPath& assetPath);

    /// Publish validated standard USD interfaces backed by MDL definitions.
    static std::vector<SdrShaderNodeConstPtr> AddShaderDefinitionsToRegistry(
        const SdfAssetPath& definitionsAsset, const SdfAssetPath& implementationAsset);

    /// Prepare native SDK nodes without accessing SdrRegistry. Internal USD
    /// discovery uses these owned nodes while the registry is being constructed.
    static std::vector<SdrShaderNodeUniquePtr> PrepareNativeModuleNodes(const SdfAssetPath& assetPath);

    /// Prepare a concrete array constructor without accessing SdrRegistry.
    /// The source asset identifies the element type's defining module. Loading
    /// completes before inspection, and the returned node owns USD data only.
    static SdrShaderNodeUniquePtr PrepareArrayConstructorNode(
        const SdfAssetPath& assetPath, const TfToken& subIdentifier);

    /// Transaction-owned concrete types for the SDK's generic constructor.
    /// Declare after the inspection owner and release before aborting its view.
    struct ArrayConstructorTypes {
        mi::base::Handle<const mi::neuraylib::IType_list> parameters;
        mi::base::Handle<const mi::neuraylib::IType_array> returnType;
        std::string sdkName;
    };

    /// Resolve within a caller-owned inspection of an already loaded module.
    /// Serializes SDK specialization with native node preparation. Never loads
    /// modules, opens transactions, or accesses SdrRegistry.
    static bool ResolveArrayConstructorTypes(
        mi::neuraylib::ITransaction* transaction,
        const std::string& qualifiedModuleName, const TfToken& subIdentifier,
        ArrayConstructorTypes& types);

    /// Prepare only the shipped standard USD interface asset. Independent of
    /// full native catalog discovery; never accesses SdrRegistry.
    SdrShaderNodeDiscoveryResultVec GetUsdPreviewSurfaceDiscoveryResults(const SdfAssetPath& definitionsAsset);

    /// Enumerate definitions in discovered modules, with a sub-identifier per result.
    USDMDL_API
    SdrShaderNodeDiscoveryResultVec GetDiscoveryResults();

    /// Snapshot configured SDK paths in lookup order (forward slashes, trailing
    /// slash), without scanning or loading modules. Called by MdlDiscoveryPlugin
    /// only after initialization; the snapshot remains valid until shutdown.
    const SdrStringVec& GetSearchPaths() const;

    /// Takes a discovery result and generates a new SdrShaderNode (name, source type, family must match).
    USDMDL_API
    SdrShaderNodeUniquePtr GetSdrFromDiscoveryResult(const SdrShaderNodeDiscoveryResult& discoveryRes);

    /// Release all MDL SDK handles and shut down usdMdl::runtime::Context. Safe to call
    /// multiple times.  Called automatically via atexit (before DLL detach)
    /// and from the destructor.
    USDMDL_API
    void Shutdown();

    /// Calls Shutdown() if not already shut down.  TfSingleton invokes
    /// this destructor from the same DLL so no cross-DLL export is
    /// required.
    ~MdlSdrIntegration();

    /// Non-copyable.
    MdlSdrIntegration(const MdlSdrIntegration&) = delete;

    /// Non-copyable.
    MdlSdrIntegration& operator=(const MdlSdrIntegration&) = delete;

private:
    MdlSdrIntegration();

    friend class TfSingleton<MdlSdrIntegration>;

    /// Set in ctor via usdMdl::runtime::Context::ensureInitialized(); if false, public methods return defaults.
    bool _mdlSdkReady = false;

    /// Guards registration of the process-exit shutdown hook when a failed
    /// first initialization is later recovered with a host-provided INeuray.
    bool _shutdownRegistered = false;

    /// Cached IMdl_discovery_result; GetDiscoveryResults walks its graph.
    mutable mi::base::Handle<const mi::neuraylib::IMdl_discovery_result> _lastDiscovery;

    /// Filled once by GetSearchPaths and then kept immutable because Sdr's
    /// discovery-plugin API returns it by reference.
    mutable SdrStringVec _searchPaths;

    /// Separate from graph discovery: reading configured paths never scans.
    mutable bool _searchPathsInitialized = false;

    /// True after the discovery cache has been published (including an empty
    /// result). Reset only during the externally-quiescent Shutdown lifecycle.
    mutable bool _discoveryInitialized = false;

    /// Protects path/graph snapshots and their initialization flags.
    /// GetDiscoveryResults pins the discovery graph in a local SDK handle and
    /// releases this mutex before walking it, so graph traversal and
    /// `_PreparseModule` never nest this lock with `_parsedNodesMutex`.
    mutable std::mutex _discoveryMutex;

    /// One pre-built `SdrShaderNode` per discovery identifier.  Populated
    /// eagerly by `_PreparseModule` (called from discovery and from
    /// `AddModuleToRegistry`) and consumed -- moved out -- by
    /// `GetSdrFromDiscoveryResult` when Sdr asks the parser plugin to parse
    /// each result.  Once a node has been moved out, the entry is erased
    /// so the SDK-derived strings backing it can be freed.
    std::unordered_map<SdrIdentifier,
                       SdrShaderNodeUniquePtr,
                       SdrIdentifierHashFunctor> _parsedNodes;

    /// Serializes all access to `_parsedNodes` and to the SDK-side parse
    /// in `_PreparseModule`.  `SdrRegistry` parallelizes parser-plugin
    /// invocations through TBB (see `SdrRegistry::GetShaderNodesByFunction`
    /// -> `_FindOrParseNodeInCache`), so `GetSdrFromDiscoveryResult` is
    /// reentered concurrently from multiple worker threads. Callers that
    /// preparse or select a definition must hold this lock; `_PreparseModule`
    /// itself does not acquire it.  We use `std::mutex` rather than
    /// `recursive_mutex` because `_PreparseModule`'s calls into the SDK
    /// never re-enter `MdlSdrIntegration`, and `AddModuleToRegistry`
    /// releases the lock before invoking `SdrRegistry::AddDiscoveryResult`
    /// so the synchronous parser callback can acquire the lock fresh on
    /// the same thread.
    mutable std::mutex _parsedNodesMutex;

    /// Automatic-interface preparation is serialized separately from explicit
    /// publication. It may take _parsedNodesMutex, but never accesses SdrRegistry
    /// or _shaderDefinitionsMutex, including during registry construction.
    std::mutex _previewSurfaceMutex;
    bool _previewSurfaceInitialized = false;
    SdrShaderNodeDiscoveryResultVec _previewSurfaceDiscoveryResults;

    /// Immutable USD-only blueprints guarded by _parsedNodesMutex. Parallel
    /// parser callbacks can recreate a node after another callback consumed its
    /// pending node, without reopening a layer or recursively loading modules.
    std::unordered_map<SdrIdentifier,
                       SdrShaderNodeUniquePtr,
                       SdrIdentifierHashFunctor> _previewSurfaceNodes;

    // Serialize explicit interface registration through validation and publication.
    // Parser callbacks never take this mutex; SDK parsing keeps its existing lock.
    std::mutex _shaderDefinitionsMutex;
};

USDMDL_API_TEMPLATE_CLASS(TfSingleton<MdlSdrIntegration>);

PXR_NAMESPACE_CLOSE_SCOPE
