// =============================================================================
// src/sema/src/checks/class_check.cpp - user classes: model, resolution, access.
//
// Implements the spec §15 object-model surface (docs/language-spec/15-classes.md):
// `resolveClasses` builds one `ClassInfo` member table per non-generic class
// declaration before the body/visit pass, `checkClassBodies` checks field
// initializers and method/constructor bodies with `this` bound, and
// `userClassMember` resolves instance/static member reads with the privacy,
// weak-field (spec §11) and CP3305/CP3802 diagnostics. Structural conformance
// and `new` live in class_conform_check.cpp (STYLE.md §8).
// =============================================================================
#include "detail/analyzer.h"
#include "detail/cycle_analysis.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

namespace {

// -----------------------------------------------------------------------------
// Stage 2 (automatic cycle handling): compiler-injected non-owning edges.
//
// The compiler may treat ONE strong edge per SCC cycle group as non-owning
// (weak-cost zero, i.e. a `WeakRef` field) when the whole-program ownership
// proof holds, so the user does not need a manual `weak`. The proof
// obligations (docs/design/auto-weak.md) are, for the chosen edge A.f -> B:
//
//   P1 direct    : A.f is a direct class reference (nullable/union-with-
//                  nullish stripped); container/function edges (`B[]`, Map/Set
//                  values, closures) cannot be a single `WeakRef`.
//   P2 no-unknown: neither A.f's type nor A is unknown-risk (an opaque
//                  `unknown` field is an escape we cannot see through).
//   P3 escape-   : no escape site for A.f other than `escape: none`; the
//      none         target B does not escape through this field.
//   P4 one-per-  : exactly one edge is chosen per SCC group, chosen
//      SCC          deterministically (lexicographically smallest A.f).
//   P5 body      : body-level escapes of B (return, global/static store,
//      escapes       closure capture, native/worker crossing) are absent.
//                  NOT yet modeled by the field-type escape sites -> the
//                  proof is incomplete today, so the gate below stays off.
//   P6 instance  : A.f is an instance field (static fields are excluded
//                  from the strong-edge graph entirely).
//   P7 gate      : the differential fuzzer + ASan gate has passed.
//
// `kAutoWeakGateEnabled` is the P5+P7 gate: while it is false the compiler
// runs in report-only mode. Sema still selects and publishes the candidate
// edge (in SemaResult::cycleEscapeSites, consumed by `cp3 check --report`)
// but never marks it `proven`, never suppresses CP3401, and codegen never
// weakens the field. Flipping the gate requires P5 (body escape analysis)
// and P7 (vendor/tools/fuzz_graphs.py differential + ASan) to be met first.
// -----------------------------------------------------------------------------
constexpr bool kAutoWeakGateEnabled = false;

struct CycleEdgeInfo {
    std::string src;
    std::string field;
    std::string dst;
    bool direct = false;  // P1: the field type is exactly the dst class ref
    bool unknown = false; // P2/P3: the field type mentions `unknown`
};

struct CycleGraphInfo {
    std::unordered_map<std::string, std::unordered_set<std::string>> graph;
    std::unordered_set<std::string> unknownRisk;
    std::unordered_set<std::string> weakEdges;  // "A->B", manual `weak`
    std::vector<CycleEdgeInfo> edges;           // every strong field edge
};

struct AutoWeakDecision {
    std::string state;  // "candidate" | "proven" | "rejected"
    std::string reason; // empty for candidate/proven
};

// The class a field type names directly, or "" when it is not a direct
// class reference: `B`, `B?`, `B | null`, `B | undefined` are direct;
// `B[]`, `Map<_, B>`, `(b: B) => void`, `B | Other`, `unknown` are not
// (P1). A union of one class plus nullish members is direct.
std::string directClassRef(
    const Type& t, const std::unordered_map<std::string, ClassInfo>& classes) {
    if (t.kind == TypeKind::Class) {
        return classes.count(t.name) ? t.name : std::string();
    }
    if (t.kind == TypeKind::Nullable) {
        return t.parts.size() == 1
                   ? directClassRef(t.parts.front(), classes)
                   : std::string();
    }
    if (t.kind == TypeKind::Union) {
        std::string found;
        for (const Type& p : t.parts) {
            if (p.isNullish()) continue;
            const std::string r = directClassRef(p, classes);
            if (r.empty() || (!found.empty() && found != r)) return std::string();
            found = r;
        }
        return found;
    }
    return std::string();
}

// The strong-edge class graph (weak excluded, unknown fail-closed), plus the
// per-field edge list the auto-weak selection needs. Mirrors the graph the
// Stage 1 analysis built inline in computeCycleCapable.
CycleGraphInfo buildCycleGraph(
    const std::unordered_map<std::string, ClassInfo>& classes) {
    CycleGraphInfo info;
    std::function<void(const Type&, std::unordered_set<std::string>&)> walk =
        [&](const Type& t, std::unordered_set<std::string>& out) {
            if (t.isUnknown()) {
                out.insert("$unknown");
                return;
            }
            if (t.kind == TypeKind::Class) {
                if (classes.count(t.name)) out.insert(t.name);
            }
            for (const Type& p : t.parts) walk(p, out);
        };
    for (const auto& [name, cls] : classes) {
        auto& edges = info.graph[name];
        for (const auto& m : cls.members) {
            if (m.isMethod || m.isStatic) continue;  // P6
            std::unordered_set<std::string> refs;
            walk(m.type, refs);
            if (m.isWeak) {
                for (const std::string& r : refs) {
                    if (classes.count(r)) info.weakEdges.insert(name + "->" + r);
                }
                continue;
            }
            const std::string direct = directClassRef(m.type, classes);
            for (const std::string& r : refs) {
                if (r == "$unknown") {
                    info.unknownRisk.insert(name);
                    continue;
                }
                if (!classes.count(r)) continue;
                edges.insert(r);
                CycleEdgeInfo e;
                e.src = name;
                e.field = m.name;
                e.dst = r;
                e.direct = direct == r;
                e.unknown = refs.count("$unknown") != 0;
                info.edges.push_back(e);
            }
        }
    }
    return info;
}

// Reachability over the strong-edge graph (the Stage 1 `reaches`).
bool cycleReaches(const CycleGraphInfo& info, const std::string& from,
                  const std::string& target) {
    std::unordered_set<std::string> seen;
    std::vector<std::string> work{from};
    while (!work.empty()) {
        std::string cur = work.back();
        work.pop_back();
        if (cur == target) return true;
        if (!seen.insert(cur).second) continue;
        const auto it = info.graph.find(cur);
        if (it != info.graph.end()) {
            for (const std::string& nxt : it->second) work.push_back(nxt);
        }
    }
    return false;
}

// The cycle-capable set: classes that can reach themselves through strong
// edges (unknown-risk classes included, fail closed).
std::unordered_set<std::string> cycleCapableOf(
    const CycleGraphInfo& info,
    const std::unordered_map<std::string, ClassInfo>& classes) {
    std::unordered_set<std::string> capable;
    for (const auto& [name, cls] : classes) {
        (void)cls;
        if (info.unknownRisk.count(name)) {
            capable.insert(name);
            continue;
        }
        const auto git = info.graph.find(name);
        if (git == info.graph.end()) continue;
        std::unordered_set<std::string> seen;
        std::vector<std::string> work(git->second.begin(), git->second.end());
        bool reachesSelf = false;
        while (!work.empty() && !reachesSelf) {
            std::string cur = work.back();
            work.pop_back();
            if (cur == name) {
                reachesSelf = true;
                break;
            }
            if (!seen.insert(cur).second) continue;
            const auto it = info.graph.find(cur);
            if (it != info.graph.end()) {
                for (const std::string& nxt : it->second) work.push_back(nxt);
            }
        }
        if (reachesSelf) capable.insert(name);
    }
    return capable;
}

// SCC cycle groups of size > 1 over the cycle-capable set (the grouping the
// Stage 1 diagnostic used). Single self-references are not groups.
std::vector<std::vector<std::string>> cycleGroupsOf(
    const CycleGraphInfo& info,
    const std::unordered_set<std::string>& capable) {
    std::vector<std::vector<std::string>> groups;
    std::unordered_set<std::string> assigned;
    for (const std::string& name : capable) {
        if (assigned.count(name)) continue;
        std::vector<std::string> group{name};
        assigned.insert(name);
        for (const std::string& other : capable) {
            if (other == name || assigned.count(other)) continue;
            if (cycleReaches(info, name, other) &&
                cycleReaches(info, other, name)) {
                group.push_back(other);
                assigned.insert(other);
            }
        }
        if (group.size() > 1) {
            std::sort(group.begin(), group.end());
            groups.push_back(group);
        }
    }
    std::sort(groups.begin(), groups.end());
    return groups;
}

// Does a cycle group already contain a manual `weak` edge?
bool groupHasManualWeak(const CycleGraphInfo& info,
                        const std::vector<std::string>& group) {
    std::unordered_set<std::string> inGroup(group.begin(), group.end());
    for (const std::string& e : info.weakEdges) {
        const std::size_t pos = e.find("->");
        if (pos == std::string::npos) continue;
        if (inGroup.count(e.substr(0, pos)) &&
            inGroup.count(e.substr(pos + 2)))
            return true;
    }
    return false;
}

// Selects ONE edge per unresolved cycle group (P4): the lexicographically
// smallest eligible `Src.field -> Dst` (by src, then field, then dst).
// Eligible means P1 (direct), P2 (no unknown) and P3 (no escape site for
// the edge other than the field store itself) hold at the field-type level;
// P5 is the remaining whole-program obligation and keeps the gate off.
// Returns decisions keyed "Src.field" for every intra-group strong edge of
// every group without a manual weak edge.
std::unordered_map<std::string, AutoWeakDecision> selectAutoWeak(
    const CycleGraphInfo& info,
    const std::vector<std::vector<std::string>>& groups) {
    std::unordered_map<std::string, AutoWeakDecision> out;
    for (const auto& group : groups) {
        if (groupHasManualWeak(info, group)) continue;
        std::unordered_set<std::string> inGroup(group.begin(), group.end());
        std::vector<const CycleEdgeInfo*> eligible;
        std::vector<const CycleEdgeInfo*> intra;
        for (const CycleEdgeInfo& e : info.edges) {
            if (!inGroup.count(e.src) || !inGroup.count(e.dst)) continue;
            intra.push_back(&e);
            if (e.unknown || info.unknownRisk.count(e.src)) continue;  // P2
            if (!e.direct) continue;                                  // P1
            eligible.push_back(&e);
        }
        std::sort(eligible.begin(), eligible.end(),
                  [](const CycleEdgeInfo* a, const CycleEdgeInfo* b) {
                      if (a->src != b->src) return a->src < b->src;
                      if (a->field != b->field) return a->field < b->field;
                      return a->dst < b->dst;
                  });
        const CycleEdgeInfo* chosen =
            eligible.empty() ? nullptr : eligible.front();
        for (const CycleEdgeInfo* e : intra) {
            const std::string key = e->src + "." + e->field;
            // One decision per field: the chosen (candidate/proven) edge
            // wins over a rejected edge of the same field naming another
            // dst (a non-direct union field can list several refs).
            const auto existing = out.find(key);
            if (existing != out.end() && existing->second.state != "rejected" &&
                e != chosen) {
                continue;
            }
            if (e == chosen) {
                // Report-only until P5+P7 pass: a candidate is a preview,
                // only a proven edge may suppress CP3401 / lower as weak.
                out[key] = AutoWeakDecision{
                    kAutoWeakGateEnabled ? "proven" : "candidate", ""};
            } else if (e->unknown || info.unknownRisk.count(e->src)) {
                out[key] = AutoWeakDecision{"rejected", "unknown-risk"};
            } else if (!e->direct) {
                out[key] = AutoWeakDecision{"rejected", "container-edge"};
            } else {
                out[key] = AutoWeakDecision{"rejected", "not-selected"};
            }
        }
    }
    return out;
}

}  // namespace

