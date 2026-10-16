//
// SPDX-License-Identifier: Apache-2.0
//

#include "EntityResolver.h"

#include "PathHelpers.h"
#include "RejectedResource.h"
#include "TileExpansion.h"
#include "UriEncoding.h"

#include <mi/mdl_sdk.h>

#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/resolvedPath.h>
#include <pxr/usd/ar/resolver.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

MDL_RUNTIME_NAMESPACE_OPEN

namespace {

// ---------------------------------------------------------------------------
// ArAssetReader: implements mi::neuraylib::IReader on top of an ArAsset's
// in-memory buffer (obtained once via ArAsset::GetBuffer()).  Supports both
// absolute access and lookahead.  Recorded access is not supported because
// the SDK rarely requires it and it would force us to implement
// IStream_position.
// ---------------------------------------------------------------------------
class ArAssetReader final
    : public mi::base::Interface_implement<mi::neuraylib::IReader>
{
public:
    ArAssetReader(std::shared_ptr<const char> buffer, mi::Size size)
        : m_buffer(std::move(buffer))
        , m_size(size)
        , m_pos(0)
    {
    }

    // ---- IReader_writer_base ----
    mi::Sint32 get_error_number() const final { return 0; }
    const char* get_error_message() const final { return nullptr; }
    bool eof() const final { return m_pos >= m_size; }
    mi::Sint32 get_file_descriptor() const final { return -1; }

    bool supports_recorded_access() const final { return false; }
    const mi::neuraylib::IStream_position* tell_position() const final { return nullptr; }
    bool seek_position(const mi::neuraylib::IStream_position*) final { return false; }
    bool rewind() final { m_pos = 0; return true; }

    bool supports_absolute_access() const final { return true; }
    mi::Sint64 tell_absolute() const final { return static_cast<mi::Sint64>(m_pos); }
    bool seek_absolute(mi::Sint64 pos) final
    {
        if (pos < 0 || static_cast<mi::Size>(pos) > m_size) {
            return false;
        }
        m_pos = static_cast<mi::Size>(pos);
        return true;
    }
    mi::Sint64 get_file_size() const final { return static_cast<mi::Sint64>(m_size); }
    bool seek_end() final { m_pos = m_size; return true; }

    // ---- IReader ----
    mi::Sint64 read(char* buffer, mi::Sint64 size) final
    {
        if (!buffer || size <= 0) {
            return 0;
        }
        const mi::Size remaining = m_size - m_pos;
        const mi::Size n =
            std::min<mi::Size>(static_cast<mi::Size>(size), remaining);
        if (n > 0) {
            std::memcpy(buffer, m_buffer.get() + m_pos, n);
            m_pos += n;
        }
        return static_cast<mi::Sint64>(n);
    }

    bool readline(char* buffer, mi::Sint32 size) final
    {
        if (!buffer || size <= 0) {
            return false;
        }
        const char* const start = m_buffer.get() + m_pos;
        const mi::Size remaining = m_size - m_pos;
        const mi::Size cap = static_cast<mi::Size>(size - 1);
        mi::Size i = 0;
        while (i < remaining && i < cap) {
            const char c = start[i];
            buffer[i] = c;
            ++i;
            if (c == '\n') {
                break;
            }
        }
        buffer[i] = '\0';
        m_pos += i;
        return true;
    }

    bool supports_lookahead() const final { return true; }
    mi::Sint64 lookahead(mi::Sint64 size, const char** buffer) const final
    {
        if (!buffer) {
            return -1;
        }
        const mi::Size remaining = m_size - m_pos;
        const mi::Size n =
            std::min<mi::Size>(static_cast<mi::Size>(size > 0 ? size : 0), remaining);
        *buffer = (n > 0) ? (m_buffer.get() + m_pos) : nullptr;
        return static_cast<mi::Sint64>(n);
    }

private:
    std::shared_ptr<const char> m_buffer;
    mi::Size m_size;
    mi::Size m_pos;
};

// ---------------------------------------------------------------------------
// ArResolvedModule
// ---------------------------------------------------------------------------
class ArResolvedModule final
    : public mi::base::Interface_implement<mi::neuraylib::IMdl_resolved_module>
{
public:
    ArResolvedModule(std::string moduleName,
                     std::string filename,
                     std::shared_ptr<const char> buffer,
                     mi::Size size)
        : m_moduleName(std::move(moduleName))
        , m_filename(std::move(filename))
        , m_buffer(std::move(buffer))
        , m_size(size)
    {
    }

    const char* get_module_name() const final { return m_moduleName.c_str(); }
    const char* get_filename() const final { return m_filename.c_str(); }

    mi::neuraylib::IReader* create_reader() const final
    {
        return new ArAssetReader(m_buffer, m_size);
    }

private:
    std::string m_moduleName;
    std::string m_filename;
    std::shared_ptr<const char> m_buffer;
    mi::Size m_size;
};

// ---------------------------------------------------------------------------
// ArResolvedResourceElement: one frame's worth of tiles.
//
// A frame element can carry one entity (plain texture, no UDIM) or many
// entities (UDIM tile set).  Bytes are fetched lazily: create_reader(i) opens
// the i-th tile's ArAsset only when the SDK actually asks for it.  This
// avoids paying the bandwidth cost of every tile when the consumer only
// needs a few.
// ---------------------------------------------------------------------------
class ArResolvedResourceElement final
    : public mi::base::Interface_implement<mi::neuraylib::IMdl_resolved_resource_element>
{
public:
    struct Tile
    {
        std::string mdlFilePath;  ///< the SDK's logical path for this tile
        std::string filename;     ///< the Ar-resolved identifier for the bytes
        mi::Sint32 u = 0;
        mi::Sint32 v = 0;
        bool uvValid = false;     ///< only true when the element has a uvtile mode
    };

    ArResolvedResourceElement(
        mi::Size frame,
        std::vector<Tile> tiles,
        mi::Size elementIndex = 0)
        : m_frame(frame)
        , m_tiles(std::move(tiles))
        , m_elementIndex(elementIndex)
    {
    }

    mi::Size get_frame_number() const final { return m_frame; }
    mi::Size get_count() const final { return m_tiles.size(); }

    const char* get_mdl_file_path(mi::Size i) const final
    {
        if (i < m_tiles.size()) {
            return m_tiles[i].mdlFilePath.c_str();
        }
        // MDL SDK 2026.0 validates element i by requesting resource i instead
        // of resource 0. Accommodate that check without adding fake tiles.
        return i == m_elementIndex && !m_tiles.empty()
            ? m_tiles.front().mdlFilePath.c_str()
            : nullptr;
    }

    const char* get_filename(mi::Size i) const final
    {
        return i < m_tiles.size() ? m_tiles[i].filename.c_str() : nullptr;
    }

    mi::neuraylib::IReader* create_reader(mi::Size i) const final
    {
        if (i >= m_tiles.size()
            || IsRejectedResourcePath(m_tiles[i].mdlFilePath)) {
            return nullptr;
        }
        // Open lazily.  Each call returns a fresh reader; if the SDK calls
        // create_reader multiple times for the same tile we pay the OpenAsset
        // cost again but avoid hoarding buffers across the whole tile set.
        PXR_NS::ArResolver& resolver = PXR_NS::ArGetResolver();
        const PXR_NS::ArResolvedPath resolved(m_tiles[i].filename);
        std::shared_ptr<PXR_NS::ArAsset> asset = resolver.OpenAsset(resolved);
        if (!asset) {
            return nullptr;
        }
        std::shared_ptr<const char> buffer = asset->GetBuffer();
        if (!buffer) {
            return nullptr;
        }
        return new ArAssetReader(std::move(buffer), asset->GetSize());
    }

    mi::base::Uuid get_resource_hash(mi::Size /*i*/) const final
    {
        return mi::base::Uuid{ 0, 0, 0, 0 };
    }

    bool get_uvtile_uv(mi::Size i, mi::Sint32& u, mi::Sint32& v) const final
    {
        // The SDK bridge also reads these outputs for a non-uvtile element.
        u = 0;
        v = 0;
        if (i >= m_tiles.size() || !m_tiles[i].uvValid) {
            return false;
        }
        u = m_tiles[i].u;
        v = m_tiles[i].v;
        return true;
    }

private:
    mi::Size m_frame = 0;
    std::vector<Tile> m_tiles;
    mi::Size m_elementIndex = 0;
};

// ---------------------------------------------------------------------------
// ArResolvedResource: 1+ frame elements.
// ---------------------------------------------------------------------------
class ArResolvedResource final
    : public mi::base::Interface_implement<mi::neuraylib::IMdl_resolved_resource>
{
public:
    ArResolvedResource(
        std::string mdlFilePathMask,
        std::string filenameMask,
        bool hasSequenceMarker,
        mi::neuraylib::Uvtile_mode uvtileMode,
        std::vector<mi::base::Handle<ArResolvedResourceElement>> elements)
        : m_mdlFilePathMask(std::move(mdlFilePathMask))
        , m_filenameMask(std::move(filenameMask))
        , m_hasSequenceMarker(hasSequenceMarker)
        , m_uvtileMode(uvtileMode)
        , m_elements(std::move(elements))
    {
    }

    // Convenience ctor for the single-tile, single-frame case (plain texture).
    ArResolvedResource(
        std::string mdlFilePath,
        std::string filename)
        : m_mdlFilePathMask(std::move(mdlFilePath))
        , m_filenameMask(std::move(filename))
        , m_hasSequenceMarker(false)
        , m_uvtileMode(mi::neuraylib::UVTILE_MODE_NONE)
    {
        ArResolvedResourceElement::Tile t;
        t.mdlFilePath = m_mdlFilePathMask;
        t.filename = m_filenameMask;
        std::vector<ArResolvedResourceElement::Tile> tiles;
        tiles.push_back(std::move(t));
        m_elements.push_back(mi::base::make_handle(
            new ArResolvedResourceElement(0, std::move(tiles))));
    }

    bool has_sequence_marker() const final { return m_hasSequenceMarker; }
    mi::neuraylib::Uvtile_mode get_uvtile_mode() const final { return m_uvtileMode; }
    const char* get_mdl_file_path_mask() const final { return m_mdlFilePathMask.c_str(); }
    const char* get_filename_mask() const final { return m_filenameMask.c_str(); }
    mi::Size get_count() const final { return m_elements.size(); }

    const mi::neuraylib::IMdl_resolved_resource_element* get_element(mi::Size i) const final
    {
        if (i >= m_elements.size()) {
            return nullptr;
        }
        m_elements[i]->retain();
        return m_elements[i].get();
    }

private:
    std::string m_mdlFilePathMask;
    std::string m_filenameMask;
    bool m_hasSequenceMarker;
    mi::neuraylib::Uvtile_mode m_uvtileMode;
    std::vector<mi::base::Handle<ArResolvedResourceElement>> m_elements;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// LooksLikeMdleContainer / HasTilingOrSequenceMarker now live in
// PathHelpers.{h,cpp} so the malformed-input branches can be directly
// unit-tested.  Bring them into this TU's scope via using-declarations
// so the call sites below stay byte-identical.
using usdMdl::runtime::path::LooksLikeMdleContainer;
using usdMdl::runtime::path::HasTilingOrSequenceMarker;

std::string
JoinAssetPath(const std::string& root, const std::string& relative)
{
    if (root.empty()) {
        return relative;
    }
    std::string result = root;
    std::replace(result.begin(), result.end(), '\\', '/');
    while (!result.empty() && result.back() == '/') {
        result.pop_back();
    }
    std::size_t first = 0;
    while (first < relative.size()
           && (relative[first] == '/' || relative[first] == '\\')) {
        ++first;
    }
    if (first == relative.size()) {
        return result;
    }
    result.push_back('/');
    result.append(relative, first, std::string::npos);
    return result;
}

std::vector<std::string>
SplitMdlName(const std::string& name)
{
    std::vector<std::string> result;
    std::size_t start = name.compare(0, 2, "::") == 0 ? 2 : 0;
    while (start < name.size()) {
        const std::size_t end = name.find("::", start);
        const std::string part = name.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty()) {
            return {};
        }
        result.push_back(part);
        if (end == std::string::npos) {
            break;
        }
        start = end + 2;
    }
    return result;
}

std::string
JoinMdlName(const std::vector<std::string>& components)
{
    std::string result;
    for (const std::string& component : components) {
        result += "::" + component;
    }
    return result;
}

// Map an absolute MDL name (leading ::) to a path relative to a search root.
std::string
QualifiedModuleNameToFilePath(const char* moduleName)
{
    if (!moduleName || moduleName[0] != ':' || moduleName[1] != ':') {
        return {};
    }
    const std::vector<std::string> components = SplitMdlName(moduleName);
    if (components.empty()) {
        return {};
    }
    std::string result;
    for (const std::string& component : components) {
        const std::string decoded = DecodeMdlPathComponent(component);
        if (decoded.empty()) {
            return {};
        }
        if (!result.empty()) {
            result.push_back('/');
        }
        result += decoded;
    }
    return result + ".mdl";
}

// URI scheme and authority components are identity, not traversable packages.
std::size_t
UriModulePrefixDepth(const char* name)
{
    if (!IsEncodedMdlName(name)) {
        return 0;
    }
    const std::string uri = DecodeMdlNameToUri(name);
    const std::size_t colon = uri.find(':');
    const bool authority = colon != std::string::npos
        && uri.compare(colon + 1, 2, "//") == 0
        && colon + 3 < uri.size() && uri[colon + 3] != '/';
    return authority ? 2 : 1;
}

// MDL permits one leading './' or an owner-bounded run of '../', never dot
// components elsewhere. Validate before Ar can normalize away invalid syntax.
// Native paths and resolver URIs are classified separately by the caller.
const char*
GetMdlResourcePathRejection(const std::string& path, const char* ownerName)
{
    const bool strictRelative = path.compare(0, 2, "./") == 0
        || path.compare(0, 3, "../") == 0;
    const auto owner = ownerName && std::strncmp(ownerName, "::", 2) == 0
        ? SplitMdlName(ownerName) : std::vector<std::string>();
    const std::size_t prefix = UriModulePrefixDepth(ownerName);
    std::size_t remainingParents = owner.size() > prefix ? owner.size() - prefix - 1 : 0;
    bool leadingParents = strictRelative;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find_first_of("/\\", start);
        const std::string component = path.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        if (component == ".") {
            if (!strictRelative || start != 0) {
                return "dot components are only allowed at the beginning of an MDL resource path";
            }
            leadingParents = false;
        } else if (component == "..") {
            if (!leadingParents) {
                return "dot components are only allowed at the beginning of an MDL resource path";
            }
            if (remainingParents == 0) {
                return "parent traversal exceeds the owning module package";
            }
            --remainingParents;
        } else {
            leadingParents = false;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return nullptr;
}

/// Resolve an MDL relative name against its owner and return the absolute MDL
/// name required by IMdl_resolved_module. The SDK passes the query and owner
/// in public-API encoded form; retaining their components preserves that form.
std::string
MakeAbsoluteModuleName(const char* relativeName, const char* ownerName)
{
    if (!relativeName || !ownerName
        || ownerName[0] != ':' || ownerName[1] != ':') {
        return {};
    }

    std::vector<std::string> owner = SplitMdlName(ownerName);
    const std::vector<std::string> relative = SplitMdlName(relativeName);
    if (owner.empty() || relative.size() < 2) {
        return {};
    }
    owner.pop_back();
    const std::size_t prefix = UriModulePrefixDepth(ownerName);
    if (owner.size() < prefix) {
        return {};
    }

    std::size_t firstName = 0;
    if (relative.front() == ".") {
        firstName = 1;
    } else {
        while (firstName < relative.size() && relative[firstName] == "..") {
            if (owner.size() <= prefix) {
                return {};
            }
            owner.pop_back();
            ++firstName;
        }
    }
    if (firstName == 0 || firstName == relative.size()) {
        return {};
    }
    for (std::size_t i = firstName; i < relative.size(); ++i) {
        if (relative[i] == "." || relative[i] == "..") {
            return {};
        }
        owner.push_back(relative[i]);
    }
    return JoinMdlName(owner);
}

/// Resolve through Ar with an optional owner anchor, without fetching bytes.
/// Resource readers open their assets lazily; module loading opens immediately.
std::string
ProbeUrlViaAr(const char* assetPath, const char* ownerFilePath)
{
    if (!assetPath || !assetPath[0]) {
        return {};
    }

    ArResolver& resolver = ArGetResolver();

    ArResolvedPath anchor;
    if (ownerFilePath && ownerFilePath[0]) {
        anchor = ArResolvedPath(std::string(ownerFilePath));
    }

    const std::string identifier =
        resolver.CreateIdentifier(std::string(assetPath), anchor);
    return identifier.empty() ? std::string()
        : resolver.Resolve(identifier).GetPathString();
}

/// A resolved module and its bytes. resolvedPath is empty on a miss.
struct ArResolved
{
    std::string resolvedPath;
    std::shared_ptr<const char> buffer;
    mi::Size size = 0;
};

ArResolved
TryResolveThroughAr(const char* assetPath, const char* ownerFilePath)
{
    ArResolved out;
    const ArResolvedPath resolved(ProbeUrlViaAr(assetPath, ownerFilePath));
    if (!resolved) {
        return out;
    }

    std::shared_ptr<ArAsset> asset = ArGetResolver().OpenAsset(resolved);
    if (!asset) {
        return out;
    }

    std::shared_ptr<const char> buffer = asset->GetBuffer();
    if (!buffer) {
        return out;
    }

    out.resolvedPath = resolved.GetPathString();
    out.buffer = std::move(buffer);
    out.size = asset->GetSize();
    return out;
}

/// Resolve an explicit URI unchanged first. Local file URIs get one native
/// path retry for filesystem-only Ar resolvers.
ArResolved
TryResolveUriThroughAr(const std::string& uri)
{
    ArResolved resolved = TryResolveThroughAr(uri.c_str(), nullptr);
    if (!resolved.resolvedPath.empty()) {
        return resolved;
    }
    const std::string fallback = FileUriToPath(uri);
    return fallback.empty()
        ? ArResolved()
        : TryResolveThroughAr(fallback.c_str(), nullptr);
}

/// Restore the marker-bearing suffix on the files Ar actually found. A new
/// asset identifier cannot locate an existing resource in a search context.
std::string
MakeAbsoluteResolvedResourceMask(
    const std::string& mask,
    const TileExpansionResult& exp)
{
    const std::size_t slash = mask.find_last_of("/\\", mask.find('<'));
    const std::size_t suffixStart = slash == std::string::npos ? 0 : slash + 1;
    std::string result;
    for (const ResolvedTile& tile : exp.tiles) {
        std::string resolved = tile.resolvedUrl;
        std::string suffix = tile.concretePath.substr(suffixStart);
#ifdef _WIN32
        if (!path::GetUriSchemeLength(resolved)) {
            std::replace(resolved.begin(), resolved.end(), '\\', '/');
            std::replace(suffix.begin(), suffix.end(), '\\', '/');
        }
#endif
        if (resolved.size() < suffix.size()
            || resolved.compare(resolved.size() - suffix.size(), suffix.size(), suffix) != 0) {
            return {};
        }
        const std::string candidate =
            resolved.substr(0, resolved.size() - suffix.size()) + mask.substr(suffixStart);
        if (!result.empty() && result != candidate) {
            return {}; // The backend does not expose one common storage mask.
        }
        result = candidate;
    }
    return result;
}

using usdMdl::runtime::path::MdlRelativeModuleNameToFilePath;
using usdMdl::runtime::path::NormalizeMdlResourceFilename;

/// Build the absolute logical MDL path required by IMdl_resolved_resource.
/// Authored MDL paths are rooted in the owner's package.
std::string
MakeAbsoluteMdlResourcePath(
    const std::string& path,
    const char* ownerName)
{
    if (!path.empty() && path.front() == '/') {
        return NormalizeMdlResourceFilename(path.c_str());
    }

    std::string packagePath;
    if (ownerName && ownerName[0] == ':' && ownerName[1] == ':') {
        // Resource paths use literal filename bytes, whereas owner names use
        // SDK percent encoding. Keep resources in the same decoded package.
        const std::string modulePath = QualifiedModuleNameToFilePath(ownerName);
        const std::size_t last = modulePath.rfind('/');
        if (last != std::string::npos) {
            packagePath = modulePath.substr(0, last);
        }
    }

    std::string combined = packagePath;
    if (!combined.empty()) {
        combined += '/';
    }
    combined += path;
    return NormalizeMdlResourceFilename(combined.c_str());
}

// ---------------------------------------------------------------------------
// The wrapper resolver itself.
// ---------------------------------------------------------------------------
class ArEntityResolver final
    : public mi::base::Interface_implement<mi::neuraylib::IMdl_entity_resolver>
{
public:
    ArEntityResolver(mi::neuraylib::IMdl_impexp_api* impexp,
                     std::vector<std::string> mdlSearchPaths)
        : m_impexp(mi::base::make_handle_dup(impexp))
        , m_mdlSearchPaths(std::move(mdlSearchPaths))
    {
    }

    mi::neuraylib::IMdl_resolved_module* resolve_module(
        const char* module_name,
        const char* owner_file_path,
        const char* owner_name,
        mi::Sint32,
        mi::Sint32,
        mi::neuraylib::IMdl_execution_context*) final
    {
        // (1) URI-encoded name: decode back to the URL the encoder
        //     produced this name from and fetch via Ar.  This is the
        //     path for off-search-path modules minted by
        //     `_DeriveQualifiedName()`.
        if (IsEncodedMdlName(module_name)) {
            const std::string url = DecodeMdlNameToUri(module_name);
            if (!url.empty()) {
                const ArResolved hit = TryResolveUriThroughAr(url);
                if (!hit.resolvedPath.empty()) {
                    return new ArResolvedModule(
                        std::string(module_name), hit.resolvedPath, hit.buffer, hit.size);
                }
            }
            return nullptr;
        }

        // (2) Validate relative import syntax and owner depth before probing Ar.
        //     The owner's package bounds parent traversal; no fixed depth cap
        //     is needed. The SDK requires an absolute MDL name in the result.
        if (module_name && module_name[0]
            && owner_name && owner_name[0]
            && owner_file_path && owner_file_path[0]
            && module_name[0] == '.')
        {
            const std::string relFile = MdlRelativeModuleNameToFilePath(module_name);
            if (relFile.empty()) {
                return nullptr;
            }
            const std::string absoluteName = MakeAbsoluteModuleName(module_name, owner_name);
            if (absoluteName.empty()) {
                return nullptr;
            }
            const ArResolved hit = TryResolveThroughAr(relFile.c_str(), owner_file_path);
            if (!hit.resolvedPath.empty()) {
                return new ArResolvedModule(
                    absoluteName, hit.resolvedPath, hit.buffer, hit.size);
            }
            return nullptr;
        }

        // (3) Absolute MDL names are mapped to a module path and probed beneath
        //     each configured MDL root through Ar.
        if (module_name && module_name[0] == ':' && module_name[1] == ':') {
            const std::string relative = QualifiedModuleNameToFilePath(module_name);
            if (relative.empty()) {
                return nullptr;
            }
            for (const std::string& root : m_mdlSearchPaths) {
                const std::string candidate = JoinAssetPath(root, relative);
                const ArResolved hit = TryResolveThroughAr(candidate.c_str(), nullptr);
                if (!hit.resolvedPath.empty()) {
                    return new ArResolvedModule(
                        module_name, hit.resolvedPath, hit.buffer, hit.size);
                }
            }
            return nullptr;
        }

        // Asset paths are converted to MDL names by the loading entry points,
        // before calling the SDK. This callback only resolves MDL names.
        return nullptr;
    }

    mi::neuraylib::IMdl_resolved_resource* resolve_resource(
        const char* file_path,
        const char* owner_file_path,
        const char* owner_name,
        mi::Sint32 pos_line,
        mi::Sint32 pos_column,
        mi::neuraylib::IMdl_execution_context* context) final
    {
        if (!file_path || !file_path[0]) {
            return nullptr;
        }
        const std::string mdlInput(file_path);
        const std::string decodedInput = DecodeMdlResourcePathToUri(mdlInput);
        const std::string& mask = decodedInput.empty() ? mdlInput : decodedInput;

        // A rejected reference must never fall through to Ar or search roots,
        // even if a file exists at the private logical path. This also covers
        // cached/imported references and unknown marker versions.
        if (IsRejectedResourcePath(mdlInput) || IsRejectedResourcePath(mask)) {
            RejectedResourceInfo rejection;
            if (DecodeRejectedResource(mask, rejection)) {
                RecordRejectedResource(context, rejection);
            }
            return nullptr;
        }

        // MDLE containers are deliberately unsupported. Delegating here would
        // bypass Ar and make behavior depend on an undocumented second path.
        if (LooksLikeMdleContainer(mask) || LooksLikeMdleContainer(mdlInput)) {
            return nullptr;
        }

        struct Candidate
        {
            std::string path;
            std::string owner;
            bool pathIsMdl = false;
        };
        std::vector<Candidate> candidates;
        const std::size_t schemeLength = path::GetUriSchemeLength(mask);
        const bool hasUriScheme = schemeLength != 0;
        const bool windowsDrivePath = path::IsWindowsDrivePath(mask);
        const bool absoluteMdlPath = mask.front() == '/';
        const bool nativeBackslashPath = mask.front() == '\\';
        if (!hasUriScheme && !windowsDrivePath && !nativeBackslashPath) {
            if (const char* reason = GetMdlResourcePathRejection(mask, owner_name)) {
                RejectedResourceInfo rejection;
                rejection.resourcePath = mdlInput;
                rejection.ownerModule = owner_name ? owner_name : "";
                rejection.ownerFile = owner_file_path ? owner_file_path : "";
                rejection.line = pos_line;
                rejection.column = pos_column;
                rejection.reason = reason;
                RecordRejectedResource(context, rejection);
                // Returning null here lets the SDK normalize the rejected path
                // into a different, readable resource. Give the compiler a
                // stable absolute identity instead; its later resource lookup
                // is rejected above, leaving an invalid value and a loadable
                // module. The descriptor has one element but no asset bytes.
                return new ArResolvedResource(
                    EncodeRejectedResource(rejection), std::string());
            }
        }

        if (hasUriScheme) {
            candidates.push_back({mask, {}, false});
            const std::string fallback = FileUriToPath(mask);
            if (!fallback.empty()) {
                candidates.push_back({fallback, {}, false});
            }
        } else if (windowsDrivePath || nativeBackslashPath) {
            candidates.push_back({mask, {}, false});
        } else if (!absoluteMdlPath) {
            candidates.push_back(
                {mask, owner_file_path ? owner_file_path : "",
                 owner_name && owner_name[0] && !IsEncodedMdlName(owner_name)});
        }
        if (!hasUriScheme && !windowsDrivePath
            && !nativeBackslashPath
            && (absoluteMdlPath || !owner_file_path || !owner_file_path[0])) {
            for (const std::string& root : m_mdlSearchPaths) {
                candidates.push_back({JoinAssetPath(root, mask), {}, true});
            }
        }
        if (absoluteMdlPath) {
            // Native POSIX paths remain accepted, after MDL search roots.
            candidates.push_back({mask, {}, false});
        }

        // UDIM / frame-sequence paths: probe every concrete candidate via Ar.
        if (m_impexp && std::any_of(candidates.begin(), candidates.end(),
                [](const Candidate& candidate) {
                    return HasTilingOrSequenceMarker(candidate.path);
                })) {
            for (const Candidate& candidate : candidates) {
                const TileExpansionResult exp = ExpandTilesViaAr(
                    m_impexp.get(), candidate.path, candidate.owner);
                if (!exp.tiles.empty()) {
                    const std::string filenameMask =
                        MakeAbsoluteResolvedResourceMask(candidate.path, exp);
                    if (filenameMask.empty()) {
                        continue;
                    }
                    if (mi::neuraylib::IMdl_resolved_resource* resource =
                            _BuildMultiElementResource(
                                candidate.path,
                                candidate.pathIsMdl
                                    ? MakeAbsoluteMdlResourcePath(mask, owner_name)
                                    : EncodeUriToMdlResourcePath(filenameMask),
                                filenameMask,
                                exp)) {
                        return resource;
                    }
                }
            }
            return nullptr;
        }

        // Plain single-file resource: probe via Ar (no eager byte fetch -
        // ArResolvedResource opens lazily in create_reader).
        for (const Candidate& candidate : candidates) {
            const std::string resolved = ProbeUrlViaAr(
                candidate.path.c_str(),
                candidate.owner.empty() ? nullptr : candidate.owner.c_str());
            if (!resolved.empty()) {
                const std::string mdlFilePath =
                    candidate.pathIsMdl
                        ? MakeAbsoluteMdlResourcePath(mask, owner_name)
                        : EncodeUriToMdlResourcePath(resolved);
                if (!mdlFilePath.empty()) {
                    return new ArResolvedResource(mdlFilePath, resolved);
                }
            }
        }
        return nullptr;
    }

private:
    /// Group the flat tile list from ExpandTilesViaAr by frame and assemble
    /// the corresponding IMdl_resolved_resource.  Caller has already ensured
    /// \p exp.tiles is non-empty.
    static mi::neuraylib::IMdl_resolved_resource*
    _BuildMultiElementResource(
        const std::string& candidateMask,
        const std::string& mdlFilePathMask,
        const std::string& filenameMask,
        const TileExpansionResult& exp)
    {
        if (mdlFilePathMask.empty()) {
            return nullptr;
        }

        std::vector<mi::base::Handle<ArResolvedResourceElement>> elements;
        std::vector<ArResolvedResourceElement::Tile> currentTiles;
        mi::Size currentFrame = exp.tiles.front().frame;
        const bool uvValid = (exp.uvtileMode != mi::neuraylib::UVTILE_MODE_NONE);
        const bool encoded = !DecodeMdlResourcePathToUri(mdlFilePathMask).empty();

        auto flushFrame = [&]() {
            if (currentTiles.empty()) {
                return;
            }
            elements.push_back(mi::base::make_handle(
                new ArResolvedResourceElement(
                    currentFrame, std::move(currentTiles), elements.size())));
            currentTiles.clear();
        };

        for (const ResolvedTile& t : exp.tiles) {
            if (t.frame != currentFrame) {
                flushFrame();
                currentFrame = t.frame;
            }
            ArResolvedResourceElement::Tile tile;
            tile.mdlFilePath = !encoded
                ? mdlFilePathMask.substr(0, mdlFilePathMask.find('<'))
                    + t.concretePath.substr(candidateMask.find('<'))
                : EncodeUriToMdlResourcePath(t.resolvedUrl);
            if (tile.mdlFilePath.empty()) {
                return nullptr;
            }
            tile.filename = t.resolvedUrl;
            tile.u = t.u;
            tile.v = t.v;
            tile.uvValid = uvValid;
            currentTiles.push_back(std::move(tile));
        }
        flushFrame();
        return new ArResolvedResource(
            /*mdlFilePathMask*/ mdlFilePathMask,
            /*filenameMask  */ filenameMask,
            exp.hasSequenceMarker,
            exp.uvtileMode,
            std::move(elements));
    }

private:
    mi::base::Handle<mi::neuraylib::IMdl_impexp_api> m_impexp;
    const std::vector<std::string> m_mdlSearchPaths;
};

} // namespace

mi::neuraylib::IMdl_entity_resolver*
CreateArEntityResolver(
    mi::neuraylib::IMdl_impexp_api* impexp,
    std::vector<std::string> mdlSearchPaths)
{
    return new ArEntityResolver(impexp, std::move(mdlSearchPaths));
}

MDL_RUNTIME_NAMESPACE_CLOSE
