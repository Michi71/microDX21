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
    , m_portaRateFactor(0.0f)
    , m_pbMode(PBAll)
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
    if (!m_voices[voice].active) return;

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
// ===========================================================================
int COPMEmu::allocVoice(Side side, int note)
{
    int start = voiceStart(side);
    int count = voiceCount(side);
    if (count == 0) return -1;

    // MONO mode: free the single voice slot before allocation
    if (m_mono && count == 1) {
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
    // Steal oldest voice within side — smooth fade-out
    int oldest = start;
    for (int i = start + 1; i < start + count; ++i) {
        if (m_voices[i].age < m_voices[oldest].age) oldest = i;
    }
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
            msgLen = 1;  // Realtime: skip
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
            bool pedalOn = (ev.data2 >= 64);
            if (m_sustainPedal && !pedalOn) {
                for (int v = 0; v < kNumVoices; ++v) {
                    if (m_voices[v].active && m_voices[v].sustained) {
                        writeReg(0x08, static_cast<uint8_t>(v));
                        m_voices[v].active = false;
                        m_voices[v].sustained = false;
                    }
                }
            }
            m_sustainPedal = pedalOn;
        }
        else if (status == 0xB0 && ev.data1 == 1) {
            // Modulation Wheel (CC#1). The DX21 B8 function
            // (m_mwPitchRange, 0..99) sets the maximum depth of
            // pitch modulation the wheel can apply; B9
            // (m_mwAmpRange, 0..99) does the same for AMD. We
            // scale the raw 0..127 CC#1 value by these ranges
            // and write to the LFO's PMD/AMD register. When the
            // range is 0 the wheel has no effect (the original
            // DX21's "MW Pitch OFF" position).
            int pmd = (ev.data2 * m_mwPitchRange) / 127;
            int amd = (ev.data2 * m_mwAmpRange)   / 127;
            // bit 7 of reg 0x19 = PMD, bits 0..6 = AMD
            uint8_t reg = static_cast<uint8_t>(((pmd & 0x7F) << 7) | (amd & 0x7F));
            writeReg(0x19, reg);
            m_mwValue = ev.data2;
        }
        else if (status == 0xB0 && ev.data1 == 2) {
            // Breath Controller
            handleBreath(ev.data2);
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
    updatePortamento();

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
        outputL[i] = rawL * m_masterGain;
        outputR[i] = rawR * m_masterGain;
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
    m_currentPatch = index;
    m_patchA = index;

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

    // Portamento: if active and voice already has a pitch, glide from there
    bool shouldPorta = (m_portaMode != PortaOff);
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
    Side side = (voice < 4) ? SideA : SideB;
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

    // Step 1: Write KC/KF WITHOUT KeyOn
    writeFrequency(voice, m_voices[voice].currentPitch, false);

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
    return 4;  // Side B always starts at voice 4
}

int COPMEmu::voiceCount(Side side) const
{
    switch (m_playMode) {
    case Single:
        return (side == SideA) ? 8 : 0;
    case Dual:
    case Split:
        return 4;
    }
    return 0;
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

        Side side = (v < 4) ? SideA : SideB;
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
    m_patchA = index;
    if (m_playMode == Single) {
        m_currentPatch = index;  // backward compat
    }
    applyPatchToSide(SideA, index);
}

void COPMEmu::setPatchB(int index)
{
    if (index < 0 || index >= TOTAL_OPM_PATCHES) return;
    m_patchB = index;
    applyPatchToSide(SideB, index);
}

void COPMEmu::setMasterTune(int cents)
{
    if (cents < -64) cents = -64;
    if (cents > 63) cents = 63;
    m_masterTune = cents;
}


// ===========================================================================
// computePortaRateFactor — map DX21 rate (0..99) to per-sample increment
// ===========================================================================
float COPMEmu::computePortaRateFactor(int rate) const
{
    if (rate <= 0) return 0.0f;
    // DX21 portamento rate 99 = ~15 semitones/sec at 48kHz
    // Rate factor = rate / (99 * sampleRate / targetSpeed)
    // Empirically: rate 99 → factor ~0.0005 per sample
    return static_cast<float>(rate) * 0.00000505f;
}

// ===========================================================================
// updatePortamento — interpolate pitch and write KC/KF for porting voices
// Called from processBlock (audio thread) after MIDI processing.
// ===========================================================================
void COPMEmu::updatePortamento()
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

        // Move currentPitch towards targetPitch
        float step = delta * m_portaRateFactor;
        // Clamp step to avoid overshooting
        if (std::abs(step) > std::abs(delta)) step = delta;

        m_voices[v].currentPitch += step;
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
        "Init Voice"
    };
    return &kInitVoice;
}

