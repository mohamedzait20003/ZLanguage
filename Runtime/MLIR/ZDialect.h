//===- ZDialect.h - The Z dialect --------------------------------*- C++ -*-===//
//
// Public entry point for the `z` dialect. The declarations themselves are
// generated from ZOps.td by mlir-tblgen; this header only pulls the generated
// pieces together in the right order.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"

// Every TableGen trait needs its C++ counterpart included here, or the generated
// op class inherits from an undeclared base and the failure cascades into dozens
// of unrelated-looking errors inside ZOps.h.inc.
//   Pure                      -> SideEffectInterfaces.h
//   SameOperandsAndResultType -> InferTypeOpInterface.h
#include "mlir/Interfaces/InferTypeOpInterface.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

// Dialect class declaration (ZDialect.h.inc) must come before the op
// declarations, which reference it.
#include "Runtime/MLIR/ZOpsDialect.h.inc"

#define GET_OP_CLASSES
#include "Runtime/MLIR/ZOps.h.inc"
