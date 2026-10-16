//
// SPDX-License-Identifier: Apache-2.0
//
// Validates the subIdentifier strings produced by
// `Utils::ComputeMdlSubIdentifier` for the canonical Omniverse Kit
// convention:
//
//   - non-overloaded materials / functions: simple name only.
//   - overloaded functions: simple name + parameter signature, with
//     same-module struct / enum types rendered unqualified (`Local`),
//     in-search-path cross-module types kept fully qualified (e.g.
//     `::tex::wrap_mode`), and builtin types passed through.
//
// The fixture `mdl/sub_identifier.mdl` declares one function per
// branch of the formatter; register that module explicitly, then assert the
// subIdentifier on each returned node without requiring startup discovery.

#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/token.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdr/shaderNode.h>

#include "tokens.h"
#include "addModuleToRegistry.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "test_exit.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

int g_failures = 0;

std::string
LookupMetadata(const SdrTokenMap& m, const TfToken& key)
{
    auto const it = m.find(key);
    return it == m.end() ? std::string() : it->second;
}

#define EXPECT(cond, msg)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr,                                              \
                         "[testMdlSubIdentifier] FAIL: %s (at %s:%d)\n",      \
                         msg, __FILE__, __LINE__);                            \
            ++g_failures;                                                    \
        }                                                                     \
    } while (0)

#define EXPECT_EQ(actual, expected, msg)                                      \
    do {                                                                      \
        const std::string _a = (actual);                                      \
        const std::string _b = (expected);                                    \
        if (_a != _b) {                                                       \
            std::fprintf(stderr,                                              \
                         "[testMdlSubIdentifier] FAIL: %s\n"                  \
                         "  got     : '%s'\n"                                 \
                         "  expected: '%s'\n  (at %s:%d)\n",                  \
                         msg, _a.c_str(), _b.c_str(), __FILE__, __LINE__);    \
            ++g_failures;                                                    \
        }                                                                     \
    } while (0)

/// Expected (sub_identifier, count) entries that exercise each
/// branch of the formatter.  Note that the SDK also synthesizes
/// overloaded constructors for our `Local` struct (the default
/// constructor, a copy constructor, and a field-wise constructor),
/// each of which becomes its own Sdr node; those are validated
/// separately from the authored function signatures.
const std::vector<std::pair<std::string, int>> kExpectedSubIds = {
    // Non-overloaded -> simple name only.
    {"simple_int",                     1},
    {"simple_struct",                  1},
    {"simple_material",                1},

    // Overload set on builtin scalar types.
    {"over_id(int)",                   1},
    {"over_id(int,int)",               1},

    // Same-module struct: unqualified type name.
    {"over_id(Local)",              1},

    // Cross-module enum: type name keeps its `::module::` prefix.
    {"over_id(::tex::wrap_mode)",      1},

    // Immediate-sized and deferred-sized arrays.
    {"over_array(int[3])",             1},
    {"over_array(float[N])",           1},

    // Builtin vector types.
    {"over_vec(float3)",               1},
    {"over_vec(float3,float3)",        1},
};

/// Auto-generated struct constructors the SDK emits for `Local`.
/// These exist because every `export struct` produces explicit-
/// constructor function definitions; they're an Sdr-level reality
/// rather than something the formatter chose, but their shape still
/// proves the formatter behaves on overloaded constructors with
/// same-module struct parameters.
const std::vector<std::pair<std::string, int>> kExpectedStructCtors = {
    {"Local(Local)",                1},   // copy constructor
    {"Local(int,float)",               1},   // explicit field-wise ctor
};

bool
LoadMdlPlugin()
{
    PlugRegistry& reg = PlugRegistry::GetInstance();
    PlugPluginPtr plug = reg.GetPluginWithName("usdMdl");
    if (!plug) {
        std::fprintf(stderr,
                     "[testMdlSubIdentifier] plugin 'usdMdl' not found in PXR_PLUGINPATH_NAME\n");
        return false;
    }
    if (!plug->Load()) {
        std::fprintf(stderr, "[testMdlSubIdentifier] usdMdl plugin failed to load\n");
        return false;
    }
    return true;
}

} // namespace

int main()
{
    std::fprintf(stderr, "\nRUN testMdlSubIdentifier\n");

    if (!LoadMdlPlugin()) {
        mdl_test::FlushAndExit(1);
    }

    const std::string fixture =
        std::filesystem::absolute("mdl/sub_identifier.mdl").generic_string();
    const auto allNodes = UsdMdlAddModuleToRegistry(SdfAssetPath(fixture));

    // Collect every subIdentifier string from nodes whose source is
    // `sub_identifier.mdl`.  Building a multiset because overloads
    // can technically share an Sdr identifier; assertions check both
    // presence and count.
    std::unordered_map<std::string, int> seen;
    int subIdNodes = 0;
    for (SdrShaderNodeConstPtr const node : allNodes) {
        if (!node) {
            continue;
        }
        const std::string impl = node->GetResolvedImplementationURI();
        if (impl.find("sub_identifier.mdl") == std::string::npos) {
            continue;
        }
        ++subIdNodes;
        const std::string sub =
            LookupMetadata(node->GetMetadata(), UsdMdlNodeMetadataTokens->SubIdentifier);
        ++seen[sub];
    }

    std::fprintf(stderr,
                 "[testMdlSubIdentifier] %d sdr nodes from sub_identifier.mdl\n",
                 subIdNodes);
    for (const auto& kv : seen) {
        std::fprintf(stderr, "  '%s' x %d\n", kv.first.c_str(), kv.second);
    }

    // Verify each expected subIdentifier appears with the expected
    // multiplicity.  Both the user-authored fixtures and the
    // auto-generated struct constructors are checked.
    auto checkExpected = [&](const std::vector<std::pair<std::string, int>>& expected,
                             const char* label)
    {
        for (const auto& kv : expected) {
            const std::string& sub = kv.first;
            const int expectedCount = kv.second;
            const int actualCount = seen[sub];
            if (actualCount != expectedCount) {
                std::fprintf(stderr,
                             "[testMdlSubIdentifier] FAIL [%s]: subIdentifier '%s' x %d expected, x %d found\n",
                             label, sub.c_str(), expectedCount, actualCount);
                ++g_failures;
            }
        }
    };
    checkExpected(kExpectedSubIds, "user");
    checkExpected(kExpectedStructCtors, "ctor");

    if (g_failures == 0) {
        std::fprintf(stderr, "[testMdlSubIdentifier] OK\n");
    } else {
        std::fprintf(stderr,
                     "[testMdlSubIdentifier] %d failure(s)\n", g_failures);
    }
    mdl_test::FlushAndExit(g_failures == 0 ? 0 : 1);
}
