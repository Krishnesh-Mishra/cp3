// =============================================================================
// src/cli/src/commands/lsp_intel_query.cpp - hover, outline and signature help.
//
// OWNERSHIP: cli module (src/cli/**). Implements the matching declarations in
// detail/lsp_intel.hpp.
//
// Every answer is read out of the analysis the checker itself produced: the
// AST for names and positions, `SemaResult::nodeTypes` for inferred types,
// `SemaResult::symbols` for declarations. Types the compiler describes with an
// internal placeholder (the ambient shape behind an unresolved import) are
// withheld instead of leaked, so the editor never shows a name the language
// does not have.
// =============================================================================
#include "detail/lsp/lsp_intel.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/lsp.hpp"
#include "cp3/core/source.hpp"
#include "cp3/core/types.hpp"

#include "detail/lsp/lsp_intel_docs.hpp"

namespace cp3::cli {
namespace {

// A literal has no name, no declaration and no documentation: there is
// nothing to describe at the cursor, so the hover stays silent.
bool isLiteralNode(const Node& node) {
    switch (node.kind) {
        case NodeKind::NumberLiteral:
        case NodeKind::StringLiteral:
        case NodeKind::BoolLiteral:
        case NodeKind::NullLiteral:
        case NodeKind::UndefinedLiteral:
            return true;
        default:
            return false;
    }
}

// The name a hover/definition works on. Empty for nodes that carry none.
std::string nameOfNode(const Node& node) {
    switch (node.kind) {
        case NodeKind::Identifier:
            return static_cast<const Identifier&>(node).name;
        case NodeKind::MemberExpr:
            return static_cast<const MemberExpr&>(node).property;
        case NodeKind::FunctionDeclaration:
            return static_cast<const FunctionDeclaration&>(node).name;
        case NodeKind::ClassDeclaration:
            return static_cast<const ClassDeclaration&>(node).name;
        case NodeKind::InterfaceDeclaration:
            return static_cast<const InterfaceDeclaration&>(node).name;
        case NodeKind::EnumDeclaration:
            return static_cast<const EnumDeclaration&>(node).name;
        case NodeKind::TypeAliasDeclaration:
            return static_cast<const TypeAliasDeclaration&>(node).name;
        case NodeKind::VariableDeclarator: {
            const auto& declarator = static_cast<const VariableDeclarator&>(node);
            if (declarator.id != nullptr &&
                declarator.id->kind == NodeKind::Identifier) {
                return static_cast<const Identifier&>(*declarator.id).name;
            }
            return std::string();
        }
        case NodeKind::MethodDeclaration: {
            const auto& method = static_cast<const MethodDeclaration&>(node);
            return keyName(method.key.get());
        }
        case NodeKind::PropertyDeclaration: {
            const auto& property = static_cast<const PropertyDeclaration&>(node);
            return keyName(property.key.get());
        }
        case NodeKind::AccessorDeclaration: {
            const auto& accessor = static_cast<const AccessorDeclaration&>(node);
            return keyName(accessor.key.get());
        }
        default:
            return std::string();
    }
}

// The nearest declaration of `name` at or before `offset` is `nearestSymbolAt`
// and the key name helper is `keyName` (both lsp_intel.cpp, shared with the
// definition and completion units).

void addEntry(std::vector<lsp::SymbolEntry>& out, const std::string& name,
              int kind, const SourceSpan& span) {
    if (name.empty() || !span.valid()) return;
    const SourceSpan selection = nameSpanIn(span, name);
    lsp::SymbolEntry entry;
    entry.name = name;
    entry.kind = kind;
    entry.line = selection.begin.line;
    entry.column = selection.begin.column;
    entry.endLine = span.end.line;
    entry.endColumn = span.end.column;
    out.push_back(std::move(entry));
}

// LSP SymbolKind numbers used here: Class 5, Method 6, Property 7,
// Constructor 9, Enum 10, Interface 11, Function 12, Variable 13,
// Constant 14, EnumMember 22, Struct 23 (type aliases).
constexpr int kKindClass = 5;
constexpr int kKindMethod = 6;
constexpr int kKindProperty = 7;
constexpr int kKindConstructor = 9;
constexpr int kKindEnum = 10;
constexpr int kKindInterface = 11;
constexpr int kKindFunction = 12;
constexpr int kKindVariable = 13;
constexpr int kKindConstant = 14;
constexpr int kKindEnumMember = 22;
constexpr int kKindTypeAlias = 23;

void collectClassMembers(const ClassDeclaration& declaration,
                         std::vector<lsp::SymbolEntry>& out) {
    for (const ClassMemberPtr& member : declaration.members) {
        if (member == nullptr) continue;
        switch (member->kind) {
            case NodeKind::PropertyDeclaration: {
                const auto& property = static_cast<const PropertyDeclaration&>(*member);
                addEntry(out, keyName(property.key.get()), kKindProperty,
                         property.span);
                break;
            }
            case NodeKind::MethodDeclaration: {
                const auto& method = static_cast<const MethodDeclaration&>(*member);
                addEntry(out, keyName(method.key.get()), kKindMethod, method.span);
                break;
            }
            case NodeKind::ConstructorDeclaration:
                addEntry(out, "constructor", kKindConstructor, member->span);
                break;
            case NodeKind::AccessorDeclaration: {
                const auto& accessor = static_cast<const AccessorDeclaration&>(*member);
                addEntry(out, keyName(accessor.key.get()), kKindProperty,
                         accessor.span);
                break;
            }
            default:
                break;
        }
    }
}

void collectSignatureMembers(const std::vector<SignatureMemberPtr>& members,
                             std::vector<lsp::SymbolEntry>& out) {
    for (const SignatureMemberPtr& member : members) {
        if (member == nullptr) continue;
        if (member->kind == NodeKind::PropertySignature) {
            const auto& property = static_cast<const PropertySignature&>(*member);
            addEntry(out, keyName(property.key.get()), kKindProperty, member->span);
        } else if (member->kind == NodeKind::MethodSignature) {
            const auto& method = static_cast<const MethodSignature&>(*member);
            addEntry(out, keyName(method.key.get()), kKindMethod, member->span);
        }
    }
}

// The body of a statement that can hold declarations (a loop body), or
// nullptr. The outline walks into it so a declaration inside a loop body is
// listed too.
const Stmt* nestedBodyOf(const Stmt& statement) {
    switch (statement.kind) {
        case NodeKind::WhileStatement:
            return static_cast<const WhileStatement&>(statement).body.get();
        case NodeKind::DoWhileStatement:
            return static_cast<const DoWhileStatement&>(statement).body.get();
        case NodeKind::ForStatement:
            return static_cast<const ForStatement&>(statement).body.get();
        case NodeKind::ForOfStatement:
            return static_cast<const ForOfStatement&>(statement).body.get();
        case NodeKind::ForInStatement:
            return static_cast<const ForInStatement&>(statement).body.get();
        default:
            return nullptr;
    }
}

void collectSymbols(const Stmt& statement, const SourceFile& file,
                    std::vector<lsp::SymbolEntry>& out) {
    if (!statement.span.valid() || statement.span.file != &file) return;
    switch (statement.kind) {
        case NodeKind::FunctionDeclaration: {
            const auto& function = static_cast<const FunctionDeclaration&>(statement);
            addEntry(out, function.name, kKindFunction, statement.span);
            // A declaration inside the body belongs to the outline too: a
            // module-like function lists the declarations it nests.
            if (function.body != nullptr) {
                collectSymbols(*function.body, file, out);
            }
            return;
        }
        case NodeKind::ClassDeclaration: {
            const auto& declaration = static_cast<const ClassDeclaration&>(statement);
            addEntry(out, declaration.name, kKindClass, statement.span);
            collectClassMembers(declaration, out);
            return;
        }
        case NodeKind::InterfaceDeclaration: {
            const auto& declaration = static_cast<const InterfaceDeclaration&>(statement);
            addEntry(out, declaration.name, kKindInterface, statement.span);
            collectSignatureMembers(declaration.members, out);
            return;
        }
        case NodeKind::EnumDeclaration: {
            const auto& declaration = static_cast<const EnumDeclaration&>(statement);
            addEntry(out, declaration.name, kKindEnum, statement.span);
            for (const EnumMemberPtr& member : declaration.members) {
                if (member != nullptr) {
                    addEntry(out, member->name, kKindEnumMember, member->span);
                }
            }
            return;
        }
        case NodeKind::TypeAliasDeclaration: {
            const auto& declaration = static_cast<const TypeAliasDeclaration&>(statement);
            addEntry(out, declaration.name, kKindTypeAlias, statement.span);
            return;
        }
        case NodeKind::VariableDeclaration: {
            const auto& declaration = static_cast<const VariableDeclaration&>(statement);
            const bool isConst = declaration.kind == VarKind::Const;
            for (const VariableDeclaratorPtr& declarator : declaration.declarations) {
                if (declarator == nullptr || declarator->id == nullptr) continue;
                if (declarator->id->kind != NodeKind::Identifier) continue;
                const auto& id = static_cast<const Identifier&>(*declarator->id);
                addEntry(out, id.name, isConst ? kKindConstant : kKindVariable,
                         declarator->span);
            }
            return;
        }
        case NodeKind::ExportDeclaration: {
            const auto& declaration = static_cast<const ExportDeclaration&>(statement);
            if (declaration.declaration != nullptr) {
                collectSymbols(*declaration.declaration, file, out);
            }
            return;
        }
        case NodeKind::BlockStatement: {
            const auto& block = static_cast<const BlockStatement&>(statement);
            for (const StmtPtr& child : block.body) {
                if (child != nullptr) collectSymbols(*child, file, out);
            }
            return;
        }
        case NodeKind::IfStatement: {
            const auto& branch = static_cast<const IfStatement&>(statement);
            if (branch.thenBranch != nullptr) {
                collectSymbols(*branch.thenBranch, file, out);
            }
            if (branch.elseBranch != nullptr) {
                collectSymbols(*branch.elseBranch, file, out);
            }
            return;
        }
        default:
            break;
    }
    // A loop body may hold declarations as well.
    if (const Stmt* body = nestedBodyOf(statement)) {
        collectSymbols(*body, file, out);
    }
}

// A call-like expression: `f(...)` and `new X(...)`.
// The innermost call-like expression whose span covers the cursor, or
// nullptr. The cursor may sit anywhere inside the call - including inside
// the callee itself - because signature help is also asked for there.
const Node* innermostCallAt(const Program& program, const SourceFile& file,
                            std::uint32_t line, std::uint32_t column) {
    const Node* best = nullptr;
    forEachNode(program, [&](const Node& node) {
        if (node.kind != NodeKind::CallExpr && node.kind != NodeKind::NewExpr) {
            return;
        }
        if (!spanContains(node.span, file, line, column)) return;
        if (best == nullptr || node.span.length() < best->span.length()) {
            best = &node;
        }
    });
    return best;
}

// The callee expression of a call-like expression (nullptr when absent).
const Expr* calleeOf(const Node& call) {
    if (call.kind == NodeKind::CallExpr) {
        return static_cast<const CallExpr&>(call).callee.get();
    }
    if (call.kind == NodeKind::NewExpr) {
        return static_cast<const NewExpr&>(call).callee.get();
    }
    return nullptr;
}

// Parameter helpers ------------------------------------------------------
// The binding name of a parameter: the identifier it binds, the argument of
// a rest parameter, nothing for a pattern.
std::string parameterBindingName(const Parameter& parameter) {
    const Node* name = parameter.name.get();
    if (name == nullptr) return std::string();
    if (name->kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*name).name;
    }
    if (name->kind == NodeKind::RestElement) {
        const auto& rest = static_cast<const RestElement&>(*name);
        if (rest.argument != nullptr && rest.argument->kind == NodeKind::Identifier) {
            return static_cast<const Identifier&>(*rest.argument).name;
        }
    }
    return std::string();
}

// The parameter list of a function-like declaration node, or nullptr. A
// variable declarator holding an arrow resolves through its initializer.
const ParamList* paramsOfDeclaration(const Node& node) {
    switch (node.kind) {
        case NodeKind::FunctionDeclaration:
            return &static_cast<const FunctionDeclaration&>(node).params;
        case NodeKind::ArrowFunction:
            return &static_cast<const ArrowFunction&>(node).params;
        case NodeKind::MethodDeclaration:
            return &static_cast<const MethodDeclaration&>(node).params;
        case NodeKind::ConstructorDeclaration:
            return &static_cast<const ConstructorDeclaration&>(node).params;
        case NodeKind::VariableDeclarator: {
            const auto& declarator = static_cast<const VariableDeclarator&>(node);
            if (declarator.init != nullptr &&
                declarator.init->kind == NodeKind::ArrowFunction) {
                return &static_cast<const ArrowFunction&>(*declarator.init).params;
            }
            return nullptr;
        }
        default:
            return nullptr;
    }
}

// The declared return type of a function-like declaration, or nullptr.
const TypePtr* returnTypeOfDeclaration(const Node& node) {
    switch (node.kind) {
        case NodeKind::FunctionDeclaration:
            return &static_cast<const FunctionDeclaration&>(node).returnType;
        case NodeKind::ArrowFunction:
            return &static_cast<const ArrowFunction&>(node).returnType;
        case NodeKind::MethodDeclaration:
            return &static_cast<const MethodDeclaration&>(node).returnType;
        default:
            return nullptr;
    }
}

// --- a local renderer for declared-type syntax -------------------------
// The compiler publishes resolved types for every declaration the checker
// saw (see `SemaResult::nodeTypes`), but a constructor's parameters have no
// published type, so their annotations are rendered here, directly from the
// TypeNode tree. It mirrors the compiler's own `describe()` spelling for
// the shapes that occur in annotations and falls back to "unknown" for the
// rest.
std::string describeTypeSyntax(const TypeNode& node);

std::string describeParameterSyntax(const Parameter& parameter) {
    std::string out;
    if (parameter.isRest) out += "...";
    const std::string name = parameterBindingName(parameter);
    out += name.empty() ? "arg" : name;
    if (!parameter.isRest &&
        (parameter.optional || parameter.defaultValue != nullptr)) {
        out += "?";
    }
    out += ": ";
    out += parameter.type != nullptr ? describeTypeSyntax(*parameter.type)
                                     : std::string("unknown");
    return out;
}

// The members of a structural value (an interface's members, an inline object
// type's members), rendered the way the hover shows them: `name: type` and
// `method(params): type`.
std::string describeSignatureMembers(const std::vector<SignatureMemberPtr>& members) {
    std::string out = "{ ";
    bool first = true;
    for (const SignatureMemberPtr& member : members) {
        if (member == nullptr) continue;
        if (member->kind == NodeKind::PropertySignature) {
            const auto& property = static_cast<const PropertySignature&>(*member);
            const std::string key = keyName(property.key.get());
            if (key.empty()) continue;
            if (!first) out += "; ";
            first = false;
            out += key;
            if (property.optional) out += "?";
            out += ": ";
            out += property.type != nullptr ? describeTypeSyntax(*property.type)
                                            : std::string("unknown");
        } else if (member->kind == NodeKind::MethodSignature) {
            const auto& method = static_cast<const MethodSignature&>(*member);
            const std::string key = keyName(method.key.get());
            if (key.empty()) continue;
            if (!first) out += "; ";
            first = false;
            out += key;
            out += "(";
            for (std::size_t i = 0; i < method.params.size(); ++i) {
                if (i != 0) out += ", ";
                out += method.params[i] != nullptr
                           ? describeParameterSyntax(*method.params[i])
                           : std::string("unknown");
            }
            out += ")";
            if (method.returnType != nullptr) {
                out += ": " + describeTypeSyntax(*method.returnType);
            }
        }
    }
    out += first ? "}" : " }";
    return out;
}

std::string describeTypeSyntax(const TypeNode& node) {
    switch (node.kind) {
        case NodeKind::NamedType: {            const auto& named = static_cast<const NamedType&>(node);
            std::string out = named.name;
            if (!named.typeArgs.empty()) {
                out += "<";
                for (std::size_t i = 0; i < named.typeArgs.size(); ++i) {
                    if (i != 0) out += ", ";
                    out += named.typeArgs[i] != nullptr
                               ? describeTypeSyntax(*named.typeArgs[i])
                               : "unknown";
                }
                out += ">";
            }
            return out;
        }
        case NodeKind::UnionType: {
            const auto& unionNode = static_cast<const UnionType&>(node);
            std::string out;
            for (std::size_t i = 0; i < unionNode.types.size(); ++i) {
                if (i != 0) out += " | ";
                out += unionNode.types[i] != nullptr
                           ? describeTypeSyntax(*unionNode.types[i])
                           : "unknown";
            }
            return out.empty() ? std::string("unknown") : out;
        }
        case NodeKind::ArrayType:
            if (const TypePtr& element =
                    static_cast<const ArrayType&>(node).element) {
                return describeTypeSyntax(*element) + "[]";
            }
            return "unknown[]";
        case NodeKind::TupleType: {
            const auto& tuple = static_cast<const TupleType&>(node);
            std::string out = "[";
            for (std::size_t i = 0; i < tuple.elements.size(); ++i) {
                if (i != 0) out += ", ";
                out += tuple.elements[i] != nullptr
                           ? describeTypeSyntax(*tuple.elements[i])
                           : "unknown";
            }
            return out + "]";
        }
        case NodeKind::FunctionType: {
            const auto& fn = static_cast<const FunctionType&>(node);
            std::string out = "(";
            for (std::size_t i = 0; i < fn.params.size(); ++i) {
                if (i != 0) out += ", ";
                out += fn.params[i] != nullptr
                           ? describeParameterSyntax(*fn.params[i])
                           : std::string("unknown");
            }
            out += ") => ";
            out += fn.returnType != nullptr
                       ? describeTypeSyntax(*fn.returnType)
                       : std::string("unknown");
            return out;
        }
        case NodeKind::ObjectType: {
            const auto& object = static_cast<const ObjectType&>(node);
            return describeSignatureMembers(object.members);
        }
        case NodeKind::ParenthesizedType: {
            const auto& inner = static_cast<const ParenthesizedType&>(node).inner;
            return inner != nullptr ? describeTypeSyntax(*inner)
                                    : std::string("unknown");
        }
        case NodeKind::LiteralType: {
            const auto& literal = static_cast<const LiteralType&>(node);
            switch (literal.literalKind) {
                case LiteralTypeKind::String:
                    return "\"" + literal.stringValue + "\"";
                case LiteralTypeKind::Number:
                    return literal.numberRaw.empty()
                               ? std::to_string(literal.numberValue)
                               : literal.numberRaw;
                case LiteralTypeKind::Bool:
                    return literal.boolValue ? "true" : "false";
            }
            return "unknown";
        }
        case NodeKind::PrefixedType: {
            const auto& prefix = static_cast<const PrefixedType&>(node);
            return prefix.prefix + "_'" + prefix.name + "'";
        }
        default:
            return "unknown";
    }
}

// The parameter text of a resolved function-type part, with the rest
// marker: a rest parameter typed `T[]` by sema renders as `...items: T[]`;
// the fixed-arity types store the element type, so `[]` is appended there.
std::string describeParameter(const Parameter& parameter, const Type* resolved) {
    std::string out;
    if (parameter.isRest) out += "...";
    const std::string name = parameterBindingName(parameter);
    out += name.empty() ? "arg" : name;
    if (!parameter.isRest &&
        (parameter.optional || parameter.defaultValue != nullptr)) {
        out += "?";
    }
    out += ": ";
    if (resolved != nullptr) {
        std::string text = describe(*resolved);
        if (parameter.isRest && resolved->kind != TypeKind::Array) {
            text += "[]";
        }
        out += std::move(text);
        return out;
    }
    out += parameter.type != nullptr ? describeTypeSyntax(*parameter.type)
                                     : std::string("unknown");
    return out;
}

// --- resolving the callee declaration -----------------------------------
// What the compiler can see of the callee's declaration: the declaring node
// (a function-like declaration the call goes to) and its node id in the
// ENTRY unit (node ids are only meaningful there; imported units have their
// own numbering, so an imported declaration reports 0 and the doc tables
// are consulted by name instead).
struct CalleeDeclaration {
    const Node* node = nullptr;
    std::uint32_t entryNodeId = 0;
    std::string name;      // how the signature renders the callee
    bool isConstructor = false;
};

const Node* findNodeById(const Program& program, std::uint32_t nodeId) {
    if (nodeId == 0) return nullptr;
    const Node* found = nullptr;
    forEachNode(program, [&](const Node& node) {
        if (found != nullptr || node.nodeId != nodeId) return;
        found = &node;
    });
    return found;
}

const Node* unwrapToFunctionLike(const Node* node) {
    if (node == nullptr) return nullptr;
    switch (node->kind) {
        case NodeKind::FunctionDeclaration:
        case NodeKind::ArrowFunction:
        case NodeKind::MethodDeclaration:
        case NodeKind::ConstructorDeclaration:
            return node;
        case NodeKind::VariableDeclarator: {
            const auto& declarator = static_cast<const VariableDeclarator&>(*node);
            if (declarator.init != nullptr &&
                declarator.init->kind == NodeKind::ArrowFunction) {
                return declarator.init.get();
            }
            return nullptr;
        }
        default:
            return nullptr;
    }
}

// Every program that may carry the declaration, entry first.
std::vector<const Program*> candidatePrograms(const AnalyzedDocument& document) {
    std::vector<const Program*> programs;
    if (document.entry != nullptr && document.entry->ast != nullptr) {
        programs.push_back(document.entry->ast.get());
    }
    if (document.graph != nullptr) {
        for (const CompilationUnit* unit : document.graph->units()) {
            if (unit == nullptr || unit->ast == nullptr) continue;
            if (unit->ast == document.entry->ast) continue;
            programs.push_back(unit->ast.get());
        }
    }
    return programs;
}

const Node* findFunctionLikeByName(const AnalyzedDocument& document,
                                   const std::string& name) {
    for (const Program* program : candidatePrograms(document)) {
        const Node* found = nullptr;
        forEachNode(*program, [&](const Node& node) {
            if (found != nullptr) return;
            if (node.kind == NodeKind::FunctionDeclaration) {
                if (static_cast<const FunctionDeclaration&>(node).name == name) {
                    found = &node;
                }
                return;
            }
            if (node.kind == NodeKind::VariableDeclarator) {
                const auto& declarator = static_cast<const VariableDeclarator&>(node);
                if (declarator.init == nullptr ||
                    declarator.init->kind != NodeKind::ArrowFunction) {
                    return;
                }
                if (declarator.id != nullptr &&
                    declarator.id->kind == NodeKind::Identifier &&
                    static_cast<const Identifier&>(*declarator.id).name == name) {
                    found = declarator.init.get();
                }
            }
        });
        if (found != nullptr) return found;
    }
    return nullptr;
}

const ClassDeclaration* findClassByName(const AnalyzedDocument& document,
                                        const std::string& name) {
    for (const Program* program : candidatePrograms(document)) {
        const ClassDeclaration* found = nullptr;
        forEachNode(*program, [&](const Node& node) {
            if (found != nullptr || node.kind != NodeKind::ClassDeclaration) {
                return;
            }
            const auto& declaration = static_cast<const ClassDeclaration&>(node);
            if (declaration.name == name) found = &declaration;
        });
        if (found != nullptr) return found;
    }
    return nullptr;
}

// The interface a name refers to, in this file or in an imported module.
const InterfaceDeclaration* findInterfaceByName(const AnalyzedDocument& document,
                                                const std::string& name) {
    for (const Program* program : candidatePrograms(document)) {
        const InterfaceDeclaration* found = nullptr;
        forEachNode(*program, [&](const Node& node) {
            if (found != nullptr || node.kind != NodeKind::InterfaceDeclaration) {
                return;
            }
            const auto& declaration = static_cast<const InterfaceDeclaration&>(node);
            if (declaration.name == name) found = &declaration;
        });
        if (found != nullptr) return found;
    }
    return nullptr;
}

// The base class a class declaration extends, when it is a plain name.
std::string baseClassNameOf(const ClassDeclaration& declaration) {
    if (declaration.extendsType == nullptr) return std::string();
    if (declaration.extendsType->kind != NodeKind::NamedType) return std::string();
    return static_cast<const NamedType&>(*declaration.extendsType).name;
}

// The explicit constructor / the method named `member` of the class, walking
// the base chain when the member is inherited.
const Node* findClassMemberByName(const AnalyzedDocument& document,
                                  const std::string& className,
                                  const std::string& member, bool constructor) {
    const ClassDeclaration* current = findClassByName(document, className);
    int depth = 0;
    while (current != nullptr && depth++ < 8) {
        for (const ClassMemberPtr& memberPtr : current->members) {
            if (memberPtr == nullptr) continue;
            if (constructor) {
                if (memberPtr->kind == NodeKind::ConstructorDeclaration) {
                    return memberPtr.get();
                }
                continue;
            }
            if (memberPtr->kind != NodeKind::MethodDeclaration) continue;
            const auto& method = static_cast<const MethodDeclaration&>(*memberPtr);
            if (keyName(method.key.get()) == member) return memberPtr.get();
        }
        const std::string base = baseClassNameOf(*current);
        current = base.empty() ? nullptr : findClassByName(document, base);
    }
    return nullptr;
}

// The declaration of the callee behind `callee`, or none when the compiler
// cannot see one (ambient surfaces, unresolved imports).
CalleeDeclaration resolveCalleeDeclaration(const AnalyzedDocument& document,
                                           const Node& callee,
                                           const SymbolInfo* symbol,
                                           bool isNew) {
    CalleeDeclaration out;
    const std::string name = nameOfNode(callee);
    out.name = name;
    if (name.empty()) return out;
    // `new Widget(...)` and `new UE.Widget(...)`: the class declaration is
    // the target. The constructor's own node id wins for the docs.
    if (isNew) {
        out.isConstructor = true;
        const Node* ctor = findClassMemberByName(document, name, name, true);
        if (ctor == nullptr) return out;
        out.node = ctor;
        if (ctor->span.valid() && ctor->span.file == document.file) {
            out.entryNodeId = ctor->nodeId;
        }
        return out;
    }
    // A method call (`receiver.name(...)`, including a static through the
    // class name): find the enclosing class of the receiver by name.
    if (callee.kind == NodeKind::MemberExpr) {
        const auto& member = static_cast<const MemberExpr&>(callee);
        std::string className;
        const Type* receiver =
            member.object != nullptr
                ? document.sema.typeOf(member.object->nodeId)
                : nullptr;
        if (receiver != nullptr && receiver->kind == TypeKind::Class &&
            !receiver->name.empty()) {
            className = receiver->name;
        } else if (member.object != nullptr &&
                   member.object->kind == NodeKind::Identifier) {
            // The receiver may still be a class binding (statics): the
            // class is found by name even when sema spelled the binding's
            // type differently.
            const auto& id = static_cast<const Identifier&>(*member.object);
            if (findClassByName(document, id.name) != nullptr) {
                className = id.name;
            }
        }
        if (!className.empty()) {
            const Node* method = findClassMemberByName(document, className,
                                                       member.property, false);
            if (method != nullptr) {
                out.node = method;
                if (method->span.valid() && method->span.file == document.file) {
                    out.entryNodeId = method->nodeId;
                }
            }
        }
        return out;
    }
    // A plain identifier callee: the binding table points at the declaring
    // node in the entry unit; a callee declared in an imported unit has no
    // entry node, so it is resolved by name across the module graph.
    if (symbol != nullptr && document.entry != nullptr &&
        document.entry->ast != nullptr) {
        if (const Node* exact =
                unwrapToFunctionLike(findNodeById(*document.entry->ast, symbol->declNodeId))) {
            out.node = exact;
            out.entryNodeId = exact->nodeId;
            return out;
        }
    }
    if (const Node* found = findFunctionLikeByName(document, name)) {
        out.node = found;
        if (found->span.valid() && found->span.file == document.file) {
            out.entryNodeId = found->nodeId;
        }
    }
    return out;
}

// --- std surfaces ------------------------------------------------------
// One parameter of a documented std signature: the text as written, its
// name, and the rest/optional markers.
struct StdParameter {
    std::string text;      // the parameter text as written ("...values: number[]")
    std::string name;      // the parameter name ("values")
    bool rest = false;
    bool optional = false;
};

std::string trimCopy(const std::string& text) {
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return std::string();
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

// Splits a top-level list at its commas: `(`, `[`, `{` and `<` nest, as do
// quoted strings.
std::vector<std::string> splitTopLevel(const std::string& inner) {
    std::vector<std::string> parts;
    std::string current;
    int depth = 0;
    char quote = '\0';
    for (std::size_t at = 0; at < inner.size(); ++at) {
        const char c = inner[at];
        if (quote != '\0') {
            current += c;
            if (c == '\\' && at + 1 < inner.size()) {
                current += inner[++at];
                continue;
            }
            if (c == quote) quote = '\0';
            continue;
        }
        if (c == '"' || c == '\'' || c == '`') {
            quote = c;
            current += c;
            continue;
        }
        if (c == '(' || c == '[' || c == '{' || c == '<') {
            ++depth;
            current += c;
            continue;
        }
        if (c == ')' || c == ']' || c == '}' || c == '>') {
            if (depth > 0) --depth;
            current += c;
            continue;
        }
        if (c == ',' && depth == 0) {
            parts.push_back(trimCopy(current));
            current.clear();
            continue;
        }
        current += c;
    }
    const std::string tail = trimCopy(current);
    if (!tail.empty() || !parts.empty()) parts.push_back(tail);
    return parts;
}

StdParameter parseStdParameter(const std::string& segment) {
    StdParameter parameter;
    std::string s = trimCopy(segment);
    parameter.text = s;
    if (s.compare(0, 3, "...") == 0) {
        parameter.rest = true;
        s = trimCopy(s.substr(3));
    }
    const std::size_t question = s.find('?');
    const std::size_t colon = s.find(':');
    if (question != std::string::npos &&
        (colon == std::string::npos || question < colon)) {
        parameter.optional = true;
    }
    std::size_t end = s.size();
    if (question != std::string::npos) end = std::min(end, question);
    if (colon != std::string::npos) end = std::min(end, colon);
    std::string name = trimCopy(s.substr(0, end));
    if (name.compare(0, 4, "ref ") == 0) name = trimCopy(name.substr(4));
    parameter.name = name.empty() ? std::string("arg") : std::move(name);
    return parameter;
}

// The pieces of a documented signature such as "max(...values: number[]): number"
// or "stringify<T>(value: T, replacer?: ...): string". The documented tables
// carry the signature as text; it is parsed here once, at the call site.
struct StdSignature {
    std::vector<StdParameter> params;
    std::string returnType;
};

bool parseStdSignature(const std::string& signature, StdSignature& out) {
    const std::size_t open = [&] {
        int angle = 0;
        int paren = 0;
        for (std::size_t at = 0; at < signature.size(); ++at) {
            const char c = signature[at];
            if (c == '<') {
                ++angle;
                continue;
            }
            if (c == '>') {
                if (angle > 0) --angle;
                continue;
            }
            if (c == '(') {
                if (angle == 0 && paren == 0) return at;
                ++paren;
                continue;
            }
            if (c == ')') {
                if (paren > 0) --paren;
            }
        }
        return std::string::npos;
    }();
    if (open == std::string::npos) return false;
    int depth = 0;
    std::size_t close = std::string::npos;
    char quote = '\0';
    for (std::size_t at = open + 1; at < signature.size(); ++at) {
        const char c = signature[at];
        if (quote != '\0') {
            if (c == '\\' && at + 1 < signature.size()) {
                ++at;
                continue;
            }
            if (c == quote) quote = '\0';
            continue;
        }
        if (c == '"' || c == '\'' || c == '`') {
            quote = c;
            continue;
        }
        if (c == '(' || c == '[' || c == '{' || c == '<') {
            ++depth;
            continue;
        }
        if (c == ')' || c == ']' || c == '}' || c == '>') {
            if (c == ')' && depth == 0) {
                close = at;
                break;
            }
            if (depth > 0) --depth;
        }
    }
    if (close == std::string::npos) return false;
    const std::string inner = signature.substr(open + 1, close - open - 1);
    for (const std::string& segment : splitTopLevel(inner)) {
        if (!segment.empty()) out.params.push_back(parseStdParameter(segment));
    }
    std::string tail = trimCopy(signature.substr(close + 1));
    if (!tail.empty() && tail.front() == ':') tail = trimCopy(tail.substr(1));
    out.returnType = tail;
    return true;
}

// The documentation the editor shows for a call: the declaration's own doc
// comment when the declaration is in the entry unit (the docs table is keyed
// by its node id), or the shared documentation walk (the std surface's own
// description, the doc comment of the declaration the name refers to).
std::string calleeDocumentation(const AnalyzedDocument& document,
                                const Node& callee,
                                const CalleeDeclaration& declaration,
                                const std::string& name,
                                const SymbolInfo* symbol) {
    if (declaration.entryNodeId != 0) {
        const auto found = document.sema.docs.find(declaration.entryNodeId);
        if (found != document.sema.docs.end()) {
            return renderDocComment(found->second);
        }
    }
    const DocAnswer doc = documentationAt(document, callee, name, symbol);
    return doc.signature.empty() ? doc.body : renderStdDoc(doc.signature, doc.body);
}

// A signature synthesized from the std surface's own documented signature
// (the same table the hover and the completion popups use). False when the
// member is nowhere in the tables.
bool stdCalleeAnswer(const AnalyzedDocument& document, const Node& callee,
                     lsp::SignatureAnswer& answer) {
    if (callee.kind != NodeKind::MemberExpr) return false;
    const auto& member = static_cast<const MemberExpr&>(callee);
    const DocAnswer doc = stdMemberDocAt(document, member);
    if (doc.signature.empty()) return false;
    StdSignature parsed;
    if (!parseStdSignature(doc.signature, parsed)) return false;
    answer.label = doc.signature;
    for (const StdParameter& parameter : parsed.params) {
        answer.parameters.push_back(parameter.text);
    }
    answer.documentation = renderStdDoc(doc.signature, doc.body);
    return true;
}

constexpr std::uint32_t kNoOffset = 0xFFFFFFFFu;

// True when the type the checker published for this node carries no
// hover-worthy information: `never` is published as the empty union. (Unknown,
// Error and the internal $ambient placeholder are already withheld by
// isPlaceholderType; this extends the same courtesy to `never`.)
bool neverType(const Type& type) {
    return type.kind == TypeKind::Union && type.parts.empty();
}

// The '(' that opens the call's argument list, scanning over an explicit
// generics list (`f<number>(...)`) and whitespace.
std::uint32_t openParenAfter(const std::string& text, std::uint32_t from,
                             std::uint32_t limit) {
    for (std::uint32_t at = from; at < limit && at < text.size(); ++at) {
        const char c = text[at];
        if (c == '(') return at;
        if (c == ' ' || c == '\t') continue;
        if (c == '<') {
            int depth = 1;
            while (++at < limit && at < text.size() && depth > 0) {
                if (text[at] == '<') ++depth;
                else if (text[at] == '>') --depth;
            }
            continue;
        }
        return kNoOffset;
    }
    return kNoOffset;
}

// The index just past a quoted literal starting at `at` (the quote itself).
std::uint32_t skipLiteral(const std::string& text, std::uint32_t at) {
    const char quote = text[at];
    for (std::uint32_t i = at + 1; i < text.size(); ++i) {
        if (text[i] == '\\') {
            ++i;
            continue;
        }
        if (text[i] == quote) return i;
    }
    return kNoOffset;
}

// 0-based index of the argument the cursor sits in: the number of top-level
// commas between the open paren and the cursor. Best effort on templates.
int countArguments(const std::string& text, std::uint32_t open,
                   std::uint32_t cursor) {
    int depth = 0;
    int commas = 0;
    for (std::uint32_t at = open + 1; at < cursor && at < text.size(); ++at) {
        const char c = text[at];
        if (c == '"' || c == '\'' || c == '`') {
            const std::uint32_t end = skipLiteral(text, at);
            if (end == kNoOffset) break;
            at = end;
            continue;
        }
        if (c == '/' && at + 1 < text.size() && text[at + 1] == '/') break;
        if (c == '/' && at + 1 < text.size() && text[at + 1] == '*') {
            const std::size_t end = text.find("*/", at + 2);
            if (end == std::string::npos) break;
            at = static_cast<std::uint32_t>(end + 1);
            continue;
        }
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            if (depth > 0) --depth;
        } else if (c == ',' && depth == 0) {
            ++commas;
        }
    }
    return commas;
}

}  // namespace

