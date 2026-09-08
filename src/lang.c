//---------------------------------------------------------------------------
//File name:    lang.c (Modified for Default Chinese and English Only)
//---------------------------------------------------------------------------
#include "launchelf.h"

// 当前语言是否为中文(UTF-8)。为 1 时 draw_text.c 的 printXY 会按 UTF-8 解码中文。
// 由本文件在各语言切换/加载处维护，draw_text.c 中以 extern 引用。
// 默认即为 1：即便 Init_Default_Language/Load_External_Language 在某些启动阶段
// 尚未设置本标志，也保证中文 UTF-8 能正常渲染，避免被当成 Latin-1 逐字节乱码。
int g_useUTF8 = 1;

// 将默认语言（Lang_Default）直接指向中文语言包
Language Lang_Default[] = {
#define lang(id, name, value) {value},
#include "../Lang/CHS.LNG"
#undef lang
    {NULL}};

// 英文语言包作为备选
static Language Lang_English[] = {
#define lang(id, name, value) {value},
#include "../Lang/ENG.LNG"
#undef lang
    {NULL}};

Language Lang_String[sizeof(Lang_Default) / sizeof(Lang_Default[0])];
Language Lang_Extern[sizeof(Lang_Default) / sizeof(Lang_Default[0])];

void *External_Lang_Buffer = NULL;

// 缩减为只保留中文（默认）和英文两个选项
#define BUILTIN_LANGUAGE_COUNT 2

static const char *builtin_language_config_names[BUILTIN_LANGUAGE_COUNT] = {
    "chinese",
    "english",
};

static const char *builtin_language_native_names[BUILTIN_LANGUAGE_COUNT] = {
    "简体中文",
    "English",
};

int normalizeBuiltinLanguage(int language)
{
	while (language < 0)
		language += BUILTIN_LANGUAGE_COUNT;
	while (language >= BUILTIN_LANGUAGE_COUNT)
		language -= BUILTIN_LANGUAGE_COUNT;
	return language;
}

static Language *getBuiltinLanguageTable(int language)
{
	switch (normalizeBuiltinLanguage(language)) {
		case 1:
			return Lang_English;
		case 0:
		default:
			return Lang_Default; // 默认返回中文
	}
}

const char *getBuiltinLanguageConfigName(int language)
{
	return builtin_language_config_names[normalizeBuiltinLanguage(language)];
}

const char *getBuiltinLanguageNativeName(int language)
{
	return builtin_language_native_names[normalizeBuiltinLanguage(language)];
}

int getBuiltinLanguageByConfigName(const char *name)
{
	if (name == NULL || name[0] == '\0')
		return -1;
	if (!stricmp(name, "0") || !stricmp(name, "chinese") || !stricmp(name, "chs") || !stricmp(name, "zh") || !stricmp(name, "简体中文"))
		return 0; // Chinese (Default)
	if (!stricmp(name, "1") || !stricmp(name, "english") || !stricmp(name, "eng") || !stricmp(name, "en"))
		return 1; // English
	return -1;
}

static void releaseExternalLanguageBuffer(void)
{
	if (External_Lang_Buffer != NULL) {
		free(External_Lang_Buffer);
		External_Lang_Buffer = NULL;
	}
}

