# microDX21 — Yamaha DX21 Emulator

A real-time FM synthesizer emulator based on the **Nuked OPM** (YM2151/YM2164) core, faithfully modelling the Yamaha DX21's sound engine. Written in C++17, targets macOS for development and **Raspberry Pi** for deployment.

> ⚠️ **Active Development** — This project now includes full DX21 performance features: Single/Dual/Split play modes, Mono/Glide, Pitch Bend, Portamento, SysEx editing, and 32 RAM voice + performance memory banks.

---

## What It Does

### DX21 Voice Engine
- **128 DX21 factory patches** in ROM — full VCED parameter set (AR, D1R, D2R, RR, D1L, LS, RS, KVS, OUT, CRS, DET, AME, EBS, ALG, FB, LFO, PMS, AMS, key offset)
- **32 editable RAM voice slots** — modify via SysEx or load/save as JSON banks
- **YM2164 (OPP) mode** — uses `opm_flags_ym2164` for authentic TL-ramping and register-timing behaviour
- **Per-operator OPP attack-rate slowdown** — models the YM2164's AR=18–30 plateau (from [nuked-opp-xcent](https://github.com/Knives-On-Strings/nuked-opp-xcent))

### Play Modes & Performance
- **SINGLE** — 1 patch, 8-voice polyphony
- **DUAL** — 2 patches simultaneously (4+4 voices), with detune and A/B balance
- **SPLIT** — 2 patches split by keyboard zone (4+4 voices), programmable split point
- **MONO mode** — last-note-priority monophonic with legato support (per side in Split)
- **Portamento** — Full Time or Fingered (legato-only), 0–99 rate
- **Pitch Bend** — 14-bit MIDI, 0–12 semitone range, with Low/High/K-on modes
- **Master Tune** — ±1 semitone (-64…+63 cents)
- **32 Performance Memories** — store complete performance state (mode, patches, split, balance, PB, portamento, chorus, transpose)

### Effects & Output
- **Stereo Ensemble/Chorus** — 3 modulated delay lines with 2 LFOs (0.5 Hz + 3 Hz), 120° phase offsets, scaled from the GS1 reference
- **10 kHz DAC reconstruction filter** — 2nd-order Butterworth biquad emulating the analog filter at the YM2151/YM2164 DAC output

