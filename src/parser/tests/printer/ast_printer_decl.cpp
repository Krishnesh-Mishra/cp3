// =============================================================================
// src/parser/tests/ast_printer_decl.cpp - declaration, module, and member node
// printers. Test-only.
// =============================================================================
#include "ast_printer.h"
#include "ast_printer_internal.h"

#include <cstdio>

namespace cp3test {
using namespace cp3;

namespace {

bool printTypeLikeDecl(const Node* n, int ind) {
    switch (n->kind) {
        case NodeKind::FunctionDeclaration: {
            const auto& x = *static_cast<const FunctionDeclaration*>(n);
            std::printf("  name=%s async=%d exported=%d typeParams=%zu params=%zu\n",
                        x.name.c_str(), x.isAsync ? 1 : 0, x.isExported ? 1 : 0,
                        x.typeParams.size(), x.params.size());
            printTypeParams(x.typeParams, ind + 2);
            printParams(x.params, ind + 2);
            printNode(x.returnType.get(), ind + 2);
            printNode(x.body.get(), ind + 2);
            return true;
        }
        case NodeKind::ClassDeclaration: {
            const auto& x = *static_cast<const ClassDeclaration*>(n);
            std::printf("  name=%s abstract=%d exported=%d typeParams=%zu implements=%zu\n",
                        x.name.c_str(), x.isAbstract ? 1 : 0, x.isExported ? 1 : 0,
                        x.typeParams.size(), x.implementsTypes.size());
            printTypeParams(x.typeParams, ind + 2);
            printNode(x.extendsType.get(), ind + 2);
            printTypes(x.implementsTypes, ind + 2);
            for (const auto& m : x.members) printNode(m.get(), ind + 2);
            return true;
        }
        case NodeKind::InterfaceDeclaration: {
            const auto& x = *static_cast<const InterfaceDeclaration*>(n);
            std::printf("  name=%s exported=%d typeParams=%zu extends=%zu\n", x.name.c_str(),
                        x.isExported ? 1 : 0, x.typeParams.size(), x.extendsTypes.size());
            printTypeParams(x.typeParams, ind + 2);
            printTypes(x.extendsTypes, ind + 2);
            for (const auto& m : x.members) printNode(m.get(), ind + 2);
            return true;
        }
        case NodeKind::EnumDeclaration: {
            const auto& x = *static_cast<const EnumDeclaration*>(n);
            std::printf("  name=%s stringBacked=%d exported=%d\n", x.name.c_str(),
                        x.stringBacked ? 1 : 0, x.isExported ? 1 : 0);
            for (const auto& m : x.members) printNode(m.get(), ind + 2);
            return true;
        }
        case NodeKind::EnumMember: {
            const auto& x = *static_cast<const EnumMember*>(n);
            std::printf("  name=%s\n", x.name.c_str());
            printNode(x.initializer.get(), ind + 2);
            return true;
        }
        case NodeKind::TypeAliasDeclaration: {
            const auto& x = *static_cast<const TypeAliasDeclaration*>(n);
            std::printf("  name=%s exported=%d typeParams=%zu\n", x.name.c_str(),
                        x.isExported ? 1 : 0, x.typeParams.size());
            printTypeParams(x.typeParams, ind + 2);
            printNode(x.type.get(), ind + 2);
            return true;
        }
        default:
            return false;
    }
}

bool printModuleDecl(const Node* n, int ind) {
    switch (n->kind) {
        case NodeKind::ImportDeclaration: {
            const auto& x = *static_cast<const ImportDeclaration*>(n);
            std::printf("  kind=%s source=%s default=%s ns=%s alias=%s\n",
                        importKindName(x.kind), x.source.c_str(), x.defaultBinding.c_str(),
                        x.namespaceBinding.c_str(), x.alias.c_str());
            for (const auto& s : x.specifiers) printNode(s.get(), ind + 2);
            return true;
        }
        case NodeKind::ImportSpecifier: {
            const auto& x = *static_cast<const ImportSpecifier*>(n);
            std::printf("  imported=%s local=%s\n", x.imported.c_str(), x.local.c_str());
            return true;
        }
        case NodeKind::ExportDeclaration: {
            const auto& x = *static_cast<const ExportDeclaration*>(n);
            std::printf("  kind=%s source=%s ns=%s\n", exportKindName(x.kind),
                        x.source.c_str(), x.namespaceExport.c_str());
            printNode(x.defaultExpr.get(), ind + 2);
            printNode(x.declaration.get(), ind + 2);
            for (const auto& s : x.specifiers) printNode(s.get(), ind + 2);
            return true;
        }
        case NodeKind::ExportSpecifier: {
            const auto& x = *static_cast<const ExportSpecifier*>(n);
            std::printf("  local=%s exported=%s\n", x.local.c_str(), x.exported.c_str());
            return true;
        }
        default:
            return false;
    }
}

}  // namespace

bool printFieldMember(const Node* n, int ind) {
    switch (n->kind) {
        case NodeKind::PropertyDeclaration: {
            const auto& x = *static_cast<const PropertyDeclaration*>(n);
            std::printf("  access=%s static=%d abstract=%d weak=%d readonly=%d "
                        "optional=%d computed=%d\n",
                        accessName(x.access), x.isStatic ? 1 : 0, x.isAbstract ? 1 : 0,
                        x.isWeak ? 1 : 0, x.isReadonly ? 1 : 0, x.optional ? 1 : 0,
                        x.computed ? 1 : 0);
            printNode(x.key.get(), ind + 2);
            printNode(x.type.get(), ind + 2);
            printNode(x.initializer.get(), ind + 2);
            return true;
        }
        case NodeKind::MethodDeclaration: {
            const auto& x = *static_cast<const MethodDeclaration*>(n);
            std::printf("  access=%s static=%d abstract=%d async=%d optional=%d "
                        "computed=%d params=%zu\n",
                        accessName(x.access), x.isStatic ? 1 : 0, x.isAbstract ? 1 : 0,
                        x.isAsync ? 1 : 0, x.optional ? 1 : 0, x.computed ? 1 : 0,
                        x.params.size());
            printNode(x.key.get(), ind + 2);
            printTypeParams(x.typeParams, ind + 2);
            printParams(x.params, ind + 2);
            printNode(x.returnType.get(), ind + 2);
            printNode(x.body.get(), ind + 2);
            return true;
        }
        case NodeKind::PropertySignature: {
            const auto& x = *static_cast<const PropertySignature*>(n);
            std::printf("  computed=%d optional=%d readonly=%d\n", x.computed ? 1 : 0,
                        x.optional ? 1 : 0, x.isReadonly ? 1 : 0);
            printNode(x.key.get(), ind + 2);
            printNode(x.type.get(), ind + 2);
            return true;
        }
        case NodeKind::MethodSignature: {
            const auto& x = *static_cast<const MethodSignature*>(n);
            std::printf("  computed=%d optional=%d hasFunction=%d params=%zu\n",
                        x.computed ? 1 : 0, x.optional ? 1 : 0, x.hasFunctionKeyword ? 1 : 0,
                        x.params.size());
            printNode(x.key.get(), ind + 2);
            printTypeParams(x.typeParams, ind + 2);
            printParams(x.params, ind + 2);
            printNode(x.returnType.get(), ind + 2);
            return true;
        }
        default:
            return false;
    }
}

bool printCallableMember(const Node* n, int ind) {
    switch (n->kind) {
        case NodeKind::ConstructorDeclaration: {
            const auto& x = *static_cast<const ConstructorDeclaration*>(n);
            std::printf("  access=%s params=%zu\n", accessName(x.access), x.params.size());
            printParams(x.params, ind + 2);
            printNode(x.body.get(), ind + 2);
            return true;
        }
        case NodeKind::AccessorDeclaration: {
            const auto& x = *static_cast<const AccessorDeclaration*>(n);
            std::printf("  accessor=%s access=%s static=%d computed=%d params=%zu\n",
                        accessorName(x.accessor), accessName(x.access), x.isStatic ? 1 : 0,
                        x.computed ? 1 : 0, x.params.size());
            printNode(x.key.get(), ind + 2);
            printParams(x.params, ind + 2);
            printNode(x.returnType.get(), ind + 2);
            printNode(x.body.get(), ind + 2);
            return true;
        }
        case NodeKind::Parameter: {
            const auto& x = *static_cast<const Parameter*>(n);
            std::printf("  optional=%d rest=%d ref=%d access=%s\n", x.optional ? 1 : 0,
                        x.isRest ? 1 : 0, x.isRef ? 1 : 0, accessName(x.access));
            printNode(x.name.get(), ind + 2);
            printNode(x.type.get(), ind + 2);
            printNode(x.defaultValue.get(), ind + 2);
            return true;
        }
        case NodeKind::TypeParameter: {
            const auto& x = *static_cast<const TypeParameter*>(n);
            std::printf("  name=%s\n", x.name.c_str());
            printNode(x.constraint.get(), ind + 2);
            printNode(x.defaultType.get(), ind + 2);
            return true;
        }
        default:
            return false;
    }
}

bool printMemberNode(const Node* n, int ind) {
    if (printFieldMember(n, ind)) return true;
    if (printCallableMember(n, ind)) return true;
    return false;
}

bool printDeclNode(const Node* n, int ind) {
    if (printTypeLikeDecl(n, ind)) return true;
    if (printModuleDecl(n, ind)) return true;
    return false;
}

}  // namespace cp3test
