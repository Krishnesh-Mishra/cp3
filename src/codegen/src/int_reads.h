// The read walk behind the deferred bound check of the int64 fast path (D64).
#pragma once

#include <optional>
#include <string>
#include <unordered_set>

#include "cp3/ast.hpp"

namespace cp3 {

// The names read somewhere in `body` outside of a `return` argument. A name
// absent from the result is read only by `return` (or never), which is what
// lets the fast path move its range check from every operation to the one
// observation point (`arith::pass` at the read).
//
// The walk is deliberately conservative: an unmodelled node kind, a binding
// pattern or a missing child makes it return `nullopt` (nothing may be
// deferred), so a caller is never told "no reads outside returns" for a body
// the walk did not fully understand.
std::optional<std::unordered_set<std::string>> namesReadOutsideReturns(const Stmt& body);

}  // namespace cp3
