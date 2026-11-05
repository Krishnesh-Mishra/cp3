// =============================================================================
// src/std/hash/codegen.cpp - `hash.*` call lowering.
//
// OWNERSHIP: the std/hash family (src/std/hash/**). One per-module lowering
// unit; the dispatch row lives in emit/std/common.cpp (emitBuiltinCall) and
// the `needsHash_` flag in generator_state.h / module_writer.cpp, so the
// rest of codegen never needs to change for this module.
//
// Every documented member lowers 1:1 onto the cp3/std/hash.hpp runtime
// surface (namespace cp3::runtime::hash; crc32/crc32c/fnv are inline in the
// header, xxhash64 is defined in src/std/hash/runtime.cpp); a member outside the
// table returns "" (defensive: the closed hash surface is rejected in sema
// as CP3305 first). A Buffer argument lowers to its byte view
// (cp3/std/buffer.hpp, the crypto sha256/sha512 pattern); a string argument
// lowers unchanged. `needsHash_` makes buildSource add the include.
//
// Width pattern (fs `size` / process `exitCode`): the runtime returns the
// exact std::uint32_t / std::uint64_t, so a Rust/C++ project keeps the
// native width; a TypeScript project widens with static_cast<double>.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/int/builtin_tables.h"

namespace cp3 {

using namespace detail;

namespace {

const char* const kHashMethods[] = {"crc32", "crc32c", "fnv1a32", "fnv1a64",
                                    "xxhash64"};

}  // namespace

std::string Generator::emitHashCall(const CallExpr& c, const MemberExpr& m) {
    if (!isNamespaceObject(m.object.get(), "hash")) return std::string();
    if (!CP3_IN_TABLE(m.property, kHashMethods)) return std::string();
    if (c.arguments.size() != 1 || !c.arguments.front()) {
        reportCodegenError(c, diagcode::kArityMismatch,
                           "arity mismatch: hash." + m.property +
                               "() takes exactly one argument");
        return "0.0";
    }
    needsHash_ = true;
    const Expr* arg = c.arguments.front().get();
    std::string value;
    if (arg && arg->kind != NodeKind::SpreadElement &&
        info(*arg).shape == Shape::Buffer) {
        needsBuffer_ = true;
        value = "::cp3::runtime::hash::" + m.property +
                "(::cp3::runtime::buffer::view(" + expr(*arg) + "))";
    } else {
        value = "::cp3::runtime::hash::" + m.property + "(" + expr(*arg) + ")";
    }
    // The runtime returns the exact u32/u64 width; a TypeScript project
    // sees it as `number`, exactly as the std integer table says.
    if (options_.typeSystem == TypeSystem::TypeScript) {
        return "static_cast<double>(" + value + ")";
    }
    return value;
}

}  // namespace cp3
