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

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/lsp.hpp"
#include "cp3/std_modules.hpp"
#include "cp3/std_surface.hpp"
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

struct StdImportSpec {
    const char* label;        // the stdlib value the user is likely to type
    const char* specifier;    // the `std/...` module that exports it
    bool named;               // false = import it as default; true = `{ label }`
    int kind;                 // CompletionItemKind (3 fn, 7 class, 6 const)
    const char* detail;
    const char* documentation;
};

// Curated stdlib surface the editor offers with `import` inserted on
// accept. Order matches roughly how likely a typed prefix matches a fresh
// file (console/process/collections first).
const StdImportSpec kStdImportSpecs[] = {
    {"console", "std/console", false, 6,
     "the console global (std/console)",
     "`console.log(...)`, `console.error(...)`, `console.info(...)` from std/console."},
    {"process", "std/process", false, 6,
     "the process global (std/process)",
     "`process.argv`, `process.env`, `process.exit(n)` from std/process."},
    {"JSON", "std/json", false, 6,
     "the JSON global (std/json)",
     "`JSON.stringify`, `JSON.parse` from std/json."},
    {"Math", "std/math", false, 6,
     "the Math global (std/math)",
     "Constants like `Math.PI`, functions like `Math.floor`, from std/math."},
    {"fs", "std/fs", false, 6,
     "the fs module (std/fs)",
     "`fs.readFile`, `fs.writeFile`, `fs.listFiles` from std/fs."},
    {"io", "std/io", false, 6,
     "the io module (std/io)",
     "`io.readFileStream`, `io.println` from std/io."},
    {"path", "std/path", false, 6,
     "the path module (std/path)",
     "Path query/normalization from std/path: `path.basename`, `path.join`, ..."},
    {"os", "std/os", false, 6,
     "the os module (std/os)",
     "Operating-system information from std/os: `os.cpus()`, `os.homedir`, ..."},
    {"crypto", "std/crypto", false, 6,
     "the crypto module (std/crypto)",
     "Cryptographic primitives from std/crypto."},
    {"URL", "std/url", true, 7,
     "the URL class (std/url)",
     "`new URL(url)`, `URLSearchParams`, from std/url."},
    {"URLSearchParams", "std/url", true, 7,
     "the URLSearchParams class (std/url)",
     "`new URLSearchParams(search)`."},
    {"Map", "std/collections", true, 7,
     "the Map class (std/collections)",
     "`new Map<K, V>()`, `.get`, `.set`, `.size` from std/collections."},
    {"Set", "std/collections", true, 7,
     "the Set class (std/collections)",
     "`new Set<T>()`, `.has`, `.add`, `.size` from std/collections."},
    {"LRUCache", "std/collections", true, 7,
     "the LRUCache class (std/collections)",
     "`new LRUCache<K, V>(capacity)`, `.peek`, `.put`, from std/collections."},
    {"Queue", "std/collections", true, 7,
     "the Queue class (std/collections)",
     "`new Queue<T>()`, `.push`, `.pop` from std/collections."},
    {"Stack", "std/collections", true, 7,
     "the Stack class (std/collections)",
     "`new Stack<T>()`, `.push`, `.pop` from std/collections."},
    {"Buffer", "std/buffer", true, 7,
     "the Buffer class (std/buffer)",
     "`Buffer.from(string)`, `Buffer.from(array)` from std/buffer."},
    {"TextEncoder", "std/text", true, 7,
     "the TextEncoder class (std/text)",
     "`new TextEncoder()`, `.encode(text)` from std/text."},
    {"TextDecoder", "std/text", true, 7,
     "the TextDecoder class (std/text)",
     "`new TextDecoder()`, `.decode(bytes)` from std/text."},
};

// True when the file imports the named specifier (any import form). The
// user may have aliased the import (`import x from "std/console"`); the
// completion still has no business adding a second line.
bool stdModuleImported(const AnalyzedDocument& document, const char* specifier) {
    for (const StmtPtr& statement : document.entry->ast->body) {
        if (!statement || statement->kind != NodeKind::ImportDeclaration) continue;
        const auto& imp = static_cast<const ImportDeclaration&>(*statement);
        if (imp.source == specifier) return true;
    }
    return false;
}

