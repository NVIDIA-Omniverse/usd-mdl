//
// SPDX-License-Identifier: Apache-2.0
//

#include "UriEncoding.h"
#include "PathHelpers.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

MDL_RUNTIME_NAMESPACE_OPEN

namespace {

constexpr const char* kSep = "::";
constexpr size_t       kSepLen = 2;
// Two-pass percent encoding for ':' so the SDK's internal one-pass
// percent-decode produces `%3A` (which avoids `:::` in segment
// boundaries) rather than `:` directly.  See the doc on
// EncodeUriToMdlName for the full rationale.
constexpr const char* kPctColon = "%253A";
constexpr size_t       kPctColonLen = 5;
constexpr const char* kPctPercent = "%2525";
constexpr const char* kPctSlash = "%252F";
constexpr size_t       kPctSlashLen = 5;
constexpr const char* kMdlExt = ".mdl";
constexpr size_t       kMdlExtLen = 4;

std::string
_NormalizeSlashes(std::string s)
{
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

std::string
_StripMdlExt(std::string s)
{
    if (s.size() >= kMdlExtLen
        && s.compare(s.size() - kMdlExtLen, kMdlExtLen, kMdlExt) == 0) {
        s.resize(s.size() - kMdlExtLen);
    }
    return s;
}

std::string
_PercentEncodeReserved(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if (c == ':') {
            out.append(kPctColon);
        } else if (c == '%') {
            // Protect literal URI escapes through both possible decoder
            // passes (our own and the SDK's internal pass).
            out.append(kPctPercent);
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string
_PercentDecode(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()
            && std::isxdigit(static_cast<unsigned char>(s[i + 1]))
            && std::isxdigit(static_cast<unsigned char>(s[i + 2])))
        {
            auto hex = [](char c) {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return c - 'A' + 10;
            };
            const int hi = hex(s[i + 1]);
            const int lo = hex(s[i + 2]);
            out.push_back(static_cast<char>((hi << 4) | lo));
            i += 2;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// The first component contains only the scheme and its exact slash prefix.
// Requiring that shape keeps ordinary later components out of URI detection.
std::string
_DecodeUriPrefix(const std::string& first)
{
    const std::size_t colon = first.find(kPctColon);
    if (colon == std::string::npos || colon == 0
        || path::GetUriSchemeLength(first.substr(0, colon) + ":") != colon) {
        return {};
    }
    std::string prefix = first.substr(0, colon) + ':';
    for (std::size_t pos = colon + kPctColonLen; pos < first.size(); pos += kPctSlashLen) {
        if (first.compare(pos, kPctSlashLen, kPctSlash) != 0) {
            return {};
        }
        prefix += '/';
    }
    return prefix;
}

// Ordinary MDL names cross one SDK encoding boundary. URI identities above
// have an extra layer to protect URI escapes; using that layer here would
// change literal filesystem names such as file%3A into file%253A.
std::string
_EncodeMdlPath(const std::string& path)
{
    constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : path) {
        // IModule's public-name character set (not URI escaping).
        if (c && std::strchr("()<>,:$#?@%", c)) {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

/// Given a `/`-separated path with `:` already percent-encoded, produce
/// `::<seg>::<seg>::<seg>` (drops empty segments produced by `/` or
/// `//`).
std::string
_PathToMdlName(const std::string& slashPath)
{
    std::string out;
    size_t pos = 0;
    while (pos < slashPath.size()) {
        const size_t next = slashPath.find('/', pos);
        const std::string seg = (next == std::string::npos)
            ? slashPath.substr(pos)
            : slashPath.substr(pos, next - pos);
        if (!seg.empty()) {
            out.append(kSep);
            out.append(seg);
        }
        if (next == std::string::npos) {
            break;
        }
        pos = next + 1;
    }
    return out;
}

/// Case-fold comparison of two characters.  On Windows the filesystem
/// is case-insensitive (and drive-letter casing in particular is not
/// preserved by every USD layer), so search-path prefix matching has
/// to be case-insensitive there.  POSIX filesystems are case-sensitive
/// and must stay so.
bool
_PathCharEqual(char a, char b)
{
#ifdef _WIN32
    auto fold = [](unsigned char c) -> unsigned char {
        return static_cast<unsigned char>(std::tolower(c));
    };
    return fold(static_cast<unsigned char>(a))
        == fold(static_cast<unsigned char>(b));
#else
    return a == b;
#endif
}

/// True if \p path begins with \p prefix (compared with `_PathCharEqual`)
/// and the next character is `/` (i.e. \p prefix names a directory
/// boundary).  Trailing `/` on \p prefix is tolerated.
bool
_IsAncestorPath(const std::string& path, const std::string& prefix)
{
    std::string p = prefix;
    if (!p.empty() && p.back() == '/') {
        p.pop_back();
    }
    if (p.empty() || path.size() <= p.size() + 1) {
        return false;
    }
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (!_PathCharEqual(path[i], p[i])) {
            return false;
        }
    }
    return path[p.size()] == '/';
}

/// Strip \p prefix and the directory boundary `/` from \p path.
/// Pre: `_IsAncestorPath(path, prefix)` returned true.
std::string
_StripAncestor(const std::string& path, const std::string& prefix)
{
    std::string p = prefix;
    if (!p.empty() && p.back() == '/') {
        p.pop_back();
    }
    return path.substr(p.size() + 1);
}

} // namespace

namespace {

/// Decode local file URIs; leave other identifiers unchanged.
/// Used to compare URLs and search paths in a single representation.
std::string
_StripFileScheme(const std::string& s)
{
    const std::string native = FileUriToPath(s);
    return native.empty() ? s : native;
}

/// True if \p s is an absolute path we should treat as `file:` for
/// long-form encoding: POSIX `/foo` or Windows drive-letter `X:/foo`.
bool
_IsAbsoluteFsPath(const std::string& s)
{
    if (s.empty()) {
        return false;
    }
    if (s.front() == '/') {
        return true;
    }
    return path::IsWindowsDrivePath(s);
}

/// Encode filename bytes, leaving MDL markers recognizable to the SDK.
std::string
_EscapeFilePath(const std::string& s)
{
    constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (const std::size_t marker = path::ResourceMarkerLength(s, i)) {
            out.append(s, i, marker);
            i += marker - 1;
            continue;
        }
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9')
            || (c && std::strchr("-._~!$&'()*+,;=:@/", c))) {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

} // namespace

std::string
NormalizeResolvedUri(const std::string& resolvedPath)
{
    if (resolvedPath.empty() || path::GetUriSchemeLength(resolvedPath)) {
        return resolvedPath;
    }

    std::string normalized = resolvedPath;
#ifdef _WIN32
    normalized = _NormalizeSlashes(normalized);
#else
    if (path::IsWindowsDrivePath(normalized)) {
        normalized = _NormalizeSlashes(normalized);
    }
#endif
    if (_IsAbsoluteFsPath(normalized)) {
        return normalized.front() == '/'
            ? "file:" + _EscapeFilePath(normalized)
            : "file:/" + _EscapeFilePath(normalized);
    }

    return normalized;
}

std::string
FileUriToPath(const std::string& uri)
{
    if (path::GetUriSchemeLength(uri) != 4) {
        return {};
    }
    for (std::size_t i = 0; i < 4; ++i) {
        if (std::tolower(static_cast<unsigned char>(uri[i])) != "file"[i]) {
            return {};
        }
    }
    std::string filePath = uri.substr(5);
    if (filePath.compare(0, 2, "//") == 0) {
        if (filePath.size() < 3 || filePath[2] != '/') {
            return {}; // No server/UNC fallback.
        }
        filePath.erase(0, 2);
    }
    if (filePath.empty() || filePath.front() != '/') {
        return {};
    }
    for (std::size_t i = 0; i < filePath.size(); ++i) {
        if (const std::size_t marker = path::ResourceMarkerLength(filePath, i)) {
            i += marker - 1;
        } else if (filePath[i] == '%') {
            if (i + 2 >= filePath.size()
                || !std::isxdigit(static_cast<unsigned char>(filePath[i + 1]))
                || !std::isxdigit(static_cast<unsigned char>(filePath[i + 2]))) {
                return {};
            }
            i += 2;
        } else if (filePath[i] == '?' || filePath[i] == '#') {
            return {};
        }
    }
    filePath = _PercentDecode(filePath);
    if (filePath.find('\0') != std::string::npos) {
        return {};
    }
#ifdef _WIN32
    if (path::IsWindowsDrivePath(filePath.substr(1))) {
        filePath.erase(0, 1);
    }
#endif
    return filePath;
}

std::string
EncodeUriToMdlName(
    const std::string& canonicalUrl,
    const std::vector<std::string>& searchPaths)
{
    if (canonicalUrl.empty()) {
        return {};
    }

    const std::string url = NormalizeResolvedUri(canonicalUrl);

    // (1) Shortened form: URI lives under one of the configured MDL
    //     search paths.  Compare both sides as filesystem-style paths
    //     (strip any leading `file:`), so a `file:` URL still matches
    //     a plain-path search-path entry.
    const std::string urlAsFsPath = _StripFileScheme(url);
    for (const std::string& sp : searchPaths) {
        const std::string spFs = _StripFileScheme(_NormalizeSlashes(sp));
        if (_IsAncestorPath(urlAsFsPath, spFs)) {
            const std::string rel = _StripAncestor(urlAsFsPath, spFs);
            return _PathToMdlName(_EncodeMdlPath(_StripMdlExt(rel)));
        }
    }

    const std::string stripped = _StripMdlExt(url);
    if (path::GetUriSchemeLength(stripped)) {
        // Share the prefix and escaping rules with resources so zero, one,
        // and multiple slashes cannot acquire different meanings by API.
        return _PathToMdlName(EncodeUriToMdlResourcePath(stripped));
    }
    return _PathToMdlName(_PercentEncodeReserved(stripped));
}

std::string
EncodeUriToMdlResourcePath(
    const std::string& canonicalUrl)
{
    const std::string uri = NormalizeResolvedUri(canonicalUrl);
    const std::size_t colon = path::GetUriSchemeLength(uri);
    if (!colon) {
        return {};
    }
    std::string out = "/" + _PercentEncodeReserved(uri.substr(0, colon + 1));
    std::size_t pos = colon + 1;
    while (pos < uri.size() && uri[pos] == '/') {
        out += kPctSlash;
        ++pos;
    }
    if (pos < uri.size()) {
        // This separator divides MDL components; it is not a URI slash.
        out += '/';
    }
    for (; pos < uri.size(); ++pos) {
        const char c = uri[pos];
        if (c == '/' && (pos + 1 == uri.size() || uri[pos + 1] == '/')) {
            out += kPctSlash; // Preserve empty components without empty MDL components.
        } else if (c == '\\') {
            out += "%255C";
        } else if (c == '.' && (pos == 0 || uri[pos - 1] == '/')
            && (uri.compare(pos, 2, "./") == 0 || uri.compare(pos, 3, "../") == 0
                || pos + 1 == uri.size() || uri.substr(pos) == "..")) {
            out += "%252E"; // Do not introduce MDL parent/current-directory components.
        } else {
            out += _PercentEncodeReserved(std::string(1, c));
        }
    }
    return out;
}

bool
IsEncodedMdlName(const char* name)
{
    if (!name) {
        return false;
    }
    return IsEncodedMdlName(std::string(name));
}

std::string
DecodeMdlPathComponent(const std::string& component)
{
    const std::string decoded = _PercentDecode(component);
    // Validate after decoding so escapes cannot introduce path separators or
    // traversal. Literal "%2E" is encoded as "%252E" and stays a filename.
    if (decoded.empty() || decoded == "." || decoded == ".."
        || decoded.find_first_of(":/\\") != std::string::npos
        || decoded.find('\0') != std::string::npos) {
        return {};
    }
    return decoded;
}

bool
IsEncodedMdlName(const std::string& name)
{
    // Must start with the exact public-API form minted by the encoder. The MDL
    // SDK decodes names at the API-to-core boundary and re-encodes them before
    // invoking an external IMdl_entity_resolver, so callbacks retain `%253A`.
    if (name.size() < kSepLen + kPctColonLen + kSepLen) {
        return false;
    }
    if (name.compare(0, kSepLen, kSep) != 0) {
        return false;
    }
    const size_t firstSep = name.find(kSep, kSepLen);
    if (firstSep == std::string::npos) {
        return false;
    }
    return !_DecodeUriPrefix(name.substr(kSepLen, firstSep - kSepLen)).empty();
}

std::string
DecodeMdlNameToUri(const std::string& name)
{
    if (!IsEncodedMdlName(name)) {
        return {};
    }

    // Module and resource identities share the URI encoding. Only the MDL
    // separators and the omitted module extension differ.
    std::string resourcePath = name;
    for (std::size_t pos = 0;
         (pos = resourcePath.find(kSep, pos)) != std::string::npos; ++pos) {
        resourcePath.replace(pos, kSepLen, "/");
    }
    const std::string uri = DecodeMdlResourcePathToUri(resourcePath);
    return uri.empty() ? std::string() : uri + kMdlExt;
}

std::string
DecodeMdlResourcePathToUri(const std::string& path)
{
    if (path.empty() || path.front() != '/') {
        return {};
    }
    const std::size_t slash = path.find('/', 1);
    const std::string first = path.substr(1, slash == std::string::npos ? slash : slash - 1);
    std::string uri = _DecodeUriPrefix(first);
    if (uri.empty()) {
        return {};
    }
    if (slash != std::string::npos) {
        uri += _PercentDecode(_PercentDecode(path.substr(slash + 1)));
    }
    return uri;
}

MDL_RUNTIME_NAMESPACE_CLOSE
