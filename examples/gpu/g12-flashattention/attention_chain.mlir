// Follows: MLIR Linalg dialect, linalg.softmax, and the transform dialect's
// transform.structured.decompose_interface (LLVM 18). Attention for four
// queries and four keys with head dimension 2, written as three named ops;
// the schedule then splits softmax into the loop nests a back end would run.

func.func @attention(%q: tensor<4x2xf32>, %k: tensor<4x2xf32>,
                     %v: tensor<4x2xf32>) -> tensor<4x2xf32> {
  %zero = arith.constant 0.0 : f32
  %s_empty = tensor.empty() : tensor<4x4xf32>
  %s_init = linalg.fill ins(%zero : f32) outs(%s_empty : tensor<4x4xf32>) -> tensor<4x4xf32>
  // S = Q K^T: one score per (query, key) pair.
  %s = linalg.matmul_transpose_b ins(%q, %k : tensor<4x2xf32>, tensor<4x2xf32>)
                                 outs(%s_init : tensor<4x4xf32>) -> tensor<4x4xf32>
  // P = softmax of each row of S.
  %p_empty = tensor.empty() : tensor<4x4xf32>
  %p = linalg.softmax dimension(1) ins(%s : tensor<4x4xf32>)
                                   outs(%p_empty : tensor<4x4xf32>) -> tensor<4x4xf32>
  // O = P V.
  %o_empty = tensor.empty() : tensor<4x2xf32>
  %o_init = linalg.fill ins(%zero : f32) outs(%o_empty : tensor<4x2xf32>) -> tensor<4x2xf32>
  %o = linalg.matmul ins(%p, %v : tensor<4x4xf32>, tensor<4x2xf32>)
                     outs(%o_init : tensor<4x2xf32>) -> tensor<4x2xf32>
  return %o : tensor<4x2xf32>
}

module attributes {transform.with_named_sequence} {
  transform.named_sequence @__transform_main(%root: !transform.any_op {transform.readonly}) {
    %softmax = transform.structured.match ops{["linalg.softmax"]} in %root
      : (!transform.any_op) -> !transform.any_op
    %parts = transform.structured.decompose_interface %softmax
      : (!transform.any_op) -> !transform.any_op
    transform.yield
  }
}
