// A 4x8 tile of f32 is one SSA value in the vector dialect, but LLVM IR has
// only 1-D vectors. Lowering to the LLVM dialect keeps the leading
// dimension as an array and the trailing one as a vector: four rows of
// vector<8xf32>, and one llvm.fadd per row. The 2-D add is unrolled along
// the rows, and no row's lanes mix with another row's.
func.func @twice(%tile: vector<4x8xf32>) -> vector<4x8xf32> {
  %sum = arith.addf %tile, %tile : vector<4x8xf32>
  return %sum : vector<4x8xf32>
}