// =============================================================================
// Resolution
// =============================================================================
void Analyzer::resolveClasses(StmtList& body) {
    for (auto& s : body) {
        if (!s) continue;
        if (s->kind == NodeKind::ExportDeclaration) {
            // `export <class-or-interface>` wraps the declaration; resolve the
            // inner classes the same way as their unexported siblings.
            auto& e = static_cast<ExportDeclaration&>(*s);
            if (e.declaration && e.declaration->kind == NodeKind::ClassDeclaration) {
                auto& inner = static_cast<ClassDeclaration&>(*e.declaration);
                if (!classResolved_.insert(inner.nodeId).second) continue;
                resolveClass(inner);
            }
            continue;
        }
        if (s->kind != NodeKind::ClassDeclaration) continue;
        auto& c = static_cast<ClassDeclaration&>(*s);
        if (!classResolved_.insert(c.nodeId).second) continue;
        resolveClass(c);
    }
}

void Analyzer::enterClassTypeParams(const TypeParamList& typeParams,
                                    TypeParamScope& saved) {
    for (const auto& tp : typeParams) {
        if (!tp) continue;
        const std::string& name = tp->name;
        if (name.empty()) continue;
        const auto it = typeNames_.find(name);
        std::optional<Type> old;
        if (it != typeNames_.end()) old = it->second;
        saved.push_back(std::make_pair(name, old));
        Type param = Type::named(TypeKind::TypeParam, name);
        // Canonical slot layout everywhere: parts[0] = constraint-or-unknown,
        // parts[1] = default-or-unknown (`isUnknown()` spells "unset", so the
        // two slots are always distinguishable by index).
        Type constraintSlot = Type::unknown();
        Type defaultSlot = Type::unknown();
        if (tp->constraint) {
            Type constraint = resolveType(*tp->constraint);
            if (tp->constraint->kind == NodeKind::NamedType) {
                typeParamConstraintNames_[typeKey(constraint)] =
                    static_cast<NamedType&>(*tp->constraint).name;
            }
            constraintSlot = std::move(constraint);
        }
        if (tp->defaultType) {
            defaultSlot = resolveType(*tp->defaultType);
        }
        param.parts.push_back(std::move(constraintSlot));
        param.parts.push_back(std::move(defaultSlot));
        typeNames_[name] = std::move(param);
    }
}

void Analyzer::leaveClassTypeParams(const TypeParamScope& saved) {
    for (const auto& [name, old] : saved) {
        if (old) {
            typeNames_[name] = *old;
        } else {
            typeNames_.erase(name);
        }
    }
}

Type Analyzer::substituteClassTypeParams(const Type& t, const ClassInfo& ci,
                                         const std::vector<Type>& args) const {
    if (t.kind == TypeKind::TypeParam) {
        for (std::size_t i = 0; i < ci.typeParamNames.size(); ++i) {
            if (ci.typeParamNames[i] != t.name) continue;
            if (i < args.size()) return args[i];
            return t;
        }
        return t;
    }
    Type out = t;
    for (Type& part : out.parts) {
        part = substituteClassTypeParams(part, ci, args);
    }
    return out;
}

