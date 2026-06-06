// 7seg-font: we use C99 array designators for readability, suppress clang/gcc warning

// ============================================================================
// dx21_ui_7seg.h
//
// 7-segment style 8x16 font for big value display on 128x32 OLED.
// Evokes the original DX21 LED look.
//
// Layout per glyph (8 columns wide, 16 rows tall). Each column is stored as
// a u16, bit i = 1 means row i is lit. 0xFFFF = solid block, 0 = empty.
//
// 7-seg layout (8 cols x 16 rows, single digit takes whole cell):
//
//     col:  0  1  2  3  4  5  6  7
//     row 0-1:      [a, top horiz]
//     row 0-6:  [f]  ........  [b]
//     row 6-7:      [g, mid horiz]
//     row 8-14: [e]  ........  [c]
//     row 14-15:    [d, bot horiz]
//
// All segments are 2 cols wide where they are vertical, and 4 cols wide
// where horizontal. col 7 is unused (blank gutter).
// ============================================================================

#ifndef DX21_UI_7SEG_H
#define DX21_UI_7SEG_H

#include <circle/types.h>

namespace DX21UI7Seg {

constexpr int CHAR_W = 8;
constexpr int CHAR_H = 16;

// Segment masks (per single column, replicated 2x for 2-col-wide verticals).
constexpr u16 SEG_A  = 0x0003;  // rows 0-1   (top horiz)
constexpr u16 SEG_F  = 0x007F;  // rows 0-6   (UL vert)
constexpr u16 SEG_B  = SEG_F;   // rows 0-6   (UR vert)
constexpr u16 SEG_GT = 0x00C0;  // rows 6-7   (mid horiz, top half-cell)
constexpr u16 SEG_GB = 0x0300;  // rows 8-9   (mid horiz, bot half-cell)
constexpr u16 SEG_E  = 0x7F00;  // rows 8-14  (LL vert)
constexpr u16 SEG_C  = SEG_E;   // rows 8-14  (LR vert)
constexpr u16 SEG_D  = 0xC000;  // rows 14-15 (bot horiz)

constexpr u16 Seg(int a, int f, int b, int g, int e, int c, int d) {
    u16 r = 0;
    if (a) r |= SEG_A;
    if (f) r |= SEG_F;
    if (b) r |= SEG_B;
    if (g) r |= SEG_GT | SEG_GB;
    if (e) r |= SEG_E;
    if (c) r |= SEG_C;
    if (d) r |= SEG_D;
    return r;
}

// Glyph indices.
enum Glyph : u8 {
    G_SPACE = 0,
    G_0, G_1, G_2, G_3, G_4, G_5, G_6, G_7, G_8, G_9,
    G_A, G_B, G_C, G_D, G_E, G_F, G_G, G_H, G_I, G_J,
    G_K, G_L, G_M, G_N, G_O, G_P, G_Q, G_R, G_S, G_T,
    G_U, G_V, G_W, G_X, G_Y, G_Z,
    G_MINUS, G_PLUS, G_DOT, G_COLON, G_EQ, G_QMARK,
    G_SLASH, G_PERCENT, G_DEG, G_LT, G_GT, G_LBRACK, G_RBRACK,
    G_UNDERSC, G_FULL, G_TICK, G_BAR, G_BAR_T, G_BAR_B,
    G_ARROW_L, G_ARROW_R, G_UP, G_DN, G_STAR,
    G_COUNT
};

// 8 columns per glyph. Each column is a u16 row pattern.
// col 0,1 = left vertical  (segment f top, e bot)
// col 2,3 = center of horizontal (a top, d bot)
// col 4,5 = right vertical (segment b top, c bot)
// col 6   = middle bar (g)
// col 7   = blank gutter
constexpr u16 kFont[G_COUNT][CHAR_W] = {
    // G_SPACE
    [G_SPACE] = { 0,0,0,0,0,0,0,0 },

    // ───── DIGITS 0..9 ─────
    [G_0] = {                                                // a b c d e f
        Seg(0,1,0,0,1,0,0), Seg(0,1,0,0,1,0,0),              // cols 0,1  f,e
        Seg(1,0,0,0,0,0,0), Seg(1,0,0,0,0,0,0),              // cols 2,3  a
        Seg(0,0,1,0,0,1,0), Seg(0,0,1,0,0,1,0),              // cols 4,5  b,c
        0, 0
    },
    [G_1] = {                                                //   b c
        0, 0,
        0, 0,
        Seg(0,0,1,0,0,1,0), Seg(0,0,1,0,0,1,0),              // cols 4,5  b,c
        0, 0
    },
    [G_2] = {                                                // a b d e g
        Seg(0,0,0,0,1,0,0), Seg(0,0,0,0,1,0,0),              // cols 0,1  e
        Seg(1,0,0,0,0,0,0), Seg(1,0,0,0,0,0,0),              // cols 2,3  a
        Seg(0,0,1,0,0,0,0), Seg(0,0,1,0,0,0,0),              // cols 4,5  b
        Seg(0,0,0,1,0,0,0),                                  // col 6    g
        0
    },
    [G_3] = {                                                // a b c d g
        0, 0,
        Seg(1,0,0,0,0,0,0), Seg(1,0,0,0,0,0,0),              // cols 2,3  a
        Seg(0,0,1,0,0,1,0), Seg(0,0,1,0,0,1,0),              // cols 4,5  b,c
        Seg(0,0,0,1,0,0,0),                                  // col 6    g
        0
    },
    [G_4] = {                                                // b c f g
        Seg(0,1,0,0,0,0,0), Seg(0,1,0,0,0,0,0),              // cols 0,1  f
        0, 0,
        Seg(0,0,1,0,0,1,0), Seg(0,0,1,0,0,1,0),              // cols 4,5  b,c
        Seg(0,0,0,1,0,0,0),                                  // col 6    g
        0
    },
    [G_5] = {                                                // a c d f g
        Seg(0,1,0,0,0,0,0), Seg(0,1,0,0,0,0,0),              // cols 0,1  f
        Seg(1,0,0,0,0,0,0), Seg(1,0,0,0,0,0,0),              // cols 2,3  a
        0, 0,
        Seg(0,0,0,1,0,0,0),                                  // col 6    g
        0
    },
    [G_6] = {                                                // a c d e f g
        Seg(0,1,0,0,1,0,0), Seg(0,1,0,0,1,0,0),              // cols 0,1  f,e
        Seg(1,0,0,0,0,0,0), Seg(1,0,0,0,0,0,0),              // cols 2,3  a
        0, 0,
        Seg(0,0,0,1,0,0,0),                                  // col 6    g
        0
    },
    [G_7] = {                                                // a b c
        0, 0,
        Seg(1,0,0,0,0,0,0), Seg(1,0,0,0,0,0,0),              // cols 2,3  a
        Seg(0,0,1,0,0,1,0), Seg(0,0,1,0,0,1,0),              // cols 4,5  b,c
        0, 0
    },
    [G_8] = {                                                // a b c d e f g
        Seg(0,1,0,0,1,0,0), Seg(0,1,0,0,1,0,0),              // cols 0,1  f,e
        Seg(1,0,0,0,0,0,0), Seg(1,0,0,0,0,0,0),              // cols 2,3  a
        Seg(0,0,1,0,0,1,0), Seg(0,0,1,0,0,1,0),              // cols 4,5  b,c
        Seg(0,0,0,1,0,0,0),                                  // col 6    g
        0
    },
    [G_9] = {                                                // a b c d f g
        Seg(0,1,0,0,0,0,0), Seg(0,1,0,0,0,0,0),              // cols 0,1  f
        Seg(1,0,0,0,0,0,0), Seg(1,0,0,0,0,0,0),              // cols 2,3  a
        Seg(0,0,1,0,0,1,0), Seg(0,0,1,0,0,1,0),              // cols 4,5  b,c
        Seg(0,0,0,1,0,0,0),                                  // col 6    g
        0
    },

    // ───── LETTERS ─────
    [G_A] = { Seg(0,1,0,0,1,0,0),Seg(0,1,0,0,1,0,0),
              Seg(1,0,0,0,0,0,0),Seg(1,0,0,0,0,0,0),
              Seg(0,0,1,0,0,1,0),Seg(0,0,1,0,0,1,0),
              Seg(0,0,0,1,0,0,0), 0 },                       // a b c e f g
    [G_B] = { Seg(0,1,0,0,1,0,0),Seg(0,1,0,0,1,0,0),        // a d e f g
              0,0,
              Seg(0,0,1,0,0,1,0),Seg(0,0,1,0,0,1,0),
              Seg(0,0,0,1,0,0,1), 0 },
    [G_C] = { Seg(0,1,0,0,1,0,0),Seg(0,1,0,0,1,0,0),        // a d e f
              Seg(1,0,0,0,0,0,0),Seg(1,0,0,0,0,0,0),
              0,0,
              Seg(0,0,0,0,0,0,1), 0 },
    [G_D] = { 0,0,                                          // b c d g
              0,0,
              Seg(0,0,1,0,0,1,0),Seg(0,0,1,0,0,1,0),
              Seg(0,0,0,1,0,0,1), 0 },
    [G_E] = { Seg(0,1,0,0,1,0,0),Seg(0,1,0,0,1,0,0),        // a d e f g
              Seg(1,0,0,0,0,0,0),Seg(1,0,0,0,0,0,0),
              0,0,
              Seg(0,0,0,1,0,0,1), 0 },
    [G_F] = { Seg(0,1,0,0,1,0,0),Seg(0,1,0,0,1,0,0),        // a e f g
              Seg(1,0,0,0,0,0,0),Seg(1,0,0,0,0,0,0),
              0,0,
              Seg(0,0,0,1,0,0,0), 0 },
    [G_G] = { Seg(0,1,0,0,1,0,0),Seg(0,1,0,0,1,0,0),        // a c d e f
              Seg(1,0,0,0,0,0,0),Seg(1,0,0,0,0,0,0),
              0,0,
              Seg(0,0,1,0,0,0,1), 0 },
    [G_H] = { Seg(0,1,0,0,1,0,0),Seg(0,1,0,0,1,0,0),        // b c e f g
              0,0,
              Seg(0,0,1,0,0,1,0),Seg(0,0,1,0,0,1,0),
              Seg(0,0,0,1,0,0,0), 0 },
    [G_I] = { Seg(0,1,0,0,1,0,0),Seg(0,1,0,0,1,0,0),        // e f
              0,0,0,0,0,0 },
    [G_J] = { 0,0,                                          // b c d e
              0,0,
              Seg(0,0,1,0,0,1,0),Seg(0,0,1,0,0,1,0),
              Seg(0,0,0,0,0,0,1), 0 },
    [G_K] = { Seg(0,1,0,0,1,0,0),Seg(0,1,0,0,1,0,0),        // approximation: c e f g
              0,0,
              Seg(0,0,1,0,0,1,0),Seg(0,0,1,0,0,1,0),
              Seg(0,0,0,1,0,0,0), 0 },
    [G_L] = { Seg(0,1,0,0,1,0,0),Seg(0,1,0,0,1,0,0),        // d e f
              0,0,0,0,
              Seg(0,0,0,0,0,0,1), 0 },
    [G_M] = { Seg(0,1,0,0,1,0,0),Seg(0,1,0,0,1,0,0),        // approx: a b e f
              Seg(1,0,0,0,0,0,0),Seg(1,0,0,0,0,0,0),
              Seg(0,0,1,0,0,1,0),Seg(0,0,1,0,0,1,0),
              0, 0 },
    [G_N] = { Seg(0,1,0,0,1,0,0),Seg(0,1,0,0,1,0,0),        // approx: a b e f
              Seg(1,0,0,0,0,0,0),Seg(1,0,0,0,0,0,0),
              Seg(0,0,1,0,0,1,0),Seg(0,0,1,0,0,1,0),
              0, 0 },
    [G_O] = { Seg(0,1,0,0,1,0,0),Seg(0,1,0,0,1,0,0),        // a b c d e f
              Seg(1,0,0,0,0,0,0),Seg(1,0,0,0,0,0,0),
              Seg(0,0,1,0,0,1,0),Seg(0,0,1,0,0,1,0),
              0, 0 },
    [G_P] = { Seg(0,1,0,0,1,0,0),Seg(0,1,0,0,1,0,0),        // a b e f g
              Seg(1,0,0,0,0,0,0),Seg(1,0,0,0,0,0,0),
              Seg(0,0,1,0,0,0,0),Seg(0,0,1,0,0,0,0),
              Seg(0,0,0,1,0,0,0), 0 },
    [G_Q] = { Seg(0,1,0,0,0,0,0),Seg(0,1,0,0,0,0,0),        // a b c d f g
              Seg(1,0,0,0,0,0,0),Seg(1,0,0,0,0,0,0),
              Seg(0,0,1,0,0,1,0),Seg(0,0,1,0,0,1,0),
              Seg(0,0,0,1,0,0,1), 0 },
    [G_R] = { Seg(0,0,0,0,1,0,0),Seg(0,0,0,0,1,0,0),        // e g
              0,0,
              0,0,
              Seg(0,0,0,1,0,0,0), 0 },
    [G_S] = { Seg(0,1,0,0,0,0,0),Seg(0,1,0,0,0,0,0),        // a c d f g (=5)
              Seg(1,0,0,0,0,0,0),Seg(1,0,0,0,0,0,0),
              0,0,
              Seg(0,0,0,1,0,0,1), 0 },
    [G_T] = { Seg(0,1,0,0,0,0,0),Seg(0,1,0,0,0,0,0),        // a b f
              Seg(1,0,0,0,0,0,0),Seg(1,0,0,0,0,0,0),
              0,0,0,0 },
    [G_U] = { Seg(0,1,0,0,1,0,0),Seg(0,1,0,0,1,0,0),        // b c d e f
              0,0,
              Seg(0,0,1,0,0,1,0),Seg(0,0,1,0,0,1,0),
              Seg(0,0,0,0,0,0,1), 0 },
    [G_V] = { Seg(0,1,0,0,1,0,0),Seg(0,1,0,0,1,0,0),        // c e (approx)
              0,0,
              Seg(0,0,1,0,0,1,0),Seg(0,0,1,0,0,1,0),
              0, 0 },
    [G_W] = { Seg(0,1,0,0,1,0,0),Seg(0,1,0,0,1,0,0),        // approx same as V
              0,0,
              Seg(0,0,1,0,0,1,0),Seg(0,0,1,0,0,1,0),
              0, 0 },
    [G_X] = { Seg(0,1,0,0,1,0,0),Seg(0,1,0,0,1,0,0),        // same approximation
              0,0,
              Seg(0,0,1,0,0,1,0),Seg(0,0,1,0,0,1,0),
              0, 0 },
    [G_Y] = { Seg(0,1,0,0,0,0,0),Seg(0,1,0,0,0,0,0),        // b c d f g
              0,0,
              Seg(0,0,1,0,0,1,0),Seg(0,0,1,0,0,1,0),
              Seg(0,0,0,1,0,0,1), 0 },
    [G_Z] = { Seg(0,0,0,0,1,0,0),Seg(0,0,0,0,1,0,0),        // a d e
              Seg(1,0,0,0,0,0,0),Seg(1,0,0,0,0,0,0),
              0,0,
              Seg(0,0,0,0,0,0,1), 0 },

    // ───── SYMBOLS ─────
    [G_MINUS] = { 0,0,0,0,                                  // g only
                  Seg(0,0,0,1,0,0,0), 0,0,0 },
    [G_PLUS] = { 0,0,                                        // g only
                 Seg(0,0,0,1,0,0,0),
                 0,0,
                 Seg(0,0,0,1,0,0,0),
                 0,0 },
    [G_DOT] = { 0,0,0,0,0,0,                                // d only
                Seg(0,0,0,0,0,0,1), 0 },
    [G_COLON] = { 0,0,                                       // g+d
                  Seg(0,0,0,1,0,0,0),
                  0,0,
                  Seg(0,0,0,0,0,0,1),
                  0,0 },
    [G_EQ] = { 0, 0, 0, 0, Seg(0,0,0,1,0,0,0), 0, 0, Seg(0,0,0,1,0,0,0) },
    [G_QMARK] = { 0, 0, Seg(1,0,0,0,0,0,0), Seg(1,0,0,0,0,0,0), Seg(0,0,1,0,0,0,0), Seg(0,0,1,0,0,0,0), Seg(0,0,0,1,0,0,0), Seg(0,0,0,1,0,0,0) },
    [G_SLASH] = { 0,0,0,0,                                   // b only
                  Seg(0,0,1,0,0,0,0),Seg(0,0,1,0,0,0,0),
                  0,0 },
    [G_PERCENT] = { 0,0,0,0,0,0,0,0 },
    [G_DEG] = { 0,0,0,0,0,0,0,0 },
    [G_LT] = { 0,0,0,0,0,0,0,0 },
    [G_GT] = { 0,0,0,0,0,0,0,0 },
    [G_LBRACK] = { 0,0,0,0,0,0,0,0 },
    [G_RBRACK] = { 0,0,0,0,0,0,0,0 },
    [G_UNDERSC] = { 0,0,0,0,0,0,                            // d only
                    Seg(0,0,0,0,0,0,1), 0 },
    [G_FULL] = { 0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF },
    [G_TICK] = { 0,0,0,0,0,0,0,0 },
    [G_BAR] = { 0,0,0,0,                                    // g
                Seg(0,0,0,1,0,0,0), 0,0,0 },
    [G_BAR_T] = { Seg(1,0,0,0,0,0,0),Seg(1,0,0,0,0,0,0),    // a
                 0,0,0,0,0,0 },
    [G_BAR_B] = { 0,0,0,0,0,0,                               // d
                  Seg(0,0,0,0,0,0,1), 0 },
    [G_ARROW_L] = { 0, 0, Seg(0,0,1,0,0,0,0), Seg(0,1,1,1,0,0,0), Seg(0,0,1,0,0,0,0), 0, 0, 0 },
    [G_ARROW_R] = { 0, 0, Seg(0,0,0,0,0,1,0), Seg(0,0,0,1,1,1,0), Seg(0,0,0,0,0,1,0), 0, 0, 0 },
    [G_UP] = { 0,0,                                          // b g b
               Seg(0,0,1,0,0,0,0),
               Seg(0,1,0,1,0,0,0),
               0,0,0,0 },
    [G_DN] = { 0,0,0,0,                                      // g
               Seg(0,1,0,1,0,0,0),
               Seg(0,0,1,0,0,0,0),
               0,0 },
    [G_STAR] = { 0,0,0,0,0,0,0,0 },
};

// Map ASCII char to glyph index. Returns G_SPACE for unsupported chars.
inline Glyph FromAscii(char c) {
    if (c >= '0' && c <= '9') return (Glyph)(G_0 + (c - '0'));
    if (c >= 'A' && c <= 'Z') return (Glyph)(G_A + (c - 'A'));
    if (c >= 'a' && c <= 'z') return (Glyph)(G_A + (c - 'a'));
    switch (c) {
        case ' ':  return G_SPACE;
        case '-':  return G_MINUS;
        case '+':  return G_PLUS;
        case '.':  return G_DOT;
        case ':':  return G_COLON;
        case '=':  return G_EQ;
        case '?':  return G_QMARK;
        case '/':  return G_SLASH;
        case '%':  return G_PERCENT;
        case '<':  return G_LT;
        case '>':  return G_GT;
        case '[':  return G_LBRACK;
        case ']':  return G_RBRACK;
        case '_':  return G_UNDERSC;
        case 0x7F: return G_FULL;
        case '|':  return G_BAR;
    }
    return G_SPACE;
}

} // namespace DX21UI7Seg

#endif // DX21_UI_7SEG_H
