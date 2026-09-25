// Two levels of tiling on one linalg.matmul, applied by the transform
// dialect rather than hand-written as loops. The first tiling carves out
// the block tile (16 x 16 outputs, 8 deep): three scf.for loops around a
// smaller matmul on subviews. The second tiling carves a thread tile
// (4 x 4, 1 deep) out of that smaller matmul: three more scf.for loops
// nested inside the first three, around a tiny 4 x 4 matmul. Both tilings
// are the same operation at a different size, matching the chapter's
// point that block and thread tiling are one idea applied at two memory
// levels. mlir-opt only runs the transform script and prints the result;
// nothing here runs on a GPU.

func.func @matmul(%a: memref<64x64xf32>, %b: memref<64x64xf32>,
                   %c: memref<64x64xf32>) {
  linalg.matmul ins(%a, %b : memref<64x64xf32>, memref<64x64xf32>)
                outs(%c : memref<64x64xf32>)
  return
}

module attributes {transform.with_named_sequence} {
  transform.named_sequence @__transform_main(
      %root: !transform.any_op {transform.readonly}) {
    %matmul = transform.structured.match ops{["linalg.matmul"]} in %root
      : (!transform.any_op) -> !transform.any_op
    %block_tile, %block_loops:3 = transform.structured.tile_using_for
        %matmul [16, 16, 8]
      : (!transform.any_op)
        -> (!transform.any_op, !transform.any_op, !transform.any_op, !transform.any_op)
    %thread_tile, %thread_loops:3 = transform.structured.tile_using_for
        %block_tile [4, 4, 1]
      : (!transform.any_op)
        -> (!transform.any_op, !transform.any_op, !transform.any_op, !transform.any_op)
    transform.yield
  }
}
