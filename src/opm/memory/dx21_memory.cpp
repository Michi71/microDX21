#include "dx21_memory.h"
#include <cstdio>
#include <cstdlib>

namespace {
// Return the parent directory portion of a forward-slash-separated path,
// or an empty string if there is no parent (e.g. "voice.json" or "").
//
// Examples:
//   "MICRODX21/BANK_01/voice_00.json" -> "MICRODX21/BANK_01"
//   "MICRODX21/performances.json"      -> "MICRODX21"
//   "voice.json"                       -> ""
//   ""                                 -> ""
std::string parentDir(const std::string& path) {
    auto slash = path.find_last_of('/');
    if (slash == std::string::npos) return "";
    return path.substr(0, slash);
}
} // namespace

// ===========================================================================
// DX21_Performance
// ===========================================================================
void DX21_Performance::initDefaults() {
    std::memset(this, 0, sizeof(*this));
    std::strncpy(name, "Init Perf", sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    playMode = 0;      // Single
    voiceA = 0;
    voiceB = 0;
    splitPoint = 60;   // C3
    balance = 50;
    pbRange = 2;
    pbMode = 0;        // All
    portaMode = 0;     // Off
    portaRate = 0;
    modSensitivity = 0;
    breathPitch = 0;
    breathAmp = 0;
    breathEGBias = 0;
    chorus = 0;
    transpose = 0;
    keyShift = 0;
}

void DX21_Performance::setName(const char* n) {
    std::strncpy(name, n, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
}

// ===========================================================================
// CDX21Memory — Constructor
// ===========================================================================
CDX21Memory::CDX21Memory(IFileSystem* fs)
    : m_fs(fs)
    , m_memoryProt(false)
{
    for (int i = 0; i < kNumRamVoices; ++i) {
        m_ramValid[i] = false;
        std::memset(&m_ram[i], 0, sizeof(DX21_Patch));
    }
    for (int i = 0; i < kNumPerformances; ++i) {
        m_perfValid[i] = false;
        m_perf[i].initDefaults();
    }
}

// ===========================================================================
// RAM Voice Management
// ===========================================================================
bool CDX21Memory::setRamVoice(int slot, const DX21_Patch& patch) {
    if (slot < 0 || slot >= kNumRamVoices) return false;
    if (m_memoryProt) return false;  // FUNCTION #23: write rejected
    m_ram[slot] = patch;
    m_ramValid[slot] = true;
    return true;
}

const DX21_Patch* CDX21Memory::getRamVoice(int slot) const {
    if (slot < 0 || slot >= kNumRamVoices) return nullptr;
    return m_ramValid[slot] ? &m_ram[slot] : nullptr;
}

DX21_Patch* CDX21Memory::getRamVoice(int slot) {
    if (slot < 0 || slot >= kNumRamVoices) return nullptr;
    return m_ramValid[slot] ? &m_ram[slot] : nullptr;
}

void CDX21Memory::clearRamVoice(int slot) {
    if (slot >= 0 && slot < kNumRamVoices) m_ramValid[slot] = false;
}

void CDX21Memory::clearAllRamVoices() {
    for (int i = 0; i < kNumRamVoices; ++i) m_ramValid[i] = false;
}

// ===========================================================================
// Performance Management
// ===========================================================================
bool CDX21Memory::setPerformance(int slot, const DX21_Performance& perf) {
    if (slot < 0 || slot >= kNumPerformances) return false;
    m_perf[slot] = perf;
    m_perfValid[slot] = true;
    return true;
}

const DX21_Performance* CDX21Memory::getPerformance(int slot) const {
    if (slot < 0 || slot >= kNumPerformances) return nullptr;
    return m_perfValid[slot] ? &m_perf[slot] : nullptr;
}

DX21_Performance* CDX21Memory::getPerformance(int slot) {
    if (slot < 0 || slot >= kNumPerformances) return nullptr;
    return m_perfValid[slot] ? &m_perf[slot] : nullptr;
}

void CDX21Memory::clearPerformance(int slot) {
    if (slot >= 0 && slot < kNumPerformances) {
        m_perfValid[slot] = false;
        m_perf[slot].initDefaults();
    }
}

void CDX21Memory::clearAllPerformances() {
    for (int i = 0; i < kNumPerformances; ++i) {
        m_perfValid[i] = false;
        m_perf[i].initDefaults();
    }
}

// ===========================================================================
// Init from ROM
// ===========================================================================
void CDX21Memory::initRamFromRom(const DX21_Patch rom[], int count) {
    int n = (count < kNumRamVoices) ? count : kNumRamVoices;
    for (int i = 0; i < n; ++i) {
        m_ram[i] = rom[i];
        m_ramValid[i] = true;
    }
    for (int i = n; i < kNumRamVoices; ++i) m_ramValid[i] = false;
}

// ===========================================================================
// JSON Serialization — Patches
// ===========================================================================
std::string CDX21Memory::patchToJson(int slot, const DX21_Patch& patch) const {
    char buf[4096];
    int pos = 0;
    pos += std::snprintf(buf + pos, sizeof(buf) - pos,
        "{\n"
        "  \"slot\": %d,\n"
        "  \"name\": \"%s\",\n"
        "  \"alg\": %d,\n"
        "  \"fb\": %d,\n"
        "  \"lfo_speed\": %d,\n"
        "  \"lfo_delay\": %d,\n"
        "  \"pmd\": %d,\n"
        "  \"amd\": %d,\n"
        "  \"lfo_sync\": %d,\n"
        "  \"lfo_wave\": %d,\n"
        "  \"pms\": %d,\n"
        "  \"ams\": %d,\n"
        "  \"key_offset\": %d,\n"
        "  \"operators\": [\n",
        slot, patch.name,
        patch.alg, patch.fb, patch.lfo_speed, patch.lfo_delay,
        patch.pmd, patch.amd, patch.lfo_sync, patch.lfo_wave,
        patch.pms, patch.ams, patch.key_offset);

    for (int op = 0; op < 4; ++op) {
        const DX21_Operator& o = patch.op[op];
        pos += std::snprintf(buf + pos, sizeof(buf) - pos,
            "    {\"ar\":%d, \"d1r\":%d, \"d2r\":%d, \"rr\":%d, \"d1l\":%d, "
            "\"ls\":%d, \"rs\":%d, \"ebs\":%d, \"ame\":%d, \"kvs\":%d, "
            "\"out\":%d, \"crs\":%d, \"det\":%d}%s\n",
            o.ar, o.d1r, o.d2r, o.rr, o.d1l, o.ls, o.rs, o.ebs, o.ame, o.kvs,
            o.out, o.crs, o.det, (op < 3) ? "," : "");
    }

    // Per-voice function data (VCED 63-76), Pitch EG and op-enable.
    // Written after the operators so older readers simply ignore it;
    // jsonToPatch treats every key as optional.
    pos += std::snprintf(buf + pos, sizeof(buf) - pos,
        "  ],\n"
        "  \"mono\": %d, \"pb_range\": %d, \"porta_mode\": %d, \"porta_time\": %d,\n"
        "  \"foot_volume\": %d, \"sus_fs\": %d, \"porta_fs\": %d, \"chorus\": %d,\n"
        "  \"mw_pitch\": %d, \"mw_amp\": %d,\n"
        "  \"bc_pitch\": %d, \"bc_amp\": %d, \"bc_pbias\": %d, \"bc_ebias\": %d,\n"
        "  \"peg_r1\": %d, \"peg_r2\": %d, \"peg_r3\": %d,\n"
        "  \"peg_l1\": %d, \"peg_l2\": %d, \"peg_l3\": %d,\n"
        "  \"op_enable\": %d\n"
        "}\n",
        patch.mono, patch.pb_range, patch.porta_mode, patch.porta_time,
        patch.foot_volume, patch.sus_fs, patch.porta_fs, patch.chorus,
        patch.mw_pitch, patch.mw_amp,
        patch.bc_pitch, patch.bc_amp, patch.bc_pbias, patch.bc_ebias,
        patch.peg_r[0], patch.peg_r[1], patch.peg_r[2],
        patch.peg_l[0], patch.peg_l[1], patch.peg_l[2],
        patch.op_enable);

    return std::string(buf);
}

bool CDX21Memory::jsonToPatch(const std::string& json, DX21_Patch& outPatch) const {
    // Minimal JSON parser: scan for key:value pairs using sscanf
    DX21_Patch p;
    std::memset(&p, 0, sizeof(p));

    const char* s = json.c_str();
    // const char* e = s + json.size();  // (unused: using std::strstr instead)

    // Parse name
    const char* namePtr = std::strstr(s, "\"name\"");
    if (namePtr) {
        namePtr = std::strchr(namePtr, ':');
        if (namePtr) {
            while (*namePtr && (*namePtr == ' ' || *namePtr == '\t' || *namePtr == '"')) ++namePtr;
            int i = 0;
            while (*namePtr && *namePtr != '"' && i < 31) {
                p.name[i++] = *namePtr++;
            }
            p.name[i] = '\0';
        }
    }

    // Helper lambda for integer fields
    auto parseInt = [&](const char* key) -> int {
        char pattern[64];
        std::snprintf(pattern, sizeof(pattern), "\"%s\"", key);
        const char* p = std::strstr(s, pattern);
        if (!p) return -9999;
        p = std::strchr(p, ':');
        if (!p) return -9999;
        while (*p && !std::isdigit(*p) && *p != '-') ++p;
        return std::atoi(p);
    };

    int alg = parseInt("alg");    if (alg != -9999) p.alg = alg;
    int fb  = parseInt("fb");    if (fb  != -9999) p.fb = fb;
    int ls  = parseInt("lfo_speed"); if (ls != -9999) p.lfo_speed = ls;
    int ld  = parseInt("lfo_delay"); if (ld != -9999) p.lfo_delay = ld;
    int pmd = parseInt("pmd");    if (pmd != -9999) p.pmd = pmd;
    int amd = parseInt("amd");    if (amd != -9999) p.amd = amd;
    int sync= parseInt("lfo_sync");if (sync!= -9999) p.lfo_sync = sync;
    int wave= parseInt("lfo_wave");if (wave!= -9999) p.lfo_wave = wave;
    int pms = parseInt("pms");    if (pms != -9999) p.pms = pms;
    int ams = parseInt("ams");    if (ams != -9999) p.ams = ams;
    int ko  = parseInt("key_offset"); if (ko != -9999) p.key_offset = (int8_t)ko;

    // Per-voice function data / PEG / op-enable. All optional —
    // older JSON files simply leave the zero defaults (the PEG and
    // op-enable zero sentinels read as "flat" / "all on", see
    // patches.h).
    int v2;
    v2 = parseInt("mono");        if (v2 != -9999) p.mono        = (uint8_t)v2;
    v2 = parseInt("pb_range");    if (v2 != -9999) p.pb_range    = (uint8_t)v2;
    v2 = parseInt("porta_mode");  if (v2 != -9999) p.porta_mode  = (uint8_t)v2;
    v2 = parseInt("porta_time");  if (v2 != -9999) p.porta_time  = (uint8_t)v2;
    v2 = parseInt("foot_volume"); if (v2 != -9999) p.foot_volume = (uint8_t)v2;
    v2 = parseInt("sus_fs");      if (v2 != -9999) p.sus_fs      = (uint8_t)v2;
    v2 = parseInt("porta_fs");    if (v2 != -9999) p.porta_fs    = (uint8_t)v2;
    v2 = parseInt("chorus");      if (v2 != -9999) p.chorus      = (uint8_t)v2;
    v2 = parseInt("mw_pitch");    if (v2 != -9999) p.mw_pitch    = (uint8_t)v2;
    v2 = parseInt("mw_amp");      if (v2 != -9999) p.mw_amp      = (uint8_t)v2;
    v2 = parseInt("bc_pitch");    if (v2 != -9999) p.bc_pitch    = (uint8_t)v2;
    v2 = parseInt("bc_amp");      if (v2 != -9999) p.bc_amp      = (uint8_t)v2;
    v2 = parseInt("bc_pbias");    if (v2 != -9999) p.bc_pbias    = (uint8_t)v2;
    v2 = parseInt("bc_ebias");    if (v2 != -9999) p.bc_ebias    = (uint8_t)v2;
    v2 = parseInt("peg_r1");      if (v2 != -9999) p.peg_r[0]    = (uint8_t)v2;
    v2 = parseInt("peg_r2");      if (v2 != -9999) p.peg_r[1]    = (uint8_t)v2;
    v2 = parseInt("peg_r3");      if (v2 != -9999) p.peg_r[2]    = (uint8_t)v2;
    v2 = parseInt("peg_l1");      if (v2 != -9999) p.peg_l[0]    = (uint8_t)v2;
    v2 = parseInt("peg_l2");      if (v2 != -9999) p.peg_l[1]    = (uint8_t)v2;
    v2 = parseInt("peg_l3");      if (v2 != -9999) p.peg_l[2]    = (uint8_t)v2;
    v2 = parseInt("op_enable");   if (v2 != -9999) p.op_enable   = (uint8_t)v2;

    // Parse operators
    const char* opSection = std::strstr(s, "\"operators\"");
    if (opSection) {
        for (int op = 0; op < 4; ++op) {
            // Find next '{' in the operators array
            opSection = std::strchr(opSection, '{');
            if (!opSection) break;

            auto parseOpInt = [opSection](const char* key) -> int {
                char pattern[64];
                std::snprintf(pattern, sizeof(pattern), "\"%s\"", key);
                const char* p = std::strstr(opSection, pattern);
                if (!p) return -9999;
                p = std::strchr(p, ':');
                if (!p) return -9999;
                while (*p && !std::isdigit(*p) && *p != '-') ++p;
                return std::atoi(p);
            };

            DX21_Operator& o = p.op[op];
            int v = parseOpInt("ar");  if (v != -9999) o.ar = v;
            v = parseOpInt("d1r");      if (v != -9999) o.d1r = v;
            v = parseOpInt("d2r");      if (v != -9999) o.d2r = v;
            v = parseOpInt("rr");       if (v != -9999) o.rr = v;
            v = parseOpInt("d1l");      if (v != -9999) o.d1l = v;
            v = parseOpInt("ls");       if (v != -9999) o.ls = v;
            v = parseOpInt("rs");       if (v != -9999) o.rs = v;
            v = parseOpInt("ebs");      if (v != -9999) o.ebs = v;
            v = parseOpInt("ame");      if (v != -9999) o.ame = v;
            v = parseOpInt("kvs");      if (v != -9999) o.kvs = v;
            v = parseOpInt("out");      if (v != -9999) o.out = v;
            v = parseOpInt("crs");      if (v != -9999) o.crs = v;
            v = parseOpInt("det");      if (v != -9999) o.det = v;

            // Advance past this object
            opSection = std::strchr(opSection, '}');
            if (opSection) ++opSection;
        }
    }

    outPatch = p;
    return true;
}

// ===========================================================================
// JSON Serialization — Performances
// ===========================================================================
std::string CDX21Memory::perfToJson(int slot, const DX21_Performance& perf) const {
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
        "{\n"
        "  \"slot\": %d,\n"
        "  \"name\": \"%s\",\n"
        "  \"playMode\": %d,\n"
        "  \"voiceA\": %d,\n"
        "  \"voiceB\": %d,\n"
        "  \"splitPoint\": %d,\n"
        "  \"balance\": %d,\n"
        "  \"pbRange\": %d,\n"
        "  \"pbMode\": %d,\n"
        "  \"portaMode\": %d,\n"
        "  \"portaRate\": %d,\n"
        "  \"modSensitivity\": %d,\n"
        "  \"breathPitch\": %d,\n"
        "  \"breathAmp\": %d,\n"
        "  \"breathEGBias\": %d,\n"
        "  \"chorus\": %d,\n"
        "  \"transpose\": %d,\n"
        "  \"keyShift\": %d\n"
        "}\n",
        slot, perf.name,
        perf.playMode, perf.voiceA, perf.voiceB, perf.splitPoint,
        perf.balance, perf.pbRange, perf.pbMode, perf.portaMode,
        perf.portaRate, perf.modSensitivity, perf.breathPitch,
        perf.breathAmp, perf.breathEGBias, perf.chorus,
        perf.transpose, perf.keyShift);
    return std::string(buf);
}

bool CDX21Memory::jsonToPerf(const std::string& json, DX21_Performance& outPerf) const {
    DX21_Performance p;
    p.initDefaults();

    const char* s = json.c_str();

    auto parseInt = [s](const char* key) -> int {
        char pattern[64];
        std::snprintf(pattern, sizeof(pattern), "\"%s\"", key);
        const char* p = std::strstr(s, pattern);
        if (!p) return -9999;
        p = std::strchr(p, ':');
        if (!p) return -9999;
        while (*p && !std::isdigit(*p) && *p != '-') ++p;
        return std::atoi(p);
    };

    // Parse name
    const char* namePtr = std::strstr(s, "\"name\"");
    if (namePtr) {
        namePtr = std::strchr(namePtr, ':');
        if (namePtr) {
            while (*namePtr && (*namePtr == ' ' || *namePtr == '\t' || *namePtr == '"')) ++namePtr;
            int i = 0;
            while (*namePtr && *namePtr != '"' && i < 15) {
                p.name[i++] = *namePtr++;
            }
            p.name[i] = '\0';
        }
    }

    int v = parseInt("playMode");      if (v != -9999) p.playMode = v;
    v = parseInt("voiceA");            if (v != -9999) p.voiceA = v;
    v = parseInt("voiceB");            if (v != -9999) p.voiceB = v;
    v = parseInt("splitPoint");        if (v != -9999) p.splitPoint = v;
    v = parseInt("balance");           if (v != -9999) p.balance = v;
    v = parseInt("pbRange");           if (v != -9999) p.pbRange = v;
    v = parseInt("pbMode");            if (v != -9999) p.pbMode = v;
    v = parseInt("portaMode");         if (v != -9999) p.portaMode = v;
    v = parseInt("portaRate");         if (v != -9999) p.portaRate = v;
    v = parseInt("modSensitivity");    if (v != -9999) p.modSensitivity = v;
    v = parseInt("breathPitch");       if (v != -9999) p.breathPitch = v;
    v = parseInt("breathAmp");         if (v != -9999) p.breathAmp = v;
    v = parseInt("breathEGBias");      if (v != -9999) p.breathEGBias = v;
    v = parseInt("chorus");            if (v != -9999) p.chorus = v;
    v = parseInt("transpose");         if (v != -9999) p.transpose = (int8_t)v;
    v = parseInt("keyShift");          if (v != -9999) p.keyShift = (int8_t)v;

    outPerf = p;
    return true;
}

// ===========================================================================
// Persistence — RAM Bank
// ===========================================================================
bool CDX21Memory::saveRamBank(const std::string& dirPath) {
    if (!m_fs) return false;
    if (dirPath.empty()) return false;

    // The FatFS-backed implementation needs an explicit mkdir before any
    // file write into a fresh directory. We call MakeDirectory recursively
    // (= `mkdir -p`) on the bank directory itself; the FS implementation
    // is responsible for also creating missing parent directories.
    //
    // Defense in depth: the GitHub-Actions release pipeline (and the local
    // helper script scripts/prepare_sd_skeleton.sh) also pre-creates the
    // MICRODX21/BANK_01..16/ skeleton on the SD image, so this is normally
    // a no-op. It is the fallback for users who flashed the kernel image
    // onto an otherwise empty SD card.
    if (!m_fs->MakeDirectory(dirPath, /*recursive=*/true)) {
        // MakeDirectory may legitimately fail on FS implementations that
        // do not support directory creation (the IFileSystem default
        // returns false). The caller in opmemuadapter.h propagates this
        // as MemoryResult::SaveFailed, which the UI surfaces as
        // "SAVE FAILED" on the OLED status line.
        return false;
    }

    for (int i = 0; i < kNumRamVoices; ++i) {
        if (!m_ramValid[i]) continue;

        char filename[64];
        std::snprintf(filename, sizeof(filename), "%s/voice_%02d.json",
                      dirPath.c_str(), i);
        std::string json = patchToJson(i, m_ram[i]);
        if (!m_fs->writeFileFromString(filename, json)) return false;
    }
    return true;
}

bool CDX21Memory::loadRamBank(const std::string& dirPath) {
    if (!m_fs) return false;

    clearAllRamVoices();

    std::vector<std::string> files;
    if (!m_fs->listDir(dirPath, files)) return false;

    for (const auto& fname : files) {
        // Parse filename: voice_XX.json
        if (fname.size() < 12) continue;
        if (fname.rfind("voice_", 0) != 0) continue;
        if (fname.rfind(".json") != fname.size() - 5) continue;

        int slot = std::atoi(fname.c_str() + 6);
        if (slot < 0 || slot >= kNumRamVoices) continue;

        std::vector<uint8_t> data;
        std::string path = dirPath + "/" + fname;
        if (!m_fs->readFileToVector(path, data)) continue;

        std::string json(data.begin(), data.end());
        if (jsonToPatch(json, m_ram[slot])) {
            m_ramValid[slot] = true;
        }
    }
    return true;
}

// ===========================================================================
// Persistence — Performance Bank
// ===========================================================================
bool CDX21Memory::savePerformanceBank(const std::string& filePath) {
    if (!m_fs) return false;
    if (filePath.empty()) return false;

    // Make sure the parent directory of the performance file exists.
    // For the canonical layout (`MICRODX21/performances.json`) this is the
    // `MICRODX21` directory itself, which the release pipeline and the
    // MakeDirectory(...) call above in saveRamBank() will have already
    // created. This call is the runtime fallback for users who didn't
    // flash the pre-populated SD image.
    std::string parent = parentDir(filePath);
    if (!parent.empty() && !m_fs->MakeDirectory(parent, /*recursive=*/true)) {
        // See note in saveRamBank() above.
        return false;
    }

    std::string json = "{\n  \"performances\": [\n";
    for (int i = 0; i < kNumPerformances; ++i) {
        json += perfToJson(i, m_perf[i]);
        if (i + 1 < kNumPerformances) json += ",";
        json += "\n";
    }
    json += "  ]\n}\n";

    return m_fs->writeFileFromString(filePath, json);
}

bool CDX21Memory::loadPerformanceBank(const std::string& filePath) {
    if (!m_fs) return false;

    std::vector<uint8_t> data;
    if (!m_fs->readFileToVector(filePath, data)) return false;

    std::string json(data.begin(), data.end());
    clearAllPerformances();

    // Find "performances" array
    const char* s = json.c_str();
    const char* arr = std::strstr(s, "\"performances\"");
    if (!arr) return false;

    for (int i = 0; i < kNumPerformances; ++i) {
        arr = std::strchr(arr, '{');
        if (!arr) break;

        // Find matching closing brace (naive: first '}')
        const char* end = std::strchr(arr, '}');
        if (!end) break;
        ++end;

        std::string obj(arr, end - arr);
        if (jsonToPerf(obj, m_perf[i])) {
            m_perfValid[i] = true;
        }

        arr = end;
    }
    return true;
}

// ===========================================================================
// SysEx Import / Export
// ===========================================================================

// VCED byte layout (76 bytes per voice) — reconstructed from DX21 spec:
// Byte   0: Algorithm (0-7)
// Byte   1: Feedback (0-7)
// Byte   2: LFO Speed (0-255)
// Byte   3: LFO Delay (0-127)
// Byte   4: PMD (0-127)
// Byte   5: AMD (0-127)
// Byte   6: LFO Sync (0/1)
// Byte   7: LFO Waveform (0-3)
// Byte   8: PMS (0-7) | AMS (0-3) in nibbles: (PMS<<4) | AMS
// Byte   9: Transpose (0-48, offset by 24)
// Bytes 10-73: 4 operators * 16 bytes each
//   Each OP: ar, d1r, d2r, rr, d1l, ls, rs, ebs, ame, kvs, out, crs, det, [3 reserved]
// Byte  74: Pitch Bend Range (0-12)
// Byte  75: (reserved)
//
// Note: This is a best-effort reconstruction. Some byte positions may
// differ on the real DX21 hardware.

static const int VCED_OP_SIZE = 16;

bool CDX21Memory::vcedToPatch(const uint8_t* vced76, DX21_Patch& outPatch) const {
    std::memset(&outPatch, 0, sizeof(outPatch));

    outPatch.alg        = vced76[0]  & 0x07;
    outPatch.fb         = vced76[1]  & 0x07;
    outPatch.lfo_speed  = vced76[2];
    outPatch.lfo_delay  = vced76[3]  & 0x7F;
    outPatch.pmd        = vced76[4]  & 0x7F;
    outPatch.amd        = vced76[5]  & 0x7F;
    outPatch.lfo_sync   = vced76[6]  & 0x01;
    outPatch.lfo_wave   = vced76[7]  & 0x03;
    outPatch.pms        = (vced76[8] >> 4) & 0x07;
    outPatch.ams        = vced76[8]  & 0x03;
    outPatch.key_offset = static_cast<int8_t>(vced76[9]) - 24;

    for (int op = 0; op < 4; ++op) {
        const uint8_t* opData = vced76 + 10 + op * VCED_OP_SIZE;
        DX21_Operator& o = outPatch.op[op];
        o.ar  = opData[0]  & 0x1F;
        o.d1r = opData[1]  & 0x1F;
        o.d2r = opData[2]  & 0x1F;
        o.rr  = opData[3]  & 0x0F;
        o.d1l = opData[4]  & 0x0F;
        o.ls  = opData[5];        // 0-99
        o.rs  = opData[6]  & 0x03;
        o.ebs = opData[7]  & 0x07;
        o.ame = opData[8]  & 0x01;
        o.kvs = opData[9]  & 0x07;
        o.out = opData[10];       // 0-99
        o.crs = opData[11] & 0x3F;
        o.det = opData[12] & 0x07;
    }

    std::snprintf(outPatch.name, sizeof(outPatch.name), "VCED Import");
    return true;
}

bool CDX21Memory::patchToVced(const DX21_Patch& patch, uint8_t* vced76) const {
    std::memset(vced76, 0, 76);

    vced76[0] = patch.alg & 0x07;
    vced76[1] = patch.fb  & 0x07;
    vced76[2] = patch.lfo_speed;
    vced76[3] = patch.lfo_delay & 0x7F;
    vced76[4] = patch.pmd & 0x7F;
    vced76[5] = patch.amd & 0x7F;
    vced76[6] = patch.lfo_sync & 0x01;
    vced76[7] = patch.lfo_wave & 0x03;
    vced76[8] = ((patch.pms & 0x07) << 4) | (patch.ams & 0x03);
    vced76[9] = static_cast<uint8_t>(patch.key_offset + 24);

    for (int op = 0; op < 4; ++op) {
        const DX21_Operator& o = patch.op[op];
        uint8_t* opData = vced76 + 10 + op * VCED_OP_SIZE;
        opData[0]  = o.ar  & 0x1F;
        opData[1]  = o.d1r & 0x1F;
        opData[2]  = o.d2r & 0x1F;
        opData[3]  = o.rr  & 0x0F;
        opData[4]  = o.d1l & 0x0F;
        opData[5]  = o.ls;           // clamp to 0-99 in encoder if needed
        opData[6]  = o.rs  & 0x03;
        opData[7]  = o.ebs & 0x07;
        opData[8]  = o.ame & 0x01;
        opData[9]  = o.kvs & 0x07;
        opData[10] = o.out;          // clamp to 0-99 in encoder if needed
        opData[11] = o.crs & 0x3F;
        opData[12] = o.det & 0x07;
    }

    vced76[74] = 2;  // Pitch Bend Range default
    vced76[75] = 0;
    return true;
}

// ===========================================================================
// Real DX21 SysEx formats (owner's manual, MIDI DATA FORMAT)
//
// Operator block order ON THE WIRE is OP4, OP2, OP3, OP1 (tables
// 5-1/5-2) — kWireOpOrder maps wire block index → internal op index
// (internal 0..3 = panel OP1..OP4).
//
// Scaling notes:
//   - VCED 54 LFO SPEED is 0..99; the engine stores the YM2151 LFRQ
//     value 0..255 → scaled on both directions.
//   - pmd/amd/lfo_delay are stored 0..127 internally but are 0..99
//     on the wire → clamped on encode, taken raw on decode.
//   - DET is 0..6 internally (center 3); the wire field allows 0..7
//     → clamped to 6 on decode.
//
// Checksum: lowest 7 bits of the two's complement of the data-byte
// sum (sum + checksum ≡ 0 mod 128). Note this differs from the
// legacy project format, which used the plain sum.
// ===========================================================================

static const int kWireOpOrder[4] = { 3, 1, 2, 0 };  // OP4, OP2, OP3, OP1

static inline uint8_t clamp99(int v) {
    if (v < 0) v = 0;
    if (v > 99) v = 99;
    return static_cast<uint8_t>(v);
}

static inline uint8_t yamahaChecksum(const uint8_t* data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) sum += data[i];
    return static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
}

