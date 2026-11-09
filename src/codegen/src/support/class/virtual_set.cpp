// =============================================================================
// src/codegen/src/support/class/virtual_set.cpp - program-wide override
// analysis (detail/class/virtual_set.h).
//
// Walks the ASTs of every unit of the program, collects every class declaration
// (name, base name, method members) and decides which members lower as C++
// `virtual`:
//
//   * pair rule - for every pair of classes (A, D) where D is declared in A's
//     transitive base chain (or a duplicate-name candidate for a link of that
//     chain, which is the only way a duplicate name can hide the true base),
//     and both declare the same method name with at least one common arity,
//     EVERY same-name member of both classes is marked `virtual` (an override
//     is a name correspondence; collapsing the same-name overloads of both
//     classes is the safe conservative reading);
//   * abstract rule - every method declared `abstract` is marked `virtual`,
//     since an abstract member is meant to be dispatched to its overrider;
//   * fail closed - a class whose base chain contains a link the analysis
//     cannot resolve exactly (an `extends` expression, a base name that names
//     no class of the program, a duplicate class name elsewhere in the
//     program marks the base itself ambiguous, a base cycle) gets ALL of its
//     own non-static, non-private methods marked `virtual`, and the
//     same-name members of every possible candidate for the ambiguous base are
//     marked `virtual` through the pair rule.
//
// Deliberately excluded (dispatch cannot reach them in C++): static members,
// private members (its `#name` form hides it at runtime exactly as in JS; its
// TS `private` form is a static-binding concession), constructors, accessors
// (JS method dispatch does not cover accessors) and fields. Overloads with the
// same name but no common arity between the two classes are not overrides;
// they keep static binding.
//
// Generic classes are modeled like ordinary ones: an override relation does not
// depend on type arguments (`Box<T> { m() }` overridden in `IntBox extends
// Box<number>` lowers to a virtual member of the C++ class template), so no
// blanket rule applies to them. The one generics form that is genuinely
// unresolvable - an `extends` clause naming a type parameter or an expression
// (`class G<T> extends T`) - falls under the fail-closed rule and marks every
// method of G `virtual`.
//
// Key format: `<cp3ClassName>::<cp3MethodName>` ("Animal::speak"). cp3 nominal
// typing keeps class names unique per unit; a duplicate name elsewhere in the
// program cannot produce an unmarked override - it makes every candidate
// ambiguous, which is exactly the fail-closed case.
// =============================================================================
#include "detail/class/virtual_set.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cp3/core/ast.hpp"
#include "detail/class/class_model.h"

