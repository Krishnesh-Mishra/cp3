# =============================================================================
# cp3_cli - command surface (init/run/check/dev/build/fmt/clean/exec/version).
#
# OWNERSHIP: CLI/pipeline owner. All rendering of diagnostics happens here, and
# the exit-code contract (ARCHITECTURE.md §4) lives here.
#
# ADDING A SOURCE FILE: append one line to `cp3_cli_sources` below. The units
# live under `src/{commands,pkg,archive,toolchain}/`; private headers live in
# `src/detail/` (STYLE.md §8) - headers declare, `.cpp` defines.
# =============================================================================

set(cp3_cli_sources
    src/cli.cpp
    src/toolchain/util.cpp
    src/toolchain/process.cpp
    src/toolchain/worker_module.cpp
    src/toolchain/worker_sites.cpp
    src/pkg/cache.cpp
    src/detail/native_packages.cpp
    src/toolchain/toolchain.cpp
    src/toolchain/toolchain_probe.cpp
    src/toolchain/toolchain_target.cpp
    src/toolchain/toolchain_versions.cpp
    src/toolchain/toolchain_available.cpp
    src/toolchain/toolchain_config.cpp
    src/toolchain/toolchain_manifest.cpp
    src/toolchain/toolchain_select.cpp
    src/toolchain/toolchain_menu.cpp
    src/toolchain/toolchain_wizard.cpp
    src/toolchain/toolchain_install.cpp
    src/toolchain/diagnostics.cpp
    src/toolchain/pipeline.cpp
    src/commands/common.cpp
    src/commands/arguments.cpp
    src/commands/check.cpp
    src/commands/lsp.cpp
    src/commands/lsp_intel.cpp
    src/commands/lsp_intel_docs.cpp
    src/commands/lsp_intel_query.cpp
    src/commands/lsp_intel_define.cpp
    src/commands/lsp_intel_complete.cpp
    src/commands/lsp_intel_members.cpp
    src/commands/lsp_intel_names.cpp
    src/commands/lsp_intel_position.cpp
    src/commands/lsp_intel_scope.cpp
    src/commands/lsp_intel_walk.cpp
    src/commands/lsp_intel_walk_children.cpp
    src/commands/dap.cpp
    src/commands/run.cpp
    src/commands/build.cpp
    src/commands/dev.cpp
    src/commands/fmt.cpp
    src/format/format.cpp
    src/commands/clean.cpp
    src/commands/native_install.cpp
    src/commands/native_install_support.cpp
    src/commands/init.cpp
    src/commands/default_entry.cpp
    src/commands/toolchain.cpp
    src/commands/toolchain_json.cpp
    src/commands/exec.cpp
    src/commands/pack.cpp
    src/commands/recompile.cpp
    src/commands/install.cpp
    src/commands/publish.cpp
    src/commands/link.cpp
    src/commands/editor_plugin.cpp
    src/commands/editor_actions.cpp
    src/commands/editor_install.cpp
    src/commands/editor_detect.cpp
    src/commands/editor_vsix.cpp
    src/commands/editor_process.cpp
    src/commands/editor_prompt.cpp
    src/commands/editor_wizard.cpp
    src/pkg/package_json.cpp
    src/pkg/package_cache.cpp
    src/pkg/platform.cpp
    src/pkg/cache_binary.cpp
    src/pkg/cache_build.cpp
    src/pkg/signatures.cpp
    src/pkg/lockfile.cpp
    src/pkg/install.cpp
    src/pkg/link.cpp
    src/pkg/github.cpp
    src/pkg/github_install.cpp
    src/pkg/scripts.cpp
    src/pkg/json.cpp
    src/pkg/checksum.cpp
    src/archive/inflate.cpp
    src/archive/deflate.cpp
    src/archive/gzip.cpp
    src/archive/tar.cpp
    src/pkg/cp3pak.cpp
    src/pkg/semver.cpp
    src/native/libclang.cpp
    src/native/bindgen.cpp
    src/native/bindgen_members.cpp
    src/native/bindgen_types.cpp
    src/native/bindgen_index.cpp
    src/native/metadata.cpp
    src/native/sdk.cpp
    src/native/sdk_libs.cpp
)

