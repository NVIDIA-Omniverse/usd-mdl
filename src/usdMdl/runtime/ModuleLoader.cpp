//
// SPDX-License-Identifier: Apache-2.0
//

#include "ModuleLoader.h"

#include "Context.h"
#include "RejectedResource.h"
#include "../debugCodes.h"

#include <mi/mdl_sdk.h>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/vt/dictionary.h>

#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

MDL_RUNTIME_NAMESPACE_OPEN
namespace {

class MdlLoadingWaitHandle final
    : public mi::base::Interface_implement<mi::neuraylib::IMdl_loading_wait_handle>
{
public:
    void wait() final
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_cv.wait(lk, [this] { return m_ready; });
    }

    void notify(mi::Sint32 result_code) final
    {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_resultCode = result_code;
            m_ready = true;
        }
        m_cv.notify_all();
    }

    mi::Sint32 get_result_code() const final
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_resultCode;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_ready = false;
    mi::Sint32 m_resultCode{ 0 };
};

class MdlLoadingWaitHandleFactory final
    : public mi::base::Interface_implement<mi::neuraylib::IMdl_loading_wait_handle_factory>
{
public:
    mi::neuraylib::IMdl_loading_wait_handle* create_wait_handle() const final
    {
        return new MdlLoadingWaitHandle();
    }
};

void AddRejectedResourceFields(VtDictionary& record, const RejectedResourceInfo& rejection)
{
    record["resourcePath"] = VtValue(rejection.resourcePath);
    record["ownerModule"] = VtValue(rejection.ownerModule);
    record["ownerFile"] = VtValue(rejection.ownerFile);
    record["line"] = VtValue(static_cast<int>(rejection.line));
    record["column"] = VtValue(static_cast<int>(rejection.column));
    record["reason"] = VtValue(rejection.reason);
}

// Keep the raw SDK message in the payload, but show authored resource paths in
// warning/debug text. Marker data also survives a cached imported dependency.
std::string DisplayResourcePaths(const std::string& message,
                                std::vector<RejectedResourceInfo>& rejections)
{
    static const std::string prefix = "/__usdMdl_rejected__/";
    std::string result = message;
    std::size_t position = 0;
    while ((position = result.find(prefix, position)) != std::string::npos) {
        std::size_t end = result.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_/-", position);
        if (end == std::string::npos) {
            end = result.size();
        }
        RejectedResourceInfo rejection;
        std::string replacement = "<rejected resource>";
        if (DecodeRejectedResource(result.substr(position, end - position), rejection)) {
            replacement = rejection.resourcePath;
            rejections.push_back(std::move(rejection));
        }
        result.replace(position, end - position, replacement);
        position += replacement.size();
    }
    return result;
}

// Copy SDK messages before releasing their execution context. The dictionaries
// also give native diagnostic delegates access to the original SDK fields.
VtDictionary CaptureMessage(const mi::neuraylib::IMessage& message,
                            const std::string& module)
{
    VtDictionary record;
    record["module"] = VtValue(module);
    record["severity"] = VtValue(static_cast<int>(message.get_severity()));
    record["kind"] = VtValue(static_cast<int>(message.get_kind()));
    record["code"] = VtValue(static_cast<int>(message.get_code()));
    const char* const text = message.get_string();
    const std::string rawMessage(text ? text : "");
    record["message"] = VtValue(rawMessage);
    std::vector<RejectedResourceInfo> rejections;
    const std::string displayMessage = DisplayResourcePaths(rawMessage, rejections);
    if (displayMessage != rawMessage) {
        record["displayMessage"] = VtValue(displayMessage);
    }
    if (!rejections.empty()) {
        AddRejectedResourceFields(record, rejections.front());
        std::set<std::string> identities;
        std::vector<VtValue> rejectedResources;
        for (const auto& rejection : rejections) {
            if (identities.insert(EncodeRejectedResource(rejection)).second) {
                VtDictionary details;
                AddRejectedResourceFields(details, rejection);
                rejectedResources.emplace_back(std::move(details));
            }
        }
        record["rejectedResources"] = VtValue(std::move(rejectedResources));
    }

    std::vector<VtValue> notes;
    for (mi::Size i = 0, count = message.get_notes_count(); i < count; ++i) {
        mi::base::Handle<const mi::neuraylib::IMessage> note(message.get_note(i));
        if (note) {
            notes.emplace_back(CaptureMessage(*note, module));
        }
    }
    record["notes"] = VtValue(std::move(notes));
    return record;
}

