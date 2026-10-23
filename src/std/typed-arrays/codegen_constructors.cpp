// =============================================================================
// src/std/typed-arrays/codegen_constructors.cpp - the JS-compat typed-array
// family's constructors (D69).
//
// The `new`-less and the `new` spellings of a family constructor lower the
// same way: `Array(...)` (the regular array), `Int8Array(...)` (the typed
// views), `ArrayBuffer(...)` / `SharedArrayBuffer(...)` (the buffers) and
// `DataView(...)` (the byte view). The regular array's own lowering (the
// `Array(...)` element list, `Array.of` / `Array.from`, the array literal and
// the declarator forms) lives in the sibling unit (codegen.cpp).
//
// Every form the corpus does not pin reports CP3002 through `notImplemented`
// and returns a compilable stub.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "detail/generator/family.h"
#include "detail/generator/type_map.h"
#include "cp3/host/native_types.hpp"

namespace cp3 {

using namespace detail;

// =============================================================================
// Constructors
// =============================================================================
// `Array(...)` / `Int8Array(...)` without `new`: sema pins both spellings to
// the same constructor.
std::string Generator::emitFamilyCtorCall(const CallExpr& c,
                                          const std::string& name) {
    if (!isFamilyCtorName(name)) return std::string();
    return emitFamilyCtorText(name, c.arguments, arrayElemOf(effectiveType(c)),
                              c);
}

// `emitCall`, but the `new`-less spelling of a family constructor lowers like
// its `new` form first. A user-declared or shadowing binding wins over the
// ambient family name.
std::string Generator::emitCallOrFamily(const CallExpr& c) {
    if (c.callee && c.callee->kind == NodeKind::Identifier &&
        c.typeArgs.empty()) {
        const std::string& name =
            static_cast<const Identifier&>(*c.callee).name;
        if (functions_.find(name) == functions_.end() && !lookup(name)) {
            std::string family = emitFamilyCtorCall(c, name);
            if (!family.empty()) return family;
        }
    }
    return emitCall(c);
}

// `new Array(2)` / `new DataView(buf)`. A `new` of anything else keeps the
// ordinary not-implemented form.
std::string Generator::emitFamilyNew(const NewExpr& n) {
    if (n.callee && n.callee->kind == NodeKind::Identifier) {
        const std::string name = static_cast<const Identifier&>(*n.callee).name;
        if (isFamilyCtorName(name)) {
            return emitFamilyCtorText(name, n.arguments,
                                      arrayElemOf(effectiveType(n)), n);
        }
    }
    notImplemented(n, "this new expression");
    return "0.0";
}

std::string Generator::emitFamilyCtorText(const std::string& cls,
                                          const ExprList& args,
                                          const Type& resultElem,
                                          const Node& site) {
    if (cls == "Array") return arrayCtorText(args, resultElem, site);
    if (cls == "ArrayBuffer" || cls == "SharedArrayBuffer") {
        if (args.size() != 1 || !args.front()) {
            notImplemented(site, "the buffer length constructor");
            return "0.0";
        }
        needsTypedArray_ = true;
        const std::string fn = cls == "ArrayBuffer" ? "makeArrayBuffer"
                                                    : "makeSharedArrayBuffer";
        return "::cp3::runtime::" + fn + "(" + expr(*args.front()) + ")";
    }
    if (cls == "DataView") {
        if (args.empty() || args.size() > 3) {
            notImplemented(site, "the DataView constructor");
            return "0.0";
        }
        needsTypedArray_ = true;
        std::string out = "::cp3::runtime::makeDataView(";
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i != 0) out += ", ";
            out += args[i] ? expr(*args[i]) : "0.0";
        }
        return out + ")";
    }
    return typedCtorText(cls, args, site);
}

// `new Int8Array(...)` (and its `new`-less spelling): a length, a literal
// list, a cp3 array, another view, or a buffer view.
std::string Generator::typedCtorText(const std::string& cls,
                                     const ExprList& args, const Node& site) {
    const char* tag = familyTag(cls);
    if (tag == nullptr) {
        notImplemented(site, "the family constructor");
        return "0.0";
    }
    const std::string t = "::cp3::runtime::" + std::string(tag);
    needsTypedArray_ = true;
    if (args.empty()) return "::cp3::runtime::makeTypedArray<" + t + ">(0.0)";
    if (args.size() > 1 && args.size() <= 3) {
        std::string out = "::cp3::runtime::makeTypedArrayView<" + t + ">(";
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i != 0) out += ", ";
            out += args[i] ? expr(*args[i]) : "0.0";
        }
        return out + ")";
    }
    if (args.size() != 1 || !args.front()) {
        notImplemented(site, "the typed-array constructor");
        return "0.0";
    }
    const Expr& arg = *args.front();
    if (arg.kind == NodeKind::ArrayLiteral) {
        return typedCtorLiteralText(t, static_cast<const ArrayLiteral&>(arg));
    }
    switch (info(arg).shape) {
        case Shape::Number:
        case Shape::Unknown:
            return "::cp3::runtime::makeTypedArray<" + t + ">(" + expr(arg) +
                   ")";
        case Shape::Array:
            return "::cp3::runtime::makeTypedArrayFromArray<" + t + ">(" +
                   expr(arg) + ")";
        case Shape::TypedArray:
            return "::cp3::runtime::makeTypedArrayFrom<" + t + ">(" + expr(arg) +
                   ")";
        case Shape::ArrayBuffer:
            return "::cp3::runtime::makeTypedArrayView<" + t + ">(" +
                   expr(arg) + ", 0.0)";
        default:
            break;
    }
    notImplemented(site, "the typed-array constructor argument");
    return "0.0";
}

// `new Int8Array([1, 2])` / `new Int8Array([...a, 1])`: the element list
// becomes the view's values. A hole (`[1, , 2]`) is `undefined` in JS, and a
// typed array coerces it to 0.
std::string Generator::typedCtorLiteralText(const std::string& t,
                                            const ArrayLiteral& lit) {
    bool hasSpread = false;
    for (const auto& el : lit.elements) {
        if (el && el->kind == NodeKind::SpreadElement) hasSpread = true;
    }
    if (hasSpread) {
        // The spread's elements join the list, so the values are collected at
        // runtime.
        const std::string list = spreadListText(Type::number(), lit.elements);
        if (list.empty()) return "0.0";
        return "::cp3::runtime::makeTypedArrayFromArray<" + t + ">("
               "::cp3::runtime::makeArray<double>(" + list + "))";
    }
    std::string out = "::cp3::runtime::makeTypedArrayOf<" + t + ">({";
    for (std::size_t i = 0; i < lit.elements.size(); ++i) {
        if (i != 0) out += ", ";
        const Expr* el = lit.elements[i].get();
        if (el == nullptr) {
            out += "0.0";
            continue;
        }
        out += expr(*el);
    }
    return out + "})";
}

}  // namespace cp3
