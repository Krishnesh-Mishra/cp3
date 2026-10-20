// =============================================================================
// src/codegen/src/detail/class_walk.h - PRIVATE model-registration walkers for
// the spec §15 object surface.
//
// `Generator::collectModels` drives these before the emit walk: `walkClasses`
// registers every class declaration (top-level and nested, so a class used
// above its declaration already has a C++ spelling), and the statement /
// expression walkers register the structural object shapes (object literals,
// `{ ... }` type annotations) reachable from a body. The registry itself is
// detail/class_model.h.
// =============================================================================
#pragma once

#include <vector>

#include "cp3/analysis.hpp"
#include "cp3/ast.hpp"
#include "detail/class_model.h"

namespace cp3 {
namespace detail {

// Registers every class declared inside `s` (a statement of a body, not the
// module root) and the object shapes its member bodies use. `local` marks the
// classes it finds (a class inside a function body).
void walkClassesForModels(const Stmt& s, bool local, const SemaResult& sema);

// Registers the object/interface shapes reachable from a statement / a whole
// body: object-literal labels and `{ ... }` annotation types, including inside
// class member bodies.
void walkStmtModels(const Stmt& s, const SemaResult& sema);
void walkExprModels(const Expr* e, const SemaResult& sema);

// The model fields of an object literal, in property order, with the
// sema-widened value types: the label contract shared by the model walker and
// the object-literal emitter (`typeKeyOf` + `objectLabelOf`).
std::vector<ObjectFieldModel> literalFields(const ObjectLiteral& lit,
                                            const SemaResult& sema);

}  // namespace detail
}  // namespace cp3
