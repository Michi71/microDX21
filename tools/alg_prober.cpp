// tools/alg_prober.cpp — per-CON algorithm topology prober
#include "opm.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <filesystem>

static void clock_n(opm_t* chip, int n) {
    int32_t dac[2];
    uint8_t sh1, sh2, so;
    for (int i = 0; i < n; i++) OPM_Clock(chip, dac, &sh1, &sh2, &so);
}

// Write a register to the OPM with the proper timing, including the
// OPP-specific 0x20 (RL/FB/CON) 4-cycle shift register delay.
static void write_reg(opm_t* chip, uint8_t reg, uint8_t data) {
    // Address phase
    OPM_Write(chip, 0, reg);
    clock_n(chip, 2);
    // Data phase
    OPM_Write(chip, 1, data);
    clock_n(chip, 2);
    // Application phase
    clock_n(chip, 32);
    // OPP-specific: 0x20 has 4-cycle shift delay
    if ((reg & 0xf8) == 0x20) {
        OPM_Write(chip, 0, 0x01);  // dummy address to clear reg_data_ready
        clock_n(chip, 2);
        clock_n(chip, 12);         // wait for pending 0x20 delays
    }
}

// Set up one OPM channel with all 4 operators carrying identical parameters.
// Only the CON value (algorithm topology) differs across runs.
static void setup_test_patch(opm_t* chip, int channel, int con) {
    const int kOpOffsets[4] = { 0, 8, 16, 24 };
    for (int op = 0; op < 4; op++) {
        int slot = channel + kOpOffsets[op];
        write_reg(chip, 0x40 + slot, 0x01);  // DT1=0, MUL=1
        write_reg(chip, 0x60 + slot, 0x3F);  // TL=63 (moderate)
        write_reg(chip, 0x80 + slot, 0x1F);  // KS=0, AR=31
        write_reg(chip, 0xA0 + slot, 0x00);  // AME=0, D1R=0
        write_reg(chip, 0xC0 + slot, 0x00);  // DT2=0, D2R=0
        write_reg(chip, 0xE0 + slot, 0x00);  // D1L=0, RR=0 (sustain)
    }
    int rl_fb_con = (3 << 6) | (0 << 3) | (con & 0x07);
    write_reg(chip, 0x20 + channel, rl_fb_con);
    write_reg(chip, 0x28 + channel, 51 << 2);  // KC for ~C3
    write_reg(chip, 0x30 + channel, 0);        // KF=0
    write_reg(chip, 0x38 + channel, 0);        // PMS=0, AMS=0
    write_reg(chip, 0x18, 0);                   // LFO off
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <out-dir>\n", argv[0]); return 2; }
    std::filesystem::path outdir = argv[1];
    std::filesystem::create_directories(outdir);

    constexpr int kNumSamples = 4800;  // 100 ms @ 48 kHz
    constexpr int kCyclesPerSample = 37;  // 3579545 / (2 * 48000)
    uint8_t sh1, sh2, so;
    int32_t dac[2];

    for (int con = 0; con < 8; con++) {
        opm_t chip;
        OPM_Reset(&chip);

        setup_test_patch(&chip, 0, con);
        write_reg(&chip, 0x08, 0x78);  // KeyOn all 4 ops

        // Prime
        for (int i = 0; i < 2000; i++) OPM_Clock(&chip, dac, &sh1, &sh2, &so);

        std::vector<int16_t> samples;
        samples.reserve(kNumSamples);
        int max_peak = 0;
        for (int i = 0; i < kNumSamples; i++) {
            for (int j = 0; j < kCyclesPerSample; j++) {
                OPM_Clock(&chip, dac, &sh1, &sh2, &so);
            }
            int32_t mixed = (dac[0] + dac[1]) / 2;
            if (mixed >  32767) mixed =  32767;
            if (mixed < -32768) mixed = -32768;
            samples.push_back((int16_t)mixed);
            if (abs(mixed) > max_peak) max_peak = abs(mixed);
        }

        char path[256];
        snprintf(path, sizeof(path), "%s/alg_probe_con_%d.raw", outdir.c_str(), con);
        FILE* f = fopen(path, "wb");
        fwrite(samples.data(), sizeof(int16_t), samples.size(), f);
        fclose(f);
        printf("CON=%d  max_peak=%d  -> %s\n", con, max_peak, path);
    }

    return 0;
}
