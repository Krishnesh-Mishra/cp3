// =============================================================================
// src/codegen/src/emit/assignment.cpp - codegen assignment lowering
// (STYLE.md §8).
//
// The `AssignmentExpr` forms kept out of emit/expr.cpp:
//
//   * the compound arithmetic writes (`+=`, `-=`, `*=`, `/=`, `%=`, `**=`);
//   * the bitwise/shift writes (`&=`, `|=`, `^=`, `<<=`, `>>=`, `>>>=`), which
//     go through cp3/core/bitwise.hpp like their binary forms;
//   * the short-circuit writes (`&&=`, `||=`, `??=`): the ternaries reproduce
//     both the assigned value and the skipped right-hand evaluation.
//
// `emitBinary`/`emitUpdate` stay in emit/expr.cpp; the `IndexExpr` target keeps
// its family write path (src/std/typed-arrays/codegen_methods.cpp).
// =============================================================================
#include "detail/generator/generator.h"

#include <optional>
#include <string>

#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/host/native_types.hpp"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// `x += y` and the other arithmetic compounds.
std::string arithAssignText(AssignOp op, const std::string& target,
                            const std::string& value) {
    const char* text = nullptr;
    switch (op) {
        case AssignOp::Add: text = "+="; break;
        case AssignOp::Subtract: text = "-="; break;
        case AssignOp::Multiply: text = "*="; break;
        default: text = "/="; break;
    }
    return "(" + target + " " + text + " " + value + ")";
}

// The cp3/core/bitwise.hpp entry point of a compound write, or nullptr when
// `op` is not a bitwise/shift one.
const char* bitwiseHelper(AssignOp op) {
    switch (op) {
        case AssignOp::BitAnd: return "band";
        case AssignOp::BitOr: return "bor";
        case AssignOp::BitXor: return "bxor";
        case AssignOp::ShiftLeft: return "shl";
        case AssignOp::ShiftRight: return "shr";
        case AssignOp::UnsignedShiftRight: return "ushr";
        default: return nullptr;
    }
}

// `x &&= y` / `x ||= y`: the ternary assigns the right operand only when the
// short circuit demands it.
std::string logicalAssignText(AssignOp op, const std::string& target,
                              const std::string& value) {
    if (op == AssignOp::LogicalAnd) {
        return "(" + target + " = cp3_truthy(" + target + ") ? (" + value +
               ") : (" + target + "))";
    }
    return "(" + target + " = cp3_truthy(" + target + ") ? (" + target +
           ") : (" + value + "))";
}

// `x ??= y`. nullopt when the target shape has no spelling here (a Maybe
// target with a nullish right operand; a non-value target). The `decltype`
// re-spells the target's own C++ type so the assignment stays exact.
std::optional<std::string> nullishAssignText(const CppInfo& T, const Expr& rhs,
                                             const std::string& target,
                                             const std::string& value) {
    const bool nullishRhs = isNullishLiteral(rhs);
    if (T.shape == Shape::Optional) {
        const std::string rhsText = nullishRhs ? "std::nullopt" : value;
        return "(" + target + " = " + target + ".has_value() ? " + target +
               " : decltype(" + target + ")(" + rhsText + "))";
    }
    if (T.shape == Shape::Maybe) {
        // A `Maybe<T>` (the three-way nullish union) keeps its flavour: the
        // right operand builds the new flavour (`Maybe<T>(nullptr)` for
        // `null`).
        return "(" + target + " = " + target + ".hasValue() ? " + target +
               " : decltype(" + target + ")(" + value + "))";
    }
    if (isPrimitiveShape(T.shape) || T.shape == Shape::Union) {
        // The target can never be nullish: the right operand is not evaluated
        // at all (JS).
        return "(" + target + ")";
    }
    // Every other shape is never nullish either (an array, a class handle, an
    // object, a function, a buffer, ...), so the write keeps the target's
    // value and the right operand is not evaluated. The binary `??` operator
    // lowers the same way (emit/expr.cpp's `emitNullish`).
    if (!T.type.isUnknown() && !T.type.isError() && !T.type.isNullable() &&
        !T.type.isNullish()) {
        return "(" + target + ")";
    }
    return std::nullopt;
}

