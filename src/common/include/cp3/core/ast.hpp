// =============================================================================
// cp3/ast.hpp - the COMPLETE Beta One abstract syntax tree.
//
// FROZEN INTERFACE (Beta One). See ARCHITECTURE.md.
//
// Design rules (see ARCHITECTURE.md "Frozen interfaces"):
//   * Every node carries a SourceSpan (`node.span`) and a stable `node.nodeId`.
//   * Nodes are plain structs (public fields) in an is-a hierarchy rooted at
//     `Node`; ownership is via std::unique_ptr.
//   * The single source of truth for "which nodes exist" is
//     CP3_AST_NODE_LIST - it generates NodeKind, the forward declarations, and
//     (in visitor.hpp) exhaustive visitor dispatch. To ADD a node: add one
//     line there and define the struct. Nothing else.
//   * Nodes hold NO semantic results (resolved types, symbol bindings). sema/
//     codegen keep side tables keyed by nodeId, so the AST stays a pure parse
//     product. Fields reserved for later use are documented as such.
//
// Coverage note - constructs the spec webserveres without a dedicated node are
// modelled by composition and MUST be documented in ARCHITECTURE.md:
//   * `parallel.for(...)` / `parallel.reduce(...)` / `parallel.run(...)`
//        -> CallExpr{ callee = MemberExpr{ Identifier("parallel"), "for" }, ... }
//   * `worker(data)`                 -> CallExpr{ callee = Identifier("worker") }
//   * `0..n` (range)                 -> RangeExpr
//   * `{ weak: value }` wrapper      -> WeakExpr
//   * `Object.keys(o)` compile macro -> CallExpr{ MemberExpr{Identifier("Object"),...} }
// =============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cp3/core/source.hpp"

namespace cp3 {

// -----------------------------------------------------------------------------
// Operator / modifier enums
// -----------------------------------------------------------------------------
enum class VarKind { Let, Var, Const };

enum class AccessModifier { None, Public, Private, Protected };

enum class UnaryOp { Not, Negate, Plus, BitNot, Typeof, Delete, Void };

enum class UpdateOp { Increment, Decrement };

enum class BinaryOp {
    Add, Subtract, Multiply, Divide, Modulo, Power,
    Equal, NotEqual, StrictEqual, StrictNotEqual,
    LessThan, GreaterThan, LessThanEqual, GreaterThanEqual,
    BitAnd, BitOr, BitXor, ShiftLeft, ShiftRight, UnsignedShiftRight,
    In, Instanceof,
};

enum class LogicalOp { And, Or, Nullish };

enum class AssignOp {
    Assign, Add, Subtract, Multiply, Divide, Modulo, Power,
    BitAnd, BitOr, BitXor, ShiftLeft, ShiftRight, UnsignedShiftRight,
    LogicalAnd, LogicalOr, Nullish,  // &&= ||= ??=
};

enum class AccessorKind { Get, Set };

enum class LiteralTypeKind { String, Number, Bool };

enum class ImportKind { Named, Default, Namespace, SideEffect, Native, Cpp };

enum class ExportKind { Named, Default, Declaration, ReExportAll, ReExportNamed };

// -----------------------------------------------------------------------------
// The node registry (single source of truth)
// -----------------------------------------------------------------------------
#define CP3_AST_NODE_LIST(X) \
    X(Program)                    \
    /* --- webserverions --- */     \
    X(NumberLiteral)              \
    X(StringLiteral)              \
    X(BoolLiteral)                \
    X(NullLiteral)                \
    X(UndefinedLiteral)           \
    X(TemplateLiteral)            \
    X(TemplateElement)            \
    X(RegExpLiteral)              \
    X(Identifier)                 \
    X(ThisExpr)                   \
    X(ArrayLiteral)               \
    X(ObjectLiteral)              \
    X(ObjectProperty)             \
    X(SpreadElement)              \
    X(WeakExpr)                   \
    X(MemberExpr)                 \
    X(IndexExpr)                  \
    X(CallExpr)                   \
    X(NewExpr)                    \
    X(UnaryExpr)                  \
    X(UpdateExpr)                 \
    X(BinaryExpr)                 \
    X(LogicalExpr)                \
    X(ConditionalExpr)            \
    X(AssignmentExpr)             \
    X(ArrowFunction)              \
    X(AwaitExpr)                  \
    X(RangeExpr)                  \
    X(AsExpr)                     \
    X(NonNullExpr)                \
    /* --- patterns --- */         \
    X(ArrayPattern)               \
    X(ObjectPattern)              \
    X(ObjectPatternProperty)      \
    X(RestElement)                \
    X(AssignmentPattern)          \
    /* --- statements --- */       \
    X(webserverionStatement)        \
    X(BlockStatement)             \
    X(EmptyStatement)             \
    X(VariableDeclaration)        \
    X(VariableDeclarator)         \
    X(FunctionDeclaration)        \
    X(ClassDeclaration)           \
    X(InterfaceDeclaration)       \
    X(EnumDeclaration)            \
    X(EnumMember)                 \
    X(TypeAliasDeclaration)       \
    X(IfStatement)                \
    X(WhileStatement)             \
    X(DoWhileStatement)           \
    X(ForStatement)               \
    X(ForOfStatement)             \
    X(ForInStatement)             \
    X(SwitchStatement)            \
    X(SwitchCase)                 \
    X(BreakStatement)             \
    X(ContinueStatement)          \
    X(ReturnStatement)            \
    X(ThrowStatement)             \
    X(TryStatement)               \
    X(CatchClause)                \
    X(LabeledStatement)           \
    X(ImportDeclaration)          \
    X(ImportSpecifier)            \
    X(ExportDeclaration)          \
    X(ExportSpecifier)            \
    /* --- class / interface members, params --- */ \
    X(PropertyDeclaration)        \
    X(MethodDeclaration)          \
    X(ConstructorDeclaration)     \
    X(AccessorDeclaration)        \
    X(StaticBlock)                \
    X(PropertySignature)          \
    X(MethodSignature)            \
    X(Parameter)                  \
    X(TypeParameter)              \
    /* --- types --- */            \
    X(NamedType)                  \
    X(UnionType)                  \
    X(ArrayType)                  \
    X(TupleType)                  \
    X(FunctionType)               \
    X(ObjectType)                 \
    X(PrefixedType)               \
    X(LiteralType)                \
    X(ParenthesizedType)

enum class NodeKind {
#define CP3_DECLARE_KIND(Name) Name,
    CP3_AST_NODE_LIST(CP3_DECLARE_KIND)
#undef CP3_DECLARE_KIND
};

const char* nodeKindName(NodeKind kind) noexcept;

// -----------------------------------------------------------------------------
// Base classes
// -----------------------------------------------------------------------------
struct Node {
    NodeKind kind;
    SourceSpan span;
    std::uint32_t nodeId = 0;  // assigned by the parser; stable for a compilation

