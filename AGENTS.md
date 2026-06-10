# AGENTS.md — microDX21 Architecture & Guidelines

## On Session Start
Ich arbeite an einem Emulator eines Yamaha DX21 Synthesizers. Beachte strikt die Echtzeit-Garantien. Deployment läuft auf Raspberry Pi 3/4/5 mit Circle Bare Metal, Develop läuft auf macOS (M4 Max). Das System hat aktuell zwei Build-Pfade: einen für `test/standalone` (SDL2/PortMidi, läuft auf macOS) und einen Cross-Compile für die Pi (`out/kernel_rpi{3,4,5}.img`).

## Projekt-Identität
- **Was**: Yamaha DX21 Emulator basierend auf Nuked-OPM (YM2151/YM2164 kompatibel).
- **Synth**: 4-Operator FM, 128 Factory-Patches, 32 RAM-Voices, 32 Performance-Memories, 3 Play-Modi (Single/Dual/Split), Mono/Glide, Pitch-Bend, Portamento, 3-Line Ensemble, DX21-kompatibler SysEx.
- **Basis**: C++17
- **Host**: macOS (M4 Max) für Dev, cross-compile aarch64-none-elf- für Bare-Metal Raspberry Pi.
- **Display**: 128×32 SSD1305/SH1106 OLED + KY-040 Rotary Encoder (direkter Framebuffer-Zugriff, kein GUI-Framework).

## Architektur-Anker (WICHTIG)

1. **Audio-Pfad**: `Synth Engine -> DAC -> Biquad LPF -> Ensemble -> Audio Out`. Niemals Logik hinter den DAC hängen.
2. **Echtzeit-Garantie**: Audio-Code muss deterministisch sein.
   - KEINE `std::vector` Resizing oder `std::string` Operationen im Audio-Callback (`processBlock()`).
   - KEIN `new`/`delete` nach `Initialize()`.
   - Display-Thread (`CDX21Display::Render()`) ist von Audio physikalisch getrennt (anderer Core), darf aber auch nicht blockieren.
   - Multi-Core-Setup via `ARM_ALLOW_MULTI_CORE` auf Pi 3/4/5: Core 1 macht Audio, Core 2 macht Display+Encoder, Core 3 macht Deferred Work (SysEx, Preset-Load).
   - **Boot-Splash** (`SetSplash(true)` + `CTimer::SimpleMsDelay(2000)` in `CKernel::Initialize()`) ist die einzige Stelle, an der im Init-Flow direkt gewartet wird. Akzeptabel weil zu dem Zeitpunkt kein Audio-Thread laeuft; ein Splash im `RunCore2()` waere NICHT akzeptabel.
3. **Multi-Platform**: Code läuft auf Pi bare-metal UND macOS standalone. Pi-Pfade via `#ifdef ARM_ALLOW_MULTI_CORE` / `#if RASPPI >= 4` etc. absichern.

## Code-Konventionen
- **C++ Standard**: Modernes C++ (C++17).
- **Naming**:
  - Klassen: `C` Prefix (z.B. `CDX21Display`, `COPMEmu`).
  - Member: `m_` Prefix für eigene Felder, `m_p` für Pointer (z.B. `m_pDX21Display`).
  - Namespaces: `DX21UI` für UI-Strings, `DX21UI7Seg` für Font-Glyphs.
- **Memory**: Samples im RAM, achte auf Pointer-Arithmetik und Cache-Alignment (`CACHE_ALIGN` macro).
- **Header-Includes**: `extern "C"` nur für Circle-API, sonst C++.
- **Constexpr**: Wo möglich, static-storage Daten als `constexpr` (z.B. `kFont[]` in `dx21_ui_7seg.h`).

## Bekannte Komponenten & Integrationen

