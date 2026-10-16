//
// SPDX-License-Identifier: Apache-2.0
//

#include "utils.h"

#include "UriEncoding.h"

#include <mi/mdl_sdk.h>

#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/base/vt/value.h>

#include <cctype>
#include <sstream>
#include <unordered_set>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

namespace Utils
{
bool
IsEligibleDefinition(const mi::neuraylib::IFunction_definition* definition)
{
    if (!definition || !definition->is_exported()) {
        return false;
    }
    mi::neuraylib::Mdl_version since, removed;
    definition->get_mdl_version(since, removed);
    return removed == mi::neuraylib::MDL_VERSION_INVALID;
}

bool
IsBuiltinModuleAsset(const SdfAssetPath& assetPath)
{
    const auto isAlias = [](const std::string& path) {
        const size_t separator = path.find_last_of("/\\");
        return path.substr(separator == std::string::npos ? 0 : separator + 1)
            == BuiltinModuleAsset;
    };
    // Resolvers may return an opaque storage identifier; the authored name
    // still carries the alias. Exact components avoid matching prefixed names.
    return isAlias(assetPath.GetAssetPath()) || isAlias(assetPath.GetResolvedPath());
}

namespace
{

std::string
_PythonQuote(const std::string& value)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('\'');
    for (const unsigned char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '\'': out += "\\'"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20 || c == 0x7f) {
                out += "\\x";
                out.push_back(kHex[c >> 4]);
                out.push_back(kHex[c & 0x0f]);
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    out.push_back('\'');
    return out;
}

template <class ARRAY_TYPE>
std::string
_ConvertVtTextArray(const VtValue& value)
{
    std::vector<std::string> strings;
    strings.reserve(value.GetArraySize());

    const VtArray<ARRAY_TYPE>& array = value.UncheckedGet<VtArray<ARRAY_TYPE>>();
    for (const ARRAY_TYPE& v : array) {
        std::ostringstream os;
        os << v;
        strings.emplace_back(_PythonQuote(os.str()));
    }

    return TfStringPrintf("[%s]", TfStringJoin(strings, ", ").c_str());
}

} // namespace

std::string
VtValueToString(const VtValue& value, const bool isCompoundElement)
{
    if (value.IsHolding<VtDictionary>()) {
        const VtDictionary& dict = value.UncheckedGet<VtDictionary>();

        std::vector<std::string> strings;
        strings.reserve(dict.size());

        for (VtDictionary::const_iterator it = dict.begin(); it != dict.end(); ++it) {
            strings.emplace_back(
                TfStringPrintf("%s:%s", _PythonQuote(it->first).c_str(),
                               VtValueToString(it->second, true).c_str()));
        }

        return TfStringPrintf("{%s}", TfStringJoin(strings, ",").c_str());
    }
    if (value.IsArrayValued()) {
        if (value.IsHolding<VtArray<std::string>>()) {
            return _ConvertVtTextArray<std::string>(value);
        }
        if (value.IsHolding<VtArray<TfToken>>()) {
            return _ConvertVtTextArray<TfToken>(value);
        }

        return TfStringify(value);
    }
    if (value.IsHolding<std::vector<VtValue>>()) {
        std::vector<std::string> parts;
        const std::vector<VtValue>& vec = value.UncheckedGet<std::vector<VtValue>>();
        parts.reserve(vec.size());
        for (const VtValue& v : vec) {
            parts.emplace_back(VtValueToString(v, true));
        }
        return TfStringPrintf("[%s]", TfStringJoin(parts, ", ").c_str());
    }
    if (value.IsEmpty()) {
        return "None";
    }
    if (isCompoundElement && (value.IsHolding<std::string>() || value.IsHolding<TfToken>())) {
        const std::string text = value.IsHolding<std::string>()
            ? value.UncheckedGet<std::string>()
            : value.UncheckedGet<TfToken>().GetString();
        return _PythonQuote(text);
    }

    return TfStringify(value);
}


namespace
{

std::string
_Trim(const std::string& s)
{
    size_t a = 0;
    size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) {
        ++a;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        --b;
    }
    return s.substr(a, b - a);
}

bool
_IsPrimitiveTypeToken(const std::string& tok)
{
    static const std::unordered_set<std::string> k{
        "bool",
        "int",
        "float",
        "double",
        "half",
        "string",
        "color",
        "uint",
        "void",
    };
    return k.count(tok) > 0;
}

} // namespace

