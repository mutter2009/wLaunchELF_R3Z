/* -*-  mode:c; tab-width:8; c-basic-offset:8; indent-tabs-mode:nil;  -*- */
//---------------------------------------------------------------------------
//File name:   smb2.c
//---------------------------------------------------------------------------
/* SMB network file browser backend.
 *
 * HISTORY / WHY THIS REWRITE EXISTS
 * ---------------------------------
 * This file used to be a libsmb2 (SMB2/3) client. On real hardware it could
 * not talk to the SMB1-only Samba / router-USB shares that Open PS2 Loader
 * connects to fine, and libsmb2's PS2 port has no working command timeout:
 * smb2_connect_share() hung the whole GUI for over a minute. Moving that
 * call to a worker thread only hid the freeze - the mount still failed.
 *
 * It is now an SMB1 (CIFS) client built on the PS2SDK "smbman" IOP module,
 * the exact same driver OPL uses. smbman registers the "smb" iomanX device,
 * has its own keepalive/auto-relogin, and once LOGON + OPENSHARE succeed the
 * share is just a normal device: all reads/writes go through fileXio*().
 *
 * IMPORTANT: every symbol declared in smb2.h keeps its original name and
 * signature. filer.c / filer_fileops.c / the settings pages / the language
 * tables / the (already finished) layout are therefore completely unchanged.
 */
#include <debug.h>
#include <netman.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <ps2ip.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <launchelf.h>
#include <sbv_patches.h>
#include <smb2.h>
#include <ps2smb.h>
#include <filer_shared.h>

#define SMB_DEV "smb:"

struct smb2_share *smb2_shares = NULL;

/* Status line shown by the file browser (empty while at least one share is up). */
char smb2_status_msg[128] = "";
int smb2_last_error = SMB2_ERR_NONE;
/* fix24: live "where are we" marker for on-screen diagnostics. The watchdog
 * appends this to the error line so the exact stuck/failed step is visible. */
char smb2_diag_stage[96] = "";
static int smb2_reload_pending = 0;

/* fix29 DIAGNOSTIC ONLY: set the stage AND paint it. drawMsg is pure EE/GS
 * drawing (no SIF), so when a later fileXioDevctl wedges the IOP the screen
 * stays frozen on the last painted step, pinpointing the failing layer. */
static void smbDiagPaint(const char *step)
{
	snprintf(smb2_diag_stage, sizeof(smb2_diag_stage), "%s", step);
	drawMsg(step);
}

void smb2_request_reload(void)
{
	smb2_reload_pending = 1;
}

int smb2_take_reload_request(void)
{
	int pending = smb2_reload_pending;

	smb2_reload_pending = 0;
	return pending;
}

/* Shared NETFS network-init guard (also referenced by nfs.c). */
int netfs_net_inited = 0;

#define discard_const(ptr) ((void *)((intptr_t)(ptr)))

static struct smb2_share *smb2_find_share(const char *name);
static void find_share(const char *path, char **name, char **p,
                       struct smb2_share **share);

/* ---- link-state wait (unchanged behaviour: bounded, never hangs the GUI) -- */
static void EthStatusCheckCb(s32 alarm_id, u16 time, void *common)
{
	iWakeupThread(*(int *)common);
}

static int WaitValidNetState(int (*checkingFunction)(void))
{
	int ThreadID, retry_cycles;

	ThreadID = GetThreadId();
	for (retry_cycles = 0; checkingFunction() == 0; retry_cycles++) {
		SetAlarm(1000 * 16, &EthStatusCheckCb, &ThreadID);
		SleepThread();
		if (retry_cycles >= 5)
			return -1;
	}
	return 0;
}

static int ethGetNetIFLinkStatus(void)
{
	return (NetManIoctl(NETMAN_NETIF_IOCTL_GET_LINK_STATUS, NULL, 0, NULL, 0) == NETMAN_NETIF_ETH_LINK_STATE_UP);
}

static int ethWaitValidNetIFLinkState(void)
{
	return WaitValidNetState(&ethGetNetIFLinkStatus);
}

/* ---- SMB1 (smbman) session state ---------------------------------------- */
/* smbman is a single-share device: it holds one LOGON session and one open
 * tree (share) at a time. We keep the configured shares in the same linked
 * list the browser already understands, and switch the active tree whenever
 * a path addresses a different share/server. */
