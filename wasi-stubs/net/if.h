#ifndef _WASI_NET_IF_H_STUB
#define _WASI_NET_IF_H_STUB
/* Stub net/if.h for WASI — WASI has no network interface support */

#define IF_NAMESIZE 16
#define IFNAMSIZ    IF_NAMESIZE

struct ifreq {
    char ifr_name[IFNAMSIZ];
    union {
        struct { unsigned short sa_family; char sa_data[14]; } ifr_addr;
        struct { unsigned short sa_family; char sa_data[14]; } ifr_netmask;
        short ifr_flags;
        int ifr_ifindex;
    };
};

static inline unsigned int if_nametoindex(const char *ifname) {
    (void)ifname;
    return 0;
}

static inline char *if_indextoname(unsigned int ifindex, char *ifname) {
    (void)ifindex; (void)ifname;
    return (char *)0;
}

#endif /* _WASI_NET_IF_H_STUB */
