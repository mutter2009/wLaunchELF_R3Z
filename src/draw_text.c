//--------------------------------------------------------------
//File name:   draw_text.c
//--------------------------------------------------------------
#include "draw_private.h"

//The font file ELISA100.FNT is needed to display MC save titles in japanese
//and the arrays defined here are needed to find correct data in that file
const u16 font404[] = {
    0xA2AF, 11,
    0xA2C2, 8,
    0xA2D1, 11,
    0xA2EB, 7,
    0xA2FA, 4,
    0xA3A1, 15,
    0xA3BA, 7,
    0xA3DB, 6,
    0xA3FB, 4,
    0xA4F4, 11,
    0xA5F7, 8,
    0xA6B9, 8,
    0xA6D9, 38,
    0xA7C2, 15,
    0xA7F2, 13,
    0xA8C1, 720,
    0xCFD4, 43,
    0xF4A5, 1030,
    0, 0};

// ASCII��SJIS�̕ϊ��p�z��
static const u8 sjis_lookup_81[256] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x00
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x10
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x20
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x30
    ' ', ',', '.', ',', '.', 0xFF, ':', ';', '?', '!', 0xFF, 0xFF, '\xff', '`', 0xFF, '^',              // 0x40
    0xFF, '_', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, '0', 0xFF, '-', '-', 0xFF, 0xFF,      // 0x50
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, '\'', '\'', '"', '"', '(', ')', 0xFF, 0xFF, '[', ']', '{',         // 0x60
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, '+', '-', 0xFF, '*', 0xFF,     // 0x70
    '/', '=', 0xFF, '<', '>', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, '\xff', 0xFF, 0xFF, '\xff', 0xFF,        // 0x80
    '$', 0xFF, 0xFF, '%', '#', '&', '*', '@', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,        // 0x90
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xA0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xB0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, '&', '|', '!', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,     // 0xC0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xD0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xE0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xF0
};
static const u8 sjis_lookup_82[256] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x00
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x10
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x20
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x30
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, '0',   // 0x40
    '1', '2', '3', '4', '5', '6', '7', '8', '9', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,           // 0x50
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',                  // 0x60
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,            // 0x70
    0xFF, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',                 // 0x80
    'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             // 0x90
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xA0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xB0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xC0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xD0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xE0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0xF0
};
int loadFont(char *path_arg)
{
	int fd;

	if (strlen(path_arg) != 0) {
		char FntPath[MAX_PATH];
		genFixPath(path_arg, FntPath);
		fd = genOpen(FntPath, FIO_O_RDONLY);
		if (fd < 0) {
			genClose(fd);
			goto use_default;
		}  // end if failed open file
		genLseek(fd, 0, SEEK_SET);
		if (genLseek(fd, 0, SEEK_END) > 4700) {
			genClose(fd);
			goto use_default;
		}
		genLseek(fd, 0, SEEK_SET);
		u8 FontHeader[100];
		genRead(fd, FontHeader, 100);
		if ((FontHeader[0] == 0x00) &&
		    (FontHeader[1] == 0x02) &&
		    (FontHeader[70] == 0x60) &&
		    (FontHeader[72] == 0x60) &&
		    (FontHeader[83] == 0x90)) {
			genLseek(fd, 1018, SEEK_SET);
			memset(FontBuffer, 0, 32 * 16);
			genRead(fd, FontBuffer + 32 * 16, 224 * 16);  //.fnt files skip 1st 32 chars
			genClose(fd);
			return 1;
		} else {  // end if good fnt file
			genClose(fd);
			goto use_default;
		}     // end else bad fnt file
	} else {  // end if external font file
	use_default:
		memcpy(FontBuffer, &font_uLE, 4096);
	}  // end else build-in font
	return 0;
}
void drawChar(unsigned int c, int x, int y, u64 colour)
{
	int i, j, pixBase, pixMask;
	u8 *cm;

	updateScr_1 = 1;

	if (c >= FONT_COUNT)
		c = '_';
	if (c > 0xFF)                  //if char is beyond normal ascii range
		cm = &font_uLE[c * 16];    //  cm points to special char def in default font
	else                           //else char is inside normal ascii range
		cm = &FontBuffer[c * 16];  //  cm points to normal char def in active font

	pixMask = 0x80;
	for (i = 0; i < 8; i++) {  //for i == each pixel column
		pixBase = -1;
		for (j = 0; j < 16; j++) {                     //for j == each pixel row
			if ((pixBase < 0) && (cm[j] & pixMask)) {  //if start of sequence
				pixBase = j;
			} else if ((pixBase > -1) && !(cm[j] & pixMask)) {  //if end of sequence
				gsKit_prim_sprite(gsGlobal, x + i, y + pixBase - 1, x + i + 1, y + j - 1, 1, colour);
				pixBase = -1;
			}
		}                  //ends for j == each pixel row
		if (pixBase > -1)  //if end of sequence including final row
			gsKit_prim_sprite(gsGlobal, x + i, y + pixBase - 1, x + i + 1, y + j - 1, 1, colour);
		pixMask >>= 1;
	}  //ends for i == each pixel column
}
void drawChar2(int n, int x, int y, u64 colour)
{
	unsigned int i, j;
	u8 b;

	updateScr_1 = 1;

	// ELISA100.FNT glyphs are 8x8; upscale to 16x16 so each Japanese character
	// occupies the same width as two ASCII cells (16px), matching CJK layout.
	for (i = 0; i < 8; i++) {
		b = elisaFnt[n + i];
		for (j = 0; j < 8; j++) {
			if (b & 0x80) {
				gsKit_prim_sprite(gsGlobal, x + j * 2, y + i * 2 - 2, x + j * 2 + 2, y + i * 2, 1, colour);
			}
			b = b << 1;
		}
	}
}
//---------------------------------------------------------------------------
// 简体中文 (UTF-8) 支持：使用 font_cn.c 提供的点阵字库（当前 12x16）
//---------------------------------------------------------------------------
extern unsigned short cn_unicode[];
extern unsigned char  font_cn[];
extern int             cn_glyph_count;
extern int             cn_glyph_width;   // 字模宽度（当前 12，ASCII 8px 的 1.5 倍）
extern int             cn_glyph_height;  // 字模高度（16）
extern int             g_useUTF8;        // 1 = 当前语言为中文(UTF-8)，由 lang.c 设置

