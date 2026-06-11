#include <cstdio>
#include "opmemu.h"

// Operator-Masken
// YM2151 KeyOn register bit positions: D6=M1, D5=C1, D4=M2, D3=C2
static const uint8_t OP_M1  = 0x40;
static const uint8_t OP_C1  = 0x20;
static const uint8_t OP_M2  = 0x10;
static const uint8_t OP_C2  = 0x08;
static const uint8_t OP_ALL = OP_M1 | OP_C1 | OP_M2 | OP_C2;  // = 0x78

// KC-Tabelle: MIDI-Note-Oktave → YM2151 KC-Wert

// ===========================================================================
// Algorithm Mapping: DX21 → YM2151
// ===========================================================================
//
// The DX21 uses a YM2164 (OPP) chip, which has the same 8 algorithm
// topologies as the YM2151 (OPM). The user-facing DX21 ALG value 1..8
// maps 1:1 to the YM2151 CON register field (bits [2:0] of register
// 0x20). The DX21_Patch struct stores alg 0-based, so it goes directly
// into CON. This is confirmed by:
//   1. The picoX21H reference project, which writes alg directly as CON
//      on real YM2151 hardware.
//   2. The MAME YM2151 emulator's set_connect() function
//      (src/devices/sound/ym2151.cpp), which is the canonical ground
//      truth for OPM algorithm topology.
//   3. The Nuked OPM fm_algorithm table in opm.c, which produces
//      the same per-CON carrier count as MAME (verified empirically
//      with tools/alg_prober.cpp).
//
// YM2151 Algorithm Topologies (CON = algorithm number, 0..7):
//   ── Carriers (OUT) / Modulators shown explicitly ──
//
//   CON=0:  M1→C1→MEM→M2→C2→OUT     1 carrier:  C2
//   CON=1:  M1→MEM, C1→MEM, MEM→M2→C2→OUT     1 carrier:  C2
//   CON=2:  M1→C2, C1→MEM, MEM→M2→C2→OUT      1 carrier:  C2
//   CON=3:  M1→C1→MEM→C2, M2→C2→OUT            1 carrier:  C2
//   CON=4:  M1→C1→OUT, M2→C2→OUT               2 carriers: C1, C2
//   CON=5:  M1→M2→OUT, C1→OUT, C2→OUT         3 carriers: M2, C1, C2
//   CON=6:  M1→C1→OUT, M2→OUT, C2→OUT         3 carriers: C1, M2, C2
//   CON=7:  M1, C1, M2, C2 all →OUT            4 carriers: all additive
//
// MEM is a 1-sample delay element that joins the chain in the more
// complex algorithms. The YM2151's CON=0 is the "4-op serial" topology
// (also known in the DX7/DX9 family as ALG 1); CON=7 is the "all
// additive" topology (DX7/DX9 ALG 32).
//
// DX21 Operator → YM2151 Slot Mapping (per picoX21H reference):
//   DX21 OP1 → YM2151 M1  (register offset +0,  KeyOn bit 0x40)
//   DX21 OP2 → YM2151 M2  (register offset +8,  KeyOn bit 0x10)
//   DX21 OP3 → YM2151 C1  (register offset +16, KeyOn bit 0x20)
//   DX21 OP4 → YM2151 C2  (register offset +24, KeyOn bit 0x08)
//
// CRITICAL: OP2 must go to M2 (+8), NOT C1 (+16). A previous bug had
// OP_SLOT={0,16,8,24} which swapped M2/C1, breaking all algorithms.
// ===========================================================================
// YM2151 KC nibble layout per octave: only 12 of the 16 nibble values are
// musical semitones — 0x3, 0x7, 0xB and 0xF are invalid and produce
// non-chromatic frequencies on the real chip.
//
// Valid nibbles map to chromatic notes as follows (within one chip octave):
//   nib 0  → C#     nib 5  → F      nib 10 → A
//   nib 1  → D      nib 6  → F#     nib 12 → A#
//   nib 2  → D#     nib 8  → G      nib 13 → B
//   nib 4  → E      nib 9  → G#     nib 14 → C  (top of the chip octave)
//
// Note that 'C' is the TOP of a chip octave, so the C/C# boundary in MIDI
// straddles a chip-octave boundary. We compensate by computing the chip
// octave as `(midi - 1) / 12 - 1` and indexing the table with `(midi - 1) % 12`,
// which lines up MIDI C (note_idx 11) with chip nibble 14 in the lower octave.
static const uint8_t KC_TABLE[12] = {
    0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14
};

// Operator-Slot-Offsets pro Kanal (YM2151 hardware slot layout)
// DX21 OP1→M1(+0), OP2→M2(+8), OP3→C1(+16), OP4→C2(+24)
static const uint8_t OP_SLOT[4] = { 0, 8, 16, 24 };

// ===========================================================================
// Konstruktor
// ===========================================================================
COPMEmu::COPMEmu(IFileSystem* fs)
    : m_fs(fs)
    , m_memory(fs)
    , m_currentPatch(0)
    , m_sustainPedal(false)
    , m_playMode(Single)
    , m_splitPoint(60)
    , m_balance(50)
    , m_mono(false)
    , m_patchA(0)
    , m_patchB(0)
    , m_masterTune(0)
    , m_masterGain(5.0f)
    , m_pbValue(0)
    , m_pbRange(2)
    , m_portaMode(PortaOff)
    , m_portaRate(0)
    , m_portaDecayFactor(0.0f)
    , m_pbMode(PBAll)
    , m_memoryProt(false)
    , m_breathValue(0)
    , m_breathPitchBias(0)
    , m_breathAmplitude(0)
    , m_breathEGBias(0)
    , m_breathEGDepth(0)
    , m_voiceAge(0)
    , m_cycleAccum(0)
    , m_staggerCount(0)
    , m_sysexEditVoice(0)
    , m_pendingCount(0)
{
    for (int i = 0; i < kSysexNumParams; ++i) {
        m_sysexParam[i] = 0;
        m_sysexDirty[i] = false;
    }
    m_filterL1.init(kSampleRate, kFilterCutoff, 0);
    m_filterL2.init(kSampleRate, kFilterCutoff, 1);
    m_filterR1.init(kSampleRate, kFilterCutoff, 0);
    m_filterR2.init(kSampleRate, kFilterCutoff, 1);
    memset(&m_chip, 0, sizeof(m_chip));
    memset(m_shadow, 0, sizeof(m_shadow));
    memset(m_pendingL, 0, sizeof(m_pendingL));
    memset(m_pendingR, 0, sizeof(m_pendingR));
    for (int i = 0; i < kNumVoices; ++i) {
        m_voices[i] = { false, false, 0, 0, 0, 0, 0.0f, 0.0f, false, 0, 0 };
    }
}

// ===========================================================================
// Initialize — OPM Reset, alle Kanäle stumm schalten
// ===========================================================================
void COPMEmu::Initialize()
{
    OPM_Reset(&m_chip);

    // Initialize RAM voices from ROM patches (first 32)
    initRamFromRom();

    clockChip(64);

    for (int ch = 0; ch < kNumVoices; ++ch) {
        writeReg(0x08, ch);  // KeyOff

        for (int op = 0; op < 4; ++op) {
            int slot = ch + OP_SLOT[op];
            writeReg(0x40 + slot, 0x01);  // DT1=0, MUL=1
            writeReg(0x60 + slot, 0x7F);  // TL=127
            writeReg(0x80 + slot, 0x1F);  // KS=0, AR=31
            writeReg(0xA0 + slot, 0x00);  // AME=0, D1R=0
            writeReg(0xC0 + slot, 0x00);  // DT2=0, D2R=0
            writeReg(0xE0 + slot, 0xFF);  // D1L=15, RR=15
        }

        writeReg(0x20 + ch, 0xC7); // RL=3, FB=0, CON=7
        writeReg(0x28 + ch, 0x00); // KC=0
        writeReg(0x30 + ch, 0x00); // KF=0
        writeReg(0x38 + ch, 0x00); // PMS=0, AMS=0
    }

    // LFO off
    writeReg(0x18, 0x00); // LFO frequency = 0
    writeReg(0x19, 0x00); // AMD = 0
    writeReg(0x1B, 0x00); // LFO waveform = saw, CT=0

    // Final OPP pipeline flush: clear any stale register pipeline state
    // and give the chip time to settle after all the register writes above.
    // The per-writeReg flush handles 0x20 delays, but a final clearing
    // ensures no edge-case stale state remains.
    m_chip.reg_data_ready = 0;
    m_chip.reg_address_ready = 0;
    clockChip(64);

    // Reset output filters to avoid clicks from stale state
    m_filterL1.reset();
    m_filterL2.reset();
    m_filterR1.reset();
    m_filterR2.reset();
}

// ===========================================================================
// clockChip — clock the OPM a number of cycles, capturing the DAC stream
// into the pending-audio ring so processBlock can decimate it properly.
// Falls der Puffer überläuft (extrem viele writeReg in einem Tick), wird
// abgeschnitten — der akustische Schaden ist deutlich geringer als ein
// DC-Plateau, weil die ältesten Samples ohnehin überschrieben würden.
// ===========================================================================
void COPMEmu::clockChip(int cycles)
{
    int32_t dac[2];
    for (int i = 0; i < cycles; ++i) {
        OPM_Clock(&m_chip, dac, nullptr, nullptr, nullptr);
        if (m_pendingCount < kPendingMax) {
            m_pendingL[m_pendingCount] = dac[0];
            m_pendingR[m_pendingCount] = dac[1];
            m_pendingCount++;
        }
    }
}

// ===========================================================================
// writeReg — Direct register write with proper OPM timing.
//
// The Nuked OPM write pipeline:
//   Clock 1: Address write -> write_a=1
//   Clock 2: write_a_en latched -> reg_data_ready cleared, reg_address latched
//   Clock 3: Data write -> write_d=1
//   Clock 4: write_d_en latched -> reg_data set, reg_data_ready=1
//   Clocks 5-36: clockChip(32) — chip visits all 32 slots, applies register
//
// In OPP (YM2164) mode, register 0x20 (RL/FB/CON) uses a 4-cycle shift
// register delay. See the flush block below for details.
// ===========================================================================
void COPMEmu::writeReg(uint8_t reg, uint8_t val)
{
    // --- Address phase (2 clocks) ---
    OPM_Write(&m_chip, 0, reg);
    clockChip(2);

    // --- Data phase (2 clocks) ---
    OPM_Write(&m_chip, 1, val);
    clockChip(2);

    // --- Application phase (32 clocks to visit all 32 slots) ---
    clockChip(32);

    // --- OPP pipeline flush for register 0x20 (RL/FB/CON) ---
    // In OPP (YM2164) mode, register 0x20 uses a 4-cycle shift register
    // delay (reg_20_delay). The delay flag is set when the chip visits
    // the matching channel while reg_data_ready=1, then fires 4 cycles
    // later, reading reg_data to apply RL/FB/CON.
    //
    // CRITICAL BUG: reg_data_ready persists into the next writeReg's
    // first clock (clock 37 of the absolute timeline). If the matching
    // channel is visited at that moment, a late flag is set. Its delay
    // fires at clock 41, AFTER the next writeReg's data write (clock 40)
    // has overwritten reg_data. ch_rl then gets a wrong value (often 0,
    // causing complete silence on that channel).
    //
    // Fix: after writing 0x20 registers, do a dummy address write to
    // clear reg_data_ready, then clock enough for pending delay flags
    // to fire while reg_data still holds the correct value.
    if ((reg & 0xf8) == 0x20) {
        // Dummy address write clears reg_data_ready after 2 clocks.
        // Address 0x01 is safe: in OPP mode 0x01 & 0xf8 == 0, so it's
        // accepted as a register address, but with no data write following,
        // no register is accidentally applied.
        OPM_Write(&m_chip, 0, 0x01);
        clockChip(2);
        // reg_data_ready is now 0. Clock for pending 0x20 delay flags
        // to fire. Worst case: flag set at cycle 38 (last moment before
        // reg_data_ready cleared), fires at cycle 42.
        // 12 clocks covers this with margin.
        clockChip(12);
    }

    m_shadow[reg] = val;
}

// ===========================================================================
// writeField
// ===========================================================================
void COPMEmu::writeField(uint8_t addr, unsigned ls_bit, unsigned bits, uint8_t data)
{
    uint8_t data_mask = (1u << bits) - 1;
    uint8_t reg_mask  = ~(data_mask << ls_bit);
    uint8_t val = (m_shadow[addr] & reg_mask) | ((data & data_mask) << ls_bit);
    writeReg(addr, val);
}

// ===========================================================================
// computeLSOffset — DX21 Level Scaling: TL adjustment based on LS and note
// ===========================================================================
uint8_t COPMEmu::computeLSOffset(uint8_t ls, int note) const
{
    if (ls == 0) return 0;
    int breakpoint = 60; // C4 = MIDI note 60
    if (note <= breakpoint) return 0;
    int semitones = note - breakpoint;
    // LS 99 → full scaling (~36 dB over 4 octaves)
    int atten = (ls * semitones * 127) / (99 * 48);
    return (atten > 127) ? 127 : static_cast<uint8_t>(atten);
}

// ===========================================================================
// computeKVSOffset — DX21 Key Velocity Sensitivity: TL adjustment
// ===========================================================================
uint8_t COPMEmu::computeKVSOffset(uint8_t kvs, int velocity) const
{
    if (kvs == 0) return 0;
    // Higher KVS → velocity affects output more
    // velocity=127 → no attenuation, velocity=1 → max attenuation
    int vel_factor = 127 - velocity;
    int atten = (kvs * vel_factor * 2) / 7;
    return (atten > 127) ? 127 : static_cast<uint8_t>(atten);
}