namespace {

// ---------------------------------------------------------------------------
// Definite-assignment analysis (spec §15). A class field declared without an
// initializer must be assigned in a constructor (or carry `!:`) or the class
// cannot be constructed; fields captured by an uninitialized `this.field`
// would read as zero-initialized storage instead.
// ---------------------------------------------------------------------------
bool thisFieldNameOut(const Expr* expr, std::string& out) {
    if (expr == nullptr || expr->kind != NodeKind::MemberExpr) return false;
    const auto& me = static_cast<const MemberExpr&>(*expr);
    if (me.object == nullptr) return false;
    if (me.object->kind != NodeKind::ThisExpr &&
        !(me.object->kind == NodeKind::Identifier &&
          static_cast<const Identifier&>(*me.object).name == "this")) {
        return false;
    }
    out = me.property;
    return !out.empty();
}
void noteStores(const Expr* expr, std::unordered_set<std::string>& out);
void noteStores(const Stmt* stmt, std::unordered_set<std::string>& out) {
    if (stmt == nullptr) return;
    switch (stmt->kind) {
        case NodeKind::expressionstatement: {
            const auto& s = static_cast<const expressionstatement&>(*stmt);
            noteStores(s.expression.get(), out);
            return;
        }
        case NodeKind::VariableDeclaration: {
            const auto& vd = static_cast<const VariableDeclaration&>(*stmt);
            for (const VariableDeclaratorPtr& d : vd.declarations) {
                if (!d) continue;
                noteStores(d->init.get(), out);
            }
            return;
        }
        case NodeKind::BlockStatement: {
            const auto& b = static_cast<const BlockStatement&>(*stmt);
            for (const StmtPtr& child : b.body) noteStores(child.get(), out);
            return;
        }
        case NodeKind::IfStatement: {
            const auto& s = static_cast<const IfStatement&>(*stmt);
            noteStores(s.condition.get(), out);
            noteStores(s.thenBranch.get(), out);
            noteStores(s.elseBranch.get(), out);
            return;
        }
        case NodeKind::WhileStatement: {
            const auto& s = static_cast<const WhileStatement&>(*stmt);
            noteStores(s.condition.get(), out);
            noteStores(s.body.get(), out);
            return;
        }
        case NodeKind::DoWhileStatement: {
            const auto& s = static_cast<const DoWhileStatement&>(*stmt);
            noteStores(s.body.get(), out);
            noteStores(s.condition.get(), out);
            return;
        }
        case NodeKind::ForStatement: {
            const auto& s = static_cast<const ForStatement&>(*stmt);
            Node* init = s.init.get();
            if (init != nullptr) {
                if (init->kind == NodeKind::VariableDeclaration ||
                    init->kind == NodeKind::BlockStatement) {
                    noteStores(static_cast<const Stmt*>(init), out);
                } else {
                    noteStores(static_cast<const Expr*>(init), out);
                }
            }
            noteStores(s.condition.get(), out);
            noteStores(s.update.get(), out);
            noteStores(s.body.get(), out);
            return;
        }
        case NodeKind::ForOfStatement:
        case NodeKind::ForInStatement: {
            const auto& s = static_cast<const ForOfStatement&>(*stmt);
            Node* left = s.left.get();
            if (left == nullptr) return;
            if (left->kind == NodeKind::VariableDeclaration ||
                left->kind == NodeKind::BlockStatement) {
                noteStores(static_cast<const Stmt*>(left), out);
            } else {
                noteStores(static_cast<const Expr*>(left), out);
            }
            noteStores(s.right.get(), out);
            noteStores(s.body.get(), out);
            return;
        }
        case NodeKind::ReturnStatement: {
            const auto& s = static_cast<const ReturnStatement&>(*stmt);
            noteStores(s.argument.get(), out);
            return;
        }
        case NodeKind::ThrowStatement: {
            const auto& s = static_cast<const ThrowStatement&>(*stmt);
            noteStores(s.argument.get(), out);
            return;
        }
        case NodeKind::TryStatement: {
            const auto& s = static_cast<const TryStatement&>(*stmt);
            noteStores(s.tryBlock.get(), out);
            if (s.handler) noteStores(s.handler->body.get(), out);
            noteStores(s.finallyBlock.get(), out);
            return;
        }
        case NodeKind::SwitchStatement: {
            const auto& s = static_cast<const SwitchStatement&>(*stmt);
            noteStores(s.discriminant.get(), out);
            for (const SwitchCasePtr& c : s.cases) {
                if (!c) continue;
                noteStores(c->test.get(), out);
                for (const StmtPtr& body : c->statements) noteStores(body.get(), out);
            }
            return;
        }
        default:
            return;
    }
}
void noteStores(const Expr* expr, std::unordered_set<std::string>& out) {
    if (expr == nullptr) return;
    switch (expr->kind) {
        case NodeKind::AssignmentExpr: {
            const auto& a = static_cast<const AssignmentExpr&>(*expr);
            std::string name;
            if (thisFieldNameOut(a.target.get(), name)) out.insert(name);
            noteStores(a.value.get(), out);
            return;
        }
        case NodeKind::UpdateExpr: {
            const auto& u = static_cast<const UpdateExpr&>(*expr);
            std::string name;
            if (thisFieldNameOut(u.operand.get(), name)) out.insert(name);
            return;
        }
        case NodeKind::MemberExpr: {
            const auto& m = static_cast<const MemberExpr&>(*expr);
            noteStores(m.object.get(), out);
            return;
        }
        case NodeKind::IndexExpr: {
            const auto& i = static_cast<const IndexExpr&>(*expr);
            noteStores(i.object.get(), out);
            noteStores(i.index.get(), out);
            return;
        }
        case NodeKind::CallExpr:
        case NodeKind::NewExpr: {
            const auto& c = static_cast<const CallExpr&>(*expr);
            noteStores(c.callee.get(), out);
            for (const ExprPtr& a : c.arguments) noteStores(a.get(), out);
            return;
        }
        case NodeKind::ArrayLiteral: {
            const auto& a = static_cast<const ArrayLiteral&>(*expr);
            for (const ExprPtr& e : a.elements) noteStores(e.get(), out);
            return;
        }
        case NodeKind::ObjectLiteral: {
            const auto& o = static_cast<const ObjectLiteral&>(*expr);
            for (const ObjectPropertyPtr& p : o.properties) {
                if (!p) continue;
                noteStores(p->value.get(), out);
            }
            return;
        }
        case NodeKind::ConditionalExpr: {
            const auto& c = static_cast<const ConditionalExpr&>(*expr);
            noteStores(c.condition.get(), out);
            noteStores(c.whenTrue.get(), out);
            noteStores(c.whenFalse.get(), out);
            return;
        }
        case NodeKind::BinaryExpr: {
            const auto& b = static_cast<const BinaryExpr&>(*expr);
            noteStores(b.left.get(), out);
            noteStores(b.right.get(), out);
            return;
        }
        case NodeKind::UnaryExpr: {
            const auto& u = static_cast<const UnaryExpr&>(*expr);
            noteStores(u.operand.get(), out);
            return;
        }
        case NodeKind::ArrowFunction: {
            const auto& a = static_cast<const ArrowFunction&>(*expr);
            Node* body = a.body.get();
            if (body == nullptr) return;
            if (body->kind == NodeKind::BlockStatement) {
                noteStores(static_cast<const Stmt*>(body), out);
            } else {
                noteStores(static_cast<const Expr*>(body), out);
            }
            return;
        }
        case NodeKind::AsExpr: {
            const auto& a = static_cast<const AsExpr&>(*expr);
            noteStores(a.expression.get(), out);
            return;
        }
        default:
            return;
    }
}

}  // namespace

void Analyzer::checkDefiniteAssignment(const ClassDeclaration& c,
                                       const ClassInfo& ci) {
    // Only classes with a constructor bear the "must assign bare fields"
    // obligation: the field is initialized at construction time.  A bare
    // `class Box<T> { value: T }` has no user-declared constructor and is
    // initialized at the use site (`b.value = 5`), which is the normal
    // structural-typing pattern the corpus relies on.  Without the gate the
    // rule errors on every such fixture.
    if (!ci.hasCtor) return;
    std::unordered_set<std::string> assigned;
    for (const ClassMemberPtr& mp : c.members) {
        if (!mp) continue;
        switch (mp->kind) {
            case NodeKind::ConstructorDeclaration: {
                const auto& cd = static_cast<const ConstructorDeclaration&>(*mp);
                noteStores(cd.body.get(), assigned);
                break;
            }
            case NodeKind::MethodDeclaration: {
                const auto& md = static_cast<const MethodDeclaration&>(*mp);
                noteStores(md.body.get(), assigned);
                break;
            }
            case NodeKind::AccessorDeclaration: {
                const auto& ad = static_cast<const AccessorDeclaration&>(*mp);
                noteStores(ad.body.get(), assigned);
                break;
            }
            default:
                break;
        }
    }
    for (const ClassMemberInfo& m : ci.members) {
        if (m.isMethod) continue;
        if (m.isStatic) continue;
        if (m.hasInitializer || m.definiteAssignment || m.optional) continue;
        if (m.type.isUnion()) {
            bool nullishMember = false;
            for (const Type& part : m.type.parts) {
                if (part.isNullish()) nullishMember = true;
            }
            if (nullishMember) continue;
        }
        if (m.type.isUnknown() || m.type.isError()) continue;  // cascade suppression
        if (assigned.count(m.name) != 0) continue;
        report(diagcode::kUninitializedField, m.span,
               "field '" + m.name +
                   "' is declared without an initializer and is never assigned "
                   "in a constructor; give it an initial value or mark it '!:' "
                   "when initialization happens elsewhere");
    }
}

void Analyzer::resolveClass(ClassDeclaration& c, bool publishBinding) {
    ClassInfo info;
    info.name = c.name;
    info.span = c.span;
    info.declNodeId = c.nodeId;
    info.isAbstract = c.isAbstract;
    for (const auto& tp : c.typeParams) {
        if (tp) info.typeParamNames.push_back(tp->name);
    }
    // A generic class's member types resolve with its parameters in scope.
    TypeParamScope saved;
    enterClassTypeParams(c.typeParams, saved);
    collectClassMembers(c, info);
    leaveClassTypeParams(saved);
    // The doc comments of the class and of its members (class_docs.cpp).
    publishClassDocs(c);
    checkDefiniteAssignment(c, info);
    const std::string name = info.name;
    classes_[name] = std::move(info);
    // The class name is also a value (statics + `new`), declared in the
    // resolve pass so a function above the class can use it. For imported
    // classes the binding is carried by the import resolution, so skip the
    // duplicate declareBinding.
    if (publishBinding) {
        typeNames_[name] = Type::named(TypeKind::Class, name);
        declareBinding(name, typeNames_[name], c.span,
                       BindingSpec{.declNodeId = c.nodeId});
    }
    checkAbstractConformance(c, classes_[name]);
}

