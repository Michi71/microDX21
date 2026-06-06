# Changelog

All notable changes to microDX21, in reverse chronological order.

## [0.1.0] — 2026-06-06 — Initial release

First public release. The project boots to a working Yamaha DX21 emulator on Raspberry Pi 3/4/5 (bare-metal Circle stdlib) and on macOS/PC (SDL2 + PortMidi).

### Display & input
- **`src/opm/dx21_ui_strings.h`** (214 lines) — All LCD strings extracted from the original DX21 ROM V1.5 firmware (`src/opm/firmware/dx21_rom_v1_5.asm`): 36 EDIT-mode parameter labels, 46 FUNCTION-mode menu items, 6 PLAY labels, ON/OFF, note names, tape-dialog labels, MIDI status messages. 16-char padded for direct copy into a 2×16 framebuffer.
- **`src/opm/dx21_ui_7seg.h`** (327 lines) — 8×16 7-segment font for big value display. Hand-composed segment masks (a/b/c/d/e/f/g) for all 10 digits, 26 uppercase letters, and 21 symbols. Includes `FromAscii(char)` ASCII→glyph mapper.
- **`src/display/display_dx21.{h,cpp}`** (148 + 395 lines) — `CDX21Display` wraps `CSSD1305SPIDisplay` from `libdisplay2` and renders 5 modes (PLAY, EDIT, PERFORMANCE, FUNCTION, MEMORY) + COMPARE overlay. Layout mirrors the original DX21's HD44780 2×16 display, scaled to 128×32.
- **`src/display/dx21_input.{h,cpp}`** (70 + 169 lines) — `CDX21Input` wraps the Circle sensor-addon's `CKY040` (ISR-driven, with switch debounce, single/double/triple-click, hold detection). Maps:
  - rotate CW/CCW → next/prev parameter
  - single click → cycle mode (PLAY → EDIT → PERFORM → FUNCTION → MEMORY → PLAY)
  - double click → COMPARE toggle
  - long press → memory-protect toggle
- **`src/display.mk`, `src/sensor.mk`** — Minimal include-path files for libdisplay2 and the sensor addon.

### Build system
- **`src/Makefile`** actively consumes `CFLAGS_FOR_TARGET` / `CXXFLAGS_FOR_TARGET` from `Config.mk` (set by `./configure` in `libs/circle-stdlib`). This propagates `-DAARCH=64 -mcpu=cortex-a53 -D__circle__ -DRASPPI=N` to every translation unit.
- **`src/kernel.{h,cpp}`** rewritten as a clean multi-core `CKernel` with:
  - `m_pDX21Display` (CDX21Display, the OLED driver)
  - `m_pDX21Input` (CDX21Input, the KY-040 wrapper)
  - 4 cores dispatched: USB+MIDI on Core 0, audio on Core 1, display on Core 2, deferred work on Core 3
  - `RunCore2()` renders the OLED at ~30 Hz
- **Build verified**: `RPI=3 ./build.sh` produces `out/kernel_rpi3.img` (813 856 bytes).

