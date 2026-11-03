// =============================================================================
// src/sema/src/detail/analyzer.h - PRIVATE declaration of the sema implementation.
//
// Declares `Analyzer`, the class that owns all sema state and does the actual
// name resolution + type checking. The frozen public boundary stays in
// `cp3/sema.hpp` (class `SemanticAnalyzer`), which is a thin shell that builds
// an `Analyzer` and runs it.
//
// Method definitions are grouped by concern across the module's translation
// units (STYLE.md §8):
//
//   semantic_analyzer.cpp   entry + top-level orchestration
//   support/scopes.cpp      scope/declaration tables, builtins, hoisting
//   support/declarations.cpp declaration/publishing passes (vars, patterns, sigs)
//   support/symbols.cpp     SymbolInfo/FunctionInfo/nodeTypes publishing
//   checks/type_check.cpp   type resolution, widening, shapes
//   checks/assignability.cpp assignability + object-diff diagnostics
//   checks/expr_check.cpp   primary expression inference
//   checks/member_check.cpp member/index/call inference
//   checks/call_check.cpp   call inference + overload resolution
//   checks/generic_check.cpp generic function substitution/inference
//   checks/ops_check.cpp    operator expression inference
//   checks/stmt_check.cpp   statements, control flow, return analysis
//   checks/banned.cpp       banned-pattern diagnostics (delete/arguments/for-in/any)
//   checks/std_check.cpp    std-namespace dispatch + `String(x)`
//   checks/enum_check.cpp   the enum declaration model (spec 04)
//   support/docs.cpp        the doc-comment reader (JSDoc-style comments)
//   src/std/<family>/sema.cpp per-family std member tables (math / path / os /
//                           process / console / JSON)
//   src/std/typed-arrays/sema.cpp the JS-compat typed-array family (D69)
//   checks/parallel_check.cpp      parallel.* capture safety: statements + hook
//   checks/parallel_expr_check.cpp parallel.* capture safety: expressions + scopes
//
// This header is implementation detail: it lives under `src/sema/src/detail/` and is
// never included by another module (STYLE.md §3).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/diag/diagnostic.hpp"
#include "cp3/host/native_bindings.hpp"
#include "cp3/host/options.hpp"
#include "cp3/sema.hpp"
#include "cp3/std_surface.hpp"
#include "detail/enums.h"
#include "detail/scopes.h"

namespace cp3 {

// One argument of a native call: the parameter it is checked against, the
// argument's inferred type, its 1-based position and where it was written.
struct NativeArgSite {
    const NativeParam* param = nullptr;
    Type type;
    std::size_t position = 0;
    SourceSpan span{};
};

// The sema implementation. One instance per `SemanticAnalyzer::analyze` call;
// it owns the scope stack, the private shape/signature tables and the
// references to the sink + result it publishes into.
class Analyzer {
public:
    Analyzer(DiagnosticSink& sink, SemaResult& out,
             const CompileOptions& options = CompileOptions{},
             const ImportedBindings* imports = nullptr)
        : sink_(sink), out_(out), options_(options), imports_(imports) {
        shapes_[kAmbientLabel] = Shape{};
        shapes_[kAmbientLabel].open = true;
    }
    // Top-level orchestration: builtins + program body (semantic_analyzer.cpp).
    void run(Program& program);

private:
    // --- diagnostics ----------------------------------------------------------
    // Anchors diagnostics to column 1 of the offending node's begin line (D2).
    SourceSpan anchorSpan(const SourceSpan& s) const;
    void report(const char* code, const SourceSpan& span, std::string message);
    // Like report(), plus `help:` entries for the pretty renderer.
    void reportSuggestive(const char* code, const SourceSpan& span,
                          std::string message,
                          std::vector<std::string> suggestions);
    // CP3301 for an unresolved name, with suggestions.
    void reportUndefinedName(const SourceSpan& span, const std::string& name);
    // CP3305 for a miss on a known receiver.
    void reportMemberMiss(const std::string& receiver, const std::string& prop,
                          const SourceSpan& span,
                          const std::vector<std::string>& candidates);
    // Every name bound in the current scope stack (suggestion candidates).
    std::vector<std::string> visibleNames() const;
    // --- banned patterns (banned.cpp) ----------------------------------------
    void checkBannedAny(const SourceSpan& span);
    void reportBannedArguments(const SourceSpan& span);
    void checkBannedDelete(const SourceSpan& span);
    // Reports CP3805 when a `for...in` target is a fixed-shape plain object.
    void checkBannedForIn(const Type& right, const SourceSpan& span);

