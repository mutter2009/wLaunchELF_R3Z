#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 wLaunchELF 中文/日文点阵字库 font_cn.c。

覆盖范围 (取并集, 按 Unicode 码点排序去重):
  1. CHS.LNG 菜单里实际出现的所有非 ASCII 字符 (简体专用字 + 全角标点)
  2. GB2312 全部汉字 (约 6763, 常用简体中文基线, 覆盖绝大多数简体文件名)
  3. Shift-JIS (cp932) 能解码出的所有字符: 日文汉字 (JIS X 0208) + 平假名 + 片假名
     + 全角符号 + NEC/IBM 扩展汉字。用于显示 PS2 存档/记忆卡里日文编码的
     繁体中文/日文文件名。
  4. 全角 ASCII (U+FF01..U+FF5E) 与全角符号 (U+FFE0..U+FFE6)

同时生成 g_sjis_unicode[] 查找表 (供 draw_text.c 在运行期把 Shift-JIS 双字节
直接映射到 Unicode 码点, 再复用同一套字模)。

字模格式 (与 font_uLE.c 兼容的"宽"版本):
  每个汉字 16 行 x 16 列 = 32 字节。
  第 r 行: byte0 = 左 8 列 (bit15 最左), byte1 = 右 8 列 (bit7 最右)。
