//===- ZDialect.cpp - The Z dialect ------------------------------*//
//
// Glue for the TableGen-generated dialect and op definitions. Custom verifiers
// and folders for `z` ops belong here as the dialect grows past M17a.
//
//===----------------------------------------------------------------------===//

#include "Runtime/MLIR/ZDialect.h"

#include "mlir/IR/Builders.h"

using namespace mlir;
using namespace mlir::z;

#include "Runtime/MLIR/ZOpsDialect.cpp.inc"

#define GET_OP_CLASSES
#include "Runtime/MLIR/ZOps.cpp.inc"

void ZDialect::initialize() {
    addOperations<
#define GET_OP_LIST
#include "Runtime/MLIR/ZOps.cpp.inc"
        >();
}
