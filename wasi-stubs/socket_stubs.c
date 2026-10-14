/*
 * Socket function stubs for WASI.
 *
 * With -D__wasilibc_use_wasip2, the WASI sysroot declares socket functions
 * but doesn't provide implementations for wasip1 targets. These stubs
 * satisfy the linker. All return errors since WASI has no real sockets.
 */

#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>

int socket(int domain, int type, int protocol) {
    (void)domain; (void)type; (void)protocol;
    errno = ENOSYS;
    return -1;
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    (void)sockfd; (void)addr; (void)addrlen;
    errno = ENOSYS;
    return -1;
}

int listen(int sockfd, int backlog) {
    (void)sockfd; (void)backlog;
    errno = ENOSYS;
    return -1;
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    (void)sockfd; (void)addr; (void)addrlen;
    errno = ENOSYS;
    return -1;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    (void)sockfd; (void)addr; (void)addrlen;
    errno = ENOSYS;
    return -1;
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen) {
    (void)sockfd; (void)buf; (void)len; (void)flags;
    (void)dest_addr; (void)addrlen;
    errno = ENOSYS;
    return -1;
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen) {
    (void)sockfd; (void)buf; (void)len; (void)flags;
    (void)src_addr; (void)addrlen;
    errno = ENOSYS;
    return -1;
}

int setsockopt(int sockfd, int level, int optname,
               const void *optval, socklen_t optlen) {
    (void)sockfd; (void)level; (void)optname;
    (void)optval; (void)optlen;
    errno = ENOSYS;
    return -1;
}

int getsockopt(int sockfd, int level, int optname,
               void *optval, socklen_t *optlen) {
    (void)sockfd; (void)level; (void)optname;
    (void)optval; (void)optlen;
    errno = ENOSYS;
    return -1;
}

int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    (void)sockfd; (void)addr; (void)addrlen;
    errno = ENOSYS;
    return -1;
}

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    (void)sockfd; (void)addr; (void)addrlen;
    errno = ENOSYS;
    return -1;
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
    (void)sockfd; (void)buf; (void)len; (void)flags;
    errno = ENOSYS;
    return -1;
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    (void)sockfd; (void)buf; (void)len; (void)flags;
    errno = ENOSYS;
    return -1;
}

int shutdown(int sockfd, int how) {
    (void)sockfd; (void)how;
    errno = ENOSYS;
    return -1;
}

int socketpair(int domain, int type, int protocol, int sv[2]) {
    (void)domain; (void)type; (void)protocol; (void)sv;
    errno = ENOSYS;
    return -1;
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags) {
    (void)sockfd; (void)msg; (void)flags;
    errno = ENOSYS;
    return -1;
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
    (void)sockfd; (void)msg; (void)flags;
    errno = ENOSYS;
    return -1;
}
