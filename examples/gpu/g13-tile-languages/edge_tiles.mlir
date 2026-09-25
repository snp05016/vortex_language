// One whole-array operation, and a separate schedule that tiles it. The
// matmul never names a thread, a tile size or an edge; the schedule names
// the tile sizes (4 x 4 over M and N, 0 = leave K untiled). Because 10 is
// not a multiple of 4, the last tile in each direction is short, and the
// tiled loop computes its size with affine.min: the loop-level form of a
// tile language's mask. mlir-opt runs the schedule, then --canonicalize
// removes dead index arithmetic so the result is readable.

module attributes {transform.with_named_sequence} {
  func.func @edge(%a: memref<10x8xf32>, %b: memref<8x10xf32>,
                  %c: memref<10x10xf32>) {
    linalg.matmul ins(%a, %b : memref<10x8xf32>, memref<8x10xf32>)
                  outs(%c : memref<10x10xf32>)
    return
  }

  // The schedule. Changing [4, 4, 0] changes only this sequence and the
  // loops it produces, never the matmul above.
  transform.named_sequence @__transform_main(%module: !transform.any_op) {
    %mm = transform.structured.match ops{["linalg.matmul"]} in %module
      : (!transform.any_op) -> !transform.any_op
    %tiled, %loops:2 = transform.structured.tile_using_for %mm[4, 4, 0]
      : (!transform.any_op) -> (!transform.any_op, !transform.any_op,
                                 !transform.any_op)
    transform.yield
  }
}
