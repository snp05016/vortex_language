// Every GPU programming model launches a "grid" of equally sized "groups" of
// threads (the vocabulary changes per model; the shape does not). This models
// that shape in ordinary C++, with no GPU and no vendor API, and shows that
// a single global id and a (group, local) pair carry the same information.
#include <cstdio>

struct GridShape {
    int groups;        // "blocks" (CUDA/HIP), "workgroups" (Vulkan/WebGPU),
                        // "threadgroups" (Metal), "work-group range" (SYCL)
    int threads_per_group;  // "blockDim" (CUDA/HIP), "local size" (OpenCL),
                             // threads per Metal threadgroup
};

struct ThreadCoord {
    int group_id;
    int local_id;
};

// Every model computes this same pair from its own built-ins:
// CUDA/HIP:   blockIdx.x, threadIdx.x
// OpenCL:     get_group_id(0), get_local_id(0)
// Metal:      threadgroup_position_in_grid, thread_position_in_threadgroup
// WGSL:       workgroup_id, local_invocation_id
ThreadCoord coord_of(const GridShape& shape, int global_id) {
    return ThreadCoord{
        global_id / shape.threads_per_group,
        global_id % shape.threads_per_group,
    };
}

int global_id_of(const GridShape& shape, ThreadCoord c) {
    return c.group_id * shape.threads_per_group + c.local_id;
}

int main() {
    GridShape shape{/*groups=*/5, /*threads_per_group=*/4};
    int total = shape.groups * shape.threads_per_group;

    for (int global_id = 0; global_id < total; ++global_id) {
        ThreadCoord c = coord_of(shape, global_id);
        int roundtrip = global_id_of(shape, c);
        printf("global %2d -> group %d, local %d (roundtrip %2d)\n",
               global_id, c.group_id, c.local_id, roundtrip);
    }
    return 0;
}