    // --- scopes + hoisting (scopes.cpp) --------------------------------------
    void pushScope();
    void popScope();
    bool lookup(const std::string& name, Binding& out) const;
    // Declares a binding in the current scope; reports CP3303 on a duplicate.
    void declareBinding(const std::string& name, const Type& type,
                        const SourceSpan& span, const BindingSpec& spec = {});
    void declareAmbient(const std::string& name, const SourceSpan& span,
                        std::uint32_t nodeId);
    // Declares a resolved cross-module import with its exported type/signature.
    void declareImported(const std::string& name, const ImportedBinding& binding,
                         const SourceSpan& span, std::uint32_t nodeId);
    // The exported name `local` is bound to, resolving a named std-import alias
    // (`import { Map as M } from "std/collections"` -> M resolves to "Map").
    // Any other spelling resolves to itself.
    std::string importedName(const std::string& local) const;
    void overrideBinding(const std::string& name, const Type& type,
                         const SourceSpan& span);
    void declareBuiltins();
    void hoistType(Stmt& s);
    void hoistFunction(FunctionDeclaration& fn);
    void hoistVars(StmtList& body);
    void hoistVarsIn(Stmt& s);
    void hoistVarBody(StmtPtr& body);
    void registerTypeParams(TypeParamList& typeParams);
    // Generic class scopes: save/restore type parameter names.
    using TypeParamScope =
        std::vector<std::pair<std::string, std::optional<Type>>>;
    void enterClassTypeParams(const TypeParamList& typeParams,
                              TypeParamScope& saved);
    void leaveClassTypeParams(const TypeParamScope& saved);
    // Substitute instantiation arguments for class type parameters.
    Type substituteClassTypeParams(const Type& t, const ClassInfo& ci,
                                   const std::vector<Type>& args) const;
    // --- declarations (declarations.cpp) -------------------------------------
    FuncSig buildSig(const std::string& name, ParamList& params, TypePtr& retType,
                     const SourceSpan* spanForTypeParams);
    void declareVariableDeclaration(VariableDeclaration& vd);
    void declareDeclarator(VariableDeclarator& d, VarKind kind);
    bool isMixedArray(const Type& t) const;
    void checkArrayLiteral(ArrayLiteral& lit, const Type& target);
    void declarePattern(Node* id, const Type& type, bool isConst,
                        std::uint32_t declNodeId, bool isVar = false);
    void declareRestPattern(RestElement& re, const Type& type, bool isConst,
                            std::uint32_t declNodeId);
    void declareArrayPattern(ArrayPattern& ap, const Type& type, bool isConst,
                             std::uint32_t declNodeId);
    void declareObjectPattern(ObjectPattern& op, const Type& type, bool isConst,
                              std::uint32_t declNodeId);
    void declareForBinding(Node* left, const Type& elemType);
    void analyzeImport(ImportDeclaration& d);
    void registerInterface(InterfaceDeclaration& i);
    // A generic interface's members and type parameters (D26): every use
    // substitutes its type arguments into the members.
    struct GenericInterface {
        std::vector<Field> fields;
        bool open = false;
        std::vector<std::string> typeParams;
    };
    // A type alias declares a compile-time name for a type (declarations.cpp).
    void declareTypeAlias(TypeAliasDeclaration& t);
    // The aliased type of `name`, with `args` substituted for the alias's own
    // type parameters.
    Type resolveTypeAlias(const std::string& name,
                          const std::vector<Type>& args) const;
    // The type `t` with the type arguments `args` substituted for the type
    // parameters `names` (the parameter at the same position).
    Type substituteTypeParams(const Type& t,
                              const std::vector<std::string>& names,
                              const std::vector<Type>& args) const;
    // The instantiated shape of a generic interface (D26).
    Type resolveGenericInterface(const std::string& name,
                                 const std::vector<Type>& args);
    // The primitive a type name spells; false when the name is not one.
    bool primitiveTypeOf(const std::string& name, Type& out);
    // Enum declaration model (enum_check.cpp).
    void registerEnum(EnumDeclaration& e);