// 在 cn_unicode[] (升序排列) 中二分查找码点，返回字模下标；未找到返回 -1
static int cn_glyph_index(unsigned int cp)
{
    int lo = 0, hi = cn_glyph_count - 1;
    if (cp > 0xFFFF)
        return -1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        unsigned short v = cn_unicode[mid];
        if (cp == v)
            return mid;
        else if (cp < v)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return -1;
}

// 绘制一个 CJK 字符 (idx 为 font_cn 中的字模下标；宽度/高度由 font_cn.c 提供)
void drawCharCN(int idx, int x, int y, u64 colour)
{
    int i, j;
    int bytes_per_row = 2;                        // 每行固定 2 字节（16 位），实际使用 cn_glyph_width 列
    int bytes_per_glyph = cn_glyph_height * bytes_per_row;
    const u8 *cm = &font_cn[idx * bytes_per_glyph];

    updateScr_1 = 1;
    for (i = 0; i < cn_glyph_height; i++) {
        u16 row = ((u16)cm[i * bytes_per_row] << 8) | cm[i * bytes_per_row + 1];  // MSB 在最左
        for (j = 0; j < cn_glyph_width; j++) {
            if (row & (0x8000 >> j))
                gsKit_prim_sprite(gsGlobal, x + j, y + i, x + j + 1, y + i + 1, 1, colour);
        }
    }
}

// Shift-JIS (cp932) -> Unicode 查找表，由 font_cn.c 生成 (索引 = leadIdx*188 + secIdx)
extern const unsigned short g_sjis_unicode[];

// 解码一个 Shift-JIS 双字节字符：返回 Unicode 码点，*nbytes=2；非法序列返回 0xFFFD
static unsigned int decode_sjis(const unsigned char *s, int *nbytes)
{
    unsigned char lead = s[0];
    if (lead < 0x81 || (lead > 0x9F && lead < 0xE0) || lead > 0xFC) {
        *nbytes = 1;
        return 0xFFFD;
    }
    unsigned char second = s[1];
    if (second < 0x40 || second == 0x7F || second > 0xFC) {
        *nbytes = 1;
        return 0xFFFD;
    }
    int li = (lead >= 0xE0) ? (lead - 0xE0 + 31) : (lead - 0x81);
    int si = (second >= 0x80) ? (second - 0x80 + 63) : (second - 0x40);
    unsigned int cp = g_sjis_unicode[li * 188 + si];
    *nbytes = 2;
    return (cp == 0xFFFF) ? 0xFFFD : cp;
}