- **Audio-Backends**: PWM, I2S (PCM5102A), USB-Gadget (Pi 3) — siehe `src/audio/`.
- **MIDI**: USB Host (DIN-MIDI-Adapter), USB Gadget (Pi als MIDI-Device), Serial MIDI (GPIO UART) — siehe `src/midi/`.
- **Display**: 128×32 SSD1305/SH1106 OLED, libdisplay2 `CSSD1305SPIDisplay` — `src/display/display_dx21.cpp`.
- **Encoder**: KY-040 via Circle-sensor-addon `CKY040` (ISR, debounce, click/double-click/hold) — `src/display/dx21_input.cpp`.
- **Filesystem**: `IFileSystem` Interface, FatFS-Implementierung für SD-Karte (Presets, Performances, SysEx-Bulk).

## Build-System

- **macOS standalone** (Dev-Loop): `cd build && cmake .. && make && ./standalone` — keine Cross-Toolchain nötig.
- **Pi Cross-Compile** (Deploy): `RPI=3 ./build.sh` oder `./build-mac.sh --rpi 3`. Erwartet ARM-Toolchain unter `/usr/local/arm-gnu-toolchain-14.3.rel1-<host>-aarch64-none-elf/bin/` (Pfad in `build-mac.sh:62` anpassen).
- **Wichtig**: `src/Makefile` zieht seine `CFLAGS_FOR_TARGET` aus `libs/circle-stdlib/Config.mk` (via `Rules.mk`). Wenn du manuell mit `make` baust, stelle sicher dass `./configure` in `libs/circle-stdlib/` bereits lief.

## UI-State-Machine

Der neue UI-Layer hat eine **6-Modi**-State-Machine, die in `CDX21Input::ApplyEvent()` definiert ist:

| Mode | Quelle (Original ROM) | UI-Verhalten |
|---|---|---|
| `kModePlay` | rtn_85 case 1 → lbl_EEB4 | Voice-Nummer + Name, Play-Mode-Label |
| `kModeEdit` | rtn_85 case 2 → lbl_EEF6 | 36 Parameter durchblättern, 7-Seg-Wert |
| `kModePerformance` | rtn_85 case 4 → lbl_EF38 | Voice A/B, Performance-Label |
| `kModeFunction` | rtn_85 case 5 → lbl_EEAE | 46 Function-Items, ON/OFF-Toggle |
| `kModeMemory` | rtn_85 case 6 → lbl_EEB1 | Tape-Dialoge |
| COMPARE | rtn_85 overlay | Voice-Buffer (BUFF) statt Edit-Voice |

Die Mode→Anzeige-Tabellen sind in `src/opm/dx21_ui_strings.h` extrahiert aus dem Original-ROM (siehe `src/opm/firmware/dx21_rom_v1_5.asm`).

## Spezifische Instruktionen für Qwen/Codex

- **Stelle klärende Fragen**, bis du dir zu 95 % sicher bist, dass du deine Aufgabe erfolgreich abschließen kannst.
- **Bei DSP-Code**: Optimiere auf ARM-NEON für Pi 3/4 (Cortex-A53), aber Code muss auch auf macOS (Apple Silicon) kompilieren.
- **Bei UI-Code**: Bediene dich aus `src/opm/dx21_ui_strings.h` und `src/opm/dx21_ui_7seg.h`. Nicht neu erfinden.
- **Wenn du `m_audioPaused` brauchst**: Das ist ein `std::atomic<bool>`, der die DMA-Output stumm schaltet während SD I/O läuft. Setzen vor preset-load, löschen nach.
- **Wenn du SysEx implementierst**: 2-Slot-Queue in `CMicroDX21`, max 64 KB pro Slot. ProcessDeferredSysEx() auf Core 3 / deferred thread.
- **Wenn du an `kernel.cpp` arbeitest**: Bedenke die Multi-Core-Aufteilung. Audio darf nicht in `RunCore2` (Display) oder `RunCore3` (Deferred) laufen.

## Build-Verifikation (Latest)

```
$ RPI=3 ./build.sh
...
LD    kernel8.elf
DUMP  kernel8.lst
COPY  kernel8.img
813856 bytes

$ file out/kernel_rpi3.img
out/kernel_rpi3.img: data   # gültiges Pi 3 / Zero 2 W 64-bit Image
```

## Wichtige Referenzen

