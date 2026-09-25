// Follows: Apple, "Porting just-in-time compilers to Apple silicon"
// (MAP_JIT, pthread_jit_write_protect_np, sys_icache_invalidate).
//
// The Apple route: one MAP_JIT mapping that is never re-protected with
// mprotect. Instead each thread switches its own view of the region between
// "writable, not executable" and "executable, not writable". This file is
// for macOS on Apple silicon only; an unsigned command-line tool needs no
// entitlement for MAP_JIT, a hardened-runtime app needs
// com.apple.security.cs.allow-jit.
//
//   0x9b017c00  mul x0, x0, x1   (llvm-mc -triple=aarch64 -show-encoding)
//   0xd65f03c0  ret

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

using MulFn = int64_t (*)(int64_t, int64_t);

int main() {
    static const uint32_t code[] = {0x9b017c00u, 0xd65f03c0u};
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));

    // MAP_JIT is the one way to ask for write and execute in one mapping;
    // without the flag this request fails with EACCES.
    void *mem = mmap(nullptr, page, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
    if (mem == MAP_FAILED) {
        std::perror("mmap(MAP_JIT)");
        return 1;
    }
    // pthread_jit_write_protect_supported_np() says whether the switch below
    // exists. It returned 0 on a GitHub Actions macOS runner, a virtual
    // machine, and 1 on the machine the chapter used; the program prints 12
    // on both, so its value is not part of the output.

    pthread_jit_write_protect_np(0);  // this thread: writable, not executable
    std::memcpy(mem, code, sizeof code);
    pthread_jit_write_protect_np(1);  // this thread: executable, not writable
    // Calling while the thread is still in the writable state is a bus error.

    sys_icache_invalidate(mem, sizeof code);

    auto mul = reinterpret_cast<MulFn>(mem);
    std::printf("%lld\n", static_cast<long long>(mul(3, 4)));

    munmap(mem, page);
    return 0;
}