static struct smb2_share *smb1_active_share = NULL;
static char smb1_active_server[64] = "";

static void smb1_reset_session(void)
{
	smb1_active_share = NULL;
	smb1_active_server[0] = '\0';
}

/* Build the NTLM hashes when a password is configured; matches OPL exactly. */
static int smb1_build_logon(struct smb2_share *s, smbLogOn_in_t *logon,
                            smbOpenShare_in_t *openshare)
{
	/* fix14: smbman rejects an empty User with a stalled NTLM session setup
	 * on many Samba / router shares. Open PS2 Loader (which connects fine
	 * here) and wLaunchELF's own SMB_test.c both substitute GUEST whenever
	 * the configured username is empty. This is a connection-layer fallback
	 * only — the saved SMB.CNF and the settings UI still show it empty, so
	 * there is no forced "GUEST" default in the config. */
	const char *user = (s->user && s->user[0]) ? s->user : "GUEST";

	memset(logon, 0, sizeof(*logon));
	memset(openshare, 0, sizeof(*openshare));

	snprintf(logon->serverIP, sizeof(logon->serverIP), "%s", s->server);
	logon->serverPort = s->port;
	snprintf(logon->User, sizeof(logon->User), "%s", user);

	if (s->password && s->password[0]) {
		smbGetPasswordHashes_in_t passwd;
		smbGetPasswordHashes_out_t hashes;

		memset(&passwd, 0, sizeof(passwd));
		memset(&hashes, 0, sizeof(hashes));
		strncpy(passwd.password, s->password, sizeof(passwd.password) - 1);
		if (fileXioDevctl(SMB_DEV, SMB_DEVCTL_GETPASSWORDHASHES,
		                  &passwd, sizeof(passwd), &hashes, sizeof(hashes)) == 0) {
			memcpy(logon->Password, &hashes, sizeof(hashes));
			logon->PasswordType = HASHED_PASSWORD;
			memcpy(openshare->Password, &hashes, sizeof(hashes));
			openshare->PasswordType = HASHED_PASSWORD;
		} else {
			strncpy(logon->Password, s->password, sizeof(logon->Password) - 1);
			logon->PasswordType = PLAINTEXT_PASSWORD;
			strncpy(openshare->Password, s->password, sizeof(openshare->Password) - 1);
			openshare->PasswordType = PLAINTEXT_PASSWORD;
		}
	} else {
		/* No password configured: anonymous / guest share. */
		logon->PasswordType = NO_PASSWORD;
		openshare->PasswordType = NO_PASSWORD;
	}

	snprintf(openshare->ShareName, sizeof(openshare->ShareName), "%s", s->sharename);
	return 0;
}

/* fix16: quick TCP reachability probe before the SMB LOGON.
 *
 * smbman's fileXioDevctl(SMB_DEVCTL_LOGON) has no timeout: when the host is
 * down / the port is closed / the address is wrong it can block the IOP fileXio
 * RPC for minutes. That single stuck RPC wedges the shared SIF channel, so the
 * browser's re-listing AND padRead() both stall — the UI shows the watchdog
 * error but no button works. Probing the TCP port first with a bounded,
 * non-blocking connect lets us fail fast in that case and never enter the
 * blocking LOGON at all, so the interface stays responsive.
 *
 * Returns 1 = reachable (or a non-numeric host we cannot probe here),
 *         0 = definitively unreachable within the timeout. */
#define SMB1_PROBE_TIMEOUT_MS 2500
static int smb1_server_reachable(const char *server, int port)
{
	struct sockaddr_in addr;
	struct timeval tv;
	fd_set wfds;
	socklen_t errlen;
	int fd, flags, rc, err;

	if (server == NULL || server[0] == '\0')
		return 0;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((unsigned short)((port > 0 && port < 65536) ? port : 445));
	addr.sin_addr.s_addr = inet_addr(server);
	/* Hostname (not a dotted quad): let smbman resolve it, do not probe. */
	if (addr.sin_addr.s_addr == INADDR_NONE)
		return 1;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return 1; /* socket unavailable: don't block LOGON on this account */

	flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
	if (rc < 0) {
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(fd, &rfds);
		FD_SET(fd, &wfds);
		tv.tv_sec = SMB1_PROBE_TIMEOUT_MS / 1000;
		tv.tv_usec = (SMB1_PROBE_TIMEOUT_MS % 1000) * 1000;
		rc = select(fd + 1, &rfds, &wfds, NULL, &tv);
		if (rc <= 0) { /* timeout or select error */
			close(fd);
			return 0;
		}
	}

	/* Distinguish a real connection from a deferred connect error. */
	err = 0;
	errlen = sizeof(err);
	getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen);
	close(fd);
	return (err == 0);
}

