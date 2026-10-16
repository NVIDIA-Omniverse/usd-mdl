//
// SPDX-License-Identifier: Apache-2.0
//

#include "Context.h"

#include "EntityResolver.h"
#include "EnvSettings.h"
#include "ModuleLoader.h"
#include "PluginLoader.h"

#include <mi/mdl_sdk.h>

#include "pxr/base/arch/error.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/envSetting.h"
#include "pxr/base/tf/getenv.h"

#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

bool
_HasNonEmptyEnv(const char* name)
{
    return name && !TfGetenv(name).empty();
}

void
_AddSearchAndResource(mi::neuraylib::IMdl_configuration* cfg, const std::string& p)
{
    if (p.empty() || !cfg) {
        return;
    }
    if (cfg->add_mdl_path(p.c_str()) != 0
        || cfg->add_resource_path(p.c_str()) != 0) {
        TF_WARN("Failed to add MDL search/resource path: %s", p.c_str());
    }
}

std::vector<std::string>
_SnapshotMdlPaths(const mi::neuraylib::IMdl_configuration* cfg)
{
    std::vector<std::string> result;
    if (!cfg) {
        return result;
    }
    result.reserve(cfg->get_mdl_paths_length());
    for (mi::Size i = 0; i < cfg->get_mdl_paths_length(); ++i) {
        mi::base::Handle<const mi::IString> path(cfg->get_mdl_path(i));
        if (path && path->get_c_str() && path->get_c_str()[0]) {
            result.emplace_back(path->get_c_str());
        }
    }
    return result;
}

} // namespace

MDL_RUNTIME_NAMESPACE_OPEN

Context&
Context::instance()
{
    static Context s_instance;
    return s_instance;
}

Context::Context() = default;

Context::~Context()
{
    // Intentionally does NOT call shutdown().  During process exit the
    // destruction order of this function-local static vs. TfSingleton
    // instances (MdlSdrIntegration, SdrRegistry) is undefined.  Calling
    // INeuray::shutdown() while those singletons still hold live
    // mi::base::Handle objects crashes inside the MDL SDK.
    //
    // Instead, MdlSdrIntegration registers a std::atexit handler that
    // releases its handles and calls shutdown() explicitly *before*
    // ExitProcess / static destruction runs.
}

bool
Context::ensureInitialized()
{
    return initialize(nullptr, GetUsdMdlSearchPathsFromEnv());
}

bool
Context::initialize(const char* mdlSdkDllPath, const std::vector<std::string>& searchPaths)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        return true;
    }
    mi::neuraylib::INeuray* const neuray = PluginLoader::loadNeuray(mdlSdkDllPath);
    if (!neuray) {
        TF_WARN("Failed to load MDL SDK (libmdl_sdk).");
        return false;
    }
    return initializeLocked(neuray, /*ownsNeuray=*/true, searchPaths);
}

bool
Context::initializeExternal(mi::neuraylib::INeuray* neuray,
                            const std::vector<std::string>& searchPaths)
{
    if (!neuray) {
        TF_WARN("Context::initializeExternal called with a null INeuray instance.");
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        if (m_neuray.get() == neuray) {
            return true;
        }
        TF_WARN("Context::initializeExternal called with a different INeuray instance "
                "while usdMdl is already initialized.");
        return false;
    }
    return initializeLocked(neuray, /*ownsNeuray=*/false, searchPaths);
}

