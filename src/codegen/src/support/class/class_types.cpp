// =============================================================================
// src/codegen/src/support/class_types.cpp - class / interface / annotation
// type lowering shared by the emit units (part of the type_map contract).
//
// The class and structural-model arms of `classify` and the annotation ->
// cp3 `Type` mapper live here so type_map.cpp stays a focused value-shape
// unit. Class handles are `Ref<T>`; interfaces use their generated wrapper.
// =============================================================================
#include "detail/generator/generator.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "detail/class/class_model.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

namespace detail {

// The Class-kind tail of `classify`: the family spellings (Buffer, typed
// views, Map/Set, Date), then the builtin EventEmitter (D74), then `Promise<T>`
// (the only other class with a spelling). Anything else stays out of M1.
CppInfo classifyClassValue(const Type& t) {
    CppInfo info;
    info.type = t;
    // The nominal std/buffer `Buffer` (src/std/buffer/codegen.cpp).
    if (classifyByteBuffer(t, info)) return info;
    // The JS-compat family (typed views, buffers, DataView).
    if (classifyFamily(t, info)) return info;
    // The std/collections family (`Map<K, V>` / `Set<T>`).
    if (classifyCollections(t, info)) return info;
    // The `std/date` value type (src/std/date/codegen.cpp).
    if (classifyDate(t, info)) return info;
    if (classifyStopwatch(t, info)) return info;
    if (classifyText(t, info)) return info;
    if (classifyUrl(t, info)) return info;

    if (classifyRegex(t, info)) return info;  // core regex (src/emit/regex.cpp)
    // The builtin EventEmitter (D74): a reference type, so its C++
    // spelling is `Ref<EventEmitter>` (cp3/async/emitter.hpp).
    if (t.name == "EventEmitter") {
        info.shape = Shape::EventEmitter;
        info.representable = true;
        info.cpp = "::cp3::runtime::Ref<::cp3::runtime::EventEmitter>";
        return info;
    }
    // A user class (spec 15): a `Ref<T>` handle over the generated struct. A
    // generic class spells its instantiation (`Box<double>`) from the Type
    // parts; all arguments must have a C++ spelling.
    if (const ClassModel* cm = ModelRegistry::get().classByName(t.name)) {
        if (!cm->typeParams.empty()) {
            if (t.parts.size() != cm->typeParams.size()) return info;
            std::string spelled = cm->cpp + "<";
            for (std::size_t i = 0; i < t.parts.size(); ++i) {
                const CppInfo arg = classify(t.parts[i]);
                if (!arg.representable) return info;
                if (i != 0) spelled += ", ";
                spelled += arg.cpp;
            }
            spelled += ">";
            info.shape = Shape::ClassRef;
            info.representable = true;
            info.cpp = "::cp3::runtime::Ref<" + spelled + ">";
            return info;
        }
        info.shape = Shape::ClassRef;
        info.representable = true;
        info.cpp = classRefCpp(*cm);
        return info;
    }
    // `Promise<T>` is the only other class type with a spelling (M1).
    if (isPromiseType(t)) return classifyPromise(t);
    return info;
}

// A structural object/interface model with a generated spelling (spec 15).
// Interfaces always use the wrapper; a plain object uses its model only when
// the all-primitive value-shape path cannot spell it (master's `$o{...}`
// value structs stay the lowering for pure-primitive literals).
CppInfo classifyObjectModel(const Type& t) {
    CppInfo info;
    ModelRegistry& reg = ModelRegistry::get();
    const ObjectModel* om = reg.objectByLabel(t.name);
    if (om == nullptr || !reg.objectUsable(t.name)) return info;
    if (!objectModelUsed(*om)) return info;
    info.shape = Shape::Object;
    info.representable = true;
    info.type = t;
    info.cpp = om->isInterface ? om->wrapper
                               : "::cp3::runtime::Ref<" + om->structName + ">";
    return info;
}

Type modelTypeFromTypeNode(const TypeNode& tn) {
    if (auto t = typeFromTypeNode(tn)) return *t;
    return Type::unknown();
}

}  // namespace detail
}  // namespace cp3