/* LOGON to a server if it is not the one already logged on. */
static int smb1_logon(struct smb2_share *s)
{
	smbLogOn_in_t logon;
	smbOpenShare_in_t openshare;
	int ret;

	if (strcmp(smb1_active_server, s->server) == 0)
		return 0; /* already logged on to this server */

	/* fix16: never enter the unbounded LOGON devctl when the TCP port is
	 * unreachable — that is what wedged the SIF channel and froze input. */
	{
		char diagstep[96];
		snprintf(diagstep, sizeof(diagstep), "29:TCP探测 %s:%d", s->server, s->port);
		smbDiagPaint(diagstep);
	}
	if (!smb1_server_reachable(s->server, s->port)) {
		DPRINTF("smb1: %s:%d unreachable (TCP probe), skipping LOGON\n",
		        s->server, s->port);
		snprintf(smb2_diag_stage, sizeof(smb2_diag_stage), "TCP不通 %s:%d",
		         s->server, s->port);
		smb1_reset_session();
		smb2_last_error = SMB2_ERR_CONNECT;
		return -1;
	}

	smb1_build_logon(s, &logon, &openshare);
	{
		char diagstep[96];
		snprintf(diagstep, sizeof(diagstep), "29:LOGON %s/%s", s->server, logon.User);
		smbDiagPaint(diagstep);
	}
	ret = fileXioDevctl(SMB_DEV, SMB_DEVCTL_LOGON, &logon, sizeof(logon), NULL, 0);
	if (ret < 0) {
		DPRINTF("smb1: LOGON to %s failed: %d\n", s->server, ret);
		snprintf(smb2_diag_stage, sizeof(smb2_diag_stage), "LOGON失败=%d", ret);
		smb1_reset_session();
		smb2_last_error = SMB2_ERR_CONNECT;
		return -1;
	}
	snprintf(smb1_active_server, sizeof(smb1_active_server), "%s", s->server);
	smb1_active_share = NULL; /* tree must be reopened on the new session */
	return 0;
}

/* OPENSHARE so that "smb:" points at this share. */
static int smb1_open_share(struct smb2_share *s)
{
	smbLogOn_in_t logon;
	smbOpenShare_in_t openshare;
	int ret;

	if (smb1_logon(s) < 0)
		return -1;
	if (smb1_active_share == s)
		return 0;

	smb1_build_logon(s, &logon, &openshare);
	{
		char diagstep[96];
		snprintf(diagstep, sizeof(diagstep), "29:打开共享 %s", s->sharename);
		smbDiagPaint(diagstep);
	}
	ret = fileXioDevctl(SMB_DEV, SMB_DEVCTL_OPENSHARE, &openshare, sizeof(openshare), NULL, 0);
	if (ret < 0) {
		DPRINTF("smb1: OPENSHARE %s failed: %d\n", s->sharename, ret);
		snprintf(smb2_diag_stage, sizeof(smb2_diag_stage), "共享打开失败=%d", ret);
		smb2_last_error = SMB2_ERR_CONNECT;
		return -1;
	}
	snprintf(smb2_diag_stage, sizeof(smb2_diag_stage), "已连接 %s/%s",
	         s->server, s->sharename);
	smb1_active_share = s;
	return 0;
}

/* Resolve a "SMB:<name>/<rest>" UI path into the active share + a plain
 * "smb:/<rest>" device path. Kept separate so all file ops share one path. */
static int smb1_resolve(const char *path, char *devpath, size_t devpath_size)
{
	struct smb2_share *share = NULL;
	char *name = NULL, *p = NULL;

	find_share(path, &name, &p, &share);
	if (share == NULL) {
		free(name);
		return -1;
	}
	free(name);

	if (smb1_open_share(share) < 0)
		return -1;

	snprintf(devpath, devpath_size, "%s%s%s", SMB_DEV,
	         (p && *p) ? "/" : "", (p && *p) ? p : "");
	return 0;
}

