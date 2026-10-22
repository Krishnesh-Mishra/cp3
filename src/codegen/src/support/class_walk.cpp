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
#include "detail/class_walk.h"

#include <string>
#include <utility>

#include "detail/class_model.h"
#include "detail/type_map.h"

namespace cp3 {
namespace detail {

namespace {

// An arrow function: its signature annotations and its body (an expression or
// a block, whose local classes and object shapes must be registered too).
void walkArrowModels(const ArrowFunction& a, const SemaResult& sema);

// Registers one class declaration (generic classes become C++ templates).
void addClassModel(const ClassDeclaration& c, bool local) {
    ModelRegistry& reg = ModelRegistry::get();
    ClassModel& cm = reg.addClass(c.name, local, &c);
    // The class may be reached by more than one walk (the statement walk and
    // the body walk): its type parameters are listed once.
    if (cm.typeParamsRecorded) return;
    cm.typeParamsRecorded = true;
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

void walkExprPtr(const ExprPtr& e, const SemaResult& sema) {
    walkExprModels(e.get(), sema);
}

void walkExprPair(const ExprPtr& a, const ExprPtr& b, const SemaResult& sema) {
    walkExprPtr(a, sema);
    walkExprPtr(b, sema);
}

void walkExprList(const ExprList& list, const SemaResult& sema) {
    for (const auto& child : list) {
        walkExprModels(child.get(), sema);
    }
}

void walkStmtList(const std::vector<StmtPtr>& body, const SemaResult& sema,
                   bool local) {
    for (const auto& child : body) {
        if (child) walkStmtModels(*child, sema, local);
    }
}

void walkBodyStmt(const StmtPtr& body, const SemaResult& sema) {
    // A function / method / arrow body is always nested: a class declared in
    // it is function-local.
    if (body) walkStmtModels(*body, sema, true);
}

void walkClassesInBody(const StmtPtr& body, const SemaResult& sema) {
    if (body) walkClassesForModels(*body, true, sema);
}

void walkParams(const ParamList& params, const SemaResult& sema) {
    for (const auto& p : params) {
        if (p && p->type) (void)modelTypeFromTypeNode(*p->type);
    }
    (void)sema;
}

// The body + signature of one function-shaped class member.
void walkFunctionModels(const ParamList& params, const TypePtr& returnType,
                        const StmtPtr& body, const SemaResult& sema) {
    walkParams(params, sema);
    if (returnType) (void)modelTypeFromTypeNode(*returnType);
    walkBodyStmt(body, sema);
}

// Every member of a class: field types + initializers, method/constructor
// signatures + bodies, accessor signatures + bodies.
void walkClassMembers(const std::vector<ClassMemberPtr>& members,
                      const SemaResult& sema) {
    for (const auto& mp : members) {
        if (!mp) continue;
        switch (mp->kind) {
            case NodeKind::PropertyDeclaration: {
                const auto& p = static_cast<const PropertyDeclaration&>(*mp);
                if (p.type) (void)modelTypeFromTypeNode(*p.type);
                walkExprPtr(p.initializer, sema);
                break;
            }
            case NodeKind::MethodDeclaration: {
                const auto& md = static_cast<const MethodDeclaration&>(*mp);
                walkFunctionModels(md.params, md.returnType, md.body, sema);
                break;
            }
            case NodeKind::ConstructorDeclaration: {
                const auto& cd =
                    static_cast<const ConstructorDeclaration&>(*mp);
                walkFunctionModels(cd.params, TypePtr(), cd.body, sema);
                break;
            }
            case NodeKind::AccessorDeclaration: {
                const auto& ad = static_cast<const AccessorDeclaration&>(*mp);
                walkFunctionModels(ad.params, ad.returnType, ad.body, sema);
                break;
            }
            default:
                break;
        }
    }
}

// Registers one class (and everything its member bodies declare).
void registerClassTree(const ClassDeclaration& c, bool local,
                       const SemaResult& sema) {
    addClassModel(c, local);
    for (const auto& t : c.implementsTypes) {
        if (t) (void)modelTypeFromTypeNode(*t);
    }
    walkClassMembers(c.members, sema);
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

void walkClassesForModels(const Stmt& s, bool local, const SemaResult& sema) {
    switch (s.kind) {
        case NodeKind::ClassDeclaration:
            registerClassTree(static_cast<const ClassDeclaration&>(s), local,
                              sema);
            return;
        case NodeKind::ExportDeclaration: {
            const auto& ed = static_cast<const ExportDeclaration&>(s);
            if (ed.declaration) {
                walkClassesForModels(*ed.declaration, local, sema);
            }
            return;
        }
        case NodeKind::BlockStatement:
            for (const auto& child : static_cast<const BlockStatement&>(s).body) {
                if (child) walkClassesForModels(*child, true, sema);
            }
            return;
        case NodeKind::IfStatement: {
            const auto& is = static_cast<const IfStatement&>(s);
            if (is.thenBranch) walkClassesForModels(*is.thenBranch, true, sema);
            if (is.elseBranch) walkClassesForModels(*is.elseBranch, true, sema);
            return;
        }
        case NodeKind::WhileStatement:
            walkClassesInBody(static_cast<const WhileStatement&>(s).body, sema);
            return;
        case NodeKind::DoWhileStatement:
            walkClassesInBody(static_cast<const DoWhileStatement&>(s).body,
                              sema);
            return;
        case NodeKind::ForStatement:
            walkClassesInBody(static_cast<const ForStatement&>(s).body, sema);
            return;
        case NodeKind::ForOfStatement:
            walkClassesInBody(static_cast<const ForOfStatement&>(s).body, sema);
            return;
        case NodeKind::ForInStatement:
            walkClassesInBody(static_cast<const ForInStatement&>(s).body, sema);
            return;
        case NodeKind::FunctionDeclaration:
            walkClassesInBody(static_cast<const FunctionDeclaration&>(s).body,
                              sema);
            return;
        case NodeKind::TryStatement: {
            const auto& ts = static_cast<const TryStatement&>(s);
            walkClassesInBody(ts.tryBlock, sema);
            if (ts.handler) walkClassesInBody(ts.handler->body, sema);
            walkClassesInBody(ts.finallyBlock, sema);
            return;
        }
        case NodeKind::SwitchStatement: {
            for (const auto& c : static_cast<const SwitchStatement&>(s).cases) {
                if (c == nullptr) continue;
                for (const auto& child : c->statements) {
                    if (child) walkClassesForModels(*child, true, sema);
                }
            }
            return;
        }
        case NodeKind::LabeledStatement:
            walkClassesInBody(static_cast<const LabeledStatement&>(s).body,
                              sema);
            return;
        default:
            return;
    }
}

// ---- the statement walker ----------------------------------------------------

namespace {

void walkForModels(const ForStatement& f, const SemaResult& sema) {
    if (f.init) {
        if (f.init->kind == NodeKind::VariableDeclaration) {
            walkStmtModels(static_cast<const Stmt&>(*f.init), sema, true);
        } else {
            walkExprModels(dynamic_cast<const Expr*>(f.init.get()), sema);
        }
    }
    walkExprPtr(f.condition, sema);
    walkExprPtr(f.update, sema);
    walkBodyStmt(f.body, sema);
}

void walkSwitchModels(const SwitchStatement& sw, const SemaResult& sema) {
    walkExprPtr(sw.discriminant, sema);
    for (const auto& c : sw.cases) {
        if (c == nullptr) continue;
        walkExprPtr(c->test, sema);
        walkStmtList(c->statements, sema, true);
    }
}

// One `var`/`let`/`const` statement: every declarator's annotation and its
// initializer.
void walkVariableDeclModels(const VariableDeclaration& vd,
                            const SemaResult& sema) {
    for (const auto& d : vd.declarations) {
        if (!d) continue;
        if (d->type) (void)modelTypeFromTypeNode(*d->type);
        walkExprPtr(d->init, sema);
    }
}

// An `if`: the condition and both branches' bodies.
void walkIfModels(const IfStatement& is, const SemaResult& sema) {
    walkExprPtr(is.condition, sema);
    walkBodyStmt(is.thenBranch, sema);
    walkBodyStmt(is.elseBranch, sema);
}

// A `try`: the three blocks, the handler body included.
void walkTryModels(const TryStatement& ts, const SemaResult& sema) {
    walkBodyStmt(ts.tryBlock, sema);
    if (ts.handler) walkBodyStmt(ts.handler->body, sema);
    walkBodyStmt(ts.finallyBlock, sema);
}

// An object literal: walk the property values, then register the shape.
void walkObjectLiteralModels(const ObjectLiteral& lit, const SemaResult& sema) {
    for (const auto& p : lit.properties) {
        if (p) walkExprPtr(p->value, sema);
    }
    registerLiteralModel(lit, sema);
}

}  // namespace

void walkStmtModels(const Stmt& s, const SemaResult& sema, bool local) {
    switch (s.kind) {
        case NodeKind::BlockStatement:
            walkStmtList(static_cast<const BlockStatement&>(s).body, sema, true);
            return;
        case NodeKind::ExpressionStatement:
            walkExprPtr(static_cast<const ExpressionStatement&>(s).expression,
                        sema);
            return;
        case NodeKind::VariableDeclaration:
            walkVariableDeclModels(static_cast<const VariableDeclaration&>(s),
                                   sema);
            return;
        case NodeKind::IfStatement:
            walkIfModels(static_cast<const IfStatement&>(s), sema);
            return;
        case NodeKind::WhileStatement: {
            const auto& ws = static_cast<const WhileStatement&>(s);
            walkExprPtr(ws.condition, sema);
            walkBodyStmt(ws.body, sema);
            return;
        }
        case NodeKind::DoWhileStatement: {
            const auto& dw = static_cast<const DoWhileStatement&>(s);
            walkBodyStmt(dw.body, sema);
            walkExprPtr(dw.condition, sema);
            return;
        }
        case NodeKind::ForStatement:
            walkForModels(static_cast<const ForStatement&>(s), sema);
            return;
        case NodeKind::ForOfStatement: {
            const auto& f = static_cast<const ForOfStatement&>(s);
            walkExprPtr(f.right, sema);
            walkBodyStmt(f.body, sema);
            return;
        }
        case NodeKind::ForInStatement: {
            const auto& f = static_cast<const ForInStatement&>(s);
            walkExprPtr(f.right, sema);
            walkBodyStmt(f.body, sema);
            return;
        }
        case NodeKind::ReturnStatement:
            walkExprPtr(static_cast<const ReturnStatement&>(s).argument, sema);
            return;
        case NodeKind::ThrowStatement:
            walkExprPtr(static_cast<const ThrowStatement&>(s).argument, sema);
            return;
        case NodeKind::TryStatement:
            walkTryModels(static_cast<const TryStatement&>(s), sema);
            return;
        case NodeKind::SwitchStatement:
            walkSwitchModels(static_cast<const SwitchStatement&>(s), sema);
            return;
        case NodeKind::LabeledStatement:
            walkBodyStmt(static_cast<const LabeledStatement&>(s).body, sema);
            return;
        case NodeKind::FunctionDeclaration: {
            const auto& fd = static_cast<const FunctionDeclaration&>(s);
            walkFunctionModels(fd.params, fd.returnType, fd.body, sema);
            return;
        }
        case NodeKind::ClassDeclaration:
            // A class declaration registers its model: a class inside a body is
            // function-local (its struct is emitted inline in that body), a
            // class at the unit's root is a namespace-scope struct.
            registerClassTree(static_cast<const ClassDeclaration&>(s), local,
                              sema);
            return;
        case NodeKind::ExportDeclaration: {
            const auto& ed = static_cast<const ExportDeclaration&>(s);
            walkBodyStmt(ed.declaration, sema);
            walkExprPtr(ed.defaultExpr, sema);
            return;
        }
        default:
            return;
    }
}

// ---- the expression walker ---------------------------------------------------

void walkExprModels(const Expr* e, const SemaResult& sema) {
    if (e == nullptr) return;
    switch (e->kind) {
        case NodeKind::TemplateLiteral:
            walkExprList(static_cast<const TemplateLiteral&>(*e).expressions, sema);
            return;
        case NodeKind::ArrayLiteral:
            walkExprList(static_cast<const ArrayLiteral&>(*e).elements, sema);
            return;
        case NodeKind::ObjectLiteral:
            walkObjectLiteralModels(static_cast<const ObjectLiteral&>(*e), sema);
            return;
        case NodeKind::MemberExpr:
            walkExprPtr(static_cast<const MemberExpr&>(*e).object, sema);
            return;
        case NodeKind::IndexExpr: {
            const auto& ix = static_cast<const IndexExpr&>(*e);
            walkExprPair(ix.object, ix.index, sema);
            return;
        }
        case NodeKind::CallExpr: {
            const auto& c = static_cast<const CallExpr&>(*e);
            walkExprPtr(c.callee, sema);
            walkExprList(c.arguments, sema);
            return;
        }
        case NodeKind::NewExpr: {
            const auto& n = static_cast<const NewExpr&>(*e);
            walkExprPtr(n.callee, sema);
            walkExprList(n.arguments, sema);
            return;
        }
        case NodeKind::UnaryExpr:
            walkExprPtr(static_cast<const UnaryExpr&>(*e).operand, sema);
            return;
        case NodeKind::UpdateExpr:
            walkExprPtr(static_cast<const UpdateExpr&>(*e).operand, sema);
            return;
        case NodeKind::AwaitExpr:
            walkExprPtr(static_cast<const AwaitExpr&>(*e).argument, sema);
            return;
        case NodeKind::SpreadElement:
            walkExprPtr(static_cast<const SpreadElement&>(*e).argument, sema);
            return;
        case NodeKind::WeakExpr:
            walkExprPtr(static_cast<const WeakExpr&>(*e).value, sema);
            return;
        case NodeKind::BinaryExpr: {
            const auto& b = static_cast<const BinaryExpr&>(*e);
            walkExprPair(b.left, b.right, sema);
            return;
        }
        case NodeKind::LogicalExpr: {
            const auto& l = static_cast<const LogicalExpr&>(*e);
            walkExprPair(l.left, l.right, sema);
            return;
        }
        case NodeKind::ConditionalExpr: {
            const auto& c = static_cast<const ConditionalExpr&>(*e);
            walkExprPtr(c.condition, sema);
            walkExprPair(c.whenTrue, c.whenFalse, sema);
            return;
        }
        case NodeKind::AssignmentExpr: {
            const auto& a = static_cast<const AssignmentExpr&>(*e);
            walkExprPair(a.target, a.value, sema);
            return;
        }
        case NodeKind::RangeExpr: {
            const auto& r = static_cast<const RangeExpr&>(*e);
            walkExprPair(r.start, r.end, sema);
            return;
        }
        case NodeKind::ArrowFunction:
            walkArrowModels(static_cast<const ArrowFunction&>(*e), sema);
            return;
        default:
            return;
    }
}

namespace {

void walkArrowModels(const ArrowFunction& a, const SemaResult& sema) {
    walkParams(a.params, sema);
    if (a.returnType) (void)modelTypeFromTypeNode(*a.returnType);
    if (a.body == nullptr) return;
    if (a.expressionBody) {
        walkExprModels(dynamic_cast<const Expr*>(a.body.get()), sema);
    } else {
        walkStmtModels(static_cast<const Stmt&>(*a.body), sema, true);
    }
}

}  // namespace

}  // namespace detail
}  // namespace cp3