std::string
StripMdlNameForFunctionOverloadLookup(const char* name)
{
    if (!name || !name[0]) {
        return std::string();
    }
    std::string s = _Trim(name);
    // Peel MDL signature / return-type decoration. MDL-SDK logs a deprecation warning if the name
    // passed to IModule::get_function_overloads() ends with ')'.
    while (!s.empty() && s.back() == ')') {
        const size_t open = s.rfind('(');
        if (open == std::string::npos) {
            // Stray trailing ')' with no '(' (e.g. "wrap_mode)" from MDL simple names for some enums).
            // IModule::get_function_overloads() warns on any name ending with ')'.
            s.pop_back();
            s = _Trim(s);
            continue;
        }
        const std::string inside = _Trim(s.substr(open + 1, s.size() - open - 1));
        std::string before = _Trim(s.substr(0, open));
        if (inside.find('(') != std::string::npos) {
            s = before;
            continue;
        }
        const size_t lastSp = before.find_last_of(" \t");
        const std::string lastToken =
            (lastSp == std::string::npos) ? before : before.substr(lastSp + 1);
        const bool beforeIsSingleToken = (before.find_first_of(" \t") == std::string::npos);
        if (beforeIsSingleToken && _IsPrimitiveTypeToken(before) && !inside.empty() &&
            inside.find(',') == std::string::npos) {
            s = inside;
            continue;
        }
        if (lastSp != std::string::npos) {
            s = lastToken;
            break;
        }
        s = before;
        break;
    }
    return s;
}

bool
ParseMdlSubIdentifier(const std::string& text, MdlSubIdentifier& result)
{
    result = {};
    const std::string s = _Trim(text);
    const size_t open = s.find('(');
    result.name = _Trim(s.substr(0, open));
    if (result.name.empty()
        || result.name.find_first_of(" \t\r\n),[]") != std::string::npos) {
        return false;
    }
    if (open == std::string::npos) {
        return true;
    }
    result.hasSignature = true;
    if (s.back() != ')') {
        return false;
    }
    const std::string inner = s.substr(open + 1, s.size() - open - 2);
    if (inner.find_first_of("()") != std::string::npos) {
        return false;
    }
    if (_Trim(inner).empty()) {
        return true;
    }
    size_t pos = 0;
    while (pos <= inner.size()) {
        const size_t comma = inner.find(',', pos);
        const std::string type = _Trim(inner.substr(pos, comma == std::string::npos
            ? std::string::npos : comma - pos));
        if (type.empty() || type.find_first_of(" \t\r\n;<>/*=&|{}") != std::string::npos) {
            return false;
        }
        // Validate token boundaries even when legacy material lookup ignores
        // type identity. A suffix must be one nonempty fixed/deferred array size.
        const size_t bracket = type.find('[');
        if (bracket != std::string::npos
            && (bracket == 0 || bracket + 2 >= type.size() || type.back() != ']'
                || type.find('[', bracket + 1) != std::string::npos)) {
            return false;
        }
        if (type.find(']') != (bracket == std::string::npos
                              ? std::string::npos : type.size() - 1)) {
            return false;
        }
        result.parameterTypes.push_back(type);
        if (comma == std::string::npos) {
            break;
        }
        pos = comma + 1;
    }
    return true;
}

namespace {

// Relative signature types use the containing package as their anchor.
// The resolved SDK name is authoritative, including for encoded off-path names;
// matching does not resolve files again or enforce additional shadowing rules.
std::string
_ExpandRelativeType(std::string type, const std::string& owner)
{
    std::string package = owner.substr(0, owner.rfind("::"));
    if (TfStringStartsWith(type, ".::")) {
        type.erase(0, 3);
    } else {
        while (TfStringStartsWith(type, "..::")) {
            if (package.empty()) {
                return {};
            }
            package.erase(package.rfind("::"));
            type.erase(0, 4);
        }
    }
    // A module and a type are required; `.::Local` is not a local-type alias.
    if (type.find("::") == std::string::npos) {
        return {};
    }
    return package + "::" + type;
}

bool
_MatchesParameterType(const std::string& requested, const std::string& actual,
                      const std::string& owner)
{
    if (requested == actual) {
        return true;
    }
    if (TfStringStartsWith(requested, ".::") || TfStringStartsWith(requested, "..::")) {
        return _ExpandRelativeType(requested, owner) == actual;
    }
    const std::string prefix = owner + "::";
    return requested.find("::") == std::string::npos
        && TfStringStartsWith(actual, prefix)
        && requested == actual.substr(prefix.size());
}

} // namespace

