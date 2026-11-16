//---------------------------------------------------------------------------
// File name:   config_smb.c
//---------------------------------------------------------------------------
// SMB 网络设置界面（自带 SMB 设备配置：显示名称 / 服务器地址 / 用户名 / 密码）
// 配置保存到 wLaunchELF 配置路径（SYS-CONF/SMB.CNF），由 src/smb2.c 的
// loadSMB2CNF() 在启动时读取并挂载共享。
//---------------------------------------------------------------------------
#include "config_private.h"
#ifdef NETFS
#include "netfs_compat.h"
#endif
#include <stdlib.h>
#include <string.h>

#define SMB_NAME_MAX 64
#define SMB_URL_MAX 256
#define SMB_USER_MAX 64
#define SMB_PASS_MAX 64

/* The on-screen keyboard (src/gui.c) caps editable text at 33 characters. */
#define SMB_KB_MAX 33

/* Path most recently written by saveSMBConfig(). Kept in this module so that
 * re-entering the network page in the same session always reads back what was
 * just saved, even when the user picked a non SYS-CONF target. */
static char smb_last_saved_path[MAX_PATH] = "";

#define SMB_CNF_CANDIDATES 8
#define SMB_CNF_PRIMARY    "mc0:/SYS-CONF/SMB.CNF"

/* Build the SMB.CNF search list. The first entries follow the same targets
 * the "Save to" dialog offers (last written / IPCONFIG.DAT directory /
 * CNF_Path override / launch directory); the final three are the fixed
 * SYS-CONF paths that init_smb2() knows about at boot time. */
static int smbConfigCandidatePaths(char paths[SMB_CNF_CANDIDATES][MAX_PATH])
{
	int n = 0;
	char tmp[MAX_PATH];
	char *sl;
	const char *dir;

	if (smb_last_saved_path[0] != '\0')
		snprintf(paths[n++], MAX_PATH, "%s", smb_last_saved_path);

	if (LoadedIPConfigPath[0] != '\0') {
		snprintf(tmp, sizeof(tmp), "%s", LoadedIPConfigPath);
		sl = strrchr(tmp, '/');
		if (sl != NULL) {
			sl[1] = '\0';
			snprintf(paths[n++], MAX_PATH, "%sSMB.CNF", tmp);
		}
	}

	if (setting != NULL && setting->CNF_Path[0] != '\0')
		configAppendPathFile(paths[n++], MAX_PATH, setting->CNF_Path, "SMB.CNF");

	dir = LaunchElfBootDir[0] ? LaunchElfBootDir : LaunchElfDir;
	if (dir[0] != '\0')
		configAppendPathFile(paths[n++], MAX_PATH, dir, "SMB.CNF");

	snprintf(paths[n++], MAX_PATH, "mc0:/SYS-CONF/SMB.CNF");
	snprintf(paths[n++], MAX_PATH, "mc1:/SYS-CONF/SMB.CNF");
	snprintf(paths[n++], MAX_PATH, "mass:/SYS-CONF/SMB.CNF");
	return n;
}

/* Write a copy of the settings to one of the fixed SYS-CONF paths that
 * init_smb2() scans at boot. Called after the user-chosen target is written,
 * so the settings survive a reboot even when "Save to" pointed elsewhere. */
int saveSMBConfigToBootPath(const char *name, const char *url,
                            const char *user, const char *pass, int auto_connect)
{
	static const char *boot_paths[3] = {
	    "mc0:/SYS-CONF/SMB.CNF",
	    "mc1:/SYS-CONF/SMB.CNF",
	    "mass:/SYS-CONF/SMB.CNF"};
	char tmpmsg[MAX_PATH];
	int i;

	for (i = 0; i < 3; i++) {
		if (saveSMBConfig(tmpmsg, boot_paths[i], name, url, user, pass, auto_connect))
			return 1;
	}
	return 0;
}

