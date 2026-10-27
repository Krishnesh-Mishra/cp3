// =============================================================================
// src/codegen/src/emit/expr.cpp - codegen expression lowering (STYLE.md §8).
//
// The expression emitter: literals, identifiers, templates, the `tsCall`/
// `cond` value-vs-condition adapters, and the operator forms. Logical
// operators use JS truthiness (`cp3_truthy` lambdas); `??` uses a real
// nullish (not truthiness) test. A node with no C++ spelling reports a codegen
// diagnostic and emits a compilable stub (never throw, never abort). Calls
// (including `console.*`)
// live in emit_call.cpp; the async / promise / timer surface in emit_async.cpp.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "detail/generator/cpp_writer.h"
#include "detail/expr/expr_mixed.h"
#include "detail/expr/expr_ops.h"
#include "detail/generator/family.h"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"
#include "cp3/diag/diagnostic_codes.hpp"

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
        case NodeKind::NewExpr:
            return emitNewExprValue(static_cast<const NewExpr&>(e));
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
        case NodeKind::WeakExpr:
        case NodeKind::RangeExpr:
        case NodeKind::SpreadElement:
        default:
            // The expression forms the ordinary table does not own: the weak
            // link wrapper, a range used as a value, a spread outside its
            // legal position.
            return emitExtendedExpr(e);
    }
}

// The expression forms the ordinary table does not own.
std::string Generator::emitExtendedExpr(const Expr& e) {
    switch (e.kind) {
        case NodeKind::WeakExpr:
            return emitWeakValue(static_cast<const WeakExpr&>(e));
        case NodeKind::RangeExpr:
            return emitRangeValue(static_cast<const RangeExpr&>(e));
        case NodeKind::SpreadElement:
            // A spread is only meaningful in an argument list or an array /
            // object literal; every one of those forms handles it before the
            // expression emitter sees it, so reaching this point is a program
            // error.
            reportTypeError(e, "a spread element is only valid in an argument "
                               "list or an array/object literal");
            return "0.0";
        default:
            reportTypeError(e, "unsupported expression");
            return "0.0";
    }
}

// `{ weak: value }` is the weak-link wrapper (spec §11): the value is stored
// as a weak reference, so the link does not add to the target's strong count.
// The link's target type comes from the value (a class instance), so the C++
// spelling is a `WeakRef<T>` built from the strong handle.
std::string Generator::emitWeakValue(const WeakExpr& w) {
    if (!w.value) {
        reportTypeError(w, "a weak link needs a target value");
        return "0.0";
    }
    needsRef_ = true;
    if (isNullishLiteral(*w.value)) {
        // An empty link: the target type can only come from the context (a
        // `T | null` binding or field).
        std::string pointee;
        if (expected_) pointee = weakPointee(stripNullish(*expected_));
        if (pointee.empty()) {
            reportTypeError(w, "a weak link needs a target type");
            return "0.0";
        }
        return "::cp3::runtime::WeakRef<" + pointee + ">(std::nullptr_t{})";
    }
    const Type value = effectiveType(*w.value);
    if (classify(value).shape != Shape::ClassRef) {
        reportTypeError(w, "a weak link needs a class instance");
        return "0.0";
    }
    // The class template argument is deduced from the strong handle.
    return "::cp3::runtime::WeakRef(" + expr(*w.value) + ")";
}

// A range used as a value (`const r = 0..5;`): sema types a range as a
// `number[]`, so the value is the range's elements - end-exclusive, the rule
// the `parallel.*` ranges use (D35).
std::string Generator::emitRangeValue(const RangeExpr& r) {
    if (!r.start || !r.end) {
        reportTypeError(r, "a range needs a start and an end");
        return "0.0";
    }
    needsTypedArray_ = true;
    const std::string values = freshLabel("range_");
    return "[&]{ std::vector<double> " + values +
           "; for (std::int64_t cp3_i = static_cast<std::int64_t>(" +
           expr(*r.start) + "); cp3_i < static_cast<std::int64_t>(" +
           expr(*r.end) + "); ++cp3_i) " + values +
           ".push_back(static_cast<double>(cp3_i)); return "
           "::cp3::runtime::makeArray<double>(std::move(" + values + ")); }()";
}