// ===========================================================================
// applyPatchToVoice — Write ALL static patch parameters for one voice.
// Called from applyProgramChange() for all 8 voices at program change time.
// Writes DT1/MUL, base TL (no LS/KVS), KS/AR, AME/D1R, DT2/D2R, D1L/RR,
// RL/FB/CON, PMS/AMS — 26 registers per voice. Per-note parameters (TL with
// LS/KVS, KC, KF, KeyOn) are handled separately in noteOn/applyTL.
// ===========================================================================
void COPMEmu::applyPatchToVoice(int voice, const DX21_Patch& patch)
{
    for (int op = 0; op < 4; ++op) {
        int slot = voice + OP_SLOT[op];
        const DX21_Operator& o = patch.op[op];

        // ---- Register 0x40: DT1[6:4] MUL[3:0] ----
        uint8_t dt1 = (o.det <= 6) ? DET_TO_DT1[o.det] : 0;
        uint8_t mul = (o.crs < 64) ? CRS_TO_MUL[o.crs] : 15;
        uint8_t dt1_mul = (dt1 << 4) | (mul & 0x0F);
        writeReg(0x40 + slot, dt1_mul);

        // ---- Register 0x60: TL[6:0] + OPP ramp enable ----
        // Base TL without LS/KVS — those are applied per-note in applyTL().
        // Bit 7 = OPP ramp enable for smooth level transitions.
        uint8_t tl = (99 - o.out) * 127 / 99;
        writeReg(0x60 + slot, 0x80 | (tl & 0x7F));

        // ---- Register 0x80: KS[7:6] AR[4:0] ----
        uint8_t ks_ar = ((o.rs & 0x03) << 6) | (o.ar & 0x1F);
        writeReg(0x80 + slot, ks_ar);

        // ---- Register 0xA0: AME[7] D1R[4:0] ----
        uint8_t ame_d1r = ((o.ame & 0x01) << 7) | (o.d1r & 0x1F);
        writeReg(0xA0 + slot, ame_d1r);

        // ---- Register 0xC0: DT2[7:6] D2R[4:0] ----
        uint8_t d2r = o.d2r & 0x1F;
        writeReg(0xC0 + slot, d2r);

        // ---- Register 0xE0: D1L[7:4] RR[3:0] ----
        uint8_t d1l = 15 - (o.d1l & 0x0F);
        uint8_t d1l_rr = (d1l << 4) | (o.rr & 0x0F);
        writeReg(0xE0 + slot, d1l_rr);

        // ---- OPP Attack-Rate Skip ----
        // The real YM2164 (OPP) has a plateau in AR=18-30 where attack times
        // are ~105-115 ms, falling between OPM AR=12 (140 ms) and AR=13 (95 ms).
        // Per the XCent/nuked-opp-xcent analysis, this is modeled by skipping
        // every Nth attack inc=1 tick.  For DX21 patches with AR in this range,
        // apply skip=6 (1.17x slower, matching OPP measurements).
        // AR < 18: no skip needed (OPM native rates match OPP).
        // AR 18-30: skip=6 approximates the OPP plateau (~1.17x slower attack).
        uint8_t ar = o.ar & 0x1F;
        uint8_t atk_skip = (ar >= 18 && ar <= 30) ? 6 : 0;
        OPM_SetAttackSkip(&m_chip, slot, atk_skip);
    }

    // ---- Channel registers ----
    uint8_t rl_fb_con = (3 << 6) | ((patch.fb & 0x07) << 3) | (patch.alg & 0x07);
    writeReg(0x20 + voice, rl_fb_con);

    uint8_t pms_ams = ((patch.pms & 0x07) << 4) | (patch.ams & 0x03);
    writeReg(0x38 + voice, pms_ams);
}

// ===========================================================================
// applyTL — Write per-note TL values with Level Scaling and Key Velocity
// Sensitivity. Only 4 register writes (one per operator) instead of the
// full 26-register applyPatch. Called at noteOn time.
// OPP TL ramp (bit 7) ensures smooth transitions from the base TL written
// by applyPatchToVoice to the per-note TL with LS/KVS offsets.
// ===========================================================================
void COPMEmu::applyTL(int voice, const DX21_Patch& patch, int midiNote, int velocity, int tlOffset)
{
    for (int op = 0; op < 4; ++op) {
        int slot = voice + OP_SLOT[op];
        const DX21_Operator& o = patch.op[op];

        // Operator ON/OFF (panel A1-A4 / SysEx function param 93):
        // a disabled operator is muted via max attenuation.
        if (!dx21_op_enabled(&patch, op)) {
            writeReg(0x60 + slot, 0x80 | 0x7F);
            continue;
        }

        uint8_t tl = (99 - o.out) * 127 / 99;
        tl += computeLSOffset(o.ls, midiNote);
        tl += computeKVSOffset(o.kvs, velocity);
        tl += static_cast<uint8_t>(tlOffset);
        if (tl > 127) tl = 127;
        writeReg(0x60 + slot, 0x80 | (tl & 0x7F));  // bit 7 = OPP ramp enable
    }
}

// ===========================================================================
// writeFrequency — write KC/KF from a pitch in semitones
// Handles master tune, pitch bend, and portamento.
// ===========================================================================
void COPMEmu::writeFrequency(int voice, float pitchSemitones, bool keyOn)
{
    pitchSemitones += m_masterTune / 100.0f;

    // Clamp to valid MIDI range after tuning
    if (pitchSemitones < 0.0f) pitchSemitones = 0.0f;
    if (pitchSemitones > 127.0f) pitchSemitones = 127.0f;

    int midiNote = static_cast<int>(pitchSemitones);
    float frac   = pitchSemitones - static_cast<float>(midiNote);

    // KC layout: chip-octave changes between C and C# (not between B and C),
    // and 'C' is the top of an octave (nibble 14). We shift the boundary
    // by subtracting 1 and reduce the octave by 1 to map:
    //   MIDI 60 (C4)  → KC 0x3E  (chip-oct 3, nibble C)
    //   MIDI 61 (C#4) → KC 0x40  (chip-oct 4, nibble C#)
    //   MIDI 69 (A4)  → KC 0x4A  (chip-oct 4, nibble A) → 440 Hz on real chip
    int n = midiNote - 1;
    if (n < 0) n = 0;
    int octave = n / 12 - 1;
    if (octave < 0) octave = 0;
    if (octave > 7) octave = 7;             // 3-bit block field
    unsigned key = KC_TABLE[n % 12];
    uint8_t kc   = static_cast<uint8_t>((octave << 4) | key);

    // KF: 0..255 = 0..1/2 semitone (64 steps = 1 semitone)
    int kf = static_cast<int>(frac * 64.0f) & 0xFF;

    writeReg(0x28 + voice, kc);
    writeReg(0x30 + voice, static_cast<uint8_t>(kf));

    if (keyOn) {
        // YM2151 KeyOn register: bits[6:3]=operator flags, bits[2:0]=channel
        writeReg(0x08, OP_ALL | static_cast<uint8_t>(voice));
    } else {
        writeReg(0x08, static_cast<uint8_t>(voice));
    }
}

// ===========================================================================
// applyPitchToVoice — update KC/KF for pitch bend / portamento on active voice
// ===========================================================================
void COPMEmu::applyPitchToVoice(int voice)
{
    if (voice < 0 || voice >= kNumVoices) return;
    // Inactive voices are still written while their Pitch EG release
    // (stage 2) glides toward PL3 during the OPM release ring-out.
    if (!m_voices[voice].active && m_voices[voice].pegStage != 2) return;

    float pitch = m_voices[voice].currentPitch;

    // Apply pitch bend
    if (m_pbRange > 0 && m_pbValue != 0) {
        float pbSemitones = (static_cast<float>(m_pbValue) / 8192.0f) * static_cast<float>(m_pbRange);
        pitch += pbSemitones;
    }

    // Apply breath controller pitch bias
    if (m_breathPitchBias > 0 && m_breathValue > 0) {
        float breathSemitones = (static_cast<float>(m_breathValue) / 127.0f) * (static_cast<float>(m_breathPitchBias) / 99.0f) * 12.0f;
        pitch += breathSemitones;
    }

    // Apply Pitch EG offset. pegLevel is 50 (center) for idle/flat
    // voices, so this is a no-op unless a PEG is in progress.
    // Scale: (level - 50) * 0.96 semitones ≈ ±4 octaves full range.
    pitch += (m_voices[voice].pegLevel - 50.0f) * 0.96f;

    writeFrequency(voice, pitch, false); // no keyOn
}

// ===========================================================================
// allocVoice
// ===========================================================================
// allocVoice — Find a free voice, retrigger same note, or steal oldest.
//
// Voice-steal strategy: instead of a hard KeyOff that abruptly cuts the
// release phase, we fade the stolen voice smoothly:
//   1. KeyOff — starts the release phase
//   2. TL → max attenuation (0xFF with OPP ramp) — fades output to silence
//   3. RR → maximum (15) — ensures fast release even if the patch has slow RR
//
// The OPP TL-ramp (bit 7 of register 0x60) transitions the total level
// smoothly one step at a time, so there is no audible click. The fast RR
// ensures the envelope ramps down quickly, and the max TL silences the
// output regardless of the envelope phase.
//
// Sustain-aware priority: when stealing, we prefer non-sustained voices
// (voices with the sustain-pedal-held flag cleared) over sustained ones.
// This matches the original DX21's held-note protection: notes that the
// player explicitly held with the pedal should not be silently destroyed
// by new note-ons until the pedal is released. Only when all active voices
// in the relevant side are sustained (extreme polyphony under sustain) do
// we fall back to stealing the oldest sustained voice.
// ===========================================================================
int COPMEmu::allocVoice(Side side, int note)
{
    int start = voiceStart(side);
    int count = voiceCount(side);
    if (count == 0) return -1;

    // MONO side: free the single voice slot before allocation.
    // count == 1 only occurs for monophonic sides (Single+MONO, or
    // a MONO patch on a SPLIT side — the 7+1 / 1+1 layouts).
    if (count == 1) {
        for (int i = start; i < start + count; ++i) {
            if (m_voices[i].active) {
                freeVoice(i);
                break;
            }
        }
    }

    // Search by original MIDI note (retrigger) within side
    for (int i = start; i < start + count; ++i) {
        if (m_voices[i].active && m_voices[i].origNote == note) {
            // Retrigger: KeyOff, then wait a few cycles before KeyOn.
            // This gives the envelope time to start release, preventing
            // a click when the new KeyOn resets phase while output is still high.
            writeReg(0x08, static_cast<uint8_t>(i));  // KeyOff only
            // Mark voice for delayed retrigger — noteOn will handle it
            m_voices[i].active = false;  // free for now
            m_voices[i].sustained = false;
            m_voices[i].keyOnDelay = 0;  // cancel any pending KeyOn
            m_voices[i].age = m_voiceAge++;
            return i;
        }
    }
    // Find a free voice within side
    for (int i = start; i < start + count; ++i) {
        if (!m_voices[i].active) return i;
    }
    // Steal a voice within side, preferring non-sustained voices.
    //
    // Sustained voices are held by the sustain pedal and represent notes
    // the player expects to keep ringing. Stealing them silently destroys
    // the held note. We therefore prefer to steal a non-sustained voice
    // first (if any exist), and only fall back to stealing a sustained
    // voice when all voices in this side are held by pedal.
    //
    // Within each pool (non-sustained, sustained), steal the oldest
    // active voice by `age` (FIFO). This matches the original DX21's
    // voice-allocation behavior, which is first-in-first-out within the
    // available pool.
    int oldest_non_sustained = -1;
    int oldest_sustained     = -1;
    for (int i = start; i < start + count; ++i) {
        if (!m_voices[i].active) continue;
        if (m_voices[i].sustained) {
            if (oldest_sustained < 0 || m_voices[i].age < m_voices[oldest_sustained].age) {
                oldest_sustained = i;
            }
        } else {
            if (oldest_non_sustained < 0 || m_voices[i].age < m_voices[oldest_non_sustained].age) {
                oldest_non_sustained = i;
            }
        }
    }
    int oldest = (oldest_non_sustained >= 0) ? oldest_non_sustained : oldest_sustained;
    // Soft-steal: KeyOff + ramp mute + fast release instead of instant cut
    writeReg(0x08, static_cast<uint8_t>(oldest));
    for (int op = 0; op < 4; ++op) {
        int slot = oldest + OP_SLOT[op];
        writeReg(0x60 + slot, 0xFF);          // TL=max with OPP ramp
        writeReg(0xE0 + slot, (0x0F << 0));   // RR=15 → fastest release
    }
    m_voices[oldest].active = false;
    m_voices[oldest].sustained = false;
    m_voices[oldest].keyOnDelay = 0;  // cancel any pending KeyOn
    return oldest;
}

void COPMEmu::freeVoice(int voice)
{
    if (voice < 0 || voice >= kNumVoices) return;
    writeReg(0x08, static_cast<uint8_t>(voice));
    for (int op = 0; op < 4; ++op) {
        int slot = voice + OP_SLOT[op];
        writeReg(0x60 + slot, 0xFF);  // TL=max with OPP ramp → soft silence
    }
    m_voices[voice].active = false;
    m_voices[voice].sustained = false;
    m_voices[voice].keyOnDelay = 0;  // cancel pending KeyOn if any
}

// ===========================================================================
// noteOn
// ===========================================================================
void COPMEmu::noteOn(int note, int velocity)
{
    if (velocity == 0) {
        noteOff(note);
        return;
    }

    switch (m_playMode) {
    case Single: {
        int voice = allocVoice(SideA, note);
        if (voice < 0) return;
        const DX21_Patch* patch = getPatch(m_patchA);
        if (!patch) break;
        setupVoice(voice, note, velocity, *patch);
        break;
    }
    case Dual: {
        int vA = allocVoice(SideA, note);
        if (vA >= 0) {
            const DX21_Patch* patchA = getPatch(m_patchA);
            if (!patchA) break;
            setupVoice(vA, note, velocity, *patchA);
        }
        int vB = allocVoice(SideB, note);
        if (vB >= 0) {
            const DX21_Patch* patchB = getPatch(m_patchB);
            if (!patchB) break;
            setupVoice(vB, note, velocity, *patchB);
        }
        break;
    }
    case Split: {
        Side side = routeNoteToSide(note);
        int voice = allocVoice(side, note);
        if (voice < 0) return;
        const DX21_Patch& patch = (side == SideA)
            ? *getPatch(m_patchA)
            : *getPatch(m_patchB);
        setupVoice(voice, note, velocity, patch);
        break;
    }
    }
}

// ===========================================================================
// noteOff
// ===========================================================================
void COPMEmu::noteOff(int note)
{
    for (int i = 0; i < kNumVoices; ++i) {
        if (m_voices[i].active && m_voices[i].origNote == note) {
            if (m_sustainPedal) {
                m_voices[i].sustained = true;
            } else {
                writeReg(0x08, static_cast<uint8_t>(i));
                m_voices[i].active = false;
                m_voices[i].sustained = false;
                m_voices[i].keyOnDelay = 0;  // cancel pending KeyOn
                // Pitch EG release: glide toward PL3 while the OPM
                // release phase rings out (updatePitchEG keeps
                // processing stage 2 even for inactive voices).
                if (m_voices[i].pegStage < 2) m_voices[i].pegStage = 2;
            }
        }
    }
}

