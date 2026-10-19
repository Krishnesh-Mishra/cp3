// =============================================================================
// src/codegen/src/code_generator.cpp - codegen entry + module assembly
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
#include "detail/generator.h"

#include <string>

#include "cp3/codegen.hpp"
#include "cp3/diagnostic_codes.hpp"
#include "detail/helper_blocks.h"
#include "detail/mangle.h"

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
// A three-way nullable (`T | null | undefined`): Maybe stores the flavor, so
// the empty case prints the flavor itself.
template <class T>
inline std::string cp3_ts(const cp3::runtime::Maybe<T>& v) {
    if (v.isNull()) return std::string("null");
    if (v.isUndefined()) return std::string("undefined");
    return cp3::runtime::toString(v.value());
}
// A boxed erroneous value (cp3/core/error.hpp): `catch (e)` binds one, and cp3
// renders it as the thrown value itself.
inline std::string cp3_ts(const ::cp3::runtime::Thrown& v) { return v.toString(); }

// ----- JS truthiness ---------------------------------------------------------
inline bool cp3_truthy(double v) { return v != 0.0 && !std::isnan(v); }
// The int64 fast path (D64) keeps a `number` as std::int64_t inside a fast
// body; its truthiness and its formatting are those of the exactly equal
// double, so these two overloads are what keeps `cp3_truthy(0LL)` and
// `cp3_ts(5LL)` unambiguous (and correct) in an int64 body.
inline bool cp3_truthy(std::int64_t v) { return v != 0; }
inline std::string cp3_ts(std::int64_t v) {
    return cp3::runtime::toString(static_cast<double>(v));
}
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
template <class T>
inline bool cp3_truthy(const cp3::runtime::Maybe<T>& v) {
    return v.hasValue() && cp3_truthy(v.value());
}

// ----- cp3 `finally` (spec §07, DECISIONS D37) -------------------------------
// Runs the finally block on every exit of the enclosing scope - normal
// completion, `break`/`continue`/`return`, or a propagating throw - exactly
// once. The normal path calls `runNow()`, which deactivates the guard before
// invoking the block, so a throw from `finally` propagates instead of being
// silently repeated. The destructor is `noexcept(false)` for the same reason
// on a `return`/`break` path (JS replaces that completion with the throw); a
// throw while another exception is already in flight terminates the process.
template <class F>
class cp3_finally_guard {
public:
    explicit cp3_finally_guard(F fn) : fn_(fn) {}
    cp3_finally_guard(const cp3_finally_guard&) = delete;
    cp3_finally_guard& operator=(const cp3_finally_guard&) = delete;
    ~cp3_finally_guard() noexcept(false) {
        if (active_) {
            active_ = false;
            fn_();
        }
    }
    void runNow() {
        if (active_) {
            active_ = false;
            fn_();
        }
    }

private:
    F fn_;
    bool active_ = true;
};

}  // namespace

)CP3HELPERS";

// The family's per-TU helpers (formatter + truthiness), added only when the
// unit uses the family: the templates name cp3::runtime types that exist only
// with the cp3/typed_array.hpp include.
const char* const kTypedArrayHelperBlock = R"CP3HELPERS(namespace {

// ----- the JS-compat typed-array family (D69) --------------------------------
// A whole typed view formats as `Int8Array(2) [1, 0]`. The overload must be a
// member template: a Ref<T> would otherwise bind to `cp3_ts(bool)` through the
// pointer conversion.
template <class Tag>
inline std::string cp3_ts(const cp3::runtime::TypedArray<Tag>& a) {
    return cp3::runtime::typedArrayText(a);
}
// A whole regular array formats as `[1, 2, 3]` (the pinned corpus form:
// element text in ", " spacing, no class prefix and no inner padding).
template <class E>
inline std::string cp3_ts(const cp3::runtime::Array<E>& a) {
    return "[" + cp3::runtime::arrayJoin(a, std::string(", ")) + "]";
}
// Every array is an object: JS truthiness is always true, the empty array
// included. Without these, `if (a)` finds no overload (Ref's bool conversion
// is explicit).
template <class Tag>
inline bool cp3_truthy(const cp3::runtime::TypedArray<Tag>&) { return true; }
template <class E>
inline bool cp3_truthy(const cp3::runtime::Array<E>&) { return true; }
// A value-union slot (`number | string` -> std::variant, docs 09): printing
// renders the held alternative, and its JS truthiness is the held value's.
template <class... Ts>
inline std::string cp3_ts(const std::variant<Ts...>& v) {
    return std::visit([](const auto& x) { return cp3_ts(x); }, v);
}
template <class... Ts>
inline bool cp3_truthy(const std::variant<Ts...>& v) {
    return std::visit([](const auto& x) { return cp3_truthy(x); }, v);
}

}  // namespace

)CP3HELPERS";

