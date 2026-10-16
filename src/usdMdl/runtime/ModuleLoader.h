//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "defines.h"
#include "../api.h"

#include <mi/mdl_sdk.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_set>

MDL_RUNTIME_NAMESPACE_OPEN

class Context;

/// Loads MDL modules into the global scope using a shared edit transaction
/// (batches concurrent `load_module` calls so they share a single
/// transaction; the SDK's `IMdl_loading_wait_handle_factory` (see
/// `createExecutionContext` in the .cpp) coordinates load races inside
/// the SDK itself).
///
/// Internal to the usdMdl library.  Only `loadModule` is exported because
/// the test binaries that drive shutdown phases (`testMdlPluginLoader`,
/// `testMdlRuntimeShutdown`) call it across the DLL boundary; the ctor /
/// dtor are invoked exclusively from `Context.cpp` inside the same DLL.
class ModuleLoader
{
public:
    /// Constructs a loader bound to the given MDL SDK context.
    explicit ModuleLoader(Context& context);
    ~ModuleLoader();

    /// Loads the named MDL module.
    ///
    /// Thread-safety: `loadModule` is safe to call from multiple threads
    /// concurrently with the *same* module name (the loaded-set lookup
    /// dedupes, and the SDK's loading-wait-handle factory blocks late
    /// arrivals until the first load finishes) and with *different*
    /// module names (concurrent calls share a single
    /// `ITransaction` whose commit is reference-counted so the last
    /// finishing loader commits the batch).  The SDK is the synchronizer
    /// for the actual `load_module` body; this class only synchronizes
    /// the transaction lifetime and the `m_loadedModules` set.
    USDMDL_API
    mi::Sint32 loadModule(const char* qualifiedModuleName);

private:
    struct LoadingBatch
    {
        mi::base::Handle<mi::neuraylib::ITransaction> transaction;
        int activeLoadCount = 0;
        bool completed = false;
        bool commitOk = false;
    };

    mi::base::Handle<mi::neuraylib::IMdl_execution_context> createExecutionContext() const;

    Context& m_context;

    std::mutex m_loadMutex;
    std::condition_variable m_loadCv;
    /// Current transaction batch. Every participant retains its own shared
    /// pointer so a later batch cannot overwrite the completion/result state
    /// while waiters from the previous batch are waking up.
    std::shared_ptr<LoadingBatch> m_loadingBatch;

    mi::base::Handle<mi::neuraylib::IMdl_loading_wait_handle_factory> m_waitHandleFactory;

    mutable std::shared_mutex m_modulesMutex;
    std::unordered_set<std::string> m_loadedModules;
};

MDL_RUNTIME_NAMESPACE_CLOSE
