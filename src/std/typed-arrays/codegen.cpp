// =============================================================================
// src/std/typed-arrays/codegen.cpp - the JS-compat typed-array family (D69).
//
// Lowers the family registered by sema (src/std/typed-arrays/sema.cpp) onto
// cp3/typed_array.hpp: the constructors and statics (`Array(n)`,
// `Array(x, y)`, `Array.of`, `Array.from`, the typed views, the buffer
// classes, DataView), the array-literal lowering and the declarator
// special-cases (`let a: number[] = Array(3)`). The member / property /
// index emitters live in the sibling codegen_methods.cpp.
//
// `classifyFamily` (type_map.h) owns the type model; these units own the
// emission only. Every form the corpus does not pin reports CP3002 through
// `notImplemented` and returns a compilable stub.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "cp3/diagnostic_codes.hpp"
#include "detail/family.h"
#include "detail/type_map.h"
#include "cp3/native_types.hpp"

namespace cp3 {

namespace detail {

// The family's C++ mapping (see type_map.h): the regular array value type, the
// typed views, the buffer classes and DataView. Every other type is left for
// the caller (`classify`'s own arms).
bool classifyFamily(const Type& t, CppInfo& info) {
    if (t.kind == TypeKind::Array) {
        CppInfo ec;
        if (t.parts.empty() || !arrayElementInfo(t.parts.front(), ec)) {
            return false;
        }
        info.shape = Shape::Array;
        info.representable = true;
        info.cpp = "::cp3::runtime::Array<" + ec.cpp + ">";
        return true;
    }
    if (t.kind != TypeKind::Class) return false;
    if (const FamilyClass* fc = familyClass(t.name)) {
        if (fc->tag[0] != '\0') {
            info.shape = Shape::TypedArray;
            info.representable = true;
            info.cpp =
                std::string("::cp3::runtime::TypedArray<::cp3::runtime::") +
                fc->tag + ">";
            return true;
        }
        if (t.name == "DataView") info.shape = Shape::DataView;
        else info.shape = Shape::ArrayBuffer;
        info.representable = true;
        info.cpp = "::cp3::runtime::Ref<::cp3::runtime::" + t.name + ">";
        return true;
    }
    return false;
}

}  // namespace detail

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
        // A literal lowers as an array (a spread element contributes its own
        // elements); the view copies the array's elements.
        const auto& lit = static_cast<const ArrayLiteral&>(arg);
        return "::cp3::runtime::makeTypedArrayFromArray<" + t + ">(" +
               arrayLiteralText(lit, Type::unknown()) + ")";
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

// `Array(...)`: the single-number length form (dense, default-initialized) and
// the multi-argument literal form. The element type comes from the result (a
// binding annotation, `.fill`, ...) because `Array(n)` alone leaves it unknown.
std::string Generator::arrayCtorText(const ExprList& args, const Type& elem,
                                     const Node& site) {
    if (elem.isUnknown()) {
        notImplemented(site, "Array(...) without a known element type");
        return "0.0";
    }
    CppInfo ec;
    if (!arrayElementInfo(elem, ec)) {
        notImplemented(site, "the Array(...) element type");
        return "0.0";
    }
    needsTypedArray_ = true;
    if (args.size() == 1 && args.front()) {
        const Shape s = info(*args.front()).shape;
        if (s == Shape::Number || s == Shape::Unknown) {
            return "::cp3::runtime::makeArrayDefault<" + ec.cpp + ">(" +
                   expr(*args.front()) + ")";
        }
    }
    bool hasSpread = false;
    for (const auto& arg : args) {
        if (arg && arg->kind == NodeKind::SpreadElement) hasSpread = true;
    }
    if (hasSpread) {
        // `Array(...xs, 3)`: the spread's elements come first (JS semantics).
        // A braced list cannot splice an existing array, so the elements are
        // collected into a vector first.
        const std::string v = freshLabel("elems_");
        std::string body = "std::vector<" + ec.cpp + "> " + v + ";";
        for (const auto& arg : args) {
            if (!arg) continue;
            if (arg->kind != NodeKind::SpreadElement) {
                body += " " + v + ".push_back(" + expr(*arg) + ");";
                continue;
            }
            const auto& sp = static_cast<const SpreadElement&>(*arg);
            if (!sp.argument || info(*sp.argument).shape != Shape::Array) {
                // A spread argument has to be an array (the element list is
                // spliced from it).
                reportUnsupported(*arg, diagcode::kTypeMismatch,
                                  "type mismatch: a spread argument must be an "
                                  "array");
                continue;
            }
            body += " { const auto& cp3_src = " + expr(*sp.argument) + "; " + v +
                    ".insert(" + v +
                    ".end(), cp3_src->begin(), cp3_src->end()); }";
        }
        return "[&]{ " + body + " return ::cp3::runtime::makeArray<" + ec.cpp +
               ">(std::move(" + v + ")); }()";
    }
    std::string out = "::cp3::runtime::makeArray<" + ec.cpp + ">(std::vector<" +
                      ec.cpp + ">{";
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) out += ", ";
        const Expr* arg = args[i].get();
        if (arg == nullptr) continue;
        out += expr(*arg);
    }
    return out + "})";
}