//---------------------------------------------------------------------------
// get_LANG_string is the main parser called for each language dependent
//---------------------------------------------------------------------------
int get_LANG_string(char **LANG_p_p, char **id_p_p, char **value_p_p)
{
	char *cp, *ip, *vp, *tp = *LANG_p_p;
	int ret, length;

	ip = NULL;
	vp = NULL;
	ret = -1;

start_line:
	while (*tp <= ' ' && *tp > '\0')
		tp += 1;
	if (*tp == '\0')
		goto exit;
	if (tp[0] == '/' && tp[1] == '/') {
		while (*tp != '\r' && *tp != '\n' && *tp > '\0')
			tp += 1;
		goto start_line;
	}
	ret = -2;
	if (strncmp(tp, "lang", 4))
		goto exit;
	tp += 4;
	ret = -3;
	while (*tp <= ' ' && *tp != '\r' && *tp != '\n' && *tp > '\0')
		tp += 1;
	if (*tp == '\0')
		goto exit;
	ret = -4;
	if (*tp != '(')
		goto exit;
	tp += 1;
	ret = -5;
	while (*tp <= ' ' && *tp != '\r' && *tp != '\n' && *tp > '\0')
		tp += 1;
	if (*tp == '\0')
		goto exit;
	ret = -6;
	if (*tp < '0' || *tp > '9')
		goto exit;
	ip = tp;
	while (*tp >= '0' && *tp <= '9')
		tp += 1;
	ret = -7;
	while (*tp <= ' ' && *tp != '\r' && *tp != '\n' && *tp > '\0')
		tp += 1;
	if (*tp == '\0')
		goto exit;
	ret = -8;
	if (*tp != ',')
		goto exit;
	tp += 1;
	ret = -9;
	while (*tp <= ' ' && *tp != '\r' && *tp != '\n' && *tp > '\0')
		tp += 1;
	if (*tp == '\0')
		goto exit;
	ret = -10;
	while (*tp != ',' && *tp != '\r' && *tp != '\n' && *tp > '\0')
		tp += 1;
	if (*tp != ',')
		goto exit;
	tp += 1;
	ret = -11;
	while (*tp <= ' ' && *tp != '\r' && *tp != '\n' && *tp > '\0')
		tp += 1;
	if (*tp == '\0')
		goto exit;
	ret = -12;
	if (*tp != '\"')
		goto exit;
	tp += 1;
	ret = -13;
	vp = tp;
close_quote:
	while (*tp != '\"' && *tp != '\r' && *tp != '\n' && *tp > '\0')
		tp += 1;
	if (*tp != '\"')
		return -13;
	cp = tp - 1;
	tp += 1;
	if (*cp == '\\')
		goto close_quote;
	length = (tp - 1) - vp;
	ret = -14;
	while (*tp <= ' ' && *tp != '\r' && *tp != '\n' && *tp > '\0')
		tp += 1;
	if (*tp == '\0')
		goto exit;
	ret = -15;
	if (*tp != ')')
		goto exit;
	tp += 1;
	ret = -16;
	while (*tp <= ' ' && *tp != '\r' && *tp != '\n' && *tp > '\0')
		tp += 1;
	if (*tp == '\0')
		goto exit;
	if (tp[0] != '/' || tp[1] != '/')
		goto finish_line;
	ret = -17;
	while (*tp != '\r' && *tp != '\n' && *tp > '\0')
		tp += 1;
	if (*tp == '\0')
		goto exit;
finish_line:
	ret = -18;
	if (*tp != '\r' && *tp != '\n')
		goto exit;
	if (tp[0] == '\r' && tp[1] == '\n')
		tp += 1;
	tp += 1;
	ret = length;

exit:
	*LANG_p_p = tp;
	*id_p_p = ip;
	*value_p_p = vp;
	return ret;
}

static int copy_LANG_value(char *dst, const char *src, int len)
{
	int si;
	int di = 0;

	for (si = 0; si < len; si++) {
		if (src[si] == '\\' && si + 1 < len) {
			switch (src[si + 1]) {
				case 'n':
					dst[di++] = '\n';
					si++;
					continue;
				case 'r':
					dst[di++] = '\r';
					si++;
					continue;
				case 't':
					dst[di++] = '\t';
					si++;
					continue;
				case '"':
					dst[di++] = '"';
					si++;
					continue;
				case '\\':
					dst[di++] = '\\';
					si++;
					continue;
				default:
					break;
			}
		}
		dst[di++] = src[si];
	}

	dst[di] = '\0';
	return di;
}

static int isMiscLaunchNameAlias(const char *name, const char *configured_path, const char *default_name)
{
	return !strcmp(name, configured_path + strlen(setting->Misc)) || !strcmp(name, default_name);
}

