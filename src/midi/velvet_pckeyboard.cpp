//
// velvet_pckeyboard.cpp
//

#include "velvet_pckeyboard.h"
#include "velvetkeys.h"
#include "config.h"
#include <circle/devicenameservice.h>
#include <circle/logger.h>
#include <cstring>
#include <assert.h>

LOGMODULE("pckbd");

CVelvetPCKeyboard* CVelvetPCKeyboard::s_pThis = nullptr;

CVelvetPCKeyboard::CVelvetPCKeyboard(CVelvetKeys* pSynth,
                                     CConfig* pConfig)
: CVelvetMIDIDevice(pSynth, pConfig)
, m_pKeyboard(nullptr)
{
    memset(m_lastKeys, 0, sizeof(m_lastKeys));
    s_pThis = this;
}

CVelvetPCKeyboard::~CVelvetPCKeyboard()
{
    s_pThis = nullptr;
}

void CVelvetPCKeyboard::Process(bool plugAndPlayUpdated)
{
    if (!plugAndPlayUpdated)
        return;

    if (!m_pKeyboard)
    {
        m_pKeyboard = (CUSBKeyboardDevice*)
            CDeviceNameService::Get()->GetDevice("ukbd1", FALSE);

        if (m_pKeyboard)
        {
            m_pKeyboard->RegisterKeyStatusHandlerRaw(KeyStatusHandlerRaw);
            m_pKeyboard->RegisterRemovedHandler(DeviceRemoved);
        }
    }
}

void CVelvetPCKeyboard::KeyStatusHandlerRaw(unsigned char modifiers,
                                            const unsigned char rawKeys[6])
{
    assert(s_pThis);

    // Released keys
    for (unsigned i = 0; i < 6; i++)
    {
        u8 oldKey = s_pThis->m_lastKeys[i];
        if (oldKey != 0 && !Contains(rawKeys, oldKey, 6))
        {
            u8 note = TranslateKeyToMIDINote(oldKey);
            if (note != 0)
            {
                u8 msg[3] = {0x80, note, 0};
                s_pThis->HandleMIDI(msg, 3);
            }
        }
    }

    // Pressed keys
    for (unsigned i = 0; i < 6; i++)
    {
        u8 newKey = rawKeys[i];
        if (newKey != 0 && !Contains(s_pThis->m_lastKeys, newKey, 6))
        {
            u8 note = TranslateKeyToMIDINote(newKey);
            if (note != 0)
            {
                u8 msg[3] = {0x90, note, 100};
                s_pThis->HandleMIDI(msg, 3);
            }
        }
    }

    memcpy(s_pThis->m_lastKeys, rawKeys, sizeof(s_pThis->m_lastKeys));
}

bool CVelvetPCKeyboard::Contains(const u8* arr, u8 val, unsigned len)
{
    for (unsigned i = 0; i < len; i++)
        if (arr[i] == val)
            return true;
    return false;
}

// Mapping USB keycodes → MIDI notes
u8 CVelvetPCKeyboard::TranslateKeyToMIDINote(u8 keycode)
{
    // USB HID → ASCII mapping
    char ch;

    if (keycode >= 0x04 && keycode <= 0x1D)
        ch = 'A' + (keycode - 0x04);
    else if (keycode >= 0x1E && keycode <= 0x26)
        ch = '1' + (keycode - 0x1E);
    else if (keycode == 0x36)
        ch = ',';
    else
        return 0;

    // QWERTY piano layout (same as MiniDexed)
    struct KeyMap { char key; u8 note; };
    static const KeyMap table[] = {
        {',', 72}, {'M', 71}, {'J', 70}, {'N', 69}, {'H', 68}, {'B', 67},
        {'G', 66}, {'V', 65}, {'C', 64}, {'D', 63}, {'X', 62}, {'S', 61},
        {'Z', 60}, {'U', 59}, {'7', 58}, {'Y', 57}, {'6', 56}, {'T', 55},
        {'5', 54}, {'R', 53}, {'E', 52}, {'3', 51}, {'W', 50}, {'2', 49},
        {'Q', 48}
    };

    for (auto& m : table)
        if (m.key == ch)
            return m.note;

    return 0;
}

void CVelvetPCKeyboard::DeviceRemoved(CDevice* dev, void* ctx)
{
    auto* self = static_cast<CVelvetPCKeyboard*>(ctx);
    self->m_pKeyboard = nullptr;
}
