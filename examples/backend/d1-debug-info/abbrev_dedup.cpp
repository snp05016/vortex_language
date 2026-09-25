// Follows: DWARF Debugging Information Format, Version 5, section 7.5.3
// (abbreviations tables), https://dwarfstd.org/dwarf5std.html
//
// Assigns abbreviation codes to a list of debugging information entries
// (DIEs) the way a DWARF producer does. An abbreviation records a tag,
// whether the entry has children, and its (attribute, form) pairs; every
// DIE with exactly that shape reuses the code, whatever its values are.
// The first seven DIEs are the ones clang -g -O0 wrote for clamp.c in
// this chapter (compile-unit and subprogram attribute lists shortened).
#include <cstdio>
#include <map>
#include <string>
#include <tuple>
#include <vector>

struct Shape {
    std::string tag;
    bool children;
    std::vector<std::string> attrs;  // "attribute:form", in order
    bool operator<(const Shape& o) const {
        return std::tie(tag, children, attrs) < std::tie(o.tag, o.children, o.attrs);
    }
};

struct Die {
    std::string what;  // the values: never part of the shape
    Shape shape;
};

int main() {
    const std::vector<std::string> var = {"location:exprloc", "name:strx1",
                                          "decl_file:data1", "decl_line:data1",
                                          "type:ref4"};
    const std::vector<std::string> gone = {"name:strx1", "decl_file:data1",
                                           "decl_line:data1", "type:ref4"};
    const std::vector<std::string> fn = {"low_pc:addrx", "high_pc:data4", "name:strx1",
                                         "type:ref4"};
    std::vector<Die> dies = {
        {"compile_unit clamp.c", {"compile_unit", true, {"name:strx1", "stmt_list:sec_offset"}}},
        {"subprogram clamp", {"subprogram", true, fn}},
        {"formal_parameter v", {"formal_parameter", false, var}},
        {"formal_parameter lo", {"formal_parameter", false, var}},
        {"formal_parameter hi", {"formal_parameter", false, var}},
        {"variable r", {"variable", false, var}},  // same list, other tag
        {"base_type int", {"base_type", false, {"name:strx1", "encoding:data1", "byte_size:data1"}}},
        // Two entries clamp.c did not have:
        {"variable t (optimized out)", {"variable", false, gone}},
        {"subprogram zero (no children)", {"subprogram", false, fn}},
    };

    std::map<Shape, int> code_of;  // the abbreviation table being built
    for (const Die& d : dies) {
        auto [it, is_new] = code_of.try_emplace(d.shape, int(code_of.size()) + 1);
        std::printf("[%d]%s %s\n", it->second, is_new ? " new " : "     ", d.what.c_str());
    }
    std::printf("%zu DIEs, %zu abbreviations\n", dies.size(), code_of.size());
    return 0;
}
