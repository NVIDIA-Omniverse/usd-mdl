//
// SPDX-License-Identifier: Apache-2.0
//
// Exercises ArEntityResolver::resolve_resource for UDIM tile sets, frame
// sequences, plain single-file textures, and the no-match fallback.  The
// probing tests construct a resolver directly to inspect its frame/tile
// results. The Sdr integration test also loads a real MDL module and opens
// its converted resource defaults directly through Ar.

#include "Context.h"
#include "EntityResolver.h"
#include "PathHelpers.h"
#include "UriEncoding.h"
#include "addModuleToRegistry.h"
#include "initialize.h"

#include <mi/mdl_sdk.h>
#include <mi/neuraylib/bsdf_isotropic_data.h>

#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/defaultResolverContext.h>
#include <pxr/usd/ar/defaultResolver.h>
#include <pxr/usd/ar/defineResolver.h>
#include <pxr/usd/ar/resolverContextBinder.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/pxr.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "test_exit.h"

PXR_NAMESPACE_USING_DIRECTIVE

// A real Ar URI resolver for tests. Exact identifiers deliberately select
// different files; normalization by usdMdl would return the wrong bytes.
class TestResourceUriResolver final : public ArDefaultResolver
{
public:
    static std::map<std::string, std::string> files;
    static std::map<std::string, std::string> redirects;
    static std::map<std::pair<std::string, std::string>, std::string> anchored;
    static std::vector<std::pair<std::string, std::string>> anchorRequests;

protected:
    std::string _CreateIdentifier(const std::string& path, const ArResolvedPath& anchor) const override
    {
        const auto key = std::make_pair(anchor.GetPathString(), path);
        anchorRequests.push_back(key);
        const auto it = anchored.find(key);
        if (it != anchored.end()) return it->second;
        return path;
    }
    ArResolvedPath _Resolve(const std::string& path) const override
    {
        const auto redirect = redirects.find(path);
        if (redirect != redirects.end()) return ArResolvedPath(redirect->second);
        return files.count(path) ? ArResolvedPath(path) : ArResolvedPath();
    }
    std::shared_ptr<ArAsset> _OpenAsset(const ArResolvedPath& path) const override
    {
        const auto it = files.find(path.GetPathString());
        return it == files.end() ? nullptr : ArDefaultResolver::_OpenAsset(ArResolvedPath(it->second));
    }
};

std::map<std::string, std::string> TestResourceUriResolver::files;
std::map<std::string, std::string> TestResourceUriResolver::redirects;
std::map<std::pair<std::string, std::string>, std::string> TestResourceUriResolver::anchored;
std::vector<std::pair<std::string, std::string>> TestResourceUriResolver::anchorRequests;
AR_DEFINE_RESOLVER(TestResourceUriResolver, ArDefaultResolver);

namespace {

namespace fs = std::filesystem;

#define EXPECT(cond, msg)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr,                                             \
                         "[testMdlResources] FAIL: %s (at %s:%d)\n",         \
                         msg, __FILE__, __LINE__);                           \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

int g_failures = 0;

/// Returns \p p as a forward-slash path (Ar canonical for MDL).
std::string
ToFwd(const fs::path& p)
{
    std::string s = p.string();
    for (char& c : s) {
        if (c == '\\') {
            c = '/';
        }
    }
    return s;
}

bool
IsAbsoluteMdlPath(const char* path)
{
    return path && path[0] == '/';
}

/// Creates \p p (including parent dirs) and writes one byte so ArAsset can
/// open it on platforms where zero-byte files behave oddly.
void
TouchFile(const fs::path& p, const std::string& contents = "x")
{
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << contents;
}

/// Build a resolver instance directly (no install) so we
/// can drive resolve_resource() synchronously.
mi::neuraylib::IMdl_entity_resolver*
MakeProbingResolver(std::vector<std::string> searchPaths = {})
{
    auto& ctx = usdMdl::runtime::Context::instance();
    if (!ctx.isInitialized()) {
        return nullptr;
    }
    return usdMdl::runtime::CreateArEntityResolver(
        ctx.getMdlImpExpApi().get(),
        std::move(searchPaths));
}

void
TestMariUdim(const fs::path& root)
{
    std::fprintf(stderr, "[testMdlResources] === Mari UDIM ===\n");
    const fs::path dir = root / "udim";
    // Mari tile 1001 = (0,0); 1010 = (9,0); 1023 = (2,2).
    const std::vector<fs::path> files = {
        dir / "tile_1001.png",
        dir / "tile_1010.png",
        dir / "tile_1023.png",
    };
    for (const auto& f : files) {
        TouchFile(f);
    }
    const std::string mask = ToFwd(dir / "tile_<UDIM>.png");

    mi::base::Handle<mi::neuraylib::IMdl_entity_resolver> resolver(MakeProbingResolver());
    EXPECT(resolver.is_valid_interface(), "resolver constructed");
    mi::base::Handle<mi::neuraylib::IMdl_resolved_resource> r(
        resolver->resolve_resource(mask.c_str(), nullptr, nullptr, 0, 0, nullptr));
    EXPECT(r.is_valid_interface(), "resolver returned non-null for UDIM mask");
    if (!r) {
        return;
    }
    EXPECT(r->get_uvtile_mode() == mi::neuraylib::UVTILE_MODE_UDIM,
           "uvtile_mode == UDIM");
    EXPECT(r->has_sequence_marker() == false, "no sequence marker");
    EXPECT(ToFwd(fs::path(r->get_filename_mask())) == mask,
           "resolved UDIM mask is absolute and preserves its marker");
    EXPECT(r->get_count() == 1, "one frame element");

    mi::base::Handle<const mi::neuraylib::IMdl_resolved_resource_element> el(
        r->get_element(0));
    EXPECT(el.is_valid_interface(), "element 0 present");
    if (!el) {
        return;
    }
    EXPECT(el->get_count() == 3, "three tiles");
    // (u,v) must match Mari numbering.
    std::set<std::pair<int, int>> got;
    for (mi::Size i = 0; i < el->get_count(); ++i) {
        mi::Sint32 u = -1, v = -1;
        EXPECT(el->get_uvtile_uv(i, u, v), "uvtile_uv returns true");
        got.emplace(u, v);
        mi::base::Handle<mi::neuraylib::IReader> reader(el->create_reader(i));
        EXPECT(reader.is_valid_interface(), "create_reader returns non-null");
    }
    EXPECT(got.count({0, 0}) == 1, "tile 1001 = (0,0)");
    EXPECT(got.count({9, 0}) == 1, "tile 1010 = (9,0)");
    EXPECT(got.count({2, 2}) == 1, "tile 1023 = (2,2)");
}