bool CDX21Memory::patchToVced93(const DX21_Patch& patch, uint8_t* v) const {
    std::memset(v, 0, DX21_VCED93_SIZE);

    for (int w = 0; w < 4; ++w) {
        const DX21_Operator& o = patch.op[kWireOpOrder[w]];
        uint8_t* d = v + w * 13;
        d[0]  = o.ar  & 0x1F;
        d[1]  = o.d1r & 0x1F;
        d[2]  = o.d2r & 0x1F;
        d[3]  = o.rr  & 0x0F;
        d[4]  = o.d1l & 0x0F;
        d[5]  = clamp99(o.ls);
        d[6]  = o.rs  & 0x03;
        d[7]  = o.ebs & 0x07;
        d[8]  = o.ame & 0x01;
        d[9]  = o.kvs & 0x07;
        d[10] = clamp99(o.out);
        d[11] = o.crs & 0x3F;
        d[12] = o.det > 6 ? 6 : o.det;
    }

    v[52] = patch.alg & 0x07;
    v[53] = patch.fb  & 0x07;
    v[54] = clamp99((patch.lfo_speed * 99 + 127) / 255);
    v[55] = clamp99(patch.lfo_delay);
    v[56] = clamp99(patch.pmd);
    v[57] = clamp99(patch.amd);
    v[58] = patch.lfo_sync & 0x01;
    v[59] = patch.lfo_wave & 0x03;
    v[60] = patch.pms & 0x07;
    v[61] = patch.ams & 0x07;
    {
        int t = patch.key_offset + 24;
        if (t < 0) t = 0;
        if (t > 48) t = 48;
        v[62] = static_cast<uint8_t>(t);
    }
    v[63] = patch.mono & 0x01;
    v[64] = patch.pb_range > 12 ? 12 : patch.pb_range;
    v[65] = patch.porta_mode & 0x01;
    v[66] = clamp99(patch.porta_time);
    v[67] = clamp99(patch.foot_volume);
    v[68] = patch.sus_fs & 0x01;
    v[69] = patch.porta_fs & 0x01;
    v[70] = patch.chorus & 0x01;
    v[71] = clamp99(patch.mw_pitch);
    v[72] = clamp99(patch.mw_amp);
    v[73] = clamp99(patch.bc_pitch);
    v[74] = clamp99(patch.bc_amp);
    v[75] = clamp99(patch.bc_pbias);
    v[76] = clamp99(patch.bc_ebias);

    // 77-86: voice name, 10 ASCII chars, space-padded.
    for (int i = 0; i < 10; ++i) {
        char c = patch.name[i];
        if (c == '\0') {
            for (; i < 10; ++i) v[77 + i] = ' ';
            break;
        }
        v[77 + i] = static_cast<uint8_t>(c) & 0x7F;
    }

    // 87-92: Pitch EG. Encode the *effective* levels so legacy
    // patches (all-zero sentinel) export as the flat 50/50/50.
    for (int i = 0; i < 3; ++i) {
        v[87 + i] = clamp99(patch.peg_r[i]);
        v[90 + i] = dx21_effective_peg_level(&patch, i);
    }
    return true;
}