- **Original DX21 ROM V1.5 (reverse-engineered)**: `src/opm/firmware/dx21_rom_v1_5.asm` (14 607 Zeilen M6803 Assembler). Daraus extrahiert: alle LCD-Strings, die Mode→Display-Handler (`rtn_85`/`rtn_184`), das LCD-Driver-Subsystem (`lcd_init`/`lcd_rtn_21`).
- **Nuked OPM**: `src/opm/opm.c` (LGPL-2.1). Cycle-accurate YM2151-Emulation mit unseren Modifikationen: phase-reset disabled, mix_div für Headroom, opp_atk_skip für AR-Plateau.
- **Nuked-OPP-xcent**: AR=18–30 Plateau-Logik. Siehe `OPM_SetAttackSkip()` in `opm.c`.
- **MiniDexed** (https://github.com/synthfrumi/MiniDexed): Vorbild für die Multi-Core-Audio-Architektur auf Pi 3/4.
- **picoX21H** (https://github.com/synthfrumi/picoX21H): Vorbild für die DX21-Patch-Architektur (applyPatch-at-program-change).

## USB architecture
- The synth Pi is USB-Host-only. USB-MIDI Gadget is provided by an
  external RP2350 "Comms" processor running pico-midi-adapter
  firmware, which bridges USB-MIDI to UART RX/TX on GPIO 14/15.
- This is reflected in `CKernel::Initialize()` (always-USB-Host path,
  no Gadget conditional), `CMicroDX21::InitMidi()` (always allocates
  the full USB-MIDI device array), and the absence of `CConfig::*USB*`
  / `CMicroDX21::IsUSBDeviceMode()` methods.
- The RPi-4 xHCI fallback for the USB-A port is still in place via
  `mUSBHCI` so a DIN-MIDI adapter plugged into the USB-A port still
  works for development.
- Later, when the Comms Pi is wired up, the in-kernel MIDI path
  stays the same — we just feed it from the serial-MIDI ring buffer
  instead of from a directly-attached DIN adapter.

## UI action surface (encoder + click)
- `CDX21Display::SelectParam(±1)` — pure UI cursor move through the per-mode list. Wraps. No synth write.
- `CDX21Display::AdjustValue(±1)` — writes through `COPMEmuAdapter::setParameter()`:
  - PLAY / PERFORMANCE → `kParamInstrument` (program change)
  - EDIT → `ResolveEditParam(kEditToAdapter[m_ParamIdx], m_EditOp)` (raw VCED byte via `writeVcedGlobal/Operator`; per-op entries target the operator selected by `m_EditOp`)
  - FUNCTION → `kFunctionToAdapter[m_ParamIdx]`
  - MEMORY → no-op (tape dialogs are non-numeric)
  Returns the new value (or -1 for un-bound entries).
- **Triple-click in EDIT mode** cycles `m_EditOp` (OP1 → OP2 → OP3 → OP4). Per-operator EDIT entries (EG, OUT, FREQ, DET, RS, LS, EBS, KVS, AME) follow the selected operator; the EDIT title row shows `OPn`. The OP strides in the `DX21ParamIndex` enum (5 core / 8 extended per op) are guarded by `static_assert`s in `opmemuadapter.h`.
- **SysEx out**: the kernel's `ForwardMIDI` is registered as SysEx-out callback on adapter AND engine. The engine answers Yamaha dump requests (`F0 43 2n 03/09 F7`, gated on FUNCTION #5 "Midi Sy Info"); FUNCTION #41 "Midi Transmit ?" triggers the 32-voice bulk via `TriggerBulkTransmit()`. Single-voice dumps round-trip through `CDX21Memory::exportVoiceSysex/importVoiceSysex` (76-byte VCED, edit slot = `m_sysexEditVoice`, Memory Protect enforced).
- `CDX21Input::m_bBrowse` — true (default) means rotation navigates the list; false means rotation edits the current param's value. Toggled by the first tick of `EventSwitchHold`. The second hold-tick (≈2 s) still toggles MEMORY PROTECT. Initialise the constructor with `m_bBrowse(true)` so a freshly-cycled mode starts in browse.
- Both `SelectParam` and `AdjustValue` are called from the CKY040 ISR dispatch path; they only touch adapter get/set (which are safe to call from the main thread / display side) and update display members. Never call them from the audio callback.
