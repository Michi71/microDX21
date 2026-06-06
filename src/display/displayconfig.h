//
// displayconfig.h – Hardware configuration for the OLED + encoder
//
// All pins are BCM numbering (Brcm). The struct is filled from
// /boot/microdx21.ini at boot by CConfig::Load().
//

#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H

#include <circle/types.h>

struct DisplayConfig
{
    // ───────────────────────────────────────────────
    // Controller Type
    // ───────────────────────────────────────────────
    enum class Controller
    {
        None,
        SH1106,
        SSD1306,
        SSD1305,
        Custom
    };

    // ───────────────────────────────────────────────
    // Bus Type
    // ───────────────────────────────────────────────
    enum class Bus
    {
        None,
        I2C,
        SPI,
        Auto = SPI  // deprecated alias: "Auto" used to mean "auto-detect"; now same as SPI
    };

    struct Resolution
    {
        unsigned width  = 128;
        unsigned height = 32;
    };

    Controller controller = Controller::None;
    Bus        bus        = Bus::None;

    Resolution resolution;

    // I2C
    u8 i2cAddress = 0x3C;

    // SPI
    unsigned spiBus   = 0;     // SPI0, SPI1
    unsigned spiMode  = 0;     // SPI Mode 0..3 (CPOL << 1 | CPHA)
    unsigned pinDC    = 0;     // Data/Command
    unsigned pinReset = 0;     // Optional, set to GPIO_PINS for "no reset line"
    unsigned spiSpeed = 8000000; // 8 MHz default

    // Rotary encoder (KY-040, 3 pins, BCM numbering)
    unsigned encPinA   = 0;
    unsigned encPinB   = 0;
    unsigned encPinBtn = 0;

    // Master enable
    bool enabled = true;
};

#endif // DISPLAY_CONFIG_H
