#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

// Models the part of static linking that chooses which archive members to
// pull in: symbol resolution across separately compiled pieces, not fixups
// inside one file (b3's two_pass_fixups.cpp is that other half).
//
// A linker starts with the object files the user named directly; each has
// symbols it defines and symbols it only references (undefined). An archive
// (a .a file, "a table of contents" over many .o members) is searched only
// for members that define a symbol still undefined: a member never pulled in
// for a symbol nobody needs. Because pulling in one member can introduce new
// undefined references, the search runs to a fixed point, not just once.
//
// Follows: Levine, "Linkers and Loaders", the chapters on archives and on
// symbol resolution (https://www.iecc.com/linker/).

struct Member {
    std::string name;
    std::set<std::string> defines;
    std::set<std::string> references;
};

int main() {
    // The objects named directly on the link line: already "pulled in".
    std::set<std::string> defined = {"main"};
    std::set<std::string> undefined = {"multiply", "print"};

    // An archive, searched only on demand, in this order. extra.o is listed
    // before iolib.o, the member that needs it: a linker that scanned the
    // archive only once, left to right, would reach extra.o while "flush"
    // is still not wanted, skip it, and later fail to resolve "flush" at
    // all. Running the scan to a fixed point (pass 2 here) is what lets
    // archive order be imperfect and still link.
    std::vector<Member> archive = {
        {"extra.o", {"flush"}, {}},
        {"mathlib.o", {"multiply"}, {}},
        {"iolib.o", {"print"}, {"flush"}},
    };
    std::vector<bool> pulled(archive.size(), false);

    int pass = 0;
    bool progress = true;
    while (progress) {
        progress = false;
        ++pass;
        for (std::size_t i = 0; i < archive.size(); ++i) {
            if (pulled[i])
                continue;
            const Member &m = archive[i];
            bool needed = false;
            for (const auto &sym : m.defines)
                if (undefined.count(sym))
                    needed = true;
            if (!needed)
                continue;

            pulled[i] = true;
            progress = true;
            std::printf("pass %d: pulling %s (defines", pass, m.name.c_str());
            for (const auto &sym : m.defines) {
                std::printf(" %s", sym.c_str());
                undefined.erase(sym);
                defined.insert(sym);
            }
            std::printf(")\n");
            for (const auto &sym : m.references)
                if (!defined.count(sym))
                    undefined.insert(sym);
        }

        std::printf("undefined after pass %d:", pass);
        if (undefined.empty()) {
            std::printf(" (none)\n");
        } else {
            for (const auto &sym : undefined)
                std::printf(" %s", sym.c_str());
            std::printf("\n");
        }
    }

    std::printf(undefined.empty() ? "link successful\n"
                                   : "link failed: unresolved symbols\n");
    return 0;
}
