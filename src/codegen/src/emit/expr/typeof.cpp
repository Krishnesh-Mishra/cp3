// =============================================================================
// src/codegen/src/emit/typeof.cpp - `typeof` lowering (STYLE.md §8).
//
// A `typeof` over a primitive value is a compile-time string; a primitive
// union reports the held alternative, and a nullish option inspects its slot
// (the empty flavor is "object" for null, "undefined" otherwise). Native
// integers and floats report "number" (D65).
//
// D16 fixes the answer for every value: `typeof <expr>` is allowed on any
// expression and yields one of "number" | "string" | "boolean" | "undefined" |
// "object" | "function". A reference-like value (an object shape, an array, a
// typed view, a class instance, a Map/Set, a promise) is "object"; a callable
// is "function"; an enum answers by its backing kind (D19). A value whose type
// is not statically known (an unknown value, a type parameter) reports a type
// error at the use site instead of silently answering "undefined".
// =============================================================================
#include "detail/generator/generator.h"

#include <string>

#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// The D16 answer of a value shape ("" when the shape has no static answer:
// an unknown value, a type parameter, a value union).
std::string typeofShape(const CppInfo& ci) {
    switch (ci.shape) {
        case Shape::Number:
        case Shape::Native: return "number";
        case Shape::String: return "string";
        case Shape::Bool: return "boolean";
        case Shape::Undefined:
        case Shape::Void: return "undefined";
        case Shape::Null: return "object";
        // The reference-like shapes: an object shape, a regular array, a typed
        // view, a buffer, a DataView, a Map/Set, a promise, an EventEmitter
        // and a user class instance.
        case Shape::Object:
        case Shape::Array:
        case Shape::TypedArray:
        case Shape::ArrayBuffer:
        case Shape::DataView:
        case Shape::Collections:
        case Shape::Buffer:
        case Shape::Promise:
        case Shape::EventEmitter:
        case Shape::ClassRef: return "object";
        case Shape::Function:
        case Shape::Listener: return "function";
        default: return std::string();
    }
}

}  // namespace

// The answer of a nullish option: the empty slot is `null` ("object") or
// `undefined`, and the held value answers by its own type. A primitive is
// asked at runtime (a union inside the option may hold different
// alternatives); a reference-like value answers by its static type.
std::string Generator::emitTypeofOption(const UnaryExpr& u, const CppInfo& ci) {
    Type inner;
    bool found = false;
    for (const Type& part : ci.type.parts) {
        if (part.isNullish()) continue;
        inner = part;
        found = true;
        break;
    }
    const CppInfo innerInfo = found ? classify(inner) : CppInfo{};
    std::string held;
    if (found && (isPrimitiveShape(innerInfo.shape) ||
                  innerInfo.shape == Shape::Union)) {
        held = innerInfo.shape == Shape::Native
                   ? "std::string(\"number\")"
                   : (ci.shape == Shape::Maybe
                          ? "::cp3::runtime::typeOf(cp3_v.value())"
                          : "::cp3::runtime::typeOf(*cp3_v)");
    } else {
        const std::string answer = found ? typeofShape(innerInfo) : std::string();
        if (answer.empty()) {
            reportTypeError(u, "typeof of this value has no static answer");
            return "std::string(\"undefined\")";
        }
        held = "std::string(\"" + answer + "\")";
    }
    if (ci.shape == Shape::Maybe) {
        return "([&]{ const auto& cp3_v = (" + expr(*u.operand) +
               "); if (cp3_v.isNull()) return std::string(\"object\"); "
               "if (cp3_v.isUndefined()) return "
               "std::string(\"undefined\"); return " +
               held + "; }())";
    }
    const char* empty =
        (ci.emptyKind == NullishKind::Null) ? "object" : "undefined";
    return "([&]{ const auto& cp3_v = (" + expr(*u.operand) +
           "); return cp3_v.has_value() ? " + held + " : std::string(\"" + empty +
           "\"); }())";
}

std::string Generator::emitTypeof(const UnaryExpr& u) {
    const CppInfo ci = info(*u.operand);
    // A regex value is a reference (object) type: `typeof r === "object"`
    // (D16; the shape itself is Unknown, so the switch cannot decide it).
    if (ci.type.kind == TypeKind::Class && ci.type.name == "RegExp") {
        return "std::string(\"object\")";
    }
    switch (ci.shape) {
        case Shape::Number: return "std::string(\"number\")";
        case Shape::Native: return "std::string(\"number\")";
        case Shape::String: return "std::string(\"string\")";
        case Shape::Bool: return "std::string(\"boolean\")";
        case Shape::Undefined: return "std::string(\"undefined\")";
        case Shape::Null: return "std::string(\"object\")";
        case Shape::Object: return "std::string(\"object\")";
        case Shape::Enum: {
            // D19: a number-backed enum member is a number, a string-backed
            // one a string. The enum's own model says which backing it has.
            const auto found = enums_.find(ci.type.name);
            const bool isString =
                found != enums_.end() && found->second.stringBacked;
            return std::string("std::string(\"") +
                   (isString ? "string" : "number") + "\")";
        }
        case Shape::Union:
            // A primitive union reports the held alternative.
            return "::cp3::runtime::unions::typeOf(" + expr(*u.operand) + ")";
        case Shape::Optional:
        case Shape::Maybe: return emitTypeofOption(u, ci);
        default: {
            const std::string answer = typeofShape(ci);
            if (!answer.empty()) return "std::string(\"" + answer + "\")";
            reportTypeError(u, "typeof of this value has no static answer");
            return "std::string(\"undefined\")";
        }
    }
}

}  // namespace cp3
