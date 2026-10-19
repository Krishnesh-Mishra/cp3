// =============================================================================
// src/codegen/src/support/scope_env.cpp - the Generator's declaration-type
// environment (split out of support/type_map.cpp for the file budget).
//
// The environment is a stack of scopes (name -> cp3 type), pushed when a
// function/closure body opens and popped when it closes. `effectiveType`
// consults it before the sema side tables, so a lowering that declares a
// parameter with a type sema cannot see (the concurrency closures, spec 13)
// still resolves its reads.
// =============================================================================
#include "detail/generator.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cp3 {

// -----------------------------------------------------------------------------
// Generator: declaration-type environment
// -----------------------------------------------------------------------------

void Generator::pushScope() { env_.emplace_back(); }

void Generator::popScope() {
    if (!env_.empty()) {
        env_.pop_back();
    }
}

void Generator::declare(const std::string& name, const std::optional<Type>& type) {
    if (!type) return;
    if (env_.empty()) env_.emplace_back();
    env_.back()[name] = *type;
}

std::optional<Type> Generator::lookup(const std::string& name) const {
    for (auto it = env_.rbegin(); it != env_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

}  // namespace cp3
