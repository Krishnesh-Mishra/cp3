// =============================================================================
// src/cli/src/toolchain/pipeline.cpp - wiring the frozen frontend stages together.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/pipeline.hpp.
//
// Stages communicate only through the frozen boundaries:
//   SourceManager -> ModuleLoader -> SemanticAnalyzer -> CodeGenerator
// and every stage reports through a DiagnosticSink; the CLI renders. The
// module loader owns ��18 prelude injection and import resolution; sema's
// nodeId-keyed side tables (SemaResult) are threaded into the three-argument
// generator (DECISIONS D1/D14) - this file is the single change site.
// =============================================================================
#include "detail/toolchain/pipeline.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cp3/core/ast.hpp"
#include "cp3/modules.hpp"
#include "cp3/sema.hpp"

#include "detail/toolchain/interface_hash.hpp"

#include "detail/pkg/json.hpp"
#include "detail/pkg/lockfile.hpp"
#include "detail/pkg/package_json.hpp"

namespace cp3::cli {

namespace {

// One unit's exported surface, in the shape sema's import table consumes.
struct ExportSurface {
    ImportedBindings named;
    bool hasDefault = false;
    ImportedBinding defaultExport;
};

using ExportSurfaceMap = std::unordered_map<std::uint32_t, ExportSurface>;

struct DependencyKey {
    std::uint32_t importer = 0;
    const SourceFile* file = nullptr;
    std::uint32_t offset = 0;