/* ---- CNF parsing --------------------------------------------------------- */
/* Parse "smb://host[:port]/share" into its parts. Tolerates a trailing slash
 * and a share-less URL (treated as no share -> browser shows the reason). */
static int smb1_parse_url(const char *url, char *server, size_t server_sz,
                          int *port, char *sharename, size_t share_sz)
{
	const char *host = url;
	const char *slash;
	char *colon;
	char hostbuf[128];

	server[0] = '\0';
	sharename[0] = '\0';
	*port = 445;

	if (url == NULL || url[0] == '\0')
		return -1;
	if (!strncmp(url, "smb://", 6))
		host = url + 6;
	else if (!strncmp(url, "\\\\", 2))
		host = url + 2;

	slash = strchr(host, '/');
	if (slash != NULL) {
		size_t hlen = (size_t)(slash - host);
		if (hlen >= sizeof(hostbuf))
			hlen = sizeof(hostbuf) - 1;
		memcpy(hostbuf, host, hlen);
		hostbuf[hlen] = '\0';
		snprintf(sharename, share_sz, "%s", slash + 1);
		/* strip trailing slashes from the share name */
		while (sharename[0] && sharename[strlen(sharename) - 1] == '/')
			sharename[strlen(sharename) - 1] = '\0';
	} else {
		snprintf(hostbuf, sizeof(hostbuf), "%s", host);
	}

	colon = strrchr(hostbuf, ':');
	if (colon != NULL) {
		int p = atoi(colon + 1);
		*colon = '\0';
		if (p > 0 && p < 65536)
			*port = p;
	}
	snprintf(server, server_sz, "%s", hostbuf);
	return (server[0] && sharename[0]) ? 0 : -1;
}

static void free_smb2_share(struct smb2_share *s)
{
	if (s == NULL)
		return;
	free(s->name);
	free(discard_const(s->user));
	free(discard_const(s->password));
	free(s->url);
	if (s == smb1_active_share)
		smb1_reset_session();
	free(s);
}

static int loadSMB2CNF(char *path)
{
	char *RAM_p;
	char *CNF_p, *name, *value;
	struct smb2_share *smb2_share = NULL;
	int entries = 0;

	if (!(RAM_p = preloadCNF(path)))
		return entries;
	CNF_p = RAM_p;
	while (get_CNF_string(&CNF_p, &name, &value)) {
		if (smb2_share == NULL) {
			smb2_share = calloc(1, sizeof(struct smb2_share));
			if (smb2_share == NULL)
				break;
		}
		if (!strcmp(name, "NAME")) {
			free(smb2_share->name);
			smb2_share->name = strdup(value);
		} else if (!strcmp(name, "USERNAME")) {
			free(discard_const(smb2_share->user));
			smb2_share->user = strdup(value);
		} else if (!strcmp(name, "PASSWORD")) {
			free(discard_const(smb2_share->password));
			smb2_share->password = strdup(value);
		} else if (!strcmp(name, "URL")) {
			if (smb2_share->name == NULL)
				smb2_share->name = strdup("SMB");
			if (value[0] == '\0') {
				smb2_last_error = SMB2_ERR_NO_SHARE;
				continue;
			}
			if (smb2_find_share(smb2_share->name)) {
				free_smb2_share(smb2_share);
				smb2_share = NULL;
				continue;
			}
			smb2_share->url = strdup(value);
			if (smb1_parse_url(value, smb2_share->server, sizeof(smb2_share->server),
			                   &smb2_share->port, smb2_share->sharename,
			                   sizeof(smb2_share->sharename)) < 0) {
				DPRINTF("invalid SMB URL: %s\n", value);
				smb2_last_error = SMB2_ERR_NO_SHARE;
				free_smb2_share(smb2_share);
				smb2_share = NULL;
				continue;
			}
			/* LOGON + OPENSHARE (SMB1, via the smbman IOP module). */
			if (smb1_open_share(smb2_share) < 0) {
				free_smb2_share(smb2_share);
				smb2_share = NULL;
				continue;
			}
			DPRINTF("Mounted SMB1 share \\\\%s\\%s as %s\n",
			        smb2_share->server, smb2_share->sharename, smb2_share->name);
			smb2_last_error = SMB2_ERR_NONE;
			smb2_share->next = smb2_shares;
			smb2_shares = smb2_share;
			smb2_share = NULL;
			entries++;
		}
	}
	free(RAM_p);
	if (smb2_share != NULL)
		free_smb2_share(smb2_share);
	return entries;
}

