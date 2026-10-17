// =============================================================================
// src/codegen/src/emit_builtin.cpp — std-namespace + primitive-method lowering.
//
// Lowers the `import ... from "std/..."` / builtin surface whose receivers sema
// models as ambient (STDLIB docs §04): `Math.*`, `path.*`, `os.*`,
// `JSON.stringify`, `process.*`, the `String(x)` global, and the `string`
// method surface + `.length`. Everything lowers onto the header-only runtime
// additions (cp3/math.hpp, cp3/string.hpp, cp3/path.hpp, cp3/os.hpp,
// cp3/json.hpp, cp3/process.hpp); the `needs*` flags recorded here drive the
// include assembly in code_generator.cpp's buildSource.
//
// A member call/property that is not part of a table below reports CP3002
// (the caller falls through to notImplemented), so unsupported std surface
// stays a graceful skip.
// =============================================================================
#include "generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "cpp_writer.h"
#include "type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// Is `e` the member expression `ns.<prop>` on a bare namespace identifier?
bool isNsMember(const Expr* e, const char* ns, const char* prop) {
    if (!e || e->kind != NodeKind::MemberExpr) return false;
    const auto& m = static_cast<const MemberExpr&>(*e);
    if (m.property != prop) return false;
    return m.object && m.object->kind == NodeKind::Identifier &&
           static_cast<const Identifier&>(*m.object).name == ns;
}

bool isNamespaceObject(const Expr* e, const char* ns) {
    return e && e->kind == NodeKind::Identifier &&
           static_cast<const Identifier&>(*e).name == ns;
}

bool inTable(const std::string& name, const char* const* table, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (name == table[i]) return true;
    }
    return false;
}

#define CP3_IN_TABLE(name, table) \
    inTable((name), (table), sizeof(table) / sizeof((table)[0]))

// Math (docs §04): the variadic `min`/`max` go through the runtime's
// initializer_list overload; everything else is a 1:1 name.
const char* const kMathUnary[] = {"sqrt", "abs",  "floor", "ceil", "round",
                                  "trunc", "cbrt", "log",   "log2", "log10",
                                  "sin",  "cos",  "tan"};
bool isMathFn(const std::string& name) {
    return name == "random" || name == "pow" || name == "min" || name == "max" ||
           name == "gcd" || name == "lcm" || name == "clamp" ||
           name == "lerp" || CP3_IN_TABLE(name, kMathUnary);
}

// path (docs §04): every member lowers 1:1 onto cp3::runtime::path.
const char* const kPathFns[] = {"join",     "resolve",  "extname", "basename",
                                "dirname",  "isAbsolute", "normalize"};
bool isPathFn(const std::string& name) { return CP3_IN_TABLE(name, kPathFns); }

// os (docs §04): all zero-argument.
const char* const kOsFns[] = {"platform", "homedir",    "tmpdir",
                              "hostname", "cpus",       "totalMemory",
                              "freeMemory"};
bool isOsFn(const std::string& name) { return CP3_IN_TABLE(name, kOsFns); }

// The `string` method surface implemented this wave (cp3/string.hpp). `at` is
// JS charAt; `split`/`charCodeAt` are not lowerable yet (wave 3 arrays).
struct StringMethod {
    const char* js;  // the cp3 method name
    const char* fn;  // the runtime spelling
};
constexpr StringMethod kStringMethods[] = {
    {"charAt", "charAt"},       {"at", "charAt"},
    {"startsWith", "startsWith"}, {"endsWith", "endsWith"},
    {"includes", "includes"},   {"repeat", "repeat"},
    {"padStart", "padStart"},   {"padEnd", "padEnd"},
    {"toUpperCase", "toUpperCase"}, {"toLowerCase", "toLowerCase"},
    {"trim", "trim"},           {"trimStart", "trimStart"},
    {"trimEnd", "trimEnd"},     {"indexOf", "indexOf"},
    {"lastIndexOf", "lastIndexOf"}, {"slice", "slice"},
    {"substring", "substring"}, {"replace", "replace"},
    {"replaceAll", "replaceAll"},
};

