# =============================================================================
# cp3_lexer - .cp3 source text -> Token stream.
#
# OWNERSHIP: lexer owner. Keep the frozen `Lexer` boundary declared in
# `include/cp3/lexer.hpp`.
#
# ADDING A SOURCE FILE: append one line to `cp3_lexer_sources` below (STYLE.md §8
# lists the target decomposition: lexer.cpp, scanner.cpp, numbers.cpp,
# strings.cpp, templates.cpp, regexp.cpp, keywords.cpp, ...). Private headers
# under `src/` are never included by another module (STYLE.md §3).
# =============================================================================

set(cp3_lexer_sources
    src/lexer.cpp
    src/scanner.cpp
    src/numbers.cpp
    src/strings.cpp
    src/templates.cpp
    src/regexp.cpp
    src/keywords.cpp
)

add_library(cp3_lexer STATIC ${cp3_lexer_sources})
target_include_directories(cp3_lexer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(cp3_lexer PUBLIC cp3_common)
cp3_set_warnings(cp3_lexer)
