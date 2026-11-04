// =============================================================================
// src/std/collections/codegen.cpp - `std/collections` `Map`/`Set` lowering.
//
// Lowers the family registered by sema (src/std/collections/sema.cpp) onto
// cp3/std/collections.hpp: `new Map<K, V>()` / `new Set<T>()`, the member
// calls (`set`/`get`/`has`/`delete`/`clear`/`keys`/`values`/`entries`/
// `forEach`), the `size` property read and the `entries().length` special
// case (the entry tuple has no C++ spelling, but its array does report a
// size). `classifyCollections` (type_map.h) owns the type model; this unit
// owns the emission only.
//
// A member outside the sema table never reaches this unit: sema reports CP3305
// (src/std/collections/sema.cpp). A `Map`/`Set` type the runtime cannot hold
// (a key that is not hashable, a value the runtime cannot compare) is a type
// error (CP3401), and a `forEach` callback that does not fit the collection's
// parameter list is one too.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "detail/generator/cpp_writer.h"
#include "detail/generator/type_map.h"
#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

namespace detail {

namespace {

// The shapes a `Map` value / a `Set` element can have: the runtime holds the
// value and compares it with `==` (a `Set` deduplicates its elements that
// way), so a primitive, an enum, a handle (an object, a class instance, an
// array, a nested collection - identity, like JS), a value union and an
// optional all fit.
bool comparableShape(Shape s) {
    switch (s) {
        case Shape::Number:
        case Shape::String:
        case Shape::Bool:
        case Shape::Native:
        case Shape::Enum:
        case Shape::Array:
        case Shape::Object:
        case Shape::ClassRef:
        case Shape::Collections:
        case Shape::Union:
        case Shape::Optional:
            return true;
        default:
            return false;
    }
}

}  // namespace

// The family's C++ mapping (see type_map.h): `Map<K, V>` -> the runtime's
// insertion-ordered `Map<K, V>` handle, `Set<T>` likewise. A Map's key is
// hashed, so it must be a primitive; the value (and a Set's element) is a
// value type the runtime can hold and compare.
bool classifyCollections(const Type& t, CppInfo& info) {
    if (t.kind != TypeKind::Class) return false;
    const bool isMap = t.name == "Map";
    const bool isLru = t.name == "LRUCache";
    if (!isMap && !isLru && t.name != "Set") return false;
    if (t.parts.empty()) return false;
    const CppInfo first = classify(t.parts.front());
    if (!first.representable) return false;
    std::string cpp;
    if (isMap || isLru) {
        if (!isPrimitiveShape(first.shape)) return false;
        const CppInfo second =
            t.parts.size() >= 2 ? classify(t.parts[1]) : CppInfo{};
        if (!second.representable || !comparableShape(second.shape)) {
            return false;
        }
        cpp = "::cp3::runtime::collections::" +
              std::string(isMap ? "Map<" : "LRUCache<") + first.cpp + ", " +
              second.cpp + ">";
    } else {
        if (!comparableShape(first.shape)) return false;
        cpp = "::cp3::runtime::collections::Set<" + first.cpp + ">";
    }
    info.shape = Shape::Collections;
    info.representable = true;
    info.cpp = std::move(cpp);
    return true;
}

}  // namespace detail

using namespace detail;

namespace {

// The canonical label of a `Map` entry tuple, mirrored from
// src/std/collections/sema.cpp.
constexpr const char* kMapEntryLabel = "$mapEntry";

bool isMapType(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "Map";
}

bool isSetType(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "Set";
}

bool isLruType(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "LRUCache";
}

// The sole callback argument of `forEach` when it is an arrow (the only form
// with parameter types to thread through `emitArrow`).
const ArrowFunction* arrowCallback(const CallExpr& c) {
    if (c.arguments.size() != 1 || !c.arguments.front()) return nullptr;
    const Expr* arg = c.arguments.front().get();
    if (arg->kind != NodeKind::ArrowFunction) return nullptr;
    return static_cast<const ArrowFunction*>(arg);
}

// The diagnostic for a `Map`/`Set` type the runtime cannot hold: the key is
// hashed (a primitive only), the value (and a Set's element) is a value type
// the runtime can compare.
std::string collectionsTypeMessage(const Type& t) {
    const bool isMap = isMapType(t);
    const std::string kind = isMap ? "Map" : "Set";
    const std::string needsArgs =
        "type mismatch: '" + kind +
        "' needs its type arguments (e.g. `new Map<string, number>()`)";
    if (t.parts.empty()) return needsArgs;
    const CppInfo first = classify(t.parts.front());
    if (!first.representable) {
        // An unknown type argument is a missing one.
        if (t.parts.front().isUnknown()) return needsArgs;
        return "type mismatch: the " + std::string(isMap ? "key" : "element") +
               " type of a " + kind + " is not known";
    }
    if (isMap) {
        if (!isPrimitiveShape(first.shape)) {
            return "type mismatch: a Map's key must be a number, a string or "
                   "a boolean (the runtime hashes it)";
        }
        const CppInfo second = classify(t.parts[1]);
        if (!second.representable) {
            if (t.parts[1].isUnknown()) return needsArgs;
            return "type mismatch: the value type of a Map is not known";
        }
    }
    return "type mismatch: a " +
           std::string(isMap ? "Map's value" : "Set's element") +
           " must be a number, a string, a boolean, an object, a class instance "
           "or an array";
}

}  // namespace

