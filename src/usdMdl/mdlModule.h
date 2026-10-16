//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "api.h"
#include "debugContext.h"
#include "function.h"

#include <pxr/base/tf/token.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/shaderNodeDiscoveryResult.h>

#include <string>
#include <unordered_map>

PXR_NAMESPACE_OPEN_SCOPE

/// Wraps a loaded MDL SDK `IModule` and the map of `Function`s it exports.
/// Functions are keyed by MDL SDK DB element name (unique per definition). Do not key only by
/// `subIdentifier` (TfToken): overloads can share a token and `unordered_map::emplace` would drop
/// later definitions.
class MdlModule
{
public:
    /// Inspect an already loaded module using the caller's transaction.
    /// The transaction is borrowed and must outlive this module and its converters.
    /// This class neither loads modules nor closes the inspection transaction.
    /// Uses the loaded SDK source filename, or an optional proxy for an embedded module.
    USDMDL_API
    MdlModule(const std::string& qualifiedModuleName, const SdfAssetPath& assetPath,
              mi::neuraylib::ITransaction* transaction);

    // Functions and diagnostic breadcrumbs point back to this module.
    MdlModule(const MdlModule&) = delete;
    MdlModule& operator=(const MdlModule&) = delete;
    MdlModule(MdlModule&&) = delete;
    MdlModule& operator=(MdlModule&&) = delete;

    /// Whether the module loaded and its functions were enumerated successfully.
    bool IsValid() const { return _isValid; }

    /// Database name of the loaded module in the shared Neuray instance.
    const std::string& GetDbName() const { return _moduleDbName; }

    /// Whether the SDK module has no backing source file, even if a proxy exists.
    bool IsEmbedded() const { return _isEmbedded; }

    /// Ar identifier of the source or embedded-module proxy; empty if neither exists.
    const std::string& GetResolvedPath() const { return _resolvedPath; }

    /// The debug context breadcrumb rooted at this module.
    const DebugContext& GetDebugCtx() const { return _debugCtx; }

    /// Module-level metadata dictionary (annotations, display name); built during construction.
    const VtDictionary& GetMetadata() const;

    /// Build an SdrShaderNode for the function matching the given discovery result.
    USDMDL_API
    SdrShaderNodeUniquePtr GetSdrFromDiscoveryResult(const SdrShaderNodeDiscoveryResult& discoveryRes) const;

    /// Build a builtin constructor anchored to this element-type module.
    SdrShaderNodeUniquePtr GetArrayConstructorNode(
        const SdrShaderNodeDiscoveryResult& discoveryRes,
        const mi::neuraylib::IType_list* inputs,
        const mi::neuraylib::IType* output,
        const std::string& sdkName);

    /// Recover both the MDL-logical and physically resolved resource paths.
    ///
    /// The asset path is the normalized absolute MDL path used for authoring.
    /// For resources backed by a single file, the resolved path retains Ar's
    /// identifier as returned by the MDL SDK, ready for Ar::OpenAsset(). Animated
    /// and UV-tiled textures retain their markers in an absolute resolved mask.
    /// Uses the borrowed inspection transaction, including for array elements.
    SdfAssetPath ResolveResourceAssetPath(
        const mi::neuraylib::IValue_resource* val) const;

private:
    void _LoadFunction(
        const char* funcDatabaseName,
        mi::neuraylib::ITransaction* t,
        const mi::neuraylib::IModule* module);

private:
    mi::neuraylib::ITransaction* const _transaction;
    bool _isValid = false;
    bool _isEmbedded = false;
    std::string _qualifiedModuleName;
    std::string _resolvedPath;

    std::string _moduleDbName;
    TfToken _moduleUsdIdentifier;

    std::unordered_map<std::string, Function> _functionsByDbName;
    DebugContext _debugCtx;

    VtDictionary _metadataDict;
};

PXR_NAMESPACE_CLOSE_SCOPE