    // --- classes (class_check.cpp) -------------------------------------------
    // Resolve class declarations into member tables before the body pass.
    void resolveClasses(StmtList& body);
    void resolveClass(ClassDeclaration& c);
    void collectClassMembers(const ClassDeclaration& c, ClassInfo& info);
    void collectClassMember(ClassMember& m, ClassInfo& info);
    void collectClassProperty(PropertyDeclaration& p, ClassInfo& info);
    void collectClassMethod(MethodDeclaration& md, ClassInfo& info);
    const ClassInfo* findClass(const std::string& name) const;
    // The nearest class in `ci`'s base chain that declares a constructor
    // (including `ci` itself), or nullptr when no class in the chain has one.
    // A derived class without its own constructor inherits this signature.
    const ClassInfo* constructorOwner(const ClassInfo& ci) const;
    const ClassMemberInfo* findClassMember(const ClassInfo& ci,
                                           const std::string& name) const;
    const ClassMemberInfo* findClassMemberChain(
        const ClassInfo& ci, const std::string& name, const ClassInfo** owner) const;
    bool classIsOrDerivesFrom(const ClassInfo& ci, const std::string& base) const;
    bool classAccessAllowed(const ClassInfo& owner, const ClassMemberInfo& m) const;
    bool memberImplementedInClass(const ClassInfo& ci,
                                  const std::string& name) const;
    Type classMemberType(const ClassMemberInfo& m) const;
    // Member access on a user-class instance: privacy/weak/CP3305/CP3802 (15).
    Type userClassMember(const Type& cls, const std::string& prop,
                         const SourceSpan& span);
    void checkAbstractConformance(const ClassDeclaration& c,
                                  const ClassInfo& ci);
    void checkClassHierarchy(const ClassDeclaration& c, const ClassInfo& ci);
    void checkClassBodies(ClassDeclaration& c);
    void checkClassMemberBody(ClassMember& m);
    void analyzeMethodBody(MethodDeclaration& md);
    void analyzeCtorBody(ConstructorDeclaration& cd);
    void analyzeAccessorBody(AccessorDeclaration& ad);
    Type thisType() const;
    // True when `t` is the built-in Error class or a user class deriving from
    // it. Only such a value may be thrown (v0.2 Error model).
    bool isErrorType(const Type& t) const;
    // --- classes: conformance + `new` (class_conform_check.cpp) --------------
    Type inferUserConstruct(Identifier& id, Binding& b, NewExpr& n);
    // `super(...)` in a derived constructor: checks the arguments against the
    // nearest base constructor and marks the frame as having called it.
    Type inferSuperCall(CallExpr& call, Identifier& id);
    // The callable form of the built-in Error constructors (`Error("x")`).
    Type inferErrorCall(CallExpr& call, Identifier& id, Binding& b,
                        const ClassInfo& ci);
    bool functionTypesEquivalent(const Type& a, const Type& b);
    // Structural class -> object/interface conformance (spec 15).
    bool classConformsToShape(const ClassInfo& ci, const Type& to);
    // Fresh object literal -> class conformance (excess-property rule).
    bool literalConformsToClass(const Type& from, const ClassInfo& ci,
                                bool fresh);
    bool emitClassAssignDiff(const Type& from, const Type& to,
                             const SourceSpan& span, bool fresh);
    bool emitClassToShapeDiff(const ClassInfo& ci, const Type& to,
                              const SourceSpan& span);
    bool emitLiteralToClassDiff(const Type& from, const ClassInfo& ci,
                                const SourceSpan& span, bool fresh);

    // --- type resolution + shapes (type_check.cpp) ---------------------------
    Shape* shapeOf(const Type& t);
    Type makeObjectType(std::vector<Field> fields, bool open, bool tuple,
                        std::vector<Type> tupleElems);
    // The source's shape minus the properties a pattern named: the type of a
    // rest object binding (TypeScript's Omit). `unknown` when the source has
    // no enumerable shape (an open shape, a class, a primitive).
    Type omitShape(const Type& source, const std::vector<std::string>& named);
    const Field* findField(const Shape* shape, const std::string& name) const;
    Type resolveType(TypeNode& node);
    Type resolveTypeInner(TypeNode& node);
    Type resolveNamedType(NamedType& nt);
    Type resolveUnionType(UnionType& ut);
    Type resolveArrayType(ArrayType& at);
    Type resolveTupleType(TupleType& tt);
    Type resolveFunctionType(FunctionType& ft);
    Type resolveObjectType(ObjectType& ot);
    Type resolvePrefixedType(PrefixedType& pt);
    // Refuse a non-nullish union that mixes a native with another member.
    void rejectNativeUnion(const Type& result, const SourceSpan& span);
    Type resolveLiteralType(LiteralType& lt);
    void addPropertySignature(std::vector<Field>& fields, bool& open,
                              PropertySignature& ps);
    void addMethodSignature(std::vector<Field>& fields, bool& open,
                            MethodSignature& ms);
    std::string keyName(Expr* key) const;
    // A class member's name: "" when the key is a computed name that is not a
    // literal (a cp3 class has a fixed set of members).
    std::string classMemberName(Expr* key, bool computed);
    // The argument types of a `new` call; a spread argument's type is unknown.
    void inferNewArgs(const ExprList& args, std::vector<Type>& types,
                      std::vector<bool>& spread);
    void resolveParamSig(Parameter& p, std::vector<Type>& out);
    Type nullableIf(bool cond, const Type& t);
    Type withoutMember(const Type& t, const Type& remove);
    // --- assignability (assignability.cpp) -----------------------------------
    bool wouldAccept(const Type& from, const Type& to, bool freshLiteral);
    bool objectAccepts(const Type& from, const Type& to, bool fresh);
    void checkAssign(const Type& from, const Type& to, const SourceSpan& span,
                     bool freshLiteral);
    bool emitObjectDiff(const Type& from, const Type& to, const SourceSpan& span,
                        bool fresh);

