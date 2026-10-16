//
// SPDX-License-Identifier: Apache-2.0
//

#include "initialize.h"

#include "mdlSdrIntegration.h"
#include "runtime/Context.h"
#include "runtime/EnvSettings.h"

PXR_NAMESPACE_OPEN_SCOPE

bool
UsdMdlInitialize(mi::neuraylib::INeuray* neuray)
{
    if (!neuray) {
        return UsdMdlInitialize();
    }
    usdMdl::runtime::Context& context = usdMdl::runtime::Context::instance();
    if (!context.initializeExternal(neuray, usdMdl::runtime::GetUsdMdlSearchPathsFromEnv())) {
        return false;
    }

    // Initialize Context before touching the singleton so it uses the host's
    // instance instead of creating its own runtime in its constructor.
    return MdlSdrIntegration::Get().EnsureInitialized();
}

bool
UsdMdlInitialize(const char* mdlSdkLibraryPath)
{
    usdMdl::runtime::Context& context = usdMdl::runtime::Context::instance();
    if (!context.initialize(mdlSdkLibraryPath, usdMdl::runtime::GetUsdMdlSearchPathsFromEnv())) {
        return false;
    }

    return MdlSdrIntegration::Get().EnsureInitialized();
}

bool
UsdMdlInitialize()
{
    return UsdMdlInitialize(static_cast<const char*>(nullptr));
}

mi::neuraylib::INeuray*
UsdMdlGetNeuray()
{
    usdMdl::runtime::Context& context = usdMdl::runtime::Context::instance();
    if (!context.isInitialized()) {
        return nullptr;
    }
    const auto& handle = context.getNeuray();
    if (handle) {
        handle->retain();
    }
    return handle.get();
}

mi::neuraylib::IMdl_entity_resolver*
UsdMdlGetEntityResolver()
{
    usdMdl::runtime::Context& context = usdMdl::runtime::Context::instance();
    if (!context.isInitialized()) {
        return nullptr;
    }
    const auto& handle = context.getEntityResolver();
    if (handle) {
        handle->retain();
    }
    return handle.get();
}

void
UsdMdlShutdown()
{
    // Avoid instantiating MdlSdrIntegration merely to perform a no-op shutdown;
    // its constructor would otherwise auto-initialize a self-owned SDK.
    if (!usdMdl::runtime::Context::instance().isInitialized()) {
        return;
    }
    MdlSdrIntegration::Get().Shutdown();
}

PXR_NAMESPACE_CLOSE_SCOPE
