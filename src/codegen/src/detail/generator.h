// =============================================================================
// src/codegen/src/detail/generator.h - PRIVATE declaration of the codegen
// emitter.
//
// Declares `Generator`, the class that owns all codegen state and lowers one
// checked `Program` to a C++20 `.hpp`/`.cpp` pair. The frozen public boundary
// (`CodeGenerator`, `GeneratedModule`, `generate(...)`) stays in `cp3/codegen.hpp`;
// `CodeGenerator::generate` is a thin shell that builds a `Generator` and runs it.
//
// Method definitions are grouped by concern across the module's translation
// units (STYLE.md §8):
//
//   code_generator.cpp     entry (generate), module namespace, synthesized main()
//   support/mangle.cpp     identifier/namespace mangling (free helpers)
//   support/cpp_writer.cpp indentation buffer + low-level literal/string helpers
//   support/type_map.cpp   cp3 type -> C++ type mapping + type resolution
//   support/value_shapes.cpp  primitive unions + object value shapes
//   support/helper_blocks.cpp the generated-code helper blocks (new families)
//   support/int_mode.cpp   the guarded int64 fast path + `%` lowering
//   support/int_reads.cpp  the read-only-inside-return analysis for int mode
//   emit/expr.cpp          expression lowering (operators, literals, tsCall/cond)
//   emit/objects.cpp       object value shapes: member reads + literal values
//   emit/assignment.cpp    assignment lowering (compounds, bitwise, logical)
//   emit/call.cpp          call lowering (dispatch only, D72)
//   emit/async.cpp         await / arrow functions / timers / Promise.*
//   emit/stmt.cpp          statement lowering
//   emit/decl.cpp          forward decls, function signatures, declarations
//   emit/enum.cpp          enum declarations, member access, same-enum switch
//                          (spec 04)
//   src/std/typed-arrays/codegen.cpp  the JS-compat typed-array family (D69)
//   emit/std/common.cpp               ambient std surface: shared helpers +
//                                     dispatch
//   src/std/<family>/codegen.cpp      Math / path / os / process / JSON /
//                                     string / console lowering
//
// This header is implementation detail: it lives under
// `src/codegen/src/detail/` and is never included by another module
// (STYLE.md §3).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cp3/analysis.hpp"
#include "cp3/ast.hpp"
#include "cp3/codegen.hpp"
#include "cp3/diagnostic.hpp"
#include "detail/enums.h"
#include "detail/generator_state.h"
#include "detail/type_map.h"
#include "detail/worker.h"

namespace cp3 {

class Generator;

// The regular-array method surface (src/std/arrays/codegen_methods.cpp): push/
// pop, the callback set, searching, slicing, the in-place mutators and a
// `for...of`; a friend of Generator, defined in that unit.
namespace detail {
struct ArrayEmitter {
    static std::string methodCall(Generator& g, const CallExpr& c,
                                  const MemberExpr& m);
    static std::string callbackText(Generator& g, const CallExpr& c,
                                    const MemberExpr& m, const CppInfo& ec,
                                    const std::string& recvText);
    static void forOf(Generator& g, const ForOfStatement& f, std::string& out,
                      int indent);
};
}  // namespace detail
// The codegen implementation. One instance per `CodeGenerator::generate` call;
// it owns the declaration-type environment, the set of functions declared in the
// unit, and the CP3002 dedup set (the state block inherited from
// `GeneratorState`, detail/generator_state.h).
class Generator : private GeneratorState {
public:
    Generator(DiagnosticSink& sink, const SemaResult& sema,
              const std::string& moduleName, bool lineDirectives = false,
              const CodegenOptions& options = CodegenOptions{});

    // Top-level orchestration: collect names, emit every statement, assemble.
    GeneratedModule run(const Program& program);

private:
    friend struct detail::ArrayEmitter;  // src/std/arrays/codegen_methods.cpp
    // --- diagnostics (code_generator.cpp) ------------------------------------
    void notImplemented(const Node& node, const std::string& what);

    // --- source mapping (support/line_map.cpp) --------------------------------
    // `#line <line> "<source>"` for `node` (DWARF/gdb); no-op without mapping.
    void emitLineDirective(const Node& node, std::string& out);
    // Pin each physical line of a raw structural chunk (int64 prologue).
    void emitMappedRaw(const Node& node, const std::string& raw, std::string& out);