static void updateLocalizedMiscPaths(void)
{
	int i;
	char *tmp;
	char default_misc[64];
	size_t default_misc_len;

	if (setting == NULL)
		return;

	sprintf(default_misc, "%s/", LNG_DEF(MISC));
	default_misc_len = strlen(default_misc);

	if (strlen(setting->Misc) > 0) {
		for (i = 0; i < 16; i++) {
			if ((i < 12) || (setting->LK_Flag[i] != 0)) {
				if (!strncmp(setting->LK_Path[i], setting->Misc, strlen(setting->Misc)) ||
				    !strncmp(setting->LK_Path[i], default_misc, default_misc_len)) {
					tmp = strrchr(setting->LK_Path[i], '/');
					if (tmp == NULL)
						continue;
					if (isMiscLaunchNameAlias(tmp + 1, setting->Misc_PS2Disc, LNG_DEF(PS2Disc)))
						sprintf(setting->LK_Path[i], "%s/%s", LNG(MISC), LNG(PS2Disc));
					else if (isMiscLaunchNameAlias(tmp + 1, setting->Misc_FileBrowser, LNG_DEF(FileBrowser)))
						sprintf(setting->LK_Path[i], "%s/%s", LNG(MISC), LNG(FileBrowser));
					else if (isMiscLaunchNameAlias(tmp + 1, setting->Misc_PS2Browser, LNG_DEF(PS2Browser)))
						sprintf(setting->LK_Path[i], "%s/%s", LNG(MISC), LNG(PS2Browser));
					else if (isMiscLaunchNameAlias(tmp + 1, setting->Misc_PS2Net, LNG_DEF(PS2Net)))
						sprintf(setting->LK_Path[i], "%s/%s", LNG(MISC), LNG(PS2Net));
					else if (isMiscLaunchNameAlias(tmp + 1, setting->Misc_PS2PowerOff, LNG_DEF(PS2PowerOff)))
						sprintf(setting->LK_Path[i], "%s/%s", LNG(MISC), LNG(PS2PowerOff));
					else if (isMiscLaunchNameAlias(tmp + 1, setting->Misc_HddManager, LNG_DEF(HddManager)))
						sprintf(setting->LK_Path[i], "%s/%s", LNG(MISC), LNG(HddManager));
					else if (isMiscLaunchNameAlias(tmp + 1, setting->Misc_TextEditor, LNG_DEF(TextEditor)))
						sprintf(setting->LK_Path[i], "%s/%s", LNG(MISC), LNG(TextEditor));
					else if (isMiscLaunchNameAlias(tmp + 1, setting->Misc_Configure, LNG_DEF(Configure)))
						sprintf(setting->LK_Path[i], "%s/%s", LNG(MISC), LNG(Configure));
					else if (isMiscLaunchNameAlias(tmp + 1, setting->Misc_ShowFont, LNG_DEF(ShowFont)))
						sprintf(setting->LK_Path[i], "%s/%s", LNG(MISC), LNG(ShowFont));
					else if (isMiscLaunchNameAlias(tmp + 1, setting->Misc_Debug_Info, LNG_DEF(Debug_Info)))
						sprintf(setting->LK_Path[i], "%s/%s", LNG(MISC), LNG(Debug_Info));
					else if (isMiscLaunchNameAlias(tmp + 1, setting->Misc_About_uLE, LNG_DEF(About_uLE)))
						sprintf(setting->LK_Path[i], "%s/%s", LNG(MISC), LNG(About_uLE));
					else if (isMiscLaunchNameAlias(tmp + 1, setting->Misc_Show_Build_Info, LNG_DEF(Build_Info)))
						sprintf(setting->LK_Path[i], "%s/%s", LNG(MISC), LNG(Build_Info));
					else if (isMiscLaunchNameAlias(tmp + 1, setting->Misc_OSDSYS, LNG_DEF(OSDSYS)))
						sprintf(setting->LK_Path[i], "%s/%s", LNG(MISC), LNG(OSDSYS));
					else if (isMiscLaunchNameAlias(tmp + 1, setting->Misc_Exploit_Installer, LNG_DEF(Exploit_Installer)) ||
					         !strcmp(tmp + 1, "Exploit Installer"))
						sprintf(setting->LK_Path[i], "%s/%s", LNG(MISC), LNG(Exploit_Installer));
					else if (isMiscLaunchNameAlias(tmp + 1, setting->Misc_Reboot_IOP, LNG_DEF(Reboot_IOP)))
						sprintf(setting->LK_Path[i], "%s/%s", LNG(MISC), LNG(Reboot_IOP));
				}
			}
		}
	}

	sprintf(setting->Misc, "%s/", LNG(MISC));
	sprintf(setting->Misc_PS2Disc, "%s/%s", LNG(MISC), LNG(PS2Disc));
	sprintf(setting->Misc_FileBrowser, "%s/%s", LNG(MISC), LNG(FileBrowser));
	sprintf(setting->Misc_PS2Browser, "%s/%s", LNG(MISC), LNG(PS2Browser));
	sprintf(setting->Misc_PS2Net, "%s/%s", LNG(MISC), LNG(PS2Net));
	sprintf(setting->Misc_PS2PowerOff, "%s/%s", LNG(MISC), LNG(PS2PowerOff));
	sprintf(setting->Misc_HddManager, "%s/%s", LNG(MISC), LNG(HddManager));
	sprintf(setting->Misc_TextEditor, "%s/%s", LNG(MISC), LNG(TextEditor));
	sprintf(setting->Misc_Configure, "%s/%s", LNG(MISC), LNG(Configure));
	sprintf(setting->Misc_ShowFont, "%s/%s", LNG(MISC), LNG(ShowFont));
	sprintf(setting->Misc_Debug_Info, "%s/%s", LNG(MISC), LNG(Debug_Info));
	sprintf(setting->Misc_About_uLE, "%s/%s", LNG(MISC), LNG(About_uLE));
	sprintf(setting->Misc_Show_Build_Info, "%s/%s", LNG(MISC), LNG(Build_Info));
	sprintf(setting->Misc_OSDSYS, "%s/%s", LNG(MISC), LNG(OSDSYS));
	sprintf(setting->Misc_Exploit_Installer, "%s/%s", LNG(MISC), LNG(Exploit_Installer));
	sprintf(setting->Misc_Reboot_IOP, "%s/%s", LNG(MISC), LNG(Reboot_IOP));
}