//---------------------------------------------------------------------------
// 读取已有 SMB.CNF（首个共享）到缓冲区；找不到则留空
//---------------------------------------------------------------------------
void loadSMBConfig(char *name, char *url, char *user, char *pass, int *auto_connect)
{
	char *ram = NULL, *p, *k, *v;
	static char paths[SMB_CNF_CANDIDATES][MAX_PATH];
	int i, n;

	name[0] = '\0';
	url[0] = '\0';
	user[0] = '\0';
	pass[0] = '\0';
	*auto_connect = 0;

	n = smbConfigCandidatePaths(paths);
	for (i = 0; i < n; i++) {
		ram = preloadCNF((char *)paths[i]);
		if (ram)
			break;
	}
	if (!ram)
		return;

	p = ram;
	while (get_CNF_string(&p, &k, &v)) {
		if (!strcmp(k, "NAME") && name[0] == '\0')
			strncpy(name, v, SMB_NAME_MAX - 1);
		else if (!strcmp(k, "URL") && url[0] == '\0')
			strncpy(url, v, SMB_URL_MAX - 1);
		else if (!strcmp(k, "USERNAME") && user[0] == '\0')
			strncpy(user, v, SMB_USER_MAX - 1);
		else if (!strcmp(k, "PASSWORD") && pass[0] == '\0')
			strncpy(pass, v, SMB_PASS_MAX - 1);
		else if (!strcmp(k, "AUTO") && *auto_connect == 0)
			*auto_connect = (strcmp(v, "1") == 0);
	}
	name[SMB_NAME_MAX - 1] = '\0';
	url[SMB_URL_MAX - 1] = '\0';
	user[SMB_USER_MAX - 1] = '\0';
	pass[SMB_PASS_MAX - 1] = '\0';

	/* The previous build wrote "GUEST" as a default account. If no password
	 * was ever set alongside it, that value is a phantom, not a credential:
	 * drop it so upgrading does not keep showing a username nobody entered.
	 * A real GUEST account with a password is left untouched. */
	if (!stricmp(user, "GUEST") && pass[0] == '\0')
		user[0] = '\0';

	free(ram);
}

//---------------------------------------------------------------------------
// 把配置写入目标路径（SMB.CNF）：NAME/USERNAME/PASSWORD/URL
//---------------------------------------------------------------------------
int saveSMBConfig(char *Message, const char *target_path,
                          const char *name, const char *url,
                          const char *user, const char *pass, int auto_connect)
{
	char path[MAX_PATH];
	int fd, ret = 0;
	char line[512];

	sprintf(Message, "%s", LNG(Saved_Failed));
	if (target_path == NULL || target_path[0] == '\0')
		return 0;

	configEnsureSysconfDir(target_path);
	if (genFixPath(target_path, path) < 0)
		return 0;

	fd = genOpen(path, FIO_O_WRONLY | FIO_O_TRUNC | FIO_O_CREAT);
	if (fd < 0)
		return 0;

	{
		sprintf(line, "NAME=%s\r\n", name);
		genWrite(fd, line, strlen(line));
		mcSync(0, NULL, &ret);
		sprintf(line, "USERNAME=%s\r\n", user);
		genWrite(fd, line, strlen(line));
		mcSync(0, NULL, &ret);
		sprintf(line, "PASSWORD=%s\r\n", pass);
		genWrite(fd, line, strlen(line));
		mcSync(0, NULL, &ret);
		/* Always write URL=, even when it is empty. loadSMB2CNF() skips
		 * empty/invalid URLs safely, while loadSMBConfig() needs the line
		 * to exist so re-entering the page never loses the server address. */
		sprintf(line, "URL=%s\r\n", (url != NULL) ? url : "");
		genWrite(fd, line, strlen(line));
		mcSync(0, NULL, &ret);
		sprintf(line, "AUTO=%d\r\n", auto_connect);
		genWrite(fd, line, strlen(line));
		mcSync(0, NULL, &ret);
		genClose(fd);
		snprintf(smb_last_saved_path, sizeof(smb_last_saved_path), "%s", target_path);
		snprintf(Message, MAX_PATH, "%s %.*s", LNG(Saved), MAX_PATH - 4, target_path);
	}
	return 1;
}

//---------------------------------------------------------------------------
// 单行文本输入：直接复用 wLaunchELF 自带的屏幕小键盘（src/gui.c 的 keyboard），
// 而不是逐字符滚动挑选。字段名画在键盘弹框上方，编辑光标停在文本末尾。
//   返回 1 = 确认（接受），0 = 取消（还原）
//---------------------------------------------------------------------------
int smbTextEntry(const char *title, char *buf, int maxlen)
{
	int limit = maxlen - 1;
	int ret;

	if (limit > SMB_KB_MAX)
		limit = SMB_KB_MAX;
	if (limit < 1)
		return 0;
	if ((int)strlen(buf) > limit)
		buf[limit] = '\0';

	clrScr(setting->color[COLOR_BACKGR]);
	printXY((char *)title, Menu_start_x, Menu_start_y,
	        setting->color[COLOR_TEXT], TRUE, 0);
	drawScr();

	ret = keyboardEx(buf, limit, 1);
	return (ret >= 0) ? 1 : 0;
}

