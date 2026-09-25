// Follows: DWARF Debugging Information Format, Version 5, section 6.2
// (line number information) and section 7.6 (LEB128),
// https://dwarfstd.org/dwarf5std.html
//
// Decodes a real DWARF line-number program by hand. The bytes are the
// program the assembler built for line_table.s (read back with
// llvm-dwarfdump --debug-line --verbose); the header values below come
// from the same table's header. Only the opcodes this program uses are
// handled, so an unknown one stops the decoder instead of guessing.
#include <cstdint>
#include <cstdio>
#include <vector>

// Header fields of this line table, as llvm-dwarfdump printed them.
constexpr int line_base = -5, line_range = 14, opcode_base = 13;

const std::vector<std::uint8_t> program = {
    0x05, 0x05,                                            // set_column 5
    0x00, 0x09, 0x02, 0, 0, 0, 0, 0, 0, 0, 0,              // set_address 0
    0x03, 0x09, 0x01,                                      // advance_line, copy
    0x05, 0x09, 0x83, 0x05, 0x05, 0x84,
    0x05, 0x09, 0x83, 0x05, 0x05, 0x84,
    0x02, 0x04, 0x00, 0x01, 0x01,                          // advance_pc 4, end
};

std::size_t pos = 0;

// LEB128: seven payload bits per byte, low bits first; a set high bit
// means another byte follows. Signed values sign-extend from the last byte.
std::uint64_t uleb() {
    std::uint64_t v = 0;
    for (int shift = 0;; shift += 7) {
        std::uint8_t b = program[pos++];
        v |= std::uint64_t(b & 0x7f) << shift;
        if (!(b & 0x80)) return v;
    }
}
std::int64_t sleb() {
    std::int64_t v = 0;
    int shift = 0;
    std::uint8_t b;
    do { b = program[pos++]; v |= std::int64_t(b & 0x7f) << shift; shift += 7; } while (b & 0x80);
    if (shift < 64 && (b & 0x40)) v |= -(std::int64_t(1) << shift);
    return v;
}

int main() {
    std::uint64_t address = 0;  // the state machine's registers, at their
    std::int64_t line = 1;      // initial values from Table 6.4
    std::uint64_t column = 0;
    auto row = [&](const char* why) {
        std::printf("  row  0x%02llx  line %lld  col %llu   (%s)\n",
                    (unsigned long long)address, (long long)line,
                    (unsigned long long)column, why);
    };
    while (pos < program.size()) {
        std::uint8_t op = program[pos++];
        if (op >= opcode_base) {  // special opcode: both registers, then a row
            int adjusted = op - opcode_base;
            int addr_step = adjusted / line_range;
            int line_step = line_base + adjusted % line_range;
            address += addr_step;
            line += line_step;
            std::printf("0x%02x special: address += %d, line += %d\n", op, addr_step, line_step);
            row("special");
        } else if (op == 0x01) { std::printf("0x01 copy\n"); row("copy");
        } else if (op == 0x02) { auto d = uleb(); address += d; std::printf("0x02 advance_pc %llu\n", (unsigned long long)d);
        } else if (op == 0x03) { auto d = sleb(); line += d; std::printf("0x03 advance_line %lld\n", (long long)d);
        } else if (op == 0x05) { column = uleb(); std::printf("0x05 set_column %llu\n", (unsigned long long)column);
        } else if (op == 0x00) {  // extended: length, sub-opcode, operands
            std::uint64_t len = uleb();
            std::uint8_t sub = program[pos];
            if (sub == 0x02) {  // an 8-byte little-endian address follows
                address = 0;
                for (int i = 8; i >= 1; --i) address = (address << 8) | program[pos + i];
                std::printf("0x00 set_address 0x%llx\n", (unsigned long long)address);
            }
            if (sub == 0x01) { std::printf("0x00 end_sequence\n"); row("end_sequence"); }
            pos += len;
        } else { std::printf("unhandled opcode 0x%02x\n", op); return 1; }
    }
    return 0;
}
