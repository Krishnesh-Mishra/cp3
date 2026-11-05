// =============================================================================
// src/cli/src/commands/lsp_intel_docs.cpp - the documentation the editor shows.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/lsp_intel_docs.hpp.
//
// Two sources, one rendering:
//   * the doc comments sema parsed into `SemaResult::docs` (a JSDoc-style
//     comment above a declaration);
//   * the std member tables (the documented signature and description of
//     every `console.log`, `Math.abs`, `map.get`, ... the compiler knows).
//
// A member the tables do not document is not silently blank: the answer says
// so, so the gap is visible instead of looking like "there is nothing to
// say about this".
// =============================================================================
#include "detail/lsp/lsp_intel_docs.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/std_surface.hpp"
#include "cp3/core/types.hpp"

namespace cp3::cli {
namespace {

// The text shown for a symbol the compiler knows but cannot describe.
std::string missingDocText(const std::string& what) {
    return "_No documentation available for `" + what + "`._";
}

// True when `member` is one of `names`.
bool contains(const std::vector<std::string>& names, const std::string& member) {
    for (const std::string& name : names) {
        if (name == member) return true;
    }
    return false;
}

// True when the document imports `name` from a std module - the namespace
// binding of `import console from "std/console"` and the named binding of
// `import { env } from "std/env"`.
bool isStdNamespace(const AnalyzedDocument& document, const std::string& name) {
    if (document.entry == nullptr || document.entry->ast == nullptr) return false;
    for (const StmtPtr& statement : document.entry->ast->body) {
        if (statement == nullptr) continue;
        if (statement->kind != NodeKind::ImportDeclaration) continue;
        const auto& import = static_cast<const ImportDeclaration&>(*statement);
        if (import.source.rfind("std/", 0) != 0) continue;
        if (import.defaultBinding == name || import.namespaceBinding == name) {
            return true;
        }
        for (const ImportSpecifierPtr& specifier : import.specifiers) {
            if (specifier == nullptr) continue;
            if (specifier->local == name || specifier->imported == name) {
                return true;
            }
        }
    }
    return false;
}

// The doc comment of the member `member` of the class declaration `cls`.
const DocComment* memberDocOf(const AnalyzedDocument& document,
                               const ClassDeclaration& cls,
                               const std::string& member) {
    for (const ClassMemberPtr& memberPtr : cls.members) {
        if (memberPtr == nullptr) continue;
        std::string name;
        if (memberPtr->kind == NodeKind::MethodDeclaration) {
            name = keyName(
                static_cast<const MethodDeclaration&>(*memberPtr).key.get());
        } else if (memberPtr->kind == NodeKind::PropertyDeclaration) {
            name = keyName(
                static_cast<const PropertyDeclaration&>(*memberPtr).key.get());
        } else {
            continue;
        }
        if (name != member) continue;
        const auto found = document.sema.docs.find(memberPtr->nodeId);
        if (found != document.sema.docs.end()) return &found->second;
    }
    return nullptr;
}

// The doc comment of the class member whose KEY is the node `nodeId`: the
// cursor sits on the member's name, which is a key webserverion rather than
// the member node itself.
const DocComment* memberDocByKey(const AnalyzedDocument& document,
                                 std::uint32_t nodeId) {
    if (nodeId == 0) return nullptr;
    if (document.entry == nullptr || document.entry->ast == nullptr) {
        return nullptr;
    }
    for (const StmtPtr& statement : document.entry->ast->body) {
        if (statement == nullptr) continue;
        if (statement->kind != NodeKind::ClassDeclaration) continue;
        const auto& cls = static_cast<const ClassDeclaration&>(*statement);
        for (const ClassMemberPtr& member : cls.members) {
            if (member == nullptr) continue;
            const Expr* key = nullptr;
            if (member->kind == NodeKind::MethodDeclaration) {
                key = static_cast<const MethodDeclaration&>(*member).key.get();
            } else if (member->kind == NodeKind::PropertyDeclaration) {
                key = static_cast<const PropertyDeclaration&>(*member).key.get();
            } else if (member->kind == NodeKind::AccessorDeclaration) {
                key = static_cast<const AccessorDeclaration&>(*member).key.get();
            } else {
                continue;
            }
            if (key == nullptr || key->nodeId != nodeId) continue;
            const auto found = document.sema.docs.find(member->nodeId);
            if (found != document.sema.docs.end()) return &found->second;
        }
    }
    return nullptr;
}

}  // namespace

// =============================================================================
// Rendering
// =============================================================================
std::string renderDocComment(const DocComment& doc) {
    std::string out = doc.summary;
    if (!doc.params.empty()) {
        if (!out.empty()) out += "\n\n";
        out += "**Parameters**\n\n";
        for (const DocParam& param : doc.params) {
            out += "- `" + param.name + "`";
            if (!param.text.empty()) out += " - " + param.text;
            out += "\n";
        }
        if (!out.empty() && out.back() == '\n') out.pop_back();
    }
    if (!doc.returns.empty()) {
        if (!out.empty()) out += "\n\n";
        out += "**Returns**\n\n" + doc.returns;
    }
    return out;
}

std::string renderStdDoc(const std::string& signature, const std::string& body) {
    if (signature.empty()) return body;
    if (body.empty()) return "`" + signature + "`";
    return "`" + signature + "`\n\n" + body;
}

// =============================================================================
// The lookups
// =============================================================================
const DocComment* docCommentAt(const AnalyzedDocument& document,
                               const std::string& name, std::uint32_t offset) {
    if (name.empty() || document.file == nullptr) return nullptr;
    const SymbolInfo* symbol =
        nearestSymbolAt(document.sema, name, *document.file, offset);
    if (symbol == nullptr) return nullptr;
    const auto found = document.sema.docs.find(symbol->declNodeId);
    if (found == document.sema.docs.end()) return nullptr;
    return &found->second;
}

const DocComment* classMemberDocAt(const AnalyzedDocument& document,
                                   const std::string& className,
                                   const std::string& member) {
    if (className.empty() || member.empty()) return nullptr;
    if (document.entry == nullptr || document.entry->ast == nullptr) return nullptr;
    for (const StmtPtr& statement : document.entry->ast->body) {
        if (statement == nullptr) continue;
        if (statement->kind != NodeKind::ClassDeclaration) continue;
        const auto& cls = static_cast<const ClassDeclaration&>(*statement);
        if (cls.name != className) continue;
        if (const DocComment* doc = memberDocOf(document, cls, member)) {
            return doc;
        }
    }
    return nullptr;
}

const Type* receiverTypeEndingAt(const AnalyzedDocument& document,
                                 std::uint32_t offset) {
    if (document.entry == nullptr || document.entry->ast == nullptr) return nullptr;
    const Node* best = nullptr;
    forEachNode(*document.entry->ast, [&](const Node& node) {
        if (!node.span.valid() || node.span.file != document.file) return;
        if (node.span.end.offset != offset) return;
        if (best == nullptr || node.span.length() > best->span.length()) {
            best = &node;
        }
    });
    if (best == nullptr) return nullptr;
    return document.sema.typeOf(best->nodeId);
}

std::string docCommentFor(const AnalyzedDocument& document,
                           std::uint32_t nodeId) {
    if (nodeId == 0) return "";
    const auto found = document.sema.docs.find(nodeId);
    if (found == document.sema.docs.end()) return "";
    return renderDocComment(found->second);
}

std::string completionMemberDoc(const AnalyzedDocument& document,
                                const std::string& namespaceName,
                                const Type* receiverType,
                                const std::string& member) {
    if (!namespaceName.empty()) {
        const StdMemberDoc doc = stdNamespaceMemberDoc(namespaceName, member);
        if (!doc.empty()) {
            return renderStdDoc(namespaceName + "." + doc.signature, doc.body);
        }
        return missingDocText(namespaceName + "." + member);
    }
    if (receiverType != nullptr && receiverType->kind == TypeKind::Object &&
        contains(networkObjectMemberNames(receiverType->name), member)) {
        const StdMemberDoc doc = networkObjectMemberDoc(
            receiverType->name, member, document.typeSystem);
        if (!doc.empty()) return renderStdDoc(doc.signature, doc.body);
        return missingDocText(receiverType->name + "." + member);
    }
    if (receiverType == nullptr) return "";
    if (receiverType->kind == TypeKind::Class) {
        const StdMemberDoc doc =
            stdInstanceMemberDoc(receiverType->name, member);
        if (!doc.empty()) {
            return renderStdDoc(receiverType->name + "." + doc.signature,
                                doc.body);
        }
        if (const DocComment* memberDoc =
                classMemberDocAt(document, receiverType->name, member)) {
            return renderDocComment(*memberDoc);
        }
        if (contains(stdInstanceMemberNames(receiverType->name), member)) {
            return missingDocText(receiverType->name + "." + member);
        }
    }
    if (receiverType->kind == TypeKind::Array) {
        const StdMemberDoc doc = stdArrayMemberDoc(member);
        if (!doc.empty()) return renderStdDoc(doc.signature, doc.body);
        return missingDocText("array." + member);
    }
    if (receiverType->kind == TypeKind::String) {
        const StdMemberDoc doc = stdStringMemberDoc(member);
        if (!doc.empty()) return renderStdDoc(doc.signature, doc.body);
        return missingDocText("string." + member);
    }
    return "";
}

std::vector<std::string> parameterNamesOf(const AnalyzedDocument& document,
                                          std::uint32_t nodeId) {
    std::vector<std::string> names;
    if (nodeId == 0) return names;
    if (document.entry == nullptr || document.entry->ast == nullptr) return names;
    const Node* found = nullptr;
    forEachNode(*document.entry->ast, [&](const Node& node) {
        if (found != nullptr) return;
        if (node.nodeId != nodeId) return;
        found = &node;
    });
    if (found == nullptr) return names;
    if (found->kind != NodeKind::FunctionDeclaration) return names;
    const auto& fn = static_cast<const FunctionDeclaration&>(*found);
    for (const ParameterPtr& parameter : fn.params) {
        if (parameter == nullptr || parameter->name == nullptr) continue;
        if (parameter->name->kind != NodeKind::Identifier) continue;
        names.push_back(
            static_cast<const Identifier&>(*parameter->name).name);
    }
    return names;
}

std::vector<std::string> parameterDocs(const AnalyzedDocument& document,
                                       const SymbolInfo* symbol,
                                       std::size_t count) {
    std::vector<std::string> docs;
    if (symbol == nullptr) return docs;
    const auto found = document.sema.docs.find(symbol->declNodeId);
    if (found == document.sema.docs.end()) return docs;
    const DocComment& doc = found->second;
    const std::vector<std::string> names =
        parameterNamesOf(document, symbol->declNodeId);
    for (std::size_t i = 0; i < count; ++i) {
        std::string text;
        // By name where the declaration's parameter names are known.
        if (i < names.size()) {
            for (const DocParam& param : doc.params) {
                if (param.name != names[i]) continue;
                text = param.text;
                break;
            }
        }
        // Otherwise by position.
        if (text.empty() && i < doc.params.size()) text = doc.params[i].text;
        docs.push_back(text);
    }
    return docs;
}

// =============================================================================
// The answers
// =============================================================================
DocAnswer stdMemberDocAt(const AnalyzedDocument& document,
                          const MemberExpr& member) {
    DocAnswer answer;
    const Expr* receiver = member.object.get();
    if (receiver == nullptr) return answer;

    // A std VALUE receiver: a Date, a Map, a Set, an array or a string.
    const Type* type = document.sema.typeOf(receiver->nodeId);
    if (type != nullptr) {
        StdMemberDoc doc;
        std::string label;
        std::vector<std::string> known;
        if (type->kind == TypeKind::Class) {
            doc = stdInstanceMemberDoc(type->name, member.property);
            label = type->name + "." + member.property;
            known = stdInstanceMemberNames(type->name);
        } else if (type->kind == TypeKind::Object) {
            doc = networkObjectMemberDoc(type->name, member.property,
                                         document.typeSystem);
            label = type->name + "." + member.property;
            known = networkObjectMemberNames(type->name);
        } else if (type->kind == TypeKind::Array) {
            doc = stdArrayMemberDoc(member.property);
            label = "array." + member.property;
            known = stdArrayMemberNames();
        } else if (type->kind == TypeKind::String) {
            doc = stdStringMemberDoc(member.property);
            label = "string." + member.property;
            known = stdStringMemberNames();
        }
        if (!doc.empty()) {
            answer.signature = doc.signature;
            answer.body = doc.body;
            return answer;
        }
        // The member exists but the tables do not document it: a visible gap,
        // never a silent blank.
        if (!label.empty() && contains(known, member.property)) {
            answer.body = missingDocText(label);
            return answer;
        }
    }

    // A std NAMESPACE receiver: `console.log`, `Math.abs`, `path.join`, ...
    if (receiver->kind != NodeKind::Identifier) return answer;
    const auto& id = static_cast<const Identifier&>(*receiver);
    if (!isStdNamespace(document, id.name)) return answer;
    const StdMemberDoc doc = stdNamespaceMemberDoc(id.name, member.property);
    if (!doc.empty()) {
        answer.signature = id.name + "." + doc.signature;
        answer.body = doc.body;
        return answer;
    }
    if (contains(stdNamespaceMemberNames(id.name), member.property)) {
        answer.body = missingDocText(id.name + "." + member.property);
    }
    return answer;
}

DocAnswer documentationAt(const AnalyzedDocument& document, const Node& node,
                           const std::string& name, const SymbolInfo* symbol) {
    DocAnswer answer;
    // The cursor is on the declaration itself: its own doc comment.
    const auto own = document.sema.docs.find(node.nodeId);
    if (own != document.sema.docs.end()) {
        answer.body = renderDocComment(own->second);
        return answer;
    }
    if (node.kind == NodeKind::MemberExpr) {
        const auto& member = static_cast<const MemberExpr&>(node);
        const DocAnswer std = stdMemberDocAt(document, member);
        if (!std.empty()) return std;
        // A member of a user class: the class's own doc comment.
        const Type* type = document.sema.typeOf(member.object != nullptr
                                                    ? member.object->nodeId
                                                    : 0);
        if (type != nullptr && type->kind == TypeKind::Class) {
            const DocComment* doc =
                classMemberDocAt(document, type->name, member.property);
            if (doc != nullptr) {
                answer.body = renderDocComment(*doc);
                return answer;
            }
        }
    }

    // The doc comment of the declaration the name refers to.
    const DocComment* doc = nullptr;
    if (symbol != nullptr) {
        const auto found = document.sema.docs.find(symbol->declNodeId);
        if (found != document.sema.docs.end()) doc = &found->second;
    }
    if (doc == nullptr) {
        // The cursor is on a class member's name: its key webserverion.
        doc = memberDocByKey(document, node.nodeId);
    }
    if (doc == nullptr && !name.empty() && document.file != nullptr) {
        doc = docCommentAt(document, name, node.span.begin.offset);
    }
    if (doc != nullptr) answer.body = renderDocComment(*doc);
    return answer;
}

}  // namespace cp3::cli
