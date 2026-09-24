/**
 * Minimal <ifaddrs.h> for PS Vita (vitasdk newlib has none).
 * Backs libjuice's local interface enumeration; implemented in
 * vita_ifaddrs.c using sceNetCtlInetGetInfo().
 */

#ifndef VITA_COMPAT_IFADDRS_H
#define VITA_COMPAT_IFADDRS_H

#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ifaddrs {
    struct ifaddrs  *ifa_next;
    char            *ifa_name;
    unsigned int     ifa_flags;
    struct sockaddr *ifa_addr;
    struct sockaddr *ifa_netmask;
    struct sockaddr *ifa_dstaddr;
    void            *ifa_data;
};

#ifndef ifa_broadaddr
#define ifa_broadaddr ifa_dstaddr
#endif

int getifaddrs(struct ifaddrs **ifap);
void freeifaddrs(struct ifaddrs *ifa);

#ifdef __cplusplus
}
#endif

#endif /* VITA_COMPAT_IFADDRS_H */
