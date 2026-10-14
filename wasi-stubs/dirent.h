#ifndef _WASI_DIRENT_H_STUB
#define _WASI_DIRENT_H_STUB
/*
 * Extended dirent stub for WASI — supplements the WASI sysroot's dirent.h
 * with missing types/functions that QEMU needs.
 *
 * WASI sysroot already provides opendir/readdir/closedir via
 * <dirent.h>, but some QEMU code uses additional POSIX features.
 * We include the real header first, then add what's missing.
 */

/* Include the real WASI dirent.h first */
#include_next <dirent.h>

/* DT_* constants may already be defined, add any missing ones */
#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#endif
#ifndef DT_FIFO
#define DT_FIFO    1
#endif
#ifndef DT_CHR
#define DT_CHR     2
#endif
#ifndef DT_DIR
#define DT_DIR     4
#endif
#ifndef DT_BLK
#define DT_BLK     6
#endif
#ifndef DT_REG
#define DT_REG     8
#endif
#ifndef DT_LNK
#define DT_LNK     10
#endif
#ifndef DT_SOCK
#define DT_SOCK    12
#endif

/*
 * scandir/alphasort are declared in the WASI sysroot's dirent.h (lines 42-43).
 * We do NOT redefine them here — link-time stubs are provided in socket_stubs.c
 * (or will link against the sysroot's implementation if available).
 */

#endif /* _WASI_DIRENT_H_STUB */