// std/buffer's per-TU helpers: `console.log(buf)` renders the Node form and a
// Buffer is always truthy. Added only when the unit uses the module (the
// overloads name ::cp3::runtime::buffer::Buffer, which exists only with the
// cp3/std/buffer.hpp include).
const char* const kBufferHelperBlock = R"CP3HELPERS(namespace {

// ----- std/buffer (the nominal Buffer) ---------------------------------------
inline std::string cp3_ts(const ::cp3::runtime::buffer::Buffer& b) {
    return ::cp3::runtime::buffer::inspect(b);
}
inline bool cp3_truthy(const ::cp3::runtime::buffer::Buffer&) { return true; }

}  // namespace

)CP3HELPERS";

// The builtin EventEmitter's per-TU helpers (D74), added only when the unit
// uses the builtin: `EventValue` exists only with the cp3/async/emitter.hpp
// include, and a listener parameter arrives as one.
const char* const kEventEmitterHelperBlock = R"CP3HELPERS(namespace {

// ----- the builtin EventEmitter (D74) -----------------------------------------
// A listener parameter is the erased emitted value; formatting and truthiness
// follow the value it carries.
inline std::string cp3_ts(const cp3::runtime::EventValue& v) {
    return v.toString();
}
inline bool cp3_truthy(const cp3::runtime::EventValue& v) {
    return v.truthy();
}

}  // namespace

)CP3HELPERS";

// Prefix every line of `text` with `pad`. Used to re-indent the top-level
// statements when they are wrapped in the top-level-`await` coroutine.
std::string indentText(const std::string& text, const std::string& pad) {
    std::string out;
    out.reserve(text.size());
    bool atLineStart = true;
    for (char c : text) {
        if (atLineStart && c != '\n') {
            out += pad;
            atLineStart = false;
        }
        out += c;
        if (c == '\n') {
            atLineStart = true;
        }
    }
    return out;
}

}  // namespace

// -----------------------------------------------------------------------------
// Generator - construction, diagnostics and top-level orchestration
// -----------------------------------------------------------------------------

Generator::Generator(DiagnosticSink& sink, const SemaResult& sema,
                     const std::string& moduleName, bool lineDirectives)
    : sink_(sink), sema_(sema), moduleName_(moduleName),
      namesp_(suggestNamespace(moduleName)), lineMapping_(lineDirectives) {}

void Generator::notImplemented(const Node& node, const std::string& what) {
    // Inside an int64 fast-path attempt nothing is reported and nothing is
    // deduped: any CP3002 makes the attempt unusable, and the ordinary pass
    // (which always runs) reports it once, in place.
    if (intMode_) intMode_->failed = true;
    if (suppressDiagnostics_) return;
    if (node.nodeId != 0) {
        if (!reported_.insert(node.nodeId).second) {
            return;
        }
    }
    sink_.error(diagcode::kNotImplemented, node.span,
                "not implemented yet: " + what);
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

    // Usage-based reification (STDLIB docs §04): register the unit's interface
    // record shapes before the emit walk, so a later `JSON.parse<T>` can name T.
    collectJsonShapes(program);

    // The unit's enums (spec 04): resolved before the emit walk (emit/enum.cpp).
    collectEnums(program);

    std::string decls;   // function forward declarations
    std::string defs;    // function definitions
    std::string mainBody;

    for (const auto& stmtPtr : program.body) {
        if (stmtPtr) {
            emitTopLevel(*stmtPtr, decls, defs, mainBody);
        }
    }

    // Enums first (signatures and record shapes can name one), then the object
    // value and JSON record structs, complete before any builder reads them.
    decls = enumDecls_ +
            objectDeclsFor(jsonShapeDecls_ + decls + defs + mainBody) +
            jsonShapeDecls_ + decls;

    // The family include (D69) is needed wherever a generated declaration or
    // definition names its C++ spellings, array types from bodies included.
    const std::string familyUse = decls + defs + mainBody;
    if (familyUse.find("::cp3::runtime::Array") != std::string::npos ||
        familyUse.find("::cp3::runtime::TypedArray") != std::string::npos ||
        familyUse.find("::cp3::runtime::SharedArrayBuffer") !=
            std::string::npos ||
        familyUse.find("::cp3::runtime::DataView") != std::string::npos) {
        needsTypedArray_ = true;
    }
    if (familyUse.find("::cp3::runtime::collections::") != std::string::npos) {
        needsCollections_ = true;
    }
    // A value union (`number | string` -> std::variant, docs 09): the variant
    // spelling and its helpers (unionStrictEquals) come from the family header
    // (cp3/array_extras.hpp, pulled by cp3/typed_array.hpp), which is also
    // where <variant> reaches the unit.
    if (familyUse.find("std::variant<") != std::string::npos) {
        needsTypedArray_ = true;
    }
    // The nominal std/buffer `Buffer` follows the same rule.
    if (familyUse.find("::cp3::runtime::buffer::Buffer") != std::string::npos) {
        needsBuffer_ = true;
    }
    // The builtin EventEmitter (D74): a module-scope binding or a signature can
    // name `Ref<EventEmitter>`/`Listener` without the emitter having been
    // lowered in this unit's own statements.
    if (familyUse.find("::cp3::runtime::EventEmitter") != std::string::npos ||
        familyUse.find("::cp3::runtime::Listener") != std::string::npos) {
        needsEventEmitter_ = true;
    }
    // The operator wave's spellings: bitwise/shift helpers, primitive
    // `std::variant` unions and generated `cp3_obj_` value structs.
    if (familyUse.find("::cp3::runtime::bitwise::") != std::string::npos) needsBitwise_ = true;
    if (familyUse.find("std::variant<") != std::string::npos) needsUnion_ = true;
    if (familyUse.find("::cp3::runtime::unions::") != std::string::npos) needsUnion_ = true;
    if (familyUse.find("cp3_obj_") != std::string::npos) needsObject_ = true;

    GeneratedModule module;
    module.header = buildHeader(decls);
    module.source = buildSource(decls, defs, mainBody);
    return module;
}