    // --- primary expressions (expr_check.cpp) --------------------------------
    Type typeOfExpr(Expr& e);
    Type inferExpr(Expr& e);
    Type inferTemplate(TemplateLiteral& tl);
    Type inferIdentifier(Identifier& id);
    Type inferArray(ArrayLiteral& al);
    Type inferObject(ObjectLiteral& ol);
    Type inferNew(NewExpr& n);
    Type inferAwait(AwaitExpr& a);
    Type inferRange(RangeExpr& r);
    Type inferWeak(WeakExpr& w);
    Type inferSpread(SpreadElement& sp);
    Expr* dynamicExpr(Node& n);
    Type inferArrow(ArrowFunction& arrow);
    void buildArrowParamTypes(ArrowFunction& arrow, std::vector<Type>& params,
                              std::vector<bool>& optional,
                              const Type* contextualType = nullptr);
    void declareArrowParams(ArrowFunction& arrow, const std::vector<Type>& params);
    Type inferArrowReturn(ArrowFunction& arrow, const std::vector<Type>& params);
    // --- member / index / call expressions (member_check.cpp) ----------------
    Type inferMember(MemberExpr& m);
    Type memberOf(const Type& ot, const std::string& prop, const SourceSpan& span,
                  bool optional);
    // Promise members (spec §14): `.then`/`.catch`/`.catchError`/`.finally`.
    Type promiseMemberOf(const Type& ot, const std::string& prop);
    Type memberOfProbe(const Type& ot, const std::string& prop,
                       const SourceSpan& span);
    // The builtin EventEmitter member table (D74, spec 14); a miss is CP3305.
    Type eventEmitterMember(const std::string& prop, const SourceSpan& span);
    Type arrayMember(const Type& arr, const std::string& prop);
    // Enum member access (`Color.Green`, spec 04; checks/enum_check.cpp).
    Type enumMember(const Type& ot, const std::string& prop, const SourceSpan& span);
    Type stringMember(const std::string& prop);
    // Language-level Number methods (`toFixed`, ...) and statics
    // (`Number.isInteger`, constants).
    Type numberMember(const std::string& prop);
    // Ambient std namespaces and the `String(x)` global.
    std::optional<Type> stdNamespaceMember(const std::string& ns,
                                           const std::string& prop);
    std::optional<Type> mathMember(const std::string& prop);
    std::optional<Type> pathMember(const std::string& prop);
    std::optional<Type> osMember(const std::string& prop);
    std::optional<Type> processMember(const std::string& prop);
    // Documented `process` shapes (`$process:` markers).
    Type processObjectType(const std::string& name);
    std::optional<Type> envMember(const std::string& prop);
    std::optional<Type> consoleMember(const std::string& prop);
    std::optional<Type> jsonMember(const std::string& prop);
    // Native std modules; nullopt keeps the ambient shape.
    std::optional<Type> cryptoMember(const std::string& prop);
    std::optional<Type> networkMember(const std::string& prop);
    // std/date statics and instance members.
    std::optional<Type> dateMember(const std::string& prop);
    std::optional<Type> dateInstanceMember(const Type& recv,
                                           const std::string& prop);
    // RegExp value members (`test`/`match`/`exec`/`source`/`flags`).
    std::optional<Type> regexInstanceMember(const Type& recv,
                                            const std::string& prop);
    // RegExp probe; a miss is CP3305.
    Type memberOfRegex(const Type& recv, const std::string& prop,
                       const SourceSpan& span);
    std::optional<Type> fsMember(const std::string& prop);
    // std/fs `FileStats` fields; nullopt keeps ambient fallthrough.
    std::optional<Type> fsReceiverMember(const Type& recv,
                                         const std::string& prop);
    // std/io fd readiness and poll helper.
    std::optional<Type> ioMember(const std::string& prop);
    bool tryInferStringBuiltin(CallExpr& call, const Identifier& id, Type& out);

