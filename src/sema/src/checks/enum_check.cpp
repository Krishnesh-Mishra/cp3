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

#include "cp3/diagnostic_codes.hpp"
#include "cp3/suggest.hpp"

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
