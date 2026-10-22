# qemu-wasi: QEMU compiled to WebAssembly (WASI), shown through wasi-gfx.
#
#   make                 fetch QEMU, apply patches/, build qemu-build/qemu-system-x86_64
#   make THREADS=1       the multi-threaded build instead (one thread per vCPU),
#                        in qemu-build-threads/; also for install and test
#   make install         copy it into m-browser (M_BROWSER, default ../m-browser)
#   make native          build a native QEMU from the same tree, for make test
#   make test            compare x86 instruction results with the native QEMU
#   make reset-qemu      re-apply patches/ to a pristine qemu-src (discards changes)
#   make export-patches  turn the commits in qemu-src/ back into patches/
#   make clean           remove build outputs; distclean also removes qemu-src/
#
# Needs wasi-sdk (WASI_SDK, default /opt/wasi-sdk), git, python3, ninja and
# pkg-config; see README.md.

WASI_SDK  ?= /opt/wasi-sdk
M_BROWSER ?= $(CURDIR)/../m-browser
NINJA     ?= ninja
THREADS   ?= 0
export WASI_SDK M_BROWSER THREADS

ifeq ($(THREADS),1)
VARIANT    := -threads
else
VARIANT    :=
endif

QEMU_SRC   := qemu-src
QEMU_BUILD := qemu-build$(VARIANT)
QEMU_WASM  := $(QEMU_BUILD)/qemu-system-x86_64
PATCHED    := $(QEMU_SRC)/.git/qemu-wasi-patched
ZLIB       := zlib-wasi$(VARIANT)/lib/libz.a
LIBS       := glib-wasi/libglib-wasi$(VARIANT).a wasi-stubs/libwasi-stubs$(VARIANT).a $(ZLIB)

.PHONY: all libs install native test reset-qemu export-patches clean distclean

all: $(QEMU_BUILD)/build.ninja libs
	python3 scripts/fix_build_ninja.py $(QEMU_BUILD)/build.ninja
	@# The libraries are link arguments, not ninja inputs: relink by hand.
	@for lib in $(LIBS); do \
	    if [ -f $(QEMU_WASM) ] && [ $$lib -nt $(QEMU_WASM) ]; then rm -f $(QEMU_WASM); fi; \
	done
	$(NINJA) -C $(QEMU_BUILD) qemu-system-x86_64
	@ls -l $(QEMU_WASM)

libs: $(ZLIB)
	$(MAKE) -C glib-wasi THREADS=$(THREADS)
	$(MAKE) -C wasi-stubs THREADS=$(THREADS)

$(ZLIB):
	scripts/build-zlib.sh

$(PATCHED): $(wildcard patches/*.patch)
	scripts/fetch-qemu.sh

$(QEMU_BUILD)/build.ninja: $(PATCHED) scripts/configure.sh | libs
	scripts/configure.sh

install: all
	cp $(QEMU_WASM) $(M_BROWSER)/qemu-system-x86_64.wasm
	@echo "Installed $(M_BROWSER)/qemu-system-x86_64.wasm"

# A native build of the same patched tree, as the reference for make test.
native: $(PATCHED)
	mkdir -p qemu-native
	cd qemu-native && { [ -f build.ninja ] || ../$(QEMU_SRC)/configure \
	    --target-list=x86_64-softmmu --disable-docs --disable-guest-agent \
	    --disable-tools --disable-werror --with-coroutine=ucontext; }
	$(NINJA) -C qemu-native qemu-system-x86_64

test:
	QEMU_WASM=$(CURDIR)/$(QEMU_WASM) tests/x86-insn-diff/run.sh

reset-qemu:
	scripts/fetch-qemu.sh --reset

export-patches:
	scripts/export-patches.sh

clean:
	rm -rf $(QEMU_BUILD)
	$(MAKE) -C glib-wasi clean THREADS=$(THREADS)
	$(MAKE) -C wasi-stubs clean THREADS=$(THREADS)

distclean: clean
	rm -rf $(QEMU_SRC) qemu-native zlib-src zlib-wasi zlib-wasi-threads
