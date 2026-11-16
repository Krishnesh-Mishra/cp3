// =============================================================================
// src/cli/src/commands/lsp_intel_members.cpp - member-access completion +
// member references & rename fallback.
//
// OWNERSHIP: cli module (src/cli/**). Completes the memberCandidates
// declaration in detail/lsp_intel_complete.hpp, and implements the
// memberRetarget / memberOccurrences declarations in detail/lsp_intel.hpp.
//
// The completion candidate surface is the documented receiver universe plus
// the shape fields this document declares (object literal keys, class/interface
// members, `: Interface` types). The list is then validated by ONE probe
// analysis: the document is re-analyzed with the member access replaced by an
// array literal reading every candidate, and only candidates the compiler
// gives a real (non-placeholder, non-control) type survive. Nothing the
// compiler would reject is offered.
//
// The member references/rename fallback handles the positions the binding
// pipeline in lsp_intel_references.cpp must refuse (a member key, an enum
// member name, an object-literal property key, the member side of a
// `receiver.member` access). It answers only from what the compiler can
// prove:
//   * the receiver's exact sema type (`typeOf`): a class/enum matched by
//     the owner nominal name, an object matched by the synthesized shape
//     label;
//   * same-file "interface IMPLEMENTATIONS" (classes implementing an interface
//     with the same member name join the family) and same-file extends
//     chains (a rename unifies every same-name declaration in the chain so a
//     drop-in rewrite stays consistent);
//   * direct import edges from the unit at hand to the owner unit (unaliased
//     or aliased named/default bindings of the family name, with no local
//     same-name declaration shadowing it).
// Anything it cannot prove - computed member keys, shorthand properties,
// unanalyzable or missing units, unlinked cross-file override/implementation -
// is REFUSED wholesale: an empty answer, never a partial rename.
// Occurrences are deduplicated by span; the override shadow sets for class
// members are treated as separate symbols when answering references (like the
// binding pipeline) and as one symbol when renaming (a half-renamed chain is
// broken code).
// =============================================================================
#include "detail/lsp/lsp_intel_complete.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cp3/core/ast.hpp"
#include "cp3/lsp.hpp"
#include "cp3/std_surface.hpp"
#include "cp3/core/types.hpp"
#include "cp3/core/source.hpp"
#include "cp3/diag/diagnostic.hpp"
#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/host/options.hpp"
#include "cp3/meta/module.hpp"
#include "cp3/core/analysis.hpp"

#include "detail/lsp/lsp_intel_docs.hpp"
#include "detail/pkg/package_json.hpp"
#include "detail/toolchain/pipeline.hpp"

namespace cp3::cli {

// Forward declaration: the definition lives further down the translation unit,
// outside the anonymous namespace below.
bool synthKeyName(const Expr* key, bool computed, std::string& name);

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

// The `extends` clause of a class, as a plain type name. Anything more
// expressive than a plain identifier (a qualified base, a computed name) is
// not followed.
std::string baseNameOf(const ClassDeclaration& declaration) {
    if (declaration.extendsType == nullptr) return std::string();
    if (declaration.extendsType->kind != NodeKind::NamedType) return std::string();
    return static_cast<const NamedType&>(*declaration.extendsType).name;
}

// The binding name of a parameter: the identifier it binds, the argument of
// a rest parameter, nothing for a pattern.
std::string bindingNameOfParameter(const Parameter& parameter) {
    const Node* name = parameter.name.get();
    if (name == nullptr) return std::string();
    if (name->kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*name).name;
    }
    if (name->kind == NodeKind::RestElement) {
        const auto& rest = static_cast<const RestElement&>(*name);
        if (rest.argument != nullptr &&
            rest.argument->kind == NodeKind::Identifier) {
            return static_cast<const Identifier&>(*rest.argument).name;
        }
    }
    return std::string();
}

