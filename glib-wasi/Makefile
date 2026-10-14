# -----------------------------------------------------------------------
#  Makefile – build libglib-wasi.a (minimal GLib shim for wasm32-wasi)
#
#  Usage:
#    make                     # builds libglib-wasi.a
#    make test                # builds + links a small test program
#    make clean               # removes build artefacts
#
#  Override WASI_SDK on the command line if it is not in /opt/wasi-sdk:
#    make WASI_SDK=/path/to/wasi-sdk
# -----------------------------------------------------------------------

WASI_SDK  ?= /opt/wasi-sdk
CC         = $(WASI_SDK)/bin/wasm32-wasip2-clang
AR         = $(WASI_SDK)/bin/ar

CFLAGS    := -O2 -Wall -D__wasi__ -DWASI_PLATFORM -I$(CURDIR)

# -----------------------------------------------------------------------
#  Source / object lists
# -----------------------------------------------------------------------

SRCDIR    := src
BUILDDIR  := build

SRCS      := $(wildcard $(SRCDIR)/*.c)
OBJS      := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))
DEPS      := $(OBJS:.o=.d)

LIB       := libglib-wasi.a

# -----------------------------------------------------------------------
#  Header list (for dependency tracking fallback)
# -----------------------------------------------------------------------

HDRS      := $(wildcard glib/*.h)

# -----------------------------------------------------------------------
#  Default target
# -----------------------------------------------------------------------

.PHONY: all clean test

all: $(LIB)

# -----------------------------------------------------------------------
#  Static library
# -----------------------------------------------------------------------

$(LIB): $(OBJS)
	$(AR) rcs $@ $^

# -----------------------------------------------------------------------
#  Compile sources → build/*.o  (with automatic header dependency tracking)
# -----------------------------------------------------------------------

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Pull in generated .d dependency files (if they exist)
-include $(DEPS)

# -----------------------------------------------------------------------
#  Test target
# -----------------------------------------------------------------------

TEST_SRC  := test/test_glib.c
TEST_BIN  := build/test_glib.wasm

test: $(LIB) | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(CURDIR) -o $(TEST_BIN) $(TEST_SRC) -L$(CURDIR) -lglib-wasi

# -----------------------------------------------------------------------
#  Clean
# -----------------------------------------------------------------------

clean:
	rm -rf $(BUILDDIR) $(LIB)
