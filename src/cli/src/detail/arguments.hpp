// =============================================================================
// src/cli/src/detail/arguments.hpp - command-line option parsing DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/commands/arguments.cpp. The command dispatcher
// (src/cli.cpp) owns the command surface; this unit owns option parsing.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "detail/commands.hpp"

namespace cp3::cli {

// Parses the tokens following the command word into `opt`. Returns an empty
// string on success or an error message on a usage problem.
std::string parseOptions(const std::string& command,
                         const std::vector<std::string>& tokens,
                         CliOptions& opt);

}  // namespace cp3::cli
