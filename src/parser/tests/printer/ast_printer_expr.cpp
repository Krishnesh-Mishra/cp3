// =============================================================================
// src/parser/tests/ast_printer_expr.cpp - webserverion, pattern, and type node
// printers. Test-only.
// =============================================================================
#include "ast_printer.h"
#include "ast_printer_internal.h"

#include <cstdio>

namespace cp3test {
using namespace cp3;

namespace {

bool printAccessExpr(const Node* n, int ind) {
    switch (n->kind) {
        case NodeKind::MemberExpr: {
            const auto& x = *static_cast<const MemberExpr*>(n);
            std::printf("  .%s optional=%d\n", x.property.c_str(), x.optional ? 1 : 0);
            printNode(x.object.get(), ind + 2);
            return true;
        }
        case NodeKind::IndexExpr: {
            const auto& x = *static_cast<const IndexExpr*>(n);
            std::printf("  optional=%d\n", x.optional ? 1 : 0);
            printNode(x.object.get(), ind + 2);
            printNode(x.index.get(), ind + 2);
            return true;
        }
        case NodeKind::CallExpr: {
            const auto& x = *static_cast<const CallExpr*>(n);
            std::printf("  optional=%d typeArgs=%zu args=%zu\n", x.optional ? 1 : 0,
                        x.typeArgs.size(), x.arguments.size());
            printNode(x.callee.get(), ind + 2);
            printTypes(x.typeArgs, ind + 2);
            printExprs(x.arguments, ind + 2);
            return true;
        }
        case NodeKind::NewExpr: {
            const auto& x = *static_cast<const NewExpr*>(n);
            std::printf("  typeArgs=%zu args=%zu\n", x.typeArgs.size(), x.arguments.size());
            printNode(x.callee.get(), ind + 2);
            printTypes(x.typeArgs, ind + 2);
            printExprs(x.arguments, ind + 2);
            return true;
        }
        default:
            return false;
    }
}

bool printOperatorExpr(const Node* n, int ind) {
    switch (n->kind) {
        case NodeKind::UnaryExpr: {
            const auto& x = *static_cast<const UnaryExpr*>(n);
            std::printf("  op=%s prefix=%d\n", unaryName(x.op), x.prefix ? 1 : 0);
            printNode(x.operand.get(), ind + 2);
            return true;
        }
        case NodeKind::UpdateExpr: {
            const auto& x = *static_cast<const UpdateExpr*>(n);
            std::printf("  op=%s prefix=%d\n", updateName(x.op), x.prefix ? 1 : 0);
            printNode(x.operand.get(), ind + 2);
            return true;
        }
        case NodeKind::BinaryExpr: {
            const auto& x = *static_cast<const BinaryExpr*>(n);
            std::printf("  op=%s\n", binaryName(x.op));
            printNode(x.left.get(), ind + 2);
            printNode(x.right.get(), ind + 2);
            return true;
        }
        case NodeKind::LogicalExpr: {
            const auto& x = *static_cast<const LogicalExpr*>(n);
            std::printf("  op=%s\n", logicalName(x.op));
            printNode(x.left.get(), ind + 2);
            printNode(x.right.get(), ind + 2);
            return true;
        }
        case NodeKind::ConditionalExpr: {
            const auto& x = *static_cast<const ConditionalExpr*>(n);
            std::printf("\n");
            printNode(x.condition.get(), ind + 2);
            printNode(x.whenTrue.get(), ind + 2);
            printNode(x.whenFalse.get(), ind + 2);
            return true;
        }
        case NodeKind::AssignmentExpr: {
            const auto& x = *static_cast<const AssignmentExpr*>(n);
            std::printf("  op=%s\n", assignName(x.op));
            printNode(x.target.get(), ind + 2);
            printNode(x.value.get(), ind + 2);
            return true;
        }
        default:
            return false;
    }
}

bool printClosureExpr(const Node* n, int ind) {
    switch (n->kind) {
        case NodeKind::ArrowFunction: {
            const auto& x = *static_cast<const ArrowFunction*>(n);
            std::printf("  isAsync=%d exprBody=%d typeParams=%zu params=%zu\n",
                        x.isAsync ? 1 : 0, x.webserverionBody ? 1 : 0, x.typeParams.size(),
                        x.params.size());
            printTypeParams(x.typeParams, ind + 2);
            printParams(x.params, ind + 2);
            printNode(x.returnType.get(), ind + 2);
            printNode(x.body.get(), ind + 2);
            return true;
        }
        case NodeKind::AwaitExpr:
            std::printf("\n");
            printNode(static_cast<const AwaitExpr*>(n)->argument.get(), ind + 2);
            return true;
        case NodeKind::RangeExpr: {
            const auto& x = *static_cast<const RangeExpr*>(n);
            std::printf("\n");
            printNode(x.start.get(), ind + 2);
            printNode(x.end.get(), ind + 2);
            return true;
        }
        default:
            return false;
    }
}

}  // namespace

bool printExprNode(const Node* n, int ind) {
    if (printAccessExpr(n, ind)) return true;
    if (printOperatorExpr(n, ind)) return true;
    if (printClosureExpr(n, ind)) return true;
    return false;
}

bool printPatternNode(const Node* n, int ind) {
    switch (n->kind) {
        case NodeKind::ArrayPattern: {
            const auto& x = *static_cast<const ArrayPattern*>(n);
            std::printf("  elements=%zu\n", x.elements.size());
            for (const auto& e : x.elements) printNode(e.get(), ind + 2);
            printNode(x.rest.get(), ind + 2);
            return true;
        }
        case NodeKind::ObjectPattern: {
            const auto& x = *static_cast<const ObjectPattern*>(n);
            std::printf("\n");
            for (const auto& p : x.properties) printNode(p.get(), ind + 2);
            printNode(x.rest.get(), ind + 2);
            return true;
        }
        case NodeKind::ObjectPatternProperty: {
            const auto& x = *static_cast<const ObjectPatternProperty*>(n);
            std::printf("  computed=%d shorthand=%d\n", x.computed ? 1 : 0,
                        x.shorthand ? 1 : 0);
            printNode(x.key.get(), ind + 2);
            printNode(x.value.get(), ind + 2);
            return true;
        }
        case NodeKind::RestElement:
            std::printf("\n");
            printNode(static_cast<const RestElement*>(n)->argument.get(), ind + 2);
            return true;
        case NodeKind::AssignmentPattern: {
            const auto& x = *static_cast<const AssignmentPattern*>(n);
            std::printf("\n");
            printNode(x.left.get(), ind + 2);
            printNode(x.right.get(), ind + 2);
            return true;
        }
        default:
            return false;
    }
}

bool printTypeNode(const Node* n, int ind) {
    switch (n->kind) {
        case NodeKind::NamedType: {
            const auto& x = *static_cast<const NamedType*>(n);
            std::printf("  name=%s typeArgs=%zu\n", x.name.c_str(), x.typeArgs.size());
            printTypes(x.typeArgs, ind + 2);
            return true;
        }
        case NodeKind::UnionType:
            std::printf("\n");
            printTypes(static_cast<const UnionType*>(n)->types, ind + 2);
            return true;
        case NodeKind::ArrayType:
            std::printf("\n");
            printNode(static_cast<const ArrayType*>(n)->element.get(), ind + 2);
            return true;
        case NodeKind::TupleType:
            std::printf("\n");
            printTypes(static_cast<const TupleType*>(n)->elements, ind + 2);
            return true;
        case NodeKind::FunctionType: {
            const auto& x = *static_cast<const FunctionType*>(n);
            std::printf("  typeParams=%zu params=%zu\n", x.typeParams.size(), x.params.size());
            printTypeParams(x.typeParams, ind + 2);
            printParams(x.params, ind + 2);
            printNode(x.returnType.get(), ind + 2);
            return true;
        }
        case NodeKind::ObjectType:
            std::printf("\n");
            for (const auto& m : static_cast<const ObjectType*>(n)->members)
                printNode(m.get(), ind + 2);
            return true;
        case NodeKind::PrefixedType: {
            const auto& x = *static_cast<const PrefixedType*>(n);
            std::printf("  prefix=%s name=%s\n", x.prefix.c_str(), x.name.c_str());
            return true;
        }
        case NodeKind::LiteralType: {
            const auto& x = *static_cast<const LiteralType*>(n);
            std::printf("  kind=%s", litKindName(x.literalKind));
            if (x.literalKind == LiteralTypeKind::String)
                std::printf(" value=%s", x.stringValue.c_str());
            if (x.literalKind == LiteralTypeKind::Number)
                std::printf(" value=%g", x.numberValue);
            if (x.literalKind == LiteralTypeKind::Bool)
                std::printf(" value=%s", x.boolValue ? "true" : "false");
            std::printf("\n");
            return true;
        }
        case NodeKind::ParenthesizedType:
            std::printf("\n");
            printNode(static_cast<const ParenthesizedType*>(n)->inner.get(), ind + 2);
            return true;
        default:
            return false;
    }
}

}  // namespace cp3test