"""
import re
import sys
from PIL import Image, ImageFont, ImageDraw

LNG_PATH = "/root/uploads/1788841088778538921-CHS.LNG"
FONT_CANDIDATES = [
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc",
]
GLYPH_W = 16
GLYPH_H = 16
GLYPH_RENDER_W = 32  # 在大画布上渲染，裁剪空白后再缩放，避免丢笔画
THRESHOLD = 128

# Shift-JIS 查找表的维度 (lead 60 种, second 188 种)
SJIS_LEADS = list(range(0x81, 0xA0)) + list(range(0xE0, 0xFD))   # 31 + 29 = 60


def lead_idx(lead):
    if 0x81 <= lead <= 0x9F:
        return lead - 0x81
    if 0xE0 <= lead <= 0xFC:
        return (lead - 0xE0) + 31
    return -1


def sec_idx(sec):
    if 0x40 <= sec <= 0x7E:
        return sec - 0x40
    if 0x80 <= sec <= 0xFC:
        return (sec - 0x80) + 63
    return -1


def collect_chars(lng_path):
    """CHS.LNG 中实际出现的非 ASCII 字符 (简体专用 + 全角标点)"""
    s = open(lng_path, encoding="utf-8").read()
    strs = re.findall(r'lang\(\d+,\s*\w+,\s*"([^"]*)"\)', s)
    chars = set()
    for t in strs:
        for ch in t:
            if ord(ch) > 0x7F:
                chars.add(ch)
    return chars


def collect_gb2312():
    """GB2312 全部可编码字符 (常用简体中文基线)"""
    out = set()
    for b1 in range(0xA1, 0xFE + 1):
        for b2 in range(0xA1, 0xFE + 1):
            try:
                ch = bytes([b1, b2]).decode("gb2312")
            except Exception:
                continue
            if ch and len(ch) == 1:
                out.add(ch)
    return out


def collect_sjis():
    """Shift-JIS (cp932) 能解码的全部字符: 日文汉字 + 假名 + 全角符号 + 扩展汉字"""
    out = set()
    for lead in SJIS_LEADS:
        for sec in list(range(0x40, 0x7F)) + list(range(0x80, 0xFD)):
            try:
                ch = bytes([lead, sec]).decode("cp932")
            except Exception:
                continue
            if ch and len(ch) == 1:
                out.add(ch)
    return out


def collect_fullwidth():
    out = set()
    for cp in list(range(0xFF01, 0xFF5E + 1)) + list(range(0xFFE0, 0xFFE6 + 1)):
        out.add(chr(cp))
    return out


def load_font(path):
    last_err = None
    # Noto Sans CJK TTC 子字体顺序通常为 0=JP 1=KR 2=SC 3=TC 4=HK
    for idx in (2, 0, 1, 3, 4):
        try:
            f = ImageFont.truetype(path, GLYPH_H, index=idx)
            img = Image.new("L", (GLYPH_W, GLYPH_H), 0)
            d = ImageDraw.Draw(img)
            d.text((0, 0), "网", font=f, fill=255)
            if img.getextrema()[1] > 0:
                return f
        except Exception as e:  # noqa
            last_err = e
    raise RuntimeError(f"无法从 {path} 加载中文字体: {last_err}")


def render_glyph(font, ch):
    # 在大画布上渲染完整字形，裁剪掉空白边框，
    # 再等比缩放(保持比例)填满 GLYPH_W x GLYPH_H 并居中。
    # 这样字形完整、不变形，且几乎无黑边(不再像 12x16 那样上下留白)。
    img_raw = Image.new("L", (GLYPH_RENDER_W, GLYPH_RENDER_W), 0)
    d = ImageDraw.Draw(img_raw)
    d.text((0, 0), ch, font=font, fill=255)

    bbox = img_raw.getbbox()
    if bbox:
        crop = img_raw.crop(bbox)
    else:
        crop = img_raw
    crop_w, crop_h = crop.size
    if crop_w <= 0 or crop_h <= 0:
        return [0] * (GLYPH_W * GLYPH_H)

    # 垂直方向拉伸填满(去除上下黑边)，水平方向保持比例居中。
    # 轻微的高拉伸在 16x16 点阵上不明显，但能确保字顶天立地、不被当成黑边。
    scale_y = float(GLYPH_H) / crop_h
    new_h = GLYPH_H
    new_w = max(1, int(round(crop_w * scale_y)))
    if new_w > GLYPH_W:
        # 若按垂直填满后宽度会超，则改为水平填满、垂直居中
        scale_x = float(GLYPH_W) / crop_w
        new_w = GLYPH_W
        new_h = max(1, int(round(crop_h * scale_x)))
        crop = crop.resize((new_w, new_h), Image.LANCZOS)
        img = Image.new("L", (GLYPH_W, GLYPH_H), 0)
        paste_x = 0
        paste_y = (GLYPH_H - new_h) // 2
    else:
        crop = crop.resize((new_w, new_h), Image.LANCZOS)
        img = Image.new("L", (GLYPH_W, GLYPH_H), 0)
        paste_x = (GLYPH_W - new_w) // 2
        paste_y = 0
    img.paste(crop, (paste_x, paste_y))

    px = img.load()
    bits = []
    for y in range(GLYPH_H):
        row = []
        for x in range(GLYPH_W):
            row.append(1 if px[x, y] >= THRESHOLD else 0)
        bits.append(row)
    return bits


def bits_to_bytes(bits):
    out = []
    for y in range(GLYPH_H):
        b0 = 0
        b1 = 0
        half = min(8, GLYPH_W)
        for x in range(half):
            if bits[y][x]:
                b0 |= (0x80 >> x)
        for x in range(half, GLYPH_W):
            if bits[y][x]:
                b1 |= (0x80 >> (x - half))
        out.append(b0)
        out.append(b1)
    return out


def build_sjis_table():
    """生成 g_sjis_unicode[]: 大小 60*188, 非法序列填 0xFFFF"""
    table = [0xFFFF] * (len(SJIS_LEADS) * 188)
    for lead in SJIS_LEADS:
        li = lead_idx(lead)
        for sec in list(range(0x40, 0x7F)) + list(range(0x80, 0xFD)):
            si = sec_idx(sec)
            if li < 0 or si < 0:
                continue
            try:
                ch = bytes([lead, sec]).decode("cp932")
            except Exception:
                continue
            if ch and len(ch) == 1:
                table[li * 188 + si] = ord(ch)
    return table


def main():
    menu = collect_chars(LNG_PATH)
    gb = collect_gb2312()
    sjis = collect_sjis()
    fw = collect_fullwidth()
    chars = sorted(menu | gb | sjis | fw, key=lambda c: ord(c))
    if not chars:
        print("未提取到任何非 ASCII 字符")
        sys.exit(1)

    font = None
    for p in FONT_CANDIDATES:
        try:
            font = load_font(p)
            print("使用字体:", p)
            break
        except Exception as e:  # noqa
            print("跳过", p, e)
    if font is None:
        sys.exit(1)

    codepoints = [ord(c) for c in chars]
    glyph_bytes = []
    for c in chars:
        glyph_bytes.extend(bits_to_bytes(render_glyph(font, c)))

    n = len(codepoints)
    lines = []
    bytes_per_glyph = GLYPH_H * 2
    lines.append("//---------------------------------------------------------------------------")
    lines.append("// File name:    font_cn.c  // 中文/日文点阵字库 (由 gen_cn_font.py 生成)")
    lines.append("//---------------------------------------------------------------------------")
    lines.append("// 每个汉字 %dx%d = %d 字节（实际使用 %d 列，高位补零）。" % (GLYPH_W, GLYPH_H, bytes_per_glyph, GLYPH_W))
    lines.append("// 配合 draw_text.c 的 UTF-8 / Shift-JIS 自动识别渲染。")
    lines.append("// cn_unicode[] 升序排列，draw_text.c 用二分查找定位字模。")
    lines.append("// g_sjis_unicode[] 供运行期把 Shift-JIS 双字节映射到 Unicode 码点。")
    lines.append("unsigned short cn_unicode[] = {")
    row = []
    for i, cp in enumerate(codepoints):
        row.append("0x%04X" % cp)
        if len(row) == 12 or i == n - 1:
            lines.append("    " + ", ".join(row) + ("," if i != n - 1 else ""))
            row = []
    lines.append("};")
    lines.append("")
    lines.append("// 字模数据: cn_glyph_index(cp) 返回下标，font_cn[idx*%d + r*2 .. +1] 为该字第 r 行" % bytes_per_glyph)
    lines.append("unsigned char font_cn[] = {")
    row = []
    for i, b in enumerate(glyph_bytes):
        row.append("0x%02X" % b)
        if len(row) == 12 or i == len(glyph_bytes) - 1:
            lines.append("    " + ", ".join(row) + ("," if i != len(glyph_bytes) - 1 else ""))
            row = []
    lines.append("};")
    lines.append("int cn_glyph_count = %d;" % n)
    lines.append("int cn_glyph_width = %d;" % GLYPH_W)
    lines.append("int cn_glyph_height = %d;" % GLYPH_H)
    lines.append("")
    lines.append("// Shift-JIS (cp932) -> Unicode 查找表，索引 = leadIdx*188 + secIdx")
    lines.append("//   非法序列填 0xFFFF。由 gen_cn_font.py 生成, 供 draw_text.c 的 decode_sjis() 使用。")
    lines.append("const unsigned short g_sjis_unicode[] = {")
    table = build_sjis_table()
    row = []
    for i, v in enumerate(table):
        row.append("0x%04X" % v)
        if len(row) == 12 or i == len(table) - 1:
            lines.append("    " + ", ".join(row) + ("," if i != len(table) - 1 else ""))
            row = []
    lines.append("};")
    lines.append("//---------------------------------------------------------------------------")
    lines.append("// End of file:  font_cn.c")
    lines.append("//---------------------------------------------------------------------------")

    with open("/workspace/font_cn.c", "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    print("字符数:", n,
          " 字库字节数:", len(glyph_bytes), "(约 %.1f KB)" % (len(glyph_bytes) / 1024))
    print("  - 菜单字符:", len(menu), " GB2312:", len(gb),
          " SJIS集:", len(sjis), " 全角:", len(fw))
    print("Shift-JIS 查找表:", len(table), "项 (约 %.1f KB)" % (len(table) * 2 / 1024))

    # 预览图 (8 列排布，取前若干字 + 末尾若干日文汉字做代表)
    preview_chars = chars[:64] + [c for c in chars if 0x3040 <= ord(c) <= 0x30FF][:32] \
        + [c for c in chars if 0x4E00 <= ord(c) <= 0x9FFF][-32:]
    preview_chars = sorted(set(preview_chars), key=lambda c: ord(c))
    cols = 8
    rows = (len(preview_chars) + cols - 1) // cols
    pad = 2
    cell = GLYPH_W + pad
    scale = 3
    bigw = cols * (GLYPH_W * scale + pad) + pad
    bigh = rows * (GLYPH_H * scale + pad) + pad
    prev = Image.new("L", (cols * cell + pad, rows * cell + pad), 255)
    pd = ImageDraw.Draw(prev)
    big = Image.new("L", (bigw, bigh), 255)
    bd = ImageDraw.Draw(big)
    for i, c in enumerate(preview_chars):
        bits = render_glyph(font, c)
        cx = (i % cols) * cell + pad
        cy = (i // cols) * cell + pad
        for y in range(GLYPH_H):
            for x in range(GLYPH_W):
                if bits[y][x]:
                    pd.point((cx + x, cy + y), 0)
        bx = (i % cols) * (GLYPH_W * scale + pad) + pad
        by = (i // cols) * (GLYPH_H * scale + pad) + pad
        for y in range(GLYPH_H):
            for x in range(GLYPH_W):
                if bits[y][x]:
                    bd.rectangle([bx + x * scale, by + y * scale,
                                  bx + x * scale + scale - 1, by + y * scale + scale - 1], 0)
    prev.save("/workspace/font_cn_preview.png")
    big.save("/workspace/font_cn_preview_big.png")
    print("已生成预览: /workspace/font_cn_preview.png, /workspace/font_cn_preview_big.png")


if __name__ == "__main__":
    main()
