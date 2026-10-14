#ifndef _WASI_SIGNAL_EXTRAS_H_STUB
#define _WASI_SIGNAL_EXTRAS_H_STUB
/*
 * Extended signal stubs for WASI.
 *
 * The WASI sysroot's signal.h (with _WASI_EMULATED_SIGNAL) provides
 * basic signal numbers and raise()/signal(), but guards siginfo_t,
 * SI_USER, SI_QUEUE, sigprocmask, etc. behind __wasilibc_unmodified_upstream.
 *
 * QEMU's osdep.h already provides sigaction/sigemptyset/etc. stubs.
 * This header adds what's still missing.
 */

#include <signal.h>
#include <sys/types.h>

/* siginfo_t — simplified version */
#ifndef _WASI_SIGINFO_DEFINED
#define _WASI_SIGINFO_DEFINED
typedef struct {
    int si_signo;
    int si_errno;
    int si_code;
    int si_pid;
    int si_uid;
    int si_status;
    void *si_addr;
    int si_band;
    union {
        int sival_int;
        void *sival_ptr;
    } si_value;
} siginfo_t;
#endif

/* SI_ constants */
#ifndef SI_USER
#define SI_USER    0
#endif
#ifndef SI_QUEUE
#define SI_QUEUE   (-1)
#endif
#ifndef SI_TIMER
#define SI_TIMER   (-2)
#endif
#ifndef SI_MESGQ
#define SI_MESGQ   (-3)
#endif
#ifndef SI_ASYNCIO
#define SI_ASYNCIO (-4)
#endif
#ifndef SI_KERNEL
#define SI_KERNEL  128
#endif

/* SIG_BLOCK / SIG_UNBLOCK / SIG_SETMASK */
#ifndef SIG_BLOCK
#define SIG_BLOCK   0
#endif
#ifndef SIG_UNBLOCK
#define SIG_UNBLOCK 1
#endif
#ifndef SIG_SETMASK
#define SIG_SETMASK 2
#endif

/* sigset_t — use a simple type if not defined */
#ifndef _WASI_SIGSET_DEFINED
#define _WASI_SIGSET_DEFINED
/* Check if sigset_t is already defined by WASI signal.h */
#endif

/* sigprocmask stub */
#ifndef _WASI_SIGPROCMASK_STUB
#define _WASI_SIGPROCMASK_STUB
static inline int sigprocmask(int how, const void *set, void *oldset) {
    (void)how; (void)set; (void)oldset;
    return 0;
}
#endif

/* pthread_sigmask stub */
#ifndef _WASI_PTHREAD_SIGMASK_STUB
#define _WASI_PTHREAD_SIGMASK_STUB
static inline int pthread_sigmask(int how, const void *set, void *oldset) {
    (void)how; (void)set; (void)oldset;
    return 0;
}
#endif

/* kill stub */
#ifndef _WASI_KILL_STUB
#define _WASI_KILL_STUB
static inline int kill(int pid, int sig) {
    (void)pid; (void)sig;
    return -1;
}
#endif

#endif /* _WASI_SIGNAL_EXTRAS_H_STUB */
