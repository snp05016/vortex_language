// A 4x8 by 8x4 matrix multiply on buffers, and a schedule that vectorizes
// it. With static shapes the whole operation fits one tile, so the
// vectorizer needs no loop: it reads each operand once, contracts, and
// writes the result back. The patterns the schedule applies after
// vectorizing are what fold the multiply-then-reduce into vector.contract.
func.func @mm(%a: memref<4x8xf32>, %b: memref<8x4xf32>, %c: memref<4x4xf32>) {
  linalg.matmul ins(%a, %b : memref<4x8xf32>, memref<8x4xf32>)
                outs(%c : memref<4x4xf32>)
  return
}

module attributes {transform.with_named_sequence} {
  transform.named_sequence @__transform_main(
      %root: !transform.any_op {transform.readonly}) {
    %mm = transform.structured.match ops{["linalg.matmul"]} in %root
        : (!transform.any_op) -> !transform.any_op
    // The pattern-based variant works on everything inside a function,
    // so the schedule walks up from the matmul to its enclosing func.
    %fn = transform.get_parent_op %mm {op_name = "func.func"}
        : (!transform.any_op) -> !transform.any_op
    %0 = transform.structured.vectorize_children_and_apply_patterns %fn
        : (!transform.any_op) -> !transform.any_op
    transform.yield
  }
}
