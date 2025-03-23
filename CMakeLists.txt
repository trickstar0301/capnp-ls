cmake_minimum_required(VERSION 3.10)
project("capnp-ls" CXX)

set(CMAKE_CXX_STANDARD 17)
set(exe_name capnp-ls)

option(USE_BUNDLED_CAPNP_TOOL "Use bundled (self-built) Cap'n Proto tool and library" OFF)

if(USE_BUNDLED_CAPNP_TOOL)
    include(ExternalProject)

    set(CAPNP_VERSION 1.1.0)
    set(CAPNP_INSTALL_DIR ${CMAKE_BINARY_DIR}/capnproto-${CAPNP_VERSION})

    ExternalProject_Add(
        capnproto_external
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        URL "https://capnproto.org/capnproto-c++-${CAPNP_VERSION}.tar.gz"
        CONFIGURE_COMMAND ./configure --prefix=${CAPNP_INSTALL_DIR} --without-fibers
        BUILD_COMMAND make -j6 check
        INSTALL_COMMAND make install
        BUILD_IN_SOURCE 1
    )

    set(CAPNP_INCLUDE_DIR ${CAPNP_INSTALL_DIR}/include)
    set(CAPNP_LIB_DIR ${CAPNP_INSTALL_DIR}/lib)
    set(CAPNP_EXECUTABLE ${CAPNP_INSTALL_DIR}/bin/capnp)

    add_library(capnp STATIC IMPORTED)
    set_target_properties(capnp PROPERTIES
        IMPORTED_LOCATION ${CAPNP_LIB_DIR}/libcapnp.a
    )

    add_library(capnp-rpc STATIC IMPORTED)
    set_target_properties(capnp-rpc PROPERTIES
        IMPORTED_LOCATION ${CAPNP_LIB_DIR}/libcapnp-rpc.a
    )

    add_library(capnp-json STATIC IMPORTED)
    set_target_properties(capnp-json PROPERTIES
        IMPORTED_LOCATION ${CAPNP_LIB_DIR}/libcapnp-json.a
    )

    add_library(kj STATIC IMPORTED)
    set_target_properties(kj PROPERTIES
        IMPORTED_LOCATION ${CAPNP_LIB_DIR}/libkj.a
    )

    add_library(kj-async STATIC IMPORTED)
    set_target_properties(kj-async PROPERTIES
        IMPORTED_LOCATION ${CAPNP_LIB_DIR}/libkj-async.a
    )
endif()

add_executable(${exe_name}
    src/main.cpp
    src/stdin_reader.cpp
    src/stdout_writer.cpp
    src/lsp_message_handler.cpp
    src/lsp_types.cpp
    src/utils.cpp
    src/subprocess_runner.cpp
    src/compilation_manager.cpp
    src/symbol_resolver.cpp
    src/compile_error_parser.cpp
)

if(USE_BUNDLED_CAPNP_TOOL)
    add_dependencies(${exe_name} capnproto_external)

    target_include_directories(${exe_name} PRIVATE ${CAPNP_INCLUDE_DIR})
    target_link_directories(${exe_name} PRIVATE ${CAPNP_LIB_DIR})

    find_package (Threads)
    target_link_libraries(${exe_name} PRIVATE
        capnp-json
        capnp-rpc
        capnp
        kj-async
        kj
        ${CMAKE_THREAD_LIBS_INIT}
    )

    target_compile_definitions(${exe_name} PRIVATE BUNDLED_CAPNP_EXECUTABLE="${CAPNP_EXECUTABLE}")
else()
    find_package(CapnProto REQUIRED)

    target_link_libraries(${exe_name} PRIVATE
        CapnProto::capnp-rpc
        CapnProto::capnp-json
    )
endif()
