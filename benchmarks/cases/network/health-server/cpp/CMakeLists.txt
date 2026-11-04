cmake_minimum_required(VERSION 3.16)

project(health-server-drogon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon CONFIG REQUIRED)

add_executable(health-server-drogon drogon_main.cpp)
target_link_libraries(health-server-drogon PRIVATE Drogon::Drogon)
