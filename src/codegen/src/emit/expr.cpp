// =============================================================================
// src/codegen/src/emit/expr.cpp - codegen expression lowering (STYLE.md §8).
//
// The expression emitter: literals, identifiers, templates, the `tsCall`/
// `cond` value-vs-condition adapters, and the operator forms. Logical
// operators use JS truthiness (`cp3_truthy` lambdas); `??` uses a real
// nullish (not truthiness) test. Out-of-scope nodes report CP3002 and emit a
// compilable stub (never throw, never abort). Calls (including `console.*`)
// live in emit_call.cpp; the async / promise / timer surface in emit_async.cpp.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "detail/cpp_writer.h"
#include "detail/expr_ops.h"
#include "detail/mangle.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

// A whole array in a formatter or truthiness test pulls the arr family block
// (D69), even when this unit never spells the C++ type itself.
void Generator::noteFamilyUse(const Expr& e) {
    const CppInfo ci = info(e);
    if (ci.shape == Shape::Array || ci.shape == Shape::TypedArray) {
        needsTypedArray_ = true;
    }
}

// The expression entry point: inside an int64 fast-path attempt a node whose
// type is `number` is lowered by the fast path (int_mode.cpp); everything else
// uses the ordinary emitter below, whose operands still route through here, so
// a `number` operand of a non-number expression (a truthiness test, a
// comparison, a template hole) is the int64 value.
std::string Generator::expr(const Expr& e) {
    // A call/construction/await/delete or a write outside a statement position
    // is an observable side effect the fast body must not run (the wrapper
    // re-runs the double body after a bail, so it would happen twice).
    if (intOn()) intGuardExpr(e);
    if (intOn() && numberTyped(e)) return intNumberExpr(e);
    return exprPlain(e);
}

// A comparison operand. `x % y` is the one expression whose zero-sign bail is
// droppable here: `imod` exists to reproduce `-0.0`, and a comparison cannot
// tell `-0` from `+0`, so the exact integer remainder is observed identically.
std::string Generator::exprCmp(const Expr& e) {
    if (!intOn() || e.kind != NodeKind::BinaryExpr) return expr(e);
    const auto& b = static_cast<const BinaryExpr&>(e);
    if (b.op != BinaryOp::Modulo || !b.left || !b.right || !numberTyped(*b.left) ||
        !numberTyped(*b.right)) {
        return expr(e);
    }
    const std::string lhs = expr(*b.left);
    const std::string rhs = expr(*b.right);
    // A known non-zero divisor needs no check at all. Anything else keeps the
    // divisor test (`x % 0` must still reach the double body's NaN).
    if (auto k = integralLiteralValue(*b.right); k && *k != 0) {
        return "(" + lhs + " % " + std::to_string(*k) + ")";
    }
    return "::cp3::runtime::arith::imodCmp(" + lhs + ", " + rhs + ")";
}

