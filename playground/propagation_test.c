#include <stdint.h>

// Propagation-power probe: ONE secret enters at the top (top_entry arg 0) and
// must flow DOWN through 6 noinline layers, crossing a different channel at each
// hop. A powerful interprocedural analysis instruments every layer from that
// single seed.
//
//   L6 register arg      -> secret-dependent arithmetic (the actual leak)
//   L5 return value      -> secret flows back UP a return
//   L4 memory write      -> callee writes secret through a caller pointer
//   L3 memory reload     -> caller reloads the secret it never saw written
//   L2 plain pass-down   -> register arg
//   L1 plain pass-down   -> register arg
//   top SEED             -> arg 0 tainted

__attribute__((noinline)) int  L6_mul(int x)            { return x * 7; }
__attribute__((noinline)) int  L5_ret(int x)            { return L6_mul(x) + 1; }
__attribute__((noinline)) void L4_write(int *out, int x){ *out = L5_ret(x); }
__attribute__((noinline)) int  L3_reload(int x) {
    int buf;
    L4_write(&buf, x);          // secret written into buf by the callee
    return buf * 3;             // reloaded here — the callee->caller-through-memory hop
}
__attribute__((noinline)) int  L2_pass(int x)           { return L3_reload(x); }
__attribute__((noinline)) int  L1_pass(int x)           { return L2_pass(x); }
__attribute__((noinline)) int  top_entry(int secret)    { return L1_pass(secret); }

// A sibling that never touches the secret — must stay CLEAN (no false positive).
__attribute__((noinline)) int  unrelated(int a, int b)  { return a + b; }
int driver(int s)  { return top_entry(s) + unrelated(1, 2); }
