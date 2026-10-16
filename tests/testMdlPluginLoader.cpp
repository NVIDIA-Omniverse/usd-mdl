//
// SPDX-License-Identifier: Apache-2.0
//
// Coverage-driven unit tests for PluginLoader.  Two narrow concerns:
//
//  1. `sdkDllPathUnderPrefix` accepts nullable inputs and trims trailing
//     path separators before composing the SDK DLL path.  Both guards
//     have non-trivial branches that the integration tests never reach.
//
//  2. `loadNeuray` is idempotent: a second call without an intervening
//     `unloadSdkDll` must short-circuit with a diagnostic and return
//     nullptr.  Exercised here so the early-return path in
//     PluginLoader.cpp gets line coverage.

#include "PluginLoader.h"
#include "ModuleLoader.h"
#include "Context.h"

#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "test_exit.h"

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr,                                              \
                         "[testMdlPluginLoader] FAIL: %s (at %s:%d)\n",       \
                         msg, __FILE__, __LINE__);                            \
            ++g_failures;                                                    \
        }                                                                     \
    } while (0)

void TestSdkDllPathUnderPrefixNull()
{
    // Both branches of the early guard.
    EXPECT(usdMdl::runtime::PluginLoader::sdkDllPathUnderPrefix(nullptr).empty(),
           "sdkDllPathUnderPrefix(nullptr) must return empty");
    EXPECT(usdMdl::runtime::PluginLoader::sdkDllPathUnderPrefix("").empty(),
           "sdkDllPathUnderPrefix(\"\") must return empty");
}

void TestSdkDllPathUnderPrefixTrimsSeparators()
{
    // Trailing path separators (both kinds) get trimmed before MI_BASE_DLL_FILE_EXT
    // is appended; the resulting path must contain libmdl_sdk somewhere.
    const std::string p1 = usdMdl::runtime::PluginLoader::sdkDllPathUnderPrefix("/some/prefix");
    const std::string p2 = usdMdl::runtime::PluginLoader::sdkDllPathUnderPrefix("/some/prefix/");
    const std::string p3 = usdMdl::runtime::PluginLoader::sdkDllPathUnderPrefix("/some/prefix///");
    EXPECT(p1.find("libmdl_sdk") != std::string::npos,
           "result must reference libmdl_sdk");
    EXPECT(p1 == p2 && p2 == p3,
           "trailing slashes must not produce a different result");
}

void TestLoadNeurayIdempotent()
{
    // ensureInitialized() loads the SDK as a side effect; a direct
    // follow-up loadNeuray() must hit the "library already loaded"
    // guard and return nullptr.
    auto& ctx = usdMdl::runtime::Context::instance();
    if (!ctx.ensureInitialized()) {
        std::fprintf(stderr,
                     "[testMdlPluginLoader] FAIL: Context::ensureInitialized()\n");
        ++g_failures;
        return;
    }
    mi::neuraylib::INeuray* second = usdMdl::runtime::PluginLoader::loadNeuray(nullptr);
    EXPECT(second == nullptr,
           "loadNeuray must refuse to re-load an already-initialised SDK");
}

void TestLoadModuleNullAndEmpty()
{
    // Coverage: ModuleLoader::loadModule null/empty input guard
    // (the early-return at the top of the function).
    auto& ctx = usdMdl::runtime::Context::instance();
    if (!ctx.ensureInitialized()) {
        std::fprintf(stderr,
                     "[testMdlPluginLoader] FAIL: Context::ensureInitialized()\n");
        ++g_failures;
        return;
    }
    auto& loader = ctx.getModuleLoader();
    EXPECT(loader.loadModule(nullptr) == -1,
           "loadModule(nullptr) must return -1");
    EXPECT(loader.loadModule("") == -1,
           "loadModule(\"\") must return -1");
}

void TestLoadModuleNonexistent()
{
    // Coverage: ModuleLoader::loadModule SDK-failure path on a real
    // (but unresolvable) module name -- exercises the load_module call
    // returning < 0 without hitting the null/empty guard.  The SDK
    // error-message dispatch loop is also reached as a side-effect
    // (the C++ test counterpart of broken.mdl, but without needing a
    // file fixture on disk).
    auto& ctx = usdMdl::runtime::Context::instance();
    if (!ctx.ensureInitialized()) {
        std::fprintf(stderr,
                     "[testMdlPluginLoader] FAIL: Context::ensureInitialized()\n");
        ++g_failures;
        return;
    }
    const mi::Sint32 rc =
        ctx.getModuleLoader().loadModule("::definitely::not::a::real::module");
    EXPECT(rc < 0,
           "loadModule for an unresolvable name must return a negative code");
}

} // namespace

int main(int /*argc*/, char* /*argv*/[])
{
    TestSdkDllPathUnderPrefixNull();
    TestSdkDllPathUnderPrefixTrimsSeparators();
    TestLoadNeurayIdempotent();
    TestLoadModuleNullAndEmpty();
    TestLoadModuleNonexistent();

    if (g_failures == 0) {
        std::fprintf(stderr,
                     "\n[testMdlPluginLoader] OK (all subtests passed)\n");
    } else {
        std::fprintf(stderr,
                     "\n[testMdlPluginLoader] %d FAILURE(S)\n", g_failures);
    }
    std::fflush(stderr);

    // Same teardown rationale as the other C++ tests in this dir.
    mdl_test::FlushAndExit(g_failures == 0 ? 0 : 1);
}