    bool operator<(const DependencyKey& other) const {
        if (importer != other.importer) return importer < other.importer;
        if (file != other.file) return file < other.file;
        return offset < other.offset;
    }
};

std::map<DependencyKey, const ModuleDependency*> dependencyIndex(
    const ModuleGraph& graph) {
    std::map<DependencyKey, const ModuleDependency*> out;
    for (const ModuleDependency& dep : graph.dependencies()) {
        out[DependencyKey{dep.importer, dep.span.file, dep.span.begin.offset}] =
            &dep;
    }
    return out;
}

const ModuleDependency* dependencyOf(
    const ModuleGraph& graph, std::uint32_t importer, const SourceSpan& span,
    const std::map<DependencyKey, const ModuleDependency*>& index) {
    const auto it =
        index.find(DependencyKey{importer, span.file, span.begin.offset});
    (void)graph;
    return it == index.end() ? nullptr : it->second;
}

const FunctionInfo* functionInfoOf(const SemaResult& sema,
                                   std::uint32_t declNodeId) {
    const auto it = sema.functions.find(declNodeId);
    return it == sema.functions.end() ? nullptr : &it->second;
}

ImportedBinding functionBinding(const SemaResult& sema,
                                const FunctionDeclaration& fn) {
    ImportedBinding out;
    out.isValue = true;
    out.isFunction = true;
    if (const FunctionInfo* fi = functionInfoOf(sema, fn.nodeId)) {
        out.type = Type::function(fi->paramTypes, fi->returnType);
        out.typeParams = fi->typeParams;
        out.paramTypes = fi->paramTypes;
        out.paramOptional = fi->paramOptional;
        out.hasRest = fi->hasRest;
        out.returnType = fi->returnType;
    } else {
        out.type = Type::function({}, Type::unknown());
        out.returnType = Type::unknown();
    }
    out.isAsync = fn.isAsync;
    return out;
}

ImportedBinding valueBinding(const Type& type, bool isType = false,
                             bool isValue = true) {
    ImportedBinding out;
    out.type = type;
    out.isType = isType;
    out.isValue = isValue;
    return out;
}

// The symbol a top-level declaration binds under `name`, if sema published one.
const SymbolInfo* symbolNamed(const SemaResult& sema, const std::string& name) {
    for (const auto& entry : sema.symbols) {
        if (entry.second.name == name) return &entry.second;
    }
    return nullptr;
}

// A type-only declaration (interface / type alias) is importable in a type
// position but has no value at runtime.
bool isTypeOnlyDeclaration(const Stmt& decl) {
    return decl.kind == NodeKind::InterfaceDeclaration ||
           decl.kind == NodeKind::TypeAliasDeclaration;
}

std::string declarationName(const Stmt& decl) {
    switch (decl.kind) {
        case NodeKind::FunctionDeclaration:
            return static_cast<const FunctionDeclaration&>(decl).name;
        case NodeKind::ClassDeclaration:
            return static_cast<const ClassDeclaration&>(decl).name;
        case NodeKind::InterfaceDeclaration:
            return static_cast<const InterfaceDeclaration&>(decl).name;
        case NodeKind::EnumDeclaration:
            return static_cast<const EnumDeclaration&>(decl).name;
        case NodeKind::TypeAliasDeclaration:
            return static_cast<const TypeAliasDeclaration&>(decl).name;
        default:
            return {};
    }
}

// Adds one declaration's exported bindings. Variables contribute one binding
// per declarator name; a destructuring pattern contributes each identifier it
// introduces (the sema symbol table already has one entry per name).
void addDeclarationExports(const Stmt& decl, const SemaResult& sema,
                           ExportSurface& out) {
    switch (decl.kind) {
        case NodeKind::FunctionDeclaration: {
            const auto& fn = static_cast<const FunctionDeclaration&>(decl);
            if (!fn.name.empty()) out.named[fn.name] = functionBinding(sema, fn);
            return;
        }
        case NodeKind::ClassDeclaration: {
            const auto& c = static_cast<const ClassDeclaration&>(decl);
            if (c.name.empty()) return;
            ImportedBinding b;
            b.type = Type::named(TypeKind::Class, c.name);
            b.isValue = true;
            b.isType = true;
            b.classDecl = &c;
            if (const SymbolInfo* si = symbolNamed(sema, c.name)) b.type = si->type;
            out.named[c.name] = std::move(b);
            return;
        }
        case NodeKind::EnumDeclaration: {
            const auto& e = static_cast<const EnumDeclaration&>(decl);
            if (e.name.empty()) return;
            ImportedBinding b;
            b.type = Type::named(TypeKind::Enum, e.name);
            b.isValue = true;
            b.isType = true;
            if (const SymbolInfo* si = symbolNamed(sema, e.name)) b.type = si->type;
            out.named[e.name] = std::move(b);
            return;
        }
        case NodeKind::InterfaceDeclaration:
        case NodeKind::TypeAliasDeclaration: {
            const std::string name = declarationName(decl);
            if (name.empty()) return;
            ImportedBinding b;
            b.type = Type::unknown();
            b.isValue = false;
            b.isType = true;
            if (decl.kind == NodeKind::InterfaceDeclaration) {
                b.interfaceDecl = static_cast<const InterfaceDeclaration*>(&decl);
            } else {
                b.aliasDecl = static_cast<const TypeAliasDeclaration*>(&decl);
            }
            out.named[name] = std::move(b);
            return;
        }
        case NodeKind::VariableDeclaration: {
            const auto& vd = static_cast<const VariableDeclaration&>(decl);
            for (const VariableDeclaratorPtr& d : vd.declarations) {
                if (!d) continue;
                const SymbolInfo* si = nullptr;
                if (const auto it = sema.symbols.find(d->nodeId);
                    it != sema.symbols.end()) {
                    si = &it->second;
                }
                if (si == nullptr && d->id &&
                    d->id->kind == NodeKind::Identifier) {
                    si = symbolNamed(
                        sema, static_cast<const Identifier&>(*d->id).name);
                }
                if (si == nullptr) continue;
                out.named[si->name] = valueBinding(si->type);
            }
            return;
        }
        default:
            return;
    }
}

bool constantTypeDeclaration(const Stmt& stmt, const std::string& name);

// The declaration that binds `name` in a unit, if it has one. Class/interface/
// alias declarations do not publish a top-level symbol the way functions and
// variables do, so the codegen and the trailing `export { }` form need this
// to funnel the declaration's AST node into the binding.
const Stmt* topLevelDeclaration(const CompilationUnit& unit,
                                const std::string& name) {
    if (!unit.ast) return nullptr;
    for (const StmtPtr& stmt : unit.ast->body) {
        if (!stmt) continue;
        const std::string declName = declarationName(*stmt);
        if (!declName.empty() && declName == name) return stmt.get();
    }
    return nullptr;
}

// The binding of a local name, for `export { local as exported }`.
std::optional<ImportedBinding> localBinding(const CompilationUnit& unit,
                                            const SemaResult& sema,
                                            const std::string& name) {
    if (const SymbolInfo* si = symbolNamed(sema, name)) {
        ImportedBinding b;
        b.type = si->type;
        b.isValue = true;
        b.isFunction = si->isFunction;
        if (si->isFunction) {
            if (const FunctionInfo* fi = functionInfoOf(sema, si->declNodeId)) {
                b.type = Type::function(fi->paramTypes, fi->returnType);
                b.typeParams = fi->typeParams;
                b.paramTypes = fi->paramTypes;
                b.paramOptional = fi->paramOptional;
                b.hasRest = fi->hasRest;
                b.returnType = fi->returnType;
            }
        }
        if (si->type.kind == TypeKind::Class) {
            b.isType = true;
            // The trailing `export { Frame }` form carries no declaration
            // pointer the way inline exports do; find the class node so
            // dependent code can type-check/reject members of the exported
            // class the same way.
            if (const Stmt* decl = topLevelDeclaration(unit, name);
                decl != nullptr && decl->kind == NodeKind::ClassDeclaration) {
                b.classDecl = static_cast<const ClassDeclaration*>(decl);
            }
        }
        if (si->type.kind == TypeKind::Enum) b.isType = true;
        return b;
    }
    if (!unit.ast) return std::nullopt;
    for (const StmtPtr& stmt : unit.ast->body) {
        if (!stmt) continue;
        if (constantTypeDeclaration(*stmt, name)) {
            ImportedBinding b;
            b.type = Type::unknown();
            b.isType = true;
            b.isValue = false;
            if (stmt->kind == NodeKind::InterfaceDeclaration) {
                b.interfaceDecl =
                    static_cast<const InterfaceDeclaration*>(stmt.get());
            } else if (stmt->kind == NodeKind::TypeAliasDeclaration) {
                b.aliasDecl =
                    static_cast<const TypeAliasDeclaration*>(stmt.get());
            }
            return b;
        }
    }
    return std::nullopt;
}

bool constantTypeDeclaration(const Stmt& stmt, const std::string& name) {
    return isTypeOnlyDeclaration(stmt) && declarationName(stmt) == name;
}

// =============================================================================
// Same-module linking
// =============================================================================
// An imported declaration's annotations can name other top-level declarations
// of the module that defines it (`type LaterAlias = Later`, `class Client {
// connect(o: Opts) }`). Those names are not this unit's imports, but the
// defining module resolved them when it was analyzed, and the consumer TU
// re-resolves the imported declaration bodies against its own type table - so
// the siblings must be re-registered there too. Linking below computes the
// transitive closure from the already-imported declarations to the defining
// module's own exported declarations, so the re-registration can find them.
// Linked entries only ever feed the type table and the class/interface/alias
// registries; they never declare a binding in this unit's value scope.

void collectFreeTypeNames(const TypeNode* t,
                          const std::unordered_set<std::string>& bound,
                          std::unordered_set<std::string>& out);
void collectFreeTypeParamNames(const TypeParamList& tps,
                               const std::unordered_set<std::string>& bound,
                               std::unordered_set<std::string>& out);

void collectFreeSignatureNames(const SignatureMember& m,
                               const std::unordered_set<std::string>& bound,
                               std::unordered_set<std::string>& out) {
    switch (m.kind) {
        case NodeKind::PropertySignature:
            collectFreeTypeNames(static_cast<const PropertySignature&>(m).type.get(),
                                 bound, out);
            return;
        case NodeKind::MethodSignature: {
            const auto& ms = static_cast<const MethodSignature&>(m);
            auto fnBound = bound;
            for (const auto& tp : ms.typeParams)
                if (tp) fnBound.insert(tp->name);
            collectFreeTypeParamNames(ms.typeParams, fnBound, out);
            for (const ParameterPtr& p : ms.params)
                if (p && p->type) collectFreeTypeNames(p->type.get(), fnBound, out);
            collectFreeTypeNames(ms.returnType.get(), fnBound, out);
            return;
        }
        case NodeKind::IndexSignature: {
            const auto& is = static_cast<const IndexSignature&>(m);
            collectFreeTypeNames(is.keyType.get(), bound, out);
            collectFreeTypeNames(is.valueType.get(), bound, out);
            return;
        }
        default:
            return;
    }
}

void collectFreeTypeParamNames(const TypeParamList& tps,
                               const std::unordered_set<std::string>& bound,
                               std::unordered_set<std::string>& out) {
    for (const TypeParameterPtr& tp : tps) {
        if (!tp) continue;
        collectFreeTypeNames(tp->constraint.get(), bound, out);
        collectFreeTypeNames(tp->defaultType.get(), bound, out);
    }
}

void collectFreeTypeNames(const TypeNode* t,
                          const std::unordered_set<std::string>& bound,
                          std::unordered_set<std::string>& out) {
    if (t == nullptr) return;
    switch (t->kind) {
        case NodeKind::NamedType: {
            const auto& nt = static_cast<const NamedType&>(*t);
            if (!nt.name.empty() && bound.find(nt.name) == bound.end()) out.insert(nt.name);
            for (const TypePtr& a : nt.typeArgs) collectFreeTypeNames(a.get(), bound, out);
            return;
        }
        case NodeKind::UnionType:
            for (const TypePtr& m : static_cast<const UnionType&>(*t).types)
                collectFreeTypeNames(m.get(), bound, out);
            return;
        case NodeKind::ArrayType:
            collectFreeTypeNames(static_cast<const ArrayType&>(*t).element.get(), bound, out);
            return;
        case NodeKind::TupleType:
            for (const TypePtr& e : static_cast<const TupleType&>(*t).elements)
                collectFreeTypeNames(e.get(), bound, out);
            return;
        case NodeKind::FunctionType: {
            const auto& ft = static_cast<const FunctionType&>(*t);
            auto fnBound = bound;
            for (const auto& tp : ft.typeParams)
                if (tp) fnBound.insert(tp->name);
            collectFreeTypeParamNames(ft.typeParams, fnBound, out);
            for (const ParameterPtr& p : ft.params)
                if (p && p->type) collectFreeTypeNames(p->type.get(), fnBound, out);
            collectFreeTypeNames(ft.returnType.get(), fnBound, out);
            return;
        }
        case NodeKind::ObjectType:
            for (const SignatureMemberPtr& m : static_cast<const ObjectType&>(*t).members)
                if (m) collectFreeSignatureNames(*m, bound, out);
            return;
        case NodeKind::ParenthesizedType:
            collectFreeTypeNames(static_cast<const ParenthesizedType&>(*t).inner.get(),
                                 bound, out);
            return;
        default:
            // LiteralType and PrefixedType carry no names.
            return;
    }
}

std::unordered_set<std::string> ownTypeParamNames(const TypeParamList& tps) {
    std::unordered_set<std::string> out;
    for (const TypeParameterPtr& tp : tps)
        if (tp) out.insert(tp->name);
    return out;
}

// The names of other top-level declarations one declaration's annotations
// reference (a best-effort syntactic scan of the annotation grammar; names
// bound by the declaration's own type parameters are excluded).
std::unordered_set<std::string> freeTypeNamesOf(const Stmt& decl) {
    std::unordered_set<std::string> out;
    switch (decl.kind) {
        case NodeKind::ClassDeclaration: {
            const auto& c = static_cast<const ClassDeclaration&>(decl);
            const auto bound = ownTypeParamNames(c.typeParams);
            collectFreeTypeParamNames(c.typeParams, bound, out);
            collectFreeTypeNames(c.extendsType.get(), bound, out);
            for (const TypePtr& t : c.implementsTypes) collectFreeTypeNames(t.get(), bound, out);
            for (const ClassMemberPtr& mp : c.members) {
                if (!mp) continue;
                if (mp->kind == NodeKind::PropertyDeclaration) {
                    const auto& p = static_cast<const PropertyDeclaration&>(*mp);
                    collectFreeTypeNames(p.type.get(), bound, out);
                } else if (mp->kind == NodeKind::MethodDeclaration) {
                    const auto& md = static_cast<const MethodDeclaration&>(*mp);
                    auto fnBound = bound;
                    for (const auto& tp : md.typeParams)
                        if (tp) fnBound.insert(tp->name);
                    collectFreeTypeParamNames(md.typeParams, fnBound, out);
                    for (const ParameterPtr& p : md.params)
                        if (p && p->type) collectFreeTypeNames(p->type.get(), fnBound, out);
                    collectFreeTypeNames(md.returnType.get(), fnBound, out);
                } else if (mp->kind == NodeKind::AccessorDeclaration) {
                    const auto& a = static_cast<const AccessorDeclaration&>(*mp);
                    for (const ParameterPtr& p : a.params)
                        if (p && p->type) collectFreeTypeNames(p->type.get(), bound, out);
                    collectFreeTypeNames(a.returnType.get(), bound, out);
                } else if (mp->kind == NodeKind::ConstructorDeclaration) {
                    const auto& cd = static_cast<const ConstructorDeclaration&>(*mp);
                    for (const ParameterPtr& p : cd.params)
                        if (p && p->type) collectFreeTypeNames(p->type.get(), bound, out);
                }
            }
            return out;
        }
        case NodeKind::InterfaceDeclaration: {
            const auto& i = static_cast<const InterfaceDeclaration&>(decl);
            const auto bound = ownTypeParamNames(i.typeParams);
            collectFreeTypeParamNames(i.typeParams, bound, out);
            for (const TypePtr& e : i.extendsTypes) collectFreeTypeNames(e.get(), bound, out);
            for (const SignatureMemberPtr& m : i.members)
                if (m) collectFreeSignatureNames(*m, bound, out);
            return out;
        }
        case NodeKind::TypeAliasDeclaration: {
            const auto& t = static_cast<const TypeAliasDeclaration&>(decl);
            const auto bound = ownTypeParamNames(t.typeParams);
            collectFreeTypeParamNames(t.typeParams, bound, out);
            collectFreeTypeNames(t.type.get(), bound, out);
            return out;
        }
        default:
            return out;
    }
}

// The declaration one import binding carries, if any.
const Stmt* declarationStmt(const ImportedBinding& binding) {
    if (binding.classDecl != nullptr)
        return static_cast<const Stmt*>(binding.classDecl);
    if (binding.interfaceDecl != nullptr)
        return static_cast<const Stmt*>(binding.interfaceDecl);
    if (binding.aliasDecl != nullptr)
        return static_cast<const Stmt*>(binding.aliasDecl);
    return nullptr;
}

// Finds `name` in one module's exported surface: the named export, or a
// default export whose declaration is that declaration.
std::optional<ImportedBinding> linkedBinding(const ExportSurface& surface,
                                             const std::string& name) {
    if (const auto found = surface.named.find(name);
        found != surface.named.end()) {
        return found->second;
    }
    if (surface.hasDefault) {
        const ImportedBinding& d = surface.defaultExport;
        if (const Stmt* decl = declarationStmt(d);
            decl != nullptr && declarationName(*decl) == name) {
            return d;
        }
    }
    return std::nullopt;
}

const SemaResult* semaForUnit(
    const CompilationUnit& unit, const SemaResult& entrySema,
    const std::unordered_map<std::uint32_t, SemaResult>& unitSemas) {
    if (unit.isEntry) return &entrySema;
    const auto it = unitSemas.find(unit.id);
    return it == unitSemas.end() ? nullptr : &it->second;
}

class ExportCollector {
public:
    ExportCollector(const ModuleGraph& graph, const SemaResult& entrySema,
                    const std::unordered_map<std::uint32_t, SemaResult>& unitSemas)
        : graph_(graph),
          entrySema_(entrySema),
          unitSemas_(unitSemas),
          deps_(dependencyIndex(graph)) {}

