// =============================================================================
// src/parser/tests/ast_printer.cpp - printer helpers, enum names, the node
// dispatcher, and the literal/object category printers. Test-only.
// =============================================================================
#include "ast_printer.h"
#include "ast_printer_internal.h"

#include <cstdio>

#include "../harness/test_text.h"

namespace cp3test {
using namespace cp3;

namespace {

void pad(int n) {
    for (int i = 0; i < n; ++i) std::fputc(' ', stdout);
}

void head(const Node* n, int ind) {
    pad(ind);
    std::printf("%s #%u @%u:%u", nodeKindName(n->kind), n->nodeId,
                n->span.begin.line, n->span.begin.column);
}

}  // namespace

const char* varKindName(VarKind k) {
    switch (k) {
        case VarKind::Let: return "let";
        case VarKind::Var: return "var";
        case VarKind::Const: return "const";
    }
    return "?";
}

const char* accessName(AccessModifier a) {
    switch (a) {
        case AccessModifier::None: return "none";
        case AccessModifier::Public: return "public";
        case AccessModifier::Private: return "private";
        case AccessModifier::Protected: return "protected";
    }
    return "?";
}

const char* unaryName(UnaryOp o) {
    switch (o) {
        case UnaryOp::Not: return "!";
        case UnaryOp::Negate: return "-";
        case UnaryOp::Plus: return "+";
        case UnaryOp::BitNot: return "~";
        case UnaryOp::Typeof: return "typeof";
        case UnaryOp::Delete: return "delete";
        case UnaryOp::Void: return "void";
    }
    return "?";
}

const char* updateName(UpdateOp o) {
    return o == UpdateOp::Increment ? "++" : "--";
}

const char* binaryName(BinaryOp o) {
    switch (o) {
        case BinaryOp::Add: return "+";
        case BinaryOp::Subtract: return "-";
        case BinaryOp::Multiply: return "*";
        case BinaryOp::Divide: return "/";
        case BinaryOp::Modulo: return "%";
        case BinaryOp::Power: return "**";
        case BinaryOp::Equal: return "==";
        case BinaryOp::NotEqual: return "!=";
        case BinaryOp::StrictEqual: return "===";
        case BinaryOp::StrictNotEqual: return "!==";
        case BinaryOp::LessThan: return "<";
        case BinaryOp::GreaterThan: return ">";
        case BinaryOp::LessThanEqual: return "<=";
        case BinaryOp::GreaterThanEqual: return ">=";
        case BinaryOp::BitAnd: return "&";
        case BinaryOp::BitOr: return "|";
        case BinaryOp::BitXor: return "^";
        case BinaryOp::ShiftLeft: return "<<";
        case BinaryOp::ShiftRight: return ">>";
        case BinaryOp::UnsignedShiftRight: return ">>>";
        case BinaryOp::In: return "in";
    }
    return "?";
}

const char* logicalName(LogicalOp o) {
    switch (o) {
        case LogicalOp::And: return "&&";
        case LogicalOp::Or: return "||";
        case LogicalOp::Nullish: return "??";
    }
    return "?";
}

const char* assignName(AssignOp o) {
    switch (o) {
        case AssignOp::Assign: return "=";
        case AssignOp::Add: return "+=";
        case AssignOp::Subtract: return "-=";
        case AssignOp::Multiply: return "*=";
        case AssignOp::Divide: return "/=";
        case AssignOp::Modulo: return "%=";
        case AssignOp::Power: return "**=";
        case AssignOp::BitAnd: return "&=";
        case AssignOp::BitOr: return "|=";
        case AssignOp::BitXor: return "^=";
        case AssignOp::ShiftLeft: return "<<=";
        case AssignOp::ShiftRight: return ">>=";
        case AssignOp::UnsignedShiftRight: return ">>>=";
        case AssignOp::LogicalAnd: return "&&=";
        case AssignOp::LogicalOr: return "||=";
        case AssignOp::Nullish: return "?\?=";
    }
    return "?";
}

const char* accessorName(AccessorKind k) {
    return k == AccessorKind::Get ? "get" : "set";
}

const char* litKindName(LiteralTypeKind k) {
    switch (k) {
        case LiteralTypeKind::String: return "string";
        case LiteralTypeKind::Number: return "number";
        case LiteralTypeKind::Bool: return "bool";
    }
    return "?";
}

const char* importKindName(ImportKind k) {
    switch (k) {
        case ImportKind::Named: return "Named";
        case ImportKind::Default: return "Default";
        case ImportKind::Namespace: return "Namespace";
        case ImportKind::SideEffect: return "SideEffect";
        case ImportKind::Native: return "Native";
        case ImportKind::Cpp: return "Cpp";
    }
    return "?";
}

const char* exportKindName(ExportKind k) {
    switch (k) {
        case ExportKind::Named: return "Named";
        case ExportKind::Default: return "Default";
        case ExportKind::Declaration: return "Declaration";
        case ExportKind::ReExportAll: return "ReExportAll";
        case ExportKind::ReExportNamed: return "ReExportNamed";
    }
    return "?";
}

void printStmts(const StmtList& l, int ind) {
    for (const auto& e : l) printNode(e.get(), ind);
}
void printExprs(const ExprList& l, int ind) {
    for (const auto& e : l) printNode(e.get(), ind);
}
void printTypes(const TypeList& l, int ind) {
    for (const auto& e : l) printNode(e.get(), ind);
}
void printParams(const ParamList& l, int ind) {
    for (const auto& e : l) printNode(e.get(), ind);
}
void printTypeParams(const TypeParamList& l, int ind) {
    for (const auto& e : l) printNode(e.get(), ind);
}

void printNode(const Node* n, int ind) {
    if (!n) {
        pad(ind);
        std::printf("<null>\n");
        return;
    }
    head(n, ind);

    if (n->kind == NodeKind::Program) {
        std::printf("\n");
        printStmts(static_cast<const Program*>(n)->body, ind + 2);
        return;
    }
    if (printLiteralNode(n, ind)) return;
    if (printObjectNode(n, ind)) return;
    if (printExprNode(n, ind)) return;
    if (printPatternNode(n, ind)) return;
    if (printStmtNode(n, ind)) return;
    if (printDeclNode(n, ind)) return;
    if (printMemberNode(n, ind)) return;
    if (printTypeNode(n, ind)) return;
    std::printf("\n");
}

bool printLiteralNode(const Node* n, int ind) {
    switch (n->kind) {
        case NodeKind::NumberLiteral: {
            const auto& x = *static_cast<const NumberLiteral*>(n);
            std::printf("  value=%g raw=%s\n", x.value, x.raw.c_str());
            return true;
        }
        case NodeKind::StringLiteral: {
            const auto& x = *static_cast<const StringLiteral*>(n);
            std::printf("  value=\"%s\" raw=%s\n", escapeForPrint(x.value).c_str(),
                        x.raw.c_str());
            return true;
        }
        case NodeKind::BoolLiteral:
            std::printf("  value=%s\n",
                        static_cast<const BoolLiteral*>(n)->value ? "true" : "false");
            return true;
        case NodeKind::NullLiteral:
        case NodeKind::UndefinedLiteral:
        case NodeKind::ThisExpr:
        case NodeKind::EmptyStatement:
            std::printf("\n");
            return true;
        case NodeKind::TemplateLiteral: {
            const auto& x = *static_cast<const TemplateLiteral*>(n);
            std::printf("  quasis=%zu exprs=%zu\n", x.quasis.size(), x.expressions.size());
            for (std::size_t i = 0; i < x.quasis.size(); ++i) {
                printNode(x.quasis[i].get(), ind + 2);
                if (i < x.expressions.size()) printNode(x.expressions[i].get(), ind + 2);
            }
            return true;
        }
        case NodeKind::TemplateElement: {
            const auto& x = *static_cast<const TemplateElement*>(n);
            std::printf("  raw=[%s] cooked=[%s] tail=%d\n", escapeForPrint(x.raw).c_str(),
                        escapeForPrint(x.cooked).c_str(), x.tail ? 1 : 0);
            return true;
        }
        case NodeKind::RegExpLiteral: {
            const auto& x = *static_cast<const RegExpLiteral*>(n);
            std::printf("  pattern=%s flags=%s\n", x.pattern.c_str(), x.flags.c_str());
            return true;
        }
        case NodeKind::Identifier:
            std::printf("  name=%s\n", static_cast<const Identifier*>(n)->name.c_str());
            return true;
        case NodeKind::ArrayLiteral: {
            const auto& x = *static_cast<const ArrayLiteral*>(n);
            std::printf("  elements=%zu\n", x.elements.size());
            for (const auto& e : x.elements) printNode(e.get(), ind + 2);
            return true;
        }
        default:
            return false;
    }
}

bool printObjectNode(const Node* n, int ind) {
    switch (n->kind) {
        case NodeKind::ObjectLiteral:
            std::printf("\n");
            for (const auto& p : static_cast<const ObjectLiteral*>(n)->properties)
                printNode(p.get(), ind + 2);
            return true;
        case NodeKind::ObjectProperty: {
            const auto& x = *static_cast<const ObjectProperty*>(n);
            std::printf("  computed=%d shorthand=%d method=%d\n", x.computed ? 1 : 0,
                        x.shorthand ? 1 : 0, x.isMethod ? 1 : 0);
            printNode(x.key.get(), ind + 2);
            printNode(x.value.get(), ind + 2);
            return true;
        }
        case NodeKind::SpreadElement:
            std::printf("\n");
            printNode(static_cast<const SpreadElement*>(n)->argument.get(), ind + 2);
            return true;
        case NodeKind::WeakExpr:
            std::printf("\n");
            printNode(static_cast<const WeakExpr*>(n)->value.get(), ind + 2);
            return true;
        default:
            return false;
    }
}

}  // namespace cp3test
