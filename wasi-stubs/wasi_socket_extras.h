#ifndef _WASI_SOCKET_EXTRAS_H_STUB
#define _WASI_SOCKET_EXTRAS_H_STUB
/*
 * Supplementary socket constants and declarations for WASI.
 *
 * The WASI sysroot provides basic socket support via __header_sys_socket.h,
 * but many POSIX constants that QEMU needs are missing. This header provides
 * them. It is included AFTER the real sys/socket.h via wasi_compat.h.
 *
 * With -D__wasilibc_use_wasip2, the sysroot provides:
 *   socket(), connect(), bind(), listen(), getsockname(), getpeername(),
 *   sendto(), recvfrom(), setsockopt()
 *
 * We provide the missing constants and function stubs here.
 */

#include <sys/types.h>

/* ---- Missing PF_/AF_ constants ---- */
/* WASI __header_sys_socket.h only defines PF_UNSPEC, PF_INET, PF_INET6 */
/* PF_LOCAL must match AF_UNIX (=3 in WASI), not 1 (which is PF_INET in WASI) */
#ifndef PF_LOCAL
#define PF_LOCAL  3
#endif
#ifndef PF_UNIX
#define PF_UNIX   PF_LOCAL
#endif
#ifndef PF_FILE
#define PF_FILE   PF_LOCAL
#endif
#ifndef AF_LOCAL
#define AF_LOCAL  PF_LOCAL
#endif
/* AF_UNIX is defined in __header_sys_socket.h as 3, keep that */
#ifndef AF_FILE
#define AF_FILE   AF_LOCAL
#endif
#ifndef PF_NETLINK
#define PF_NETLINK 16
#endif
#ifndef AF_NETLINK
#define AF_NETLINK PF_NETLINK
#endif
#ifndef PF_PACKET
#define PF_PACKET  17
#endif
#ifndef AF_PACKET
#define AF_PACKET  PF_PACKET
#endif
#ifndef PF_VSOCK
#define PF_VSOCK   40
#endif
#ifndef AF_VSOCK
#define AF_VSOCK   PF_VSOCK
#endif

/* ---- Missing SO_ constants ---- */
#ifndef SO_DEBUG
#define SO_DEBUG        1
#endif
#ifndef SO_REUSEADDR
#define SO_REUSEADDR    2
#endif
#ifndef SO_DONTROUTE
#define SO_DONTROUTE    5
#endif
#ifndef SO_BROADCAST
#define SO_BROADCAST    6
#endif
#ifndef SO_SNDBUF
#define SO_SNDBUF       7
#endif
#ifndef SO_RCVBUF
#define SO_RCVBUF       8
#endif
#ifndef SO_KEEPALIVE
#define SO_KEEPALIVE    9
#endif
#ifndef SO_OOBINLINE
#define SO_OOBINLINE    10
#endif
#ifndef SO_NO_CHECK
#define SO_NO_CHECK     11
#endif
#ifndef SO_PRIORITY
#define SO_PRIORITY     12
#endif
#ifndef SO_LINGER
#define SO_LINGER       13
#endif
#ifndef SO_REUSEPORT
#define SO_REUSEPORT    15
#endif
#ifndef SO_PASSCRED
#define SO_PASSCRED     16
#endif
#ifndef SO_PEERCRED
#define SO_PEERCRED     17
#endif
#ifndef SO_RCVLOWAT
#define SO_RCVLOWAT     18
#endif
#ifndef SO_SNDLOWAT
#define SO_SNDLOWAT     19
#endif
#ifndef SO_BINDTODEVICE
#define SO_BINDTODEVICE 25
#endif

/* ---- Missing IPPROTO constants ---- */
#ifndef IPPROTO_IP
#define IPPROTO_IP   0
#endif
#ifndef IPPROTO_TCP
#define IPPROTO_TCP  6
#endif
#ifndef IPPROTO_UDP
#define IPPROTO_UDP  17
#endif
#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6 41
#endif

/* ---- IP_TOS, TCP_NODELAY, IPV6_V6ONLY ---- */
#ifndef IP_TOS
#define IP_TOS 1
#endif
#ifndef IP_TTL
#define IP_TTL 2
#endif
#ifndef TCP_NODELAY
#define TCP_NODELAY 1
#endif
#ifndef IPV6_V6ONLY
#define IPV6_V6ONLY 26
#endif

/* ---- Missing MSG_ constants ---- */
#ifndef MSG_OOB
#define MSG_OOB       0x0001
#endif
#ifndef MSG_DONTROUTE
#define MSG_DONTROUTE 0x0004
#endif
#ifndef MSG_CTRUNC
#define MSG_CTRUNC    0x0008
#endif
#ifndef MSG_EOR
#define MSG_EOR       0x0080
#endif
#ifndef MSG_ERRQUEUE
#define MSG_ERRQUEUE  0x2000
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL  0x4000
#endif
#ifndef MSG_MORE
#define MSG_MORE      0x8000
#endif
#ifndef MSG_CMSG_CLOEXEC
#define MSG_CMSG_CLOEXEC 0x40000000
#endif

