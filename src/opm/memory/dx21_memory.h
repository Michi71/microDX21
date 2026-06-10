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
static constexpr uint32_t DX21_SYSEX_VCED_SIZE = 76;      // one voice record
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

    // --- SysEx Import / Export (DX21 compatible) ---
    // Import a DX21 32-voice VCED bulk dump (SysEx data, including F0..F7).
    // Returns number of voices successfully imported, or -1 on parse error.
    int importSysex(const uint8_t* data, size_t len);

    // Export all 32 RAM voices as a DX21-compatible 32-voice VCED bulk dump.
    // Output includes F0..F7 framing. Returns true on success.
    bool exportSysex(std::vector<uint8_t>& out) const;

    // Export a single voice as a 1-voice VCED dump
    // (F0 43 0n 03 <count> <76-byte VCED> <checksum> F7).
    // The patch does not have to live in this memory — the caller can
    // pass the edit buffer or a ROM patch. Returns true on success.
    bool exportVoiceSysex(const DX21_Patch& patch, std::vector<uint8_t>& out) const;

    // Import a 1-voice VCED dump (modelId 0x03) into RAM slot `slot`
    // (0..31). Validates framing, byte count and checksum. Honours
    // Memory Protect. Returns true if the voice was stored.
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

    // SysEx VCED encode/decode
    bool vcedToPatch(const uint8_t* vced76, DX21_Patch& outPatch) const;
    bool patchToVced(const DX21_Patch& patch, uint8_t* vced76) const;
};
