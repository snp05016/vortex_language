// Follows: Arm, AAPCS64, "Homogeneous Aggregates" and "Parameter passing
//   rules" (B.3, B.4, C.2, C.12).
// Follows: x86-64 psABI, "Parameter Passing" (classification of aggregates).
//
// Decides how a small C struct passed by value travels, under AAPCS64 and
// under SysV AMD64, when enough argument registers are free. Fields are
// scalars only; no unions, bit-fields or over-aligned members.
#include <cstdio>
#include <string>
#include <vector>

struct Field { bool is_float; int size; };  // laid out in order, aligned

struct Shape { const char* name; std::vector<Field> fields; };

int size_of(const Shape& s, std::vector<int>* offsets = nullptr) {
    int off = 0, align = 1;
    for (const Field& f : s.fields) {
        off = (off + f.size - 1) / f.size * f.size;
        if (offsets) offsets->push_back(off);
        off += f.size;
        if (f.size > align) align = f.size;
    }
    return (off + align - 1) / align * align;
}

std::string aapcs64(const Shape& s) {
    bool hfa = s.fields.size() <= 4;  // up to four members, one float type
    for (const Field& f : s.fields)
        hfa = hfa && f.is_float && f.size == s.fields[0].size;
    if (hfa) return "HFA: one v register per member";
    int size = size_of(s);
    if (size > 16) return "copied by the caller; x register holds its address";
    int n = (size + 7) / 8;
    return std::to_string(n) + (n == 1 ? " x register" : " x registers");
}

std::string sysv(const Shape& s) {
    std::vector<int> offsets;
    int size = size_of(s, &offsets);
    if (size > 16) return "MEMORY: copied onto the stack";
    std::string out;
    for (int eightbyte = 0; eightbyte * 8 < size; ++eightbyte) {
        bool all_float = true;  // INTEGER wins over SSE within one eightbyte
        for (std::size_t i = 0; i < s.fields.size(); ++i)
            if (offsets[i] / 8 == eightbyte) all_float &= s.fields[i].is_float;
        out += all_float ? "SSE " : "INTEGER ";
    }
    return out + "(one register per eightbyte)";
}

int main() {
    const Field f32{true, 4}, f64{true, 8}, i32{false, 4}, i64{false, 8};
    const std::vector<Shape> shapes = {
        {"{float x, y, z}", {f32, f32, f32}},
        {"{float v[6]}", {f32, f32, f32, f32, f32, f32}},
        {"{int n; float w}", {i32, f32}},
        {"{double d; long n}", {f64, i64}},
        {"{long a, b, c}", {i64, i64, i64}},
    };
    for (const Shape& s : shapes) {
        std::printf("%-20s %2d bytes\n", s.name, size_of(s));
        std::printf("  AAPCS64: %s\n", aapcs64(s).c_str());
        std::printf("  SysV:    %s\n", sysv(s).c_str());
    }
}