const Type* expectedArgumentTypeAt(const AnalyzedDocument& document,
                                   std::uint32_t cursor) {
    if (!analysisReady(document)) return nullptr;
    const CallExpr* best = nullptr;
    forEachNode(*document.entry->ast, [&](const Node& node) {
        if (node.kind != NodeKind::CallExpr || !node.span.valid() ||
            node.span.file != document.file || cursor < node.span.begin.offset ||
            cursor > node.span.end.offset) {
            return;
        }
        const auto& call = static_cast<const CallExpr&>(node);
        if (call.callee == nullptr || !call.callee->span.valid() ||
            cursor < call.callee->span.end.offset) {
            return;
        }
        if (best == nullptr || call.span.length() < best->span.length()) {
            best = &call;
        }
    });
    if (best == nullptr || best->callee == nullptr) return nullptr;
    const std::uint32_t open = openParenAfter(
        document.file->text, best->callee->span.end.offset, cursor);
    if (open == kNoOffset) return nullptr;
    const int argument = countArguments(document.file->text, open, cursor);
    if (argument < 0) return nullptr;
    const Type* callee = document.sema.typeOf(best->callee->nodeId);
    if (callee == nullptr || callee->kind != TypeKind::Function ||
        static_cast<std::size_t>(argument) + 1 >= callee->parts.size()) {
        return nullptr;
    }
    const Type* expected = &callee->parts[static_cast<std::size_t>(argument)];
    if (expected->isUnknown() || expected->isError() ||
        (expected->kind == TypeKind::Object && expected->name == "$ambient")) {
        return nullptr;
    }
    return expected;
}

