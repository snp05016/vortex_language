// Hand-encode the x86-64 instruction `addss (%base,%index,scale), %xmmN`
// from its parts: a mandatory prefix, a two-byte opcode, a ModR/M byte and
// a SIB (scale-index-base) byte. This form is five bytes long; other
// x86-64 instructions are anywhere from 1 to 15.
//
// Follows: OSDev Wiki, "X86-64 Instruction Encoding" (ModR/M and SIB),
// and the ADDSS entry of the Intel SDM, volume 2.

#include <cstdint>
#include <cstdio>
#include <vector>

// Register numbers 0 to 7 only (rax=0, rcx=1, rsi=6, rdi=7): r8 to r15 and
// xmm8 to xmm15 need a REX prefix for their fourth bit. Two more cases are
// left out on purpose, because with mod=00 they mean something else:
// base 5 (rbp) would mean "no base, 32-bit displacement", and index 4
// (rsp) means "no index".
std::vector<std::uint8_t> encode_addss_mem(unsigned xmm_dst, unsigned base,
                                           unsigned index, unsigned scale) {
    unsigned scale_code = (scale == 1) ? 0 : (scale == 2) ? 1 : (scale == 4) ? 2 : 3;
    std::uint8_t modrm = static_cast<std::uint8_t>(
        (0b00u << 6) | ((xmm_dst & 7u) << 3) | 0b100u);  // rm=100: a SIB byte follows
    std::uint8_t sib = static_cast<std::uint8_t>(
        (scale_code << 6) | ((index & 7u) << 3) | (base & 7u));
    return {0xf3,        // mandatory prefix: scalar single precision
            0x0f, 0x58,  // opcode: ADDSS
            modrm, sib};
}

void print_instruction(const char* text, const std::vector<std::uint8_t>& bytes) {
    std::printf("%-28s ->", text);
    for (std::uint8_t b : bytes) std::printf(" %02x", b);
    std::printf("\n");
}

int main() {
    print_instruction("addss (%rdi,%rcx,4), %xmm0", encode_addss_mem(0, 7, 1, 4));
    print_instruction("addss (%rsi,%rax,8), %xmm3", encode_addss_mem(3, 6, 0, 8));
    return 0;
}
