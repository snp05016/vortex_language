// vector.contract is the vector dialect's own matrix multiply: the same
// (m, n, k) index structure as linalg.matmul, but over values that are
// already loaded into registers rather than over a memref or a tensor.
func.func @matmul2x2(%a: vector<2x2xf32>, %b: vector<2x2xf32>, %c: vector<2x2xf32>) -> vector<2x2xf32> {
  %0 = vector.contract {
    indexing_maps = [
      affine_map<(m, n, k) -> (m, k)>,
      affine_map<(m, n, k) -> (k, n)>,
      affine_map<(m, n, k) -> (m, n)>
    ],
    iterator_types = ["parallel", "parallel", "reduction"]
  } %a, %b, %c : vector<2x2xf32>, vector<2x2xf32> into vector<2x2xf32>
  return %0 : vector<2x2xf32>
}
