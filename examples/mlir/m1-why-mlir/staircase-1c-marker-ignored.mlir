// Same op as linalg-1, with the marker changed from "parallel" to
// "reduction". The verifier still accepts it, and this particular
// lowering step produces the identical affine loop either way: the
// marker was real information, but this step never reads it.
// Run: mlir-opt --convert-linalg-to-affine-loops
func.func @scale_add(%a: memref<4xf32>, %b: memref<4xf32>, %out: memref<4xf32>) {
  linalg.generic {
    indexing_maps = [affine_map<(i) -> (i)>, affine_map<(i) -> (i)>, affine_map<(i) -> (i)>],
    iterator_types = ["reduction"]
  } ins(%a, %b : memref<4xf32>, memref<4xf32>) outs(%out : memref<4xf32>) {
  ^bb0(%x: f32, %y: f32, %z: f32):
    %sum = arith.addf %x, %y : f32
    linalg.yield %sum : f32
  }
  return
}
