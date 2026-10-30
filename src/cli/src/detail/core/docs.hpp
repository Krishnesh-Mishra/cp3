#pragma once

#include <string>

#include "cp3/diag/diagnostic.hpp"

namespace cp3::cli {

// Writes a self-contained, per-diagnostic help page to the operating system's
// temporary directory and returns its file URL. An empty result means that the
// page could not be written.
std::string writeDiagnosticHelpPage(const Diagnostic& diagnostic);

}  // namespace cp3::cli