std::string Generator::exprPlain(const Expr& e) {
    switch (e.kind) {
        case NodeKind::NumberLiteral:
            return numberLiteralText(static_cast<const NumberLiteral&>(e).value);
        case NodeKind::StringLiteral:
            // Emit as a typed std::string: a bare literal would bind to
            // `cp3_ts(bool)`/`cp3_truthy(bool)` and print every literal as "true".
            return "std::string(" +
                   cppStringLiteral(static_cast<const StringLiteral&>(e).value) + ")";
        case NodeKind::BoolLiteral:
            return static_cast<const BoolLiteral&>(e).value ? "true" : "false";
        case NodeKind::NullLiteral:
            return "nullptr";
        case NodeKind::UndefinedLiteral:
            return "cp3::runtime::undefined";
        case NodeKind::RegExpLiteral: return emitRegexLiteral(static_cast<const RegExpLiteral&>(e));
        case NodeKind::Identifier:
            return emitIdentifier(static_cast<const Identifier&>(e));
        case NodeKind::ThisExpr: return "this";
        case NodeKind::TemplateLiteral:
            return emitTemplate(static_cast<const TemplateLiteral&>(e));
        case NodeKind::BinaryExpr:
            return emitBinary(static_cast<const BinaryExpr&>(e));
        case NodeKind::LogicalExpr:
            return emitLogical(static_cast<const LogicalExpr&>(e));
        case NodeKind::UnaryExpr:
            return emitUnary(static_cast<const UnaryExpr&>(e));
        case NodeKind::UpdateExpr:
            return emitUpdate(static_cast<const UpdateExpr&>(e));
        case NodeKind::ConditionalExpr:
            return emitConditional(static_cast<const ConditionalExpr&>(e));
        case NodeKind::AssignmentExpr:
            return emitAssignment(static_cast<const AssignmentExpr&>(e));
        case NodeKind::CallExpr:
            return emitCallOrFamily(static_cast<const CallExpr&>(e));
        case NodeKind::NewExpr: {
            // A user class / interface / object model first (spec 15), then
            // `new Worker(...)` (D80): a channel handle, not a class/family
            // value. Then std/network + std/collections constructors, the
            // builtin `new EventEmitter()` (D74), and the JS-compat family
            // constructors; anything else reports.
            const auto& n = static_cast<const NewExpr&>(e);
            if (std::string model = emitModelNew(n); !model.empty()) {
                return model;
            }
            if (std::string worker = emitWorkerNew(n); !worker.empty()) {
                return worker;
            }
            if (std::string net = emitNetworkNew(n); !net.empty()) {
                return net;
            }
            if (std::string coll = emitCollectionsNew(n); !coll.empty()) return coll;
            if (std::string em = emitEventEmitterNew(n); !em.empty()) return em;
            return emitFamilyNew(n);
        }
        case NodeKind::ArrayLiteral:
            return arrayLiteralText(static_cast<const ArrayLiteral&>(e), Type::unknown());
        case NodeKind::IndexExpr:
            return emitIndexExpr(static_cast<const IndexExpr&>(e));
        case NodeKind::AwaitExpr:
            return emitAwait(static_cast<const AwaitExpr&>(e));
        case NodeKind::MemberExpr:
            return emitMemberRead(static_cast<const MemberExpr&>(e));
        case NodeKind::ObjectLiteral: {
            // A class/interface/structural-object model literal first; else
            // the all-primitive value-struct path (emit/objects.cpp).
            const auto& lit = static_cast<const ObjectLiteral&>(e);
            if (std::string model = emitModelObjectLiteral(lit); !model.empty()) {
                return model;
            }
            return emitObjectLiteral(e);
        }
        case NodeKind::ArrowFunction:
            return emitArrowValue(e, static_cast<const ArrowFunction&>(e));
        default:
            notImplemented(e, "this expression");
            return "0.0";
    }
}

std::string Generator::emitMemberRead(const MemberExpr& m) {
    // A native binding's own member (`dm.Color`, `dm.Color.Red`): the
    // binding's recorded C++ names are the source of truth.
    if (std::string native = emitNativeMember(m); !native.empty()) {
        return native;
    }
    // An imported enum is linked as a type-only export (no C++ binding), but
    // its member surface still lowers: `Color.Green` is `Color::Green` in the
    // importing unit's namespace. Try the enum member before the link-miss
    // report so a known enum is not shadowed by `this type-only export`.
    if (std::string enumMember = emitEnumMember(m); !enumMember.empty()) {
        return enumMember;
    }
    // A property read through a namespace import/re-export: the link miss is
    // the use site's diagnostic.
    if (std::string miss = linkedMemberMiss(m); !miss.empty()) {
        notImplemented(*m.object, miss);
        return "0.0";
    }
    // A class / interface / object model member read first (spec 15); then the
    // builtin property reads (`Math.PI`, `path.sep`, `process.argv`,
    // `s.length`) and a std/json record field, then a member read of an object
    // value shape (`o.a`, `o?.a`); anything else stays CP3002
    // (emit_builtin.cpp, emit/objects.cpp).
    std::string value = emitModelMember(m);
    if (value.empty()) value = emitBuiltinMember(m);
    if (value.empty()) value = emitObject(m);
    if (!value.empty()) return value;
    // A type-parameter receiver (`p.x` where `p: T extends Point2`, spec §16)
    // reads the member off the C++ template parameter. `cp3_member_base`
    // dereferences a Ref<T> class handle and leaves a value struct alone.
    if (m.object && info(*m.object).shape == Shape::TypeParam) {
        return "cp3_member_base(" + expr(*m.object) + ")." +
               mapIdent(m.property);
    }
    notImplemented(m, "this property access");
    return "0.0";
}

