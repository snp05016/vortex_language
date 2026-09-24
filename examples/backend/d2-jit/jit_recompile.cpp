// Follows: Eli Bendersky, "How to JIT - an introduction".
//
// A JIT does not compile once and stop: an auto-tuner rewrites the same
// address with a new variant and times it again. This overwrites one code
// buffer twice and calls it after each write, to show that the
// write/protect/invalidate sequence from jit_call.cpp is not a one-time
// setup step but something every rewrite has to repeat.
//
// Encodings, again read off `llvm-mc -arch=aarch64 -show-encoding`:
//   0x8b010000  add x0, x0, x1
//   0xcb010000  sub x0, x0, x1
//   0xd65f03c0  ret

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

using BinFn = int64_t (*)(int64_t, int64_t);

namespace {

void write_variant(void *mem, const uint32_t *words, std::size_t size) {
    if (mprotect(mem, size, PROT_READ | PROT_WRITE) != 0) {
        std::perror("mprotect(RW)");
        std::exit(1);
    }
    std::memcpy(mem, words, size);
    if (mprotect(mem, size, PROT_READ | PROT_EXEC) != 0) {
        std::perror("mprotect(RX)");
        std::exit(1);
    }
    __builtin___clear_cache(static_cast<char *>(mem),
                             static_cast<char *>(mem) + size);
}

}  // namespace

int main() {
    static const uint32_t add_variant[] = {0x8b010000u, 0xd65f03c0u};
    static const uint32_t sub_variant[] = {0xcb010000u, 0xd65f03c0u};
    const std::size_t size = sizeof(add_variant);

    void *mem = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        std::perror("mmap");
        return 1;
    }

    write_variant(mem, add_variant, size);
    auto variant = reinterpret_cast<BinFn>(mem);
    std::printf("%lld\n", static_cast<long long>(variant(3, 4)));

    // Same address, different instructions. Skipping the RW/RX toggle or the
    // cache invalidation here is the bug this chapter is about: the second
    // call could still run the first variant.
    write_variant(mem, sub_variant, size);
    std::printf("%lld\n", static_cast<long long>(variant(3, 4)));

    munmap(mem, size);
    return 0;
}
