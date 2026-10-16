//
// SPDX-License-Identifier: Apache-2.0
//

#include "PathHelpers.h"
#include "UriEncoding.h"

#include <cctype>
#include <vector>

MDL_RUNTIME_NAMESPACE_OPEN
namespace path {

bool
IsWindowsDrivePath(const std::string& p)
{
    return p.size() >= 3
        && std::isalpha(static_cast<unsigned char>(p[0]))
        && p[1] == ':' && (p[2] == '/' || p[2] == '\\');
}

std::size_t
GetUriSchemeLength(const std::string& p)
{
    if (p.empty() || IsWindowsDrivePath(p)
        || !std::isalpha(static_cast<unsigned char>(p[0]))) {
        return 0;
    }
    const std::size_t colon = p.find(':');
    if (colon == std::string::npos) {
        return 0;
    }
    for (std::size_t i = 1; i < colon; ++i) {
        const unsigned char c = static_cast<unsigned char>(p[i]);
        if (!std::isalnum(c) && c != '+' && c != '-' && c != '.') {
            return 0;
        }
    }
    return colon;
}

bool
LooksLikeMdleContainer(const std::string& p)
{
    constexpr char marker[] = ".mdle:";
    if (p.size() < sizeof(marker) - 1) {
        return false;
    }
    for (std::size_t i = 0; i + sizeof(marker) - 1 <= p.size(); ++i) {
        bool matches = true;
        for (std::size_t j = 0; j < sizeof(marker) - 1; ++j) {
            const unsigned char c = static_cast<unsigned char>(p[i + j]);
            if (static_cast<char>(std::tolower(c)) != marker[j]) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return true;
        }
    }
    return false;
}

std::size_t
ResourceMarkerLength(const std::string& p, std::size_t position)
{
    if (position >= p.size() || p[position] != '<') {
        return 0;
    }
    for (const std::string marker : {"<UDIM>", "<UVTILE0>", "<UVTILE1>"}) {
        if (p.compare(position, marker.size(), marker) == 0) {
            return marker.size();
        }
    }
    std::size_t end = position + 1;
    while (end < p.size() && p[end] == '#') {
        ++end;
    }
    return end > position + 1 && end < p.size() && p[end] == '>'
        ? end - position + 1 : 0;
}

FrameMarker
FindFrameMarker(const std::string& p)
{
    for (std::size_t pos = p.find('<'); pos != std::string::npos;
         pos = p.find('<', pos + 1)) {
        const std::size_t length = ResourceMarkerLength(p, pos);
        if (length && p[pos + 1] == '#') {
            return {pos, length - 2};
        }
    }
    return {};
}

bool
HasFrameMarker(const std::string& p)
{
    return FindFrameMarker(p).digits != 0;
}

bool
HasTilingOrSequenceMarker(const std::string& p)
{
    if (p.find("<UDIM>") != std::string::npos
        || p.find("<UVTILE0>") != std::string::npos
        || p.find("<UVTILE1>") != std::string::npos) {
        return true;
    }
    return HasFrameMarker(p);
}

std::string
NormalizeMdlResourceFilename(const char* raw)
{
    if (!raw || !raw[0]) {
        return {};
    }

    std::string s(raw);
    if (s.front() == '/' || s.front() == '\\') {
        return s;
    }
    if (IsWindowsDrivePath(s)) {
        return s;
    }
    if (GetUriSchemeLength(s) != 0) {
        return s;
    }

    // Canonicalize relative MDL paths while adding the required root slash.
    // In particular, `/./foo` is not a valid absolute MDL file path.
    std::vector<std::string> segments;
    std::size_t start = 0;
    while (start <= s.size()) {
        const std::size_t end = s.find_first_of("/\\", start);
        const std::string segment = s.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        if (segment.empty() || segment == ".") {
            // Ignore duplicate separators and current-directory components.
        } else if (segment == "..") {
            if (segments.empty()) {
                return {};
            }
            segments.pop_back();
        } else {
            segments.push_back(segment);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    std::string result;
    for (const std::string& segment : segments) {
        result += "/" + segment;
    }
    // Inputs containing only separators/current-directory components do not
    // identify a resource.  Do not turn them into the MDL root directory.
    return result;
}

std::string
MdlRelativeModuleNameToFilePath(const char* mdlName)
{
    if (!mdlName || !mdlName[0]) {
        return {};
    }
    // Absolute: starts with "::"
    if (mdlName[0] == ':' && mdlName[1] == ':') {
        return {};
    }

    const std::string in(mdlName);
    std::vector<std::string> components;
    size_t start = 0;
    for (;;) {
        const size_t pos = in.find("::", start);
        const std::string component = in.substr(
            start, pos == std::string::npos ? std::string::npos : pos - start);
        if (component.empty()) {
            return {};
        }
        components.push_back(component);
        if (pos == std::string::npos) {
            break;
        }
        start = pos + 2;
    }

    if (components.size() < 2
        || (components.front() != "." && components.front() != "..")) {
        return {};
    }

    bool sawCurrent = false;
    size_t firstName = 0;
    for (; firstName < components.size(); ++firstName) {
        const std::string& component = components[firstName];
        if (component == ".") {
            if (firstName != 0) {
                return {};
            }
            sawCurrent = true;
        } else if (component == "..") {
            if (sawCurrent) {
                return {};
            }
        } else {
            break;
        }
    }
    if (firstName == components.size()) {
        return {};
    }

    for (size_t i = firstName; i < components.size(); ++i) {
        components[i] = DecodeMdlPathComponent(components[i]);
        if (components[i].empty()) {
            return {};
        }
    }

    std::string result;
    for (size_t i = 0; i < components.size(); ++i) {
        if (i > 0) {
            result += '/';
        }
        result += components[i];
    }
    return result + ".mdl";
}

} // namespace path
MDL_RUNTIME_NAMESPACE_CLOSE