// ===========================================================================
// processMidi — Lock-free SPSC enqueue of MIDI events.
// Called from MIDI/main thread. Events are consumed by processMidiBuffer()
// on the audio thread. No locks needed — single producer, single consumer.
// ===========================================================================
void COPMEmu::processMidi(uint8_t* data, int size)
{
    // Active-Sensing watchdog: ANY incoming MIDI data counts as
    // activity and resets the 300 ms timeout on the audio thread.
    if (size > 0) {
        m_midiActivity.store(true, std::memory_order_release);
    }

    int i = 0;
    while (i < size) {
        uint8_t status = data[i];
        uint8_t msgLen = 0;

        if ((status & 0xF0) == 0x90 || (status & 0xF0) == 0x80 || (status & 0xF0) == 0xB0) {
            msgLen = 3;
        }
        else if ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0) {
            msgLen = 2;  // Program Change / Channel Pressure
        }
        else if (status == 0xF8 || status == 0xFE || status == 0xFC) {
            msgLen = 1;  // Realtime
            if (status == 0xFE) {
                // Active Sensing: arm the audio-thread watchdog.
                m_activeSenseSeen.store(true, std::memory_order_release);
            }
        }
        else if (status == 0xF0) {
            // SysEx: find length by scanning for 0xF7
            int j = i;
            while (j < size && data[j] != 0xF7) ++j;
            if (j < size && data[j] == 0xF7) {
                handleSysex(&data[i], j - i + 1);
                i = j + 1;
            } else {
                ++i;  // incomplete SysEx, skip
            }
            continue;
        }
        else {
            ++i;
            continue;
        }

        if (i + msgLen > size) break;

        // Buffer Note On/Off, CC (sustain), and Program Change
        if ((status & 0xF0) == 0x90 || (status & 0xF0) == 0x80 || (status & 0xF0) == 0xB0) {
            int writePos = m_midiWritePos.load(std::memory_order_relaxed);
            int nextPos = writePos + 1;
            if (nextPos - m_midiReadPos.load(std::memory_order_acquire) < kMidiRingSize) {
                m_midiRing[writePos & kMidiRingMask] = { status, data[i + 1], data[i + 2] };
                m_midiWritePos.store(nextPos, std::memory_order_release);
            }
        }
        else if ((status & 0xF0) == 0xC0) {
            // Program Change: store in atomic for thread-safe handoff
            setCurrentProgram(data[i + 1]);
        }

        i += msgLen;
    }
}

// ===========================================================================
// updateActiveSensing — MIDI Active-Sensing (0xFE) watchdog.
// Called from processBlock (audio thread). After the first 0xFE the
// original DX21 expects MIDI data at least every ~300 ms; on timeout
// it performs the same processing as All Notes Off (CC#123) and
// disarms until the next 0xFE. No heap, two lock-free atomics —
// safe for the realtime path.
// ===========================================================================
void COPMEmu::updateActiveSensing(int numSamples)
{
    if (m_activeSenseSeen.exchange(false, std::memory_order_acq_rel)) {
        m_asArmed = true;
    }
    const bool activity =
        m_midiActivity.exchange(false, std::memory_order_acq_rel);

    if (!m_asArmed) return;

    if (activity) {
        m_asIdleSamples = 0;
        return;
    }

    m_asIdleSamples += static_cast<uint32_t>(numSamples);
    if (m_asIdleSamples < kActiveSenseTimeout) return;

    // Timeout: mirror the CC#123 All-Notes-Off handling, then disarm
    // until the next 0xFE (like the original firmware).
    for (int v = 0; v < kNumVoices; ++v) {
        if (m_voices[v].active) {
            writeReg(0x08, static_cast<uint8_t>(v));
            m_voices[v].active = false;
            m_voices[v].sustained = false;
            m_voices[v].keyOnDelay = 0;
            if (m_voices[v].pegStage < 2) m_voices[v].pegStage = 2;
        }
    }
    m_asArmed = false;
    m_asIdleSamples = 0;
}

// ===========================================================================
// processMidiBuffer — Consume MIDI events from the lock-free ring buffer.
// Called from the audio thread (processBlock). Single consumer of the SPSC
// queue — no locks needed.
// ===========================================================================
void COPMEmu::processMidiBuffer()
{
    // Check for pending program change from main thread
    int prog = m_pendingProgram.load(std::memory_order_acquire);
    if (prog >= 0) {
        m_pendingProgram.store(-1, std::memory_order_release);
        applyProgramChange(prog);
    }

    // Apply pending SysEx parameter changes
    applySysexChanges();

    int readPos  = m_midiReadPos.load(std::memory_order_relaxed);
    int writePos = m_midiWritePos.load(std::memory_order_acquire);
    int count = writePos - readPos;
    // Safety: if count looks unreasonable, something went wrong
    if (count < 0 || count > kMidiRingSize) count = 0;

    // Stagger noteOns: process at most 2 per audio tick to avoid
    // register-write burst clicks. Defer remaining to next processBlock().
    int noteOnsThisTick = 0;
    const int kMaxNoteOnsPerTick = 2;

    for (int i = 0; i < count; ++i) {
        MidiEvent ev = m_midiRing[(readPos + i) & kMidiRingMask];
        uint8_t status = ev.status & 0xF0;

        if (status == 0x90) {
            if (ev.data2 > 0) {
                if (noteOnsThisTick < kMaxNoteOnsPerTick) {
                    noteOn(ev.data1, ev.data2);
                    noteOnsThisTick++;
                } else {
                    // Defer: push back to ring buffer for next tick
                    int writePos = m_midiWritePos.load(std::memory_order_relaxed);
                    int nextPos = writePos + 1;
                    if (nextPos - m_midiReadPos.load(std::memory_order_acquire) < kMidiRingSize) {
                        m_midiRing[writePos & kMidiRingMask] = ev;
                        m_midiWritePos.store(nextPos, std::memory_order_release);
                    }
                }
            } else {
                noteOff(ev.data1);
            }
        }
        else if (status == 0x80) {
            noteOff(ev.data1);
        }
        else if (status == 0xB0 && ev.data1 == 64) {
            // FUNCTION #31 "Foot Sustain": when OFF, the sustain
            // pedal is ignored entirely.
            if (!m_footSustainOn) continue;
            bool pedalOn = (ev.data2 >= 64);
            if (m_sustainPedal && !pedalOn) {
                for (int v = 0; v < kNumVoices; ++v) {
                    if (m_voices[v].active && m_voices[v].sustained) {
                        writeReg(0x08, static_cast<uint8_t>(v));
                        m_voices[v].active = false;
                        m_voices[v].sustained = false;
                        // Pitch EG release (see noteOff)
                        if (m_voices[v].pegStage < 2) m_voices[v].pegStage = 2;
                    }
                }
            }
            m_sustainPedal = pedalOn;
        }
        else if (status == 0xB0 && ev.data1 == 1) {
            // Modulation Wheel (CC#1). The DX21 B8 function
            // (m_mwPitchRange, 0..99) sets the maximum depth of
            // pitch modulation the wheel can apply; B9
            // (m_mwAmpRange, 0..99) does the same for AMD.
            // MW and BC contributions are summed in
            // updateLFOModDepth(), like the original firmware.
            m_mwValue = ev.data2;
            updateLFOModDepth();
        }
        else if (status == 0xB0 && ev.data1 == 2) {
            // Breath Controller
            handleBreath(ev.data2);
        }
        else if (status == 0xB0 && ev.data1 == 5) {
            // Portamento Time (manual: reception data 4-1-1 (3)).
            setPortamentoRate((ev.data2 * 99) / 127);
        }
        else if (status == 0xB0 && ev.data1 == 7) {
            // Foot Volume (manual: CC#7). FUNCTION B6 sets the
            // control RANGE 0..99; the effective gain is computed
            // in processBlock from m_footVolume + m_footVolumeRange.
            if (m_footVolumeRange > 0) {
                m_footVolume = ev.data2;
            }
        }
        else if (status == 0xB0 && ev.data1 == 65) {
            // Portamento footswitch (manual: CC#65). Only honoured
            // when FUNCTION B5 "Foot Porta" enables the jack.
            if (m_footPortaOn) {
                m_portaSwitch = (ev.data2 >= 64);
            }
        }
        else if (status == 0xB0 && ev.data1 == 123) {
            // All Notes Off (channel mode message, manual 4-1-2 a).
            for (int v = 0; v < kNumVoices; ++v) {
                if (m_voices[v].active) {
                    writeReg(0x08, static_cast<uint8_t>(v));
                    m_voices[v].active = false;
                    m_voices[v].sustained = false;
                    m_voices[v].keyOnDelay = 0;
                    if (m_voices[v].pegStage < 2) m_voices[v].pegStage = 2;
                }
            }
        }
        else if (status == 0xB0 && (ev.data1 == 126 || ev.data1 == 127)) {
            // MONO mode ON (126) / POLY mode ON (127) — manual
            // 4-1-2 b, received only when MIDI CH INFO is ON (the
            // CH-INFO gate sits in the device layer).
            setMono(ev.data1 == 126);
        }
        else if (status == 0xE0) {
            // Pitch Bend: 14-bit value (data2 MSB, data1 LSB)
            int pb = (ev.data2 << 7) | ev.data1;  // 0..16383
            setPitchBend(pb);
        }
    }

    if (count > 0) {
        m_midiReadPos.store(readPos + count, std::memory_order_release);
    }
}

// ===========================================================================
// processBlock — Audio rendering
//
// MIDI-Events werden zuerst verarbeitet. Jeder writeReg/clockChip-Cycle, der
// dabei vom Chip produziert wird, landet im Pending-Ringpuffer (m_pendingL/R).
// Anschließend mischt processBlock pro Output-Sample exakt kCyclesPerSample
// (mit Fractional-Accumulator) DAC-Werte — zuerst aus dem Pending-Puffer,
// dann durch direktes Clocken des Chips, sobald der Puffer leer ist.
//
// Dadurch wird der Audio-Stream während der writeReg-Phase nicht mehr zu
// einem DC-Mittelwert kollabiert (was zuvor ein hörbares Plateau und damit
// einen Klick beim Notenwechsel verursachte), sondern als echte Wellenform
// mit korrekter Sample-Rate-Decimation übernommen.
//
// Ein 2nd-order Butterworth-Tiefpass bei 10 kHz emuliert den analogen
// Rekonstruktionsfilter am DAC-Ausgang des YM2151/YM2164.
// ===========================================================================
void COPMEmu::processBlock(float* outputL, float* outputR, int numSamples)
{
    // Anti-clipping: the YM2151/YM2164 DAC has limited dynamic range (≈±32K).
    // With 8 active voices, the internal mix[] accumulator overflows the DAC's
    // range, causing hard digital clipping INSIDE the DAC. kScale is applied
    // AFTER clipping, so reducing it only makes the signal quieter — the
    // distortion remains. To fix this, we right-shift the mix[] accumulation
    // by 2 bits (mix_div=2) before it reaches the DAC, gaining 12 dB of
    // headroom. kScale compensates so the output level stays the same.
    //
    // We use a FIXED mix_div=2 (not dynamic) to avoid audible artifacts
    // from gain jumps when the voice count changes. The 2-bit quantization
    // noise is ≈-120 dB — far below the DAC's own ≈-78 dB noise floor.
    static const uint32_t kFracThreshold = 2 * kSampleRate;  // 96000
    int32_t dac[2];

    m_chip.mix_div = 2;  // constant: 12 dB headroom for all voice counts

    // Base scale compensated for mix_div=2: 4/(32768*4) = 1/32768.
    // Single voice at max output ≈ -18 dBFS, 8 voices ≈ 0 dBFS (no clipping).
    static const float kScale = 1.0f / 32768.0f;

    // MIDI-Events verarbeiten. Die writeReg-Aufrufe darin hängen neue
    // DAC-Samples an den bestehenden Pending-Puffer an. Getragene Daten
    // aus dem vorherigen processBlock-Aufruf werden zuerst konsumiert.
    processMidiBuffer();
    updateActiveSensing(numSamples);
    updatePortamento(numSamples);
    updatePitchEG(numSamples);

    // Process pending KeyOns: countdown and send when delay expires.
    // This gives the TL ramp time to reach near-max attenuation before
    // the new note starts, eliminating the note-on click.
    for (int v = 0; v < kNumVoices; ++v) {
        if (m_voices[v].keyOnDelay > 0) {
            m_voices[v].keyOnDelay -= numSamples;
            if (m_voices[v].keyOnDelay <= 0) {
                m_voices[v].keyOnDelay = 0;
                completeKeyOn(v);
            }
        }
    }

    int src = 0;  // Leseposition im Pending-Puffer
    for (int i = 0; i < numSamples; ++i) {
        // Cycles für diesen Output-Sample bestimmen (mit Fractional-Accum).
        int cycles = kCyclesPerSample;
        m_cycleAccum += kCycleFracStep;
        if (m_cycleAccum >= kFracThreshold) {
            cycles++;
            m_cycleAccum -= kFracThreshold;
        }

        int32_t sumL = 0, sumR = 0;
        int taken = 0;

        // Erst aus dem Pending-Puffer ziehen (Audio aus writeReg-Cycles).
        while (taken < cycles && src < m_pendingCount) {
            sumL += m_pendingL[src];
            sumR += m_pendingR[src];
            src++;
            taken++;
        }
        // Fehlende Cycles direkt vom Chip clocken (kein Eintrag in den
        // Puffer — wir schreiben direkt in das Output-Sample).
        while (taken < cycles) {
            OPM_Clock(&m_chip, dac, nullptr, nullptr, nullptr);
            sumL += dac[0];
            sumR += dac[1];
            taken++;
        }

        float rawL = static_cast<float>(sumL) / cycles * kScale;
        float rawR = static_cast<float>(sumR) / cycles * kScale;
        // FUNCTION B6 "Foot Volume": CC#7 scales the output level
        // within the configured control range (0 = pedal off,
        // 99 = full span down to silence):
        //   gain = 1 - (range/99) * (1 - cc7/127)
        // Plain int reads — the MIDI consumer (processMidiBuffer)
        // runs on this same audio thread, so there is no race.
        const float footGain = 1.0f -
            (static_cast<float>(m_footVolumeRange) / 99.0f) *
            (1.0f - static_cast<float>(m_footVolume) / 127.0f);
        outputL[i] = rawL * m_masterGain * footGain;
        outputR[i] = rawR * m_masterGain * footGain;
    }

    // --- Clip Detection ---
    // Debug: find the maximum absolute sample value to locate clipping source.
    float maxAbs = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        float aL = fabsf(outputL[i]);
        float aR = fabsf(outputR[i]);
        if (aL > maxAbs) maxAbs = aL;
        if (aR > maxAbs) maxAbs = aR;
    }
    if (maxAbs > 1.0f) {
        // This should never happen with mix_div=2 and m_masterGain=1.0f.
        // If it does, the clipping is inside the OPM chip or in the filter.
        static float peakMax = 0.0f;
        if (maxAbs > peakMax) peakMax = maxAbs;
        // Print once every ~10 seconds to avoid spam
        static int clipReport = 0;
        if (++clipReport >= 480000) {
            printf("[CLIP] peak=%.3f (max ever=%.3f) in %d samples\n",
                   maxAbs, peakMax, numSamples);
            clipReport = 0;
            peakMax = 0.0f;
        }
    }

    // --- Ensemble/Chorus effect & Final DSP Stage ---
    // DX21-style stereo chorus: 3 modulated delay lines with 120° phase offsets.
    // LFO1 ~0.5 Hz (slow vibrato), LFO2 ~3 Hz (fast shimmer, lower depth).
    // The mix of 85% slow + 15% fast creates a lush, organic chorus sound.
    //
    // IMPORTANT: LFO phases are in RADIANS (0..2pi per cycle) so sinf() works
    // correctly.  
    if (m_ensembleOn) {
        static constexpr float kTwoPi      = 6.2831853071795864f;
        static constexpr float kLfo1Freq   = 0.5f;
        static constexpr float kLfo2Freq   = 3.0f;
        static constexpr float kLfo1Inc    = kLfo1Freq * kTwoPi / kSampleRate;
        static constexpr float kLfo2Inc    = kLfo2Freq * kTwoPi / kSampleRate;
        static constexpr float kPhaseOffset1 = kTwoPi / 3.0f;
        static constexpr float kPhaseOffset2 = kTwoPi * 2.0f / 3.0f;
        static constexpr float kBaseDelay = 250.0f;
        static constexpr float kModDepth   = 85.0f;

        for (int i = 0; i < numSamples; ++i) {
            float modA = 0.85f * sinf(m_lfo1Phase)                + 0.15f * sinf(m_lfo2Phase);
            float modB = 0.85f * sinf(m_lfo1Phase + kPhaseOffset1) + 0.15f * sinf(m_lfo2Phase + kPhaseOffset1);
            float modC = 0.85f * sinf(m_lfo1Phase + kPhaseOffset2) + 0.15f * sinf(m_lfo2Phase + kPhaseOffset2);

            m_delayA.setDelay(kBaseDelay + modA * kModDepth);
            m_delayB.setDelay(kBaseDelay + modB * kModDepth);
            m_delayC.setDelay(kBaseDelay + modC * kModDepth);

            float dryL = outputL[i];
            float dryR = outputR[i];

            m_delayA.pushSample(dryL);
            m_delayB.pushSample(dryL);
            m_delayC.pushSample(dryR);

            // 1. Chorus Mix erstellen
            float choL = dryL * 0.5f + m_delayA.popSample() * 0.5f - m_delayB.popSample() * 0.3f;
            float choR = dryR * 0.5f + m_delayC.popSample() * 0.5f - m_delayB.popSample() * 0.3f;

            // 2. REKONSTRUKTIONSFILTER ERST HIER ANWENDEN (Fängt Chorus-Artefakte ab)
            float filteredL = m_filterL2.process(m_filterL1.process(choL));
            float filteredR = m_filterR2.process(m_filterR1.process(choR));

            // 3. DC BLOCKER AM ABSOLUTEN ENDE
            float outL = filteredL - m_dcLastInL + 0.998f * m_dcLastOutL;
            float outR = filteredR - m_dcLastInR + 0.998f * m_dcLastOutR;
            
            m_dcLastInL = filteredL; m_dcLastOutL = outL;
            m_dcLastInR = filteredR; m_dcLastOutR = outR;

            outputL[i] = outL;
            outputR[i] = outR;

            m_lfo1Phase += kLfo1Inc; if (m_lfo1Phase >= kTwoPi) m_lfo1Phase -= kTwoPi;
            m_lfo2Phase += kLfo2Inc; if (m_lfo2Phase >= kTwoPi) m_lfo2Phase -= kTwoPi;
        }
    } else {
        // Fallback: Wenn Chorus AUS ist, Filter und DC-Blocker direkt nacheinander ausführen
        for (int i = 0; i < numSamples; i++) {
            float filteredL = m_filterL2.process(m_filterL1.process(outputL[i]));
            float filteredR = m_filterR2.process(m_filterR1.process(outputR[i]));

            float outL = filteredL - m_dcLastInL + 0.998f * m_dcLastOutL;
            float outR = filteredR - m_dcLastInR + 0.998f * m_dcLastOutR;
            
            m_dcLastInL = filteredL; m_dcLastOutL = outL;
            m_dcLastInR = filteredR; m_dcLastOutR = outR;
            
            outputL[i] = outL;
            outputR[i] = outR;
        }
    }

    // Unverbrauchte Pending-Daten an den Anfang des Puffers umkopieren,
    // damit sie im nächsten processBlock-Aufruf weiter konsumiert werden.
    // Ohne diesen Schritt würden bei kleinen Audio-Buffern (z.B. 128 Samples)
    // die OPM-Cycles aus MIDI-Verarbeitung (Program-Change: ~3300 Cycles,
    // NoteOn: ~440-800 Cycles) verloren gehen — hörbar als verschluckte Töne.
    int remaining = m_pendingCount - src;
    if (remaining > 0) {
        memmove(m_pendingL, m_pendingL + src, remaining * sizeof(int32_t));
        memmove(m_pendingR, m_pendingR + src, remaining * sizeof(int32_t));
    }
    m_pendingCount = remaining;
}

