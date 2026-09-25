// The same blend, as one linalg.add, mapped to the GPU by a schedule
// instead of by fixed passes. The schedule tiles twice with scf.forall
// loops that carry mapping attributes:
//   4x8 tiles, one per block:     rows -> block y, columns -> block x
//   1x1 tiles, one per thread:    rows -> thread y, columns -> thread x
// then turns the outer forall into a gpu.launch and the inner one into
// thread ids. Here the schedule, not a pass's habit, decides that
// columns, the contiguous index, run along thread x.
module attributes {transform.with_named_sequence} {
  func.func @blend(%a: memref<8x16xf32>, %b: memref<8x16xf32>, %out: memref<8x16xf32>) {
    linalg.add ins(%a, %b : memref<8x16xf32>, memref<8x16xf32>)
               outs(%out : memref<8x16xf32>)
    return
  }

  transform.named_sequence @__transform_main(%root: !transform.any_op {transform.readonly}) {
    %add = transform.structured.match ops{["linalg.add"]} in %root
        : (!transform.any_op) -> !transform.any_op
    %tile, %grid = transform.structured.tile_using_forall %add tile_sizes [4, 8]
        (mapping = [#gpu.block<y>, #gpu.block<x>])
        : (!transform.any_op) -> (!transform.any_op, !transform.any_op)
    %point, %lanes = transform.structured.tile_using_forall %tile tile_sizes [1, 1]
        (mapping = [#gpu.thread<y>, #gpu.thread<x>])
        : (!transform.any_op) -> (!transform.any_op, !transform.any_op)
    %func = transform.structured.match ops{["func.func"]} in %root
        : (!transform.any_op) -> !transform.any_op
    %launch = transform.gpu.map_forall_to_blocks %func generate_gpu_launch
        : (!transform.any_op) -> !transform.any_op
    // block_dims is x, y, z: 8 threads across columns, 4 down rows.
    transform.gpu.map_nested_forall_to_threads %launch block_dims = [8, 4, 1]
        : (!transform.any_op) -> !transform.any_op
    transform.yield
  }
}