void
TestZBrushUvtile0(const fs::path& root)
{
    std::fprintf(stderr, "[testMdlResources] === ZBrush UVTILE0 ===\n");
    const fs::path dir = root / "uvtile0";
    // <UVTILE0> substitutes to "_u<U>_v<V>" (with the leading underscore baked
    // into the substitution), 0-indexed.  Files match that naming verbatim.
    const std::vector<std::pair<fs::path, std::pair<int, int>>> files = {
        { dir / "tile_u0_v0.png", { 0, 0 } },
        { dir / "tile_u1_v0.png", { 1, 0 } },
        { dir / "tile_u2_v3.png", { 2, 3 } },
    };
    for (const auto& f : files) {
        TouchFile(f.first);
    }
    const std::string mask = ToFwd(dir / "tile<UVTILE0>.png");

    mi::base::Handle<mi::neuraylib::IMdl_entity_resolver> resolver(MakeProbingResolver());
    mi::base::Handle<mi::neuraylib::IMdl_resolved_resource> r(
        resolver->resolve_resource(mask.c_str(), nullptr, nullptr, 0, 0, nullptr));
    EXPECT(r.is_valid_interface(), "UVTILE0 result non-null");
    if (!r) {
        return;
    }
    EXPECT(r->get_uvtile_mode() == mi::neuraylib::UVTILE_MODE_UVTILE0,
           "uvtile_mode == UVTILE0");
    mi::base::Handle<const mi::neuraylib::IMdl_resolved_resource_element> el(
        r->get_element(0));
    EXPECT(el && el->get_count() == 3, "three UVTILE0 tiles");
    if (!el) {
        return;
    }
    std::set<std::pair<int, int>> got;
    for (mi::Size i = 0; i < el->get_count(); ++i) {
        mi::Sint32 u = -1, v = -1;
        el->get_uvtile_uv(i, u, v);
        got.emplace(u, v);
    }
    for (const auto& f : files) {
        EXPECT(got.count(f.second) == 1, "expected UVTILE0 (u,v) found");
    }
}

void
TestMudboxUvtile1(const fs::path& root)
{
    std::fprintf(stderr, "[testMdlResources] === Mudbox UVTILE1 ===\n");
    const fs::path dir = root / "uvtile1";
    // <UVTILE1> substitutes to "_u<U>_v<V>" with 1-indexed u,v in the
    // filename, but the SDK reports 0-indexed (u,v) coordinates back via
    // get_uvtile_uv.
    TouchFile(dir / "tile_u1_v1.png"); // -> (0,0)
    TouchFile(dir / "tile_u2_v1.png"); // -> (1,0)
    const std::string mask = ToFwd(dir / "tile<UVTILE1>.png");

    mi::base::Handle<mi::neuraylib::IMdl_entity_resolver> resolver(MakeProbingResolver());
    mi::base::Handle<mi::neuraylib::IMdl_resolved_resource> r(
        resolver->resolve_resource(mask.c_str(), nullptr, nullptr, 0, 0, nullptr));
    EXPECT(r.is_valid_interface(), "UVTILE1 result non-null");
    if (!r) {
        return;
    }
    EXPECT(r->get_uvtile_mode() == mi::neuraylib::UVTILE_MODE_UVTILE1,
           "uvtile_mode == UVTILE1");
    mi::base::Handle<const mi::neuraylib::IMdl_resolved_resource_element> el(
        r->get_element(0));
    EXPECT(el && el->get_count() == 2, "two UVTILE1 tiles");
}

void
TestFrameSequence(const fs::path& root)
{
    std::fprintf(stderr, "[testMdlResources] === Frame sequence ===\n");
    const fs::path dir = root / "frames";
    // The SDK substitutes <####> with the bare integer frame number (no zero
    // padding) - the marker's '#' count drives the file-list regex inside the
    // SDK, not the per-frame substitution.  So file_3 is the literal filename
    // for frame 3, regardless of marker width.
    // Sequential frames 0..4, then a gap, then frame 12; tests early-out.
    const std::vector<int> frames = { 0, 1, 2, 3, 4, 12 };
    for (int f : frames) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "frame_%d.png", f);
        TouchFile(dir / buf);
    }
    const std::string mask = ToFwd(dir / "frame_<####>.png");

    mi::base::Handle<mi::neuraylib::IMdl_entity_resolver> resolver(MakeProbingResolver());
    mi::base::Handle<mi::neuraylib::IMdl_resolved_resource> r(
        resolver->resolve_resource(mask.c_str(), nullptr, nullptr, 0, 0, nullptr));
    EXPECT(r.is_valid_interface(), "frame-sequence result non-null");
    if (!r) {
        return;
    }
    EXPECT(r->has_sequence_marker(), "has_sequence_marker == true");
    EXPECT(r->get_uvtile_mode() == mi::neuraylib::UVTILE_MODE_NONE,
           "no uvtile mode on plain frame seq");
    EXPECT(ToFwd(fs::path(r->get_filename_mask())) == mask,
           "resolved frame mask is absolute and preserves its marker");
    EXPECT(r->get_count() == frames.size(),
           "one element per existing frame");

    std::set<mi::Size> seenFrames;
    for (mi::Size i = 0; i < r->get_count(); ++i) {
        mi::base::Handle<const mi::neuraylib::IMdl_resolved_resource_element> el(
            r->get_element(i));
        EXPECT(el.is_valid_interface(), "frame element non-null");
        if (!el) continue;
        EXPECT(el->get_count() == 1, "one tile per frame");
        seenFrames.insert(el->get_frame_number());
    }
    for (int f : frames) {
        EXPECT(seenFrames.count(static_cast<mi::Size>(f)) == 1,
               "expected frame number recorded");
    }
}

void
TestAnimatedUdim(const fs::path& root)
{
    std::fprintf(stderr, "[testMdlResources] === Animated UDIM ===\n");
    const fs::path dir = root / "animated-udim";
    const std::vector<fs::path> files = {
        dir / "tiled_resource.00.1001.png",
        dir / "tiled_resource.00.1002.png",
        dir / "tiled_resource.00.1011.png",
        dir / "tiled_resource.02.1012.png",
    };
    for (const fs::path& file : files) {
        TouchFile(file);
    }
    const std::string fileMask =
        ToFwd(dir / "tiled_resource.<##>.<UDIM>.png");
    const std::vector<std::string> masks = {
        "/animated-udim/tiled_resource.<##>.<UDIM>.png",
        usdMdl::runtime::NormalizeResolvedUri(fileMask),
    };
    const std::string mdlMask =
        "/animated-udim/tiled_resource.<##>.<UDIM>.png";

    mi::base::Handle<mi::neuraylib::IMdl_entity_resolver> resolver(
        MakeProbingResolver({ToFwd(root)}));
    for (const std::string& mask : masks) {
        mi::base::Handle<mi::neuraylib::IMdl_resolved_resource> resource(
            resolver->resolve_resource(
                mask.c_str(), nullptr, nullptr, 0, 0, nullptr));
        EXPECT(resource.is_valid_interface(), "animated UDIM result non-null");
        if (!resource) {
            continue;
        }
        EXPECT(resource->has_sequence_marker(), "animated UDIM has a sequence marker");
        EXPECT(resource->get_uvtile_mode() == mi::neuraylib::UVTILE_MODE_UDIM,
               "animated UDIM uses Mari numbering");
        EXPECT(IsAbsoluteMdlPath(resource->get_mdl_file_path_mask()),
               "animated UDIM returns an absolute MDL path mask");
        if (mask == mdlMask) {
            EXPECT(std::string(resource->get_mdl_file_path_mask()) == mdlMask,
                   "authored absolute MDL masks retain their package identity");
        }
        EXPECT(resource->get_filename_mask()
                   && ToFwd(fs::path(resource->get_filename_mask())) == fileMask,
               "animated UDIM returns the native resolved filename mask");
        EXPECT(resource->get_count() == 2, "animated UDIM has frames 0 and 2");

        mi::Size fileCount = 0;
        std::set<std::string> resolvedFiles;
        for (mi::Size i = 0; i < resource->get_count(); ++i) {
            mi::base::Handle<const mi::neuraylib::IMdl_resolved_resource_element>
                element(resource->get_element(i));
            EXPECT(element.is_valid_interface(), "animated UDIM frame element present");
            if (element) {
                fileCount += element->get_count();
                for (mi::Size j = 0; j < element->get_count(); ++j) {
                    EXPECT(IsAbsoluteMdlPath(element->get_mdl_file_path(j)),
                           "animated UDIM tile returns an absolute MDL path");
                    EXPECT(element->get_filename(j) && element->get_filename(j)[0],
                           "animated UDIM tile returns its resolved filename");
                    if (element->get_filename(j)) {
                        resolvedFiles.insert(ToFwd(fs::path(element->get_filename(j))));
                    }
                }
            }
        }
        EXPECT(fileCount == files.size(), "animated UDIM resolves all four tiles");
        for (const fs::path& file : files) {
            EXPECT(resolvedFiles.count(ToFwd(file)) == 1,
                   "animated UDIM returns every native resolved filename");
        }
    }
}

