/**
 * Force-included into every libjuice translation unit on PS Vita.
 *
 * - Marks the platform little-endian so picohash.h doesn't reach for the
 *   glibc-only <endian.h> (vitasdk newlib has none; the Vita CPU is ARM LE).
 * - Provides IPv6 socket option constants that vitasdk headers may lack.
 *   The Vita has no IPv6 support: AF_INET6 sockets fail at creation, so
 *   these setsockopt calls are never reached - the constants only need to
 *   exist for compilation.
 */

#ifndef VITA_JUICE_COMPAT_H
#define VITA_JUICE_COMPAT_H

#ifndef __LITTLE_ENDIAN__
#define __LITTLE_ENDIAN__ 1
#endif

#include <netinet/in.h>
#include <netdb.h>

#ifndef IPV6_V6ONLY
#define IPV6_V6ONLY 27
#endif
#ifndef IPV6_TCLASS
#define IPV6_TCLASS 67
#endif
#ifndef IPV6_UNICAST_HOPS
#define IPV6_UNICAST_HOPS 4
#endif

/* No SIGPIPE on the Vita; the flag can be a no-op. */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* getaddrinfo/getnameinfo flags newlib may not define. Zero/BSD fallbacks:
 * missing AI_* hint flags simply mean the resolver does a little more work;
 * NI_* values match classic BSD. */
#ifndef AI_NUMERICSERV
#define AI_NUMERICSERV 0
#endif
#ifndef AI_ADDRCONFIG
#define AI_ADDRCONFIG 0
#endif
#ifndef AI_NUMERICHOST
#define AI_NUMERICHOST 0
#endif
#ifndef AI_PASSIVE
#define AI_PASSIVE 0
#endif
#ifndef NI_NUMERICHOST
#define NI_NUMERICHOST 0x00000002
#endif
#ifndef NI_NUMERICSERV
#define NI_NUMERICSERV 0x00000008
#endif
#ifndef NI_DGRAM
#define NI_DGRAM 0x00000010
#endif

#endif /* VITA_JUICE_COMPAT_H */
