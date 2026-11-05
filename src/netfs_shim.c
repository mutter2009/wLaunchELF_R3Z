/*
 * netfs_shim.c - PS2 compatibility shims for the NETFS (libsmb2 / libnfs) build.
 *
 * The ps2sdk image ships libsmb2 as ports/lib/libsmb2.a. Its server path
 * (smb2_serve_port) calls gethostname(), which the PS2 newlib / ps2ip stack
 * does not provide. wLaunchELF only uses libsmb2 as a *client*, so this code
 * is never actually executed at runtime, but the symbol must exist for the
 * link to succeed.
 *
 * libnfs' own PS2 shims (nfs_getaddrinfo / nfs_freeaddrinfo) live in its
 * ps2_ee/ps2_compat.c and are handled by patching libnfs' Makefile during the
 * CI install step, so they are intentionally NOT duplicated here.
 */

#ifdef NETFS

#include <string.h>
#include <stddef.h>

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

#endif /* NETFS */
