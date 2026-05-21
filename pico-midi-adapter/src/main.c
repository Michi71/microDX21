/*
 * USB to serial MIDI adapter with SysEx buffering
 * For VelvetKeys — RP2350 Mini
 *
 * Adapted from the TinyUSB midi_test example by Rene Stange
 *
 * SysEx support added for VelvetKeys web configurator:
 *   Browser → WebMIDI → USB → [Pico] → UART → RPi (VelvetKeys)
 *   RPi (VelvetKeys) → UART → [Pico] → USB → WebMIDI → Browser
 *
 * Strategy: SysEx messages (F0...F7) are buffered completely before
 * forwarding. Normal MIDI (NoteOn, CC, etc.) is passed through immediately.
 * A non-blocking UART TX ring buffer prevents stalling the USB task.
 */

/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "bsp/board.h"
#include "tusb.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

#define WS2812_PIN      16
#define IS_RGBW         false

// Maximum single SysEx message size.
//
// USB → UART direction (s_u2s_buf) sees the LARGEST messages:
//   PUT_PRESET sends an entire preset JSON in one un-chunked SysEx,
//   base64-encoded.  A 36-parameter preset with metadata serialises
//   to ~700–1000 bytes of UTF-8, ~1.0–1.4 KB after base64 + framing.
//   The previous 512-byte limit silently discarded such messages,
//   making "Save Preset" in the web configurator a no-op.
//
// UART → USB direction (s_s2u_buf) only carries the synth's chunked
//   responses (≤209 bytes per chunk), so the same buffer easily fits
//   them with massive headroom.
//
// 8 KiB covers any plausible preset size.
#define SYSEX_BUF_SIZE  8192

// Non-blocking UART TX ring buffer.
// Sized to hold a full PUT_PRESET burst (~1.4 KB) plus a few queued
// CC / note messages without back-pressuring tx_push().
#define UART_TX_BUF     8192

// SysEx timeout: if F7 is not received within this many ms after F0,
// the incomplete message is discarded (guards against USB glitches).
#define SYSEX_TIMEOUT_MS  2000

// ---------------------------------------------------------------------------
// LED state
// ---------------------------------------------------------------------------

typedef enum {
    LED_OFF   = 0,
    LED_MIDI  = 1,   // Normal MIDI activity  → red
    LED_SYSEX = 2,   // SysEx activity        → purple
} LedMode;

static LedMode  s_led_mode     = LED_OFF;
static uint32_t s_led_last_ms  = 0;
static bool     s_led_on       = false;
static LedMode  s_led_shown    = LED_OFF;

// ---------------------------------------------------------------------------
// UART TX ring buffer
// ---------------------------------------------------------------------------

static uint8_t  s_tx_buf[UART_TX_BUF];
static uint32_t s_tx_head = 0;   // write index
static uint32_t s_tx_tail = 0;   // read index

static inline bool     tx_empty(void) { return s_tx_head == s_tx_tail; }
static inline uint32_t tx_used(void)  { return (s_tx_head - s_tx_tail + UART_TX_BUF) % UART_TX_BUF; }

// Push bytes into ring buffer; silently drops on overflow (shouldn't happen
// with a 2 KB buffer and ≤512 byte SysEx chunks).
static void tx_push(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uint32_t next = (s_tx_head + 1) % UART_TX_BUF;
        if (next != s_tx_tail) {          // not full
            s_tx_buf[s_tx_head] = data[i];
            s_tx_head = next;
        }
    }
}

// Drain ring buffer into UART hardware FIFO (non-blocking).
static void uart_tx_task(void)
{
    while (!tx_empty() && uart_is_writable(uart0)) {
        uart_putc_raw(uart0, s_tx_buf[s_tx_tail]);
        s_tx_tail = (s_tx_tail + 1) % UART_TX_BUF;
    }
}

// ---------------------------------------------------------------------------
// SysEx state — USB → UART direction
// ---------------------------------------------------------------------------

