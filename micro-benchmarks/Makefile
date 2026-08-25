CC = ../build/bin/clang

# TIMING flags, for the benchmark harnesses. -O0 here is deliberate and should
# NOT be raised: the committed cycle counts in {,un}fenced_output.txt were
# produced with it, and changing it makes new numbers incomparable with them.
# The harness is not what is being measured; the libsodium call inside the
# rdtsc/rdtscp window is.
CFLAGS = -fomit-frame-pointer -O0 -Werror -std=c18

# ANALYSIS flags, for emitting IR to run the taint analysis over. MUST be
# optimized. At -O0 clang spills every argument to an alloca, the taint
# analysis's use-list closure dies at that store, and the analysis reports
# essentially nothing (measured on hello.c: 1 tainted instruction at -O0 vs 22
# at -O2). -O0 also implies optnone, so mem2reg cannot repair it afterwards.
# See ../docs/fences/design/precision-and-soundness.md section 2.1.
IRFLAGS = -fomit-frame-pointer -O2 -Werror -std=c18

LDFLAGS = -lsodium

# The harnesses are x86-64 ONLY: START/STOP_CYCLE_TIMER use rdtsc/rdtscp with
# cpuid serialization and %rax/%rbx/%rcx/%rdx clobbers. They do not compile for
# AArch64 at any optimization level. To emit IR on an arm64 host, cross-target:
#   make ir XTARGET='--target=x86_64-apple-macos13' SYSROOT="-isysroot $(xcrun --show-sdk-path)"
# On macOS a from-source clang also needs SYSROOT even when targeting the host
# (see CLAUDE.md, macOS clang.cfg note). Both default to empty, so a Linux
# x86-64 build is unaffected.
XTARGET =
SYSROOT =

# Libsodium paths
#
# IMPORTANT: the fenced/unfenced archives below must ALSO be built optimized,
# for the same reason IRFLAGS is. The fence pass acts on the taint analysis's
# output, so an -O0 libsodium receives almost no fences. The build scripts that
# produce these archives are NOT in this repository; whatever level they used,
# it was not -O0 (an -O0 library could not have cost the measured 90x).
# Recovering the exact flags is required to reproduce that number.
LIBSODIUM_DIR = /home/rgangar/Documents/libsodium-stable
LIBSODIUM_FENCED = $(LIBSODIUM_DIR)/bitcode/libsodium_fenced.a
LIBSODIUM_UNFENCED = $(LIBSODIUM_DIR)/bitcode/libsodium_unfenced.a
LIBSODIUM_INCLUDES = -I$(LIBSODIUM_DIR)/src/libsodium/include

# Find all eval_*.c files and create corresponding executable names
EVAL_SRCS = $(wildcard eval_*.c)
EVAL_BINS = $(EVAL_SRCS:.c=)
EVAL_FENCED_BINS = $(EVAL_SRCS:.c=_fenced)
EVAL_UNFENCED_BINS = $(EVAL_SRCS:.c=_unfenced)

# Default target: build all eval binaries (system libsodium)
all: $(EVAL_BINS)

# Build all fenced versions
fenced: $(EVAL_FENCED_BINS)

# Build all unfenced versions (our build, no fences)
unfenced: $(EVAL_UNFENCED_BINS)

# Build both fenced and unfenced versions
both: fenced unfenced

# Pattern rule for building eval_* executables (system libsodium)
eval_%: eval_%.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# Pattern rule for building eval_*_fenced executables (fenced libsodium)
eval_%_fenced: eval_%.c $(LIBSODIUM_FENCED)
	$(CC) $(CFLAGS) $(LIBSODIUM_INCLUDES) $< $(LIBSODIUM_FENCED) -o $@

# Pattern rule for building eval_*_unfenced executables (unfenced libsodium)
eval_%_unfenced: eval_%.c $(LIBSODIUM_UNFENCED)
	$(CC) $(CFLAGS) $(LIBSODIUM_INCLUDES) $< $(LIBSODIUM_UNFENCED) -o $@

# Build libsodium_test
libsodium_test: libsodium_test.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# Build hello (no libsodium needed)
hello: hello.c
	$(CC) $(CFLAGS) $< -o $@

# Clean up all binaries
clean:
	rm -f $(EVAL_BINS) $(EVAL_FENCED_BINS) $(EVAL_UNFENCED_BINS) libsodium_test hello

# Generate LLVM IR for all eval files, for the taint analysis.
# Uses IRFLAGS (-O2), NOT CFLAGS (-O0): see the note at the top of this file.
ir: $(EVAL_SRCS:.c=.ll)

%.ll: %.c
	$(CC) -S -emit-llvm -g $(IRFLAGS) $(XTARGET) $(SYSROOT) $< -o $@

clean-ir:
	rm -f *.ll

.PHONY: all fenced unfenced both clean ir clean-ir
