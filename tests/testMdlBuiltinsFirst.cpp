// SPDX-License-Identifier: Apache-2.0

#include "initialize.h"
#include "tokens.h"

#include <mi/mdl_sdk.h>
#include <pxr/base/arch/env.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>

#include <initializer_list>
#include <string>
#include <utility>

PXR_NAMESPACE_USING_DIRECTIVE
using namespace mi::neuraylib;

static void TestEnum(ITransaction* transaction, const char* signature,
                     const char* parameter, const char* symbol,
                     const std::initializer_list<const char*>& values)
{
    const auto* node = SdrRegistry::GetInstance().GetShaderNodeFromAsset(
        SdfAssetPath("mdl-builtin.mdl"), SdrTokenMap(), TfToken(signature), UsdMdlTokens->Mdl);
    TF_AXIOM(node && node->IsValid());
    const std::string expectedDb = std::string("mdl::") + signature;
    TF_AXIOM(node->GetMetadata().at(UsdMdlNodeMetadataTokens->MdlDbDefinitionName) == expectedDb);
    mi::base::Handle<const IFunction_definition> definition(
        transaction->access<IFunction_definition>(expectedDb.c_str()));
    TF_AXIOM(definition);
    mi::base::Handle<const IType_list> parameters(definition->get_parameter_types());
    mi::base::Handle<const IType> type(parameters->get_type(parameter));
    mi::base::Handle<const IType> underlying(type->skip_all_type_aliases());
    mi::base::Handle<const IType_enum> enumeration(underlying->get_interface<IType_enum>());
    TF_AXIOM(enumeration && std::string(enumeration->get_symbol()) == symbol);
    TF_AXIOM(enumeration->get_size() == values.size());
    const auto* property = node->GetShaderInput(TfToken(parameter));
    TF_AXIOM(property && property->GetOptions().size() == values.size());
    mi::Size index = 0;
    for (const char* name : values) {
        TF_AXIOM(std::string(enumeration->get_value_name(index)) == name);
        TF_AXIOM(enumeration->get_value_code(index) == static_cast<mi::Sint32>(index));
        TF_AXIOM(property->GetOptions()[index] == SdrOption(TfToken(name), TfToken(std::to_string(index))));
        ++index;
    }
}

static void TestFirstModule()
{
    mi::base::Handle<mi::neuraylib::INeuray> neuray(UsdMdlGetNeuray());
    mi::base::Handle<IDatabase> database(neuray->get_api_component<IDatabase>());
    mi::base::Handle<IScope> scope(database->get_global_scope());
    {
        mi::base::Handle<ITransaction> transaction(scope->create_transaction());
        mi::base::Handle<const IModule> module(
            transaction->access<IModule>("mdl::%3Cbuiltins%3E"));
        // Catch the assumption that starting Neuray already loads this module.
        TF_AXIOM(!module);
        transaction->commit();
    }

    // This is the first requested MDL module, with no dummy or library roots.
    const auto* node = SdrRegistry::GetInstance().GetShaderNodeFromAsset(
        SdfAssetPath("mdl-builtin.mdl"), SdrTokenMap(), TfToken("float(float)"),
        UsdMdlTokens->Mdl);
    TF_AXIOM(node && node->IsValid());
    const auto& metadata = node->GetMetadata();
    TF_AXIOM(metadata.at(UsdMdlNodeMetadataTokens->ModuleUsdIdentifier) == "mdl-builtin.mdl");
    TF_AXIOM(metadata.at(UsdMdlNodeMetadataTokens->SubIdentifier) == "float(float)");
    TF_AXIOM(metadata.at(UsdMdlNodeMetadataTokens->MdlDbModuleName) == "mdl::%3Cbuiltins%3E");
    TF_AXIOM(metadata.at(UsdMdlNodeMetadataTokens->MdlDbDefinitionName) == "mdl::float(float)");
    TF_AXIOM(metadata.at(UsdMdlNodeMetadataTokens->IsEmbeddedModule) == "true");
    TF_AXIOM(node->GetResolvedImplementationURI().empty());
    TF_AXIOM(node->GetResolvedDefinitionURI().empty());

    // Use a new transaction to verify the loader committed real SDK elements.
    mi::base::Handle<ITransaction> transaction(scope->create_transaction());
    {
        mi::base::Handle<const IModule> module(transaction->access<IModule>(
            metadata.at(UsdMdlNodeMetadataTokens->MdlDbModuleName).c_str()));
        mi::base::Handle<const IFunction_definition> definition(
            transaction->access<IFunction_definition>(
                metadata.at(UsdMdlNodeMetadataTokens->MdlDbDefinitionName).c_str()));
        TF_AXIOM(module && definition);
        TF_AXIOM(std::string(module->get_mdl_name()) == "::%3Cbuiltins%3E");
        TF_AXIOM(std::string(definition->get_mdl_module_name()) == module->get_mdl_name());
        TF_AXIOM(std::string(definition->get_mdl_name()) == "float(float)");
        TF_AXIOM(definition->get_parameter_count() == 1);
        TF_AXIOM(std::string(definition->get_mdl_parameter_type_name(0)) == "float");
    }
    transaction->commit();

    // Independently check SDK enum identities and values, rather than deriving
    // the expected spelling/options from the plugin's own metadata.
    transaction = mi::base::Handle<ITransaction>(scope->create_transaction());
    for (const auto& entry : {std::make_pair("intensity_mode(intensity_mode)", "value"),
                              std::make_pair("int(intensity_mode)", "value"),
                              std::make_pair("material_emission(edf,color,intensity_mode)", "mode")}) {
        TestEnum(transaction.get(), entry.first, entry.second, "::intensity_mode",
                 {"intensity_radiant_exitance", "intensity_power"});
    }
    TestEnum(transaction.get(), "texture_2d(string,::tex::gamma_mode,string)", "gamma",
             "::tex::gamma_mode", {"gamma_default", "gamma_linear", "gamma_srgb"});
    transaction->commit();

    // The alias must not bypass the ordinary exact-signature selection rules.
    for (const char* subId : {"float", "float(float,float)",
                            "intensity_mode(::intensity_mode)", "int(::intensity_mode)",
                            "texture_2d(string,gamma_mode,string)"}) {
        TfErrorMark errors;
        const auto* rejected = SdrRegistry::GetInstance().GetShaderNodeFromAsset(
            SdfAssetPath("mdl-builtin.mdl"), SdrTokenMap(), TfToken(subId),
            UsdMdlTokens->Mdl);
        TF_AXIOM(!rejected || !rejected->IsValid());
        TF_AXIOM(!errors.IsClean());
        errors.Clear();
    }
}

int main()
{
    ArchSetEnv("USD_MDL_DISCOVER_ON_STARTUP", "0", true);
    ArchSetEnv("USD_MDL_SEARCH_PATHS", "", true);
    ArchSetEnv("USD_MDL_ADD_USER_PATHS", "0", true);
    ArchSetEnv("USD_MDL_ADD_SYSTEM_PATHS", "0", true);
    ArchSetEnv("USD_MDL_ALLOW_FIRST_OVERLOAD", "0", true);
    ArchRemoveEnv("MDL_USER_PATH");
    ArchRemoveEnv("MDL_SYSTEM_PATH");
    ArchRemoveEnv("PXR_AR_DEFAULT_SEARCH_PATH");
    TF_AXIOM(!mi::base::Handle<mi::neuraylib::INeuray>(UsdMdlGetNeuray()).get());
    TF_AXIOM(UsdMdlInitialize());
    TestFirstModule();
    UsdMdlShutdown();
    return 0;
}