// -----------------------------------------------------------------------------
// Generator - assembly (generated .hpp / .cpp)
// -----------------------------------------------------------------------------

std::string Generator::buildHeader(const std::string& decls) const {
    const std::string guard =
        "CP3_GENERATED_" + upperSnake(sanitizeIdent(moduleName_)) + "_HPP";
    std::string out;
    out += "// Generated by cp3c codegen from `" + moduleName_ + "` - do not edit.\n";
    out += "#ifndef " + guard + "\n";
    out += "#define " + guard + "\n\n";
    out += "#include <optional>\n";
    out += "#include <string>\n";
    if (needsUnion_) {
        // A signature can name a primitive union (`std::variant<...>`).
        out += "#include <variant>\n";
    }
    out += "\n";
    out += "#include \"cp3/core/runtime.hpp\"\n";
    if (needsTuple_) {
        // An enum-carrying object literal can surface in a forward declaration
        // (a parameter default).
        out += "#include <tuple>\n";
    }
    if (needsEventLoop_) {
        // A declared `async function` returns `Promise<T>` (spec §14), so the
        // alias must be complete for callers of this header.
        out += "#include \"cp3/async/promise.hpp\"\n";
    }
    if (needsTypedArray_) {
        // The JS-compat typed-array family (D69): a signature or module-scope
        // binding can name `Array<T>`/the typed views.
        out += "#include \"cp3/typed_array.hpp\"\n";
    }
    if (needsCollections_) {
        // std/collections (D44): a signature or module-scope binding can name
        // `Map<K, V>`/`Set<T>`.
        out += "#include \"cp3/std/collections.hpp\"\n";
    }
    if (needsBuffer_) {
        // std/buffer: a signature or module-scope binding can name `Buffer`.
        out += "#include \"cp3/std/buffer.hpp\"\n";
    }
    if (needsEventEmitter_) {
        // The builtin EventEmitter (D74): a signature or module-scope binding
        // can name `Ref<EventEmitter>`/`Listener`.
        out += "#include \"cp3/async/emitter.hpp\"\n";
    }
    out += "\n";
    out += "namespace " + namesp_ + " {\n\n";
    out += decls;
    out += "\n}  // namespace " + namesp_ + "\n\n";
    out += "#endif  // " + guard + "\n";
    return out;
}

