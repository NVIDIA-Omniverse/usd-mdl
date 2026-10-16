//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "defines.h"
#include "../api.h"

#include <mi/mdl_sdk.h>

#include <string>
#include <vector>

MDL_RUNTIME_NAMESPACE_OPEN

/// Original request carried by the private unresolved-resource identity.
struct RejectedResourceInfo
{
    std::string resourcePath;
    std::string ownerModule;
    std::string ownerFile;
    mi::Sint32 line = 0;
    mi::Sint32 column = 0;
    std::string reason;
};

/// The marker is an absolute MDL path containing the complete original request.
/// It remains useful when imported defaults outlive their loading context.
USDMDL_API
std::string EncodeRejectedResource(const RejectedResourceInfo& info);

/// Recognize all versions of the reserved namespace, including malformed paths.
/// Reserved paths must never reach Ar even when decoding fails.
USDMDL_API
bool IsRejectedResourcePath(const std::string& path);

/// Decode only the supported version. On failure, leave info unchanged.
USDMDL_API
bool DecodeRejectedResource(const std::string& path, RejectedResourceInfo& info);

/// Attach owned per-load records through the SDK's user_data option. Existing
/// foreign user_data is preserved; markers still work without attached state.
USDMDL_API
bool AttachRejectedResourceState(mi::neuraylib::IMdl_execution_context* context);

/// Record an original request once per owner, path and source position.
USDMDL_API
void RecordRejectedResource(const mi::neuraylib::IMdl_execution_context* context,
                            const RejectedResourceInfo& info);

/// Snapshot records without retaining the SDK context or holding its locks.
USDMDL_API
std::vector<RejectedResourceInfo> GetRejectedResourceRecords(
    const mi::neuraylib::IMdl_execution_context* context);

MDL_RUNTIME_NAMESPACE_CLOSE
