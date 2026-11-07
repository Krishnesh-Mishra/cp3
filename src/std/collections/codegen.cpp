// =============================================================================
// src/std/collections/codegen.cpp - `std/collections` `Map`/`Set` lowering,
// plus the folded-in `Queue`/`Stack`/`PriorityQueue`/`RingBuffer`/`BitSet`/
// `MultiMap`/`ConcurrentMap` (from the former queue/stack/priority-queue/
// ring-buffer/bitset/multimap/concurrent-map modules) onto cp3/std/collections.hpp, the single unified
// header.
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
#include "detail/generator/family.h"
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

// The shapes a `PriorityQueue` element can have: the heap's default
// comparator is `a < b`, so an element type with no `<` spelling (an object,
// a class instance, an array, ...) cannot be classified even though a custom
// comparator could order it structurally. (Folded in from the former priority-queue module.)
bool orderedShape(Shape s) {
    return s == Shape::Number || s == Shape::String || s == Shape::Bool ||
           s == Shape::Native;
}

}  // namespace

// The family's C++ mapping (see type_map.h): the whole `std/collections`
// family folds in here. `Map<K, V>` / `LRUCache<K, V>` / `MultiMap<K, V>` /
// `ConcurrentMap<K, V>` -> the runtime handle; `Set<T>` / `Queue<T>` /
// `Stack<T>` / `PriorityQueue<T>` / `RingBuffer<T>` -> their handles;
// `BitSet` carries no type arguments. A hashed key must be a primitive; a
// `PriorityQueue` element must be orderable by the default `<`; every other
// element/value must be a value type the runtime can hold and compare. The
// shape is `Shape::Collections` for every folded-in type (the handle is a
// `Ref<...>`, exactly like `Map`/`Set`).
bool classifyCollections(const Type& t, CppInfo& info) {
    if (t.kind != TypeKind::Class) return false;
    if (t.name == "BitSet") {
        info.shape = Shape::Collections;
        info.representable = true;
        info.cpp = "::cp3::runtime::collections::BitSet";
        return true;
    }
    const bool isMap = t.name == "Map";
    const bool isLru = t.name == "LRUCache";
    const bool isMultiMap = t.name == "MultiMap";
    const bool isConcurrentMap = t.name == "ConcurrentMap";
    const bool isPair =
        isMap || isLru || isMultiMap || isConcurrentMap;
    const bool isSet = t.name == "Set";
    const bool isQueue = t.name == "Queue";
    const bool isStack = t.name == "Stack";
    const bool isPq = t.name == "PriorityQueue";
    const bool isRing = t.name == "RingBuffer";
    if (!isPair && !isSet && !isQueue && !isStack && !isPq && !isRing) {
        return false;
    }
    if (t.parts.empty()) return false;
    const std::string ns = "::cp3::runtime::collections::";
    if (isPair) {
        const CppInfo first = classify(t.parts.front());
        if (!first.representable || !isPrimitiveShape(first.shape)) return false;
        const CppInfo second =
            t.parts.size() >= 2 ? classify(t.parts[1]) : CppInfo{};
        if (!second.representable || !comparableShape(second.shape)) {
            return false;
        }
        const char* handle = isMap ? "Map<" : (isLru ? "LRUCache<"
                                     : (isMultiMap ? "MultiMap<"
                                                   : "ConcurrentMap<"));
        info.shape = Shape::Collections;
        info.representable = true;
        info.cpp = ns + handle + first.cpp + ", " + second.cpp + ">";
        return true;
    }
    const CppInfo first = classify(t.parts.front());
    if (!first.representable) return false;
    if (isPq) {
        if (!orderedShape(first.shape)) return false;
        info.shape = Shape::Collections;
        info.representable = true;
        info.cpp = ns + "PriorityQueue<" + first.cpp + ">";
        return true;
    }
    if (isRing) {
        // The ring's storage is a contiguous vector, so any representable
        // element fits (no hashing/comparison is performed).
        info.shape = Shape::Collections;
        info.representable = true;
        info.cpp = ns + "RingBuffer<" + first.cpp + ">";
        return true;
    }
    if (!comparableShape(first.shape)) return false;
    info.shape = Shape::Collections;
    info.representable = true;
    const char* handle = isSet ? "Set<" : (isQueue ? "Queue<" : "Stack<");
    info.cpp = ns + handle + first.cpp + ">";
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

bool isQueueType(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "Queue";
}

bool isStackType(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "Stack";
}

bool isPriorityQueueType(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "PriorityQueue";
}

bool isRingBufferType(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "RingBuffer";
}

bool isBitSetType(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "BitSet";
}

bool isMultiMapType(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "MultiMap";
}

bool isConcurrentMapType(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "ConcurrentMap";
}

// The diagnostic for a folded-in type the runtime cannot hold.
std::string collectionsSingleTypeMessage(const std::string& kind,
                                         const Type& t) {
    std::string needsArgs;
    if (kind == "PriorityQueue") {
        needsArgs = "type mismatch: 'PriorityQueue' needs its type argument (e.g. "
                    "`new PriorityQueue<number>()`)";
    } else if (kind == "RingBuffer") {
        needsArgs = "type mismatch: 'RingBuffer' needs its type argument (e.g. "
                    "`new RingBuffer<number>(16)`)";
    } else {
        needsArgs = "type mismatch: '" + kind +
                    "' needs its type argument (e.g. `new " + kind +
                    "<number>()`)";
    }
    if (t.parts.empty()) return needsArgs;
    const CppInfo first = classify(t.parts.front());
    if (!first.representable) {
        // An unknown type argument is a missing one.
        if (t.parts.front().isUnknown()) return needsArgs;
        return "type mismatch: the element type of a " + kind + " is not known";
    }
    if (kind == "PriorityQueue") {
        return "type mismatch: a PriorityQueue's element must be a number, a "
               "string, a boolean or a native numeric value (the default "
               "comparator orders it with `<`)";
    }
    if (kind == "RingBuffer") {
        return "type mismatch: the element type of a RingBuffer is not known";
    }
    return "type mismatch: a " + kind +
           "'s element must be a number, a string, a boolean, an object, a "
           "class instance or an array";
}

// The sole callback argument of `forEach` when it is an arrow (the only form
// with parameter types to thread through `emitArrow`).
const ArrowFunction* arrowCallback(const CallExpr& c) {
    if (c.arguments.size() != 1 || !c.arguments.front()) return nullptr;
    const Expr* arg = c.arguments.front().get();
    if (arg->kind != NodeKind::ArrowFunction) return nullptr;
    return static_cast<const ArrowFunction*>(arg);
}

// The diagnostic for a `Map`/`Set`/`LRUCache` type the runtime cannot hold:
// the key is hashed (a primitive only), the value (and a Set's element) is a
// value type the runtime can compare.
std::string collectionsTypeMessage(const Type& t) {
    const bool isMap = isMapType(t);
    if (isMultiMapType(t) || isConcurrentMapType(t)) {
        const std::string kind =
            isMultiMapType(t) ? "MultiMap" : "ConcurrentMap";
        const std::string mmNeedsArgs =
            "type mismatch: '" + kind +
            "' needs its type arguments (e.g. `new " + kind +
            "<string, number>()`)";
        if (t.parts.empty()) return mmNeedsArgs;
        const CppInfo mmFirst = classify(t.parts.front());
        if (!mmFirst.representable) {
            if (t.parts.front().isUnknown()) return mmNeedsArgs;
            return "type mismatch: the key type of a " + kind + " is not known";
        }
        if (!isPrimitiveShape(mmFirst.shape)) {
            return "type mismatch: a " + kind +
                   "'s key must be a number, a string or a boolean (the "
                   "runtime hashes it)";
        }
        if (t.parts.size() < 2) return mmNeedsArgs;
        const CppInfo mmSecond = classify(t.parts[1]);
        if (!mmSecond.representable) {
            if (t.parts[1].isUnknown()) return mmNeedsArgs;
            return "type mismatch: the value type of a " + kind + " is not known";
        }
        if (!comparableShape(mmSecond.shape)) {
            return "type mismatch: a " + kind +
                   "'s value must be a number, a string, a boolean, an "
                   "object, a class instance or an array";
        }
        return mmNeedsArgs;
    }
    const bool isLru = isLruType(t);
    const std::string kind =
        isMap ? "Map" : (isLru ? "LRUCache" : "Set");
    const std::string needsArgs =
        "type mismatch: '" + kind +
        "' needs its type arguments (e.g. `new " +
        (isLru ? "LRUCache<string, number>" : "Map<string, number>") + "()`)";
    if (t.parts.empty()) return needsArgs;
    const CppInfo first = classify(t.parts.front());
    if (!first.representable) {
        // An unknown type argument is a missing one.
        if (t.parts.front().isUnknown()) return needsArgs;
        return "type mismatch: the " + std::string((isMap || isLru) ? "key" : "element") +
               " type of a " + kind + " is not known";
    }
    if (isMap || isLru) {
        if (!isPrimitiveShape(first.shape)) {
            return "type mismatch: a " + kind + "'s key must be a number, a string or "
                   "a boolean (the runtime hashes it)";
        }
        if (t.parts.size() < 2) return needsArgs;
        const CppInfo second = classify(t.parts[1]);
        if (!second.representable) {
            if (t.parts[1].isUnknown()) return needsArgs;
            return "type mismatch: the value type of a " + kind + " is not known";
        }
        if (!comparableShape(second.shape)) {
            return "type mismatch: a " + kind +
                   "'s value must be a number, a string, a boolean, an object, a class instance "
                   "or an array";
        }
        return std::string();
    }
    return "type mismatch: a " +
           std::string("Set's element") +
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

// `new Map<string, number>()` / `new Set<number>()` / `new Queue<number>()` /
// `new Stack<number>()` / `new PriorityQueue<number>(cmp?)` /
// `new RingBuffer<number>(16)` / `new BitSet(size?)` /
// `new MultiMap<string, number>()` / `new ConcurrentMap<string, number>()`.
// The folded-in families' lowering was merged from the former queue/stack,
// priority-queue, ring-buffer, bitset, multimap and concurrent-map modules;
// `needsCollections_` is the only include flag (the unified header). The `PriorityQueue` comparator, when present, is an
// arrow or a function-typed value validated inline (it cannot be a separate
// `Generator` method without a declaration in generator.h).
std::string Generator::emitCollectionsNew(const NewExpr& n) {
    const Type t = effectiveType(n);
    const bool isPairNew =
        isMapType(t) || isLruType(t) || isMultiMapType(t) ||
        isConcurrentMapType(t);
    const bool isSingleNew = isSetType(t) || isQueueType(t) ||
                             isStackType(t) || isPriorityQueueType(t) ||
                             isRingBufferType(t);
    if (!isPairNew && !isSingleNew && !isBitSetType(t)) {
        return std::string();
    }
    if (isBitSetType(t)) {
        if (n.arguments.size() > 1) {
            reportCodegenError(n, diagcode::kArityMismatch,
                               "arity mismatch: BitSet's constructor takes 0 or "
                               "1 argument (optional size in bits), got " +
                                   std::to_string(n.arguments.size()));
            return "0.0";
        }
        needsCollections_ = true;
        if (n.arguments.empty() || !n.arguments.front()) {
            return "::cp3::runtime::collections::makeBitSet()";
        }
        return std::string("::cp3::runtime::collections::makeBitSet(") +
               "static_cast<std::uint64_t>(" +
               expr(*n.arguments.front()) + "))";
    }
    const CppInfo ci = classify(t);
    if (!ci.representable || t.parts.empty()) {
        if (isSetType(t) || isMapType(t) || isLruType(t) ||
            isMultiMapType(t) || isConcurrentMapType(t)) {
            reportTypeError(n, collectionsTypeMessage(t));
        } else {
            reportTypeError(n, collectionsSingleTypeMessage(t.name, t));
        }
        return "0.0";
    }
    needsCollections_ = true;
    std::string elems = classify(t.parts.front()).cpp;
    if (isPairNew && t.parts.size() >= 2) {
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
        return "::cp3::runtime::collections::makeLRU<" + elems + ">(static_cast<std::uint64_t>(" +
               expr(*n.arguments.front()) + "))";
    }
    if (isRingBufferType(t)) {
        if (n.arguments.size() != 1 || !n.arguments.front()) {
            reportCodegenError(
                n, diagcode::kArityMismatch,
                "arity mismatch: RingBuffer's constructor takes exactly one "
                "argument (capacity)");
            return "0.0";
        }
        return "::cp3::runtime::collections::makeRingBuffer<" + elems +
               ">(static_cast<std::uint64_t>(" +
               expr(*n.arguments.front()) + "))";
    }
    if (isPriorityQueueType(t)) {
        if (n.arguments.size() > 1) {
            reportCodegenError(n, diagcode::kArityMismatch,
                               "arity mismatch: PriorityQueue's constructor "
                               "takes at most one argument (comparator)");
            return "0.0";
        }
        if (n.arguments.empty() || !n.arguments.front()) {
            return "::cp3::runtime::collections::makePriorityQueue<" + elems + ">()";
        }
        // The constructor comparator (folded in from the former priority-queue module): an
        // inline arrow takes the element type for both parameters; any other
        // callable (a named function, a function-valued binding) is emitted
        // as-is and its declared parameter types must match the element the
        // heap compares.
        const std::string elemCpp = classify(t.parts.front()).cpp;
        const Expr& arg = *n.arguments.front();
        std::string cmp;
        if (arg.kind == NodeKind::ArrowFunction) {
            cmp = emitArrow(static_cast<const ArrowFunction&>(arg),
                            {elemCpp, elemCpp});
        } else {
            const Type fnType = effectiveType(arg);
            if (fnType.kind == TypeKind::Function) {
                const std::size_t arity =
                    fnType.parts.empty() ? 0 : fnType.parts.size() - 1;
                if (arity > 2) {
                    reportCodegenError(
                        n, diagcode::kArityMismatch,
                        "arity mismatch: the PriorityQueue comparator takes "
                        "at most 2 arguments (a, b), got " +
                            std::to_string(arity));
                    return "0.0";
                }
                for (std::size_t i = 0; i < arity; ++i) {
                    const CppInfo actual = classify(fnType.parts[i]);
                    if (!actual.representable) continue;
                    if (actual.cpp != elemCpp) {
                        reportTypeError(arg,
                                        "type mismatch: the PriorityQueue "
                                        "comparator's parameter " +
                                            std::to_string(i + 1) +
                                            " does not match the queue's "
                                            "element type");
                        return "0.0";
                    }
                }
                if (!fnType.parts.empty()) {
                    const Type ret = fnType.parts.back();
                    if (ret.kind != TypeKind::Boolean) {
                        const CppInfo rc = classify(ret);
                        if (rc.representable && rc.cpp != "bool") {
                            reportTypeError(arg,
                                            "type mismatch: the "
                                            "PriorityQueue comparator must "
                                            "return boolean (true when its "
                                            "first argument outranks its "
                                            "second)");
                            return "0.0";
                        }
                    }
                }
            } else if (!fnType.isUnknown() &&
                       fnType.kind != TypeKind::Unknown) {
                reportTypeError(arg,
                                "type mismatch: the PriorityQueue comparator "
                                "must be a function (a, b) => boolean");
                return "0.0";
            }
            cmp = expr(arg);
        }
        return "::cp3::runtime::collections::makePriorityQueue<" + elemCpp +
               ">(" + cmp + ")";
    }
    if (isQueueType(t) || isStackType(t) || isMultiMapType(t) ||
        isConcurrentMapType(t)) {
        if (!n.arguments.empty()) {
            const std::string kind =
                isQueueType(t)     ? "Queue"
                : isStackType(t)   ? "Stack"
                : isMultiMapType(t) ? "MultiMap"
                                    : "ConcurrentMap";
            reportCodegenError(n, diagcode::kArityMismatch,
                               "arity mismatch: " + kind +
                                   "'s constructor takes no arguments, got " +
                                   std::to_string(n.arguments.size()));
            return "0.0";
        }
        const std::string make =
            isQueueType(t) ? "makeQueue"
            : isStackType(t) ? "makeStack"
            : isMultiMapType(t) ? "makeMultiMap"
                                : "makeConcurrentMap";
        return "::cp3::runtime::collections::" + make + "<" + elems + ">()";
    }
    if (!n.arguments.empty() && n.arguments.front()) {
        // The from-iterable form: `new Set<T>(elements)` / `new
        // Map<K, V>(entries)`. `std::get` reads a pair (the runtime's
        // `entries()` spelling) and a tuple (an array-literal of entry
        // pairs) alike, so one loop covers both sources.
        needsCollections_ = true;
        needsTuple_ = true;
        const std::string arg = expr(*n.arguments.front());
        if (isMapType(t)) {
            return "([&]{ auto cp3_m = ::cp3::runtime::collections::makeMap<" +
                   elems + ">(); const auto cp3_entries = (" + arg +
                   "); for (const auto& cp3_e : *cp3_entries) " +
                   "cp3_m->set(std::get<0>(cp3_e), std::get<1>(cp3_e)); "
                   "return cp3_m; }())";
        }
        return "::cp3::runtime::collections::makeSetFromArray<" +
               classify(t.parts.front()).cpp + ">(" + arg + ")";
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
    // The `Map.groupBy(list, callback)` static: the receiver is the class
    // name itself, so it is dispatched before the instance surface.
    if (m.property == "groupBy" && m.object->kind == NodeKind::Identifier &&
        static_cast<Identifier&>(*m.object).name == "Map" &&
        !lookup("Map")) {
        needsCollections_ = true;
        const Type ret = effectiveType(c);
        if (!isMapType(ret) || ret.parts.size() < 2) {
            reportTypeError(c, "type mismatch: Map.groupBy needs an array and "
                               "a key callback");
            return "0.0";
        }
        const Type itemsType = ret.parts[1].kind == TypeKind::Array
                                   ? ret.parts[1]
                                   : Type::unknown();
        const Type elem = arrayElemOf(itemsType);
        const CppInfo ec = classify(elem);
        const CppInfo kc =
            ret.parts.empty() ? CppInfo{} : classify(ret.parts.front());
        if (!ec.representable || !kc.representable ||
            !isPrimitiveShape(kc.shape) ||
            c.arguments.size() != 2 || !c.arguments.front() ||
            !c.arguments[1]) {
            reportTypeError(c, "type mismatch: Map.groupBy needs an array and "
                               "a key callback");
            return "0.0";
        }
        const Expr& items = *c.arguments.front();
        const Expr& cb = *c.arguments[1];
        // The JS callback context is (item, index, array), matching sema's
        // context {elem, number, elem[]}. The runtime now passes all three
        // (see groupBy in collections.hpp); previously only two were passed
        // and only two were typed here, so a callback declaring the array
        // parameter lowered to a generic lambda parameter with no argument.
        const std::string arrayCpp = "::cp3::runtime::Array<" + ec.cpp + ">";
        std::string callback;
        if (cb.kind == NodeKind::ArrowFunction) {
            callback = emitArrow(static_cast<const ArrowFunction&>(cb),
                                 {ec.cpp, "double", arrayCpp});
        } else {
            const Type fnType = effectiveType(cb);
            if (fnType.kind == TypeKind::Function && !fnType.parts.empty()) {
                const std::size_t arity =
                    fnType.parts.size() > 1 ? fnType.parts.size() - 1 : 0;
                if (arity > 3) {
                    reportTypeError(
                        cb, "type mismatch: the Map.groupBy callback takes at most "
                            "3 argument(s), got " +
                                std::to_string(arity));
                    return "0.0";
                }
                // Adapt a named function / function-typed binding (which may
                // declare any prefix of (item, index, array), e.g.
                // `(string) => string`) to the runtime's 3-argument call, as
                // collectionsForEachCallback does for forEach.
                const std::string fnText = expr(cb);
                std::string call;
                for (std::size_t i = 0; i < arity; ++i) {
                    if (i != 0) call += ", ";
                    call += "cp3_value" + std::to_string(i);
                }
                callback = "[cp3_cb = (" + fnText + ")](" + ec.cpp +
                           " cp3_value0, double cp3_value1, " + arrayCpp +
                           " cp3_value2) -> " + kc.cpp + " { return cp3_cb(" +
                           call + "); }";
            } else {
                callback = expr(cb);
            }
        }
        return "::cp3::runtime::collections::groupBy<" + kc.cpp + ", " +
               ec.cpp + ">(" + expr(items) + ", " + callback + ")";
    }
    const Type recv = effectiveType(*m.object);
    const bool isMap = isMapType(recv);
    const bool isLru = isLruType(recv);
    const bool isPairCall =
        isMultiMapType(recv) || isConcurrentMapType(recv);
    const bool isSingleCall =
        isQueueType(recv) || isStackType(recv) || isPriorityQueueType(recv) ||
        isRingBufferType(recv) || isBitSetType(recv);
    if (!isMap && !isLru && !isPairCall && !isSingleCall &&
        !isSetType(recv)) {
        return std::string();
    }
    if (!classify(recv).representable) {
        if (isQueueType(recv) || isStackType(recv) ||
            isPriorityQueueType(recv) || isRingBufferType(recv)) {
            reportTypeError(c, collectionsSingleTypeMessage(recv.name, recv));
        } else {
            reportTypeError(c, collectionsTypeMessage(recv));
        }
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
    if (isPairCall) {
        // `MultiMap` (add/get/has/delete/clear) and `ConcurrentMap`
        // (set/get/has/delete): the `delete` -> `remove` keyword rename
        // matches Map/Set.
        if (prop == "add") return obj + "->add(" + args() + ")";
        if (prop == "set") return obj + "->set(" + args() + ")";
        if (prop == "get") return obj + "->get(" + args() + ")";
        if (prop == "has") return obj + "->has(" + args() + ")";
        if (prop == "delete") return obj + "->remove(" + args() + ")";
        if (prop == "clear") return obj + "->clear()";
        return std::string();
    }
    if (isSingleCall) {
        if (isBitSetType(recv)) {
            // The index narrows through the exact `uint64_t` width; a
            // TypeScript `number` argument narrows here, exactly like the
            // LRUCache capacity. `and`/`or`/`xor` lower to
            // `andSet`/`orSet`/`xorSet` (C++ alternative tokens cannot name
            // a member), matching `union` -> `unionSet` for Set.
            const auto narrow = [](const std::string& arg) {
                return "static_cast<std::uint64_t>(" + arg + ")";
            };
            if (prop == "set" || prop == "get" || prop == "clear") {
                if (c.arguments.size() != 1 || !c.arguments.front()) {
                    reportCodegenError(c, diagcode::kArityMismatch,
                                       "arity mismatch: " + prop +
                                           " takes exactly one argument (the "
                                           "bit index)");
                    return "0.0";
                }
                return obj + "->" + prop + "(" +
                       narrow(expr(*c.arguments.front())) + ")";
            }
            if (prop == "count") {
                if (!c.arguments.empty()) {
                    reportCodegenError(c, diagcode::kArityMismatch,
                                       "arity mismatch: count takes no "
                                       "arguments, got " +
                                           std::to_string(c.arguments.size()));
                    return "0.0";
                }
                const std::string call = obj + "->count()";
                if (options_.typeSystem == TypeSystem::TypeScript) {
                    return "static_cast<double>(" + call + ")";
                }
                return call;
            }
            const char* runtimeName = nullptr;
            if (prop == "and") runtimeName = "andSet";
            else if (prop == "or") runtimeName = "orSet";
            else if (prop == "xor") runtimeName = "xorSet";
            if (runtimeName != nullptr) {
                if (c.arguments.size() != 1 || !c.arguments.front()) {
                    reportCodegenError(c, diagcode::kArityMismatch,
                                       "arity mismatch: " + prop +
                                           " takes exactly one argument (the "
                                           "other set)");
                    return "0.0";
                }
                return obj + "->" + runtimeName + "(" + args() + ")";
            }
            return std::string();
        }
        // `Queue` (enqueue/dequeue/peek/clear/isEmpty), `Stack`
        // (push/pop/peek/clear/isEmpty), `PriorityQueue`
        // (push/pop/peek), `RingBuffer` (push/pop/peek/clear) - the merged
        // dispatch from the former queue/stack/priority-queue/ring-buffer
        // modules, with the single `needsCollections_` flag.
        if (prop == "enqueue") return obj + "->enqueue(" + args() + ")";
        if (prop == "push") return obj + "->push(" + args() + ")";
        if (prop == "dequeue") return obj + "->dequeue()";
        if (prop == "pop") return obj + "->pop()";
        if (prop == "peek") return obj + "->peek()";
        if (prop == "clear") return obj + "->clear()";
        if (prop == "isEmpty") return obj + "->isEmpty()";
        return std::string();
    }
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
    if (prop == "union") return obj + "->unionSet(" + args() + ")";
    if (prop == "intersection") return obj + "->intersectionSet(" + args() + ")";
    if (prop == "difference") return obj + "->differenceSet(" + args() + ")";
    if (prop == "isSubsetOf") return obj + "->isSubsetOf(" + args() + ")";
    if (prop == "forEach") {
        const CppInfo t =
            recv.parts.empty() ? CppInfo{} : classify(recv.parts.front());
        const std::string cb = collectionsForEachCallback(c, {t.cpp});
        if (cb.empty()) return "0.0";
        return obj + "->forEach(" + cb + ")";
    }
    return std::string();
}

// Property reads: `m.size` / `s.size` / `q.size` / `rb.size` (+
// `rb.capacity` / `rb.isFull`, `lru.capacity`) and the `entries().length`
// case.
std::string Generator::emitCollectionsMember(const MemberExpr& m) {
    if (!m.object) return std::string();
    const Type recv = effectiveType(*m.object);
    if (isMapType(recv) || isSetType(recv) || isLruType(recv) ||
        isMultiMapType(recv) || isConcurrentMapType(recv) ||
        isQueueType(recv) || isStackType(recv) ||
        isRingBufferType(recv)) {
        // `.size` / `.capacity` are the std count width (`usize` in a rust
        // project); a TypeScript project keeps `number`, so cast there.
        const auto read = [this](const std::string& self,
                                 const char* name) -> std::string {
            const std::string call = "(" + self + ")->" + name + "()";
            if (options_.typeSystem == TypeSystem::TypeScript) {
                return "static_cast<double>(" + call + ")";
            }
            return call;
        };
        if (m.property == "size") {
            needsCollections_ = true;
            return read(expr(*m.object), "size");
        }
        if (isLruType(recv) && m.property == "capacity") {
            needsCollections_ = true;
            return read(expr(*m.object), "capacity");
        }
        if (isRingBufferType(recv) && m.property == "capacity") {
            needsCollections_ = true;
            return read(expr(*m.object), "capacity");
        }
        if (isRingBufferType(recv) && m.property == "isFull") {
            needsCollections_ = true;
            return "(" + expr(*m.object) + ")->isFull()";
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