    const ExportSurface& surfaceOf(CompilationUnit& unit) {
        const auto cached = surfaces_.find(unit.id);
        if (cached != surfaces_.end()) return cached->second;
        // A cycle contributes the partial surface; the cache entry exists
        // before recursion, exactly like the codegen link builder.
        ExportSurface& surface = surfaces_[unit.id];
        if (visiting_.count(unit.id) != 0 || !unit.ast) return surface;
        const SemaResult* sema = semaForUnit(unit, entrySema_, unitSemas_);
        if (sema == nullptr) return surface;
        visiting_.insert(unit.id);
        for (const StmtPtr& stmt : unit.ast->body) {
            if (!stmt || stmt->kind != NodeKind::ExportDeclaration) continue;
            applyExport(static_cast<const ExportDeclaration&>(*stmt), unit,
                        *sema, surface);
        }
        visiting_.erase(unit.id);
        return surface;
    }

    // Links the same-module declarations an imported binding's annotations
    // transitively reference into `out` (type positions only) - see the
    // "Same-module linking" block above. `homes` maps each binding to the
    // exported surface of the module it came from.
    void linkSameModuleTypes(ImportedBindings& out,
                             std::unordered_map<std::string,
                                                const ExportSurface*>& homes) {
        struct Item {
            const Stmt* decl;
            const ExportSurface* surface;
        };
        std::vector<Item> work;
        std::unordered_set<std::uint32_t> seenDecls;
        for (const auto& [name, binding] : out) {
            const Stmt* decl = declarationStmt(binding);
            const auto home = homes.find(name);
            if (decl != nullptr && home != homes.end() &&
                seenDecls.insert(decl->nodeId).second) {
                work.push_back({decl, home->second});
            }
        }
        while (!work.empty()) {
            const Item item = work.back();
            work.pop_back();
            const ExportSurface* surface = item.surface;
            for (const std::string& name : freeTypeNamesOf(*item.decl)) {
                if (out.find(name) != out.end()) continue;
                std::optional<ImportedBinding> linked = linkedBinding(*surface, name);
                if (!linked || !linked->isType) continue;
                out.emplace(name, *linked);
                homes[name] = surface;
                if (const Stmt* linkedDecl = declarationStmt(*linked);
                    linkedDecl != nullptr && seenDecls.insert(linkedDecl->nodeId).second) {
                    work.push_back({linkedDecl, surface});
                }
            }
        }
    }