bool CDX21Memory::vced93ToPatch(const uint8_t* v, DX21_Patch& p) const {
    std::memset(&p, 0, sizeof(p));

    for (int w = 0; w < 4; ++w) {
        DX21_Operator& o = p.op[kWireOpOrder[w]];
        const uint8_t* d = v + w * 13;
        o.ar  = d[0]  & 0x1F;
        o.d1r = d[1]  & 0x1F;
        o.d2r = d[2]  & 0x1F;
        o.rr  = d[3]  & 0x0F;
        o.d1l = d[4]  & 0x0F;
        o.ls  = clamp99(d[5]);
        o.rs  = d[6]  & 0x03;
        o.ebs = d[7]  & 0x07;
        o.ame = d[8]  & 0x01;
        o.kvs = d[9]  & 0x07;
        o.out = clamp99(d[10]);
        o.crs = d[11] & 0x3F;
        o.det = d[12] > 6 ? 6 : d[12];
    }

    p.alg       = v[52] & 0x07;
    p.fb        = v[53] & 0x07;
    p.lfo_speed = static_cast<uint8_t>((clamp99(v[54]) * 255 + 49) / 99);
    p.lfo_delay = clamp99(v[55]);
    p.pmd       = clamp99(v[56]);
    p.amd       = clamp99(v[57]);
    p.lfo_sync  = v[58] & 0x01;
    p.lfo_wave  = v[59] & 0x03;
    p.pms       = v[60] & 0x07;
    p.ams       = v[61] & 0x03;
    p.key_offset = static_cast<int8_t>((v[62] > 48 ? 48 : v[62]) - 24);
    p.mono       = v[63] & 0x01;
    p.pb_range   = v[64] > 12 ? 12 : v[64];
    p.porta_mode = v[65] & 0x01;
    p.porta_time = clamp99(v[66]);
    p.foot_volume= clamp99(v[67]);
    p.sus_fs     = v[68] & 0x01;
    p.porta_fs   = v[69] & 0x01;
    p.chorus     = v[70] & 0x01;
    p.mw_pitch   = clamp99(v[71]);
    p.mw_amp     = clamp99(v[72]);
    p.bc_pitch   = clamp99(v[73]);
    p.bc_amp     = clamp99(v[74]);
    p.bc_pbias   = clamp99(v[75]);
    p.bc_ebias   = clamp99(v[76]);

    int n = 0;
    for (int i = 0; i < 10; ++i) {
        char c = static_cast<char>(v[77 + i] & 0x7F);
        p.name[i] = (c >= 0x20) ? c : ' ';
        if (p.name[i] != ' ') n = i + 1;
    }
    p.name[n] = '\0';
    if (n == 0) std::snprintf(p.name, sizeof(p.name), "VCED Import");

    for (int i = 0; i < 3; ++i) {
        p.peg_r[i] = clamp99(v[87 + i]);
        p.peg_l[i] = clamp99(v[90 + i]);
    }
    // An incoming flat PEG (50/50/50) must stay flat even though the
    // levels are non-zero — nothing to do, the values are stored
    // verbatim. (The 0/0/0 sentinel only applies to legacy data.)

    p.op_enable = 0x0F;  // VCED carries no op-enable; all on
    return true;
}