void Init_Default_Language(void)
{
	memcpy(Lang_String, Lang_Default, sizeof(Lang_String));
	g_useUTF8 = 1;   // 默认语言为中文
}

void Set_Language(int language)
{
	releaseExternalLanguageBuffer();
	if (setting != NULL)
		setting->language = normalizeBuiltinLanguage(language);
	memcpy(Lang_String, getBuiltinLanguageTable(language), sizeof(Lang_String));
	g_useUTF8 = (normalizeBuiltinLanguage(language) == 0) ? 1 : 0;  // 0=中文
	updateLocalizedMiscPaths();
}

void Load_External_Language(void)
{
	int error_id = -1;
	int test = 0;
	u32 index = 0;
	char filePath[MAX_PATH];
	char *file_bp, *file_tp, *lang_bp, *lang_tp, *oldf_tp = NULL;
	char *id_p, *value_p;
	int lang_size = 0;
	int fd;
	Language *Lang;

	releaseExternalLanguageBuffer();

	Lang = getBuiltinLanguageTable(setting != NULL ? setting->language : 0);
	memcpy(Lang_String, Lang, sizeof(Lang_String));

	if (setting != NULL && strlen(setting->lang_file) != 0) {
		error_id = -2;
		genFixPath(setting->lang_file, filePath);
		fd = genOpen(filePath, FIO_O_RDONLY);
		if (fd >= 0) {
			int file_size = genLseek(fd, 0, SEEK_END);

			error_id = -3;
			if (file_size > 0) {
				error_id = -4;
				file_bp = malloc(file_size + 1);
				if (file_bp == NULL)
					goto aborted_1;

				error_id = -5;
				genLseek(fd, 0, SEEK_SET);
				if (genRead(fd, file_bp, file_size) != file_size)
					goto release_1;
				file_bp[file_size] = '\0';

				error_id = -6;
				file_tp = file_bp;
				while (1) {
					oldf_tp = file_tp;
					test = get_LANG_string(&file_tp, &id_p, &value_p);
					if (test == -1)
						break;
					if (test < 0)
						goto release_1;
					index = atoi(id_p);
					if (index >= LANG_COUNT)
						goto release_1;
					lang_size += test + 1;
				}

				error_id = -7;
				lang_bp = malloc(lang_size + 1);
				if (lang_bp == NULL)
					goto release_1;

				memcpy(Lang_Extern, Lang, sizeof(Lang_Extern));

				file_tp = file_bp;
				lang_tp = lang_bp;
				while ((test = get_LANG_string(&file_tp, &id_p, &value_p)) >= 0) {
					int decoded_len;

					index = atoi(id_p);
					Lang_Extern[index].String = lang_tp;
					decoded_len = copy_LANG_value(lang_tp, value_p, test);
					lang_tp += decoded_len + 1;
				}
				External_Lang_Buffer = lang_bp;
				Lang = Lang_Extern;
				error_id = 0;
			release_1:
				free(file_bp);
			}
		aborted_1:
			genClose(fd);
		}
	}

	if (error_id < -1) {
		char tmp_s[4096], t1_s[102], t2_s[102];
		int pos = 0, stp = 0;
		sprintf(tmp_s,
		        "LNG loading failed with error_id==%d and test==%d\n"
		        "The latest string index (possibly invalid) was %d\n"
		        "%n",
		        error_id, test, index, &stp);
		pos += stp;
		if (error_id == -2) {
			sprintf(tmp_s + pos,
			        "This was a failure to open the file:\n"
			        "\"%s\"\n",
			        filePath);
		}
		if (error_id == -6) {
			strncpy(t1_s, oldf_tp, 100);
			t1_s[100] = '\0';
			strncpy(t2_s, file_tp, 100);
			t2_s[100] = '\0';
			sprintf(tmp_s + pos,
			        "This was a parsing error when trying to parse the text:\n"
			        "\"%s\"\n"
			        "That attempt failed somehow, after reaching this point:\n"
			        "\"%s\"\n",
			        t1_s, t2_s);
		}
		strcat(tmp_s, "Use either OK or CANCEL to continue (no diff)");
		ynDialog(tmp_s);
	}

	memcpy(Lang_String, Lang, sizeof(Lang_String));
	// 关键修复：即使 setting 尚未分配(NULL)，也应按"当前语言(默认中文=0)"判断，
	// 不能因为 setting==NULL 就把 g_useUTF8 清零，否则中文会被当成 Latin-1 逐字节乱码。
	g_useUTF8 = (normalizeBuiltinLanguage(setting != NULL ? setting->language : 0) == 0) ? 1 : 0;
	updateLocalizedMiscPaths();
}
//---------------------------------------------------------------------------
//End of file:  lang.c
//---------------------------------------------------------------------------
