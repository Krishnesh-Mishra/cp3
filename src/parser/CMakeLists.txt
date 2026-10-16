# =============================================================================
# cp3_parser — Token stream -> AST.
#
# OWNERSHIP: parser owner. Keep the frozen `Parser` boundary declared in
# `include/cp3/parser.hpp`.
#
# ADDING A SOURCE FILE: append one line to `cp3_parser_sources` below (STYLE.md §8
# lists the target decomposition: parser.cpp, statements.cpp, expressions.cpp,
# declarations.cpp, types.cpp, patterns.cpp, ...).
# =============================================================================

set(cp3_parser_sources
    src/parser.cpp
    src/tokens.cpp
    src/statements.cpp
    src/declarations.cpp
    src/members.cpp
    src/patterns.cpp
    src/types.cpp
    src/expressions.cpp
    src/primary.cpp
    src/literals.cpp
    src/node_ids.cpp
)

add_library(cp3_parser STATIC ${cp3_parser_sources})
target_include_directories(cp3_parser PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(cp3_parser PUBLIC cp3_common cp3_lexer)
cp3_set_warnings(cp3_parser)
