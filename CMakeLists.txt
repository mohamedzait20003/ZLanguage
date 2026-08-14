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
    Src/Sema.cpp
    Src/CodeGen.cpp
)

target_include_directories(zc PRIVATE Include ${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_options(zc PRIVATE ${LLVM_DEFINITIONS_LIST})

if(LLVM_LINK_LLVM_DYLIB AND TARGET LLVM)
    message(STATUS "Linking against shared LLVM")
    target_link_libraries(zc PRIVATE LLVM)
    file(WRITE ${CMAKE_BINARY_DIR}/llvm_bin_dir.txt "${LLVM_TOOLS_BINARY_DIR}")
else()
    message(STATUS "Linking against static LLVM component libraries")
    llvm_map_components_to_libnames(llvm_libs
        core support irreader
        codegen mc mcparser target
        x86codegen x86info x86desc x86asmparser
        passes
    )
    target_link_libraries(zc PRIVATE ${llvm_libs})
endif()

# ---------------------------------------------------------------------------
# MLIR tensor backend
# ---------------------------------------------------------------------------
option(Z_ENABLE_MLIR "Build the MLIR tensor backend (M17a+)" OFF)

if(Z_ENABLE_MLIR)
    find_package(MLIR REQUIRED CONFIG)
    message(STATUS "Found MLIR at ${MLIR_DIR}")

    list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}" "${LLVM_CMAKE_DIR}")
    include(TableGen)
    include(AddLLVM)
    include(AddMLIR)

    add_subdirectory(Runtime/MLIR)

    target_link_libraries(zc PRIVATE ZMLIR)
    target_compile_definitions(zc PRIVATE Z_ENABLE_MLIR=1)
endif()

# `cmake --build build --target check` runs the whole suite.
# Test/run_tests.sh can also be invoked directly, optionally with a name filter.
find_program(BASH_PROGRAM bash)
if(BASH_PROGRAM)
    add_custom_target(check
        COMMAND ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/Test/run_tests.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS zc
        USES_TERMINAL
        COMMENT "Running Z test suite"
    )
else()
    message(STATUS "bash not found — 'check' target unavailable")
endif()
