/**
 * getifaddrs()/freeifaddrs()/if_nametoindex() for PS Vita.
 *
 * The Vita has no interface enumeration API in newlib; libjuice needs one to
 * gather ICE host candidates. We report two interfaces: the loopback and the
 * active network connection (IP + netmask from sceNetCtlInetGetInfo).
 */

#include "ifaddrs.h"
#include "net/if.h"
#include "sys/ioctl.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#ifdef __vita__
#include <psp2/net/netctl.h>
#endif

/* One allocation holds everything freeifaddrs() must release. */
struct vita_ifaddrs_block {
    struct ifaddrs entries[2];
    struct sockaddr_in addrs[2];
    struct sockaddr_in netmasks[2];
    char names[2][8];
};

int getifaddrs(struct ifaddrs **ifap) {
    if (!ifap) return -1;
    *ifap = NULL;

    struct vita_ifaddrs_block *blk =
        (struct vita_ifaddrs_block *)calloc(1, sizeof(struct vita_ifaddrs_block));
    if (!blk) return -1;

    int count = 0;

    /* Loopback */
    {
        struct ifaddrs *e = &blk->entries[count];
        struct sockaddr_in *sa = &blk->addrs[count];
        struct sockaddr_in *nm = &blk->netmasks[count];
        strcpy(blk->names[count], "lo0");
        sa->sin_family = AF_INET;
        sa->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        nm->sin_family = AF_INET;
        nm->sin_addr.s_addr = htonl(0xFF000000u);
        e->ifa_name = blk->names[count];
        e->ifa_flags = IFF_UP | IFF_RUNNING | IFF_LOOPBACK;
        e->ifa_addr = (struct sockaddr *)sa;
        e->ifa_netmask = (struct sockaddr *)nm;
        count++;
    }

#ifdef __vita__
    /* Active connection (WiFi) via NetCtl. Init is idempotent: if the app
     * already initialised NetCtl this returns NOT_TERMINATED, which is fine. */
    sceNetCtlInit();

    SceNetCtlInfo ipInfo;
    memset(&ipInfo, 0, sizeof(ipInfo));
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &ipInfo) == 0 &&
        ipInfo.ip_address[0] != '\0') {
        struct ifaddrs *e = &blk->entries[count];
        struct sockaddr_in *sa = &blk->addrs[count];
        struct sockaddr_in *nm = &blk->netmasks[count];

        sa->sin_family = AF_INET;
        if (inet_pton(AF_INET, ipInfo.ip_address, &sa->sin_addr) == 1) {
            strcpy(blk->names[count], "wlan0");

            nm->sin_family = AF_INET;
            SceNetCtlInfo maskInfo;
            memset(&maskInfo, 0, sizeof(maskInfo));
            if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_NETMASK, &maskInfo) != 0 ||
                inet_pton(AF_INET, maskInfo.netmask, &nm->sin_addr) != 1) {
                nm->sin_addr.s_addr = htonl(0xFFFFFF00u); /* assume /24 */
            }

            e->ifa_name = blk->names[count];
            e->ifa_flags = IFF_UP | IFF_RUNNING | IFF_BROADCAST | IFF_MULTICAST;
            e->ifa_addr = (struct sockaddr *)sa;
            e->ifa_netmask = (struct sockaddr *)nm;
            count++;
        }
    }
#endif

    for (int i = 0; i < count - 1; i++) {
        blk->entries[i].ifa_next = &blk->entries[i + 1];
    }

    /* The first entry's address is the block's address, so freeifaddrs() can
     * free() the whole allocation. */
    *ifap = &blk->entries[0];
    return 0;
}

void freeifaddrs(struct ifaddrs *ifa) {
    /* getifaddrs() returns a pointer to the first entry of a single
     * vita_ifaddrs_block allocation (entries[] is its first member). */
    free(ifa);
}

unsigned int if_nametoindex(const char *ifname) {
    if (!ifname) return 0;
    if (strcmp(ifname, "lo0") == 0) return 1;
    if (strcmp(ifname, "wlan0") == 0) return 2;
    return 0;
}

/* Minimal ioctl(): vitasdk newlib has none. libjuice/usrsctp only use
 * FIONBIO on sockets, which maps to fcntl(F_SETFL, O_NONBLOCK) - newlib's
 * fcntl forwards that to sceNetSetsockopt(SO_NONBLOCK). */
int ioctl(int fd, unsigned long request, ...) {
    if (request == FIONBIO) {
        va_list args;
        va_start(args, request);
        int *val = va_arg(args, int *);
        va_end(args);

        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0) flags = 0;
        if (val && *val) {
            flags |= O_NONBLOCK;
        } else {
            flags &= ~O_NONBLOCK;
        }
        return fcntl(fd, F_SETFL, flags);
    }
    errno = ENOSYS;
    return -1;
}