const char* SeverityName(int severity)
{
    switch (severity) {
    case mi::base::MESSAGE_SEVERITY_FATAL: return "fatal";
    case mi::base::MESSAGE_SEVERITY_ERROR: return "error";
    case mi::base::MESSAGE_SEVERITY_WARNING: return "warning";
    case mi::base::MESSAGE_SEVERITY_INFO: return "info";
    case mi::base::MESSAGE_SEVERITY_VERBOSE: return "verbose";
    case mi::base::MESSAGE_SEVERITY_DEBUG: return "debug";
    default: return "unknown";
    }
}

void FormatRejectionDetails(std::ostringstream& text, const VtDictionary& record)
{
    text << " [" << record.find("ownerModule")->second.UncheckedGet<std::string>()
         << ": " << record.find("ownerFile")->second.UncheckedGet<std::string>()
         << "(" << record.find("line")->second.UncheckedGet<int>()
         << "," << record.find("column")->second.UncheckedGet<int>() << ") "
         << record.find("reason")->second.UncheckedGet<std::string>() << "]";
}

void FormatMessage(std::ostringstream& text, const VtDictionary& record,
                   const std::string& indent = std::string())
{
    const int severity = record.find("severity")->second.UncheckedGet<int>();
    text << indent << "[load_module " << record.find("module")->second.UncheckedGet<std::string>()
         << "] severity=" << SeverityName(severity)
         << " kind=" << record.find("kind")->second.UncheckedGet<int>()
         << " code=" << record.find("code")->second.UncheckedGet<int>();
    const auto display = record.find("displayMessage");
    text << " " << (display != record.end() ? display : record.find("message"))
        ->second.UncheckedGet<std::string>();
    const auto rejections = record.find("rejectedResources");
    if (rejections != record.end()) {
        for (const VtValue& value : rejections->second.UncheckedGet<std::vector<VtValue>>()) {
            FormatRejectionDetails(text, value.UncheckedGet<VtDictionary>());
        }
    } else if (record.find("reason") != record.end()) {
        FormatRejectionDetails(text, record);
    }
    for (const VtValue& note : record.find("notes")->second.UncheckedGet<std::vector<VtValue>>()) {
        text << "\n" << indent << "  note: ";
        FormatMessage(text, note.UncheckedGet<VtDictionary>(), indent + "  ");
    }
}

bool IsWarningSeverity(int severity)
{
    return severity == mi::base::MESSAGE_SEVERITY_FATAL
        || severity == mi::base::MESSAGE_SEVERITY_ERROR
        || severity == mi::base::MESSAGE_SEVERITY_WARNING;
}

using MessageIdentity = std::tuple<int, int, int, std::string>;

MessageIdentity GetMessageIdentity(const VtDictionary& record)
{
    return std::make_tuple(
        record.find("severity")->second.UncheckedGet<int>(),
        record.find("kind")->second.UncheckedGet<int>(),
        record.find("code")->second.UncheckedGet<int>(),
        record.find("message")->second.UncheckedGet<std::string>());
}

void CollectNoteIdentities(const VtDictionary& record,
                           std::set<MessageIdentity>& identities)
{
    for (const VtValue& note : record.find("notes")->second.UncheckedGet<std::vector<VtValue>>()) {
        const auto& noteRecord = note.UncheckedGet<VtDictionary>();
        identities.insert(GetMessageIdentity(noteRecord));
        CollectNoteIdentities(noteRecord, identities);
    }
}

void CollectReportedRejections(const VtDictionary& record,
                               std::set<std::string>& reported)
{
    const auto resources = record.find("rejectedResources");
    if (resources != record.end()) {
        for (const VtValue& value : resources->second.UncheckedGet<std::vector<VtValue>>()) {
            const auto& fields = value.UncheckedGet<VtDictionary>();
            RejectedResourceInfo rejection;
            rejection.resourcePath = fields.find("resourcePath")->second.UncheckedGet<std::string>();
            rejection.ownerModule = fields.find("ownerModule")->second.UncheckedGet<std::string>();
            rejection.ownerFile = fields.find("ownerFile")->second.UncheckedGet<std::string>();
            rejection.reason = fields.find("reason")->second.UncheckedGet<std::string>();
            rejection.line = fields.find("line")->second.UncheckedGet<int>();
            rejection.column = fields.find("column")->second.UncheckedGet<int>();
            reported.insert(EncodeRejectedResource(rejection));
        }
    }
    for (const VtValue& note : record.find("notes")->second.UncheckedGet<std::vector<VtValue>>()) {
        CollectReportedRejections(note.UncheckedGet<VtDictionary>(), reported);
    }
}