add_library(cp3_cli STATIC ${cp3_cli_sources})
target_include_directories(cp3_cli PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Private headers resolve as `detail/...` from every unit subdirectory.
target_include_directories(cp3_cli PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# -----------------------------------------------------------------------------
# libclang C API headers for `native-install`.
#
# Only the declarations are needed at build time: libclang itself is loaded
# dynamically at runtime from the user's LLVM installation (CP3_LIBCLANG may
# override the search), so building cp3c never requires LLVM to be present.
#
# The public clang-c headers are vendored under third_party/clang-c/include
# (Apache-2.0 WITH LLVM-exception, pinned in third_party/clang-c/VERSION.txt),
# which keeps `cmake -S . -B build` working on a machine without LLVM. Point
# -DCP3_LIBCLANG_INCLUDE_DIR=<dir> at a system LLVM include directory to build
# against that copy instead.
# -----------------------------------------------------------------------------
if(NOT CP3_LIBCLANG_INCLUDE_DIR)
    set(CP3_LIBCLANG_INCLUDE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/clang-c/include")
endif()
if(NOT EXISTS "${CP3_LIBCLANG_INCLUDE_DIR}/clang-c/Index.h")
    message(FATAL_ERROR
        "clang-c/Index.h was not found under '${CP3_LIBCLANG_INCLUDE_DIR}'. "
        "The vendored copy in third_party/clang-c/include is missing, or "
        "-DCP3_LIBCLANG_INCLUDE_DIR points at a directory without the libclang "
        "C API headers (for example 'C:/Program Files/LLVM/include').")
endif()
target_include_directories(cp3_cli PRIVATE "${CP3_LIBCLANG_INCLUDE_DIR}")

# -----------------------------------------------------------------------------
# cp3c lsp - the language server module (src/lsp/**, PKG-CONTRACTS sec.2/sec.5).
#
# The module keeps its own directory, public header and private detail headers,
# but its translation units are compiled into cp3_cli: the contract says ONE
# product binary (`cp3c lsp`, never a second executable) and src/CMakeLists.txt
# (where a new module target would be registered) is outside the LSP
# workstream's file scope. The include dir makes `cp3/lsp.hpp` resolvable from
# both src/lsp and src/cli.
# -----------------------------------------------------------------------------
set(cp3_lsp_dir "${CMAKE_CURRENT_SOURCE_DIR}/../lsp")
target_sources(cp3_cli PRIVATE
    "${cp3_lsp_dir}/src/json.cpp"
    "${cp3_lsp_dir}/src/framing.cpp"
    "${cp3_lsp_dir}/src/text.cpp"
    "${cp3_lsp_dir}/src/protocol.cpp"
    "${cp3_lsp_dir}/src/server.cpp"
    "${cp3_lsp_dir}/src/intel.cpp"
)
# src/lsp/src resolves the module's `detail/...` private headers. It is
# appended AFTER src/cli/src so CLI units keep resolving their own `detail/...`
# headers first; the two sets collide only if an LSP header were named exactly
# like a CLI one, which the lsp_*/no-overlap names deliberately prevent.
target_include_directories(cp3_cli PRIVATE "${cp3_lsp_dir}/include"
                                           "${cp3_lsp_dir}/src")

# -----------------------------------------------------------------------------
# cp3c dap - the debug adapter module (src/dap/**, cp3/dap.hpp).
#
# Same shape as the LSP module above, for the same reason (ONE product binary:
# `cp3c dap`, never a second executable) and with the same collision rule: the
# module's private headers are named dap_*/mi/process/session, none of which
# exists under src/cli/src or src/lsp/src.
# -----------------------------------------------------------------------------
set(cp3_dap_dir "${CMAKE_CURRENT_SOURCE_DIR}/../dap")
target_sources(cp3_cli PRIVATE
    "${cp3_dap_dir}/src/json.cpp"
    "${cp3_dap_dir}/src/framing.cpp"
    "${cp3_dap_dir}/src/mi.cpp"
    "${cp3_dap_dir}/src/process.cpp"
    "${cp3_dap_dir}/src/session.cpp"
    "${cp3_dap_dir}/src/session_queries.cpp"
    "${cp3_dap_dir}/src/server.cpp"
    "${cp3_dap_dir}/src/server_requests.cpp"
)
target_include_directories(cp3_cli PRIVATE "${cp3_dap_dir}/include"
                                           "${cp3_dap_dir}/src")

target_link_libraries(cp3_cli PUBLIC
    cp3_common cp3_lexer cp3_parser cp3_modules cp3_sema cp3_codegen)
cp3_set_warnings(cp3_cli)
