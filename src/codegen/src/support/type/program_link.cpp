// =============================================================================
// src/codegen/src/support/program_link.cpp - the whole-program link builder.
//
// OWNERSHIP: codegen module. Implements detail/program_link.h.
//
// Walks the checked units once and fills a `ProgramLink`: every unit's export
// surface, resolved to the qualified C++ spellings the unit's namespace carries
// (recursing through `export * from` / `export { x } from` chains; a cycle
// yields the partial surface built so far, matching ES-module reality), and
// every local import of every unit, resolved against the surface of the unit
// it points to. `std`/package/native/cpp specifiers are not linked: they keep
// their existing lowering (a lowered intrinsic, or a use-site error when no
// lowering exists).
//
// A form this milestone cannot spell in C++ (a namespace import, a namespace
// re-export, an anonymous default export, a type-only export, a binding
// without a namespace-scope C++ name) is recorded with an empty `cpp` plus a
// `miss` reason, so the name stays quiet until it is actually used - then the
// use site reports an unresolved-import error.
// =============================================================================
#include "detail/generator/program_link.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "cp3/core/ast.hpp"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"

namespace cp3::detail {

namespace {

// A `unit + span` key naming one import / re-export edge (ModuleDependency
// carries no id of its own, and a prelude-injected import keeps the prelude's
// file, so the source file is part of the key).
struct DepKey {
    std::uint32_t importer = 0;
    const SourceFile* file = nullptr;
    std::uint32_t offset = 0;

    bool operator<(const DepKey& other) const {
        if (importer != other.importer) return importer < other.importer;
        if (file != other.file) return file < other.file;
        return offset < other.offset;
    }
};

// An importable name with no C++ spelling; `reason` is reported at the use.
LinkedName missName(std::string reason) {
    LinkedName out;
    out.miss = std::move(reason);
    return out;
}

// The exported name of a type-only declaration (nullptr when `decl` is not a
// type declaration).
const std::string* typeDeclarationName(const Stmt& decl) {
    switch (decl.kind) {
        case NodeKind::ClassDeclaration:
            return &static_cast<const ClassDeclaration&>(decl).name;
        case NodeKind::InterfaceDeclaration:
            return &static_cast<const InterfaceDeclaration&>(decl).name;
        case NodeKind::EnumDeclaration:
            return &static_cast<const EnumDeclaration&>(decl).name;
        case NodeKind::TypeAliasDeclaration:
            return &static_cast<const TypeAliasDeclaration&>(decl).name;
        default:
            return nullptr;
    }
}

// The declarator of `vd` that binds `name`, or nullptr.
const VariableDeclarator* declaratorNamed(const VariableDeclaration& vd,
                                          const std::string& name) {
    for (const VariableDeclaratorPtr& d : vd.declarations) {
        if (d && d->id && d->id->kind == NodeKind::Identifier &&
            static_cast<const Identifier&>(*d->id).name == name) {
            return d.get();
        }
    }
    return nullptr;
}

// The cp3 type of a literal initializer (the conservative subset a linked
// module binding needs); anything richer leaves the use unresolved and reports
// an error at the use site.
Type literalInitType(const Expr& e) {
    switch (e.kind) {
        case NodeKind::NumberLiteral: return Type::number();
        case NodeKind::StringLiteral: return Type::string();
        case NodeKind::BoolLiteral: return Type::boolean();
        default: return Type::unknown();
    }
}

}  // namespace

// Builds the program link table (program_link.h). One instance per build; the
// surface and import maps fill in place, so a recursive lookup during a cycle
// sees the partial table instead of recursing forever.
class LinkBuilder {
public:
    LinkBuilder(const ModuleGraph& graph,
                const std::vector<CheckedModuleUnit>& units);