// A function value: the context's expected function type first (a binding
// annotation, a call argument, a field), else the arrow's own resolved type. A
// Listener-shaped one is boxed; a plain value-returning one becomes a
// `std::function`.
std::string Generator::emitArrowValue(const Expr& e, const ArrowFunction& a) {
    std::optional<Type> fnType;
    if (expected_ && expected_->kind == TypeKind::Function) {
        fnType = *expected_;
    } else {
        const Type own = effectiveType(a);
        if (own.kind == TypeKind::Function) fnType = own;
    }
    if (fnType && classify(*fnType).shape == Shape::Listener) {
        return emitListenerValue(e);
    }
    if (fnType) return emitArrow(a, {}, /*byValue=*/true);
    notImplemented(e, "this function value");
    return "0.0";
}

std::string Generator::emitIdentifier(const Identifier& id) {
    if (id.name == "undefined") return "cp3::runtime::undefined";
    if (id.name == "null") return "nullptr";
    // D22: the `super` value is the current instance viewed as its base.
    if (id.name == "super") return "*this";
    if (id.name == "NaN") return "std::numeric_limits<double>::quiet_NaN()";
    if (id.name == "Infinity") return "std::numeric_limits<double>::infinity()";
    if (id.name == "true") return "true";
    if (id.name == "false") return "false";
    // A `var` read that precedes its declaration (the binding is hoisted and
    // sema typed the read `undefined`): the C++ declaration does not exist yet,
    // so emit the value the binding holds at that point in JS.
    const auto binding = lookup(id.name);
    if (!binding) {
        // An imported binding of a whole-program build: the target unit's
        // qualified C++ name, or the link miss reported at the use site.
        const auto linked = linkedNames_.find(id.name);
        if (linked != linkedNames_.end()) {
            if (linked->second.cpp.empty()) {
                notImplemented(id, linked->second.miss);
                return "0.0";
            }
            return linked->second.cpp;
        }
        if (const Type* t = sema_.typeOf(id.nodeId); t && t->isUndefined()) {
            return "cp3::runtime::undefined";
        }
        // A class name is a type, not a value: using one as a value (other
        // than as the receiver of a static member) has no C++ spelling.
        if (ModelRegistry::get().classByName(id.name) != nullptr) {
            notImplemented(id, "a class used as a value");
            return "0.0";
        }
        // A module-scope function used as a value (a named function passed as
        // an argument) is namespace-owned like any other global.
        return functions_.count(id.name) != 0 ? moduleRef(id.name)
                                              : mapIdent(id.name);
    }
    // The binding lives in an int64 register; |value| <= 2^53 under the proof,
    // so the conversion to cp3's double `number` is exact.
    if (const std::string text = int64ReadText(id.name); !text.empty()) {
        return text;
    }
    const std::string text =
        binding->global ? moduleRef(id.name) : mapIdent(id.name);
    if (binding->cell) {
        return text + ".get()";
    }
    return text;
}

// console.log/format: route an argument into the runtime formatter. The
// argument's own emitter reports any genuinely unsupported node; this just
// selects the formatter, letting `cp3_ts` overload-resolution pick the type.
std::string Generator::tsCall(const Expr& e) {
    const CppInfo ci = info(e);
    // A class instance or interface wrapper formats only when it exposes a
    // `toString` (the generated overload); a structural object never does, and
    // an array of those has no formatter either. Staying on the CP3002 path
    // here keeps the generated C++ compilable instead of failing overload
    // resolution.
    if (!valueFormattable(ci.type)) {
        notImplemented(e, "formatting this value");
        return "std::string(\"undefined\")";
    }
    noteFamilyUse(e);
    // An enum value formats by its backing kind (spec 04): the underlying
    // number for a number-backed enum, the member's string for a string-backed
    // one (the per-enum toString helper). An enum whose declaration did not
    // surface in this unit keeps the unit a skip instead of emitting a call
    // with no formatter overload.
    if (ci.shape == Shape::Enum) {
        const auto found = enums_.find(ci.type.name);
        if (found == enums_.end()) {
            notImplemented(e, "this enum value");
            return "0.0";
        }
        const std::string value = expr(e);
        if (found->second.stringBacked) {
            return "cp3_ts(cp3_enum_" + found->second.cpp + "_toString(" + value +
                   "))";
        }
        return "cp3_ts(static_cast<double>(" + value + "))";
    }
    // A `T | null` optional prints "null" when empty, whereas a
    // `T | undefined` / `T?` optional prints "undefined".
    if (ci.shape == Shape::Optional && ci.emptyKind == NullishKind::Null) {
        return "cp3_ts_null(" + expr(e) + ")";
    }
    return "cp3_ts(" + expr(e) + ")";
}

