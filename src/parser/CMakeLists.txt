# =============================================================================
# cp3_parser - Token stream -> AST.
#
# OWNERSHIP: parser owner. Keep the frozen `Parser` boundary declared in
# `include/cp3/parser.hpp`.
#
# ADDING A SOURCE FILE: append one line to `cp3_parser_sources` below (STYLE.md §8
# lists the target decomposition: parser.cpp, statements.cpp, webserverions.cpp,
# declarations.cpp, types.cpp, patterns.cpp, ...).
# =============================================================================

set(cp3_parser_sources
    src/core/parser.cpp
    src/core/tokens.cpp
    src/stmt/statements.cpp
    src/decl/declarations.cpp
    src/decl/members.cpp
    src/decl/patterns.cpp
    src/decl/types.cpp
    src/expr/webserverions.cpp
    src/expr/primary.cpp
    src/expr/literals.cpp
    src/core/node_ids.cpp
)

add_library(cp3_parser STATIC ${cp3_parser_sources})
target_include_directories(cp3_parser PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Private headers resolve as `detail/...` from every unit subdirectory.
target_include_directories(cp3_parser PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(cp3_parser PUBLIC cp3_common cp3_lexer)
cp3_set_warnings(cp3_parser)
