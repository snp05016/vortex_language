# O7. Calls and aggregates: inlining and SROA

<p class="page-intro">When to copy a function body into its caller, and how splitting structs and small arrays into scalars lets later passes see through them.</p>

!!! note "This chapter is being written"

    It belongs to the Optimize book (The middle end). Until it is published,
    the [book overview](index.md) lists every chapter and its status.

    **Builds on:** [O3. SSA form: construction and destruction](o3-ssa.md), [O5. Constants and dead code](o5-constants-and-dead-code.md).
