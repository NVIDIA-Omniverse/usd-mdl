// SPDX-License-Identifier: Apache-2.0

#include "addModuleToRegistry.h"
#include "initialize.h"
#include "tokens.h"

#include <mi/mdl_sdk.h>
#include <pxr/base/arch/env.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdr/shaderNode.h>

#include <algorithm>
#include <filesystem>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

bool HasMdlNodes(SdrRegistry& registry)
{
    for (const auto* node : registry.GetShaderNodesByFamily()) {
        if (node && node->GetSourceType() == UsdMdlTokens->Mdl) {
            return true;
        }
    }
    return false;
}

void CheckConfiguredPaths(SdrRegistry& registry)
{
    const auto uris = registry.GetSearchURIs();
    mi::base::Handle<mi::neuraylib::INeuray> neuray(UsdMdlGetNeuray());
    mi::base::Handle<mi::neuraylib::IMdl_configuration> configuration(
        neuray->get_api_component<mi::neuraylib::IMdl_configuration>());
    TF_AXIOM(configuration->get_mdl_paths_length() > 0);
    auto next = uris.begin();
    for (mi::Size i = 0; i < configuration->get_mdl_paths_length(); ++i) {
        mi::base::Handle<const mi::IString> path(configuration->get_mdl_path(i));
        std::string normalized = path->get_c_str();
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        if (normalized.back() != '/') {
            normalized += '/';
        }
        // Other discovery plugins also contribute URIs; the MDL roots must
        // appear in the SDK's order within that aggregate list.
        next = std::find(next, uris.end(), normalized);
        TF_AXIOM(next != uris.end());
        ++next;
    }
    TF_AXIOM(registry.GetSearchURIs() == uris);
}

} // namespace

int main(int argc, char** argv)
{
    const bool explicitInitialize = argc > 1 && std::string(argv[1]) == "initialize";
    // Exercise the actual unset default even on a developer's eager environment.
    if (argc > 2 && std::string(argv[2]) == "unset") {
        ArchRemoveEnv("USD_MDL_DISCOVER_ON_STARTUP");
    } else {
        ArchSetEnv("USD_MDL_DISCOVER_ON_STARTUP", "0", true);
    }

    TF_AXIOM(!mi::base::Handle<mi::neuraylib::INeuray>(UsdMdlGetNeuray()).get());
    auto& registry = SdrRegistry::GetInstance();
    TF_AXIOM(!HasMdlNodes(registry));
    const auto earlyPaths = registry.GetSearchURIs();
    TF_AXIOM(registry.GetSearchURIs() == earlyPaths);
    TF_AXIOM(!mi::base::Handle<mi::neuraylib::INeuray>(UsdMdlGetNeuray()).get());

    if (explicitInitialize) {
        TF_AXIOM(UsdMdlInitialize());
        CheckConfiguredPaths(registry);
        TF_AXIOM(!HasMdlNodes(registry));

        // Inspecting paths must not compile even an ordinary fixture module.
        mi::base::Handle<mi::neuraylib::INeuray> neuray(UsdMdlGetNeuray());
        mi::base::Handle<mi::neuraylib::IDatabase> database(
            neuray->get_api_component<mi::neuraylib::IDatabase>());
        mi::base::Handle<mi::neuraylib::IScope> scope(database->get_global_scope());
        mi::base::Handle<mi::neuraylib::ITransaction> transaction(scope->create_transaction());
        {
            mi::base::Handle<const mi::neuraylib::IModule> module(
                transaction->access<mi::neuraylib::IModule>("mdl::arithmetic"));
            TF_AXIOM(!module);
        }
        transaction->commit();
    }

    const auto fixture = std::filesystem::absolute("mdl/arithmetic.mdl").generic_string();
    const auto* node = registry.GetShaderNodeFromAsset(
        SdfAssetPath(fixture), SdrTokenMap(), TfToken("test_add(int,int)"), UsdMdlTokens->Mdl);
    TF_AXIOM(node && node->IsValid());
    TF_AXIOM(node->GetMetadata().at(UsdMdlNodeMetadataTokens->SubIdentifier) == "test_add(int,int)");
    TF_AXIOM(mi::base::Handle<mi::neuraylib::INeuray>(UsdMdlGetNeuray()).get());
    CheckConfiguredPaths(registry);
    TF_AXIOM(!HasMdlNodes(registry));

    const auto nodes = UsdMdlAddModuleToRegistry(SdfAssetPath(fixture));
    TF_AXIOM(nodes.size() == 2);
    TF_AXIOM(HasMdlNodes(registry));
    UsdMdlShutdown();
    TF_AXIOM(!mi::base::Handle<mi::neuraylib::INeuray>(UsdMdlGetNeuray()).get());
    TF_AXIOM(registry.GetSearchURIs() == earlyPaths);
    return 0;
}
