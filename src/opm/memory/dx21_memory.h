#pragma once

#include "../patches.h"
#include "../io/ifilesystem.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// ===========================================================================
// DX21 Performance Memory
// Stores one complete performance configuration (play mode, voices, effects).
// Matches the DX21's 32 internal performance memories.
// ===========================================================================
struct DX21_Performance {
    char     name[16];
    uint8_t  playMode;      // 0=Single, 1=Dual, 2=Split
    uint8_t  voiceA;        // Patch index (0-127)
    uint8_t  voiceB;        // Patch index (0-127)
    uint8_t  splitPoint;    // MIDI note (0-127, default 60=C3)
    uint8_t  balance;     // 0-99 (50=center)
    uint8_t  pbRange;       // 0-12 semitones
    uint8_t  pbMode;        // 0=All, 1=Low, 2=High, 3=K-on
    uint8_t  portaMode;     // 0=Off, 1=FullTime, 2=Fingered
    uint8_t  portaRate;     // 0-99
    uint8_t  modSensitivity;// 0-99 (mod wheel depth scaling)
    uint8_t  breathPitch;   // 0-99
    uint8_t  breathAmp;     // 0-99
    uint8_t  breathEGBias;  // 0-99
    uint8_t  chorus;        // 0/1
    int8_t   transpose;     // -24 to +24 semitones
    int8_t   keyShift;      // -24 to +24 semitones

    void initDefaults();
    void setName(const char* n);
};

// ===========================================================================
// DX21 SysEx Constants
// ===========================================================================
// Real DX21 formats (owner's manual, MIDI DATA FORMAT tables 5-1/5-2):
//   1-voice VCED:  format 0x03, 93 data bytes
//   32-voice VMEM: format 0x04, 73 packed bytes per voice, zero-padded
//                  to 128 per voice → 4096 data bytes
// The checksum is the lowest 7 bits of the two's complement of the
// data-byte sum (sum + checksum ≡ 0 mod 128).
static constexpr uint32_t DX21_VCED93_SIZE    = 93;
static constexpr uint32_t DX21_VMEM_SIZE      = 73;
static constexpr uint32_t DX21_VMEM_PADDED    = 128;
static constexpr uint32_t DX21_VMEM_BULK_SIZE = 32 * DX21_VMEM_PADDED; // 4096

// Legacy microDX21-internal formats (pre-hardware-compat, kept for
// RECEIVE only so old dumps remain loadable): 76-byte VCED records,
// format byte 0x09 for the 32-voice bulk, simple-sum checksum.
static constexpr uint32_t DX21_SYSEX_VCED_SIZE = 76;      // legacy one-voice record
static constexpr uint32_t DX21_SYSEX_BULK_SIZE = 32 * DX21_SYSEX_VCED_SIZE; // 2432 bytes
static constexpr uint8_t  DX21_SYSEX_HEADER[] = {0xF0, 0x43};

// ===========================================================================
// CDX21Memory — Manages 32 RAM voice slots and 32 performance memories.
// Uses IFileSystem for persistence (JSON banks + DX21 SysEx import/export).
// ===========================================================================
class CDX21Memory {
public:
    static constexpr int kNumRamVoices = 32;
    static constexpr int kNumPerformances = 32;

    // Construct with an optional IFileSystem for load/save.
    // If fs is nullptr, memory is volatile (no persistence).
    explicit CDX21Memory(IFileSystem* fs = nullptr);

    // --- Memory Protect (Function #23) ---
    // When ON, setRamVoice() and importSysex() reject all writes
    // (returns false / -1). Read paths (getRamVoice) are unaffected.
    // The real DX21 hardware also gates these paths.
    void setMemoryProtect(bool on) { m_memoryProt = on; }
    bool isMemoryProtected() const  { return m_memoryProt; }

    // --- RAM Voice Management ---
    bool setRamVoice(int slot, const DX21_Patch& patch);
    const DX21_Patch* getRamVoice(int slot) const;
    DX21_Patch* getRamVoice(int slot);
    void clearRamVoice(int slot);
    void clearAllRamVoices();

