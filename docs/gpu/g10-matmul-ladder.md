# G10. The GPU matmul ladder

<p class="page-intro">From a naive kernel to a warp-tiled one, each rung explained by one hardware fact.</p>

!!! note "This chapter is being written"

    It belongs to the GPU book (D. Matmul and friends). Until it is published,
    the [book overview](index.md) lists every chapter and its status.

    **Builds on:** [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md), [G5. Occupancy and latency hiding](g5-occupancy.md).
