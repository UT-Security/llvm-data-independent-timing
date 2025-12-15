# Taint Analysis Pass - Usage Guide

This guide explains how to use the interprocedural taint analysis pass in LLVM to track tainted function arguments through your code.

## Overview

The TaintAnalysis pass performs forward dataflow analysis to track tainted values from **function arguments specified in a CSV file** across function boundaries. This is useful for:
- Security analysis (tracking untrusted input through specific functions)
- Data flow verification
- Understanding how specific function arguments propagate through your program

## Configuration: CSV File Format

The pass reads taint sources from a CSV file with the following format:

```csv
FunctionName,ArgumentIndex
```

- **FunctionName**: The name of the function
- **ArgumentIndex**: Zero-based index of the argument to track (0 = first argument, 1 = second, etc.)
- Lines starting with `#` are comments
- Empty lines are ignored

### Example CSV File (`taint_sources.csv`):

```csv
# Taint Sources Configuration
# Format: FunctionName,ArgumentIndex

# Mark argument 0 of 'process_input' as tainted
process_input,0

# Mark both arguments of 'combine_inputs' as tainted
combine_inputs,0
combine_inputs,1

# Common input functions
read,0
scanf,0
getenv,0
```

## Building

After adding the taint analysis files, rebuild LLVM:

```bash
ninja -C build
```

Or rebuild just the Analysis library:

```bash
ninja -C build LLVMAnalysis
```

## Basic Usage

### 1. Create Your CSV File

First, create a CSV file specifying which function arguments should be tracked:

```bash
cat > my_sources.csv << 'EOF'
# Track first argument of process_user_input
process_user_input,0

# Track second argument of handle_request
handle_request,1
EOF
```

### 2. Generate LLVM IR

Compile your C/C++ code to LLVM IR:

```bash
./build/bin/clang -S -emit-llvm example.c -o example.ll
```

### 3. Run Taint Analysis

Run the analysis with the CSV file:

```bash
./build/bin/opt -passes='print<taint-analysis>' \
  -taint-sources-file=my_sources.csv \
  -disable-output example.ll
```

## Example

### C Code (`example.c`):

```c
#include <stdio.h>

int process_input(int user_input) {
    // user_input is marked as tainted in CSV
    int result = user_input * 2;
    return result + 10;
}

int helper(int x) {
    return x * 3;
}

void caller() {
    int tainted = 42;  // From process_input
    int result = helper(tainted);  // Taint propagates here
    printf("%d\n", result);
}

int main() {
    int val = process_input(100);
    printf("Result: %d\n", val);
    return 0;
}
```

### CSV File (`sources.csv`):

```csv
process_input,0
```

### Commands:

```bash
# Compile to IR
./build/bin/clang -S -emit-llvm -g example.c -o example.ll

# Run analysis
./build/bin/opt -passes='print<taint-analysis>' \
  -taint-sources-file=sources.csv \
  -disable-output example.ll
```

### Output:

```
===== Taint Analysis Results =====
Taint Source Arguments: 1
  process_input::arg0

Total Tainted Instructions: 5

Tainted by process_input::arg0 (5 instructions):
  [Line 6] in process_input:   %result = mul i32 %user_input, 2
  [Line 7] in process_input:   %add = add i32 %result, 10
  [Line 7] in process_input:   ret i32 %add
  [Line 15] in caller:   %result = call i32 @helper(i32 %tainted)
  [Line 16] in caller:   call i32 (i8*, ...) @printf(i8* getelementptr(...), i32 %result)

==================================
```

## Understanding the Output

The analysis output includes:

1. **Taint Source Arguments**: Lists all function arguments marked as taint sources from the CSV
   ```
   process_input::arg0  (argument 0 of function process_input)
   ```

2. **Total Tainted Instructions**: Total count of instructions affected by taint

3. **Tainted Instructions**: Grouped by source argument, showing:
   - **Line number** (if debug info available with `-g`)
   - **Function name** where the instruction appears
   - **The LLVM IR instruction** itself

## Advanced Usage

### Multiple Sources

Track multiple function arguments:

```csv
read_data,0
write_data,1
process,0
process,2
```