// The declaration an import binds, in the imported module: the export the
// import names. Null when the name is not imported from a local module.
const Node* importedDeclarationFor(const AnalyzedDocument& document,
                                   const std::string& name) {
    if (document.entry == nullptr || document.entry->ast == nullptr ||
        document.graph == nullptr || document.file == nullptr) {
        return nullptr;
    }
    const ImportDeclaration* import =
        importForName(*document.entry->ast, name, *document.file);
    if (import == nullptr) return nullptr;
    const ModuleDependency* dependency =
        dependencyFor(*document.graph, document.entry->id, import->source);
    if (dependency == nullptr || !dependency->resolved ||
        dependency->resolution != ModuleResolution::Local) {
        return nullptr;
    }
    CompilationUnit* unit = document.graph->unit(dependency->imported);
    if (unit == nullptr || unit->ast == nullptr) return nullptr;
    const bool wantDefault = import->defaultBinding == name;
    std::string importedName;
    bool matched = wantDefault;
    for (const ImportSpecifierPtr& specifier : import->specifiers) {
        if (specifier == nullptr) continue;
        const std::string& local =
            specifier->local.empty() ? specifier->imported : specifier->local;
        if (local != name) continue;
        importedName = specifier->imported;
        matched = true;
        break;
    }
    if (!matched) return nullptr;
    return findExported(*unit->ast, importedName, wantDefault).node;
}