void
TestNoMarker(const fs::path& root)
{
    std::fprintf(stderr, "[testMdlResources] === Plain (no markers) ===\n");
    const fs::path file = root / "plain" / "plain.png";
    TouchFile(file);

    mi::base::Handle<mi::neuraylib::IMdl_entity_resolver> resolver(MakeProbingResolver());
    mi::base::Handle<mi::neuraylib::IMdl_resolved_resource> r(
        resolver->resolve_resource(ToFwd(file).c_str(), nullptr, nullptr, 0, 0, nullptr));
    EXPECT(r.is_valid_interface(), "plain result non-null");
    if (!r) {
        return;
    }
    EXPECT(r->get_count() == 1, "single frame");
    EXPECT(!r->has_sequence_marker(), "no sequence marker");
    EXPECT(r->get_uvtile_mode() == mi::neuraylib::UVTILE_MODE_NONE, "no uvtile mode");
    EXPECT(IsAbsoluteMdlPath(r->get_mdl_file_path_mask()),
           "plain resource returns an absolute MDL path mask");
    mi::base::Handle<const mi::neuraylib::IMdl_resolved_resource_element> el(
        r->get_element(0));
    EXPECT(el && el->get_count() == 1, "single tile");
    if (el) {
        EXPECT(IsAbsoluteMdlPath(el->get_mdl_file_path(0)),
               "plain resource returns an absolute MDL path");
        EXPECT(el->get_filename(0)
                   && ToFwd(fs::path(el->get_filename(0))) == ToFwd(file),
               "plain resource filename is the native resolved path");
        mi::base::Handle<mi::neuraylib::IReader> reader(el->create_reader(0));
        EXPECT(reader.is_valid_interface(), "lazy reader opens");
    }

    const std::string fileUri = usdMdl::runtime::NormalizeResolvedUri(ToFwd(file));
    mi::base::Handle<mi::neuraylib::IMdl_resolved_resource> uriResource(
        resolver->resolve_resource(
            fileUri.c_str(), nullptr, nullptr, 0, 0, nullptr));
    EXPECT(uriResource.is_valid_interface(), "single-file URI resolves");
    if (uriResource) {
        const std::string mdlPath = uriResource->get_mdl_file_path_mask();
        EXPECT(IsAbsoluteMdlPath(mdlPath.c_str()),
               "single-file URI result has an absolute MDL path");
        EXPECT(mdlPath != fileUri,
               "single-file URI is not returned as an MDL path");
        EXPECT(usdMdl::runtime::DecodeMdlResourcePathToUri(mdlPath) == fileUri,
               "encoded MDL path retains the resolved URI identity");
        mi::base::Handle<const mi::neuraylib::IMdl_resolved_resource_element>
            uriElement(uriResource->get_element(0));
        EXPECT(uriElement
                   && std::string(uriElement->get_mdl_file_path(0)) == mdlPath,
               "single-file element uses the same absolute MDL path");
        EXPECT(uriElement && uriElement->get_filename(0)
                   && ToFwd(fs::path(uriElement->get_filename(0))) == ToFwd(file),
               "single-file URI resolves to a native filename");

        mi::base::Handle<mi::neuraylib::IMdl_resolved_resource> roundTrip(
            resolver->resolve_resource(
                mdlPath.c_str(), nullptr, nullptr, 0, 0, nullptr));
        EXPECT(roundTrip.is_valid_interface(),
               "encoded absolute MDL path resolves again");
        if (roundTrip) {
            EXPECT(std::string(roundTrip->get_mdl_file_path_mask()) == mdlPath,
                   "encoded absolute MDL path is stable across resolution");
        }
    }

    std::string alternateFileUri = fileUri;
    alternateFileUri.replace(0, 4, "FiLe");
    alternateFileUri.insert(5, "//"); // Exercise file:///path as well.
    mi::base::Handle<mi::neuraylib::IMdl_resolved_resource> alternateResource(
        resolver->resolve_resource(
            alternateFileUri.c_str(), nullptr, nullptr, 0, 0, nullptr));
    EXPECT(alternateResource.is_valid_interface(),
           "mixed-case empty-authority file URI resolves");
}

void
TestRelativeResourceLogicalPath(const fs::path& root)
{
    std::fprintf(stderr, "[testMdlResources] === Relative logical path ===\n");
    const fs::path owner = root / "pkg" / "module.mdl";
    const fs::path file = root / "pkg" / "resources" / "plain.png";
    TouchFile(owner);
    TouchFile(file);

    mi::base::Handle<mi::neuraylib::IMdl_entity_resolver> resolver(MakeProbingResolver());
    mi::base::Handle<mi::neuraylib::IMdl_resolved_resource> r(
        resolver->resolve_resource(
            "./resources/plain.png",
            ToFwd(owner).c_str(),
            "::pkg::module",
            0,
            0,
            nullptr));
    EXPECT(r.is_valid_interface(), "relative resource result non-null");
    if (!r) {
        return;
    }
    EXPECT(std::string(r->get_mdl_file_path_mask()) == "/pkg/resources/plain.png",
           "relative mask is absolute in the owner's MDL package");
    mi::base::Handle<const mi::neuraylib::IMdl_resolved_resource_element> el(
        r->get_element(0));
    EXPECT(el && std::string(el->get_mdl_file_path(0)) == "/pkg/resources/plain.png",
           "relative element path matches the absolute logical mask");

    const std::string ownerUri =
        usdMdl::runtime::NormalizeResolvedUri(ToFwd(owner));
    const std::string encodedOwner =
        usdMdl::runtime::EncodeUriToMdlName(ownerUri, {});
    mi::base::Handle<mi::neuraylib::IMdl_resolved_resource> encoded(
        resolver->resolve_resource(
            "./resources/plain.png",
            ToFwd(owner).c_str(),
            encodedOwner.c_str(),
            0,
            0,
            nullptr));
    EXPECT(encoded.is_valid_interface(),
           "relative resource with a URI-encoded owner resolves");
    if (encoded) {
        const std::string encodedPath = encoded->get_mdl_file_path_mask();
        EXPECT(IsAbsoluteMdlPath(encodedPath.c_str()),
               "URI-encoded owner produces an absolute MDL resource path");
        EXPECT(usdMdl::runtime::DecodeMdlResourcePathToUri(encodedPath)
                   == usdMdl::runtime::NormalizeResolvedUri(ToFwd(file)),
               "encoded owner MDL path retains the resource URI identity");
    }
}

