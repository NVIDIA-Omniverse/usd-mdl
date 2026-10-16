//
// SPDX-License-Identifier: Apache-2.0
//

// Exercise public Sdr-to-MDL name and enum binding with actual SDK calls.
// This checks argument identity and values, without compiling or rendering.
#include "addModuleToRegistry.h"
#include "enumValue.h"
#include "initialize.h"
#include "shaderDefinitions.h"
#include "tokens.h"
#include "test_exit.h"

#include <mi/mdl_sdk.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
using namespace mi::neuraylib;
int g_failures = 0;

bool Check(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "[testMdlShaderDefinitionBinding] FAIL: %s\n", message);
        ++g_failures;
    }
    return condition;
}

SdrShaderNodeConstPtr FindNode(const std::vector<SdrShaderNodeConstPtr>& nodes,
                              const char* identifier)
{
    for (const auto* node : nodes) {
        if (node && node->GetIdentifier() == TfToken(identifier)) {
            return node;
        }
    }
    Check(false, "registered shader definition has the requested identifier");
    return nullptr;
}

mi::base::Handle<const IFunction_definition> AccessDefinition(
    ITransaction* transaction, SdrShaderNodeConstPtr node)
{
    if (!node) {
        return {};
    }
    const auto& metadata = node->GetMetadata();
    const auto dbName = metadata.find(UsdMdlNodeMetadataTokens->MdlDbDefinitionName);
    if (!Check(dbName != metadata.end(), "node retains the original SDK definition DB name")) {
        return {};
    }
    mi::base::Handle<const IFunction_definition> definition(
        transaction->access<IFunction_definition>(dbName->second.c_str()));
    Check(static_cast<bool>(definition), "metadata names an accessible SDK definition");
    return definition;
}

mi::base::Handle<const IType_enum> GetEnumType(
    const IFunction_definition* definition, const char* parameter)
{
    const mi::base::Handle<const IType_list> types(definition->get_parameter_types());
    const mi::base::Handle<const IType> type(types->get_type(parameter));
    const mi::base::Handle<const IType> unaliased(type ? type->skip_all_type_aliases() : nullptr);
    if (!Check(static_cast<bool>(unaliased), "original MDL parameter type exists")) {
        return {};
    }
    const auto enumType = unaliased.get_interface<const IType_enum>();
    Check(static_cast<bool>(enumType), "original MDL parameter retains its enum type");
    return enumType;
}

mi::base::Handle<const IValue> ReadCallArgument(const IFunction_call* call, const char* parameter)
{
    const mi::base::Handle<const IExpression_list> arguments(call->get_arguments());
    const mi::base::Handle<const IExpression> expression(arguments->get_expression(parameter));
    const mi::base::Handle<const IExpression_constant> constant(
        expression ? expression->get_interface<const IExpression_constant>() : nullptr);
    if (!Check(static_cast<bool>(constant), "SDK call retains a constant authored argument")) {
        return {};
    }
    return mi::base::Handle<const IValue>(constant->get_value());
}

mi::base::Handle<IFunction_call> CreateCall(
    const IFunction_definition* definition, IExpression_factory* factory,
    const std::vector<std::pair<std::string, IValue*>>& values)
{
    mi::base::Handle<IExpression_list> arguments(factory->create_expression_list());
    for (const auto& argument : values) {
        const mi::base::Handle<IExpression_constant> expression(factory->create_constant(argument.second));
        if (!Check(static_cast<bool>(expression), "SDK creates the authored argument expression")
            || !Check(arguments->add_expression(argument.first.c_str(), expression.get()) == 0,
                      "SDK accepts the original implementation parameter name")) {
            return {};
        }
    }
    mi::Sint32 error = -999;
    mi::base::Handle<IFunction_call> call(definition->create_function_call(arguments.get(), &error));
    Check(call && error == 0, "SDK creates a type-correct call with the mapped arguments");
    return call;
}

void CheckEnum(const IValue_enum* value, const char* name, int code)
{
    if (Check(value != nullptr, "enum binding returns an SDK enum value")) {
        Check(value->get_name() && std::string(value->get_name()) == name,
              "enum binding preserves the requested member name");
        Check(value->get_value() == code, "enum binding uses the declared integer code");
    }
}

void CheckCallEnum(const IFunction_call* call, const char* parameter, const char* name, int code)
{
    if (!call) {
        return;
    }
    const auto value = ReadCallArgument(call, parameter);
    const mi::base::Handle<const IValue_enum> enumValue(
        value ? value->get_interface<const IValue_enum>() : nullptr);
    CheckEnum(enumValue.get(), name, code);
}

