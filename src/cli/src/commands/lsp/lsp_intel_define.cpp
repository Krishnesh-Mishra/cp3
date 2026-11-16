// =============================================================================
// src/cli/src/commands/lsp_intel_define.cpp - go-to-definition.
//
// OWNERSHIP: cli module (src/cli/**). Implements definitionAt
// (detail/lsp_intel.hpp).
//
// Answers only where the compiler has a declaration to point at:
//   * a name declared in the same file -> its declaration (the nearest one at
//     or before the cursor, so shadowing resolves to the right binding);
//   * a name bound from a LOCAL module import -> the exported declaration in
//     the target file, through the module graph's resolved dependency edge;
//     an import binding whose target's declaration cannot be proven points at
//     the binding's own site in this file (std/package/native never do);
//   * a member position (`receiver.member`, or a member's own key) -> the
//     declaration(s) that declare the member: the member's key span on the
//     owning class/interface (this file or an analyzed module), on the
//     owners it extends, and on the interfaces it implements. What it cannot
//     prove answers nothing;
//   * a std/package/native import -> no location (empty), never a wrong one.
// Namespace imports deliberately answer nothing. Member definitions walk the
// owner declaration the way the outline does (collectClassMembers /
// collectSignatureMembers): the member key carries the name, so a member
// jump lands on the key span, never on the enclosing member statement.
// =============================================================================
#include "detail/lsp/lsp_intel.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/lsp.hpp"
#include "cp3/meta/module.hpp"
#include "cp3/core/source.hpp"

