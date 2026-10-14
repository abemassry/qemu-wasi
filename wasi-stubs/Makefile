# libwasi-stubs.a: stand-ins for what wasi-libc (wasm32-wasip1) lacks.
#
#   pthread_stubs.c  pthreads for a single thread: pthread_create() reports
#                    success but never runs the thread, locks are no-ops
#   socket_stubs.c   the BSD socket API, failing (networking goes through
#                    the host instead; see QEMU's net/wasi-user.c.inc)
#   posix_stubs.c    getpid(), shm_open(), msync(), pthread_atfork(), ...
#   syslog_stub.c    syslog() to stderr
#
# The headers in this directory are on QEMU's include path ahead of the
# sysroot's, to fill in missing declarations (see QEMU's osdep.h).

WASI_SDK ?= /opt/wasi-sdk
CC       := $(WASI_SDK)/bin/wasm32-wasip1-clang
AR       := $(WASI_SDK)/bin/llvm-ar

CFLAGS   := -O2 -g -Wall -I$(CURDIR) \
            -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_PROCESS_CLOCKS \
            -D_WASI_EMULATED_MMAN

SRCS     := posix_stubs.c pthread_stubs.c socket_stubs.c syslog_stub.c
OBJS     := $(SRCS:%.c=build/%.o)
LIB      := libwasi-stubs.a

.PHONY: all clean

all: $(LIB)

$(LIB): $(OBJS)
	rm -f $@
	$(AR) rcs $@ $^

build/%.o: %.c | build
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

build:
	mkdir -p $@

clean:
	rm -rf build $(LIB)

-include $(OBJS:.o=.d)
