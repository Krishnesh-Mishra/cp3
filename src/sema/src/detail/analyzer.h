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
//   checks/ops_check.cpp    operator expression inference
//   checks/stmt_check.cpp   statements, control flow, return analysis
//   checks/banned.cpp       banned-pattern diagnostics (delete/arguments/for-in/any)
//   checks/std_check.cpp    std-namespace dispatch + `String(x)`
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

#include "cp3/analysis.hpp"
#include "cp3/ast.hpp"
#include "cp3/diagnostic.hpp"
#include "detail/scopes.h"

namespace cp3 {

// The sema implementation. One instance per `SemanticAnalyzer::analyze` call;
// it owns the scope stack, the private shape/signature tables and the
// references to the sink + result it publishes into.
class Analyzer {
public:
    Analyzer(DiagnosticSink& sink, SemaResult& out) : sink_(sink), out_(out) {
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
    void notImplemented(const SourceSpan& span, const std::string& what);
    // CP3301 for a name that resolves nowhere: suggests the closest visible
    // names and, when the name is a std module, its exact import statement.
    void reportUndefinedName(const SourceSpan& span, const std::string& name);
    // CP3305 for a miss on a known receiver (`'console'` / `object` text, the
    // member accessed, and the implemented member names to rank).
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
    void overrideBinding(const std::string& name, const Type& type,
                         const SourceSpan& span);
    void declareBuiltins();
    void hoistType(Stmt& s);
    void hoistFunction(FunctionDeclaration& fn);
    void hoistVars(StmtList& body);
    void hoistVarsIn(Stmt& s);
    void hoistVarBody(StmtPtr& body);
    void registerTypeParams(TypeParamList& typeParams);

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

    // --- type resolution + shapes (type_check.cpp) ---------------------------
    Shape* shapeOf(const Type& t);
    Type makeObjectType(std::vector<Field> fields, bool open, bool tuple,
                        std::vector<Type> tupleElems);
    const Field* findField(const Shape* shape, const std::string& name) const;
    Type resolveType(TypeNode& node);
    Type resolveNamedType(NamedType& nt);
    Type resolveUnionType(UnionType& ut);
    Type resolveArrayType(ArrayType& at);
    Type resolveTupleType(TupleType& tt);
    Type resolveFunctionType(FunctionType& ft);
    Type resolveObjectType(ObjectType& ot);
    Type resolvePrefixedType(PrefixedType& pt);
    Type resolveLiteralType(LiteralType& lt);
    void addPropertySignature(std::vector<Field>& fields, bool& open,
                              PropertySignature& ps);
    void addMethodSignature(std::vector<Field>& fields, bool& open,
                            MethodSignature& ms);
    std::string keyName(Expr* key) const;
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
                              std::vector<bool>& optional);
    void declareArrowParams(ArrowFunction& arrow, const std::vector<Type>& params);
    Type inferArrowReturn(ArrowFunction& arrow, const std::vector<Type>& params);

    // --- member / index / call expressions (member_check.cpp) ----------------
    Type inferMember(MemberExpr& m);
    Type memberOf(const Type& ot, const std::string& prop, const SourceSpan& span,
                  bool optional);
    Type memberOfProbe(const Type& ot, const std::string& prop,
                       const SourceSpan& span);
    Type arrayMember(const Type& arr, const std::string& prop);
    Type stringMember(const std::string& prop);
    // Typed views of the ambient std namespaces + the `String(x)` global
    // (the per-family tables live in src/std/<family>/sema.cpp; STDLIB docs
    // §04).
    std::optional<Type> stdNamespaceMember(const std::string& ns,
                                           const std::string& prop);
    std::optional<Type> mathMember(const std::string& prop);
    std::optional<Type> pathMember(const std::string& prop);
    std::optional<Type> osMember(const std::string& prop);
    std::optional<Type> processMember(const std::string& prop);
    std::optional<Type> consoleMember(const std::string& prop);
    std::optional<Type> jsonMember(const std::string& prop);
    // Native-backed std modules: a member outside the documented surface
    // returns nullopt, so member access keeps the permissive ambient shape.
    // Definitions live in the per-module src/std/<module>/sema.cpp units.
    std::optional<Type> cryptoMember(const std::string& prop);
    std::optional<Type> networkMember(const std::string& prop);
    // std/date (src/std/date/sema.cpp): the namespace statics (`Date.now`,
    // `Date.fromISOString`) and, for a Date value, its instance members.
    std::optional<Type> dateMember(const std::string& prop);
    std::optional<Type> dateInstanceMember(const Type& recv,
                                           const std::string& prop);
    bool tryInferStringBuiltin(CallExpr& call, const Identifier& id, Type& out);

    // --- std/network (src/std/network/sema.cpp) ------------------------------
    // One documented object shape per member table entry ("$network:<name>").
    Type networkObjectType(const std::string& name);
    // Named-import call sites: `fetch(...)` / `createServer()`.
    bool tryInferNetworkCall(CallExpr& call, const Identifier& id, Type& out);
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
    void reportUndefinedCall(CallExpr& call, Identifier& id);
    bool tryInferJsonCall(CallExpr& call, MemberExpr& m, Type& out);
    bool tryInferPromiseStaticCall(CallExpr& call, MemberExpr& m, Type& out);
    void inferArgs(CallExpr& call);
    void sigFromFunctionType(const Type& ct, FuncSig& localSig);
    void inferArg(Expr* a);
    void checkArgs(const FuncSig& sig, CallExpr& call);
    std::string arityText(std::size_t required, std::size_t maxArgs,
                          bool rest) const;

