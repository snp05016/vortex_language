#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

// A one-pass encoder for three AArch64 branch forms and one SUB. Each word is
// written as soon as its line is read. A branch whose target may not be known
// yet is written with a zero offset field and recorded as a fixup. At the end
// of the section every fixup is either resolved here, because its label is in
// this section, or kept as a relocation, because it is not.
//
// The four words it prints match what llvm-mc 18.1.8 writes for the same text
// (-triple=aarch64-linux-gnu -show-encoding).
//
// Follows: Arm, DDI 0602, "CBZ", "CBNZ", "B" and "SUB (immediate)";
// Salomon, "Assemblers and Loaders", section 1.3, "The One-Pass Assembler".

enum class Field { Imm19, Imm26 };   // CBZ, CBNZ, B.cond: 19 bits. B, BL: 26.

struct Fixup {
    std::size_t index;   // which word holds the hole
    const char *mnemonic;
    std::string label;
    Field field;
};

struct Section {
    std::vector<std::uint32_t> words;
    std::map<std::string, std::size_t> labels;   // label -> word index
    std::vector<Fixup> fixups;

    void label(const std::string &name) { labels[name] = words.size(); }
    void emit(std::uint32_t word) { words.push_back(word); }
    void branch(std::uint32_t op, const char *mn, const std::string &to, Field f) {
        fixups.push_back({words.size(), mn, to, f});
        words.push_back(op);   // the offset field stays zero for now
    }
};

std::uint32_t sub_imm(unsigned rd, unsigned rn, unsigned imm12) {
    return 0xD1000000u | imm12 << 10 | rn << 5 | rd;
}

int main() {
    Section text;
    text.label("count_down");
    text.branch(0xB4000000u, "cbz x0", "done", Field::Imm19);   // forward
    text.label("loop");
    text.emit(sub_imm(0, 0, 1));
    text.branch(0xB5000000u, "cbnz x0", "loop", Field::Imm19);  // backward
    text.label("done");
    text.branch(0x14000000u, "b", "tick", Field::Imm26);        // not in this file

    for (const Fixup &f : text.fixups) {
        auto it = text.labels.find(f.label);
        if (it == text.labels.end()) {
            std::printf("0x%02zx %-8s %-5s relocation for the linker\n",
                        f.index * 4, f.mnemonic, f.label.c_str());
            continue;
        }
        // Offsets count 4-byte words, so the distance in words is the field.
        long delta = static_cast<long>(it->second) - static_cast<long>(f.index);
        long limit = f.field == Field::Imm19 ? 1L << 18 : 1L << 25;
        if (delta < -limit || delta >= limit) {
            std::printf("0x%02zx out of range: needs a longer form\n", f.index * 4);
            continue;
        }
        std::uint32_t bits = static_cast<std::uint32_t>(delta);
        std::uint32_t &word = text.words[f.index];
        word |= f.field == Field::Imm19 ? (bits & 0x7FFFFu) << 5 : bits & 0x3FFFFFFu;
        std::printf("0x%02zx %-8s %-5s resolved here, %+ld words\n",
                    f.index * 4, f.mnemonic, f.label.c_str(), delta);
    }
    for (std::size_t i = 0; i < text.words.size(); ++i)
        std::printf("0x%02zx: %08x\n", i * 4, text.words[i]);
    return 0;
}
