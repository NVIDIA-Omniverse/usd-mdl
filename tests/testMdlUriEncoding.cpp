//
// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the URI <-> MDL-module-name encoder defined in
// src/usdMdl/runtime/UriEncoding.h.

#include "UriEncoding.h"

#include <cstdio>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "test_exit.h"

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr,                                              \
                         "[testMdlUriEncoding] FAIL: %s (at %s:%d)\n",        \
                         msg, __FILE__, __LINE__);                            \
            ++g_failures;                                                    \
        }                                                                     \
    } while (0)

#define EXPECT_EQ(actual, expected, msg)                                      \
    do {                                                                      \
        const std::string _a = (actual);                                      \
        const std::string _b = (expected);                                    \
        if (_a != _b) {                                                       \
            std::fprintf(stderr,                                              \
                         "[testMdlUriEncoding] FAIL: %s\n"                    \
                         "  got     : '%s'\n"                                 \
                         "  expected: '%s'\n  (at %s:%d)\n",                  \
                         msg, _a.c_str(), _b.c_str(), __FILE__, __LINE__);    \
            ++g_failures;                                                    \
        }                                                                     \
    } while (0)

using usdMdl::runtime::EncodeUriToMdlName;
using usdMdl::runtime::EncodeUriToMdlResourcePath;
using usdMdl::runtime::DecodeMdlNameToUri;
using usdMdl::runtime::DecodeMdlResourcePathToUri;
using usdMdl::runtime::NormalizeResolvedUri;
using usdMdl::runtime::IsEncodedMdlName;

void TestEncodeWorkedExamples()
{
    const std::vector<std::string> noPaths;

    // file: on Linux (also: bare absolute path).  Both encode the same.
    EXPECT_EQ(EncodeUriToMdlName("file:/home/user/foo.mdl", noPaths),
              "::file%253A%252F::home::user::foo",
              "file: Linux URI");
    EXPECT_EQ(EncodeUriToMdlName("/home/user/foo.mdl", noPaths),
              "::file%253A%252F::home::user::foo",
              "bare absolute path treated as file:");

    // file: on Windows (drive letter colon also gets %253A).
    EXPECT_EQ(EncodeUriToMdlName("file:/C:/user/foo.mdl", noPaths),
              "::file%253A%252F::C%253A::user::foo",
              "file: Windows URI");

    // Bare Windows path (no scheme) treated as file:; backslashes
    // normalize to forward slashes first.
    EXPECT_EQ(EncodeUriToMdlName("C:/Users/dhaase/foo.mdl", noPaths),
              "::file%253A%252F::C%253A::Users::dhaase::foo",
              "bare Windows path treated as file:");
    EXPECT_EQ(EncodeUriToMdlName("C:\\Users\\dhaase\\foo.mdl", noPaths),
              "::file%253A%252F::C%253A::Users::dhaase::foo",
              "bare Windows backslash path treated as file:");

    // http: with authority.
    EXPECT_EQ(EncodeUriToMdlName("http://host/pkg/foo.mdl", noPaths),
              "::http%253A%252F%252F::host::pkg::foo",
              "http: URI with authority");
    EXPECT_EQ(EncodeUriToMdlName("file:///home/user/foo.mdl", noPaths),
              "::file%253A%252F%252F%252F::home::user::foo",
              "file: URI with empty authority");

    // myapp: without authority.
    EXPECT_EQ(EncodeUriToMdlName("myapp:/pkg/foo.mdl", noPaths),
              "::myapp%253A%252F::pkg::foo",
              "myapp: URI without authority");
}

void TestNormalizeResolvedUri()
{
    EXPECT_EQ(NormalizeResolvedUri("C:\\Users\\dhaase\\textures\\one.png"),
              "file:/C:/Users/dhaase/textures/one.png",
              "Windows filesystem path becomes a file URI");
    EXPECT_EQ(NormalizeResolvedUri("/opt/mdl/textures/one.png"),
              "file:/opt/mdl/textures/one.png",
              "POSIX filesystem path becomes a file URI");
    EXPECT_EQ(NormalizeResolvedUri("omniverse://server/folder\\one.png"),
              "omniverse://server/folder\\one.png",
              "existing resolver identifier is preserved verbatim");
    EXPECT_EQ(NormalizeResolvedUri("/textures/a b#%20.png"),
              "file:/textures/a%20b%23%2520.png", "native names use URI escaping");
    EXPECT_EQ(NormalizeResolvedUri("/textures/tile.<##>.<UDIM>.png"),
              "file:/textures/tile.<##>.<UDIM>.png", "texture markers stay literal");
    EXPECT_EQ(NormalizeResolvedUri(u8"/textures/caf\u00e9.png"),
              "file:/textures/caf%C3%A9.png", "UTF-8 filename bytes are escaped");
    EXPECT_EQ(NormalizeResolvedUri("/textures/a<literal>.png"),
              "file:/textures/a%3Cliteral%3E.png", "only real markers are preserved");
    EXPECT_EQ(usdMdl::runtime::FileUriToPath("FiLe:///textures/a%20b%23%2520.png"),
              "/textures/a b#%20.png", "file URI escapes decode exactly once");
    for (const std::string& uri : {
             "file://server/share/a.png", "file:relative.png",
             "file:/a%00.png", "file:/a%GG.png", "file:/a%.png",
             "file:/a.png?query", "file:/a.png#fragment"}) {
        EXPECT_EQ(usdMdl::runtime::FileUriToPath(uri), "", "unsupported file URI has no native fallback");
    }
}

