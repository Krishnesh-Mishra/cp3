// =============================================================================
// src/cli/src/commands/lsp_intel_members.cpp - member-access completion.
//
// OWNERSHIP: cli module (src/cli/**). Completes the memberCandidates
// declaration in detail/lsp_intel_complete.hpp.
//
// The candidate surface is the documented receiver universe plus the shape
// fields this document declares (object literal keys, class/interface
// members, `: Interface` types). The list is then validated by ONE probe
// analysis: the document is re-analyzed with the member access replaced by an
// array literal reading every candidate, and only candidates the compiler
// gives a real (non-placeholder, non-control) type survive. Nothing the
// compiler would reject is offered.
// =============================================================================
#include "detail/lsp/lsp_intel_complete.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cp3/core/ast.hpp"
#include "cp3/lsp.hpp"
#include "cp3/std_surface.hpp"
#include "cp3/core/types.hpp"

#include "detail/lsp/lsp_intel_docs.hpp"

namespace cp3::cli {
namespace {


// -----------------------------------------------------------------------------
// Member candidates
// -----------------------------------------------------------------------------

// The documented member surfaces of the language's built-in receivers, unioned
// (the probe below removes whatever the receiver at hand does not have).
// Order is the docs order (string, array, collections, date, buffers,
// events/promises, fs, process); duplicates are removed on use.
const char* const kMemberUniverse[] = {
    "length", "charAt", "at", "charCodeAt", "startsWith", "endsWith",
    "includes", "repeat", "padStart", "padEnd", "toUpperCase", "toLowerCase",
    "trim", "trimStart", "trimEnd", "indexOf", "lastIndexOf", "slice",
    "substring", "replace", "replaceAll", "push", "unshift", "pop", "shift",
    "join", "sort", "fill", "reverse", "copyWithin", "subarray", "set", "get",
    "has", "delete", "clear", "keys", "values", "entries", "forEach", "add",
    "size", "toISOString", "toString", "getFullYear", "getMonth", "getDate",
    "getHours", "getMinutes", "getSeconds", "addDays", "addHours",
    "addMinutes", "addSeconds", "isBefore", "isAfter", "diffMs", "byteLength",
    "byteOffset", "BYTES_PER_ELEMENT", "buffer", "on", "once", "off", "emit",
    "then", "catch", "catchError", "finally", "isFile", "isDirectory",
    "modifiedAt", "stdout", "stderr", "stdin", "exitCode", "write", "kill",
};

void appendUnique(std::vector<std::string>& names, const std::string& name) {
    if (name.empty()) return;
    for (const std::string& existing : names) {
        if (existing == name) return;
    }
    names.push_back(name);
}

void appendObjectKeys(std::vector<std::string>& names, const ObjectLiteral& literal) {
    for (const ObjectPropertyPtr& property : literal.properties) {
        if (property == nullptr) continue;
        appendUnique(names, keyName(property->key.get()));
    }
}

void appendClassMemberKeys(std::vector<std::string>& names,
                           const ClassDeclaration& declaration) {
    for (const ClassMemberPtr& member : declaration.members) {
        if (member == nullptr) continue;
        if (member->kind == NodeKind::PropertyDeclaration) {
            appendUnique(names,
                         keyName(static_cast<const PropertyDeclaration&>(*member).key.get()));
        } else if (member->kind == NodeKind::MethodDeclaration) {
            appendUnique(names,
                         keyName(static_cast<const MethodDeclaration&>(*member).key.get()));
        } else if (member->kind == NodeKind::AccessorDeclaration) {
            appendUnique(names,
                         keyName(static_cast<const AccessorDeclaration&>(*member).key.get()));
        }
    }
}

void appendInterfaceMemberKeys(std::vector<std::string>& names,
                               const InterfaceDeclaration& declaration) {
    for (const SignatureMemberPtr& member : declaration.members) {
        if (member == nullptr) continue;
        if (member->kind == NodeKind::PropertySignature) {
            appendUnique(names,
                         keyName(static_cast<const PropertySignature&>(*member).key.get()));
        } else if (member->kind == NodeKind::MethodSignature) {
            appendUnique(names,
                         keyName(static_cast<const MethodSignature&>(*member).key.get()));
        }
    }
}

// Members of the interface `typeName` declared in this file (`const s: Shape`).
void appendTypeShapeKeys(const Program& program, const SourceFile& file,
                         const std::string& typeName,
                         std::vector<std::string>& names) {
    if (typeName.empty()) return;
    for (const StmtPtr& statement : program.body) {
        if (statement == nullptr || !statement->span.valid() ||
            statement->span.file != &file) {
            continue;
        }
        if (statement->kind != NodeKind::InterfaceDeclaration) continue;
        const auto& declaration = static_cast<const InterfaceDeclaration&>(*statement);
        if (declaration.name == typeName) appendInterfaceMemberKeys(names, declaration);
    }
}

// Shape fields the receiver's own declaration carries: object literal keys and
// `: Interface` members for variables, plus class/interface members when the
// receiver names the type itself.
void appendShapeFields(const AnalyzedDocument& document,
                       const std::string& receiver,
                       std::vector<std::string>& names) {
    if (receiver.empty() || lastIdentifier(receiver) != receiver) return;
    if (document.entry == nullptr || document.entry->ast == nullptr) return;
    const Program& program = *document.entry->ast;
    for (const StmtPtr& statement : program.body) {
        if (statement == nullptr || !statement->span.valid() ||
            statement->span.file != document.file) {
            continue;
        }
        if (statement->kind == NodeKind::ClassDeclaration) {
            const auto& declaration = static_cast<const ClassDeclaration&>(*statement);
            if (declaration.name == receiver) appendClassMemberKeys(names, declaration);
        } else if (statement->kind == NodeKind::InterfaceDeclaration) {
            const auto& declaration = static_cast<const InterfaceDeclaration&>(*statement);
            if (declaration.name == receiver) appendInterfaceMemberKeys(names, declaration);
        } else if (statement->kind == NodeKind::VariableDeclaration) {
            const auto& declaration = static_cast<const VariableDeclaration&>(*statement);
            for (const VariableDeclaratorPtr& declarator : declaration.declarations) {
                if (declarator == nullptr || declarator->id == nullptr) continue;
                if (declarator->id->kind != NodeKind::Identifier) continue;
                if (static_cast<const Identifier&>(*declarator->id).name != receiver) {
                    continue;
                }
                if (declarator->init != nullptr &&
                    declarator->init->kind == NodeKind::ObjectLiteral) {
                    appendObjectKeys(
                        names, static_cast<const ObjectLiteral&>(*declarator->init));
                }
                if (declarator->type != nullptr &&
                    declarator->type->kind == NodeKind::NamedType) {
                    appendTypeShapeKeys(
                        program, *document.file,
                        static_cast<const NamedType&>(*declarator->type).name, names);
                }
            }
        }
    }
}

// The receiver node can be absent or have no type while the user is midway
// through `response.`. The binding table often still has the contextual type
// (notably for HTTP callback parameters), so consult it before falling back to
// a broad candidate universe.
const Type* receiverTypeForCompletion(const AnalyzedDocument& document,
                                      const std::string& receiver,
                                      std::uint32_t dot) {
    if (const Type* type = receiverTypeEndingAt(document, dot)) return type;
    if (document.file == nullptr || receiver.empty() ||
        lastIdentifier(receiver) != receiver) {
        return nullptr;
    }
    const SymbolInfo* symbol = nearestSymbolAt(document.sema, receiver,
                                               *document.file, dot);
    return symbol == nullptr ? nullptr : &symbol->type;
}

bool isAmbientSurface(const Type* type) {
    return type != nullptr && type->kind == TypeKind::Object &&
           type->name == "$ambient";
}

// A useful fail-closed completion result when the probe analysis could not
// finish. Only exact standard-library surfaces and source-declared fields are
// included; the global member union below is deliberately not used here.
std::vector<lsp::CompletionCandidate> knownReceiverCandidates(
    const AnalyzedDocument& document, const std::string& receiver,
    const Type* receiverType) {
    std::vector<std::string> names;
    std::string namespaceName;
    const std::string root = lastIdentifier(receiver);
    if (!root.empty() && isAmbientSurface(receiverType)) {
        const std::vector<std::string> namespaceNames =
            stdNamespaceMemberNames(root);
        if (!namespaceNames.empty()) {
            namespaceName = root;
            for (const std::string& name : namespaceNames) {
                appendUnique(names, name);
            }
        }
    }
    if (namespaceName.empty() && receiverType != nullptr) {
        if (receiverType->kind == TypeKind::Object) {
            for (const std::string& name :
                 networkObjectMemberNames(receiverType->name)) {
                appendUnique(names, name);
            }
        } else if (receiverType->kind == TypeKind::Class) {
            for (const std::string& name :
                 stdInstanceMemberNames(receiverType->name)) {
                appendUnique(names, name);
            }
            appendShapeFields(document, receiverType->name, names);
        } else if (receiverType->kind == TypeKind::Array) {
            for (const std::string& name : stdArrayMemberNames()) {
                appendUnique(names, name);
            }
        } else if (receiverType->kind == TypeKind::String) {
            for (const std::string& name : stdStringMemberNames()) {
                appendUnique(names, name);
            }
        }
    }
    appendShapeFields(document, receiver, names);

    std::vector<lsp::CompletionCandidate> result;
    for (const std::string& name : names) {
        lsp::CompletionCandidate candidate;
        candidate.label = name;
        std::string signature;
        if (!namespaceName.empty()) {
            signature = stdNamespaceMemberDoc(namespaceName, name).signature;
        } else if (receiverType != nullptr &&
                   receiverType->kind == TypeKind::Object) {
            signature =
                networkObjectMemberDoc(receiverType->name, name,
                                       document.typeSystem)
                    .signature;
        } else if (receiverType != nullptr &&
                   receiverType->kind == TypeKind::Class) {
            signature = stdInstanceMemberDoc(receiverType->name, name).signature;
        } else if (receiverType != nullptr &&
                   receiverType->kind == TypeKind::Array) {
            signature = stdArrayMemberDoc(name).signature;
        } else if (receiverType != nullptr &&
                   receiverType->kind == TypeKind::String) {
            signature = stdStringMemberDoc(name).signature;
        }
        candidate.detail = signature;
        candidate.kind = signature.find('(') != std::string::npos
                             ? (namespaceName.empty() ? 2 : 3)
                             : 5;
        candidate.documentation = completionMemberDoc(
            document, namespaceName, receiverType, name);
        result.push_back(std::move(candidate));
    }
    return result;
}

std::vector<std::string> candidateNames(const AnalyzedDocument& document,
                                        const std::string& receiver,
                                        std::uint32_t dot) {
    const std::string last = lastIdentifier(receiver);
    const Type* receiverType = receiverTypeForCompletion(document, receiver, dot);
    if (!last.empty() && isAmbientSurface(receiverType)) {
        // A closed std namespace has an exact table: no need to union.
        std::vector<std::string> stdNames = stdNamespaceMemberNames(last);
        if (!stdNames.empty()) return stdNames;
    }
    std::vector<std::string> names;
    if (receiverType != nullptr && receiverType->kind == TypeKind::Object) {
        for (const std::string& name :
             networkObjectMemberNames(receiverType->name)) {
            appendUnique(names, name);
        }
    } else if (receiverType == nullptr) {
        // A parser recovery node may leave no exact receiver span. Offer the
        // server API names as probe candidates; the analysis below keeps only
        // members the compiler can resolve on this actual receiver.
        for (const std::string& name :
             networkObjectMemberNames("$network:HttpServer")) {
            appendUnique(names, name);
        }
    }
    if (receiverType != nullptr && receiverType->kind == TypeKind::Class) {
        for (const std::string& name :
             stdInstanceMemberNames(receiverType->name)) {
            appendUnique(names, name);
        }
        appendShapeFields(document, receiverType->name, names);
    }
    for (const char* name : kMemberUniverse) appendUnique(names, name);
    appendShapeFields(document, receiver, names);
    return names;
}

std::vector<lsp::CompletionCandidate> sourceShapeCandidates(
    const AnalyzedDocument& document, const std::string& receiver,
    const std::string& namespaceName, const Type* receiverType) {
    std::vector<std::string> names;
    appendShapeFields(document, receiver, names);
    // A nominal class receiver's members are declared directly on its class
    // and do not require the synthetic probe either.
    if (receiverType != nullptr && receiverType->kind == TypeKind::Class) {
        appendShapeFields(document, receiverType->name, names);
    }
    std::vector<lsp::CompletionCandidate> result;
    for (const std::string& name : names) {
        lsp::CompletionCandidate candidate;
        candidate.label = name;
        candidate.kind = 5;
        candidate.documentation = completionMemberDoc(
            document, namespaceName, receiverType, name);
        result.push_back(std::move(candidate));
    }
    return result;
}

constexpr const char* kProbeAbsent = "__cp3_lsp_probe_absent";

std::string probeArrayText(const std::string& receiver,
                           const std::vector<std::string>& names) {
    std::string out = "[";
    for (const std::string& name : names) {
        out += receiver;
        out += '.';
        out += name;
        out += ", ";
    }
    out += receiver;
    out += '.';
    out += kProbeAbsent;
    out += ']';
    return out;
}

const ArrayLiteral* arrayStartingAt(const Program& program,
                                    std::uint32_t offset) {
    const ArrayLiteral* found = nullptr;
    forEachNode(program, [&](const Node& node) {
        if (found != nullptr || node.kind != NodeKind::ArrayLiteral) return;
        if (node.span.begin.offset != offset) return;
        found = static_cast<const ArrayLiteral*>(&node);
    });
    return found;
}
}  // namespace

// One probe analysis keeps the candidates the compiler can actually resolve.
std::vector<lsp::CompletionCandidate> memberCandidates(
    const AnalyzedDocument& document, const std::string& text,
    const CompletionContext& context, std::uint32_t offset) {
    std::vector<lsp::CompletionCandidate> candidates;
    if (context.receiverBegin >= context.dot) return candidates;
    const std::string receiver =
        text.substr(context.receiverBegin, context.dot - context.receiverBegin);
    const std::vector<std::string> names =
        candidateNames(document, receiver, context.dot);
    if (names.empty()) return candidates;
    const Type* sourceReceiverType =
        receiverTypeForCompletion(document, receiver, context.dot);
    std::string probe = text;
    probe.replace(context.receiverBegin, offset - context.receiverBegin,
                  probeArrayText(receiver, names));
    auto probed = analyzeDocument(document.file->path, probe);
    const std::string root = lastIdentifier(receiver);
    const std::string namespaceName =
        isAmbientSurface(sourceReceiverType) &&
                !stdNamespaceMemberNames(root).empty()
            ? root
            : std::string();
    if (!analysisReady(*probed)) {
        candidates = knownReceiverCandidates(document, receiver,
                                             sourceReceiverType);
        if (candidates.empty()) {
            return sourceShapeCandidates(document, receiver, namespaceName,
                                         sourceReceiverType);
        }
        return candidates;
    }
    const ArrayLiteral* array =
        arrayStartingAt(*probed->entry->ast, context.receiverBegin);
    if (array == nullptr || array->elements.size() != names.size() + 1) {
        candidates = knownReceiverCandidates(document, receiver,
                                             sourceReceiverType);
        if (candidates.empty()) {
            return sourceShapeCandidates(document, receiver, namespaceName,
                                         sourceReceiverType);
        }
        return candidates;
    }
    const Type* control = probed->sema.typeOf(array->elements.back()->nodeId);
    // The receiver's type (an array, a string, a user class) and the std
    // namespace the receiver names: both decide what a candidate's
    // documentation is.
    const Type* receiverType = nullptr;
    if (!array->elements.empty() && array->elements.front() != nullptr &&
        array->elements.front()->kind == NodeKind::MemberExpr) {
        const auto& member = static_cast<const MemberExpr&>(*array->elements.front());
        if (member.object != nullptr) {
            receiverType = probed->sema.typeOf(member.object->nodeId);
        }
    }
    for (std::size_t i = 0; i < names.size(); ++i) {
        const ExprPtr& element = array->elements[i];
        if (element == nullptr) continue;
        const Type* type = probed->sema.typeOf(element->nodeId);
        if (type == nullptr || type->isError() || type->isUnknown()) continue;
        if (control != nullptr && *type == *control) continue;
        lsp::CompletionCandidate candidate;
        candidate.label = names[i];
        candidate.kind = type->kind == TypeKind::Function ? 3 : 5;
        // An ambient surface has no printable type of its own: the
        // documented signature (in the documentation) is what the editor
        // shows instead of the internal placeholder label.
        candidate.detail = isPlaceholderType(*type) ? "" : describe(*type);
        candidate.documentation =
            completionMemberDoc(document, namespaceName, receiverType,
                                names[i]);
        candidates.push_back(std::move(candidate));
    }
    if (candidates.empty()) {
        candidates = knownReceiverCandidates(document, receiver,
                                             sourceReceiverType);
        if (!candidates.empty()) return candidates;
    }
    // Keep source-declared fields visible if the parser recovered the rest of
    // the document but the synthetic probe could not type one candidate. The
    // shape is read from the original object/class/interface declaration, so
    // these are real members rather than guesses from the global universe.
    for (lsp::CompletionCandidate& candidate :
         sourceShapeCandidates(document, receiver, namespaceName,
                               sourceReceiverType)) {
        bool present = false;
        for (const lsp::CompletionCandidate& existing : candidates) {
            if (existing.label == candidate.label) {
                present = true;
                break;
            }
        }
        if (!present) candidates.push_back(std::move(candidate));
    }
    return candidates;
}


}  // namespace cp3::cli
