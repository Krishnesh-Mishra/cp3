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

#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/host/std_integer.hpp"
#include "detail/std_tables.hpp"

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
                                       "values", "forEach", "size",
                                       "union", "intersection", "difference",
                                       "isSubsetOf"};
const char* const kLruMemberNames[] = {"set",     "get",    "has",    "delete",
                                       "clear",   "size",   "capacity", "keys"};

// The documented members of a `Map` (docs/standard-library/04, Map / Set).
const DocRow kMapDocRows[] = {
    {"set", "set(key: K, value: V): void",
     "Stores `value` under `key`, replacing an earlier value."},
    {"get", "get(key: K): V | undefined",
     "Returns the value stored under `key`, or `undefined` when the key is "
     "not in the map."},
    {"has", "has(key: K): boolean",
     "True when `key` is in the map."},
    {"delete", "delete(key: K): boolean",
     "Removes `key` from the map and returns whether a key was actually "
     "removed."},
    {"clear", "clear(): void", "Removes every entry from the map."},
    {"keys", "keys(): K[]",
     "Returns the keys of the map, in insertion order."},
    {"values", "values(): V[]",
     "Returns the values of the map, in insertion order."},
    {"entries", "entries(): [K, V][]",
     "Returns the `[key, value]` pairs of the map, in insertion order."},
    {"groupBy", "groupBy(list: T[], callback: (item: T, index: number) => K): Map<K, T[]>",
     "The static `Map.groupBy`: groups the items by the callback's key."},
    {"forEach", "forEach(callback: (value: V, key: K) => void): void",
     "Calls `callback` for every entry, with the value first and the key "
     "second, in insertion order."},
    {"size", "size: number", "The number of entries in the map."},
};

// The documented members of a `Set` (docs/standard-library/04, Map / Set).
const DocRow kSetDocRows[] = {
    {"add", "add(value: T): void",
     "Adds `value` to the set. A value that is already in the set keeps its "
     "first position."},
    {"has", "has(value: T): boolean", "True when `value` is in the set."},
    {"delete", "delete(value: T): boolean",
     "Removes `value` from the set and returns whether a value was actually "
     "removed."},
    {"clear", "clear(): void", "Removes every value from the set."},
    {"values", "values(): T[]",
     "Returns the values of the set, in insertion order."},
    {"union", "union(other: Set<T>): Set<T>",
     "Returns a fresh set with every value of both sets."},
    {"intersection", "intersection(other: Set<T>): Set<T>",
     "Returns a fresh set with the values present in both sets."},
    {"difference", "difference(other: Set<T>): Set<T>",
     "Returns a fresh set with the values not present in `other`."},
    {"isSubsetOf", "isSubsetOf(other: Set<T>): boolean",
     "True when every value of this set is in `other`."},
    {"forEach", "forEach(callback: (value: T) => void): void",
     "Calls `callback` for every value, in insertion order."},
    {"size", "size: number", "The number of values in the set."},
};

// The documented members of an `LRUCache` (capacity-bounded, evicting).
const DocRow kLruDocRows[] = {
    {"set", "set(key: K, value: V): void",
     "Stores `value` under `key` as most recently used, evicting the least "
     "recently used entry when over capacity."},
    {"get", "get(key: K): V | undefined",
     "Returns the value stored under `key` and marks it most recently used, "
     "or `undefined` when the key is not in the cache."},
    {"has", "has(key: K): boolean",
     "True when `key` is in the cache (does not affect eviction order)."},
    {"delete", "delete(key: K): boolean",
     "Removes `key` from the cache and returns whether it was present."},
    {"clear", "clear(): void", "Removes every entry from the cache."},
    {"size", "size: number", "The number of entries in the cache."},
    {"capacity", "capacity: number", "The maximum number of entries."},
    {"keys", "keys(): K[]",
     "Returns the keys of the cache, most recently used first."},
};

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
    return t.kind == TypeKind::Class &&
           (t.name == "Map" || t.name == "Set" || t.name == "LRUCache");
}

