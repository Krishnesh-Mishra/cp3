// =============================================================================
// src/std/arrays/codegen_methods.cpp - the regular-array method surface
// (docs/language-spec/09-arrays-strings.md): mutation, callback iteration,
// searching, slicing, the in-place mutators and `for...of`.
//
// detail::ArrayEmitter (generator.h) holds the static entry points; Generator
// friends it for the private emitters it drives. The lowering calls the
// runtime extras in cp3/array_extras.hpp with the element type from sema;
// callback methods lower the arrow with the element type as its parameter
// context. Out-of-scope forms report CP3002.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "detail/cpp_writer.h"
#include "detail/family.h"
#include "detail/mangle.h"
#include "detail/pattern_emitter.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// The arrow argument at `index`, or null: the caller reports the unsupported
// non-arrow form (a named function callback is not lowered).
const ArrowFunction* arrowArg(const CallExpr& c, std::size_t index) {
    if (index >= c.arguments.size() || !c.arguments[index]) return nullptr;
    const Expr* arg = c.arguments[index].get();
    if (arg->kind != NodeKind::ArrowFunction) return nullptr;
    return static_cast<const ArrowFunction*>(arg);
}

bool isOneOf(const std::string& prop, const char* const* names,
             std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (prop == names[i]) return true;
    }
    return false;
}

template <std::size_t N>
bool isOneOf(const std::string& prop, const char* const (&names)[N]) {
    return isOneOf(prop, names, N);
}

}  // namespace

// The callback set (`map` / `filter` / `reduce` / `find` / `findIndex` /
// `some` / `every`): each arrow lowers with its parameter context.
std::string detail::ArrayEmitter::callbackText(Generator& g, const CallExpr& c,
                                               const MemberExpr& m,
                                               const CppInfo& ec,
                                               const std::string& recvText) {
    const std::string& prop = m.property;
    const ArrowFunction* cb = arrowArg(c, 0);
    if (cb == nullptr) {
        g.notImplemented(c, "a non-arrow array callback");
        return "0.0";
    }
    if (prop == "map") {
        CppInfo rc;
        if (!arrayElementInfo(arrayElemOf(g.effectiveType(c)), rc)) {
            g.notImplemented(c, "the array map result type");
            return "0.0";
        }
        return "::cp3::runtime::arrayMap<" + rc.cpp + ">(" + recvText + ", " +
               g.emitArrow(*cb, {ec.cpp}) + ")";
    }
    if (prop == "reduce") {
        // The accumulator parameter has the fold's result type: the initial
        // value's when given, the element's otherwise (a no-initial fold seeds
        // from the first element, JS-style).
        CppInfo rc;
        if (!arrayElementInfo(g.effectiveType(c), rc)) {
            g.notImplemented(c, "the array reduce result type");
            return "0.0";
        }
        const std::string fold = g.emitArrow(*cb, {rc.cpp, ec.cpp});
        if (c.arguments.size() > 1 && c.arguments[1]) {
            return "::cp3::runtime::arrayReduce<" + rc.cpp + ">(" + recvText +
                   ", " + fold + ", " + g.expr(*c.arguments[1]) + ")";
        }
        // A no-initial fold keeps the element type; a callback that changes it
        // has no lowering yet.
        if (rc.cpp != ec.cpp) {
            g.notImplemented(c, "this no-initial reduce (changing type)");
            return "0.0";
        }
        return "::cp3::runtime::arrayReduceNoInit<" + rc.cpp + ">(" + recvText +
               ", " + fold + ")";
    }
    const char* fn = nullptr;
    if (prop == "filter") fn = "arrayFilter";
    else if (prop == "find") fn = "arrayFind";
    else if (prop == "findIndex") fn = "arrayFindIndex";
    else if (prop == "some") fn = "arraySome";
    else if (prop == "every") fn = "arrayEvery";
    if (fn == nullptr) return std::string();
    return "::cp3::runtime::" + std::string(fn) + "<" + ec.cpp + ">(" +
           recvText + ", " + g.emitArrow(*cb, {ec.cpp}) + ")";
}

