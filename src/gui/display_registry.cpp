#include "display_registry.h"
#include <epd2in13v4display.h>

static bool s_Registered = [](){

    auto& reg = DisplayRegistry::Instance();

    // ───────────────────────────────────────────────
    // I2C: SH1106
    // ───────────────────────────────────────────────
    reg.Register(DisplayConfig::Bus::I2C,
                 DisplayConfig::Controller::SH1106,
                 [](const DisplayConfig& cfg, CI2CMaster* pI2C, CSPIMaster*) {
                     return new CSH1106Display(
                         pI2C,
                         cfg.resolution.width,
                         cfg.resolution.height,
                         cfg.i2cAddress,
                         400000
                     );
                 });


    // ───────────────────────────────────────────────
    // I2C: SSD1305
    // ───────────────────────────────────────────────
    reg.Register(DisplayConfig::Bus::I2C,
                 DisplayConfig::Controller::SSD1306,
                 [](const DisplayConfig& cfg, CI2CMaster* pI2C, CSPIMaster*) {
                     return new CSSD1305Display(
                         pI2C,
                         cfg.resolution.width,
                         cfg.resolution.height,
                         cfg.i2cAddress,
                         400000
                     );
                 });

    // ───────────────────────────────────────────────
    // I2C: SSD1306
    // ───────────────────────────────────────────────
    reg.Register(DisplayConfig::Bus::I2C,
                 DisplayConfig::Controller::SSD1306,
                 [](const DisplayConfig& cfg, CI2CMaster* pI2C, CSPIMaster*) {
                     return new CSSD1306Display(
                         pI2C,
                         cfg.resolution.width,
                         cfg.resolution.height,
                         cfg.i2cAddress,
                         400000
                     );
                 });

    // ───────────────────────────────────────────────
    // SPI: SH1106
    // ───────────────────────────────────────────────
    reg.Register(DisplayConfig::Bus::SPI,
                 DisplayConfig::Controller::SH1106,
                 [](const DisplayConfig& cfg, CI2CMaster*, CSPIMaster* pSPI) {
                     return new CSH1106SPIDisplay(
                         pSPI,
                         cfg.resolution.width,
                         cfg.resolution.height,
                         cfg.pinDC,
                         cfg.pinReset,
                         cfg.spiSpeed
                     );
                 });

    // ───────────────────────────────────────────────
    // SPI: SSD1305
    // ───────────────────────────────────────────────
    reg.Register(DisplayConfig::Bus::SPI, 
                 DisplayConfig::Controller::SSD1305,
                 [](const DisplayConfig& cfg, CI2CMaster*, CSPIMaster* pSPI) {
                     return new CSSD1305SPIDisplay(
                         pSPI,
                         cfg.resolution.width,
                         cfg.resolution.height,
                         cfg.pinDC,
                         cfg.pinReset,
                         cfg.spiSpeed
                     );
                 });

    // ───────────────────────────────────────────────
    // SPI: SSD1306
    // ───────────────────────────────────────────────
    reg.Register(DisplayConfig::Bus::SPI,
                 DisplayConfig::Controller::SSD1306,
                 [](const DisplayConfig& cfg, CI2CMaster*, CSPIMaster* pSPI) {
                     return new CSSD1306SPIDisplay(
                         pSPI,
                         cfg.resolution.width,
                         cfg.resolution.height,
                         cfg.pinDC,
                         cfg.pinReset,
                         cfg.spiSpeed
                     );
                 });

    // ───────────────────────────────────────────────
    // AUTO: I2C → SSD1306
    // ───────────────────────────────────────────────
    reg.Register(DisplayConfig::Bus::I2C,
                 DisplayConfig::Controller::Auto,
                 [](const DisplayConfig& cfg, CI2CMaster* pI2C, CSPIMaster*) {
                     return new CSSD1306Display(
                         pI2C,
                         cfg.resolution.width,
                         cfg.resolution.height,
                         cfg.i2cAddress,
                         cfg.spiSpeed
                     );
                 });

    // ───────────────────────────────────────────────
    // AUTO: SPI → SH1106
    // ───────────────────────────────────────────────
    reg.Register(DisplayConfig::Bus::SPI,
                 DisplayConfig::Controller::Auto,
                 [](const DisplayConfig& cfg, CI2CMaster*, CSPIMaster* pSPI) {
                     return new CSH1106SPIDisplay(
                         pSPI,
                         cfg.resolution.width,
                         cfg.resolution.height,
                         cfg.pinDC,
                         cfg.pinReset,
                         cfg.spiSpeed
                     );
                 });

    // ───────────────────────────────────────────────
    // SPI: EPD2in13V4 (E-Paper)
    // ───────────────────────────────────────────────
    reg.Register(DisplayConfig::Bus::SPI,
                 DisplayConfig::Controller::EPD2IN13V4,
                 [](const DisplayConfig& cfg, CI2CMaster*, CSPIMaster* pSPI) {
                     auto* epd = new CEpd2in13V4Display(
                         pSPI,
                         cfg.pinDC,
                         cfg.epdResetPin,
                         cfg.epdBusyPin,
                         cfg.epdChipSelect,
                         0, 0, 4000000
                     );
                     if (!epd->Initialize(cfg.rotation)) {
                         delete epd;
                         return (CDisplay*)nullptr;
                     }
                     return (CDisplay*)epd;
                 });

    return true;
}();
