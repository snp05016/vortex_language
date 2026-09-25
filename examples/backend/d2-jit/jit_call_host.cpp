// Follows: Arm AAPCS64, "Use of IP0 and IP1 by the linker" (a veneer may
// overwrite x16 and x17 at a call), and the MOVZ, MOVK and BR encodings
// as printed by llvm-mc -show-encoding.
//
// Generated code often has to call back into the program that made it. The
// JIT already knows the callee's final address, so there is no relocation to
// leave for a linker: the address goes straight into the instructions. A
// bl cannot be trusted to reach (only +/-128 MiB, and mmap may place the
// buffer anywhere), so the stub builds all 64 bits in x16 and branches there.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>

namespace {

// MOVZ/MOVK (wide immediate), 64-bit form:
//   sf=1 | opc (2 bits) | 100101 | hw (2) | imm16 (16) | Rd (5)
// hw selects which 16-bit slice of the register the immediate lands in.
uint32_t movz(unsigned rd, uint16_t imm, unsigned hw) {
    return 0xd2800000u | (hw << 21) | (uint32_t{imm} << 5) | rd;
}
uint32_t movk(unsigned rd, uint16_t imm, unsigned hw) {
    return 0xf2800000u | (hw << 21) | (uint32_t{imm} << 5) | rd;
}
uint32_t br(unsigned rn) { return 0xd61f0000u | (rn << 5); }

// Five words: put `target` in x16 one 16-bit slice at a time, then jump.
void emit_stub(uint32_t *out, uint64_t target) {
    out[0] = movz(16, static_cast<uint16_t>(target), 0);
    for (unsigned hw = 1; hw < 4; ++hw)
        out[hw] = movk(16, static_cast<uint16_t>(target >> (16 * hw)), hw);
    out[4] = br(16);
}

int64_t host_square(int64_t x) { return x * x; }

}  // namespace

int main() {
    // A fixed address first, so the words can be checked against llvm-mc.
    uint32_t words[5];
    emit_stub(words, 0x9abc5678deadbeefull);
    for (uint32_t w : words) std::printf("%08x\n", w);

    // Now a real one: the stub jumps to host_square. Because it is a tail
    // jump (br, not blr), host_square returns straight to our caller, and
    // x0 still holds the argument we passed to the stub.
    emit_stub(words, reinterpret_cast<uint64_t>(&host_square));
    void *mem = mmap(nullptr, sizeof words, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) return 1;
    std::memcpy(mem, words, sizeof words);
    if (mprotect(mem, sizeof words, PROT_READ | PROT_EXEC) != 0) return 1;
    __builtin___clear_cache(static_cast<char *>(mem),
                            static_cast<char *>(mem) + sizeof words);

    auto stub = reinterpret_cast<int64_t (*)(int64_t)>(mem);
    std::printf("stub(9) = %lld\n", static_cast<long long>(stub(9)));
    munmap(mem, sizeof words);
    return 0;
}
