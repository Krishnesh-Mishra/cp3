// =============================================================================
// src/codegen/src/code_generator.cpp — codegen entry + module assembly
// (STYLE.md §8).
//
// Owns the frozen `CodeGenerator::generate` boundary (cp3/codegen.hpp), the
// `Generator` constructor/diagnostic helpers, the module namespace, and the
// synthesized assembly: the generated .hpp, the .cpp's per-TU helper block and
// `main()`. The per-concern emit units (type_map/emit_expr/emit_stmt/emit_decl)
// are driven by `Generator::run` here.
//
// Out-of-scope AST nodes report CP3002 (deduped per nodeId) and emit a
// compilable stub; the generator never throws and never aborts. The dedup is
// what drives the CLI's exit-3 "skipped" behavior.
// =============================================================================
#include "generator.h"

#include <string>

#include "cp3/codegen.hpp"
#include "cp3/diagnostic_codes.hpp"
#include "mangle.h"

namespace cp3 {

using namespace detail;

namespace {

// The generated per-TU helper block (anonymous namespace in the .cpp).
const char* const kHelperBlock = R"CP3HELPERS(namespace {

// ----- cp3 value formatting (routes into the D9 runtime formatter) -----------
inline std::string cp3_ts(double v) { return cp3::runtime::toString(v); }
inline std::string cp3_ts(const std::string& v) { return cp3::runtime::toString(v); }
inline std::string cp3_ts(bool v) { return cp3::runtime::toString(v); }
inline std::string cp3_ts(cp3::runtime::Undefined v) { return cp3::runtime::toString(v); }
inline std::string cp3_ts(std::nullptr_t v) { return cp3::runtime::toString(v); }
// Defensive: keeps a raw C string from binding to cp3_ts(bool) through the
// pointer->bool conversion (which would format every literal as "true").
inline std::string cp3_ts(const char* v) {
    return cp3::runtime::toString(std::string(v ? v : ""));
}
inline std::string cp3_ts(const std::optional<double>& v) {
    return v.has_value() ? cp3::runtime::toString(*v) : std::string("undefined");
}
inline std::string cp3_ts(const std::optional<std::string>& v) {
    return v.has_value() ? cp3::runtime::toString(*v) : std::string("undefined");
}
inline std::string cp3_ts(const std::optional<bool>& v) {
    return v.has_value() ? cp3::runtime::toString(*v) : std::string("undefined");
}
// A `T | null` optional: its empty case is `null`, not `undefined`.
inline std::string cp3_ts_null(const std::optional<double>& v) {
    return v.has_value() ? cp3::runtime::toString(*v) : std::string("null");
}
inline std::string cp3_ts_null(const std::optional<std::string>& v) {
    return v.has_value() ? cp3::runtime::toString(*v) : std::string("null");
}
inline std::string cp3_ts_null(const std::optional<bool>& v) {
    return v.has_value() ? cp3::runtime::toString(*v) : std::string("null");
}

// ----- JS truthiness ---------------------------------------------------------
inline bool cp3_truthy(double v) { return v != 0.0 && !std::isnan(v); }
inline bool cp3_truthy(const std::string& v) { return !v.empty(); }
inline bool cp3_truthy(bool v) { return v; }
inline bool cp3_truthy(cp3::runtime::Undefined) { return false; }
inline bool cp3_truthy(std::nullptr_t) { return false; }
// Defensive: JS truthiness of a C string is "non-empty", not "non-null".
inline bool cp3_truthy(const char* v) { return v != nullptr && *v != '\0'; }
inline bool cp3_truthy(const std::optional<double>& v) {
    return v.has_value() && cp3_truthy(*v);
}
inline bool cp3_truthy(const std::optional<std::string>& v) {
    return v.has_value() && cp3_truthy(*v);
}
inline bool cp3_truthy(const std::optional<bool>& v) {
    return v.has_value() && cp3_truthy(*v);
}

}  // namespace

)CP3HELPERS";

}  // namespace

