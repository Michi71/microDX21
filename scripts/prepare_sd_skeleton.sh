#!/usr/bin/env bash
# scripts/prepare_sd_skeleton.sh
#
# Create the empty SD card directory skeleton for microDX21 RAM-bank and
# performance persistence. Idempotent: safe to run multiple times.
#
# Usage:
#   scripts/prepare_sd_skeleton.sh <mountpoint>
#
# Example:
#   scripts/prepare_sd_skeleton.sh /Volumes/boot
#   scripts/prepare_sd_skeleton.sh /mnt/sd
#
# The same logic is embedded in .github/workflows/build.yml so the
# released SD image ships with the skeleton pre-created.
set -euo pipefail

MOUNT="${1:-}"
if [[ -z "$MOUNT" ]]; then
  echo "usage: $0 <sd-mountpoint>" >&2
  exit 2
fi
if [[ ! -d "$MOUNT" ]]; then
  echo "error: mountpoint '$MOUNT' does not exist or is not a directory" >&2
  exit 2
fi

# These must match config/microdx21.ini and the constants in
# src/opm/memory/dx21_memory.cpp / src/audio/opmemuadapter.h.
BASE_DIR="MICRODX21"
BANK_PREFIX="BANK_"
BANK_COUNT=16
BANK_DIGITS=2

printf -v BANK_FMT "%s%%0%dd" "$BANK_PREFIX" "$BANK_DIGITS"

echo "Preparing microDX21 SD skeleton under $MOUNT/$BASE_DIR/"
mkdir -p "$MOUNT/$BASE_DIR"
for ((i = 1; i <= BANK_COUNT; i++)); do
  printf -v bankdir "$BANK_FMT" "$i"
  mkdir -p "$MOUNT/$BASE_DIR/$bankdir"
done
echo "Created $BANK_COUNT bank directories (BANK_01..BANK_$(printf "%0${BANK_DIGITS}d" $BANK_COUNT))"
echo "Done."
