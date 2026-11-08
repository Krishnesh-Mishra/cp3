# Cross toolchain: windows-arm64 (aarch64-w64-mingw32) from x86_64 Windows,
# using llvm-mingw (https://github.com/mstorsjo/llvm-mingw). The default root
# is %USERPROFILE%/tools/llvm-mingw; override with -DCP3_MINGW_ROOT_ARM64=<path>
# or the CP3_MINGW_ROOT_ARM64 environment variable.
#
# The Windows vendored archives are built with -static so cp3c.exe and programs
# built by it carry no third-party MinGW runtime DLLs.
#
# Usage (from the repository root on Windows):
#   cmake -S . -B build/out/windows-arm64/build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
#         -DCMAKE_TOOLCHAIN_FILE=vendor/tools/cmake/windows-aarch64-llvm-mingw.cmake

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT CP3_MINGW_ROOT_ARM64)
    if(DEFINED ENV{CP3_MINGW_ROOT_ARM64})
        set(CP3_MINGW_ROOT_ARM64 "$ENV{CP3_MINGW_ROOT_ARM64}")
    elseif(DEFINED ENV{USERPROFILE})
        set(CP3_MINGW_ROOT_ARM64 "$ENV{USERPROFILE}/tools/llvm-mingw")
    else()
        set(CP3_MINGW_ROOT_ARM64 "C:/llvm-mingw")
    endif()
endif()

# CMake treats backslashes in quoted paths as escapes; always use forward slashes.
file(TO_CMAKE_PATH "${CP3_MINGW_ROOT_ARM64}" CP3_MINGW_ROOT_ARM64)

set(LLVM_MINGW_BIN "${CP3_MINGW_ROOT_ARM64}/bin")
if(NOT EXISTS "${LLVM_MINGW_BIN}/aarch64-w64-mingw32-clang++.exe")
    message(FATAL_ERROR
        "llvm-mingw not found at '${CP3_MINGW_ROOT_ARM64}' "
        "(expected aarch64-w64-mingw32-clang++.exe in bin/).\n"
        "Install llvm-mingw 20261104 (ucrt, x86_64) to %USERPROFILE%/tools/llvm-mingw, or set\n"
        "CP3_MINGW_ROOT_ARM64 to its root. See vendor/tools/check-prerequisites.py.")
endif()

set(CMAKE_C_COMPILER "${LLVM_MINGW_BIN}/aarch64-w64-mingw32-clang.exe")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW_BIN}/aarch64-w64-mingw32-clang++.exe")
set(CMAKE_AR "${LLVM_MINGW_BIN}/aarch64-w64-mingw32-ar.exe")
set(CMAKE_RANLIB "${LLVM_MINGW_BIN}/aarch64-w64-mingw32-ranlib.exe")

# Fully static: no third-party runtime DLLs for the produced compiler.
set(CMAKE_C_FLAGS_INIT "-static")
set(CMAKE_CXX_FLAGS_INIT "-static")

set(CMAKE_FIND_ROOT_PATH "${CP3_MINGW_ROOT_ARM64}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