// Stdlib types offered in type positions (e.g. after a `: ` annotation). Same
// shape as the value-position list but distinct from `std/*` runtime exports:
// editors should suggest the types module, not the runtime one.
const struct StdTypeImportSpec {
    const char* label;
    const char* specifier;
    const char* detail;
    const char* documentation;
} kStdTypeImportSpecs[] = {
    {"URL", "std/url/types", "the URL class (std/url/types)",
     "`URL` from std/url/types, used in annotations only."},
    {"URLSearchParams", "std/url/types", "the URLSearchParams class (std/url/types)",
     "`URLSearchParams` from std/url/types, used in annotations only."},
    {"Date", "std/date/types", "the Date class (std/date/types)",
     "`Date` from std/date/types, used in annotations only."},
    {"Map", "std/collections/types", "the Map class (std/collections/types)",
     "`Map<K, V>` from std/collections/types, used in annotations only."},
    {"Set", "std/collections/types", "the Set class (std/collections/types)",
     "`Set<T>` from std/collections/types, used in annotations only."},
    {"LRUCache", "std/collections/types", "the LRUCache class (std/collections/types)",
     "`LRUCache<K, V>` from std/collections/types, used in annotations only."},
    {"MultiMap", "std/collections/types", "the MultiMap class (std/collections/types)",
     "`MultiMap<K, V>` from std/collections/types, used in annotations only."},
    {"ConcurrentMap", "std/collections/types", "the ConcurrentMap class (std/collections/types)",
     "`ConcurrentMap<K, V>` from std/collections/types, used in annotations only."},
    {"Queue", "std/collections/types", "the Queue class (std/collections/types)",
     "`Queue<T>` from std/collections/types, used in annotations only."},
    {"Stack", "std/collections/types", "the Stack class (std/collections/types)",
     "`Stack<T>` from std/collections/types, used in annotations only."},
    {"PriorityQueue", "std/collections/types", "the PriorityQueue class (std/collections/types)",
     "`PriorityQueue<T>` from std/collections/types, used in annotations only."},
    {"RingBuffer", "std/collections/types", "the RingBuffer class (std/collections/types)",
     "`RingBuffer<T>` from std/collections/types, used in annotations only."},
    {"BitSet", "std/collections/types", "the BitSet class (std/collections/types)",
     "`BitSet` from std/collections/types, used in annotations only."},
    {"Buffer", "std/buffer/types", "the Buffer class (std/buffer/types)",
     "`Buffer` from std/buffer/types, used in annotations only."},
    {"TextEncoder", "std/text/types", "the TextEncoder class (std/text/types)",
     "`TextEncoder` from std/text/types, used in annotations only."},
    {"TextDecoder", "std/text/types", "the TextDecoder class (std/text/types)",
     "`TextDecoder` from std/text/types, used in annotations only."},
    {"WebSocket", "std/network/types", "the WebSocket class (std/network/types)",
     "`WebSocket` from std/network/types, used in annotations only."},
    {"Router", "std/network/types", "the Router class (std/network/types)",
     "`Router` from std/network/types, used in annotations only."},
    {"TcpSocket", "std/network/types", "the TcpSocket class (std/network/types)",
     "`TcpSocket` from std/network/types, used in annotations only."},
    {"TcpServer", "std/network/types", "the TcpServer class (std/network/types)",
     "`TcpServer` from std/network/types, used in annotations only."},
    {"TlsSocket", "std/network/types", "the TlsSocket class (std/network/types)",
     "`TlsSocket` from std/network/types, used in annotations only."},
    {"TlsServer", "std/network/types", "the TlsServer class (std/network/types)",
     "`TlsServer` from std/network/types, used in annotations only."},
    {"UdpSocket", "std/network/types", "the UdpSocket class (std/network/types)",
     "`UdpSocket` from std/network/types, used in annotations only."},
};