// A construction: a user class / interface / object model first (spec 15),
// then `new Worker(...)` (D80): a channel handle, not a class/family value.
// Then std/network + std/collections constructors, the builtin
// `new EventEmitter()` (D74), and the JS-compat family constructors;
// anything else reports.
std::string Generator::emitNewExprValue(const NewExpr& n) {
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
    // the use site's diagnostic (the same CP3304 the call path reports).
    if (std::string miss = linkedMemberMiss(m); !miss.empty()) {
        reportCodegenError(*m.object, diagcode::kUnresolvedImport,
                           "unresolved import: " + miss);
        return "0.0";
    }
    // A class / interface / object model member read first (spec 15); then the
    // builtin property reads (`Math.PI`, `path.sep`, `process.argv`,
    // `s.length`) and a std/json record field, then a member read of an object
    // value shape (`o.a`, `o?.a`); a receiver that may be null is a program
    // error, and every other unowned read is a type error at the use site.
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
    // A receiver that may be null needs `?.`: reading it unguarded is a
    // program error in a statically typed language. A receiver whose value
    // shape has no spelling is a type error at the use site.
    const CppInfo recv = m.object ? info(*m.object) : CppInfo{};
    if (recv.shape == Shape::Optional || recv.shape == Shape::Maybe) {
        reportTypeError(m, "the receiver of '" + m.property +
                             "' may be null; use '?.' to read it safely");
        return "0.0";
    }
    reportTypeError(m, "property access '" + m.property +
                           "' has no C++ representation");
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
    reportTypeError(e, "this function value has no callable type");
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
        if (const auto linked = linkedNames_.find(id.name);
            linked != linkedNames_.end()) {
            if (linked->second.cpp.empty()) {
                // The imported name has no C++ lowering (a namespace import, a
                // type-only export): the use site is a program error, the same
                // CP3304 the call path reports.
                reportCodegenError(id, diagcode::kUnresolvedImport,
                                   "unresolved import: " + linked->second.miss);
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
            reportTypeError(id, "a class is a type, not a value");
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
    // an array of those has no formatter either. Reporting the use site keeps
    // the generated C++ compilable instead of failing overload resolution.
    if (!valueFormattable(ci.type)) {
        reportTypeError(e, "this value cannot be formatted");
        return "std::string(\"undefined\")";
    }
    noteFamilyUse(e);
    // An enum value formats by its backing kind (spec 04): the underlying
    // number for a number-backed enum, the member's string for a string-backed
    // one (the per-enum toString helper). An enum whose declaration did not
    // surface in this unit is an internal collection error, not a silent
    // formatter call.
    if (ci.shape == Shape::Enum) {
        const auto found = enums_.find(ci.type.name);
        if (found == enums_.end()) {
            reportCodegenError(
                e, diagcode::kInternalError,
                "internal error: enum '" + ci.type.name +
                    "' was not collected for formatting");
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
        case BinaryOp::In:
            return emitInOperator(b);
    }
    return "false";
}

// `key in container`: the container is a Map (a key lookup), a Set (a value
// lookup), an array or a typed view (an index in range) or a fixed-shape
// object (a known field). The spec bans `for...in` over a plain object (its
// keys are known at compile time), so the object form answers from the shape:
// a literal key that names a field is in the object, any other key is not.
std::string Generator::emitInOperator(const BinaryExpr& b) {
    if (!b.left || !b.right) return "false";
    const Type container = effectiveType(*b.right);
    const CppInfo key = info(*b.left);
    switch (classify(container).shape) {
        case Shape::Collections: {
            // A Map looks the key up, a Set the value.
            if (container.parts.empty()) break;
            const CppInfo element = classify(container.parts.front());
            if (!element.representable || !isPrimitiveShape(element.shape)) break;
            if (key.shape != element.shape) {
                reportTypeError(*b.left,
                                "the 'in' operator needs a key of type '" +
                                    element.cpp + "'");
                return "false";
            }
            needsCollections_ = true;
            return "(" + expr(*b.right) + ")->has(" + expr(*b.left) + ")";
        }
        case Shape::Array:
        case Shape::TypedArray: {
            // An index in range: the array's length is the bound.
            if (key.shape != Shape::Number) {
                reportTypeError(*b.left,
                                "the 'in' operator over an array needs a "
                                "number index");
                return "false";
            }
            noteFamilyUse(*b.right);
            const std::string recv = expr(*b.right);
            const std::string index = expr(*b.left);
            const std::string bound =
                (classify(container).shape == Shape::TypedArray)
                    ? "static_cast<double>(" + recv + "->length)"
                    : "static_cast<double>(" + recv + "->size())";
            return "((" + index + " >= 0.0) && (" + index + " < " + bound +
                   "))";
        }
        case Shape::Object: {
            const std::string answer = emitInObject(b, container);
            if (!answer.empty()) return answer;
            break;
        }
        default:
            break;
    }
    reportTypeError(b, "the 'in' operator needs a Map, Set, array or object on "
                       "the right");
    return "false";
}

// `"name" in object`: a fixed-shape object's fields are known at compile
// time, so the answer is the field's presence. The spec bans `for...in` over
// a plain object (its keys are known), so only a literal key can name one;
// "" when the receiver is not a value shape the emitter knows.
std::string Generator::emitInObject(const BinaryExpr& b, const Type& container) {
    if (b.left->kind != NodeKind::StringLiteral) {
        reportTypeError(*b.left,
                        "the 'in' operator over an object needs a literal key");
        return "false";
    }
    const std::string name = static_cast<const StringLiteral&>(*b.left).value;
    std::string structName;
    std::vector<ObjectField> fields;
    if (!objectShapeOf(container, structName, fields)) return std::string();
    for (const ObjectField& field : fields) {
        if (field.name == name) return "true";
    }
    return "false";
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
        // Mixed operand types: the result is the union of the two operand
        // types, spelled through the value model (detail/expr_mixed.h).
        detail::MixedBranches m;
        m.left = L;
        m.right = R;
        m.whole = info(l);
        m.leftText = expr(*l.left);
        m.rightText = expr(*l.right);
        m.conditionText = "true";
        if (const std::string mixed =
                detail::mixedLogical(m, l.op == LogicalOp::And);
            !mixed.empty()) {
            return mixed;
        }
        reportTypeError(l, "logical operator operands have no common C++ type");
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
            reportTypeError(u, "this unary operator has no C++ lowering");
            return "0.0";
    }
}

std::string Generator::emitUpdate(const UpdateExpr& u) {
    if (u.operand->kind != NodeKind::Identifier) {
        return emitUpdateTarget(u);
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

// The C++ lvalue of a member write target (a field of a value shape or of a
// class) plus the field's cp3 type; false when the member is not a writable
// field (a method, a weak link, a static or an accessor).
bool Generator::memberFieldTarget(const MemberExpr& m, std::string& lvalue,
                                   Type& fieldType) {
    if (!m.object) return false;
    const Type receiver = effectiveType(*m.object);
    // A value shape (`$o{...}`): the field is a member of the generated
    // struct behind the handle.
    Type object;
    bool optional = false;
    if (objectReceiver(receiver, object, optional) && !optional) {
        std::string structName;
        std::vector<ObjectField> fields;
        if (objectShapeOf(object, structName, fields)) {
            for (const ObjectField& field : fields) {
                if (field.name != m.property) continue;
                lvalue = "((" + expr(*m.object) + ")->" +
                         mapIdent(m.property) + ")";
                fieldType = field.type;
                return true;
            }
        }
    }
    // A class instance: the field is a member of the class handle's struct.
    const ClassModel* cm = classModelOf(stripNullish(receiver));
    if (cm == nullptr) return false;
    const ClassMemberModel* member =
        findClassMemberChain(*cm, m.property, nullptr);
    if (member == nullptr || member->isMethod || member->isStatic ||
        member->isWeak) {
        return false;
    }
    lvalue = "((" + expr(*m.object) + ")->" + mapIdent(m.property) + ")";
    fieldType = member->type;
    return true;
}

// `o.a++`, `a[i]++`: the target's value changes by one. The expression yields
// the old value (postfix) or the new one (prefix), and the target is read and
// written exactly once.
std::string Generator::emitUpdateTarget(const UpdateExpr& u) {
    const Expr& operand = *u.operand;
    if (operand.kind == NodeKind::MemberExpr) {
        return emitUpdateMember(u, static_cast<const MemberExpr&>(operand));
    }
    if (operand.kind == NodeKind::IndexExpr) {
        return emitUpdateIndex(u, static_cast<const IndexExpr&>(operand));
    }
    // A call result or a `new` expression: not a value a program can write to
    // (the same rule the assignment target check states).
    reportTypeError(u, "invalid update target: assign to a name, a property "
                       "or an index");
    return "0.0";
}

// `o.a++`: the field of a value shape or of a class.
std::string Generator::emitUpdateMember(const UpdateExpr& u,
                                         const MemberExpr& m) {
    const char* op = u.op == UpdateOp::Increment ? "+" : "-";
    const char* amount = "1";
    std::string lvalue;
    Type fieldType;
    if (!memberFieldTarget(m, lvalue, fieldType)) {
        reportTypeError(u, "cannot update this property");
        return "0.0";
    }
    const CppInfo field = classify(fieldType);
    if (field.shape != Shape::Number && field.shape != Shape::Native) {
        reportTypeError(u, "cannot increment a value that is not a number");
        return "0.0";
    }
    const std::string step = lvalue + " " + op + "= " + amount;
    if (u.prefix) return "(" + step + ")";
    return "([&]{ const auto cp3_old = " + lvalue + "; " + step +
           "; return cp3_old; }())";
}

// `a[i]++`: an element of a regular array or of a typed view.
std::string Generator::emitUpdateIndex(const UpdateExpr& u,
                                        const IndexExpr& ix) {
    const char* op = u.op == UpdateOp::Increment ? "+" : "-";
    const char* amount = "1";
    if (!ix.object || !ix.index) {
        reportTypeError(u, "cannot update this element");
        return "0.0";
    }
    Type receiver = effectiveType(*ix.object);
    if (receiver.isLiteral()) receiver = receiver.literalBaseType();
    const CppInfo shape = classify(receiver);
    const std::string recv = expr(*ix.object);
    const std::string index = indexOperandText(*ix.index);
    // The read and the write, with the receiver and the index bound once.
    std::string read;
    std::string write;
    if (shape.shape == Shape::TypedArray) {
        const char* tag = familyTag(receiver.name);
        if (tag == nullptr) {
            reportTypeError(u, "cannot update this element");
            return "0.0";
        }
        needsTypedArray_ = true;
        const std::string view = "::cp3::runtime::" + std::string(tag);
        read = "::cp3::runtime::typedArrayAtChecked<" + view +
               ">(cp3_recv, cp3_idx)";
        write = "::cp3::runtime::typedArrayWriteAt<" + view +
                ">(cp3_recv, cp3_idx, ";
    } else if (receiver.kind == TypeKind::Array) {
        CppInfo element;
        if (!arrayElementInfo(arrayElemOf(receiver), element)) {
            reportTypeError(u, "cannot update this element");
            return "0.0";
        }
        if (element.shape != Shape::Number && element.shape != Shape::Native) {
            reportTypeError(u, "cannot increment an element that is not a "
                               "number");
            return "0.0";
        }
        needsTypedArray_ = true;
        read = "::cp3::runtime::arrayAt<" + element.cpp +
               ">(cp3_recv, cp3_idx)";
        write = "::cp3::runtime::arrayWriteAt<" + element.cpp +
                ">(cp3_recv, cp3_idx, ";
    } else {
        reportTypeError(u, "cannot update this element");
        return "0.0";
    }
    const std::string head = "([&]{ auto&& cp3_recv = " + recv +
                             "; const double cp3_idx = " + index + "; ";
    const std::string step = std::string("(cp3_old ") + op + " " + amount + ")";
    if (u.prefix) {
        return head + "const auto cp3_old = " + read + "; " + write + step +
               "); return cp3_old " + op + " " + amount + "; }())";
    }
    return head + "const auto cp3_old = " + read + "; " + write + step +
           "); return cp3_old; }())";
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
    // Mixed branches: the result is the union of the two branch types, spelled
    // through the value model (detail/expr_mixed.h).
    detail::MixedBranches m;
    m.left = A;
    m.right = B;
    m.whole = whole;
    m.leftText = a;
    m.rightText = b;
    m.conditionText = cond(*c.condition);
    if (const std::string mixed = detail::mixedConditional(m); !mixed.empty()) {
        return mixed;
    }
    reportTypeError(c, "ternary branches have no common C++ type");
    return "0.0";
}
}  // namespace cp3
