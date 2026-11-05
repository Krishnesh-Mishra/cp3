// =============================================================================
// src/codegen/src/emit/std/common.cpp - ambient std surface: shared argument
// lowering and the per-namespace dispatch.
//
// Lowers the `import ... from "std/..."` / builtin surface whose receivers sema
// models as ambient (STDLIB docs §04): `Math.*`, `path.*`, `os.*`,
// `JSON.stringify`, `process.*`, the `{ env }` named import, the `String(x)`
// global, and the `string` method surface + `.length`. Per-family lowering
// (calls and member reads) lives in the `src/std/<family>/codegen.cpp` units;
// the `needs*` flags they record drive the include assembly in
// code_generator.cpp's buildSource.
//
// A member call/property that is not part of a family's table returns "" so
// the caller can report the exact surface error; a std construct with no
// lowering is a real diagnostic, never a placeholder skip.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

#include "detail/int/builtin_tables.h"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/type_map.h"
#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

using namespace detail;

namespace {

// Every element of an array literal is a plain element: a spread inside the
// literal has no static spelling (its elements are a runtime array's).
bool plainElements(const ArrayLiteral& lit) {
    for (const auto& elementPtr : lit.elements) {
        const Expr* element = elementPtr.get();
        if (!element || element->kind == NodeKind::SpreadElement) return false;
    }
    return true;
}

}  // namespace

// Lowers a call's arguments plain (no formatting adapter). A spread argument
// splices the elements of an array into the argument list: the elements must
// be known at compile time (an array literal), because a fixed-arity C++ call
// cannot take a runtime array's elements.
std::vector<std::string> Generator::emitPlainArgs(const CallExpr& c) {
    std::vector<std::string> args;
    args.reserve(c.arguments.size());
    for (const auto& argPtr : c.arguments) {
        const Expr* arg = argPtr.get();
        if (!arg) continue;
        if (arg->kind == NodeKind::SpreadElement) {
            const auto& spread = static_cast<const SpreadElement&>(*arg);
            const Expr* source = spread.argument.get();
            const bool literal =
                source != nullptr && source->kind == NodeKind::ArrayLiteral;
            if (!literal) {
                // A fixed-arity C++ call cannot take a runtime array's
                // elements; only the array-literal form has a static splice.
                reportUnsupported(
                    *arg, diagcode::kTypeMismatch,
                    "type mismatch: a spread argument of this call must be "
                    "an array literal");
                continue;
            }
            const auto& lit = static_cast<const ArrayLiteral&>(*source);
            if (!plainElements(lit)) {
                reportUnsupported(
                    *arg, diagcode::kTypeMismatch,
                    "type mismatch: a spread argument of this call must be "
                    "an array literal without nested spreads");
                continue;
            }
            for (const auto& elementPtr : lit.elements) {
                args.push_back(expr(*elementPtr));
            }
            continue;
        }
        args.push_back(expr(*arg));
    }
    return args;
}

// -----------------------------------------------------------------------------
// Dispatch
// -----------------------------------------------------------------------------

