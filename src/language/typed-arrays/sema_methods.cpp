// =============================================================================
// src/language/typed-arrays/sema_methods.cpp - the JS-compat typed-array family
// (D69), the member surface.
//
// The `Array.of` / `Array.from` statics, the `arr.fill(...)` special form,
// the member tables for the 11 typed classes, the buffer classes and
// DataView, and the read-only member guard. The constructor grammar, the
// shared helpers and the indexed-read / declarator checks live in
// sema.cpp.
// =============================================================================
#include "detail/analyzer.h"

#include <string>
#include <utility>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/host/std_integer.hpp"

namespace cp3 {

// =============================================================================
// `Array.of` / `Array.from`
// =============================================================================
bool Analyzer::tryInferArrayStaticCall(CallExpr& call, MemberExpr& m,
                                       Type& out) {
    if (!m.object || m.object->kind != NodeKind::Identifier ||
        static_cast<Identifier&>(*m.object).name != "Array") {
        return false;
    }
    if (m.property == "of") {
        Type elem = Type::unknown();
        bool first = true;
        for (auto& a : call.arguments) {
            Type at = Type::unknown();
            if (a && a->kind == NodeKind::SpreadElement) {
                auto& sp = static_cast<SpreadElement&>(*a);
                at = sp.argument ? inferExpr(*sp.argument) : Type::unknown();
            } else if (a) {
                at = inferExpr(*a);
            }
            elem = first ? widen(at) : unify(elem, widen(at));
            first = false;
        }
        out = withType(call, Type::array(elem));
        return true;
    }
    if (m.property == "isArray") {
        // `Array.isArray(x)` — a compile-time-constant `boolean`: true when
        // the argument's static type is an array (typed views return false,
        // matching the runtime's separate typed-array classes).
        out = withType(call, Type::boolean());
        return true;
    }
    if (m.property == "from") {
        if (call.arguments.size() != 1 || !call.arguments.front()) {
            report(diagcode::kArityMismatch, call.span,
                   "arity mismatch: expected 1, got " +
                       std::to_string(call.arguments.size()) + " arguments");
            out = withType(call, Type::error());
            return true;
        }
        const Type at = inferExpr(*call.arguments.front());
        if (at.kind == TypeKind::Array) {
            out = withType(call, Type::array(elemOf(at)));
            return true;
        }
        if (isTypedArrayClass(at)) {
            out = withType(call, Type::array(Type::number()));
            return true;
        }
        if (isPermissive(at)) {
            out = withType(call, Type::array(Type::unknown()));
            return true;
        }
        if (isStringish(at)) {
            report(diagcode::kTypeMismatch, call.span,
                   "type mismatch: Array.from over a general iterable is not "
                   "supported yet (pass an array or a typed array)");
            out = withType(call, Type::error());
            return true;
        }
        report(diagcode::kTypeMismatch, call.span,
               "type mismatch: cannot copy '" + pretty(at) +
                   "' into an array");
        out = withType(call, Type::error());
        return true;
    }
    return false;
}

// `arr.fill(value, start?, end?)`. The element type follows the receiver: for
// `Array(n)` (element unknown) the value fixes it; for a typed receiver the
// value must be a number.
bool Analyzer::tryInferFillCall(CallExpr& call, MemberExpr& m, Type& out) {
    if (m.property != "fill" || !m.object) return false;
    const Type ot = typeOfExpr(*m.object);
    if (ot.kind != TypeKind::Array && !isTypedArrayClass(ot)) return false;
    const Type elem = ot.kind == TypeKind::Array ? elemOf(ot) : Type::number();
    // `start`/`end` are signed relative indices (negatives count from the
    // end); the filled value stays the element type (`number` for a view).
    const Type idx =
        stdIntegerType(StdIntegerKind::SignedIndex, options_.typeSystem);
    FuncSig sig;
    sig.name = "fill";
    sig.params = {elem, idx, idx};
    sig.optional = std::vector<bool>{false, true, true};
    sig.ret = ot;
    checkArgs(sig, call);
    if (ot.kind == TypeKind::Array && elem.isUnknown()) {
        const Type value = call.arguments.empty() || !call.arguments.front()
                               ? Type::unknown()
                               : widen(typeOfExpr(*call.arguments.front()));
        out = withType(call, Type::array(value));
        return true;
    }
    out = withType(call, ot);
    return true;
}

// The member-side family hooks `inferCall` wires in: the `Array.of/from`
// statics first, then the `arr.fill(...)` special form.
bool Analyzer::tryInferFamilyMethodCall(CallExpr& call, MemberExpr& member,
                                        Type& out) {
    if (tryInferArrayStaticCall(call, member, out)) return true;
    return tryInferFillCall(call, member, out);
}

// `inferCall`, but the `new`-less family constructor spelling types like
// `new` first (`Array(2)`, `Array(1, 2)`, `Float32Array([1])`); an undefined
// or shadowed name still reaches `inferCall` with its own diagnostics.
Type Analyzer::inferCallOrFamily(CallExpr& call) {
    if (call.callee && call.callee->kind == NodeKind::Identifier) {
        auto& id = static_cast<Identifier&>(*call.callee);
        Binding b;
        if (isFamilyCtorName(id.name) && lookup(id.name, b) &&
            isAmbientType(b.type)) {
            setType(id, b.type);
            return withType(call,
                            inferFamilyConstruct(id.name, call.typeArgs,
                                                 call.arguments, call.span));
        }
    }
    return inferCall(call);
}

// =============================================================================
// Member tables (the JS surface the corpus pins)
// =============================================================================
Type Analyzer::typedArrayMember(const Type& recv, const std::string& prop) {
    // Width pattern: `length`/`byteLength`/`byteOffset` are the std length
    // width (`usize`/`size_t`, canonical `u64`); `BYTES_PER_ELEMENT` is a
    // byte count (the std count width). Relative index parameters are signed
    // indices (negatives count from the end), except `set`'s offset which
    // stays signed so a negative still reaches the runtime RangeError.
    // Element values (`indexOf` search value, `set` source, `fill` value)
    // stay `number`: they are typed-array elements, not std integers.
    const Type len = stdIntegerType(StdIntegerKind::Length, options_.typeSystem);
    const Type idx =
        stdIntegerType(StdIntegerKind::SignedIndex, options_.typeSystem);
    if (prop == "length" || prop == "byteLength" || prop == "byteOffset") {
        return len;
    }
    if (prop == "BYTES_PER_ELEMENT") {
        return stdIntegerType(StdIntegerKind::Count, options_.typeSystem);
    }
    if (prop == "buffer") return Type::named(TypeKind::Class, "ArrayBuffer");
    if (prop == "subarray" || prop == "slice") {
        return withOptionalTail2(Type::function({idx, idx}, recv));
    }
    if (prop == "copyWithin") {
        return withOptionalTail2(Type::function({idx, idx, idx}, recv));
    }
    if (prop == "reverse") return Type::function({}, recv);
    if (prop == "indexOf") {
        return withOptionalTail(Type::function({Type::number(), idx}, idx));
    }
    if (prop == "includes") {
        return withOptionalTail(
            Type::function({Type::number(), idx}, Type::boolean()));
    }
    if (prop == "join") {
        return withOptionalTail(
            Type::function({Type::string()}, Type::string()));
    }
    if (prop == "set") {
        return withOptionalTail(
            Type::function({Type::unknown(), idx}, Type::voidType()));
    }
    if (prop == "fill") {
        return withOptionalTail2(
            Type::function({Type::number(), idx, idx}, recv));
    }
    return ambient();
}

Type Analyzer::bufferMember(const Type& recv, const std::string& prop) {
    if (prop == "byteLength") {
        return stdIntegerType(StdIntegerKind::Length, options_.typeSystem);
    }
    if (prop == "slice") {
        const Type idx =
            stdIntegerType(StdIntegerKind::SignedIndex, options_.typeSystem);
        return withOptionalTail2(Type::function({idx, idx}, recv));
    }
    return ambient();
}

Type Analyzer::dataViewMember(const std::string& prop) {
    if (prop == "buffer") return Type::named(TypeKind::Class, "ArrayBuffer");
    // `byteOffset` is a non-negative offset (the std index width) and
    // `byteLength` a length; the `get*`/`set*` offset stays signed so a
    // negative still reaches the runtime RangeError. The accessed values
    // stay `number` (the data width, not a std integer).
    if (prop == "byteOffset") {
        return stdIntegerType(StdIntegerKind::Index, options_.typeSystem);
    }
    if (prop == "byteLength") {
        return stdIntegerType(StdIntegerKind::Length, options_.typeSystem);
    }
    const Type idx =
        stdIntegerType(StdIntegerKind::SignedIndex, options_.typeSystem);
    if (prop.rfind("get", 0) == 0) {
        return withOptionalTail(
            Type::function({idx, Type::boolean()}, Type::number()));
    }
    if (prop.rfind("set", 0) == 0) {
        return withOptionalTail(
            Type::function({idx, Type::number(), Type::boolean()},
                           Type::voidType()));
    }
    return ambient();
}

bool Analyzer::isReadOnlyFamilyProp(const Type& ot,
                                    const std::string& prop) const {
    if (isTypedArrayClass(ot)) {
        return prop == "length" || prop == "byteLength" ||
               prop == "byteOffset" || prop == "buffer";
    }
    if (isBufferClass(ot)) return prop == "byteLength";
    if (isDataViewClass(ot)) {
        return prop == "buffer" || prop == "byteOffset" || prop == "byteLength";
    }
    return false;
}

}  // namespace cp3