// ===========================================================================
// Patch-Auswahl
// ===========================================================================
int  COPMEmu::getNumPrograms()              { return TOTAL_OPM_PATCHES; }
int  COPMEmu::getCurrentProgram()         { return m_currentPatch; }
const char* COPMEmu::getCurrentProgramName() { const DX21_Patch* p = getPatch(m_currentPatch); return p ? p->name : ""; }
const char* COPMEmu::getProgramName(int index) {
    if (index >= 0 && index < TOTAL_OPM_PATCHES)
        { const DX21_Patch* p = getPatch(index); return p ? p->name : ""; }
    return "";
}

void COPMEmu::setEnsembleOn(bool on)
{
    m_ensembleOn = on;
    if (on) {
        // Reset delay lines and LFO phases for clean start
        m_delayA.reset();
        m_delayB.reset();
        m_delayC.reset();
        m_lfo1Phase = 0.0f;
        m_lfo2Phase = 0.0f;
    }
}

bool COPMEmu::getEnsembleOn() { return m_ensembleOn; }

void COPMEmu::setCurrentProgram(int index)
{
    // Thread-safe: store the program number in an atomic variable.
    // The audio thread (processMidiBuffer) will pick it up and apply
    // the register writes, avoiding race conditions on the OPM chip state.
    if (index >= 0 && index < TOTAL_OPM_PATCHES) {
        m_pendingProgram.store(index, std::memory_order_release);
    }
}

// ===========================================================================
// applyProgramChange — Apply a pending program change on the audio thread.
// Writes LFO registers for the new patch. This must only be called from
// the audio thread (processMidiBuffer) to avoid race conditions on m_chip.
// ===========================================================================
void COPMEmu::applyProgramChange(int index)
{
    const int oldStartB = voiceStart(SideB);
    m_currentPatch = index;
    m_patchA = index;

    // The SysEx/panel edit target follows the current program when it
    // is a RAM voice (0..31). ROM programs (32..127) are read-only:
    // -1 makes the writeVced*/applySysexParam guards reject edits
    // until the user loads the voice into RAM (A11/A12).
    m_sysexEditVoice = (index < CDX21Memory::kNumRamVoices) ? index : -1;

    // Selecting a voice discards the edit/compare context (the manual
    // is explicit: leaving EDIT and pressing a voice selector erases
    // the edit buffer — EDIT RECALL is the safety net).
    m_bCompare = false;
    m_bEditDirty = false;

    // SPLIT: the new side-A patch can move the poly/mono boundary —
    // rebuild the layout (frees voices) instead of in-place re-apply.
    if (m_playMode == Split && voiceStart(SideB) != oldStartB) {
        refreshVoiceLayout();
        return;
    }

    applyPatchToSide(SideA, index);
    // In Single mode side B has no voices; applyPatchToSide handles count==0.
    if (m_playMode != Single) {
        applyPatchToSide(SideB, m_patchB);
    }
}


// ===========================================================================
// setupVoice — configure a single voice for note-on
// ===========================================================================
void COPMEmu::setupVoice(int voice, int note, int velocity, const DX21_Patch& patch)
{
    int transposed = note + patch.key_offset;
    if (transposed < 0) transposed = 0;
    if (transposed > 127) transposed = 127;

    float pitch = static_cast<float>(transposed);

    m_voices[voice].active = true;
    m_voices[voice].sustained = false;
    m_voices[voice].origNote = note;
    m_voices[voice].note = transposed;
    m_voices[voice].velocity = velocity;
    m_voices[voice].age = m_voiceAge++;

    // --- Pitch EG: copy the effective PEG from the patch into the
    // voice so the audio loop never has to look up the patch. A flat
    // PEG (all levels 50, incl. the legacy all-zero sentinel) parks
    // the EG in the idle stage so updatePitchEG() skips the voice.
    {
        Voice& v = m_voices[voice];
        bool flat = true;
        for (int i = 0; i < 3; ++i) {
            v.pegR[i] = patch.peg_r[i] > 99 ? 99 : patch.peg_r[i];
            v.pegL[i] = dx21_effective_peg_level(&patch, i);
            if (v.pegL[i] != 50) flat = false;
        }
        if (flat) {
            v.pegLevel = 50.0f;
            v.pegStage = 3;          // idle: no pitch offset, no work
        } else {
            v.pegLevel = static_cast<float>(v.pegL[2]);  // start at PL3
            v.pegStage = 0;          // attack: move toward PL1
        }
    }

    // Portamento: if active and voice already has a pitch, glide from
    // there. CC#65 (portamento footswitch, gated by FUNCTION B5) can
    // veto the effect via m_portaSwitch.
    bool shouldPorta = (m_portaMode != PortaOff) && m_portaSwitch;
    if (shouldPorta && m_voices[voice].currentPitch != 0.0f) {
        if (m_portaMode == PortaFingered) {
            // Fingered: only portamento if legato (voice was already active)
            // We already know currentPitch != 0, but check if it was recently active
            // In our model: if currentPitch is set and voice was not just stolen,
            // we treat it as legato.
            m_voices[voice].targetPitch = pitch;
            m_voices[voice].isPorting = true;
        } else {
            // Full Time: always portamento
            m_voices[voice].targetPitch = pitch;
            m_voices[voice].isPorting = true;
        }
    } else {
        // No portamento: snap immediately
        m_voices[voice].currentPitch = pitch;
        m_voices[voice].targetPitch = pitch;
        m_voices[voice].isPorting = false;
    }

    // Pre-KeyOn mute: set TL to max attenuation with OPP ramp.
    // This ramps the output toward silence before the delayed KeyOn arrives.
    // Combined with the free-running oscillator (no pg_inc masking during
    // KeyOff), this ensures a smooth transition without phase freeze clicks.
    // The KeyOn is delayed by kKeyOnDelay samples to give the TL ramp time
    // to reach near-max attenuation, eliminating the note-on click.
    for (int op = 0; op < 4; ++op) {
        writeReg(0x60 + voice + OP_SLOT[op], 0xFF);  // ramp mute (soft)
    }

    // Delay KeyOn: store pitch/TL data and send it after kKeyOnDelay samples.
    // During the delay, the TL ramps toward max attenuation and the EG starts
    // releasing, so the voice is nearly silent when KeyOn finally arrives.
    m_voices[voice].keyOnDelay = kKeyOnDelay;


}

// ===========================================================================
// completeKeyOn — send delayed KeyOn after TL ramp has reached silence.
// Called from processBlock when the keyOnDelay countdown reaches 0.
// ===========================================================================
void COPMEmu::completeKeyOn(int voice)
{
    if (voice < 0 || voice >= kNumVoices) return;
    if (!m_voices[voice].active) return;

    const DX21_Patch* patch = nullptr;
    Side side = sideOfVoice(voice);
    if (m_playMode == Single) {
        patch = getPatch(m_patchA);
    } else {
        patch = (side == SideA) ? getPatch(m_patchA) : getPatch(m_patchB);
    }
    if (!patch) return;

    int transposed = m_voices[voice].note;

    // Compute balance TL offset based on side
    int tlOffset = 0;
    if (m_playMode != Single && m_balance != 50) {
        if (m_balance < 50 && side == SideA) {
            tlOffset = ((50 - m_balance) * 127) / 50;
        } else if (m_balance > 50 && side == SideB) {
            tlOffset = ((m_balance - 50) * 127) / 50;
        }
    }

    // Step 1: Write KC/KF WITHOUT KeyOn. applyPitchToVoice folds in
    // pitch bend, breath bias and the Pitch-EG start level (PL3), so
    // a PEG voice starts at the right pitch instead of jumping after
    // the first audio block.
    applyPitchToVoice(voice);

    // Step 2: Apply target TL with OPP ramp (fade-in from silence)
    applyTL(voice, *patch, transposed, m_voices[voice].velocity, tlOffset);

    // Step 3: KeyOn LAST — operators start with TL near max, ramp fading in
    writeReg(0x08, OP_ALL | static_cast<uint8_t>(voice));
}

// ===========================================================================
// applyPatchToSide — write static patch parameters to all voices of a side
// ===========================================================================
void COPMEmu::applyPatchToSide(Side side, int patchIndex)
{
    if (patchIndex < 0 || patchIndex >= TOTAL_OPM_PATCHES) return;
    const DX21_Patch* patch = getPatch(patchIndex);
    if (!patch) return;

    int start = voiceStart(side);
    int count = voiceCount(side);
    for (int v = start; v < start + count; ++v) {
        applyPatchToVoice(v, *patch);  // patch is pointer here
    }

    // Global LFO registers (only written once, not per voice)
    if (count > 0) {
        writeReg(0x18, patch->lfo_speed);
        writeReg(0x19, patch->amd & 0x7F);            // AMD
        writeReg(0x19, (patch->pmd & 0x7F) | 0x80);   // PMD (bit 7=1)
        writeField(0x1B, 0, 2, patch->lfo_wave & 0x03);
    }
}

// ===========================================================================
// routeNoteToSide — determine which side a MIDI note belongs to in SPLIT mode
// ===========================================================================
COPMEmu::Side COPMEmu::routeNoteToSide(int note) const
{
    if (m_playMode != Split) return SideA;
    return (note <= m_splitPoint) ? SideA : SideB;
}