    // Builds the import table sema needs for one unit: local binding name ->
    // the exported declaration's type/signature.
    ImportedBindings importsFor(CompilationUnit& unit) {
        ImportedBindings out;
        if (!unit.ast) return out;
        std::unordered_map<std::string, const ExportSurface*> homes;
        for (const StmtPtr& stmt : unit.ast->body) {
            if (!stmt || stmt->kind != NodeKind::ImportDeclaration) continue;
            const auto& imp = static_cast<const ImportDeclaration&>(*stmt);
            const ModuleDependency* dep =
                dependencyOf(graph_, unit.id, imp.span, deps_);
            const bool moduleTarget =
                dep != nullptr &&
                (dep->resolution == ModuleResolution::Local ||
                 dep->resolution == ModuleResolution::Package);
            if (!moduleTarget || dep->imported == UINT32_MAX) continue;
            CompilationUnit* target = graph_.unit(dep->imported);
            if (target == nullptr) continue;
            const ExportSurface& targetSurface = surfaceOf(*target);
            if (!imp.defaultBinding.empty() && targetSurface.hasDefault) {
                out[imp.defaultBinding] = targetSurface.defaultExport;
                homes[imp.defaultBinding] = &targetSurface;
            }
            for (const ImportSpecifierPtr& spec : imp.specifiers) {
                if (!spec) continue;
                const auto found = targetSurface.named.find(spec->imported);
                if (found == targetSurface.named.end()) continue;
                const std::string local =
                    spec->local.empty() ? spec->imported : spec->local;
                out[local] = found->second;
                homes[local] = &targetSurface;
            }
        }
        linkSameModuleTypes(out, homes);
        return out;
    }

private:
    CompilationUnit* targetOf(const ExportDeclaration& ed,
                              const CompilationUnit& unit) const {
        const ModuleDependency* dep =
            dependencyOf(graph_, unit.id, ed.span, deps_);
        const bool moduleTarget =
            dep != nullptr &&
            (dep->resolution == ModuleResolution::Local ||
             dep->resolution == ModuleResolution::Package);
        if (!moduleTarget || dep->imported == UINT32_MAX) return nullptr;
        return graph_.unit(dep->imported);
    }