namespace cp3::cli {
namespace {

std::string declarationName(const Stmt& statement) {
    switch (statement.kind) {
        case NodeKind::FunctionDeclaration:
            return static_cast<const FunctionDeclaration&>(statement).name;
        case NodeKind::ClassDeclaration:
            return static_cast<const ClassDeclaration&>(statement).name;
        case NodeKind::InterfaceDeclaration:
            return static_cast<const InterfaceDeclaration&>(statement).name;
        case NodeKind::EnumDeclaration:
            return static_cast<const EnumDeclaration&>(statement).name;
        case NodeKind::TypeAliasDeclaration:
            return static_cast<const TypeAliasDeclaration&>(statement).name;
        default:
            return std::string();
    }
}

ExportHit declaratorHit(const VariableDeclaration& declaration,
                        const std::string& name) {
    for (const VariableDeclaratorPtr& declarator : declaration.declarations) {
        if (declarator == nullptr || declarator->id == nullptr) continue;
        if (declarator->id->kind != NodeKind::Identifier) continue;
        const auto& id = static_cast<const Identifier&>(*declarator->id);
        if (id.name == name) return ExportHit{declarator.get(), name};
    }
    return {};
}

// `name` declared directly by this statement (variables check their
// declarators), or an empty hit.
ExportHit hitInStatement(const Stmt& statement, const std::string& name) {
    switch (statement.kind) {
        case NodeKind::FunctionDeclaration:
        case NodeKind::ClassDeclaration:
        case NodeKind::InterfaceDeclaration:
        case NodeKind::EnumDeclaration:
        case NodeKind::TypeAliasDeclaration:
            if (declarationName(statement) == name) return ExportHit{&statement, name};
            return {};
        case NodeKind::VariableDeclaration:
            return declaratorHit(static_cast<const VariableDeclaration&>(statement),
                                 name);
        default:
            return {};
    }
}

// The declaration of `name` in `program`, including declarations wrapped in
// `export <declaration>` (the parser may attach either shape).
ExportHit findDirect(const Program& program, const std::string& name) {
    for (const StmtPtr& statement : program.body) {
        if (statement == nullptr || !statement->span.valid()) continue;
        ExportHit hit = hitInStatement(*statement, name);
        if (hit.node != nullptr) return hit;
        if (statement->kind != NodeKind::ExportDeclaration) continue;
        const auto& declaration = static_cast<const ExportDeclaration&>(*statement);
        if (declaration.declaration == nullptr) continue;
        hit = hitInStatement(*declaration.declaration, name);
        if (hit.node != nullptr) return hit;
    }
    return {};
}

// The default export's declaration, when the module has one.
ExportHit findDefault(const Program& program) {
    for (std::size_t i = 0; i < program.body.size(); ++i) {
        const StmtPtr& statement = program.body[i];
        if (statement == nullptr || statement->kind != NodeKind::ExportDeclaration) {
            continue;
        }
        const auto& declaration = static_cast<const ExportDeclaration&>(*statement);
        if (declaration.kind != ExportKind::Default) continue;
        if (declaration.declaration != nullptr) {
            return ExportHit{declaration.declaration.get(),
                             declarationName(*declaration.declaration)};
        }
        if (declaration.defaultExpr == nullptr) {
            // The parser cannot attach interface/type/enum to a default
            // export inline: it emits an empty shell and leaves the
            // declaration as the following top-level statement. That
            // statement is the default export's declaration.
            if (i + 1 < program.body.size()) {
                const StmtPtr& next = program.body[i + 1];
                if (next != nullptr &&
                    (next->kind == NodeKind::InterfaceDeclaration ||
                     next->kind == NodeKind::TypeAliasDeclaration ||
                     next->kind == NodeKind::EnumDeclaration)) {
                    return ExportHit{next.get(), declarationName(*next)};
                }
            }
            continue;
        }
        if (declaration.defaultExpr->kind == NodeKind::Identifier) {
            return ExportHit{
                declaration.defaultExpr.get(),
                static_cast<const Identifier&>(*declaration.defaultExpr).name};
        }
        return ExportHit{declaration.defaultExpr.get(), std::string()};
    }
    return {};
}

// The declaration node with the given id, or nullptr.
const Node* findNodeById(const Program& program, std::uint32_t nodeId) {
    if (nodeId == 0) return nullptr;
    const Node* found = nullptr;
    forEachNode(program, [&](const Node& node) {
        if (found != nullptr || node.nodeId != nodeId) return;
        found = &node;
    });
    return found;
}

// The name a declaration node declares (a function, a class, a variable).
std::string declaredNameOf(const Node& node) {
    switch (node.kind) {
        case NodeKind::FunctionDeclaration:
            return static_cast<const FunctionDeclaration&>(node).name;
        case NodeKind::ClassDeclaration:
            return static_cast<const ClassDeclaration&>(node).name;
        case NodeKind::InterfaceDeclaration:
            return static_cast<const InterfaceDeclaration&>(node).name;
        case NodeKind::EnumDeclaration:
            return static_cast<const EnumDeclaration&>(node).name;
        case NodeKind::TypeAliasDeclaration:
            return static_cast<const TypeAliasDeclaration&>(node).name;
        case NodeKind::VariableDeclarator: {
            const auto& declarator = static_cast<const VariableDeclarator&>(node);
            if (declarator.id != nullptr &&
                declarator.id->kind == NodeKind::Identifier) {
                return static_cast<const Identifier&>(*declarator.id).name;
            }
            return std::string();
        }
        default:
            return std::string();
    }
}

std::optional<lsp::SourceTarget> targetOf(const SourceSpan& span) {
    lsp::SourceTarget target;
    target.path = span.file->path;
    target.line = span.begin.line;
    target.column = span.begin.column;
    target.endLine = span.end.line;
    target.endColumn = span.end.column;
    return target;
}

// The exported declaration a local import should jump to.
ExportHit findExportedImpl(const Program& program, const std::string& name,
                            bool wantDefault) {
    if (wantDefault) return findDefault(program);
    const ExportHit direct = findDirect(program, name);
    if (direct.node != nullptr) return direct;
    // `export { local as name }` renames a local declaration.
    for (const StmtPtr& statement : program.body) {
        if (statement == nullptr || statement->kind != NodeKind::ExportDeclaration) {
            continue;
        }
        const auto& declaration = static_cast<const ExportDeclaration&>(*statement);
        for (const ExportSpecifierPtr& specifier : declaration.specifiers) {
            if (specifier == nullptr || specifier->exported != name) continue;
            if (specifier->local.empty() || specifier->local == name) return {};
            return findDirect(program, specifier->local);
        }
    }
    return {};
}

// The import declaration that binds `name` in this unit's file, or nullptr.
const ImportDeclaration* importForNameImpl(const Program& program,
                                           const std::string& name,
                                           const SourceFile& file) {
    for (const StmtPtr& statement : program.body) {
        if (statement == nullptr || statement->kind != NodeKind::ImportDeclaration) {
            continue;
        }
        if (!statement->span.valid() || statement->span.file != &file) continue;
        const auto& import = static_cast<const ImportDeclaration&>(*statement);
        if (import.defaultBinding == name || import.namespaceBinding == name ||
            import.alias == name) {
            return &import;
        }
        for (const ImportSpecifierPtr& specifier : import.specifiers) {
            if (specifier == nullptr) continue;
            const std::string& local =
                specifier->local.empty() ? specifier->imported : specifier->local;
            if (local == name) return &import;
        }
    }
    return nullptr;
}

// True when `symbol` is one of the bindings the import statement itself
// declares. Named specifiers get a span on the identifier while default
// bindings may carry the statement span, so containment (not equality) is the
// test; a local declaration that shadows the import starts after the
// statement's end and is therefore not an import binding.
bool isImportBinding(const SymbolInfo& symbol, const ImportDeclaration& import) {
    if (!symbol.span.valid() || symbol.span.file != import.span.file) return false;
    return symbol.span.begin.offset >= import.span.begin.offset &&
           symbol.span.begin.offset < import.span.end.offset;
}

const ModuleDependency* dependencyForImpl(const ModuleGraph& graph,
                                          std::uint32_t importer,
                                          const std::string& specifier) {
    for (const ModuleDependency& dependency : graph.dependencies()) {
        if (dependency.importer == importer && dependency.specifier == specifier) {
            return &dependency;
        }
    }
    return nullptr;
}

// The unit a re-export (`export ... from <source>`) pulls names from, or
// nullptr when the edge is not a resolvable local module.
CompilationUnit* reExportUnitOf(const ModuleGraph& graph,
                                std::uint32_t importer,
                                const std::string& source) {
    const ModuleDependency* dependency = dependencyForImpl(graph, importer, source);
    if (dependency == nullptr || !dependency->resolved ||
        dependency->resolution != ModuleResolution::Local) {
        return nullptr;
    }
    return graph.unit(dependency->imported);
}

// The chain step a local import binding reaches: mirrors `importHopOf` in
// resolveExportChain. Namespace and native aliases carry no single
// declaration and refuse.
struct ImportHop {
    CompilationUnit* unit = nullptr;
    std::string name;      // empty when the step is a default binding
    bool wantDefault = false;
};

bool importHopOf(const ModuleGraph& graph, const CompilationUnit& unit,
                 const std::string& local, ImportHop& out) {
    const Program& program = *unit.ast;
    const ImportDeclaration* import =
        importForNameImpl(program, local, *unit.file);
    if (import == nullptr) return false;
    CompilationUnit* target = reExportUnitOf(graph, unit.id, import->source);
    if (target == nullptr) return false;
    if (import->defaultBinding == local) {
        out.unit = target;
        out.name = std::string();
        out.wantDefault = true;
        return true;
    }
    for (const ImportSpecifierPtr& specifier : import->specifiers) {
        if (specifier == nullptr) continue;
        const std::string& specLocal = specifier->local.empty()
                                           ? specifier->imported
                                           : specifier->local;
        if (specLocal != local) continue;
        out.unit = target;
        out.name = specifier->imported;
        out.wantDefault = false;
        return true;
    }
    return false;  // namespace/native alias binding
}

// The imported name's declaration in the target module, or nothing (std,
// package, native, cpp and unresolved imports answer no location).
std::vector<lsp::SourceTarget> importedTargets(
    const AnalyzedDocument& document, const ImportDeclaration& import,
    const std::string& localName) {
    std::vector<lsp::SourceTarget> targets;
    if (document.graph == nullptr || document.entry == nullptr) return targets;
    const ModuleDependency* dependency =
        dependencyForImpl(*document.graph, document.entry->id, import.source);
    if (dependency == nullptr || !dependency->resolved ||
        dependency->resolution != ModuleResolution::Local) {
        return targets;
    }
    CompilationUnit* target = document.graph->unit(dependency->imported);
    if (target == nullptr || target->ast == nullptr) return targets;
    const bool wantDefault = import.defaultBinding == localName;
    std::string importedName;
    bool matched = wantDefault;
    for (const ImportSpecifierPtr& specifier : import.specifiers) {
        if (specifier == nullptr) continue;
        const std::string& local =
            specifier->local.empty() ? specifier->imported : specifier->local;
        if (local != localName) continue;
        importedName = specifier->imported;
        matched = true;
        break;
    }
    if (!matched) return targets;  // namespace/alias binding: no single declaration
    // Resolve through re-export chains (`export { x as y } from "./m"`,
    // `export * from "./m"`): the terminal declaration lives in the module
    // that declares it, not the re-exporting barrel.
    const std::optional<ExportedDeclaration> hit = resolveExportChain(
        *document.graph, target, importedName, wantDefault);
    if (!hit.has_value()) return targets;
    const std::optional<lsp::SourceTarget> found =
        targetOf(nameSpanIn(hit->declaration->span, hit->name));
    if (found.has_value()) targets.push_back(*found);
    return targets;
}

// Target of the declaration at a member position. Member declarations keep no
// binding table entry, so these walk the same AST shapes the outline does:
// the owner declaration's member keys carry the name position, and the
// member-side-of-access position re-resolves through the member fallback in
// lsp_intel_members.cpp (the same retarget references and rename use to leave
// the binding pipeline). The owner may live in this file or any analyzed
// module unit (preludes excluded).

// The plain `extends Name` base of a class, or empty.
std::string baseClassName(const ClassDeclaration& declaration) {
    if (declaration.extendsType == nullptr ||
        declaration.extendsType->kind != NodeKind::NamedType) {
        return std::string();
    }
    return static_cast<const NamedType&>(*declaration.extendsType).name;
}

// The name position of the member whose key carries `name` (a class member or
// a signature member), or an empty optional. An Identifier key is already the
// position; a string key carves the same way the outline does. Computed and
// number keys never participate.
std::optional<SourceSpan> memberNameSpan(const Node& member,
                                         const std::string& name) {
    const Expr* key = nullptr;
    bool computed = false;
    switch (member.kind) {
        case NodeKind::PropertyDeclaration:
            key = static_cast<const PropertyDeclaration&>(member).key.get();
            computed = static_cast<const PropertyDeclaration&>(member).computed;
            break;
        case NodeKind::MethodDeclaration:
            key = static_cast<const MethodDeclaration&>(member).key.get();
            computed = static_cast<const MethodDeclaration&>(member).computed;
            break;
        case NodeKind::AccessorDeclaration:
            key = static_cast<const AccessorDeclaration&>(member).key.get();
            computed = static_cast<const AccessorDeclaration&>(member).computed;
            break;
        case NodeKind::PropertySignature:
            key = static_cast<const PropertySignature&>(member).key.get();
            computed = static_cast<const PropertySignature&>(member).computed;
            break;
        case NodeKind::MethodSignature:
            key = static_cast<const MethodSignature&>(member).key.get();
            computed = static_cast<const MethodSignature&>(member).computed;
            break;
        default:
            return std::nullopt;
    }
    if (computed || key == nullptr) return std::nullopt;
    // keyName spells identifier and string keys (number keys spell nothing).
    if (keyName(key) != name) return std::nullopt;
    const SourceSpan carved = key->kind == NodeKind::Identifier
                                  ? key->span
                                  : nameSpanIn(key->span, name);
    if (!carved.valid()) return std::nullopt;
    return carved;
}

// The binding name of a parameter: the identifier it binds, the argument of
// a rest parameter, nothing for a pattern.
std::string bindingNameOfParameter(const Parameter& parameter) {
    const Node* name = parameter.name.get();
    if (name == nullptr) return std::string();
    if (name->kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*name).name;
    }
    if (name->kind == NodeKind::RestElement) {
        const auto& rest = static_cast<const RestElement&>(*name);
        if (rest.argument != nullptr &&
            rest.argument->kind == NodeKind::Identifier) {
            return static_cast<const Identifier&>(*rest.argument).name;
        }
    }
    return std::string();
}

// The identifier name of the receiver of the member expression the cursor
// sits on, or empty when the receiver is not a plain identifier.
std::string receiverNameAt(const Program& program, const SourceFile& file,
                           std::uint32_t line, std::uint32_t column) {
    const Node* node = innermostNodeAt(program, file, line, column);
    // A cursor sitting just past the property's last byte is outside every
    // span; step back one byte and re-resolve the same position.
    if (node == nullptr && column > 1) {
        node = innermostNodeAt(program, file, line, column - 1);
    }
    if (node == nullptr || node->kind != NodeKind::MemberExpr) {
        return std::string();
    }
    const auto& expr = static_cast<const MemberExpr&>(*node);
    const Expr* receiver = expr.object.get();
    if (receiver == nullptr || receiver->kind != NodeKind::Identifier) {
        return std::string();
    }
    return static_cast<const Identifier&>(*receiver).name;
}

// The declared type name of `name` in this unit: an annotated parameter or
// variable, or a `new X()` initializer. Empty when there is no annotation and
// no construction. Structural type labels carry the field list, not the
// interface's name, so this is where the owning type is spelled.
std::string declaredTypeNameOfLocal(const Program& program,
                                    const std::string& name) {
    if (name.empty()) return std::string();
    std::string found;
    forEachNode(program, [&](const Node& node) {
        if (!found.empty()) return;
        if (node.kind == NodeKind::Parameter) {
            const auto& parameter = static_cast<const Parameter&>(node);
            if (bindingNameOfParameter(parameter) != name) return;
            if (parameter.type != nullptr &&
                parameter.type->kind == NodeKind::NamedType) {
                found = static_cast<const NamedType&>(*parameter.type).name;
            }
            return;
        }
        if (node.kind != NodeKind::VariableDeclarator) return;
        const auto& declarator = static_cast<const VariableDeclarator&>(node);
        if (declarator.id == nullptr || declarator.id->kind != NodeKind::Identifier) {
            return;
        }
        if (static_cast<const Identifier&>(*declarator.id).name != name) return;
        if (declarator.type != nullptr &&
            declarator.type->kind == NodeKind::NamedType) {
            found = static_cast<const NamedType&>(*declarator.type).name;
            return;
        }
        if (declarator.init != nullptr &&
            declarator.init->kind == NodeKind::NewExpr) {
            const auto& construction =
                static_cast<const NewExpr&>(*declarator.init);
            if (construction.callee != nullptr &&
                construction.callee->kind == NodeKind::Identifier) {
                found = static_cast<const Identifier&>(*construction.callee).name;
            }
        }
    });
    return found;
}

// Collects the name spans of the members named `name` on the nominal owner
// `ownerName` (a class or an interface declaration), searching the entry unit
// first and then the analyzed import closure (preludes excluded). A class
// owner contributes its own member, its base classes' members and the
// members of the interfaces it implements; an interface owner contributes its
// own member and its parent interfaces'. At most one declaration of
// `ownerName` participates (entry wins over an imported one); every owner it
// reaches through implements/extends contributes. `seen` keys the walk on
// <kind>:<name> so a self-referential chain stops.
void collectOwnerMemberSpans(const std::vector<const CompilationUnit*>& units,
                             NodeKind ownerKind, const std::string& ownerName,
                             const std::string& name, int depth,
                             std::unordered_set<std::string>& seen,
                             std::vector<SourceSpan>& out) {
    if (ownerName.empty() || depth > 8) return;
    const bool wantClass = ownerKind == NodeKind::ClassDeclaration;
    const std::string key = (wantClass ? "class:" : "interface:") + ownerName;
    if (!seen.insert(key).second) return;
    for (const CompilationUnit* unit : units) {
        if (unit == nullptr || unit->ast == nullptr) continue;
        for (const StmtPtr& statement : unit->ast->body) {
            if (statement == nullptr || !statement->span.valid()) continue;
            const Stmt* declared = statement.get();
            if (declared->kind == NodeKind::ExportDeclaration) {
                const auto& declaration =
                    static_cast<const ExportDeclaration&>(*declared);
                if (declaration.declaration == nullptr) continue;
                declared = declaration.declaration.get();
            }
            if (wantClass) {
                if (declared->kind != NodeKind::ClassDeclaration) continue;
                const auto& owner = static_cast<const ClassDeclaration&>(*declared);
                if (owner.name != ownerName) continue;
                for (const ClassMemberPtr& member : owner.members) {
                    if (member == nullptr) continue;
                    if (std::optional<SourceSpan> found =
                            memberNameSpan(*member, name)) {
                        out.push_back(*found);
                    }
                }
                const std::string base = baseClassName(owner);
                if (!base.empty()) {
                    collectOwnerMemberSpans(units, NodeKind::ClassDeclaration,
                                            base, name, depth + 1, seen, out);
                }
                for (const TypePtr& type : owner.implementsTypes) {
                    if (type == nullptr || type->kind != NodeKind::NamedType) {
                        continue;
                    }
                    collectOwnerMemberSpans(units, NodeKind::InterfaceDeclaration,
                                            static_cast<const NamedType&>(*type).name,
                                            name, depth + 1, seen, out);
                }
                return;  // the first declaration in unit order wins
            }
            if (declared->kind != NodeKind::InterfaceDeclaration) continue;
            const auto& owner =
                static_cast<const InterfaceDeclaration&>(*declared);
            if (owner.name != ownerName) continue;
            for (const SignatureMemberPtr& member : owner.members) {
                if (member == nullptr) continue;
                if (std::optional<SourceSpan> found =
                        memberNameSpan(*member, name)) {
                    out.push_back(*found);
                }
            }
            for (const TypePtr& type : owner.extendsTypes) {
                if (type == nullptr || type->kind != NodeKind::NamedType) {
                    continue;
                }
                collectOwnerMemberSpans(units, NodeKind::InterfaceDeclaration,
                                        static_cast<const NamedType&>(*type).name,
                                        name, depth + 1, seen, out);
            }
            return;  // the first declaration in unit order wins
        }
    }
}

// Definition for a member position. The member's own key position answers
// with that position (a no-op jump, exactly like selfTarget); the member side
// of `receiver.member` (or a bare name inside a class body) answers with
// every declaration that declares the member on the receiver's owner: the
// owner itself (a class or interface declared in this unit or an imported
// one), the classes it extends, and the interfaces it implements. Either
// case re-resolves through memberRetarget, so declared members and their keys
// stay in sync with the reference/rename fallback. Duplicate landing spots
// (a member reached through an implements and an extends clause) drop out; a
// member the owner does not declare answers nothing.
std::vector<lsp::SourceTarget> memberDefinitionTargets(
    AnalyzedDocument& document, const std::string& path,
    std::uint32_t line, std::uint32_t column) {
    std::vector<lsp::SourceTarget> targets;
    if (!analysisReady(document)) return targets;
    // A cursor sitting just past a member's key (or any name) is not inside
    // its span; step back one byte and re-resolve the same position.
    auto retargeted = memberRetarget(document, path, line, column);
    if (!retargeted.has_value() && column > 1) {
        retargeted = memberRetarget(document, path, line, column - 1);
    }
    if (!retargeted.has_value()) return targets;
    // A cursor sitting on a member's own key: the key position is the
    // declaration, no owner lookup needed.
    if (retargeted->ownerNodeId != 0) {
        targets.push_back(retargeted->declaration);
        return targets;
    }
    std::string owner = retargeted->ownerLabel;
    NodeKind ownerKind = retargeted->ownerKind;
    if (retargeted->ownerKind == NodeKind::ObjectType) {
        // An interface is a structural shape: the label is the field list,
        // not the interface's name. The binding site spells it
        // (`s: Sized`, `const c: Config`, `new X()`).
        const std::string receiver = receiverNameAt(*document.entry->ast,
                                                    *document.file,
                                                    line, column);
        owner = declaredTypeNameOfLocal(*document.entry->ast, receiver);
        ownerKind = NodeKind::InterfaceDeclaration;
    } else if (retargeted->ownerKind != NodeKind::ClassDeclaration &&
               retargeted->ownerKind != NodeKind::InterfaceDeclaration) {
        // An enum owner carries its own name in the label; an enum member's
        // declaration is the enum declaration itself, which the enum-side
        // scaffolding reports elsewhere. Not a class/interface member: refuse.
        return targets;
    }
    if (owner.empty()) return targets;
    const std::vector<const CompilationUnit*> units =
        reachableUnitsFor(document);
    std::unordered_set<std::string> seen;
    std::vector<SourceSpan> spans;
    collectOwnerMemberSpans(units, ownerKind, owner, retargeted->name, 0, seen,
                            spans);
    std::unordered_set<std::string> emitted;
    for (const SourceSpan& span : spans) {
        if (!span.valid()) continue;
        const std::optional<lsp::SourceTarget> target = targetOf(span);
        if (!target.has_value()) continue;
        const std::string key =
            target->path + ":" + std::to_string(target->line) + ":" +
            std::to_string(target->column) + ":" +
            std::to_string(target->endLine) + ":" +
            std::to_string(target->endColumn);
        if (!emitted.insert(key).second) continue;
        targets.push_back(*target);
    }
    return targets;
}

// Cursor on a declaration's own name: answer the declaration (a no-op jump
// that still tells the client "this is the definition").
std::vector<lsp::SourceTarget> selfTarget(const Node& node) {
    std::vector<lsp::SourceTarget> targets;
    std::string name;
    if (node.kind == NodeKind::FunctionDeclaration ||
        node.kind == NodeKind::ClassDeclaration ||
        node.kind == NodeKind::InterfaceDeclaration ||
        node.kind == NodeKind::EnumDeclaration ||
        node.kind == NodeKind::TypeAliasDeclaration) {
        name = declarationName(static_cast<const Stmt&>(node));
    } else if (node.kind == NodeKind::VariableDeclarator) {
        const auto& declarator = static_cast<const VariableDeclarator&>(node);
        if (declarator.id != nullptr && declarator.id->kind == NodeKind::Identifier) {
            name = static_cast<const Identifier&>(*declarator.id).name;
        }
    }
    if (name.empty()) return targets;
    const std::optional<lsp::SourceTarget> found =
        targetOf(nameSpanIn(node.span, name));
    if (found.has_value()) targets.push_back(*found);
    return targets;
}

}  // namespace

