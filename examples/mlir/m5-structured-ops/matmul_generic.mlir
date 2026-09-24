// Follows: MLIR linalg dialect (linalg.generic, indexing_maps, iterator_types).
#map_a = affine_map<(i, j, k) -> (i, k)>
#map_b = affine_map<(i, j, k) -> (k, j)>
#map_c = affine_map<(i, j, k) -> (i, j)>

func.func @matmul_generic(%a: memref<4x3xf32>, %b: memref<3x5xf32>, %c: memref<4x5xf32>) {
  linalg.generic {
    indexing_maps = [#map_a, #map_b, #map_c],
    iterator_types = ["parallel", "parallel", "reduction"]
  } ins(%a, %b : memref<4x3xf32>, memref<3x5xf32>) outs(%c : memref<4x5xf32>) {
  ^bb0(%a_elem: f32, %b_elem: f32, %c_elem: f32):
    %prod = arith.mulf %a_elem, %b_elem : f32
    %sum = arith.addf %c_elem, %prod : f32
    linalg.yield %sum : f32
  }
  return
}
