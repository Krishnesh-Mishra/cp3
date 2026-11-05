// =============================================================================
// src/std/readline/runtime.cpp - `std/readline` anchor TU (ledger L).
//
// OWNERSHIP: the std/readline family (src/std/readline/**). Compiled as a
// runtime TU by the readline native row whenever a program imports
// "std/readline" (src/std/readline/native.hpp).
//
// The whole surface is header-only delegation onto the process stdin pump
// (cp3/std/readline.hpp); this TU exists as the row's own anchor so the
// family has a compiled home for any future non-inline member, and so the
// header is instantiated in a real translation unit. The stdin pump itself
// lives in src/std/process/runtime.cpp, listed on the same native row.
// =============================================================================
#include "cp3/std/readline.hpp"
