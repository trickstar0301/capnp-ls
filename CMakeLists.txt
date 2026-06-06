cmake_minimum_required(VERSION 3.16)
project("capnp-ls" CXX)

message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "C++ Compiler ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "C++ Compiler version: ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ flags: ${CMAKE_CXX_FLAGS}")

if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(exe_name capnp-ls)

option(CAPNP_LS_BUILD_TESTS "Build capnp-ls tests" ON)
set(CAPNP_SOURCE_DIR "" CACHE PATH "Path to Cap'n Proto C++ source directory containing src/capnp/compiler")

if(CAPNP_SOURCE_DIR)
    get_filename_component(CAPNP_SOURCE_DIR "${CAPNP_SOURCE_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set(CAPNP_SOURCE_DIR "${CAPNP_SOURCE_DIR}" CACHE PATH "Path to Cap'n Proto C++ source directory containing src/capnp/compiler" FORCE)
endif()

add_executable(${exe_name}
    src/main.cpp
    src/stdin_reader.cpp
    src/stdout_writer.cpp
    src/lsp_message_handler.cpp
    src/lsp_types.cpp
    src/utils.cpp
    src/linked_compiler.cpp
    src/compilation_manager.cpp
    src/symbol_resolver.cpp
)

if(NOT EXISTS "${CAPNP_SOURCE_DIR}/src/capnp/compiler/compiler.h")
    message(FATAL_ERROR "CAPNP_SOURCE_DIR must point to a Cap'n Proto C++ source directory with src/capnp/compiler/compiler.h")
endif()

set(BUILD_TESTING OFF CACHE BOOL "Disable Cap'n Proto subproject tests" FORCE)
add_subdirectory("${CAPNP_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/capnproto-build" EXCLUDE_FROM_ALL)

target_sources(${exe_name} PRIVATE
    "${CAPNP_SOURCE_DIR}/src/capnp/compiler/module-loader.c++"
)
target_include_directories(${exe_name} BEFORE PRIVATE ${CAPNP_SOURCE_DIR}/src)
target_compile_definitions(${exe_name} PRIVATE
    CAPNP_LS_CAPNP_SOURCE_DIR="${CAPNP_SOURCE_DIR}"
)
target_link_libraries(${exe_name} PRIVATE
    capnp-rpc
    capnp-json
    capnpc
)

if(CAPNP_LS_BUILD_TESTS)
    enable_testing()
    add_executable(capnp-ls-linked-compiler-test
        tests/linked_compiler_test.cpp
        src/linked_compiler.cpp
        src/symbol_resolver.cpp
    )
    target_sources(capnp-ls-linked-compiler-test PRIVATE
        "${CAPNP_SOURCE_DIR}/src/capnp/compiler/module-loader.c++"
    )
    target_include_directories(capnp-ls-linked-compiler-test BEFORE PRIVATE
        ${CAPNP_SOURCE_DIR}/src
    )
    target_link_libraries(capnp-ls-linked-compiler-test PRIVATE
        capnpc
        capnp
        kj-async
        kj
    )
    target_include_directories(capnp-ls-linked-compiler-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(capnp-ls-linked-compiler-test PRIVATE
        CAPNP_LS_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples/client/testFixture"
        CAPNP_LS_CAPNP_SOURCE_DIR="${CAPNP_SOURCE_DIR}"
    )
    add_test(NAME linked-compiler COMMAND capnp-ls-linked-compiler-test)
endif()
