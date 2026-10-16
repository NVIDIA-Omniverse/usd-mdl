//
// SPDX-License-Identifier: Apache-2.0
//

// Lifecycle coverage for the public usdMdl initialization API. CTest runs
// this executable in separate processes for owned and host-supplied runtimes:
//   owned              - no INeuray supplied; usdMdl creates and shuts it down
//   external-unstarted - rejection before host startup, then successful retry
//   external-started   - a host-owned STARTED instance is supplied

#include "Context.h"
#include "EntityResolver.h"
#include "PluginLoader.h"
#include "addModuleToRegistry.h"
#include "initialize.h"
#include "tokens.h"

#include <mi/mdl_sdk.h>

#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/base/tf/diagnostic.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "test_exit.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                       \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::fprintf(stderr,                                                \
                         "[testMdlNeurayOwnership] FAIL: %s (%s:%d)\n",       \
                         msg, __FILE__, __LINE__);                              \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

// Query counts without leaving an extra reference. These lifecycle tests are
// single-threaded, so changes identify ownership transfers by the accessors.
template<class Interface>
mi::Uint32 ReferenceCount(Interface* value)
{
    value->retain();
    return value->release();
}

template<class Interface>
void CheckPublicReference(const mi::base::Handle<Interface>& stored, Interface* (*getter)())
{
    TF_AXIOM(stored);
    const auto baseline = ReferenceCount(stored.get());
    {
        mi::base::Handle<Interface> first(getter());
        TF_AXIOM(first.get() == stored.get());
        TF_AXIOM(ReferenceCount(stored.get()) == baseline + 1);
        mi::base::Handle<Interface> second(getter());
        TF_AXIOM(second.get() == first.get());
        TF_AXIOM(ReferenceCount(stored.get()) == baseline + 2);
        first.reset();
        TF_AXIOM(ReferenceCount(second.get()) == baseline + 1);
    }
    TF_AXIOM(ReferenceCount(stored.get()) == baseline);
}

void CheckAccessorOwnership(usdMdl::runtime::Context& context)
{
    CheckPublicReference(context.getNeuray(), UsdMdlGetNeuray);
    CheckPublicReference(context.getEntityResolver(), UsdMdlGetEntityResolver);
    const auto& impexp = context.getMdlImpExpApi();
    const auto baseline = ReferenceCount(impexp.get());
    {
        mi::base::Handle<mi::neuraylib::IMdl_entity_resolver> resolver(
            usdMdl::runtime::CreateArEntityResolver(impexp.get(), {}));
        TF_AXIOM(ReferenceCount(resolver.get()) == 1);
        TF_AXIOM(ReferenceCount(impexp.get()) == baseline + 1);
    }
    TF_AXIOM(ReferenceCount(impexp.get()) == baseline);
}

bool
LoadFixtureModule(const std::string& mdlPath)
{
    const std::filesystem::path source =
        std::filesystem::path(mdlPath) / "node_metadata/first/definitions.mdl";
    const auto nodes = UsdMdlAddModuleToRegistry(
        SdfAssetPath(source.generic_string(), source.generic_string()));
    if (nodes.size() != 3) {
        return false;
    }
    // Consume the public metadata with an independent SDK transaction. A
    // correctly formatted string is insufficient if it names the wrong object.
    mi::base::Handle<mi::neuraylib::INeuray> neuray(UsdMdlGetNeuray());
    if (!neuray) {
        return false;
    }
    mi::base::Handle<mi::neuraylib::IDatabase> database(
        neuray->get_api_component<mi::neuraylib::IDatabase>());
    mi::base::Handle<mi::neuraylib::IScope> scope(database->get_global_scope());
    mi::base::Handle<mi::neuraylib::ITransaction> transaction(scope->create_transaction());
    if (!transaction) {
        return false;
    }
    bool valid = true;
    for (const auto* node : nodes) {
        const auto& metadata = node->GetMetadata();
        const auto moduleName = metadata.find(UsdMdlNodeMetadataTokens->MdlDbModuleName);
        const auto definitionName = metadata.find(UsdMdlNodeMetadataTokens->MdlDbDefinitionName);
        const auto signature = metadata.find(UsdMdlNodeMetadataTokens->MdlNameWithSignature);
        if (moduleName == metadata.end() || definitionName == metadata.end()
            || signature == metadata.end()) {
            valid = false;
            continue;
        }
        mi::base::Handle<const mi::neuraylib::IModule> module(
            transaction->access<mi::neuraylib::IModule>(moduleName->second.c_str()));
        mi::base::Handle<const mi::neuraylib::IFunction_definition> definition(
            transaction->access<mi::neuraylib::IFunction_definition>(definitionName->second.c_str()));
        valid &= module && definition
            && std::string(module->get_mdl_name()) == "::node_metadata::first::definitions"
            && std::string(definition->get_mdl_name())
                == "::node_metadata::first::definitions::" + signature->second
            && std::string(definition->get_module()) == moduleName->second;
    }
    return transaction->commit() == 0 && valid;
}