    void applyExport(const ExportDeclaration& ed, CompilationUnit& unit,
                     const SemaResult& sema, ExportSurface& out) {
        switch (ed.kind) {
            case ExportKind::Declaration:
                if (ed.declaration) addDeclarationExports(*ed.declaration, sema, out);
                return;
            case ExportKind::Named:
                for (const ExportSpecifierPtr& spec : ed.specifiers) {
                    if (!spec) continue;
                    const auto binding = localBinding(unit, sema, spec->local);
                    if (!binding) continue;
                    const std::string exported =
                        spec->exported.empty() ? spec->local : spec->exported;
                    out.named[exported] = *binding;
                }
                return;
            case ExportKind::Default: {
                out.hasDefault = true;
                if (ed.defaultExpr) {
                    ImportedBinding b;
                    if (const Type* t = sema.typeOf(ed.defaultExpr->nodeId)) {
                        b.type = *t;
                    }
                    if (ed.defaultExpr->kind == NodeKind::Identifier) {
                        const std::string name =
                            static_cast<const Identifier&>(*ed.defaultExpr).name;
                        if (const auto local = localBinding(unit, sema, name)) {
                            b = *local;
                        }
                    }
                    b.isValue = true;
                    out.defaultExport = std::move(b);
                }
                return;
            }
            case ExportKind::ReExportNamed: {
                CompilationUnit* target = targetOf(ed, unit);
                if (target == nullptr) return;
                const ExportSurface& targetSurface = surfaceOf(*target);
                for (const ExportSpecifierPtr& spec : ed.specifiers) {
                    if (!spec) continue;
                    const auto found = targetSurface.named.find(spec->local);
                    if (found == targetSurface.named.end()) continue;
                    const std::string exported =
                        spec->exported.empty() ? spec->local : spec->exported;
                    out.named[exported] = found->second;
                }
                return;
            }
            case ExportKind::ReExportAll: {
                CompilationUnit* target = targetOf(ed, unit);
                if (target == nullptr) return;
                const ExportSurface& targetSurface = surfaceOf(*target);
                for (const auto& entry : targetSurface.named) {
                    out.named[entry.first] = entry.second;
                }
                return;
            }
        }
    }