/* ---- CMSG macros ---- */
#ifndef CMSG_DATA
struct cmsghdr {
    unsigned int cmsg_len;
    int          cmsg_level;
    int          cmsg_type;
};

#define __CMSG_LEN(cmsg) (((cmsg)->cmsg_len + sizeof(long) - 1) & ~(long)(sizeof(long) - 1))
#define __CMSG_NEXT(cmsg) ((unsigned char *)(cmsg) + __CMSG_LEN(cmsg))
#define __MHDR_END(mhdr) ((unsigned char *)(mhdr)->msg_control + (mhdr)->msg_controllen)

#define CMSG_DATA(cmsg) ((unsigned char *) (((struct cmsghdr *)(cmsg)) + 1))
#define CMSG_NXTHDR(mhdr, cmsg) ((cmsg)->cmsg_len < sizeof (struct cmsghdr) || \
    __CMSG_LEN(cmsg) + sizeof(struct cmsghdr) >= __MHDR_END(mhdr) - (unsigned char *)(cmsg) \
    ? 0 : (struct cmsghdr *)__CMSG_NEXT(cmsg))
#define CMSG_FIRSTHDR(mhdr) ((size_t) (mhdr)->msg_controllen >= sizeof (struct cmsghdr) ? (struct cmsghdr *) (mhdr)->msg_control : (struct cmsghdr *) 0)

#define CMSG_ALIGN(len) (((len) + sizeof (size_t) - 1) & (size_t) ~(sizeof (size_t) - 1))
#define CMSG_SPACE(len) (CMSG_ALIGN (len) + CMSG_ALIGN (sizeof (struct cmsghdr)))
#define CMSG_LEN(len)   (CMSG_ALIGN (sizeof (struct cmsghdr)) + (len))

#define SCM_RIGHTS      0x01
#define SCM_CREDENTIALS 0x02
#endif

/* ---- SOCK_RAW and other types ---- */
#ifndef SOCK_RAW
#define SOCK_RAW       3
#endif
#ifndef SOCK_RDM
#define SOCK_RDM       4
#endif
#ifndef SOCK_SEQPACKET
#define SOCK_SEQPACKET 5
#endif

/* ---- Missing function stubs ---- */
/* socketpair, sendmsg, recvmsg are NOT available in WASI */

#include <errno.h>
#include <string.h>

#ifndef _WASI_SOCKETPAIR_STUB
#define _WASI_SOCKETPAIR_STUB
static inline int socketpair(int domain, int type, int protocol, int sv[2]) {
    (void)domain; (void)type; (void)protocol; (void)sv;
    errno = ENOSYS;
    return -1;
}
#endif

/* sendmsg/recvmsg stubs */
#ifndef _WASI_SENDMSG_STUB
#define _WASI_SENDMSG_STUB
struct msghdr;  /* forward decl - already defined by WASI headers */
static inline ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags) {
    (void)sockfd; (void)msg; (void)flags;
    errno = ENOSYS;
    return -1;
}
static inline ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
    (void)sockfd; (void)msg; (void)flags;
    errno = ENOSYS;
    return -1;
}
#endif

/* sockatmark stub */
#ifndef _WASI_SOCKATMARK_STUB
#define _WASI_SOCKATMARK_STUB
static inline int sockatmark(int sockfd) {
    (void)sockfd;
    return 0;
}
#endif

/*
 * ---- Socket calls the wasip1 sysroot header hides ----
 *
 * __header_sys_socket.h only declares these under __wasilibc_use_wasip2.
 * Defining that macro for the whole build also switches other parts of
 * wasi-libc (notably open()) to the wasip2 implementation, which changes
 * the errno reported for paths outside the preopened directories; QEMU
 * depends on getting ENOENT there.  So just declare the functions here.
 * wasi-stubs/socket_stubs.c provides the definitions.
 */
#ifndef __wasilibc_use_wasip2
#ifndef _WASI_SOCKET_DECLS_STUB
#define _WASI_SOCKET_DECLS_STUB
#include <sys/socket.h>   /* for socklen_t and struct sockaddr */
#ifndef SOMAXCONN
#define SOMAXCONN 128
#endif
int setsockopt(int sockfd, int level, int optname,
               const void *optval, socklen_t optlen);
int getsockname(int sockfd, struct sockaddr *__restrict addr,
                socklen_t *__restrict addrlen);
int getpeername(int sockfd, struct sockaddr *__restrict addr,
                socklen_t *__restrict addrlen);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t recvfrom(int sockfd, void *__restrict buf, size_t len, int flags,
                 struct sockaddr *__restrict src_addr,
                 socklen_t *__restrict addrlen);
#endif
#endif

#endif /* _WASI_SOCKET_EXTRAS_H_STUB */