std::string Generator::cond(const Expr& e) {
    noteFamilyUse(e);
    if (e.kind == NodeKind::LogicalExpr) {
        const auto& l = static_cast<const LogicalExpr&>(e);
        if (l.op == LogicalOp::And) return "(" + cond(*l.left) + " && " + cond(*l.right) + ")";
        if (l.op == LogicalOp::Or) return "(" + cond(*l.left) + " || " + cond(*l.right) + ")";
    } else if (e.kind == NodeKind::UnaryExpr) {
        const auto& u = static_cast<const UnaryExpr&>(e);
        if (u.op == UnaryOp::Not) return "(! " + cond(*u.operand) + ")";
    }
    return "cp3_truthy(" + expr(e) + ")";
}

std::string Generator::emitBinary(const BinaryExpr& b) {
    // A comparison may lower a `%` operand without the `-0.0` bail (exprCmp).
    const bool cmp = isComparisonOp(b.op);
    const CppInfo L = info(*b.left);
    const CppInfo R = info(*b.right);
    std::string lhs = cmp ? exprCmp(*b.left) : expr(*b.left);
    std::string rhs = cmp ? exprCmp(*b.right) : expr(*b.right);
    // In a native expression an in-range integral literal is an integer
    // token, not a double: `n / 2` must stay integer division, `n % 2` must
    // compile, and `n < 1e15` must compare at full width. A fractional
    // literal keeps its double spelling (the expression is then `number`).
    if (L.shape == Shape::Native || R.shape == Shape::Native) {
        if (const auto k = integralLiteralValue(*b.left); k && !L.type.isNative()) {
            lhs = std::to_string(*k);
        }
        if (const auto k = integralLiteralValue(*b.right);
            k && !R.type.isNative()) {
            rhs = std::to_string(*k);
        }
    }

    if (auto native = nativeBinaryText(b, L, R, lhs, rhs)) return *native;

    switch (b.op) {
        case BinaryOp::Add:
            if (L.shape == Shape::String || R.shape == Shape::String) {
                // D8: only string + string is legal; sema rejects the rest.
                return "(std::string(" + lhs + ") + " + rhs + ")";
            }
            return "(" + lhs + " + " + rhs + ")";
        case BinaryOp::Subtract:
        case BinaryOp::Multiply:
        case BinaryOp::Divide: {
            const char* op = binaryOpSpelling(b.op);
            return "(" + lhs + " " + op + " " + rhs + ")";
        }
        case BinaryOp::Modulo:
            return modCall(lhs, *b.right, rhs);
        case BinaryOp::Power:
            return "std::pow(" + lhs + ", " + rhs + ")";
        case BinaryOp::BitAnd:
            return "::cp3::runtime::bitwise::band(" + lhs + ", " + rhs + ")";
        case BinaryOp::BitOr:
            return "::cp3::runtime::bitwise::bor(" + lhs + ", " + rhs + ")";
        case BinaryOp::BitXor:
            return "::cp3::runtime::bitwise::bxor(" + lhs + ", " + rhs + ")";
        case BinaryOp::ShiftLeft:
            return "::cp3::runtime::bitwise::shl(" + lhs + ", " + rhs + ")";
        case BinaryOp::ShiftRight:
            return "::cp3::runtime::bitwise::shr(" + lhs + ", " + rhs + ")";
        case BinaryOp::UnsignedShiftRight:
            return "::cp3::runtime::bitwise::ushr(" + lhs + ", " + rhs + ")";
        case BinaryOp::LessThan:
        case BinaryOp::GreaterThan:
        case BinaryOp::LessThanEqual:
        case BinaryOp::GreaterThanEqual: {
            const char* op = binaryOpSpelling(b.op);
            if (L.shape == Shape::String || R.shape == Shape::String) {
                return "(std::string(" + lhs + ") " + op + " " + rhs + ")";
            }
            return "(" + lhs + " " + op + " " + rhs + ")";
        }
        case BinaryOp::Equal:
        case BinaryOp::NotEqual:
        case BinaryOp::StrictEqual:
        case BinaryOp::StrictNotEqual:
            return emitEquality(b, L, R, lhs, rhs);
        default:
            notImplemented(b, "this binary operator");
            return "0.0";
    }
}

