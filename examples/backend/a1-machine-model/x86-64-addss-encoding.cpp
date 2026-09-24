// Hand-encode the x86-64 instruction `addss (%base,%index,scale), %xmm(reg)`
// from its field layout: a mandatory prefix, a two-byte opcode, a ModR/M
// byte and a SIB (scale-index-base) byte. Unlike the AArch64 example next
// to this one, there is no single fixed width: this form is five bytes,
// and other x86-64 instructions are as short as one or as long as fifteen.

#include <cstdint>
#include <cstdio>
#include <vector>

// Register numbers 0-7 only (rdi=7, rsi=6, rax=0, rcx=1, xmm0=0, xmm3=3):
// this example stays inside that range, so no REX prefix is needed for an
// extended register (x86-64 registers r8-r15 and xmm8-xmm15 need one).
std::vector<std::uint8_t> encode_addss_mem(unsigned xmm_dst, unsigned base,
                                            unsigned index, unsigned scale) {
    std::vector<std::uint8_t> bytes;
    bytes.push_back(0xf3);                       // mandatory prefix: scalar single
    bytes.push_back(0x0f);                       // two-byte opcode, part 1
    bytes.push_back(0x58);                       // two-byte opcode, part 2: ADDSS
    // ModR/M: mod=00 (memory, no displacement), reg=xmm_dst, rm=100
    // (rm=100 does not name a register; it says "a SIB byte follows").
    std::uint8_t modrm = (0b00 << 6) | ((xmm_dst & 0x7) << 3) | 0b100;
    bytes.push_back(modrm);
    // SIB: scale as a power-of-two code (1,2,4,8 -> 00,01,10,11), then
    // the index register and the base register.
    unsigned scale_code = (scale == 1) ? 0 : (scale == 2) ? 1 : (scale == 4) ? 2 : 3;
    std::uint8_t sib = (scale_code << 6) | ((index & 0x7) << 3) | (base & 0x7);
    bytes.push_back(sib);
    return bytes;
}

void print_instruction(const char* text, const std::vector<std::uint8_t>& bytes) {
    std::printf("%-30s ->", text);
    for (std::uint8_t b : bytes) {
        std::printf(" %02x", b);
    }
    std::printf("\n");
}

int main() {
    // addss (%rdi,%rcx,4), %xmm0: rdi=7 (base), rcx=1 (index), scale 4.
    print_instruction("addss (%rdi,%rcx,4), %xmm0",
                       encode_addss_mem(/*xmm_dst=*/0, /*base=*/7, /*index=*/1, /*scale=*/4));
    // addss (%rsi,%rax,8), %xmm3: rsi=6 (base), rax=0 (index), scale 8.
    print_instruction("addss (%rsi,%rax,8), %xmm3",
                       encode_addss_mem(/*xmm_dst=*/3, /*base=*/6, /*index=*/0, /*scale=*/8));
    return 0;
}