// ===========================================================================
// voiceStart / voiceCount — voice allocation ranges per side and play mode
// ===========================================================================
int COPMEmu::voiceStart(Side side) const
{
    if (side == SideA) return 0;
    // SPLIT: side B starts right after side A's (dynamic) range so
    // the 7+1 / 1+7 / 1+1 layouts pack the 8 chip channels densely.
    if (m_playMode == Split) return voiceCount(SideA);
    return 4;  // DUAL: side B always starts at voice 4
}

int COPMEmu::voiceCount(Side side) const
{
    switch (m_playMode) {
    case Single:
        // In MONO mode, the synth plays at most one note at a time.
        // We reflect this in voiceCount so that allocVoice's free-
        // the-single-slot path triggers correctly (count == 1).
        if (side != SideA) return 0;
        return m_mono ? 1 : 8;
    case Dual:
        return 4;
    case Split: {
        // DX21 MONO+POLY mix: poly/mono is a voice (VCED 63) flag,
        // so each split side follows its own patch. A MONO side
        // gets 1 note, the POLY partner the remaining 7 ("7+1").
        // Both MONO → 1+1; both POLY → classic 4+4.
        const bool aMono = sideIsMono(SideA);
        const bool bMono = sideIsMono(SideB);
        if (aMono && bMono) return 1;
        if (aMono) return (side == SideA) ? 1 : 7;
        if (bMono) return (side == SideA) ? 7 : 1;
        return 4;
    }
    }
    return 0;
}

bool COPMEmu::sideIsMono(Side side) const
{
    const DX21_Patch* p = getPatch(side == SideA ? m_patchA : m_patchB);
    return p && (p->mono & 0x01);
}

// ===========================================================================
// refreshVoiceLayout — free all voices and re-apply both side patches.
// Called whenever the SPLIT voice layout may have changed (patch select
// or a poly/mono edit): active voices could otherwise sit outside the
// new side ranges and keep ringing with stale patch data.
// ===========================================================================
void COPMEmu::refreshVoiceLayout()
{
    for (int i = 0; i < kNumVoices; ++i) {
        if (m_voices[i].active) freeVoice(i);
    }
    applyPatchToSide(SideA, m_patchA);
    if (m_playMode != Single) applyPatchToSide(SideB, m_patchB);
}

// ===========================================================================
// Performance Parameter Setters
// ===========================================================================
void COPMEmu::setPlayMode(PlayMode mode)
{
    if (m_playMode == mode) return;

    // Free all voices before switching modes to avoid stale state
    for (int i = 0; i < kNumVoices; ++i) {
        if (m_voices[i].active) {
            freeVoice(i);
        }
    }
    m_playMode = mode;

    // Re-apply patches for the new mode
    applyPatchToSide(SideA, m_patchA);
    applyPatchToSide(SideB, m_patchB);
}

void COPMEmu::setSplitPoint(int note)
{
    m_splitPoint = note;
}

void COPMEmu::setBalance(int balance)
{
    if (balance < 0) balance = 0;
    if (balance > 99) balance = 99;
    m_balance = balance;

    // Re-apply TL with new balance offset for all active voices
    if (m_playMode == Single) return;

    for (int v = 0; v < kNumVoices; ++v) {
        if (!m_voices[v].active) continue;

        Side side = sideOfVoice(v);
        int tlOffset = 0;
        if (m_balance < 50 && side == SideA) {
            tlOffset = ((50 - m_balance) * 127) / 50;
        } else if (m_balance > 50 && side == SideB) {
            tlOffset = ((m_balance - 50) * 127) / 50;
        }

        // Determine patch for this voice
        int patchIdx = (side == SideA) ? m_patchA : m_patchB;
        if (patchIdx < 0 || patchIdx >= TOTAL_OPM_PATCHES) continue;
        const DX21_Patch* patch = getPatch(patchIdx);
        if (!patch) continue;

        applyTL(v, *patch, m_voices[v].note, m_voices[v].velocity, tlOffset);
    }
}

void COPMEmu::setMono(bool mono)
{
    if (m_mono == mono) return;

    // Free all voices before switching poly/mono
    for (int i = 0; i < kNumVoices; ++i) {
        if (m_voices[i].active) {
            freeVoice(i);
        }
    }
    m_mono = mono;
}

void COPMEmu::setPatchA(int index)
{
    if (index < 0 || index >= TOTAL_OPM_PATCHES) return;
    const int oldStartB = voiceStart(SideB);
    m_patchA = index;
    if (m_playMode == Single) {
        m_currentPatch = index;  // backward compat
    }
    // SPLIT: a new patch can flip the side's poly/mono flag and move
    // the 7+1 boundary — rebuild the whole layout in that case.
    if (m_playMode == Split && voiceStart(SideB) != oldStartB) {
        refreshVoiceLayout();
        return;
    }
    applyPatchToSide(SideA, index);
}

void COPMEmu::setPatchB(int index)
{
    if (index < 0 || index >= TOTAL_OPM_PATCHES) return;
    const int oldStartB = voiceStart(SideB);
    m_patchB = index;
    if (m_playMode == Split && voiceStart(SideB) != oldStartB) {
        refreshVoiceLayout();
        return;
    }
    applyPatchToSide(SideB, index);
}

void COPMEmu::setMasterTune(int cents)
{
    if (cents < -64) cents = -64;
    if (cents > 63) cents = 63;
    m_masterTune = cents;
}


// ===========================================================================
// computePortaDecayFactor — map DX21 rate (0..99) to per-sample decay
// multiplier for exponential portamento.
//
// The real DX21's portamento slide is approximately exponential in
// time (per the owner's manual: "the relationship is exponential, not
// linear"). With rate=99, a one-octave slide takes about 15 seconds.
//
// We model this as an exponential decay of the pitch gap:
//
//   gap(n+1) = gap(n) × decay_per_sample
//   new_pitch = target - new_gap
//
// where decay_per_sample is a value just below 1.0 (closer to 1.0 = slower
// slide). With this scheme, the gap halves every "T_half" seconds,
// where T_half = -ln(0.5) / ln(decay_per_sample) / sample_rate.
//
// Mapping rate 0..99 to T_half:
//   rate = 0   → T_half = 0    (no portamento, instant snap)
//   rate = 99  → T_half ≈ 2.5s (one octave ≈ 5x half-life ≈ 12.5s)
//
// The relationship rate → T_half is linear so that low rates (1..30)
// feel snappier and high rates (70..99) feel smoother, which matches
// what a player expects from a "portamento time" knob.
float COPMEmu::computePortaDecayFactor(int rate) const
{
    if (rate <= 0) return 0.0f;

    // Linear mapping: rate 1 → T_half=0.025s, rate 99 → T_half=2.5s.
    const float t_half_max = 2.5f;  // seconds at rate=99
    const float t_half     = (rate / 99.0f) * t_half_max;

    // decay_per_sample = 0.5 ^ (1 / (T_half * sample_rate))
    return std::pow(0.5f, 1.0f / (t_half * (float)kSampleRate));
}

// ===========================================================================
// updatePortamento — interpolate pitch and write KC/KF for porting voices
// Called from processBlock (audio thread) after MIDI processing.
// ===========================================================================
void COPMEmu::updatePortamento(int numSamples)
{
    if (m_portaMode == PortaOff) return;

    for (int v = 0; v < kNumVoices; ++v) {
        if (!m_voices[v].active || !m_voices[v].isPorting) continue;

        float delta = m_voices[v].targetPitch - m_voices[v].currentPitch;
        if (std::abs(delta) < 0.001f) {
            m_voices[v].currentPitch = m_voices[v].targetPitch;
            m_voices[v].isPorting = false;
            applyPitchToVoice(v);
            continue;
        }

        // Exponential decay of the gap, applied per-block (updatePortamento
        // is called once per processBlock, not per sample).
        //
        // The per-sample decay factor is m_portaDecayFactor. For a
        // block of N samples, the effective per-block decay is:
        //   effective = m_portaDecayFactor ^ N
        // which we compute via std::pow to avoid overflow / drift.
        //
        // This makes the glide speed independent of the audio buffer
        // size: pumping 256 samples at a time vs 64 samples at a time
        // produces the same overall glide curve.
        float effectiveDecay = std::pow(m_portaDecayFactor, (float)numSamples);
        m_voices[v].currentPitch = m_voices[v].targetPitch - (delta * effectiveDecay);
        applyPitchToVoice(v);
    }
}

// ===========================================================================
// updatePitchEG — advance the per-voice Pitch EG (VCED 87-92)
//
// The real DX21 implements the PEG in firmware by re-writing KC/KF —
// the YM2164 has no pitch envelope — and we do the same here, once
// per audio block (like updatePortamento).
//
// Stages: 0 = move toward PL1 (key on), 1 = move toward PL2 and
// sustain there, 2 = move toward PL3 (key released), 3 = idle.
// Levels are DX21 units (0..99, 50 = center, ±0.96 st per unit).
//
// Rate → speed mapping (approximation; the manual only specifies
// "99 = instantaneous, 0 = slowest"): a full 0→99 sweep takes
//   T = 10 ms * 2^((99 - rate) / 10)
// i.e. ~10 ms at rate 99 (plus the instant-snap special case) up to
// ~10 s at rate 0. Tune against the original ROM if needed.
// ===========================================================================
void COPMEmu::updatePitchEG(int numSamples)
{
    const float dt = static_cast<float>(numSamples) / static_cast<float>(kSampleRate);

    for (int v = 0; v < kNumVoices; ++v) {
        Voice& vc = m_voices[v];
        if (vc.pegStage >= 3) continue;
        // Inactive voices still run the release stage (2) so the
        // pitch glides toward PL3 while the OPM release rings out.
        if (!vc.active && vc.pegStage != 2) continue;

        const int stage = vc.pegStage;
        const float target = static_cast<float>(vc.pegL[stage == 0 ? 0 : (stage == 1 ? 1 : 2)]);
        const uint8_t rate = vc.pegR[stage];

        if (vc.pegLevel == target) {
            // Already at the stage target: advance out of transient
            // stages; stage 1 sustains at PL2 with zero work.
            if (stage == 0) vc.pegStage = 1;
            else if (stage == 2) vc.pegStage = 3;
            continue;
        }

        float next;
        if (rate >= 99) {
            next = target;  // instantaneous
        } else {
            const float sweepSec = 0.010f * std::pow(2.0f, (99.0f - rate) / 10.0f);
            const float step = (99.0f / sweepSec) * dt;  // level units this block
            if (vc.pegLevel < target) {
                next = vc.pegLevel + step;
                if (next > target) next = target;
            } else {
                next = vc.pegLevel - step;
                if (next < target) next = target;
            }
        }
        vc.pegLevel = next;

        if (next == target) {
            if (stage == 0)      vc.pegStage = 1;  // on to PL2
            else if (stage == 2) vc.pegStage = 3;  // release done
        }
        applyPitchToVoice(v);
    }
}

void COPMEmu::setMasterGain(float gain)
{
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 8.0f) gain = 8.0f;
    m_masterGain = gain;
}

// ===========================================================================
// allNotesOff — Issue KeyOff to all 8 OPM channels. Each voice's release
// phase will continue to ring out per its RR setting, but no new notes
// can be triggered. Voice bookkeeping is reset so noteOn() will not
// think any voice is "still playing" after the release is done.
// ===========================================================================
void COPMEmu::allNotesOff()
{
    // 1) Hardware: KeyOff on all 8 channels (reg 0x08, one byte per ch).
    for (int ch = 0; ch < kNumVoices; ++ch) {
        writeReg(0x08, static_cast<uint8_t>(ch));
    }

    // 2) Software: mark every voice as inactive and clear its
    //    per-voice state. Skip the MIDI ring buffer — callers that
    //    want to drop pending notes should call resetEngine() below.
    for (int v = 0; v < kNumVoices; ++v) {
        m_voices[v].active = false;
        m_voices[v].sustained = false;
        m_voices[v].note = -1;
        m_voices[v].origNote = -1;
        m_voices[v].velocity = 0;
        m_voices[v].keyOnDelay = 0;
        m_voices[v].currentPitch = 0.0f;
        m_voices[v].targetPitch  = 0.0f;
        m_voices[v].isPorting    = false;
        m_voices[v].attackSamples = 0;
        m_voices[v].pegLevel = 50.0f;
        m_voices[v].pegStage = 3;
    }
}

// ===========================================================================
// resetEngine — Full audio-pipeline shutdown. Drops in-flight notes,
// silences all OPM channels, and clears the MIDI ring buffer. Use this
// from the panic path so a subsequent power-on starts from a clean
// state (no stuck voices, no buffered NoteOns).
// ===========================================================================
// ===========================================================================
// Init Voice — Set all 76 VCED bytes to the "Initialized Voice" factory
// default and re-apply to the OPM. Comparable to the DX21's "Init. Voice ?"
// function (A10 in the FUNCTION parameter list).
//
// The defaults are tuned to give a simple sine-like bell: a single carrier
// with short decay, no modulation, no LFO. This is the canonical "empty
// patch" a sound designer starts from when creating a new voice.
// ===========================================================================
const DX21_Patch* COPMEmu::GetInitVoicePatch() {
    // Static const — initialised once at program start.
    static const DX21_Patch kInitVoice = {
        // alg, fb, lfo_speed, lfo_delay, pmd, amd, lfo_sync, lfo_wave
        0, 0, 0, 0, 0, 0, 1, 0,
        // pms, ams, key_offset
        0, 0, 0,
        // 4 operators, defaulting to a simple bell on OP1
        {
            // OP1 (carrier): ar, d1r, d2r, rr, d1l, ls, rs, ebs, ame, kvs, out, crs, det
            { 31, 20, 0, 4, 0, 0, 0, 0, 0, 0, 99,  4, 3 },
            // OP2..OP4 (modulators): muted
            { 31, 31, 0, 4, 0, 0, 0, 0, 0, 0,  0,  1, 3 },
            { 31, 31, 0, 4, 0, 0, 0, 0, 0, 0,  0,  1, 3 },
            { 31, 31, 0, 4, 0, 0, 0, 0, 0, 0,  0,  1, 3 },
        },
        "Init Voice",
        // Per-voice function data (VCED 63-76), manual "INITIALIZED
        // VOICE DATA LIST": mono off, PB range 2, porta off/0,
        // foot volume range 0, sustain+porta footswitches on,
        // chorus off, MW pitch 50, MW amp 0, BC all 0 except
        // pitch bias center 50.
        /*mono*/ 0, /*pb_range*/ 2, /*porta_mode*/ 0, /*porta_time*/ 0,
        /*foot_volume*/ 0, /*sus_fs*/ 1, /*porta_fs*/ 1, /*chorus*/ 0,
        /*mw_pitch*/ 50, /*mw_amp*/ 0,
        /*bc_pitch*/ 0, /*bc_amp*/ 0, /*bc_pbias*/ 50, /*bc_ebias*/ 0,
        // Pitch EG: instant rates, flat levels (no effect)
        /*peg_r*/ {99, 99, 99}, /*peg_l*/ {50, 50, 50},
        // All four operators enabled
        /*op_enable*/ 0x0F
    };
    return &kInitVoice;
}

