// =============================================================================
// src/std/arrays/codegen_methods.cpp - the regular-array method surface
// (docs/language-spec/09-arrays-strings.md): mutation, callback iteration,
// searching, slicing, the in-place mutators, `for...of` and `for await`.
//
// detail::ArrayEmitter (generator.h) holds the static entry points; Generator
// friends it for the private emitters it drives. The lowering calls the
// runtime extras in cp3/array_extras.hpp with the element type from sema;
// callback methods lower the arrow with the element type as its parameter
// context. A callback may also be a named function (the runtime takes any
// callable), and a type the element or the result has no C++ spelling is
// reported as a type error instead of being lowered.
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
#include "cp3/diagnostic_codes.hpp"

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
    // A named function (or a function-typed binding) is a callback too: the
    // runtime takes any callable, so the function's own C++ signature is
    // used and the element type is not a parameter context.
    const Expr* fn = nullptr;
    if (cb == nullptr) {
        fn = (c.arguments.size() > 0 && c.arguments[0]) ? c.arguments[0].get()
                                                         : nullptr;
        if (fn == nullptr) {
            // No callback at all: sema reports the arity mismatch, and the
            // diagnostic is repeated here so the call never lowers to a
            // runtime call with a missing argument.
            g.reportCodegenError(c, diagcode::kArityMismatch,
                                 "arity mismatch: expected 1, got 0 arguments");
            return "0.0";
        }
    }
    if (prop == "map") {
        // The result array's element type: the callback's return type.
        const Type resultElem = arrayElemOf(g.effectiveType(c));
        CppInfo rc;
        if (!arrayElementInfo(resultElem, rc)) {
            g.reportArrayElementType(resultElem, c, "the result of map()");
            return "0.0";
        }
        const std::string call =
            cb != nullptr ? g.emitArrow(*cb, {ec.cpp}) : g.expr(*fn);
        return "::cp3::runtime::arrayMap<" + rc.cpp + ">(" + recvText + ", " +
               call + ")";
    }
    if (prop == "reduce") {
        // The accumulator parameter has the fold's result type: the initial
        // value's when given, the element's otherwise (a no-initial fold seeds
        // from the first element, JS-style). The accumulator is a plain value
        // (not an array element), so any type with a C++ spelling fits.
        const CppInfo rc = classify(g.effectiveType(c));
        if (!rc.representable || rc.shape == Shape::Void) {
            g.reportTypeError(c, "type mismatch: the result of reduce() has "
                                 "no C++ spelling");
            return "0.0";
        }
        const std::string fold =
            cb != nullptr ? g.emitArrow(*cb, {rc.cpp, ec.cpp}) : g.expr(*fn);
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
    const char* fnName = nullptr;
    if (prop == "filter") fnName = "arrayFilter";
    else if (prop == "find") fnName = "arrayFind";
    else if (prop == "findIndex") fnName = "arrayFindIndex";
    else if (prop == "some") fnName = "arraySome";
    else if (prop == "every") fnName = "arrayEvery";
    if (fnName == nullptr) return std::string();
    const std::string call =
        cb != nullptr ? g.emitArrow(*cb, {ec.cpp}) : g.expr(*fn);
    return "::cp3::runtime::" + std::string(fnName) + "<" + ec.cpp + ">(" +
           recvText + ", " + call + ")";
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
        // The receiver's element type has no C++ spelling: the method cannot
        // be lowered, and the diagnostic says why (the same one the array's
        // own declaration reports).
        g.reportArrayElementType(elem, c, "an array method");
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
    // `flat()`: one level. An array of arrays flattens to the inner element
    // type; an array whose elements are not arrays has nothing to flatten, and
    // JS returns a copy of the receiver.
    if (elem.kind == TypeKind::Array) {
        CppInfo ic;
        if (arrayElementInfo(arrayElemOf(elem), ic)) {
            return "::cp3::runtime::arrayFlat<" + ic.cpp + ">(" + recvText + ")";
        }
        g.reportArrayElementType(arrayElemOf(elem), c, "a flattened array");
        return "0.0";
    }
    return "::cp3::runtime::arrayFromArray<" + ec.cpp + ">(" + recvText + ")";
}

// `for (const x of a)`: over a regular array; the binding takes the element
// type. The source handle is bound to a named local before the range-for:
// iterating `*(temporary Ref)` lets the backing vector die at the end of the
// range-init full-expression (a use-after-free seen as std::bad_alloc).
//
// `awaitValues` is the `for await` form: every bound value is awaited, so
// the binding takes the AWAITED value (a promise resolves to its value, D41)
// and the await needs a coroutine to suspend in.
void detail::ArrayEmitter::forOfImpl(Generator& g, const ForOfStatement& f,
                                     const std::string& jsLabel,
                                     std::string& out, int indent,
                                     bool awaitValues) {    if (g.intOn()) g.intFail();
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
    // The awaited value: a promise resolves to its value, anything else is its
    // own value (D41).
    Type bound = elem;
    if (awaitValues && isPromiseType(elem)) bound = promiseValue(elem);
    const CppInfo bc = classify(bound);
    if (awaitValues && !bc.representable) {
        g.reportTypeError(f, "type mismatch: the awaited value of for await "
                             "has no C++ spelling");
        line(out, indent, "/* for await: unsupported element type */");
        return;
    }
    // The await suspends, so it needs a coroutine (an `async function` body
    // or the top level).
    if (awaitValues && !g.awaitContextOk(f)) {
        line(out, indent, "/* for await: no coroutine to suspend in */");
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
    // The bound value: the element itself, or its awaited form.
    const std::string valueText =
        awaitValues ? "(co_await ::cp3::runtime::awaitValue(" + item + "))"
                    : item;
    if (vd != nullptr) {
        const std::string cv = (vd->kind == VarKind::Const) ? "const " : "";
        for (const auto& declPtr : vd->declarations) {
            const VariableDeclarator* d = declPtr.get();
            if (d == nullptr || !d->id) continue;
            if (d->id->kind == NodeKind::Identifier) {
                const std::string name =
                    mapIdent(static_cast<const Identifier&>(*d->id).name);
                std::optional<Type> bt = g.bindingType(*d);
                if (!bt) bt = bound;
                const CppInfo ci = classify(*bt);
                line(out, indent + 1,
                     cv + (ci.representable ? ci.cpp : bc.cpp) + " " + name +
                         " = " + valueText + ";");
                g.declare(static_cast<const Identifier&>(*d->id).name, bt);
            } else {
                std::optional<Type> bt = g.resolvedBindingType(*d);
                if (!bt) bt = bound;
                PatternEmitter(g).emitBindings(*d->id, valueText, bt, out,
                                               indent + 1);
            }
        }
    } else if (f.left && isPatternNode(*f.left)) {
        PatternEmitter(g).emitBindings(*f.left, valueText, bound, out, indent + 1);
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

void detail::ArrayEmitter::forOf(Generator& g, const ForOfStatement& f,
                                 const std::string& jsLabel, std::string& out,
                                 int indent) {
    forOfImpl(g, f, jsLabel, out, indent, /*awaitValues=*/false);
}

// `for await (const x of a)`: the same loop, with every bound value awaited.
// The await needs a coroutine to suspend in, so the form is only legal inside
// an `async function` or at the top level.
void detail::ArrayEmitter::forAwait(Generator& g, const ForOfStatement& f,
                                    const std::string& jsLabel, std::string& out,
                                    int indent) {
    forOfImpl(g, f, jsLabel, out, indent, /*awaitValues=*/true);
}

}  // namespace cp3
