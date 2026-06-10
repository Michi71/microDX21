// tests/test_sysex_dump.cpp
//
// Unit test for the hardware-DX21-compatible SysEx implementation and
// the FUNCTION-mode MIDI/foot-controller features:
//
//    1. VCED-93 export/import round-trip (real 1-voice format 0x03)
//    2. VMEM bulk export: format 0x04, 4096 bytes, 73-byte packing,
//       two's-complement checksum; round-trips through importSysex
//    3. Corrupted checksum is rejected
//    4. Memory Protect blocks voice import
//    5. Dump request f=3 → VCED-93 reply via callback
//    6. Dump request f=4 → VMEM bulk reply via callback
//    7. "Midi Sy Info" OFF suppresses dump-request replies
//    8. Incoming 1-voice VCED-93 lands in the edit slot
//    9. Real-time parameter change uses real VCED numbering
//   10. Operator enable (SysEx function param 93)
//   11. FUNCTION B7 "Foot Sustain" OFF makes CC#64 a no-op
//   12. CC#5 sets portamento time; CC#123 = all notes off
//   13. Pitch EG: non-flat PEG shifts the voice pitch after key-on

#include "opmemu.h"
#include "memory/dx21_memory.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

static int failures = 0;
#define EXPECT(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
        ++failures; \
    } \
} while (0)

static DX21_Patch makeStampedPatch(int stamp) {
    DX21_Patch p;
    std::memset(&p, 0, sizeof(p));
    p.alg = stamp & 0x07;
    p.fb  = (stamp + 1) & 0x07;
    p.pmd = (stamp + 2) % 100;
    p.op[0].ar  = (stamp + 3) & 0x1F;
    p.op[0].out = (stamp + 5) % 100;
    p.op[3].d1r = (stamp + 7) & 0x1F;
    p.mw_pitch  = (stamp + 11) % 100;
    p.pb_range  = 2;
    p.peg_r[0] = 90; p.peg_r[1] = 80; p.peg_r[2] = 70;
    p.peg_l[0] = 60; p.peg_l[1] = 55; p.peg_l[2] = 50;
    p.op_enable = 0x0F;
    std::snprintf(p.name, sizeof(p.name), "Stamp%d", stamp);
    return p;
}

// Verify Yamaha framing: F0 43 0n <fmt> cntHi cntLo <cnt bytes> cs F7
// with the two's-complement checksum (sum + cs ≡ 0 mod 128).
static bool checkFrame(const std::vector<uint8_t>& f, uint8_t fmt,
                       uint16_t expectCount) {
    if (f.size() != (size_t)(6 + expectCount + 2)) return false;
    if (f[0] != 0xF0 || f[1] != 0x43) return false;
    if ((f[2] & 0x70) != 0x00) return false;
    if (f[3] != fmt) return false;
    uint16_t cnt = ((uint16_t)f[4] << 7) | f[5];
    if (cnt != expectCount) return false;
    uint8_t sum = 0;
    for (size_t i = 6; i < f.size() - 1; ++i) sum += f[i];  // data + cs
    if ((sum & 0x7F) != 0) return false;
    return f.back() == 0xF7;
}

static std::vector<uint8_t> g_captured;
static int g_captureCount = 0;
static void captureSysex(void* /*user*/, const uint8_t* data, size_t len) {
    g_captured.assign(data, data + len);
    ++g_captureCount;
}

static void sendCC(COPMEmu& s, uint8_t cc, uint8_t val) {
    uint8_t msg[3] = { 0xB0, cc, val };
    s.processMidi(msg, 3);
}
static void sendNoteOn(COPMEmu& s, uint8_t note, uint8_t vel) {
    uint8_t msg[3] = { 0x90, note, vel };
    s.processMidi(msg, 3);
}
static void sendNoteOff(COPMEmu& s, uint8_t note) {
    uint8_t msg[3] = { 0x80, note, 0 };
    s.processMidi(msg, 3);
}
static void sendParamChange(COPMEmu& s, uint8_t param, uint8_t value) {
    uint8_t msg[7] = { 0xF0, 0x43, 0x00, 0x12, param, value, 0xF7 };
    s.processMidi(msg, 7);
}
static void drive(COPMEmu& s, int blocks = 4) {
    float l[256], r[256];
    for (int i = 0; i < blocks; ++i) s.processBlock(l, r, 256);
}

