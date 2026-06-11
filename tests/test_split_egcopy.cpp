// tests/test_split_egcopy.cpp
//
// Unit tests for two DX21 features:
//
//   1. SPLIT MONO+POLY mixing ("7+1"): poly/mono is a per-voice
//      (VCED 63) flag, so in SPLIT mode each side follows its own
//      patch. A MONO side gets 1 note, the POLY partner 7; both
//      POLY → 4+4, both MONO → 1+1.
//
//   2. EG COPY: copy AR/D1R/D1L/D2R/RR from one operator to
//      another in the current edit voice (panel utility).
//
#include "opmemu.h"
#include "io/std_filesystem.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::fprintf(stderr, "FAIL: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
    ++failures; } } while (0)

static void noteOn(COPMEmu& synth, uint8_t note, uint8_t vel = 100) {
    uint8_t msg[3] = { 0x90, note, vel };
    synth.processMidi(msg, 3);
}

static void noteOff(COPMEmu& synth, uint8_t note) {
    uint8_t msg[3] = { 0x80, note, 0 };
    synth.processMidi(msg, 3);
}

static void pumpFor(COPMEmu& synth, int totalSamples) {
    float l[256], r[256];
    int remaining = totalSamples;
    while (remaining > 0) {
        int n = std::min(remaining, 256);
        synth.processBlock(l, r, n);
        remaining -= n;
    }
}

static int countActiveVoices(const COPMEmu& synth, int from = 0, int to = 8) {
    int count = 0;
    for (int i = from; i < to; ++i) {
        if (synth.isVoiceActive(i)) ++count;
    }
    return count;
}

static void allOff(COPMEmu& synth) {
    for (int n = 0; n < 128; ++n) noteOff(synth, (uint8_t)n);
    pumpFor(synth, 4096);
}

// Configure SPLIT with RAM patches 0 (side A) / 1 (side B) and the
// given mono flags, split point C3 (60).
static void setupSplit(COPMEmu& synth, bool monoA, bool monoB) {
    DX21_Patch* pa = synth.memory().getRamVoice(0);
    DX21_Patch* pb = synth.memory().getRamVoice(1);
    pa->mono = monoA ? 1 : 0;
    pb->mono = monoB ? 1 : 0;
    synth.setPlayMode(COPMEmu::Split);
    synth.setSplitPoint(60);
    synth.setPatchA(0);
    synth.setPatchB(1);
    pumpFor(synth, 512);
}

