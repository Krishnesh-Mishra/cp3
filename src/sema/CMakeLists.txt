# =============================================================================
# cp3_sema - AST -> diagnostics / resolved semantics.
#
# OWNERSHIP: sema owner. Keep the frozen `SemanticAnalyzer` boundary declared in
# `include/cp3/sema.hpp`.
#
# ADDING A SOURCE FILE: append one line to `cp3_sema_sources` below (STYLE.md §8
# lists the target decomposition: semantic_analyzer.cpp plus checks/*.cpp and
# support/*.cpp; private headers live under src/detail/). Since D72 the std
# family slices live under ../std/<family>/ and keep their phase names
# (sema.cpp / sema_methods.cpp).
# =============================================================================

set(cp3_sema_sources
    src/semantic_analyzer.cpp
    src/support/scopes.cpp
    src/support/declarations.cpp
    src/checks/type_check.cpp
    src/checks/assignability.cpp
    src/checks/expr_check.cpp
    src/checks/member_check.cpp
    src/checks/call_check.cpp
    src/checks/ops_check.cpp
    src/checks/stmt_check.cpp
    src/checks/flow_check.cpp
    src/support/symbols.cpp
    src/checks/banned.cpp
    src/checks/parallel_check.cpp
    src/checks/parallel_expr_check.cpp
    src/checks/std_check.cpp
    src/checks/enum_check.cpp
    ../std/crypto/sema.cpp
    ../std/network/sema.cpp
    ../std/fs/sema.cpp
    ../std/typed-arrays/sema.cpp
    ../std/typed-arrays/sema_methods.cpp
    ../std/math/sema.cpp
    ../std/path/sema.cpp
    ../std/os/sema.cpp
    ../std/process/sema.cpp
    ../std/console/sema.cpp
    ../std/json/sema.cpp
    ../std/collections/sema.cpp
    ../std/date/sema.cpp
    ../std/buffer/sema.cpp
)

add_library(cp3_sema STATIC ${cp3_sema_sources})
target_include_directories(cp3_sema PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Private headers resolve as `detail/...` from every unit subdirectory.
target_include_directories(cp3_sema PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(cp3_sema PUBLIC cp3_common cp3_parser)
cp3_set_warnings(cp3_sema)
