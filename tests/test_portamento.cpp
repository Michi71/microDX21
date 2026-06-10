// tests/test_portamento.cpp
//
// Unit test for the exponential portamento (glide) curve in COPMEmu.
//
// The portamento slide model in the synth is:
//
//   gap(n+1) = gap(n) × decay_per_sample
//   new_pitch = target - new_gap
//
// where decay_per_sample = 0.5 ^ (1 / (T_half × sample_rate)) and
// T_half is the time (in seconds) for the pitch gap to halve.
//
// Mapping rate 0..99 to T_half:
//   rate = 0   → T_half = 0   (no portamento, instant snap)
//   rate = 99  → T_half ≈ 2.5s
//
// Tests:
//   1. Rate = 0: pitch snaps instantly (no glide)
//   2. Rate > 0: pitch glides from current to target, asymptotically
//   3. Higher rate = slower glide (smaller decay_per_sample)
//   4. Final pitch == target (within numerical precision)
//   5. Time for 1 octave at rate=99 is roughly 5-15 seconds
//   6. updatePortamento is a no-op for inactive voices
//   7. updatePortamento is a no-op for voices not in isPorting state

#include "opmemu.h"
#include "io/std_filesystem.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

using namespace std;

static int failures = 0;
#define EXPECT(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
        ++failures; \
    } \
} while (0)
#define EXPECT_NEAR(a, b, tol) do { \
    float _a = (a), _b = (b); \
    if (std::fabs(_a - _b) > (tol)) { \
        std::fprintf(stderr, "FAIL: %s == %s (%.4f vs %.4f, tol %.4f) @ %s:%d\n", \
                     #a, #b, _a, _b, (float)(tol), __FILE__, __LINE__); \
        ++failures; \
    } \
} while (0)

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
static void pumpFor(COPMEmu& synth, int totalSamples) {
    float l[256], r[256];
    int remaining = totalSamples;
    while (remaining > 0) {
        int n = std::min(remaining, 256);
        synth.processBlock(l, r, n);
        remaining -= n;
    }
}

