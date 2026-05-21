#ifndef DISPLAY_REGISTRY_H
#define DISPLAY_REGISTRY_H

#include <functional>
#include <map>
#include "displayconfig.h"
#include <circle/display.h>

#include "sh1106display.h"
#include "ssd1305display.h"
#include "ssd1306display.h"
#include "sh1106spidisplay.h"
#include "ssd1305spidisplay.h"
#include "ssd1306spidisplay.h"
#include <epd2in13v4display.h>
#include <circle/screen.h>

class DisplayRegistry
{
public:
    using FactoryFunc = std::function<CDisplay*(const DisplayConfig&,
                                                CI2CMaster*,
                                                CSPIMaster*)>;

    static DisplayRegistry& Instance()
    {
        static DisplayRegistry inst;
        return inst;
    }

    void Register(DisplayConfig::Bus bus,
                  DisplayConfig::Controller ctrl,
                  FactoryFunc func)
    {
        m_Factories[{bus, ctrl}] = func;
    }

    CDisplay* Create(const DisplayConfig& cfg,
                     CI2CMaster* pI2C,
                     CSPIMaster* pSPI)
    {
        auto it = m_Factories.find({cfg.bus, cfg.controller});
        if (it != m_Factories.end())
            return it->second(cfg, pI2C, pSPI);

        // Fallback: Controller Auto → nur Bus matchen
        for (auto& [key, func] : m_Factories)
        {
            if (key.first == cfg.bus)
                return func(cfg, pI2C, pSPI);
        }

        return nullptr;
    }

private:
    using Key = std::pair<DisplayConfig::Bus, DisplayConfig::Controller>;
    std::map<Key, FactoryFunc> m_Factories;
};

#endif
