// SPDX-License-Identifier: Apache-2.0

#include "addModuleToRegistry.h"
#include "initialize.h"
#include "mdlModule.h"
#include "tokens.h"

#include <mi/mdl_sdk.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>

#include <filesystem>
#include <map>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE
using namespace mi::neuraylib;

static void TestDefaults()
{
    TfErrorMark errors;
    std::map<std::string, SdrShaderNodeConstPtr> nodes;
    for (const auto* node : UsdMdlAddModuleToRegistry(SdfAssetPath("expression.mdl"))) {
        TF_AXIOM(node && node->IsValid());
        nodes.emplace(node->GetMetadata().at(UsdMdlNodeMetadataTokens->SubIdentifier), node);
    }
    for (const auto* node : UsdMdlAddModuleToRegistry(SdfAssetPath("texture_array_defaults.mdl"))) {
        TF_AXIOM(node && node->IsValid());
        nodes.emplace(node->GetMetadata().at(UsdMdlNodeMetadataTokens->SubIdentifier), node);
    }
    TF_AXIOM(errors.IsClean());
    TF_AXIOM(!nodes.empty());

    mi::base::Handle<mi::neuraylib::INeuray> neuray(UsdMdlGetNeuray());
    mi::base::Handle<IDatabase> database(neuray->get_api_component<IDatabase>());
    mi::base::Handle<IScope> scope(database->get_global_scope());
    mi::base::Handle<ITransaction> transaction(scope->create_transaction());
    TF_AXIOM(transaction);

    struct Case {
        const char* definition;
        const char* input;
        IExpression::Kind kind;
        bool representable = true;
    };
    // Check the SDK representation independently of the converter. This guards
    // against a fixture silently becoming constant after an SDK/compiler update.
    const Case cases[] = {
        {"literal_array", "param", IExpression::EK_CONSTANT},
        {"parameter_array", "param", IExpression::EK_CALL},
        {"state_color", "diffuse_color", IExpression::EK_CALL},
        {"state_color", "highlight_color", IExpression::EK_CONSTANT},
        {"constant_nested", "param", IExpression::EK_CONSTANT},
        {"test_literal_matrix_columns", "param", IExpression::EK_CONSTANT},
        {"test_call_expression", "param", IExpression::EK_CALL},
        {"test_call_expression_with_args", "param", IExpression::EK_CALL},
        {"test_call_expression_array", "param", IExpression::EK_CALL},
        {"test_parameter_expression", "param", IExpression::EK_PARAMETER},
        {"test_multi_parameter_expression", "param", IExpression::EK_PARAMETER},
        {"parameter_nested", "param", IExpression::EK_CALL},
        {"default_boundaries", "zero", IExpression::EK_CONSTANT},
        {"default_boundaries", "folded", IExpression::EK_CONSTANT},
        {"default_boundaries", "referenced", IExpression::EK_PARAMETER},
        {"default_boundaries", "computed", IExpression::EK_CALL},
        {"constant_struct", "param", IExpression::EK_CONSTANT, false},
    };
    for (const auto& test : cases) {
        const auto* node = nodes.at(test.definition);
        const auto* input = node->GetShaderInput(TfToken(test.input));
        TF_AXIOM(input);
        TF_AXIOM(input->GetTypeAsSdfType().HasSdfType());
        TF_AXIOM(!node->GetShaderOutputNames().empty());
        const std::string& dbName =
            node->GetMetadata().at(UsdMdlNodeMetadataTokens->MdlDbDefinitionName);
        mi::base::Handle<const IFunction_definition> definition(
            transaction->access<IFunction_definition>(dbName.c_str()));
        TF_AXIOM(definition);
        TF_AXIOM(node->GetShaderInputNames().size() == definition->get_parameter_count());
        mi::base::Handle<const IExpression_list> defaults(definition->get_defaults());
        mi::base::Handle<const IExpression> expression(defaults->get_expression(test.input));
        TF_AXIOM(expression && expression->get_kind() == test.kind);
        const auto& metadata = input->GetMetadata();
        TF_AXIOM(metadata.at(UsdMdlMetadataTokens->HasDefault) == "true");
        const TfToken expectedKind = test.kind == IExpression::EK_CONSTANT
            ? UsdMdlExpressionKinds->Constant
            : (test.kind == IExpression::EK_CALL
                ? UsdMdlExpressionKinds->Call : UsdMdlExpressionKinds->Parameter);
        TF_AXIOM(metadata.at(UsdMdlMetadataTokens->ExpressionKind) == expectedKind.GetString());
        if (test.kind == IExpression::EK_CONSTANT) {
            TF_AXIOM(metadata.count(UsdMdlMetadataTokens->ExpressionValue) == 0);
        }
        const bool unavailable = test.kind != IExpression::EK_CONSTANT || !test.representable;
        TF_AXIOM(input->GetDefaultValue().IsEmpty() == unavailable);
        TF_AXIOM(input->GetDefaultValueAsSdfType().IsEmpty() == unavailable);
    }
    const auto* boundaries = nodes.at("default_boundaries");
    TF_AXIOM(boundaries->GetShaderInput(TfToken("required"))->GetMetadata()
                 .at(UsdMdlMetadataTokens->HasDefault) == "false");
    for (const auto& name : boundaries->GetShaderOutputNames()) {
        TF_AXIOM(boundaries->GetShaderOutput(name)->GetMetadata()
                     .count(UsdMdlMetadataTokens->HasDefault) == 0);
    }
    TF_AXIOM(boundaries->GetShaderInput(TfToken("required"))->GetDefaultValue().IsEmpty());
    TF_AXIOM(boundaries->GetShaderInput(TfToken("zero"))->GetDefaultValue() == VtValue(0.0f));
    TF_AXIOM(boundaries->GetShaderInput(TfToken("folded"))->GetDefaultValue() == VtValue(6.0f));
    TF_AXIOM(nodes.at("constant_nested")->GetShaderInput(TfToken("param"))->GetDefaultValue()
             == VtValue(GfVec3f(0.1f, 0.2f, 0.3f)));
    TF_AXIOM(transaction->commit() == 0);
    TF_AXIOM(errors.IsClean());
}

