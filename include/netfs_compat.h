/* -*-  mode:c; tab-width:8; c-basic-offset:8; indent-tabs-mode:nil;  -*- */
//---------------------------------------------------------------------------
//File name:   netfs_compat.h
//---------------------------------------------------------------------------
// Compatibility shims that let sahlberg's NETFS (SMB2 / NFS) sources build
// inside the R3Z tree. The upstream NETFS code was written against an older
// wLaunchELF where these symbols were globally visible; R3Z keeps them local
// to individual .c files, so we re-provide them here.
//---------------------------------------------------------------------------
#ifndef NETFS_COMPAT_H
#define NETFS_COMPAT_H

#include <string.h>

/* MC attribute constants used by smb2.c / nfs.c (normally private to filer.c). */
#ifndef MC_ATTR_norm_folder
#define MC_ATTR_norm_folder 0x8427  /* Normal folder on PS2 MC */
#endif
#ifndef MC_ATTR_norm_file
#define MC_ATTR_norm_file   0x8497  /* file (PS2/PS1) on PS2 MC */
#endif

/* R3Z keeps clearMcTable() as a static in filer.c (capital M). The NETFS code
 * expects the lowercase variant, so provide it. */
static inline void clear_mcTable(sceMcTblGetDir *mcT)
{
	memset((void *)mcT, 0, sizeof(sceMcTblGetDir));
}

/* Set once NetMan + ps2ip have been initialised on the EE side. Acts as an
 * idempotency guard shared between smb2.c and nfs.c. */
extern int netfs_net_inited;

/* Bring up the network stack (loads SMAP/PS2IP IOP modules, then NetMan +
 * ps2ip on the EE) and mount any SMB2 / NFS shares described in SMB2.CNF /
 * NFS.CNF. Idempotent. Returns 0 on success. */
int init_netfs(void);

/* Re-establish SMB2/NFS mounts after the config changed: tear down the
 * existing mounts and re-read SMB.CNF / NFS.CNF, re-mounting the shares.
 * The EE network stack (netman/ps2ip) is left up. Returns 0 on success,
 * -1 if the link never came up. */
int netfs_reconnect(void);

/* Returns 1 if SMB.CNF carries AUTO=1 (connect at startup), else 0. */
int netfs_autostart_enabled(void);

#endif /* NETFS_COMPAT_H */
//---------------------------------------------------------------------------
//End of file: netfs_compat.h
//---------------------------------------------------------------------------