void
TestSearchPathResolution(const fs::path& root)
{
    std::fprintf(stderr, "[testMdlResources] === Search-path resolution ===\n");
    const fs::path searchRoot = root / "search-root";
    const fs::path module = searchRoot / "pkg" / "nested" / "module.mdl";
    const fs::path sibling = searchRoot / "pkg" / "nested" / "sibling.mdl";
    const fs::path peer = searchRoot / "pkg" / "peer.mdl";
    const fs::path resource = searchRoot / "pkg" / "resources" / "plain.png";
    TouchFile(module);
    TouchFile(sibling);
    TouchFile(peer);
    TouchFile(resource);
    TouchFile(root / "escape.mdl");
    TouchFile(root / "escape.png");

    mi::base::Handle<mi::neuraylib::IMdl_entity_resolver> resolver(
        MakeProbingResolver({ToFwd(searchRoot)}));
    EXPECT(resolver.is_valid_interface(), "search-path resolver constructed");
    if (!resolver) {
        return;
    }

    mi::base::Handle<mi::neuraylib::IMdl_resolved_module> qualified(
        resolver->resolve_module(
            "::pkg::nested::module", nullptr, nullptr, 0, 0, nullptr));
    EXPECT(qualified.is_valid_interface(), "qualified module resolves under configured root");
    if (qualified) {
        EXPECT(std::string(qualified->get_module_name()) == "::pkg::nested::module",
               "qualified result preserves requested MDL name");
        EXPECT(ToFwd(fs::path(qualified->get_filename())) == ToFwd(module),
               "qualified result reports resolved module filename");
    }

    mi::base::Handle<mi::neuraylib::IMdl_resolved_module> escapedModule(
        resolver->resolve_module(
            "::..::escape", nullptr, nullptr, 0, 0, nullptr));
    EXPECT(!escapedModule.is_valid_interface(),
           "malformed qualified name cannot escape a configured root");

    mi::base::Handle<mi::neuraylib::IMdl_resolved_module> relative(
        resolver->resolve_module(
            ".::sibling",
            ToFwd(module).c_str(),
            "::pkg::nested::module",
            0,
            0,
            nullptr));
    EXPECT(relative.is_valid_interface(), "same-package relative module resolves via owner");
    if (relative) {
        EXPECT(std::string(relative->get_module_name()) == "::pkg::nested::sibling",
               "resolver derives same-package absolute MDL name");
        EXPECT(ToFwd(fs::path(relative->get_filename())) == ToFwd(sibling),
               "same-package relative filename is anchored to owner");
    }

    mi::base::Handle<mi::neuraylib::IMdl_resolved_module> parent(
        resolver->resolve_module(
            "..::peer",
            ToFwd(module).c_str(),
            "::pkg::nested::module",
            0,
            0,
            nullptr));
    EXPECT(parent.is_valid_interface(), "parent-package relative module resolves via owner");
    if (parent) {
        EXPECT(std::string(parent->get_module_name()) == "::pkg::peer",
               "resolver derives parent-package absolute MDL name");
        EXPECT(ToFwd(fs::path(parent->get_filename())) == ToFwd(peer),
               "parent-package relative filename is anchored to owner");
    }

    mi::base::Handle<mi::neuraylib::IMdl_resolved_resource> absoluteResource(
        resolver->resolve_resource(
            "/pkg/resources/plain.png", nullptr, nullptr, 0, 0, nullptr));
    EXPECT(absoluteResource.is_valid_interface(),
           "absolute MDL resource path resolves under configured root");
    if (absoluteResource) {
        EXPECT(std::string(absoluteResource->get_mdl_file_path_mask())
                   == "/pkg/resources/plain.png",
               "absolute MDL resource retains its logical path");
    }


    mi::base::Handle<mi::neuraylib::IMdl_resolved_resource> escapedResource(
        resolver->resolve_resource(
            "/../escape.png", nullptr, nullptr, 0, 0, nullptr));
    EXPECT(escapedResource.is_valid_interface(),
           "invalid absolute resource retains a rejection descriptor");
    if (escapedResource) {
        mi::base::Handle<const mi::neuraylib::IMdl_resolved_resource_element> element(
            escapedResource->get_element(0));
        EXPECT(element && !mi::base::Handle<mi::neuraylib::IReader>(element->create_reader(0)),
               "invalid absolute resource cannot supply bytes outside the root");
    }

    mi::base::Handle<mi::neuraylib::IMdl_resolved_resource> mdle(
        resolver->resolve_resource(
            "archive.mdle:member.png", nullptr, nullptr, 0, 0, nullptr));
    EXPECT(!mdle.is_valid_interface(), "MDLE member paths are explicitly unsupported");
}

void
TestMissFallback(const fs::path& root)
{
    std::fprintf(stderr, "[testMdlResources] === Miss (no built-in fallback) ===\n");
    // Direct at a UDIM mask in an empty directory.  Our test resolver has no
    // built-in fallback, so a miss should return null (not crash).
    const fs::path dir = root / "empty";
    fs::create_directories(dir);
    const std::string mask = ToFwd(dir / "missing<UDIM>.png");

    mi::base::Handle<mi::neuraylib::IMdl_entity_resolver> resolver(MakeProbingResolver());
    mi::base::Handle<mi::neuraylib::IMdl_resolved_resource> r(
        resolver->resolve_resource(mask.c_str(), nullptr, nullptr, 0, 0, nullptr));
    EXPECT(!r.is_valid_interface(), "miss with no fallback returns null");
}

void
CheckResourceContents(
    mi::neuraylib::IMdl_resolved_resource* resource,
    const std::map<std::tuple<mi::Size, int, int>, std::string>& expected)
{
    EXPECT(resource, "resource resolves");
    if (!resource) return;
    std::map<std::tuple<mi::Size, int, int>, std::string> actual;
    for (mi::Size f = 0; f < resource->get_count(); ++f) {
        mi::base::Handle<const mi::neuraylib::IMdl_resolved_resource_element> element(
            resource->get_element(f));
        EXPECT(element, "resolved frame is present");
        if (!element) continue;
        for (mi::Size i = 0; i < element->get_count(); ++i) {
            mi::Sint32 u = 0, v = 0;
            const bool tiled = element->get_uvtile_uv(i, u, v);
            EXPECT(tiled == (resource->get_uvtile_mode() != mi::neuraylib::UVTILE_MODE_NONE),
                   "UV coordinates agree with resource kind");
            mi::base::Handle<mi::neuraylib::IReader> reader(element->create_reader(i));
            EXPECT(reader, "resolved entity can be opened");
            if (!reader) continue;
            char bytes[256];
            const mi::Sint64 count = reader->read(bytes, sizeof(bytes));
            EXPECT(count >= 0 && count < sizeof(bytes), "fixture bytes can be read completely");
            if (count < 0 || count >= sizeof(bytes)) continue;
            const auto key = std::make_tuple(element->get_frame_number(), u, v);
            EXPECT(actual.emplace(key, std::string(bytes, static_cast<std::size_t>(count))).second,
                   "each frame/tile coordinate occurs exactly once");
        }
    }
    EXPECT(actual == expected, "every frame/tile yields the expected bytes, with no missing or extra entities");
}

void
CheckResourceRoundTrips(
    mi::neuraylib::IMdl_entity_resolver* resolver,
    const std::string& input,
    const std::map<std::tuple<mi::Size, int, int>, std::string>& expected,
    const char* owner = nullptr)
{
    mi::base::Handle<mi::neuraylib::IMdl_resolved_resource> resource(
        resolver->resolve_resource(input.c_str(), owner, nullptr, 0, 0, nullptr));
    CheckResourceContents(resource.get(), expected);
    if (!resource) return;
    EXPECT(IsAbsoluteMdlPath(resource->get_mdl_file_path_mask()), "MDL identity is absolute");
    EXPECT(resource->get_filename_mask() && resource->get_filename_mask()[0], "storage mask is available");
    if (!resource->get_mdl_file_path_mask() || !resource->get_filename_mask()) return;
    for (const std::string& path : {
             std::string(resource->get_mdl_file_path_mask()),
             usdMdl::runtime::NormalizeResolvedUri(resource->get_filename_mask())}) {
        mi::base::Handle<mi::neuraylib::IMdl_resolved_resource> again(
            resolver->resolve_resource(path.c_str(), nullptr, nullptr, 0, 0, nullptr));
        CheckResourceContents(again.get(), expected);
    }
}