// =============================================================================
// `new Map<K, V>()` / `new Set<T>()`
// =============================================================================
Type Analyzer::inferCollectionsConstruct(const std::string& name,
                                         TypeList& typeArgs) {
    if (name != "Map" && name != "Set" && name != "LRUCache")
        return Type::unknown();
    // Map/LRUCache carry { K, V }; Set carries { T }. Missing annotations
    // stay unknown; codegen reports CP3401 when the constructor has no
    // complete type argument list to lower.
    const std::size_t want = name == "Set" ? 1 : 2;
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
    if (recv.name == "LRUCache") {
        const Type first =
            recv.parts.empty() ? Type::unknown() : recv.parts.front();
        const Type second =
            recv.parts.size() >= 2 ? recv.parts[1] : Type::unknown();
        // The count/capacity surface is the std count width: `usize` in a
        // Rust project (`size_t` in a C++ one; number in TypeScript).
        if (prop == "size" || prop == "capacity") {
            return stdIntegerType(StdIntegerKind::Length, options_.typeSystem);
        }
        if (prop == "set") {
            return Type::function({first, second}, Type::voidType());
        }
        if (prop == "get") {
            return Type::function(
                {first}, Type::unionOf({second, Type::undefined()}));
        }
        if (prop == "has" || prop == "delete") {
            return Type::function({first}, Type::boolean());
        }
        if (prop == "clear") return Type::function({}, Type::voidType());
        if (prop == "keys") return Type::function({}, Type::array(first));
        reportMemberMiss("LRUCache", prop, span,
                         candidateNames(kLruMemberNames));
        return Type::unknown();
    }
    const bool isMap = recv.name == "Map";
    const Type first =
        recv.parts.empty() ? Type::unknown() : recv.parts.front();
    const Type second =
        recv.parts.size() >= 2 ? recv.parts[1] : Type::unknown();
    if (prop == "size") {
        return stdIntegerType(StdIntegerKind::Length, options_.typeSystem);
    }
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
    if (prop == "union" || prop == "intersection" || prop == "difference") {
        return Type::function({recv}, recv);
    }
    if (prop == "isSubsetOf") {
        return Type::function({recv}, Type::boolean());
    }
    if (prop == "forEach") {
        const Type cb = Type::function({first}, Type::voidType());
        return Type::function({cb}, Type::voidType());
    }
    if (prop == "clear") return Type::function({}, Type::voidType());
    reportMemberMiss("Set", prop, span, candidateNames(kSetMemberNames));
    return Type::unknown();
}

StdMemberDoc collectionsMemberDoc(const std::string& family,
                                   const std::string& prop) {
    if (family == "Map") return memberDoc(kMapDocRows, prop);
    if (family == "Set") return memberDoc(kSetDocRows, prop);
    if (family == "LRUCache") return memberDoc(kLruDocRows, prop);
    return StdMemberDoc{};
}

std::vector<std::string> collectionsMemberNames(const std::string& family) {
    if (family == "Map") return docRowNames(kMapDocRows);
    if (family == "Set") return docRowNames(kSetDocRows);
    if (family == "LRUCache") return docRowNames(kLruDocRows);
    return {};
}

// `Map.groupBy(list, callback)`: static; the result `Map<K, T[]>` follows
// the callback's return type, and the callback gets the item/index/array
// context exactly like the array callback methods (the signature is
// published so codegen lowers the arrow with matching parameter types).
bool Analyzer::tryInferCollectionsStaticCall(CallExpr& call, MemberExpr& m,
                                             Type& out) {
    if (!m.object || m.object->kind != NodeKind::Identifier ||
        static_cast<Identifier&>(*m.object).name != "Map" ||
        m.property != "groupBy") {
        return false;
    }
    Binding mapBinding;
    if (lookup("Map", mapBinding) && !mapBinding.isAmbient) {
        return false;  // a user `Map` shadows the std family
    }
    if (call.arguments.size() != 2) {
        report(diagcode::kArityMismatch, call.span,
               "arity mismatch: Map.groupBy expects exactly 2 arguments (the "
               "list and the key callback), got " +
                   std::to_string(call.arguments.size()));
        inferArgs(call);
        out = withType(call, Type::unknown());
        return true;
    }
    Type items = call.arguments.front() ? inferExpr(*call.arguments.front())
                                        : Type::unknown();
    if (items.kind != TypeKind::Array) {
        report(diagcode::kTypeMismatch, call.arguments.front()->span,
               "type mismatch: Map.groupBy's first argument must be an array");
        inferArgs(call);
        out = withType(call, Type::unknown());
        return true;
    }
    const Type elem = elemOf(items);
    Type key = Type::unknown();
    Expr* cb = call.arguments[1] ? call.arguments[1].get() : nullptr;
    if (cb != nullptr && cb->kind == NodeKind::ArrowFunction) {
        auto& arrow = static_cast<ArrowFunction&>(*cb);
        const std::vector<Type> context = {elem, Type::number(),
                                           Type::array(elem)};
        key = widen(inferArrowReturn(arrow, context));
        // Publish the inferred signature for codegen's parameter types.
        if (arrow.nodeId != 0) {
            FuncSig as;
            as.ret = key;
            as.declNodeId = arrow.nodeId;
            for (std::size_t i = 0; i < arrow.params.size(); ++i) {
                const Parameter* p = arrow.params[i].get();
                as.params.push_back(i < context.size() ? context[i]
                                                       : Type::unknown());
                as.optional.push_back(p != nullptr &&
                                      (p->optional || p->defaultValue != nullptr));
            }
            publishFunction(as, "");
            arrowSigs_[arrow.nodeId] = std::move(as);
        }
    } else if (cb != nullptr) {
        const Type fnType = inferExpr(*cb);
        if (fnType.kind == TypeKind::Function && !fnType.parts.empty()) {
            key = widen(fnType.parts.back());
        }
    }
    inferArgs(call);
    Type result(TypeKind::Class, "Map");
    result.parts = {key, Type::array(elem)};
    out = withType(call, result);
    return true;
}

}  // namespace cp3