    Node(NodeKind k, SourceSpan s) : kind(k), span(std::move(s)) {}
    virtual ~Node() = default;

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
};

struct Expr : Node {
    Expr(NodeKind k, SourceSpan s) : Node(k, std::move(s)) {}
};

struct Stmt : Node {
    Stmt(NodeKind k, SourceSpan s) : Node(k, std::move(s)) {}
};

// Declarations are statements so a module body is simply a list<StmtPtr>.
struct Decl : Stmt {
    Decl(NodeKind k, SourceSpan s) : Stmt(k, std::move(s)) {}
};

struct TypeNode : Node {
    TypeNode(NodeKind k, SourceSpan s) : Node(k, std::move(s)) {}
};

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct ClassMember : Node {
    // `@Name` / `@Name(args)` attached to this member. They are compile-time
    // macros: sema/codegen consume them before emission and the generated C++
    // never carries decorator metadata.
    std::vector<ExprPtr> decorators;
    ClassMember(NodeKind k, SourceSpan s) : Node(k, std::move(s)) {}
};

struct SignatureMember : Node {
    SignatureMember(NodeKind k, SourceSpan s) : Node(k, std::move(s)) {}
};

// Forward declarations, generated from the registry.
#define CP3_FWD(Name) struct Name;
CP3_AST_NODE_LIST(CP3_FWD)
#undef CP3_FWD

// Ownership aliases.
using NodePtr = std::unique_ptr<Node>;
using StmtPtr = std::unique_ptr<Stmt>;
using TypePtr = std::unique_ptr<TypeNode>;
using DeclPtr = std::unique_ptr<Decl>;
using ClassMemberPtr = std::unique_ptr<ClassMember>;
using SignatureMemberPtr = std::unique_ptr<SignatureMember>;

using ProgramPtr = std::unique_ptr<Program>;
using TemplateElementPtr = std::unique_ptr<TemplateElement>;
using ObjectPropertyPtr = std::unique_ptr<ObjectProperty>;
using ObjectPatternPropertyPtr = std::unique_ptr<ObjectPatternProperty>;
using VariableDeclaratorPtr = std::unique_ptr<VariableDeclarator>;
using EnumMemberPtr = std::unique_ptr<EnumMember>;
using SwitchCasePtr = std::unique_ptr<SwitchCase>;
using CatchClausePtr = std::unique_ptr<CatchClause>;
using ImportSpecifierPtr = std::unique_ptr<ImportSpecifier>;
using ExportSpecifierPtr = std::unique_ptr<ExportSpecifier>;
using ParameterPtr = std::unique_ptr<Parameter>;
using TypeParameterPtr = std::unique_ptr<TypeParameter>;

using ParamList = std::vector<ParameterPtr>;
using TypeParamList = std::vector<TypeParameterPtr>;
using TypeList = std::vector<TypePtr>;
using ExprList = std::vector<ExprPtr>;
using StmtList = std::vector<StmtPtr>;

// =============================================================================
// Small shared pieces (defined before nodes that embed them)
// =============================================================================

// A function parameter.
//   function f(a: number, b?: string, ref c: number, ...rest: number[])
struct Parameter : Node {
    NodePtr name;            // Identifier, ArrayPattern, ObjectPattern, or RestElement
    TypePtr type;            // optional
    ExprPtr defaultValue;    // optional (destructuring defaults use AssignmentPattern instead)
    bool optional = false;   // `b?`
    bool isRest = false;     // `...rest`
    bool isRef = false;      // `ref x`
    AccessModifier access = AccessModifier::None;  // constructor parameter property
    bool isReadonly = false; // `readonly` constructor parameter property

