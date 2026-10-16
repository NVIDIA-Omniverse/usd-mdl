//
// SPDX-License-Identifier: Apache-2.0
//
// Pure URI <-> MDL-module-name encoder.  Replaces the stateful name
// registry the runtime used to keep for off-search-path modules.
//
// MDL-module-name encoding rules. The SDK decodes public names once at the
// API-to-core boundary and re-encodes them before calling an external entity
// resolver. The resolver and database therefore use the same public form
// passed to load_module(); only the internal compiler-core boundary sees a
// decoded form.
//
//   1. URIs that point under a configured MDL search path collapse to
//      `::<package>::<sub>::<name>` (no scheme prefix).  This is the
//      shortening case the search-path mechanism exists for.
//      Components use the SDK's ordinary single-pass percent encoding.
//
//   2. Everything else encodes losslessly with two passes: first
//      `:` -> `%3A` (so `::` segment boundaries stay unambiguous after
//      the SDK's decode), then `%` -> `%25` (so the SDK's one decode
//      pass leaves the `%3A` intact rather than producing `:` and
//      breaking the MDL name grammar).  Combined: `:` -> `%253A`.
//      Literal percent signs already present in the URI are similarly
//      protected as `%2525`; otherwise the reverse path would decode URI
//      escapes such as `%20` into different characters.
//      Store the scheme and every immediately following slash in a dedicated
//      first component (`:` -> `%253A`, each `/` -> `%252F`). Even zero-slash
//      URIs separate that component from the remainder. Module and resource
//      encodings share these rules. Modules strip `.mdl` and use `::` instead
//      of `/` between components.
//
// Examples (canonicalUrl -> EncodeUriToMdlName()):
//
//   file:/home/user/foo.mdl                         -> ::file%253A%252F::home::user::foo
//   /home/user/foo.mdl  (treated as file:)          -> ::file%253A%252F::home::user::foo
//   file:/C:/user/foo.mdl  (Windows drive letter)   -> ::file%253A%252F::C%253A::user::foo
//   <sp>/nvidia/core_definitions.mdl  (with <sp> on search path)
//                                                   -> ::nvidia::core_definitions
//   http://host/pkg/foo.mdl                         -> ::http%253A%252F%252F::host::pkg::foo
//   file:///home/user/foo.mdl                       -> ::file%253A%252F%252F%252F::home::user::foo
//   myapp:/pkg/foo.mdl                              -> ::myapp%253A%252F::pkg::foo
//   myapp:pkg/foo.mdl                               -> ::myapp%253A::pkg::foo
//   myapp:foo.mdl                                   -> ::myapp%253A::foo
//
// The reverse direction is the entity resolver's job. The explicit slash
// prefix makes this transform reversible for arbitrary Ar resolver schemes;
// it does not rely on a hard-coded scheme table.

#pragma once

#include "defines.h"
#include "../api.h"

#include <string>
#include <vector>

MDL_RUNTIME_NAMESPACE_OPEN

/// Convert an absolute identifier to a URI for internal MDL-name encoding.
/// Native paths gain a `file:` scheme and URI escaping; existing resolver URIs
/// and MDL texture markers are preserved. Do not use this for Sdf resolved
/// paths or Ar::OpenAsset(), which require the original resolver identifier.
USDMDL_API
std::string NormalizeResolvedUri(const std::string& resolvedPath);

/// Decode a local file URI once to a native path. Nonempty authorities,
/// malformed escapes, NULs, queries, and fragments have no native fallback.
/// MDL frame and UV-tile markers are allowed in URI masks.
USDMDL_API
std::string FileUriToPath(const std::string& uri);

/// Decode one ordinary public MDL name component to a filename component.
/// Reject decoded separators, traversal components, NULs, and empty names.
/// This is one SDK decoding pass, not the two-layer URI identity decoding.
USDMDL_API
std::string DecodeMdlPathComponent(const std::string& component);

/// Map an asset URL or absolute filesystem path to an MDL module name.
///
/// \param canonicalUrl  An absolute filesystem path or a URI with an
///                      explicit scheme.  Filesystem paths without a
///                      scheme are treated as `file:`.  The trailing
///                      `.mdl` suffix is optional; it's stripped if
///                      present.
/// \param searchPaths   Active MDL search paths (filesystem paths,
///                      typically queried from
///                      `IMdl_configuration::get_mdl_path`).  The first
///                      one whose path is a prefix of \p canonicalUrl
///                      triggers the shortened form.
/// \return              An absolute MDL name beginning with `::` on
///                      success; an empty string on empty / malformed
///                      input.
USDMDL_API
std::string EncodeUriToMdlName(const std::string& canonicalUrl,
                               const std::vector<std::string>& searchPaths);

/// Map an absolute resolver identifier to an absolute MDL resource path.
/// Explicit identifiers are encoded losslessly, even below an MDL search
/// root: shortening would allow another root to shadow the selected file.
USDMDL_API
std::string EncodeUriToMdlResourcePath(const std::string& canonicalUrl);

/// Reverse the long form produced by EncodeUriToMdlResourcePath(). Ordinary
/// MDL paths such as `/package/resource.png` return an empty string.
USDMDL_API
std::string DecodeMdlResourcePathToUri(const std::string& path);

/// True iff \p name has the scheme/slash prefix shape produced by
/// `EncodeUriToMdlName` for an off-search-path URL.  Short names like
/// `::nvidia::core_definitions` and stdlib names like `::df` / `::std`
/// return false.
USDMDL_API
bool IsEncodedMdlName(const char* name);

USDMDL_API
bool IsEncodedMdlName(const std::string& name);

/// Reverse of `EncodeUriToMdlName` for names that satisfy
/// `IsEncodedMdlName`.  Returns the URL the encoder would have produced
/// the name from (with `.mdl` re-appended).  Returns an empty string
/// for names that aren't encoded URIs. Authority syntax is recovered from
/// the exact slash prefix encoded in the scheme segment.
USDMDL_API
std::string DecodeMdlNameToUri(const std::string& name);

MDL_RUNTIME_NAMESPACE_CLOSE
