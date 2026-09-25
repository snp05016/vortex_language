// Lazy and eager binding through a PLT and GOT, modelled with real function
// pointers. Each import owns one GOT slot. Lazily, every slot starts out
// pointing at the resolver (standing in for PLT[0], which calls the
// dynamic linker). The stub records which slot it came from, as AArch64's
// PLT does in x16, and jumps through the slot. The resolver looks the name
// up in the library's export table, overwrites the slot and calls on.
// Eager binding does every lookup before the program's first call.
//
// Follows: Arm, "System V ABI for the Arm 64-bit Architecture", section
// "Procedure Linkage Table".

#include <cstdio>
#include <cstring>

using Fn = int (*)(int);

// The shared library: three functions and the table that exports them.
int square(int x) { return x * x; }
int negate(int x) { return -x; }
int cube(int x) { return x * x * x; }
struct Export {
    const char *name;
    Fn fn;
};
constexpr Export kLibrary[] = {
    {"cube", cube}, {"negate", negate}, {"square", square}};

// The program: the names it imports, one GOT slot each. It imports cube
// but, on this run, never calls it.
constexpr const char *kImports[] = {"square", "negate", "cube"};
constexpr int kSlots = 3;
Fn got[kSlots];
int ip0 = -1;  // the slot the current stub came from
int lookups = 0;

Fn lookup(const char *name) {
    ++lookups;
    for (const Export &e : kLibrary)
        if (std::strcmp(e.name, name) == 0)
            return e.fn;
    return nullptr;  // a real loader stops the program here
}

int resolver(int arg) {
    got[ip0] = lookup(kImports[ip0]);
    std::printf("  resolver: slot %d now holds %s\n", ip0, kImports[ip0]);
    return got[ip0](arg);
}

int call_via_plt(int slot, int arg) {
    ip0 = slot;
    return got[slot](arg);
}

void run(const char *mode) {
    std::printf("%s binding\n", mode);
    const int calls[][2] = {{0, 3}, {0, 4}, {1, 5}, {0, 6}};
    for (const auto &c : calls)
        std::printf("  %s(%d) = %d\n", kImports[c[0]], c[1],
                    call_via_plt(c[0], c[1]));
    std::printf("  lookups: %d for 4 calls\n", lookups);
}

int main() {
    for (Fn &slot : got)
        slot = resolver;  // what the loader leaves in each slot
    lookups = 0;
    run("lazy");

    lookups = 0;
    for (int i = 0; i < kSlots; ++i)
        got[i] = lookup(kImports[i]);  // all binds before the first call
    run("eager");
}