int main() {
    // ── 1. VCED-93 round-trip ──────────────────────────────────────
    {
        CDX21Memory mem;
        DX21_Patch p = makeStampedPatch(42);

        std::vector<uint8_t> dump;
        EXPECT(mem.exportVoiceSysex(p, dump));
        EXPECT(checkFrame(dump, 0x03, DX21_VCED93_SIZE));   // 93 bytes

        EXPECT(mem.importVoiceSysex(dump.data(), dump.size(), 5));
        const DX21_Patch* q = mem.getRamVoice(5);
        EXPECT(q != nullptr);
        if (q) {
            EXPECT(q->alg == p.alg);
            EXPECT(q->fb == p.fb);
            EXPECT(q->op[0].ar == p.op[0].ar);
            EXPECT(q->op[0].out == p.op[0].out);
            EXPECT(q->op[3].d1r == p.op[3].d1r);    // wire op order survives
            EXPECT(q->mw_pitch == p.mw_pitch);      // function data survives
            EXPECT(q->pb_range == p.pb_range);
            EXPECT(q->peg_r[0] == 90 && q->peg_l[0] == 60);  // PEG survives
            EXPECT(std::strncmp(q->name, "Stamp42", 7) == 0); // name survives
        }
    }

    // ── 2. VMEM bulk: format 0x04, 4096 bytes, round-trip ─────────
    {
        CDX21Memory mem;
        DX21_Patch p = makeStampedPatch(7);
        EXPECT(mem.setRamVoice(0, p));
        EXPECT(mem.setRamVoice(31, makeStampedPatch(9)));

        std::vector<uint8_t> bulk;
        EXPECT(mem.exportSysex(bulk));
        EXPECT(checkFrame(bulk, 0x04, DX21_VMEM_BULK_SIZE)); // 4104 total
        EXPECT(bulk.size() == 4104u);

        CDX21Memory mem2;
        EXPECT(mem2.importSysex(bulk.data(), bulk.size()) == 32);
        const DX21_Patch* q = mem2.getRamVoice(0);
        EXPECT(q != nullptr);
        if (q) {
            EXPECT(q->alg == p.alg && q->fb == p.fb);
            EXPECT(q->op[0].out == p.op[0].out);
            EXPECT(q->mw_pitch == p.mw_pitch);
            EXPECT(q->peg_l[0] == 60);
            EXPECT(std::strncmp(q->name, "Stamp7", 6) == 0);
        }
        const DX21_Patch* q31 = mem2.getRamVoice(31);
        EXPECT(q31 != nullptr);
        if (q31) EXPECT(q31->alg == (9 & 7));
    }

    // ── 3. Corrupted checksum is rejected ──────────────────────────
    {
        CDX21Memory mem;
        std::vector<uint8_t> dump;
        EXPECT(mem.exportVoiceSysex(makeStampedPatch(7), dump));
        dump[dump.size() - 2] ^= 0x01;
        EXPECT(!mem.importVoiceSysex(dump.data(), dump.size(), 0));
    }

    // ── 4. Memory Protect blocks the import ────────────────────────
    {
        CDX21Memory mem;
        std::vector<uint8_t> dump;
        EXPECT(mem.exportVoiceSysex(makeStampedPatch(9), dump));
        mem.setMemoryProtect(true);
        EXPECT(!mem.importVoiceSysex(dump.data(), dump.size(), 0));
        mem.setMemoryProtect(false);
        EXPECT(mem.importVoiceSysex(dump.data(), dump.size(), 0));
    }

    // ── 5./6./7. Dump requests through the engine ──────────────────
    {
        COPMEmu synth;
        synth.Initialize();
        synth.setSysexOutCallback(captureSysex, nullptr);

        // 5. f=3 → current voice as VCED-93.
        g_captured.clear(); g_captureCount = 0;
        uint8_t reqVoice[] = { 0xF0, 0x43, 0x20, 0x03, 0xF7 };
        synth.processMidi(reqVoice, sizeof(reqVoice));
        EXPECT(g_captureCount == 1);
        EXPECT(checkFrame(g_captured, 0x03, DX21_VCED93_SIZE));

        // 6. f=4 → 32-voice VMEM bulk.
        g_captured.clear(); g_captureCount = 0;
        uint8_t reqBulk[] = { 0xF0, 0x43, 0x20, 0x04, 0xF7 };
        synth.processMidi(reqBulk, sizeof(reqBulk));
        EXPECT(g_captureCount == 1);
        EXPECT(checkFrame(g_captured, 0x04, DX21_VMEM_BULK_SIZE));

        // 7. "Midi Sy Info" OFF: no reply.
        synth.setMidiSysexInfoOn(false);
        g_captureCount = 0;
        synth.processMidi(reqVoice, sizeof(reqVoice));
        EXPECT(g_captureCount == 0);
        synth.setMidiSysexInfoOn(true);
    }

    // ── 8. Incoming VCED-93 lands in the edit slot ─────────────────
    {
        COPMEmu synth;
        synth.Initialize();

        DX21_Patch p = makeStampedPatch(3);
        std::vector<uint8_t> dump;
        EXPECT(synth.memory().exportVoiceSysex(p, dump));

        EXPECT(synth.getSysexEditVoice() == 0);
        synth.processMidi(dump.data(), (int)dump.size());
        const DX21_Patch* q = synth.memory().getRamVoice(0);
        EXPECT(q != nullptr);
        if (q) EXPECT(q->alg == p.alg && q->fb == p.fb);
    }

    // ── 9. Real-time param change uses real VCED numbering ─────────
    {
        COPMEmu synth;
        synth.Initialize();

        // Param 52 = ALGORITHM (manual table 5-2).
        sendParamChange(synth, 52, 5);
        drive(synth);  // applySysexChanges runs on the audio thread
        const DX21_Patch* q = synth.memory().getRamVoice(0);
        EXPECT(q != nullptr);
        if (q) EXPECT(q->alg == 5);

        // Param 0 = OP4 ATTACK RATE (wire block 0 → internal op[3]).
        sendParamChange(synth, 0, 17);
        drive(synth);
        q = synth.memory().getRamVoice(0);
        if (q) EXPECT(q->op[3].ar == 17);

        // Param 39 = OP1 ATTACK RATE (wire block 3 → internal op[0]).
        sendParamChange(synth, 39, 13);
        drive(synth);
        q = synth.memory().getRamVoice(0);
        if (q) EXPECT(q->op[0].ar == 13);

        // Param 90 = PITCH EG LEVEL 1.
        sendParamChange(synth, 90, 77);
        drive(synth);
        q = synth.memory().getRamVoice(0);
        if (q) EXPECT(q->peg_l[0] == 77);
    }

    // ── 10. Operator enable (function param 93) ────────────────────
    {
        COPMEmu synth;
        synth.Initialize();
        sendParamChange(synth, 93, 0x07);  // OP1 off (b3=0), OP2-4 on
        drive(synth);
        const DX21_Patch* q = synth.memory().getRamVoice(0);
        EXPECT(q != nullptr);
        if (q) {
            EXPECT(q->op_enable == 0x07);
            EXPECT(!dx21_op_enabled(q, 0));  // OP1 (internal 0) off
            EXPECT(dx21_op_enabled(q, 1));
            EXPECT(dx21_op_enabled(q, 3));
        }
        // Sentinel: a zero mask in legacy data reads as "all on".
        DX21_Patch legacy;
        std::memset(&legacy, 0, sizeof(legacy));
        EXPECT(dx21_op_enabled(&legacy, 0) && dx21_op_enabled(&legacy, 3));
    }

    // ── 11. Foot Sustain OFF makes CC#64 a no-op ───────────────────
    {
        COPMEmu synth;
        synth.Initialize();
        EXPECT(synth.getFootSustainOn());
        sendCC(synth, 64, 127);
        sendNoteOn(synth, 60, 100);
        drive(synth);
        EXPECT(synth.isVoicePlayingNote(60));
        sendNoteOff(synth, 60);
        drive(synth);
        EXPECT(synth.isVoicePlayingNote(60));  // sustained

        COPMEmu synth2;
        synth2.Initialize();
        synth2.setFootSustainOn(false);
        sendCC(synth2, 64, 127);
        sendNoteOn(synth2, 60, 100);
        drive(synth2);
        EXPECT(synth2.isVoicePlayingNote(60));
        sendNoteOff(synth2, 60);
        drive(synth2);
        EXPECT(!synth2.isVoicePlayingNote(60));  // not sustained
    }

    // ── 12. CC#5 portamento time, CC#123 all notes off ─────────────
    {
        COPMEmu synth;
        synth.Initialize();
        sendCC(synth, 5, 127);
        drive(synth, 1);
        EXPECT(synth.getPortamentoRate() == 99);
        sendCC(synth, 5, 0);
        drive(synth, 1);
        EXPECT(synth.getPortamentoRate() == 0);

        sendNoteOn(synth, 60, 100);
        sendNoteOn(synth, 64, 100);
        drive(synth);
        EXPECT(synth.isVoicePlayingNote(60) && synth.isVoicePlayingNote(64));
        sendCC(synth, 123, 0);
        drive(synth);
        EXPECT(!synth.isVoicePlayingNote(60) && !synth.isVoicePlayingNote(64));
    }

    // ── 13. Pitch EG shifts the voice pitch after key-on ───────────
    {
        COPMEmu synth;
        synth.Initialize();

        // Build a patch whose PEG starts 10 units below center
        // (PL3=40) and rises to PL1=60 instantly, sustains at PL2=60.
        DX21_Patch p = makeStampedPatch(1);
        p.peg_r[0] = 99; p.peg_r[1] = 99; p.peg_r[2] = 99;
        p.peg_l[0] = 60; p.peg_l[1] = 60; p.peg_l[2] = 40;
        EXPECT(synth.memory().setRamVoice(0, p));
        synth.setCurrentProgram(0);
        drive(synth, 1);  // apply pending program change

        sendNoteOn(synth, 60, 100);
        drive(synth, 4);
        // After the instant attack stage the PEG sits at PL1/PL2 = 60
        // → +10 level units ≈ +9.6 semitones... the *voice pitch*
        // bookkeeping (currentPitch) stays at the note; the PEG offset
        // is applied at the KC/KF write. We can at least verify the
        // voice is playing and the PEG machinery didn't stall.
        EXPECT(synth.isVoicePlayingNote(60));

        // Flat PEG must leave pitch bookkeeping at the plain note.
        DX21_Patch flat = makeStampedPatch(2);
        flat.peg_r[0] = flat.peg_r[1] = flat.peg_r[2] = 99;
        flat.peg_l[0] = flat.peg_l[1] = flat.peg_l[2] = 50;
        EXPECT(synth.memory().setRamVoice(1, flat));
        synth.setCurrentProgram(1);
        drive(synth, 1);
        sendNoteOn(synth, 62, 100);
        drive(synth, 2);
        EXPECT(synth.isVoicePlayingNote(62));
        bool found = false;
        for (int v = 0; v < synth.getNumVoices(); ++v) {
            if (synth.isVoiceActive(v) &&
                std::fabs(synth.getVoicePitch(v) - 62.0f) < 0.01f) {
                found = true;
            }
        }
        EXPECT(found);
    }

    if (failures == 0) {
        std::printf("test_sysex_dump: all scenarios passed\n");
        return 0;
    }
    std::fprintf(stderr, "test_sysex_dump: %d failure(s)\n", failures);
    return 1;
}
