// mem_ops.c — exercises various memory operations for taint analysis testing.
//
// Build & test:
//   clang -O0 -S -emit-llvm -Xclang -disable-O0-optnone \
//         -fno-asynchronous-unwind-tables -fno-unwind-tables mem_ops.c -o
//         mem_ops.ll
//   opt -S mem_ops.ll -passes=taint-annotate -taint-src=mem_secret.txt -o
//   mem_ops.annotated.ll llc -O0 -stop-after=prologepilog mem_ops.annotated.ll
//   -o mem_ops.pe.mir
//   llc -passes='taint-analysis-transform' -taint-output=mem_tainted.txt \
//       -debug-only=taint-analysis mem_ops.pe.mir

// --- 1. Simple store/load through a local variable ---
// Taint arg 'secret', store it, load it back.
int store_load(int secret, int clean) {
  int tmp = secret; // store secret → tmp, load tmp
  return tmp + clean;
}

// --- 2. Array access with tainted index ---
// Taint 'idx': the index used to access the array is tainted.
int array_index(int idx, int *arr) {
  return arr[idx]; // GEP + load using tainted index
}

// --- 3. Store through tainted pointer ---
// Taint 'ptr': the destination pointer is tainted.
void store_via_ptr(int *ptr, int value) {
  *ptr = value; // store through tainted pointer
}

// --- 4. Load through tainted pointer ---
// Taint 'ptr': the source pointer is tainted.
int load_via_ptr(int *ptr) {
  return *ptr; // load through tainted pointer
}

// --- 5. Struct field access ---
struct Pair {
  int x;
  int y;
};

// Taint 'p' (the struct pointer). Access both fields.
int struct_access(struct Pair *p) {
  int a = p->x; // GEP(0) + load
  int b = p->y; // GEP(1) + load
  return a + b;
}

// --- 6. Multi-level: tainted value flows through several locals ---
int multi_hop(int secret) {
  int a = secret + 1;
  int b = a * 2;
  int c = b - 3;
  return c;
}

// --- 7. Conditional store: tainted value stored on one branch only ---
int cond_store(int secret, int flag) {
  int result;
  if (flag)
    result = secret; // tainted store
  else
    result = 0;  // clean store
  return result; // load — should be tainted (conservative)
}

// --- 8. memcpy-like manual copy (byte-by-byte) ---
void byte_copy(char *dst, char *src, int n) {
  for (int i = 0; i < n; i++)
    dst[i] = src[i]; // load from tainted src, store to dst
}

int main() {
  int arr[] = {10, 20, 30, 40, 50};
  struct Pair p = {1, 2};
  int val = 42;
  char buf[8] = {0};

  store_load(100, 200);
  array_index(2, arr);
  store_via_ptr(&val, 99);
  load_via_ptr(&val);
  struct_access(&p);
  multi_hop(777);
  cond_store(999, 1);
  byte_copy(buf, (char *)&val, 4);

  return 0;
}