// 自动识别 UTF-8 或 Shift-JIS 并解码一个字符：返回码点，*nbytes 返回占用字节数。
// 策略：先尝试按 UTF-8 解析；若字节落入 Shift-JIS 首字节范围且不构成合法 UTF-8，
//       则按 SJIS 解析。这样菜单(UTF-8 中文)与日文/繁体中文文件名(SJIS)都能正确显示。
static unsigned int decode_any(const unsigned char *s, int *nbytes)
{
    unsigned char c = s[0];
    if (c < 0x80) {
        *nbytes = 1;
        return c;
    }
    // --- 尝试 UTF-8 ---
    if (c >= 0xC2 && c <= 0xDF) {                  // 2 字节
        if ((s[1] & 0xC0) == 0x80) {
            *nbytes = 2;
            return ((c & 0x1F) << 6) | (s[1] & 0x3F);
        }
    } else if (c >= 0xE0 && c <= 0xEF) {           // 3 字节
        if ((s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
            *nbytes = 3;
            return ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        }
    } else if (c >= 0xF0 && c <= 0xF4) {           // 4 字节
        if ((s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
            *nbytes = 4;
            return ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
                   ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        }
    }
    // --- 否则按 Shift-JIS 解析 (覆盖 0xE0-0xEF 但次字节非 UTF-8 续字节的情形) ---
    return decode_sjis(s, nbytes);
}

// 计算字符串的显示像素宽度（ASCII=spacing，CJK/SJIS=spacing * cn_glyph_width / 8），用于自动缩放/截断
static int text_display_width(const char *s, int spacing)
{
    int w = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int nb;
        decode_any(p, &nb);
        if (nb <= 1)
            w += spacing;
        else
            w += (cn_glyph_width * spacing) / 8;
        p += nb;
    }
    return w;
}
//---------------------------------------------------------------------------

int printXY(const char *s, int x, int y, u64 colour, int draw, int space)
{
	unsigned int c1, c2;
	int i;
	int text_spacing = 8;

	if (space > 0) {
		while (text_display_width(s, text_spacing) > space)
			if (--text_spacing <= 5)
				break;
	} else {
		while (text_display_width(s, text_spacing) > SCREEN_WIDTH - SCREEN_MARGIN - FONT_WIDTH * 2)
			if (--text_spacing <= 5)
				break;
	}

	i = 0;
	while ((c1 = (unsigned char)s[i++]) != 0) {
		if (c1 != 0xFF) {  // Normal character (含 UTF-8 中文)
			if (g_useUTF8 && c1 >= 0x80) {
				// UTF-8 或 Shift-JIS 多字节序列 -> 从 font_cn 取汉字
				int nb;
				unsigned int cp = decode_any((const unsigned char *)s + i - 1, &nb);
				int idx = cn_glyph_index(cp);
				if (draw) {
					if (idx >= 0)
						drawCharCN(idx, x, y, colour);
					else
						drawChar('_', x, y, colour);
				}
				i += nb - 1;          // decode_any 已含首字节，补上剩余续字节
				x += (cn_glyph_width * text_spacing) / 8;
			} else {                  // ASCII / 单字节
				if (draw)
					drawChar(c1, x, y, colour);
				x += text_spacing;
			}
			if (x > SCREEN_WIDTH - SCREEN_MARGIN - FONT_WIDTH)
				break;
			continue;
		}  //End if for normal character
		// Here we got a sequence starting with 0xFF ('\xff')
		if ((c2 = (unsigned char)s[i++]) == 0)
			break;
		if ((c2 < '0') || (c2 > '='))
			continue;
		c1 = (c2 - '0') * 2 + 0x100;
		if (draw) {
			//expand sequence �0=Circle  �1=Cross  �2=Square  �3=Triangle  �4=FilledBox
			//"\xff:"=Pad_Right  "\xff;"=Pad_Down  "\xff<"=Pad_Left  "\xff="=Pad_Up
			drawChar(c1, x, y, colour);
			x += 8;
			if (x > SCREEN_WIDTH - SCREEN_MARGIN - FONT_WIDTH)
				break;
			drawChar(c1 + 1, x, y, colour);
			x += 8;
			if (x > SCREEN_WIDTH - SCREEN_MARGIN - FONT_WIDTH)
				break;
		}
	}  // ends while(1)
	return x;
}
int printXY_sjis(const unsigned char *s, int x, int y, u64 colour, int draw)
{
	int n;
	u8 ascii;
	u16 code;
	int i, j, tmp;

	i = 0;
	while (s[i]) {
		if ((s[i] & 0x80) && s[i + 1]) {  //we have top bit and some more char ?
			// SJIS
			code = s[i++];
			code = (code << 8) + s[i++];

			switch (code) {
				// Circle == "��"
				case 0x819B:
					if (draw) {
						drawChar(0x100, x, y, colour);
						drawChar(0x101, x + 8, y, colour);
					}
					x += 16;
					break;
				// Cross == "\xff~"
				case 0x817E:
					if (draw) {
						drawChar(0x102, x, y, colour);
						drawChar(0x103, x + 8, y, colour);
					}
					x += 16;
					break;
				// Square == "��"
				case 0x81A0:
					if (draw) {
						drawChar(0x104, x, y, colour);
						drawChar(0x105, x + 8, y, colour);
					}
					x += 16;
					break;
				// Triangle == "��"
				case 0x81A2:
					if (draw) {
						drawChar(0x106, x, y, colour);
						drawChar(0x107, x + 8, y, colour);
					}
					x += 16;
					break;
				// FilledBox == "��"
				case 0x81A1:
					if (draw) {
						drawChar(0x108, x, y, colour);
						drawChar(0x109, x + 8, y, colour);
					}
					x += 16;
					break;
				default:
					if (elisaFnt != NULL) {  // elisa font is available ?
						tmp = y;
						if (code <= 0x829A)
							tmp++;
						// SJIS����EUC�ɕϊ�
						if (code >= 0xE000)
							code -= 0x4000;
						code = ((((code >> 8) & 0xFF) - 0x81) << 9) + (code & 0x00FF);
						if ((code & 0x00FF) >= 0x80)
							code--;
						if ((code & 0x00FF) >= 0x9E)
							code += 0x62;
						else
							code -= 0x40;
						code += 0x2121 + 0x8080;

						// EUC����b�����t�H���g�̔ԍ��𐶐�
						n = (((code >> 8) & 0xFF) - 0xA1) * (0xFF - 0xA1) + (code & 0xFF) - 0xA1;
						j = 0;
						while (font404[j]) {
							if (code >= font404[j]) {
								if (code <= font404[j] + font404[j + 1] - 1) {
									n = -1;
									break;
								} else {
									n -= font404[j + 1];
								}
							}
							j += 2;
						}
						n *= 8;

					if (n >= 0 && n <= 55008) {
						if (draw)
							drawChar2(n, x, tmp, colour);
						x += 16;  // wide glyph: same width as two ASCII cells
					} else {
							if (draw)
								drawChar('_', x, y, colour);
							x += 8;
						}
					} else {  //elisa font is not available
						ascii = 0xFF;
						if (code >> 8 == 0x81)
							ascii = sjis_lookup_81[code & 0x00FF];
						else if (code >> 8 == 0x82)
							ascii = sjis_lookup_82[code & 0x00FF];
						if (ascii != 0xFF) {
							if (draw)
								drawChar((char)ascii, x, y, colour);
						} else {
							if (draw)
								drawChar('_', x, y, colour);
						}
						x += 8;
					}
					break;
			}
		} else {  //First char does not have top bit set or no following char
			if (draw)
				drawChar(s[i], x, y, colour);
			i++;
			x += 8;
		}
		if (x > SCREEN_WIDTH - SCREEN_MARGIN - FONT_WIDTH) {
			//x=16; y=y+8;
			return x;
		}
	}
	return x;
}
char *transcpy_sjis(char *d, const unsigned char *s)
{
	u8 ascii;
	u16 code1, code2;
	int i, j;

	for (i = 0, j = 0; s[i];) {
		code1 = s[i++];
		if ((code1 & 0x80) && s[i]) {  //we have top bit and some more char (SJIS) ?
			// SJIS
			code2 = s[i++];
			ascii = 0xFF;
			if (code1 == 0x81)
				ascii = sjis_lookup_81[code2];
			else if (code1 == 0x82)
				ascii = sjis_lookup_82[code2];
			if (ascii != 0xFF) {
				d[j++] = (char)ascii;
			} else {
				d[j++] = '_';
			}
		} else {  //First char lacks top bit set or no following char (non-SJIS)
			d[j++] = (char)code1;
		}
	}             //ends for
	d[j] = '\0';  //terminate result string
	return d;
}
//--------------------------------------------------------------
//End of file: draw_text.c
//--------------------------------------------------------------
