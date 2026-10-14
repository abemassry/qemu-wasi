#ifndef _WASI_GRP_H_STUB
#define _WASI_GRP_H_STUB
/* Stub grp.h for WASI — WASI has no concept of groups */

#include <sys/types.h>

struct group {
    char   *gr_name;
    char   *gr_passwd;
    gid_t   gr_gid;
    char  **gr_mem;
};

static inline struct group *getgrnam(const char *name) {
    (void)name;
    return (struct group *)0;
}

static inline struct group *getgrgid(gid_t gid) {
    (void)gid;
    return (struct group *)0;
}

static inline int getgrnam_r(const char *name, struct group *grp,
                             char *buf, size_t buflen, struct group **result) {
    (void)name; (void)grp; (void)buf; (void)buflen;
    if (result) *result = (struct group *)0;
    return -1;
}

static inline void setgrent(void) {}
static inline void endgrent(void) {}
static inline struct group *getgrent(void) { return (struct group *)0; }

#endif /* _WASI_GRP_H_STUB */