    // --- declarations (emit_decl.cpp) ----------------------------------------
    void collectFunctionNames(const Stmt& s);
    void emitTopLevel(const Stmt& s, std::string& decls, std::string& defs, std::string& mainBody);
    void emitExport(const ExportDeclaration& ed, std::string& decls,
                    std::string& defs, std::string& mainBody);
    void addFunction(const FunctionDeclaration& fd, std::string& decls, std::string& defs);
    std::string plainBody(const FunctionDeclaration& fd, const std::string& ret);
    void emitModuleVar(const VariableDeclaration& vd, std::string& decls,
                       std::string& mainBody);
    // A module-variable type M1 cannot represent stays "not implemented".
    void reportModuleVarType(const VariableDeclarator& d);
    void emitModuleVarNoInit(const detail::CppInfo& ci, const std::string& name,
                             const std::string& cv, std::string& decls);
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
    // Annotation -> cp3 Type, with collected-enum Class parts rewritten.
    std::optional<Type> typeOfNode(const TypeNode& tn) const;
    void rewriteEnumTypes(Type& t) const;
    Type effectiveType(const Expr& e) const;
    Type memberReadType(const MemberExpr& m) const;
    Type inferBinary(const BinaryExpr& b) const;
    Type inferLogical(const LogicalExpr& l) const;
    Type inferUnary(const UnaryExpr& u) const;
    detail::CppInfo info(const Expr& e) const;

    // --- expressions (emit_expr.cpp) -----------------------------------------
    std::string exprPlain(const Expr& e);
    std::string expr(const Expr& e);
    std::string exprCmp(const Expr& e);
    std::string emitIdentifier(const Identifier& id);
    std::string emitTemplate(const TemplateLiteral& tl);
    std::string tsCall(const Expr& e);
    std::string cond(const Expr& e);
    // A whole array in a formatter/truthiness test pulls the arr family (D69).
    void noteFamilyUse(const Expr& e);
    std::string emitBinary(const BinaryExpr& b);
    std::string emitEquality(const BinaryExpr& b, const detail::CppInfo& L,
                             const detail::CppInfo& R, const std::string& lhs,
                             const std::string& rhs);
    // The Shape::Union half of `==`/`===` (emit/equality.cpp).
    std::string unionEqualityText(const BinaryExpr& b, const detail::CppInfo& L,
                                  const detail::CppInfo& R, const std::string& lhs,
                                  const std::string& rhs);
    std::string emitLogical(const LogicalExpr& l);
    std::string emitNullish(const LogicalExpr& l);
    std::string emitUnary(const UnaryExpr& u);
    std::string emitTypeof(const UnaryExpr& u);
    std::string emitUpdate(const UpdateExpr& u);
    std::string emitConditional(const ConditionalExpr& c);
    std::string emitAssignment(const AssignmentExpr& a);

    // --- calls (emit/call.cpp) -----------------------------------------------
    std::string emitCall(const CallExpr& c);
    // Documented global numeric parsers (`parseInt`/`parseFloat`).
    std::string emitParseBuiltin(const CallExpr& c, const std::string& name);
    // User call arguments, applying optional/default parameters to `undefined`.
    std::vector<std::string> emitCallArgs(const CallExpr& c,
                                          const FunctionDeclaration* decl);
    bool isConsoleMember(const CallExpr& c) const;

    // --- parallel.* (emit/parallel.cpp) --------------------------------------
    // Ambient builtin (spec 13): documented shapes lower onto cp3/parallel/**,
    // everything else reports CP3002. `parallelCallType` types ambient results.
    std::optional<Type> parallelCallType(const CallExpr& c) const;
    bool parallelRange(const Expr& range, std::string& begin, std::string& end);
    bool parallelClosureShape(const ArrowFunction& a, std::size_t count) const;
    std::string emitParallelCall(const CallExpr& c, const MemberExpr& m);
    std::string emitParallelFor(const CallExpr& c);
    std::string emitParallelReduce(const CallExpr& c);
    std::string emitParallelRun(const CallExpr& c);
    std::string emitParallelClosure(
        const ArrowFunction& a, const std::vector<Type>& paramTypes,
        const std::vector<std::string>& contextParams);

