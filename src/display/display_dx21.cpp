//
// display_dx21.cpp
//
// CDX21Display implementation for 128x32 SSD1305 SPI OLED.
// Renders the 6 DX21 modes (PLAY, EDIT, PERFORMANCE, FUNCTION, MEMORY,
// COMPARE) using the original ROM's display layout.
//

#include "display_dx21.h"
#include <circle/logger.h>
#include <circle/util.h>
#include <string.h>
#include <stdio.h>

using namespace DX21UI;
using namespace DX21UI7Seg;

// External 6x8 font (from Circle's addon/display/font6x8.h).
// Each char = 6 cols x 8 rows, stored as 8 bytes per char (1 bit per row).
#include <font6x8.h>
constexpr int FONT6x8_W = 6;
constexpr int FONT6x8_H = 8;

static const char FromDisplay[] = "dx21disp";

// ═══════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════

CDX21Display::CDX21Display(CSPIMaster* pSPI, const Config& cfg)
    : m_pSPI(pSPI)
    , m_Config(cfg)
    , m_pDisplay(nullptr)
    , m_Mode(kModePlay)
    , m_ParamIdx(0)
    , m_Value(0)
    , m_ValueStr(nullptr)
    , m_VoiceName("Init Voice")
    , m_VoiceNum(1)
    , m_Status(nullptr)
    , m_bCompare(false)
    , m_bMemProt(false)
    , m_bDirty(true)
{
    memset(m_PageBuf, 0, sizeof(m_PageBuf));
}

CDX21Display::~CDX21Display() {
    delete m_pDisplay;
}

bool CDX21Display::Initialize() {
    m_pDisplay = new CSSD1305SPIDisplay(m_pSPI,
                                         m_Config.nDCPin,
                                         m_Config.nResetPin,
                                         kWidth, kHeight,
                                         m_Config.nCPOL, m_Config.nCPHA,
                                         m_Config.nClockSpeed,
                                         m_Config.nChipSelect);
    if (!m_pDisplay) {
        CLogger::Get()->Write(FromDisplay, LogError, "alloc failed");
        return false;
    }
    if (!m_pDisplay->Initialize()) {
        CLogger::Get()->Write(FromDisplay, LogError, "init failed");
        delete m_pDisplay;
        m_pDisplay = nullptr;
        return false;
    }
    m_pDisplay->On();
    m_pDisplay->Clear();
    m_pDisplay->Show();
    m_bDirty = true;
    CLogger::Get()->Write(FromDisplay, LogNotice,
        "128x32 SSD1305 SPI display ready (DC=%u RST=%u CS=%u @%u Hz)",
        m_Config.nDCPin,
        m_Config.nResetPin == GPIO_PINS ? 0 : m_Config.nResetPin,
        m_Config.nChipSelect, m_Config.nClockSpeed);
    return true;
}

// ═══════════════════════════════════════════════════
// Framebuffer primitives
// ═══════════════════════════════════════════════════

void CDX21Display::ClearFrameBuffer() {
    if (m_pDisplay) m_pDisplay->Clear();
}

void CDX21Display::DrawHLine(int x, int y, int w) {
    if (!m_pDisplay) return;
    for (int dx = 0; dx < w; ++dx) {
        int xi = x + dx;
        if (xi < 0 || xi >= (int)kWidth || y < 0 || y >= (int)kHeight) continue;
        m_pDisplay->SetPixel(xi, y, 1);
    }
}

void CDX21Display::DrawVLine(int x, int y, int h) {
    if (!m_pDisplay) return;
    for (int dy = 0; dy < h; ++dy) {
        int yi = y + dy;
        if (x < 0 || x >= (int)kWidth || yi < 0 || yi >= (int)kHeight) continue;
        m_pDisplay->SetPixel(x, yi, 1);
    }
}

// ═══════════════════════════════════════════════════
// 6x8 text rendering
// ═══════════════════════════════════════════════════

void CDX21Display::DrawText6x8(int x, int y, const char* s, int maxChars,
                                bool inverted) {
    if (!m_pDisplay || !s) return;
    for (int i = 0; i < maxChars && s[i] != '\0'; ++i) {
        char c = s[i];
        if (c < ' ' || c > '~') c = ' ';
        const u8* glyph = Font6x8[c - ' '];
        int gx = x + i * FONT6x8_W;
        for (int col = 0; col < FONT6x8_W; ++col) {
            u8 bits = glyph[col];
            if (inverted) bits = ~bits;
            for (int row = 0; row < FONT6x8_H; ++row) {
                if (bits & (1 << row)) {
                    int px = gx + col;
                    int py = y + row;
                    if (px < (int)kWidth && py < (int)kHeight) {
                        m_pDisplay->SetPixel(px, py, 1);
                    }
                }
            }
        }
    }
}

void CDX21Display::DrawText6x8_Page(int page, const char* s, bool inverted) {
    int y = page * FONT6x8_H;
    DrawText6x8(0, y, s, 21, inverted);  // 21 chars fit in 128 px (21*6=126)
}

