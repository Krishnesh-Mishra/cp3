// =============================================================================
// src/cli/src/detail/hotpatch/state_map.cpp - module-scope mutable bindings
// (DEFINITIONS).
// =============================================================================
#include "detail/hotpatch/state_map.hpp"

#include <sstream>
#include <system_error>

#include "detail/core/util.hpp"
#include "detail/toolchain/incremental.hpp"
#include "detail/toolchain/interface_hash.hpp"
#include "cp3/sema.hpp"

namespace cp3::cli::hotpatch {

namespace {

// Mirror of the codegen's static-initializer predicate for declarator
// initializers that need no state and no reload (HMR: a const with a literal
// initializer can live in the lib).
bool isStaticInitExpr(const Expr& e) {
    switch (e.kind) {
        case NodeKind::NumberLiteral:
        case NodeKind::StringLiteral:
        case NodeKind::BoolLiteral:
        case NodeKind::NullLiteral:
        case NodeKind::UndefinedLiteral:
            return true;
        case NodeKind::UnaryExpr: {
            const auto& u = static_cast<const UnaryExpr&>(e);
            return u.operand && isStaticInitExpr(*u.operand);
        }
        case NodeKind::BinaryExpr: {
            const auto& b = static_cast<const BinaryExpr&>(e);
            return b.left && b.right && isStaticInitExpr(*b.left) &&
                   isStaticInitExpr(*b.right);
        }
        case NodeKind::LogicalExpr: {
            const auto& l = static_cast<const LogicalExpr&>(e);
            return l.left && l.right && isStaticInitExpr(*l.left) &&
                   isStaticInitExpr(*l.right);
        }
        case NodeKind::ConditionalExpr: {
            const auto& c = static_cast<const ConditionalExpr&>(e);
            return c.condition && c.whenTrue && c.whenFalse &&
                   isStaticInitExpr(*c.condition) &&
                   isStaticInitExpr(*c.whenTrue) &&
                   isStaticInitExpr(*c.whenFalse);
        }
        default:
            return false;
    }
}

std::string bindingNameOf(const Expr& id) {
    if (id.kind != NodeKind::Identifier) return {};
    return static_cast<const Identifier&>(id).name;
}

// Recursive collection for patterns (array and object binding forms).
void collectPatternBindingNames(const Node& pattern,
                                std::vector<std::string>& out) {
    switch (pattern.kind) {
        case NodeKind::Identifier:
            out.push_back(static_cast<const Identifier&>(pattern).name);
            return;
        case NodeKind::ArrayPattern: {
            const auto& array = static_cast<const ArrayPattern&>(pattern);
            for (const NodePtr& element : array.elements) {
                if (element) collectPatternBindingNames(*element, out);
            }
            if (array.rest) collectPatternBindingNames(*array.rest, out);
            return;
        }
        case NodeKind::ObjectPattern: {
            const auto& object = static_cast<const ObjectPattern&>(pattern);
            for (const ObjectPatternPropertyPtr& property : object.properties) {
                if (!property) continue;
                if (property->value) collectPatternBindingNames(*property->value, out);
            }
            if (object.rest) collectPatternBindingNames(*object.rest, out);
            return;
        }
        case NodeKind::RestElement: {
            const auto& rest = static_cast<const RestElement&>(pattern);
            if (rest.argument) collectPatternBindingNames(*rest.argument, out);
            return;
        }
        case NodeKind::AssignmentPattern: {
            const auto& assignment =
                static_cast<const AssignmentPattern&>(pattern);
            if (assignment.left) collectPatternBindingNames(*assignment.left, out);
            return;
        }
        default:
            return;
    }
}

const VariableDeclaration* exportedDeclarationOf(const Stmt& stmt) {
    if (stmt.kind == NodeKind::VariableDeclaration) {
        return static_cast<const VariableDeclaration*>(&stmt);
    }
    if (stmt.kind == NodeKind::ExportDeclaration) {
        const auto& ed = static_cast<const ExportDeclaration&>(stmt);
        if (ed.declaration && ed.declaration->kind == NodeKind::VariableDeclaration) {
            return static_cast<const VariableDeclaration*>(
                ed.declaration.get());
        }
    }
    return nullptr;
}

void collectVariableDeclarator(const VariableDeclarator& declarator,
                               const SemaResult& sema, ModuleStateLayout& out) {
    if (declarator.id == nullptr) return;
    if (declarator.id->kind == NodeKind::Identifier) {
        std::string name =
            static_cast<const Identifier&>(*declarator.id).name;
        const Type* type = sema.typeOf(declarator.nodeId);
        if (type == nullptr && declarator.init != nullptr) {
            type = sema.typeOf(declarator.init->nodeId);
        }
        out.fields.push_back(
            {std::move(name), typeToCanonicalText(type != nullptr ? *type
                                                                   : Type::unknown())});
        return;
    }
    // Destructured module-scope let/var binding: the names are bound to
    // `undefined` at runtime, so their slots hold the cp3 `undefined` value.
    out.hasDestructuredModuleBindings = true;
    std::vector<std::string> names;
    collectPatternBindingNames(*declarator.id, names);
    for (std::string& name : names) {
        out.fields.push_back({std::move(name), "undefined"});
    }
}

}  // namespace

ModuleStateLayout computeModuleStateLayout(const CompilationUnit& unit,
                                           const SemaResult& sema) {
    ModuleStateLayout out;
    if (unit.ast == nullptr) return out;
    for (const StmtPtr& stmt : unit.ast->body) {
        if (!stmt) continue;
        const VariableDeclaration* vd = exportedDeclarationOf(*stmt);
        if (vd == nullptr) continue;
        if (vd->kind == VarKind::Const) {
            // const bindings (literal-init or not) stay as namespace-scope
            // globals in the lib; only let/var needs the state block.
            continue;
        }
        for (const VariableDeclaratorPtr& d : vd->declarations) {
            if (d == nullptr) continue;
            collectVariableDeclarator(*d, sema, out);
        }
    }

    std::string material;
    for (const StateFieldEntry& field : out.fields) {
        material += field.name;
        material += ' ';
        material += field.typeText;
        material += '\n';
    }
    out.layoutHash = incrementalHex64(fnv1a64(material));
    out.isSwappable = !out.hasDestructuredModuleBindings;
    return out;
}

}  // namespace cp3::cli::hotpatch
