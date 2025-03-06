cmake_minimum_required(VERSION 3.10)
project("capnp-ls" CXX)

set(CMAKE_CXX_STANDARD 17)
set(exe_name capnp-ls)

find_package(CapnProto REQUIRED)

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

target_link_directories(${exe_name} PRIVATE /usr/local/lib)

target_link_libraries(${exe_name} PRIVATE
	CapnProto::capnp-rpc
	CapnProto::capnp-json
	kj
	kj-async
)