void deinit_smb2(void)
{
	struct smb2_share *smb2_share;

	/* Close the tree and log off once, before discarding the share list. */
	if (smb1_active_share != NULL) {
		fileXioDevctl(SMB_DEV, SMB_DEVCTL_CLOSESHARE, NULL, 0, NULL, 0);
		fileXioDevctl(SMB_DEV, SMB_DEVCTL_LOGOFF, NULL, 0, NULL, 0);
		smb1_reset_session();
	}
	while (smb2_shares) {
		smb2_share = smb2_shares->next;
		free_smb2_share(smb2_shares);
		smb2_shares = smb2_share;
	}
}

/* A CNF file that exists wins the search; do not reconnect to the same dead
 * server once per candidate path (that felt exactly like a console freeze). */
static int smb2TryCnf(const char *path)
{
	char *ram = preloadCNF((char *)path);

	if (ram == NULL)
		return -1;
	free(ram);
	smb2_last_error = SMB2_ERR_NO_SHARE;
	return loadSMB2CNF((char *)path);
}

static int smb2LoadAllCNF(void)
{
	static const char * const fixed[] = {
		"mc0:/SYS-CONF/SMB.CNF",
		"mc1:/SYS-CONF/SMB.CNF",
		"mass:/SYS-CONF/SMB.CNF",
		"mass:/SMB.CNF",
		"mc0:/SMB.CNF"
	};
	char path[MAX_PATH];
	char tmp[MAX_PATH];
	const char *dir;
	char *slash;
	int rc, i;

	smb2_last_error = SMB2_ERR_NO_CNF;

	if (LoadedIPConfigPath[0] != '\0') {
		snprintf(tmp, sizeof(tmp), "%s", LoadedIPConfigPath);
		slash = strrchr(tmp, '/');
		if (slash != NULL) {
			slash[1] = '\0';
			snprintf(path, sizeof(path), "%sSMB.CNF", tmp);
			rc = smb2TryCnf(path);
			if (rc >= 0)
				return rc;
		}
	}

	if (setting != NULL && setting->CNF_Path[0] != '\0') {
		snprintf(tmp, sizeof(tmp), "%s", setting->CNF_Path);
		slash = strrchr(tmp, '/');
		if (slash != NULL) {
			slash[1] = '\0';
			snprintf(path, sizeof(path), "%sSMB.CNF", tmp);
			rc = smb2TryCnf(path);
			if (rc >= 0)
				return rc;
		}
	}

	dir = LaunchElfBootDir[0] ? LaunchElfBootDir : LaunchElfDir;
	if (dir[0] != '\0') {
		snprintf(path, sizeof(path), "%s%sSMB.CNF", dir,
		         (dir[strlen(dir) - 1] == '/') ? "" : "/");
		rc = smb2TryCnf(path);
		if (rc >= 0)
			return rc;
	}

	for (i = 0; i < (int)(sizeof(fixed) / sizeof(fixed[0])); i++) {
		rc = smb2TryCnf(fixed[i]);
		if (rc >= 0)
			return rc;
	}
	return 0;
}

/* Re-read SMB.CNF without re-initialising NetMan/ps2ip or waiting for link. */
int smb2_reload(void)
{
	deinit_smb2();
	return smb2LoadAllCNF();
}