// The hover's text for a declaration that lives in another module: the
// declaration as it is written there.
std::string describeImportedDeclaration(const Node& declaration,
                                        const std::string& name) {
    switch (declaration.kind) {
        case NodeKind::ClassDeclaration: {
            const auto& cls = static_cast<const ClassDeclaration&>(declaration);
            return name + ": " + cls.name;
        }
        case NodeKind::InterfaceDeclaration: {
            const auto& interface =
                static_cast<const InterfaceDeclaration&>(declaration);
            return name + ": " + describeSignatureMembers(interface.members);
        }
        case NodeKind::EnumDeclaration: {
            const auto& enumeration = static_cast<const EnumDeclaration&>(declaration);
            return name + ": " + enumeration.name;
        }
        case NodeKind::TypeAliasDeclaration: {
            const auto& alias = static_cast<const TypeAliasDeclaration&>(declaration);
            return name + ": " + (alias.type != nullptr
                                      ? describeTypeSyntax(*alias.type)
                                      : std::string("unknown"));
        }
        case NodeKind::FunctionDeclaration: {
            const auto& function = static_cast<const FunctionDeclaration&>(declaration);
            std::string out = name + "(";
            for (std::size_t i = 0; i < function.params.size(); ++i) {
                if (i != 0) out += ", ";
                out += function.params[i] != nullptr
                           ? describeParameterSyntax(*function.params[i])
                           : std::string("unknown");
            }
            out += ")";
            if (function.returnType != nullptr) {
                out += ": " + describeTypeSyntax(*function.returnType);
            }
            return out;
        }
        case NodeKind::VariableDeclaration: {
            const auto& variable = static_cast<const VariableDeclaration&>(declaration);
            for (const VariableDeclaratorPtr& declarator : variable.declarations) {
                if (declarator == nullptr || declarator->id == nullptr) continue;
                if (declarator->id->kind != NodeKind::Identifier) continue;
                if (static_cast<const Identifier&>(*declarator->id).name != name) {
                    continue;
                }
                if (declarator->type != nullptr) {
                    return name + ": " + describeTypeSyntax(*declarator->type);
                }
                return name;
            }
            return name;
        }
        default:
            return name;
    }
}

