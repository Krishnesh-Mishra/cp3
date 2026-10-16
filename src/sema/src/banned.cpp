// =============================================================================
// src/sema/src/banned.cpp — banned-pattern diagnostics (language-spec 08/23).
//
// Owns the four banned-JS-pattern checks: `delete` (kDeleteBanned),
// `arguments` (kArgumentsBanned), `for...in` over a plain object
// (kForInPlainObjectBanned) and the `any` type (kAnyTypeBanned). Each helper is
// called from the expression / statement pass that reaches the construct, so the
// diagnostic still anchors to the offending node (D2).
// =============================================================================
#include "analyzer.h"

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

void Analyzer::checkBannedAny(const SourceSpan& span) {
    report(diagcode::kAnyTypeBanned, span,
           "the 'any' type is banned; use 'unknown' or a concrete type");
}

void Analyzer::reportBannedArguments(const SourceSpan& span) {
    report(diagcode::kArgumentsBanned, span,
           "the 'arguments' object is banned; use a rest parameter instead");
}

void Analyzer::checkBannedDelete(const SourceSpan& span) {
    report(diagcode::kDeleteBanned, span,
           "the 'delete' operator is banned; a fixed-shape object has no "
           "removable fields (use Map<K, V>.delete)");
}

void Analyzer::checkBannedForIn(const Type& right, const SourceSpan& span) {
    if (right.kind == TypeKind::Object && !isAmbientType(right) &&
        !isTupleType(right)) {
        report(diagcode::kForInPlainObjectBanned, span,
               "for...in over a plain object is banned (its keys are known at "
               "compile time); use Map<K, V> or access fields by name");
    }
}

}  // namespace cp3