const char* stringRuntimeFn(const std::string& method) {
    for (const StringMethod& entry : kStringMethods) {
        if (method == entry.js) return entry.fn;
    }
    return nullptr;
}

}  // namespace

// Lowers a call's arguments plain (no formatting adapter). Reports CP3002 for
// a spread argument, consistent with the rest of the emitter.
std::vector<std::string> Generator::emitPlainArgs(const CallExpr& c) {
    std::vector<std::string> args;
    args.reserve(c.arguments.size());
    for (const auto& argPtr : c.arguments) {
        const Expr* arg = argPtr.get();
        if (!arg) continue;
        if (arg->kind == NodeKind::SpreadElement) {
            notImplemented(*arg, "spread argument");
            continue;
        }
        args.push_back(expr(*arg));
    }
    return args;
}

// -----------------------------------------------------------------------------
// Per-namespace call lowering
// -----------------------------------------------------------------------------

std::string Generator::emitMathCall(const CallExpr& c, const MemberExpr& m) {
    if (!isMathFn(m.property)) return std::string();
    needsMath_ = true;
    const std::vector<std::string> args = emitPlainArgs(c);
    if (m.property == "min" || m.property == "max") {
        // The runtime pair takes an initializer_list<double>; the braces make
        // this a list-initialization context, so each element is forced to
        // double (an int-looking spelling would be a narrowing conversion).
        std::string list;
        for (const std::string& arg : args) {
            if (!list.empty()) list += ", ";
            list += "static_cast<double>(" + arg + ")";
        }
        return "::cp3::runtime::math::" + m.property + "({" + list + "})";
    }
    return "::cp3::runtime::math::" + m.property + "(" +
           joinStrings(args, ", ") + ")";
}

std::string Generator::emitPathCall(const CallExpr& c, const MemberExpr& m) {
    if (!isPathFn(m.property)) return std::string();
    needsPath_ = true;
    return "::cp3::runtime::path::" + m.property + "(" +
           joinStrings(emitPlainArgs(c), ", ") + ")";
}

std::string Generator::emitOsCall(const CallExpr& c, const MemberExpr& m) {
    if (!isOsFn(m.property)) return std::string();
    needsOs_ = true;
    return "::cp3::runtime::os::" + m.property + "(" +
           joinStrings(emitPlainArgs(c), ", ") + ")";
}

std::string Generator::emitJsonCall(const CallExpr& c, const MemberExpr& m) {
    // `JSON.stringify(value)`: scalars only this wave; `undefined`, objects,
    // arrays and the indent form stay CP3002 (D9 scalar rendering applies
    // inside the runtime overloads).
    if (m.property != "stringify" || c.arguments.size() != 1) return std::string();
    const Expr* arg = c.arguments.front().get();
    if (!arg || arg->kind == NodeKind::SpreadElement) return std::string();
    const CppInfo ci = info(*arg);
    if (ci.shape != Shape::Number && ci.shape != Shape::String &&
        ci.shape != Shape::Bool && ci.shape != Shape::Null) {
        return std::string();
    }
    needsJson_ = true;
    return "::cp3::runtime::json::stringify(" + expr(*arg) + ")";
}

std::string Generator::emitProcessCall(const CallExpr& c, const MemberExpr& m) {
    // `process.env.get(name)` -> `string | undefined` (std_check.cpp types it).
    if (m.property == "get" && c.arguments.size() == 1) {
        if (isNsMember(m.object.get(), "process", "env")) {
            Expr* arg = c.arguments.front().get();
            if (!arg || arg->kind == NodeKind::SpreadElement) return std::string();
            needsProcess_ = true;
            return "::cp3::runtime::process::envGet(" + expr(*arg) + ")";
        }
    }
    if (m.property == "exit" && c.arguments.size() == 1) {
        Expr* arg = c.arguments.front().get();
        if (!arg || arg->kind == NodeKind::SpreadElement) return std::string();
        needsProcess_ = true;
        return "::cp3::runtime::process::exit(" + expr(*arg) + ")";
    }
    return std::string();
}