// The hover's text for a member of a class or interface, in this file or in
// an imported module.
std::string describeMemberOf(const Node& owner, const std::string& member) {
    if (owner.kind == NodeKind::ClassDeclaration) {
        const auto& cls = static_cast<const ClassDeclaration&>(owner);
        for (const ClassMemberPtr& declaration : cls.members) {
            if (declaration == nullptr) continue;
            if (declaration->kind == NodeKind::PropertyDeclaration) {
                const auto& property =
                    static_cast<const PropertyDeclaration&>(*declaration);
                if (keyName(property.key.get()) != member) continue;
                return member + ": " +
                       (property.type != nullptr ? describeTypeSyntax(*property.type)
                                                 : std::string("unknown"));
            }
            if (declaration->kind == NodeKind::MethodDeclaration) {
                const auto& method = static_cast<const MethodDeclaration&>(*declaration);
                if (keyName(method.key.get()) != member) continue;
                std::string out = member + "(";
                for (std::size_t i = 0; i < method.params.size(); ++i) {
                    if (i != 0) out += ", ";
                    out += method.params[i] != nullptr
                               ? describeParameterSyntax(*method.params[i])
                               : std::string("unknown");
                }
                out += ")";
                if (method.returnType != nullptr) {
                    out += ": " + describeTypeSyntax(*method.returnType);
                }
                return out;
            }
        }
        return member;
    }
    if (owner.kind == NodeKind::InterfaceDeclaration) {
        const auto& interface = static_cast<const InterfaceDeclaration&>(owner);
        for (const SignatureMemberPtr& declaration : interface.members) {
            if (declaration == nullptr) continue;
            if (declaration->kind == NodeKind::PropertySignature) {
                const auto& property = static_cast<const PropertySignature&>(*declaration);
                if (keyName(property.key.get()) != member) continue;
                return member + ": " +
                       (property.type != nullptr ? describeTypeSyntax(*property.type)
                                                 : std::string("unknown"));
            }
            if (declaration->kind == NodeKind::MethodSignature) {
                const auto& method = static_cast<const MethodSignature&>(*declaration);
                if (keyName(method.key.get()) != member) continue;
                std::string out = member + "(";
                for (std::size_t i = 0; i < method.params.size(); ++i) {
                    if (i != 0) out += ", ";
                    out += method.params[i] != nullptr
                               ? describeParameterSyntax(*method.params[i])
                               : std::string("unknown");
                }
                out += ")";
                if (method.returnType != nullptr) {
                    out += ": " + describeTypeSyntax(*method.returnType);
                }
                return out;
            }
        }
        return member;
    }
    return member;
}

