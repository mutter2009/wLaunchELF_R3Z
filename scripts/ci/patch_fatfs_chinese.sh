#!/bin/sh
#=============================================================================
# Patch FatFs for Chinese (GBK / CP936) long file names on FAT32 + exFAT
#-----------------------------------------------------------------------------
# 此脚本在 GitHub Actions 构建 wLaunchELF 之前运行。
# 关键修正（相对上一版）：
#   1. 只做 `sed` 改 ffconf.h 的宏数值，绝不 `make clean`、绝不 `download_dependencies`，
#      否则会把已经 patch 好的 fatfs 源码删掉再重新 clone 回未 patch 的版本。
#   2. 用 ps2sdk 标准的 `make -C iop/fs ... install` 从源码重新编译出 936 版 IRX，
#      并覆盖预编译的旧 IRX（之前只调 `make` 默认目标，只执行了 clean，没编译）。
#=============================================================================
set -u

echo "=========================================="
echo "FatFs Chinese LFN patch script"
PS2SDK="${PS2SDK:-/usr/local/ps2dev/ps2sdk}"
PS2SDKSRC="${PS2SDKSRC:-$PS2SDK}"
echo "Using PS2SDK=$PS2SDK"
echo "Using PS2SDKSRC=$PS2SDKSRC"
echo "=========================================="

# 1) 定位 fatfs 的 ffconf.h（容器里源码与预编译混合，可能有几种路径）
FFCONF=""
for cand in \
  "$PS2SDK/common/external_deps/fatfs/source/include/ffconf.h" \
  "$PS2SDKSRC/common/external_deps/fatfs/source/include/ffconf.h" \
  "$PS2SDK/common/external_deps/fatfs_inprogress/source/include/ffconf.h" \
  "$PS2SDKSRC/common/external_deps/fatfs_inprogress/source/include/ffconf.h" \
  "$PS2SDK/iop/fs/bdmfs_fatfs/src/ffconf.h" \
  "$PS2SDKSRC/iop/fs/bdmfs_fatfs/src/ffconf.h" ; do
  if [ -f "$cand" ]; then FFCONF="$cand"; break; fi
done
if [ -z "$FFCONF" ]; then
  echo "ERROR: ffconf.h not found under $PS2SDK / $PS2SDKSRC"
  find "$PS2SDK" "$PS2SDKSRC" -name ffconf.h 2>/dev/null | head
  exit 1
fi
echo "Found ffconf.h: $FFCONF"

echo "--- Before patch ---"
grep -nE "FF_CODE_PAGE|FF_USE_LFN|FF_FS_EXFAT|FF_LFN_UNICODE" "$FFCONF" || true

# 2) 只改宏的数值，绝不产生 #undef，也不动其它行
sed -i -e 's/^#define[[:space:]][[:space:]]*FF_CODE_PAGE[[:space:]][[:space:]]*[0-9].*/#define FF_CODE_PAGE   936/' \
       -e 's/^#define[[:space:]][[:space:]]*FF_USE_LFN[[:space:]][[:space:]]*[0-9].*/#define FF_USE_LFN   2/' \
       -e 's/^#define[[:space:]][[:space:]]*FF_FS_EXFAT[[:space:]][[:space:]]*[0-9].*/#define FF_FS_EXFAT   1/' \
       -e 's/^#define[[:space:]][[:space:]]*FF_LFN_UNICODE[[:space:]][[:space:]]*[0-9].*/#define FF_LFN_UNICODE   0/' \
       "$FFCONF"

echo "--- After patch ---"
grep -nE "FF_CODE_PAGE|FF_USE_LFN|FF_FS_EXFAT|FF_LFN_UNICODE" "$FFCONF" || true

# 3) 确认 CP936 转换表存在（ffunicode.c 含 936 或全表）
FATSRC_DIR="$(dirname "$(dirname "$FFCONF")")"
if grep -q "936" "$FATSRC_DIR/source/ffunicode.c" 2>/dev/null; then
  echo "OK: ffunicode.c appears to support CP936"
else
  echo "WARN: could not confirm CP936 in ffunicode.c (continuing anyway)"
fi

# 4) 强制 fatfs 重新编译（改了 ffconf.h 头文件，需 touch 源文件让 make 重编）
echo "Touching FatFs sources to force rebuild..."
touch "$FATSRC_DIR"/source/*.c 2>/dev/null || true

# 5) 从源码重新编译并安装存储模块 IRX 到 $PS2SDK/iop/irx/
#    ps2sdk 标准方式：make -C <module> all install
#    顺序：bdm(底层) -> bdmfs_fatfs(依赖 bdm + fatfs) -> usbmass_bd
rebuild_module() {
  mod="$1"
  echo "Building $mod ..."
  if make -C "$PS2SDK/$mod" all install 2>&1; then
    echo "  OK: built $mod"
  else
    echo "  WARN: 'make -C $PS2SDK/$mod all install' failed; will try full 'make iop' fallback"
    return 1
  fi
}

ok=1
rebuild_module iop/fs/bdm          || ok=0
rebuild_module iop/fs/bdmfs_fatfs  || ok=0
rebuild_module iop/usb/usbmass_bd  || ok=0

# 6) 兜底：若单个模块编译失败，整体编译 iop 层（ps2sdk 标准入口，上下文最完整）
if [ "$ok" -eq 0 ]; then
  echo "Falling back to full 'make -C $PS2SDK iop' ..."
  make -C "$PS2SDK" iop 2>&1 || echo "WARN: 'make iop' also failed; check toolchain env"
fi

# 7) 诊断输出
echo "=========================================="
echo "Final IRX files (timestamps should be NEW, not Sep 3):"
ls -la "$PS2SDK/iop/irx/bdm.irx" \
      "$PS2SDK/iop/irx/bdmfs_fatfs.irx" \
      "$PS2SDK/iop/irx/usbmass_bd.irx" 2>&1 || true
echo "FatFs Chinese patch complete."
echo "=========================================="
