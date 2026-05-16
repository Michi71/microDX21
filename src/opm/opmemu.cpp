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
// The DX21 uses a YM2164 (OPP) chip, which has the same 8 algorithm topologies
// as the YM2151 (OPM). The DX21 algorithm number maps 1:1 to the YM2151 CON
// register field (bits [2:0] of register 0x20). This is confirmed by the
// picoX21H reference project which uses alg directly as CON on real hardware.
//
// YM2151 Algorithm Topologies (CON = algorithm number):
//
//   CON=0: M1→C1→OUT, M2→C2→OUT  (two independent FM pairs)
//   CON=1: M1→C1→OUT, M2 + C2→OUT (FM pair + two additive)
//   CON=2: M1 + C1→OUT, M2→C2→OUT (two additive + FM pair)
//   CON=3: M1→C1→C2→OUT, M2→C2→OUT (serial FM + parallel modulator)
//   CON=4: M1→M2→C2→OUT, C1→OUT   (3-op chain + additive)
//   CON=5: M1→C1→OUT, M2→C2→OUT   (two FM pairs)
//   CON=6: M1→(M2+C2), C1→C2→OUT  (complex)
//   CON=7: M1+C1+M2+C2→OUT         (all additive)
//
// DX21 Operator → YM2151 Slot Mapping (per picoX21H reference):
//   DX21 OP1 → YM2151 M1  (register offset +0)
//   DX21 OP2 → YM2151 M2  (register offset +8)
//   DX21 OP3 → YM2151 C1  (register offset +16)
//   DX21 OP4 → YM2151 C2  (register offset +24)
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
COPMEmu::COPMEmu()
    : m_currentPatch(0)
    , m_sustainPedal(false)
    , m_voiceAge(0)
    , m_cycleAccum(0)
    , m_pendingCount(0)
{
    m_filterL.init(kSampleRate, kFilterCutoff);
    m_filterR.init(kSampleRate, kFilterCutoff);
    memset(&m_chip, 0, sizeof(m_chip));
    memset(m_shadow, 0, sizeof(m_shadow));
    memset(m_pendingL, 0, sizeof(m_pendingL));
    memset(m_pendingR, 0, sizeof(m_pendingR));
    for (int i = 0; i < kNumVoices; ++i) {
        m_voices[i] = { false, false, 0, 0, 0, 0 };
    }
}