    const ModuleGraph& graph_;
    const SemaResult& entrySema_;
    const std::unordered_map<std::uint32_t, SemaResult>& unitSemas_;
    std::map<DependencyKey, const ModuleDependency*> deps_;
    ExportSurfaceMap surfaces_;
    std::unordered_set<std::uint32_t> visiting_;
};

// The units in dependency order, entry included. Preludes arrive through their
// importers and are never analyzed standalone.
std::vector<CompilationUnit*> analysisOrder(const ModuleGraph& graph) {
    std::vector<CompilationUnit*> order = graph.compilationOrder();
    std::unordered_set<std::uint32_t> seen;
    std::vector<CompilationUnit*> out;
    out.reserve(order.size());
    for (CompilationUnit* unit : order) {
        if (unit == nullptr || unit->isPrelude || !unit->ast) continue;
        if (seen.insert(unit->id).second) out.push_back(unit);
    }
    return out;
}

// A lock target may be absolute (older locks) or project-relative (v0.2
// locks). Both forms name the same installed/linked package directory.
std::filesystem::path lockTarget(const std::filesystem::path& projectRoot,
                                 const std::string& target) {
    std::filesystem::path path(target);
    if (path.is_relative()) path = projectRoot / path;
    return path.lexically_normal();
}

// The directory an installed package lives in. Linked packages use their lock
// target; normal installs land under .cp3/packages/<name>.
std::filesystem::path packageRootFor(const std::string& specifier,
                                     const std::filesystem::path& projectRoot) {
    std::vector<LockEntry> entries;
    std::string err;
    if (readLockfile(projectRoot, entries, err)) {
        if (const LockEntry* entry = findLockEntry(entries, specifier)) {
            if (!entry->target.empty()) {
                const std::filesystem::path target =
                    lockTarget(projectRoot, entry->target);
                std::error_code ec;
                if (std::filesystem::is_directory(target, ec)) return target;
            }
        }
    }
    return projectRoot / ".cp3" / "packages" / std::filesystem::path(specifier);
}

// Resolves a package's declared `main`. Archives built before v0.2 can carry
// `"main": "src/index.cp3"` while the archive root has `index.cp3`; accept both
// layouts so an old artifact stays usable after the compiler fix. The returned
// `moduleName` is still the declared module path (`src/index`), because that is
// the namespace its shipped C++ implementation was generated under.
std::optional<PackageEntry> packageEntry(
    const std::filesystem::path& root) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(root, ec)) {
        PackageEntry info;
        info.path = root;
        info.root = root.parent_path();
        info.moduleName = root.stem().string();
        return info;
    }
    if (!std::filesystem::is_directory(root, ec)) return std::nullopt;

