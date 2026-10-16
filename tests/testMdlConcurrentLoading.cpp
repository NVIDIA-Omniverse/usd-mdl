//
// SPDX-License-Identifier: Apache-2.0
//

#include "Context.h"
#include "ModuleLoader.h"

#include <pxr/base/tf/diagnostic.h>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

int
main()
{
    usdMdl::runtime::Context& context = usdMdl::runtime::Context::instance();
    if (!context.ensureInitialized()) {
        std::fprintf(stderr, "[testMdlConcurrentLoading] Context init failed\n");
        return 1;
    }

    // Exercise same-module SDK wait handles and different-module shared
    // transaction participants in one synchronized wave.
    const std::vector<std::string> modules = {
        "::annotation", "::array", "::color", "::expression",
        "::matrix", "::string", "::struct", "::vector",
    };
    constexpr int kThreadCount = 48;
    std::mutex startMutex;
    std::condition_variable startCv;
    int ready = 0;
    bool start = false;
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&, i] {
            {
                std::unique_lock<std::mutex> lock(startMutex);
                ++ready;
                startCv.notify_all();
                startCv.wait(lock, [&] { return start; });
            }
            const std::string& module = modules[static_cast<std::size_t>(i) % modules.size()];
            if (context.getModuleLoader().loadModule(module.c_str()) < 0) {
                ++failures;
            }
        });
    }

    {
        std::unique_lock<std::mutex> lock(startMutex);
        startCv.wait(lock, [&] { return ready == kThreadCount; });
        start = true;
    }
    startCv.notify_all();

    for (std::thread& thread : threads) {
        thread.join();
    }

    // Inspect committed state before a sequential load could repair it.
    {
        mi::base::Handle<mi::neuraylib::ITransaction> inspection(
            context.getScope()->create_transaction());
        TF_AXIOM(inspection);
        for (const std::string& name : modules) {
            mi::base::Handle<const mi::neuraylib::IModule> module(
                inspection->access<mi::neuraylib::IModule>(("mdl" + name).c_str()));
            TF_AXIOM(module);
            TF_AXIOM(std::string(module->get_mdl_name()) == name);
            TF_AXIOM(module->get_function_count() > 0);
            mi::base::Handle<const mi::neuraylib::IFunction_definition> definition(
                inspection->access<mi::neuraylib::IFunction_definition>(module->get_function(0)));
            TF_AXIOM(definition);
        }
        inspection->abort();
    }

    // Every successfully committed module must now take the loaded-set fast
    // path and remain observable as loaded.
    for (const std::string& module : modules) {
        if (context.getModuleLoader().loadModule(module.c_str()) < 0) {
            ++failures;
        }
    }

    context.shutdown();
    if (failures.load() != 0) {
        std::fprintf(stderr, "[testMdlConcurrentLoading] %d load failure(s)\n",
                     failures.load());
        return 1;
    }
    std::fprintf(stderr, "[testMdlConcurrentLoading] OK\n");
    return 0;
}