void CheckTransform(ITransaction* transaction, IValue_factory* values,
                    IExpression_factory* expressions, SdrShaderNodeConstPtr node)
{
    const auto definition = AccessDefinition(transaction, node);
    if (!definition) {
        return;
    }
    Check(definition->get_mdl_simple_name() && std::string(definition->get_mdl_simple_name()) == "PreviewTransform2d",
          "transform metadata selects the original MDL definition");
    const auto input = node->GetShaderInput(TfToken("in"));
    if (!Check(input != nullptr, "USD transform exposes inputs:in")) {
        return;
    }
    Check(input->GetImplementationName() == "st", "USD input in binds to MDL parameter st");
    const mi::base::Handle<const IType_list> types(definition->get_parameter_types());
    const std::string implementationName = input->GetImplementationName();
    const mi::base::Handle<const IType> type(types->get_type(implementationName.c_str()));
    const mi::base::Handle<const IType> unaliased(type ? type->skip_all_type_aliases() : nullptr);
    const mi::base::Handle<const IType_vector> vectorType(
        unaliased ? unaliased->get_interface<const IType_vector>() : nullptr);
    if (!Check(vectorType && vectorType->get_size() == 2, "st has the original MDL float2 vector shape")) {
        return;
    }
    const mi::base::Handle<const IType_atomic> elementType(vectorType->get_element_type());
    Check(elementType->get_kind() == IType::TK_FLOAT, "st retains float precision");
    mi::base::Handle<IValue_vector> argument(values->create_vector(vectorType.get()));
    const mi::base::Handle<IValue_float> first(values->create_float(0.25f));
    const mi::base::Handle<IValue_float> second(values->create_float(0.75f));
    if (!Check(argument && argument->set_value(0, first.get()) == 0
               && argument->set_value(1, second.get()) == 0, "SDK builds a non-default transform input")) {
        return;
    }
    {
        const mi::base::Handle<IExpression_constant> expression(expressions->create_constant(argument.get()));
        const mi::base::Handle<IExpression_list> wrongArguments(expressions->create_expression_list());
        Check(wrongArguments->add_expression("in", expression.get()) == 0,
              "expression list can contain the exposed USD alias");
        mi::Sint32 error = 0;
        const mi::base::Handle<IFunction_call> wrongCall(
            definition->create_function_call(wrongArguments.get(), &error));
        Check(!wrongCall && error != 0, "SDK rejects the USD alias when implementation-name mapping is omitted");
    }
    const auto call = CreateCall(definition.get(), expressions, {{implementationName, argument.get()}});
    if (!call) {
        return;
    }
    const auto retained = ReadCallArgument(call.get(), "st");
    const mi::base::Handle<const IValue_vector> vector(
        retained ? retained->get_interface<const IValue_vector>() : nullptr);
    if (!Check(static_cast<bool>(vector), "SDK call uses st rather than the USD alias in")) {
        return;
    }
    const mi::base::Handle<const IValue> firstValue(vector->get_value(0));
    const mi::base::Handle<const IValue> secondValue(vector->get_value(1));
    const mi::base::Handle<const IValue_float> firstFloat(firstValue->get_interface<const IValue_float>());
    const mi::base::Handle<const IValue_float> secondFloat(secondValue->get_interface<const IValue_float>());
    Check(firstFloat && secondFloat && firstFloat->get_value() == 0.25f && secondFloat->get_value() == 0.75f,
          "SDK call retains both authored transform coordinates");
}

void CheckTexture(ITransaction* transaction, IValue_factory* values,
                  IExpression_factory* expressions, SdrShaderNodeConstPtr node)
{
    const auto definition = AccessDefinition(transaction, node);
    if (!definition) {
        return;
    }
    Check(definition->get_mdl_simple_name() && std::string(definition->get_mdl_simple_name()) == "PreviewTexture",
          "texture metadata selects the original MDL definition");
    const auto input = node->GetShaderInput(TfToken("wrapS"));
    if (!Check(input != nullptr, "USD texture exposes wrapS")) {
        return;
    }
    Check(input->GetTypeAsSdfType().GetSdfType() == SdfValueTypeNames->Token,
          "USD wrapS uses token storage");
    const VtValue usdDefault = input->GetDefaultValueAsSdfType();
    if (!Check(usdDefault.IsHolding<TfToken>() && usdDefault.UncheckedGet<TfToken>() == TfToken("useMetadata"),
               "proxy default retains the MDL useMetadata enum member")) {
        return;
    }
    const std::string implementationName = input->GetImplementationName();
    const auto type = GetEnumType(definition.get(), implementationName.c_str());
    if (!type) {
        return;
    }
    const mi::base::Handle<IValue_enum> defaultValue(UsdMdlCreateEnumValue(values, type.get(), usdDefault));
    CheckEnum(defaultValue.get(), "useMetadata", 20);
    const mi::base::Handle<IValue_enum> authored(
        UsdMdlCreateEnumValue(values, type.get(), VtValue(TfToken("repeat"))));
    CheckEnum(authored.get(), "repeat", 7);
    if (authored) {
        const auto call = CreateCall(definition.get(), expressions, {{implementationName, authored.get()}});
        CheckCallEnum(call.get(), implementationName.c_str(), "repeat", 7);
        CheckCallEnum(call.get(), "wrapT", "useMetadata", 20);
    }
    Check(!mi::base::Handle<IValue_enum>(UsdMdlCreateEnumValue(values, type.get(), VtValue(TfToken("unknown")))),
          "unknown enum token is rejected");
    Check(!mi::base::Handle<IValue_enum>(UsdMdlCreateEnumValue(values, type.get(), VtValue(1))),
          "integer enum values are codes rather than member positions");
    Check(!mi::base::Handle<IValue_enum>(UsdMdlCreateEnumValue(values, type.get(), VtValue(7.0f))),
          "floating-point enum values are rejected");
    Check(!mi::base::Handle<IValue_enum>(UsdMdlCreateEnumValue(values, type.get(), VtValue())),
          "empty enum values are rejected");
    Check(!mi::base::Handle<IValue_enum>(UsdMdlCreateEnumValue(nullptr, type.get(), VtValue(TfToken("repeat")))),
          "null value factory is rejected");
    Check(!mi::base::Handle<IValue_enum>(UsdMdlCreateEnumValue(values, nullptr, VtValue(TfToken("repeat")))),
          "null enum type is rejected");
}

