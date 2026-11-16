/*
 * netfs_shim.c - PS2 compatibility shims for the NETFS build.
 *
 * gethostname(): libsmb2's server path called it; kept harmlessly for any
 * library that still expects it. wLaunchELF is a client only, so it is never
 * actually executed at runtime, but the symbol must exist to link.
 *
 * poll(): libnfs' PS2 port (ps2_ee/ps2_compat.h) DECLARES poll() but ships no
 * implementation. It used to be satisfied by the libsmb2 PS2 port that this
 * project linked for SMB2/3. The SMB backend is now smbman (SMB1) and no
 * longer links libsmb2, so the symbol disappeared and the final link failed
 * with "undefined reference to poll" from libnfs-sync.o. We provide it here
 * on top of lwIP select() from -lps2ip (already linked for NETFS).
 *
 * libnfs' other PS2 shims (nfs_getaddrinfo / nfs_freeaddrinfo) live in its
 * ps2_ee/ps2_compat.c and are patched into its Makefile during the CI install
 * step, so they are intentionally NOT duplicated here.
 */

#ifdef NETFS

#include <string.h>
#include <stddef.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

int gethostname(char *name, size_t len)
{
    static const char host[] = "PlayStation2";
    size_t n = sizeof(host) - 1;

    if (name == NULL || len == 0)
        return -1;
    if (n >= len)
        n = len - 1;
    memcpy(name, host, n);
    name[n] = '\0';
    return 0;
}

/*
 * ABI note: ps2_compat.h declares
 *   struct pollfd { int fd; short events; short revents; };
 *   int poll(struct pollfd *fds, unsigned int nfds, int timo);
 * We use a private tag with the exact same layout so we do not have to pull
 * ps2_compat.h (which is guarded by PS2_EE and drags in other headers) into
 * this translation unit. libnfs only ever polls a single socket with
 * POLLIN/POLLOUT, which select() covers directly.
 */
struct wle_pollfd {
    int fd;
    short events;
    short revents;
};

#define WLE_POLLIN   0x0001
#define WLE_POLLPRI  0x0002
#define WLE_POLLOUT  0x0004
#define WLE_POLLERR  0x0008
#define WLE_POLLHUP  0x0010
#define WLE_POLLNVAL 0x0020

int poll(struct wle_pollfd *fds, unsigned int nfds, int timo)
{
    fd_set rfds, wfds, xfds;
    struct timeval tv;
    struct timeval *ptv = NULL;
    int maxfd = -1;
    int i, n;

    if (fds == NULL || nfds == 0)
        return 0;

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&xfds);
    for (i = 0; (unsigned int)i < nfds; i++) {
        fds[i].revents = 0;
        if (fds[i].fd < 0)
            continue;
        if (fds[i].fd > maxfd)
            maxfd = fds[i].fd;
        if (fds[i].events & (WLE_POLLIN | WLE_POLLPRI))
            FD_SET(fds[i].fd, &rfds);
        if (fds[i].events & WLE_POLLOUT)
            FD_SET(fds[i].fd, &wfds);
        FD_SET(fds[i].fd, &xfds);
    }
    if (maxfd < 0)
        return 0;

    if (timo >= 0) {
        tv.tv_sec = timo / 1000;
        tv.tv_usec = (timo % 1000) * 1000;
        ptv = &tv;
    }

    n = select(maxfd + 1, &rfds, &wfds, &xfds, ptv);
    if (n <= 0)
        return n;

    for (i = 0; (unsigned int)i < nfds; i++) {
        short r = 0;
        if (fds[i].fd < 0) {
            fds[i].revents = WLE_POLLNVAL;
            continue;
        }
        /* Match libsmb2's PS2 port: the except set maps to POLLHUP on
         * lwIP (out-of-band/hang-up), which is what libnfs expects. */
        if (FD_ISSET(fds[i].fd, &rfds)) r |= WLE_POLLIN;
        if (FD_ISSET(fds[i].fd, &wfds)) r |= WLE_POLLOUT;
        if (FD_ISSET(fds[i].fd, &xfds)) r |= WLE_POLLHUP;
        fds[i].revents = r;
    }
    return n;
}

#endif /* NETFS */
