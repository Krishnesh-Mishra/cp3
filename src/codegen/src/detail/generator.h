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
//   emit/decl.cpp          forward decls, function definitions, declarations
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
#include <utility>
#include <vector>

#include "cp3/analysis.hpp"
#include "cp3/ast.hpp"
#include "cp3/codegen.hpp"
#include "cp3/diagnostic.hpp"
#include "detail/class_model.h"
#include "detail/enums.h"
#include "detail/generator_state.h"
#include "detail/listener.h"
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
    static void forOf(Generator& g, const ForOfStatement& f,
                      const std::string& jsLabel, std::string& out, int indent);
};
struct ProgramAssembly;  // detail/program_assembly.h (emit/program.cpp)
// The identifier a parameter binds directly, unwrapping a trailing `...rest`
// (emit/patterns.cpp); nullptr for a destructuring pattern.
const Identifier* parameterIdentifier(const Parameter& p);
struct CallBuilder;
struct PatternEmitter;
struct ModuleVarEmitter;
struct AssignEmitter;
struct PushReserveEmitter;  // emit/push_reserve.cpp
}  // namespace detail

// One destructuring source: the C++ text of the value being unpacked, its cp3
// type and whether the pattern's bindings are `const`.
struct PatternSource {
    std::string text;
    Type type;
    bool isConst = false;
};

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

    // The whole-program entry and its per-unit emission (emit/program.cpp).
    GeneratedModule runProgram(const ModuleGraph& graph,
                               const std::vector<CheckedModuleUnit>& units);
    void runUnit(const Program& program, std::string& decls, std::string& defs,
                 std::string& mainBody, std::string& globals);
    void emitProgramUnit(const detail::ProgramLink& link,
                         const CompilationUnit& unit,
                         detail::ProgramAssembly& assembly);

