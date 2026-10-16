// SPDX-License-Identifier: Apache-2.0

// Characterize path-contract gaps before changing resolver behavior. Explicit
// expectations for known limitations should change alongside their fixes.

#include "initialize.h"
#include "mdlModule.h"
#include <mi/mdl_sdk.h>
#include <mi/neuraylib/bsdf_isotropic_data.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE
using namespace mi::neuraylib;
template<class T> using MdlHandle = mi::base::Handle<T>;
namespace fs = std::filesystem;

namespace {

void Write(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << text;
    TF_AXIOM(file.good());
}

std::string Text(const char* value) { return value ? value : ""; }

std::string Read(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    TF_AXIOM(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void CheckFile(const std::string& resolved, const fs::path& expected)
{
    TF_AXIOM(!resolved.empty() && fs::equivalent(resolved, expected));
    const auto asset = ArGetResolver().OpenAsset(ArResolvedPath(resolved));
    TF_AXIOM(asset);
    const auto buffer = asset->GetBuffer();
    TF_AXIOM(buffer && std::string(buffer.get(), asset->GetSize()) == Read(expected));
}

// Retain the exact inputs seen by the external resolver, not just the source
// spelling: the compiler may reject or normalize a request before calling us.
class RecordingResolver final : public mi::base::Interface_implement<IMdl_entity_resolver>
{
public:
    explicit RecordingResolver(IMdl_entity_resolver* delegate)
        : _delegate(delegate, mi::base::DUP_INTERFACE) {}
    std::vector<std::string> modules, resources;
    IMdl_resolved_module* resolve_module(const char* name, const char* file,
        const char* owner, mi::Sint32 line, mi::Sint32 column,
        IMdl_execution_context* context) override
    {
        modules.emplace_back(Text(name));
        return _delegate->resolve_module(name, file, owner, line, column, context);
    }
    IMdl_resolved_resource* resolve_resource(const char* path, const char* file,
        const char* owner, mi::Sint32 line, mi::Sint32 column,
        IMdl_execution_context* context) override
    {
        resources.emplace_back(Text(path));
        return _delegate->resolve_resource(path, file, owner, line, column, context);
    }
private:
    MdlHandle<IMdl_entity_resolver> _delegate;
};

struct Runtime
{
    MdlHandle<IMdl_factory> factory;
    MdlHandle<IMdl_impexp_api> impexp;
    MdlHandle<IMdl_configuration> configuration;
    MdlHandle<IScope> scope;
    fs::path root = fs::absolute("mdl");
    Runtime()
    {
        TF_AXIOM(UsdMdlInitialize());
        mi::base::Handle<mi::neuraylib::INeuray> neuray(UsdMdlGetNeuray());
        factory = MdlHandle<IMdl_factory>(neuray->get_api_component<IMdl_factory>());
        impexp = MdlHandle<IMdl_impexp_api>(neuray->get_api_component<IMdl_impexp_api>());
        configuration = MdlHandle<IMdl_configuration>(neuray->get_api_component<IMdl_configuration>());
        MdlHandle<IDatabase> database(neuray->get_api_component<IDatabase>());
        scope = MdlHandle<IScope>(database->get_global_scope());
    }
};

void PrintMessages(IMdl_execution_context* context)
{
    for (mi::Size i = 0; i < context->get_messages_count(); ++i) {
        MdlHandle<const IMessage> message(context->get_message(i));
        std::printf("  diagnostic %d: %s\n", message->get_code(), message->get_string());
    }
}

// Every case uses a fresh transaction; an earlier successful import must not
// allow a later case to bypass the resolver through the SDK database cache.
void Compile(Runtime& runtime, RecordingResolver* recording, const std::string& name,
             bool success, const char* resourcePath = nullptr, const char* expectedMdlPath = nullptr,
             int expectedError = 0)
{
    recording->modules.clear();
    recording->resources.clear();
    MdlHandle<ITransaction> transaction(runtime.scope->create_transaction());
    {
        MdlHandle<IMdl_execution_context> context(runtime.factory->create_execution_context());
        const auto rc = runtime.impexp->load_module(transaction.get(), name.c_str(), context.get());
        std::printf("COMPILE %s rc=%d\n", name.c_str(), rc);
        for (const auto& path : recording->modules) std::printf("  module %s\n", path.c_str());
        for (const auto& path : recording->resources) std::printf("  resource %s\n", path.c_str());
        PrintMessages(context.get());
        TF_AXIOM((rc == 0) == success);
        if (success) TF_AXIOM(context->get_error_messages_count() == 0);
        if (expectedError) {
            bool found = false;
            for (mi::Size i = 0; i < context->get_error_messages_count(); ++i) {
                MdlHandle<const IMessage> message(context->get_error_message(i));
                found |= message->get_code() == expectedError;
            }
            TF_AXIOM(found);
        }
        if (resourcePath) {
            TF_AXIOM(std::find(recording->resources.begin(), recording->resources.end(),
                               resourcePath) != recording->resources.end());
        }
        if (success && resourcePath) {
            MdlHandle<const mi::IString> dbName(runtime.factory->get_db_module_name(name.c_str()));
            MdlHandle<const IModule> module(transaction->access<IModule>(dbName->get_c_str()));
            TF_AXIOM(module && module->get_function_count() == 1);
            MdlHandle<const IFunction_definition> definition(
                transaction->access<IFunction_definition>(module->get_function(0)));
            MdlHandle<const IExpression_list> defaults(definition->get_defaults());
            MdlHandle<const IExpression_constant> expression(defaults->get_expression<IExpression_constant>("tex"));
            MdlHandle<const IValue_texture> value(expression->get_value<IValue_texture>());
            std::printf("  compiled value mdl=%s db=%s\n",
                        Text(value->get_file_path()).c_str(), Text(value->get_value()).c_str());
            if (expectedMdlPath) {
                TF_AXIOM(value->get_value() && Text(value->get_file_path()) == expectedMdlPath);
                MdlHandle<const ITexture> texture(transaction->access<ITexture>(value->get_value()));
                MdlHandle<const IImage> image(transaction->access<IImage>(texture->get_image()));
                CheckFile(Text(image->get_filename(0, 0)),
                          runtime.root / (std::string(expectedMdlPath).substr(1)));
            } else {
                // Characterize the existing missing-resource policy: the
                // module loads, but its resource has no usable DB reference.
                TF_AXIOM(Text(value->get_value()).empty());
            }
        } else if (success) {
            MdlHandle<const mi::IString> dbName(runtime.factory->get_db_module_name(name.c_str()));
            MdlHandle<const IModule> module(transaction->access<IModule>(dbName->get_c_str()));
            MdlHandle<const IFunction_definition> definition(
                transaction->access<IFunction_definition>(module->get_function(0)));
            MdlHandle<const IExpression_list> defaults(definition->get_defaults());
            MdlHandle<const IExpression_constant> expression(defaults->get_expression<IExpression_constant>("x"));
            MdlHandle<const IValue_float> value(expression->get_value<IValue_float>());
            TF_AXIOM(value && value->get_value() == 7.0f);
        }
    }
    transaction->abort();
}

void TestPaths(Runtime& runtime, bool builtin)
{
    std::printf("RESOLVER %s\n", builtin ? "SDK builtin" : "plugin");
    // Construct the SDK baseline without an installed external resolver;
    // otherwise its resolution machinery can delegate back to our plugin.
    runtime.configuration->set_entity_resolver(nullptr);
    MdlHandle<IMdl_entity_resolver> sdk(runtime.configuration->get_entity_resolver());
    MdlHandle<IMdl_entity_resolver> resolver(UsdMdlGetEntityResolver());
    MdlHandle<RecordingResolver> recording(new RecordingResolver(
        builtin ? sdk.get() : resolver.get()));
    runtime.configuration->set_entity_resolver(recording.get());
    const auto owner = runtime.root / "d3/nested/owner.mdl";
    Write(owner, "mdl 1.8; export float value() = 1.0;\n");
    fs::create_directories(runtime.root / "d3/nested/sub");
    fs::copy_file(runtime.root / "resources/one.png", runtime.root / "d3/nested/texture.png",
                  fs::copy_options::overwrite_existing);
    fs::copy_file(runtime.root / "resources/two.png", runtime.root / "texture.png",
                  fs::copy_options::overwrite_existing);
    fs::copy_file(runtime.root / "resources/one.png", runtime.root.parent_path() / "texture.png",
                  fs::copy_options::overwrite_existing);
    struct Case {
        const char* path;
        const char* logical;
        const char* compiled;
        int sdkError = 291;
    };
    // Rejected requests return unreadable placeholders without errors. The
    // module stays loadable, and SDK normalization cannot substitute a file.
    const std::vector<Case> paths = {
        {"./texture.png", "/d3/nested/texture.png", "/d3/nested/texture.png"},
        {"../nested/texture.png", "/d3/nested/texture.png", "/d3/nested/texture.png"},
        {"../../texture.png", "/texture.png", "/texture.png"},
        {"/d3/nested/texture.png", "/d3/nested/texture.png", "/d3/nested/texture.png"},
        {"/d3/nested/./texture.png", nullptr, nullptr},
        {"/d3/nested/sub/../texture.png", nullptr, nullptr},
        {"./sub/../texture.png", nullptr, nullptr},
        {"./sub/./../texture.png", nullptr, nullptr},
        {"././texture.png", nullptr, nullptr},
        {"../../../texture.png", nullptr, nullptr, 292},
        {"./../nested/texture.png", nullptr, nullptr},
        {".././nested/texture.png", nullptr, nullptr},
        {"sub/../texture.png", nullptr, nullptr},
        {"./sub/../tile_<UDIM>.png", nullptr, nullptr},
        {"./sub/../frame.<##>.png", nullptr, nullptr}
    };
    for (const char* file : {"tile_1001.png", "frame.00.png"}) {
        fs::copy_file(runtime.root / "resources/one.png", owner.parent_path() / file,
                      fs::copy_options::overwrite_existing);
    }
    for (std::size_t i = 0; i < paths.size(); ++i) {
        const auto& test = paths[i];
        const std::string path = test.path;
        if (!builtin) {
            MdlHandle<IMdl_execution_context> context(runtime.factory->create_execution_context());
            MdlHandle<IMdl_resolved_resource> resource(recording->resolve_resource(
                path.c_str(), owner.generic_string().c_str(), "::d3::nested::owner", 0, 0, context.get()));
            std::printf("DIRECT %s -> %s | %s\n", path.c_str(),
                resource ? Text(resource->get_mdl_file_path_mask()).c_str() : "MISS",
                resource ? Text(resource->get_filename_mask()).c_str() : "");
            TF_AXIOM(resource);
            TF_AXIOM(context->get_error_messages_count() == 0);
            if (test.logical) {
                TF_AXIOM(Text(resource->get_mdl_file_path_mask()) == test.logical);
                CheckFile(Text(resource->get_filename_mask()), runtime.root / std::string(test.logical).substr(1));
            } else {
                // Rejected identities have no physical asset and cannot open
                // bytes, even when a normalized filename exists under a root.
                TF_AXIOM(Text(resource->get_filename_mask()).empty());
                MdlHandle<const IMdl_resolved_resource_element> element(resource->get_element(0));
                TF_AXIOM(element && element->get_count() == 1);
                MdlHandle<IReader> reader(element->create_reader(0));
                TF_AXIOM(!reader);
            }
        }
        const auto stem = "dot_" + std::to_string(i);
        Write(owner.parent_path() / (stem + ".mdl"), "mdl 1.8; export float probe("
            "uniform texture_2d tex = texture_2d(\"" + path + "\")) = 1.0;\n");
        const std::string callback = path == "sub/../texture.png" ? "./" + path : path;
        Compile(runtime, recording.get(), "::d3::nested::" + stem,
                !builtin || test.logical != nullptr, callback.c_str(), test.compiled,
                builtin && !test.logical ? test.sdkError : 0);
    }

    if (!builtin) {
        Write(owner.parent_path() / "missing.mdl", "mdl 1.8; export float probe("
            "uniform texture_2d tex = texture_2d(\"./missing.png\")) = 1.0;\n");
        Compile(runtime, recording.get(), "::d3::nested::missing", true, "./missing.png");
    }

    struct DepthCase { int depth, parents; };
    for (const auto& test : {DepthCase{0, 1}, DepthCase{2, 2}, DepthCase{2, 3},
                             DepthCase{16, 16}, DepthCase{17, 17}, DepthCase{17, 18}}) {
        const int depth = test.depth;
        fs::path package = runtime.root;
        std::string name = "::";
        std::string relative;
        for (int i = 0; i < depth; ++i) {
            package /= "p";
            name += "p::";
        }
        for (int i = 0; i < test.parents; ++i) relative += "..::";
        Write(runtime.root / "d3_target.mdl", "mdl 1.8; export const float marker = 7.0;\n");
        // The excessive traversal has a physical target, so rejecting it must
        // depend on the package hierarchy rather than a missing file.
        Write(runtime.root.parent_path() / "d3_target.mdl",
              "mdl 1.8; export const float marker = 9.0;\n");
        const auto stem = "deep_" + std::to_string(test.parents);
        const auto file = package / (stem + ".mdl");
        Write(file, "mdl 1.8; import " + relative + "d3_target::*;\n"
                    "export float probe(float x = ::d3_target::marker) = x;\n");
        const auto moduleName = name + stem;
        if (!builtin) {
            MdlHandle<IMdl_resolved_module> module(recording->resolve_module(
                (relative + "d3_target").c_str(), file.generic_string().c_str(),
                moduleName.c_str(), 0, 0, nullptr));
            std::printf("DIRECT DEPTH %d PARENTS %d -> %s\n", depth, test.parents,
                        module ? module->get_module_name() : "MISS");
            TF_AXIOM(bool(module) == (test.parents <= depth));
            if (module) {
                TF_AXIOM(Text(module->get_module_name()) == "::d3_target");
                CheckFile(module->get_filename(), runtime.root / "d3_target.mdl");
            }
        }
        const bool success = test.parents <= depth;
        Compile(runtime, recording.get(), moduleName, success, nullptr, nullptr,
                success ? 0 : (builtin ? 292 : 120));
        TF_AXIOM(std::find(recording->modules.begin(), recording->modules.end(),
                           relative + "d3_target") != recording->modules.end());
    }
    runtime.configuration->set_entity_resolver(resolver.get());
}

std::string Describe(ITransaction* transaction, const IValue_resource* value, const char* label)
{
    std::string original, resolved;
    const char* dbName = value->get_value();
    TF_AXIOM(dbName);
    if (value->get_kind() == IValue::VK_TEXTURE) {
        MdlHandle<const ITexture> texture(transaction->access<ITexture>(dbName));
        MdlHandle<const IImage> image(transaction->access<IImage>(texture->get_image()));
        original = Text(image->get_original_filename());
        resolved = Text(image->get_filename(0, 0));
    } else if (value->get_kind() == IValue::VK_LIGHT_PROFILE) {
        MdlHandle<const ILightprofile> light(transaction->access<ILightprofile>(dbName));
        original = Text(light->get_original_filename());
        resolved = Text(light->get_filename());
    } else {
        MdlHandle<const IBsdf_measurement> measurement(transaction->access<IBsdf_measurement>(dbName));
        original = Text(measurement->get_original_filename());
        resolved = Text(measurement->get_filename());
    }
    std::printf("VALUE %s mdl=%s original=%s resolved=%s\n", label,
                Text(value->get_file_path()).c_str(), original.c_str(), resolved.c_str());
    return original;
}

void Inspect(Runtime& runtime, ITransaction* transaction, const char* name, bool fallback = false)
{
    MdlHandle<const mi::IString> dbName(runtime.factory->get_db_module_name(name));
    MdlHandle<const IModule> module(transaction->access<IModule>(dbName->get_c_str()));
    TF_AXIOM(module && module->get_function_count() == 1);
    MdlHandle<const IFunction_definition> definition(
        transaction->access<IFunction_definition>(module->get_function(0)));
    MdlHandle<const IExpression_list> defaults(definition->get_defaults());
    MdlModule converted(name, SdfAssetPath("d3/nested/defaults.mdl"), transaction);
    SdrShaderNodeDiscoveryResult result(TfToken(name), SdrVersion().GetAsDefault(),
        "probe", TfToken(), TfToken("mdl"), TfToken("mdl"), "d3/nested/defaults.mdl", "",
        std::string(), SdrTokenMap(), module->get_function(0), TfToken("probe"));
    const auto node = converted.GetSdrFromDiscoveryResult(result);
    TF_AXIOM(node && node->IsValid());
    for (const auto& entry : {std::pair<const char*, const char*>{"tex", "texture.png"},
                              {"light", "light.ies"}, {"measurement", "measurement.mbsdf"}}) {
        const char* parameter = entry.first;
        const std::string filename = entry.second;
        MdlHandle<const IExpression_constant> expression(defaults->get_expression<IExpression_constant>(parameter));
        TF_AXIOM(expression);
        MdlHandle<const IValue_resource> value(expression->get_value<IValue_resource>());
        TF_AXIOM(value);
        const auto original = Describe(transaction, value.get(), parameter);
        const std::string logical = "/d3/nested/" + filename;
        TF_AXIOM(Text(value->get_file_path()) == (fallback ? "" : logical));
        TF_AXIOM(original == (fallback ? "./" + filename : ""));
        const auto& asset = node->GetShaderInput(TfToken(parameter))->GetDefaultValue().Get<SdfAssetPath>();
        std::printf("SDR %s %s -> %s | %s\n", name, parameter,
                    asset.GetAssetPath().c_str(), asset.GetResolvedPath().c_str());
        CheckFile(asset.GetResolvedPath(), runtime.root / "d3/nested" / filename);
        // Known fallback defect: the root-looking logical path disagrees with
        // the nested file retained in resolvedPath. Keep this explicit until
        // the fallback contract is decided, rather than silently accepting both.
        TF_AXIOM(asset.GetAssetPath() == (fallback ? "/" + filename : logical));
        if (fallback) {
            MdlHandle<IMdl_entity_resolver> resolver(UsdMdlGetEntityResolver());
            MdlHandle<IMdl_resolved_resource> reauthored(resolver->resolve_resource(
                asset.GetAssetPath().c_str(), nullptr, nullptr, 0, 0, nullptr));
            TF_AXIOM(reauthored);
            CheckFile(Text(reauthored->get_filename_mask()), runtime.root / filename);
            TF_AXIOM(Read(runtime.root / filename) != Read(runtime.root / "d3/nested" / filename));
        }
    }
}

void TestFallbacks(Runtime& runtime)
{
    const fs::path nested = runtime.root / "d3/nested";
    fs::copy_file(runtime.root / "resources/one.ies", nested / "light.ies",
                  fs::copy_options::overwrite_existing);
    auto rootLight = Read(nested / "light.ies");
    rootLight.replace(rootLight.find("[TEST]"), 6, "[TEST] root collision");
    Write(runtime.root / "light.ies", rootLight);
    MdlHandle<Bsdf_isotropic_data> data(new Bsdf_isotropic_data(1, 1, BSDF_SCALAR));
    MdlHandle<Bsdf_buffer> buffer(data->get_bsdf_buffer());
    buffer->get_data()[0] = 0.125f;
    TF_AXIOM(runtime.impexp->export_bsdf_data((nested / "measurement.mbsdf").generic_string().c_str(),
                                            data.get(), nullptr) == 0);
    buffer->get_data()[0] = 0.25f;
    TF_AXIOM(runtime.impexp->export_bsdf_data((runtime.root / "measurement.mbsdf").generic_string().c_str(),
                                            data.get(), nullptr) == 0);
    Write(nested / "defaults.mdl", "mdl 1.8; export float probe("
        "uniform texture_2d tex = texture_2d(\"./texture.png\"),"
        "uniform light_profile light = light_profile(\"./light.ies\"),"
        "uniform bsdf_measurement measurement = bsdf_measurement(\"./measurement.mbsdf\")) = 1.0;\n");
    MdlHandle<ITransaction> transaction(runtime.scope->create_transaction());
    TF_AXIOM(runtime.impexp->load_module(transaction.get(), "::d3::nested::defaults") == 0);
    Inspect(runtime, transaction.get(), "::d3::nested::defaults");

    // Host-created DB resources can lack an MDL path. Give reset_file() a
    // relative filename and inspect the actual definition defaults the builder
    // preserves, rather than assuming this branch is reached by source imports.
    TF_AXIOM(runtime.configuration->add_resource_path(nested.generic_string().c_str()) == 0);
    {
        MdlHandle<IImage> image(transaction->create<IImage>("Image"));
        TF_AXIOM(image->reset_file("./texture.png") == 0);
        TF_AXIOM(transaction->store(image.get(), "d3_image") == 0);
        MdlHandle<ITexture> texture(transaction->create<ITexture>("Texture"));
        TF_AXIOM(texture->set_image("d3_image") == 0);
        TF_AXIOM(transaction->store(texture.get(), "d3_texture") == 0);
        MdlHandle<ILightprofile> light(transaction->create<ILightprofile>("Lightprofile"));
        TF_AXIOM(light->reset_file("./light.ies") == 0);
        TF_AXIOM(transaction->store(light.get(), "d3_light") == 0);
        MdlHandle<IBsdf_measurement> measurement(transaction->create<IBsdf_measurement>("Bsdf_measurement"));
        TF_AXIOM(measurement->reset_file("./measurement.mbsdf") == 0);
        TF_AXIOM(transaction->store(measurement.get(), "d3_measurement") == 0);
    }
    {
        MdlHandle<IValue_factory> values(runtime.factory->create_value_factory(transaction.get()));
        MdlHandle<IType_factory> types(runtime.factory->create_type_factory(transaction.get()));
        MdlHandle<IExpression_factory> expressions(runtime.factory->create_expression_factory(transaction.get()));
        MdlHandle<const IType_texture> textureType(types->create_texture(IType_texture::TS_2D));
        std::vector<MdlHandle<IValue_resource>> resources;
        resources.emplace_back(values->create_texture(textureType.get(), "d3_texture"));
        resources.emplace_back(values->create_light_profile("d3_light"));
        resources.emplace_back(values->create_bsdf_measurement("d3_measurement"));
        MdlHandle<IType_list> parameters(types->create_type_list());
        MdlHandle<IExpression_list> defaults(expressions->create_expression_list());
        const char* names[] = {"tex", "light", "measurement"};
        for (std::size_t i = 0; i < resources.size(); ++i) {
            TF_AXIOM(resources[i]);
            TF_AXIOM(Text(resources[i]->get_file_path()).empty());
            Describe(transaction.get(), resources[i].get(), names[i]);
            MdlHandle<const IType> type(resources[i]->get_type());
            MdlHandle<const IType_alias> uniform(types->create_alias(type.get(), IType::MK_UNIFORM, nullptr));
            MdlHandle<IExpression_constant> expression(expressions->create_constant(resources[i].get()));
            TF_AXIOM(parameters->add_type(names[i], uniform.get()) == 0);
            TF_AXIOM(defaults->add_expression(names[i], expression.get()) == 0);
        }
        MdlHandle<IValue_float> one(values->create_float(1.0f));
        MdlHandle<IExpression_constant> body(expressions->create_constant(one.get()));
        MdlHandle<IMdl_execution_context> context(runtime.factory->create_execution_context());
        MdlHandle<IMdl_module_builder> builder(runtime.factory->create_module_builder(transaction.get(),
            "mdl::d3::nested::host", MDL_VERSION_1_8, MDL_VERSION_LATEST, context.get()));
        TF_AXIOM(builder);
        const auto rc = builder->add_function("probe", body.get(), nullptr, parameters.get(),
            defaults.get(), nullptr, nullptr, nullptr, true, false, IType::MK_NONE, context.get());
        std::printf("BUILDER rc=%d\n", rc);
        PrintMessages(context.get());
        TF_AXIOM(rc == 0);
        Inspect(runtime, transaction.get(), "::d3::nested::host");
    }
    // Editing an existing resource through the public SDK API can erase its
    // MDL path without replacing the definition's resource reference.
    std::vector<std::string> resourceNames;
    {
        MdlHandle<const IModule> module(transaction->access<IModule>("mdl::d3::nested::host"));
        MdlHandle<const IFunction_definition> definition(
            transaction->access<IFunction_definition>(module->get_function(0)));
        MdlHandle<const IExpression_list> defaults(definition->get_defaults());
        for (const char* name : {"tex", "light", "measurement"}) {
            MdlHandle<const IExpression_constant> expression(defaults->get_expression<IExpression_constant>(name));
            MdlHandle<const IValue_resource> value(expression->get_value<IValue_resource>());
            resourceNames.emplace_back(value->get_value());
        }
        MdlHandle<const ITexture> texture(transaction->access<ITexture>(resourceNames[0].c_str()));
        resourceNames[0] = texture->get_image();
    }
    {
        MdlHandle<IImage> image(transaction->edit<IImage>(resourceNames[0].c_str()));
        TF_AXIOM(image->reset_file("./texture.png") == 0);
        MdlHandle<ILightprofile> light(transaction->edit<ILightprofile>(resourceNames[1].c_str()));
        TF_AXIOM(light->reset_file("./light.ies") == 0);
        MdlHandle<IBsdf_measurement> measurement(transaction->edit<IBsdf_measurement>(resourceNames[2].c_str()));
        TF_AXIOM(measurement->reset_file("./measurement.mbsdf") == 0);
    }
    std::printf("AFTER SDK RESOURCE EDIT\n");
    Inspect(runtime, transaction.get(), "::d3::nested::host", true);
    transaction->abort();
}

} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    {
        Runtime runtime;
        TestPaths(runtime, false);
        TestPaths(runtime, true);
        TestFallbacks(runtime);
    }
    UsdMdlShutdown();
}
