/* -*-  mode:c; tab-width:8; c-basic-offset:8; indent-tabs-mode:nil;  -*- */
//---------------------------------------------------------------------------
//File name:   smb2.c
//---------------------------------------------------------------------------
/* fix17: back to the original, proven libsmb2 (SMB2) backend.
 *
 * fix13..16 replaced this with the SMB1 smbman.irx backend to match OPL, but
 * it never connected on the user's Samba share and each workaround (worker
 * thread, watchdog, generation tokens, TCP pre-probe, no re-list) only moved
 * the freeze around. The user's mature, working tools are SMB2FM (SMB2), so
 * SMB2 is the correct protocol here. This file restores the original libsmb2
 * implementation and adds ONE crucial fix the original lacked:
 * smb2_set_timeout(), so an unreachable server fails in seconds instead of
 * hanging the EE thread forever.
 *
 * All public symbols, struct names, language strings, settings pages and the
 * (already finished) layout are unchanged. The async framework in init.c /
 * filer*.c is kept: it simply wraps this backend in a worker thread.
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
#include <filer_shared.h>

/* fix18c: the PS2SDK's libsmb2.h uses SMB2_GUID_SIZE (line 461) and the
 * smb2_lease_key type (line 726) but does not include the header that defines
 * them (smb2/smb2.h). Newer GCC errors out on this. Include it first so the
 * two symbols are declared before libsmb2.h uses them. */
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>

/* fix18d: libsmb2.a calls gethostname() (nm: "U gethostname" in libsmb2.c.obj)
 * but neither libsmb2.a nor the PS2SDK defines it, so the final link fails
 * with "undefined reference to gethostname". wLaunchELF is an SMB client only
 * (the server path that reads the hostname never runs), so a harmless static
 * value is enough — the symbol just has to exist to satisfy the linker.
 * Signature matches newlib's <sys/unistd.h> declaration. poll() is NOT
 * defined here: libsmb2.a supplies its own (compat.c.obj: "T poll"). */
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

#define discard_const(ptr) ((void *)((intptr_t)(ptr)))

struct smb2_share *smb2_shares = NULL;

/* Status line shown by the file browser (empty while at least one share is up). */
char smb2_status_msg[128] = "";
int smb2_last_error = SMB2_ERR_NONE;
static int smb2_reload_pending = 0;

/* Shared NETFS network-init guard (also referenced by nfs.c). */
int netfs_net_inited = 0;

/* Seconds before libsmb2 aborts a stalled command (connect included). The
 * original code never set this (default 0 = wait forever) — that is why a
 * dead/unreachable server froze the UI on "正在启动 SMB 网络...". */
#define SMB2_COMMAND_TIMEOUT_SEC 6

/* fix18: belt-and-braces TCP reachability probe.
 * smb2_set_timeout() bounds libsmb2 command timeouts, but on the PS2 port the
 * initial socket connect() can still stall on a firewall that drops SYN
 * packets (no RST), which a timeout alone does not always rescue. A bounded
 * non-blocking connect + select() lets an unreachable host fail in ~2.5s and
 * keeps the GUI responsive. This is the same proven probe fix16 used for
 * smbman; port 445 is fixed because libsmb2's URL has no port field.
 * Returns 1 = reachable (or a hostname we cannot probe, let libsmb2 resolve),
 *         0 = definitively unreachable within the timeout. */
