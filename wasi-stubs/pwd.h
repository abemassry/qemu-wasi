#ifndef _WASI_PWD_H_STUB
#define _WASI_PWD_H_STUB
/* Stub pwd.h for WASI — WASI has no concept of users/groups */

#include <sys/types.h>

struct passwd {
    char   *pw_name;
    char   *pw_passwd;
    uid_t   pw_uid;
    gid_t   pw_gid;
    char   *pw_gecos;
    char   *pw_dir;
    char   *pw_shell;
};

static inline struct passwd *getpwnam(const char *name) {
    (void)name;
    return (struct passwd *)0;
}

static inline struct passwd *getpwuid(uid_t uid) {
    (void)uid;
    return (struct passwd *)0;
}

static inline int getpwnam_r(const char *name, struct passwd *pwd,
                             char *buf, size_t buflen, struct passwd **result) {
    (void)name; (void)pwd; (void)buf; (void)buflen;
    if (result) *result = (struct passwd *)0;
    return -1;
}

static inline int initgroups(const char *user, gid_t group) {
    (void)user; (void)group;
    return 0;
}

#endif /* _WASI_PWD_H_STUB */