void appendStdlibTypeImports(const AnalyzedDocument& document,
                             std::vector<lsp::CompletionCandidate>& out) {
    for (const auto& spec : kStdTypeImportSpecs) {
        if (stdModuleImported(document, spec.specifier)) continue;
        if (hasLabel(out, spec.label)) continue;
        lsp::CompletionCandidate candidate;
        candidate.label = spec.label;
        candidate.kind = 7;  // CompletionItemKind.Class
        candidate.detail = spec.detail;
        candidate.documentation = spec.documentation;
        lsp::AdditionalEdit edit;
        edit.line = 1;
        edit.column = 1;
        edit.endLine = 1;
        edit.endColumn = 1;
        edit.newText = std::string("import { ") + spec.label +
                       " } from \"" + spec.specifier + "\";\n";
        candidate.additionalEdits.push_back(std::move(edit));
        out.push_back(std::move(candidate));
    }
}

void appendStdlibImports(const AnalyzedDocument& document,
                         std::vector<lsp::CompletionCandidate>& out) {
    for (const StdImportSpec& spec : kStdImportSpecs) {
        if (stdModuleImported(document, spec.specifier)) continue;
        if (hasLabel(out, spec.label)) continue;
        lsp::CompletionCandidate candidate;
        candidate.label = spec.label;
        candidate.kind = spec.kind;
        candidate.detail = spec.detail;
        candidate.documentation = spec.documentation;
        lsp::AdditionalEdit edit;
        edit.line = 1;
        edit.column = 1;
        edit.endLine = 1;
        edit.endColumn = 1;
        if (spec.named) {
            edit.newText = std::string("import { ") + spec.label + " } from \"" +
                           spec.specifier + "\";\n";
        } else {
            edit.newText = std::string("import ") + spec.label + " from \"" +
                           spec.specifier + "\";\n";
        }
        candidate.additionalEdits.push_back(std::move(edit));
        out.push_back(std::move(candidate));
    }
}

