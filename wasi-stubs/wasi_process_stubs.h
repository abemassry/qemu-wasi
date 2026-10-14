#ifndef _WASI_PROCESS_STUBS_H
#define _WASI_PROCESS_STUBS_H
/*
 * POSIX process stubs for WASI.
 * WASI has no fork/exec/daemon/syslog/resource limits.
 * These stubs allow QEMU to compile; the functions return errors at runtime.
 */

#include <errno.h>
#include <sys/types.h>
#include <stdarg.h>

/* ---- fork/exec/waitpid ---- */
#ifndef _WASI_FORK_STUB
#define _WASI_FORK_STUB
static inline pid_t fork(void) {
    errno = ENOSYS;
    return -1;
}
#endif

#ifndef _WASI_EXECV_STUB
#define _WASI_EXECV_STUB
static inline int execv(const char *path, char *const argv[]) {
    (void)path; (void)argv;
    errno = ENOSYS;
    return -1;
}

static inline int execve(const char *path, char *const argv[], char *const envp[]) {
    (void)path; (void)argv; (void)envp;
    errno = ENOSYS;
    return -1;
}

static inline int execvp(const char *file, char *const argv[]) {
    (void)file; (void)argv;
    errno = ENOSYS;
    return -1;
}
#endif

#ifndef _WASI_WAITPID_STUB
#define _WASI_WAITPID_STUB
static inline pid_t waitpid(pid_t pid, int *status, int options) {
    (void)pid; (void)status; (void)options;
    errno = ECHILD;
    return -1;
}
#endif

/* ---- daemon ---- */
#ifndef _WASI_DAEMON_STUB
#define _WASI_DAEMON_STUB
static inline int daemon(int nochdir, int noclose) {
    (void)nochdir; (void)noclose;
    errno = ENOSYS;
    return -1;
}
#endif

/* ---- syslog ---- */
#ifndef _WASI_SYSLOG_STUB
#define _WASI_SYSLOG_STUB
#define LOG_EMERG   0
#define LOG_ALERT   1
#define LOG_CRIT    2
#define LOG_ERR     3
#define LOG_WARNING 4
#define LOG_NOTICE  5
#define LOG_INFO    6
#define LOG_DEBUG   7
#define LOG_PID     0x01
#define LOG_DAEMON  (3<<3)
#define LOG_AUTH    (4<<3)
#define LOG_USER    (1<<3)
#define LOG_LOCAL0  (16<<3)

static inline void openlog(const char *ident, int option, int facility) {
    (void)ident; (void)option; (void)facility;
}

static inline void closelog(void) {}

/* syslog can't be static inline because it's variadic and would need
   __attribute__((format)) - just declare and provide in a .c stub */
void syslog(int priority, const char *format, ...);
#endif

/* ---- Resource limits ---- */
#ifndef _WASI_RLIMIT_STUB
#define _WASI_RLIMIT_STUB
#ifndef RLIMIT_NOFILE
#define RLIMIT_NOFILE 7
#endif
#ifndef RLIMIT_MEMLOCK
#define RLIMIT_MEMLOCK 8
#endif

typedef unsigned long rlim_t;
#define RLIM_INFINITY (~(rlim_t)0)

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

static inline int getrlimit(int resource, struct rlimit *rlim) {
    (void)resource;
    if (rlim) {
        rlim->rlim_cur = RLIM_INFINITY;
        rlim->rlim_max = RLIM_INFINITY;
    }
    return 0;
}

static inline int setrlimit(int resource, const struct rlimit *rlim) {
    (void)resource; (void)rlim;
    return 0;
}
#endif

/* ---- setpgid/setsid ---- */
#ifndef _WASI_SETPGID_STUB
#define _WASI_SETPGID_STUB
static inline int setpgid(pid_t pid, pid_t pgid) {
    (void)pid; (void)pgid;
    return 0;
}

static inline pid_t setsid(void) {
    return 1;
}

static inline pid_t getpgid(pid_t pid) {
    (void)pid;
    return 1;
}
#endif

/* ---- pipe ---- */
#ifndef _WASI_PIPE_STUB
#define _WASI_PIPE_STUB
static inline int pipe(int pipefd[2]) {
    (void)pipefd;
    errno = ENOSYS;
    return -1;
}
static inline int pipe2(int pipefd[2], int flags) {
    (void)pipefd; (void)flags;
    errno = ENOSYS;
    return -1;
}
#endif

/* ---- User/group/permission stubs ---- */
#ifndef _WASI_USERPERM_STUB
#define _WASI_USERPERM_STUB

typedef unsigned int mode_t;

static inline int setgid(unsigned int gid) {
    (void)gid;
    return 0;
}

static inline int setuid(unsigned int uid) {
    (void)uid;
    return 0;
}

static inline int setgroups(size_t size, const unsigned int *list) {
    (void)size; (void)list;
    return 0;
}

static inline int chroot(const char *path) {
    (void)path;
    errno = ENOSYS;
    return -1;
}

#ifndef _WASI_UMASK_DEFINED
#define _WASI_UMASK_DEFINED
static inline unsigned int umask(unsigned int mask) {
    (void)mask;
    return 022;
}
#endif

#endif /* _WASI_USERPERM_STUB */

#endif /* _WASI_PROCESS_STUBS_H */
