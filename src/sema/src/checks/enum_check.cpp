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

// `<number>` and `-<number>` / `+<number>` are number-backed; a string literal
// is string-backed; everything else needs the codegen-side evaluator and is
// reported as not implemented.
MemberInit classifyMemberInit(const Expr* init) {
    if (!init) return MemberInit::Missing;
    switch (init->kind) {
        case NodeKind::StringLiteral:
            return MemberInit::String;
        case NodeKind::NumberLiteral:
            return MemberInit::Number;
        case NodeKind::UnaryExpr: {
            const auto& u = static_cast<const UnaryExpr&>(*init);
            if (u.op != UnaryOp::Negate && u.op != UnaryOp::Plus) {
                return MemberInit::Other;
            }
            const Expr* operand = u.operand.get();
            if (operand && operand->kind == NodeKind::NumberLiteral) {
                return MemberInit::Number;
            }
            return MemberInit::Other;
        }
        default:
            return MemberInit::Other;
    }
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
        const MemberInit kind = classifyMemberInit(m->initializer.get());

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
                notImplemented(m->span, "this enum member initializer");
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
