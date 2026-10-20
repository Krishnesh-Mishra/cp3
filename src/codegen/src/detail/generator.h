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
#include "detail/class_model.h"
#include "detail/enums.h"
#include "detail/generator_state.h"
#include "detail/type_map.h"

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

// The spec 15 models are referenced by value in the private method signatures.
using detail::ClassMemberModel;
using detail::ClassModel;
using detail::ObjectModel;

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
    // `#line` for DWARF/gdb; a no-op unless mapping is on.
    void emitLineDirective(const Node& node, std::string& out);
    // Pins a raw chunk's lines to `node`; verbatim when mapping is off.
    void emitMappedRaw(const Node& node, const std::string& raw, std::string& out);

    // --- user classes / interfaces / object shapes (emit/class_decl.cpp) ------
    // Models keyed as sema keys them; structs/methods land in the decl/def text.
    void collectModels(const Program& program);
    void collectClassNames(const Stmt& s);
    void collectInterfaces(const Program& program);
    void collectBodyModels(const Stmt& s);
    void collectClassMemberModels(ClassModel& cm);
    void collectClassAccessorModel(ClassModel& cm, const ClassMember& mp);
    void emitTopLevelClass(const ClassDeclaration& c);
    void emitAllClasses();
    void emitLocalClass(const ClassDeclaration& c, std::string& out, int indent);
    void emitClassStruct(const ClassModel& cm, std::string& out, int indent,
                         std::string* defs);
    void emitClassMember(const ClassModel& cm, const ClassMemberModel& m,
                         std::string& out, int indent, std::string* defs);
    void emitClassAccessorMember(const ClassModel& cm,
                                 const ClassMemberModel& m, std::string& out,
                                 int indent, std::string* defs);
    std::string memberFieldText(const ClassMemberModel& m);
    std::string methodReturnType(const MethodDeclaration& md,
                                 const ClassModel* cm = nullptr);
    std::string methodParamText(const Parameter& p,
                                const ClassModel* cm = nullptr);
    std::string accessorReturnType(const AccessorDeclaration& ad,
                                   const ClassModel* cm = nullptr);
    std::string accessorBodyText(const AccessorDeclaration& ad,
                                 const ClassModel& cm, int indent);
    std::string methodBodyText(const MethodDeclaration& md, const ClassModel& cm,
                               int indent, bool isStatic);
    std::string ctorBodyText(const ConstructorDeclaration& cd,
                             const ClassModel& cm, int indent);
    std::string weakPointee(const Type& fieldType);

    // --- model expressions + initializers (emit/class_expr.cpp) ---------------
    std::string emitModelNew(const NewExpr& n);
    std::string emitModelMember(const MemberExpr& m);
    std::string emitModelCall(const CallExpr& c, const MemberExpr& m);
    std::string emitModelAssign(const AssignmentExpr& a, const MemberExpr& m);
    std::string emitSuperMemberAssign(const AssignmentExpr& a,
                                      const MemberExpr& m,
                                      const ClassModel& derived);
    std::string emitModelObjectLiteral(const ObjectLiteral& lit);
    std::string modelInitText(const VariableDeclarator& d, const Type& declared);
    std::string objectInitText(const ObjectModel& om, const ObjectLiteral& lit);
    std::string classLiteralInit(const ClassModel& cm, const ObjectLiteral& lit);
    std::string detachedMethodLambda(
        const ClassModel& cm, const ClassMemberModel& m, const std::string& recv);
    std::string objectFieldDefaultText(const Type& t);
    // One field store (weak/optional/compound); "" when the caller must report.
    std::string storeText(const ClassMemberModel& m, const std::string& lvalue,
                          const AssignmentExpr& a, const std::string& value);

    // --- declarations (emit_decl.cpp) ----------------------------------------
    void collectFunctionNames(const Stmt& s);
    void emitTopLevel(const Stmt& s, std::string& decls, std::string& defs, std::string& mainBody);
    void emitExport(const ExportDeclaration& ed, std::string& decls,
                    std::string& defs, std::string& mainBody);
    void addFunction(const FunctionDeclaration& fd, std::string& decls, std::string& defs);
    std::string plainBody(const FunctionDeclaration& fd, const std::string& ret);
    void emitModuleVar(const VariableDeclaration& vd, std::string& decls,
                       std::string& mainBody);
    // Reports a module-variable type M1 cannot represent (CP3002, no mis-typing).
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
    // True for a value binding; false for a class/interface name.
    bool hasValueBinding(const std::string& name) const;
    std::optional<Type> bindingType(const VariableDeclarator& d) const;
    std::optional<Type> resolvedBindingType(const VariableDeclarator& d) const;
    // Annotation -> cp3 Type, with collected-enum parts rewritten.
    std::optional<Type> typeOfNode(const TypeNode& tn) const;
    void rewriteEnumTypes(Type& t) const;
    Type effectiveType(const Expr& e) const;
    Type inferBinary(const BinaryExpr& b) const;
    Type inferLogical(const LogicalExpr& l) const;
    Type inferUnary(const UnaryExpr& u) const;
    detail::CppInfo info(const Expr& e) const;

    // --- expressions (emit_expr.cpp) -----------------------------------------
    std::string exprPlain(const Expr& e);
    std::string emitNewExpr(const NewExpr& n);
    std::string emitMemberAny(const MemberExpr& m);
    std::string emitObjectAny(const ObjectLiteral& lit);
    std::string expr(const Expr& e);
    std::string exprCmp(const Expr& e);
    std::string emitIdentifier(const Identifier& id);
    std::string emitTemplate(const TemplateLiteral& tl);
    std::string tsCall(const Expr& e);
    std::string cond(const Expr& e);
    // A whole-array format/truthiness use pulls the array family block (D69).
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
    std::string emitIdentifierCall(const CallExpr& c, const std::string& name);
    // `parseInt`/`parseFloat`: argument coercion plus the runtime helper.
    std::string emitParseBuiltin(const CallExpr& c, const std::string& name);
    // Call arguments, applying optional/default parameters for `undefined`.
    std::vector<std::string> emitCallArgs(const CallExpr& c,
                                          const FunctionDeclaration* decl);
    bool isConsoleMember(const CallExpr& c) const;

    // --- parallel.* / worker(...) (emit/parallel.cpp, emit/worker.cpp) --------
    // `parallel` and `worker` are ambient builtins (spec §13); each entry point
    // lowers onto cp3/parallel/** and reports CP3002 for anything outside the
    // documented shapes. `parallelCallType` types the calls sema leaves
    // ambient (the reduce accumulator's type, the `parallel.run` result
    // marker); `effectiveType`/`bindingType` consult it first.
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
    // `worker(...)` call forms, pre-scan, and the worker unit's own lowering.
    std::string emitWorkerCall(const CallExpr& c);
    std::string workerArgText(const Expr& arg);
    std::string emitWorkerObjectLiteral(const ObjectLiteral& ol);
    std::string emitWorkerMemberWrite(const AssignmentExpr& a);
    void collectWorkerObjects(const Program& program);
    void registerWorkerObject(const Type& objType, std::vector<std::string> names);
    std::string workerParamText(const Parameter& p, const ArrowFunction& arrow,
                                std::size_t index);
    std::string workerObjectParamText(const ObjectType& ot, const std::string& name,
                                      const std::optional<Type>& pt);
    std::string workerReturnType(const ArrowFunction& arrow);
    std::string workerBody(const ArrowFunction& arrow, const std::string& ret);
    GeneratedModule runWorkerModule(const Program& program);

    // --- std/console lowering (src/std/console/codegen.cpp) ------------------
    // `console.<member>(...)` onto cp3/std/console.hpp (D49 object form).
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
    // `new EventEmitter()`, boxed listeners, on/once/off/emit; "" = not mine.
    std::string emitEventEmitterNew(const NewExpr& n);
    std::string emitListenerValue(const Expr& e);
    std::string emitEventEmitterCall(const CallExpr& c, const MemberExpr& m);

    // --- the JS-compat typed-array family (src/std/typed-arrays/codegen.cpp) ---
    // The JS-compat family: constructors/statics, method tables, index reads.
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
    // Family method receiver; a ctor/literal takes the call's element type.
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

    // --- std/collections (src/std/collections/codegen.cpp) --------------------
    // `new Map/Set`, their members, `size` and `entries().length`; "" = not mine.
    std::string emitCollectionsNew(const NewExpr& n);
    std::string emitCollectionsMemberCall(const CallExpr& c, const MemberExpr& m);
    std::string emitCollectionsMember(const MemberExpr& m);

    // --- std namespaces + primitive methods (src/std/*/codegen.cpp) ----------
    // std namespace calls/reads and the string surface; "" = CP3002 upstream.
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
    // `std/process` marker receivers: SpawnHandle members and argv lowering.
    bool isProcessObject(const Expr* e) const;
    std::string emitProcessObjectCall(const CallExpr& c, const MemberExpr& m);
    std::string processArgs(const Expr& e);
    // std/env (src/std/env/codegen.cpp): the `{ env }` named import's
    // get/has/load calls, backed by cp3/std/env.hpp (composed on process).
    std::string emitEnvCall(const CallExpr& c, const MemberExpr& m);
    std::string emitStringMethodCall(const CallExpr& c, const MemberExpr& m);
    // Native std modules: one lowering unit per module; "" = keep CP3002.
    std::string emitCryptoCall(const CallExpr& c, const MemberExpr& m);
    std::string emitNetworkCall(const CallExpr& c, const MemberExpr& m);
    // `std/fs`: namespace table plus the `$fs:` marker receivers.
    std::string emitFsCall(const CallExpr& c, const MemberExpr& m);
    bool isFsReceiver(const Expr* e) const;
    std::string emitFsMemberCall(const CallExpr& c, const MemberExpr& m);
    std::string emitFsMemberRead(const MemberExpr& m);
    std::string emitFsStreamOn(const CallExpr& c, const MemberExpr& m);
    // std/date (src/std/date/codegen.cpp): the `Date.*` statics and the Date
    // value's instance methods; pure family, backed by cp3/std/date.hpp.
    std::string emitDateCall(const CallExpr& c, const MemberExpr& m);
    std::string emitRegexLiteral(const RegExpLiteral& lit);  // D48, cp3/core/regex.hpp
    std::string emitRegexCall(const CallExpr& c, const MemberExpr& m);  // test/match/exec
    std::string emitRegexMember(const MemberExpr& m);  // source/flags reads
    // `Buffer` statics, `buf.toString`/`slice`/`length`; "" = not mine.
    std::string emitByteBufferStaticCall(const CallExpr& c, const MemberExpr& m);
    std::string emitByteBufferMethodCall(const CallExpr& c, const MemberExpr& m);
    std::string emitByteBufferMember(const MemberExpr& m);
    // `std/network` receivers are recognised by the sema marker names
    // ("$network:*"): named imports, `new` forms, member calls/reads.
    bool isNetworkObject(const Expr* e) const;
    // Network Map-shaped field reads, re-derived from the hints below.
    std::optional<Type> networkMapFieldType(const Expr& e) const;
    std::string emitNetworkMapFieldCall(const CallExpr& c, const MemberExpr& m);
    std::string emitNetworkMapFieldMember(const MemberExpr& m);
    std::string emitNetworkIdentifier(const CallExpr& c, const std::string& name);
    std::string emitUnresolvedCall(const CallExpr& c, const std::string& name);
    std::string emitNetworkNew(const NewExpr& n);
    std::string emitNetworkMemberCall(const CallExpr& c, const MemberExpr& m);
    std::string emitNetworkMemberRead(const MemberExpr& m);
    std::string emitNetworkFetch(const CallExpr& c, const ExprList& args);
    // `json<T>()` with a reified interface, and `json(payload, status?)`.
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
    void emitWhile(const WhileStatement& ws, std::string& out, int indent);
    void emitDoWhile(const DoWhileStatement& dw, std::string& out, int indent);
    void emitVarDecl(const VariableDeclaration& vd, std::string& out, int indent);
    std::string declaratorText(const VariableDeclarator& d, VarKind kind);
    void emitFor(const ForStatement& f, std::string& out, int indent);
    void emitReturn(const ReturnStatement& r, std::string& out, int indent);
    void emitThrow(const ThrowStatement& t, std::string& out, int indent);
    void emitTry(const TryStatement& ts, std::string& out, int indent);

    // --- enums (emit/enum.cpp) ------------------------------------------------
    // Enum decls, member reads, enum-carrying literals and same-enum switches.
    void collectEnums(const Program& program);
    std::string emitEnumMember(const MemberExpr& m);
    std::string emitEnumObjectLiteral(const ObjectLiteral& ol);
    bool lowerEnumSwitch(const SwitchStatement& sw, std::string& out, int indent);
    void emitEnumSwitch(const Stmt& s, std::string& out, int indent);

    // --- int64 fast path + `%` lowering (int_mode.cpp) ------------------------
    // A guarded int64 body plus its double wrapper (D64).
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
    // A local binding's C++ type, and whether int mode makes it int64_t.
    detail::CppInfo bindingCpp(const VariableDeclarator& d) const;
    bool intBinding(const detail::CppInfo& ci) const;
    void declareBinding(const VariableDeclarator& d);
    // A fresh binding; shadowing a deferred-check name fails the attempt.
    void intNewBinding(const std::string& name);
    // `name` is read only in `return`s, so its check may be deferred.
    bool intDeferable(const std::string& name) const;
    // The text of `a` for the identifier target `id` (checked or deferred).
    std::string intAssignText(const AssignmentExpr& a, const Identifier& id);
    // The value of a deferred assignment: `x +/- <small literal>`, or empty.
    std::string intAssignValue(const Expr& e);
    // An operand of a raw `+`/`-`: a deferred read stays unchecked.
    std::string intRawOperand(const Expr& e);

    // --- usage-based include flags (support/usage_includes.cpp) --------------
    // Usage-based family includes/helper blocks (D69/74, operator spellings).
    void applyUsageIncludes(const std::string& familyUse);

    // --- assembly (code_generator.cpp) ---------------------------------------
    std::string buildHeader(const std::string& decls) const;
    std::string buildSource(const std::string& decls, const std::string& defs,
                            const std::string& mainBody) const;
    // `main`: process argv, top-level await, event-loop drain.
    void emitMain(std::string& out, const std::string& mainBody) const;

    // Per-unit state lives in GeneratorState (generator_state.h).
};

}  // namespace cp3
