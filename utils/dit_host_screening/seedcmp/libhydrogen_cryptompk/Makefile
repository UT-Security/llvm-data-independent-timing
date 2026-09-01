PREFIX ?= /usr/local
WFLAGS ?= -Wall -Wextra -Wmissing-prototypes -Wdiv-by-zero -Wbad-function-cast -Wcast-align -Wcast-qual -Wfloat-equal -Wmissing-declarations -Wnested-externs -Wno-unknown-pragmas -Wpointer-arith -Wredundant-decls -Wstrict-prototypes -Wswitch-enum -Wno-type-limits
CFLAGS ?= -Os -march=native -fno-exceptions $(WFLAGS) -fPIC
CFLAGS += -I.
OBJ = hydrogen.o
AR ?= ar
RANLIB ?= ranlib

SRC = \
	hydrogen.c \
	hydrogen.h \
	impl/common.h \
	impl/core.h \
	impl/gimli-core.h \
	impl/hash.h \
	impl/hydrogen_p.h \
	impl/kdf.h \
	impl/kx.h \
	impl/pwhash.h \
	impl/random.h \
	impl/secretbox.h \
	impl/sign.h \
	impl/x25519.h

all: lib

lib: libhydrogen.so

$(OBJ): $(SRC)

libhydrogen.so: $(OBJ)
	$(CC) -shared -o $@ $^

.PHONY: clean

clean:
	rm -f libhydrogen.so $(OBJ)
	rm -f tests/tests tests/*.done

check: test

distclean: clean
