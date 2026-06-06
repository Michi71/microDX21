#include "microdx21_usbmididevice.h"
#include <circle/devicenameservice.h>
#include <cstring>
#include <assert.h>

CMicroDX21USBMIDIDevice::CMicroDX21USBMIDIDevice(CMicroDX21* pSynth,
                                           CConfig* pConfig,
                                           unsigned instance)
: CMicroDX21MIDIDevice(pSynth, pConfig)
, m_instance(instance)
, m_pUSBDevice(nullptr)
, m_sysexIndex(0)
{
    m_deviceName = "umidi" + std::to_string(instance + 1);
}

void CMicroDX21USBMIDIDevice::Process(bool plugAndPlayUpdated)
{
    // Flush outgoing queue (allocation-free) — muss immer laufen,
    // auch ohne PlugAndPlay-Update (Gadget-Mode feuert nie PnP).
    while (m_sendHead != m_sendTail)
    {
        auto& e = m_sendQueue[m_sendTail];
        if (m_pUSBDevice)
            m_pUSBDevice->SendPlainMIDI(e.cable, e.data, e.len);
        m_sendTail = (m_sendTail + 1) % SendQueueSize;
    }

    if (!plugAndPlayUpdated)
        return;

    // Try to find device if not already connected.
    if (!m_pUSBDevice)
    {
        m_pUSBDevice = (CUSBMIDIDevice*)
            CDeviceNameService::Get()->GetDevice(m_deviceName.c_str(), FALSE);

        if (m_pUSBDevice)
        {
            m_pUSBDevice->RegisterPacketHandler(PacketHandler, this);
            m_pUSBDevice->RegisterRemovedHandler(DeviceRemoved, this);
        }
    }
}

void CMicroDX21USBMIDIDevice::Send(const u8* msg, size_t len, unsigned cable)
{
    if (len > sizeof(SendQueueEntry::data))
        return; // Message too large for queue slot

    unsigned nextHead = (m_sendHead + 1) % SendQueueSize;
    if (nextHead == m_sendTail)
        return; // Queue full

    SendQueueEntry& entry = m_sendQueue[m_sendHead];
    memcpy(entry.data, msg, len);
    entry.len = len;
    entry.cable = cable;
    m_sendHead = nextHead;
}

void CMicroDX21USBMIDIDevice::PacketHandler(unsigned cable, u8* packet,
                                         unsigned length, unsigned device,
                                         void* param)
{
    auto* self = static_cast<CMicroDX21USBMIDIDevice*>(param);
    self->HandleUSBPacket(packet, length, cable, device);
}

void CMicroDX21USBMIDIDevice::DeviceRemoved(CDevice*, void* ctx)
{
    auto* self = static_cast<CMicroDX21USBMIDIDevice*>(ctx);
    self->m_pUSBDevice = nullptr;
}

void CMicroDX21USBMIDIDevice::HandleUSBPacket(u8* packet, unsigned length,
                                           unsigned cable, unsigned device)
{
    if (length == 0 || !packet)
        return;

    if (packet[0] == 0xF0 && m_sysexIndex == 0)
    {
        if (length > sizeof(m_sysexBuffer))
        {
            m_sysexIndex = 0;
            return;
        }
        memcpy(m_sysexBuffer, packet, length);
        m_sysexIndex = length;
        return;
    }

    if (m_sysexIndex > 0)
    {
        for (unsigned i = 0; i < length; i++)
        {
            u8 b = packet[i];

            if (m_sysexIndex >= sizeof(m_sysexBuffer))
            {
                m_sysexIndex = 0;
                return;
            }

            if (b == 0xF7)
            {
                m_sysexBuffer[m_sysexIndex++] = b;
                HandleMIDI(m_sysexBuffer, m_sysexIndex, cable);
                m_sysexIndex = 0;
                return;
            }

            if (b & 0x80)
            {
                m_sysexIndex = 0;
                return;
            }

            m_sysexBuffer[m_sysexIndex++] = b;
        }
        return;
    }

    HandleMIDI(packet, length, cable);
}