void COPMEmu::initVoice() {
    int voice = m_sysexEditVoice;
    if (voice < 0 || voice >= CDX21Memory::kNumRamVoices) return;

    if (m_memoryProt) return;  // FUNCTION #23: init-voice is a write

    DX21_Patch* ram = m_memory.getRamVoice(voice);
    if (!ram) return;

    markEditDirty(ram);

    // Copy the static init patch into RAM and re-apply to every chip
    // voice currently sounding it (slot index ≠ chip channel).
    *ram = *GetInitVoicePatch();
    reapplyEditPatch(voice);
}

// ===========================================================================
// egCopy — EDIT-mode "EG Copy from OP" utility (original panel feature).
// Copies the five EG parameters from srcOp to dstOp via
// writeVcedOperator(), which handles RAM update, OPM register writes
// for all sounding channels, COMPARE dirty-marking, Memory Protect
// and the real-time parameter-change transmit.
// ===========================================================================
bool COPMEmu::egCopy(int srcOp, int dstOp) {
    if (srcOp < 0 || srcOp > 3 || dstOp < 0 || dstOp > 3) return false;
    if (srcOp == dstOp) return false;

    int voice = m_sysexEditVoice;
    if (voice < 0 || voice >= CDX21Memory::kNumRamVoices) return false;
    if (m_memoryProt) return false;  // FUNCTION #23: EG copy is a write

    const DX21_Patch* ram = m_memory.getRamVoice(voice);
    if (!ram) return false;

    // Copy the source EG by value first — writeVcedOperator mutates
    // the same patch, and src must stay untouched.
    const DX21_Operator src = ram->op[srcOp];

    // writeVcedOperator internal param ids: 0=AR, 2=D1R, 8=D1L,
    // 1=D2R, 3=RR (see the switch in writeVcedOperator).
    writeVcedOperator(dstOp, 0, src.ar);
    writeVcedOperator(dstOp, 2, src.d1r);
    writeVcedOperator(dstOp, 8, src.d1l);
    writeVcedOperator(dstOp, 1, src.d2r);
    writeVcedOperator(dstOp, 3, src.rr);
    return true;
}

// ===========================================================================
// saveEditRecall — snapshot the current edit buffer into m_editRecall.
// Called when the user first edits a parameter; subsequent edits
// overwrite the buffer, but the first edit "freezes" the pre-edit
// state. The DX21 actually only saves one snapshot per voice (no
// multi-step undo); we follow suit.
// ===========================================================================
void COPMEmu::saveEditRecall() {
    int voice = m_sysexEditVoice;
    if (voice < 0 || voice >= CDX21Memory::kNumRamVoices) return;
    if (m_memoryProt) return;  // FUNCTION #23: snapshot is a write
    DX21_Patch* ram = m_memory.getRamVoice(voice);
    if (!ram) return;
    m_editRecall       = *ram;
    m_bEditRecallValid = true;
}

// ===========================================================================
// loadEditRecall — restore the snapshot and re-apply to the OPM.
// Only valid if saveEditRecall() was called at least once.
// ===========================================================================
void COPMEmu::loadEditRecall() {
    if (!m_bEditRecallValid) return;
    if (m_memoryProt) return;  // FUNCTION #23: recall overwrites the voice
    int voice = m_sysexEditVoice;
    if (voice < 0 || voice >= CDX21Memory::kNumRamVoices) return;
    DX21_Patch* ram = m_memory.getRamVoice(voice);
    if (!ram) return;
    markEditDirty(ram);
    *ram = m_editRecall;
    // The edit slot is a RAM index, not a chip channel — re-apply to
    // every chip voice currently sounding the patch.
    reapplyEditPatch(voice);
}

void COPMEmu::resetEngine()
{
    allNotesOff();

    // 3) Force every operator's TL to maximum attenuation (127) so
    //    the OPM outputs silence even during the release phase.
    //    bit 7 of reg 0x60 is OPP TL-ramp enable — keep it set so
    //    the change is click-free.
    for (int ch = 0; ch < kNumVoices; ++ch) {
        for (int op = 0; op < 4; ++op) {
            int slot = ch + OP_SLOT[op];
            writeReg(0x60 + slot, 0x80 | 127);
        }
    }

    // 4) Drop any pending MIDI events so a re-start doesn't see
    //    NoteOns that were queued before the panic.
    m_midiWritePos.store(0, std::memory_order_release);
    m_midiReadPos.store(0,  std::memory_order_release);

    // 5) Zero the deferred SysEx queue (real-time param changes
    //    that haven't been applied yet). The opmemu.h struct doesn't
    //    expose the bulk-dump drain/fill counters, so we only clear
    //    the dirty flags here — applySysexChanges() won't reapply
    //    anything until a new SysEx arrives.
    for (int i = 0; i < kSysexNumParams; ++i) {
        m_sysexDirty[i] = false;
    }
}

// ===========================================================================
// setPitchBend — apply global pitch bend to all active voices
// MIDI pitch bend: 0..16383, center = 8192
// ===========================================================================
void COPMEmu::setPitchBend(int value)
{
    if (value < 0) value = 0;
    if (value > 16383) value = 16383;
    m_pbValue = value - 8192;  // center at 0

    if (m_pbMode == PBAll) {
        // Apply to all active voices
        for (int v = 0; v < kNumVoices; ++v) {
            if (m_voices[v].active) {
                applyPitchToVoice(v);
            }
        }
    } else {
        // Apply only to the target voice
        int target = findVoiceForPB();
        if (target >= 0) {
            applyPitchToVoice(target);
        }
    }
}

// ===========================================================================
// setPitchBendRange — 0..12 semitones
// ===========================================================================
void COPMEmu::setPitchBendRange(int range)
{
    if (range < 0) range = 0;
    if (range > 12) range = 12;
    m_pbRange = range;
}

// ===========================================================================
// setPortamentoMode — 0=Off, 1=FullTime, 2=Fingered
// ===========================================================================
void COPMEmu::setPortamentoMode(int mode)
{
    if (mode < 0 || mode > 2) return;
    PortaMode oldMode = m_portaMode;
    m_portaMode = static_cast<PortaMode>(mode);
    if (oldMode == PortaOff && m_portaMode != PortaOff) {
        m_portaDecayFactor = computePortaDecayFactor(m_portaRate);
    }
}

// ===========================================================================
// setPortamentoRate — 0..99
// ===========================================================================
void COPMEmu::setPortamentoRate(int rate)
{
    if (rate < 0) rate = 0;
    if (rate > 99) rate = 99;
    m_portaRate = rate;
    m_portaDecayFactor = computePortaDecayFactor(rate);
}


// ===========================================================================
// findVoiceForPB — find the target voice for pitch bend based on PBMode
// ===========================================================================
int COPMEmu::findVoiceForPB() const
{
    if (m_pbMode == PBAll) return -1;  // apply to all

    int target = -1;
    switch (m_pbMode) {
    case PBLow: {
        // Find active voice with lowest MIDI note
        int lowestNote = 128;
        for (int v = 0; v < kNumVoices; ++v) {
            if (m_voices[v].active && m_voices[v].origNote < lowestNote) {
                lowestNote = m_voices[v].origNote;
                target = v;
            }
        }
        break;
    }
    case PBHigh: {
        // Find active voice with highest MIDI note
        int highestNote = -1;
        for (int v = 0; v < kNumVoices; ++v) {
            if (m_voices[v].active && m_voices[v].origNote > highestNote) {
                highestNote = m_voices[v].origNote;
                target = v;
            }
        }
        break;
    }
    case PBKOn: {
        // Find active voice with highest age (most recent)
        uint32_t newestAge = 0;
        for (int v = 0; v < kNumVoices; ++v) {
            if (m_voices[v].active && m_voices[v].age >= newestAge) {
                newestAge = m_voices[v].age;
                target = v;
            }
        }
        break;
    }
    default:
        break;
    }
    return target;
}

// ===========================================================================
// handleBreath — MIDI CC#2 breath controller
// ===========================================================================
void COPMEmu::handleBreath(int value)
{
    if (value < 0) value = 0;
    if (value > 127) value = 127;
    m_breathValue = value;

    // FUNCTION #35 "BC Pitch": breath modulates the LFO pitch depth
    // (PMD), summed with the mod wheel's contribution.
    if (m_breathPitch > 0) {
        updateLFOModDepth();
    }

    // Apply breath pitch bias to active voices
    if (m_breathPitchBias > 0) {
        for (int v = 0; v < kNumVoices; ++v) {
            if (m_voices[v].active) {
                applyPitchToVoice(v);
            }
        }
    }
}

// ===========================================================================
// updateLFOModDepth — write MW+BC modulation depth to YM2151 reg 0x19
//
// MW (CC#1) and BC (CC#2) both scale the LFO depth; their contributions
// are summed and clamped to 127, like the original DX21 firmware.
//
// Note on reg 0x19: bit 7 selects the target — 1 = PMD (bits 0..6),
// 0 = AMD (bits 0..6) — so PMD and AMD need TWO separate writes. The
// previous single-write version (((pmd & 0x7F) << 7) | amd) overflowed
// the uint8_t and silently dropped the PMD value.
// ===========================================================================
void COPMEmu::updateLFOModDepth()
{
    int pmd = (m_mwValue * m_mwPitchRange) / 127
            + (m_breathValue * m_breathPitch) / 127;
    int amd = (m_mwValue * m_mwAmpRange) / 127;
    if (pmd > 127) pmd = 127;
    if (amd > 127) amd = 127;

    writeReg(0x19, static_cast<uint8_t>(0x80 | (pmd & 0x7F)));
    writeReg(0x19, static_cast<uint8_t>(amd & 0x7F));
}

// ===========================================================================
// setPBMode — 0=All, 1=Low, 2=High, 3=K-on
// ===========================================================================
void COPMEmu::setPBMode(int mode)
{
    if (mode < 0 || mode > 3) return;
    m_pbMode = static_cast<PBMode>(mode);
}

// ===========================================================================
// Breath Controller Parameter Setters
// ===========================================================================
void COPMEmu::setBreathPitch(int value)
{
    if (value < 0) value = 0;
    if (value > 99) value = 99;
    m_breathPitch = value;
}

void COPMEmu::setBreathPitchBias(int value)
{
    if (value < 0) value = 0;
    if (value > 99) value = 99;
    m_breathPitchBias = value;
}

void COPMEmu::setBreathAmplitude(int value)
{
    if (value < 0) value = 0;
    if (value > 99) value = 99;
    m_breathAmplitude = value;
}

void COPMEmu::setBreathEGBias(int value)
{
    if (value < 0) value = 0;
    if (value > 99) value = 99;
    m_breathEGBias = value;
}

void COPMEmu::setBreathEGDepth(int value)
{
    if (value < 0) value = 0;
    if (value > 99) value = 99;
    m_breathEGDepth = value;
}


// ===========================================================================
// initRamFromRom — Copy first 32 ROM patches into RAM bank
// ===========================================================================
void COPMEmu::initRamFromRom()
{
    m_memory.initRamFromRom(dx21_patches, TOTAL_OPM_PATCHES);
}

// ===========================================================================
// applyPerformance — Load a performance memory into the engine
// ===========================================================================
void COPMEmu::applyPerformance(int perfSlot)
{
    const DX21_Performance* perf = m_memory.getPerformance(perfSlot);
    if (!perf) return;

    // Free all voices before switching
    for (int i = 0; i < kNumVoices; ++i) {
        if (m_voices[i].active) {
            freeVoice(i);
        }
    }

    m_playMode = static_cast<PlayMode>(perf->playMode);
    m_patchA = perf->voiceA;
    m_patchB = perf->voiceB;
    m_splitPoint = perf->splitPoint;
    m_balance = perf->balance;
    m_pbRange = perf->pbRange;
    m_pbMode = static_cast<PBMode>(perf->pbMode);
    m_portaMode = static_cast<PortaMode>(perf->portaMode);
    m_portaRate = perf->portaRate;
    m_portaDecayFactor = computePortaDecayFactor(m_portaRate);
    m_masterTune = perf->transpose;  // Using transpose as master tune for now
    m_ensembleOn = (perf->chorus != 0);
    // breath params
    m_breathPitchBias = perf->breathPitch;
    m_breathAmplitude = perf->breathAmp;
    m_breathEGBias = perf->breathEGBias;

    // Apply patches to sides
    applyPatchToSide(SideA, m_patchA);
    applyPatchToSide(SideB, m_patchB);
}

// ===========================================================================
// getPatch — RAM priority lookup, ROM fallback
// ===========================================================================
const DX21_Patch* COPMEmu::getPatch(int index) const
{
    if (index < 0 || index >= 128) return nullptr;

    // COMPARE: while active, the current program resolves to the
    // pre-edit snapshot so held/new notes sound the original voice.
    if (m_bCompare && index == m_currentPatch) {
        return &m_compareBuffer;
    }

    // Try RAM first (slots 0-31)
    if (index < CDX21Memory::kNumRamVoices) {
        const DX21_Patch* ram = m_memory.getRamVoice(index);
        if (ram) return ram;
    }

    // Fallback to ROM
    if (index < TOTAL_OPM_PATCHES) return &dx21_patches[index];
    return nullptr;
}


// ===========================================================================
// SysEx Real-Time Parameter Change
// ===========================================================================

// DX21 Parameter Change SysEx format:
//   F0 43 0n 12 pp vv F7
//   n  = device number (0-15)
//   12 = DX21 model ID
//   pp = parameter number (0-75)
//   vv = value (0-127)
//
// DX21 VCED byte layout (76 bytes per voice):
//   Byte 0:   Algorithm (0-7)
//   Byte 1:   Feedback (0-7)
//   Byte 2:   LFO Speed (0-255)
//   Byte 3:   LFO Delay (0-127)
//   Byte 4:   PMD (0-127)
//   Byte 5:   AMD (0-127)
//   Byte 6:   LFO Sync (0/1)
//   Byte 7:   LFO Waveform (0-3)
//   Byte 8:   (PMS << 4) | AMS
//   Byte 9:   Transpose (0-48, offset 24)
//   Byte 10-23: OP1 (14 bytes)
//   Byte 24-37: OP2
//   Byte 38-51: OP3
//   Byte 52-65: OP4
//   Byte 66-73: Name (8 chars)
//   Byte 74:  Pitch Bend Range (0-12)
//   Byte 75:  (reserved)
//
// Operator bytes (14 per operator):
//   [0] AR, [1] D1R, [2] D2R, [3] RR, [4] D1L,
//   [5] LS, [6] RS, [7] EBS, [8] AME, [9] KVS,
//   [10] OUT, [11] CRS, [12] DET, [13] reserved

