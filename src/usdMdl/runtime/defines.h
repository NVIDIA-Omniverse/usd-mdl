//
// SPDX-License-Identifier: Apache-2.0
//
// Namespace helpers for the `usdMdl::runtime` layer.
//
// Runtime sources are compiled directly into the merged `usdMdl`
// shared library; export annotations come from `usdMdl/api.h`
// (`USDMDL_API`).  This file defines only the namespace alias so
// runtime headers can open `usdMdl::runtime` without inlining the
// `namespace usdMdl { namespace runtime { ... } }` boilerplate.
//
// Runtime headers that publish symbols across the shared-library
// boundary (consumed by `_usdMdl` and test executables) include
// `usdMdl/api.h` explicitly in addition to this header.

#ifndef MDL_PLUGIN_USDMDL_RUNTIME_DEFINES_H
#define MDL_PLUGIN_USDMDL_RUNTIME_DEFINES_H

#define MDL_RUNTIME_NS               usdMdl::runtime
#define MDL_RUNTIME_NAMESPACE_OPEN   namespace usdMdl { namespace runtime {
#define MDL_RUNTIME_NAMESPACE_CLOSE  } }

#endif
