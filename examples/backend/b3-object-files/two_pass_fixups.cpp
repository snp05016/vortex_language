#include <cstdio>
#include <map>
#include <string>
#include <vector>

// A minimal two-pass assembler for a three-instruction toy machine. It
// isolates one idea, forward references, from any real encoding: none of
// this is a real instruction set.
//
// Pass 1 walks the program once and records only where each label landed;
// it resolves nothing, because a forward jump (to a label written later)
// has no address yet. Pass 2 has the complete label table, so it resolves
// every JMP, forward or backward, the same way.
//
// Follows: Salomon, "Assemblers and Loaders" (1993), section 1.2, "The
// Two-Pass Assembler" (https://www.davidsalomon.name/assem.advertis/asl.pdf).

enum class Op { Set, Jmp, Halt };

struct Instr {
    Op op;
    std::string label;   // this instruction's own label, or "" for none
    int imm = 0;          // SET's immediate
    std::string target;   // JMP's target label
};

int main() {
    // Every instruction is one word here, so its address is its index in
    // this vector. x86-64 assemblers cannot assume that: instruction lengths
    // vary, and a jump's own length can depend on how far it jumps.
    std::vector<Instr> program = {
        {Op::Set, "start", 1, ""},
        {Op::Jmp, "", 0, "skip"},        // forward: "skip" is defined below
        {Op::Set, "", 99, ""},           // dead code; the jump above skips it
        {Op::Jmp, "skip", 0, "start"},   // backward: "start" is already known
        {Op::Halt, "", 0, ""},
    };

    // Pass 1: addresses and labels only, no resolution.
    std::map<std::string, int> labels;
    for (int addr = 0; addr < static_cast<int>(program.size()); ++addr) {
        if (!program[addr].label.empty())
            labels[program[addr].label] = addr;
    }

    // Pass 2: the table is complete, so both jumps resolve the same way.
    std::printf("addr  instruction\n");
    for (int addr = 0; addr < static_cast<int>(program.size()); ++addr) {
        const Instr &ins = program[addr];
        switch (ins.op) {
        case Op::Set:
            std::printf("%4d  SET R0, %d\n", addr, ins.imm);
            break;
        case Op::Jmp: {
            int target = labels.at(ins.target);   // the fixup, filled in now
            std::printf("%4d  JMP %-5s -> %d\n", addr, ins.target.c_str(), target);
            break;
        }
        case Op::Halt:
            std::printf("%4d  HALT\n", addr);
            break;
        }
    }
    return 0;
}
