// =============================================================================
// src/codegen/src/detail/class/virtual_set.h - PRIVATE program-wide override
// analysis (support/class/virtual_set.cpp).
//
// The locked dispatch rule: a base-typed member call runs the derived override,
// exactly as in TS/JS. A C++ struct member reaches that dispatch only when it is
// declared `virtual`, so codegen must know, before any class struct is
// emitted, which members of which classes are overridden somewhere in the
// whole program. This header is the one entry point: it builds the set of
// `<className>::<methodName>` pairs that lower as `virtual` members.
//
// The set is computed once per program, from the ASTs of every unit of the
// program (an override can be declared in a unit other than the one that emits
// the base's struct), BEFORE the per-unit emission runs. A single-file build
// passes its one unit. The caller installs the result on the per-unit
// generators (`Generator::virtualMethodSet_`); `collectModels` reads it while
// registering the class member models, and `emitClassMethodMember` spells the
// `virtual` keyword from it.
//
// Fail closed: any relation the analysis cannot model exactly (a duplicate
// class name in the program, an `extends` expression, a base that is not a
// nominal class of the program) marks the affected members `virtual` rather
// than static-bound. A cyclic base relation is bounded by the DFS visited set,
// so a class in a cycle lands in its own ancestor set and its same-name
// methods pair-mark with themselves. Overmarking only costs a vtable slot;
// undermarking breaks JS dispatch semantics.
// =============================================================================
#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace cp3 {
struct Program;  // cp3/core/ast.hpp
namespace detail {

// One `<className>::<methodName>` entry per member that must lower as a C++
// `virtual` member (e.g. "Animal::speak"). The class name is the cp3 class
// name (unmangled) so every unit computes the same key; an empty set means no
// member of this unit's classes has an override anywhere in the program.
std::unordered_set<std::string> buildVirtualMethodSet(
    const std::vector<const Program*>& programs);

}  // namespace detail
}  // namespace cp3