void
TestResourceIdentity(const fs::path& root)
{
    std::fprintf(stderr, "[testMdlResources] === Resource identity and bytes ===\n");
    const fs::path first = root / "first";
    const fs::path second = root / "second";
    TouchFile(first / "same.png", "first root");
    TouchFile(second / "same.png", "second root");
    TouchFile(first / "tile_1001.png", "wrong tile");
    TouchFile(second / "tile_1001.png", "second tile 1001");
    TouchFile(second / "tile_1002.png", "second tile 1002");
    mi::base::Handle<mi::neuraylib::IMdl_entity_resolver> resolver(
        MakeProbingResolver({ToFwd(first), ToFwd(second)}));
    CheckResourceRoundTrips(resolver.get(), ToFwd(second / "same.png"),
                           {{{0, 0, 0}, "second root"}});
    CheckResourceRoundTrips(resolver.get(), "/same.png", {{{0, 0, 0}, "first root"}});
    CheckResourceRoundTrips(resolver.get(), ToFwd(second / "tile_<UDIM>.png"),
                           {{{0, 0, 0}, "second tile 1001"}, {{0, 1, 0}, "second tile 1002"}});

    // No MDL search root: only Ar's bound context can find these files.
    const fs::path contextRoot = root / "context";
    TouchFile(contextRoot / "images/tile_1001.png", "context tile");
    TouchFile(contextRoot / "images/anim.00.1001.png", "frame zero");
    TouchFile(contextRoot / "images/anim.02.1012.png", "frame two");
    const fs::path owner = root / "owner/module.mdl";
    TouchFile(owner);
    mi::base::Handle<mi::neuraylib::IMdl_entity_resolver> contextResolver(MakeProbingResolver());
    ArResolverContextBinder binder(ArDefaultResolverContext({ToFwd(contextRoot)}));
    CheckResourceRoundTrips(contextResolver.get(), "images/tile_<UDIM>.png", {{{0, 0, 0}, "context tile"}});
    CheckResourceRoundTrips(contextResolver.get(), "images/tile_<UDIM>.png",
                           {{{0, 0, 0}, "context tile"}}, ToFwd(owner).c_str());
    CheckResourceRoundTrips(contextResolver.get(), "images/anim.<##>.<UDIM>.png",
                           {{{0, 0, 0}, "frame zero"}, {{2, 1, 1}, "frame two"}});

    // This URI is authored independently of NormalizeResolvedUri, so matching
    // encoder/decoder mistakes cannot make the test pass.
    TouchFile(root / "space # %20.png", "escaped filename");
    TouchFile(root / "space%20%23%20%2520.png", "wrong literal escapes");
    const std::string rootUri = "file:" + std::string(ToFwd(root).front() == '/' ? "" : "/") + ToFwd(root);
    CheckResourceRoundTrips(contextResolver.get(), rootUri + "/space%20%23%20%2520.png",
                           {{{0, 0, 0}, "escaped filename"}});
    CheckResourceRoundTrips(contextResolver.get(), ToFwd(root / "space # %20.png"),
                           {{{0, 0, 0}, "escaped filename"}});
    CheckResourceRoundTrips(contextResolver.get(), rootUri + "/context/images/tile_%3CUDIM%3E.png",
                           {{{0, 0, 0}, "context tile"}});
}

void
TestCustomUriIdentity(const fs::path& root)
{
    std::fprintf(stderr, "[testMdlResources] === Custom Ar URI identity ===\n");
    const std::vector<std::string> uris = {
        "test-resource://host/a//texture.png",
        "test-resource://host/a/texture.png",
        "test-resource://host/a/./texture.png",
        "test-resource://host/a/../texture.png",
        "test-resource:opaque.png",
        "test-resource:/opaque.png",
        "test-resource://opaque.png",
        "test-resource:///opaque.png",
        "test-resource:folder/opaque.png",
        "test-resource://host/literal%2520.png",
        "test-resource://host/literal%20.png",
    };
    mi::base::Handle<mi::neuraylib::IMdl_entity_resolver> resolver(MakeProbingResolver());
    for (std::size_t i = 0; i < uris.size(); ++i) {
        const fs::path file = root / "custom" / (std::to_string(i) + ".png");
        const std::string bytes = "custom asset " + std::to_string(i);
        TouchFile(file, bytes);
        TestResourceUriResolver::files.emplace(uris[i], ToFwd(file));
        CheckResourceRoundTrips(resolver.get(), uris[i], {{{0, 0, 0}, bytes}});
        mi::base::Handle<mi::neuraylib::IMdl_resolved_resource> resource(
            resolver->resolve_resource(uris[i].c_str(), nullptr, nullptr, 0, 0, nullptr));
        EXPECT(resource && resource->get_filename_mask()
                   && std::string(resource->get_filename_mask()) == uris[i],
               "custom Ar storage identifiers are returned verbatim");
        if (!resource) continue;
        const auto asset = ArGetResolver().OpenAsset(ArResolvedPath(resource->get_filename_mask()));
        EXPECT(asset && asset->GetBuffer()
                   && std::string(asset->GetBuffer().get(), asset->GetSize()) == bytes,
               "custom resolver identifier opens directly in Ar with the expected bytes");
    }
    TouchFile(root / "custom/frame0.png", "custom frame zero");
    TouchFile(root / "custom/frame2.png", "custom frame two");
    TestResourceUriResolver::files.emplace("test-resource://host/tile.00.1001.png",
                                         ToFwd(root / "custom/frame0.png"));
    TestResourceUriResolver::files.emplace("test-resource://host/tile.02.1012.png",
                                         ToFwd(root / "custom/frame2.png"));
    CheckResourceRoundTrips(resolver.get(), "test-resource://host/tile.<##>.<UDIM>.png",
                           {{{0, 0, 0}, "custom frame zero"}, {{2, 1, 1}, "custom frame two"}});
}

void
TestSdrResolvedModuleUri(const fs::path& root)
{
    std::fprintf(stderr, "[testMdlResources] === Sdr module identifier opened directly by Ar ===\n");
    const std::string requested = "test-resource://host/module.mdl";
    const std::string resolved = "test-resource://host/storage//module%2520.mdl";
    const std::string bytes = "mdl 1.8;\nexport float marker() = 0.375;\n";
    const fs::path file = root / "custom/module.mdl";
    TouchFile(file, bytes);
    TestResourceUriResolver::redirects.emplace(requested, resolved);
    TestResourceUriResolver::files.emplace(resolved, ToFwd(file));

    // Supply only the authored URI. It cannot be opened until Ar resolves it;
    // the resulting storage identifier must survive SDK loading and Sdr parsing.
    ArResolver& resolver = ArGetResolver();
    EXPECT(!resolver.OpenAsset(ArResolvedPath(requested)), "authored URI is not a storage identifier");
    const auto nodes = UsdMdlAddModuleToRegistry(SdfAssetPath(requested));
    EXPECT(nodes.size() == 1, "custom resolver module exports one definition");
    if (nodes.size() != 1) return;
    const auto* node = nodes.front();
    EXPECT(node->GetResolvedImplementationURI() == resolved,
           "Sdr implementation URI preserves the selected Ar identifier verbatim");
    EXPECT(node->GetResolvedDefinitionURI() == resolved,
           "MDL definition and implementation come from the same resolved module");
    const auto asset = resolver.OpenAsset(ArResolvedPath(node->GetResolvedImplementationURI()));
    EXPECT(asset, "Sdr implementation URI opens directly in Ar");
    if (!asset) return;
    const auto buffer = asset->GetBuffer();
    EXPECT(buffer && std::string(buffer.get(), asset->GetSize()) == bytes,
           "Ar returns the exact loaded MDL module bytes");
}

