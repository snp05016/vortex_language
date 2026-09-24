#include <iostream>
#include <string>

// Models lazy PLT binding for one imported function. A call to an imported
// function does not call it directly: it calls a small stub (the PLT entry),
// which reads a pointer from the global offset table (the GOT). The very
// first time, that GOT slot still points back into the stub's own resolver,
// which finds the real address, overwrites the GOT slot with it, then jumps
// on to the target. Every later call reads the now-correct GOT slot and
// reaches the target with no resolver in the path.
//
// Real addresses are never printed here (the docs never show pointer
// values); this prints which path a call took instead.
//
// Follows: MaskRay (Fangrui Song), "All about Procedure Linkage Table"
// (https://maskray.me/blog/all-about-procedure-linkage-table).

struct Import {
    std::string name;
    bool resolved = false;   // has the GOT slot been rewritten yet?
};

// What the PLT stub for this import does, on every call.
void call_through_plt(Import &imp) {
    if (!imp.resolved) {
        std::cout << "call " << imp.name
                  << ": PLT stub -> GOT slot (unresolved) -> resolver\n";
        std::cout << "  resolver finds " << imp.name
                  << ", rewrites the GOT slot, jumps to it\n";
        imp.resolved = true;
    } else {
        std::cout << "call " << imp.name
                  << ": PLT stub -> GOT slot (resolved) -> " << imp.name
                  << ", directly\n";
    }
}

int main() {
    Import print_fn{"print"};
    Import sqrt_fn{"sqrt_impl"};

    call_through_plt(print_fn);   // first call: pays for resolution
    call_through_plt(print_fn);   // second call: GOT slot already correct
    call_through_plt(sqrt_fn);    // a different import starts unresolved too
    call_through_plt(sqrt_fn);
    call_through_plt(print_fn);   // still resolved from the first call
    return 0;
}