void appendBuiltinValues(std::vector<lsp::CompletionCandidate>& out) {
    struct Builtin {
        const char* name;
        int kind;
        const char* detail;
        const char* documentation;
    };
    static constexpr Builtin values[] = {
        {"Object", 7, "built-in global",
         "Provides object-related language operations such as `Object.keys(value)`."},
        {"String", 3, "String(value): string",
         "Converts a value to its CP3 string representation. With no argument, "
         "it returns `\"undefined\"`.\n\n```cp3\nconst label = String(value);\n```"},
        {"Number", 7, "built-in global", "The CP3 Number global."},
        {"Boolean", 7, "built-in global", "The CP3 Boolean global."},
        {"Array", 7, "built-in global", "The CP3 Array global."},
        {"parseInt", 3, "parseInt(text: string): number",
         "Parses an integer from the beginning of a string. Legacy JS-compatibility "
         "global kept for developer comfort: it always returns cp3 `number` "
         "regardless of the project `typeSystem`; prefer `parseTo<T>` for exact native widths."},
        {"parseFloat", 3, "parseFloat(text: string): number",
         "Parses a floating-point number from the beginning of a string. Legacy "
         "JS-compatibility global kept for developer comfort: it always returns "
         "cp3 `number` regardless of the project `typeSystem`; prefer `parseTo<T>` "
         "for exact native widths."},
        {"isNaN", 3, "built-in function", "Checks whether a number is NaN."},
        {"isFinite", 3, "built-in function", "Checks whether a number is finite."},
        {"print", 3, "built-in function", "Prints a value to standard output."},
        {"Promise", 7, "built-in type and value", "The built-in asynchronous result type."},
        {"EventEmitter", 7, "built-in class", "Creates an event emitter with `on`, `once`, `off`, and `emit` methods."},
        {"Worker", 7, "built-in class", "Creates a worker thread."},
        {"Thread", 7, "built-in class", "The CP3 worker-thread handle type."},
        {"setTimeout", 3, "built-in function", "Schedules a callback after a delay."},
        {"setInterval", 3, "built-in function", "Schedules a callback repeatedly."},
        {"clearTimeout", 3, "built-in function", "Cancels a timeout."},
        {"clearInterval", 3, "built-in function", "Cancels a repeating timer."},
        {"sleep", 3, "sleep(ms: number): Promise<void>", "Resolves after the specified delay."},
        {"Int8Array", 7, "built-in typed-array constructor", "Creates an 8-bit signed integer typed array."},
        {"Uint8Array", 7, "built-in typed-array constructor", "Creates an 8-bit unsigned integer typed array."},
        {"Uint8ClampedArray", 7, "built-in typed-array constructor", "Creates an 8-bit clamped unsigned typed array."},
        {"Int16Array", 7, "built-in typed-array constructor", "Creates a 16-bit signed integer typed array."},
        {"Uint16Array", 7, "built-in typed-array constructor", "Creates a 16-bit unsigned integer typed array."},
        {"Int32Array", 7, "built-in typed-array constructor", "Creates a 32-bit signed integer typed array."},
        {"Uint32Array", 7, "built-in typed-array constructor", "Creates a 32-bit unsigned integer typed array."},
        {"Float32Array", 7, "built-in typed-array constructor", "Creates a 32-bit floating-point typed array."},
        {"Float64Array", 7, "built-in typed-array constructor", "Creates a 64-bit floating-point typed array."},
        {"BigInt64Array", 7, "built-in typed-array constructor", "Creates a 64-bit signed BigInt typed array."},
        {"BigUint64Array", 7, "built-in typed-array constructor", "Creates a 64-bit unsigned BigInt typed array."},
        {"ArrayBuffer", 7, "built-in buffer constructor", "Creates a fixed-length raw byte buffer."},
        {"SharedArrayBuffer", 7, "built-in buffer constructor", "Creates a shared raw byte buffer."},
        {"DataView", 7, "built-in view constructor", "Reads and writes typed values over an ArrayBuffer."},
        {"Symbol", 7, "built-in global", "The CP3 Symbol global."},
        {"BigInt", 7, "built-in global", "The CP3 BigInt global."},
        {"NaN", 21, "number", "The not-a-number numeric value."},
        {"Infinity", 21, "number", "The positive infinity numeric value."},
    };
    for (const Builtin& value : values) {
        if (hasLabel(out, value.name)) continue;
        lsp::CompletionCandidate candidate;
        candidate.label = value.name;
        candidate.kind = value.kind;
        candidate.detail = value.detail;
        candidate.documentation = value.documentation;
        if (std::string(value.name) == "String") {
            // Complete a usable string expression rather than a bare
            // conversion function value in an argument position.
            candidate.insertText = "String(${1:value})";
            candidate.insertTextFormat = 2;
        }
        out.push_back(std::move(candidate));
    }
}

