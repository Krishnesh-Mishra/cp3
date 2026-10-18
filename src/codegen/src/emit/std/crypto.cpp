// =============================================================================
// src/codegen/src/emit/std/crypto.cpp - `crypto.*` call lowering.
//
// OWNERSHIP: the crypto wave (std/crypto). One per-module lowering unit; the
// dispatch row already exists in emit/std/common.cpp (emitBuiltinCall), so the
// rest of codegen never needs to change for this module.
//
// Every documented member (docs/standard-library/04 section crypto) lowers 1:1
// onto the runtime surface in cp3/std/crypto.hpp (namespace
// cp3::runtime::crypto); a member outside the table returns "" so the caller
// keeps reporting CP3002. String arguments and results are `std::string` per
// the rest of the std surface; `needsCrypto_` makes buildSource add the
// include.
// =============================================================================
#include "detail/generator.h"

#include <string>

#include "detail/cpp_writer.h"

namespace cp3 {

using namespace detail;

namespace {

// The documented `crypto` member -> its runtime spelling (cp3/std/crypto.hpp).
// `Buffer` parameters and results lower as raw-byte `std::string`s, the
// beta-one stand-in the pinned cases pin (`.length` == byte count).
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
    return "::cp3::runtime::crypto::" + std::string(fn) + "(" +
           joinStrings(emitPlainArgs(c), ", ") + ")";
}

}  // namespace cp3
