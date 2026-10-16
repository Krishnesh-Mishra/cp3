// =============================================================================
// src/parser/src/node_ids.cpp — nodeId assignment (DECISIONS.md D6).
//
// Every node (including `Program`) receives a 1-based id in pre-order: a node
// is numbered before its children, and template-literal quasis/expressions are
// interleaved. Ids start at 1; 0 means "unassigned".
//
// NodeKind is grouped by category in CP3_AST_NODE_LIST (ast.hpp); the category
// ranges keep each function small and are guarded by static_asserts below.
// =============================================================================
#include "tokens.h"

#include <cstddef>

namespace cp3 {
namespace {

constexpr int kOf(NodeKind k) noexcept { return static_cast<int>(k); }

// CP3_AST_NODE_LIST groups kinds as: expressions, patterns, statements
// (incl. modules), members/params, types — in that order.
static_assert(kOf(NodeKind::RangeExpr) < kOf(NodeKind::ArrayPattern));
static_assert(kOf(NodeKind::AssignmentPattern) < kOf(NodeKind::ExpressionStatement));
static_assert(kOf(NodeKind::ExportSpecifier) < kOf(NodeKind::PropertyDeclaration));
static_assert(kOf(NodeKind::TypeParameter) < kOf(NodeKind::NamedType));

bool isExprKind(NodeKind k) noexcept {
    return kOf(k) >= kOf(NodeKind::NumberLiteral) && kOf(k) <= kOf(NodeKind::RangeExpr);
}
bool isPatternKind(NodeKind k) noexcept {
    return kOf(k) >= kOf(NodeKind::ArrayPattern) && kOf(k) <= kOf(NodeKind::AssignmentPattern);
}
bool isStmtKind(NodeKind k) noexcept {
    return kOf(k) >= kOf(NodeKind::ExpressionStatement) &&
           kOf(k) <= kOf(NodeKind::ExportSpecifier);
}
bool isMemberKind(NodeKind k) noexcept {
    return kOf(k) >= kOf(NodeKind::PropertyDeclaration) &&
           kOf(k) <= kOf(NodeKind::TypeParameter);
}

}  // namespace

void ParserImpl::assignIds(Node& node) {
    node.nodeId = nextId_++;
    if (node.kind == NodeKind::Program) {
        assignAll(static_cast<Program&>(node).body);
        return;
    }
    if (isExprKind(node.kind)) return assignExprChildren(node);
    if (isPatternKind(node.kind)) return assignPatternChildren(node);
    if (isStmtKind(node.kind)) return assignStmtChildren(node);
    if (isMemberKind(node.kind)) return assignMemberChildren(node);
    assignTypeChildren(node);
}

void ParserImpl::assignExprChildren(Node& node) {
    switch (node.kind) {
        case NodeKind::NumberLiteral:
        case NodeKind::StringLiteral:
        case NodeKind::BoolLiteral:
        case NodeKind::NullLiteral:
        case NodeKind::UndefinedLiteral:
        case NodeKind::TemplateElement:
        case NodeKind::RegExpLiteral:
        case NodeKind::Identifier:
        case NodeKind::ThisExpr:
            break;
        case NodeKind::TemplateLiteral: {
            auto& tl = static_cast<TemplateLiteral&>(node);
            std::size_t n = tl.quasis.size();
            for (std::size_t i = 0; i < n; ++i) {
                if (tl.quasis[i]) assignIds(*tl.quasis[i]);
                if (i < tl.expressions.size() && tl.expressions[i]) {
                    assignIds(*tl.expressions[i]);
                }
            }
            break;
        }
        case NodeKind::ArrayLiteral:
            assignAll(static_cast<ArrayLiteral&>(node).elements);
            break;
        case NodeKind::ObjectLiteral:
            assignAll(static_cast<ObjectLiteral&>(node).properties);
            break;
        case NodeKind::ObjectProperty: {
            auto& p = static_cast<ObjectProperty&>(node);
            assignOpt(p.key.get());
            assignOpt(p.value.get());
            break;
        }
        case NodeKind::SpreadElement:
            assignOpt(static_cast<SpreadElement&>(node).argument.get());
            break;
        case NodeKind::WeakExpr:
            assignOpt(static_cast<WeakExpr&>(node).value.get());
            break;
        case NodeKind::MemberExpr:
            assignOpt(static_cast<MemberExpr&>(node).object.get());
            break;
        case NodeKind::IndexExpr: {
            auto& e = static_cast<IndexExpr&>(node);
            assignOpt(e.object.get());
            assignOpt(e.index.get());
            break;
        }
        case NodeKind::CallExpr:
            assignCallChildren(static_cast<CallExpr&>(node));
            break;
        case NodeKind::NewExpr:
            assignCallChildren(static_cast<NewExpr&>(node));
            break;
        case NodeKind::UnaryExpr:
        case NodeKind::UpdateExpr:
        case NodeKind::BinaryExpr:
        case NodeKind::LogicalExpr:
        case NodeKind::ConditionalExpr:
        case NodeKind::AssignmentExpr:
        case NodeKind::ArrowFunction:
        case NodeKind::AwaitExpr:
        case NodeKind::RangeExpr:
            assignOperatorChildren(node);
            break;
        default:
            break;
    }
}

void ParserImpl::assignOperatorChildren(Node& node) {
    switch (node.kind) {
        case NodeKind::UnaryExpr:
            assignOpt(static_cast<UnaryExpr&>(node).operand.get());
            break;
        case NodeKind::UpdateExpr:
            assignOpt(static_cast<UpdateExpr&>(node).operand.get());
            break;
        case NodeKind::BinaryExpr: {
            auto& e = static_cast<BinaryExpr&>(node);
            assignOpt(e.left.get());
            assignOpt(e.right.get());
            break;
        }
        case NodeKind::LogicalExpr: {
            auto& e = static_cast<LogicalExpr&>(node);
            assignOpt(e.left.get());
            assignOpt(e.right.get());
            break;
        }
        case NodeKind::ConditionalExpr: {
            auto& e = static_cast<ConditionalExpr&>(node);
            assignOpt(e.condition.get());
            assignOpt(e.whenTrue.get());
            assignOpt(e.whenFalse.get());
            break;
        }
        case NodeKind::AssignmentExpr: {
            auto& e = static_cast<AssignmentExpr&>(node);
            assignOpt(e.target.get());
            assignOpt(e.value.get());
            break;
        }
        case NodeKind::ArrowFunction: {
            auto& f = static_cast<ArrowFunction&>(node);
            assignAll(f.typeParams);
            assignAll(f.params);
            assignOpt(f.returnType.get());
            assignOpt(f.body.get());
            break;
        }
        case NodeKind::AwaitExpr:
            assignOpt(static_cast<AwaitExpr&>(node).argument.get());
            break;
        case NodeKind::RangeExpr: {
            auto& e = static_cast<RangeExpr&>(node);
            assignOpt(e.start.get());
            assignOpt(e.end.get());
            break;
        }
        default:
            break;
    }
}

void ParserImpl::assignPatternChildren(Node& node) {
    switch (node.kind) {
        case NodeKind::ArrayPattern: {
            auto& p = static_cast<ArrayPattern&>(node);
            assignAll(p.elements);
            assignOpt(p.rest.get());
            break;
        }
        case NodeKind::ObjectPattern: {
            auto& p = static_cast<ObjectPattern&>(node);
            assignAll(p.properties);
            assignOpt(p.rest.get());
            break;
        }
        case NodeKind::ObjectPatternProperty: {
            auto& p = static_cast<ObjectPatternProperty&>(node);
            assignOpt(p.key.get());
            assignOpt(p.value.get());
            break;
        }
        case NodeKind::RestElement:
            assignOpt(static_cast<RestElement&>(node).argument.get());
            break;
        case NodeKind::AssignmentPattern: {
            auto& p = static_cast<AssignmentPattern&>(node);
            assignOpt(p.left.get());
            assignOpt(p.right.get());
            break;
        }
        default:
            break;
    }
}

void ParserImpl::assignStmtChildren(Node& node) {
    switch (node.kind) {
        case NodeKind::ExpressionStatement:
            assignOpt(static_cast<ExpressionStatement&>(node).expression.get());
            break;
        case NodeKind::BlockStatement:
            assignAll(static_cast<BlockStatement&>(node).body);
            break;
        case NodeKind::VariableDeclaration:
            assignAll(static_cast<VariableDeclaration&>(node).declarations);
            break;
        case NodeKind::VariableDeclarator: {
            auto& d = static_cast<VariableDeclarator&>(node);
            assignOpt(d.id.get());
            assignOpt(d.type.get());
            assignOpt(d.init.get());
            break;
        }
        case NodeKind::EmptyStatement:
        case NodeKind::BreakStatement:
        case NodeKind::ContinueStatement:
        case NodeKind::ImportSpecifier:
        case NodeKind::ExportSpecifier:
            break;
        case NodeKind::FunctionDeclaration:
        case NodeKind::ClassDeclaration:
        case NodeKind::InterfaceDeclaration:
        case NodeKind::EnumDeclaration:
        case NodeKind::EnumMember:
        case NodeKind::TypeAliasDeclaration:
            assignDeclChildren(node);
            break;
        case NodeKind::IfStatement:
        case NodeKind::WhileStatement:
        case NodeKind::DoWhileStatement:
        case NodeKind::ForStatement:
        case NodeKind::ForOfStatement:
        case NodeKind::ForInStatement:
        case NodeKind::SwitchStatement:
        case NodeKind::SwitchCase:
        case NodeKind::ReturnStatement:
        case NodeKind::ThrowStatement:
        case NodeKind::TryStatement:
        case NodeKind::CatchClause:
        case NodeKind::LabeledStatement:
            assignControlChildren(node);
            break;
        case NodeKind::ImportDeclaration:
            assignAll(static_cast<ImportDeclaration&>(node).specifiers);
            break;
        case NodeKind::ExportDeclaration: {
            auto& e = static_cast<ExportDeclaration&>(node);
            assignOpt(e.defaultExpr.get());
            assignOpt(e.declaration.get());
            assignAll(e.specifiers);
            break;
        }
        default:
            break;
    }
}

void ParserImpl::assignDeclChildren(Node& node) {
    switch (node.kind) {
        case NodeKind::FunctionDeclaration: {
            auto& f = static_cast<FunctionDeclaration&>(node);
            assignAll(f.typeParams);
            assignAll(f.params);
            assignOpt(f.returnType.get());
            assignOpt(f.body.get());
            break;
        }
        case NodeKind::ClassDeclaration: {
            auto& c = static_cast<ClassDeclaration&>(node);
            assignAll(c.typeParams);
            assignOpt(c.extendsType.get());
            assignAll(c.implementsTypes);
            assignAll(c.members);
            break;
        }
        case NodeKind::InterfaceDeclaration: {
            auto& i = static_cast<InterfaceDeclaration&>(node);
            assignAll(i.typeParams);
            assignAll(i.extendsTypes);
            assignAll(i.members);
            break;
        }
        case NodeKind::EnumDeclaration:
            assignAll(static_cast<EnumDeclaration&>(node).members);
            break;
        case NodeKind::EnumMember:
            assignOpt(static_cast<EnumMember&>(node).initializer.get());
            break;
        case NodeKind::TypeAliasDeclaration: {
            auto& t = static_cast<TypeAliasDeclaration&>(node);
            assignAll(t.typeParams);
            assignOpt(t.type.get());
            break;
        }
        default:
            break;
    }
}

void ParserImpl::assignControlChildren(Node& node) {
    switch (node.kind) {
        case NodeKind::IfStatement: {
            auto& s = static_cast<IfStatement&>(node);
            assignOpt(s.condition.get());
            assignOpt(s.thenBranch.get());
            assignOpt(s.elseBranch.get());
            break;
        }
        case NodeKind::WhileStatement: {
            auto& s = static_cast<WhileStatement&>(node);
            assignOpt(s.condition.get());
            assignOpt(s.body.get());
            break;
        }
        case NodeKind::DoWhileStatement: {
            auto& s = static_cast<DoWhileStatement&>(node);
            assignOpt(s.body.get());
            assignOpt(s.condition.get());
            break;
        }
        case NodeKind::ForStatement: {
            auto& s = static_cast<ForStatement&>(node);
            assignOpt(s.init.get());
            assignOpt(s.condition.get());
            assignOpt(s.update.get());
            assignOpt(s.body.get());
            break;
        }
        case NodeKind::ForOfStatement:
            assignForLoopChildren(static_cast<ForOfStatement&>(node));
            break;
        case NodeKind::ForInStatement:
            assignForLoopChildren(static_cast<ForInStatement&>(node));
            break;
        case NodeKind::SwitchStatement: {
            auto& s = static_cast<SwitchStatement&>(node);
            assignOpt(s.discriminant.get());
            assignAll(s.cases);
            break;
        }
        case NodeKind::SwitchCase: {
            auto& c = static_cast<SwitchCase&>(node);
            assignOpt(c.test.get());
            assignAll(c.statements);
            break;
        }
        case NodeKind::ReturnStatement:
            assignOpt(static_cast<ReturnStatement&>(node).argument.get());
            break;
        case NodeKind::ThrowStatement:
            assignOpt(static_cast<ThrowStatement&>(node).argument.get());
            break;
        case NodeKind::TryStatement: {
            auto& s = static_cast<TryStatement&>(node);
            assignOpt(s.tryBlock.get());
            assignOpt(s.handler.get());
            assignOpt(s.finallyBlock.get());
            break;
        }
        case NodeKind::CatchClause: {
            auto& c = static_cast<CatchClause&>(node);
            assignOpt(c.param.get());
            assignOpt(c.body.get());
            break;
        }
        case NodeKind::LabeledStatement:
            assignOpt(static_cast<LabeledStatement&>(node).body.get());
            break;
        default:
            break;
    }
}

void ParserImpl::assignMemberChildren(Node& node) {
    switch (node.kind) {
        case NodeKind::PropertyDeclaration: {
            auto& p = static_cast<PropertyDeclaration&>(node);
            assignOpt(p.key.get());
            assignOpt(p.type.get());
            assignOpt(p.initializer.get());
            break;
        }
        case NodeKind::MethodDeclaration: {
            auto& m = static_cast<MethodDeclaration&>(node);
            assignOpt(m.key.get());
            assignAll(m.typeParams);
            assignAll(m.params);
            assignOpt(m.returnType.get());
            assignOpt(m.body.get());
            break;
        }
        case NodeKind::ConstructorDeclaration: {
            auto& c = static_cast<ConstructorDeclaration&>(node);
            assignAll(c.params);
            assignOpt(c.body.get());
            break;
        }
        case NodeKind::AccessorDeclaration: {
            auto& a = static_cast<AccessorDeclaration&>(node);
            assignOpt(a.key.get());
            assignAll(a.params);
            assignOpt(a.returnType.get());
            assignOpt(a.body.get());
            break;
        }
        case NodeKind::PropertySignature: {
            auto& p = static_cast<PropertySignature&>(node);
            assignOpt(p.key.get());
            assignOpt(p.type.get());
            break;
        }
        case NodeKind::MethodSignature: {
            auto& m = static_cast<MethodSignature&>(node);
            assignOpt(m.key.get());
            assignAll(m.typeParams);
            assignAll(m.params);
            assignOpt(m.returnType.get());
            break;
        }
        case NodeKind::Parameter: {
            auto& p = static_cast<Parameter&>(node);
            assignOpt(p.name.get());
            assignOpt(p.type.get());
            assignOpt(p.defaultValue.get());
            break;
        }
        case NodeKind::TypeParameter: {
            auto& t = static_cast<TypeParameter&>(node);
            assignOpt(t.constraint.get());
            assignOpt(t.defaultType.get());
            break;
        }
        default:
            break;
    }
}

void ParserImpl::assignTypeChildren(Node& node) {
    switch (node.kind) {
        case NodeKind::NamedType:
            assignAll(static_cast<NamedType&>(node).typeArgs);
            break;
        case NodeKind::UnionType:
            assignAll(static_cast<UnionType&>(node).types);
            break;
        case NodeKind::ArrayType:
            assignOpt(static_cast<ArrayType&>(node).element.get());
            break;
        case NodeKind::TupleType:
            assignAll(static_cast<TupleType&>(node).elements);
            break;
        case NodeKind::FunctionType: {
            auto& f = static_cast<FunctionType&>(node);
            assignAll(f.typeParams);
            assignAll(f.params);
            assignOpt(f.returnType.get());
            break;
        }
        case NodeKind::ObjectType:
            assignAll(static_cast<ObjectType&>(node).members);
            break;
        case NodeKind::PrefixedType:
        case NodeKind::LiteralType:
            break;
        case NodeKind::ParenthesizedType:
            assignOpt(static_cast<ParenthesizedType&>(node).inner.get());
            break;
        default:
            break;
    }
}

}  // namespace cp3
