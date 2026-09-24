/**
 * Minimal <net/if.h> for PS Vita (vitasdk newlib has none).
 * Provides just the IFF_* flags and if_nametoindex() used by libjuice.
 * if_nametoindex() is implemented in vita_ifaddrs.c.
 */

#ifndef VITA_COMPAT_NET_IF_H
#define VITA_COMPAT_NET_IF_H

#ifdef __cplusplus
extern "C" {
#endif

#define IFF_UP          0x1
#define IFF_BROADCAST   0x2
#define IFF_LOOPBACK    0x8
#define IFF_POINTOPOINT 0x10
#define IFF_RUNNING     0x40
#define IFF_NOARP       0x80
#define IFF_PROMISC     0x100
#define IFF_MULTICAST   0x8000

#ifndef IF_NAMESIZE
#define IF_NAMESIZE 16
#endif
#ifndef IFNAMSIZ
#define IFNAMSIZ IF_NAMESIZE
#endif

unsigned int if_nametoindex(const char *ifname);

#ifdef __cplusplus
}
#endif

#endif /* VITA_COMPAT_NET_IF_H */
