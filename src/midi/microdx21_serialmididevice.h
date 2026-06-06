//
// microdx21_serialmididevice.h
//

#ifndef _VELVET_SERIALMIDIDEVICE_H
#define _VELVET_SERIALMIDIDEVICE_H

#include "microdx21_mididevice.h"
#include <circle/actled.h>
#include <circle/interrupt.h>
#include <circle/serial.h>
#include <circle/spinlock.h>
#include <circle/timer.h>

class CConfig;
class CUserInterface;
class CMicroDX21;

class CMicroDX21SerialMIDIDevice : public CMicroDX21MIDIDevice
{
public:
    CMicroDX21SerialMIDIDevice(CMicroDX21* pSynth,
                            CInterruptSystem* pInterrupt,
                            CConfig* pConfig);

    ~CMicroDX21SerialMIDIDevice();

    bool Initialize();
    void Process();
    void Send(const u8* msg, size_t len, unsigned cable = 0) override;

private:
    void HandleIncomingByte(u8 byte);
    // Drain our own TX ring buffer into CSerialDevice, honouring its
    // back-pressure (i.e. only advancing the read pointer by the number
    // of bytes that CSerialDevice actually accepted).
    void DrainTx();

private:
    CConfig* m_pConfig;

    CSerialDevice m_serial;

    // ──────────────────────────────────────────────────────────
    // Custom TX ring buffer — replaces CWriteBufferDevice.
    //
    // Rationale: Circle's CWriteBufferDevice::Update() ignores the
    // return value of the underlying CSerialDevice::Write() call,
    // which silently drops bytes when the serial driver's internal
    // 2 KB buffer is saturated.  At MIDI baud (31250) the host
    // loop calls Update() much faster than the UART can drain,
    // so a multi-chunk SysEx response (e.g. GET_PRESET_INDEX,
    // ~4.7 KB) loses ~97 % of its bytes after the first ~20 ms
    // and never reaches the Pico/Browser.
    //
    // We keep our own large ring buffer here and drain it via
    // direct CSerialDevice::Write() calls, advancing m_txOut only
    // by the byte count that the serial driver actually accepted.
    // ──────────────────────────────────────────────────────────
    static constexpr size_t TxBufferSize = 65536;     // power of 2
    static constexpr size_t TxBufferMask = TxBufferSize - 1;
    u8                m_txBuffer[TxBufferSize];
    volatile unsigned m_txIn;
    volatile unsigned m_txOut;
    CSpinLock         m_txSpinLock;

    // Running status parser
    u8  m_status;
    u8  m_data[2];
    int m_dataIndex;

    // SysEx buffer (incoming from UART)
    u8  m_sysex[65536];
    int m_sysexIndex;

    bool m_inSysEx;

    // LED flash on incoming data
    u64  m_nLedOffTime;
    bool m_bLedActive;
};

#endif