void
RunOwned(const std::string& mdlPath, const char* sdkPath = nullptr,
         const char* expectedPluginDirectory = nullptr)
{
    const bool initialized = sdkPath
        ? UsdMdlInitialize(sdkPath) : UsdMdlInitialize();
    EXPECT(initialized, "initialization must create a plugin-owned INeuray instance");
    if (!initialized) {
        return;
    }

    usdMdl::runtime::Context& context = usdMdl::runtime::Context::instance();
    EXPECT(context.isInitialized(), "owned Context must be initialized");
    CheckAccessorOwnership(context);
    mi::base::Handle<mi::neuraylib::INeuray> neuray(UsdMdlGetNeuray());
    EXPECT(neuray, "public getter must expose owned INeuray");
    if (neuray) {
        EXPECT(neuray->get_status() ==
                   mi::neuraylib::INeuray::STARTED,
               "owned INeuray must be started");
    }
    EXPECT(LoadFixtureModule(mdlPath),
           "owned INeuray must load and register an MDL module");

    if (expectedPluginDirectory) {
        mi::base::Handle<mi::neuraylib::IPlugin_configuration> plugins(
            neuray->get_api_component<mi::neuraylib::IPlugin_configuration>());
        bool foundOiio = false;
        bool foundDds = false;
        for (mi::Size i = 0; plugins && i < plugins->get_plugin_length(); ++i) {
            mi::base::Handle<mi::base::IPlugin_descriptor> descriptor(
                plugins->get_plugin_descriptor(i));
            const char* path = descriptor ? descriptor->get_plugin_library_path() : nullptr;
            if (!path) {
                continue;
            }
            const std::filesystem::path library(path);
            const auto name = library.stem().string();
            if (name == "nv_openimageio" || name == "dds") {
                EXPECT(expectedPluginDirectory[0]
                           && std::filesystem::equivalent(library.parent_path(), expectedPluginDirectory),
                       "image plugins must come from the relocated SDK directory");
                foundOiio |= name == "nv_openimageio";
                foundDds |= name == "dds";
            }
        }
        if (expectedPluginDirectory[0]) {
            EXPECT(foundOiio && foundDds, "both relocated image plugins must be loaded");
        } else {
            EXPECT(!foundOiio && !foundDds,
                   "missing adjacent image plugins must not load from the library search path");
        }
    }

    neuray.reset();
    UsdMdlShutdown();
    EXPECT(!context.isInitialized(),
           "UsdMdlShutdown() must tear down an owned Context");
    EXPECT(mi::base::Handle<mi::neuraylib::INeuray>(UsdMdlGetNeuray()).get() == nullptr,
           "public getter must return null after owned runtime shutdown");
    EXPECT(!mi::base::Handle<mi::neuraylib::IMdl_entity_resolver>(UsdMdlGetEntityResolver()),
           "resolver getter must also return null while uninitialized");

    // Idempotence and the no-initialization-on-shutdown guard.
    UsdMdlShutdown();
    EXPECT(!context.isInitialized(),
           "a repeated UsdMdlShutdown() must remain a no-op");
}