    std::vector<std::filesystem::path> candidates;
    JsonValue manifest;
    std::string err;
    std::string main;
    const bool hasManifest = loadPackageJson(root, manifest, err);
    if (hasManifest) {
        main = packageMain(manifest);
        candidates.push_back(root / std::filesystem::path(main));
        const std::filesystem::path mainPath(main);
        if (!mainPath.filename().empty()) {
            candidates.push_back(root / mainPath.filename());
        }
    }
    candidates.push_back(root / "index.cp3");
    candidates.push_back(root / "src" / "index.cp3");

    for (const std::filesystem::path& candidate : candidates) {
        if (!std::filesystem::is_regular_file(candidate, ec)) continue;
        std::filesystem::path modulePath =
            main.empty() ? std::filesystem::relative(candidate, root, ec)
                         : std::filesystem::path(main);
        if (main.empty() && ec) modulePath = candidate.filename();
        modulePath.replace_extension();
        std::string moduleName = modulePath.generic_string();
        if (moduleName.empty()) moduleName = candidate.stem().string();
        PackageEntry info;
        info.path = candidate;
        info.root = root;
        info.moduleName = std::move(moduleName);
        return info;
    }
    return std::nullopt;
}

std::optional<PackageEntry> resolvePackageEntry(
    const std::string& specifier, const std::filesystem::path& projectRoot) {
    return packageEntry(packageRootFor(specifier, projectRoot));
}

}  // namespace

std::unique_ptr<ModuleGraph> loadModuleGraph(SourceManager& sm, const SourceFile& entry,
                                             DiagnosticSink& sink) {
    ModuleLoader loader(sink, resolvePackageEntry);
    return loader.load(sm, entry);
}

std::vector<std::string> importedStdModules(const ModuleGraph& graph) {
    std::vector<std::string> modules;
    for (const ModuleDependency& dep : graph.dependencies()) {
        if (dep.resolution == ModuleResolution::Std) {
            modules.push_back(dep.specifier);
        }
    }
    return modules;
}

bool analyzeProgram(Program& program, SemaResult& out, DiagnosticSink& sink,
                    const CompileOptions& compile,
                    const ImportedBindings* imports) {
    SemanticAnalyzer analyzer(sink, compile, imports);
    return analyzer.analyze(program, out);
}

