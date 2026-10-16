// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "api.h"
#include <pxr/base/vt/value.h>
namespace mi { namespace neuraylib {
class IValue_enum;
class IValue_factory;
class IType_enum;
}}

PXR_NAMESPACE_OPEN_SCOPE

/// Convert a symbolic USD token/string or native integer code to an SDK enum
/// value of the supplied original MDL type. Symbolic values match member names,
/// never integer positions. This supports sparse codes and named aliases.
/// The returned interface has one retained reference; the caller releases it.
/// Factory/type handles must belong to the caller's live SDK transaction.
/// Unsupported values or unknown members emit a diagnostic and return nullptr.
USDMDL_API
mi::neuraylib::IValue_enum* UsdMdlCreateEnumValue(
    mi::neuraylib::IValue_factory* factory,
    const mi::neuraylib::IType_enum* type,
    const VtValue& value);

PXR_NAMESPACE_CLOSE_SCOPE
