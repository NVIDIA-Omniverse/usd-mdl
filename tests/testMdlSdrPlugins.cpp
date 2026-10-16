//
// SPDX-License-Identifier: Apache-2.0
//
// Sweep test: for every MDL-source SdrShaderNode discovered by SdrRegistry,
// author a UsdShade.Shader on an in-memory stage with the node's source asset
// and sub-identifier, then resolve the shader back via the canonical USD entry
// point UsdShadeShader::GetShaderNodeForSourceType("mdl") and assert identity.
//
// Set PXR_PLUGINPATH_NAME to the directory containing the usdMdl plugInfo.json
// (same folder as the usdMdl shared library), and MDL_USER_PATH to a directory
// whose .mdl files should be picked up, before running this executable.
// Set USD_MDL_DISCOVER_ON_STARTUP=1 to enable the discovery sweep.
//
// Optional env: MDL_TEST_PARSE_RESULTS_LOG=<file> dumps a verbose property
// listing per resolved node to the named file.

#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/getenv.h>
#include <pxr/base/tf/token.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdShade/shader.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "test_exit.h"
#include "tokens.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

// Local aliases keep the call sites concise; the project-wide source
// of truth is the token registries declared in `tokens.h`.
const TfToken& kMdl = UsdMdlTokens->Mdl;
const TfToken& kMdlSubIdentifierMetadata = UsdMdlNodeMetadataTokens->SubIdentifier;

void
WriteSortedTokenMap(std::ostream& out, const SdrTokenMap& m, const std::string& indent)
{
    if (m.empty()) {
        out << indent << "(none)\n";
        return;
    }
    std::set<std::string> keys;
    for (const auto& kv : m) {
        keys.insert(kv.first.GetString());
    }
    for (const std::string& k : keys) {
        const TfToken key(k);
        auto it = m.find(key);
        if (it != m.end()) {
            out << indent << k << " = " << it->second << "\n";
        }
    }
}

void
WriteShaderPropertyFull(std::ostream& out, const SdrShaderProperty& prop,
                        const std::string& indent)
{
    out << indent << "name: " << prop.GetName().GetString() << "\n";
    out << indent << "type: " << prop.GetType().GetString() << "\n";
    out << indent << "defaultValue: " << prop.GetDefaultValue() << "\n";
    out << indent << "isOutput: " << (prop.IsOutput() ? "true" : "false") << "\n";
    out << indent << "isArray: " << (prop.IsArray() ? "true" : "false") << "\n";
    out << indent << "arraySize: " << prop.GetArraySize() << "\n";
    out << indent << "metadata:\n";
    WriteSortedTokenMap(out, prop.GetMetadata(), indent + "  ");
    out << indent << "hints:\n";
    WriteSortedTokenMap(out, prop.GetHints(), indent + "  ");
    if (!prop.GetOptions().empty()) {
        out << indent << "options:\n";
        for (const SdrOption& opt : prop.GetOptions()) {
            out << indent << "  " << opt.first.GetString() << " -> "
                << opt.second.GetString() << "\n";
        }
    }
}

void
WriteShaderNodeFull(std::ostream& out, const SdrShaderNode* node)
{
    if (!node) {
        out << "(null SdrShaderNode)\n";
        return;
    }
    out << "identifier: " << node->GetIdentifier().GetString() << "\n";
    out << "name: " << node->GetName() << "\n";
    out << "family: " << node->GetFamily().GetString() << "\n";
    out << "context: " << node->GetContext().GetString() << "\n";
    out << "sourceType: " << node->GetSourceType().GetString() << "\n";
    out << "resolvedDefinitionURI: " << node->GetResolvedDefinitionURI() << "\n";
    out << "resolvedImplementationURI: " << node->GetResolvedImplementationURI() << "\n";
    out << "isValid: " << (node->IsValid() ? "true" : "false") << "\n";
    out << "node metadata:\n";
    WriteSortedTokenMap(out, node->GetMetadata(), "  ");
    out << "inputs:\n";
    for (const TfToken& inputName : node->GetShaderInputNames()) {
        out << "  " << inputName.GetString() << ":\n";
        if (SdrShaderPropertyConstPtr p = node->GetShaderInput(inputName)) {
            WriteShaderPropertyFull(out, *p, "    ");
        }
    }
    out << "outputs:\n";
    for (const TfToken& outputName : node->GetShaderOutputNames()) {
        out << "  " << outputName.GetString() << ":\n";
        if (SdrShaderPropertyConstPtr p = node->GetShaderOutput(outputName)) {
            WriteShaderPropertyFull(out, *p, "    ");
        }
    }
}

std::string
MetadataLookup(const SdrTokenMap& m, const TfToken& key)
{
    auto it = m.find(key);
    return it == m.end() ? std::string() : it->second;
}

} // namespace