// Splits a "label\nmember\nmember" shape (the shapeText format the
// interface_hash.hpp renderers emit) into the serializer's record struct.
InterfaceClassShape splitShapeText(const std::string& text) {
    InterfaceClassShape shape;
    const std::size_t nl = text.find('\n');
    if (nl == std::string::npos) {
        shape.label = text;
        return shape;
    }
    shape.label = text.substr(0, nl);
    std::size_t at = nl + 1;
    while (at < text.size()) {
        const std::size_t end = text.find('\n', at);
        if (end == std::string::npos) {
            shape.memberText.push_back(text.substr(at));
            break;
        }
        shape.memberText.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    return shape;
}

// Computes each emittable unit's interface hash. `ExportSurface` is the same
// structure `analyzeProgramGraph` builds; the builders in interface_hash.hpp
// flatten it into the canonical signature text and `interfaceHashOf` hashes
// it. Deterministic per source and sema state. Used by the incremental
// reuse wiring (per-unit fingerprint of dependency interfaces).
std::unordered_map<std::uint32_t, std::uint64_t> interfaceHashesForProgram(
    const ModuleGraph& graph, const SemaResult& entrySema,
    const std::unordered_map<std::uint32_t, SemaResult>& unitSemas) {
    ExportCollector exports(graph, entrySema, unitSemas);
    std::unordered_map<std::uint32_t, std::uint64_t> out;
    for (CompilationUnit* unit : graph.units()) {
        if (unit == nullptr || !unit->ast) continue;
        if (unit->isPrelude && !unit->isEntry) continue;
        if (semaForUnit(*unit, entrySema, unitSemas) == nullptr) continue;
        const ExportSurface& surface = exports.surfaceOf(*unit);
        std::vector<InterfaceExportEntry> entries;
        std::vector<InterfaceClassShape> classes;
        entries.reserve(surface.named.size() + 1);
        for (const auto& mapping : surface.named) {
            const ImportedBinding& b = mapping.second;
            if (b.classDecl != nullptr) {
                classes.push_back(
                    splitShapeText(classDeclToInterfaceShape(*b.classDecl)));
                continue;
            }
            if (b.interfaceDecl != nullptr) {
                classes.push_back(splitShapeText(
                    interfaceDeclToInterfaceShape(*b.interfaceDecl)));
                continue;
            }
            if (b.aliasDecl != nullptr) {
                classes.push_back(splitShapeText(
                    aliasDeclToInterfaceShape(*b.aliasDecl)));
                continue;
            }
            entries.push_back(importedBindingToEntry(
                mapping.first, b, b.isFunction || !b.isType ? "value" : "type"));
        }
        if (surface.hasDefault) {
            const ImportedBinding& b = surface.defaultExport;
            if (b.classDecl != nullptr) {
                classes.push_back(
                    splitShapeText(classDeclToInterfaceShape(*b.classDecl)));
            } else if (b.interfaceDecl != nullptr) {
                classes.push_back(splitShapeText(
                    interfaceDeclToInterfaceShape(*b.interfaceDecl)));
            } else if (b.aliasDecl != nullptr) {
                classes.push_back(splitShapeText(
                    aliasDeclToInterfaceShape(*b.aliasDecl)));
            } else {
                entries.push_back(
                    importedBindingToEntry("default", b, "default"));
            }
        }
        out[unit->id] = interfaceHashOf(entries, classes);
    }
    return out;
}

bool hasProgramModules(const ModuleGraph& graph) {
    for (const ModuleDependency& dep : graph.dependencies()) {
        if (dep.imported != UINT32_MAX &&
            (dep.resolution == ModuleResolution::Local ||
             dep.resolution == ModuleResolution::Package)) {
            return true;
        }
    }
    return false;
}

void analyzeProgramGraph(ModuleGraph& graph, SemaResult& entrySema,
                         std::unordered_map<std::uint32_t, SemaResult>& unitSemas,
                         DiagnosticSink& sink, const CompileOptions& compile) {
    const std::vector<CompilationUnit*> order = analysisOrder(graph);
    ExportCollector exports(graph, entrySema, unitSemas);
    for (CompilationUnit* unit : order) {
        if (unit == nullptr || !unit->ast) continue;
        const ImportedBindings imports = exports.importsFor(*unit);
        SemaResult& out = unit->isEntry ? entrySema : unitSemas[unit->id];
        analyzeProgram(*unit->ast, out, sink, compile,
                       imports.empty() ? nullptr : &imports);
    }
}

GeneratedModule generateModule(const GenerateModuleInput& input) {
    CodeGenerator codegen(input.sink, input.lineDirectives);
    return codegen.generate(input.program, input.sema, input.moduleName,
                            input.options);
}

GeneratedModule generateProgramModule(const GenerateProgramInput& input) {
    return generateProgram(input);
}

GeneratedProgramModules generateProgramModules(const GenerateProgramInput& input) {
    return cp3::generateProgramModules(input);
}

}  // namespace cp3::cli