std::string Generator::emitBuiltinCall(const CallExpr& c, const MemberExpr& m) {
    // `Math.min/max(...xs)` with a runtime array. The family lowers the
    // variadic pair through an initializer_list, which cannot be built from a
    // runtime array's elements, so splice the values here and reduce them with
    // the same ECMAScript semantics (NaN propagates; empty is +/-Infinity).
    if (isNamespaceObject(m.object.get(), "Math") &&
        (m.property == "max" || m.property == "min")) {
        bool runtimeSpread = false;
        for (const auto& argPtr : c.arguments) {
            const Expr* arg = argPtr.get();
            if (!arg || arg->kind != NodeKind::SpreadElement) continue;
            const auto& spread = static_cast<const SpreadElement&>(*arg);
            const Expr* source = spread.argument.get();
            if (source == nullptr || source->kind != NodeKind::ArrayLiteral ||
                !plainElements(static_cast<const ArrayLiteral&>(*source))) {
                runtimeSpread = true;
                break;
            }
        }
        if (runtimeSpread) {
            needsMath_ = true;
            std::string body = "std::vector<double> cp3_math_args;";
            for (const auto& argPtr : c.arguments) {
                const Expr* arg = argPtr.get();
                if (!arg) continue;
                if (arg->kind == NodeKind::SpreadElement) {
                    const auto& spread = static_cast<const SpreadElement&>(*arg);
                    const Expr* source = spread.argument.get();
                    if (source == nullptr) {
                        reportCodegenError(
                            c, diagcode::kArityMismatch,
                            "arity mismatch: Math." + m.property +
                                "() cannot take an empty spread");
                        return "0.0";
                    }
                    const Type at = effectiveType(*source);
                    const CppInfo ec =
                        at.kind == TypeKind::Array && !at.parts.empty()
                            ? classify(at.parts.front())
                            : CppInfo{};
                    if (at.kind != TypeKind::Array || at.parts.empty() ||
                        !ec.representable ||
                        (ec.shape != Shape::Number &&
                         ec.shape != Shape::Native &&
                         ec.shape != Shape::Bool)) {
                        reportTypeError(
                            *arg,
                            "type mismatch: a spread argument of Math." +
                                m.property +
                                "() must be an array of numbers");
                        return "0.0";
                    }
                    body += " { const auto& cp3_math_src = " + expr(*source) +
                            "; cp3_math_args.insert(cp3_math_args.end(), "
                            "cp3_math_src->begin(), cp3_math_src->end()); }";
                    continue;
                }
                const CppInfo ac = info(*arg);
                if (ac.representable && ac.shape != Shape::Number &&
                    ac.shape != Shape::Native && ac.shape != Shape::Bool) {
                    reportTypeError(
                        *arg, "type mismatch: Math." + m.property +
                                  "() takes number arguments");
                    return "0.0";
                }
                body += " cp3_math_args.push_back(static_cast<double>(" +
                        expr(*arg) + "));";
            }
            const bool isMax = m.property == "max";
            body += std::string(" double cp3_math_result = ") +
                    (isMax ? "-std::numeric_limits<double>::infinity()"
                           : "std::numeric_limits<double>::infinity()") +
                    ";";
            body += " for (double cp3_math_value : cp3_math_args) {";
            body += " if (std::isnan(cp3_math_value)) return cp3_math_value;";
            body += std::string(" if (cp3_math_value ") + (isMax ? ">" : "<") +
                    " cp3_math_result) cp3_math_result = cp3_math_value; }";
            body += " return cp3_math_result;";
            return "[&]{ " + body + " }()";
        }
    }
    if (isNamespaceObject(m.object.get(), "Math")) return emitMathCall(c, m);
    // Native-integer methods (src/sema member_check.cpp): `v.wrappingMul(x)`.
    if (m.object && info(*m.object).shape == Shape::Native) {
        const CppInfo receiver = info(*m.object);
        if (m.property == "wrappingMul" && receiver.representable) {
            const std::vector<std::string> args = emitPlainArgs(c);
            if (args.size() != 1) return std::string();
            return "::cp3::runtime::checked::wrappingMul<" + receiver.cpp +
                   ">(" + expr(*m.object) + ", " + args.front() + ")";
        }
        // Native scalar `.toString()` (sema member_check.cpp): zero arguments,
        // exact decimal spelling for every width. The runtime overload set
        // routes ints through the exact integral formatter (never `double`)
        // and floats through the shortest-float spelling.
        if (m.property == "toString" && receiver.representable) {
            const std::vector<std::string> args = emitPlainArgs(c);
            if (!args.empty()) return std::string();
            return "::cp3::runtime::toString(" + expr(*m.object) + ")";
        }
    }
    if (isNamespaceObject(m.object.get(), "String") &&
        m.property == "fromCharCode" && !hasValueBinding("String")) {
        // A native-integer argument keeps its exact value: casting it to
        // `double` would round u64 codes above 2^53. The byte semantics are
        // the same (trunc and modulo 256); only the carrier differs. Plain
        // `number` lists keep the std string path untouched.
        bool hasNative = false;
        for (const auto& argPtr : c.arguments) {
            const Expr* arg = argPtr.get();
            if (arg && info(*arg).shape == Shape::Native) {
                hasNative = true;
                break;
            }
        }
        if (hasNative) {
            needsString_ = true;
            std::string list;
            for (const std::string& arg : emitPlainArgs(c)) {
                if (!list.empty()) list += ", ";
                list += "::cp3::runtime::scalar::byteOf(" + arg + ")";
            }
            return "::cp3::runtime::scalar::fromCharCodeBytes({" + list + "})";
        }
        return emitStringStaticCall(c, m);
    }
    if (isNamespaceObject(m.object.get(), "String") &&
        !hasValueBinding("String")) {
        return emitStringStaticCall(c, m);
    }
    if (isNamespaceObject(m.object.get(), "Number") &&
        !hasValueBinding("Number")) {
        return emitNumberStaticCall(c, m);
    }
    if (isNamespaceObject(m.object.get(), "path")) return emitPathCall(c, m);
    if (isNamespaceObject(m.object.get(), "os")) return emitOsCall(c, m);
    if (isNamespaceObject(m.object.get(), "JSON")) return emitJsonCall(c, m);
    if (isNamespaceObject(m.object.get(), "Object") &&
        !hasValueBinding("Object")) {
        return emitObjectCall(c, m);
    }
    if (isNamespaceObject(m.object.get(), "Error") &&
        !hasValueBinding("Error") && m.property == "captureStackTrace" &&
        !c.arguments.empty() && c.arguments.front()) {
        // Refresh the error's stack (v0.2: `name: message` plus one `at <fn>`
        // frame when the optional function is given; sema already proved the
        // target is an Error handle so the cast stays in C++).
        const Expr& target = *c.arguments.front();
        std::string fnName = "\"\"";
        if (c.arguments.size() > 1 && c.arguments[1] &&
            c.arguments[1]->kind == NodeKind::Identifier) {
            fnName = cppStringLiteral(
                static_cast<Identifier&>(*c.arguments[1]).name);
        }
        return "::cp3::Error::captureStackTrace(" + expr(target) + ", " +
               fnName + ")";
    }
    if (isNamespaceObject(m.object.get(), "crypto")) return emitCryptoCall(c, m);
    if (isNamespaceObject(m.object.get(), "network")) {
        return emitNetworkCall(c, m);
    }
    if (isNamespaceObject(m.object.get(), "fs")) return emitFsCall(c, m);
    if (isNamespaceObject(m.object.get(), "io")) return emitIoCall(c, m);
    if (isNamespaceObject(m.object.get(), "url") &&
        !hasValueBinding("url")) {
        return emitUrlCall(c, m);
    }
    // `process.*` (exit/exec/spawn; argv/env reads lower in emitProcessMember)
    // and the `{ env }` named import from std/env (src/std/env/codegen.cpp).
    if (isNamespaceObject(m.object.get(), "process")) {
        return emitProcessCall(c, m);
    }
    if (isNamespaceObject(m.object.get(), "env")) {
        return emitEnvCall(c, m);
    }
    // Process value receivers: the SpawnHandle member calls (`p.on/write/
    // kill`, src/std/process/codegen.cpp).
    if (isProcessObject(m.object.get())) return emitProcessObjectCall(c, m);
    // `std/network` receivers (`res.status(404).text(...)`, `app.get(...)`,
    // `ws.send(...)`, `wss.on(...)`): the documented members lower to the plain
    // C++ member call on the runtime object (src/std/network/codegen.cpp).
    if (std::string ex = emitExpressSettingsMemberCall(c, m); !ex.empty()) return ex;
    if (std::string er = emitExpressRequestCall(c, m); !er.empty()) return er;
    if (isNetworkObject(m.object.get())) return emitNetworkMemberCall(c, m);
    // `std/fs` receivers (`statSync(...).isDirectory`, `rs.on(...)`,
    // `ws.write(...)`, `chunk.toString()`); src/std/fs/codegen.cpp.
    if (isFsReceiver(m.object.get())) return emitFsMemberCall(c, m);
    // `std/date` (src/std/date/codegen.cpp): the `Date.*` statics and the Date
    // value's instance methods (`d.addDays(1)`, `d.getFullYear()`).
    if (std::string date = emitDateCall(c, m); !date.empty()) return date;
    // `std/time` (src/std/time/codegen.cpp): `time.stopwatch()` and Stopwatch
    // value members (`sw.start()`, `sw.elapsed()`).
    if (std::string time = emitTimeCall(c, m); !time.empty()) return time;
    // `std/hash` (src/std/hash/codegen.cpp): `hash.crc32/...` calls.
    if (isNamespaceObject(m.object.get(), "hash")) return emitHashCall(c, m);
    // `std/readline` (src/std/readline/codegen.cpp).
    if (std::string rl = emitReadlineCall(c, m); !rl.empty()) return rl;
    if (isReadlineObject(m.object.get())) return emitReadlineObjectCall(c, m);
    // `std/text` + `std/log` value members (src/std/text/codegen.cpp,
    // src/std/log/codegen.cpp).
    if (std::string text = emitTextMemberCall(c, m); !text.empty()) return text;
    if (std::string lg = emitLogMemberCall(c, m); !lg.empty()) return lg;
    // Core regex literals (D48; src/emit/regex.cpp): `r.test(s)`, `r.match(s)`,
    // `r.exec(s)` on a RegExp value.
    if (std::string regex = emitRegexCall(c, m); !regex.empty()) return regex;
    if (m.object && m.object->kind == NodeKind::ArrayLiteral) {
        std::string literal = emitStringMethodCall(c, m);
        if (!literal.empty()) return literal;
    } else if (m.object && info(*m.object).shape == Shape::String) {
        return emitStringMethodCall(c, m);
    } else if (m.object && info(*m.object).shape == Shape::Number) {
        if (std::string number = emitNumberMethodCall(c, m); !number.empty()) {
            return number;
        }
    }
    // A std/network Map-shaped documented field read (`req.headers.get(...)`,
    // `res.headers.get(...)`): the open marker hides the Map type from sema,
    // so this hook hands the collections lowering the receiver type
    // (src/std/network/codegen.cpp).
    if (std::string field = emitNetworkMapFieldCall(c, m); !field.empty()) {
        return field;
    }
    // The std/collections Map/Set member calls (`m.set(...)`, `s.add(...)`,
    // `forEach(...)`): src/std/collections/codegen.cpp.
    if (std::string coll = emitCollectionsMemberCall(c, m); !coll.empty()) {
        return coll;
    }
    // The regular-array method surface (src/std/arrays/codegen_methods.cpp):
    // push/pop, map/filter/reduce/find/..., indexOf/includes/binarySearch,
    // slice/flat/unique/shuffle.
    if (std::string arr = ArrayEmitter::methodCall(*this, c, m); !arr.empty()) {
        return arr;
    }
    // The JS-compat typed-array family (src/std/typed-arrays/codegen.cpp):
    // `Array.of/from`, the array/typed-view method tables, the buffer `.slice`
    // and DataView.
    if (std::string family = emitFamilyMemberCall(c, m); !family.empty()) {
        return family;
    }
    // The nominal std/buffer `Buffer` (src/std/buffer/codegen.cpp): the
    // `Buffer.from/alloc` statics first, then the instance methods.
    if (std::string buf = emitByteBufferStaticCall(c, m); !buf.empty()) {
        return buf;
    }
    if (std::string buf = emitByteBufferMethodCall(c, m); !buf.empty()) {
        return buf;
    }
    return std::string();
}