// The `forEach` callback. The runtime calls the callback with the value (and,
// for a `Map`, the key), so the callback's own signature has to fit: JS allows
// a callback with fewer parameters, and only the ones it declares are passed.
// An inline arrow takes the collection's own types as its parameters; any
// other callback (a named function, a function-valued binding) is adapted, and
// a declared parameter whose type does not match the value the runtime passes
// is a type error. `expected` is the collection's parameter list, in call
// order (`(value, key)` for a `Map`, `(value)` for a `Set`).
std::string Generator::collectionsForEachCallback(
    const CallExpr& c, const std::vector<std::string>& expected) {
    if (c.arguments.size() != 1 || !c.arguments.front()) {
        reportCodegenError(c, diagcode::kArityMismatch,
                           "arity mismatch: forEach takes exactly one callback");
        return std::string();
    }
    const Expr& arg = *c.arguments.front();
    const ArrowFunction* arrow = arrowCallback(c);
    const Type fnType = effectiveType(arg);
    if (arrow == nullptr && fnType.kind != TypeKind::Function) {
        reportTypeError(arg, "type mismatch: the forEach callback must be a function");
        return std::string();
    }
    // A function type's parts are the parameters, then the return type.
    const std::size_t arity =
        fnType.kind == TypeKind::Function && !fnType.parts.empty()
            ? fnType.parts.size() - 1
            : (arrow != nullptr ? arrow->params.size() : 0);
    if (arity > expected.size()) {
        reportCodegenError(c, diagcode::kArityMismatch,
                           "arity mismatch: the forEach callback takes at most " +
                               std::to_string(expected.size()) +
                               " argument(s), got " + std::to_string(arity));
        return std::string();
    }
    for (std::size_t i = 0;
         i < arity && fnType.kind == TypeKind::Function; ++i) {
        const CppInfo actual = classify(fnType.parts[i]);
        // An untyped parameter fits any value, so only a typed one is checked.
        if (!actual.representable) continue;
        if (actual.cpp != expected[i]) {
            reportTypeError(arg,
                            "type mismatch: the forEach callback's parameter " +
                                std::to_string(i + 1) +
                                " does not match the value the collection passes");
            return std::string();
        }
    }
    const std::string callback =
        arrow != nullptr ? emitArrow(*arrow, expected) : expr(arg);
    if (arrow != nullptr && arity == expected.size()) {
        // The arrow's own parameters are the collection's: the runtime can
        // call it directly.
        return callback;
    }
    // The adapter: the collection's parameter types, and only the parameters
    // the callback declares. The callback value is evaluated once (an
    // init-capture), exactly like the JS argument.
    std::string params;
    std::string call;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (i != 0) params += ", ";
        params += expected[i] + " cp3_value" + std::to_string(i);
    }
    for (std::size_t i = 0; i < arity; ++i) {
        if (i != 0) call += ", ";
        call += "cp3_value" + std::to_string(i);
    }
    return "[cp3_cb = (" + callback + ")](" + params + ") { cp3_cb(" + call + "); }";
}