    // --- Performance Management ---
    bool setPerformance(int slot, const DX21_Performance& perf);
    const DX21_Performance* getPerformance(int slot) const;
    DX21_Performance* getPerformance(int slot);
    void clearPerformance(int slot);
    void clearAllPerformances();

    // --- Persistence (JSON) ---
    // Save/load the entire RAM bank (32 voices) to/from a directory.
    // Each voice is stored as a separate .json file.
    bool saveRamBank(const std::string& dirPath);
    bool loadRamBank(const std::string& dirPath);

    // Save/load all 32 performances as a single JSON file.
    bool savePerformanceBank(const std::string& filePath);
    bool loadPerformanceBank(const std::string& filePath);

    // --- SysEx Import / Export (hardware-DX21 compatible) ---
    // Import a 32-voice bulk dump (SysEx data, including F0..F7).
    // Accepts the real DX21 VMEM format (0x04, 4096 bytes, two's-
    // complement checksum) and, receive-only, the legacy microDX21
    // format (0x09, 2432 bytes, simple-sum checksum). Returns number
    // of voices successfully imported, or -1 on parse error.
    int importSysex(const uint8_t* data, size_t len);

    // Export all 32 RAM voices as a real DX21 32-voice VMEM bulk dump
    // (F0 43 0n 04 20 00 <4096 bytes> <checksum> F7). Compatible with
    // DX21/DX27/DX100 hardware, editors and the .syx banks in the
    // wild. Returns true on success.
    bool exportSysex(std::vector<uint8_t>& out) const;

    // Export a single voice as a real DX21 1-voice VCED dump
    // (F0 43 0n 03 00 5D <93-byte VCED> <checksum> F7).
    // The patch does not have to live in this memory — the caller can
    // pass the edit buffer or a ROM patch. Returns true on success.
    bool exportVoiceSysex(const DX21_Patch& patch, std::vector<uint8_t>& out) const;

    // Import a 1-voice VCED dump (format 0x03) into RAM slot `slot`
    // (0..31). Accepts the real 93-byte VCED and, receive-only, the
    // legacy 76-byte record (dispatch by byte count). Validates
    // framing, byte count and checksum. Honours Memory Protect.
    // Returns true if the voice was stored.
    bool importVoiceSysex(const uint8_t* data, size_t len, int slot);

    // --- Init from ROM patches ---
    // Copy the first 32 ROM patches into RAM for factory defaults.
    void initRamFromRom(const DX21_Patch rom[], int count);

private:
    IFileSystem* m_fs;
    bool         m_memoryProt;  // FUNCTION #23
    DX21_Patch   m_ram[kNumRamVoices];
    bool         m_ramValid[kNumRamVoices];
    DX21_Performance m_perf[kNumPerformances];
    bool         m_perfValid[kNumPerformances];

    // JSON serialization helpers
    std::string patchToJson(int slot, const DX21_Patch& patch) const;
    bool jsonToPatch(const std::string& json, DX21_Patch& outPatch) const;
    std::string perfToJson(int slot, const DX21_Performance& perf) const;
    bool jsonToPerf(const std::string& json, DX21_Performance& outPerf) const;

    // SysEx VCED encode/decode (legacy 76-byte project format)
    bool vcedToPatch(const uint8_t* vced76, DX21_Patch& outPatch) const;
    bool patchToVced(const DX21_Patch& patch, uint8_t* vced76) const;

    // Real DX21 formats (manual tables 5-1/5-2). Operator block order
    // on the wire is OP4, OP2, OP3, OP1.
    bool vced93ToPatch(const uint8_t* vced93, DX21_Patch& outPatch) const;
    bool patchToVced93(const DX21_Patch& patch, uint8_t* vced93) const;
    bool vmemToPatch(const uint8_t* vmem73, DX21_Patch& outPatch) const;
    bool patchToVmem(const DX21_Patch& patch, uint8_t* vmem73) const;
};
