# =============================================================================
# cp3_sema — AST -> diagnostics / resolved semantics.
#
# OWNERSHIP: sema owner. Keep the frozen `SemanticAnalyzer` boundary declared in
# `include/cp3/sema.hpp`.
#
# ADDING A SOURCE FILE: append one line to `cp3_sema_sources` below (STYLE.md §8
# lists the target decomposition: semantic_analyzer.cpp, scopes.cpp,
# declarations.cpp, type_check.cpp, expr_check.cpp, stmt_check.cpp, symbols.cpp,
# banned.cpp; the expression/assignability concerns are split further to stay
# within the 500-line hard budget: member_check.cpp, ops_check.cpp,
# assignability.cpp).
# =============================================================================

set(cp3_sema_sources
    src/semantic_analyzer.cpp
    src/scopes.cpp
    src/declarations.cpp
    src/type_check.cpp
    src/assignability.cpp
    src/expr_check.cpp
    src/member_check.cpp
    src/ops_check.cpp
    src/stmt_check.cpp
    src/symbols.cpp
    src/banned.cpp
    src/parallel_check.cpp
)

add_library(cp3_sema STATIC ${cp3_sema_sources})
target_include_directories(cp3_sema PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(cp3_sema PUBLIC cp3_common cp3_parser)
cp3_set_warnings(cp3_sema)
