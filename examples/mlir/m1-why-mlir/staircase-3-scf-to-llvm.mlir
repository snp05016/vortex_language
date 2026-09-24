// One step further: structured loops are gone, replaced by blocks and a
// branch; memref values carry a descriptor struct (pointer, offset, sizes).
// Run: mlir-opt --convert-scf-to-cf --convert-arith-to-llvm
//              --finalize-memref-to-llvm --convert-func-to-llvm
//              --reconcile-unrealized-casts
module {
  func.func @scale_add(%arg0: memref<4xf32>, %arg1: memref<4xf32>, %arg2: memref<4xf32>) {
    %c0 = arith.constant 0 : index
    %c4 = arith.constant 4 : index
    %c1 = arith.constant 1 : index
    scf.for %arg3 = %c0 to %c4 step %c1 {
      %0 = memref.load %arg0[%arg3] : memref<4xf32>
      %1 = memref.load %arg1[%arg3] : memref<4xf32>
      %2 = arith.addf %0, %1 : f32
      memref.store %2, %arg2[%arg3] : memref<4xf32>
    }
    return
  }
}
