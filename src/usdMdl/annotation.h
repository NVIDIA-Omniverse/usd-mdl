//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MDL_ANNOTATION_H
#define MDL_ANNOTATION_H

#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/usd/sdr/declare.h>

#include "debugContext.h"
#include "tokens.h"

#include <mi/mdl_sdk.h>


PXR_NAMESPACE_OPEN_SCOPE

namespace Annotation
{

// Append annotations in SDK block order to an ordered vector of records under
// annotationPrefixes / "annotations" (by default mdl.annotation / annotations).
// Each record stores the full qualified SDK name, including its signature, and
// one complete arguments dictionary; zero-argument markers have an empty dict.
// Repeated names and overloads remain separate, including across calls. Example:
//
//   { "annotations": [
//       { "name": "::anno::soft_range(float,float)",
//         "arguments": { "min": 0, "max": 10 } } ] }
void GetMetadata(const mi::neuraylib::IAnnotation_block* const iAnnotationBlock,
                 const DebugContext& debugCtx,
                 VtDictionary& metadata,
                 const std::vector<std::string>& annotationPrefixes = { UsdMdlMetadataTokens->Annotation.GetString() });

// Promote SDR__-prefixed annotations matching SdrNodeMetadata token names,
// then map ::anno::display_name / description as fallbacks. Direct annotations
// win, including empty values. All eligible direct occurrences must agree:
// differing values warn and return false without changing the output map,
// including repeated names and overloads. SDK scalar fallbacks use the last
// complete eligible occurrence. Every source record remains in raw metadata.
bool BuildNodeMetadata(VtDictionary& dict, SdrTokenMap& metadata);

// Promote SDR__-prefixed annotations matching SdrPropertyMetadata names, then apply
// ::anno fallbacks (display_name, description, in_group, usage) and hints.
// Conflicting direct values warn and return false, as for node metadata.
// Single arguments use their value regardless of argument name; zero-argument
// markers become "1"; multi-argument occurrences only contribute raw records.
// Boolean metadata uses "0"/"1" and string-array metadata uses "|" separators.
// Distinct ::anno::usage hints are joined in first-occurrence order with "|";
// SDR__hints takes precedence. Other SDK fallbacks/hints use the last complete occurrence.
// Unannotated properties get connectable="1". enable_if remains opaque MDL
// expression text in hints, without promotion to USD shownIf. No records are
// removed by promotion or by flattening metadata to string-valued Sdr maps.
bool BuildPropertyMetadata(VtDictionary& dict,
                           SdrTokenMap& metadata,
                           SdrTokenMap& hints);

};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // MDL_ANNOTATION_H