    // --- std/network (src/std/network/sema.cpp) ------------------------------
    // One documented shape per network member.
    Type networkObjectType(const std::string& name);
    // Documented marker field reads keep their declared type.
    std::optional<Type> networkObjectMember(const Type& recv,
                                            const std::string& prop);
    // Named-import call sites: `fetch(...)` / `createServer()`.
    bool tryInferNetworkCall(CallExpr& call, const Identifier& id, Type& out);
    // D79 options check for `createServer(options?)`: reports CP3401 for an
    // invalid shape/value and returns true when the call opts into scale mode
    // (a `threads` value that is not the literal 1).
    bool checkCreateServerOptions(CallExpr& call);
    // The generic instance member (`res.json<T>()` / `req.json<T>()`): the
    // explicit T substitutes into Promise<T>, matching `JSON.parse<T>`.
    bool tryInferNetworkMemberCall(CallExpr& call, MemberExpr& m, Type& out);
    // `new WebSocket(url)` / `new WebSocketServer({ port })`; unknown -> unknown.
    Type inferNetworkConstruct(const std::string& name, ExprList& args,
                               const SourceSpan& span);
    Type inferIndex(IndexExpr& ix);
    Type indexOf(const Type& ot, IndexExpr& ix);
    Type indexOfProbe(const Type& ot, IndexExpr& ix);
    Type indexOfObject(const Type& ot, IndexExpr& ix);
    Type tupleIndexAt(const Shape& sh, IndexExpr& ix);
    Type inferCall(CallExpr& call);
    bool tryTimerBuiltinCall(CallExpr& call, const Identifier& id, Type& out);
    // `parseInt`/`parseFloat` are typed `number`; `String(x)` elsewhere.
    bool tryParseBuiltinCall(CallExpr& call, const Identifier& id, Type& out);
    void reportUndefinedCall(CallExpr& call, Identifier& id);
    bool tryInferJsonCall(CallExpr& call, MemberExpr& m, Type& out);
    // `Object.keys` / `.values` / `.entries` over a statically known object
    // shape (src/std/object/sema.cpp).
    bool tryInferObjectCall(CallExpr& call, MemberExpr& m, Type& out);
    bool tryInferPromiseStaticCall(CallExpr& call, MemberExpr& m, Type& out);
    // `env.get<T>`: explicit T, else `{ default }`, else `string`.
    bool tryInferEnvCall(CallExpr& call, MemberExpr& m, Type& out);
    // Member-call inference chain (JSON, Promise, env, family, Buffer).
    bool tryInferMemberCall(CallExpr& call, MemberExpr& m, Type& out);
    void inferArgs(CallExpr& call);
    void sigFromFunctionType(const Type& ct, FuncSig& localSig);
    void inferArg(Expr* a);
    void checkArgs(const FuncSig& sig, CallExpr& call);
    // The signature the call's arguments are checked against.
    const FuncSig* callSig(const CallExpr& call, const Type& ct,
                           FuncSig& localSig);
    // The element type a spread argument carries, checked against the
    // parameters it reaches.
    void checkSpreadArg(const FuncSig& sig, Expr* a, std::size_t index);
    // The signature of the class method a member call names (the rest flag and
    // the optional parameters live in the class member table, not in the
    // function type the member read yields).
    const FuncSig* classMemberSig(const MemberExpr& m);
    // A type argument list on a non-generic callee is a type error
    // (generic_check.cpp).
    void checkTypeArgs(const FuncSig& sig, CallExpr& call);
    std::string arityText(std::size_t required, std::size_t maxArgs,
                          bool rest) const;
    // --- generic functions (generic_check.cpp) -------------------------------
    // Resolves a call to a generic function (spec §16): explicit type arguments
    // are checked against the constraints, the rest are inferred, then the
    // substituted signature runs the ordinary arity/assignability check.
    Type inferGenericCall(CallExpr& call, const FuncSig& sig);
    // Replaces every type-parameter leaf of `t` by its binding in `subst`.
    Type substituteType(const Type& t,
                        const std::unordered_map<std::string, Type>& subst);
    // Binds the type parameters of `param` from the argument type `arg`
    // (structural: arrays, tuples, class type arguments). Never fails; a
    // mismatch surfaces later through the substituted parameter check.
    void unifyParam(const Type& param, const Type& arg,
                    std::unordered_map<std::string, Type>& subst);

    // --- overload resolution (member_check.cpp) ------------------------------
    // Overload resolution for a callee naming an overload set.
    Type inferOverloadCall(CallExpr& call, const Binding& binding,
                           Identifier& id);
    void collectArgTypes(CallExpr& call, std::vector<Type>& out,
                         bool& hasSpread);
    bool overloadViable(const FuncSig& sig, const std::vector<Type>& argTypes,
                        bool hasSpread);

    // --- the JS-compat typed-array family (src/std/typed-arrays/sema.cpp) ----
    // Family constructors and nominal types (D69); codegen owns the C++.
    Type inferFamilyConstruct(const std::string& cls, ExprList& args,
                              const SourceSpan& span);
    // Family call/member typing.
    Type inferCallOrFamily(CallExpr& call);
    bool tryInferFamilyMethodCall(CallExpr& call, MemberExpr& member, Type& out);
    Type inferArrayConstruct(const ExprList& args,
                             const std::vector<Type>& types,
                             const SourceSpan& span);
    Type inferBufferConstruct(const std::string& cls, const ExprList& args,
                              const std::vector<Type>& types,
                              const SourceSpan& span);
    Type inferDataViewConstruct(const ExprList& args,
                                const std::vector<Type>& types,
                                const SourceSpan& span);
    Type inferTypedViewConstruct(const std::string& cls, const ExprList& args,
                                 const std::vector<Type>& types,
                                 const SourceSpan& span);
    bool tryInferArrayStaticCall(CallExpr& call, MemberExpr& m, Type& out);
    bool tryInferFillCall(CallExpr& call, MemberExpr& m, Type& out);
    // Array `map`/`reduce` result types.
    bool tryInferArrayCall(CallExpr& call, MemberExpr& m, Type& out);
    Type typedArrayMember(const Type& recv, const std::string& prop);
    Type bufferMember(const Type& recv, const std::string& prop);
    Type dataViewMember(const std::string& prop);
    Type typedIndexRead(IndexExpr& ix);
    // Typed-array proof machinery (src/std/typed-arrays/sema_index.cpp): the
    // constant-value and length helpers, the in-range proof for an index, and
    // the for-loop induction analysis that feeds it.
    std::optional<double> constantNumber(const Expr& e) const;
    std::optional<double> knownTypedLength(const Expr& e) const;
    bool typedIndexProvedInRange(const IndexExpr& ix) const;
    // Pattern recognized by analyzeFor: `for (let i: i64 = 0; i < N; ...)`
    // whose body does not write `i`. Returns {name, exclusive upper bound}.
    struct IndexRangeFact {
        std::string induction;
        double upper = 0.0;
    };
    std::optional<IndexRangeFact> forLoopRange(ForStatement& stmt);
    // True for the `Array(n)` length form (family owns its element type).
    bool checkArrayCtorDeclarator(const VariableDeclarator& d, bool hasAnn,
                                  const Type& annType, const Type& initType);
    bool isReadOnlyFamilyProp(const Type& ot, const std::string& prop) const;
    // --- std/collections (src/std/collections/sema.cpp) ----------------------
    // `new Map/Set`: nominal types carrying their arguments.
    Type inferCollectionsConstruct(const std::string& name, TypeList& typeArgs);
    // Map/Set member surface; a miss is CP3305.
    Type collectionsMember(const Type& recv, const std::string& prop,
                           const SourceSpan& span);

