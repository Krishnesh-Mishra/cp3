// =============================================================================
// src/cli/src/commands/lsp/intel/lsp_intel_calls.cpp - the call hierarchy.
//
// OWNERSHIP: cli module (src/cli/**). Implements the callHierarchyPrepareAt /
// callHierarchyIncomingAt / callHierarchyOutgoingAt declarations in
// detail/lsp/lsp_intel.hpp.
//
// The call hierarchy answers three questions about the program's calls:
//   * prepare  - the callable the cursor names: the callee of the call at the
//                cursor, or the name itself (a method, a getter, a
//                constructor);
//   * incoming - the callers of that callable;
//   * outgoing - the callees inside that callable's body.
//
// Everything is read from the compiler's own model: the AST for the call
// sites and the declarations, the SemaResult for the resolved types. A
// position the compiler cannot resolve answers empty - never a guess.
// =============================================================================
#include "detail/lsp/lsp_intel.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/lsp.hpp"
#include "cp3/meta/module.hpp"
#include "cp3/std_surface.hpp"
#include "cp3/core/source.hpp"
#include "cp3/core/types.hpp"

namespace cp3::cli {
namespace {

// LSP SymbolKind numbers the items carry.
constexpr int kKindClass = 5;
constexpr int kKindMethod = 6;
constexpr int kKindProperty = 7;
constexpr int kKindConstructor = 9;
constexpr int kKindFunction = 12;

// A callable the hierarchy can point at.
struct Callable {
    const Node* declaration = nullptr;  // the declaration node (null for a std member)
    std::string name;                   // the name the editor shows
    int kind = kKindFunction;
    std::string detail;                 // the signature line
    std::string path;                   // the file the declaration lives in
    SourceSpan span;                    // the declaration's span
    SourceSpan nameSpan;                // the name's span
};

// Every program that may carry a declaration: the entry first, then the
// imported modules.
std::vector<const Program*> programsOf(const AnalyzedDocument& document) {
    std::vector<const Program*> programs;
    if (document.entry != nullptr && document.entry->ast != nullptr) {
        programs.push_back(document.entry->ast.get());
    }
    if (document.graph != nullptr) {
        for (const CompilationUnit* unit : document.graph->units()) {
            if (unit == nullptr || unit->ast == nullptr) continue;
            if (document.entry != nullptr && unit->ast == document.entry->ast) {
                continue;
            }
            programs.push_back(unit->ast.get());
        }
    }
    return programs;
}

// The kinds whose declaration can be a caller or a callee.
bool isCallableKind(NodeKind kind) {
    switch (kind) {
        case NodeKind::FunctionDeclaration:
        case NodeKind::MethodDeclaration:
        case NodeKind::ConstructorDeclaration:
            return true;
        default:
            return false;
    }
}

// True when `outer` covers `inner`.
bool covers(const SourceSpan& outer, const SourceSpan& inner) {
    if (!outer.valid() || !inner.valid()) return false;
    return outer.begin.offset <= inner.begin.offset &&
           outer.end.offset >= inner.end.offset;
}

// The innermost callable whose span covers `span`.
const Node* enclosingCallable(const Program& program, const SourceSpan& span) {
    const Node* best = nullptr;
    forEachNode(program, [&](const Node& node) {
        if (!isCallableKind(node.kind)) return;
        if (!covers(node.span, span)) return;
        if (best == nullptr || node.span.length() < best->span.length()) {
            best = &node;
        }
    });
    return best;
}

// The class declaration that contains `span`.
const Node* enclosingClass(const Program& program, const SourceSpan& span) {
    const Node* best = nullptr;
    forEachNode(program, [&](const Node& node) {
        if (node.kind != NodeKind::ClassDeclaration) return;
        if (!covers(node.span, span)) return;
        if (best == nullptr || node.span.length() < best->span.length()) {
            best = &node;
        }
    });
    return best;
}

// The class declaration a name refers to.
const ClassDeclaration* classByName(const AnalyzedDocument& document,
                                    const std::string& name) {
    for (const Program* program : programsOf(document)) {
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

// The interface declaration a name refers to.
const InterfaceDeclaration* interfaceByName(const AnalyzedDocument& document,
                                            const std::string& name) {
    for (const Program* program : programsOf(document)) {
        const InterfaceDeclaration* found = nullptr;
        forEachNode(*program, [&](const Node& node) {
            if (found != nullptr ||
                node.kind != NodeKind::InterfaceDeclaration) {
                return;
            }
            const auto& declaration =
                static_cast<const InterfaceDeclaration&>(node);
            if (declaration.name == name) found = &declaration;
        });
        if (found != nullptr) return found;
    }
    return nullptr;
}

// The function declaration a name refers to.
const Node* functionByName(const AnalyzedDocument& document,
                            const std::string& name) {
    for (const Program* program : programsOf(document)) {
        const Node* found = nullptr;
        forEachNode(*program, [&](const Node& node) {
            if (found != nullptr) return;
            if (node.kind != NodeKind::FunctionDeclaration) return;
            if (static_cast<const FunctionDeclaration&>(node).name == name) {
                found = &node;
            }
        });
        if (found != nullptr) return found;
    }
    return nullptr;
}

// The key a class or interface member carries (its name).
std::string memberKey(const Node& member) {
    switch (member.kind) {
        case NodeKind::PropertyDeclaration:
            return keyName(
                static_cast<const PropertyDeclaration&>(member).key.get());
        case NodeKind::MethodDeclaration:
            return keyName(
                static_cast<const MethodDeclaration&>(member).key.get());
        case NodeKind::AccessorDeclaration:
            return keyName(
                static_cast<const AccessorDeclaration&>(member).key.get());
        case NodeKind::PropertySignature:
            return keyName(
                static_cast<const PropertySignature&>(member).key.get());
        case NodeKind::MethodSignature:
            return keyName(
                static_cast<const MethodSignature&>(member).key.get());
        default:
            return std::string();
    }
}

// The member `name` of a class.
const Node* classMemberOf(const ClassDeclaration& owner,
                          const std::string& name) {
    for (const ClassMemberPtr& member : owner.members) {
        if (member == nullptr) continue;
        if (memberKey(*member) == name) return member.get();
    }
    return nullptr;
}

// The member `name` of an interface.
const Node* interfaceMemberOf(const InterfaceDeclaration& owner,
                              const std::string& name) {
    for (const SignatureMemberPtr& member : owner.members) {
        if (member == nullptr) continue;
        if (memberKey(*member) == name) return member.get();
    }
    return nullptr;
}

// The name a declaration carries.
std::string declaredName(const Node& declaration) {
    switch (declaration.kind) {
        case NodeKind::FunctionDeclaration:
            return static_cast<const FunctionDeclaration&>(declaration).name;
        case NodeKind::MethodDeclaration:
            return memberKey(declaration);
        case NodeKind::MethodSignature:
            return memberKey(declaration);
        case NodeKind::ConstructorDeclaration:
            return std::string();
        case NodeKind::ClassDeclaration:
            return static_cast<const ClassDeclaration&>(declaration).name;
        case NodeKind::InterfaceDeclaration:
            return static_cast<const InterfaceDeclaration&>(declaration).name;
        case NodeKind::AccessorDeclaration:
            return memberKey(declaration);
        default:
            return std::string();
    }
}

// The signature line of a declaration, as the editor shows it.
std::string declaredDetail(const Node& declaration) {
    switch (declaration.kind) {
        case NodeKind::MethodDeclaration: {
            const auto& method = static_cast<const MethodDeclaration&>(declaration);
            std::string out = declaredName(declaration) + "(";
            for (std::size_t i = 0; i < method.params.size(); ++i) {
                if (i != 0) out += ", ";
                out += "arg";
            }
            out += ")";
            return out;
        }
        case NodeKind::MethodSignature: {
            const auto& method = static_cast<const MethodSignature&>(declaration);
            std::string out = declaredName(declaration) + "(";
            for (std::size_t i = 0; i < method.params.size(); ++i) {
                if (i != 0) out += ", ";
                out += "arg";
            }
            out += ")";
            return out;
        }
        case NodeKind::FunctionDeclaration:
            return "function " +
                   static_cast<const FunctionDeclaration&>(declaration).name;
        case NodeKind::ConstructorDeclaration:
            return "constructor";
        case NodeKind::AccessorDeclaration:
            return "getter " + declaredName(declaration);
        case NodeKind::ClassDeclaration:
            return "class " +
                   static_cast<const ClassDeclaration&>(declaration).name;
        default:
            return std::string();
    }
}

// The symbol kind an item carries.
int kindOf(const Node& declaration) {
    switch (declaration.kind) {
        case NodeKind::MethodDeclaration:
            return kKindMethod;
        case NodeKind::MethodSignature:
            return kKindMethod;
        case NodeKind::ConstructorDeclaration:
            return kKindConstructor;
        case NodeKind::ClassDeclaration:
            return kKindClass;
        case NodeKind::AccessorDeclaration:
            return kKindProperty;
        default:
            return kKindFunction;
    }
}

// The path of the file a declaration lives in.
std::string pathOf(const SourceSpan& span) {
    return span.valid() && span.file != nullptr ? span.file->path
                                                : std::string();
}

// The callable a declaration is.
Callable callableOf(const Node& declaration) {
    Callable callable;
    callable.declaration = &declaration;
    callable.name = declaredName(declaration);
    callable.kind = kindOf(declaration);
    callable.detail = declaredDetail(declaration);
    callable.path = pathOf(declaration.span);
    callable.span = declaration.span;
    callable.nameSpan = nameSpanIn(declaration.span, callable.name);
    return callable;
}

// The declared type of a name, read from the source: a parameter or a
// variable annotation names the class or interface the name holds. The
// resolved type of an interface-typed value is a structural shape, so the
// declaration site is what names the owner.
std::string declaredTypeNameOf(const Program& program,
                               const std::string& name) {
    if (name.empty()) return std::string();
    std::string found;
    forEachNode(program, [&](const Node& node) {
        if (!found.empty()) return;
        if (node.kind == NodeKind::Parameter) {
            const auto& parameter = static_cast<const Parameter&>(node);
            if (parameter.name == nullptr ||
                parameter.name->kind != NodeKind::Identifier) {
                return;
            }
            if (static_cast<const Identifier&>(*parameter.name).name != name) {
                return;
            }
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
        }
    });
    return found;
}

// The member `name` of a class or of one of its bases.
const Node* classMemberInChain(const AnalyzedDocument& document,
                               const std::string& className,
                               const std::string& member) {
    std::string current = className;
    for (int depth = 0; depth < 8; ++depth) {
        const ClassDeclaration* owner = classByName(document, current);
        if (owner == nullptr) return nullptr;
        const Node* found = classMemberOf(*owner, member);
        if (found != nullptr) return found;
        if (owner->extendsType == nullptr ||
            owner->extendsType->kind != NodeKind::NamedType) {
            return nullptr;
        }
        current = static_cast<const NamedType&>(*owner->extendsType).name;
    }
    return nullptr;
}

// The class of the receiver of a member access: the class of the receiver's
// type, the class the receiver names (a static access), or the class its
// declaration names (a parameter annotated with a class type).
std::string receiverClassName(const AnalyzedDocument& document,
                              const Expr* receiver) {
    if (receiver == nullptr) return std::string();
    const Type* type = document.sema.typeOf(receiver->nodeId);
    if (type != nullptr && type->kind == TypeKind::Class &&
        !type->name.empty()) {
        return type->name;
    }
    if (receiver->kind != NodeKind::Identifier) return std::string();
    const std::string& name = static_cast<const Identifier&>(*receiver).name;
    if (classByName(document, name) != nullptr) return name;
    // The declared type of the receiver: a parameter or a variable annotated
    // with the interface or class it holds.
    return declaredTypeNameOf(*document.entry->ast, name);
}

// The std surface member a receiver names (a namespace like `console`), when
// the member is part of that surface.
bool stdMemberOf(const std::string& receiver, const std::string& member) {
    if (receiver.empty()) return false;
    for (const std::string& name : stdNamespaceMemberNames(receiver)) {
        if (name == member) return true;
    }
    return false;
}

// The callable a call's callee names.
std::optional<Callable> resolveCallee(const AnalyzedDocument& document,
                                       const Expr* callee) {
    if (callee == nullptr) return std::nullopt;
    if (callee->kind == NodeKind::Identifier) {
        const std::string& name = static_cast<const Identifier&>(*callee).name;
        const Node* declaration = functionByName(document, name);
        if (declaration == nullptr) return std::nullopt;
        return callableOf(*declaration);
    }
    if (callee->kind != NodeKind::MemberExpr) return std::nullopt;
    const auto& member = static_cast<const MemberExpr&>(*callee);
    const std::string& name = member.property;
    // `this.method()`: the class the call sits in.
    if (member.object != nullptr &&
        member.object->kind == NodeKind::ThisExpr) {
        const Node* owner = enclosingClass(*document.entry->ast, member.span);
        if (owner != nullptr) {
            const Node* found = classMemberOf(
                static_cast<const ClassDeclaration&>(*owner), name);
            if (found != nullptr) return callableOf(*found);
        }
    }
    const std::string ownerName = receiverClassName(document, member.object.get());
    if (!ownerName.empty()) {
        const Node* found = classMemberInChain(document, ownerName, name);
        if (found != nullptr) return callableOf(*found);
        if (const InterfaceDeclaration* owner =
                interfaceByName(document, ownerName)) {
            const Node* member_ = interfaceMemberOf(*owner, name);
            if (member_ != nullptr) return callableOf(*member_);
        }
    }
    // A std surface member (`console.log`): the surface owns no file, so the
    // item points at the file the call sits in.
    if (member.object != nullptr &&
        member.object->kind == NodeKind::Identifier) {
        const std::string& receiver =
            static_cast<const Identifier&>(*member.object).name;
        if (stdMemberOf(receiver, name)) {
            Callable callable;
            callable.name = name;
            callable.kind = kKindMethod;
            callable.detail = receiver + "." + name;
            callable.path = document.file != nullptr ? document.file->path
                                                    : std::string();
            return callable;
        }
    }
    return std::nullopt;
}

// The item an entry carries.
lsp::HierarchyEntry entryOf(const Callable& callable) {
    lsp::HierarchyEntry entry;
    entry.name = callable.name;
    entry.kind = callable.kind;
    entry.detail = callable.detail;
    entry.path = callable.path;
    if (callable.span.valid()) {
        entry.line = callable.span.begin.line;
        entry.column = callable.span.begin.column;
        entry.endLine = callable.span.end.line;
        entry.endColumn = callable.span.end.column;
    }
    if (callable.nameSpan.valid()) {
        entry.nameLine = callable.nameSpan.begin.line;
        entry.nameColumn = callable.nameSpan.begin.column;
        entry.nameEndLine = callable.nameSpan.end.line;
        entry.nameEndColumn = callable.nameSpan.end.column;
    }
    return entry;
}

// A call site of the program with the callable it names.
struct CallSite {
    const Node* call = nullptr;
    std::optional<Callable> callee;
};

std::vector<CallSite> callSitesOf(const AnalyzedDocument& document) {
    std::vector<CallSite> sites;
    if (document.entry == nullptr || document.entry->ast == nullptr) {
        return sites;
    }
    forEachNode(*document.entry->ast, [&](const Node& node) {
        if (node.kind != NodeKind::CallExpr && node.kind != NodeKind::NewExpr) {
            return;
        }
        CallSite site;
        site.call = &node;
        const Expr* callee = nullptr;
        if (node.kind == NodeKind::CallExpr) {
            callee = static_cast<const CallExpr&>(node).callee.get();
        } else {
            callee = static_cast<const NewExpr&>(node).callee.get();
        }
        site.callee = resolveCallee(document, callee);
        sites.push_back(site);
    });
    return sites;
}

// The innermost call the cursor sits in (a call or a construction).
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

// The class an item points at (a construction names the class).
std::optional<Callable> classCallable(const AnalyzedDocument& document,
                                      const std::string& name) {
    const ClassDeclaration* owner = classByName(document, name);
    if (owner == nullptr) return std::nullopt;
    Callable item;
    item.declaration = owner;
    item.name = owner->name;
    item.kind = kKindClass;
    item.detail = "class " + owner->name;
    item.path = pathOf(owner->span);
    item.span = owner->span;
    item.nameSpan = nameSpanIn(owner->span, owner->name);
    return item;
}

// The callable the cursor names.
std::optional<Callable> callableAt(const AnalyzedDocument& document,
                                   std::uint32_t line, std::uint32_t column) {
    if (document.entry == nullptr || document.entry->ast == nullptr ||
        document.file == nullptr) {
        return std::nullopt;
    }
    // A call at the cursor: the hierarchy points at what the call names - the
    // callee, anywhere in the call (a member-expression callee only spans its
    // property token, so the receiver sits outside it). A cursor on an
    // argument names that argument instead (a function passed as a value).
    const Node* call =
        innermostCallAt(*document.entry->ast, *document.file, line, column);
    if (call != nullptr) {
        const Expr* callee =
            call->kind == NodeKind::CallExpr
                ? static_cast<const CallExpr&>(*call).callee.get()
                : static_cast<const NewExpr&>(*call).callee.get();
        // A cursor on an argument names what the argument names (a function
        // passed as a value). An argument's span covers the whole argument,
        // so this is exact even for a cursor on a member-expression receiver
        // inside one.
        const ExprList* arguments = nullptr;
        if (call->kind == NodeKind::CallExpr) {
            arguments = &static_cast<const CallExpr&>(*call).arguments;
        } else {
            arguments = &static_cast<const NewExpr&>(*call).arguments;
        }
        bool onArgument = false;
        for (const ExprPtr& argument : *arguments) {
            if (argument != nullptr &&
                spanContains(argument->span, *document.file, line, column)) {
                onArgument = true;
                break;
            }
        }
        if (callee != nullptr && !onArgument) {
            const std::optional<Callable> callable =
                resolveCallee(document, callee);
            if (callable.has_value()) return callable;
            // `new Widget()`: the class is the item.
            if (callee->kind == NodeKind::Identifier) {
                const std::string& name =
                    static_cast<const Identifier&>(*callee).name;
                return classCallable(document, name);
            }
            return std::nullopt;
        }
    }
    // No call at the cursor (or the cursor sits on one of the call's
    // arguments): the name itself (a function passed as a value) or the
    // callable declaration the cursor sits on (`function helper`, a
    // method's name).
    const Node* node =
        innermostNodeAt(*document.entry->ast, *document.file, line, column);
    if (node == nullptr) return std::nullopt;
    if (isCallableKind(node->kind)) {
        const Callable callable = callableOf(*node);
        // A constructor carries no name of its own; nothing to point at.
        if (callable.name.empty()) return std::nullopt;
        return callable;
    }
    if (node->kind != NodeKind::Identifier && node->kind != NodeKind::MemberExpr) {
        return std::nullopt;
    }
    const Expr& name = static_cast<const Expr&>(*node);
    return resolveCallee(document, &name);
}

// The declaration the entry points at (the item carries the name and the
// file it was declared in).
const Node* declarationFor(const AnalyzedDocument& document,
                           const lsp::HierarchyEntry& entry) {
    if (document.entry == nullptr || document.entry->ast == nullptr) {
        return nullptr;
    }
    const Node* found = nullptr;
    forEachNode(*document.entry->ast, [&](const Node& node) {
        if (found != nullptr) return;
        if (!isCallableKind(node.kind) &&
            node.kind != NodeKind::ClassDeclaration) {
            return;
        }
        if (declaredName(node) != entry.name) return;
        if (!entry.path.empty() && pathOf(node.span) != entry.path) return;
        found = &node;
    });
    return found;
}

}  // namespace

std::vector<lsp::HierarchyEntry> callHierarchyPrepareAt(const std::string& path,
                                                         const std::string& text,
                                                         std::uint32_t line,
                                                         std::uint32_t column) {
    std::vector<lsp::HierarchyEntry> entries;
    auto document = analyzeDocument(path, text);
    if (!analysisReady(*document)) return entries;
    const std::optional<Callable> callable = callableAt(*document, line, column);
    if (!callable.has_value()) return entries;
    entries.push_back(entryOf(*callable));
    return entries;
}

std::vector<lsp::HierarchyEntry> callHierarchyIncomingAt(
    const std::string& path, const std::string& text,
    const lsp::HierarchyEntry& entry) {
    std::vector<lsp::HierarchyEntry> entries;
    auto document = analyzeDocument(path, text);
    if (!analysisReady(*document)) return entries;
    for (const CallSite& site : callSitesOf(*document)) {
        if (!site.callee.has_value()) continue;
        if (site.callee->name != entry.name) continue;
        // The caller: the callable that contains the call.
        const Node* caller =
            enclosingCallable(*document->entry->ast, site.call->span);
        if (caller == nullptr) continue;
        const Callable callable = callableOf(*caller);
        if (callable.name.empty()) continue;
        entries.push_back(entryOf(callable));
    }
    return entries;
}

std::vector<lsp::HierarchyEntry> callHierarchyOutgoingAt(
    const std::string& path, const std::string& text,
    const lsp::HierarchyEntry& entry) {
    std::vector<lsp::HierarchyEntry> entries;
    auto document = analyzeDocument(path, text);
    if (!analysisReady(*document)) return entries;
    const Node* declaration = declarationFor(*document, entry);
    if (declaration == nullptr) return entries;
    for (const CallSite& site : callSitesOf(*document)) {
        if (!site.callee.has_value()) continue;
        // The call must sit inside the item's body.
        if (!covers(declaration->span, site.call->span)) continue;
        entries.push_back(entryOf(*site.callee));
    }
    return entries;
}

}  // namespace cp3::cli
