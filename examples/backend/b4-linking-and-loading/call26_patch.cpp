// A linker's two main steps for one call, done by hand: lay out the merged
// .text section, then patch the BL instruction's R_AARCH64_CALL26
// relocation with X = S + A - P (symbol address + addend - address of the
// place). BL stores X / 4 in its low 26 bits, so X must be a multiple of 4
// in [-2^27, 2^27). The output words match what llvm-mc encodes for
// "bl #16" and "bl #-24".
//
// Follows: Arm, "ELF for the Arm 64-bit Architecture (AArch64)", the
// relocation operations and the R_AARCH64_CALL26 entry.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

struct Object {
    std::string name;
    std::uint64_t text_size;
};

// main.o calls report; report.o defines report at offset 0 of its .text.
constexpr std::uint64_t kCallOffsetInMain = 0x8;
constexpr std::uint64_t kReportOffset = 0x0;
constexpr std::uint64_t kTextBase = 0x10000;  // a toy start address
constexpr std::uint32_t kBL = 0x94000000;     // BL with an imm26 of zero

void link(const std::vector<Object> &order) {
    std::printf("layout:");
    for (const auto &o : order)
        std::printf(" %s", o.name.c_str());
    std::printf("\n");

    // Step 1: merge every .text in command-line order; each one's start
    // address is the sum of the sizes placed before it.
    std::uint64_t next = kTextBase, main_at = 0, report_at = 0;
    for (const auto &o : order) {
        if (o.name == "main.o") main_at = next;
        if (o.name == "report.o") report_at = next;
        next += o.text_size;
    }

    // Step 2: apply the relocation. A (the addend) is 0 for a plain call.
    const std::uint64_t P = main_at + kCallOffsetInMain;
    const std::uint64_t S = report_at + kReportOffset;
    const std::int64_t X = static_cast<std::int64_t>(S - P);
    std::printf("  P = 0x%llx, S = 0x%llx, X = %lld\n",
                static_cast<unsigned long long>(P),
                static_cast<unsigned long long>(S), static_cast<long long>(X));

    if (X < -(std::int64_t{1} << 27) || X >= (std::int64_t{1} << 27) || X % 4) {
        std::printf("  out of range for BL: the linker needs a veneer\n");
        return;
    }
    const std::uint32_t imm26 = static_cast<std::uint32_t>(X >> 2) & 0x3FFFFFF;
    std::printf("  imm26 = 0x%07x, patched word = 0x%08x\n", imm26, kBL | imm26);
}

int main() {
    const Object main_o{"main.o", 0x18};
    const Object report_o{"report.o", 0x10};
    const Object big_o{"big.o", 0x8000000};  // 128 MiB of other code

    link({main_o, report_o});         // forward call
    link({report_o, main_o});         // same objects, other order: backward
    link({main_o, big_o, report_o});  // just past BL's reach
}
