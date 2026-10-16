//
// SPDX-License-Identifier: Apache-2.0
//
// Dynamic load of libmdl_sdk and MDL plugin DLLs.

#pragma once

#include "defines.h"
#include "../api.h"

#include <mi/mdl_sdk.h>

#include <string>

MDL_RUNTIME_NAMESPACE_OPEN

/// Static utility class for loading/unloading the MDL SDK shared library
/// and its plugin DLLs.
class PluginLoader
{
public:
    /// Path to `libmdl_sdk` under an install prefix.
    /// Windows: `<prefix>/bin/libmdl_sdk.dll`. Linux/macOS: `<prefix>/lib/libmdl_sdk.so|.dylib`.
    USDMDL_API
    static std::string sdkDllPathUnderPrefix(const char* installPrefix);

    /// Load `libmdl_sdk` (or \p dllPath if non-null), resolve `mi_factory`, return
    /// INeuray. Caller must call shutdown() then unloadSdkDll().
    /// Returns nullptr on failure.
    USDMDL_API
    static mi::neuraylib::INeuray* loadNeuray(const char* dllPath);

    /// Free the DSO opened by loadNeuray(). Call only after INeuray::shutdown().
    /// Internal: only Context.cpp / PluginLoader.cpp call this.
    static bool unloadSdkDll();

    /// Load the MDL SDK's image plugins (`nv_openimageio` and `dds`)
    /// via `IPlugin_configuration::load_plugin_library`.  Must be
    /// called between acquiring \p neuray and `INeuray::start()`.
    /// These plugins decode the texture / image resources MDL modules
    /// reference at module-load time. Best-effort: missing plugins emit a
    /// `TF_WARN` and the call returns the number of plugins that
    /// loaded successfully (consumers without raster-resource needs
    /// can ignore the result).
    ///
    /// Plugins are loaded only from beside the SDK loaded by loadNeuray();
    /// there is no name-based fallback. Only plugin-owned instances
    /// use this setup; host-owned instances must already be started.
    ///
    /// Internal: called only from Context.cpp.
    static int loadImagePlugins(mi::neuraylib::INeuray* neuray);

    /// Static-only utility class; not instantiable.
    PluginLoader() = delete;
};

MDL_RUNTIME_NAMESPACE_CLOSE
