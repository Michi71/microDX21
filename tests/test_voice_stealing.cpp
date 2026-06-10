// tests/test_voice_stealing.cpp
//
// Unit test for COPMEmu's voice-stealing strategy with the
// sustain-aware priority added in this change.

#include "opmemu.h"
#include "io/std_filesystem.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cassert>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s @ %s:%d\n", #cond, __FILE__, __LINE__); ++failures; } } while(0)

static void noteOn(COPMEmu& synth, uint8_t note, uint8_t vel = 100) {
    uint8_t msg[3] = { 0x90, note, vel };
    synth.processMidi(msg, 3);
}

static void noteOff(COPMEmu& synth, uint8_t note) {
    uint8_t msg[3] = { 0x80, note, 0 };
    synth.processMidi(msg, 3);
}

static void sustainOn(COPMEmu& synth) {
    uint8_t msg[3] = { 0xB0, 64, 127 };
    synth.processMidi(msg, 3);
}

static void sustainOff(COPMEmu& synth) {
    uint8_t msg[3] = { 0xB0, 64, 0 };
    synth.processMidi(msg, 3);
}

// Drive the synth a number of samples. processBlock internally
// calls processMidiBuffer which dequeues events. The noteOn path
// is staggered (max 2 per processMidiBuffer call), so for 8 noteOns
// we need 4 processBlock calls (= 1024 samples with 256-sample blocks).
static void pumpFor(COPMEmu& synth, int totalSamples) {
    float l[256], r[256];
    int remaining = totalSamples;
    while (remaining > 0) {
        int n = std::min(remaining, 256);
        synth.processBlock(l, r, n);
        remaining -= n;
    }
}

static int countActiveVoices(const COPMEmu& synth) {
    int n = synth.getNumVoices();
    int count = 0;
    for (int i = 0; i < n; ++i) {
        if (synth.isVoiceActive(i)) ++count;
    }
    return count;
}

// Test 1: oldest-voice stealing without sustain.
static void test_oldest_steal_no_sustain() {
    std::fprintf(stderr, "test 1: oldest steal (no sustain)...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();

    // Play 8 notes in order
    for (uint8_t n = 60; n < 68; ++n) {
        noteOn(synth, n);
        pumpFor(synth, 256);  // drain after each to avoid stagger
    }
    EXPECT(countActiveVoices(synth) == 8);

    // Now play a 9th note — should steal voice playing note 60 (oldest)
    noteOn(synth, 80);
    pumpFor(synth, 256);
    EXPECT(countActiveVoices(synth) == 8);
    EXPECT(!synth.isVoicePlayingNote(60));
    EXPECT(synth.isVoicePlayingNote(80));
}

// Test 2: sustain-aware steal.
//
// Setup: 4 voices that are sustained (held by pedal), 4 voices that are
// not. Trigger a 9th note. Verify the stolen voice is non-sustained.
static void test_sustain_aware_steal() {
    std::fprintf(stderr, "test 2: sustain-aware steal...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();

    // Step 1: Play 8 notes (all non-sustained) with no noteOff. All
    // 8 voices become active.
    for (uint8_t n = 60; n < 68; ++n) {
        noteOn(synth, n);
        pumpFor(synth, 256);
    }
    EXPECT(countActiveVoices(synth) == 8);

    // Step 2: Engage sustain. The currently-active voices are NOT
    // auto-marked sustained (sustain only marks notes sustained at
    // the moment of noteOff). So we need to re-issue noteOff for the
    // 4 voices we want to be sustained.
    sustainOn(synth);
    pumpFor(synth, 256);

    // Step 3: noteOff the first 4 notes (60, 61, 62, 63) — with
    // sustain on, they become sustained.
    for (uint8_t n = 60; n < 64; ++n) {
        noteOff(synth, n);
        pumpFor(synth, 256);
    }

    // Verify: 4 sustained + 4 non-sustained voices.
    int n_sustained = 0, n_non_sustained = 0;
    for (int i = 0; i < synth.getNumVoices(); ++i) {
        if (synth.isVoiceActive(i)) {
            if (synth.isVoiceSustained(i)) ++n_sustained;
            else ++n_non_sustained;
        }
    }
    EXPECT(n_sustained == 4);
    EXPECT(n_non_sustained == 4);

    // Step 4: Play a 9th note. Should steal a non-sustained voice.
    noteOn(synth, 90);
    pumpFor(synth, 256);
    EXPECT(countActiveVoices(synth) == 8);

    // All 4 sustained voices should still be active.
    for (int i = 0; i < synth.getNumVoices(); ++i) {
        if (synth.isVoiceSustained(i)) {
            EXPECT(synth.isVoiceActive(i));
        }
    }
    EXPECT(synth.isVoicePlayingNote(90));
}

// Test 3: extreme polyphony — all 8 voices sustained. Steal the oldest.
static void test_all_sustained_steal() {
    std::fprintf(stderr, "test 3: all-sustained steal (fallback)...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();

    // Engage sustain pedal
    sustainOn(synth);
    pumpFor(synth, 256);

    // Play 8 notes with sustain on. Each note gets sustained when we
    // noteOff it.
    for (uint8_t n = 60; n < 68; ++n) {
        noteOn(synth, n);
        pumpFor(synth, 256);
        noteOff(synth, n);
        pumpFor(synth, 256);
    }
    EXPECT(countActiveVoices(synth) == 8);
    for (int i = 0; i < synth.getNumVoices(); ++i) {
        EXPECT(synth.isVoiceSustained(i));
    }

    // Play a 9th note — all voices sustained, must steal oldest.
    noteOn(synth, 80);
    pumpFor(synth, 256);
    EXPECT(countActiveVoices(synth) == 8);
    EXPECT(!synth.isVoicePlayingNote(60));
    EXPECT(synth.isVoicePlayingNote(80));
}

int main() {
    test_oldest_steal_no_sustain();
    test_sustain_aware_steal();
    test_all_sustained_steal();

    if (failures == 0) {
        std::fprintf(stderr, "ALL TESTS PASSED\n");
        return 0;
    } else {
        std::fprintf(stderr, "%d TEST(S) FAILED\n", failures);
        return 1;
    }
}