#define SMB2_PROBE_TIMEOUT_MS 2500
static int smb2_server_reachable(const char *server)
{
	struct sockaddr_in addr;
	struct timeval tv;
	fd_set wfds, rfds;
	socklen_t errlen;
	int fd, flags, rc, err;

	if (server == NULL || server[0] == '\0')
		return 0;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(445);
	addr.sin_addr.s_addr = inet_addr(server);
	/* Non-numeric host: let libsmb2 resolve it, do not block on a probe. */
	if (addr.sin_addr.s_addr == INADDR_NONE)
		return 1;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return 1; /* socket unavailable: don't block connect on this account */

	flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
	if (rc < 0) {
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(fd, &rfds);
		FD_SET(fd, &wfds);
		tv.tv_sec = SMB2_PROBE_TIMEOUT_MS / 1000;
		tv.tv_usec = (SMB2_PROBE_TIMEOUT_MS % 1000) * 1000;
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

static struct smb2_share *smb2_find_share(const char *name);
static void find_share(const char *path, char **name, char **p,
                       struct smb2_share **share);

/* ---- link-state wait (bounded, never hangs forever) ---------------------- */
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
		if (retry_cycles >= 10)
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

/* ---- share list ---------------------------------------------------------- */
static void set_time(sceMcStDateTime *ps2time, u64 smb2time)
{
	struct tm *tm;
	time_t t = smb2time & 0xffffffff;

	tm = localtime(&t);
	if (tm == NULL)
		return;
	ps2time->Sec   = tm->tm_sec;
	ps2time->Min   = tm->tm_min;
	ps2time->Hour  = tm->tm_hour;
	ps2time->Day   = tm->tm_mday + 1;
	ps2time->Month = tm->tm_mon + 1;
	ps2time->Year  = tm->tm_year + 1900;
}

static void free_smb2_share(struct smb2_share *smb2_share)
{
	if (smb2_share == NULL)
		return;
	if (smb2_share->smb2) {
		smb2_disconnect_share(smb2_share->smb2);
		smb2_destroy_context(smb2_share->smb2);
	}
	free(smb2_share->name);
	free(discard_const(smb2_share->user));
	free(discard_const(smb2_share->password));
	free(smb2_share->url);
	free(smb2_share);
}

/* Parse one SMB.CNF into mounted libsmb2 shares. */
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
			smb2_share = malloc(sizeof(struct smb2_share));
			memset(smb2_share, 0, sizeof(struct smb2_share));
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
			struct smb2_context *smb2;
			struct smb2_url *url;

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
			smb2_share->smb2 = smb2 = smb2_init_context();
			if (smb2 == NULL) {
				smb2_last_error = SMB2_ERR_CONNECT;
				free_smb2_share(smb2_share);
				smb2_share = NULL;
				continue;
			}
			/* fix17: bound every command (socket connect included). */
			smb2_set_timeout(smb2, SMB2_COMMAND_TIMEOUT_SEC);
			smb2_set_user(smb2, smb2_share->user);
			smb2_set_password(smb2, smb2_share->password);
			url = smb2_parse_url(smb2, smb2_share->url);
			if (url == NULL) {
				smb2_last_error = SMB2_ERR_NO_SHARE;
				free_smb2_share(smb2_share);
				smb2_share = NULL;
				continue;
			}
			/* fix18: skip libsmb2 connect when the TCP port is
			 * unreachable, so a SYN-dropping host fails in ~2.5s
			 * instead of hanging the worker / showing a frozen UI. */
			if (!smb2_server_reachable(url->server)) {
				DPRINTF("smb2: %s:445 unreachable (TCP probe), skip connect\n",
				        url->server);
				smb2_destroy_url(url);
				smb2_last_error = SMB2_ERR_CONNECT;
				free_smb2_share(smb2_share);
				smb2_share = NULL;
				continue;
			}
			if (smb2_connect_share(smb2, url->server, url->share,
			                       smb2_share->user) < 0) {
				DPRINTF("failed to connect to : //%s/%s %s\n",
				        url->server, url->share, smb2_get_error(smb2));
				smb2_destroy_url(url);
				smb2_last_error = SMB2_ERR_CONNECT;
				free_smb2_share(smb2_share);
				smb2_share = NULL;
				continue;
			}
			DPRINTF("Mounted share: \\\\%s\\%s as %s\n",
			        url->server, url->share, smb2_share->name);
			smb2_destroy_url(url);

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

	while (smb2_shares) {
		smb2_share = smb2_shares->next;
		free_smb2_share(smb2_shares);
		smb2_shares = smb2_share;
	}
}

/* A CNF file that exists wins the search. */
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

	DPRINTF("Waiting for connection...\n");
	if (ethWaitValidNetIFLinkState() != 0) {
		DPRINTF("Error: failed to get valid link status.\n");
		smb2_last_error = SMB2_ERR_LINK;
		return -1;
	}
	DPRINTF("Network Initialized\n");

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

/* Split "SMB:<name>/<rest>" into share name and relative path. */
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
	int n = 0;
	struct smb2dir *dir = NULL;
	struct smb2_share *share = NULL;
	char *name = NULL, *p;
	struct smb2dirent *ent;

	/* Root of SMB: is the list of configured shares. */
	if (path[4] == '\0' || (path[4] == '/' && path[5] == '\0')) {
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

	find_share(path, &name, &p, &share);
	if (share == NULL) {
		free(name);
		return 0;
	}

	dir = smb2_opendir(share->smb2, (p != NULL && *p != '\0') ? p : "/");
	if (dir == NULL) {
		free(name);
		return 0;
	}
	while ((ent = smb2_readdir(share->smb2, dir))) {
		if (!strcmp(ent->name, ".") || !strcmp(ent->name, ".."))
			continue;
		if (ent->st.smb2_type != SMB2_TYPE_FILE &&
		    ent->st.smb2_type != SMB2_TYPE_DIRECTORY)
			continue;
		if (strlen(ent->name) >= MAX_NAME)
			continue;
		strncpy(info[n].name, ent->name, MAX_NAME);
		info[n].name[MAX_NAME - 1] = 0;
		clear_mcTable(&info[n].stats);
		if (ent->st.smb2_type == SMB2_TYPE_DIRECTORY) {
			info[n].stats.AttrFile = MC_ATTR_norm_folder;
		} else {
			info[n].stats.AttrFile = MC_ATTR_norm_file;
			info[n].stats.FileSizeByte = ent->st.smb2_size;
			info[n].stats.Reserve2 = 0;
		}
		strncpy((char *)info[n].stats.EntryName, info[n].name, 32);
		set_time(&info[n].stats._Create, ent->st.smb2_ctime);
		set_time(&info[n].stats._Modify, ent->st.smb2_mtime);
		n++;
		if (n >= max)
			break;
	}
	size_valid = 1;
	time_valid = 1;

	free(name);
	smb2_closedir(share->smb2, dir);
	return n;
}

int SMB2mkdir(const char *path, int fileMode)
{
	struct smb2_share *share = NULL;
	char *name = NULL, *p = NULL;
	int rc = 0;

	if (path[4] == '\0')
		return -EINVAL;
	find_share(path, &name, &p, &share);
	if (share == NULL) {
		free(name);
		return -EINVAL;
	}
	rc = smb2_mkdir(share->smb2, p);
	free(name);
	return rc;
}

int SMB2rmdir(const char *path)
{
	struct smb2_share *share = NULL;
	char *name = NULL, *p = NULL;
	int rc = 0;

	if (path[4] == '\0')
		return -EINVAL;
	find_share(path, &name, &p, &share);
	if (share == NULL) {
		free(name);
		return -EINVAL;
	}
	if (p[strlen(p) - 1] == '/')
		p[strlen(p) - 1] = 0;
	rc = smb2_rmdir(share->smb2, p);
	free(name);
	return rc;
}

int SMB2unlink(const char *path)
{
	struct smb2_share *share = NULL;
	char *name = NULL, *p = NULL;
	int rc = 0;

	if (path[4] == '\0')
		return -EINVAL;
	find_share(path, &name, &p, &share);
	if (share == NULL) {
		free(name);
		return -EINVAL;
	}
	rc = smb2_unlink(share->smb2, p);
	free(name);
	return rc;
}

struct SMB2FH *SMB2open(const char *path, int mode)
{
	struct smb2_share *share = NULL;
	char *name = NULL, *p = NULL;
	struct SMB2FH *fh;

	if (path[4] == '\0')
		return NULL;
	find_share(path, &name, &p, &share);
	if (share == NULL) {
		free(name);
		return NULL;
	}
	fh = malloc(sizeof(struct SMB2FH));
	if (fh == NULL) {
		free(name);
		return NULL;
	}
	fh->smb2 = share->smb2;
	fh->fh = smb2_open(share->smb2, p, mode);
	free(name);
	if (fh->fh == NULL) {
		free(fh);
		return NULL;
	}
	return fh;
}

int SMB2close(struct SMB2FH *fh)
{
	if (fh == NULL)
		return -1;
	return smb2_close(fh->smb2, fh->fh);
}

int SMB2read(struct SMB2FH *fh, char *buf, int size)
{
	if (fh == NULL)
		return -1;
	return smb2_read(fh->smb2, fh->fh, (uint8_t *)buf, size);
}

int SMB2write(struct SMB2FH *fh, char *buf, int size)
{
	if (fh == NULL)
		return -1;
	return smb2_write(fh->smb2, fh->fh, (uint8_t *)buf, size);
}

int SMB2lseek(struct SMB2FH *fh, int where, int how)
{
	uint64_t co = 0;
	int rc;

	if (fh == NULL)
		return -1;
	rc = smb2_lseek(fh->smb2, fh->fh, where, how, &co);
	if (rc < 0)
		return -1;
	return (int)co;
}

int SMB2stat(const char *path, int *is_dir)
{
	struct smb2_share *share = NULL;
	char *name = NULL, *p = NULL;
	struct smb2dir *dir = NULL;
	struct smb2fh *fh = NULL;
	int rc = -1;

	if (path[4] == '\0' || (path[4] == '/' && path[5] == '\0')) {
		*is_dir = 1;
		return 0;
	}
	find_share(path, &name, &p, &share);
	if (share == NULL) {
		free(name);
		return -1;
	}
	dir = smb2_opendir(share->smb2, (p != NULL && *p != '\0') ? p : "/");
	if (dir != NULL) {
		smb2_closedir(share->smb2, dir);
		*is_dir = 1;
		rc = 0;
	} else {
		fh = smb2_open(share->smb2, (p != NULL && *p != '\0') ? p : "/", 0);
		if (fh != NULL) {
			smb2_close(share->smb2, fh);
			*is_dir = 0;
			rc = 0;
		}
	}
	free(name);
	return rc;
}

int SMB2rename(const char *path, const char *newpath)
{
	struct smb2_share *share = NULL;
	char *name = NULL, *p = NULL;
	int ret;

	if (path[4] == '\0')
		return -EINVAL;
	find_share(path, &name, &p, &share);
	if (share == NULL) {
		free(name);
		return -ENOENT;
	}
	ret = smb2_rename(share->smb2, p, newpath);
	free(name);
	return ret;
}

//---------------------------------------------------------------------------
//End of file: smb2.c
//---------------------------------------------------------------------------
