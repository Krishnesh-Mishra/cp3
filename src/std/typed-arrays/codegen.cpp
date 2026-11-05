// =============================================================================
// src/std/typed-arrays/codegen.cpp - the JS-compat typed-array family (D69).
//
// Lowers the family registered by sema (src/std/typed-arrays/sema.cpp) onto
// cp3/typed_array.hpp: the regular array's statics (`Array.of`, `Array.from`),
// the array-literal lowering and the declarator special-cases
// (`let a: number[] = Array(3)`). The family constructors live in the sibling
// codegen_constructors.cpp and the member / property / index emitters in
// codegen_methods.cpp.
//
// `classifyFamily` (type_map.h) owns the type model; these units own the
// emission only. A value this unit cannot represent is reported as a type
// error by the emitter that owns the construct.
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

// The diagnostic for an element type an array cannot hold: the type is
// unknown (nothing says what the elements are) or it has no C++ spelling.
// cp3 has no fully dynamic array (the language spec's rule), so the binding
// has to name the element type.
void Generator::reportArrayElementType(const Type& elem, const Node& site,
                                        const std::string& what) {
    if (elem.isUnknown()) {
        reportTypeError(site, "cannot infer the element type of " + what +
                              "; annotate the binding");
        return;
    }
    reportTypeError(site, "type mismatch: an array cannot hold elements of "
                          "type '" + describe(elem) + "'");
}

