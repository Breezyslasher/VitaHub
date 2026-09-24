/**
 * Minimal <sys/uio.h> for PS Vita (vitasdk newlib has none).
 * struct iovec is already defined by vitasdk's <sys/socket.h>; usrsctp only
 * needs the type (it never calls readv()/writev()).
 */

#ifndef VITA_COMPAT_SYS_UIO_H
#define VITA_COMPAT_SYS_UIO_H

#include <sys/types.h>
#include <sys/socket.h> /* defines struct iovec on vitasdk */

#ifdef __cplusplus
extern "C" {
#endif

ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
ssize_t writev(int fd, const struct iovec *iov, int iovcnt);

#ifdef __cplusplus
}
#endif

#endif /* VITA_COMPAT_SYS_UIO_H */
