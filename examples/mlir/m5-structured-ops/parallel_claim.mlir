// Follows: MLIR 'linalg' dialect, "Property 3" and "Property 4" of
// linalg.generic, and the -convert-linalg-to-parallel-loops pass.
// Two row sums that differ only in iterator_types. The second claims that
// j is parallel, which is false: four steps of a row write the same out[i].
// The verifier accepts both; the lowering believes each claim.
#map_in = affine_map<(i, j) -> (i, j)>
#map_out = affine_map<(i, j) -> (i)>

func.func @honest(%in: memref<3x4xf32>, %out: memref<3xf32>) {
  linalg.generic {
    indexing_maps = [#map_in, #map_out],
    iterator_types = ["parallel", "reduction"]
  } ins(%in : memref<3x4xf32>) outs(%out : memref<3xf32>) {
  ^bb0(%elem: f32, %acc: f32):
    %sum = arith.addf %acc, %elem : f32
    linalg.yield %sum : f32
  }
  return
}

func.func @false_claim(%in: memref<3x4xf32>, %out: memref<3xf32>) {
  linalg.generic {
    indexing_maps = [#map_in, #map_out],
    iterator_types = ["parallel", "parallel"]
  } ins(%in : memref<3x4xf32>) outs(%out : memref<3xf32>) {
  ^bb0(%elem: f32, %acc: f32):
    %sum = arith.addf %acc, %elem : f32
    linalg.yield %sum : f32
  }
  return
}
