cmake_minimum_required(VERSION 3.10)
project(ls_hwbp_tool LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(ls-hwbp
    app/src/main/cpp/main.cpp
    app/src/main/cpp/driver.cpp
    app/src/main/cpp/symbol.cpp
    app/src/main/cpp/report.cpp
    app/src/main/cpp/cli.cpp
    app/src/main/cpp/mcp.cpp)
target_compile_options(ls-hwbp PRIVATE -Wall -Wextra -O2)
find_package(Threads REQUIRED)
target_link_libraries(ls-hwbp PRIVATE Threads::Threads)