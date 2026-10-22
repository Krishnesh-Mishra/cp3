// =============================================================================
// src/codegen/src/detail/pattern_emitter.h - binding-pattern lowering
// (STYLE.md section 8).
//
// Destructuring is one cohesive lowering with its own recursive walk, so it
// lives on a friend helper (`detail::PatternEmitter`) rather than on the
// Generator declaration: the helper holds only the Generator reference and the
// current source/type site. The implementations are in emit/destructure.cpp.
// =============================================================================
#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "cp3/analysis.hpp"
#include "cp3/ast.hpp"

namespace cp3 {

class Generator;

namespace detail {

// The name a parameter binds (a rest's `RestElement` argument); null for a
// destructuring parameter (bound at the top of the body instead).
const Identifier* parameterIdentifier(const Parameter& p);

struct PatternEmitter {
    explicit PatternEmitter(Generator& generator) : g(generator) {}

    Generator& g;

    // One destructuring site: the C++ value text and its cp3 type.
    struct Site {
        std::string access;
        std::optional<Type> type;
    };

    // Emits one binding per name the pattern introduces, reading each value
    // from `src` (the materialized source local) typed `type`.
    void emitBindings(const Node& pattern, const std::string& src,
                      const std::optional<Type>& type, std::string& out,
                      int indent);
    // One destructuring parameter: the C++ parameter itself is the anonymous
    // `cp3_param<index>`, so the bindings are emitted at the top of the body.
    void emitParam(const Parameter& p, const FunctionDeclaration& fd,
                   std::size_t index, std::string& body);
    // A destructuring declaration with no initializer: every name the pattern
    // introduces is bound to `undefined` (the lowering a plain `let x;` gets).
    // There is no source to read, so the pattern walk never runs.
    void emitUndefinedBindings(const Node& pattern, std::string& out,
                               int indent);

    void walkPattern(const Node& p, const Site& site, std::string& out,
                     int indent);
    void walkAssignmentPattern(const AssignmentPattern& ap, const Site& site,
                               std::string& out, int indent);
    void walkArrayPattern(const ArrayPattern& ap, const Site& site,
                          std::string& out, int indent);
    void walkObjectPattern(const ObjectPattern& op, const Site& site,
                           std::string& out, int indent);
    // A pattern the shape machinery cannot read: CP3002, but declare every name.
    void emitPatternFallback(const Node& p, std::string& out, int indent);
    // One bound name: the binding line plus the scope entry (`Cell` if shared).
    void bindPatternName(const Identifier& id, const Site& site,
                         std::string& out, int indent);
};

}  // namespace detail
}  // namespace cp3
