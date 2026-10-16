// =============================================================================
// src/codegen/src/generator.h — PRIVATE declaration of the codegen emitter.
//
// Declares `Generator`, the class that owns all codegen state and lowers one
// checked `Program` to a C++20 `.hpp`/`.cpp` pair. The frozen public boundary
// (`CodeGenerator`, `GeneratedModule`, `generate(...)`) stays in `cp3/codegen.hpp`;
// `CodeGenerator::generate` is a thin shell that builds a `Generator` and runs it.
//
// Method definitions are grouped by concern across the module's translation
// units (STYLE.md §8):
//
//   code_generator.cpp  entry (generate), module namespace, synthesized main()
//   mangle.cpp          identifier/namespace mangling (free helpers)
//   cpp_writer.cpp      indentation buffer + low-level literal/string helpers
//   type_map.cpp        cp3 type -> C++ type mapping + type resolution
//   emit_expr.cpp       expression lowering (incl. ?? / && / || / console.log)
//   emit_stmt.cpp       statement lowering
//   emit_decl.cpp       forward decls, function signatures, declarations
//
// This header is implementation detail: it lives under `src/codegen/src/` and is
// never included by another module (STYLE.md §3).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cp3/analysis.hpp"
#include "cp3/ast.hpp"
#include "cp3/codegen.hpp"
#include "cp3/diagnostic.hpp"
#include "type_map.h"

namespace cp3 {

// The codegen implementation. One instance per `CodeGenerator::generate` call;
// it owns the declaration-type environment, the set of functions declared in the
// unit, and the CP3002 dedup set.
class Generator {
public:
    Generator(DiagnosticSink& sink, const SemaResult& sema,
              const std::string& moduleName);

    // Top-level orchestration: collect names, emit every statement, assemble.
    GeneratedModule run(const Program& program);

private:
    // --- diagnostics (code_generator.cpp) ------------------------------------
    void notImplemented(const Node& node, const std::string& what);

    // --- declarations (emit_decl.cpp) ----------------------------------------
    void collectFunctionNames(const Stmt& s);
    void emitTopLevel(const Stmt& s, std::string& decls, std::string& defs,
                      std::string& mainBody);
    void emitExport(const ExportDeclaration& ed, std::string& decls,
                    std::string& defs, std::string& mainBody);
    void addFunction(const FunctionDeclaration& fd, std::string& decls,
                     std::string& defs);
    std::optional<Type> parameterType(const Parameter& p,
                                      const FunctionDeclaration& fd,
                                      std::size_t index) const;
    std::string parameterText(const Parameter& p, const FunctionDeclaration& fd,
                              std::size_t index, bool includeDefault);
    std::string functionReturnType(const FunctionDeclaration& fd);
    const Expr* firstReturnedExpr(const Stmt& s) const;

    // --- declaration-type environment + type resolution (type_map.cpp) -------
    void pushScope();
    void popScope();
    void declare(const std::string& name, const std::optional<Type>& type);
    std::optional<Type> lookup(const std::string& name) const;
    std::optional<Type> bindingType(const VariableDeclarator& d) const;
    std::optional<Type> resolvedBindingType(const VariableDeclarator& d) const;
    Type effectiveType(const Expr& e) const;
    Type inferBinary(const BinaryExpr& b) const;
    Type inferLogical(const LogicalExpr& l) const;
    Type inferUnary(const UnaryExpr& u) const;
    detail::CppInfo info(const Expr& e) const;

    // --- expressions (emit_expr.cpp) -----------------------------------------
    std::string expr(const Expr& e);
    std::string emitIdentifier(const Identifier& id);
    std::string emitTemplate(const TemplateLiteral& tl);
    std::string tsCall(const Expr& e);
    std::string cond(const Expr& e);
    std::string emitBinary(const BinaryExpr& b);
    std::string emitEquality(const BinaryExpr& b, const detail::CppInfo& L,
                             const detail::CppInfo& R, const std::string& lhs,
                             const std::string& rhs);
    std::string emitLogical(const LogicalExpr& l);
    std::string emitNullish(const LogicalExpr& l);
    std::string emitUnary(const UnaryExpr& u);
    std::string emitTypeof(const UnaryExpr& u);
    std::string emitUpdate(const UpdateExpr& u);
    std::string emitConditional(const ConditionalExpr& c);
    std::string emitAssignment(const AssignmentExpr& a);
    std::string emitCall(const CallExpr& c);
    bool isConsoleMember(const CallExpr& c) const;
    bool isConsoleCall(const CallExpr& c, const char* method) const;
    std::string emitConsoleLog(const CallExpr& c);

    // --- statements (emit_stmt.cpp) ------------------------------------------
    void stmt(const Stmt& s, std::string& out, int indent);
    void emitBranch(const Stmt& s, std::string& out, int indent);
    void emitVarDecl(const VariableDeclaration& vd, std::string& out, int indent);
    std::string declaratorText(const VariableDeclarator& d, VarKind kind);
    void emitFor(const ForStatement& f, std::string& out, int indent);
    void emitReturn(const ReturnStatement& r, std::string& out, int indent);

    // --- assembly (code_generator.cpp) ---------------------------------------
    std::string buildHeader(const std::string& decls) const;
    std::string buildSource(const std::string& decls, const std::string& defs,
                            const std::string& mainBody) const;

    // --- state ----------------------------------------------------------------
    DiagnosticSink& sink_;
    const SemaResult& sema_;
    std::string moduleName_;
    std::string namesp_;

    bool consoleImported_ = false;
    std::string currentReturn_ = "int";

    std::vector<std::unordered_map<std::string, Type>> env_;
    std::unordered_set<std::string> functions_;
    std::unordered_set<std::uint32_t> reported_;
};

}  // namespace cp3