    Parameter(SourceSpan s) : Node(NodeKind::Parameter, std::move(s)) {}
};

// A generic type parameter: <T extends Shape = Default>
struct TypeParameter : Node {
    std::string name;
    TypePtr constraint;    // optional `extends X`
    TypePtr defaultType;   // optional `= X`

    TypeParameter(SourceSpan s) : Node(NodeKind::TypeParameter, std::move(s)) {}
};

// =============================================================================
// Module root
// =============================================================================
struct Program : Node {
    StmtList body;
    Program(SourceSpan s) : Node(NodeKind::Program, std::move(s)) {}
};

// =============================================================================
// webserverions
// =============================================================================
struct NumberLiteral : Expr {
    double value = 0.0;
    std::string raw;  // original text, e.g. "0x1F", "1_000_000"
    NumberLiteral(SourceSpan s, double v, std::string r)
        : Expr(NodeKind::NumberLiteral, std::move(s)), value(v), raw(std::move(r)) {}
};

struct StringLiteral : Expr {
    std::string value;  // decoded (escapes applied)
    std::string raw;    // raw including quotes
    StringLiteral(SourceSpan s, std::string v, std::string r)
        : Expr(NodeKind::StringLiteral, std::move(s)),
          value(std::move(v)),
          raw(std::move(r)) {}
};

struct BoolLiteral : Expr {
    bool value = false;
    BoolLiteral(SourceSpan s, bool v) : Expr(NodeKind::BoolLiteral, std::move(s)), value(v) {}
};

struct NullLiteral : Expr {
    explicit NullLiteral(SourceSpan s) : Expr(NodeKind::NullLiteral, std::move(s)) {}
};

struct UndefinedLiteral : Expr {
    explicit UndefinedLiteral(SourceSpan s) : Expr(NodeKind::UndefinedLiteral, std::move(s)) {}
};

// One quasi (literal chunk) of a template literal.
struct TemplateElement : Node {
    std::string cooked;  // escapes resolved
    std::string raw;     // as written
    bool tail = false;   // true for the final quasi
    explicit TemplateElement(SourceSpan s) : Node(NodeKind::TemplateElement, std::move(s)) {}
};

// `head${e0}mid${e1}tail` - quasis.size() == webserverions.size() + 1.
struct TemplateLiteral : Expr {
    std::vector<TemplateElementPtr> quasis;
    ExprList webserverions;
    explicit TemplateLiteral(SourceSpan s) : Expr(NodeKind::TemplateLiteral, std::move(s)) {}
};

// Native regex literal: /pattern/flags (core language, no import - spec §20/§3).
struct RegExpLiteral : Expr {
    std::string pattern;
    std::string flags;
    RegExpLiteral(SourceSpan s, std::string p, std::string f)
        : Expr(NodeKind::RegExpLiteral, std::move(s)),
          pattern(std::move(p)),
          flags(std::move(f)) {}
};

struct Identifier : Expr {
    std::string name;
    Identifier(SourceSpan s, std::string n)
        : Expr(NodeKind::Identifier, std::move(s)), name(std::move(n)) {}
};

struct ThisExpr : Expr {
    explicit ThisExpr(SourceSpan s) : Expr(NodeKind::ThisExpr, std::move(s)) {}
};

struct ArrayLiteral : Expr {
    ExprList elements;  // may contain SpreadElement; nullptr entries = holes
    explicit ArrayLiteral(SourceSpan s) : Expr(NodeKind::ArrayLiteral, std::move(s)) {}
};

// A property in an object literal. `key` is an Identifier/StringLiteral/
// NumberLiteral, or an arbitrary webserverion when `computed`.
struct ObjectProperty : Node {
    ExprPtr key;
    ExprPtr value;          // may be nullptr for shorthand (`{ a }` -> value = Identifier a)
    bool computed = false;  // `{ [expr]: v }`
    bool shorthand = false; // `{ a }`
    bool isMethod = false;  // `{ m() {} }` (reserved)
    explicit ObjectProperty(SourceSpan s) : Node(NodeKind::ObjectProperty, std::move(s)) {}
};

struct ObjectLiteral : Expr {
    std::vector<ObjectPropertyPtr> properties;
    explicit ObjectLiteral(SourceSpan s) : Expr(NodeKind::ObjectLiteral, std::move(s)) {}
};

struct SpreadElement : Expr {
    ExprPtr argument;
    explicit SpreadElement(SourceSpan s) : Expr(NodeKind::SpreadElement, std::move(s)) {}
};

// The `{ weak: value }` object-literal wrapper (spec §11): a weak link that
// does not add to the target's strong count.
struct WeakExpr : Expr {
    ExprPtr value;
    explicit WeakExpr(SourceSpan s) : Expr(NodeKind::WeakExpr, std::move(s)) {}
};

struct MemberExpr : Expr {
    ExprPtr object;
    std::string property;      // simple name access (a.b)
    bool optional = false;     // a?.b
    explicit MemberExpr(SourceSpan s) : Expr(NodeKind::MemberExpr, std::move(s)) {}
};

struct IndexExpr : Expr {
    ExprPtr object;
    ExprPtr index;             // a[i] / a["k"]
    bool optional = false;     // a?.[i]
    explicit IndexExpr(SourceSpan s) : Expr(NodeKind::IndexExpr, std::move(s)) {}
};

struct CallExpr : Expr {
    ExprPtr callee;              // Identifier/MemberExpr/etc. (also worker(...), parallel.for(...))
    ExprList arguments;          // may contain SpreadElement
    TypeList typeArgs;           // optional explicit generics: f<number>(...)
    bool optional = false;       // f?.()
    explicit CallExpr(SourceSpan s) : Expr(NodeKind::CallExpr, std::move(s)) {}
};

struct NewExpr : Expr {
    ExprPtr callee;              // Identifier or MemberExpr (new Person(), new UE.Actor())
    TypeList typeArgs;           // new Box<number>()
    ExprList arguments;
    explicit NewExpr(SourceSpan s) : Expr(NodeKind::NewExpr, std::move(s)) {}
};

struct UnaryExpr : Expr {
    UnaryOp op = UnaryOp::Not;
    ExprPtr operand;
    bool prefix = true;          // all Beta One unary ops are prefix; kept explicit
    explicit UnaryExpr(SourceSpan s) : Expr(NodeKind::UnaryExpr, std::move(s)) {}
};

struct UpdateExpr : Expr {
    UpdateOp op = UpdateOp::Increment;
    ExprPtr operand;
    bool prefix = true;          // ++x vs x++
    explicit UpdateExpr(SourceSpan s) : Expr(NodeKind::UpdateExpr, std::move(s)) {}
};

struct BinaryExpr : Expr {
    BinaryOp op = BinaryOp::Add;
    ExprPtr left;
    ExprPtr right;
    explicit BinaryExpr(SourceSpan s) : Expr(NodeKind::BinaryExpr, std::move(s)) {}
};

// Short-circuiting operators are kept distinct so semantics are not lost.
struct LogicalExpr : Expr {
    LogicalOp op = LogicalOp::And;
    ExprPtr left;
    ExprPtr right;
    explicit LogicalExpr(SourceSpan s) : Expr(NodeKind::LogicalExpr, std::move(s)) {}
};

struct ConditionalExpr : Expr {
    ExprPtr condition;
    ExprPtr whenTrue;
    ExprPtr whenFalse;
    explicit ConditionalExpr(SourceSpan s) : Expr(NodeKind::ConditionalExpr, std::move(s)) {}
};

struct AssignmentExpr : Expr {
    AssignOp op = AssignOp::Assign;
    ExprPtr target;   // Identifier / MemberExpr / IndexExpr / pattern
    ExprPtr value;
    explicit AssignmentExpr(SourceSpan s) : Expr(NodeKind::AssignmentExpr, std::move(s)) {}
};

// `(a, b) => expr` or `(a, b) => { ... }`; may be `async`.
struct ArrowFunction : Expr {
    bool isAsync = false;
    TypeParamList typeParams;
    ParamList params;
    TypePtr returnType;     // optional
    NodePtr body;           // webserverion (webserverionBody=true) or BlockStatement
    bool webserverionBody = true;
    explicit ArrowFunction(SourceSpan s) : Expr(NodeKind::ArrowFunction, std::move(s)) {}
};

struct AwaitExpr : Expr {
    ExprPtr argument;
    explicit AwaitExpr(SourceSpan s) : Expr(NodeKind::AwaitExpr, std::move(s)) {}
};

// `start..end` - used by parallel.for / parallel.reduce ranges.
struct RangeExpr : Expr {
    ExprPtr start;
    ExprPtr end;
    explicit RangeExpr(SourceSpan s) : Expr(NodeKind::RangeExpr, std::move(s)) {}
};

// `expr as Type` - a compile-time-checked type assertion (TypeScript `as`).
struct AsExpr : Expr {
    ExprPtr webserverion;
    TypePtr type;
    explicit AsExpr(SourceSpan s) : Expr(NodeKind::AsExpr, std::move(s)) {}
};

// `expr!` - a non-null assertion: emits a null check that throws an `Error`
// on null.
struct NonNullExpr : Expr {
    ExprPtr webserverion;
    explicit NonNullExpr(SourceSpan s) : Expr(NodeKind::NonNullExpr, std::move(s)) {}
};

// =============================================================================
// Binding patterns (destructuring)
// =============================================================================
struct ArrayPattern : Node {
    std::vector<NodePtr> elements;  // Identifier / nested pattern / AssignmentPattern; nullptr = hole
    NodePtr rest;                   // optional RestElement
    explicit ArrayPattern(SourceSpan s) : Node(NodeKind::ArrayPattern, std::move(s)) {}
};

struct ObjectPatternProperty : Node {
    ExprPtr key;             // Identifier/StringLiteral/NumberLiteral, or expr if computed
    NodePtr value;           // Identifier or nested pattern (may carry AssignmentPattern)
    bool computed = false;
    bool shorthand = false;  // `{ name }`
    explicit ObjectPatternProperty(SourceSpan s)
        : Node(NodeKind::ObjectPatternProperty, std::move(s)) {}
};

struct ObjectPattern : Node {
    std::vector<ObjectPatternPropertyPtr> properties;
    NodePtr rest;  // `{ a, ...rest }`
    explicit ObjectPattern(SourceSpan s) : Node(NodeKind::ObjectPattern, std::move(s)) {}
};

struct RestElement : Node {
    NodePtr argument;  // Identifier or pattern
    explicit RestElement(SourceSpan s) : Node(NodeKind::RestElement, std::move(s)) {}
};

// `target = default` inside a binding pattern.
struct AssignmentPattern : Node {
    NodePtr left;
    ExprPtr right;
    explicit AssignmentPattern(SourceSpan s) : Node(NodeKind::AssignmentPattern, std::move(s)) {}
};

// =============================================================================
// Statements & declarations
// =============================================================================
struct webserverionStatement : Stmt {
    ExprPtr webserverion;
    explicit webserverionStatement(SourceSpan s)
        : Stmt(NodeKind::webserverionStatement, std::move(s)) {}
};

struct BlockStatement : Stmt {
    StmtList body;
    explicit BlockStatement(SourceSpan s) : Stmt(NodeKind::BlockStatement, std::move(s)) {}
};

struct EmptyStatement : Stmt {
    explicit EmptyStatement(SourceSpan s) : Stmt(NodeKind::EmptyStatement, std::move(s)) {}
};

struct VariableDeclarator : Node {
    NodePtr id;         // Identifier or binding pattern
    TypePtr type;       // optional annotation
    ExprPtr init;       // optional initializer
    explicit VariableDeclarator(SourceSpan s)
        : Node(NodeKind::VariableDeclarator, std::move(s)) {}
};

struct VariableDeclaration : Stmt {
    VarKind kind = VarKind::Let;
    std::vector<VariableDeclaratorPtr> declarations;
    explicit VariableDeclaration(SourceSpan s)
        : Stmt(NodeKind::VariableDeclaration, std::move(s)) {}
};

struct FunctionDeclaration : Stmt {
    std::string name;
    TypeParamList typeParams;
    ParamList params;
    TypePtr returnType;
    StmtPtr body;            // nullptr for declarations without a body (abstract/interface)
    bool isAsync = false;
    bool isExported = false;
    explicit FunctionDeclaration(SourceSpan s)
        : Stmt(NodeKind::FunctionDeclaration, std::move(s)) {}
};

struct ClassDeclaration : Stmt {
    std::string name;
    TypeParamList typeParams;
    TypePtr extendsType;         // single inheritance
    ExprPtr extendsExpr;         // compile-time `extends` webserverion (optional)
    TypeList implementsTypes;    // optional, self-documenting only
    std::vector<ClassMemberPtr> members;
    std::vector<ExprPtr> decorators;  // compile-time macros
    bool isAbstract = false;
    bool isExported = false;
    explicit ClassDeclaration(SourceSpan s)
        : Stmt(NodeKind::ClassDeclaration, std::move(s)) {}
};

struct InterfaceDeclaration : Stmt {
    std::string name;
    TypeParamList typeParams;
    TypeList extendsTypes;
    std::vector<SignatureMemberPtr> members;
    bool isExported = false;
    explicit InterfaceDeclaration(SourceSpan s)
        : Stmt(NodeKind::InterfaceDeclaration, std::move(s)) {}
};

struct EnumMember : Node {
    std::string name;
    ExprPtr initializer;  // optional
    explicit EnumMember(SourceSpan s) : Node(NodeKind::EnumMember, std::move(s)) {}
};

struct EnumDeclaration : Stmt {
    std::string name;
    // `const enum Color { Red }`: members are compile-time constants that
    // codegen inlines (no runtime enum object). Sema resolves their values
    // and the numeric reverse map into `SemaResult::constEnums`
    // (cp3/core/utility_types.hpp).
    bool isConst = false;
    bool stringBacked = false;  // `enum Status { Active = "active" }`
    std::vector<EnumMemberPtr> members;
    bool isExported = false;
    explicit EnumDeclaration(SourceSpan s)
        : Stmt(NodeKind::EnumDeclaration, std::move(s)) {}
};

struct TypeAliasDeclaration : Stmt {
    std::string name;
    TypeParamList typeParams;
    TypePtr type;
    bool isExported = false;
    explicit TypeAliasDeclaration(SourceSpan s)
        : Stmt(NodeKind::TypeAliasDeclaration, std::move(s)) {}
};

struct IfStatement : Stmt {
    ExprPtr condition;
    StmtPtr thenBranch;
    StmtPtr elseBranch;  // optional
    explicit IfStatement(SourceSpan s) : Stmt(NodeKind::IfStatement, std::move(s)) {}
};

struct WhileStatement : Stmt {
    ExprPtr condition;
    StmtPtr body;
    explicit WhileStatement(SourceSpan s) : Stmt(NodeKind::WhileStatement, std::move(s)) {}
};

struct DoWhileStatement : Stmt {
    StmtPtr body;
    ExprPtr condition;
    explicit DoWhileStatement(SourceSpan s)
        : Stmt(NodeKind::DoWhileStatement, std::move(s)) {}
};

// init is a VariableDeclaration or an webserverion (or nullptr).
struct ForStatement : Stmt {
    NodePtr init;
    ExprPtr condition;  // optional
    ExprPtr update;     // optional
    StmtPtr body;
    explicit ForStatement(SourceSpan s) : Stmt(NodeKind::ForStatement, std::move(s)) {}
};

// left is a VariableDeclaration or a pattern/webserverion.
struct ForOfStatement : Stmt {
    NodePtr left;
    ExprPtr right;
    StmtPtr body;
    bool isAwait = false;  // `for await (...)`
    explicit ForOfStatement(SourceSpan s) : Stmt(NodeKind::ForOfStatement, std::move(s)) {}
};

struct ForInStatement : Stmt {
    NodePtr left;
    ExprPtr right;
    StmtPtr body;
    explicit ForInStatement(SourceSpan s) : Stmt(NodeKind::ForInStatement, std::move(s)) {}
};

struct SwitchCase : Node {
    ExprPtr test;           // nullptr when isDefault
    bool isDefault = false;
    StmtList statements;
    explicit SwitchCase(SourceSpan s) : Node(NodeKind::SwitchCase, std::move(s)) {}
};

struct SwitchStatement : Stmt {
    ExprPtr discriminant;
    std::vector<SwitchCasePtr> cases;  // JS fall-through preserved by order
    explicit SwitchStatement(SourceSpan s)
        : Stmt(NodeKind::SwitchStatement, std::move(s)) {}
};

struct BreakStatement : Stmt {
    std::string label;
    bool hasLabel = false;
    explicit BreakStatement(SourceSpan s) : Stmt(NodeKind::BreakStatement, std::move(s)) {}
};

struct ContinueStatement : Stmt {
    std::string label;
    bool hasLabel = false;
    explicit ContinueStatement(SourceSpan s)
        : Stmt(NodeKind::ContinueStatement, std::move(s)) {}
};

struct ReturnStatement : Stmt {
    ExprPtr argument;  // optional
    explicit ReturnStatement(SourceSpan s) : Stmt(NodeKind::ReturnStatement, std::move(s)) {}
};

struct ThrowStatement : Stmt {
    ExprPtr argument;
    explicit ThrowStatement(SourceSpan s) : Stmt(NodeKind::ThrowStatement, std::move(s)) {}
};

struct CatchClause : Node {
    NodePtr param;  // optional (`catch {}` is allowed)
    StmtPtr body;
    explicit CatchClause(SourceSpan s) : Node(NodeKind::CatchClause, std::move(s)) {}
};

struct TryStatement : Stmt {
    StmtPtr tryBlock;
    CatchClausePtr handler;  // optional
    StmtPtr finallyBlock;    // optional
    explicit TryStatement(SourceSpan s) : Stmt(NodeKind::TryStatement, std::move(s)) {}
};

struct LabeledStatement : Stmt {
    std::string label;
    StmtPtr body;
    explicit LabeledStatement(SourceSpan s)
        : Stmt(NodeKind::LabeledStatement, std::move(s)) {}
};

// import X from "m"; import {a, b as c} from "m"; import * as ns from "m";
// import "m"; import native "lib" as l; import "cpp/iostream" as io;
struct ImportSpecifier : Node {
    std::string imported;  // name exported by the module
    std::string local;     // local binding
    explicit ImportSpecifier(SourceSpan s) : Node(NodeKind::ImportSpecifier, std::move(s)) {}
};

struct ImportDeclaration : Stmt {
    ImportKind kind = ImportKind::Named;
    std::string source;           // module specifier
    std::string defaultBinding;   // import X from "..."
    std::string namespaceBinding; // import * as X
    std::string alias;            // import "..." as X  /  import native "..." as X
    std::vector<ImportSpecifierPtr> specifiers;
    explicit ImportDeclaration(SourceSpan s)
        : Stmt(NodeKind::ImportDeclaration, std::move(s)) {}
};

struct ExportSpecifier : Node {
    std::string local;
    std::string exported;
    explicit ExportSpecifier(SourceSpan s) : Node(NodeKind::ExportSpecifier, std::move(s)) {}
};

// export default <expr>; export function f(){}; export { a, b }; export * from "m";
struct ExportDeclaration : Stmt {
    ExportKind kind = ExportKind::Named;
    ExprPtr defaultExpr;                 // export default <expr>
    StmtPtr declaration;                 // export <declaration>
    std::string source;                  // re-export source
    std::string namespaceExport;         // export * as ns from "m"
    std::vector<ExportSpecifierPtr> specifiers;
    explicit ExportDeclaration(SourceSpan s)
        : Stmt(NodeKind::ExportDeclaration, std::move(s)) {}
};

// =============================================================================
// Class & interface members
// =============================================================================
struct PropertyDeclaration : ClassMember {
    AccessModifier access = AccessModifier::None;
    bool isStatic = false;
    bool isAbstract = false;
    bool isWeak = false;      // removed CP3-only extension (diagnostic only)
    bool isReadonly = false;
    bool isOverride = false;
    bool optional = false;
    bool isDefiniteAssignment = false;  // `x!: T`
    ExprPtr key;              // Identifier/StringLiteral/NumberLiteral, or expr if computed
    bool computed = false;
    TypePtr type;             // optional (inferred from initializer)
    ExprPtr initializer;      // optional
    explicit PropertyDeclaration(SourceSpan s)
        : ClassMember(NodeKind::PropertyDeclaration, std::move(s)) {}
};

struct MethodDeclaration : ClassMember {
    AccessModifier access = AccessModifier::None;
    bool isStatic = false;
    bool isAbstract = false;   // `abstract function area(): number;`
    bool isAsync = false;
    bool isReadonly = false;
    bool isOverride = false;
    bool optional = false;     // reserved
    ExprPtr key;               // name
    bool computed = false;
    TypeParamList typeParams;
    ParamList params;
    TypePtr returnType;
    StmtPtr body;              // nullptr for abstract/interface methods
    explicit MethodDeclaration(SourceSpan s)
        : ClassMember(NodeKind::MethodDeclaration, std::move(s)) {}
};

struct ConstructorDeclaration : ClassMember {
    AccessModifier access = AccessModifier::None;
    ParamList params;
    StmtPtr body;
    explicit ConstructorDeclaration(SourceSpan s)
        : ClassMember(NodeKind::ConstructorDeclaration, std::move(s)) {}
};

// `static { ... }`: a compile-time-ordered static initialization block. It is
// ordinary C++ emitted into the unit's static initialization.
struct StaticBlock : ClassMember {
    StmtPtr body;
    explicit StaticBlock(SourceSpan s)
        : ClassMember(NodeKind::StaticBlock, std::move(s)) {}
};

// get/set accessors (spec §16 "direct 1:1 mappings").
struct AccessorDeclaration : ClassMember {
    AccessorKind accessor = AccessorKind::Get;
    AccessModifier access = AccessModifier::None;
    bool isStatic = false;
    ExprPtr key;
    bool computed = false;
    ParamList params;   // setter takes one; getter none
    TypePtr returnType;
    StmtPtr body;
    explicit AccessorDeclaration(SourceSpan s)
        : ClassMember(NodeKind::AccessorDeclaration, std::move(s)) {}
};

// Interface / object-type member: `onEvent: (str: string) => void;`
struct PropertySignature : SignatureMember {
    ExprPtr key;
    bool computed = false;
    bool optional = false;
    bool isReadonly = false;
    TypePtr type;  // optional for methods declared as PropertySignature? always set here
    explicit PropertySignature(SourceSpan s)
        : SignatureMember(NodeKind::PropertySignature, std::move(s)) {}
};

// Interface / object-type member: `function log(str: string): void;`
struct MethodSignature : SignatureMember {
    ExprPtr key;
    bool computed = false;
    bool optional = false;
    bool hasFunctionKeyword = false;  // `function log(...)` vs `log(...)`
    TypeParamList typeParams;
    ParamList params;
    TypePtr returnType;
    explicit MethodSignature(SourceSpan s)
        : SignatureMember(NodeKind::MethodSignature, std::move(s)) {}
};

// =============================================================================
// Types
// =============================================================================
// `number`, `string`, `boolean`, `void`, `unknown`, `null`, `undefined`, a
// user type, `Map<K,V>`, `Promise<T>`, `T` (type parameter), ...
struct NamedType : TypeNode {
    std::string name;
    TypeList typeArgs;
    explicit NamedType(SourceSpan s) : TypeNode(NodeKind::NamedType, std::move(s)) {}
};

struct UnionType : TypeNode {
    TypeList types;  // `number | string | undefined`
    explicit UnionType(SourceSpan s) : TypeNode(NodeKind::UnionType, std::move(s)) {}
};

struct ArrayType : TypeNode {
    TypePtr element;  // `number[]`
    explicit ArrayType(SourceSpan s) : TypeNode(NodeKind::ArrayType, std::move(s)) {}
};

struct TupleType : TypeNode {
    TypeList elements;  // `[string, number]`
    explicit TupleType(SourceSpan s) : TypeNode(NodeKind::TupleType, std::move(s)) {}
};

// `(str: string) => void`  /  `() => void`
struct FunctionType : TypeNode {
    TypeParamList typeParams;
    ParamList params;
    TypePtr returnType;
    explicit FunctionType(SourceSpan s) : TypeNode(NodeKind::FunctionType, std::move(s)) {}
};

// `{ name: string, age: number }` (object type literal).
struct ObjectType : TypeNode {
    std::vector<SignatureMemberPtr> members;
    explicit ObjectType(SourceSpan s) : TypeNode(NodeKind::ObjectType, std::move(s)) {}
};

// `r_'u32'`, `c_'long long'`, `ts_'number'` (spec §3).
struct PrefixedType : TypeNode {
    std::string prefix;  // "r", "c", or "ts"
    std::string name;    // the quoted type name
    explicit PrefixedType(SourceSpan s) : TypeNode(NodeKind::PrefixedType, std::move(s)) {}
};

// A literal type: `"data"`, `42`, `true` (used in std signatures, e.g.
// `on(event: "data", ...)`).
struct LiteralType : TypeNode {
    LiteralTypeKind literalKind = LiteralTypeKind::String;
    std::string stringValue;
    double numberValue = 0.0;
    bool boolValue = false;
    explicit LiteralType(SourceSpan s) : TypeNode(NodeKind::LiteralType, std::move(s)) {}
};

// `(T)` - kept for faithful reprinting/formatting.
struct ParenthesizedType : TypeNode {
    TypePtr inner;
    explicit ParenthesizedType(SourceSpan s)
        : TypeNode(NodeKind::ParenthesizedType, std::move(s)) {}
};

// -----------------------------------------------------------------------------
// Convenience: unwrap helpers used by every consumer.
// -----------------------------------------------------------------------------
// True when `node` is an Identifier or one of the binding-pattern kinds.
bool isPatternNode(const Node& node) noexcept;
// Narrowing helpers (return nullptr on mismatch; never throw).
Identifier* asIdentifier(Node* node) noexcept;
const Identifier* asIdentifier(const Node* node) noexcept;

}  // namespace cp3