namespace cp3 {
namespace detail {

namespace {

// -----------------------------------------------------------------------------
// Collection: the classes of the program and their method members
// -----------------------------------------------------------------------------

struct MethodFact {
    std::string name;
    std::size_t arity = 0;  // rest parameter counts as one
    bool isAbstract = false;
};

struct ClassFact {
    std::string name;
    // The `extends` target as a simple name. `extendsExpr`/`extendsType` that
    // is not a plain `NamedType` is a base the analysis cannot resolve (an
    // expression, a union, a type parameter) and fails closed.
    std::string baseName;             // "" for none (or unresolvable)
    bool baseExtendsExpr = false;     // `extends <expr>`
    bool baseExtendsComplex = false;  // `extends <non-NamedType>`
    std::vector<MethodFact> methods;  // non-static, non-private, named
};

// `keyNameOf`-compatible spelling of a method's name ("" when not fixed); the
// model collector uses the same spelling, so both sides key identically.
std::string methodNameOf(const MethodDeclaration& md) {
    return keyNameOf(md.key.get());
}

void addMethodFacts(const ClassDeclaration& c, ClassFact& fact) {
    for (const ClassMemberPtr& mp : c.members) {
        if (!mp || mp->kind != NodeKind::MethodDeclaration) continue;
        const auto& md = static_cast<const MethodDeclaration&>(*mp);
        if (md.isStatic) continue;
        const std::string name = methodNameOf(md);
        // A private member (`private` modifier or a `#name`) is not dispatched.
        if (md.access == AccessModifier::Private ||
            (!name.empty() && name.front() == '#')) {
            continue;
        }
        if (name.empty()) continue;  // a computed name is a program error
        MethodFact m;
        m.name = name;
        m.arity = md.params.size();
        m.isAbstract = md.isAbstract;
        fact.methods.push_back(std::move(m));
    }
}

ClassFact collectClassFact(const ClassDeclaration& c) {
    ClassFact fact;
    fact.name = c.name;
    if (c.extendsExpr) {
        fact.baseExtendsExpr = true;
    } else if (c.extendsType) {
        if (c.extendsType->kind == NodeKind::NamedType) {
            fact.baseName = static_cast<const NamedType&>(*c.extendsType).name;
        } else {
            fact.baseExtendsComplex = true;
        }
    }
    addMethodFacts(c, fact);
    return fact;
}

// The walker: every class declaration in the program, at any nesting depth.
// It mirrors the model walker's disciplines (support/class/class_walk.cpp) and
// is deliberately a superset of it - it also descends into static blocks,
// parameter default expressions and computed keys, anywhere a class
// declaration could be hidden. Missing one would leave its overrides undispatched.
struct Collector {
    std::vector<const ClassDeclaration*> classes;
    void add(const ClassDeclaration& c) { classes.push_back(&c); }
};

void scanExpr(const Expr* e, Collector& col);
void scanBody(const StmtPtr& body, Collector& col);
void walkPattern(const Node* n, Collector& col);
void scanStmt(const Stmt& s, Collector& col);

void walkParams(const ParamList& params, Collector& col) {
    for (const ParameterPtr& p : params) {
        if (!p) continue;
        walkPattern(p->name.get(), col);
        scanExpr(p->defaultValue.get(), col);
    }
}

void scanClassMembers(const ClassDeclaration& c, Collector& col) {
    for (const ClassMemberPtr& mp : c.members) {
        if (!mp) continue;
        switch (mp->kind) {
            case NodeKind::PropertyDeclaration: {
                const auto& p = static_cast<const PropertyDeclaration&>(*mp);
                scanExpr(p.key.get(), col);  // a computed key is an expression
                scanExpr(p.initializer.get(), col);
                break;
            }
            case NodeKind::MethodDeclaration: {
                const auto& md = static_cast<const MethodDeclaration&>(*mp);
                scanExpr(md.key.get(), col);
                walkParams(md.params, col);
                scanBody(md.body, col);
                break;
            }
            case NodeKind::ConstructorDeclaration: {
                const auto& cd = static_cast<const ConstructorDeclaration&>(*mp);
                walkParams(cd.params, col);
                scanBody(cd.body, col);
                break;
            }
            case NodeKind::AccessorDeclaration: {
                const auto& ad = static_cast<const AccessorDeclaration&>(*mp);
                scanExpr(ad.key.get(), col);
                walkParams(ad.params, col);
                scanBody(ad.body, col);
                break;
            }
            case NodeKind::StaticBlock:
                // A class declared in a static block is below the model
                // walker's reach; this analysis must not be.
                scanBody(static_cast<const StaticBlock&>(*mp).body, col);
                break;
            default:
                break;
        }
    }
}

void walkPattern(const Node* n, Collector& col) {
    if (n == nullptr) return;
    switch (n->kind) {
        case NodeKind::Identifier:
            return;
        case NodeKind::ArrayPattern: {
            const auto& ap = static_cast<const ArrayPattern&>(*n);
            for (const NodePtr& el : ap.elements) walkPattern(el.get(), col);
            walkPattern(ap.rest.get(), col);
            return;
        }
        case NodeKind::ObjectPattern: {
            const auto& op = static_cast<const ObjectPattern&>(*n);
            for (const ObjectPatternPropertyPtr& prop : op.properties) {
                if (!prop) continue;
                scanExpr(prop->key.get(), col);  // a computed key is an expr
                walkPattern(prop->value.get(), col);
            }
            walkPattern(op.rest.get(), col);
            return;
        }
        case NodeKind::RestElement:
            walkPattern(static_cast<const RestElement&>(*n).argument.get(), col);
            return;
        case NodeKind::AssignmentPattern: {
            const auto& asn = static_cast<const AssignmentPattern&>(*n);
            walkPattern(asn.left.get(), col);
            scanExpr(asn.right.get(), col);  // a pattern default is an expr
            return;
        }
        default:
            return;
    }
}

void scanBody(const StmtPtr& body, Collector& col) {
    if (body) scanStmt(*body, col);
}

void scanStmt(const Stmt& s, Collector& col) {
    switch (s.kind) {
        case NodeKind::ClassDeclaration: {
            const auto& c = static_cast<const ClassDeclaration&>(s);
            if (!c.name.empty()) col.add(c);
            // `extends <expr>` and decorator expressions are expressions; they
            // can nest arrows returning a class.
            scanExpr(c.extendsExpr.get(), col);
            for (const ExprPtr& d : c.decorators) scanExpr(d.get(), col);
            scanClassMembers(c, col);
            return;
        }
        case NodeKind::ExportDeclaration: {
            const auto& ed = static_cast<const ExportDeclaration&>(s);
            if (ed.declaration) scanStmt(*ed.declaration, col);
            scanExpr(ed.defaultExpr.get(), col);
            // The declaration `export default class ...` nests its (static
            // block) members through the class path above.
            return;
        }
        case NodeKind::BlockStatement:
            for (const StmtPtr& child : static_cast<const BlockStatement&>(s).body) {
                if (child) scanStmt(*child, col);
            }
            return;
        case NodeKind::expressionstatement:
            scanExpr(static_cast<const expressionstatement&>(s).expression.get(),
                     col);
            return;
        case NodeKind::VariableDeclaration:
            for (const VariableDeclaratorPtr& d :
                 static_cast<const VariableDeclaration&>(s).declarations) {
                if (d) {
                    walkPattern(d->id.get(), col);
                    scanExpr(d->init.get(), col);
                }
            }
            return;
        case NodeKind::IfStatement: {
            const auto& is = static_cast<const IfStatement&>(s);
            scanExpr(is.condition.get(), col);
            scanBody(is.thenBranch, col);
            scanBody(is.elseBranch, col);
            return;
        }
        case NodeKind::WhileStatement: {
            const auto& ws = static_cast<const WhileStatement&>(s);
            scanExpr(ws.condition.get(), col);
            scanBody(ws.body, col);
            return;
        }
        case NodeKind::DoWhileStatement: {
            const auto& dw = static_cast<const DoWhileStatement&>(s);
            scanBody(dw.body, col);
            scanExpr(dw.condition.get(), col);
            return;
        }
        case NodeKind::ForStatement: {
            const auto& f = static_cast<const ForStatement&>(s);
            if (f.init) {
                if (f.init->kind == NodeKind::VariableDeclaration) {
                    scanStmt(*static_cast<const Stmt*>(f.init.get()), col);
                } else {
                    scanExpr(dynamic_cast<const Expr*>(f.init.get()), col);
                }
            }
            scanExpr(f.condition.get(), col);
            scanExpr(f.update.get(), col);
            scanBody(f.body, col);
            return;
        }
        case NodeKind::ForOfStatement: {
            const auto& f = static_cast<const ForOfStatement&>(s);
            const Node* left = f.left.get();
            if (left) {
                if (left->kind == NodeKind::VariableDeclaration) {
                    scanStmt(*static_cast<const Stmt*>(left), col);
                } else {
                    walkPattern(left, col);
                }
            }
            scanExpr(f.right.get(), col);
            scanBody(f.body, col);
            return;
        }
        case NodeKind::ForInStatement: {
            const auto& f = static_cast<const ForInStatement&>(s);
            const Node* left = f.left.get();
            if (left) {
                if (left->kind == NodeKind::VariableDeclaration) {
                    scanStmt(*static_cast<const Stmt*>(left), col);
                } else {
                    walkPattern(left, col);
                }
            }
            scanExpr(f.right.get(), col);
            scanBody(f.body, col);
            return;
        }
        case NodeKind::ReturnStatement:
            scanExpr(static_cast<const ReturnStatement&>(s).argument.get(), col);
            return;
        case NodeKind::ThrowStatement:
            scanExpr(static_cast<const ThrowStatement&>(s).argument.get(), col);
            return;
        case NodeKind::TryStatement: {
            const auto& ts = static_cast<const TryStatement&>(s);
            scanBody(ts.tryBlock, col);
            if (ts.handler) {
                // A catch binding pattern may carry AssignmentPattern defaults
                // (real expressions).
                walkPattern(ts.handler->param.get(), col);
                scanBody(ts.handler->body, col);
            }
            scanBody(ts.finallyBlock, col);
            return;
        }
        case NodeKind::SwitchStatement:
            for (const SwitchCasePtr& casePtr :
                 static_cast<const SwitchStatement&>(s).cases) {
                if (!casePtr) continue;
                scanExpr(casePtr->test.get(), col);
                for (const StmtPtr& child : casePtr->statements) {
                    if (child) scanStmt(*child, col);
                }
            }
            return;
        case NodeKind::LabeledStatement:
            scanBody(static_cast<const LabeledStatement&>(s).body, col);
            return;
        case NodeKind::FunctionDeclaration: {
            const auto& fd = static_cast<const FunctionDeclaration&>(s);
            walkParams(fd.params, col);
            scanBody(fd.body, col);
            return;
        }
        case NodeKind::EnumDeclaration:
            // An enum member's initializer is an expression (it could nest an
            // arrow returning a class).
            for (const EnumMemberPtr& member :
                 static_cast<const EnumDeclaration&>(s).members) {
                if (member) scanExpr(member->initializer.get(), col);
            }
            return;
        case NodeKind::InterfaceDeclaration:
        case NodeKind::TypeAliasDeclaration:
        case NodeKind::ImportDeclaration:
        case NodeKind::EmptyStatement:
            return;
        default:
            return;
    }
}

void scanExpr(const Expr* e, Collector& col) {
    if (e == nullptr) return;
    switch (e->kind) {
        case NodeKind::TemplateLiteral:
            for (const ExprPtr& part :
                 static_cast<const TemplateLiteral&>(*e).expressions) {
                scanExpr(part.get(), col);
            }
            return;
        case NodeKind::ArrayLiteral:
            for (const ExprPtr& part :
                 static_cast<const ArrayLiteral&>(*e).elements) {
                scanExpr(part.get(), col);
            }
            return;
        case NodeKind::ObjectLiteral:
            for (const ObjectPropertyPtr& prop :
                 static_cast<const ObjectLiteral&>(*e).properties) {
                if (!prop) continue;
                scanExpr(prop->key.get(), col);    // a computed key is an expr
                scanExpr(prop->value.get(), col);  // nullptr for shorthand
            }
            return;
        case NodeKind::MemberExpr:
            scanExpr(static_cast<const MemberExpr&>(*e).object.get(), col);
            return;
        case NodeKind::IndexExpr: {
            const auto& ix = static_cast<const IndexExpr&>(*e);
            scanExpr(ix.object.get(), col);
            scanExpr(ix.index.get(), col);
            return;
        }
        case NodeKind::CallExpr: {
            const auto& c = static_cast<const CallExpr&>(*e);
            scanExpr(c.callee.get(), col);
            for (const ExprPtr& arg : c.arguments) scanExpr(arg.get(), col);
            return;
        }
        case NodeKind::NewExpr: {
            const auto& n = static_cast<const NewExpr&>(*e);
            scanExpr(n.callee.get(), col);
            for (const ExprPtr& arg : n.arguments) scanExpr(arg.get(), col);
            return;
        }
        case NodeKind::UnaryExpr:
            scanExpr(static_cast<const UnaryExpr&>(*e).operand.get(), col);
            return;
        case NodeKind::UpdateExpr:
            scanExpr(static_cast<const UpdateExpr&>(*e).operand.get(), col);
            return;
        case NodeKind::AwaitExpr:
            scanExpr(static_cast<const AwaitExpr&>(*e).argument.get(), col);
            return;
        case NodeKind::SpreadElement:
            scanExpr(static_cast<const SpreadElement&>(*e).argument.get(), col);
            return;
        case NodeKind::WeakExpr:
            scanExpr(static_cast<const WeakExpr&>(*e).value.get(), col);
            return;
        case NodeKind::BinaryExpr: {
            const auto& b = static_cast<const BinaryExpr&>(*e);
            scanExpr(b.left.get(), col);
            scanExpr(b.right.get(), col);
            return;
        }
        case NodeKind::LogicalExpr: {
            const auto& l = static_cast<const LogicalExpr&>(*e);
            scanExpr(l.left.get(), col);
            scanExpr(l.right.get(), col);
            return;
        }
        case NodeKind::ConditionalExpr: {
            const auto& c = static_cast<const ConditionalExpr&>(*e);
            scanExpr(c.condition.get(), col);
            scanExpr(c.whenTrue.get(), col);
            scanExpr(c.whenFalse.get(), col);
            return;
        }
        case NodeKind::AssignmentExpr: {
            const auto& a = static_cast<const AssignmentExpr&>(*e);
            // The target is an expression (Identifier/MemberExpr/IndexExpr).
            scanExpr(a.target.get(), col);
            scanExpr(a.value.get(), col);
            return;
        }
        case NodeKind::RangeExpr: {
            const auto& r = static_cast<const RangeExpr&>(*e);
            scanExpr(r.start.get(), col);
            scanExpr(r.end.get(), col);
            return;
        }
        case NodeKind::ArrowFunction: {
            const auto& a = static_cast<const ArrowFunction&>(*e);
            walkParams(a.params, col);
            if (a.body == nullptr) return;
            if (a.expressionBody) {
                scanExpr(dynamic_cast<const Expr*>(a.body.get()), col);
            } else {
                // `a.body` is a plain Node (an expression or a block
                // statement); not a StmtPtr.
                scanStmt(*static_cast<const Stmt*>(a.body.get()), col);
            }
            return;
        }
        case NodeKind::AsExpr:
            scanExpr(static_cast<const AsExpr&>(*e).expression.get(), col);
            return;
        case NodeKind::NonNullExpr:
            scanExpr(static_cast<const NonNullExpr&>(*e).expression.get(), col);
            return;
        default:
            return;
    }
}

}  // namespace

std::unordered_set<std::string> buildVirtualMethodSet(
    const std::vector<const Program*>& programs) {
    // Phase 1 - collect the classes of the program in deterministic,
    // declaration order (unit by unit, statement by statement).
    std::vector<ClassFact> facts;
    {
        Collector col;
        for (const Program* program : programs) {
            if (program == nullptr) continue;
            for (const StmtPtr& stmt : program->body) {
                if (stmt) scanStmt(*stmt, col);
            }
        }
        // A class declaration appears at exactly one tree position, so the
        // collector never sees it twice.
        facts.reserve(col.classes.size());
        for (const ClassDeclaration* c : col.classes) {
            facts.push_back(collectClassFact(*c));
        }
    }

    // Index: declaration order preserved for deterministic candidate lists.
    std::unordered_map<std::string, std::vector<std::size_t>> byName;
    for (std::size_t i = 0; i < facts.size(); ++i) {
        byName[facts[i].name].push_back(i);
    }

    std::unordered_set<std::string> virt;
    const auto mark = [&virt](const std::string& className,
                              const std::string& methodName) {
        virt.insert(className + "::" + methodName);
    };
    const auto markAllOf = [&virt, &facts](std::size_t index) {
        for (const MethodFact& m : facts[index].methods) {
            virt.insert(facts[index].name + "::" + m.name);
        }
    };

    // The transitive closure of the classes that may be `i`'s ancestors: a
    // resolved link contributes its target; an ambiguous link (a duplicate
    // class name elsewhere in the program) contributes EVERY candidate, since
    // any of them may be the true base; an unresolvable link contributes
    // nothing and marks the class unmodelable (its own members are marked
    // fail closed instead). A cycle is bounded by the visited set.
    struct AncestorResult {
        std::vector<std::size_t> indices;
        bool unmodelable = false;  // an unseen/cyclic base somewhere above
    };
    const auto ancestorsOf =
        [&facts, &byName](std::size_t i) -> AncestorResult {
            AncestorResult out;
            std::unordered_set<std::size_t> seen;
            std::vector<std::size_t> stack{i};
            while (!stack.empty()) {
                const std::size_t cur = stack.back();
                stack.pop_back();
                const ClassFact& f = facts[cur];
                if (f.baseExtendsExpr || f.baseExtendsComplex) {
                    out.unmodelable = true;
                    continue;
                }
                if (f.baseName.empty()) continue;
                const auto it = byName.find(f.baseName);
                if (it == byName.end()) {
                    // A base that is not a nominal class of this program: an
                    // imported/package/builtin base whose members codegen
                    // cannot see.
                    out.unmodelable = true;
                    continue;
                }
                for (const std::size_t candidate : it->second) {
                    if (seen.insert(candidate).second) {
                        out.indices.push_back(candidate);
                        stack.push_back(candidate);
                    }
                }
            }
            return out;
        };

    for (std::size_t i = 0; i < facts.size(); ++i) {
        // Rule 1 - a pair (i, j) where j is possibly an ancestor of i: same
        // method name with at least one common arity means every same-name
        // member of both classes may be dispatched to the other's override.
        const AncestorResult ancestors = ancestorsOf(i);
        for (const std::size_t j : ancestors.indices) {
            const ClassFact& base = facts[j];
            // Group the two classes' member names to decide per name.
            std::unordered_set<std::string> names;
            for (const MethodFact& m : facts[i].methods) names.insert(m.name);
            for (const MethodFact& b : base.methods) names.insert(b.name);
            for (const std::string& name : names) {
                bool shared = false;
                for (const MethodFact& m : facts[i].methods) {
                    for (const MethodFact& b : base.methods) {
                        if (b.name == name && m.name == name &&
                            b.arity == m.arity) {
                            shared = true;
                            break;
                        }
                    }
                    if (shared) break;
                }
                if (!shared) continue;
                // Collapse every same-name member of both classes, including
                // overloads whose arity does not match (fail closed and cheap).
                for (const MethodFact& m : facts[i].methods) {
                    if (m.name == name) mark(facts[i].name, m.name);
                }
                for (const MethodFact& m : base.methods) {
                    if (m.name == name) mark(base.name, m.name);
                }
            }
        }
        // Rule 2 - an unseen base somewhere above: the base may declare any of
        // this class's methods, so all of them are overridable and every
        // dispatch through a base reference may name this class's method.
        if (ancestors.unmodelable) markAllOf(i);
    }

    // Rule 3 - an abstract member is meant to be dispatched to its overrider;
    // the concrete declarations in the descendants are marked by the pair
    // rule. Marking the abstract declaration itself is required for the base's
    // own slot.
    for (const ClassFact& f : facts) {
        for (const MethodFact& m : f.methods) {
            if (m.isAbstract) mark(f.name, m.name);
        }
    }
    return virt;
}

}  // namespace detail
}  // namespace cp3
