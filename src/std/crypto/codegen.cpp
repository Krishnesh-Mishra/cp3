// =============================================================================
// src/std/crypto/codegen.cpp - `crypto.*` call lowering.
//
// OWNERSHIP: the std/crypto family (src/std/crypto/**). One per-module lowering unit; the
// dispatch row already exists in emit/std/common.cpp (emitBuiltinCall), so the
// rest of codegen never needs to change for this module.
//
// Every documented member (docs/standard-library/04 section crypto) lowers 1:1
// onto the runtime surface in cp3/std/crypto.hpp (namespace
// cp3::runtime::crypto); a member outside the table returns "" (defensive: the
// closed crypto surface is rejected in sema as CP3305 first). String arguments
// and results are `std::string` per the rest of the std surface; a `Buffer`
// argument to sha256/sha512 lowers to its byte view (see emitCryptoCall).
// `needsCrypto_` makes buildSource add the include.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

#include "detail/generator/cpp_writer.h"

namespace cp3 {

using namespace detail;

namespace {

// The documented `crypto` member -> its runtime spelling (cp3/std/crypto.hpp).
// `randomBytes` returns the nominal Buffer: the runtime hands back the raw
// bytes as `std::string` and the call is wrapped with
// `::cp3::runtime::buffer::from`; a `Buffer` argument to sha256/sha512 lowers
// to its byte view (the runtime digest surface takes `std::string_view`).
struct CryptoFn {
    const char* js;
    const char* fn;
};
constexpr CryptoFn kCryptoFns[] = {
    {"randomUUID", "randomUuid"},   {"randomBytes", "randomBytes"},
    {"sha256", "sha256"},           {"sha512", "sha512"},
    {"AES256", "aes256Encrypt"},    {"AES256Decrypt", "aes256Decrypt"},
    // --- Ledger D extensions (append-only) ---------------------------------
    {"hmacSha256", "hmacSha256"},   {"hmacSha512", "hmacSha512"},
    {"blake2b", "blake2b"},         {"getRandomValues", "getRandomValues"},
    {"uuidParse", "uuidParse"},     {"uuidValidate", "uuidValidate"},
    {"uuidVersion", "uuidVersion"}, {"sha256Raw", "sha256Raw"},
};

const char* cryptoRuntimeFn(const std::string& name) {
    for (const CryptoFn& entry : kCryptoFns) {
        if (name == entry.js) return entry.fn;
    }
    return nullptr;
}

}  // namespace

std::string Generator::emitCryptoCall(const CallExpr& c, const MemberExpr& m) {
    const char* fn = cryptoRuntimeFn(m.property);
    if (!fn) return std::string();
    needsCrypto_ = true;
    // `randomBytes(n): Buffer`: the runtime produces the raw bytes as
    // `std::string`; wrap them into the nominal Buffer (cp3/std/buffer.hpp).
    // `n` is the std length width: a Rust/C++ project's argument is already
    // `u64` (cast here so a widened literal picks the exact overload), while
    // a TypeScript project's `number` passes through to the `double`
    // overload, which keeps the fractional/negative validation.
    if (m.property == "randomBytes" && c.arguments.size() == 1 &&
        c.arguments.front()->kind != NodeKind::SpreadElement) {
        needsBuffer_ = true;
        std::string arg = joinStrings(emitPlainArgs(c), ", ");
        if (options_.typeSystem != TypeSystem::TypeScript) {
            arg = "static_cast<std::uint64_t>(" + arg + ")";
        }
        return "::cp3::runtime::buffer::from(::cp3::runtime::crypto::randomBytes(" +
               arg + "))";
    }
    // `sha256(s: string | Buffer)` / `sha512(...)`: a Buffer argument lowers to
    // its non-owning byte view, the `std::string_view` the runtime digest
    // surface takes (cp3/std/buffer.hpp). A string argument lowers unchanged.
    if ((m.property == "sha256" || m.property == "sha512") &&
        c.arguments.size() == 1) {
        const Expr* arg = c.arguments.front().get();
        if (arg && arg->kind != NodeKind::SpreadElement &&
            info(*arg).shape == Shape::Buffer) {
            needsBuffer_ = true;
            return "::cp3::runtime::crypto::" + std::string(fn) +
                   "(::cp3::runtime::buffer::view(" + expr(*arg) + "))";
        }
    }
    // --- Ledger D extensions (append-only branches) -------------------------
    // Raw-byte results (`Buffer` in the cp3 surface): wrap the runtime's
    // raw `std::string` with ::cp3::runtime::buffer::from, as randomBytes.
    if ((m.property == "getRandomValues" || m.property == "sha256Raw" ||
         m.property == "uuidParse") &&
        c.arguments.size() == 1 &&
        c.arguments.front()->kind != NodeKind::SpreadElement) {
        const Expr* arg = c.arguments.front().get();
        needsBuffer_ = true;
        std::string inner = "::cp3::runtime::crypto::" + std::string(fn) + "(";
        if (arg && arg->kind != NodeKind::SpreadElement &&
            (m.property == "sha256Raw") &&
            info(*arg).shape == Shape::Buffer) {
            inner += "::cp3::runtime::buffer::view(" + expr(*arg) + ")";
        } else if (m.property == "getRandomValues" &&
                   options_.typeSystem != TypeSystem::TypeScript) {
            // The std length width (see `randomBytes` above).
            inner += "static_cast<std::uint64_t>(" +
                     joinStrings(emitPlainArgs(c), ", ") + ")";
        } else {
            inner += joinStrings(emitPlainArgs(c), ", ");
        }
        return "::cp3::runtime::buffer::from(" + inner + "))";
    }
    // One `string | Buffer` argument lowering to its byte view (blake2b).
    if (m.property == "blake2b" && c.arguments.size() == 1) {
        const Expr* arg = c.arguments.front().get();
        if (arg && arg->kind != NodeKind::SpreadElement &&
            info(*arg).shape == Shape::Buffer) {
            needsBuffer_ = true;
            return "::cp3::runtime::crypto::" + std::string(fn) +
                   "(::cp3::runtime::buffer::view(" + expr(*arg) + "))";
        }
    }
    // Two `string | Buffer` arguments (hmacSha256/hmacSha512): each Buffer
    // argument lowers to its byte view independently.
    if ((m.property == "hmacSha256" || m.property == "hmacSha512") &&
        c.arguments.size() == 2) {
        const std::vector<std::string> plain = emitPlainArgs(c);
        std::string call = "::cp3::runtime::crypto::" + std::string(fn) + "(";
        for (std::size_t i = 0; i < c.arguments.size(); ++i) {
            if (i != 0) call += ", ";
            const Expr* arg = c.arguments[i].get();
            if (arg && arg->kind != NodeKind::SpreadElement &&
                info(*arg).shape == Shape::Buffer) {
                needsBuffer_ = true;
                call += "::cp3::runtime::buffer::view(" + plain[i] + ")";
            } else {
                call += plain[i];
            }
        }
        return call + ")";
    }
    return "::cp3::runtime::crypto::" + std::string(fn) + "(" +
           joinStrings(emitPlainArgs(c), ", ") + ")";
}

}  // namespace cp3
