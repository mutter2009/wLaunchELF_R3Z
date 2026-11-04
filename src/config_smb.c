//---------------------------------------------------------------------------
// File name:   config_smb.c
//---------------------------------------------------------------------------
// SMB 网络设置界面（自带 SMB 设备配置：显示名称 / 服务器地址 / 用户名 / 密码）
// 配置保存到 wLaunchELF 配置路径（SYS-CONF/SMB.CNF），由 src/smb2.c 的
// loadSMB2CNF() 在启动时读取并挂载共享。
//---------------------------------------------------------------------------
#include "config_private.h"
#include <stdlib.h>
#include <string.h>

#define SMB_NAME_MAX 64
#define SMB_URL_MAX 256
#define SMB_USER_MAX 64
#define SMB_PASS_MAX 64

// 字符输入面板可用字符集（小写/大写/数字/常用符号）
static const char SMB_CHARSET[] =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789"
    " .-_:/@";
#define SMB_CHARSET_LEN (sizeof(SMB_CHARSET) - 1)

//---------------------------------------------------------------------------
// 读取已有 SMB.CNF（首个共享）到缓冲区；找不到则留空
//---------------------------------------------------------------------------
static void loadSMBConfig(char *name, char *url, char *user, char *pass)
{
	char *ram, *p, *k, *v;
	const char *paths[3] = {
	    "mc0:/SYS-CONF/SMB.CNF",
	    "mc1:/SYS-CONF/SMB.CNF",
	    "mass:/SYS-CONF/SMB.CNF"};
	int i;

	name[0] = '\0';
	url[0] = '\0';
	user[0] = '\0';
	pass[0] = '\0';

	for (i = 0; i < 3; i++) {
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
	}
	name[SMB_NAME_MAX - 1] = '\0';
	url[SMB_URL_MAX - 1] = '\0';
	user[SMB_USER_MAX - 1] = '\0';
	pass[SMB_PASS_MAX - 1] = '\0';
	free(ram);
}

//---------------------------------------------------------------------------
// 把配置写入目标路径（SMB.CNF）：NAME/USERNAME/PASSWORD/URL
//---------------------------------------------------------------------------
static void saveSMBConfig(char *Message, const char *target_path,
                          const char *name, const char *url,
                          const char *user, const char *pass)
{
	char path[MAX_PATH];
	int fd, ret = 0;
	char line[512];

	sprintf(Message, "%s", LNG(Saved_Failed));
	if (target_path == NULL || target_path[0] == '\0')
		return;

	configEnsureSysconfDir(target_path);
	if (genFixPath(target_path, path) < 0)
		return;

	fd = genOpen(path, FIO_O_WRONLY | FIO_O_TRUNC | FIO_O_CREAT);
	if (fd >= 0) {
		sprintf(line, "NAME=%s\r\n", name);
		genWrite(fd, line, strlen(line));
		mcSync(0, NULL, &ret);
		sprintf(line, "USERNAME=%s\r\n", user);
		genWrite(fd, line, strlen(line));
		mcSync(0, NULL, &ret);
		sprintf(line, "PASSWORD=%s\r\n", pass);
		genWrite(fd, line, strlen(line));
		mcSync(0, NULL, &ret);
		sprintf(line, "URL=%s\r\n", url);
		genWrite(fd, line, strlen(line));
		mcSync(0, NULL, &ret);
		genClose(fd);
		snprintf(Message, MAX_PATH, "%s %.*s", LNG(Saved), MAX_PATH - 4, target_path);
	}
}

//---------------------------------------------------------------------------
// 单行文本输入子界面：用字符面板“挑选”字符组合成字符串
//   返回 1 = 确认（接受），0 = 取消（还原）
//   X:添加字符   □:删除上一个字符   O:确认   △:取消
//---------------------------------------------------------------------------
static int smbTextEntry(const char *title, char *buf, int maxlen)
{
	int cur = 0;       // 字符面板光标
	int len = (int)strlen(buf);
	int event = 1, post_event = 0;
	int x, y, i;
	char c[MAX_PATH];
	int win, wend;
	int selx;

	while (1) {
		waitPadReady(0, 0);
		if (readpad()) {
			if (new_pad & PAD_LEFT) {
				event |= 2;
				if (cur > 0)
					cur--;
			} else if (new_pad & PAD_RIGHT) {
				event |= 2;
				if (cur < SMB_CHARSET_LEN - 1)
					cur++;
			} else if (new_pad & PAD_UP) {
				event |= 2;
				cur -= 10;
				if (cur < 0)
					cur = 0;
			} else if (new_pad & PAD_DOWN) {
				event |= 2;
				cur += 10;
				if (cur > SMB_CHARSET_LEN - 1)
					cur = SMB_CHARSET_LEN - 1;
			} else if (new_pad & PAD_CROSS) {
				event |= 2;
				if (len < maxlen - 1) {
					buf[len++] = SMB_CHARSET[cur];
					buf[len] = '\0';
				}
			} else if (new_pad & PAD_SQUARE) {
				event |= 2;
				if (len > 0) {
					len--;
					buf[len] = '\0';
				}
			} else if (new_pad & PAD_CIRCLE) {
				event |= 2;
				return 1;  // 确认
			} else if (new_pad & PAD_TRIANGLE) {
				event |= 2;
				return 0;  // 取消
			}
		}

		if (event || post_event) {
			clrScr(setting->color[COLOR_BACKGR]);
			x = Menu_start_x;
			y = Menu_start_y;

			printXY((char *)title, x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;
			y += FONT_HEIGHT / 2;

			// 当前已输入文本
			sprintf(c, "> %s", buf);
			printXY(c, x + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;
			y += FONT_HEIGHT / 2;

			// 字符面板（带窗口滚动与高亮）
			win = cur - 12;
			if (win < 0)
				win = 0;
			wend = win + 24;
			if (wend > SMB_CHARSET_LEN) {
				wend = SMB_CHARSET_LEN;
				win = wend - 24;
				if (win < 0)
					win = 0;
			}
			{
				char winstr[32];
				int wi = 0;
				for (i = win; i < wend && wi < (int)sizeof(winstr) - 1; i++)
					winstr[wi++] = SMB_CHARSET[i];
				winstr[wi] = '\0';
				printXY(winstr, x + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
				selx = x + 2 * FONT_WIDTH + (cur - win) * FONT_WIDTH;
				drawChar((unsigned int)SMB_CHARSET[cur], selx, y, setting->color[COLOR_SELECT]);
			}
			y += FONT_HEIGHT;
			y += FONT_HEIGHT / 2;

			sprintf(c, "X:%s  SQ:%s  O:%s  Tri:%s",
			        LNG(Add), LNG(Subtract), LNG(OK), LNG(Cancel));
			printXY(c, x, y, setting->color[COLOR_TEXT], TRUE, 0);
		}
		drawScr();
		post_event = event;
		event = 0;
	}
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

	loadSMBConfig(name, url, user, pass);
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
						saveSMBConfig(msg, save_path, name, url, user, pass);
					}
				} else  //s == CONFIG_SMB_RETURN
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
// End of file: config_smb.c
//---------------------------------------------------------------------------
