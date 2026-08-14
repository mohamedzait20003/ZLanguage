# The Z runtime: linked into the programs zc produces, NOT into zc itself.
#
# One entry per package. Adding a runtime package means adding a directory and
# one line here — nothing outside the package needs editing.

add_library(zruntime STATIC
    GC/zgc.c
    String/zstring.c
    Dynamic/zdynamic.c
)

# Packages include each other as "String/zstring.h", so the include root is
# Runtime/ itself and each header stays next to the code implementing it.
target_include_directories(zruntime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(zruntime PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# MLIR tensor backend — a compiler-side package, gated separately.
if(Z_ENABLE_MLIR)
    add_subdirectory(MLIR)
endif()