### Fixed
- **`font6x8.h` linker error** — `font6x8.h` defines `constexpr u8 Font6x8[FONT_SIZE][8]`, which has internal linkage in C++17. `display_dx21.cpp` includes the header directly instead of `extern`-declaring it.
- **`#pragma GCC diagnostic ignored "-Wc99-designator"`** removed from `dx21_ui_7seg.h` (the warning doesn't exist in `aarch64-none-elf-gcc 14.3`).
- **`snprintf` truncation** warning: `char line[22]` → `char line[32]` in `display_dx21.cpp`.
- **`src/Makefile`** no longer has a dead `OPTIMIZE` flag; the actual cross-compile flags come from `CFLAGS_FOR_TARGET`.

### Source tree
```
src/
├── kernel.{h,cpp}             Bare-metal CKernel (multi-core launch, panic, core dispatch)
├── main.cpp                   C-style main() entry point
├── microdx21.{h,cpp}          CMicroDX21 — MIDI, presets, audio, SysEx
├── circle_stdlib_vk.h         StdlibApp base class (tty, log, file system, network)
├── common.h                   maplong/mapfloat/constrain helpers
├── audio/                     PWM, I2S, USB-gadget audio backends
├── midi/                      USB / Serial / PC-keyboard MIDI drivers
├── display/                   128×32 OLED + KY-040 encoder UI
│   ├── display_dx21.{h,cpp}   CDX21Display
│   ├── dx21_input.{h,cpp}     CDX21Input
│   └── displayconfig.h        DisplayConfig struct
├── opm/
│   ├── opm.c                  Nuked OPM (cycle-accurate YM2151/YM2164)
│   ├── opmemu.{h,cpp}         DX21 voice management, MIDI, effects
│   ├── patches.h              128 DX21 factory voices
│   ├── memory/                RAM voices, performance memories, JSON+SysEx
│   ├── io/                    IFileSystem interface + std/FatFS impls
│   ├── dx21_ui_strings.h      ROM-extracted UI strings
│   └── dx21_ui_7seg.h         7-seg font
├── system/                    Boot configuration (CConfig)
├── util/ringbuffer.h          Lock-free SPSC ringbuffer
├── opm/firmware/
│   └── dx21_rom_v1_5.asm      Reverse-engineered DX21 ROM (M6803) — reference
├── Synth_OPM.mk               Audio-engine CFLAGS
├── display.mk, sensor.mk      Addon include paths
├── Rules.mk                   Circle-stdlib LIBS+INCLUDES
└── Makefile
```

## [Earlier] — Synth engine stability, voice management, MIDI

These are the foundational changes that were iterated on before the public release. They're listed here for context, but the public release above already includes all of them.

### Nuked OPM click fix
Disabled phase-reset in `OPM_PhaseGenerate()`. The standard Nuked OPM code does `pg_phase[slot] = 0` on every KeyOn, which on the real YM2151/YM2164 causes a transient click (because `logsinrom[0] = 0x859`, maximum amplitude). The phase now free-runs continuously; the OPP TL-ramp bit-7 of register 0x60 provides the smooth fade-in. This is the definitive fix — earlier attempts (TL-ramp, DC blocker, staggered KeyOns, per-voice soft-attack envelope, chip-internal mute counter) did not fully solve the problem.

### OPP attack-rate skip
Ported from `nuked-opp-xcent`. The YM2164 has a plateau in AR=18–30 where attack times are ~105–115 ms (between OPM AR=12 and AR=13). Modelled by skipping every Nth attack increment tick via `OPM_SetAttackSkip()`.

### DAC anti-clipping (mix_div)
Added a 2-bit `mix_div` field to the `opm_t` struct. The per-operator contribution to `mix[]` is right-shifted by `mix_div` bits *before* DAC serialization, giving 12 dB of headroom with `mix_div=2` (fixed). The output gain `kScale = 1/32768` compensates so the perceived level stays the same. Quantization noise from the truncation: ≈−120 dB, far below the DAC's own ≈−78 dB noise floor.

A dynamic `mix_div` (0/1/2 depending on voice count) was tested first but caused audible gain jumps when voices were added/removed.

### Voice stealing
Stolen voices get `KeyOff` + `TL=0xFF` (ramp mute) + `RR=15` instead of instant cut. The OPP ramp ensures no audible click. In `setupVoice()`, operators are ramp-muted before KeyOn, then faded in via OPP TL-ramp.

### Real-time performance
- **applyPatch at program-change time** instead of every noteOn. Only per-note writes (TL × 4 + KC + KF + KeyOn × 3 = 7 writes ≈ 280 cycles per note). Reduces audio loss per noteOn by ~75% vs 26-register applyPatch.
- **Pending-audio carry-over**: unconsumed OPM cycles from MIDI processing are preserved across `processBlock()` calls via `memmove`, preventing audio gaps for small audio buffers.
- **LFO phases in radians** (not 0…1) — `sinf()` requires radians; 0…1 range only covers ~57° of the sine wave.
- **Pitch bend** via per-voice KC/KF real-time updates with sub-semitone resolution.
- **Portamento** via per-sample pitch interpolation in `processBlock()`. Smooth, glitch-free glides.

### 32 Performance memories + 32 RAM voice slots
- JSON persistence via the `IFileSystem` interface (`std::filesystem` for PC, FatFS for embedded).
- SysEx VCED bulk dump import/export (`F0 43 0n 09 bb cc …data… ss F7`).
- Real-time parameter change: `F0 43 0n 12 pp vv F7`.

### MIDI implementation
- **Lock-free SPSC ring buffer** between MIDI thread and audio ISR.
- **Channel filter** that lets channel-voice messages (notes, CC, PC, pitch bend) be filtered by `MidiChannel`, but always lets SysEx and system real-time (clock, start/stop/continue) pass.
- **Soft-thru** is unfiltered (useful for DAW setups).
- **SysEx** is queued in a 2-slot buffer (up to 64 KB each) and processed in `ProcessDeferredSysEx()` on the deferred-work thread.

### Multi-core deployment (Pi 3/4/5 with `ARM_ALLOW_MULTI_CORE`)
| Core | Role |
|---|---|
| 0 | USB PnP poll, MIDI, audio DMA-IRQ consumer |
| 1 | Audio generation (fills the lock-free ringbuffer) |
| 2 | OLED render (~30 Hz) + encoder event drain |
| 3 | Deferred work: SysEx, preset load, file I/O |

`m_audioPaused` atomic gates DMA output during heavy operations.
