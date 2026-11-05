// =============================================================================
// src/sema/src/checks/enum_check.cpp - the enum declaration model (spec 04).
//
// Owns `registerEnum`, the single pass that resolves one `enum` declaration
// into an `EnumInfo` (detail/enums.h): it classifies every member's backing
// kind, reports the two documented backing diagnostics (CP3401 for an implicit
// member after a string-backed one, CP3706 for a declaration that mixes the
// kinds, D20), registers the type name as `TypeKind::Enum` and declares the
// enum's value binding. `enumMember` below is `memberOfProbe`'s enum arm
// (member_check.cpp). `visitEnumDecl` (declarations.cpp) and
// `hoistType` (scopes.cpp) both forward here; the `declaredTypes_` guard keeps
// the registration idempotent (STYLE.md §8).
// =============================================================================
#include "detail/analyzer.h"
#include "detail/enums.h"

#include <string>
#include <utility>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/diag/suggest.hpp"

namespace cp3 {

namespace {

// The backing kind of one member initializer.
enum class MemberInit { Missing, Number, String, Other };

// The kind of a constant expression: a number, a string, or neither (the
// expression is not a compile-time constant).
enum class ConstKind { None, Number, String };

// The member of `members` with this name, or nullptr. A linked member
// initializer (`B = A`) reads the member it names.
const EnumMemberInfo* linkedMember(const std::vector<EnumMemberInfo>& members,
                                   const std::string& name) {
    for (const EnumMemberInfo& m : members) {
        if (m.name == name) return &m;
    }
    return nullptr;
}

// The linked member an initializer names, or nullptr: a bare name (`A`) or the
// enum's own name with a member (`Color.A`).
const EnumMemberInfo* linkedTarget(const Expr* e,
                                   const std::vector<EnumMemberInfo>& members,
                                   const std::string& enumName) {
    if (e == nullptr) return nullptr;
    if (e->kind == NodeKind::Identifier) {
        return linkedMember(members, static_cast<const Identifier&>(*e).name);
    }
    if (e->kind != NodeKind::MemberExpr) return nullptr;
    const auto& m = static_cast<const MemberExpr&>(*e);
    if (!m.object || m.object->kind != NodeKind::Identifier) return nullptr;
    if (static_cast<const Identifier&>(*m.object).name != enumName) return nullptr;
    return linkedMember(members, m.property);
}

// The kind of a constant expression in the context of one enum declaration.
ConstKind constKind(const Expr* e, const std::vector<EnumMemberInfo>& members,
                    const std::string& enumName);

// The kind of a unary operator over a constant: a sign or a bit-not over a
// number.
ConstKind unaryConstKind(const UnaryExpr& u,
                          const std::vector<EnumMemberInfo>& members,
                          const std::string& enumName) {
    if (u.op != UnaryOp::Negate && u.op != UnaryOp::Plus &&
        u.op != UnaryOp::BitNot) {
        return ConstKind::None;
    }
    return constKind(u.operand.get(), members, enumName) == ConstKind::Number
               ? ConstKind::Number
               : ConstKind::None;
}

// The kind of a binary operator over two constants: the arithmetic operators
// work on numbers, `+` also concatenates two strings.
ConstKind binaryConstKind(const BinaryExpr& b,
                          const std::vector<EnumMemberInfo>& members,
                          const std::string& enumName) {
    const ConstKind left = constKind(b.left.get(), members, enumName);
    const ConstKind right = constKind(b.right.get(), members, enumName);
    if (b.op == BinaryOp::Add) {
        if (left == ConstKind::Number && right == ConstKind::Number) {
            return ConstKind::Number;
        }
        if (left == ConstKind::String && right == ConstKind::String) {
            return ConstKind::String;
        }
        return ConstKind::None;
    }
    const bool arithmetic = b.op == BinaryOp::Subtract ||
                            b.op == BinaryOp::Multiply ||
                            b.op == BinaryOp::Divide || b.op == BinaryOp::Modulo;
    if (!arithmetic) return ConstKind::None;
    return left == ConstKind::Number && right == ConstKind::Number
               ? ConstKind::Number
               : ConstKind::None;
}

// The kind of a constant expression in the context of one enum declaration.
// `members` are the members of the same enum resolved so far; a linked member
// initializer takes the kind of the member it names.
//
// The accepted forms are the ones the codegen evaluator can read
// (src/codegen/src/support/enum_values.cpp keeps the two in step): a literal, a
// unary sign / bit-not, an arithmetic operator over constants, and a link to an
// earlier member.
ConstKind constKind(const Expr* e, const std::vector<EnumMemberInfo>& members,
                    const std::string& enumName) {
    if (e == nullptr) return ConstKind::None;
    switch (e->kind) {
        case NodeKind::NumberLiteral:
            return ConstKind::Number;
        case NodeKind::StringLiteral:
            return ConstKind::String;
        case NodeKind::UnaryExpr:
            return unaryConstKind(static_cast<const UnaryExpr&>(*e), members,
                                  enumName);
        case NodeKind::BinaryExpr:
            return binaryConstKind(static_cast<const BinaryExpr&>(*e), members,
                                   enumName);
        default:
            break;
    }
    const EnumMemberInfo* target = linkedTarget(e, members, enumName);
    if (target == nullptr) return ConstKind::None;
    return target->isString ? ConstKind::String : ConstKind::Number;
}

// The backing kind of one member initializer: a number, a string, nothing (the
// member inherits the declaration's backing kind) or a non-constant value.
MemberInit classifyMemberInit(const Expr* init,
                               const std::vector<EnumMemberInfo>& members,
                               const std::string& enumName) {
    if (!init) return MemberInit::Missing;
    switch (constKind(init, members, enumName)) {
        case ConstKind::Number: return MemberInit::Number;
        case ConstKind::String: return MemberInit::String;
        case ConstKind::None: return MemberInit::Other;
    }
    return MemberInit::Other;
}

// -----------------------------------------------------------------------------
// Constant evaluation: the member values + reverse map (const enum support)
// -----------------------------------------------------------------------------
// `registerEnum` classifies every member above; the helpers below *evaluate*
// the same constant expressions into the compile-time values published in
// `SemaResult::constEnums` (cp3/core/utility_types.hpp). The accepted forms
// deliberately mirror `constKind` (a literal, a unary sign / bit-not, an
// arithmetic operator over constants, a link to an earlier member) so the
// two can never drift: anything `constKind` calls a constant evaluates
// here, and vice versa. Codegen inlines `const enum` member accesses from
// the published table - there is no runtime enum object and no runtime
// metadata.

// The already-resolved value of the member `name`, or nullptr.
const ConstEnumValue* resolvedMember(const std::vector<ConstEnumValue>& values,
                                     const std::string& name) {
    for (const ConstEnumValue& v : values) {
        if (v.name == name) return &v;
    }
    return nullptr;
}

// The linked member an initializer names (same forms as `linkedTarget`,
// but over the evaluated values): a bare name (`A`) or `Enum.A`.
const ConstEnumValue* resolvedTarget(
    const Expr* e, const std::vector<ConstEnumValue>& values,
    const std::string& enumName) {
    if (e == nullptr) return nullptr;
    if (e->kind == NodeKind::Identifier) {
        return resolvedMember(values,
                              static_cast<const Identifier&>(*e).name);
    }
    if (e->kind != NodeKind::MemberExpr) return nullptr;
    const auto& m = static_cast<const MemberExpr&>(*e);
    if (!m.object || m.object->kind != NodeKind::Identifier) return nullptr;
    if (static_cast<const Identifier&>(*m.object).name != enumName) {
        return nullptr;
    }
    return resolvedMember(values, m.property);
}

// Evaluates a constant *number* expression. False when the expression is
// not a numeric constant (the caller keeps the member valueless; the
// classification pass already reported non-constants).
bool evalConstNumber(const Expr* e,
                     const std::vector<ConstEnumValue>& values,
                     const std::string& enumName, double& out) {
    if (e == nullptr) return false;
    switch (e->kind) {
        case NodeKind::NumberLiteral:
            out = static_cast<const NumberLiteral&>(*e).value;
            return true;
        case NodeKind::UnaryExpr: {
            const auto& u = static_cast<const UnaryExpr&>(*e);
            if (u.op != UnaryOp::Negate && u.op != UnaryOp::Plus &&
                u.op != UnaryOp::BitNot) {
                return false;
            }
            double inner = 0.0;
            if (!evalConstNumber(u.operand.get(), values, enumName, inner)) {
                return false;
            }
            switch (u.op) {
                case UnaryOp::Negate: out = -inner; break;
                case UnaryOp::Plus: out = inner; break;
                case UnaryOp::BitNot:
                    out = static_cast<double>(~static_cast<long long>(inner));
                    break;
                default: return false;
            }
            return true;
        }
        case NodeKind::BinaryExpr: {
            const auto& b = static_cast<const BinaryExpr&>(*e);
            double l = 0.0, r = 0.0;
            if (!evalConstNumber(b.left.get(), values, enumName, l) ||
                !evalConstNumber(b.right.get(), values, enumName, r)) {
                return false;
            }
            switch (b.op) {
                case BinaryOp::Add: out = l + r; break;
                case BinaryOp::Subtract: out = l - r; break;
                case BinaryOp::Multiply: out = l * r; break;
                case BinaryOp::Divide:
                    if (r == 0.0) return false;
                    out = l / r;
                    break;
                case BinaryOp::Modulo: {
                    if (r == 0.0) return false;
                    // Truncating remainder (JS `%` semantics).
                    out = l - static_cast<double>(
                                  static_cast<long long>(l / r)) * r;
                    break;
                }
                default: return false;
            }
            return true;
        }
        default:
            break;
    }
    const ConstEnumValue* target = resolvedTarget(e, values, enumName);
    if (target == nullptr || target->isString || !target->hasValue) {
        return false;
    }
    out = target->numberValue;
    return true;
}

// Evaluates a constant *string* expression: a literal, `+` over two string
// constants, or a link to an earlier string member.
bool evalConstString(const Expr* e,
                     const std::vector<ConstEnumValue>& values,
                     const std::string& enumName, std::string& out) {
    if (e == nullptr) return false;
    if (e->kind == NodeKind::StringLiteral) {
        out = static_cast<const StringLiteral&>(*e).value;
        return true;
    }
    if (e->kind == NodeKind::BinaryExpr) {
        const auto& b = static_cast<const BinaryExpr&>(*e);
        if (b.op != BinaryOp::Add) return false;
        std::string l, r;
        if (!evalConstString(b.left.get(), values, enumName, l) ||
            !evalConstString(b.right.get(), values, enumName, r)) {
            return false;
        }
        out = l + r;
        return true;
    }
    const ConstEnumValue* target = resolvedTarget(e, values, enumName);
    if (target == nullptr || !target->isString || !target->hasValue) {
        return false;
    }
    out = target->stringValue;
    return true;
}

}  // namespace

void Analyzer::registerEnum(EnumDeclaration& e) {
    if (declaredTypes_.count(e.nodeId) != 0) return;
    declaredTypes_.insert(e.nodeId);

    EnumInfo info;
    info.name = e.name;

    bool sawString = false;
    bool sawNumber = false;
    bool previousString = false;
    for (auto& m : e.members) {
        if (!m) continue;
        EnumMemberInfo mi;
        mi.name = m->name;
        const MemberInit kind =
            classifyMemberInit(m->initializer.get(), info.members, e.name);

        switch (kind) {
            case MemberInit::String:
                mi.isString = true;
                break;
            case MemberInit::Number:
                break;
            case MemberInit::Missing:
                // An implicit member after a string-backed one has no value to
                // inherit: TS parity is an error, not an empty string (D20).
                if (previousString) {
                    report(diagcode::kTypeMismatch, m->span,
                           "type mismatch: enum member '" + m->name +
                               "' is missing an initializer after a "
                               "string-backed member");
                    mi.isString = true;
                }
                break;
            case MemberInit::Other:
                // An enum member carries a compile-time constant: a literal, a
                // constant expression over literals, or a link to an earlier
                // member of the same enum.
                report(diagcode::kTypeMismatch, m->span,
                       "type mismatch: enum member '" + m->name +
                           "' must be initialized with a constant number or "
                           "string expression");
                break;
        }

        sawString = sawString || mi.isString;
        sawNumber =
            sawNumber || (!mi.isString && kind != MemberInit::Other);
        previousString = mi.isString;
        info.members.push_back(std::move(mi));
    }

    if (sawString && sawNumber) {
        report(diagcode::kMixedEnumBacking, e.span,
               "enum '" + e.name +
                   "' mixes number and string members; an enum must be backed "
                   "by one kind only");
    }
    info.stringBacked = sawString;

    // The compile-time values + reverse map (const enum support): walk the
    // members again, evaluating each initializer with the helpers above.
    // Numeric members auto-increment (the first implicit member is 0, each
    // later one is previous + 1) exactly as the codegen evaluator reads
    // them; string members keep their string. The reverse map covers
    // numeric values only (TypeScript parity) and collapses duplicates to
    // the last member with that value. Published for regular and `const`
    // enums alike: a `const enum` is consumed by inlining, a regular one
    // by its runtime object - both read the same table.
    {
        ConstEnumInfo resolved;
        resolved.name = e.name;
        resolved.isConst = e.isConst;
        resolved.stringBacked = sawString;
        resolved.declNodeId = e.nodeId;
        double previousNumber = -1.0;
        bool havePreviousNumber = false;
        for (auto& m : e.members) {
            if (!m) continue;
            const EnumMemberInfo* decl = info.member(m->name);
            ConstEnumValue v;
            v.name = m->name;
            v.isString = decl != nullptr && decl->isString;
            if (m->initializer) {
                if (v.isString) {
                    v.hasValue = evalConstString(m->initializer.get(),
                                                 resolved.values, e.name,
                                                 v.stringValue);
                } else {
                    v.hasValue = evalConstNumber(m->initializer.get(),
                                                 resolved.values, e.name,
                                                 v.numberValue);
                }
            } else if (!v.isString) {
                // Implicit member: previous + 1 (0 for the first).
                v.numberValue = havePreviousNumber ? previousNumber + 1.0 : 0.0;
                v.hasValue = true;
            }
            if (v.hasValue && !v.isString) {
                previousNumber = v.numberValue;
                havePreviousNumber = true;
            }
            resolved.values.push_back(std::move(v));
        }
        if (!sawString) {
            for (const ConstEnumValue& v : resolved.values) {
                if (!v.hasValue || v.isString) continue;
                bool replaced = false;
                for (ConstEnumReverseEntry& entry : resolved.reverse) {
                    if (entry.numberValue == v.numberValue) {
                        entry.name = v.name;  // last member wins
                        replaced = true;
                        break;
                    }
                }
                if (!replaced) {
                    resolved.reverse.push_back(
                        ConstEnumReverseEntry{v.numberValue, v.name});
                }
            }
        }
        out_.constEnums[e.nodeId] = std::move(resolved);
    }

    const Type enumType = Type::named(TypeKind::Enum, e.name);
    if (!e.name.empty()) {
        typeNames_[e.name] = enumType;
        declareBinding(e.name, enumType, e.span,
                       BindingSpec{.isConst = true, .declNodeId = e.nodeId});
        enums_[e.name] = std::move(info);
    }
    setType(e, enumType);
}

// -----------------------------------------------------------------------------
// Member access
// -----------------------------------------------------------------------------
// `Color.Green` (spec 04): a known member resolves to the enum type; an unknown
// member is CP3305 naming the enum's members as suggestions. An enum type not
// declared in this unit keeps the ambient fallthrough.
Type Analyzer::enumMember(const Type& ot, const std::string& prop,
                          const SourceSpan& span) {
    const auto found = enums_.find(ot.name);
    if (found == enums_.end()) return ambient();
    const EnumInfo& info = found->second;
    if (info.member(prop)) return Type::named(TypeKind::Enum, ot.name);
    std::vector<std::string> candidates;
    candidates.reserve(info.members.size());
    for (const EnumMemberInfo& m : info.members) {
        candidates.push_back(m.name);
    }
    std::string message =
        "member '" + prop + "' does not exist on enum '" + ot.name + "'";
    std::vector<std::string> suggestions;
    const std::vector<std::string> ranked = suggestNames(prop, candidates);
    if (!ranked.empty()) {
        message += "; did you mean '" + ranked.front() + "'?";
        suggestions.push_back(didYouMeanPhrase(ranked));
    }
    reportSuggestive(diagcode::kMemberNotFound, span, std::move(message),
                     std::move(suggestions));
    return Type::unknown();
}

}  // namespace cp3