// The exported declaration a local import binds.
ExportHit findExported(const Program& program, const std::string& name,
                       bool wantDefault) {
    return findExportedImpl(program, name, wantDefault);
}
// The terminal declaration for `name` as `unit` exports it, following the
// re-export chain (`export { x as y } from "./m"`, `export * from "./m"`)
// across Local module edges. std/package/native/unknown hops refuse. On
// success `name` is the declaration's own name in `unit`.
std::optional<ExportedDeclaration> resolveExportChain(
    const ModuleGraph& graph, CompilationUnit* unit, const std::string& name,
    bool wantDefault) {
    std::string wanted = name;
    bool unresolvedDefault = wantDefault;
    const std::size_t maxHops = graph.units().size() + 1;
    for (std::size_t hop = 0; hop < maxHops; ++hop) {
        if (unit == nullptr || unit->ast == nullptr) return std::nullopt;
        const Program& program = *unit->ast;
        if (unresolvedDefault) {
            const ExportHit hit = findDefault(program);
            if (hit.node == nullptr) return std::nullopt;
            if (hit.node->kind == NodeKind::Identifier) {
                // `export default <identifier>`: the binding the identifier
                // refers to - follow it (an import binding or a local
                // declaration).
                const std::string local =
                    static_cast<const Identifier&>(*hit.node).name;
                ImportHop step;
                if (importHopOf(graph, *unit, local, step)) {
                    unit = step.unit;
                    wanted = step.name;
                    unresolvedDefault = step.wantDefault;
                    continue;
                }
                const ExportHit localHit = findDirect(program, local);
                if (localHit.node != nullptr) {
                    return ExportedDeclaration{unit, localHit.node,
                                               localHit.name};
                }
                return std::nullopt;
            }
            return ExportedDeclaration{unit, hit.node, hit.name};
        }
        const ExportHit direct = findDirect(program, wanted);
        if (direct.node != nullptr) {
            return ExportedDeclaration{unit, direct.node, direct.name};
        }
        // A re-export hop: `export { local as wanted } from "./m"` (or a
        // plain alias over a local declaration), or `export * from "./m"`.
        bool followed = false;
        for (const StmtPtr& statement : program.body) {
            if (followed) break;
            if (statement == nullptr || statement->kind !=
                                       NodeKind::ExportDeclaration) {
                continue;
            }
            const auto& declaration =
                static_cast<const ExportDeclaration&>(*statement);
            if (declaration.kind == ExportKind::ReExportAll) {
                CompilationUnit* target =
                    reExportUnitOf(graph, unit->id, declaration.source);
                if (target == nullptr || target->ast == nullptr) {
                    return std::nullopt;
                }
                unit = target;
                followed = true;
                break;
            }
            if (declaration.kind == ExportKind::Default) continue;
            for (const ExportSpecifierPtr& specifier : declaration.specifiers) {
                if (specifier == nullptr) continue;
                if (specifier->exported != wanted) continue;
                if (declaration.source.empty()) {
                    // An alias over a local declaration or a local import
                    // binding, not a sourced re-export.
                    const ExportHit localHit =
                        findDirect(program, specifier->local);
                    if (localHit.node != nullptr) {
                        return ExportedDeclaration{unit, localHit.node,
                                                   localHit.name};
                    }
                    ImportHop step;
                    if (importHopOf(graph, *unit, specifier->local, step)) {
                        unit = step.unit;
                        wanted = step.name;
                        unresolvedDefault = step.wantDefault;
                        followed = true;
                        break;
                    }
                    return std::nullopt;
                }
                CompilationUnit* target =
                    reExportUnitOf(graph, unit->id, declaration.source);
                if (target == nullptr || target->ast == nullptr) {
                    return std::nullopt;
                }
                unit = target;
                wanted = specifier->local;
                followed = true;
                break;
            }
        }
        if (!followed) return std::nullopt;
    }
    return std::nullopt;
}

