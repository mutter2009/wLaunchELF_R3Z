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

/* fix24: a short, human-readable description of the exact step the SMB1
 * connect worker is currently in (e.g. "TCP probe 192.168.1.1:445",
 * "LOGON GUEST", "OPENSHARE ps2"). Updated by smb2.c at each stage and shown
 * on screen when the watchdog fires, so a stuck/ failing step is visible
 * instead of a generic "connect failed". Defined in smb2.c. */
extern char smb2_diag_stage[96];

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

/* ---- OPL-style asynchronous SMB bring-up -------------------------------
 * libsmb2's smb2_connect_share() is synchronous and the PS2 port has no
 * working command timeout. Doing it on the GUI thread froze the browser for
 * minutes on an unreachable / SMB1-only server. Like OPL (which runs its SMB
 * logon on a background IO thread), all network work now happens on a worker
 * EE thread. The GUI only reads smb2_shares while state == DONE and refreshes
 * the listing once netfs_async_consume_completion() reports a finish. */
enum {
	NETFS_ASYNC_IDLE = 0,       /* no connect started yet */
	NETFS_ASYNC_CONNECTING,     /* worker owns the share list; do not read it */
	NETFS_ASYNC_DONE            /* share list is stable; smb2_last_error valid */
};
extern volatile int netfs_async_state;
extern volatile int netfs_async_generation;
/* fix20: TRUE while a connect worker thread is alive (cleared only when the
 * worker really returns). pad.c uses this to skip USB-keyboard SIF reads: a
 * watchdog timeout flips state to DONE but the worker may still hold SIF. */
extern volatile int netfs_async_worker_busy;

/* fix14: timestamp (seconds, via time(NULL)) of the last connect request.
 * The GUI watchdog uses it to break out of a LOGON that never returns, so
 * "正在启动 SMB 网络..." can never trap the browser forever. */
extern volatile unsigned int netfs_async_started_at;
#define NETFS_CONNECT_TIMEOUT_SEC 12

/* Kick off a background (re)connect. reload=1 re-reads SMB.CNF on an already
 * initialised stack, reload=0 performs the full stack bring-up. Never blocks.
 * Returns 0 if started (or already running), -1 if the thread could not be
 * created. */
int netfs_connect_async(int reload);

/* Returns 1 exactly once for every completed background connect. */
int netfs_async_consume_completion(void);

/* fix14: call from the GUI loop while CONNECTING. Breaks a stuck LOGON after
 * NETFS_CONNECT_TIMEOUT_SEC so the browser never freezes permanently. */
void netfs_async_watchdog(void);

/* Apply SMB config changes. Never blocks the GUI: hands the mount to the
 * background worker. The caller must have written SMB.CNF first. Returns 0
 * when the worker was started/already running, -1 on failure. */
int netfs_reconnect(void);

/* Returns 1 if SMB.CNF carries AUTO=1 (connect at startup), else 0. */
int netfs_autostart_enabled(void);

/* fix24: called from the GUI thread AFTER a failed SMB connect, before
 * rebootIopAndReloadCoreStackSilent(). Clears every cached NETFS flag so the
 * next visit re-loads smbman and re-initialises NetMan/ps2ip from scratch.
 * Without this, an IOP reset leaves have_smbman/netfs_net_inited set and the
 * next connect silently skips loading the driver -> permanent failure. */
void netfs_clear_stack_flags(void);

#endif /* NETFS_COMPAT_H */
//---------------------------------------------------------------------------
//End of file: netfs_compat.h
//---------------------------------------------------------------------------
