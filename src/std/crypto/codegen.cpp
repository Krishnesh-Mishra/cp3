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
// argument to sha256/sha512 lowers to its bytes (see emitCryptoCall).
// `needsCrypto_` makes buildSource add the include.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>

#include "detail/generator/cpp_writer.h"

namespace cp3 {

using namespace detail;

namespace {

// The documented `crypto` member -> its runtime spelling (cp3/std/crypto.hpp).
// `randomBytes` returns the nominal Buffer: the runtime hands back the raw
// bytes as `std::string` and the call is wrapped with
// `::cp3::runtime::buffer::from`; a `Buffer` argument to sha256/sha512 lowers
// to its bytes (the runtime digest surface takes `std::string`).
struct CryptoFn {
    const char* js;
    const char* fn;
};
constexpr CryptoFn kCryptoFns[] = {
    {"randomUUID", "randomUuid"},   {"randomBytes", "randomBytes"},
    {"sha256", "sha256"},           {"sha512", "sha512"},
    {"AES256", "aes256Encrypt"},    {"AES256Decrypt", "aes256Decrypt"},
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
    // `randomBytes(n: number): Buffer`: the runtime produces the raw bytes as
    // `std::string`; wrap them into the nominal Buffer (cp3/std/buffer.hpp).
    if (m.property == "randomBytes" && c.arguments.size() == 1 &&
        c.arguments.front()->kind != NodeKind::SpreadElement) {
        needsBuffer_ = true;
        return "::cp3::runtime::buffer::from(::cp3::runtime::crypto::randomBytes(" +
               joinStrings(emitPlainArgs(c), ", ") + "))";
    }
    // `sha256(s: string | Buffer)` / `sha512(...)`: a Buffer argument lowers to
    // its raw bytes, the `const std::string&` the runtime digest surface takes
    // (cp3/std/buffer.hpp). A string argument lowers unchanged.
    if ((m.property == "sha256" || m.property == "sha512") &&
        c.arguments.size() == 1) {
        const Expr* arg = c.arguments.front().get();
        if (arg && arg->kind != NodeKind::SpreadElement &&
            info(*arg).shape == Shape::Buffer) {
            needsBuffer_ = true;
            return "::cp3::runtime::crypto::" + std::string(fn) +
                   "(::cp3::runtime::buffer::bytes(" + expr(*arg) + "))";
        }
    }
    return "::cp3::runtime::crypto::" + std::string(fn) + "(" +
           joinStrings(emitPlainArgs(c), ", ") + ")";
}

}  // namespace cp3
