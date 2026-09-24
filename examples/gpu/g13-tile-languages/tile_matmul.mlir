// A matmul written as one whole-tile operation, tiled by a schedule that
// runs separately from it. This is the pattern every tile language hides
// behind a block-level `dot`: state the operation on a tile, then let a
// schedule choose the loop nest and the sub-tiles, without touching the
// operation itself. Here the schedule is MLIR's transform dialect; in
// Triton it is the compiler's own tiling pass; in CuTe it is a layout the
// programmer picks from a library. mlir-opt runs the schedule and prints
// the result; nothing is compiled further.

module attributes {transform.with_named_sequence} {
  func.func @tile_matmul(%a: memref<16x8xf32>, %b: memref<8x16xf32>,
                          %c: memref<16x16xf32>) {
    linalg.matmul ins(%a, %b : memref<16x8xf32>, memref<8x16xf32>)
                   outs(%c : memref<16x16xf32>)
    return
  }

  // The schedule: find the matmul, then tile it 4 x 4 x 2 (M x N x K).
  // Changing these three numbers, or adding a second tiling for a warp
  // level between this one and the thread level, changes only this
  // sequence, never the operation it rewrites.
  transform.named_sequence @__transform_main(%module: !transform.any_op) {
    %matmul = transform.structured.match ops{["linalg.matmul"]} in %module
      : (!transform.any_op) -> !transform.any_op
    %tiled, %loops:3 = transform.structured.tile_using_for %matmul[4, 4, 2]
      : (!transform.any_op) -> (!transform.any_op, !transform.any_op,
                                 !transform.any_op, !transform.any_op)
    transform.yield
  }
}
