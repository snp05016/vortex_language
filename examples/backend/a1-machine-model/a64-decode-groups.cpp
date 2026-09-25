// Sort A64 instruction words into the top-level groups of Arm's decode
// table. A decoder does not look at the whole word first: it looks at a
// few fixed bits, picks a group, and only then reads the group's fields.
//
// Follows: Arm A64 ISA (DDI 0602), "Index by Encoding", top-level table
// (op0 is bit 31, op1 is bits 28 to 25).

#include <cstdint>
#include <cstdio>

// A pattern such as "x1x0" lists op1 from bit 28 down to bit 25; 'x' means
// "either value".
bool matches(unsigned op1, const char* pattern) {
    for (int i = 0; i < 4; ++i) {
        unsigned bit = (op1 >> (3 - i)) & 1u;
        if (pattern[i] != 'x' && pattern[i] - '0' != static_cast<int>(bit)) {
            return false;
        }
    }
    return true;
}

const char* group(std::uint32_t word) {
    unsigned op0 = word >> 31;
    unsigned op1 = (word >> 25) & 0xfu;
    if (op1 == 0) return op0 == 0 ? "reserved" : "SME";
    if (op1 == 0b0010) return "SVE";
    if (matches(op1, "00x1")) return "unallocated";
    if (matches(op1, "100x")) return "data processing, immediate";
    if (matches(op1, "101x")) return "branches, exceptions, system";
    if (matches(op1, "x101")) return "data processing, register";
    if (matches(op1, "x111")) return "scalar floating point and SIMD";
    return "loads and stores";  // the only pattern left is x1x0
}

int main() {
    // Words as llvm-mc 18 printed them for these lines of assembly.
    struct Sample { const char* text; std::uint32_t word; };
    const Sample samples[] = {
        {"add x0, x1, x2", 0x8b020020},
        {"add x0, x0, #1", 0x91000400},
        {"ldr s0, [x0, x1, lsl #2]", 0xbc617800},
        {"fadd s0, s0, s1", 0x1e212800},
        {"b.lo #8", 0x54000043},
        {"ret", 0xd65f03c0},
    };
    for (const Sample& s : samples) {
        unsigned op1 = (s.word >> 25) & 0xfu;
        std::printf("0x%08x  op1=%u%u%u%u  %-30s %s\n", s.word, (op1 >> 3) & 1u,
                    (op1 >> 2) & 1u, (op1 >> 1) & 1u, op1 & 1u, group(s.word),
                    s.text);
    }
    return 0;
}
