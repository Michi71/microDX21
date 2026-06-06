#
# Makefile - microDX21
#
# 4-millis: clean source tree, no LVGL, single-encoder + 128x32 SSD1305 OLED
#

CIRCLE_STDLIB_DIR = ../libs/circle-stdlib
DISPLAY_DIR = ../libs/libdisplay2
SENSOR_DIR  = ../libs/circle-stdlib/libs/circle/addon/sensor

OBJS = main.o kernel.o microdx21.o \
       system/config.o \
       audio/microdx21_multicore.o \
       audio/microdx21_i2s.o audio/microdx21_pwm.o \
       display/display_dx21.o \
       display/dx21_input.o \
       midi/microdx21_pckeyboard.o midi/microdx21_usbmididevice.o midi/microdx21_serialmididevice.o \
       midi/microdx21_mididevice.o \
       opm/opmemu.o opm/opm.o opm/memory/dx21_memory.o

ifneq ($(filter 4 5,$(RPI)),)
OBJS += audio/microdx21_usb.o
endif

EXTRACLEAN = $(OBJS) $(OBJS:.o=.d)

# RPi 3/4/5 (Cortex-A53) tuning
# Pull ARCH from Config.mk (set by ./configure in libs/circle-stdlib)
ARCH ?= -DAARCH=64 -mcpu=cortex-a53 -mlittle-endian
CFLAGS_FOR_TARGET ?= $(ARCH) -D__circle__ -DRASPPI=3 -DREALTIME -DSAVE_VFP_REGS_ON_IRQ -DSCREEN_DMA_BURST_LENGTH=1
CXXFLAGS_FOR_TARGET ?= $(CFLAGS_FOR_TARGET)

include ./Synth_OPM.mk
include ./display.mk
include ./sensor.mk

# Include paths
INCLUDE += -I . -I ./audio -I ./midi -I ./display -I ./system -I ./util
INCLUDE += -I $(DISPLAY_DIR)
INCLUDE += -I $(SENSOR_DIR)

include ./Rules.mk

STANDARD := -std=gnu++17 -Wno-aligned-new -Wno-psabi
CXXFLAGS := $(filter-out -std=c++14 -std=gnu++14 -std=c++11 -std=gnu++11,$(CXXFLAGS))
CXXFLAGS += $(STANDARD)
CFLAGS += $(CFLAGS_FOR_TARGET)
CXXFLAGS += $(CXXFLAGS_FOR_TARGET)

# USB Gadget IDs (must be non-zero)
CXXFLAGS += -DUSB_GADGET_VENDOR_ID=0x1D6B -DUSB_GADGET_DEVICE_ID_MIDI=0x8001

# Nuked-OPM is C, not C++: explicit rule
opm/opm.o: opm/opm.c opm/opm.h
	$(CC) $(CFLAGS) $(INCLUDE) -c -o $@ $<
