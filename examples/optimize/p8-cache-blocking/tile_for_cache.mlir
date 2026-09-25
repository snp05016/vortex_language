// MLIR's affine-loop-tile pass, left to choose tile sizes from a cache
// budget (cache-size, in KiB) instead of being given them. The same
// matrix multiplication at two sizes, tiled for the same 32 KiB cache.
// Read the step of each outer loop: that is the tile size it chose.
//
// Follows: the Affine dialect's pass documentation, -affine-loop-tile, and
// getTileSizes in mlir/lib/Dialect/Affine/Transforms/LoopTiling.cpp
// (release/18.x).

func.func @small(%a: memref<64x64xf32>, %b: memref<64x64xf32>, %c: memref<64x64xf32>) {
  affine.for %i = 0 to 64 {
    affine.for %k = 0 to 64 {
      affine.for %j = 0 to 64 {
        %x = affine.load %a[%i, %k] : memref<64x64xf32>
        %y = affine.load %b[%k, %j] : memref<64x64xf32>
        %z = affine.load %c[%i, %j] : memref<64x64xf32>
        %p = arith.mulf %x, %y : f32
        %s = arith.addf %z, %p : f32
        affine.store %s, %c[%i, %j] : memref<64x64xf32>
      }
    }
  }
  return
}

func.func @large(%a: memref<256x256xf32>, %b: memref<256x256xf32>, %c: memref<256x256xf32>) {
  affine.for %i = 0 to 256 {
    affine.for %k = 0 to 256 {
      affine.for %j = 0 to 256 {
        %x = affine.load %a[%i, %k] : memref<256x256xf32>
        %y = affine.load %b[%k, %j] : memref<256x256xf32>
        %z = affine.load %c[%i, %j] : memref<256x256xf32>
        %p = arith.mulf %x, %y : f32
        %s = arith.addf %z, %p : f32
        affine.store %s, %c[%i, %j] : memref<256x256xf32>
      }
    }
  }
  return
}
