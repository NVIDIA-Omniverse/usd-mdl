//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MDL_PLUGIN_USDMDL_API_H
#define MDL_PLUGIN_USDMDL_API_H

#include <pxr/base/arch/export.h>

// Mirrors OpenUSD's per-library api.h pattern (cf. pxr/usd/sdr/api.h) with
// the OpenUSD global `PXR_STATIC` extended by the library-specific
// `USDMDL_STATIC` so we can opt this library into static linkage even when
// the rest of the USD stack is shared. Today usdMdl is built SHARED and its
// CMake target defines USDMDL_EXPORTS only while compiling the producer.
//
//   - PXR_STATIC / USDMDL_STATIC -> macros expand to nothing
//   - USDMDL_EXPORTS             -> producer side (the usdMdl translation units)
//   - default                    -> consumer side (importers of a shared usdMdl)
#if defined(PXR_STATIC) || defined(USDMDL_STATIC)
#   define USDMDL_API
#   define USDMDL_API_TEMPLATE_CLASS(...)
#   define USDMDL_API_TEMPLATE_STRUCT(...)
#   define USDMDL_LOCAL
#else
#   if defined(USDMDL_EXPORTS)
#       define USDMDL_API ARCH_EXPORT
#       define USDMDL_API_TEMPLATE_CLASS(...) ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#       define USDMDL_API_TEMPLATE_STRUCT(...) ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#   else
#       define USDMDL_API ARCH_IMPORT
#       define USDMDL_API_TEMPLATE_CLASS(...) ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#       define USDMDL_API_TEMPLATE_STRUCT(...) ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#   endif
#   define USDMDL_LOCAL ARCH_HIDDEN
#endif

#endif
