// Follows: Arm, AAPCS64, "Stack constraints at a public interface"
//   (SP mod 16 = 0) and "Subroutine calls" (BL puts the return address in LR).
// Follows: x86-64 psABI, "The Stack Frame" (16-byte aligned before the call
//   instruction; rsp + 8 a multiple of 16 at the function entry).
//
// A caller that is itself a function must reserve its locals and outgoing
// stack arguments so that the stack pointer is 16-byte aligned at its own
// call. This computes how much it reserves on each ISA, starting from the
// state the ABI guarantees at the caller's entry.
#include <cstdio>
#include <initializer_list>

int round_up(int n, int to) { return (n + to - 1) / to * to; }

struct Isa {
    const char* name;
    int entry_offset;  // sp mod 16 at a function's entry
    int call_pushes;   // bytes the call instruction pushes
};

// Bytes the caller subtracts from sp so that sp mod 16 == 0 at its call.
int reserve(const Isa& isa, int needed) {
    return round_up(needed + isa.entry_offset, 16) - isa.entry_offset;
}

int main() {
    const Isa isas[] = {{"AArch64 (bl)", 0, 0}, {"x86-64 (call)", 8, 8}};
    const int frame_record = 16;  // saved frame pointer and return address
    for (const Isa& isa : isas) {
        std::printf("%s: sp mod 16 at entry = %d\n", isa.name,
                    isa.entry_offset);
        for (int stack_args : {0, 8, 16, 24, 32}) {
            // AArch64 saves x29 and x30 in the frame. On x86-64 the return
            // address is already on the stack; the caller keeps 8 bytes of
            // its own (a saved rbp, or the padding clang pushes).
            int needed = stack_args + (isa.call_pushes ? 8 : frame_record);
            int r = reserve(isa, needed);
            int sp_at_call = (isa.entry_offset - r % 16 + 16) % 16;
            int sp_in_callee = (sp_at_call - isa.call_pushes + 16) % 16;
            std::printf("  %2d stack-argument bytes: reserve %2d, sp mod 16 at "
                        "the call = %d, in the callee = %d\n",
                        stack_args, r, sp_at_call, sp_in_callee);
        }
    }
}
