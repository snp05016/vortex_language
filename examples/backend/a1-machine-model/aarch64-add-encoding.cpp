// Hand-encode the AArch64 instruction `add xD, xN, xM` (64-bit, shifted
// register form, shift amount 0) from its field layout and print the
// bytes. An assembler does this arithmetic for every instruction form.
//
// Follows: Arm A64 ISA (DDI 0602), "ADD (shifted register)".

#include <cstdint>
#include <cstdio>

// Bit 31 (sf) picks 64-bit; bits 30 and 29 (op, S) pick "add, leave the
// flags alone"; bits 28 to 24 hold the fixed pattern 01011 of this
// instruction class; bits 23 and 22 pick the shift kind (00 is LSL) and
// bits 15 to 10 (imm6) the shift amount, both zero here.
std::uint32_t encode_add_register(unsigned rd, unsigned rn, unsigned rm) {
    std::uint32_t word = 0;
    word |= 1u << 31;             // sf: 64-bit
    word |= 0b01011u << 24;       // this instruction class
    word |= (rm & 0x1fu) << 16;   // Rm, five bits
    word |= (rn & 0x1fu) << 5;    // Rn, five bits
    word |= (rd & 0x1fu);         // Rd, five bits
    return word;                  // op, S, shift, bit 21, imm6 stay 0
}

void print_instruction(const char* text, std::uint32_t word) {
    // The first byte in memory is the least significant byte of the word
    // (little-endian), which is also the order llvm-mc prints.
    std::printf("%-16s -> %02x %02x %02x %02x (word 0x%08x)\n", text,
                word & 0xff, (word >> 8) & 0xff, (word >> 16) & 0xff,
                (word >> 24) & 0xff, word);
}

int main() {
    print_instruction("add x0, x1, x2", encode_add_register(0, 1, 2));
    print_instruction("add x5, x6, x7", encode_add_register(5, 6, 7));
    // Register number 31 in this form names the zero register, xzr.
    print_instruction("add x0, x1, xzr", encode_add_register(0, 1, 31));
    return 0;
}
