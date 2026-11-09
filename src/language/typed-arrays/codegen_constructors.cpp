// =============================================================================
// src/language/typed-arrays/codegen_constructors.cpp - the JS-compat typed-array
// family's constructors (D69).
//
// The `new`-less and the `new` spellings of a family constructor lower the
// same way: `Array(...)` (the regular array), `Int8Array(...)` (the typed
// views), `ArrayBuffer(...)` / `SharedArrayBuffer(...)` (the buffers) and
// `DataView(...)` (the byte view). The regular array's own lowering (the
// `Array(...)` element list, `Array.of` / `Array.from`, the array literal and
// the declarator forms) lives in the sibling unit (codegen.cpp).
//
// A form the family grammar rejects (a wrong arity, or an argument the type
// checker refuses) reports the matching compile error. A `new` of a value that
// is not a class or a family constructor reports a type error instead of
// lowering a stub.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "detail/generator/family.h"
#include "detail/generator/type_map.h"
#include "cp3/diag/diagnostic_codes.hpp"
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
    if (c.callee && c.callee->kind == NodeKind::Identifier) {
        // `Array` is the one family member sema accepts explicit type
        // arguments on (`Array<number>(n)`, TS spelling); every other family
        // constructor takes none.
        const std::string& name =
            static_cast<const Identifier&>(*c.callee).name;
        const bool familyCall = c.typeArgs.empty() || name == "Array";
        if (familyCall && functions_.find(name) == functions_.end() &&
            !lookup(name)) {
            std::string family = emitFamilyCtorCall(c, name);
            if (!family.empty()) return family;
        }
    }
    return emitCall(c);
}

// `new Array(2)` / `new DataView(buf)`. A user-declared value or function of
// the same spelling wins over the ambient family name (sema resolves the
// family constructor only for an ambient binding). A `new` of anything else
// is not a constructible cp3 value: functions, namespaces and ambient values
// have no `new` lowering.
std::string Generator::emitFamilyNew(const NewExpr& n) {
    if (n.callee && n.callee->kind == NodeKind::Identifier) {
        const std::string name = static_cast<const Identifier&>(*n.callee).name;
        if (functions_.find(name) == functions_.end() && !lookup(name) &&
            isFamilyCtorName(name)) {
            return emitFamilyCtorText(name, n.arguments,
                                      arrayElemOf(effectiveType(n)), n);
        }
    }
    const Type calleeType = n.callee ? effectiveType(*n.callee) : Type::unknown();
    reportUnsupported(n, diagcode::kTypeMismatch,
                      "type mismatch: only a class or a typed-array family "
                      "constructor can be constructed with 'new' (got '" +
                          describe(calleeType) + "')");
    return "0.0";
}

