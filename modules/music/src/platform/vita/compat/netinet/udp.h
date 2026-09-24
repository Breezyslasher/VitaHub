/**
 * Minimal <netinet/udp.h> for PS Vita (vitasdk newlib has none).
 * Classic BSD UDP header, used by usrsctp for SCTP-over-UDP encapsulation.
 */

#ifndef VITA_COMPAT_NETINET_UDP_H
#define VITA_COMPAT_NETINET_UDP_H

#include <sys/types.h>

struct udphdr {
	u_int16_t uh_sport; /* source port */
	u_int16_t uh_dport; /* destination port */
	u_int16_t uh_ulen;  /* udp length */
	u_int16_t uh_sum;   /* udp checksum */
};

#endif /* VITA_COMPAT_NETINET_UDP_H */