bool CDX21Memory::patchToVmem(const DX21_Patch& patch, uint8_t* v) const {
    std::memset(v, 0, DX21_VMEM_SIZE);

    for (int w = 0; w < 4; ++w) {
        const DX21_Operator& o = patch.op[kWireOpOrder[w]];
        uint8_t* d = v + w * 10;
        d[0] = o.ar  & 0x1F;
        d[1] = o.d1r & 0x1F;
        d[2] = o.d2r & 0x1F;
        d[3] = o.rr  & 0x0F;
        d[4] = o.d1l & 0x0F;
        d[5] = clamp99(o.ls);
        d[6] = static_cast<uint8_t>(((o.ame & 0x01) << 6) |
                                    ((o.ebs & 0x07) << 3) |
                                     (o.kvs & 0x07));
        d[7] = clamp99(o.out);
        d[8] = o.crs & 0x3F;
        d[9] = static_cast<uint8_t>(((o.rs & 0x03) << 3) |
                                     (o.det > 6 ? 6 : o.det));
    }

    v[40] = static_cast<uint8_t>(((patch.lfo_sync & 0x01) << 6) |
                                 ((patch.fb & 0x07) << 3) |
                                  (patch.alg & 0x07));
    v[41] = clamp99((patch.lfo_speed * 99 + 127) / 255);
    v[42] = clamp99(patch.lfo_delay);
    v[43] = clamp99(patch.pmd);
    v[44] = clamp99(patch.amd);
    v[45] = static_cast<uint8_t>(((patch.pms & 0x07) << 4) |
                                 ((patch.ams & 0x03) << 2) |
                                  (patch.lfo_wave & 0x03));
    {
        int t = patch.key_offset + 24;
        if (t < 0) t = 0;
        if (t > 48) t = 48;
        v[46] = static_cast<uint8_t>(t);
    }
    v[47] = patch.pb_range > 12 ? 12 : patch.pb_range;
    v[48] = static_cast<uint8_t>(((patch.chorus     & 0x01) << 4) |
                                 ((patch.mono       & 0x01) << 3) |
                                 ((patch.sus_fs     & 0x01) << 2) |
                                 ((patch.porta_fs   & 0x01) << 1) |
                                  (patch.porta_mode & 0x01));
    v[49] = clamp99(patch.porta_time);
    v[50] = clamp99(patch.foot_volume);
    v[51] = clamp99(patch.mw_pitch);
    v[52] = clamp99(patch.mw_amp);
    v[53] = clamp99(patch.bc_pitch);
    v[54] = clamp99(patch.bc_amp);
    v[55] = clamp99(patch.bc_pbias);
    v[56] = clamp99(patch.bc_ebias);

    for (int i = 0; i < 10; ++i) {
        char c = patch.name[i];
        if (c == '\0') {
            for (; i < 10; ++i) v[57 + i] = ' ';
            break;
        }
        v[57 + i] = static_cast<uint8_t>(c) & 0x7F;
    }

    for (int i = 0; i < 3; ++i) {
        v[67 + i] = clamp99(patch.peg_r[i]);
        v[70 + i] = dx21_effective_peg_level(&patch, i);
    }
    return true;
}