void CDX21Display::DrawCursor(int x, int y, int w, int h) {
    // Inverted block (used as text-mode cursor on the original LCD).
    for (int dy = 0; dy < h; ++dy) {
        for (int dx = 0; dx < w; ++dx) {
            int px = x + dx, py = y + dy;
            if (px < (int)kWidth && py < (int)kHeight) {
                m_pDisplay->SetPixel(px, py, 1);
            }
        }
    }
}

// ═══════════════════════════════════════════════════
// 7-segment rendering (8x16 digits)
// ═══════════════════════════════════════════════════

void CDX21Display::DrawGlyph7Seg(int gx, int gy, Glyph g, bool inverted) {
    if (!m_pDisplay) return;
    const u16* col_pat = kFont[g];
    for (int col = 0; col < CHAR_W; ++col) {
        u16 pat = col_pat[col];
        if (inverted) pat = ~pat;
        for (int row = 0; row < CHAR_H; ++row) {
            if (pat & (1u << row)) {
                int px = gx + col;
                int py = gy + row;
                if (px < (int)kWidth && py < (int)kHeight) {
                    m_pDisplay->SetPixel(px, py, 1);
                }
            }
        }
    }
}

void CDX21Display::DrawBigNumber(int x, int y, int value, int minDigits) {
    if (!m_pDisplay) return;
    // Render right-aligned, padding with G_SPACE.
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", value);
    int len = (int)strlen(buf);
    if (len < minDigits) {
        int pad = minDigits - len;
        int total = pad + len;
        if (x + total * CHAR_W > (int)kWidth) {
            // Not enough room; trim leading padding to fit
            int avail = ((int)kWidth - x) / CHAR_W;
            if (avail < 1) return;
            if (total > avail) {
                int drop = total - avail;
                if (drop > pad) drop = pad;
                pad -= drop;
                total -= drop;
            }
        }
        for (int i = 0; i < pad; ++i) {
            DrawGlyph7Seg(x, y, G_SPACE);
            x += CHAR_W;
        }
        for (int i = 0; i < len; ++i) {
            DrawGlyph7Seg(x, y, FromAscii(buf[i]));
            x += CHAR_W;
        }
    } else {
        int total = len;
        if (x + total * CHAR_W > (int)kWidth) {
            int avail = ((int)kWidth - x) / CHAR_W;
            if (avail < 1) return;
            if (total > avail) total = avail;
        }
        for (int i = 0; i < total; ++i) {
            DrawGlyph7Seg(x, y, FromAscii(buf[i]));
            x += CHAR_W;
        }
    }
}

void CDX21Display::DrawBigString(int x, int y, const char* s) {
    if (!m_pDisplay || !s) return;
    while (*s && x + CHAR_W <= (int)kWidth) {
        DrawGlyph7Seg(x, y, FromAscii(*s));
        x += CHAR_W;
        ++s;
    }
}

// ═══════════════════════════════════════════════════
// Per-mode Render() dispatch
// ═══════════════════════════════════════════════════

