// tests/test_memory_protect.cpp
//
// Unit test for Memory-Protect (DX21 Function #23 / dat_F610:35).
//
// When Memory-Protect is ON, the synth must reject every write that
// would touch RAM voices or performance memories:
//
//   1. setRamVoice() ............................... returns false
//   2. importSysex() ............................... returns -1
//   3. Real-time SysEx param change (0x12 pp vv) ... no write
//   4. Slider edit (writeVcedGlobal) .............. no write
//   5. Operator edit (writeVcedOperator) .......... no write
//   6. initVoice() .................................. no write
//   7. saveEditRecall() / loadEditRecall() ......... no write
//
// Read paths (getRamVoice, getPatch, OPM playing) are NOT affected.

#include "opmemu.h"
#include "memory/dx21_memory.h"
#include "io/std_filesystem.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace std;

static int failures = 0;
#define EXPECT(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
        ++failures; \
    } \
} while (0)

// Make a unique-ish patch by stomping on a few fields, so the
// "before/after" comparison can't be fooled by an empty write.
static DX21_Patch makeStampedPatch(int stamp) {
    DX21_Patch p;
    std::memset(&p, 0, sizeof(p));
    p.alg  = (stamp + 0) & 0x07;
    p.fb   = (stamp + 1) & 0x07;
    p.pmd  = (stamp + 2) & 0x7F;
    p.alg  = stamp & 0x07;
    p.op[0].ar  = (stamp + 3) & 0x1F;
    p.op[0].d1r = (stamp + 4) & 0x1F;
    p.op[0].out = (stamp + 5) & 0x7F;
    std::snprintf(p.name, sizeof(p.name), "Stamp%d", stamp);
    return p;
}

// Build a minimal valid 32-voice VCED SysEx bulk dump we can feed
// back into importSysex(). Body is the "vced" of 32 voices * 76 bytes
// each, plus the standard F0 43 0n 09 header, byte count, checksum,
// and F7 terminator. The voices are stamped so the test can verify
// they did NOT land in RAM.
static std::vector<uint8_t> makeBulkSysex(int stamp) {
    std::vector<uint8_t> data;
    data.push_back(0xF0);
    data.push_back(0x43);
    data.push_back(0x00);  // device 0
    data.push_back(0x09);  // 32-voice VCED bulk dump
    // 32 voices * 76 bytes = 2432 bytes
    constexpr uint16_t kCount = 32 * 76;
    data.push_back(static_cast<uint8_t>(kCount >> 7));
    data.push_back(static_cast<uint8_t>(kCount & 0x7F));
    for (int v = 0; v < 32; ++v) {
        DX21_Patch p = makeStampedPatch(stamp * 100 + v);
        uint8_t vced[76] = {0};
        vced[0] = p.alg;
        vced[1] = p.fb;
        vced[4] = p.pmd;
        // Operator 0: AR, D1R, OUT
        vced[10] = p.op[0].ar;
        vced[11] = p.op[0].d1r;
        vced[20] = p.op[0].out;
        for (int i = 0; i < 76; ++i) data.push_back(vced[i]);
    }
    // Checksum
    uint8_t cs = 0;
    for (size_t i = 6; i < data.size(); ++i) cs += data[i];
    cs &= 0x7F;
    data.push_back(cs);
    data.push_back(0xF7);
    return data;
}

// ---- Test 1: setRamVoice rejected ----
static void test_setRamVoice_rejected() {
    std::fprintf(stderr, "test 1: setRamVoice rejected under protect...\n");
    StdFileSystem fs;
    CDX21Memory mem(&fs);

    // Seed slot 5 with a known patch.
    EXPECT(mem.setRamVoice(5, makeStampedPatch(1)));
    const DX21_Patch* before = mem.getRamVoice(5);
    EXPECT(before != nullptr);
    int origAlg = before->alg;
    int origOp0ar = before->op[0].ar;

    // Enable protect, try to overwrite with a different stamp.
    mem.setMemoryProtect(true);
    EXPECT(mem.isMemoryProtected());
    EXPECT(!mem.setRamVoice(5, makeStampedPatch(2)));

    // Verify the patch is unchanged.
    const DX21_Patch* after = mem.getRamVoice(5);
    EXPECT(after != nullptr);
    EXPECT(after->alg == origAlg);
    EXPECT(after->op[0].ar == origOp0ar);
    EXPECT(std::strcmp(after->name, "Stamp1") == 0);
}

// ---- Test 2: importSysex rejected ----
static void test_importSysex_rejected() {
    std::fprintf(stderr, "test 2: importSysex rejected under protect...\n");
    StdFileSystem fs;
    CDX21Memory mem(&fs);

    // Seed slot 0 with a baseline.
    EXPECT(mem.setRamVoice(0, makeStampedPatch(7)));

    mem.setMemoryProtect(true);
    auto syx = makeBulkSysex(99);
    int n = mem.importSysex(syx.data(), syx.size());
    EXPECT(n == -1);

    // The pre-existing patch in slot 0 must still be the baseline.
    const DX21_Patch* p = mem.getRamVoice(0);
    EXPECT(p != nullptr);
    EXPECT(std::strcmp(p->name, "Stamp7") == 0);
    EXPECT(p->alg == (7 & 0x07));
}

