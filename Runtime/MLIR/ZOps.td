//===- ZOps.td - The Z dialect ----------------------------*- tablegen -*-===//
//
// The `z` dialect holds Z-level tensor semantics that have no faithful `linalg`
// spelling *at the point of emission*. It is deliberately tiny.
//
// Rule for adding an op: an operation earns a place here only if lowering it
// eagerly to `linalg` while emitting would lose information a later pass needs.
// Everything else is emitted as `linalg`/`arith`/`tensor` directly. A bloated
// custom dialect is how the tensor-lowering decision goes wrong.
//
// M17a defines exactly one op — `z.add` — because this milestone's real content
// is build integration and the LLVM-IR <-> MLIR handoff, not tensor semantics.
//
//===----------------------------------------------------------------------===//

#ifndef Z_OPS
#define Z_OPS

include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"   // Pure
include "mlir/Interfaces/InferTypeOpInterface.td"   // SameOperandsAndResultType

//===----------------------------------------------------------------------===//
// Dialect
//===----------------------------------------------------------------------===//

def Z_Dialect : Dialect {
  let name = "z";
  let summary = "High-level Z tensor operations, lowered to linalg";
  let cppNamespace = "::mlir::z";
}

//===----------------------------------------------------------------------===//
// Base class
//===----------------------------------------------------------------------===//

class Z_Op<string mnemonic, list<Trait> traits = []>
    : Op<Z_Dialect, mnemonic, traits>;

//===----------------------------------------------------------------------===//
// Operations
//===----------------------------------------------------------------------===//

def Z_AddOp : Z_Op<"add", [Pure, SameOperandsAndResultType]> {
  let summary = "Elementwise tensor addition";
  let description = [{
    Adds two tensors of identical shape and element type, elementwise.

    Broadcasting is deliberately *not* part of this op in M17a. The broadcasting
    form arrives in M17b as `z.broadcast_binop`, which carries the operand shapes
    until the lowering pass can build the `linalg.generic` indexing maps.

    Lowered by ZToLinalg to a `linalg.generic` with identity indexing maps and an
    `arith.addf` / `arith.addi` body chosen by element type.

    Example:
    ```mlir
    %r = z.add %a, %b : tensor<4xf32>
    ```
  }];

  let arguments = (ins AnyRankedTensor:$lhs, AnyRankedTensor:$rhs);
  let results   = (outs AnyRankedTensor:$result);

  let assemblyFormat = "$lhs `,` $rhs attr-dict `:` type($result)";
}

#endif // Z_OPS
