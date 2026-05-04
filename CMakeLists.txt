cmake_minimum_required(VERSION 4.3.1)

project (ITCH_Viewer
         VERSION 0.2.0
         )

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PROFILING OFF)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/OrderBook.cpp
    src/clickhouse/ClickhouseOrderBook.cpp
    )

add_subdirectory(contribs/clickhouse-cpp)



target_compile_options(${PROJECT_NAME}
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    )

if (PROFILING)
    target_compile_definitions(${PROJECT_NAME} PUBLIC PROF)
endif()

target_include_directories(${PROJECT_NAME}
    SYSTEM PRIVATE
    contribs/clickhouse-cpp
    contribs/clickhouse-cpp/contrib/absl
    )

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    dl
    clickhouse-cpp-lib
    )