int init_smb2(const char *ip, const char *netmask, const char *gw)
{
	struct ip4_addr IP, NM, GW;
	int ip4[4];
	int rc;

	if (!netfs_net_inited) {
		NetManInit();
		sscanf(ip, "%d.%d.%d.%d", &ip4[0], &ip4[1], &ip4[2], &ip4[3]);
		IP4_ADDR(&IP, ip4[0], ip4[1], ip4[2], ip4[3]);
		sscanf(netmask, "%d.%d.%d.%d", &ip4[0], &ip4[1], &ip4[2], &ip4[3]);
		IP4_ADDR(&NM, ip4[0], ip4[1], ip4[2], ip4[3]);
		sscanf(gw, "%d.%d.%d.%d", &ip4[0], &ip4[1], &ip4[2], &ip4[3]);
		IP4_ADDR(&GW, ip4[0], ip4[1], ip4[2], ip4[3]);
		ps2ipInit(&IP, &NM, &GW);
		netfs_net_inited = 1;
	}

	smbDiagPaint("29:等待网线链路");
	DPRINTF("Waiting for connection...\n");
	if (ethWaitValidNetIFLinkState() != 0) {
		DPRINTF("Error: failed to get valid link status.\n");
		snprintf(smb2_diag_stage, sizeof(smb2_diag_stage), "网线未连接");
		smb2_last_error = SMB2_ERR_LINK;
		return -1;
	}
	DPRINTF("Network Initialized\n");

	smbDiagPaint("29:读取SMB.CNF");
	rc = smb2LoadAllCNF();
	DPRINTF("init_smb2: %d share(s) mounted.\n", rc);
	return rc;
}

int smb2_mount_count(void)
{
	struct smb2_share *share;
	int n = 0;

	for (share = smb2_shares; share != NULL; share = share->next)
		n++;
	return n;
}

static struct smb2_share *smb2_find_share(const char *name)
{
	struct smb2_share *share;

	for (share = smb2_shares; share; share = share->next) {
		if (!strcmp(share->name, name))
			return share;
	}
	return NULL;
}

/* Split "SMB:<name>/<rest>" into share name and relative path. Identical
 * semantics to the old libsmb2 version so the rest of wLE is unaffected. */
static void find_share(const char *path, char **name, char **p,
                       struct smb2_share **share)
{
	*name = strdup(&path[4]);
	if (*name == NULL) {
		*share = NULL;
		*p = NULL;
		return;
	}
	if ((*name)[0] == '/')
		memmove(*name, *name + 1, strlen(*name) + 1);
	*p = strchr(*name, '/');
	if (*p != NULL) {
		*((*p)++) = 0;
	} else {
		*p = *name + strlen(*name);
	}
	*share = smb2_find_share(*name);
}

int readSMB2(const char *path, FILEINFO *info, int max)
{
	int n = 0, dd = -1;
	char devpath[MAX_PATH];
	iox_dirent_t record;

	/* Root of SMB: is the list of configured shares. */
	if (path[4] == '\0' || (path[4] == '/' && path[5] == '\0')) {
		struct smb2_share *share;
		for (share = smb2_shares; share; share = share->next) {
			if (strlen(share->name) >= MAX_NAME)
				continue;
			strncpy(info[n].name, share->name, MAX_NAME - 1);
			info[n].name[MAX_NAME - 1] = 0;
			clear_mcTable(&info[n].stats);
			info[n].stats.AttrFile = MC_ATTR_norm_folder;
			strncpy((char *)info[n].stats.EntryName, info[n].name, 32);
			n++;
			if (n >= max)
				break;
		}
		size_valid = 1;
		time_valid = 1;
		return n;
	}

	if (smb1_resolve(path, devpath, sizeof(devpath)) < 0)
		return 0;

	if ((dd = fileXioDopen(devpath)) < 0)
		return 0;
	while (fileXioDread(dd, &record) > 0) {
		if (FIO_S_ISDIR(record.stat.mode) &&
		    (!strcmp(record.name, ".") || !strcmp(record.name, "..")))
			continue;
		if (strlen(record.name) >= MAX_NAME)
			continue;

		strncpy(info[n].name, record.name, MAX_NAME - 1);
		info[n].name[MAX_NAME - 1] = 0;
		clear_mcTable(&info[n].stats);
		if (FIO_S_ISDIR(record.stat.mode)) {
			info[n].stats.AttrFile = MC_ATTR_norm_folder;
		} else if (FIO_S_ISREG(record.stat.mode)) {
			info[n].stats.AttrFile = MC_ATTR_norm_file;
			info[n].stats.FileSizeByte = record.stat.size;
			info[n].stats.Reserve2 = record.stat.hisize;
		} else {
			continue;
		}
		snprintf((char *)info[n].stats.EntryName, 32, "%.31s", info[n].name);
		memcpy((void *)&info[n].stats._Create, record.stat.ctime, 8);
		memcpy((void *)&info[n].stats._Modify, record.stat.mtime, 8);
		n++;
		if (n >= max)
			break;
	}
	size_valid = 1;
	time_valid = 1;
	fileXioDclose(dd);
	return n;
}