void ForwardLoadDiagnostics(const mi::neuraylib::IMdl_execution_context& context,
                            const std::string& module)
{
    // The error-only list is a subset of this list. Visiting both would duplicate
    // diagnostics. Notes stay attached even when their own severity is INFO.
    const bool debugEnabled = TfDebug::IsEnabled(USD_MDL_DIAGNOSTICS);
    std::vector<VtDictionary> records;
    std::set<MessageIdentity> attachedNotes;
    std::set<std::string> reportedRejections;
    for (mi::Size i = 0, count = context.get_messages_count(); i < count; ++i) {
        mi::base::Handle<const mi::neuraylib::IMessage> message(context.get_message(i));
        if (!message) {
            continue;
        }
        if (!IsWarningSeverity(message->get_severity()) && !debugEnabled) {
            continue;
        }
        records.emplace_back(CaptureMessage(*message, module));
        CollectNoteIdentities(records.back(), attachedNotes);
        if (IsWarningSeverity(message->get_severity())) {
            CollectReportedRejections(records.back(), reportedRejections);
        }
    }

    // Resolver warnings can be dropped by the compiler's intermediate
    // contexts. Supply any missing original rejection from our per-load state,
    // after SDK loading and transaction bookkeeping have finished.
    for (const auto& rejection : GetRejectedResourceRecords(&context)) {
        if (!reportedRejections.insert(EncodeRejectedResource(rejection)).second) {
            continue;
        }
        VtDictionary record;
        record["module"] = VtValue(module);
        record["severity"] = VtValue(static_cast<int>(mi::base::MESSAGE_SEVERITY_WARNING));
        record["kind"] = VtValue(static_cast<int>(mi::neuraylib::IMessage::MSG_INTEGRATION));
        record["code"] = VtValue(0);
        record["origin"] = VtValue(std::string("resolver"));
        record["message"] = VtValue(std::string("Rejected MDL resource \"")
            + rejection.resourcePath + "\".");
        record["notes"] = VtValue(std::vector<VtValue>());
        AddRejectedResourceFields(record, rejection);
        records.push_back(std::move(record));
    }

    for (const VtDictionary& record : records) {
        const bool warn = IsWarningSeverity(record.find("severity")->second.UncheckedGet<int>());
        // The SDK also exposes compiler notes as standalone INFO messages.
        // Print those copies only with their parent. This is scoped to this
        // execution context; independent warning/error parents are never folded.
        if (!warn && attachedNotes.count(GetMessageIdentity(record))) {
            continue;
        }
        std::ostringstream text;
        FormatMessage(text, record);
        if (warn) {
            // Preserve the existing warning-and-return-result contract. Tf errors
            // would introduce Python exceptions; a fatal Tf diagnostic would abort.
            TF_WARN(TfDiagnosticInfo(record), TF_DIAGNOSTIC_WARNING_TYPE, text.str());
        } else {
            TF_DEBUG(USD_MDL_DIAGNOSTICS).Msg("%s\n", text.str().c_str());
        }
    }
}

} // namespace

ModuleLoader::ModuleLoader(Context& context)
    : m_context(context)
{
    m_waitHandleFactory = mi::base::make_handle(new MdlLoadingWaitHandleFactory());
}

ModuleLoader::~ModuleLoader()
{
    std::lock_guard<std::mutex> lock(m_loadMutex);
    if (m_loadingBatch && m_loadingBatch->transaction
        && m_loadingBatch->transaction->is_open()) {
        // Exceptional: loader destroyed while loads were still pending.
        m_loadingBatch->transaction->abort();
    }
    if (m_loadingBatch) {
        m_loadingBatch->transaction.reset();
        m_loadingBatch->activeLoadCount = 0;
        m_loadingBatch->commitOk = false;
        m_loadingBatch->completed = true;
        m_loadingBatch.reset();
        m_loadCv.notify_all();
    }
}

