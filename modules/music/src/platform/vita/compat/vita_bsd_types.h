/**
 * BSD compatibility force-include for building usrsctp on PS Vita.
 *
 * usrsctp's userspace build assumes BSD-flavoured libc headers. vitasdk's
 * newlib provides most of it, but only when BSD visibility is enabled, and a
 * few derived types are missing entirely. This header is force-included
 * (-include) into every usrsctp translation unit.
 */

#ifndef VITA_BSD_TYPES_H
#define VITA_BSD_TYPES_H

/* Enable BSD-visible definitions (u_char, u_int, caddr_t, timercmp, ...) in
 * newlib headers regardless of the -std level used to compile usrsctp. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _BSD_SOURCE
#define _BSD_SOURCE 1
#endif
#ifndef __BSD_VISIBLE
#define __BSD_VISIBLE 1
#endif

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

/* Constants newlib doesn't provide. usrsctp only compares against ERESTART
 * (never a case label), so any distinct value works; 85 matches Linux. */
#ifndef UIO_MAXIOV
#define UIO_MAXIOV 1024
#endif
#ifndef ERESTART
#define ERESTART 85
#endif
#ifndef SOMAXCONN
#define SOMAXCONN 128
#endif
#ifndef IPPORT_RESERVED
#define IPPORT_RESERVED 1024
#endif

/* Ancillary (control) message support: vitasdk defines struct msghdr with
 * msg_control/msg_controllen but not struct cmsghdr or the CMSG_* macros.
 * usrsctp iterates control data manually with CMSG_ALIGN. */
#ifndef CMSG_ALIGN
struct cmsghdr {
    socklen_t cmsg_len;   /* data byte count, including header */
    int       cmsg_level; /* originating protocol */
    int       cmsg_type;  /* protocol-specific type */
};
#define CMSG_ALIGN(len) (((len) + sizeof(long) - 1) & ~(sizeof(long) - 1))
#define CMSG_LEN(len)   (CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))
#define CMSG_SPACE(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(len))
#define CMSG_DATA(cmsg) ((unsigned char *)((struct cmsghdr *)(cmsg) + 1))
#endif

#endif /* VITA_BSD_TYPES_H */
