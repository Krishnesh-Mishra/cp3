// =============================================================================
// src/codegen/src/emit/std/path.cpp - `path.*` call lowering.
//
// Every listed `path` member lowers 1:1 onto cp3::runtime::path
// (`path.sep` lowers in emit/std/common.cpp's emitBuiltinMember).
// =============================================================================
#include "detail/generator.h"

#include <string>
#include <vector>

#include "detail/builtin_tables.h"
#include "detail/cpp_writer.h"

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

}  // namespace cp3