void Analyzer::collectClassMembers(const ClassDeclaration& c, ClassInfo& info) {
    if (c.extendsType) {
        Type bt = resolveType(*c.extendsType);
        if (bt.kind == TypeKind::Class && !bt.name.empty()) {
            info.baseName = bt.name;
        }
    }
    for (const TypePtr& t : c.implementsTypes) {
        if (!t) continue;
        resolveType(*t);  // self-documenting only (spec §15)
        // The cycle analysis needs the closed world of an interface-typed
        // field: every class that implements the interface is a candidate
        // holder. The interface name is recorded here (the interface may be
        // declared later in the unit, so the label mapping is resolved in the
        // post-pass).
        if (t->kind == NodeKind::NamedType) {
            info.implementsNames.push_back(
                static_cast<const NamedType&>(*t).name);
        }
    }
    for (const ClassMemberPtr& mp : c.members) {
        if (mp) collectClassMember(*mp, info);
    }
}

// A class member's name: the key of a property, method or accessor. A
// computed key (`[expr]`) names the member at runtime, which a cp3 class
// (a fixed set of members) cannot webserver - unless the key is a literal, the
// one form whose value is known at compile time.
std::string Analyzer::classMemberName(Expr* key, bool computed) {
    if (key == nullptr) return {};
    if (computed && key->kind != NodeKind::StringLiteral &&
        key->kind != NodeKind::NumberLiteral) {
        return {};
    }
    return keyName(key);
}

// One property member: its name/key, modifiers and resolved field type.
void Analyzer::collectClassProperty(PropertyDeclaration& p, ClassInfo& info) {
    std::string name = classMemberName(p.key.get(), p.computed);
    if (name.empty()) {
        report(diagcode::kComputedMemberName, p.span,
               "a computed class member name must be a literal: a cp3 class "
               "has a fixed set of members");
        return;
    }
    ClassMemberInfo mi;
    mi.name = name;
    mi.span = p.span;
    mi.isStatic = p.isStatic;
    mi.isPrivate = p.access == AccessModifier::Private ||
                   (!name.empty() && name.front() == '#');
    mi.isProtected = p.access == AccessModifier::Protected;
    mi.isWeak = p.isWeak;
    mi.isReadonly = p.isReadonly;
    mi.isOverride = p.isOverride;
    mi.hasInitializer = p.initializer != nullptr;
    mi.optional = p.optional;
    mi.definiteAssignment = p.isDefiniteAssignment;
    if (p.type) {
        mi.type = resolveType(*p.type);
    } else if (p.initializer) {
        mi.type = widen(inferExpr(*p.initializer));
    } else {
        mi.type = Type::unknown();
    }
    info.members.push_back(std::move(mi));
}

// One method member: its signature (a same-name method extends the overloads).
void Analyzer::collectClassMethod(MethodDeclaration& md, ClassInfo& info) {
    std::string name = classMemberName(md.key.get(), md.computed);
    if (name.empty()) {
        report(diagcode::kComputedMemberName, md.span,
               "a computed class member name must be a literal: a cp3 class "
               "has a fixed set of members");
        return;
    }
    // A generic method's own type parameters are in scope while its signature
    // is built, so `T` in an annotation resolves to a type parameter.
    TypeParamScope saved;
    enterClassTypeParams(md.typeParams, saved);
    FuncSig sig = buildSig(name, md.params, md.returnType, &md.span);
    leaveClassTypeParams(saved);
    sig.declNodeId = md.nodeId;
    if (md.isAsync) {
        // spec 14: calling an async method yields a Promise of its
        // annotation (mirrors hoistFunction for function declarations).
        sig.isAsync = true;
        if (sig.annotatedReturn && isPromiseType(sig.ret)) {
            sig.asyncValueRet = promiseValue(sig.ret);
        } else if (sig.annotatedReturn && !sig.ret.isUnknown() &&
                   !sig.ret.isError()) {
            // Permissive: sync m(): T behaves like Promise<T>.
            sig.asyncValueRet = sig.ret;
            sig.ret = promiseOf(sig.ret);
        } else {
            sig.annotatedReturn = false;
            sig.ret = promiseOf(Type::unknown());
            sig.asyncValueRet = Type::unknown();
        }
    }
    sigs_[md.nodeId] = sig;
    ClassMemberInfo* prev = nullptr;
    for (ClassMemberInfo& cm : info.members) {
        if (cm.isMethod && cm.name == name) {
            prev = &cm;
            break;
        }
    }
    if (prev) {
        // Overload set: resolution stays permissive, C++ picks.
        if (prev->overloads.empty()) {
            prev->overloads.push_back(prev->sig);
        }
        prev->overloads.push_back(sig);
        prev->type = ambient();
        return;
    }
    ClassMemberInfo mi;
    mi.name = name;
    mi.span = md.span;
    mi.isStatic = md.isStatic;
    mi.isPrivate = md.access == AccessModifier::Private ||
                   (!name.empty() && name.front() == '#');
    mi.isProtected = md.access == AccessModifier::Protected;
    mi.isMethod = true;
    mi.isAbstract = md.isAbstract;
    mi.isReadonly = md.isReadonly;
    mi.isOverride = md.isOverride;
    mi.optional = md.optional;
    mi.sig = sig;
    mi.type = Type::function(sig.params, sig.ret);
    info.members.push_back(std::move(mi));
}

void Analyzer::collectClassMember(ClassMember& m, ClassInfo& info) {
    switch (m.kind) {
        case NodeKind::PropertyDeclaration:
            collectClassProperty(static_cast<PropertyDeclaration&>(m), info);
            return;
        case NodeKind::MethodDeclaration:
            collectClassMethod(static_cast<MethodDeclaration&>(m), info);
            return;
        case NodeKind::ConstructorDeclaration: {
            auto& cd = static_cast<ConstructorDeclaration&>(m);
            TypePtr none;
            FuncSig sig = buildSig("constructor", cd.params, none, nullptr);
            sig.declNodeId = cd.nodeId;
            sigs_[cd.nodeId] = sig;
            info.ctorSig = sig;
            info.hasCtor = true;
            // Constructor parameter properties (`constructor(public x: T)`)
            // declare an instance field and initialize it from the argument.
            for (const auto& pp : cd.params) {
                if (!pp) continue;
                if (pp->access == AccessModifier::None && !pp->isReadonly) {
                    continue;
                }
                if (pp->isRest || !pp->name ||
                    pp->name->kind != NodeKind::Identifier) {
                    report(diagcode::kUnexpectedToken, pp->span,
                           "a constructor parameter property must be a simple "
                           "identifier");
                    continue;
                }
                const auto& id = static_cast<const Identifier&>(*pp->name);
                bool duplicate = false;
                for (const ClassMemberInfo& existing : info.members) {
                    if (existing.name == id.name) duplicate = true;
                }
                if (duplicate) {
                    report(diagcode::kDuplicateMember, pp->span,
                           "duplicate class member '" + id.name + "'");
                    continue;
                }
                ClassMemberInfo field;
                field.name = id.name;
                field.span = pp->span;
                field.isPrivate = pp->access == AccessModifier::Private;
                field.isProtected = pp->access == AccessModifier::Protected;
                field.isReadonly = pp->isReadonly;
                field.hasInitializer = true;
                field.optional = pp->optional;
                if (pp->type) {
                    field.type = resolveType(*pp->type);
                } else if (pp->defaultValue) {
                    field.type = defaultLiteralType(options_.typeSystem,
                                                    inferExpr(*pp->defaultValue));
                } else {
                    report(diagcode::kTypeMismatch, pp->span,
                           "parameter property '" + id.name +
                               "' needs a type annotation or a default value");
                    field.type = Type::unknown();
                }
                if (pp->optional) field.type = nullableIf(true, field.type);
                info.members.push_back(std::move(field));
            }
            return;
        }
        case NodeKind::AccessorDeclaration: {
            auto& ad = static_cast<AccessorDeclaration&>(m);
            // D21: TS-style accessors. The getter's return type governs reads;
            // the setter writes through the same type.
            std::string name = classMemberName(ad.key.get(), ad.computed);
            if (name.empty()) {
                report(diagcode::kComputedMemberName, ad.span,
                       "a computed class member name must be a literal: a cp3 "
                       "class has a fixed set of members");
                return;
            }
            if (ad.accessor != AccessorKind::Get) return;
            ClassMemberInfo mi;
            mi.name = name;
            mi.span = ad.span;
            mi.isStatic = ad.isStatic;
            mi.isPrivate = ad.access == AccessModifier::Private ||
                           (!name.empty() && name.front() == '#');
            mi.isProtected = ad.access == AccessModifier::Protected;
            mi.hasInitializer = true;  // a getter satisfies every read
            mi.type = ad.returnType ? resolveType(*ad.returnType)
                                    : Type::unknown();
            info.members.push_back(std::move(mi));
            return;
        }
        default:
            return;
    }
}