    ProgramLink build();

private:
    // --- unit lookup -----------------------------------------------------------
    const SemaResult* semaOf(std::uint32_t id) const;
    const ModuleDependency* dependencyOf(std::uint32_t importer,
                                         const SourceSpan& span) const;
    CompilationUnit* localTarget(const CompilationUnit& unit,
                                 const SourceSpan& span) const;
    std::string qualified(std::uint32_t id, const std::string& name) const;

    // --- export surfaces ---------------------------------------------------------
    UnitSurface& surfaceOf(std::uint32_t id);
    void computeSurface(CompilationUnit& unit, UnitSurface& surface);
    void applyExport(const ExportDeclaration& ed, CompilationUnit& unit,
                     UnitSurface& surface);
    void applyDeclared(const Stmt& decl, CompilationUnit& unit,
                       UnitSurface& surface);
    void applySpecifiers(const std::vector<ExportSpecifierPtr>& specs,
                         CompilationUnit& unit, UnitSurface& surface);
    void applyReExported(const ExportDeclaration& ed, CompilationUnit& unit,
                         UnitSurface& surface);
    void applyReExportAll(const ExportDeclaration& ed, CompilationUnit& unit,
                          UnitSurface& surface);
    LinkedName reExported(CompilationUnit* target, const std::string& name);

    // --- binding resolution --------------------------------------------------------
    LinkedName bindingOf(CompilationUnit& unit, const std::string& name);
    std::optional<LinkedName> declaredBinding(CompilationUnit& unit,
                                              const std::string& name);
    std::optional<LinkedName> statementBinding(CompilationUnit& unit,
                                               const Stmt& s,
                                               const std::string& name);
    std::optional<LinkedName> importedBinding(CompilationUnit& unit,
                                              const std::string& name);
    LinkedName defaultBinding(CompilationUnit& unit, const ExportDeclaration& ed);
    LinkedName functionBinding(const CompilationUnit& unit,
                               const FunctionDeclaration& fd);
    LinkedName variableBinding(const CompilationUnit& unit,
                               const VariableDeclarator& d);

    // --- imports ---------------------------------------------------------------------
    std::unordered_map<std::string, LinkedName>& importsOf(std::uint32_t id);
    void fillImports(CompilationUnit& unit,
                     std::unordered_map<std::string, LinkedName>& out);