void TestEncodeShortenInsideSearchPath()
{
    // Search-path-relative shortening: when the URI lives under one of
    // the configured MDL search paths, the encoder collapses to the
    // package-relative form and drops the scheme prefix.
    const std::vector<std::string> sps = {
        "/opt/mdl/modules",
        "/opt/mdl/usd",
    };
    EXPECT_EQ(EncodeUriToMdlName("/opt/mdl/modules/acme/core_definitions.mdl", sps),
              "::acme::core_definitions",
              "shortening: bare path under search path");
    EXPECT_EQ(EncodeUriToMdlName("file:/opt/mdl/modules/acme/core_definitions.mdl", sps),
              "::acme::core_definitions",
              "shortening: file: URI under search path");
    // A search-path entry with a trailing slash should still match.
    const std::vector<std::string> spsTrailing = { "/opt/mdl/modules/" };
    EXPECT_EQ(EncodeUriToMdlName("/opt/mdl/modules/materials/Example.mdl", spsTrailing),
              "::materials::Example",
              "shortening: trailing-slash search-path entry");
    // No match -> long form.
    const std::vector<std::string> noMatch = { "/some/other/root" };
    EXPECT_EQ(EncodeUriToMdlName("/opt/mdl/modules/acme/foo.mdl", noMatch),
              "::file%253A%252F::opt::mdl::modules::acme::foo",
              "no shortening: search path does not cover the URL");
    for (const std::string& path : {
            "/opt/mdl/modules/acme/file%3A/paint%25.mdl",
            "file:/opt/mdl/modules/acme/file%253A/paint%2525.mdl",
            "file:///opt/mdl/modules/acme/file%253A/paint%2525.mdl"}) {
        EXPECT_EQ(EncodeUriToMdlName(path, sps),
                  "::acme::file%253A::paint%2525",
                  "short names escape literal filename percents once for the SDK");
    }
}

void TestMdlPathComponents()
{
    using usdMdl::runtime::DecodeMdlPathComponent;
    EXPECT_EQ(DecodeMdlPathComponent("file%253A"), "file%3A", "one SDK decode pass");
    EXPECT_EQ(DecodeMdlPathComponent("%252E%252E"), "%2E%2E", "literal escape is a filename");
    EXPECT_EQ(DecodeMdlPathComponent("%28%29%3C%3E%2C%24%23%3F%40%25"),
              "()<>,$#?@%", "SDK name metacharacters decode");
    EXPECT_EQ(EncodeUriToMdlName("/root/()<>,:$#?@%.mdl", {"/root"}),
              "::%28%29%3C%3E%2C%3A%24%23%3F%40%25", "SDK name metacharacters encode");
    for (const char* invalid : {"", ".", "..", "%2E", "%2E%2E", "a%2Fb", "a%5Cb", "a%3Ab", "%00"}) {
        EXPECT_EQ(DecodeMdlPathComponent(invalid), "", "decoded component cannot alter path structure");
    }
}

void TestEncodeDegenerate()
{
    EXPECT_EQ(EncodeUriToMdlName("", {}), "",
              "empty input returns empty");
    // No `.mdl` suffix is allowed; the encoder doesn't require it.
    EXPECT_EQ(EncodeUriToMdlName("/home/user/foo", {}),
              "::file%253A%252F::home::user::foo",
              "missing .mdl suffix accepted");
}