//---------------------------------------------------------------------------
enum CONFIG_SMB {
	CONFIG_SMB_FIRST = 1,
	CONFIG_SMB_NAME = CONFIG_SMB_FIRST,
	CONFIG_SMB_URL,
	CONFIG_SMB_USER,
	CONFIG_SMB_PASS,
	CONFIG_SMB_AFT_FIELDS,
	CONFIG_SMB_SAVE = CONFIG_SMB_AFT_FIELDS,
#ifdef NETFS
	CONFIG_SMB_AUTO,
	CONFIG_SMB_RECONNECT,
#endif
	CONFIG_SMB_RETURN,
	CONFIG_SMB_COUNT
};

void Config_SMB(void)
{
	int s;
	int event, post_event;
	int x, y, len;
	char c[MAX_PATH];
	char name[SMB_NAME_MAX], url[SMB_URL_MAX], user[SMB_USER_MAX], pass[SMB_PASS_MAX];
	int auto_connect = 0;
	char msg[MAX_PATH];
	char save_override_path[MAX_PATH];
	char save_cwd_path[MAX_PATH];
	char save_sysconf_path[MAX_PATH];
	int has_override_path;
	int save_target;
	char *save_path;
	char loaded_path[MAX_PATH];
	char masked[64];

	event = 1;  //event = initial entry
	post_event = 0;
	s = CONFIG_SMB_FIRST;

	loadSMBConfig(name, url, user, pass, &auto_connect);
	configBuildSaveTargets(save_override_path, sizeof(save_override_path),
	                       save_cwd_path, sizeof(save_cwd_path),
	                       save_sysconf_path, sizeof(save_sysconf_path),
	                       "SMB.CNF", loaded_path, &has_override_path);

	while (1) {
		waitPadReady(0, 0);
		if (readpad()) {
			if (new_pad & PAD_UP) {
				event |= 2;
				if (s != CONFIG_SMB_FIRST)
					s--;
				else {
					s = CONFIG_SMB_RETURN;
				}
			} else if (new_pad & PAD_DOWN) {
				event |= 2;
				if (s != CONFIG_SMB_COUNT - 1)
					s++;
				else
					s = CONFIG_SMB_FIRST;
			} else if ((!swapKeys && new_pad & PAD_CROSS) || (swapKeys && new_pad & PAD_CIRCLE)) {
				event |= 2;
				if (s < CONFIG_SMB_AFT_FIELDS) {
					char *buf;
					int maxl;
					char tmp[256];
					if (s == CONFIG_SMB_NAME) {
						buf = name;
						maxl = SMB_NAME_MAX;
					} else if (s == CONFIG_SMB_URL) {
						buf = url;
						maxl = SMB_URL_MAX;
					} else if (s == CONFIG_SMB_USER) {
						buf = user;
						maxl = SMB_USER_MAX;
					} else {
						buf = pass;
						maxl = SMB_PASS_MAX;
					}
					strncpy(tmp, buf, maxl - 1);
					tmp[maxl - 1] = '\0';
					if (smbTextEntry((s == CONFIG_SMB_NAME) ? LNG(SMB_Name)
					                 : (s == CONFIG_SMB_URL) ? LNG(SMB_Server)
					                 : (s == CONFIG_SMB_USER) ? LNG(SMB_User)
					                                         : LNG(SMB_Password),
					                 tmp, maxl)) {
						strncpy(buf, tmp, maxl - 1);
						buf[maxl - 1] = '\0';
					}
				} else if (s == CONFIG_SMB_SAVE) {
					save_target = configSaveTargetPrompt(save_override_path, save_cwd_path,
					                                    save_sysconf_path, loaded_path,
					                                    has_override_path);
					if (save_target != CONFIG_SAVE_TARGET_CANCEL) {
						if (save_target == CONFIG_SAVE_TARGET_OVERRIDE)
							save_path = save_override_path;
						else if (save_target == CONFIG_SAVE_TARGET_CWD)
							save_path = save_cwd_path;
						else
							save_path = save_sysconf_path;
						configRefreshSaveTargetForWrite(save_target, save_path, MAX_PATH,
						                               "SMB.CNF", loaded_path);
						saveSMBConfig(msg, save_path, name, url, user, pass, auto_connect);
					}
				}
#ifdef NETFS
					else if (s == CONFIG_SMB_AUTO) {
						auto_connect = auto_connect ? 0 : 1;
					} else if (s == CONFIG_SMB_RECONNECT) {
						/* Background connect: returns at once, no GUI freeze.
						 * Result shows up on the SMB browser status line. */
						netfs_reconnect();
						snprintf(msg, sizeof(msg), "%s", LNG(SMB_Starting));
					}
#endif
					else  //s == CONFIG_SMB_RETURN
					return;
			} else if ((swapKeys && new_pad & PAD_CROSS) || (!swapKeys && new_pad & PAD_CIRCLE)) {
				event |= 2;
				if (s == CONFIG_SMB_RETURN)
					return;
			} else if (new_pad & PAD_TRIANGLE)
				return;
		}

		if (event || post_event) {
			clrScr(setting->color[COLOR_BACKGR]);

			x = Menu_start_x;
			y = Menu_start_y;

			printXY(LNG(SMB_Config), x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			y += FONT_HEIGHT / 2;

			configFormatLabelValue(c, sizeof(c), LNG(SMB_Name),
			                      name[0] ? name : "-");
			printXY(c, x + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			configFormatLabelValue(c, sizeof(c), LNG(SMB_Server),
			                      url[0] ? url : "-");
			printXY(c, x + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			configFormatLabelValue(c, sizeof(c), LNG(SMB_User),
			                      user[0] ? user : "-");
			printXY(c, x + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			// 密码以 * 掩码显示
			len = (int)strlen(pass);
			if (len > (int)sizeof(masked) - 1)
				len = (int)sizeof(masked) - 1;
			{
				int k;
				for (k = 0; k < len; k++)
					masked[k] = '*';
				masked[len] = '\0';
			}
			configFormatLabelValue(c, sizeof(c), LNG(SMB_Password),
			                      pass[0] ? masked : "-");
			printXY(c, x + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			y += FONT_HEIGHT / 2;

			snprintf(masked, sizeof(masked), "...");
			configFormatLabelValue(c, sizeof(c), LNG(Save_to), masked);
			printXY(c, x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

#ifdef NETFS
			configFormatLabelValue(c, sizeof(c), LNG(SMB_AutoConnect), auto_connect ? LNG(SMB_On) : LNG(SMB_Off));
			printXY(c, x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			configFormatLabelValue(c, sizeof(c), LNG(SMB_Reconnect), ">");
			printXY(c, x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;
#endif

			sprintf(c, "  %s", LNG(RETURN));
			printXY(c, x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			//Cursor positioning section
			y = Menu_start_y + s * FONT_HEIGHT + FONT_HEIGHT / 2;
			if (s >= CONFIG_SMB_AFT_FIELDS)
				y += FONT_HEIGHT / 2;
			if (s >= CONFIG_SMB_RETURN)
				y += FONT_HEIGHT / 2;
			drawChar(LEFT_CUR, x, y, setting->color[COLOR_TEXT]);

			//Tooltip section
			if (s < CONFIG_SMB_AFT_FIELDS) {
				len = sprintf(c, "%s", LNG(Right_DPad_to_Edit));
			} else if (s == CONFIG_SMB_SAVE) {
				if (swapKeys)
					len = sprintf(c, "\xFF"
					                 "1:%s",
					              LNG(Select));
				else
					len = sprintf(c, "\xFF"
					                 "0:%s",
					              LNG(Select));
			} else {
				if (swapKeys)
					len = sprintf(c, "\xFF"
					                 "1:%s",
					              LNG(OK));
				else
					len = sprintf(c, "\xFF"
					                 "0:%s",
					              LNG(OK));
			}
			sprintf(&c[len], " \xFF"
			                 "3:%s",
			        LNG(Return));
			setScrTmp(msg, c);
		}
		drawScr();
		post_event = event;
		event = 0;
	}
}
//---------------------------------------------------------------------------

#ifdef NETFS
//---------------------------------------------------------------------------
// Reads SMB.CNF (first match among mc0:/mc1:/mass:) and returns 1 if it
// carries AUTO=1, meaning the user wants SMB mounted automatically at
// startup. Used by main.c before the main menu loop.
//---------------------------------------------------------------------------
int netfs_autostart_enabled(void)
{
	char *ram = NULL, *p, *k, *v;
	static char paths[SMB_CNF_CANDIDATES][MAX_PATH];
	int i, n;

	/* Use the same candidate list as the settings page, otherwise AUTO=1
	 * is invisible when SMB.CNF was saved outside SYS-CONF. */
	n = smbConfigCandidatePaths(paths);
	for (i = 0; i < n; i++) {
		ram = preloadCNF(paths[i]);
		if (ram)
			break;
	}
	if (!ram)
		return 0;
	p = ram;
	while (get_CNF_string(&p, &k, &v)) {
		if (!strcmp(k, "AUTO") && !strcmp(v, "1")) {
			free(ram);
			return 1;
		}
	}
	free(ram);
	return 0;
}
#endif
//---------------------------------------------------------------------------
// End of file: config_smb.c
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
