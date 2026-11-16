#!/bin/sh
#=============================================================================
# Ensure smbman.irx is available for the NETFS (SMB1) build
#-----------------------------------------------------------------------------
# The smb: backend is now driven by the PS2SDK "smbman" IOP module (the same
# CIFS driver Open PS2 Loader uses). It must be embedded as smbman_irx.o.
#
# The upstream ps2homebrew container usually ships a prebuilt smbman.irx at
# $PS2SDK/iop/irx/smbman.irx. Older/slimmer images sometimes omit it. This
# script guarantees one exists by building iop/network/smbman from the PS2SDK
# source tree the workflow already cloned (PS2SDKSRC) and installing it.
#
# It never fails the build on its own: embed.make has a hard check that gives
# a clear error if, after this step, no smbman.irx can be found at all.
#=============================================================================
set -u

PS2SDK="${PS2SDK:-/usr/local/ps2dev/ps2sdk}"
PS2SDKSRC="${PS2SDKSRC:-$PS2SDK}"
DST="$PS2SDK/iop/irx/smbman.irx"

echo "=========================================="
echo "smbman.irx availability check"
echo "Using PS2SDK=$PS2SDK"
echo "Using PS2SDKSRC=$PS2SDKSRC"
echo "=========================================="

mkdir -p "$PS2SDK/iop/irx"

if [ -f "$DST" ]; then
  echo "OK: prebuilt smbman.irx already present at $DST"
  ls -la "$DST"
  exit 0
fi

# Locate the PS2SDK source tree if the environment hint is stale.
if [ ! -f "$PS2SDKSRC/Defs.make" ]; then
  for cand in /usr/local/ps2sdk "$PS2SDK" "$RUNNER_TEMP"/ps2sdk-src /tmp/ps2sdk-src; do
    [ -n "$cand" ] || continue
    if [ -f "$cand/Defs.make" ]; then
      PS2SDKSRC="$cand"
      echo "Using fallback PS2SDKSRC=$PS2SDKSRC"
      break
    fi
  done
fi

if [ ! -f "$PS2SDKSRC/Defs.make" ]; then
  echo "WARN: no PS2SDK source tree found; cannot rebuild smbman.irx here."
  echo "      embed.make will report the missing module at build time."
  exit 0
fi

MOD="$PS2SDKSRC/iop/network/smbman"
if [ ! -d "$MOD" ]; then
  echo "WARN: $MOD not found; cannot rebuild smbman.irx."
  exit 0
fi

echo "Building smbman.irx from $MOD ..."
if make -C "$MOD" all 2>&1; then
  echo "  OK: smbman module built"
else
  echo "  WARN: 'make -C $MOD all' failed; trying full iop build"
  make -C "$PS2SDKSRC" iop 2>&1 || echo "WARN: 'make iop' also failed"
fi

SRC_IRX="$MOD/irx/smbman.irx"
if [ -f "$SRC_IRX" ]; then
  cp -f "$SRC_IRX" "$DST" || {
    echo "WARN: failed to copy $SRC_IRX -> $DST"
    exit 0
  }
  echo "OK: installed $SRC_IRX -> $DST"
else
  # Some SDK layouts install straight into iop/irx during 'make iop'.
  if [ -f "$PS2SDKSRC/iop/irx/smbman.irx" ]; then
    cp -f "$PS2SDKSRC/iop/irx/smbman.irx" "$DST"
    echo "OK: installed smbman.irx from source tree iop/irx"
  else
    echo "WARN: smbman.irx not produced; embed.make will report it."
  fi
fi

echo "Final check:"
ls -la "$DST" 2>&1 || true
echo "=========================================="