// The dispatch: returns "" when the member is not a regular-array method this
// unit lowers (the caller keeps its own CP3002 path).
std::string detail::ArrayEmitter::methodCall(Generator& g, const CallExpr& c,
                                             const MemberExpr& m) {
    if (!m.object) return std::string();
    static const char* const kCallbacks[] = {"map",     "filter", "reduce",
                                             "find",    "findIndex",
                                             "some",    "every"};
    static const char* const kSimple[] = {
        "push", "pop", "unshift", "shift", "unique", "shuffle",
        "indexOf", "includes", "binarySearch", "slice", "flat"};
    const std::string& prop = m.property;
    const bool callback = isOneOf(prop, kCallbacks);
    if (!callback && !isOneOf(prop, kSimple)) {
        return std::string();
    }
    const Type recv = g.effectiveType(*m.object);
    if (recv.kind != TypeKind::Array) return std::string();
    const Type elem = arrayElemOf(recv);
    CppInfo ec;
    if (!arrayElementInfo(elem, ec)) {
        g.notImplemented(c, "the array method element type");
        return "0.0";
    }
    g.needsTypedArray_ = true;
    const std::string recvText = g.familyReceiverText(*m.object, elem, c);
    if (callback) return callbackText(g, c, m, ec, recvText);

    auto arg = [&c, &g](std::size_t i) -> std::string {
        return i < c.arguments.size() && c.arguments[i]
                   ? g.expr(*c.arguments[i])
                   : std::string();
    };
    if (prop == "push" || prop == "unshift") {
        const char* fn = prop == "push" ? "arrayPush" : "arrayUnshift";
        return "::cp3::runtime::" + std::string(fn) + "<" + ec.cpp + ">(" +
               recvText + ", " + arg(0) + ")";
    }
    if (prop == "pop" || prop == "shift") {
        const char* fn = prop == "pop" ? "arrayPop" : "arrayShift";
        return "::cp3::runtime::" + std::string(fn) + "<" + ec.cpp + ">(" +
               recvText + ")";
    }
    if (prop == "unique" || prop == "shuffle") {
        const char* fn = prop == "unique" ? "arrayUnique" : "arrayShuffle";
        return "::cp3::runtime::" + std::string(fn) + "<" + ec.cpp + ">(" +
               recvText + ")";
    }
    if (prop == "indexOf" || prop == "includes" || prop == "binarySearch") {
        const char* fn = prop == "indexOf"   ? "arrayIndexOf"
                         : prop == "includes" ? "arrayIncludes"
                                              : "arrayBinarySearch";
        std::string out = "::cp3::runtime::" + std::string(fn) + "<" + ec.cpp +
                          ">(" + recvText + ", " + arg(0);
        if (c.arguments.size() > 1) out += ", " + arg(1);
        return out + ")";
    }
    if (prop == "slice") {
        std::string out =
            "::cp3::runtime::arraySlice<" + ec.cpp + ">(" + recvText;
        for (const auto& a : c.arguments) {
            out += ", ";
            out += a ? g.expr(*a) : "0.0";
        }
        return out + ")";
    }
    // `flat()`: one level; the receiver's element must itself be an array.
    CppInfo ic;
    if (elem.kind != TypeKind::Array ||
        !arrayElementInfo(arrayElemOf(elem), ic)) {
        g.notImplemented(c, "the array flat element type");
        return "0.0";
    }
    return "::cp3::runtime::arrayFlat<" + ic.cpp + ">(" + recvText + ")";
}

// `for (const x of a)`: over a regular array; the binding takes the element
// type (the alternative `for await` form is out of scope and reports). The
// source handle is bound to a named local before the range-for: iterating
// `*(temporary Ref)` lets the backing vector die at the end of the range-init
// full-expression (a use-after-free seen as std::bad_alloc).
void detail::ArrayEmitter::forOf(Generator& g, const ForOfStatement& f,
                                 const std::string& jsLabel, std::string& out,
                                 int indent) {
    if (g.intOn()) g.intFail();
    if (f.isAwait) g.notImplemented(f, "for await");
    const auto* vd = f.left && f.left->kind == NodeKind::VariableDeclaration
                         ? static_cast<const VariableDeclaration*>(f.left.get())
                         : nullptr;
    const Type elem = f.right ? arrayElemOf(g.effectiveType(*f.right))
                              : Type::unknown();
    CppInfo ec;
    if (!f.right || !arrayElementInfo(elem, ec)) {
        g.notImplemented(f, "this for-of source");
        line(out, indent, "/* CP3002: unsupported for-of source */");
        return;
    }
    g.needsTypedArray_ = true;
    const std::string recv = g.familyReceiverText(*f.right, elem, f);
    const std::string item = "cp3_item" + std::to_string(g.uniqueCounter_++);
    const std::string range = "cp3_range" + std::to_string(g.uniqueCounter_++);
    const std::string breakLabel = g.freshLabel("forof_end_");
    const std::string continueLabel = g.freshLabel("forof_next_");
    // The range expression can itself be a temporary array handle (an array
    // literal builds a fresh `makeArray(...)`): keep the handle in the C++20
    // init-statement so its storage outlives the loop. Iterating the handle
    // directly would read freed storage under `/std:c++20` (the P2718R0
    // lifetime extension is C++23 only).
    g.pushFlowFrame(true, breakLabel, continueLabel, jsLabel);
    g.pushScope();
    line(out, indent,
         "for (auto " + range + " = " + recv + "; " + ec.cpp + " " + item + " : *" +
             range + ") {");
    if (vd != nullptr) {
        const std::string cv = (vd->kind == VarKind::Const) ? "const " : "";
        for (const auto& declPtr : vd->declarations) {
            const VariableDeclarator* d = declPtr.get();
            if (d == nullptr || !d->id) continue;
            if (d->id->kind == NodeKind::Identifier) {
                const std::string name =
                    mapIdent(static_cast<const Identifier&>(*d->id).name);
                std::optional<Type> bt = g.bindingType(*d);
                if (!bt) bt = elem;
                const CppInfo ci = classify(*bt);
                line(out, indent + 1,
                     cv + (ci.representable ? ci.cpp : ec.cpp) + " " + name + " = " +
                         item + ";");
                g.declare(static_cast<const Identifier&>(*d->id).name, bt);
            } else {
                std::optional<Type> bt = g.resolvedBindingType(*d);
                if (!bt) bt = elem;
                PatternEmitter(g).emitBindings(*d->id, item, bt, out, indent + 1);
            }
        }
    } else if (f.left && isPatternNode(*f.left)) {
        PatternEmitter(g).emitBindings(*f.left, item, elem, out, indent + 1);
    } else {
        g.notImplemented(f, "this for-of binding");
        line(out, indent + 1, "/* CP3002: unsupported for-of binding */");
    }
    if (f.body) g.emitBranch(*f.body, out, indent + 1);
    line(out, indent + 1, continueLabel + ": ;");
    line(out, indent, "}");
    g.popScope();
    g.popFlowFrame();
    line(out, indent, breakLabel + ": ;");
}

}  // namespace cp3