    // --- the std/buffer `Buffer` family (src/std/buffer/sema.cpp) ------------
    // Buffer member table, statics, and the D47 encoding check.
    Type byteBufferMember(const Type& recv, const std::string& prop);
    bool tryInferBufferStaticCall(CallExpr& call, MemberExpr& m, Type& out);
    bool tryInferBufferCall(CallExpr& call, MemberExpr& m, Type& out);
    bool checkByteBufferEncoding(const Expr* arg);
    // --- parallel.* capture safety (parallel_check.cpp) ----------------------
    // Parallel closure capture safety (CP3901/CP3902).
    void checkParallelCall(CallExpr& call);
    // --- native interop (checks/native_check.cpp) -----------------------------
    // The alias -> binding map, built before the statements are checked.
    void collectNativeAliases(const Program& program);
    // The binding's enums join the enum table (`dm.Color.Red`).
    void registerNativeEnums(const NativeModule& module);
    // The binding a native alias names, or nullptr.
    const NativeModule* nativeModuleFor(const std::string& alias) const;
    // The cp3 type of one member of a binding (a function, class or enum).
    // Type::error() means "not a native member": the receiver is not a native
    // alias, so the caller keeps its own lowering.
    Type nativeMemberType(MemberExpr& m);
    Type nativeMemberTypeOf(const NativeModule& module, const std::string& name,
                            const SourceSpan& span);
    // The call check of a native call: the arity, then every argument.
    void checkNativeCall(const CallExpr& call, const NativeFunction& fn);
    void checkNativeArgument(const NativeFunction& fn, const NativeArgSite& site);
    // The member-call hook: `alias.f(...)` is a call into the binding.
    bool tryInferNativeCall(CallExpr& call, MemberExpr& m, Type& out);

