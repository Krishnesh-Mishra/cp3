// =============================================================================
// src/std/os/codegen.cpp - `os.*` call lowering.
//
// All listed `os` members are zero-argument; each lowers 1:1 onto
// cp3::runtime::os.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

#include "detail/int/builtin_tables.h"
#include "detail/generator/cpp_writer.h"

namespace cp3 {

using namespace detail;

namespace {

// os (docs §04): all zero-argument.
const char* const kOsFns[] = {"platform", "homedir",    "tmpdir",
                              "hostname", "cpus",       "totalMemory",
                              "freeMemory"};
bool isOsFn(const std::string& name) { return CP3_IN_TABLE(name, kOsFns); }

}  // namespace

std::string Generator::emitOsCall(const CallExpr& c, const MemberExpr& m) {
    if (!isOsFn(m.property)) return std::string();
    needsOs_ = true;
    return "::cp3::runtime::os::" + m.property + "(" +
           joinStrings(emitPlainArgs(c), ", ") + ")";
}

}  // namespace cp3
