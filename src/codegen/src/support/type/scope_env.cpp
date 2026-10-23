// =============================================================================
// src/codegen/src/support/scope_env.cpp - the Generator's declaration-type
// environment (split out of support/type_map.cpp for the file budget).
//
// The environment is a stack of scopes (name -> cp3 binding), pushed when a
// function/closure body opens and popped when it closes. `effectiveType`
// consults it before the sema side tables, so a lowering that declares a
// parameter with a type sema cannot see (the concurrency closures, spec 13)
// still resolves its reads.
//
// A binding records more than its cp3 type: `cell` marks the shared `Cell<T>`
// representation (a captured, reassigned local, detail/captures.h) and
// `global` marks a module-scope binding emitted into the module namespace,
// whose references are qualified by `moduleRef` so a name like `log` cannot
// become ambiguous in `main` (where a `using namespace` is in effect).
// =============================================================================
#include "detail/generator/generator.h"

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

void Generator::declare(const std::string& name, const std::optional<Type>& type,
                        bool cell, bool global) {
    if (!type) return;
    if (env_.empty()) env_.emplace_back();
    env_.back()[name] = detail::Binding{*type, cell, global};
}

std::optional<detail::Binding> Generator::lookup(const std::string& name) const {
    for (auto it = env_.rbegin(); it != env_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

std::string Generator::moduleRef(const std::string& name) const {
    return namesp_ + "::" + detail::mapIdent(name);
}

bool Generator::hasValueBinding(const std::string& name) const {
    return lookup(name).has_value() || moduleValues_.count(name) != 0;
}

}  // namespace cp3
