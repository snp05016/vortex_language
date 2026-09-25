// Follows: LLVM Project, "Convergence And Uniformity", sections "Uniformity"
// and "Divergent Cycle Exits".
// https://llvm.org/docs/ConvergenceAndUniformity.html
//
// A uniformity analysis, run to a fixed point, on one small kernel:
//
//   entry:  tid = thread index; n = kernel argument; c1 = tid < n
//           branch on c1 to then1 / else1
//   then1:  a = n * 2                 else1:  b = n * 3
//   merge1: r = phi(a, b); c2 = n > 4; branch on c2 to then2 / merge2
//   then2:  t = n + 1
//   merge2: s = phi(t, n)
//   loop:   i = phi(0, i1); i1 = i + 1; c3 = i1 < tid; back to loop or exit
//   exit:   e = i1
//
// Three rules make a value divergent (different across the lanes of a warp):
//   operand:   it reads a divergent value;
//   join:      it is a phi where the two sides of a divergent branch meet;
//   loop exit: it reads a value from inside a loop whose exit branch is
//              divergent, so lanes leave on different iterations.

#include <array>
#include <cstddef>
#include <cstdio>
#include <string_view>

enum class Kind { plain, phi };

struct Value {
    const char* name;
    const char* block;
    Kind kind;
    std::array<int, 2> ops;  // indices into values; -1 means a constant
};

enum class Edge { join, loop_exit };

struct Branch {
    int cond;           // the value the branch tests
    const char* block;  // where the two paths meet, or the loop's exit
    Edge edge;
};

constexpr std::array<Value, 13> values{{
    {"tid", "entry", Kind::plain, {-1, -1}},  // 0: the seed
    {"n", "entry", Kind::plain, {-1, -1}},    // 1: same for every lane
    {"c1", "entry", Kind::plain, {0, 1}},     // 2
    {"a", "then1", Kind::plain, {1, -1}},     // 3
    {"b", "else1", Kind::plain, {1, -1}},     // 4
    {"r", "merge1", Kind::phi, {3, 4}},       // 5
    {"c2", "merge1", Kind::plain, {1, -1}},   // 6
    {"t", "then2", Kind::plain, {1, -1}},     // 7
    {"s", "merge2", Kind::phi, {7, 1}},       // 8
    {"i", "loop", Kind::phi, {-1, 10}},       // 9
    {"i1", "loop", Kind::plain, {9, -1}},     // 10
    {"c3", "loop", Kind::plain, {10, 0}},     // 11
    {"e", "exit", Kind::plain, {10, -1}},     // 12
}};

constexpr std::array<Branch, 3> branches{{
    {2, "merge1", Edge::join},
    {6, "merge2", Edge::join},
    {11, "exit", Edge::loop_exit},
}};

int main() {
    std::array<bool, values.size()> divergent{};
    std::array<const char*, values.size()> why{};
    divergent[0] = true;
    why[0] = "seed";

    auto mark = [&](std::size_t v, const char* reason) {
        if (divergent[v]) return false;
        divergent[v] = true;
        why[v] = reason;
        return true;
    };

    auto in_loop = [](int op) {
        return std::string_view{values[static_cast<std::size_t>(op)].block} ==
               "loop";
    };

    // Each pass can only turn values from uniform to divergent, so the loop
    // stops after at most one pass per value.
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t v = 0; v < values.size(); ++v)
            for (int op : values[v].ops)
                if (op >= 0 && divergent[static_cast<std::size_t>(op)])
                    changed |= mark(v, "operand");
        for (const Branch& br : branches) {
            if (!divergent[static_cast<std::size_t>(br.cond)]) continue;
            for (std::size_t v = 0; v < values.size(); ++v) {
                if (std::string_view{values[v].block} != br.block) continue;
                if (br.edge == Edge::join && values[v].kind == Kind::phi)
                    changed |= mark(v, "join");
                if (br.edge == Edge::loop_exit)
                    for (int op : values[v].ops)
                        if (op >= 0 && in_loop(op))
                            changed |= mark(v, "loop exit");
            }
        }
    }

    for (std::size_t v = 0; v < values.size(); ++v) {
        std::printf("%-4s%-8s", values[v].name, values[v].block);
        if (divergent[v])
            std::printf("divergent (%s)\n", why[v]);
        else
            std::printf("uniform\n");
    }
}
