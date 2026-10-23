// =============================================================================
// src/cli/src/commands/lsp_intel_walk_children.cpp - family child enumeration.
//
// OWNERSHIP: cli module (src/cli/**). Completes the declarations in
// detail/lsp_intel_walk.hpp.
//
// One switch per family, split so no function exceeds the layout budget: the
// expression family is grouped into shape, call-like and operator nodes; the
// control family into loops and the rest. A family visitor only visits
// children; the recursion itself is visitSubtree (lsp_intel_walk.cpp).
// =============================================================================
#include "detail/lsp/lsp_intel_walk.hpp"

namespace cp3::cli {
namespace {

// --- expression family, grouped by shape -------------------------------------

void visitShapeChildren(const Node& node, const Visit& visit) {
    switch (node.kind) {
        case NodeKind::TemplateLiteral: {
            const auto& literal = static_cast<const TemplateLiteral&>(node);
            for (const TemplateElementPtr& quasi : literal.quasis) {
                visitSubtree(quasi.get(), visit);
            }
            visitExprs(literal.expressions, visit);
            return;
        }
        case NodeKind::ArrayLiteral:
            visitExprs(static_cast<const ArrayLiteral&>(node).elements, visit);
            return;
        case NodeKind::ObjectLiteral: {
            const auto& literal = static_cast<const ObjectLiteral&>(node);
            for (const ObjectPropertyPtr& property : literal.properties) {
                visitSubtree(property.get(), visit);
            }
            return;
        }
        case NodeKind::SpreadElement:
            visitSubtree(static_cast<const SpreadElement&>(node).argument.get(),
                         visit);
            return;
        case NodeKind::WeakExpr:
            visitSubtree(static_cast<const WeakExpr&>(node).value.get(), visit);
            return;
        case NodeKind::MemberExpr:
            visitSubtree(static_cast<const MemberExpr&>(node).object.get(), visit);
            return;
        case NodeKind::IndexExpr: {
            const auto& index = static_cast<const IndexExpr&>(node);
            visitSubtree(index.object.get(), visit);
            visitSubtree(index.index.get(), visit);
            return;
        }
        default:
            return;
    }
}

void visitCallChildren(const Node& node, const Visit& visit) {
    switch (node.kind) {
        case NodeKind::CallExpr: {
            const auto& call = static_cast<const CallExpr&>(node);
            visitSubtree(call.callee.get(), visit);
            visitExprs(call.arguments, visit);
            return;
        }
        case NodeKind::NewExpr: {
            const auto& construct = static_cast<const NewExpr&>(node);
            visitSubtree(construct.callee.get(), visit);
            visitExprs(construct.arguments, visit);
            return;
        }
        default:
            return;
    }
}

void visitOperatorChildren(const Node& node, const Visit& visit) {
    switch (node.kind) {
        case NodeKind::UnaryExpr:
            visitSubtree(static_cast<const UnaryExpr&>(node).operand.get(), visit);
            return;
        case NodeKind::UpdateExpr:
            visitSubtree(static_cast<const UpdateExpr&>(node).operand.get(), visit);
            return;
        case NodeKind::BinaryExpr: {
            const auto& binary = static_cast<const BinaryExpr&>(node);
            visitSubtree(binary.left.get(), visit);
            visitSubtree(binary.right.get(), visit);
            return;
        }
        case NodeKind::LogicalExpr: {
            const auto& logical = static_cast<const LogicalExpr&>(node);
            visitSubtree(logical.left.get(), visit);
            visitSubtree(logical.right.get(), visit);
            return;
        }
        case NodeKind::ConditionalExpr: {
            const auto& conditional = static_cast<const ConditionalExpr&>(node);
            visitSubtree(conditional.condition.get(), visit);
            visitSubtree(conditional.whenTrue.get(), visit);
            visitSubtree(conditional.whenFalse.get(), visit);
            return;
        }
        case NodeKind::AssignmentExpr: {
            const auto& assignment = static_cast<const AssignmentExpr&>(node);
            visitSubtree(assignment.target.get(), visit);
            visitSubtree(assignment.value.get(), visit);
            return;
        }
        case NodeKind::ArrowFunction: {
            const auto& function = static_cast<const ArrowFunction&>(node);
            visitTypeParams(function.typeParams, visit);
            visitParams(function.params, visit);
            visitSubtree(function.returnType.get(), visit);
            visitSubtree(function.body.get(), visit);
            return;
        }
        case NodeKind::AwaitExpr:
            visitSubtree(static_cast<const AwaitExpr&>(node).argument.get(), visit);
            return;
        case NodeKind::RangeExpr: {
            const auto& range = static_cast<const RangeExpr&>(node);
            visitSubtree(range.start.get(), visit);
            visitSubtree(range.end.get(), visit);
            return;
        }
        default:
            return;
    }
}

// --- statement family, loops split out ---------------------------------------

void visitLoopChildren(const Node& node, const Visit& visit) {
    switch (node.kind) {
        case NodeKind::WhileStatement: {
            const auto& statement = static_cast<const WhileStatement&>(node);
            visitSubtree(statement.condition.get(), visit);
            visitSubtree(statement.body.get(), visit);
            return;
        }
        case NodeKind::DoWhileStatement: {
            const auto& statement = static_cast<const DoWhileStatement&>(node);
            visitSubtree(statement.body.get(), visit);
            visitSubtree(statement.condition.get(), visit);
            return;
        }
        case NodeKind::ForStatement: {
            const auto& loop = static_cast<const ForStatement&>(node);
            visitSubtree(loop.init.get(), visit);
            visitSubtree(loop.condition.get(), visit);
            visitSubtree(loop.update.get(), visit);
            visitSubtree(loop.body.get(), visit);
            return;
        }
        case NodeKind::ForOfStatement: {
            const auto& loop = static_cast<const ForOfStatement&>(node);
            visitSubtree(loop.left.get(), visit);
            visitSubtree(loop.right.get(), visit);
            visitSubtree(loop.body.get(), visit);
            return;
        }
        case NodeKind::ForInStatement: {
            const auto& loop = static_cast<const ForInStatement&>(node);
            visitSubtree(loop.left.get(), visit);
            visitSubtree(loop.right.get(), visit);
            visitSubtree(loop.body.get(), visit);
            return;
        }
        default:
            return;
    }
}

}  // namespace

// --- the two entry points the planner calls ----------------------------------

void visitExprChildren(const Node& node, const Visit& visit) {
    switch (node.kind) {
        case NodeKind::TemplateLiteral:
        case NodeKind::ArrayLiteral:
        case NodeKind::ObjectLiteral:
        case NodeKind::SpreadElement:
        case NodeKind::WeakExpr:
        case NodeKind::MemberExpr:
        case NodeKind::IndexExpr:
            visitShapeChildren(node, visit);
            return;
        case NodeKind::CallExpr:
        case NodeKind::NewExpr:
            visitCallChildren(node, visit);
            return;
        case NodeKind::UnaryExpr:
        case NodeKind::UpdateExpr:
        case NodeKind::BinaryExpr:
        case NodeKind::LogicalExpr:
        case NodeKind::ConditionalExpr:
        case NodeKind::AssignmentExpr:
        case NodeKind::ArrowFunction:
        case NodeKind::AwaitExpr:
        case NodeKind::RangeExpr:
            visitOperatorChildren(node, visit);
            return;
        default:
            return;  // literals, Identifier, ThisExpr
    }
}

void visitDeclStmtChildren(const Node& node, const Visit& visit) {
    switch (node.kind) {
        case NodeKind::VariableDeclaration: {
            const auto& declaration = static_cast<const VariableDeclaration&>(node);
            for (const VariableDeclaratorPtr& declarator : declaration.declarations) {
                visitSubtree(declarator.get(), visit);
            }
            return;
        }
        case NodeKind::VariableDeclarator: {
            const auto& declarator = static_cast<const VariableDeclarator&>(node);
            visitSubtree(declarator.id.get(), visit);
            visitSubtree(declarator.type.get(), visit);
            visitSubtree(declarator.init.get(), visit);
            return;
        }
        case NodeKind::FunctionDeclaration: {
            const auto& function = static_cast<const FunctionDeclaration&>(node);
            visitTypeParams(function.typeParams, visit);
            visitParams(function.params, visit);
            visitSubtree(function.returnType.get(), visit);
            visitSubtree(function.body.get(), visit);
            return;
        }
        case NodeKind::ClassDeclaration: {
            const auto& declaration = static_cast<const ClassDeclaration&>(node);
            visitTypeParams(declaration.typeParams, visit);
            visitSubtree(declaration.extendsType.get(), visit);
            visitTypes(declaration.implementsTypes, visit);
            for (const ClassMemberPtr& member : declaration.members) {
                visitSubtree(member.get(), visit);
            }
            return;
        }
        case NodeKind::InterfaceDeclaration: {
            const auto& declaration = static_cast<const InterfaceDeclaration&>(node);
            visitTypeParams(declaration.typeParams, visit);
            visitTypes(declaration.extendsTypes, visit);
            for (const SignatureMemberPtr& member : declaration.members) {
                visitSubtree(member.get(), visit);
            }
            return;
        }
        case NodeKind::EnumDeclaration: {
            const auto& declaration = static_cast<const EnumDeclaration&>(node);
            for (const EnumMemberPtr& member : declaration.members) {
                visitSubtree(member.get(), visit);
            }
            return;
        }
        case NodeKind::EnumMember:
            visitSubtree(static_cast<const EnumMember&>(node).initializer.get(),
                         visit);
            return;
        case NodeKind::TypeAliasDeclaration: {
            const auto& declaration = static_cast<const TypeAliasDeclaration&>(node);
            visitTypeParams(declaration.typeParams, visit);
            visitSubtree(declaration.type.get(), visit);
            return;
        }
        case NodeKind::ImportDeclaration: {
            const auto& declaration = static_cast<const ImportDeclaration&>(node);
            for (const ImportSpecifierPtr& specifier : declaration.specifiers) {
                visitSubtree(specifier.get(), visit);
            }
            return;
        }
        case NodeKind::ExportDeclaration: {
            const auto& declaration = static_cast<const ExportDeclaration&>(node);
            visitSubtree(declaration.declaration.get(), visit);
            visitSubtree(declaration.defaultExpr.get(), visit);
            for (const ExportSpecifierPtr& specifier : declaration.specifiers) {
                visitSubtree(specifier.get(), visit);
            }
            return;
        }
        default:
            return;  // ImportSpecifier, ExportSpecifier (leaves)
    }
}

void visitControlStmtChildren(const Node& node, const Visit& visit) {
    switch (node.kind) {
        case NodeKind::WhileStatement:
        case NodeKind::DoWhileStatement:
        case NodeKind::ForStatement:
        case NodeKind::ForOfStatement:
        case NodeKind::ForInStatement:
            visitLoopChildren(node, visit);
            return;
        case NodeKind::ExpressionStatement:
            visitSubtree(static_cast<const ExpressionStatement&>(node).expression.get(),
                         visit);
            return;
        case NodeKind::BlockStatement:
            visitStmts(static_cast<const BlockStatement&>(node).body, visit);
            return;
        case NodeKind::IfStatement: {
            const auto& statement = static_cast<const IfStatement&>(node);
            visitSubtree(statement.condition.get(), visit);
            visitSubtree(statement.thenBranch.get(), visit);
            visitSubtree(statement.elseBranch.get(), visit);
            return;
        }
        case NodeKind::SwitchStatement: {
            const auto& statement = static_cast<const SwitchStatement&>(node);
            visitSubtree(statement.discriminant.get(), visit);
            for (const SwitchCasePtr& branch : statement.cases) {
                visitSubtree(branch.get(), visit);
            }
            return;
        }
        case NodeKind::SwitchCase: {
            const auto& branch = static_cast<const SwitchCase&>(node);
            visitSubtree(branch.test.get(), visit);
            visitStmts(branch.statements, visit);
            return;
        }
        case NodeKind::ReturnStatement:
            visitSubtree(static_cast<const ReturnStatement&>(node).argument.get(),
                         visit);
            return;
        case NodeKind::ThrowStatement:
            visitSubtree(static_cast<const ThrowStatement&>(node).argument.get(),
                         visit);
            return;
        case NodeKind::TryStatement: {
            const auto& statement = static_cast<const TryStatement&>(node);
            visitSubtree(statement.tryBlock.get(), visit);
            visitSubtree(statement.handler.get(), visit);
            visitSubtree(statement.finallyBlock.get(), visit);
            return;
        }
        case NodeKind::CatchClause: {
            const auto& clause = static_cast<const CatchClause&>(node);
            visitSubtree(clause.param.get(), visit);
            visitSubtree(clause.body.get(), visit);
            return;
        }
        case NodeKind::LabeledStatement:
            visitSubtree(static_cast<const LabeledStatement&>(node).body.get(), visit);
            return;
        default:
            return;  // EmptyStatement, BreakStatement, ContinueStatement
    }
}

}  // namespace cp3::cli
