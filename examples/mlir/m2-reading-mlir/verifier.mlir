// Five broken functions and one repaired one, checked by the verifier.
//
// --split-input-file cuts the file at each separator comment (five dashes)
// and checks every chunk on its own.
// --verify-diagnostics turns the expected-error and expected-note comments
// into assertions: mlir-opt succeeds only if each one appears on the line that
// @+1 names (the next line), and no other diagnostic appears. Only the last
// chunk is valid, so it is the only one printed.

func.func @not_dominated(%flag: i1, %a: f32) -> f32 {
  cf.cond_br %flag, ^then, ^join
^then:
  // expected-note @+1 {{operand defined here}}
  %doubled = arith.addf %a, %a : f32
  cf.br ^join
^join:
  // expected-error @+1 {{operand #0 does not dominate this use}}
  %r = arith.mulf %doubled, %a : f32
  return %r : f32
}

// -----

func.func @no_terminator(%a: f32) -> f32 {
  // expected-error @+1 {{block with no terminator}}
  %r = arith.addf %a, %a : f32
}

// -----

func.func @wrong_return_type(%a: i32) -> f32 {
  // expected-error @+1 {{type of return operand 0 ('i32') doesn't match function result type ('f32')}}
  return %a : i32
}

// -----

func.func @undefined_callee(%a: f32) -> f32 {
  // expected-error @+1 {{'missing' does not reference a valid function}}
  %r = func.call @missing(%a) : (f32) -> f32
  return %r : f32
}

// -----

func.func @shape_mismatch(%a: memref<8x16xf32>, %b: memref<15x4xf32>, %c: memref<8x4xf32>) {
  // expected-error @+1 {{operand #1 has shape's dimension #0 to be 16, but found 15}}
  linalg.matmul ins(%a, %b : memref<8x16xf32>, memref<15x4xf32>) outs(%c : memref<8x4xf32>)
  return
}

// -----

// The first chunk, repaired: each branch passes its value to ^join.
func.func @repaired(%flag: i1, %a: f32) -> f32 {
  cf.cond_br %flag, ^then, ^join(%a : f32)
^then:
  %doubled = arith.addf %a, %a : f32
  cf.br ^join(%doubled : f32)
^join(%chosen: f32):
  %r = arith.mulf %chosen, %a : f32
  return %r : f32
}
