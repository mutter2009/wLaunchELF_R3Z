//---------------------------------------------------------------------------
// File name:   config_network.c
//---------------------------------------------------------------------------
#include "config_private.h"
#include <stdlib.h>
#ifdef NETFS
#include "netfs_compat.h"
#endif

//---------------------------------------------------------------------------
// Network settings GUI by Slam-Tilt
//---------------------------------------------------------------------------
static void saveNetworkSettings(char *Message, const char *target_path)
{
	char firstline[50];
	int out_fd, in_fd;
	int ret = 0, i = 0;
	int size, sizeleft = 0;
	char *ipconfigfile = 0;
	char path[MAX_PATH];

	// Default message, will get updated if save is sucessfull
	sprintf(Message, "%s", LNG(Saved_Failed));

	sprintf(firstline, "%s %s %s\n\r", ip, netmask, gw);

	if (target_path == NULL || target_path[0] == '\0')
		return;



	// Preserve any existing data beyond the first line of the selected target.

	if (genFixPath(target_path, path) >= 0)
		in_fd = genOpen(path, FIO_O_RDONLY);
	else
		in_fd = -1;

	if (in_fd >= 0) {

		size = genLseek(in_fd, 0, SEEK_END);
		DPRINTF("%s: size of existing file is %ibytes\n\r", __func__, size);

		if (size > 0 && (ipconfigfile = (char *)memalign(64, size)) != NULL) {
			int read_size;

			genLseek(in_fd, 0, SEEK_SET);
			read_size = genRead(in_fd, ipconfigfile, size);
			if (read_size > 0) {
				size = read_size;
				for (i = 0; i < size && ipconfigfile[i] != '\r' && ipconfigfile[i] != '\n'; i++)
					;
				while (i < size && (ipconfigfile[i] == '\r' || ipconfigfile[i] == '\n'))
					i++;
				sizeleft = size - i;
			}
		}

		genClose(in_fd);
	}

	// Writing the data out

	configEnsureSysconfDir(target_path);
	if (genFixPath(target_path, path) < 0) {
		if (ipconfigfile != NULL)
			free(ipconfigfile);
		return;
	}
	out_fd = genOpen(path, FIO_O_WRONLY | FIO_O_TRUNC | FIO_O_CREAT);
	if (out_fd >= 0) {
		mcSync(0, NULL, &ret);
		genWrite(out_fd, firstline, strlen(firstline));
		mcSync(0, NULL, &ret);

		// If we have any extra data, spit that out too.
		if (sizeleft > 0) {
			mcSync(0, NULL, &ret);
			genWrite(out_fd, &ipconfigfile[i], sizeleft);
			mcSync(0, NULL, &ret);
		}

		snprintf(Message, MAX_PATH, "%s %.*s", LNG(Saved), MAX_PATH - 4, target_path);
		snprintf(LoadedIPConfigPath, sizeof(LoadedIPConfigPath), "%s", target_path);

		genClose(out_fd);
	}
	if (ipconfigfile != NULL)
		free(ipconfigfile);
}
//---------------------------------------------------------------------------
// Convert IP string to numbers
//---------------------------------------------------------------------------
static void ipStringToOctet(char *ip, int ip_octet[4])
{

	// This takes a string (ip) representing an IP address and converts it
	// into an array of ints (ip_octet)
	// Rewritten 22/10/05

	char oct_str[5];
	int oct_cnt, i, oct_len;

	oct_cnt = 0;
	oct_len = 0;
	oct_str[0] = '\0';

	for (i = 0; ((i <= strlen(ip)) && (oct_cnt < 4)); i++) {
		if ((ip[i] == '.') || (i == strlen(ip))) {
			ip_octet[oct_cnt] = atoi(oct_str);
			oct_cnt++;
			oct_len = 0;
			oct_str[0] = '\0';
		} else if (oct_len < (int)sizeof(oct_str) - 1) {
			oct_str[oct_len++] = ip[i];
			oct_str[oct_len] = '\0';
		}
	}
}
//---------------------------------------------------------------------------
static data_ip_struct BuildOctets(char *ip, char *nm, char *gw)
{

	// Populate 3 arrays with the ip address (as ints)

	data_ip_struct iplist;

	ipStringToOctet(ip, iplist.ip);
	ipStringToOctet(nm, iplist.nm);
	ipStringToOctet(gw, iplist.gw);

	return (iplist);
}
//---------------------------------------------------------------------------
enum CONFIG_NET {
	CONFIG_NET_FIRST = 1,
	CONFIG_NET_IP = CONFIG_NET_FIRST,
	CONFIG_NET_NM,
	CONFIG_NET_GW,
#ifdef NETFS
	CONFIG_NET_SMB_HOST,
	CONFIG_NET_SMB_PORT,
	CONFIG_NET_SMB_SHARE,
	CONFIG_NET_SMB_USER,
	CONFIG_NET_SMB_PASS,
	CONFIG_NET_SMB_AUTO,
	CONFIG_NET_SMB_RECONNECT,
#endif
	CONFIG_NET_AFT_IP,
	CONFIG_NET_SAVE = CONFIG_NET_AFT_IP,
	CONFIG_NET_RETURN,