// ===========================================================================
// handleSysex — parse incoming SysEx, queue parameter changes for audio thread
// Called from MIDI thread (processMidi).
// ===========================================================================
void COPMEmu::handleSysex(const uint8_t* data, int size)
{
    if (size < 5) return;
    if (data[0] != 0xF0 || data[1] != 0x43) return;

    // data[2] = sn: s = sub-status (0 = voice/param data, 2 = dump
    // request), n = device number (ignored — we answer on any device).
    uint8_t subStatus = data[2] & 0x70;

    // Yamaha Dump Request: F0 43 2n ff F7 (manual 4-2-2 (5)). The
    // DX21 answers with the matching dump when FUNCTION #5 "Midi Sy
    // Info" is ON. f=3 requests the current voice (1-voice VCED-93),
    // f=4 the 32-voice VMEM bulk (4096 bytes). f=9 is kept as a
    // legacy alias for the bulk. The reply goes out through the
    // SysEx-out callback on the MIDI/main thread — never the audio
    // callback, so the std::vector heap use here is fine.
    if (subStatus == 0x20) {
        if (data[size - 1] != 0xF7) return;
        if (!m_sysexInfoOn || !m_sysexOutFn) return;
        uint8_t format = data[3];
        std::vector<uint8_t> reply;
        if (format == 0x03) {
            if (!exportCurrentVoiceSysex(reply)) return;
        } else if (format == 0x04 || format == 0x09) {
            if (!m_memory.exportSysex(reply)) return;
        } else {
            return;
        }
        m_sysexOutFn(m_sysexOutUser, reply.data(), reply.size());
        return;
    }

    if (size < 7) return;
    uint8_t modelId = data[3];

    // DX21 1-voice VCED dump: F0 43 0n 03 ... F7. Received into the
    // current edit slot (m_sysexEditVoice) in RAM — the same slot the
    // real-time parameter change (0x12) targets. Memory Protect is
    // enforced inside importVoiceSysex. The next key-on picks up the
    // new data (setupVoice reads the RAM patch per note).
    if (modelId == 0x03) {
        m_memory.importVoiceSysex(data, (size_t)size, m_sysexEditVoice);
        return;
    }

    // DX21 Parameter Change: F0 43 0n 12 pp vv F7
    if (modelId == 0x12 && size == 7 && data[6] == 0xF7) {
        uint8_t param = data[4];
        uint8_t value = data[5];
        if (param < kSysexNumParams) {
            m_sysexParam[param] = value;
            m_sysexDirty[param] = true;
        }
        return;
    }

    // 32-voice bulk dump: real DX21 VMEM (F0 43 0n 04, 4096 bytes)
    // or legacy microDX21 format (F0 43 0n 09, 2432 bytes). Format
    // validation and dispatch happen in CDX21Memory::importSysex.
    if (modelId == 0x04 || modelId == 0x09) {
        // FUNCTION #23 (Memory Protect): bulk dump is a write to all 32
        // RAM voices. Reject when protected.
        if (m_memoryProt) {
            return;
        }
        // We could store it and let the audio thread handle it,
        // but bulk dumps are large and rare. For now, handle directly
        // on MIDI thread since they don't touch audio-critical registers.
        int imported = m_memory.importSysex(data, size);
        (void)imported;
        return;
    }
}

// ===========================================================================
// exportCurrentVoiceSysex — build a 1-voice VCED dump of the current program
// Called from the MIDI/main thread (dump-request reply, UI voice transmit).
// ===========================================================================
bool COPMEmu::exportCurrentVoiceSysex(std::vector<uint8_t>& out)
{
    const DX21_Patch* p = getPatch(getCurrentProgram());
    if (!p) return false;
    return m_memory.exportVoiceSysex(*p, out);
}

// ===========================================================================
// applySysexChanges — consume queued parameter changes on audio thread
// ===========================================================================
void COPMEmu::applySysexChanges()
{
    for (int i = 0; i < kSysexNumParams; ++i) {
        if (!m_sysexDirty[i]) continue;
        m_sysexDirty[i] = false;
        // Defence in depth: if protect was enabled after handleSysex()
        // queued the change, drop it here too.
        if (m_memoryProt) continue;
        applySysexParam(i, m_sysexParam[i]);
    }
}

// ===========================================================================
// applySysexParam — write one VCED parameter to the OPM chip
// This updates both the OPM registers and the RAM patch.
// ===========================================================================
void COPMEmu::applySysexParam(int paramIndex, uint8_t value)
{
    int voice = m_sysexEditVoice;
    if (voice < 0 || voice >= CDX21Memory::kNumRamVoices) return;

    if (m_memoryProt) return;  // FUNCTION #23: real-time param is a write

    DX21_Patch* ramPatch = m_memory.getRamVoice(voice);
    if (!ramPatch) return;

    markEditDirty(ramPatch);   // COMPARE: snapshot the pre-edit state

    // ── Real DX21 VCED parameter numbering (manual table 5-2) ──────
    //
    // 0-51: four operator blocks of 13 params each, wire order
    //       OP4, OP2, OP3, OP1 (internal op indices 3, 1, 2, 0).
    //       Within a block: AR, D1R, D2R, RR, D1L, LS, RS, EBS, AME,
    //       KVS, OUT, FREQ, DET — mapped onto writeVcedOperator()'s
    //       internal param ids below.
    // 52-76: voice globals + per-voice function data (table 5-2).
    // 77-86: voice name (10 ASCII chars).
    // 87-92: Pitch EG (PR1-3, PL1-3).
    // 93:    operator enable mask (function param table 5-3).
    //
    // writeVcedOperator/writeVcedGlobal update both the RAM patch
    // and the OPM registers and re-check Memory Protect themselves.

    if (paramIndex >= 0 && paramIndex < 52) {
        static const int kWireToInternalOp[4] = { 3, 1, 2, 0 };
        // VCED in-block order → writeVcedOperator internal param id
        static const int kVcedOpParam[13] =
            { 0, 2, 1, 3, 8, 5, 6, 7, 13, 9, 10, 11, 12 };
        int block = paramIndex / 13;
        int sub   = paramIndex % 13;
        if (sub == 12 && value > 6) value = 6;  // DET wire range is 0-7
        // transmit=false: incoming MIDI edits must not echo back out.
        writeVcedOperator(kWireToInternalOp[block], kVcedOpParam[sub], value, false);
        return;
    }

    switch (paramIndex) {
        case 52: writeVcedGlobal(0, value, false); break;       // ALGORITHM
        case 53: writeVcedGlobal(1, value, false); break;       // FEEDBACK
        case 54: // LFO SPEED: wire 0-99 → internal LFRQ 0-255
            writeVcedGlobal(6, (uint8_t)(((value > 99 ? 99 : value) * 255 + 49) / 99), false);
            break;
        case 55: writeVcedGlobal(7, value, false); break;       // LFO DELAY
        case 56: writeVcedGlobal(8, value, false); break;       // PMD
        case 57: writeVcedGlobal(9, value, false); break;       // AMD
        case 58: writeVcedGlobal(10, value, false); break;      // LFO SYNC
        case 59: writeVcedGlobal(11, value, false); break;      // LFO WAVE
        case 60: // PMS — keep current AMS
            writeVcedGlobal(12, (uint8_t)(((value & 0x07) << 4) | (ramPatch->ams & 0x03)), false);
            break;
        case 61: // AMS — keep current PMS
            writeVcedGlobal(12, (uint8_t)(((ramPatch->pms & 0x07) << 4) | (value & 0x03)), false);
            break;
        case 62: writeVcedGlobal(13, value > 48 ? 48 : value, false); break; // TRANSPOSE

        // Per-voice function data (also mirrored into the live engine
        // state so the change is audible immediately, like on the
        // original hardware).
        case 63:
            ramPatch->mono = value & 1;
            if (m_playMode == Split) {
                // SPLIT: poly/mono is per-side (patch flag). Editing
                // it can move the 7+1 boundary — rebuild the layout.
                refreshVoiceLayout();
            } else {
                setMono(value & 1);
            }
            break;
        case 64:
            ramPatch->pb_range = value > 12 ? 12 : value;
            setPitchBendRange(ramPatch->pb_range);
            break;
        case 65: // PORTAMENTO MODE: 0=full time, 1=fingered
            ramPatch->porta_mode = value & 1;
            setPortamentoMode((value & 1) ? 2 : 1);
            break;
        case 66:
            ramPatch->porta_time = value > 99 ? 99 : value;
            setPortamentoRate(ramPatch->porta_time);
            break;
        case 67:
            ramPatch->foot_volume = value > 99 ? 99 : value;
            setFootVolumeRange(ramPatch->foot_volume);
            break;
        case 68: ramPatch->sus_fs = value & 1;   setFootSustainOn(value & 1); break;
        case 69: ramPatch->porta_fs = value & 1; setFootPortaOn(value & 1);   break;
        case 70: ramPatch->chorus = value & 1;   setEnsembleOn(value & 1);    break;
        case 71:
            ramPatch->mw_pitch = value > 99 ? 99 : value;
            setMWPitchRange(ramPatch->mw_pitch);
            break;
        case 72:
            ramPatch->mw_amp = value > 99 ? 99 : value;
            setMWAmpRange(ramPatch->mw_amp);
            break;
        case 73:
            ramPatch->bc_pitch = value > 99 ? 99 : value;
            setBreathPitch(ramPatch->bc_pitch);
            break;
        case 74:
            ramPatch->bc_amp = value > 99 ? 99 : value;
            setBreathAmplitude(ramPatch->bc_amp);
            break;
        case 75:
            ramPatch->bc_pbias = value > 99 ? 99 : value;
            setBreathPitchBias(ramPatch->bc_pbias);
            break;
        case 76:
            ramPatch->bc_ebias = value > 99 ? 99 : value;
            setBreathEGBias(ramPatch->bc_ebias);
            break;

        // 87-92: Pitch EG. Stored in the patch; picked up by the next
        // key-on (setupVoice copies the effective PEG into the voice).
        case 87: case 88: case 89:
            ramPatch->peg_r[paramIndex - 87] = value > 99 ? 99 : value;
            break;
        case 90: case 91: case 92:
            ramPatch->peg_l[paramIndex - 90] = value > 99 ? 99 : value;
            break;

        // 93: operator enable/disable (function param table 5-3).
        // Bits 3..0 = OP1..OP4. Re-apply TL to active voices of the
        // edited program so the change is audible immediately.
        case 93:
            ramPatch->op_enable = value & 0x0F;
            if (m_currentPatch == voice) {
                for (int v = 0; v < kNumVoices; ++v) {
                    if (m_voices[v].active) {
                        applyTL(v, *ramPatch, m_voices[v].note,
                                m_voices[v].velocity);
                    }
                }
            }
            break;

        default:
            if (paramIndex >= 77 && paramIndex <= 86) {  // VOICE NAME
                ramPatch->name[paramIndex - 77] = (char)(value & 0x7F);
                ramPatch->name[10] = '\0';
            }
            break;
    }
}

// ===========================================================================
// writeVcedGlobal — Write global voice parameters (algorithm, feedback, LFO, etc.)
// Maps DX21 VCED parameter indices to OPM registers.
// Called from COPMEmuAdapter when parameter sliders change.
// ===========================================================================
void COPMEmu::writeVcedGlobal(int param, uint8_t value, bool transmit)
{
    int voice = m_sysexEditVoice;
    if (voice < 0 || voice >= CDX21Memory::kNumRamVoices) return;

    if (m_memoryProt) return;  // FUNCTION #23: slider edit is a write

    DX21_Patch* ramPatch = m_memory.getRamVoice(voice);
    if (!ramPatch) return;

    markEditDirty(ramPatch);   // COMPARE: snapshot the pre-edit state

    // Chip channels currently sounding the edited patch. The edit
    // slot is a RAM index (0..31) — it is NOT a chip channel, so
    // per-channel registers (0x20+ch, 0x38+ch) are written for every
    // channel whose side plays this patch.
    int chBegin[2] = {0, 0}, chEnd[2] = {0, 0};
    int nRanges = 0;
    if (m_patchA == voice) {
        chBegin[nRanges] = voiceStart(SideA);
        chEnd[nRanges]   = voiceStart(SideA) + voiceCount(SideA);
        ++nRanges;
    }
    if (m_playMode != Single && m_patchB == voice) {
        chBegin[nRanges] = voiceStart(SideB);
        chEnd[nRanges]   = voiceStart(SideB) + voiceCount(SideB);
        ++nRanges;
    }
    auto forEachCh = [&](auto&& fn) {
        for (int r = 0; r < nRanges; ++r)
            for (int ch = chBegin[r]; ch < chEnd[r]; ++ch)
                fn(ch);
    };

    // VCED number for the real-time transmit (manual table 5-2);
    // -1 = no transmit for this internal id.
    int txParam = -1;
    uint8_t txValue = value;

    switch (param) {
        case 0: // Algorithm (0-7)
            ramPatch->alg = value & 0x07;
            forEachCh([&](int ch) { writeField(0x20 + ch, 0, 3, ramPatch->alg); });
            txParam = 52; txValue = ramPatch->alg;
            break;
        case 1: // Feedback (0-7)
            ramPatch->fb = value & 0x07;
            forEachCh([&](int ch) { writeField(0x20 + ch, 3, 3, ramPatch->fb); });
            txParam = 53; txValue = ramPatch->fb;
            break;
        case 6: // LFO Speed (0-255, YM2151 LFRQ; VCED 54 is 0-99)
            ramPatch->lfo_speed = value;
            writeReg(0x18, ramPatch->lfo_speed);
            txParam = 54;
            txValue = (uint8_t)((ramPatch->lfo_speed * 99 + 127) / 255);
            break;
        case 7: // LFO Delay (0-127)
            ramPatch->lfo_delay = value & 0x7F;
            txParam = 55; txValue = ramPatch->lfo_delay > 99 ? 99 : ramPatch->lfo_delay;
            break;
        case 8: // PMD (Pitch Mod Depth, 0-127)
            ramPatch->pmd = value & 0x7F;
            writeReg(0x19, (ramPatch->pmd & 0x7F) | 0x80);
            txParam = 56; txValue = ramPatch->pmd > 99 ? 99 : ramPatch->pmd;
            break;
        case 9: // AMD (Amplitude Mod Depth, 0-127)
            ramPatch->amd = value & 0x7F;
            writeReg(0x19, ramPatch->amd & 0x7F);
            txParam = 57; txValue = ramPatch->amd > 99 ? 99 : ramPatch->amd;
            break;
        case 10: // LFO Sync (0 or 1)
            ramPatch->lfo_sync = value & 0x01;
            txParam = 58; txValue = ramPatch->lfo_sync;
            break;
        case 11: // LFO Waveform (0-3)
            ramPatch->lfo_wave = value & 0x03;
            writeField(0x1B, 0, 2, ramPatch->lfo_wave);
            txParam = 59; txValue = ramPatch->lfo_wave;
            break;
        case 12: // PMS (0-7) + AMS (0-3), packed (pms << 4) | ams
            ramPatch->pms = (value >> 4) & 0x07;
            ramPatch->ams = value & 0x03;
            forEachCh([&](int ch) {
                writeReg(0x38 + ch,
                         (uint8_t)((ramPatch->pms << 4) | (ramPatch->ams & 0x03)));
            });
            // Two separate VCED params on the wire.
            if (transmit) {
                transmitParamChange(60, ramPatch->pms);
                transmitParamChange(61, ramPatch->ams);
            }
            break;
        case 13: // Key Offset / Transpose (0-48, centre 24)
            ramPatch->key_offset = (int8_t)((int)value - 24);
            txParam = 62; txValue = value > 48 ? 48 : value;
            break;

        // Pitch EG (VCED 87-92). Firmware-side feature — no OPM
        // register; stored in the patch and picked up by the next
        // key-on (setupVoice copies the effective PEG per voice).
        case 14: ramPatch->peg_r[0] = value > 99 ? 99 : value; txParam = 87; txValue = ramPatch->peg_r[0]; break;
        case 15: ramPatch->peg_r[1] = value > 99 ? 99 : value; txParam = 88; txValue = ramPatch->peg_r[1]; break;
        case 16: ramPatch->peg_r[2] = value > 99 ? 99 : value; txParam = 89; txValue = ramPatch->peg_r[2]; break;
        case 17: ramPatch->peg_l[0] = value > 99 ? 99 : value; txParam = 90; txValue = ramPatch->peg_l[0]; break;
        case 18: ramPatch->peg_l[1] = value > 99 ? 99 : value; txParam = 91; txValue = ramPatch->peg_l[1]; break;
        case 19: ramPatch->peg_l[2] = value > 99 ? 99 : value; txParam = 92; txValue = ramPatch->peg_l[2]; break;

        default:
            return;
    }

    if (transmit && txParam >= 0) {
        transmitParamChange(txParam, txValue);
    }
}

