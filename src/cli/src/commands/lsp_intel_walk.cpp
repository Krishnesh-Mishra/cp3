// =============================================================================
// src/cli/src/commands/lsp_intel_walk.cpp - the AST walker (planner).
//
// OWNERSHIP: cli module (src/cli/**). Implements forEachNode
// (detail/lsp_intel.hpp); the family child enumerations live in
// lsp_intel_walk_children.cpp and the shared declarations in
// detail/lsp_intel_walk.hpp.
//
// Node kinds are aggregated into contiguous enum ranges (patterns,
// expressions, statements, class members, signatures, types); the dispatcher
// tests those ranges in order, and leaves are simply not recursed. Every node
// in a parsed program is visited exactly once.
// =============================================================================
#include "detail/lsp_intel.hpp"
#include "detail/lsp_intel_walk.hpp"

namespace cp3::cli {
namespace {

bool inRange(NodeKind kind, NodeKind first, NodeKind last) {
    return kind >= first && kind <= last;
}

bool isPatternKind(NodeKind kind) {
    return inRange(kind, NodeKind::ArrayPattern, NodeKind::AssignmentPattern);
}
bool isExprKind(NodeKind kind) {
    return inRange(kind, NodeKind::NumberLiteral, NodeKind::RangeExpr);
}
bool isStmtKind(NodeKind kind) {
    return inRange(kind, NodeKind::ExpressionStatement, NodeKind::ExportDeclaration);
}
bool isDeclStmtKind(NodeKind kind) {
    switch (kind) {
        case NodeKind::VariableDeclaration:
        case NodeKind::VariableDeclarator:
        case NodeKind::FunctionDeclaration:
        case NodeKind::ClassDeclaration:
        case NodeKind::InterfaceDeclaration:
        case NodeKind::EnumDeclaration:
        case NodeKind::EnumMember:
        case NodeKind::TypeAliasDeclaration:
        case NodeKind::ImportDeclaration:
        case NodeKind::ImportSpecifier:
        case NodeKind::ExportDeclaration:
        case NodeKind::ExportSpecifier:
            return true;
        default:
            return false;
    }
}
bool isClassMemberKind(NodeKind kind) {
    return inRange(kind, NodeKind::PropertyDeclaration,
                   NodeKind::AccessorDeclaration);
}
bool isSignatureKind(NodeKind kind) {
    return inRange(kind, NodeKind::PropertySignature, NodeKind::MethodSignature);
}
bool isTypeKind(NodeKind kind) {
    return inRange(kind, NodeKind::NamedType, NodeKind::ParenthesizedType);
}


void visitPatternChildren(const Node& node, const Visit& visit) {
    switch (node.kind) {
        case NodeKind::ArrayPattern: {
            const auto& pattern = static_cast<const ArrayPattern&>(node);
            for (const NodePtr& element : pattern.elements) {
                visitSubtree(element.get(), visit);
            }
            visitSubtree(pattern.rest.get(), visit);
            return;
        }
        case NodeKind::ObjectPattern: {
            const auto& pattern = static_cast<const ObjectPattern&>(node);
            for (const ObjectPatternPropertyPtr& property : pattern.properties) {
                visitSubtree(property.get(), visit);
            }
            visitSubtree(pattern.rest.get(), visit);
            return;
        }
        case NodeKind::ObjectPatternProperty: {
            const auto& property = static_cast<const ObjectPatternProperty&>(node);
            visitSubtree(property.key.get(), visit);
            visitSubtree(property.value.get(), visit);
            return;
        }
        case NodeKind::RestElement:
            visitSubtree(static_cast<const RestElement&>(node).argument.get(), visit);
            return;
        case NodeKind::AssignmentPattern: {
            const auto& pattern = static_cast<const AssignmentPattern&>(node);
            visitSubtree(pattern.left.get(), visit);
            visitSubtree(pattern.right.get(), visit);
            return;
        }
        default:
            return;
    }
}


void visitClassMemberChildren(const Node& node, const Visit& visit) {
    switch (node.kind) {
        case NodeKind::PropertyDeclaration: {
            const auto& member = static_cast<const PropertyDeclaration&>(node);
            visitSubtree(member.key.get(), visit);
            visitSubtree(member.type.get(), visit);
            visitSubtree(member.initializer.get(), visit);
            return;
        }
        case NodeKind::MethodDeclaration: {
            const auto& member = static_cast<const MethodDeclaration&>(node);
            visitSubtree(member.key.get(), visit);
            visitTypeParams(member.typeParams, visit);
            visitParams(member.params, visit);
            visitSubtree(member.returnType.get(), visit);
            visitSubtree(member.body.get(), visit);
            return;
        }
        case NodeKind::ConstructorDeclaration: {
            const auto& member = static_cast<const ConstructorDeclaration&>(node);
            visitParams(member.params, visit);
            visitSubtree(member.body.get(), visit);
            return;
        }
        case NodeKind::AccessorDeclaration: {
            const auto& member = static_cast<const AccessorDeclaration&>(node);
            visitSubtree(member.key.get(), visit);
            visitParams(member.params, visit);
            visitSubtree(member.returnType.get(), visit);
            visitSubtree(member.body.get(), visit);
            return;
        }
        default:
            return;
    }
}

void visitSignatureChildren(const Node& node, const Visit& visit) {
    if (node.kind == NodeKind::PropertySignature) {
        const auto& member = static_cast<const PropertySignature&>(node);
        visitSubtree(member.key.get(), visit);
        visitSubtree(member.type.get(), visit);
        return;
    }
    if (node.kind == NodeKind::MethodSignature) {
        const auto& member = static_cast<const MethodSignature&>(node);
        visitSubtree(member.key.get(), visit);
        visitTypeParams(member.typeParams, visit);
        visitParams(member.params, visit);
        visitSubtree(member.returnType.get(), visit);
    }
}

void visitTypeChildren(const Node& node, const Visit& visit) {
    switch (node.kind) {
        case NodeKind::NamedType:
            visitTypes(static_cast<const NamedType&>(node).typeArgs, visit);
            return;
        case NodeKind::UnionType:
            visitTypes(static_cast<const UnionType&>(node).types, visit);
            return;
        case NodeKind::ArrayType:
            visitSubtree(static_cast<const ArrayType&>(node).element.get(), visit);
            return;
        case NodeKind::TupleType:
            visitTypes(static_cast<const TupleType&>(node).elements, visit);
            return;
        case NodeKind::FunctionType: {
            const auto& type = static_cast<const FunctionType&>(node);
            visitParams(type.params, visit);
            visitSubtree(type.returnType.get(), visit);
            return;
        }
        case NodeKind::ObjectType: {
            const auto& type = static_cast<const ObjectType&>(node);
            for (const SignatureMemberPtr& member : type.members) {
                visitSubtree(member.get(), visit);
            }
            return;
        }
        case NodeKind::PrefixedType:
            return;  // `r_'u32'` etc.: prefix and name are strings, no children
        case NodeKind::ParenthesizedType:
            visitSubtree(static_cast<const ParenthesizedType&>(node).inner.get(), visit);
            return;
        default:
            return;  // LiteralType
    }
}


void visitChildren(const Node& node, const Visit& visit) {
    switch (node.kind) {
        case NodeKind::Program:
            visitStmts(static_cast<const Program&>(node).body, visit);
            return;
        case NodeKind::TemplateElement:
        case NodeKind::ImportSpecifier:
        case NodeKind::ExportSpecifier:
            return;  // leaves
        case NodeKind::ObjectProperty: {
            const auto& property = static_cast<const ObjectProperty&>(node);
            visitSubtree(property.key.get(), visit);
            visitSubtree(property.value.get(), visit);
            return;
        }
        case NodeKind::Parameter: {
            const auto& parameter = static_cast<const Parameter&>(node);
            visitSubtree(parameter.name.get(), visit);
            visitSubtree(parameter.type.get(), visit);
            visitSubtree(parameter.defaultValue.get(), visit);
            return;
        }
        case NodeKind::TypeParameter: {
            const auto& parameter = static_cast<const TypeParameter&>(node);
            visitSubtree(parameter.constraint.get(), visit);
            visitSubtree(parameter.defaultType.get(), visit);
            return;
        }
        default:
            break;
    }
    if (isPatternKind(node.kind)) {
        visitPatternChildren(node, visit);
        return;
    }
    if (isExprKind(node.kind)) {
        visitExprChildren(node, visit);
        return;
    }
    if (isStmtKind(node.kind)) {
        if (isDeclStmtKind(node.kind)) visitDeclStmtChildren(node, visit);
        else visitControlStmtChildren(node, visit);
        return;
    }
    if (isClassMemberKind(node.kind)) {
        visitClassMemberChildren(node, visit);
        return;
    }
    if (isSignatureKind(node.kind)) {
        visitSignatureChildren(node, visit);
        return;
    }
    if (isTypeKind(node.kind)) visitTypeChildren(node, visit);
}

}  // namespace

void visitExprs(const ExprList& list, const Visit& visit) {
    for (const ExprPtr& child : list) visitSubtree(child.get(), visit);
}

void visitStmts(const StmtList& list, const Visit& visit) {
    for (const StmtPtr& child : list) visitSubtree(child.get(), visit);
}

void visitParams(const ParamList& list, const Visit& visit) {
    for (const ParameterPtr& parameter : list) {
        visitSubtree(parameter.get(), visit);
    }
}

void visitTypeParams(const TypeParamList& list, const Visit& visit) {
    for (const TypeParameterPtr& parameter : list) {
        visitSubtree(parameter.get(), visit);
    }
}

void visitTypes(const TypeList& list, const Visit& visit) {
    for (const TypePtr& child : list) visitSubtree(child.get(), visit);
}


void visitSubtree(const Node* node, const Visit& visit) {
    if (node == nullptr) return;
    visit(*node);
    visitChildren(*node, visit);
}


void forEachNode(const Program& program, const NodeVisitor& visit) {
    visitSubtree(&program, visit);
}

}  // namespace cp3::cli