	CONFIG_NET_COUNT
};

#ifdef NETFS
#define NET_SMB_HOST_MAX  64
#define NET_SMB_PORT_MAX  8
#define NET_SMB_SHARE_MAX 96
#define NET_SMB_USER_MAX  64
#define NET_SMB_PASS_MAX  64
#define NET_SMB_URL_MAX   256

/* Split a libsmb2 URL (smb://[user@]host[:port]/share[?opts]) into the
 * three fields shown on the network page. This mirrors the classic anyi
 * Chinese build, which kept server / port / share as separate settings. */
static void netSplitSmbUrl(const char *url, char *host, size_t host_sz,
                           char *port, size_t port_sz, char *share, size_t share_sz)
{
	const char *p = (url != NULL) ? url : "";
	const char *slash;
	char authority[128];
	char *colon, *at;
	size_t n;

	host[0] = '\0';
	port[0] = '\0';
	share[0] = '\0';

	if (!strncmp(p, "smb://", 6))
		p += 6;
	else if (!strncmp(p, "smb2://", 7))
		p += 7;

	slash = strchr(p, '/');
	n = slash ? (size_t)(slash - p) : strlen(p);
	if (n > sizeof(authority) - 1)
		n = sizeof(authority) - 1;
	memcpy(authority, p, n);
	authority[n] = '\0';

	at = strrchr(authority, '@');
	if (at != NULL)
		memmove(authority, at + 1, strlen(at + 1) + 1);

	colon = strrchr(authority, ':');
	if (colon != NULL) {
		*colon = '\0';
		snprintf(port, port_sz, "%s", colon + 1);
	}
	snprintf(host, host_sz, "%s", authority);

	if (slash != NULL) {
		const char *s = slash + 1;
		const char *q = strchr(s, '?');
		size_t sn = q ? (size_t)(q - s) : strlen(s);
		if (sn > share_sz - 1)
			sn = share_sz - 1;
		memcpy(share, s, sn);
		share[sn] = '\0';
	}
	if (port[0] == '\0')
		snprintf(port, port_sz, "445");
}

static void netBuildSmbUrl(char *url, size_t url_sz, const char *host,
                           const char *port, const char *share)
{
	int p = atoi(port);

	if (host[0] == '\0' || share[0] == '\0') {
		url[0] = '\0';
		return;
	}
	if (p > 0 && p != 445)
		snprintf(url, url_sz, "smb://%s:%d/%s", host, p, share);
	else
		snprintf(url, url_sz, "smb://%s/%s", host, share);
}
#endif

