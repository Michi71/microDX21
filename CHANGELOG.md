# Changelog

All notable changes to microDX21, in reverse chronological order.

## [Unreleased]

> The Removed/Added/Changed sub-sections immediately below describe
> the **0.1.0** public release delta. The Documentation, SD card,
> Synth engine, Memory Protect, Build, Fixed, and Test coverage
> sub-sections further down describe the **post-0.1.0** work.

### Added (post-0.1.0, manual-comparison pass II — UI workflows)
- **Audible COMPARE** (double-click in PLAY/EDIT/PERFORMANCE): the
  first edit after a program change snapshots the pre-edit voice;
  COMPARE makes `getPatch()` serve the snapshot so held and new notes
  sound the ORIGINAL voice while the edits stay in RAM. Refused with
  "NO EDITS YET" when nothing was edited (matches the real DX21);
  program change resets the compare context.
- **Edit transmit**: panel edits send real-time parameter changes
  (`F0 43 1n 12 pp vv F7`, real VCED numbering 0-92) when "Midi Sy
  Info" is ON; selecting a voice on the panel additionally transmits
  the 1-voice VCED dump (manual: transmission data 2-2-2). Incoming
  MIDI parameter changes are applied with `transmit=false` so they
  never echo back out.
- **MEMORY mode grew from 3 to 7 actions** (new two-picker stage for
  source+destination): Store Voice (edit voice → RAM slot 1-32,
  the panel STORE), ROM Grp→Bank (A11: ROM group 1-16 → RAM bank
  1-4), ROM Voice→Slot (A12: any of the 128 ROM voices, picker shows
  the voice name), Store Perf (live setup → performance slot 1-32).
  SD Save/Load now also persist `MICRODX21/performances.json`.
  All store paths honour Memory Protect (new result "MEM PROTECTED").
- **PERFORMANCE mode is operable**: value-rotation steps through the
  32 performance memories and applies them (`applyPerformance`, now
  incl. chorus); the display shows the performance name, play-mode
  tag (SI/DU/SP) and programmed voices A/B; empty slots render as
  "-- EMPTY --". `capturePerformance()` snapshots the live engine
  state for the MEMORY "Store Perf" action.