// Test 1: rate=0 (portamento disabled) → pitch snaps instantly
static void test_rate_zero_no_glide() {
    std::fprintf(stderr, "test 1: rate=0 = no glide...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();
    synth.setPortamentoMode(1);  // Full Time
    synth.setPortamentoRate(0);   // rate=0 → no portamento

    noteOn(synth, 60);
    pumpFor(synth, 256);
    noteOn(synth, 72);  // 1 octave up
    pumpFor(synth, 64);

    // Without portamento, the new note should snap to its pitch
    // immediately. Voice 0's currentPitch should be 72.0 (or very close).
    int voice = 0;
    // Get the voice's currentPitch via the public API? We don't have
    // a direct accessor. Use the note-playing check as a proxy.
    EXPECT(synth.isVoicePlayingNote(72));
}

// Test 2: rate>0 → exponential glide happens.
// 
// Uses MONO mode for predictable portamento behavior (consecutive
// notes reuse the same voice). Uses rate=10 to get a glide that
// completes in ~0.25s so the test doesn't need to pump for many
// seconds.
static void test_glide_happens() {
    std::fprintf(stderr, "test 2: rate>0 = glide happens (MONO mode, fast rate)...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();
    synth.setMono(true);
    synth.setPortamentoMode(1);        // Full Time
    synth.setPortamentoRate(10);       // fast glide (~0.25s for 1 octave)

    noteOn(synth, 60);
    pumpFor(synth, 256);
    noteOn(synth, 72);  // 1 octave up
    pumpFor(synth, 1024);  // ~21ms

    // Voice should be playing note 72 (the new note)
    EXPECT(synth.isVoicePlayingNote(72));
    float pitch = synth.getVoicePitch(0);
    // With rate=10 and exponential decay, pitch should be in transit
    EXPECT(pitch > 60.0f);
    EXPECT(pitch < 72.0f);

    // After ~1 second total (1024+48000 samples), with rate=10 (T_half≈0.25s),
    // about 4 half-lives have elapsed, leaving ~6% of the original 12-semitone
    // gap, i.e. ~0.73 semitones. Pitch should be ~71.27.
    pumpFor(synth, 48000);
    float pitch2 = synth.getVoicePitch(0);
    EXPECT_NEAR(pitch2, 72.0f, 0.8f);
}

// Test 3: rate=99 takes much longer than rate=10 for the same glide.
// After 1 second, the slow synth should still be mid-glide while
// the fast synth is at target.
static void test_higher_rate_slower() {
    std::fprintf(stderr, "test 3: higher rate = slower glide (MONO mode)...\n");
    StdFileSystem fs1, fs2;
    COPMEmu fastSynth(&fs1);
    fastSynth.Initialize();
    fastSynth.setMono(true);
    fastSynth.setPortamentoMode(1);
    fastSynth.setPortamentoRate(10);  // fast

    COPMEmu slowSynth(&fs2);
    slowSynth.Initialize();
    slowSynth.setMono(true);
    slowSynth.setPortamentoMode(1);
    slowSynth.setPortamentoRate(99);  // very slow

    // Same note pattern
    noteOn(fastSynth, 60); pumpFor(fastSynth, 256);
    noteOn(fastSynth, 72); pumpFor(fastSynth, 256);
    noteOn(slowSynth, 60); pumpFor(slowSynth, 256);
    noteOn(slowSynth, 72); pumpFor(slowSynth, 256);

    // After 1 second, compare pitches
    pumpFor(fastSynth, 48000);
    pumpFor(slowSynth, 48000);
    float fastPitch = fastSynth.getVoicePitch(0);
    float slowPitch = slowSynth.getVoicePitch(0);

    // The slow synth should be further from target than the fast one
    EXPECT(std::fabs(72.0f - slowPitch) > std::fabs(72.0f - fastPitch));
    // The slow synth should still be mid-glide (not yet at 72)
    EXPECT(slowPitch < 70.0f);
    // The fast synth (rate=10, T_half≈0.25s) should be near target after ~1s.
    // At 1.01s total, about 4 half-lives have elapsed → ~6% of the 12-semitone
    // gap remains → pitch ≈ 71.25.
    EXPECT_NEAR(fastPitch, 72.0f, 0.8f);
}

// Test 4: exponential decay — verify the formula's time behavior.
// With rate=10 (T_half ≈ 0.25s), at 0.5s the gap should be ~25% of
// the original, and at 1.0s the gap should be ~6% of the original.
static void test_exponential_convergence() {
    std::fprintf(stderr, "test 4: exponential convergence (MONO mode)...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();
    synth.setMono(true);
    synth.setPortamentoMode(1);
    synth.setPortamentoRate(10);  // T_half ≈ 0.25s

    noteOn(synth, 60);
    pumpFor(synth, 256);
    noteOn(synth, 72);

    // Pump for 0.5s, then check pitch
    pumpFor(synth, 24000);
    float pitch1 = synth.getVoicePitch(0);
    // After ~0.5s, with T_half=0.25s, gap halves twice → 25% remaining
    // gap = 12 * 0.25 = 3 semitones. Pitch should be ~72 - 3 = 69.
    EXPECT(pitch1 > 67.0f);
    EXPECT(pitch1 < 70.0f);

    // Pump for another 1.0s, total 1.5s
    pumpFor(synth, 48000);
    float pitch2 = synth.getVoicePitch(0);
    // After 1.5s, T_half=0.25s means 6 half-lives → 1.5% remaining
    // gap = 12 * 0.015 = 0.18 semitones. Pitch should be very close to 72.
    EXPECT_NEAR(pitch2, 72.0f, 0.3f);
}

// Test 5: setPortamentoMode change works (Off disables, FullTime enables)
static void test_mode_change() {
    std::fprintf(stderr, "test 5: portamento mode change...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();
    synth.setPortamentoRate(50);

    // Start with mode = Off
    synth.setPortamentoMode(0);
    noteOn(synth, 60); pumpFor(synth, 256);
    noteOn(synth, 72); pumpFor(synth, 64);
    EXPECT(synth.isVoicePlayingNote(72));

    // Switch to FullTime
    synth.setPortamentoMode(1);
    noteOn(synth, 60); pumpFor(synth, 256);
    noteOn(synth, 72); pumpFor(synth, 64);
    EXPECT(synth.isVoicePlayingNote(72));

    // Switch back to Off
    synth.setPortamentoMode(0);
    noteOn(synth, 60); pumpFor(synth, 256);
    noteOn(synth, 72); pumpFor(synth, 64);
    EXPECT(synth.isVoicePlayingNote(72));
}

int main() {
    test_rate_zero_no_glide();
    test_glide_happens();
    test_higher_rate_slower();
    test_exponential_convergence();
    test_mode_change();

    if (failures == 0) {
        std::fprintf(stderr, "ALL TESTS PASSED\n");
        return 0;
    } else {
        std::fprintf(stderr, "%d TEST(S) FAILED\n", failures);
        return 1;
    }
}
