// =============================================================================
// src/codegen/src/detail/class_walk.h - PRIVATE model-registration walkers for
// the spec §15 object surface.
//
// `Generator::collectModels` drives these before the emit walk: `walkClasses`
// registers every class declaration (top-level and nested, so a class used
// above its declaration already has a C++ spelling), and the statement /
// webserverion walkers register the structural object shapes (object literals,
// `{ ... }` type annotations) reachable from a body. The registry itself is
// detail/class_model.h.
// =============================================================================
#pragma once

#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "detail/class/class_model.h"

namespace cp3 {
namespace detail {

// Registers every class declared inside `s` (a statement of a body, not the
// module root) and the object shapes its member bodies use. `local` marks the
// classes it finds (a class inside a function body).
void walkClassesForModels(const Stmt& s, bool local, const SemaResult& sema);

// Registers the object/interface shapes reachable from a statement / a whole
// body: object-literal labels and `{ ... }` annotation types, including inside
// class member bodies.
//
// `local` says whether the statement sits inside a function / block body: a
// class declaration there is function-local (its struct is emitted inline in
// the body that declares it), while a class at the unit's root is a
// namespace-scope struct. The walk registers the class declarations it meets,
// so a class inside an arrow-function body - which no statement walk reaches -
// still gets its model before the emit walk.
void walkStmtModels(const Stmt& s, const SemaResult& sema, bool local);
void walkExprModels(const Expr* e, const SemaResult& sema);

// The model fields of an object literal, in property order, with the
// sema-widened value types: the label contract shared by the model walker and
// the object-literal emitter (`typeKeyOf` + `objectLabelOf`).
std::vector<ObjectFieldModel> literalFields(const ObjectLiteral& lit,
                                            const SemaResult& sema);

}  // namespace detail
}  // namespace cp3
