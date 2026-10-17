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
//   emit_expr.cpp       expression lowering (operators, literals, tsCall/cond)
//   emit_call.cpp       call lowering + console.*
//   emit_async.cpp      await / arrow functions / timers / Promise.*
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
    void emitModuleVar(const VariableDeclaration& vd, std::string& decls,
                       std::string& mainBody);
    std::optional<Type> parameterType(const Parameter& p,
                                      const FunctionDeclaration& fd,
                                      std::size_t index) const;
    std::string parameterText(const Parameter& p, const FunctionDeclaration& fd,
                              std::size_t index, bool includeDefault);
    std::string functionReturnType(const FunctionDeclaration& fd);
    Type asyncValueType(const FunctionDeclaration& fd) const;
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

    // --- calls + console.* (emit_call.cpp) -----------------------------------
    std::string emitCall(const CallExpr& c);
    // Lowers a user-function call's argument list, applying the callee's
    // optional/default parameters to arguments that are explicit `undefined`.
    std::vector<std::string> emitCallArgs(const CallExpr& c,
                                          const FunctionDeclaration* decl);
    bool isConsoleMember(const CallExpr& c) const;
    bool isConsoleCall(const CallExpr& c, const char* method) const;
    std::string emitConsoleLog(const CallExpr& c);
    std::string emitConsoleMethod(const CallExpr& c, const char* method);

    // --- std namespaces + primitive methods (emit_builtin.cpp) ----------------
    // Lower `Math.*`/`path.*`/`os.*`/`JSON.stringify`/`process.*` calls, the
    // `string` method surface, and builtin property reads (`Math.PI`,
    // `path.sep`, `process.argv`, `s.length`). Each returns "" when the form
    // is not implemented, so the caller can report CP3002.
    std::string emitBuiltinCall(const CallExpr& c, const MemberExpr& m);
    std::string emitBuiltinMember(const MemberExpr& m);
    std::string emitMathCall(const CallExpr& c, const MemberExpr& m);
    std::string emitPathCall(const CallExpr& c, const MemberExpr& m);
    std::string emitOsCall(const CallExpr& c, const MemberExpr& m);
    std::string emitJsonCall(const CallExpr& c, const MemberExpr& m);
    std::string emitProcessCall(const CallExpr& c, const MemberExpr& m);
    std::string emitStringMethodCall(const CallExpr& c, const MemberExpr& m);
    std::vector<std::string> emitPlainArgs(const CallExpr& c);

    // --- async / promises / errors (emit_async.cpp) --------------------------
    std::string emitAwait(const AwaitExpr& a);
    std::string emitArrow(const ArrowFunction& a,
                          const std::vector<std::string>& contextParams);
    std::vector<std::string> arrowParamText(
        const ArrowFunction& a, const std::vector<std::string>& contextParams);
    std::string arrowReturnType(const ArrowFunction& a);
    std::string emitTimerBuiltin(const CallExpr& c, const std::string& name);
    std::string emitPromiseMemberCall(const CallExpr& c, const MemberExpr& m);
    std::string emitPromiseStatic(const CallExpr& c, const MemberExpr& m);
    std::string emitPromiseResolve(const CallExpr& c);
    std::string emitPromiseReject(const CallExpr& c);

    // --- statements (emit_stmt.cpp) ------------------------------------------
    void stmt(const Stmt& s, std::string& out, int indent);
    void emitBranch(const Stmt& s, std::string& out, int indent);
    void emitIf(const IfStatement& is, std::string& out, int indent);
    void emitVarDecl(const VariableDeclaration& vd, std::string& out, int indent);
    std::string declaratorText(const VariableDeclarator& d, VarKind kind);
    void emitFor(const ForStatement& f, std::string& out, int indent);
    void emitReturn(const ReturnStatement& r, std::string& out, int indent);
    void emitThrow(const ThrowStatement& t, std::string& out, int indent);
    void emitTry(const TryStatement& ts, std::string& out, int indent);

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
    bool consoleStderr_ = false;   // console.warn/.error used (stderr output)
    bool needsEventLoop_ = false;  // async/timers: emit runEventLoop() in main
    bool needsMath_ = false;       // Math.* -> cp3/math.hpp include
    bool needsString_ = false;     // string methods -> cp3/string.hpp include
    bool needsPath_ = false;       // path.* -> cp3/path.hpp include
    bool needsOs_ = false;         // os.* -> cp3/os.hpp include
    bool needsJson_ = false;       // JSON.stringify -> cp3/json.hpp include
    bool needsProcess_ = false;    // process.* -> cp3/process.hpp include
    bool topLevelAwait_ = false;   // an `await` appeared at top level
    bool inAsyncBody_ = false;     // current function body lowers to a coroutine
    int nestingDepth_ = 0;         // function + arrow nesting (0 == top level)
    int uniqueCounter_ = 0;        // fresh names for generated scaffolding
    std::string currentReturn_ = "int";

    std::vector<std::unordered_map<std::string, Type>> env_;
    std::unordered_set<std::string> functions_;
    std::unordered_map<std::string, const FunctionDeclaration*> functionDecls_;
    std::unordered_set<std::string> asyncFunctions_;   // `async function` names
    std::vector<std::string> thrownBindings_;          // active `catch (e)` names
    std::unordered_set<std::uint32_t> reported_;
};

}  // namespace cp3
