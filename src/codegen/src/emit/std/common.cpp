// =============================================================================
// src/codegen/src/emit/std/common.cpp - ambient std surface: shared argument
// lowering and the per-namespace dispatch.
//
// Lowers the `import ... from "std/..."` / builtin surface whose receivers sema
// models as ambient (STDLIB docs §04): `Math.*`, `path.*`, `os.*`,
// `JSON.stringify`, `process.*`, the `String(x)` global, and the `string`
// method surface + `.length`. Per-family lowering lives in the sibling
// `emit/std/<family>.cpp` units; the `needs*` flags they record drive the
// include assembly in code_generator.cpp's buildSource.
//
// A member call/property that is not part of a family's table reports CP3002
// (the caller falls through to notImplemented), so unsupported std surface
// stays a graceful skip.
// =============================================================================
#include "detail/generator.h"

#include <string>
#include <vector>

#include "detail/builtin_tables.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

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
// Dispatch
// -----------------------------------------------------------------------------

std::string Generator::emitBuiltinCall(const CallExpr& c, const MemberExpr& m) {
    if (isNamespaceObject(m.object.get(), "Math")) return emitMathCall(c, m);
    if (isNamespaceObject(m.object.get(), "path")) return emitPathCall(c, m);
    if (isNamespaceObject(m.object.get(), "os")) return emitOsCall(c, m);
    if (isNamespaceObject(m.object.get(), "JSON")) return emitJsonCall(c, m);
    if (isNamespaceObject(m.object.get(), "crypto")) return emitCryptoCall(c, m);
    if (isNamespaceObject(m.object.get(), "network")) {
        return emitNetworkCall(c, m);
    }
    // `process.*` plus the one nested member this wave lowers:
    // `process.env.get(name)`.
    if (isNamespaceObject(m.object.get(), "process") ||
        isNsMember(m.object.get(), "process", "env")) {
        return emitProcessCall(c, m);
    }
    // `std/network` receivers (`res.status(404).text(...)`, `app.get(...)`,
    // `ws.send(...)`, `wss.on(...)`): the documented members lower to the plain
    // C++ member call on the runtime object (emit/std/network.cpp).
    if (isNetworkObject(m.object.get())) return emitNetworkMemberCall(c, m);
    if (m.object && m.object->kind == NodeKind::ArrayLiteral) {
        std::string literal = emitStringMethodCall(c, m);
        if (!literal.empty()) return literal;
    } else if (m.object && info(*m.object).shape == Shape::String) {
        return emitStringMethodCall(c, m);
    }
    // The JS-compat typed-array family (emit/tarrays.cpp): `Array.of/from`,
    // the array/typed-view method tables, the buffer `.slice` and DataView.
    if (std::string family = emitFamilyMemberCall(c, m); !family.empty()) {
        return family;
    }
    return std::string();
}

std::string Generator::emitBuiltinMember(const MemberExpr& m) {
    if (isNamespaceObject(m.object.get(), "Math")) {
        if (m.property == "PI") { needsMath_ = true; return "::cp3::runtime::math::kPi"; }
        if (m.property == "E") { needsMath_ = true; return "::cp3::runtime::math::kE"; }
    }
    // `std/network` field reads (`res.status`, `res.ok`, `req.method`, ...).
    if (std::string net = emitNetworkMemberRead(m); !net.empty()) return net;
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
    // The JS-compat typed-array family (emit/tarrays.cpp): `.length` of a cp3
    // array, typed-view/buffer/DataView members, `X.BYTES_PER_ELEMENT`.
    if (std::string family = emitFamilyMember(m); !family.empty()) {
        return family;
    }
    return std::string();
}

}  // namespace cp3