bool CDX21Memory::vmemToPatch(const uint8_t* v, DX21_Patch& p) const {
    std::memset(&p, 0, sizeof(p));

    for (int w = 0; w < 4; ++w) {
        DX21_Operator& o = p.op[kWireOpOrder[w]];
        const uint8_t* d = v + w * 10;
        o.ar  = d[0] & 0x1F;
        o.d1r = d[1] & 0x1F;
        o.d2r = d[2] & 0x1F;
        o.rr  = d[3] & 0x0F;
        o.d1l = d[4] & 0x0F;
        o.ls  = clamp99(d[5]);
        o.ame = (d[6] >> 6) & 0x01;
        o.ebs = (d[6] >> 3) & 0x07;
        o.kvs = d[6] & 0x07;
        o.out = clamp99(d[7]);
        o.crs = d[8] & 0x3F;
        o.rs  = (d[9] >> 3) & 0x03;
        o.det = (d[9] & 0x07) > 6 ? 6 : (d[9] & 0x07);
    }

    p.lfo_sync  = (v[40] >> 6) & 0x01;
    p.fb        = (v[40] >> 3) & 0x07;
    p.alg       = v[40] & 0x07;
    p.lfo_speed = static_cast<uint8_t>((clamp99(v[41]) * 255 + 49) / 99);
    p.lfo_delay = clamp99(v[42]);
    p.pmd       = clamp99(v[43]);
    p.amd       = clamp99(v[44]);
    p.pms       = (v[45] >> 4) & 0x07;
    p.ams       = (v[45] >> 2) & 0x03;
    p.lfo_wave  = v[45] & 0x03;
    p.key_offset = static_cast<int8_t>((v[46] > 48 ? 48 : v[46]) - 24);
    p.pb_range   = v[47] > 12 ? 12 : v[47];
    p.chorus     = (v[48] >> 4) & 0x01;
    p.mono       = (v[48] >> 3) & 0x01;
    p.sus_fs     = (v[48] >> 2) & 0x01;
    p.porta_fs   = (v[48] >> 1) & 0x01;
    p.porta_mode = v[48] & 0x01;
    p.porta_time = clamp99(v[49]);
    p.foot_volume= clamp99(v[50]);
    p.mw_pitch   = clamp99(v[51]);
    p.mw_amp     = clamp99(v[52]);
    p.bc_pitch   = clamp99(v[53]);
    p.bc_amp     = clamp99(v[54]);
    p.bc_pbias   = clamp99(v[55]);
    p.bc_ebias   = clamp99(v[56]);

    int n = 0;
    for (int i = 0; i < 10; ++i) {
        char c = static_cast<char>(v[57 + i] & 0x7F);
        p.name[i] = (c >= 0x20) ? c : ' ';
        if (p.name[i] != ' ') n = i + 1;
    }
    p.name[n] = '\0';
    if (n == 0) std::snprintf(p.name, sizeof(p.name), "VMEM Import");

    for (int i = 0; i < 3; ++i) {
        p.peg_r[i] = clamp99(v[67 + i]);
        p.peg_l[i] = clamp99(v[70 + i]);
    }
    p.op_enable = 0x0F;
    return true;
}

