//
// SPDX-License-Identifier: Apache-2.0
//

#include "annotation.h"

#include "converter/converter.h"
#include "utils.h"

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/sdr/parserPlugin.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>

#include <string>
#include <unordered_set>
#include <vector>

using namespace mi::neuraylib;

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((deprecated, "deprecated"))
    ((description, "description"))
    ((display_name, "display_name"))
    ((enable_if, "enable_if"))
    ((group, "group"))
    ((hard_range, "hard_range"))
    ((hidden, "hidden"))
    ((hint, "hint"))
    ((in_group, "in_group"))
    ((name, "name"))
    ((soft_range, "soft_range"))
    ((subgroup, "subgroup"))
    ((subsubgroup, "subsubgroup"))
    ((ui_order, "ui_order"))
    ((unused, "unused"))
    ((usage, "usage"))
);
// clang-format on

namespace Annotation
{

namespace
{

using AnnotationRecords = std::vector<VtValue>;
static const char* const _annotationsKey = "annotations";
static const char* const _argumentsKey = "arguments";

static AnnotationRecords _CollectAnnotations(const IAnnotation_block* const iAnnotationBlock,
                                             const DebugContext& debugCtx)
{
    AnnotationRecords records;
    if (!iAnnotationBlock)
    {
        return records;
    }

    const mi::Size numAnnotations = iAnnotationBlock->get_size();
    records.reserve(numAnnotations);
    for (mi::Size i = 0; i < numAnnotations; ++i)
    {
        const mi::base::Handle<const IAnnotation> annotation(iAnnotationBlock->get_annotation(i));
        if (!annotation)
        {
            ERROR_GET_ITEM(debugCtx, IAnnotation_block, IAnnotation);
            return records;
        }
        const char* annotationName = annotation->get_name();
        if (!annotationName)
        {
            continue;
        }
        const mi::base::Handle<const IExpression_list> expressionList(annotation->get_arguments());
        if (!expressionList)
        {
            ERROR_GET_ITEM(debugCtx, IExpression_list, IAnnotation);
            return records;
        }

        VtDictionary arguments;
        bool converted = true;
        const mi::Size numExpressions = expressionList->get_size();
        for (mi::Size j = 0; j < numExpressions; ++j)
        {
            const mi::base::Handle<const IExpression> expression(expressionList->get_expression(j));
            if (!expression)
            {
                ERROR_GET_ITEM(debugCtx, IExpression, IExpression_list);
                return records;
            }
            // Annotation arguments are constants and cannot contain resources.
            const VtValue value =
                Converter::ConvertConstantExpression(expression.get(), /*mdlModule=*/nullptr, debugCtx);
            const char* argumentName = expressionList->get_name(j);
            if (value.IsEmpty() || !argumentName)
            {
                converted = false;
                continue;
            }
            arguments[argumentName] = value;
        }
        if (converted)
        {
            VtDictionary record;
            record[_tokens->name.GetString()] = VtValue(std::string(annotationName));
            record[_argumentsKey] = VtValue(std::move(arguments));
            records.emplace_back(std::move(record));
        }
    }
    return records;
}

static const AnnotationRecords& _GetAnnotations(const VtDictionary& dict)
{
    const VtValue* value = dict.GetValueAtPath(
        {UsdMdlMetadataTokens->Annotation.GetString(), _annotationsKey});
    if (value && value->IsHolding<AnnotationRecords>())
    {
        return value->UncheckedGet<AnnotationRecords>();
    }
    static const AnnotationRecords empty;
    return empty;
}

static bool _ReadAnnotation(const VtValue& value,
                            const std::string*& name,
                            const VtDictionary*& arguments)
{
    if (!value.IsHolding<VtDictionary>())
    {
        return false;
    }
    const auto& record = value.UncheckedGet<VtDictionary>();
    const auto nameEntry = record.find(_tokens->name.GetString());
    const auto argumentsEntry = record.find(_argumentsKey);
    if (nameEntry == record.end() || !nameEntry->second.IsHolding<std::string>() ||
        argumentsEntry == record.end() || !argumentsEntry->second.IsHolding<VtDictionary>())
    {
        return false;
    }
    name = &nameEntry->second.UncheckedGet<std::string>();
    arguments = &argumentsEntry->second.UncheckedGet<VtDictionary>();
    return true;
}

static std::string _QualifiedAnnotationName(const std::string& name)
{
    return name.substr(0, name.find('('));
}

static bool _IsBuiltinAnnotation(const std::string& name, const TfToken& annotationKey)
{
    const std::string qualifiedName = _QualifiedAnnotationName(name);
    const std::string expected = "::anno::" + annotationKey.GetString();
    if (qualifiedName == expected)
    {
        return true;
    }
    // The SDK can retain a language-version suffix on older built-ins, such as
    // ::anno::in_group$1.7. Normalize identity here, never the stored raw name.
    if (!TfStringStartsWith(qualifiedName, expected + "$"))
    {
        return false;
    }
    const std::string version = qualifiedName.substr(expected.size() + 1);
    const auto dot = version.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == version.size() ||
        version.find('.', dot + 1) != std::string::npos)
    {
        return false;
    }
    for (const char character : version)
    {
        if (character != '.' && (character < '0' || character > '9'))
        {
            return false;
        }
    }
    return true;
}

static const VtDictionary* _LastBuiltinArguments(const VtDictionary& dict,
                                                const TfToken& annotationKey)
{
    const VtDictionary* last = nullptr;
    for (const auto& record : _GetAnnotations(dict))
    {
        const std::string* name = nullptr;
        const VtDictionary* arguments = nullptr;
        if (_ReadAnnotation(record, name, arguments) &&
            _IsBuiltinAnnotation(*name, annotationKey))
        {
            last = arguments;
        }
    }
    return last;
}

static std::string _MetadataString(const VtValue& value)
{
    if (value.IsHolding<bool>())
    {
        return value.UncheckedGet<bool>() ? "1" : "0";
    }
    if (value.IsHolding<VtArray<std::string>>())
    {
        const auto& values = value.UncheckedGet<VtArray<std::string>>();
        return TfStringJoin(std::vector<std::string>(values.begin(), values.end()), "|");
    }
    return Utils::VtValueToString(value);
}

static void _PromoteToHints(const VtDictionary& dict, SdrTokenMap& hints)
{
    static const TfTokenVector hintKeys = {
        _tokens->deprecated, _tokens->enable_if, _tokens->hard_range, _tokens->hidden,
        _tokens->soft_range, _tokens->ui_order, _tokens->unused};
    for (const TfToken& key : hintKeys)
    {
        const VtDictionary* arguments = _LastBuiltinArguments(dict, key);
        if (!arguments)
        {
            continue;
        }
        std::string text;
        const auto condition = arguments->find("condition");
        if (key == _tokens->enable_if && condition != arguments->end() &&
            condition->second.IsHolding<std::string>())
        {
            // MDL expression text belongs to the consuming UI; preserve it
            // without evaluating it or translating it into USD shownIf.
            text = condition->second.UncheckedGet<std::string>();
        }
        else
        {
            text = arguments->empty() ? "1" : Utils::VtValueToString(VtValue(*arguments));
        }
        hints.emplace(key, text);
    }
}

static TfToken _FindMetadataKey(const std::string& annotationName,
                                const TfTokenVector& keys)
{
    const std::string qualifiedName = _QualifiedAnnotationName(annotationName);
    const auto separator = qualifiedName.rfind("::");
    const std::string basename = separator == std::string::npos
        ? qualifiedName : qualifiedName.substr(separator + 2);
    static const std::string prefix = "SDR__";
    if (!TfStringStartsWith(basename, prefix))
    {
        return TfToken();
    }
    const std::string name = basename.substr(prefix.size());
    for (const TfToken& key : keys)
    {
        const std::string& keyName = key.GetString();
        if (name == keyName ||
            (TfStringStartsWith(keyName, "__SDR__") && name == keyName.substr(7)))
        {
            return key;
        }
    }
    return TfToken();
}

static bool _PromoteDirectMetadata(const VtDictionary& dict,
                                   SdrTokenMap& metadata,
                                   const TfTokenVector& keys)
{
    SdrTokenMap directMetadata;
    SdrTokenMap sources;
    for (const auto& record : _GetAnnotations(dict))
    {
        const std::string* name = nullptr;
        const VtDictionary* arguments = nullptr;
        if (!_ReadAnnotation(record, name, arguments))
        {
            continue;
        }
        const TfToken key = _FindMetadataKey(*name, keys);
        // Multiple arguments cannot identify a single metadata value, but must
        // not conceal an earlier or later eligible occurrence of the same name.
        if (key.IsEmpty() || arguments->size() > 1)
        {
            continue;
        }
        const std::string text = arguments->empty()
            ? "1" : _MetadataString(arguments->begin()->second);
        const auto inserted = directMetadata.emplace(key, text);
        if (inserted.second)
        {
            sources.emplace(key, *name);
        }
        else if (inserted.first->second != text)
        {
            TF_WARN("Conflicting Sdr metadata annotations '%s' and '%s' for field '%s'.",
                    sources.at(key).c_str(), name->c_str(), key.GetText());
            return false;
        }
    }
    // Check all occurrences before changing the output. Raw records, including
    // promoted annotations, remain intact for consumers needing full detail.
    for (const auto& entry : directMetadata)
    {
        metadata[entry.first] = entry.second;
    }
    return true;
}

static void _AddBuiltinMetadata(const VtDictionary& dict,
                                SdrTokenMap& metadata,
                                const TfToken& annotationKey,
                                const TfToken& argumentKey,
                                const TfToken& metadataKey)
{
    const VtDictionary* arguments = _LastBuiltinArguments(dict, annotationKey);
    if (arguments)
    {
        const auto value = arguments->find(argumentKey.GetString());
        if (value != arguments->end())
        {
            // Direct metadata wins even when its value is empty.
            metadata.emplace(metadataKey, _MetadataString(value->second));
        }
    }
}

static void _PromoteCommonAnnotations(const VtDictionary& dict,
                                      SdrTokenMap& metadata,
                                      const TfToken& helpKey,
                                      const TfToken& labelKey)
{
    _AddBuiltinMetadata(dict, metadata, _tokens->description, _tokens->description, helpKey);
    _AddBuiltinMetadata(dict, metadata, _tokens->display_name, _tokens->name, labelKey);
}

static void _PromotePropertyAnnotations(const VtDictionary& dict, SdrTokenMap& metadata)
{
    _PromoteCommonAnnotations(dict, metadata,
        SdrPropertyMetadata->Help, SdrPropertyMetadata->Label);

    static const TfTokenVector pageKeys = {
        _tokens->group, _tokens->subgroup, _tokens->subsubgroup};
    if (const VtDictionary* inGroup = _LastBuiltinArguments(dict, _tokens->in_group))
    {
        std::vector<std::string> pages;
        for (const TfToken& pageKey : pageKeys)
        {
            const auto page = inGroup->find(pageKey.GetString());
            if (page == inGroup->end())
            {
                break;
            }
            const std::string text = _MetadataString(page->second);
            if (text.empty())
            {
                break;
            }
            pages.push_back(text);
        }
        if (!pages.empty())
        {
            metadata.emplace(SdrPropertyMetadata->Page,
                TfStringJoin(pages, SdrPropertyTokens->PageDelimiter.GetText()));
        }
    }

    std::vector<std::string> usage;
    std::unordered_set<std::string> seenUsage;
    for (const auto& record : _GetAnnotations(dict))
    {
        const std::string* name = nullptr;
        const VtDictionary* arguments = nullptr;
        if (!_ReadAnnotation(record, name, arguments) ||
            !_IsBuiltinAnnotation(*name, _tokens->usage))
        {
            continue;
        }
        const auto hint = arguments->find(_tokens->hint.GetString());
        if (hint != arguments->end())
        {
            const std::string text = _MetadataString(hint->second);
            if (seenUsage.insert(text).second)
            {
                usage.push_back(text);
            }
        }
    }
    if (!usage.empty())
    {
        metadata.emplace(SdrPropertyMetadata->Hints, TfStringJoin(usage, "|"));
    }
}

static void _FlattenMetadata(const VtDictionary& dict, SdrTokenMap& metadata)
{
    for (const auto& entry : dict)
    {
        metadata.emplace(TfToken(entry.first), Utils::VtValueToString(entry.second));
    }
}

} // namespace

