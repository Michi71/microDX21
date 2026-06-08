#!/usr/bin/env bash
# microDX21 Unified Build Script
# Lokal & GitHub Actions kompatibel

set -e
set -o pipefail

# ───────────────────────────────────────────────
# Toolchain-Pfad (lokal, 15.2.rel1)
# ───────────────────────────────────────────────
# Default to the local ARM GNU Toolchain 15.2.rel1 if available;
# the CI / GitHub-Actions environment overrides this via
# $TOOLCHAIN_BIN or by having `aarch64-none-elf-*` on PATH already.
if [ -z "${TOOLCHAIN_BIN:-}" ]; then
    if [ -d "/Applications/ArmGNUToolchain/15.2.rel1/aarch64-none-elf/bin" ]; then
        export PATH="/Applications/ArmGNUToolchain/15.2.rel1/aarch64-none-elf/bin:$PATH"
    fi
fi

# ───────────────────────────────────────────────
# Parameter prüfen
# ───────────────────────────────────────────────

if [ -z "$RPI" ]; then
    echo "Error: RPI variable not set (1,2,3,4,5)"
    exit 1
fi

if [ "$RPI" -gt "2" ]; then
    TOOLCHAIN_PREFIX="aarch64-none-elf-"
else
    TOOLCHAIN_PREFIX="arm-none-eabi-"
fi

echo "Building microDX21 for Raspberry Pi $RPI"
echo "Using toolchain prefix: $TOOLCHAIN_PREFIX"

# ───────────────────────────────────────────────
# Circle System Options
# ───────────────────────────────────────────────

OPTIONS=""
#if [ "$RPI" -gt "4" ]; then
#    OPTIONS="${OPTIONS} -o SCREEN_HEADLESS"              # CRITICAL for RPi4/5/CM5
#fi
OPTIONS="${OPTIONS} -o REALTIME"                    # IRQ latency improvements
OPTIONS="${OPTIONS} -o SAVE_VFP_REGS_ON_IRQ"        # FPU safety
OPTIONS="${OPTIONS} -o SCREEN_DMA_BURST_LENGTH=1"   # Stable DMA timing

# Multi-core for RPi2+
if [ "$RPI" -gt "1" ]; then
    OPTIONS="${OPTIONS} -o ARM_ALLOW_MULTI_CORE"
fi

# SDHOST only for RPi3
if [ "$RPI" == "3" ]; then
    OPTIONS="${OPTIONS} -o USE_SDHOST"
fi

echo "Circle options: $OPTIONS"

# ───────────────────────────────────────────────
# Build Circle
# ───────────────────────────────────────────────

echo "Building Circle..."

cd libs/circle-stdlib
if [ -f Config.mk ]; then
    make mrproper
fi

# circle-stdlib's ./configure (Circle 51.0+) uses the bash builtin `mapfile`,
# which only exists in bash >= 4. macOS still ships bash 3.2 as /bin/bash, so
# we must run configure with a newer bash (e.g. Homebrew's). Pick the first
# bash >= 4 we can find.
BASH4=""
for cand in "${BASH:-}" "$(command -v bash)" /opt/homebrew/bin/bash /usr/local/bin/bash; do
    [ -x "$cand" ] || continue
    if [ "$("$cand" -c 'echo ${BASH_VERSINFO[0]}')" -ge 4 ] 2>/dev/null; then
        BASH4="$cand"
        break
    fi
done

if [ -z "$BASH4" ]; then
    echo "Error: circle-stdlib's ./configure needs bash >= 4 (for 'mapfile')." >&2
    echo "       macOS ships bash 3.2. Install a newer bash, e.g.: brew install bash" >&2
    exit 1
fi

echo "Using bash for configure: $BASH4 ($("$BASH4" --version | head -1))"

"$BASH4" ./configure \
    -r ${RPI} \
    --prefix "${TOOLCHAIN_PREFIX}" \
    ${OPTIONS} \

# Detect OS for CPU count
if [ "$(uname -s)" = "Darwin" ]; then
    NCPU=$(sysctl -n hw.ncpu)
else
    NCPU=$(nproc)
fi

make -j${NCPU}

# Addons
cd libs/circle/addon/Properties && make clean && make -j${NCPU}
# cd ../wlan && make clean && make -j${NCPU}
cd ../sensor && make clean && make -j${NCPU}
cd ../display && make clean && make -j${NCPU}
cd ../../../../../..

# ───────────────────────────────────────────────
# Build libdisplay2
# ───────────────────────────────────────────────

echo "Building libdisplay2..."
cd libs/libdisplay2
make clean || true
make -j${NCPU}
cd ../..

# ───────────────────────────────────────────────
# Build microDX21
# ───────────────────────────────────────────────

echo "Building microDX21..."
cd src
make clean || true
make -j${NCPU}
cd ..

# ───────────────────────────────────────────────
# Output
# ───────────────────────────────────────────────

mkdir -p out
cp ./src/kernel*.img ./out/kernel_rpi${RPI}.img

echo "✔ Build complete: out/kernel_rpi${RPI}.img"
