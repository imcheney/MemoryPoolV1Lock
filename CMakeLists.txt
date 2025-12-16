cmake_minimum_required(VERSION 3.15)
project(MemoryPoolAlpha LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(memorypool STATIC
    MemoryPool.cpp
)

target_include_directories(memorypool
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(memorypool
    PUBLIC cxx_std_17
)

option(MEMORYPOOL_BUILD_EXAMPLE "Build example executable" OFF)
option(MEMORYPOOL_BUILD_TESTS "Build test executables" ON)
option(MEMORYPOOL_BUILD_BENCHMARKS "Build benchmark executables" ON)

if(MEMORYPOOL_BUILD_EXAMPLE)
    add_executable(memorypool_example
        example.cpp
    )
    target_link_libraries(memorypool_example PRIVATE memorypool)
endif()

if(MEMORYPOOL_BUILD_TESTS)
    add_executable(memorypool_unit_tests
        tests/unit_tests.cpp
    )
    target_link_libraries(memorypool_unit_tests PRIVATE memorypool)
    target_compile_features(memorypool_unit_tests PRIVATE cxx_std_17)

    add_executable(memorypool_concurrency_smoke
        tests/concurrency_smoke.cpp
    )
    target_link_libraries(memorypool_concurrency_smoke PRIVATE memorypool)
    target_compile_features(memorypool_concurrency_smoke PRIVATE cxx_std_17)

    add_executable(memorypool_concurrency_lockfree
        tests/concurrency_lockfree.cpp
    )
    target_link_libraries(memorypool_concurrency_lockfree PRIVATE memorypool)
    target_compile_features(memorypool_concurrency_lockfree PRIVATE cxx_std_17)
endif()

if(MEMORYPOOL_BUILD_BENCHMARKS)
    add_executable(memorypool_benchmark
        benchmarks/pool_benchmark.cpp
    )
    target_link_libraries(memorypool_benchmark PRIVATE memorypool)
    target_compile_features(memorypool_benchmark PRIVATE cxx_std_17)
endif()
