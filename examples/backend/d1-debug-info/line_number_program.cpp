// Follows: DWARF Debugging Information Format, Version 5, section 6.2
// (the line number program), https://dwarfstd.org/dwarf5std.html
//
// A tiny model of the DWARF line-number state machine. Real DWARF has
// many opcodes and byte-level encoding tricks for compactness; this
// keeps only the three that matter for understanding the idea: move the
// code address forward, move the source line (forward or back), and
// copy the current (address, line) pair into the table as a row.
#include <cstdio>
#include <vector>

enum class Op { AdvancePc, AdvanceLine, Copy, EndSequence };

struct Instr {
    Op op;
    int arg;  // unused for Copy and EndSequence
};

struct Row {
    unsigned address;
    int line;
    bool end_sequence;
};

std::vector<Row> run(const std::vector<Instr>& program) {
    unsigned address = 0;
    int line = 1;
    std::vector<Row> rows;
    for (const Instr& in : program) {
        switch (in.op) {
            case Op::AdvancePc:   address += static_cast<unsigned>(in.arg); break;
            case Op::AdvanceLine: line += in.arg; break;
            case Op::Copy:        rows.push_back({address, line, false}); break;
            case Op::EndSequence: rows.push_back({address, line, true}); break;
        }
    }
    return rows;
}

int main() {
    // A loop: line 11 is a condition checked twice, at two addresses;
    // the address keeps rising even where the line number falls back.
    std::vector<Instr> program = {
        {Op::AdvanceLine, 9}, {Op::Copy, 0},                      // line 10, addr 0
        {Op::AdvancePc, 8}, {Op::AdvanceLine, 1}, {Op::Copy, 0},  // line 11, addr 8 (loop check)
        {Op::AdvancePc, 4}, {Op::AdvanceLine, 1}, {Op::Copy, 0},  // line 12, addr 12 (loop body)
        {Op::AdvancePc, 4}, {Op::AdvanceLine, -1}, {Op::Copy, 0}, // line 11, addr 16 (back edge)
        {Op::AdvancePc, 8}, {Op::AdvanceLine, 3}, {Op::Copy, 0},  // line 14, addr 24 (after loop)
        {Op::AdvancePc, 4}, {Op::EndSequence, 0},                 // addr 28, sequence end
    };

    std::vector<Row> rows = run(program);
    std::printf("%-10s %s\n", "address", "line");
    for (const Row& r : rows) {
        if (r.end_sequence) {
            std::printf("0x%-8x %s\n", r.address, "end_sequence");
        } else {
            std::printf("0x%-8x %d\n", r.address, r.line);
        }
    }
    return 0;
}
