// =============================================================================
// src/codegen/src/support/class_walk.cpp - model-registration walkers
// (detail/class_walk.h).
//
// One recursive walk over the unit's statements and expressions, run before
// the emit walk: every non-generic class declaration becomes a ClassModel and
// every structural object shape (an object literal, a `{ ... }` annotation)
// becomes an ObjectModel, keyed so that the type map can resolve a sema type
// to its generated C++ spelling. Class member bodies are walked too, so a
// class declared inside a method body is registered before it is emitted.
// =============================================================================
#include "detail/class/class_walk.h"

#include <string>
#include <utility>

#include "detail/class/class_model.h"
#include "detail/generator/type_map.h"

namespace cp3 {
namespace detail {

namespace {

// An arrow function: its signature annotations and its body (an expression or
// a block, whose local classes and object shapes must be registered too).
void walkArrowModels(const ArrowFunction& a, const SemaResult& sema,
                     TypeSystem typeSystem);

// Registers one class declaration (generic classes become C++ templates).
void addClassModel(const ClassDeclaration& c, bool local,
                   const SemaResult& sema) {
    ModelRegistry& reg = ModelRegistry::get();
    ClassModel& cm = reg.addClass(c.name, local, &c);
    // The class may be reached by more than one walk (the statement walk and
    // the body walk): its type parameters are listed once.
    if (cm.typeParamsRecorded) return;
    cm.typeParamsRecorded = true;
    // The cycle-capability verdict (sema publishes it per class declaration
    // id): a cycle-capable class uses the cycle-aware handle and the
    // collector's visitor.
    // The class model carries the collector verdict so every spelling of a
    // class handle follows one rule.
    const auto it = sema.cycleEligible.find(c.nodeId);
    cm.cycleCollector = it != sema.cycleEligible.end() && it->second;
    for (const auto& tp : c.typeParams) {
        if (tp) cm.typeParams.push_back(tp->name);
    }
    if (local && !cm.typeParams.empty()) {
        // A C++ local class cannot be a template: the struct moves to the
        // unit's namespace scope under a name of its own.
        cm.structName = reg.nextLocalClassName(c.name);
        cm.cpp = "::" + reg.namesp() + "::" + cm.structName;
        cm.namespaceScope = true;
    }
    if (c.extendsType && c.extendsType->kind == NodeKind::NamedType) {
        cm.baseName = static_cast<const NamedType&>(*c.extendsType).name;
    }
}

void walkExprPtr(const ExprPtr& e, const SemaResult& sema,
                   TypeSystem typeSystem) {
    walkExprModels(e.get(), sema, typeSystem);
}

void walkExprPair(const ExprPtr& a, const ExprPtr& b, const SemaResult& sema,
                    TypeSystem typeSystem) {
    walkExprPtr(a, sema, typeSystem);
    walkExprPtr(b, sema, typeSystem);
}

void walkExprList(const ExprList& list, const SemaResult& sema,
                    TypeSystem typeSystem) {
    for (const auto& child : list) {
        walkExprModels(child.get(), sema, typeSystem);
    }
}

void walkStmtList(const std::vector<StmtPtr>& body, const SemaResult& sema,
                    bool local, TypeSystem typeSystem) {
    for (const auto& child : body) {
        if (child) walkStmtModels(*child, sema, local, typeSystem);
    }
}

void walkBodyStmt(const StmtPtr& body, bool local, const SemaResult& sema,
                    TypeSystem typeSystem) {
    if (body) walkStmtModels(*body, sema, local, typeSystem);
}

void walkClassesInBody(const StmtPtr& body, const SemaResult& sema,
                         TypeSystem typeSystem) {
    if (body) walkClassesForModels(*body, true, sema, typeSystem);
}

void walkParams(const ParamList& params, const SemaResult& sema,
                   TypeSystem typeSystem) {
    for (const auto& p : params) {
        if (p && p->type) (void)modelTypeFromTypeNode(*p->type, typeSystem);
    }
    (void)sema;
}

// The body + signature of one function-shaped class member.
void walkFunctionModels(const ParamList& params, const TypePtr& returnType,
                           const StmtPtr& body, const SemaResult& sema,
                           TypeSystem typeSystem) {
    walkParams(params, sema, typeSystem);
    if (returnType) (void)modelTypeFromTypeNode(*returnType, typeSystem);
    walkBodyStmt(body, true, sema, typeSystem);
}

// Every member of a class: field types + initializers, method/constructor
// signatures + bodies, accessor signatures + bodies.
void walkClassMembers(const std::vector<ClassMemberPtr>& members,
                       const SemaResult& sema, TypeSystem typeSystem) {
    for (const auto& mp : members) {
        if (!mp) continue;
        switch (mp->kind) {
            case NodeKind::PropertyDeclaration: {
                const auto& p = static_cast<const PropertyDeclaration&>(*mp);
                if (p.type) (void)modelTypeFromTypeNode(*p.type, typeSystem);
                walkExprPtr(p.initializer, sema, typeSystem);
                break;
            }
            case NodeKind::MethodDeclaration: {
                const auto& md = static_cast<const MethodDeclaration&>(*mp);
                walkFunctionModels(md.params, md.returnType, md.body, sema,
                                 typeSystem);
                break;
            }
            case NodeKind::ConstructorDeclaration: {
                const auto& cd =
                    static_cast<const ConstructorDeclaration&>(*mp);
                walkFunctionModels(cd.params, TypePtr(), cd.body, sema,
                                 typeSystem);
                break;
            }
            case NodeKind::AccessorDeclaration: {
                const auto& ad = static_cast<const AccessorDeclaration&>(*mp);
                walkFunctionModels(ad.params, ad.returnType, ad.body, sema,
                                 typeSystem);
                break;
            }
            default:
                break;
        }
    }
}

// Registers one class (and everything its member bodies declare).
void registerClassTree(const ClassDeclaration& c, bool local,
                        const SemaResult& sema, TypeSystem typeSystem) {
    addClassModel(c, local, sema);
    for (const auto& t : c.implementsTypes) {
        if (t) (void)modelTypeFromTypeNode(*t, typeSystem);
    }
    walkClassMembers(c.members, sema, typeSystem);
}

// Registers the plain object model of a literal, keyed by the sema label.
void registerLiteralModel(const ObjectLiteral& lit, const SemaResult& sema) {
    const std::vector<ObjectFieldModel> fields = literalFields(lit, sema);
    if (fields.empty()) return;
    const std::string label = objectLabelOf(fields);
    ModelRegistry& reg = ModelRegistry::get();
    if (reg.objectByLabel(label) == nullptr) {
        reg.ensureObject(label, fields);
    }
}

}  // namespace

std::vector<ObjectFieldModel> literalFields(const ObjectLiteral& lit,
                                            const SemaResult& sema) {
    std::vector<ObjectFieldModel> fields;
    for (const auto& p : lit.properties) {
        if (!p) continue;
        // A computed key names the property at runtime; only a literal key
        // (whose value the compiler knows) is a fixed field of the shape.
        ObjectFieldModel f;
        f.name = classMemberName(p->key.get(), p->computed);
        if (f.name.empty()) continue;
        const Expr* value = p->value.get();
        const Expr* typed = value != nullptr ? value : p->key.get();
        if (typed != nullptr) {
            if (const Type* t = sema.typeOf(typed->nodeId)) {
                f.type = widenType(*t);
            }
        }
        f.isFunction = f.type.kind == TypeKind::Function;
        // Duplicate keys: the first wins (JS object-literal semantics).
        bool duplicate = false;
        for (const ObjectFieldModel& seen : fields) {
            if (seen.name == f.name) duplicate = true;
        }
        if (!duplicate) fields.push_back(std::move(f));
    }
    return fields;
}

// ---- the class walker --------------------------------------------------------

void walkClassesForModels(const Stmt& s, bool local, const SemaResult& sema,
                          TypeSystem typeSystem) {
    switch (s.kind) {
        case NodeKind::ClassDeclaration:
            registerClassTree(static_cast<const ClassDeclaration&>(s), local,
                              sema, typeSystem);
            return;
        case NodeKind::ExportDeclaration: {
            const auto& ed = static_cast<const ExportDeclaration&>(s);
            if (ed.declaration) {
                walkClassesForModels(*ed.declaration, local, sema, typeSystem);
            }
            return;
        }
        case NodeKind::BlockStatement:
            for (const auto& child : static_cast<const BlockStatement&>(s).body) {
                if (child) walkClassesForModels(*child, true, sema,
                                                      typeSystem);
            }
            return;
        case NodeKind::IfStatement: {
            const auto& is = static_cast<const IfStatement&>(s);
            if (is.thenBranch)
                walkClassesForModels(*is.thenBranch, true, sema, typeSystem);
            if (is.elseBranch)
                walkClassesForModels(*is.elseBranch, true, sema, typeSystem);
            return;
        }
        case NodeKind::WhileStatement:
            walkClassesInBody(static_cast<const WhileStatement&>(s).body, sema,
                              typeSystem);
            return;
        case NodeKind::DoWhileStatement:
            walkClassesInBody(static_cast<const DoWhileStatement&>(s).body,
                              sema, typeSystem);
            return;
        case NodeKind::ForStatement:
            walkClassesInBody(static_cast<const ForStatement&>(s).body, sema,
                              typeSystem);
            return;
        case NodeKind::ForOfStatement:
            walkClassesInBody(static_cast<const ForOfStatement&>(s).body, sema,
                              typeSystem);
            return;
        case NodeKind::ForInStatement:
            walkClassesInBody(static_cast<const ForInStatement&>(s).body, sema,
                              typeSystem);
            return;
        case NodeKind::FunctionDeclaration:
            walkClassesInBody(static_cast<const FunctionDeclaration&>(s).body,
                              sema, typeSystem);
            return;
        case NodeKind::TryStatement: {
            const auto& ts = static_cast<const TryStatement&>(s);
            walkClassesInBody(ts.tryBlock, sema, typeSystem);
            if (ts.handler)
                walkClassesInBody(ts.handler->body, sema, typeSystem);
            walkClassesInBody(ts.finallyBlock, sema, typeSystem);
            return;
        }
        case NodeKind::SwitchStatement: {
            for (const auto& c : static_cast<const SwitchStatement&>(s).cases) {
                if (c == nullptr) continue;
                for (const auto& child : c->statements) {
                    if (child) walkClassesForModels(*child, true, sema,
                                                      typeSystem);
                }
            }
            return;
        }
        case NodeKind::LabeledStatement:
            walkClassesInBody(static_cast<const LabeledStatement&>(s).body,
                              sema, typeSystem);
            return;
        default:
            return;
    }
}

// ---- the statement walker ----------------------------------------------------

namespace {

void walkForModels(const ForStatement& f, const SemaResult& sema,
                    TypeSystem typeSystem) {
    if (f.init) {
        if (f.init->kind == NodeKind::VariableDeclaration) {
            walkStmtModels(static_cast<const Stmt&>(*f.init), sema, true,
                         typeSystem);
        } else {
            walkExprModels(dynamic_cast<const Expr*>(f.init.get()), sema,
                         typeSystem);
        }
    }
    walkExprPtr(f.condition, sema, typeSystem);
    walkExprPtr(f.update, sema, typeSystem);
    walkBodyStmt(f.body, true, sema, typeSystem);
}

void walkSwitchModels(const SwitchStatement& sw, const SemaResult& sema,
                       TypeSystem typeSystem) {
    walkExprPtr(sw.discriminant, sema, typeSystem);
    for (const auto& c : sw.cases) {
        if (c == nullptr) continue;
        walkExprPtr(c->test, sema, typeSystem);
        walkStmtList(c->statements, sema, true, typeSystem);
    }
}

// One `var`/`let`/`const` statement: every declarator's annotation and its
// initializer.
void walkVariableDeclModels(const VariableDeclaration& vd,
                              const SemaResult& sema, TypeSystem typeSystem) {
    for (const auto& d : vd.declarations) {
        if (!d) continue;
        if (d->type) (void)modelTypeFromTypeNode(*d->type, typeSystem);
        walkExprPtr(d->init, sema, typeSystem);
    }
}

// An `if`: the condition and both branches' bodies.
void walkIfModels(const IfStatement& is, const SemaResult& sema,
                   TypeSystem typeSystem) {
    walkExprPtr(is.condition, sema, typeSystem);
    walkBodyStmt(is.thenBranch, true, sema, typeSystem);
    walkBodyStmt(is.elseBranch, true, sema, typeSystem);
}

// A `try`: the three blocks, the handler body included.
void walkTryModels(const TryStatement& ts, const SemaResult& sema,
                    TypeSystem typeSystem) {
    walkBodyStmt(ts.tryBlock, true, sema, typeSystem);
    if (ts.handler)
        walkBodyStmt(ts.handler->body, true, sema, typeSystem);
    walkBodyStmt(ts.finallyBlock, true, sema, typeSystem);
}

// An object literal: walk the property values, then register the shape.
void walkObjectLiteralModels(const ObjectLiteral& lit,
                              const SemaResult& sema, TypeSystem typeSystem) {
    for (const auto& p : lit.properties) {
        if (p) walkExprPtr(p->value, sema, typeSystem);
    }
    registerLiteralModel(lit, sema);
}

}  // namespace

void walkStmtModels(const Stmt& s, const SemaResult& sema, bool local,
                    TypeSystem typeSystem) {
    switch (s.kind) {
        case NodeKind::BlockStatement:
            walkStmtList(static_cast<const BlockStatement&>(s).body, sema,
                         true, typeSystem);
            return;
        case NodeKind::expressionstatement:
            walkExprPtr(static_cast<const expressionstatement&>(s).expression,
                        sema, typeSystem);
            return;
        case NodeKind::VariableDeclaration:
            walkVariableDeclModels(static_cast<const VariableDeclaration&>(s),
                                   sema, typeSystem);
            return;
        case NodeKind::IfStatement:
            walkIfModels(static_cast<const IfStatement&>(s), sema,
                         typeSystem);
            return;
        case NodeKind::WhileStatement: {
            const auto& ws = static_cast<const WhileStatement&>(s);
            walkExprPtr(ws.condition, sema, typeSystem);
            walkBodyStmt(ws.body, true, sema, typeSystem);
            return;
        }
        case NodeKind::DoWhileStatement: {
            const auto& dw = static_cast<const DoWhileStatement&>(s);
            walkBodyStmt(dw.body, true, sema, typeSystem);
            walkExprPtr(dw.condition, sema, typeSystem);
            return;
        }
        case NodeKind::ForStatement:
            walkForModels(static_cast<const ForStatement&>(s), sema,
                         typeSystem);
            return;
        case NodeKind::ForOfStatement: {
            const auto& f = static_cast<const ForOfStatement&>(s);
            walkExprPtr(f.right, sema, typeSystem);
            walkBodyStmt(f.body, true, sema, typeSystem);
            return;
        }
        case NodeKind::ForInStatement: {
            const auto& f = static_cast<const ForInStatement&>(s);
            walkExprPtr(f.right, sema, typeSystem);
            walkBodyStmt(f.body, true, sema, typeSystem);
            return;
        }
        case NodeKind::ReturnStatement:
            walkExprPtr(static_cast<const ReturnStatement&>(s).argument, sema,
                         typeSystem);
            return;
        case NodeKind::ThrowStatement:
            walkExprPtr(static_cast<const ThrowStatement&>(s).argument, sema,
                         typeSystem);
            return;
        case NodeKind::TryStatement:
            walkTryModels(static_cast<const TryStatement&>(s), sema,
                          typeSystem);
            return;
        case NodeKind::SwitchStatement:
            walkSwitchModels(static_cast<const SwitchStatement&>(s), sema,
                            typeSystem);
            return;
        case NodeKind::LabeledStatement:
            walkBodyStmt(static_cast<const LabeledStatement&>(s).body, true, sema,
                         typeSystem);
            return;
        case NodeKind::FunctionDeclaration: {
            const auto& fd = static_cast<const FunctionDeclaration&>(s);
            walkFunctionModels(fd.params, fd.returnType, fd.body, sema,
                              typeSystem);
            return;
        }
        case NodeKind::ClassDeclaration:
            // A class declaration registers its model: a class inside a body is
            // function-local (its struct is emitted inline in that body), a
            // class at the unit's root is a namespace-scope struct.
            registerClassTree(static_cast<const ClassDeclaration&>(s), local,
                              sema, typeSystem);
            return;
        case NodeKind::ExportDeclaration: {
            const auto& ed = static_cast<const ExportDeclaration&>(s);
            walkBodyStmt(ed.declaration, local, sema, typeSystem);
            walkExprPtr(ed.defaultExpr, sema, typeSystem);
            return;
        }
        default:
            return;
    }
}

// ---- the expression walker ---------------------------------------------------

void walkExprModels(const Expr* e, const SemaResult& sema,
                    TypeSystem typeSystem) {
    if (e == nullptr) return;
    switch (e->kind) {
        case NodeKind::TemplateLiteral:
            walkExprList(static_cast<const TemplateLiteral&>(*e).expressions,
                         sema, typeSystem);
            return;
        case NodeKind::ArrayLiteral:
            walkExprList(static_cast<const ArrayLiteral&>(*e).elements, sema,
                         typeSystem);
            return;
        case NodeKind::ObjectLiteral:
            walkObjectLiteralModels(static_cast<const ObjectLiteral&>(*e), sema,
                                   typeSystem);
            return;
        case NodeKind::MemberExpr:
            walkExprPtr(static_cast<const MemberExpr&>(*e).object, sema,
                         typeSystem);
            return;
        case NodeKind::IndexExpr: {
            const auto& ix = static_cast<const IndexExpr&>(*e);
            walkExprPair(ix.object, ix.index, sema, typeSystem);
            return;
        }
        case NodeKind::CallExpr: {
            const auto& c = static_cast<const CallExpr&>(*e);
            walkExprPtr(c.callee, sema, typeSystem);
            walkExprList(c.arguments, sema, typeSystem);
            return;
        }
        case NodeKind::NewExpr: {
            const auto& n = static_cast<const NewExpr&>(*e);
            walkExprPtr(n.callee, sema, typeSystem);
            walkExprList(n.arguments, sema, typeSystem);
            return;
        }
        case NodeKind::UnaryExpr:
            walkExprPtr(static_cast<const UnaryExpr&>(*e).operand, sema,
                         typeSystem);
            return;
        case NodeKind::UpdateExpr:
            walkExprPtr(static_cast<const UpdateExpr&>(*e).operand, sema,
                         typeSystem);
            return;
        case NodeKind::AwaitExpr:
            walkExprPtr(static_cast<const AwaitExpr&>(*e).argument, sema,
                         typeSystem);
            return;
        case NodeKind::SpreadElement:
            walkExprPtr(static_cast<const SpreadElement&>(*e).argument, sema,
                         typeSystem);
            return;
        case NodeKind::WeakExpr:
            walkExprPtr(static_cast<const WeakExpr&>(*e).value, sema,
                         typeSystem);
            return;
        case NodeKind::BinaryExpr: {
            const auto& b = static_cast<const BinaryExpr&>(*e);
            walkExprPair(b.left, b.right, sema, typeSystem);
            return;
        }
        case NodeKind::LogicalExpr: {
            const auto& l = static_cast<const LogicalExpr&>(*e);
            walkExprPair(l.left, l.right, sema, typeSystem);
            return;
        }
        case NodeKind::ConditionalExpr: {
            const auto& c = static_cast<const ConditionalExpr&>(*e);
            walkExprPtr(c.condition, sema, typeSystem);
            walkExprPair(c.whenTrue, c.whenFalse, sema, typeSystem);
            return;
        }
        case NodeKind::AssignmentExpr: {
            const auto& a = static_cast<const AssignmentExpr&>(*e);
            walkExprPair(a.target, a.value, sema, typeSystem);
            return;
        }
        case NodeKind::RangeExpr: {
            const auto& r = static_cast<const RangeExpr&>(*e);
            walkExprPair(r.start, r.end, sema, typeSystem);
            return;
        }
        case NodeKind::ArrowFunction:
            walkArrowModels(static_cast<const ArrowFunction&>(*e), sema,
                          typeSystem);
            return;
        default:
            return;
    }
}

namespace {

void walkArrowModels(const ArrowFunction& a, const SemaResult& sema,
                     TypeSystem typeSystem) {
    walkParams(a.params, sema, typeSystem);
    if (a.returnType) (void)modelTypeFromTypeNode(*a.returnType, typeSystem);
    if (a.body == nullptr) return;
    if (a.expressionBody) {
        walkExprModels(dynamic_cast<const Expr*>(a.body.get()), sema,
                       typeSystem);
    } else {
        walkStmtModels(static_cast<const Stmt&>(*a.body), sema, true,
                       typeSystem);
    }
}

}  // namespace

}  // namespace detail
}  // namespace cp3
