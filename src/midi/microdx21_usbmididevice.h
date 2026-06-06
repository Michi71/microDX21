#ifndef _VELVET_USBMIDIDEVICE_H
#define _VELVET_USBMIDIDEVICE_H

#include "microdx21_mididevice.h"
#include <circle/usb/usbmidi.h>
#include <circle/device.h>

class CMicroDX21USBMIDIDevice : public CMicroDX21MIDIDevice
{
public:
    CMicroDX21USBMIDIDevice(CMicroDX21* pSynth,
                         CConfig* pConfig,
                         unsigned instance = 0);

    void Process(bool plugAndPlayUpdated);
    void Send(const u8* msg, size_t len, unsigned cable = 0) override;

private:
    static void PacketHandler(unsigned cable, u8* packet, unsigned length,
                              unsigned device, void* param);
    static void DeviceRemoved(CDevice* dev, void* ctx);

    void HandleUSBPacket(u8* packet, unsigned length, unsigned cable, unsigned device);

private:
    unsigned m_instance;

    CUSBMIDIDevice* volatile m_pUSBDevice;

    u8 m_sysexBuffer[4096];
    unsigned m_sysexIndex;

    struct SendQueueEntry {
        u8 data[16];
        size_t len;
        unsigned cable;
    };

    static const unsigned SendQueueSize = 32;
    SendQueueEntry m_sendQueue[SendQueueSize];
    unsigned m_sendHead = 0;
    unsigned m_sendTail = 0;
};

#endif
