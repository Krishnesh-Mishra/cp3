// =============================================================================
// src/codegen/src/emit/std/crypto.cpp - `crypto.*` call lowering.
//
// OWNERSHIP: the crypto wave (std/crypto). One per-module lowering unit; the
// dispatch row already exists in emit/std/common.cpp (emitBuiltinCall), so the
// rest of codegen never needs to change for this module.
//
// STUB: returns "" until the crypto wave lands, which makes the caller report
// CP3002 (not implemented) exactly as before this file existed. The wave
// lowers onto the runtime surface in cp3/std/crypto.hpp (namespace
// cp3::runtime::crypto) and sets `needsCrypto_` so buildSource adds the
// include.
// =============================================================================
#include "detail/generator.h"

#include <string>

namespace cp3 {

using namespace detail;

std::string Generator::emitCryptoCall(const CallExpr& c, const MemberExpr& m) {
    (void)c;
    (void)m;
    return std::string();
}

}  // namespace cp3