// `new Map<string, number>()` / `new Set<number>()`.
std::string Generator::emitCollectionsNew(const NewExpr& n) {
    const Type t = effectiveType(n);
    if (!isMapType(t) && !isSetType(t) && !isLruType(t)) return std::string();
    const CppInfo ci = classify(t);
    if (!ci.representable || t.parts.empty()) {
        reportTypeError(n, collectionsTypeMessage(t));
        return "0.0";
    }
    needsCollections_ = true;
    std::string elems = classify(t.parts.front()).cpp;
    if ((isMapType(t) || isLruType(t)) && t.parts.size() >= 2) {
        elems += ", " + classify(t.parts[1]).cpp;
    }
    if (isLruType(t)) {
        if (n.arguments.size() != 1 || !n.arguments.front()) {
            reportCodegenError(
                n, diagcode::kArityMismatch,
                "arity mismatch: LRUCache's constructor takes exactly one "
                "argument (capacity)");
            return "0.0";
        }
        return "::cp3::runtime::collections::makeLRU<" + elems + ">(" +
               expr(*n.arguments.front()) + ")";
    }
    return "::cp3::runtime::collections::" +
           std::string(isMapType(t) ? "makeMap" : "makeSet") + "<" + elems +
           ">()";
}

// The member-call dispatcher (`m.set(...)`, `s.add(...)`, the `forEach`
// callback forms).
std::string Generator::emitCollectionsMemberCall(const CallExpr& c,
                                                 const MemberExpr& m) {
    if (!m.object) return std::string();
    const Type recv = effectiveType(*m.object);
    const bool isMap = isMapType(recv);
    const bool isLru = isLruType(recv);
    if (!isMap && !isLru && !isSetType(recv)) return std::string();
    if (!classify(recv).representable) {
        reportTypeError(c, collectionsTypeMessage(recv));
        return "0.0";
    }
    needsCollections_ = true;
    const std::string obj = "(" + expr(*m.object) + ")";
    const std::string& prop = m.property;
    const auto args = [&]() {
        std::string out;
        for (const std::string& a : emitPlainArgs(c)) {
            if (!out.empty()) out += ", ";
            out += a;
        }
        return out;
    };
    if (isMap || isLru) {
        if (prop == "set") return obj + "->set(" + args() + ")";
        if (prop == "get") return obj + "->get(" + args() + ")";
        if (prop == "has") return obj + "->has(" + args() + ")";
        if (prop == "delete") return obj + "->remove(" + args() + ")";
        if (prop == "clear") return obj + "->clear()";
        if (prop == "keys") return obj + "->keys()";
        if (isLru) return std::string();
        if (prop == "values") return obj + "->values()";
        if (prop == "entries") return obj + "->entries()";
        if (prop == "forEach") {
            const CppInfo k = recv.parts.empty()
                                  ? CppInfo{}
                                  : classify(recv.parts.front());
            const CppInfo v = recv.parts.size() >= 2
                                  ? classify(recv.parts[1])
                                  : CppInfo{};
            const std::string cb = collectionsForEachCallback(c, {v.cpp, k.cpp});
            if (cb.empty()) return "0.0";
            return obj + "->forEach(" + cb + ")";
        }
        return std::string();
    }
    if (prop == "add") return obj + "->add(" + args() + ")";
    if (prop == "has") return obj + "->has(" + args() + ")";
    if (prop == "delete") return obj + "->remove(" + args() + ")";
    if (prop == "clear") return obj + "->clear()";
    if (prop == "values") return obj + "->values()";
    if (prop == "forEach") {
        const CppInfo t =
            recv.parts.empty() ? CppInfo{} : classify(recv.parts.front());
        const std::string cb = collectionsForEachCallback(c, {t.cpp});
        if (cb.empty()) return "0.0";
        return obj + "->forEach(" + cb + ")";
    }
    return std::string();
}

// Property reads: `m.size` / `s.size` and the `entries().length` case.
std::string Generator::emitCollectionsMember(const MemberExpr& m) {
    if (!m.object) return std::string();
    const Type recv = effectiveType(*m.object);
    if (isMapType(recv) || isSetType(recv) || isLruType(recv)) {
        if (m.property == "size") {
            needsCollections_ = true;
            return "static_cast<double>((" + expr(*m.object) + ")->size())";
        }
        if (isLruType(recv) && m.property == "capacity") {
            needsCollections_ = true;
            return "static_cast<double>((" + expr(*m.object) + ")->capacity())";
        }
        return std::string();
    }
    // `.length` of `m.entries()` (`[K, V][]`): the tuple element has no C++
    // spelling, but the runtime entries array still reports its size.
    if (recv.kind == TypeKind::Array && !recv.parts.empty() &&
        recv.parts.front().kind == TypeKind::Object &&
        recv.parts.front().name == kMapEntryLabel && m.property == "length") {
        needsCollections_ = true;
        return "static_cast<double>((" + expr(*m.object) + ")->size())";
    }
    return std::string();
}


}  // namespace cp3