void GetMetadata(const IAnnotation_block* const iAnnotationBlock,
                 const DebugContext& debugCtx,
                 VtDictionary& metadata,
                 const std::vector<std::string>& annotationPrefixes)
{
    AnnotationRecords records = _CollectAnnotations(iAnnotationBlock, debugCtx);
    if (records.empty())
    {
        return;
    }
    std::vector<std::string> keyPath = annotationPrefixes;
    keyPath.emplace_back(_annotationsKey);
    if (const VtValue* existing = metadata.GetValueAtPath(keyPath))
    {
        if (existing->IsHolding<AnnotationRecords>())
        {
            AnnotationRecords combined = existing->UncheckedGet<AnnotationRecords>();
            combined.insert(combined.end(), records.begin(), records.end());
            records = std::move(combined);
        }
    }
    metadata.SetValueAtPath(keyPath, VtValue(std::move(records)));
}

bool BuildNodeMetadata(VtDictionary& dict, SdrTokenMap& metadata)
{
    if (!_PromoteDirectMetadata(dict, metadata, SdrNodeMetadata->allTokens))
    {
        return false;
    }
    _PromoteCommonAnnotations(dict, metadata, SdrNodeMetadata->Help, SdrNodeMetadata->Label);
    _FlattenMetadata(dict, metadata);
    return true;
}

bool BuildPropertyMetadata(VtDictionary& dict,
                           SdrTokenMap& metadata,
                           SdrTokenMap& hints)
{
    if (!_PromoteDirectMetadata(dict, metadata, SdrPropertyMetadata->allTokens))
    {
        return false;
    }
    _PromotePropertyAnnotations(dict, metadata);
    metadata.emplace(SdrPropertyMetadata->Connectable, "1");
    _PromoteToHints(dict, hints);
    _FlattenMetadata(dict, metadata);
    return true;
}

} // Annotation


PXR_NAMESPACE_CLOSE_SCOPE