- **Voice-name editor** (FUNCTION #45 "Name :"): click opens the
  editor, rotation cycles the character under the underline cursor,
  click advances, the last click commits through
  `COPMEmu::setVoiceName()` (Memory Protect enforced, name bytes
  transmitted as VCED params 77-86).
- **Edit-slot fix**: `m_sysexEditVoice` was clamped to chip channels
  (0-7) and used as a register channel index — edits to RAM slots
  8-31 wrote the wrong patch AND the wrong registers. The edit slot
  now follows the current program (RAM 0-31; ROM programs are
  read-only until loaded), and `writeVcedGlobal/Operator` write the
  chip registers for every channel actually sounding the edited
  patch (side A/B aware) instead of channel == slot.
- `tests/test_voice_workflow.cpp` — 5 scenarios: edit transmit with
  real VCED numbers + Sy-Info gate, no-echo on MIDI edits, COMPARE
  snapshot/restore/reset, setVoiceName + protect, performance
  capture/apply round-trip.

### Added (post-0.1.0, manual-comparison pass — hardware SysEx, PEG, op switches, CC)
- **Hardware-DX21-compatible SysEx** (the previous formats were
  project-internal and did not match the owner's manual):
  - 1-voice VCED dump: format 0x03 with **93 data bytes** (manual
    table 5-2) including per-voice function data (poly/mono, PB
    range, porta, foot, MW/BC ranges, chorus), the 10-char voice
    name and the Pitch EG. Wire operator-block order OP4, OP2, OP3,
    OP1.
  - 32-voice bulk dump: format **0x04** with **4096 data bytes**
    (73-byte packed VMEM per voice + 55 zero pad, manual table 5-1).
    Standard DX21/DX27/DX100 `.syx` banks now import, and exported
    banks load into real hardware and editors.
  - Checksum: two's complement of the data sum (was: plain sum).
  - Dump requests: f=3 (voice) and f=4 (bulk) per the manual; f=9
    kept as legacy alias. The legacy project formats (0x09 bulk,
    76-byte VCED records) remain importable, receive-only.
  - Real-time parameter change (modelId 0x12) renumbered to the real
    VCED layout 0–92 + function param 93 (operator enable);
    `kSysexNumParams` 76 → 94. Param changes to function data
    (63–76) are mirrored into the live engine state.
  - `DX21_Patch` extended with the per-voice function fields, PEG
    and op-enable (appended after `name` so the 128 factory-patch
    initializers stay valid; legacy zero values are interpreted via
    sentinel accessors `dx21_effective_peg_level()` /
    `dx21_op_enabled()`). JSON persistence round-trips the new
    fields; old JSON files load with defaults.
- **Pitch EG (VCED 87-92, EDIT entries 22-27)**: PR1-3/PL1-3
  implemented firmware-side like the original (the YM2164 has no
  pitch envelope): per-voice stages PL3→PL1→PL2(sustain)→PL3 advanced
  once per audio block, folded into the KC/KF writes next to pitch
  bend, portamento and breath bias. The release stage keeps gliding
  while the OPM release rings out. Levels: 50 = center,
  ±0.96 st/unit (≈ ±4 octaves); rates: 99 = instant, full sweep
  ≈ 10 ms × 2^((99-r)/10). EDIT-mode entries 22-27 are now live
  (`kParamPEGR1..kParamPEGL3`).
- **Operator ON/OFF** (panel A1-A4 / SysEx function param 93):
  per-patch enable mask (TX81Z bit order b3..b0 = OP1..OP4); a
  disabled operator is muted via max attenuation in `applyTL()`.
  Live changes to the edited program re-apply TL to active voices.
- **MIDI CC reception completed** (manual 4-1-1/4-1-2): CC#5
  portamento time, CC#7 foot volume (was wrongly listening on CC#4),
  CC#65 portamento footswitch (gated by FUNCTION B5 "Foot Porta",
  new `kParamFootPorta` binding), CC#123 all notes off, CC#126/127
  mono/poly mode. **Foot Volume (B6) is now a control range 0-99**
  per the manual (gain = 1 - range/99 × (1 - cc7/127)), not an
  on/off switch.
- `tests/test_sysex_dump.cpp` extended to 13 scenarios: VCED-93 and
  VMEM-4096 round-trips, two's-complement checksum validation,
  Memory Protect, dump requests f=3/f=4, Sy-Info gating, VCED
  param-change numbering (incl. wire op order), operator enable,
  foot sustain, CC#5/CC#123, Pitch EG smoke test.

### Added (post-0.1.0, June 2026 gap-closing pass)
- **OP2–4 editing in EDIT mode**: triple-click on the encoder cycles
  the target operator (OP1 → OP2 → OP3 → OP4); the per-operator EDIT
  entries (EG AR/D1R/D1L/D2R/RR, OUT, FREQ, DET, RS, LS, EBS, KVS,
  AME) now write to the selected operator instead of always OP1.
  `ResolveEditParam()` maps the OP1 binding to the other ops via the
  fixed enum strides (5 core / 8 extended params per op), guarded by
  `static_assert`s in `opmemuadapter.h`. The EDIT title row shows
  `EDIT OPn` whenever the current entry is per-operator.
- **SysEx dump out (transmit)**:
  - `COPMEmu::setSysexOutCallback()` — the engine can now send SysEx;
    the adapter forwards the kernel's `ForwardMIDI` callback into it
    (this resolves the old "no SysEx out callback yet" TODO).
  - **Yamaha dump requests** (`F0 43 2n ff F7`) are answered when
    FUNCTION #5 "Midi Sy Info" is ON: `ff = 0x03` → 1-voice VCED dump
    of the current program, `ff = 0x09` → 32-voice bulk dump. Editors
    and librarians can now pull patches from the synth.
  - **1-voice VCED dump (modelId 0x03)**: new
    `CDX21Memory::exportVoiceSysex()` / `importVoiceSysex()` (76-byte
    VCED, same framing/checksum as the bulk dump). Incoming 1-voice
    dumps land in the edit slot (`m_sysexEditVoice`); Memory Protect
    is enforced. `COPMEmuAdapter::TriggerVoiceTransmit()` sends the
    current voice on demand.
  - `tests/test_sysex_dump.cpp` — 8 scenarios: voice round-trip,
    checksum rejection, Memory Protect, both dump requests, Sy-Info
    gating, edit-slot receive, Foot-Sustain gating.
- **FUNCTION-mode bindings completed** (`kFunctionToAdapter` now has
  22 live bindings, was 12): Dual Detune (#2), Midi Switch (#3),
  Midi Recv Ch (#6) + Omni (#7, same state: Omni = ch 0), Foot
  Volume (#30), Foot Sustain (#31), MW Pitch (#33), MW Amplitude
  (#34), BC Pitch (#35), Midi Trns Ch (#40). New engine state:
  - `m_midiSwitchOn` / `m_midiRecvCh` — `CMicroDX21::
    ShouldAcceptChannel()` now consults the engine (single source of
    truth; config and `setMidiSetting(kMidiChannel)` mirror into it).
    Midi Switch OFF mutes all channel-voice reception.
  - `m_footVolumeOn` / `m_footSustainOn` — CC#4 scales the stereo
    output (snaps back to full when disabled); CC#64 is ignored when
    Foot Sustain is OFF.
  - `m_breathPitch` (BC Pitch, 0..99) — breath now modulates the LFO
    PMD, summed with the mod wheel in the new `updateLFOModDepth()`.

### Fixed (post-0.1.0, June 2026 gap-closing pass)
- **YM2151 reg 0x19 PMD write**: the mod-wheel handler packed PMD and
  AMD into one byte (`((pmd & 0x7F) << 7) | amd`), which overflows
  uint8_t and silently drops the PMD value. PMD (bit 7 set) and AMD
  (bit 7 clear) are now written as two separate writes.
- **`CDX21Display::SetStatus()` dangling pointer**: the encoder ISR
  passed stack-local snprintf buffers whose lifetime ended before the
  display thread rendered them. SetStatus now copies into a fixed
  internal buffer (no heap).
- **Uninitialized engine members**: `m_mwValue`, `m_mwPitchRange`,
  `m_mwAmpRange`, `m_chInfoOn`, `m_sysexInfoOn`, `m_midiTransmitCh`,
  `m_dualDetune`, `m_bEditRecallValid` had neither constructor-init-
  list entries nor inline initializers. All engine state now has
  deterministic defaults (DX21 factory-style: MW Pitch 50, Ch/Sy
  Info ON).

### Removed
- **USB-MIDI Gadget mode**: dropped the in-kernel `CUSBMIDIGadget` branch that let the synth Pi appear as a USB-MIDI device to a host. The synth Pi is now USB-Host-only. USB-MIDI Gadget is handled by an external RP2350 "Comms" processor (pico-midi-adapter) that bridges USB-MIDI to UART RX/TX on GPIO 14/15. See `pico-midi-adapter/README` for the wiring.
  - `CKernel::m_pUSBGadget` member and constructor/destructor removed (`src/kernel.h`, `src/kernel.cpp`).
  - `CMicroDX21::IsUSBDeviceMode()` and `GetUSBGadgetPin()` removed (`src/microdx21.h`, `src/microdx21.cpp`).
  - `CConfig::IsUSBGadget()` / `GetUSBGadgetPin()` / `m_bUSBGadget` / `m_nUSBGadgetPin` removed (`src/system/config.h`, `src/system/config.cpp`).
  - `USBGadget=`, `USBGadgetPin=` keys dropped from `config/microdx21.ini`. The `[USB]` section now just documents the new architecture.
  - `libusbgadget.a` removed from `src/Rules.mk`.
  - `CMicroDX21::InitMidi()` always allocates the full set of host-side USB-MIDI devices (no more "single device for gadget" branch).
  - `RASPPI < 5` / `RASPPI == 4` conditional blocks in `CKernel::Initialize()` collapsed to one always-USB-Host path.
  - `build.sh` no longer passes `-o USB_GADGET_VENDOR_ID=0x2E8A` to Circle's `./configure`.
  - `README.md` roadmap item "Audio over USB Gadget" replaced with "USB-MIDI Gadget (removed)" explaining the Comms-processor architecture.

### Added
- **Safe shutdown (`CMicroDX21::Panic()` + `CKernel::PanicHandler()`)**:
  - `COPMEmu::allNotesOff()` writes `KeyOff` (reg 0x08) to all 8 OPM channels and resets per-voice bookkeeping (active, sustained, note, velocity, portamento, attack samples, keyOnDelay).
  - `COPMEmu::resetEngine()` = `allNotesOff()` + force every operator's TL to 127 (max attenuation) + clear the MIDI ring buffer (write=read=0) + clear the `m_sysexDirty[]` flags. After this the OPM outputs silence within one DMA buffer (~10 ms @ 48 kHz / 256 chunk).
  - `CMicroDX21::Panic()` calls `resetEngine()` on the synth and `CSoundBaseDevice::Cancel()` on the sound device. Cancel is the standard Circle API: the next boot calls `Start()` from the audio-thread entry point and resumes cleanly.
  - `CKernel::PanicHandler()` is now: `EnableIRQs` → `m_pMicroDX21->Panic()` → `m_pDX21Display->Off()` → `m_bRunning = false` → `wfi` forever. Brings the system to a safe, silent, OLED-off state on any unhandled exception so a power-cycle recovers cleanly.
  - Graceful Run() exit also calls `Panic()` + `Off()` before returning `ShutdownHalt`, so a future "user holds button combo to shut down" feature gets the same teardown for free.
- **Boot splash fade-in** (top-down, 4 pages over 1 s):
  - `CDX21Display::m_SplashProgress` (0..4) gates which pages of the splash banner are visible. Each step the kernel drives takes 250 ms, so the banner builds up as: page 0 (`*  YAMAHA  *`) at 0.25 s → +page 1 (7-seg `DX21`) at 0.5 s → +page 2 (`* SYNTHESIZER *`) at 0.75 s → +page 3 (`v0.1.0  INIT...`) at 1.0 s. 1 s hold at full banner, then `SetSplash(false)` to PLAY mode.
  - `CDX21Display::SetSplash(bool)` is now a real method (was inline) and resets `m_SplashProgress` on every entry, so a re-entry (e.g. after a panic) starts from the top instead of flashing the full banner.
  - `CDX21Display::SetSplashProgress(int n)` and `GetSplashProgress()` for external control. MarkDirty()s on change so the next 5 Hz refresh tick re-renders.
  - 128×32 OLED with 4 pages of 8 px → 4 unlock steps feels natural (one per page). Matches the original DX21's row-by-row LCD initialisation by the 6803 firmware.
- **Tape save/load UI** (3-stage MEMORY dialog): Save / Load / Verify the 32 RAM voices to/from SD card banks.
  - 3-stage state machine in `CDX21Display::m_MemoryStage`:
    - Stage 0 (pick action): rotation cycles Save / Load / Verify. Click advances to stage 1.
    - Stage 1 (confirm): rotation toggles YES / NO. Click with NO goes back to stage 0; click with YES advances to stage 2.
    - Stage 2 (pick group): rotation cycles 1..16. Click executes the action.
    - Stage 3 (result): shows OK / SAVE FAILED / LOAD FAILED / NO SD CARD / VERIFY MISMATCH in big 7-seg for 2 s, then auto-clears back to stage 0.
  - Bank layout: `SD:/MICRODX21/BANK_NN/voice_00.json` … `voice_31.json` for NN in 01..16. Uses the existing `CDX21Memory::saveRamBank/loadRamBank` interface.
  - New `COPMEmuAdapter::MemoryResult` enum + 3 methods: `saveRamBankToFile(int group)`, `loadRamBankFromFile(int group)`, `verifyRamBank(int group)`. Static `MemoryResultString(MemoryResult)` maps to a 16-char status line.
  - Encoder dispatch in `dx21_input.cpp` routes MEMORY-mode rotation to `MemoryPickAction/ToggleYesNo/PickGroup`, and click to `MemoryConfirm()`. The m_bBrowse flag is ignored in MEMORY mode (it's a dialog, not a value editor).
  - `TAPE_LABELS` extended from 9 to 23 entries: 3 actions (Save / Load / Verify), 2 YES/NO confirmation, 16 group labels, 2 result feedback (ERR / Completed).
- **Complete VCED parameter coverage**:
  - `COPMEmu::writeVcedGlobal()` extended with cases for PMS+AMS (param 12, packed) and Key Offset (param 13).
  - `COPMEmu::writeVcedOperator()` extended with cases for D2R (param 1), RR (param 3), LS (param 5), RS (param 6), EBS (param 7), AME (param 13, on 0xA0 bit 7), KVS (param 9), DET (param 12). AME is on a separate VCED param id from D1L because the two share register 0xE0 / 0xA0 in different ways.
  - `DX21ParamIndex` enum grew from 37 to 78 entries (added: per-op D2R/RR/LS/RS/EBS/AME/KVS/DET × 4 = 32, plus PMS, AMS, KeyOffset, MasterTune, Mono, PBMode, Breath*4 = 9).
  - `COPMEmuAdapter::setParameter()` and `getParameter()` cover all 78 entries. Setter for kParamPMS/kParamAMS preserves the other nibble when changing one. kParamKeyOffset maps 0..1 to -24..+24. kParamMasterTune maps 0..1 to -64..+63. kParamBreath* map 0..1 to 0..99.
  - `kEditToAdapter[]` (36 entries) now has 26 live bindings (was 14): P MOD SENS, A MOD SENS, E BIAS SENS, KEY VELOCITY, FREQUENCY, DETUNE, EG D2R, EG RR, RATE SCALE, LEVEL SCALE all wired to OP1.
  - `kFunctionToAdapter[]` (46 entries) now has 12 live bindings (was 9): Master Tune, Mono Mode, Fingered/Full Time/Foot Porta, BC Amplitude, BC Pitch Bias, BC EG Bias, Middle C, Bend Mode, Key Shift.

- **Live encoder → synth parameter writes**:
  - `COPMEmuAdapter::kParamInstrument` (0): normalized 0..1 → program 0..N-1. New entry at the top of the enum; all other indices shifted by +1.
  - `CDX21Display::SelectParam(delta)` and `CDX21Display::AdjustValue(delta)`:
    - `SelectParam(±1)`: pure UI cursor move through the per-mode list (PLAY: 1..128, EDIT: 0..35, FUNCTION: 0..45, PERFORMANCE: 0..5, MEMORY: 0..8). Wraps at both ends.
    - `AdjustValue(±1)`: writes the meaningful change for the current mode through `COPMEmuAdapter::setParameter()`:
      - PLAY / PERFORMANCE → `kParamInstrument` (program change, immediate)
      - EDIT → `kEditToAdapter[m_ParamIdx]` (writes through `writeVcedGlobal` / `writeVcedOperator`)
      - FUNCTION → `kFunctionToAdapter[m_ParamIdx]`
      - MEMORY → no-op (tape dialogs are non-numeric)
    Returns the new absolute value (or -1 for un-bound entries).
  - `CDX21Input::m_bBrowse` flag: rotation in EDIT/FUNCTION is either "navigate the list" (browse=true, default) or "edit the value of the current param" (browse=false). The first tick of `EventSwitchHold` toggles the flag; the second tick (≈2 s) still toggles MEMORY PROTECT.
  - Status messages: rotation shows `VAL=NNN`, hold-tick-1 shows `BROWSE` / `EDIT`, hold-tick-2 shows `MEMORY PROTECTED` / `MEMORY UNPROTECTED`.
- **Power-on splash**: `CDX21Display::SetSplash(true)` + 2-second delay in `kernel.cpp::Initialize()`. While splash is active, `Render()` ignores `m_Mode` and shows the boot banner:
  - Page 0: `*  YAMAHA  *` (6×8 text)
  - Page 1: `DX21` (big 7-segment, 32 px tall, fills the row)
  - Page 2: `* SYNTHESIZER *` (6×8 text)
  - Page 3: `v0.1.0  INIT...` (version + init hint)
  Mirrors the original DX21's 2×16 character LCD boot banner on the 128×32 OLED's 4 pages. The 7-seg `DX21` mark uses the new big-string font in `dx21_ui_7seg.h`.

### Changed
- `CDX21Display::Render()` dispatch: if `m_bSplash` is true, render the splash instead of the per-mode page. The new member is declared after `m_LastRenderMs` in the header to match the constructor's init-list order (-Wreorder).

### Documentation & diagrams
- **Stack-exploded-view image for the Pi Zero 2 WH hardware build** (`doc/images/microdx21_stack_exploded.svg` + `.png` + `.body.txt` + `.prompt.md` + `render_stack.sh`).
  LEGO-style exploded view of the four-board stack in mechanical order
  (top→bottom): 1. Waveshare 2.23" OLED HAT (SSD1305, SPI/I2C), 2.
  Adafruit Perma-Proto Bonnet Mini (carries the 5-wire KY-040 cable
  only — no encoder, no audio jacks, no MIDI/HP jacks on the proto),
  3. WM8960 Hi-Fi Sound Card HAT (stereo codec over I2S + I2C), 4.
  Raspberry Pi Zero 2 WH base. The KY-040 rotary encoder sits **outside
  the stack** and is wired to the proto bonnet via 5 flying leads
  (Encoder A, B, SW, GND, +3.3 V). The image is referenced from
  `README.md` "Hardware" section so newcomers can see the assembly
  before they pick up the soldering iron.
- **Algorithm-topology comment block** in `src/opm/opmemu.cpp` (the
  `kNumCarriersPerAlg[]` table and the prose around it) replaced with a
  MAME-derived carrier count for each of the 8 algorithms: CON 0..3 =
  1 carrier, CON 4 = 2, CON 5..6 = 3, CON 7 = 4. Verified empirically
  with `tools/alg_prober.cpp` against the running Nuked-OPM instance.
  Also added a reference assembly listing
  `src/opm/firmware/dx21_rom_v1_5.asm` (14 607 lines of reverse-
  engineered M6803 code) for future SysEx- and patch-table work.
  The raw ROM dump (`src/opm/firmware/DX21romv15.BIN`, 32 KB, V1.5)
  stays **local-only** (gitignored) for copyright reasons.
- **Local reference docs in `doc/`** for ongoing algorithm and
  voice-table analysis: `Yamaha-TX81Z-Manual.pdf` (algorithm
  diagrams), the gist by @bryc (`e997954473940ad97a825da4e7a496fa`)
  referenced from comments, and `tx81z-algorithms.jpg` /
  `refcard.gif` as visual aids when reasoning about the DX21's 4-OP
  topology. These scans are **not tracked** in the repo (gitignored,
  copyright).

### SD card persistence
- **Pre-create `MICRODX21/BANK_01..16/` on the release image** so the
  MEMORY-mode tape-save flow has somewhere to land the first time.
  `scripts/prepare_sd_skeleton.sh` builds an empty skeleton tree
  (`voice_00.json` … `voice_31.json` placeholders in each bank,
  `performances.json` at the top) and is invoked by
  `.github/workflows/build.yml` right before the IMG is published.
  Users who only reflash the kernel can now run "SAVE → group 01 → YES"
  out of the box.
- **Runtime fallback `IFileSystem::MakeDirectory(path, recursive=true)`**:
  before, a missing parent dir would cause `saveRamBank` /
  `savePerformanceBank` to fail on a freshly formatted card. Now both
  backends (`std_filesystem.h`, `fatfs_filesystem.h`) implement
  `MakeDirectory` so the synth can create the `MICRODX21/` and
  `BANK_NN/` parents on demand. The release-pipeline pre-create is
  still the canonical path; this is the safety net.
- **`config/microdx21.ini`** gained a `[SDCard]` section documenting
  the bank layout and the optional auto-load behaviour.

### Synth engine correctness
- **Sustain-aware voice stealing** (`commit 62537e0`):
  - `COPMEmu::allocVoice()` now prefers a non-sustained voice over a
    sustained one when stealing, so a held note under sustain-pedal
    never gets cut off by an incoming key press. The fallback chain
    is: (1) inactive voice, (2) oldest non-sustained active voice,
    (3) oldest sustained voice. The soft-steal envelope (TL ramp +
    fast RR) is preserved on the chosen victim.
  - New public accessors on `COPMEmu` for test inspection:
    `isVoiceActive(i)`, `isVoiceSustained(i)`, `isVoicePlayingNote(n)`.
  - `tests/test_voice_stealing.cpp` — 3 scenarios covering (a) prefer
    inactive, (b) prefer non-sustained, (c) fall through to oldest
    sustained.
- **Configurable velocity curve** (`commit 3af0f41`):
  - The pre-existing `applyVelocityCurve()` was defined but never
    called from the MIDI path — fixed. `CMicroDX21::NoteOn()` now
    threads every key-on through it.
  - 5 named curves via `vel_out = round((vel_in/127)^p * 127)`:
    `linear` (p=1.0), `soft` (p=1.3), `hard` (p=0.7), `dx21` (p=1.5),
    `softest` (p=2.0).
  - Logic extracted to header-only `src/microdx21/velocitycurve.h`
    (namespace `microdx21`, enum `VelocityCurve`).
  - Configurable from `config/microdx21.ini` (`VelocityCurve=dx21`)
    or via the new `kMidiVelocityCurve` SysEx/CC param (clamped 0..4).
  - `tests/test_velocity_curve.cpp` — 7 scenarios verifying the
    monotonicity, end-point anchoring, and per-curve shape.
- **Exponential portamento with per-block decay** (`commit 89f1e4f`):
  - The DX21's glide is exponential in time (per the owner's manual);
    the old implementation was linear with a per-sample rate factor,
    which made the glide speed depend on the audio buffer size.
  - `m_portaRateFactor` → `m_portaDecayFactor`; new formula:
    `T_half_max = 2.5 s` at rate=99, `decay = 0.5^(1/(T_half * sr))`,
    applied per-block via `std::pow(m_portaDecayFactor, numSamples)`.
    This makes 64-sample and 256-sample buffers produce the same
    curve. Rate 0 disables (instant snap), rate 99 gives ~12.5 s per
    octave.
  - Fixed a pre-existing bug where `voiceCount(SideA)` in MONO mode
    returned 8 instead of 1, so `allocVoice`'s MONO pre-step never
    fired and consecutive MONO notes landed on different voices (no
    portamento). Now `voiceCount` returns 1 in MONO+Single.
  - New public accessors: `getVoicePitch(int)`, `isVoicePorting(int)`.
  - `tests/test_portamento.cpp` — 5 scenarios (rate=0 snap, glide
    happens, higher rate = slower, exponential convergence at
    known offsets, mode change Off→FullTime→Off).

### Memory Protect (DX21 Function #23 / dat_F610:35)
Before, the encoder hold-tick #2 toggled a flag in `CDX21Display` that
was pure cosmetics — the 2×2 pixel indicator. Nothing in `COPMEmu` or
`CDX21Memory` actually honoured it, so a SysEx bulk dump from a
connected editor would happily overwrite all 32 RAM voices regardless
of the toggle. The full enforcement lands in `commit 1617a0b`:

- **Gated write paths in `COPMEmu`** (high-level helpers): `initVoice()`,
  `saveEditRecall()` / `loadEditRecall()`, `writeVcedGlobal()` /
  `writeVcedOperator()`, `applySysexParam()` (real-time 0x12 pp vv),
  and `applySysexChanges()` (defence in depth — drops any queued
  changes if the flag was enabled after `handleSysex()` queued them).
  `handleSysex()` rejects the 32-voice VCED bulk dump (modelId=0x09).
- **Gated write paths in `CDX21Memory`**: `setRamVoice()` returns
  `false`, `importSysex()` returns `-1`. The `COPMEmu::setMemoryProtect`
  setter delegates to `CDX21Memory::setMemoryProtect` so one call
  updates both layers.
- **Wiring**: `CDX21Input::ApplyEvent` (hold-tick #2) now also calls
  `m_pDisplay->GetAdapter()->setMemoryProtect(newProt)` on top of the
  display flag, so the toggle reaches the synth.
- **Read paths unaffected** — `getRamVoice()`, `applyPatchToSide()`,
  anything that reads from RAM still works. The synth keeps playing
  whatever was last stored; the user just can't change it. Matches the
  real DX21 hardware behaviour.
- **Test back-door**: `COPMEmu::TestDoor` (public struct, friend) —
  exposes `writeVcedGlobal/Operator` for unit tests via a friend
  declaration. Production code never calls into it; the only consumers
  are the test files.
- `tests/test_memory_protect.cpp` — 8 scenarios (setRamVoice rejected,
  importSysex rejected, reads still work, real-time param change
  rejected, writeVcedGlobal rejected, writeVcedOperator rejected,
  initVoice rejected, unprotect restores writes).

### Build system & CI fixes
- **`scripts/prepare_sd_skeleton.sh`** wired into
  `.github/workflows/build.yml` as the canonical step that builds the
  empty `MICRODX21/BANK_01..16/` tree before the IMG is published.
- **GH Actions build verification** — every commit on `main` now
  produces a valid `out/kernel_rpi{3,4,5}.img` (typical Pi-3 build:
  813 856 bytes, runs in ~3 min on the hosted runner).

### Fixed
- **`CConfig::Load()` duplicate `trim` lambda** (`commit b637f87`).
  When the `VelocityCurve` parsing block was added, a second
  `auto trim = [](std::string& s) { ... };` was declared inside the
  function, shadowing the one at the top of the same function. macOS
  clang++ did not catch it because the host target doesn't actually
  instantiate the FatFS include path; `aarch64-none-elf-g++` on GH
  Actions did, with `conflicting declaration 'auto trim'`. Removed
  the inner declaration, kept the outer. No behaviour change.
- **`dx21_input.cpp` forward-declaration of `COPMEmuAdapter`**
  (`commit 4b8c880`). The encoder hold-tick #2 calls
  `pA->setMemoryProtect(newProt)` through a forward-declared type.
  Clang accepts this as a free call, `aarch64-none-elf-g++` requires
  the full type. Replaced the forward decl with `#include
  "audio/opmemuadapter.h"`. No new dependency.

### Test coverage
| Target | ctest name | Scenarios | Status |
|---|---|---|---|
| `test_voice_stealing` | `voice_stealing` | 3 (prefer inactive, prefer non-sustained, fall through to oldest sustained) | PASS |
| `test_velocity_curve` | `velocity_curve` | 7 (5 curves + monotonicity + end-point anchoring) | PASS |
| `test_portamento` | `portamento` | 5 (rate=0 snap, glide, slower rate, exponential convergence, mode change) | PASS |
| `test_memory_protect` | `memory_protect` | 8 (5 write paths rejected + reads unaffected + unprotect restores) | PASS |

All 4 ctest targets are registered with `add_test()` and run on every
host build (`cd build && cmake .. && cmake --build . && ctest`).
The cross-compile path on GH Actions runs the same targets via the
toolchain-bound `Makefile` build of the same files.

## [0.1.0] — 2026-06-06 — Initial release

First public release. The project boots to a working Yamaha DX21 emulator on Raspberry Pi 3/4/5 (bare-metal Circle stdlib) and on macOS/PC (SDL2 + PortMidi).

### Display & input
- **`CDX21Display::Render()`** reads live state from a `COPMEmuAdapter` set via `SetAdapter()`. 5 mode-specific helpers (`RenderPlayMode` / `RenderEditMode` / `RenderPerformanceMode` / `RenderFunctionMode` / `RenderMemoryMode`) each write to the 4 SSD1305 pages. Per-mode values:
  - PLAY: voice number + name from the adapter, play mode (SINGLE/DUAL/SPLIT) on page 3, bank group (A1-A8..B9-B16) derived from voice / 16.
  - EDIT: 7-seg big value comes from a static `kEditToAdapter[]` table mapping 36 EDIT-mode entries to `DX21ParamIndex` values. 14 entries have live getters; the other 22 fall back to "n/a".
  - PERFORMANCE: voice name in 7-seg, or "BUFF" when COMPARE is toggled.
  - FUNCTION: 9 of 46 entries have live getters; the rest show "=n/a".
  - MEMORY: bank name in 7-seg, tape dialog label.
- **`CDX21Display::InvalidateIfStale(maxAgeMs)`** — if the last render is older than `maxAgeMs`, force a redraw. Used by `RunCore2()` at 5 Hz so MIDI-driven state changes (Program Change, CC#0 Bank Select) become visible without explicit `Set*()` calls.
- **`CDX21Input`** wraps the Circle sensor-addon's `CKY040` (ISR-driven, with switch debounce, single/double/triple-click, hold detection). Maps:
  - rotate CW/CCW → next/prev parameter
  - single click → cycle mode (PLAY → EDIT → PERFORM → FUNCTION → MEMORY → PLAY)
  - double click → COMPARE toggle
  - long press → memory-protect toggle
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
