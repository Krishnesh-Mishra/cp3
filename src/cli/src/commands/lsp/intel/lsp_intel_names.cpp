// =============================================================================
// src/cli/src/commands/lsp_intel_names.cpp - specifier / name completion.
//
// OWNERSHIP: cli module (src/cli/**). Completes the importSpecifierCandidates,
// legalKeywords, namesAType and generalCandidates declarations in
// detail/lsp_intel_complete.hpp.
//
// Two sources the compiler already knows: the `std/...` module registry
// (stdModuleSpecifiers) for a cursor inside an import string, and the names
// the document declares plus language keywords and snippets for a cursor in
// code. A relative specifier deliberately answers nothing: completion never
// scans the disk.
//
// The general answer is ordered like the TypeScript extension's popup: the
// names in scope at the cursor first (lsp_intel_scope.cpp), then the names
// the file imports, then the module-level declarations, and the language
// keywords last - and only the ones that may legally appear at the cursor.
// =============================================================================
#include "detail/lsp/lsp_intel_complete.hpp"
#include "detail/lsp/lsp_intel_docs.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/lsp.hpp"
#include "cp3/std_modules.hpp"
#include "cp3/core/types.hpp"

namespace cp3::cli {

// -----------------------------------------------------------------------------
// Import specifiers, declarations, keywords, snippets
// -----------------------------------------------------------------------------

namespace {


bool hasLabel(const std::vector<lsp::CompletionCandidate>& items,
              const std::string& label) {
    for (const lsp::CompletionCandidate& item : items) {
        if (item.label == label) return true;
    }
    return false;
}

void appendNamed(std::vector<lsp::CompletionCandidate>& out,
                 const std::string& name, int kind, const std::string& detail) {
    if (name.empty() || hasLabel(out, name)) return;
    lsp::CompletionCandidate candidate;
    candidate.label = name;
    candidate.kind = kind;
    candidate.detail = detail;
    out.push_back(std::move(candidate));
}

// One named candidate with the documentation the editor shows next to it.
// A candidate that has a doc comment carries it; one that does not carries an
// empty string, and the editor shows the detail line alone.
void appendNamed(std::vector<lsp::CompletionCandidate>& out,
                 const std::string& name, int kind, const std::string& detail,
                 const std::string& documentation) {
    if (name.empty() || hasLabel(out, name)) return;
    lsp::CompletionCandidate candidate;
    candidate.label = name;
    candidate.kind = kind;
    candidate.detail = detail;
    candidate.documentation = documentation;
    out.push_back(std::move(candidate));
}

void appendSnippet(std::vector<lsp::CompletionCandidate>& out,
                   const std::string& label, const std::string& insertText,
                   const std::string& detail) {
    if (hasLabel(out, label)) return;
    lsp::CompletionCandidate candidate;
    candidate.label = label;
    candidate.kind = 15;  // CompletionItemKind.Snippet
    candidate.detail = detail;
    candidate.insertText = insertText;
    candidate.insertTextFormat = 2;
    out.push_back(std::move(candidate));
}

const char* keywordDocumentation(const std::string& keyword) {
    if (keyword == "const") return "Declares a binding that cannot be reassigned. This does not make referenced objects deeply immutable.";
    if (keyword == "let") return "Declares a block-scoped binding that may be reassigned.";
    if (keyword == "var") return "Declares a function-scoped binding for JavaScript compatibility. Prefer `let` or `const`.";
    if (keyword == "function") return "Declares a named function. Parameters and an optional return type can be annotated.";
    if (keyword == "return") return "Returns a value from the current function, or ends it when no value is given.";
    if (keyword == "class") return "Declares a nominal class with fields, methods, and an optional constructor.";
    if (keyword == "interface") return "Declares a structural type shape used for checking and typed data.";
    if (keyword == "type") return "Declares a type alias.";
    if (keyword == "enum") return "Declares a named set of enum members.";
    if (keyword == "if") return "Runs a statement when its condition is truthy.";
    if (keyword == "else") return "Provides the alternative branch of an `if` statement.";
    if (keyword == "while") return "Repeats a statement while its condition is truthy.";
    if (keyword == "do") return "Starts a loop whose body runs before its condition is checked.";
    if (keyword == "for") return "Starts a `for`, `for...of`, or `for...in` loop.";
    if (keyword == "switch") return "Selects a matching `case` branch from one expression.";
    if (keyword == "case") return "Labels a branch in a `switch` statement.";
    if (keyword == "default") return "Labels the fallback branch in a `switch` statement.";
    if (keyword == "try") return "Starts a block whose thrown errors can be handled by `catch` and `finally`.";
    if (keyword == "catch") return "Handles an error thrown from the preceding `try` block.";
    if (keyword == "finally") return "Runs cleanup after a `try` block, whether or not it throws.";
    if (keyword == "throw") return "Throws an error value from the current execution path.";
    if (keyword == "break") return "Exits the nearest loop or `switch` statement.";
    if (keyword == "continue") return "Skips to the next iteration of the nearest loop.";
    if (keyword == "async") return "Marks a function or method as asynchronous; its result is represented by a promise.";
    if (keyword == "await") return "Waits for a promise-like value inside an asynchronous function.";
    if (keyword == "new") return "Constructs an instance of a class.";
    if (keyword == "this") return "Refers to the current instance inside a class method or constructor.";
    if (keyword == "extends") return "Names a base class in a class declaration.";
    if (keyword == "implements") return "Names an interface a class promises to implement.";
    if (keyword == "weak") return "Declares a weak class reference field, which does not keep the referenced object alive.";
    if (keyword == "of") return "Introduces the iterable expression in a `for...of` loop.";
    if (keyword == "in") return "Introduces the key-iteration expression in a `for...in` loop.";
    if (keyword == "void") return "The return type for a function that produces no value.";
    if (keyword == "unknown") return "A type-safe top type that must be narrowed before use.";
    if (keyword == "typeof") return "Returns the runtime type-name string for a value.";
    if (keyword == "true" || keyword == "false") return "A boolean literal.";
    if (keyword == "null") return "The explicit null value; distinct from `undefined`.";
    if (keyword == "undefined") return "The absent-value sentinel; distinct from `null`.";
    if (keyword == "import") return "Imports declarations from a module.";
    if (keyword == "export") return "Makes a declaration available to importing modules.";
    if (keyword == "from") return "Names the source module in an import declaration.";
    if (keyword == "as") return "Renames an imported or exported declaration.";
    if (keyword == "static") return "Declares a class member associated with the class rather than each instance.";
    if (keyword == "public") return "Declares a publicly accessible class member.";
    if (keyword == "private") return "Declares a class member accessible only inside its declaring class.";
    if (keyword == "protected") return "Declares a class member accessible to the class and derived classes.";
    if (keyword == "abstract") return "Declares an abstract class or member that requires a concrete implementation.";
    if (keyword == "get") return "Declares or accesses a getter, depending on its syntactic position.";
    if (keyword == "set") return "Declares or accesses a setter, depending on its syntactic position.";
    if (keyword == "ref") return "Marks a parameter as a reference parameter.";
    if (keyword == "native") return "Marks an import binding supplied by a native CP3 binding.";
    return "CP3 language keyword.";
}

void appendPrimitiveTypes(std::vector<lsp::CompletionCandidate>& out) {
    struct PrimitiveType {
        const char* name;
        const char* description;
    };
    static constexpr PrimitiveType types[] = {
        {"number", "The CP3 numeric type (64-bit floating point)."},
        {"string", "An immutable UTF-8 string value."},
        {"boolean", "A `true` or `false` value."},
        {"void", "The return type of a function that does not return a value."},
        {"undefined", "The type of the `undefined` value."},
        {"null", "The type of the `null` value."},
        {"unknown", "A value whose type must be narrowed before it can be used."},
    };
    for (const PrimitiveType& type : types) {
        appendNamed(out, type.name, 25, "built-in type", type.description);
    }
    static constexpr const char* builtinClasses[] = {
        "Promise", "Worker", "Thread", "EventEmitter", "Date",
        "Int8Array", "Uint8Array", "Uint8ClampedArray", "Int16Array",
        "Uint16Array", "Int32Array", "Uint32Array", "Float32Array",
        "Float64Array", "BigInt64Array", "BigUint64Array", "ArrayBuffer",
        "SharedArrayBuffer", "DataView",
    };
    for (const char* name : builtinClasses) {
        appendNamed(out, name, 7, "built-in type",
                    std::string("The built-in CP3 type `") + name + "`.");
    }
}

// The type the compiler inferred for a declaration, when it knows one.
std::string typeDetail(const SemaResult& sema, std::uint32_t nodeId,
                       const std::string& fallback) {
    const Type* type = sema.typeOf(nodeId);
    if (type == nullptr || type->isUnknown() || type->isError()) return fallback;
    if (type->kind == TypeKind::Object && !type->name.empty() &&
        type->name.front() == '$') {
        return fallback;  // the compiler's placeholder for an unresolved import
    }
    return describe(*type);
}

void appendImportCandidates(const ImportDeclaration& import,
                            std::vector<lsp::CompletionCandidate>& out) {
    const std::string detail = "imported from \"" + import.source + "\"";
    appendNamed(out, import.defaultBinding, 6, detail);  // Variable
    appendNamed(out, import.namespaceBinding, 9, detail);  // Module
    appendNamed(out, import.alias, 6, detail);
    for (const ImportSpecifierPtr& specifier : import.specifiers) {
        if (specifier == nullptr) continue;
        const std::string& local =
            specifier->local.empty() ? specifier->imported : specifier->local;
        appendNamed(out, local, 6, detail);
    }
}

// The names a variable declaration binds, with the type the compiler gave
// and the doc comment of the declaration they came from.
void appendVariableDeclaration(const AnalyzedDocument& document,
                                const VariableDeclaration& declaration,
                                std::vector<lsp::CompletionCandidate>& out) {
    const bool isConst = declaration.kind == VarKind::Const;
    for (const VariableDeclaratorPtr& declarator : declaration.declarations) {
        if (declarator == nullptr || declarator->id == nullptr) continue;
        if (declarator->id->kind != NodeKind::Identifier) continue;
        appendNamed(out, static_cast<const Identifier&>(*declarator->id).name,
                    isConst ? 21 : 6,
                    typeDetail(document.sema, declarator->id->nodeId,
                               isConst ? "const" : "let"),
                    docCommentFor(document, declarator->nodeId));
    }
}

void appendStatementCandidates(const AnalyzedDocument& document,
                               const Stmt& statement,
                               std::vector<lsp::CompletionCandidate>& out,
                               bool typesOnly) {
    switch (statement.kind) {
        case NodeKind::FunctionDeclaration: {
            if (typesOnly) return;
            const auto& declaration =
                static_cast<const FunctionDeclaration&>(statement);
            appendNamed(out, declaration.name, 3,
                        typeDetail(document.sema, statement.nodeId, "function"),
                        docCommentFor(document, statement.nodeId));
            return;
        }
        case NodeKind::ClassDeclaration: {
            const auto& declaration = static_cast<const ClassDeclaration&>(statement);
            appendNamed(out, declaration.name, 7,
                        typeDetail(document.sema, statement.nodeId, "class"),
                        docCommentFor(document, statement.nodeId));
            return;
        }
        case NodeKind::InterfaceDeclaration: {
            const auto& declaration =
                static_cast<const InterfaceDeclaration&>(statement);
            appendNamed(out, declaration.name, 8,
                        typeDetail(document.sema, statement.nodeId, "interface"),
                        docCommentFor(document, statement.nodeId));
            return;
        }
        case NodeKind::EnumDeclaration: {
            const auto& declaration = static_cast<const EnumDeclaration&>(statement);
            appendNamed(out, declaration.name, 13,
                        typeDetail(document.sema, statement.nodeId, "enum"),
                        docCommentFor(document, statement.nodeId));
            return;
        }
        case NodeKind::TypeAliasDeclaration: {
            const auto& declaration =
                static_cast<const TypeAliasDeclaration&>(statement);
            appendNamed(out, declaration.name, 22,
                        typeDetail(document.sema, statement.nodeId, "type"),
                        docCommentFor(document, statement.nodeId));
            return;
        }
        case NodeKind::VariableDeclaration:
            if (typesOnly) return;
            appendVariableDeclaration(
                document, static_cast<const VariableDeclaration&>(statement), out);
            return;
        case NodeKind::ImportDeclaration:
            if (typesOnly) return;
            appendImportCandidates(static_cast<const ImportDeclaration&>(statement),
                                   out);
            return;
        case NodeKind::ExportDeclaration: {
            const auto& declaration = static_cast<const ExportDeclaration&>(statement);
            if (declaration.declaration != nullptr) {
                appendStatementCandidates(document, *declaration.declaration, out,
                                          typesOnly);
            }
            return;
        }
        default:
            return;
    }
}

// The names the module declares at the top level.
void appendDeclarationCandidates(const AnalyzedDocument& document,
                                 std::vector<lsp::CompletionCandidate>& out,
                                 bool typesOnly) {
    for (const StmtPtr& statement : document.entry->ast->body) {
        if (statement == nullptr || !statement->span.valid()) continue;
        if (statement->span.file != document.file) continue;  // prelude tail
        if (statement->kind == NodeKind::ImportDeclaration) continue;
        appendStatementCandidates(document, *statement, out, typesOnly);
    }
}

// The names the file imports.
void appendImportedCandidates(const AnalyzedDocument& document,
                              std::vector<lsp::CompletionCandidate>& out) {
    for (const StmtPtr& statement : document.entry->ast->body) {
        if (statement == nullptr || !statement->span.valid()) continue;
        if (statement->span.file != document.file) continue;  // prelude tail
        if (statement->kind != NodeKind::ImportDeclaration) continue;
        appendImportCandidates(static_cast<const ImportDeclaration&>(*statement),
                               out);
    }
}

// The keywords a statement may start with.
const char* const kStatementKeywords[] = {
    "let", "const", "var", "function", "class", "interface", "enum", "type",
    "import", "export", "if", "else", "while", "do", "for", "switch", "try",
    "return", "throw", "break", "continue", "async", "abstract",
};

// The keywords an expression may start with.
const char* const kExpressionKeywords[] = {
    "new", "this", "await", "typeof", "async", "true", "false", "null",
    "undefined",
};

// The keywords a class member may start with.
const char* const kClassBodyKeywords[] = {
    "static", "public", "private", "protected", "abstract", "async", "get",
    "set", "function", "weak",
};

// True when a completion kind names a type (a class, an interface, an enum
// or a type alias) rather than a value.
bool namesAType(int kind) {
    return kind == 7 || kind == 8 || kind == 13 || kind == 22;
}

}  // namespace

std::vector<std::string> legalKeywords(CompletionPosition position) {
    std::vector<std::string> out;
    switch (position) {
        case CompletionPosition::StatementStart:
            break;
        case CompletionPosition::TryBlock:
            out.push_back("catch");
            out.push_back("finally");
            break;
        case CompletionPosition::SwitchBlock:
            out.push_back("case");
            out.push_back("default");
            break;
        case CompletionPosition::ExpressionStart:
            for (const char* word : kExpressionKeywords) out.push_back(word);
            return out;
        case CompletionPosition::ClassBody:
            for (const char* word : kClassBodyKeywords) out.push_back(word);
            return out;
        case CompletionPosition::ParameterList:
            out.push_back("ref");
            return out;
        case CompletionPosition::ImportClause:
            out.push_back("as");
            out.push_back("from");
            out.push_back("native");
            return out;
        case CompletionPosition::TypePosition:
        case CompletionPosition::DeclarationName:
        case CompletionPosition::MemberPosition:
        case CompletionPosition::Unknown:
            return out;
    }
    for (const char* word : kStatementKeywords) out.push_back(word);
    return out;
}

std::vector<lsp::CompletionCandidate> incompleteCandidates(
    CompletionPosition position) {
    std::vector<lsp::CompletionCandidate> candidates;
    if (position == CompletionPosition::TypePosition) appendPrimitiveTypes(candidates);
    for (const std::string& keyword : legalKeywords(position)) {
        appendNamed(candidates, keyword, 14, "keyword",
                    keywordDocumentation(keyword));
    }
    if (position == CompletionPosition::StatementStart) {
        appendSnippet(candidates, "function",
                      "function ${1:name}(${2}) {\n\t${0}\n}",
                      "declare a function");
        appendSnippet(candidates, "class", "class ${1:Name} {\n\t${0}\n}",
                      "declare a class");
    }
    return candidates;
}

std::vector<lsp::CompletionCandidate> importSpecifierCandidates(
    const std::string& text, const CompletionContext& context,
    std::uint32_t offset) {
    std::vector<lsp::CompletionCandidate> candidates;
    if (context.stringBegin >= offset) return candidates;
    const std::string partial =
        text.substr(context.stringBegin + 1, offset - context.stringBegin - 1);
    if (!partial.empty() && partial.front() == '.') {
        return candidates;  // a relative path: no disk scanning, answer nothing
    }
    for (const std::string& specifier : stdModuleSpecifiers()) {
        lsp::CompletionCandidate candidate;
        candidate.label = specifier;
        candidate.kind = 9;  // CompletionItemKind.Module
        candidate.detail = "std module";
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}


std::vector<lsp::CompletionCandidate> generalCandidates(
    const AnalyzedDocument& document, const CompletionContext& context,
    std::uint32_t offset) {
    std::vector<lsp::CompletionCandidate> candidates;
    const CompletionPosition position = context.position;
    const bool typesOnly = position == CompletionPosition::TypePosition;
    if (typesOnly) appendPrimitiveTypes(candidates);
    // 1. The names in scope at the cursor: the local scope comes first.
    for (const lsp::CompletionCandidate& candidate :
         scopeCandidates(document, position, offset)) {
        if (typesOnly && !namesAType(candidate.kind)) continue;
        appendNamed(candidates, candidate.label, candidate.kind,
                    candidate.detail, candidate.documentation);
    }
    // 2. The names the file imports.
    if (!typesOnly) appendImportedCandidates(document, candidates);
    // 3. The module-level declarations.
    appendDeclarationCandidates(document, candidates, typesOnly);
    // 4. The snippets: a statement may start here.
    if (position == CompletionPosition::StatementStart) {
        appendSnippet(candidates, "import", "import ${1:name} from \"${2:./module.cp3}\";",
                      "import a module");
        appendSnippet(candidates, "function", "function ${1:name}(${2}) {\n\t${0}\n}",
                      "declare a function");
        appendSnippet(candidates, "class", "class ${1:Name} {\n\t${0}\n}",
                      "declare a class");
        appendSnippet(candidates, "interface", "interface ${1:Name} {\n\t${0}\n}",
                      "declare an interface");
    }
    // 5. The keywords that may legally appear here, last.
    for (const std::string& keyword : legalKeywords(position)) {
        appendNamed(candidates, keyword, 14, "keyword",
                    keywordDocumentation(keyword));
    }
    return candidates;
}

}  // namespace cp3::cli
