// =============================================================================
// src/sema/src/checks/call_check.cpp - call inference and overload resolution.
//
// Owns `inferCall` (the call-expression dispatcher), argument arity/type
// checking (`checkArgs`), the per-callable special cases (`JSON.parse`/
// `stringify`, the timer builtins) and overload resolution
// (spec Â§17). The Promise statics live in the sibling promise_call.cpp;
// member/index inference lives in member_check.cpp; primary
// expressions live in expr_check.cpp (STYLE.md Â§8).
// =============================================================================
#include "detail/analyzer.h"

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#include "cp3/core/ast.hpp"
#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/host/native_types.hpp"

namespace cp3 {

// =============================================================================
// Calls
// =============================================================================
Type Analyzer::inferCall(CallExpr& call) {
    Type ct = Type::unknown();
    FuncSig localSig;
    const FuncSig* sig = nullptr;
    if (call.callee && call.callee->kind == NodeKind::Identifier) {
        auto& id = static_cast<Identifier&>(*call.callee);
        if (id.name == "super") return inferSuperCall(call, id);
        Binding b;
        if (!lookup(id.name, b)) {
            reportUndefinedCall(call, id);
            return withType(call, Type::error());
        }
        ct = b.type;
        setType(id, ct);
        if (isAmbientType(b.type)) {
            Type builtin;
            if (tryTimerBuiltinCall(call, id, builtin)) {
                return withType(call, builtin);
            }
            if (tryInferStringBuiltin(call, id, builtin)) {
                return withType(call, builtin);
            }
            if (tryParseBuiltinCall(call, id, builtin)) {
                return withType(call, builtin);
            }
            if (tryInferCoercionBuiltin(call, id, builtin)) {
                return withType(call, builtin);
            }
            if (tryInferBase64Builtin(call, id, builtin)) {
                return withType(call, builtin);
            }
            if (tryInferParseToBuiltin(call, id, builtin)) {
                return withType(call, builtin);
            }
            if (tryInferCloneBuiltin(call, id, builtin)) {
                return withType(call, builtin);
            }
            if (tryInferNetworkCall(call, id, builtin)) {
                return withType(call, builtin);
            }
        }
        if (isErrorClassName(id.name)) {
            if (const ClassInfo* ci = findClass(id.name)) {
                return inferErrorCall(call, id, b, *ci);
            }
        }
        if (b.overloads.size() >= 2) {
            return inferOverloadCall(call, b, id);
        }
        if (b.isFunction) {
            localSig = b.sig;
            sig = &localSig;
        }
    } else if (call.callee) {
        ct = inferExpr(*call.callee);
        // JSON.parse returns `unknown`: the shape is only known at runtime, so
        // callers must narrow before use (spec Â§20).
        if (call.callee->kind == NodeKind::MemberExpr) {
            checkParallelCall(call);
            auto& member = static_cast<MemberExpr&>(*call.callee);
            Type inferred;
            if (tryInferMemberCall(call, member, inferred)) {
                return inferred;
            }
        }
    }

    for (auto& ta : call.typeArgs) if (ta) resolveType(*ta);

    if (ct.isError()) {
        inferArgs(call);
        return withType(call, Type::error());
    }

    if (sig == nullptr && ct.kind == TypeKind::Function) {
        sig = callSig(call, ct, localSig);
    }

    if (sig != nullptr) {
        // A generic function substitutes its type arguments first (spec Â§16).
        if (!sig->typeParams.empty()) return inferGenericCall(call, *sig);
        checkArgs(*sig, call);
        return withType(call, sig->ret);
    }

    // Ambient / unknown callables are permissive.
    if (isAmbientType(ct) || ct.isUnknown()) {
        inferArgs(call);
        return withType(call, ambient());
    }

    report(diagcode::kNotCallable, call.span,
           "value of type '" + pretty(ct) + "' is not callable");
    inferArgs(call);
    return withType(call, Type::error());
}

// The member-call site inference chain (one hook per family, each owning its
// own typing): JSON.parse (unknown), the Promise statics, `env.get<T>`
// (src/std/env/sema.cpp), the std/network generic member calls
// (src/std/network/sema.cpp) and the JS-compat family / Buffer methods.
// Returns true when one of them typed the call and `out` is set.
bool Analyzer::tryInferMemberCall(CallExpr& call, MemberExpr& m, Type& out) {
    // A native binding's own function (`dm.add(...)`): the binding's
    // parameter list is the source of truth (checks/native_check.cpp).
    if (tryInferNativeCall(call, m, out)) return true;
    if (tryInferObjectCall(call, m, out)) return true;
    if (tryInferJsonCall(call, m, out)) return true;
    if (tryInferPromiseStaticCall(call, m, out)) return true;
    if (tryInferCollectionsStaticCall(call, m, out)) return true;
    if (tryInferErrorStaticCall(call, m, out)) return true;
    if (tryInferMathCall(call, m, out)) return true;
    if (tryInferEnvCall(call, m, out)) return true;
    // std/network generics: `res.json<T>()` / `req.json<T>()` (src/std/network/sema.cpp).
    if (tryInferNetworkMemberCall(call, m, out)) return true;
    // The regular-array callback folds (`map` / `reduce`;
    // src/std/arrays/sema_methods.cpp); the family hook owns `fill`.
    if (tryInferArrayCall(call, m, out)) return true;
    if (tryInferFamilyMethodCall(call, m, out)) return true;
    return tryInferBufferCall(call, m, out);
}

// Core timer builtins (spec Â§14, STDLIB docs Â§03): honest result types so
// `let id = setInterval(...)`, `clearTimeout(id)` and `await sleep(...)` type
// correctly (the ambient table is permissive by default). Returns true when
// `id` is one of them and `out` is set.
bool Analyzer::tryTimerBuiltinCall(CallExpr& call, const Identifier& id, Type& out) {
    if (id.name == "setTimeout" || id.name == "setInterval") {
        inferArgs(call);
        out = Type::number();
        return true;
    }
    if (id.name == "sleep") {
        inferArgs(call);
        out = promiseOf(Type::voidType());
        return true;
    }
    if (id.name == "clearTimeout" || id.name == "clearInterval") {
        inferArgs(call);
        out = Type::voidType();
        return true;
    }
    if (id.name == "queueMicrotask" || id.name == "setImmediate") {
        inferArgs(call);
        out = Type::voidType();
        return true;
    }
    return false;
}

// The documented global numeric parsers (STDLIB docs section 3): `parseInt`
// and `parseFloat` both return cp3 `number`, so `let n = parseInt(s)` and
// ternaries over the result type correctly. Codegen lowers the calls to the
// runtime helpers in cp3/core/runtime.hpp.
//
// The documented surface is one argument - the text to parse. Any other
// argument list is a program error (CP3403), not an unimplemented feature: the
// runtime helper takes exactly one string.
bool Analyzer::tryParseBuiltinCall(CallExpr& call, const Identifier& id, Type& out) {
    if (id.name != "parseInt" && id.name != "parseFloat") return false;
    bool hasSpread = false;
    for (auto& a : call.arguments) {
        if (a && a->kind == NodeKind::SpreadElement) hasSpread = true;
    }
    if (hasSpread) {
        report(diagcode::kArityMismatch, call.span,
               id.name + "() takes exactly one argument (the text to parse); a "
                        "spread argument cannot be counted here");
    } else if (call.arguments.size() != 1) {
        const std::size_t given = call.arguments.size();
        report(diagcode::kArityMismatch, call.span,
               "arity mismatch: " + id.name + "() expects exactly 1 argument (the "
               "text to parse), got " + std::to_string(given) +
                   (given == 1 ? " argument" : " arguments"));
    }
    inferArgs(call);
    out = Type::number();
    return true;
}

// `structuredClone(value)`: deep-copies the value (arrays, objects, maps,
// sets, class instances with a cloner). Result type is the argument's type.
bool Analyzer::tryInferCloneBuiltin(CallExpr& call, const Identifier& id,
                                    Type& out) {
    if (id.name != "structuredClone") return false;
    if (call.arguments.empty() || !call.arguments.front()) {
        report(diagcode::kArityMismatch, call.span,
               "arity mismatch: structuredClone expects exactly one argument");
        inferArgs(call);
        out = withType(call, Type::unknown());
        return true;
    }
    out = withType(call, inferExpr(*call.arguments.front()));
    return true;
}

// The `atob` / `btoa` globals: base64 text <-> byte text, both `string`.
// Exactly one argument (the text); any other argument list is CP3403.
bool Analyzer::tryInferBase64Builtin(CallExpr& call, const Identifier& id,
                                     Type& out) {
    if (id.name != "atob" && id.name != "btoa") return false;
    if (call.arguments.size() != 1 || !call.arguments.front()) {
        const std::size_t given = call.arguments.size();
        report(diagcode::kArityMismatch, call.span,
               "arity mismatch: " + id.name + "() expects exactly 1 argument, "
               "got " + std::to_string(given) +
                   (given == 1 ? " argument" : " arguments"));
        inferArgs(call);
        out = Type::unknown();
        return true;
    }
    inferArgs(call);
    out = Type::string();
    return true;
}

// The scalar shapes parseTo converts between: string, number, boolean and the
// native numeric types (a native is already the value type, never a pointer or
// reference, so it has a runtime coercion). Everything compound (arrays,
// objects, classes, unions...) is rejected.
namespace {

bool parseScalarType(const Type& t) {
    const Type base = t.literalBaseType();
    return base.kind == TypeKind::String || base.kind == TypeKind::Number ||
           base.kind == TypeKind::Boolean || isNativeType(base);
}

}  // namespace

// `parseTo(value, keyword)` / `parseTo<Type>(value)`: a bidirectional scalar
// converter (ROADMAP). The target is a static type: the
// `number`/`string`/`boolean` keyword in the two-argument form, or any scalar
// type in the call's type argument (`r_'i32'` cannot name a value, so natives
// take the type-argument form). The source may be any scalar value, so
// `parseTo("123", number)`, `parseTo(n, string)` and
// `parseTo<r_'i64'>(i32Value)` all work. A literal source is still checked
// against the target at compile time (an unrepresentable literal is CP3401);
// a variable source is converted by the runtime coercion helpers.
bool Analyzer::tryInferParseToBuiltin(CallExpr& call, const Identifier& id,
                                      Type& out) {
    if (id.name != "parseTo") return false;
    if (call.arguments.empty()) {
        report(diagcode::kArityMismatch, call.span,
               "arity mismatch: parseTo expects a value and a target type");
        out = withType(call, Type::unknown());
        return true;
    }
    Expr* value = call.arguments.front().get();
    Type target;
    if (!call.typeArgs.empty() && call.typeArgs.front()) {
        target = resolveType(*call.typeArgs.front());
        if (call.arguments.size() != 1) {
            report(diagcode::kArityMismatch, call.span,
                   "arity mismatch: parseTo<T>(value) expects exactly one "
                   "argument (the value)");
            if (value != nullptr) inferExpr(*value);
            out = withType(call, Type::unknown());
            return true;
        }
    } else if (call.arguments.size() == 2 && call.arguments[1] &&
               call.arguments[1]->kind == NodeKind::Identifier) {
        const std::string& keyword =
            static_cast<Identifier&>(*call.arguments[1]).name;
        if (keyword == "number") target = Type::number();
        else if (keyword == "string") target = Type::string();
        else if (keyword == "boolean") target = Type::boolean();
        else {
            report(diagcode::kTypeMismatch, call.arguments[1]->span,
                   "type mismatch: parseTo's second argument must be a static "
                   "type (number, string, boolean) or use the type-argument "
                   "form parseTo<T>(value)");
            if (value != nullptr) inferExpr(*value);
            out = withType(call, Type::unknown());
            return true;
        }
    } else {
        report(diagcode::kTypeMismatch, call.span,
               "type mismatch: parseTo expects parseTo(value, Type) or "
               "parseTo<Type>(value)");
        inferArgs(call);
        out = withType(call, Type::unknown());
        return true;
    }
    // The keyword form's second argument is type syntax, not a value: infer
    // the source only (inferring `number` would report an undefined name).
    const Type source = value != nullptr ? inferExpr(*value) : Type::error();
    if (source.isError()) {
        out = withType(call, Type::error());
        return true;
    }
    if (!parseScalarType(target)) {
        report(diagcode::kTypeMismatch, call.span,
               "type mismatch: parseTo's target must be a scalar type "
               "(number, string, boolean, or a native numeric type), got '" +
                   pretty(target) + "'");
        out = withType(call, Type::unknown());
        return true;
    }
    if (!source.isUnknown() && !parseScalarType(source)) {
        report(diagcode::kTypeMismatch,
               value != nullptr ? value->span : call.span,
               "type mismatch: parseTo's value must be a scalar (string, "
               "number, boolean, or a native numeric type), got '" +
                   pretty(source) + "'");
        out = withType(call, Type::unknown());
        return true;
    }
    // A literal source keeps the compile-time fit check: the runtime coercion
    // sees the same text/value, so an unrepresentable literal is reported now.
    bool fits = true;
    std::string literalText;
    if (value != nullptr && value->kind == NodeKind::StringLiteral) {
        literalText = static_cast<const StringLiteral&>(*value).value;
        if (target.kind == TypeKind::Native) {
            double parsed = 0.0;
            fits = textIsNumber(literalText, parsed) &&
                   nativeCanRepresentLiteral(target, parsed);
        } else if (target.kind == TypeKind::Number) {
            double parsed = 0.0;
            fits = textIsNumber(literalText, parsed);
        } else if (target.kind == TypeKind::Boolean) {
            fits = literalText == "true" || literalText == "false";
        }
    } else if (value != nullptr && value->kind == NodeKind::NumberLiteral &&
               target.kind == TypeKind::Native) {
        fits = nativeCanRepresentLiteral(
            target, static_cast<const NumberLiteral&>(*value).value);
    }
    if (!fits) {
        std::string what = "the value";
        if (value != nullptr && value->kind == NodeKind::StringLiteral) {
            what = "'" + literalText + "'";
        }
        // Point at `parseTo` itself, keeping its real column; the value's
        // line is the same, and the call is what the reader must change.
        reportAt(diagcode::kTypeMismatch, id.span,
                 "type mismatch: " + what + " cannot be parsed to the "
                 "requested target type '" + pretty(target) + "'");
        out = withType(call, Type::unknown());
        return true;
    }
    out = withType(call, target);
    return true;
}

// A full ECMAScript `Number()`-style literal text: the runtime's toNumber
// applies to the same text, so what is accepted here is exactly what parses
// (decimal/exponent, 0x/0b/0o, surrounding whitespace, empty = 0).
bool Analyzer::textIsNumber(const std::string& text, double& value) {
    // Trim ASCII whitespace.
    std::size_t start = 0;
    while (start < text.size() &&
           std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    std::size_t end = text.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    if (start >= end) {
        value = 0.0;
        return true;  // Number("") === 0
    }
    const std::string trimmed = text.substr(start, end - start);
    const char* begin = trimmed.c_str();
    char* finish = nullptr;
    // strtod handles sign, decimal, exponent and 0x; 0b/0o are spelled here.
    if (trimmed.size() > 2 && trimmed[0] == '0' &&
        (trimmed[1] == 'b' || trimmed[1] == 'B' || trimmed[1] == 'o' ||
         trimmed[1] == 'O')) {
        const int base = (trimmed[1] == 'b' || trimmed[1] == 'B') ? 2 : 8;
        unsigned long parsed = std::strtoul(begin + 2, &finish, base);
        if (finish != begin + trimmed.size() || finish == begin + 2) {
            return false;
        }
        value = static_cast<double>(parsed);
        return true;
    }
    value = std::strtod(begin, &finish);
    return finish == begin + trimmed.size() && finish != begin;
}
// The argument is unconstrained (JS converts anything); extra arguments are
// ignored and a spread contributes its first element, exactly like `String`.
bool Analyzer::tryInferCoercionBuiltin(CallExpr& call, const Identifier& id, Type& out) {
    if (id.name != "Number" && id.name != "Boolean" &&
        id.name != "isNaN" && id.name != "isFinite") {
        return false;
    }
    inferArgs(call);
    out = id.name == "Number" ? Type::number() : Type::boolean();
    return true;
}

void Analyzer::reportUndefinedCall(CallExpr& call, Identifier& id) {
    if (id.name == "arguments") {
        reportBannedArguments(id.span);
    } else {
        reportUndefinedName(id.span, id.name);
    }
    inferArgs(call);
}

bool Analyzer::tryInferJsonCall(CallExpr& call, MemberExpr& m, Type& out) {
    if ((m.property == "parse" || m.property == "stringify") && m.object &&
        m.object->kind == NodeKind::Identifier &&
        static_cast<Identifier&>(*m.object).name == "JSON") {
        // JSON.stringify(value, indent?): `unknown` accepts any argument type
        // (M1 cannot represent objects/arrays yet, but must not reject them),
        // and the optional indent is a number. JSON.parse(text) requires a
        // string. Both are enforced in `check` (M2 open bug 9).
        FuncSig sig;
        sig.annotatedReturn = true;
        sig.name = "JSON." + m.property;
        // JSON.parse<T>(...) returns T (explicit) or `unknown` (callers must
        // narrow); JSON.stringify yields a string.
        Type t = Type::unknown();
        if (m.property == "stringify") {
            sig.params = {Type::unknown(), Type::number()};
            sig.optional = {false, true};
            sig.ret = Type::string();
            t = Type::string();
        } else {
            sig.params = {Type::string()};
            sig.optional = {false};
            if (!call.typeArgs.empty() && call.typeArgs.front()) {
                t = resolveType(*call.typeArgs.front());
            }
            sig.ret = t;
        }
        checkArgs(sig, call);
        out = withType(call, t);
        return true;
    }
    return false;
}

void Analyzer::inferArgs(CallExpr& call) {
    for (auto& a : call.arguments) inferArg(a.get());
}

void Analyzer::sigFromFunctionType(const Type& ct, FuncSig& localSig) {
    localSig.name = "";
    // A family marker (`$network:Next`, ...) survives into the signature so
    // the arity report can name the documented surface instead of printing
    // the parameter counts behind the marker's callable shape.
    if (!ct.name.empty() && ct.name.front() == '$') localSig.name = ct.name;
    localSig.annotatedReturn = true;
    if (!ct.parts.empty()) {
        localSig.ret = ct.parts.back();
        for (std::size_t i = 0; i + 1 < ct.parts.size(); ++i) {
            localSig.params.push_back(ct.parts[i]);
            localSig.optional.push_back(false);
        }
    }
    if (ct.name == kVariadicLabel ||
        ct.name == kVariadicOptionalTailLabel) {
        localSig.rest = true;
    }
    if (ct.name == kOptionalTailLabel && !localSig.optional.empty()) {
        localSig.optional.back() = true;
    }
    if (ct.name == kVariadicOptionalTailLabel && !localSig.optional.empty()) {
        localSig.optional.back() = true;
    }
    if (ct.name == kOptionalTail2Label && localSig.optional.size() >= 2) {
        localSig.optional[localSig.optional.size() - 1] = true;
        localSig.optional[localSig.optional.size() - 2] = true;
    }
}

// The signature of the class method a member call names: the class member
// table keeps the rest parameter and the optional parameters, which the
// function type the member read yields does not carry. Null when the
// receiver is not a user class or the member is not a method.
const FuncSig* Analyzer::classMemberSig(const MemberExpr& m) {
    if (!m.object) return nullptr;
    const Type rt = typeOfExpr(*m.object);
    if (rt.kind != TypeKind::Class) return nullptr;
    const ClassInfo* ci = findClass(rt.name);
    if (ci == nullptr) return nullptr;
    const ClassMemberInfo* member = findClassMember(*ci, m.property);
    if (member == nullptr || !member->isMethod) return nullptr;
    return &member->sig;
}

// The element type a spread argument carries, checked against the parameters
// it reaches: a rest parameter takes the elements, a fixed-argument signature
// fills the parameters from the spread's position on.
void Analyzer::checkSpreadArg(const FuncSig& sig, Expr* a, std::size_t index) {
    auto& sp = static_cast<SpreadElement&>(*a);
    if (!sp.argument) return;
    // A spread of an array literal fills the parameters positionally: each
    // element is checked against the parameter it lands on (the same shape the
    // codegen expands). A computed array carries a single element type, which
    // must satisfy every parameter the spread reaches.
    if (sp.argument->kind == NodeKind::ArrayLiteral) {
        // Infer the literal itself first: the codegen reads its recorded
        // element type to spell the array.
        inferExpr(*sp.argument);
        const auto& literal = static_cast<const ArrayLiteral&>(*sp.argument);
        for (std::size_t k = 0; k < literal.elements.size(); ++k) {
            Expr* element = literal.elements[k].get();
            if (element == nullptr ||
                element->kind == NodeKind::SpreadElement) {
                continue;
            }
            const Type et = inferExpr(*element);
            if (sig.rest && !sig.params.empty()) {
                checkAssign(et, sig.params.back(), element->span, false);
                continue;
            }
            const std::size_t j = index + k;
            if (j < sig.params.size() && sig.params[j].kind != TypeKind::Array) {
                checkAssign(et, sig.params[j], element->span, false);
            }
        }
        return;
    }
    const Type at = inferExpr(*sp.argument);
    const Type elem = at.kind == TypeKind::Array && !at.parts.empty()
                          ? at.parts.front()
                          : Type::unknown();
    if (sig.rest && !sig.params.empty()) {
        checkAssign(elem, sig.params.back(), a->span, false);
        return;
    }
    for (std::size_t j = index; j < sig.params.size(); ++j) {
        // A parameter that takes an array is a rest parameter (the signature
        // of a function value carries the array, not its element type).
        if (sig.params[j].kind == TypeKind::Array) continue;
        checkAssign(elem, sig.params[j], a->span, false);
    }
}

// The signature the call's arguments are checked against: a class method
// keeps its rest parameter in the class member table (the function type the
// member read yields does not carry it), everything else comes from the
// function type.
const FuncSig* Analyzer::callSig(const CallExpr& call, const Type& ct,
                                 FuncSig& localSig) {
    if (call.callee && call.callee->kind == NodeKind::MemberExpr) {
        const FuncSig* member =
            classMemberSig(static_cast<const MemberExpr&>(*call.callee));
        if (member != nullptr) return member;
    }
    sigFromFunctionType(ct, localSig);
    return &localSig;
}

void Analyzer::inferArg(Expr* a) {
    if (!a) return;
    if (a->kind == NodeKind::SpreadElement) {
        auto& sp = static_cast<SpreadElement&>(*a);
        if (sp.argument) inferExpr(*sp.argument);
        return;
    }
    inferExpr(*a);
}

void Analyzer::checkArgs(const FuncSig& sig, CallExpr& call) {
    // A type argument list on a callee that declares no type parameters is a
    // type error (the generics unit owns the rule).
    checkTypeArgs(sig, call);
    bool hasSpread = false;
    std::size_t firstSpread = call.arguments.size();
    for (std::size_t i = 0; i < call.arguments.size(); ++i) {
        if (call.arguments[i] && call.arguments[i]->kind == NodeKind::SpreadElement) {
            hasSpread = true;
            firstSpread = i;
            break;
        }
    }
    std::size_t required = 0;
    for (std::size_t i = 0; i < sig.params.size(); ++i) {
        if (i < sig.optional.size() && sig.optional[i]) continue;
        ++required;
    }
    const std::size_t given = call.arguments.size();
    const std::size_t maxArgs =
        sig.rest ? static_cast<std::size_t>(-1) : sig.params.size();
    if (!hasSpread && (given < required || given > maxArgs)) {
        // The middleware continuation is callable with no parameters; an
        // argument is the documented `next()` contract violation, so name it
        // instead of printing "expected 0".
        if (sig.name == "$network:Next") {
            report(diagcode::kArityMismatch, call.span,
                   "arity mismatch: the std/network next() continuation takes "
                   "no arguments, got " +
                       std::to_string(given));
        } else {
            report(diagcode::kArityMismatch, call.span,
                   "arity mismatch: expected " +
                       arityText(required, maxArgs, sig.rest) + ", got " +
                       std::to_string(given) + " argument" +
                       (given == 1 ? "" : "s"));
        }
    }
    if (hasSpread && !sig.rest) {
        // A spread into a fixed-argument signature fills the parameters it
        // reaches positionally: a parameter the spread does not reach is
        // `undefined`, which no cp3 parameter type takes, so the spread must
        // be the last argument (the arguments after it would shift with the
        // spread's length).
        if (firstSpread + 1 != given) {
            report(diagcode::kArityMismatch, call.span,
                   "a spread argument must be the last argument of a call "
                   "with a fixed argument list");
        }
    }
    for (std::size_t i = 0; i < call.arguments.size(); ++i) {
        Expr* a = call.arguments[i].get();
        if (!a) continue;
        if (a->kind == NodeKind::SpreadElement) {
            checkSpreadArg(sig, a, i);
            continue;
        }
        Type pt = Type::unknown();
        if (sig.rest && sig.params.size() > 0 && i >= sig.params.size()) {
            pt = sig.params.back();
        } else if (i < sig.params.size()) {
            pt = sig.params[i];
        }
        // An arrow passed to a function-typed parameter can use the expected
        // signature while its body is checked. This keeps inferred parameter
        // bindings precise inside the arrow (`app.post(..., (req, res) => ...)`).
        const bool contextualArrow = a->kind == NodeKind::ArrowFunction &&
                                     pt.kind == TypeKind::Function &&
                                     a->nodeId != 0;
        if (contextualArrow) contextualArrowTypes_[a->nodeId] = pt;
        Type at = inferExpr(*a);
        if (contextualArrow) contextualArrowTypes_.erase(a->nodeId);
        if (contextualArrow && at.kind == TypeKind::Function &&
            pt.parts.size() >= 1) {
            const auto& arrow = static_cast<const ArrowFunction&>(*a);
            const std::size_t supplied = pt.parts.size() - 1;
            // A callback may ignore parameters the caller supplies, but it
            // cannot require parameters the caller never passes. Keep this
            // check at the extra parameter's span so the editor marks the
            // offending name (`aa`) instead of underlining the whole call.
            for (std::size_t parameterIndex = supplied;
                 parameterIndex < arrow.params.size(); ++parameterIndex) {
                const Parameter* extra = arrow.params[parameterIndex].get();
                if (extra == nullptr || extra->optional || extra->defaultValue ||
                    extra->isRest) {
                    continue;
                }
                const Identifier* name = asIdentifier(extra->name.get());
                const std::string label = name != nullptr && !name->name.empty()
                                              ? " '" + name->name + "'"
                                              : std::string();
                report(diagcode::kTypeMismatch, extra->span,
                       "callback parameter" + label +
                           " has no corresponding argument; the callback "
                           "provides only " + std::to_string(supplied));
            }
        }
        // An explicit `undefined` for an optional/default parameter is the same
        // as omitting the argument (TS accepts it; the default applies), so it
        // is not an assignability error.
        const bool undefinedForOptional =
            a->kind == NodeKind::UndefinedLiteral && i < sig.optional.size() &&
            sig.optional[i];
        if (!undefinedForOptional) {
            checkAssign(at, pt, a->span, false);
        }
    }
}

std::string Analyzer::arityText(std::size_t required, std::size_t maxArgs,
                                bool rest) const {
    if (rest) return "at least " + std::to_string(required);
    if (maxArgs == required) return std::to_string(required);
    return std::to_string(required) + " to " + std::to_string(maxArgs);
}

// =============================================================================
// Overload resolution (spec Â§17)
// =============================================================================
Type Analyzer::inferOverloadCall(CallExpr& call, const Binding& binding,
                                 Identifier& id) {
    std::vector<Type> argTypes;
    bool hasSpread = false;
    collectArgTypes(call, argTypes, hasSpread);
    for (const FuncSig& sig : binding.overloads) {
        if (overloadViable(sig, argTypes, hasSpread)) {
            // A viable overload is accepted permissively (M1 does not yet
            // implement overload-dispatch typing); the ambient type keeps the
            // rest of the file from cascading, exactly as before.
            return withType(call, ambient());
        }
    }
    report(diagcode::kNoMatchingOverload, call.span,
           "no overload of '" + id.name +
               "' matches the supplied arguments");
    return withType(call, Type::error());
}

void Analyzer::collectArgTypes(CallExpr& call, std::vector<Type>& out,
                               bool& hasSpread) {
    out.reserve(call.arguments.size());
    for (auto& a : call.arguments) {
        if (!a) {
            out.push_back(Type::unknown());
            continue;
        }
        if (a->kind == NodeKind::SpreadElement) {
            hasSpread = true;
            auto& sp = static_cast<SpreadElement&>(*a);
            if (sp.argument) inferExpr(*sp.argument);
            out.push_back(Type::unknown());
            continue;
        }
        out.push_back(inferExpr(*a));
    }
}

bool Analyzer::overloadViable(const FuncSig& sig,
                              const std::vector<Type>& argTypes,
                              bool hasSpread) {
    std::size_t required = 0;
    for (std::size_t i = 0; i < sig.params.size(); ++i) {
        if (i < sig.optional.size() && sig.optional[i]) continue;
        ++required;
    }
    const std::size_t given = argTypes.size();
    const std::size_t maxArgs =
        sig.rest ? static_cast<std::size_t>(-1) : sig.params.size();
    if (!hasSpread && (given < required || given > maxArgs)) return false;
    for (std::size_t i = 0; i < argTypes.size(); ++i) {
        Type pt = Type::unknown();
        if (sig.rest && !sig.params.empty() && i >= sig.params.size()) {
            pt = sig.params.back();
        } else if (i < sig.params.size()) {
            pt = sig.params[i];
        }
        if (!wouldAccept(argTypes[i], pt, false)) return false;
    }
    return true;
}

}  // namespace cp3

