//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "api.h"

#include <pxr/pxr.h>

namespace mi {
namespace neuraylib {
class IMdl_entity_resolver;
class INeuray;
}
}

PXR_NAMESPACE_OPEN_SCOPE

/// Initialize usdMdl's MDL runtime integration.
///
/// When \p neuray is null, usdMdl loads libmdl_sdk, creates and starts its own
/// INeuray instance, and owns its shutdown. The library is loaded by name
/// using the operating system's search rules; no build-time SDK path is used.
/// When an instance is supplied, usdMdl retains it only while attached;
/// the caller remains responsible for
/// its final shutdown and the SDK library's lifetime.
///
/// A supplied instance must already be STARTED; other states are rejected
/// without configuring or starting it. The host is responsible for startup
/// and pre-start setup such as loading MDL image plugins. Search paths from
/// usdMdl's environment settings are added during attachment. The plugin
/// replaces the entity resolver; it does not save the previous resolver or
/// search paths for restoration on detach or attachment failure. Hosts that
/// need restoration must retain their custom resolver and save their paths.
///
/// Call UsdMdlShutdown() before the host shuts down an injected instance.
/// Calling this function again with the same active instance is harmless;
/// attempting to replace an active instance fails.
///
/// Runtime lifecycle calls must be externally serialized with all discovery,
/// parsing, and module-loading work. Do not initialize or shut down usdMdl
/// while another thread is using its Sdr plugins or runtime handles.
USDMDL_API
bool UsdMdlInitialize(mi::neuraylib::INeuray* neuray);

/// Create a plugin-owned instance using an explicit SDK library path.
/// A null or empty \p mdlSdkLibraryPath
/// uses the platform library name (libmdl_sdk.dll/.so/.dylib) and the operating
/// system's library search rules. An explicit path is used without fallback.
/// The SDK's dependencies must also be available to the platform loader.
///
/// The path is used only on first initialization; an active runtime is not
/// replaced. The host-instance overload's lifecycle requirements also apply.
USDMDL_API
bool UsdMdlInitialize(const char* mdlSdkLibraryPath);

/// Create a plugin-owned instance by loading libmdl_sdk by name with the
/// operating system's library search rules. No build-time SDK path is used.
/// The host-instance overload's lifecycle requirements also apply.
USDMDL_API
bool UsdMdlInitialize();

/// Return the active INeuray instance, or nullptr when usdMdl is not initialized.
/// This query does not initialize the runtime. A non-null result has its reference
/// count increased; the caller owns that reference and should adopt it into a
/// mi::base::Handle. Release it and all SDK handles and transactions obtained
/// through it before calling UsdMdlShutdown(). Owning a reference does not grant
/// responsibility for shutdown: do not shut down a plugin-owned instance yourself.
/// The lifecycle serialization contract of UsdMdlInitialize() also applies.
USDMDL_API
mi::neuraylib::INeuray* UsdMdlGetNeuray();

/// Return usdMdl's active Ar-backed MDL entity resolver, or nullptr when the
/// runtime is not initialized. This query does not initialize the runtime.
/// A non-null result has its reference count increased; the caller owns that
/// reference and should adopt it into a mi::base::Handle. Release it and any
/// resolver results before calling UsdMdlShutdown(). The lifecycle serialization
/// contract of UsdMdlInitialize() also applies.
USDMDL_API
mi::neuraylib::IMdl_entity_resolver* UsdMdlGetEntityResolver();

/// Detach usdMdl from the active MDL runtime. This clears cached Sdr nodes,
/// releases SDK handles, and uninstalls usdMdl's Ar entity resolver. A
/// self-created INeuray is also shut down and its SDK library unloaded; an
/// injected INeuray remains started and owned by the caller.
/// The previous entity resolver is not restored, and search paths remain
/// configured. Restoring the host's previous configuration is its responsibility.
///
/// Safe to call repeatedly. If usdMdl has not been initialized, this is a
/// no-op. It must not run concurrently with discovery, parsing, or module
/// loading; quiesce those users first.
USDMDL_API
void UsdMdlShutdown();

PXR_NAMESPACE_CLOSE_SCOPE
