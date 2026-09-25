// Which archive members does a traditional Unix linker pull in, and why
// does the order of archives on the command line matter?
//
// The linker keeps a set of undefined symbols and visits the inputs left to
// right. An object file is always included. An archive is searched only at
// its own position: a member is pulled in when it defines a symbol that is
// undefined right now, and the search of that archive repeats until it
// pulls nothing more. An archive is never revisited for needs created later.
//
// Follows: Ian Lance Taylor, "Linkers part 11" (archives), and the GNU ld
// manual's description of -l and --start-group.

#include <cstdio>
#include <set>
#include <string>
#include <vector>

struct Member {
    std::string name;
    std::set<std::string> defines;
    std::set<std::string> uses;
};

struct Input {
    std::string name;
    std::vector<Member> members;  // an object file is an archive of one,
    bool archive;                 // but it is included unconditionally
};

void add(const Member &m, std::set<std::string> &defined,
         std::set<std::string> &undefined) {
    for (const auto &s : m.defines) {
        defined.insert(s);
        undefined.erase(s);
    }
    for (const auto &s : m.uses)
        if (!defined.count(s))
            undefined.insert(s);
}

void link(const std::vector<Input> &line) {
    std::printf("link:");
    for (const auto &in : line)
        std::printf(" %s", in.name.c_str());
    std::printf("\n");

    std::set<std::string> defined, undefined;
    for (const auto &in : line) {
        if (!in.archive) {
            add(in.members[0], defined, undefined);
            continue;
        }
        std::vector<bool> pulled(in.members.size(), false);
        for (int pass = 1, progress = 1; progress; ++pass) {
            progress = 0;
            for (std::size_t i = 0; i < in.members.size(); ++i) {
                if (pulled[i])
                    continue;
                bool wanted = false;
                for (const auto &s : in.members[i].defines)
                    wanted = wanted || undefined.count(s) > 0;
                if (!wanted)
                    continue;
                pulled[i] = true;
                progress = 1;
                add(in.members[i], defined, undefined);
                std::printf("  %s pass %d: pull %s\n", in.name.c_str(), pass,
                            in.members[i].name.c_str());
            }
        }
    }

    std::printf("  undefined at the end:");
    for (const auto &s : undefined)
        std::printf(" %s", s.c_str());
    std::printf(undefined.empty() ? " (none)\n  link succeeds\n"
                                  : "\n  link fails\n");
}

int main() {
    const Input main_o{"main.o", {{"main.o", {"main"}, {"parse", "show"}}}, false};
    // pad.o comes first, so the need that show.o creates for "pad" is only
    // met on a second pass over the same archive.
    const Input libtext{"libtext.a",
                        {{"pad.o", {"pad"}, {}},
                         {"show.o", {"show"}, {"pad"}},
                         {"trim.o", {"trim"}, {}}},
                        true};
    const Input libparse{"libparse.a", {{"parse.o", {"parse"}, {"trim"}}}, true};

    link({main_o, libtext, libparse});  // parse.o needs trim too late
    link({main_o, libparse, libtext});  // every need arrives before its archive
}