// The C++ compound operator a native integer target keeps (full width,
// integer `%`); nullptr for every other operator.
const char* nativeCompoundOp(AssignOp op) {
    switch (op) {
        case AssignOp::Modulo: return "%";
        case AssignOp::BitAnd: return "&";
        case AssignOp::BitOr: return "|";
        case AssignOp::BitXor: return "^";
        case AssignOp::ShiftLeft: return "<<";
        case AssignOp::ShiftRight: return ">>";
        default: return nullptr;
    }
}

// The checked native-integer helper for a compound write, or nullptr.
const char* checkedCompoundName(AssignOp op) {
    switch (op) {
        case AssignOp::Add: return "add";
        case AssignOp::Subtract: return "sub";
        case AssignOp::Multiply: return "mul";
        case AssignOp::Divide: return "div";
        case AssignOp::Modulo: return "mod";
        default: return nullptr;
    }
}

}  // namespace

namespace detail {

// The identifier-target half of `emitAssignment`: a shared `Cell<T>`, a
// module global (qualified through `moduleRef`), or a plain local. It lives on
// a friend helper so the dispatch function stays within the function budget.
struct AssignEmitter {
    explicit AssignEmitter(Generator& generator) : g(generator) {}

    Generator& g;

    std::string identifier(const AssignmentExpr& a, const Identifier& id);
    // The one proven R2 write, in the int64 register (empty when this
    // assignment is not it).
    std::string int64WriteText(const AssignmentExpr& a);
    // A compound write to a shared cell: the cell's value is read, combined
    // and written back. Every operator has a cell form.
    std::string cell(AssignOp op, const CppInfo& T, const std::string& target,
                     const std::string& value, const Expr& rhs);
    // The arithmetic/bitwise half of a cell write.
    std::string cellValue(AssignOp op, const CppInfo& T,
                          const std::string& target, const std::string& value,
                          const Expr& rhs);
};

// A compound write to a shared cell: the cell's own value is read, combined
// and written back, so the enclosing frame and every closure see the new
// value. Every operator is covered (the arithmetic ones, the modulo, the
// power, the bitwise/shift ones, the short-circuit ones and the nullish one).
std::string AssignEmitter::cell(AssignOp op, const CppInfo& T,
                                const std::string& target,
                                const std::string& value, const Expr& rhs) {
    const std::string current = target + ".get()";
    switch (op) {
        case AssignOp::Assign:
            return "(" + target + ".set(" + value + "), " + current + ")";
        case AssignOp::LogicalAnd:
        case AssignOp::LogicalOr:
            // The short circuit: the right operand is written only when the
            // target is falsy (`&&=`) or truthy (`||=`).
            return "(" + target + ".set(cp3_truthy(" + current + ") ? (" +
                   value + ") : (" + current + ")), " + current + ")";
        case AssignOp::Nullish: {
            const bool nullishRhs = isNullishLiteral(rhs);
            if (T.shape == Shape::Optional) {
                const std::string rhsText = nullishRhs ? "std::nullopt" : value;
                return "(" + target + ".set(" + current + ".has_value() ? " +
                       current + " : " + rhsText + "), " + current + ")";
            }
            if (T.shape == Shape::Maybe) {
                // A `Maybe<T>` keeps its flavour: the right operand builds the
                // new one (`Maybe<T>(nullptr)` for `null`).
                return "(" + target + ".set(" + current + ".hasValue() ? " +
                       current + " : " + value + "), " + current + ")";
            }
            // A target that is never nullish keeps its value; the right
            // operand is not evaluated at all (JS).
            return current;
        }
        default:
            return cellValue(op, T, target, value, rhs);
    }
}

// The arithmetic and bitwise half of a cell write: the cell's value is
// combined with the assigned one and written back.
std::string AssignEmitter::cellValue(AssignOp op, const CppInfo& T,
                                     const std::string& target,
                                     const std::string& value, const Expr& rhs) {
    const std::string current = target + ".get()";
    if (T.shape == Shape::Native && isNativeInteger(T.type)) {
        if (const char* fn = checkedCompoundName(op); fn != nullptr) {
            return "(" + target + ".set(::cp3::runtime::checked::" + fn +
                   "<" + T.cpp + ">(" + current + ", " + value + ")), " +
                   current + ")";
        }
    }
    switch (op) {
        case AssignOp::Add:
        case AssignOp::Subtract:
        case AssignOp::Multiply:
        case AssignOp::Divide: {
            const char* text = nullptr;
            switch (op) {
                case AssignOp::Add: text = "+="; break;
                case AssignOp::Subtract: text = "-="; break;
                case AssignOp::Multiply: text = "*="; break;
                default: text = "/="; break;
            }
            return "(" + target + ".set(" + current + " " + text + " " + value +
                   "), " + current + ")";
        }
        case AssignOp::Modulo:
            return "(" + target + ".set(" + g.modCall(current, rhs, value) +
                   "), " + current + ")";
        case AssignOp::Power:
            return "(" + target + ".set(std::pow(" + current + ", " + value +
                   ")), " + current + ")";
        case AssignOp::BitAnd:
        case AssignOp::BitOr:
        case AssignOp::BitXor:
        case AssignOp::ShiftLeft:
        case AssignOp::ShiftRight:
        case AssignOp::UnsignedShiftRight: {
            // The bitwise/shift writes keep the ECMAScript ToInt32 semantics
            // of their binary forms (cp3/core/bitwise.hpp).
            const char* helper = bitwiseHelper(op);
            return "(" + target + ".set(::cp3::runtime::bitwise::" + helper +
                   "(" + current + ", " + value + ")), " + current + ")";
        }
        default:
            return "(" + target + ".set(" + value + "), " + current + ")";
    }
}

std::string AssignEmitter::int64WriteText(const AssignmentExpr& a) {
    const auto it = g.int64Writes_.find(a.nodeId);
    if (it == g.int64Writes_.end()) return std::string();
    // The one proven R2 write, in the int64 register (the product and
    // remainder are exact because the value stays below 2^53). The cast keeps
    // the expression's C++ type the `double` the original assignment had, so
    // a nested write hands back the same value in the same type as before: the
    // formatter and the numeric operators see a `number`, not an int64.
    const TopInt64Binding& b = *it->second;
    return "(static_cast<double>(" + b.alias + " = " + b.alias +
           " * std::int64_t(" + std::to_string(b.mul) + ") % std::int64_t(" +
           std::to_string(b.mod) + ")))";
}

std::string AssignEmitter::identifier(const AssignmentExpr& a,
                                      const Identifier& id) {
    const auto binding = g.lookup(id.name);
    const std::string target =
        (binding && binding->global) ? g.moduleRef(id.name) : mapIdent(id.name);
    if (const std::string r2 = int64WriteText(a); !r2.empty()) return r2;
    const CppInfo T = g.info(*a.target);
    // The assigned value is emitted against the target's type (an object
    // literal assigned to a fresh field, a lambda stored in a function slot).
    const std::optional<Type> savedExpected = g.expected_;
    if (binding) g.expected_ = binding->type;
    std::string value = g.expr(*a.value);
    g.expected_ = savedExpected;
    if (binding && binding->cell) {
        // A shared cell: `x = v` is `(x.set(v), x.get())`, so the assignment
        // expression still evaluates to the assigned value. Every compound
        // operator has a cell form.
        return cell(a.op, T, target, value, *a.value);
    }
    // A native target takes an in-range integral literal as an integer token.
    if (T.shape == Shape::Native) {
        if (const auto k = integralLiteralValue(*a.value)) value = std::to_string(*k);
    }

    // Native integer arithmetic is checked (overflow/division by zero throw).
    if (T.shape == Shape::Native && isNativeInteger(T.type)) {
        if (const char* fn = checkedCompoundName(a.op); fn != nullptr) {
            return "(" + target + " = ::cp3::runtime::checked::" + fn + "<" +
                   T.cpp + ">(" + target + ", " + value + "))";
        }
    }
    // A native integer keeps the C++ compound operator for the bitwise/shift
    // forms; the helpers below are the ToInt32 `number` path.
    if (T.shape == Shape::Native) {
        if (const char* op = nativeCompoundOp(a.op); op != nullptr) {
            return "(" + target + " " + op + "= " + value + ")";
        }
    }
    if (const char* helper = bitwiseHelper(a.op)) {
        // ECMAScript ToInt32 on both operands, a double result.
        return "(" + target + " = ::cp3::runtime::bitwise::" + helper + "(" +
               target + ", " + value + "))";
    }
    switch (a.op) {
        case AssignOp::Assign:
            if (T.shape == Shape::Optional && isNullishLiteral(*a.value)) {
                return "(" + target + " = std::nullopt)";
            }
            return "(" + target + " = " + value + ")";
        case AssignOp::Add:
        case AssignOp::Subtract:
        case AssignOp::Multiply:
        case AssignOp::Divide:
            return arithAssignText(a.op, target, value);
        case AssignOp::Modulo:
            return "(" + target + " = " + g.modCall(target, *a.value, value) + ")";
        case AssignOp::Power:
            return "(" + target + " = std::pow(" + target + ", " + value + "))";
        case AssignOp::LogicalAnd:
        case AssignOp::LogicalOr:
            if (T.representable) {
                return logicalAssignText(a.op, target, value);
            }
            break;
        case AssignOp::Nullish:
            if (auto text = nullishAssignText(T, *a.value, target, value)) {
                return *text;
            }
            break;
        default:
            break;
    }
    g.reportTypeError(a, "this compound assignment operator has no C++ lowering");
    return "0.0";
}

}  // namespace detail