// ===========================================================================
// Initialize — OPM Reset, alle Kanäle stumm schalten
// ===========================================================================
void COPMEmu::Initialize()
{
    OPM_Reset(&m_chip, opm_flags_ym2164);

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
    if (m_chip.opp) {
        m_chip.reg_data_ready = 0;
        m_chip.reg_address_ready = 0;
    }
    clockChip(64);

    // Reset output filters to avoid clicks from stale state
    m_filterL.reset();
    m_filterR.reset();
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
    if (m_chip.opp && (reg & 0xf8) == 0x20) {
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
void COPMEmu::applyTL(int voice, const DX21_Patch& patch, int midiNote, int velocity)
{
    for (int op = 0; op < 4; ++op) {
        int slot = voice + OP_SLOT[op];
        const DX21_Operator& o = patch.op[op];

        uint8_t tl = (99 - o.out) * 127 / 99;
        tl += computeLSOffset(o.ls, midiNote);
        tl += computeKVSOffset(o.kvs, velocity);
        if (tl > 127) tl = 127;
        writeReg(0x60 + slot, 0x80 | (tl & 0x7F));  // bit 7 = OPP ramp enable
    }
}

// ===========================================================================
// setFrequency
// ===========================================================================
void COPMEmu::setFrequency(int voice, bool keyOn)
{
    int note = m_voices[voice].note;

    // KC layout: chip-octave changes between C and C# (not between B and C),
    // and 'C' is the top of an octave (nibble 14). We shift the boundary
    // by subtracting 1 and reduce the octave by 1 to map:
    //   MIDI 60 (C4)  → KC 0x3E  (chip-oct 3, nibble C)
    //   MIDI 61 (C#4) → KC 0x40  (chip-oct 4, nibble C#)
    //   MIDI 69 (A4)  → KC 0x4A  (chip-oct 4, nibble A) → 440 Hz on real chip
    int n = note - 1;
    if (n < 0) n = 0;
    int octave = n / 12 - 1;
    if (octave < 0) octave = 0;
    if (octave > 7) octave = 7;             // 3-bit block field
    unsigned key = KC_TABLE[n % 12];
    uint8_t kc   = static_cast<uint8_t>((octave << 4) | key);

    writeReg(0x28 + voice, kc);
    writeReg(0x30 + voice, 0x00);           // KF=0 (no sub-semitone tuning)

    if (keyOn) {
        // YM2151 KeyOn register: bits[6:3]=operator flags, bits[2:0]=channel
        writeReg(0x08, OP_ALL | static_cast<uint8_t>(voice));
    } else {
        writeReg(0x08, static_cast<uint8_t>(voice));
    }
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
int COPMEmu::allocVoice(int note)
{
    // Search by original MIDI note (retrigger)
    for (int i = 0; i < kNumVoices; ++i) {
        if (m_voices[i].active && m_voices[i].origNote == note) {
            // Retrigger: KeyOff first to reset envelope, then fade
            writeReg(0x08, static_cast<uint8_t>(i));
            for (int op = 0; op < 4; ++op) {
                int slot = i + OP_SLOT[op];
                writeReg(0x60 + slot, 0xFF);  // TL=max + OPP ramp → silence
            }
            m_voices[i].age = m_voiceAge++;
            return i;
        }
    }
    // Find a free voice
    for (int i = 0; i < kNumVoices; ++i) {
        if (!m_voices[i].active) return i;
    }
    // Steal oldest voice — smooth fade-out
    int oldest = 0;
    for (int i = 1; i < kNumVoices; ++i) {
        if (m_voices[i].age < m_voices[oldest].age) oldest = i;
    }
    // KeyOff starts the release phase
    writeReg(0x08, static_cast<uint8_t>(oldest));
    // Fade to silence: TL → max with OPP ramp, RR → maximum for fast release
    for (int op = 0; op < 4; ++op) {
        int slot = oldest + OP_SLOT[op];
        writeReg(0x60 + slot, 0xFF);  // TL=max + OPP ramp → smooth fade
        writeReg(0xE0 + slot, (0x0F << 0));  // RR=15 → fastest release
    }
    m_voices[oldest].active = false;
    m_voices[oldest].sustained = false;
    return oldest;
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

    int voice = allocVoice(note);
    if (voice < 0) return;

    const DX21_Patch& patch = dx21_patches[m_currentPatch];
    int transposed = note + patch.key_offset;
    if (transposed < 0) transposed = 0;
    if (transposed > 127) transposed = 127;

    m_voices[voice].active = true;
    m_voices[voice].sustained = false;
    m_voices[voice].origNote = note;
    m_voices[voice].note = transposed;
    m_voices[voice].velocity = velocity;
    m_voices[voice].age = m_voiceAge++;

    // Phase-reset click mask: KeyOn resets all operator phase counters to 0,
    // producing a brief transient. To mask this, we start from silence (TL=max)
    // and fade in via the OPP TL-ramp. Sequence:
    //   1. TL = max attenuation (0xFF) — mute the voice
    //   2. KC, KF, KeyOn — operators start from phase 0, but output is silent
    //   3. Target TL with OPP ramp — smooth fade-in from silence (~2 ms)
    for (int op = 0; op < 4; ++op) {
        writeReg(0x60 + voice + OP_SLOT[op], 0xFF);
    }
    setFrequency(voice, true);
    applyTL(voice, patch, transposed, velocity);
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

    int readPos  = m_midiReadPos.load(std::memory_order_relaxed);
    int writePos = m_midiWritePos.load(std::memory_order_acquire);
    int count = writePos - readPos;
    // Safety: if count looks unreasonable, something went wrong
    if (count < 0 || count > kMidiRingSize) count = 0;

    for (int i = 0; i < count; ++i) {
        MidiEvent ev = m_midiRing[(readPos + i) & kMidiRingMask];
        uint8_t status = ev.status & 0xF0;

        if (status == 0x90) {
            if (ev.data2 > 0) {
                noteOn(ev.data1, ev.data2);
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
    static const float kScale = 1.0f / 32768.0f;
    static const uint32_t kFracThreshold = 2 * kSampleRate;  // 96000
    int32_t dac[2];

    // MIDI-Events verarbeiten. Die writeReg-Aufrufe darin hängen neue
    // DAC-Samples an den bestehenden Pending-Puffer an. Getragene Daten
    // aus dem vorherigen processBlock-Aufruf werden zuerst konsumiert.
    processMidiBuffer();

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

        outputL[i] = m_filterL.process(static_cast<float>(sumL) / cycles * kScale);
        outputR[i] = m_filterR.process(static_cast<float>(sumR) / cycles * kScale);
    }

    // --- Ensemble/Chorus effect ---
    // DX21-style stereo chorus: 3 modulated delay lines with 120° phase offsets.
    // LFO1 ~0.5 Hz (slow vibrato), LFO2 ~3 Hz (fast shimmer, lower depth).
    // The mix of 85% slow + 15% fast creates a lush, organic chorus sound.
    //
    // IMPORTANT: LFO phases are in RADIANS (0..2pi per cycle) so sinf() works
    // correctly.  
    if (m_ensembleOn) {
        static constexpr float kTwoPi      = 6.2831853071795864f;
        static constexpr float kLfo1Freq   = 0.5f;    // Hz - slow vibrato
        static constexpr float kLfo2Freq   = 3.0f;    // Hz - fast shimmer
        static constexpr float kLfo1Inc     = kLfo1Freq * kTwoPi / kSampleRate;  // radians/sample
        static constexpr float kLfo2Inc     = kLfo2Freq * kTwoPi / kSampleRate;
        static constexpr float kPhaseOffset1 = kTwoPi / 3.0f;      // 120 deg in radians
        static constexpr float kPhaseOffset2 = kTwoPi * 2.0f / 3.0f; // 240 deg

        // Delay parameters (at 48 kHz) - scaled from GS1 reference (180/60 at 34.6kHz)
        static constexpr float kBaseDelay = 250.0f;   // ~5.2 ms base delay
        static constexpr float kModDepth   = 85.0f;    // ~1.8 ms modulation depth

        for (int i = 0; i < numSamples; ++i) {
            // LFO modulation: 85% slow + 15% fast (phases in radians)
            float modA = 0.85f * sinf(m_lfo1Phase)                + 0.15f * sinf(m_lfo2Phase);
            float modB = 0.85f * sinf(m_lfo1Phase + kPhaseOffset1) + 0.15f * sinf(m_lfo2Phase + kPhaseOffset1);
            float modC = 0.85f * sinf(m_lfo1Phase + kPhaseOffset2) + 0.15f * sinf(m_lfo2Phase + kPhaseOffset2);

            // Set modulated delay times
            m_delayA.setDelay(kBaseDelay + modA * kModDepth);
            m_delayB.setDelay(kBaseDelay + modB * kModDepth);
            m_delayC.setDelay(kBaseDelay + modC * kModDepth);

            // Push dry signal into delay lines
            m_delayA.pushSample(outputL[i]);
            m_delayB.pushSample(outputL[i]);
            m_delayC.pushSample(outputR[i]);

            // Pop modulated signal from delay lines
            float sampA = m_delayA.popSample();
            float sampB = m_delayB.popSample();
            float sampC = m_delayC.popSample();

            // Stereo mix: L = dry/2 + A/2 - B*0.3, R = dry/2 + C/2 - B*0.3
            outputL[i] = outputL[i] * 0.5f + sampA * 0.5f - sampB * 0.3f;
            outputR[i] = outputR[i] * 0.5f + sampC * 0.5f - sampB * 0.3f;

            // Advance LFO phases (radians, wrap at 2pi)
            m_lfo1Phase += kLfo1Inc;
            m_lfo2Phase += kLfo2Inc;
            if (m_lfo1Phase >= kTwoPi) m_lfo1Phase -= kTwoPi;
            if (m_lfo2Phase >= kTwoPi) m_lfo2Phase -= kTwoPi;
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
const char* COPMEmu::getCurrentProgramName() { return dx21_patches[m_currentPatch].name; }
const char* COPMEmu::getProgramName(int index) {
    if (index >= 0 && index < TOTAL_OPM_PATCHES)
        return dx21_patches[index].name;
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
    const DX21_Patch& patch = dx21_patches[m_currentPatch];

    // Write ALL static patch parameters for ALL 8 voices.
    // approach: write everything at program change time,
    // not per noteOn. Per-note only writes TL (with LS/KVS), KC, KF, KeyOn.
    for (int v = 0; v < kNumVoices; ++v) {
        applyPatchToVoice(v, patch);
    }

    // Global LFO registers
    writeReg(0x18, patch.lfo_speed);
    writeReg(0x19, patch.amd & 0x7F);            // AMD
    writeReg(0x19, (patch.pmd & 0x7F) | 0x80);   // PMD (bit 7=1)
    writeField(0x1B, 0, 2, patch.lfo_wave & 0x03);
}