std::string Generator::buildSource(const std::string& decls, const std::string& defs,
                                   const std::string& mainBody) const {
    std::string out;
    out += "// Generated by cp3c codegen from `" + moduleName_ + "` - do not edit.\n";
    out += "#include <cmath>\n";
    out += "#include <cstddef>\n";
    out += "#include <cstdint>\n";
    out += "#include <limits>\n";
    out += "#include <optional>\n";
    out += "#include <string>\n";
    if (needsTuple_) out += "#include <tuple>\n";  // enum-carrying object literals
    if (needsUnion_) out += "#include <variant>\n";  // primitive unions
    if (needsEventLoop_) out += "#include <vector>\n";  // Promise input lists
    out += "\n";
    out += "#include \"cp3/core/runtime.hpp\"\n";
    out += "#include \"cp3/core/error.hpp\"\n";
    if (needsBitwise_) out += "#include \"cp3/core/bitwise.hpp\"\n";  // & | ^ ~ << >> >>>
    if (needsUnion_) out += "#include \"cp3/core/union.hpp\"\n";  // primitive unions
    if (needsArith_) {
        out += "#include \"cp3/core/arith.hpp\"\n";  // `%` lowering / int64 fast path
    }
    if (needsEventLoop_) {
        out += "#include \"cp3/async/promise.hpp\"\n";  // Promise/await/timers
    }
    if (needsTypedArray_) {
        out += "#include \"cp3/typed_array.hpp\"\n";  // the family (D69)
    }
    if (needsEventEmitter_) {
        out += "#include \"cp3/async/emitter.hpp\"\n";  // the builtin (D74)
    }
    if (consoleStderr_ || needsConsoleAbi_) {
        out += "#include \"cp3/std/console.hpp\"\n";  // the console family
    }
    if (needsMath_) out += "#include \"cp3/std/math.hpp\"\n";
    if (needsString_) out += "#include \"cp3/std/string.hpp\"\n";
    if (needsPath_) out += "#include \"cp3/std/path.hpp\"\n";
    if (needsOs_) out += "#include \"cp3/std/os.hpp\"\n";
    if (needsJson_) out += "#include \"cp3/std/json.hpp\"\n";
    if (needsProcess_) out += "#include \"cp3/std/process.hpp\"\n";
    if (needsEnv_) out += "#include \"cp3/std/env.hpp\"\n";
    if (needsCrypto_) out += "#include \"cp3/std/crypto.hpp\"\n";
    if (needsDate_) out += "#include \"cp3/std/date.hpp\"\n";
    if (needsNetwork_) out += "#include \"cp3/std/network.hpp\"\n";
    if (needsCollections_) {
        out += "#include \"cp3/std/collections.hpp\"\n";  // Map/Set (D44)
    }
    if (needsBuffer_) out += "#include \"cp3/std/buffer.hpp\"\n";
    if (needsFs_) out += "#include \"cp3/std/fs.hpp\"\n";
    out += "\n";
    out += kHelperBlock;
    if (needsTypedArray_) {
        out += kTypedArrayHelperBlock;
    }
    if (needsBuffer_) {
        out += kBufferHelperBlock;
    }
    if (needsEventEmitter_) {
        out += kEventEmitterHelperBlock;
    }
    if (needsUnion_) {
        out += unionHelperBlock();
    }
    if (needsObject_) {
        out += objectHelperBlock();
    }
    out += "\nnamespace " + namesp_ + " {\n\n";
    out += decls;
    out += "\n";
    out += defs;
    out += "\n}  // namespace " + namesp_ + "\n\n";
    out += "using namespace " + namesp_ + ";\n\n";
    emitMain(out, mainBody);
    return out;
}

void Generator::emitMain(std::string& out, const std::string& mainBody) const {
    if (needsProcess_) {
        // std/process argv: hand the C argument vector to the runtime before
        // any cp3 statement runs (process.argv, pinned by
        // process_argv_is_array).
        out += "int main(int cp3_argc, char** cp3_argv) {\n";
        out += "    ::cp3::runtime::process::initArgv(cp3_argc, cp3_argv);\n";
    } else {
        out += "int main() {\n";
    }
    if (topLevelAwait_) {
        // Top-level `await` (spec §14): the entry unit's statements run inside
        // an eagerly-started coroutine, driven to completion by the loop below.
        out += "    auto cp3_top_level = [&]() -> ::cp3::runtime::Promise<void> {\n";
        out += indentText(mainBody, "    ");
        out += "        co_return;\n";
        out += "    };\n";
        out += "    cp3_top_level();\n";
    } else {
        out += mainBody;
    }
    if (needsEventLoop_) {
        out += "    ::cp3::runtime::runEventLoop();\n";
    }
    out += "    return 0;\n";
    out += "}\n";
}

// -----------------------------------------------------------------------------
// CodeGenerator (frozen boundary)
// -----------------------------------------------------------------------------

GeneratedModule CodeGenerator::generate(const Program& program,
                                        const SemaResult& sema,
                                        const std::string& moduleName) {
    Generator generator(sink_, sema, moduleName, lineDirectives_);
    return generator.run(program);
}

GeneratedModule CodeGenerator::generate(const Program& program,
                                        const std::string& moduleName) {
    return generate(program, SemaResult{}, moduleName);
}

}  // namespace cp3