// Native (`r_'...'` / `c_'...'`) integral operators keep the raw C++ operator:
// `%` stays an integer remainder (never std::fmod), and the bitwise/shift
// forms run at the declared width instead of the ToInt32 number path.
std::optional<std::string> Generator::nativeBinaryText(
    const BinaryExpr& b, const CppInfo& L, const CppInfo& R,
    const std::string& lhs, const std::string& rhs) {
    if (L.shape != Shape::Native && R.shape != Shape::Native) {
        return std::nullopt;
    }
    if (b.op == BinaryOp::Modulo) {
        const bool nativePair = L.shape == Shape::Native &&
                                R.shape == Shape::Native;
        const bool literalPair =
            (L.shape == Shape::Native &&
             integralLiteralValue(*b.right).has_value()) ||
            (R.shape == Shape::Native &&
             integralLiteralValue(*b.left).has_value());
        if (nativePair || literalPair) return "(" + lhs + " % " + rhs + ")";
        return std::nullopt;  // mixed native/number `%`: sema reports CP3406
    }
    const char* op = nullptr;
    switch (b.op) {
        case BinaryOp::BitAnd: op = "&"; break;
        case BinaryOp::BitOr: op = "|"; break;
        case BinaryOp::BitXor: op = "^"; break;
        case BinaryOp::ShiftLeft: op = "<<"; break;
        case BinaryOp::ShiftRight: op = ">>"; break;
        default: return std::nullopt;
    }
    return "(" + lhs + " " + op + " " + rhs + ")";
}

std::string Generator::emitLogical(const LogicalExpr& l) {
    if (l.op == LogicalOp::Nullish) {
        return emitNullish(l);
    }
    const CppInfo L = info(*l.left);
    const CppInfo R = info(*l.right);
    if (!L.representable || !R.representable || L.shape == Shape::Unsupported ||
        R.shape == Shape::Unsupported || L.cpp != R.cpp) {
        // A primitive-union result (`0 || "x"`): each branch is wrapped into
        // the canonical variant so the lambda has a single return type.
        const CppInfo whole = info(l);
        if (whole.representable && whole.shape == Shape::Union &&
            isPrimitiveShape(L.shape) && isPrimitiveShape(R.shape)) {
            const std::string lhs = expr(*l.left);
            const std::string rhs = expr(*l.right);
            const std::string cast = "static_cast<" + whole.cpp + ">";
            if (l.op == LogicalOp::And) {
                return "([&]{ auto cp3_l = (" + cast + "(" + lhs +
                       ")); return cp3_truthy(cp3_l) ? (" + cast + "(" + rhs +
                       ")) : cp3_l; }())";
            }
            return "([&]{ auto cp3_l = (" + cast + "(" + lhs +
                   ")); return cp3_truthy(cp3_l) ? cp3_l : (" + cast + "(" +
                   rhs + ")); }())";
        }
        notImplemented(l, "logical operator with mixed operand types");
        return "0.0";
    }
    const std::string lhs = expr(*l.left);
    const std::string rhs = expr(*l.right);
    if (l.op == LogicalOp::And) {
        return "([&]{ auto cp3_l = (" + lhs + "); return cp3_truthy(cp3_l) ? (" + rhs +
               ") : cp3_l; }())";
    }
    return "([&]{ auto cp3_l = (" + lhs + "); return cp3_truthy(cp3_l) ? cp3_l : (" + rhs +
           "); }())";
}

std::string Generator::emitNullish(const LogicalExpr& l) {
    const CppInfo L = info(*l.left);
    const CppInfo R = info(*l.right);
    const std::string lhs = expr(*l.left);
    const std::string rhs = expr(*l.right);

    // A statically nullish left side (`null ?? x`) never produces a value: the
    // result is the right operand; the left is evaluated only for side effects.
    if (L.shape == Shape::Null || L.shape == Shape::Undefined) {
        return "((" + lhs + "), (" + rhs + "))";
    }
    // `x ?? <nullish>` is `x` (with x evaluated exactly once).
    if (R.shape == Shape::Null || R.shape == Shape::Undefined) {
        return "(" + lhs + ")";
    }
    if (L.shape == Shape::Optional) {
        return "([&]{ auto cp3_l = (" + lhs +
               "); return cp3_l.has_value() ? *cp3_l : (" + rhs + "); }())";
    }
    if (L.shape == Shape::Maybe) {
        return "([&]{ auto cp3_l = (" + lhs +
               "); return cp3_l.hasValue() ? cp3_l.value() : (" + rhs + "); }())";
    }
    return "(" + lhs + ")";
}