    // --- worker handles (emit/worker.cpp, emit/worker_scan.cpp) --------------
    // File and inline `new Worker` sites resolve to a worker unit (D80).
    void collectWorkerSites(const Program& program);
    void emitInlineWorkerEntries(std::string& decls, std::string& defs);
    std::string emitWorkerNew(const NewExpr& n);
    std::string emitWorkerHandleCall(const CallExpr& c, const MemberExpr& m);
    std::string workerOnText(const CallExpr& c, const std::string& recv,
                             const Type& direction, const std::string& ownerNS,
                             bool workerSide);
    std::string workerSendText(const CallExpr& c, const std::string& recv,
                               const WorkerChannel& channel,
                               const std::string& ownerNS,
                               detail::WorkerSendSurface surface);
    std::string workerTerminateText(const CallExpr& c, const std::string& recv);
    std::string workerListenerText(const ArrowFunction& arrow, const Type& direction,
                                   const std::string& ownerNS, bool error);
    std::string workerListenerLambda(const ArrowFunction& arrow,
                                     const std::string& paramType, const Type& bound);
    std::string workerPayloadText(const Type& t, const std::string& ownerNS) const;
    std::optional<Type> workerSendArgumentType(const Expr& arg) const;
    bool workerPayloadAccepts(const Type& expected, const Type& actual,
                              const std::string& ownerNS) const;
    std::string workerPayloadConversion(const Type& expected, const std::string& expectedCpp,
                                        const Type& actual, const std::string& text) const;
    std::string workerObjectConversion(const Type& expected, const Type& actual,
                                       const std::string& text, const std::string& ownerNS) const;
    std::string objectLiteralForType(const ObjectLiteral& lit,
                                     const Type& declared);
    std::optional<Type> workerCallType(const CallExpr& c) const;
    std::optional<Type> workerBindingFor(const NewExpr& n) const;
    const WorkerSite* workerSiteAt(std::uint32_t nodeId) const;
    std::size_t workerInlineIndex(std::uint32_t nodeId) const;
    void emitWorkerEntryBody(const ArrowFunction& arrow, const Type& in,
                             const Type& outType, std::string& out);
    void reportWorkerCode(const Node& node, const std::string& code,
                          const std::string& message);
    // The worker side: lowers a worker unit's full module (imports, functions,
    // enums, `thread_local` module vars, and its default-export arrow as
    // `void cp3_default(ThreadHandle<In, Out>& thread)`).
    void emitWorkerTopLevel(const Stmt& s, std::string& decls,
                            std::string& defs, std::string& prologue);
    void emitWorkerModuleVar(const VariableDeclaration& vd, std::string& defs,
                             std::string& prologue);
    GeneratedModule runWorkerModule(const Program& program);

    // --- std/console lowering (src/std/console/codegen.cpp) ------------------
    // Every `console.<member>(...)` call lands here (emit_call.cpp keeps only
    // the `console.` receiver check). Each member lowers onto its contract in
    // cp3/std/console.hpp; an object-literal argument gets the explicit D49
    // object form.
    std::string emitConsoleCall(const CallExpr& c, const MemberExpr& m);
    std::string emitConsoleLogCall(const CallExpr& c);
    std::string emitConsoleStreamCall(const CallExpr& c, const std::string& method);
    std::string emitConsoleAssert(const CallExpr& c);
    std::string emitConsoleInput(const CallExpr& c);
    std::string emitConsoleListener(const CallExpr& c, const std::string& method);
    std::string consoleListenerArg(const Expr& arg);
    std::string consoleArgText(const Expr& arg);
    std::string consoleObjectText(const ObjectLiteral& literal);
    std::string consoleNestedValue(const Expr& value);

    // --- the builtin EventEmitter (D74, cp3/async/emitter.hpp) ----------------
    // `new EventEmitter()`; the boxed-listener value form (`let h = () => {}`,
    // an inline arrow argument, a function-typed local passed on); and the
    // on/once/off/emit method calls. Each returns "" when the form is not one
    // of them, so the caller keeps its other paths.
    std::string emitEventEmitterNew(const NewExpr& n);
    std::string emitListenerValue(const Expr& e);
    std::string emitEventEmitterCall(const CallExpr& c, const MemberExpr& m);