void CheckSurface(ITransaction* transaction, IValue_factory* values,
                  IExpression_factory* expressions, SdrShaderNodeConstPtr node)
{
    const auto definition = AccessDefinition(transaction, node);
    if (!definition) {
        return;
    }
    Check(definition->is_material() && definition->get_mdl_simple_name()
          && std::string(definition->get_mdl_simple_name()) == "PreviewSurface",
          "surface proxy retains the original material definition");
    const auto type = GetEnumType(definition.get(), "opacityMode");
    if (!type) {
        return;
    }
    const mi::base::Handle<IValue_enum> authored(
        UsdMdlCreateEnumValue(values, type.get(), VtValue(std::string("presence"))));
    CheckEnum(authored.get(), "presence", 9);
    if (authored) {
        const auto call = CreateCall(definition.get(), expressions, {{"opacityMode", authored.get()}});
        CheckCallEnum(call.get(), "opacityMode", "presence", 9);
    }
}

void CheckSparse(ITransaction* transaction, IValue_factory* values,
                 IExpression_factory* expressions, const std::vector<SdrShaderNodeConstPtr>& nodes)
{
    SdrShaderNodeConstPtr node = nullptr;
    for (const auto* candidate : nodes) {
        const auto candidateDefinition = AccessDefinition(transaction, candidate);
        if (candidateDefinition && candidateDefinition->get_mdl_simple_name()
            && std::string(candidateDefinition->get_mdl_simple_name()) == "test_metadata_tokens") {
            node = candidate;
            break;
        }
    }
    if (!Check(node != nullptr, "ordinary MDL metadata fixture is registered")) {
        return;
    }
    const auto definition = AccessDefinition(transaction, node);
    if (!definition) {
        return;
    }
    Check(definition->get_mdl_simple_name() && std::string(definition->get_mdl_simple_name()) == "test_metadata_tokens",
          "ordinary enum metadata selects the original MDL definition");
    const auto tokenInput = node->GetShaderInput(TfToken("enum_token"));
    const auto integerInput = node->GetShaderInput(TfToken("enum_integer"));
    if (!Check(tokenInput && integerInput, "ordinary MDL exposes both annotated and native enum inputs")) {
        return;
    }
    const auto type = GetEnumType(definition.get(), "enum_token");
    if (!type) {
        return;
    }
    const VtValue tokenDefault = tokenInput->GetDefaultValueAsSdfType();
    Check(tokenDefault.IsHolding<TfToken>() && tokenDefault.UncheckedGet<TfToken>() == TfToken("middle"),
          "symbolic default preserves the compiled SDK member for duplicate enum codes");
    const mi::base::Handle<IValue_enum> mappedDefault(UsdMdlCreateEnumValue(values, type.get(), tokenDefault));
    CheckEnum(mappedDefault.get(), "middle", 8);
    const mi::base::Handle<IValue_enum> alias(UsdMdlCreateEnumValue(values, type.get(), VtValue(TfToken("alias"))));
    CheckEnum(alias.get(), "alias", 8);
    const VtValue integerDefault = integerInput->GetDefaultValue();
    Check(integerInput->GetTypeAsSdfType().GetSdfType() == SdfValueTypeNames->Int
          && integerDefault.IsHolding<int>() && integerDefault.UncheckedGet<int>() == 8,
          "ordinary MDL enum retains integer USD storage and its declared code");
    const mi::base::Handle<IValue_enum> integer(UsdMdlCreateEnumValue(values, type.get(), integerDefault));
    CheckEnum(integer.get(), "middle", 8);
    const mi::base::Handle<IValue_enum> last(UsdMdlCreateEnumValue(values, type.get(), VtValue(TfToken("last"))));
    const mi::base::Handle<IValue_enum> negative(UsdMdlCreateEnumValue(values, type.get(), VtValue(-3)));
    CheckEnum(last.get(), "last", 42);
    CheckEnum(negative.get(), "negative", -3);
    if (mappedDefault && integer) {
        const auto call = CreateCall(definition.get(), expressions,
                                     {{"enum_token", mappedDefault.get()}, {"enum_integer", integer.get()}});
        CheckCallEnum(call.get(), "enum_token", "middle", 8);
        CheckCallEnum(call.get(), "enum_integer", "middle", 8);
    }
    if (alias && integer) {
        const auto call = CreateCall(definition.get(), expressions,
                                     {{"enum_token", alias.get()}, {"enum_integer", integer.get()}});
        CheckCallEnum(call.get(), "enum_token", "alias", 8);
        CheckCallEnum(call.get(), "enum_integer", "middle", 8);
    }
    if (last && negative) {
        const auto call = CreateCall(definition.get(), expressions,
                                     {{"enum_token", last.get()}, {"enum_integer", negative.get()}});
        CheckCallEnum(call.get(), "enum_token", "last", 42);
        CheckCallEnum(call.get(), "enum_integer", "negative", -3);
    }
}

