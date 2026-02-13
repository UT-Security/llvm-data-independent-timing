# This directory is for testing the taint analysis pass.

## How to build and test

```bash
# Build the LLVM project with the taint analysis pass
# (Assuming you have already built LLVM)

# Compile the C code to LLVM IR
clang -g -O0 -S -emit-llvm -Xclang -disable-O0-optnone \
      -fno-asynchronous-unwind-tables -fno-unwind-tables \
      mem_ops.c -o mem_ops.ll

# Run the taint analysis pass
opt -S mem_ops.ll -passes=taint-annotate -taint-src=mem_secret.txt -o \
    mem_ops.annotated.ll

# Convert to machine code and run the taint analysis transform
llc -O0 -stop-after=prologepilog mem_ops.annotated.ll -o \
    mem_ops.pe.mir

llc -passes='taint-analysis-transform' -taint-output=mem_tainted.txt \
    -debug-only=taint-analysis mem_ops.pe.mir
```

Other testcases can be found in the `test` directory.