// The declared type of the receiver, read from the source: a parameter or a
// variable annotation names its type, and `new Widget()` names the class.
// The resolved type of an imported owner is a structural shape label, so the
// declaration site is what names the owner.
std::string declaredTypeNameOf(const Program& program,
                               const std::string& name) {
    if (name.empty()) return std::string();
    std::string found;
    forEachNode(program, [&](const Node& node) {
        if (!found.empty()) return;
        if (node.kind == NodeKind::Parameter) {
            const auto& parameter = static_cast<const Parameter&>(node);
            if (parameterBindingName(parameter) != name) return;
            if (parameter.type != nullptr &&
                parameter.type->kind == NodeKind::NamedType) {
                found = static_cast<const NamedType&>(*parameter.type).name;
            }
            return;
        }
        if (node.kind != NodeKind::VariableDeclarator) return;
        const auto& declarator = static_cast<const VariableDeclarator&>(node);
        if (declarator.id == nullptr ||
            declarator.id->kind != NodeKind::Identifier) {
            return;
        }
        if (static_cast<const Identifier&>(*declarator.id).name != name) return;
        if (declarator.type != nullptr &&
            declarator.type->kind == NodeKind::NamedType) {
            found = static_cast<const NamedType&>(*declarator.type).name;
            return;
        }
        if (declarator.init != nullptr &&
            declarator.init->kind == NodeKind::NewExpr) {
            const auto& construction =
                static_cast<const NewExpr&>(*declarator.init);
            if (construction.callee != nullptr &&
                construction.callee->kind == NodeKind::Identifier) {
                found = static_cast<const Identifier&>(*construction.callee).name;
            }
        }
    });
    return found;
}

