//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "debugCodes.h"
#include "debugContext.h"

#include <pxr/base/tf/diagnostic.h>

#include <string>

#define ERROR_GET_INTERFACE(PREFIX, SOURCE, DEST)                                                                              \
    TF_ERROR(USD_MDL, "%s\tUnable to get: '%s' interface from: '%s'.", (PREFIX).format().c_str(), typeid(DEST).name(), typeid(SOURCE).name())

#define ERROR_GET_ITEM(PREFIX, SOURCE, DEST)                                                                                   \
    TF_ERROR(USD_MDL, "%s\tUnable to get: '%s' from: '%s'.", (PREFIX).format().c_str(), typeid(DEST).name(), typeid(SOURCE).name())

#define ERROR_GET_INDEXED_ITEM(PREFIX, SOURCE, DEST, IDX)                                                                      \
    TF_ERROR(USD_MDL, "%s\tUnable to get: '%s' from: '%s' at index: '%s'.", (PREFIX).format().c_str(), typeid(DEST).name(),    \
             typeid(SOURCE).name(), std::to_string(static_cast<uint64_t>(IDX)).c_str())

#define ERROR_COMPOUND_NOT_SUPPORTED(PREFIX, ITYPE, KIND, SIZE)                                                                \
    TF_ERROR(USD_MDL, "%s\t'%s' of kind: '%s' and size: '%s' not supported.", (PREFIX).format().c_str(), typeid(ITYPE).name(), (KIND).c_str(), \
             (SIZE).c_str())

#define ERROR_VTVALUE_GET(PREFIX, EXPECTED_TYPE, VTVALUE)                                                                      \
    TF_ERROR(USD_MDL, "%s\tUnable to process VtValue, expected '%s', actual: '%s'", (PREFIX).format().c_str(), typeid(EXPECTED_TYPE).name(), \
             (VTVALUE).GetTypeid().name())

/// Emitted by `MdlModule::GetSdrFromDiscoveryResult`'s legacy compatibility
/// fallback when USD_MDL_ALLOW_FIRST_OVERLOAD is enabled and a bare name
/// matches multiple definitions. The first SDK-enumerated overload is
/// used; the caller can disambiguate by re-authoring the subIdentifier
/// with parentheses (e.g. `my_func(int)`).
#define WARN_MULTIPLE_OVERLOADS(PREFIX, SUBIDENTIFIER, REPLACEMENT)                                                            \
    TF_WARN(USD_MDL, "%s\tsubIdentifier: '%s'."                                                                                \
                     "  Multiple results found for this subIdentifier due to function overloading."                            \
                     "  The first overload found will be used unless the parameter list is added to the subIdentifier."        \
                     "  Defaulting to: '%s'.",                                                                                 \
             (PREFIX).format().c_str(), (SUBIDENTIFIER), (REPLACEMENT))
