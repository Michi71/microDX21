#
# Synth_OPM.mk
#
# Build flags for the DX21 FM synthesizer engine (Nuked OPM / COPMEmu).
# Replaces the former Synth_VKSynth.mk which targeted the sample-based
# VKPiano engine.
#

OPM_DIR = ../src/opm

INCLUDE += -I $(OPM_DIR)
INCLUDE += -I $(OPM_DIR)/io
INCLUDE += -I $(OPM_DIR)/memory

# ═══════════════════════════════════════════════════════════
# AUDIO-SPECIFIC FLAGS
# ═══════════════════════════════════════════════════════════
# -fno-math-errno: Keine math errno für bessere Performance
# -fno-trapping-math: Keine Trapping für schnellere FP-Operationen
# -ffast-math: Fast Math für Audio (sicher bei kontrollierten Eingaben)
# -mcpu=cortex-a53: Spezifische Cortex-A53 Optimierung
# ═══════════════════════════════════════════════════════════
CXXFLAGS += -fno-math-errno
CXXFLAGS += -fno-trapping-math
CXXFLAGS += -ffast-math
CXXFLAGS += -mcpu=cortex-a53

# ═══════════════════════════════════════════════════════════
# NEON OPTIMIZATION
# ═══════════════════════════════════════════════════════════
# NEON für Audio-Engine aktivieren (Pi 3/4/5)
# ═══════════════════════════════════════════════════════════
ifeq ($(RPI), $(filter $(RPI), 3 4 5))
DEFINE += -DARM_MATH_NEON
DEFINE += -DARM_MATH_NEON_EXPERIMENTAL
DEFINE += -DHAVE_NEON
DEFINE += -DFORCE_NEON
CXXFLAGS += -mfpu=neon-fp-armv8
endif
