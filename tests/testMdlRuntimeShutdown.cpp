//
// SPDX-License-Identifier: Apache-2.0
//

// Bisection test for MDL SDK shutdown.
//
// Phase 1 (no args):      Context lifecycle only — no modules
// Phase 2 (MDL_PATH):     Context + ModuleLoader::loadModule
// Phase 3 (MDL_PATH -p):  Context + ModuleLoader + Module ctor (no parse)
// Phase 4 (MDL_PATH -pp):  + GetSdrFromDiscoveryResult (full parse w/ Converters)
// Phase 5 (MDL_PATH -ppp): + MdlSdrIntegration singleton (GetDiscoveryResults + parse)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "Context.h"
#include "ModuleLoader.h"
#include "PluginLoader.h"
#include "mdlModule.h"
#include "mdlSdrIntegration.h"

#include <pxr/base/tf/diagnostic.h>
#include <pxr/usd/sdr/shaderNodeDiscoveryResult.h>
#include <pxr/usd/sdr/shaderNode.h>

PXR_NAMESPACE_USING_DIRECTIVE

#define LOG(...) do { std::fprintf(stderr, __VA_ARGS__); std::fflush(stderr); } while(0)

int main(int argc, char* argv[])
{
    const char* mdlUserPath = nullptr;
    int parseLevel = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-ppp") == 0) { parseLevel = 3; }
        else if (std::strcmp(argv[i], "-pp") == 0) { parseLevel = 2; }
        else if (std::strcmp(argv[i], "-p") == 0) { parseLevel = 1; }
        else { mdlUserPath = argv[i]; }
    }

    auto& ctx = usdMdl::runtime::Context::instance();
    if (mdlUserPath) {
        std::vector<std::string> paths = { mdlUserPath };
        std::string dll = usdMdl::runtime::PluginLoader::sdkDllPathUnderPrefix(MDL_SDK_INSTALL_PREFIX);
        if (!ctx.initialize(dll.c_str(), paths)) {
            LOG("[test] FAIL: Context::initialize\n");
            return 1;
        }
    } else {
        if (!ctx.ensureInitialized()) {
            LOG("[test] FAIL: Context::ensureInitialized\n");
            return 1;
        }
    }
    LOG("[test] Context initialized.\n");
    TF_AXIOM(ctx.isInitialized());
    TF_AXIOM(ctx.getNeuray());
    TF_AXIOM(ctx.getNeuray()->get_status() == mi::neuraylib::INeuray::STARTED);

    if (mdlUserPath) {
        LOG("[test] Loading ::color via ModuleLoader...\n");
        mi::Sint32 rc = ctx.getModuleLoader().loadModule("::color");
        LOG("[test] loadModule returned %d\n", (int)rc);
        TF_AXIOM(rc >= 0);

        if (parseLevel >= 1) {
            std::string resolvedPath = std::string(mdlUserPath) + "/color.mdl";
            LOG("[test] Creating Module for ::color...\n");
            mi::base::Handle<mi::neuraylib::ITransaction> inspection(
                ctx.getScope()->create_transaction());
            if (!inspection) {
                return 1;
            }
            {
                MdlModule mod("::color", SdfAssetPath(resolvedPath), inspection.get());
                LOG("[test] Module valid=%d\n", (int)mod.IsValid());
                TF_AXIOM(mod.IsValid());

                if (parseLevel >= 2) {
                    LOG("[test] Phase 4: calling GetSdrFromDiscoveryResult (full parse)...\n");

                    // Build a discovery result matching color.mdl / test_color
                    SdrShaderNodeDiscoveryResult dr(
                        TfToken("::color"),
                        SdrVersion().GetAsDefault(),
                        "test_color",
                        TfToken(),
                        TfToken("mdl"),
                        TfToken("mdl"),
                        resolvedPath,
                        resolvedPath,
                        std::string(),
                        SdrTokenMap(),
                        "mdl::color::test_color",
                        TfToken("test_color"));

                    SdrShaderNodeUniquePtr node = mod.GetSdrFromDiscoveryResult(dr);
                    LOG("[test] parsed node valid=%d\n", (int)(node && node->IsValid()));
                    TF_AXIOM(node && node->IsValid());
                    node.reset();
                    LOG("[test] SdrShaderNode destroyed.\n");
                }
            }
            inspection->abort();
            LOG("[test] Module destroyed.\n");
        }
    }

    if (parseLevel >= 3 && mdlUserPath) {
        LOG("[test] Phase 5: MdlSdrIntegration singleton path\n");

        // Step A: just GetDiscoveryResults (populates _lastDiscovery + loads modules)
        MdlSdrIntegration& reg = MdlSdrIntegration::Get();
        SdrShaderNodeDiscoveryResultVec results = reg.GetDiscoveryResults();
        LOG("[test] Phase 5A: GetDiscoveryResults returned %zu results\n", results.size());
        TF_AXIOM(!results.empty());

        // Step B: parse first result (triggers EnsureNodeData / Converters)
        if (!results.empty()) {
            SdrShaderNodeUniquePtr node = reg.GetSdrFromDiscoveryResult(results[0]);
            LOG("[test] Phase 5B: parsed node valid=%d\n", (int)(node && node->IsValid()));
            TF_AXIOM(node && node->IsValid());
            node.reset();
            LOG("[test] Phase 5B: SdrShaderNode destroyed\n");
        }

        // Step C: explicit Shutdown (same as atexit handler does)
        LOG("[test] Phase 5C: calling MdlSdrIntegration::Shutdown()\n");
        reg.Shutdown();
        TF_AXIOM(!ctx.isInitialized());
        TF_AXIOM(!ctx.getNeuray());
        TF_AXIOM(!ctx.getScope());
        TF_AXIOM(!ctx.getEntityResolver());
        LOG("[test] Phase 5C: Shutdown() complete\n");

        LOG("[test] shutdown() complete. Exiting.\n");
        return 0;
    }

    LOG("[test] Calling Context::shutdown()...\n");
    ctx.shutdown();
    TF_AXIOM(!ctx.isInitialized());
    TF_AXIOM(!ctx.getNeuray());
    TF_AXIOM(!ctx.getScope());
    TF_AXIOM(!ctx.getEntityResolver());
    LOG("[test] shutdown() complete. Exiting.\n");

    return 0;
}