// =============================================================================
// Abstract conformance + bodies
// =============================================================================
void Analyzer::checkClassHierarchy(const ClassDeclaration& c,
                                   const ClassInfo& ci) {
    (void)c;
    const ClassInfo* base =
        ci.baseName.empty() ? nullptr : findClass(ci.baseName);
    for (const ClassMemberInfo& m : ci.members) {
        if (!m.isOverride) continue;
        const ClassMemberInfo* inherited =
            base ? findClassMemberChain(*base, m.name, nullptr) : nullptr;
        if (inherited == nullptr) {
            report(diagcode::kMemberNotFound, m.span,
                   "member '" + m.name + "' is marked override but " +
                       (base ? "base class '" + base->name + "' has no such "
                               "member"
                             : "the class has no base class"));
            continue;
        }
        if (m.isMethod != inherited->isMethod) {
            report(diagcode::kTypeMismatch, m.span,
                   "member '" + m.name +
                       "' cannot override a member of a different kind");
        }
    }
}

void Analyzer::checkClassBodies(ClassDeclaration& c) {
    if (const ClassInfo* ci = findClass(c.name)) {
        checkClassHierarchy(c, *ci);
    }
    if (!findClass(c.name)) return;
    // Generic class bodies check once, with the parameters bound to TypeParam.
    TypeParamScope saved;
    enterClassTypeParams(c.typeParams, saved);
    classStack_.push_back(c.name);
    for (const ClassMemberPtr& mp : c.members) {
        if (mp) checkClassMemberBody(*mp);
    }
    classStack_.pop_back();
    leaveClassTypeParams(saved);
}

void Analyzer::checkClassMemberBody(ClassMember& m) {
    switch (m.kind) {
        case NodeKind::PropertyDeclaration: {
            auto& p = static_cast<PropertyDeclaration&>(m);
            // Unannotated initializers were typed during resolution.
            if (!p.initializer || !p.type) return;
            pushScope();
            declareBinding("this", thisType(), p.span,
                           BindingSpec{.publish = false});
            Type ft = resolveType(*p.type);
            Type it = inferExpr(*p.initializer);
            checkAssign(it, ft, p.initializer->span,
                        p.initializer->kind == NodeKind::ObjectLiteral);
            popScope();
            return;
        }
        case NodeKind::MethodDeclaration: {
            auto& md = static_cast<MethodDeclaration&>(m);
            if (md.body) analyzeMethodBody(md);
            return;
        }
        case NodeKind::ConstructorDeclaration:
            analyzeCtorBody(static_cast<ConstructorDeclaration&>(m));
            return;
        case NodeKind::AccessorDeclaration:
            analyzeAccessorBody(static_cast<AccessorDeclaration&>(m));
            return;
        case NodeKind::StaticBlock: {
            auto& sb = static_cast<StaticBlock&>(m);
            // A static block has no `this`; static members are reached through
            // the class name. It is checked in the enclosing class scope.
            pushScope();
            if (sb.body) visitStmt(*sb.body);
            popScope();
            return;
        }
        default:
            return;
    }
}

void Analyzer::analyzeMethodBody(MethodDeclaration& md) {
    const FuncSig& sig = sigs_[md.nodeId];
    // A generic method's type parameters are in scope in its body (§16).
    TypeParamScope saved;
    enterClassTypeParams(md.typeParams, saved);
    pushScope();
    for (std::size_t i = 0; i < md.params.size() && i < sig.params.size(); ++i) {
        Parameter* p = md.params[i].get();
        if (!p) continue;
        if (p->isRest) {
            declarePattern(p->name.get(), Type::array(sig.params[i]), false,
                           p->nodeId);
        } else {
            declarePattern(p->name.get(), nullableIf(p->optional, sig.params[i]),
                           false, p->nodeId);
        }
    }
    declareBinding("this", thisType(), md.span, BindingSpec{.publish = false});
    FuncCtx ctx;
    ctx.annotated = sig.annotatedReturn;
    // An async method's `return` yields the Promise's value type (spec 14).
    ctx.ret = sig.isAsync ? sig.asyncValueRet : sig.ret;
    ctx.isAsync = sig.isAsync;
    funcStack_.push_back(ctx);
    // A method is a function of its own: a `break`/`continue` inside it binds
    // to its own loops, never to a loop around the class declaration.
    const FunctionContext outer = enterFunctionBody();
    if (md.body) visitStmt(*md.body);
    leaveFunctionBody(outer);
    funcStack_.pop_back();
    popScope();
    if (sig.annotatedReturn && !sig.ret.isVoid() && !sig.ret.isUndefined() &&
        sig.ret.kind != TypeKind::Unknown) {
        if (md.body && !allPathsReturn(md.body.get())) {
            report(diagcode::kMissingReturn, md.span,
                   "method '" + sig.name +
                       "' is declared to return a value but may fall off "
                       "the end of its body");
        }
    }
    leaveClassTypeParams(saved);
}

// The accessor body (D21): `this` plus the setter's parameter are in scope.
void Analyzer::analyzeAccessorBody(AccessorDeclaration& ad) {
    if (!ad.body) return;
    pushScope();
    declareBinding("this", thisType(), ad.span, BindingSpec{.publish = false});
    for (const auto& p : ad.params) {
        if (!p) continue;
        Type t = p->type ? resolveType(*p->type) : Type::unknown();
        if (p->optional && !t.isUnknown()) t = nullableIf(true, t);
        declarePattern(p->name.get(), t, false, p->nodeId);
    }
    FuncCtx ctx;
    ctx.annotated = ad.returnType != nullptr;
    ctx.ret = ad.returnType ? resolveType(*ad.returnType) : Type::unknown();
    funcStack_.push_back(ctx);
    const FunctionContext outer = enterFunctionBody();
    visitStmt(*ad.body);
    leaveFunctionBody(outer);
    funcStack_.pop_back();
    popScope();
}

void Analyzer::analyzeCtorBody(ConstructorDeclaration& cd) {
    const FuncSig& sig = sigs_[cd.nodeId];
    const ClassInfo* ci =
        classStack_.empty() ? nullptr : findClass(classStack_.back());
    const bool derived = ci != nullptr && !ci->baseName.empty();
    pushScope();
    for (std::size_t i = 0; i < cd.params.size() && i < sig.params.size(); ++i) {
        Parameter* p = cd.params[i].get();
        if (!p) continue;
        if (p->isRest) {
            declarePattern(p->name.get(), Type::array(sig.params[i]), false,
                           p->nodeId);
        } else {
            declarePattern(p->name.get(), nullableIf(p->optional, sig.params[i]),
                           false, p->nodeId);
        }
    }
    declareBinding("this", thisType(), cd.span, BindingSpec{.publish = false});
    FuncCtx ctx;
    ctx.annotated = false;
    ctx.ret = Type::voidType();
    ctx.isCtor = true;
    funcStack_.push_back(ctx);
    const FunctionContext outer = enterFunctionBody();
    if (cd.body) visitStmt(*cd.body);
    if (derived && cd.body != nullptr && !funcStack_.back().sawSuper) {
        report(diagcode::kNotCallable, cd.span,
               "constructor of class '" + classStack_.back() +
                   "' must call super(...) because it derives from '" +
                   ci->baseName + "'");
    }
    leaveFunctionBody(outer);
    funcStack_.pop_back();
    popScope();
}

Type Analyzer::thisType() const {
    if (classStack_.empty()) return ambient();
    return Type::named(TypeKind::Class, classStack_.back());
}

bool Analyzer::isErrorType(const Type& type) const {
    if (type.kind != TypeKind::Class) return false;
    const ClassInfo* ci = findClass(type.name);
    int guard = 0;
    while (ci != nullptr && guard++ < 64) {
        if (isErrorClassName(ci->name)) return true;
        ci = ci->baseName.empty() ? nullptr : findClass(ci->baseName);
    }
    return false;
}

// =============================================================================
// Member lookup + access
// =============================================================================
const ClassInfo* Analyzer::findClass(const std::string& name) const {
    const auto it = classes_.find(name);
    return it == classes_.end() ? nullptr : &it->second;
}

const ClassInfo* Analyzer::constructorOwner(const ClassInfo& ci) const {
    const ClassInfo* k = &ci;
    int guard = 0;
    while (k != nullptr && guard++ < 64) {
        if (k->hasCtor) return k;
        k = k->baseName.empty() ? nullptr : findClass(k->baseName);
    }
    return nullptr;
}

const ClassMemberInfo* Analyzer::findClassMember(const ClassInfo& ci,
                                                 const std::string& name) const {
    for (const ClassMemberInfo& m : ci.members) {
        if (m.name == name) return &m;
    }
    return nullptr;
}

