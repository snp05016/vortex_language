// Follows: DWARF Debugging Information Format, Version 5, section 7.5.3
// (the abbreviations table), https://dwarfstd.org/dwarf5std.html
//
// Models why DWARF gives each debugging information entry (DIE) an
// abbreviation code instead of writing its tag and attribute list out in
// full. Two DIEs with the same tag and the same set of attributes (even
// if the attribute values differ) can share one abbreviation; only the
// values change per DIE. This builds a tiny DIE tree for a two-function
// toy program and counts how many distinct shapes it actually needs.
#include <cstdio>
#include <map>
#include <string>
#include <vector>

enum class Tag { CompileUnit, Subprogram, Parameter, Variable };

// A DIE's "shape" is its tag plus which attributes it carries. Two DIEs
// with the same shape can reuse one abbreviation; DWARF does not care
// that their attribute values (a name, a type, a location) differ.
struct Shape {
    Tag tag;
    bool has_name;
    bool has_type;
    bool has_location;
    bool operator<(const Shape& o) const {
        return std::tie(tag, has_name, has_type, has_location) <
               std::tie(o.tag, o.has_name, o.has_type, o.has_location);
    }
};

struct Die {
    std::string label;  // for printing only; not part of the shape
    Shape shape;
};

int main() {
    // One compile unit, two functions, each with a parameter and a
    // local variable: eight DIEs from four distinct shapes.
    std::vector<Die> dies = {
        {"compile_unit",       {Tag::CompileUnit, true, false, false}},
        {"fn average3",        {Tag::Subprogram,  true, true,  true}},
        {"  param a",          {Tag::Parameter,   true, true,  true}},
        {"  local sum",        {Tag::Variable,    true, true,  true}},
        {"fn clamp",           {Tag::Subprogram,  true, true,  true}},
        {"  param value",      {Tag::Parameter,   true, true,  true}},
        {"  local result",     {Tag::Variable,    true, true,  true}},
        {"  local unused_tmp", {Tag::Variable,    true, false, false}},
    };

    std::map<Shape, int> abbrev_of_shape;
    std::vector<int> abbrev_of_die;
    for (const Die& d : dies) {
        auto it = abbrev_of_shape.find(d.shape);
        if (it == abbrev_of_shape.end()) {
            int code = static_cast<int>(abbrev_of_shape.size()) + 1;
            it = abbrev_of_shape.emplace(d.shape, code).first;
        }
        abbrev_of_die.push_back(it->second);
    }

    for (std::size_t i = 0; i < dies.size(); ++i) {
        std::printf("abbrev %d  %s\n", abbrev_of_die[i], dies[i].label.c_str());
    }
    std::printf("%zu DIEs, %zu distinct abbreviations\n",
                dies.size(), abbrev_of_shape.size());
    return 0;
}
