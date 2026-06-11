// tests/test_active_sensing.cpp
//
// MIDI Active-Sensing (0xFE) watchdog: after the first 0xFE the
// DX21 expects MIDI data at least every ~300 ms. On timeout it
// performs All Notes Off and disarms until the next 0xFE. Without
// any 0xFE the watchdog must never fire.
//
#include "opmemu.h"
#include "io/std_filesystem.h"
#include <algorithm>
#include <cstdio>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::fprintf(stderr, "FAIL: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
    ++failures; } } while (0)

static void noteOn(COPMEmu& synth, uint8_t note, uint8_t vel = 100) {
    uint8_t msg[3] = { 0x90, note, vel };
    synth.processMidi(msg, 3);
}

static void activeSense(COPMEmu& synth) {
    uint8_t msg[1] = { 0xFE };
    synth.processMidi(msg, 1);
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

static int countActiveVoices(const COPMEmu& synth) {
    int count = 0;
    for (int i = 0; i < synth.getNumVoices(); ++i) {
        if (synth.isVoiceActive(i)) ++count;
    }
    return count;
}

static const int kMs = 48;  // samples per millisecond @48 kHz

// Test 1: without 0xFE the watchdog never fires.
static void test_no_sensing_no_timeout() {
    std::fprintf(stderr, "test 1: no 0xFE -> no timeout...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();

    noteOn(synth, 60);
    pumpFor(synth, 1000 * kMs);              // 1 s of silence on the wire
    EXPECT(!synth.isActiveSensingArmed());
    EXPECT(countActiveVoices(synth) == 1);   // note still ringing
}

// Test 2: 0xFE arms; 300 ms idle cuts all notes and disarms.
static void test_timeout_cuts_notes() {
    std::fprintf(stderr, "test 2: 0xFE + 300 ms idle -> all notes off...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();

    noteOn(synth, 60);
    noteOn(synth, 64);
    pumpFor(synth, 512);
    EXPECT(countActiveVoices(synth) == 2);

    activeSense(synth);
    pumpFor(synth, 200 * kMs);               // < 300 ms: still alive
    EXPECT(synth.isActiveSensingArmed());
    EXPECT(countActiveVoices(synth) == 2);

    pumpFor(synth, 200 * kMs);               // total 400 ms idle
    EXPECT(countActiveVoices(synth) == 0);   // watchdog fired
    EXPECT(!synth.isActiveSensingArmed());   // disarmed until next 0xFE

    // After the timeout, normal playing works again and a long idle
    // does NOT cut notes (watchdog disarmed).
    noteOn(synth, 62);
    pumpFor(synth, 1000 * kMs);
    EXPECT(countActiveVoices(synth) == 1);
}

// Test 3: periodic 0xFE keep-alives prevent the timeout.
static void test_keepalive() {
    std::fprintf(stderr, "test 3: periodic 0xFE keep-alives...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();

    noteOn(synth, 60);
    activeSense(synth);
    for (int i = 0; i < 10; ++i) {           // 10 × 100 ms = 1 s total
        pumpFor(synth, 100 * kMs);
        activeSense(synth);
    }
    pumpFor(synth, 100 * kMs);
    EXPECT(synth.isActiveSensingArmed());
    EXPECT(countActiveVoices(synth) == 1);   // never timed out
}

// Test 4: any MIDI data (not just 0xFE) resets the timer.
static void test_any_data_resets() {
    std::fprintf(stderr, "test 4: any MIDI data resets the timer...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();

    activeSense(synth);
    pumpFor(synth, 250 * kMs);               // 250 ms idle
    noteOn(synth, 60);                       // activity resets the timer
    pumpFor(synth, 250 * kMs);               // 250 ms more (500 ms since FE)
    EXPECT(synth.isActiveSensingArmed());
    EXPECT(countActiveVoices(synth) == 1);   // no timeout

    pumpFor(synth, 200 * kMs);               // now 450 ms truly idle
    EXPECT(countActiveVoices(synth) == 0);   // timeout fired
}

int main() {
    test_no_sensing_no_timeout();
    test_timeout_cuts_notes();
    test_keepalive();
    test_any_data_resets();

    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::fprintf(stderr, "all active-sensing tests passed\n");
    return 0;
}
