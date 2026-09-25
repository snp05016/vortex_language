// Reverse each block's 64 values in place, through workgroup memory.
// Each thread copies one value into %tile, waits at the barrier until the
// whole block has done the same, then reads its mirror image back.
// Without the barrier, a thread could read a slot its neighbour has not
// written yet.
//
// %tile is a memory attribution: a buffer that belongs to one launch of
// the kernel and is shared by the threads of one block. Its memref type
// carries the workgroup address space; the NVVM lowering turns it into a
// global in LLVM address space 3, NVPTX's shared memory.
//
// The option use-bare-ptr-memref-call-conv passes %data as one pointer
// instead of a memref descriptor, which keeps the output short.
gpu.module @tiles {
  gpu.func @reverse_tiles(%data: memref<256xf32>)
      workgroup(%tile: memref<64xf32, #gpu.address_space<workgroup>>)
      kernel {
    %c63 = arith.constant 63 : index
    %c64 = arith.constant 64 : index
    %bx = gpu.block_id x
    %tx = gpu.thread_id x
    %base = arith.muli %bx, %c64 : index
    %i = arith.addi %base, %tx : index
    %v = memref.load %data[%i] : memref<256xf32>
    memref.store %v, %tile[%tx] : memref<64xf32, #gpu.address_space<workgroup>>
    gpu.barrier
    %mirror = arith.subi %c63, %tx : index
    %w = memref.load %tile[%mirror] : memref<64xf32, #gpu.address_space<workgroup>>
    memref.store %w, %data[%i] : memref<256xf32>
    gpu.return
  }
}
