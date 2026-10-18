// =============================================================================
// cp3/std/process.hpp - `process` (std/process) backing: exit + argv/env reads.
//
// OWNERSHIP: the std/process family (src/std/process/**). Additive to the generated-code
// ABI (D14): all functions are `inline`, header-only.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   process.exit(code)        -> `::cp3::runtime::process::exit(code)`
//   process.argv              -> `::cp3::runtime::process::argv()`
//   process.argv.length       -> `argv().size()` (emitted directly by codegen)
//   process.env.get(name)     -> `::cp3::runtime::process::envGet(name)`
//
//   `process.exec`/`process.spawn` (docs §04) are NOT implemented in this wave:
//   they need the async child-process machinery (streams + event loop); the
//   check-mode signature cases stay permissive in sema.
//
// -----------------------------------------------------------------------------
// SEMANTICS
// -----------------------------------------------------------------------------
//   * exit(code): flushes stdout, then terminates the process with `code`
//     (Node `process.exit`, corpus-pinned by process_exit_zero/nonzero and
//     cli/run_exit_code_propagates).
//   * argv() is a minimal program-argument vector: it reports the program name
//     (argv.length >= 1, as in Node). Real argv plumbing lands with the CLI
//     wave that can inject it.
//   * envGet(name) returns `string | undefined`: the environment value or
//     nothing when the variable is unset/empty (process_env_get_unset pins
//     unset === undefined).
// =============================================================================
#pragma once

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace cp3::runtime::process {

inline void exit(double code) {
    // std::exit runs static destructors (which flush std::cout), but be
    // explicit: a generated program may have written through the C streams.
    std::cout.flush();
    std::fflush(nullptr);
    std::exit(static_cast<int>(code));
}

inline std::vector<std::string> argv() { return std::vector<std::string>{"cp3"}; }

inline std::optional<std::string> envGet(const std::string& name) {
    if (const char* value = std::getenv(name.c_str())) {
        if (*value != '\0') return std::string(value);
    }
    return std::nullopt;
}

}  // namespace cp3::runtime::process