void CDX21Display::Render() {
    if (!m_pDisplay) return;
    if (!m_bDirty) return;
    m_bDirty = false;

    m_pDisplay->Clear();

    // Page 0 (rows 0-7)  : mode title (16 chars) at left + small status icons
    // Page 1 (rows 8-15) : mode title (16 chars) at right OR big value display
    // Page 2 (rows 16-23): parameter name (16 chars) or voice name
    // Page 3 (rows 24-31): status line / value string

    char line[32];
    const char* title = "";

    switch (m_Mode) {
        case kModePlay: {
            title = MODE_TITLE_PLAY;
            // Page 0: "<n>"   (voice number surrounded by spaces)
            snprintf(line, sizeof(line), "  <%3d>          ", m_VoiceNum);
            DrawText6x8(0, 0, line, 16);
            // Page 1: voice name (big, 7-seg-ish would be overkill for text)
            char big[17];
            snprintf(big, sizeof(big), "%-16.16s", m_VoiceName ? m_VoiceName : "");
            DrawText6x8(0, 8, big, 16, m_bCompare);  // invert if compare
            // Page 2: bank group
            snprintf(line, sizeof(line), "A1-A8           ");
            DrawText6x8(0, 16, line, 16);
            // Page 3: play-mode label / status
            if (m_Status) {
                snprintf(line, sizeof(line), "%-16.16s", m_Status);
            } else {
                snprintf(line, sizeof(line), "PLAY SINGLE     ");
            }
            DrawText6x8(0, 24, line, 16);
            break;
        }

        case kModeEdit: {
            title = MODE_TITLE_EDIT;
            // Page 0: "EDIT" + param index (e.g. "EDIT 1/36")
            snprintf(line, sizeof(line), "%-9s %2d/%-2d     ",
                     title, m_ParamIdx + 1, EDIT_PARAM_COUNT);
            DrawText6x8(0, 0, line, 16);
            // Page 1: big value (right-aligned in 7-seg)
            DrawBigNumber(0, 8, m_Value >= 0 ? m_Value : 0, 3);
            // Page 2: parameter name
            const char* pname = (m_ParamIdx >= 0 && m_ParamIdx < EDIT_PARAM_COUNT)
                ? EDIT_PARAM_NAMES[m_ParamIdx]
                : "                 ";
            DrawText6x8(0, 16, pname, 16);
            // Page 3: small status / unit / value-as-string
            if (m_ValueStr) {
                snprintf(line, sizeof(line), "%-16.16s", m_ValueStr);
            } else if (m_Status) {
                snprintf(line, sizeof(line), "%-16.16s", m_Status);
            } else {
                snprintf(line, sizeof(line), "                ");
            }
            DrawText6x8(0, 24, line, 16);
            break;
        }

        case kModePerformance: {
            title = MODE_TITLE_PERFORMANCE;
            // Page 0: "PERFORM" + voice A/B
            snprintf(line, sizeof(line), "%-9s %3d      ", title, m_VoiceNum);
            DrawText6x8(0, 0, line, 16);
            // Page 1: voice name in big (8x16) font, or 'Buff' if compare
            if (m_bCompare) {
                DrawBigString(0, 8, "BUFF");
            } else {
                DrawBigString(0, 8, m_VoiceName ? m_VoiceName : "");
            }
            // Page 2: performance label
            const char* pname = (m_ParamIdx >= 0 && m_ParamIdx < PLAY_LABEL_COUNT)
                ? PLAY_LABELS[m_ParamIdx]
                : "                ";
            DrawText6x8(0, 16, pname, 16);
            // Page 3: status
            if (m_Status) {
                snprintf(line, sizeof(line), "%-16.16s", m_Status);
            } else {
                snprintf(line, sizeof(line), "A1-A8           ");
            }
            DrawText6x8(0, 24, line, 16);
            break;
        }

        case kModeFunction: {
            title = MODE_TITLE_FUNCTION;
            // Page 0: "FUNCTION  n/46"
            snprintf(line, sizeof(line), "%-9s %2d/%-2d    ",
                     title, m_ParamIdx + 1, FUNCTION_COUNT);
            DrawText6x8(0, 0, line, 16);
            // Page 1: function name in big
            const char* fname = (m_ParamIdx >= 0 && m_ParamIdx < FUNCTION_COUNT)
                ? FUNCTION_NAMES[m_ParamIdx]
                : "                ";
            DrawBigString(0, 8, fname);
            // Page 2: value or ON/OFF
            if (m_ValueStr) {
                snprintf(line, sizeof(line), "%-16.16s", m_ValueStr);
            } else if (m_Value >= 0) {
                snprintf(line, sizeof(line), "=%-15d", m_Value);
            } else {
                snprintf(line, sizeof(line), "                ");
            }
            DrawText6x8(0, 16, line, 16);
            // Page 3: status
            if (m_Status) {
                snprintf(line, sizeof(line), "%-16.16s", m_Status);
            } else {
                snprintf(line, sizeof(line), "                ");
            }
            DrawText6x8(0, 24, line, 16);
            break;
        }

        case kModeMemory: {
            title = MODE_TITLE_MEMORY;
            snprintf(line, sizeof(line), "%-9s %2d      ", title, m_ParamIdx);
            DrawText6x8(0, 0, line, 16);
            // Big value: bank index 1..4
            DrawBigString(0, 8, m_ValueStr ? m_ValueStr : "TAPE");
            // Page 2: tape dialog
            const char* tname = (m_ParamIdx >= 0 && m_ParamIdx < TAPE_LABEL_COUNT)
                ? TAPE_LABELS[m_ParamIdx]
                : "                ";
            DrawText6x8(0, 16, tname, 16);
            // Page 3: status
            if (m_Status) {
                snprintf(line, sizeof(line), "%-16.16s", m_Status);
            } else {
                snprintf(line, sizeof(line), "                ");
            }
            DrawText6x8(0, 24, line, 16);
            break;
        }

        default: {
            snprintf(line, sizeof(line), "DX21  MODE %d   ", (int)m_Mode);
            DrawText6x8(0, 0, line, 16);
            DrawText6x8(0, 8, "                ", 16);
            DrawText6x8(0, 16, "                ", 16);
            DrawText6x8(0, 24, "                ", 16);
            break;
        }
    }

    // Memory-protect indicator (top-right corner, 2x2 block at (124,0))
    if (m_bMemProt) {
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx)
                m_pDisplay->SetPixel(124 + dx, dy, 1);
    }

    // Compare overlay marker (top-right, 4x2)
    if (m_bCompare) {
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 4; ++dx)
                m_pDisplay->SetPixel(120 + dx, dy, 1);
    }

    m_pDisplay->Show();
}
