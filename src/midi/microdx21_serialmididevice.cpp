//
// microdx21_serialmididevice.cpp
//

#include "microdx21_serialmididevice.h"
#include "config.h"
#include "microdx21.h"
#include <circle/logger.h>
#include <cstring>

LOGMODULE("uartmidi");

#define UART_DEVICE 0   // Circle UART0 = GPIO14/15

CMicroDX21SerialMIDIDevice::CMicroDX21SerialMIDIDevice(CMicroDX21* pSynth,
                                                 CInterruptSystem* pInterrupt,
                                                 CConfig* pConfig)
: CMicroDX21MIDIDevice(pSynth, pConfig)
, m_pConfig(pConfig)
, m_serial(pInterrupt, TRUE, UART_DEVICE)
, m_txIn(0)
, m_txOut(0)
, m_status(0)
, m_dataIndex(0)
, m_sysexIndex(0)
, m_inSysEx(false)
, m_nLedOffTime(0)
, m_bLedActive(false)
{
}

CMicroDX21SerialMIDIDevice::~CMicroDX21SerialMIDIDevice()
{
}

bool CMicroDX21SerialMIDIDevice::Initialize()
{
    bool ok = m_serial.Initialize(m_pConfig->GetMIDIBaudRate());
    if (!ok)
        return false;

    unsigned opts = m_serial.GetOptions();
    opts &= ~(SERIAL_OPTION_ONLCR); // disable CR→CRLF translation
    m_serial.SetOptions(opts);

    return true;
}

// ──────────────────────────────────────────────────────────────
// Drain our TX ring into CSerialDevice, honouring its return value.
//
// CSerialDevice has its own 2 KB internal TX ring fed by an ISR
// at the wire rate (3125 B/s @ 31250 baud).  When that ring is
// full, CSerialDevice::Write(buf, n) returns < n.
//
// We must NOT advance m_txOut by anything more than CSerialDevice
// actually accepted — otherwise the un-accepted bytes are lost.
//
// Strategy: write at most one contiguous slice per call (no wrap),
// because CSerialDevice::Write() takes a flat buffer.  If the ring
// is wrapped, we'll catch the second half on the next Process().
// ──────────────────────────────────────────────────────────────
void CMicroDX21SerialMIDIDevice::DrainTx()
{
    unsigned in, out;

    m_txSpinLock.Acquire();
    in  = m_txIn;
    out = m_txOut;
    m_txSpinLock.Release();

    if (in == out)
        return; // empty

    // Compute size of the contiguous slice (without wrap).
    size_t nContig = (in > out) ? (in - out) : (TxBufferSize - out);

    // Write up to nContig bytes — CSerialDevice may accept fewer.
    int nWritten = m_serial.Write(&m_txBuffer[out], nContig);
    if (nWritten <= 0)
        return;

    m_txSpinLock.Acquire();
    m_txOut = (m_txOut + (unsigned)nWritten) & TxBufferMask;
    m_txSpinLock.Release();
}

void CMicroDX21SerialMIDIDevice::Process()
{
    DrainTx();

    u8 buf[128];
    int n = m_serial.Read(buf, sizeof(buf));
    if (n > 0)
    {
        for (int i = 0; i < n; i++)
            HandleIncomingByte(buf[i]);
    }

    if (m_bLedActive && CTimer::Get()->GetClockTicks64() >= m_nLedOffTime)
    {
        CActLED::Get()->Off();
        m_bLedActive = false;
    }
}

static void FlashLed(u64& offTime, bool& active, unsigned durationUs)
{
    u64 now = CTimer::Get()->GetClockTicks64();
    offTime = now + durationUs;
    if (!active)
    {
        CActLED::Get()->On();
        active = true;
    }
}

// Called by ForwardMIDI() / SysEx response callback.
// Enqueue into our TX ring; bytes are drained later in Process().
//
// On overflow we drop the *remaining* bytes of this message and log
// once.  A 64 KiB ring covers any plausible SysEx response burst
// (GET_PRESET_INDEX = ~4.8 KiB), so overflow indicates a real
// downstream stall (e.g. UART cable disconnected).
void CMicroDX21SerialMIDIDevice::Send(const u8* msg, size_t len, unsigned cable)
{
    if (!msg || len == 0)
        return;

    bool overflow = false;

    m_txSpinLock.Acquire();

    for (size_t i = 0; i < len; i++)
    {
        unsigned next = (m_txIn + 1) & TxBufferMask;
        if (next == m_txOut)
        {
            overflow = true;
            break;          // ring full — drop remaining bytes
        }
        m_txBuffer[m_txIn] = msg[i];
        m_txIn = next;
    }

    m_txSpinLock.Release();

    if (overflow)
        LOGWARN("UART TX ring full, dropped bytes (UART/Pico stalled?)");
}

void CMicroDX21SerialMIDIDevice::HandleIncomingByte(u8 byte)
{
    // ───────────────────────────────────────────────
    // System Real-Time messages (0xF8–0xFF except SysEx)
    // These can appear ANYWHERE and must be forwarded immediately.
    // ───────────────────────────────────────────────
    if (byte >= 0xF8)
    {
        FlashLed(m_nLedOffTime, m_bLedActive, 50000);
        HandleMIDI(&byte, 1);
        return;
    }

    // ───────────────────────────────────────────────
    // SysEx handling
    // ───────────────────────────────────────────────
    if (byte == 0xF0)
    {
        m_inSysEx = true;
        m_sysexIndex = 0;
        m_sysex[m_sysexIndex++] = byte;
        return;
    }

    if (m_inSysEx)
    {
        if (byte == 0xF7)
        {
            m_sysex[m_sysexIndex++] = byte;
            FlashLed(m_nLedOffTime, m_bLedActive, 50000);
            HandleMIDI(m_sysex, m_sysexIndex);
            m_inSysEx = false;
            return;
        }

        if (m_sysexIndex < (int)sizeof(m_sysex))
            m_sysex[m_sysexIndex++] = byte;

        return;
    }

    // ───────────────────────────────────────────────
    // Status byte (0x80–0xEF) — Channel Voice messages update running status.
    // System Common (0xF1–0xF7) must NOT affect running status or the
    // parser will wait for the wrong number of data bytes and desync.
    // ───────────────────────────────────────────────
    if (byte & 0x80)
    {
        if (byte < 0xF0)
        {
            m_status = byte;
            m_dataIndex = 0;
            return;
        }

        // 0xF0 handled above (SysEx); 0xF8–0xFF handled at top (Real-Time).
        // Remaining 0xF1–0xF7: discard to avoid parser desync.
        return;
    }

    // ───────────────────────────────────────────────
    // Data byte
    // ───────────────────────────────────────────────
    if (m_status == 0)
        return; // ignore stray data

    m_data[m_dataIndex++] = byte;

    int needed =
        ((m_status & 0xE0) == 0xC0 || (m_status & 0xE0) == 0xD0)
        ? 1  // Program Change, Channel Aftertouch
        : 2; // all others

    if (m_dataIndex >= needed)
    {
        u8 msg[3];
        msg[0] = m_status;
        msg[1] = m_data[0];
        msg[2] = (needed == 2 ? m_data[1] : 0);

        // 200ms flash for Note On/Off, 50ms for all other channel messages
        bool isNote = (m_status & 0xF0) == 0x90 || (m_status & 0xF0) == 0x80;
        FlashLed(m_nLedOffTime, m_bLedActive, isNote ? 200000 : 50000);

        HandleMIDI(msg, needed + 1);

        m_dataIndex = 0;
    }
}