    // --- overload resolution (member_check.cpp) ------------------------------
    // Resolves a call whose callee names an overload set (Binding::overloads):
    // accepts the call when a signature is viable and reports CP3416 otherwise.
    Type inferOverloadCall(CallExpr& call, const Binding& binding,
                           Identifier& id);
    void collectArgTypes(CallExpr& call, std::vector<Type>& out,
                         bool& hasSpread);
    bool overloadViable(const FuncSig& sig, const std::vector<Type>& argTypes,
                        bool hasSpread);

    // --- the JS-compat typed-array family (src/std/typed-arrays/sema.cpp) ----
    // Constructors registered by `declareBuiltins` (D69): `Array` extras plus
    // the 11 typed classes, the buffer classes and DataView. Family types are
    // nominal `TypeKind::Class` values; codegen owns the C++ mapping.
    Type inferFamilyConstruct(const std::string& cls, ExprList& args,
                              const SourceSpan& span);
    // `inferCall`'s family wrapper (the `new`-less constructor spelling types
    // like `new`) and the member-side statics (`Array.of/from`) + `arr.fill`.
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
    Type typedArrayMember(const Type& recv, const std::string& prop);
    Type bufferMember(const Type& recv, const std::string& prop);
    Type dataViewMember(const std::string& prop);
    Type typedIndexRead(IndexExpr& ix);
    // True when `d` is the `Array(n)` length form (the family check owns its
    // element typing, so the generic assign check must stand down).
    bool checkArrayCtorDeclarator(const VariableDeclarator& d, bool hasAnn,
                                  const Type& annType, const Type& initType);
    bool isReadOnlyFamilyProp(const Type& ot, const std::string& prop) const;

    // --- std/collections (src/std/collections/sema.cpp) ----------------------
    // `new Map<K, V>()` / `new Set<T>()`: nominal class types carrying their
    // type arguments (parts { K, V } / { T }); unknown for other names.
    Type inferCollectionsConstruct(const std::string& name, TypeList& typeArgs);
    // The docs/04 Map/Set member surface; a member outside it reports CP3305
    // with the implemented member names as candidates.
    Type collectionsMember(const Type& recv, const std::string& prop,
                           const SourceSpan& span);

    // --- parallel.* capture safety (parallel_check.cpp) ----------------------
    // Walks the closures passed to a `parallel.*` entry point and reports a
    // captured-state write (CP3902) or an unsafe computed-index write (CP3901);
    // writes through parallel.for's own induction variable are allowed.
    void checkParallelCall(CallExpr& call);

    // --- operators (ops_check.cpp) -------------------------------------------
    Type inferUnary(UnaryExpr& u);
    Type arithmeticUnaryResult(UnaryExpr& u, const Type& ot);
    std::string unaryText(UnaryOp op) const;
    Type inferUpdate(UpdateExpr& up);
    Type inferBinary(BinaryExpr& be);
    Type evalBinary(BinaryOp op, const Type& l, const Type& r,
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

    // --- publishing (symbols.cpp) --------------------------------------------
    void setType(const Node& n, const Type& t);
    Type withType(const Node& n, const Type& t);
    void publishSymbol(const Binding& b);
    void publishFunction(const FuncSig& sig, const std::string& name);
    void updateFunctionReturnType(std::uint32_t nodeId, const Type& ret);

    // --- state ----------------------------------------------------------------
    DiagnosticSink& sink_;
    SemaResult& out_;
    std::vector<std::unordered_map<std::string, Binding>> scopes_;
    std::unordered_map<std::string, Shape> shapes_;
    std::unordered_map<std::string, Type> typeNames_;
    std::unordered_map<std::uint32_t, FuncSig> sigs_;
    std::unordered_set<std::uint32_t> declaredFns_;
    std::unordered_set<std::uint32_t> declaredTypes_;
    std::unordered_map<std::uint32_t, FuncSig> arrowSigs_;
    std::vector<std::string> labels_;
    // Call nodes already checked for parallel.* capture safety, so a node that
    // is inferred more than once is never reported twice.
    std::unordered_set<std::uint32_t> parallelChecked_;

    struct FuncCtx {
        bool annotated = false;
        Type ret;
        bool sawReturn = false;
        std::vector<Type> returnTypes;
    };
    std::vector<FuncCtx> funcStack_;
    int loopDepth_ = 0;
    int switchDepth_ = 0;
};

// --- std-namespace surface helpers (std_check.cpp) ---------------------------
//
// The closed families (Math / path / os / crypto / console / JSON) report
// CP3305 for an unknown member (member_check.cpp); the open ones (network,
// process, fs, ...) keep the permissive ambient fallthrough.
bool isClosedStdNamespace(const std::string& ns);

// The implemented member names of `ns`, used as CP3305 candidates.
std::vector<std::string> stdNamespaceMemberNames(const std::string& ns);

// The implemented member names of one family's table, the CP3305 candidate
// list `stdNamespaceMemberNames` (std_check.cpp) forwards to. Each family unit
// (src/std/<family>/sema.cpp) defines its own next to the table.
std::vector<std::string> mathMemberNames();
std::vector<std::string> pathMemberNames();
std::vector<std::string> osMemberNames();
std::vector<std::string> consoleMemberNames();
std::vector<std::string> jsonMemberNames();
std::vector<std::string> cryptoMemberNames();
std::vector<std::string> dateMemberNames();
std::vector<std::string> dateInstanceMemberNames();

// The nominal Date value type (std/date; src/std/date/sema.cpp). member_check
// routes a Date receiver's members to the family table and reports CP3305 for
// a miss, so the class never falls back to the permissive ambient shape.
bool isDateClass(const Type& t);

// The exact default-import statement for a missing std module binding (e.g.
// `import Math from "std/math";`), or "" when `name` names no std module.
std::string stdImportHint(const std::string& name);

}  // namespace cp3
