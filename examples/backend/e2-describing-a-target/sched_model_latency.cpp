// A toy scheduling model with LLVM's two levels of indirection. Each
// instruction names a scheduling class ("an integer multiply writes its
// result"), which is shared by every processor. Each processor model then
// gives each class a latency. The code that computes when results are ready
// never names a processor; changing the model changes only the table.
// The latencies are invented for the illustration, not taken from a chip.
// Follows: LLVM 18.1.8 TargetSchedule.td (SchedWrite, WriteRes, Latency).

#include <array>
#include <iostream>
#include <string_view>

enum Write { WriteLoad, WriteMul, WriteAlu, WriteStore, NumWrites };

struct Instr {
    std::string_view text;
    Write write;
    int waits_for;  // index of the instruction whose result it reads, or -1
};

// A dependent chain: each instruction reads the previous one's result.
constexpr std::array<Instr, 4> kBlock = {{
    {"ldr x1, [x0]", WriteLoad, -1},
    {"mul x2, x1, x1", WriteMul, 0},
    {"add x3, x2, #1", WriteAlu, 1},
    {"str x3, [x0, #8]", WriteStore, 2},
}};

struct Model {
    std::string_view name;
    std::array<int, NumWrites> latency;  // indexed by Write
};

constexpr Model kSlowMul = {"model A", {4, 3, 1, 1}};
constexpr Model kFastMul = {"model B", {4, 1, 1, 1}};

// No issue limit: an instruction starts as soon as the result it reads is
// ready. Real models also bound issue width and unit use (IssueWidth, WriteRes).
void schedule(const Model& m) {
    std::cout << m.name << '\n';
    std::array<int, kBlock.size()> ready{};
    int last = 0;
    for (std::size_t i = 0; i < kBlock.size(); ++i) {
        const Instr& in = kBlock[i];
        int start = in.waits_for < 0 ? 0 : ready[in.waits_for];
        ready[i] = start + m.latency[in.write];
        last = ready[i] > last ? ready[i] : last;
        std::cout << "  " << in.text << ": start " << start << ", ready "
                  << ready[i] << '\n';
    }
    std::cout << "  block: " << last << " cycles\n";
}

int main() {
    schedule(kSlowMul);
    schedule(kFastMul);
}