int
main()
{
    std::cout << "\nRUN testMdlSdrPlugins (USD-driven sweep)\n" << std::endl;

    PlugPluginPtr plug =
        PlugRegistry::GetInstance().GetPluginWithName("usdMdl");
    TF_AXIOM(plug);
    TF_AXIOM(plug->Load());

    UsdStageRefPtr stage = UsdStage::CreateInMemory();
    TF_AXIOM(stage);
    stage->DefinePrim(SdfPath("/World"));

    // Constructing SdrRegistry runs full discovery (which loads MDL modules
    // through our DiscoveryPlugin -> usdMdl/runtime -> MDL SDK).
    SdrRegistry& sdr = SdrRegistry::GetInstance();

    const SdrIdentifierVec ids =
        sdr.GetShaderNodeIdentifiers(TfToken(), SdrVersionFilterAllVersions);

    int authored = 0;
    int validCount = 0;
    std::vector<SdrShaderNodeConstPtr> mdlNodes;
    mdlNodes.reserve(ids.size());

    for (const SdrIdentifier& id : ids) {
        SdrShaderNodeConstPtrVec nodesForId = sdr.GetShaderNodesByIdentifier(id);
        for (const SdrShaderNodeConstPtr& src : nodesForId) {
            if (!src || src->GetSourceType() != kMdl) {
                continue;
            }
            mdlNodes.push_back(src);

            const std::string impl = src->GetResolvedImplementationURI();
            if (impl.empty()) {
                std::fprintf(stderr,
                             "[testMdlSdrPlugins] node %s has no implementation URI\n",
                             src->GetIdentifier().GetString().c_str());
                continue;
            }

            const std::string subId =
                MetadataLookup(src->GetMetadata(), kMdlSubIdentifierMetadata);
            if (subId.empty()) {
                std::fprintf(stderr,
                             "[testMdlSdrPlugins] node %s missing mdl:subIdentifier metadata\n",
                             src->GetIdentifier().GetString().c_str());
                continue;
            }

            const SdfPath primPath(
                std::string("/World/Mdl_") + std::to_string(authored));
            UsdPrim shaderPrim = stage->DefinePrim(primPath, TfToken("Shader"));
            TF_AXIOM(shaderPrim);
            UsdShadeShader shader(shaderPrim);
            TF_AXIOM(shader);

            // sourceAsset: use the resolved implementation URI as both asset
            // and resolved path; that mirrors what SdrShaderNodeDiscoveryResult
            // stored when our DiscoveryPlugin emitted this node.
            const SdfAssetPath assetPath(impl, impl);
            TF_AXIOM(shader.SetSourceAsset(assetPath, kMdl));
            TF_AXIOM(shader.SetSourceAssetSubIdentifier(TfToken(subId), kMdl));

            SdrShaderNodeConstPtr const resolved =
                shader.GetShaderNodeForSourceType(kMdl);
            if (!resolved) {
                std::fprintf(stderr,
                             "[testMdlSdrPlugins] UsdShade resolution failed for %s (sub=%s)\n",
                             impl.c_str(), subId.c_str());
                continue;
            }
            if (resolved->GetIdentifier() != src->GetIdentifier()) {
                std::fprintf(stderr,
                             "[testMdlSdrPlugins] identifier mismatch: src=%s resolved=%s\n",
                             src->GetIdentifier().GetString().c_str(),
                             resolved->GetIdentifier().GetString().c_str());
                continue;
            }
            ++authored;
            if (resolved->IsValid()) {
                ++validCount;
            } else {
                std::fprintf(stderr,
                             "[testMdlSdrPlugins] invalid node for %s (sub=%s)\n",
                             impl.c_str(), subId.c_str());
            }
        }
    }

    std::cout << "  MDL nodes discovered: " << mdlNodes.size()
              << "  authored & re-resolved: " << authored
              << "  valid: " << validCount << "\n";

    TF_AXIOM(!mdlNodes.empty());
    TF_AXIOM(authored == static_cast<int>(mdlNodes.size()));
    // Every discovered fixture node must survive the round trip as a valid node,
    // including nodes whose resource defaults cannot be represented or loaded.
    TF_AXIOM(validCount == authored);

    const std::string logPath = TfGetenv("MDL_TEST_PARSE_RESULTS_LOG");
    if (!logPath.empty()) {
            std::ofstream out(logPath, std::ios::out | std::ios::trunc);
            if (!out) {
                std::cerr << "[testMdlSdrPlugins] could not open \"" << logPath
                          << "\" for parse log\n";
            } else {
                out << "USD-driven MDL SdrShaderNode sweep\n";
                out << "count: " << mdlNodes.size() << "\n\n";
                for (size_t i = 0; i < mdlNodes.size(); ++i) {
                    out << "======== node " << i << " ========\n";
                    WriteShaderNodeFull(out, mdlNodes[i]);
                    out << "\n";
                }
                std::cout << "\n[testMdlSdrPlugins] wrote dump to \"" << logPath << "\"\n";
            }
    }

    std::printf("\nOK: %d MDL SdrShaderNode(s) round-tripped through UsdShade.Shader\n",
                authored);

    std::cout.flush();
    std::cerr.flush();

    // Bypass DLL detach to avoid re-entering MDL SDK code via Sdr singleton
    // destruction during ExitProcess (same reason as testMdlUsdShade).
    mdl_test::FlushAndExit(0);
}
