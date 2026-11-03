// =============================================================================
// src/std/io/codegen.cpp - `std/io` lowering.
//
// OWNERSHIP: the std/io family (src/std/io/**). The namespace dispatch arms
// live in emit/std/common.cpp:
//
//   io.waitReadable(fd)        -> ::cp3::runtime::io::waitReadable(fd)
//   io.waitWritable(fd)        -> ::cp3::runtime::io::waitWritable(fd)
//   io.pollUntilDone(a, b)     -> ::cp3::runtime::io::pollUntilDone(a, b)
//   io.POLL_READING/...        -> ::cp3::runtime::io::POLL_...
//
// The runtime header is included by module_writer/program assembly when
// needsIo_ is set. Phase 3 has no lowering here yet.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/int/builtin_tables.h"

namespace cp3 {

using namespace detail;

namespace {
constexpr const char* kRuntimeNs = "::cp3::runtime::io::";
}

std::string Generator::emitIoCall(const CallExpr& c, const MemberExpr& m) {
    if (!isNamespaceObject(m.object.get(), "io")) return std::string();

    if (m.property == "waitReadable" || m.property == "waitWritable") {
        if (c.arguments.size() != 1 || !c.arguments.front()) {
            reportCodegenError(c, diagcode::kArityMismatch,
                               "io." + m.property + "(fd) takes one argument");
            return "0.0";
        }
        needsIo_ = true;
        needsEventLoop_ = true;
        return std::string(kRuntimeNs) + m.property + "(" +
               expr(*c.arguments.front()) + ")";
    }

    if (m.property == "pollUntilDone") {
        if (c.arguments.size() != 2 || !c.arguments[0] || !c.arguments[1]) {
            reportCodegenError(
                c, diagcode::kArityMismatch,
                "io.pollUntilDone(getSocket, checkProgress) takes two arguments");
            return "0.0";
        }
        needsIo_ = true;
        needsEventLoop_ = true;
        return std::string(kRuntimeNs) + "pollUntilDone(" +
               expr(*c.arguments[0]) + ", " + expr(*c.arguments[1]) + ")";
    }

    return std::string();
}

std::string Generator::emitIoMember(const MemberExpr& m) {
    if (!isNamespaceObject(m.object.get(), "io")) return std::string();
    if (m.property == "POLL_READING" || m.property == "POLL_WRITING" ||
        m.property == "POLL_DONE" || m.property == "POLL_FAILED") {
        needsIo_ = true;
        return std::string(kRuntimeNs) + m.property;
    }
    return std::string();
}

}  // namespace cp3