int SMB2mkdir(const char *path, int fileMode)
{
	char devpath[MAX_PATH];
	if (path[4] == '\0')
		return -EINVAL;
	if (smb1_resolve(path, devpath, sizeof(devpath)) < 0)
		return -EINVAL;
	return fileXioMkdir(devpath, fileMode);
}

int SMB2rmdir(const char *path)
{
	char devpath[MAX_PATH];
	if (path[4] == '\0')
		return -EINVAL;
	if (smb1_resolve(path, devpath, sizeof(devpath)) < 0)
		return -EINVAL;
	if (devpath[strlen(devpath) - 1] == '/')
		devpath[strlen(devpath) - 1] = '\0';
	return fileXioRmdir(devpath);
}

int SMB2unlink(const char *path)
{
	char devpath[MAX_PATH];
	if (path[4] == '\0')
		return -EINVAL;
	if (smb1_resolve(path, devpath, sizeof(devpath)) < 0)
		return -EINVAL;
	return fileXioRemove(devpath);
}

struct SMB2FH *SMB2open(const char *path, int mode)
{
	char devpath[MAX_PATH];
	struct SMB2FH *fh;
	int fd;

	if (path[4] == '\0')
		return NULL;
	if (smb1_resolve(path, devpath, sizeof(devpath)) < 0)
		return NULL;
	fd = fileXioOpen(devpath, mode, 0666);
	if (fd < 0)
		return NULL;
	fh = malloc(sizeof(struct SMB2FH));
	if (fh == NULL) {
		fileXioClose(fd);
		return NULL;
	}
	fh->fd = fd;
	return fh;
}

int SMB2close(struct SMB2FH *fh)
{
	int ret;
	if (fh == NULL)
		return -1;
	ret = fileXioClose(fh->fd);
	free(fh);
	return ret;
}

int SMB2read(struct SMB2FH *fh, char *buf, int size)
{
	if (fh == NULL)
		return -1;
	return fileXioRead(fh->fd, buf, size);
}

int SMB2write(struct SMB2FH *fh, char *buf, int size)
{
	if (fh == NULL)
		return -1;
	return fileXioWrite(fh->fd, buf, size);
}

int SMB2lseek(struct SMB2FH *fh, int where, int how)
{
	if (fh == NULL)
		return -1;
	return (int)fileXioLseek(fh->fd, where, how);
}

int SMB2stat(const char *path, int *is_dir)
{
	char devpath[MAX_PATH];
	iox_stat_t st;

	if (path[4] == '\0' || (path[4] == '/' && path[5] == '\0')) {
		*is_dir = 1;
		return 0;
	}
	if (smb1_resolve(path, devpath, sizeof(devpath)) < 0)
		return -1;
	if (devpath[strlen(devpath) - 1] == '/')
		devpath[strlen(devpath) - 1] = '\0';
	if (fileXioGetStat(devpath, &st) < 0)
		return -1;
	*is_dir = FIO_S_ISDIR(st.mode) ? 1 : 0;
	return 0;
}

int SMB2rename(const char *path, const char *newpath)
{
	char devpath[MAX_PATH];
	char newdevpath[MAX_PATH];

	if (path[4] == '\0')
		return -EINVAL;
	if (smb1_resolve(path, devpath, sizeof(devpath)) < 0)
		return -ENOENT;
	/* newpath is a full "SMB:<share>/<rest>" path in wLE. */
	if (strncmp(newpath, "SMB:", 4) == 0) {
		if (smb1_resolve(newpath, newdevpath, sizeof(newdevpath)) < 0)
			return -ENOENT;
	} else {
		snprintf(newdevpath, sizeof(newdevpath), "%s%s%s", SMB_DEV,
		         (newpath[0] == '/') ? "" : "/", newpath);
	}
	return fileXioRename(devpath, newdevpath);
}

//---------------------------------------------------------------------------
//End of file: smb2.c
//---------------------------------------------------------------------------
