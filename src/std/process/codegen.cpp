// =============================================================================
// src/std/process/codegen.cpp - `process.*` call lowering.
//
// `process.env.get(name)` -> `string | undefined` (typed by sema's
// std_check.cpp) and `process.exit(code)`; `process.argv` lowers in
// emit/std/common.cpp's emitBuiltinMember.
// =============================================================================
#include "detail/generator.h"

#include <string>

#include "detail/builtin_tables.h"

namespace cp3 {

using namespace detail;

std::string Generator::emitProcessCall(const CallExpr& c, const MemberExpr& m) {
    // `process.env.get(name)` -> `string | undefined` (std_check.cpp types it).
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

}  // namespace cp3
