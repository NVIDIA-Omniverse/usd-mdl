//
// SPDX-License-Identifier: Apache-2.0
//

#include "RejectedResource.h"

#include <array>
#include <charconv>
#include <mutex>
#include <set>
#include <string_view>
#include <tuple>
#include <utility>

MDL_RUNTIME_NAMESPACE_OPEN
namespace {

constexpr std::string_view ReservedPrefix = "/__usdMdl_rejected__/";
constexpr std::string_view VersionPrefix = "/__usdMdl_rejected__/v1/";

std::string HexEncode(const std::string& value)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string encoded;
    for (const unsigned char byte : value) {
        encoded.push_back(digits[byte >> 4]);
        encoded.push_back(digits[byte & 15]);
    }
    return encoded;
}

int HexDigit(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

bool HexDecode(std::string_view encoded, std::string& value)
{
    if ((encoded.size() & 1) != 0) return false;
    value.reserve(encoded.size() / 2);
    for (std::size_t index = 0; index < encoded.size(); index += 2) {
        const int high = HexDigit(encoded[index]);
        const int low = HexDigit(encoded[index + 1]);
        if (high < 0 || low < 0) return false;
        const char byte = static_cast<char>((high << 4) | low);
        // SDK path/module inputs are C strings, so a NUL cannot be authored.
        if (byte == '\0') return false;
        value.push_back(byte);
    }
    return true;
}

bool ParsePosition(std::string_view value, mi::Sint32& position)
{
    if (value.empty()) return false;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), position);
    return result.ec == std::errc() && result.ptr == value.data() + value.size();
}

using RecordIdentity = std::tuple<std::string, std::string, std::string,
                                  mi::Sint32, mi::Sint32>;

class IRejectedResourceState
    : public mi::base::Interface_declare<0x57675f73, 0x5f43, 0x4ea7,
                                         0xb9, 0xdf, 0x39, 0x7c, 0x04, 0xa6, 0xa9, 0x22>
{
public:
    virtual void record(const RejectedResourceInfo& info) const = 0;
    virtual std::vector<RejectedResourceInfo> snapshot() const = 0;
};

class RejectedResourceState final
    : public mi::base::Interface_implement<IRejectedResourceState>
{
public:
    void record(const RejectedResourceInfo& info) const final
    {
        const RecordIdentity identity(info.ownerModule, info.ownerFile,
                                      info.resourcePath, info.line, info.column);
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_identities.insert(identity).second) {
            m_records.push_back(info);
        }
    }

    std::vector<RejectedResourceInfo> snapshot() const final
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_records;
    }

private:
    mutable std::mutex m_mutex;
    mutable std::set<RecordIdentity> m_identities;
    mutable std::vector<RejectedResourceInfo> m_records;
};

mi::base::Handle<const mi::base::IInterface> GetUserData(
    const mi::neuraylib::IMdl_execution_context* context)
{
    if (!context) return {};
    const mi::base::IInterface* data = nullptr;
    if (context->get_option("user_data", &data) != 0) return {};
    return mi::base::Handle<const mi::base::IInterface>(data);
}

mi::base::Handle<const IRejectedResourceState> GetState(
    const mi::neuraylib::IMdl_execution_context* context)
{
    const auto data = GetUserData(context);
    return mi::base::Handle<const IRejectedResourceState>(
        data ? data->get_interface<IRejectedResourceState>() : nullptr);
}

} // namespace

std::string EncodeRejectedResource(const RejectedResourceInfo& info)
{
    return std::string(VersionPrefix)
        + HexEncode(info.resourcePath) + "/"
        + HexEncode(info.ownerModule) + "/"
        + HexEncode(info.ownerFile) + "/"
        + HexEncode(info.reason) + "/"
        + std::to_string(info.line) + "/"
        + std::to_string(info.column);
}

bool IsRejectedResourcePath(const std::string& path)
{
    return path.compare(0, ReservedPrefix.size(), ReservedPrefix) == 0;
}

bool DecodeRejectedResource(const std::string& path, RejectedResourceInfo& info)
{
    const std::string_view marker(path);
    if (marker.substr(0, VersionPrefix.size()) != VersionPrefix) return false;
    std::array<std::string_view, 6> fields;
    std::size_t begin = VersionPrefix.size();
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const std::size_t end = marker.find('/', begin);
        if ((index + 1 < fields.size()) != (end != std::string_view::npos)) return false;
        fields[index] = marker.substr(begin, end == std::string_view::npos
            ? marker.size() - begin : end - begin);
        if (end != std::string_view::npos) begin = end + 1;
    }

    RejectedResourceInfo decoded;
    if (!HexDecode(fields[0], decoded.resourcePath)
        || !HexDecode(fields[1], decoded.ownerModule)
        || !HexDecode(fields[2], decoded.ownerFile)
        || !HexDecode(fields[3], decoded.reason)
        || !ParsePosition(fields[4], decoded.line)
        || !ParsePosition(fields[5], decoded.column)) {
        return false;
    }
    info = std::move(decoded);
    return true;
}

bool AttachRejectedResourceState(mi::neuraylib::IMdl_execution_context* context)
{
    if (!context) return false;
    const auto data = GetUserData(context);
    if (data) {
        const mi::base::Handle<const IRejectedResourceState> state(
            data->get_interface<IRejectedResourceState>());
        return static_cast<bool>(state);
    }
    const mi::base::Handle<IRejectedResourceState> state(new RejectedResourceState());
    return context->set_option("user_data", state.get()) == 0;
}

void RecordRejectedResource(const mi::neuraylib::IMdl_execution_context* context,
                            const RejectedResourceInfo& info)
{
    const auto state = GetState(context);
    if (state) state->record(info);
}

std::vector<RejectedResourceInfo> GetRejectedResourceRecords(
    const mi::neuraylib::IMdl_execution_context* context)
{
    const auto state = GetState(context);
    return state ? state->snapshot() : std::vector<RejectedResourceInfo>();
}

MDL_RUNTIME_NAMESPACE_CLOSE
