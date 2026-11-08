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
    src/support/docs.cpp
    src/checks/type/type_check.cpp
    src/checks/expr/assignability.cpp
    src/checks/class/class_check.cpp
    src/checks/class/class_docs.cpp
    src/checks/class/class_conform_check.cpp
    src/checks/expr/expr_check.cpp
    src/checks/expr/member_check.cpp
    src/checks/call/call_check.cpp
    src/checks/call/promise_call.cpp
    src/checks/call/generic_check.cpp
    src/checks/expr/ops_check.cpp
    src/checks/type/native_types.cpp
    src/checks/type/native_check.cpp
    src/checks/stmt/stmt_check.cpp
    src/checks/expr/flow_check.cpp
    src/checks/type/regex_check.cpp
    src/support/symbols.cpp
    src/checks/type/banned.cpp
    src/checks/stmt/parallel_check.cpp
    src/checks/stmt/parallel_expr_check.cpp
    src/checks/type/std_check.cpp
    src/checks/type/enum_check.cpp
    ../std/crypto/sema.cpp
    ../std/network/codegen/sema.cpp
    ../std/fs/sema.cpp
    ../std/io/sema.cpp
    ../language/typed-arrays/sema.cpp
    ../language/typed-arrays/sema_index.cpp
    ../language/typed-arrays/sema_methods.cpp
    ../language/arrays/sema_methods.cpp
    ../language/parallel/sema_methods.cpp
    ../language/number/sema.cpp
    ../language/number/sema_docs.cpp
    ../std/math/sema.cpp
    ../std/path/sema.cpp
    ../std/os/sema.cpp
    ../std/process/sema.cpp
    ../std/env/sema.cpp
    ../std/console/sema.cpp
    ../std/json/sema.cpp
    ../std/object/sema.cpp
    ../std/collections/sema.cpp
    ../std/date/sema.cpp
    ../std/buffer/sema.cpp
    ../std/url/sema.cpp
    ../std/time/sema.cpp
    ../std/hash/sema.cpp

    ../std/text/sema.cpp
    ../std/readline/sema.cpp
    ../std/network/webserver/settings/sema.cpp
    ../std/network/webserver/request/sema.cpp
    ../std/network/dns/sema.cpp
    ../language/arrays/sema_docs.cpp
    ../std/string/sema_docs.cpp
)

add_library(cp3_sema STATIC ${cp3_sema_sources})
target_include_directories(cp3_sema PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Private headers resolve as `detail/...` from every unit subdirectory.
target_include_directories(cp3_sema PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(cp3_sema PUBLIC cp3_common cp3_parser)
cp3_set_warnings(cp3_sema)