void Run(const std::filesystem::path& mdlDirectory, const std::filesystem::path& definitions)
{
    const auto module = mdlDirectory / "preview_surface.mdl";
    const auto metadataModule = mdlDirectory / "sdr_metadata.mdl";
    const auto nodes = UsdMdlAddShaderDefinitionsToRegistry(
        SdfAssetPath(definitions.generic_string()), SdfAssetPath(module.generic_string()));
    const auto metadataNodes = UsdMdlAddModuleToRegistry(SdfAssetPath(metadataModule.generic_string()));
    const auto transform = FindNode(nodes, "UsdMdlFixturePreviewTransform2d");
    const auto texture = FindNode(nodes, "UsdMdlFixturePreviewTexture");
    const auto surface = FindNode(nodes, "UsdMdlFixturePreviewSurface");

    mi::base::Handle<INeuray> neuray(UsdMdlGetNeuray());
    if (!Check(static_cast<bool>(neuray), "public runtime accessor exposes the live SDK")) {
        return;
    }
    mi::base::Handle<IDatabase> database(neuray->get_api_component<IDatabase>());
    if (!Check(static_cast<bool>(database), "public SDK exposes its database")) {
        return;
    }
    mi::base::Handle<IScope> scope(database->get_global_scope());
    if (!Check(static_cast<bool>(scope), "SDK exposes the global scope")) {
        return;
    }
    mi::base::Handle<ITransaction> transaction(scope->create_transaction());
    if (!Check(static_cast<bool>(transaction), "consumer can open its own SDK transaction")) {
        return;
    }
    {
        const mi::base::Handle<IMdl_factory> factory(neuray->get_api_component<IMdl_factory>());
        if (!Check(static_cast<bool>(factory), "public SDK exposes its MDL factory")) {
            transaction->abort();
            return;
        }
        const mi::base::Handle<IValue_factory> values(factory->create_value_factory(transaction.get()));
        const mi::base::Handle<IExpression_factory> expressions(factory->create_expression_factory(transaction.get()));
        if (Check(values && expressions, "SDK creates transaction-bound value and expression factories")) {
            CheckTransform(transaction.get(), values.get(), expressions.get(), transform);
            CheckTexture(transaction.get(), values.get(), expressions.get(), texture);
            CheckSurface(transaction.get(), values.get(), expressions.get(), surface);
            CheckSparse(transaction.get(), values.get(), expressions.get(), metadataNodes);
        }
    }
    // Calls, definitions, types, values and factories have all released their
    // transaction references before closing this consumer-owned transaction.
    transaction->abort();
}
}

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "Usage: testMdlShaderDefinitionBinding <fixture-mdl-directory> <shader-definitions.usda>\n");
        return 2;
    }
    if (!Check(UsdMdlInitialize(), "public initialization starts the runtime")) {
        UsdMdlShutdown();
        mdl_test::FlushAndExit(1);
    }
    Run(std::filesystem::absolute(std::filesystem::path(argv[1])),
        std::filesystem::absolute(std::filesystem::path(argv[2])));
    UsdMdlShutdown();
    std::printf("[testMdlShaderDefinitionBinding] %s\n", g_failures ? "FAILED" : "PASSED");
    std::fflush(stdout);
    std::fflush(stderr);
    mdl_test::FlushAndExit(g_failures ? 1 : 0);
}
