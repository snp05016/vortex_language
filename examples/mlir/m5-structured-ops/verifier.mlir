// Follows: MLIR 'linalg' dialect, linalg.generic "Property 1" (operands
// define the iteration space), and "Testing Guide" for --verify-diagnostics.
// Three broken structured ops and two valid ones. Each expected-error comment
// is an assertion about the error on the next line.
#id = affine_map<(i, j) -> (i, j)>
#row = affine_map<(i, j) -> (i)>
func.func @short_output(%in: memref<3x4xf32>, %out: memref<2xf32>) {
  // expected-error @+1 {{inferred input/output operand #1 has shape's dimension #0 to be 3, but found 2}}
  linalg.generic {indexing_maps = [#id, #row], iterator_types = ["parallel", "reduction"]}
      ins(%in : memref<3x4xf32>) outs(%out : memref<2xf32>) {
  ^bb0(%e: f32, %acc: f32):
    %s = arith.addf %acc, %e : f32
    linalg.yield %s : f32
  }
  return
}
// -----
#id = affine_map<(i, j) -> (i, j)>
#row = affine_map<(i, j) -> (i)>
func.func @one_iterator(%in: memref<3x4xf32>, %out: memref<3xf32>) {
  // expected-error @+1 {{expected indexing_map #0 to have 1 dim(s) to match the number of loops}}
  linalg.generic {indexing_maps = [#id, #row], iterator_types = ["parallel"]}
      ins(%in : memref<3x4xf32>) outs(%out : memref<3xf32>) {
  ^bb0(%e: f32, %acc: f32):
    %s = arith.addf %acc, %e : f32
    linalg.yield %s : f32
  }
  return
}
// -----
#shifted = affine_map<(i, k) -> (i + k)>
#row = affine_map<(i, k) -> (i)>
func.func @window_without_taps(%x: memref<6xf32>, %y: memref<4xf32>) {
  // expected-error @+1 {{expected the shape-to-loops map to be non-null}}
  linalg.generic {indexing_maps = [#shifted, #row], iterator_types = ["parallel", "reduction"]}
      ins(%x : memref<6xf32>) outs(%y : memref<4xf32>) {
  ^bb0(%e: f32, %acc: f32):
    %s = arith.addf %acc, %e : f32
    linalg.yield %s : f32
  }
  return
}
// -----
#shifted = affine_map<(i, k) -> (i + k)>
#tap = affine_map<(i, k) -> (k)>
#row = affine_map<(i, k) -> (i)>
// Valid: the taps operand gives k its extent, 3, so y[i] = sum of w[k] * x[i + k].
func.func @window_sum(%x: memref<6xf32>, %w: memref<3xf32>, %y: memref<4xf32>) {
  linalg.generic {indexing_maps = [#shifted, #tap, #row], iterator_types = ["parallel", "reduction"]}
      ins(%x, %w : memref<6xf32>, memref<3xf32>) outs(%y : memref<4xf32>) {
  ^bb0(%e: f32, %t: f32, %acc: f32):
    %p = arith.mulf %e, %t : f32
    %s = arith.addf %acc, %p : f32
    linalg.yield %s : f32
  }
  return
}
// -----
#id = affine_map<(i, j) -> (i, j)>
#row = affine_map<(i, j) -> (i)>
// Valid, and wrong: the output is column 0 of the input (stride 4), so the
// op overwrites data it has yet to read. No rule checks for overlap.
func.func @output_overlaps_input(%in: memref<3x4xf32>) {
  %col = memref.subview %in[0, 0] [3, 1] [1, 1] : memref<3x4xf32> to memref<3xf32, strided<[4]>>
  linalg.generic {indexing_maps = [#id, #row], iterator_types = ["parallel", "reduction"]}
      ins(%in : memref<3x4xf32>) outs(%col : memref<3xf32, strided<[4]>>) {
  ^bb0(%e: f32, %acc: f32):
    %s = arith.addf %acc, %e : f32
    linalg.yield %s : f32
  }
  return
}
