// =============================================================================
// src/std/typed-arrays/codegen.cpp - the JS-compat typed-array family (D69).
//
// Lowers the family registered by sema (src/std/typed-arrays/sema.cpp) onto
// cp3/typed_array.hpp: the constructors and statics (`Array(n)`,
// `Array(x, y)`, `Array.of`, `Array.from`, the typed views, the buffer
// classes, DataView), the array-literal lowering and the declarator
// special-cases (`let a: number[] = Array(3)`). The member / property / index
// emitters live in the sibling codegen_methods.cpp.
//
// `classifyFamily` (type_map.h) owns the type model; these units own the
// emission only. Every form the corpus does not pin reports CP3002 through
// `notImplemented` and returns a compilable stub.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "detail/family.h"
#include "detail/type_map.h"
#include "cp3/native_types.hpp"

namespace cp3 {

namespace detail {

// The family's C++ mapping (see type_map.h): the regular array value type, the
// typed views, the buffer classes and DataView. Every other type is left for
// the caller (`classify`'s own arms handle primitives, Promise, ...).
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
        const auto& lit = static_cast<const ArrayLiteral&>(arg);
        std::string out = "::cp3::runtime::makeTypedArrayOf<" + t + ">({";
        for (std::size_t i = 0; i < lit.elements.size(); ++i) {
            if (i != 0) out += ", ";
            const Expr* el = lit.elements[i].get();
            if (el == nullptr || el->kind == NodeKind::SpreadElement) {
                notImplemented(site, "a spread argument");
                continue;
            }
            out += expr(*el);
        }
        return out + "})";
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
    std::string out = "::cp3::runtime::makeArray<" + ec.cpp + ">(std::vector<" +
                      ec.cpp + ">{";
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) out += ", ";
        const Expr* arg = args[i].get();
        if (arg == nullptr || arg->kind == NodeKind::SpreadElement) {
            notImplemented(site, "a spread argument");
            continue;
        }
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

std::string Generator::familyReceiverText(const Expr& obj, const Type& elem,
                                          const Node& site) {
    if (const ExprList* args = arrayCtorArgs(obj)) {
        return arrayCtorText(*args, elem, site);
    }
    if (obj.kind == NodeKind::ArrayLiteral) {
        return arrayLiteralText(static_cast<const ArrayLiteral&>(obj), elem);
    }
    return expr(obj);
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
    std::string out = "::cp3::runtime::makeArray<" + ec.cpp + ">(std::vector<" +
                      ec.cpp + ">{";
    for (std::size_t i = 0; i < lit.elements.size(); ++i) {
        if (i != 0) out += ", ";
        const Expr* el = lit.elements[i].get();
        if (el == nullptr || el->kind == NodeKind::SpreadElement) {
            notImplemented(lit, "a spread element");
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

// The initializer of a declaration that lowers specially: `let a: number[] =
// Array(3)` (element from the annotation) and `let a: number[] = []` (the
// annotation fixes an otherwise unknowable element type). "" means the caller
// lowers the initializer normally.
std::string Generator::declaratorInitText(const VariableDeclarator& d,
                                          const Type& declared) {
    if (!d.init || declared.kind != TypeKind::Array) return std::string();
    if (const ExprList* args = arrayCtorArgs(*d.init)) {
        return arrayCtorText(*args, arrayElemOf(declared), *d.init);
    }
    if (d.init->kind == NodeKind::ArrayLiteral &&
        (arrayElemOf(effectiveType(*d.init)).isUnknown() ||
         isNativeType(arrayElemOf(declared)))) {
        return arrayLiteralText(static_cast<const ArrayLiteral&>(*d.init),
                                arrayElemOf(declared));
    }
    return std::string();
}


}  // namespace cp3
