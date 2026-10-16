//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <pxr/pxr.h>

#include <string>

PXR_NAMESPACE_OPEN_SCOPE

/// Lightweight stack-allocated breadcrumb for building diagnostic prefix
/// strings lazily. Each node holds raw pointers to already-existing storage
/// (TfToken text, member strings, loop locals) and a parent pointer forming
/// a singly-linked list. format() is only called on the error/warning path,
/// so the happy path incurs zero heap allocations.
struct DebugContext
{
    const DebugContext* parent = nullptr;
    const char* label = nullptr;
    const char* name = nullptr;

    /// Build the full diagnostic prefix string by walking the parent chain.
    std::string format() const
    {
        const char* const safeLabel = label ? label : "context";
        const char* const safeName = name ? name : "<unnamed>";
        if (parent)
            return parent->format() + "\t" + safeLabel + ": " + safeName;
        return std::string(safeLabel) + ": " + safeName;
    }
};

PXR_NAMESPACE_CLOSE_SCOPE