void
RunExternal(const std::string& mdlPath, bool testUnstartedRejection,
            const char* sdkPath = nullptr)
{
    const std::string dll = sdkPath ? sdkPath
        : usdMdl::runtime::PluginLoader::sdkDllPathUnderPrefix(MDL_SDK_INSTALL_PREFIX);
    mi::base::Handle<mi::neuraylib::INeuray> hostNeuray(
        usdMdl::runtime::PluginLoader::loadNeuray(dll.c_str()));
    EXPECT(hostNeuray, "host must be able to create an INeuray instance");
    if (!hostNeuray) {
        return;
    }

    EXPECT(hostNeuray->get_status() == mi::neuraylib::INeuray::PRE_STARTING,
           "new host INeuray must be PRE_STARTING");

    mi::Size hostPluginCount = 0;
    {
        mi::base::Handle<mi::neuraylib::IMdl_configuration> configuration(
            hostNeuray->get_api_component<mi::neuraylib::IMdl_configuration>());
        EXPECT(configuration, "external INeuray must expose IMdl_configuration");
        mi::base::Handle<mi::neuraylib::IPlugin_configuration> plugins(
            hostNeuray->get_api_component<mi::neuraylib::IPlugin_configuration>());
        EXPECT(plugins, "external INeuray must expose IPlugin_configuration");
        if (testUnstartedRejection && configuration && plugins) {
            const auto mdlPaths = configuration->get_mdl_paths_length();
            const auto resourcePaths = configuration->get_resource_paths_length();
            const auto pluginCount = plugins->get_plugin_length();
            EXPECT(!UsdMdlInitialize(hostNeuray.get()),
                   "an unstarted host instance must be rejected");
            EXPECT(mi::base::Handle<mi::neuraylib::INeuray>(UsdMdlGetNeuray()).get() == nullptr,
                   "rejection must leave usdMdl uninitialized");
            EXPECT(!mi::base::Handle<mi::neuraylib::IMdl_entity_resolver>(UsdMdlGetEntityResolver()),
                   "resolver getter must also return null while uninitialized");
            EXPECT(hostNeuray->get_status() == mi::neuraylib::INeuray::PRE_STARTING,
                   "rejection must not start or shut down the host instance");
            EXPECT(configuration->get_mdl_paths_length() == mdlPaths
                       && configuration->get_resource_paths_length() == resourcePaths,
                   "rejection must not add environment search paths to the host");
            EXPECT(plugins->get_plugin_length() == pluginCount,
                   "rejection must not load image plugins into the host");
            UsdMdlShutdown();
        }
        // The host owns startup configuration, even after a rejected attachment.
        if (sdkPath && plugins) {
            // The relocation test supplies plugins on the library search path,
            // away from the SDK. Hosts remain free to choose that layout.
            EXPECT(plugins->load_plugin_library("nv_openimageio" MI_BASE_DLL_FILE_EXT) == 0,
                   "host must be able to load nv_openimageio by name");
            EXPECT(plugins->load_plugin_library("dds" MI_BASE_DLL_FILE_EXT) == 0,
                   "host must be able to load dds by name");
        }
        if (configuration) {
            EXPECT(configuration->add_mdl_path(mdlPath.c_str()) == 0,
                   "host must be able to add the fixture MDL path");
            EXPECT(configuration->add_resource_path(mdlPath.c_str()) == 0,
                   "host must be able to add the fixture resource path");
        }
        configuration.reset();
        EXPECT(hostNeuray->start() == 0,
               "host must be able to start INeuray before injection");
        if (plugins) {
            hostPluginCount = plugins->get_plugin_length();
        }
    }

    EXPECT(UsdMdlInitialize(hostNeuray.get()),
           "UsdMdlInitialize(external) must accept the host INeuray instance");

    usdMdl::runtime::Context& context = usdMdl::runtime::Context::instance();
    EXPECT(context.isInitialized(), "external Context must be initialized");
    CheckAccessorOwnership(context);
    EXPECT(mi::base::Handle<mi::neuraylib::INeuray>(UsdMdlGetNeuray()).get() == hostNeuray.get(),
           "public getter must return the exact injected INeuray instance");
    EXPECT(hostNeuray->get_status() == mi::neuraylib::INeuray::STARTED,
           "attachment must preserve the host's STARTED state");
    {
        mi::base::Handle<mi::neuraylib::IPlugin_configuration> plugins(
            hostNeuray->get_api_component<mi::neuraylib::IPlugin_configuration>());
        EXPECT(plugins && plugins->get_plugin_length() == hostPluginCount,
               "attachment must not load plugins into a host-owned instance");
    }
    EXPECT(UsdMdlInitialize(hostNeuray.get()),
           "reinitializing with the same external instance must be harmless");
    EXPECT(LoadFixtureModule(mdlPath),
           "external INeuray must load and register an MDL module");

    UsdMdlShutdown();
    EXPECT(!context.isInitialized(),
           "UsdMdlShutdown() must detach Context from an external instance");
    EXPECT(mi::base::Handle<mi::neuraylib::INeuray>(UsdMdlGetNeuray()).get() == nullptr,
           "public getter must return null after detaching an external runtime");
    EXPECT(!mi::base::Handle<mi::neuraylib::IMdl_entity_resolver>(UsdMdlGetEntityResolver()),
           "resolver getter must also return null while uninitialized");
    EXPECT(hostNeuray->get_status() == mi::neuraylib::INeuray::STARTED,
           "UsdMdlShutdown() must not shut down a host-owned INeuray");

    EXPECT(hostNeuray->shutdown() == 0,
           "host must retain responsibility for external INeuray shutdown");
    EXPECT(!UsdMdlInitialize(hostNeuray.get()),
           "a shut-down host instance must also be rejected");
    EXPECT(mi::base::Handle<mi::neuraylib::INeuray>(UsdMdlGetNeuray()).get() == nullptr,
           "rejected shut-down instances must not become active");
    EXPECT(!mi::base::Handle<mi::neuraylib::IMdl_entity_resolver>(UsdMdlGetEntityResolver()),
           "resolver getter must also return null while uninitialized");
    hostNeuray.reset();
}

} // namespace