    // --- the JS-compat typed-array family (src/std/typed-arrays/codegen.cpp) ---
    // Constructors with and without `new` (`Array(2)`, `Int8Array([1])`,
    // `DataView(buf)`), the `Array.of`/`Array.from` statics, the method tables
    // (`fill`/`join`/... on arrays and typed views, `get*`/`set*` on DataView,
    // `slice` on the buffer classes), indexed reads/writes, and the declarator
    // forms whose element type comes from the binding. Each entry point
    // returns "" when the form is not one of them.
    std::string emitFamilyCtorCall(const CallExpr& c, const std::string& name);
    // `emitCall` with the `new`-less constructor spelling tried first.
    std::string emitCallOrFamily(const CallExpr& c);
    std::string emitFamilyCtorText(const std::string& cls, const ExprList& args,
                                   const Type& resultElem, const Node& site);
    std::string emitFamilyNew(const NewExpr& n);
    std::string emitFamilyMemberCall(const CallExpr& c, const MemberExpr& m);
    std::string emitFamilyMember(const MemberExpr& m);
    std::string emitFamilyIndexRead(const IndexExpr& ix);
    std::string indexOperandText(const Expr& index);  // enum member -> number
    // The `IndexExpr` dispatch: the family read, else a not-implemented report.
    std::string emitIndexExpr(const IndexExpr& ix);
    std::string emitFamilyIndexWrite(const AssignmentExpr& a);
    // The receiver text of a family method: an `Array(n)` constructor or an
    // array literal takes the element type the call fixed (`elem`).
    std::string familyReceiverText(const Expr& obj, const Type& elem, const Node& site);
    std::string arrayCtorText(const ExprList& args, const Type& elem, const Node& site);
    std::string typedCtorText(const std::string& cls, const ExprList& args, const Node& site);
    std::string arrayStaticCallText(const CallExpr& c, const std::string& prop);
    std::string arrayFillCallText(const CallExpr& c, const MemberExpr& m, const Type& recv);
    std::string arrayJoinCallText(const CallExpr& c, const MemberExpr& m, const Type& recv);
    std::string arraySortCallText(const CallExpr& c, const MemberExpr& m, const Type& recv);
    std::string arrayIncludesCallText(const CallExpr& c, const MemberExpr& m, const Type& recv);
    std::string typedMethodCallText(const CallExpr& c, const MemberExpr& m, const char* tag);
    std::string dataViewCallText(const CallExpr& c, const MemberExpr& m);
    std::string arrayLiteralText(const ArrayLiteral& lit, const Type& elemOverride);
    std::string declaratorInitText(const VariableDeclarator& d, const Type& declared);

    // --- object value shapes (emit/objects.cpp) -------------------------------
    std::string emitObject(const Expr& e);         // `o.a` / `o?.a` reads
    std::string emitObjectLiteral(const Expr& e);  // `{...}` literal values
    std::string emitObjectMemberWrite(const AssignmentExpr& a);  // `o.a = v`

    // --- std/collections (src/std/collections/codegen.cpp) --------------------
    // `new Map<K, V>()` / `new Set<T>()`, the member calls and the `size` /
    // `entries().length` reads; each returns "" when the form is not one of them.
    std::string emitCollectionsNew(const NewExpr& n);
    std::string emitCollectionsMemberCall(const CallExpr& c, const MemberExpr& m);
    std::string emitCollectionsMember(const MemberExpr& m);