std::string Generator::emitStringMethodCall(const CallExpr& c, const MemberExpr& m) {
    // Literal array element set: `["a", "b"].includes(x)` lowers to an equality
    // chain (the only array form representable before wave 3). Any other form
    // on an array receiver stays CP3002 — never fall through to the string
    // method table.
    if (m.object && m.object->kind == NodeKind::ArrayLiteral) {
        if (m.property != "includes" || c.arguments.size() != 1) {
            return std::string();
        }
        const auto& array = static_cast<const ArrayLiteral&>(*m.object);
        Expr* arg = c.arguments.front().get();
        if (!arg || arg->kind == NodeKind::SpreadElement) return std::string();
        std::string list;
        for (const auto& elementPtr : array.elements) {
            const Expr* element = elementPtr.get();
            if (!element || element->kind == NodeKind::SpreadElement) {
                return std::string();
            }
            if (info(*element).shape != Shape::String) return std::string();
            if (!list.empty()) list += ", ";
            list += expr(*element);
        }
        needsString_ = true;
        return "::cp3::runtime::strings::oneOf(" + expr(*arg) + ", {" + list + "})";
    }
    const char* fn = stringRuntimeFn(m.property);
    if (!fn) return std::string();
    needsString_ = true;
    std::vector<std::string> args;
    args.reserve(c.arguments.size() + 1);
    args.push_back(expr(*m.object));
    for (std::string& arg : emitPlainArgs(c)) args.push_back(std::move(arg));
    return "::cp3::runtime::strings::" + std::string(fn) + "(" +
           joinStrings(args, ", ") + ")";
}

// -----------------------------------------------------------------------------
// Dispatch
// -----------------------------------------------------------------------------

std::string Generator::emitBuiltinCall(const CallExpr& c, const MemberExpr& m) {
    if (isNamespaceObject(m.object.get(), "Math")) return emitMathCall(c, m);
    if (isNamespaceObject(m.object.get(), "path")) return emitPathCall(c, m);
    if (isNamespaceObject(m.object.get(), "os")) return emitOsCall(c, m);
    if (isNamespaceObject(m.object.get(), "JSON")) return emitJsonCall(c, m);
    // `process.*` plus the one nested member this wave lowers:
    // `process.env.get(name)`.
    if (isNamespaceObject(m.object.get(), "process") ||
        isNsMember(m.object.get(), "process", "env")) {
        return emitProcessCall(c, m);
    }
    if (m.object && m.object->kind == NodeKind::ArrayLiteral) {
        return emitStringMethodCall(c, m);
    }
    if (m.object && info(*m.object).shape == Shape::String) {
        return emitStringMethodCall(c, m);
    }
    return std::string();
}

std::string Generator::emitBuiltinMember(const MemberExpr& m) {
    if (isNamespaceObject(m.object.get(), "Math")) {
        if (m.property == "PI") { needsMath_ = true; return "::cp3::runtime::math::kPi"; }
        if (m.property == "E") { needsMath_ = true; return "::cp3::runtime::math::kE"; }
    }
    if (isNsMember(&m, "path", "sep")) {
        needsPath_ = true;
        return "std::string(::cp3::runtime::path::kSep)";
    }
    if (isNsMember(&m, "process", "argv")) {
        needsProcess_ = true;
        return "::cp3::runtime::process::argv()";
    }
    if (m.property == "length" && m.object) {
        if (info(*m.object).shape == Shape::String) {
            // D63: `.length` is the UTF-8 byte count; a double keeps the
            // generated expression numeric (the template `cp3_ts` overloads
            // would be ambiguous for size_t).
            return "static_cast<double>((" + expr(*m.object) + ").size())";
        }
        if (isNsMember(m.object.get(), "process", "argv")) {
            needsProcess_ = true;
            return "static_cast<double>(::cp3::runtime::process::argv().size())";
        }
    }
    return std::string();
}

}  // namespace cp3