void COPMEmu::initVoice() {
    int voice = m_sysexEditVoice;
    if (voice < 0 || voice >= kNumVoices) return;

    DX21_Patch* ram = m_memory.getRamVoice(voice);
    if (!ram) return;

    // Copy the static init patch into RAM and re-apply.
    *ram = *GetInitVoicePatch();
    applyPatchToVoice(voice, *ram);
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
    if (voice < 0 || voice >= kNumVoices) return;
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
    int voice = m_sysexEditVoice;
    if (voice < 0 || voice >= kNumVoices) return;
    DX21_Patch* ram = m_memory.getRamVoice(voice);
    if (!ram) return;
    *ram = m_editRecall;
    applyPatchToVoice(voice, *ram);
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
        m_portaRateFactor = computePortaRateFactor(m_portaRate);
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
    m_portaRateFactor = computePortaRateFactor(rate);
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
    m_portaRateFactor = computePortaRateFactor(m_portaRate);
    m_masterTune = perf->transpose;  // Using transpose as master tune for now
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
    if (size < 7) return;
    if (data[0] != 0xF0 || data[1] != 0x43) return;

    // uint8_t deviceAndModel = data[2];  // 0n where n = device number (currently unused)
    uint8_t modelId = data[3];

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

    // DX21 32-voice VCED Bulk Dump: F0 43 0n 09 ... F7
    // Handled by CDX21Memory::importSysex — just pass it through here.
    if (modelId == 0x09) {
        // We could store it and let the audio thread handle it,
        // but bulk dumps are large and rare. For now, handle directly
        // on MIDI thread since they don't touch audio-critical registers.
        int imported = m_memory.importSysex(data, size);
        (void)imported;
        return;
    }
}

