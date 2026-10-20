// =============================================================================
// src/cli/src/toolchain/worker_module.cpp - implements detail/worker_module.hpp.
//
// OWNERSHIP: cli module (src/cli/**). The worker linkage of spec 13:
//
//   * `findWorkerUnit` resolves the canonical `import worker from "<local>.cp3"`
//     of the entry unit against the module graph. Two or more canonical imports,
//     a non-local resolution, a missing unit or a missing AST all resolve to
//     nullopt (the entry's `worker(...)` then stays a CP3002 skip);
//   * `programCallsWorker` is a conservative scan for a `worker(...)` call, so
//     an unused import never pays for a worker TU. It over-approximates (an
//     unknown node kind reports true) and never under-approximates;
//   * `generateWorkerModule` runs sema on the worker unit with its own
//     `SemaResult` and lowers it with `CodegenOptions::workerModule` (only the
//     default export becomes `cp3_default`).
//
// The scan splits each direction over two functions (`…MentionsWorker` and
// `…Rest`), purely to keep every function inside the 80-line budget.
// =============================================================================
#include "detail/worker_module.hpp"

#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include "cp3/sema.hpp"

#include "detail/util.hpp"

namespace cp3::cli {

namespace {

// The one `import worker from "<local>.cp3"` of the unit; nullptr when the unit
// has none, and also when it has two or more (the target would be ambiguous).
const ImportDeclaration* canonicalWorkerImport(const Program& program) {
    const ImportDeclaration* found = nullptr;
    for (const auto& stmtPtr : program.body) {
        if (!stmtPtr || stmtPtr->kind != NodeKind::ImportDeclaration) continue;
        const auto& imp = static_cast<const ImportDeclaration&>(*stmtPtr);
        if (imp.kind != ImportKind::Default || imp.defaultBinding != "worker") {
            continue;
        }
        if (!endsWith(imp.source, ".cp3")) continue;
        if (found != nullptr) return nullptr;
        found = &imp;
    }
    return found;
}

// The include path of the worker's generated header as seen from the entry's
// generated .cpp: both live under the build dir, the entry inside its module
// name's parent (empty for the common bare-stem case).
std::string workerHeaderPath(const std::string& entryModuleName,
                             const std::string& workerModuleName) {
    const fs::path from = fs::path(entryModuleName).parent_path();
    const fs::path hpp(workerModuleName + ".hpp");
    if (from.empty()) return hpp.generic_string();
    return hpp.lexically_relative(from).generic_string();
}

// --- the conservative `worker(...)` scan ------------------------------------

bool exprMentionsWorker(const Expr& e);
bool exprRestMentionsWorker(const Expr& e);
bool stmtMentionsWorker(const Stmt& s);
bool stmtRestMentionsWorker(const Stmt& s);
bool forHeaderMentionsWorker(const NodePtr& left, const ExprPtr& right);

bool exprChildMentions(const ExprPtr& p) {
    return p != nullptr && exprMentionsWorker(*p);
}

bool stmtChildMentions(const StmtPtr& p) {
    return p != nullptr && stmtMentionsWorker(*p);
}

bool anyExprChildMentions(std::initializer_list<const ExprPtr*> children) {
    for (const ExprPtr* child : children) {
        if (child != nullptr && exprChildMentions(*child)) return true;
    }
    return false;
}

bool anyStmtChildMentions(std::initializer_list<const StmtPtr*> children) {
    for (const StmtPtr* child : children) {
        if (child != nullptr && stmtChildMentions(*child)) return true;
    }
    return false;
}

bool anyListedExprMentions(const std::vector<ExprPtr>& list) {
    for (const auto& item : list) {
        if (item && exprMentionsWorker(*item)) return true;
    }
    return false;
}

// The expression kinds that head ordinary entry code: leaves, the call itself,
// and the recurse shapes that are not containers.
bool exprMentionsWorker(const Expr& e) {
    switch (e.kind) {
        case NodeKind::Identifier:
        case NodeKind::NumberLiteral:
        case NodeKind::StringLiteral:
        case NodeKind::BoolLiteral:
        case NodeKind::NullLiteral:
        case NodeKind::UndefinedLiteral:
        case NodeKind::ThisExpr:
            return false;
        case NodeKind::CallExpr: {
            const auto& c = static_cast<const CallExpr&>(e);
            if (c.callee && c.callee->kind == NodeKind::Identifier &&
                static_cast<const Identifier&>(*c.callee).name == "worker") {
                return true;
            }
            return exprChildMentions(c.callee) || anyListedExprMentions(c.arguments);
        }
        case NodeKind::NewExpr: {
            const auto& n = static_cast<const NewExpr&>(e);
            return exprChildMentions(n.callee) || anyListedExprMentions(n.arguments);
        }
        case NodeKind::UnaryExpr:
            return exprChildMentions(static_cast<const UnaryExpr&>(e).operand);
        case NodeKind::UpdateExpr:
            return exprChildMentions(static_cast<const UpdateExpr&>(e).operand);
        case NodeKind::AwaitExpr:
            return exprChildMentions(static_cast<const AwaitExpr&>(e).argument);
        case NodeKind::SpreadElement:
            return exprChildMentions(static_cast<const SpreadElement&>(e).argument);
        case NodeKind::MemberExpr:
            return exprChildMentions(static_cast<const MemberExpr&>(e).object);
        default:
            return exprRestMentionsWorker(e);
    }
}

// The container expression kinds: literals of expressions and the operators.
bool exprRestMentionsWorker(const Expr& e) {
    switch (e.kind) {
        case NodeKind::BinaryExpr: {
            const auto& b = static_cast<const BinaryExpr&>(e);
            return anyExprChildMentions({&b.left, &b.right});
        }
        case NodeKind::LogicalExpr: {
            const auto& l = static_cast<const LogicalExpr&>(e);
            return anyExprChildMentions({&l.left, &l.right});
        }
        case NodeKind::AssignmentExpr: {
            const auto& a = static_cast<const AssignmentExpr&>(e);
            return anyExprChildMentions({&a.target, &a.value});
        }
        case NodeKind::ConditionalExpr: {
            const auto& c = static_cast<const ConditionalExpr&>(e);
            return anyExprChildMentions({&c.condition, &c.whenTrue, &c.whenFalse});
        }
        case NodeKind::IndexExpr: {
            const auto& ix = static_cast<const IndexExpr&>(e);
            return anyExprChildMentions({&ix.object, &ix.index});
        }
        case NodeKind::RangeExpr: {
            const auto& r = static_cast<const RangeExpr&>(e);
            return anyExprChildMentions({&r.start, &r.end});
        }
        case NodeKind::ArrayLiteral:
            return anyListedExprMentions(
                static_cast<const ArrayLiteral&>(e).elements);
        case NodeKind::TemplateLiteral:
            return anyListedExprMentions(
                static_cast<const TemplateLiteral&>(e).expressions);
        case NodeKind::ObjectLiteral: {
            const auto& ol = static_cast<const ObjectLiteral&>(e);
            for (const auto& propertyPtr : ol.properties) {
                const ObjectProperty* p = propertyPtr.get();
                if (p == nullptr) continue;
                if (p->computed && exprChildMentions(p->key)) return true;
                if (exprChildMentions(p->value)) return true;
            }
            return false;
        }
        case NodeKind::ArrowFunction: {
            const auto& a = static_cast<const ArrowFunction&>(e);
            if (!a.body) return false;
            if (a.expressionBody) {
                return exprMentionsWorker(*static_cast<const Expr*>(a.body.get()));
            }
            return stmtMentionsWorker(*static_cast<const Stmt*>(a.body.get()));
        }
        default:
            // Unknown shape (weak link, class expression, ...): assume it
            // might mention worker(...).
            return true;
    }
}

// The statement kinds that head ordinary entry code: the value-producing and
// nested-block shapes.
bool stmtMentionsWorker(const Stmt& s) {
    switch (s.kind) {
        case NodeKind::ExpressionStatement:
            return exprChildMentions(
                static_cast<const ExpressionStatement&>(s).expression);
        case NodeKind::VariableDeclaration: {
            const auto& vd = static_cast<const VariableDeclaration&>(s);
            for (const auto& d : vd.declarations) {
                if (d && exprChildMentions(d->init)) return true;
            }
            return false;
        }
        case NodeKind::BlockStatement: {
            const auto& b = static_cast<const BlockStatement&>(s);
            for (const auto& child : b.body) {
                if (stmtChildMentions(child)) return true;
            }
            return false;
        }
        case NodeKind::IfStatement: {
            const auto& is = static_cast<const IfStatement&>(s);
            return exprChildMentions(is.condition) ||
                   anyStmtChildMentions({&is.thenBranch, &is.elseBranch});
        }
        case NodeKind::ReturnStatement:
            return exprChildMentions(
                static_cast<const ReturnStatement&>(s).argument);
        case NodeKind::ThrowStatement:
            return exprChildMentions(
                static_cast<const ThrowStatement&>(s).argument);
        case NodeKind::TryStatement: {
            const auto& t = static_cast<const TryStatement&>(s);
            if (anyStmtChildMentions({&t.tryBlock, &t.finallyBlock})) return true;
            return t.handler && stmtChildMentions(t.handler->body);
        }
        case NodeKind::FunctionDeclaration:
            return stmtChildMentions(
                static_cast<const FunctionDeclaration&>(s).body);
        case NodeKind::ExportDeclaration: {
            const auto& ed = static_cast<const ExportDeclaration&>(s);
            return stmtChildMentions(ed.declaration) ||
                   exprChildMentions(ed.defaultExpr);
        }
        case NodeKind::ImportDeclaration:
        case NodeKind::TypeAliasDeclaration:
        case NodeKind::InterfaceDeclaration:
            return false;  // no expression can hide a call in these
        default:
            return stmtRestMentionsWorker(s);
    }
}

// The loop / switch / label statement kinds.
bool stmtRestMentionsWorker(const Stmt& s) {
    switch (s.kind) {
        case NodeKind::WhileStatement: {
            const auto& w = static_cast<const WhileStatement&>(s);
            return exprChildMentions(w.condition) || stmtChildMentions(w.body);
        }
        case NodeKind::DoWhileStatement: {
            const auto& w = static_cast<const DoWhileStatement&>(s);
            return exprChildMentions(w.condition) || stmtChildMentions(w.body);
        }
        case NodeKind::ForStatement: {
            const auto& f = static_cast<const ForStatement&>(s);
            if (f.init) {
                const bool fromInit =
                    f.init->kind == NodeKind::VariableDeclaration
                        ? stmtMentionsWorker(static_cast<const Stmt&>(*f.init))
                        : exprMentionsWorker(static_cast<const Expr&>(*f.init));
                if (fromInit) return true;
            }
            return exprChildMentions(f.condition) ||
                   exprChildMentions(f.update) || stmtChildMentions(f.body);
        }
        case NodeKind::ForOfStatement: {
            const auto& f = static_cast<const ForOfStatement&>(s);
            return forHeaderMentionsWorker(f.left, f.right) ||
                   stmtChildMentions(f.body);
        }
        case NodeKind::ForInStatement: {
            const auto& f = static_cast<const ForInStatement&>(s);
            return forHeaderMentionsWorker(f.left, f.right) ||
                   stmtChildMentions(f.body);
        }
        case NodeKind::SwitchStatement: {
            const auto& sw = static_cast<const SwitchStatement&>(s);
            if (exprChildMentions(sw.discriminant)) return true;
            for (const auto& casePtr : sw.cases) {
                if (!casePtr) continue;
                if (exprChildMentions(casePtr->test)) return true;
                for (const auto& child : casePtr->statements) {
                    if (stmtChildMentions(child)) return true;
                }
            }
            return false;
        }
        case NodeKind::LabeledStatement:
            return stmtChildMentions(
                static_cast<const LabeledStatement&>(s).body);
        default:
            // Unknown shape (class / enum declaration, ...): assume it might
            // mention worker(...).
            return true;
    }
}

// The for-of/for-in header: the left binding when it is a declaration, plus
// the iterated expression.
bool forHeaderMentionsWorker(const NodePtr& left, const ExprPtr& right) {
    if (left && left->kind == NodeKind::VariableDeclaration &&
        stmtMentionsWorker(static_cast<const Stmt&>(*left))) {
        return true;
    }
    return exprChildMentions(right);
}

}  // namespace

std::optional<WorkerUnit> findWorkerUnit(const Program& entry,
                                         const ModuleGraph& graph,
                                         const std::string& entryModuleName) {
    const ImportDeclaration* imp = canonicalWorkerImport(entry);
    const CompilationUnit* entryUnit = graph.entry();
    if (imp == nullptr || entryUnit == nullptr) return std::nullopt;
    for (const ModuleDependency& dep : graph.dependencies()) {
        if (dep.importer != entryUnit->id || dep.specifier != imp->source) continue;
        if (dep.resolution != ModuleResolution::Local || !dep.resolved) continue;
        const CompilationUnit* unit = graph.unit(dep.imported);
        if (unit == nullptr || !unit->ast) return std::nullopt;
        WorkerUnit out;
        out.unit = unit;
        out.moduleName = fs::path(imp->source).stem().string();
        out.sourceKey = unit->file != nullptr ? unit->file->path : std::string();
        out.headerPath = workerHeaderPath(entryModuleName, out.moduleName);
        return out;
    }
    return std::nullopt;
}

bool programCallsWorker(const Program& program) {
    for (const auto& stmtPtr : program.body) {
        if (stmtPtr && stmtMentionsWorker(*stmtPtr)) return true;
    }
    return false;
}

GeneratedModule generateWorkerModule(const WorkerUnit& worker, bool lineDirectives,
                                     DiagnosticSink& sink,
                                     const CompileOptions& compile) {
    if (worker.unit == nullptr || !worker.unit->ast) return GeneratedModule{};
    SemaResult sema;
    SemanticAnalyzer analyzer(sink, compile);
    if (!analyzer.analyze(*worker.unit->ast, sema)) {
        // The worker unit does not analyze cleanly: leave the entry's
        // `worker(...)` a skip rather than compiling a partial worker.
        return GeneratedModule{};
    }
    CodegenOptions options;
    options.workerModule = true;
    options.typeSystem = compile.typeSystem;
    CodeGenerator codegen(sink, lineDirectives);
    return codegen.generate(*worker.unit->ast, sema, worker.moduleName, options);
}

}  // namespace cp3::cli
