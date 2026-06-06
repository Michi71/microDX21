//
// microdx21_pckeyboard.h
//

#ifndef _VELVET_PCKEYBOARD_H
#define _VELVET_PCKEYBOARD_H

#include "microdx21_mididevice.h"
#include <circle/usb/usbkeyboard.h>
#include <circle/device.h>
#include <circle/types.h>

class CConfig;
class CUserInterface;
class CMicroDX21;

class CMicroDX21PCKeyboard : public CMicroDX21MIDIDevice
{
public:
    CMicroDX21PCKeyboard(CMicroDX21* pSynth,
                      CConfig* pConfig);

    ~CMicroDX21PCKeyboard();

    void Process(bool plugAndPlayUpdated);

private:
    static void KeyStatusHandlerRaw(unsigned char modifiers,
                                    const unsigned char rawKeys[6]);

    static u8 TranslateKeyToMIDINote(u8 keycode);
    static bool Contains(const u8* arr, u8 val, unsigned len);

    static void DeviceRemoved(CDevice* dev, void* ctx);

private:
    CUSBKeyboardDevice* volatile m_pKeyboard;

    u8 m_lastKeys[6];

    static CMicroDX21PCKeyboard* s_pThis;
};

#endif
