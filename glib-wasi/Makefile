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
AR         = $(WASI_SDK)/bin/ar

# THREADS=1: for QEMU's multi-threaded build (wasm32-wasip1-threads), with
# real locks; built separately, as libglib-wasi-threads.a.
THREADS   ?= 0
ifeq ($(THREADS),1)
CC         = $(WASI_SDK)/bin/wasm32-wasip1-threads-clang
CFLAGS    := -O2 -Wall -D__wasi__ -DWASI_PLATFORM -DWASI_THREADS -pthread -I$(CURDIR)
VARIANT   := -threads
else
CC         = $(WASI_SDK)/bin/wasm32-wasip2-clang
CFLAGS    := -O2 -Wall -D__wasi__ -DWASI_PLATFORM -I$(CURDIR)
VARIANT   :=
endif

# -----------------------------------------------------------------------
#  Source / object lists
# -----------------------------------------------------------------------

SRCDIR    := src
BUILDDIR  := build$(VARIANT)

SRCS      := $(wildcard $(SRCDIR)/*.c)
OBJS      := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))
DEPS      := $(OBJS:.o=.d)

LIB       := libglib-wasi$(VARIANT).a

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
