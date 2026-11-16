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

/* Status line shown by the file browser while visiting SMB:. Empty when at
 * least one share is mounted. Defined in smb2.c. */
extern char smb2_status_msg[128];

/* OPL-style startup status codes (OPL uses gNetworkStartup +
 * ethDisplayErrorStatus()). Instead of an unexplained empty SMB folder, the
 * GUI now reports WHY the mount failed. Defined in smb2.c. */
enum {
	SMB2_ERR_NONE = 0,
	SMB2_ERR_NO_CNF,    /* no SMB.CNF found in any candidate path */
	SMB2_ERR_NO_SHARE,  /* CNF found but URL / share name empty */
	SMB2_ERR_LINK,      /* Ethernet link never came up */
	SMB2_ERR_CONNECT    /* server refused / timed out / bad credentials */
};
extern int smb2_last_error;

/* Re-read SMB.CNF without touching NetMan/ps2ip or waiting for the link. */
int smb2_reload(void);

/* The settings page must never run a synchronous mount from the GUI thread.
 * It sets a pending flag instead; the next visit to "SMB:" consumes it. */
void smb2_request_reload(void);
int smb2_take_reload_request(void);

/* Bring up the network stack (loads SMAP/PS2IP IOP modules, then NetMan +
 * ps2ip on the EE) and mount any SMB2 / NFS shares described in SMB2.CNF /
 * NFS.CNF. Idempotent. Returns 0 on success. */
int init_netfs(void);

/* Apply SMB config changes. If the stack is up, re-read SMB.CNF and re-mount
 * (each context has a bounded command timeout). If not, just queue the
 * request: the next visit to "SMB:" brings up the stack. Never loads IOP
 * modules from the settings GUI. Returns 0 on success, -1 otherwise. */
int netfs_reconnect(void);

/* Returns 1 if SMB.CNF carries AUTO=1 (connect at startup), else 0. */
int netfs_autostart_enabled(void);

#endif /* NETFS_COMPAT_H */
//---------------------------------------------------------------------------
//End of file: netfs_compat.h
//---------------------------------------------------------------------------
