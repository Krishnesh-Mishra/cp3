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
//   support/int_mode.cpp   the guarded int64 fast path + `%` lowering
//   support/int_reads.cpp  the read-only-inside-return analysis for int mode
//   emit/expr.cpp          expression lowering (operators, literals, tsCall/cond)
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
#include "detail/type_map.h"

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
    std::string plainBody(const FunctionDeclaration& fd, const std::string& ret);
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
    // A type annotation -> a cp3 Type (detail::typeFromTypeNode), with every
    // Class part named like a collected enum rewritten to TypeKind::Enum.
    std::optional<Type> typeOfNode(const TypeNode& tn) const;
    void rewriteEnumTypes(Type& t) const;
    Type effectiveType(const Expr& e) const;
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
    // A whole array in a formatter or truthiness test pulls the arr family
    // block (D69), even when this unit never spells the C++ type itself (a
    // cross-module call's result, say).
    void noteFamilyUse(const Expr& e) {
        const detail::CppInfo ci = info(e);
        if (ci.shape == detail::Shape::Array ||
            ci.shape == detail::Shape::TypedArray) {
            needsTypedArray_ = true;
        }
    }
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

    // --- calls (emit/call.cpp) -----------------------------------------------
    std::string emitCall(const CallExpr& c);
    // Lowers a user-function call's argument list, applying the callee's
    // optional/default parameters to arguments that are explicit `undefined`.
    std::vector<std::string> emitCallArgs(const CallExpr& c,
                                          const FunctionDeclaration* decl);
    bool isConsoleMember(const CallExpr& c) const;

    // --- std/console lowering (src/std/console/codegen.cpp) ------------------
    // Every `console.<member>(...)` call lands here (emit_call.cpp keeps only
    // the `console.` receiver check). Each member lowers onto its contract in
    // cp3/std/console.hpp; an object-literal argument gets the explicit D49
    // object form.
    std::string emitConsoleCall(const CallExpr& c, const MemberExpr& m);
    std::string emitConsoleLogCall(const CallExpr& c);
    std::string emitConsoleStreamCall(const CallExpr& c,
                                      const std::string& method);
    std::string emitConsoleAssert(const CallExpr& c);
    std::string emitConsoleInput(const CallExpr& c);
    std::string emitConsoleListener(const CallExpr& c,
                                    const std::string& method);
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
    // The `IndexExpr` dispatch: the family read when the receiver is one,
    // otherwise the ordinary not-implemented report.
    std::string emitIndexExpr(const IndexExpr& ix);
    std::string emitFamilyIndexWrite(const AssignmentExpr& a);
    // The receiver text of a family method: an `Array(n)` constructor or an
    // array literal takes the element type the call fixed (`elem`).
    std::string familyReceiverText(const Expr& obj, const Type& elem,
                                   const Node& site);
    std::string arrayCtorText(const ExprList& args, const Type& elem,
                              const Node& site);
    std::string typedCtorText(const std::string& cls, const ExprList& args,
                              const Node& site);
    std::string arrayStaticCallText(const CallExpr& c,
                                    const std::string& prop);
    std::string arrayFillCallText(const CallExpr& c, const MemberExpr& m,
                                  const Type& recv);
    std::string arrayJoinCallText(const CallExpr& c, const MemberExpr& m,
                                  const Type& recv);
    std::string arraySortCallText(const CallExpr& c, const MemberExpr& m,
                                  const Type& recv);
    std::string arrayIncludesCallText(const CallExpr& c, const MemberExpr& m,
                                      const Type& recv);
    std::string typedMethodCallText(const CallExpr& c, const MemberExpr& m,
                                    const char* tag);
    std::string dataViewCallText(const CallExpr& c, const MemberExpr& m);
    std::string arrayLiteralText(const ArrayLiteral& lit,
                                 const Type& elemOverride);
    std::string declaratorInitText(const VariableDeclarator& d,
                                   const Type& declared);

    // --- std/collections (src/std/collections/codegen.cpp) --------------------
    // `new Map<K, V>()` / `new Set<T>()`, the member calls (`set`/`get`/`has`/
    // `delete`/`clear`/`keys`/`values`/`entries`/`forEach`) and the `size` /
    // `entries().length` property reads. Each returns "" when the form is not
    // one of them.
    std::string emitCollectionsNew(const NewExpr& n);
    std::string emitCollectionsMemberCall(const CallExpr& c,
                                          const MemberExpr& m);
    std::string emitCollectionsMember(const MemberExpr& m);

    // --- std namespaces + primitive methods (src/std/*/codegen.cpp) ----------
    // Lower `Math.*`/`path.*`/`os.*`/`JSON.stringify`/`process.*` calls, the
    // `string` method surface, and builtin property reads (`Math.PI`,
    // `path.sep`, `process.argv`, `s.length`). Each returns "" when the form
    // is not implemented, so the caller can report CP3002. The per-family
    // member reads live with their lowering units too; the namespace dispatch
    // (emitBuiltinCall / emitBuiltinMember) and the shared helpers
    // (detail/builtin_tables.h) stay in emit/std/common.cpp.
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
    std::string emitStringMethodCall(const CallExpr& c, const MemberExpr& m);
    // Native-backed std modules (src/std/crypto/codegen.cpp,
    // src/std/network/codegen.cpp): one per-module lowering unit. Each returns
    // "" when the form is not implemented, so the caller keeps reporting
    // CP3002 meanwhile.
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
    // methods and the `buf.length` read. Each returns "" when the form is not
    // one of them.
    std::string emitByteBufferStaticCall(const CallExpr& c, const MemberExpr& m);
    std::string emitByteBufferMethodCall(const CallExpr& c, const MemberExpr& m);
    std::string emitByteBufferMember(const MemberExpr& m);
    // `std/network` receivers are recognised by the sema marker names
    // ("$network:*"), so the hooks live on the receiver, not on the member:
    // named imports (identifier calls), `new` forms, member calls/reads.
    bool isNetworkObject(const Expr* e) const;
    std::string emitNetworkIdentifier(const CallExpr& c, const std::string& name);
    std::string emitUnresolvedCall(const CallExpr& c, const std::string& name);
    std::string emitNetworkNew(const NewExpr& n);
    std::string emitNetworkMemberCall(const CallExpr& c, const MemberExpr& m);
    std::string emitNetworkMemberRead(const MemberExpr& m);
    std::string emitNetworkFetch(const CallExpr& c, const ExprList& args);
    std::string fetchInitText(const Expr& init);
    std::string networkArg(const Type& receiver, const std::string& property,
                           const CallExpr& c, const Expr& arg, std::size_t index);
    std::string networkArgs(const Type& receiver, const std::string& property,
                            const CallExpr& c);
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

    // --- enums (emit/enum.cpp) ------------------------------------------------
    // Top-level `enum` declarations (spec 04): `collectEnums` resolves each
    // declaration and prepends the `enum class <name> : long long` text;
    // `emitEnumMember`/`emitEnumObjectLiteral` lower a member access and an
    // enum-carrying object literal; `emitEnumSwitch` handles a switch statement
    // (the same-enum lowering via `lowerEnumSwitch`, else the CP3002 stub).
    void collectEnums(const Program& program);
    std::string emitEnumMember(const MemberExpr& m);
    std::string emitEnumObjectLiteral(const ObjectLiteral& ol);
    bool lowerEnumSwitch(const SwitchStatement& sw, std::string& out, int indent);
    void emitEnumSwitch(const Stmt& s, std::string& out, int indent);

    // --- int64 fast path + `%` lowering (int_mode.cpp) ------------------------
    // The fast path is emitted per function: a body with every `number`
    // lowered to a guarded std::int64_t, plus a double wrapper that guards the
    // arguments once and re-runs the ordinary body when a guard bails.
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
    std::string modCall(const std::string& lhs, const Expr& rhs,
                        const std::string& rhsText);
    // The C++ type source of a local binding (declared, else inferred from the
    // initializer) and whether int mode lowers it to std::int64_t.
    detail::CppInfo bindingCpp(const VariableDeclarator& d) const;
    bool intBinding(const detail::CppInfo& ci) const;
    void declareBinding(const VariableDeclarator& d);
    // A fresh binding: a name with a deferred bound check may not be shadowed
    // (the marks are keyed by name), so shadowing fails the attempt.
    void intNewBinding(const std::string& name);
    // `name` is read only inside `return`s, so its bound check may be deferred.
    bool intDeferable(const std::string& name) const;
    // The text of `a` for the identifier target `id` (checked or deferred).
    std::string intAssignText(const AssignmentExpr& a, const Identifier& id);
    // The value of a deferred assignment: `x +/- <small literal>`, or empty.
    std::string intAssignValue(const Expr& e);
    // An operand of a raw `+`/`-`: a deferred read stays unchecked.
    std::string intRawOperand(const Expr& e);

    // --- assembly (code_generator.cpp) ---------------------------------------
    std::string buildHeader(const std::string& decls) const;
    std::string buildSource(const std::string& decls, const std::string& defs,
                            const std::string& mainBody) const;

    // --- state ----------------------------------------------------------------
    // The int64 fast-path state (D64): non-null exactly while a function's fast
    // body is being attempted. `failed` means the attempt cannot be proven
    // exact and must be discarded, in which case the ordinary double body is
    // the only lowering.
    struct IntMode {
        bool failed = false;
        // Names read somewhere outside a `return` argument (int_reads.cpp);
        // `nullopt` when that walk could not see the whole body. A name absent
        // here is read only by `return`, so its bound check can be deferred
        // from the operation to that single observation point.
        std::optional<std::unordered_set<std::string>> readOutsideReturns;
        // Locals whose bound check is currently deferred: their value may sit
        // outside [-2^53, 2^53] until `arith::pass` re-checks it at a read.
        std::unordered_set<std::string> deferred;
        // Set while emitting a statement's own expression: the one position
        // where an assignment's value is discarded, so a raw store is allowed.
        bool stmtExpr = false;
    };
    std::unique_ptr<IntMode> intMode_;

    DiagnosticSink& sink_;
    const SemaResult& sema_;
    std::string moduleName_;
    std::string namesp_;

    bool consoleImported_ = false;
    bool consoleStderr_ = false;   // console.warn/.error used (stderr output)
    bool needsConsoleAbi_ = false; // console async/misc members -> family header
    bool needsEventLoop_ = false;  // async/timers: emit runEventLoop() in main
    bool needsArith_ = false;      // `%`/int64 lowering -> cp3/core/arith.hpp include
    bool suppressDiagnostics_ = false;  // an int64 attempt must not report
    bool needsMath_ = false;       // Math.* -> cp3/std/math.hpp include
    bool needsString_ = false;     // string methods -> cp3/std/string.hpp include
    bool needsPath_ = false;       // path.* -> cp3/std/path.hpp include
    bool needsOs_ = false;         // os.* -> cp3/std/os.hpp include
    bool needsJson_ = false;       // JSON.stringify/parse -> cp3/std/json.hpp include
    // The registered JSON record shapes (usage-based reification): per
    // interface name the C++ struct text, the generated builder body and the
    // declared field names, plus the sema shape label -> interface name. The
    // structs materialize into the decls section on first use only.
    std::unordered_map<std::string, std::string> jsonShapeStructs_;
    std::unordered_map<std::string, std::string> jsonShapeBuilders_;
    std::unordered_map<std::string, std::vector<std::string>> jsonShapeFields_;
    std::unordered_map<std::string, std::string> jsonShapeByLabel_;
    std::unordered_set<std::string> jsonShapeUsed_;
    std::string jsonShapeDecls_;
    // The unit's enums (collectEnums): the resolved model per declaration name,
    // the accumulated `enum class` decls text, and the sema object label ->
    // field names of every enum-carrying object literal that lowered to a
    // `std::tuple` (the field read is `std::get<i>`).
    std::unordered_map<std::string, detail::EnumCppInfo> enums_;
    std::string enumDecls_;
    std::unordered_map<std::string, std::vector<std::string>> enumObjectFields_;
    bool needsProcess_ = false;    // process.* -> cp3/std/process.hpp include
    bool needsCrypto_ = false;     // crypto.* -> cp3/std/crypto.hpp include
    bool needsDate_ = false;       // std/date -> cp3/std/date.hpp include
    bool needsNetwork_ = false;    // std/network -> cp3/std/network.hpp include
    bool needsFs_ = false;         // std/fs -> cp3/std/fs.hpp include
    bool needsTypedArray_ = false; // the family -> cp3/typed_array.hpp include
    bool needsCollections_ = false;  // std/collections -> cp3/std/collections.hpp
    bool needsBuffer_ = false;       // std/buffer -> cp3/std/buffer.hpp include
    bool needsTuple_ = false;        // enum-carrying object literal -> <tuple>
    bool needsEventEmitter_ = false;  // EventEmitter/Listener -> cp3/async/emitter.hpp
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
