# microDX21 — Yamaha DX21 Emulator (Proof of Concept)

A real-time FM synthesizer emulator based on the **Nuked OPM** (YM2151/YM2164) core, faithfully modelling the Yamaha DX21's sound engine. Written in C++17, targets macOS for development and **Raspberry Pi** for deployment.

> ⚠️ **Proof of Concept** — This project demonstrates that the DX21's voice architecture can be accurately emulated using Nuked-OPM in OPP (YM2164) mode with proper VCED-to-register conversion. It is not a full DX21 emulation (no keyboard, effects chain, or UI beyond the basic standalone test app).

---

## What It Does

- **128 DX21 factory patches** — full VCED parameter set (AR, D1R, D2R, RR, D1L, LS, RS, KVS, OUT, CRS, DET, AME, EBS, ALG, FB, LFO, PMS, AMS, key offset)
- **YM2164 (OPP) mode** — uses `opm_flags_ym2164` for authentic TL-ramping and register-timing behaviour
- **8-voice polyphony** with voice-stealing (oldest-first) and smooth fade-out via OPP TL-ramp
- **DX21 → YM2151 register conversion** — correct mapping of all VCED parameters to chip registers, including:
  - DET→DT1 sign mapping (DX21's 0=−3…6=+3 → YM2151 DT1 nibbles)
  - CRS→MUL frequency ratio conversion
  - D1L inversion (DX21: higher = more sustain → YM2151: lower = more sustain)
  - KC note-code mapping using valid YM2151 nibbles (skips 3, 7, 11, 15)
- **OPP Attack-Rate Slowdown** — per-slot AR skip mechanism (from [nuked-opp-xcent](https://github.com/Knives-On-Strings/nuked-opp-xcent)) models the YM2164's AR=18–30 plateau between OPM AR=12 (140 ms) and AR=13 (95 ms)
- **Stereo Ensemble/Chorus** — 3 modulated delay lines with 2 LFOs (0.5 Hz + 3 Hz), 120° phase offsets, scaled from the GS1 reference implementation
- **10 kHz DAC reconstruction filter** — 2nd-order Butterworth biquad emulating the analog filter at the YM2151/YM2164 DAC output
- **Lock-free SPSC MIDI queue** — atomic ring buffer for thread-safe MIDI → audio handoff
- **Pending-audio pipeline** — OPM clock cycles from register writes are buffered with carry-over between audio blocks, preventing audio loss during noteOn bursts and program changes
- **Program change via atomic** — thread-safe patch switching without race conditions on chip state

---

## Architecture

```
MIDI Input ──► SPSC Ring Buffer ──► processMidiBuffer() ──► writeReg/clockChip
                   (256 events)          (audio thread)       │
                                                              ▼
                                                     Pending Audio Buffer
                                                      (16384 cycles, carried over)
                                                              │
                                                              ▼
                              OPM Chip (Nuked, YM2164/OPP mode) ──► DAC stream
                                                              │
                                                              ▼
                                                    Biquad LPF (10 kHz)
                                                              │
                                                     ┌────────┴────────┐
                                                     ▼                 ▼
                                              Delay A,B,C      Delay A,B,C
                                              (LFO-modulated)  (LFO-modulated)
                                                     │                 │
                                                     ▼                 ▼
                                              Stereo Ensemble    Direct Out
                                                     │                 │
                                                     └────────┬────────┘
                                                              ▼
                                                       Audio Output
                                                      (48 kHz, stereo float)
```

### Key Design Decisions

| Decision | Rationale |
|---|---|
| **applyPatch at program-change time** | Only per-note writes are TL (4×), KC, KF, KeyOn (3×) = 7 writes ≈ 280 cycles. Reduces audio loss per noteOn by ~75% vs 26-register applyPatch |
| **TL ramp-in on noteOn** | Before KeyOn, all 4 TL registers set to 0xFF (max attenuation + OPP ramp). Target TL then transitions smoothly from silence (~2 ms fade-in), masking phase-reset click |
| **Voice-steal smooth fade** | KeyOff + TL=max + RR=15 for stolen voices. OPP ramp ensures no audible click when stealing |
| **Pending-audio carry-over** | Unconsumed OPM cycles from MIDI processing are preserved across `processBlock()` calls via `memmove`, preventing audio gaps for small audio buffers |
| **LFO phases in radians** | `sinf()` requires radians; 0…1 range only covered ~57° of the sine wave, making chorus barely audible |

---

## VCED → YM2151 Register Mapping

The DX21's VCED parameters don't map 1:1 to YM2151 registers. Key conversions:

| DX21 Parameter | YM2151 Register | Conversion |
|---|---|---|
| DET (0–6: −3…+3) | DT1[6:4] | `{7,6,5,0,1,2,3}[det]` — negative half maps to DT1 5–7 |
| CRS (0–63) | MUL[3:0] | Lookup table: 0→0, 1→1, 2→2, 3→3, 4→4, 5→5, 6→6, 7→8, … 63→15 |
| OUT (0–99) | TL[6:0] | `tl = (99 - out) * 127 / 99` |
| D1L (0–15) | D1L[7:4] | `15 - d1l` — DX21 inverts the direction |
| LS (0–99) | TL offset | Per-note: `atten = ls * (note - 60) * 127 / (99 * 48)` |
| KVS (0–7) | TL offset | Per-note: velocity-sensitive attenuation |
| ALG (0–7) | CON[2:0] | Direct mapping |
| Key Offset | KC octave shift | Transposed note used for KC, original note for voice lookup |

### KC Note Code Mapping

The YM2151 uses only 12 of 16 possible 4-bit note codes — values 3, 7, 11, 15 produce non-chromatic frequencies. The valid nibbles per octave:

```
Nibble:  0    1    2    4    5    6    8    9   10   12   13   14
Note:    C#   D    D#   E    F    F#   G    G#  A    A#   B    C
```

C sits at the **top** of each chip octave (nibble 14), so the octave boundary falls between C and C#. This is handled by computing `octave = (note - 1) / 12 - 1`, which correctly maps MIDI 60 → KC 0x3E (C4), MIDI 69 → KC 0x4A (A4 = 440 Hz).

---

## Ensemble / Chorus Effect

Based on the GS1 ensemble implementation, adapted for 48 kHz:

- **3 modulated delay lines** with 120° phase offsets for stereo width
- **2 LFOs**: 0.5 Hz (slow vibrato, 85% depth) + 3.0 Hz (fast shimmer, 15% depth)
- **Base delay**: 250 samples (~5.2 ms) — scaled from GS1 reference at 34.6 kHz
- **Modulation depth**: 85 samples (~1.8 ms) — subtle shimmer without pitch wobble
- **Stereo mix**: `L = dry/2 + A/2 − B·0.3`, `R = dry/2 + C/2 − B·0.3`

---

## Build & Run

### Prerequisites

- CMake 3.10+
- C++17 compiler (Clang/GCC)
- SDL2
- PortMidi

### Build

```bash
cd microDX21
mkdir -p build && cd build
cmake ..
make
```

### Run

```bash
./opp_standalone
```

### Controls

| Key | Action |
|---|---|
| `+` | Next patch |
| `-` | Previous patch |
| `e` | Toggle ensemble/chorus |
| `q` | Quit |
| MIDI In | Note On/Off, CC#64 (sustain), Program Change |

---

## API

```cpp
#include "opmemu.h"

COPMEmu emu;
emu.Initialize();
emu.setCurrentProgram(0);
emu.setEnsembleOn(true);

// Audio callback — call from your audio thread
emu.processBlock(outputL, outputR, numSamples);

// MIDI — call from your MIDI thread (lock-free SPSC queue)
emu.processMidi(midiData, midiLength);

// Query
int numPrograms = emu.getNumPrograms();           // 128
const char* name = emu.getProgramName(index);       // "Deep Grand"
bool ensemble = emu.getEnsembleOn();
```

All public methods are safe to call from any thread — the MIDI queue and program-change atomics handle the thread boundary internally.

---

## File Structure

```
src/opm/
├── opm.c           Nuked OPM emulation (unmodified, + attack-skip patch)
├── opm.h           Nuked OPM header (+ opp_atk_skip fields, SetAttackSkip API)
├── opmemu.cpp      DX21 emulator (VCED conversion, voice management, effects)
├── opmemu.h        Public API, BiquadFilter, EnsembleDelayLine, Voice, ring buffer
└── patches.h       128 DX21 factory patches (full VCED parameter set)

test/
└── standalone.cpp  SDL2 + PortMidi test application
```

---

## Acknowledgements

- **Nuked OPM** by Nuke.YKT — the YM2151/YM2164 emulation core, based on die analysis by gtr3qq (siliconpr0n.org). Licensed LGPL-2.1.
- **nuked-opp-xcent** by Knives On Strings — the OPP attack-rate skip mechanism for YM2164 AR=18–30 plateau modelling. Licensed LGPL-2.1.
- **picoX21H** — parallel hardware DX21 project whose voice-program architecture informed the applyPatch-at-program-change design.

---

## License

The DX21 emulator code (opmemu.cpp, opmemu.h, patches.h, standalone.cpp) is released under the **MIT License**.

The Nuked OPM core (opm.c, opm.h) is licensed under the **GNU LGPL-2.1-or-later**. Any binary that links against it must comply with the LGPL relink right — either link dynamically, or provide object files for relinking.

The nuked-opp-xcent code (src/opm_diff/) is licensed under the **GNU LGPL-2.1-or-later**.