// `Array(...)`: the single-number length form (dense, default-initialized) and
// the multi-argument literal form. The element type comes from the result (a
// binding annotation, `.fill`, ...) because `Array(n)` alone leaves it unknown.
std::string Generator::arrayCtorText(const ExprList& args, const Type& elem,
                                     const Node& site) {
    if (elem.isUnknown()) {
        // A type error in cp3, exactly as the sema reports the declarator form.
        reportCodegenError(site, diagcode::kTypeMismatch,
                           "cannot infer the element type of Array(n); "
                           "annotate the binding or chain .fill(...)");
        return "0.0";
    }
    CppInfo ec;
    if (!arrayElementInfo(elem, ec)) {
        // The element type is known but has no C++ spelling (an error type, a
        // void, ...): the program cannot be lowered, and the diagnostic says
        // why instead of leaving a silent stub behind.
        reportArrayElementType(elem, site, "Array(...)");
        return "0.0";
    }
    needsTypedArray_ = true;
    // The length form: a single number argument. A spread argument is never
    // a length - it is the literal form (JS parity).
    if (args.size() == 1 && args.front() &&
        args.front()->kind != NodeKind::SpreadElement) {
        const Shape s = info(*args.front()).shape;
        if (s == Shape::Number || s == Shape::Unknown) {
            return "::cp3::runtime::makeArrayDefault<" + ec.cpp + ">(" +
                   expr(*args.front()) + ")";
        }
    }
    // `Array(...a)`: a spread's elements join the literal list, so the
    // vector is built at runtime instead of in a braced initializer.
    bool hasSpread = false;
    for (const auto& arg : args) {
        if (arg && arg->kind == NodeKind::SpreadElement) hasSpread = true;
    }
    if (hasSpread) {
        const std::string list = spreadListText(elem, args);
        if (list.empty()) return "0.0";
        return "::cp3::runtime::makeArray<" + ec.cpp + ">(" + list + ")";
    }
    std::string out = "::cp3::runtime::makeArray<" + ec.cpp + ">(std::vector<" +
                      ec.cpp + ">{";
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) out += ", ";
        const Expr* arg = args[i].get();
        if (arg == nullptr) {
            // A hole (`Array(1, , 2)`) is `undefined` in JS.
            out += "::cp3::runtime::undefined";
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
        // The element type comes from the call's own type; an empty
        // `Array.of()` has none, so the context's expected type (the binding
        // annotation) supplies it - `let a: number[] = Array.of()`.
        Type elem = arrayElemOf(effectiveType(c));
        if (elem.isUnknown() && expected_ && expected_->kind == TypeKind::Array) {
            elem = arrayElemOf(*expected_);
        }
        CppInfo ec;
        if (!arrayElementInfo(elem, ec)) {
            reportArrayElementType(elem, c, "Array.of()");
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
        // A type error, exactly as the sema reports it.
        reportCodegenError(c, diagcode::kTypeMismatch,
                           "type mismatch: Array.from needs an array or a typed array");
        return "0.0";
    }
    if (prop == "isArray" && c.arguments.size() == 1 && c.arguments.front()) {
        // An array literal (or any Array-typed webserverion) is an array; every
        // other static type is not. Typed views are distinct classes at
        // runtime, so they correctly report false here too.
        const Expr& arg = *c.arguments.front();
        if (arg.kind == NodeKind::ArrayLiteral) return "true";
        const Type st = effectiveType(arg);
        return st.kind == TypeKind::Array ? "true" : "false";
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
        reportArrayElementType(elem, lit, "an empty array literal");
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
            if (!sp.argument) {
                sink_.error(diagcode::kTypeMismatch, el->span,
                            "type mismatch: cannot spread this value into the "
                            "array literal");
                continue;
            }
            const CppInfo si = info(*sp.argument);
            if (si.shape == Shape::Array) {
                body += " { const auto& cp3_src = " + expr(*sp.argument) + "; " +
                        v + ".insert(" + v + ".end(), cp3_src->begin(), cp3_src->end()); }";
                continue;
            }
            if (si.shape == Shape::Collections) {
                // `[...set]` spreads the values, `[...map]` the entries.
                const Type recv = effectiveType(*sp.argument);
                const bool isMapCol =
                    recv.kind == TypeKind::Class && recv.name == "Map";
                body += " { const auto& cp3_src = " + expr(*sp.argument) + "; " +
                        "const auto cp3_spread = cp3_src->" +
                        (isMapCol ? std::string("entries()")
                                  : std::string("values()")) +
                        "; " + v + ".insert(" + v + ".end(), " +
                        "cp3_spread->begin(), cp3_spread->end()); }";
                continue;
            }
            if (si.shape == Shape::TypedArray && ec.cpp == "double") {
                // A typed view spreads as its numbers (JS parity: the elements
                // of an Int8Array are numbers).
                body += " { const auto& cp3_src = ::cp3::runtime::arrayFromTyped(" +
                        expr(*sp.argument) + "); " + v + ".insert(" + v +
                        ".end(), cp3_src->begin(), cp3_src->end()); }";
                continue;
            }
            if (si.shape == Shape::String && ec.cpp == "std::string") {
                // A string spreads as its characters (JS parity; cp3 counts
                // code points, like `charAt`).
                body += " { const std::string& cp3_src = " + expr(*sp.argument) +
                        "; std::size_t cp3_i = 0; while (cp3_i < cp3_src.size()) "
                        "{ std::size_t cp3_n = 1; while (cp3_i + cp3_n < cp3_src.size() "
                        "&& (static_cast<unsigned char>(cp3_src[cp3_i + cp3_n]) & 0xC0u) "
                        "== 0x80u) ++cp3_n; " + v +
                        ".push_back(cp3_src.substr(cp3_i, cp3_n)); cp3_i += cp3_n; } }";
                continue;
            }
            // A value cp3 cannot iterate (a class instance, a Map, ...) or one
            // whose elements do not fit the literal's element type.
            sink_.error(diagcode::kTypeMismatch, el->span,
                        "type mismatch: cannot spread '" + describe(si.type) +
                            "' into the array literal");
            continue;
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
            // D69.4: cp3 arrays are dense and have no hole state. A hole holds
            // the element type's default value and keeps its position.
            out += ec.cpp + "{}";
            continue;
        }
        if (el->kind == NodeKind::SpreadElement) {
            // The spread branch above owns every spread element; keep a real
            // diagnostic here instead of a placeholder if that ever changes.
            reportCodegenError(lit, diagcode::kTypeMismatch,
                               "type mismatch: cannot spread this value into "
                               "the array literal");
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
        // A tuple element (`[["a", 1]]` for `[string, number][]`): the inner
        // literal is a std::tuple value, not a nested array (spec 03).
        if (el->kind == NodeKind::ArrayLiteral &&
            isTupleType(effectiveType(*el))) {
            const Type et = effectiveType(*el);
            const auto& inner = static_cast<const ArrayLiteral&>(*el);
            std::string cpp;
            if (tupleCpp(et, cpp) && inner.elements.size() == et.parts.size()) {
                needsTuple_ = true;
                out += cpp + "{";
                for (std::size_t j = 0; j < inner.elements.size(); ++j) {
                    out += (j == 0 ? "" : ", ") + expr(*inner.elements[j]);
                }
                out += "}";
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
    // `let t: [string, number] = ["a", 1]`: the tuple annotation fixes the
    // element types, so brace-initialize the std::tuple directly instead of
    // lowering the literal as an Array (spec 03).
    if (d.init && isTupleType(declared) &&
        d.init->kind == NodeKind::ArrayLiteral) {
        const auto& lit = static_cast<const ArrayLiteral&>(*d.init);
        std::string cpp;
        if (lit.elements.size() != declared.parts.size() ||
            !tupleCpp(declared, cpp)) {
            return std::string();
        }
        needsTuple_ = true;
        std::string out = cpp + "{";
        for (std::size_t i = 0; i < lit.elements.size(); ++i) {
            const Expr* element = lit.elements[i].get();
            if (element == nullptr || element->kind == NodeKind::SpreadElement) {
                return std::string();
            }
            out += (i == 0 ? "" : ", ") + expr(*element);
        }
        return out + "}";
    }
    if (!d.init || declared.kind != TypeKind::Array) return std::string();
    if (const ExprList* args = arrayCtorArgs(*d.init)) {
        return arrayCtorText(*args, arrayElemOf(declared), *d.init);
    }
    if (d.init->kind == NodeKind::ArrayLiteral) {
        // The annotation's element type wins when it has a spelling: a class /
        // interface element (`let s: Shape[] = [new Sq()]`) must lower to the
        // annotated handle type, not the literal's inferred union. A native
        // element annotation and an otherwise unknowable element type take the
        // same path.
        const Type elem = arrayElemOf(declared);
        CppInfo dc;
        if (arrayElementInfo(elem, dc) || isNativeType(elem) ||
            arrayElemOf(effectiveType(*d.init)).isUnknown()) {
            return arrayLiteralText(static_cast<const ArrayLiteral&>(*d.init),
                                    elem);
        }
        return std::string();
    }
    return std::string();
}


}  // namespace cp3
