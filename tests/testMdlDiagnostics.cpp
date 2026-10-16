// SPDX-License-Identifier: Apache-2.0
// Real SDK diagnostics through Tf, including successful compilation and resources.
#include "Context.h"
#include "ModuleLoader.h"
#include "PluginLoader.h"

#include <mi/mdl_sdk.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/diagnosticMgr.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/base/tf/warning.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/base/vt/value.h>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
int failures = 0;
#define EXPECT(condition, message) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "[testMdlDiagnostics] FAIL: %s (line %d)\n", message, __LINE__); \
        ++failures; \
    } \
} while (0)

struct WarningRecord {
    VtDictionary payload;
    std::string text;
};

class DiagnosticCapture final : public TfDiagnosticMgr::Delegate {
public:
    DiagnosticCapture() { TfDiagnosticMgr::GetInstance().AddDelegate(this); }
    ~DiagnosticCapture() override { TfDiagnosticMgr::GetInstance().RemoveDelegate(this); }
    void IssueError(const TfError&) override { ++errors; }
    void IssueFatalError(const TfCallContext&, const std::string&) override { ++errors; }
    void IssueStatus(const TfStatus&) override { ++statuses; }
    void IssueWarning(const TfWarning& warning) override {
        const auto* payload = warning.GetInfo<VtDictionary>();
        std::lock_guard<std::mutex> lock(mutex);
        if (payload) {
            warnings.push_back({*payload, warning.GetCommentary()});
        } else {
            genericWarnings.push_back(warning.GetCommentary());
        }
    }
    std::vector<WarningRecord> Take() {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<WarningRecord> result;
        result.swap(warnings);
        return result;
    }
    std::vector<std::string> TakeGeneric() {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<std::string> result;
        result.swap(genericWarnings);
        return result;
    }
    std::atomic<int> errors{0};
    std::atomic<int> statuses{0};
private:
    std::mutex mutex;
    std::vector<WarningRecord> warnings;
    std::vector<std::string> genericWarnings;
};

class HostLogger final : public mi::base::Interface_implement<mi::base::ILogger> {
public:
    void message(mi::base::Message_severity severity, const char* category,
                 const char* text) override {
        message(severity, category, mi::base::Message_details(), text);
    }
    void message(mi::base::Message_severity severity, const char*,
                 const mi::base::Message_details&, const char* text) override {
        std::lock_guard<std::mutex> lock(mutex);
        messages.emplace_back(static_cast<int>(severity), text ? text : "");
    }
    bool Contains(const std::string& text) const {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& message : messages) {
            if (message.second.find(text) != std::string::npos) return true;
        }
        return false;
    }
private:
    mutable std::mutex mutex;
    std::vector<std::pair<int, std::string>> messages;
};

template<class T>
const T* Field(const VtDictionary& value, const char* key) {
    const auto it = value.find(key);
    return it != value.end() && it->second.IsHolding<T>()
        ? &it->second.UncheckedGet<T>() : nullptr;
}

void CheckPayload(const VtDictionary& value, const std::string& module, bool isNote = false) {
    const auto* name = Field<std::string>(value, "module");
    const auto* severity = Field<int>(value, "severity");
    const auto* kind = Field<int>(value, "kind");
    const auto* code = Field<int>(value, "code");
    const auto* text = Field<std::string>(value, "message");
    const auto* notes = Field<std::vector<VtValue>>(value, "notes");
    EXPECT(name && *name == module, "diagnostic must retain the requested module");
    EXPECT(severity && *severity >= 0 && *severity <= 5, "original SDK severity must be an integer");
    EXPECT(kind && *kind >= 0, "original SDK kind must be an integer");
    EXPECT(code, "original SDK code must be an integer");
    EXPECT(text && !text->empty(), "original message must be retained");
    EXPECT(notes, "notes must be an owned collection");
    if (!isNote) EXPECT(severity && *severity <= 2, "only warning/error/fatal must become Tf warnings");
    if (notes) {
        for (const auto& note : *notes) {
            EXPECT(note.IsHolding<VtDictionary>(), "each note must retain its own structured fields");
            if (note.IsHolding<VtDictionary>()) CheckPayload(note.UncheckedGet<VtDictionary>(), module, true);
        }
    }
}