void
TestModuleUriPrefixes(const fs::path& root)
{
    std::fprintf(stderr, "[testMdlResources] === Explicit URI prefixes through SDK and Sdr ===\n");
    using namespace mi::neuraylib;
    using namespace usdMdl::runtime;
    auto& ctx = Context::instance();
    mi::base::Handle<mi::neuraylib::INeuray> neuray(UsdMdlGetNeuray());
    mi::base::Handle<IDatabase> db(neuray->get_api_component<IDatabase>());
    mi::base::Handle<IScope> scope(db->get_global_scope());
    mi::base::Handle<IMdl_entity_resolver> resolver(MakeProbingResolver());
    std::set<std::string> databaseNames;
    unsigned index = 0;
    for (const std::string& prefix : {"test-resource:", "test-resource:/",
                                      "test-resource://host/", "test-resource:///"}) {
        const fs::path dir = root / "prefixes" / std::to_string(index);
        const float siblingValue = 10.0f * ++index;
        auto add = [&](const std::string& relative, const std::string& bytes) {
            const fs::path file = dir / relative;
            TouchFile(file, bytes);
            TestResourceUriResolver::files.emplace(prefix + relative, ToFwd(file));
        };
        // Exercise both public entry points before these module names exist
        // in the SDK database or Sdr, independently of host-side loading.
        for (bool registration : {false, true}) {
            const std::string leaf = registration ? "registered.mdl" : "cold.mdl";
            const std::string uri = prefix + leaf;
            const float expected = siblingValue + (registration ? 2.0f : 1.0f);
            add(leaf, "mdl 1.8; export float probe(float x = "
                + std::to_string(expected) + ") = x;\n");
            SdrShaderNodeConstPtr node = nullptr;
            if (registration) {
                const auto nodes = UsdMdlAddModuleToRegistry(SdfAssetPath(uri));
                EXPECT(nodes.size() == 1, "fresh URI module registers one definition");
                if (nodes.size() == 1) node = nodes.front();
            } else {
                node = SdrRegistry::GetInstance().GetShaderNodeFromAsset(
                    SdfAssetPath(uri), {}, TfToken("probe"), TfToken("mdl"));
            }
            EXPECT(node && node->IsValid(), "public entry point loads a fresh URI module");
            if (node) {
                EXPECT(node->GetResolvedImplementationURI() == uri, "fresh lookup preserves URI spelling");
                const auto* input = node->GetShaderInput(TfToken("x"));
                EXPECT(input && input->GetDefaultValue() == VtValue(expected), "fresh lookup selects distinct content");
            }
        }
        add("sibling.mdl", "mdl 1.8; export const float sibling_marker = "
            + std::to_string(siblingValue) + ";\n");
        add("pkg/sibling.mdl", "mdl 1.8; export const float sibling_marker = "
            + std::to_string(siblingValue) + ";\n");
        add("parent.mdl", "mdl 1.8; export const float parent_marker = 3.0;\n");
        add("single.mdl", "mdl 1.8; import .::sibling::*; "
            "export float probe(float x = sibling::sibling_marker) = x;\n");
        add("pkg/entry%3A.mdl", "mdl 1.8; import .::sibling::*; import ..::parent::*; "
            "export float probe(float x = sibling::sibling_marker + parent::parent_marker) = x;\n");
        for (const std::string& relative : {"single.mdl", "pkg/entry%3A.mdl"}) {
            const bool nested = relative.find('/') != std::string::npos;
            const std::string uri = prefix + relative;
            const float expected = siblingValue + (nested ? 3.0f : 0.0f);
            TestResourceUriResolver::anchored[{uri, "./sibling.mdl"}] =
                prefix + (nested ? "pkg/sibling.mdl" : "sibling.mdl");
            TestResourceUriResolver::anchored[{uri, "../parent.mdl"}] = prefix + "parent.mdl";
            const std::string name = EncodeUriToMdlName(uri, {});
            EXPECT(IsEncodedMdlName(name) && DecodeMdlNameToUri(name) == uri,
                   "URI prefix and literal escapes survive the module identity");
            mi::base::Handle<const mi::IString> dbName(ctx.getMdlFactory()->get_db_module_name(name.c_str()));
            EXPECT(dbName && databaseNames.insert(dbName->get_c_str()).second,
                   "each URI form receives a distinct SDK database name");
            if (!dbName) continue;
            mi::base::Handle<ITransaction> txn(scope->create_transaction());
            const int rc = ctx.getMdlImpExpApi()->load_module(txn.get(), name.c_str(), nullptr);
            EXPECT(rc == 0, "SDK loads the module and its owner-relative imports");
            if (rc < 0) { txn->abort(); continue; }
            {
                mi::base::Handle<const IModule> module(txn->access<IModule>(dbName->get_c_str()));
                EXPECT(module && std::string(module->get_filename()) == uri
                           && std::string(module->get_mdl_name()) == name,
                       "SDK preserves both public name and Ar storage identifier");
            }
            EXPECT(txn->commit() == 0, "host-loaded module becomes visible to Sdr");
            const auto* cold = SdrRegistry::GetInstance().GetShaderNodeFromAsset(
                SdfAssetPath(uri), {}, TfToken("probe"), TfToken("mdl"));
            EXPECT(cold && cold->IsValid(), "cold Sdr lookup accepts each URI form");
            if (cold) {
                const auto* input = cold->GetShaderInput(TfToken("x"));
                EXPECT(input && input->GetDefaultValue() == VtValue(expected),
                       "different slash forms expose the corresponding module default");
                EXPECT(cold->GetResolvedImplementationURI() == uri,
                       "cold Sdr implementation identifier retains exact URI spelling");
            }
            const auto nodes = UsdMdlAddModuleToRegistry(SdfAssetPath(uri));
            EXPECT(nodes.size() == 1 && nodes.front()->IsValid(), "explicit registration accepts each URI form");
            if (nodes.size() == 1) {
                EXPECT(nodes.front()->GetResolvedImplementationURI() == uri,
                       "registration preserves the source identifier");
                const auto* input = nodes.front()->GetShaderInput(TfToken("x"));
                EXPECT(input && input->GetDefaultValue() == VtValue(expected), "registration selects the correct default");
            }
            // Ar knows a parent target even at the URI root. The entity
            // resolver must reject traversal before reaching that mapping.
            if (!nested) {
                const auto before = TestResourceUriResolver::anchorRequests.size();
                mi::base::Handle<IMdl_resolved_module> escaped(resolver->resolve_module(
                    "..::parent", uri.c_str(), name.c_str(), 0, 0, nullptr));
                mi::base::Handle<IMdl_resolved_resource> resource(resolver->resolve_resource(
                    "../parent.mdl", uri.c_str(), name.c_str(), 0, 0, nullptr));
                EXPECT(!escaped && resource, "invalid resource traversal retains a rejection descriptor");
                if (resource) {
                    EXPECT(resource->get_count() == 1, "rejection descriptor has one resource element");
                    mi::base::Handle<const IMdl_resolved_resource_element> element(resource->get_element(0));
                    EXPECT(element && !mi::base::Handle<IReader>(element->create_reader(0)),
                           "rejected resource never supplies asset bytes");
                }
                EXPECT(TestResourceUriResolver::anchorRequests.size() == before,
                       "invalid traversal and its reader are rejected before Ar anchoring");
            }
            const std::string bytes = "resource for " + uri;
            add(nested ? "pkg/resource.bin" : "resource.bin", bytes);
            const std::string resourceUri = prefix + (nested ? "pkg/resource.bin" : "resource.bin");
            TestResourceUriResolver::anchored[{uri, "./resource.bin"}] = resourceUri;
            mi::base::Handle<IMdl_resolved_resource> resource(resolver->resolve_resource(
                "./resource.bin", uri.c_str(), name.c_str(), 0, 0, nullptr));
            CheckResourceContents(resource.get(), {{{0, 0, 0}, bytes}});
            if (resource) {
                EXPECT(std::string(resource->get_filename_mask()) == resourceUri,
                       "owner-relative resource retains its custom URI prefix");
                CheckResourceRoundTrips(resolver.get(), resource->get_mdl_file_path_mask(),
                                        {{{0, 0, 0}, bytes}});
            }
        }
    }
}

