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
                              "freeMemory", "totalmem", "freemem",
                              "loadavg",  "networkInterfaces"};
bool isOsFn(const std::string& name) { return CP3_IN_TABLE(name, kOsFns); }

}  // namespace

std::string Generator::emitOsCall(const CallExpr& c, const MemberExpr& m) {
    if (!isOsFn(m.property)) return std::string();
    needsOs_ = true;
    if (m.property == "loadavg" || m.property == "networkInterfaces") {
        needsTuple_ = true;
    }
    const std::string call = "::cp3::runtime::os::" + m.property + "(" +
                             joinStrings(emitPlainArgs(c), ", ") + ")";
    // `cpus()` and the memory probes carry the exact std width (`usize` /
    // `u64`); a TypeScript project keeps `number`, so the read widens here.
    if (options_.typeSystem == TypeSystem::TypeScript &&
        (m.property == "cpus" || m.property == "totalMemory" ||
         m.property == "freeMemory" || m.property == "totalmem" ||
         m.property == "freemem")) {
        return "static_cast<double>(" + call + ")";
    }
    return call;
}

}  // namespace cp3