int CDX21Memory::importSysex(const uint8_t* data, size_t len) {
    if (len < 8) return -1;

    // Check Yamaha SysEx header: F0 43
    if (data[0] != 0xF0 || data[1] != 0x43) return -1;

    uint8_t format = data[3];
    uint16_t byteCount = (static_cast<uint16_t>(data[4]) << 7) | data[5];

    // --- Real DX21 32-voice VMEM bulk: format 0x04, 4096 bytes ---
    if (format == 0x04) {
        if (byteCount != DX21_VMEM_BULK_SIZE) return -1;
        if (len < static_cast<size_t>(6 + byteCount + 2)) return -1;
        // Two's-complement checksum: sum of data + checksum ≡ 0 mod 128
        if (yamahaChecksum(data + 6, byteCount) != data[6 + byteCount])
            return -1;

        // FUNCTION #23 (Memory Protect): bulk dump writes all 32 RAM
        // voices. Reject when protected.
        if (m_memoryProt) return -1;

        int imported = 0;
        for (int v = 0; v < 32; ++v) {
            // 73 VMEM bytes per voice, zero-padded to 128.
            const uint8_t* vmem = data + 6 + v * DX21_VMEM_PADDED;
            if (vmemToPatch(vmem, m_ram[v])) {
                m_ramValid[v] = true;
                ++imported;
            }
        }
        return imported;
    }

    // --- Legacy microDX21 bulk (receive-only): format 0x09, 2432 B ---
    if (format == 0x09) {
        if (byteCount != DX21_SYSEX_BULK_SIZE) return -1;
        if (len < static_cast<size_t>(6 + byteCount + 2)) return -1;
        uint8_t checksum = 0;
        for (uint16_t i = 0; i < byteCount; ++i) checksum += data[6 + i];
        if ((checksum & 0x7F) != data[6 + byteCount]) return -1;

        if (m_memoryProt) return -1;

        int imported = 0;
        for (int v = 0; v < 32; ++v) {
            const uint8_t* vced = data + 6 + v * DX21_SYSEX_VCED_SIZE;
            if (vcedToPatch(vced, m_ram[v])) {
                m_ramValid[v] = true;
                ++imported;
            }
        }
        return imported;
    }

    return -1;
}

