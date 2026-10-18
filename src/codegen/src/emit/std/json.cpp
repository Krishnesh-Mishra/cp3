// =============================================================================
// src/codegen/src/emit/std/json.cpp - `JSON.stringify` call lowering.
//
// Scalars only this wave; `undefined`, objects, arrays and the indent form
// stay CP3002 (D9 scalar rendering applies inside the runtime overloads).
// =============================================================================
#include "detail/generator.h"

#include <string>

#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

std::string Generator::emitJsonCall(const CallExpr& c, const MemberExpr& m) {
    // `JSON.stringify(value)`: scalars only this wave; `undefined`, objects,
    // arrays and the indent form stay CP3002 (D9 scalar rendering applies
    // inside the runtime overloads).
    if (m.property != "stringify" || c.arguments.size() != 1) return std::string();
    const Expr* arg = c.arguments.front().get();
    if (!arg || arg->kind == NodeKind::SpreadElement) return std::string();
    const CppInfo ci = info(*arg);
    if (ci.shape != Shape::Number && ci.shape != Shape::String &&
        ci.shape != Shape::Bool && ci.shape != Shape::Null) {
        return std::string();
    }
    needsJson_ = true;
    return "::cp3::runtime::json::stringify(" + expr(*arg) + ")";
}

}  // namespace cp3