// The expected type of the argument the cursor sits in, when the callee is
// declared in another module: the parameter's annotation as written there.
std::string expectedImportedArgumentSyntax(const AnalyzedDocument& document,
                                           std::uint32_t cursor) {
    if (document.entry == nullptr || document.entry->ast == nullptr ||
        document.file == nullptr) {
        return std::string();
    }
    const CallExpr* best = nullptr;
    forEachNode(*document.entry->ast, [&](const Node& node) {
        if (node.kind != NodeKind::CallExpr || !node.span.valid() ||
            node.span.file != document.file || cursor < node.span.begin.offset ||
            cursor > node.span.end.offset) {
            return;
        }
        const auto& call = static_cast<const CallExpr&>(node);
        if (call.callee == nullptr || !call.callee->span.valid() ||
            cursor < call.callee->span.end.offset) {
            return;
        }
        if (best == nullptr || call.span.length() < best->span.length()) {
            best = &call;
        }
    });
    if (best == nullptr || best->callee == nullptr) return std::string();
    if (best->callee->kind != NodeKind::Identifier) return std::string();
    const std::string calleeName =
        static_cast<const Identifier&>(*best->callee).name;
    const Node* declaration = importedDeclarationFor(document, calleeName);
    if (declaration == nullptr ||
        declaration->kind != NodeKind::FunctionDeclaration) {
        return std::string();
    }
    const auto& function = static_cast<const FunctionDeclaration&>(*declaration);
    const std::uint32_t open = openParenAfter(
        document.file->text, best->callee->span.end.offset, cursor);
    if (open == kNoOffset) return std::string();
    const int argument = countArguments(document.file->text, open, cursor);
    if (argument < 0 ||
        static_cast<std::size_t>(argument) >= function.params.size()) {
        return std::string();
    }
    const ParameterPtr& parameter = function.params[static_cast<std::size_t>(argument)];
    if (parameter == nullptr || parameter->type == nullptr) {
        return std::string();
    }
    return describeTypeSyntax(*parameter->type);
}

// The properties of an object literal, with the type each value carries.
std::string describeObjectProperties(const AnalyzedDocument& document,
                                     const ObjectLiteral& literal) {
    std::string out = "{ ";
    bool first = true;
    for (const ObjectPropertyPtr& property : literal.properties) {
        if (property == nullptr) continue;
        const std::string key = keyName(property->key.get());
        if (key.empty()) continue;
        if (!first) out += "; ";
        first = false;
        out += key;
        out += ": ";
        const Type* valueType =
            property->value != nullptr
                ? document.sema.typeOf(property->value->nodeId)
                : nullptr;
        if (valueType == nullptr) {
            out += "unknown";
            continue;
        }
        // A literal value widens to its base primitive: the hover shows the
        // type of the property, not the value it happens to hold.
        const Type& shown =
            valueType->isLiteral() ? valueType->literalBaseType() : *valueType;
        out += describe(shown);
    }
    out += first ? "}" : " }";
    return out;
}

// The members of the value a name refers to, for the hover. A structural
// value (an interface, an object literal) has no nominal type - the resolved
// type is only an internal shape label - so the members are what an editor
// shows. Empty when the name refers to something else.
std::string describeValueMembers(const AnalyzedDocument& document,
                                 const std::string& name,
                                 std::uint32_t offset) {
    if (name.empty() || document.entry == nullptr ||
        document.entry->ast == nullptr) {
        return std::string();
    }
    const SymbolInfo* symbol =
        nearestSymbolAt(document.sema, name, *document.file, offset);
    if (symbol == nullptr) return std::string();
    const Node* declaration =
        findNodeById(*document.entry->ast, symbol->declNodeId);
    if (declaration == nullptr) return std::string();
    if (declaration->kind == NodeKind::InterfaceDeclaration) {
        const auto& interface =
            static_cast<const InterfaceDeclaration&>(*declaration);
        return describeSignatureMembers(interface.members);
    }
    if (declaration->kind == NodeKind::VariableDeclarator) {
        const auto& declarator =
            static_cast<const VariableDeclarator&>(*declaration);
        if (declarator.init != nullptr &&
            declarator.init->kind == NodeKind::ObjectLiteral) {
            const auto& literal =
                static_cast<const ObjectLiteral&>(*declarator.init);
            return describeObjectProperties(document, literal);
        }
    }
    return std::string();
}

std::optional<lsp::HoverAnswer> hoverAt(const std::string& path,
                                        const std::string& text,
                                        std::uint32_t line,
                                        std::uint32_t column) {
    auto document = analyzeDocument(path, text);
    if (!analysisReady(*document)) return std::nullopt;
    const Node* node =
        innermostNodeAt(*document->entry->ast, *document->file, line, column);
    if (node == nullptr) return std::nullopt;
    const std::uint32_t offset = offsetOf(*document->file, line, column);
    lsp::HoverAnswer answer;
    // A literal carries no declaration and no documentation: what the editor
    // can show is the type the call expects at this position - and nothing at
    // all when the position has no expected type.
    if (isLiteralNode(*node)) {
        const Type* expected = expectedArgumentTypeAt(*document, offset);
        if (expected != nullptr) {
            answer.contents = describe(*expected);
            return answer;
        }
        const std::string syntax =
            expectedImportedArgumentSyntax(*document, offset);
        if (syntax.empty()) return std::nullopt;
        answer.contents = syntax;
        return answer;
    }
    std::string name = nameOfNode(*node);
    // A class name in a type position (`const box: Box`, `extends Box`) is a
    // TypeNode, not an Identifier node: borrow the annotation's own name so
    // the declaration fallback can resolve the class binding instead of
    // reporting nothing there.
    if (name.empty() && node->kind == NodeKind::NamedType) {
        name = static_cast<const NamedType&>(*node).name;
    }
    const Type* type = document->sema.typeOf(node->nodeId);
    const SymbolInfo* symbol = nullptr;
    if (type == nullptr && !name.empty()) {
        symbol = nearestSymbolAt(document->sema, name, *document->file, offset);
        if (symbol != nullptr) type = &symbol->type;
    }
    // The documentation: the std surface's own description, or the doc
    // comment of the declaration the name refers to.
    const DocAnswer doc = documentationAt(*document, *node, name, symbol);
    // A structural value (an interface, an object literal) has no nominal
    // type to show - the resolved type is only an internal shape label. The
    // members are what the hover prints instead.
    const std::string members =
        describeValueMembers(*document, name, offset);
    // The engine answers only from the checker's own inference: a type worth
    // showing is not unknown, error, the internal placeholder behind an
    // unresolved import, or `never`.
    if (type != nullptr && !neverType(*type) &&
        (!isPlaceholderType(*type) || !members.empty())) {
        if (!name.empty()) {
            // An identifier (or a named member): the declaration-site format
            // this editor has always shown - `name: type`.
            answer.contents = name + ": " +
                              (members.empty() ? describe(*type) : members);
        } else {
            // A value expression: the described type on its own line, no
            // prefix. A literal widens to its base primitive (`describe()`
            // prints the literal's value, which would only echo the source).
            const Type& shown = type->isLiteral() ? type->literalBaseType()
                                                  : *type;
            answer.contents = members.empty() ? describe(shown) : members;
        }
        answer.documentation = doc.signature.empty()
                                   ? doc.body
                                   : renderStdDoc(doc.signature, doc.body);
        return answer;
    }
    // unknown/error/never (or no type at all): the documented signature of
    // the declaration, when there is one; otherwise the declaration the name
    // refers to in another module.
    if (!name.empty()) {
        if (const Node* imported = importedDeclarationFor(*document, name)) {
            answer.contents = describeImportedDeclaration(*imported, name);
            answer.documentation = doc.body;
            return answer;
        }
    }
    // A member of a class or interface: the receiver's declaration names the
    // owner, and the member is described from the owner's declaration - in
    // this file or in an imported module.
    if (node->kind == NodeKind::MemberExpr) {
        const auto& member = static_cast<const MemberExpr&>(*node);
        std::string receiverName;
        if (member.object != nullptr &&
            member.object->kind == NodeKind::Identifier) {
            receiverName = static_cast<const Identifier&>(*member.object).name;
        }
        const std::string ownerName =
            declaredTypeNameOf(*document->entry->ast, receiverName);
        if (!ownerName.empty()) {
            if (const ClassDeclaration* owner =
                    findClassByName(*document, ownerName)) {
                answer.contents = describeMemberOf(*owner, name);
                answer.documentation = doc.body;
                return answer;
            }
            if (const InterfaceDeclaration* owner =
                    findInterfaceByName(*document, ownerName)) {
                answer.contents = describeMemberOf(*owner, name);
                answer.documentation = doc.body;
                return answer;
            }
        }
    }
    if (doc.empty()) return std::nullopt;
    answer.contents = doc.signature;
    answer.documentation = doc.body;
    return answer;
}

