#!/bin/bash
# VelvetKeys Unified Build Script
# Lokal & GitHub Actions kompatibel

set -e
set -o pipefail

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

echo "Building VelvetKeys for Raspberry Pi $RPI"
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

# Copy lv_conf.h to lvgl
# cp src/lv_conf.h libs/circle-stdlib/libs/circle/addon/lvgl

cd libs/circle-stdlib
if [ -f Config.mk ]; then
    make mrproper
fi

./configure \
    -r ${RPI} \
    --prefix "${TOOLCHAIN_PREFIX}" \
    ${OPTIONS} \
    -o USB_GADGET_VENDOR_ID=0x2E8A \

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
cd ../lvgl && make clean && make -j${NCPU}
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
# Build VelvetKeys
# ───────────────────────────────────────────────

echo "Building VelvetKeys..."
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
