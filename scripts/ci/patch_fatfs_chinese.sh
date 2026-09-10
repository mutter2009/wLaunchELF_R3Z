#!/bin/bash
# Patch FatFs configuration so bdmfs_fatfs returns Chinese long file names
# in GBK (code page 936) for BOTH FAT32 and exFAT volumes.
# This script is intended for the GitHub Actions CI build.
#
# It also rebuilds the storage stack modules (bdm, bdmfs_fatfs, usbmass_bd)
# from patched source and installs them into $PS2SDK/iop/irx so embed.make
# picks up the patched versions instead of the prebuilt ones.

set -e

# Prefer explicitly-set variables, otherwise fall back to common defaults.
PS2SDK="${PS2SDK:-${PS2SDK_PREFIX:-}}"
PS2SDKSRC="${PS2SDKSRC:-${PS2SDK_SRC:-}}"

if [ -z "$PS2SDK" ]; then
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

if [ -z "$PS2SDKSRC" ] || [ ! -f "$PS2SDKSRC/Defs.make" ]; then
  # Try pointing at the install tree itself; some builds use PS2SDK==PS2SDKSRC.
  if [ -f "$PS2SDK/iop/Rules.make" ]; then
    PS2SDKSRC="$PS2SDK"
  else
    echo "Error: PS2SDKSRC is not set or does not point to a valid PS2SDK source tree." >&2
    exit 1
  fi
fi

echo "Using PS2SDK=$PS2SDK"
echo "Using PS2SDKSRC=$PS2SDKSRC"

# Export for child make processes
export PS2SDK PS2SDKSRC

# Download fatfs external dependency if it has not been cloned yet.
FATFS_DIR="$PS2SDKSRC/common/external_deps/fatfs"
if [ ! -d "$FATFS_DIR" ]; then
  echo "Downloading PS2SDK external dependencies (this includes FatFs)..."
  (cd "$PS2SDKSRC" && ./download_dependencies.sh)
fi

FFCFG="$FATFS_DIR/source/include/ffconf.h"
if [ ! -f "$FFCFG" ]; then
  echo "Error: $FFCFG not found. FatFs layout may have changed." >&2
  find "$FATFS_DIR" -maxdepth 3 -type f >&2 || true
  exit 1
fi

# Helper: ensure a macro exists and is set to the given numeric value;
# if it does not exist, append it.
set_macro() {
  local name="$1" value="$2"
  if grep -qE "^#define[[:space:]]+$name[[:space:]]" "$FFCFG"; then
    sed -i -E "s|^#define[[:space:]]+$name[[:space:]]+[0-9]+[[:space:]]*.*|#define $name  $value|" "$FFCFG"
  else
    printf '#define %s  %s\n' "$name" "$value" >> "$FFCFG"
  fi
}

echo "Before patch:"
grep -E '^#define FF_CODE_PAGE|^#define FF_USE_LFN|^#define FF_FS_EXFAT|^#define FF_LFN_UNICODE' "$FFCFG" || true

set_macro FF_CODE_PAGE    936   # Simplified Chinese / GBK -> returns GBK-encoded long names
set_macro FF_USE_LFN       2     # Enable long file names (stack buffer)
set_macro FF_FS_EXFAT      1     # Enable exFAT long-name support
set_macro FF_LFN_UNICODE   0     # LFN returned in current code page (GBK), matching wLaunchELF GBK decoder

echo "After patch:"
grep -E '^#define FF_CODE_PAGE|^#define FF_USE_LFN|^#define FF_FS_EXFAT|^#define FF_LFN_UNICODE' "$FFCFG" || true

# Make sure external deps are built before we try to build the modules.
echo "Building PS2SDK common/external_deps (FatFs etc.)..."
make -C "$PS2SDKSRC/common/external_deps" all

# Build and install the storage modules so embed.make picks them up.
build_install_module() {
  local mod_dir="$1" mod_name="$2"
  if [ ! -f "$mod_dir/Makefile" ]; then
    echo "Warning: $mod_dir/Makefile not found, skipping $mod_name build." >&2
    return 0
  fi
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

echo "FatFs Chinese patch and storage module rebuild complete."