### MIDI & SysEx
- **Lock-free SPSC MIDI queue** — atomic ring buffer for thread-safe MIDI → audio handoff
- **Real-time SysEx parameter change** — DX21-compatible `F0 43 0n 12 pp vv F7` format edits any VCED parameter live
- **SysEx bulk import/export** — DX21 32-voice VCED dump compatibility
- **Breath Controller** (CC#2) — pitch bias, amplitude, EG bias mapping
- **Modulation Wheel** (CC#1) → LFO PMD
- **Sustain Pedal** (CC#64)

---

## Architecture

```
MIDI Input ──► SPSC Ring Buffer ──► processMidiBuffer() ──► writeReg/clockChip
                   (512 events)          (audio thread)       │
                                                              ▼
                                                     Pending Audio Buffer
                                                      (32768 cycles, carried over)
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

### Voice Management

| Mode | Side A | Side B | Patches | Notes |
|------|--------|--------|---------|-------|
| **Single** | Voices 0–7 | — | Patch A | 8-voice polyphony |
| **Dual** | Voices 0–3 | Voices 4–7 | Patch A + B | Simultaneous layers, A/B balance |
| **Split** | Voices 0–3 | Voices 4–7 | Patch A + B | Left/right of split point |

In **MONO** mode, each side is limited to 1 voice (last-note-priority). In Split mode, this allows one polyphonic and one monophonic side (e.g., bass + lead).

### Key Design Decisions

| Decision | Rationale |
|---|---|
| **applyPatch at program-change time** | Only per-note writes are TL (4×), KC, KF, KeyOn (3×) = 7 writes ≈ 280 cycles. Reduces audio loss per noteOn by ~75% vs 26-register applyPatch |
| **TL ramp-in on noteOn** | Before KeyOn, all 4 TL registers set to 0xFF (max attenuation + OPP ramp). Target TL then transitions smoothly from silence (~2 ms fade-in), masking phase-reset click |
| **Voice-steal smooth fade** | KeyOff + TL=max + RR=15 for stolen voices. OPP ramp ensures no audible click when stealing |
| **Pending-audio carry-over** | Unconsumed OPM cycles from MIDI processing are preserved across `processBlock()` calls via `memmove`, preventing audio gaps for small audio buffers |
| **LFO phases in radians** | `sinf()` requires radians; 0…1 range only covered ~57° of the sine wave, making chorus barely audible |
| **Pitch Bend via KC/KF** | YM2151 has no native pitch bend. KC/KF are updated in real-time for all active voices with sub-semitone resolution |
| **Portamento on audio thread** | `updatePortamento()` runs in `processBlock()`, interpolating pitch per-sample and writing KC/KF. Smooth, glitch-free glides |

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
Note:    C#   D   D#    E    F    F#   G    G#  A    A#    B    C
```

C sits at the **top** of each chip octave (nibble 14), so the octave boundary falls between C and C#. This is handled by computing `octave = (note - 1) / 12 - 1`, which correctly maps MIDI 60 → KC 0x3E (C4), MIDI 69 → KC 0x4A (A4 = 440 Hz).

---

## Performance Parameters

All performance parameters are stored in 32 **Performance Memories** and can be saved/loaded as JSON:

| Parameter | Range | Description |
|-----------|-------|-------------|
| Play Mode | Single/Dual/Split | Voice allocation strategy |
| Voice A / Voice B | 0–127 | Patch indices |
| Split Point | 0–127 | MIDI note for Split mode |
| A/B Balance | 0–99 | 50 = center, lower = quieter A, higher = quieter B |
| Pitch Bend Range | 0–12 | Semitones |
| PB Mode | All/Low/High/K-on | Which note receives pitch bend |
| Portamento Mode | Off/Full/Fingered | Glide behaviour |
| Portamento Rate | 0–99 | Glide speed |
| Modulation Sensitivity | 0–99 | Mod wheel depth scaling |
| Breath Pitch/Amplitude/EG | 0–99 | Breath controller mappings |
| Chorus | On/Off | Ensemble effect |
| Transpose | -24…+24 | Global semitone shift |
| Key Shift | -24…+24 | Instant transpose (like DX21's KEY SHIFT button) |

---

## SysEx Real-Time Parameter Change

The emulator supports DX21-compatible SysEx parameter changes for live editing:

### Parameter Change
```
F0 43 0n 12 pp vv F7
```
- `n` = device number (0–15, ignored)
- `12` = DX21 model ID
- `pp` = parameter number (0–75, VCED byte index)
- `vv` = value (0–127)

**Examples:**
- `F0 43 00 12 00 05 F7` → Algorithm = 5 for edit voice
- `F0 43 00 12 0A 1F F7` → OP1 AR = 31
- `F0 43 00 12 48 50 F7` → Name char 'P' (ASCII 0x50)

### Bulk Dump (32 Voices)
```
F0 43 0n 09 bb cc ...data... ss F7
```
- Imported into the 32 RAM voice slots via `CDX21Memory::importSysex()`
- Exported via `CDX21Memory::exportSysex()`

### Edit Voice

Use `setSysexEditVoice(0..7)` to select which voice slot receives parameter changes. Default: voice 0.

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
./standalone
```

### Controls (Keyboard)

| Key | Action |
|---|---|
| `s` / `d` / `p` | Play Mode: Single / Dual / Split |
| `m` | Toggle Mono |
| `+` / `-` | Patch A next / prev |
| `a` / `b` | Patch B next / prev |
| `[` / `]` | Split Point -1 / +1 |
| `<` / `>` | Balance -1 / +1 |
| `l` / `h` / `k` / `0` | PB Mode: Low / High / K-on / All |
| `r` | Pitch Bend Range cycle (0–12) |
| `o` | Portamento Mode cycle (Off/Full/Fingered) |
| `O` / `P` | Portamento Rate +5 / -5 |
| `t` / `T` | Master Tune -1 / +1 cent |
| `e` | Toggle ensemble/chorus |
| `0`–`9` | Apply Performance Memory 0–9 |
| `S` | Save RAM bank to `ram_bank/` |
| `L` | Load RAM bank from `ram_bank/` |
| `?` / `/` | Print status |
| `q` | Quit |

### MIDI Input

- **Note On/Off** — polyphonic, velocity-sensitive
- **CC#1** — Modulation Wheel → LFO PMD
- **CC#2** — Breath Controller → pitch/amplitude/EG bias
- **CC#64** — Sustain Pedal
- **Pitch Bend** — 14-bit, range configurable
- **Program Change** — select patch A
- **SysEx** — real-time parameter change or bulk dump

---

## API

```cpp
#include "opmemu.h"
#include "io/std_filesystem.h"  // or fatfs_filesystem.h for embedded

// Construction with IFileSystem for load/save
StdFileSystem fs;
COPMEmu emu(&fs);
emu.Initialize();
emu.initRamFromRom();  // copy ROM patches into RAM slots

// Performance setup
emu.setPlayMode(COPMEmu::Dual);
emu.setPatchA(0);    // ROM patch 0
emu.setPatchB(42);   // ROM patch 42
emu.setBalance(50);  // center
emu.setMono(false);

// Real-time parameters
emu.setPitchBendRange(2);
emu.setPortamentoMode(COPMEmu::PortaFullTime);
emu.setPortamentoRate(30);
emu.setMasterTune(0);

// Select edit voice for SysEx
emu.setSysexEditVoice(0);

// Audio callback — call from your audio thread
emu.processBlock(outputL, outputR, numSamples);

// MIDI — call from your MIDI thread (lock-free SPSC queue)
emu.processMidi(midiData, midiLength);

// Memory management
emu.loadRamBank("ram_bank");
emu.saveRamBank("ram_bank");
emu.loadPerformanceBank("performances.json");
emu.savePerformanceBank("performances.json");

// Apply a performance memory
emu.applyPerformance(3);  // load performance slot 3

// Query
int numPrograms = emu.getNumPrograms();           // 128
const char* name = emu.getProgramName(index);       // "Deep Grand"
bool ensemble = emu.getEnsembleOn();
```

All public methods are safe to call from any thread — the MIDI queue, program-change atomics, and SysEx dirty-flags handle the thread boundary internally.

---

## File Structure

```
src/opm/
├── opm.c                    Nuked OPM emulation (+ attack-skip patch, **phase-reset disabled for click-free KeyOn**)
├── opm.h                    Nuked OPM header (+ opp_atk_skip fields, SetAttackSkip API)
├── opmemu.cpp               DX21 emulator engine (voice management, effects, MIDI)
├── opmemu.h                 Public API, filters, delays, MIDI queue, performance state
├── patches.h                128 DX21 factory patches (full VCED parameter set)
├── memory/
│   ├── dx21_memory.h        CDX21Memory: 32 RAM slots + 32 performances + SysEx
│   └── dx21_memory.cpp      JSON persistence, SysEx VCED encode/decode
└── io/
    ├── ifilesystem.h        IFileSystem interface (read/write/list)
    ├── std_filesystem.h     std::filesystem implementation (PC/macOS)
    └── fatfs_filesystem.h   FatFS implementation (embedded / Circle)

test/
└── standalone.cpp           SDL2 + PortMidi interactive test application
```

---

## Core Modifications

### Nuked OPM Click Fix (Phase-Reset)
The original Nuked OPM core resets operator phase to 0 on every KeyOn (`pg_phase[slot] = 0`). On the real YM2151/YM2164, this causes a transient click because `logsinrom[0] = 0x859` (maximum amplitude). In a polyphonic context with fast note transitions (glissando, piano staccato), this produces audible crackling.

**Fix**: In `OPM_PhaseGenerate()`, the phase-reset block is commented out:
```c
if (chip->pg_reset_latch[slot] || chip->mode_test[3])
{
    // Phase reset disabled to prevent KeyOn clicks
    // chip->pg_phase[slot] = 0;
}
```
The phase now free-runs continuously. The OPP TL-ramp (bit 7 of register 0x60) provides smooth fade-in/out transitions. This is the definitive fix — previous attempts (TL-ramp, DC blocker, staggered KeyOns, per-voice soft-attack envelope, chip-internal mute counter) did not fully solve the problem.

### OPP Attack-Rate Skip
Per `nuked-opp-xcent`, the YM2164 has a plateau in AR=18–30 where attack times are ~105–115 ms (between OPM AR=12 and AR=13). This is modelled by skipping every Nth attack increment tick via `OPM_SetAttackSkip()`.

### DAC Anti-Clipping (mix_div)
The YM2151/YM2164 DAC has a limited dynamic range (≈±32 K). With 8 active voices, the internal `mix[]` accumulator overflows the DAC's floating-point mantissa, causing hard digital clipping *inside* the DAC. Reducing the output gain (`kScale`) doesn't help — it's applied after clipping, making the signal quieter but still distorted.

**Fix**: A `mix_div` field (2 bits) was added to the `opm_t` struct. The per-operator contribution to `mix[]` is right-shifted by `mix_div` bits *before* DAC serialization, gaining 6 dB of headroom per shift. The output gain `kScale` compensates so the perceived level stays the same.

- `mix_div = 2` (fixed, always active) — 12 dB headroom, prevents clipping for up to 8 voices at full output
- `kScale = 1/32768` — compensates for the /4 shift in the DAC path
- Quantization noise from the 2-bit truncation: ≈−120 dB, far below the DAC's own ≈−78 dB noise floor

A dynamic `mix_div` (0/1/2 depending on voice count) was tested first but caused audible gain jumps when voices were added/removed. The fixed `mix_div = 2` avoids this entirely.

### Voice Stealing & Muting
- `freeVoice()` now writes `TL=0xFF` (ramp mute) instead of `0x7F` (instant mute) for softer voice release.
- Stolen voices get `KeyOff` + `TL=0xFF` (ramp) + `RR=15` (fast release) instead of instant cut.
- In `setupVoice()`, operators are ramp-muted before KeyOn, then faded in via OPP TL-ramp.

### Output Gain
`setMasterGain(float)` / `getMasterGain()` provides a global output gain multiplier (0.0–8.0, default 3.0) applied after the reconstruction filter. The default of 3.0 (+9.5 dB) compensates for the internal headroom scaling — typical DX21 patches land at around −15 dBFS for a single voice and −6 dBFS for 4–8 voices.

---

## Acknowledgements

- **Nuked OPM** by Nuke.YKT — the YM2151/YM2164 emulation core, based on die analysis by gtr3qq (siliconpr0n.org). Licensed LGPL-2.1.
- **nuked-opp-xcent** by Knives On Strings — the OPP attack-rate skip mechanism for YM2164 AR=18–30 plateau modelling. Licensed LGPL-2.1.
- **picoX21H** — parallel hardware DX21 project whose voice-program architecture informed the applyPatch-at-program-change design.

---

## License

The DX21 emulator code (`opmemu.cpp`, `opmemu.h`, `patches.h`, `memory/`, `io/`, `standalone.cpp`) is released under the **MIT License**.

The Nuked OPM core (`opm.c`, `opm.h`) is licensed under the **GNU LGPL-2.1-or-later**. Any binary that links against it must comply with the LGPL relink right — either link dynamically, or provide object files for relinking.

The nuked-opp-xcent code is licensed under the **GNU LGPL-2.1-or-later**.
