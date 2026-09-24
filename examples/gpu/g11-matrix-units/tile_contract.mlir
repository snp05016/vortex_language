// A compiler that targets a matrix unit needs one IR operation that means
// "multiply this small tile by that small tile and accumulate", so that a
// later lowering can match it to a single hardware instruction (WMMA's
// `mma.sync`, a SIMD-group matrix multiply-accumulate, an MFMA) instead of a
// loop of scalar multiplies and adds. MLIR's vector dialect already has such
// an operation: vector.contract, applied here to 4 x 4 tiles standing in for
// the small fixed shape a real matrix unit accepts. mlir-opt only parses,
// verifies and prints this file; nothing runs.

func.func @tile_multiply_accumulate(%a: vector<4x4xf32>, %b: vector<4x4xf32>, %c: vector<4x4xf32>) -> vector<4x4xf32> {
  %d = vector.contract {
    indexing_maps = [
      affine_map<(i, j, k) -> (i, k)>,
      affine_map<(i, j, k) -> (k, j)>,
      affine_map<(i, j, k) -> (i, j)>
    ],
    iterator_types = ["parallel", "parallel", "reduction"],
    kind = #vector.kind<add>
  } %a, %b, %c : vector<4x4xf32>, vector<4x4xf32> into vector<4x4xf32>
  return %d : vector<4x4xf32>
}