    // --- std namespaces + primitive methods (src/std/*/codegen.cpp) ----------
    // Lower `Math.*`/`path.*`/`os.*`/`JSON.stringify`/`process.*` calls, the
    // `string` method surface and builtin property reads (`Math.PI`, `path.sep`,
    // `process.argv`, `s.length`). Each returns "" for a form it does not
    // implement, so the caller reports CP3002; the dispatch and the shared
    // tables (detail/builtin_tables.h) stay in emit/std/common.cpp.
    std::string emitBuiltinCall(const CallExpr& c, const MemberExpr& m);
    std::string emitBuiltinMember(const MemberExpr& m);
    std::string emitMathCall(const CallExpr& c, const MemberExpr& m);
    std::string emitMathMember(const MemberExpr& m);
    std::string emitPathCall(const CallExpr& c, const MemberExpr& m);
    std::string emitPathMember(const MemberExpr& m);
    std::string emitOsCall(const CallExpr& c, const MemberExpr& m);
    std::string emitJsonCall(const CallExpr& c, const MemberExpr& m);
    // The JSON record surface (STDLIB docs §04 / docs/language-spec §23): the
    // unit's interface declarations are registered up front (collectJsonShapes)
    // and a record shape is reified only when some `JSON.parse<T>` names it.
    // A registered declaration emits nothing by itself.
    void collectJsonShapes(const Program& program);
    void emitJsonInterface(const InterfaceDeclaration& i);
    std::string emitJsonStringify(const CallExpr& c);
    std::string emitJsonParse(const CallExpr& c);
    std::string jsonRecordParse(const std::string& shape, const std::string& text);
    std::string jsonValueText(const Expr& e);
    std::string emitJsonMember(const MemberExpr& m);
    std::string emitProcessCall(const CallExpr& c, const MemberExpr& m);
    std::string emitProcessMember(const MemberExpr& m);
    // `std/process` value receivers are recognised by the sema marker names
    // ("$process:*"), so the SpawnHandle hooks live on the receiver: the
    // `p.on/write/kill` member calls plus the string[] argument-vector
    // lowering exec/spawn share.
    bool isProcessObject(const Expr* e) const;
    std::string emitProcessObjectCall(const CallExpr& c, const MemberExpr& m);
    std::string processArgs(const Expr& e);
    // std/env (src/std/env/codegen.cpp): the `{ env }` named import's
    // get/has/load calls, backed by cp3/std/env.hpp (composed on process).
    std::string emitEnvCall(const CallExpr& c, const MemberExpr& m);
    std::string emitStringMethodCall(const CallExpr& c, const MemberExpr& m);
    // Native-backed std modules (src/std/{crypto,network}/codegen.cpp): one
    // lowering unit per module, each returning "" for a form it does not
    // implement, so the caller keeps reporting CP3002 meanwhile.
    std::string emitCryptoCall(const CallExpr& c, const MemberExpr& m);
    std::string emitNetworkCall(const CallExpr& c, const MemberExpr& m);
    // `std/fs` (src/std/fs/codegen.cpp): the namespace table plus the
    // FileStats / ReadStream / WriteStream receivers, recognised by the
    // `$fs:` marker names.
    std::string emitFsCall(const CallExpr& c, const MemberExpr& m);
    bool isFsReceiver(const Expr* e) const;
    std::string emitFsMemberCall(const CallExpr& c, const MemberExpr& m);
    std::string emitFsMemberRead(const MemberExpr& m);
    std::string emitFsStreamOn(const CallExpr& c, const MemberExpr& m);
    // std/date (src/std/date/codegen.cpp): the `Date.*` statics and the Date
    // value's instance methods; pure family, backed by cp3/std/date.hpp.
    std::string emitDateCall(const CallExpr& c, const MemberExpr& m);
    // The nominal std/buffer `Buffer` (src/std/buffer/codegen.cpp): the
    // `Buffer.from`/`Buffer.alloc` statics, the `buf.toString`/`buf.slice`
    // methods and the `buf.length` read; "" when the form is not one of them.
    std::string emitByteBufferStaticCall(const CallExpr& c, const MemberExpr& m);
    std::string emitByteBufferMethodCall(const CallExpr& c, const MemberExpr& m);
    std::string emitByteBufferMember(const MemberExpr& m);
    // `std/network` receivers are recognised by the sema marker names
    // ("$network:*"): named imports, `new` forms, member calls/reads.
    bool isNetworkObject(const Expr* e) const;
    // Std/network Map-shaped field reads (`res.headers`, `req.params`, ...):
    // their `Map<string, string>` type, re-derived from the hints below.
    std::optional<Type> networkMapFieldType(const Expr& e) const;
    std::string emitNetworkMapFieldCall(const CallExpr& c, const MemberExpr& m);
    std::string emitNetworkMapFieldMember(const MemberExpr& m);
    std::string emitNetworkIdentifier(const CallExpr& c, const std::string& name);
    std::string emitUnresolvedCall(const CallExpr& c, const std::string& name);
    std::string emitNetworkNew(const NewExpr& n);
    std::string emitNetworkMemberCall(const CallExpr& c, const MemberExpr& m);
    std::string emitNetworkMemberRead(const MemberExpr& m);
    std::string emitNetworkFetch(const CallExpr& c, const ExprList& args);
    // `res.json<T>()` / `req.json<T>()` (reified interface T; "" else) and
    // `res.json(payload, status?)`: the json value tree serializes the latter.
    std::string networkJsonRecord(const MemberExpr& m, const CallExpr& c);
    std::string networkResponseJson(const CallExpr& c, const MemberExpr& m);
    std::string fetchInitText(const Expr& init);
    std::string networkArg(const Type& receiver, const std::string& property,
                           const CallExpr& c, const Expr& arg, std::size_t index);
    std::string networkArgs(const Type& receiver, const std::string& property, const CallExpr& c);
    std::vector<std::string> emitPlainArgs(const CallExpr& c);

