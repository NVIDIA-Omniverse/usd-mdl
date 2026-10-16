//
// SPDX-License-Identifier: Apache-2.0
//

#include <usdMdl/addModuleToRegistry.h>
#include <usdMdl/initialize.h>
#include <usdMdl/tokens.h>

#include <mi/mdl_sdk.h>

#include <pxr/base/tf/token.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdShade/connectableAPI.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/output.h>
#include <pxr/usd/usdShade/shader.h>

#include <iostream>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

bool
PrintMdlSearchPaths()
{
    mi::base::Handle<mi::neuraylib::INeuray> neuray(UsdMdlGetNeuray());
    if (!neuray) {
        std::cerr << "MDL runtime is not initialized; call UsdMdlInitialize() first\n";
        return false;
    }
    mi::base::Handle<mi::neuraylib::IMdl_configuration> configuration(
        neuray->get_api_component<mi::neuraylib::IMdl_configuration>());

    // Read the effective SDK configuration after all initialization paths merge.
    const mi::Size count = configuration->get_mdl_paths_length();
    std::cout << "MDL search paths (in lookup order):\n";
    if (count == 0) {
        std::cout << "  (none)\n";
    }
    for (mi::Size i = 0; i < count; ++i) {
        mi::base::Handle<const mi::IString> path(configuration->get_mdl_path(i));
        std::cout << "  [" << i << "] " << path->get_c_str() << '\n';
    }
    return true;
}

bool
PrintMdlDefinition(const SdrShaderNode& node)
{
    mi::base::Handle<mi::neuraylib::INeuray> neuray(UsdMdlGetNeuray());
    const auto& metadata = node.GetMetadata();
    const auto moduleDbName = metadata.find(UsdMdlNodeMetadataTokens->MdlDbModuleName);
    const auto definitionDbName = metadata.find(UsdMdlNodeMetadataTokens->MdlDbDefinitionName);
    if (!neuray || moduleDbName == metadata.end() || definitionDbName == metadata.end()) {
        std::cerr << "MDL runtime or database names are unavailable\n";
        return false;
    }

    mi::base::Handle<mi::neuraylib::IDatabase> database(
        neuray->get_api_component<mi::neuraylib::IDatabase>());
    mi::base::Handle<mi::neuraylib::IScope> scope(
        database ? database->get_global_scope() : nullptr);
    mi::base::Handle<mi::neuraylib::ITransaction> transaction(
        scope ? scope->create_transaction() : nullptr);
    if (!transaction) {
        std::cerr << "cannot create an MDL transaction\n";
        return false;
    }
    mi::base::Handle<const mi::neuraylib::IModule> module(
        transaction->access<mi::neuraylib::IModule>(moduleDbName->second.c_str()));
    mi::base::Handle<const mi::neuraylib::IFunction_definition> definition(
        transaction->access<mi::neuraylib::IFunction_definition>(definitionDbName->second.c_str()));
    if (!module || !definition) {
        std::cerr << "cannot access MDL module or definition: "
                  << moduleDbName->second << ", " << definitionDbName->second << '\n';
        definition.reset();
        module.reset();
        transaction->abort();
        return false;
    }
    std::cout << "MDL database module: " << moduleDbName->second << '\n'
              << "MDL qualified module name: " << module->get_mdl_name() << '\n'
              << "MDL database definition: " << definitionDbName->second << '\n'
              << "MDL simple name: " << definition->get_mdl_simple_name() << '\n';
    definition.reset();
    module.reset();
    if (transaction->commit() != 0) {
        std::cerr << "cannot commit the MDL transaction\n";
        return false;
    }
    // These SDK handles must be released before main calls UsdMdlShutdown().
    return true;
}

void
PrintResourceFiles(mi::neuraylib::IMdl_entity_resolver* resolver, const SdfAssetPath& assetPath)
{
    std::cout << "    files:";
    const std::string& resolvedPath = assetPath.GetResolvedPath();
    // Concrete paths already retain Ar's identifier. Only masks need expansion.
    // Open a concrete path unchanged with the same resolver/context:
    // auto asset = ArGetResolver().OpenAsset(ArResolvedPath(resolvedPath));
    if (resolvedPath.empty() || resolvedPath.find('<') == std::string::npos) {
        std::cout << (resolvedPath.empty() ? " (none)\n" : "\n      " + resolvedPath + "\n");
        return;
    }
    mi::base::Handle<mi::neuraylib::IMdl_resolved_resource> resource(
        resolver
            ? resolver->resolve_resource(resolvedPath.c_str(),
                                         nullptr, nullptr, 0, 0, nullptr)
            : nullptr);
    bool printed = false;
    if (resource) {
        for (mi::Size frame = 0; frame < resource->get_count(); ++frame) {
            mi::base::Handle<const mi::neuraylib::IMdl_resolved_resource_element> element(
                resource->get_element(frame));
            if (!element) continue;
            for (mi::Size tile = 0; tile < element->get_count(); ++tile) {
                const char* filename = element->get_filename(tile);
                if (filename && filename[0]) {
                    // auto asset = ArGetResolver().OpenAsset(ArResolvedPath(filename));
                    std::cout << "\n      " << filename;
                    printed = true;
                }
            }
        }
    }
    std::cout << (printed ? "\n" : " (none)\n");
}

} // namespace

