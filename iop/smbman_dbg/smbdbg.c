/*
 * smbdbg.c - diagnostic logger for the vendored smbman IOP module.
 *
 * Appends a plain-text trace to mass:/SMBDEBUG.LOG so a real PS2 can report
 * exactly where SMB_DEVCTL_LOGON fails, with no serial cable or sniffer.
 * Best-effort: logging must never alter SMB behaviour, so open/write errors
 * are silently ignored and the SMB code path never branches on a log result.
 *
 * Uses the plain ioman open()/close()/write() API (same as ps2ftpd), which is
 * backed by iomanX at runtime and needs only three extra IRX imports.
 *
 * Diagnostic-only; not part of upstream PS2SDK smbman.
 */

#include <types.h>
#include <stdio.h>
#include <sysclib.h>
#include <ioman.h>

#include "smbdbg.h"

/* Values from PS2SDK iop/kernel/include/sys/fcntl.h. Defined locally so the
 * probe does not depend on that header being on the module include path. */
#ifndef O_WRONLY
#define O_WRONLY 0x0002
#endif
#ifndef O_APPEND
#define O_APPEND 0x0100
#endif
#ifndef O_CREAT
#define O_CREAT  0x0200
#endif

#define SMBDBG_PATH  "mass:/SMBDEBUG.LOG"
#define SMBDBG_PATH2 "mc0:/SMBDEBUG.LOG"

static int smbdbg_fd = -1;
static int smbdbg_tried = 0;

void smbdbg_open(void)
{
    if (smbdbg_tried)
        return;
    smbdbg_tried = 1;
    /* O_WRONLY | O_CREAT | O_APPEND - keep lines across LOGON retries. */
    smbdbg_fd = open(SMBDBG_PATH, O_WRONLY | O_CREAT | O_APPEND);
    if (smbdbg_fd < 0)
        smbdbg_fd = open(SMBDBG_PATH2, O_WRONLY | O_CREAT | O_APPEND);
}

void smbdbg_close(void)
{
    if (smbdbg_fd >= 0) {
        close(smbdbg_fd);
        smbdbg_fd = -1;
    }
}

void smbdbg_write(const char *s)
{
    int len;

    smbdbg_open();
    if (smbdbg_fd < 0)
        return;

    len = strlen(s);
    if (len > 0)
        (void)write(smbdbg_fd, (void *)s, len);
}