bool CDX21Memory::exportSysex(std::vector<uint8_t>& out) const {
    out.clear();
    out.reserve(8 + DX21_VMEM_BULK_SIZE);

    out.push_back(0xF0);
    out.push_back(0x43);
    out.push_back(0x00);  // Device number 0
    out.push_back(0x04);  // 32-voice VMEM bulk dump (real DX21 format)

    // Byte count: MSB first, 7-bit split (4096 → 0x20 0x00)
    out.push_back(static_cast<uint8_t>(DX21_VMEM_BULK_SIZE >> 7));
    out.push_back(static_cast<uint8_t>(DX21_VMEM_BULK_SIZE & 0x7F));

    // Encode all 32 RAM voices: 73 VMEM bytes + 55 zero-pad each.
    uint8_t vmem[DX21_VMEM_PADDED];
    for (int v = 0; v < 32; ++v) {
        std::memset(vmem, 0, sizeof(vmem));
        if (m_ramValid[v]) {
            patchToVmem(m_ram[v], vmem);
            // Real DX21 hardware dumps carry a constant 0x01 at
            // padding offset 91 of every voice record (verified
            // against the four factory banks in doc/SYX — all 128
            // voices). Undocumented in the manual; mirrored here so
            // our exports are byte-identical to hardware dumps.
            vmem[91] = 0x01;
        }
        out.insert(out.end(), vmem, vmem + DX21_VMEM_PADDED);
    }

    out.push_back(yamahaChecksum(out.data() + 6, DX21_VMEM_BULK_SIZE));
    out.push_back(0xF7);

    return true;
}

