/**
 * Minimal <netinet/in_systm.h> for PS Vita (vitasdk newlib has none).
 * Classic BSD network-order scalar types used by <netinet/ip.h>.
 */

#ifndef VITA_COMPAT_NETINET_IN_SYSTM_H
#define VITA_COMPAT_NETINET_IN_SYSTM_H

#include <sys/types.h>

typedef u_int16_t n_short; /* short as received from the net */
typedef u_int32_t n_long;  /* long as received from the net */
typedef u_int32_t n_time;  /* ms since 00:00 GMT, byte rev */

#endif /* VITA_COMPAT_NETINET_IN_SYSTM_H */