std::vector<lsp::SymbolEntry> symbolsAt(const std::string& path,
                                        const std::string& text) {
    std::vector<lsp::SymbolEntry> entries;
    auto document = analyzeDocument(path, text);
    if (!analysisReady(*document)) return entries;
    for (const StmtPtr& statement : document->entry->ast->body) {
        if (statement != nullptr) {
            collectSymbols(*statement, *document->file, entries);
        }
    }
    return entries;
}

std::optional<lsp::SignatureAnswer> signatureAt(const std::string& path,
                                                const std::string& text,
                                                std::uint32_t line,
                                                std::uint32_t column) {
    auto document = analyzeDocument(path, text);
    if (!analysisReady(*document)) return std::nullopt;
    const std::uint32_t cursor = offsetOf(*document->file, line, column);
    // The innermost call-like expression covering the cursor. The cursor may
    // sit inside the callee itself - signature help is also asked for there,
    // and the argument list opens after the callee.
    const Node* call =
        innermostCallAt(*document->entry->ast, *document->file, line, column);
    if (call == nullptr) return std::nullopt;
    const bool isNew = call->kind == NodeKind::NewExpr;
    const Expr* callee = calleeOf(*call);
    if (callee == nullptr || !callee->span.valid()) return std::nullopt;
    const std::uint32_t open =
        openParenAfter(document->file->text, callee->span.end.offset,
                       call->span.end.offset);
    if (open == kNoOffset) return std::nullopt;
    // The callee's type: the checker published it with the resolved parameter
    // list and return type.
    const Type* calleeType = document->sema.typeOf(callee->nodeId);
    const SymbolInfo* symbol = nullptr;
    if (callee->kind == NodeKind::Identifier) {
        const auto& id = static_cast<const Identifier&>(*callee);
        symbol = nearestSymbolAt(document->sema, id.name, *document->file, cursor);
        if (calleeType == nullptr && symbol != nullptr) {
            calleeType = &symbol->type;
        }
    }
    // The declaration that declares the callee, when the compiler can see one.
    const CalleeDeclaration declaration =
        resolveCalleeDeclaration(*document, *callee, symbol, isNew);
    const std::string name =
        declaration.name.empty() ? nameOfNode(*callee) : declaration.name;
    const int activeParameter =
        countArguments(document->file->text, open, cursor);

    // A declared callee: the label carries the callee's name, the parameter
    // names with their types, the optional/rest markers and the declared
    // return type - e.g. `add(a: number, b?: string): number`.
    const ParamList* params = declaration.node != nullptr
                                  ? paramsOfDeclaration(*declaration.node)
                                  : nullptr;
    if (params != nullptr) {
        lsp::SignatureAnswer answer;
        answer.activeParameter = static_cast<int>(activeParameter);
        std::string label = name + "(";
        const std::size_t declared = params->size();
        const bool typesMatch = calleeType != nullptr &&
                                calleeType->kind == TypeKind::Function &&
                                calleeType->parts.size() == declared + 1;
        for (std::size_t i = 0; i < declared; ++i) {
            const ParameterPtr& parameter = (*params)[i];
            std::string type;
            const Type* resolved =
                typesMatch ? &calleeType->parts[i] : nullptr;
            if (parameter == nullptr) {
                type = "unknown";
            } else {
                type = describeParameter(*parameter, resolved);
            }
            if (i != 0) label += ", ";
            label += type;
            answer.parameters.push_back(std::move(type));
        }
        label += ")";
        std::string ret;
        if (declaration.isConstructor) {
            // A constructor's result is the class itself.
            ret = name;
        } else if (calleeType != nullptr && calleeType->kind == TypeKind::Function &&
                   !calleeType->parts.empty()) {
            ret = describe(calleeType->parts.back());
        } else if (const TypePtr* annotation =
                       returnTypeOfDeclaration(*declaration.node);
                   annotation != nullptr && *annotation != nullptr) {
            ret = describeTypeSyntax(**annotation);
        } else {
            ret = "unknown";
        }
        answer.label = label + ": " + ret;
        answer.documentation =
            calleeDocumentation(*document, *callee, declaration, name, symbol);
        answer.parameterDocs = parameterDocs(*document, declaration.entryNodeId,
                                            answer.parameters.size());
        return answer;
    }

    // An std surface call: the documented signature and description the std
    // tables carry, e.g. `Math.max(...values: number[]): number`.
    lsp::SignatureAnswer stdAnswer;
    if (stdCalleeAnswer(*document, *callee, stdAnswer)) {
        stdAnswer.activeParameter = static_cast<int>(activeParameter);
        return stdAnswer;
    }

    // A function-typed callee whose declaration the compiler cannot see:
    // the bare type, exactly as before.
    if (calleeType != nullptr && calleeType->kind == TypeKind::Function &&
        !calleeType->parts.empty()) {
        lsp::SignatureAnswer answer;
        answer.label = describe(*calleeType);
        for (std::size_t i = 0; i + 1 < calleeType->parts.size(); ++i) {
            answer.parameters.push_back(describe(calleeType->parts[i]));
        }
        answer.activeParameter = static_cast<int>(activeParameter);
        answer.documentation =
            calleeDocumentation(*document, *callee, declaration, name, symbol);
        answer.parameterDocs = parameterDocs(*document, declaration.entryNodeId,
                                            answer.parameters.size());
        return answer;
    }
    return std::nullopt;
}

}  // namespace cp3::cli