// `Array.of(...)` / `Array.from(src)`.
std::string Generator::arrayStaticCallText(const CallExpr& c,
                                           const std::string& prop) {
    if (prop == "of") {
        CppInfo ec;
        if (!arrayElementInfo(arrayElemOf(effectiveType(c)), ec)) {
            notImplemented(c, "the Array.of element type");
            return "0.0";
        }
        needsTypedArray_ = true;
        std::string out = "::cp3::runtime::makeArray<" + ec.cpp +
                          ">(std::vector<" + ec.cpp + ">{";
        for (std::size_t i = 0; i < c.arguments.size(); ++i) {
            if (i != 0) out += ", ";
            out += c.arguments[i] ? expr(*c.arguments[i]) : "0.0";
        }
        return out + "})";
    }
    if (prop == "from" && c.arguments.size() == 1 && c.arguments.front()) {
        const Expr& src = *c.arguments.front();
        const Type st = effectiveType(src);
        if (st.kind == TypeKind::Array && info(src).representable) {
            needsTypedArray_ = true;
            return "::cp3::runtime::arrayFromArray(" + expr(src) + ")";
        }
        if (st.kind == TypeKind::Class && familyTag(st.name) != nullptr) {
            needsTypedArray_ = true;
            return "::cp3::runtime::arrayFromTyped(" + expr(src) + ")";
        }
        notImplemented(c, "Array.from over this source");
        return "0.0";
    }
    return std::string();  // not a form this unit lowers
}

// An array literal; `elemOverride` supplies the element type when the literal
// set none (an empty literal with an annotated binding).
std::string Generator::arrayLiteralText(const ArrayLiteral& lit,
                                        const Type& elemOverride) {
    Type elem = elemOverride;
    if (elem.isUnknown()) elem = arrayElemOf(effectiveType(lit));
    CppInfo ec;
    if (!arrayElementInfo(elem, ec)) {
        notImplemented(lit, "the array literal element type");
        return "0.0";
    }
    needsTypedArray_ = true;
    bool hasSpread = false;
    for (const auto& el : lit.elements) {
        if (el && el->kind == NodeKind::SpreadElement) hasSpread = true;
    }
    if (hasSpread) {
        // `[...a, 4, 5]`: a std::vector braced list cannot splice an existing
        // vector, so concatenate into a local vector inside a lambda.
        const std::string v = freshLabel("elems_");
        std::string body = "std::vector<" + ec.cpp + "> " + v + ";";
        for (const auto& el : lit.elements) {
            if (!el) continue;
            if (el->kind != NodeKind::SpreadElement) {
                body += " " + v + ".push_back(" + expr(*el) + ");";
                continue;
            }
            const auto& sp = static_cast<const SpreadElement&>(*el);
            if (!sp.argument || info(*sp.argument).shape != Shape::Array) {
                notImplemented(*el, "this spread element");
                continue;
            }
            body += " { const auto& cp3_src = " + expr(*sp.argument) + "; " + v +
                    ".insert(" + v +
                    ".end(), cp3_src->begin(), cp3_src->end()); }";
        }
        return "[&]{ " + body + " return ::cp3::runtime::makeArray<" + ec.cpp +
               ">(std::move(" + v + ")); }()";
    }
    std::string out = "::cp3::runtime::makeArray<" + ec.cpp + ">(std::vector<" +
                      ec.cpp + ">{";
    for (std::size_t i = 0; i < lit.elements.size(); ++i) {
        if (i != 0) out += ", ";
        const Expr* el = lit.elements[i].get();
        if (el == nullptr) {
            // A hole: a cp3 array is dense (D69.4 - no hole state exists), so
            // the slot holds the element type's default value.
            out += ec.cpp + "{}";
            continue;
        }
        // A native element takes the literal as an integer token: the braced
        // initializer would reject `1.0` as a narrowing conversion, and an
        // i64 element must not round-trip through double.
        if (isNativeType(elem)) {
            if (const auto k = integralLiteralValue(*el)) {
                out += std::to_string(*k);
                continue;
            }
        }
        out += expr(*el);
    }
    return out + "})";
}

}  // namespace cp3
