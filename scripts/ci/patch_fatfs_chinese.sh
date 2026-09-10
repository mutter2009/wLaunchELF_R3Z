#!/bin/bash
# Patch FatFs configuration so bdmfs_fatfs returns Chinese long file names
# in GBK (code page 936) for BOTH FAT32 and exFAT volumes.
# This script is intended for the GitHub Actions CI build.

set -e

PS2SDKSRC="${PS2SDKSRC:-}"
PS2SDK="${PS2SDK:-}"

if [ -z "$PS2SDKSRC" ]; then
  echo "Error: PS2SDKSRC is not set." >&2
  exit 1
fi
if [ ! -f "$PS2SDKSRC/Defs.make" ]; then
  echo "Error: $PS2SDKSRC does not look like a PS2SDK source tree." >&2
  exit 1
fi

# Download fatfs external dependency if it has not been cloned yet.
if [ ! -d "$PS2SDKSRC/common/external_deps/fatfs" ]; then
  echo "Downloading PS2SDK external dependencies (this includes FatFs)..."
  (cd "$PS2SDKSRC" && ./download_dependencies.sh)
fi

FFCFG="$PS2SDKSRC/common/external_deps/fatfs/source/include/ffconf.h"
if [ ! -f "$FFCFG" ]; then
  echo "Error: $FFCFG not found. FatFs layout may have changed." >&2
  echo "Contents of $PS2SDKSRC/common/external_deps/fatfs:" >&2
  find "$PS2SDKSRC/common/external_deps/fatfs" -maxdepth 3 -type f >&2 || true
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

# Remove the prebuilt IRX so embed.make rebuilds bdmfs_fatfs from patched source.
if [ -n "$PS2SDK" ] && [ -f "$PS2SDK/iop/irx/bdmfs_fatfs.irx" ]; then
  echo "Removing prebuilt $PS2SDK/iop/irx/bdmfs_fatfs.irx so it rebuilds from source..."
  rm -f "$PS2SDK/iop/irx/bdmfs_fatfs.irx"
else
  echo "No prebuilt bdmfs_fatfs.irx found; source build will be used automatically."
fi
