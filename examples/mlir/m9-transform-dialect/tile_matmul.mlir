// The schedule (the transform.named_sequence) never names a size or a loop
// bound written in this file's payload function: it finds linalg.matmul by
// op name, then tiles whatever it finds. The same schedule would tile a
// matmul of any shape.
module attributes {transform.with_named_sequence} {
  func.func @mm(%a: memref<64x32xf32>, %b: memref<32x16xf32>, %c: memref<64x16xf32>) {
    linalg.matmul ins(%a, %b : memref<64x32xf32>, memref<32x16xf32>)
                  outs(%c : memref<64x16xf32>)
    return
  }

  transform.named_sequence @__transform_main(%root: !transform.any_op {transform.readonly}) {
    %matmul = transform.structured.match ops{["linalg.matmul"]} in %root
        : (!transform.any_op) -> !transform.any_op
    %tiled, %loops:3 = transform.structured.tile_using_for %matmul [16, 16, 8]
        : (!transform.any_op) -> (!transform.any_op, !transform.any_op, !transform.any_op, !transform.any_op)
    transform.yield
  }
}
