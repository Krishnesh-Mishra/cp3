// =============================================================================
// src/codegen/src/detail/top_int64_scan.h - PRIVATE whole-unit facts for the
// entry-body int64 proof.
//
// The proof (support/top_int64.cpp) needs facts that only a whole-unit walk
// can produce: every binding declaration (so a shadowed name is visible), the
// first reference of every name (so a use before the declaration is visible),
// every write (so a binding written twice is visible), and the loops a
// `continue` can reach. The walk lives in support/top_int64_scan*.cpp; this
// header is its shared vocabulary.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cp3/core/ast.hpp"

namespace cp3 {
namespace detail {

// The whole-unit walk's facts. `complete` turns false on a construct the walk
// does not model: the proof then rejects every candidate in the unit.
struct TopInt64Scan {
    // name -> how many times the unit binds it.
    std::unordered_map<std::string, int> bindings;
    // Every identifier that appears in the unit (so a generated alias cannot
    // collide with a source name).
    std::unordered_set<std::string> idents;
    // name -> the offset of its first reference.
    std::unordered_map<std::string, std::uint32_t> firstRef;
    // Every assignment to an identifier target.
    std::vector<const AssignmentExpr*> writes;
    // Every update of an identifier operand.
    std::vector<const UpdateExpr*> updates;
    // The writes and updates that sit inside a function / arrow body: they run
    // at a later call, so they cannot be part of a straight-line proof.
    std::unordered_set<const AssignmentExpr*> frameWrites;
    std::unordered_set<const UpdateExpr*> frameUpdates;
    // Every `while` loop of the unit, and the ones a `continue` can reach.
    std::vector<const WhileStatement*> whiles;
    std::unordered_set<const WhileStatement*> continued;
    // The `while` loops currently being walked (a `continue` belongs to the
    // innermost one).
    std::vector<const WhileStatement*> whileStack;
    // True while the walk is inside a function / arrow body.
    bool inFrame = false;
    // False on a construct the walk does not model.
    bool complete = true;
};

// A reference to `name`: its first position orders the reads against the
// declarations.
void noteTopInt64Ref(const Node& n, const std::string& name,
                      TopInt64Scan& scan);
// A binding: a declarator, a parameter, or a catch binding.
void scanTopInt64Name(const Node* id, TopInt64Scan& scan);
// The parameters of a function / arrow.
void scanTopInt64Params(const ParamList& params, TopInt64Scan& scan);
// A list of expressions.
void scanTopInt64ExprList(const ExprList& list, TopInt64Scan& scan);
// One expression.
void scanTopInt64Expr(const Expr& e, TopInt64Scan& scan);
// One statement.
void scanTopInt64Stmt(const Stmt& st, TopInt64Scan& scan);
// Walks every statement of the unit and fills the facts.
void scanTopInt64Unit(const Program& program, TopInt64Scan& scan);

}  // namespace detail
}  // namespace cp3