    // --- async / promises / errors (emit_async.cpp) --------------------------
    std::string emitAwait(const AwaitExpr& a);
    std::string emitArrow(const ArrowFunction& a, const std::vector<std::string>& contextParams);
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

    // --- enums (emit/enum.cpp) ------------------------------------------------
    // `collectEnums` resolves top-level `enum` declarations (spec 04) and
    // prepends the `enum class <name> : long long` text; `emitEnumMember` and
    // `emitEnumObjectLiteral` lower member reads and enum-carrying literals;
    // `emitEnumSwitch` handles switches (the same-enum lowering, else the stub).
    void collectEnums(const Program& program);
    std::string emitEnumMember(const MemberExpr& m);
    std::string emitEnumObjectLiteral(const ObjectLiteral& ol);
    bool lowerEnumSwitch(const SwitchStatement& sw, std::string& out, int indent);
    void emitEnumSwitch(const Stmt& s, std::string& out, int indent);

    // --- int64 fast path + `%` lowering (int_mode.cpp) ------------------------
    // Per function: a body with every `number` lowered to a guarded
    // std::int64_t, plus a double wrapper that re-runs it when a guard bails.
    bool intEligible(const FunctionDeclaration& fd, const std::string& ret) const;
    std::string intBody(const FunctionDeclaration& fd, bool& kept);
    std::string intSignature(const FunctionDeclaration& fd) const;
    std::string intWrapperPrologue(const FunctionDeclaration& fd) const;
    bool intOn() const;
    void intFail();
    bool numberTyped(const Expr& e) const;
    std::string intNumberExpr(const Expr& e);
    std::string intLiteral(const Expr& e);
    std::string intIdentifier(const Identifier& id);
    std::string intBinary(const BinaryExpr& b);
    std::string intProduct(const BinaryExpr& b);
    std::string intUnary(const UnaryExpr& u);
    std::string intUpdate(const UpdateExpr& u);
    std::string intAssignment(const AssignmentExpr& a);
    std::string intConditional(const ConditionalExpr& c);
    std::string intLogical(const LogicalExpr& l);
    // `a % b`: `modConst<K>` for an integral literal divisor, else `mod`.
    std::string modCall(const std::string& lhs, const Expr& rhs, const std::string& rhsText);
    // The C++ type of a local binding (declared, else inferred) + int-mode flag.
    detail::CppInfo bindingCpp(const VariableDeclarator& d) const;
    bool intBinding(const detail::CppInfo& ci) const;
    void declareBinding(const VariableDeclarator& d);
    // A fresh binding: a deferred-bound-check name may not be shadowed.
    void intNewBinding(const std::string& name);
    // `name` is read only inside `return`s, so its bound check may be deferred.
    bool intDeferable(const std::string& name) const;
    // The text of `a` for the identifier target `id` (checked or deferred).
    std::string intAssignText(const AssignmentExpr& a, const Identifier& id);
    std::string intAssignValue(const Expr& e);  // `x +/- <small literal>`
    std::string intRawOperand(const Expr& e);   // a raw `+`/`-` operand

    // --- assembly (code_generator.cpp) ---------------------------------------
    std::string buildHeader(const std::string& decls,
                            const std::string& globals) const;
    std::string sourceIncludes() const;
    std::string buildSource(const std::string& decls, const std::string& defs,
                            const std::string& globals,
                            const std::string& mainBody) const;
    // Sets the include flags from the spellings a unit actually emitted.
    void noteEmittedSpellings(const std::string& text);
    // The `int main` unit of the generated source: the std/process argument
    // vector init, the top-level-await coroutine and the event-loop drain.
    void emitMain(std::string& out, const std::string& mainBody) const;

    // The per-unit state block lives in `GeneratorState`
    // (detail/generator_state.h), inherited privately above.
};

}  // namespace cp3