void CheckUnique(const std::vector<WarningRecord>& records) {
    std::set<std::string> seen;
    for (const auto& record : records) {
        const auto* module = Field<std::string>(record.payload, "module");
        const auto* text = Field<std::string>(record.payload, "message");
        if (module && text) EXPECT(seen.insert(*module + "\n" + *text).second,
                                  "a context message must be forwarded once");
    }
}

void CheckLogger(mi::neuraylib::ILogging_configuration* logging, HostLogger* expected) {
    mi::base::Handle<mi::base::ILogger> current(logging->get_receiving_logger());
    EXPECT(current.get() == expected, "usdMdl must preserve the application's receiving logger");
}
} // namespace

int main() {
    DiagnosticCapture capture;
    TfErrorMark errors;
    const auto missingLibrary = (std::filesystem::absolute("missing_sdk")
        / (std::string("libmdl_sdk") + MI_BASE_DLL_FILE_EXT)).generic_string();
    EXPECT(!usdMdl::runtime::PluginLoader::loadNeuray(missingLibrary.c_str()),
           "missing SDK library must preserve null failure result");
    const auto loaderWarnings = capture.TakeGeneric();
    EXPECT(loaderWarnings.size() == 1, "SDK load failure must reach a native Tf delegate once");
    for (const auto& text : loaderWarnings) {
        EXPECT(text.find(missingLibrary) != std::string::npos, "loader warning must retain attempted path");
#ifdef MI_PLATFORM_WINDOWS
        EXPECT(text.find("Windows error") != std::string::npos, "loader warning must retain native error code");
#else
        EXPECT(text.find("dlopen failed") != std::string::npos, "loader warning must retain native loader details");
#endif
    }
    EXPECT(errors.IsClean(), "missing library warning must not introduce Tf errors");
    const auto mdlPath = std::filesystem::absolute("mdl").generic_string();
    const auto library = usdMdl::runtime::PluginLoader::sdkDllPathUnderPrefix(MDL_SDK_INSTALL_PREFIX);
    mi::base::Handle<mi::neuraylib::INeuray> neuray(
        usdMdl::runtime::PluginLoader::loadNeuray(library.c_str()));
    EXPECT(neuray, "host SDK must load");
    if (!neuray) return 1;
    mi::base::Handle<mi::neuraylib::ILogging_configuration> logging(
        neuray->get_api_component<mi::neuraylib::ILogging_configuration>());
    mi::base::Handle<HostLogger> logger(new HostLogger());
    EXPECT(logging, "SDK logging configuration must exist");
    if (!logging) return 1;
    logging->set_receiving_logger(logger.get());
    logging->set_log_level(mi::base::MESSAGE_SEVERITY_WARNING);
    // A real PNG decoder and a valid-image control distinguish corrupt bytes
    // from missing decoder support on this host-owned instance.
    {
        mi::base::Handle<mi::neuraylib::IPlugin_configuration> plugins(
            neuray->get_api_component<mi::neuraylib::IPlugin_configuration>());
        EXPECT(plugins, "host image-plugin configuration must exist");
        if (!plugins) return 1;
        const auto imagePlugin = (std::filesystem::path(library).parent_path()
            / (std::string("nv_openimageio") + MI_BASE_DLL_FILE_EXT)).generic_string();
        EXPECT(plugins->load_plugin_library(imagePlugin.c_str()) == 0,
               "host must load the PNG decoder before starting the SDK");
    }
    EXPECT(neuray->start() == 0, "host SDK must start");
    auto& context = usdMdl::runtime::Context::instance();
    EXPECT(context.initializeExternal(neuray.get(), {mdlPath}), "usdMdl must attach to host SDK");
    if (!context.isInitialized()) return 1;
    CheckLogger(logging.get(), logger.get());
    capture.Take();
    auto& loader = context.getModuleLoader();

    EXPECT(loader.loadModule("::clean") == 0, "clean module must load normally");
    EXPECT(capture.Take().empty(), "clean load must not emit Tf warnings");
    EXPECT(capture.statuses.load() == 0, "ordinary SDK information must stay quiet by default");

    EXPECT(loader.loadModule("::warning") == 0, "compiler warnings must not fail a load");
    const auto warningRecords = capture.Take();
    EXPECT(!warningRecords.empty(), "successful compilation warning must reach Tf");
    CheckUnique(warningRecords);
    bool compilerWarning = false;
    for (const auto& record : warningRecords) {
        CheckPayload(record.payload, "::warning");
        const auto* severity = Field<int>(record.payload, "severity");
        const auto* text = Field<std::string>(record.payload, "message");
        compilerWarning |= severity && *severity == mi::base::MESSAGE_SEVERITY_WARNING
            && text && text->find("warning.mdl(") != std::string::npos;
        if (text) EXPECT(record.text.find(*text) != std::string::npos,
                         "formatted warning must include the SDK source location and text");
    }
    EXPECT(compilerWarning, "warning must retain original compiler severity and source location");
    EXPECT(logger->Contains("warning.mdl"), "host logger must still receive compiler warnings");
    EXPECT(loader.loadModule("::warning") == 0, "cached success must preserve the loaded-set result");
    EXPECT(capture.Take().empty(), "cached load must not replay diagnostics");

    EXPECT(loader.loadModule("::invalid") < 0, "invalid module must retain native failure result");
    const auto invalidRecords = capture.Take();
    EXPECT(!invalidRecords.empty(), "compiler failure must reach Tf");
    CheckUnique(invalidRecords);
    bool compilerError = false;
    bool hasNotes = false;
    for (const auto& record : invalidRecords) {
        CheckPayload(record.payload, "::invalid");
        const auto* severity = Field<int>(record.payload, "severity");
        const auto* notes = Field<std::vector<VtValue>>(record.payload, "notes");
        compilerError |= severity && *severity == mi::base::MESSAGE_SEVERITY_ERROR;
        hasNotes |= notes && !notes->empty();
        if (notes) {
            for (const auto& note : *notes) {
                if (!note.IsHolding<VtDictionary>()) continue;
                const auto* noteText = Field<std::string>(note.UncheckedGet<VtDictionary>(), "message");
                if (noteText && !noteText->empty()) {
                    const auto first = record.text.find(*noteText);
                    EXPECT(first != std::string::npos, "compiler note must appear in its parent's diagnostic");
                    EXPECT(first == std::string::npos
                               || record.text.find(*noteText, first + noteText->size()) == std::string::npos,
                           "compiler note must be formatted once under its parent");
                }
            }
        }
    }
    EXPECT(compilerError, "failure must preserve original error severity without issuing a Tf error");
    EXPECT(hasNotes, "overload error must preserve attached compiler notes");

    EXPECT(std::filesystem::is_regular_file("mdl/resources/corrupt_diagnostic_texture.png"),
           "corrupt-texture coverage must use an existing file");
    EXPECT(loader.loadModule("::valid_resource") == 0,
           "valid texture control must load with the host's decoder");
    EXPECT(capture.Take().empty(), "valid texture must not produce resource warnings");

    struct ResourceCase { const char* module; const char* resource; };
    const ResourceCase resourceCases[] = {
        {"::missing_resource", "missing_diagnostic_texture.png"},
        {"::corrupt_resource", "corrupt_diagnostic_texture.png"},
        {"::malformed_resource", "resources/../valid_diagnostic_texture.png"}
    };
    for (const auto& test : resourceCases) {
        EXPECT(loader.loadModule(test.module) >= 0,
               "invalid resources must leave their module load successful");
        const auto resourceRecords = capture.Take();
        EXPECT(!resourceRecords.empty(), "resource failure must reach Tf as a warning");
        CheckUnique(resourceRecords);
        bool resourceWarning = false;
        for (const auto& record : resourceRecords) {
            CheckPayload(record.payload, test.module);
            const auto* severity = Field<int>(record.payload, "severity");
            const auto* kind = Field<int>(record.payload, "kind");
            const auto* text = Field<std::string>(record.payload, "message");
            const auto* display = Field<std::string>(record.payload, "displayMessage");
            const auto* resourcePath = Field<std::string>(record.payload, "resourcePath");
            const auto* visible = display ? display : text;
            // Do not depend on SDK-specific explanation text: it may describe
            // corrupt data as an unsupported image format. Require resource identity.
            resourceWarning |= severity && *severity == mi::base::MESSAGE_SEVERITY_WARNING
                && kind && *kind == mi::neuraylib::IMessage::MSG_INTEGRATION
                && visible && visible->find(test.resource) != std::string::npos;
            if (visible) EXPECT(record.text.find(*visible) != std::string::npos,
                                "formatted warning must retain the public resource diagnostic");
            if (resourcePath) {
                EXPECT(resourcePath->find(test.resource) != std::string::npos,
                       "rejection payload must retain the original authored path");
                EXPECT(text && text->find("/__usdMdl_rejected__/") != std::string::npos,
                       "original SDK message must remain available to native delegates");
                EXPECT(record.text.find("/__usdMdl_rejected__/") == std::string::npos,
                       "private identity must not appear in formatted USD warnings");
            }
        }
        EXPECT(resourceWarning, "resource warning must identify its module and resource");
        // The application logger receives the original SDK message. Rejected
        // resources use a private SDK identity; only Tf presentation translates
        // it back to the authored path. Do not replace or rewrite that logger.
        EXPECT(logger->Contains(std::string(test.module) == "::malformed_resource"
                    ? "/__usdMdl_rejected__/" : test.resource),
               "host logger must still receive the unmodified SDK resource warning");
        EXPECT(errors.IsClean() && capture.errors.load() == 0,
               "invalid resources must not introduce Tf errors");
        EXPECT(loader.loadModule(test.module) == 0, "permissive resource load must be cached");
        EXPECT(capture.Take().empty(), "cached resource load must not replay diagnostics");
    }

    // Each caller owns its context; concurrent batches must not mix diagnostics.
    const std::vector<std::string> names{"::warning_a", "::warning_b"};
    std::mutex startMutex;
    std::condition_variable startCv;
    int ready = 0;
    bool start = false;
    std::atomic<int> threadFailures{0};
    std::vector<std::thread> threads;
    for (const auto& name : names) {
        threads.emplace_back([&, name] {
            TfErrorMark threadErrors;
            {
                std::unique_lock<std::mutex> lock(startMutex);
                ++ready;
                startCv.notify_all();
                startCv.wait(lock, [&] { return start; });
            }
            if (loader.loadModule(name.c_str()) < 0 || !threadErrors.IsClean()) ++threadFailures;
        });
    }
    {
        std::unique_lock<std::mutex> lock(startMutex);
        startCv.wait(lock, [&] { return ready == static_cast<int>(names.size()); });
        start = true;
    }
    startCv.notify_all();
    for (auto& thread : threads) thread.join();
    EXPECT(threadFailures.load() == 0, "concurrent successful warnings must not introduce errors");
    const auto concurrentRecords = capture.Take();
    CheckUnique(concurrentRecords);
    for (const auto& name : names) {
        std::size_t count = 0;
        for (const auto& record : concurrentRecords) {
            const auto* module = Field<std::string>(record.payload, "module");
            if (module && *module == name) {
                ++count;
                CheckPayload(record.payload, name);
                const auto* text = Field<std::string>(record.payload, "message");
                EXPECT(text && text->find(name.substr(2) + ".mdl") != std::string::npos,
                       "concurrent warning must correspond to its own compiler source");
            }
        }
        EXPECT(count == warningRecords.size(), "each independent context must forward its warnings once");
        EXPECT(loader.loadModule(name.c_str()) == 0, "concurrent module must be committed and cached");
    }
    EXPECT(capture.Take().empty(), "concurrent cached modules must not replay warnings");
    CheckLogger(logging.get(), logger.get());
    EXPECT(errors.IsClean() && capture.errors.load() == 0, "forwarding must not post Tf errors");

    context.shutdown();
    EXPECT(neuray->get_status() == mi::neuraylib::INeuray::STARTED, "host ownership must remain unchanged");
    CheckLogger(logging.get(), logger.get());
    logging->set_receiving_logger(nullptr);
    logging.reset();
    logger.reset();
    EXPECT(neuray->shutdown() == 0, "host SDK must shut down normally");
    neuray.reset();
    std::fprintf(stderr, "[testMdlDiagnostics] %s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
