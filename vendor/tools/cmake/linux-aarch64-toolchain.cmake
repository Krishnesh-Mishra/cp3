# Cross toolchain: aarch64-linux-gnu on an x86_64 Linux host (WSL2 or native).
# Needs gcc-aarch64-linux-gnu and g++-aarch64-linux-gnu installed.
#
# Usage:
#   cmake -S <src> -B <build> -DCMAKE_TOOLCHAIN_FILE=vendor/tools/cmake/linux-aarch64-toolchain.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_AR /usr/bin/aarch64-linux-gnu-ar)
set(CMAKE_RANLIB /usr/bin/aarch64-linux-gnu-ranlib)

set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
