CC = ../build/bin/clang
CFLAGS = -fomit-frame-pointer -O0 -Werror -std=c18
LDFLAGS = -lsodium

# Libsodium paths
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

# Generate LLVM IR for all eval files
ir: $(EVAL_SRCS:.c=.ll)

%.ll: %.c
	$(CC) -S -emit-llvm -g $(CFLAGS) $< -o $@

clean-ir:
	rm -f *.ll

.PHONY: all fenced unfenced both clean ir clean-ir