std::string Generator::emitUnary(const UnaryExpr& u) {
    switch (u.op) {
        case UnaryOp::Not:
            return "(! " + cond(*u.operand) + ")";
        case UnaryOp::Negate:
            return "(-(" + expr(*u.operand) + "))";
        case UnaryOp::Plus:
            return "(+(" + expr(*u.operand) + "))";
        case UnaryOp::BitNot:
            // Native integers keep `~` (no ToInt32 truncation).
            if (info(*u.operand).shape == Shape::Native) {
                return "(~(" + expr(*u.operand) + "))";
            }
            return "::cp3::runtime::bitwise::bnot(" + expr(*u.operand) + ")";
        case UnaryOp::Void:
            return "((void)(" + expr(*u.operand) + "), cp3::runtime::undefined)";
        case UnaryOp::Typeof:
            return emitTypeof(u);
        default:
            notImplemented(u, "this unary operator");
            return "0.0";
    }
}

std::string Generator::emitUpdate(const UpdateExpr& u) {
    if (u.operand->kind != NodeKind::Identifier) {
        notImplemented(u, "update of a non-identifier target");
        return "0.0";
    }
    const auto& id = static_cast<const Identifier&>(*u.operand);
    const auto binding = lookup(id.name);
    const std::string target = (binding && binding->global)
                                   ? moduleRef(id.name)
                                   : mapIdent(id.name);
    if (binding && binding->cell) {
        // A shared cell: `x++` / `++x` through `set`, keeping the JS result
        // (postfix observes the old value, prefix the new one).
        const char* delta = u.op == UpdateOp::Increment ? "+" : "-";
        std::string out = "([&]{ auto cp3_o = " + target + ".get(); " + target +
                          ".set(cp3_o " + delta + " 1.0); return ";
        out += u.prefix ? (target + ".get();") : "cp3_o;";
        out += " }())";
        return out;
    }
    if (u.op == UpdateOp::Increment) {
        return u.prefix ? "(++" + target + ")" : "(" + target + "++)";
    }
    return u.prefix ? "(--" + target + ")" : "(" + target + "--)";
}

std::string Generator::emitConditional(const ConditionalExpr& c) {
    const CppInfo A = info(*c.whenTrue);
    const CppInfo B = info(*c.whenFalse);
    std::string a = expr(*c.whenTrue);
    std::string b = expr(*c.whenFalse);
    if (A.representable && B.representable && A.cpp == B.cpp) {
        return "(" + cond(*c.condition) + " ? " + a + " : " + b + ")";
    }
    // Mixed branches: allow nullish folding into an optional result.
    if (A.shape == Shape::Optional && isNullishLiteral(*c.whenFalse)) {
        return "(" + cond(*c.condition) + " ? " + a + " : std::nullopt)";
    }
    if (B.shape == Shape::Optional && isNullishLiteral(*c.whenTrue)) {
        return "(" + cond(*c.condition) + " ? std::nullopt : " + b + ")";
    }
    // `cond ? nativeValue : undefined|null` (D65): the result is the native
    // type's `std::optional`; wrap the value branch and fold the other.
    const CppInfo whole = info(c);
    if (whole.shape == Shape::Optional) {
        if (A.shape == Shape::Native && isNullishLiteral(*c.whenFalse)) {
            return "(" + cond(*c.condition) + " ? std::optional<" + A.cpp +
                   ">(" + a + ") : std::nullopt)";
        }
        if (B.shape == Shape::Native && isNullishLiteral(*c.whenTrue)) {
            return "(" + cond(*c.condition) + " ? std::nullopt : std::optional<" +
                   B.cpp + ">(" + b + "))";
        }
    }
    notImplemented(c, "ternary with non-uniform branch types");
    return "0.0";
}

}  // namespace cp3