bool CDX21Memory::exportVoiceSysex(const DX21_Patch& patch,
                                   std::vector<uint8_t>& out) const {
    out.clear();
    out.reserve(8 + DX21_VCED93_SIZE);

    out.push_back(0xF0);
    out.push_back(0x43);
    out.push_back(0x00);  // Device number 0
    out.push_back(0x03);  // 1-voice VCED dump (real DX21 format)

    // Byte count: MSB first (93 → 0x00 0x5D)
    out.push_back(static_cast<uint8_t>(DX21_VCED93_SIZE >> 7));
    out.push_back(static_cast<uint8_t>(DX21_VCED93_SIZE & 0x7F));

    uint8_t vced[DX21_VCED93_SIZE];
    if (!patchToVced93(patch, vced)) {
        out.clear();
        return false;
    }
    out.insert(out.end(), vced, vced + DX21_VCED93_SIZE);

    out.push_back(yamahaChecksum(out.data() + 6, DX21_VCED93_SIZE));
    out.push_back(0xF7);

    return true;
}

bool CDX21Memory::importVoiceSysex(const uint8_t* data, size_t len, int slot) {
    if (!data || slot < 0 || slot >= kNumRamVoices) return false;

    if (len < 8) return false;
    if (data[0] != 0xF0 || data[1] != 0x43) return false;
    if (data[3] != 0x03) return false;

    uint16_t byteCount = (static_cast<uint16_t>(data[4]) << 7) | data[5];

    // --- Real DX21 1-voice VCED: 93 bytes ---
    if (byteCount == DX21_VCED93_SIZE) {
        if (len < static_cast<size_t>(6 + byteCount + 2)) return false;
        if (yamahaChecksum(data + 6, byteCount) != data[6 + byteCount])
            return false;

        // FUNCTION #23 (Memory Protect): a 1-voice dump writes the
        // edit slot in RAM — same policy as the real-time parameter
        // change and the bulk dump.
        if (m_memoryProt) return false;

        if (!vced93ToPatch(data + 6, m_ram[slot])) return false;
        m_ramValid[slot] = true;
        return true;
    }

    // --- Legacy microDX21 76-byte record (receive-only) ---
    if (byteCount == DX21_SYSEX_VCED_SIZE) {
        if (len < static_cast<size_t>(6 + byteCount + 2)) return false;
        uint8_t checksum = 0;
        for (uint16_t i = 0; i < byteCount; ++i) checksum += data[6 + i];
        if ((checksum & 0x7F) != data[6 + byteCount]) return false;

        if (m_memoryProt) return false;

        if (!vcedToPatch(data + 6, m_ram[slot])) return false;
        m_ramValid[slot] = true;
        return true;
    }

    return false;
}