    const ModuleGraph& graph_;
    ProgramLink link_;
    std::vector<std::uint32_t> order_;
    std::map<DepKey, const ModuleDependency*> deps_;
};

LinkBuilder::LinkBuilder(const ModuleGraph& graph,
                         const std::vector<CheckedModuleUnit>& units)
    : graph_(graph) {
    for (const CheckedModuleUnit& u : units) {
        if (u.unit == nullptr || u.sema == nullptr) continue;
        order_.push_back(u.unit->id);
        link_.semas[u.unit->id] = u.sema;
    }
    for (const ModuleDependency& dep : graph.dependencies()) {
        deps_[DepKey{dep.importer, dep.span.file, dep.span.begin.offset}] = &dep;
    }
}

ProgramLink LinkBuilder::build() {
    for (const std::uint32_t id : order_) {
        surfaceOf(id);
    }
    for (const std::uint32_t id : order_) {
        importsOf(id);
    }
    return std::move(link_);
}

const SemaResult* LinkBuilder::semaOf(std::uint32_t id) const {
    const auto it = link_.semas.find(id);
    return it == link_.semas.end() ? nullptr : it->second;
}

const ModuleDependency* LinkBuilder::dependencyOf(std::uint32_t importer,
                                                  const SourceSpan& span) const {
    const auto it = deps_.find(DepKey{importer, span.file, span.begin.offset});
    return it == deps_.end() ? nullptr : it->second;
}

CompilationUnit* LinkBuilder::localTarget(const CompilationUnit& unit,
                                          const SourceSpan& span) const {
    const ModuleDependency* dep = dependencyOf(unit.id, span);
    if (dep == nullptr || dep->resolution != ModuleResolution::Local ||
        dep->imported == UINT32_MAX) {
        return nullptr;
    }
    return graph_.unit(dep->imported);
}

std::string LinkBuilder::qualified(std::uint32_t id,
                                   const std::string& name) const {
    const CompilationUnit* unit = graph_.unit(id);
    const std::string ns = unit != nullptr ? suggestNamespace(unit->moduleName)
                                           : std::string("cp3_mod_module");
    return ns + "::" + mapIdent(name);
}

UnitSurface& LinkBuilder::surfaceOf(std::uint32_t id) {
    const auto it = link_.surfaces.find(id);
    if (it != link_.surfaces.end()) return it->second;  // complete or in progress
    UnitSurface& surface = link_.surfaces[id];
    CompilationUnit* unit = graph_.unit(id);
    if (unit == nullptr) return surface;
    surface.namesp = suggestNamespace(unit->moduleName);
    if (unit->ast) computeSurface(*unit, surface);
    return surface;
}

void LinkBuilder::computeSurface(CompilationUnit& unit, UnitSurface& surface) {
    for (const StmtPtr& stmt : unit.ast->body) {
        if (!stmt || stmt->kind != NodeKind::ExportDeclaration) continue;
        applyExport(static_cast<const ExportDeclaration&>(*stmt), unit, surface);
    }
}

void LinkBuilder::applyExport(const ExportDeclaration& ed,
                              CompilationUnit& unit, UnitSurface& surface) {
    switch (ed.kind) {
        case ExportKind::Default:
            surface.hasDefault = true;
            surface.defaultExport = defaultBinding(unit, ed);
            return;
        case ExportKind::Declaration:
            if (ed.declaration) applyDeclared(*ed.declaration, unit, surface);
            return;
        case ExportKind::Named:
            applySpecifiers(ed.specifiers, unit, surface);
            return;
        case ExportKind::ReExportNamed:
            applyReExported(ed, unit, surface);
            return;
        case ExportKind::ReExportAll:
            applyReExportAll(ed, unit, surface);
            return;
    }
}

void LinkBuilder::applyDeclared(const Stmt& decl, CompilationUnit& unit,
                                UnitSurface& surface) {
    if (decl.kind == NodeKind::FunctionDeclaration) {
        const auto& fd = static_cast<const FunctionDeclaration&>(decl);
        if (!fd.name.empty()) surface.named[fd.name] = functionBinding(unit, fd);
        return;
    }
    if (decl.kind == NodeKind::VariableDeclaration) {
        const auto& vd = static_cast<const VariableDeclaration&>(decl);
        for (const VariableDeclaratorPtr& d : vd.declarations) {
            if (!d || !d->id || d->id->kind != NodeKind::Identifier) continue;
            const auto& id = static_cast<const Identifier&>(*d->id);
            surface.named[id.name] = variableBinding(unit, *d);
        }
        return;
    }
    if (const std::string* name = typeDeclarationName(decl); name != nullptr) {
        if (!name->empty()) {
            surface.named[*name] = missName("the imported name is a type, not a value");
        }
    }
}

void LinkBuilder::applySpecifiers(
    const std::vector<ExportSpecifierPtr>& specs, CompilationUnit& unit,
    UnitSurface& surface) {
    for (const ExportSpecifierPtr& spec : specs) {
        if (!spec) continue;
        const std::string exported =
            spec->exported.empty() ? spec->local : spec->exported;
        surface.named[exported] = bindingOf(unit, spec->local);
    }
}

void LinkBuilder::applyReExported(const ExportDeclaration& ed,
                                  CompilationUnit& unit, UnitSurface& surface) {
    CompilationUnit* target = localTarget(unit, ed.span);
    for (const ExportSpecifierPtr& spec : ed.specifiers) {
        if (!spec) continue;
        const std::string exported =
            spec->exported.empty() ? spec->local : spec->exported;
        surface.named[exported] = reExported(target, spec->local);
    }
}

void LinkBuilder::applyReExportAll(const ExportDeclaration& ed,
                                   CompilationUnit& unit, UnitSurface& surface) {
    if (!ed.namespaceExport.empty()) {
        surface.named[ed.namespaceExport] = missName("a namespace re-export is not supported");
    }
    CompilationUnit* target = localTarget(unit, ed.span);
    if (target == nullptr || target->id == unit.id || target->ast == nullptr) {
        return;
    }
    const UnitSurface& from = surfaceOf(target->id);
    for (const auto& entry : from.named) {
        surface.named.emplace(entry.first, entry.second);  // explicit exports win
    }
}

LinkedName LinkBuilder::reExported(CompilationUnit* target,
                                   const std::string& name) {
    if (target == nullptr || target->ast == nullptr) {
        return missName("the re-exported name is not exported");
    }
    const UnitSurface& from = surfaceOf(target->id);
    const auto found = from.named.find(name);
    return found != from.named.end() ? found->second
                                     : missName("the re-exported name is not exported");
}

LinkedName LinkBuilder::defaultBinding(CompilationUnit& unit,
                                       const ExportDeclaration& ed) {
    if (ed.declaration != nullptr) {
        const Stmt& decl = *ed.declaration;
        if (decl.kind == NodeKind::FunctionDeclaration) {
            const auto& fd = static_cast<const FunctionDeclaration&>(decl);
            if (!fd.name.empty()) return functionBinding(unit, fd);
        } else if (decl.kind == NodeKind::VariableDeclaration) {
            const auto& vd = static_cast<const VariableDeclaration&>(decl);
            if (vd.declarations.size() == 1 && vd.declarations.front() != nullptr &&
                vd.declarations.front()->id != nullptr &&
                vd.declarations.front()->id->kind == NodeKind::Identifier) {
                return variableBinding(unit, *vd.declarations.front());
            }
        }
        return missName("the default export has no value cp3 can use");
    }
    if (ed.defaultExpr != nullptr && ed.defaultExpr->kind == NodeKind::Identifier) {
        const std::string& name =
            static_cast<const Identifier&>(*ed.defaultExpr).name;
        if (auto declared = declaredBinding(unit, name)) return *declared;
        if (auto imported = importedBinding(unit, name)) return *imported;
    }
    return missName("the default export has no value cp3 can use");
}

LinkedName LinkBuilder::bindingOf(CompilationUnit& unit,
                                  const std::string& name) {
    if (auto declared = declaredBinding(unit, name)) return *declared;
    if (auto imported = importedBinding(unit, name)) return *imported;
    return missName("the module does not export it");
}

std::optional<LinkedName> LinkBuilder::declaredBinding(CompilationUnit& unit,
                                                       const std::string& name) {
    if (!unit.ast) return std::nullopt;
    for (const StmtPtr& stmt : unit.ast->body) {
        if (!stmt) continue;
        if (auto found = statementBinding(unit, *stmt, name)) return found;
    }
    return std::nullopt;
}

std::optional<LinkedName> LinkBuilder::statementBinding(CompilationUnit& unit,
                                                        const Stmt& s,
                                                        const std::string& name) {
    switch (s.kind) {
        case NodeKind::FunctionDeclaration: {
            const auto& fd = static_cast<const FunctionDeclaration&>(s);
            if (fd.name == name) return functionBinding(unit, fd);
            return std::nullopt;
        }
        case NodeKind::VariableDeclaration: {
            const auto& vd = static_cast<const VariableDeclaration&>(s);
            if (const VariableDeclarator* d = declaratorNamed(vd, name)) {
                return variableBinding(unit, *d);
            }
            return std::nullopt;
        }
        case NodeKind::ExportDeclaration: {
            const auto& ed = static_cast<const ExportDeclaration&>(s);
            if (ed.declaration) return statementBinding(unit, *ed.declaration, name);
            return std::nullopt;
        }
        default:
            return std::nullopt;
    }
}

std::optional<LinkedName> LinkBuilder::importedBinding(CompilationUnit& unit,
                                                       const std::string& name) {
    auto& map = importsOf(unit.id);
    const auto it = map.find(name);
    if (it == map.end()) return std::nullopt;
    return it->second;
}

LinkedName LinkBuilder::functionBinding(const CompilationUnit& unit,
                                        const FunctionDeclaration& fd) {
    LinkedName out;
    out.cpp = qualified(unit.id, fd.name);
    out.fn = &fd;
    out.isAsync = fd.isAsync;
    if (const SemaResult* sema = semaOf(unit.id)) {
        const auto it = sema->functions.find(fd.nodeId);
        if (it != sema->functions.end()) out.type = it->second.returnType;
    }
    return out;
}

LinkedName LinkBuilder::variableBinding(const CompilationUnit& unit,
                                        const VariableDeclarator& d) {
    if (d.id == nullptr || d.id->kind != NodeKind::Identifier) {
        return missName("the exported binding is a destructuring pattern");
    }
    const auto& id = static_cast<const Identifier&>(*d.id);
    Type type = Type::unknown();
    if (const SemaResult* sema = semaOf(unit.id)) {
        if (const Type* t = sema->typeOf(d.nodeId)) type = *t;
        else if (const Type* t2 = sema->typeOf(id.nodeId)) type = *t2;
    }
    if (type.isUnknown() && d.init != nullptr) type = literalInitType(*d.init);
    if (d.type != nullptr) {
        if (auto annotated = typeFromTypeNode(*d.type)) type = *annotated;
    }
    // The unit's own emitter keeps a binding it cannot type a module-init
    // local (emit/decl.cpp emitModuleVar), so there is no namespace-scope C++
    // name for an importer to reach: a use reports an unresolved-import error
    // instead.
    if (!classify(type).representable) return missName("the exported binding has no C++ spelling");
    LinkedName out;
    out.cpp = qualified(unit.id, id.name);
    out.type = type;
    return out;
}

std::unordered_map<std::string, LinkedName>& LinkBuilder::importsOf(
    std::uint32_t id) {
    const auto it = link_.imports.find(id);
    if (it != link_.imports.end()) return it->second;  // complete or in progress
    auto& map = link_.imports[id];
    if (CompilationUnit* unit = graph_.unit(id)) fillImports(*unit, map);
    return map;
}

void LinkBuilder::fillImports(CompilationUnit& unit,
                              std::unordered_map<std::string, LinkedName>& out) {
    if (!unit.ast) return;
    for (const StmtPtr& stmt : unit.ast->body) {
        if (!stmt || stmt->kind != NodeKind::ImportDeclaration) continue;
        const auto& imp = static_cast<const ImportDeclaration&>(*stmt);
        CompilationUnit* target = localTarget(unit, imp.span);
        if (target == nullptr) continue;  // std/package/native/cpp: unchanged
        const UnitSurface& surface = surfaceOf(target->id);
        if (!imp.defaultBinding.empty()) {
            out[imp.defaultBinding] = surface.hasDefault
                                          ? surface.defaultExport
                                          : missName("the module has no default export");
        }
        for (const ImportSpecifierPtr& spec : imp.specifiers) {
            if (!spec) continue;
            const std::string local =
                spec->local.empty() ? spec->imported : spec->local;
            const auto found = surface.named.find(spec->imported);
            out[local] = found != surface.named.end() ? found->second
                                                      : missName("the module does not export it");
        }
        if (!imp.namespaceBinding.empty()) {
            out[imp.namespaceBinding] = missName("the namespace import form is not supported");
        }
    }
}

// -----------------------------------------------------------------------------
// Entry
// -----------------------------------------------------------------------------

ProgramLink buildProgramLink(const ModuleGraph& graph,
                             const std::vector<CheckedModuleUnit>& units) {
    LinkBuilder builder(graph, units);
    return builder.build();
}

}  // namespace cp3::detail
