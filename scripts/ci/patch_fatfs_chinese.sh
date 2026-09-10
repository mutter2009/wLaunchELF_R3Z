#!/bin/bash
# Patch FatFs configuration so bdmfs_fatfs returns Chinese long file names
# in GBK (code page 936) for BOTH FAT32 and exFAT volumes.
# This script is intended for the GitHub Actions CI build.
#
# It also rebuilds the storage stack modules (bdm, bdmfs_fatfs, usbmass_bd)
# from patched source and installs them into $PS2SDK/iop/irx so embed.make
# picks up the patched versions instead of the prebuilt ones.

set -e

PS2SDK="${PS2SDK:-}"
PS2SDKSRC="${PS2SDKSRC:-}"

echo "=========================================="
echo "FatFs Chinese LFN patch script"
echo "Initial PS2SDK=$PS2SDK"
echo "Initial PS2SDKSRC=$PS2SDKSRC"
echo "=========================================="

# 1. Locate PS2SDK install tree.
if [ -z "$PS2SDK" ] || [ ! -f "$PS2SDK/Defs.make" ]; then
  for cand in /usr/local/ps2sdk /opt/ps2sdk /ps2sdk "$HOME/ps2sdk"; do
    if [ -f "$cand/Defs.make" ]; then
      PS2SDK="$cand"
      break
    fi
  done
fi
if [ -z "$PS2SDK" ] || [ ! -f "$PS2SDK/Defs.make" ]; then
  echo "Error: PS2SDK is not set or does not point to a valid PS2SDK install tree." >&2
  exit 1
fi
export PS2SDK

# 2. Locate PS2SDK source tree.
if [ -z "$PS2SDKSRC" ] || [ ! -f "$PS2SDKSRC/Defs.make" ]; then
  # Try using the install tree itself if it also contains sources.
  if [ -f "$PS2SDK/iop/Rules.make" ] && [ -f "$PS2SDK/iop/fs/bdmfs_fatfs/Makefile" ]; then
    PS2SDKSRC="$PS2SDK"
  else
    echo "Error: PS2SDKSRC is not set or does not point to a valid PS2SDK source tree." >&2
    exit 1
  fi
fi
export PS2SDKSRC

echo "Using PS2SDK=$PS2SDK"
echo "Using PS2SDKSRC=$PS2SDKSRC"

# 3. Ensure external dependencies (FatFs) are present.
FATFS_DIR="$PS2SDKSRC/common/external_deps/fatfs"
if [ ! -d "$FATFS_DIR" ]; then
  echo "Downloading PS2SDK external dependencies (this includes FatFs)..."
  if [ -x "$PS2SDKSRC/download_dependencies.sh" ]; then
    (cd "$PS2SDKSRC" && ./download_dependencies.sh)
  else
    echo "Error: $PS2SDKSRC/download_dependencies.sh not found." >&2
    exit 1
  fi
fi

FFCFG="$FATFS_DIR/source/include/ffconf.h"
if [ ! -f "$FFCFG" ]; then
  echo "Error: $FFCFG not found. FatFs layout may have changed." >&2
  find "$FATFS_DIR" -maxdepth 3 -type f >&2 || true
  exit 1
fi

# 4. Helper: set a numeric macro in ffconf.h using #undef + #define to avoid redefinition conflicts.
set_macro() {
  local name="$1" value="$2"
  # Remove existing definition and any previous #undef.
  sed -i -E "/^#define[[:space:]]+${name}[[:space:]]/d" "$FFCFG"
  sed -i -E "/^#undef[[:space:]]+${name}[[:space:]]*$/d" "$FFCFG"
  # Append #undef and #define at end of file.
  printf '\n#undef %s\n#define %s  %s\n' "$name" "$name" "$value" >> "$FFCFG"
}

echo ""
echo "Before patch ($FFCFG):"
grep -E '^#undef FF_CODE_PAGE|^#define FF_CODE_PAGE|^#undef FF_USE_LFN|^#define FF_USE_LFN|^#undef FF_FS_EXFAT|^#define FF_FS_EXFAT|^#undef FF_LFN_UNICODE|^#define FF_LFN_UNICODE' "$FFCFG" || true

set_macro FF_CODE_PAGE    936   # Simplified Chinese / GBK -> returns GBK-encoded long names
set_macro FF_USE_LFN       2     # Enable long file names (stack buffer)
set_macro FF_FS_EXFAT      1     # Enable exFAT long-name support
set_macro FF_LFN_UNICODE   0     # LFN returned in current code page (GBK), matching wLaunchELF GBK decoder

echo ""
echo "After patch ($FFCFG):"
grep -E '^#undef FF_CODE_PAGE|^#define FF_CODE_PAGE|^#undef FF_USE_LFN|^#define FF_USE_LFN|^#undef FF_FS_EXFAT|^#define FF_FS_EXFAT|^#undef FF_LFN_UNICODE|^#define FF_LFN_UNICODE' "$FFCFG" || true

# 5. Verify ffunicode.c supports code page 936.
FFUNI="$FATFS_DIR/source/ffunicode.c"
if [ -f "$FFUNI" ]; then
  echo ""
  echo "Checking $FFUNI for CP936 support..."
  if grep -qE 'FF_CODE_PAGE == 936|FF_CODE_PAGE == 0' "$FFUNI"; then
    echo "OK: ffunicode.c appears to support CP936 or code-page 0 (all tables)."
  else
    echo "WARNING: ffunicode.c does not appear to support CP936. Chinese LFN may not work." >&2
  fi
fi

# 6. Clean and rebuild external deps so FatFs picks up the patched ffconf.h.
echo ""
echo "Cleaning FatFs external deps to force rebuild with patched config..."
make -C "$PS2SDKSRC/common/external_deps" clean 2>/dev/null || true
make -C "$PS2SDKSRC/common/external_deps" all

# 7. Clean and rebuild the storage modules so they embed the new FatFs.
build_install_module() {
  local mod_dir="$1" mod_name="$2"
  if [ ! -f "$mod_dir/Makefile" ]; then
    echo "Warning: $mod_dir/Makefile not found, skipping $mod_name build." >&2
    return 0
  fi
  echo ""
  echo "Building $mod_name from $mod_dir ..."
  make -C "$mod_dir" clean 2>/dev/null || true
  make -C "$mod_dir" release
  echo "$mod_name installed to $PS2SDK/iop/irx/"
  ls -la "$PS2SDK/iop/irx/$mod_name.irx" || true
}

build_install_module "$PS2SDKSRC/iop/fs/bdm"         "bdm"
build_install_module "$PS2SDKSRC/iop/fs/bdmfs_fatfs" "bdmfs_fatfs"
build_install_module "$PS2SDKSRC/iop/usb/usbmass_bd" "usbmass_bd"

# Optional: also rebuild ata_bd if the source is available.
if [ -f "$PS2SDKSRC/iop/dev9/ata_bd/Makefile" ]; then
  build_install_module "$PS2SDKSRC/iop/dev9/ata_bd"  "ata_bd"
fi

echo ""
echo "=========================================="
echo "FatFs Chinese patch and storage module rebuild complete."
echo "Final IRX files:"
ls -la "$PS2SDK/iop/irx/bdm.irx" "$PS2SDK/iop/irx/bdmfs_fatfs.irx" "$PS2SDK/iop/irx/usbmass_bd.irx" || true
echo "=========================================="
