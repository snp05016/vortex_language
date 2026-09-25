// Follows: MLIR 'linalg' dialect, "Named Payload-Carrying Ops", and the
// -linalg-generalize-named-ops pass in MLIR's Passes list.
// The same 4x3 by 3x5 product twice: once spelled out as linalg.generic,
// once as the named op. Generalizing the named op should give the first.
#map_a = affine_map<(i, j, k) -> (i, k)>
#map_b = affine_map<(i, j, k) -> (k, j)>
#map_c = affine_map<(i, j, k) -> (i, j)>

func.func @spelled_out(%a: memref<4x3xf32>, %b: memref<3x5xf32>, %c: memref<4x5xf32>) {
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

func.func @named(%a: memref<4x3xf32>, %b: memref<3x5xf32>, %c: memref<4x5xf32>) {
  linalg.matmul ins(%a, %b : memref<4x3xf32>, memref<3x5xf32>) outs(%c : memref<4x5xf32>)
  return
}
