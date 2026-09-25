// A transform handle is an SSA value like any other (O3): each is defined
// once and stands for a set of payload ops. tile_using_for consumes its
// input handle, so using %matmul again after tiling with it is invalid,
// exactly as reusing a moved-from value would be in an ownership system.
// The matmul below is never tiled a second time; the interpreter rejects
// the schedule before it runs anything past the first tiling.
module attributes {transform.with_named_sequence} {
  func.func @one_matmul(%a: memref<8x8xf32>, %b: memref<8x8xf32>, %c: memref<8x8xf32>) {
    // expected-note @below {{ancestor payload op}}
    // expected-note @below {{nested payload op}}
    linalg.matmul ins(%a, %b : memref<8x8xf32>, memref<8x8xf32>) outs(%c : memref<8x8xf32>)
    return
  }

  transform.named_sequence @__transform_main(%root: !transform.any_op {transform.readonly}) {
    // expected-note @below {{handle to invalidated ops}}
    %matmul = transform.structured.match ops{["linalg.matmul"]} in %root
        : (!transform.any_op) -> !transform.any_op
    // expected-note @below {{invalidated by this transform op}}
    %tiled, %loops:3 = transform.structured.tile_using_for %matmul [4, 4, 4]
        : (!transform.any_op) -> (!transform.any_op, !transform.any_op, !transform.any_op, !transform.any_op)
    // expected-error @below {{op uses a handle invalidated by a previously executed transform op}}
    %tiled2, %loops2:3 = transform.structured.tile_using_for %matmul [2, 2, 2]
        : (!transform.any_op) -> (!transform.any_op, !transform.any_op, !transform.any_op, !transform.any_op)
    transform.yield
  }
}
