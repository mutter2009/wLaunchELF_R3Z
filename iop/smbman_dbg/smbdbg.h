/*
 * smbdbg.h - diagnostic logger for the vendored smbman IOP module.
 * Diagnostic-only; not part of upstream PS2SDK smbman.
 */
#ifndef SMBDBG_H
#define SMBDBG_H

void smbdbg_open(void);
void smbdbg_close(void);
void smbdbg_write(const char *s);

/* IOP sysclib has sprintf() but no vsprintf(), so format into a stack buffer
 * at the call site and hand the finished line to smbdbg_write(). */
#define smbdbg_printf(fmt, args...)                 \
    do {                                            \
        char _smbdbg_line[512];                     \
        int _smbdbg_len = sprintf(_smbdbg_line, fmt, ##args); \
        if (_smbdbg_len > 0)                        \
            smbdbg_write(_smbdbg_line);             \
    } while (0)

#endif
