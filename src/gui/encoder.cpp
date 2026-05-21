#include "encoder.h"
#include <circle/synchronize.h>
#include <atomic>

// ------------------------------------------------------------
// Encoder integration for LVGL
// ------------------------------------------------------------
// Flow:
// 1. Hardware (KY040) detects encoder rotation/button press
// 2. DisplayManager.OnEncoderEvent() calls encoder_hw_delta() / encoder_hw_button()
// 3. These update the global state (g_encoder_diff, g_encoder_pressed)
// 4. LVGL calls encoder_read_cb() periodically to read the state
// 5. The encoder input device sends the data to the focused widget
//
// The encoder must be assigned to a LVGL group (done in GUIController::Init)
// and widgets must be added to that group to receive encoder input.
// ------------------------------------------------------------

// ------------------------------------------------------------
// Globale Encoder-Zustände (werden von LVGL abgefragt)
// Written from GPIO/timer IRQ on Core 0, read by LVGL poll
// on Core 2 — atomare Operationen für Cross-Core-Race-Safety
// ------------------------------------------------------------

static std::atomic<uint32_t> g_last_key{0};
static std::atomic<bool>     g_key_pending{false};

static std::atomic<bool>     g_block_key_left{false};
static std::atomic<bool>     g_block_key_right{false};

// ------------------------------------------------------------
// Von deiner Hardware aufzurufen (Core 0 / IRQ)
// ------------------------------------------------------------
void encoder_hw_delta(int delta)
{
    if (delta > 0) {
        g_last_key.store(LV_KEY_RIGHT, std::memory_order_release);
        g_key_pending.store(true, std::memory_order_release);
    } else if (delta < 0) {
        g_last_key.store(LV_KEY_LEFT, std::memory_order_release);
        g_key_pending.store(true, std::memory_order_release);
    }
}

void encoder_hw_click()
{
    g_last_key.store(LV_KEY_ENTER, std::memory_order_release);
    g_key_pending.store(true, std::memory_order_release);
}

void encoder_block_left(bool block)
{
    g_block_key_left.store(block, std::memory_order_release);
}

bool get_encoder_block_left()
{
    return g_block_key_left.load(std::memory_order_acquire);
}

void encoder_block_right(bool block)
{
    g_block_key_right.store(block, std::memory_order_release);
}

bool get_encoder_block_right()
{
    return g_block_key_right.load(std::memory_order_acquire);
}

// ------------------------------------------------------------
// LVGL read callback (Core 2)
// ------------------------------------------------------------
static void encoder_read_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    // Atomar read-and-clear: verhindert, dass ein IRQ auf Core 0
    // g_last_key überschreibt, während wir lesen.
    bool pending = g_key_pending.exchange(false, std::memory_order_acq_rel);
    uint32_t key = g_last_key.load(std::memory_order_acquire);

    if (pending) {
        data->key = key;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ------------------------------------------------------------
// LVGL-Indev erzeugen
// ------------------------------------------------------------
lv_indev_t* encoder_init_lvgl()
{
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, encoder_read_cb);
    return indev;
}