### Running with Optimization Pipeline

Run taint analysis after optimizations:

```bash
./build/bin/opt -passes='default<O2>' \
  -passes-ep-optimizer-last='print<taint-analysis>' \
  -taint-sources-file=sources.csv \
  -disable-output example.ll
```

### As Part of Custom Pipeline

```bash
./build/bin/opt -passes='inline,simplifycfg,print<taint-analysis>' \
  -taint-sources-file=sources.csv \
  -disable-output example.ll
```

## Compilation with Debug Info

To get line numbers in the output, compile with `-g`:

```bash
./build/bin/clang -S -emit-llvm -g example.c -o example.ll
```

Without `-g`, you'll still see all tainted instructions, just without line numbers.

## Use Cases

1. **Security Auditing**: Track how specific untrusted inputs flow through your code
2. **API Analysis**: Understand which functions are affected by certain parameters
3. **Refactoring**: See the impact of changing a function parameter
4. **Compliance**: Verify sensitive data doesn't reach unintended locations

## Running Tests

Run the taint analysis tests:

```bash
./build/bin/llvm-lit llvm/test/Analysis/TaintAnalysis/
```

## Programmatic Usage

To use TaintAnalysis in your own LLVM tool or pass:

```cpp
#include "llvm/Analysis/TaintAnalysis.h"
#include "llvm/IR/PassManager.h"

// In your pass's run method:
TaintInfo &TI = MAM.getResult<TaintAnalysis>(M);

// Query if a value is tainted
for (Function &F : M) {
    for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
            if (TI.isTainted(&I)) {
                errs() << "Tainted instruction: " << I << "\n";
            }
        }
    }
}

// Get detailed information about all tainted values
for (const auto &Info : TI.getTaintedValuesInfo()) {
    errs() << "Tainted by: "
           << Info.SourceArg->getParent()->getName()
           << "::arg" << Info.SourceArg->getArgNo() << "\n";
    if (Info.LineNumber > 0) {
        errs() << "  at line " << Info.LineNumber << "\n";
    }
}
```

## Limitations

- **Conservative Analysis**: May over-approximate taint (false positives)
- **Path-Insensitive**: Doesn't consider control flow conditions
- **No Sanitization**: Doesn't recognize when data is validated/sanitized
- **CSV-Only Sources**: Only function arguments specified in CSV are tracked
- **Requires Explicit Configuration**: You must create the CSV file

## Troubleshooting

**"Warning: No taint sources identified"**:
- Make sure you specified `-taint-sources-file=your_file.csv`
- Check that function names in CSV match actual function names in IR
- Verify argument indices are valid (0-based)

**No line numbers in output**:
- Compile with `-g` flag to include debug information
- Use: `clang -S -emit-llvm -g source.c -o output.ll`

**Pass not found**:
- Rebuild LLVM after adding files: `ninja -C build`
- Check that all files are in correct locations

**CSV parsing errors**:
- Ensure format is: `FunctionName,ArgumentIndex`
- No spaces around comma (or trim them)
- Use `#` for comments

## Tips

1. **Start Small**: Begin with a simple CSV file tracking 1-2 functions
2. **Use Comments**: Document why each source is tracked in the CSV
3. **Compile with -g**: Always use debug info for better output
4. **Check Function Names**: Use `llvm-dis` to see actual function names in IR
5. **Iterative Analysis**: Run analysis, refine CSV, repeat

## Example Workflow

```bash
# 1. Create CSV
cat > sources.csv << 'EOF'
user_input_handler,0
process_request,1
EOF

# 2. Compile with debug info
./build/bin/clang -S -emit-llvm -g myapp.c -o myapp.ll

# 3. Run analysis
./build/bin/opt -passes='print<taint-analysis>' \
  -taint-sources-file=sources.csv \
  -disable-output myapp.ll 2>&1 | tee results.txt

# 4. Review results
less results.txt
```

## References

- LLVM Pass Manager: https://llvm.org/docs/NewPassManager.html
- Writing LLVM Passes: https://llvm.org/docs/WritingAnLLVMPass.html
- LLVM IR Language Reference: https://llvm.org/docs/LangRef.html