int
main(int argc, char** argv)
{
    // A host with its own Neuray can call UsdMdlInitialize(appNeuray) before
    // any Sdr use, then shut down that instance after UsdMdlShutdown().
    if (argc != 3) {
        std::cerr << "usage: inspect_mdl_material <scene.usd> </PrimPath>\n";
        return 2;
    }

    const UsdStageRefPtr stage = UsdStage::Open(argv[1]);
    if (!stage) {
        std::cerr << "cannot open stage: " << argv[1] << '\n';
        return 1;
    }
    const UsdPrim prim = stage->GetPrimAtPath(SdfPath(argv[2]));
    if (!prim) {
        std::cerr << "prim does not exist: " << argv[2] << '\n';
        return 1;
    }

    const UsdShadeMaterial material =
        UsdShadeMaterialBindingAPI(prim).ComputeBoundMaterial();
    if (!material) {
        std::cerr << "prim has no bound material: " << argv[2] << '\n';
        return 1;
    }

    const TfToken mdl("mdl");
    const UsdShadeOutput surface = material.GetSurfaceOutput(mdl);
    const UsdShadeOutput::SourceInfoVector sources = surface.GetConnectedSources();
    if (sources.size() != 1) {
        std::cerr << "material does not have exactly one MDL surface source\n";
        return 1;
    }

    const UsdShadeShader shader(sources.front().source.GetPrim());
    SdfAssetPath sourceAsset;
    TfToken subIdentifier;
    if (!shader
        || !shader.GetSourceAsset(&sourceAsset, mdl)
        || !shader.GetSourceAssetSubIdentifier(&subIdentifier, mdl)) {
        std::cerr << "MDL source asset metadata is incomplete\n";
        return 1;
    }

    if (!UsdMdlInitialize()) {
        std::cerr << "could not initialize the MDL runtime\n";
        return 1;
    }
    if (!PrintMdlSearchPaths()) {
        UsdMdlShutdown();
        return 1;
    }

    const std::vector<SdrShaderNodeConstPtr> nodes =
        UsdMdlAddModuleToRegistry(sourceAsset);
    if (nodes.empty()) {
        std::cerr << "usdMdl could not load " << sourceAsset << '\n';
        UsdMdlShutdown();
        return 1;
    }

    const SdrShaderNodeConstPtr node = shader.GetShaderNodeForSourceType(mdl);
    if (!node) {
        std::cerr << "usdMdl could not find MDL definition "
                  << subIdentifier << " in " << sourceAsset << '\n';
        UsdMdlShutdown();
        return 1;
    }
    mi::base::Handle<mi::neuraylib::IMdl_entity_resolver> entityResolver(
        UsdMdlGetEntityResolver());

    std::cout << "Prim " << prim.GetPath()
              << " uses material " << material.GetPath()
              << " from " << sourceAsset.GetAssetPath()
              << " (MDL definition " << subIdentifier << ")\n";

    std::cout << "Sdr inputs:\n";
    for (const TfToken& name : node->GetShaderInputNames()) {
        const SdrShaderPropertyConstPtr input = node->GetShaderInput(name);
        if (!input) {
            continue;
        }
        const VtValue& defaultValue = input->GetDefaultValue();
        std::cout << "  " << name << ": type=" << input->GetType();
        if (defaultValue.IsHolding<SdfAssetPath>()) {
            const SdfAssetPath& assetPath = defaultValue.UncheckedGet<SdfAssetPath>();
            std::cout << ", authored=" << assetPath.GetAssetPath()
                      << ", resolved=" << assetPath.GetResolvedPath();
        } else {
            std::cout << ", default=" << defaultValue;
        }
        if (!input->GetLabel().IsEmpty()) {
            std::cout << ", label=" << input->GetLabel();
        }
        if (!input->GetHelp().empty()) {
            std::cout << ", help=" << input->GetHelp();
        }
        if (!input->GetPage().IsEmpty()) {
            std::cout << ", page=" << input->GetPage();
        }
        for (const auto& hint : input->GetHints()) {
            std::cout << ", " << hint.first << '=' << hint.second;
        }
        std::cout << '\n';
        if (defaultValue.IsHolding<SdfAssetPath>()) {
            PrintResourceFiles(entityResolver.get(), defaultValue.UncheckedGet<SdfAssetPath>());
        }
    }
    const int exitCode = PrintMdlDefinition(*node) ? 0 : 1;
    entityResolver.reset();
    UsdMdlShutdown();
    return exitCode;
}
