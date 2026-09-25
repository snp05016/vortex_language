// SAXPY as a single-source program: the host function and the code every
// thread runs sit together, the thread code inline in a gpu.launch region.
// --gpu-kernel-outlining splits them the way a single-source compiler must:
// the region becomes a kernel in its own gpu.module (the device side), and
// a gpu.launch_func takes its place in the host function. Every value the
// region used from outside, here n, a, x and y, becomes a kernel argument.
module {
  func.func @saxpy(%a: f32, %x: memref<1000xf32>, %y: memref<1000xf32>) {
    %one = arith.constant 1 : index
    %groups = arith.constant 4 : index
    %group_size = arith.constant 256 : index
    %n = arith.constant 1000 : index
    gpu.launch blocks(%bx, %by, %bz) in (%gx = %groups, %gy = %one, %gz = %one)
               threads(%tx, %ty, %tz) in (%sx = %group_size, %sy = %one, %sz = %one) {
      %base = arith.muli %bx, %sx : index
      %i = arith.addi %base, %tx : index
      // 4 groups of 256 threads are 1024 threads for 1000 elements:
      // the last 24 must skip the loads and the store.
      %inside = arith.cmpi ult, %i, %n : index
      scf.if %inside {
        %xv = memref.load %x[%i] : memref<1000xf32>
        %yv = memref.load %y[%i] : memref<1000xf32>
        %ax = arith.mulf %a, %xv : f32
        %sum = arith.addf %ax, %yv : f32
        memref.store %sum, %y[%i] : memref<1000xf32>
      }
      gpu.terminator
    }
    return
  }
}