static uint8_t  s_u2s_buf[SYSEX_BUF_SIZE];  // accumulation buffer
static uint32_t s_u2s_len     = 0;
static bool     s_u2s_in_sx   = false;
static uint32_t s_u2s_start_ms = 0;          // timestamp of F0

// ---------------------------------------------------------------------------
// SysEx state — UART → USB direction
// ---------------------------------------------------------------------------

static uint8_t  s_s2u_buf[SYSEX_BUF_SIZE];
static uint32_t s_s2u_len     = 0;
static bool     s_s2u_in_sx   = false;
static uint32_t s_s2u_start_ms = 0;

// ---------------------------------------------------------------------------
// UART RX helper
// ---------------------------------------------------------------------------

static size_t uart_rx_read(uint8_t *dst, size_t maxlen)
{
    size_t n = 0;
    while (n < maxlen && uart_is_readable(uart0))
        dst[n++] = uart_getc(uart0);
    return n;
}

// ---------------------------------------------------------------------------
// WS2812 LED helpers
// ---------------------------------------------------------------------------

static void put_pixel(uint32_t pixel_grb)
{
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}

static void put_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    put_pixel(((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
}

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

void midi_task(void);
void led_task(void);

// ---------------------------------------------------------------------------
// MAIN
// ---------------------------------------------------------------------------

int main(void)
{
    board_init();
    tusb_init();

    // UART0 at MIDI standard baud rate (31250)
    uart_init(uart0, 31250);
    gpio_set_function(0, GPIO_FUNC_UART);   // GPIO0 = TX
    gpio_set_function(1, GPIO_FUNC_UART);   // GPIO1 = RX

    // WS2812 NeoPixel via PIO state machine 0
    uint offset = pio_add_program(pio0, &ws2812_program);
    ws2812_program_init(pio0, 0, offset, WS2812_PIN, 800000, IS_RGBW);

    while (1) {
        tud_task();       // TinyUSB device task — must be called frequently
        uart_tx_task();   // Drain UART TX ring buffer (non-blocking)
        midi_task();      // Bridge USB ↔ UART
        led_task();       // Update WS2812 LED
    }

    return 0;
}

// ---------------------------------------------------------------------------
// TinyUSB device callbacks
// ---------------------------------------------------------------------------

void tud_mount_cb(void)   {}
void tud_umount_cb(void)  { s_led_mode = LED_OFF; }
void tud_suspend_cb(bool remote_wakeup_en) { (void)remote_wakeup_en; }
void tud_resume_cb(void)  {}

// ---------------------------------------------------------------------------
// MIDI Task — bidirectional USB ↔ UART bridge with SysEx buffering
// ---------------------------------------------------------------------------

void midi_task(void)
{
    if (!tud_midi_mounted())
        return;

    uint32_t now = board_millis();

    // -----------------------------------------------------------------------
    // Direction: USB → UART
    //
    // tud_midi_stream_read() returns raw MIDI bytes (F0..F7 for SysEx).
    // Normal MIDI bytes are pushed immediately into the UART TX ring buffer.
    // SysEx bytes are buffered until the closing F7, then pushed as one block.
    // -----------------------------------------------------------------------
    {
        uint8_t raw[128];
        uint32_t n = tud_midi_stream_read(raw, sizeof(raw));

        // Timeout guard: discard incomplete SysEx older than SYSEX_TIMEOUT_MS
        if (s_u2s_in_sx && (now - s_u2s_start_ms) > SYSEX_TIMEOUT_MS) {
            s_u2s_in_sx  = false;
            s_u2s_len    = 0;
        }

        for (uint32_t i = 0; i < n; i++) {
            uint8_t b = raw[i];

            if (b == 0xF0) {                    // SysEx start
                s_u2s_in_sx    = true;
                s_u2s_len      = 0;
                s_u2s_start_ms = now;
            }

            if (s_u2s_in_sx) {
                if (s_u2s_len < SYSEX_BUF_SIZE) {
                    s_u2s_buf[s_u2s_len++] = b;
                } else {
                    // Buffer overflow — discard this message
                    s_u2s_in_sx = false;
                    s_u2s_len   = 0;
                    // Note: b might be F7; don't fall through to else branch
                    continue;
                }

                if (b == 0xF7) {                // SysEx end — forward complete message
                    tx_push(s_u2s_buf, s_u2s_len);
                    s_u2s_in_sx = false;
                    s_u2s_len   = 0;
                    s_led_mode  = LED_SYSEX;
                    s_led_last_ms = now;
                }
            } else {
                // Normal MIDI byte — pass through immediately
                tx_push(&b, 1);
                if (s_led_mode != LED_SYSEX) {
                    s_led_mode    = LED_MIDI;
                    s_led_last_ms = now;
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Direction: UART → USB
    //
    // Raw MIDI bytes arrive from VelvetKeys over UART.
    // Normal MIDI is written directly to the USB MIDI stream.
    // SysEx bytes are buffered until F7, then written as one complete block
    // so that tud_midi_stream_write() creates well-formed USB MIDI packets.
    // -----------------------------------------------------------------------
    {
        uint8_t raw[64];
        size_t n = uart_rx_read(raw, sizeof(raw));

        // Timeout guard for incomplete SysEx from UART side
        if (s_s2u_in_sx && (now - s_s2u_start_ms) > SYSEX_TIMEOUT_MS) {
            s_s2u_in_sx = false;
            s_s2u_len   = 0;
        }

        for (size_t i = 0; i < n; i++) {
            uint8_t b = raw[i];

            if (b == 0xF0) {                    // SysEx start
                s_s2u_in_sx    = true;
                s_s2u_len      = 0;
                s_s2u_start_ms = now;
            }

            if (s_s2u_in_sx) {
                if (s_s2u_len < SYSEX_BUF_SIZE) {
                    s_s2u_buf[s_s2u_len++] = b;
                } else {
                    // Buffer overflow — discard
                    s_s2u_in_sx = false;
                    s_s2u_len   = 0;
                    continue;
                }

                if (b == 0xF7) {                // SysEx end — write complete block to USB
                    // tud_midi_stream_write packetises raw MIDI bytes into
                    // USB MIDI packets (CIN 0x04/0x05/0x06/0x07) automatically.
                    // CFG_TUD_MIDI_TX_BUFSIZE (512) holds the full chunk.
                    tud_midi_stream_write(0, s_s2u_buf, s_s2u_len);
                    s_s2u_in_sx   = false;
                    s_s2u_len     = 0;
                    s_led_mode    = LED_SYSEX;
                    s_led_last_ms = now;
                }
            } else {
                // Normal MIDI byte — write immediately
                tud_midi_stream_write(0, &b, 1);
                if (s_led_mode != LED_SYSEX) {
                    s_led_mode    = LED_MIDI;
                    s_led_last_ms = now;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// LED Task — WS2812 status indicator
//
//   Red    (0x40, 0x00, 0x00) — normal MIDI activity
//   Purple (0x40, 0x00, 0x40) — SysEx activity
//   Off                       — idle
//
// LED stays on for 50 ms after the last activity, then goes off.
// ---------------------------------------------------------------------------

void led_task(void)
{
    bool active = (s_led_mode != LED_OFF) &&
                  ((board_millis() - s_led_last_ms) < 50);

    if (active) {
        if (!s_led_on || s_led_shown != s_led_mode) {
            if (s_led_mode == LED_SYSEX)
                put_rgb(0x40, 0x00, 0x40);   // Purple: SysEx
            else
                put_rgb(0x40, 0x00, 0x00);   // Red: normal MIDI
            s_led_on    = true;
            s_led_shown = s_led_mode;
        }
    } else {
        if (s_led_on) {
            put_rgb(0x00, 0x00, 0x00);
            s_led_on   = false;
            s_led_mode = LED_OFF;
        }
    }
}
