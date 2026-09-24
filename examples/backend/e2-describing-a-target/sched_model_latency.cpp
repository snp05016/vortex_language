// A toy scheduling model: one opcode-to-latency table, walked by a single
// generic function that computes when each instruction in a straight-line
// dependency chain becomes ready. TableGen's scheduling model description
// plays the same role for a real target: a shared, target-independent
// scheduling pass asks "how many cycles does this opcode take", and each
// subtarget answers from its own table instead of its own code.
// Follows: LLVM TableGen Backends, "Instruction Scheduling Models".
// https://llvm.org/docs/TableGen/BackEnds.html

#include <array>
#include <iostream>
#include <string_view>

enum class Op { Load, Mul, Add, Store };

struct Instr {
    Op opcode;
    std::string_view name;
};

// A chain: each instruction consumes the previous one's result, so none
// can start before the one before it finishes. Opcode order is fixed;
// only the latency table below changes between the two models.
constexpr std::array<Instr, 4> kChain = {{
    {Op::Load, "load"},
    {Op::Mul, "mul"},
    {Op::Add, "add"},
    {Op::Store, "store"},
}};

// Two subtarget models, indexed by Op: Load, Mul, Add, Store latencies.
constexpr std::array<unsigned, 4> kGenericLatency = {4, 3, 1, 1};
constexpr std::array<unsigned, 4> kFastMulLatency = {4, 1, 1, 1};

unsigned latency(const std::array<unsigned, 4>& model, Op op) {
    return model[static_cast<unsigned>(op)];
}

void schedule(std::string_view label, const std::array<unsigned, 4>& model) {
    std::cout << label << '\n';
    unsigned ready = 0;
    for (const auto& instr : kChain) {
        unsigned start = ready;
        unsigned finish = start + latency(model, instr.opcode);
        std::cout << "  " << instr.name << ": start " << start << ", finish "
                  << finish << '\n';
        ready = finish;
    }
    std::cout << "  total: " << ready << " cycles\n";
}

int main() {
    schedule("generic model", kGenericLatency);
    schedule("fast-multiply model", kFastMulLatency);
}