// ---- Test 3: read paths still work ----
static void test_reads_still_work() {
    std::fprintf(stderr, "test 3: reads still work under protect...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();
    synth.setMemoryProtect(true);
    EXPECT(synth.isMemoryProtected());

    // getRamVoice() should still return the seeded RAM patches
    // (initRamFromRom runs in Initialize()).
    const DX21_Patch* p = synth.memory().getRamVoice(0);
    EXPECT(p != nullptr);
    EXPECT(p->name[0] != '\0');
}

// ---- Test 4: real-time SysEx param change rejected ----
static void test_realtime_param_rejected() {
    std::fprintf(stderr, "test 4: real-time param change rejected under protect...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();
    synth.setSysexEditVoice(0);
    synth.setMemoryProtect(true);

    // Capture RAM[0] before.
    const DX21_Patch* before = synth.memory().getRamVoice(0);
    EXPECT(before != nullptr);
    int origAlg = before->alg;
    int origFb  = before->fb;

    // Send real-time param changes: alg=7, fb=7.
    // F0 43 0n 12 pp vv F7  (n=0, pp=0/1, vv=7)
    uint8_t msg1[7] = { 0xF0, 0x43, 0x00, 0x12, 0x00, 0x07, 0xF7 };
    synth.processMidi(msg1, 7);
    uint8_t msg2[7] = { 0xF0, 0x43, 0x00, 0x12, 0x01, 0x07, 0xF7 };
    synth.processMidi(msg2, 7);

    // Pump enough to let the audio thread consume the queue.
    float l[256], r[256];
    for (int i = 0; i < 4; ++i) synth.processBlock(l, r, 256);

    // RAM[0] must be unchanged.
    const DX21_Patch* after = synth.memory().getRamVoice(0);
    EXPECT(after != nullptr);
    EXPECT(after->alg == origAlg);
    EXPECT(after->fb  == origFb);
}

// ---- Test 5: writeVcedGlobal rejected ----
static void test_writeVcedGlobal_rejected() {
    std::fprintf(stderr, "test 5: writeVcedGlobal rejected under protect...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();
    synth.setSysexEditVoice(0);
    synth.setMemoryProtect(true);

    const DX21_Patch* before = synth.memory().getRamVoice(0);
    EXPECT(before != nullptr);
    int origPmd = before->pmd;

    // Direct call (simulates the slider/UI path).
    COPMEmu::TestDoor::writeVcedGlobal(synth, 4, 99);  // PMD index 4
    COPMEmu::TestDoor::writeVcedGlobal(synth, 0, 7);   // algorithm

    const DX21_Patch* after = synth.memory().getRamVoice(0);
    EXPECT(after->pmd == origPmd);
    EXPECT(after->alg == before->alg);
}

// ---- Test 6: writeVcedOperator rejected ----
static void test_writeVcedOperator_rejected() {
    std::fprintf(stderr, "test 6: writeVcedOperator rejected under protect...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();
    synth.setSysexEditVoice(0);
    synth.setMemoryProtect(true);

    const DX21_Patch* before = synth.memory().getRamVoice(0);
    EXPECT(before != nullptr);
    int origAr = before->op[0].ar;

    COPMEmu::TestDoor::writeVcedOperator(synth, 0, 0, 31);  // OP0 AR = 31
    COPMEmu::TestDoor::writeVcedOperator(synth, 0, 11, 63); // OP0 CRS = 63

    const DX21_Patch* after = synth.memory().getRamVoice(0);
    EXPECT(after->op[0].ar  == origAr);
    EXPECT(after->op[0].crs == before->op[0].crs);
}

// ---- Test 7: initVoice rejected ----
static void test_initVoice_rejected() {
    std::fprintf(stderr, "test 7: initVoice rejected under protect...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();
    synth.setSysexEditVoice(0);
    synth.setMemoryProtect(true);

    const DX21_Patch* before = synth.memory().getRamVoice(0);
    EXPECT(before != nullptr);
    // Stomp the patch with a recognisable fingerprint.
    DX21_Patch* ram = synth.memory().getRamVoice(0);
    ram->alg = 3;
    ram->op[0].ar = 7;
    std::snprintf(ram->name, sizeof(ram->name), "BEFORE");

    // initVoice() must NOT overwrite when protected.
    synth.initVoice();

    const DX21_Patch* after = synth.memory().getRamVoice(0);
    EXPECT(after->alg == 3);
    EXPECT(after->op[0].ar == 7);
    EXPECT(std::strcmp(after->name, "BEFORE") == 0);
}

// ---- Test 8: unprotect restores write ability ----
static void test_unprotect_restores_writes() {
    std::fprintf(stderr, "test 8: unprotect restores write ability...\n");
    StdFileSystem fs;
    COPMEmu synth(&fs);
    synth.Initialize();
    synth.setSysexEditVoice(0);

    // Protect, try to write, fails.
    synth.setMemoryProtect(true);
    COPMEmu::TestDoor::writeVcedGlobal(synth, 0, 5);  // alg=5
    {
        const DX21_Patch* p = synth.memory().getRamVoice(0);
        EXPECT(p->alg != 5);
    }

    // Unprotect, write succeeds.
    synth.setMemoryProtect(false);
    COPMEmu::TestDoor::writeVcedGlobal(synth, 0, 5);
    {
        const DX21_Patch* p = synth.memory().getRamVoice(0);
        EXPECT(p->alg == 5);
    }
}

int main() {
    test_setRamVoice_rejected();
    test_importSysex_rejected();
    test_reads_still_work();
    test_realtime_param_rejected();
    test_writeVcedGlobal_rejected();
    test_writeVcedOperator_rejected();
    test_initVoice_rejected();
    test_unprotect_restores_writes();

    if (failures == 0) {
        std::fprintf(stderr, "ALL TESTS PASSED\n");
        return 0;
    } else {
        std::fprintf(stderr, "%d TEST(S) FAILED\n", failures);
        return 1;
    }
}
