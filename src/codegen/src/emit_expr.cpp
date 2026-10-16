// =============================================================================
// src/codegen/src/emit_expr.cpp — codegen expression lowering (STYLE.md §8).
//
// The expression emitter: literals, identifiers, templates, calls (including
// `console.log`/`console.info` lowering onto the D9 runtime formatter), and the
// operator forms. Logical operators use JS truthiness (`cp3_truthy` lambdas);
// `??` uses a real nullish (not truthiness) test. Out-of-scope nodes report
// CP3002 and emit a compilable stub (never throw, never abort).
// =============================================================================
#include "generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "cpp_writer.h"
#include "mangle.h"
#include "type_map.h"

namespace cp3 {

using namespace detail;

namespace {

const char* binaryOpSpelling(BinaryOp op) {
    switch (op) {
        case BinaryOp::Add: return "+";
        case BinaryOp::Subtract: return "-";
        case BinaryOp::Multiply: return "*";
        case BinaryOp::Divide: return "/";
        case BinaryOp::LessThan: return "<";
        case BinaryOp::GreaterThan: return ">";
        case BinaryOp::LessThanEqual: return "<=";
        case BinaryOp::GreaterThanEqual: return ">=";
        default: return nullptr;
    }
}

}  // namespace

std::string Generator::expr(const Expr& e) {
    switch (e.kind) {
        case NodeKind::NumberLiteral:
            return numberLiteralText(static_cast<const NumberLiteral&>(e).value);
        case NodeKind::StringLiteral:
            // Emit as a typed std::string. A bare literal would otherwise
            // bind to `cp3_ts(bool)`/`cp3_truthy(bool)` (a const char* ->
            // bool pointer conversion beats const char* -> std::string),
            // silently formatting every literal as "true".
            return "std::string(" +
                   cppStringLiteral(static_cast<const StringLiteral&>(e).value) + ")";
        case NodeKind::BoolLiteral:
            return static_cast<const BoolLiteral&>(e).value ? "true" : "false";
        case NodeKind::NullLiteral:
            return "nullptr";
        case NodeKind::UndefinedLiteral:
            return "cp3::runtime::undefined";
        case NodeKind::Identifier:
            return emitIdentifier(static_cast<const Identifier&>(e));
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
            return emitCall(static_cast<const CallExpr&>(e));
        default:
            notImplemented(e, "expression");
            return "0.0";
    }
}

std::string Generator::emitIdentifier(const Identifier& id) {
    if (id.name == "undefined") return "cp3::runtime::undefined";
    if (id.name == "null") return "nullptr";
    if (id.name == "NaN") return "std::numeric_limits<double>::quiet_NaN()";
    if (id.name == "Infinity") return "std::numeric_limits<double>::infinity()";
    if (id.name == "true") return "true";
    if (id.name == "false") return "false";
    return mapIdent(id.name);
}

std::string Generator::emitTemplate(const TemplateLiteral& tl) {
    std::string out;
    for (std::size_t i = 0; i < tl.quasis.size(); ++i) {
        const std::string cooked = tl.quasis[i] ? tl.quasis[i]->cooked : std::string();
        out += (out.empty() ? "" : " + ");
        out += "std::string(" + cppStringLiteral(cooked) + ")";
        if (i < tl.expressions.size()) {
            out += " + " + tsCall(*tl.expressions[i]);
        }
    }
    if (out.empty()) {
        out = "std::string()";
    }
    return "(" + out + ")";
}

// console.log/format: route an argument into the runtime formatter. The
// argument's own emitter reports any genuinely unsupported node; this just
// selects the formatter, letting `cp3_ts` overload-resolution pick the type.
std::string Generator::tsCall(const Expr& e) {
    const CppInfo ci = info(e);
    // A `T | null` optional prints "null" when empty, whereas a
    // `T | undefined` / `T?` optional prints "undefined".
    if (ci.shape == Shape::Optional && ci.emptyKind == NullishKind::Null) {
        return "cp3_ts_null(" + expr(e) + ")";
    }
    return "cp3_ts(" + expr(e) + ")";
}

std::string Generator::cond(const Expr& e) {
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
    const std::string lhs = expr(*b.left);
    const std::string rhs = expr(*b.right);
    const CppInfo L = info(*b.left);
    const CppInfo R = info(*b.right);

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
            return "std::fmod(" + lhs + ", " + rhs + ")";
        case BinaryOp::Power:
            return "std::pow(" + lhs + ", " + rhs + ")";
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
            notImplemented(b, "binary operator");
            return "0.0";
    }
}

