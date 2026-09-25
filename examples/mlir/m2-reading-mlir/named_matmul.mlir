// A named structured operation hides a region.
//
// In custom syntax linalg.matmul is one line: two inputs after `ins`, one
// output after `outs`, and no result, because it writes into the output
// buffer. The generic form printed below shows what the name stands for: a
// region whose block receives one element of each operand, multiplies,
// adds into the output element and yields the sum.
//
// The shapes are part of the types, so a 16-column input can only meet a
// 16-row input; the verifier rejects any other pairing.

func.func @small_matmul(%a: memref<8x16xf32>, %b: memref<16x4xf32>,
                        %c: memref<8x4xf32>) {
  linalg.matmul ins(%a, %b : memref<8x16xf32>, memref<16x4xf32>)
                outs(%c : memref<8x4xf32>)
  return
}