void
TestSdrResolvedAssetPaths(const fs::path& mdlRoot)
{
    std::fprintf(stderr, "[testMdlResources] === Sdr defaults opened directly by Ar ===\n");
    const fs::path local = mdlRoot / "resources/space # %20.png";
    fs::copy_file(mdlRoot / "resources/one.png", local);
    // Generate a decodable measurement so this checks the successful BSDF
    // path as well as the invalid placeholder fixtures used by Python tests.
    const fs::path measured = mdlRoot / "resources/measured.mbsdf";
    mi::base::Handle<mi::neuraylib::Bsdf_isotropic_data> data(
        new mi::neuraylib::Bsdf_isotropic_data(1, 1, mi::neuraylib::BSDF_SCALAR));
    mi::base::Handle<mi::neuraylib::Bsdf_buffer> buffer(data->get_bsdf_buffer());
    buffer->get_data()[0] = 0.125f;
    const int exported = usdMdl::runtime::Context::instance().getMdlImpExpApi()
        ->export_bsdf_data(ToFwd(measured).c_str(), data.get(), nullptr);
    EXPECT(exported == 0, "valid BSDF measurement fixture exported");
    if (exported != 0) return;
    ArResolver& resolver = ArGetResolver();
    const fs::path module = mdlRoot / "ar_resource_defaults.mdl";
    TouchFile(module,
        "mdl 1.8;\nexport material defaults(\n"
        "  uniform texture_2d local = texture_2d(\"./resources/space # %20.png\"),\n"
        "  uniform light_profile light = light_profile(\"./resources/one.ies\"),\n"
        "  uniform bsdf_measurement measured = bsdf_measurement(\"./resources/measured.mbsdf\")\n"
        ") = material();\n");
    const auto nodes = UsdMdlAddModuleToRegistry(SdfAssetPath(ToFwd(module)));
    EXPECT(nodes.size() == 1, "fixture module exports one definition");
    if (nodes.size() != 1) return;
    struct Expected {
        const char* input;
        std::string identifier;
        fs::path file;
    };
    for (const Expected& expected : std::vector<Expected>{
             {"local", resolver.Resolve(ToFwd(local)).GetPathString(), local},
             {"light", resolver.Resolve(ToFwd(mdlRoot / "resources/one.ies")).GetPathString(),
              mdlRoot / "resources/one.ies"},
             {"measured", resolver.Resolve(ToFwd(measured)).GetPathString(), measured}}) {
        const auto* property = nodes.front()->GetShaderInput(TfToken(expected.input));
        EXPECT(property && property->GetDefaultValue().IsHolding<SdfAssetPath>(),
               "resource input exposes an SdfAssetPath default");
        if (!property || !property->GetDefaultValue().IsHolding<SdfAssetPath>()) continue;
        const auto& path = property->GetDefaultValue().UncheckedGet<SdfAssetPath>();
        EXPECT(path.GetResolvedPath() == expected.identifier,
               "Sdf resolved path preserves Ar's identifier verbatim");
        const auto asset = resolver.OpenAsset(ArResolvedPath(path.GetResolvedPath()));
        EXPECT(asset, "Sdf resolved path opens directly in Ar without conversion or re-resolution");
        if (!asset) continue;
        const auto buffer = asset->GetBuffer();
        EXPECT(buffer, "opened resource bytes are available");
        if (!buffer) continue;
        std::ifstream file(expected.file, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        EXPECT(std::string(buffer.get(), asset->GetSize()) == bytes,
               "direct Ar access yields the expected fixture bytes");
    }
}

// Pure-string coverage includes malformed inputs the SDK does not exercise.
void
TestPathHelpers()
{
    using namespace usdMdl::runtime::path;
    std::fprintf(stderr, "[testMdlResources] === PathHelpers ===\n");

    // ---- URI / drive classification ----
    EXPECT(GetUriSchemeLength("C:/one.png") == 0, "drive path is not a URI");
    EXPECT(GetUriSchemeLength("C:\\one.png") == 0, "backslash drive is not a URI");
    EXPECT(GetUriSchemeLength("x:opaque") == 1, "one-letter URI scheme is valid");
    EXPECT(GetUriSchemeLength("my+app.v2:/one") == 9, "RFC scheme characters work");
    EXPECT(GetUriSchemeLength("1bad:one") == 0, "scheme must begin with a letter");

    // ---- LooksLikeMdleContainer ----
    EXPECT(LooksLikeMdleContainer("foo.mdle:bar"), "lowercase .mdle:");
    EXPECT(LooksLikeMdleContainer("foo.MDLE:bar"), "uppercase .MDLE:");
    EXPECT(LooksLikeMdleContainer("foo.MdLe:bar"), "mixed-case .MdLe:");
    EXPECT(LooksLikeMdleContainer("nested/path/foo.mdle:bar/baz"),
           "nested .mdle: anywhere in path");
    EXPECT(!LooksLikeMdleContainer("foo.mdl"), "plain .mdl is not a container");
    EXPECT(!LooksLikeMdleContainer("foo.mdle"), ".mdle alone (no colon) is not a member ref");
    EXPECT(!LooksLikeMdleContainer(""), "empty string");

    // ---- HasFrameMarker ----
    EXPECT(HasFrameMarker("foo<#>.png"), "single #");
    EXPECT(HasFrameMarker("foo<####>.png"), "four #");
    EXPECT(HasFrameMarker("a<#>b<##>c"), "two markers");
    EXPECT(!HasFrameMarker("foo<###.png"), "missing close bracket");
    EXPECT(!HasFrameMarker("foo<>.png"), "empty angle pair (no #)");
    EXPECT(!HasFrameMarker("foo.png"), "no marker");
    EXPECT(!HasFrameMarker(""), "empty");

    // ---- HasTilingOrSequenceMarker ----
    EXPECT(HasTilingOrSequenceMarker("foo<UDIM>.png"), "UDIM");
    EXPECT(HasTilingOrSequenceMarker("foo<UVTILE0>.png"), "UVTILE0");
    EXPECT(HasTilingOrSequenceMarker("foo<UVTILE1>.png"), "UVTILE1");
    EXPECT(HasTilingOrSequenceMarker("foo<###>.png"), "frame marker via composite");
    EXPECT(!HasTilingOrSequenceMarker("foo.png"), "plain texture");
    EXPECT(!HasTilingOrSequenceMarker(""), "empty");

    // ---- NormalizeMdlResourceFilename ----
    EXPECT(NormalizeMdlResourceFilename(nullptr) == "", "null resource path");
    EXPECT(NormalizeMdlResourceFilename("") == "", "empty resource path");
    EXPECT(NormalizeMdlResourceFilename("resources/one.png") == "/resources/one.png",
           "relative MDL resource gains leading slash");
    EXPECT(NormalizeMdlResourceFilename("./resources/one.png") == "/resources/one.png",
           "current-directory component is removed");
    EXPECT(NormalizeMdlResourceFilename("pkg/../resources//one.png") == "/resources/one.png",
           "relative dot segments and duplicate separators are canonicalized");
    EXPECT(NormalizeMdlResourceFilename("../resources/one.png") == "",
           "relative path cannot escape the MDL root");
    EXPECT(NormalizeMdlResourceFilename("./") == "",
           "directory-only relative path does not become the MDL root");
    EXPECT(NormalizeMdlResourceFilename("/resources/one.png") == "/resources/one.png",
           "absolute MDL resource remains unchanged");
    EXPECT(NormalizeMdlResourceFilename("C:/textures/one.png") == "C:/textures/one.png",
           "Windows drive path remains unchanged");
    EXPECT(NormalizeMdlResourceFilename("omniverse://server/one.png")
               == "omniverse://server/one.png",
           "resolver URI remains unchanged");
    EXPECT(NormalizeMdlResourceFilename("file:/textures/one.png")
               == "file:/textures/one.png",
           "file URI remains unchanged");
    EXPECT(NormalizeMdlResourceFilename("my+app.v2:/textures/one.png")
               == "my+app.v2:/textures/one.png",
           "custom URI scheme remains unchanged");
    EXPECT(NormalizeMdlResourceFilename("1bad:one.png") == "/1bad:one.png",
           "invalid URI scheme is treated as a relative MDL path");

    // ---- MdlRelativeModuleNameToFilePath ----
    // MDL relative-name components map directly to strict relative URL
    // components, as in MDL-SDK File_resolver::to_url().
    EXPECT(MdlRelativeModuleNameToFilePath(".::foo") == "./foo.mdl",
           "single-dot relative");
    EXPECT(MdlRelativeModuleNameToFilePath(".::foo::bar") == "./foo/bar.mdl",
           "single-dot nested");
    EXPECT(MdlRelativeModuleNameToFilePath("..::pkg::baz") == "../pkg/baz.mdl",
           "double-dot relative");
    EXPECT(MdlRelativeModuleNameToFilePath("..::..::a::b") == "../../a/b.mdl",
           "two-parent relative");
    EXPECT(MdlRelativeModuleNameToFilePath(nullptr) == "",
           "null input rejected");
    EXPECT(MdlRelativeModuleNameToFilePath("") == "",
           "empty input rejected");
    EXPECT(MdlRelativeModuleNameToFilePath("::absolute") == "",
           "absolute name rejected");
    EXPECT(MdlRelativeModuleNameToFilePath("::foo::bar") == "",
           "absolute multi-segment rejected");
    EXPECT(MdlRelativeModuleNameToFilePath("nodots") == "",
           "non-dotted name rejected");
    EXPECT(MdlRelativeModuleNameToFilePath(".") == "",
           "single dot with no tail rejected");
    EXPECT(MdlRelativeModuleNameToFilePath("..") == "",
           "double dot with no tail rejected");
    EXPECT(MdlRelativeModuleNameToFilePath(".:foo") == "",
           "single colon after dot rejected");
    EXPECT(MdlRelativeModuleNameToFilePath(".::") == "",
           "dot + separator with empty tail rejected");
    EXPECT(MdlRelativeModuleNameToFilePath("...::tail") == "",
           "dot run is not an MDL relative-name component");
    EXPECT(MdlRelativeModuleNameToFilePath(".::..::tail") == "",
           "parent component cannot follow current-package component");
    EXPECT(MdlRelativeModuleNameToFilePath("..::pkg::..::tail") == "",
           "parent component cannot follow a package name");
    EXPECT(MdlRelativeModuleNameToFilePath("..::pkg::::tail") == "",
           "empty package component rejected");
    // String conversion has no arbitrary depth cap. The resolver checks the
    // owner's package depth before performing I/O (testMdlPathContracts).
    std::string deep;
    std::string deepPath;
    for (int i = 0; i < 17; ++i) {
        deep += "..::";
        deepPath += "../";
    }
    deep += "tail";
    EXPECT(MdlRelativeModuleNameToFilePath(deep.c_str()) == deepPath + "tail.mdl",
           "17 parent components convert without an arbitrary cap");
}

} // namespace