bool
Context::initializeLocked(mi::neuraylib::INeuray* neuray,
                          bool ownsNeuray,
                          const std::vector<std::string>& searchPaths)
{
    m_ownsNeuray = ownsNeuray;
    if (ownsNeuray) {
        // PluginLoader::loadNeuray() returns an owned interface reference.
        m_neuray = mi::base::Handle<mi::neuraylib::INeuray>(neuray);
    } else {
        // Function parameters are caller-owned by convention. Retain a
        // duplicate reference for as long as usdMdl uses the host instance.
        m_neuray = mi::base::make_handle_dup(neuray);
    }

    const mi::neuraylib::INeuray::Status initialStatus = neuray->get_status();
    const auto expectedStatus = ownsNeuray
        ? mi::neuraylib::INeuray::PRE_STARTING : mi::neuraylib::INeuray::STARTED;
    if (initialStatus != expectedStatus) {
        TF_WARN("%s INeuray has status %u; expected %s.",
                ownsNeuray ? "Plugin-owned" : "Host-owned",
                static_cast<unsigned int>(initialStatus),
                ownsNeuray ? "PRE_STARTING" : "STARTED (the host must start it before attachment)");
        resetLocked(/*shutdownOwnedNeuray=*/false);
        return false;
    }

    mi::base::Handle<mi::neuraylib::IMdl_configuration> mdl_configuration(
        neuray->get_api_component<mi::neuraylib::IMdl_configuration>());
    if (!mdl_configuration) {
        TF_WARN("IMdl_configuration not available.");
        resetLocked(/*shutdownOwnedNeuray=*/false);
        return false;
    }

    for (const std::string& p : searchPaths) {
        _AddSearchAndResource(mdl_configuration.get(), p);
    }

    // Opt-in to the SDK's user/system path discovery.  The legacy
    // `MDL_USER_PATH` / `MDL_SYSTEM_PATH` env vars also force the
    // respective SDK call so existing setups (and the ctest harness,
    // which sets MDL_USER_PATH) keep working without touching the new
    // env knobs.
    if (TfGetEnvSetting(USD_MDL_ADD_USER_PATHS) || _HasNonEmptyEnv("MDL_USER_PATH")) {
        mdl_configuration->add_mdl_user_paths();
    }
    if (TfGetEnvSetting(USD_MDL_ADD_SYSTEM_PATHS) || _HasNonEmptyEnv("MDL_SYSTEM_PATH")) {
        mdl_configuration->add_mdl_system_paths();
    }

    if (ownsNeuray) {
        // SDK startup and image-plugin configuration belong to the instance's
        // owner. Host instances arrive started and must not be reconfigured here.
        PluginLoader::loadImagePlugins(neuray);

        const mi::Sint32 startResult = neuray->start();
        if (startResult != 0) {
            TF_WARN("INeuray::start failed (%d).", static_cast<int>(startResult));
            mdl_configuration.reset();
            resetLocked(/*shutdownOwnedNeuray=*/false);
            return false;
        }
    }

    // Acquire all required components before installing the resolver. The
    // import/export API supplies UDIM/frame-marker substitution.
    m_mdlFactory = neuray->get_api_component<mi::neuraylib::IMdl_factory>();
    m_database = neuray->get_api_component<mi::neuraylib::IDatabase>();
    m_mdlImpExpApi = neuray->get_api_component<mi::neuraylib::IMdl_impexp_api>();
    m_discoveryApi = neuray->get_api_component<mi::neuraylib::IMdl_discovery_api>();

    if (!m_mdlFactory || !m_database || !m_mdlImpExpApi || !m_discoveryApi) {
        TF_WARN("Failed to acquire core MDL API components after start.");
        mdl_configuration.reset();
        resetLocked(/*shutdownOwnedNeuray=*/true);
        return false;
    }

    // Install the Ar-only entity resolver after start and before the first
    // load_module() call. It receives the complete configured path snapshot,
    // including explicitly enabled SDK user/system roots, so ordinary
    // qualified modules are resolved through Ar without an SDK fallback.
    m_entityResolver = mi::base::Handle<mi::neuraylib::IMdl_entity_resolver>(
        CreateArEntityResolver(
            m_mdlImpExpApi.get(), _SnapshotMdlPaths(mdl_configuration.get())));
    if (!m_entityResolver) {
        TF_WARN("Failed to construct Ar-backed MDL entity resolver.");
        mdl_configuration.reset();
        resetLocked(/*shutdownOwnedNeuray=*/true);
        return false;
    }
    mdl_configuration->set_entity_resolver(m_entityResolver.get());

    m_scope = m_database->get_global_scope();
    if (!m_scope) {
        TF_WARN("get_global_scope returned nullptr.");
        mdl_configuration.reset();
        resetLocked(/*shutdownOwnedNeuray=*/true);
        return false;
    }

    m_moduleLoader = std::make_unique<ModuleLoader>(*this);
    m_initialized = true;
    return true;
}

void
Context::resetLocked(bool shutdownOwnedNeuray)
{
    // Uninstall our entity resolver before releasing API components or
    // returning control to an external owner.
    if (m_entityResolver && m_neuray &&
        m_neuray->get_status() == mi::neuraylib::INeuray::STARTED) {
        mi::base::Handle<mi::neuraylib::IMdl_configuration> mdl_configuration(
            m_neuray->get_api_component<mi::neuraylib::IMdl_configuration>());
        if (mdl_configuration) {
            mdl_configuration->set_entity_resolver(nullptr);
        }
    }
    m_entityResolver.reset();

    m_moduleLoader.reset();
    m_scope.reset();
    m_discoveryApi.reset();
    m_mdlImpExpApi.reset();
    m_mdlFactory.reset();
    m_database.reset();

    const bool owned = m_ownsNeuray;
    if (m_neuray && owned && shutdownOwnedNeuray &&
        m_neuray->get_status() == mi::neuraylib::INeuray::STARTED) {
        m_neuray->shutdown();
    }
    m_neuray.reset();

    if (owned) {
        PluginLoader::unloadSdkDll();
    }
    m_ownsNeuray = false;
    m_initialized = false;
}

void
Context::shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
        return;
    }
    resetLocked(/*shutdownOwnedNeuray=*/true);
}

mi::base::Handle<mi::neuraylib::ITransaction>
Context::createTransaction()
{
    if (!m_initialized || !m_scope) {
        return {};
    }
    return mi::base::Handle<mi::neuraylib::ITransaction>(m_scope->create_transaction());
}

ModuleLoader&
Context::getModuleLoader()
{
    ARCH_AXIOM(m_initialized && m_moduleLoader);
    return *m_moduleLoader;
}

MDL_RUNTIME_NAMESPACE_CLOSE