// Test 1: classic POLY+POLY split stays 4+4.
static void test_split_4_4() {
    std::fprintf(stderr, "test 1: SPLIT poly+poly = 4+4...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();
    setupSplit(synth, false, false);

    for (uint8_t n = 40; n < 46; ++n) { noteOn(synth, n); pumpFor(synth, 256); }
    EXPECT(countActiveVoices(synth, 0, 4) == 4);   // low side capped at 4
    EXPECT(countActiveVoices(synth, 4, 8) == 0);

    for (uint8_t n = 70; n < 76; ++n) { noteOn(synth, n); pumpFor(synth, 256); }
    EXPECT(countActiveVoices(synth, 4, 8) == 4);   // high side capped at 4
}

// Test 2: POLY low + MONO high = 7+1.
static void test_split_7_1() {
    std::fprintf(stderr, "test 2: SPLIT poly+mono = 7+1...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();
    setupSplit(synth, false, true);

    // Low (poly) side gets 7 voices (0..6)
    for (uint8_t n = 40; n < 48; ++n) { noteOn(synth, n); pumpFor(synth, 256); }
    EXPECT(countActiveVoices(synth, 0, 7) == 7);
    EXPECT(countActiveVoices(synth, 7, 8) == 0);

    // High (mono) side: one voice (7), retriggered last-note-priority
    noteOn(synth, 72); pumpFor(synth, 256);
    EXPECT(synth.isVoiceActive(7));
    EXPECT(synth.isVoicePlayingNote(72));
    noteOn(synth, 74); pumpFor(synth, 256);
    EXPECT(countActiveVoices(synth, 7, 8) == 1);   // still mono
    EXPECT(synth.isVoicePlayingNote(74));
    EXPECT(!synth.isVoicePlayingNote(72));

    // Poly side survives untouched
    EXPECT(countActiveVoices(synth, 0, 7) == 7);
}

// Test 3: MONO low + POLY high = 1+7.
static void test_split_1_7() {
    std::fprintf(stderr, "test 3: SPLIT mono+poly = 1+7...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();
    setupSplit(synth, true, false);

    noteOn(synth, 40); pumpFor(synth, 256);
    noteOn(synth, 45); pumpFor(synth, 256);
    EXPECT(countActiveVoices(synth, 0, 1) == 1);   // mono: 1 note max
    EXPECT(synth.isVoicePlayingNote(45));
    EXPECT(!synth.isVoicePlayingNote(40));

    for (uint8_t n = 70; n < 78; ++n) { noteOn(synth, n); pumpFor(synth, 256); }
    EXPECT(countActiveVoices(synth, 1, 8) == 7);   // poly side: 7 voices
}

// Test 4: MONO+MONO = 1+1.
static void test_split_1_1() {
    std::fprintf(stderr, "test 4: SPLIT mono+mono = 1+1...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();
    setupSplit(synth, true, true);

    noteOn(synth, 40); pumpFor(synth, 256);
    noteOn(synth, 42); pumpFor(synth, 256);
    noteOn(synth, 70); pumpFor(synth, 256);
    noteOn(synth, 72); pumpFor(synth, 256);
    EXPECT(countActiveVoices(synth) == 2);
    EXPECT(synth.isVoicePlayingNote(42));
    EXPECT(synth.isVoicePlayingNote(72));
}

// Test 5: layout rebuild when the mono flag changes mid-play.
static void test_split_layout_switch() {
    std::fprintf(stderr, "test 5: SPLIT layout rebuild on patch change...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();
    setupSplit(synth, false, false);          // 4+4

    for (uint8_t n = 40; n < 44; ++n) { noteOn(synth, n); pumpFor(synth, 256); }
    EXPECT(countActiveVoices(synth, 0, 4) == 4);

    // Flip side B to a MONO patch → boundary moves 4 → 7, all voices
    // are freed (no stale notes outside the new ranges).
    synth.memory().getRamVoice(2)->mono = 1;
    synth.setPatchB(2);
    pumpFor(synth, 512);
    EXPECT(countActiveVoices(synth) == 0);

    // New layout active: 7 poly low notes fit
    for (uint8_t n = 40; n < 47; ++n) { noteOn(synth, n); pumpFor(synth, 256); }
    EXPECT(countActiveVoices(synth, 0, 7) == 7);
    allOff(synth);
}

// Test 6: EG COPY copies exactly the 5 EG params.
static void test_eg_copy() {
    std::fprintf(stderr, "test 6: EG COPY...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();
    synth.setCurrentProgram(0);
    pumpFor(synth, 512);

    // Give OP1 a distinctive EG and OP3 distinctive non-EG params.
    COPMEmu::TestDoor::writeVcedOperator(synth, 0, 0, 21);  // AR
    COPMEmu::TestDoor::writeVcedOperator(synth, 0, 2, 13);  // D1R
    COPMEmu::TestDoor::writeVcedOperator(synth, 0, 8,  9);  // D1L
    COPMEmu::TestDoor::writeVcedOperator(synth, 0, 1,  7);  // D2R
    COPMEmu::TestDoor::writeVcedOperator(synth, 0, 3,  5);  // RR
    COPMEmu::TestDoor::writeVcedOperator(synth, 2, 10, 33); // OP3 OUT
    COPMEmu::TestDoor::writeVcedOperator(synth, 2, 11, 17); // OP3 CRS

    EXPECT(synth.egCopy(0, 2));               // OP1 → OP3

    const DX21_Patch* p = synth.memory().getRamVoice(0);
    EXPECT(p != nullptr);
    if (p) {
        EXPECT(p->op[2].ar  == 21);
        EXPECT(p->op[2].d1r == 13);
        EXPECT(p->op[2].d1l ==  9);
        EXPECT(p->op[2].d2r ==  7);
        EXPECT(p->op[2].rr  ==  5);
        // Source untouched
        EXPECT(p->op[0].ar  == 21 && p->op[0].rr == 5);
        // Non-EG params of the destination untouched
        EXPECT(p->op[2].out == 33);
        EXPECT(p->op[2].crs == 17);
    }

    // Rejections: src == dst, out of range
    EXPECT(!synth.egCopy(1, 1));
    EXPECT(!synth.egCopy(-1, 2));
    EXPECT(!synth.egCopy(0, 4));

    // Memory Protect gates the copy
    synth.setMemoryProtect(true);
    EXPECT(!synth.egCopy(0, 1));
    synth.setMemoryProtect(false);
    EXPECT(synth.egCopy(0, 1));
}

int main() {
    test_split_4_4();
    test_split_7_1();
    test_split_1_7();
    test_split_1_1();
    test_split_layout_switch();
    test_eg_copy();

    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::fprintf(stderr, "all SPLIT/EG-COPY tests passed\n");
    return 0;
}
