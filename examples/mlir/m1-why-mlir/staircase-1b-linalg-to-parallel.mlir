// Same starting point as linalg-1, one different choice of step: this pass
// reads the iterator_types marker and keeps the "may run in any order" fact
// instead of dropping it, producing scf.parallel straight away.
// Run: mlir-opt --convert-linalg-to-parallel-loops
func.func @scale_add(%a: memref<4xf32>, %b: memref<4xf32>, %out: memref<4xf32>) {
  linalg.generic {
    indexing_maps = [affine_map<(i) -> (i)>, affine_map<(i) -> (i)>, affine_map<(i) -> (i)>],
    iterator_types = ["parallel"]
  } ins(%a, %b : memref<4xf32>, memref<4xf32>) outs(%out : memref<4xf32>) {
  ^bb0(%x: f32, %y: f32, %z: f32):
    %sum = arith.addf %x, %y : f32
    linalg.yield %sum : f32
  }
  return
}
