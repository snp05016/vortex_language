// One SAXPY over n elements with groups of 256 threads, launched in the
// units each programming model's launch call counts. Models that count
// groups must round the group count up, which leaves idle threads that the
// kernel has to guard against. Models that count threads can end the grid
// with a smaller group instead, so every thread has an element.
#include <cstdio>

enum class Counts { groups, threads_uniform, threads_nonuniform };

struct Style {
    const char* launch;
    Counts counts;
};

void show(const Style& style, long n, long group_size) {
    long groups = (n + group_size - 1) / group_size;  // ceiling division
    long threads = groups * group_size;
    long last_group = group_size;
    if (style.counts == Counts::threads_nonuniform) {
        // The last group holds only the remainder (OpenCL's "remainder
        // work-group"; Metal's nonuniform threadgroup).
        threads = n;
        last_group = n - (groups - 1) * group_size;
    }
    // threads_uniform: SYCL rejects a global size that local size does not
    // divide, so the host rounds the global size up itself, as above.
    long idle = threads - n;
    std::printf("%-44s groups %ld, threads %4ld, last group %3ld, idle %2ld, guard %s\n",
                style.launch, groups, threads, last_group, idle, idle > 0 ? "yes" : "no");
}

int main() {
    const Style styles[] = {
        {"CUDA/HIP <<<groups, 256>>>", Counts::groups},
        {"Vulkan vkCmdDispatch(groups, 1, 1)", Counts::groups},
        {"WebGPU dispatchWorkgroups(groups)", Counts::groups},
        {"Metal dispatchThreadgroups(groups, 256)", Counts::groups},
        {"SYCL nd_range(rounded-up global, 256)", Counts::threads_uniform},
        {"OpenCL global n, local 256 (non-uniform)", Counts::threads_nonuniform},
        {"Metal dispatchThreads(n, 256)", Counts::threads_nonuniform},
    };
    const long sizes[] = {1000, 1024};
    for (long n : sizes) {
        std::printf("n = %ld\n", n);
        for (const Style& style : styles) {
            show(style, n, 256);
        }
    }
    return 0;
}
