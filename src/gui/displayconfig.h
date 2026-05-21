#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H

#include <circle/types.h>
#include <string>

struct DisplayConfig
{
    // ───────────────────────────────────────────────
    // Display Controller
    // ───────────────────────────────────────────────
    enum class Controller
    {
        None,
        Auto,
        SH1106,
        SSD1306,
        SSD1305,
        ST7789,
        HDMI,
        EPD2IN13V4,
        Custom
    };

    // ───────────────────────────────────────────────
    // Bus Type
    // ───────────────────────────────────────────────
    enum class Bus
    {
        None,       
        Auto,
        I2C,
        SPI,
        HDMI
    };

    // ───────────────────────────────────────────────
    // Resolution
    // ───────────────────────────────────────────────
    struct Resolution
    {
        unsigned width  = 128;
        unsigned height = 64;
    };

    // ───────────────────────────────────────────────
    // Config Fields
    // ───────────────────────────────────────────────
    Controller controller = Controller::None;
    Bus        bus        = Bus::None;

    Resolution resolution;

    // I2C
    u8 i2cAddress = 0x3C;

    // SPI
    unsigned spiBus   = 0;     // SPI0, SPI1 …
    unsigned spiMode  = 0;     // SPI Mode
    unsigned pinDC    = 0;     // Data/Command
    unsigned pinReset = 0;     // Optional
    unsigned spiSpeed = 8000000; // 8 MHz default

    // Rotation (0, 90, 180, 270)
    unsigned rotation = 0;

    // Encoder Pins
    unsigned encPinA   = 0;
    unsigned encPinB   = 0;
    unsigned encPinBtn = 0;

    // EPD Pins (für EPD2in13V4)
    unsigned epdResetPin = 0;
    unsigned epdBusyPin  = 0;
    unsigned epdChipSelect = 0;

    // Touch Pins (für GT1151)
    unsigned touchResetPin = 0;
    unsigned touchIRQPin   = 0;
    unsigned touchI2CAddress = 0x14;

    // Optional: Display enabled?
    bool enabled = true;
};

#endif // DISPLAY_CONFIG_H
