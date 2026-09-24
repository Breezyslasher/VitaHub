/**
 * Force-included into every libdatachannel translation unit on PS Vita.
 * Socket flags newlib doesn't define; there is no SIGPIPE on the Vita so
 * MSG_NOSIGNAL can be a no-op.
 */

#ifndef VITA_DATACHANNEL_COMPAT_H
#define VITA_DATACHANNEL_COMPAT_H

#include <sys/socket.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#endif /* VITA_DATACHANNEL_COMPAT_H */