bool
MatchesMdlSubIdentifier(const MdlSubIdentifier& request,
                        const mi::neuraylib::IFunction_definition* definition)
{
    if (request.name != StripMdlNameForFunctionOverloadLookup(
            definition->get_mdl_simple_name())) {
        return false;
    }
    if (!request.hasSignature) {
        return true;
    }
    if (request.parameterTypes.size() != definition->get_parameter_count()) {
        return false;
    }
    for (mi::Size i = 0; i < definition->get_parameter_count(); ++i) {
        const char* actual = definition->get_mdl_parameter_type_name(i);
        if (!actual || !_MatchesParameterType(request.parameterTypes[i], actual,
                                               definition->get_mdl_module_name())) {
            return false;
        }
    }
    return true;
}

namespace {

// Prefer logical search-path identities. Only types in the owning module are
// unqualified; encoded off-path sibling identities remain package-relative.
std::string
_FormatParameterTypeName(const char* mdlTypeName,
                         const std::string& ownerModuleName)
{
    if (!mdlTypeName || !mdlTypeName[0]) {
        return std::string();
    }
    const std::string s(mdlTypeName);

    // Primitives / stdlib aggregates carry no scope qualifier.
    if (s.find("::") == std::string::npos || ownerModuleName.empty()) {
        return s;
    }

    // Same module: `::<owner>::Name[...]` -> `Name[...]`.
    const std::string ownerPrefix = ownerModuleName + "::";
    if (s.compare(0, ownerPrefix.size(), ownerPrefix) == 0) {
        return s.substr(ownerPrefix.size());
    }

    // Off-search-path sibling: only when the host module itself is an
    // encoded off-path name.  Express the reference relative to the
    // host's package (its containing directory) so no absolute path
    // leaks into the sub-identifier:
    //   `::<pkg>::sibling::Name[...]` -> `.::sibling::Name[...]`.
    if (usdMdl::runtime::IsEncodedMdlName(ownerModuleName)) {
        const size_t lastSep = ownerModuleName.rfind("::");
        if (lastSep != std::string::npos && lastSep > 0) {
            const std::string pkgPrefix =
                ownerModuleName.substr(0, lastSep) + "::";
            if (s.compare(0, pkgPrefix.size(), pkgPrefix) == 0) {
                return ".::" + s.substr(pkgPrefix.size());
            }
        }
        // Off-path but not under the host's package: can't express
        // with a single `.` anchor; leave verbatim for now.
        return s;
    }

    // In-search-path cross-module type: keep fully qualified.
    return s;
}

} // namespace

TfToken
ComputeMdlSubIdentifier(
    const mi::neuraylib::IFunction_definition* def,
    const mi::neuraylib::IModule* module)
{
    if (!def || !module) {
        return TfToken();
    }

    const bool isMaterial = def->is_material();
    const char* const simple = def->get_mdl_simple_name();
    const std::string overloadName = StripMdlNameForFunctionOverloadLookup(simple);
    mi::base::Handle<const mi::IArray> const funcOverloads(
        module->get_function_overloads(overloadName.c_str()));
    const bool isOverloaded = funcOverloads && funcOverloads->get_length() > 1;

    // A signature is needed only to distinguish overloads. Materials cannot
    // be overloaded, so adding one would make authored references needlessly
    // sensitive to changes in the parameter list.
    if (isMaterial || !isOverloaded) {
        return TfToken(simple ? simple : "");
    }

    // Overloaded functions: append the parameter signature.  Each
    // parameter's type name comes from the SDK via
    // `get_mdl_parameter_type_name` and is then rewritten by
    // `_FormatParameterTypeName` into the relocatable convention:
    // same-module types become `Name`, off-path sibling types
    // become `.::sibling::Name`, in-search-path cross-module types
    // (e.g. `::tex::wrap_mode`) stay fully qualified, and builtin types
    // pass through.
    if (!simple) {
        return TfToken();
    }
    const std::string ownerModuleName = def->get_mdl_module_name();

    std::string out(simple);
    out.push_back('(');
    const mi::Size paramCount = def->get_parameter_count();
    for (mi::Size i = 0; i < paramCount; ++i) {
        if (i != 0) {
            out.push_back(',');
        }
        out += _FormatParameterTypeName(
            def->get_mdl_parameter_type_name(i), ownerModuleName);
    }
    out.push_back(')');
    return TfToken(out);
}

} // namespace Utils

PXR_NAMESPACE_CLOSE_SCOPE