int
main(int argc, char* argv[])
{
    if (argc < 3 || argc > 5) {
        std::fprintf(stderr,
                     "usage: testMdlNeurayOwnership "
                     "<mode> <mdl-path> [sdk-library] [expected-plugin-directory]\n");
        return 2;
    }

    const std::string mode = argv[1];
    const std::string mdlPath =
        std::filesystem::absolute(argv[2]).lexically_normal().generic_string();

    EXPECT(mi::base::Handle<mi::neuraylib::INeuray>(UsdMdlGetNeuray()).get() == nullptr,
           "public getter must not initialize an unused runtime");
    EXPECT(!mi::base::Handle<mi::neuraylib::IMdl_entity_resolver>(UsdMdlGetEntityResolver()),
           "resolver getter must also return null while uninitialized");

    if (mode == "owned") {
        RunOwned(mdlPath, argc >= 4 ? argv[3] : nullptr, argc == 5 ? argv[4] : nullptr);
    } else if (mode == "owned-no-image-plugins" && argc == 4) {
        RunOwned(mdlPath, argv[3], "");
    } else if (mode == "owned-empty") {
        RunOwned(mdlPath, "");
    } else if (mode == "owned-unavailable") {
        EXPECT(!UsdMdlInitialize(),
               "default loading must fail when the SDK is absent from the library search path");
        EXPECT(mi::base::Handle<mi::neuraylib::INeuray>(UsdMdlGetNeuray()).get() == nullptr,
               "a missing default SDK must leave the runtime uninitialized");
        EXPECT(!mi::base::Handle<mi::neuraylib::IMdl_entity_resolver>(UsdMdlGetEntityResolver()),
               "resolver getter must also return null while uninitialized");
        UsdMdlShutdown();
    } else if (mode == "owned-invalid") {
        const auto missing = std::filesystem::absolute("missing/libmdl_sdk" MI_BASE_DLL_FILE_EXT);
        EXPECT(!UsdMdlInitialize(missing.string().c_str()),
               "an invalid explicit SDK path must fail even when a default SDK is available");
        EXPECT(mi::base::Handle<mi::neuraylib::INeuray>(UsdMdlGetNeuray()).get() == nullptr,
               "failed SDK loading must leave the runtime uninitialized");
        EXPECT(!mi::base::Handle<mi::neuraylib::IMdl_entity_resolver>(UsdMdlGetEntityResolver()),
               "resolver getter must also return null while uninitialized");
        RunOwned(mdlPath);
    } else if (mode == "external-unstarted") {
        RunExternal(mdlPath, /*testUnstartedRejection=*/true);
    } else if (mode == "external-started") {
        RunExternal(mdlPath, /*testUnstartedRejection=*/false, argc >= 4 ? argv[3] : nullptr);
    } else {
        std::fprintf(stderr, "unknown mode: %s\n", mode.c_str());
        return 2;
    }

    if (g_failures == 0) {
        std::fprintf(stderr, "[testMdlNeurayOwnership] OK (%s)\n", mode.c_str());
    } else {
        std::fprintf(stderr, "[testMdlNeurayOwnership] %d FAILURE(S) (%s)\n",
                     g_failures, mode.c_str());
    }
    std::fflush(stderr);
    mdl_test::FlushAndExit(g_failures == 0 ? 0 : 1);
}
