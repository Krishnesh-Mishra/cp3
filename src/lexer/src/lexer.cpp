// =============================================================================
// src/lexer/src/lexer.cpp - lexer entry point (frozen `Lexer::lex`).
//
// The only translation unit that knows the frozen public boundary
// (cp3/lexer.hpp). All scanning lives in the internal `Scanner`
// (scanner.h + scanner/numbers/strings/templates/regexp/keywords.cpp); this
// file just constructs one over the file and sink and returns its stream
// (STYLE.md §8: lexer.cpp is entry/driver only).
// =============================================================================
#include "cp3/lexer.hpp"

#include "scanner.h"

namespace cp3 {

TokenStream Lexer::lex() { return Scanner(file_, sink_).run(); }

}  // namespace cp3
