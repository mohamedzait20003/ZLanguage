cmake_minimum_required(VERSION 3.20)
project(ZCompiler LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM include dirs: ${LLVM_INCLUDE_DIRS}")

add_executable(zc
    Src/main.cpp
    Src/Lexer.cpp
    Src/Parser.cpp
    Src/CodeGen.cpp
)

target_include_directories(zc PRIVATE Include ${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_options(zc PRIVATE ${LLVM_DEFINITIONS_LIST})

llvm_map_components_to_libnames(llvm_libs
    core support irreader
    codegen mc mcparser target
    x86codegen x86info x86desc x86asmparser
    passes
)
target_link_libraries(zc PRIVATE ${llvm_libs})
