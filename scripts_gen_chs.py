#!/usr/bin/env python3
# 重新生成 Lang/CHS.LNG：以上游 ENG.LNG (主枚举) 为基准，按字符串 name 从 fork 的 CHS.LNG 取中文值，
# 缺失的上游新串回退英文；SMB 串手动注入中文。保证 CHS.LNG 的 id/name 与 ENG.LNG 完全对齐。
import re, subprocess, sys

RE = re.compile(r'lang\(\s*(\d+)\s*,\s*([A-Za-z_0-9]+)\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)')

def parse(text):
    out = []
    for m in RE.finditer(text):
        out.append((int(m.group(1)), m.group(2), m.group(3)))
    return out

# 上游 ENG.LNG（当前磁盘文件，已含 SMB 串）
with open('Lang/ENG.LNG', 'r', encoding='utf-8') as f:
    eng = parse(f.read())

# fork 的 CHS.LNG（按 name 建索引）
fork_chs_txt = subprocess.check_output(['git', 'show', 'origin/master:Lang/CHS.LNG'], text=True)
fork_chs = {name: val for (_id, name, val) in parse(fork_chs_txt)}

# SMB 串中文覆盖
override = {
    'SMB_Config': 'SMB 网络设置',
    'SMB_Name': '显示名称',
    'SMB_Server': '服务器地址',
    'SMB_User': '用户名',
    'SMB_Password': '密码',
}

lines = []
fallback = []  # 回退英文的上游串
dropped = []  # fork 有但上游没有的串（将被丢弃）
for (_id, name, eng_val) in eng:
    if name in override:
        val = override[name]
    elif name in fork_chs:
        val = fork_chs[name]
    else:
        val = eng_val
        fallback.append(name)
    lines.append('lang(%d, %s, "%s")' % (_id, name, val))

# 找出 fork 有但上游没有的 name（避免静默丢失重要中文）
eng_names = {name for (_id, name, _v) in eng}
for name in fork_chs:
    if name not in eng_names:
        dropped.append(name)

with open('Lang/CHS.LNG', 'w', encoding='utf-8') as f:
    f.write('\n'.join(lines) + '\n')

print('CHS.LNG 生成条数:', len(lines), '(ENG 基准:', len(eng), ')')
print('回退英文的上游新串数:', len(fallback))
if fallback:
    print('  回退串:', fallback)
print('fork 有但上游已不存在(丢弃)的串数:', len(dropped))
if dropped:
    print('  丢弃串:', dropped)
