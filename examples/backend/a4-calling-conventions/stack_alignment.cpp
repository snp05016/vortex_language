// Follows: ARM-software/abi-aa, AAPCS64, section 6.4.2 "The Stack" (the
// stack must be quad-word aligned at a public interface).
// <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
// Follows: x86-64-ABI (SysV AMD64 psABI), section 3.2.2 "The Stack Frame"
// ("%rsp + 8 is always a multiple of 16 when control is transferred to the
// function entry point"). <https://gitlab.com/x86-psABIs/x86-64-ABI>
//
// Both ABIs require the stack pointer to be 16-byte aligned at a call, but
// they disagree about where the return address lives, so what the callee
// sees at entry differs. AArch64's `bl` leaves the return address in the
// link register and never touches the stack pointer; x86-64's `call`
// pushes eight bytes of return address. A compiler laying out stack
// arguments has to account for that difference.

#include <cstdio>
#include <initializer_list>

struct Rule {
    const char* name;
    int call_pushes_return_address;  // 0 for bl, 8 for call
};

bool aligned_at_the_call(int sp_before_call, int stack_argument_bytes) {
    int sp_at_call = sp_before_call - stack_argument_bytes;
    return sp_at_call % 16 == 0;
}

bool aligned_at_callee_entry(const Rule& abi, int sp_at_call) {
    int sp_at_entry = sp_at_call - abi.call_pushes_return_address;
    int required_remainder = abi.call_pushes_return_address == 0 ? 0 : 8;
    return sp_at_entry % 16 == required_remainder;
}

int main() {
    Rule aarch64{"AArch64 (bl)", 0};
    Rule x86_64{"x86-64 (call)", 8};
    int sp_before_call = 64;  // any 16-aligned starting point

    for (int bytes : {0, 8, 16, 24}) {
        for (const auto& abi : {aarch64, x86_64}) {
            bool call_ok = aligned_at_the_call(sp_before_call, bytes);
            int sp_at_call = sp_before_call - bytes;
            bool entry_ok = call_ok && aligned_at_callee_entry(abi, sp_at_call);
            std::printf("%-14s %2d stack-argument bytes: call %s, entry %s\n",
                        abi.name, bytes,
                        call_ok ? "aligned" : "MISALIGNED",
                        entry_ok ? "aligned" : "MISALIGNED");
        }
    }
}
