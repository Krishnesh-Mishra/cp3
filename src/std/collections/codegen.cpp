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
// (src/std/collections/sema.cpp). Forms this unit cannot lower report CP3002
// through `notImplemented` and return a compilable stub, so they stay a
// graceful skip.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "detail/cpp_writer.h"
#include "detail/type_map.h"

namespace cp3 {

namespace detail {

// The family's C++ mapping (see type_map.h): `Map<K, V>` -> the runtime's
// insertion-ordered `Map<K, V>` handle, `Set<T>` likewise. Keys and values
// must have an M1 spelling (primitives); every other type is left for
// `classify`'s own arms (a CP3002 skip at the use site).
bool classifyCollections(const Type& t, CppInfo& info) {
    if (t.kind != TypeKind::Class) return false;
    const bool isMap = t.name == "Map";
    if (!isMap && t.name != "Set") return false;
    const CppInfo first = t.parts.empty() ? CppInfo{} : classify(t.parts.front());
    if (!first.representable || !isPrimitiveShape(first.shape)) return false;
    std::string cpp;
    if (isMap) {
        const CppInfo second =
            t.parts.size() >= 2 ? classify(t.parts[1]) : CppInfo{};
        if (!second.representable || !isPrimitiveShape(second.shape)) {
            return false;
        }
        cpp = "::cp3::runtime::collections::Map<" + first.cpp + ", " +
              second.cpp + ">";
    } else {
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

// The sole callback argument of `forEach` when it is an arrow (the only form
// with parameter types to thread through `emitArrow`).
const ArrowFunction* arrowCallback(const CallExpr& c) {
    if (c.arguments.size() != 1 || !c.arguments.front()) return nullptr;
    const Expr* arg = c.arguments.front().get();
    if (arg->kind != NodeKind::ArrowFunction) return nullptr;
    return static_cast<const ArrowFunction*>(arg);
}

}  // namespace

// `new Map<string, number>()` / `new Set<number>()`.
std::string Generator::emitCollectionsNew(const NewExpr& n) {
    const Type t = effectiveType(n);
    if (!isMapType(t) && !isSetType(t)) return std::string();
    const CppInfo ci = classify(t);
    if (!ci.representable || t.parts.empty()) {
        notImplemented(n, "this Map/Set type");
        return "0.0";
    }
    needsCollections_ = true;
    std::string elems = classify(t.parts.front()).cpp;
    if (isMapType(t) && t.parts.size() >= 2) {
        elems += ", " + classify(t.parts[1]).cpp;
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
    if (!isMap && !isSetType(recv)) return std::string();
    if (!classify(recv).representable) {
        notImplemented(c, "this Map/Set receiver");
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
    if (isMap) {
        if (prop == "set") return obj + "->set(" + args() + ")";
        if (prop == "get") return obj + "->get(" + args() + ")";
        if (prop == "has") return obj + "->has(" + args() + ")";
        if (prop == "delete") return obj + "->remove(" + args() + ")";
        if (prop == "clear") return obj + "->clear()";
        if (prop == "keys") return obj + "->keys()";
        if (prop == "values") return obj + "->values()";
        if (prop == "entries") return obj + "->entries()";
        if (prop == "forEach") {
            const ArrowFunction* cb = arrowCallback(c);
            if (cb == nullptr) {
                notImplemented(c, "a non-arrow Map forEach callback");
                return "0.0";
            }
            const CppInfo k = recv.parts.empty()
                                  ? CppInfo{}
                                  : classify(recv.parts.front());
            const CppInfo v = recv.parts.size() >= 2
                                  ? classify(recv.parts[1])
                                  : CppInfo{};
            return obj + "->forEach(" + emitArrow(*cb, {v.cpp, k.cpp}) + ")";
        }
        return std::string();
    }
    if (prop == "add") return obj + "->add(" + args() + ")";
    if (prop == "has") return obj + "->has(" + args() + ")";
    if (prop == "delete") return obj + "->remove(" + args() + ")";
    if (prop == "clear") return obj + "->clear()";
    if (prop == "values") return obj + "->values()";
    if (prop == "forEach") {
        const ArrowFunction* cb = arrowCallback(c);
        if (cb == nullptr) {
            notImplemented(c, "a non-arrow Set forEach callback");
            return "0.0";
        }
        const CppInfo t =
            recv.parts.empty() ? CppInfo{} : classify(recv.parts.front());
        return obj + "->forEach(" + emitArrow(*cb, {t.cpp}) + ")";
    }
    return std::string();
}

// Property reads: `m.size` / `s.size` and the `entries().length` case.
std::string Generator::emitCollectionsMember(const MemberExpr& m) {
    if (!m.object) return std::string();
    const Type recv = effectiveType(*m.object);
    if (isMapType(recv) || isSetType(recv)) {
        if (m.property != "size") return std::string();
        needsCollections_ = true;
        return "static_cast<double>((" + expr(*m.object) + ")->size())";
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