int
main()
{
    std::fprintf(stderr, "\nRUN testMdlResources\n");

    const char* testDir = std::getenv("PXR_TEST_DIR");
    if (!testDir) mdl_test::FlushAndExit(1);
    PlugRegistry::GetInstance().RegisterPlugins(
        ToFwd(fs::path(testDir) / "resourceResolver/plugInfo.json"));

    PlugPluginPtr plug =
        PlugRegistry::GetInstance().GetPluginWithName("usdMdl");
    if (!plug || !plug->Load()) {
        std::fprintf(stderr, "[testMdlResources] FAIL: cannot load usdMdl\n");
        mdl_test::FlushAndExit(1);
    }
    if (!usdMdl::runtime::Context::instance().ensureInitialized()) {
        std::fprintf(stderr, "[testMdlResources] FAIL: Context init\n");
        mdl_test::FlushAndExit(1);
    }
    EXPECT(mi::base::Handle<mi::neuraylib::IMdl_entity_resolver>(UsdMdlGetEntityResolver()).get()
               == usdMdl::runtime::Context::instance().getEntityResolver().get(),
           "public resolver accessor returns the active Ar resolver");

    // Sandbox dir under the test's CWD; `cmake/mdlTestWrapper.py` clears
    // the entire per-test temp dir on success.
    const fs::path root = fs::current_path() / "tilegrid";
    std::error_code ec;
    fs::remove_all(root, ec); // ignore errors
    fs::create_directories(root);

    TestMariUdim(root);
    TestZBrushUvtile0(root);
    TestMudboxUvtile1(root);
    TestFrameSequence(root);
    TestAnimatedUdim(root);
    TestNoMarker(root);
    TestRelativeResourceLogicalPath(root);
    TestSearchPathResolution(root);
    TestMissFallback(root);
    TestResourceIdentity(root);
    TestCustomUriIdentity(root);
    TestSdrResolvedModuleUri(root);
    TestModuleUriPrefixes(root);
    TestSdrResolvedAssetPaths(fs::path(testDir) / "mdl");
    TestPathHelpers();

    if (g_failures == 0) {
        std::fprintf(stderr, "\n[testMdlResources] OK (all subtests passed)\n");
    } else {
        std::fprintf(stderr, "\n[testMdlResources] %d FAILURE(S)\n", g_failures);
    }
    std::fflush(stderr);

    // Same teardown rationale as the other C++ tests in this dir: bypass DLL
    // detach so SDK shutdown sequencing doesn't crash the process.
    mdl_test::FlushAndExit(g_failures == 0 ? 0 : 1);
}
