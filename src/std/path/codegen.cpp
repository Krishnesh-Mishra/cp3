// =============================================================================
// src/std/path/codegen.cpp - `path.*` call lowering.
//
// Every listed `path` member lowers 1:1 onto cp3::runtime::path
// (`path.sep` lowers in emitPathMember below; the namespace dispatch stays in
// emit/std/common.cpp).
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

#include "detail/int/builtin_tables.h"
#include "detail/generator/cpp_writer.h"

namespace cp3 {

using namespace detail;

namespace {

// path (docs §04): every member lowers 1:1 onto cp3::runtime::path.
const char* const kPathFns[] = {"join",     "resolve",  "extname", "basename",
                                "dirname",  "isAbsolute", "normalize"};
bool isPathFn(const std::string& name) { return CP3_IN_TABLE(name, kPathFns); }

}  // namespace

std::string Generator::emitPathCall(const CallExpr& c, const MemberExpr& m) {
    if (!isPathFn(m.property)) return std::string();
    needsPath_ = true;
    return "::cp3::runtime::path::" + m.property + "(" +
           joinStrings(emitPlainArgs(c), ", ") + ")";
}

// The `path.sep` property read. Returns "" when `m` is not it (defensive: the
// closed path surface is rejected in sema as CP3305 first).
std::string Generator::emitPathMember(const MemberExpr& m) {
    if (!isNsMember(&m, "path", "sep")) return std::string();
    needsPath_ = true;
    return "std::string(::cp3::runtime::path::kSep)";
}

}  // namespace cp3