const ClassMemberInfo* Analyzer::findClassMemberChain(
    const ClassInfo& ci, const std::string& name, const ClassInfo** owner) const {
    const ClassInfo* k = &ci;
    int guard = 0;
    while (k && guard++ < 64) {
        const ClassMemberInfo* m = findClassMember(*k, name);
        if (m) {
            if (owner) *owner = k;
            return m;
        }
        k = k->baseName.empty() ? nullptr : findClass(k->baseName);
    }
    if (owner) *owner = nullptr;
    return nullptr;
}

bool Analyzer::classIsOrDerivesFrom(const ClassInfo& ci,
                                    const std::string& base) const {
    const ClassInfo* k = &ci;
    int guard = 0;
    while (k && guard++ < 64) {
        if (k->name == base) return true;
        k = k->baseName.empty() ? nullptr : findClass(k->baseName);
    }
    return false;
}

bool Analyzer::classAccessAllowed(const ClassInfo& owner,
                                  const ClassMemberInfo& m) const {
    if (!m.isPrivate && !m.isProtected) return true;
    for (const std::string& frame : classStack_) {
        if (m.isPrivate) {
            if (frame == owner.name) return true;
        } else {
            const ClassInfo* f = findClass(frame);
            if (f && classIsOrDerivesFrom(*f, owner.name)) return true;
        }
    }
    return false;
}

Type Analyzer::classMemberType(const ClassMemberInfo& m) const {
    if (!m.isMethod) return m.type;
    if (!m.overloads.empty()) return ambient();
    Type t = Type::function(m.sig.params, m.sig.ret);
    // A rest parameter takes an unbounded argument list, which the fixed-arity
    // function type cannot webserver: the tag widens the signature's `rest` flag
    // at the call site (the marker the variadic array mutators use, scopes.h).
    if (m.sig.rest) t.name = kVariadicOptionalTailLabel;
    return t;
}

Type Analyzer::userClassMember(const Type& cls, const std::string& prop,
                               const SourceSpan& span) {
    const ClassInfo* ci = findClass(cls.name);
    if (!ci) return ambient();
    const ClassInfo* owner = nullptr;
    const ClassMemberInfo* m = findClassMemberChain(*ci, prop, &owner);
    if (!m) {
        if (assignTarget_) {
            report(diagcode::kUndeclaredProperty, span,
                   "cannot assign to undeclared property '" + prop +
                       "' of class '" + ci->name + "'");
        } else {
            std::vector<std::string> candidates;
            candidates.reserve(ci->members.size());
            for (const ClassMemberInfo& cm : ci->members) {
                candidates.push_back(cm.name);
            }
            reportMemberMiss("class '" + ci->name + "'", prop, span, candidates);
        }
        return Type::unknown();
    }
    if (!classAccessAllowed(*owner, *m)) {
        report(diagcode::kMemberNotFound, span,
               "property '" + prop + "' of class '" + owner->name + "' is " +
                   (m->isPrivate ? "private" : "protected"));
        return Type::unknown();
    }
    if (assignTarget_ && m->isReadonly) {
        const bool inOwnConstructor =
            !classStack_.empty() && classStack_.back() == owner->name &&
            !funcStack_.empty() && funcStack_.back().isCtor;
        if (!inOwnConstructor) {
            report(diagcode::kTypeMismatch, span,
                   "cannot assign to readonly property '" + prop +
                       "' outside its class constructor");
            return Type::unknown();
        }
    }
    Type t = classMemberType(*m);
    // A generic class instantiation substitutes its type arguments into the
    // member type (`Box<number>.value` is number, not T).
    if (!ci->typeParamNames.empty() && !cls.parts.empty()) {
        t = substituteClassTypeParams(t, *ci, cls.parts);
    }
    if (m->optional && !hasNullish(t)) {
        // A `?` member reads as `T | undefined` exactly like `weak` fields
        // (`it.note === undefined` is then a normal comparison, not a
        // differing-kinds tuple).
        t = unify(t, Type::undefined());
    }
    if (m->isWeak && !assignTarget_) {
        weakFieldHit_ = true;
        if (!hasNullish(t)) t = unify(t, Type::undefined());
    } else if (kAutoWeakGateEnabled && !assignTarget_ && owner != nullptr) {
        // Stage 2 (report-only until the P5+P7 gate passes): a proven
        // compiler-injected edge reads exactly like a manual `weak` field
        // (`T | undefined`, so member access through it needs narrowing).
        // While the gate is off this branch is dead and reads stay strong.
        const CycleGraphInfo graph = buildCycleGraph(classes_);
        const auto decisions =
            selectAutoWeak(graph, cycleGroupsOf(graph, cycleCapableOf(graph, classes_)));
        const auto it = decisions.find(owner->name + "." + m->name);
        if (it != decisions.end() && it->second.state == "proven") {
            weakFieldHit_ = true;
            if (!hasNullish(t)) t = unify(t, Type::undefined());
        }
    }
    return t;
}

// -----------------------------------------------------------------------------
// The whole-program reference graph behind the automatic cycle collector.
//
// The Stage 1 analysis above answers "can this class reach itself?" with a
// reachability walk. The collector needs more: the strongly connected
// components of the whole reference graph, the concrete field path that
// forms each cycle, and a verdict per class (cycle-capable vs. acyclic, i.e.
// zero collector cost). That is the analysis in detail/cycle_analysis.h.
//
// This pass is report-only: it publishes the side table and the report
// lines, and changes no ownership. The CP3401 cycle diagnostic keeps its
// current verdict until the collector lands.
// -----------------------------------------------------------------------------
void Analyzer::computeCycleComponents() {
    CycleAnalysisInput in;
    // The interface implementers: a class that implements an interface is a
    // candidate holder of an interface-typed field (the closed world). The
    // interface is a structural type, so it is identified by its label.
    std::unordered_map<std::string, std::vector<std::string>> implementers;
    for (const auto& [name, info] : classes_) {
        for (const std::string& iface : info.implementsNames) {
            const auto it = typeNames_.find(iface);
            if (it == typeNames_.end()) continue;
            for (const std::string& labeled : interfacesForLabel(it->second.name)) {
                implementers[labeled].push_back(name);
            }
        }
    }
    for (auto& [name, list] : implementers) {
        (void)name;
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }
    for (const auto& [name, info] : classes_) {
        in.classIds[name] = info.declNodeId;
        if (!info.baseName.empty()) in.baseOf[name] = info.baseName;
        for (const ClassMemberInfo& m : info.members) {
            if (m.isMethod) continue;
            CycleAnalysisInput::FieldEdge edge;
            edge.owner = name;
            edge.field = m.name;
            edge.type = m.type;
            edge.weak = m.isWeak;
            edge.isStatic = m.isStatic;
            in.fields.push_back(std::move(edge));
        }
    }
    in.implementersOf = implementers;
    in.interfaceByLabel = interfaceByLabel_;
    const CycleAnalysis analysis = analyzeCycles(in);
    out_.cycleCapable = analysis.cycleCapable;
    out_.cycleEligible = analysis.cycleEligible;
    out_.collectorEligibleClasses = analysis.collectorEligibleClasses;
    out_.cycleComponents.clear();
    for (const CycleComponent& component : analysis.components) {
        if (!component.cycleCapable) continue;
        SemaResult::CycleComponentInfo info;
        info.classes = component.labels;
        info.paths = component.paths;
        info.hasOpaque = component.hasOpaque;
        out_.cycleComponents.push_back(std::move(info));
    }
    out_.acyclicClasses = analysis.acyclicClasses;
}

