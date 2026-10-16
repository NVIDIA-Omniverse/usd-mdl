// SPDX-License-Identifier: Apache-2.0

// Exercise naming at the SDK boundary: string round trips alone cannot show
// which source file was loaded or whether aliases create distinct DB modules.
// Some assertions characterize current limitations; update them with fixes.
#include "initialize.h"
#include "addModuleToRegistry.h"
#include "runtime/UriEncoding.h"
#include <mi/mdl_sdk.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdr/shaderProperty.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE
using namespace mi::neuraylib;
using namespace usdMdl::runtime;
template<class T> using Handle = mi::base::Handle<T>;
namespace fs = std::filesystem;

namespace {
std::string Text(const char* text) { return text ? text : ""; }

void Write(const fs::path& file, const std::string& source)
{
    fs::create_directories(file.parent_path());
    std::ofstream stream(file);
    stream << source;
    TF_AXIOM(stream.good());
}

const char* source = "mdl 1.8; export float probe(float x = 7.0) = x;\n";

class Recorder final : public mi::base::Interface_implement<IMdl_entity_resolver>
{
public:
    explicit Recorder(IMdl_entity_resolver* resolver)
        : delegate(resolver, mi::base::DUP_INTERFACE) {}
    Handle<IMdl_entity_resolver> delegate;
    bool rejectFilenames = false;
    unsigned filenameRequests = 0;
    std::vector<std::string> requests;

    IMdl_resolved_module* resolve_module(const char* name, const char* file,
        const char* owner, mi::Sint32 line, mi::Sint32 column,
        IMdl_execution_context* context) override
    {
        const std::string request = Text(name);
        requests.push_back(request);
        const bool filename = request.compare(0, 2, "::") != 0
            && request.compare(0, 3, ".::") != 0
            && request.compare(0, 4, "..::") != 0;
        if (filename) ++filenameRequests;
        std::printf("  CALLBACK name=%s owner=%s filename=%d\n",
            request.c_str(), Text(owner).c_str(), filename);
        if (filename && rejectFilenames) return nullptr;
        auto* result = delegate->resolve_module(name, file, owner, line, column, context);
        std::printf("  RESULT name=%s file=%s\n",
            result ? Text(result->get_module_name()).c_str() : "",
            result ? Text(result->get_filename()).c_str() : "");
        return result;
    }

