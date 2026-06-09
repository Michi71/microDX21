#!/usr/bin/env bash

# ───────────────────────────────────────────────────────────────
# VelvetKeys macOS Build Script
# Komfortabel • Farbig • Fehlertolerant • Schnell
# ───────────────────────────────────────────────────────────────

set -o pipefail

# Farben
RED="\033[0;31m"
GREEN="\033[0;32m"
YELLOW="\033[1;33m"
BLUE="\033[0;34m"
NC="\033[0m"

echo -e "${BLUE}───────────────────────────────────────────────${NC}"
echo -e "${BLUE} microDX21 macOS Build System${NC}"
echo -e "${BLUE}───────────────────────────────────────────────${NC}"

# ───────────────────────────────────────────────────────────────
# Optionen
# ───────────────────────────────────────────────────────────────

RPI=3
CLEAN=0
VERBOSE=0

usage() {
    echo "Usage: $0 [--rpi 1|2|3|4|5] [--clean] [--verbose]"
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --rpi)
            RPI="$2"
            shift 2
            ;;
        --clean)
            CLEAN=1
            shift
            ;;
        --verbose)
            VERBOSE=1
            shift
            ;;
        *)
            usage
            ;;
    esac
done

echo -e "${GREEN}Target Raspberry Pi:${NC} $RPI"
[[ $CLEAN -eq 1 ]] && echo -e "${YELLOW}Clean build enabled${NC}"
[[ $VERBOSE -eq 1 ]] && echo -e "${YELLOW}Verbose mode enabled${NC}"

# ───────────────────────────────────────────────────────────────
# Toolchain finden
# ───────────────────────────────────────────────────────────────

# TOOLCHAIN_DIR="/usr/local/arm-gnu-toolchain-14.3.rel1-darwin-arm64-aarch64-none-elf/bin"
TOOLCHAIN_DIR="/Applications/ArmGNUToolchain/15.2.rel1/aarch64-none-elf/bin"

if [[ ! -d "$TOOLCHAIN_DIR" ]]; then
    echo -e "${RED}Error: ARM toolchain not found at:${NC}"
    echo "  $TOOLCHAIN_DIR"
    echo "Install it here or update the path in this script."
    exit 1
fi

export PATH="$TOOLCHAIN_DIR:$PATH"

if ! command -v aarch64-none-elf-gcc >/dev/null; then
    echo -e "${RED}Error: aarch64-none-elf-gcc not found in PATH${NC}"
    exit 1
fi

echo -e "${GREEN}Toolchain OK${NC}"

# ───────────────────────────────────────────────────────────────
# Submodules aktualisieren
# ───────────────────────────────────────────────────────────────

echo -e "${BLUE}Updating submodules...${NC}"
git submodule update --init --recursive

# ───────────────────────────────────────────────────────────────
# Clean?
# ───────────────────────────────────────────────────────────────

if [[ $CLEAN -eq 1 ]]; then
    echo -e "${YELLOW}Cleaning Circle and VelvetKeys...${NC}"
    (cd libs/circle-stdlib && make mrproper || true)
    (cd src && make clean || true)
fi

# ───────────────────────────────────────────────────────────────
# Build starten
# ───────────────────────────────────────────────────────────────

echo -e "${BLUE}Starting build...${NC}"

LOGFILE="build_$(date +%Y%m%d_%H%M%S).log"

RPI=$RPI VERBOSE=$VERBOSE bash -ex build.sh 2>&1 | tee "$LOGFILE"

STATUS=${PIPESTATUS[0]}

if [[ $STATUS -ne 0 ]]; then
    echo -e "${RED}❌ Build failed!${NC}"
    echo "See log: $LOGFILE"
    exit $STATUS
fi

echo -e "${GREEN}✔ Build complete!${NC}"
echo "Kernel image: src/kernel*.img"
echo "Log saved to: $LOGFILE"