// The declared type of a receiver, read from the source: a variable or
// parameter annotation names its type, and `new Widget()` names the class.
// The resolved type of an imported owner is a structural shape label, so the
// declaration site is what names the owner.
std::string declaredTypeNameOfLocal(const Program& program,
                                    const std::string& name) {
    if (name.empty()) return std::string();
    std::string found;
    forEachNode(program, [&](const Node& node) {
        if (!found.empty()) return;
        if (node.kind == NodeKind::Parameter) {
            const auto& parameter = static_cast<const Parameter&>(node);
            if (bindingNameOfParameter(parameter) != name) return;
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

// Collects the members of the class/interface `name`, walking the extends
// chain across module boundaries. Entry-declaration wins over an imported one
// of the same name (the units are ordered entry first and the walk stops at
// the first declaration found). `seen` breaks cycles in a self-referential
// chain.
void appendImportedTypeMembers(const std::vector<const CompilationUnit*>& units,
                               const std::string& name, int depth,
                               std::unordered_set<std::string>& seen,
                               std::vector<std::string>& names) {
    if (name.empty() || depth > 16) return;
    if (!seen.insert(name).second) return;
    for (const CompilationUnit* unit : units) {
        if (unit == nullptr || unit->ast == nullptr) continue;
        for (const StmtPtr& statement : unit->ast->body) {
            if (statement == nullptr || !statement->span.valid()) continue;
            if (unit->file != nullptr && statement->span.file != unit->file) {
                continue;  // a prelude tail riding in this unit
            }
            // `export class Store { ... }` is wrapped in an ExportDeclaration.
            const Stmt* declared = statement.get();
            if (statement->kind == NodeKind::ExportDeclaration) {
                const auto& declaration =
                    static_cast<const ExportDeclaration&>(*statement);
                if (declaration.declaration == nullptr) continue;
                declared = declaration.declaration.get();
            }
            if (declared->kind == NodeKind::ClassDeclaration) {
                const auto& declaration =
                    static_cast<const ClassDeclaration&>(*declared);
                if (declaration.name != name) continue;
                appendClassMemberKeys(names, declaration);
                appendImportedTypeMembers(units, baseNameOf(declaration),
                                          depth + 1, seen, names);
                return;
            }
            if (declared->kind == NodeKind::InterfaceDeclaration) {
                const auto& declaration =
                    static_cast<const InterfaceDeclaration&>(*declared);
                if (declaration.name != name) continue;
                appendInterfaceMemberKeys(names, declaration);
                for (const TypePtr& base : declaration.extendsTypes) {
                    if (base == nullptr || base->kind != NodeKind::NamedType) {
                        continue;
                    }
                    appendImportedTypeMembers(
                        units, static_cast<const NamedType&>(*base).name,
                        depth + 1, seen, names);
                }
                return;
            }
        }
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
    // An imported nominal type is not declared in this file at all
    // (`new Store()`, `x: P`), and a class may inherit its members from an
    // imported base. Resolve the receiver's declared type name and walk the
    // analyzed import closure for both.
    const std::vector<const CompilationUnit*> units = reachableUnitsFor(document);
    std::unordered_set<std::string> seen;
    const std::string declared = declaredTypeNameOfLocal(program, receiver);
    if (!declared.empty()) {
        appendImportedTypeMembers(units, declared, 0, seen, names);
    }
    appendImportedTypeMembers(units, receiver, 0, seen, names);
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

// --- union receivers -------------------------------------------------------
// The checker keeps union member reads permissive by design (a member missing
// on one arm is ambient, not an error), so the probe analysis cannot tell
// which arm a candidate came from. A union or `T | null | undefined` receiver
// therefore offers only the members every non-nullish part carries: the
// intersection of the part surfaces, deduplicated in first-seen order. A part
// whose surface the tables cannot decode contributes nothing, which is how
// foreign members stay off the list (`string | number` has no provable common
// member and offers nothing).

std::vector<std::string> surfaceNamesForPart(const AnalyzedDocument& document,
                                             const Type& part) {
    switch (part.kind) {
        case TypeKind::String:
            return stdStringMemberNames();
        case TypeKind::Number:
            return stdInstanceMemberNames("Number");
        case TypeKind::Boolean:
            return stdInstanceMemberNames("Boolean");
        case TypeKind::Array:
            return stdArrayMemberNames();
        case TypeKind::Class: {
            std::vector<std::string> names =
                part.name.empty() ? std::vector<std::string>{}
                                  : stdInstanceMemberNames(part.name);
            if (!part.name.empty()) {
                const std::vector<const CompilationUnit*> units =
                    reachableUnitsFor(document);
                std::unordered_set<std::string> seen;
                appendImportedTypeMembers(units, part.name, 0, seen, names);
            }
            return names;
        }
        case TypeKind::TypeParam:
            return part.parts.empty()
                       ? std::vector<std::string>{}
                       : surfaceNamesForPart(document, part.parts.front());
        case TypeKind::Nullable:
        case TypeKind::Union: {
            std::vector<const Type*> members;
            std::function<void(const Type&)> collect =
                [&members, &collect](const Type& t) {
                    if (t.kind == TypeKind::Union ||
                        t.kind == TypeKind::Nullable) {
                        for (const Type& inner : t.parts) collect(inner);
                        return;
                    }
                    if (t.isNullish()) return;
                    members.push_back(&t);
                };
            collect(part);
            if (members.empty()) return {};
            std::vector<std::string> common =
                surfaceNamesForPart(document, *members.front());
            for (std::size_t i = 1; i < members.size() && !common.empty(); ++i) {
                const std::vector<std::string> other =
                    surfaceNamesForPart(document, *members[i]);
                std::vector<std::string> next;
                for (const std::string& name : common) {
                    for (const std::string& candidate : other) {
                        if (candidate == name) {
                            next.push_back(name);
                            break;
                        }
                    }
                }
                common.swap(next);
            }
            return common;
        }
        case TypeKind::Literal:
            return surfaceNamesForPart(document, part.literalBaseType());
        default:
            // An enum, a function, a tuple or record shape, an ambient
            // placeholder: nothing the tables can prove, so contribute
            // nothing and let the intersection drop the name.
            return {};
    }
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
        } else if (receiverType->kind == TypeKind::Number) {
            // `number` has a closed std cast surface (toFixed, toPrecision,
            // toExponential, toString); it decides the list like every other
            // exact receiver.
            for (const std::string& name : stdInstanceMemberNames("Number")) {
                appendUnique(names, name);
            }
        } else if (receiverType->kind == TypeKind::Boolean) {
            // `boolean` has a closed std surface (toString), mirroring
            // number/string.
            for (const std::string& name : stdInstanceMemberNames("Boolean")) {
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
                   receiverType->kind == TypeKind::Number) {
            signature = stdInstanceMemberDoc("Number", name).signature;
        } else if (receiverType != nullptr &&
                   receiverType->kind == TypeKind::Boolean) {
            signature = stdInstanceMemberDoc("Boolean", name).signature;
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
    // The number, array and string surfaces have their own tables in the
    // compiler: they are the authoritative list, so an editor cannot hide a
    // documented member (the universe below is only the fallback for a
    // receiver whose type the checker could not resolve).
    if (receiverType != nullptr && receiverType->kind == TypeKind::Number) {
        for (const std::string& name : stdInstanceMemberNames("Number")) {
            appendUnique(names, name);
        }
    } else if (receiverType != nullptr &&
               receiverType->kind == TypeKind::Boolean) {
        for (const std::string& name : stdInstanceMemberNames("Boolean")) {
            appendUnique(names, name);
        }
    } else if (receiverType != nullptr && receiverType->kind == TypeKind::Array) {
        for (const std::string& name : stdArrayMemberNames()) {
            appendUnique(names, name);
        }
    } else if (receiverType != nullptr && receiverType->kind == TypeKind::String) {
        for (const std::string& name : stdStringMemberNames()) {
            appendUnique(names, name);
        }
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
// (Outside the anonymous namespace: lsp_intel_complete.cpp calls it.)
// The probe analysis reports an unknown member (CP3305): a candidate the
// compiler does not know is never offered. The diagnostic names the member it
// is about ("<type> has no member 'x'"), which is what tells the candidates
// apart - the report itself is anchored at the enclosing statement.
bool reportsUnknownMember(const std::vector<Diagnostic>& diagnostics,
                           const std::string& name) {
    for (const Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.code != diagcode::kMemberNotFound) continue;
        const std::size_t head = diagnostic.message.find("has no member ");
        if (head == std::string::npos) continue;
        const std::size_t open = diagnostic.message.find('\'', head);
        if (open == std::string::npos) continue;
        const std::size_t close = diagnostic.message.find('\'', open + 1);
        if (close == std::string::npos) continue;
        if (diagnostic.message.compare(open + 1, close - open - 1, name) == 0) {
            return true;
        }
    }
    return false;
}

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
    // A union (or `T | null | undefined`) receiver offers only the members
    // every non-nullish part carries; the probe analysis is permissive for
    // unions by design, so the intersection is decided here instead.
    if (sourceReceiverType != nullptr &&
        (sourceReceiverType->kind == TypeKind::Union ||
         sourceReceiverType->kind == TypeKind::Nullable)) {
        std::vector<lsp::CompletionCandidate> result;
        for (const std::string& name :
             surfaceNamesForPart(document, *sourceReceiverType)) {
            lsp::CompletionCandidate candidate;
            candidate.label = name;
            candidate.kind = 5;
            candidate.detail = "";
            result.push_back(std::move(candidate));
        }
        return result;
    }
    // A number, an array or a string has an exact member table in the
    // compiler, and that table IS the candidate list: no probe needed (a
    // generic method - `map`, `reduce` - has no type until it is called).
    if (sourceReceiverType != nullptr &&
        (sourceReceiverType->kind == TypeKind::Array ||
         sourceReceiverType->kind == TypeKind::String ||
         sourceReceiverType->kind == TypeKind::Number ||
         sourceReceiverType->kind == TypeKind::Boolean)) {
        return knownReceiverCandidates(document, receiver, sourceReceiverType);
    }
    std::string probe = text;    probe.replace(context.receiverBegin, offset - context.receiverBegin,
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
        // A member the compiler does not know is never offered: the probe
        // analysis reports it (CP3305) and the access has no type.
        if (reportsUnknownMember(probed->diagnostics, names[i])) continue;
        const Type* type = probed->sema.typeOf(element->nodeId);
        // An ambient placeholder means the member does not exist on this
        // receiver (an unknown surface, an unknown class member).
        if (type == nullptr || type->isError() || type->isUnknown() ||
            isPlaceholderType(*type)) {
            continue;
        }
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

namespace {

// ==========================================================================
// Member / property / enum-member references & rename fallback
//
// The binding pipeline refuses member keys, object-literal keys, enum members
// and enum member specifier positions. The machinery below answers those
// positions instead: resolve the cursor to a member symbol, then collect
// that member`s occurrences in every unit of the graph that can prove one.
// ==========================================================================

const char* const kMemberOwnerLiteral = "$o{";

struct MemberOccurrence {
    const SourceFile* file = nullptr;
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
    bool isDeclaration = false;
};

void addMemberOccurrence(std::vector<MemberOccurrence>& out,
                         const SourceSpan& span, bool isDeclaration) {
    if (!span.valid() || span.file == nullptr) return;
    MemberOccurrence occurrence;
    occurrence.file = span.file;
    occurrence.begin = span.begin.offset;
    occurrence.end = span.end.offset;
    occurrence.isDeclaration = isDeclaration;
    for (MemberOccurrence& existing : out) {
        if (existing.file == occurrence.file &&
            existing.begin == occurrence.begin &&
            existing.end == occurrence.end) {
            existing.isDeclaration = existing.isDeclaration || occurrence.isDeclaration;
            return;
        }
    }
    out.push_back(occurrence);
}

bool memberIsIdentByte(unsigned char c) {
    return std::isalnum(c) != 0 || c == '_' || c == '$';
}

bool memberCarveName(const SourceSpan& span, const std::string& name,
                     SourceSpan* out) {
    if (!span.valid() || span.file == nullptr || name.empty() || out == nullptr) {
        return false;
    }
    const std::string& text = span.file->text;
    const std::uint32_t limit = span.end.offset;
    const std::size_t wanted = name.size();
    std::uint32_t at = span.begin.offset;
    for (;;) {
        const std::size_t hit = text.find(name, at);
        if (hit == std::string::npos) return false;
        if (hit >= limit || hit + wanted > limit) return false;
        const bool leftOk = hit == 0 ||
            !memberIsIdentByte(static_cast<unsigned char>(text[hit - 1]));
        const bool rightOk = hit + wanted >= text.size() ||
            !memberIsIdentByte(static_cast<unsigned char>(text[hit + wanted]));
        if (leftOk && rightOk) {
            SourceSpan found;
            found.file = span.file;
            found.begin = span.file->positionAt(static_cast<std::uint32_t>(hit));
            found.end =
                span.file->positionAt(static_cast<std::uint32_t>(hit + wanted));
            *out = found;
            return true;
        }
        at = static_cast<std::uint32_t>(hit + 1);
    }
}

bool memberSpanCovers(const SourceSpan& outer, const SourceSpan& inner) {
    if (!outer.valid() || !inner.valid() || outer.file != inner.file) return false;
    return outer.begin.offset <= inner.begin.offset &&
           inner.end.offset <= outer.end.offset;
}

bool memberSpanEquals(const SourceSpan& span, const SourceSpan& other) {
    return span.file == other.file && span.begin.offset == other.begin.offset &&
           span.end.offset == other.end.offset;
}

// Fresh analysis of `unit` (the entry borrows the already-analyzed document).
// Prefers the sibling sema the document analysis already produced (the import
// wiring keeps it around); falls back to a fresh per-unit analysis.
struct MemberUnitAnalysis {
    CompilationUnit* unit = nullptr;
    const Program* program = nullptr;
    const SourceFile* file = nullptr;
    const SemaResult* sema = nullptr;
    std::unique_ptr<SemaResult> owned;
};

MemberUnitAnalysis memberAnalyzeUnit(AnalyzedDocument& document, CompilationUnit* unit,
                                     const CompileOptions& compile) {
    MemberUnitAnalysis view;
    view.unit = unit;
    view.program = unit != nullptr && unit->ast != nullptr ? unit->ast.get() : nullptr;
    view.file = unit != nullptr ? unit->file : nullptr;
    if (unit != nullptr && unit->isEntry) {
        view.sema = &document.sema;
        return view;
    }
    const auto cached =
        unit != nullptr ? document.unitSemas.find(unit->id) : document.unitSemas.end();
    if (cached != document.unitSemas.end()) {
        view.owned = std::make_unique<SemaResult>(cached->second);
        view.sema = view.owned.get();
        return view;
    }
    view.owned = std::make_unique<SemaResult>();
    CollectingDiagnosticSink sink;
    if (unit != nullptr && unit->ast != nullptr) {
        analyzeProgram(*unit->ast, *view.owned, sink, compile, nullptr);
    }
    view.sema = view.owned.get();
    return view;
}

CompileOptions memberCompileOptionsFor(const AnalyzedDocument& document,
                                       const std::string& path) {
    CompileOptions compile;
    std::filesystem::path root;
    if (findPackageRoot(std::filesystem::path(path).parent_path(), root)) {
        applyProjectTypeSettings(root, compile);
    }
    if (document.graph != nullptr) {
        compile.nativeBindings = document.graph->nativeBindings();
    }
    return compile;
}

// The span of the property part of `receiver.member` inside the whole
// MemberExpr span: the bytes at the end of the span, back-checked against the
// `.` they must follow (`?.` included). Anything else carves nothing and
// produces no occurrence.
bool memberExprPropertySpan(const MemberExpr& member, SourceSpan* out) {
    const std::string& property = member.property;
    const SourceSpan& span = member.span;
    if (property.empty() || !span.valid() || span.file == nullptr || out == nullptr) {
        return false;
    }
    const std::string& text = span.file->text;
    const std::size_t end = span.end.offset;
    const std::size_t wanted = property.size();
    if (end < wanted || end > text.size()) return false;
    const std::size_t at = end - wanted;
    if (text.compare(at, wanted, property) != 0) return false;
    const bool dotted = at >= 1 && text[at - 1] == '.';
    const bool opted = at >= 2 && text[at - 1] == '?' && text[at - 2] == '.';
    if (!dotted && !opted) return false;
    SourceSpan carved;
    carved.file = span.file;
    carved.begin = span.file->positionAt(static_cast<std::uint32_t>(at));
    carved.end = span.file->positionAt(static_cast<std::uint32_t>(end));
    *out = carved;
    return true;
}

bool memberKeyOf(const Node& member, const Expr*& key, bool& computed) {
    switch (member.kind) {
        case NodeKind::PropertyDeclaration:
            key = static_cast<const PropertyDeclaration&>(member).key.get();
            computed = static_cast<const PropertyDeclaration&>(member).computed;
            return true;
        case NodeKind::MethodDeclaration:
            key = static_cast<const MethodDeclaration&>(member).key.get();
            computed = static_cast<const MethodDeclaration&>(member).computed;
            return true;
        case NodeKind::AccessorDeclaration:
            key = static_cast<const AccessorDeclaration&>(member).key.get();
            computed = static_cast<const AccessorDeclaration&>(member).computed;
            return true;
        case NodeKind::PropertySignature:
            key = static_cast<const PropertySignature&>(member).key.get();
            computed = static_cast<const PropertySignature&>(member).computed;
            return true;
        case NodeKind::MethodSignature:
            key = static_cast<const MethodSignature&>(member).key.get();
            computed = static_cast<const MethodSignature&>(member).computed;
            return true;
        case NodeKind::ObjectProperty:
            key = static_cast<const ObjectProperty&>(member).key.get();
            computed = static_cast<const ObjectProperty&>(member).computed;
            return true;
        default:
            return false;
    }
}

bool memberIsAbstract(const Node& member) {
    switch (member.kind) {
        case NodeKind::PropertyDeclaration:
            return static_cast<const PropertyDeclaration&>(member).isAbstract;
        case NodeKind::MethodDeclaration:
            return static_cast<const MethodDeclaration&>(member).isAbstract;
        default:
            return false;
    }
}

std::string memberTypeKey(const Type& t);

// The printable shape label a signature-member list produces: the same
// `$o{name:typeKey,...}` spelling `makeObjectType` builds (fields in
// declaration order, then `*:<typeKey>,` when an index signature is present).
// Only plain (non-computed) keys synthesize; anything else bails so the
// caller can fail closed.
bool synthSignatureMembersLabel(const std::vector<SignatureMemberPtr>& members,
                                const SemaResult& sema, std::string& label) {
    std::string out = kMemberOwnerLiteral;
    std::string indexKey;
    bool hasIndex = false;
    for (const SignatureMemberPtr& member : members) {
        if (member == nullptr) return false;
        switch (member->kind) {
            case NodeKind::PropertySignature: {
                const auto& ps = static_cast<const PropertySignature&>(*member);
                std::string fieldName;
                if (!synthKeyName(ps.key.get(), ps.computed, fieldName)) return false;
                out += fieldName;
                out += ps.optional ? "?" : "";
                out += ':';
                const Type* ft = ps.type != nullptr ? sema.typeOf(ps.type->nodeId) : nullptr;
                out += memberTypeKey(ft != nullptr ? *ft : Type::unknown());
                out += ',';
                break;
            }
            case NodeKind::MethodSignature: {
                const auto& ms = static_cast<const MethodSignature&>(*member);
                std::string fieldName;
                if (!synthKeyName(ms.key.get(), ms.computed, fieldName)) return false;
                out += fieldName;
                out += ms.optional ? "?" : "";
                out += ':';
                std::vector<Type> params;
                for (const ParameterPtr& param : ms.params) {
                    if (param == nullptr) continue;
                    const Type* pt = param->type != nullptr ? sema.typeOf(param->type->nodeId)
                                                             : nullptr;
                    params.push_back(pt != nullptr ? *pt : Type::unknown());
                }
                const Type* ret = ms.returnType != nullptr ? sema.typeOf(ms.returnType->nodeId)
                                                           : nullptr;
                const Type fn = Type::function(std::move(params),
                                               ret != nullptr ? *ret : Type::unknown());
                out += memberTypeKey(fn);
                out += ',';
                break;
            }
            case NodeKind::IndexSignature: {
                const auto& is = static_cast<const IndexSignature&>(*member);
                if (is.valueType == nullptr) return false;
                const Type* vt = sema.typeOf(is.valueType->nodeId);
                indexKey = memberTypeKey(vt != nullptr ? *vt : Type::unknown());
                hasIndex = true;
                break;
            }
            default:
                return false;
        }
    }
    if (hasIndex) {
        out += "*:";
        out += indexKey;
        out += ',';
    }
    out += '}';
    label = std::move(out);
    return true;
}

std::string memberTypeKey(const Type& t) {
    std::string s = typeKindName(t.kind);
    s += '|';
    s += t.name;
    s += '|';
    if (t.isLiteral()) {
        switch (t.literalBase) {
            case TypeKind::Number:
                s += std::to_string(t.numberValue);
                break;
            case TypeKind::String:
                s += t.stringValue;
                break;
            case TypeKind::Boolean:
                s += t.boolValue ? "T" : "F";
                break;
            default:
                break;
        }
    }
    for (const Type& part : t.parts) {
        s += '<';
        s += memberTypeKey(part);
        s += '>';
    }
    return s;
}

// The name a statement-declaration node carries, for the collection
// routines in this file (a local spelling of the binding layer's table).
std::string declarationNameOfForMember(const Node& node) {
    switch (node.kind) {
        case NodeKind::ClassDeclaration:
            return static_cast<const ClassDeclaration&>(node).name;
        case NodeKind::InterfaceDeclaration:
            return static_cast<const InterfaceDeclaration&>(node).name;
        case NodeKind::EnumDeclaration:
            return static_cast<const EnumDeclaration&>(node).name;
        case NodeKind::TypeAliasDeclaration:
            return static_cast<const TypeAliasDeclaration&>(node).name;
        case NodeKind::FunctionDeclaration:
            return static_cast<const FunctionDeclaration&>(node).name;
        case NodeKind::VariableDeclaration:
            return std::string();  // no single name; not discriminated here
        default:
            return std::string();
    }
}

struct UnitDecls {
    std::vector<const ClassDeclaration*> classes;
    std::vector<const InterfaceDeclaration*> interfaces;
    std::vector<const EnumDeclaration*> enums;
    std::vector<const ObjectType*> objectTypes;
    std::vector<const ObjectLiteral*> literals;
};

void gatherUnitDecls(const Program& program, UnitDecls& out) {
    forEachNode(program, [&](const Node& node) {
        switch (node.kind) {
            case NodeKind::ClassDeclaration:
                out.classes.push_back(&static_cast<const ClassDeclaration&>(node));
                break;
            case NodeKind::InterfaceDeclaration:
                out.interfaces.push_back(&static_cast<const InterfaceDeclaration&>(node));
                break;
            case NodeKind::EnumDeclaration:
                out.enums.push_back(&static_cast<const EnumDeclaration&>(node));
                break;
            case NodeKind::ObjectType:
                out.objectTypes.push_back(&static_cast<const ObjectType&>(node));
                break;
            case NodeKind::ObjectLiteral:
                out.literals.push_back(&static_cast<const ObjectLiteral&>(node));
                break;
            default:
                break;
        }
    });
}

struct PassUnit {
    MemberUnitAnalysis analysis;
    UnitDecls decls;
};

struct FamilyMember {
    CompilationUnit* unit = nullptr;
    const Node* member = nullptr;      // member declaration node
    const Node* owner = nullptr;       // owning class/interface/enum/objectType
    bool unified = false;              // same-symbol as the anchor (interface/abstract link)
    bool fromLiteral = false;          // object-literal property (same synthesized label)
};

struct MemberFamily {
    std::vector<FamilyMember> members;
    std::vector<std::string> classNames;       // nominal classes in family
    std::vector<std::string> objectLabels;     // object-shape labels ($o{...}) in family
    std::string enumName;                      // enum owners: the enum name
    std::vector<SourceSpan> bodySpans;         // bodies of family class members
};

// Everything analyzed across the graph for one member pass, kept alive for
// the whole pass. `incomplete` means a proof failed: the answer is a refusal.
struct MemberPass {
    std::vector<PassUnit> units;
    MemberFamily family;
    std::unordered_map<const InterfaceDeclaration*, std::string> interfaceLabels;
    std::unordered_map<const ObjectType*, std::string> objectTypeLabels;
    std::unordered_map<const ObjectLiteral*, std::string> literalLabels;
    bool incomplete = false;
};

const std::string* interfaceLabelOf(MemberPass& pass, std::size_t viewIndex,
                                   const InterfaceDeclaration* decl) {
    auto it = pass.interfaceLabels.find(decl);
    if (it != pass.interfaceLabels.end()) return &it->second;
    std::string label;
    if (!synthSignatureMembersLabel(decl->members, *pass.units[viewIndex].analysis.sema, label)) {
        pass.incomplete = true;
        return nullptr;
    }
    const auto placed = pass.interfaceLabels.emplace(decl, std::move(label));
    return &placed.first->second;
}

const std::string* objectTypeLabelOf(MemberPass& pass, std::size_t viewIndex,
                                     const ObjectType* decl) {
    auto it = pass.objectTypeLabels.find(decl);
    if (it != pass.objectTypeLabels.end()) return &it->second;
    std::string label;
    if (!synthSignatureMembersLabel(decl->members, *pass.units[viewIndex].analysis.sema, label)) {
        pass.incomplete = true;
        return nullptr;
    }
    const auto placed = pass.objectTypeLabels.emplace(decl, std::move(label));
    return &placed.first->second;
}

}  // namespace

// Forward declaration: the definition lives further down the translation unit.
bool literalLabelOf(MemberPass& pass, std::size_t viewIndex,
                    const ObjectLiteral* literal);

bool unitHasDeclaredName(const Program& program, const std::string& name) {
    bool found = false;
    forEachNode(program, [&](const Node& node) {
        if (found) return;
        const std::string candidate = declarationNameOfForMember(node);
        if (!candidate.empty() && candidate == name) found = true;
    });
    return found;
}

// Whether `unit` may bind a cross-file declaration from `ownerUnit`: either
// it IS the owner itself, or it imports the name straight from it (named or
// default imports both bind; namespace imports carry no same-name binding).

struct UnitKeySpans {
    std::vector<SourceSpan> spans;
};

void collectKeySpans(const Program& program, UnitKeySpans& out) {
    forEachNode(program, [&](const Node& node) {
        const Expr* key = nullptr;
        bool computed = false;
        switch (node.kind) {
            case NodeKind::PropertyDeclaration:
                key = static_cast<const PropertyDeclaration&>(node).key.get();
                computed = static_cast<const PropertyDeclaration&>(node).computed;
                break;
            case NodeKind::MethodDeclaration:
                key = static_cast<const MethodDeclaration&>(node).key.get();
                computed = static_cast<const MethodDeclaration&>(node).computed;
                break;
            case NodeKind::AccessorDeclaration:
                key = static_cast<const AccessorDeclaration&>(node).key.get();
                computed = static_cast<const AccessorDeclaration&>(node).computed;
                break;
            case NodeKind::PropertySignature:
                key = static_cast<const PropertySignature&>(node).key.get();
                computed = static_cast<const PropertySignature&>(node).computed;
                break;
            case NodeKind::MethodSignature:
                key = static_cast<const MethodSignature&>(node).key.get();
                computed = static_cast<const MethodSignature&>(node).computed;
                break;
            case NodeKind::ObjectProperty: {
                const auto& member = static_cast<const ObjectProperty&>(node);
                key = member.key.get();
                computed = member.computed;
                if (member.shorthand) return;  // binds the local name itself
                break;
            }
            default:
                return;
        }
        if (computed || key == nullptr) return;
        if (key->kind != NodeKind::Identifier && key->kind != NodeKind::StringLiteral) {
            return;
        }
        out.spans.push_back(key->span);
    });
}

bool isKeySpan(const UnitKeySpans& spans, const SourceSpan& span) {
    for (const SourceSpan& key : spans.spans) {
        if (memberSpanEquals(key, span)) return true;
    }
    return false;
}

void collectPatternSpans(const Program& program, std::vector<SourceSpan>& spans) {
    forEachNode(program, [&](const Node& node) {
        const Node* pattern = nullptr;
        if (node.kind == NodeKind::ObjectPattern || node.kind == NodeKind::ArrayPattern ||
            node.kind == NodeKind::ObjectPatternProperty || node.kind == NodeKind::RestElement) {
            pattern = &node;
        } else if (node.kind == NodeKind::VariableDeclarator) {
            const auto& declarator = static_cast<const VariableDeclarator&>(node);
            if (declarator.id != nullptr && declarator.id->kind != NodeKind::Identifier) {
                pattern = declarator.id.get();
            }
        } else if (node.kind == NodeKind::Parameter) {
            const auto& parameter = static_cast<const Parameter&>(node);
            if (parameter.name != nullptr && parameter.name->kind != NodeKind::Identifier) {
                pattern = parameter.name.get();
            }
        }
        if (pattern != nullptr) spans.push_back(pattern->span);
    });
}

bool memberSpanContainsSpan(const SourceSpan& outer, const SourceSpan& inner) {
    return memberSpanCovers(outer, inner);
}

bool synthKeyName(const Expr* key, bool computed, std::string& name) {
    if (computed || key == nullptr) return false;
    if (key->kind == NodeKind::Identifier) {
        name = static_cast<const Identifier&>(*key).name;
    } else if (key->kind == NodeKind::StringLiteral) {
        name = static_cast<const StringLiteral&>(*key).value;
    } else if (key->kind == NodeKind::NumberLiteral) {
        name = static_cast<const NumberLiteral&>(*key).raw;
    } else {
        return false;
    }
    return !name.empty();
}

std::vector<const Node*> classMemberNodesOf(const ClassDeclaration& decl) {
    std::vector<const Node*> out;
    for (const ClassMemberPtr& member : decl.members) out.push_back(member.get());
    return out;
}

std::vector<const Node*> interfaceMemberNodesOf(const InterfaceDeclaration& decl) {
    std::vector<const Node*> out;
    for (const SignatureMemberPtr& member : decl.members) out.push_back(member.get());
    return out;
}

std::vector<const Node*> objectTypeMemberNodesOf(const ObjectType& decl) {
    std::vector<const Node*> out;
    for (const SignatureMemberPtr& member : decl.members) out.push_back(member.get());
    return out;
}

std::vector<const Node*> enumMemberNodesOf(const EnumDeclaration& decl) {
    std::vector<const Node*> out;
    for (const EnumMemberPtr& member : decl.members) out.push_back(member.get());
    return out;
}

std::vector<const Node*> literalMemberNodesOf(const ObjectLiteral& decl) {
    std::vector<const Node*> out;
    for (const ObjectPropertyPtr& member : decl.properties) out.push_back(member.get());
    return out;
}

// Plain (identifier/string-key) members named `name`; number and computed
// keys never participate in a provable member rename.
void collectMembersNamed(const std::vector<const Node*>& members, const std::string& name,
                         std::vector<const Node*>& out) {
    out.clear();
    for (const Node* member : members) {
        if (member == nullptr) continue;
        const Expr* key = nullptr;
        bool computed = true;
        if (!memberKeyOf(*member, key, computed)) continue;
        if (computed || key == nullptr) continue;
        std::string spelling;
        if (!synthKeyName(key, false, spelling)) continue;
        if (spelling == name) out.push_back(member);
    }
}

bool classMemberIsAbstractNamed(const ClassDeclaration& decl, const std::string& name) {
    std::vector<const Node*> hits;
    collectMembersNamed(classMemberNodesOf(decl), name, hits);
    for (const Node* member : hits) {
        if (memberIsAbstract(*member)) return true;
    }
    return false;
}

bool interfaceHasNamedMember(const InterfaceDeclaration& decl, const std::string& name) {
    std::vector<const Node*> hits;
    collectMembersNamed(interfaceMemberNodesOf(decl), name, hits);
    return !hits.empty();
}

std::string namedTypeName(const TypeNode* type) {
    if (type == nullptr) return std::string();
    if (type->kind == NodeKind::NamedType) {
        return static_cast<const NamedType&>(*type).name;
    }
    if (type->kind == NodeKind::ParenthesizedType) {
        const auto& paren = static_cast<const ParenthesizedType&>(*type);
        return paren.inner != nullptr ? namedTypeName(paren.inner.get()) : std::string();
    }
    return std::string();
}

// Add the class's own same-name members to the family (with the `unified`
// rename/references verdict attached to every entry).
void FamAdd(MemberFamily& fam, const PassUnit& unit, const Node* member,
            const Node* owner, bool unified, bool fromLiteral);

void addClassMembers(MemberPass& pass, const ClassDeclaration* owner,
                     std::size_t ownerUnitIndex, const std::string& name,
                     bool unified, MemberFamily& fam) {
    if (owner == nullptr) return;
    std::vector<const Node*> members;
    collectMembersNamed(classMemberNodesOf(*owner), name, members);
    for (const Node* member : members) {
        FamAdd(fam, pass.units[ownerUnitIndex], member, owner, unified, /*fromLiteral=*/false);
        fam.bodySpans.push_back(member->span);
    }
    if (std::find(fam.classNames.begin(), fam.classNames.end(), owner->name) ==
        fam.classNames.end()) {
        fam.classNames.push_back(owner->name);
    }
}

void addInterfaceMembers(MemberPass& pass, const InterfaceDeclaration* owner,
                         std::size_t ownerUnitIndex, const std::string& name,
                         bool unified, MemberFamily& fam) {
    if (owner == nullptr) return;
    std::vector<const Node*> members;
    collectMembersNamed(interfaceMemberNodesOf(*owner), name, members);
    for (const Node* member : members) {
        FamAdd(fam, pass.units[ownerUnitIndex], member, owner, unified, /*fromLiteral=*/false);
    }
}

void addObjectTypeMembers(MemberPass& pass, const ObjectType* owner,
                          std::size_t ownerUnitIndex, const std::string& name,
                          bool unified, MemberFamily& fam) {
    if (owner == nullptr) return;
    std::vector<const Node*> members;
    collectMembersNamed(objectTypeMemberNodesOf(*owner), name, members);
    for (const Node* member : members) {
        FamAdd(fam, pass.units[ownerUnitIndex], member, owner, unified, /*fromLiteral=*/false);
    }
}

void addLiteralMembers(MemberPass& pass, const ObjectLiteral* owner,
                       std::size_t ownerUnitIndex, const std::string& name,
                       MemberFamily& fam) {
    if (owner == nullptr) return;
    std::vector<const Node*> members;
    collectMembersNamed(literalMemberNodesOf(*owner), name, members);
    for (const Node* member : members) {
        FamAdd(fam, pass.units[ownerUnitIndex], member, owner, /*unified=*/true,
               /*fromLiteral=*/true);
    }
}

void addEnumMembers(MemberPass& pass, const EnumDeclaration* owner,
                    std::size_t ownerUnitIndex, const std::string& name,
                    MemberFamily& fam) {
    if (owner == nullptr) return;
    std::vector<const Node*> members;
    collectMembersNamed(enumMemberNodesOf(*owner), name, members);
    for (const Node* member : members) {
        FamAdd(fam, pass.units[ownerUnitIndex], member, owner, /*unified=*/true,
               /*fromLiteral=*/false);
    }
}

void FamAdd(MemberFamily& fam, const PassUnit& unit, const Node* member,
            const Node* owner, bool unified, bool fromLiteral) {
    for (FamilyMember& existing : fam.members) {
        if (existing.member == member && existing.unit == unit.analysis.unit) {
            if (unified) existing.unified = true;
            return;
        }
    }
    FamilyMember entry;
    entry.unit = unit.analysis.unit;
    entry.member = member;
    entry.owner = owner;
    entry.unified = unified;
    entry.fromLiteral = fromLiteral;
    fam.members.push_back(entry);
}

std::size_t unitViewIndex(const MemberPass& pass, const CompilationUnit* unit) {
    for (std::size_t i = 0; i < pass.units.size(); ++i) {
        if (pass.units[i].analysis.unit == unit) return i;
    }
    return pass.units.size();
}

std::size_t entryViewIndex(const MemberPass& pass) {
    for (std::size_t i = 0; i < pass.units.size(); ++i) {
        if (pass.units[i].analysis.unit != nullptr && pass.units[i].analysis.unit->isEntry) {
            return i;
        }
    }
    return pass.units.size();
}

// Locate the declaration a name resolves to from `viewIndex`'s unit: the
// unit's own declaration wins (shadows), else a direct local import edge bound
// to the unit that declares it, else the graph-unique declaration. Ambiguity
// anywhere refuses.
enum class DeclLocateKind { Class, Interface, Enum };

const Node* unitDeclByName(const PassUnit& unit, DeclLocateKind kind,
                           const std::string& name) {
    if (kind == DeclLocateKind::Class) {
        const ClassDeclaration* found = nullptr;
        for (const ClassDeclaration* d : unit.decls.classes) {
            if (d->name != name) continue;
            if (found != nullptr) return nullptr;  // ambiguous in one unit
            found = d;
        }
        return found;
    } else if (kind == DeclLocateKind::Interface) {
        const InterfaceDeclaration* found = nullptr;
        for (const InterfaceDeclaration* d : unit.decls.interfaces) {
            if (d->name != name) continue;
            if (found != nullptr) return nullptr;
            found = d;
        }
        return found;
    }
    const EnumDeclaration* found = nullptr;
    for (const EnumDeclaration* d : unit.decls.enums) {
        if (d->name != name) continue;
        if (found != nullptr) return nullptr;
        found = d;
    }
    return found;
}

bool locateNamedDecl(const ModuleGraph& graph, const MemberPass& pass,
                     std::size_t viewIndex, const std::string& name,
                     DeclLocateKind kind, const Node*& declOut, std::size_t& indexOut) {
    const PassUnit& view = pass.units[viewIndex];
    if (const Node* local = unitDeclByName(view, kind, name)) {
        declOut = local;
        indexOut = viewIndex;
        return true;
    }
    const Program* program = view.analysis.program;
    const CompilationUnit* unit = view.analysis.unit;
    if (program == nullptr || unit == nullptr) return false;
    for (const StmtPtr& statement : program->body) {
        if (statement == nullptr || statement->kind != NodeKind::ImportDeclaration) {
            continue;
        }
        const auto& import = static_cast<const ImportDeclaration&>(*statement);
        bool imported = false;
        for (const ImportSpecifierPtr& specifier : import.specifiers) {
            if (specifier != nullptr && specifier->imported == name) imported = true;
        }
        if (import.defaultBinding == name) imported = true;
        if (!imported) continue;
        const ModuleDependency* dependency = nullptr;
        for (const ModuleDependency& candidate : graph.dependencies()) {
            if (candidate.importer == unit->id && candidate.specifier == import.source &&
                candidate.resolved && candidate.resolution == ModuleResolution::Local) {
                dependency = &candidate;
                break;
            }
        }
        if (dependency == nullptr) continue;
        const std::size_t ownerIndex = unitViewIndex(pass, graph.unit(dependency->imported));
        if (ownerIndex >= pass.units.size()) return false;
        if (const Node* decl = unitDeclByName(pass.units[ownerIndex], kind, name)) {
            declOut = decl;
            indexOut = ownerIndex;
            return true;
        }
        return false;  // an import bound here cannot be the owner we rename
    }
    const Node* unique = nullptr;
    std::size_t count = 0;
    std::size_t idx = pass.units.size();
    for (std::size_t i = 0; i < pass.units.size(); ++i) {
        if (const Node* decl = unitDeclByName(pass.units[i], kind, name)) {
            ++count;
            unique = decl;
            idx = i;
        }
    }
    if (count == 1) {
        declOut = unique;
        indexOut = idx;
        return true;
    }
    return false;
}

void synthAllLabels(MemberPass& pass) {    for (std::size_t i = 0; i < pass.units.size(); ++i) {
        const UnitDecls& decls = pass.units[i].decls;
        for (const InterfaceDeclaration* decl : decls.interfaces) {
            (void)interfaceLabelOf(pass, i, decl);
        }
        for (const ObjectType* decl : decls.objectTypes) {
            (void)objectTypeLabelOf(pass, i, decl);
        }
        for (const ObjectLiteral* decl : decls.literals) {
            if (!literalLabelOf(pass, i, decl)) {
                pass.incomplete = true;
            }
        }
    }
}

// Spread the recipe of one object-labelled owner over every declaration in
// the graph that produces the same synthesized label (interfaces, inline
// object types, object literals): each such declaration's same-name member
// keys are occurrences with full member-symbol status.
void addSameLabelMembers(MemberPass& pass, const std::string& name,
                         MemberFamily& fam) {
    for (std::size_t i = 0; i < pass.units.size(); ++i) {
        for (const InterfaceDeclaration* decl : pass.units[i].decls.interfaces) {
            auto found = pass.interfaceLabels.find(decl);
            if (found == pass.interfaceLabels.end()) continue;
            for (const std::string& label : fam.objectLabels) {
                if (found->second != label) continue;
                addInterfaceMembers(pass, decl, i, name, /*unified=*/true, fam);
            }
        }
        for (const ObjectType* decl : pass.units[i].decls.objectTypes) {
            auto found = pass.objectTypeLabels.find(decl);
            if (found == pass.objectTypeLabels.end()) continue;
            for (const std::string& label : fam.objectLabels) {
                if (found->second != label) continue;
                addObjectTypeMembers(pass, decl, i, name, /*unified=*/true, fam);
            }
        }
        for (const ObjectLiteral* decl : pass.units[i].decls.literals) {
            auto found = pass.literalLabels.find(decl);
            if (found == pass.literalLabels.end()) continue;
            for (const std::string& label : fam.objectLabels) {
                if (found->second != label) continue;
                addLiteralMembers(pass, decl, i, name, fam);
            }
        }
    }
}


bool literalLabelOf(MemberPass& pass, std::size_t viewIndex,
                    const ObjectLiteral* literal) {
    const Type* type = pass.units[viewIndex].analysis.sema != nullptr
                           ? pass.units[viewIndex].analysis.sema->typeOf(literal->nodeId)
                           : nullptr;
    if (type == nullptr || type->kind != TypeKind::Object ||
        type->name.compare(0, 3, kMemberOwnerLiteral) != 0) {
        pass.incomplete = true;
        return false;
    }
    pass.literalLabels.emplace(literal, type->name);
    return true;
}

void growMemberFamily(const ModuleGraph& graph, MemberPass& pass,
                      const std::string& name, MemberOccurrenceMode mode) {
    MemberFamily& fam = pass.family;
    std::unordered_set<const Node*> classJoined;
    std::unordered_set<const Node*> interfaceJoined;
    std::unordered_set<const Node*> receiverJoined;
    auto pushObjectLabel = [&](const std::string& label) {
        if (std::find(fam.objectLabels.begin(), fam.objectLabels.end(), label) ==
            fam.objectLabels.end()) {
            fam.objectLabels.push_back(label);
        }
    };
    for (const FamilyMember& fm : fam.members) {
        if (fm.owner == nullptr) continue;
        if (fm.owner->kind == NodeKind::ClassDeclaration) {
            classJoined.insert(fm.owner);
            receiverJoined.insert(fm.owner);
        } else if (fm.owner->kind == NodeKind::InterfaceDeclaration) {
            interfaceJoined.insert(fm.owner);
        }
    }
    auto lookupClass = [&](std::size_t vi, const std::string& nm,
                          std::size_t& idxOut) -> const ClassDeclaration* {
        const Node* decl = nullptr;
        if (!locateNamedDecl(graph, pass, vi, nm, DeclLocateKind::Class, decl, idxOut)) {
            return nullptr;
        }
        return static_cast<const ClassDeclaration*>(decl);
    };
    auto lookupInterface = [&](std::size_t vi, const std::string& nm,
                               std::size_t& idxOut) -> const InterfaceDeclaration* {
        const Node* decl = nullptr;
        if (!locateNamedDecl(graph, pass, vi, nm, DeclLocateKind::Interface, decl, idxOut)) {
            return nullptr;
        }
        return static_cast<const InterfaceDeclaration*>(decl);
    };
    bool grew = true;
    std::size_t guard = 0;
    while (grew && guard++ < 64) {
        grew = false;
        for (std::size_t i = 0; i < pass.units.size(); ++i) {
            const PassUnit& view = pass.units[i];
            const UnitDecls& decls = view.decls;

            // (a) A class implements a family interface with a same-name
            // member: the implementation shares the symbol.
            for (const ClassDeclaration* D : decls.classes) {
                if (classJoined.count(D) != 0) continue;
                bool joined = false;
                for (const TypePtr& type : D->implementsTypes) {
                    const std::string base = namedTypeName(type.get());
                    if (base.empty()) continue;
                    std::size_t idxOut = i;
                    const InterfaceDeclaration* Iface = lookupInterface(i, base, idxOut);
                    if (Iface == nullptr) continue;
                    if (interfaceJoined.count(Iface) == 0) continue;
                    if (!interfaceHasNamedMember(*Iface, name)) continue;
                    addClassMembers(pass, D, i, name, /*unified*/true, fam);
                    classJoined.insert(D);
                    if (const std::string* label = interfaceLabelOf(pass, idxOut, Iface)) {
                        pushObjectLabel(*label);
                    }
                    grew = true;
                    joined = true;
                    break;
                }
                if (joined) continue;
                // (b) A class extends a family class with a same-name member:
                // shadow joins in a rename, refusal keeps symbols distinct.
                if (D->extendsType == nullptr) continue;
                const std::string base = namedTypeName(D->extendsType.get());
                if (base.empty()) continue;
                std::size_t idxOut = i;
                const ClassDeclaration* B = lookupClass(i, base, idxOut);
                if (B == nullptr) continue;
                if (classJoined.count(B) == 0) continue;
                if (classJoined.count(D) != 0) continue;
                const bool abstractBase = classMemberIsAbstractNamed(*B, name);
                const bool unified = abstractBase || mode == MemberOccurrenceMode::Rename;
                addClassMembers(pass, D, i, name, unified, fam);
                classJoined.insert(D);
                grew = true;
            }
            // (c) An interface extends a family interface (transitively) with
            // a same-name member.
            for (const InterfaceDeclaration* I : decls.interfaces) {
                if (interfaceJoined.count(I) != 0) continue;
                bool joined = false;
                for (const TypePtr& type : I->extendsTypes) {
                    const std::string base = namedTypeName(type.get());
                    if (base.empty()) continue;
                    std::size_t idxOut = i;
                    const InterfaceDeclaration* J = lookupInterface(i, base, idxOut);
                    if (J == nullptr) continue;
                    if (interfaceJoined.count(J) == 0) continue;
                    if (!interfaceHasNamedMember(*J, name) &&
                        !interfaceHasNamedMember(*I, name)) {
                        continue;
                    }
                    addInterfaceMembers(pass, I, i, name, /*unified*/true, fam);
                    interfaceJoined.insert(I);
                    if (const std::string* label = interfaceLabelOf(pass, i, I)) {
                        pushObjectLabel(*label);
                    }
                    grew = true;
                    joined = true;
                    break;
                }
                if (joined) continue;
                // Interfaces sharing the synthesized shape label join: the
                // label is the identity, so the same name is the same field.
                const std::string* label = interfaceLabelOf(pass, i, I);
                if (label == nullptr) continue;
                for (const std::string& wanted : fam.objectLabels) {
                    if (*label != wanted) continue;
                    addInterfaceMembers(pass, I, i, name, /*unified*/true, fam);
                    interfaceJoined.insert(I);
                    pushObjectLabel(*label);
                    grew = true;
                    break;
                }
            }
        }
        // (d) Ancestor walk for joined classes: a member's extends chain
        // links keep one symbol across the override boundary for rename, and
        // for references through abstract bridges only (concrete shadows
        // are distinct symbols, so references keep one declaration each).
        for (std::size_t i = 0; i < pass.units.size(); ++i) {
            for (const ClassDeclaration* D : pass.units[i].decls.classes) {
                if (classJoined.count(D) == 0) continue;
                const ClassDeclaration* cursor = D;
                for (int hops = 0; hops < 32; ++hops) {
                    if (cursor->extendsType == nullptr) break;
                    const std::string base = namedTypeName(cursor->extendsType.get());
                    if (base.empty()) break;
                    const Node* decl = nullptr;
                    std::size_t idxOut = i;
                    if (!locateNamedDecl(graph, pass, i, base, DeclLocateKind::Class,
                                         decl, idxOut)) {
                        break;
                    }
                    const ClassDeclaration* B = static_cast<const ClassDeclaration*>(decl);
                    const bool abstractBase = classMemberIsAbstractNamed(*B, name);
                    if (mode == MemberOccurrenceMode::References && !abstractBase) {
                        break;  // concrete shadow link: symbols stay distinct
                    }
                    if (classJoined.count(B) == 0) {
                        addClassMembers(pass, B, idxOut, name, /*unified*/true, fam);
                        classJoined.insert(B);
                        grew = true;
                    }
                    cursor = B;
                }
            }
        }
        // (e) Descendant receivers join the receiver class set: any class
        // whose extends chain or implements list is in the family means its
        // instances bind the member too; uses on those receivers count, but
        // no member declarations are added for them (the shadow decl is a
        // distinct symbol for references and, for rename, caught by rule (b)
        // which unifies every same-name decl in the chain).
        for (std::size_t i = 0; i < pass.units.size(); ++i) {
            for (const ClassDeclaration* D : pass.units[i].decls.classes) {
                if (receiverJoined.count(D) != 0) continue;
                bool joined = false;
                for (const TypePtr& type : D->implementsTypes) {
                    const std::string base = namedTypeName(type.get());
                    if (base.empty()) continue;
                    const Node* decl = nullptr;
                    std::size_t idxOut = i;
                    if (!locateNamedDecl(graph, pass, i, base, DeclLocateKind::Interface,
                                         decl, idxOut)) {
                        continue;
                    }
                    const InterfaceDeclaration* Iface = static_cast<const InterfaceDeclaration*>(decl);
                    if (interfaceJoined.count(Iface) != 0) {
                        joined = true;
                        break;
                    }
                }
                if (!joined && D->extendsType != nullptr) {
                    const std::string base = namedTypeName(D->extendsType.get());
                    if (!base.empty()) {
                        const Node* decl = nullptr;
                        std::size_t idxOut = i;
                        if (locateNamedDecl(graph, pass, i, base, DeclLocateKind::Class,
                                            decl, idxOut)) {
                            const ClassDeclaration* B = static_cast<const ClassDeclaration*>(decl);
                            if (receiverJoined.count(B) != 0 || classJoined.count(B) != 0) {
                                joined = true;
                            }
                        }
                    }
                }
                if (!joined) continue;
                receiverJoined.insert(D);
                if (std::find(fam.classNames.begin(), fam.classNames.end(), D->name) ==
                    fam.classNames.end()) {
                    fam.classNames.push_back(D->name);
                }
                grew = true;
            }
        }
    }
    for (std::size_t i = 0; i < pass.units.size(); ++i) {
        // New passes may have found more interfaces with matching labels;
        // re-add literal keys in case the recipe grew in the fixpoint.
        for (const ObjectLiteral* decl : pass.units[i].decls.literals) {
            auto found = pass.literalLabels.find(decl);
            if (found == pass.literalLabels.end()) continue;
            for (const std::string& wanted : fam.objectLabels) {
                if (found->second == wanted) addLiteralMembers(pass, decl, i, name, fam);
            }
        }
        for (const ObjectType* decl : pass.units[i].decls.objectTypes) {
            const std::string* label = objectTypeLabelOf(pass, i, decl);
            if (label == nullptr) continue;
            for (const std::string& wanted : fam.objectLabels) {
                if (*label == wanted) addObjectTypeMembers(pass, decl, i, name, true, fam);
            }
        }
    }
    (void)graph;
}



// ---------------------------------------------------------------------------
// Seed the family from the cursor anchor
// ---------------------------------------------------------------------------

// Owner resolution for use-position (`receiver.member` / bare callee)
// anchors: the class/enum the receiver type resolves to, entry-local first,
// else an import edge bound to the name, else the graph-unique declaration.
const ClassDeclaration* seedClass(const ModuleGraph& graph, const MemberPass& pass,
                                  std::size_t viewIndex, const std::string& name,
                                  const ClassDeclaration*& ownerOut, std::size_t& indexOut) {
    const Node* decl = nullptr;
    if (!locateNamedDecl(graph, pass, viewIndex, name, DeclLocateKind::Class, decl, indexOut)) {
        return nullptr;
    }
    ownerOut = static_cast<const ClassDeclaration*>(decl);
    return ownerOut;
}

const EnumDeclaration* seedEnum(const ModuleGraph& graph, const MemberPass& pass,
                                std::size_t viewIndex, const std::string& name,
                                const EnumDeclaration*& ownerOut, std::size_t& indexOut) {
    const Node* decl = nullptr;
    if (!locateNamedDecl(graph, pass, viewIndex, name, DeclLocateKind::Enum, decl, indexOut)) {
        return nullptr;
    }
    ownerOut = static_cast<const EnumDeclaration*>(decl);
    return ownerOut;
}

bool seedMemberFamily(const ModuleGraph& graph, MemberPass& pass,
                      const std::string& name, const MemberRetarget& target) {
    MemberFamily& fam = pass.family;
    const std::size_t entry = entryViewIndex(pass);
    if (entry >= pass.units.size()) return false;
    std::size_t anchorUnit = unitViewIndex(pass, target.ownerUnit);
    if (anchorUnit >= pass.units.size()) anchorUnit = entry;
    const PassUnit& anchorView = pass.units[anchorUnit];

    switch (target.ownerKind) {
        case NodeKind::ClassDeclaration: {
            const ClassDeclaration* owner = nullptr;
            std::size_t ownerIndex = anchorUnit;
            if (target.ownerNodeId != 0) {
                for (const ClassDeclaration* d : anchorView.decls.classes) {
                    if (d->nodeId == target.ownerNodeId) { owner = d; break; }
                }
            } else {
                owner = seedClass(graph, pass, anchorUnit, target.ownerLabel, owner, ownerIndex);
                anchorUnit = ownerIndex;
            }
            if (owner == nullptr) return false;
            addClassMembers(pass, owner, anchorUnit, name, /*unified*/true, fam);
            return true;
        }
        case NodeKind::InterfaceDeclaration: {
            const InterfaceDeclaration* owner = nullptr;
            if (target.ownerNodeId != 0) {
                for (const InterfaceDeclaration* d : anchorView.decls.interfaces) {
                    if (d->nodeId == target.ownerNodeId) { owner = d; break; }
                }
                if (owner == nullptr) return false;
                const std::string* label = interfaceLabelOf(pass, anchorUnit, owner);
                if (label == nullptr) return false;
                fam.objectLabels.push_back(*label);
                addInterfaceMembers(pass, owner, anchorUnit, name, /*unified*/true, fam);
                return true;
            }
            if (target.ownerLabel.empty()) return false;
            fam.objectLabels.push_back(target.ownerLabel);
            return true;
        }
        case NodeKind::ObjectType: {
            if (target.ownerNodeId != 0) {
                const ObjectType* owner = nullptr;
                for (const ObjectType* d : anchorView.decls.objectTypes) {
                    if (d->nodeId == target.ownerNodeId) { owner = d; break; }
                }
                if (owner == nullptr) return false;
                const std::string* label = objectTypeLabelOf(pass, anchorUnit, owner);
                if (label == nullptr) return false;
                fam.objectLabels.push_back(*label);
                addObjectTypeMembers(pass, owner, anchorUnit, name, /*unified*/true, fam);
                return true;
            }
            fam.objectLabels.push_back(target.ownerLabel);
            return true;
        }
        case NodeKind::ObjectLiteral: {
            if (target.ownerNodeId != 0) {
                const ObjectLiteral* owner = nullptr;
                for (const ObjectLiteral* d : anchorView.decls.literals) {
                    if (d->nodeId == target.ownerNodeId) { owner = d; break; }
                }
                if (owner == nullptr) return false;
                if (!literalLabelOf(pass, anchorUnit, owner)) return false;
                const auto found = pass.literalLabels.find(owner);
                if (found == pass.literalLabels.end()) return false;
                fam.objectLabels.push_back(found->second);
                addLiteralMembers(pass, owner, anchorUnit, name, fam);
                return true;
            }
            if (target.ownerLabel.empty()) return false;
            fam.objectLabels.push_back(target.ownerLabel);
            return true;
        }
        case NodeKind::EnumDeclaration: {
            const EnumDeclaration* owner = nullptr;
            std::size_t ownerIndex = anchorUnit;
            if (target.ownerNodeId != 0) {
                for (const EnumDeclaration* d : anchorView.decls.enums) {
                    if (d->nodeId == target.ownerNodeId) { owner = d; break; }
                }
            } else {
                owner = seedEnum(graph, pass, anchorUnit, target.ownerLabel, owner, ownerIndex);
                anchorUnit = ownerIndex;
            }
            if (owner == nullptr) return false;
            fam.enumName = owner->name;
            addEnumMembers(pass, owner, anchorUnit, name, fam);
            return true;
        }
        default:
            return false;
    }
}

// Member-position anchor resolution / occurrence collection
// ==========================================================================

lsp::SourceTarget memberTargetOf(const SourceFile& file, std::uint32_t begin,
                                 std::uint32_t end) {
    const SourcePos b = file.positionAt(begin);
    const SourcePos e = file.positionAt(end);
    lsp::SourceTarget target;
    target.path = file.path;
    target.line = b.line;
    target.column = b.column;
    target.endLine = e.line;
    target.endColumn = e.column;
    return target;
}

bool memberRecipeOf(const Type& t, std::vector<std::string>& classes,
                    std::vector<std::string>& enums,
                    std::vector<std::string>& labels) {
    switch (t.kind) {
        case TypeKind::Class:
            classes.push_back(t.name);
            return true;
        case TypeKind::Enum:
            enums.push_back(t.name);
            return true;
        case TypeKind::Object:
            if (t.name.compare(0, 3, kMemberOwnerLiteral) == 0) labels.push_back(t.name);
            return true;
        case TypeKind::Nullable:
        case TypeKind::Union:
            for (const Type& part : t.parts) {
                (void)memberRecipeOf(part, classes, enums, labels);
            }
            return true;
        default:
            return true;
    }
}

bool memberRecipeMatches(const MemberFamily& fam, const Type& t) {
    std::vector<std::string> classes;
    std::vector<std::string> enums;
    std::vector<std::string> labels;
    (void)memberRecipeOf(t, classes, enums, labels);
    for (const std::string& candidate : classes) {
        for (const std::string& member : fam.classNames) {
            if (member == candidate) return true;
        }
    }
    for (const std::string& candidate : enums) {
        if (!fam.enumName.empty() && fam.enumName == candidate) return true;
    }
    for (const std::string& candidate : labels) {
        for (const std::string& label : fam.objectLabels) {
            if (label == candidate) return true;
        }
    }
    return false;
}

void emitFamilyDeclMembers(const MemberPass& pass, const std::string& name,
                           MemberOccurrenceMode mode,
                           std::vector<MemberOccurrence>& out) {
    for (const FamilyMember& fm : pass.family.members) {
        if (mode == MemberOccurrenceMode::References && !fm.unified) continue;
        if (fm.member == nullptr) continue;
        SourceSpan span;
        if (fm.member->kind == NodeKind::EnumMember) {
            const auto& member = static_cast<const EnumMember&>(*fm.member);
            if (!memberCarveName(member.span, name, &span)) continue;
        } else {
            const Expr* key = nullptr;
            bool computed = true;
            if (!memberKeyOf(*fm.member, key, computed)) continue;
            if (computed || key == nullptr) continue;
            if (key->kind == NodeKind::StringLiteral) {
                if (!memberCarveName(key->span, name, &span)) continue;
            } else if (key->kind == NodeKind::Identifier) {
                span = key->span;
            } else {
                continue;
            }
        }
        addMemberOccurrence(out, span, /*isDeclaration=*/true);
    }
}

bool insideSpans(const std::vector<SourceSpan>& spans, const SourceSpan& inner) {
    for (const SourceSpan& outer : spans) {
        if (memberSpanCovers(outer, inner)) return true;
    }
    return false;
}

void emitUnitOccurrences(const PassUnit& view, const MemberPass& pass,
                         const std::string& name,
                         std::vector<MemberOccurrence>& out) {
    const Program* program = view.analysis.program;
    if (program == nullptr) return;
    const SemaResult* sema = view.analysis.sema;
    const SourceFile* file = view.analysis.file;
    // Family class bodies in this unit: the bare-callee rule reads these.
    std::vector<SourceSpan> bodySpans;
    for (const SourceSpan& span : pass.family.bodySpans) {
        if (span.file == file) bodySpans.push_back(span);
    }
    UnitKeySpans keySpans;
    collectKeySpans(*program, keySpans);
    std::vector<SourceSpan> patternSpans;
    collectPatternSpans(*program, patternSpans);
    std::unordered_map<const ObjectProperty*, const ObjectLiteral*> ownerOf;
    for (const ObjectLiteral* literal : view.decls.literals) {
        for (const ObjectPropertyPtr& property : literal->properties) {
            ownerOf[property.get()] = literal;
        }
    }
    forEachNode(*program, [&](const Node& node) {
        if (node.kind == NodeKind::MemberExpr) {
            const auto& expr = static_cast<const MemberExpr&>(node);
            if (expr.property != name) return;
            SourceSpan propSpan;
            if (!memberExprPropertySpan(expr, &propSpan)) return;
            const Expr* receiver = expr.object.get();
            if (receiver == nullptr || sema == nullptr) return;
            const Type* receiverType = sema->typeOf(receiver->nodeId);
            if (receiverType == nullptr) return;
            if (!memberRecipeMatches(pass.family, *receiverType)) return;
            addMemberOccurrence(out, propSpan, /*isDeclaration=*/false);
            return;
        }
        if (node.kind == NodeKind::ObjectProperty) {
            const auto& property = static_cast<const ObjectProperty&>(node);
            if (property.shorthand || property.computed) return;
            const Expr* key = property.key.get();
            if (key == nullptr) return;
            if (key->kind != NodeKind::Identifier && key->kind != NodeKind::StringLiteral) {
                return;
            }
            std::string spelling;
            if (!synthKeyName(key, false, spelling)) return;
            if (spelling != name) return;
            const auto found = ownerOf.find(&property);
            if (found == ownerOf.end()) return;
            const auto literalFound = pass.literalLabels.find(found->second);
            if (literalFound == pass.literalLabels.end()) return;
            bool labelMatches = false;
            for (const std::string& label : pass.family.objectLabels) {
                if (label == literalFound->second) { labelMatches = true; break; }
            }
            if (!labelMatches) return;
            SourceSpan keySpan;
            if (key->kind == NodeKind::StringLiteral) {
                if (!memberCarveName(key->span, name, &keySpan)) return;
            } else {
                keySpan = key->span;
            }
            addMemberOccurrence(out, keySpan, /*isDeclaration=*/true);
            return;
        }
        if (node.kind == NodeKind::Identifier) {
            const Identifier& identifier = static_cast<const Identifier&>(node);
            if (identifier.name != name) return;
            if (bodySpans.empty()) return;
            const SourceSpan span = identifier.span;
            bool inside = false;
            for (const SourceSpan& body : bodySpans) {
                if (memberSpanCovers(body, span)) { inside = true; break; }
            }
            if (!inside) return;
            if (isKeySpan(keySpans, span)) return;
            if (insideSpans(patternSpans, span)) return;
            if (sema != nullptr) {
                const auto it = sema->resolvedBindings.find(identifier.nodeId);
                if (it != sema->resolvedBindings.end()) {
                    bool boundToFamily = false;
                    for (const FamilyMember& fm : pass.family.members) {
                        if (fm.unit != view.analysis.unit) continue;
                        if (fm.member != nullptr && fm.member->nodeId == it->second) {
                            boundToFamily = true;
                            break;
                        }
                    }
                    if (!boundToFamily) return;
                }
            }
            addMemberOccurrence(out, span, /*isDeclaration=*/false);
        }
    });
}

std::vector<lsp::SourceTarget> memberToTargets(const std::vector<MemberOccurrence>& occurrences,
                                               bool includeDeclaration) {
    std::vector<lsp::SourceTarget> targets;
    for (const MemberOccurrence& occ : occurrences) {
        if (!includeDeclaration && occ.isDeclaration) continue;
        targets.push_back(memberTargetOf(*occ.file, occ.begin, occ.end));
    }
    return targets;
}



struct KeyHit {
    NodeKind ownerKind = NodeKind::Program;
    const Node* owner = nullptr;
    const Node* member = nullptr;
    SourceSpan keySpan;
};

// A cursor landing on a declaration position's member key (a class/interface
// property/method/accessor, an enum member name, or an object-literal
// property key) yields the member declaration to re-key. Computed keys and
// number keys never participate; shorthand keys never rename.
std::optional<KeyHit> keyHitAt(const Program& program, const SourceFile& file,
                               std::uint32_t line, std::uint32_t column) {
    const std::uint32_t offset = offsetOf(file, line, column);
    struct OwnerEntry { NodeKind kind; const Node* node; std::uint32_t length; };
    std::vector<OwnerEntry> owners;
    forEachNode(program, [&](const Node& node) {
        switch (node.kind) {
            case NodeKind::ClassDeclaration:
                owners.push_back({NodeKind::ClassDeclaration, &node, node.span.length()});
                return;
            case NodeKind::InterfaceDeclaration:
                owners.push_back({NodeKind::InterfaceDeclaration, &node, node.span.length()});
                return;
            case NodeKind::EnumDeclaration:
                owners.push_back({NodeKind::EnumDeclaration, &node, node.span.length()});
                return;
            case NodeKind::ObjectType:
                owners.push_back({NodeKind::ObjectType, &node, node.span.length()});
                return;
            case NodeKind::ObjectLiteral:
                owners.push_back({NodeKind::ObjectLiteral, &node, node.span.length()});
                return;
            default:
                return;
        }
    });
    auto findOwner = [&](const SourceSpan& inner) -> const OwnerEntry* {
        const OwnerEntry* hitEntry = nullptr;
        for (const OwnerEntry& entry : owners) {
            if (!memberSpanCovers(entry.node->span, inner)) continue;
            if (hitEntry == nullptr || entry.length < hitEntry->length) hitEntry = &entry;
        }
        return hitEntry;
    };
    std::optional<KeyHit> hit;
    forEachNode(program, [&](const Node& node) {
        if (hit.has_value()) return;
        const OwnerEntry* owner = findOwner(node.span);
        if (node.kind == NodeKind::EnumMember) {
            if (owner == nullptr || owner->kind != NodeKind::EnumDeclaration) return;
            const auto& member = static_cast<const EnumMember&>(node);
            SourceSpan carved;
            if (!memberCarveName(member.span, member.name, &carved)) return;
            if (offset < carved.begin.offset || offset >= carved.end.offset) return;
            KeyHit candidate;
            candidate.ownerKind = NodeKind::EnumDeclaration;
            candidate.owner = owner->node;
            candidate.member = &node;
            candidate.keySpan = carved;
            hit = candidate;
            return;
        }
        const Expr* key = nullptr;
        bool computed = false;
        if (node.kind == NodeKind::PropertyDeclaration ||
            node.kind == NodeKind::MethodDeclaration ||
            node.kind == NodeKind::AccessorDeclaration ||
            node.kind == NodeKind::PropertySignature ||
            node.kind == NodeKind::MethodSignature) {
            (void)memberKeyOf(node, key, computed);
        } else if (node.kind == NodeKind::ObjectProperty) {
            const auto& member = static_cast<const ObjectProperty&>(node);
            if (member.shorthand) return;
            key = member.key.get();
            computed = member.computed;
        } else {
            return;
        }
        if (computed || key == nullptr) return;
        if (key->kind != NodeKind::Identifier && key->kind != NodeKind::StringLiteral) {
            return;
        }
        if (!spanContains(key->span, file, line, column)) return;
        std::string spelling;
        if (!synthKeyName(key, false, spelling)) return;
        SourceSpan span = key->span;
        if (key->kind == NodeKind::StringLiteral) {
            if (!memberCarveName(key->span, spelling, &span)) return;
        }
        if (owner == nullptr) return;
        KeyHit candidate;
        candidate.ownerKind = owner->kind;
        candidate.owner = owner->node;
        candidate.member = &node;
        candidate.keySpan = span;
        hit = candidate;
    });
    return hit;
}

const ClassDeclaration* findEnclosingClass(const Program& program,
                                           const SourceSpan& span) {
    const ClassDeclaration* hit = nullptr;
    std::uint32_t bestLength = std::numeric_limits<std::uint32_t>::max();
    forEachNode(program, [&](const Node& node) {
        if (node.kind != NodeKind::ClassDeclaration) return;
        const auto& decl = static_cast<const ClassDeclaration&>(node);
        if (!memberSpanCovers(decl.span, span)) return;
        if (hit == nullptr || decl.span.length() < bestLength) {
            hit = &decl;
            bestLength = decl.span.length();
        }
    });
    return hit;
}

std::optional<MemberRetarget> memberRetarget(AnalyzedDocument& document,
                                             const std::string& path,
                                             std::uint32_t line,
                                             std::uint32_t column) {
    (void)path;
    if (!analysisReady(document)) return std::nullopt;
    const SourceFile& file = *document.file;
    const Program& program = *document.entry->ast;
    const Node* node = innermostNodeAt(program, file, line, column);
    if (node == nullptr) return std::nullopt;

    // (A) The member side of `obj.member`. `MemberExpr.property` is a plain
    //     string, so there is no identifier node to consult.
    if (node->kind == NodeKind::MemberExpr) {
        const auto& expr = static_cast<const MemberExpr&>(*node);
        SourceSpan propSpan;
        if (!memberExprPropertySpan(expr, &propSpan)) return std::nullopt;
        if (!spanContains(propSpan, file, line, column)) return std::nullopt;
        if (expr.object == nullptr) return std::nullopt;
        const Type* receiver = document.sema.typeOf(expr.object->nodeId);
        if (receiver == nullptr) return std::nullopt;
        std::vector<std::string> classes;
        std::vector<std::string> enums;
        std::vector<std::string> labels;
        (void)memberRecipeOf(*receiver, classes, enums, labels);
        MemberRetarget target;
        target.name = expr.property;
        target.ownerUnit = document.entry;
        target.ownerNodeId = 0;
        if (!classes.empty()) {
            target.ownerKind = NodeKind::ClassDeclaration;
            target.ownerLabel = classes.front();
        } else if (!enums.empty()) {
            target.ownerKind = NodeKind::EnumDeclaration;
            target.ownerLabel = enums.front();
        } else if (!labels.empty()) {
            target.ownerKind = NodeKind::ObjectType;
            target.ownerLabel = labels.front();
        } else {
            return std::nullopt;
        }
        target.declaration = memberTargetOf(file, propSpan.begin.offset, propSpan.end.offset);
        return target;
    }

    // (B) The cursor sits on a declaration position of the member.
    const std::optional<KeyHit> hit = keyHitAt(program, file, line, column);
    if (hit.has_value()) {
        std::string spelling;
        if (hit->member->kind == NodeKind::EnumMember) {
            spelling = static_cast<const EnumMember&>(*hit->member).name;
        } else {
            const Expr* key = nullptr;
            bool computed = true;
            if (!memberKeyOf(*hit->member, key, computed)) return std::nullopt;
            if (!synthKeyName(key, computed, spelling)) return std::nullopt;
        }
        MemberRetarget target;
        target.name = spelling;
        target.ownerUnit = document.entry;
        target.ownerKind = hit->ownerKind;
        target.ownerNodeId = hit->owner->nodeId;
        target.declaration = memberTargetOf(file, hit->keySpan.begin.offset, hit->keySpan.end.offset);
        return target;
    }

    // (C) A bare identifier inside a class body that declares the member.
    if (node->kind == NodeKind::Identifier) {
        const Identifier& identifier = static_cast<const Identifier&>(*node);
        if (identifier.name.empty()) return std::nullopt;
        if (document.sema.resolvedBindings.find(identifier.nodeId) !=
            document.sema.resolvedBindings.end()) {
            return std::nullopt;  // the binding machinery owns this one
        }
        const ClassDeclaration* enclosing = findEnclosingClass(program, identifier.span);
        if (enclosing == nullptr) return std::nullopt;
        std::vector<const Node*> hits;
        collectMembersNamed(classMemberNodesOf(*enclosing), identifier.name, hits);
        if (hits.empty()) return std::nullopt;
        MemberRetarget target;
        target.name = identifier.name;
        target.ownerKind = NodeKind::ClassDeclaration;
        target.ownerLabel = enclosing->name;
        target.ownerUnit = document.entry;
        target.ownerNodeId = 0;
        target.declaration =
            memberTargetOf(file, identifier.span.begin.offset, identifier.span.end.offset);
        return target;
    }

    return std::nullopt;
}

std::vector<lsp::SourceTarget> memberOccurrences(AnalyzedDocument& document,
                                                 const std::string& path,
                                                 const MemberRetarget& target,
                                                 bool includeDeclaration,
                                                 MemberOccurrenceMode mode) {
    std::vector<lsp::SourceTarget> targets;
    if (!analysisReady(document)) return targets;
    const ModuleGraph* graph = document.graph.get();
    if (graph == nullptr) return targets;
    const std::string name = target.name;
    std::vector<MemberOccurrence> occurrences;

    MemberPass pass;
    const CompileOptions compile = memberCompileOptionsFor(document, path);
    for (CompilationUnit* unit : graph->units()) {
        if (unit == nullptr) {
            pass.incomplete = true;  // a missing unit may hold occurrences
            continue;
        }
        if (unit->isPrelude) continue;  // compiler-owned surfaces
        PassUnit view;
        view.analysis = memberAnalyzeUnit(document, unit, compile);
        if (view.analysis.program != nullptr) {
            gatherUnitDecls(*view.analysis.program, view.decls);
        } else if (unit->file != nullptr &&
                   unit->file->text.find(name) != std::string::npos) {
            pass.incomplete = true;  // prescreened but unanalyzable
            continue;
        }
        pass.units.push_back(std::move(view));
    }
    if (pass.incomplete) return {};
    synthAllLabels(pass);
    if (pass.incomplete) return {};
    if (!seedMemberFamily(*graph, pass, name, target)) return {};
    addSameLabelMembers(pass, name, pass.family);
    growMemberFamily(*graph, pass, name, mode);
    emitFamilyDeclMembers(pass, name, mode, occurrences);
    for (std::size_t i = 0; i < pass.units.size(); ++i) {
        emitUnitOccurrences(pass.units[i], pass, name, occurrences);
    }
    return memberToTargets(occurrences, includeDeclaration);
}

}  // namespace cp3::cli