std::string Generator::emitEquality(const BinaryExpr& b, const CppInfo& L,
                                    const CppInfo& R, const std::string& lhs,
                                    const std::string& rhs) {
    const bool isEqual = (b.op == BinaryOp::Equal || b.op == BinaryOp::StrictEqual);
    const bool isStrict = (b.op == BinaryOp::StrictEqual || b.op == BinaryOp::StrictNotEqual);
    const char* op = isEqual ? "==" : "!=";

    const bool lNullish = (L.shape == Shape::Null || L.shape == Shape::Undefined);
    const bool rNullish = (R.shape == Shape::Null || R.shape == Shape::Undefined);

    // null == undefined is true (loose); === is false. Both are nullish.
    if (lNullish && rNullish) {
        const bool sameKind = (L.shape == R.shape);
        if (isStrict) {
            return sameKind ? (isEqual ? "true" : "false") : (isEqual ? "false" : "true");
        }
        return isEqual ? "true" : "false";
    }

    // optional <-> nullish: an optional only ever holds a value or nothing.
    // Strict `===` only matches when the empty slot's flavor agrees.
    if (L.shape == Shape::Optional && rNullish) {
        if (isStrict && !optionalEmptyMatches(L.emptyKind, R.shape)) {
            return isEqual ? "false" : "true";
        }
        return isEqual ? "(! " + lhs + ".has_value())" : "(" + lhs + ".has_value())";
    }
    if (R.shape == Shape::Optional && lNullish) {
        if (isStrict && !optionalEmptyMatches(R.emptyKind, L.shape)) {
            return isEqual ? "false" : "true";
        }
        return isEqual ? "(! " + rhs + ".has_value())" : "(" + rhs + ".has_value())";
    }

    // optional vs optional, or optional vs T: compare the unwrapped value.
    if (L.shape == Shape::Optional || R.shape == Shape::Optional) {
        const std::string opt = (L.shape == Shape::Optional) ? lhs : rhs;
        const std::string val = (L.shape == Shape::Optional) ? rhs : lhs;
        return "((" + opt + ".has_value()) && (*" + opt + " " + op + " " + val + "))";
    }

    // Same representation: compare directly. D15 keeps loose `==` same-type.
    if (L.representable && R.representable && L.cpp == R.cpp) {
        return "(" + lhs + " " + op + " " + rhs + ")";
    }

    // Differing primitive kinds: D15 says loose `==` is false; `===` is false
    // too. Preserve operand evaluation with the comma operator.
    return "(" + lhs + ", " + rhs + ", " + (isEqual ? "false" : "true") + ")";
}

