// Decode a few SPIR-V instructions by hand, word by word.
// Follows: Khronos, "SPIR-V Specification", section 2.3 "Physical Layout of
// a SPIR-V Module and Instruction" and section 3 "Binary Form".
//
// The words below were cut from the binary that mlir-translate
// --serialize-spirv produced for relu_spirv.mlir on this book's machine:
// the 5-word header, the first capability, the memory model, and the
// comparison and branch that the scf.if became. Everything else in that
// 257-word module is left out; the point is the encoding, not the program.
#include <array>
#include <cstdint>
#include <cstdio>
#include <span>

namespace {

enum class Kind { id, number };  // how to print an operand word

struct Opcode {
  std::uint16_t code;
  const char* name;
  std::array<Kind, 4> operands;  // only the first (word count - 1) are used
};

using enum Kind;
// A tiny slice of the specification's opcode table (section 3.3).
constexpr std::array opcodes{
    Opcode{17, "OpCapability", {number}},
    Opcode{14, "OpMemoryModel", {number, number}},
    Opcode{186, "OpFOrdGreaterThan", {id, id, id, id}},
    Opcode{247, "OpSelectionMerge", {id, number}},
    Opcode{250, "OpBranchConditional", {id, id, id}},
    Opcode{248, "OpLabel", {id}},
    Opcode{62, "OpStore", {id, id}},
    Opcode{249, "OpBranch", {id}},
};

constexpr std::array<std::uint32_t, 5> header{
    0x07230203, 0x00010000, 0x00000016, 0x0000002c, 0x00000000};

constexpr std::array<std::uint32_t, 33> body{
    0x00020011, 0x00000001,                          // capability
    0x0003000e, 0x00000000, 0x00000001,              // memory model
    0x000500ba, 0x00000020, 0x00000021, 0x0000001e, 0x0000001f,
    0x000300f7, 0x00000027, 0x00000000,
    0x000400fa, 0x00000021, 0x00000025, 0x00000026,
    0x000200f8, 0x00000025,
    0x0003003e, 0x00000023, 0x0000001e,
    0x000200f9, 0x00000027,
    0x000200f8, 0x00000026,
    0x0003003e, 0x00000023, 0x0000001f,
    0x000200f9, 0x00000027,
    0x000200f8, 0x00000027,
};

const Opcode* find(std::uint16_t code) {
  for (const Opcode& op : opcodes)
    if (op.code == code) return &op;
  return nullptr;
}

}  // namespace

int main() {
  std::printf("magic   0x%08x\n", header[0]);
  // Version bytes are 0 | major | minor | 0, high to low.
  std::printf("version %u.%u\n", (header[1] >> 16) & 0xff, (header[1] >> 8) & 0xff);
  std::printf("bound   %u (every id is below this)\n\n", header[3]);

  std::span<const std::uint32_t> words{body};
  while (!words.empty()) {
    // Word 0 of every instruction: word count in the high 16 bits,
    // opcode in the low 16. The count lets a reader skip opcodes it
    // does not know, which is what keeps the format extensible.
    std::uint32_t count = words[0] >> 16;
    auto code = static_cast<std::uint16_t>(words[0] & 0xffff);
    const Opcode* op = find(code);
    std::printf("0x%08x  %u words  %-20s", words[0], count,
                op ? op->name : "(unknown)");
    for (std::uint32_t i = 1; i < count; ++i) {
      if (op && op->operands[i - 1] == id)
        std::printf(" %%%u", words[i]);  // an <id>: a name, not a value
      else
        std::printf(" %u", words[i]);
    }
    std::printf("\n");
    words = words.subspan(count);
  }
}