    // --- operators (ops_check.cpp) -------------------------------------------
    Type inferUnary(UnaryExpr& u);    Type arithmeticUnaryResult(UnaryExpr& u, const Type& ot);
    std::string unaryText(UnaryOp op) const;
    Type inferUpdate(UpdateExpr& up);
    Type inferBinary(BinaryExpr& be);
    Type evalBinary(BinaryOp op, const Type& l, const Type& r,
                    const SourceSpan& span);
    // Native arithmetic/bitwise result; nullopt lets comparison/equality
    // decide (they always yield boolean).
    std::optional<Type> evalNativeBinary(BinaryOp op, const Type& ln,
                                         const Type& rn,
                                         const SourceSpan& span);
    std::string binaryText(BinaryOp op) const;
    Type inferLogical(LogicalExpr& le);
    Type inferConditional(ConditionalExpr& ce);
    Type inferAssignment(AssignmentExpr& ae);
    Type compoundResult(AssignOp op, const Type& targetType, const Type& valType,
                        const SourceSpan& span);
    Type targetTypeOf(Expr& target);
    void checkAssignToTarget(Expr& target, const Type& valType,
                             const SourceSpan& span, bool constCheck = true);
    // --- statements (stmt_check.cpp) -----------------------------------------
    void checkStmts(StmtList& body);
    void visitStmt(Stmt& s);
    void visitExecStmt(Stmt& s);
    void visitDeclStmt(Stmt& s);
    void visitBreak(BreakStatement& b);
    void visitContinue(ContinueStatement& c);
    void visitLabeled(LabeledStatement& l);
    void visitClassDecl(ClassDeclaration& c);
    void visitEnumDecl(EnumDeclaration& e);
    void visitTypeAlias(TypeAliasDeclaration& t);
    void visitInterfaceDecl(InterfaceDeclaration& i);
    void visitExport(ExportDeclaration& e);
    void checkCondition(const Type& t, const SourceSpan& span);
    bool hasLabel(const std::string& label) const;
    bool hasIterationLabel(const std::string& label) const;
    // The control-flow context a function body starts in: no loop, no switch
    // and no label of the enclosing function. A `break`/`continue` (labeled or
    // not) cannot cross a function boundary - JS binds both to the function
    // they appear in - so a body analyzed with the enclosing context would
    // accept a `continue` that no loop of its own can reach.
    struct FunctionContext {
        int loop = 0;
        int switchCount = 0;
        std::vector<std::string> labels;
        std::vector<std::string> iterationLabels;
    };
    FunctionContext enterFunctionBody();
    void leaveFunctionBody(const FunctionContext& saved);
    void analyzeIf(IfStatement& stmt);
    bool detectTypeofNarrowing(Expr& cond, std::string& name, Type& type,
                               bool& negated);
    void analyzeWhile(WhileStatement& stmt);
    void analyzeDoWhile(DoWhileStatement& stmt);
    void analyzeFor(ForStatement& stmt);
    void analyzeForOf(ForOfStatement& stmt);
    void analyzeForIn(ForInStatement& stmt);
    void analyzeSwitch(SwitchStatement& stmt);
    void analyzeReturn(ReturnStatement& stmt);
    void analyzeTry(TryStatement& stmt);
    void analyzeFunctionBody(FunctionDeclaration& fn);
    bool allPathsReturn(Node* body);
    // True when every switch path terminates.
    bool switchCannotFallThrough(const SwitchStatement& sw);
    // --- publishing (symbols.cpp) --------------------------------------------
    void setType(const Node& n, const Type& t);
    Type withType(const Node& n, const Type& t);
    void publishSymbol(const Binding& b);
    void publishFunction(const FuncSig& sig, const std::string& name);
    void updateFunctionReturnType(std::uint32_t nodeId, const Type& ret);
    // Publishes the doc comment that documents the declaration starting at
    // `declSpan`, under `nodeId` (the declaration's own node id). The comment
    // is read from the source text (support/docs.cpp); a declaration without
    // one publishes nothing.
    void publishDoc(std::uint32_t nodeId, const SourceSpan& declSpan);
    // Publishes the doc comments of a class and of its members, plus the
    // members' own types (checks/class_docs.cpp).
    void publishClassDocs(ClassDeclaration& c);
    // --- state ----------------------------------------------------------------
    DiagnosticSink& sink_;
    SemaResult& out_;
    CompileOptions options_;
    const ImportedBindings* imports_ = nullptr;
    std::vector<std::unordered_map<std::string, Binding>> scopes_;
    std::unordered_map<std::string, Shape> shapes_;
    std::unordered_map<std::string, Type> typeNames_;
    // A generic type alias's own type parameter names, by alias name: a use
    // site substitutes its type arguments into the aliased type.
    std::unordered_map<std::string, std::vector<std::string>> typeAliasParams_;
    // The generic interfaces of this unit, by name.
    std::unordered_map<std::string, GenericInterface> genericInterfaces_;
    // The declared spelling of a named type-parameter constraint, keyed by the
    // constraint type's canonical key (registerTypeParams, scopes.cpp): lets a
    // constraint-violation diagnostic print `Shape` instead of `object`.
    std::unordered_map<std::string, std::string> typeParamConstraintNames_;
    // The resolved enum declarations, keyed by type name (checks/enum_check.cpp).
    std::unordered_map<std::string, EnumInfo> enums_;
    std::unordered_map<std::uint32_t, FuncSig> sigs_;
    std::unordered_set<std::uint32_t> declaredFns_;
    std::unordered_set<std::uint32_t> declaredTypes_;
    std::unordered_map<std::uint32_t, FuncSig> arrowSigs_;
    // Expected function types supplied by a call/assignment context while an
    // arrow is first inferred. This gives unannotated callback parameters the
    // same useful types in the body that TypeScript-style contextual typing
    // provides (e.g. HTTP `req` and `res`).
    std::unordered_map<std::uint32_t, Type> contextualArrowTypes_;
    // Named std imports bound under another spelling (local -> exported name):
    // `import { Map as M } from "std/collections"` records M -> Map so the
    // construct tables keyed on the canonical name still match.
    std::unordered_map<std::string, std::string> stdImportAliases_;
    // The native bindings this unit imports, keyed by their local alias
    // (`import native "demo" as dm` -> dm). The models live in the compile
    // options; the map points into them.
    std::unordered_map<std::string, const NativeModule*> nativeAliases_;
    std::vector<std::string> labels_;
    // The active labels that name an ITERATION statement (a loop): a
    // `continue label;` may only target one of these (JS: "continue" needs an
    // iteration statement; a label on a block or a switch may only be broken
    // out of).
    std::vector<std::string> iterationLabels_;
    // Parallel-checked call nodes (report once).
    std::unordered_set<std::uint32_t> parallelChecked_;
    // User classes: member tables, declaration guard, class stack, weak reads;
    // `weakFieldHit_` is set by userClassMember and consumed by inferMember.
    std::unordered_map<std::string, ClassInfo> classes_;
    std::unordered_set<std::uint32_t> classResolved_;
    std::vector<std::string> classStack_;
    std::unordered_set<std::uint32_t> weakReads_;
    bool weakFieldHit_ = false;
    bool assignTarget_ = false;
    struct FuncCtx {
        bool annotated = false;
        Type ret;
        bool sawReturn = false;
        std::vector<Type> returnTypes;
        // The frame is an `async` function (or an async arrow): only such a
        // frame may `await` (the top level may too).
        bool isAsync = false;
        // The frame is a class constructor. A derived constructor must call
        // `super(...)` exactly once before touching `this`.
        bool isCtor = false;
        bool sawSuper = false;
    };
    std::vector<FuncCtx> funcStack_;
    // Induction facts from for-loops recognized by forLoopRange: the loop
    // variable's name and an exclusive upper bound on it, pushed while the
    // body is checked so typedIndexRead can prove the index in range.
    std::vector<IndexRangeFact> indexRanges_;
    int loopDepth_ = 0;
    int switchDepth_ = 0;
};

// --- std-namespace surface helpers (std_check.cpp) ---------------------------
//
// The closed families (Math / path / os / crypto / console / JSON / Date /
// fs / process / env) report CP3305 for an unknown member (member_check.cpp);
// the open ones (network, ...) keep the permissive ambient fallthrough.
bool isClosedStdNamespace(const std::string& ns);

// The implemented member names of `ns`, used as CP3305 candidates.
std::vector<std::string> stdNamespaceMemberNames(const std::string& ns);

// The implemented member names of one family's table, the CP3305 candidate
// list `stdNamespaceMemberNames` (std_check.cpp) forwards to. Each family unit
// (src/std/<family>/sema.cpp) defines its own next to the table.
std::vector<std::string> mathMemberNames();
std::vector<std::string> numberMemberNames();
std::vector<std::string> numberStaticMemberNames();
std::vector<std::string> pathMemberNames();
std::vector<std::string> osMemberNames();
std::vector<std::string> processMemberNames();
std::vector<std::string> envMemberNames();
std::vector<std::string> consoleMemberNames();
std::vector<std::string> consoleMemberArgumentValues(
    const std::string& prop, std::size_t argumentIndex);
std::vector<std::string> jsonMemberNames();
std::vector<std::string> cryptoMemberNames();
std::vector<std::string> fsMemberNames();
std::vector<std::string> ioMemberNames();
std::vector<std::string> dateMemberNames();
std::vector<std::string> dateInstanceMemberNames();

// The documentation of one family member: the documented signature and the
// Markdown description the editor shows. Each family unit defines its own
// next to the table it already owns, so the two cannot drift apart.
// `Number.*` statics (`Number.isInteger`, constants); nullopt keeps the ambient
// fallthrough. Defined in src/std/number/sema.cpp.
std::optional<Type> numberStaticMember(const std::string& prop);

StdMemberDoc mathMemberDoc(const std::string& prop);
StdMemberDoc numberMemberDoc(const std::string& prop);
StdMemberDoc numberStaticMemberDoc(const std::string& prop);
StdMemberDoc pathMemberDoc(const std::string& prop);
StdMemberDoc osMemberDoc(const std::string& prop);
StdMemberDoc processMemberDoc(const std::string& prop);
StdMemberDoc envMemberDoc(const std::string& prop);
StdMemberDoc consoleMemberDoc(const std::string& prop);
StdMemberDoc jsonMemberDoc(const std::string& prop);
StdMemberDoc cryptoMemberDoc(const std::string& prop);
StdMemberDoc fsMemberDoc(const std::string& prop);
StdMemberDoc ioMemberDoc(const std::string& prop);
StdMemberDoc fsStatsMemberDoc(const std::string& prop);
std::vector<std::string> fsStatsMemberNames();
StdMemberDoc dateMemberDoc(const std::string& prop);
StdMemberDoc dateInstanceMemberDoc(const std::string& prop);
StdMemberDoc processExecResultDoc(const std::string& prop);
StdMemberDoc processSpawnHandleDoc(const std::string& prop);
std::vector<std::string> processExecResultMemberNames();
std::vector<std::string> processSpawnHandleMemberNames();
// The `Map` / `Set` members (std/collections).
StdMemberDoc collectionsMemberDoc(const std::string& family,
                                   const std::string& prop);
std::vector<std::string> collectionsMemberNames(const std::string& family);
// The array and string method surfaces.
StdMemberDoc arrayMemberDoc(const std::string& prop);
std::vector<std::string> arrayMemberNames();
StdMemberDoc stringMemberDoc(const std::string& prop);
std::vector<std::string> stringMemberNames();

// The nominal Date value type (std/date; src/std/date/sema.cpp). member_check
// routes a Date receiver's members to the family table and reports CP3305 for
// a miss, so the class never falls back to the permissive ambient shape.
bool isDateClass(const Type& t);

// The nominal RegExp value type (core regex literals, D48;
// src/sema/src/checks/regex_check.cpp). expr_check types a RegExpLiteral as
// `regexLiteralType()`; member_check routes a RegExp receiver's members to the
// family table and reports CP3305 for a miss, so the class never falls back to
// the permissive ambient shape.
Type regexLiteralType();
bool isRegexClass(const Type& t);
std::vector<std::string> regexInstanceMemberNames();

// The exact default-import statement for a missing std module binding (e.g.
// `import Math from "std/math";`), or "" when `name` names no std module.
std::string stdImportHint(const std::string& name);

}  // namespace cp3