    IMdl_resolved_resource* resolve_resource(const char* path, const char* file,
        const char* owner, mi::Sint32 line, mi::Sint32 column,
        IMdl_execution_context* context) override
    {
        return delegate->resolve_resource(path, file, owner, line, column, context);
    }
};

struct Runtime
{
    Handle<IMdl_factory> factory;
    Handle<IMdl_impexp_api> impexp;
    Handle<IMdl_configuration> config;
    Handle<IScope> scope;
    fs::path root = fs::absolute("mdl");
    Runtime()
    {
        TF_AXIOM(UsdMdlInitialize());
        mi::base::Handle<mi::neuraylib::INeuray> neuray(UsdMdlGetNeuray());
        factory = Handle<IMdl_factory>(neuray->get_api_component<IMdl_factory>());
        impexp = Handle<IMdl_impexp_api>(neuray->get_api_component<IMdl_impexp_api>());
        config = Handle<IMdl_configuration>(neuray->get_api_component<IMdl_configuration>());
        Handle<IDatabase> db(neuray->get_api_component<IDatabase>());
        scope = Handle<IScope>(db->get_global_scope());
    }
    Handle<Recorder> Install(bool builtin)
    {
        config->set_entity_resolver(nullptr);
        Handle<IMdl_entity_resolver> sdk(config->get_entity_resolver());
        Handle<IMdl_entity_resolver> resolver(UsdMdlGetEntityResolver());
        Handle<Recorder> recorder(new Recorder(builtin ? sdk.get() : resolver.get()));
        config->set_entity_resolver(recorder.get());
        return recorder;
    }
};

struct Loaded { int rc; std::string db, file; float value = -1; };

Loaded Load(Runtime& runtime, ITransaction* txn, const std::string& name)
{
    Handle<IMdl_execution_context> context(runtime.factory->create_execution_context());
    Loaded loaded{runtime.impexp->load_module(txn, name.c_str(), context.get()), {}, {}};
    for (mi::Size i = 0; i < context->get_messages_count(); ++i) {
        Handle<const IMessage> message(context->get_message(i));
        std::printf("  MESSAGE %d %s\n", message->get_code(), message->get_string());
    }
    if (loaded.rc >= 0) {
        TF_AXIOM(context->get_error_messages_count() == 0);
        Handle<const mi::IString> db(runtime.factory->get_db_module_name(name.c_str()));
        TF_AXIOM(db);
        loaded.db = db->get_c_str();
        Handle<const IModule> module(txn->access<IModule>(loaded.db.c_str()));
        TF_AXIOM(module && module->get_function_count() == 1);
        loaded.file = Text(module->get_filename());
        Handle<const IFunction_definition> def(txn->access<IFunction_definition>(module->get_function(0)));
        Handle<const IExpression_list> defaults(def->get_defaults());
        Handle<const IExpression_constant> expr(defaults->get_expression<IExpression_constant>("x"));
        Handle<const IValue_float> value(expr->get_value<IValue_float>());
        TF_AXIOM(value);
        loaded.value = value->get_value();
    }
    std::printf("LOAD name=%s rc=%d db=%s file=%s value=%.1f\n",
        name.c_str(), loaded.rc, loaded.db.c_str(), loaded.file.c_str(), loaded.value);
    return loaded;
}

void PackageNames(Runtime& runtime)
{
    struct Case { const char* path; bool reserved; };
    for (const Case& test : {
            Case{"d4_control/identity.mdl", false},
            {"file%3A/identity.mdl", true},
            {"custom%3A/identity.mdl", true},
            {"file%3A%2F/identity.mdl", true},
            {"file%3A%2F%2F/identity.mdl", true},
            {"file%3A%2F%2F%2F/identity.mdl", true},
            {"file%253A/identity.mdl", false},
            {"d4%25plain/identity.mdl", false},
            {"d4_nested/file%3A/identity.mdl", false},
            {"d4_nested/file%3A%2F/identity.mdl", false},
            {"d4_nested/file%3A%2F%2F/identity.mdl", false},
            {"d4_nested/file%3A%2F%2F%2F/identity.mdl", false},
            {"d4_nested/%2E%2E/%2F%5C%00.mdl", false},
            {"d4_nested/percent%/module%3A.mdl", false},
            {"d4_nested/hash#at@/identity.mdl", false},
            {"file%3A.mdl", false}}) {
        const fs::path file = runtime.root / test.path;
        Write(file, source);
        Handle<const mi::IString> mdl(runtime.impexp->get_mdl_module_name(file.generic_string().c_str()));
        const std::string name = mdl ? mdl->get_c_str() : "";
        TF_AXIOM(!name.empty());
        TF_AXIOM(IsEncodedMdlName(name) == test.reserved);
        TF_AXIOM(EncodeUriToMdlName(file.generic_string(), {runtime.root.generic_string()}) == name);
        std::printf("PACKAGE path=%s sdk-name=%s encoded=%d\n",
            test.path, name.c_str(), IsEncodedMdlName(name));
        for (bool builtin : {false, true}) {
            auto recorder = runtime.Install(builtin);
            std::printf("MODE %s\n", builtin ? "builtin" : "plugin");
            Handle<ITransaction> txn(runtime.scope->create_transaction());
            const auto loaded = Load(runtime, txn.get(), name);
            const bool succeeds = builtin || !test.reserved;
            TF_AXIOM(loaded.rc == (succeeds ? 0 : -2));
            TF_AXIOM(recorder->requests == std::vector<std::string>{name});
            if (succeeds) {
                TF_AXIOM(fs::equivalent(loaded.file, file) && loaded.value == 7.0f);
            }
            txn->abort();
        }
        if (!test.reserved) {
            auto recorder = runtime.Install(false);
            const auto* cold = SdrRegistry::GetInstance().GetShaderNodeFromAsset(
                SdfAssetPath(file.generic_string()), {}, TfToken("probe"), TfToken("mdl"));
            TF_AXIOM(cold && cold->IsValid());
            TF_AXIOM(cold->GetShaderInput(TfToken("x"))->GetDefaultValue().Get<float>() == 7.0f);
            TF_AXIOM(fs::equivalent(cold->GetResolvedImplementationURI(), file));
            const auto nodes = UsdMdlAddModuleToRegistry(SdfAssetPath(file.generic_string()));
            TF_AXIOM(nodes.size() == 1 && nodes.front()->IsValid());
            TF_AXIOM(fs::equivalent(nodes.front()->GetResolvedImplementationURI(), file));
            TF_AXIOM(nodes.front()->GetShaderInput(TfToken("x"))->GetDefaultValue().Get<float>() == 7.0f);
        }
    }
}

void PercentImportsAndResources(Runtime& runtime)
{
    const fs::path dir = runtime.root / "d4_import/file%3A";
    Write(dir / "sibling%3A.mdl", "mdl 1.8; export const float marker = 7.0;\n");
    Write(dir / "caller.mdl", "mdl 1.8; import .::'sibling%3A'::*; "
        "export float probe(float x = 'sibling%3A'::marker) = x;\n");
    const fs::path absolute = runtime.root / "d4_import/absolute.mdl";
    Write(absolute, "mdl 1.8; import ::d4_import::'file%3A'::'sibling%3A'::*; "
        "export float probe(float x = ::d4_import::'file%3A'::'sibling%3A'::marker) = x;\n");
    for (const auto& file : {dir / "caller.mdl", absolute}) {
        Handle<const mi::IString> name(runtime.impexp->get_mdl_module_name(file.generic_string().c_str()));
        TF_AXIOM(name);
        for (bool builtin : {false, true}) {
            auto recorder = runtime.Install(builtin);
            Handle<ITransaction> txn(runtime.scope->create_transaction());
            const auto loaded = Load(runtime, txn.get(), name->get_c_str());
            TF_AXIOM(loaded.rc == 0 && loaded.value == 7.0f && fs::equivalent(loaded.file, file));
            txn->abort();
        }
    }

    auto recorder = runtime.Install(false);
    Write(dir / "resource%3A.bin", "literal-percent resource");
    Handle<IMdl_resolved_resource> resource(recorder->resolve_resource(
        "./resource%3A.bin", (dir / "caller.mdl").generic_string().c_str(),
        "::d4_import::file%253A::caller", 0, 0, nullptr));
    TF_AXIOM(resource);
    TF_AXIOM(Text(resource->get_mdl_file_path_mask()) == "/d4_import/file%3A/resource%3A.bin");
    Handle<IMdl_resolved_resource> again(recorder->resolve_resource(
        resource->get_mdl_file_path_mask(), nullptr, nullptr, 0, 0, nullptr));
    TF_AXIOM(again && fs::equivalent(again->get_filename_mask(), dir / "resource%3A.bin"));

    // Direct malformed callbacks cannot introduce traversal or separators
    // through decoding, even with a real target available to resolve.
    for (const char* bad : {"%2E%2E", "%2F", "%5C", "%3A", "%00"}) {
        const std::string name = "::d4_import::" + std::string(bad) + "::absolute";
        Handle<IMdl_resolved_module> result(recorder->resolve_module(
            name.c_str(), nullptr, nullptr, 0, 0, nullptr));
        TF_AXIOM(!result);
        const std::string relative = ".::" + std::string(bad) + "::absolute";
        Handle<IMdl_resolved_module> rel(recorder->resolve_module(relative.c_str(),
            (dir / "caller.mdl").generic_string().c_str(),
            "::d4_import::file%253A::caller", 0, 0, nullptr));
        TF_AXIOM(!rel);
    }
}

void Aliases(Runtime& runtime)
{
    auto recorder = runtime.Install(false);
    for (bool inside : {true, false}) {
        const fs::path file = (inside ? runtime.root : runtime.root.parent_path() / "outside")
            / "d4_alias" / "file%3A" / "identity.mdl";
        Write(file, source);
        const std::string native = file.generic_string();
        const std::string single = NormalizeResolvedUri(native);
        TF_AXIOM(single.compare(0, 6, "file:/") == 0);
        const std::string triple = "file:///" + single.substr(6);
        const std::vector<std::string> roots = {runtime.root.generic_string()};
        // Keep this transaction open: the second alias must encounter the
        // first module in the DB, rather than loading into a fresh cache.
        Handle<ITransaction> txn(runtime.scope->create_transaction());
        std::vector<Loaded> results;
        for (const auto& path : {native, single, triple}) {
            const auto name = EncodeUriToMdlName(path, roots);
            std::printf("ALIAS inside=%d path=%s name=%s\n", inside, path.c_str(), name.c_str());
            results.push_back(Load(runtime, txn.get(), name));
            TF_AXIOM(results.back().rc >= 0);
            TF_AXIOM(fs::equivalent(results.back().file, file) && results.back().value == 7.0f);
        }
        TF_AXIOM(results[0].rc == 0 && results[1].rc == 1);
        TF_AXIOM(results[0].db == results[1].db);
        TF_AXIOM(results[2].rc == (inside ? 1 : 0));
        TF_AXIOM((results[0].db == results[2].db) == inside);
        txn->abort();
    }
}

void CollisionSelectsDifferentFile(Runtime& runtime)
{
    const fs::path outside = runtime.root.parent_path() / "outside/d4_collision/identity.mdl";
    Write(outside, source);
    const std::string encoded = EncodeUriToMdlName(outside.generic_string(), {});

    // Mirror the encoder's public name as a real package under the MDL root.
    // The SDK escapes each literal '%' once when deriving its public name.
    fs::path inside = runtime.root;
    std::size_t start = 2;
    while (start < encoded.size()) {
        const auto end = encoded.find("::", start);
        std::string component = encoded.substr(start,
            end == std::string::npos ? end : end - start);
        for (std::size_t pos = 0; (pos = component.find("%25", pos)) != std::string::npos; ++pos)
            component.replace(pos, 3, "%");
        inside /= component;
        if (end == std::string::npos) break;
        start = end + 2;
    }
    inside += ".mdl";
    Write(inside, "mdl 1.8; export float probe(float x = 9.0) = x;\n");
    Handle<const mi::IString> natural(runtime.impexp->get_mdl_module_name(inside.generic_string().c_str()));
    TF_AXIOM(natural && encoded == natural->get_c_str());
    for (bool builtin : {false, true}) {
        auto recorder = runtime.Install(builtin);
        Handle<ITransaction> txn(runtime.scope->create_transaction());
        const auto loaded = Load(runtime, txn.get(), encoded);
        std::printf("COLLISION builtin=%d value=%.1f\n", builtin, loaded.value);
        TF_AXIOM(loaded.rc == 0 && loaded.value == (builtin ? 9.0f : 7.0f));
        TF_AXIOM(fs::equivalent(loaded.file, builtin ? inside : outside));
        txn->abort();
    }
    // Confirm that the public registration path also selects the outside file
    // when the authored asset names the existing file inside the search root.
    auto recorder = runtime.Install(false);
    const auto nodes = UsdMdlAddModuleToRegistry(SdfAssetPath(inside.generic_string()));
    TF_AXIOM(nodes.size() == 1 && nodes.front()->IsValid());
    const float value = nodes.front()->GetShaderInput(TfToken("x"))->GetDefaultValue().Get<float>();
    std::printf("COLLISION_REGISTER authored=%s value=%.1f\n", inside.generic_string().c_str(), value);
    TF_AXIOM(value == 7.0f);
}

void FilenameCallbacks(Runtime& runtime)
{
    auto recorder = runtime.Install(false);
    const fs::path file = runtime.root / "d4_requests" / "direct.mdl";
    Write(file, source);
    const std::string native = file.generic_string();
    // Existing assets must still be rejected when a caller bypasses the
    // loading entry point's asset-to-MDL-name conversion.
    for (const auto& request : {native, std::string("d4_requests/direct.mdl"),
                               std::string("./direct.mdl"), NormalizeResolvedUri(native)}) {
        Handle<IMdl_resolved_module> direct(recorder->resolve_module(
            request.c_str(), native.c_str(), "::d4_requests::direct", 0, 0, nullptr));
        TF_AXIOM(!direct);
        std::printf("DIRECT_FILENAME rejected=%s\n", request.c_str());
    }
    for (const auto& request : {native, std::string("d4_requests/direct.mdl"),
                               std::string("::d4_requests::direct")}) {
        recorder->requests.clear();
        Handle<ITransaction> txn(runtime.scope->create_transaction());
        const auto result = Load(runtime, txn.get(), request);
        std::printf("SDK_ARGUMENT request=%s callbacks=%zu rc=%d\n",
            request.c_str(), recorder->requests.size(), result.rc);
        const bool qualified = request.compare(0, 2, "::") == 0;
        TF_AXIOM(result.rc == (qualified ? 0 : -1));
        TF_AXIOM(recorder->requests.size() == (qualified ? 1u : 0u));
        txn->abort();
    }
    recorder->filenameRequests = 0;
    recorder->rejectFilenames = true;
    Write(runtime.root / "d4_absolute.mdl", "mdl 1.8; export const float marker = 9.0;\n");
    for (bool inside : {true, false}) {
        const fs::path dir = (inside ? runtime.root : runtime.root.parent_path() / "outside")
            / "d4_register";
        Write(dir / "sibling.mdl", "mdl 1.8; export const float marker = 7.0;\n");
        Write(dir / "caller.mdl", "mdl 1.8; import .::sibling::*; "
            "export float probe(float x = sibling::marker) = x;\n");
        const auto nodes = UsdMdlAddModuleToRegistry(SdfAssetPath((dir / "caller.mdl").generic_string()));
        std::printf("REGISTER inside=%d nodes=%zu filename-callbacks=%u\n",
            inside, nodes.size(), recorder->filenameRequests);
        TF_AXIOM(nodes.size() == 1 && nodes.front()->IsValid());
        TF_AXIOM(nodes.front()->GetShaderInput(TfToken("x"))->GetDefaultValue().Get<float>() == 7.0f);
        TF_AXIOM(recorder->filenameRequests == 0);
        TF_AXIOM(std::find(recorder->requests.begin(), recorder->requests.end(), ".::sibling")
            != recorder->requests.end());

        // A different module name avoids both the SDK load cache and Sdr's
        // registered node cache when exercising cold parser lookup.
        Write(dir / "cold.mdl", "mdl 1.8; import .::sibling::*; import ::d4_absolute::*; "
            "export float probe(float x = sibling::marker + d4_absolute::marker) = x;\n");
        recorder->requests.clear();
        const auto* cold = SdrRegistry::GetInstance().GetShaderNodeFromAsset(
            SdfAssetPath((dir / "cold.mdl").generic_string()), {}, TfToken("probe"), TfToken("mdl"));
        TF_AXIOM(cold && cold->IsValid());
        TF_AXIOM(cold->GetShaderInput(TfToken("x"))->GetDefaultValue().Get<float>() == 16.0f);
        TF_AXIOM(!recorder->requests.empty() && recorder->filenameRequests == 0);
        std::printf("COLD_LOOKUP inside=%d filename-callbacks=%u\n", inside, recorder->filenameRequests);
    }
    Handle<IMdl_entity_resolver> resolver(UsdMdlGetEntityResolver());
    runtime.config->set_entity_resolver(resolver.get());
}
} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    {
        Runtime runtime;
        PackageNames(runtime);
        PercentImportsAndResources(runtime);
        Aliases(runtime);
        CollisionSelectsDifferentFile(runtime);
        FilenameCallbacks(runtime);
    }
    UsdMdlShutdown();
}