// The entry first, then the units the analyzed import closure contains for
// it; preludes are excluded. An unresolved local import contributes nothing.
std::vector<const CompilationUnit*> reachableUnitsFor(
    const AnalyzedDocument& document) {
    std::vector<const CompilationUnit*> units;
    auto keep = [&units](const CompilationUnit* unit) {
        if (unit == nullptr || unit->ast == nullptr || unit->isPrelude) return;
        for (const CompilationUnit* seen : units) {
            if (seen == unit) return;
        }
        units.push_back(unit);
    };
    if (document.entry != nullptr) keep(document.entry);
    if (document.graph != nullptr) {
        for (const CompilationUnit* unit : document.graph->units()) keep(unit);
    }
    return units;
}

// The import declaration that binds `name` in this unit's file, or nullptr.
const ImportDeclaration* importForName(const Program& program,
                                       const std::string& name,
                                       const SourceFile& file) {
    return importForNameImpl(program, name, file);
}

// The dependency edge of `specifier` imported by `importer`, or nullptr.
const ModuleDependency* dependencyFor(const ModuleGraph& graph,
                                      std::uint32_t importer,
                                      const std::string& specifier) {
    return dependencyForImpl(graph, importer, specifier);
}

std::vector<lsp::SourceTarget> definitionAt(const std::string& path,
                                            const std::string& text,
                                            std::uint32_t line,
                                            std::uint32_t column) {
    std::vector<lsp::SourceTarget> targets;
    auto document = analyzeDocument(path, text);
    if (!analysisReady(*document)) return targets;
    const Node* node =
        innermostNodeAt(*document->entry->ast, *document->file, line, column);
    // A cursor sitting on the boundary just past a name (`count|;`) is inside
    // no span: retry from the byte before the cursor, which belongs to the
    // name the cursor follows.
    if (node == nullptr && column > 1) {
        node = innermostNodeAt(*document->entry->ast, *document->file, line,
                               column - 1);
    }
    if (node == nullptr) return targets;
    // A member access (`receiver.member`): jump to the member's declaration
    // on the owner (a class/interface from this file or an imported
    // module) when it can be proven.
    if (node->kind == NodeKind::MemberExpr) {
        return memberDefinitionTargets(*document, path, line, column);
    }
    // A name in a type position (`c: Sized`, `const r: Radians = 1`) is a
    // NamedType, not an Identifier: the import it binds is the same one.
    std::string name;
    if (node->kind == NodeKind::Identifier) {
        name = static_cast<const Identifier&>(*node).name;
    } else if (node->kind == NodeKind::NamedType) {
        name = static_cast<const NamedType&>(*node).name;
    } else {
        const std::vector<lsp::SourceTarget> memberTargets =
            memberDefinitionTargets(*document, path, line, column);
        if (!memberTargets.empty()) return memberTargets;
        return selfTarget(*node);
    }
    const std::uint32_t offset = offsetOf(*document->file, line, column);
    const SymbolInfo* symbol =
        nearestSymbolAt(document->sema, name, *document->file, offset);
    // The compiler's own binding answer is authoritative: a function has no
    // symbol entry (its binding lives in the function table), but every use
    // resolves to the declaration.
    std::uint32_t declNodeId = 0;
    if (node->kind == NodeKind::Identifier) {
        const auto it = document->sema.resolvedBindings.find(node->nodeId);
        if (it != document->sema.resolvedBindings.end()) declNodeId = it->second;
    }
    if (symbol != nullptr) declNodeId = symbol->declNodeId;
    const ImportDeclaration* import =
        importForNameImpl(*document->entry->ast, name, *document->file);
    if (import != nullptr &&
        (symbol == nullptr || isImportBinding(*symbol, *import))) {
        std::vector<lsp::SourceTarget> imported =
            importedTargets(*document, *import, name);
        if (imported.empty() && document->graph != nullptr &&
            document->entry != nullptr &&
            import->namespaceBinding != name && import->alias != name) {
            // The binding exists but the target's default export could not be
            // proven (`import x from "./m"` where m has no default export):
            // point at the binding's own site in this file instead of
            // nowhere. Local edges only; std/package/native stay silent and
            // namespace bindings answer nothing.
            const ModuleDependency* dependency = dependencyForImpl(
                *document->graph, document->entry->id, import->source);
            const bool localEdge = dependency != nullptr && dependency->resolved &&
                                   dependency->resolution == ModuleResolution::Local;
            if (localEdge) {
                const std::optional<lsp::SourceTarget> found =
                    targetOf(nameSpanIn(import->span, name));
                if (found.has_value()) imported.push_back(*found);
            }
        }
        return imported;
    }
    if (symbol == nullptr && declNodeId == 0 && import == nullptr) {
        // The binding tables cannot see a member position (a member's own
        // key, a bare name inside a class body). The member fallback answers
        // where it can; the binding pipeline keeps winning everywhere else.
        const std::vector<lsp::SourceTarget> memberTargets =
            memberDefinitionTargets(*document, path, line, column);
        if (!memberTargets.empty()) return memberTargets;
    }
    if (symbol != nullptr) {
        const std::optional<lsp::SourceTarget> found =
            targetOf(nameSpanIn(symbol->span, name));
        if (found.has_value()) targets.push_back(*found);
        return targets;
    }
    if (declNodeId != 0) {
        // The declaration the cursor's name resolved to.
        const Node* declaration = findNodeById(*document->entry->ast, declNodeId);
        if (declaration != nullptr) {
            const std::string declared = declaredNameOf(*declaration);
            if (!declared.empty()) {
                const std::optional<lsp::SourceTarget> found =
                    targetOf(nameSpanIn(declaration->span, declared));
                if (found.has_value()) targets.push_back(*found);
            }
        }
    }
    return targets;
}

}  // namespace cp3::cli
