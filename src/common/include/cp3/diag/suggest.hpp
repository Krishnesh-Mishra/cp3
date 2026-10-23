// =============================================================================
// cp3/suggest.hpp - "did you mean" candidate ranking for failed name lookups.
//
// Used by sema and the module loader to fill Diagnostic::suggestions (rendered
// as `help:` lines in the pretty block) and, for the single best candidate, the
// diagnostic message itself (visible without a TTY).
//
// Dependency-free on purpose: cp3_common links into sema, modules and cli, so
// this header must not pull anything beyond the standard library. Matching is
// UTF-8 code-point-safe and bounded; a candidate that is not close enough is
// dropped rather than guessed.
// =============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cp3 {

/// Ranks `candidates` against `name` and returns at most `maxCount` suggestions.
///
/// Order: case-insensitive equality first, then small edit distance with a
/// prefix/suffix bonus; everything above a length-scaled threshold is dropped,
/// so an unrelated name returns an empty list (never a wild guess).
std::vector<std::string> suggestNames(const std::string& name,
                                      const std::vector<std::string>& candidates,
                                      std::size_t maxCount = 3);

/// The single best suggestion for `name`, or "" when none is close enough.
std::string suggestName(const std::string& name, const std::vector<std::string>& candidates);

/// Formats a ranked list as "Did you mean 'a', 'b', or 'c'?" (max three).
/// Returns "" for an empty list.
std::string didYouMeanPhrase(const std::vector<std::string>& names);

}  // namespace cp3