void TestIsEncoded()
{
    EXPECT(IsEncodedMdlName("::file%253A%252F::home::user::foo"),
           "encoded file: name recognised");
    EXPECT(IsEncodedMdlName("::http%253A::host::pkg::foo"),
           "encoded http: name recognised");
    EXPECT(!IsEncodedMdlName("::file%3A::home::user::foo"),
           "internal compiler form is not a public resolver name");
    EXPECT(!IsEncodedMdlName("::nvidia::core_definitions"),
           "shortened name is NOT encoded (resolved via search paths)");
    EXPECT(!IsEncodedMdlName("::df"),
           "stdlib-style name is NOT encoded");
    EXPECT(!IsEncodedMdlName(""),
           "empty string is NOT encoded");
    EXPECT(!IsEncodedMdlName(static_cast<const char*>(nullptr)),
           "nullptr is NOT encoded");
    EXPECT(!IsEncodedMdlName("file%253A::home::user::foo"),
           "missing leading `::` -> NOT encoded");
    for (const char* malformed : {"::abc%253Alocation::foo", "::4abc%253A::foo",
                                 "::abc%253A%252Fjunk::foo", "::abc%253A%2F::foo"}) {
        EXPECT(!IsEncodedMdlName(malformed), "only a valid scheme and complete slash markers form a prefix");
        EXPECT_EQ(DecodeMdlNameToUri(malformed), "", "malformed scheme prefix is not decoded");
    }
}

void TestDecodeRoundTrip()
{
    // Round-trip every example through encode -> decode and confirm
    // the URI we get back matches what we put in (modulo the
    // .mdl tail being added by the decoder).
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"abc:location/foo.mdl",       "::abc%253A::location::foo"},
        {"abc:foo.mdl",                "::abc%253A::foo"},
        {"abc:/location/foo.mdl",      "::abc%253A%252F::location::foo"},
        {"abc://location/foo.mdl",     "::abc%253A%252F%252F::location::foo"},
        {"abc:///location/foo.mdl",    "::abc%253A%252F%252F%252F::location::foo"},
        {"file:/home/user/foo.mdl",     "::file%253A%252F::home::user::foo"},
        {"file:/C:/user/foo.mdl",       "::file%253A%252F::C%253A::user::foo"},
        {"http://host/pkg/foo.mdl",     "::http%253A%252F%252F::host::pkg::foo"},
        {"file:///home/user/foo.mdl",   "::file%253A%252F%252F%252F::home::user::foo"},
        {"myapp:/pkg/foo.mdl",          "::myapp%253A%252F::pkg::foo"},
    };
    for (const auto& kv : cases) {
        const std::string& url = kv.first;
        const std::string& name = kv.second;
        EXPECT_EQ(EncodeUriToMdlName(url, {}), name,
                  ("encode roundtrip src: " + url).c_str());
        EXPECT_EQ(DecodeMdlNameToUri(name), url,
                  ("decode roundtrip src: " + name).c_str());
    }
}

void TestLiteralPercentEscapesRoundTrip()
{
    const std::vector<std::string> urls = {
        "custom:pkg/file%3A%2520.mdl",
        "custom:single%3A.mdl",
        "https://host/pkg/albedo%20tile.mdl",
        "https://host/pkg/literal%2520escape.mdl",
        "myapp:/pkg/encoded%2Fslash%25value.mdl",
    };
    for (const std::string& url : urls) {
        const std::string encoded = EncodeUriToMdlName(url, {});
        EXPECT_EQ(DecodeMdlNameToUri(encoded), url,
                  ("literal percent escape round-trip: " + url).c_str());

    }

    EXPECT_EQ(DecodeMdlNameToUri(
                  EncodeUriToMdlName("HTTPS://host/pkg/foo.mdl", {})),
              "HTTPS://host/pkg/foo.mdl",
              "URI schemes are case-insensitive when restoring authority");
}

void TestDecodeNonEncoded()
{
    // Names that aren't encoded URIs decode to "" so the caller falls
    // back to the SDK's resolver path.
    EXPECT_EQ(DecodeMdlNameToUri("::nvidia::core_definitions"), "",
              "shortened name decodes to empty");
    EXPECT_EQ(DecodeMdlNameToUri("::df"), "",
              "stdlib-style name decodes to empty");
    EXPECT_EQ(DecodeMdlNameToUri(""), "",
              "empty input decodes to empty");
}

void TestAuthorityShape()
{
    for (const std::string& scheme : {"file", "custom+resolver"}) {
        std::set<std::string> identities;
        for (const std::string& slashes : {"", "/", "//", "///"}) {
            const std::string uri = scheme + ":" + slashes + "host/pkg/file%253A.mdl";
            const std::string name = EncodeUriToMdlName(uri, {});
            EXPECT(identities.insert(name).second, "each slash spelling retains its URI identity");
            EXPECT_EQ(NormalizeResolvedUri(uri), uri, "file and custom URI spelling is preserved");
            EXPECT_EQ(DecodeMdlNameToUri(name), uri, "slash and percent spelling round-trip exactly");
            const std::string resource = EncodeUriToMdlResourcePath(uri);
            std::string moduleAsResource = name;
            for (std::size_t pos = 0; (pos = moduleAsResource.find("::", pos)) != std::string::npos; ++pos)
                moduleAsResource.replace(pos, 2, "/");
            EXPECT_EQ(moduleAsResource + ".mdl", resource, "module and resource prefixes agree");
            EXPECT_EQ(DecodeMdlResourcePathToUri(resource), uri, "resource slash spelling round-trips");
        }
    }
    EXPECT_EQ(DecodeMdlNameToUri(
                  EncodeUriToMdlName("custom+resolver://host/pkg/foo.mdl", {})),
              "custom+resolver://host/pkg/foo.mdl",
              "authority is preserved for an arbitrary resolver scheme");
    EXPECT_EQ(DecodeMdlNameToUri(
                  EncodeUriToMdlName("custom+resolver:/pkg/foo.mdl", {})),
              "custom+resolver:/pkg/foo.mdl",
              "non-authority form remains distinct for the same scheme");
    EXPECT_EQ(DecodeMdlNameToUri(
                  EncodeUriToMdlName("custom+resolver:///pkg/foo.mdl", {})),
              "custom+resolver:///pkg/foo.mdl",
              "empty authority remains distinct for the same scheme");
}

