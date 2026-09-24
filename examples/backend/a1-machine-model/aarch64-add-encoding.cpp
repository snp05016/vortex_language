// Hand-encode the AArch64 instruction `add xD, xN, xM` (64-bit register
// form, no shift) from the field layout in the A64 ISA and print the
// resulting bytes. This is the whole story behind "an ISA is a contract
// about bits": the assembler you use every day is doing exactly this
// arithmetic, just for every instruction form at once.

#include <cstdint>
#include <cstdio>

// Field layout for ADD (shifted register), 64-bit, shift amount 0.
// Bit 31 (sf) selects the 64-bit register file; bits 30 and 29 (op, S)
// pick "add, do not set flags"; bits 28-24 are the fixed pattern 01011
// that names this instruction class; bits 23-22 (shift kind) and 21
// are 0 for "no shift"; Rm, imm6, Rn and Rd fill the rest.
std::uint32_t encode_add_register(unsigned rd, unsigned rn, unsigned rm) {
    std::uint32_t word = 0;
    word |= 1u << 31;               // sf: 64-bit
    word |= 0u << 30;               // op: add
    word |= 0u << 29;               // S: do not set NZCV
    word |= 0b01011u << 24;         // fixed pattern for this class
    word |= 0b00u << 22;            // shift kind: none used
    word |= 0u << 21;               // fixed 0
    word |= (rm & 0x1fu) << 16;     // Rm
    word |= 0u << 10;               // imm6: shift amount, 0
    word |= (rn & 0x1fu) << 5;      // Rn
    word |= (rd & 0x1fu);           // Rd
    return word;
}

void print_instruction(const char* text, std::uint32_t word) {
    // AArch64 is little-endian: the least significant byte of the word
    // is the first byte in memory, which is also the order llvm-mc
    // prints when it shows an instruction's encoding.
    std::printf("%-22s -> %02x %02x %02x %02x (word 0x%08x)\n", text,
                word & 0xff, (word >> 8) & 0xff, (word >> 16) & 0xff,
                (word >> 24) & 0xff, word);
}

int main() {
    print_instruction("add x0, x1, x2", encode_add_register(0, 1, 2));
    print_instruction("add x5, x6, x7", encode_add_register(5, 6, 7));
    return 0;
}
