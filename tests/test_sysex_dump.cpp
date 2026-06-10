// tests/test_sysex_dump.cpp
//
// Unit test for the SysEx dump-out path and the FUNCTION-mode
// MIDI/foot-controller features:
//
//   1. CDX21Memory::exportVoiceSysex / importVoiceSysex round-trip
//   2. importVoiceSysex rejects a corrupted checksum
//   3. importVoiceSysex honours Memory Protect
//   4. Dump request F0 43 2n 03 F7 → 1-voice VCED reply via callback
//   5. Dump request F0 43 2n 09 F7 → 32-voice bulk reply via callback
//   6. "Midi Sy Info" OFF suppresses dump-request replies
//   7. Incoming 1-voice VCED dump (modelId 0x03) lands in the edit slot
//   8. FUNCTION #31 "Foot Sustain" OFF makes CC#64 a no-op

#include "opmemu.h"
#include "memory/dx21_memory.h"

#include <cstdio>
#include <cstring>
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
    p.pmd = (stamp + 2) & 0x7F;
    p.op[0].ar  = (stamp + 3) & 0x1F;
    p.op[0].out = (stamp + 5) & 0x7F;
    std::snprintf(p.name, sizeof(p.name), "Stamp%d", stamp);
    return p;
}

// Verify the standard Yamaha framing of a dump produced by the synth:
// F0 43 0n <fmt> cntHi cntLo <cnt bytes> checksum F7.
static bool checkFrame(const std::vector<uint8_t>& f, uint8_t fmt,
                       uint16_t expectCount) {
    if (f.size() != (size_t)(6 + expectCount + 2)) return false;
    if (f[0] != 0xF0 || f[1] != 0x43) return false;
    if ((f[2] & 0x70) != 0x00) return false;
    if (f[3] != fmt) return false;
    uint16_t cnt = ((uint16_t)f[4] << 7) | f[5];
    if (cnt != expectCount) return false;
    uint8_t cs = 0;
    for (size_t i = 6; i < f.size() - 2; ++i) cs += f[i];
    if ((cs & 0x7F) != f[f.size() - 2]) return false;
    return f.back() == 0xF7;
}

// Callback capture buffer.
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
static void drive(COPMEmu& s, int blocks = 4) {
    float l[256], r[256];
    for (int i = 0; i < blocks; ++i) s.processBlock(l, r, 256);
}

int main() {
    // ── 1. exportVoiceSysex / importVoiceSysex round-trip ──────────
    {
        CDX21Memory mem;
        DX21_Patch p = makeStampedPatch(42);
        EXPECT(mem.setRamVoice(0, p));

        std::vector<uint8_t> dump;
        EXPECT(mem.exportVoiceSysex(p, dump));
        EXPECT(checkFrame(dump, 0x03, DX21_SYSEX_VCED_SIZE));

        EXPECT(mem.importVoiceSysex(dump.data(), dump.size(), 5));
        const DX21_Patch* q = mem.getRamVoice(5);
        EXPECT(q != nullptr);
        if (q) {
            EXPECT(q->alg == p.alg);
            EXPECT(q->fb == p.fb);
            EXPECT(q->op[0].ar == p.op[0].ar);
            EXPECT(q->op[0].out == p.op[0].out);
        }
    }

    // ── 2. Corrupted checksum is rejected ──────────────────────────
    {
        CDX21Memory mem;
        std::vector<uint8_t> dump;
        EXPECT(mem.exportVoiceSysex(makeStampedPatch(7), dump));
        dump[dump.size() - 2] ^= 0x01;  // break the checksum
        EXPECT(!mem.importVoiceSysex(dump.data(), dump.size(), 0));
    }

    // ── 3. Memory Protect blocks the import ────────────────────────
    {
        CDX21Memory mem;
        std::vector<uint8_t> dump;
        EXPECT(mem.exportVoiceSysex(makeStampedPatch(9), dump));
        mem.setMemoryProtect(true);
        EXPECT(!mem.importVoiceSysex(dump.data(), dump.size(), 0));
        mem.setMemoryProtect(false);
        EXPECT(mem.importVoiceSysex(dump.data(), dump.size(), 0));
    }

    // ── 4./5./6. Dump requests through the engine ──────────────────
    {
        COPMEmu synth;
        synth.Initialize();
        synth.setSysexOutCallback(captureSysex, nullptr);

        // 4. Request the current voice (1-voice VCED).
        g_captured.clear(); g_captureCount = 0;
        uint8_t reqVoice[] = { 0xF0, 0x43, 0x20, 0x03, 0xF7 };
        synth.processMidi(reqVoice, sizeof(reqVoice));
        EXPECT(g_captureCount == 1);
        EXPECT(checkFrame(g_captured, 0x03, DX21_SYSEX_VCED_SIZE));

        // 5. Request the 32-voice bulk.
        g_captured.clear(); g_captureCount = 0;
        uint8_t reqBulk[] = { 0xF0, 0x43, 0x20, 0x09, 0xF7 };
        synth.processMidi(reqBulk, sizeof(reqBulk));
        EXPECT(g_captureCount == 1);
        EXPECT(checkFrame(g_captured, 0x09, DX21_SYSEX_BULK_SIZE));

        // 6. "Midi Sy Info" OFF: no reply.
        synth.setMidiSysexInfoOn(false);
        g_captureCount = 0;
        synth.processMidi(reqVoice, sizeof(reqVoice));
        EXPECT(g_captureCount == 0);
        synth.setMidiSysexInfoOn(true);
    }

    // ── 7. Incoming 1-voice dump lands in the edit slot ────────────
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

        // With Memory Protect ON the same dump must bounce off.
        DX21_Patch p2 = makeStampedPatch(4);
        std::vector<uint8_t> dump2;
        EXPECT(synth.memory().exportVoiceSysex(p2, dump2));
        synth.setMemoryProtect(true);
        synth.processMidi(dump2.data(), (int)dump2.size());
        q = synth.memory().getRamVoice(0);
        EXPECT(q != nullptr);
        if (q) EXPECT(q->fb == p.fb);  // still the old stamp
        synth.setMemoryProtect(false);
    }

    // ── 8. Foot Sustain OFF makes CC#64 a no-op ────────────────────
    {
        // Default (ON): pedal holds the released note.
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

        // OFF: the pedal is ignored, the note releases normally.
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

    if (failures == 0) {
        std::printf("test_sysex_dump: all scenarios passed\n");
        return 0;
    }
    std::fprintf(stderr, "test_sysex_dump: %d failure(s)\n", failures);
    return 1;
}