void TestMdlResourcePathEncoding()
{
    EXPECT_EQ(EncodeUriToMdlResourcePath("abc:location/texture.png"),
              "/abc%253A/location/texture.png", "zero-slash prefix is its own component");
    EXPECT_EQ(EncodeUriToMdlResourcePath("abc:texture.png"),
              "/abc%253A/texture.png", "zero-slash resource without a directory");
    EXPECT_EQ(
        EncodeUriToMdlResourcePath("file:/C:/user/texture.png"),
        "/file%253A%252F/C%253A/user/texture.png",
        "file URI maps to an absolute MDL resource path");
    EXPECT_EQ(
        EncodeUriToMdlResourcePath("/home/user/texture.png"),
        "/file%253A%252F/home/user/texture.png",
        "absolute Linux path maps to an absolute MDL resource path");
    EXPECT_EQ(
        EncodeUriToMdlResourcePath(
            "custom+resolver://host/pkg/texture.png"),
        "/custom+resolver%253A%252F%252F/host/pkg/texture.png",
        "resolver URI maps to an absolute MDL resource path");
    EXPECT_EQ(
        EncodeUriToMdlResourcePath("file:/C:/user/resource.mdl"),
        "/file%253A%252F/C%253A/user/resource.mdl",
        "resource encoding does not strip an .mdl extension");

    const std::vector<std::string> urls = {
        "file:/C:/user/texture.png",
        "file:/home/user/texture.png",
        "custom+resolver://host/pkg/texture_<UDIM>.png",
        "custom://host/a//texture.png",
        "custom://host/a/texture.png",
        "custom:opaque.png",
        "custom:/opaque.png",
        "custom:///opaque.png",
        "custom:folder/opaque.png",
        "custom://host/a/../texture.png",
        "custom://host/a/./texture.png",
        "custom://host/a/texture.png?path=/a//b#fragment",
        "custom://host/escaped%2Fslash%2520.png",
        "custom://host/back\\slash.png",
        "custom://host/a///",
        "custom:",
        "custom:/",
        "custom://",
        "custom://host/tile.<###>.<UVTILE0>.png",
        "file:/home/user/a%20b%23%2520.png",
    };
    std::set<std::string> identities;
    for (const std::string& url : urls) {
        const std::string encoded = EncodeUriToMdlResourcePath(url);
        EXPECT(!encoded.empty() && encoded.front() == '/', "resource identity is an absolute MDL path");
        EXPECT(encoded.find("//") == std::string::npos
                   && encoded.find("/../") == std::string::npos
                   && encoded.find("/./") == std::string::npos,
               "resource identity has no empty or parent/current-directory components");
        EXPECT(identities.insert(encoded).second, "distinct URI spellings retain distinct identities");
        EXPECT_EQ(
            DecodeMdlResourcePathToUri(encoded),
            url,
            ("resource path round-trip: " + url).c_str());
    }
    EXPECT_EQ(
        DecodeMdlResourcePathToUri("/package/texture.png"),
        "",
        "ordinary MDL resource path is not treated as an encoded URI");
}

} // namespace

int main()
{
    std::fprintf(stderr, "\nRUN testMdlUriEncoding\n");

    TestEncodeWorkedExamples();
    TestNormalizeResolvedUri();
    TestEncodeShortenInsideSearchPath();
    TestMdlPathComponents();
    TestEncodeDegenerate();
    TestIsEncoded();
    TestDecodeRoundTrip();
    TestLiteralPercentEscapesRoundTrip();
    TestDecodeNonEncoded();
    TestAuthorityShape();
    TestMdlResourcePathEncoding();

    if (g_failures == 0) {
        std::fprintf(stderr, "[testMdlUriEncoding] OK\n");
    } else {
        std::fprintf(stderr,
                     "[testMdlUriEncoding] %d failure(s)\n", g_failures);
    }
    mdl_test::FlushAndExit(g_failures == 0 ? 0 : 1);
}
