# The `z` dialect and the lowering pipeline. Built only when Z_ENABLE_MLIR=ON.

# mlir-tblgen generates the dialect and op classes from ZOps.td. The generated
# headers are written under ${CMAKE_CURRENT_BINARY_DIR}, and the include paths
# below let sources refer to them as "Runtime/MLIR/ZOps.h.inc" — matching the
# repo-relative include style used elsewhere.
# tblgen builds its -I flags from the *directory* INCLUDE_DIRECTORIES property,
# not from target_include_directories, so MLIR's include dir has to be added
# here or ZOps.td cannot find mlir/IR/OpBase.td.
include_directories(${MLIR_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})

set(LLVM_TARGET_DEFINITIONS ZOps.td)

mlir_tablegen(ZOps.h.inc -gen-op-decls)
mlir_tablegen(ZOps.cpp.inc -gen-op-defs)
mlir_tablegen(ZOpsDialect.h.inc -gen-dialect-decls -dialect=z)
mlir_tablegen(ZOpsDialect.cpp.inc -gen-dialect-defs -dialect=z)

add_public_tablegen_target(ZDialectIncGen)

add_library(ZMLIR STATIC
    ZDialect.cpp
)

add_dependencies(ZMLIR ZDialectIncGen)

# ${PROJECT_BINARY_DIR} so "Runtime/MLIR/ZOps.h.inc" resolves to the generated file;
# ${PROJECT_SOURCE_DIR} so "Runtime/MLIR/ZDialect.h" resolves to the checked-in one.
target_include_directories(ZMLIR PUBLIC
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_BINARY_DIR}
    ${MLIR_INCLUDE_DIRS}
    ${LLVM_INCLUDE_DIRS}
)

# MLIR requires RTTI to be configured the same way it was built with.
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(ZMLIR PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-rtti>)
endif()

target_link_libraries(ZMLIR PUBLIC
    MLIRIR
    MLIRSupport
    MLIRFuncDialect
    MLIRArithDialect
    MLIRTensorDialect
    MLIRLinalgDialect
)
