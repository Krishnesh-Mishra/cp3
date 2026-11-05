// =============================================================================
// src/codegen/src/detail/listener.h - PRIVATE the EventEmitter listener
// lowering (D74, spec §14).
//
// The listener of an `on`/`once`/`off` call is a function value the emitter
// boxes (cp3/async/emitter.hpp). Three shapes reach the lowering:
//
//   * an inline arrow      -> the lambda is the boxed callable;
//   * a named function     -> the C++ function is the boxed callable;
//   * a function value     -> the value (a `std::function`) is the callable.
//
// A callable whose parameters are all untyped takes the emitted values as they
// are (the erased `EventValue`); a callable with a typed parameter gets an
// adapter around it that converts the erased value to the declared type.
//
// The helper is a friend of `Generator` (like `detail::CallBuilder`) so the
// Generator declaration stays within its budget.
//
//   src/codegen/src/emit/call.cpp - the helper's implementation
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"

namespace cp3 {

class Generator;

namespace detail {

struct ListenerBuilder {
    // The boxed listener value of a listener webserverion ("" when the value is
    // not a listener this lowering can spell).
    static std::string value(Generator& g, const Expr& e);
    // The boxed listener of an inline arrow.
    static std::string arrow(Generator& g, const ArrowFunction& a);
    // The boxed listener of a named function declaration.
    static std::string named(Generator& g, const Expr& e);
    // The boxed listener of a function-typed value (a `std::function`).
    static std::string functionValue(Generator& g, const Expr& e,
                                      const Type& fnType);
    // The parameter types of a listener callable: the annotation, then sema's
    // parameter type, then the type the context expects.
    static std::vector<Type> paramTypes(Generator& g, const ArrowFunction& a);
};

}  // namespace detail
}  // namespace cp3
