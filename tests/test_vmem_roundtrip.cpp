// tests/test_vmem_roundtrip.cpp
//
// VMEM bit-packing verification against REAL DX21 .syx banks
// (doc/SYX/dx21_a..d.syx, dumped from original hardware).
//
// For every bank file the test checks:
//   1. Framing: F0 43 0n 04 20 00 <4096 bytes> <checksum> F7,
//      two's-complement checksum valid.
//   2. importSysex() decodes all 32 voices.
//   3. Voice names decode to printable ASCII (catches wrong byte
//      offsets in the name region 57..66).
//   4. Roundtrip: exportSysex() re-produces the original bank
//      BYTE-EXACTLY in the 73 meaningful VMEM bytes per voice.
//      Any diff means the pack/unpack loses or moves bits.
//   5. Idempotence: import(export) → export is byte-identical.
//
// Usage: test_vmem_roundtrip [dir]   (default: doc/SYX)
//
#include "memory/dx21_memory.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::fprintf(stderr, "FAIL: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
    ++failures; } } while (0)

static bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize((size_t)n);
    size_t rd = std::fread(out.data(), 1, (size_t)n, f);
    std::fclose(f);
    return rd == (size_t)n;
}

static uint8_t yamahaChecksum(const uint8_t* data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) sum += data[i];
    return (uint8_t)((0x80 - (sum & 0x7F)) & 0x7F);
}

// Compare two complete bulk dumps. Reports diffs in the meaningful
// VMEM region (bytes 0..72 of each 128-byte voice record) as
// failures; diffs in the zero-padding 73..127 are warnings only
// (not produced by our packer, may carry junk in the original).
static void compareBulks(const char* tag,
                         const std::vector<uint8_t>& orig,
                         const std::vector<uint8_t>& ours) {
    EXPECT(ours.size() == orig.size());
    if (ours.size() != orig.size()) return;

    // Header (skip byte 2 = device/channel) + trailing checksum/F7.
    EXPECT(orig[0] == ours[0] && orig[1] == ours[1]);
    EXPECT(orig[3] == ours[3] && orig[4] == ours[4] && orig[5] == ours[5]);
    EXPECT(ours[orig.size() - 1] == 0xF7);

    int diffMeaning = 0, diffPad = 0;
    for (int v = 0; v < 32; ++v) {
        const uint8_t* o = orig.data() + 6 + v * DX21_VMEM_PADDED;
        const uint8_t* u = ours.data() + 6 + v * DX21_VMEM_PADDED;
        for (uint32_t i = 0; i < DX21_VMEM_PADDED; ++i) {
            if (o[i] == u[i]) continue;
            if (i < DX21_VMEM_SIZE) {
                ++diffMeaning;
                if (diffMeaning <= 20) {
                    std::fprintf(stderr,
                        "%s: DIFF voice %2d vmem[%2u]: orig %02X ours %02X\n",
                        tag, v + 1, i, o[i], u[i]);
                }
            } else {
                ++diffPad;
            }
        }
    }
    std::fprintf(stderr, "%s: %d diff(s) meaningful, %d diff(s) padding\n",
                 tag, diffMeaning, diffPad);
    // Both must be byte-exact: real hardware dumps carry a constant
    // 0x01 at padding offset 91 that exportSysex() mirrors.
    EXPECT(diffMeaning == 0);
    EXPECT(diffPad == 0);
}

// Returns false when the bank file is absent (skip — the factory
// banks are deliberately NOT committed, like the firmware ROM).
static bool testBank(const std::string& path) {
    std::vector<uint8_t> raw;
    if (!readFile(path, raw)) {
        std::fprintf(stderr, "skip: %s not present (local-only data)\n",
                     path.c_str());
        return false;
    }
    std::fprintf(stderr, "── bank %s ──\n", path.c_str());

    // 1. Framing of the real bank
    EXPECT(raw.size() == (size_t)(6 + DX21_VMEM_BULK_SIZE + 2));
    EXPECT(raw[0] == 0xF0 && raw[1] == 0x43);
    EXPECT(raw[3] == 0x04);   // VMEM format
    EXPECT(((raw[4] << 7) | raw[5]) == (int)DX21_VMEM_BULK_SIZE);
    EXPECT(raw.back() == 0xF7);
    EXPECT(yamahaChecksum(raw.data() + 6, DX21_VMEM_BULK_SIZE)
           == raw[6 + DX21_VMEM_BULK_SIZE]);

    // 2. Import
    CDX21Memory mem(nullptr);
    int n = mem.importSysex(raw.data(), raw.size());
    EXPECT(n == 32);
    if (n != 32) return true;

    // 3. Names must be printable ASCII (wrong offsets → garbage)
    for (int v = 0; v < 32; ++v) {
        const DX21_Patch* p = mem.getRamVoice(v);
        EXPECT(p != nullptr);
        if (!p) continue;
        bool printable = true;
        for (int i = 0; p->name[i] && i < 10; ++i) {
            if (p->name[i] < 0x20 || p->name[i] > 0x7E) printable = false;
        }
        EXPECT(printable);
        std::fprintf(stderr, "  voice %2d: \"%s\"\n", v + 1, p->name);
    }

    // 4. Roundtrip against the original bytes
    std::vector<uint8_t> out;
    EXPECT(mem.exportSysex(out));
    compareBulks("roundtrip", raw, out);

    // 5. Idempotence: import our own export, export again
    CDX21Memory mem2(nullptr);
    EXPECT(mem2.importSysex(out.data(), out.size()) == 32);
    std::vector<uint8_t> out2;
    EXPECT(mem2.exportSysex(out2));
    EXPECT(out2 == out);
    return true;
}

int main(int argc, char** argv) {
    std::string dir = (argc > 1) ? argv[1] : "doc/SYX";
    const char* banks[] = { "dx21_a.syx", "dx21_b.syx",
                            "dx21_c.syx", "dx21_d.syx" };
    int tested = 0;
    for (const char* b : banks) {
        if (testBank(dir + "/" + b)) ++tested;
    }
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    if (tested == 0) {
        std::fprintf(stderr,
            "no banks found in %s — test SKIPPED (place the factory\n"
            ".syx banks there to run the hardware roundtrip check)\n",
            dir.c_str());
        return 0;
    }
    std::fprintf(stderr, "all VMEM roundtrip tests passed (%d banks)\n",
                 tested);
    return 0;
}