std::string Generator::emitLogical(const LogicalExpr& l) {
    if (l.op == LogicalOp::Nullish) {
        return emitNullish(l);
    }
    const CppInfo L = info(*l.left);
    const CppInfo R = info(*l.right);
    if (!L.representable || !R.representable || L.shape == Shape::Unsupported ||
        R.shape == Shape::Unsupported || L.cpp != R.cpp) {
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

    // `x ?? <nullish>` is `x` (with x evaluated exactly once).
    if (R.shape == Shape::Null || R.shape == Shape::Undefined) {
        return "(" + lhs + ")";
    }
    if (L.shape == Shape::Optional) {
        return "([&]{ auto cp3_l = (" + lhs +
               "); return cp3_l.has_value() ? *cp3_l : (" + rhs + "); }())";
    }
    if (L.shape == Shape::Null || L.shape == Shape::Undefined) {
        return "((" + lhs + "), (" + rhs + "))";
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
        case UnaryOp::Void:
            return "((void)(" + expr(*u.operand) + "), cp3::runtime::undefined)";
        case UnaryOp::Typeof:
            return emitTypeof(u);
        default:
            notImplemented(u, "unary operator");
            return "0.0";
    }
}

std::string Generator::emitTypeof(const UnaryExpr& u) {
    const CppInfo ci = info(*u.operand);
    switch (ci.shape) {
        case Shape::Number: return "std::string(\"number\")";
        case Shape::String: return "std::string(\"string\")";
        case Shape::Bool: return "std::string(\"boolean\")";
        case Shape::Undefined: return "std::string(\"undefined\")";
        case Shape::Null: return "std::string(\"object\")";
        default:
            notImplemented(u, "typeof of a non-primitive value");
            return "std::string(\"undefined\")";
    }
}

std::string Generator::emitUpdate(const UpdateExpr& u) {
    if (u.operand->kind != NodeKind::Identifier) {
        notImplemented(u, "update of a non-identifier target");
        return "0.0";
    }
    const std::string target = mapIdent(static_cast<const Identifier&>(*u.operand).name);
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
    notImplemented(c, "ternary with non-uniform branch types");
    return "0.0";
}

std::string Generator::emitAssignment(const AssignmentExpr& a) {
    if (a.target->kind != NodeKind::Identifier) {
        notImplemented(a, "assignment to a non-identifier target");
        return "0.0";
    }
    const std::string target = mapIdent(static_cast<const Identifier&>(*a.target).name);
    const CppInfo T = info(*a.target);
    const std::string value = expr(*a.value);

    switch (a.op) {
        case AssignOp::Assign:
            if (T.shape == Shape::Optional && isNullishLiteral(*a.value)) {
                return "(" + target + " = std::nullopt)";
            }
            return "(" + target + " = " + value + ")";
        case AssignOp::Add:
        case AssignOp::Subtract:
        case AssignOp::Multiply:
        case AssignOp::Divide: {
            const char* op = nullptr;
            switch (a.op) {
                case AssignOp::Add: op = "+="; break;
                case AssignOp::Subtract: op = "-="; break;
                case AssignOp::Multiply: op = "*="; break;
                default: op = "/="; break;
            }
            return "(" + target + " " + op + " " + value + ")";
        }
        case AssignOp::Modulo:
            return "(" + target + " = std::fmod(" + target + ", " + value + "))";
        case AssignOp::Power:
            return "(" + target + " = std::pow(" + target + ", " + value + "))";
        default:
            notImplemented(a, "compound assignment operator");
            return "0.0";
    }
}

std::string Generator::emitCall(const CallExpr& c) {
    if (isConsoleCall(c, "log") || isConsoleCall(c, "info")) {
        return emitConsoleLog(c);
    }
    if (isConsoleMember(c)) {
        notImplemented(c, "console method (only log/info are supported in M1)");
        return "0.0";
    }
    if (c.callee->kind == NodeKind::Identifier) {
        const std::string name = static_cast<const Identifier&>(*c.callee).name;
        if (name == "worker") {
            notImplemented(c, "worker(...)");
            return "0.0";
        }
        if (!c.typeArgs.empty()) {
            notImplemented(c, "generic call");
            return "0.0";
        }
        if (functions_.find(name) == functions_.end()) {
            // Not a function declared in this unit and not an intrinsic we
            // lower (console.* is handled above): treat it as out of scope
            // rather than emitting a call to an undeclared C++ name.
            notImplemented(c, "call to function '" + name + "' not declared in this unit");
            return "0.0";
        }
        std::vector<std::string> args;
        for (const auto& arg : c.arguments) {
            if (!arg) { args.push_back("0.0"); continue; }
            if (arg->kind == NodeKind::SpreadElement) {
                notImplemented(*arg, "spread argument");
                continue;
            }
            args.push_back(expr(*arg));
        }
        return mapIdent(name) + "(" + joinStrings(args, ", ") + ")";
    }
    if (c.callee->kind == NodeKind::MemberExpr) {
        const auto& m = static_cast<const MemberExpr&>(*c.callee);
        if (m.object && m.object->kind == NodeKind::Identifier &&
            static_cast<const Identifier&>(*m.object).name == "parallel") {
            notImplemented(c, "parallel.*");
            return "0.0";
        }
        notImplemented(c, "method call");
        return "0.0";
    }
    notImplemented(c, "call expression");
    return "0.0";
}

bool Generator::isConsoleMember(const CallExpr& c) const {
    if (c.callee->kind != NodeKind::MemberExpr) return false;
    const auto& m = static_cast<const MemberExpr&>(*c.callee);
    return m.object && m.object->kind == NodeKind::Identifier &&
           static_cast<const Identifier&>(*m.object).name == "console";
}

bool Generator::isConsoleCall(const CallExpr& c, const char* method) const {
    if (c.callee->kind != NodeKind::MemberExpr) return false;
    const auto& m = static_cast<const MemberExpr&>(*c.callee);
    return isConsoleMember(c) && m.property == method;
}

std::string Generator::emitConsoleLog(const CallExpr& c) {
    if (!consoleImported_) {
        notImplemented(c, "console used without `import console from \"std/console\"`");
        return "0.0";
    }
    if (c.arguments.empty()) {
        return "cp3::runtime::println(std::string())";
    }
    std::string joined = tsCall(*c.arguments.front());
    for (std::size_t i = 1; i < c.arguments.size(); ++i) {
        joined += " + std::string(\" \") + " + tsCall(*c.arguments[i]);
    }
    return "cp3::runtime::println(" + joined + ")";
}

}  // namespace cp3