std::optional<Type> builtinResultType(const std::string& name) {
    if (name == "String") return Type::string();
    if (name == "NaN" || name == "Infinity") return Type::number();
    if (name == "parallel") return Type(TypeKind::Object, "parallel");
    if (name == "Object" || name == "Promise" || name == "Worker" ||
        name == "Number" || name == "Boolean" || name == "Array" ||
        name == "Thread" || name == "EventEmitter" || name == "Symbol" ||
        name == "BigInt" || name == "parseInt" || name == "parseFloat" ||
        name == "isNaN" || name == "isFinite" || name == "print" ||
        name == "setTimeout" || name == "setInterval" ||
        name == "clearTimeout" || name == "clearInterval" || name == "sleep" ||
        name == "Int8Array" || name == "Uint8Array" ||
        name == "Uint8ClampedArray" || name == "Int16Array" ||
        name == "Uint16Array" || name == "Int32Array" ||
        name == "Uint32Array" || name == "Float32Array" ||
        name == "Float64Array" || name == "BigInt64Array" ||
        name == "BigUint64Array" || name == "ArrayBuffer" ||
        name == "SharedArrayBuffer" || name == "DataView") {
        return Type::function({}, Type::named(TypeKind::Class, name));
    }
    if (name == "worker") {
        return Type::function({}, Type::named(TypeKind::Class, "Worker"));
    }
    return std::nullopt;
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

const SymbolInfo* completionSymbol(const AnalyzedDocument& document,
                                  const std::string& name,
                                  std::uint32_t offset) {
    if (const SymbolInfo* visible = nearestSymbolAt(
            document.sema, name, *document.file, offset)) {
        return visible;
    }
    // A top-level declaration later in the file is still a useful completion
    // candidate, even though its declaration starts after the cursor.
    const SymbolInfo* later = nullptr;
    for (const auto& entry : document.sema.symbols) {
        const SymbolInfo& symbol = entry.second;
        if (symbol.name != name || !symbol.span.valid() ||
            symbol.span.file != document.file) {
            continue;
        }
        if (later == nullptr || symbol.span.begin.offset < later->span.begin.offset) {
            later = &symbol;
        }
    }
    return later;
}

// The keywords a statement may start with.
const char* const kStatementKeywords[] = {
    "let", "const", "var", "function", "class", "interface", "enum", "type",
    "import", "export", "if", "else", "while", "do", "for", "switch", "try",
    "return", "throw", "break", "continue", "async", "abstract",
};

// The keywords an expression may start with.
const char* const kexpressionKeywords[] = {
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

bool keywordMatchesExpectedType(const std::string& keyword,
                                const Type& expected,
                                bool awaitAllowed) {
    if (keyword == "await") return awaitAllowed;
    if (keyword == "true" || keyword == "false") {
        return isAssignable(Type::boolean(), expected);
    }
    if (keyword == "null") return isAssignable(Type::nullType(), expected);
    if (keyword == "undefined") return isAssignable(Type::undefined(), expected);
    if (keyword == "typeof") return isAssignable(Type::string(), expected);
    if (keyword == "async") {
        return expected.kind == TypeKind::Function || expected.isUnknown() ||
               expected.isError();
    }
    if (keyword == "new" || keyword == "this") {
        // Their concrete class depends on the following identifier. Keep them
        // for object/class contexts; hide them for known scalar arguments.
        return expected.kind == TypeKind::Object ||
               expected.kind == TypeKind::Class || expected.isUnion() ||
               expected.isNullable() || expected.isUnknown() ||
               expected.isError();
    }
    return true;
}

bool candidateFitsExpectedType(const Type& source, const Type& expected) {
    if (source.isUnknown() || source.isError() || expected.isUnknown() ||
        expected.isError() || source.kind == TypeKind::TypeParam ||
        expected.kind == TypeKind::TypeParam ||
        (source.kind == TypeKind::Object && source.name == "$ambient") ||
        (expected.kind == TypeKind::Object && expected.name == "$ambient")) {
        return true;  // unknown is not evidence that a candidate is wrong.
    }
    return isAssignable(source, expected);
}

}  // namespace

std::vector<std::string> legalKeywords(CompletionPosition position,
                                      bool awaitAllowed) {
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
        case CompletionPosition::expressionstart:
            for (const char* word : kexpressionKeywords) {
                if (!awaitAllowed && std::string(word) == "await") continue;
                out.push_back(word);
            }
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
    CompletionPosition position, bool awaitAllowed) {
    std::vector<lsp::CompletionCandidate> candidates;
    if (position == CompletionPosition::TypePosition) appendPrimitiveTypes(candidates);
    for (const std::string& keyword : legalKeywords(position, awaitAllowed)) {
        // Parser recovery cannot prove control-flow placement. Keep these
        // context-sensitive forms out of the grammar-only fallback.
        if (keyword == "return" || keyword == "break" ||
            keyword == "continue" || keyword == "else") {
            continue;
        }
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

// The named import list at the cursor (`import { fetch, | } from "std/network"`).
// Lexical only, text-in/text-out: finds the enclosing `{ ... }`, the `from`
// specifier after it, and looks the module up in the std registry. Returns an
// empty list when the cursor is not inside an import clause or the specifier
// is not a known std module (the caller falls back to the general candidates).
std::vector<lsp::CompletionCandidate> importNamedCandidates(
    const std::string& text, const std::uint32_t offset) {
    std::vector<lsp::CompletionCandidate> candidates;
    if (offset > text.size()) return candidates;
    // Find the enclosing open brace behind the cursor, keeping balance.
    std::int32_t depth = 0;
    std::size_t open = std::string::npos;
    for (std::size_t i = offset; i-- > 0;) {
        const char c = text[i];
        if (c == '}') { depth -= 1; continue; }
        if (c == '{') {
            if (depth == 0) { open = i; break; }
            depth += 1;
        }
    }
    if (open == std::string::npos) return candidates;
    // The `{` must belong to an `import ... from` (or `export ... from`)
    // clause: an `import` keyword (or `from` on the same line, for the
    // two-line form) appears before it. Keep the lookback small.
    const std::string before = text.substr(0, open);
    const std::size_t importAt = before.rfind("import");
    if (importAt == std::string::npos) return candidates;
    if (before.substr(importAt).find('\n') != std::string::npos) return candidates;
    // A default binding may stand between `import` and the brace; that is fine.
    // The module specifier comes after the matching `}`.
    std::int32_t closeDepth = 0;
    std::size_t close = std::string::npos;
    for (std::size_t i = open + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '{') closeDepth += 1;
        else if (c == '}') {
            if (closeDepth == 0) { close = i; break; }
            closeDepth -= 1;
        }
    }
    if (close == std::string::npos) return candidates;
    // `from "<spec>"` after the closing brace (allowing line breaks).
    const std::size_t from = close + 1;
    const std::size_t fromAt = text.find("from", from);
    if (fromAt == std::string::npos || fromAt > from + 32) return candidates;
    std::size_t quote = text.find('"', fromAt);
    if (quote == std::string::npos) quote = text.find('\'', fromAt);
    if (quote == std::string::npos || quote > fromAt + 16) return candidates;
    const std::size_t end = text.find(text[quote] == '"' ? '"' : '\'', quote + 1);
    if (end == std::string::npos) return candidates;
    const std::string specifier = text.substr(quote + 1, end - quote - 1);
    for (const std::string& name : stdModuleExportNames(specifier)) {
        lsp::CompletionCandidate candidate;
        candidate.label = name;
        candidate.kind = 6;  // CompletionItemKind.Variable
        candidate.detail = "named export from " + specifier;
        candidates.push_back(std::move(candidate));
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

std::vector<lsp::CompletionCandidate> stringArgumentCandidates(
    const AnalyzedDocument& document, std::uint32_t offset) {
    std::vector<lsp::CompletionCandidate> candidates;
    if (!analysisReady(document) || document.file == nullptr) return candidates;

    const CallExpr* activeCall = nullptr;
    const MemberExpr* activeMember = nullptr;
    forEachNode(*document.entry->ast, [&](const Node& node) {
        if (node.kind != NodeKind::CallExpr || !node.span.valid() ||
            node.span.file != document.file || offset < node.span.begin.offset ||
            offset > node.span.end.offset) {
            return;
        }
        const auto& call = static_cast<const CallExpr&>(node);
        if (call.arguments.empty() || call.arguments.front() == nullptr ||
            call.arguments.front()->kind != NodeKind::StringLiteral ||
            call.callee == nullptr || call.callee->kind != NodeKind::MemberExpr) {
            return;
        }
        const SourceSpan& argumentSpan = call.arguments.front()->span;
        if (!argumentSpan.valid() || argumentSpan.file != document.file ||
            offset < argumentSpan.begin.offset || offset > argumentSpan.end.offset) {
            return;
        }
        const auto& member = static_cast<const MemberExpr&>(*call.callee);
        if ((member.property != "on" && member.property != "off") ||
            member.object == nullptr ||
            member.object->kind != NodeKind::Identifier) {
            return;
        }
        const auto& object = static_cast<const Identifier&>(*member.object);
        const auto resolved = document.sema.resolvedBindings.find(object.nodeId);
        if (resolved == document.sema.resolvedBindings.end()) return;
        bool isConsoleImport = false;
        for (const StmtPtr& statement : document.entry->ast->body) {
            if (statement == nullptr ||
                statement->kind != NodeKind::ImportDeclaration ||
                statement->nodeId != resolved->second) {
                continue;
            }
            const auto& import = static_cast<const ImportDeclaration&>(*statement);
            isConsoleImport = import.source == "std/console" &&
                (import.defaultBinding == object.name ||
                 import.namespaceBinding == object.name ||
                 import.alias == object.name);
            break;
        }
        if (!isConsoleImport) return;
        if (activeCall == nullptr || node.span.length() < activeCall->span.length()) {
            activeCall = &call;
            activeMember = &member;
        }
    });
    if (activeCall == nullptr || activeMember == nullptr) return candidates;

    for (const std::string& value : stdNamespaceMemberArgumentValues(
             "console", activeMember->property, 0)) {
        lsp::CompletionCandidate candidate;
        candidate.label = value;
        candidate.kind = 20;  // CompletionItemKind.EnumMember
        candidate.detail = "console event";
        if (value == "keypress") {
            candidate.documentation =
                "Emitted for each key read from stdin. The listener receives "
                "the key as a string.\n\n```cp3\nconsole.on(\"keypress\", "
                "(key) => console.log(key));\n```";
        } else if (value == "line") {
            candidate.documentation =
                "Emitted when a complete line is read from stdin, without its "
                "line terminator. The listener receives the line as a string.";
        } else if (value == "close") {
            candidate.documentation =
                "Emitted once when stdin reaches end of file. The listener "
                "receives an empty string.";
        }
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
    if (typesOnly) {
        appendPrimitiveTypes(candidates);
        appendStdlibTypeImports(document, candidates);
    }
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
    // 4. Compiler globals are real expression names, but they are not
    // published as source declarations in SemaResult. Add them explicitly
    // after nearer user bindings so they never outrank a local shadow.
    if (!typesOnly &&
        (position == CompletionPosition::expressionstart ||
         position == CompletionPosition::StatementStart ||
         position == CompletionPosition::Unknown)) {
        appendBuiltinValues(candidates);
        // 5. Stdlib surface that the user reaches through an `import` - offered
        // with an auto-import additional edit, so typing `console<tab>` writes
        // `import console from "std/console";` into the file automatically.
        appendStdlibImports(document, candidates);
    }
    // If the active argument has a resolved primitive parameter type, omit
    // names that the checker would reject there. Keep unknowns visible while
    // a declaration is still being typed; no speculative type is invented.
    if (!typesOnly && context.expectedType != nullptr) {
        const Type& expected = *context.expectedType;
        candidates.erase(
            std::remove_if(candidates.begin(), candidates.end(),
                           [&](const lsp::CompletionCandidate& candidate) {
                               const SymbolInfo* symbol = completionSymbol(
                                   document, candidate.label, offset);
                               if (const std::optional<Type> builtin =
                                       builtinResultType(candidate.label)) {
                                   return !candidateFitsExpectedType(*builtin,
                                                                     expected);
                               }
                               if (symbol == nullptr || symbol->type.isUnknown() ||
                                   symbol->type.isError()) {
                                   return false;
                               }
                               return !candidateFitsExpectedType(symbol->type,
                                                                 expected);
                           }),
            candidates.end());
    }
    // 5. The snippets: a statement may start here.
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
    // 6. The keywords that may legally appear here, last.
    for (const std::string& keyword :
         legalKeywords(position, context.awaitAllowed)) {
        if (!contextualKeywordAllowed(document, offset, keyword)) continue;
        if (context.expectedType != nullptr &&
            !keywordMatchesExpectedType(keyword, *context.expectedType,
                                        context.awaitAllowed)) {
            continue;
        }
        appendNamed(candidates, keyword, 14, "keyword",
                    keywordDocumentation(keyword));
    }
    return candidates;
}

}  // namespace cp3::cli