mi::base::Handle<mi::neuraylib::IMdl_execution_context>
ModuleLoader::createExecutionContext() const
{
    mi::base::Handle<mi::neuraylib::IMdl_execution_context> ctx(
        m_context.getMdlFactory()->create_execution_context());
    if (!ctx) {
        return {};
    }
    // Resource resolution is enabled by default in the SDK. It calls our
    // Ar-backed entity resolver (see EntityResolver.cpp) for every resource
    // referenced from MDL source, honoring MDL search-path and relative rules.
    // Image plugins configured by the SDK instance's owner before startup
    // decode the resolved bytes.
    //
    // Resources referenced from USD layers (Sdf authoring) continue
    // to flow through Ar directly without involving the SDK.
    //
    // Note: VDB and Ptex resources cannot be loaded - the SDK does
    // not ship plugins for those formats.  See docs/ar-resolver.md
    // "Limitations" for details.
    ctx->set_option("internal_space", "coordinate_world");
    // The wait-handle factory is the SDK's mechanism for serializing
    // concurrent `load_module` calls that race on the same module: late
    // arrivals block on a wait handle until the first thread's load
    // completes.  Without it, concurrent loads on a shared transaction
    // would race inside the SDK.
    ctx->set_option("loading_wait_handle_factory", m_waitHandleFactory.get());
    // Collection enriches warnings for compiler-folded resources. The marker
    // itself preserves rejection even if optional user_data is unavailable.
    AttachRejectedResourceState(ctx.get());
    return ctx;
}

mi::Sint32
ModuleLoader::loadModule(const char* qualifiedModuleName)
{
    if (!qualifiedModuleName || !qualifiedModuleName[0]) {
        return -1;
    }

    {
        std::shared_lock<std::shared_mutex> lk(m_modulesMutex);
        if (m_loadedModules.find(qualifiedModuleName) != m_loadedModules.end()) {
            return 0;
        }
    }

    std::string const nameCopy = qualifiedModuleName;

    std::shared_ptr<LoadingBatch> batch;
    {
        std::unique_lock<std::mutex> lk(m_loadMutex);
        if (!m_loadingBatch) {
            const auto& scope = m_context.getScope();
            if (!scope) {
                return -2;
            }
            auto newBatch = std::make_shared<LoadingBatch>();
            newBatch->transaction = mi::base::Handle<mi::neuraylib::ITransaction>(
                scope->create_transaction());
            if (!newBatch->transaction) {
                return -2;
            }
            m_loadingBatch = std::move(newBatch);
        }
        batch = m_loadingBatch;
        ++batch->activeLoadCount;
    }

    mi::base::Handle<mi::neuraylib::IMdl_execution_context> ctx(createExecutionContext());
    if (!ctx) {
        std::unique_lock<std::mutex> lk(m_loadMutex);
        --batch->activeLoadCount;
        if (batch->activeLoadCount == 0 && batch->transaction) {
            // Exceptional: no in-flight loads succeeded; nothing to commit.
            batch->transaction->abort();
            batch->transaction.reset();
            batch->commitOk = false;
            batch->completed = true;
            if (m_loadingBatch == batch) {
                m_loadingBatch.reset();
            }
            lk.unlock();
            m_loadCv.notify_all();
        }
        if (lk.owns_lock()) {
            lk.unlock();
        }
        TF_WARN("ModuleLoader: create_execution_context failed.");
        return -2;
    }

    mi::Sint32 const res = m_context.getMdlImpExpApi()->load_module(
        batch->transaction.get(), nameCopy.c_str(), ctx.get());

    bool commitOk = true;
    mi::Sint32 commitResult = 0;
    {
        std::unique_lock<std::mutex> lk(m_loadMutex);
        --batch->activeLoadCount;
        if (batch->activeLoadCount == 0) {
            if (batch->transaction) {
                mi::Sint32 const c = batch->transaction->commit();
                if (c != 0) {
                    commitResult = c;
                    commitOk = false;
                }
                batch->transaction.reset();
            }
            batch->commitOk = commitOk;
            batch->completed = true;
            if (m_loadingBatch == batch) {
                m_loadingBatch.reset();
            }
            lk.unlock();
            m_loadCv.notify_all();
        } else {
            m_loadCv.wait(lk, [&batch] { return batch->completed; });
            commitOk = batch->commitOk;
        }
    }

    if (res >= 0 && commitOk) {
        std::unique_lock<std::shared_mutex> lk(m_modulesMutex);
        m_loadedModules.insert(nameCopy);
    }

    if (commitResult != 0) {
        TF_WARN("ModuleLoader: commit of loading transaction failed (%d).",
                static_cast<int>(commitResult));
    }

    // Keep the per-call context alive across batch completion, then copy and
    // forward its messages outside loader locks. Diagnostic delegates must not
    // observe an unfinished batch, and reporting allocations cannot strand it.
    ForwardLoadDiagnostics(*ctx, nameCopy);

    return (res >= 0 && !commitOk) ? mi::Sint32(-2) : res;
}

MDL_RUNTIME_NAMESPACE_CLOSE
