// interproc_ops.c — exercises interprocedural taint propagation.
//
// Build & test:
//   clang -g -O2 -S -emit-llvm \
//         -fno-asynchronous-unwind-tables -fno-unwind-tables \
//         interproc_ops.c -o interproc_ops.ll
//
//   opt -S interproc_ops.ll -passes=taint-annotate \
//       -taint-src=interproc_secret.txt -o interproc_ops.annotated.ll
//
//   llc -O2 -stop-after=prologepilog interproc_ops.annotated.ll \
//       -o interproc_ops.pe.mir
//
//   llc -passes='taint-analysis-transform' \
//       -taint-output=interproc_tainted.txt \
//       -debug-only=taint-analysis interproc_ops.pe.mir

#define NOINLINE __attribute__((noinline))

// --- 1. Simple call: tainted arg passed to callee, callee returns it ---
// Taint flows: caller_simple → identity → back to caller_simple
NOINLINE int identity(int x) {
  return x;
}

NOINLINE int caller_simple(int secret) {
  return identity(secret);
}

// --- 2. Multi-hop: taint flows through a chain of calls ---
// Taint flows: caller_chain → add_one → double_it → back up
NOINLINE int add_one(int x) {
  return x + 1;
}

NOINLINE int double_it(int x) {
  return x * 2;
}

NOINLINE int caller_chain(int secret) {
  int a = add_one(secret);
  int b = double_it(a);
  return b;
}

// --- 3. Callee modifies tainted pointer's memory ---
// Taint flows: caller_ptr → write_to_ptr (stores tainted value)
NOINLINE void write_to_ptr(int *dst, int val) {
  *dst = val;
}

NOINLINE int caller_ptr(int secret) {
  int local;
  write_to_ptr(&local, secret);
  return local;
}

// --- 4. Callee returns clean value even with tainted arg ---
// Taint should NOT propagate to return (callee ignores tainted arg)
NOINLINE int ignore_arg(int tainted, int clean) {
  return clean;
}

NOINLINE int caller_clean(int secret, int safe) {
  return ignore_arg(secret, safe);
}

// --- 5. Indirect call through function pointer ---
// Conservative: taint should propagate through unknown callee
typedef int (*func_ptr)(int);

NOINLINE int apply(func_ptr fn, int x) {
  return fn(x);
}

NOINLINE int caller_indirect(int secret) {
  return apply(identity, secret);
}

// --- 6. External function call ---
// Conservative: taint propagates through the external call boundary.
NOINLINE int external_process(int x) {
  return x;
}

NOINLINE int caller_external(int secret) {
  return external_process(secret);
}

// --- 7. Recursive function with tainted input ---
// Taint should propagate through recursive calls
NOINLINE int factorial(int n) {
  if (n <= 1)
    return 1;
  return n * factorial(n - 1);
}

NOINLINE int caller_recursive(int secret) {
  return factorial(secret);
}

int main(void) {
  caller_simple(42);
  caller_chain(10);
  caller_ptr(99);
  caller_clean(100, 200);
  caller_indirect(55);
  caller_recursive(5);
  return 0;
}
