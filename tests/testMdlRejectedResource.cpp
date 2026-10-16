// SPDX-License-Identifier: Apache-2.0
// Reproduce #25 with colliding root/nested PNGs, then prove rejection does not
// let the SDK reopen a normalized resource under a different identity.
#include "Context.h"
#include "ModuleLoader.h"
#include "RejectedResource.h"
#include "initialize.h"
#include "mdlModule.h"
#include <mi/mdl_sdk.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/diagnosticMgr.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE
using namespace mi::neuraylib;
template<class T> using MdlHandle = mi::base::Handle<T>;
namespace fs = std::filesystem;
namespace {
int failures = 0;
#define EXPECT(c, text) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, text); ++failures; } } while (0)
std::string Text(const char* value) { return value ? value : ""; }
void Write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary); file << text; TF_AXIOM(file.good());
}
std::string Read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary); TF_AXIOM(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
struct Warning { std::string text; VtDictionary data; };
class Capture final : public TfDiagnosticMgr::Delegate {
public:
    Capture() { TfDiagnosticMgr::GetInstance().AddDelegate(this); }
    ~Capture() override { TfDiagnosticMgr::GetInstance().RemoveDelegate(this); }
    void IssueError(const TfError&) override { ++errors; }
    void IssueFatalError(const TfCallContext&, const std::string&) override { ++errors; }
    void IssueStatus(const TfStatus&) override {}
    void IssueWarning(const TfWarning& warning) override {
        std::lock_guard<std::mutex> lock(mutex);
        const auto* data = warning.GetInfo<VtDictionary>();
        warnings.push_back({warning.GetCommentary(), data ? *data : VtDictionary()});
    }
    std::vector<Warning> Take() { std::lock_guard<std::mutex> lock(mutex); std::vector<Warning> result; result.swap(warnings); return result; }
    std::atomic<int> errors{0};
private:
    std::mutex mutex;
    std::vector<Warning> warnings;
};
template<class T> const T* Field(const VtDictionary& value, const char* key) {
    const auto found = value.find(key);
    return found != value.end() && found->second.IsHolding<T>() ? &found->second.UncheckedGet<T>() : nullptr;
}
// Only the historical rejection behavior is reproduced here. Every accepted
// request still goes through the same plugin, SDK compiler and PNG decoder.
class LegacyNullResolver final : public mi::base::Interface_implement<IMdl_entity_resolver> {
public:
    explicit LegacyNullResolver(IMdl_entity_resolver* delegate) : delegate(delegate, mi::base::DUP_INTERFACE) {}
    IMdl_resolved_module* resolve_module(const char* name, const char* file, const char* owner,
        mi::Sint32 line, mi::Sint32 column, IMdl_execution_context* context) override {
        return delegate->resolve_module(name, file, owner, line, column, context);
    }
    IMdl_resolved_resource* resolve_resource(const char* path, const char* file, const char* owner,
        mi::Sint32 line, mi::Sint32 column, IMdl_execution_context* context) override {
        if (Text(path) == "../../../texture.png" || Text(path) == "./../nested/texture.png") return nullptr;
        return delegate->resolve_resource(path, file, owner, line, column, context);
    }
private:
    MdlHandle<IMdl_entity_resolver> delegate;
};
struct Runtime {
    MdlHandle<IMdl_factory> factory;
    MdlHandle<IMdl_impexp_api> impexp;
    MdlHandle<IMdl_configuration> configuration;
    MdlHandle<IScope> scope;
    MdlHandle<IMdl_entity_resolver> resolver;
    fs::path root = fs::absolute("mdl");
    Runtime() {
        TF_AXIOM(UsdMdlInitialize());
        MdlHandle<INeuray> neuray(UsdMdlGetNeuray());
        factory = MdlHandle<IMdl_factory>(neuray->get_api_component<IMdl_factory>());
        impexp = MdlHandle<IMdl_impexp_api>(neuray->get_api_component<IMdl_impexp_api>());
        configuration = MdlHandle<IMdl_configuration>(neuray->get_api_component<IMdl_configuration>());
        MdlHandle<IDatabase> database(neuray->get_api_component<IDatabase>());
        scope = MdlHandle<IScope>(database->get_global_scope());
        resolver = MdlHandle<IMdl_entity_resolver>(UsdMdlGetEntityResolver());
    }
};
void CheckTexture(ITransaction* transaction, const IValue_texture* value, const fs::path& file,
                  unsigned width, unsigned height, const char* type, float red, float blue) {
    EXPECT(value && !Text(value->get_value()).empty(), "valid texture must have a SDK database resource");
    if (!value || Text(value->get_value()).empty()) return;
    MdlHandle<const ITexture> texture(transaction->access<ITexture>(value->get_value()));
    MdlHandle<const IImage> image(texture ? transaction->access<IImage>(texture->get_image()) : nullptr);
    EXPECT(image, "texture's image must exist"); if (!image) return;
    const auto filename = Text(image->get_filename(0, 0));
    EXPECT(!filename.empty() && fs::equivalent(filename, file), "loaded filename must select the authored target");
    auto asset = ArGetResolver().OpenAsset(ArResolvedPath(filename));
    EXPECT(asset, "loaded image file must open through Ar");
    if (asset) {
        const auto buffer = asset->GetBuffer();
        EXPECT(buffer && std::string(buffer.get(), asset->GetSize()) == Read(file), "Ar bytes must identify the same PNG");
    }
    MdlHandle<const ICanvas> canvas(image->get_canvas(0, 0, 0));
    EXPECT(canvas && canvas->get_resolution_x() == width && canvas->get_resolution_y() == height,
           "canvas dimensions distinguish nested A from root B");
    if (canvas) {
        EXPECT(Text(canvas->get_type()) == type, "PNG channel layout must match the fixture");
        MdlHandle<const ITile> tile(canvas->get_tile()); float pixel[4] = {};
        EXPECT(tile, "PNG canvas must contain decoded pixels");
        if (tile) { tile->get_pixel(0, 0, pixel); EXPECT(pixel[0] == red && pixel[2] == blue, "decoded pixel identifies the selected PNG"); }
        std::printf("image file=%s size=%ux%u type=%s\n", filename.c_str(), canvas->get_resolution_x(), canvas->get_resolution_y(), canvas->get_type());
    }
}
void Inspect(Runtime& runtime, ITransaction* transaction, const std::string& name,
             const char* invalid, const fs::path* valid, bool extraRoot = false) {
    MdlHandle<const mi::IString> dbName(runtime.factory->get_db_module_name(name.c_str()));
    MdlHandle<const IModule> module(transaction->access<IModule>(dbName->get_c_str()));
    EXPECT(module && module->get_function_count() == 1, "probe module must contain one exported definition"); if (!module) return;
    MdlHandle<const IFunction_definition> definition(transaction->access<IFunction_definition>(module->get_function(0)));
    MdlHandle<const IExpression_list> defaults(definition->get_defaults());
    MdlHandle<const IExpression_constant> expression(defaults->get_expression<IExpression_constant>("tex"));
    MdlHandle<const IValue_texture> value(expression ? expression->get_value<IValue_texture>() : nullptr);
    EXPECT(value, "probe's texture default must be a SDK texture value"); if (!value) return;
    if (valid) {
        const bool rootImage = fs::equivalent(*valid, runtime.root / "texture.png");
        CheckTexture(transaction, value.get(), *valid, rootImage ? 64 : 1, rootImage ? 64 : 1,
                     rootImage ? "Rgb" : "Rgba", rootImage ? 0.0f : 1.0f, rootImage ? 1.0f : 0.0f);
    } else {
        EXPECT(Text(value->get_value()).empty(), "rejected resource must remain invalid, never substitute another PNG");
    }
    const auto source = Text(module->get_filename());
    MdlModule converted(name, SdfAssetPath(source), transaction);
    SdrShaderNodeDiscoveryResult discovery(TfToken(name), SdrVersion().GetAsDefault(), "probe", TfToken(),
        TfToken("mdl"), TfToken("mdl"), source, source, std::string(), SdrTokenMap(), module->get_function(0), TfToken("probe"));
    const auto node = converted.GetSdrFromDiscoveryResult(discovery);
    EXPECT(node && node->IsValid(), "module with rejected resource must produce a valid Sdr node");
    if (node) {
        const auto* input = node->GetShaderInput(TfToken("tex"));
        EXPECT(input && input->GetDefaultValue().IsHolding<SdfAssetPath>(), "texture port must have an asset default");
        if (input && input->GetDefaultValue().IsHolding<SdfAssetPath>()) {
            const auto& asset = input->GetDefaultValue().Get<SdfAssetPath>();
            if (!valid) EXPECT(asset == SdfAssetPath(), "Sdr rejected default must be empty, without a private identity");
            else EXPECT(!asset.GetResolvedPath().empty() && fs::equivalent(asset.GetResolvedPath(), *valid), "Sdr control must resolve to the same PNG");
        }
    }
    if (extraRoot) {
        MdlHandle<const IExpression_constant> rootExpression(defaults->get_expression<IExpression_constant>("root"));
        MdlHandle<const IValue_texture> rootValue(rootExpression ? rootExpression->get_value<IValue_texture>() : nullptr);
        CheckTexture(transaction, rootValue.get(), runtime.root / "texture.png", 64, 64, "Rgb", 0.0f, 1.0f);
        if (node) {
            const auto& asset = node->GetShaderInput(TfToken("root"))->GetDefaultValue().Get<SdfAssetPath>();
            EXPECT(asset.GetAssetPath() == "/texture.png", "legitimate root input must retain its MDL identity");
        }
    }
    if (invalid) std::printf("rejected authored=%s db=%s\n", invalid, Text(value->get_value()).c_str());
}
void CheckRejectedIdentities(Runtime& runtime) {
    using namespace usdMdl::runtime;
    RejectedResourceInfo original;
    original.resourcePath = "../../../space # %20.png";
    original.ownerModule = "::d3::nested::identity";
    original.ownerFile = (runtime.root / "d3/nested/space # owner.mdl").generic_string();
    original.reason = "parent traversal exceeds the owning module package";
    original.line = 9; original.column = 17;
    const std::string marker = EncodeRejectedResource(original);
    RejectedResourceInfo decoded;
    EXPECT(IsRejectedResourcePath(marker) && DecodeRejectedResource(marker, decoded),
           "private identity must survive serialization for cached imported defaults");
    EXPECT(decoded.resourcePath == original.resourcePath && decoded.ownerModule == original.ownerModule
               && decoded.ownerFile == original.ownerFile && decoded.reason == original.reason
               && decoded.line == 9 && decoded.column == 17,
           "serialization must preserve the authored path and diagnostic context");
    RejectedResourceInfo unchanged = original;
    EXPECT(!DecodeRejectedResource(marker + "/extra", unchanged)
               && unchanged.resourcePath == original.resourcePath && unchanged.line == 9,
           "malformed identity must be rejected without partially modifying output");
    for (const char* reserved : {"/__usdMdl_rejected__/future/texture.png",
                                "/__usdMdl_rejected__/v1/not_a_complete_record"}) {
        const auto target = runtime.root / std::string(reserved).substr(1);
        fs::create_directories(target.parent_path());
        fs::copy_file(runtime.root / "texture.png", target, fs::copy_options::overwrite_existing);
        EXPECT(IsRejectedResourcePath(reserved), "all reserved marker versions must be recognized");
        EXPECT(!MdlHandle<IMdl_resolved_resource>(runtime.resolver->resolve_resource(
                   reserved, nullptr, nullptr, 0, 0, nullptr)),
               "reserved identity must not fall through to an existing readable file");
    }
    MdlHandle<IMdl_execution_context> context(runtime.factory->create_execution_context());
    MdlHandle<IMdl_execution_context> foreign(runtime.factory->create_execution_context());
    EXPECT(context->set_option("user_data", foreign.get()) == 0, "SDK must accept caller-owned user_data");
    EXPECT(!AttachRejectedResourceState(context.get()), "diagnostic collection must not replace caller-owned user_data");
    MdlHandle<IMdl_resolved_resource> descriptor(runtime.resolver->resolve_resource(
        "../../../texture.png", original.ownerFile.c_str(), original.ownerModule.c_str(), 9, 17, context.get()));
    EXPECT(descriptor && descriptor->get_count() == 1, "direct native caller must receive an unreadable descriptor");
    if (descriptor) {
        EXPECT(Text(descriptor->get_filename_mask()).empty(), "rejection descriptor must not claim a physical file");
        MdlHandle<const IMdl_resolved_resource_element> element(descriptor->get_element(0));
        EXPECT(element && element->get_count() == 1, "SDK-valid descriptor must have one resource entity");
        EXPECT(element && !MdlHandle<IReader>(element->create_reader(0)), "initial descriptor must never open resource bytes");
        EXPECT(!MdlHandle<IMdl_resolved_resource>(runtime.resolver->resolve_resource(
                   descriptor->get_mdl_file_path_mask(), nullptr, nullptr, 0, 0, context.get())),
               "later SDK resource lookup must reject the private identity");
    }
    const mi::base::IInterface* retained = nullptr;
    EXPECT(context->get_option("user_data", &retained) == 0, "native user_data must remain available");
    MdlHandle<const mi::base::IInterface> retainedData(retained);
    EXPECT(retainedData.get() == static_cast<const mi::base::IInterface*>(foreign.get()),
           "resolver must preserve the original caller-owned user_data interface");
    EXPECT(context->get_error_messages_count() == 0, "native direct rejection must not add compiler errors");
}
void DirectLoads(Runtime& runtime, const std::string& mode) {
    MdlHandle<IMdl_entity_resolver> selected;
    if (mode == "legacy") selected = MdlHandle<IMdl_entity_resolver>(new LegacyNullResolver(runtime.resolver.get()));
    else if (mode == "sdk") {
        runtime.configuration->set_entity_resolver(nullptr);
        selected = MdlHandle<IMdl_entity_resolver>(runtime.configuration->get_entity_resolver());
    } else selected = runtime.resolver;
    runtime.configuration->set_entity_resolver(selected.get());
    const fs::path nested = runtime.root / "d3/nested";
    struct Case { const char* path; const char* target; };
    const Case cases[] = {{"../../../texture.png", nullptr}, {"./../nested/texture.png", nullptr},
        {"./texture.png", "d3/nested/texture.png"}, {"../../texture.png", "texture.png"},
        {"./sub/../tile_<UDIM>.png", nullptr}, {"./sub/../frame.<##>.png", nullptr}};
    fs::create_directories(nested / "sub");
    for (unsigned i = 0; i < (mode == "plugin" ? 6u : 1u); ++i) {
        const auto& test = cases[i]; const std::string name = "::d3::nested::case_" + std::to_string(i);
        const std::string source = i == 0
            ? "mdl 1.8;\nexport float probe(uniform texture_2d tex = texture_2d(\"../../../texture.png\")) = 1.0;\n"
            : "mdl 1.8;\nexport texture_2d probe(\n"
              "uniform texture_2d tex = texture_2d(\"" + std::string(test.path) + "\"),\n"
              "uniform texture_2d root = texture_2d(\"/texture.png\")) { return tex; }\n";
        Write(nested / ("case_" + std::to_string(i) + ".mdl"), source);
        MdlHandle<ITransaction> transaction(runtime.scope->create_transaction());
        {
            MdlHandle<IMdl_execution_context> context(runtime.factory->create_execution_context());
            const auto rc = runtime.impexp->load_module(transaction.get(), name.c_str(), context.get());
            std::printf("mode=%s path=%s rc=%d\n", mode.c_str(), test.path, rc);
            if (mode == "sdk") {
                EXPECT(rc < 0 && context->get_error_messages_count() != 0, "SDK filesystem resolver must reject the original reproduction");
            } else {
                EXPECT(rc >= 0 && context->get_error_messages_count() == 0, "plugin must preserve permissive module loading");
                const fs::path target = runtime.root / (mode == "legacy" ? "texture.png" : test.target ? test.target : "");
                if (rc >= 0) Inspect(runtime, transaction.get(), name, test.target ? nullptr : test.path,
                                     mode == "legacy" || test.target ? &target : nullptr, i != 0);
            }
        }
        transaction->abort();
    }
    runtime.configuration->set_entity_resolver(runtime.resolver.get());
}
void CheckRejectionWarnings(const std::vector<Warning>& warnings, const std::set<std::string>& modules,
                            bool imported = false) {
    std::set<std::string> reported;
    std::set<std::string> sdkMessages;
    for (const auto& warning : warnings) {
        const auto* path = Field<std::string>(warning.data, "resourcePath");
        if (!path) continue;
        const auto* owner = Field<std::string>(warning.data, "ownerModule");
        const auto* file = Field<std::string>(warning.data, "ownerFile");
        const auto* line = Field<int>(warning.data, "line");
        const auto* column = Field<int>(warning.data, "column");
        const auto* reason = Field<std::string>(warning.data, "reason");
        EXPECT(*path == "../../../texture.png", "warning must retain authored relative path");
        EXPECT(owner && modules.count(*owner), "concurrent warning must retain the owning module");
        EXPECT(file && file->find(owner ? owner->substr(owner->rfind("::") + 2) + ".mdl" : "unexpected") != std::string::npos,
               "warning must identify the matching module file");
        EXPECT(line && *line > 0 && column && *column > 0, "source position must survive the rejection");
        EXPECT(reason && !reason->empty() && warning.text.find(*reason) != std::string::npos, "warning must explain rejection");
        EXPECT(warning.text.find(*path) != std::string::npos, "warning must show the authored path");
        EXPECT(warning.text.find("/__usdMdl_rejected__/") == std::string::npos,
               "printed USD warning must hide the private resource identity");
        if (owner) {
            const bool first = reported.insert(*owner).second;
            if (!imported) EXPECT(first, "each independent module rejection must be reported once");
        }
        if (imported) {
            // The SDK can warn once while loading the provider and again while
            // cloning its omitted argument into the consumer. Keep both distinct
            // SDK diagnostics; reject replay or an extra plugin fallback warning.
            const auto* raw = Field<std::string>(warning.data, "message");
            EXPECT(raw && sdkMessages.insert(*raw).second, "each distinct SDK diagnostic must be forwarded once");
            EXPECT(!Field<std::string>(warning.data, "origin"), "SDK-reported imports must not gain a duplicate resolver warning");
        }
        std::printf("warning %s\n", warning.text.c_str());
    }
    EXPECT(reported == modules, "each requested module must report its rejection");
}
unsigned CheckImportedExpression(ITransaction* transaction, const IExpression* expression,
                                 unsigned depth = 0) {
    EXPECT(expression && depth < 32, "imported expression graph must be finite and inspectable");
    if (!expression || depth >= 32) return 0;
    if (expression->get_kind() == IExpression::EK_CONSTANT) {
        MdlHandle<const IExpression_constant> constant(expression->get_interface<IExpression_constant>());
        MdlHandle<const IValue_texture> texture(constant->get_value<IValue_texture>());
        if (!texture) return 0;
        EXPECT(Text(texture->get_value()).empty(), "consumer's imported default must remain an invalid SDK texture");
        return 1;
    }
    MdlHandle<const IExpression_list> arguments;
    if (expression->get_kind() == IExpression::EK_DIRECT_CALL) {
        MdlHandle<const IExpression_direct_call> call(expression->get_interface<IExpression_direct_call>());
        arguments = MdlHandle<const IExpression_list>(call->get_arguments());
    } else if (expression->get_kind() == IExpression::EK_CALL) {
        MdlHandle<const IExpression_call> expressionCall(expression->get_interface<IExpression_call>());
        MdlHandle<const IFunction_call> call(transaction->access<IFunction_call>(expressionCall->get_call()));
        EXPECT(call, "imported call must have a database argument graph");
        if (call) arguments = MdlHandle<const IExpression_list>(call->get_arguments());
    }
    unsigned count = 0;
    if (arguments) for (mi::Size i = 0; i < arguments->get_size(); ++i) {
        MdlHandle<const IExpression> argument(arguments->get_expression(i));
        count += CheckImportedExpression(transaction, argument.get(), depth + 1);
    }
    return count;
}
void InspectConsumer(Runtime& runtime, ITransaction* transaction, const char* name) {
    MdlHandle<const mi::IString> dbName(runtime.factory->get_db_module_name(name));
    MdlHandle<const IModule> module(transaction->access<IModule>(dbName->get_c_str()));
    EXPECT(module && module->get_function_count() == 1, "consumer must expose one definition");
    if (!module) return;
    MdlHandle<const IFunction_definition> definition(transaction->access<IFunction_definition>(module->get_function(0)));
    MdlHandle<const IExpression> body(definition->get_body());
    unsigned count = CheckImportedExpression(transaction, body.get());
    for (mi::Size i = 0; i < definition->get_temporary_count(); ++i) {
        MdlHandle<const IExpression> temporary(definition->get_temporary(i));
        count += CheckImportedExpression(transaction, temporary.get());
    }
    EXPECT(count > 0, "consumer must actually contain the imported texture default, not merely load the provider");
}
void PluginLoads(Runtime& runtime, Capture& capture) {
    auto& loader = usdMdl::runtime::Context::instance().getModuleLoader();
    const fs::path nested = runtime.root / "d3/nested";
    const std::set<std::string> names{"::d3::nested::load_a", "::d3::nested::load_b"};
    for (const auto& name : names) {
        const auto stem = name.substr(name.rfind("::") + 2);
        Write(nested / (stem + ".mdl"), "mdl 1.8;\nexport texture_2d probe(\n"
              "uniform texture_2d tex = texture_2d(\"../../../texture.png\"),\n"
              "uniform texture_2d root = texture_2d(\"/texture.png\")) { return tex; }\n");
    }
    capture.Take();
    std::mutex mutex; std::condition_variable cv; int ready = 0; bool start = false;
    std::atomic<int> failed{0}; std::vector<std::thread> threads;
    for (const auto& name : names) threads.emplace_back([&, name] {
        TfErrorMark errors;
        { std::unique_lock<std::mutex> lock(mutex); ++ready; cv.notify_all(); cv.wait(lock, [&] { return start; }); }
        if (loader.loadModule(name.c_str()) < 0 || !errors.IsClean()) ++failed;
    });
    { std::unique_lock<std::mutex> lock(mutex); cv.wait(lock, [&] { return ready == 2; }); start = true; }
    cv.notify_all(); for (auto& thread : threads) thread.join();
    EXPECT(failed == 0, "concurrent rejected resources must preserve successful loads without Tf errors");
    CheckRejectionWarnings(capture.Take(), names);
    {
        MdlHandle<ITransaction> transaction(runtime.scope->create_transaction());
        for (const auto& name : names) Inspect(runtime, transaction.get(), name, "../../../texture.png", nullptr, true);
        transaction->abort();
    }
    for (const auto& name : names) EXPECT(loader.loadModule(name.c_str()) == 0, "repeated load must be cached");
    EXPECT(capture.Take().empty(), "cached loads must not replay rejection warnings");
    // Import a function whose omitted texture argument owns the rejection.
    Write(nested / "provider.mdl", "mdl 1.8;\nexport texture_2d probe(uniform texture_2d tex = texture_2d(\"../../../texture.png\")) { return tex; }\n");
    Write(runtime.root / "consumer.mdl", "mdl 1.8;\nimport ::d3::nested::provider::*;\nexport texture_2d entry() { return ::d3::nested::provider::probe(); }\n");
    EXPECT(loader.loadModule("::consumer") >= 0, "imported rejected defaults must leave consumer loadable");
    CheckRejectionWarnings(capture.Take(), {"::d3::nested::provider"}, true);
    {
        MdlHandle<ITransaction> transaction(runtime.scope->create_transaction());
        Inspect(runtime, transaction.get(), "::d3::nested::provider", "../../../texture.png", nullptr);
        InspectConsumer(runtime, transaction.get(), "::consumer");
        transaction->abort();
    }
    EXPECT(loader.loadModule("::consumer") == 0 && capture.Take().empty(), "cached imports must remain quiet");
    // The provider is committed and cached: a new load has no original callback
    // record and must still recognize the identity in its imported default.
    Write(runtime.root / "consumer_cached.mdl", "mdl 1.8;\nimport ::d3::nested::provider::*;\n"
        "export texture_2d entry() { return ::d3::nested::provider::probe(); }\n");
    EXPECT(loader.loadModule("::consumer_cached") >= 0, "new consumer of a cached provider must remain loadable");
    {
        MdlHandle<ITransaction> transaction(runtime.scope->create_transaction());
        InspectConsumer(runtime, transaction.get(), "::consumer_cached");
        transaction->abort();
    }
    CheckRejectionWarnings(capture.Take(), {"::d3::nested::provider"}, true);
}
} // namespace
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string mode = argc > 1 ? argv[1] : "plugin";
    TF_AXIOM(mode == "legacy" || mode == "sdk" || mode == "plugin");
    Capture capture; TfErrorMark errors;
    { Runtime runtime; CheckRejectedIdentities(runtime); DirectLoads(runtime, mode); if (mode == "plugin") PluginLoads(runtime, capture); }
    EXPECT(errors.IsClean() && capture.errors == 0, "resource rejection must not post Tf errors");
    UsdMdlShutdown();
    std::printf("[testMdlRejectedResource] %s mode=%s\n", failures ? "FAILED" : "OK", mode.c_str());
    return failures ? 1 : 0;
}
