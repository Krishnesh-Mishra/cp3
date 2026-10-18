// =============================================================================
// src/std/process/codegen.cpp - `process.*` call lowering.
//
// `process.env.get(name)` -> `string | undefined` (typed by sema's
// src/std/process/sema.cpp) and `process.exit(code)`; `process.argv` and
// `process.argv.length` lower in emitProcessMember below.
// =============================================================================
#include "detail/generator.h"

#include <string>

#include "detail/builtin_tables.h"

namespace cp3 {

using namespace detail;

std::string Generator::emitProcessCall(const CallExpr& c, const MemberExpr& m) {
    // `process.env.get(name)` -> `string | undefined` (src/std/process/sema.cpp
    // types it).
    if (m.property == "get" && c.arguments.size() == 1) {
        if (isNsMember(m.object.get(), "process", "env")) {
            Expr* arg = c.arguments.front().get();
            if (!arg || arg->kind == NodeKind::SpreadElement) return std::string();
            needsProcess_ = true;
            return "::cp3::runtime::process::envGet(" + expr(*arg) + ")";
        }
    }
    if (m.property == "exit" && c.arguments.size() == 1) {
        Expr* arg = c.arguments.front().get();
        if (!arg || arg->kind == NodeKind::SpreadElement) return std::string();
        needsProcess_ = true;
        return "::cp3::runtime::process::exit(" + expr(*arg) + ")";
    }
    return std::string();
}

// The `process.argv` / `process.argv.length` property reads (the one nested
// member the surface lowers as a read). Returns "" when `m` is neither, so the
// caller keeps the CP3002 fallthrough.
std::string Generator::emitProcessMember(const MemberExpr& m) {
    if (isNsMember(&m, "process", "argv")) {
        needsProcess_ = true;
        return "::cp3::runtime::process::argv()";
    }
    if (m.property == "length" && isNsMember(m.object.get(), "process", "argv")) {
        needsProcess_ = true;
        return "static_cast<double>(::cp3::runtime::process::argv().size())";
    }
    return std::string();
}

}  // namespace cp3
