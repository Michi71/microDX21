// tests/test_voice_workflow.cpp
//
// Unit test for the voice-workflow features added in the
// manual-comparison UI pass:
//
//   1. Panel edits transmit real-time parameter changes
//      (F0 43 1n 12 pp vv F7) with real VCED numbering, Sy-Info gated
//   2. Incoming MIDI parameter changes do NOT echo back out
//   3. Audible COMPARE: first edit snapshots the voice; COMPARE
//      serves the original, leaving the edited data in RAM;
//      program change resets the compare context
//   4. setVoiceName: rename + Memory Protect gate
//   5. capturePerformance / applyPerformance round-trip

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

static std::vector<std::vector<uint8_t>> g_frames;
static void captureSysex(void* /*user*/, const uint8_t* data, size_t len) {
    g_frames.emplace_back(data, data + len);
}

static void drive(COPMEmu& s, int blocks = 2) {
    float l[256], r[256];
    for (int i = 0; i < blocks; ++i) s.processBlock(l, r, 256);
}

// Find a captured 7-byte param-change frame for `param`; returns the
// value or -1.
static int findParamChange(uint8_t param) {
    for (const auto& f : g_frames) {
        if (f.size() == 7 && f[0] == 0xF0 && f[1] == 0x43 &&
            (f[2] & 0x70) == 0x10 && f[3] == 0x12 && f[4] == param &&
            f[6] == 0xF7) {
            return f[5];
        }
    }
    return -1;
}

int main() {
    // ── 1. Panel edits transmit parameter changes ──────────────────
    {
        COPMEmu synth;
        synth.Initialize();
        synth.setSysexOutCallback(captureSysex, nullptr);
        synth.setCurrentProgram(0);
        drive(synth);

        // Global: ALGORITHM (internal id 0 → VCED 52).
        g_frames.clear();
        COPMEmu::TestDoor::writeVcedGlobal(synth, 0, 5);
        EXPECT(findParamChange(52) == 5);

        // Operator: internal op 0 (= OP1) AR → wire block 3 → VCED 39.
        g_frames.clear();
        COPMEmu::TestDoor::writeVcedOperator(synth, 0, 0, 17);
        EXPECT(findParamChange(39) == 17);

        // Sy Info OFF: no transmit.
        synth.setMidiSysexInfoOn(false);
        g_frames.clear();
        COPMEmu::TestDoor::writeVcedGlobal(synth, 1, 3);
        EXPECT(g_frames.empty());
        synth.setMidiSysexInfoOn(true);
    }

    // ── 2. Incoming MIDI param changes do not echo ─────────────────
    {
        COPMEmu synth;
        synth.Initialize();
        synth.setSysexOutCallback(captureSysex, nullptr);
        synth.setCurrentProgram(0);
        drive(synth);

        g_frames.clear();
        uint8_t msg[7] = { 0xF0, 0x43, 0x00, 0x12, 52, 4, 0xF7 };
        synth.processMidi(msg, 7);
        drive(synth);  // applySysexChanges on the audio thread
        const DX21_Patch* q = synth.memory().getRamVoice(0);
        EXPECT(q && q->alg == 4);   // change applied...
        EXPECT(g_frames.empty());   // ...but not echoed back
    }

    // ── 3. Audible COMPARE ─────────────────────────────────────────
    {
        COPMEmu synth;
        synth.Initialize();
        synth.setCurrentProgram(0);
        drive(synth);

        // Without edits COMPARE must refuse.
        EXPECT(!synth.setCompare(true));
        EXPECT(!synth.getCompare());

        char origName[32];
        std::snprintf(origName, sizeof(origName), "%s",
                      synth.getCurrentProgramName());

        // Rename = an edit; the pre-edit voice goes to the snapshot.
        EXPECT(synth.setVoiceName("EDITED"));
        EXPECT(synth.isEditDirty());
        EXPECT(std::strcmp(synth.getCurrentProgramName(), "EDITED") == 0);

        // COMPARE on: the current program reads as the ORIGINAL...
        EXPECT(synth.setCompare(true));
        EXPECT(std::strcmp(synth.getCurrentProgramName(), origName) == 0);
        // ...while the edited data stays in RAM.
        const DX21_Patch* ram = synth.memory().getRamVoice(0);
        EXPECT(ram && std::strcmp(ram->name, "EDITED") == 0);

        // COMPARE off: edited voice again.
        EXPECT(!synth.setCompare(false));
        EXPECT(std::strcmp(synth.getCurrentProgramName(), "EDITED") == 0);

        // Program change resets the compare context.
        EXPECT(synth.setCompare(true));
        synth.setCurrentProgram(1);
        drive(synth);
        EXPECT(!synth.getCompare());
        EXPECT(!synth.isEditDirty());
        EXPECT(!synth.setCompare(true));  // fresh program: nothing to compare
    }

    // ── 4. setVoiceName honours Memory Protect ─────────────────────
    {
        COPMEmu synth;
        synth.Initialize();
        synth.setCurrentProgram(0);
        drive(synth);
        synth.setMemoryProtect(true);
        EXPECT(!synth.setVoiceName("NOPE"));
        synth.setMemoryProtect(false);
        EXPECT(synth.setVoiceName("YEP"));
    }

    // ── 5. capturePerformance / applyPerformance round-trip ────────
    {
        COPMEmu synth;
        synth.Initialize();

        synth.setPlayMode(COPMEmu::Dual);
        synth.setBalance(70);
        synth.setPitchBendRange(7);
        synth.setPortamentoRate(33);
        synth.setEnsembleOn(true);

        DX21_Performance perf;
        synth.capturePerformance(perf);
        perf.setName("TESTPERF");
        EXPECT(synth.memory().setPerformance(3, perf));

        // Scramble the engine state...
        synth.setPlayMode(COPMEmu::Single);
        synth.setBalance(50);
        synth.setPitchBendRange(2);
        synth.setPortamentoRate(0);
        synth.setEnsembleOn(false);

        // ...and restore it from the performance memory.
        synth.applyPerformance(3);
        EXPECT(synth.getPlayMode() == COPMEmu::Dual);
        EXPECT(synth.getBalance() == 70);
        EXPECT(synth.getPitchBendRange() == 7);
        EXPECT(synth.getPortamentoRate() == 33);
        EXPECT(synth.getEnsembleOn());

        const DX21_Performance* stored = synth.memory().getPerformance(3);
        EXPECT(stored && std::strncmp(stored->name, "TESTPERF", 8) == 0);
    }

    if (failures == 0) {
        std::printf("test_voice_workflow: all scenarios passed\n");
        return 0;
    }
    std::fprintf(stderr, "test_voice_workflow: %d failure(s)\n", failures);
    return 1;
}