std::string Generator::emitAssignment(const AssignmentExpr& a) {
    if (a.target->kind == NodeKind::MemberExpr) {
        const auto& member = static_cast<const MemberExpr&>(*a.target);
        // A class/interface/structural-object field write (spec 15).
        if (std::string w = emitModelAssign(a, member); !w.empty()) {
            return w;
        }
        // A structural object's field write (`h.onEvent = f`, `box.v = 1`).
        if (std::string w = emitMemberAssign(a, member); !w.empty()) {
            return w;
        }
        reportCodegenError(
            a, diagcode::kInvalidAssignmentTarget,
            "invalid assignment target: this target has no C++ write form");
        return "0.0";
    }
    if (a.target->kind != NodeKind::Identifier) {
        // Array/typed-view element writes (`a[1] = 9`, `u8[0] = 5`) lower to
        // the family's checked writes first
        // (src/std/typed-arrays/codegen_methods.cpp).
        if (a.target->kind == NodeKind::IndexExpr) {
            if (std::string w = emitFamilyIndexWrite(a); !w.empty()) return w;
            // A tuple slot (`t[0] = 2`): std::get<i> is the lvalue.
            const auto& ix = static_cast<const IndexExpr&>(*a.target);
            if (a.op == AssignOp::Assign && a.value && ix.object && ix.index) {
                const Type tuple = effectiveType(*ix.object);
                if (isTupleType(tuple)) {
                    const auto index = integralLiteralValue(*ix.index);
                    if (index && *index >= 0 &&
                        static_cast<std::size_t>(*index) < tuple.parts.size()) {
                        const CppInfo ec = classify(tuple.parts[*index]);
                        if (ec.representable) {
                            needsTuple_ = true;
                            return "(std::get<" + std::to_string(*index) + ">(" +
                                   expr(*ix.object) + ") = " + expr(*a.value) +
                                   ")";
                        }
                    }
                }
            }
        }
        // A destructuring assignment (`[a, b] = expr`, `{ x } = expr`): the
        // parser reads the target as an array/object literal whose elements
        // are the assignment targets. A target form the write does not cover
        // falls through to the report below.
        if (a.target->kind == NodeKind::ArrayLiteral ||
            a.target->kind == NodeKind::ObjectLiteral) {
            std::string text = emitDestructuringAssign(a);
            if (!text.empty()) return text;
        }
        reportCodegenError(
            a, diagcode::kInvalidAssignmentTarget,
            "invalid assignment target: this target has no C++ write form");
        return "0.0";
    }
    return detail::AssignEmitter(*this).identifier(
        a, static_cast<const Identifier&>(*a.target));
}

}  // namespace cp3