std::string Generator::emitBuiltinMember(const MemberExpr& m) {
    // Enum member access (`Color.Green`) and the enum-carrying object
    // literal's field read (src/emit/enum.cpp).
    if (std::string member = emitEnumMember(m); !member.empty()) return member;
    // Core regex literals (D48; src/emit/regex.cpp): `.source` / `.flags`.
    if (std::string regex = emitRegexMember(m); !regex.empty()) return regex;
    if (std::string math = emitMathMember(m); !math.empty()) return math;
    if (std::string number = emitNumberMember(m); !number.empty()) return number;
    // `std/network` field reads (`res.status`, `res.ok`, `req.method`, ...).
    if (std::string net = emitNetworkMemberRead(m); !net.empty()) return net;
    // `std/fs` FileStats reads (`stat.size`, `stat.isDirectory`, ...).
    if (std::string fsStat = emitFsMemberRead(m); !fsStat.empty()) return fsStat;
    // `std/io` constants (`io.POLL_READING`, ...).
    if (std::string io = emitIoMember(m); !io.empty()) return io;
    if (std::string sep = emitPathMember(m); !sep.empty()) return sep;
    if (m.property == "length" && m.object) {
        if (info(*m.object).shape == Shape::String) {
            // D63: `.length` is the UTF-8 byte count; a double keeps the
            // generated expression numeric (the template `cp3_ts` overloads
            // would be ambiguous for size_t).
            return "static_cast<double>((" + expr(*m.object) + ").size())";
        }
    }
    if (std::string proc = emitProcessMember(m); !proc.empty()) return proc;
    // Promise allSettled/withResolvers record members (`.ok`, `.value`,
    // `.reason`, `.promise`, `.resolve`, `.reject`).
    if (std::string rec = emitPromiseRecordMember(m); !rec.empty()) return rec;
    // A std/network Map-shaped field read's `.size` (`req.params.size`): same
    // hint as the call form above (src/std/network/codegen.cpp).
    if (std::string field = emitNetworkMapFieldMember(m); !field.empty()) {
        return field;
    }
    // `std/collections` `m.size` / `s.size` reads and the `entries().length`
    // case (src/std/collections/codegen.cpp).
    if (std::string coll = emitCollectionsMember(m); !coll.empty()) return coll;
    // The JS-compat typed-array family (src/std/typed-arrays/codegen.cpp):
    // `.length` of a cp3 array, typed-view/buffer/DataView members,
    // `X.BYTES_PER_ELEMENT`.
    if (std::string family = emitFamilyMember(m); !family.empty()) {
        return family;
    }
    // A JSON record field read (`u.id` on a `JSON.parse<T>` result,
    // src/std/json/codegen.cpp).
    if (std::string json = emitJsonMember(m); !json.empty()) return json;
    // The nominal std/buffer `Buffer` (src/std/buffer/codegen.cpp): `.length`.
    if (std::string buf = emitByteBufferMember(m); !buf.empty()) return buf;
    if (std::string text = emitTextMember(m); !text.empty()) return text;
    if (std::string lg = emitLogMember(m); !lg.empty()) return lg;
    if (std::string rl = emitReadlineMember(m); !rl.empty()) return rl;
    if (std::string ex = emitExpressSettingsMemberRead(m); !ex.empty()) return ex;
    if (std::string er = emitExpressRequestMember(m); !er.empty()) return er;
    return std::string();
}

}  // namespace cp3
