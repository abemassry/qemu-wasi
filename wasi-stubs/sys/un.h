#ifndef _WASI_SYS_UN_H_STUB
#define _WASI_SYS_UN_H_STUB
/*
 * Override for sys/un.h — provides struct sockaddr_un with sun_path field.
 * The WASI sysroot's version omits sun_path, but QEMU needs it.
 */

#include <sys/types.h>

/* Use our own include guard that also blocks the WASI sysroot version */
#ifndef __wasilibc___struct_sockaddr_un_h
#define __wasilibc___struct_sockaddr_un_h
#endif

#ifndef _WASI_SOCKADDR_UN_DEFINED
#define _WASI_SOCKADDR_UN_DEFINED

#ifndef __BIGGEST_ALIGNMENT__
#define __BIGGEST_ALIGNMENT__ 16
#endif

#include <stdint.h>

/* Pull in sa_family_t from the sysroot */
#ifndef __typedef_sa_family_t
typedef unsigned short sa_family_t;
#endif

struct sockaddr_un {
    sa_family_t sun_family;
    char        sun_path[108];
};

#endif /* _WASI_SOCKADDR_UN_DEFINED */

#endif /* _WASI_SYS_UN_H_STUB */
