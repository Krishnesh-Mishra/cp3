// =============================================================================
// src/std/collections/sema.cpp - `std/collections` `Map`/`Set` typing.
//
// OWNERSHIP: the std/collections family (src/std/collections/**). `Map<K, V>`
// and `Set<T>` are nominal `TypeKind::Class` types carrying their type
// arguments (parts == { K, V } / { T }), the same shape as `Promise<T>` (spec
// §14); the constructor grammar is `new Map<string, number>()`.
//
// The member tables below ARE the docs/04 surface: they drive member typing
// and the CP3305 candidate list at once (`reportMemberMiss`, wired from
// member_check.cpp's `memberOfProbe` Class branch), so the two cannot drift
// apart. `entries()` carries the canonical `$mapEntry` tuple label so codegen
// recognizes the entries array whose element has no C++ spelling.
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <string>
#include <vector>

namespace cp3 {

namespace {

// The canonical label of a `Map` entry tuple `[K, V]` (the element of
// `entries()`). The tuple has no C++ spelling of its own; codegen recognizes
// the label to lower `.length` on the runtime entries array.
constexpr const char* kMapEntryLabel = "$mapEntry";

Type mapEntryType(const Type& key, const Type& value) {
    Type t(TypeKind::Object, kMapEntryLabel);
    t.parts.push_back(key);
    t.parts.push_back(value);
    return t;
}

// The docs/04 surface, in table order; the names drive the CP3305 candidates.
const char* const kMapMemberNames[] = {"set",     "get",     "has",     "delete",
                                       "clear",   "keys",    "values",  "entries",
                                       "forEach", "size"};
const char* const kSetMemberNames[] = {"add",   "has",   "delete",  "clear",
                                       "values", "forEach", "size"};

template <std::size_t N>
std::vector<std::string> candidateNames(const char* const (&names)[N]) {
    std::vector<std::string> out;
    out.reserve(N);
    for (const char* name : names) out.emplace_back(name);
    return out;
}

}  // namespace

// =============================================================================
// The family model (shared with scopes.h and member_check.cpp)
// =============================================================================
bool isCollectionsClass(const Type& t) {
    return t.kind == TypeKind::Class && (t.name == "Map" || t.name == "Set");
}

// =============================================================================
// `new Map<K, V>()` / `new Set<T>()`
// =============================================================================
Type Analyzer::inferCollectionsConstruct(const std::string& name,
                                         TypeList& typeArgs) {
    if (name != "Map" && name != "Set") return Type::unknown();
    // Map carries { K, V }; Set carries { T }. Missing annotations stay
    // unknown and become a CP3002 skip at codegen instead of a false accept.
    const std::size_t want = name == "Map" ? 2 : 1;
    std::vector<Type> parts;
    parts.reserve(want);
    for (std::size_t i = 0; i < want; ++i) {
        if (i < typeArgs.size() && typeArgs[i]) {
            parts.push_back(resolveType(*typeArgs[i]));
        } else {
            parts.push_back(Type::unknown());
        }
    }
    Type t(TypeKind::Class, name);
    t.parts = std::move(parts);
    return t;
}

// =============================================================================
// The member surface
// =============================================================================
Type Analyzer::collectionsMember(const Type& recv, const std::string& prop,
                                 const SourceSpan& span) {
    const bool isMap = recv.name == "Map";
    const Type first =
        recv.parts.empty() ? Type::unknown() : recv.parts.front();
    const Type second =
        recv.parts.size() >= 2 ? recv.parts[1] : Type::unknown();
    if (prop == "size") return Type::number();
    if (isMap) {
        if (prop == "set") {
            return Type::function({first, second}, Type::voidType());
        }
        if (prop == "get") {
            // A missing key yields `undefined` (docs/04).
            return Type::function({first},
                                  Type::unionOf({second, Type::undefined()}));
        }
        if (prop == "has" || prop == "delete") {
            return Type::function({first}, Type::boolean());
        }
        if (prop == "keys") return Type::function({}, Type::array(first));
        if (prop == "values") return Type::function({}, Type::array(second));
        if (prop == "entries") {
            return Type::function({}, Type::array(mapEntryType(first, second)));
        }
        if (prop == "forEach") {
            // JS callback order: (value, key).
            const Type cb = Type::function({second, first}, Type::voidType());
            return Type::function({cb}, Type::voidType());
        }
        if (prop == "clear") return Type::function({}, Type::voidType());
        reportMemberMiss("Map", prop, span, candidateNames(kMapMemberNames));
        return Type::unknown();
    }
    if (prop == "add") return Type::function({first}, Type::voidType());
    if (prop == "has" || prop == "delete") {
        return Type::function({first}, Type::boolean());
    }
    if (prop == "values") return Type::function({}, Type::array(first));
    if (prop == "forEach") {
        const Type cb = Type::function({first}, Type::voidType());
        return Type::function({cb}, Type::voidType());
    }
    if (prop == "clear") return Type::function({}, Type::voidType());
    reportMemberMiss("Set", prop, span, candidateNames(kSetMemberNames));
    return Type::unknown();
}

}  // namespace cp3
