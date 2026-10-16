//
// SPDX-License-Identifier: Apache-2.0
//

#include "PluginLoader.h"

#include <mi/mdl_sdk.h>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/arch/symbols.h>
#include <pxr/pxr.h>

#include <filesystem>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

#ifdef MI_PLATFORM_WINDOWS
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

MDL_RUNTIME_NAMESPACE_OPEN

std::string
PluginLoader::sdkDllPathUnderPrefix(const char* installPrefix)
{
    if (!installPrefix || !installPrefix[0]) {
        return {};
    }
    std::string p = installPrefix;
    while (!p.empty() && (p.back() == '/' || p.back() == '\\')) {
        p.pop_back();
    }
    // Windows installs DLLs into <prefix>/bin; Linux/macOS install shared
    // libraries into <prefix>/lib.
#ifdef MI_PLATFORM_WINDOWS
    return p + "/bin/libmdl_sdk" + MI_BASE_DLL_FILE_EXT;
#else
    return p + "/lib/libmdl_sdk" + MI_BASE_DLL_FILE_EXT;
#endif
}

namespace {

#ifdef MI_PLATFORM_WINDOWS
using DsoHandle = HMODULE;
DsoHandle g_mdlSdkDll = nullptr;
#else
using DsoHandle = void*;
DsoHandle g_mdlSdkDll = nullptr;
#endif

std::string g_mdlSdkDirectory;

} // namespace

mi::neuraylib::INeuray*
PluginLoader::loadNeuray(const char* dllPath)
{
    if (g_mdlSdkDll) {
        TF_WARN("PluginLoader::loadNeuray: library already loaded.");
        return nullptr;
    }

    const char* path = dllPath;
    std::string defaultName;
    if (!path || !path[0]) {
        defaultName = std::string("libmdl_sdk") + MI_BASE_DLL_FILE_EXT;
        path = defaultName.c_str();
    }

#ifdef MI_PLATFORM_WINDOWS
    DsoHandle handle = LoadLibraryA(path);
    if (!handle) {
        const DWORD loadError = GetLastError();
        TF_WARN("PluginLoader::loadNeuray: LoadLibraryA failed for %s "
                "(Windows error %lu; check the DLL and its dependencies).",
                path, static_cast<unsigned long>(loadError));
        return nullptr;
    }
    void* symbol = reinterpret_cast<void*>(GetProcAddress(handle, "mi_factory"));
#else
    DsoHandle handle = dlopen(path, RTLD_LAZY);
    if (!handle) {
        const char* const loadError = dlerror();
        TF_WARN("PluginLoader::loadNeuray: dlopen failed for %s: %s",
                path, loadError ? loadError : "unknown loader error");
        return nullptr;
    }
    void* symbol = dlsym(handle, "mi_factory");
#endif
    if (!symbol) {
        TF_WARN("PluginLoader::loadNeuray: mi_factory not found in %s.", path);
#ifdef MI_PLATFORM_WINDOWS
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        return nullptr;
    }

    g_mdlSdkDll = handle;
    // Resolve the actual loaded object, including a library found by name.
    // Image plugins must follow this SDK when the application is relocated.
    std::string loadedPath;
    if (ArchGetAddressInfo(symbol, &loadedPath, nullptr, nullptr, nullptr)) {
        g_mdlSdkDirectory = std::filesystem::path(loadedPath).parent_path().string();
    }

#ifdef MI_PLATFORM_WINDOWS
    // Pin the DLL so Windows never calls DllMain(DLL_PROCESS_DETACH) for it
    // during ExitProcess.  libmdl_sdk's DLL detach handler crashes
    // (0xC0000409) on Windows even after a clean INeuray::shutdown() sequence.
    // Pinning is harmless: the OS reclaims all memory when the process exits.
    {
        HMODULE pinned = nullptr;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCSTR>(symbol),
            &pinned);
    }
#endif

    mi::neuraylib::INeuray* neuray =
        mi::neuraylib::mi_factory<mi::neuraylib::INeuray>(symbol);
    if (!neuray) {
        mi::base::Handle<mi::neuraylib::IVersion> version(
            mi::neuraylib::mi_factory<mi::neuraylib::IVersion>(symbol));
        if (!version) {
            TF_WARN("PluginLoader::loadNeuray: incompatible libmdl_sdk: %s.", path);
        } else {
            TF_WARN("PluginLoader::loadNeuray: header/lib version mismatch for %s "
                    "(lib %s vs header %s).",
                    path, version->get_product_version(),
                    MI_NEURAYLIB_PRODUCT_VERSION_STRING);
        }
        unloadSdkDll();
        return nullptr;
    }
    return neuray;
}

int
PluginLoader::loadImagePlugins(mi::neuraylib::INeuray* neuray)
{
    if (!neuray) {
        return 0;
    }

    mi::base::Handle<mi::neuraylib::IPlugin_configuration> plugin_cfg(
        neuray->get_api_component<mi::neuraylib::IPlugin_configuration>());
    if (!plugin_cfg) {
        TF_WARN("PluginLoader::loadImagePlugins: IPlugin_configuration not available.");
        return 0;
    }

    auto tryLoad = [&](const char* name) -> bool {
        const std::string ext = MI_BASE_DLL_FILE_EXT;
        const std::string base = std::string(name) + ext;

        // Keep image plugins with the selected SDK; a name-based fallback
        // could silently load a plugin from a different SDK installation.
        if (!g_mdlSdkDirectory.empty()) {
            const std::string fullPath =
                (std::filesystem::path(g_mdlSdkDirectory) / base).string();
            if (plugin_cfg->load_plugin_library(fullPath.c_str()) == 0) {
                return true;
            }
        }
        TF_WARN("PluginLoader: failed to load image plugin '%s' beside the loaded SDK.  "
                "MDL modules referencing texture / image resources may "
                "fail to load.",
                base.c_str());
        return false;
    };

    int loaded = 0;
    if (tryLoad("nv_openimageio")) ++loaded;
    if (tryLoad("dds")) ++loaded;
    return loaded;
}

bool
PluginLoader::unloadSdkDll()
{
    g_mdlSdkDirectory.clear();
#ifdef MI_PLATFORM_WINDOWS
    if (!g_mdlSdkDll) {
        return false;
    }
    BOOL ok = FreeLibrary(g_mdlSdkDll);
    g_mdlSdkDll = nullptr;
    return ok != 0;
#else
    if (!g_mdlSdkDll) {
        return false;
    }
    int r = dlclose(g_mdlSdkDll);
    g_mdlSdkDll = nullptr;
    return r == 0;
#endif
}

MDL_RUNTIME_NAMESPACE_CLOSE