void Config_Network(void)
{
	int s, l;
	int x, y;
	int event, post_event = 0;
	int len;
	char c[MAX_PATH];
	char value[MAX_PATH];
	data_ip_struct ipdata;
	char NetMsg[MAX_PATH] = "";
	char save_override_path[MAX_PATH];
	char save_cwd_path[MAX_PATH];
	char save_sysconf_path[MAX_PATH];
	int has_override_path;
	int save_target;
	char *save_path;
#ifdef NETFS
	char smb_name[NET_SMB_HOST_MAX];
	char smb_url[NET_SMB_URL_MAX];
	char smb_user[NET_SMB_USER_MAX];
	char smb_pass[NET_SMB_PASS_MAX];
	char smb_host[NET_SMB_HOST_MAX];
	char smb_port[NET_SMB_PORT_MAX];
	char smb_share[NET_SMB_SHARE_MAX];
	char smb_save_path[MAX_PATH];
	char smb_masked[NET_SMB_PASS_MAX];
	int smb_auto = 0;
#endif

	event = 1;
	s = CONFIG_NET_FIRST;
	l = 1;
	ipdata = BuildOctets(ip, netmask, gw);
	configBuildSaveTargets(save_override_path, sizeof(save_override_path),
	                       save_cwd_path, sizeof(save_cwd_path),
	                       save_sysconf_path, sizeof(save_sysconf_path),
	                       "IPCONFIG.DAT", LoadedIPConfigPath, &has_override_path);
#ifdef NETFS
	loadSMBConfig(smb_name, smb_url, smb_user, smb_pass, &smb_auto);
	netSplitSmbUrl(smb_url, smb_host, sizeof(smb_host),
	               smb_port, sizeof(smb_port), smb_share, sizeof(smb_share));
#endif

	while (1) {
		waitPadReady(0, 0);
		if (readpad()) {
			if (new_pad & PAD_UP) {
				event |= 2;
				if (s != CONFIG_NET_FIRST)
					s--;
				else
					s = CONFIG_NET_RETURN;
				l = 1;
			} else if (new_pad & PAD_DOWN) {
				event |= 2;
				if (s != CONFIG_NET_COUNT - 1)
					s++;
				else
					s = CONFIG_NET_FIRST;
				l = 1;
			} else if (new_pad & PAD_LEFT) {
				event |= 2;
				if (s <= CONFIG_NET_GW && l > 1)
					l--;
			} else if (new_pad & PAD_RIGHT) {
				event |= 2;
				if (s <= CONFIG_NET_GW && l < 5)
					l++;
			} else if ((!swapKeys && new_pad & PAD_CROSS) || (swapKeys && new_pad & PAD_CIRCLE)) {
				event |= 2;
				if (s <= CONFIG_NET_GW && l > 1) {
					if (s == CONFIG_NET_IP) {
						if (ipdata.ip[l - 2] > 0)
							ipdata.ip[l - 2]--;
					} else if (s == CONFIG_NET_NM) {
						if (ipdata.nm[l - 2] > 0)
							ipdata.nm[l - 2]--;
					} else if (s == CONFIG_NET_GW) {
						if (ipdata.gw[l - 2] > 0)
							ipdata.gw[l - 2]--;
					}
				}
			} else if ((swapKeys && new_pad & PAD_CROSS) || (!swapKeys && new_pad & PAD_CIRCLE)) {
				event |= 2;
				if (s <= CONFIG_NET_GW && l > 1) {
					if (s == CONFIG_NET_IP) {
						if (ipdata.ip[l - 2] < 255)
							ipdata.ip[l - 2]++;
					} else if (s == CONFIG_NET_NM) {
						if (ipdata.nm[l - 2] < 255)
							ipdata.nm[l - 2]++;
					} else if (s == CONFIG_NET_GW) {
						if (ipdata.gw[l - 2] < 255)
							ipdata.gw[l - 2]++;
					}
#ifdef NETFS
				} else if (s >= CONFIG_NET_SMB_HOST && s <= CONFIG_NET_SMB_PASS) {
					char *buf = NULL;
					int maxl = 0;
					const char *title = NULL;
					char tmp[256];

					if (s == CONFIG_NET_SMB_HOST)       { buf = smb_host;  maxl = sizeof(smb_host);  title = LNG(SMB_Server_IP); }
					else if (s == CONFIG_NET_SMB_PORT)  { buf = smb_port;  maxl = sizeof(smb_port);  title = LNG(SMB_Port); }
					else if (s == CONFIG_NET_SMB_SHARE) { buf = smb_share; maxl = sizeof(smb_share); title = LNG(SMB_Share); }
					else if (s == CONFIG_NET_SMB_USER)  { buf = smb_user;  maxl = sizeof(smb_user);  title = LNG(SMB_User); }
					else                                { buf = smb_pass;  maxl = sizeof(smb_pass);  title = LNG(SMB_Password); }

					strncpy(tmp, buf, maxl - 1);
					tmp[maxl - 1] = '\0';
					if (smbTextEntry(title, tmp, maxl)) {
						strncpy(buf, tmp, maxl - 1);
						buf[maxl - 1] = '\0';
					}
				} else if (s == CONFIG_NET_SMB_AUTO) {
					smb_auto = smb_auto ? 0 : 1;
				} else if (s == CONFIG_NET_SMB_RECONNECT) {
					int rc = netfs_reconnect();
					snprintf(NetMsg, sizeof(NetMsg), "%s",
					         rc == 0 ? LNG(SMB_Connected) : LNG(SMB_ConnectFailed));
#endif
				} else if (s == CONFIG_NET_SAVE) {
					save_target = configSaveTargetPrompt(save_override_path, save_cwd_path, save_sysconf_path, LoadedIPConfigPath, has_override_path);
					if (save_target != CONFIG_SAVE_TARGET_CANCEL) {
						sprintf(ip, "%i.%i.%i.%i", ipdata.ip[0], ipdata.ip[1], ipdata.ip[2], ipdata.ip[3]);
						sprintf(netmask, "%i.%i.%i.%i", ipdata.nm[0], ipdata.nm[1], ipdata.nm[2], ipdata.nm[3]);
						sprintf(gw, "%i.%i.%i.%i", ipdata.gw[0], ipdata.gw[1], ipdata.gw[2], ipdata.gw[3]);

						if (save_target == CONFIG_SAVE_TARGET_OVERRIDE)
							save_path = save_override_path;
						else if (save_target == CONFIG_SAVE_TARGET_CWD)
							save_path = save_cwd_path;
						else
							save_path = save_sysconf_path;
						configRefreshSaveTargetForWrite(save_target, save_path, MAX_PATH, "IPCONFIG.DAT", LoadedIPConfigPath);
						saveNetworkSettings(NetMsg, save_path);
#ifdef NETFS
						/* SMB.CNF lives next to the chosen IPCONFIG.DAT. */
						snprintf(smb_save_path, sizeof(smb_save_path), "%s", save_path);
						{
							char *sl = strrchr(smb_save_path, '/');
							if (sl != NULL)
								strcpy(sl + 1, "SMB.CNF");
						}
						netBuildSmbUrl(smb_url, sizeof(smb_url), smb_host, smb_port, smb_share);
						if (smb_name[0] == '\0')
							snprintf(smb_name, sizeof(smb_name), "%s",
							         smb_share[0] ? smb_share : "SMB");
						saveSMBConfig(NetMsg, smb_save_path, smb_name, smb_url,
						              smb_user, smb_pass, smb_auto);
#endif
					}
				} else
					return;
			} else if (new_pad & PAD_TRIANGLE)
				return;
		}

		if (event || post_event) {
			clrScr(setting->color[COLOR_BACKGR]);

			x = Menu_start_x;
			y = Menu_start_y;

			printXY(LNG(NETWORK_SETTINGS), x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;
			y += FONT_HEIGHT / 2;

			len = (strlen(LNG(IP_Address)) + 5 > strlen(LNG(Netmask)) + 5) ?
			          strlen(LNG(IP_Address)) + 5 : strlen(LNG(Netmask)) + 5;
			len = (len > strlen(LNG(Gateway)) + 5) ? len : strlen(LNG(Gateway)) + 5;
			sprintf(c, "%s:", LNG(IP_Address));
			printXY(c, x + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			sprintf(c, "%.3i . %.3i . %.3i . %.3i", ipdata.ip[0], ipdata.ip[1], ipdata.ip[2], ipdata.ip[3]);
			printXY(c, x + len * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			sprintf(c, "%s:", LNG(Netmask));
			printXY(c, x + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			sprintf(c, "%.3i . %.3i . %.3i . %.3i", ipdata.nm[0], ipdata.nm[1], ipdata.nm[2], ipdata.nm[3]);
			printXY(c, x + len * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			sprintf(c, "%s:", LNG(Gateway));
			printXY(c, x + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			sprintf(c, "%.3i . %.3i . %.3i . %.3i", ipdata.gw[0], ipdata.gw[1], ipdata.gw[2], ipdata.gw[3]);
			printXY(c, x + len * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

#ifdef NETFS
			configFormatLabelValue(c, sizeof(c), LNG(SMB_Server_IP), smb_host[0] ? smb_host : "-");
			printXY(c, x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			configFormatLabelValue(c, sizeof(c), LNG(SMB_Port), smb_port[0] ? smb_port : "445");
			printXY(c, x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			configFormatLabelValue(c, sizeof(c), LNG(SMB_Share), smb_share[0] ? smb_share : "-");
			printXY(c, x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			configFormatLabelValue(c, sizeof(c), LNG(SMB_User), smb_user[0] ? smb_user : "-");
			printXY(c, x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			{
				int k, plen = (int)strlen(smb_pass);
				if (plen > (int)sizeof(smb_masked) - 1)
					plen = (int)sizeof(smb_masked) - 1;
				for (k = 0; k < plen; k++)
					smb_masked[k] = '*';
				smb_masked[plen] = '\0';
			}
			configFormatLabelValue(c, sizeof(c), LNG(SMB_Password), smb_pass[0] ? smb_masked : "-");
			printXY(c, x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			configFormatLabelValue(c, sizeof(c), LNG(SMB_AutoConnect), smb_auto ? LNG(SMB_On) : LNG(SMB_Off));
			printXY(c, x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			configFormatLabelValue(c, sizeof(c), LNG(SMB_Reconnect), ">");
			printXY(c, x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;
#endif
			y += FONT_HEIGHT / 2;

			snprintf(value, sizeof(value), "...");
			configFormatLabelValue(c, sizeof(c), LNG(Save_to), value);
			printXY(c, x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;
			y += FONT_HEIGHT / 2;
			sprintf(c, "  %s", LNG(RETURN));
			printXY(c, x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			y = Menu_start_y + s * FONT_HEIGHT + FONT_HEIGHT / 2;
			if (s >= CONFIG_NET_AFT_IP)
				y += FONT_HEIGHT / 2;
			if (s >= CONFIG_NET_RETURN)
				y += FONT_HEIGHT / 2;
			if (l > 1)
				x += (len - 1) * FONT_WIDTH - 1 + (l - 2) * 6 * FONT_WIDTH;
			drawChar(LEFT_CUR, x, y, setting->color[COLOR_TEXT]);

			if (s <= CONFIG_NET_GW && l == 1) {
				len = sprintf(c, "%s", LNG(Right_DPad_to_Edit));
			} else if (s <= CONFIG_NET_GW) {
				if (swapKeys)
					len = sprintf(c, "\xFF" "1:%s \xFF" "0:%s", LNG(Add), LNG(Subtract));
				else
					len = sprintf(c, "\xFF" "0:%s \xFF" "1:%s", LNG(Add), LNG(Subtract));
			} else if (s == CONFIG_NET_SAVE) {
				if (swapKeys)
					len = sprintf(c, "\xFF" "1:%s", LNG(Select));
				else
					len = sprintf(c, "\xFF" "0:%s", LNG(Select));
			} else {
				if (swapKeys)
					len = sprintf(c, "\xFF" "1:%s", LNG(OK));
				else
					len = sprintf(c, "\xFF" "0:%s", LNG(OK));
			}
			sprintf(&c[len], " \xFF" "3:%s", LNG(Return));
			setScrTmp(NetMsg, c);
		}
		drawScr();
		post_event = event;
		event = 0;
	}
}
//---------------------------------------------------------------------------
// End of file: config_network.c
//---------------------------------------------------------------------------
