// Follows: Eli Bendersky, "How to JIT - an introduction" (allocate, write,
// make executable, call through a function pointer).
//
// Hand-encodes two AArch64 instructions and calls them through a function
// pointer. The words below are not guessed: `echo 'add x0, x0, x1\nret' |
// llvm-mc -arch=aarch64 -show-encoding` gives bytes 00 00 01 8b, c0 03 5f d6,
// which read as the little-endian 32-bit words below.
//   0x8b010000  add x0, x0, x1   (x0 = x0 + x1; AAPCS64 puts arg 0 in x0,
//                                 arg 1 in x1, and the result back in x0)
//   0xd65f03c0  ret
//
// This never sets PROT_WRITE and PROT_EXEC on the mapping at the same time:
// the page is writable while the bytes go in, then mprotect switches it to
// executable before the first call. mmap and mprotect work in whole pages,
// so the 8 bytes below occupy one page. On macOS this route works for a
// command-line tool without the hardened runtime; a hardened-runtime app
// uses MAP_JIT instead (see jit_map_jit.cpp).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

using AddFn = int64_t (*)(int64_t, int64_t);

int main() {
    static const uint32_t code[] = {0x8b010000u, 0xd65f03c0u};
    const std::size_t size = sizeof(code);

    void *mem = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        std::perror("mmap");
        return 1;
    }
    std::memcpy(mem, code, size);
    if (mprotect(mem, size, PROT_READ | PROT_EXEC) != 0) {
        std::perror("mprotect");
        return 1;
    }
    // The core may already have fetched and cached the old (empty) bytes at
    // this address; without this, it could run stale instructions.
    __builtin___clear_cache(static_cast<char *>(mem),
                             static_cast<char *>(mem) + size);

    auto add = reinterpret_cast<AddFn>(mem);
    std::printf("%lld\n", static_cast<long long>(add(3, 4)));

    munmap(mem, size);
    return 0;
}
