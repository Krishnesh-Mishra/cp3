// =============================================================================
// src/codegen/src/support/structural_walk.cpp - the structural AST walk
// (STYLE.md §8).
//
// Implements detail/structural_walk.h. Every node kind the walk understands is
// routed to its shape group; the groups report through the subclass hooks:
// identifier reads/writes, binding-pattern names, `let` declarators and frame
// boundaries (function/arrow entry and exit).
//
// The walk descends into an arrow's parameter defaults BEFORE entering the
// arrow's frame, so a default expression belongs to the enclosing frame; the
// same holds for function declarations. A `for` header binding is reported
// with `declaresLocals == false` (a per-iteration copy, never a cell).
// =============================================================================
#include "detail/class/structural_walk.h"

#include <string>
#include <vector>

namespace cp3 {
namespace detail {

void collectPatternNames(const Node& n, std::vector<std::string>& out) {
    switch (n.kind) {
        case NodeKind::Identifier:
            out.push_back(static_cast<const Identifier&>(n).name);
            return;
        case NodeKind::AssignmentPattern: {
            const auto& ap = static_cast<const AssignmentPattern&>(n);
            if (ap.left) collectPatternNames(*ap.left, out);
            return;
        }
        case NodeKind::RestElement: {
            const auto& re = static_cast<const RestElement&>(n);
            if (re.argument) collectPatternNames(*re.argument, out);
            return;
        }
        case NodeKind::ArrayPattern: {
            const auto& ap = static_cast<const ArrayPattern&>(n);
            for (const auto& el : ap.elements) {
                if (el) collectPatternNames(*el, out);
            }
            if (ap.rest) collectPatternNames(*ap.rest, out);
            return;
        }
        case NodeKind::ObjectPattern: {
            const auto& op = static_cast<const ObjectPattern&>(n);
            for (const auto& prop : op.properties) {
                if (prop && prop->value) collectPatternNames(*prop->value, out);
            }
            if (op.rest) collectPatternNames(*op.rest, out);
            return;
        }
        default:
            return;
    }
}

void StructuralWalk::visit(const Node& n) {
    if (stopped_) return;
    if (onNode(n)) {
        stopped_ = true;
        return;
    }
    switch (n.kind) {
        case NodeKind::Identifier:
            readIdentifier(static_cast<const Identifier&>(n));
            return;
        case NodeKind::FunctionDeclaration:
        case NodeKind::ArrowFunction:
            visitFrame(n);
            return;
        case NodeKind::TemplateLiteral:
        case NodeKind::ArrayLiteral:
        case NodeKind::ObjectLiteral:
        case NodeKind::SpreadElement:
        case NodeKind::WeakExpr:
            visitValueExpr(n);
            return;
        case NodeKind::MemberExpr:
        case NodeKind::IndexExpr:
        case NodeKind::CallExpr:
        case NodeKind::NewExpr:
            visitAccessExpr(n);
            return;
        case NodeKind::UnaryExpr:
        case NodeKind::UpdateExpr:
        case NodeKind::BinaryExpr:
        case NodeKind::LogicalExpr:
        case NodeKind::AwaitExpr:
        case NodeKind::RangeExpr:
            visitArithExpr(n);
            return;
        case NodeKind::ConditionalExpr:
        case NodeKind::AssignmentExpr:
            visitBranchExpr(n);
            return;
        case NodeKind::expressionstatement:
        case NodeKind::BlockStatement:
        case NodeKind::VariableDeclaration:
        case NodeKind::IfStatement:
        case NodeKind::SwitchStatement:
        case NodeKind::ReturnStatement:
        case NodeKind::ThrowStatement:
        case NodeKind::LabeledStatement:
        case NodeKind::ExportDeclaration:
            visitStatement(n);
            return;
        case NodeKind::WhileStatement:
        case NodeKind::DoWhileStatement:
        case NodeKind::ForStatement:
            visitLoop(n);
            return;
        case NodeKind::ForOfStatement:
        case NodeKind::ForInStatement:
            visitIteration(n);
            return;
        case NodeKind::TryStatement:
            visitTry(n);
            return;
        default:
            return;  // literals, type nodes, imports: no runtime references
    }
}

bool StructuralWalk::isPatternNode(const Node& n) {
    return n.kind == NodeKind::ArrayPattern || n.kind == NodeKind::ObjectPattern ||
           n.kind == NodeKind::AssignmentPattern ||
           n.kind == NodeKind::RestElement;
}

void StructuralWalk::visitFrame(const Node& n) {
    if (n.kind == NodeKind::ArrowFunction) {
        const auto& a = static_cast<const ArrowFunction&>(n);
        for (const auto& p : a.params) {
            if (p && p->defaultValue) visit(*p->defaultValue);
        }
        enterFrame(n, &a.params);
        if (a.body) visit(*a.body);
        leaveFrame();
        return;
    }
    const auto& fd = static_cast<const FunctionDeclaration&>(n);
    for (const auto& p : fd.params) {
        if (p && p->defaultValue) visit(*p->defaultValue);
    }
    enterFrame(n, &fd.params);
    if (fd.body) visit(*fd.body);
    leaveFrame();
}

void StructuralWalk::visitValueExpr(const Node& n) {
    switch (n.kind) {
        case NodeKind::TemplateLiteral: {
            const auto& tl = static_cast<const TemplateLiteral&>(n);
            for (const auto& e : tl.expressions) {
                if (e) visit(*e);
            }
            return;
        }
        case NodeKind::ArrayLiteral: {
            for (const auto& e : static_cast<const ArrayLiteral&>(n).elements) {
                if (e) visit(*e);
            }
            return;
        }
        case NodeKind::ObjectLiteral: {
            for (const auto& p : static_cast<const ObjectLiteral&>(n).properties) {
                if (!p) continue;
                if (p->computed && p->key) visit(*p->key);
                if (p->value) visit(*p->value);
            }
            return;
        }
        case NodeKind::SpreadElement: {
            const auto& sp = static_cast<const SpreadElement&>(n);
            if (sp.argument) visit(*sp.argument);
            return;
        }
        default: {
            const auto& w = static_cast<const WeakExpr&>(n);
            if (w.value) visit(*w.value);
            return;
        }
    }
}

void StructuralWalk::visitAccessExpr(const Node& n) {
    switch (n.kind) {
        case NodeKind::MemberExpr: {
            const auto& m = static_cast<const MemberExpr&>(n);
            if (m.object) visit(*m.object);
            return;
        }
        case NodeKind::IndexExpr: {
            const auto& ix = static_cast<const IndexExpr&>(n);
            if (ix.object) visit(*ix.object);
            if (ix.index) visit(*ix.index);
            return;
        }
        case NodeKind::CallExpr: {
            const auto& c = static_cast<const CallExpr&>(n);
            if (c.callee) visit(*c.callee);
            for (const auto& a : c.arguments) {
                if (a) visit(*a);
            }
            return;
        }
        default: {
            const auto& e = static_cast<const NewExpr&>(n);
            if (e.callee) visit(*e.callee);
            for (const auto& a : e.arguments) {
                if (a) visit(*a);
            }
            return;
        }
    }
}

void StructuralWalk::visitArithExpr(const Node& n) {
    switch (n.kind) {
        case NodeKind::UnaryExpr: {
            const auto& u = static_cast<const UnaryExpr&>(n);
            if (u.operand) visit(*u.operand);
            return;
        }
        case NodeKind::UpdateExpr: {
            const auto& u = static_cast<const UpdateExpr&>(n);
            if (!u.operand) return;
            if (u.operand->kind == NodeKind::Identifier) {
                writeIdentifier(static_cast<const Identifier&>(*u.operand));
            } else {
                visit(*u.operand);
            }
            return;
        }
        case NodeKind::BinaryExpr: {
            const auto& b = static_cast<const BinaryExpr&>(n);
            if (b.left) visit(*b.left);
            if (b.right) visit(*b.right);
            return;
        }
        case NodeKind::LogicalExpr: {
            const auto& l = static_cast<const LogicalExpr&>(n);
            if (l.left) visit(*l.left);
            if (l.right) visit(*l.right);
            return;
        }
        case NodeKind::AwaitExpr: {
            const auto& a = static_cast<const AwaitExpr&>(n);
            if (a.argument) visit(*a.argument);
            return;
        }
        default: {
            const auto& r = static_cast<const RangeExpr&>(n);
            if (r.start) visit(*r.start);
            if (r.end) visit(*r.end);
            return;
        }
    }
}

void StructuralWalk::visitBranchExpr(const Node& n) {
    if (n.kind == NodeKind::ConditionalExpr) {
        const auto& c = static_cast<const ConditionalExpr&>(n);
        if (c.condition) visit(*c.condition);
        if (c.whenTrue) visit(*c.whenTrue);
        if (c.whenFalse) visit(*c.whenFalse);
        return;
    }
    const auto& a = static_cast<const AssignmentExpr&>(n);
    if (a.target) {
        if (a.target->kind == NodeKind::Identifier) {
            writeIdentifier(static_cast<const Identifier&>(*a.target));
        } else if (isPatternNode(*a.target)) {
            visitPattern(*a.target, /*write=*/true);
        } else {
            visit(*a.target);
        }
    }
    if (a.value) visit(*a.value);
}

void StructuralWalk::visitStatement(const Node& n) {
    switch (n.kind) {
        case NodeKind::expressionstatement: {
            const auto& es = static_cast<const expressionstatement&>(n);
            if (es.expression) visit(*es.expression);
            return;
        }
        case NodeKind::BlockStatement: {
            for (const auto& s : static_cast<const BlockStatement&>(n).body) {
                if (s) visit(*s);
            }
            return;
        }
        case NodeKind::VariableDeclaration:
            visitVarDecl(static_cast<const VariableDeclaration&>(n),
                         /*declaresLocals=*/true);
            return;
        case NodeKind::IfStatement: {
            const auto& is = static_cast<const IfStatement&>(n);
            if (is.condition) visit(*is.condition);
            if (is.thenBranch) visit(*is.thenBranch);
            if (is.elseBranch) visit(*is.elseBranch);
            return;
        }
        case NodeKind::SwitchStatement: {
            const auto& sw = static_cast<const SwitchStatement&>(n);
            if (sw.discriminant) visit(*sw.discriminant);
            for (const auto& c : sw.cases) {
                if (!c) continue;
                if (c->test) visit(*c->test);
                for (const auto& s : c->statements) {
                    if (s) visit(*s);
                }
            }
            return;
        }
        case NodeKind::ReturnStatement: {
            const auto& r = static_cast<const ReturnStatement&>(n);
            if (r.argument) visit(*r.argument);
            return;
        }
        case NodeKind::ThrowStatement: {
            const auto& t = static_cast<const ThrowStatement&>(n);
            if (t.argument) visit(*t.argument);
            return;
        }
        case NodeKind::LabeledStatement: {
            const auto& ls = static_cast<const LabeledStatement&>(n);
            if (ls.body) visit(*ls.body);
            return;
        }
        default: {
            const auto& ed = static_cast<const ExportDeclaration&>(n);
            if (ed.declaration) visit(*ed.declaration);
            if (ed.defaultExpr) visit(*ed.defaultExpr);
            return;
        }
    }
}

void StructuralWalk::visitLoop(const Node& n) {
    switch (n.kind) {
        case NodeKind::WhileStatement: {
            const auto& ws = static_cast<const WhileStatement&>(n);
            if (ws.condition) visit(*ws.condition);
            if (ws.body) visit(*ws.body);
            return;
        }
        case NodeKind::DoWhileStatement: {
            const auto& dw = static_cast<const DoWhileStatement&>(n);
            if (dw.body) visit(*dw.body);
            if (dw.condition) visit(*dw.condition);
            return;
        }
        default: {
            const auto& f = static_cast<const ForStatement&>(n);
            if (f.init) {
                if (f.init->kind == NodeKind::VariableDeclaration) {
                    visitVarDecl(static_cast<const VariableDeclaration&>(*f.init),
                                 /*declaresLocals=*/false);
                } else {
                    visit(*f.init);
                }
            }
            if (f.condition) visit(*f.condition);
            if (f.update) visit(*f.update);
            if (f.body) visit(*f.body);
            return;
        }
    }
}

void StructuralWalk::visitIteration(const Node& n) {
    const ForOfStatement* of = n.kind == NodeKind::ForOfStatement
                                   ? &static_cast<const ForOfStatement&>(n)
                                   : nullptr;
    const Node* left =
        of ? of->left.get() : static_cast<const ForInStatement&>(n).left.get();
    const Expr* right =
        of ? of->right.get() : static_cast<const ForInStatement&>(n).right.get();
    const Stmt* body =
        of ? of->body.get() : static_cast<const ForInStatement&>(n).body.get();
    if (left) {
        if (left->kind == NodeKind::VariableDeclaration) {
            visitVarDecl(static_cast<const VariableDeclaration&>(*left),
                         /*declaresLocals=*/false);
        } else if (isPatternNode(*left)) {
            visitPattern(*left, /*write=*/true);
        } else {
            visit(*left);
        }
    }
    if (right) visit(*right);
    if (body) visit(*body);
}

void StructuralWalk::visitTry(const Node& n) {
    const auto& ts = static_cast<const TryStatement&>(n);
    if (ts.tryBlock) visit(*ts.tryBlock);
    if (ts.handler) {
        std::vector<std::string> names;
        if (ts.handler->param) collectPatternNames(*ts.handler->param, names);
        bindNames(names, /*declaresLocals=*/false);
        if (ts.handler->body) visit(*ts.handler->body);
    }
    if (ts.finallyBlock) visit(*ts.finallyBlock);
}

void StructuralWalk::visitPattern(const Node& n, bool write) {
    if (stopped_) return;
    if (onNode(n)) {
        stopped_ = true;
        return;
    }
    switch (n.kind) {
        case NodeKind::Identifier: {
            const auto& id = static_cast<const Identifier&>(n);
            if (write) {
                writeIdentifier(id);
            } else {
                bindNames({id.name}, /*declaresLocals=*/patternDeclares_);
            }
            return;
        }
        case NodeKind::AssignmentPattern: {
            const auto& ap = static_cast<const AssignmentPattern&>(n);
            if (ap.right) visit(*ap.right);
            if (ap.left) visitPattern(*ap.left, write);
            return;
        }
        case NodeKind::RestElement: {
            const auto& re = static_cast<const RestElement&>(n);
            if (re.argument) visitPattern(*re.argument, write);
            return;
        }
        case NodeKind::ArrayPattern: {
            const auto& ap = static_cast<const ArrayPattern&>(n);
            for (const auto& el : ap.elements) {
                if (el) visitPattern(*el, write);
            }
            if (ap.rest) visitPattern(*ap.rest, write);
            return;
        }
        case NodeKind::ObjectPattern: {
            const auto& op = static_cast<const ObjectPattern&>(n);
            for (const auto& prop : op.properties) {
                if (!prop) continue;
                if (prop->computed && prop->key) visit(*prop->key);
                if (prop->value) visitPattern(*prop->value, write);
            }
            if (op.rest) visitPattern(*op.rest, write);
            return;
        }
        default:
            visit(n);
            return;
    }
}

void StructuralWalk::visitVarDecl(const VariableDeclaration& vd, bool declaresLocals) {
    const bool saved = patternDeclares_;
    patternDeclares_ = declaresLocals;
    for (const auto& d : vd.declarations) {
        if (!d) continue;
        boundDeclarator(*d);
        if (d->id) visitPattern(*d->id, /*write=*/false);
        if (d->init) visit(*d->init);
    }
    patternDeclares_ = saved;
}

}  // namespace detail
}  // namespace cp3
