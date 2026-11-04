// =============================================================================
// src/std/url/codegen.cpp - lowering for the `std/url` surface.
//
// `encodeURIComponent(s)` -> `::cp3::runtime::url::encodeURIComponent(s)`
// =============================================================================
#include "detail/generator/generator.h"

#include <string>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/int/builtin_tables.h"

namespace cp3 {

using namespace detail;

namespace {

const char* const kUrlMethods[] = {"encodeURIComponent",
                                   "decodeURIComponent", "encodeURI",
                                   "decodeURI"};

}  // namespace

std::string Generator::emitUrlCall(const CallExpr& c, const MemberExpr& m) {
    if (!CP3_IN_TABLE(m.property, kUrlMethods)) return std::string();
    if (c.arguments.size() != 1 || !c.arguments.front()) {
        reportCodegenError(c, diagcode::kArityMismatch,
                           "arity mismatch: " + m.property +
                               "() takes exactly one argument");
        return "0.0";
    }
    needsUrl_ = true;
    return "::cp3::runtime::url::" + m.property + "(" +
           expr(*c.arguments.front()) + ")";
}

}  // namespace cp3