// ===========================================================================
// applySysexChanges — consume queued parameter changes on audio thread
// ===========================================================================
void COPMEmu::applySysexChanges()
{
    for (int i = 0; i < kSysexNumParams; ++i) {
        if (!m_sysexDirty[i]) continue;
        m_sysexDirty[i] = false;
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
    if (voice < 0 || voice >= kNumVoices) return;

    // Update RAM patch so the change persists
    DX21_Patch* ramPatch = m_memory.getRamVoice(voice);
    if (!ramPatch) return;

    // --- Global parameters ---
    if (paramIndex == 0) {
        // Algorithm
        ramPatch->alg = value & 0x07;
        writeField(0x20 + voice, 0, 3, ramPatch->alg);
    }
    else if (paramIndex == 1) {
        // Feedback
        ramPatch->fb = value & 0x07;
        writeField(0x20 + voice, 3, 3, ramPatch->fb);
    }
    else if (paramIndex == 2) {
        // LFO Speed
        ramPatch->lfo_speed = value;
        writeReg(0x18, ramPatch->lfo_speed);
    }
    else if (paramIndex == 3) {
        // LFO Delay (not a direct OPM register; store in patch only)
        ramPatch->lfo_delay = value & 0x7F;
    }
    else if (paramIndex == 4) {
        // PMD
        ramPatch->pmd = value & 0x7F;
        writeReg(0x19, (ramPatch->pmd & 0x7F) | 0x80);
    }
    else if (paramIndex == 5) {
        // AMD
        ramPatch->amd = value & 0x7F;
        writeReg(0x19, ramPatch->amd & 0x7F);
    }
    else if (paramIndex == 6) {
        // LFO Sync
        ramPatch->lfo_sync = value & 0x01;
    }
    else if (paramIndex == 7) {
        // LFO Waveform
        ramPatch->lfo_wave = value & 0x03;
        writeField(0x1B, 0, 2, ramPatch->lfo_wave);
    }
    else if (paramIndex == 8) {
        // PMS (upper nibble) + AMS (lower nibble)
        ramPatch->pms = (value >> 4) & 0x07;
        ramPatch->ams = value & 0x03;
        writeReg(0x38 + voice, ((ramPatch->pms & 0x07) << 4) | (ramPatch->ams & 0x03));
    }
    else if (paramIndex == 9) {
        // Transpose
        ramPatch->key_offset = static_cast<int8_t>(value) - 24;
    }
    else if (paramIndex >= 10 && paramIndex < 66) {
        // Operator parameters
        int opParam = paramIndex - 10;
        int op = opParam / 14;
        int sub = opParam % 14;
        if (op < 0 || op > 3) return;

        DX21_Operator& o = ramPatch->op[op];
        int slot = voice + OP_SLOT[op];

        switch (sub) {
            case 0: // AR
                o.ar = value & 0x1F;
                writeField(0x80 + slot, 0, 5, o.ar);
                break;
            case 1: // D1R
                o.d1r = value & 0x1F;
                writeField(0xA0 + slot, 0, 5, o.d1r);
                break;
            case 2: // D2R
                o.d2r = value & 0x1F;
                writeField(0xC0 + slot, 0, 5, o.d2r);
                break;
            case 3: // RR
                o.rr = value & 0x0F;
                writeField(0xE0 + slot, 0, 4, o.rr);
                break;
            case 4: // D1L
                o.d1l = value & 0x0F;
                writeField(0xE0 + slot, 4, 4, o.d1l);
                break;
            case 5: // LS
                o.ls = value;
                break;
            case 6: // RS
                o.rs = value & 0x03;
                writeField(0x80 + slot, 6, 2, o.rs);
                break;
            case 7: // EBS
                o.ebs = value & 0x07;
                break;
            case 8: // AME
                o.ame = value & 0x01;
                writeField(0xA0 + slot, 7, 1, o.ame);
                break;
            case 9: // KVS
                o.kvs = value & 0x07;
                break;
            case 10: // OUT (Output Level → TL)
                o.out = value;
                {
                    uint8_t tl = (99 - o.out) * 127 / 99;
                    if (tl > 127) tl = 127;
                    writeReg(0x60 + slot, 0x80 | (tl & 0x7F));
                }
                break;
            case 11: // CRS (Coarse Ratio → MUL)
                o.crs = value & 0x3F;
                {
                    uint8_t mul = CRS_TO_MUL[o.crs & 0x3F];
                    writeField(0x40 + slot, 0, 4, mul);
                }
                break;
            case 12: // DET (Detune → DT1)
                o.det = value & 0x07;
                {
                    uint8_t dt1 = DET_TO_DT1[o.det];
                    writeField(0x40 + slot, 4, 3, dt1);
                }
                break;
            default:
                break;
        }
    }
    else if (paramIndex >= 66 && paramIndex < 74) {
        // Name characters (not mapped to OPM registers)
        int nameIdx = paramIndex - 66;
        if (nameIdx >= 0 && nameIdx < 8) {
            ramPatch->name[nameIdx] = static_cast<char>(value);
        }
    }
    else if (paramIndex == 74) {
        // Pitch Bend Range (performance parameter)
        m_pbRange = value & 0x7F;
        if (m_pbRange > 12) m_pbRange = 12;
    }
}

// ===========================================================================
// writeVcedGlobal — Write global voice parameters (algorithm, feedback, LFO, etc.)
// Maps DX21 VCED parameter indices to OPM registers.
// Called from COPMEmuAdapter when parameter sliders change.
// ===========================================================================
void COPMEmu::writeVcedGlobal(int param, uint8_t value)
{
    int voice = m_sysexEditVoice;
    if (voice < 0 || voice >= kNumVoices) return;

    DX21_Patch* ramPatch = m_memory.getRamVoice(voice);
    if (!ramPatch) return;

    switch (param) {
        case 0: // Algorithm (0-7)
            ramPatch->alg = value & 0x07;
            writeField(0x20 + voice, 0, 3, ramPatch->alg);
            break;
        case 1: // Feedback (0-7)
            ramPatch->fb = value & 0x07;
            writeField(0x20 + voice, 3, 3, ramPatch->fb);
            break;
        case 6: // LFO Speed (0-255)
            ramPatch->lfo_speed = value;
            writeReg(0x18, ramPatch->lfo_speed);
            break;
        case 7: // LFO Delay (0-127)
            ramPatch->lfo_delay = value & 0x7F;
            // Note: LFO delay is not a direct OPM register, stored in patch only
            break;
        case 8: // PMD (Pitch Mod Depth, 0-127)
            ramPatch->pmd = value & 0x7F;
            writeReg(0x19, (ramPatch->pmd & 0x7F) | 0x80);
            break;
        case 9: // AMD (Amplitude Mod Depth, 0-127)
            ramPatch->amd = value & 0x7F;
            writeReg(0x19, ramPatch->amd & 0x7F);
            break;
        case 10: // LFO Sync (0 or 1)
            ramPatch->lfo_sync = value & 0x01;
            // Note: LFO sync is not directly programmable on YM2151, internal behavior
            break;
        case 11: // LFO Waveform (0-3: tri, saw, square, s/h)
            ramPatch->lfo_wave = value & 0x03;
            writeField(0x1B, 0, 2, ramPatch->lfo_wave);
            break;
        case 12: // PMS (Pitch Mod Sensitivity, 0-7) + AMS (Amp Mod Sens, 0-3)
            // VCED byte 8 is packed: (pms << 4) | ams. We re-use the
            // high nibble (pms) and stash ams in the low nibble of a
            // synthetic local; for the adapter we route them as two
            // separate 0..1 params so the user can pick one at a time.
            // In practice: pms uses the upper 4 bits of the byte, ams
            // the lower 2 bits. We mirror that here so a future bulk
            // dump round-trips.
            ramPatch->pms = (value >> 4) & 0x07;
            ramPatch->ams = value & 0x03;
            {
                uint8_t pms_ams = (ramPatch->pms << 4) | (ramPatch->ams & 0x03);
                writeReg(0x38 + voice, pms_ams);
            }
            break;
        case 13: // Key Offset / Transpose (0-48, offset 24 → -24..+24 semitones)
            // Stored as int8_t in the patch. The adapter will pass
            // 0..1 normalised, and we map to 0..48 with +24 centre.
            ramPatch->key_offset = (int8_t)((int)value - 24);
            break;
        default:
            break;
    }
}

// ===========================================================================
// writeVcedOperator — Write per-operator parameters (AR, D1R, D1L, OUT, CRS)
// Maps DX21 VCED parameter indices to OPM registers.
// Called from COPMEmuAdapter when per-op sliders change.
// ===========================================================================
void COPMEmu::writeVcedOperator(int op, int param, uint8_t value)
{
    int voice = m_sysexEditVoice;
    if (voice < 0 || voice >= kNumVoices) return;
    if (op < 0 || op >= 4) return;

    DX21_Patch* ramPatch = m_memory.getRamVoice(voice);
    if (!ramPatch) return;

    DX21_Operator& o = ramPatch->op[op];
    int slot = voice + OP_SLOT[op];

    switch (param) {
        case 0: // AR (Attack Rate, 0-31)
            o.ar = value & 0x1F;
            writeField(0x80 + slot, 0, 5, o.ar);
            break;
        case 2: // D1R (Decay1 Rate, 0-31)
            o.d1r = value & 0x1F;
            writeField(0xA0 + slot, 0, 5, o.d1r);
            break;
        case 8: // D1L (Decay1 Level, 0-15)
            o.d1l = value & 0x0F;
            {
                uint8_t d1l = 15 - (o.d1l & 0x0F);
                uint8_t rr = o.rr & 0x0F;
                uint8_t d1l_rr = (d1l << 4) | rr;
                writeReg(0xE0 + slot, d1l_rr);
            }
            break;
        case 10: // OUT (Output Level, 0-99)
            o.out = value;
            {
                uint8_t tl = (99 - o.out) * 127 / 99;
                if (tl > 127) tl = 127;
                writeReg(0x60 + slot, 0x80 | (tl & 0x7F));
            }
            break;
        case 11: // CRS (Coarse Ratio, 0-63)
            o.crs = value & 0x3F;
            {
                uint8_t mul = CRS_TO_MUL[o.crs & 0x3F];
                writeField(0x40 + slot, 0, 4, mul);
            }
            break;
        case 1: // D2R (Decay 2 Rate, 0-31) -- register 0xC0[4:0]
            o.d2r = value & 0x1F;
            writeField(0xC0 + slot, 0, 5, o.d2r);
            break;
        case 3: // RR (Release Rate, 0-15) -- packed in 0xE0[3:0]
            o.rr = value & 0x0F;
            {
                uint8_t d1l = 15 - (o.d1l & 0x0F);
                uint8_t d1l_rr = (d1l << 4) | o.rr;
                writeReg(0xE0 + slot, d1l_rr);
            }
            break;
        case 5: // LS (Level Scaling, 0-99) -- no direct OPM reg, applied per-note
            o.ls = value;
            if (o.ls > 99) o.ls = 99;
            break;
        case 6: // RS (Rate Scaling, 0-3) -- packed in 0x80[7:6] with AR
            o.rs = value & 0x03;
            {
                uint8_t ks_ar = ((o.rs & 0x03) << 6) | (o.ar & 0x1F);
                writeReg(0x80 + slot, ks_ar);
            }
            break;
        case 7: // EBS (EG Bias Sensitivity, 0-7) -- no direct OPM reg
            o.ebs = value & 0x07;
            break;
        case 13: // AME (AM Enable, 0/1) -- bit 7 of 0xA0
            o.ame = value & 0x01;
            {
                uint8_t ame_d1r = ((o.ame & 0x01) << 7) | (o.d1r & 0x1F);
                writeReg(0xA0 + slot, ame_d1r);
            }
            break;
        case 9: // KVS (Key Velocity Sensitivity, 0-7) -- no direct OPM reg
            o.kvs = value & 0x07;
            break;
        case 12: // DET (Detune, 0-6: 0=-3,1=-2,2=-1,3=0,4=+1,5=+2,6=+3)
            o.det = value & 0x07;
            {
                uint8_t dt1 = (o.det <= 6) ? DET_TO_DT1[o.det] : 0;
                uint8_t mul = (o.crs < 64) ? CRS_TO_MUL[o.crs] : 15;
                uint8_t dt1_mul = (dt1 << 4) | (mul & 0x0F);
                writeReg(0x40 + slot, dt1_mul);
            }
            break;
        default:
            break;
    }
}

// ===========================================================================
// setSysexEditVoice — select which voice slot receives real-time parameter changes
// ===========================================================================
void COPMEmu::setSysexEditVoice(int voice)
{
    if (voice < 0) voice = 0;
    if (voice >= kNumVoices) voice = kNumVoices - 1;
    m_sysexEditVoice = voice;
}