// -----------------------------------------------------------------------------
// Generator — construction, diagnostics and top-level orchestration
// -----------------------------------------------------------------------------

Generator::Generator(DiagnosticSink& sink, const SemaResult& sema,
                     const std::string& moduleName)
    : sink_(sink), sema_(sema), moduleName_(moduleName),
      namesp_(suggestNamespace(moduleName)) {}

void Generator::notImplemented(const Node& node, const std::string& what) {
    if (node.nodeId != 0) {
        if (!reported_.insert(node.nodeId).second) {
            return;
        }
    }
    sink_.error(diagcode::kNotImplemented, node.span,
                std::string("codegen: ") + nodeKindName(node.kind) + " ('" + what +
                    "') is not implemented in M1");
}

GeneratedModule Generator::run(const Program& program) {
    for (const auto& stmtPtr : program.body) {
        if (!stmtPtr) continue;
        collectFunctionNames(*stmtPtr);
        if (stmtPtr->kind == NodeKind::ImportDeclaration) {
            const auto& imp = static_cast<const ImportDeclaration&>(*stmtPtr);
            if (imp.source == "std/console") {
                consoleImported_ = true;
            }
        }
    }

    std::string decls;   // function forward declarations
    std::string defs;    // function definitions
    std::string mainBody;

    for (const auto& stmtPtr : program.body) {
        if (stmtPtr) {
            emitTopLevel(*stmtPtr, decls, defs, mainBody);
        }
    }

    GeneratedModule module;
    module.header = buildHeader(decls);
    module.source = buildSource(decls, defs, mainBody);
    return module;
}

// -----------------------------------------------------------------------------
// Generator — assembly (generated .hpp / .cpp)
// -----------------------------------------------------------------------------

std::string Generator::buildHeader(const std::string& decls) const {
    const std::string guard =
        "CP3_GENERATED_" + upperSnake(sanitizeIdent(moduleName_)) + "_HPP";
    std::string out;
    out += "// Generated by cp3c codegen from `" + moduleName_ + "` — do not edit.\n";
    out += "#ifndef " + guard + "\n";
    out += "#define " + guard + "\n\n";
    out += "#include <optional>\n";
    out += "#include <string>\n\n";
    out += "#include \"cp3/runtime.hpp\"\n\n";
    out += "namespace " + namesp_ + " {\n\n";
    out += decls;
    out += "\n}  // namespace " + namesp_ + "\n\n";
    out += "#endif  // " + guard + "\n";
    return out;
}

std::string Generator::buildSource(const std::string& decls, const std::string& defs,
                                   const std::string& mainBody) const {
    std::string out;
    out += "// Generated by cp3c codegen from `" + moduleName_ + "` — do not edit.\n";
    out += "#include <cmath>\n";
    out += "#include <cstddef>\n";
    out += "#include <limits>\n";
    out += "#include <optional>\n";
    out += "#include <string>\n\n";
    out += "#include \"cp3/runtime.hpp\"\n\n";
    out += kHelperBlock;
    out += "\nnamespace " + namesp_ + " {\n\n";
    out += decls;
    out += "\n";
    out += defs;
    out += "\n}  // namespace " + namesp_ + "\n\n";
    out += "using namespace " + namesp_ + ";\n\n";
    out += "int main() {\n";
    out += mainBody;
    out += "    return 0;\n";
    out += "}\n";
    return out;
}

// -----------------------------------------------------------------------------
// CodeGenerator (frozen boundary)
// -----------------------------------------------------------------------------

GeneratedModule CodeGenerator::generate(const Program& program,
                                        const SemaResult& sema,
                                        const std::string& moduleName) {
    Generator generator(sink_, sema, moduleName);
    return generator.run(program);
}

GeneratedModule CodeGenerator::generate(const Program& program,
                                        const std::string& moduleName) {
    return generate(program, SemaResult{}, moduleName);
}

}  // namespace cp3
