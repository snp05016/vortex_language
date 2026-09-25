// A 2x2 contraction lowered by the outer-product strategy. Each step of the
// reduction over k becomes one rank-1 update of the whole accumulator: a
// column of %a times a row of %b, added into the running result. The first
// update's accumulator is %c, so the old contents of %c are added first.
#lhs = affine_map<(m, n, k) -> (m, k)>
#rhs = affine_map<(m, n, k) -> (k, n)>
#acc = affine_map<(m, n, k) -> (m, n)>
func.func @mm2x2(%a: vector<2x2xf32>, %b: vector<2x2xf32>,
                 %c: vector<2x2xf32>) -> vector<2x2xf32> {
  %r = vector.contract {indexing_maps = [#lhs, #rhs, #acc],
                        iterator_types = ["parallel", "parallel", "reduction"],
                        kind = #vector.kind<add>}
       %a, %b, %c : vector<2x2xf32>, vector<2x2xf32> into vector<2x2xf32>
  return %r : vector<2x2xf32>
}

module attributes {transform.with_named_sequence} {
  transform.named_sequence @__transform_main(
      %root: !transform.any_op {transform.readonly}) {
    %fn = transform.structured.match ops{["func.func"]} in %root
        : (!transform.any_op) -> !transform.any_op
    transform.apply_patterns to %fn {
      transform.apply_patterns.vector.lower_contraction
          lowering_strategy = "outerproduct"
    } : !transform.any_op
    transform.yield
  }
}