// -----------------------------------------------------------------------------
// Stage 1: cycle-capability analysis (automatic cycle handling).
//
// Builds the strong-edge class graph from field types (arrays, Map/Set
// values, unions, function parts and closures included; `weak` fields are not
// strong edges) and marks every class that can reach itself through strong
// edges. An `unknown`-typed field is an opaque sink: it marks its source
// cycle-capable (fail closed). Report-only in v0.2: the set is published to
// SemaResult::cycleCapableClasses; no diagnostic is emitted from it yet, and
// plain refcounting is unchanged for every type.
//
// Stage 3 (below, inside computeCycleCapable): region inference + group
// counting per cycle group. Both verdicts are fail-closed candidates
// published in SemaResult::cycleGroups / cycleRegions / cycleGroupCounts;
// no codegen consumes them yet, so plain refcounting is still unchanged for
// every type. See docs/design/cycle-regions.md.
// -----------------------------------------------------------------------------
void Analyzer::computeCycleCapable() {
    std::unordered_map<std::string, std::unordered_set<std::string>> graph;
    std::unordered_set<std::string> unknownRisk;
    // Weak edges inside each SCC: a cycle group with one of these is broken
    // by the user and needs no diagnostic.
    std::unordered_set<std::string> weakEdges;  // "A->B"
    std::function<void(const Type&, std::unordered_set<std::string>&)> walk =
        [&](const Type& t, std::unordered_set<std::string>& out) {
            if (t.isUnknown()) {
                out.insert("$unknown");
                return;
            }
            if (t.kind == TypeKind::Class) {
                if (classes_.count(t.name)) out.insert(t.name);
            }
            for (const Type& p : t.parts) walk(p, out);
        };
    for (const auto& [name, info] : classes_) {
        auto& edges = graph[name];
        for (const auto& m : info.members) {
            if (m.isMethod || m.isStatic) continue;
            std::unordered_set<std::string> refs;
            walk(m.type, refs);
            if (m.isWeak) {
                for (const std::string& r : refs) {
                    if (classes_.count(r)) weakEdges.insert(name + "->" + r);
                }
                continue;  // weak edges do not count toward cycles
            }
            for (const std::string& r : refs) {
                if (r == "$unknown") {
                    unknownRisk.insert(name);
                    out_.cycleEscapeSites.push_back(
                        name + "." + m.name + " : unknown (escape: opaque)");
                } else if (classes_.count(r)) {
                    edges.insert(r);
                    if (cycleCapableClasses_.count(r) ||
                        graph.count(r)) {
                        out_.cycleEscapeSites.push_back(
                            name + "." + m.name + " : " + r +
                            " (store: field)");
                    }
                }
            }
        }
    }
    // Reachability: a class is cycle-capable when it can reach itself through
    // strong edges (covers self-loops and mutual/longer cycles).
    cycleCapableClasses_.clear();
    auto reaches = [&](const std::string& from, const std::string& target,
                       std::unordered_set<std::string>& seen) {
        std::vector<std::string> work{from};
        while (!work.empty()) {
            std::string cur = work.back();
            work.pop_back();
            if (cur == target) return true;
            if (!seen.insert(cur).second) continue;
            for (const std::string& nxt : graph[cur]) work.push_back(nxt);
        }
        return false;
    };
    for (const auto& [name, info] : classes_) {
        (void)info;
        if (unknownRisk.count(name)) {
            cycleCapableClasses_.insert(name);
            continue;
        }
        std::unordered_set<std::string> seen;
        // Seed with direct successors so a self-loop is found.
        std::vector<std::string> work(graph[name].begin(), graph[name].end());
        bool reachesSelf = false;
        while (!work.empty() && !reachesSelf) {
            std::string cur = work.back();
            work.pop_back();
            if (cur == name) {
                reachesSelf = true;
                break;
            }
            if (!seen.insert(cur).second) continue;
            for (const std::string& nxt : graph[cur]) work.push_back(nxt);
        }
        if (reachesSelf) cycleCapableClasses_.insert(name);
    }
    // Cycle groups: the strongly-connected components of
    // cycleCapableClasses (mutual reachability), singleton self-loops
    // included. Shared by the step-4 diagnostics and the stage-3 verdicts.
    std::vector<std::vector<std::string>> groups;
    {
        std::unordered_set<std::string> assigned;
        for (const std::string& name : cycleCapableClasses_) {
            if (assigned.count(name)) continue;
            std::vector<std::string> group{name};
            assigned.insert(name);
            for (const std::string& other : cycleCapableClasses_) {
                if (other == name || assigned.count(other)) continue;
                std::unordered_set<std::string> s1, s2;
                if (reaches(name, other, s1) && reaches(other, name, s2)) {
                    group.push_back(other);
                    assigned.insert(other);
                }
            }
            std::sort(group.begin(), group.end());
            groups.push_back(std::move(group));
        }
        std::sort(groups.begin(), groups.end(),
                  [](const std::vector<std::string>& a,
                     const std::vector<std::string>& b) {
                      return a.front() < b.front();
                  });
    }
    // -----------------------------------------------------------------
    // Stage 2: compiler-injected non-owning edges (auto-weak).
    //
    // Selects ONE candidate edge per unresolved SCC group (P1-P4) and
    // publishes every decision in SemaResult::cycleEscapeSites, which
    // `cp3 check --report` already prints: this is the `--report`
    // auto-weak preview. The line format is the sema<->codegen contract:
    //
    //   auto-weak Src.field -> Dst : candidate (escape: none)
    //   auto-weak Src.field -> Dst : proven (escape: none)
    //   auto-weak Src.field -> Dst : rejected (reason: container-edge)
    //
    // Only `proven` may suppress CP3401 below and lower as `WeakRef` in
    // codegen. While kAutoWeakGateEnabled is false (P5 body escapes and
    // P7 fuzzer/ASan gate outstanding) every chosen edge is only a
    // `candidate`: report-only, CP3401 is kept, codegen is unchanged.
    // -----------------------------------------------------------------
    std::unordered_map<std::string, AutoWeakDecision> autoWeakDecisions;
    std::unordered_map<std::string, std::string> autoWeakDst;
    {
        const CycleGraphInfo info = buildCycleGraph(classes_);
        const std::vector<std::vector<std::string>> sccGroups =
            cycleGroupsOf(info, cycleCapableOf(info, classes_));
        autoWeakDecisions = selectAutoWeak(info, sccGroups);
        for (const CycleEdgeInfo& e : info.edges) {
            autoWeakDst.emplace(e.src + "." + e.field, e.dst);
        }
        std::vector<std::string> lines;
        for (const auto& [key, decision] : autoWeakDecisions) {
            const auto dit = autoWeakDst.find(key);
            if (dit == autoWeakDst.end()) continue;
            std::string line =
                "auto-weak " + key + " -> " + dit->second + " : " + decision.state;
            if (decision.state == "rejected") {
                line += " (reason: " + decision.reason + ")";
            } else {
                line += " (escape: none)";
            }
            lines.push_back(line);
        }
        std::sort(lines.begin(), lines.end());
        for (const std::string& line : lines) {
            out_.cycleEscapeSites.push_back(line);
        }
    }
    // Step 4 diagnostics: a cycle through two or more distinct edges (SCC of
    // size > 1) with no `weak` edge inside it is a compile error naming the
    // cycle and asking the user to mark one edge weak. Single self-referencing
    // fields (`next: Node | null`) are exempt: recursive types are not
    // runtime cycles and linked lists/trees must keep working.
    for (const auto& group : groups) {
        if (group.size() <= 1) continue;
        std::unordered_set<std::string> inGroup(group.begin(), group.end());
        bool hasWeak = false;
        for (const std::string& e : weakEdges) {
            const std::string arrow = "->";
            const std::size_t pos = e.find(arrow);
            if (pos == std::string::npos) continue;
            if (inGroup.count(e.substr(0, pos)) &&
                inGroup.count(e.substr(pos + arrow.size())))
                hasWeak = true;
        }
        if (hasWeak) continue;
        // A collector-eligible SCC is reclaimed automatically at the loop
        // safe points once it becomes unreachable (docs/design/cycle-collector.md):
        // no manual `weak` edge is required, so no diagnostic is emitted.
        // Partial eligibility keeps CP3401 (fail-closed).
        bool allEligible = true;
        for (const std::string& name : group) {
            const ClassInfo* ci = findClass(name);
            if (ci == nullptr) { allEligible = false; break; }
            const auto it = out_.cycleEligible.find(ci->declNodeId);
            if (it == out_.cycleEligible.end() || !it->second) {
                allEligible = false;
                break;
            }
        }
        if (allEligible) continue;
        // Stage 2: a proven compiler-injected edge (gate on, P1-P7 met)
        // breaks the cycle with weak-cost zero, so CP3401 is suppressed.
        // A mere candidate keeps CP3401 (report-only default) and is named
        // in the diagnostic as the would-be automatic edge.
        std::string autoWeakPick;
        bool autoWeakProven = false;
        for (const auto& [key, decision] : autoWeakDecisions) {
            const std::size_t dot = key.find('.');
            if (dot == std::string::npos) continue;
            if (!inGroup.count(key.substr(0, dot))) continue;
            if (decision.state == "proven") {
                autoWeakProven = true;
                autoWeakPick = key;
                break;
            }
            if (decision.state == "candidate" && autoWeakPick.empty()) {
                autoWeakPick = key;
            }
        }
        if (autoWeakProven) continue;
        std::string path;
        for (std::size_t i = 0; i < group.size(); ++i) {
            if (i) path += " -> ";
            path += group[i];
        }
        path += " -> " + group.front();
        const auto it = classes_.find(group.front());
        const SourceSpan span = it != classes_.end() ? it->second.span
                                                     : SourceSpan{};
        report(diagcode::kTypeMismatch, span,
               "reference cycle " + path +
                   " has no weak edge; mark one edge weak to break the "
                   "cycle (automatic resolution unavailable for this "
                   "shape)" +
                   (autoWeakPick.empty()
                        ? std::string()
                        : "; auto-weak candidate " + autoWeakPick +
                              " -> " + autoWeakDst[autoWeakPick] +
                              " (report-only: whole-program ownership proof "
                              "not yet available)"));
    }
    // -----------------------------------------------------------------
    // Stage 3: region inference + group counting per cycle group.
    //
    // Region inference (bump-arena candidates): a group is a candidate
    // only when every fact a bump arena needs is provable from the class
    // graph alone - closed group (no strong edge leaves it), no opaque
    // `unknown` field, no static field (a program-lifetime root), no
    // function-typed field referencing the group (a closure can hold a
    // hidden reference), no generic/abstract member (the concrete shape
    // is not closed), no base-class edge in either direction (inheritance
    // is an invisible strong edge), and no user `weak` edge (already
    // resolved manually). Anything else, or anything unprovable, fails
    // closed to `regionKind == "none"`: no region, plain refcounting.
    // Candidacy is NOT activation: codegen must still prove the group's
    // allocation sites are function-local before placing them in an
    // arena (see docs/design/cycle-regions.md).
    //
    // Group counting (refcount elision per group): one group counter
    // replaces the per-object counters of the group's members; strong
    // edges inside the group - and only those - stop touching any
    // counter. Sound under the same fail-closed facts as above, except
    // external strong edges and a user `weak` edge are allowed: external
    // edges are the group's roots (they acquire/release the group
    // counter) and a `weak` edge never retained anything.
    // -----------------------------------------------------------------
    {
        // True when `t` carries a function-typed part that references a
        // class of `inGroup`: a closure field is a hidden reference the
        // field graph does not count, so it defeats both verdicts.
        std::function<bool(const Type&, const std::unordered_set<std::string>&)>
            hasClosureRef = [&](const Type& t,
                                  const std::unordered_set<std::string>& inGroup)
                -> bool {
            if (t.kind == TypeKind::Function) {
                std::unordered_set<std::string> refs;
                walk(t, refs);
                for (const std::string& r : refs) {
                    if (inGroup.count(r)) return true;
                }
            }
            for (const Type& p : t.parts) {
                if (hasClosureRef(p, inGroup)) return true;
            }
            return false;
        };
        for (const auto& group : groups) {
            SemaResult::CycleGroupInfo info;
            info.classes = group;
            const std::unordered_set<std::string> inGroup(group.begin(),
                                                          group.end());
            bool hasStaticRef = false;
            bool hasClosure = false;
            bool genericOrAbstract = false;
            for (const std::string& name : group) {
                const auto it = classes_.find(name);
                if (it == classes_.end()) continue;
                const ClassInfo& ci = it->second;
                if (ci.isAbstract || !ci.typeParamNames.empty()) {
                    genericOrAbstract = true;
                }
                for (const ClassMemberInfo& m : ci.members) {
                    if (m.isMethod) continue;
                    std::unordered_set<std::string> refs;
                    walk(m.type, refs);
                    for (const std::string& r : refs) {
                        if (!inGroup.count(r)) continue;
                        if (m.isStatic) hasStaticRef = true;
                        if (hasClosureRef(m.type, inGroup)) hasClosure = true;
                    }
                }
            }
            // Inheritance is an invisible strong edge in both directions:
            // a group member's base outside the group, or a derived class
            // outside the group inheriting a group member's fields.
            bool baseLeaks = false;
            for (const std::string& name : group) {
                const auto it = classes_.find(name);
                if (it == classes_.end()) continue;
                const std::string& base = it->second.baseName;
                if (!base.empty() && !inGroup.count(base)) baseLeaks = true;
            }
            for (const auto& [name, ci] : classes_) {
                if (inGroup.count(name)) continue;
                if (inGroup.count(ci.baseName)) baseLeaks = true;
            }
            // A closed region also needs no incoming strong edge: a heap
            // object outside the group holding a group class can outlive
            // any function-local arena, and sema has no allocation-site
            // proof to rule that out. Static fields of outside classes
            // referencing the group are the same root in stronger form.
            // (Group counting is unaffected: incoming edges are roots.)
            bool hasIncoming = false;
            for (const auto& [name, edges] : graph) {
                if (inGroup.count(name)) continue;
                for (const std::string& tgt : edges) {
                    if (inGroup.count(tgt)) hasIncoming = true;
                }
            }
            for (const auto& [name, ci] : classes_) {
                if (inGroup.count(name)) continue;
                for (const ClassMemberInfo& m : ci.members) {
                    if (m.isMethod || !m.isStatic) continue;
                    std::unordered_set<std::string> refs;
                    walk(m.type, refs);
                    for (const std::string& r : refs) {
                        if (inGroup.count(r)) hasIncoming = true;
                    }
                }
            }
            for (const std::string& name : group) {
                if (unknownRisk.count(name)) ++info.unknownRefs;
                const auto git = graph.find(name);
                if (git == graph.end()) continue;
                for (const std::string& tgt : git->second) {
                    if (inGroup.count(tgt)) {
                        ++info.strongInternalEdges;
                    } else {
                        ++info.externalStrongEdges;
                    }
                }
            }
            for (const std::string& e : weakEdges) {
                const std::string arrow = "->";
                const std::size_t pos = e.find(arrow);
                if (pos == std::string::npos) continue;
                if (inGroup.count(e.substr(0, pos)) &&
                    inGroup.count(e.substr(pos + arrow.size())))
                    ++info.weakInternalEdges;
            }
            info.hasWeakEdge = info.weakInternalEdges > 0;
            std::string label;
            for (std::size_t i = 0; i < group.size(); ++i) {
                if (i) label += ",";
                label += group[i];
            }
            // Fail-closed reasons, in a fixed priority order so the
            // report is deterministic. The first applicable reason wins.
            std::string failReason;
            if (info.unknownRefs > 0) {
                failReason = "unknown-typed field (opaque, unprovable)";
            } else if (hasStaticRef) {
                failReason = "static field stores a group class (program root)";
            } else if (hasClosure) {
                failReason = "function-typed field references the group (closure escape unknown)";
            } else if (genericOrAbstract) {
                failReason = "generic or abstract class in group (shape not closed)";
            } else if (baseLeaks) {
                failReason = "base-class edge crosses the group boundary (inherited fields invisible)";
            }
            info.regionEligible = failReason.empty() &&
                                  info.externalStrongEdges == 0 &&
                                  !hasIncoming && !info.hasWeakEdge;
            if (info.regionEligible) {
                info.regionKind = "bump-arena-candidate";
                info.regionReason =
                    "closed group, short-lived candidate (codegen still "
                    "must prove function-local allocation sites)";
            } else {
                info.regionKind = "none";
                if (!failReason.empty()) {
                    info.regionReason = failReason;
                } else if (info.externalStrongEdges > 0) {
                    info.regionReason =
                        "strong edge leaves the group (not a closed region)";
                } else if (hasIncoming) {
                    info.regionReason =
                        "strong edge enters the group from outside (root lifetime unprovable)";
                } else {
                    info.regionReason =
                        "user weak edge already breaks the cycle (manual resolution)";
                }
            }
            info.groupCountEligible = failReason.empty();
            if (info.groupCountEligible) {
                info.countReason =
                    std::to_string(info.strongInternalEdges) +
                    " internal strong edge(s) elided" +
                    (info.hasWeakEdge ? "; weak edge never counted" : "");
            } else {
                info.countReason = failReason;
            }
            out_.cycleGroups.push_back(info);
            if (info.regionKind != "none") {
                out_.cycleRegions.push_back(label + " : " + info.regionKind +
                                            " (" + info.regionReason + ")");
            }
            out_.cycleGroupCounts.push_back(
                label + " : " +
                (info.groupCountEligible ? "group-count eligible ("
                                         : "group-count ineligible (") +
                info.countReason + ")");
        }
        std::sort(out_.cycleGroups.begin(), out_.cycleGroups.end(),
                  [](const SemaResult::CycleGroupInfo& a,
                     const SemaResult::CycleGroupInfo& b) {
                      return a.classes.front() < b.classes.front();
                  });
        std::sort(out_.cycleRegions.begin(), out_.cycleRegions.end());
        std::sort(out_.cycleGroupCounts.begin(), out_.cycleGroupCounts.end());
    }
    out_.cycleCapableClasses.assign(cycleCapableClasses_.begin(),
                                    cycleCapableClasses_.end());
    std::sort(out_.cycleCapableClasses.begin(), out_.cycleCapableClasses.end());
    std::sort(out_.cycleEscapeSites.begin(), out_.cycleEscapeSites.end());
}

}  // namespace cp3