private:
    friend struct detail::ArrayEmitter;  // src/std/arrays/codegen_methods.cpp
    friend struct detail::PatternEmitter;  // emit/destructure.cpp
    friend struct detail::ModuleVarEmitter;  // emit/module_var.cpp
    friend struct detail::AssignEmitter;  // emit/assignment.cpp
    friend struct detail::PushReserveEmitter;  // emit/push_reserve.cpp
    friend struct detail::ListenerBuilder;  // detail/listener.h (emit/call.cpp)
    // --- diagnostics (code_generator.cpp) ------------------------------------
    void notImplemented(const Node& node, const std::string& what);
    // A hard type error at a use site: the form is known and known to be
    // wrong (an element type with no spelling, an element type that cannot
    // be inferred), so the diagnostic says why instead of skipping the
    // program. The CP3002 marker stays for the forms that are *unknown*.
    void reportTypeError(const Node& node, const std::string& message);
    // A codegen-stage diagnostic with its own code (not the CP3002 marker):
    // the construct is a program error, not a missing feature (a member
    // outside a documented surface, a value no C++ spelling can carry).
    void reportCodegenError(const Node& node, const std::string& code,
                             const std::string& message);
    // A real compile error (not the CP3002 "not built yet" marker): the
    // construct has no lowering at all, so the program must not compile.
    // Same dedup and int-path bail as `notImplemented`.
    void reportUnsupported(const Node& node, const char* code,
                           const std::string& message);

    // --- source mapping (support/line_map.cpp) --------------------------------
    // `#line <line> "<source>"` for `node` (DWARF/gdb); no-op without mapping.
    void emitLineDirective(const Node& node, std::string& out);
    // Pin each physical line of a raw structural chunk (int64 prologue).
    void emitMappedRaw(const Node& node, const std::string& raw, std::string& out);

    // --- user classes / interfaces / object shapes (emit/class_decl.cpp) ------
    // Models keyed as sema keys them; structs/methods land in the decl/def text.
    void collectModels(const Program& program);
    void collectClassNames(const Stmt& s);
    void collectInterfaces(const Program& program);
    void collectBodyModels(const Stmt& s);
    void collectClassMemberModels(ClassModel& cm);
    void collectClassMethodModel(ClassModel& cm, const MethodDeclaration& md);
    void collectClassAccessorModel(ClassModel& cm, const ClassMember& mp);
    void emitTopLevelClass(const ClassDeclaration& c);
    void emitAllClasses();
    void emitLocalClass(const ClassDeclaration& c, std::string& out, int indent);
    void emitClassStruct(const ClassModel& cm, std::string& out, int indent,
                         std::string* defs);
    void emitClassMember(const ClassModel& cm, const ClassMemberModel& m,
                         std::string& out, int indent, std::string* defs);
    void emitClassMethodMember(const ClassModel& cm, const ClassMemberModel& m,
                                std::string& out, int indent, std::string* defs);
    void emitClassAccessorMember(const ClassModel& cm,
                                 const ClassMemberModel& m, std::string& out,
                                 int indent, std::string* defs);
    std::string memberFieldText(const ClassMemberModel& m);
    std::string methodReturnType(const MethodDeclaration& md,
                                 const ClassModel* cm = nullptr,
                                 const std::vector<std::string>& methodTypeParams = {});
    std::string methodParamText(const Parameter& p,
                                const ClassModel* cm = nullptr,
                                const std::vector<std::string>& methodTypeParams = {});
    std::string accessorReturnType(const AccessorDeclaration& ad,
                                   const ClassModel* cm = nullptr);
    std::string accessorBodyText(const AccessorDeclaration& ad,
                                 const ClassModel& cm, int indent);
    std::string methodBodyText(const MethodDeclaration& md, const ClassModel& cm,
                               int indent,
                               const std::vector<std::string>& methodTypeParams = {});
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
    // The constructor's arguments for a class literal.
    std::string ctorArgsText(const ClassMemberModel& ctor, const ClassModel& decl,
                             const ObjectLiteral& lit);
    // `new C(...args)`: the spread array's elements are the constructor's
    // arguments.
    std::string newSpreadText(const ClassModel& cm, const std::string& self,
                               const ExprList& arguments, std::size_t spreadAt);
    std::string detachedMethodLambda(
        const ClassModel& cm, const ClassMemberModel& m, const std::string& recv);
    std::string objectFieldDefaultText(const Type& t);
    // One field store (weak/optional/compound); "" when the caller must report.
    std::string storeText(const ClassMemberModel& m, const std::string& lvalue,
                          const AssignmentExpr& a, const std::string& value);

    // --- declarations (emit_decl.cpp) ----------------------------------------
    void collectDeclaredNames(const Stmt& s);
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
    // The C++ spelling of a parameter's own type ("" when it has none: an
    // untyped parameter is spelled as a template parameter instead).
    std::string parameterCppType(const Parameter& p,
                                 const FunctionDeclaration& fd,
                                 std::size_t index);
    // The template parameter carrying an untyped parameter's type.
    std::string untypedParamTemplate(std::size_t index) const;
    std::string parameterText(const Parameter& p, const FunctionDeclaration& fd,
                              std::size_t index, bool includeDefault);
    std::string functionReturnType(const FunctionDeclaration& fd);
    // The cp3 type of the value a function returns (annotation, sema, then the
    // first `return`): it fixes `expected_` and the literal shape.
    std::optional<Type> returnValueType(const FunctionDeclaration& fd) const;
    Type asyncValueType(const FunctionDeclaration& fd) const;
    const Expr* firstReturnedExpr(const Stmt& s) const;

    // --- declaration-type environment + type resolution (type_resolve.cpp) ---
    void pushScope();
    void popScope();
    // `cell`: shared `Cell<T>`; `global`: module-scope (reference via moduleRef).
    void declare(const std::string& name, const std::optional<Type>& type,
                 bool cell = false, bool global = false);
    std::optional<detail::Binding> lookup(const std::string& name) const;
    // The C++ spelling of a module-scope entity, qualified with its namespace.
    std::string moduleRef(const std::string& name) const;
    // True for a value binding; false for a class/interface name.
    bool hasValueBinding(const std::string& name) const;
    std::optional<Type> bindingType(const VariableDeclarator& d) const;
    std::optional<Type> resolvedBindingType(const VariableDeclarator& d) const;
    // An annotation -> Type, with enum-named Class parts rewritten to Enum.
    std::optional<Type> typeOfNode(const TypeNode& tn) const;
    void rewriteEnumTypes(Type& t) const;
    Type effectiveType(const Expr& e) const;
    // The value of a temporary `Promise.resolve(v)` over a non-promise, or
    // nullptr when the awaited expression is not that shape.
    const Expr* temporaryResolveValue(const Expr* argument) const;
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
    std::string emitMemberRead(const MemberExpr& m);
    std::string emitTemplate(const TemplateLiteral& tl);
    std::string tsCall(const Expr& e);
    std::string cond(const Expr& e);
    // A whole array in a formatter/truthiness test pulls the arr family (D69).
    void noteFamilyUse(const Expr& e);
    std::string emitBinary(const BinaryExpr& b);
    // The `in` operator: a Map/Set membership test, an index-in-range test
    // over an array or a typed view, a field test over a fixed-shape object.
    std::string emitInOperator(const BinaryExpr& b);
    std::string emitInObject(const BinaryExpr& b, const Type& container);
    // A construction: the class / worker / network / collections / emitter /
    // family dispatch.
    std::string emitNewExprValue(const NewExpr& n);
    // Native `%`, bitwise and shift lowering (nullopt keeps the number path).
    std::optional<std::string> nativeBinaryText(const BinaryExpr& b,
                                                const detail::CppInfo& L,
                                                const detail::CppInfo& R,
                                                const std::string& lhs,
                                                const std::string& rhs);
    std::string emitEquality(const BinaryExpr& b, const detail::CppInfo& L,
                             const detail::CppInfo& R, const std::string& lhs,
                             const std::string& rhs);
    std::string unionEqualityText(const BinaryExpr& b, const detail::CppInfo& L,
                                  const detail::CppInfo& R, const std::string& lhs,
                                  const std::string& rhs);
    std::string emitLogical(const LogicalExpr& l);
    std::string emitNullish(const LogicalExpr& l);
    std::string emitUnary(const UnaryExpr& u);
    std::string emitTypeof(const UnaryExpr& u);
    // The answer of a nullish option (the empty slot and the held value).
    std::string emitTypeofOption(const UnaryExpr& u, const detail::CppInfo& ci);
    std::string emitUpdate(const UpdateExpr& u);
    // An update of a target that is not a name: a property or an element.
    std::string emitUpdateTarget(const UpdateExpr& u);
    std::string emitUpdateMember(const UpdateExpr& u, const MemberExpr& m);
    std::string emitUpdateIndex(const UpdateExpr& u, const IndexExpr& ix);
    // The C++ lvalue of a writable member (a field of a value shape or of a
    // class) plus the field's cp3 type.
    bool memberFieldTarget(const MemberExpr& m, std::string& lvalue,
                           Type& fieldType);
    // The expression forms the ordinary table does not own: the weak-link
    // wrapper (`{ weak: value }`, spec §11), a range used as a value
    // (`0..5`) and a spread outside its legal position.
    std::string emitExtendedExpr(const Expr& e);
    std::string emitWeakValue(const WeakExpr& w);
    std::string emitRangeValue(const RangeExpr& r);
    std::string emitConditional(const ConditionalExpr& c);
    std::string emitAssignment(const AssignmentExpr& a);

    // --- calls (emit/call.cpp) -----------------------------------------------
    std::string emitCall(const CallExpr& c);
    // A call to an imported binding of a whole-program build: the linked
    // qualified C++ name ("" when the name is not linked, so the caller keeps
    // its ambient lowering); a link miss reports CP3002 at the use site.
    std::string emitLinkedCall(const CallExpr& c, const std::string& name);
    // Linked-call helpers (emit/call_args.cpp): the member miss at the use
    // site, and the identifier-callee chain (imports, builtins, user
    // functions).
    std::string linkedMemberMiss(const MemberExpr& m) const;
    // The documented global numeric parsers (`parseInt`/`parseFloat`):
    // argument coercion plus the runtime helper (STDLIB section 3).
    std::string emitParseBuiltin(const CallExpr& c, const std::string& name);
    // Argument lowering and calls through a function value (emit/call_args.cpp).
    friend struct detail::CallBuilder;
    bool isConsoleMember(const CallExpr& c) const;

    // --- native interop (emit/native_call.cpp) --------------------------------
    // The native bindings this unit imports, keyed by their local alias, and
    // the headers the generated .cpp must include for them.
    void collectNativeImports(const Program& program);
    // One imported binding: the alias map, the headers and the enums.
    void collectNativeBinding(const NativeBinding& binding,
                              const ImportDeclaration& imp);
    // The binding's enums join the enum table (the C++ enum class itself
    // comes from the native library).
    void collectNativeEnums(const NativeModule& module);
    // The C++ arguments of one native call (a buffer or number array
    // parameter expands to a pointer and a length).
    std::vector<std::string> nativeArgTexts(const CallExpr& c,
                                            const NativeFunction& fn,
                                            const std::vector<std::string>& temporaries);
    // The name of the temporary a twice-read argument is bound to.
    std::string nativeTempName(std::size_t index);
    // The `const unsigned char*` a byte-buffer parameter takes.
    std::string nativeBytesPointer(const std::string& text);
    // The cp3 spelling of a native result (a number is a double, a
    // `const char*` result the empty string for a null pointer).
    std::string nativeResultText(const NativeFunction& fn,
                                  const std::string& call);
    // `alias.f(...)` -> the recorded C++ call; "" when it is not one.
    std::string emitNativeCall(const CallExpr& c, const MemberExpr& m);
    // `dm.Color` / `dm.Color.Red` -> the enum's C++ name / member.
    std::string emitNativeMember(const MemberExpr& m);

    // --- generic functions + tuples (support/generic.cpp) ---------------------
    // Real C++ function templates: the `template <typename T, ...>` prefix
    // from the declared type parameters, the callee spelling with any explicit
    // call-site type arguments (`identity<number>(...)`), the tuple lowering
    // (`std::make_tuple`, `std::get<N>`) and the object-literal argument
    // reification (`getX({ x: 5 })` -> a struct named after the literal).
    std::string templatePrefix(const FunctionDeclaration& fd);
    std::string calleeText(const std::string& name, const CallExpr& c);
    std::string typeArgText(const TypeNode& tn);
    // The explicit type arguments of a generic call, resolved to cp3 types,
    // and the substitution of the callee's type parameters by them.
    std::vector<Type> resolvedTypeArgs(const CallExpr& c) const;
    Type substituteTypeArgs(const Type& t, const FunctionDeclaration& fd,
                            const CallExpr& c) const;
    Type substituteTypeParams(const Type& t, const FunctionDeclaration& fd,
                              const std::vector<Type>& args) const;
    std::string reifyObjectArg(const ObjectLiteral& lit);
    std::string emitTupleLiteral(const ArrayLiteral& lit);
    std::string emitGenericIndexRead(const IndexExpr& ix);

    // --- parallel.* / worker(...) (emit/parallel.cpp, emit/worker.cpp) --------
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
    // A message listener's destructuring parameter: the payload arrives whole
    // and the pattern binds its fields at the top of the body.
    void workerListenerPattern(const Parameter& p, const Type& payload,
                               const std::string& src, std::string& body);
    // The worker side: lowers a worker unit's full module (imports, functions,
    // enums, `thread_local` module vars, and its default-export arrow as
    // `void cp3_default(ThreadHandle<In, Out>& thread)`).
    void emitWorkerTopLevel(const Stmt& s, std::string& decls,
                            std::string& defs, std::string& prologue);
    void emitWorkerModuleVar(const VariableDeclaration& vd, std::string& defs,
                             std::string& prologue);
    void emitWorkerUnitBody(const Program& program, std::string& decls,
                             std::string& defs, std::string& prologue);
    bool resolveWorkerUnitEntry(const Program& program,
                                detail::WorkerUnitEntry& out);
    GeneratedModule runWorkerModule(const Program& program);

    // --- std/console lowering (src/std/console/codegen.cpp) ------------------
    // Each member lowers onto cp3/std/console.hpp; an object-literal argument
    // gets the explicit D49 object form.
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
    // The parts form of a console call that carries a spread argument.
    std::string consolePartsText(const CallExpr& c, std::size_t from);

    // --- the builtin EventEmitter (D74, cp3/async/emitter.hpp) ----------------
    // Construction, the boxed-listener value form and the on/once/off/emit
    // calls; each returns "" for a form it does not own.
    std::string emitEventEmitterNew(const NewExpr& n);
    // A function value in a generic expression position (spec 10): boxed as a
    // Listener when the context wants one, else a `std::function`.
    std::string emitArrowValue(const Expr& e, const ArrowFunction& a);
    std::string emitListenerValue(const Expr& e);
    std::string emitEventEmitterCall(const CallExpr& c, const MemberExpr& m);

    // --- the JS-compat typed-array family (src/std/typed-arrays/codegen.cpp) ---
    // Constructors, statics, method tables, indexed access and the declarator
    // forms whose element type comes from the binding; "" outside the family.
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
    // The diagnostic for an element type an array cannot hold: the type is
    // unknown (nothing says what the elements are) or it has no C++ spelling.
    void reportArrayElementType(const Type& elem, const Node& site,
                                const std::string& what);
    std::string typedCtorText(const std::string& cls, const ExprList& args, const Node& site);
    // `new Int8Array([1, 2])` / `new Int8Array([...a, 1])`: the element list
    // becomes the view's values.
    std::string typedCtorLiteralText(const std::string& t,
                                     const ArrayLiteral& lit);
    std::string arrayStaticCallText(const CallExpr& c, const std::string& prop);
    std::string arrayFillCallText(const CallExpr& c, const MemberExpr& m, const Type& recv);
    std::string arrayJoinCallText(const CallExpr& c, const MemberExpr& m, const Type& recv);
    std::string arraySortCallText(const CallExpr& c, const MemberExpr& m, const Type& recv);
    std::string arrayIncludesCallText(const CallExpr& c, const MemberExpr& m, const Type& recv);
    std::string typedMethodCallText(const CallExpr& c, const MemberExpr& m, const char* tag);
    std::string dataViewCallText(const CallExpr& c, const MemberExpr& m);
    std::string arrayLiteralText(const ArrayLiteral& lit, const Type& elemOverride);
    // The tuple declarator's initializer: src/codegen/src/support/generic.cpp.
    std::string tupleDeclaratorInitText(const VariableDeclarator& d,
                                         const Type& declared);
    std::string declaratorInitText(const VariableDeclarator& d, const Type& declared);

    // --- object value shapes (emit/objects.cpp) -------------------------------
    std::string emitObject(const Expr& e);         // `o.a` / `o?.a` reads
    std::string emitObjectLiteral(const Expr& e);  // `{...}` literal values

    // --- binding patterns + records (emit/patterns.cpp) -----------------------
    // An existing record spelling: a `$o{...}` value struct or a materialized
    // std/json interface record; false when neither represents the shape.
    bool recordLayout(const Type& t, std::string& structName, std::vector<std::string>& fieldNames);
    // A literal built for a known record type, including spread properties.
    std::string recordLiteralText(const ObjectLiteral& lit, const Type& expected);
    // A literal filling a structural object model's struct.
    std::string recordModelText(const ObjectLiteral& lit, const ObjectModel& om);
    // The statements that copy a spread source's fields into a record.
    bool spreadCopyText(const Expr& argument,
                        const std::vector<std::string>& targetFields,
                        const std::string& outName, const std::string& outAccess,
                        std::string& out);
    // The excess-property diagnostic for an object literal.
    void reportExcessProperty(const Node& site, const std::string& name);
    bool emitDestructureDecl(const VariableDeclarator& d, VarKind kind,
                             std::string& out, int indent);
    void emitPatternParams(const FunctionDeclaration& fd, std::string& out, int indent);
    void unpackPattern(const Node& pattern, const PatternSource& src, std::string& out, int indent);
    void unpackArrayPattern(const ArrayPattern& p, const PatternSource& src,
                            std::string& out, int indent);
    void unpackObjectPattern(const ObjectPattern& p, const PatternSource& src,
                             std::string& out, int indent);
    void bindPatternTarget(const Node& target, const PatternSource& value,
                           std::string& out, int indent);
    // A destructuring *assignment* (`[a, b] = expr`, `{ x } = expr`): the
    // right side is evaluated once and every target of the pattern is
    // written. The expression's value is the right side (JS).
    std::string emitDestructuringAssign(const AssignmentExpr& a);
    // One target of a destructuring assignment: an identifier is written, a
    // nested array/object literal is unpacked in turn. `access` is the C++
    // expression that reads the value. False when the target has a form the
    // write does not cover.
    bool assignDestructuringTarget(const Node& target, const std::string& access,
                                    const Type& source, std::string& body);
    // The object-literal half of a destructuring assignment.
    bool assignDestructuringFields(const ObjectLiteral& lit,
                                    const std::string& access, const Type& source,
                                    std::string& body);
    // The array-literal half of a destructuring assignment.
    bool assignDestructuringElements(const ArrayLiteral& lit,
                                     const std::string& access, const Type& source,
                                     std::string& body);

    // A structural object's field write (`o.a = v`); "" when not one.
    std::string emitMemberAssign(const AssignmentExpr& a, const MemberExpr& m);

    // --- closures (emit/destructure.cpp) --------------------------------------
    // Is `name` a shared `Cell<T>` of the frame? (reads/writes use get/set)
    bool isCell(const std::string& name) const;
    // --- std/collections (src/std/collections/codegen.cpp) --------------------
    // `new Map<K, V>()` / `new Set<T>()` and the member calls; "" otherwise.
    std::string emitCollectionsNew(const NewExpr& n);
    std::string emitCollectionsMemberCall(const CallExpr& c, const MemberExpr& m);
    std::string emitCollectionsMember(const MemberExpr& m);
    // WS2.8: `const v = m.get(k) ?? d;` immediately followed by
    // `m.set(k, value)` lowers to one probe plus an in-place store when the
    // pure-read proof holds; consumes both statements and returns true.
    bool emitFusedMapUpdate(const StmtList& body, std::size_t& index,
                            std::string& out, int indent);

    // --- std namespaces + primitive methods (src/std/*/codegen.cpp) ----------
    // `Math.*`/`path.*`/`os.*`/`JSON.stringify`/`process.*`, primitive methods
    // and builtin reads; "" for an unimplemented form (the caller reports).
    std::string emitBuiltinCall(const CallExpr& c, const MemberExpr& m);
    std::string emitBuiltinMember(const MemberExpr& m);
    std::string emitMathCall(const CallExpr& c, const MemberExpr& m);
    std::string emitMathMember(const MemberExpr& m);
    std::string emitPathCall(const CallExpr& c, const MemberExpr& m);
    std::string emitPathMember(const MemberExpr& m);
    std::string emitOsCall(const CallExpr& c, const MemberExpr& m);
    std::string emitJsonCall(const CallExpr& c, const MemberExpr& m);
    // The JSON record surface (STDLIB §04 / language spec §23): interfaces are
    // registered up front; a record is reified only when `JSON.parse<T>` names
    // it, so a registered declaration emits nothing by itself.
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
    // `String(value)`: the scalar conversion global (emit/call.cpp).
    std::string emitStringBuiltin(const CallExpr& c);
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
    // The argument list of a class method / constructor call: a rest parameter
    // takes the trailing arguments as one array (spec 15).
    std::vector<std::string> classRestArgs(const std::vector<std::string>& args,
                                           const ParamList* params);
    // The argument list of a class method call: a rest parameter takes the
    // trailing arguments as one array (spec 15).
    std::vector<std::string> classMethodArgs(const CallExpr& c,
                                             const ClassMemberModel& mem);

    // --- async / promises / errors (emit_async.cpp) --------------------------
    std::string emitAwait(const AwaitExpr& a);
    std::string emitArrow(const ArrowFunction& a,
                          const std::vector<std::string>& contextParams,
                          bool byValue = false);
    std::vector<std::string> arrowParamText(
        const ArrowFunction& a, const std::vector<std::string>& contextParams,
        const std::optional<Type>& expectedFn);
    // A rest parameter's check: it must be the last parameter and its element
    // type must be known.
    void checkRestParam(const Parameter& p, bool last,
                        std::optional<Type>& type);
    std::optional<Type> arrowReturnCpType(
        const ArrowFunction& a, const std::optional<Type>& expectedFn);
    std::string arrowReturnType(const ArrowFunction& a,
                                const std::optional<Type>& expectedFn);
    // --- closures (emit/closures.cpp) ----------------------------------------
    // The cp3 type of an arrow's parameter (annotation, sema, then context).
    std::optional<Type> arrowParamType(const ArrowFunction& a,
                                       std::size_t index,
                                       const std::optional<Type>& expectedFn);
    // The arrow's body statements and its C++ signature (the out-parameter).
    std::string arrowBodyText(const ArrowFunction& a, bool isAsync,
                              const std::optional<Type>& expectedFn,
                              std::string& signature);
    std::string emitTimerBuiltin(const CallExpr& c, const std::string& name);
    // The delay a timer call's spread argument supplies.
    std::string timerSpreadDelay(const SpreadElement& sp);
    std::string emitPromiseMemberCall(const CallExpr& c, const MemberExpr& m);
    std::string emitPromiseStatic(const CallExpr& c, const MemberExpr& m);
    std::string emitPromiseResolve(const CallExpr& c);
    std::string emitPromiseReject(const CallExpr& c);
    // The Promise combinators (`Promise.all` / `race` / `allSettled`): the
    // operand is an array of promises (or of plain values, which are already
    // resolved), in emit/async_promise.cpp.
    std::string emitPromiseCombinator(const CallExpr& c, const std::string& prop);

    // --- spread arguments / spread elements (emit/spread.cpp) ----------------
    // The C++ text of a spread source's first element (`f(...values)` in a
    // one-parameter call); "" when the source is not an array with a known
    // element type. `wantCpp` is the spelling the element must have.
    std::string spreadFirstText(const SpreadElement& sp,
                                const std::string& wantCpp);
    // The C++ text of a spread source that is an array whose elements have the
    // wanted spelling; "" when the source is not such an array.
    std::string spreadArrayText(const SpreadElement& sp,
                                 const std::string& wantCpp);
    // The C++ text of a runtime-built `std::vector<E>`: the parts in order,
    // each either an element expression or a spread of an array.
    std::string spreadListText(const Type& elemType, const ExprList& parts);

    // --- statements (emit/stmt.cpp) ------------------------------------------
    void stmt(const Stmt& s, std::string& out, int indent);
    void emitBranch(const Stmt& s, std::string& out, int indent);
    void emitIf(const IfStatement& is, std::string& out, int indent);
    void emitWhile(const WhileStatement& ws, std::string& out, int indent);
    void emitDoWhile(const DoWhileStatement& dw, std::string& out, int indent);
    void emitVarDecl(const VariableDeclaration& vd, std::string& out, int indent);
    std::string declaratorText(const VariableDeclarator& d, VarKind kind);
    // The const-reference spelling for a proven array-read binding, or "" when
    // the declarator was not proven (support/array_refs.cpp).
    std::string arrayRefDeclText(const VariableDeclarator& d,
                                 const std::string& name);
    void emitPushReserveHint(const WhileStatement& ws, std::string& out,
                             int indent);
    void emitReturn(const ReturnStatement& r, std::string& out, int indent);
    void emitThrow(const ThrowStatement& t, std::string& out, int indent);
    void emitTry(const TryStatement& ts, std::string& out, int indent);

    // --- control flow (emit/control.cpp) -------------------------------------
    // Generated labels plus `goto` lower break/continue and JS labels.
    std::string freshLabel(const char* tag);
    void pushFlowFrame(bool isLoop, const std::string& breakLabel,
                       const std::string& continueLabel, const std::string& jsLabel);
    void popFlowFrame();
    std::string emitWhile(const WhileStatement& w, const std::string& jsLabel,
                          std::string& out, int indent);
    std::string emitDoWhile(const DoWhileStatement& d, const std::string& jsLabel,
                            std::string& out, int indent);
    std::string emitFor(const ForStatement& f, const std::string& jsLabel,
                        std::string& out, int indent);
    void emitForOf(const ForOfStatement& f, const std::string& jsLabel,
                   std::string& out, int indent);
    void emitForIn(const ForInStatement& f, const std::string& jsLabel,
                   std::string& out, int indent);
    std::string emitSwitch(const SwitchStatement& s, const std::string& jsLabel,
                           std::string& out, int indent);
    void emitLabeled(const LabeledStatement& l, std::string& out, int indent);
    void emitBreak(const BreakStatement& b, std::string& out, int indent);
    void emitContinue(const ContinueStatement& c, std::string& out, int indent);
    bool emitControlDispatch(const Stmt& s, std::string& out, int indent);

    // --- enums (emit/enum.cpp) ------------------------------------------------
    // Top-level `enum class` declarations (spec 04), member reads, enum-carrying
    // literals and the same-enum switch lowering.
    void collectEnums(const Program& program);
    std::string emitEnumMember(const MemberExpr& m);
    std::string emitEnumObjectLiteral(const ObjectLiteral& ol);
    bool lowerEnumSwitch(const SwitchStatement& sw, std::string& out, int indent);

    // --- array read references (support/array_refs.cpp) ----------------------
    // Mark every declarator whose `arr[i]` read is proven safe to bind by
    // const reference for the whole unit, or mark none when the proof cannot
    // see the whole unit.
    void analyzeArrayRefs(const Program& program);

    // --- int64 fast path + `%` lowering (int_mode.cpp) ------------------------
    // A guarded int64 body plus its double wrapper (D64).
    // `analyzeTopInt64` (support/top_int64.cpp) proves the two entry-body shapes
    // that stay integral for their whole life and records them for the
    // statement/expression emitters.
    void analyzeTopInt64(const Program& program);
    bool moduleUsesBeforeDecl(const Program& program) const;
    void decideModuleScope(const Program& program);
    bool intEligible(const FunctionDeclaration& fd, const std::string& ret) const;
    std::string intBody(const FunctionDeclaration& fd, bool& kept);
    std::string intSignature(const FunctionDeclaration& fd) const;
    std::string intWrapperPrologue(const FunctionDeclaration& fd) const;
    bool intOn() const;
    void intFail();
    // `name` resolves to a plain frame-local binding (not a module global and
    // not a shared closure cell).
    bool intLocal(const std::string& name) const;
    bool numberTyped(const Expr& e) const;
    std::string intNumberExpr(const Expr& e);
    std::string intLiteral(const Expr& e);
    std::string intIdentifier(const Identifier& id);
    std::string intBinary(const BinaryExpr& b);
    // `x % k == 0` in force on the current branch path -> a proven divisor.
    std::optional<std::pair<std::string, std::int64_t>> divisibleGuard(
        const Expr& e);
    // An observable effect or a `ref` argument discards the fast attempt.
    bool intEffectful(const Expr& e) const;
    // The guard every expression of the fast body passes.
    void intGuardExpr(const Expr& e);
    // The read text of an entry-body int64 binding (empty when it is none).
    std::string int64ReadText(const std::string& name);
    // The declaration text of an entry-body int64 binding (empty when none).
    std::string int64DeclaratorText(const VariableDeclarator& d);
    bool factStableBranch(const Stmt& branch) const;  // straight-line only
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
    // `name` is read only in `return`s, so its check may be deferred.
    bool intDeferable(const std::string& name) const;
    // The text of `a` for the identifier target `id` (checked or deferred).
    std::string intAssignText(const AssignmentExpr& a, const Identifier& id);
    std::string intAssignValue(const Expr& e);  // `x +/- <small literal>`
    std::string intRawOperand(const Expr& e);   // a raw `+`/`-` operand

    // --- usage-based include flags (support/usage_includes.cpp) --------------
    // Usage-based family includes/helper blocks (D69/74, operator spellings).
    void applyUsageIncludes(const std::string& familyUse);

    // --- assembly (code_generator.cpp) ---------------------------------------
    std::string buildHeader(const std::string& decls,
                            const std::string& globals) const;
    std::string sourceIncludes() const;
    std::string buildSource(const std::string& decls, const std::string& defs,
                            const std::string& globals,
                            const std::string& mainBody) const;
    // `main`: process argv, top-level await, event-loop drain.
    void emitMain(std::string& out, const std::string& mainBody) const;

    // Per-unit state lives in GeneratorState (generator_state.h).
};

}  // namespace cp3
