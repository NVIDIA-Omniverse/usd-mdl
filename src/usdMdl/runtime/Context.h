//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "defines.h"
#include "../api.h"

#include <mi/mdl_sdk.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

MDL_RUNTIME_NAMESPACE_OPEN

class ModuleLoader;

/// Singleton-style context for the MDL SDK and its global database scope.
/// By default the context loads and owns libmdl_sdk/INeuray. A host may
/// instead inject an existing INeuray instance; injected instances are
/// retained while in use but remain owned by the host.
/// Initialization and shutdown are lifecycle operations: callers must
/// externally serialize them with all users of handles returned by Context.
class Context
{
public:
    /// Returns the singleton Context instance (created on first call).
    USDMDL_API
    static Context& instance();

    /// \param mdlSdkDllPath Path to libmdl_sdk (see PluginLoader::loadNeuray), or nullptr for default name.
    /// \param searchPaths MDL and resource search paths (added before system/user paths).
    /// \return false on failure (TF diagnostics).
    USDMDL_API
    bool initialize(const char* mdlSdkDllPath, const std::vector<std::string>& searchPaths);

    /// Attach to a host-owned INeuray instance in STARTED state. The host is
    /// responsible for SDK startup and image-plugin configuration. shutdown()
    /// releases usdMdl's handles and resolver without calling INeuray::shutdown().
    ///
    /// Returns false for nullptr, an unsupported lifecycle state, or when a
    /// different INeuray instance is already active.
    USDMDL_API
    bool initializeExternal(mi::neuraylib::INeuray* neuray,
                            const std::vector<std::string>& searchPaths);

    /// Load libmdl_sdk by name using the platform loader's search rules on first use.
    USDMDL_API
    bool ensureInitialized();

    /// Releases all handles and uninstalls the Ar entity resolver. Also shuts
    /// down the MDL SDK and unloads its DSO when this context created it. A
    /// host-injected INeuray instance is never shut down or unloaded here.
    USDMDL_API
    void shutdown();

    /// True if the MDL SDK has been successfully initialized.
    bool isInitialized() const { return m_initialized; }

    /// Opens a new database transaction on the global scope. Caller closes it;
    /// read-only inspection uses abort after releasing accessed SDK handles.
    /// Returns an empty handle if the context is not initialized.
    /// Internal: only invoked from inside the usdMdl library.
    mi::base::Handle<mi::neuraylib::ITransaction> createTransaction();

    /// Returns the shared MDL module loader.
    USDMDL_API
    ModuleLoader& getModuleLoader();

    /// Interface accessors below borrow Context's owning handles. Bind a const
    /// reference for immediate use, or copy the handle to retain a reference.
    /// Empty before initialization and after shutdown. Release any copies and
    /// finish all uses before shutdown; handles do not keep the SDK library loaded.
    ///@{

    /// The top-level INeuray interface.
    const mi::base::Handle<mi::neuraylib::INeuray>& getNeuray() const { return m_neuray; }

    /// Factory for creating MDL types, values, and expressions.
    const mi::base::Handle<mi::neuraylib::IMdl_factory>& getMdlFactory() const { return m_mdlFactory; }

    /// MDL import/export API for module serialization.
    const mi::base::Handle<mi::neuraylib::IMdl_impexp_api>& getMdlImpExpApi() const { return m_mdlImpExpApi; }

    /// Ar-backed resolver installed in the MDL SDK configuration.
    const mi::base::Handle<mi::neuraylib::IMdl_entity_resolver>& getEntityResolver() const { return m_entityResolver; }

    /// Discovery API for finding installed MDL packages and modules.
    const mi::base::Handle<mi::neuraylib::IMdl_discovery_api>& getMdlDiscoveryApi() const { return m_discoveryApi; }

    /// The global database scope used for all transactions.
    const mi::base::Handle<mi::neuraylib::IScope>& getScope() const { return m_scope; }

    ///@}

private:
    Context();
    ~Context();

    /// Non-copyable.
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    /// Pre: mutex held and not yet initialized. Takes ownership of the
    /// factory-returned INeuray reference when ownsNeuray is true; duplicates
    /// the caller-owned reference otherwise.
    bool initializeLocked(mi::neuraylib::INeuray* neuray,
                          bool ownsNeuray,
                          const std::vector<std::string>& searchPaths);

    /// Release partially or fully initialized state. Pre: mutex held.
    void resetLocked(bool shutdownOwnedNeuray);

    std::mutex m_mutex;
    bool m_initialized = false;
    bool m_ownsNeuray = false;

    mi::base::Handle<mi::neuraylib::INeuray> m_neuray;
    mi::base::Handle<mi::neuraylib::IMdl_factory> m_mdlFactory;
    mi::base::Handle<mi::neuraylib::IDatabase> m_database;
    mi::base::Handle<mi::neuraylib::IMdl_impexp_api> m_mdlImpExpApi;
    mi::base::Handle<mi::neuraylib::IMdl_discovery_api> m_discoveryApi;
    mi::base::Handle<mi::neuraylib::IScope> m_scope;
    /// Ar-only external resolver installed via
    /// IMdl_configuration::set_entity_resolver while we are initialized.
    mi::base::Handle<mi::neuraylib::IMdl_entity_resolver> m_entityResolver;
    std::unique_ptr<ModuleLoader> m_moduleLoader;
};

MDL_RUNTIME_NAMESPACE_CLOSE
