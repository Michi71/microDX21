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

    pos += std::snprintf(buf + pos, sizeof(buf) - pos,
        "  ]\n}\n");

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

int CDX21Memory::importSysex(const uint8_t* data, size_t len) {
    if (len < 8) return -1;

    // Check Yamaha SysEx header: F0 43
    if (data[0] != 0xF0 || data[1] != 0x43) return -1;

    // Check format: 32-voice VCED bulk dump = byte 3 == 0x09
    uint8_t format = data[3];
    if (format != 0x09) return -1;

    // Byte count: 2 bytes, MSB first
    if (len < 8 + 2) return -1;
    uint16_t byteCount = (static_cast<uint16_t>(data[4]) << 7) | data[5];
    if (byteCount != DX21_SYSEX_BULK_SIZE) return -1;

    // Checksum
    if (len < static_cast<size_t>(8 + 2 + byteCount + 1 + 1)) return -1;
    uint8_t checksum = 0;
    for (uint16_t i = 0; i < byteCount; ++i) {
        checksum += data[6 + i];
    }
    checksum &= 0x7F;
    if (data[6 + byteCount] != checksum) return -1;

    // FUNCTION #23 (Memory Protect): bulk dump is a write to all 32
    // RAM voices. Reject when protected.
    if (m_memoryProt) return -1;

    // Parse voices
    int imported = 0;
    for (int v = 0; v < 32; ++v) {
        const uint8_t* vced = data + 6 + v * 76;
        if (vcedToPatch(vced, m_ram[v])) {
            m_ramValid[v] = true;
            ++imported;
        }
    }

    return imported;
}

bool CDX21Memory::exportSysex(std::vector<uint8_t>& out) const {
    out.clear();
    out.reserve(8 + DX21_SYSEX_BULK_SIZE);

    out.push_back(0xF0);
    out.push_back(0x43);
    out.push_back(0x00);  // Device number 0
    out.push_back(0x09);  // 32-voice VCED bulk dump

    // Byte count: MSB first
    out.push_back(static_cast<uint8_t>(DX21_SYSEX_BULK_SIZE >> 7));
    out.push_back(static_cast<uint8_t>(DX21_SYSEX_BULK_SIZE & 0x7F));

    // Encode all 32 RAM voices
    uint8_t vced[76];
    for (int v = 0; v < 32; ++v) {
        if (m_ramValid[v]) {
            patchToVced(m_ram[v], vced);
        } else {
            std::memset(vced, 0, 76);
        }
        out.insert(out.end(), vced, vced + 76);
    }

    // Checksum
    uint8_t checksum = 0;
    for (size_t i = 6; i < out.size(); ++i) {
        checksum += out[i];
    }
    checksum &= 0x7F;
    out.push_back(checksum);
    out.push_back(0xF7);

    return true;
}