// ===========================================================================
// writeVcedOperator — Write per-operator parameters (AR, D1R, D1L, OUT, CRS)
// Maps DX21 VCED parameter indices to OPM registers.
// Called from COPMEmuAdapter when per-op sliders change.
// ===========================================================================
void COPMEmu::writeVcedOperator(int op, int param, uint8_t value, bool transmit)
{
    int voice = m_sysexEditVoice;
    if (voice < 0 || voice >= CDX21Memory::kNumRamVoices) return;
    if (op < 0 || op >= 4) return;

    if (m_memoryProt) return;  // FUNCTION #23: slider edit is a write

    DX21_Patch* ramPatch = m_memory.getRamVoice(voice);
    if (!ramPatch) return;

    markEditDirty(ramPatch);   // COMPARE: snapshot the pre-edit state

    DX21_Operator& o = ramPatch->op[op];

    // Chip channels currently sounding the edited patch (the edit
    // slot is a RAM index 0..31, not a chip channel — see
    // writeVcedGlobal).
    int chBegin[2] = {0, 0}, chEnd[2] = {0, 0};
    int nRanges = 0;
    if (m_patchA == voice) {
        chBegin[nRanges] = voiceStart(SideA);
        chEnd[nRanges]   = voiceStart(SideA) + voiceCount(SideA);
        ++nRanges;
    }
    if (m_playMode != Single && m_patchB == voice) {
        chBegin[nRanges] = voiceStart(SideB);
        chEnd[nRanges]   = voiceStart(SideB) + voiceCount(SideB);
        ++nRanges;
    }
    auto forEachSlot = [&](auto&& fn) {
        for (int r = 0; r < nRanges; ++r)
            for (int ch = chBegin[r]; ch < chEnd[r]; ++ch)
                fn(ch + OP_SLOT[op]);
    };

    switch (param) {
        case 0: // AR (Attack Rate, 0-31)
            o.ar = value & 0x1F;
            forEachSlot([&](int s) { writeField(0x80 + s, 0, 5, o.ar); });
            break;
        case 1: // D2R (Decay 2 Rate, 0-31) -- register 0xC0[4:0]
            o.d2r = value & 0x1F;
            forEachSlot([&](int s) { writeField(0xC0 + s, 0, 5, o.d2r); });
            break;
        case 2: // D1R (Decay1 Rate, 0-31)
            o.d1r = value & 0x1F;
            forEachSlot([&](int s) { writeField(0xA0 + s, 0, 5, o.d1r); });
            break;
        case 3: // RR (Release Rate, 0-15) -- packed in 0xE0[3:0]
            o.rr = value & 0x0F;
            forEachSlot([&](int s) {
                uint8_t d1l = 15 - (o.d1l & 0x0F);
                writeReg(0xE0 + s, (uint8_t)((d1l << 4) | o.rr));
            });
            break;
        case 5: // LS (Level Scaling, 0-99) -- applied per-note
            o.ls = value > 99 ? 99 : value;
            break;
        case 6: // RS (Rate Scaling, 0-3) -- packed in 0x80[7:6] with AR
            o.rs = value & 0x03;
            forEachSlot([&](int s) {
                writeReg(0x80 + s, (uint8_t)(((o.rs & 0x03) << 6) | (o.ar & 0x1F)));
            });
            break;
        case 7: // EBS (EG Bias Sensitivity, 0-7) -- no direct OPM reg
            o.ebs = value & 0x07;
            break;
        case 8: // D1L (Decay1 Level, 0-15)
            o.d1l = value & 0x0F;
            forEachSlot([&](int s) {
                uint8_t d1l = 15 - (o.d1l & 0x0F);
                writeReg(0xE0 + s, (uint8_t)((d1l << 4) | (o.rr & 0x0F)));
            });
            break;
        case 9: // KVS (Key Velocity Sensitivity, 0-7) -- per-note
            o.kvs = value & 0x07;
            break;
        case 10: // OUT (Output Level, 0-99 → TL)
            o.out = value > 99 ? 99 : value;
            forEachSlot([&](int s) {
                uint8_t tl = (99 - o.out) * 127 / 99;
                if (tl > 127) tl = 127;
                writeReg(0x60 + s, 0x80 | (tl & 0x7F));
            });
            break;
        case 11: // CRS (Coarse Ratio, 0-63 → MUL)
            o.crs = value & 0x3F;
            forEachSlot([&](int s) {
                writeField(0x40 + s, 0, 4, CRS_TO_MUL[o.crs & 0x3F]);
            });
            break;
        case 12: // DET (Detune, 0-6)
            o.det = (value & 0x07) > 6 ? 6 : (value & 0x07);
            forEachSlot([&](int s) {
                uint8_t dt1 = DET_TO_DT1[o.det];
                uint8_t mul = (o.crs < 64) ? CRS_TO_MUL[o.crs] : 15;
                writeReg(0x40 + s, (uint8_t)((dt1 << 4) | (mul & 0x0F)));
            });
            break;
        case 13: // AME (AM Enable, 0/1) -- bit 7 of 0xA0
            o.ame = value & 0x01;
            forEachSlot([&](int s) {
                writeReg(0xA0 + s, (uint8_t)(((o.ame & 0x01) << 7) | (o.d1r & 0x1F)));
            });
            break;
        default:
            return;
    }

    if (transmit) {
        // VCED number (manual table 5-2): wire op-block order is
        // OP4, OP2, OP3, OP1; internal op index → wire block is the
        // self-inverse permutation {3,1,2,0}. Internal param id →
        // in-block VCED position:
        static const int kInternalToWireBlock[4] = { 3, 1, 2, 0 };
        static const int kInternalToVcedSub[14] =
            //  0  1  2  3   4  5  6  7  8  9  10  11  12  13
            {   0, 2, 1, 3, -1, 5, 6, 7, 4, 9, 10, 11, 12,  8 };
        int sub = (param >= 0 && param < 14) ? kInternalToVcedSub[param] : -1;
        if (sub >= 0) {
            int vced = kInternalToWireBlock[op] * 13 + sub;
            uint8_t tx;
            switch (param) {  // transmit the stored (masked) value
                case 0:  tx = o.ar;  break;
                case 1:  tx = o.d2r; break;
                case 2:  tx = o.d1r; break;
                case 3:  tx = o.rr;  break;
                case 5:  tx = o.ls;  break;
                case 6:  tx = o.rs;  break;
                case 7:  tx = o.ebs; break;
                case 8:  tx = o.d1l; break;
                case 9:  tx = o.kvs; break;
                case 10: tx = o.out; break;
                case 11: tx = o.crs; break;
                case 12: tx = o.det; break;
                default: tx = o.ame; break;  // 13
            }
            transmitParamChange(vced, tx);
        }
    }
}

// ===========================================================================
// setSysexEditVoice — select which voice slot receives real-time parameter changes
// ===========================================================================
void COPMEmu::setSysexEditVoice(int voice)
{
    // The edit slot is a RAM voice index (0..31), NOT a chip channel.
    if (voice < 0) voice = 0;
    if (voice >= CDX21Memory::kNumRamVoices)
        voice = CDX21Memory::kNumRamVoices - 1;
    m_sysexEditVoice = voice;
}

// ===========================================================================
// reapplyEditPatch — re-apply the edited patch to every chip voice
// currently sounding it (side A and/or B).
// ===========================================================================
void COPMEmu::reapplyEditPatch(int slot)
{
    if (m_patchA == slot) applyPatchToSide(SideA, slot);
    if (m_playMode != Single && m_patchB == slot) applyPatchToSide(SideB, slot);
}

// ===========================================================================
// transmitParamChange — real-time parameter change out
// F0 43 1n 12 pp vv F7 (manual 2-2-2 (1)); gated on "Midi Sy Info".
// Called from the panel-edit path (display/input thread), never from
// the audio callback.
// ===========================================================================
void COPMEmu::transmitParamChange(int vcedParam, uint8_t value)
{
    if (!m_sysexInfoOn || !m_sysexOutFn) return;
    if (vcedParam < 0 || vcedParam > 127) return;
    uint8_t msg[7] = { 0xF0, 0x43, 0x10, 0x12,
                       (uint8_t)vcedParam, (uint8_t)(value & 0x7F), 0xF7 };
    m_sysexOutFn(m_sysexOutUser, msg, sizeof(msg));
}

// ===========================================================================
// setCompare — audible COMPARE (original vs. edited voice)
// ===========================================================================
bool COPMEmu::setCompare(bool on)
{
    if (on && !m_bEditDirty) return false;   // nothing to compare yet
    if (m_bCompare == on) return m_bCompare;
    m_bCompare = on;
    // getPatch() now resolves the current program to the snapshot
    // (or back to the edited RAM data) — re-apply to the chip.
    reapplyEditPatch(m_currentPatch);
    return m_bCompare;
}

// ===========================================================================
// setVoiceName — rename the current edit voice (FUNCTION "Name :")
// ===========================================================================
bool COPMEmu::setVoiceName(const char* name)
{
    if (!name) return false;
    int slot = m_sysexEditVoice;
    if (slot < 0 || slot >= CDX21Memory::kNumRamVoices) return false;
    if (m_memoryProt) return false;  // FUNCTION #23: rename is a write

    DX21_Patch* ram = m_memory.getRamVoice(slot);
    if (!ram) return false;

    markEditDirty(ram);

    int i = 0;
    for (; i < 10 && name[i] != '\0'; ++i) {
        char c = name[i];
        ram->name[i] = (c >= 0x20 && c < 0x7F) ? c : ' ';
    }
    ram->name[i] = '\0';

    // Transmit the name as VCED params 77-86 (space-padded).
    for (int t = 0; t < 10; ++t) {
        transmitParamChange(77 + t, (t < i) ? (uint8_t)ram->name[t] : (uint8_t)' ');
    }
    return true;
}

// ===========================================================================
// capturePerformance — snapshot the live engine state (Store Perf)
// ===========================================================================
void COPMEmu::capturePerformance(DX21_Performance& out) const
{
    out.initDefaults();
    out.playMode   = (uint8_t)m_playMode;
    out.voiceA     = (uint8_t)m_patchA;
    out.voiceB     = (uint8_t)m_patchB;
    out.splitPoint = (uint8_t)m_splitPoint;
    out.balance    = (uint8_t)m_balance;
    out.pbRange    = (uint8_t)m_pbRange;
    out.pbMode     = (uint8_t)m_pbMode;
    out.portaMode  = (uint8_t)m_portaMode;
    out.portaRate  = (uint8_t)m_portaRate;
    out.chorus     = m_ensembleOn ? 1 : 0;
    out.transpose  = (int8_t)m_masterTune;   // mirrors applyPerformance
    out.breathPitch  = (uint8_t)m_breathPitchBias;
    out.breathAmp    = (uint8_t)m_breathAmplitude;
    out.breathEGBias = (uint8_t)m_breathEGBias;
}

// ----------------------------------------------------------------------------
// setMemoryProtect / isMemoryProtected — Function #23 (dat_F610:35)
//
// Toggle the global write-protect on RAM voices and performance memories.
// When ON, every write path that lands in CDX21Memory::m_ram or
// CDX21Memory::m_perf is rejected:
//   - importSysex (32-voice VCED bulk dump)
//   - applySysexParam (real-time F0 43 0n 12 pp vv F7)
//   - writeVcedGlobal / writeVcedOperator (slider edits)
//   - initVoice, saveEditRecall, loadEditRecall
//
// Read paths (getRamVoice, applyPatchToSide) are unaffected — the synth
// still plays what's in RAM, the user just can't change it.
void COPMEmu::setMemoryProtect(bool on)
{
    m_memoryProt = on;
    m_memory.setMemoryProtect(on);  // keep both layers in sync
}

// ----------------------------------------------------------------------------
// TestDoor — thin public back-door for unit tests. Lets the test code call
// the private writeVcedGlobal/Operator methods so it can verify the
// memory-protect gates without going through SysEx or the adapter.
// ----------------------------------------------------------------------------
void COPMEmu::TestDoor::writeVcedGlobal(COPMEmu& e, int p, uint8_t v)
{
    e.writeVcedGlobal(p, v);
}

void COPMEmu::TestDoor::writeVcedOperator(COPMEmu& e, int op, int p, uint8_t v)
{
    e.writeVcedOperator(op, p, v);
}