static void TestBorrowedInspection()
{
    // Uncommitted content distinguishes the supplied transaction from any
    // accidental fresh transaction inside selection, conversion, or resources.
    // This is a visibility test; production loads commit before inspection.
    namespace fs = std::filesystem;
    fs::copy_file("mdl/resources/one.png", "mdl/resources/inspection_one.png",
                  fs::copy_options::overwrite_existing);
    fs::copy_file("mdl/resources/two.png", "mdl/resources/inspection_two.png",
                  fs::copy_options::overwrite_existing);
    const char* source = R"mdl(
mdl 1.8;
import ::state::*;
export float3 snapshot(
    float3 direction = state::texture_tangent_v(7),
    uniform texture_2d first = texture_2d("/resources/inspection_one.png"),
    uniform texture_2d second = texture_2d("/resources/inspection_two.png"),
    uniform texture_2d[2] textures = texture_2d[](
        texture_2d("/resources/inspection_one.png"),
        texture_2d("/resources/inspection_two.png")))
{ return direction; }
)mdl";
    mi::base::Handle<mi::neuraylib::INeuray> neuray(UsdMdlGetNeuray());
    mi::base::Handle<IDatabase> database(neuray->get_api_component<IDatabase>());
    mi::base::Handle<IScope> scope(database->get_global_scope());
    mi::base::Handle<IMdl_impexp_api> impexp(neuray->get_api_component<IMdl_impexp_api>());
    mi::base::Handle<ITransaction> transaction(scope->create_transaction());
    TF_AXIOM(impexp->load_module_from_string(
        transaction.get(), "::inspection_snapshot", source) == 0);

    {
        mi::base::Handle<const IModule> module(
            transaction->access<IModule>("mdl::inspection_snapshot"));
        TF_AXIOM(module && module->get_function_count() == 1);
        mi::base::Handle<const IFunction_definition> definition(
            transaction->access<IFunction_definition>(module->get_function(0)));
        mi::base::Handle<const IExpression_list> defaults(definition->get_defaults());
        mi::base::Handle<const IExpression_call> call(
            defaults->get_expression<IExpression_call>("direction"));
        mi::base::Handle<const IExpression_constant> constant(
            defaults->get_expression<IExpression_constant>("first"));
        mi::base::Handle<const IValue_texture> texture(constant->get_value<IValue_texture>());
        TF_AXIOM(call && texture && texture->get_value());
        mi::base::Handle<ITransaction> observer(scope->create_transaction());
        {
            mi::base::Handle<const IModule> absentModule(
                observer->access<IModule>("mdl::inspection_snapshot"));
            mi::base::Handle<const IFunction_call> absentCall(
                observer->access<IFunction_call>(call->get_call()));
            mi::base::Handle<const ITexture> absentTexture(
                observer->access<ITexture>(texture->get_value()));
            TF_AXIOM(!absentModule && !absentCall && !absentTexture);
        }
        observer->abort();
    }

    mi::base::Handle<IMdl_entity_resolver> resolver(UsdMdlGetEntityResolver());
    // Prevent filesystem fallback from hiding an incorrect resource DB lookup.
    // The SDK has loaded these private copies and retains their filenames.
    for (const char* suffix : {"one", "two"}) {
        const std::string resourcePath = std::string("/resources/inspection_") + suffix + ".png";
        TF_AXIOM(fs::remove("mdl" + resourcePath));
        mi::base::Handle<IMdl_resolved_resource> missing(
            resolver->resolve_resource(
                resourcePath.c_str(), nullptr, "::inspection_snapshot", 0, 0, nullptr));
        TF_AXIOM(!missing);
    }

    SdrShaderNodeUniquePtr node;
    {
        MdlModule module("::inspection_snapshot", SdfAssetPath("inspection_snapshot.mdl"),
                         transaction.get());
        TF_AXIOM(module.IsValid());
        SdrShaderNodeDiscoveryResult result(
            TfToken("inspection_snapshot"), SdrVersion().GetAsDefault(), "snapshot", TfToken(),
            TfToken("mdl"), TfToken("mdl"), "inspection_snapshot.mdl", "",
            std::string(), SdrTokenMap(), std::string(), TfToken("missing"));
        const auto missing = module.GetSdrFromDiscoveryResult(result);
        TF_AXIOM(!missing || !missing->IsValid());
        TF_AXIOM(transaction->is_open());
        result.subIdentifier = TfToken("snapshot");
        node = module.GetSdrFromDiscoveryResult(result);
        TF_AXIOM(node && node->IsValid());
        TF_AXIOM(transaction->is_open());
    }
    transaction->abort();
    TF_AXIOM(!transaction->is_open());

    // Nodes must own their USD data even after the inspected DB content is gone.
    TF_AXIOM(node->GetShaderInput(TfToken("direction"))->GetMetadata()
                 .at(UsdMdlMetadataTokens->ExpressionValue)
             == "mdl::state::texture_tangent_v(int)");
    const auto* array = node->GetShaderInput(TfToken("textures"));
    TF_AXIOM(array->GetDefaultValue().IsHolding<VtArray<SdfAssetPath>>());
    const auto& paths = array->GetDefaultValue().UncheckedGet<VtArray<SdfAssetPath>>();
    TF_AXIOM(paths.size() == 2);
    for (size_t i = 0; i < paths.size(); ++i) {
        const std::string suffix = i == 0 ? "one" : "two";
        TF_AXIOM(paths[i].GetAssetPath() == "/resources/inspection_" + suffix + ".png");
        TF_AXIOM(fs::path(paths[i].GetResolvedPath()).lexically_normal()
                 == fs::absolute("mdl/resources/inspection_" + suffix + ".png").lexically_normal());
        const auto* scalar = node->GetShaderInput(TfToken(i == 0 ? "first" : "second"));
        TF_AXIOM(scalar->GetDefaultValue().Get<SdfAssetPath>() == paths[i]);
    }
    mi::base::Handle<ITransaction> observer(scope->create_transaction());
    {
        mi::base::Handle<const IModule> absent(
            observer->access<IModule>("mdl::inspection_snapshot"));
        TF_AXIOM(!absent);
    }
    observer->abort();
}

int main()
{
    TestDefaults();
    TestBorrowedInspection();
    UsdMdlShutdown();
    return 0;
}