std::string Generator::emitFamilyCtorText(const std::string& cls,
                                          const ExprList& args,
                                          const Type& resultElem,
                                          const Node& site) {
    if (cls == "Array") return arrayCtorText(args, resultElem, site);
    if (cls == "ArrayBuffer" || cls == "SharedArrayBuffer") {
        if (args.size() != 1) {
            reportUnsupported(site, diagcode::kArityMismatch,
                              "arity mismatch: expected 1, got " +
                                  std::to_string(args.size()) + " arguments");
            return "0.0";
        }
        if (!args.front()) {
            reportUnsupported(site, diagcode::kTypeMismatch,
                              "type mismatch: the buffer length constructor "
                              "needs a byte-length argument");
            return "0.0";
        }
        needsTypedArray_ = true;
        const std::string fn = cls == "ArrayBuffer" ? "makeArrayBuffer"
                                                    : "makeSharedArrayBuffer";
        return "::cp3::runtime::" + fn + "(" + expr(*args.front()) + ")";
    }
    if (cls == "DataView") {
        if (args.empty() || args.size() > 3) {
            reportUnsupported(site, diagcode::kArityMismatch,
                              "arity mismatch: expected 1 to 3, got " +
                                  std::to_string(args.size()) + " arguments");
            return "0.0";
        }
        if (!args.front()) {
            reportUnsupported(site, diagcode::kTypeMismatch,
                              "type mismatch: a DataView needs an ArrayBuffer "
                              "as its first argument");
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
        // `emitFamilyCtorText` routes `Array`, the two buffers and `DataView`
        // before this point, so only a typed-view name reaches here; a missing
        // tag is a compiler invariant, not a program error.
        reportUnsupported(site, diagcode::kInternalError,
                          "the typed-array constructor has no tag for '" + cls +
                              "'");
        return "0.0";
    }
    const std::string t = "::cp3::runtime::" + std::string(tag);
    needsTypedArray_ = true;
    if (args.empty()) return "::cp3::runtime::makeTypedArray<" + t + ">(0.0)";
    if (args.size() > 3) {
        reportUnsupported(site, diagcode::kArityMismatch,
                          "arity mismatch: expected 1 to 3, got " +
                              std::to_string(args.size()) + " arguments");
        return "0.0";
    }
    if (args.size() > 1) {
        if (!args.front()) {
            reportUnsupported(site, diagcode::kTypeMismatch,
                              "type mismatch: a typed-array view needs an "
                              "ArrayBuffer as its first argument");
            return "0.0";
        }
        std::string out = "::cp3::runtime::makeTypedArrayView<" + t + ">(";
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i != 0) out += ", ";
            out += args[i] ? expr(*args[i]) : "0.0";
        }
        return out + ")";
    }
    if (!args.front()) {
        reportUnsupported(site, diagcode::kTypeMismatch,
                          "type mismatch: the typed-array constructor needs an "
                          "argument");
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
        case Shape::Array: {
            // The runtime copies a `Array<double>`; another element type has
            // no coercion (typed-array constructors reject scalar coercions
            // too), so it is a type error rather than a raw C++ mismatch.
            const Type elem = arrayElemOf(info(arg).type);
            CppInfo ec;
            if (!arrayElementInfo(elem, ec) || ec.cpp != "double") {
                reportUnsupported(arg, diagcode::kTypeMismatch,
                                  "type mismatch: cannot construct '" + cls +
                                      "' from an array of '" + describe(elem) +
                                      "' (typed-array elements are numbers)");
                return "0.0";
            }
            return "::cp3::runtime::makeTypedArrayFromArray<" + t + ">(" +
                   expr(arg) + ")";
        }
        case Shape::TypedArray:
            return "::cp3::runtime::makeTypedArrayFrom<" + t + ">(" + expr(arg) +
                   ")";
        case Shape::ArrayBuffer:
            return "::cp3::runtime::makeTypedArrayView<" + t + ">(" +
                   expr(arg) + ", 0.0)";
        default:
            break;
    }
    reportUnsupported(arg, diagcode::kTypeMismatch,
                      "type mismatch: cannot construct '" + cls + "' from '" +
                          describe(info(arg).type) +
                          "' (expected a length, an array, a typed array or an "
                          "ArrayBuffer)");
    return "0.0";
}

// `new Int8Array([1, 2])` / `new Int8Array([...a, 1])`: the element list
// becomes the view's values. A hole (`[1, , 2]`) is `undefined` in JS, and a
// typed array coerces it to 0. An explicit nullish element does the same; any
// other element type is a type error (a raw C++ mismatch would otherwise leak
// out of the generated program).
std::string Generator::typedCtorLiteralText(const std::string& t,
                                            const ArrayLiteral& lit) {
    bool hasSpread = false;
    for (const auto& el : lit.elements) {
        if (el && el->kind == NodeKind::SpreadElement) hasSpread = true;
    }
    for (const auto& el : lit.elements) {
        if (!el || el->kind == NodeKind::SpreadElement) continue;
        const CppInfo ei = info(*el);
        if (ei.shape == Shape::Number) continue;
        if (!hasSpread &&
            (ei.shape == Shape::Undefined || ei.shape == Shape::Null)) {
            continue;
        }
        reportTypeError(*el, "type mismatch: a typed-array element must be a "
                             "number (got '" +
                                 describe(ei.type) + "')");
        return "0.0";
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
        const CppInfo ei = info(*el);
        if (ei.shape == Shape::Undefined || ei.shape == Shape::Null) {
            out += "0.0";
            continue;
        }
        out += expr(*el);
    }
    return out + "})";
}

}  // namespace cp3
