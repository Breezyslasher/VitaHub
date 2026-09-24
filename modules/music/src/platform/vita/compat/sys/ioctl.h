/**
 * Minimal <sys/ioctl.h> for PS Vita (vitasdk newlib has none).
 * libjuice/usrsctp only use ioctl(fd, FIONBIO, &v) to toggle non-blocking
 * mode on sockets. Our shim (vita_ifaddrs.c) implements exactly that via
 * fcntl(F_SETFL, O_NONBLOCK), which vitasdk newlib supports on sockets.
 */

#ifndef VITA_COMPAT_SYS_IOCTL_H
#define VITA_COMPAT_SYS_IOCTL_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FIONBIO
#define FIONBIO 0x8004667e /* set/clear non-blocking i/o */
#endif
#ifndef FIONREAD
#define FIONREAD 0x4004667f /* get # bytes to read */
#endif

int ioctl(int fd, unsigned long request, ...);

#ifdef __cplusplus
}
#endif

#endif /* VITA_COMPAT_SYS_IOCTL_H */
